// ============================================================================
//  PolyVV-20 :: FVM assembly — 3D advection-diffusion with Class-1 skewness
//  correction + implicit non-orthogonal correction (WLS gradients),
//  Method of Manufactured Solutions (Gauss quadrature source),
//  templated over scalar type T (double / std::complex<double>).
//  No mocked numbers: everything below is assembled from the real mesh.
// ============================================================================
#pragma once
#include <complex>
#include <vector>
#include <array>
#include <tuple>
#include <cmath>
#include <algorithm>
#include <numeric>
#include "mesh.h"

namespace vv {

constexpr double PI = 3.14159265358979323846;
constexpr double GAMMA_BASE = 0.1;   // diffusion coefficient used for MMS solves
constexpr double U0_BASE = 1.0;      // velocity scale
constexpr double SIGMA_BASE = 1.0;   // manufactured source scale

// ---------------- manufactured fields (analytic) ----------------
inline double phiExact(double x, double y, double z) {
  return std::sin(PI * x) * std::cos(PI * y) * std::exp(0.5 * z);
}
// div(u phi) with div(u) = 0 identically  =>  u . grad(phi)
//   phi = sin(px) cos(py) e^{z/2},  u = [sin(px)cos(py)cos(pz), -cos(px)sin(py)cos(pz), 0]
//   u1*px + u2*py = pi sin(px)cos(px) e^{z/2} (cos^2 py + sin^2 py) cos(pz)
//                 = (pi/2) sin(2 pi x) cos(pi z) e^{z/2}
inline double divUPhi(double x, double /*y*/, double z) {
  return 0.5 * PI * std::sin(2.0 * PI * x) * std::cos(PI * z) * std::exp(0.5 * z);
}
// ∇²phi = (1/4 - 2 pi^2) sin(pi x) cos(pi y) e^{z/2}
inline double lapPhi(double x, double y, double z) {
  return (0.25 - 2.0 * PI * PI) * std::sin(PI * x) * std::cos(PI * y) * std::exp(0.5 * z);
}
inline Vec3 uExact(double x, double y, double z) {
  return {std::sin(PI * x) * std::cos(PI * y) * std::cos(PI * z),
          -std::cos(PI * x) * std::sin(PI * y) * std::cos(PI * z), 0.0};
}
// Stokes momentum manufactured solution: U* = u_exact, p* = sin(pi x) sin(2 pi y) sin(pi z)
inline double pStokes(double x, double y, double z) {
  return std::sin(PI * x) * std::sin(2.0 * PI * y) * std::sin(PI * z);
}
// Body force for 0 = -grad(p) + nu ∇²U + f  with solution (U*, p*):
//   f = grad(p*) - nu ∇²U* = grad(p*) + 3 pi^2 nu U*   (each component has k^2 = 3 pi^2)
inline Vec3 fStokes(double x, double y, double z, double nu) {
  Vec3 U = uExact(x, y, z);
  Vec3 gp{PI * std::cos(PI * x) * std::sin(2 * PI * y) * std::sin(PI * z),
          2 * PI * std::sin(PI * x) * std::cos(2 * PI * y) * std::sin(PI * z),
          PI * std::sin(PI * x) * std::sin(2 * PI * y) * std::cos(PI * z)};
  return gp + U * (3.0 * PI * PI * nu);
}

// ---------------- Gauss-quadrature cell integrals (2x2x2) -------------------
struct QuadCell {
  double qDiv = 0;      // ∫ div(u phi) dV
  double qLap = 0;      // ∫ ∇²phi dV
  double phiStar = 0;   // phi* at cell centroid
  Vec3 pStarU;          // U* at cell centroid (RC objective)
  double pStarScalar = 0;  // p* at cell centroid (RC objective)
};
struct QuadData { std::vector<QuadCell> cells; };

inline QuadData buildQuadData(const DistortedMesh& M) {
  QuadData Q;
  Q.cells.resize(M.cells.size());
  const double g = 1.0 / std::sqrt(3.0);
  const double gp[2] = {-g, g};
  // node order from Cell::v: 000,100,110,010,001,101,111,011  (i&1 -> x, i&2 -> y, i&4 -> z)
  for (size_t ci = 0; ci < M.cells.size(); ++ci) {
    const Cell& c = M.cells[ci];
    Vec3 V[8];
    for (int i = 0; i < 8; ++i) V[i] = M.nodes[c.v[i]];
    // Cell::v face-loop order {000,100,110,010,001,101,111,011}; explicit parity tables
    const double SR[8] = {-1, +1, +1, -1, -1, +1, +1, -1};
    const double SS[8] = {-1, -1, +1, +1, -1, -1, +1, +1};
    const double ST[8] = {-1, -1, -1, -1, +1, +1, +1, +1};
    QuadCell out;
    for (int a = 0; a < 2; ++a) for (int b = 0; b < 2; ++b) for (int cc = 0; cc < 2; ++cc) {
      double r = gp[a], s = gp[b], t = gp[cc];
      Vec3 xq, dxr, dxs, dxt;
      for (int i = 0; i < 8; ++i) {
        double sr = SR[i], ss = SS[i], st = ST[i];
        double N  = (1 + sr * r) * (1 + ss * s) * (1 + st * t) / 8.0;
        double Nr = sr * (1 + ss * s) * (1 + st * t) / 8.0;
        double Ns = (1 + sr * r) * ss * (1 + st * t) / 8.0;
        double Nt = (1 + sr * r) * (1 + ss * s) * st / 8.0;
        xq += V[i] * N;
        dxr += V[i] * Nr;
        dxs += V[i] * Ns;
        dxt += V[i] * Nt;
      }
      double detJ = dxr.dot(dxs.cross(dxt));
      double w = std::fabs(detJ);  // 2-point Gauss weight = 1 per direction
      out.qDiv += w * divUPhi(xq.x, xq.y, xq.z);
      out.qLap += w * lapPhi(xq.x, xq.y, xq.z);
    }
    out.phiStar = phiExact(c.centroid.x, c.centroid.y, c.centroid.z);
    out.pStarU = uExact(c.centroid.x, c.centroid.y, c.centroid.z);
    out.pStarScalar = pStokes(c.centroid.x, c.centroid.y, c.centroid.z);
    Q.cells[ci] = out;
  }
  return Q;
}

// ---------------- face geometry cache (exact, shared) ------------------------
struct FaceG {
  double alpha = 0;      // |S|^2 / (d . S)
  double beta = 0;       // (xf - xP).S / (d . S)
  double alphaOrth = 0;  // |S| / |d|   (legacy orthogonal operator)
  double dDotS = 0;
  Vec3 d;
  Vec3 mSkew;            // x_f - (x_P + beta d)
  Vec3 kCorr;            // S_f - alpha d
  Vec3 S, xf;
  double mHat = 0;       // u_hat(xf) . S  (unscaled)
};
struct MeshCache {
  std::vector<FaceG> fg;
  std::vector<double> phiB;    // phi_exact(xf) on boundary faces
  std::vector<double> cellVol;
};
struct CorrectionFlags {
  bool legacyOrth = false;   // naive orthogonal operator only (V0)
  bool useSkew = true;       // Class-1 skewness correction (m_f^skew)
  bool useCorr = true;       // implicit non-orthogonal correction (k_f^corr)
};

inline MeshCache buildMeshCache(const DistortedMesh& M) {
  MeshCache C;
  C.fg.resize(M.faces.size());
  C.phiB.assign(M.faces.size(), 0.0);
  C.cellVol.assign(M.cells.size(), 0.0);
  for (size_t i = 0; i < M.cells.size(); ++i) C.cellVol[i] = M.cells[i].volume;
  for (size_t fi = 0; fi < M.faces.size(); ++fi) {
    const Face& f = M.faces[fi];
    FaceG g;
    g.S = f.S; g.xf = f.xf;
    const Vec3 xP = M.cells[f.cellP].centroid;
    g.d = (f.cellN >= 0) ? (M.cells[f.cellN].centroid - xP) : (f.xf - xP);
    g.dDotS = g.d.dot(g.S);
    double dn = g.d.norm();
    double Sn = f.S.norm();
    if (g.dDotS > 0.0) {
      g.alpha = f.S.norm2() / g.dDotS;
      g.beta = (f.xf - xP).dot(g.S) / g.dDotS;
    } else {
      g.alpha = g.alphaOrth = Sn / std::max(dn, 1e-14);
      g.beta = 0.5;
    }
    g.alphaOrth = Sn / std::max(dn, 1e-14);
    g.mSkew = f.xf - (xP + g.d * g.beta);
    g.kCorr = f.S - g.d * g.alpha;
    g.mHat = uExact(f.xf.x, f.xf.y, f.xf.z).dot(f.S);
    C.fg[fi] = g;
    C.phiB[fi] = phiExact(f.xf.x, f.xf.y, f.xf.z);
  }
  return C;
}

// ---------------- assembly (templated over T) --------------------------------
template <typename T> struct ScalarParams { T gamma, u0, sigma; };
template <typename T> struct Sys {
  int n = 0;
  std::vector<std::tuple<int, int, T>> triplets;
  std::vector<T> rhs;
};

template <typename T>
static inline void LvecOf(const GradientData& G, int c, int j, std::array<double, 3>& outv, bool& has) {
  if (j == c) { outv = G.cells[c].self; has = true; return; }
  for (const auto& pr : G.cells[c].cols) if (pr.first == j) { outv = pr.second; has = true; return; }
  has = false;
}

template <typename T>
Sys<T> assembleScalar(const DistortedMesh& M, const GradientData& G, const MeshCache& C,
                      const QuadData& Q, const ScalarParams<T>& sp,
                      const CorrectionFlags& flags) {
  const int n = (int)M.cells.size();
  Sys<T> out;
  out.n = n;
  out.rhs.assign(n, T(0.0));
  std::vector<T> constVec(n, T(0.0));
  auto add = [&](int r, int c, const T& v) {
    if (!(v == T(0.0))) out.triplets.emplace_back(r, c, v);
  };
  // ---- interior faces ----
  for (size_t fi = 0; fi < M.faces.size(); ++fi) {
    const Face& f = M.faces[fi];
    if (f.cellN < 0) continue;
    const FaceG& g = C.fg[fi];
    const int P = f.cellP, N = f.cellN;
    const T gamma = sp.gamma, m = sp.u0 * (T)g.mHat;
    double alpha = g.alpha, beta = g.beta;
    Vec3 kCorr = g.kCorr, mSkew = g.mSkew;
    if (flags.legacyOrth) { alpha = g.alphaOrth; beta = 0.5; kCorr = Vec3{}; mSkew = Vec3{}; }
    else {
      if (!flags.useCorr) kCorr = Vec3{};
      if (!flags.useSkew) mSkew = Vec3{};
    }
    const T aPP = m * (T)(1.0 - beta) + gamma * (T)alpha;
    const T aPN = m * (T)beta - gamma * (T)alpha;
    // correction coupling: cvec = m*mSkew - gamma*kCorr
    const std::array<T, 3> cvec{m * (T)mSkew.x - gamma * (T)kCorr.x,
                                m * (T)mSkew.y - gamma * (T)kCorr.y,
                                m * (T)mSkew.z - gamma * (T)kCorr.z};
    std::vector<int> js{P, N};
    for (const auto& pr : G.cells[P].cols) js.push_back(pr.first);
    for (const auto& pr : G.cells[N].cols) js.push_back(pr.first);
    std::sort(js.begin(), js.end());
    js.erase(std::unique(js.begin(), js.end()), js.end());
    for (int j : js) {
      std::array<double, 3> lp{0, 0, 0}, ln{0, 0, 0};
      bool hp = false, hn = false;
      LvecOf<T>(G, P, j, lp, hp);
      LvecOf<T>(G, N, j, ln, hn);
      std::array<double, 3> s{0.5 * ((hp ? lp[0] : 0.0) + (hn ? ln[0] : 0.0)),
                              0.5 * ((hp ? lp[1] : 0.0) + (hn ? ln[1] : 0.0)),
                              0.5 * ((hp ? lp[2] : 0.0) + (hn ? ln[2] : 0.0))};
      T corr = cvec[0] * (T)s[0] + cvec[1] * (T)s[1] + cvec[2] * (T)s[2];
      T base = (j == P ? aPP : T(0.0)) + (j == N ? aPN : T(0.0));
      T valP = base + corr;
      add(P, j, valP);
      add(N, j, -valP);
    }
    const Vec3 tP{G.cells[P].rhsBoundary[0], G.cells[P].rhsBoundary[1], G.cells[P].rhsBoundary[2]};
    const Vec3 tN{G.cells[N].rhsBoundary[0], G.cells[N].rhsBoundary[1], G.cells[N].rhsBoundary[2]};
    T c0 = (cvec[0] * (T)(tP.x + tN.x) + cvec[1] * (T)(tP.y + tN.y) + cvec[2] * (T)(tP.z + tN.z)) * (T)0.5;
    constVec[P] += c0;
    constVec[N] -= c0;
  }
  // ---- boundary faces (Dirichlet) ----
  for (size_t fi = 0; fi < M.faces.size(); ++fi) {
    const Face& f = M.faces[fi];
    if (f.cellN >= 0) continue;
    const FaceG& g = C.fg[fi];
    const int P = f.cellP;
    const T gamma = sp.gamma, m = sp.u0 * (T)g.mHat;
    const double alpha = flags.legacyOrth ? g.alphaOrth : g.alpha;
    const Vec3 kCorr = (flags.legacyOrth || !flags.useCorr) ? Vec3{} : g.kCorr;
    const T phiB = (T)C.phiB[fi];
    const bool outflow = g.mHat > 0.0;
    if (outflow) add(P, P, m);
    add(P, P, gamma * (T)alpha);
    const std::array<T, 3> cvec{-gamma * (T)kCorr.x, -gamma * (T)kCorr.y, -gamma * (T)kCorr.z};
    std::vector<int> js{P};
    for (const auto& pr : G.cells[P].cols) js.push_back(pr.first);
    std::sort(js.begin(), js.end());
    js.erase(std::unique(js.begin(), js.end()), js.end());
    for (int j : js) {
      std::array<double, 3> lp{0, 0, 0};
      bool hp = false;
      LvecOf<T>(G, P, j, lp, hp);
      if (!hp) continue;
      add(P, j, cvec[0] * (T)lp[0] + cvec[1] * (T)lp[1] + cvec[2] * (T)lp[2]);
    }
    const Vec3 tP{G.cells[P].rhsBoundary[0], G.cells[P].rhsBoundary[1], G.cells[P].rhsBoundary[2]};
    T c0 = -gamma * (T)alpha * phiB + (outflow ? T(0.0) : m * phiB)
         + cvec[0] * (T)tP.x + cvec[1] * (T)tP.y + cvec[2] * (T)tP.z;
    constVec[P] += c0;
  }
  // ---- source (Gauss quadrature), then solve A phi = src - const ----
  const T gamma = sp.gamma, u0 = sp.u0, sigma = sp.sigma;
  for (int i = 0; i < n; ++i)
    out.rhs[i] = sigma * (u0 * (T)Q.cells[i].qDiv - gamma * (T)Q.cells[i].qLap) - constVec[i];
  return out;
}

template <typename T>
T objectiveJ(const DistortedMesh& M, const QuadData& Q, const std::vector<T>& phi) {
  T J = 0;
  for (size_t i = 0; i < M.cells.size(); ++i) {
    T d = phi[i] - (T)Q.cells[i].phiStar;
    J += (T)M.cells[i].volume * d * d;
  }
  return J;
}

// ----------------------------- small dense LU -----------------
template <typename T>
bool luFactor(std::vector<T>& A, int n, std::vector<int>& perm) {
  perm.resize(n);
  std::iota(perm.begin(), perm.end(), 0);
  const double TINY = 1e-30;
  for (int col = 0; col < n; ++col) {
    int pivRow = col;
    double best = std::fabs(A[(size_t)perm[col] * n + col]);
    for (int r = col + 1; r < n; ++r) {
      double v = std::fabs(A[(size_t)perm[r] * n + col]);
      if (v > best) { best = v; pivRow = r; }
    }
    if (best < TINY) return false;
    std::swap(perm[col], perm[pivRow]);
    const int pr = perm[col];
    T diag = A[(size_t)pr * n + col];
    for (int r = col + 1; r < n; ++r) {
      const int rr = perm[r];
      T f = A[(size_t)rr * n + col] / diag;
      A[(size_t)rr * n + col] = f;
      for (int c = col + 1; c < n; ++c) A[(size_t)rr * n + c] -= f * A[(size_t)pr * n + c];
    }
  }
  return true;
}
template <typename T>
std::vector<T> luSolve(const std::vector<T>& LU, const std::vector<int>& perm, int n,
                       const std::vector<T>& b) {
  std::vector<T> x(n), y(n);
  for (int i = 0; i < n; ++i) y[i] = b[perm[i]];
  for (int i = 0; i < n; ++i) {
    const int ri = perm[i];
    T s = y[i];
    for (int j = 0; j < i; ++j) s -= LU[(size_t)ri * n + j] * y[j];
    y[i] = s;
  }
  for (int i = n - 1; i >= 0; --i) {
    const int ri = perm[i];
    T s = y[i];
    for (int j = i + 1; j < n; ++j) s -= LU[(size_t)ri * n + j] * x[j];
    x[i] = s / LU[(size_t)ri * n + i];
  }
  return x;
}
// Solve A^T x = b given PA = LU (A = P^T L U  =>  A^T = U^T L^T P)
template <typename T>
std::vector<T> luSolveT(const std::vector<T>& LU, const std::vector<int>& perm, int n,
                        const std::vector<T>& b) {
  std::vector<T> z(n), w(n), x(n);
  for (int j = 0; j < n; ++j) {  // U^T z = b
    T s = b[j];
    for (int i = 0; i < j; ++i) s -= LU[(size_t)perm[i] * n + j] * z[i];
    z[j] = s / LU[(size_t)perm[j] * n + j];
  }
  for (int i = n - 1; i >= 0; --i) {  // L^T w = z
    T s = z[i];
    for (int j = i + 1; j < n; ++j) s -= LU[(size_t)perm[j] * n + i] * w[j];
    w[i] = s;
  }
  for (int i = 0; i < n; ++i) x[perm[i]] = w[i];  // x = P^T w
  return x;
}
template <typename T>
std::vector<T> tripletsToDense(const Sys<T>& S) {
  std::vector<T> A((size_t)S.n * S.n, T(0.0));
  for (const auto& [r, c, v] : S.triplets) A[(size_t)r * S.n + c] += v;
  return A;
}

// ----------------------------- BiCGSTAB (double, CSR) -----------------
struct CSR {
  int n = 0;
  std::vector<int> rowPtr, colIdx;
  std::vector<double> val;
};
inline CSR tripletsToCSR(const Sys<double>& S) {
  const int n = S.n;
  std::vector<std::vector<std::pair<int, double>>> rows(n);
  for (auto& [r, c, v] : S.triplets) rows[r].emplace_back(c, v);
  CSR out; out.n = n; out.rowPtr.assign(n + 1, 0);
  size_t nnz = 0;
  for (int r = 0; r < n; ++r) {
    auto& row = rows[r];
    std::sort(row.begin(), row.end(), [](auto& a, auto& b) { return a.first < b.first; });
    size_t w = 0;
    for (size_t i = 0; i < row.size();) {
      size_t j = i; double sum = 0.0;
      while (j < row.size() && row[j].first == row[i].first) { sum += row[j].second; ++j; }
      row[w] = {row[i].first, sum}; ++w; i = j;
    }
    row.resize(w); nnz += w; out.rowPtr[r + 1] = (int)nnz;
  }
  out.colIdx.resize(nnz); out.val.resize(nnz);
  size_t w = 0;
  for (int r = 0; r < n; ++r) for (auto& p : rows[r]) { out.colIdx[w] = p.first; out.val[w] = p.second; ++w; }
  return out;
}
inline void spMV(const CSR& A, const std::vector<double>& x, std::vector<double>& y) {
  for (int r = 0; r < A.n; ++r) {
    double s = 0.0;
    for (int p = A.rowPtr[r]; p < A.rowPtr[r + 1]; ++p) s += A.val[p] * x[A.colIdx[p]];
    y[r] = s;
  }
}
struct BiCGResult { std::vector<double> x; int iters = 0; double relResid = -1.0; bool converged = false; };
inline BiCGResult bicgstab(const CSR& A, const std::vector<double>& b, double tol, int maxit) {
  const int n = A.n;
  std::vector<double> Minv(n, 1.0);
  for (int i = 0; i < n; ++i)
    for (int p = A.rowPtr[i]; p < A.rowPtr[i + 1]; ++p)
      if (A.colIdx[p] == i) { Minv[i] = (std::fabs(A.val[p]) > 1e-30) ? 1.0 / A.val[p] : 1.0; break; }
  double bnrm = 0.0; for (double v : b) bnrm += v * v; bnrm = std::sqrt(std::max(bnrm, 1e-300));
  std::vector<double> x(n, 0.0), r = b, rh(n, 0.0), p(n, 0.0), v(n, 0.0);
  std::vector<double> s(n, 0.0), t(n, 0.0), phat(n, 0.0), shat(n, 0.0);
  std::copy(r.begin(), r.end(), rh.begin());
  double rhoOld = 1.0, alpha = 1.0, omega = 1.0;
  BiCGResult res;
  for (int it = 0; it < maxit; ++it) {
    double rho = 0.0; for (int i = 0; i < n; ++i) rho += rh[i] * r[i];
    if (std::fabs(rho) < 1e-300) break;
    if (it == 0) { for (int i = 0; i < n; ++i) p[i] = r[i]; }
    else {
      double beta = (rho / rhoOld) * (alpha / (std::fabs(omega) < 1e-300 ? 1e-300 : omega));
      for (int i = 0; i < n; ++i) p[i] = r[i] + beta * (p[i] - omega * v[i]);
    }
    for (int i = 0; i < n; ++i) phat[i] = p[i] * Minv[i];
    spMV(A, phat, v);
    double rhv = 0.0; for (int i = 0; i < n; ++i) rhv += rh[i] * v[i];
    if (std::fabs(rhv) < 1e-300) break;
    alpha = rho / rhv;
    for (int i = 0; i < n; ++i) s[i] = r[i] - alpha * v[i];
    double snrm = 0.0; for (double v2 : s) snrm += v2 * v2;
    if (std::sqrt(snrm) < tol * bnrm) {
      for (int i = 0; i < n; ++i) x[i] += alpha * phat[i];
      res.x = x; res.iters = it + 1; res.converged = true; res.relResid = std::sqrt(snrm) / bnrm;
      return res;
    }
    for (int i = 0; i < n; ++i) shat[i] = s[i] * Minv[i];
    spMV(A, shat, t);
    double tt = 0.0, ts = 0.0; for (int i = 0; i < n; ++i) { tt += t[i] * t[i]; ts += t[i] * s[i]; }
    omega = (std::fabs(tt) > 1e-300) ? ts / tt : 1.0;
    for (int i = 0; i < n; ++i) x[i] += alpha * phat[i] + omega * shat[i];
    for (int i = 0; i < n; ++i) r[i] = s[i] - omega * t[i];
    double rnrm = 0.0; for (double v2 : r) rnrm += v2 * v2;
    rhoOld = rho;
    if (std::sqrt(rnrm) < tol * bnrm) {
      res.x = x; res.iters = it + 1; res.converged = true; res.relResid = std::sqrt(rnrm) / bnrm;
      return res;
    }
  }
  res.x = x; res.iters = maxit; res.converged = false;
  spMV(A, x, t);
  double rn = 0.0; for (int i = 0; i < n; ++i) rn += (b[i] - t[i]) * (b[i] - t[i]);
  res.relResid = std::sqrt(rn) / bnrm;
  return res;
}
}  // namespace vv
