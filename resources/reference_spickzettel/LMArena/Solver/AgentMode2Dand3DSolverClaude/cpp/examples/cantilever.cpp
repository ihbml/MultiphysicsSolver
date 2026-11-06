#include <fieldline/solver.hpp>

#include <cmath>
#include <iomanip>
#include <iostream>
#include <vector>

using namespace fieldline;

namespace {

template <typename Values>
void print_numbers(const Values& values) {
  std::cout << '[';
  for (std::size_t i = 0; i < values.size(); ++i) {
    if (i) std::cout << ',';
    std::cout << values[i];
  }
  std::cout << ']';
}

void print_frame(const ThermoelasticSolver& solver, const StepInfo& info) {
  std::cout << "{\"time\":" << solver.time() << ",\"temperature\":";
  print_numbers(solver.temperatures());
  std::cout << ",\"displacement\":";
  print_numbers(solver.displacements());
  std::cout << ",\"thermalIterations\":" << info.thermal.iterations
            << ",\"mechanicalIterations\":" << info.mechanical.iterations
            << ",\"thermalResidual\":" << info.thermal.relative_residual
            << ",\"mechanicalResidual\":" << info.mechanical.relative_residual
            << ",\"peakTemperature\":" << info.max_temperature
            << ",\"maxDisplacement\":" << info.max_displacement << '}';
}

}  // namespace

int main() {
  constexpr unsigned nx = 40;
  constexpr unsigned ny = 15;
  constexpr double width = 0.12;
  constexpr double height = 0.045;
  constexpr double reference = 293.15;
  constexpr double heat_density = 1.8e7;

  std::vector<Point2> nodes;
  std::vector<Triangle> triangles;
  std::vector<double> sources;
  std::vector<TemperatureBC> temperature_bcs;
  std::vector<DisplacementBC> displacement_bcs;
  nodes.reserve((nx + 1) * (ny + 1));
  triangles.reserve(2 * nx * ny);
  sources.reserve(2 * nx * ny);

  auto node = [](unsigned x, unsigned y) { return static_cast<Index>(y * (nx + 1) + x); };
  for (unsigned y = 0; y <= ny; ++y) {
    for (unsigned x = 0; x <= nx; ++x) {
      nodes.push_back({width * x / nx, height * y / ny});
      if (x == 0) {
        const Index index = node(x, y);
        temperature_bcs.push_back({index, reference});
        displacement_bcs.push_back({index, 0, 0.0});
        displacement_bcs.push_back({index, 1, 0.0});
      }
    }
  }

  for (unsigned y = 0; y < ny; ++y) {
    for (unsigned x = 0; x < nx; ++x) {
      const Index a = node(x, y), b = node(x + 1, y);
      const Index c = node(x, y + 1), d = node(x + 1, y + 1);
      // Alternating diagonals avoid a directional bias in the visualization.
      if ((x + y) % 2 == 0) {
        triangles.push_back({a, b, d});
        triangles.push_back({a, d, c});
      } else {
        triangles.push_back({a, b, c});
        triangles.push_back({b, d, c});
      }
      const double center_x = width * (x + 0.5) / nx;
      sources.push_back(center_x >= 0.075 ? heat_density : 0.0);
      sources.push_back(center_x >= 0.075 ? heat_density : 0.0);
    }
  }

  const Material aluminum{205.0, 2700.0, 900.0, 69e9, 0.33, 23e-6};
  SolverOptions options;
  options.time_step = 1.0;
  options.reference_temperature = reference;
  options.thickness = 0.005;
  options.relative_tolerance = 1e-10;
  ProblemView problem{{nodes, triangles}, std::span(&aluminum, 1), sources, {},
                      temperature_bcs, displacement_bcs, options};
  ThermoelasticSolver solver(problem);

  std::cout << std::setprecision(10);
  std::cout << "{\"meta\":{\"name\":\"Thermoelastic cantilever\",\"width\":"
            << width << ",\"height\":" << height
            << ",\"referenceTemperature\":" << reference
            << ",\"baseHeatSource\":" << heat_density
            << ",\"timeStep\":" << options.time_step
            << ",\"totalTime\":32,\"material\":\"Aluminum 6061\"},\"nodes\":[";
  for (std::size_t i = 0; i < nodes.size(); ++i) {
    if (i) std::cout << ',';
    std::cout << '[' << nodes[i].x << ',' << nodes[i].y << ']';
  }
  std::cout << "],\"triangles\":[";
  for (std::size_t i = 0; i < triangles.size(); ++i) {
    if (i) std::cout << ',';
    std::cout << '[' << triangles[i][0] << ',' << triangles[i][1] << ','
              << triangles[i][2] << ']';
  }
  std::cout << "],\"frames\":[";

  StepInfo info;
  info.min_temperature = reference;
  info.max_temperature = reference;
  print_frame(solver, info);
  for (unsigned step = 1; step <= 32; ++step) {
    info = solver.step();
    if (step % 2 == 0) {
      std::cout << ',';
      print_frame(solver, info);
    }
  }
  std::cout << "]}\n";
  std::cerr << "Generated " << nodes.size() << " nodes, " << triangles.size()
            << " triangles, 17 frames; final peak " << info.max_temperature
            << " K, max displacement " << info.max_displacement << " m.\n";
}