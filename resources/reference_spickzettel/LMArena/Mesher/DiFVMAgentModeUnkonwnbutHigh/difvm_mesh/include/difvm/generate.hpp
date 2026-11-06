// DiFVM Mesher — mesh sources: structured box grid and STL cut-cell mesher.
#pragma once

#include <string>
#include <vector>

#include "difvm/polymesh.hpp"

namespace difvm {

// ---------------------------------------------------------------------------
// Structured hexahedral box (blockMesh equivalent, patch names configurable).
// ---------------------------------------------------------------------------
struct BoxGridSpec {
  Vec3 lo{-1.0, -1.0, -1.0};
  Vec3 hi{1.0, 1.0, 1.0};
  int nx = 8, ny = 8, nz = 8;
  // optional grading: cell size grows geometrically towards hi by this factor
  double grading = 1.0;
  // patch names for the six sides, in order: xmin xmax ymin ymax zmin zmax
  std::vector<std::string> patch_names{"xmin", "xmax", "ymin", "ymax", "zmin", "zmax"};
  std::vector<std::string> patch_types = std::vector<std::string>(6, "patch");
};
PolyMeshRaw make_box_grid(const BoxGridSpec& spec);

// ---------------------------------------------------------------------------
// STL ingestion (binary + ASCII, normals re-derived from winding)
// ---------------------------------------------------------------------------
struct TriangleSoup {
  std::vector<Vec3> verts;     // 3 per triangle
  std::vector<Vec3> normals;   // 1 per triangle (unit, outward)
  Vec3 lo{}, hi{};
  double area() const;
  std::size_t n_tris() const { return verts.size() / 3; }
  void finalise();             // orient normals outward, fix bbox
};
TriangleSoup read_stl(const std::string& path);
void write_stl(const std::string& path, const TriangleSoup& soup);   // binary STL

// ---------------------------------------------------------------------------
// Cartesian cut-cell mesher: cells intersected by the surface are clipped to
// the fluid side (convex half-space clipping), everything else stays a hex.
// ---------------------------------------------------------------------------
struct CutCellOptions {
  double h = 0.05;                  // target cell size
  double pad_cells = 1.0;           // fluid padding around the bbox
  double min_volume_fraction = 1e-3; // drop slivers below this fraction of h^3
  double point_quantum = 1e-6;      // vertex welding tolerance, relative to h
  bool report = true;
};

struct CutCellStats {
  int nx = 0, ny = 0, nz = 0;
  long n_cut_cells = 0;
  long n_solid_cells = 0;
  long n_dropped_slivers = 0;
  long n_triangles = 0;
  double grid_volume = 0.0;
  double fluid_volume = 0.0;
  double surface_area = 0.0;
  double exact_volume = 0.0;    // divergence-theorem volume of the closed STL
  double time_ms = 0.0;
  std::string to_string() const;
};

// Returns a raw mesh whose interior interfaces are coincident per-cell faces
// (they are welded into internal faces by build_graph_mesh).
PolyMeshRaw make_cutcell_mesh(const TriangleSoup& soup, const CutCellOptions& opts, CutCellStats* stats = nullptr);

}  // namespace difvm
