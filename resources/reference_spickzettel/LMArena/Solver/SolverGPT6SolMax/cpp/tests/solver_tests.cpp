#include <fieldline/solver.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <iostream>
#include <stdexcept>
#include <vector>

using namespace fieldline;

namespace {

void near(double actual, double expected, double tolerance, const char* label) {
  if (!std::isfinite(actual) || std::abs(actual - expected) > tolerance) {
    throw std::runtime_error(std::string(label) + ": expected " +
                             std::to_string(expected) + ", got " +
                             std::to_string(actual));
  }
}

struct Fixture {
  std::array<Point2, 3> nodes{{{0.0, 0.0}, {1.0, 0.0}, {0.0, 1.0}}};
  std::array<Triangle, 1> triangles{{{0, 1, 2}}};
  std::array<Material, 1> materials{{{2.0, 2.0, 5.0, 1000.0, 0.25, 0.01}}};
  std::array<double, 1> source{{2.0}};
  std::vector<TemperatureBC> temperature_bcs;
  std::vector<DisplacementBC> displacement_bcs{{0, 0, 0.0},
                                                {0, 1, 0.0},
                                                {1, 1, 0.0}};
  SolverOptions options;

  Fixture() {
    options.time_step = 0.25;
    options.relative_tolerance = 1e-12;
    options.absolute_tolerance = 1e-13;
  }

  ProblemView problem() const {
    return {{nodes, triangles}, materials, source, {}, temperature_bcs,
            displacement_bcs, options};
  }
};

void uniform_insulated_heating() {
  Fixture f;
  ThermoelasticSolver solver(f.problem());
  const StepInfo result = solver.step();
  const double rise = f.source[0] * f.options.time_step /
                      (f.materials[0].density * f.materials[0].specific_heat);

  for (double temperature : solver.temperatures()) {
    near(temperature, 293.15 + rise, 1e-11, "energy-conserving temperature");
  }
  for (std::size_t i = 0; i < f.nodes.size(); ++i) {
    near(solver.displacements()[2 * i], 0.01 * rise * f.nodes[i].x,
         1e-11, "free thermal expansion x");
    near(solver.displacements()[2 * i + 1], 0.01 * rise * f.nodes[i].y,
         1e-11, "free thermal expansion y");
  }
  near(result.time, 0.25, 1e-14, "time advance");
  if (result.thermal.absolute_residual > 1e-10 ||
      result.mechanical.absolute_residual > 1e-8) {
    throw std::runtime_error("linear systems did not reach their requested tolerance");
  }
}

void prescribed_temperature_and_initial_state() {
  Fixture f;
  f.source[0] = 0.0;
  f.temperature_bcs = {{0, 303.15}, {1, 303.15}, {2, 303.15}};
  ThermoelasticSolver solver(f.problem());
  solver.step();
  near(solver.displacements()[2], 0.1, 1e-11, "prescribed uniform expansion x");
  near(solver.displacements()[5], 0.1, 1e-11, "prescribed uniform expansion y");

  f.temperature_bcs.clear();
  ThermoelasticSolver initialized(f.problem());
  const std::array<double, 3> initial{{318.15, 318.15, 318.15}};
  initialized.set_initial_temperature(initial);
  initialized.step();
  for (double temperature : initialized.temperatures()) {
    near(temperature, 318.15, 1e-11, "constant insulated initial field");
  }
}

void winding_and_linear_scaling() {
  Fixture f;
  f.temperature_bcs = {{0, 293.15}};
  ThermoelasticSolver standard(f.problem());
  standard.step();

  f.options.preconditioner = Preconditioner::jacobi;
  ThermoelasticSolver jacobi(f.problem());
  jacobi.step();
  for (std::size_t i = 0; i < f.nodes.size(); ++i) {
    near(jacobi.temperatures()[i], standard.temperatures()[i], 1e-11,
         "Jacobi and IC(0) temperature agreement");
    near(jacobi.displacements()[2 * i], standard.displacements()[2 * i],
         1e-11, "Jacobi and IC(0) displacement agreement");
  }
  f.options.preconditioner = Preconditioner::incomplete_cholesky;

  f.triangles[0] = {0, 2, 1};
  ThermoelasticSolver reversed(f.problem());
  reversed.step();
  for (std::size_t i = 0; i < f.nodes.size(); ++i) {
    near(reversed.temperatures()[i], standard.temperatures()[i], 1e-11,
         "orientation-independent temperature");
    near(reversed.displacements()[2 * i], standard.displacements()[2 * i],
         1e-11, "orientation-independent x displacement");
    near(reversed.displacements()[2 * i + 1], standard.displacements()[2 * i + 1],
         1e-11, "orientation-independent y displacement");
  }

  ThermoelasticSolver doubled(f.problem());
  doubled.step(2.0);
  for (std::size_t i = 0; i < f.nodes.size(); ++i) {
    near(doubled.temperatures()[i] - 293.15,
         2.0 * (standard.temperatures()[i] - 293.15), 1e-10,
         "linear heat source scaling");
    near(doubled.displacements()[2 * i], 2.0 * standard.displacements()[2 * i],
         1e-11, "linear displacement scaling");
  }

  for (int step = 0; step < 2; ++step) {
    standard.step();
    reversed.step();
    doubled.step(2.0);
    for (std::size_t i = 0; i < f.nodes.size(); ++i) {
      near(reversed.temperatures()[i], standard.temperatures()[i], 1e-10,
           "multi-step orientation invariance");
      near(doubled.temperatures()[i] - 293.15,
           2.0 * (standard.temperatures()[i] - 293.15), 1e-10,
           "multi-step heat source scaling");
      near(doubled.displacements()[2 * i],
           2.0 * standard.displacements()[2 * i], 1e-11,
           "multi-step displacement scaling");
    }
  }
}

void heterogeneous_energy_balance() {
  const std::array<Point2, 4> nodes{{{0, 0}, {1, 0}, {0, 1}, {1, 1}}};
  const std::array<Triangle, 2> triangles{{{0, 1, 3}, {0, 3, 2}}};
  const std::array<Material, 2> materials{{
      {2.0, 2.0, 5.0, 1000.0, 0.25, 0.01},
      {8.0, 3.0, 4.0, 2000.0, 0.30, 0.02}}};
  const std::array<double, 2> heat{{2.0, 5.0}};
  const std::array<DisplacementBC, 3> clamps{{{0, 0, 0}, {0, 1, 0}, {1, 1, 0}}};
  SolverOptions options;
  options.time_step = 0.25;
  options.thermal_theta = 0.5;
  options.relative_tolerance = 1e-12;
  const ProblemView problem{{nodes, triangles}, materials, heat, {}, {}, clamps, options};
  ThermoelasticSolver solver(problem);
  solver.step();

  double stored_energy = 0.0;
  double supplied_energy = 0.0;
  for (std::size_t e = 0; e < triangles.size(); ++e) {
    double mean_rise = 0.0;
    for (Index node : triangles[e]) {
      mean_rise += (solver.temperatures()[node] - options.reference_temperature) / 3.0;
    }
    const double area = 0.5;
    stored_energy += materials[e].density * materials[e].specific_heat *
                     area * mean_rise;
    supplied_energy += heat[e] * area * options.time_step;
  }
  near(stored_energy, supplied_energy, 1e-10,
       "heterogeneous Crank-Nicolson energy balance");
}

void rejects_bad_inputs() {
  Fixture f;
  f.nodes[2] = {2.0, 0.0};
  bool rejected = false;
  try {
    ThermoelasticSolver solver(f.problem());
  } catch (const std::invalid_argument&) {
    rejected = true;
  }
  if (!rejected) throw std::runtime_error("degenerate triangle was accepted");

  f.nodes[2] = {0.0, 1.0};
  f.temperature_bcs = {{0, 293.15}, {0, 310.0}};
  rejected = false;
  try {
    ThermoelasticSolver solver(f.problem());
  } catch (const std::invalid_argument&) {
    rejected = true;
  }
  if (!rejected) throw std::runtime_error("conflicting constraints were accepted");

  f.temperature_bcs.clear();
  f.displacement_bcs.pop_back();
  rejected = false;
  try {
    ThermoelasticSolver solver(f.problem());
  } catch (const std::invalid_argument&) {
    rejected = true;
  }
  if (!rejected) throw std::runtime_error("unconstrained rotation was accepted");

  const std::array<Point2, 6> disconnected_nodes{{
      {0, 0}, {1, 0}, {0, 1}, {3, 0}, {4, 0}, {3, 1}}};
  const std::array<Triangle, 2> disconnected_triangles{{{0, 1, 2}, {3, 4, 5}}};
  const std::array<DisplacementBC, 3> first_body_only{{
      {0, 0, 0}, {0, 1, 0}, {1, 1, 0}}};
  const ProblemView disconnected{{disconnected_nodes, disconnected_triangles},
                                 f.materials, {}, {}, {}, first_body_only, f.options};
  rejected = false;
  try {
    ThermoelasticSolver solver(disconnected);
  } catch (const std::invalid_argument&) {
    rejected = true;
  }
  if (!rejected) throw std::runtime_error("unconstrained disconnected body was accepted");
}

}  // namespace

int main() {
  try {
    uniform_insulated_heating();
    prescribed_temperature_and_initial_state();
    winding_and_linear_scaling();
    heterogeneous_energy_balance();
    rejects_bad_inputs();
    std::cout << "All Fieldline solver tests passed.\n";
    return 0;
  } catch (const std::exception& error) {
    std::cerr << "Test failed: " << error.what() << '\n';
    return 1;
  }
}