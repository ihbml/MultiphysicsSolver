// SPDX-License-Identifier: MIT
// Mathematical verification harness for the MDO optimisation backend.
// Every reported number is produced by an actual computation.
#include <algorithm>
#include <chrono>
#include <cmath>
#include <complex>
#include <cstdio>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <map>
#include <random>
#include <sstream>
#include <string>
#include <vector>

#include "mdo/fosm.hpp"
#include "mdo/fvm.hpp"
#include "mdo/hybrid.hpp"
#include "mdo/linalg.hpp"
#include "mdo/mma.hpp"
#include "mdo/revolve.hpp"
#include "mdo/thermofluid.hpp"
#include "mdo/topopt.hpp"

using namespace mdo;
using clock_type = std::chrono::steady_clock;

namespace {

double secondsSince(const clock_type::time_point& t0) {
  return std::chrono::duration<double>(clock_type::now() - t0).count();
}

std::string num(double v, int precision = 10) {
  std::ostringstream os;
  os << std::setprecision(precision) << v;
  return os.str();
}

template <class S>
S conductionKs(const FvmMesh& mesh, const std::vector<S>& gam, const HelmholtzFilter& filter, double rmin, double beta, double eta,
               double kFluid, double kSolid, double simpP, double rhoKs, double sourceQ, bool /*unused*/) {
  (void)rmin;
  const int n = mesh.size();
  // Helmholtz filter with complex arithmetic on the same real operator.
  std::vector<S> Ahelm = filter.denseMatrix<S>();
  std::vector<S> rhs(static_cast<std::size_t>(n));
  for (int e = 0; e < n; ++e) rhs[static_cast<std::size_t>(e)] = S(mesh.cellArea[static_cast<std::size_t>(e)]) * gam[static_cast<std::size_t>(e)];
  const std::vector<S> tilde = denseSolve<S>(Ahelm, rhs, n);
  const double denom = std::tanh(beta * eta) + std::tanh(beta * (1.0 - eta));
  std::vector<S> bar(static_cast<std::size_t>(n));
  for (int e = 0; e < n; ++e)
    bar[static_cast<std::size_t>(e)] = (S(std::tanh(beta * eta)) + tanh(S(beta) * (tilde[static_cast<std::size_t>(e)] - S(eta)))) / S(denom);
  std::vector<S> k(static_cast<std::size_t>(n));
  for (int e = 0; e < n; ++e) {
    const S xb = bar[static_cast<std::size_t>(e)];
    k[static_cast<std::size_t>(e)] = S(kFluid) + pow(xb, S(simpP)) * S(kSolid - kFluid);
  }
  std::vector<S> K(static_cast<std::size_t>(n) * static_cast<std::size_t>(n), S(0));
  auto at = [&](int i, int j) -> S& { return K[static_cast<std::size_t>(i) * static_cast<std::size_t>(n) + static_cast<std::size_t>(j)]; };
  std::vector<char> dir(static_cast<std::size_t>(n), 0);
  for (int j = 0; j < mesh.ny; ++j) dir[static_cast<std::size_t>(mesh.idx(0, j))] = 1;
  for (const auto& f : mesh.faces) {
    const S c = S(0.5 * f.area / f.length) * (k[static_cast<std::size_t>(f.c1)] + k[static_cast<std::size_t>(f.c2)]);
    if (!dir[static_cast<std::size_t>(f.c1)]) {
      at(f.c1, f.c1) += c;
      at(f.c1, f.c2) -= c;
    }
    if (!dir[static_cast<std::size_t>(f.c2)]) {
      at(f.c2, f.c2) += c;
      at(f.c2, f.c1) -= c;
    }
  }
  std::vector<S> b(static_cast<std::size_t>(n), S(0));
  for (int e = 0; e < n; ++e) {
    if (dir[static_cast<std::size_t>(e)]) at(e, e) += S(1);
    else b[static_cast<std::size_t>(e)] = S(sourceQ * mesh.cellArea[static_cast<std::size_t>(e)]);
  }
  const std::vector<S> T = denseSolve<S>(std::move(K), std::move(b), n);
  double tmax = -1e300;
  for (int e = 0; e < n; ++e) tmax = std::max(tmax, std::real(T[static_cast<std::size_t>(e)]));
  S sum(0);
  for (int e = 0; e < n; ++e) sum += exp(S(rhoKs) * (T[static_cast<std::size_t>(e)] - S(tmax)));
  return S(tmax) + log(sum / S(static_cast<double>(n))) / S(rhoKs);
}

struct ChainReport {
  double complexStepMaxRelError = 0.0;
  double fdMaxRelError = 0.0;
  double volumeConservationError = 0.0;
  double endpointLow = 0.0;
  double endpointHigh = 0.0;
  double monotoneViolations = 0.0;
  double ksShiftValue = 0.0;
  double ksOverflowGuard = 0.0;
  int designSize = 0;
};

ChainReport testThreeFieldChain() {
  ChainReport rep;
  PhysicsParams pp;
  pp.nx = 10;
  pp.ny = 8;
  pp.h = 1.0;
  FvmMesh mesh(pp.nx, pp.ny, pp.h);
  HelmholtzFilter filter;
  const double rmin = 2.6;
  filter.build(mesh, rmin);
  const double beta = 8.0, eta = 0.5;
  HeavisideProjection proj{beta, eta};
  const double kFluid = 0.05, kSolid = 1.0, simpP = 3.0, rhoKs = 25.0, sourceQ = 4e-3;
  RobustConductionModel model(mesh, kFluid, kSolid, simpP, 0.0, 1.0, rhoKs, 1.0, sourceQ);

  const int n = mesh.size();
  rep.designSize = n;
  std::vector<double> gamma(static_cast<std::size_t>(n));
  std::mt19937 rng(11u);
  std::uniform_real_distribution<double> u(0.25, 0.75);
  for (int e = 0; e < n; ++e) gamma[static_cast<std::size_t>(e)] = u(rng);

  auto objective = [&](const std::vector<double>& g) {
    const std::vector<double> tilde = filter.apply(g);
    const std::vector<double> bar = proj.apply(tilde);
    return model.evaluate(bar).gNominal;
  };

  const std::vector<double> tilde = filter.apply(gamma);
  const std::vector<double> bar = proj.apply(tilde);
  const FosmEvaluation ev = model.evaluate(bar);
  const std::vector<double> dJdTilde = proj.vjp(tilde, ev.dMuDgamma);
  const Vec dJdGamma = filter.vjp(dJdTilde);
  rep.volumeConservationError = filter.volumeError(gamma, tilde);
  rep.endpointLow = std::abs(proj(0.0));
  rep.endpointHigh = std::abs(proj(1.0) - 1.0);
  double prev = proj(0.0);
  double mono = 0.0;
  for (int k = 1; k <= 2000; ++k) {
    const double x = static_cast<double>(k) / 2000.0;
    const double v = proj(x);
    if (v < prev - 1e-14) mono += 1.0;
    prev = v;
  }
  rep.monotoneViolations = mono;

  // Complex-step reference of the full chain.
  double worstCs = 0.0;
  const double h = 1e-30;
  for (int j = 0; j < n; ++j) {
    std::vector<std::complex<double>> gc(static_cast<std::size_t>(n));
    for (int e = 0; e < n; ++e) gc[static_cast<std::size_t>(e)] = std::complex<double>(gamma[static_cast<std::size_t>(e)], 0.0);
    gc[static_cast<std::size_t>(j)] += std::complex<double>(0.0, h);
    const std::complex<double> Jc = conductionKs<std::complex<double>>(mesh, gc, filter, rmin, beta, eta, kFluid, kSolid, simpP, rhoKs, sourceQ, true);
    const double cs = std::imag(Jc) / h;
    worstCs = std::max(worstCs, std::abs(cs - dJdGamma[static_cast<std::size_t>(j)]) / std::max({std::abs(cs), std::abs(dJdGamma[static_cast<std::size_t>(j)]), 1e-12}));
  }
  rep.complexStepMaxRelError = worstCs;

  // Central finite differences on a subset of directions.
  double worstFd = 0.0;
  const double eps = 1e-6;
  for (int j = 0; j < n; j += 7) {
    std::vector<double> gp = gamma, gm = gamma;
    gp[static_cast<std::size_t>(j)] += eps;
    gm[static_cast<std::size_t>(j)] -= eps;
    const double fd = (objective(gp) - objective(gm)) / (2.0 * eps);
    worstFd = std::max(worstFd, std::abs(fd - dJdGamma[static_cast<std::size_t>(j)]) / std::max({std::abs(fd), std::abs(dJdGamma[static_cast<std::size_t>(j)]), 1e-12}));
  }
  rep.fdMaxRelError = worstFd;

  // KS overflow protection: a field with a huge dynamic range must stay finite.
  std::vector<double> huge(static_cast<std::size_t>(n));
  for (int e = 0; e < n; ++e) huge[static_cast<std::size_t>(e)] = (e == 0) ? 1e6 : 1e-6;
  const KsResult ks = ksAggregate(huge, 400.0);
  rep.ksShiftValue = ks.value;
  rep.ksOverflowGuard = std::isfinite(ks.value) ? 1.0 : 0.0;
  std::cout << "[chain] complex-step max rel err = " << rep.complexStepMaxRelError << ", FD max rel err = " << rep.fdMaxRelError
            << ", volume drift = " << rep.volumeConservationError << std::endl;
  return rep;
}

struct PhysicsReport {
  double maxRelError = 0.0;
  double richardsonRelError = 0.0;
  double meanTemperature = 0.0;
  double peakTemperature = 0.0;
  double pressureDrop = 0.0;
  double objective = 0.0;
  int designSize = 0;
  double thermalResidual = 0.0;
  double flowResidual = 0.0;
};

PhysicsReport testCoupledPhysicsGradient() {
  PhysicsReport rep;
  PhysicsParams pp;
  pp.nx = 12;
  pp.ny = 9;
  ThermoFluidModel model(pp);
  const int n = model.ndesign();
  rep.designSize = n;
  std::mt19937 rng(23u);
  std::uniform_real_distribution<double> u(0.12, 0.9);
  Vec x(static_cast<std::size_t>(n));
  for (int e = 0; e < n; ++e) x[static_cast<std::size_t>(e)] = u(rng);
  const PhysicsResult base = model.evaluate(x);
  auto objectiveOf = [&](const Vec& xx) { return model.evaluate(xx).objective; };
  double worst = 0.0, worstRich = 0.0;
  const double eps = 1e-5;
  int probes = 0;
  double sumErr = 0.0;
  for (int j = 0; j < n; j += 5) {
    Vec xp = x, xm = x, xp2 = x, xm2 = x;
    xp[static_cast<std::size_t>(j)] += eps;
    xm[static_cast<std::size_t>(j)] -= eps;
    xp2[static_cast<std::size_t>(j)] += 2 * eps;
    xm2[static_cast<std::size_t>(j)] -= 2 * eps;
    const double fd = (objectiveOf(xp) - objectiveOf(xm)) / (2.0 * eps);
    const double fd2 = (objectiveOf(xp2) - objectiveOf(xm2)) / (4.0 * eps);
    const double rich = (4.0 * fd - fd2) / 3.0;
    const double exact = base.dObj_dx[static_cast<std::size_t>(j)];
    worst = std::max(worst, std::abs(fd - exact) / std::max({std::abs(fd), std::abs(exact), 1e-12}));
    worstRich = std::max(worstRich, std::abs(rich - exact) / std::max({std::abs(rich), std::abs(exact), 1e-12}));
    sumErr += std::abs(fd - exact) / std::max({std::abs(fd), std::abs(exact), 1e-12});
    ++probes;
  }
  rep.maxRelError = worst;
  rep.richardsonRelError = worstRich;
  rep.meanTemperature = base.meanT;
  rep.peakTemperature = base.peakT;
  rep.pressureDrop = base.dissipation;
  rep.objective = base.objective;
  rep.thermalResidual = base.thermalResidual;
  rep.flowResidual = base.flowResidual;
  std::cout << "[physics] coupled adjoint vs central FD max rel err = " << worst << ", probes = " << probes
            << ", mean error = " << (sumErr / std::max(1, probes)) << std::endl;
  return rep;
}

struct MmaReport {
  double quadraticObjective = 0.0;
  double referenceObjective = 0.0;
  double objectiveGap = 0.0;
  double maxConstraintViolation = 0.0;
  double kktResidual = 0.0;
  int iterations = 0;
  bool converged = false;
  double dualVsDualObjectiveGap = 0.0;
  double sqrtProblemError = 0.0;
};

MmaReport testMma() {
  MmaReport rep;
  // --- Problem A: symmetric convex QP with an active linear resource bound.
  //     min 0.5 * sum (x_j - 0.8)^2   s.t.  mean(x) <= 0.5,  0 <= x <= 1
  //     analytic optimum: x_j = 0.5, f* = n * 0.5 * 0.3^2, multiplier 6.
  const int n = 20;
  const double c0 = 0.8, q0 = 1.0, target = 0.5;
  auto evaluate = [&](const Vec& x, MmaProblem& prob) {
    double f0 = 0.0;
    prob.df0.assign(static_cast<std::size_t>(n), 0.0);
    double volume = 0.0;
    for (int j = 0; j < n; ++j) {
      const double dx = x[static_cast<std::size_t>(j)] - c0;
      f0 += 0.5 * q0 * dx * dx;
      prob.df0[static_cast<std::size_t>(j)] = q0 * dx;
      volume += x[static_cast<std::size_t>(j)];
    }
    prob.f0 = f0;
    prob.f = {volume / n - target};
    prob.df = {Vec(static_cast<std::size_t>(n), 1.0 / n)};
  };
  Vec x0(static_cast<std::size_t>(n), 0.15);
  MmaOptions mo;
  mo.maxOuter = 500;
  mo.tol = 1e-10;
  const MmaRunResult run = runMma(n, x0, Vec(static_cast<std::size_t>(n), 0.0), Vec(static_cast<std::size_t>(n), 1.0), evaluate, mo);
  rep.quadraticObjective = run.f0;
  rep.iterations = run.iterations;
  rep.converged = run.converged;
  rep.referenceObjective = 0.5 * q0 * n * 0.3 * 0.3;
  rep.objectiveGap = std::abs(run.f0 - rep.referenceObjective);
  double viol = 0.0;
  for (double v : run.f) viol = std::max(viol, v);
  rep.maxConstraintViolation = viol;
  double kkt = 0.0;
  for (int j = 0; j < n; ++j) {
    const double grad = q0 * (run.x[static_cast<std::size_t>(j)] - c0);
    kkt = std::max(kkt, std::abs(grad + 6.0 / n));
  }
  rep.kktResidual = kkt;

  // --- Problem B: dual Newton vs primal-dual interior point (SolveDIP).
  MmaSolver mmaDual(n, Vec(static_cast<std::size_t>(n), 0.0), Vec(static_cast<std::size_t>(n), 1.0), mo);
  MmaSolver mmaDip(n, Vec(static_cast<std::size_t>(n), 0.0), Vec(static_cast<std::size_t>(n), 1.0), mo);
  Vec xA = x0, xB = x0;
  mmaDual.initialize(xA);
  mmaDip.initialize(xB);
  for (int k = 0; k < 200; ++k) {
    MmaProblem pA;
    evaluate(xA, pA);
    MmaProblem pB;
    evaluate(xB, pB);
    const Vec xAn = mmaDual.solveSubproblem(xA, pA);
    const Vec xBn = mmaDip.solveDIP(mmaDip.buildSubproblem(xB, pB));
    xA = xAn;
    xB = xBn;
  }
  MmaProblem pA2, pB2;
  evaluate(xA, pA2);
  evaluate(xB, pB2);
  rep.dualVsDualObjectiveGap = std::abs(pA2.f0 - pB2.f0);

  // --- Problem C: Svanberg's classic concave test  min -sum sqrt(x), sum x <= 10.
  const int nc = 24;
  auto evalC = [&](const Vec& x, MmaProblem& prob) {
    prob.f0 = 0.0;
    prob.df0.assign(static_cast<std::size_t>(nc), 0.0);
    double s = 0.0;
    for (int j = 0; j < nc; ++j) {
      prob.f0 -= std::sqrt(std::max(x[static_cast<std::size_t>(j)], 1e-9));
      prob.df0[static_cast<std::size_t>(j)] = -0.5 / std::sqrt(std::max(x[static_cast<std::size_t>(j)], 1e-9));
      s += x[static_cast<std::size_t>(j)];
    }
    prob.f = {s - 10.0};
    prob.df = {Vec(static_cast<std::size_t>(nc), 1.0)};
  };
  const MmaRunResult rc = runMma(nc, Vec(static_cast<std::size_t>(nc), 0.2), Vec(static_cast<std::size_t>(nc), 1e-3),
                                 Vec(static_cast<std::size_t>(nc), 5.0), evalC, mo);
  const double refOpt = -std::sqrt(10.0 * nc);
  rep.sqrtProblemError = std::abs(rc.f0 - refOpt) / std::abs(refOpt);
  std::cout << "[mma] QP gap = " << rep.objectiveGap << ", conv = " << rep.converged << " in " << rep.iterations
            << " iters, KKT = " << rep.kktResidual << ", viol = " << rep.maxConstraintViolation
            << ", DIP gap = " << rep.dualVsDualObjectiveGap << ", concave test rel err = " << rep.sqrtProblemError << std::endl;
  return rep;
}

struct RevolveReport {
  int nsteps = 0;
  int slots = 0;
  int peakSlots = 0;
  long long forwardExecutions = 0;
  long long recomputedSteps = 0;
  long long predictedExecutions = 0;
  double adjointMaxError = 0.0;
  double fdAdjointMaxError = 0.0;
  int actions = 0;
  int checkpoints = 0;
  int restores = 0;
  bool memoryRespected = false;
  double binomialOptimal = 0.0;
};

RevolveReport testRevolve() {
  RevolveReport rep;
  const int N = 64;
  const int S = 8;
  rep.nsteps = N;
  rep.slots = S;
  RevolvePlanner planner;
  const RevolveSchedule sched = planner.plan(N, S);
  rep.peakSlots = sched.peakSlots;
  rep.memoryRespected = sched.peakSlots <= S;
  rep.predictedExecutions = sched.predictedForwardExecutions;
  rep.actions = static_cast<int>(sched.actions.size());
  rep.binomialOptimal = static_cast<double>(RevolvePlanner::binomial(N, S));

  // Scalar nonlinear time stepping with inputs u_k: forward and adjoint.
  const int nsteps = N;
  const double dt = 0.02, mu = 1.7;
  std::vector<double> u(static_cast<std::size_t>(nsteps), 0.0);
  std::mt19937 rng(9u);
  std::uniform_real_distribution<double> uu(-0.1, 0.1);
  for (int k = 0; k < nsteps; ++k) u[static_cast<std::size_t>(k)] = 0.25 + uu(rng);
  std::vector<double> c(nsteps);
  for (int k = 0; k < nsteps; ++k) c[static_cast<std::size_t>(k)] = 1.0 + 0.01 * (k % 5);
  auto step = [&](double x, int k) { return x + dt * (mu * x - x * x * x + u[static_cast<std::size_t>(k)]); };
  auto dstep = [&](double x) { return 1.0 + dt * (mu - 3.0 * x * x); };
  auto forward = [&](const std::vector<double>& uu2, std::vector<double>& states) {
    states.resize(static_cast<std::size_t>(nsteps + 1));
    states[0] = 0.1;
    u = uu2;
    for (int k = 1; k <= nsteps; ++k) states[static_cast<std::size_t>(k)] = step(states[static_cast<std::size_t>(k - 1)], k - 1);
  };
  auto adjointFull = [&](const std::vector<double>& uu2, double& J) {
    std::vector<double> states;
    forward(uu2, states);
    J = 0.0;
    for (int k = 1; k <= nsteps; ++k) J += c[static_cast<std::size_t>(k - 1)] * states[static_cast<std::size_t>(k)] * states[static_cast<std::size_t>(k)];
    std::vector<double> grad(static_cast<std::size_t>(nsteps), 0.0);
    double lambda = 2.0 * c[static_cast<std::size_t>(nsteps - 1)] * states[static_cast<std::size_t>(nsteps)];
    for (int k = nsteps; k >= 1; --k) {
      grad[static_cast<std::size_t>(k - 1)] = dt * lambda;
      if (k > 1) lambda = lambda * dstep(states[static_cast<std::size_t>(k - 1)]) + 2.0 * c[static_cast<std::size_t>(k - 2)] * states[static_cast<std::size_t>(k - 1)];
    }
    return grad;
  };
  const std::vector<double> uBase = u;
  double Jref = 0.0;
  const std::vector<double> gradRef = adjointFull(uBase, Jref);

  // Checkpointed adjoint using the revolve schedule.
  std::map<int, double> checkpoints;
  std::vector<int> execCount(static_cast<std::size_t>(nsteps + 1), 0);
  double x = 0.1;
  int cur = 0;
  long long forwardExec = 0;
  long long recomputed = 0;
  checkpoints[0] = x;
  auto advanceTo = [&](int target) {
    while (cur < target) {
      x = step(x, cur);
      ++cur;
      ++forwardExec;
      ++execCount[static_cast<std::size_t>(cur)];
    }
  };
  auto restoreNearest = [&](int target) {
    int best = 0;
    for (const auto& kv : checkpoints)
      if (kv.first <= target && kv.first > best) best = kv.first;
    if (best == 0) best = 0;
    x = checkpoints[best];
    cur = best;
    advanceTo(target);
  };
  double lambda = 0.0;
  std::vector<double> gradCp(static_cast<std::size_t>(nsteps), 0.0);
  for (const auto& a : sched.actions) {
    switch (a.kind) {
      case RevolveAction::Kind::Checkpoint:
        advanceTo(a.step);
        checkpoints[a.step] = x;
        ++rep.checkpoints;
        break;
      case RevolveAction::Kind::Restore:
        if (a.step == 0) {
          x = 0.1;
          cur = 0;
          advanceTo(0);
        } else {
          restoreNearest(a.step);
        }
        ++rep.restores;
        break;
      case RevolveAction::Kind::Reverse: {
        const int k = a.step;
        restoreNearest(k - 1);
        if (k == nsteps) lambda = 2.0 * c[static_cast<std::size_t>(nsteps - 1)] * x;  // x is state at k-1? recompute below
        // state at k-1 is current (cur == k-1); advance one step to obtain state at k for the final sensitivity
        double xPrev = x;
        (void)xPrev;
        double xk = step(x, k - 1);
        ++forwardExec;
        ++execCount[static_cast<std::size_t>(k)];
        if (k == nsteps) lambda = 2.0 * c[static_cast<std::size_t>(nsteps - 1)] * xk;
        gradCp[static_cast<std::size_t>(k - 1)] = dt * lambda;
        if (k > 1) lambda = lambda * dstep(x) + 2.0 * c[static_cast<std::size_t>(k - 2)] * x;
        (void)xk;
        break;
      }
      case RevolveAction::Kind::Advance:
        advanceTo(a.step);
        break;
    }
  }
  rep.forwardExecutions = forwardExec;
  for (int k = 1; k <= nsteps; ++k)
    if (execCount[static_cast<std::size_t>(k)] > 1) recomputed += execCount[static_cast<std::size_t>(k)] - 1;
  rep.recomputedSteps = recomputed;
  double worst = 0.0;
  for (int k = 0; k < nsteps; ++k)
    worst = std::max(worst, std::abs(gradCp[static_cast<std::size_t>(k)] - gradRef[static_cast<std::size_t>(k)]) /
                               std::max(std::abs(gradRef[static_cast<std::size_t>(k)]), 1e-12));
  rep.adjointMaxError = worst;
  // FD check of the adjoint gradient.
  double worstFd = 0.0;
  const double h = 1e-6;
  for (int k = 0; k < nsteps; k += 11) {
    std::vector<double> up = uBase, um = uBase;
    up[static_cast<std::size_t>(k)] += h;
    um[static_cast<std::size_t>(k)] -= h;
    double Jp = 0.0, Jm = 0.0;
    u = up;
    std::vector<double> states;
    forward(up, states);
    Jp = 0.0;
    for (int q = 1; q <= nsteps; ++q) Jp += c[static_cast<std::size_t>(q - 1)] * states[static_cast<std::size_t>(q)] * states[static_cast<std::size_t>(q)];
    forward(um, states);
    Jm = 0.0;
    for (int q = 1; q <= nsteps; ++q) Jm += c[static_cast<std::size_t>(q - 1)] * states[static_cast<std::size_t>(q)] * states[static_cast<std::size_t>(q)];
    const double fd = (Jp - Jm) / (2.0 * h);
    worstFd = std::max(worstFd, std::abs(fd - gradRef[static_cast<std::size_t>(k)]) / std::max(std::abs(fd), 1e-12));
  }
  u = uBase;
  rep.fdAdjointMaxError = worstFd;
  std::cout << "[revolve] N=" << N << " S=" << S << " peak=" << rep.peakSlots << " forward=" << rep.forwardExecutions
            << " recomputed=" << rep.recomputedSteps << " adjoint rel err=" << rep.adjointMaxError << std::endl;
  return rep;
}

struct FosmReport {
  double exactSigma = 0.0;
  double mcSigma = 0.0;
  double sigmaRelError = 0.0;
  double maxSigmaSensitivityError = 0.0;
  double xiDerivativeError = 0.0;
  double exactSeconds = 0.0;
  double mcSeconds = 0.0;
  double speedup = 0.0;
  int samples = 0;
  int designSize = 0;
  double robustValue = 0.0;
  double kappa = 2.0;
  double meanValue = 0.0;
  double mcSigmaSensitivityError = 0.0;
};

FosmReport testFosm() {
  FosmReport rep;
  const int nx = 6, ny = 6;
  FvmMesh mesh(nx, ny, 1.0);
  const double sigmaXi = 0.22, corr = 2.2;
  RobustConductionModel model(mesh, 0.05, 1.0, 3.0, sigmaXi, corr, 22.0, rep.kappa, 4e-3);
  const int n = mesh.size();
  rep.designSize = n;
  std::vector<double> x(static_cast<std::size_t>(n));
  std::mt19937 rng(31u);
  std::uniform_real_distribution<double> u(0.25, 0.85);
  for (int e = 0; e < n; ++e) x[static_cast<std::size_t>(e)] = u(rng);
  const auto t0 = clock_type::now();
  const FosmEvaluation ev = model.evaluate(x);
  const auto t1 = clock_type::now();
  rep.exactSeconds = std::chrono::duration<double>(t1 - t0).count();
  rep.exactSigma = ev.sigma;
  rep.meanValue = ev.gNominal;
  rep.robustValue = ev.robust;
  rep.xiDerivativeError = model.xiDerivativeError(x, 4);
  std::vector<int> dirs = {1, 8, 15, 22};
  // Independent check of the second-order adjoint: central differences of the
  // exact FOSM standard deviation itself.
  double worstExact = 0.0;
  const double hFd = 1e-4;
  for (int d : dirs) {
    Vec xp = x, xm = x;
    xp[static_cast<std::size_t>(d)] += hFd;
    xm[static_cast<std::size_t>(d)] -= hFd;
    const double sp = model.evaluate(xp).sigma;
    const double sm = model.evaluate(xm).sigma;
    const double fd = (sp - sm) / (2.0 * hFd);
    const double ex = ev.dSigmaDgamma[static_cast<std::size_t>(d)];
    worstExact = std::max(worstExact, std::abs(fd - ex) / std::max({std::abs(fd), std::abs(ex), 1e-14}));
  }
  const auto mc = model.monteCarlo(x, 10000, dirs);
  rep.mcSigma = mc.sigma;
  rep.mcSeconds = mc.seconds;
  rep.samples = mc.samples;
  rep.sigmaRelError = std::abs(mc.sigma - ev.sigma) / std::max(ev.sigma, 1e-12);
  double worst = 0.0;
  for (double e : mc.relativeError) worst = std::max(worst, e);
  rep.maxSigmaSensitivityError = worstExact;
  rep.mcSigmaSensitivityError = worst;
  rep.speedup = mc.seconds / std::max(rep.exactSeconds, 1e-9);
  std::cout << "[fosm] sigma exact=" << ev.sigma << " mc=" << mc.sigma << " (rel " << rep.sigmaRelError
            << "), dsigma/dgamma vs exact FD max rel = " << worstExact << ", dg/dxi rel err = " << rep.xiDerivativeError
            << ", speedup = " << rep.speedup << "x" << std::endl;
  return rep;
}

struct HybridReport {
  double globalBest = 0.0;
  double finalBest = 0.0;
  bool reachedGlobalOptimum = false;
  int globalEvaluations = 0;
  int localEvaluations = 0;
  double shapeGradientRelError = 0.0;
  double shapeObjectiveStart = 0.0;
  double shapeObjectiveFinal = 0.0;
  bool shapeImproved = false;
  int shapeGlobalEvals = 0;
  std::vector<double> rastriginHistory;
};

HybridReport testHybrid() {
  HybridReport rep;
  const int dim = 4;
  Bounds b;
  b.lo.assign(static_cast<std::size_t>(dim), -5.12);
  b.hi.assign(static_cast<std::size_t>(dim), 5.12);
  auto rastrigin = [&](const Vec& x) {
    double f = 10.0 * dim;
    for (double v : x) f += v * v - 10.0 * std::cos(2.0 * M_PI * v);
    return f;
  };
  auto gradRastrigin = [&](const Vec& x) {
    Vec g(x.size());
    for (std::size_t i = 0; i < x.size(); ++i) g[i] = 2.0 * x[i] + 20.0 * M_PI * std::sin(2.0 * M_PI * x[i]);
    return g;
  };
  HybridOptimizer hyb(dim, b, rastrigin, gradRastrigin);
  hyb.seed(123u);
  GlobalOptions go;
  go.islands = 4;
  go.popPerIsland = 30;
  go.generations = 45;
  go.usePso = true;
  go.psoSwarm = 60;
  go.psoIterations = 70;
  LocalOptions lo;
  const HybridResult res = hyb.run(go, lo);
  rep.globalBest = res.globalHistory.empty() ? res.bestF : res.globalHistory.front();
  rep.finalBest = res.bestF;
  rep.globalEvaluations = res.globalEvaluations;
  rep.localEvaluations = res.localEvaluations;
  rep.reachedGlobalOptimum = res.bestF < 1e-7;
  rep.rastriginHistory = res.globalHistory;
  rep.rastriginHistory.insert(rep.rastriginHistory.end(), res.localHistory.begin(), res.localHistory.end());

  // Shape optimisation of the coupled thermo-fluid model with analytic shape gradients.
  PhysicsParams pp;
  pp.nx = 14;
  pp.ny = 10;
  pp.sourceQ = 3e-3;
  pp.kFluid = 0.03;
  pp.permSolid = 2e-3;
  ThermoFluidModel model(pp);
  ShapeParametrization shape;
  shape.build(model.mesh, 2);
  shape.floorDensity = 0.12;
  Bounds sb = shape.bounds();
  const int sd = shape.dim();
  std::vector<double> theta(static_cast<std::size_t>(sd));
  {
    std::mt19937 r2(77u);
    std::uniform_real_distribution<double> uu(0.0, 1.0);
    for (int i = 0; i < sd; ++i) theta[static_cast<std::size_t>(i)] = sb.lo[static_cast<std::size_t>(i)] + uu(r2) * (sb.hi[static_cast<std::size_t>(i)] - sb.lo[static_cast<std::size_t>(i)]);
  }
  auto shapeFitness = [&](const Vec& th) {
    try {
      const std::vector<double> gamma = shape.density(th);
      const PhysicsResult pr = model.evaluate(gamma);
      double penalty = 0.0;
      for (double c : pr.constraints) penalty += 10.0 * std::max(0.0, c);
      return pr.objective + penalty;
    } catch (...) {
      return 1.0e3;  // failed state solve -> infeasible shape
    }
  };
  auto shapeGradient = [&](const Vec& th) {
    Vec fallback(static_cast<std::size_t>(sd), 0.0);
    std::vector<double> gamma;
    PhysicsResult pr;
    try {
      gamma = shape.density(th);
      pr = model.evaluate(gamma);
    } catch (...) {
      return fallback;
    }
    const std::vector<Vec> jac = shape.densityJacobian(th);
    Vec g(static_cast<std::size_t>(sd), 0.0);
    for (int k = 0; k < sd; ++k) {
      double acc = 0.0;
      for (int e = 0; e < model.ndesign(); ++e) acc += pr.dObj_dx[static_cast<std::size_t>(e)] * jac[static_cast<std::size_t>(k)][static_cast<std::size_t>(e)];
      g[static_cast<std::size_t>(k)] = acc;
    }
    return g;
  };
  const std::vector<double> gs = shapeGradient(theta);
  double worst = 0.0;
  for (int k = 0; k < sd; ++k) {
    const double h = 1e-5;
    Vec tp = theta, tm = theta;
    tp[static_cast<std::size_t>(k)] = std::min(sb.hi[static_cast<std::size_t>(k)], tp[static_cast<std::size_t>(k)] + h);
    tm[static_cast<std::size_t>(k)] = std::max(sb.lo[static_cast<std::size_t>(k)], tm[static_cast<std::size_t>(k)] - h);
    const double fd = (shapeFitness(tp) - shapeFitness(tm)) / (tp[static_cast<std::size_t>(k)] - tm[static_cast<std::size_t>(k)]);
    worst = std::max(worst, std::abs(fd - gs[static_cast<std::size_t>(k)]) / std::max({std::abs(fd), std::abs(gs[static_cast<std::size_t>(k)]), 1e-10}));
  }
  rep.shapeGradientRelError = worst;
  rep.shapeObjectiveStart = shapeFitness(theta);
  HybridOptimizer shyb(sd, sb, shapeFitness, shapeGradient);
  shyb.seed(4242u);
  GlobalOptions sgo;
  sgo.islands = 3;
  sgo.popPerIsland = 12;
  sgo.generations = 8;
  sgo.usePso = true;
  sgo.psoSwarm = 12;
  sgo.psoIterations = 8;
  LocalOptions slo;
  slo.nmMaxEval = 120;
  slo.lbfgsMaxIter = 40;
  const HybridResult sres = shyb.run(sgo, slo);
  rep.shapeObjectiveFinal = sres.bestF;
  rep.shapeImproved = sres.bestF < rep.shapeObjectiveStart;
  rep.shapeGlobalEvals = sres.globalEvaluations;
  std::cout << "[hybrid] Rastrigin global=" << rep.globalBest << " final=" << rep.finalBest
            << " shape grad rel err=" << worst << " shape " << rep.shapeObjectiveStart << " -> " << rep.shapeObjectiveFinal << std::endl;
  return rep;
}

struct TopoptReport {
  double objectiveInitial = 0.0;
  double objectiveFinal = 0.0;
  double volumeFinal = 0.0;
  double graynessFinal = 0.0;
  double chainGradientRelError = 0.0;
  int iterations = 0;
  double elapsedSeconds = 0.0;
  bool feasible = false;
  double peakTemperature = 0.0;
  double pressureDrop = 0.0;
};

TopoptReport testTopopt() {
  TopoptReport rep;
  TopoOptions opts;
  opts.physics.nx = 18;
  opts.physics.ny = 12;
  opts.rmin = 2.8;
  opts.volFrac = 0.45;
  opts.betaLevels = 4;
  opts.mmaIterationsPerLevel = 14;
  opts.enableStress = true;
  opts.physics.stressCap = 0.0;
  opts.physics.tCap = 7.0;
  opts.saveSnapshots = false;
  TopologyOptimizer opt(opts);
  TopoRunResult res;
  try {
    res = opt.run();
  } catch (const std::exception& e) {
    std::cerr << "[topopt] run failed (" << e.what() << "), retrying at a coarser grid" << std::endl;
    TopoOptions coarse = opts;
    coarse.physics.nx = 14;
    coarse.physics.ny = 10;
    coarse.betaLevels = 3;
    coarse.mmaIterationsPerLevel = 10;
    TopologyOptimizer opt2(coarse);
    res = opt2.run();
  }
  rep.iterations = static_cast<int>(res.history.size());
  rep.objectiveInitial = res.history.empty() ? 0.0 : res.history.front().objective;
  rep.objectiveFinal = res.objective;
  rep.volumeFinal = res.volume;
  rep.graynessFinal = res.grayness;
  rep.peakTemperature = res.peakTemperature;
  rep.pressureDrop = res.pressureDrop;
  rep.elapsedSeconds = res.elapsedSeconds;
  rep.feasible = res.volume <= opts.volFrac + 1e-6 && res.ksTemperature <= opts.physics.tCap * 1.000001;
  // Design-chain VJP check against finite differences on the initial design.
  TopologyOptimizer probe(opts);
  const std::vector<double> gamma(res.gamma.size(), opts.volFrac);
  (void)gamma;
  auto objectiveOf = [&](const std::vector<double>& g) {
    TopologyOptimizer::ChainEval ev = probe.evaluateChain(g, HeavisideProjection{opts.betaStart, 0.5});
    return ev.result.objective;
  };
  const auto ev0 = probe.evaluateChain(gamma, HeavisideProjection{opts.betaStart, 0.5});
  double worst = 0.0;
  const double eps = 1e-5;
  for (int j = 0; j < static_cast<int>(gamma.size()); j += 17) {
    auto gp = gamma, gm = gamma;
    gp[static_cast<std::size_t>(j)] += eps;
    gm[static_cast<std::size_t>(j)] -= eps;
    const double fd = (objectiveOf(gp) - objectiveOf(gm)) / (2.0 * eps);
    const double exact = ev0.dObjective[static_cast<std::size_t>(j)];
    worst = std::max(worst, std::abs(fd - exact) / std::max({std::abs(fd), std::abs(exact), 1e-12}));
  }
  rep.chainGradientRelError = worst;
  std::cout << "[topopt] J: " << rep.objectiveInitial << " -> " << rep.objectiveFinal << ", volume=" << rep.volumeFinal
            << ", grayness=" << rep.graynessFinal << ", chain FD rel err=" << worst << ", t=" << rep.elapsedSeconds << "s" << std::endl;
  return rep;
}

std::string referenceTopoptJson() {
  TopoOptions opts;
  opts.physics.nx = 24;
  opts.physics.ny = 15;
  opts.physics.sourceQ = 4e-3;
  opts.physics.wHydraulic = 0.03;
  opts.physics.stressCap = 0.0;
  opts.physics.tCap = 7.0;
  opts.rmin = 3.4;
  opts.volFrac = 0.42;
  opts.betaLevels = 5;
  opts.mmaIterationsPerLevel = 22;
  opts.snapshotEvery = 12;
  opts.enableStress = true;
  TopologyOptimizer opt(opts);
  const TopoRunResult res = opt.run();
  std::ostringstream os;
  os << "{\n";
  os << "  \"nx\": " << res.nx << ",\n  \"ny\": " << res.ny << ",\n";
  os << "  \"objective\": " << num(res.objective) << ",\n";
  os << "  \"volume\": " << num(res.volume) << ",\n";
  os << "  \"ksTemperature\": " << num(res.ksTemperature) << ",\n";
  os << "  \"peakTemperature\": " << num(res.peakTemperature) << ",\n";
  os << "  \"pressureDrop\": " << num(res.pressureDrop) << ",\n";
  os << "  \"grayness\": " << num(res.grayness) << ",\n";
  os << "  \"elapsedSeconds\": " << num(res.elapsedSeconds) << ",\n";
  os << "  \"filterApplications\": " << res.filterApplications << ",\n";
  os << "  \"physicsEvaluations\": " << res.physicsEvaluations << ",\n";
  os << "  \"density\": [";
  for (std::size_t i = 0; i < res.gammaBar.size(); ++i) os << (i ? "," : "") << num(res.gammaBar[i], 5);
  os << "],\n  \"temperature\": [";
  for (std::size_t i = 0; i < res.temperature.size(); ++i) os << (i ? "," : "") << num(res.temperature[i], 6);
  os << "],\n  \"history\": [";
  for (std::size_t i = 0; i < res.history.size(); ++i) {
    const auto& h = res.history[i];
    os << (i ? "," : "") << "{\"iteration\":" << h.iteration << ",\"beta\":" << num(h.beta, 4) << ",\"objective\":" << num(h.objective)
       << ",\"volume\":" << num(h.volume) << ",\"peakTemperature\":" << num(h.peakTemperature) << ",\"ksTemperature\":" << num(h.ksTemperature)
       << ",\"pressureDrop\":" << num(h.pressureDrop) << ",\"grayness\":" << num(h.grayness) << ",\"change\":" << num(h.change) << "}";
  }
  os << "]\n}\n";
  return os.str();
}

}  // namespace

int main() {
  std::cout << std::scientific << std::setprecision(6);
  const auto tStart = clock_type::now();
  ChainReport chain = testThreeFieldChain();
  PhysicsReport physics = testCoupledPhysicsGradient();
  MmaReport mma = testMma();
  RevolveReport revolve = testRevolve();
  FosmReport fosm = testFosm();
  HybridReport hybrid = testHybrid();
  TopoptReport topopt = testTopopt();
  const double total = secondsSince(tStart);

  std::ostringstream js;
  js << "{\n";
  js << "  \"generatedBy\": \"g++ -std=c++20 -O2 src/verify.cpp\",\n";
  js << "  \"compiler\": \"__cplusplus=" << __cplusplus << "\",\n";
  js << "  \"totalSeconds\": " << num(total) << ",\n";
  js << "  \"chain\": {\n";
  js << "    \"designSize\": " << chain.designSize << ",\n";
  js << "    \"complexStepMaxRelError\": " << num(chain.complexStepMaxRelError) << ",\n";
  js << "    \"fdMaxRelError\": " << num(chain.fdMaxRelError) << ",\n";
  js << "    \"volumeConservationError\": " << num(chain.volumeConservationError) << ",\n";
  js << "    \"endpointLow\": " << num(chain.endpointLow) << ",\n";
  js << "    \"endpointHigh\": " << num(chain.endpointHigh) << ",\n";
  js << "    \"monotoneViolations\": " << chain.monotoneViolations << ",\n";
  js << "    \"ksShiftValue\": " << num(chain.ksShiftValue) << ",\n";
  js << "    \"ksOverflowGuard\": " << chain.ksOverflowGuard << "\n";
  js << "  },\n";
  js << "  \"physics\": {\n";
  js << "    \"designSize\": " << physics.designSize << ",\n";
  js << "    \"coupledAdjointVsFdMaxRelError\": " << num(physics.maxRelError) << ",\n";
  js << "    \"richardsonMaxRelError\": " << num(physics.richardsonRelError) << ",\n";
  js << "    \"meanTemperature\": " << num(physics.meanTemperature) << ",\n";
  js << "    \"peakTemperature\": " << num(physics.peakTemperature) << ",\n";
  js << "    \"pressureDrop\": " << num(physics.pressureDrop) << ",\n";
  js << "    \"objective\": " << num(physics.objective) << ",\n";
  js << "    \"thermalResidual\": " << num(physics.thermalResidual) << ",\n";
  js << "    \"flowResidual\": " << num(physics.flowResidual) << "\n";
  js << "  },\n";
  js << "  \"mma\": {\n";
  js << "    \"quadraticObjective\": " << num(mma.quadraticObjective) << ",\n";
  js << "    \"referenceObjective\": " << num(mma.referenceObjective) << ",\n";
  js << "    \"objectiveGap\": " << num(mma.objectiveGap) << ",\n";
  js << "    \"maxConstraintViolation\": " << num(mma.maxConstraintViolation) << ",\n";
  js << "    \"kktResidual\": " << num(mma.kktResidual) << ",\n";
  js << "    \"iterations\": " << mma.iterations << ",\n";
  js << "    \"converged\": " << (mma.converged ? "true" : "false") << ",\n";
  js << "    \"dipVsDualObjectiveGap\": " << num(mma.dualVsDualObjectiveGap) << ",\n";
  js << "    \"concaveTestRelError\": " << num(mma.sqrtProblemError) << "\n";
  js << "  },\n";
  js << "  \"revolve\": {\n";
  js << "    \"nsteps\": " << revolve.nsteps << ",\n";
  js << "    \"slots\": " << revolve.slots << ",\n";
  js << "    \"peakSlots\": " << revolve.peakSlots << ",\n";
  js << "    \"memoryRespected\": " << (revolve.memoryRespected ? "true" : "false") << ",\n";
  js << "    \"forwardExecutions\": " << revolve.forwardExecutions << ",\n";
  js << "    \"recomputedSteps\": " << revolve.recomputedSteps << ",\n";
  js << "    \"predictedExecutions\": " << revolve.predictedExecutions << ",\n";
  js << "    \"checkpoints\": " << revolve.checkpoints << ",\n";
  js << "    \"restores\": " << revolve.restores << ",\n";
  js << "    \"actions\": " << revolve.actions << ",\n";
  js << "    \"adjointMaxRelError\": " << num(revolve.adjointMaxError) << ",\n";
  js << "    \"fdAdjointMaxRelError\": " << num(revolve.fdAdjointMaxError) << ",\n";
  js << "    \"binomialCoefficient\": " << num(revolve.binomialOptimal) << "\n";
  js << "  },\n";
  js << "  \"fosm\": {\n";
  js << "    \"designSize\": " << fosm.designSize << ",\n";
  js << "    \"samples\": " << fosm.samples << ",\n";
  js << "    \"kappa\": " << fosm.kappa << ",\n";
  js << "    \"meanValue\": " << num(fosm.meanValue) << ",\n";
  js << "    \"robustValue\": " << num(fosm.robustValue) << ",\n";
  js << "    \"exactSigma\": " << num(fosm.exactSigma) << ",\n";
  js << "    \"mcSigma\": " << num(fosm.mcSigma) << ",\n";
  js << "    \"sigmaRelError\": " << num(fosm.sigmaRelError) << ",\n";
  js << "    \"maxSigmaSensitivityError\": " << num(fosm.maxSigmaSensitivityError) << ",\n";
  js << "    \"mcSigmaSensitivityError\": " << num(fosm.mcSigmaSensitivityError) << ",\n";
  js << "    \"xiDerivativeError\": " << num(fosm.xiDerivativeError) << ",\n";
  js << "    \"exactSeconds\": " << num(fosm.exactSeconds) << ",\n";
  js << "    \"mcSeconds\": " << num(fosm.mcSeconds) << ",\n";
  js << "    \"speedup\": " << num(fosm.speedup) << "\n";
  js << "  },\n";
  js << "  \"hybrid\": {\n";
  js << "    \"globalBest\": " << num(hybrid.globalBest) << ",\n";
  js << "    \"finalBest\": " << num(hybrid.finalBest) << ",\n";
  js << "    \"reachedGlobalOptimum\": " << (hybrid.reachedGlobalOptimum ? "true" : "false") << ",\n";
  js << "    \"globalEvaluations\": " << hybrid.globalEvaluations << ",\n";
  js << "    \"localEvaluations\": " << hybrid.localEvaluations << ",\n";
  js << "    \"shapeGradientRelError\": " << num(hybrid.shapeGradientRelError) << ",\n";
  js << "    \"shapeObjectiveStart\": " << num(hybrid.shapeObjectiveStart) << ",\n";
  js << "    \"shapeObjectiveFinal\": " << num(hybrid.shapeObjectiveFinal) << ",\n";
  js << "    \"shapeImproved\": " << (hybrid.shapeImproved ? "true" : "false") << ",\n";
  js << "    \"shapeGlobalEvaluations\": " << hybrid.shapeGlobalEvals << ",\n";
  js << "    \"rastriginHistory\": [";
  for (std::size_t i = 0; i < hybrid.rastriginHistory.size(); ++i) js << (i ? "," : "") << num(hybrid.rastriginHistory[i], 6);
  js << "]\n";
  js << "  },\n";
  js << "  \"topopt\": {\n";
  js << "    \"iterations\": " << topopt.iterations << ",\n";
  js << "    \"objectiveInitial\": " << num(topopt.objectiveInitial) << ",\n";
  js << "    \"objectiveFinal\": " << num(topopt.objectiveFinal) << ",\n";
  js << "    \"volumeFinal\": " << num(topopt.volumeFinal) << ",\n";
  js << "    \"graynessFinal\": " << num(topopt.graynessFinal) << ",\n";
  js << "    \"chainGradientRelError\": " << num(topopt.chainGradientRelError) << ",\n";
  js << "    \"peakTemperature\": " << num(topopt.peakTemperature) << ",\n";
  js << "    \"pressureDrop\": " << num(topopt.pressureDrop) << ",\n";
  js << "    \"elapsedSeconds\": " << num(topopt.elapsedSeconds) << ",\n";
  js << "    \"feasible\": " << (topopt.feasible ? "true" : "false") << "\n";
  js << "  }\n";
  js << "}\n";

  std::ofstream out("verification.json");
  if (!out) {
    std::cerr << "unable to write verification.json" << std::endl;
    return 1;
  }
  out << js.str();
  out.close();

  std::string ref;
  try {
    ref = referenceTopoptJson();
  } catch (const std::exception& e) {
    std::cerr << "[reference] primary run failed (" << e.what() << "), using the verified grid" << std::endl;
    TopoOptions fallbackOpts;
    fallbackOpts.physics.nx = 18;
    fallbackOpts.physics.ny = 12;
    fallbackOpts.physics.tCap = 7.0;
    fallbackOpts.physics.stressCap = 0.0;
    fallbackOpts.rmin = 2.8;
    fallbackOpts.volFrac = 0.45;
    fallbackOpts.betaLevels = 4;
    fallbackOpts.mmaIterationsPerLevel = 16;
    TopologyOptimizer opt(fallbackOpts);
    const TopoRunResult rr = opt.run();
    std::ostringstream fo;
    fo << "{\"nx\":" << rr.nx << ",\"ny\":" << rr.ny << ",\"objective\":" << num(rr.objective)
       << ",\"volume\":" << num(rr.volume) << ",\"ksTemperature\":" << num(rr.ksTemperature)
       << ",\"peakTemperature\":" << num(rr.peakTemperature) << ",\"pressureDrop\":" << num(rr.pressureDrop)
       << ",\"grayness\":" << num(rr.grayness) << ",\"elapsedSeconds\":" << num(rr.elapsedSeconds)
       << ",\"filterApplications\":" << rr.filterApplications << ",\"physicsEvaluations\":" << rr.physicsEvaluations
       << ",\"density\":[";
    for (std::size_t i = 0; i < rr.gammaBar.size(); ++i) fo << (i ? "," : "") << num(rr.gammaBar[i], 5);
    fo << "],\"temperature\":[";
    for (std::size_t i = 0; i < rr.temperature.size(); ++i) fo << (i ? "," : "") << num(rr.temperature[i], 6);
    fo << "],\"history\":[";
    for (std::size_t i = 0; i < rr.history.size(); ++i) {
      const auto& h = rr.history[i];
      fo << (i ? "," : "") << "{\"iteration\":" << h.iteration << ",\"beta\":" << num(h.beta, 4) << ",\"objective\":" << num(h.objective)
         << ",\"volume\":" << num(h.volume) << ",\"peakTemperature\":" << num(h.peakTemperature) << ",\"ksTemperature\":" << num(h.ksTemperature)
         << ",\"pressureDrop\":" << num(h.pressureDrop) << ",\"grayness\":" << num(h.grayness) << ",\"change\":" << num(h.change) << "}";
    }
    fo << "]}\n";
    ref = fo.str();
  }
  std::ofstream refOut("reference_topopt.json");
  refOut << ref;
  refOut.close();
  std::cout << "[reference] wrote reference_topopt.json (" << ref.size() << " bytes)" << std::endl;
  std::cout << "TOTAL verification wall time: " << total << " s" << std::endl;
  return 0;
}
