#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <memory>
#include <vector>

#include "difvm_mesh.hpp"
#include "sdf_engine.hpp"

namespace cfd::octree {

inline constexpr uint8_t kLeafFlagActive = 1u << 0;
inline constexpr uint8_t kLeafFlagCut    = 1u << 1;
inline constexpr uint8_t kLeafFlagRefine = 1u << 2;
inline constexpr uint8_t kLeafFlagBlBand = 1u << 3;

inline constexpr uint8_t kMaxOctreeLevel = 19;
inline constexpr int64_t kLatticePerRoot = int64_t{1} << kMaxOctreeLevel;  // 524288
inline constexpr uint32_t kLatticeMask   = static_cast<uint32_t>(kLatticePerRoot - 1);

struct Leaf {
  uint64_t key;    // [63..5] 57-bit 3D Morton code (19 bits/axis), [4..0] level (0..19)
  uint32_t root;   // graded root cell index
  uint16_t zone;   // zone id
  uint8_t  flags;  // Bit0 active, Bit1 cut, Bit2 refine, Bit3 bl_band
  uint8_t  owned;  // 8-bit corner ownership bitmask ("smallest (root, key) owns")
};
static_assert(sizeof(Leaf) == 16, "Leaf must be exactly 16 bytes");

struct RegularFace {
  int32_t owner;
  int32_t neighbour;
  uint32_t meta;  // axis_dir (3b), coarse_level (5b), flags (24b)
};
static_assert(sizeof(RegularFace) == 12, "RegularFace must be 12 bytes");

struct GeneralFace {
  std::array<int32_t, 4> nodes;
  int32_t owner;
  int32_t neighbour;  // -1 for boundary faces
  uint16_t patch_id;
  uint16_t flags;     // Bit0: transition, Bit1: farfield, Bit2: wall
};
static_assert(sizeof(GeneralFace) == 28, "GeneralFace must be 28 bytes");

struct MortonSampleKey {
  uint32_t root;
  uint64_t key;
};

struct MortonBuildStats {
  int64_t n_leaves = 0;
  int64_t n_cells = 0;
  int64_t n_regular_faces = 0;
  int64_t n_general_faces = 0;
  int64_t n_vertices = 0;
  uint64_t peak_bytes = 0;
  double build_time_s = 0.0;
  int32_t balanced_ok = 0;
};

inline uint64_t split_by_3(uint32_t a) noexcept {
  uint64_t x = static_cast<uint64_t>(a & kLatticeMask);
  x = (x | (x << 32)) & 0x1f00000000ffffULL;
  x = (x | (x << 16)) & 0x1f0000ff0000ffULL;
  x = (x | (x << 8))  & 0x100f00f00f00f00fULL;
  x = (x | (x << 4))  & 0x10c30c30c30c30c3ULL;
  x = (x | (x << 2))  & 0x1249249249249249ULL;
  return x;
}

inline uint32_t compact_by_3(uint64_t x) noexcept {
  x &= 0x1249249249249249ULL;
  x = (x | (x >> 2))  & 0x10c30c30c30c30c3ULL;
  x = (x | (x >> 4))  & 0x100f00f00f00f00fULL;
  x = (x | (x >> 8))  & 0x1f0000ff0000ffULL;
  x = (x | (x >> 16)) & 0x1f00000000ffffULL;
  x = (x | (x >> 32)) & 0x7FFFFULL;
  return static_cast<uint32_t>(x);
}

inline uint64_t morton3_encode(uint32_t ix, uint32_t iy, uint32_t iz) noexcept {
  return split_by_3(ix) | (split_by_3(iy) << 1) | (split_by_3(iz) << 2);
}

inline void morton3_decode(uint64_t code, uint32_t& ix, uint32_t& iy, uint32_t& iz) noexcept {
  ix = compact_by_3(code);
  iy = compact_by_3(code >> 1);
  iz = compact_by_3(code >> 2);
}

inline uint64_t make_leaf_key(uint64_t morton, uint8_t level) noexcept {
  return (morton << 5) | static_cast<uint64_t>(level & 31u);
}

inline uint64_t leaf_morton(uint64_t key) noexcept {
  return key >> 5;
}

inline uint8_t leaf_level(uint64_t key) noexcept {
  return static_cast<uint8_t>(key & 31u);
}

inline bool leaf_less(const Leaf& a, const Leaf& b) noexcept {
  if (a.root != b.root) return a.root < b.root;
  return a.key < b.key;
}

inline bool key_pair_less(uint32_t ra, uint64_t ka, uint32_t rb, uint64_t kb) noexcept {
  if (ra != rb) return ra < rb;
  return ka < kb;
}

inline uint32_t make_regular_face_meta(uint8_t axis_dir, uint8_t level, uint32_t flags = 0u) noexcept {
  return (static_cast<uint32_t>(axis_dir) & 7u) |
         ((static_cast<uint32_t>(level) & 31u) << 3) |
         ((flags & 0xFFFFFFu) << 8);
}

inline uint8_t popcount8(uint8_t x) noexcept {
  static constexpr uint8_t kPop4[16] = {0, 1, 1, 2, 1, 2, 2, 3, 1, 2, 2, 3, 2, 3, 3, 4};
  return static_cast<uint8_t>(kPop4[x & 15u] + kPop4[(x >> 4) & 15u]);
}

namespace detail {
inline std::vector<double> build_graded_axis(
    double d_min, double b_min, double b_max, double d_max, double h_core) {
  double c_min = std::max(d_min, b_min - 1.5 * h_core), c_max = std::min(d_max, b_max + 1.5 * h_core);
  if (c_min - d_min < 1.2 * h_core) c_min = d_min;
  if (d_max - c_max < 1.2 * h_core) c_max = d_max;
  if (c_max <= c_min) { c_min = d_min; c_max = d_max; }
  const int64_t n_core = std::max<int64_t>(1, static_cast<int64_t>(std::llround((c_max - c_min) / h_core)));
  const double dx = (c_max - c_min) / static_cast<double>(n_core), max_h = 16.0 * dx;
  auto slab = [&](double len) {
    std::vector<double> st;
    if (len <= 1e-12 * h_core) return st;
    for (double cur = dx, sum = 0.0; sum < len;) {
      const double nxt = std::min(max_h, cur * 1.25);
      if (!st.empty() && sum + 0.5 * nxt >= len) break;
      st.push_back(nxt); sum += nxt; cur = nxt;
    }
    double tot = 0.0; for (double v : st) tot += v;
    for (double& s : st) s *= (len / tot);
    return st;
  };
  const auto left = slab(c_min - d_min), right = slab(d_max - c_max);
  std::vector<double> coords;
  coords.reserve(left.size() + static_cast<size_t>(n_core) + right.size() + 1);
  if (!left.empty()) {
    double pos = d_min; coords.push_back(d_min);
    for (size_t i = left.size(); i > 1; --i) coords.push_back(pos += left[i - 1]);
  }
  coords.push_back(c_min);
  for (int64_t i = 1; i < n_core; ++i) coords.push_back(c_min + static_cast<double>(i) * dx);
  coords.push_back(c_max);
  if (!right.empty()) {
    double pos = c_max;
    for (size_t i = 0; i + 1 < right.size(); ++i) coords.push_back(pos += right[i]);
    coords.push_back(d_max);
  }
  return coords;
}
}  // namespace detail

class LinearMortonForest {
 public:
  LinearMortonForest() = default;

  int32_t build(
      const double* surf_points,
      int64_t n_points,
      const int32_t* surf_faces,
      int64_t n_faces,
      double h0,
      int32_t max_depth,
      const double* farfield_bbox,
      MortonBuildStats* out_stats = nullptr
  );

  void build_sample_index();
  void parallel_sort_leaves();

  int64_t find_leaf(uint32_t root, uint64_t key) const noexcept;
  int64_t find_leaf_at_point(const Vec3& p) const noexcept;
  int64_t find_leaf_for_voxel(int64_t vx, int64_t vy, int64_t vz) const noexcept;
  bool verify_2to1_balance() const;

  const std::vector<Leaf>& leaves() const noexcept { return leaves_; }
  const std::vector<RegularFace>& regular_faces() const noexcept { return regular_faces_; }
  const std::vector<GeneralFace>& general_faces() const noexcept { return general_faces_; }
  const std::vector<Vec3>& vertex_coords() const noexcept { return vertex_coords_; }
  const std::vector<int32_t>& leaf_cell_id() const noexcept { return leaf_cell_id_; }
  const std::vector<int32_t>& leaf_vertex_offset() const noexcept { return leaf_vertex_offset_; }
  uint64_t peak_bytes() const noexcept { return peak_bytes_; }

 private:
  static constexpr size_t kNumSamples = 4096;

  Vec3 origin_shift_{0, 0, 0};
  Vec3 dom_min_{0, 0, 0};
  Vec3 dom_max_{0, 0, 0};
  std::vector<double> x_coords_;
  std::vector<double> y_coords_;
  std::vector<double> z_coords_;
  int64_t nx_ = 0;
  int64_t ny_ = 0;
  int64_t nz_ = 0;
  uint32_t n_root_ = 0;
  int32_t effective_max_depth_ = 0;

  std::vector<Leaf> leaves_;
  std::vector<MortonSampleKey> sample_keys_;
  std::vector<size_t> sample_offsets_;
  std::vector<int32_t> leaf_cell_id_;
  std::vector<int32_t> leaf_vertex_offset_;
  std::vector<Vec3> vertex_coords_;
  std::vector<RegularFace> regular_faces_;
  std::vector<GeneralFace> general_faces_;
  uint64_t peak_bytes_ = 0;
  size_t sdf_bytes_ = 0;

  void update_peak_bytes(size_t extra_scratch_bytes = 0) noexcept;
  int32_t subdivide_marked(const uint8_t* split_mask);
  int32_t subdivide_marked_atomic(const std::atomic<uint8_t>* split_mask);
  Vec3 leaf_center(const Leaf& lf) const noexcept;
  Vec3 leaf_size(const Leaf& lf) const noexcept;
  void leaf_lattice_origin(const Leaf& lf, int64_t& ox, int64_t& oy, int64_t& oz, int64_t& sz) const noexcept;
  void find_corner_owner(int64_t ci, int64_t cj, int64_t ck,
                         int64_t& out_best_idx, uint8_t& out_best_corner, bool& out_touches_active) const noexcept;
  int32_t lookup_corner_vertex(int64_t ci, int64_t cj, int64_t ck) const noexcept;
  int32_t assign_vertices_and_extract_faces(int64_t n_cells);
};

}  // namespace cfd::octree
