// SPDX-License-Identifier: MIT
// Three-field thermo-fluid topology optimisation driver:
//   gamma -> Helmholtz PDE filter -> Heaviside projection -> Darcy + energy
//   -> discrete adjoint sensitivities -> Svanberg MMA with beta continuation.
#pragma once

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <random>
#include <stdexcept>
#include <string>
#include <vector>

#include "mdo/fvm.hpp"
#include "mdo/mma.hpp"
#include "mdo/thermofluid.hpp"

namespace mdo {

struct TopoOptions {
  double rmin = 3.5;
  double volFrac = 0.42;
  double betaStart = 1.0;
  double betaMax = 32.0;
  int betaLevels = 6;
  int mmaIterationsPerLevel = 28;
  bool enableProjection = true;
  bool enableStress = true;
  double eta = 0.5;
  double moveLimit = 0.25;
  bool saveSnapshots = true;
  int snapshotEvery = 8;
  PhysicsParams physics;
  MmaOptions mma;
  double gammaNoise = 0.0;
};

struct TopoHistoryPoint {
  int iteration = 0;
  double beta = 1.0;
  double objective = 0.0;
  double volume = 0.0;
  double ksTemperature = 0.0;
  double peakTemperature = 0.0;
  double pressureDrop = 0.0;
  double thermalResistance = 0.0;
  double grayness = 0.0;
  double change = 0.0;
  double filterResidual = 0.0;
  bool feasible = false;
};

struct TopoSnapshot {
  int iteration = 0;
  double beta = 1.0;
  double objective = 0.0;
  std::vector<float> density;
};

struct TopoRunResult {
  TopoOptions options;
  std::vector<double> gamma;
  std::vector<double> gammaTilde;
  std::vector<double> gammaBar;
  std::vector<TopoHistoryPoint> history;
  std::vector<TopoSnapshot> snapshots;
  double objective = 0.0;
  double volume = 0.0;
  double ksTemperature = 0.0;
  double peakTemperature = 0.0;
  double pressureDrop = 0.0;
  double grayness = 0.0;
  double elapsedSeconds = 0.0;
  int physicsEvaluations = 0;
  int filterApplications = 0;
  int adjointSolves = 0;
  bool converged = false;
  int nx = 0;
  int ny = 0;
  std::vector<double> temperature;
  std::vector<double> pressure;
};

class TopologyOptimizer {
 public:
  explicit TopologyOptimizer(const TopoOptions& opts) : opts_(opts), model_(opts.physics) {
    filter_.build(model_.mesh, opts_.rmin);
  }

  TopoRunResult run() {
    const auto t0 = std::chrono::steady_clock::now();
    const int n = model_.ndesign();
    Vec gamma(static_cast<std::size_t>(n), opts_.volFrac);
    std::mt19937 rng(7u);
    std::uniform_real_distribution<double> noise(-opts_.gammaNoise, opts_.gammaNoise);
    if (opts_.gammaNoise > 0.0)
      for (int e = 0; e < n; ++e) gamma[static_cast<std::size_t>(e)] = std::min(1.0, std::max(0.05, gamma[static_cast<std::size_t>(e)] + noise(rng)));

    MmaSolver mma(n, Vec(static_cast<std::size_t>(n), 0.0), Vec(static_cast<std::size_t>(n), 1.0), opts_.mma);
    gamma = enforceVolume(gamma, HeavisideProjection{1e-4, opts_.eta});
    mma.initialize(gamma);
    TopoRunResult out;
    out.options = opts_;
    out.nx = model_.mesh.nx;
    out.ny = model_.mesh.ny;

    int globalIter = 0;
    int levels = std::max(1, opts_.betaLevels);
    bool converged = false;
    for (int lvl = 0; lvl < levels; ++lvl) {
      const double t = levels == 1 ? 1.0 : static_cast<double>(lvl) / static_cast<double>(levels - 1);
      const double beta = opts_.enableProjection ? opts_.betaStart * std::pow(opts_.betaMax / opts_.betaStart, t) : 0.0;
      HeavisideProjection proj;
      proj.beta = std::max(beta, 1e-4);
      proj.eta = opts_.eta;
      for (int it = 0; it < opts_.mmaIterationsPerLevel; ++it) {
        const ChainEval ev = evaluateChain(gamma, proj);
        MmaProblem prob;
        prob.f0 = ev.result.objective;
        prob.df0 = ev.dObjective;
        prob.f = ev.result.constraints;
        prob.df = ev.dConstraints;
        const Vec gammaNew = mma.solveSubproblem(gamma, prob);
        double change = 0.0;
        for (int e = 0; e < n; ++e)
          change = std::max(change, std::abs(gammaNew[static_cast<std::size_t>(e)] - gamma[static_cast<std::size_t>(e)]));
        ++globalIter;
        TopoHistoryPoint hp;
        hp.iteration = globalIter;
        hp.beta = beta;
        hp.objective = ev.result.objective;
        hp.volume = ev.result.volume;
        hp.ksTemperature = ev.result.ksT;
        hp.peakTemperature = ev.result.peakT;
        hp.pressureDrop = ev.result.dissipation;
        hp.thermalResistance = ev.result.meanT;
        hp.grayness = grayness(ev.gammaBar);
        hp.change = change;
        hp.filterResidual = ev.filterResidual;
        hp.feasible = true;
        for (double c : ev.result.constraints) hp.feasible = hp.feasible && (c <= 1e-6);
        out.history.push_back(hp);
        if (opts_.saveSnapshots && (globalIter % std::max(1, opts_.snapshotEvery) == 0 || globalIter == 1)) {
          TopoSnapshot snap;
          snap.iteration = globalIter;
          snap.beta = beta;
          snap.objective = ev.result.objective;
          snap.density.assign(ev.gammaBar.begin(), ev.gammaBar.end());
          for (auto& v : snap.density) v = static_cast<float>(v);
          out.snapshots.push_back(std::move(snap));
        }
        if (std::isnan(ev.result.objective)) throw std::runtime_error("objective became NaN");
        gamma = enforceVolume(gammaNew, proj);
        if (change < 2e-3 && it > 4) {
          if (lvl == levels - 1) converged = true;
          break;
        }
      }
    }
    const ChainEval fin = evaluateChain(gamma, HeavisideProjection{opts_.betaMax, opts_.eta});
    out.gamma = gamma;
    out.gammaTilde = fin.gammaTilde;
    out.gammaBar = fin.gammaBar;
    out.objective = fin.result.objective;
    out.volume = fin.result.volume;
    out.ksTemperature = fin.result.ksT;
    out.peakTemperature = fin.result.peakT;
    out.pressureDrop = fin.result.dissipation;
    out.grayness = grayness(fin.gammaBar);
    out.temperature = fin.result.T;
    out.pressure = fin.result.p;
    out.physicsEvaluations = physicsEvals_;
    out.filterApplications = filterApplications_;
    out.adjointSolves = adjointSolves_;
    out.converged = converged;
    if (opts_.saveSnapshots) {
      TopoSnapshot snap;
      snap.iteration = globalIter;
      snap.beta = opts_.betaMax;
      snap.objective = out.objective;
      snap.density.assign(fin.gammaBar.begin(), fin.gammaBar.end());
      for (auto& v : snap.density) v = static_cast<float>(v);
      out.snapshots.push_back(std::move(snap));
    }
    const auto t1 = std::chrono::steady_clock::now();
    out.elapsedSeconds = std::chrono::duration<double>(t1 - t0).count();
    return out;
  }

  struct ChainEval {
    std::vector<double> gammaTilde;
    std::vector<double> gammaBar;
    PhysicsResult result;
    Vec dObjective;
    std::vector<Vec> dConstraints;
    double filterResidual = 0.0;
  };

  ChainEval evaluateChain(const std::vector<double>& gamma, const HeavisideProjection& proj) {
    ChainEval ev;
    int filterIters = 0;
    ev.gammaTilde = filter_.apply(gamma, &filterIters);
    ++filterApplications_;
    ev.filterResidual = filter_.volumeError(gamma, ev.gammaTilde);
    ev.gammaBar = proj.apply(ev.gammaTilde);
    ev.result = model_.evaluate(ev.gammaBar);
    ++physicsEvals_;
    adjointSolves_ += 3;  // thermal + flow adjoint per constraint/objective family
    ev.dObjective = chainToGamma(proj, ev.gammaTilde, ev.result.dObj_dx);
    ev.dConstraints.clear();
    for (const auto& g : ev.result.dCon_dx) ev.dConstraints.push_back(chainToGamma(proj, ev.gammaTilde, g));
    return ev;
  }

 private:
  // Feasibility restoration: shift the design by a constant so that the
  // filtered+projected physical volume matches the target exactly. This is the
  // standard volume-preserving update used with Heaviside continuation; the MMA
  // step direction remains fully gradient based.
  Vec enforceVolume(const Vec& gamma, const HeavisideProjection& proj) const {
    const double target = opts_.volFrac;
    auto projectedVolume = [&](const Vec& g) {
      const std::vector<double> tilde = filter_.apply(g);
      const std::vector<double> bar = proj.apply(tilde);
      double vol = 0.0;
      for (int e = 0; e < model_.ndesign(); ++e)
        vol += model_.mesh.cellArea[static_cast<std::size_t>(e)] * bar[static_cast<std::size_t>(e)];
      return vol / model_.mesh.totalArea();
    };
    double lo = -0.05, hi = 0.05;
    for (int it = 0; it < 30; ++it) {
      const double mid = 0.5 * (lo + hi);
      Vec shifted(gamma.size());
      for (std::size_t e = 0; e < gamma.size(); ++e) shifted[e] = std::min(1.0, std::max(0.0, gamma[e] + mid));
      if (projectedVolume(shifted) < target) lo = mid;
      else hi = mid;
    }
    Vec shifted(gamma.size());
    const double mid = 0.5 * (lo + hi);
    for (std::size_t e = 0; e < gamma.size(); ++e) shifted[e] = std::min(1.0, std::max(0.0, gamma[e] + mid));
    return shifted;
  }

  Vec chainToGamma(const HeavisideProjection& proj, const std::vector<double>& gammaTilde, const Vec& dJdBar) const {
    const std::vector<double> dJdTilde = proj.vjp(gammaTilde, dJdBar);
    return filter_.vjp(dJdTilde);
  }

  static double grayness(const std::vector<double>& x) {
    double g = 0.0;
    for (double v : x) g += 4.0 * v * (1.0 - v);
    return g / static_cast<double>(std::max<std::size_t>(1, x.size()));
  }

  TopoOptions opts_;
  ThermoFluidModel model_;
  HelmholtzFilter filter_;
  int physicsEvals_ = 0;
  int filterApplications_ = 0;
  int adjointSolves_ = 0;
};

}  // namespace mdo
