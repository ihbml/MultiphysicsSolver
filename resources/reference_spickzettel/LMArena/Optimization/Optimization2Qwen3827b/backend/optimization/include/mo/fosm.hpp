// ============================================================================
// mo/fosm.hpp — Generalized First-Order Second-Moment (FOSM) robust
// optimization without Monte Carlo (Kranz, Luedeker & Kriegesmann 2023 style)
//
//  Robust metric:        Phi_g(g) = mu_g(g) + kappa * sigma_g(g)
//  Stochastic direction: v_xi = Sigma_xi a / sigma,  a = dg/dxi
//  Exact sensitivity:    d(sigma_g)/dgamma = (dag/dgamma)^T v_xi
//                        = ONE tangent-over-adjoint (second-order adjoint)
//                          Hessian-vector product along v_xi:
//                          h(gamma) = a(gamma)^T v   =>  dh/dgamma
//  Model: unsteady-free conduction  A(gamma, xi) T = b0 + Phi xi
//         A = A0(gamma) + sum_j xi_j A'_j,   A'_j = eps * (k . phi_j) D-tilde
//         g = KS(T, Tref, rho)  (peak-temperature constraint surrogate)
//  Spatial exponential covariance: Sigma_ij = s^2 exp(-|x_i - x_j| / l_corr)
// ============================================================================
#pragma once
#include "core.hpp"
#include <chrono>

namespace mo {

struct FOSM {
  const Model& M;
  double rho = 10.0, Tref = 0.0;
  double sigma = 0.4, ell = 0.3, eps = 0.25;
  int nxi = 16;
  std::vector<RVec> phi;      // spatial basis (Gaussian bumps), max |phi_j| = 1
  RVec xc;                    // basis centers (2*nxi packed)
  RMat Sigma;
  Cholesky chol;

  explicit FOSM(const Model& M_) : M(M_) {}

  void setup(int nxi_, double sigma_, double ell_, double eps_, unsigned seed = 11) {
    nxi = nxi_; sigma = sigma_; ell = ell_; eps = eps_;
    int n = M.n;
    Rng rng(seed);
    phi.assign(nxi, RVec(n, 0.0));
    xc.assign(2 * nxi, 0.0);
    double bw = 0.09;
    for (int j = 0; j < nxi; ++j) {
      xc[2 * j] = 0.1 + 0.8 * rng.u();
      xc[2 * j + 1] = 0.1 + 0.8 * rng.u();
      double mx = 0.0;
      for (int i = 0; i < n; ++i) {
        double dx = M.px[i] - xc[2 * j], dy = M.py[i] - xc[2 * j + 1];
        phi[j][i] = std::exp(-(dx*dx + dy*dy) / (2 * bw * bw));
        mx = std::max(mx, std::fabs(phi[j][i]));
      }
      if (mx > 0) for (int i = 0; i < n; ++i) phi[j][i] /= mx;
    }
    Sigma.assign(nxi, RVec(nxi, 0.0));
    for (int i = 0; i < nxi; ++i)
      for (int j = 0; j < nxi; ++j) {
        double d = std::hypot(xc[2*i] - xc[2*j], xc[2*i+1] - xc[2*j+1]);
        Sigma[i][j] = sigma * sigma * std::exp(-d / ell);
      }
    chol.factor(Sigma);
  }

  // psi field: psi_i = sum_j xi_j phi_j(i)
  RVec psi(const RVec& xi) const {
    int n = M.n;
    RVec s(n, 0.0);
    for (int j = 0; j < nxi; ++j)
      if (std::fabs(xi[j]) > 0)
        for (int i = 0; i < n; ++i) s[i] += xi[j] * phi[j][i];
    return s;
  }
  RVec rhs(const RVec& xi) const {
    int n = M.n;
    RVec b = M.b0;
    for (int j = 0; j < nxi; ++j)
      for (int i = 0; i < n; ++i) b[i] += xi[j] * phi[j][i] * 2.0; // load amplitude
    return b;
  }
  // conduction matrix with material perturbed by psi
  RMat amat(const RVec& gbar, const RVec& psi_, RVec* kk = nullptr) const {
    int n = M.n;
    RVec k0 = M.kfield(gbar);
    RVec kkvec(n);
    for (int i = 0; i < n; ++i) kkvec[i] = k0[i] * (1.0 + eps * psi_[i]);
    if (kk) *kk = kkvec;
    RMat A(n, RVec(n, 0.0));
    for (auto& e : M.eg) {
      double K = e.w0 * 0.5 * (kkvec[e.i] + kkvec[e.j]);
      A[e.i][e.j] += -K; A[e.j][e.i] += -K;
      A[e.i][e.i] += K;  A[e.j][e.j] += K;
    }
    for (int i = 0; i < n; ++i) A[i][i] += M.hconv;
    return A;
  }

  struct State {
    RMat A;
    DenseLU<double> lu;
    RVec gbar, k, T, w, c, t_a, a, sig_a, v, w1;
    double g0 = 0.0, sig = 0.0;
  };

  // forward + grad_xi (at xi = 0) + stochastic direction
  State evaluate(const RVec& gbar) const {
    int n = M.n;
    State st;
    st.gbar = gbar;
    st.k = M.kfield(gbar);
    st.A = amat(gbar, RVec(n, 0.0));
    st.lu.factor(st.A);
    st.lu.solve(M.b0, st.T);
    st.w = ks_w<double>(st.T, Tref, rho);
    st.c.assign(n, 0.0);
    for (int i = 0; i < n; ++i) st.c[i] = st.w[i];   // dKS/dT_i = w_i
    st.g0 = ks_agg<double>(st.T, Tref, rho);
    // primary adjoint  t_a = A^{-T} c
    DenseLU<double> luT;
    {
      RMat AT(n, RVec(n, 0.0));
      for (int i = 0; i < n; ++i) for (int j = 0; j < n; ++j) AT[i][j] = st.A[j][i];
      luT.factor(AT);
    }
    luT.solve(st.c, st.t_a);
    // a_j = t_a^T phi_j - t_a^T A'_j T      (dg/dxi_j at xi = 0)
    st.a.assign(nxi, 0.0);
    for (int j = 0; j < nxi; ++j) {
      RVec ApT;
      // (A'_j v)_i = sum_{e=(i,r)} eps w0/2 (k_i phi_j(i) + k_r phi_j(r)) (v_i - v_r)
      ApT.assign(n, 0.0);
      for (auto& e : M.eg) {
        double dk = 0.5 * (st.k[e.i] * phi[j][e.i] + st.k[e.j] * phi[j][e.j]) * eps;
        double f = e.w0 * dk * (st.T[e.i] - st.T[e.j]);
        ApT[e.i] += f;
        ApT[e.j] -= f;
      }
      st.a[j] = 2.0 * dot(st.t_a, phi[j]) - dot(st.t_a, ApT);
    }
    // sigma = sqrt(a^T Sigma a);  v = Sigma a / sigma
    RVec Sa;
    for (int i = 0; i < nxi; ++i) {
      double s = 0.0;
      for (int j = 0; j < nxi; ++j) s += Sigma[i][j] * st.a[j];
      Sa[i] = s;
    }
    st.sig = std::sqrt(std::max(1e-300, dot(st.a, Sa)));
    st.sig_a = Sa;
    st.v.assign(nxi, 0.0);
    for (int i = 0; i < nxi; ++i) st.v[i] = Sa[i] / st.sig;
    st.w1.assign(n, 0.0);
    for (int j = 0; j < nxi; ++j)
      for (int i = 0; i < n; ++i) st.w1[i] += st.v[j] * phi[j][i];
    return st;
  }
  RVec a_of(const State& st) const { return st.a; }

  // A'_v v = (eps (k . w1) D-tilde) v
  RVec Apv_vec(const State& st, const RVec& v, const RVec* w1v = nullptr) const {
    int n = M.n;
    const RVec& w1 = (w1v ? *w1v : st.w1);
    RVec k1 = st.k;
    for (int i = 0; i < n; ++i) k1[i] *= w1[i];
    RVec r(n, 0.0);
    for (auto& e : M.eg) {
      double dk = 0.5 * (k1[e.i] + k1[e.j]) * eps;
      double f = e.w0 * dk * (v[e.i] - v[e.j]);
      r[e.i] += f;
      r[e.j] -= f;
    }
    return r;
  }

  // d(sigma)/dgamma via the single HVP along v (tangent-over-adjoint).
  //  h(gbar) = a^T v = 2 t_a^T w1 - t_a^T A'_v t_a
  //  dh/dg_i = dk'(g_i)/sigma * [ -2 E1_i - rho( (w.u_i)^T q1 - (w^T u_i)(w^T q1) )
  //                               - eps w1_i C_i ]
  //  where q1 = A^{-1} (2 w1 - 2 A'_v t_a) is the second-order adjoint
  //  (tangent-over-adjoint) along the stochastic direction.
  RVec dsigma_dgbar(const State& st) const {
    int n = M.n;
    RVec g(n, 0.0);
    RVec u1(n);
    {
      RVec tmp = Apv_vec(st, st.t_a);
      for (int i = 0; i < n; ++i) u1[i] = 2.0 * (st.w1[i] - tmp[i]);
    }
    RVec q1;
    st.lu.solve(u1, q1);
    double wq1 = dot(st.w, q1);
    auto Dtilde_b = [&](int i) -> RVec {
      RVec r(n, 0.0);
      for (auto& pr : M.adj[i]) {
        int j = pr.first; double w0 = pr.second;
        double f = 0.5 * w0 * (M.b0[i] - M.b0[j]);
        r[i] += f;
        r[j] -= f;
      }
      return r;
    };
    for (int i = 0; i < n; ++i) {
      RVec Db = Dtilde_b(i);
      RVec ui;
      st.lu.solve(Db, ui);
      double wu = dot(st.w, ui);
      double wuq = 0.0;
      for (int k = 0; k < n; ++k) wuq += st.w[k] * ui[k] * q1[k];
      double E1 = 0.0, C_ = 0.0;
      for (auto& pr : M.adj[i]) {
        int j = pr.first; double w0 = pr.second;
        E1 += 0.5 * w0 * (st.t_a[i] - st.t_a[j]) * (q1[i] - q1[j]);
        C_ += 0.5 * w0 * (st.t_a[i] - st.t_a[j]) * (st.t_a[i] - st.t_a[j]);
      }
      g[i] = -2.0 * E1 - rho * (wuq - wu * wq1) - eps * st.w1[i] * C_;
      g[i] *= M.kval_der(st.gbar[i]);
      g[i] /= st.sig;
    }
    return g;
  }

  // gradient of the mean objective g(gamma, 0) w.r.t. gbar
  RVec dg_dgbar(const State& st) const {
    int n = M.n;
    RVec g(n, 0.0);
    double wT = dot(st.w, st.T);
    auto Dtilde_b = [&](int i) -> RVec {
      RVec r(n, 0.0);
      for (auto& pr : M.adj[i]) {
        int j = pr.first; double w0 = pr.second;
        double f = 0.5 * w0 * (M.b0[i] - M.b0[j]);
        r[i] += f;
        r[j] -= f;
      }
      return r;
    };
    for (int i = 0; i < n; ++i) {
      RVec Db = Dtilde_b(i);
      RVec ui;
      st.lu.solve(Db, ui);
      double wu = dot(st.w, ui);
      double wuT = 0.0;
      for (int k = 0; k < n; ++k) wuT += st.w[k] * ui[k] * st.T[k];
      double E = 0.0;
      for (auto& pr : M.adj[i]) {
        int j = pr.first; double w0 = pr.second;
        E += 0.5 * w0 * (st.t_a[i] - st.t_a[j]) * (st.T[i] - st.T[j]);
      }
      g[i] = -E - rho * (wuT - wu * wT);
      g[i] *= M.kval_der(st.gbar[i]);
    }
    return g;
  }

  // ---- Monte Carlo reference ----
  struct MCOut { double mu = 0.0, sig = 0.0, t_s = 0.0; long long ns = 0; };
  MCOut monte_carlo(const RVec& gbar, int nsamp, unsigned seed = 99) const {
    int n = M.n;
    auto t0 = std::chrono::steady_clock::now();
    RVec k0 = M.kfield(gbar);
    Rng rng(seed);
    RVec z(nxi), xi(nxi);
    double mu = 0.0, mu2 = 0.0;
    for (int s = 0; s < nsamp; ++s) {
      for (int j = 0; j < nxi; ++j) z[j] = rng.nrm();
      chol.apply_L(z, xi);
      RVec ps = psi(xi);
      RVec k(n);
      for (int i = 0; i < n; ++i) k[i] = k0[i] * (1.0 + eps * ps[i]);
      RMat A(n, RVec(n, 0.0));
      for (auto& e : M.eg) {
        double K = e.w0 * 0.5 * (k[e.i] + k[e.j]);
        A[e.i][e.j] += -K; A[e.j][e.i] += -K;
        A[e.i][e.i] += K;  A[e.j][e.j] += K;
      }
      for (int i = 0; i < n; ++i) A[i][i] += M.hconv;
      RVec b = rhs(xi);
      DenseLU<double> lu;
      lu.factor(A);
      RVec T;
      lu.solve(b, T);
      double g = ks_agg<double>(T, Tref, rho);
      mu += g; mu2 += g * g;
    }
    auto t1 = std::chrono::steady_clock::now();
    MCOut o;
    o.mu = mu / nsamp;
    o.sig = std::sqrt(std::max(0.0, mu2 / nsamp - o.mu * o.mu));
    o.ns = nsamp;
    o.t_s = std::chrono::duration<double>(t1 - t0).count();
    return o;
  }
};

}  // namespace mo
