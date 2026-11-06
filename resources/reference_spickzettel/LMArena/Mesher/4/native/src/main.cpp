#include "engine.hpp"

#include <chrono>
#include <iostream>
#include <sstream>

#ifdef _OPENMP
#include <omp.h>
#endif

using namespace fvmspm;

namespace {
std::string errorJson(const std::string &msg) {
  std::string safe;
  for (char c : msg) {
    if (c == '"' || c == '\\') safe.push_back('\\');
    safe.push_back(c);
  }
  return "{\"error\":\"" + safe + "\"}";
}
}  // namespace

int main(int argc, char **argv) {
  std::ios::sync_with_stdio(false);
  std::stringstream buffer;
  buffer << std::cin.rdbuf();
  std::string input = buffer.str();
  if (argc > 1 && input.empty()) {
    std::ostringstream ss;
    for (int i = 1; i < argc; ++i) ss << argv[i] << "\n";
    input = ss.str();
  }

  try {
    const Config cfg = parseConfig(input);
    if (cfg.op == "health") {
      std::cout << "{\"ok\":true,\"engine\":\"fvm-sph-mesher\",\"version\":\"1.0.0\","
                   "\"language\":\"C++17\",\"standard\":\"C++17\""
#ifdef _OPENMP
                   ",\"openmp\":true,\"maxThreads\":" +
                       std::to_string(omp_get_max_threads()) +
#endif
#ifndef _OPENMP
                   ",\"openmp\":false,\"maxThreads\":1"
#endif
                   ",\"features\":[\"voronoi-mesh\",\"sph-consistency\",\"hllc-fvm\","
                   "\"particle-mesh-interpolation\",\"sensitivities\"]}"
                << std::endl;
      return 0;
    }

#ifdef _OPENMP
    if (cfg.threads > 0) omp_set_num_threads(cfg.threads);
    if (cfg.threads == 0) omp_set_num_threads(omp_get_max_threads());
#endif

    const auto t0 = std::chrono::high_resolution_clock::now();
    MeshOptions opts;
    Mesh mesh = buildMesh(cfg, opts);
    const auto t1 = std::chrono::high_resolution_clock::now();
    const double buildMs = std::chrono::duration<double, std::milli>(t1 - t0).count();

    if (mesh.cells.empty()) {
      std::cout << errorJson("mesh generation produced no cells") << std::endl;
      return 2;
    }

    const double seedCount = static_cast<double>(mesh.cells.size());
    if (cfg.op == "mesh") {
      std::cout << meshToJson(mesh, cfg, buildMs, seedCount, static_cast<double>(mesh.cells.size()))
                << std::endl;
      return 0;
    }
    if (cfg.op == "probe") {
      std::cout << probeResidual(cfg, mesh) << std::endl;
      return 0;
    }
    if (cfg.op == "solve") {
      SolveResult res = solve(cfg, mesh);
      std::cout << "{\"mesh\":"
                << meshToJson(mesh, cfg, buildMs, seedCount, static_cast<double>(mesh.cells.size()))
                << ",\"solve\":" << solveToJson(res, mesh, cfg) << "}" << std::endl;
      return 0;
    }
    std::cout << errorJson("unknown op: " + cfg.op) << std::endl;
    return 2;
  } catch (const std::exception &e) {
    std::cout << errorJson(e.what()) << std::endl;
    return 3;
  } catch (...) {
    std::cout << errorJson("unknown native failure") << std::endl;
    return 3;
  }
}
