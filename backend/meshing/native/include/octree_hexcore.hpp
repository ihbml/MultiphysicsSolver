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

struct OctreeOptions {
  double h0 = 0.05;
  double h_min = 0.005;
  double h_max = 0.2;
  int32_t max_depth = 4;
  int32_t domain_mode = 0; // 0=external_fluid, 1=internal_fluid, 2=solid_only, 3=cht_coupled
  double small_cell_ratio = 0.20;
  double wsrd_alpha_target = 0.50;
  double snap_tol_factor = 0.25;
  double amips_max = 1e4;
  bool snap_cut_cells = true;
  bool merge_small_cells = true;
  bool enable_rcm = true;
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
