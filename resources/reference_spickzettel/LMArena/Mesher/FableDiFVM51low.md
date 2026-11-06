Below is a complete, standalone implementation of **only the DiFVM Mesher**: the component that turns an unstructured polyhedral mesh (OpenFOAM `constant/polyMesh/`) into the static, 64‑byte‑aligned SoA graph arrays (`owner`, `neighbour`, `S_f`, `x_f`, `Δ_f`, `k_f`, …) plus the precomputed **`MeshErrorMap`** (`θ_f`, `m_f^skew`, volume‑jump ratio, `β_f`, `ω_i`, `k_f^comp`) that the Graph‑FVM solver later consumes with zero runtime overhead.

## Language choice: C++20

| Candidate | Verdict for *this* task |
|---|---|
| **C++20** ✅ | Irregular topology work (CSR construction, RCM renumbering, sorting, pyramid decomposition) is pointer/index-heavy and branchy — exactly what C++ with OpenMP does best. Natural home for the SoA memory layout that the C++ Graph-FVM backbone, the Fortran kernels (`iso_c_binding`) and the Rust layer all read through one C ABI. |
| Fortran | Excellent for the dense flux/linear-algebra kernels later, poor fit for parsing, dictionaries, graph permutations and dynamic topology. |
| Rust | Great for the I/O/mmap/streaming layer (as planned), but the mesher is one component the C++ core must own so the solver's SoA layout, allocator and index type are defined in a single place. |

Performance properties: one pass over faces for geometry, one pass over cells (via a prebuilt cell→face CSR, gather-style, no atomics), all `#pragma omp parallel for`, RCM renumbering + upper-triangular face ordering for cache locality and direct CSR assembly, `std::from_chars` parsing (no `iostream`/`strtod` locale overhead).

---

## File layout

```
backend/native_cpp/mesh/
├── graph_mesh.h              # SoA types, options, C++ API
├── polymesh_reader.cc        # OpenFOAM ASCII polyMesh parser
├── graph_mesh_builder.cc     # RCM, geometry, graph arrays, MeshErrorMap
├── graph_mesh_c_api.h        # C ABI (Rust / Fortran / Python ctypes)
├── graph_mesh_c_api.cc
├── mesh_check_main.cc        # CLI: build + quality report
└── CMakeLists.txt
```

---

### `graph_mesh.h`

```cpp
// Copyright 2025 CFDSolver. Google C++ Style, C++20.
// DiFVM Mesher: static graph-FVM mesh (SoA, 64-byte aligned) + MeshErrorMap.
#pragma once

#include <cstddef>
#include <cstdint>
#include <new>
#include <string>
#include <vector>

namespace difvm {

// 64-bit indices: meshes of 1e8–1e9 cells exceed int32 face counts.
using Index = std::int64_t;

// ---------------------------------------------------------------------------
// 64-byte aligned allocator (AVX-512 line / cache line).
// ---------------------------------------------------------------------------
template <class T, std::size_t kAlign = 64>
struct AlignedAllocator {
  using value_type = T;
  static constexpr std::align_val_t kAlignVal{kAlign};

  AlignedAllocator() noexcept = default;
  template <class U>
  AlignedAllocator(const AlignedAllocator<U, kAlign>&) noexcept {}

  T* allocate(std::size_t n) {
    return static_cast<T*>(::operator new(n * sizeof(T), kAlignVal));
  }
  void deallocate(T* p, std::size_t) noexcept { ::operator delete(p, kAlignVal); }

  template <class U>
  struct rebind { using other = AlignedAllocator<U, kAlign>; };
  bool operator==(const AlignedAllocator&) const noexcept { return true; }
  bool operator!=(const AlignedAllocator&) const noexcept { return false; }
};

template <class T>
using AVec = std::vector<T, AlignedAllocator<T>>;

// Structure-of-Arrays vector field.
struct Vec3SoA {
  AVec<double> x, y, z;
  void Resize(std::size_t n) { x.resize(n); y.resize(n); z.resize(n); }
  std::size_t size() const { return x.size(); }
};

struct Patch {
  std::string name;
  std::string type;   // wall, patch, empty, symmetry, ...
  Index start = 0;    // first face index (global face numbering)
  Index size = 0;
};

// ---------------------------------------------------------------------------
// Raw topology as read from disk (points + face->point CSR + owner/neighbour).
// ---------------------------------------------------------------------------
struct PolyMesh {
  Index n_cells = 0;
  std::vector<double> xyz;          // AoS, 3*n_points (only used during build)
  std::vector<Index> face_offset;   // CSR, size n_faces+1
  std::vector<Index> face_points;   // CSR payload
  std::vector<Index> owner;         // n_faces
  std::vector<Index> neighbour;     // n_internal_faces
  std::vector<Patch> patches;
};

// ---------------------------------------------------------------------------
// MeshErrorMap tuning (all smooth, branch-free tanh shaping).
// ---------------------------------------------------------------------------
struct ErrorMapOptions {
  // k_f^comp = lambda(theta) * k_f with
  //   lambda = 1 - (1-lambda_min) * tanh^2((theta - theta_damp)/width)  for theta>theta_damp
  double theta_damp_deg = 65.0;
  double theta_damp_width_deg = 10.0;
  double lambda_min = 0.2;

  // beta_f = Q(theta) * Q(skew) * Q(vol_ratio-1), Q = normalised 0.5(1-tanh((q-qc)/w))
  double theta_beta_deg = 70.0, theta_beta_width_deg = 5.0;
  double skew_beta = 0.5, skew_beta_width = 0.15;
  double vol_ratio_beta = 8.0, vol_ratio_width = 4.0;

  // omega_i = omega_min + (1-omega_min) * min_f(quality of faces of cell i)
  double omega_min = 0.25;
};

struct BuildOptions {
  bool renumber_rcm = true;   // Reverse Cuthill-McKee cell renumbering
  ErrorMapOptions errmap;
};

// ---------------------------------------------------------------------------
// The static graph mesh consumed by the Graph-FVM solver.
// Convention: internal faces first (owner < neighbour, sorted by owner then
// neighbour = "upper triangular order"), boundary faces after, grouped by patch.
// S_f points from owner to neighbour (outward from owner).
// ---------------------------------------------------------------------------
struct GraphMesh {
  Index n_cells = 0, n_faces = 0, n_internal_faces = 0, n_points = 0;

  // --- Topology (edge list E) ---
  AVec<Index> owner;       // |E|
  AVec<Index> neighbour;   // |E_internal|
  // Cell -> face CSR (gather side), sign = +1 owner / -1 neighbour.
  AVec<Index> cell_face_offset;   // n_cells+1
  AVec<Index> cell_faces;
  AVec<std::int8_t> cell_face_sign;

  // --- Node (cell) geometry ---
  Vec3SoA points;          // kept for post-processing / cut-cells
  Vec3SoA cell_centre;
  AVec<double> cell_volume;
  AVec<double> cell_closedness;   // |sum S_f| / sum |S_f|  (0 = watertight)

  // --- Edge (face) geometry ---
  Vec3SoA face_centre;     // x_f
  Vec3SoA face_area;       // S_f
  AVec<double> face_mag;   // |S_f|
  Vec3SoA d;               // d_ij = x_j - x_i (boundary: x_f - x_i)
  AVec<double> d_mag;
  AVec<double> weight;     // linear interpolation weight of owner, w_f in [0,1]
  Vec3SoA delta;           // over-relaxed orthogonal part  Δ_f
  AVec<double> delta_coeff;// |Δ_f| / |d_ij|
  Vec3SoA k;               // raw non-orthogonal correction  k_f = S_f - Δ_f

  // --- MeshErrorMap (CMM-style precomputed compensation) ---
  AVec<double> theta;      // non-orthogonality angle [rad]
  Vec3SoA m_skew;          // skewness offset vector  x_f - x_I
  AVec<double> skewness;   // |m_skew| / |d_ij|
  AVec<double> vol_ratio;  // max(V_i,V_j)/min(V_i,V_j)
  Vec3SoA k_comp;          // tanh-damped k_f
  AVec<double> beta;       // per-face HO/upwind blending in [0,1]
  AVec<double> omega;      // per-cell diagonal shield in (0,1]

  // --- Boundary ---
  std::vector<Patch> patches;

  // --- Renumbering maps (new -> old), identity if disabled ---
  AVec<Index> cell_new_to_old;
  AVec<Index> face_new_to_old;
};

// Reads constant/polyMesh/{points,faces,owner,neighbour,boundary} (ASCII).
// `dir` may be the case root or the polyMesh directory itself.
PolyMesh ReadOpenFoamPolyMesh(const std::string& dir);

// Renumbers, computes geometry, builds graph arrays and the MeshErrorMap.
GraphMesh BuildGraphMesh(PolyMesh mesh, const BuildOptions& opts = {});

// Human-readable quality summary.
std::string MeshReport(const GraphMesh& g);

}  // namespace difvm
```

---

### `polymesh_reader.cc`

```cpp
#include <algorithm>
#include <charconv>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <stdexcept>
#include <system_error>

#include "graph_mesh.h"

namespace difvm {
namespace {

std::string ReadFile(const std::string& path) {
  std::ifstream in(path, std::ios::binary);
  if (!in) throw std::runtime_error("DiFVM mesher: cannot open " + path);
  return std::string(std::istreambuf_iterator<char>(in), {});
}

// Minimal zero-copy tokenizer for OpenFOAM ASCII dictionaries/lists.
class Cursor {
 public:
  Cursor(const std::string& s, std::string name)
      : p_(s.data()), end_(s.data() + s.size()), name_(std::move(name)) {}

  void SkipWs() {
    for (;;) {
      while (p_ < end_ && std::isspace(static_cast<unsigned char>(*p_))) ++p_;
      if (p_ + 1 < end_ && p_[0] == '/' && p_[1] == '/') {
        while (p_ < end_ && *p_ != '\n') ++p_;
        continue;
      }
      if (p_ + 1 < end_ && p_[0] == '/' && p_[1] == '*') {
        p_ += 2;
        while (p_ + 1 < end_ && !(p_[0] == '*' && p_[1] == '/')) ++p_;
        p_ = std::min(p_ + 2, end_);
        continue;
      }
      return;
    }
  }
  char Peek() { SkipWs(); return p_ < end_ ? *p_ : '\0'; }
  void Expect(char c) {
    SkipWs();
    if (p_ >= end_ || *p_ != c) Fail(std::string("expected '") + c + "'");
    ++p_;
  }
  Index ReadInt() {
    SkipWs();
    Index v;
    auto r = std::from_chars(p_, end_, v);
    if (r.ec != std::errc{}) Fail("expected integer");
    p_ = r.ptr;
    return v;
  }
  double ReadDouble() {
    SkipWs();
    double v;
    auto r = std::from_chars(p_, end_, v);
    if (r.ec != std::errc{}) Fail("expected floating point number");
    p_ = r.ptr;
    return v;
  }
  std::string ReadWord() {
    SkipWs();
    const char* s = p_;
    while (p_ < end_ && !std::isspace(static_cast<unsigned char>(*p_)) &&
           !std::strchr("(){};", *p_))
      ++p_;
    if (s == p_) Fail("expected word");
    return std::string(s, p_);
  }
  // Skips a dictionary entry value including the terminating ';'.
  void SkipEntryValue() {
    int depth = 0;
    while (p_ < end_) {
      SkipWs();
      if (p_ >= end_) break;
      const char c = *p_;
      if (c == '(' || c == '{' || c == '[') ++depth;
      else if (c == ')' || c == '}' || c == ']') --depth;
      else if (c == ';' && depth == 0) { ++p_; return; }
      ++p_;
    }
    Fail("unterminated dictionary entry");
  }
  [[noreturn]] void Fail(const std::string& msg) const {
    throw std::runtime_error("DiFVM mesher: " + name_ + ": parse error: " + msg);
  }

 private:
  const char* p_;
  const char* end_;
  std::string name_;
};

void ParseHeader(Cursor& c) {
  if (c.ReadWord() != "FoamFile") c.Fail("missing FoamFile header");
  c.Expect('{');
  while (c.Peek() != '}') {
    const std::string key = c.ReadWord();
    if (key == "format") {
      const std::string fmt = c.ReadWord();
      c.Expect(';');
      if (fmt != "ascii")
        c.Fail("binary polyMesh not supported by this reader "
               "(run `foamFormatConvert -ascii` or export ascii)");
    } else {
      c.SkipEntryValue();
    }
  }
  c.Expect('}');
}

std::string Locate(const std::string& dir) {
  namespace fs = std::filesystem;
  if (fs::exists(fs::path(dir) / "points")) return dir;
  const auto sub = fs::path(dir) / "constant" / "polyMesh";
  if (fs::exists(sub / "points")) return sub.string();
  throw std::runtime_error("DiFVM mesher: no polyMesh found under " + dir);
}

}  // namespace

PolyMesh ReadOpenFoamPolyMesh(const std::string& dir_in) {
  const std::string dir = Locate(dir_in);
  PolyMesh m;

  {  // points
    const std::string txt = ReadFile(dir + "/points");
    Cursor c(txt, "points");
    ParseHeader(c);
    const Index n = c.ReadInt();
    c.Expect('(');
    m.xyz.resize(static_cast<std::size_t>(3 * n));
    for (Index i = 0; i < n; ++i) {
      c.Expect('(');
      m.xyz[3 * i + 0] = c.ReadDouble();
      m.xyz[3 * i + 1] = c.ReadDouble();
      m.xyz[3 * i + 2] = c.ReadDouble();
      c.Expect(')');
    }
    c.Expect(')');
  }
  {  // faces
    const std::string txt = ReadFile(dir + "/faces");
    Cursor c(txt, "faces");
    ParseHeader(c);
    const Index n = c.ReadInt();
    c.Expect('(');
    m.face_offset.resize(n + 1);
    m.face_points.reserve(static_cast<std::size_t>(4 * n));
    m.face_offset[0] = 0;
    for (Index f = 0; f < n; ++f) {
      const Index np = c.ReadInt();
      c.Expect('(');
      for (Index k = 0; k < np; ++k) m.face_points.push_back(c.ReadInt());
      c.Expect(')');
      m.face_offset[f + 1] = static_cast<Index>(m.face_points.size());
    }
    c.Expect(')');
  }
  auto read_labels = [&](const char* name, std::vector<Index>& out) {
    const std::string txt = ReadFile(dir + "/" + name);
    Cursor c(txt, name);
    ParseHeader(c);
    const Index n = c.ReadInt();
    c.Expect('(');
    out.resize(n);
    for (Index i = 0; i < n; ++i) out[i] = c.ReadInt();
    c.Expect(')');
  };
  read_labels("owner", m.owner);
  read_labels("neighbour", m.neighbour);

  {  // boundary
    const std::string txt = ReadFile(dir + "/boundary");
    Cursor c(txt, "boundary");
    ParseHeader(c);
    const Index n = c.ReadInt();
    c.Expect('(');
    m.patches.resize(n);
    for (Index i = 0; i < n; ++i) {
      Patch& p = m.patches[i];
      p.name = c.ReadWord();
      c.Expect('{');
      while (c.Peek() != '}') {
        const std::string key = c.ReadWord();
        if (key == "type")           { p.type = c.ReadWord(); c.Expect(';'); }
        else if (key == "nFaces")    { p.size = c.ReadInt();  c.Expect(';'); }
        else if (key == "startFace") { p.start = c.ReadInt(); c.Expect(';'); }
        else                         { c.SkipEntryValue(); }
      }
      c.Expect('}');
    }
    c.Expect(')');
  }

  // Validation
  const Index n_faces = static_cast<Index>(m.owner.size());
  const Index n_points = static_cast<Index>(m.xyz.size() / 3);
  if (static_cast<Index>(m.face_offset.size()) - 1 != n_faces)
    throw std::runtime_error("DiFVM mesher: faces/owner size mismatch");
  if (static_cast<Index>(m.neighbour.size()) > n_faces)
    throw std::runtime_error("DiFVM mesher: neighbour larger than owner");
  for (Index p : m.face_points)
    if (p < 0 || p >= n_points) throw std::runtime_error("DiFVM mesher: point index out of range");
  Index max_cell = -1;
  for (Index o : m.owner) max_cell = std::max(max_cell, o);
  for (Index nb : m.neighbour) max_cell = std::max(max_cell, nb);
  m.n_cells = max_cell + 1;
  Index covered = static_cast<Index>(m.neighbour.size());
  for (const Patch& p : m.patches) {
    if (p.start != covered) throw std::runtime_error("DiFVM mesher: patch '" + p.name + "' not contiguous");
    covered += p.size;
  }
  if (covered != n_faces) throw std::runtime_error("DiFVM mesher: boundary patches do not cover all boundary faces");
  return m;
}

}  // namespace difvm
```

---

### `graph_mesh_builder.cc`

```cpp
#include <algorithm>
#include <cmath>
#include <numeric>
#include <sstream>
#include <stdexcept>

#include "graph_mesh.h"

namespace difvm {
namespace {

constexpr double kPi = 3.14159265358979323846;
constexpr double kTiny = 1e-300;

struct V3 { double x, y, z; };
constexpr V3 operator+(V3 a, V3 b) { return {a.x + b.x, a.y + b.y, a.z + b.z}; }
constexpr V3 operator-(V3 a, V3 b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
constexpr V3 operator*(double s, V3 a) { return {s * a.x, s * a.y, s * a.z}; }
constexpr double Dot(V3 a, V3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
constexpr V3 Cross(V3 a, V3 b) {
  return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
}
inline double Mag(V3 a) { return std::sqrt(Dot(a, a)); }

inline V3 Get(const Vec3SoA& v, Index i) { return {v.x[i], v.y[i], v.z[i]}; }
inline void Set(Vec3SoA& v, Index i, V3 a) { v.x[i] = a.x; v.y[i] = a.y; v.z[i] = a.z; }

// Normalised smooth quality factor: 1 at q=0, 0.5 at q=qc, -> 0 beyond.
inline double Quality(double q, double qc, double w) {
  const double s = 0.5 * (1.0 - std::tanh((q - qc) / w));
  const double s0 = 0.5 * (1.0 - std::tanh(-qc / w));
  return std::min(1.0, s / s0);
}

// ---------------------------------------------------------------------------
// Reverse Cuthill-McKee on the cell adjacency graph. Returns new->old.
// ---------------------------------------------------------------------------
std::vector<Index> ReverseCuthillMcKee(Index n, const std::vector<Index>& off,
                                       const std::vector<Index>& adj) {
  std::vector<Index> degree(n);
  for (Index i = 0; i < n; ++i) degree[i] = off[i + 1] - off[i];
  std::vector<Index> stamp(n, -1);
  Index epoch = 0;
  std::vector<Index> nbrs;

  auto bfs = [&](Index s, std::vector<Index>& o, Index& last_level_begin) {
    ++epoch;
    o.clear();
    o.push_back(s);
    stamp[s] = epoch;
    std::size_t head = 0;
    last_level_begin = 0;
    while (head < o.size()) {
      last_level_begin = static_cast<Index>(head);
      const std::size_t level_end = o.size();
      for (; head < level_end; ++head) {
        const Index u = o[head];
        nbrs.clear();
        for (Index k = off[u]; k < off[u + 1]; ++k) {
          const Index v = adj[k];
          if (stamp[v] != epoch) { stamp[v] = epoch; nbrs.push_back(v); }
        }
        std::sort(nbrs.begin(), nbrs.end(),
                  [&](Index a, Index b) { return degree[a] < degree[b]; });
        o.insert(o.end(), nbrs.begin(), nbrs.end());
      }
    }
  };

  std::vector<Index> seeds(n);
  std::iota(seeds.begin(), seeds.end(), 0);
  std::stable_sort(seeds.begin(), seeds.end(),
                   [&](Index a, Index b) { return degree[a] < degree[b]; });

  std::vector<char> done(n, 0);
  std::vector<Index> out, comp;
  out.reserve(n);
  for (Index s0 : seeds) {  // one BFS tree per connected component
    if (done[s0]) continue;
    Index s = s0, llb = 0;
    bfs(s, comp, llb);
    for (int it = 0; it < 4; ++it) {  // pseudo-peripheral start node
      Index best = comp[llb];
      for (std::size_t k = llb; k < comp.size(); ++k)
        if (degree[comp[k]] < degree[best]) best = comp[k];
      if (best == s) break;
      s = best;
      bfs(s, comp, llb);
    }
    for (Index c : comp) { done[c] = 1; out.push_back(c); }
  }
  std::reverse(out.begin(), out.end());
  return out;
}

// ---------------------------------------------------------------------------
// Renumber cells (optional RCM) and bring faces into canonical order:
// internal faces with owner<neighbour sorted by (owner, neighbour);
// boundary faces untouched (patch ranges stay valid).
// ---------------------------------------------------------------------------
void CanonicaliseTopology(PolyMesh& m, bool rcm, AVec<Index>& cell_n2o,
                          AVec<Index>& face_n2o) {
  const Index nc = m.n_cells;
  const Index nf = static_cast<Index>(m.owner.size());
  const Index nif = static_cast<Index>(m.neighbour.size());

  std::vector<Index> cell_new_of_old(nc);
  cell_n2o.resize(nc);
  if (rcm) {
    std::vector<Index> off(nc + 1, 0);
    for (Index f = 0; f < nif; ++f) { ++off[m.owner[f] + 1]; ++off[m.neighbour[f] + 1]; }
    for (Index i = 0; i < nc; ++i) off[i + 1] += off[i];
    std::vector<Index> adj(off[nc]), fill(off.begin(), off.end() - 1);
    for (Index f = 0; f < nif; ++f) {
      adj[fill[m.owner[f]]++] = m.neighbour[f];
      adj[fill[m.neighbour[f]]++] = m.owner[f];
    }
    const std::vector<Index> n2o = ReverseCuthillMcKee(nc, off, adj);
    for (Index i = 0; i < nc; ++i) { cell_n2o[i] = n2o[i]; cell_new_of_old[n2o[i]] = i; }
  } else {
    std::iota(cell_n2o.begin(), cell_n2o.end(), 0);
    std::iota(cell_new_of_old.begin(), cell_new_of_old.end(), 0);
  }

  std::vector<Index> own(nf), nei(nif);
  std::vector<char> flip(nf, 0);
  for (Index f = 0; f < nif; ++f) {
    Index o = cell_new_of_old[m.owner[f]], n = cell_new_of_old[m.neighbour[f]];
    if (o > n) { std::swap(o, n); flip[f] = 1; }
    own[f] = o; nei[f] = n;
  }
  for (Index f = nif; f < nf; ++f) own[f] = cell_new_of_old[m.owner[f]];

  face_n2o.resize(nf);
  std::iota(face_n2o.begin(), face_n2o.end(), 0);
  std::sort(face_n2o.begin(), face_n2o.begin() + nif, [&](Index a, Index b) {
    return own[a] != own[b] ? own[a] < own[b] : nei[a] < nei[b];
  });

  std::vector<Index> new_off(nf + 1, 0), new_pts;
  new_pts.reserve(m.face_points.size());
  std::vector<Index> new_owner(nf), new_neigh(nif);
  for (Index f = 0; f < nf; ++f) {
    const Index old = face_n2o[f];
    const Index b = m.face_offset[old], e = m.face_offset[old + 1];
    if (flip[old]) for (Index k = e - 1; k >= b; --k) new_pts.push_back(m.face_points[k]);
    else           for (Index k = b; k < e; ++k)      new_pts.push_back(m.face_points[k]);
    new_off[f + 1] = static_cast<Index>(new_pts.size());
    new_owner[f] = own[old];
    if (f < nif) new_neigh[f] = nei[old];
  }
  m.face_offset.swap(new_off);
  m.face_points.swap(new_pts);
  m.owner.swap(new_owner);
  m.neighbour.swap(new_neigh);
}

// ---------------------------------------------------------------------------
// Face centroids / area vectors by triangle fan about the point mean
// (exact for planar polygons, robust for warped faces).
// ---------------------------------------------------------------------------
void ComputeFaceGeometry(const PolyMesh& m, GraphMesh& g) {
  const Index nf = g.n_faces;
  auto P = [&](Index p) -> V3 { return {m.xyz[3 * p], m.xyz[3 * p + 1], m.xyz[3 * p + 2]}; };

#pragma omp parallel for schedule(static)
  for (Index f = 0; f < nf; ++f) {
    const Index b = m.face_offset[f], n = m.face_offset[f + 1] - b;
    V3 ctr, area;
    if (n == 3) {
      const V3 p0 = P(m.face_points[b]), p1 = P(m.face_points[b + 1]), p2 = P(m.face_points[b + 2]);
      ctr = (1.0 / 3.0) * (p0 + p1 + p2);
      area = 0.5 * Cross(p1 - p0, p2 - p0);
    } else {
      V3 est{0, 0, 0};
      for (Index k = 0; k < n; ++k) est = est + P(m.face_points[b + k]);
      est = (1.0 / static_cast<double>(n)) * est;
      V3 sum_n{0, 0, 0}, sum_ac{0, 0, 0};
      double sum_a = 0;
      for (Index k = 0; k < n; ++k) {
        const V3 p0 = P(m.face_points[b + k]), p1 = P(m.face_points[b + (k + 1) % n]);
        const V3 nv = Cross(p1 - p0, est - p0);
        const double a = Mag(nv);
        sum_n = sum_n + nv;
        sum_a += a;
        sum_ac = sum_ac + a * ((1.0 / 3.0) * (p0 + p1 + est));
      }
      ctr = sum_a > kTiny ? (1.0 / sum_a) * sum_ac : est;
      area = 0.5 * sum_n;
    }
    Set(g.face_centre, f, ctr);
    Set(g.face_area, f, area);
    g.face_mag[f] = Mag(area);
  }
}

void BuildCellFaceCsr(GraphMesh& g) {
  const Index nc = g.n_cells, nf = g.n_faces, nif = g.n_internal_faces;
  g.cell_face_offset.assign(nc + 1, 0);
  for (Index f = 0; f < nf; ++f) ++g.cell_face_offset[g.owner[f] + 1];
  for (Index f = 0; f < nif; ++f) ++g.cell_face_offset[g.neighbour[f] + 1];
  for (Index i = 0; i < nc; ++i) g.cell_face_offset[i + 1] += g.cell_face_offset[i];
  g.cell_faces.resize(g.cell_face_offset[nc]);
  g.cell_face_sign.resize(g.cell_face_offset[nc]);
  std::vector<Index> fill(g.cell_face_offset.begin(), g.cell_face_offset.end() - 1);
  for (Index f = 0; f < nf; ++f) {
    const Index s = fill[g.owner[f]]++;
    g.cell_faces[s] = f; g.cell_face_sign[s] = +1;
  }
  for (Index f = 0; f < nif; ++f) {
    const Index s = fill[g.neighbour[f]]++;
    g.cell_faces[s] = f; g.cell_face_sign[s] = -1;
  }
}

// Cell centroids / volumes by pyramid decomposition about the face-centre mean.
void ComputeCellGeometry(GraphMesh& g) {
  const Index nc = g.n_cells;
#pragma omp parallel for schedule(static)
  for (Index i = 0; i < nc; ++i) {
    const Index b = g.cell_face_offset[i], e = g.cell_face_offset[i + 1];
    V3 est{0, 0, 0};
    for (Index k = b; k < e; ++k) est = est + Get(g.face_centre, g.cell_faces[k]);
    est = (1.0 / static_cast<double>(e - b)) * est;

    double vol = 0, sum_mag = 0;
    V3 ctr{0, 0, 0}, sum_s{0, 0, 0};
    for (Index k = b; k < e; ++k) {
      const Index f = g.cell_faces[k];
      const V3 s_out = static_cast<double>(g.cell_face_sign[k]) * Get(g.face_area, f);
      const V3 xf = Get(g.face_centre, f);
      const double pyr_vol = Dot(s_out, xf - est) / 3.0;
      vol += pyr_vol;
      ctr = ctr + pyr_vol * (0.75 * xf + 0.25 * est);
      sum_s = sum_s + s_out;
      sum_mag += g.face_mag[f];
    }
    g.cell_volume[i] = vol;
    Set(g.cell_centre, i, std::abs(vol) > kTiny ? (1.0 / vol) * ctr : est);
    g.cell_closedness[i] = sum_mag > kTiny ? Mag(sum_s) / sum_mag : 0.0;
  }
}

// ---------------------------------------------------------------------------
// Per-face graph attributes + MeshErrorMap.
// ---------------------------------------------------------------------------
void ComputeFaceGraphAttributes(GraphMesh& g, const ErrorMapOptions& o) {
  const Index nf = g.n_faces, nif = g.n_internal_faces;
  const double th_damp = o.theta_damp_deg * kPi / 180.0;
  const double th_damp_w = o.theta_damp_width_deg * kPi / 180.0;
  const double th_beta = o.theta_beta_deg * kPi / 180.0;
  const double th_beta_w = o.theta_beta_width_deg * kPi / 180.0;

#pragma omp parallel for schedule(static)
  for (Index f = 0; f < nf; ++f) {
    const bool internal = f < nif;
    const Index i = g.owner[f];
    const V3 xi = Get(g.cell_centre, i), xf = Get(g.face_centre, f);
    const V3 sf = Get(g.face_area, f);
    const double mag = std::max(g.face_mag[f], kTiny);
    const V3 shat = (1.0 / mag) * sf;

    V3 dvec;
    if (internal) dvec = Get(g.cell_centre, g.neighbour[f]) - xi;
    else          dvec = xf - xi;
    const double dm = std::max(Mag(dvec), kTiny);
    const V3 dhat = (1.0 / dm) * dvec;

    // Non-orthogonality
    const double cos_t = std::clamp(Dot(shat, dhat), -1.0, 1.0);
    const double theta = std::acos(cos_t);
    const double cos_safe = std::max(cos_t, 1e-6);   // guards theta -> 90°

    // Over-relaxed decomposition  Δ_f = |S_f| / (Ŝ_f·d̂) d̂,  k_f = S_f - Δ_f
    const V3 delta = (mag / cos_safe) * dhat;
    const V3 k = sf - delta;

    // tanh damping of k_f beyond theta_damp (C1-smooth via tanh^2)
    double lambda = 1.0;
    if (theta > th_damp) {
      const double t = std::tanh((theta - th_damp) / th_damp_w);
      lambda = 1.0 - (1.0 - o.lambda_min) * t * t;
    }

    // Skewness: intersection of line x_i->x_j with face plane
    V3 m_skew{0, 0, 0};
    double skew = 0.0, weight = 1.0, vr = 1.0, beta = 1.0;
    if (internal) {
      const Index j = g.neighbour[f];
      const V3 xj = Get(g.cell_centre, j);
      const double denom = Dot(dvec, shat);
      const double t = std::abs(denom) > kTiny ? Dot(xf - xi, shat) / denom : 0.5;
      const V3 x_int = xi + t * dvec;
      m_skew = xf - x_int;
      skew = Mag(m_skew) / dm;

      const double wden = Dot(sf, dvec);
      weight = std::abs(wden) > kTiny ? std::clamp(Dot(sf, xj - xf) / wden, 0.0, 1.0) : 0.5;

      const double vi = std::abs(g.cell_volume[i]), vj = std::abs(g.cell_volume[j]);
      vr = std::max(vi, vj) / std::max(std::min(vi, vj), kTiny);

      beta = Quality(theta, th_beta, th_beta_w) *
             Quality(skew, o.skew_beta, o.skew_beta_width) *
             Quality(vr - 1.0, o.vol_ratio_beta - 1.0, o.vol_ratio_width);
    }

    Set(g.d, f, dvec);
    g.d_mag[f] = dm;
    g.weight[f] = weight;
    Set(g.delta, f, delta);
    g.delta_coeff[f] = mag / (cos_safe * dm);
    Set(g.k, f, k);
    Set(g.k_comp, f, lambda * k);
    g.theta[f] = theta;
    Set(g.m_skew, f, m_skew);
    g.skewness[f] = skew;
    g.vol_ratio[f] = vr;
    g.beta[f] = beta;
  }
}

// Per-cell shield omega_i from the worst face attached to the cell.
void ComputeCellShield(GraphMesh& g, const ErrorMapOptions& o) {
  const Index nc = g.n_cells, nif = g.n_internal_faces;
  const double th_beta = o.theta_beta_deg * kPi / 180.0;
  const double th_beta_w = o.theta_beta_width_deg * kPi / 180.0;
#pragma omp parallel for schedule(static)
  for (Index i = 0; i < nc; ++i) {
    double q = 1.0;
    for (Index k = g.cell_face_offset[i]; k < g.cell_face_offset[i + 1]; ++k) {
      const Index f = g.cell_faces[k];
      const double qf = f < nif ? g.beta[f] : Quality(g.theta[f], th_beta, th_beta_w);
      q = std::min(q, qf);
    }
    // Non-closed (cut) cells get additionally damped.
    q *= Quality(g.cell_closedness[i], 1e-3, 5e-4);
    g.omega[i] = o.omega_min + (1.0 - o.omega_min) * q;
  }
}

}  // namespace

GraphMesh BuildGraphMesh(PolyMesh m, const BuildOptions& opts) {
  GraphMesh g;
  CanonicaliseTopology(m, opts.renumber_rcm, g.cell_new_to_old, g.face_new_to_old);

  g.n_cells = m.n_cells;
  g.n_faces = static_cast<Index>(m.owner.size());
  g.n_internal_faces = static_cast<Index>(m.neighbour.size());
  g.n_points = static_cast<Index>(m.xyz.size() / 3);
  g.patches = m.patches;

  g.owner.assign(m.owner.begin(), m.owner.end());
  g.neighbour.assign(m.neighbour.begin(), m.neighbour.end());

  g.points.Resize(g.n_points);
  for (Index p = 0; p < g.n_points; ++p) {
    g.points.x[p] = m.xyz[3 * p]; g.points.y[p] = m.xyz[3 * p + 1]; g.points.z[p] = m.xyz[3 * p + 2];
  }

  const std::size_t nf = g.n_faces, nc = g.n_cells;
  g.face_centre.Resize(nf); g.face_area.Resize(nf); g.face_mag.resize(nf);
  g.d.Resize(nf); g.d_mag.resize(nf); g.weight.resize(nf);
  g.delta.Resize(nf); g.delta_coeff.resize(nf); g.k.Resize(nf); g.k_comp.Resize(nf);
  g.theta.resize(nf); g.m_skew.Resize(nf); g.skewness.resize(nf);
  g.vol_ratio.resize(nf); g.beta.resize(nf);
  g.cell_centre.Resize(nc); g.cell_volume.resize(nc); g.cell_closedness.resize(nc);
  g.omega.resize(nc);

  ComputeFaceGeometry(m, g);
  BuildCellFaceCsr(g);
  ComputeCellGeometry(g);
  ComputeFaceGraphAttributes(g, opts.errmap);
  ComputeCellShield(g, opts.errmap);

  for (Index i = 0; i < g.n_cells; ++i)
    if (g.cell_volume[i] <= 0.0)
      throw std::runtime_error("DiFVM mesher: non-positive cell volume at cell " + std::to_string(i));
  return g;
}

std::string MeshReport(const GraphMesh& g) {
  const Index nif = g.n_internal_faces;
  double max_th = 0, sum_th = 0, max_sk = 0, max_vr = 1, sum_beta = 0, min_omega = 1,
         vol = 0, max_closed = 0, min_v = 1e300, max_v = 0;
  Index over65 = 0, over70 = 0, beta_lt_half = 0;
  for (Index f = 0; f < nif; ++f) {
    const double deg = g.theta[f] * 180.0 / kPi;
    max_th = std::max(max_th, deg); sum_th += deg;
    over65 += deg > 65.0; over70 += deg > 70.0;
    max_sk = std::max(max_sk, g.skewness[f]);
    max_vr = std::max(max_vr, g.vol_ratio[f]);
    sum_beta += g.beta[f]; beta_lt_half += g.beta[f] < 0.5;
  }
  for (Index i = 0; i < g.n_cells; ++i) {
    vol += g.cell_volume[i];
    min_v = std::min(min_v, g.cell_volume[i]); max_v = std::max(max_v, g.cell_volume[i]);
    max_closed = std::max(max_closed, g.cell_closedness[i]);
    min_omega = std::min(min_omega, g.omega[i]);
  }
  std::ostringstream s;
  s << "DiFVM GraphMesh report\n"
    << "  cells            : " << g.n_cells << "\n"
    << "  faces            : " << g.n_faces << " (internal " << nif << ")\n"
    << "  points           : " << g.n_points << "\n"
    << "  patches          : " << g.patches.size() << "\n";
  for (const Patch& p : g.patches)
    s << "    - " << p.name << " [" << p.type << "] start=" << p.start << " n=" << p.size << "\n";
  s << "  total volume     : " << vol << "  (min " << min_v << ", max " << max_v << ")\n"
    << "  max closedness   : " << max_closed << "\n"
    << "  non-orthogonality: max " << max_th << " deg, mean " << (nif ? sum_th / nif : 0)
    << " deg, >65: " << over65 << ", >70: " << over70 << "\n"
    << "  max skewness     : " << max_sk << "\n"
    << "  max volume ratio : " << max_vr << "\n"
    << "  mean beta_f      : " << (nif ? sum_beta / nif : 1) << "  (faces with beta<0.5: "
    << beta_lt_half << ")\n"
    << "  min omega_i      : " << min_omega << "\n";
  return s.str();
}

}  // namespace difvm
```

---

### `graph_mesh_c_api.h`

```c
/* C ABI for Rust (cdylib FFI) and Fortran (iso_c_binding). Zero-copy views. */
#pragma once
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct DiFVMMesh DiFVMMesh;

typedef struct { const double* x; const double* y; const double* z; } DiFVMVec3View;

typedef struct {
  int64_t n_cells, n_faces, n_internal_faces, n_points, n_patches;
  const int64_t* owner;
  const int64_t* neighbour;
  const int64_t* cell_face_offset;
  const int64_t* cell_faces;
  const int8_t*  cell_face_sign;
  DiFVMVec3View  points, cell_centre;
  const double*  cell_volume;
  const double*  cell_closedness;
  DiFVMVec3View  face_centre, face_area;
  const double*  face_mag;
  DiFVMVec3View  d;
  const double*  d_mag;
  const double*  weight;
  DiFVMVec3View  delta;
  const double*  delta_coeff;
  DiFVMVec3View  k, k_comp;
  const double*  theta;
  DiFVMVec3View  m_skew;
  const double*  skewness;
  const double*  vol_ratio;
  const double*  beta;
  const double*  omega;
  const int64_t* cell_new_to_old;
  const int64_t* face_new_to_old;
} DiFVMMeshView;

/* Returns 0 on success, nonzero on error (message copied into err). */
int  difvm_mesh_from_polymesh(const char* dir, int renumber_rcm,
                              DiFVMMesh** out, char* err, size_t err_len);
int  difvm_mesh_view(const DiFVMMesh* mesh, DiFVMMeshView* view);
int  difvm_mesh_patch(const DiFVMMesh* mesh, int64_t i, const char** name,
                      const char** type, int64_t* start, int64_t* size);
void difvm_mesh_free(DiFVMMesh* mesh);

#ifdef __cplusplus
}
#endif
```

### `graph_mesh_c_api.cc`

```cpp
#include "graph_mesh_c_api.h"

#include <cstring>
#include <exception>

#include "graph_mesh.h"

struct DiFVMMesh { difvm::GraphMesh g; };

namespace {
DiFVMVec3View View(const difvm::Vec3SoA& v) { return {v.x.data(), v.y.data(), v.z.data()}; }
}  // namespace

extern "C" {

int difvm_mesh_from_polymesh(const char* dir, int renumber_rcm, DiFVMMesh** out,
                             char* err, size_t err_len) {
  try {
    difvm::BuildOptions opts;
    opts.renumber_rcm = renumber_rcm != 0;
    auto* m = new DiFVMMesh{difvm::BuildGraphMesh(difvm::ReadOpenFoamPolyMesh(dir), opts)};
    *out = m;
    return 0;
  } catch (const std::exception& e) {
    if (err && err_len) { std::strncpy(err, e.what(), err_len - 1); err[err_len - 1] = 0; }
    return 1;
  }
}

int difvm_mesh_view(const DiFVMMesh* m, DiFVMMeshView* v) {
  if (!m || !v) return 1;
  const difvm::GraphMesh& g = m->g;
  *v = DiFVMMeshView{};
  v->n_cells = g.n_cells; v->n_faces = g.n_faces; v->n_internal_faces = g.n_internal_faces;
  v->n_points = g.n_points; v->n_patches = static_cast<int64_t>(g.patches.size());
  v->owner = g.owner.data(); v->neighbour = g.neighbour.data();
  v->cell_face_offset = g.cell_face_offset.data(); v->cell_faces = g.cell_faces.data();
  v->cell_face_sign = g.cell_face_sign.data();
  v->points = View(g.points); v->cell_centre = View(g.cell_centre);
  v->cell_volume = g.cell_volume.data(); v->cell_closedness = g.cell_closedness.data();
  v->face_centre = View(g.face_centre); v->face_area = View(g.face_area);
  v->face_mag = g.face_mag.data();
  v->d = View(g.d); v->d_mag = g.d_mag.data(); v->weight = g.weight.data();
  v->delta = View(g.delta); v->delta_coeff = g.delta_coeff.data();
  v->k = View(g.k); v->k_comp = View(g.k_comp);
  v->theta = g.theta.data(); v->m_skew = View(g.m_skew); v->skewness = g.skewness.data();
  v->vol_ratio = g.vol_ratio.data(); v->beta = g.beta.data(); v->omega = g.omega.data();
  v->cell_new_to_old = g.cell_new_to_old.data(); v->face_new_to_old = g.face_new_to_old.data();
  return 0;
}

int difvm_mesh_patch(const DiFVMMesh* m, int64_t i, const char** name, const char** type,
                     int64_t* start, int64_t* size) {
  if (!m || i < 0 || i >= static_cast<int64_t>(m->g.patches.size())) return 1;
  const difvm::Patch& p = m->g.patches[i];
  *name = p.name.c_str(); *type = p.type.c_str(); *start = p.start; *size = p.size;
  return 0;
}

void difvm_mesh_free(DiFVMMesh* m) { delete m; }

}  // extern "C"
```

---

### `mesh_check_main.cc`

```cpp
#include <chrono>
#include <iostream>

#include "graph_mesh.h"

int main(int argc, char** argv) {
  if (argc < 2) {
    std::cerr << "usage: mesh_check <case_dir|polyMesh_dir> [--no-rcm]\n";
    return 2;
  }
  try {
    difvm::BuildOptions opts;
    if (argc > 2 && std::string(argv[2]) == "--no-rcm") opts.renumber_rcm = false;
    const auto t0 = std::chrono::steady_clock::now();
    difvm::PolyMesh pm = difvm::ReadOpenFoamPolyMesh(argv[1]);
    const auto t1 = std::chrono::steady_clock::now();
    difvm::GraphMesh g = difvm::BuildGraphMesh(std::move(pm), opts);
    const auto t2 = std::chrono::steady_clock::now();
    std::cout << difvm::MeshReport(g)
              << "  read  time       : " << std::chrono::duration<double>(t1 - t0).count() << " s\n"
              << "  build time       : " << std::chrono::duration<double>(t2 - t1).count() << " s\n";
  } catch (const std::exception& e) {
    std::cerr << e.what() << "\n";
    return 1;
  }
  return 0;
}
```

### `CMakeLists.txt`

```cmake
cmake_minimum_required(VERSION 3.20)
project(difvm_mesher CXX)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
find_package(OpenMP)

add_library(difvm_mesh SHARED
  polymesh_reader.cc graph_mesh_builder.cc graph_mesh_c_api.cc)
target_include_directories(difvm_mesh PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_options(difvm_mesh PRIVATE
  $<$<CXX_COMPILER_ID:GNU,Clang>:-O3 -march=native -Wall -Wextra>
  $<$<CXX_COMPILER_ID:MSVC>:/O2 /W4>)
if(OpenMP_CXX_FOUND)
  target_link_libraries(difvm_mesh PUBLIC OpenMP::OpenMP_CXX)
endif()

add_executable(mesh_check mesh_check_main.cc)
target_link_libraries(mesh_check PRIVATE difvm_mesh)
```

---

## Consuming the arrays from the other layers

**Fortran (`iso_c_binding`)** — the face arrays are already SIMD-ready:

```fortran
interface
  function difvm_mesh_view(mesh, view) bind(C, name="difvm_mesh_view") result(ierr)
    import :: c_ptr, c_int
    type(c_ptr), value :: mesh
    type(c_ptr), value :: view        ! pointer to a bind(C) DiFVMMeshView mirror
    integer(c_int) :: ierr
  end function
end interface
! Afterwards: call c_f_pointer(view%beta, beta, [view%n_faces]) etc.
```

**Rust** (`extern "C"` block mirroring `DiFVMMeshView`, then `std::slice::from_raw_parts(v.owner, v.n_faces as usize)` inside one small `unsafe` block).

## What the solver gets for free

| Array | Used by |
|---|---|
| `owner/neighbour`, upper-triangular order | gather–compute–scatter edge loop; direct CSR/LDU matrix assembly without sorting |
| `cell_face_offset/cell_faces/sign` | gather-style aggregation (no scatter-add atomics on CPU) |
| `weight`, `delta_coeff`, `k_comp` | linear interpolation, orthogonal diffusion coefficient, damped over-relaxed non-orthogonal correction |
| `m_skew`, `beta` | skew-corrected face values, branch-free deferred-correction blending `ṁ_f φ_f^U + β_f ṁ_f(φ^HO − φ^U)` |
| `omega` | diagonal shield `a_P ← a_P/ω_i` and source damping in cut/sliver cells |
| `cell_closedness` | hook for the later Octree cut-cell stage (non-watertight cells are flagged and damped automatically) |

Everything is computed once, held in RAM, and never touched again during PSO/SIMP/adjoint loops — no runtime quality checks, no branches.

**Notes / limits of this slice:** ASCII `polyMesh` only (binary and `.gz` deliberately left to the Rust `memmap2` I/O layer); Octree cut-cell generation itself is the next vertical slice — this mesher is its consumer-side contract. Requires GCC ≥ 11 / Clang ≥ 14 (libc++ 17) / MSVC 2019+ for `std::from_chars<double>`.