#include "hexcore_state.hpp"

namespace cfd {

int32_t OctreeHexcore::generate(
    NativeMeshData& mesh,
    const double* surf_points,
    int64_t n_points,
    const int32_t* surf_faces,
    int64_t n_faces,
    const double* farfield_bbox,
    const OctreeOptions& options
) {
  if (!surf_points || !surf_faces || n_points <= 0 || n_faces <= 0 || options.h0 <= 0.0) {
    return -1;
  }

  mesh.clear();

  hexcore::HexcoreState st;
  st.options = options;

  int32_t rc = hexcore::passSetupAndRootGrid(
      st, surf_points, n_points, surf_faces, n_faces, farfield_bbox
  );
  if (rc != 0) return rc;

  rc = hexcore::passRefineAndBalance(st);
  if (rc != 0) return rc;

  rc = hexcore::passClassifyLeaves(st, surf_points, n_points, surf_faces, n_faces);
  if (rc != 0) return rc;

  rc = hexcore::passExtractFacesAndVertices(st);
  if (rc != 0) return rc;

  rc = hexcore::passSnapWallVertices(st);
  if (rc != 0) return rc;

  rc = hexcore::passComputeGeometryAndSplitWarped(st);
  if (rc != 0) return rc;

  rc = hexcore::passMergeSmallCells(st);
  if (rc != 0) return rc;

  rc = hexcore::passDeduplicateAndRcm(st);
  if (rc != 0) return rc;

  rc = hexcore::passAssembleOutput(st, mesh);
  if (rc != 0) return rc;

  return 0;
}

}  // namespace cfd
