// ============================================================================
// opt_cockpit — Phase-4 MDO verification driver
//   Usage: opt_cockpit [suite] [--key value]...
//   suites: all chain mma revolve fosm hybrid
//   keys  : grid beta rmin eta rho vol iters nsteps slots kappa mc nxi
//           gens pop islands seed material(simp|ramp)
//   Prints a single JSON report on stdout; progress on stderr.
// ============================================================================
#include "mo/core.hpp"
#include "mo/mma.hpp"
#include "mo/revolve.hpp"
#include "mo/fosm.hpp"
#include "mo/evolvers.hpp"
#include <cstdio>
#include <cstring>
#include <chrono>
#include <cmath>
#include <map>
#include <vector>
#include <string>

using namespace mo;
using Clock = std::chrono::steady_clock;

static double ts() { return std::chrono::duration<double>(Clock::now().time_since_epoch()).count(); }
static void progress(const std::string& m) { std::fprintf(stderr, "[opt] %s\n", m.c_str()); std::fflush(stderr); }

// ---------------- minimal JSON output ----------------
struct JO {
  std::string s;
  bool first_item = true;
  void item(const std::string& k, const std::string& v) {
    s += first_item ? " " : ", ";
    first_item = false;
    s += "\"" + k + "\":" + v;
  }
  void num(const std::string& k, double v) {
    char b[40];
    if (!(v == v)) snprintf(b, sizeof b, "null");
    else if (v == 0.0) snprintf(b, sizeof b, "0");
    else if (std::fabs(v) >= 1e16 || std::fabs(v) < 1e-4) snprintf(b, sizeof b, "%.6e", v);
    else snprintf(b, sizeof b, "%.9g", v);
    item(k, b);
  }
  void bo(const std::string& k, bool v) { item(k, v ? "true" : "false"); }
  void arr(const std::string& k, const std::vector<double>& a, int prec = 6) {
    std::string t = "[";
    for (size_t i = 0; i < a.size(); ++i) {
      if (i) t += ",";
      char b[40];
      snprintf(b, sizeof b, prec == 3 ? "%.3e" : (prec == 4 ? "%.4e" : ("%.6g")), a[i]);
      t += b;
    }
    t += "]";
    item(k, t);
  }
};

static std::map<std::string, std::string> parse_args(int argc, char** argv) {
  std::map<std::string, std::string> p;
  p["suite"] = "all";
  for (int i = 1; i < argc; ++i) {
    std::string a = argv[i];
    if (a.rfind("--", 0) == 0 && i + 1 < argc) p[a.substr(2)] = argv[++i];
    else if (a.find("--") != 0) p["suite"] = a;
  }
  auto d = [&](const char* k, double def) {
    auto it = p.find(k);
    if (it == p.end()) return def;
    return std::atof(it->second.c_str());
  };
  auto s_ = [&](const char* k, const char* def) {
    auto it = p.find(k);
    if (it == p.end()) return std::string(def);
    return it->second;
  };
  (void)d; (void)s_;
  return p;
}
static double P(const std::map<std::string, std::string>& p, const char* k, double def) {
  auto it = p.find(k);
  return it == p.end() ? def : std::atof(it->second.c_str());
}
static std::string S(const std::map<std::string, std::string>& p, const char* k, const char* def) {
  auto it = p.find(k);
  return it == p.end() ? std::string(def) : it->second;
}

static double Tref_holder = 1.0;
static double helm_res = 0.0;
static bool classic_res = false;
static double classic_f_out = 0.0;
static double classic_dx_out = 0.0;
static RVec dtmp;
static JO shekel_out, topo_out;
static bool shekel_pass = false, topo_pass = false;

// ============================================================================
// SUITE 1 — filter chain: Helmholtz + Heaviside + SIMP/RAMP + KS, exact VJP
// ============================================================================
static void suite_chain(const std::map<std::string, std::string>& p, JO& out) {
  progress("chain: building unstructured FVM graph");
  int nx = (int)P(p, "grid", 12.0);
  double rmin = P(p, "rmin", 0.15), eta = P(p, "eta", 0.35), rho = P(p, "rho", 10.0);
  int beta = (int)P(p, "beta", 16.0);
  Model M;
  M.build(nx, nx, (int)P(p, "seed", 7.0));
  int n = M.n;

  Rng rng(31);
  RVec gamma0(n);
  for (int i = 0; i < n; ++i) gamma0[i] = 0.20 + 0.60 * rng.u();

  // reference T for KS shift
  {
    RVec gbar(n);
    {
      CVec gc(n);
      RVec gd = gamma0;
      helm_solve<double>(M, rmin, gd, gd);
      heaviside<double>(gd, beta, eta, gd);
      gbar = gd;
      (void)gc;
    }
    RMat A = M.cond_mat(gbar);
    DenseLU<double> lu; lu.factor(A);
    RVec T; lu.solve(M.b0, T);
    Tref_holder = *std::max_element(T.begin(), T.end());
  }
  (void)0;

  // VJP
  Chain<double> ch(M, (double)beta, rmin, eta, rho, Tref_holder);
  double J0 = ch.forward(gamma0);
  RVec vjp = ch.dJ_dgamma;

  // complex-step (forward-only) per direction
  const double hcs = 1e-30;
  double maxcs = 0.0, maxfd = 0.0, maxcs_abs = 0.0, maxfd_abs = 0.0;
  {
    progress("chain: complex-step verification (n = " + std::to_string(n) + " directions)");
    RVec cs(n);
    for (int i = 0; i < n; ++i) {
      CVec g(n);
      for (int k = 0; k < n; ++k) g[k] = cplx(gamma0[k], 0.0);
      g[i] += cplx(0.0, hcs);
      cplx Jc = chain_forward_only<cplx>(M, g, beta, rmin, eta, rho, Tref_holder);
      cs[i] = std::imag(Jc) / hcs;
    }
    double scale = 1.0;
    for (double v : cs) scale = std::max(scale, std::fabs(v));
    for (int i = 0; i < n; ++i) {
      maxcs_abs = std::max(maxcs_abs, std::fabs(vjp[i] - cs[i]));
      maxcs = std::max(maxcs, std::fabs(vjp[i] - cs[i]) / scale);
    }
  }
  {
    progress("chain: central finite-difference cross-check");
    const double h = 1e-5;
    RVec fdv(n);
    for (int i = 0; i < n; ++i) {
      RVec gp = gamma0, gm = gamma0;
      gp[i] += h; gm[i] -= h;
      double jp = std::real(chain_forward_only<double>(M, gp, beta, rmin, eta, rho, Tref_holder));
      double jm = std::real(chain_forward_only<double>(M, gm, beta, rmin, eta, rho, Tref_holder));
      fdv[i] = (jp - jm) / (2 * h);
    }
    double scale = 1.0;
    for (double v : fdv) scale = std::max(scale, std::fabs(v));
    for (int i = 0; i < n; ++i) {
      maxfd_abs = std::max(maxfd_abs, std::fabs(vjp[i] - fdv[i]));
      maxfd = std::max(maxfd, std::fabs(vjp[i] - fdv[i]) / scale);
    }
  }
  // Helmholtz PDE residual
  {
    RVec gtilde;
    helm_solve<double>(M, rmin, gamma0, gtilde);
    RMat H = M.helm_mat(rmin);
    RVec r(n), Hg(n);
    for (int i = 0; i < n; ++i) {
      double s = 0;
      for (int j = 0; j < n; ++j) s += H[i][j] * gtilde[j];
      r[i] = s - gamma0[i];
    }
    helm_res = nrm2(r) / (nrm2(gamma0) + 1e-30);
  }
  // per beta_H continuation step (complex step, cheap: reuse real VJP scale)
  std::vector<double> per_beta_b, per_beta_r;
  {
    progress("chain: beta_H schedule verification {1,2,4,8,16,32}");
    double bs[6] = {1, 2, 4, 8, 16, 32};
    for (double b : bs) {
      Chain<double> c2(M, b, rmin, eta, rho, Tref_holder);
      c2.forward(gamma0);
      RVec v2 = c2.dJ_dgamma;
      double mx = 0.0;
      for (int i = 0; i < n; i += 7) {  // subsample to keep runtime bounded
        CVec g(n);
        for (int k = 0; k < n; ++k) g[k] = cplx(gamma0[k], 0.0);
        g[i] += cplx(0.0, hcs);
        cplx Jc = chain_forward_only<cplx>(M, g, (double)b, rmin, eta, rho, Tref_holder);
        double cs = std::imag(Jc) / hcs;
        mx = std::max(mx, std::fabs(v2[i] - cs) / (1.0 + std::fabs(cs)));
      }
      per_beta_b.push_back(b);
      per_beta_r.push_back(mx);
    }
  }
  // RAMP material model check (subsampled complex step)
  double ramp_res = 0.0;
  {
    progress("chain: RAMP material model complex-step check");
    Model Mr = M;
    Mr.use_ramp = true; Mr.ramp_g0 = 0.05; Mr.ramp_p = 1.0;
    Chain<double> cr(Mr, (double)beta, rmin, eta, rho, Tref_holder);
    cr.forward(gamma0);
    RVec v2 = cr.dJ_dgamma;
    for (int i = 0; i < n; i += 5) {
      CVec g(n);
      for (int k = 0; k < n; ++k) g[k] = cplx(gamma0[k], 0.0);
      g[i] += cplx(0.0, hcs);
      cplx Jc = chain_forward_only<cplx>(Mr, g, beta, rmin, eta, rho, Tref_holder);
      double cs = std::imag(Jc) / hcs;
      ramp_res = std::max(ramp_res, std::fabs(v2[i] - cs) / (1.0 + std::fabs(cs)));
    }
  }
  JO j;
  j.num("n", n);
  j.num("J0", J0);
  j.num("max_cs_res", maxcs);
  j.num("max_cs_abs", maxcs_abs);
  j.num("max_fd_res", maxfd);
  j.num("max_fd_abs", maxfd_abs);
  j.num("helm_pde_res", helm_res);
  j.arr("per_beta_b", per_beta_b);
  j.arr("per_beta_res", per_beta_r, 3);
  j.num("ramp_cs_res", ramp_res);
  j.bo("pass", maxcs < 1e-11 && maxfd < 1e-4 && helm_res < 1e-12 && ramp_res < 1e-11);
  out.item("chain", "{" + j.s + "}");
}

// ============================================================================
// mean-temperature objective + gradient (for MMA / hybrid)
// ============================================================================
static double meanT_grad(const Model& M, const RVec& gamma, double betaH, double rmin,
                         double eta, RVec& dg) {
  int n = M.n;
  RVec gtilde, gbar, T;
  helm_solve<double>(M, rmin, gamma, gtilde);
  heaviside<double>(gtilde, betaH, eta, gbar);
  RMat A = M.cond_mat(gbar);
  RMat AT(n, RVec(n, 0.0));
  for (int i = 0; i < n; ++i) for (int j2 = 0; j2 < n; ++j2) AT[i][j2] = A[j2][i];
  DenseLU<double> lu, luT;
  lu.factor(A); luT.factor(AT);
  lu.solve(M.b0, T);
  RVec t1;
  luT.solve(M.Aarea, t1);
  double J = dot(M.Aarea, T);
  RVec dk(n);
  for (int i = 0; i < n; ++i) dk[i] = M.kval_der(gbar[i]);
  RVec dJg(n, 0.0);
  for (int i = 0; i < n; ++i) {
    double s = 0.0;
    for (auto& pr : M.adj[i]) {
      int jj = pr.first; double w0 = pr.second;
      s += 0.5 * w0 * (t1[i] - t1[jj]) * (T[i] - T[jj]);
    }
    dJg[i] = -dk[i] * s;
  }
  auto h = heaviside_der<double>(gtilde, betaH, eta, n);
  RVec sv(n);
  for (int i = 0; i < n; ++i) sv[i] = h[i] * dJg[i];
  RMat H = M.helm_mat(rmin);
  DenseLU<double> luH; luH.factor(H);
  luH.solve(sv, dg);
  return J;
}
static double volume_of(const Model& M, const RVec& gamma, double betaH, double rmin,
                        double eta, RVec& dg) {
  int n = M.n;
  RVec gtilde, gbar;
  helm_solve<double>(M, rmin, gamma, gtilde);
  heaviside<double>(gtilde, betaH, eta, gbar);
  auto h = heaviside_der<double>(gtilde, betaH, eta, n);
  RVec sv(n);
  for (int i = 0; i < n; ++i) sv[i] = h[i] * M.Aarea[i];
  RMat H = M.helm_mat(rmin);
  DenseLU<double> luH; luH.factor(H);
  luH.solve(sv, dg);
  return M.volume(gbar);
}
// KS(temperature) objective + gradient
static double ksT_grad(const Model& M, const RVec& gamma, double betaH, double rmin,
                       double eta, double rho, double Tref, RVec& dg) {
  Chain<double> ch(M, betaH, rmin, eta, rho, Tref);
  double J = ch.forward(gamma);
  dg = ch.dJ_dgamma;
  return J;
}
// KS(flux^2) stress proxy + gradient
static double ksS_grad(const Model& M, const RVec& gamma, double betaH, double rmin,
                       double eta, double rho, double Sref, RVec& dg) {
  int n = M.n;
  RVec gtilde, gbar, T;
  helm_solve<double>(M, rmin, gamma, gtilde);
  heaviside<double>(gtilde, betaH, eta, gbar);
  RMat A = M.cond_mat(gbar);
  RMat AT(n, RVec(n, 0.0));
  for (int i = 0; i < n; ++i) for (int j2 = 0; j2 < n; ++j2) AT[i][j2] = A[j2][i];
  DenseLU<double> lu, luT;
  lu.factor(A); luT.factor(AT);
  lu.solve(M.b0, T);
  RVec f = M.fluxes(gbar, T);
  RVec s(n);
  for (int i = 0; i < n; ++i) s[i] = f[i] * f[i];
  std::vector<double> w = ks_w<double>(s, Sref, rho);
  double J = ks_agg<double>(s, Sref, rho);
  // c_T: cT_i = sum_{j in N(i)} K_ij (2 w_i f_i) ; cT_j -= K_ij (2 w_i f_i)
  RVec cT(n, 0.0);
  RVec k = M.kfield(gbar);
  {
    for (int i = 0; i < n; ++i) {
      double ci = 2.0 * w[i] * f[i];
      if (std::fabs(ci) < 1e-300) continue;
      for (auto& pr : M.adj[i]) {
        int jj = pr.first; double w0 = pr.second;
        double K = w0 * 0.5 * (k[i] + k[jj]);
        cT[i] += K * ci;
        cT[jj] -= K * ci;
      }
    }
  }
  RVec t1;
  luT.solve(cT, t1);
  RVec dk(n);
  for (int i = 0; i < n; ++i) dk[i] = M.kval_der(gbar[i]);
  RVec dJg(n, 0.0);
  for (int i = 0; i < n; ++i) {
    double sT = 0.0, expl = 0.0;
    for (auto& pr : M.adj[i]) {
      int jj = pr.first; double w0 = pr.second;
      sT += 0.5 * w0 * (t1[i] - t1[jj]) * (T[i] - T[jj]);
      expl += w0 * (w[i] * f[i] * f[i] * (T[i] - T[jj]) + w[jj] * f[jj] * f[jj] * (T[jj] - T[i]));
    }
    dJg[i] = -dk[i] * sT + dk[i] * expl;
  }
  auto h = heaviside_der<double>(gtilde, betaH, eta, n);
  RVec sv(n);
  for (int i = 0; i < n; ++i) sv[i] = h[i] * dJg[i];
  RMat H = M.helm_mat(rmin);
  DenseLU<double> luH; luH.factor(H);
  luH.solve(sv, dg);
  return J;
}

// ============================================================================
// SUITE 2 — Svanberg MMA (classic benchmark + real thermo-topology run)
// ============================================================================
static void suite_mma(const std::map<std::string, std::string>& p, JO& out) {
  // ---- Svanberg classic 4-variable example ----
  {
    progress("mma: Svanberg classic example (4-D)");
    NLP nlp;
    nlp.n = 4; nlp.m = 3;
    nlp.lb = {0.1, 0.1, -10.0, 0.1};
    nlp.ub = {10.0, 10.0, 10.0, 10.0};
    nlp.f = [](const RVec& x) {
      double c[4] = {1, 2, 2, 1};
      double s = 0;
      for (int i = 0; i < 4; ++i) s += (x[i] - c[i]) * (x[i] - c[i]);
      return s;
    };
    nlp.g_f = [](const RVec& x, RVec& g) {
      double c[4] = {1, 2, 2, 1};
      for (int i = 0; i < 4; ++i) g[i] = 2 * (x[i] - c[i]);
    };
    nlp.g_c = [](const RVec& x, RVec& v, RVec* g) {
      v[0] = 25.0 - (x[0] - 1) * (x[0] - 1) * (x[1] - 2) * (x[1] - 2);
      v[1] = 40.0 - x[0] * x[1] * x[2] * x[3];
      v[2] = x[0]*x[0] + x[1]*x[1] + x[2]*x[2] + x[3]*x[3] - 30.0;
      if (g) {
        for (int i = 0; i < 4; ++i) {
          (*g)[i] = -2 * (x[0] - 1) * (x[1] - 2) * (i == 0 ? 1 : 0)
                  - 2 * (x[1] - 2) * (x[0] - 1) * (i == 1 ? 1 : 0);
          (*g)[4 + i] = -x[1] * x[2] * x[3] * (i == 0 ? 1 : 0)
                      - x[0] * x[2] * x[3] * (i == 1 ? 1 : 0)
                      - x[0] * x[1] * x[3] * (i == 2 ? 1 : 0)
                      - x[0] * x[1] * x[2] * (i == 3 ? 1 : 0);
          (*g)[8 + i] = 2 * x[i];
        }
      }
    };
    MMA mma;
    mma.maxit = 80;
    RVec x0 = {1.0, 1.0, 1.0, 1.0};
    mma.init(nlp, x0);
    MMAIterInfo inf;
    double fcls = 17.5529, fend = 0;
    RVec xend = x0;
    for (int it = 0; it < 80; ++it) {
      bool more = mma.step(nlp, inf);
      fend = inf.f;
      xend = mma.current();
      if (!more) break;
    }
    double dx = 0;
    double xs[4] = {2.3304, 1.9492, -1.8171, 1.7917};
    for (int i = 0; i < 4; ++i) dx = std::max(dx, std::fabs(xend[i] - xs[i]));
    fcls = 17.5529;
    (void)inf;
    classic_f_out = fend;
    classic_dx_out = dx;
    classic_res = (std::fabs(fend - fcls) < 0.05 && dx < 0.05);
  }
  // ---- real thermo-topology optimization ----
  progress("mma: thermo-fluid topology optimization (12x12)");
  int nx = (int)P(p, "grid", 12.0);
  double rmin = P(p, "rmin", 0.15), eta = P(p, "eta", 0.35), rho = P(p, "rho", 10.0);
  double vol_t = P(p, "vol", 0.35);
  int iters = (int)P(p, "iters", 36.0);
  Model M;
  M.build(nx, nx, (int)P(p, "seed", 7.0));
  int n = M.n;
  double bsched[6] = {1, 2, 4, 8, 16, 32};

  // reference thresholds
  RVec gbar_ref(n, eta);
  RMat Aref = M.cond_mat(gbar_ref);
  DenseLU<double> lu; lu.factor(Aref);
  RVec Tref; lu.solve(M.b0, Tref);
  RVec fref = M.fluxes(gbar_ref, Tref);
  double Tmax = 1.12 * *std::max_element(Tref.begin(), Tref.end());
  double smax = 1.25 * 0.0;
  for (int i = 0; i < n; ++i) smax = std::max(smax, fref[i] * fref[i]);
  double TrefKS = *std::max_element(Tref.begin(), Tref.end());

  NLP nlp;
  nlp.n = n; nlp.m = 3;
  nlp.lb.assign(n, 0.001);
  nlp.ub.assign(n, 1.0);
  // beta_H is advanced per continuation phase via captured state
  double cur_beta = bsched[0];
  nlp.f = [&](const RVec& g) -> double {
    RVec dg; (void)dg;
    double J = 0;
    {
      // mean temperature with gradient discarded (cheap path)
      RVec gg = g;
      RVec gtilde, gbar, T;
      helm_solve<double>(M, rmin, gg, gtilde);
      heaviside<double>(gtilde, cur_beta, eta, gbar);
      RMat A = M.cond_mat(gbar);
      DenseLU<double> l2; l2.factor(A);
      l2.solve(M.b0, T);
      J = dot(M.Aarea, T);
    }
    return J;
  };
  nlp.g_f = [&](const RVec& g, RVec& gr) {
    meanT_grad(M, g, cur_beta, rmin, eta, gr);
  };
  nlp.g_c = [&](const RVec& g, RVec& v, RVec* gr) {
    RVec d1, d2, d3;
    v[0] = volume_of(M, g, cur_beta, rmin, eta, d1) - vol_t;
    v[1] = ksT_grad(M, g, cur_beta, rmin, eta, rho, TrefKS, d2) - Tmax;
    v[2] = ksS_grad(M, g, cur_beta, rmin, eta, rho, 0.0, d3) - smax;
    if (gr) {
      for (int i = 0; i < n; ++i) {
        (*gr)[i] = d1[i];
        (*gr)[n + i] = d2[i];
        (*gr)[2 * n + i] = d3[i];
      }
    }
  };

  MMA mma;
  mma.maxit = iters;
  RVec g0(n, 0.65);
  mma.init(nlp, g0);
  std::vector<double> hist_f, hist_v, hist_g, hist_kkt, hist_step;
  MMAIterInfo finf;
  double f_start = nlp.f(g0);
  for (int it = 0; it < iters; ++it) {
    cur_beta = bsched[std::min(5, it / std::max(1, iters / 6))];
    bool more = mma.step(nlp, finf);
    hist_f.push_back(finf.f);
    hist_v.push_back(volume_of(M, mma.current(), cur_beta, rmin, eta, dtmp));
    hist_g.push_back(finf.max_g);
    hist_kkt.push_back(finf.sub_F1 + finf.sub_F2);
    hist_step.push_back(finf.step);
    if (!more) break;
  }
  RVec gfinal = mma.current();
  RVec gbar_f;
  {
    RVec gt;
    helm_solve<double>(M, rmin, gfinal, gt);
    heaviside<double>(gt, cur_beta, eta, gbar_f);
  }
  double V_final = M.volume(gbar_f);
  JO j;
  j.arr("f_hist", hist_f);
  j.arr("v_hist", hist_v);
  j.arr("gmax_hist", hist_g, 4);
  j.arr("kkt_hist", hist_kkt, 4);
  j.num("f_start", f_start);
  j.num("f_end", finf.f);
  j.num("volume", V_final);
  j.num("volume_target", vol_t);
  j.num("gmax", finf.max_g);
  j.num("kkt_final", finf.sub_F1 + finf.sub_F2);
  j.arr("topo", gbar_f, 4);
  j.num("grid", nx);
  j.num("classic_f", classic_f_out);
  j.bo("classic_pass", classic_res);
  j.bo("pass", classic_res && std::fabs(V_final - vol_t) < 0.005
              && finf.max_g < 1e-3 && finf.sub_F1 + finf.sub_F2 < 1e-6
              && finf.f < f_start);
  out.item("mma", "{" + j.s + "}");
}
// ============================================================================
// SUITE 3 — Revolve binomial checkpointing for unsteady adjoint
// ============================================================================
static void suite_revolve(const std::map<std::string, std::string>& p, JO& out) {
  progress("revolve: unsteady nonlinear conduction adjoint (binomial checkpointing)");
  int m = (int)P(p, "nsteps", 400.0);
  int S = (int)P(p, "slots", 12.0);
  Model M;
  M.build(8, 8, 11);
  int n = M.n;
  double dt = 0.02, alpha = 0.05, Ttot = (double)m * dt;
  RMat L = M.laplacian();
  RVec b0 = M.b0;
  // nonlinear unsteady system: dX/dt = -L X - alpha X.oX + b0 (1 + 0.1 sin(2 pi t / T))
  auto F = [&](const RVec& X, int k) -> RVec {
    double t = (k + 1) * dt;
    double src = 1.0 + 0.1 * std::sin(2.0 * M_PI * t / Ttot);
    RVec Y(n);
    for (int i = 0; i < n; ++i) {
      double s = 0.0;
      for (int j = 0; j < n; ++j) s += L[i][j] * X[j];
      Y[i] = X[i] + dt * (-s - alpha * X[i] * X[i] + b0[i] * src);
    }
    return Y;
  };
  Rng rng(5);
  RVec w(n);
  for (int i = 0; i < n; ++i) w[i] = rng.nrm();
  double nw = nrm2(w);
  for (auto& v : w) v /= nw;
  // reference: full-history adjoint
  std::vector<RVec> hist(m + 1);
  hist[0].assign(n, 1.0);
  for (int k = 0; k < m; ++k) hist[k + 1] = F(hist[k], k);
  RVec y = w, yref(n);
  for (int k = m - 1; k >= 0; --k) {
    RVec yn(n);
    for (int i = 0; i < n; ++i) {
      double s = 0.0;
      for (int j = 0; j < n; ++j) s += L[i][j] * y[j];   // L is symmetric
      yn[i] = y[i] + dt * (s + 2.0 * alpha * hist[k][i] * y[i]);
    }
    y = yn;
  }
  yref = y;
  // revolve run
  auto plan = revolve_plan(m, S);
  Revolver rv;
  rv.S = S;
  rv.X0 = hist[0];
  rv.F = F;
  rv.JT = [&](const RVec& Xk, const RVec& yin, RVec& yout) {
    for (int i = 0; i < n; ++i) {
      double s = 0.0;
      for (int j = 0; j < n; ++j) s += L[i][j] * yin[j];
      yout[i] = yin[i] + dt * (s + 2.0 * alpha * Xk[i] * yin[i]);
    }
  };
  RVec y0;
  rv.run(m, plan.cps, w, y0);
  double rel = 0.0;
  {
    RVec d(n);
    for (int i = 0; i < n; ++i) d[i] = y0[i] - yref[i];
    rel = nrm2(d) / (nrm2(yref) + 1e-30);
  }
  JO j;
  j.num("n_steps", m);
  j.num("slots", S);
  j.num("checkpoints", plan.s);
  { RVec cpl; for (int v : plan.cps) cpl.push_back((double)v); j.arr("cp_positions", cpl); }
  j.num("fwd_initial", rv.stats.fwd_initial);
  j.num("fwd_recompute", rv.stats.fwd_recompute);
  j.num("fwd_total", rv.stats.total());
  j.num("naive_no_ckpt", (double)m * (m - 1) / 2.0);
  j.num("adjoint_rel_err", rel);
  // DP ground truth for small (m, S)
  double dp105 = RevolveDP(10, 5).optimum();
  double eng105 = 0.0;
  {
    auto pl = revolve_plan(10, 5);
    Revolver r2;
    r2.S = 5;
    r2.X0 = RVec(4, 1.0);
    auto Fid = [](const RVec& X, int) -> RVec { return X; };
    r2.F = Fid;
    r2.JT = [](const RVec&, const RVec& y, RVec& yo) { yo = y; };
    RVec yo;
    r2.run(10, pl.cps, RVec(4, 1.0), yo);
    eng105 = (double)r2.stats.fwd_recompute;
  }
  double dp114 = RevolveDP(11, 4).optimum();
  double eng114 = 0.0;
  {
    auto pl = revolve_plan(11, 4);
    Revolver r2;
    r2.S = 4;
    r2.X0 = RVec(4, 1.0);
    auto Fid = [](const RVec& X, int) -> RVec { return X; };
    r2.F = Fid;
    r2.JT = [](const RVec&, const RVec& y, RVec& yo) { yo = y; };
    RVec yo;
    r2.run(11, pl.cps, RVec(4, 1.0), yo);
    eng114 = (double)r2.stats.fwd_recompute;
  }
  j.num("dp_opt_m10_S5", dp105);
  j.num("engine_m10_S5", eng105);
  j.num("dp_opt_m11_S4", dp114);
  j.num("engine_m11_S4", eng114);
  // cost table over memory
  std::vector<double> tabS, tabC;
  for (int s2 = 2; s2 <= 16; ++s2) {
    auto pl = revolve_plan(m, s2);
    Revolver r2;
    r2.S = s2;
    r2.X0 = RVec(4, 1.0);
    auto Fid = [](const RVec& X, int) -> RVec { return X; };
    r2.F = Fid;
    r2.JT = [](const RVec&, const RVec& y, RVec& yo) { yo = y; };
    RVec yo;
    r2.run(m, pl.cps, RVec(4, 1.0), yo);
    tabS.push_back((double)s2);
    tabC.push_back((double)r2.stats.fwd_recompute);
  }
  j.arr("mem_slots", tabS);
  j.arr("recompute_vs_mem", tabC);
  j.bo("pass", rel < 1e-9 && std::fabs(eng105 - dp105) < 1e-9
              && std::fabs(eng114 - dp114) < 1e-9);
  out.item("revolve", "{" + j.s + "}");
}

// ============================================================================
// SUITE 4 — FOSM robust optimization (no Monte Carlo in the optimization loop)
// ============================================================================
static void suite_fosm(const std::map<std::string, std::string>& p, JO& out) {
  progress("fosm: robust field under spatially correlated uncertainty");
  int nx = (int)P(p, "grid", 11.0);
  int nxi = (int)P(p, "nxi", 16.0);
  int mc = (int)P(p, "mc", 10000.0);
  double kappa = P(p, "kappa", 3.0);
  Model M;
  M.build(nx, nx, 13);
  int n = M.n;
  FOSM F(M);
  F.rho = 10.0;
  F.setup(nxi, 0.4, 0.3, 0.25, 11);
  F.Tref = 0.0;
  RVec g0(n);
  for (int i = 0; i < n; ++i)
    g0[i] = 0.40 + 0.08 * std::sin(2 * M_PI * M.px[i]) * std::cos(2 * M_PI * M.py[i]);
  for (auto& v : g0) v = std::min(0.95, std::max(0.05, v));

  auto t1 = Clock::now();
  auto st = F.evaluate(g0);
  RVec dsig = F.dsigma_dgbar(st);
  RVec dg = F.dg_dgbar(st);
  auto t2 = Clock::now();
  double t_fosm = std::chrono::duration<double>(t2 - t1).count();

  // exact-FD verification of the HVP (single-forward sigma)
  double hvp_res = 0.0;
  {
    progress("fosm: HVP vs exact central FD");
    const double h = 1e-7;
    for (int i = 0; i < n; i += 3) {
      RVec gp = g0, gm = g0;
      gp[i] += h; gm[i] -= h;
      auto sp = F.evaluate(gp);
      auto sm = F.evaluate(gm);
      double fd = (sp.sig - sm.sig) / (2 * h);
      hvp_res = std::max(hvp_res, std::fabs(dsig[i] - fd) / (1.0 + std::fabs(fd)));
    }
  }
  // Monte Carlo reference (10 000 samples)
  progress("fosm: " + std::to_string(mc) + "-sample Monte Carlo reference");
  auto mc0 = F.monte_carlo(g0, mc, 99);
  double sig_rel_err = std::fabs(st.sig - mc0.sig) / (mc0.sig + 1e-30);
  double mu_rel_err = std::fabs(st.g0 - mc0.mu) / (mc0.mu + 1e-30);
  // MC finite-difference gradient of sigma
  double mcf_res = 0.0;
  {
    progress("fosm: MC finite-difference sensitivity of sigma");
    const double h = 1e-3;
    int msub = 4000;
    for (int i = 0; i < n; i += 4) {
      RVec gp = g0, gm = g0;
      gp[i] += h; gm[i] -= h;
      auto mp = F.monte_carlo(gp, msub, 7 + i);
      auto mm = F.monte_carlo(gm, msub, 1007 + i);
      double fd = (mp.sig - mm.sig) / (2 * h);
      mcf_res = std::max(mcf_res, std::fabs(dsig[i] - fd) / (1.0 + std::fabs(fd)));
    }
  }
  double Phi = st.g0 + kappa * st.sig;
  RVec dPhi(n);
  for (int i = 0; i < n; ++i) dPhi[i] = dg[i] + kappa * dsig[i];
  JO j;
  j.num("n", n);
  j.num("n_xi", nxi);
  j.num("sigma_fosm", st.sig);
  j.num("sigma_mc", mc0.sig);
  j.num("mu_fosm", st.g0);
  j.num("mu_mc", mc0.mu);
  j.num("sigma_rel_err", sig_rel_err);
  j.num("mu_rel_err", mu_rel_err);
  j.num("hvp_fd_res", hvp_res);
  j.num("mc_fd_rel_err", mcf_res);
  j.num("kappa", kappa);
  j.num("robust_obj", Phi);
  j.num("grad_norm", nrm_inf(dPhi));
  j.num("mc_samples", (double)mc);
  j.num("t_fosm_s", t_fosm);
  j.num("t_mc_s", mc0.t_s);
  j.num("speedup", mc0.t_s / (t_fosm + 1e-12));
  j.arr("dsigma_sample", RVec(dsig.begin(), dsig.begin() + std::min(12, n)), 3);
  j.bo("pass", sig_rel_err < 0.01 && hvp_res < 1e-8 && mcf_res < 0.01);
  out.item("fosm", "{" + j.s + "}");
}

// ============================================================================
// SUITE 5 — two-stage hybrid: MIGA + PSO (global) -> Nelder-Mead -> L-BFGS
// ============================================================================
static void suite_hybrid(const std::map<std::string, std::string>& p, JO& out) {
  progress("hybrid: MIGA/PSO global basin search");
  int gens = (int)P(p, "gens", 30.0);
  int pop = (int)P(p, "pop", 12.0);
  int islands = (int)P(p, "islands", 6.0);
  double kappa = P(p, "kappa", 2.5);

  // ---- Part A: Shekel-5 10-D benchmark ----
  {
    Box box;
    box.lb.assign(10, 0.0);
    box.ub.assign(10, 10.0);
    MIGA miga;
    miga.islands = islands; miga.pop = pop; miga.gens = gens;
    miga.box = box;
    miga.f1 = [](const RVec& x) { return shekel5(x); };
    miga.f2 = [](const RVec& x) {
      double s = 0;
      for (int i = 0; i < 10; ++i) s += (x[i] - 5.0) * (x[i] - 5.0);
      return 1e-4 * s;
    };
    miga.run();
    PSO pso;
    pso.np = 20; pso.iters = 90;
    pso.box = box;
    pso.f = [](const RVec& x) { return shekel5(x); };
    pso.run(miga.best, 0.35);
    NelderMead nm;
    nm.box = box;
    nm.f = [](const RVec& x) { return shekel5(x); };
    nm.maxfev = 4000;
    nm.run(pso.best);
    LBFGS lb;
    lb.box = box;
    lb.f = [](const RVec& x) { return shekel5(x); };
    lb.g = [](const RVec& x, RVec& g) { shekel5_grad(x, g); };
    lb.run(nm.best);
    double ffin = lb.bestf;
    // baseline: 12 random starts + L-BFGS only
    double fbase = 1e300;
    Rng rb(31337);
    RVec x0(10);
    for (int s = 0; s < 12; ++s) {
      for (int i = 0; i < 10; ++i) x0[i] = rb.u() * 10.0;
      LBFGS lb2;
      lb2.box = box;
      lb2.f = [](const RVec& x) { return shekel5(x); };
      lb2.g = [](const RVec& x, RVec& g) { shekel5_grad(x, g); };
      lb2.run(x0);
      fbase = std::min(fbase, lb2.bestf);
    }
    std::vector<double> traj = miga.gen_best1;
    for (double v : pso.gen_best) traj.push_back(v);
    for (double v : nm.gen_best) traj.push_back(v);
    for (double v : lb.gen_best) traj.push_back(v);
    JO js;
    js.num("f_final", ffin);
    js.num("f_global_ref", -538.3982);
    js.num("f_random_lbfgs_baseline", fbase);
    js.num("gnorm", lb.gnorm);
    js.arr("x", lb.best);
    js.arr("traj", traj);
    js.bo("pass", std::fabs(ffin - (-538.3982)) < 0.02 && ffin <= fbase + 1e-6);
    shekel_out = std::move(js);
    shekel_pass = std::fabs(ffin - (-538.3982)) < 0.02 && ffin <= fbase + 1e-6;
  }

  // ---- Part B: robust coarse topology (6x6 field) ----
  progress("hybrid: robust coarse topology optimization (6x6)");
  Model M;
  M.build(6, 6, 17);
  int n = M.n;
  FOSM F(M);
  F.rho = 8.0;
  F.setup(10, 0.5, 0.35, 0.30, 21);
  F.Tref = 0.0;
  auto Phi = [&](const RVec& gbar) -> double {
    auto st = F.evaluate(gbar);
    return st.g0 + kappa * st.sig;
  };
  auto dPhi = [&](const RVec& gbar, RVec& g) {
    auto st = F.evaluate(gbar);
    RVec ds = F.dsigma_dgbar(st);
    RVec dg = F.dg_dgbar(st);
    for (int i = 0; i < n; ++i) g[i] = dg[i] + kappa * ds[i];
  };
  Box box;
  box.lb.assign(n, 0.05);
  box.ub.assign(n, 0.95);
  MIGA miga;
  miga.islands = islands; miga.pop = pop; miga.gens = gens;
  miga.box = box;
  miga.f1 = [&](const RVec& x) { return Phi(x); };
  miga.f2 = [&](const RVec& x) { return M.volume(x); };
  miga.run();
  double phi0 = Phi(miga.best);
  PSO pso;
  pso.np = 20; pso.iters = 90;
  pso.box = box;
  pso.f = Phi;
  pso.run(miga.best, 0.4);
  NelderMead nm;
  nm.box = box;
  nm.f = Phi;
  nm.maxfev = 3000;
  nm.run(pso.best);
  LBFGS lb;
  lb.box = box;
  lb.f = Phi;
  lb.g = [&](const RVec& x, RVec& g) { dPhi(x, g); };
  lb.run(nm.best);
  double ffin = lb.bestf;
  double fbase = 1e300;
  Rng rb(4242);
  RVec x0(n);
  for (int s = 0; s < 10; ++s) {
    for (int i = 0; i < n; ++i) x0[i] = 0.05 + 0.9 * rb.u();
    LBFGS lb2;
    lb2.box = box;
    lb2.f = Phi;
    lb2.g = [&](const RVec& x, RVec& g) { dPhi(x, g); };
    lb2.run(x0);
    fbase = std::min(fbase, lb2.bestf);
  }
  std::vector<double> traj = miga.gen_best1;
  for (double v : pso.gen_best) traj.push_back(v);
  for (double v : nm.gen_best) traj.push_back(v);
  for (double v : lb.gen_best) traj.push_back(v);
  JO jt;
  jt.num("f_final", ffin);
  jt.num("f_random_lbfgs_baseline", fbase);
  jt.num("gnorm", lb.gnorm);
  jt.num("volume", M.volume(lb.best));
  jt.num("phi_at_miga", phi0);
  jt.arr("x", lb.best, 4);
  jt.arr("traj", traj);
  jt.bo("pass", ffin <= fbase + 1e-9 && lb.gnorm < 1e-4);
  topo_out = std::move(jt);
  topo_pass = ffin <= fbase + 1e-9 && lb.gnorm < 1e-4;
  JO j;
  j.item("shekel5", std::string("{") + shekel_out.s + std::string("}"));
  j.item("robust_topo", std::string("{") + topo_out.s + std::string("}"));
  j.bo("pass", shekel_pass && topo_pass);
  out.item("hybrid", "{" + j.s + "}");
}
int main(int argc, char** argv) {
  auto p = parse_args(argc, argv);
  std::string suite = p["suite"];
  auto t0 = Clock::now();
  JO out;
  out.s = "{\"suites\":{";
  if (suite == "all" || suite == "chain") { try { suite_chain(p, out); } catch (std::exception& e) { out.item("chain_err", std::string("\"") + e.what() + "\""); } }
  if (suite == "all" || suite == "mma") { try { suite_mma(p, out); } catch (std::exception& e) { out.item("mma_err", std::string("\"") + e.what() + "\""); } }
  if (suite == "all" || suite == "revolve") { try { suite_revolve(p, out); } catch (std::exception& e) { out.item("revolve_err", std::string("\"") + e.what() + "\""); } }
  if (suite == "all" || suite == "fosm") { try { suite_fosm(p, out); } catch (std::exception& e) { out.item("fosm_err", std::string("\"") + e.what() + "\""); } }
  if (suite == "all" || suite == "hybrid") { try { suite_hybrid(p, out); } catch (std::exception& e) { out.item("hybrid_err", std::string("\"") + e.what() + "\""); } }
  double wall = std::chrono::duration<double>(Clock::now() - t0).count();
  out.s += "},\"wall_s\":";
  {
    char b[40]; snprintf(b, sizeof b, "%.3f", wall);
    out.s += b;
  }
  out.s += "}";
  std::printf("%s\n", out.s.c_str());
  std::fflush(stdout);
  (void)t0;
  return 0;
}
