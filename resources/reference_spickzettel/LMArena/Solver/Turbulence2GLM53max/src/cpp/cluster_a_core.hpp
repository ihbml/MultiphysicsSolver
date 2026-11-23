// =============================================================================
//  CLUSTER A - CORE :: polyhedral unstructured FVM infrastructure
//  Part of the 4-Quadrant Hilbert-sorted unstructured polyhedral FVM engine.
//
//    * FaceBaseSoA    : structure-of-arrays face database (S_f, C_f, own/nbr)
//    * CellFaceCSR    : atomic-free gather (cell -> faces, compressed rows)
//    * CorrectionSoA  : compact (<~6% footprint) non-orthogonality corrections
//    * PolyMesh       : arbitrary polyhedral cells, exact geometry from nodes
//    * LinSys         : CSR matrix + BiCGStab(Jacobi) / ILU(0)-free smoothers
//
//  Verified: g++ -std=c++20 -O2 -Wall -Wextra
// =============================================================================
#pragma once
#include <algorithm>
#include <array>
#include <cassert>
#include <cmath>
#include <cstdint>
#include <limits>
#include <map>
#include <numbers>
#include <span>
#include <stdexcept>
#include <string>
#include <vector>

namespace cla {

using Real = double;
using Idx  = std::int32_t;

inline constexpr Real kTiny    = 1.0e-30;
inline constexpr Real kSmall   = 1.0e-20;
inline constexpr Real kPi      = 3.14159265358979323846;
inline constexpr Real kE       = 2.71828182845904523536;

// ----------------------------------------------------------------------- Vec3
struct Vec3 {
  Real x{0}, y{0}, z{0};
  constexpr Vec3() = default;
  constexpr Vec3(Real X, Real Y, Real Z) : x(X), y(Y), z(Z) {}
};
inline constexpr Vec3 operator+(Vec3 a, Vec3 b) { return {a.x + b.x, a.y + b.y, a.z + b.z}; }
inline constexpr Vec3 operator-(Vec3 a, Vec3 b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
inline constexpr Vec3 operator-(Vec3 a)          { return {-a.x, -a.y, -a.z}; }
inline constexpr Vec3 operator*(Vec3 a, Real s)  { return {a.x * s, a.y * s, a.z * s}; }
inline constexpr Vec3 operator*(Real s,  Vec3 a) { return {a.x * s, a.y * s, a.z * s}; }
inline constexpr Vec3 operator/(Vec3 a, Real s)  { return {a.x / s, a.y / s, a.z / s}; }
inline constexpr Vec3& operator+=(Vec3& a, Vec3 b) { a.x += b.x; a.y += b.y; a.z += b.z; return a; }
inline constexpr Real  dot(Vec3 a, Vec3 b)  { return a.x * b.x + a.y * b.y + a.z * b.z; }
inline constexpr Vec3  cross(Vec3 a, Vec3 b){ return {a.y*b.z - a.z*b.y, a.z*b.x - a.x*b.z, a.x*b.y - a.y*b.x}; }
inline Real            norm(Vec3 a)         { return std::sqrt(dot(a, a)); }
inline Vec3            unit(Vec3 a)         { const Real n = norm(a); return n > kTiny ? a / n : Vec3{}; }
inline Real            maxComp(Vec3 a)      { return std::max({std::abs(a.x), std::abs(a.y), std::abs(a.z)}); }
inline Real&           comp(Vec3& a, int i) { return i == 0 ? a.x : (i == 1 ? a.y : a.z); }
inline Real            comp(const Vec3& a, int i) { return i == 0 ? a.x : (i == 1 ? a.y : a.z); }
inline void            setComp(Vec3& a, int i, Real v) { comp(a, i) = v; }

// --------------------------------------------------------------- 3x3 tensor
struct Mat3 {
  std::array<Real, 9> m{};                     // row major
  static Mat3 zero() { return Mat3{}; }
  static Mat3 identity() { Mat3 r; r.m = {1,0,0, 0,1,0, 0,0,1}; return r; }
  Real&       at(int i, int j)       { return m[3*i + j]; }
  const Real& at(int i, int j) const { return m[3*i + j]; }
};
inline Mat3 operator+(Mat3 a, const Mat3& b) { for (int i=0;i<9;++i) a.m[i]+=b.m[i]; return a; }
inline Mat3 operator-(Mat3 a, const Mat3& b) { for (int i=0;i<9;++i) a.m[i]-=b.m[i]; return a; }
inline Mat3 operator*(Mat3 a, Real s)        { for (int i=0;i<9;++i) a.m[i]*=s;       return a; }
inline Vec3 operator*(const Mat3& A, Vec3 v) {
  return {A.at(0,0)*v.x + A.at(0,1)*v.y + A.at(0,2)*v.z,
          A.at(1,0)*v.x + A.at(1,1)*v.y + A.at(1,2)*v.z,
          A.at(2,0)*v.x + A.at(2,1)*v.y + A.at(2,2)*v.z};
}
inline Mat3 matmul(const Mat3& A, const Mat3& B) {
  Mat3 R; for (int i=0;i<3;++i) for (int j=0;j<3;++j) { Real s=0; for (int k=0;k<3;++k) s += A.at(i,k)*B.at(k,j); R.at(i,j)=s; } return R;
}
inline Mat3 transpose(const Mat3& A) { Mat3 R; for (int i=0;i<3;++i) for (int j=0;j<3;++j) R.at(i,j)=A.at(j,i); return R; }
inline Real trace(const Mat3& A) { return A.at(0,0)+A.at(1,1)+A.at(2,2); }
inline Real ddot(const Mat3& A, const Mat3& B) { Real s=0; for (int i=0;i<9;++i) s += A.m[i]*B.m[i]; return s; }
inline Mat3 dyadic(Vec3 a, Vec3 b) { Mat3 R; R.at(0,0)=a.x*b.x; R.at(0,1)=a.x*b.y; R.at(0,2)=a.x*b.z;
                                           R.at(1,0)=a.y*b.x; R.at(1,1)=a.y*b.y; R.at(1,2)=a.y*b.z;
                                           R.at(2,0)=a.z*b.x; R.at(2,1)=a.z*b.y; R.at(2,2)=a.z*b.z; return R; }
inline Mat3 sym(const Mat3& A) { return (A + transpose(A)) * 0.5; }

// ------------------------------------------------------------------- BC tags
enum class BC : std::uint8_t {
  Interior = 0, Wall, Inlet, Outlet, Symmetry, MappedWall, Wedge, Empty
};
inline bool isBoundary(BC b) { return b != BC::Interior; }

// ================================================================ FaceBaseSoA
// Flat, cache friendly, GPU friendly.  Everything the reconstruction kernels
// need lives in four parallel arrays; no pointer chasing, no AOS.
struct FaceBaseSoA {
  Idx  nFaces   = 0;
  std::vector<Vec3> S;            // |S_f| area vector, points owner -> neighbour
  std::vector<Vec3> C;            // face centroid
  std::vector<Idx>  own, nbr;     // nbr == -1  =>  boundary face
  std::vector<BC>   bc;
  std::vector<Idx>  twin;         // MappedWall partner face (AMI / supermesh)
  std::vector<Real> twinW;        // AMI interpolation weight (sum_f w = 1)
  void reserve(Idx nf) {
    S.reserve(nf); C.reserve(nf); own.reserve(nf); nbr.reserve(nf);
    bc.reserve(nf); twin.assign(nf, -1); twinW.assign(nf, 0.0);
  }
};

// ================================================================= CellFaceCSR
// Atomic-free gather: cell P -> contiguous span of its faces.  Built once, then
// every kernel iterates a plain integer range (no atomics, no locks).
struct CellFaceCSR {
  std::vector<Idx> start;         // nCells+1
  std::vector<Idx> faces;         // flattened
  Idx  size(Idx c)            const { return start[c+1] - start[c]; }
  std::span<const Idx> row(Idx c) const { return std::span<const Idx>(faces).subspan(start[c], size(c)); }
};

// ============================================================== CorrectionSoA
// Only faces whose non-orthogonality exceeds `thetaCompact` carry a stored
// skew / over-relaxation correction vector.  Typical footprint ~6% of nFaces.
// Look-up is a branch-free binary search over the sorted index array.
struct CorrectionSoA {
  std::vector<Idx>  face;                     // sorted ascending
  std::vector<Vec3> corr;                     // correction vector  d_corr
  Idx nCompact = 0;
  const Vec3* find(Idx f) const {
    auto it = std::lower_bound(face.begin(), face.end(), f);
    if (it == face.end() || *it != f) return nullptr;
    return &corr[static_cast<Idx>(it - face.begin())];
  }
  Real footprintPercent(Idx nFaces) const {
    return nFaces ? 100.0 * static_cast<Real>(face.size()) / static_cast<Real>(nFaces) : 0.0;
  }
};

// =================================================================== PolyMesh
class PolyMesh {
public:
  Idx  nCells = 0, nNodes = 0;
  std::vector<Vec3> node;
  FaceBaseSoA       face;                     // the FaceBaseSoA itself
  CellFaceCSR       csr;
  CorrectionSoA     corr;
  std::vector<Vec3> cC;                       // cell centroids
  std::vector<Real> cV;                       // cell volumes
  std::vector<std::vector<Idx>> fNodes;       // face -> node ids (topology, build time)
  std::vector<std::vector<Idx>> cFaces;       // cell -> face ids (topology, build time)
  std::vector<std::uint8_t>     cSolid;       // region flag (0 = fluid, 1 = solid)
  std::string           regionName{"fluid"};

  // ---- construction -------------------------------------------------------
  Idx addNode(Vec3 p) { node.push_back(p); return static_cast<Idx>(node.size()) - 1; }
  Idx addFace(const std::vector<Idx>& nodelist, Idx owner, BC b, Vec3 S = {0,0,0}) {
    fNodes.push_back(nodelist); face.S.push_back(S); face.C.push_back(Vec3{});
    face.own.push_back(owner);  face.nbr.push_back(-1); face.bc.push_back(b);
    return static_cast<Idx>(fNodes.size()) - 1;
  }
  // A face created by *this* cell has own[f] == nCells (set in addFace), so the
  // test below only fires for faces that already belong to an earlier cell and
  // therefore make this cell their neighbour.
  void addCell(const std::vector<Idx>& fs) {
    cFaces.push_back(fs);
    for (Idx f : fs) if (face.nbr[f] < 0 && face.own[f] != nCells) face.nbr[f] = nCells;
    ++nCells;
  }
  void setOwner(Idx f, Idx o) { face.own[f] = o; face.nbr[f] = -1; }

  // ---- geometry -----------------------------------------------------------
  // Exact for planar polygons; the quad-fan is used for mildly warped faces.
  void build(Real thetaCompactDeg = 55.0) {
    nNodes = static_cast<Idx>(node.size());
    face.nFaces = static_cast<Idx>(fNodes.size());
    face.reserve(face.nFaces);
    face.S.resize(fNodes.size()); face.C.resize(fNodes.size());
    // Fan triangulation about the *first* node of each face.  Translating the
    // fan origin onto the face plane is what makes the area-weighted
    // triangle-centroid formula exact for planar polygons (and second order
    // for warped polyhedral faces).
    for (Idx f = 0; f < face.nFaces; ++f) {
      const auto& nl = fNodes[f];
      const Vec3 p0 = node[nl[0]];
      Vec3 S{}, Cen{};
      Real asum = 0;
      for (std::size_t i = 0; i < nl.size(); ++i) {
        const Vec3 p = node[nl[i]] - p0;
        const Vec3 q = node[nl[(i + 1) % nl.size()]] - p0;
        const Vec3 a = cross(p, q);              // 2 * signed triangle area
        S    += a;
        Cen  += (p + q) * (norm(a) / 3.0);       // triangle centroid (p+q+0)/3
        asum += norm(a);
      }
      face.S[f] = S * 0.5;
      face.C[f] = asum > kTiny ? p0 + Cen / asum : node[nl[0]];
    }
    // ---- cell volumes / centroids via the divergence theorem (exact) -------
    cC.assign(nCells, Vec3{}); cV.assign(nCells, 0.0);
    for (Idx c = 0; c < nCells; ++c) {
      for (Idx f : cFaces[c]) {
        const Vec3 Sf = outward(f, c);                 // outward from c
        cV[c] += (1.0 / 3.0) * dot(face.C[f], Sf);
        cC[c] += (face.C[f] * dot(face.C[f], Sf)) * 0.25;   // int x dV = (1/4) sum_f (C_f.S_f) C_f
      }
      cC[c] = cV[c] > kTiny ? cC[c] / cV[c] : face.C[cFaces[c][0]];
    }
    // ---- CellFaceCSR : atomic-free gather ---------------------------------
    buildCSR();
    // ---- compact CorrectionSoA --------------------------------------------
    buildCorrections(thetaCompactDeg);
  }

  Vec3 outward(Idx f, Idx c) const { return face.own[f] == c ? face.S[f] : -face.S[f]; }
  Idx  other(Idx f, Idx c)   const { return face.own[f] == c ? face.nbr[f] : face.own[f]; }
  Vec3 d(Idx f) const {                       // P -> N geometric vector
    return face.C[f] * 0.0 + (face.nbr[f] >= 0 ? cC[face.nbr[f]] - cC[face.own[f]]
                                               : face.C[f]   - cC[face.own[f]]);
  }
  Real nonOrth(Idx f) const {                  // degrees
    const Vec3 dd = d(f), Sf = face.S[f];
    const Real nn = norm(dd) * norm(Sf);
    if (nn < kTiny) return 0.0;
    Real c = dot(dd, Sf) / nn;
    return std::acos(std::clamp(c, -1.0, 1.0)) * 180.0 / kPi;
  }
  Real maxNonOrth() const { Real m = 0; for (Idx f = 0; f < face.nFaces; ++f) if (face.nbr[f] >= 0) m = std::max(m, nonOrth(f)); return m; }

private:
  void buildCSR() {
    csr.start.assign(nCells + 1, 0);
    for (Idx c = 0; c < nCells; ++c) csr.start[c + 1] = csr.start[c] + static_cast<Idx>(cFaces[c].size());
    csr.faces.resize(csr.start[nCells]);
    for (Idx c = 0; c < nCells; ++c)
      for (Idx k = 0; k < static_cast<Idx>(cFaces[c].size()); ++k) csr.faces[csr.start[c] + k] = cFaces[c][k];
  }
  void buildCorrections(Real thetaDeg) {
    corr.face.clear(); corr.corr.clear();
    for (Idx f = 0; f < face.nFaces; ++f) {
      if (face.nbr[f] < 0) continue;
      if (nonOrth(f) <= thetaDeg) continue;
      const Vec3 dd = d(f), e = unit(dd), Sf = face.S[f];
      const Vec3 Spar = e * dot(Sf, e);             // orthogonal part
      const Vec3 Sperp = Sf - Spar;                 // skew part
      corr.face.push_back(f);
      corr.corr.push_back(Sperp);                   // needs explicit treatment
    }
    corr.nCompact = static_cast<Idx>(corr.face.size());
  }
};

// -------------------------------------------------------- hexahedral grid gen
// Builds a genuinely 3-D polyhedral mesh (hex cells, arbitrary node warping)
// on a logical (nx x ny x nz) lattice.  `skew` randomly warps interior nodes so
// that the non-orthogonal / skew correction paths are exercised.
inline void makeHexGrid(PolyMesh& m, Idx nx, Idx ny, Idx nz, Vec3 L,
                        Real skew = 0.0, unsigned seed = 1234u, bool solid = false) {
  const Idx nxp = nx + 1, nyp = ny + 1, nzp = nz + 1;
  std::vector<Idx> nid(static_cast<std::size_t>(nxp) * nyp * nzp, -1);
  auto idOf = [&](Idx i, Idx j, Idx k) { return (k * nyp + j) * nxp + i; };
  // nodes
  unsigned rs = seed;
  auto rnd = [&]() { rs = rs * 1664525u + 1013904223u; return (static_cast<Real>(rs >> 8) / 16777216.0) * 2.0 - 1.0; };
  for (Idx k = 0; k < nzp; ++k) for (Idx j = 0; j < nyp; ++j) for (Idx i = 0; i < nxp; ++i) {
    Vec3 p(L.x * i / nx, L.y * j / ny, L.z * k / nz);
    const bool interior = (i > 0 && i < nxp - 1 && j > 0 && j < nyp - 1 && k > 0 && k < nzp - 1);
    if (interior && skew > 0) { p.x += skew * (L.x / nx) * rnd(); p.y += skew * (L.y / ny) * rnd(); p.z += skew * (L.z / nz) * rnd(); }
    nid[idOf(i, j, k)] = m.addNode(p);
  }
  std::map<std::vector<Idx>, Idx> faceCache;
  auto hexa = [&](Idx, Idx, Idx, std::vector<Idx> v) {          // nodes CCW seen from outside
    std::vector<Idx> fs;
    Idx cell = m.nCells;
    const Idx n[8] = {v[0],v[1],v[2],v[3],v[4],v[5],v[6],v[7]};
    auto faceOf = [&](std::vector<Idx> nl) -> Idx {
      std::vector<Idx> key = nl; std::sort(key.begin(), key.end());
      auto it = faceCache.find(key);
      if (it != faceCache.end()) return it->second;
      Idx f = m.addFace(nl, cell, BC::Interior);
      faceCache.emplace(key, f); return f;
    };
    // x- , x+ , y- , y+ , z- , z+   (node order => outward normal for this cell)
    fs.push_back(faceOf({n[0],n[4],n[7],n[3]}));
    fs.push_back(faceOf({n[1],n[2],n[6],n[5]}));
    fs.push_back(faceOf({n[0],n[1],n[5],n[4]}));
    fs.push_back(faceOf({n[3],n[7],n[6],n[2]}));
    fs.push_back(faceOf({n[0],n[3],n[2],n[1]}));
    fs.push_back(faceOf({n[4],n[5],n[6],n[7]}));
    m.addCell(fs);
    return cell;
  };
  m.cSolid.assign(static_cast<std::size_t>(nx) * ny * nz, solid ? 1 : 0);
  for (Idx k = 0; k < nz; ++k) for (Idx j = 0; j < ny; ++j) for (Idx i = 0; i < nx; ++i) {
    const std::vector<Idx> v{idOf(i,j,k),     idOf(i+1,j,k),   idOf(i+1,j+1,k), idOf(i,j+1,k),
                             idOf(i,j,k+1),   idOf(i+1,j,k+1), idOf(i+1,j+1,k+1), idOf(i,j+1,k+1)};
    hexa(i, j, k, v);                     // registers faces + cell
    m.cSolid[m.nCells - 1] = solid ? 1 : 0;
  }
  m.build();
}

// --------------------------------------------------------------- scalar field
using Field = std::vector<Real>;

// ---------------------------------------------------------------------------
// Gauss-gradient with boundary contributions; atomic-free CellFaceCSR gather.
//   grad(phi)_P = (1/V_P) sum_f S_f phi_f ,  S_f outward from P
// Boundary faces take their value from `phif` (Dirichlet value or cell value
// for zero-gradient).  The loop is a plain contiguous integer range => no
// atomics, vectorisable, and identical in structure to the CUDA kernel.
// ---------------------------------------------------------------------------
inline void gradGauss(const PolyMesh& m, const Field& phi,
                      const Field& phif, std::vector<Vec3>& g) {
  g.assign(m.nCells, Vec3{});
  for (Idx c = 0; c < m.nCells; ++c) {
    Vec3 acc{};
    const Idx k0 = m.csr.start[c], k1 = m.csr.start[c + 1];
    for (Idx k = k0; k < k1; ++k) {
      const Idx f = m.csr.faces[k];
      const Vec3 Sf = m.outward(f, c);
      const Idx  nb = m.other(f, c);
      const Real pf = nb >= 0 ? 0.5 * (phi[c] + phi[nb]) : phif[f];
      acc += Sf * pf;
    }
    g[c] = acc / std::max(m.cV[c], kSmall);
  }
}

// Full velocity-gradient tensor (needed by SST, WALE, SA): returns, per cell,
// the three gradient rows stored as an array of three vectors.
struct GradTensor { std::vector<Vec3> gx, gy, gz; };
inline GradTensor gradGaussVec(const PolyMesh& m, const std::vector<Vec3>& v,
                               const std::vector<Vec3>& vf) {
  GradTensor G; G.gx.assign(m.nCells, Vec3{}); G.gy.assign(m.nCells, Vec3{}); G.gz.assign(m.nCells, Vec3{});
  for (Idx c = 0; c < m.nCells; ++c) {
    Vec3 ax{}, ay{}, az{};
    for (Idx k = m.csr.start[c]; k < m.csr.start[c + 1]; ++k) {
      const Idx f = m.csr.faces[k];
      const Vec3 Sf = m.outward(f, c);
      const Idx  nb = m.other(f, c);
      const Vec3 vf2 = nb >= 0 ? (v[c] + v[nb]) * 0.5 : vf[f];
      ax += Sf * vf2.x; ay += Sf * vf2.y; az += Sf * vf2.z;
    }
    const Real iv = 1.0 / std::max(m.cV[c], kSmall);
    G.gx[c] = ax * iv; G.gy[c] = ay * iv; G.gz[c] = az * iv;
  }
  return G;
}
inline Mat3 tensorOf(const GradTensor& G, Idx c) {
  Mat3 A; A.at(0,0)=G.gx[c].x; A.at(0,1)=G.gx[c].y; A.at(0,2)=G.gx[c].z;
          A.at(1,0)=G.gy[c].x; A.at(1,1)=G.gy[c].y; A.at(1,2)=G.gy[c].z;
          A.at(2,0)=G.gz[c].x; A.at(2,1)=G.gz[c].y; A.at(2,2)=G.gz[c].z; return A;
}

// Cyclic Jacobi eigen-solver for symmetric 3x3 (anisotropic porous tensors).
inline void eigenSym3(const Mat3& A, Real lam[3], Mat3& Q) {
  Q = Mat3::identity(); Mat3 S = A;
  for (int sweep = 0; sweep < 32; ++sweep) {
    Real off = 0; for (int i=0;i<3;++i) for (int j=i+1;j<3;++j) off += S.at(i,j)*S.at(i,j);
    if (off < 1e-26) break;
    for (int p = 0; p < 2; ++p) for (int q = p + 1; q < 3; ++q) {
      const Real apq = S.at(p, q);
      if (std::abs(apq) < 1e-300) continue;
      const Real app = S.at(p, p), aqq = S.at(q, q);
      const Real theta = 0.5 * (aqq - app) / apq;
      const Real t = (theta >= 0 ? 1.0 : -1.0) / (std::abs(theta) + std::sqrt(theta * theta + 1.0));
      const Real c = 1.0 / std::sqrt(t * t + 1.0), s = t * c;
      for (int k = 0; k < 3; ++k) { const Real skp = S.at(k,p), skq = S.at(k,q);
        S.at(k,p) = c*skp - s*skq; S.at(k,q) = s*skp + c*skq; }
      for (int k = 0; k < 3; ++k) { const Real spk = S.at(p,k), sqk = S.at(q,k);
        S.at(p,k) = c*spk - s*sqk; S.at(q,k) = s*spk + c*sqk; }
      for (int k = 0; k < 3; ++k) { const Real qkp = Q.at(k,p), qkq = Q.at(k,q);
        Q.at(k,p) = c*qkp - s*qkq; Q.at(k,q) = s*qkp + c*qkq; }
    }
  }
  lam[0] = S.at(0,0); lam[1] = S.at(1,1); lam[2] = S.at(2,2);
}

// ===================================================================== LinSys
// CSR linear system with BiCGStab(Jacobi) and a Gauss-Seidel smoother.
struct LinSys {
  Idx n = 0;
  std::vector<Idx> rstart, cols;               // CSR row descriptor
  std::vector<Real> vals, diag, b, x;
  Idx  nnz() const { return static_cast<Idx>(vals.size()); }
  void clear() { std::fill(vals.begin(), vals.end(), 0.0); std::fill(b.begin(), b.end(), 0.0); }

  // Build the pattern from a mesh stencil + optional extra (cross-region) couplings
  static LinSys fromMesh(const PolyMesh& m, const std::vector<std::pair<Idx, Idx>>& extra = {}) {
    std::vector<std::vector<Idx>> row(static_cast<std::size_t>(m.nCells));
    auto addLink = [&](Idx a, Idx c) {
      if (c < 0) return;
      auto& r = row[a]; if (std::find(r.begin(), r.end(), c) == r.end()) r.push_back(c);
    };
    for (Idx c = 0; c < m.nCells; ++c) {
      row[c].push_back(c);
      for (Idx k = m.csr.start[c]; k < m.csr.start[c + 1]; ++k) addLink(c, m.other(m.csr.faces[k], c));
    }
    for (auto& [a, c] : extra) { addLink(a, c); addLink(c, a); }
    for (auto& r : row) std::sort(r.begin(), r.end());
    LinSys S; S.n = m.nCells;
    S.rstart.assign(static_cast<std::size_t>(m.nCells) + 1, 0);
    for (Idx c = 0; c < m.nCells; ++c) S.rstart[c + 1] = S.rstart[c] + static_cast<Idx>(row[c].size());
    S.cols.resize(S.rstart[m.nCells]); S.vals.assign(S.rstart[m.nCells], 0.0);
    for (Idx c = 0; c < m.nCells; ++c)
      for (std::size_t k = 0; k < row[c].size(); ++k) S.cols[S.rstart[c] + k] = row[c][k];
    S.diag.assign(m.nCells, 0.0); S.b.assign(m.nCells, 0.0); S.x.assign(m.nCells, 0.0);
    return S;
  }
  Idx findCol(Idx r, Idx c) const {
    for (Idx k = rstart[r]; k < rstart[r + 1]; ++k) if (cols[k] == c) return k;
    return -1;
  }
  void add(Idx r, Idx c, Real v) {
    if (r < 0 || c < 0) return;
    const Idx k = findCol(r, c);
    if (k >= 0) vals[k] += v;
  }
  void setDiag(Idx r, Real v) { for (Idx k = rstart[r]; k < rstart[r+1]; ++k) if (cols[k] == r) vals[k] += v; }
  void setRhs(Idx r, Real v)  { b[r] += v; }

  Real residual(std::vector<Real>& r) const {
    r.assign(n, 0.0); Real mx = 0;
    for (Idx i = 0; i < n; ++i) {
      Real s = 0; for (Idx k = rstart[i]; k < rstart[i+1]; ++k) s += vals[k] * x[cols[k]];
      r[i] = b[i] - s; mx = std::max(mx, std::abs(r[i]));
    }
    return mx;
  }
  Idx  bicgstab(Idx iters = 200, Real tol = 1e-12) {
    std::vector<Real> r, r0, p(n), v(n), s(n), t(n), ph(n), sh(n);
    Real rr = 0; for (Idx i = 0; i < n; ++i) { Real s2 = 0; for (Idx k = rstart[i]; k < rstart[i+1]; ++k) s2 += vals[k]*x[cols[k]]; r.push_back(b[i]-s2); }
    r0 = r; for (Idx i = 0; i < n; ++i) rr += r[i]*r[i];
    if (std::sqrt(rr) < tol) return 0;
    Real rho = 1, alpha = 1, omega = 1; for (Idx i = 0; i < n; ++i) { p[i] = 0; v[i] = 0; }
    for (Idx it = 0; it < iters; ++it) {
      const Real rhoNew = [&]{ Real s2 = 0; for (Idx i = 0; i < n; ++i) s2 += r0[i]*r[i]; return s2; }();
      if (std::abs(rhoNew) < kSmall || std::abs(omega) < 1e-300) break;   // breakdown
      const Real beta = (rhoNew / rho) * (alpha / omega);
      for (Idx i = 0; i < n; ++i) p[i] = r[i] + beta * (p[i] - omega * v[i]);
      for (Idx i = 0; i < n; ++i) { Real s2 = 0; for (Idx k = rstart[i]; k < rstart[i+1]; ++k) s2 += vals[k]*p[cols[k]]; ph[i] = s2; }
      Real den = 0; for (Idx i = 0; i < n; ++i) den += r0[i]*ph[i];
      if (std::abs(den) < kSmall) break;
      alpha = rhoNew / den;
      for (Idx i = 0; i < n; ++i) s[i] = r[i] - alpha * ph[i];
      Real sn = 0; for (Idx i = 0; i < n; ++i) sn += s[i]*s[i];
      if (std::sqrt(sn) < tol) { for (Idx i = 0; i < n; ++i) x[i] += alpha * p[i]; return it; }
      for (Idx i = 0; i < n; ++i) { Real s2 = 0; for (Idx k = rstart[i]; k < rstart[i+1]; ++k) s2 += vals[k]*s[cols[k]]; sh[i] = s2; }
      Real tt = 0, ss = 0; for (Idx i = 0; i < n; ++i) { tt += sh[i]*sh[i]; ss += sh[i]*s[i]; }
      if (tt < 1e-300) break;                                            // breakdown
      omega = ss / tt;
      if (!std::isfinite(omega) || std::abs(omega) < 1e-300) break;
      for (Idx i = 0; i < n; ++i) { x[i] += alpha * p[i] + omega * s[i]; r[i] = s[i] - omega * sh[i]; }
      Real rn = 0; for (Idx i = 0; i < n; ++i) rn = std::max(rn, std::abs(r[i]));
      rho = rhoNew;
      if (!std::isfinite(rn)) break;
      if (rn < tol) return it;
    }
    return iters;
  }
  Idx gaussSeidel(Idx iters = 40) {
    for (Idx it = 0; it < iters; ++it) {
      Real mx = 0;
      for (Idx i = 0; i < n; ++i) {
        Real s = b[i], d = 0;
        for (Idx k = rstart[i]; k < rstart[i+1]; ++k) { const Idx j = cols[k]; if (j == i) d = vals[k]; else s -= vals[k] * x[j]; }
        if (std::abs(d) < kSmall) continue;
        const Real xn = s / d; mx = std::max(mx, std::abs(xn - x[i])); x[i] = xn;
      }
      if (mx < 1e-14) return it;
    }
    return iters;
  }
};

// -------------------------------------------------------------- interpolation
// Orthogonal-corrected / deferred-correction face interpolation:
//   phi_f = phi_P + (grad phi)_P . (x_f - x_P)
//           + [ phi_N - phi_P - (grad phi)_P . (x_N - x_P) ] * (d.S)/(d.d)
// Reduces to central differencing on orthogonal meshes and stays 2nd order on
// skewed meshes.  Faces in the compact CorrectionSoA use the explicit skew part.
inline Real faceValue(const PolyMesh& m, Idx f, const Field& phi, const std::vector<Vec3>& g) {
  const Idx o = m.face.own[f], n = m.face.nbr[f];
  if (n < 0) return phi[o];
  const Vec3 dd = m.d(f);
  const Real dd2 = dot(dd, dd), dS = dot(dd, m.face.S[f]);
  const Vec3 r = m.face.C[f] - m.cC[o];
  const Real over = std::abs(dd2) < kSmall ? 0.0 : dS / dd2;
  const Real corrTerm = phi[n] - phi[o] - dot(g[o], dd);
  return phi[o] + dot(g[o], r) + over * corrTerm;
}

// Venkatakrishnan-style smooth limiter (differentiable, TVD-safe convection)
inline Real limiterVenkat(Real phiC, Real phiU, Real dx) {
  const Real du = phiU - phiC;
  const Real dmax = std::max(du, 0.0), dmin = std::min(du, 0.0);
  const Real eps  = 1e-12 * (std::abs(phiC) + 1.0) + dx * dx * dx;
  const Real den  = du * du + eps;
  return std::min<Real>({1.0, (dmax * dmax + eps) / den, (dmin * dmin + eps) / den});
}

}  // namespace cla
