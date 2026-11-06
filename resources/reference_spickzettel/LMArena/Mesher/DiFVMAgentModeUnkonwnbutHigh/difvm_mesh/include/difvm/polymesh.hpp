// DiFVM Mesher — raw (file-level) polyhedral topology.
//
// This is the neutral hand-over format between mesh *sources* (OpenFOAM
// polyMesh, VTK, octree cut-cell generator) and the graph-FVM compiler that
// produces the static SoA arrays consumed by the solver (graph_mesh.hpp).
#pragma once

#include <string>
#include <vector>

#include "difvm/core.hpp"

namespace difvm {

// Boundary-condition role, inferred from the OpenFOAM patch type / generator.
enum class PatchRole : std::uint8_t {
  kInternal = 0,
  kWall,
  kInlet,
  kOutlet,
  kSymmetry,
  kEmpty,     // 2D empty patch
  kWedge,     // 2D axisymmetric
  kInterface, // fluid-solid cut-cell interface / other
  kUnknown
};

inline const char* to_string(PatchRole r) {
  switch (r) {
    case PatchRole::kWall: return "wall";
    case PatchRole::kInlet: return "inlet";
    case PatchRole::kOutlet: return "outlet";
    case PatchRole::kSymmetry: return "symmetry";
    case PatchRole::kEmpty: return "empty";
    case PatchRole::kWedge: return "wedge";
    case PatchRole::kInterface: return "interface";
    case PatchRole::kUnknown: return "unknown";
    case PatchRole::kInternal: break;
  }
  return "internal";
}

struct PatchInfo {
  std::string name = "patch";
  std::string type = "patch";       // OpenFOAM type string (wall, patch, empty, ...)
  PatchRole role = PatchRole::kUnknown;
  idx start = 0;                    // first face (index into face arrays)
  idx size = 0;                     // number of faces
  idx end() const { return start + size; }
};

// Raw polyhedral mesh: faces stored in CSR (like OpenFOAM `faces`), with the
// OpenFOAM convention that faces [0, n_internal) are internal (owner only
// lower-indexed cell guaranteed, neighbour = second cell) and the remaining
// faces are boundary faces of the patches listed in `patches` (contiguous).
struct PolyMeshRaw {
  std::vector<Vec3> points;          // vertex coordinates
  std::vector<gidx> face_offsets;    // size n_faces + 1
  std::vector<idx> face_verts;       // concatenated point indices
  std::vector<idx> owner;            // size n_faces
  std::vector<idx> neighbour;        // size n_internal_faces (empty for VTK-derived)
  std::vector<PatchInfo> patches;    // covers faces [n_internal_faces, n_faces)
  gidx n_cells = 0;

  idx n_faces() const { return static_cast<idx>(owner.size()); }
  idx n_internal() const { return static_cast<idx>(neighbour.size()); }
  idx n_points() const { return static_cast<idx>(points.size()); }
  idx n_face_verts(idx f) const {
    return static_cast<idx>(face_offsets[static_cast<std::size_t>(f) + 1] - face_offsets[static_cast<std::size_t>(f)]);
  }
  const idx* face_begin(idx f) const { return face_verts.data() + face_offsets[static_cast<std::size_t>(f)]; }

  void clear() {
    points.clear();
    face_offsets.clear();
    face_verts.clear();
    owner.clear();
    neighbour.clear();
    patches.clear();
    n_cells = 0;
  }

  void add_face(const idx* verts, int n, idx own, idx nb, idx patch_face_index) {
    (void)patch_face_index;
    face_offsets.push_back(static_cast<gidx>(face_verts.size()));
    for (int i = 0; i < n; ++i) face_verts.push_back(verts[i]);
    owner.push_back(own);
    if (nb >= 0 && neighbour.size() < owner.size()) neighbour.push_back(nb);
  }

  // Finalise CSR offsets after all faces were pushed face-by-face.
  void finalize_offsets() {
    if (face_offsets.empty() || face_offsets.front() != 0) {
      face_offsets.insert(face_offsets.begin(), 0);
    }
    while (face_offsets.size() < owner.size() + 1) {
      face_offsets.push_back(static_cast<gidx>(face_verts.size()));
    }
  }

  // Number of points actually referenced by faces (unused points are legal in
  // OpenFOAM meshes and are removed by the compiler when requested).
  std::vector<char> used_points() const;

  // Bounding box of used points.
  void bounds(Vec3& lo, Vec3& hi) const;

  // Cheap structural validation (index ranges, face sizes, patch coverage).
  void validate() const;

  std::string summary() const;
};

// Infer the BC role from an OpenFOAM patch type string.
PatchRole infer_patch_role(const std::string& name, const std::string& type);

}  // namespace difvm
