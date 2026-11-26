#include "hexcore_state.hpp"

namespace cfd {

namespace hexcore {

void rebuildCellToFacesCSR(HexcoreState& st) {
  const int64_t n_cells = st.n_active_cells;
  const int64_t n_faces = st.faces.size();
  st.cell_face_offsets.assign(static_cast<size_t>(n_cells) + 1, 0);

  for (int64_t f = 0; f < n_faces; ++f) {
    const int64_t o = st.faces.owner[static_cast<size_t>(f)];
    const int64_t nb = st.faces.neighbour[static_cast<size_t>(f)];
    if (o >= 0 && o < n_cells) {
      ++st.cell_face_offsets[static_cast<size_t>(o) + 1];
    }
    if (nb >= 0 && nb < n_cells) {
      ++st.cell_face_offsets[static_cast<size_t>(nb) + 1];
    }
  }

  for (int64_t c = 0; c < n_cells; ++c) {
    st.cell_face_offsets[static_cast<size_t>(c + 1)] += st.cell_face_offsets[static_cast<size_t>(c)];
  }

  st.cell_face_ids.resize(static_cast<size_t>(st.cell_face_offsets[static_cast<size_t>(n_cells)]));
  std::vector<int64_t> cursor(
      st.cell_face_offsets.begin(),
      st.cell_face_offsets.begin() + static_cast<size_t>(n_cells)
  );

  for (int64_t f = 0; f < n_faces; ++f) {
    const int64_t o = st.faces.owner[static_cast<size_t>(f)];
    const int64_t nb = st.faces.neighbour[static_cast<size_t>(f)];
    if (o >= 0 && o < n_cells) {
      st.cell_face_ids[static_cast<size_t>(cursor[static_cast<size_t>(o)]++)] = f;
    }
    if (nb >= 0 && nb < n_cells) {
      st.cell_face_ids[static_cast<size_t>(cursor[static_cast<size_t>(nb)]++)] = f;
    }
  }
}

}  // namespace hexcore

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
