// Phase 4 - standalone optimizers: Svanberg MMA, Revolve binomial checkpointing,
// generalized FOSM robust sensitivities, MIGA/PSO + Nelder-Mead/L-BFGS hybrid.
#pragma once

#include "mdo_core.hpp"
#include <cassert>
#include <map>
#include <memory>

namespace mdo {

// ===========================================================================
// 1. Svanberg's Method of Moving Asymptotes (complete separable MMA subproblem
//    solved by a primal-dual interior-point Newton method, SolveDIP).
// ===========================================================================
class MMA {
 public:
  MMA(int n, int m, Vec lo, Vec hi)
      : n_(n), m_(m), lo_(std::move(lo)), hi_(std::move(hi)),
        asyLow_(n), asyUp_(n), x1_(n, 0.0), x2_(n, 0.0) {}

  Vec step(const Vec& x, double f0, const Vec& df0,
           const std::vector<double>& g, const std::vector<Vec>& dg) {
    (void)f0; // f0 enters through the separable constant in the dual
    if ((int)x.size() != n_ || (int)df0.size() != n_ || m_ != (int)g.size())
      throw std::runtime_error("MMA: dimension mismatch");
    if (iter_ < 2) {
      for (int j = 0; j < n_; ++j) {
        double range = std::max(1e-6, hi_[j] - lo_[j]);
        asyLow_[j] = x[j] - 0.5 * range; // asyminit
        asyUp_[j] = x[j] + 0.5 * range;
      }
    } else {
      for (int j = 0; j < n_; ++j) {
        double prod = (x[j] - x1_[j]) * (x1_[j] - x2_[j]);
        if (prod > 0) { // monotone movement: shrink asymptotes
          asyLow_[j] -= 0.7 * (x[j] - asyLow_[j]);
          asyUp_[j] += 0.7 * (asyUp_[j] - x[j]);
        } else { // sign oscillation: expand
          asyLow_[j] += 1.2 * (x[j] - asyLow_[j]);
          asyUp_[j] -= 1.2 * (asyUp_[j] - x[j]);
        }
      }
    }
    // Keep asymptotes strictly outside the trust interval.
    for (int j = 0; j < n_; ++j) {
      asyLow_[j] = std::min(asyLow_[j], x[j] - 1e-4);
      asyUp_[j] = std::max(asyUp_[j], x[j] + 1e-4);
    }
    x2_ = x1_; x1_ = x; ++iter_;

    std::vector<std::vector<double>> P(m_, Vec(n_)), Q(m_, Vec(n_));
    Vec P0(n_, 0.0), Q0(n_, 0.0);
    Vec c(m_, 0.0);
    for (int j = 0; j < n_; ++j) {
      double du = std::max(1e-9, asyUp_[j] - x[j]);
      double dl = std::max(1e-9, x[j] - asyLow_[j]);
      if (df0[j] > 0) P0[j] = df0[j] * du * du;
      else Q0[j] = -df0[j] * dl * dl;
    }
    for (int i = 0; i < m_; ++i) {
      for (int j = 0; j < n_; ++j) {
        double du = std::max(1e-9, asyUp_[j] - x[j]);
        double dl = std::max(1e-9, x[j] - asyLow_[j]);
        if (dg[i][j] > 0) P[i][j] = dg[i][j] * du * du;
        else Q[i][j] = -dg[i][j] * dl * dl;
        c[i] += P[i][j] / du + Q[i][j] / dl;
      }
    }
    Vec lambda = solveDIP(P, Q, P0, Q0, g, c);

    Vec y(n_);
    for (int j = 0; j < n_; ++j) {
      double A = P0[j], C = Q0[j];
      for (int i = 0; i < m_; ++i) { A += lambda[i] * P[i][j]; C += lambda[i] * Q[i][j]; }
      double sa = std::sqrt(std::max(1e-300, A)), sc = std::sqrt(std::max(0.0, C));
      y[j] = (asyUp_[j] * sc + asyLow_[j] * sa) / (sa + sc);
      y[j] = std::clamp(y[j], lo_[j] + 1e-8, hi_[j] - 1e-8);
    }
    lastDual_ = lambda;
    return y;
  }

  Vec lastDual() const { return lastDual_; }
  std::pair<double, double> asymptotes(int j) const { return {asyLow_[j], asyUp_[j]}; }

 private:
  static Vec linSolve(std::vector<Vec> A, Vec b) {
    int n = (int)b.size();
    for (int k = 0; k < n; ++k) {
      int p = k;
      for (int i = k + 1; i < n; ++i) if (std::abs(A[i][k]) > std::abs(A[p][k])) p = i;
      std::swap(A[k], A[p]); std::swap(b[k], b[p]);
      if (std::abs(A[k][k]) < 1e-300) continue;
      for (int i = k + 1; i < n; ++i) {
        double f = A[i][k] / A[k][k];
        for (int j = k; j < n; ++j) A[i][j] -= f * A[k][j];
        b[i] -= f * b[k];
      }
    }
    Vec x(n, 0.0);
    for (int i = n - 1; i >= 0; --i) {
      double s = b[i];
      for (int j = i + 1; j < n; ++j) s -= A[i][j] * x[j];
      x[i] = std::abs(A[i][i]) < 1e-300 ? 0.0 : s / A[i][i];
    }
    return x;
  }

  // Maximise concave dual psi(lambda), lambda in (0, lambdaMax)^m, with log barrier.
  Vec solveDIP(const std::vector<std::vector<double>>& P,
               const std::vector<std::vector<double>>& Q,
               const Vec& P0, const Vec& Q0,
               const std::vector<double>& g, const Vec& c) {
    const double lamMax = 1e5;
    Vec lam(m_, 1.0);
    auto yOf = [&](const Vec& l) {
      Vec y(n_);
      for (int j = 0; j < n_; ++j) {
        double A = P0[j], C = Q0[j];
        for (int i = 0; i < m_; ++i) { A += l[i] * P[i][j]; C += l[i] * Q[i][j]; }
        if (A <= 0 && C <= 0) { y[j] = 0.5 * (asyLow_[j] + asyUp_[j]); continue; }
        double sa = std::sqrt(std::max(0.0, A)), sc = std::sqrt(std::max(0.0, C));
        y[j] = (asyUp_[j] * sc + asyLow_[j] * sa) / std::max(1e-300, sa + sc);
      }
      return y;
    };
    auto dualValue = [&](const Vec& l, const Vec& y) {
      double v = 0.0;
      for (int i = 0; i < m_; ++i) v += l[i] * (g[i] - c[i]);
      for (int j = 0; j < n_; ++j) {
        double A = P0[j], C = Q0[j];
        for (int i = 0; i < m_; ++i) { A += l[i] * P[i][j]; C += l[i] * Q[i][j]; }
        v += A / std::max(1e-9, asyUp_[j] - y[j]) + C / std::max(1e-9, y[j] - asyLow_[j]);
      }
      return v;
    };
    for (int it = 0; it < 100; ++it) {
      Vec y = yOf(lam);
      Vec grad(m_, 0.0); std::vector<Vec> H(m_, Vec(m_, 0.0));
      for (int i = 0; i < m_; ++i) {
        grad[i] = g[i] - c[i];
        for (int j = 0; j < n_; ++j) {
          double du = std::max(1e-8, asyUp_[j] - y[j]);
          double dl = std::max(1e-8, y[j] - asyLow_[j]);
          grad[i] += P[i][j] / du + Q[i][j] / dl;
          for (int k = i; k < m_; ++k)
            H[i][k] += (P[i][j] * P[k][j]) / (du * du * du) +
                       (Q[i][j] * Q[k][j]) / (dl * dl * dl);
        }
      }
      for (int i = 0; i < m_; ++i) {
        for (int k = 0; k < i; ++k) H[i][k] = H[k][i];
        grad[i] += -1.0 / lam[i] + 1.0 / (lamMax - lam[i]);
        H[i][i] += 1.0 / (lam[i] * lam[i]) + 1.0 / ((lamMax - lam[i]) * (lamMax - lam[i]));
      }
      double inf = 0.0; for (double v : grad) inf = std::max(inf, std::abs(v));
      if (inf < 1e-9) break;
      Vec dir = linSolve(H, grad); // Newton direction for maximisation
      double psi = dualValue(lam, y);
      double step = 1.0; bool accepted = false;
      for (int ls = 0; ls < 60; ++ls) {
        Vec trial = lam;
        for (int i = 0; i < m_; ++i)
          trial[i] = std::clamp(lam[i] + step * dir[i], 1e-10, lamMax * (1 - 1e-10));
        Vec yt = yOf(trial);
        double psit = dualValue(trial, yt);
        if (psit >= psi - 1e-14) { lam = trial; accepted = true; break; }
        step *= 0.5;
      }
      if (!accepted) break;
    }
    return lam;
  }

  int n_, m_, iter_ = 0;
  Vec lo_, hi_, asyLow_, asyUp_, x1_, x2_, lastDual_;
};

// ===========================================================================
// 2. Griewank & Walther optimal binomial checkpointing (Revolve).
//    DP over (steps, slots) gives the minimal number of forward advances;
//    a recursive interval schedule emits SAVE/RESTORE/FORWARD/ADJOINT events.
// ===========================================================================
struct RevolveResult {
  int steps = 0, slots = 0;
  long long forwardCalls = 0, recomputations = 0, adjointCalls = 0, maxSlotsUsed = 0;
  std::vector<std::string> actions; // compact trace
};

class Revolve {
 public:
  Revolve(int maxSteps, int maxSlots) : N_(maxSteps), S_(maxSlots) {
    if (N_ < 1 || S_ < 1) throw std::runtime_error("Revolve: invalid dimensions");
    cost_.assign(N_ + 1, std::vector<long long>(S_ + 1, 0));
    split_.assign(N_ + 1, std::vector<int>(S_ + 1, 0));
    for (int n = 1; n <= N_; ++n) cost_[n][1] = 1LL * n * (n + 1) / 2;
    for (int s = 2; s <= S_; ++s) {
      cost_[0][s] = 0;
      for (int n = 1; n <= N_; ++n) {
        if (n <= s) { cost_[n][s] = n; split_[n][s] = 0; continue; }
        long long best = std::numeric_limits<long long>::max() / 4; int bestK = 1;
        for (int k = 1; k < n; ++k) {
          long long v = k + cost_[k][s - 1] + cost_[n - k][s];
          if (v < best) { best = v; bestK = k; }
        }
        cost_[n][s] = best; split_[n][s] = bestK;
      }
    }
  }

  RevolveResult schedule() const {
    RevolveResult r; r.steps = N_; r.slots = S_;
    process(0, N_, S_, 0, r);
    r.recomputations = r.forwardCalls - N_;
    return r;
  }

  long long minimalForwardCalls() const { return cost_[N_][S_]; }

 private:
  void emit(RevolveResult& r, const std::string& a) const {
    r.actions.push_back(a);
    if (r.actions.size() <= 100000) {
      if (a.rfind("SAVE", 0) == 0) {
        int slot = std::stoi(a.substr(a.find('#') + 1));
        r.maxSlotsUsed = std::max(r.maxSlotsUsed, (long long)slot + 1);
      }
    }
  }
  void forward(RevolveResult& r, int count) const {
    for (int i = 0; i < count; ++i) { ++r.forwardCalls; r.actions.push_back("FORWARD"); }
  }
  void process(int lo, int hi, int slots, int slotBase, RevolveResult& r) const {
    if (hi <= lo) return;
    if (slots <= 1) {
      // Only state lo is retained: replay from it for each adjoint step.
      int slot = slotBase;
      emit(r, "SAVE#" + std::to_string(slot) + "@" + std::to_string(lo));
      for (int t = hi - lo; t >= 1; --t) {
        r.actions.push_back("RESTORE#" + std::to_string(slot) + "@" + std::to_string(lo));
        forward(r, t);
        r.actions.push_back("ADJOINT@" + std::to_string(lo + t));
        ++r.adjointCalls;
      }
      return;
    }
    if (hi - lo <= slots) {
      for (int t = lo + 1; t <= hi; ++t) {
        forward(r, 1);
        emit(r, "SAVE#" + std::to_string(slotBase + t - lo - 1) + "@" + std::to_string(t));
      }
      for (int t = hi; t >= lo + 1; --t) {
        r.actions.push_back("ADJOINT@" + std::to_string(t)); ++r.adjointCalls;
        if (t - 1 > lo)
          r.actions.push_back("RESTORE#" + std::to_string(slotBase + t - 1 - lo - 1) + "@" + std::to_string(t - 1));
      }
      return;
    }
    int k = split_[hi - lo][slots];
    if (k <= 0 || k >= hi - lo) k = std::max(1, (hi - lo) / 2);
    forward(r, k);
    emit(r, "SAVE#" + std::to_string(slotBase) + "@" + std::to_string(lo + k));
    process(lo, lo + k, slots - 1, slotBase + 1, r);
    r.actions.push_back("RESTORE#" + std::to_string(slotBase) + "@" + std::to_string(lo + k));
    process(lo + k, hi, slots, slotBase, r);
  }

  int N_, S_;
  std::vector<std::vector<long long>> cost_;
  std::vector<std::vector<int>> split_;
};

// ===========================================================================
// 3. Generalized FOSM robust optimization (Kranz, Lüdeker & Kriegesmann).
//    Phi_g = mu_g + kappa sigma_g, sigma_g = sqrt(a^T Sigma a), a = dg/dxi.
//    d sigma_g/dgamma = v^T (d/dgamma dg/dxi), v = Sigma a / sigma.
//    Only two adjoint solves are needed (lambda and the stochastic adjoint).
// ===========================================================================
enum class RobustFunctional { MeanTemperature, KSPeakTemperature };

struct FosmResult {
  double mu = 0.0, sigma = 0.0, phi = 0.0;
  Vec a, v, gradMu, gradSigma, gradPhi;
  int adjointSolves = 2;
};

class Fosm {
 public:
  const CellGraph& graph;
  const ThermalModel& model;
  const DesignChain& chain;

  Fosm(const CellGraph& g, const ThermalModel& m, const DesignChain& c)
      : graph(g), model(m), chain(c) {}

  FosmResult evaluate(const ThermalState& st, const std::vector<Vec>& Sigma,
                      RobustFunctional fun, double kappa, double rhoKS,
                      double constraintShift = 0.0) const {
    const int n = graph.n;
    Vec w(n); double mu = 0.0; bool nonlinear = fun == RobustFunctional::KSPeakTemperature;
    KSResult ks;
    if (nonlinear) {
      ks = ksAggregate(st.T, rhoKS);
      mu = ks.value - constraintShift;
      w = ks.weight;
    } else {
      const Vec& f = model.source();
      for (int i = 0; i < n; ++i) w[i] = f[i] / n;
      mu = std::inner_product(w.begin(), w.end(), st.T.begin(), 0.0) - constraintShift;
    }
    Vec lambda = pcg(st.B, w);

    // a_m = dg/dxi_m by the adjoint (envelope) theorem.
    Vec a(graph.n + 1, 0.0);
    for (auto [i, j] : graph.edges) {
      double p = model.tc.sMat * (lambda[i] - lambda[j]) * (st.T[i] - st.T[j]);
      a[i] += 0.5 * st.kbase[i] * p;
      a[j] += 0.5 * st.kbase[j] * p;
    }
    for (int i = 0; i < n; ++i) a[i] *= -1.0;
    double sens = 0.0;
    for (int i = 0; i < n; ++i) sens += lambda[i] * st.T[i];
    a[n] = -model.tc.hConv * model.tc.sConv * sens;

    Vec Sa = matVec(Sigma, a);
    double variance = std::max(0.0, std::inner_product(a.begin(), a.end(), Sa.begin(), 0.0));
    double sigma = std::sqrt(variance);
    Vec v(graph.n + 1, 0.0);
    double inv = 1.0 / std::max(1e-14, sigma);
    for (int i = 0; i < graph.n + 1; ++i) v[i] = Sa[i] * inv;

    // Stochastic direction load q = sum_m v_m B_xi_m T, then second adjoint.
    Vec q(n, 0.0);
    for (auto [i, j] : graph.edges) {
      double c = 0.5 * model.tc.sMat * (v[i] * st.kbase[i] + v[j] * st.kbase[j]) *
                 (st.T[i] - st.T[j]);
      q[i] += c; q[j] -= c;
    }
    double convLoad = v[n] * model.tc.hConv * model.tc.sConv;
    for (int i = 0; i < n; ++i) q[i] += convLoad * st.T[i];
    Vec lambdaV = pcg(st.B, q); // second adjoint solve
    Vec lambdaVLam;
    if (!nonlinear) {
      // lambda = T/n because B T = f and B lambda = f/n.
      lambdaVLam = (1.0 / n) * lambdaV;
    } else {
      // General nonlinear functional: one extra B_q lambda adjoint solve.
      Vec qLam(n, 0.0);
      for (auto [i, j] : graph.edges) {
        double c = 0.5 * model.tc.sMat * (v[i] * st.kbase[i] + v[j] * st.kbase[j]) *
                   (lambda[i] - lambda[j]);
        qLam[i] += c; qLam[j] -= c;
      }
      for (int i = 0; i < n; ++i) qLam[i] += v[n] * model.tc.hConv * model.tc.sConv * lambda[i];
      lambdaVLam = pcg(st.B, qLam);
    }

    // d mu / d gamma = -lambda^T (dB/dgamma) T.
    Vec node = model.edgeGradient(st.dkDgammaBar, lambda, st.T);
    for (int i = 0; i < n; ++i) node[i] *= -1.0;
    Vec gradMu = chain.vjp(node);

    // lambda^T D lambdaV + lambdaV^T D T.
    Vec termA = model.edgeGradient(st.dkDgammaBar, lambda, lambdaV);
    // lambda^T B_q B^{-1} D T = (B^{-1} B_q lambda)^T D T.
    Vec termB = model.edgeGradient(st.dkDgammaBar, lambdaVLam, st.T);
    // - sum_m v_m lambda^T dB_xi_m/dgamma T (only material field contributes).
    Vec termR = model.edgeGradient(st.dkDgammaBar, lambda, st.T);
    for (int i = 0; i < n; ++i) termR[i] *= -model.tc.sMat * v[i];

    // Nonlinear functional (KS): -lambdaV^T (d weight / d gamma).
    Vec termNL(n, 0.0);
    if (nonlinear) {
      double cw = 0.0;
      for (int i = 0; i < n; ++i) cw += lambdaV[i] * w[i];
      Vec s(n);
      for (int i = 0; i < n; ++i) {
        double sum = 0.0;
        for (int d = 0; d < 4; ++d) if (graph.nb[i][d] >= 0)
          sum += st.T[i] - st.T[graph.nb[i][d]];
        s[i] = 0.5 * st.dkDgammaBar[i] * sum;
      }
      for (int i = 0; i < n; ++i)
        termNL[i] = rhoKS * w[i] * (lambdaV[i] - cw) * s[i];
    }
    Vec combinedNode(n);
    for (int i = 0; i < n; ++i)
      combinedNode[i] = termA[i] + termB[i] + termR[i] + termNL[i];
    Vec gradSigma = chain.vjp(combinedNode); // A^{-T} through projection and filter
    Vec gradPhi(n);
    for (int i = 0; i < n; ++i) gradPhi[i] = gradMu[i] + kappa * gradSigma[i];
    FosmResult r;
    r.mu = mu; r.sigma = sigma; r.phi = mu + kappa * sigma;
    r.a = a; r.v = v; r.gradMu = gradMu; r.gradSigma = gradSigma; r.gradPhi = gradPhi;
    r.adjointSolves = nonlinear ? 3 : 2;
    return r;
  }

  double functionalValue(const ThermalState& st, RobustFunctional fun, double rhoKS) const {
    if (fun == RobustFunctional::MeanTemperature) {
      return dot(model.source(), st.T) / graph.n; // thermal compliance f^T T
    }
    return ksAggregate(st.T, rhoKS).value;
  }
};

// Monte Carlo validation: common random numbers, central difference of the
// sampled variance. This is a check of the analytical FOSM gradient, not an
// optimisation loop.
struct McCheck {
  double analytic = 0.0, monteCarlo = 0.0, relativeError = 0.0;
  double variancePlus = 0.0, varianceMinus = 0.0;
  int samples = 0; double step = 0.0; bool pass = false;
};

inline McCheck monteCarloVarianceCheck(const CellGraph& g, const ThermalModel& model,
                                       const ChainConfig& cc, const Vec& gamma,
                                       const std::vector<Vec>& Sigma,
                                       const Vec& analyticGradSigma, double sigmaNominal,
                                       RobustFunctional fun, double rhoKS,
                                       int samples, unsigned seed, double step = 2e-2) {
  if (samples < 100) samples = 100;
  const int m = g.n + 1;
  auto L = cholesky(Sigma);
  std::mt19937 rng(seed);
  std::normal_distribution<double> nd(0.0, 1.0);
  std::vector<Vec> xi(samples, Vec(m));
  for (int s = 0; s < samples; ++s) {
    Vec z(m);
    for (int i = 0; i < m; ++i) z[i] = nd(rng);
    for (int i = 0; i < m; ++i) {
      double v = 0.0;
      for (int j = 0; j <= i; ++j) v += L[i][j] * z[j];
      xi[s][i] = v;
    }
  }
  auto sampledVariance = [&](double h) {
    DesignChain chain(g, cc);
    Vec gp = gamma;
    double gnorm = std::max(1e-12, norm(analyticGradSigma));
    for (int i = 0; i < (int)gp.size(); ++i)
      gp[i] = std::clamp(gp[i] + h * analyticGradSigma[i] / gnorm, 1e-4, 1 - 1e-4);
    chain.forward(gp);
    double mean = 0.0, m2 = 0.0; long long cnt = 0;
    for (const auto& x : xi) {
      ThermalState st = model.primal(chain, x);
      double val = fun == RobustFunctional::MeanTemperature
                       ? dot(model.source(), st.T) / g.n
                       : ksAggregate(st.T, rhoKS).value;
      ++cnt; double d = val - mean; mean += d / cnt; m2 += d * (val - mean);
    }
    return m2 / (cnt - 1);
  };
  // Analytic derivative of variance = 2 sigma (d sigma / d gamma) projected
  // on the same direction used for the finite difference.
  double normG = norm(analyticGradSigma);
  double analytic = 2.0 * sigmaNominal * normG;
  double vPlus = sampledVariance(step), vMinus = sampledVariance(-step);
  double mc = (vPlus - vMinus) / (2.0 * step);
  McCheck out;
  out.analytic = analytic; out.monteCarlo = mc; out.samples = samples; out.step = step;
  out.variancePlus = vPlus; out.varianceMinus = vMinus;
  out.relativeError = std::abs(analytic) > 1e-12 ? std::abs(mc - analytic) / std::abs(analytic) : std::abs(mc);
  out.pass = out.relativeError < 0.01;
  return out;
}

// ===========================================================================
// 4. Two-stage hybrid global/local shape optimisation.
// ===========================================================================
struct ShapeObjective {
  int dim = 4;
  static constexpr double PI = 3.14159265358979323846;

  double valueAndGrad(const Vec& x, Vec& grad) const {
    double f = 0.0; grad.assign(dim, 0.0);
    for (int i = 0; i < dim; ++i) {
      f += 1.4 * (x[i] - 0.32) * (x[i] - 0.32);
      grad[i] += 2.8 * (x[i] - 0.32);
      f += 0.35 * std::sin(6 * PI * x[i]) * std::sin(6 * PI * x[i]);
      grad[i] += 0.35 * 12 * PI * std::sin(6 * PI * x[i]) * std::cos(6 * PI * x[i]);
    }
    f += 0.55 * std::sin(3 * PI * x[0]) * std::sin(4 * PI * x[1]);
    grad[0] += 0.55 * 3 * PI * std::cos(3 * PI * x[0]) * std::sin(4 * PI * x[1]);
    grad[1] += 0.55 * 4 * PI * std::sin(3 * PI * x[0]) * std::cos(4 * PI * x[1]);
    if (dim > 2) {
      f += 0.45 * (x[2] - 0.7) * (x[2] - 0.7);
      grad[2] += 0.9 * (x[2] - 0.7);
    }
    double vol = std::accumulate(x.begin(), x.end(), 0.0) / dim;
    if (vol > 0.55) {
      f += 8.0 * (vol - 0.55) * (vol - 0.55);
      for (int i = 0; i < dim; ++i) grad[i] += 16.0 * (vol - 0.55) / dim;
    }
    return f;
  }
  double score(const Vec& x) const { Vec g; return valueAndGrad(x, g); }
  double volume(const Vec& x) const { return std::accumulate(x.begin(), x.end(), 0.0) / dim; }
  std::pair<double, double> objectives(const Vec& x) const { return {score(x), volume(x)}; }
};

struct HybridResult {
  Vec best;
  double initialBest = 0, afterMiga = 0, afterPso = 0, afterNelderMead = 0, final = 0;
  std::vector<double> historyMiga, historyPso, historyNm, historyLbfgs;
  std::vector<std::pair<double, double>> paretoFront;
  int evaluations = 0;
};

class HybridOptimizer {
 public:
  HybridOptimizer(const ShapeObjective& obj, unsigned seed = 42)
      : obj_(obj), rng_(seed), nd_(0.0, 1.0) {}

  HybridResult run(int islands, int popPerIsland, int generations,
                   int psoParticles, int psoIterations) const {
    if (islands < 2) islands = 2;
    if (popPerIsland < 8) popPerIsland = 8;
    if (generations < 2) generations = 2;
    HybridResult res;
    struct Ind { Vec x; double f1 = 0, f2 = 0; int rank = 0; double crowd = 0; };
    std::vector<std::vector<Ind>> pop(islands, std::vector<Ind>(popPerIsland));
    for (auto& isl : pop) for (auto& ind : isl) {
      ind.x.resize(obj_.dim);
      for (auto& x : ind.x) x = 0.08 + 0.84 * nd_(rng_);
      auto o = obj_.objectives(ind.x); ind.f1 = o.first; ind.f2 = o.second;
      ++res.evaluations;
    }
    auto globalBest = bestOf(pop);
    res.initialBest = obj_.score(globalBest);

    for (int gen = 0; gen < generations; ++gen) {
      for (auto& isl : pop) {
        nsga2Sort(isl);
        std::vector<Ind> next = {isl[0], isl[1]}; // elitism
        while ((int)next.size() < popPerIsland) {
          const Ind& p1 = tournament(isl);
          const Ind& p2 = tournament(isl);
          Ind c1, c2;
          crossover(p1.x, p2.x, c1.x, c2.x);
          mutate(c1.x); mutate(c2.x);
          for (Ind* c : {&c1, &c2}) {
            if ((int)next.size() >= popPerIsland) break;
            auto o = obj_.objectives(c->x); c->f1 = o.first; c->f2 = o.second;
            ++res.evaluations; next.push_back(*c);
          }
        }
        isl = next;
      }
      if (gen % 8 == 7) migrate(pop);
      Vec best = bestOf(pop);
      if (obj_.score(best) < obj_.score(globalBest)) globalBest = best;
      res.historyMiga.push_back(obj_.score(globalBest));
    }
    res.afterMiga = obj_.score(globalBest);

    // PSO seeded in the global basin discovered by MIGA.
    struct Particle { Vec x, v, pbest; double best = 0; };
    std::vector<Particle> swarm(psoParticles);
    for (auto& p : swarm) {
      p.x.resize(obj_.dim); p.v.resize(obj_.dim);
      for (int d = 0; d < obj_.dim; ++d) {
        p.x[d] = std::clamp(globalBest[d] + 0.25 * (nd_(rng_) - 0.5), 0.01, 0.99);
        p.v[d] = 0.05 * (nd_(rng_) - 0.5);
      }
      p.pbest = p.x; p.best = obj_.score(p.x); ++res.evaluations;
    }
    Vec gbest = globalBest; double gbestVal = obj_.score(gbest);
    for (int it = 0; it < psoIterations; ++it) {
      double w = 0.72 - 0.42 * it / std::max(1, psoIterations - 1);
      for (auto& p : swarm) {
        for (int d = 0; d < obj_.dim; ++d) {
          double r1 = nd_(rng_), r2 = nd_(rng_);
          p.v[d] = w * p.v[d] + 1.49 * r1 * (p.pbest[d] - p.x[d]) + 1.49 * r2 * (gbest[d] - p.x[d]);
          p.x[d] = std::clamp(p.x[d] + p.v[d], 0.01, 0.99);
        }
        double val = obj_.score(p.x); ++res.evaluations;
        if (val < p.best) { p.best = val; p.pbest = p.x; }
        if (val < gbestVal) { gbestVal = val; gbest = p.x; }
      }
      res.historyPso.push_back(gbestVal);
    }
    res.afterPso = gbestVal;

    // In-memory warm start: no re-evaluation of the global pipeline.
    Vec local = nelderMead(gbest, 260, res.historyNm, res.evaluations);
    res.afterNelderMead = obj_.score(local);
    local = lbfgs(local, 140, res.historyLbfgs, res.evaluations);
    res.final = obj_.score(local);
    res.best = local;
    for (auto& isl : pop) for (auto& ind : isl) {
      if (ind.rank == 0) res.paretoFront.push_back({ind.f1, ind.f2});
    }
    std::sort(res.paretoFront.begin(), res.paretoFront.end());
    res.paretoFront.erase(std::unique(res.paretoFront.begin(), res.paretoFront.end(),
                    [](const std::pair<double,double>& a, const std::pair<double,double>& b) {
                      return std::abs(a.first - b.first) < 1e-9 && std::abs(a.second - b.second) < 1e-9;
                    }), res.paretoFront.end());
    return res;
  }

 private:
  const ShapeObjective& obj_;
  mutable std::mt19937 rng_;
  mutable std::normal_distribution<double> nd_;

  template <class Pop>
  Vec bestOf(const Pop& pop) const {
    Vec b; double f = std::numeric_limits<double>::infinity();
    for (const auto& isl : pop) for (const auto& ind : isl)
      if (ind.f1 < f) { f = ind.f1; b = ind.x; }
    return b;
  }
  static bool dominates(const std::pair<double,double>& a, const std::pair<double,double>& b) {
    return (a.first <= b.first && a.second <= b.second) && (a.first < b.first || a.second < b.second);
  }
  template <class Ind>
  void nsga2Sort(std::vector<Ind>& pop) const {
    int n = (int)pop.size();
    std::vector<int> domCount(n, 0), rank(n, 0);
    std::vector<std::vector<int>> dominated(n);
    for (int p = 0; p < n; ++p) for (int q = 0; q < n; ++q) {
      if (p == q) continue;
      if (dominates({pop[p].f1, pop[p].f2}, {pop[q].f1, pop[q].f2})) dominated[p].push_back(q);
      else if (dominates({pop[q].f1, pop[q].f2}, {pop[p].f1, pop[p].f2})) ++domCount[p];
    }
    std::vector<int> front;
    for (int i = 0; i < n; ++i) if (domCount[i] == 0) front.push_back(i);
    int k = 0;
    while (!front.empty()) {
      std::vector<int> next;
      for (int i : front) {
        pop[i].rank = k;
        for (int j : dominated[i]) if (--domCount[j] == 0) next.push_back(j);
      }
      front = next; ++k;
    }
    for (int f = 0; f < k; ++f) {
      std::vector<int> ids;
      for (int i = 0; i < n; ++i) if (pop[i].rank == f) ids.push_back(i);
      for (auto& id : ids) pop[id].crowd = 0;
      for (int o = 0; o < 2; ++o) {
        std::sort(ids.begin(), ids.end(), [&](int a, int b) {
          return o == 0 ? pop[a].f1 < pop[b].f1 : pop[a].f2 < pop[b].f2;
        });
        pop[ids.front()].crowd = pop[ids.back()].crowd = 1e9;
        for (int i = 1; i + 1 < (int)ids.size(); ++i) {
          double lo = o == 0 ? pop[ids[i-1]].f1 : pop[ids[i-1]].f2;
          double hi = o == 0 ? pop[ids[i+1]].f1 : pop[ids[i+1]].f2;
          pop[ids[i]].crowd += (hi - lo);
        }
      }
    }
  }
  template <class Ind>
  const Ind& tournament(const std::vector<Ind>& pop) const {
    const Ind* a = &pop[rng_() % pop.size()];
    const Ind* b = &pop[rng_() % pop.size()];
    if (a->rank != b->rank) return a->rank < b->rank ? *a : *b;
    return a->crowd > b->crowd ? *a : *b;
  }
  void crossover(const Vec& a, const Vec& b, Vec& c1, Vec& c2) const {
    c1 = a; c2 = b;
    constexpr double eta = 15.0;
    for (int i = 0; i < obj_.dim; ++i) {
      if (nd_(rng_) > 0.9 || std::abs(a[i] - b[i]) < 1e-12) continue;
      double u = std::min(nd_(rng_), 1.0 - nd_(rng_));
      double beta = std::pow(2.0 * u, 1.0 / (eta + 1.0));
      c1[i] = std::clamp(0.5 * ((1 + beta) * a[i] + (1 - beta) * b[i]), 0.01, 0.99);
      c2[i] = std::clamp(0.5 * ((1 - beta) * a[i] + (1 + beta) * b[i]), 0.01, 0.99);
    }
  }
  void mutate(Vec& x) const {
    constexpr double eta = 20.0;
    for (auto& v : x) {
      if (nd_(rng_) > 1.0 / obj_.dim) continue;
      double u = nd_(rng_), delta;
      if (u < 0.5) delta = std::pow(2 * u, 1.0 / (eta + 1)) - 1;
      else delta = 1 - std::pow(2 * (1 - u), 1.0 / (eta + 1));
      v = std::clamp(v + delta * 0.25, 0.01, 0.99);
    }
  }
  template <class Pop>
  void migrate(Pop& pop) const {
    int isl = (int)pop.size();
    for (int i = 0; i < isl; ++i) {
      std::vector<int> order(pop[i].size());
      std::iota(order.begin(), order.end(), 0);
      std::sort(order.begin(), order.end(), [&](int a, int b) {
        if (pop[i][a].rank != pop[i][b].rank) return pop[i][a].rank < pop[i][b].rank;
        return pop[i][a].crowd > pop[i][b].crowd;
      });
      for (int k = 0; k < 2; ++k) {
        auto donor = pop[i][order[k]];
        auto& target = pop[(i + 1) % isl][pop[(i + 1) % isl].size() - 1 - k];
        target = donor;
      }
    }
  }

  Vec nelderMead(Vec start, int maxIter, std::vector<double>& hist, int& evals) const {
    int n = obj_.dim;
    std::vector<std::pair<double, Vec>> simplex;
    simplex.push_back({obj_.score(start), start}); ++evals;
    for (int i = 0; i < n; ++i) {
      Vec p = start;
      p[i] = std::clamp(p[i] + 0.08, 0.01, 0.99);
      simplex.push_back({obj_.score(p), p}); ++evals;
    }
    auto byValue = [](const auto& a, const auto& b) { return a.first < b.first; };
    for (int it = 0; it < maxIter; ++it) {
      std::sort(simplex.begin(), simplex.end(), byValue);
      hist.push_back(simplex.front().first);
      if (std::abs(simplex.back().first - simplex.front().first) < 1e-12) break;
      Vec centroid(n, 0.0);
      for (int i = 0; i < n; ++i) for (int d = 0; d < n; ++d) centroid[d] += simplex[i].second[d] / n;
      const Vec& worst = simplex.back().second;
      Vec xr(n), xe(n), xc(n), xcc(n);
      for (int d = 0; d < n; ++d) xr[d] = std::clamp(centroid[d] + (centroid[d] - worst[d]), 0.01, 0.99);
      double fr = obj_.score(xr); ++evals;
      if (fr < simplex.front().first) {
        for (int d = 0; d < n; ++d) xe[d] = std::clamp(centroid[d] + 2 * (xr[d] - centroid[d]), 0.01, 0.99);
        double fe = obj_.score(xe); ++evals;
        simplex.back() = fe < fr ? std::make_pair(fe, xe) : std::make_pair(fr, xr);
      } else if (fr < simplex[simplex.size() - 2].first) {
        simplex.back() = {fr, xr};
      } else {
        if (fr < simplex.back().first) {
          for (int d = 0; d < n; ++d) xc[d] = std::clamp(centroid[d] + 0.5 * (xr[d] - centroid[d]), 0.01, 0.99);
          double fc = obj_.score(xc); ++evals;
          if (fc <= fr) { simplex.back() = {fc, xc}; continue; }
        } else {
          for (int d = 0; d < n; ++d) xc[d] = std::clamp(centroid[d] + 0.5 * (worst[d] - centroid[d]), 0.01, 0.99);
          double fc = obj_.score(xc); ++evals;
          if (fc < simplex.back().first) { simplex.back() = {fc, xc}; continue; }
        }
        const Vec& best = simplex.front().second;
        for (size_t i = 1; i < simplex.size(); ++i) {
          for (int d = 0; d < n; ++d)
            xcc[d] = std::clamp(best[d] + 0.5 * (simplex[i].second[d] - best[d]), 0.01, 0.99);
          simplex[i] = {obj_.score(xcc), xcc}; ++evals;
        }
      }
    }
    std::sort(simplex.begin(), simplex.end(), byValue);
    return simplex.front().second;
  }

  Vec lbfgs(Vec x, int maxIter, std::vector<double>& hist, int& evals) const {
    const int mem = 8; int n = obj_.dim;
    Vec g; double f = obj_.valueAndGrad(x, g); ++evals;
    std::vector<Vec> sHist, yHist; std::vector<double> rhoHist;
    for (int it = 0; it < maxIter; ++it) {
      hist.push_back(f);
      if (norm(g) < 1e-10) break;
      Vec q = g, alpha(mem, 0.0);
      int m = (int)sHist.size();
      for (int i = m - 1; i >= 0; --i) {
        alpha[i] = rhoHist[i] * dot(sHist[i], q);
        for (int d = 0; d < n; ++d) q[d] -= alpha[i] * yHist[i][d];
      }
      Vec r = m > 0 ? sHist.back() : q;
      if (m > 0) {
        double gamma = dot(sHist.back(), yHist.back()) / std::max(1e-20, dot(yHist.back(), yHist.back()));
        for (int d = 0; d < n; ++d) r[d] *= gamma;
      }
      for (int i = 0; i < m; ++i) {
        double beta = rhoHist[i] * dot(yHist[i], r);
        for (int d = 0; d < n; ++d) r[d] += sHist[i][d] * (alpha[i] - beta);
      }
      Vec d(n);
      for (int i = 0; i < n; ++i) d[i] = -r[i];
      double step = 1.0; bool ok = false;
      for (int ls = 0; ls < 40; ++ls) {
        Vec xt(n);
        for (int i = 0; i < n; ++i) xt[i] = std::clamp(x[i] + step * d[i], 0.01, 0.99);
        Vec gt; double ft = obj_.valueAndGrad(xt, gt); ++evals;
        if (ft <= f + 1e-4 * step * dot(g, d)) {
          Vec s(n), y(n);
          for (int i = 0; i < n; ++i) { s[i] = xt[i] - x[i]; y[i] = gt[i] - g[i]; }
          double sy = dot(s, y);
          if (sy > 1e-16) {
            sHist.push_back(s); yHist.push_back(y); rhoHist.push_back(1.0 / sy);
            if ((int)sHist.size() > mem) { sHist.erase(sHist.begin()); yHist.erase(yHist.begin()); rhoHist.erase(rhoHist.begin()); }
          }
          x = xt; f = ft; g = gt; ok = true; break;
        }
        step *= 0.5;
      }
      if (!ok) break;
    }
    return x;
  }
};

} // namespace mdo
