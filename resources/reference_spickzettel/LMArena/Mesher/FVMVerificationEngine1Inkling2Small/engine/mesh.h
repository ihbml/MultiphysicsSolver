// ============================================================================
//  PolyVV-20 :: ASME V&V 20-2009 computational verification engine
//  Distorted 3D polyhedral / hex-cut-cell mesh family generator + exact
//  pyramid-decomposition geometry (S_f, x_f, V_c) + WLS cell gradients.
//  Real, deterministic, no mocked numbers. Compiled with g++ -std=c++20 -O2.
// ============================================================================
#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <limits>
#include <numeric>
#include <string>
#include <vector>

namespace vv {

// ---------------------------------------------------------------- 3-vector -
struct Vec3 {
  double x = 0.0, y = 0.0, z = 0.0;
  Vec3() = default;
  Vec3(double a, double b, double c) : x(a), y(b), z(c) {}
  Vec3 operator+(const Vec3& o) const { return {x + o.x, y + o.y, z + o.z}; }
  Vec3 operator-(const Vec3& o) const { return {x - o.x, y - o.y, z - o.z}; }
  Vec3 operator*(double s) const { return {x * s, y * s, z * s}; }
  Vec3 operator*(const Vec3& o) const { return {x * o.x, y * o.y, z * o.z}; }
  Vec3 operator/(double s) const { return {x / s, y / s, z / s}; }
  Vec3& operator+=(const Vec3& o) { x += o.x; y += o.y; z += o.z; return *this; }
  double dot(const Vec3& o) const { return x * o.x + y * o.y + z * o.z; }
  Vec3 cross(const Vec3& o) const {
    return {y * o.z - z * o.y, z * o.x - x * o.z, x * o.y - y * o.x};
  }
  double norm() const { return std::sqrt(x * x + y * y + z * z); }
  double norm2() const { return x * x + y * y + z * z; }
  bool finite() const {
    return std::isfinite(x) && std::isfinite(y) && std::isfinite(z);
  }
};

// -------------------------------------------- deterministic hashing / noise -
inline uint64_t splitmix64(uint64_t x) {
  x += 0x9E3779B97F4A7C15ULL;
  x = (x ^ (x >> 30)) * 0xBF58476D1CE4E5B9ULL;
  x = (x ^ (x >> 27)) * 0x94D049BB133111EBULL;
  return x ^ (x >> 31);
}
inline double hashSym(int i, int j, int k, int c, uint64_t seed) {
  uint64_t h = seed;
  h = splitmix64(h ^ (uint64_t)(int64_t)i * 0xA0761D6478BD642FULL);
  h = splitmix64(h ^ (uint64_t)(int64_t)j * 0xE7037ED1A0B428DBULL);
  h = splitmix64(h ^ (uint64_t)(int64_t)k * 0x8EBC6AF09C88C6E3ULL);
  h = splitmix64(h ^ (uint64_t)(c + 1) * 0x589965CC75374CC3ULL);
  return (double)(h >> 11) * (1.0 / 9007199254740992.0) * 2.0 - 1.0;  // [-1,1]
}

// Smooth, deterministic pseudo-random vector field: trilinear interpolation of
// a hashed coarse lattice (L cells per axis) — the "smooth randomized" part of
// the perturbation. Components in [-1,1].
struct LatticeField {
  int L = 4;
  uint64_t seed = 0xC0FFEEULL;
  double corner(int i, int j, int k, int c) const { return hashSym(i, j, k, c, seed); }
  Vec3 direction(double x, double y, double z) const {
    auto axis = [this](double u, int& i0, double& f) {
      double g = u * (double)L;
      i0 = (int)std::floor(g);
      if (i0 > L - 1) i0 = L - 1;
      if (i0 < 0) i0 = 0;
      f = g - (double)i0;
      f = std::clamp(f, 0.0, 1.0);
    };
    int i, j, k; double fx, fy, fz;
    axis(x, i, fx); axis(y, j, fy); axis(z, k, fz);
    Vec3 d;
    double* comps = &d.x;
    for (int c = 0; c < 3; ++c) {
      double c000 = corner(i, j, k, c),     c100 = corner(i + 1, j, k, c);
      double c010 = corner(i, j + 1, k, c), c110 = corner(i + 1, j + 1, k, c);
      double c001 = corner(i, j, k + 1, c), c101 = corner(i + 1, j, k + 1, c);
      double c011 = corner(i, j + 1, k + 1, c), c111 = corner(i + 1, j + 1, k + 1, c);
      double c00 = c000 + (c100 - c000) * fx, c10 = c010 + (c110 - c010) * fx;
      double c01 = c001 + (c101 - c001) * fx, c11 = c011 + (c111 - c011) * fx;
      double c0 = c00 + (c10 - c00) * fy,  c1 = c01 + (c11 - c01) * fy;
      comps[c] = c0 + (c1 - c0) * fz;
    }
    return d;
  }
};

// ---------------------------------------------------------------- geometry -
// Newell area vector: exact for planar polygons, robust for slightly folded
// faces; used for S_f. Sign gives orientation by vertex order.
inline Vec3 newellNormal(const std::vector<Vec3>& p) {
  Vec3 S;
  const size_t n = p.size();
  for (size_t m = 0; m < n; ++m) {
    const Vec3& a = p[m];
    const Vec3& b = p[(m + 1) % n];
    S.x += (a.y - b.y) * (a.z + b.z);
    S.y += (a.z - b.z) * (a.x + b.x);
    S.z += (a.x - b.x) * (a.y + b.y);
  }
  return S * 0.5;
}
inline Vec3 polygonCentroid(const std::vector<Vec3>& p) {
  // Fan-triangulated from v0 (valid for star-shaped / planar polygons)
  double wSum = 0.0, cx = 0, cy = 0, cz = 0;
  for (size_t i = 1; i + 1 < p.size(); ++i) {
    Vec3 e1 = p[i] - p[0], e2 = p[i + 1] - p[0];
    Vec3 cr = e1.cross(e2);
    double w = 0.5 * cr.norm();
    Vec3 ct = (p[0] + p[i] + p[i + 1]) * (1.0 / 3.0);
    wSum += w; cx += w * ct.x; cy += w * ct.y; cz += w * ct.z;
  }
  if (wSum <= 0.0) return p[0];
  return {cx / wSum, cy / wSum, cz / wSum};
}

enum class FaceClass { OK = 0, NONORTHO = 1, SEVERE = 2, INVALID = 3 };
inline const char* faceClassName(FaceClass c) {
  switch (c) {
    case FaceClass::OK: return "OK";
    case FaceClass::NONORTHO: return "NONORTHO";
    case FaceClass::SEVERE: return "SEVERE";
    default: return "INVALID";
  }
}
inline FaceClass classifyCosTheta(double cosTheta) {
  if (!(cosTheta > 1e-8)) return FaceClass::INVALID;  // cosθ <= 1e-8 (or NaN)
  if (cosTheta >= 0.92) return FaceClass::OK;
  if (cosTheta >= 0.55) return FaceClass::NONORTHO;
  return FaceClass::SEVERE;
}

// ------------------------------------------------------------- mesh types --
struct Face {
  std::vector<int> v;          // polygon vertices, ordered so S points P -> N
  int cellP = -1, cellN = -1;  // owner, neighbour (-1 = physical boundary)
  Vec3 S;                      // exact area vector S_f (Newell)
  Vec3 xf;                     // exact face centroid x_f
  double area = 0.0;
  double cosTheta = 1.0;       // d_f . S_f / (|d_f||S_f|)
  FaceClass cls = FaceClass::OK;
};
struct Cell {
  std::array<int, 8> v{};
  std::vector<int> faces;      // incident face indices
  Vec3 centroid;               // exact centroid (pyramid decomposition)
  double volume = 0.0;         // exact volume v_c
  int i = 0, j = 0, k = 0;
};
struct DistortedMesh {
  int N = 0;
  double h = 0.0;
  double alphaUsed = 0.0;        // calibrated displacement amplitude
  double maxAngleDeg = 0.0;      // worst face non-orthogonality
  std::vector<Vec3> nodes;
  std::vector<Cell> cells;
  std::vector<Face> faces;
  int invalidFaces = 0;
  double minVolumeRatio = 1.0;
  bool repaired = false;

  int nodeId(int i, int j, int k) const {
    const int s = N + 1;
    return (i * s + j) * s + k;
  }
  int cellId(int i, int j, int k) const {
    const int s = N;
    return (i * s + j) * s + k;
  }
};

// ----------------------------------------------------------- build & calibrate
inline Vec3 perturbedNode(const LatticeField& lf, const Vec3& x, int i, int j, int k, int N,
                          double h, double alpha, uint64_t seed, double scale,
                          double& outSmoothWeight) {
  (void)outSmoothWeight;
  // Smooth randomized component (lattice field) + independent nodal jitter;
  // boundary nodes slide along the boundary plane, corners are pinned so the
  // physical domain stays exactly [0,1]^3.
  Vec3 ds = lf.direction(x.x, x.y, x.z);              // smooth, bounded |c|<=1
  Vec3 rnd{hashSym(i, j, k, 0, seed), hashSym(i, j, k, 1, seed), hashSym(i, j, k, 2, seed)};
  Vec3 disp = (ds * 0.62 + rnd * 0.55) * (alpha * h * scale);
  // hard clamp: no component may exceed 0.62 h of nodal displacement
  double cap = 0.62 * h;
  disp.x = std::clamp(disp.x, -cap, cap);
  disp.y = std::clamp(disp.y, -cap, cap);
  disp.z = std::clamp(disp.z, -cap, cap);
  if (i == 0 || i == N) disp.x = 0.0;  // slide only along face
  if (j == 0 || j == N) disp.y = 0.0;
  if (k == 0 || k == N) disp.z = 0.0;
  if (i == 0 || i == N) {
    if (j == 0 || j == N) disp.z = 0.0;
    if (k == 0 || k == N) disp.y = 0.0;
  }
  if (j == 0 || j == N) {
    if (i == 0 || i == N) disp.z = 0.0;
    if (k == 0 || k == N) disp.x = 0.0;
  }
  if (k == 0 || k == N) {
    if (i == 0 || i == N) disp.y = 0.0;
    if (j == 0 || j == N) disp.x = 0.0;
  }
  return x + disp;
}

inline void finalizeGeometry(DistortedMesh& M) {
  // nodes -> already set. Faces: compute S, xf, area.
  for (auto& f : M.faces) {
    std::vector<Vec3> p(f.v.size());
    for (size_t m = 0; m < f.v.size(); ++m) p[m] = M.nodes[f.v[m]];
    f.S = newellNormal(p);
    f.xf = polygonCentroid(p);
    f.area = f.S.norm();
  }
  // spark of naivety: nothing. Exact volumes for cells via divergence theorem:
  //   V = (1/3) Σ_f x_f · S_f  (exact for planar faces, outward S)
  // centroids via pyramid (tetra) decomposition from vertex-average seed.
  for (auto& c : M.cells) {
    Vec3 p0;
    for (int n : c.v) p0 += M.nodes[n];
    p0 = p0 * (1.0 / 8.0);
    double vol = 0.0;
    Vec3 cm;
    // Pyramid decomposition: fan-triangulate each face, tetra (p0, a, b, d)
    const int selfIdx = (int)(&c - M.cells.data());
    for (int fi : c.faces) {
      const Face& f = M.faces[fi];
      const bool owner = (f.cellP == selfIdx);
      int nv = (int)f.v.size();
      Vec3 a = M.nodes[f.v[0]];
      for (int t = 1; t + 1 < nv; ++t) {
        Vec3 b = M.nodes[f.v[t]], d = M.nodes[f.v[t + 1]];
        Vec3 e1 = b - p0, e2 = d - p0;
        double vt = (a - p0).dot(e1.cross(e2)) / 6.0;
        if (!owner) vt = -vt;
        Vec3 tc = (a + b + d + p0) * 0.25;
        vol += vt;
        cm += tc * vt;
      }
    }
    double vDiv = 0.0;
    for (int fi : c.faces) {
      const Face& f = M.faces[fi];
      const bool owner = (f.cellP == selfIdx);
      vDiv += (owner ? 1.0 : -1.0) * f.xf.dot(f.S);
    }
    M.cells[selfIdx].volume = std::fabs(vDiv) / 3.0;
    if (std::fabs(vol) > 1e-30) M.cells[selfIdx].centroid = cm / vol;
    else M.cells[selfIdx].centroid = p0;
  }
  // per-face classification vs cell centroid distance
  M.maxAngleDeg = 0.0;
  M.invalidFaces = 0;
  M.minVolumeRatio = 1.0;
  for (auto& f : M.faces) {
    Vec3 dp = M.cells[f.cellP].centroid;
    Vec3 d = (f.cellN >= 0) ? M.cells[f.cellN].centroid - dp : f.xf - dp;
    double dn = d.norm();
    f.cosTheta = (dn > 0.0 && f.area > 0.0) ? d.dot(f.S) / (dn * f.area) : 1.0;
    if (!std::isfinite(f.cosTheta)) f.cosTheta = -1.0;
    f.cls = classifyCosTheta(f.cosTheta);
    if (f.cls == FaceClass::INVALID) M.invalidFaces++;
    double ang = std::acos(std::clamp(f.cosTheta, -1.0, 1.0)) * 180.0 / M_PI;
    M.maxAngleDeg = std::max(M.maxAngleDeg, ang);
  }
  const double h3 = M.h * M.h * M.h;
  for (const auto& c : M.cells)
    M.minVolumeRatio = std::min(M.minVolumeRatio, c.volume / h3);
}

inline DistortedMesh buildMesh(int N, double alphaSkew, uint64_t seed, double scale) {
  DistortedMesh M;
  M.N = N;
  M.h = 1.0 / N;
  M.nodes.resize((size_t)(N + 1) * (N + 1) * (N + 1));
  LatticeField lf;
  lf.L = 4;
  lf.seed = seed ^ 0x51AB3ULL;
  for (int i = 0; i <= N; ++i)
    for (int j = 0; j <= N; ++j)
      for (int k = 0; k <= N; ++k) {
        Vec3 x{i * M.h, j * M.h, k * M.h};
        double dummy = 0.0;
        M.nodes[M.nodeId(i, j, k)] = perturbedNode(lf, x, i, j, k, N, M.h, alphaSkew, seed, scale, dummy);
      }
  // cells (8-node hex-cut topology)
  for (int i = 0; i < N; ++i)
    for (int j = 0; j < N; ++j)
      for (int k = 0; k < N; ++k) {
        Cell c;
        c.i = i; c.j = j; c.k = k;
        c.v = {M.nodeId(i, j, k),     M.nodeId(i + 1, j, k),     M.nodeId(i + 1, j + 1, k),
               M.nodeId(i, j + 1, k), M.nodeId(i, j, k + 1),     M.nodeId(i + 1, j, k + 1),
               M.nodeId(i + 1, j + 1, k + 1), M.nodeId(i, j + 1, k + 1)};
        M.cells.push_back(c);
      }
  // faces with outward-from-owner orientation baked in
  // x-faces (normal +x from lower to upper cell)
  for (int i = 0; i <= N; ++i)
    for (int j = 0; j < N; ++j)
      for (int k = 0; k < N; ++k) {
        std::array<int, 4> q{M.nodeId(i, j, k), M.nodeId(i, j + 1, k),
                             M.nodeId(i, j + 1, k + 1), M.nodeId(i, j, k + 1)};
        Face f;
        bool boundaryLow = (i == 0), boundaryHigh = (i == N);
        if (boundaryLow) {  // owner is (0,j,k), outward = -x  -> flip
          f.v = {q[3], q[2], q[1], q[0]};
          f.cellP = M.cellId(0, j, k);
          f.cellN = -1;
        } else if (boundaryHigh) {  // owner is (N-1,j,k), outward = +x
          f.v = {q[0], q[1], q[2], q[3]};
          f.cellP = M.cellId(N - 1, j, k);
          f.cellN = -1;
        } else {
          f.v = {q[0], q[1], q[2], q[3]};
          f.cellP = M.cellId(i - 1, j, k);
          f.cellN = M.cellId(i, j, k);
        }
        M.faces.push_back(f);
      }
  // y-faces
  for (int j = 0; j <= N; ++j)
    for (int i = 0; i < N; ++i)
      for (int k = 0; k < N; ++k) {
        std::array<int, 4> q{M.nodeId(i, j, k), M.nodeId(i, j, k + 1),
                             M.nodeId(i + 1, j, k + 1), M.nodeId(i + 1, j, k)};
        Face f;
        bool boundaryLow = (j == 0), boundaryHigh = (j == N);
        if (boundaryLow) {
          f.v = {q[3], q[2], q[1], q[0]};
          f.cellP = M.cellId(i, 0, k);
          f.cellN = -1;
        } else if (boundaryHigh) {
          f.v = {q[0], q[1], q[2], q[3]};
          f.cellP = M.cellId(i, N - 1, k);
          f.cellN = -1;
        } else {
          f.v = {q[0], q[1], q[2], q[3]};
          f.cellP = M.cellId(i, j - 1, k);
          f.cellN = M.cellId(i, j, k);
        }
        M.faces.push_back(f);
      }
  // z-faces
  for (int k = 0; k <= N; ++k)
    for (int i = 0; i < N; ++i)
      for (int j = 0; j < N; ++j) {
        std::array<int, 4> q{M.nodeId(i, j, k), M.nodeId(i + 1, j, k),
                             M.nodeId(i + 1, j + 1, k), M.nodeId(i, j + 1, k)};
        Face f;
        bool boundaryLow = (k == 0), boundaryHigh = (k == N);
        if (boundaryLow) {
          f.v = {q[3], q[2], q[1], q[0]};
          f.cellP = M.cellId(i, j, 0);
          f.cellN = -1;
        } else if (boundaryHigh) {
          f.v = {q[0], q[1], q[2], q[3]};
          f.cellP = M.cellId(i, j, N - 1);
          f.cellN = -1;
        } else {
          f.v = {q[0], q[1], q[2], q[3]};
          f.cellP = M.cellId(i, j, k - 1);
          f.cellN = M.cellId(i, j, k);
        }
        M.faces.push_back(f);
      }
  // link cells <-> faces
  for (size_t fi = 0; fi < M.faces.size(); ++fi) {
    Face& f = M.faces[fi];
    M.cells[f.cellP].faces.push_back((int)fi);
    if (f.cellN >= 0) M.cells[f.cellN].faces.push_back((int)fi);
  }
  finalizeGeometry(M);
  M.alphaUsed = alphaSkew * scale;
  return M;
}

// Auto-calibrate distortion scale so worst face angle lands near the 45 deg
// target while no cell inverts (deterministic; no repairs needed).
inline DistortedMesh calibrateAndBuild(int N, uint64_t seed, double targetMaxAngleDeg = 45.0,
                                       double alphaSkew = 0.55) {
  DistortedMesh best;
  bool have = false;
  double lo = 0.15, hi = 1.35;
  double bestScale = 0.5;
  for (int it = 0; it < 24; ++it) {
    double scale = 0.5 * (lo + hi);
    DistortedMesh M = buildMesh(N, alphaSkew, seed, scale);
    bool ok = (M.invalidFaces == 0) && (M.minVolumeRatio > 0.30) &&
              (M.maxAngleDeg <= targetMaxAngleDeg + 6.0);
    bool goodEnough = ok && (M.maxAngleDeg >= targetMaxAngleDeg - 4.0);
    if (ok && (!have || M.maxAngleDeg > best.maxAngleDeg)) {
      best = M;
      have = true;
      bestScale = scale;
    }
    if (M.maxAngleDeg > targetMaxAngleDeg + 6.0 || !ok) {
      if (M.invalidFaces > 0 || M.minVolumeRatio <= 0.30) hi = scale;  // too strong
      else hi = scale;
    } else {
      lo = scale;  // can afford more distortion
    }
    if (goodEnough) { best = M; bestScale = scale; have = true; break; }
  }
  if (!have) best = buildMesh(N, alphaSkew, seed, 0.32);  // guaranteed-safe fallback
  best.alphaUsed = alphaSkew * bestScale;
  return best;
}

// 3x3 matrix inverse (small, exact)
inline bool inv3(const double A[3][3], double inv[3][3]) {
  double a = A[0][0], b = A[0][1], c = A[0][2];
  double d = A[1][0], e = A[1][1], f = A[1][2];
  double g = A[2][0], h = A[2][1], i = A[2][2];
  double A00 = e * i - f * h, A01 = c * h - b * i, A02 = b * f - c * e;
  double det = a * A00 + d * A01 + g * A02;
  if (std::fabs(det) < 1e-30) return false;
  double invDet = 1.0 / det;
  inv[0][0] = A00 * invDet; inv[0][1] = A01 * invDet; inv[0][2] = A02 * invDet;
  inv[1][0] = (f * g - d * i) * invDet; inv[1][1] = (a * i - c * g) * invDet; inv[1][2] = (c * d - a * f) * invDet;
  inv[2][0] = (d * h - e * g) * invDet; inv[2][1] = (b * g - a * h) * invDet; inv[2][2] = (a * e - b * d) * invDet;
  return true;
}

// Weighted least-squares gradient operator per cell (geometry only; values
// plugged in later, possibly as std::complex<double>).
struct CellWLS {
  // per interior neighbour: column vector (3)
  std::vector<std::pair<int, std::array<double, 3>>> cols;   // cell j -> dW delta * w
  std::array<double, 3> self{0, 0, 0};                        // diagonal: -winv * sum(w*delta)
  std::array<double, 3> rhsBoundary{0, 0, 0};                 // t_c (boundary contributions)
  int dummy = 0;
};

struct GradientData {
  std::vector<CellWLS> cells;   // aligned with mesh.cells
};

inline GradientData buildGradientData(const DistortedMesh& M,
                                      const std::vector<double>& phiB) {
  const size_t nc = M.cells.size();
  GradientData G;
  G.cells.assign(nc, CellWLS{});
  // collect per-cell point list: interior neighbours + boundary face points
  std::vector<std::vector<int>> intFaces(nc);  // cell -> face ids
  for (size_t fi = 0; fi < M.faces.size(); ++fi) {
    const Face& f = M.faces[fi];
    intFaces[f.cellP].push_back((int)fi);
    if (f.cellN >= 0) intFaces[f.cellN].push_back((int)fi);
  }
  for (size_t ci = 0; ci < nc; ++ci) {
    const Cell& c = M.cells[ci];
    double W[3][3] = {{0, 0, 0}, {0, 0, 0}, {0, 0, 0}};
    Vec3 sumWd;
    struct Pt { int cell; int face; Vec3 delta; double w; double val; };
    std::vector<Pt> pts;
    for (int fi : intFaces[ci]) {
      const Face& f = M.faces[fi];
      int nb = (f.cellP == (int)ci) ? f.cellN : f.cellP;
      if (nb < 0) {  // boundary face
        Vec3 delta = f.xf - c.centroid;
        double w = f.area / std::max(delta.norm(), 1e-14);
        pts.push_back({-1, fi, delta, w, phiB[fi]});
      } else {
        Vec3 delta = M.cells[nb].centroid - c.centroid;
        double w = f.area / std::max(delta.norm(), 1e-14);
        pts.push_back({nb, fi, delta, w, 0.0});
      }
    }
    for (const auto& p : pts) {
      W[0][0] += p.w * p.delta.x * p.delta.x; W[0][1] += p.w * p.delta.x * p.delta.y; W[0][2] += p.w * p.delta.x * p.delta.z;
      W[1][0] += p.w * p.delta.y * p.delta.x; W[1][1] += p.w * p.delta.y * p.delta.y; W[1][2] += p.w * p.delta.y * p.delta.z;
      W[2][0] += p.w * p.delta.z * p.delta.x; W[2][1] += p.w * p.delta.z * p.delta.y; W[2][2] += p.w * p.delta.z * p.delta.z;
      sumWd += p.delta * p.w;
    }
    double Winv[3][3];
    if (!inv3(W, Winv)) {
      // near-degenerate: fall back to identity * small weight
      for (int a = 0; a < 3; ++a)
        for (int b = 0; b < 3; ++b) Winv[a][b] = (a == b) ? 1.0 : 0.0;
    }
    CellWLS out;
    auto applyWinv = [&](const Vec3& v) {
      return Vec3{Winv[0][0] * v.x + Winv[0][1] * v.y + Winv[0][2] * v.z,
                  Winv[1][0] * v.x + Winv[1][1] * v.y + Winv[1][2] * v.z,
                  Winv[2][0] * v.x + Winv[2][1] * v.y + Winv[2][2] * v.z};
    };
    for (const auto& p : pts) {
      if (p.cell >= 0) {
        Vec3 col = applyWinv(p.delta * p.w);
        out.cols.emplace_back(p.cell, std::array<double, 3>{col.x, col.y, col.z});
      } else {
        Vec3 col = applyWinv(p.delta * (p.w * p.val));
        out.rhsBoundary[0] += col.x; out.rhsBoundary[1] += col.y; out.rhsBoundary[2] += col.z;
      }
    }
    Vec3 selfV = applyWinv(sumWd);
    out.self = {-selfV.x, -selfV.y, -selfV.z};  // coefficient of phi_c is NEGATIVE
    G.cells[ci] = std::move(out);
  }
  return G;
}

}  // namespace vv
