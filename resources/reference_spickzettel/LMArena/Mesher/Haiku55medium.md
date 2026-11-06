# DiFVM Mesher in C++20

I implemented only the mesher from the DiFVM part of the spec. It covers mesh ingestion, geometry, the static graph, cache-friendly layout, and the `MeshErrorMap`. The solver, optimization, and Rust/Fortran layers are out of scope.

## Language choice: C++20

The mesher is a memory-bound job: text parsing, geometry on a fixed topology, and building a static graph. It is not a dense flop kernel. For that job, C++20 is the best fit:

- **Layout control:** 64-byte-aligned SoA arrays and CSR gather graphs without hidden allocations. The spec assigns this topology layer to C++.
- **Race-free parallelism:** every per-cell quantity is computed as a gather over a CSR cell→face list, so OpenMP needs no atomics.
- **Parsing:** `std::from_chars` on a single buffer is fast and simple.

Fortran would be fine for the arithmetic but is awkward for nested OpenFOAM lists and owning graph structures. Rust is equally fast and safer; if you want, the parser is the natural piece to move there later (e.g., `memmap2`), calling this core through a C ABI.

## What it does

1. **Reads** OpenFOAM ASCII `constant/polyMesh/` (points, faces, owner, neighbour, boundary) without modification. This is the drop-in path from DiFVM §2.6.4.
2. **Generates** a Cartesian hex block with optional interior jitter for tests and benchmarks.
3. **Computes geometry** with triangle-fan face areas and face centroids, and pyramid-decomposed cell volumes and centroids.
4. **Builds the static graph:** owner/neighbour, a cell→face CSR gather list, and per-internal-face `d_f`, linear weights `w_f`, over-relaxed `Δ_f`, and `k_f = S_f − Δ_f`.
5. **Renumbers cells along a Morton curve** and sorts faces by owner, per patch range, for cache locality.
6. **Exports a `MeshErrorMap`:** non-orthogonality θ_f, skew vector m_f^skew, expansion ratio, planarity, blend factor β_f, damped correction k_f^comp, per-cell ω_i, flag bits, and closedness.
7. **Self-tests:** volume conservation, closedness, exact Gauss gradient through the graph, and a single-cell OpenFOAM fixture that exercises the reader.

The spec gives the shape of β_f, k_f^comp, and ω_i but not their numbers. The defaults in `MeshQualityConfig` are my choices and are fully configurable.

## Layout

```
difvm_mesher/
├── CMakeLists.txt
├── include/difvm/mesh/mesh.hpp
└── src/
    ├── polymesh_io.cpp   # OpenFOAM reader + Cartesian generator
    ├── mesh_build.cpp    # geometry, CSR, Morton reorder, error map
    └── main.cpp          # CLI + self-tests
```

---

### `CMakeLists.txt`

```cmake
cmake_minimum_required(VERSION 3.20)
project(difvm_mesher LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(OpenMP)

add_library(difvm_mesh
  src/polymesh_io.cpp
  src/mesh_build.cpp)
target_include_directories(difvm_mesh PUBLIC include)

if(MSVC)
  target_compile_options(difvm_mesh PRIVATE /O2 /fp:precise)
else()
  target_compile_options(difvm_mesh PRIVATE -O3 -ffp-contract=off -fno-fast-math -Wall -Wextra)
endif()

if(OpenMP_CXX_FOUND)
  target_link_libraries(difvm_mesh PUBLIC OpenMP::OpenMP_CXX)
endif()

add_executable(difvm_mesher src/main.cpp)
target_link_libraries(difvm_mesher PRIVATE difvm_mesh)
```

---

### `include/difvm/mesh/mesh.hpp`

```cpp
#pragma once
// DiFVM mesher: public data model and API.
// Design: SoA, 64-byte aligned arrays, static CSR gather graph, no heap
// allocation inside per-face or per-cell loops.

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <limits>
#include <new>
#include <string>
#include <vector>

namespace difvm::mesh {

using Index  = std::int32_t;  // cell / face / point label
using Offset = std::int64_t;  // CSR offsets (safe beyond 2^31 entries)
using Real   = double;        // FP64 tier-1 (always available)

// ---- 64-byte aligned storage (cache line / AVX-512) ------------------------
template <typename T, std::size_t Alignment = 64>
struct AlignedAllocator {
  using value_type = T;
  AlignedAllocator() noexcept = default;
  template <typename U>
  explicit AlignedAllocator(const AlignedAllocator<U, Alignment>&) noexcept {}

  T* allocate(std::size_t n) {
    return static_cast<T*>(::operator new(n * sizeof(T), std::align_val_t{Alignment}));
  }
  void deallocate(T* p, std::size_t) noexcept {
    ::operator delete(p, std::align_val_t{Alignment});
  }
  template <typename U>
  bool operator==(const AlignedAllocator<U, Alignment>&) const noexcept { return true; }
  template <typename U>
  bool operator!=(const AlignedAllocator<U, Alignment>&) const noexcept { return false; }
};

template <typename T>
using AlignedVec = std::vector<T, AlignedAllocator<T>>;

// ---- Small vector helpers (value type only, never stored in hot arrays) ----
struct Vec3 {
  Real x{0}, y{0}, z{0};
};
inline Vec3 operator+(Vec3 a, Vec3 b) { return {a.x + b.x, a.y + b.y, a.z + b.z}; }
inline Vec3 operator-(Vec3 a, Vec3 b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
inline Vec3 operator*(Real s, Vec3 a) { return {s * a.x, s * a.y, s * a.z}; }
inline Vec3 operator*(Vec3 a, Real s) { return {s * a.x, s * a.y, s * a.z}; }
inline Vec3 operator/(Vec3 a, Real s) { return {a.x / s, a.y / s, a.z / s}; }
inline Real dot(Vec3 a, Vec3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
inline Vec3 cross(Vec3 a, Vec3 b) {
  return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
}
inline Real norm(Vec3 a) { return std::sqrt(dot(a, a)); }

// Structure-of-arrays vector field: x[], y[], z[] each 64-byte aligned.
struct Vec3Field {
  AlignedVec<Real> x, y, z;
  void resize(std::size_t n) { x.resize(n); y.resize(n); z.resize(n); }
  std::size_t size() const { return x.size(); }
  Vec3 get(std::size_t i) const { return {x[i], y[i], z[i]}; }
  void set(std::size_t i, Vec3 v) { x[i] = v.x; y[i] = v.y; z[i] = v.z; }
  void flip(std::size_t i) { x[i] = -x[i]; y[i] = -y[i]; z[i] = -z[i]; }
};

struct Patch {
  std::string name;
  std::string type = "patch";
  Index start_face = 0;   // absolute face index (OpenFOAM convention)
  Index n_faces = 0;
};

// Direct image of constant/polyMesh (before geometry).
struct RawPolyMesh {
  Vec3Field points;
  AlignedVec<Offset> face_ptr;   // n_faces + 1, CSR into face_pts
  AlignedVec<Index> face_pts;
  AlignedVec<Index> owner;       // n_faces
  AlignedVec<Index> neighbour;   // n_internal
  std::vector<Patch> patches;
};

// Smooth, tunable blend / damping parameters. Shapes come from the DiFVM spec;
// the numbers are defaults to be calibrated.
struct MeshQualityConfig {
  // beta_f = sigmoid((theta0 - theta)/w_theta) * sigmoid((skew0 - skew)/w_skew)
  Real theta_blend0_deg = 70.0;
  Real theta_blend_width_deg = 4.0;
  Real skew_blend0 = 0.5;
  Real skew_blend_width = 0.1;
  // k_comp = s_k * k_f, s_k -> k_damp_min above theta_crit (spec: 65 deg)
  Real theta_crit_deg = 65.0;
  Real theta_damp_width_deg = 5.0;
  Real k_damp_min = 0.25;
  // omega_i = max(omega_min, min_f beta_f)
  Real omega_min = 0.1;
  // flags
  Real nonorth_flag_deg = 70.0;
  Real skew_flag = 0.5;
  Real expansion_flag = 5.0;
};

enum CellFlag : std::uint8_t {
  kCellNonOrth        = 1 << 0,
  kCellSkew           = 1 << 1,
  kCellExpansion      = 1 << 2,
  kCellDegenerate     = 1 << 3,
  kCellNegativeVolume = 1 << 4,
  kCellNotClosed      = 1 << 5,
};

struct QualityStats {
  Real max_nonorth_deg = 0.0, mean_nonorth_deg = 0.0;
  Real max_skew = 0.0, max_expansion = 1.0;
  Real min_volume = 0.0, max_planarity = 0.0, max_closedness = 0.0;
  Index n_flagged_cells = 0, n_degenerate_faces = 0, n_negative_volume = 0;
};

// Geometric error map: exported once, consumed by the solver at 0 ms runtime cost.
struct MeshErrorMap {
  // internal faces (size n_internal)
  AlignedVec<Real> theta_deg;        // non-orthogonality angle (0 = orthogonal)
  AlignedVec<Real> skew;             // |m_skew| / |d|
  AlignedVec<Real> expansion;        // max(V)/min(V)
  AlignedVec<Real> beta;             // upwind blend factor in [0,1]
  AlignedVec<std::uint8_t> degenerate;
  Vec3Field m_skew;                  // x_f - intersection(P->N, face plane)
  Vec3Field k_comp;                  // damped non-orthogonal correction
  // all faces (size n_faces)
  AlignedVec<Real> planarity;        // max out-of-plane distance / sqrt(area)
  // cells (size n_cells)
  AlignedVec<Real> omega;            // (0, 1]
  AlignedVec<Real> closedness;       // |sum_f n_f S_f| / sum_f |S_f|
  AlignedVec<std::uint8_t> flags;    // CellFlag bits
  QualityStats stats;
};

struct Mesh {
  Index n_cells = 0, n_faces = 0, n_internal = 0;

  // cells
  Vec3Field cell_centre;
  AlignedVec<Real> cell_volume;

  // faces: internal first (sorted by owner, neighbour), then patches (sorted by owner)
  AlignedVec<Index> owner;       // n_faces
  AlignedVec<Index> neighbour;   // n_internal
  Vec3Field Sf;                  // n_faces, outward from owner
  AlignedVec<Real> magSf;
  Vec3Field face_centre;

  // internal-face graph edges
  Vec3Field d;                   // x_N - x_P
  AlignedVec<Real> inv_magd;
  AlignedVec<Real> w_owner;      // linear interpolation weight of owner
  Vec3Field delta;               // orthogonal part Delta_f, parallel to d
  Vec3Field k;                   // non-orthogonal correction k_f = S_f - Delta_f

  // cell -> face CSR: race-free gather replacement for scatter-add
  AlignedVec<Offset> cell_face_ptr;        // n_cells + 1
  AlignedVec<Index> cell_face;             // face ids
  AlignedVec<std::int8_t> cell_face_sign;  // +1 owner, -1 neighbour

  std::vector<Patch> patches;
  MeshErrorMap errors;
};

struct BuildOptions {
  bool morton_reorder = true;   // cache-locality renumbering of cells
  bool sort_faces = true;       // owner-sorted faces (per patch range)
  MeshQualityConfig quality{};
};

// ---- API --------------------------------------------------------------------
RawPolyMesh read_openfoam_polymesh(const std::filesystem::path& poly_mesh_dir);

RawPolyMesh make_cartesian_hex(Index nx, Index ny, Index nz,
                               Real lx, Real ly, Real lz,
                               Real jitter = 0.0, std::uint32_t seed = 1);

Mesh build_mesh(const RawPolyMesh& raw, const BuildOptions& opt = {});

}  // namespace difvm::mesh
```

---

### `src/polymesh_io.cpp`

```cpp
// OpenFOAM ASCII polyMesh reader and analytic Cartesian hex generator.
#include <algorithm>
#include <array>
#include <cctype>
#include <charconv>
#include <cstddef>
#include <fstream>
#include <random>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "difvm/mesh/mesh.hpp"

namespace difvm::mesh {
namespace {

bool is_space(char c) { return std::isspace(static_cast<unsigned char>(c)) != 0; }

bool is_delim(char c) {
  return is_space(c) || c == '(' || c == ')' || c == '{' || c == '}' || c == ';' || c == '"';
}

std::string_view trim(std::string_view s) {
  while (!s.empty() && is_space(s.front())) s.remove_prefix(1);
  while (!s.empty() && is_space(s.back())) s.remove_suffix(1);
  return s;
}

// Minimal zero-allocation tokenizer for OpenFOAM ASCII lists and dictionaries.
class Lexer {
 public:
  Lexer(std::string_view text, std::string origin)
      : s_(text), origin_(std::move(origin)) {}

  void skip() {
    while (pos_ < s_.size()) {
      const char c = s_[pos_];
      if (is_space(c)) {
        ++pos_;
      } else if (c == '/' && pos_ + 1 < s_.size() && s_[pos_ + 1] == '/') {
        while (pos_ < s_.size() && s_[pos_] != '\n') ++pos_;
      } else if (c == '/' && pos_ + 1 < s_.size() && s_[pos_ + 1] == '*') {
        const auto e = s_.find("*/", pos_ + 2);
        pos_ = (e == std::string_view::npos) ? s_.size() : e + 2;
      } else {
        break;
      }
    }
  }

  char peek() {
    skip();
    return pos_ < s_.size() ? s_[pos_] : '\0';
  }

  void expect(char c) {
    if (peek() != c) fail(std::string("expected '") + c + "'");
    ++pos_;
  }

  std::string_view word() {
    skip();
    const std::size_t b = pos_;
    while (pos_ < s_.size() && !is_delim(s_[pos_])) ++pos_;
    return s_.substr(b, pos_ - b);
  }

  std::string_view peek_word() {
    const std::size_t save = pos_;
    const auto w = word();
    pos_ = save;
    return w;
  }

  // Text up to (and consuming) the terminator.
  std::string_view until(char term) {
    skip();
    const auto e = s_.find(term, pos_);
    if (e == std::string_view::npos) fail(std::string("missing '") + term + "'");
    const auto v = trim(s_.substr(pos_, e - pos_));
    pos_ = e + 1;
    return v;
  }

  // Skips a { ... } block, honouring nesting and quoted strings.
  void skip_balanced_block() {
    expect('{');
    int depth = 1;
    while (depth > 0) {
      if (pos_ >= s_.size()) fail("unterminated '{' block");
      const char c = s_[pos_++];
      if (c == '"') {
        const auto e = s_.find('"', pos_);
        if (e == std::string_view::npos) fail("unterminated string");
        pos_ = e + 1;
      } else if (c == '{') {
        ++depth;
      } else if (c == '}') {
        --depth;
      }
    }
  }

  void skip_header() {
    if (peek_word() == "FoamFile") {
      word();
      skip_balanced_block();
    }
  }

  template <typename T>
  T number() {
    skip();
    const char* b = s_.data() + pos_;
    const char* e = s_.data() + s_.size();
    T v{};
    const auto [ptr, ec] = std::from_chars(b, e, v);
    if (ec != std::errc()) fail("invalid number");
    pos_ += static_cast<std::size_t>(ptr - b);
    return v;
  }

  [[noreturn]] void fail(const std::string& msg) const {
    const std::size_t end = std::min(pos_, s_.size());
    const auto line = 1 + static_cast<std::size_t>(
        std::count(s_.begin(), s_.begin() + static_cast<std::ptrdiff_t>(end), '\n'));
    throw std::runtime_error(origin_ + ":" + std::to_string(line) + ": " + msg);
  }

 private:
  std::string_view s_;
  std::string origin_;
  std::size_t pos_ = 0;
};

std::string read_file(const std::filesystem::path& p) {
  std::ifstream in(p, std::ios::binary | std::ios::ate);
  if (!in) throw std::runtime_error("cannot open " + p.string());
  const auto size = static_cast<std::streamsize>(in.tellg());
  std::string buf(static_cast<std::size_t>(size), '\0');
  in.seekg(0);
  in.read(buf.data(), size);
  return buf;
}

void require_ascii(const std::string& text, const std::string& origin) {
  const auto pos = text.find("format");
  if (pos == std::string::npos) return;
  const auto v = trim(std::string_view(text).substr(pos + 6, 16));
  if (v.substr(0, 6) == "binary") {
    throw std::runtime_error(origin + ": binary polyMesh not supported yet; "
                             "convert with `foamFormatConvert -ascii`");
  }
}

void read_points(Lexer& lx, Vec3Field& pts) {
  const auto n = lx.number<Index>();
  lx.expect('(');
  pts.resize(static_cast<std::size_t>(n));
  for (Index i = 0; i < n; ++i) {
    lx.expect('(');
    const Vec3 p{lx.number<Real>(), lx.number<Real>(), lx.number<Real>()};  // braced: left-to-right
    lx.expect(')');
    pts.set(static_cast<std::size_t>(i), p);
  }
  lx.expect(')');
}

void read_faces(Lexer& lx, RawPolyMesh& r) {
  const auto n = lx.number<Index>();
  lx.expect('(');
  r.face_ptr.assign(1, Offset{0});
  r.face_ptr.reserve(static_cast<std::size_t>(n) + 1);
  for (Index f = 0; f < n; ++f) {
    const auto k = lx.number<Index>();
    lx.expect('(');
    for (Index j = 0; j < k; ++j) r.face_pts.push_back(lx.number<Index>());
    lx.expect(')');
    r.face_ptr.push_back(static_cast<Offset>(r.face_pts.size()));
  }
  lx.expect(')');
}

void read_label_list(Lexer& lx, AlignedVec<Index>& out) {
  const auto n = lx.number<Index>();
  out.resize(static_cast<std::size_t>(n));
  if (lx.peek() == '{') {  // uniform-list form N{value}
    lx.expect('{');
    const auto v = lx.number<Index>();
    lx.expect('}');
    std::fill(out.begin(), out.end(), v);
    return;
  }
  lx.expect('(');
  for (auto& v : out) v = lx.number<Index>();
  lx.expect(')');
}

Index to_index(Lexer& lx, std::string_view v) {
  Index out{};
  const auto [ptr, ec] = std::from_chars(v.data(), v.data() + v.size(), out);
  if (ec != std::errc() || ptr != v.data() + v.size()) {
    lx.fail("expected integer, got '" + std::string(v) + "'");
  }
  return out;
}

void read_boundary(Lexer& lx, std::vector<Patch>& out) {
  const auto n = lx.number<Index>();
  lx.expect('(');
  for (Index p = 0; p < n; ++p) {
    Patch pt;
    pt.name = std::string(lx.word());
    lx.expect('{');
    while (lx.peek() != '}') {
      const auto key = lx.word();
      const auto val = lx.until(';');
      if (key == "type") {
        pt.type = std::string(val);
      } else if (key == "nFaces") {
        pt.n_faces = to_index(lx, val);
      } else if (key == "startFace") {
        pt.start_face = to_index(lx, val);
      }
    }
    lx.expect('}');
    out.push_back(std::move(pt));
  }
  lx.expect(')');
}

}  // namespace

RawPolyMesh read_openfoam_polymesh(const std::filesystem::path& dir) {
  RawPolyMesh r;
  auto parse_file = [&](const char* name, auto&& body) {
    const auto path = dir / name;
    const std::string text = read_file(path);
    require_ascii(text, path.string());
    Lexer lx(text, path.string());
    lx.skip_header();
    body(lx);
  };
  parse_file("points", [&](Lexer& lx) { read_points(lx, r.points); });
  parse_file("faces", [&](Lexer& lx) { read_faces(lx, r); });
  parse_file("owner", [&](Lexer& lx) { read_label_list(lx, r.owner); });
  parse_file("neighbour", [&](Lexer& lx) { read_label_list(lx, r.neighbour); });
  parse_file("boundary", [&](Lexer& lx) { read_boundary(lx, r.patches); });
  return r;
}

RawPolyMesh make_cartesian_hex(Index nx, Index ny, Index nz,
                               Real lx, Real ly, Real lz,
                               Real jitter, std::uint32_t seed) {
  if (nx < 1 || ny < 1 || nz < 1 || lx <= 0 || ly <= 0 || lz <= 0) {
    throw std::invalid_argument("make_cartesian_hex: invalid dimensions");
  }
  const std::array<Index, 3> n{nx, ny, nz};
  const std::array<Real, 3> h{lx / nx, ly / ny, lz / nz};

  auto node_id = [&](Index i, Index j, Index k) { return i + (nx + 1) * (j + (ny + 1) * k); };
  auto cell_id = [&](Index i, Index j, Index k) { return i + nx * (j + ny * k); };

  RawPolyMesh r;
  r.points.resize(static_cast<std::size_t>((nx + 1) * (ny + 1) * (nz + 1)));
  std::mt19937 rng(seed);
  std::uniform_real_distribution<Real> uni(-1.0, 1.0);
  for (Index k = 0; k <= nz; ++k)
    for (Index j = 0; j <= ny; ++j)
      for (Index i = 0; i <= nx; ++i) {
        Vec3 p{i * h[0], j * h[1], k * h[2]};
        const bool interior = i > 0 && i < nx && j > 0 && j < ny && k > 0 && k < nz;
        if (jitter > 0 && interior) {  // boundary nodes stay fixed -> domain unchanged
          p.x += jitter * h[0] * uni(rng);
          p.y += jitter * h[1] * uni(rng);
          p.z += jitter * h[2] * uni(rng);
        }
        r.points.set(static_cast<std::size_t>(node_id(i, j, k)), p);
      }

  auto orient = [&r](std::array<Index, 4>& q, Vec3 out) {
    const Vec3 a = r.points.get(q[0]), b = r.points.get(q[1]), c = r.points.get(q[2]);
    if (dot(cross(b - a, c - a), out) < 0) std::swap(q[1], q[3]);
  };

  struct TmpFace {
    std::array<Index, 4> node;
    Index own;
    Index nei;
    int patch;  // -1 internal, 0..5 = xMin,xMax,yMin,yMax,zMin,zMax
  };
  std::vector<TmpFace> faces;

  for (int ax = 0; ax < 3; ++ax) {
    const int ib = (ax + 1) % 3, ic = (ax + 2) % 3;
    auto at = [&](Index p, Index u, Index v) {
      std::array<Index, 3> a{};
      a[ax] = p;
      a[ib] = u;
      a[ic] = v;
      return a;
    };
    auto nid = [&](Index p, Index u, Index v) { const auto a = at(p, u, v); return node_id(a[0], a[1], a[2]); };
    auto cid = [&](Index p, Index u, Index v) { const auto a = at(p, u, v); return cell_id(a[0], a[1], a[2]); };

    std::array<Real, 3> unit{};
    unit[ax] = 1.0;
    const Vec3 e{unit[0], unit[1], unit[2]};

    for (Index p = 0; p <= n[ax]; ++p)
      for (Index u = 0; u < n[ib]; ++u)
        for (Index v = 0; v < n[ic]; ++v) {
          TmpFace tf{{nid(p, u, v), nid(p, u + 1, v), nid(p, u + 1, v + 1), nid(p, u, v + 1)},
                     -1, -1, -1};
          Vec3 out = e;
          if (p > 0 && p < n[ax]) {
            tf.own = cid(p - 1, u, v);
            tf.nei = cid(p, u, v);
            tf.patch = -1;
          } else if (p == 0) {
            tf.own = cid(0, u, v);
            tf.patch = 2 * ax;
            out = -1.0 * e;
          } else {
            tf.own = cid(n[ax] - 1, u, v);
            tf.patch = 2 * ax + 1;
          }
          orient(tf.node, out);
          faces.push_back(tf);
        }
  }

  std::stable_sort(faces.begin(), faces.end(),
                   [](const TmpFace& a, const TmpFace& b) { return a.patch < b.patch; });

  std::array<Index, 6> count{};
  r.face_ptr.assign(1, Offset{0});
  for (const TmpFace& tf : faces) {
    r.owner.push_back(tf.own);
    if (tf.patch < 0) {
      r.neighbour.push_back(tf.nei);
    } else {
      ++count[static_cast<std::size_t>(tf.patch)];
    }
    r.face_pts.insert(r.face_pts.end(), tf.node.begin(), tf.node.end());
    r.face_ptr.push_back(static_cast<Offset>(r.face_pts.size()));
  }

  static constexpr std::array<const char*, 6> kNames{"xMin", "xMax", "yMin", "yMax", "zMin", "zMax"};
  Index start = static_cast<Index>(r.neighbour.size());
  for (std::size_t q = 0; q < 6; ++q) {
    r.patches.push_back(Patch{kNames[q], "patch", start, count[q]});
    start += count[q];
  }
  return r;
}

}  // namespace difvm::mesh
```

---

### `src/mesh_build.cpp`

```cpp
// Geometry, static graph, Morton renumbering and the MeshErrorMap.
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <numeric>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include "difvm/mesh/mesh.hpp"

#if defined(_OPENMP)
#define DIFVM_PAR_FOR _Pragma("omp parallel for schedule(static)")
#else
#define DIFVM_PAR_FOR
#endif

namespace difvm::mesh {
namespace {

constexpr Real kPi = 3.141592653589793238462643383279502884;
constexpr Real kRadToDeg = 180.0 / kPi;
constexpr Real kEps = 1e-14;      // relative guard
constexpr Real kMinCos = 1e-3;    // |cos theta| below this = degenerate (theta > 89.94 deg)

inline Real sigmoid(Real x) { return 0.5 * (1.0 + std::tanh(x)); }
inline Real clamp01(Real x) { return std::min(1.0, std::max(0.0, x)); }

// ---- permutation helpers ----------------------------------------------------
template <typename T>
void apply_perm(AlignedVec<T>& v, const std::vector<Index>& perm) {
  AlignedVec<T> tmp(v.size());
  for (std::size_t i = 0; i < v.size(); ++i) tmp[i] = v[static_cast<std::size_t>(perm[i])];
  v.swap(tmp);
}

void apply_perm(Vec3Field& v, const std::vector<Index>& perm) {
  apply_perm(v.x, perm);
  apply_perm(v.y, perm);
  apply_perm(v.z, perm);
}

// ---- validation -------------------------------------------------------------
Index validate(const RawPolyMesh& r) {
  const auto nf = r.owner.size();
  const auto ni = r.neighbour.size();
  if (r.face_ptr.size() != nf + 1) throw std::runtime_error("polyMesh: faces/owner size mismatch");
  if (ni > nf) throw std::runtime_error("polyMesh: more neighbours than faces");

  Index max_cell = -1;
  for (Index c : r.owner) {
    if (c < 0) throw std::runtime_error("polyMesh: negative owner label");
    max_cell = std::max(max_cell, c);
  }
  for (Index c : r.neighbour) {
    if (c < 0) throw std::runtime_error("polyMesh: negative neighbour label");
    max_cell = std::max(max_cell, c);
  }
  if (max_cell < 0) throw std::runtime_error("polyMesh: no cells");

  for (std::size_t f = 0; f < nf; ++f) {
    if (r.face_ptr[f + 1] - r.face_ptr[f] < 3) throw std::runtime_error("polyMesh: face with < 3 points");
  }
  for (Index p : r.face_pts) {
    if (p < 0 || static_cast<std::size_t>(p) >= r.points.size()) {
      throw std::runtime_error("polyMesh: point index out of range");
    }
  }

  std::int64_t covered = 0;
  for (const Patch& p : r.patches) {
    if (p.start_face < static_cast<Index>(ni) || p.n_faces < 0 ||
        p.start_face + p.n_faces > static_cast<Index>(nf)) {
      throw std::runtime_error("polyMesh: patch '" + p.name + "' outside boundary face range");
    }
    covered += p.n_faces;
  }
  if (covered != static_cast<std::int64_t>(nf - ni)) {
    throw std::runtime_error("polyMesh: patches do not cover all boundary faces");
  }
  return max_cell + 1;
}

// ---- face geometry: triangle fan around the average point ------------------
void compute_face_geometry(const RawPolyMesh& raw, Mesh& m) {
  DIFVM_PAR_FOR
  for (Index f = 0; f < m.n_faces; ++f) {
    const Offset b = raw.face_ptr[f];
    const auto n = static_cast<Index>(raw.face_ptr[f + 1] - b);

    Vec3 c{};
    for (Index i = 0; i < n; ++i) c = c + raw.points.get(raw.face_pts[b + i]);
    c = c / static_cast<Real>(n);

    Vec3 S{}, acc{};
    Real wsum = 0.0;
    for (Index i = 0; i < n; ++i) {
      const Vec3 p0 = raw.points.get(raw.face_pts[b + i]);
      const Vec3 p1 = raw.points.get(raw.face_pts[b + (i + 1) % n]);
      const Vec3 a = 0.5 * cross(p0 - c, p1 - c);  // triangle area vector
      const Real ma = norm(a);
      S = S + a;
      acc = acc + ma * ((c + p0 + p1) / 3.0);      // triangle centroid, area-weighted
      wsum += ma;
    }
    const Vec3 xf = wsum > 0 ? acc / wsum : c;
    m.Sf.set(f, S);
    m.magSf[f] = norm(S);
    m.face_centre.set(f, xf);

    const Real mag = m.magSf[f];
    if (mag > 0) {
      const Vec3 nhat = S / mag;
      Real dev = 0.0;
      for (Index i = 0; i < n; ++i) {
        dev = std::max(dev, std::abs(dot(raw.points.get(raw.face_pts[b + i]) - xf, nhat)));
      }
      m.errors.planarity[f] = dev / std::sqrt(mag);
    }
  }
}

// ---- cell -> face CSR (gather graph) ----------------------------------------
void build_cell_face_csr(Mesh& m) {
  const auto nc = static_cast<std::size_t>(m.n_cells);
  AlignedVec<Offset>& ptr = m.cell_face_ptr;
  ptr.assign(nc + 1, Offset{0});
  for (Index f = 0; f < m.n_faces; ++f) ++ptr[static_cast<std::size_t>(m.owner[f]) + 1];
  for (Index f = 0; f < m.n_internal; ++f) ++ptr[static_cast<std::size_t>(m.neighbour[f]) + 1];
  std::partial_sum(ptr.begin(), ptr.end(), ptr.begin());

  for (std::size_t c = 0; c < nc; ++c) {
    if (ptr[c + 1] == ptr[c]) {
      throw std::runtime_error("polyMesh: cell " + std::to_string(c) + " has no faces (unused label)");
    }
  }

  const auto total = static_cast<std::size_t>(ptr[nc]);
  m.cell_face.assign(total, Index{0});
  m.cell_face_sign.assign(total, std::int8_t{0});

  AlignedVec<Offset> fill(ptr.begin(), ptr.end() - 1);
  auto put = [&](Index c, Index f, std::int8_t s) {
    const auto pos = static_cast<std::size_t>(fill[static_cast<std::size_t>(c)]++);
    m.cell_face[pos] = f;
    m.cell_face_sign[pos] = s;
  };
  for (Index f = 0; f < m.n_faces; ++f) put(m.owner[f], f, 1);
  for (Index f = 0; f < m.n_internal; ++f) put(m.neighbour[f], f, -1);
}

// ---- cell volumes and centres: pyramid decomposition (gather, race-free) ----
void compute_cell_geometry(Mesh& m) {
  const auto nc = static_cast<std::size_t>(m.n_cells);
  DIFVM_PAR_FOR
  for (std::size_t c = 0; c < nc; ++c) {
    const Offset b = m.cell_face_ptr[c], e = m.cell_face_ptr[c + 1];

    Vec3 apex{};  // pyramid apex: mean of face centres (any point works exactly)
    for (Offset k = b; k < e; ++k) apex = apex + m.face_centre.get(m.cell_face[k]);
    apex = apex / static_cast<Real>(e - b);

    Real vol = 0.0;
    Vec3 acc{};
    for (Offset k = b; k < e; ++k) {
      const Index f = m.cell_face[k];
      const Vec3 xf = m.face_centre.get(f);
      const Vec3 n_out = static_cast<Real>(m.cell_face_sign[k]) * m.Sf.get(f);
      const Real pv = dot(xf - apex, n_out) / 3.0;  // signed pyramid volume
      vol += pv;
      acc = acc + pv * (0.75 * xf + 0.25 * apex);   // pyramid centroid
    }
    m.cell_volume[c] = vol;
    m.cell_centre.set(c, vol != 0.0 ? acc / vol : apex);
  }
}

// ---- Morton (Z-order) renumbering of cells ----------------------------------
std::uint64_t spread3(std::uint64_t x) {
  x &= 0x1fffffULL;
  x = (x | (x << 32)) & 0x1f00000000ffffULL;
  x = (x | (x << 16)) & 0x1f0000ff0000ffULL;
  x = (x | (x << 8)) & 0x100f00f00f00f00fULL;
  x = (x | (x << 4)) & 0x10c30c30c30c30c3ULL;
  x = (x | (x << 2)) & 0x1249249249249249ULL;
  return x;
}

std::uint64_t morton3(std::uint32_t x, std::uint32_t y, std::uint32_t z) {
  return spread3(x) | (spread3(y) << 1) | (spread3(z) << 2);
}

std::uint32_t quant21(Real t) {
  t = std::clamp(t, 0.0, 1.0);
  return static_cast<std::uint32_t>(t * 2097151.0);
}

void morton_reorder_cells(Mesh& m) {
  const auto nc = static_cast<std::size_t>(m.n_cells);
  if (nc == 0) return;

  Vec3 lo{std::numeric_limits<Real>::max(), std::numeric_limits<Real>::max(), std::numeric_limits<Real>::max()};
  Vec3 hi{std::numeric_limits<Real>::lowest(), std::numeric_limits<Real>::lowest(), std::numeric_limits<Real>::lowest()};
  for (std::size_t c = 0; c < nc; ++c) {
    const Vec3 p = m.cell_centre.get(c);
    lo = {std::min(lo.x, p.x), std::min(lo.y, p.y), std::min(lo.z, p.z)};
    hi = {std::max(hi.x, p.x), std::max(hi.y, p.y), std::max(hi.z, p.z)};
  }
  Vec3 span = hi - lo;
  if (span.x <= 0) span.x = 1;
  if (span.y <= 0) span.y = 1;
  if (span.z <= 0) span.z = 1;

  std::vector<std::uint64_t> code(nc);
  DIFVM_PAR_FOR
  for (std::size_t c = 0; c < nc; ++c) {
    const Vec3 p = m.cell_centre.get(c);
    code[c] = morton3(quant21((p.x - lo.x) / span.x),
                      quant21((p.y - lo.y) / span.y),
                      quant21((p.z - lo.z) / span.z));
  }

  std::vector<Index> perm(nc);  // new -> old
  std::iota(perm.begin(), perm.end(), Index{0});
  std::sort(perm.begin(), perm.end(), [&](Index a, Index b) {
    const auto ca = code[static_cast<std::size_t>(a)], cb = code[static_cast<std::size_t>(b)];
    return ca != cb ? ca < cb : a < b;
  });
  std::vector<Index> inv(nc);  // old -> new
  for (std::size_t i = 0; i < nc; ++i) inv[static_cast<std::size_t>(perm[i])] = static_cast<Index>(i);

  apply_perm(m.cell_centre, perm);
  apply_perm(m.cell_volume, perm);

  for (Index f = 0; f < m.n_faces; ++f) m.owner[f] = inv[static_cast<std::size_t>(m.owner[f])];
  for (Index f = 0; f < m.n_internal; ++f) {
    m.neighbour[f] = inv[static_cast<std::size_t>(m.neighbour[f])];
    if (m.owner[f] > m.neighbour[f]) {  // keep owner < neighbour; flip normal
      std::swap(m.owner[f], m.neighbour[f]);
      m.Sf.flip(static_cast<std::size_t>(f));
    }
  }
}

// ---- face ordering: internal by (owner, neighbour); patches by owner ---------
void sort_faces(Mesh& m) {
  const auto ni = static_cast<std::size_t>(m.n_internal);
  const auto nf = static_cast<std::size_t>(m.n_faces);

  std::vector<Index> perm(nf);
  std::iota(perm.begin(), perm.end(), Index{0});
  std::sort(perm.begin(), perm.begin() + static_cast<std::ptrdiff_t>(ni), [&](Index a, Index b) {
    if (m.owner[a] != m.owner[b]) return m.owner[a] < m.owner[b];
    return m.neighbour[a] < m.neighbour[b];
  });
  for (const Patch& p : m.patches) {
    auto first = perm.begin() + p.start_face;
    std::stable_sort(first, first + p.n_faces,
                     [&](Index a, Index b) { return m.owner[a] < m.owner[b]; });
  }

  apply_perm(m.owner, perm);
  apply_perm(m.neighbour, perm);  // only the first ni entries are used; perm maps them into [0, ni)
  apply_perm(m.Sf, perm);
  apply_perm(m.magSf, perm);
  apply_perm(m.face_centre, perm);
  apply_perm(m.errors.planarity, perm);
}

// ---- internal-face derived quantities + per-face error map ------------------
void compute_internal_derived(Mesh& m, const MeshQualityConfig& q) {
  auto& E = m.errors;
  DIFVM_PAR_FOR
  for (Index f = 0; f < m.n_internal; ++f) {
    const Index P = m.owner[f];
    const Index N = m.neighbour[f];
    const Vec3 xP = m.cell_centre.get(P);
    const Vec3 xN = m.cell_centre.get(N);
    const Vec3 xf = m.face_centre.get(f);
    const Vec3 S = m.Sf.get(f);
    const Real magS = m.magSf[f];
    const Vec3 d = xN - xP;
    const Real magd = norm(d);
    const Real inv_d = magd > 0 ? 1.0 / magd : 0.0;

    m.d.set(f, d);
    m.inv_magd[f] = inv_d;

    // Linear interpolation weight of the owner (inverse normal-distance)
    const Real sOwn = std::abs(dot(S, xf - xP));
    const Real sNei = std::abs(dot(S, xN - xf));
    m.w_owner[f] = (sOwn + sNei > 0) ? sNei / (sOwn + sNei) : 0.5;

    // Over-relaxed decomposition (Jasak):  Delta = |S|^2/(S·d) d,  k = S - Delta
    const Real Sd = dot(S, d);
    const Real cosS = (magS > 0 && magd > 0) ? Sd / (magS * magd) : 0.0;  // signed
    const Real cos_abs = std::abs(cosS);
    const bool dir_degenerate = cos_abs < kMinCos || magd <= 0 || magS <= 0;
    const Real cos_safe = std::copysign(std::max(cos_abs, kMinCos), cosS);
    const Vec3 delta = (magS / cos_safe) * (d * inv_d);
    const Vec3 kf = S - delta;
    m.delta.set(f, delta);
    m.k.set(f, kf);

    const Real theta = std::acos(std::min(1.0, cos_abs)) * kRadToDeg;

    // Skewness: face centre vs. intersection of the P->N line with the face plane
    const bool sd_tiny = std::abs(Sd) <= kEps * magS * magd;
    Vec3 m_sk{};
    Real skew = std::numeric_limits<Real>::infinity();
    if (!sd_tiny) {
      const Real t = dot(xf - xP, S) / Sd;
      m_sk = xf - (xP + t * d);
      skew = norm(m_sk) * inv_d;
    }

    const Real VP = m.cell_volume[P], VN = m.cell_volume[N];
    const Real vmin = std::min(VP, VN), vmax = std::max(VP, VN);
    const Real expansion = vmin > 0 ? vmax / vmin : std::numeric_limits<Real>::infinity();

    const Real b_theta = sigmoid((q.theta_blend0_deg - theta) / q.theta_blend_width_deg);
    const Real b_skew = sigmoid((q.skew_blend0 - skew) / q.skew_blend_width);
    const Real beta = clamp01(b_theta * b_skew);
    const Real s_k = q.k_damp_min +
                     (1.0 - q.k_damp_min) * sigmoid((q.theta_crit_deg - theta) / q.theta_damp_width_deg);

    E.theta_deg[f] = theta;
    E.skew[f] = skew;
    E.expansion[f] = expansion;
    E.beta[f] = beta;
    E.degenerate[f] = (dir_degenerate || sd_tiny) ? 1 : 0;
    E.m_skew.set(f, m_sk);
    E.k_comp.set(f, s_k * kf);
  }
}

// ---- per-cell error map, flags and global statistics ------------------------
void compute_cell_errors(Mesh& m, const MeshQualityConfig& q) {
  auto& E = m.errors;
  const auto nc = static_cast<std::size_t>(m.n_cells);

  DIFVM_PAR_FOR
  for (std::size_t c = 0; c < nc; ++c) {
    Real min_beta = 1.0, max_theta = 0.0, max_skew = 0.0, max_exp = 1.0, sum_area = 0.0;
    Vec3 sum_S{};
    bool has_internal = false;
    std::uint8_t fl = 0;

    for (Offset k = m.cell_face_ptr[c]; k < m.cell_face_ptr[c + 1]; ++k) {
      const Index f = m.cell_face[k];
      sum_S = sum_S + static_cast<Real>(m.cell_face_sign[k]) * m.Sf.get(f);
      sum_area += m.magSf[f];
      if (f < m.n_internal) {
        has_internal = true;
        min_beta = std::min(min_beta, E.beta[f]);
        max_theta = std::max(max_theta, E.theta_deg[f]);
        max_skew = std::max(max_skew, E.skew[f]);
        max_exp = std::max(max_exp, E.expansion[f]);
        if (E.degenerate[f]) fl |= kCellDegenerate;
      }
    }

    E.omega[c] = has_internal ? std::max(q.omega_min, min_beta) : 1.0;
    E.closedness[c] = sum_area > 0 ? norm(sum_S) / sum_area : 0.0;
    if (E.closedness[c] > 1e-10) fl |= kCellNotClosed;
    if (max_theta > q.nonorth_flag_deg) fl |= kCellNonOrth;
    if (max_skew > q.skew_flag) fl |= kCellSkew;
    if (max_exp > q.expansion_flag) fl |= kCellExpansion;
    if (m.cell_volume[c] <= 0) fl |= kCellNegativeVolume;
    E.flags[c] = fl;
  }

  // Global statistics (serial, O(N))
  QualityStats& s = E.stats;
  s = QualityStats{};
  s.min_volume = std::numeric_limits<Real>::infinity();
  Real sum_theta = 0.0;
  for (Index f = 0; f < m.n_internal; ++f) {
    s.max_nonorth_deg = std::max(s.max_nonorth_deg, E.theta_deg[f]);
    sum_theta += E.theta_deg[f];
    s.max_skew = std::max(s.max_skew, E.skew[f]);
    s.max_expansion = std::max(s.max_expansion, E.expansion[f]);
    s.n_degenerate_faces += E.degenerate[f];
  }
  s.mean_nonorth_deg = m.n_internal > 0 ? sum_theta / static_cast<Real>(m.n_internal) : 0.0;
  for (Real p : E.planarity) s.max_planarity = std::max(s.max_planarity, p);
  for (std::size_t c = 0; c < nc; ++c) {
    s.min_volume = std::min(s.min_volume, m.cell_volume[c]);
    if (m.cell_volume[c] <= 0) ++s.n_negative_volume;
    s.max_closedness = std::max(s.max_closedness, E.closedness[c]);
    if (E.flags[c]) ++s.n_flagged_cells;
  }
}

}  // namespace

Mesh build_mesh(const RawPolyMesh& raw, const BuildOptions& opt) {
  const Index n_cells = validate(raw);

  Mesh m;
  m.n_cells = n_cells;
  m.n_faces = static_cast<Index>(raw.owner.size());
  m.n_internal = static_cast<Index>(raw.neighbour.size());
  m.patches = raw.patches;

  const auto nc = static_cast<std::size_t>(m.n_cells);
  const auto nf = static_cast<std::size_t>(m.n_faces);
  const auto ni = static_cast<std::size_t>(m.n_internal);

  m.owner = raw.owner;
  m.neighbour = raw.neighbour;
  m.cell_centre.resize(nc);
  m.cell_volume.assign(nc, 0.0);
  m.Sf.resize(nf);
  m.magSf.assign(nf, 0.0);
  m.face_centre.resize(nf);
  m.errors.planarity.assign(nf, 0.0);

  compute_face_geometry(raw, m);
  build_cell_face_csr(m);          // temporary: needed for cell geometry
  compute_cell_geometry(m);
  if (opt.morton_reorder) morton_reorder_cells(m);
  if (opt.sort_faces) sort_faces(m);
  build_cell_face_csr(m);          // final CSR for the renumbered mesh

  m.d.resize(ni);
  m.inv_magd.assign(ni, 0.0);
  m.w_owner.assign(ni, 0.5);
  m.delta.resize(ni);
  m.k.resize(ni);

  auto& E = m.errors;
  E.theta_deg.assign(ni, 0.0);
  E.skew.assign(ni, 0.0);
  E.expansion.assign(ni, 1.0);
  E.beta.assign(ni, 1.0);
  E.degenerate.assign(ni, 0);
  E.m_skew.resize(ni);
  E.k_comp.resize(ni);
  E.omega.assign(nc, 1.0);
  E.closedness.assign(nc, 0.0);
  E.flags.assign(nc, 0);

  compute_internal_derived(m, opt.quality);
  compute_cell_errors(m, opt.quality);
  return m;
}

}  // namespace difvm::mesh
```

---

### `src/main.cpp`

```cpp
// CLI and self-tests for the DiFVM mesher.
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>

#include "difvm/mesh/mesh.hpp"

using namespace difvm::mesh;
namespace fs = std::filesystem;

namespace {

int g_failures = 0;

void check(bool ok, const std::string& what) {
  std::cout << (ok ? "  [ok]   " : "  [FAIL] ") << what << '\n';
  if (!ok) ++g_failures;
}

Real total_volume(const Mesh& m) {
  Real v = 0.0;
  for (Real x : m.cell_volume) v += x;
  return v;
}

// Discrete Gauss gradient of phi = x through the CSR gather graph.
// Must be exact on an orthogonal Cartesian mesh.
Real gauss_gradient_error_for_x(const Mesh& m) {
  Real worst = 0.0;
  for (Index c = 0; c < m.n_cells; ++c) {
    Vec3 g{};
    for (Offset k = m.cell_face_ptr[c]; k < m.cell_face_ptr[c + 1]; ++k) {
      const Index f = m.cell_face[k];
      Real phi_f;
      if (f < m.n_internal) {
        const Index P = m.owner[f], N = m.neighbour[f];
        const Real w = m.w_owner[f];
        phi_f = w * m.cell_centre.x[P] + (1.0 - w) * m.cell_centre.x[N];
      } else {
        phi_f = m.face_centre.x[f];
      }
      g = g + (static_cast<Real>(m.cell_face_sign[k]) * phi_f) * m.Sf.get(f);
    }
    g = g / m.cell_volume[c];
    worst = std::max(worst, norm(g - Vec3{1.0, 0.0, 0.0}));
  }
  return worst;
}

void print_summary(const Mesh& m, double build_ms) {
  const auto& s = m.errors.stats;
  std::cout << "cells " << m.n_cells << "  faces " << m.n_faces
            << "  internal " << m.n_internal << "  patches " << m.patches.size() << '\n';
  std::cout << "build time            " << build_ms << " ms\n";
  std::cout << "total volume          " << total_volume(m) << '\n';
  std::cout << "non-orth max / mean   " << s.max_nonorth_deg << " / " << s.mean_nonorth_deg << " deg\n";
  std::cout << "skew max              " << s.max_skew << '\n';
  std::cout << "expansion max         " << s.max_expansion << '\n';
  std::cout << "planarity max         " << s.max_planarity << '\n';
  std::cout << "closedness max        " << s.max_closedness << '\n';
  std::cout << "min volume            " << s.min_volume << '\n';
  std::cout << "negative volumes      " << s.n_negative_volume << '\n';
  std::cout << "flagged cells         " << s.n_flagged_cells << '\n';
  std::cout << "degenerate faces      " << s.n_degenerate_faces << '\n';
}

// ---- self-tests -------------------------------------------------------------

void test_cartesian_clean() {
  std::cout << "Cartesian 8x8x8 unit cube, no jitter\n";
  const Mesh m = build_mesh(make_cartesian_hex(8, 8, 8, 1.0, 1.0, 1.0));
  const auto& s = m.errors.stats;

  check(std::abs(total_volume(m) - 1.0) < 1e-12, "total volume == 1");

  Real max_dv = 0.0;
  for (Real v : m.cell_volume) max_dv = std::max(max_dv, std::abs(v - 1.0 / 512.0));
  check(max_dv < 1e-14, "every cell volume == h^3");

  check(s.max_closedness < 1e-12, "closed cells: |sum S_f| / sum |S_f| < 1e-12");
  check(s.max_nonorth_deg < 1e-6, "orthogonal mesh: non-orthogonality ~ 0");
  check(s.max_skew < 1e-12, "orthogonal mesh: no skewness");
  check(s.n_flagged_cells == 0, "no flagged cells on a clean mesh");

  Real min_beta = 1.0;
  for (Real b : m.errors.beta) min_beta = std::min(min_beta, b);
  check(min_beta > 0.95, "beta_f ~ 1 on a clean mesh (full 2nd-order)");

  check(gauss_gradient_error_for_x(m) < 1e-12, "Gauss gradient of phi = x is exact via CSR gather");
}

void test_jittered() {
  std::cout << "Cartesian 8x8x8 with 0.3 h interior jitter\n";
  const Mesh m = build_mesh(make_cartesian_hex(8, 8, 8, 1.0, 1.0, 1.0, 0.3, 42));
  const auto& s = m.errors.stats;

  // Boundary is fixed and planar, so the total volume must be exact.
  check(std::abs(total_volume(m) - 1.0) < 1e-10, "total volume preserved under interior jitter");
  check(s.max_closedness < 1e-12, "closedness preserved under jitter");
  check(s.max_nonorth_deg > 1.0, "jitter produces non-orthogonality");
  check(s.n_negative_volume == 0, "no negative cell volumes");

  bool beta_ok = true, omega_ok = true;
  for (Real b : m.errors.beta) beta_ok = beta_ok && b >= 0.0 && b <= 1.0;
  for (Real w : m.errors.omega) omega_ok = omega_ok && w > 0.0 && w <= 1.0;
  check(beta_ok, "beta_f in [0, 1]");
  check(omega_ok, "omega_i in (0, 1]");

  bool orth_ok = true;  // Delta_f must be parallel to d and satisfy Delta . Shat = |S|
  for (Index f = 0; f < m.n_internal && orth_ok; ++f) {
    const Real magS = m.magSf[f];
    const Vec3 Sf = m.Sf.get(f);
    const Vec3 dl = m.delta.get(f);
    orth_ok = std::abs(dot(dl, Sf) / magS - magS) < 1e-9 * (1.0 + magS);
  }
  check(orth_ok, "over-relaxed Delta_f . S_hat == |S_f|");
}

void test_polymesh_reader() {
  std::cout << "OpenFOAM ASCII polyMesh reader (single hex cell fixture)\n";
  const fs::path root = fs::temp_directory_path() / "difvm_selftest";
  const fs::path dir = root / "constant" / "polyMesh";
  fs::create_directories(dir);

  auto header = [](const std::string& cls, const std::string& obj) {
    return "FoamFile\n{\n    version 2.0;\n    format      ascii;\n    class       " + cls +
           ";\n    object      " + obj + ";\n}\n// fixture\n";
  };
  std::ofstream(dir / "points")
      << header("vectorField", "points")
      << "8\n(\n(0 0 0)\n(1 0 0)\n(1 1 0)\n(0 1 0)\n(0 0 1)\n(1 0 1)\n(1 1 1)\n(0 1 1)\n)\n";
  std::ofstream(dir / "faces")
      << header("faceList", "faces")
      << "6\n(\n4(0 4 7 3)\n4(1 2 6 5)\n4(0 1 5 4)\n4(3 7 6 2)\n4(0 3 2 1)\n4(4 5 6 7)\n)\n";
  std::ofstream(dir / "owner")
      << header("labelList", "owner") << "6\n(\n0\n0\n0\n0\n0\n0\n)\n";
  std::ofstream(dir / "neighbour")
      << header("labelList", "neighbour") << "0\n(\n)\n";
  std::ofstream(dir / "boundary")
      << header("polyBoundaryMesh", "boundary")
      << "1\n(\nbox\n{\n    type patch;\n    nFaces 6;\n    startFace 0;\n}\n)\n";

  const Mesh m = build_mesh(read_openfoam_polymesh(dir));
  check(m.n_cells == 1, "one cell read");
  check(m.n_faces == 6 && m.n_internal == 0, "six boundary faces, no internal faces");
  check(std::abs(total_volume(m) - 1.0) < 1e-14, "unit cube volume from the pyramid decomposition");
  check(norm(m.cell_centre.get(0) - Vec3{0.5, 0.5, 0.5}) < 1e-14, "cell centre (0.5, 0.5, 0.5)");

  bool unit_faces = true;
  for (Real a : m.magSf) unit_faces = unit_faces && std::abs(a - 1.0) < 1e-14;
  check(unit_faces, "all face areas == 1");
  check(m.errors.stats.max_closedness < 1e-14, "single cell is closed");

  std::error_code ec;
  fs::remove_all(root, ec);
}

int run_selftests() {
  std::cout << "== DiFVM mesher self-tests ==\n";
  test_cartesian_clean();
  test_jittered();
  test_polymesh_reader();
  std::cout << (g_failures == 0 ? "ALL PASSED\n" : "FAILURES: " + std::to_string(g_failures) + "\n");
  return g_failures;
}

}  // namespace

int main(int argc, char** argv) {
  try {
    fs::path polymesh;
    Index nx = 16, ny = 16, nz = 16;
    Real jitter = 0.0;
    bool morton = true;
    bool selftest = false;

    for (int i = 1; i < argc; ++i) {
      const std::string_view a = argv[i];
      auto next = [&]() -> std::string {
        if (i + 1 >= argc) throw std::invalid_argument(std::string(a) + " needs a value");
        return argv[++i];
      };
      if (a == "--selftest") {
        selftest = true;
      } else if (a == "--polymesh") {
        polymesh = next();
      } else if (a == "--cartesian") {
        nx = std::stoi(next());
        ny = std::stoi(next());
        nz = std::stoi(next());
      } else if (a == "--jitter") {
        jitter = std::stod(next());
      } else if (a == "--no-morton") {
        morton = false;
      } else {
        throw std::invalid_argument("unknown option " + std::string(a));
      }
    }

    if (selftest) return run_selftests();

    const auto t0 = std::chrono::steady_clock::now();
    const RawPolyMesh raw = polymesh.empty()
                                ? make_cartesian_hex(nx, ny, nz, 1.0, 1.0, 1.0, jitter)
                                : read_openfoam_polymesh(polymesh);
    BuildOptions opt;
    opt.morton_reorder = morton;
    const Mesh m = build_mesh(raw, opt);
    const auto t1 = std::chrono::steady_clock::now();

    print_summary(m, std::chrono::duration<double, std::milli>(t1 - t0).count());
    return 0;
  } catch (const std::exception& e) {
    std::cerr << "error: " << e.what() << '\n';
    return 2;
  }
}
```

---

## Build and run

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j

./build/difvm_mesher --selftest                       # run all checks
./build/difvm_mesher --cartesian 128 128 128 --jitter 0.2
./build/difvm_mesher --polymesh path/to/case/constant/polyMesh
```

The self-tests should end with `ALL PASSED`. The OpenFOAM reader can be checked against a real case by running `blockMesh` and passing the `constant/polyMesh` directory.

## Design notes

- **Gather instead of scatter.** Cell-indexed quantities (volumes, centres, error flags, closedness) are computed through the cell→face CSR. Each cell writes only its own slot, so OpenMP needs no atomics and results are deterministic.
- **Owner-sorted faces and Morton cell order.** Consecutive cells are close in memory, and each cell's faces are contiguous. This matches the spec's Zero-Copy partitioning idea: a contiguous cell range is a spatially compact partition.
- **Exact closedness.** Each face normal is the sum of fan-triangle area vectors, so Σ S_f = 0 per cell up to rounding. This is the strongest single correctness check for the whole pipeline.
- **Over-relaxed correction.** Δ_f = |S_f|²/(S_f·d) · d and k_f = S_f − Δ_f, as in the DiFVM paper. The signed form avoids sign flips on mildly inverted cells.

## Limitations and next steps

- **ASCII only.** Binary `polyMesh` must be converted with `foamFormatConvert -ascii`. Binary support and `mmap` input are the next I/O step.
- **`faceCompactList`** (separate offset list) is not parsed. Add it to `read_faces` if your OpenFOAM version writes it.
- **`Index` is 32-bit.** Offsets are 64-bit, but for more than 2³¹ faces, switch `Index` to `int64_t` in `mesh.hpp`.
- **`std::from_chars` for `double`** needs GCC 11+, recent MSVC, or a current libc++. Otherwise, substitute `fast_float`.
- **Not yet included:** VTK import, MPI/METIS partitioning, and a C ABI (`extern "C"`) for the Rust layer. The `Mesh` struct is already a flat set of aligned arrays, so the C ABI wrapper is a thin layer on top.