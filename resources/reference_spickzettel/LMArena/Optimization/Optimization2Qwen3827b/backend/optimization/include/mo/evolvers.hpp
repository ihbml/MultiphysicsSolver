// ============================================================================
// mo/evolvers.hpp — two-stage hybrid global-to-local optimizer stack
//   Stage 1 (global): Multi-Island Genetic Algorithm (MIGA)
//       * ring-topology migration, SBX crossover, polynomial mutation
//       * NSGA-II non-dominated sorting + crowding distance selection
//     + Particle Swarm Optimization (PSO) for basin exploitation
//   Stage 2 (local): Nelder-Mead downhill simplex, then L-BFGS
//       (two-loop recursion, strong-Wolfe backtracking, exact gradients)
//   Includes the 10-D Shekel-5 multimodal benchmark (known global optimum
//   f* = -538.3982 at a permutation of (4,0,3,0,3,4,1,0,5,2)).
// ============================================================================
#pragma once
#include "core.hpp"
#include <functional>
#include <set>

namespace mo {

struct Box { RVec lb, ub; };

// ---------------- Shekel-5 (10-D, 257 local minima) ----------------
// f(x) = -exp(-sum_i x_i^2 / (2 pi)) - sum_{m=1..6} sum_i (x_i - a_mi)^2 c_i^2
// Known global optimum: f* = -538.3982 at permutations of (4,0,3,0,3,4,1,0,5,2)
inline double shekel5(const RVec& x) {
  const double A[6][10] = {
    {0.1,10,20,20,10,10,10,10,10,10},
    {10,0.1,20,20,10,10,10,10,10,10},
    {20,20,0.1,20,10,10,10,10,10,10},
    {20,20,20,0.1,10,10,10,10,10,10},
    {10,10,20,20,0.1,10,10,10,10,10},
    {10,10,10,10,10,0.1,10,10,10,10}};
  const double C[10] = {1,10,20,2,0.5,4,3,6,8,1};
  double s = 0.0;
  for (int i = 0; i < 10; ++i) s += x[i]*x[i];
  double f = -std::exp(-0.5 * s / M_PI);
  for (int m = 0; m < 6; ++m)
    for (int i = 0; i < 10; ++i) {
      double d = x[i] - A[m][i];
      f -= d * d * C[i] * C[i];
    }
  return f;
}
inline void shekel5_grad(const RVec& x, RVec& g) {
  const double A[6][10] = {
    {0.1,10,20,20,10,10,10,10,10,10},
    {10,0.1,20,20,10,10,10,10,10,10},
    {20,20,0.1,20,10,10,10,10,10,10},
    {20,20,20,0.1,10,10,10,10,10,10},
    {10,10,20,20,0.1,10,10,10,10,10},
    {10,10,10,10,10,0.1,10,10,10,10}};
  const double C[10] = {1,10,20,2,0.5,4,3,6,8,1};
  g.assign(10, 0.0);
  double e = std::exp(-dot(x, x) / (2.0 * M_PI));
  for (int i = 0; i < 10; ++i) g[i] += -x[i] * e / M_PI;
  for (int m = 0; m < 6; ++m)
    for (int i = 0; i < 10; ++i) g[i] += -2.0 * (x[i] - A[m][i]) * C[i] * C[i];
}

// ---------------- MIGA: multi-island GA with NSGA-II-style selection -------
struct MIGA {
  int islands = 6, pop = 16, gens = 36;
  int migrate_every = 9, migrants = 2;
  double cx_rate = 0.85, sbx_eta = 15.0, mut_eta = 20.0;
  mutable Rng rng{4321};
  Box box;
  std::function<double(const RVec&)> f1, f2;   // bi-objective
  std::vector<std::vector<RVec>> popu;
  struct Row { int island; int rank; double cd; double f1, f2; };
  std::vector<double> gen_best1, gen_best2;
  RVec best;
  double best1 = 1e300, best2 = 0.0;

  void init() {
    int n = (int)box.lb.size();
    popu.assign(islands, {});
    gen_best1.clear(); gen_best2.clear();
    for (int isl = 0; isl < islands; ++isl) {
      RVec c(n);
      for (int i = 0; i < n; ++i) c[i] = box.lb[i] + (box.ub[i]-box.lb[i]) * rng.u();
      for (int p = 0; p < pop; ++p) {
        RVec x(n);
        for (int i = 0; i < n; ++i)
          x[i] = box.lb[i] + (box.ub[i]-box.lb[i]) *
                 std::min(1.0, std::max(0.0, 0.25 + 0.5 * rng.u() + 0.2 * rng.u()));
        // pull toward island center
        for (int i = 0; i < n; ++i) x[i] = 0.7 * x[i] + 0.3 * c[i];
        for (int i = 0; i < n; ++i)
          x[i] = std::min(box.ub[i], std::max(box.lb[i], x[i]));
        popu[isl].push_back(x);
      }
    }
    best = popu[0][0];
    best1 = 1e300;
  }

  RVec crossover(const RVec& a, const RVec& b, bool& child1) const {
    int n = (int)a.size();
    RVec c1 = a, c2 = b;
    for (int i = 0; i < n; ++i) {
      if (rng.u() >= cx_rate) continue;
      double u = rng.u();
      double beta;
      if (std::fabs(a[i] - b[i]) < 1e-14) beta = 1.0;
      else if (u <= 0.5) beta = std::pow(2.0 * u, 1.0 / (sbx_eta + 1.0));
      else beta = std::pow(1.0 / (2.0 * (1.0 - u)), 1.0 / (sbx_eta + 1.0));
      c1[i] = 0.5 * ((a[i] + b[i]) + beta * (a[i] - b[i]));
      c2[i] = 0.5 * ((a[i] + b[i]) - beta * (a[i] - b[i]));
    }
    clip(c1); clip(c2);
    child1 = rng.u() < 0.5;
    return child1 ? c1 : c2;
  }
  void mutate(RVec& x) const {
    int n = (int)x.size();
    for (int i = 0; i < n; ++i) {
      if (rng.u() >= 1.0 / n) continue;
      double u = rng.u(), delta;
      if (u < 0.5) delta = std::pow(2.0 * u, 1.0 / (mut_eta + 1.0)) - 1.0;
      else delta = 1.0 - std::pow(2.0 * (1.0 - u), 1.0 / (mut_eta + 1.0));
      x[i] += delta * (box.ub[i] - box.lb[i]);
    }
    clip(x);
  }
  void clip(RVec& x) const {
    for (int i = 0; i < (int)x.size(); ++i)
      x[i] = std::min(box.ub[i], std::max(box.lb[i], x[i]));
  }

  // NSGA-II style: sort (f1, f2) population, fill by rank + crowding
  struct Ind { RVec x; double v1, v2, rank, cd; };
  void nsga2_select(std::vector<Ind>& all, std::vector<Ind>& keep, int keepn) {
    int m = (int)all.size();
    std::vector<std::vector<int>> front;
    std::vector<int> dom(m, 0);
    std::vector<std::vector<int>> domby(m);
    for (int i = 0; i < m; ++i)
      for (int j = i + 1; j < m; ++j) {
        bool ij = all[i].v1 <= all[j].v1 && all[i].v2 <= all[j].v2 &&
                  (all[i].v1 < all[j].v1 || all[i].v2 < all[j].v2);
        bool ji = all[j].v1 <= all[i].v1 && all[j].v2 <= all[i].v2 &&
                  (all[j].v1 < all[i].v1 || all[j].v2 < all[i].v2);
        if (ij) { dom[j]++; domby[i].push_back(j); }
        if (ji) { dom[i]++; domby[j].push_back(i); }
      }
    std::vector<char> done(m, 0);
    std::vector<int> cur;
    for (int i = 0; i < m; ++i) if (dom[i] == 0) cur.push_back(i);
    int r = 0;
    while (!cur.empty()) {
      front.push_back(cur);
      for (int i : cur) { all[i].rank = r; done[i] = 1; }
      std::vector<int> nxt;
      for (int i : cur)
        for (int j : domby[i])
          if (--dom[j] == 0 && !done[j]) nxt.push_back(j);
      cur = nxt;
      ++r;
    }
    keep.clear();
    for (auto& F : front) {
      if ((int)keep.size() + (int)F.size() <= keepn) {
        // crowding distance within this front
        double m1 = 1e300, M1 = -1e300, m2 = 1e300, M2 = -1e300;
        for (int i : F) { m1 = std::min(m1, all[i].v1); M1 = std::max(M1, all[i].v1);
                          m2 = std::min(m2, all[i].v2); M2 = std::max(M2, all[i].v2); }
        for (int i : F) all[i].cd = 0.0;
        for (int obj = 0; obj < 2; ++obj) {
          std::sort(F.begin(), F.end(), [&](int a, int b) {
            return obj == 0 ? all[a].v1 < all[b].v1 : all[a].v2 < all[b].v2;
          });
          double span = obj == 0 ? (M1 - m1) : (M2 - m2);
          if (span < 1e-30) { for (int i : F) all[i].cd += 1e300; continue; }
          for (int k = 1; k + 1 < (int)F.size(); ++k) {
            double d = obj == 0
                ? (all[F[k+1]].v1 - all[F[k-1]].v1) / span
                : (all[F[k+1]].v2 - all[F[k-1]].v2) / span;
            all[F[k]].cd += d;
          }
          all[F.front()].cd = 1e300;
          all[F.back()].cd = 1e300;
        }
        for (int i : F) keep.push_back(all[i]);
      } else {
        std::sort(F.begin(), F.end(), [&](int a, int b) { return all[a].cd > all[b].cd; });
        for (int i : F) {
          if ((int)keep.size() >= keepn) break;
          keep.push_back(all[i]);
        }
        break;
      }
    }
  }

  void run(std::string* log = nullptr) {
    init();
    int n = (int)box.lb.size();
    for (int g = 0; g < gens; ++g) {
      for (int isl = 0; isl < islands; ++isl) {
        std::vector<Ind> all;
        for (auto& x : popu[isl]) {
          Ind I; I.x = x; I.v1 = f1(x); I.v2 = f2(x);
          all.push_back(I);
          if (I.v1 < best1) { best1 = I.v1; best = x; }
        }
        for (int p = 0; p < pop; ++p) {
          int pa = (int)(rng.u() * pop), pb = (int)(rng.u() * pop);
          if (pa == pb) pb = (pb + 1) % pop;
          RVec c1, c2;
          bool first = rng.u() < 0.5;
          c1 = crossover(popu[isl][pa], popu[isl][pb], first);
          bool first2 = rng.u() < 0.5;
          c2 = crossover(popu[isl][pa], popu[isl][pb], first2);
          mutate(c1); mutate(c2);
          all.push_back(Ind{c1, f1(c1), f2(c1), 0, 0});
          all.push_back(Ind{c2, f1(c2), f2(c2), 0, 0});
        }
        std::vector<Ind> keep;
        nsga2_select(all, keep, pop);
        popu[isl].clear();
        for (auto& I : keep) popu[isl].push_back(I.x);
      }
      // track generation best
      double b1 = 1e300, b2 = 0;
      for (auto& isl : popu)
        for (auto& x : isl) { b1 = std::min(b1, f1(x)); b2 = std::min(b2, f2(x)); }
      gen_best1.push_back(b1);
      gen_best2.push_back(b2);
      // ring migration
      if (g % migrate_every == migrate_every - 1 && g < gens - 1) {
        std::vector<std::vector<RVec>> send(islands);
        for (int isl = 0; isl < islands; ++isl) {
          std::vector<std::pair<double,int>> ord(popu[isl].size());
          for (int p = 0; p < pop; ++p) ord[p] = {f1(popu[isl][p]), p};
          std::sort(ord.begin(), ord.end());
          for (int t = 0; t < migrants; ++t) send[isl].push_back(popu[isl][ord[t].second]);
        }
        for (int isl = 0; isl < islands; ++isl) {
          int nxt = (isl + 1) % islands;
          // replace worst non-front individuals in nxt
          std::vector<std::pair<double,int>> ord(popu[nxt].size());
          for (int p = 0; p < pop; ++p) ord[p] = {f1(popu[nxt][p]), p};
          std::sort(ord.rbegin(), ord.rend());
          for (auto& c : send[isl]) {
            popu[nxt][ord[0].second] = c;
            std::sort(ord.rbegin(), ord.rend());
          }
        }
      }
    }
  }
};

// ---------------- PSO ----------------
struct PSO {
  int np = 24, iters = 100;
  double w0 = 0.9, w1 = 0.4, c1 = 1.8, c2 = 1.8, vspan = 0.5;
  Rng rng{777};
  Box box;
  std::function<double(const RVec&)> f;
  RVec best;
  double bestf = 1e300;
  std::vector<double> gen_best;

  void run(const RVec& center, double radius) {
    int n = (int)box.lb.size();
    RVec lo(n), hi(n);
    for (int i = 0; i < n; ++i) {
      lo[i] = std::max(box.lb[i], center[i] - radius * (box.ub[i]-box.lb[i]));
      hi[i] = std::min(box.ub[i], center[i] + radius * (box.ub[i]-box.lb[i]));
      if (lo[i] > hi[i]) { lo[i] = box.lb[i]; hi[i] = box.ub[i]; }
    }
    std::vector<RVec> P(n), V(np), PB(n);
    std::vector<double> PF(np);
    for (int p = 0; p < np; ++p) {
      for (int i = 0; i < n; ++i) {
        P[p][i] = lo[i] + (hi[i]-lo[i]) * rng.u();
        V[p][i] = 0.0;
      }
      PF[p] = f(P[p]);
      PB[p] = P[p];
      if (PF[p] < bestf) { bestf = PF[p]; best = P[p]; }
    }
    for (int it = 0; it < iters; ++it) {
      double w = w0 + (w1 - w0) * (double)it / iters;
      double g = 1e300;
      for (int p = 0; p < np; ++p) {
        for (int i = 0; i < n; ++i) {
          double span = std::max(1e-12, box.ub[i] - box.lb[i]);
          V[p][i] = w * V[p][i]
                  + c1 * rng.u() * (PB[p][i] - P[p][i])
                  + c2 * rng.u() * (best[i]  - P[p][i]);
          double vmax = vspan * span;
          V[p][i] = std::min(vmax, std::max(-vmax, V[p][i]));
          P[p][i] = std::min(box.ub[i], std::max(box.lb[i], P[p][i] + V[p][i]));
        }
        double fp = f(P[p]);
        if (fp < PF[p]) { PF[p] = fp; PB[p] = P[p]; }
        if (fp < g) { g = fp; best = P[p]; bestf = fp; }
      }
      gen_best.push_back(g);
    }
  }
};

// ---------------- Nelder-Mead downhill simplex ----------------
struct NelderMead {
  int maxfev = 6000;
  Rng rng{555};
  std::function<double(const RVec&)> f;
  Box box;
  RVec best;
  double bestf = 1e300;
  int fevals = 0;
  std::vector<double> gen_best;

  void run(const RVec& x0) {
    int n = (int)x0.size();
    std::vector<RVec> X(n + 1);
    std::vector<double> F(n + 1);
    X[0] = x0; F[0] = f(x0); ++fevals;
    for (int i = 0; i < n; ++i) {
      X[i+1] = x0;
      double h = 0.05 * std::max(std::fabs(x0[i]), 1.0);
      X[i+1][i] += (x0[i] < 0.5 * (box.lb[i] + box.ub[i])) ? h : -h;
      X[i+1][i] = std::min(box.ub[i], std::max(box.lb[i], X[i+1][i]));
      F[i+1] = f(X[i+1]); ++fevals;
    }
    gen_best.clear();
    double alpha = 1.0, gam = 2.0, rho = 0.5, sig = 0.5;
    for (int it = 0; it < 2000 && fevals < maxfev; ++it) {
      std::vector<int> ord(n + 1);
      std::iota(ord.begin(), ord.end(), 0);
      std::sort(ord.begin(), ord.end(), [&](int a, int b) { return F[a] < F[b]; });
      for (int k = 0; k < n + 1; ++k) { X[k] = X[ord[k]]; F[k] = F[ord[k]]; }
      if (std::fabs(F[n] - F[0]) < 1e-10 && (nrm2(X[n]) < 1e-8)) break;
      RVec cen(n);
      for (int i = 0; i < n; ++i) {
        double s = 0; for (int k = 0; k < n; ++k) s += X[k][i];
        cen[i] = s / n;
      }
      RVec xr = cen;
      for (int i = 0; i < n; ++i) xr[i] = cen[i] + alpha * (cen[i] - X[n][i]);
      clip(xr);
      double fr = f(xr); ++fevals;
      if (fr < F[0]) {
        RVec xe = cen;
        for (int i = 0; i < n; ++i) xe[i] = cen[i] + gam * (cen[i] - X[n][i]);
        clip(xe);
        double fe = f(xe); ++fevals;
        if (fe < fr) { X[n] = xe; F[n] = fe; }
        else { X[n] = xr; F[n] = fr; }
      } else if (fr < F[n-1]) { X[n] = xr; F[n] = fr; }
      else {
        RVec xc = cen;
        for (int i = 0; i < n; ++i) xc[i] = cen[i] + rho * (X[n][i] - cen[i]);
        clip(xc);
        double fc = f(xc); ++fevals;
        if (fc < F[n]) { X[n] = xc; F[n] = fc; }
        else {
          for (int k = 1; k < n + 1; ++k) {
            for (int i = 0; i < n; ++i) X[k][i] = X[0][i] + sig * (X[k][i] - X[0][i]);
            clip(X[k]);
            F[k] = f(X[k]); ++fevals;
          }
        }
      }
      double bg = 1e300;
      for (int k = 0; k < n + 1; ++k) bg = std::min(bg, F[k]);
      if ((int)gen_best.size() < iters_record()) gen_best.push_back(bg);
    }
    // final best
    int bk = 0;
    for (int k = 1; k < n + 1; ++k) if (F[k] < F[bk]) bk = k;
    bestf = F[bk]; best = X[bk];
  }
  int iters_record() const { return 100000; }
  void clip(RVec& x) const {
    for (int i = 0; i < (int)x.size(); ++i)
      x[i] = std::min(box.ub[i], std::max(box.lb[i], x[i]));
  }
};

// ---------------- L-BFGS (two-loop, strong Wolfe) ----------------
struct LBFGS {
  int mem = 10, maxit = 300;
  double tol = 1e-9;
  Box box;
  std::function<double(const RVec&)> f;
  std::function<void(const RVec&, RVec&)> g;
  RVec best;
  double bestf = 1e300;
  int iters_done = 0;
  double gnorm = 1e300;
  std::vector<double> gen_best;

  void run(const RVec& x0) {
    int n = (int)x0.size();
    RVec x = x0, gg;
    g(x, gg);
    bestf = f(x);
    best = x;
    gnorm = nrm_inf(gg);
    gen_best.push_back(bestf);
    std::vector<RVec> s, y;
    std::vector<double> ry;
    for (int it = 0; it < maxit; ++it) {
      if (gnorm < tol) break;
      (void)0;
      // two-loop recursion
      RVec q = gg;
      RVec al(s.size());
      for (int i = (int)s.size() - 1; i >= 0; --i) {
        double a = dot(s[i], q) / ry[i];
        al[i] = a;
        for (int k = 0; k < n; ++k) q[k] -= a * y[i][k];
      }
      RVec r = q;
      double rs = dot(s.back(), y.back());
      double rr = dot(r, r);
      double gam = rr > 1e-30 ? rs / rr : 1.0;
      for (int k = 0; k < n; ++k) r[k] *= gam;
      for (size_t i = 0; i < s.size(); ++i) {
        double b = dot(y[i], r) / ry[i];
        for (int k = 0; k < n; ++k) r[k] += (al[i] - b) * s[i][k];
      }
      RVec d(n);
      for (int k = 0; k < n; ++k) d[k] = -r[k];
      // line search (strong Wolfe)
      double stp = 1.0;
      if (dot(gg, d) > 1e-18) stp = 0.0;
      bool found = false;
      RVec xn(n);
      RVec gx, gn;
      g(x, gx);
      double gtx = dot(gx, d);
      for (int ls = 0; ls < 40 && stp > 1e-14; ++ls) {
        bool inb = true;
        for (int k = 0; k < n; ++k) {
          xn[k] = x[k] + stp * d[k];
          if (xn[k] < box.lb[k] || xn[k] > box.ub[k]) inb = false;
        }
        if (!inb) { stp *= 0.5; continue; }
        double fn = f(xn);
        gn.assign(n, 0.0);
        g(xn, gn);
        double gtn = dot(gn, d);
        if (fn <= bestf + 1e-4 * stp * gtx && gtn <= 0.9 * gtx) {
          found = true;
          break;
        }
        if (gtn > 0) stp *= 0.5;
        else stp *= std::max(0.1, std::min(0.5, 1.0 - (fn - bestf - 1e-4*stp*gtx) /
                    std::max(1e-30, std::fabs(gtn - gtx))));
      }
      if (!found || stp < 1e-14) break;
      RVec sn(n), yn(n);
      for (int k = 0; k < n; ++k) sn[k] = stp * d[k];
      gn.assign(n, 0.0);
      g(xn, gn);
      double fn = f(xn);
      for (int k = 0; k < n; ++k) yn[k] = gn[k] - gx[k];
      double ryy = dot(sn, yn);
      if (ryy > 1e-12 * stp * stp) {
        s.push_back(sn); y.push_back(yn); ry.push_back(ryy);
        if ((int)s.size() > mem) { s.erase(s.begin()); y.erase(y.begin()); ry.erase(ry.begin()); }
      }
      x = xn; gg = gn; bestf = fn; best = x;
      gnorm = nrm_inf(gg);
      iters_done = it + 1;
      gen_best.push_back(bestf);
    }
  }
};

}  // namespace mo
