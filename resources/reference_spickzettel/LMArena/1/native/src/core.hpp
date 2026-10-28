// Shared types and declarations for the FVM/SPH mesher core.
//
// Method references implemented in this library
// ---------------------------------------------
// [W1] Z. Wang, O.J. Haidn, X. Hu, "Incorporating the algorithm for the
//      boundary condition from FVM into the framework of Eulerian SPH",
//      Comput. Phys. Commun. 307 (2025) 109429 (arXiv:2312.03542).
//      -> body-fitted particle relaxation, zero-order consistency
//         sum_j V_j grad W_ij = 0, divergence-of-position boundary detector
//         (gamma = 0.75 d), boundary-surface flux contribution A_iS = -sum_P A_iP
//         stored in the inner configuration, kernel-gradient correction B_i.
// [W2] Y.-D. Zhang, F.-R. Ming, P.-P. Wang, D.-F. Liang, "An adaptive SPH-FVM
//      method with optimized particle-mesh interpolation scheme for strongly
//      compressible multiphase flows", Int. J. Multiphase Flow 189 (2025) 105256.
//      -> adaptive particle/mesh domain split (core / buffer / mesh) and the
//         optimized particle-mesh interpolation scheme (OPMIS): kernel-cell
//         overlap quadrature + linear moment (reproducing) correction.
// [W3] P. Du, Y. Li, M. Xu, J.-X. Wang, "DiFVM: A Vectorized Graph-Based Finite
//      Volume Solver for Differentiable CFD on Unstructured Meshes",
//      arXiv:2603.15920.
//      -> collocated cell-centred polyhedral mesh topology exported as static
//         gather/scatter graph arrays (owner/neighbour CSR, face area vectors,
//         cell centres/volumes, over-relaxed non-orthogonality correction).
#pragma once

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <functional>
#include <limits>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "json.hpp"

namespace mesher {

using Real = double;
constexpr Real kPi = 3.14159265358979323846;
constexpr Real kInf = std::numeric_limits<Real>::infinity();

// ---------------------------------------------------------------- linalg ----
struct V2 {
  Real x = 0, y = 0;
  V2() = default;
  V2(Real a, Real b) : x(a), y(b) {}
  V2& operator+=(const V2& o) {
    x += o.x;
    y += o.y;
    return *this;
  }
  V2& operator-=(const V2& o) {
    x -= o.x;
    y -= o.y;
    return *this;
  }
  V2& operator*=(Real s) {
    x *= s;
    y *= s;
    return *this;
  }
};
inline V2 operator+(V2 a, const V2& b) { return a += b; }
inline V2 operator-(V2 a, const V2& b) { return a -= b; }
inline V2 operator*(V2 a, Real s) { return a *= s; }
inline V2 operator*(Real s, V2 a) { return a *= s; }
inline V2 operator/(V2 a, Real s) { return a *= (1.0 / s); }
inline Real dot(const V2& a, const V2& b) { return a.x * b.x + a.y * b.y; }
inline Real cross(const V2& a, const V2& b) { return a.x * b.y - a.y * b.x; }
inline Real norm2(const V2& a) { return dot(a, a); }
inline Real norm(const V2& a) { return std::sqrt(dot(a, a)); }
inline V2 perp(const V2& a) { return V2(a.y, -a.x); }
inline V2 normalized(V2 a) {
  const Real n = norm(a);
  return n > 1e-300 ? a / n : V2(0, 0);
}
inline Real orient2d(const V2& a, const V2& b, const V2& c) {
  return (b.x - a.x) * (c.y - a.y) - (b.y - a.y) * (c.x - a.x);
}
struct AABB {
  V2 lo{1e30, 1e30}, hi{-1e30, -1e30};
  void expand(const V2& p) {
    lo.x = std::min(lo.x, p.x);
    lo.y = std::min(lo.y, p.y);
    hi.x = std::max(hi.x, p.x);
    hi.y = std::max(hi.y, p.y);
  }
  V2 center() const { return (lo + hi) * 0.5; }
  V2 size() const { return hi - lo; }
  bool valid() const { return hi.x >= lo.x && hi.y >= lo.y; }
};

// ------------------------------------------------------------------ spec ----
struct Sdf {
  std::function<Real(V2)> f;
  Real operator()(const V2& p) const { return f(p); }
  V2 grad(const V2& p, Real eps = 1e-6) const {
    return V2((f(V2(p.x + eps, p.y)) - f(V2(p.x - eps, p.y))) / (2 * eps),
              (f(V2(p.x, p.y + eps)) - f(V2(p.x, p.y - eps))) / (2 * eps));
  }
  // Projection onto the zero level set (unit-normalised, robust for
  // non-exact distance fields such as the radial star/ellipse approximations).
  V2 project(const V2& p) const {
    const Real d = f(p);
    V2 g = grad(p, 1e-5);
    const Real gn = norm(g);
    if (gn < 1e-12) return p;
    return p - g * (d / gn);
  }
};

struct BoundaryRule {
  std::string name = "wall";
  std::string bc = "wall";      // wall|noslip|reflecting|inflow|outflow|farfield|nonreflecting|symmetry|moving
  std::string where = "all";    // all|xmin|xmax|ymin|ymax|xrange|yrange|circle|hole|outer|body
  V2 a{0, 0}, b{0, 0};
  Real radius = 0;
  int priority = 0;
};

struct RefineTarget {
  int kind = 0;  // 0 point, 1 circle, 2 box
  V2 c{0, 0};
  Real r = 0;
  V2 lo{0, 0}, hi{0, 0};
  Real hNear = 0;
  Real width = 0;
  int level = 1;  // APR level requested inside this target
};

struct FvmSpec {
  bool enabled = true;
  Real base = 0.05;          // background cell size
  Real minSize = 0.0;        // smallest allowed size (0 -> base/8)
  int contourSamples = 0;    // 0 -> derived from base
  int smoothIters = 30;
  Real smoothWeight = 0.5;
  int blCount = 0;           // boundary layer rows
  Real blFirst = 0;
  Real blGrowth = 1.2;
  std::string blPatch = "wall";
  bool blOnlyMatching = true;
  std::vector<RefineTarget> targets;
  std::vector<BoundaryRule> rules;
  bool vertexDual = false;   // additionally emit the median-dual (vertex centred) graph
};

struct SphSpec {
  bool enabled = true;
  Real dp = 0.05;
  int kernel = 0;            // 0 WendlandC2, 1 cubic spline, 2 quartic spline
  Real hRatio = 2.6;         // kernel support radius R = hRatio*dp = 2*h with h = 1.3*dp
                             // (SPHinXsys convention: kernel_ratio 1.3, support 2h)
  bool relax = true;
  int relaxIters = 400;
  Real relaxCoeff = 0.35;
  bool snapBoundary = true;
  Real bandFactor = 0.6;     // particles within bandFactor*dp of the wall are snapped
  int levels = 1;            // multi-resolution levels (dp_l = dp / 2^l)
  int levelKind = 0;         // 0 off, 1 distance-to-target, 2 core region
  std::vector<RefineTarget> levelTargets;
  Real levelRadius = 0;
  int volumeModel = 0;       // 0 nominal dp^2, 1 Shepard 1/sum W
  bool gradientCorrection = true;   // [W1] first-order kernel-gradient correction B_i
  Real gammaFactor = 0.75;          // [W1] boundary detector threshold = gamma*d
  int boundaryLayers = 2;           // number of wall-adjacent particle layers treated as boundary
};

struct CouplingSpec {
  bool enabled = true;
  std::string mode = "adaptive";     // adaptive | fixed | sphOnly
  int coreKind = 1;                  // 1 circle, 2 box, 3 whole domain
  V2 coreCenter{0, 0};
  Real coreRadius = 0;
  V2 coreLo{0, 0}, coreHi{0, 0};
  Real buffer = 0;                   // 0 -> 2*dp
  std::string p2m = "opmis";         // opmis | shepard | nearest
  std::string m2p = "opmis";
  bool momentCorrection = true;
  int quadrature = 3;                // 1 centroid, 3 midpoint(4-pt), 7 point rule
  bool patchTest = true;
};

struct OutputSpec {
  std::string dir = ".";
  std::vector<std::string> formats{"vtu", "msh", "json"};
  int previewLimit = 20000;
  bool preview = true;
};

struct Spec {
  std::string name = "case";
  Sdf geom;
  mj::Value geomJson;
  AABB domain;          // finite bounding box of the computational domain
  FvmSpec fvm;
  SphSpec sph;
  CouplingSpec cpl;
  OutputSpec out;
  int threads = 0;
  mj::Value raw;
};

Spec parseSpec(const mj::Value& root);
AABB sdfBounds(const mj::Value& node);

// ------------------------------------------------------------- utilities ----
struct Timer {
  std::map<std::string, Real> ms;
  std::chrono::steady_clock::time_point t0 = std::chrono::steady_clock::now();
  void add(const std::string& k, const std::chrono::steady_clock::time_point& a,
           const std::chrono::steady_clock::time_point& b) {
    ms[k] += std::chrono::duration<Real, std::milli>(b - a).count();
  }
  Real total() const {
    Real s = 0;
    for (auto& kv : ms) s += kv.second;
    return s;
  }
};
struct ScopedTimer {
  Timer& t;
  const std::string k;
  std::chrono::steady_clock::time_point a = std::chrono::steady_clock::now();
  ScopedTimer(Timer& tt, std::string key) : t(tt), k(std::move(key)) {}
  ~ScopedTimer() { t.add(k, a, std::chrono::steady_clock::now()); }
};

// Uniform background grid used for all neighbour queries.
struct Grid {
  V2 lo;
  Real cell = 1;
  int nx = 1, ny = 1;
  std::vector<int> head;         // (nx*ny) -> first particle index or -1
  std::vector<int> next;         // per item -> next in bucket

  void build(const AABB& box, Real cellSize, size_t n, const V2* pts) {
    lo = box.lo;
    cell = std::max(cellSize, 1e-12);
    nx = std::max(1, static_cast<int>(std::ceil((box.hi.x - box.lo.x) / cell)) + 1);
    ny = std::max(1, static_cast<int>(std::ceil((box.hi.y - box.lo.y) / cell)) + 1);
    head.assign(static_cast<size_t>(nx) * ny, -1);
    next.assign(n, -1);
    for (size_t i = 0; i < n; ++i) insert(static_cast<int>(i), pts[i]);
  }
  void insert(int idx, const V2& p) {
    int cx = static_cast<int>((p.x - lo.x) / cell);
    int cy = static_cast<int>((p.y - lo.y) / cell);
    cx = std::clamp(cx, 0, nx - 1);
    cy = std::clamp(cy, 0, ny - 1);
    const size_t b = static_cast<size_t>(cy) * nx + cx;
    next[idx] = head[b];
    head[b] = idx;
  }
  template <class F>
  void forNear(const V2& p, Real radius, F&& fn) const {
    const int span = static_cast<int>(std::ceil(radius / cell));
    int cx = static_cast<int>((p.x - lo.x) / cell);
    int cy = static_cast<int>((p.y - lo.y) / cell);
    for (int j = cy - span; j <= cy + span; ++j) {
      if (j < 0 || j >= ny) continue;
      for (int i = cx - span; i <= cx + span; ++i) {
        if (i < 0 || i >= nx) continue;
        int idx = head[static_cast<size_t>(j) * nx + i];
        while (idx >= 0) {
          fn(idx);
          idx = next[idx];
        }
      }
    }
  }
};

// ------------------------------------------------------------------ mesh ----
struct Face {
  int owner = -1;
  int neighbour = -1;   // -1 for boundary faces
  int patch = -1;       // index into patchNames for boundary faces
  V2 c;                 // face centre
  V2 sf;                // area vector, |sf| = face length, oriented owner -> neighbour
  V2 delta;             // cN - cO
  Real len = 0;
  Real nonOrtho = 0;    // angle(Sf, delta) in degrees
  Real deltaCoeff = 0;  // 1 / (delta . n_hat)
  Real weight = 0.5;    // linear interpolation weight of the owner
  V2 corr;              // over-relaxed non-orthogonality correction vector T = Sf - E
};

struct Mesh {
  bool valid = false;
  int dim = 2;
  std::vector<V2> nodes;
  std::vector<std::array<int, 3>> cells;     // triangle connectivity (node ids)
  std::vector<V2> cc;                        // cell centres
  std::vector<Real> vol;                     // cell volumes (areas in 2D)
  std::vector<Face> faces;
  std::vector<int> interiorFaces;
  std::vector<int> boundaryFaces;
  std::vector<std::string> patchNames;
  std::vector<std::string> patchBc;
  std::vector<int> patchFaceCount;
  std::vector<int> patchNodeLoop;            // 1 = boundary contour node
  // DiFVM static graph arrays [W3] ----------------------------------------
  std::vector<int> gOwner, gNeighbour, gPatch;
  std::vector<Real> gSf;         // 2*nf
  std::vector<Real> gCf;         // 2*nf
  std::vector<Real> gMagSf, gDeltaCoeffs, gWeights, gNonOrtho, gCorr;
  std::vector<int> cellFacePtr, cellFaceIdx;    // cell -> face CSR
  std::vector<int> nodeCellPtr, nodeCellIdx;    // node -> cell CSR (vertex dual)
  std::vector<int> cellNodeFlat;                // 3*nc connectivity (vectorised gather)
  std::vector<Real> nodeX;                      // 2*nn flattened coordinates
  // median dual (vertex centred) volumes, only if requested
  std::vector<Real> dualVol;
  // quality ----------------------------------------------------------------
  Real minAspect = 0, maxAspect = 0, avgAspect = 0;
  Real maxSkew = 0, avgSkew = 0;
  Real maxNonOrtho = 0, avgNonOrtho = 0, p99NonOrtho = 0;
  Real minVol = 0, maxVol = 0, totalVol = 0;
  Real minEdge = 0, maxEdge = 0;
  int nNegativeVol = 0;
  Real domainArea = 0, perimeter = 0;
  AABB box;
};

// -------------------------------------------------------------- particles ----
enum ParticleCat : int {
  PCAT_INTERIOR = 0,
  PCAT_BOUNDARY = 1,   // [W1] inside the boundary layer, missing kernel support
  PCAT_GHOST = 2,      // wall / dummy particles outside the fluid domain
  PCAT_CORE = 3,       // [W2] adaptive SPH core region
  PCAT_BUFFER = 4,     // [W2] SPH/FVM overlap (buffer) region
};

struct Particles {
  bool valid = false;
  int dim = 2;
  std::vector<V2> pos;
  std::vector<Real> volume;      // V_i
  std::vector<Real> dp;          // local particle spacing
  std::vector<Real> h;           // support radius
  std::vector<int> level;        // multi-resolution level
  std::vector<int> category;     // ParticleCat
  std::vector<int> bcType;       // index into bcNames, -1 if none
  std::vector<int> patch;        // nearest boundary patch for boundary particles
  std::vector<int> region;       // 0 mesh-only, 1 core, 2 buffer  [W2]
  // [W1] zero-order consistency / FVM-equivalent boundary storage
  std::vector<V2> aSum;          // sum_j V_j gradW_ij  (inner configuration)
  std::vector<V2> aBnd;          // A_iS = -aSum        (boundary surface contribution)
  std::vector<Real> aBndMag;     // |A_iS|
  std::vector<V2> nBnd;          // unit normal of the boundary surface contribution
  std::vector<Real> divPos;      // divergence of position, ~ d away from boundaries
  std::vector<Real> isBoundary;  // 1 if divPos < gamma*d
  std::vector<Real> wallDist;    // distance to the physical boundary
  std::vector<Real> consistency; // |sum_j V_j gradW_ij| residual
  std::vector<std::array<Real, 4>> bMat;  // kernel-gradient correction matrix B_i
  // neighbour CSR (the "inner configuration" of SPHinXsys) -----------------
  std::vector<int> nbrPtr, nbrIdx;
  std::vector<Real> nbrDwdr;     // dW/dr * V_j
  std::vector<Real> nbrR;        // r_ij
  std::vector<int> nbrCount;
  // aggregates -------------------------------------------------------------
  int nInterior = 0, nBoundary = 0, nGhost = 0, nCore = 0, nBuffer = 0;
  Real sumA = 0, maxA = 0, meanA = 0;
  Real meanAInterior = 0, maxAInterior = 0;   // residual over particles with full support
  Real shepardVolumeSum = 0;
  Real meanDivPos = 0, minDivPos = 0;
  Real bndAreaSum = 0;           // sum |A_iS| over boundary particles ~ perimeter
  Real cvDp = 0;                 // coefficient of variation of the spacing
  Real meanNbrs = 0;
  Real relaxResidual0 = 0, relaxResidual1 = 0;
  int nInteriorRef = 0;        // particles used for the interior residual statistics
  Real sphRegionArea = 0;      // area actually covered by the retained particles
  int relaxItersDone = 0;
  std::vector<std::string> bcNames;
  Real kernelSupport = 0;
  std::string kernelName = "wendlandC2";
  AABB box;
};

// -------------------------------------------------------------- coupling ----
struct CsrMatrix {
  int rows = 0, cols = 0;
  std::vector<int> rowPtr, col;
  std::vector<Real> val;
  size_t nnz() const { return col.size(); }
};

struct PatchTest {
  std::vector<std::string> fields;
  std::vector<Real> l2;          // per field, per scheme
  std::vector<std::string> schemes;
};

struct Coupling {
  bool valid = false;
  std::vector<int> cellRegion;      // 0 fvm, 1 buffer, 2 ghost(core)
  std::vector<int> cellCoupling;    // 1 if the cell touches the SPH/FVM interface
  std::vector<int> particleRegion;  // ParticleCat-like: 3 core, 4 buffer
  std::vector<Real> cellVolumeFrac; // particle volume fraction per coupling cell
  CsrMatrix p2m, m2p;
  std::vector<int> interfaceFaces;  // face ids forming the coupling interface
  std::vector<int> interfaceParticles;
  int nCoreCells = 0, nBufferCells = 0, nGhostCells = 0, nInterfaceFaces = 0;
  int nCoreParticles = 0, nBufferParticles = 0;
  Real coreArea = 0, bufferArea = 0;
  Real p2mRowSumErr = 0, m2pRowSumErr = 0;
  PatchTest test;
  std::string p2mScheme, m2pScheme;
  Real bufferWidth = 0;
};

// ---------------------------------------------------------------- result ----
struct Result {
  Spec spec;
  Real sphBndAreaFull = 0;   // [W1] half-perimeter identity before the coupling trim
  Real sphVolumeFull = 0;    // particle volume sum before the coupling trim
  Mesh mesh;
  Particles parts;
  Coupling coupling;
  Timer timer;
  std::vector<std::string> notes;
  std::vector<std::string> warnings;
  std::vector<std::string> artifacts;
  Real totalMs = 0;
  std::string engine;
  int threadsUsed = 1;
};

// ------------------------------------------------------------- module APIs ----
Sdf buildSdf(const mj::Value& node);
Real sizeField(const FvmSpec& f, const V2& p, const Sdf& s);
Real sizeFieldMin(const FvmSpec& f, const Sdf& s);

struct ContourLoops {
  std::vector<std::vector<V2>> loops;   // oriented: CCW outer, CW holes
  std::vector<int> isHole;
  AABB box;
  Real area = 0, perimeter = 0;
};
ContourLoops extractContours(const Sdf& s, const AABB& searchBox, Real sampleSpacing);
bool pointInDomainLoops(const V2& p, const ContourLoops& c);

Mesh buildFvmMesh(const Spec& spec, const ContourLoops& contours, Timer& timer,
                  std::vector<std::string>& warnings);

struct KernelInfo {
  int id = 0;
  std::string name = "wendlandC2";
  Real norm = 1;      // normalisation for support radius R (2D)
  Real support = 1;   // cutoff in units of h
};
KernelInfo kernelInfo(int id);
// W and dW/dr as functions of r with support radius R (already dimensional).
Real kernelW(int id, Real r, Real R);
Real kernelDwdr(int id, Real r, Real R);
Real kernelNormConst(int id, Real R);

Particles buildSphParticles(const Spec& spec, const ContourLoops& contours, Timer& timer,
                            std::vector<std::string>& warnings);
void computeConsistency(const Spec& spec, Particles& p, Timer& timer);
// Recompute all particle aggregates (categories, consistency statistics).
void finalizeParticleStats(const Spec& spec, Particles& p);

Coupling buildCoupling(const Spec& spec, const Mesh& mesh, Particles& parts, Timer& timer,
                       std::vector<std::string>& warnings);

void writeArtifacts(Result& r);
void writeManifest(const Result& r, const std::string& path);

// small helpers shared across TUs
inline bool fileExists(const std::string& path);
int bcTypeFromName(const std::string& s);
std::string bcNameFromType(int t);

}  // namespace mesher
