// DiFVM Mesher — mesh I/O interfaces.
#pragma once

#include <string>

#include "difvm/graph_mesh.hpp"
#include "difvm/polymesh.hpp"

namespace difvm {

// ---- OpenFOAM ------------------------------------------------------------
// Reads <case>/constant/polyMesh/{points,faces,owner,neighbour,boundary}.
PolyMeshRaw read_openfoam_case(const std::string& case_dir);
// Writes a mesh into <case_dir>/constant/polyMesh (blockMesh-compatible).
void write_openfoam_case(const std::string& case_dir, const GraphMesh& gm);

// ---- VTK -----------------------------------------------------------------
// Reads legacy ASCII .vtk (UNSTRUCTURED_GRID) and XML ASCII .vtu.
PolyMeshRaw read_vtk(const std::string& path);
// Writes XML .vtu (ASCII, inline) with polyhedron cells + quality fields.
void write_vtu(const std::string& path, const GraphMesh& gm);
// Writes a legacy ASCII .vtk surface skin (boundary faces only) — cheap visual
// check of patches in any viewer.
void write_vtk_surface(const std::string& path, const GraphMesh& gm);
// Writes a legacy ASCII .vtk of cell centres as a point cloud carrying the
// MeshErrorMap fields (theta/beta/omega). Robust for every cell type.
void write_vtk_points(const std::string& path, const GraphMesh& gm);

// ---- Native binary (mmap-friendly, load into the solver in one read) ------
void write_binary(const std::string& path, const GraphMesh& gm);
GraphMesh read_binary(const std::string& path);

// Format detection by extension / directory layout.
enum class MeshFormat { kOpenFOAMCase, kVtkLegacy, kVtu, kBinary, kUnknown };
MeshFormat detect_format(const std::string& path);
PolyMeshRaw load_polymesh(const std::string& path, MeshFormat fmt = MeshFormat::kUnknown);

}  // namespace difvm
