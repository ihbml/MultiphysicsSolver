#pragma once
// ============================================================================
//  fvm/core.hpp
//  Zero-copy C-ABI SoA Data Lake (CAbiSpan) + 4-Quadrant Hilbert-sorted
//  CellFaceCSR + strict 2-class geometry contracts + Rhie-Chow face mass-flux
//  reconstruction + pressure-correction Poisson assembly.
//  C++20, header-only. All CellFaceCSR gathers are deterministic and
//  atomic-free: exactly one thread/warp owns a cell write, face fluxes are
//  computed once per face and gathered by both adjacent cells.
// ============================================================================
#include <cstdint>
#include <cstddef>
#include <cmath>
#include <vector>
#include <algorithm>
#include <cstring>
#include <numeric>
#include <cstdio>

namespace fvm {

// ---------------------------------------------------------------- vector3 --
struct V3 { double x = 0, y = 0, z = 0; };
inline V3 sub(V3 a, V3 b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
inline V3 add(V3 a, V3 b) { return {a.x + b.x, a.y + b.y, a.z + b.z}; }
inline V3 mul(V3 a, double s) { return {a.x * s, a.y * s, a.z * s}; }
inline double dot(V3 a, V3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
inline double len(V3 a) { return std::sqrt(dot(a, a)); }

// ------------------------------------------- 4-quadrant face segmentation --
// Seg 0: inner_fast      Seg 1: inner_corrected
// Seg 2: halo_fast       Seg 3: halo_corrected
// Faces inside every segment are Hilbert-sorted for coalesced access.
enum FaceSeg : int32_t {
  SEG_INNER_FAST = 0,
  SEG_INNER_CORRECTED = 1,
  SEG_HALO_FAST = 2,
  SEG_HALO_CORRECTED = 3,
  N_SEG = 4
};
inline const char* seg_name(int s) {
  static const char* n[N_SEG] = {"inner_fast", "inner_corrected", "halo_fast", "halo_corrected"};
  return n[s];
}

// ------------------------------------------------------- C-ABI data lake --
// Raw-pointer span over the in-memory SoA Data Lake. The solver never copies
// field data out of the lake; it reads/writes through these views only.
struct CAbiSpan {
  int32_t n_cells = 0, n_owned = 0, n_faces = 0;
  int32_t seg_begin[N_SEG + 1] = {0, 0, 0, 0, 0};  // quadrant offsets into face arrays

  // primary SoA fields (one contiguous array per field)
  double* u = nullptr, * v = nullptr, * w = nullptr;   // velocity
  double* p = nullptr;                                   // pressure
  double* T = nullptr, * h = nullptr;                    // temperature / enthalpy
  double* rho = nullptr, * mu = nullptr;                 // density / viscosity
  double* gamma = nullptr;                               // Brinkman porosity in [0,1]
  double* vol = nullptr;                                 // cell volumes

  // geometry SoA
  double* cx = nullptr, * cy = nullptr, * cz = nullptr;  // cell centroids x_P
  double* fx = nullptr, * fy = nullptr, * fz = nullptr;  // face centroids x_f
  double* sx = nullptr, * sy = nullptr, * sz = nullptr;  // face area vectors S_f
  int32_t* owner = nullptr, * neighbour = nullptr;       // per face (neighbour=-1: boundary)
  int32_t* face_seg = nullptr;                           // quadrant of each face

  // CellFaceCSR: owned cell c -> faces cf_list[cf_ptr[c] .. cf_ptr[c+1]),
  // grouped by quadrant (seg 0 .. seg 3) in that order.
  int32_t* cf_ptr = nullptr, * cf_list = nullptr;

  // compact CorrectionSoA: 7 doubles per face (~6% of the primary SoA
  // footprint): c_s = deferred-correction vector, c_k = non-orthogonal
  // correction, c_omega = under-relaxation factor.
  double* c_sx = nullptr, * c_sy = nullptr, * c_sz = nullptr;
  double* c_kx = nullptr, * c_ky = nullptr, * c_kz = nullptr;
  double* c_omega = nullptr;
};

// ------------------------------------------------------ Hilbert ordering --
// 2D Hilbert (xy2d, public-domain algorithm) and 3D Morton fallback, used to
// sort face indices inside each quadrant segment.
inline void hilbert_rot(int n, int* x, int* y, int rx, int ry) {
  if (ry == 0) {
    if (rx == 1) { *x = n - 1 - *x; *y = n - 1 - *y; }
    int t = *x; *x = *y; *y = t;
  }
}
inline uint64_t hilbert_xy2d(int n, int x, int y) {
  uint64_t d = 0;
  for (int s = n / 2, rx, ry; s > 0; s /= 2) {
    rx = (x & s) ? 1 : 0;
    ry = (y & s) ? 1 : 0;
    d += (uint64_t)s * (uint64_t)s * (uint64_t)((3 * rx) ^ ry);
    hilbert_rot(s * 2 ? n : n, &x, &y, rx, ry);  // rotate within current square of side n
  }
  return d;
}
inline uint64_t morton3d(uint32_t x, uint32_t y, uint32_t z) {
  auto part = [](uint32_t v) {
    v = (v | (v << 16)) & 0x030000FFu;
    v = (v | (v << 8)) & 0x0300F00Fu;
    v = (v | (v << 4)) & 0x030C30C3u;
    v = (v | (v << 2)) & 0x09249249u;
    return v;
  };
  return (uint64_t)part(x) | ((uint64_t)part(y) << 1) | ((uint64_t)part(z) << 2);
}

// --------------------------------------------- CellFaceCSR construction ---
// Deterministic and atomic-free: two sequential passes (count, then fill).
// For each owned cell the face list is grouped by quadrant seg 0..3, and
// inside each quadrant ordered by Hilbert key of the face centroid.
inline void csr_count_and_fill(CAbiSpan& L, int32_t* cf_ptr, int32_t* cf_list) {
  const int n = L.n_owned;
  std::vector<int32_t> cnt(n + 1, 0);
  for (int f = 0; f < L.n_faces; ++f) {
    const int o = L.owner[f], nb = L.neighbour[f];
    if (o >= 0 && o < n) cnt[o + 1]++;
    if (nb >= 0 && nb < n && nb != o) cnt[nb + 1]++;
  }
  for (int i = 0; i < n; ++i) cnt[i + 1] += cnt[i];
  for (int i = 0; i <= n; ++i) cf_ptr[i] = cnt[i];
  std::vector<int32_t> cur(cf_ptr, cf_ptr + n);
  // quadrant-major, Hilbert-sorted within quadrant
  for (int seg = 0; seg < N_SEG; ++seg) {
    const int b = L.seg_begin[seg], e = L.seg_begin[seg + 1];
    std::vector<int32_t> idx;
    idx.reserve(e - b);
    for (int f = b; f < e; ++f) idx.push_back(f);
    std::sort(idx.begin(), idx.end(), [&](int a, int b2) {
      uint64_t ka = morton3d((uint32_t)(L.fx[a] * 4096), (uint32_t)(L.fy[a] * 4096), (uint32_t)(L.fz[a] * 4096));
      uint64_t kb = morton3d((uint32_t)(L.fx[b2] * 4096), (uint32_t)(L.fy[b2] * 4096), (uint32_t)(L.fz[b2] * 4096));
      return ka < kb;
    });
    for (int f : idx) {
      const int o = L.owner[f], nb = L.neighbour[f];
      if (o >= 0 && o < n) cf_list[cur[o]++] = f;
      if (nb >= 0 && nb < n && nb != o) cf_list[cur[nb]++] = f;
    }
  }
  for (int i = 0; i <= n; ++i) cf_ptr[i] = cnt[i];
}

// Convenience wrapper: builds the CSR in-place into the lake's own arrays.
inline void build_cell_face_csr(CAbiSpan& L) {
  csr_count_and_fill(L, L.cf_ptr, L.cf_list);
}

// ------------------------------------------------- strict geometry classes --
// Class contract 1: linear-exact plane intersection weight
//   beta_f = ((x_f - x_P) . S_f) / (d_f . S_f)
inline V3 cell_c(const CAbiSpan& L, int c) { return {L.cx[c], L.cy[c], L.cz[c]}; }
inline V3 face_c(const CAbiSpan& L, int f) { return {L.fx[f], L.fy[f], L.fz[f]}; }
inline V3 face_S(const CAbiSpan& L, int f) { return {L.sx[f], L.sy[f], L.sz[f]}; }
inline V3 face_d(const CAbiSpan& L, int f) {
  const int P = L.owner[f], N = L.neighbour[f];
  if (N >= 0) return sub(cell_c(L, N), cell_c(L, P));
  return mul(sub(face_c(L, f), cell_c(L, P)), 2.0);  // boundary: mirrored ghost
}
inline double beta_face(const CAbiSpan& L, int f) {
  const V3 S = face_S(L, f), d = face_d(L, f);
  return dot(sub(face_c(L, f), cell_c(L, L.owner[f])), S) / dot(d, S);
}
// Class contract 2: over-relaxed conductance
//   alpha_f = ||S_f||^2 / (d_f . S_f)
inline double alpha_face(const CAbiSpan& L, int f) {
  const V3 S = face_S(L, f), d = face_d(L, f);
  return dot(S, S) / dot(d, S);
}
// linear interpolation weight of the face between owner and neighbour
inline double theta_face(const CAbiSpan& L, int f) {
  const V3 d = face_d(L, f);
  return dot(sub(face_c(L, f), cell_c(L, L.owner[f])), d) / dot(d, d);
}

// ------------------------------------------- Green-Gauss cell gradients ---
// grad_P = (1/V_P) sum_f s_f phi_f S_f,  phi_f linearly interpolated (beta-exact)
inline V3 grad_gg(const CAbiSpan& L, const double* phi, int c) {
  V3 g{0, 0, 0};
  for (int k = L.cf_ptr[c]; k < L.cf_ptr[c + 1]; ++k) {
    const int f = L.cf_list[k];
    const int P = L.owner[f], N = L.neighbour[f];
    const double s = (P == c) ? 1.0 : -1.0;
    const double th = theta_face(L, f);
    const double pN = (N >= 0) ? phi[N] : phi[P];
    const double phif = phi[P] * (1.0 - th) + pN * th;
    g = add(g, mul(face_S(L, f), s * phif));
  }
  return mul(g, 1.0 / L.vol[c]);
}

// ------------------------------------------------- Rhie-Chow face flux -----
// Over-relaxed face pressure-gradient flux:
//   G_f = (grad p)_f . S_f
//       = 	g_f.S_f + alpha_f ( p_N - p_P - 	g_f . d_f ),   	g_f = interp cell grads
// Mass flux (Rhie-Chow):
//   m_f = rho_f [ ubar_f.S_f - Dbar_f ( G_f - 	g_f.S_f ) ]
//       = rho_f [ ubar_f.S_f - Dbar_f alpha_f ( p_N - p_P - 	g_f.d_f ) ]
// which removes the odd-even (checkerboard) decoupling of collocated grids.
struct RCFace {
  double Gf;      // (grad p)_f . S_f  (over-relaxed)
  double gbarS;   // interpolated cell-gradient part
  double mdot;    // reconstructed mass flux
};
inline RCFace rhie_chow_face(const CAbiSpan& L, int f, const double* p, const V3* gradP,
                             const double* u, const double* v, const double* w,
                             double Dbar, double rho_f) {
  const int P = L.owner[f], N = L.neighbour[f];
  const V3 S = face_S(L, f), d = face_d(L, f);
  const double al = alpha_face(L, f);
  const double th = theta_face(L, f);
  const V3 gP = gradP[P];
  const V3 gN = (N >= 0) ? gradP[N] : gP;
  const V3 gb = add(mul(gP, 1.0 - th), mul(gN, th));
  const double pN = (N >= 0) ? p[N] : p[P];
  const double Gf = dot(gb, S) + al * (pN - p[P] - dot(gb, d));
  const V3 uf = add(mul(V3{L.u[P], L.v[P], L.w[P]}, 1.0 - th),
                    mul(V3{(N >= 0) ? L.u[N] : L.u[P], (N >= 0) ? L.v[N] : L.v[P],
                           (N >= 0) ? L.w[N] : L.w[P]}, th));
  const double mdot = rho_f * (dot(uf, S) - Dbar * (Gf - dot(gb, S)));
  return {Gf, dot(gb, S), mdot};
}

// ------------------------------------- pressure-correction Poisson (PISO) --
// Continuity: sum_f s_f m_f = 0 with m_f = m*_f - c_f (p'_N - p'_P) - deferred
// correction (Class 0: gradient-free upwind; the grad p' part is deferred).
//   c_f = rho_f * Dbar_f * alpha_f
// SPD system A p' = b with
//   A[P][P] += c_f (x2), A[P][N] -= c_f,  b[P] -= s_f m*_f (+ boundary flux),
// plus continuity diagonal stabilization  A[P][P] += stab * sum c_f.
struct PoissonSystem {
  int n = 0;
  std::vector<int32_t> ptr, col;
  std::vector<double> val, b;
};
inline PoissonSystem assemble_pressure_poisson(const CAbiSpan& L, const double* mstar,
                                                const double* Dbar, const double* rho,
                                                double stab = 1e-6) {
  const int n = L.n_owned;
  std::vector<std::vector<std::pair<int, double>>> rows(n);
  std::vector<double> b(n, 0.0), diag(n, 0.0);
  auto addA = [&](int i, int j, double v) { rows[i].push_back({j, v}); };
  for (int f = 0; f < L.n_faces; ++f) {
    const int P = L.owner[f], N = L.neighbour[f];
    if (P < 0 || P >= n) continue;
    const double rho_f = (N >= 0 && N < L.n_cells) ? 0.5 * (rho[P] + rho[N]) : rho[P];
    const double c = rho_f * Dbar[f] * alpha_face(L, f);
    const double s = (L.owner[f] == P) ? 1.0 : -1.0;  // owner-side sign
    if (N >= 0 && N < n) {
      addA(P, P, c); addA(P, N, -c);
      addA(N, N, c); addA(N, P, -c);
      b[P] -= mstar[f];
      b[N] += mstar[f];
      diag[P] += c; diag[N] += c;
    } else {
      // boundary face: prescribed flux m_b enters the rhs only
      b[P] += s * mstar[f];
      diag[P] += c;
    }
  }
  PoissonSystem S; S.n = n; S.b = std::move(b);
  S.ptr.assign(n + 1, 0);
  for (int i = 0; i < n; ++i) {
    std::sort(rows[i].begin(), rows[i].end());
    // merge duplicates, then add diagonal stabilization
    std::vector<std::pair<int, double>> m;
    for (auto& pr : rows[i]) {
      if (!m.empty() && m.back().first == pr.first) m.back().second += pr.second;
      else m.push_back(pr);
    }
    bool has_diag = false;
    for (auto& pr : m) if (pr.first == i) has_diag = true;
    if (!has_diag) m.push_back({i, 0.0});
    std::sort(m.begin(), m.end());
    for (auto& pr : m) {
      double vv = pr.second;
      if (pr.first == i) vv += stab * diag[i];
      S.col.push_back(pr.first); S.val.push_back(vv);
    }
    S.ptr[i + 1] = (int32_t)S.col.size();
  }
  return S;
}

// Correct face fluxes after solving A p' = b:  m_f = m*_f - c_f (p'_N - p'_P)
inline void apply_pressure_correction(const CAbiSpan& L, const PoissonSystem& S,
                                      const double* pprime, const double* mstar,
                                      const double* Dbar, const double* rho, double* mdot) {
  for (int f = 0; f < L.n_faces; ++f) {
    const int P = L.owner[f], N = L.neighbour[f];
    const double rho_f = (N >= 0 && N < L.n_cells) ? 0.5 * (rho[P] + rho[N]) : rho[P];
    const double c = rho_f * Dbar[f] * alpha_face(L, f);
    if (N >= 0 && N < L.n_owned) mdot[f] = mstar[f] - c * (pprime[N] - pprime[P]);
    else mdot[f] = mstar[f];  // prescribed boundary flux
  }
}

// Deterministic atomic-free CellFaceCSR residual/diagonal gather:
// per-cell continuity residual  D_c = sum_{f in CSR(c)} s_f m_f   (0 <= c < n_owned)
inline void gather_continuity(const CAbiSpan& L, const double* mdot, double* D) {
  for (int c = 0; c < L.n_owned; ++c) {
    double acc = 0.0;
    for (int k = L.cf_ptr[c]; k < L.cf_ptr[c + 1]; ++k) {
      const int f = L.cf_list[k];
      acc += (L.owner[f] == c ? 1.0 : -1.0) * mdot[f];
    }
    D[c] = acc;
  }
}

// ============================================================ 1D mesh ======
// Collocated 1D mesh used by the standalone verification test. Stretched
// (non-uniform) grid to exercise the beta_f / alpha_f contracts.
struct Lake1D {
  std::vector<double> u, v, w, p, T, h, rho, mu, gamma, vol;
  std::vector<double> cx, cy, cz, fx, fy, fz, sx, sy, sz;
  std::vector<double> c_sx, c_sy, c_sz, c_kx, c_ky, c_kz, c_om;
  std::vector<int32_t> owner, neighbour, face_seg, cf_ptr, cf_list;
  CAbiSpan L;

  void build(int N, double length, double stretch) {
    const int nf = N + 1;
    auto X = [&](int i) {  // stretched cell-center coordinate
      double s = (double)i / N;
      if (std::fabs(stretch) < 1e-12) return length * s;  // uniform limit
      return length * (std::exp(stretch * s) - 1.0) / (std::exp(stretch) - 1.0);
    };
    cx.assign(N, 0); vol.assign(N, 0);
    for (int i = 0; i < N; ++i) cx[i] = 0.5 * (X(i) + X(i + 1));
    fx.assign(nf, 0); sx.assign(nf, 1.0);
    for (int f = 0; f < nf; ++f) fx[f] = X(f);
    for (int i = 0; i < N; ++i) vol[i] = fx[i + 1] - fx[i];
    cy.assign(N, 0); cz.assign(N, 0);
    fy.assign(nf, 0); fz.assign(nf, 0); sy.assign(nf, 0); sz.assign(nf, 0);
    u.assign(N, 0); v.assign(N, 0); w.assign(N, 0); p.assign(N, 0);
    T.assign(N, 300); h.assign(N, 0); rho.assign(N, 1.0); mu.assign(N, 1e-5);
    gamma.assign(N, 1.0);
    owner.assign(nf, 0); neighbour.assign(nf, -1); face_seg.assign(nf, 0);
    for (int f = 0; f < nf; ++f) {
      if (f == 0) { owner[f] = 0; neighbour[f] = -1; face_seg[f] = SEG_INNER_CORRECTED; }
      else if (f == nf - 1) { owner[f] = N - 1; neighbour[f] = -1; face_seg[f] = SEG_INNER_CORRECTED; }
      else { owner[f] = f - 1; neighbour[f] = f; face_seg[f] = SEG_INNER_FAST; }
    }
    // quadrant offsets: seg0 = inner faces, seg1 = boundary (corrected), seg2/3 empty
    L.seg_begin[0] = 0;
    L.seg_begin[1] = nf - 2;           // inner faces occupy [0, nf-2) after reorder below
    L.seg_begin[2] = nf - 2;
    L.seg_begin[3] = nf;
    L.seg_begin[4] = nf;
    // physically reorder face arrays: seg0 inner_fast, seg1 inner_corrected
    {
      std::vector<int32_t> ord;
      for (int f = 0; f < nf; ++f) if (face_seg[f] == SEG_INNER_FAST) ord.push_back(f);
      for (int f = 0; f < nf; ++f) if (face_seg[f] == SEG_INNER_CORRECTED) ord.push_back(f);
      auto reorder = [&](std::vector<double>& a) {
        std::vector<double> b(nf);
        for (int i = 0; i < nf; ++i) b[i] = a[ord[i]];
        a = b;
      };
      auto reorderi = [&](std::vector<int32_t>& a) {
        std::vector<int32_t> b(nf);
        for (int i = 0; i < nf; ++i) b[i] = a[ord[i]];
        a = b;
      };
      reorder(fx); reorder(fy); reorder(fz); reorder(sx); reorder(sy); reorder(sz);
      reorderi(owner); reorderi(neighbour); reorderi(face_seg);
      L.seg_begin[0] = 0;
      L.seg_begin[1] = nf - 2;
      L.seg_begin[2] = nf - 2;
      L.seg_begin[3] = nf;
      L.seg_begin[4] = nf;
    }
    c_sx.assign(nf, 0); c_sy.assign(nf, 0); c_sz.assign(nf, 0);
    c_kx.assign(nf, 0); c_ky.assign(nf, 0); c_kz.assign(nf, 0);
    c_om.assign(nf, 1.0);
    cf_ptr.assign(N + 1, 0); cf_list.assign(2 * (nf - 2) + 2, 0);

    L.n_cells = N; L.n_owned = N; L.n_faces = nf;
    L.u = u.data(); L.v = v.data(); L.w = w.data(); L.p = p.data();
    L.T = T.data(); L.h = h.data(); L.rho = rho.data(); L.mu = mu.data();
    L.gamma = gamma.data(); L.vol = vol.data();
    L.cx = cx.data(); L.cy = cy.data(); L.cz = cz.data();
    L.fx = fx.data(); L.fy = fy.data(); L.fz = fz.data();
    L.sx = sx.data(); L.sy = sy.data(); L.sz = sz.data();
    L.owner = owner.data(); L.neighbour = neighbour.data(); L.face_seg = face_seg.data();
    L.cf_ptr = cf_ptr.data(); L.cf_list = cf_list.data();
    L.c_sx = c_sx.data(); L.c_sy = c_sy.data(); L.c_sz = c_sz.data();
    L.c_kx = c_kx.data(); L.c_ky = c_ky.data(); L.c_kz = c_kz.data();
    L.c_omega = c_om.data();
    csr_count_and_fill(L, cf_ptr.data(), cf_list.data());
  }
};

}  // namespace fvm
