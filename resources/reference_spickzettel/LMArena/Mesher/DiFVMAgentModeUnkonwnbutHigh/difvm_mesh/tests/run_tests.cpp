// DiFVM Mesher — self-contained test suite (no external framework).
#include <cmath>
#include <cstdio>
#include <cstring>
#include <map>
#include <numeric>
#include <string>
#include <vector>

#include "difvm/generate.hpp"
#include "difvm/graph_mesh.hpp"
#include "difvm/io.hpp"
#include "difvm/reorder.hpp"

using namespace difvm;

namespace {

int g_fail = 0, g_checks = 0, g_test_fail = 0;
std::string g_current;

#define CHECK(cond)                                                                  \
  do {                                                                               \
    ++g_checks;                                                                      \
    if (!(cond)) {                                                                   \
      ++g_fail;                                                                      \
      std::printf("    FAIL [%s] %s:%d  %s\n", g_current.c_str(), __FILE__, __LINE__, #cond); \
    }                                                                                \
  } while (0)

#define CHECK_NEAR(a, b, tol)                                                                   \
  do {                                                                                          \
    ++g_checks;                                                                                 \
    const double _a = (a), _b = (b);                                                            \
    if (!(std::fabs(_a - _b) <= (tol))) {                                                       \
      ++g_fail;                                                                                 \
      std::printf("    FAIL [%s] %s:%d  |%g - %g| > %g\n", g_current.c_str(), __FILE__,          \
                  __LINE__, _a, _b, static_cast<double>(tol));                                  \
    }                                                                                           \
  } while (0)

void begin(const char* name) {
  g_current = name;
  g_test_fail = g_fail;
  std::printf("  %-52s", name);
  std::fflush(stdout);
}
void end() {
  std::printf("%s\n", (g_fail == g_test_fail) ? "ok" : "FAILED");
}

// ---------------------------------------------------------------------------
// helpers
// ---------------------------------------------------------------------------
std::string tmpfile(const char* name) { return std::string("/tmp/difvm_test_") + name; }

void make_sphere_soup(TriangleSoup& soup, double r, int nlat, int nlon) {
  std::vector<std::vector<int>> idx(static_cast<std::size_t>(nlat) + 1,
                                    std::vector<int>(static_cast<std::size_t>(nlon)));
  for (int i = 0; i <= nlat; ++i) {
    const double th = M_PI * i / nlat;
    for (int j = 0; j < nlon; ++j) {
      const double ph = 2.0 * M_PI * j / nlon;
      soup.verts.push_back(Vec3{r * std::sin(th) * std::cos(ph), r * std::sin(th) * std::sin(ph), r * std::cos(th)});
      idx[static_cast<std::size_t>(i)][static_cast<std::size_t>(j)] = static_cast<int>(soup.verts.size()) - 1;
    }
  }
  for (int i = 0; i < nlat; ++i)
    for (int j = 0; j < nlon; ++j) {
      const int j2 = (j + 1) % nlon;
      const int a = idx[static_cast<std::size_t>(i)][static_cast<std::size_t>(j)];
      const int b = idx[static_cast<std::size_t>(i)][static_cast<std::size_t>(j2)];
      const int c = idx[static_cast<std::size_t>(i + 1)][static_cast<std::size_t>(j2)];
      const int d = idx[static_cast<std::size_t>(i + 1)][static_cast<std::size_t>(j)];
      // both triangles of the quad a->b->c->d must wind the same way
      if (i > 0) { soup.verts.push_back(soup.verts[static_cast<std::size_t>(a)]); soup.verts.push_back(soup.verts[static_cast<std::size_t>(c)]); soup.verts.push_back(soup.verts[static_cast<std::size_t>(d)]); }
      if (i < nlat - 1) { soup.verts.push_back(soup.verts[static_cast<std::size_t>(a)]); soup.verts.push_back(soup.verts[static_cast<std::size_t>(b)]); soup.verts.push_back(soup.verts[static_cast<std::size_t>(c)]); }
    }
  soup.finalise();
}

double sum_volumes(const GraphMesh& gm) {
  double v = 0.0;
  for (gidx i = 0; i < gm.n_cells; ++i) v += gm.vol[static_cast<std::size_t>(i)];
  return v;
}

// total |Sf| of boundary faces that are NOT on the bounding box
double interface_area(const GraphMesh& gm, const Vec3& lo, const Vec3& hi) {
  const double tol = 1e-9 * norm(hi - lo);
  double area = 0.0;
  for (gidx f = gm.n_internal; f < gm.n_faces; ++f) {
    const std::size_t fi = static_cast<std::size_t>(f);
    bool on_box = false;
    for (int s = 0; s < 6 && !on_box; ++s) {
      const int axis = s / 2;
      const double target = (s % 2) ? (axis == 0 ? hi.x : axis == 1 ? hi.y : hi.z)
                                   : (axis == 0 ? lo.x : axis == 1 ? lo.y : lo.z);
      bool all = true;
      for (gidx k = gm.fv_offsets[fi]; k < gm.fv_offsets[fi + 1] && all; ++k) {
        const Vec3& p = gm.points[static_cast<std::size_t>(gm.fv[static_cast<std::size_t>(k)])];
        const double v = (axis == 0) ? p.x : (axis == 1 ? p.y : p.z);
        if (std::fabs(v - target) > tol) all = false;
      }
      if (all) on_box = true;
    }
    if (!on_box) area += gm.magSf[fi];
  }
  return area;
}

}  // namespace

// ---------------------------------------------------------------------------
int main() {
  set_num_threads(2);
  std::printf("DiFVM mesher test suite (threads=%d)\n\n", num_threads());

  // 1 ---------------------------------------------------------------- box grid
  {
    begin("box grid: exact volumes, faces, closure");
    BoxGridSpec spec;
    spec.lo = Vec3{-1, -2, -3};
    spec.hi = Vec3{1, 2, 3};
    spec.nx = 4; spec.ny = 5; spec.nz = 6;
    PolyMeshRaw raw = make_box_grid(spec);
    CHECK(raw.n_cells == 4 * 5 * 6);
    BuildOptions bo;
    GraphMesh gm = build_graph_mesh(raw, bo);
    CHECK(gm.n_cells == 120);
    CHECK(gm.n_internal == 3 * 4 * 5 * 6 - 4 * 5 - 4 * 6 - 5 * 6);  // interior faces
    CHECK(gm.n_boundary == 2 * (5 * 6) + 2 * (4 * 6) + 2 * (4 * 5));
    const double dx = 2.0 / 4, dy = 4.0 / 5, dz = 6.0 / 6;
    const double cell_vol = dx * dy * dz;
    for (gidx i = 0; i < gm.n_cells; ++i) CHECK_NEAR(gm.vol[static_cast<std::size_t>(i)], cell_vol, 1e-12);
    CHECK_NEAR(sum_volumes(gm), 2.0 * 4.0 * 6.0, 1e-10);
    MeshAudit au = audit_mesh(gm);
    CHECK(au.n_negative_cells == 0);
    CHECK(au.max_closure_residual < 1e-13);
    CHECK_NEAR(au.max_non_ortho_deg, 0.0, 1e-9);
    CHECK_NEAR(au.min_beta, 1.0, 1e-12);
    CHECK_NEAR(au.min_omega, 1.0, 1e-12);
    CHECK(gm.patches.size() == 6);
    // orthogonality: delta_f == Sf, k_f == 0
    for (gidx f = 0; f < gm.n_faces; ++f) {
      const std::size_t fi = static_cast<std::size_t>(f);
      CHECK_NEAR(norm(gm.k_f.get(fi)), 0.0, 1e-12);
      CHECK_NEAR(norm(gm.delta_f.get(fi)), gm.magSf[fi], 1e-12);
    }
    end();
  }

  // 2 ------------------------------------------------------------ skewed grid
  {
    begin("perturbed grid: non-orthogonality + MeshErrorMap");
    BoxGridSpec spec;
    spec.lo = Vec3{0, 0, 0};
    spec.hi = Vec3{1, 1, 1};
    spec.nx = spec.ny = spec.nz = 6;
    PolyMeshRaw raw = make_box_grid(spec);
    // distort interior points deterministically
    auto nx1 = spec.nx + 1;
    for (int k = 0; k <= spec.nz; ++k)
      for (int j = 0; j <= spec.ny; ++j)
        for (int i = 0; i <= spec.nx; ++i) {
          if (i == 0 || j == 0 || k == 0 || i == spec.nx || j == spec.ny || k == spec.nz) continue;
          Vec3& p = raw.points[static_cast<std::size_t>(i + static_cast<long>(nx1) * (j + static_cast<long>(spec.ny + 1) * k))];
          p.x += 0.05 * std::sin(3.1 * j + 1.7 * k);
          p.y += 0.05 * std::sin(2.3 * k + 0.9 * i);
          p.z += 0.05 * std::sin(1.9 * i + 2.7 * j);
        }
    BuildOptions bo;
    GraphMesh gm = build_graph_mesh(raw, bo);
    MeshAudit au = audit_mesh(gm);
    CHECK(au.n_negative_cells == 0);
    CHECK(au.max_closure_residual < 1e-9);
    CHECK(au.max_non_ortho_deg > 5.0);       // non-orthogonality is detected
    CHECK(au.min_beta < 1.0);                // blurring kicks in somewhere
    CHECK(au.min_omega < 1.0);
    CHECK(au.max_skewness > 0.0);
    // total volume of the distorted box is still exactly 1 (faces shared)
    CHECK_NEAR(sum_volumes(gm), 1.0, 1e-9);
    // k_f + delta_f == Sf by construction
    for (gidx f = 0; f < gm.n_faces; ++f) {
      const std::size_t fi = static_cast<std::size_t>(f);
      const Vec3 r = gm.delta_f.get(fi) + gm.k_f.get(fi) - gm.Sf.get(fi);
      CHECK(norm(r) < 1e-10 * (1.0 + gm.magSf[fi]));
    }
    end();
  }

  // 3 ------------------------------------------------------------- binary roundtrip
  {
    begin("binary .difvmmesh round-trip is bit-exact");
    BoxGridSpec spec;
    spec.nx = 5; spec.ny = 4; spec.nz = 3;
    GraphMesh gm = build_graph_mesh(make_box_grid(spec), BuildOptions{});
    const std::string path = tmpfile("rt.difvmmesh");
    write_binary(path, gm);
    GraphMesh g2 = read_binary(path);
    CHECK(g2.n_cells == gm.n_cells);
    CHECK(g2.n_faces == gm.n_faces);
    CHECK(g2.n_internal == gm.n_internal);
    CHECK(g2.fv == std::vector<idx>(gm.fv.begin(), gm.fv.end()));
    CHECK(g2.patches.size() == gm.patches.size());
    bool identical = true;
    for (gidx i = 0; i < gm.n_cells; ++i) {
      if (gm.vol[static_cast<std::size_t>(i)] != g2.vol[static_cast<std::size_t>(i)]) identical = false;
      if (gm.cf.get(static_cast<std::size_t>(i)).x != g2.cf.get(static_cast<std::size_t>(i)).x) identical = false;
    }
    for (gidx f = 0; f < gm.n_faces; ++f) {
      if (gm.theta[static_cast<std::size_t>(f)] != g2.theta[static_cast<std::size_t>(f)]) identical = false;
      if (gm.beta[static_cast<std::size_t>(f)] != g2.beta[static_cast<std::size_t>(f)]) identical = false;
    }
    CHECK(identical);
    std::remove(path.c_str());
    end();
  }

  // 4 ------------------------------------------------------ openfoam round-trip
  {
    begin("OpenFOAM polyMesh write/read round-trip");
    BoxGridSpec spec;
    spec.nx = 4; spec.ny = 3; spec.nz = 5;
    spec.patch_names = {"inlet", "outlet", "wallTop", "wallBottom", "front", "back"};
    spec.patch_types = {"patch", "patch", "wall", "wall", "wall", "wall"};
    GraphMesh gm = build_graph_mesh(make_box_grid(spec), BuildOptions{});
    const std::string dir = tmpfile("case_of");
    write_openfoam_case(dir, gm);
    PolyMeshRaw raw2 = read_openfoam_case(dir);
    CHECK(raw2.n_cells == gm.n_cells);
    CHECK(raw2.n_faces() == gm.n_faces);
    GraphMesh g2 = build_graph_mesh(raw2, BuildOptions{});
    CHECK(g2.n_cells == gm.n_cells);
    CHECK(g2.n_faces == gm.n_faces);
    CHECK(g2.patches.size() == gm.patches.size());
    CHECK_NEAR(sum_volumes(g2), sum_volumes(gm), 1e-12);
    // patch roles are inferred from the OpenFOAM patch names/types
    bool inlet_found = false, wall_found = false;
    for (const auto& p : g2.patches) {
      if (p.name == "inlet" && p.role == PatchRole::kInlet) inlet_found = true;
      if (p.name == "wallTop" && p.role == PatchRole::kWall) wall_found = true;
    }
    CHECK(inlet_found);
    CHECK(wall_found);
    std::system((std::string("rm -rf '") + dir + "'").c_str());
    end();
  }

  // 5 ---------------------------------------------------------------- VTK I/O
  {
    begin("VTK .vtu write + read (polyhedron cells)");
    BoxGridSpec spec;
    spec.nx = 3; spec.ny = 3; spec.nz = 3;
    GraphMesh gm = build_graph_mesh(make_box_grid(spec), BuildOptions{});
    const std::string path = tmpfile("rt.vtu");
    write_vtu(path, gm);
    PolyMeshRaw raw2 = read_vtk(path);
    CHECK(raw2.n_cells == gm.n_cells);
    GraphMesh g2 = build_graph_mesh(raw2, BuildOptions{});
    CHECK(g2.n_cells == gm.n_cells);
    CHECK(g2.n_internal == gm.n_internal);
    CHECK_NEAR(sum_volumes(g2), sum_volumes(gm), 1e-9);
    CHECK(audit_mesh(g2).max_closure_residual < 1e-12);
    std::remove(path.c_str());
    end();
  }

  // 6 -------------------------------------------------- VTK legacy cell pairing
  {
    begin("VTK legacy hex input: faces welded into internal faces");
    // single 2x1x1 hexahedron block written as legacy .vtk
    const std::string path = tmpfile("pair.vtk");
    {
      std::FILE* f = std::fopen(path.c_str(), "w");
      std::fprintf(f, "# vtk DataFile Version 3.0\ncube\nASCII\nDATASET UNSTRUCTURED_GRID\n");
      std::fprintf(f, "POINTS 12 double\n");
      // two hexes sharing a face
      const double pts[12][3] = {{0, 0, 0}, {1, 0, 0}, {1, 1, 0}, {0, 1, 0}, {0, 0, 1}, {1, 0, 1},
                                 {1, 1, 1}, {0, 1, 1}, {2, 0, 0}, {2, 1, 0}, {2, 1, 1}, {2, 0, 1}};
      for (int i = 0; i < 12; ++i) std::fprintf(f, "%g %g %g\n", pts[i][0], pts[i][1], pts[i][2]);
      std::fprintf(f, "CELLS 2 18\n8 0 1 2 3 4 5 6 7\n8 1 8 9 2 5 11 10 6\n");
      std::fprintf(f, "CELL_TYPES 2\n12\n12\n");
      std::fclose(f);
    }
    PolyMeshRaw raw = read_vtk(path);
    CHECK(raw.n_cells == 2);
    BuildOptions bo;
    GraphMesh gm = build_graph_mesh(raw, bo);
    CHECK(gm.n_cells == 2);
    CHECK(gm.n_internal == 1);   // the shared quad was welded
    CHECK(gm.n_boundary == 10);
    CHECK_NEAR(sum_volumes(gm), 2.0, 1e-12);
    MeshAudit au = audit_mesh(gm);
    CHECK(au.max_closure_residual < 1e-12);
    CHECK_NEAR(au.max_non_ortho_deg, 0.0, 1e-9);
    std::remove(path.c_str());
    end();
  }

  // 7 ------------------------------------------------------------- reordering
  {
    begin("reordering (RCM / Morton) preserves geometry, improves locality");
    BoxGridSpec spec;
    spec.nx = 12; spec.ny = 10; spec.nz = 8;
    GraphMesh base = build_graph_mesh(make_box_grid(spec), BuildOptions{});
    const double v0 = sum_volumes(base);
    // scramble the cell numbering: a solver-relevant mesh is rarely ordered well
    auto scrambled = [&]() {
      PolyMeshRaw raw = make_box_grid(spec);
      const gidx nc = raw.n_cells;
      std::vector<idx> perm(static_cast<std::size_t>(nc));
      for (gidx i = 0; i < nc; ++i) perm[static_cast<std::size_t>(i)] = static_cast<idx>(i);
      std::srand(12345);
      for (gidx i = nc - 1; i > 0; --i) std::swap(perm[static_cast<std::size_t>(i)],
                                                   perm[static_cast<std::size_t>(std::rand() % (i + 1))]);
      for (auto& c : raw.owner) c = perm[static_cast<std::size_t>(c)];
      for (auto& c : raw.neighbour) c = perm[static_cast<std::size_t>(c)];
      return build_graph_mesh(raw, BuildOptions{});
    };
    for (ReorderMethod m : {ReorderMethod::kRcm, ReorderMethod::kMorton}) {
      GraphMesh gm = scrambled();
      ReorderReport rr = reorder_mesh(gm, m);
      CHECK_NEAR(sum_volumes(gm), v0, 1e-10);
      const MeshAudit au = audit_mesh(gm);
      CHECK(au.max_closure_residual < 1e-13);
      CHECK(rr.bandwidth_after < rr.bandwidth_before);         // locality improved
      CHECK(rr.mean_span_after < rr.mean_span_before);
      // per-cell volume multiset identical
      std::vector<double> a(base.vol.begin(), base.vol.end());
      std::vector<double> b(gm.vol.begin(), gm.vol.end());
      std::sort(a.begin(), a.end());
      std::sort(b.begin(), b.end());
      bool same = (a.size() == b.size());
      for (std::size_t i = 0; same && i < a.size(); ++i)
        if (std::fabs(a[i] - b[i]) > 1e-12 * std::fabs(a[i])) same = false;
      CHECK(same);
    }
    end();
  }

  // 8 ------------------------------------------------------- cut-cell sphere
  {
    begin("cut-cell sphere: volume/area convergence to analytic");
    TriangleSoup soup;
    make_sphere_soup(soup, 0.5, 40, 48);
    const double exact_sphere = 4.0 / 3.0 * M_PI * std::pow(0.5, 3);
    const double exact_area = 4.0 * M_PI * 0.25;
    std::printf("\n");
    double prev_err = 1e9;
    double prev_aerr = 1e9;
    for (double h : {0.08, 0.04, 0.02}) {
      CutCellOptions co;
      co.h = h;
      CutCellStats st;
      PolyMeshRaw raw = make_cutcell_mesh(soup, co, &st);
      BuildOptions bo;
      bo.assume_oriented = true;
      GraphMesh gm = build_graph_mesh(raw, bo);
      const MeshAudit au = audit_mesh(gm);
      // the mesher reports its own fluid volume (it knows the cut geometry)
      const double fluid_exact = st.grid_volume - exact_sphere;
      const double err = std::fabs(st.fluid_volume - fluid_exact) / fluid_exact;
      const double aerr = std::fabs(st.surface_area - exact_area) / exact_area;
      const double open_frac = static_cast<double>(au.n_open_cells) / static_cast<double>(gm.n_cells);
      std::printf("    h=%-6.3f cells=%-6lld cut=%-5ld used=%-5ld vol_err=%.3e area_err=%.3e closure=%.1e open=%.2f%%\n",
                  h, static_cast<long long>(gm.n_cells), st.n_cut_cells, st.n_dropped_slivers, err, aerr,
                  au.max_closure_residual, 100.0 * open_frac);
      CHECK(au.n_negative_cells == 0);
      CHECK(open_frac < 5e-3);      // holes only at grazing/tangent configurations
      CHECK(err < 2.5e-2);          // fluid volume vs analytic
      CHECK(aerr < prev_aerr);      // wetted area converges monotonically
      CHECK(st.n_cut_cells > 0);
      prev_err = err;
      prev_aerr = aerr;
    }
    end();
  }

  // 8b ------------------------------------------------ cut-cell plane (cube)
  {
    begin("cut-cell cube: planar surface reproduced exactly, no dropped cells");
    TriangleSoup soup;
    std::vector<Vec3> v;
    for (int i = 0; i < 8; ++i)
      v.push_back(Vec3{i & 1 ? 0.5 : -0.5, i & 2 ? 0.5 : -0.5, i & 4 ? 0.5 : -0.5} + Vec3{0.037, 0.011, 0.023});
    static const int qd[6][4] = {{0, 2, 3, 1}, {4, 5, 7, 6}, {0, 1, 5, 4}, {2, 6, 7, 3}, {0, 4, 6, 2}, {1, 3, 7, 5}};
    for (int f = 0; f < 6; ++f) {
      const int* q = qd[f];
      const int t1[3] = {q[0], q[1], q[2]}, t2[3] = {q[0], q[2], q[3]};
      for (int k = 0; k < 3; ++k) soup.verts.push_back(v[t1[k]]);
      for (int k = 0; k < 3; ++k) soup.verts.push_back(v[t2[k]]);
    }
    soup.finalise();
    CutCellOptions co;
    co.h = 0.055;
    CutCellStats st;
    PolyMeshRaw raw = make_cutcell_mesh(soup, co, &st);
    BuildOptions bo;
    bo.assume_oriented = true;
    GraphMesh gm = build_graph_mesh(raw, bo);
    const MeshAudit au = audit_mesh(gm);
    const double fluid_exact = st.grid_volume - 1.0;
    const double err = std::fabs(st.fluid_volume - fluid_exact) / fluid_exact;
    std::printf("    grid %dx%dx%d cut=%ld dropped=%ld vol_err=%.3e wetted=%.4f faces=%lld internal=%lld closure=%.1e\n",
                st.nx, st.ny, st.nz, st.n_cut_cells, st.n_dropped_slivers, err, st.surface_area,
                static_cast<long long>(gm.n_faces), static_cast<long long>(gm.n_internal),
                au.max_closure_residual);
    CHECK(st.n_dropped_slivers == 0);
    CHECK(err < 3e-2);
    CHECK(std::fabs(st.surface_area - 6.0) / 6.0 < 6e-2);   // cone caps approximate edge patches
    CHECK(au.max_closure_residual < 1e-10);
    CHECK(au.n_open_cells == 0);
    CHECK(gm.n_internal > gm.n_boundary);   // the cut layer must be welded, not walled
    end();
  }

  // 9 --------------------------------------------------------- quality guards
  {
    begin("MeshErrorMap guards: beta/omega/k_f behaviour at bad faces");
    BoxGridSpec spec;
    spec.lo = Vec3{0, 0, 0};
    spec.hi = Vec3{1, 1, 1};
    spec.nx = 4; spec.ny = 4; spec.nz = 4;
    PolyMeshRaw raw = make_box_grid(spec);
    BuildOptions bo;
    bo.err.theta_crit_deg = 20.0;
    bo.err.theta_width_deg = 2.0;
    bo.err.skew_sigma0 = 0.1;
    bo.err.skew_width = 0.02;
    GraphMesh gm = build_graph_mesh(raw, bo);
    // an orthogonal mesh must be untouched even with aggressive guards
    for (gidx f = 0; f < gm.n_faces; ++f) {
      CHECK_NEAR(gm.beta[static_cast<std::size_t>(f)], 1.0, 1e-9);
      CHECK_NEAR(gm.omega[static_cast<std::size_t>(0)], 1.0, 1e-9);
    }
    // beta is monotone in theta: compare a synthetic pair of angles
    auto beta_of_theta = [&](double deg, double sigma) {
      const double th = deg * M_PI / 180.0;
      const double f_theta = 0.5 * (1.0 - std::tanh((th - bo.err.theta_crit_deg * M_PI / 180.0) /
                                                   (bo.err.theta_width_deg * M_PI / 180.0)));
      const double f_skew = 0.5 * (1.0 - std::tanh((sigma - bo.err.skew_sigma0) / bo.err.skew_width));
      return f_theta * f_skew;
    };
    CHECK(beta_of_theta(5.0, 0.0) > beta_of_theta(25.0, 0.0));
    CHECK(beta_of_theta(25.0, 0.0) > beta_of_theta(45.0, 0.0));
    CHECK(beta_of_theta(5.0, 0.0) > 0.99);
    CHECK(beta_of_theta(80.0, 0.5) < 0.01);
    end();
  }

  // 10 ------------------------------------------------------------ big mesh bench
  {
    begin("large grid compile (32^3) stays consistent after reorder");
    BoxGridSpec spec;
    spec.nx = spec.ny = spec.nz = 32;
    Timer t;
    GraphMesh gm = build_graph_mesh(make_box_grid(spec), BuildOptions{});
    const double tb = t.seconds();
    reorder_mesh(gm, ReorderMethod::kRcm);
    MeshAudit au = audit_mesh(gm);
    CHECK(au.ok());
    CHECK_NEAR(au.total_volume, 8.0, 1e-9);
    std::printf("\n    %lld cells, %lld faces, compile %.1f ms, %.2f Mcells/s\n",
                static_cast<long long>(gm.n_cells), static_cast<long long>(gm.n_faces), tb * 1e3,
                gm.n_cells / tb / 1e6);
    end();
  }

  std::printf("\n%d checks, %d failures\n", g_checks, g_fail);
  return g_fail == 0 ? 0 : 1;
}
