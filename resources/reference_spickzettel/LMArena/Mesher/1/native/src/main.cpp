// CLI front-end of the native mesher core.
//
//   mesher run   --spec <file|-> [--out DIR] [--threads N] [--stdout]
//   mesher bench [--out FILE] [--threads N]
//   mesher version
//
// `run` reads a JSON job description, executes the full pipeline
// (contour -> unstructured FVM mesh + DiFVM graph -> Eulerian SPH particles
//  with the [W1] boundary storage -> [W2] adaptive coupling operators),
// writes the requested artifacts into the output directory and drops a
// manifest.json next to them.
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>

#include "core.hpp"

#ifdef _OPENMP
#include <omp.h>
#endif

namespace fs = std::filesystem;
using namespace mesher;

namespace {

std::string engineString() {
  std::ostringstream os;
  os << "mesher-core 1.0.0 (C++20";
#ifdef _OPENMP
  os << ", OpenMP " << _OPENMP;
#endif
  os << ", " << __VERSION__ << ")";
  return os.str();
}

std::string slurp(const std::string& path) {
  if (path == "-") {
    std::ostringstream os;
    os << std::cin.rdbuf();
    return os.str();
  }
  std::ifstream is(path);
  if (!is) throw std::runtime_error("cannot read spec file: " + path);
  std::ostringstream os;
  os << is.rdbuf();
  return os.str();
}

Real contourSpacing(const Spec& spec) {
  const FvmSpec& f = spec.fvm;
  const Real diag = norm(spec.domain.size());
  Real csp = sizeFieldMin(f, spec.geom);
  if (f.blCount > 0 && f.blFirst > 0) csp = std::min(csp, f.blFirst * 1.5);
  csp *= 0.5;
  return std::max(csp, diag / 4000.0);
}

Result runPipeline(Spec spec) {
  Result r;
  const auto t0 = std::chrono::steady_clock::now();
  int threads = spec.threads;
#ifdef _OPENMP
  if (threads <= 0) threads = omp_get_max_threads();
  omp_set_num_threads(threads);
  r.threadsUsed = omp_get_max_threads();
#else
  if (threads <= 0) threads = 1;
  r.threadsUsed = 1;
#endif
  r.spec = spec;
  r.engine = engineString();

  ContourLoops contours;
  {
    ScopedTimer st(r.timer, "contourExtraction");
    contours = extractContours(spec.geom, spec.domain, contourSpacing(spec));
  }
  r.notes.push_back("contour loops: " + std::to_string(contours.loops.size()) + " (area " +
                    std::to_string(contours.area) + ", perimeter " +
                    std::to_string(contours.perimeter) + ")");
  r.mesh = buildFvmMesh(r.spec, contours, r.timer, r.warnings);
  r.parts = buildSphParticles(r.spec, contours, r.timer, r.warnings);
  r.sphBndAreaFull = r.parts.bndAreaSum;
  r.sphVolumeFull = r.parts.sphRegionArea;
  r.coupling = buildCoupling(r.spec, r.mesh, r.parts, r.timer, r.warnings);
  // consistency must reflect the final (adaptively trimmed) particle set
  if (r.coupling.valid && r.parts.valid) {
    computeConsistency(r.spec, r.parts, r.timer);
    finalizeParticleStats(r.spec, r.parts);
    r.coupling.nCoreParticles = r.parts.nCore;
    r.coupling.nBufferParticles = r.parts.nBuffer;
  }
  {
    ScopedTimer st(r.timer, "io");
    writeArtifacts(r);
  }
  r.totalMs = std::chrono::duration<Real, std::milli>(std::chrono::steady_clock::now() - t0).count();
  return r;
}

int cmdRun(int argc, char** argv) {
  std::string specPath, outDir;
  bool toStdout = false;
  int threads = 0;
  for (int i = 2; i < argc; ++i) {
    const std::string a = argv[i];
    if (a == "--spec" && i + 1 < argc) specPath = argv[++i];
    else if (a == "--out" && i + 1 < argc) outDir = argv[++i];
    else if (a == "--threads" && i + 1 < argc) threads = std::atoi(argv[++i]);
    else if (a == "--stdout") toStdout = true;
  }
  if (specPath.empty()) {
    std::cerr << "{\"ok\":false,\"error\":\"--spec is required\"}\n";
    return 2;
  }
  mj::Value root = mj::parse(slurp(specPath));
  Spec spec = parseSpec(root);
  if (!outDir.empty()) spec.out.dir = outDir;
  if (threads > 0) spec.threads = threads;
  if (spec.out.dir.empty() || spec.out.dir == ".") spec.out.dir = "out";

  Result r = runPipeline(std::move(spec));
  const std::string manifestPath = r.spec.out.dir + "/manifest.json";
  writeManifest(r, manifestPath);
  if (toStdout) {
    std::ifstream is(manifestPath);
    std::ostringstream os;
    os << is.rdbuf();
    std::cout << os.str();
  } else {
    mj::Writer w;
    w.objBegin();
    w.kv("ok", true);
    w.kv("manifest", manifestPath);
    w.kv("cells", (long long)r.mesh.cells.size());
    w.kv("particles", (long long)r.parts.pos.size());
    w.kv("totalMs", r.totalMs);
    w.objEnd();
    std::cout << w.str() << "\n";
  }
  return 0;
}

// Small built-in scaling benchmark used by the /api/benchmark endpoint.
int cmdBench(int argc, char** argv) {
  std::string outPath;
  int threads = 0;
  for (int i = 2; i < argc; ++i) {
    const std::string a = argv[i];
    if (a == "--out" && i + 1 < argc) outPath = argv[++i];
    else if (a == "--threads" && i + 1 < argc) threads = std::atoi(argv[++i]);
  }
  // Build the benchmark specs structurally (no string concatenation).
  std::vector<std::string> specs;
  auto cavitySpec = [](int n) {
    const Real h = 1.0 / n;
    mj::Writer w;
    w.objBegin();
    w.kv("name", "bench-cavity-" + std::to_string(n));
    w.key("geometry"); w.objBegin();
    w.kv("kind", "box");
    w.key("min"); w.arrBegin(); w.number(0); w.number(0); w.arrEnd();
    w.key("max"); w.arrBegin(); w.number(1); w.number(1); w.arrEnd();
    w.objEnd();
    w.key("fvm"); w.objBegin();
    w.kv("baseCellSize", h);
    w.kv("smoothingIters", 10LL);
    w.key("patchRules"); w.arrBegin();
    w.objBegin(); w.kv("name", "movingWall"); w.kv("bc", "moving"); w.kv("where", "ymin");
    w.kv("priority", 5LL); w.objEnd();
    w.objBegin(); w.kv("name", "wall"); w.kv("bc", "wall"); w.kv("where", "all");
    w.kv("priority", 0LL); w.objEnd();
    w.arrEnd();
    w.objEnd();
    w.key("sph"); w.objBegin();
    w.kv("particleSpacing", h);
    w.key("relaxation"); w.objBegin(); w.kv("iterations", 60LL); w.objEnd();
    w.objEnd();
    w.objEnd();
    return w.str();
  };
  auto cylinderSpec = [](int n) {
    const Real h = 1.0 / n;
    mj::Writer w;
    w.objBegin();
    w.kv("name", "bench-cylinder-" + std::to_string(n));
    w.key("geometry"); w.objBegin();
    w.kv("kind", "difference");
    w.key("a"); w.objBegin(); w.kv("kind", "box");
    w.key("min"); w.arrBegin(); w.number(0); w.number(0); w.arrEnd();
    w.key("max"); w.arrBegin(); w.number(2.2); w.number(0.41); w.arrEnd(); w.objEnd();
    w.key("b"); w.objBegin(); w.kv("kind", "circle");
    w.key("center"); w.arrBegin(); w.number(0.2); w.number(0.2); w.arrEnd();
    w.kv("radius", 0.05); w.objEnd();
    w.objEnd();
    w.key("fvm"); w.objBegin();
    w.kv("baseCellSize", h);
    w.kv("smoothingIters", 10LL);
    w.key("boundaryLayer"); w.objBegin();
    w.kv("count", 3LL); w.kv("firstHeight", h * 0.35); w.kv("growth", 1.25);
    w.kv("patch", "body"); w.objEnd();
    w.key("refinement"); w.objBegin(); w.key("targets"); w.arrBegin();
    w.objBegin(); w.kv("shape", "circle");
    w.key("center"); w.arrBegin(); w.number(0.2); w.number(0.2); w.arrEnd();
    w.kv("radius", 0.05); w.kv("hNear", h * 0.5); w.kv("width", 0.25); w.objEnd();
    w.arrEnd(); w.objEnd();
    w.key("patchRules"); w.arrBegin();
    w.objBegin(); w.kv("name", "inlet"); w.kv("bc", "inflow"); w.kv("where", "xmin");
    w.kv("priority", 9LL); w.objEnd();
    w.objBegin(); w.kv("name", "outlet"); w.kv("bc", "outflow"); w.kv("where", "xmax");
    w.kv("priority", 9LL); w.objEnd();
    w.objBegin(); w.kv("name", "body"); w.kv("bc", "wall"); w.kv("where", "hole");
    w.kv("priority", 8LL); w.objEnd();
    w.objBegin(); w.kv("name", "wall"); w.kv("bc", "wall"); w.kv("where", "all");
    w.kv("priority", 0LL); w.objEnd();
    w.arrEnd();
    w.objEnd();
    w.key("sph"); w.objBegin();
    w.kv("particleSpacing", h);
    w.key("relaxation"); w.objBegin(); w.kv("iterations", 60LL); w.objEnd();
    w.objEnd();
    w.key("coupling"); w.objBegin();
    w.kv("enabled", true); w.kv("mode", "adaptive");
    w.key("coreRegion"); w.objBegin(); w.kv("shape", "circle");
    w.key("center"); w.arrBegin(); w.number(0.2); w.number(0.2); w.arrEnd();
    w.kv("radius", 0.14); w.objEnd();
    w.kv("bufferWidth", 4 * h);
    w.kv("patchTest", true);
    w.objEnd();
    w.objEnd();
    return w.str();
  };
  for (const int n : {32, 64, 128, 256}) specs.push_back(cavitySpec(n));
  for (const int n : {64, 128}) specs.push_back(cylinderSpec(n));

  mj::Writer w(1 << 16);
  w.objBegin();
  w.kv("ok", true);
  w.kv("engine", engineString());
#ifdef _OPENMP
  if (threads > 0) omp_set_num_threads(threads);
  w.kv("threads", (long long)omp_get_max_threads());
  w.kv("maxThreads", (long long)omp_get_max_threads());
#else
  w.kv("threads", 1LL);
#endif
  w.key("runs");
  w.arrBegin();
  for (const auto& s : specs) {
    mj::Value root;
    try {
      root = mj::parse(s);
    } catch (const std::exception& e) {
      std::cerr << "spec was: " << s << "\n";
      throw;
    }
    Spec spec = parseSpec(root);
    spec.out.preview = false;
    spec.out.formats = {};  // no artifacts, pure compute benchmark
    spec.out.dir = fs::temp_directory_path().string() + "/mesher-bench";
    if (threads > 0) spec.threads = threads;
    Result r = runPipeline(std::move(spec));
    Real sum = 0;
    for (const auto& kv : r.timer.ms) sum += kv.second;
    w.objBegin();
    w.kv("name", r.spec.name);
    w.kv("cells", (long long)r.mesh.cells.size());
    w.kv("faces", (long long)r.mesh.faces.size());
    w.kv("particles", (long long)r.parts.pos.size());
    w.kv("neighbourPairs", (long long)r.parts.nbrIdx.size());
    w.kv("p2mNnz", (long long)r.coupling.p2m.nnz());
    w.kv("totalMs", r.totalMs);
    w.kv("pipelineMs", sum);
    w.kv("cellsPerSecond", sum > 0 ? r.mesh.cells.size() / (sum * 1e-3) : 0.0);
    w.kv("particlesPerSecond", sum > 0 ? r.parts.pos.size() / (sum * 1e-3) : 0.0);
    w.key("timings");
    w.objBegin();
    for (const auto& kv : r.timer.ms) w.kv(kv.first, kv.second);
    w.objEnd();
    w.objEnd();
  }
  w.arrEnd();
  w.objEnd();
  const std::string out = w.str();
  if (!outPath.empty()) {
    std::error_code ec;
    fs::create_directories(fs::path(outPath).parent_path(), ec);
    std::ofstream os(outPath);
    os << out;
    std::cout << "{\"ok\":true,\"path\":\"" << outPath << "\"}\n";
  } else {
    std::cout << out << "\n";
  }
  return 0;
}

}  // namespace

int main(int argc, char** argv) {
  try {
    if (argc < 2) {
      std::cerr << "usage: mesher <run|bench|version> ...\n";
      return 2;
    }
    const std::string cmd = argv[1];
    if (cmd == "version" || cmd == "--version" || cmd == "-v") {
      mj::Writer w;
      w.objBegin();
      w.kv("ok", true);
      w.kv("engine", engineString());
      w.kv("dimension", 2LL);
#ifdef _OPENMP
      w.kv("openmp", true);
      w.kv("maxThreads", (long long)omp_get_max_threads());
#else
      w.kv("openmp", false);
      w.kv("maxThreads", 1LL);
#endif
      w.objEnd();
      std::cout << w.str() << "\n";
      return 0;
    }
    if (cmd == "run") return cmdRun(argc, argv);
    if (cmd == "bench" || cmd == "selftest" || cmd == "benchmark") return cmdBench(argc, argv);
    std::cerr << "{\"ok\":false,\"error\":\"unknown command: " << cmd << "\"}\n";
    return 2;
  } catch (const std::exception& e) {
    mj::Writer w;
    w.objBegin();
    w.kv("ok", false);
    w.kv("error", std::string(e.what()));
    w.objEnd();
    std::cerr << w.str() << "\n";
    return 1;
  }
}
