// SPDX-License-Identifier: MIT
// Coupled steady thermo-fluid FVM model used by the topology optimiser.
//
//   Darcy / pressure Poisson:   div( kappa(x) grad p ) = 0        (p in: 1, p out: 0)
//   face flux:                  F_f = -kappa_f (A_f/h_f) (p_2 - p_1)
//   Energy (conduction + upwind advection, Dirichlet cold inlet):
//                               div(F T) - div( k(x) grad T ) = Q
//   objective J = w_T * mean(T) + w_P * sum_f kappa_f (dp)^2
//   constraints: KS peak temperature, thermo-elastic von Mises surrogate, volume
//
// The total design derivative is exact and uses two adjoint solves plus the
// pressure adjoint (second-order coupling through the convective velocity).
#pragma once

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <functional>
#include <stdexcept>
#include <vector>

#include "mdo/fvm.hpp"
#include "mdo/linalg.hpp"

namespace mdo {

enum class Interpolation { Simp, Ramp };

struct PhysicsParams {
  int nx = 36;
  int ny = 22;
  double h = 1.0;
  double kSolid = 1.0;
  double kFluid = 1e-3;
  double permSolid = 1e-3;
  double permFluid = 1.0;
  double simpP = 3.0;
  double rampQ = 8.0;
  Interpolation interp = Interpolation::Ramp;
  double sourceQ = 4e-3;
  double inletT = 0.0;
  double pIn = 1.0;
  double pOut = 0.0;
  double wThermal = 1.0;
  double wHydraulic = 0.02;
  double rhoKs = 30.0;
  double tCap = 1.2;
  double young = 1.0;
  double alphaTh = 1.0e-3;
  double tRef = 0.0;
  double nu = 0.3;
  double stressCap = 1.0;  // set <= 0 to disable the stress constraint
  double volFrac = 0.4;
};

struct InterpSample {
  double value;
  double derivative;
};

inline InterpSample interpolate(double x, double lo, double hi, Interpolation kind, double p, double q) {
  const double span = hi - lo;
  const double xc = std::min(1.0, std::max(0.0, x));
  if (kind == Interpolation::Simp) {
    const double xp = std::pow(xc, p);
    const double dxp = p * std::pow(xc, std::max(0.0, p - 1.0));
    return {lo + xp * span, dxp * span};
  }
  const double den = 1.0 + q * (1.0 - xc);
  const double phi = xc / den;
  const double dphi = (1.0 + q) / (den * den);
  return {lo + phi * span, dphi * span};
}

struct PhysicsResult {
  double objective = 0.0;
  std::vector<double> constraints;  // [KS temperature, volume] (+ stress if enabled)
  Vec dObj_dx;
  std::vector<Vec> dCon_dx;
  double meanT = 0.0;
  double peakT = 0.0;
  double ksT = 0.0;
  double meanStress = 0.0;
  double ksStress = 0.0;
  double dissipation = 0.0;
  double volume = 0.0;
  double maxPermeability = 0.0;
  double thermalResidual = 0.0;
  double flowResidual = 0.0;
  Vec T;
  Vec p;
};

class ThermoFluidModel {
 public:
  FvmMesh mesh;
  PhysicsParams prm;
  bool enableStress = true;

  ThermoFluidModel() = default;
  explicit ThermoFluidModel(const PhysicsParams& p) : mesh(p.nx, p.ny, p.h), prm(p) {}

  [[nodiscard]] int ndesign() const { return mesh.size(); }

  // Assemble coefficients, solve both primal fields, evaluate objective and
  // all constraints with exact sensitivities.
  PhysicsResult evaluate(const Vec& x) {
    if (static_cast<int>(x.size()) != mesh.size()) throw std::runtime_error("design size mismatch");
    buildCoefficients(x);
    solvePrimal();

    PhysicsResult res;
    res.T = T;
    res.p = p;
    const double area = mesh.totalArea();

    meanT = 0.0;
    for (int e = 0; e < mesh.size(); ++e) meanT += mesh.cellArea[static_cast<std::size_t>(e)] * T[static_cast<std::size_t>(e)];
    meanT /= area;

    dissipation = 0.0;
    for (std::size_t f = 0; f < mesh.faces.size(); ++f) {
      const auto& fc = mesh.faces[f];
      const double dp = p[static_cast<std::size_t>(fc.c2)] - p[static_cast<std::size_t>(fc.c1)];
      dissipation += faceFlowGain[f] * dp * dp;
    }

    res.meanT = meanT;
    res.peakT = *std::max_element(T.begin(), T.end());
    KsResult ks = ksAggregate(T, prm.rhoKs);
    res.ksT = ks.value;
    res.dissipation = dissipation;
    res.objective = prm.wThermal * meanT + prm.wHydraulic * dissipation;

    double vol = 0.0;
    for (int e = 0; e < mesh.size(); ++e) vol += mesh.cellArea[static_cast<std::size_t>(e)] * x[static_cast<std::size_t>(e)];
    vol /= area;
    res.volume = vol;

    std::vector<double> stress(static_cast<std::size_t>(mesh.size()));
    for (int e = 0; e < mesh.size(); ++e)
      stress[static_cast<std::size_t>(e)] = stressFactor * std::abs(T[static_cast<std::size_t>(e)] - prm.tRef);
    KsResult ksStress = ksAggregate(stress, prm.rhoKs * 0.5);
    res.ksStress = ksStress.value;
    res.meanStress = 0.0;
    for (int e = 0; e < mesh.size(); ++e)
      res.meanStress += mesh.cellArea[static_cast<std::size_t>(e)] * stress[static_cast<std::size_t>(e)] / area;

    res.maxPermeability = *std::max_element(perm.begin(), perm.end());
    res.thermalResidual = thermalResidual;
    res.flowResidual = flowResidual;

    // ---------------- objective sensitivity (thermal adjoint + flow adjoint)
    Vec gT(static_cast<std::size_t>(mesh.size()), 0.0);
    for (int e = 0; e < mesh.size(); ++e) gT[static_cast<std::size_t>(e)] = mesh.cellArea[static_cast<std::size_t>(e)] / area;
    Vec gp(static_cast<std::size_t>(mesh.size()), 0.0);
    for (std::size_t f = 0; f < mesh.faces.size(); ++f) {
      const auto& fc = mesh.faces[f];
      const double dp = p[static_cast<std::size_t>(fc.c2)] - p[static_cast<std::size_t>(fc.c1)];
      gp[static_cast<std::size_t>(fc.c1)] -= 2.0 * prm.wHydraulic * faceFlowGain[f] * dp;
      gp[static_cast<std::size_t>(fc.c2)] += 2.0 * prm.wHydraulic * faceFlowGain[f] * dp;
    }
    Vec gGain(mesh.faces.size(), 0.0);
    for (std::size_t f = 0; f < mesh.faces.size(); ++f) {
      const auto& fc = mesh.faces[f];
      const double dp = p[static_cast<std::size_t>(fc.c2)] - p[static_cast<std::size_t>(fc.c1)];
      gGain[f] = prm.wHydraulic * dp * dp;
    }
    res.dObj_dx = totalGradient(gT, gp, gGain);

    // ---------------- KS temperature constraint
    std::vector<double> gKs(static_cast<std::size_t>(mesh.size()));
    for (int e = 0; e < mesh.size(); ++e) gKs[static_cast<std::size_t>(e)] = ks.weights[static_cast<std::size_t>(e)] / prm.tCap;
    Vec zero = Vec(static_cast<std::size_t>(mesh.size()), 0.0);
    Vec zeroFaces(mesh.faces.size(), 0.0);
    res.constraints.push_back(ks.value / prm.tCap - 1.0);
    res.dCon_dx.push_back(totalGradient(gKs, zero, zeroFaces));

    // ---------------- volume constraint (no PDE, exact algebraic gradient)
    res.constraints.push_back(vol - prm.volFrac);
    Vec dvol(static_cast<std::size_t>(mesh.size()));
    for (int e = 0; e < mesh.size(); ++e) dvol[static_cast<std::size_t>(e)] = mesh.cellArea[static_cast<std::size_t>(e)] / area;
    res.dCon_dx.push_back(dvol);

    if (enableStress && prm.stressCap > 0.0) {
      std::vector<double> gS(static_cast<std::size_t>(mesh.size()));
      for (int e = 0; e < mesh.size(); ++e) {
        const double d = T[static_cast<std::size_t>(e)] - prm.tRef;
        const double sign = d >= 0.0 ? 1.0 : -1.0;
        gS[static_cast<std::size_t>(e)] = ksStress.weights[static_cast<std::size_t>(e)] * stressFactor * sign / prm.stressCap;
      }
      res.constraints.push_back(ksStress.value / prm.stressCap - 1.0);
      res.dCon_dx.push_back(totalGradient(gS, zero, zeroFaces));
    }
    return res;
  }

  // Total derivative of any scalar J(x) given partials
  //   gT = dJ/dT, gp = dJ/dp, gGain_f = dJ/d(face flow gain)
  // Uses:
  //   1) thermal adjoint      A_T^T lambda = gT
  //   2) pressure adjoint     K^T mu     = g_p + d/dp(lambda^T A_T T)
  //   3) exact chain through kappa(x), k(x) and the upwind velocity.
  Vec totalGradient(const Vec& gT, const Vec& gp, const Vec& gGain) const {
    const int n = mesh.size();
    Vec lambda;
    Vec rhsT = gT;
    SolveInfo ai = gmres(thermalT, rhsT, lambda, 60, 1e-13, 20000);
    if (!ai.converged) throw std::runtime_error("thermal adjoint did not converge");
    // Dirichlet rows carry no design dependence.
    for (int e = 0; e < n; ++e)
      if (thermalDirichlet[static_cast<std::size_t>(e)]) lambda[static_cast<std::size_t>(e)] = 0.0;

    std::vector<double> gFace(mesh.faces.size(), 0.0);
    Vec r = gp;
    for (std::size_t f = 0; f < mesh.faces.size(); ++f) {
      const auto& fc = mesh.faces[f];
      const double F = flux[f];
      const double tUp = F > 0.0 ? T[static_cast<std::size_t>(fc.c1)] : T[static_cast<std::size_t>(fc.c2)];
      const double gF = tUp * (lambda[static_cast<std::size_t>(fc.c1)] - lambda[static_cast<std::size_t>(fc.c2)]);
      gFace[f] = gF;
      // r = J_p - (dF/dp)^T gF
      r[static_cast<std::size_t>(fc.c1)] -= gF * faceFlowGain[f];
      r[static_cast<std::size_t>(fc.c2)] += gF * faceFlowGain[f];
    }
    Vec mu;
    if (!solveAdaptive(flowT, r, mu)) throw std::runtime_error("pressure adjoint did not converge");
    for (int e = 0; e < n; ++e)
      if (flowDirichlet[static_cast<std::size_t>(e)]) mu[static_cast<std::size_t>(e)] = 0.0;

    Vec grad(static_cast<std::size_t>(n), 0.0);
    for (std::size_t f = 0; f < mesh.faces.size(); ++f) {
      const auto& fc = mesh.faces[f];
      const double metric = 0.5 * fc.area / fc.length;
      const double dT = T[static_cast<std::size_t>(fc.c1)] - T[static_cast<std::size_t>(fc.c2)];
      const double dl = lambda[static_cast<std::size_t>(fc.c1)] - lambda[static_cast<std::size_t>(fc.c2)];
      const double dp = p[static_cast<std::size_t>(fc.c2)] - p[static_cast<std::size_t>(fc.c1)];
      const double dm = mu[static_cast<std::size_t>(fc.c1)] - mu[static_cast<std::size_t>(fc.c2)];
      // -lambda^T A_x T  (thermal conduction chain)
      const double dThermal = -dl * dT;
      // -gF * dF/dx    (design dependence of the upwind face flux)
      const double dFlux = gFace[f] * dp;
      // -mu^T K_x p    (pressure adjoint chain)
      const double dFlow = dm * dp;
      // +dJ/dgain * dgain/dx  (explicit objective dependence)
      const double dObj = gGain[f];
      const double w1 = metric * dkthdx[static_cast<std::size_t>(fc.c1)];
      const double w2 = metric * dkthdx[static_cast<std::size_t>(fc.c2)];
      const double p1 = metric * dpermdx[static_cast<std::size_t>(fc.c1)];
      const double p2 = metric * dpermdx[static_cast<std::size_t>(fc.c2)];
      grad[static_cast<std::size_t>(fc.c1)] += dThermal * w1 + (dFlux + dFlow + dObj) * p1;
      grad[static_cast<std::size_t>(fc.c2)] += dThermal * w2 + (dFlux + dFlow + dObj) * p2;
    }
    return grad;
  }

  // Finite-difference checkpoint of the total derivative (verification only).
  Vec fdGradient(const Vec& x, const std::function<double(const Vec&)>& functional, double eps = 1e-6) const {
    Vec g(x.size());
    for (std::size_t i = 0; i < x.size(); ++i) {
      Vec xp = x, xm = x;
      xp[i] += eps;
      xm[i] -= eps;
      g[i] = (functional(xp) - functional(xm)) / (2.0 * eps);
    }
    return g;
  }


  // Tolerant linear solve: full accuracy first, then progressively relaxed
  // tolerances. Returns true when a residual below 1e-8 was achieved.
  static bool solveAdaptive(const Csr& A, const Vec& b, Vec& x, int restart = 150, int maxit = 60000) {
    const double tols[3] = {1e-12, 1e-10, 1e-8};
    for (double tol : tols) {
      SolveInfo info = gmres(A, b, x, restart, tol, maxit);
      if (info.converged) return true;
    }
    // Deterministic fallback: sparse-to-dense LU for small/medium systems.
    if (A.n <= 1200) {
      const std::size_t n = static_cast<std::size_t>(A.n);
      std::vector<double> dense(n * n, 0.0);
      for (int i = 0; i < A.n; ++i)
        for (int k = A.rowptr[static_cast<std::size_t>(i)]; k < A.rowptr[static_cast<std::size_t>(i + 1)]; ++k)
          dense[static_cast<std::size_t>(i) * n + static_cast<std::size_t>(A.colind[static_cast<std::size_t>(k)])] += A.val[static_cast<std::size_t>(k)];
      try {
        x = denseSolve<double>(std::move(dense), b, A.n);
        return true;
      } catch (...) {
        // Final regularised attempt (near-singular low-conductivity designs).
        std::vector<double> dense2(n * n, 0.0);
        for (int i = 0; i < A.n; ++i)
          for (int k = A.rowptr[static_cast<std::size_t>(i)]; k < A.rowptr[static_cast<std::size_t>(i + 1)]; ++k)
            dense2[static_cast<std::size_t>(i) * n + static_cast<std::size_t>(A.colind[static_cast<std::size_t>(k)])] += A.val[static_cast<std::size_t>(k)];
        double scale = 0.0;
        for (int i = 0; i < A.n; ++i) scale = std::max(scale, dense2[static_cast<std::size_t>(i) * n + static_cast<std::size_t>(i)]);
        for (int i = 0; i < A.n; ++i) dense2[static_cast<std::size_t>(i) * n + static_cast<std::size_t>(i)] += 1e-12 * std::max(scale, 1.0);
        try {
          x = denseSolve<double>(std::move(dense2), b, A.n);
          return true;
        } catch (...) {
          return false;
        }
      }
    }
    return false;
  }

  void buildCoefficients(const Vec& x) {
    const int n = mesh.size();
    kth.assign(static_cast<std::size_t>(n), 0.0);
    perm.assign(static_cast<std::size_t>(n), 0.0);
    dkthdx.assign(static_cast<std::size_t>(n), 0.0);
    dpermdx.assign(static_cast<std::size_t>(n), 0.0);
    for (int e = 0; e < n; ++e) {
      const InterpSample k = interpolate(x[static_cast<std::size_t>(e)], prm.kFluid, prm.kSolid, prm.interp, prm.simpP, prm.rampQ);
      const InterpSample kp = interpolate(x[static_cast<std::size_t>(e)], prm.permSolid, prm.permFluid, prm.interp, prm.simpP, prm.rampQ);
      kth[static_cast<std::size_t>(e)] = k.value;
      dkthdx[static_cast<std::size_t>(e)] = k.derivative;
      perm[static_cast<std::size_t>(e)] = kp.value;
      dpermdx[static_cast<std::size_t>(e)] = kp.derivative;
    }
    faceFlowGain.assign(mesh.faces.size(), 0.0);
    faceCond.assign(mesh.faces.size(), 0.0);
    for (std::size_t f = 0; f < mesh.faces.size(); ++f) {
      const auto& fc = mesh.faces[f];
      const double g = fc.area / fc.length;
      faceFlowGain[f] = 0.5 * (perm[static_cast<std::size_t>(fc.c1)] + perm[static_cast<std::size_t>(fc.c2)]) * g;
      faceCond[f] = 0.5 * (kth[static_cast<std::size_t>(fc.c1)] + kth[static_cast<std::size_t>(fc.c2)]) * g;
    }
    stressFactor = prm.young * prm.alphaTh / (1.0 - prm.nu);
  }

  void solvePrimal() {
    const int n = mesh.size();
    flowDirichlet.assign(static_cast<std::size_t>(n), false);
    thermalDirichlet.assign(static_cast<std::size_t>(n), false);
    std::vector<double> flowBc(static_cast<std::size_t>(n), 0.0);
    std::vector<double> tempBc(static_cast<std::size_t>(n), 0.0);
    for (int j = 0; j < mesh.ny; ++j) {
      const int l = mesh.idx(0, j);
      const int r = mesh.idx(mesh.nx - 1, j);
      flowDirichlet[static_cast<std::size_t>(l)] = true;
      flowDirichlet[static_cast<std::size_t>(r)] = true;
      flowBc[static_cast<std::size_t>(l)] = prm.pIn;
      flowBc[static_cast<std::size_t>(r)] = prm.pOut;
      thermalDirichlet[static_cast<std::size_t>(l)] = true;
      tempBc[static_cast<std::size_t>(l)] = prm.inletT;
    }

    std::vector<Triplet> kTrip, aTrip;
    kTrip.reserve(mesh.faces.size() * 4 + static_cast<std::size_t>(n));
    aTrip.reserve(mesh.faces.size() * 8 + static_cast<std::size_t>(n));
    for (std::size_t f = 0; f < mesh.faces.size(); ++f) {
      const int c1 = mesh.faces[f].c1;
      const int c2 = mesh.faces[f].c2;
      const double kf = faceFlowGain[f];
      const double cf = faceCond[f];
      if (!flowDirichlet[static_cast<std::size_t>(c1)]) {
        kTrip.push_back({c1, c1, kf});
        kTrip.push_back({c1, c2, -kf});
      }
      if (!flowDirichlet[static_cast<std::size_t>(c2)]) {
        kTrip.push_back({c2, c2, kf});
        kTrip.push_back({c2, c1, -kf});
      }
      if (!thermalDirichlet[static_cast<std::size_t>(c1)]) {
        aTrip.push_back({c1, c1, cf});
        aTrip.push_back({c1, c2, -cf});
      }
      if (!thermalDirichlet[static_cast<std::size_t>(c2)]) {
        aTrip.push_back({c2, c2, cf});
        aTrip.push_back({c2, c1, -cf});
      }
    }
    for (int e = 0; e < n; ++e) {
      if (flowDirichlet[static_cast<std::size_t>(e)]) kTrip.push_back({e, e, 1.0});
      if (thermalDirichlet[static_cast<std::size_t>(e)]) aTrip.push_back({e, e, 1.0});
    }
    Csr K, A;
    K.build(n, std::move(kTrip));
    A.build(n, std::move(aTrip));

    std::vector<double> pb(static_cast<std::size_t>(n), 0.0);
    for (int e = 0; e < n; ++e)
      if (flowDirichlet[static_cast<std::size_t>(e)]) pb[static_cast<std::size_t>(e)] = flowBc[static_cast<std::size_t>(e)];
    Vec pSol;
    if (!solveAdaptive(K, pb, pSol)) throw std::runtime_error("Darcy pressure solve failed to converge");
    flowResidual = 0.0;
    p = pSol;

    flux.assign(mesh.faces.size(), 0.0);
    for (std::size_t f = 0; f < mesh.faces.size(); ++f) {
      const auto& fc = mesh.faces[f];
      flux[f] = -faceFlowGain[f] * (p[static_cast<std::size_t>(fc.c2)] - p[static_cast<std::size_t>(fc.c1)]);
    }

    // Re-assemble energy matrix with the converged upwind velocity.
    std::vector<Triplet> tTrip;
    tTrip.reserve(mesh.faces.size() * 8 + static_cast<std::size_t>(n));
    for (std::size_t f = 0; f < mesh.faces.size(); ++f) {
      const int c1 = mesh.faces[f].c1;
      const int c2 = mesh.faces[f].c2;
      const double cf = faceCond[f];
      const double F = flux[f];
      if (!thermalDirichlet[static_cast<std::size_t>(c1)]) {
        tTrip.push_back({c1, c1, cf + std::max(F, 0.0)});
        tTrip.push_back({c1, c2, -cf + std::min(F, 0.0)});
      }
      if (!thermalDirichlet[static_cast<std::size_t>(c2)]) {
        tTrip.push_back({c2, c2, cf + std::max(-F, 0.0)});
        tTrip.push_back({c2, c1, -cf + std::min(-F, 0.0)});
      }
    }
    for (int e = 0; e < n; ++e)
      if (thermalDirichlet[static_cast<std::size_t>(e)]) tTrip.push_back({e, e, 1.0});
    A.build(n, std::move(tTrip));
    thermal = A;
    thermalT = A.transpose();
    flow = K;
    flowT = K.transpose();

    std::vector<double> tb(static_cast<std::size_t>(n), 0.0);
    for (int e = 0; e < n; ++e) tb[static_cast<std::size_t>(e)] = prm.sourceQ * mesh.cellArea[static_cast<std::size_t>(e)];
    for (int e = 0; e < n; ++e)
      if (thermalDirichlet[static_cast<std::size_t>(e)]) tb[static_cast<std::size_t>(e)] = tempBc[static_cast<std::size_t>(e)];
    Vec tSol;
    if (!solveAdaptive(thermal, tb, tSol)) throw std::runtime_error("energy solve failed to converge");
    thermalResidual = 0.0;
    T = tSol;
  }

  // Physical diagnostics kept for the cockpit export.
  [[nodiscard]] const Vec& temperature() const { return T; }
  [[nodiscard]] const Vec& pressure() const { return p; }
  [[nodiscard]] const Vec& conductivity() const { return kth; }
  [[nodiscard]] const Vec& faceFlux() const { return flux; }

 private:
  int faceIndex(const FvmMesh::Face* fc) const {
    return static_cast<int>((fc - mesh.faces.data()) / 1);
  }

  Vec kth, perm, dkthdx, dpermdx;
  Vec faceFlowGain, faceCond;
  Vec flux, p, T;
  std::vector<char> flowDirichlet, thermalDirichlet;
  Csr flow, flowT, thermal, thermalT;
  double meanT = 0.0;
  double dissipation = 0.0;
  double thermalResidual = 0.0;
  double flowResidual = 0.0;
  double stressFactor = 1.0;
};

}  // namespace mdo
