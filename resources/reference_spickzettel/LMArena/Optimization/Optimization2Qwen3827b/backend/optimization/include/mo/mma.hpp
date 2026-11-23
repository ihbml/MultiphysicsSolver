// ============================================================================
// mo/mma.hpp — Svanberg Method of Moving Asymptotes (self-contained C++20)
//
// Convex separable approximation (Svanberg 1987 / gradient-based form):
//   f~_i(x) = f_i(x0) + sum_j [ p_ij (x_j - x0_j)/(x_j - L_j)
//                              + q_ij (x_j - x0_j)/(U_j - x_j) ]
//   p_ij = max(0, df_i/dx_j) * (x0_j - L_j),  q_ij = max(0, -df_i/dx_j) * (U_j - x0_j)
// The subproblem  min f~_0(x)  s.t.  f~_i(x) - f_i(x0) <= 0, i = 1..m,
// is a convex separable program solved by Svanberg's primal-dual
// interior-point Newton solver ("SolveDIP") on
//   F1(x, lam) = grad f~_0(x) + J~^T lam
//   F2(x, lam) = g~(x) + mu*e - lam/mu
// with backtracking, mu-damping and lambda/box safeguards.
//
// Asymptote adaptation (Svanberg 1987, original rule):
//   sign change of (x_k - x_{k-1})(x_{k-1} - x_{k-2}) with
//   (x_k - L_k < 0.1 dL_{k-1})  ->  dL = asymin * max(|x_k - x_{k-1}|, asyminit dL_{k-1})
//   (x_k - L_k < 0.1 dL_{k-1})  ->  dL = asymdec * dL_{k-1}
//   else                        ->  dL = asyminc * dL_{k-1}
//   (symmetric for U),  asyminit = 0.5, asymdec = 0.7, asyminc = 1.2.
// ============================================================================
#pragma once
#include "core.hpp"
#include <functional>
#include <cmath>

namespace mo {

// bound-constrained NLP interface
struct NLP {
  int n = 0, m = 0;
  RVec lb, ub;
  std::function<double(const RVec&)> f;
  std::function<void(const RVec&, RVec&)> g_f;                       // grad f
  // fills vals[0..m-1]; if grads != nullptr fills grads[i*n + j] = dg_i/dx_j
  std::function<void(const RVec&, RVec&, RVec*)> g_c;
};

struct MMAIterInfo {
  int iter = 0;
  double f = 0.0;
  RVec gvals;                 // constraint values (<= 0 feasible)
  double max_g = 0.0;
  double step = 0.0;          // accepted step length
  double sub_F1 = 0.0;        // subproblem KKT residual (stationarity)
  double sub_F2 = 0.0;        // subproblem KKT residual (complementarity form)
  int dip_iters = 0;
  double f_tilde_check = 0.0; // |f~_0(x0) - f(x0)| self-consistency
};

class MMA {
 public:
  // Svanberg parameters
  double asymin = 0.1, asyminit = 0.5, asymdec = 0.7, asyminc = 1.2;
  int maxit = 40;
  double mu0 = 1.0, mu_decay = 0.5;
  int max_dip = 80;
  double dip_tol = 1e-10;
  bool verbose = false;

  RVec x, x1, x2;               // x, x^{k-1}, x^{k-2}
  RVec L, U;
  RVec dL_prev, dU_prev;
  RVec lam;
  bool inited = false;
  long long total_dip_iters = 0;

  void init(const NLP& p, const RVec& x0) {
    x = x0; x1 = x0; x2 = x0;
    int n = p.n;
    L.resize(n); U.resize(n);
    for (int j = 0; j < n; ++j) {
      double dL = 10.0 * (x[j] - p.lb[j]);
      double dU = 10.0 * (p.ub[j] - x[j]);
      dL = std::max(dL, 1e-3);
      dU = std::max(dU, 1e-3);
      L[j] = x[j] - dL;
      U[j] = x[j] + dU;
      dL_prev[j] = dL;
      dU_prev[j] = dU;
    }
    lam.assign(p.m, 1.0);
    inited = true;
  }

  // one MMA outer iteration; returns false when converged on step length
  bool step(const NLP& p, MMAIterInfo& out) {
    int n = p.n, m = p.m;
    double f0 = p.f(x);
    RVec gf(n); p.g_f(x, gf);
    RVec gvals(m);
    RVec gc(m * n);
    p.g_c(x, gvals, &gc);
    out.f = f0;
    out.gvals = gvals;
    out.max_g = 0.0;
    for (double v : gvals) out.max_g = std::max(out.max_g, v);

    if (!inited) { init(p, x); return true; }

    // ---- asymptote adaptation (Svanberg 1987) ----
    RVec dL(n), dU(n);
    for (int j = 0; j < n; ++j) {
      double sgn = (x[j] - x1[j]) * (x1[j] - x2[j]);
      if (sgn < 0.0) {
        if (x[j] - L[j] < 0.1 * dL_prev[j])
          dL[j] = asymin * std::max(std::fabs(x[j] - x1[j]), asyminit * dL_prev[j]);
        else
          dL[j] = asyminc * dL_prev[j];
      } else {
        if (x[j] - L[j] < 0.1 * dL_prev[j]) dL[j] = asymdec * dL_prev[j];
        else dL[j] = asyminc * dL_prev[j];
      }
      double sgnU = sgn;
      if (sgnU < 0.0) {
        if (x1[j] - U[j] < -0.1 * dU_prev[j])
          dU[j] = asymin * std::max(std::fabs(x[j] - x1[j]), asyminit * dU_prev[j]);
        else
          dU[j] = asyminc * dU_prev[j];
      } else {
        if (x1[j] - U[j] < -0.1 * dU_prev[j]) dU[j] = asymdec * dU_prev[j];
        else dU[j] = asyminc * dU_prev[j];
      }
      dL[j] = std::max(dL[j], 1e-6);
      dU[j] = std::max(dU[j], 1e-6);
      L[j] = x[j] - dL[j];
      U[j] = x[j] + dU[j];
    }

    // ---- build p_ij, q_ij for i = 0 (objective) .. m-1 ----
    int m1 = m + 1;
    RVec fv(m1), gv(m1 * n);
    fv[0] = f0;
    for (int j = 0; j < n; ++j) gv[j] = gf[j];
    for (int i = 1; i <= m; ++i) {
      fv[i] = gvals[i - 1];
      for (int j = 0; j < n; ++j) gv[i * n + j] = gc[(i - 1) * n + j];
    }
    RVec P(m1 * n), Q(m1 * n);
    for (int i = 0; i <= m; ++i)
      for (int j = 0; j < n; ++j) {
        P[i * n + j] = std::max(0.0, gv[i * n + j]) * dL[j];
        Q[i * n + j] = std::max(0.0, -gv[i * n + j]) * dU[j];
      }

    // ---- solve the convex subproblem with SolveDIP ----
    RVec xs;
    double F1 = 0.0, F2 = 0.0;
    int dipIt = 0;
    solve_DIP(p, P, Q, fv, xs, F1, F2, dipIt);
    total_dip_iters += dipIt;
    out.sub_F1 = F1;
    out.sub_F2 = F2;
    out.dip_iters = dipIt;

    // self-consistency of the separable model at x0
    double ft0 = fv[0];
    for (int j = 0; j < n; ++j)
      ft0 += P[j] * (x[j] - x[j]) / (x[j] - L[j]) + Q[j] * (x[j] - x[j]) / (U[j] - x[j]);
    out.f_tilde_check = std::fabs(ft0 - f0);

    x2 = x1;
    x1 = x;
    // ---- line search on the true problem ----
    double dphi_dx0 = 0.0;
    {
      // directional derivative of f~_0 at x0 along d = xs - x
      RVec d(n);
      for (int j = 0; j < n; ++j) d[j] = xs[j] - x[j];
      for (int j = 0; j < n; ++j) {
        double hp = P[j] / (x[j] - L[j]);          // d/dx of P(x-x0)/(x-L) at x0
        double hq = Q[j] / (U[j] - x[j]);
        dphi_dx0 += (hp - hq) * d[j];
      }
    }
    double best = 1.0, bestF = f0, bestG = out.max_g;
    double t = 1.0;
    for (int ls = 0; ls < 34; ++ls) {
      RVec xt(n);
      for (int j = 0; j < n; ++j)
        xt[j] = std::min(std::max(x[j] + t * (xs[j] - x[j]), p.lb[j] + 1e-9), p.ub[j] - 1e-9);
      double ft = p.f(xt);
      RVec gt(m);
      p.g_c(xt, gt, nullptr);
      double mt = 0.0;
      for (double v : gt) mt = std::max(mt, v);
      if (ft <= f0 - 1e-4 * t * dphi_dx0 + 1e-14 && mt <= out.max_g + 1e-4 + 1e-9 * std::fabs(out.max_g)) {
        best = t; bestF = ft; bestG = mt;
        break;
      }
      if (t < 1e-6) break;
      t *= 0.5;
    }
    if (best < 1e-9) {
      out.step = 0.0;
      return false;  // stalled
    }
    for (int j = 0; j < n; ++j)
      x[j] = std::min(std::max(x[j] + best * (xs[j] - x[j]), p.lb[j] + 1e-9), p.ub[j] - 1e-9);
    out.step = best;
    (void)bestF; (void)bestG;
    return std::fabs(best * nrm_inf(substep(p))) > 1e-9;
  }

  bool converged(const MMAIterInfo& a) const { return a.step < 1e-9; }

  const RVec& current() const { return x; }

 private:
  RVec substep(const NLP& p) { (void)p; return RVec(1, 1.0); }

  // separable model pieces
  double ftilde_i(int i, const RVec& P, const RVec& Q, const RVec& fv,
                  const RVec& xt, const RVec& x0, const RVec& L, const RVec& U) const {
    double s = fv[i];
    int n = (int)x0.size();
    for (int j = 0; j < n; ++j)
      s += P[i * n + j] * (xt[j] - x0[j]) / (xt[j] - L[j])
         + Q[i * n + j] * (xt[j] - x0[j]) / (U[j] - xt[j]);
    return s;
  }
  // d ftilde_i / dx_j
  double dftilde_dx(int i, int j, const RVec& P, const RVec& Q,
                    const RVec& xt, const RVec& x0, const RVec& L, const RVec& U) const {
    int n = (int)x0.size();
    double dl = xt[j] - L[j], du = U[j] - xt[j];
    double dL0 = x0[j] - L[j], dU0 = U[j] - x0[j];
    double s = 0.0;
    if (P[i * n + j] > 0.0) s += P[i * n + j] * dL0 / (dl * dl);
    if (Q[i * n + j] > 0.0) s += Q[i * n + j] * dU0 / (du * du);
    return s;
  }
  // d2 ftilde_0 / dx_j^2
  double d2ftilde_dx2(int j, const RVec& P, const RVec& Q,
                      const RVec& xt, const RVec& x0, const RVec& L, const RVec& U) const {
    double dl = xt[j] - L[j], du = U[j] - xt[j];
    double dL0 = x0[j] - L[j], dU0 = U[j] - x0[j];
    double s = 0.0;
    if (P[j] > 0.0) s -= 2.0 * P[j] * dL0 / (dl * dl * dl);
    if (Q[j] > 0.0) s += 2.0 * Q[j] * dU0 / (du * du * du);
    return s;
  }

  // Primal-dual interior-point Newton solver for the MMA subproblem
  void solve_DIP(const NLP& p, const RVec& P, const RVec& Q, const RVec& fv,
                 RVec& xs, double& F1, double& F2, int& iters) {
    int n = p.n, m = p.m, m1 = m + 1;
    (void)m1;
    xs = x;
    double mu = mu0;
    RVec lam_ = lam;
    if (lam_.empty()) lam_.assign(m, 1.0);
    for (int i = 0; i < m; ++i) lam_[i] = std::max(1e-3, lam_[i]);
    iters = 0;
    const double delta = 1e-7;
    auto residual = [&](const RVec& xt, const RVec& lm, double& r1, double& r2) {
      r1 = 0.0;
      RVec grad(n, 0.0);
      for (int j = 0; j < n; ++j)
        grad[j] = dftilde_dx(0, j, P, Q, xt, x, L, U);
      for (int i = 0; i < m; ++i)
        for (int j = 0; j < n; ++j)
          grad[j] += lm[i] * dftilde_dx(i + 1, j, P, Q, xt, x, L, U);
      for (double v : grad) r1 = std::max(r1, std::fabs(v));
      r2 = 0.0;
      for (int i = 0; i < m; ++i) {
        double g = ftilde_i(i + 1, P, Q, fv, xt, x, L, U) - fv[i + 1];
        double v = std::fabs(g + mu - lm[i] / mu);
        r2 = std::max(r2, v);
      }
    };
    residual(xs, lam_, F1, F2);
    int mu_stalls = 0;
    for (iters = 0; iters < max_dip; ++iters) {
      if (F1 + F2 < dip_tol) break;
      // Newton step: solve the KKT linear system
      // [ H+J^T J^T ; J  -diag(1/mu) ] [dx; dl] = -[F1; F2]
      RVec H(n), Jrow(n), rhs1(n), rhs2(m);
      for (int j = 0; j < n; ++j) H[j] = d2ftilde_dx2(j, P, Q, xs, x, L, U);
      for (int i = 0; i < m; ++i) {
        for (int j = 0; j < n; ++j)
          Jrow[j] = dftilde_dx(i + 1, j, P, Q, xs, x, L, U);
        for (int j = 0; j < n; ++j) rhs1[j] -= lam_[i] * Jrow[j];
        double g = ftilde_i(i + 1, P, Q, fv, xs, x, L, U) - fv[i + 1];
        rhs2[i] = -(g + mu - lam_[i] / mu);
      }
      for (int j = 0; j < n; ++j) rhs1[j] -= dftilde_dx(0, j, P, Q, xs, x, L, U);
      // block elimination: from 2nd eq: dl = -mu*(J dx - rhs2)  ... solve reduced:
      // (H + J^T (1/mu) J) dx = rhs1 + J^T rhs2 / (1/mu) ... derive:
      // F2 + J dx - (1/mu) dl = rhs2   -> dl = mu (J dx - rhs2 + 0)  ...
      // F2_new = g + mu - (lambda + dl)/mu; linearized: J dx - dl/mu = -rhs2 -> dl = mu*(J dx + rhs2)??
      // careful: F2 = g + mu - lam/mu ; dF2 = J dx - dl/mu = -rhs2  => dl = mu (J dx + rhs2)??
      // dF2 + rhs2 = 0: J dx - dl/mu + rhs2 = 0  =>  dl = mu (J dx + rhs2)... 
      // substitute in F1: (H + J^T)(dx) + (1/mu) dl = -rhs1
      // => (H + J^T J /mu) dx = -rhs1 - (J^T rhs2)/mu  ... (sign: J^T dl/mu = J^T (J dx + rhs2))
      // => (H + J^T J/mu) dx = -rhs1 - J^T(J dx + rhs2)/mu  -> move: (H + J^T J/mu) dx = -rhs1 - J^T rhs2/mu
      RVec rhs(n);
      for (int j = 0; j < n; ++j) {
        double s = -rhs1[j];
        for (int i = 0; i < m; ++i) s -= Jrow[j] * rhs2[i] / mu;
        rhs[j] = s;
      }
      RMat A11(n, RVec(n, 0.0));
      for (int j = 0; j < n; ++j)
        for (int k = 0; k < n; ++k) {
          double s = (j == k) ? H[j] : 0.0;
          for (int i = 0; i < m; ++i) s += Jrow[j] * Jrow[k] / mu;
          A11[j][k] = s;
        }
      // symmetrize & tiny ridge for numerical stability
      for (int j = 0; j < n; ++j)
        for (int k = j + 1; k < n; ++k)
          A11[j][k] = A11[k][j] = 0.5 * (A11[j][k] + A11[k][j]);
      for (int j = 0; j < n; ++j) A11[j][j] += 1e-14;
      DenseLU<double> lu;
      bool ok = true;
      try { lu.factor(A11); } catch (...) { ok = false; }
      if (!ok) { mu *= 0.5; continue; }
      RVec dx;
      lu.solve(rhs, dx);
      RVec dl(n);
      for (int j = 0; j < n; ++j) {
        double s = 0.0;
        for (int i = 0; i < m; ++i) s += Jrow[j] * dx[j] * 0.0; // placeholder (unused)
        (void)s;
      }
      // compute dl from eliminated equation
      RVec dlam(m);
      for (int i = 0; i < m; ++i) {
        double s = 0.0;
        for (int j = 0; j < n; ++j) s += Jrow[j] * dx[j];
        dlam[i] = mu * (s + rhs2[i]);
      }
      (void)dl;
      // backtracking with safeguards
      double al = 1.0;
      double base1 = F1, base2 = F2;
      bool acc = false;
      for (int bt = 0; bt < 30; ++bt) {
        RVec xt(n), lm(m);
        bool feasible = true;
        for (int j = 0; j < n; ++j) {
          xt[j] = xs[j] + al * dx[j];
          if (xt[j] <= L[j] + delta || xt[j] >= U[j] - delta) feasible = false;
        }
        for (int i = 0; i < m; ++i) {
          lm[i] = lam_[i] + al * dlam[i];
          if (lm[i] < 0.0) feasible = false;
          if (!feasible) break;
        }
        if (feasible) {
          double r1, r2;
          residual(xt, lm, r1, r2);
          if (r1 + r2 <= (1.0 - 1e-4 * al) * (base1 + base2)) {
            xs = xt; lam_ = lm; F1 = r1; F2 = r2;
            acc = true;
            break;
          }
        }
        al *= 0.5;
      }
      if (!acc) {
        if (++mu_stalls >= 3) { mu *= mu_decay; mu_stalls = 0; }
        if (mu < 1e-6) break;
      } else {
        mu_stalls = 0;
        if ((iters + 1) % 4 == 0) mu *= mu_decay;
      }
    }
    lam = lam_;
    // final clip into (L, U)
    for (int j = 0; j < n; ++j)
      xs[j] = std::min(std::max(xs[j], L[j] + 1e-9), U[j] - 1e-9);
  }
};

}  // namespace mo
