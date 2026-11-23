// SPDX-License-Identifier: MIT
// Generalized First-Order Second-Moment (FOSM) robust optimisation without
// Monte Carlo sampling (cf. Kranz, Luedeker & Kriegesmann 2023, TUHH).
//
//   xi ~ N(0, Sigma),  Sigma_ij = sigma_xi^2 exp(-||x_i - x_j|| / l_corr)
//   Phi(gamma) = mu_g(gamma) + kappa * sigma_g(gamma)
//   sigma_g^2  = (dg/dxi)^T Sigma (dg/dxi)
//
// The uncertainty perturbs the material coefficient field multiplicatively,
//   k_e(gamma, xi) = k_e(gamma) * exp(sigma_xi * xi_e),
// so the state equation A(gamma, xi) T = b keeps an exact tangent.
//
// Exact sigma sensitivity (no MC, no finite differences):
//   lambda = A^{-T} dg/dT                     (nominal adjoint)
//   d      = dg/dxi = -lambda^T (dA/dxi) T    (one parameter sweep)
//   v      = Sigma d / sigma
//   mu_v   = A^{-1} sum_i v_i (dA/dxi_i) T    (vector-Hessian product, tangent side)
//   eta_v  = A^{-1} sum_i v_i (dA/dxi_i) lambda
//   dsigma/dgamma_j = (1/sigma) * ( eta_v^T A_{gamma_j} T + lambda^T A_{gamma_j} mu_v )
// i.e. exactly one extra second-order adjoint pair, no MC and no FD.
#pragma once

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <random>
#include <stdexcept>
#include <vector>

#include "mdo/fvm.hpp"
#include "mdo/linalg.hpp"

namespace mdo {

inline Mat exponentialCovariance(const FvmMesh& mesh, double sigmaXi, double corrLength) {
  const int n = mesh.size();
  Mat S(static_cast<std::size_t>(n), Vec(static_cast<std::size_t>(n), 0.0));
  for (int i = 0; i < n; ++i) {
    const double xi = (i % mesh.nx + 0.5) * mesh.h;
    const double yi = (i / mesh.nx + 0.5) * mesh.h;
    for (int j = 0; j < n; ++j) {
      const double xj = (j % mesh.nx + 0.5) * mesh.h;
      const double yj = (j / mesh.nx + 0.5) * mesh.h;
      const double dist = std::sqrt((xi - xj) * (xi - xj) + (yi - yj) * (yi - yj));
      S[static_cast<std::size_t>(i)][static_cast<std::size_t>(j)] = sigmaXi * sigmaXi * std::exp(-dist / corrLength);
    }
  }
  return S;
}

inline Mat choleskyDense(const Mat& A) {
  const int n = static_cast<int>(A.size());
  Mat L(static_cast<std::size_t>(n), Vec(static_cast<std::size_t>(n), 0.0));
  for (int i = 0; i < n; ++i) {
    for (int j = 0; j <= i; ++j) {
      double s = A[static_cast<std::size_t>(i)][static_cast<std::size_t>(j)];
      for (int k = 0; k < j; ++k) s -= L[static_cast<std::size_t>(i)][static_cast<std::size_t>(k)] * L[static_cast<std::size_t>(j)][static_cast<std::size_t>(k)];
      if (i == j)
        L[static_cast<std::size_t>(i)][static_cast<std::size_t>(j)] = std::sqrt(std::max(s, 1e-14));
      else
        L[static_cast<std::size_t>(i)][static_cast<std::size_t>(j)] = s / L[static_cast<std::size_t>(j)][static_cast<std::size_t>(j)];
    }
  }
  return L;
}

struct FosmEvaluation {
  double gNominal = 0.0;
  double sigma = 0.0;
  double robust = 0.0;
  Vec dGdXi;
  Vec dMuDgamma;
  Vec dSigmaDgamma;
  Vec dRobustDgamma;
  double peakT = 0.0;
  int primalSolves = 0;
  int adjointSolves = 0;
};

class RobustConductionModel {
 public:
  RobustConductionModel(const FvmMesh& mesh, double kFluid, double kSolid, double simpP, double sigmaXi,
                        double corrLength, double rhoKs, double kappa, double sourceQ)
      : mesh_(mesh), kFluid_(kFluid), kSolid_(kSolid), p_(simpP), sigmaXi_(sigmaXi), rhoKs_(rhoKs), kappa_(kappa), sourceQ_(sourceQ) {
    covariance_ = exponentialCovariance(mesh_, sigmaXi_, corrLength);
  }

  [[nodiscard]] int ndesign() const { return mesh_.size(); }
  [[nodiscard]] const Mat& covariance() const { return covariance_; }

  [[nodiscard]] double conductivity(double x) const {
    const double xc = std::min(1.0, std::max(0.0, x));
    return kFluid_ + std::pow(xc, p_) * (kSolid_ - kFluid_);
  }
  [[nodiscard]] double dConductivity(double x) const {
    const double xc = std::min(1.0, std::max(0.0, x));
    return p_ * std::pow(xc, std::max(0.0, p_ - 1.0)) * (kSolid_ - kFluid_);
  }

  struct System {
    Csr A;
    Vec b;
    std::vector<char> dirichlet;
    std::vector<double> k;
    Vec faceCond;
  };

  // Assembles A(gamma, xi) with a multiplicative log-normal material field.
  System assemble(const Vec& x, const Vec& xi) const {
    const int n = mesh_.size();
    System sys;
    sys.dirichlet.assign(static_cast<std::size_t>(n), false);
    sys.k.resize(static_cast<std::size_t>(n));
    std::vector<double> flowScale(static_cast<std::size_t>(n), 1.0);
    for (int e = 0; e < n; ++e) {
      const double xc = std::min(1.0, std::max(0.0, x[static_cast<std::size_t>(e)]));
      sys.k[static_cast<std::size_t>(e)] = conductivity(xc);
      if (!xi.empty()) sys.k[static_cast<std::size_t>(e)] *= std::exp(sigmaXi_ * xi[static_cast<std::size_t>(e)]);
    }
    sys.faceCond.assign(mesh_.faces.size(), 0.0);
    for (std::size_t f = 0; f < mesh_.faces.size(); ++f) {
      const auto& fc = mesh_.faces[f];
      sys.faceCond[f] = 0.5 * (sys.k[static_cast<std::size_t>(fc.c1)] + sys.k[static_cast<std::size_t>(fc.c2)]) * fc.area / fc.length;
    }
    for (int j = 0; j < mesh_.ny; ++j) sys.dirichlet[static_cast<std::size_t>(mesh_.idx(0, j))] = true;
    std::vector<Triplet> trip;
    trip.reserve(mesh_.faces.size() * 4 + static_cast<std::size_t>(n));
    for (std::size_t f = 0; f < mesh_.faces.size(); ++f) {
      const auto& fc = mesh_.faces[f];
      const double c = sys.faceCond[f];
      if (!sys.dirichlet[static_cast<std::size_t>(fc.c1)]) {
        trip.push_back({fc.c1, fc.c1, c});
        trip.push_back({fc.c1, fc.c2, -c});
      }
      if (!sys.dirichlet[static_cast<std::size_t>(fc.c2)]) {
        trip.push_back({fc.c2, fc.c2, c});
        trip.push_back({fc.c2, fc.c1, -c});
      }
    }
    for (int e = 0; e < n; ++e)
      if (sys.dirichlet[static_cast<std::size_t>(e)]) trip.push_back({e, e, 1.0});
    sys.A.build(n, std::move(trip));
    sys.b.assign(static_cast<std::size_t>(n), 0.0);
    for (int e = 0; e < n; ++e) sys.b[static_cast<std::size_t>(e)] = sourceQ_ * mesh_.cellArea[static_cast<std::size_t>(e)];
    for (int e = 0; e < n; ++e)
      if (sys.dirichlet[static_cast<std::size_t>(e)]) sys.b[static_cast<std::size_t>(e)] = 0.0;
    return sys;
  }

  [[nodiscard]] Vec solve(const System& sys) const {
    Vec T;
    SolveInfo info = gmres(sys.A, sys.b, T, 60, 1e-12, 20000);
    if (!info.converged) throw std::runtime_error("FOSM conduction solve failed");
    return T;
  }

  [[nodiscard]] bool solveSystem(Vec& out, const Csr& A, const Vec& b) const {
    SolveInfo info = gmres(A, b, out, 60, 1e-12, 20000);
    if (!info.converged) info = gmres(A, b, out, 120, 1e-10, 40000);
    return info.converged;
  }

  // Local face metric q_f = (u_c1-u_c2)(w_c1-w_c2) accumulated per cell, with the
  // Dirichlet compatibility mask applied to the first argument.
  [[nodiscard]] Vec faceAccumulate(const Vec& u, const Vec& w, const std::vector<char>& mask) const {
    Vec acc(static_cast<std::size_t>(mesh_.size()), 0.0);
    for (std::size_t f = 0; f < mesh_.faces.size(); ++f) {
      const auto& fc = mesh_.faces[f];
      const double u1 = mask[static_cast<std::size_t>(fc.c1)] ? 0.0 : u[static_cast<std::size_t>(fc.c1)];
      const double u2 = mask[static_cast<std::size_t>(fc.c2)] ? 0.0 : u[static_cast<std::size_t>(fc.c2)];
      const double q = (u1 - u2) * (w[static_cast<std::size_t>(fc.c1)] - w[static_cast<std::size_t>(fc.c2)]) *
                       (0.5 * fc.area / fc.length);
      acc[static_cast<std::size_t>(fc.c1)] += q;
      acc[static_cast<std::size_t>(fc.c2)] += q;
    }
    return acc;
  }

  [[nodiscard]] double temperatureObjective(const Vec& T) const { return ksAggregate(T, rhoKs_).value; }

  // Exact sigma gradient and robust objective at design x (nominal parameter set).
  FosmEvaluation evaluate(const Vec& x) const {
    const int n = mesh_.size();
    FosmEvaluation out;
    const Vec xi0(static_cast<std::size_t>(n), 0.0);
    System sys = assemble(x, xi0);
    Vec T = solve(sys);
    out.primalSolves = 1;
    KsResult ks = ksAggregate(T, rhoKs_);
    out.gNominal = ks.value;
    out.peakT = *std::max_element(T.begin(), T.end());

    // ---- nominal adjoint: A lambda = dg/dT
    Vec gT = ks.weights;
    Vec lambda;
    SolveInfo li = gmres(sys.A, gT, lambda, 60, 1e-12, 20000);
    if (!li.converged) throw std::runtime_error("FOSM nominal adjoint failed");
    for (int e = 0; e < n; ++e)
      if (sys.dirichlet[static_cast<std::size_t>(e)]) lambda[static_cast<std::size_t>(e)] = 0.0;
    out.adjointSolves = 1;

    // ---- dg/dxi: one parameter sweep, using the same adjoint
    Vec dGdXi(static_cast<std::size_t>(n), 0.0);
    for (int i = 0; i < n; ++i) {
      double acc = 0.0;
      for (std::size_t f = 0; f < mesh_.faces.size(); ++f) {
        const auto& fc = mesh_.faces[f];
        if (fc.c1 != i && fc.c2 != i) continue;
        const double l1 = sys.dirichlet[static_cast<std::size_t>(fc.c1)] ? 0.0 : lambda[static_cast<std::size_t>(fc.c1)];
        const double l2 = sys.dirichlet[static_cast<std::size_t>(fc.c2)] ? 0.0 : lambda[static_cast<std::size_t>(fc.c2)];
        const double metric = fc.area / fc.length;
        const double dC = 0.5 * sigmaXi_ * sys.k[static_cast<std::size_t>(i)] * metric;
        acc -= (l1 - l2) * (T[static_cast<std::size_t>(fc.c1)] - T[static_cast<std::size_t>(fc.c2)]) * dC;
      }
      dGdXi[static_cast<std::size_t>(i)] = acc;
    }
    out.dGdXi = dGdXi;

    // ---- sigma = sqrt(d^T Sigma d)
    const Vec SigD = matVec(covariance_, dGdXi);
    double var = std::max(dot(dGdXi, SigD), 0.0);
    out.sigma = std::sqrt(var);
    out.robust = out.gNominal + kappa_ * out.sigma;

    // ---- dsigma/dgamma with TWO extra linear solves (tangent-over-adjoint VHP)
    Vec vEta(static_cast<std::size_t>(n), 0.0);
    if (out.sigma > 1e-300) {
      for (int i = 0; i < n; ++i) vEta[static_cast<std::size_t>(i)] = SigD[static_cast<std::size_t>(i)] / out.sigma;
    }
    // sum_i v_i (dA/dxi_i) T  and  sum_i v_i (dA/dxi_i) lambda
    Vec tVec(static_cast<std::size_t>(n), 0.0), sVec(static_cast<std::size_t>(n), 0.0);
    for (std::size_t f = 0; f < mesh_.faces.size(); ++f) {
      const auto& fc = mesh_.faces[f];
      const double metric = fc.area / fc.length;
      const double wsum = 0.5 * sigmaXi_ * metric *
                          (vEta[static_cast<std::size_t>(fc.c1)] * sys.k[static_cast<std::size_t>(fc.c1)] +
                           vEta[static_cast<std::size_t>(fc.c2)] * sys.k[static_cast<std::size_t>(fc.c2)]);
      const double dT = T[static_cast<std::size_t>(fc.c1)] - T[static_cast<std::size_t>(fc.c2)];
      const double l1 = sys.dirichlet[static_cast<std::size_t>(fc.c1)] ? 0.0 : lambda[static_cast<std::size_t>(fc.c1)];
      const double l2 = sys.dirichlet[static_cast<std::size_t>(fc.c2)] ? 0.0 : lambda[static_cast<std::size_t>(fc.c2)];
      if (!sys.dirichlet[static_cast<std::size_t>(fc.c1)]) {
        tVec[static_cast<std::size_t>(fc.c1)] += wsum * dT;
        sVec[static_cast<std::size_t>(fc.c1)] += wsum * (l1 - l2);
      }
      if (!sys.dirichlet[static_cast<std::size_t>(fc.c2)]) {
        tVec[static_cast<std::size_t>(fc.c2)] -= wsum * dT;
        sVec[static_cast<std::size_t>(fc.c2)] -= wsum * (l1 - l2);
      }
    }
    // Dirichlet rows hold identity entries, so their tangent RHS is zero.
    for (int e = 0; e < n; ++e) {
      if (sys.dirichlet[static_cast<std::size_t>(e)]) {
        tVec[static_cast<std::size_t>(e)] = 0.0;
        sVec[static_cast<std::size_t>(e)] = 0.0;
      }
    }
    Vec muVec, etaVec;
    SolveInfo mi = gmres(sys.A, tVec, muVec, 60, 1e-12, 20000);
    SolveInfo ei = gmres(sys.A, sVec, etaVec, 60, 1e-12, 20000);
    if (!mi.converged || !ei.converged) throw std::runtime_error("FOSM second-order adjoint failed");
    out.adjointSolves += 2;

    const Vec accSigma = faceAccumulate(etaVec, T, sys.dirichlet);
    const Vec accSigma2 = faceAccumulate(lambda, muVec, sys.dirichlet);
    const Vec accMu = faceAccumulate(lambda, T, sys.dirichlet);
    // Curvature of the KS functional enters through d(lambda)/d(gamma):
    //   zeta = A^{-1} (diag(w) - w w^T) mu  with w = dKS/dT, giving the extra
    //   term zeta^T A_gamma T in the sigma sensitivity.
    double wMu = 0.0;
    for (int e = 0; e < n; ++e) wMu += ks.weights[static_cast<std::size_t>(e)] * muVec[static_cast<std::size_t>(e)];
    Vec hKs(static_cast<std::size_t>(n), 0.0);
    for (int e = 0; e < n; ++e)
      hKs[static_cast<std::size_t>(e)] = ks.weights[static_cast<std::size_t>(e)] *
                                         (muVec[static_cast<std::size_t>(e)] - wMu);
    Vec zeta;
    const bool haveZeta = solveSystem(zeta, sys.A, hKs);
    const Vec accZeta = haveZeta ? faceAccumulate(zeta, T, sys.dirichlet) : Vec(static_cast<std::size_t>(n), 0.0);
    out.dMuDgamma.assign(static_cast<std::size_t>(n), 0.0);
    out.dSigmaDgamma.assign(static_cast<std::size_t>(n), 0.0);
    out.dRobustDgamma.assign(static_cast<std::size_t>(n), 0.0);
    for (int e = 0; e < n; ++e) {
      const double dk = dConductivity(x[static_cast<std::size_t>(e)]);
      out.dMuDgamma[static_cast<std::size_t>(e)] = -accMu[static_cast<std::size_t>(e)] * dk;
      // The sigma tangent includes the explicit design dependence of dA/dxi
      // (the perturbed material coefficient itself depends on gamma).
      const double explicitTerm = (sys.k[static_cast<std::size_t>(e)] > 1e-300)
                                      ? vEta[static_cast<std::size_t>(e)] * dGdXi[static_cast<std::size_t>(e)] * dk /
                                            sys.k[static_cast<std::size_t>(e)]
                                      : 0.0;
      // dsigma/dgamma = lambda^T A_gamma mu + eta^T A_gamma T + d_explicit,
      // where mu and eta already carry the 1/sigma weight (v = Sigma d / sigma).
      const double dSig = (out.sigma > 1e-300)
                              ? (accSigma[static_cast<std::size_t>(e)] + accSigma2[static_cast<std::size_t>(e)] +
                                 accZeta[static_cast<std::size_t>(e)]) * dk + explicitTerm
                              : 0.0;
      out.dSigmaDgamma[static_cast<std::size_t>(e)] = dSig;
      out.dRobustDgamma[static_cast<std::size_t>(e)] = out.dMuDgamma[static_cast<std::size_t>(e)] + kappa_ * dSig;
    }
    return out;
  }

  // ---- Monte Carlo reference (verification only, deliberately brute force)
  struct McResult {
    double sigma = 0.0;
    double mean = 0.0;
    int samples = 0;
    double seconds = 0.0;
    std::vector<double> sigmaSensitivity;
    std::vector<double> exactSensitivity;
    std::vector<double> relativeError;
    std::vector<int> directions;
  };

  McResult monteCarlo(const Vec& x, int samples, const std::vector<int>& directions, double fdStep = 2e-3) const {
    McResult out;
    out.samples = samples;
    out.directions = directions;
    const int n = mesh_.size();
    const Mat L = choleskyDense(covariance_);
    std::mt19937 rng(20240517u);
    std::normal_distribution<double> norm(0.0, 1.0);
    std::vector<Vec> xi(static_cast<std::size_t>(samples), Vec(static_cast<std::size_t>(n), 0.0));
    for (int s = 0; s < samples; ++s) {
      Vec z(static_cast<std::size_t>(n));
      for (int i = 0; i < n; ++i) z[static_cast<std::size_t>(i)] = norm(rng);
      for (int i = 0; i < n; ++i) {
        double acc = 0.0;
        for (int k = 0; k <= i; ++k) acc += L[static_cast<std::size_t>(i)][static_cast<std::size_t>(k)] * z[static_cast<std::size_t>(k)];
        xi[static_cast<std::size_t>(s)][static_cast<std::size_t>(i)] = acc;
      }
    }
    auto sigmaOf = [&](const Vec& xd) {
      double sum = 0.0, sum2 = 0.0;
      for (int s = 0; s < samples; ++s) {
        System sys = assemble(xd, xi[static_cast<std::size_t>(s)]);
        Vec T = solve(sys);
        const double g = temperatureObjective(T);
        sum += g;
        sum2 += g * g;
      }
      const double m = sum / samples;
      const double var = std::max(sum2 / samples - m * m, 0.0);
      out.mean = m;
      return std::sqrt(var);
    };
    const auto t0 = std::chrono::steady_clock::now();
    out.sigma = sigmaOf(x);
    for (int d : directions) {
      Vec xp = x, xm = x;
      xp[static_cast<std::size_t>(d)] += fdStep;
      xm[static_cast<std::size_t>(d)] = std::max(0.0, xm[static_cast<std::size_t>(d)] - fdStep);
      const double sp = sigmaOf(xp);
      const double sm = sigmaOf(xm);
      out.sigmaSensitivity.push_back((sp - sm) / (xp[static_cast<std::size_t>(d)] - xm[static_cast<std::size_t>(d)]));
    }
    const auto t1 = std::chrono::steady_clock::now();
    out.seconds = std::chrono::duration<double>(t1 - t0).count();
    const FosmEvaluation exact = evaluate(x);
    for (int d : directions) out.exactSensitivity.push_back(exact.dSigmaDgamma[static_cast<std::size_t>(d)]);
    for (std::size_t i = 0; i < out.sigmaSensitivity.size(); ++i) {
      const double den = std::max(std::abs(out.exactSensitivity[i]), 1e-12);
      out.relativeError.push_back(std::abs(out.sigmaSensitivity[i] - out.exactSensitivity[i]) / den);
    }
    return out;
  }

  // Design-independent deterministic verification that the assembled tangent is
  // correct: compare dg/dxi with central differences in xi space.
  double xiDerivativeError(const Vec& x, int probes = 4) const {
    const int n = mesh_.size();
    const Vec xi0(static_cast<std::size_t>(n), 0.0);
    System sys = assemble(x, xi0);
    Vec T = solve(sys);
    KsResult ks = ksAggregate(T, rhoKs_);
    Vec lambda;
    gmres(sys.A, ks.weights, lambda, 60, 1e-12, 20000);
    Vec dGdXi(static_cast<std::size_t>(n), 0.0);
    for (std::size_t f = 0; f < mesh_.faces.size(); ++f) {
      const auto& fc = mesh_.faces[f];
      const double l1 = sys.dirichlet[static_cast<std::size_t>(fc.c1)] ? 0.0 : lambda[static_cast<std::size_t>(fc.c1)];
      const double l2 = sys.dirichlet[static_cast<std::size_t>(fc.c2)] ? 0.0 : lambda[static_cast<std::size_t>(fc.c2)];
      const double metric = fc.area / fc.length;
      const double base = (l1 - l2) * (T[static_cast<std::size_t>(fc.c1)] - T[static_cast<std::size_t>(fc.c2)]) * (0.5 * sigmaXi_ * metric);
      dGdXi[static_cast<std::size_t>(fc.c1)] -= base * sys.k[static_cast<std::size_t>(fc.c1)];
      dGdXi[static_cast<std::size_t>(fc.c2)] -= base * sys.k[static_cast<std::size_t>(fc.c2)];
    }
    double worst = 0.0;
    const double h = 1e-6;
    for (int p = 0; p < probes; ++p) {
      const int i = (p * 7 + 3) % n;
      Vec xip = xi0, xim = xi0;
      xip[static_cast<std::size_t>(i)] += h;
      xim[static_cast<std::size_t>(i)] -= h;
      const double gp = temperatureObjective(solve(assemble(x, xip)));
      const double gm = temperatureObjective(solve(assemble(x, xim)));
      const double fd = (gp - gm) / (2.0 * h);
      const double scale = std::max({std::abs(fd), std::abs(dGdXi[static_cast<std::size_t>(i)]), 1e-10});
      worst = std::max(worst, std::abs(fd - dGdXi[static_cast<std::size_t>(i)]) / scale);
    }
    return worst;
  }

 private:
  static Vec matVec(const Mat& A, const Vec& x) {
    Vec y(A.size(), 0.0);
    for (std::size_t i = 0; i < A.size(); ++i) {
      double acc = 0.0;
      for (std::size_t j = 0; j < A[i].size(); ++j) acc += A[i][j] * x[j];
      y[i] = acc;
    }
    return y;
  }

  FvmMesh mesh_;
  double kFluid_;
  double kSolid_;
  double p_;
  double sigmaXi_;
  double rhoKs_;
  double kappa_;
  double sourceQ_;
  Mat covariance_;
};

}  // namespace mdo
