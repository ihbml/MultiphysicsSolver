#pragma once

#include <cstdint>
#include <vector>
#include <array>
#include <memory>
#include <string>
#include "difvm_mesh.hpp"
#include "cfd_mesher_native.h"
#include "sdf_engine.hpp"

namespace cfd {

struct MosaicFace {
  std::vector<int64_t> nodes;
  int64_t owner{-1};
  int64_t neighbour{-1};
  Vec3 normal_owner{0, 0, 0};
  Vec3 centre{0, 0, 0};
  std::string patch_name{"internal"};
  bool is_farfield{false};
  bool is_cut_face{false};
  bool is_folded{false};
};

struct OctreeOptions {
  double h0 = 0.05;
  double h_min = 0.005;
  double h_max = 0.2;
  double grading = 1.2;
  double alpha_curv = 0.2;
  double beta_prox = 0.25;
  int32_t max_depth = 4;
  int32_t domain_mode = 0; // 0=external_fluid, 1=internal_fluid, 2=solid_only, 3=cht_coupled
  double small_cell_ratio = 0.20;
  double wsrd_alpha_target = 0.50;
  double snap_tol_factor = 0.25;
  double amips_max = 1e4;
  bool snap_cut_cells = true;
  bool merge_small_cells = true;
  bool enable_rcm = true;
  int32_t num_bl_layers = 0;
  double bl_first_height = 0.0;
  double bl_growth_rate = 1.2;
  bool bl_pinching_enabled = true;
  double bl_pinching_angle_deg = 40.0;
  bool enable_cht = false;
  bool enable_cyclic = false;
  double cyclic_angle_rad = 0.0;
  int32_t distmesh_iters = 0;
};

class OctreeHexcore {
 public:
  OctreeHexcore() = default;

  int32_t generate(
      NativeMeshData& mesh,
      const double* surf_points,
      int64_t n_points,
      const int32_t* surf_faces,
      int64_t n_faces,
      const double* farfield_bbox,
      const OctreeOptions& options
  );
};

}  // namespace cfd
