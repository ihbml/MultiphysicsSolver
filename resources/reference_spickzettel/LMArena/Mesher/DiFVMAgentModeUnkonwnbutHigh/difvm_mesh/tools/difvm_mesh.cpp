// difvm_mesh — command line front-end of the DiFVM mesher.
#include <cmath>
#include <cstdio>
#include <cstring>
#include <map>
#include <string>
#include <vector>

#include "difvm/generate.hpp"
#include "difvm/graph_mesh.hpp"
#include "difvm/io.hpp"
#include "difvm/reorder.hpp"

using namespace difvm;

namespace {

struct Args {
  std::vector<std::string> pos;
  std::map<std::string, std::string> opt;

  bool has(const std::string& k) const { return opt.count(k) > 0; }
  std::string get(const std::string& k, const std::string& def = "") const {
    auto it = opt.find(k);
    return it == opt.end() ? def : it->second;
  }
  double num(const std::string& k, double def) const {
    auto it = opt.find(k);
    return it == opt.end() ? def : std::atof(it->second.c_str());
  }
  int integer(const std::string& k, int def) const {
    auto it = opt.find(k);
    return it == opt.end() ? def : std::atoi(it->second.c_str());
  }
};

Args parse_args(int argc, char** argv, int from) {
  Args a;
  for (int i = from; i < argc; ++i) {
    const std::string s = argv[i];
    if (s.rfind("--", 0) == 0) {
      const std::size_t eq = s.find('=');
      if (eq != std::string::npos) {
        a.opt[s.substr(2, eq - 2)] = s.substr(eq + 1);
      } else if (i + 1 < argc && std::strncmp(argv[i + 1], "--", 2) != 0 &&
                 !(a.pos.empty() && std::string(argv[i + 1]).size() && std::string(argv[1]) == "convert" && i == 1)) {
        a.opt[s.substr(2)] = argv[++i];
      } else {
        a.opt[s.substr(2)] = "1";
      }
    } else if (s == "-o" && i + 1 < argc) {
      a.opt["o"] = argv[++i];
    } else {
      a.pos.push_back(s);
    }
  }
  return a;
}

void usage() {
  std::printf(
      "difvm_mesh — DiFVM static graph-FVM mesher\n\n"
      "USAGE\n"
      "  difvm_mesh info     <mesh> [--full] [--hist]\n"
      "  difvm_mesh convert  <mesh> -o out.difvmmesh [options]\n"
      "  difvm_mesh validate <mesh> [--strict]\n"
      "  difvm_mesh gen box      -o <case|file.difvmmesh> [--nx 8 --ny 8 --nz 8 --lo '-1 -1 -1' --hi '1 1 1']\n"
      "  difvm_mesh gen cutcell  --stl geom.stl --h 0.05 -o out.difvmmesh\n"
      "  difvm_mesh bench        [--n 96] [--threads N]\n\n"
      "  <mesh> = OpenFOAM case dir | .vtk | .vtu | .difvmmesh\n\n"
      "CONVERT OPTIONS\n"
      "  --reorder rcm|morton|hilbert|none   cell renumbering for locality (default rcm)\n"
      "  --vtu FILE            write XML .vtu with MeshErrorMap cell fields\n"
      "  --vtk-surface FILE    write boundary skin (POLYDATA)\n"
      "  --vtk-points FILE     write cell centres + quality fields\n"
      "  --of DIR              write an OpenFOAM case (drop-in for cross-validation)\n"
      "  --patch-box           (re)build boundary patches from the bounding box\n"
      "  --no-weld             do not weld coincident points/faces\n"
      "  --theta-crit DEG      MeshErrorMap non-orthogonality guard (default 65)\n"
      "  --theta-width DEG     tanh transition width (default 8)\n"
      "  --skew-sigma0 S       skewness centre of the beta blend (default 0.5)\n"
      "  --omega-min V         floor of the cell shield (default 0.05)\n"
      "  --threads N           worker threads (default: all cores)\n"
      "  --quiet / --verbose\n"
      "  --bench               print compile-stage timings\n");
}

GraphMesh load_any(const std::string& path, const BuildOptions& bo) {
  const MeshFormat fmt = detect_format(path);
  if (fmt == MeshFormat::kBinary) return read_binary(path);
  PolyMeshRaw raw = load_polymesh(path, fmt);
  log_verbose(raw.summary());
  GraphMesh gm = build_graph_mesh(raw, bo);
  gm.source = path;
  return gm;
}

void stage_timings(const GraphMesh& gm, double t_build, const std::string& path) {
  std::printf("  compile: %.1f ms -> %.2f Mfaces/s, %.2f Mcells/s input rate\n", t_build * 1e3,
              (gm.n_faces / std::max(t_build, 1e-9)) / 1e6, (gm.n_cells / std::max(t_build, 1e-9)) / 1e6);
  (void)path;
}

int cmd_info(const Args& a) {
  if (a.pos.empty()) { usage(); return 2; }
  BuildOptions bo;
  bo.threads = a.integer("threads", 0);
  Timer t;
  GraphMesh gm = load_any(a.pos[0], bo);
  const double tb = t.seconds();
  MeshAudit au = audit_mesh(gm, bo.threads);
  std::printf("%s\n", gm.source.c_str());
  std::printf("  cells=%lld  faces=%lld (internal=%lld boundary=%lld)  points=%zu  patches=%zu\n",
              static_cast<long long>(gm.n_cells), static_cast<long long>(gm.n_faces),
              static_cast<long long>(gm.n_internal), static_cast<long long>(gm.n_boundary),
              gm.points.size(), gm.patches.size());
  for (const auto& p : gm.patches) {
    std::printf("    patch %-16s type=%-10s faces=%d start=%d\n", p.name.c_str(), p.type.c_str(), p.size, p.start);
  }
  if (detect_format(a.pos[0]) != MeshFormat::kBinary) stage_timings(gm, tb, a.pos[0]);
  std::printf("  memory: %.2f MiB (%.1f B/cell)\n", gm.memory_bytes() / 1048576.0,
              gm.n_cells ? static_cast<double>(gm.memory_bytes()) / static_cast<double>(gm.n_cells) : 0.0);
  std::printf("%s\n", au.to_string(a.has("full")).c_str());
  if (a.has("hist")) std::printf("%s", quality_histogram(gm).c_str());
  return au.ok() ? 0 : 1;
}

int cmd_validate(const Args& a) {
  if (a.pos.empty()) { usage(); return 2; }
  BuildOptions bo;
  bo.threads = a.integer("threads", 0);
  GraphMesh gm = load_any(a.pos[0], bo);
  MeshAudit au = audit_mesh(gm, bo.threads);
  std::printf("%s\n", au.to_string(true).c_str());
  gidx bad = 0;
  // per-face sanity
  for (gidx f = 0; f < gm.n_faces; ++f) {
    if (!(gm.magSf[static_cast<std::size_t>(f)] > 0.0) || !std::isfinite(gm.magSf[static_cast<std::size_t>(f)])) ++bad;
  }
  if (bad) std::printf("  %lld faces with non-positive/non-finite area\n", static_cast<long long>(bad));
  const bool ok = au.ok() && bad == 0;
  std::printf("VERDICT: %s\n", ok ? "PASS" : "FAIL");
  return ok ? 0 : 1;
}

int cmd_convert(const Args& a) {
  if (a.pos.empty() || !a.has("o")) { usage(); return 2; }
  BuildOptions bo;
  bo.threads = a.integer("threads", 0);
  if (a.has("no-weld")) bo.merge_duplicate_points = false;
  bo.err.theta_crit_deg = a.num("theta-crit", bo.err.theta_crit_deg);
  bo.err.theta_width_deg = a.num("theta-width", bo.err.theta_width_deg);
  bo.err.skew_sigma0 = a.num("skew-sigma0", bo.err.skew_sigma0);
  bo.err.omega_min = a.num("omega-min", bo.err.omega_min);

  Timer t;
  GraphMesh gm = load_any(a.pos[0], bo);
  const double t_build = t.seconds();
  if (a.has("patch-box")) {
    MeshAudit au = audit_mesh(gm, bo.threads);
    auto_patch_boundary(gm, au.lo, au.hi);
  }
  ReorderReport rr;
  {
    const ReorderMethod m = parse_reorder(a.get("reorder", "rcm"));
    rr = reorder_mesh(gm, m);
  }
  MeshAudit au = audit_mesh(gm, bo.threads);
  std::printf("%s\n", au.to_string(false).c_str());
  std::printf("  %s\n", rr.to_string().c_str());
  if (a.has("bench")) stage_timings(gm, t_build, a.pos[0]);
  write_binary(a.get("o"), gm);
  if (a.has("vtu")) write_vtu(a.get("vtu"), gm);
  if (a.has("vtk-surface")) write_vtk_surface(a.get("vtk-surface"), gm);
  if (a.has("vtk-points")) write_vtk_points(a.get("vtk-points"), gm);
  if (a.has("of")) write_openfoam_case(a.get("of"), gm);
  return au.ok() ? 0 : 1;
}

std::vector<double> parse_triple(const std::string& s, const Vec3& def) {
  double v[3] = {def.x, def.y, def.z};
  if (!s.empty()) {
    if (std::sscanf(s.c_str(), "%lf,%lf,%lf", &v[0], &v[1], &v[2]) != 3 &&
        std::sscanf(s.c_str(), "%lf %lf %lf", &v[0], &v[1], &v[2]) != 3) {
      fail("cannot parse vector '" + s + "' (use 'x,y,z' or 'x y z')");
    }
  }
  return {v[0], v[1], v[2]};
}

int cmd_gen(const Args& a) {
  if (a.pos.empty()) { usage(); return 2; }
  const std::string what = a.pos[0];
  BuildOptions bo;
  bo.assume_oriented = true;   // both generators emit outward-oriented faces
  bo.threads = a.integer("threads", 0);
  if (a.has("no-weld")) bo.merge_duplicate_points = false;

  GraphMesh gm;
  if (what == "box") {
    BoxGridSpec spec;
    const auto lo = parse_triple(a.num("lo", 0) != 0 ? a.get("lo") : a.get("lo", ""), spec.lo);
    const auto hi = parse_triple(a.get("hi", ""), spec.hi);
    spec.lo = Vec3{lo[0], lo[1], lo[2]};
    spec.hi = Vec3{hi[0], hi[1], hi[2]};
    spec.nx = a.integer("nx", 8);
    spec.ny = a.integer("ny", spec.nx);
    spec.nz = a.integer("nz", spec.nx);
    spec.grading = a.num("grading", 1.0);
    if (a.has("names")) {
      // --names inlet,outlet,walls,walls,walls,walls
      spec.patch_names.clear();
      std::string s = a.get("names");
      std::size_t p = 0;
      while (p <= s.size()) {
        const std::size_t q = s.find(',', p);
        spec.patch_names.push_back(s.substr(p, (q == std::string::npos ? s.size() : q) - p));
        if (q == std::string::npos) break;
        p = q + 1;
      }
    }
    Timer t;
    PolyMeshRaw raw = make_box_grid(spec);
    gm = build_graph_mesh(raw, bo);
    gm.source = "box:" + std::to_string(spec.nx) + "x" + std::to_string(spec.ny) + "x" + std::to_string(spec.nz);
    log_info("generated box grid in " + std::to_string(t.seconds() * 1e3) + " ms");
  } else if (what == "cutcell") {
    if (!a.has("stl")) fail("gen cutcell requires --stl FILE");
    TriangleSoup soup = read_stl(a.get("stl"));
    CutCellOptions co;
    co.h = a.num("h", 0.05);
    co.pad_cells = a.num("pad", 1.0);
    co.min_volume_fraction = a.num("min-vol-frac", 1e-3);
    CutCellStats st;
    PolyMeshRaw raw = make_cutcell_mesh(soup, co, &st);
    log_info(st.to_string());
    gm = build_graph_mesh(raw, bo);
    auto_patch_boundary(gm, soup.lo - Vec3{co.h, co.h, co.h}, soup.hi + Vec3{co.h, co.h, co.h});
    gm.source = "cutcell:" + a.get("stl");
  } else {
    fail("unknown generator '" + what + "' (box|cutcell)");
  }

  ReorderReport rr = reorder_mesh(gm, parse_reorder(a.get("reorder", "rcm")));
  MeshAudit au = audit_mesh(gm, bo.threads);
  std::printf("%s\n", au.to_string(false).c_str());
  std::printf("  %s\n", rr.to_string().c_str());
  if (a.has("o")) write_binary(a.get("o"), gm);
  if (a.has("vtu")) write_vtu(a.get("vtu"), gm);
  if (a.has("of")) write_openfoam_case(a.get("of"), gm);
  if (a.has("vtk-surface")) write_vtk_surface(a.get("vtk-surface"), gm);
  return au.ok() ? 0 : 1;
}

int cmd_bench(const Args& a) {
  const int n = a.integer("n", 96);
  const int threads = a.integer("threads", 0);
  BuildOptions bo;
  bo.threads = threads;
  if (threads > 0) set_num_threads(threads);
  std::printf("DiFVM mesher benchmark — %dx%dx%d box (%lld cells)\n", n, n, n,
              static_cast<long long>(n) * n * n);
  std::printf("threads: %d\n\n", num_threads());

  BoxGridSpec spec;
  spec.lo = Vec3{0, 0, 0};
  spec.hi = Vec3{1, 1, 1};
  spec.nx = spec.ny = spec.nz = n;

  Timer t;
  PolyMeshRaw raw = make_box_grid(spec);
  const double t_gen = t.seconds();
  t.reset();
  GraphMesh gm = build_graph_mesh(raw, bo);
  const double t_build = t.seconds();
  t.reset();
  MeshAudit au = audit_mesh(gm, threads);
  const double t_audit = t.seconds();
  t.reset();
  const std::string tmp = "/tmp/difvm_bench.difvmmesh";
  write_binary(tmp, gm);
  const double t_write = t.seconds();
  t.reset();
  GraphMesh gm2 = read_binary(tmp);
  const double t_read = t.seconds();
  std::remove(tmp.c_str());

  const double nf = static_cast<double>(gm.n_faces), nc = static_cast<double>(gm.n_cells);
  auto row = [](const char* name, double secs, double work, double unit_scale) {
    std::printf("  %-22s %8.1f ms   %9.2f M%s/s\n", name, secs * 1e3, (work / std::max(secs, 1e-12)) / unit_scale,
                work == 0 ? "" : "");
  };
  row("generate grid", t_gen, nc, 1e6);
  row("compile graph", t_build, nc, 1e6);
  row("audit", t_audit, nc + nf, 1e6);
  row("write binary", t_write, nc, 1e6);
  row("read binary", t_read, nc, 1e6);
  std::printf("\n  compile rate:   %.2f Mcells/s   %.2f Mfaces/s\n", (nc / t_build) / 1e6, (nf / t_build) / 1e6);
  std::printf("  binary I/O:     %.1f MiB in %.1f ms (%.0f MiB/s)\n", gm.memory_bytes() / 1048576.0, t_write * 1e3,
              (gm.memory_bytes() / 1048576.0) / std::max(t_write, 1e-12));
  std::printf("  footprint:      %.1f MiB (%.0f B/cell, %.0f B/face)\n", gm.memory_bytes() / 1048576.0,
              gm.memory_bytes() / nc, gm.memory_bytes() / nf);
  std::printf("  audit:          closure=%.2e  alpha_max=%.2f deg\n", au.max_closure_residual, au.max_non_ortho_deg);
  (void)gm2;
  return 0;
}

}  // namespace

int main(int argc, char** argv) {
  if (argc < 2) { usage(); return 2; }
  const std::string cmd = argv[1];
  try {
    if (cmd == "-h" || cmd == "--help" || cmd == "help") { usage(); return 0; }
    if (argc > 2 && std::string(argv[2]) == "--verbose") log_level() = LogLevel::kVerbose;
    if (cmd == "info") { Args a = parse_args(argc, argv, 2); if (a.has("verbose")) log_level() = LogLevel::kVerbose; return cmd_info(a); }
    if (cmd == "convert") return cmd_convert(parse_args(argc, argv, 2));
    if (cmd == "validate") return cmd_validate(parse_args(argc, argv, 2));
    if (cmd == "gen") return cmd_gen(parse_args(argc, argv, 2));
    if (cmd == "bench") return cmd_bench(parse_args(argc, argv, 2));
    usage();
    return 2;
  } catch (const std::exception& e) {
    std::fprintf(stderr, "error: %s\n", e.what());
    return 1;
  }
}
