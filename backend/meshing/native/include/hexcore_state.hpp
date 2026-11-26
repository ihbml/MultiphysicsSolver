#pragma once

#include <array>
#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include "octree_hexcore.hpp"
#include "polygon_geometry.hpp"

namespace cfd::hexcore {

struct OctreeNode {
  Vec3 bmin;
  Vec3 bmax;
  int64_t first_child = -1;
  int64_t cell_index = -1;
  int32_t level = 0;
  int16_t region = 0;
  uint8_t is_leaf = 1;
  uint8_t is_active = 0;

  inline Vec3 center() const noexcept { return (bmin + bmax) * 0.5; }
  inline Vec3 size() const noexcept { return bmax - bmin; }
  inline double v_bg() const noexcept {
    const Vec3 s = bmax - bmin;
    return s.x * s.y * s.z;
  }
};
static_assert(sizeof(OctreeNode) <= 80, "OctreeNode must remain compact (<= 80 bytes)");

class ChunkedNodePool {
 public:
  static constexpr size_t kChunkShift = 16;  // 65,536 nodes/chunk (~4.7 MB)
  static constexpr size_t kChunkSize = size_t{1} << kChunkShift;
  static constexpr size_t kChunkMask = kChunkSize - 1;

  size_t size() const noexcept { return size_; }
  bool empty() const noexcept { return size_ == 0; }

  void resize(size_t new_size) {
    const size_t needed_chunks = (new_size == 0) ? 0 : (((new_size - 1) >> kChunkShift) + 1);
    while (chunks_.size() < needed_chunks) {
      chunks_.push_back(std::make_unique<OctreeNode[]>(kChunkSize));
    }
    size_ = new_size;
  }

  OctreeNode& operator[](size_t idx) noexcept {
    return chunks_[idx >> kChunkShift][idx & kChunkMask];
  }

  const OctreeNode& operator[](size_t idx) const noexcept {
    return chunks_[idx >> kChunkShift][idx & kChunkMask];
  }

 private:
  std::vector<std::unique_ptr<OctreeNode[]>> chunks_;
  size_t size_ = 0;
};

struct RawQuad {
  std::array<Vec3, 4> pts;
  int64_t owner = -1;
  int64_t neighbour = -1;
  std::string patch_name;
  bool is_farfield = false;
  bool is_cut_face = false;
};

struct HexcoreState {
  OctreeOptions options;
  std::unique_ptr<MeshSDF> sdf;

  Vec3 origin_shift{0, 0, 0};
  std::vector<double> shifted_surf_points;
  Vec3 s_min{0, 0, 0};
  Vec3 s_max{0, 0, 0};
  Vec3 dom_min{0, 0, 0};
  Vec3 dom_max{0, 0, 0};
  bool is_closed_surface = false;
  bool is_multi_shell_closed = false;
  double surf_signed_vol = 0.0;

  std::vector<double> x_coords;
  std::vector<double> y_coords;
  std::vector<double> z_coords;
  int64_t nx = 0;
  int64_t ny = 0;
  int64_t nz = 0;
  size_t n_root = 0;
  int32_t effective_max_depth = 0;
  double eps_probe = 0.0;
  double quant_tol = 0.0;
  double dom_tol = 0.0;
  size_t max_octree_nodes = 0;
  int max_threads = 1;

  ChunkedNodePool nodes;
  std::vector<int64_t> all_leaves;
  std::vector<int64_t> active_leaves;
  std::vector<Vec3> active_grad_c;
  int64_t n_active_cells = 0;
  std::vector<CellType> cell_types;

  std::unordered_map<QuantizedKey, int64_t, QuantizedKeyHash> vmap;
  std::vector<Vec3> node_coords;
  std::vector<std::array<int64_t, 8>> hex_corners;
  std::vector<MosaicFace> mosaic_faces;
  std::vector<std::vector<int64_t>> cell_to_faces;

  std::vector<Vec3> cell_approx_center;
  std::vector<double> cell_vols;
  std::vector<Vec3> cell_cents;
  std::vector<std::vector<int64_t>> cell_verts;
  std::vector<int64_t> old_to_new_cell;
  std::vector<int64_t> new_to_old_active;
  int64_t final_n_cells = 0;

  std::vector<MosaicFace> final_internal_faces;
  std::vector<MosaicFace> final_boundary_faces;
  std::vector<MosaicFace> all_faces;
  int64_t n_internal = 0;
  int64_t total_faces = 0;

  int64_t findLeafAtPoint(const Vec3& p) const;
  bool subdivideBatch(const std::vector<int64_t>& split_ids, std::vector<int64_t>& out_children);
};

size_t getMaxOctreeNodes();

std::vector<double> buildGradedAxisCoords(
    double d_min,
    double b_min,
    double b_max,
    double d_max,
    double h_core,
    double growth_ratio,
    double max_stretch
);

std::vector<int64_t> computeFastRCM(int64_t n_cells, const std::vector<MosaicFace>& internal_faces);

int32_t passSetupAndRootGrid(
    HexcoreState& st,
    const double* surf_points,
    int64_t n_points,
    const int32_t* surf_faces,
    int64_t n_faces,
    const double* farfield_bbox
);

int32_t passRefineAndBalance(HexcoreState& st);

int32_t passClassifyLeaves(
    HexcoreState& st,
    const double* surf_points,
    int64_t n_points,
    const int32_t* surf_faces,
    int64_t n_faces
);

int32_t passExtractFacesAndVertices(HexcoreState& st);
int32_t passSnapWallVertices(HexcoreState& st);
int32_t passComputeGeometryAndSplitWarped(HexcoreState& st);
int32_t passMergeSmallCells(HexcoreState& st);
int32_t passDeduplicateAndRcm(HexcoreState& st);
int32_t passAssembleOutput(HexcoreState& st, NativeMeshData& mesh);

}  // namespace cfd::hexcore
