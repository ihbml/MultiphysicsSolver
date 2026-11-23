// SPDX-License-Identifier: MIT
// Method of Moving Asymptotes (Svanberg, 1987/2002) for smooth NLP:
//
//   min f0(x)   s.t.  f_i(x) <= 0,  xmin <= x <= xmax
//
// Convex separable approximation at the iterate x^k:
//   fhat_i(x) = c_i + sum_j [ p_ij/(U_j - x_j) + q_ij/(x_j - L_j) ]
// with p_ij = (U_j-x_j^k)^2 max(df_i/dx_j, 0), q_ij = (x_j^k-L_j)^2 max(-df_i/dx_j, 0)
// and adaptive, oscillating-sign driven asymptotes L_j^k, U_j^k.
//
// Two subproblem solvers are provided:
//   * SolveDualNewton     -- exact dual Newton with closed form inner minimiser,
//                            O(m^3 + m n) per iteration (default, scalable).
//   * SolveDIP            -- primal-dual interior-point Newton on the full KKT
//                            system (used as an independent cross-check).
#pragma once

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <limits>
#include <stdexcept>
#include <vector>

#include "mdo/linalg.hpp"

namespace mdo {

struct MmaOptions {
  int maxOuter = 300;
  double tol = 1e-7;
  double asyminit = 0.5;
  double asymdec = 0.7;
  double asyminc = 1.2;
  double moveLimit = 0.35;
  double raaScale = 1e-7;
  int dualMaxIters = 200;
  double dualTol = 1e-12;
  bool useInteriorPoint = false;
};

struct MmaProblem {
  double f0 = 0.0;
  Vec df0;
  std::vector<double> f;
  std::vector<Vec> df;
};

struct SubproblemData {
  Vec low, upp;      // asymptote bounds (strictly outside xmin/xmax)
  Vec x;             // current design
  Vec p0, q0, c0;    // objective approximation
  std::vector<Vec> p, q;
  std::vector<double> c;
};

class MmaSolver {
 public:
  MmaSolver(int n, std::vector<double> xmin, std::vector<double> xmax, MmaOptions opts = {})
      : n_(n), xmin_(std::move(xmin)), xmax_(std::move(xmax)), opt_(opts) {
    if (static_cast<int>(xmin_.size()) != n_ || static_cast<int>(xmax_.size()) != n_)
      throw std::runtime_error("MmaSolver: bound size mismatch");
    // Initial asymptotes (Svanberg: low = x - asyminit*range, upp = x + asyminit*range).
    low_.resize(static_cast<std::size_t>(n_));
    upp_.resize(static_cast<std::size_t>(n_));
    for (int j = 0; j < n_; ++j) {
      const double range = xmax_[static_cast<std::size_t>(j)] - xmin_[static_cast<std::size_t>(j)];
      low_[static_cast<std::size_t>(j)] = xmin_[static_cast<std::size_t>(j)];
      upp_[static_cast<std::size_t>(j)] = xmax_[static_cast<std::size_t>(j)];
      (void)range;
    }
    initialized_ = false;
  }

  void initialize(const Vec& x) {
    xold1_ = x;
    xold2_ = x;
    for (int j = 0; j < n_; ++j) {
      const double range = xmax_[static_cast<std::size_t>(j)] - xmin_[static_cast<std::size_t>(j)];
      low_[static_cast<std::size_t>(j)] = x[static_cast<std::size_t>(j)] - opt_.asyminit * range;
      upp_[static_cast<std::size_t>(j)] = x[static_cast<std::size_t>(j)] + opt_.asyminit * range;
    }
    initialized_ = true;
    firstSkip_ = true;
  }

  // Adaptive asymptote update using the sign of the design oscillation.
  void updateAsymptotes(const Vec& x) {
    if (!initialized_) {
      initialize(x);
      return;
    }
    if (firstSkip_) {
      // The initial asymptotes already bracket the starting design.
      firstSkip_ = false;
      return;
    }
    for (int j = 0; j < n_; ++j) {
      const std::size_t jj = static_cast<std::size_t>(j);
      const double prod = (x[jj] - xold1_[jj]) * (xold1_[jj] - xold2_[jj]);
      const double scaleLow = (xold1_[jj] - low_[jj]);
      const double scaleUpp = (upp_[jj] - xold1_[jj]);
      if (prod < 0.0) {
        low_[jj] = x[jj] - opt_.asymdec * std::abs(scaleLow);
        upp_[jj] = x[jj] + opt_.asymdec * std::abs(scaleUpp);
      } else if (prod > 0.0) {
        low_[jj] = x[jj] - opt_.asyminc * std::abs(scaleLow);
        upp_[jj] = x[jj] + opt_.asyminc * std::abs(scaleUpp);
      } else {
        low_[jj] = x[jj] - opt_.asyminit * std::abs(scaleLow);
        upp_[jj] = x[jj] + opt_.asyminit * std::abs(scaleUpp);
      }
      const double range = xmax_[jj] - xmin_[jj];
      const double loGuard = xmin_[jj] - 0.25 * range;
      const double upGuard = xmax_[jj] + 0.25 * range;
      if (!(low_[jj] < x[jj])) low_[jj] = x[jj] - 0.5 * range;
      if (!(upp_[jj] > x[jj])) upp_[jj] = x[jj] + 0.5 * range;
      low_[jj] = std::max(low_[jj], loGuard);
      upp_[jj] = std::min(upp_[jj], upGuard);
    }
    xold2_ = xold1_;
    xold1_ = x;
  }

  // Solve one MMA subproblem. Returns the next design iterate as a clipped step.
  Vec solveSubproblem(const Vec& x, const MmaProblem& prob) {
    if (static_cast<int>(prob.df0.size()) != n_) throw std::runtime_error("MMA gradient size mismatch");
    updateAsymptotes(x);
    SubproblemData sp = buildSubproblem(x, prob);
    Vec xnew = opt_.useInteriorPoint ? solveDIP(sp) : solveDualNewton(sp);
    // Move limit + physical bounds.
    for (int j = 0; j < n_; ++j) {
      const std::size_t jj = static_cast<std::size_t>(j);
      const double range = xmax_[jj] - xmin_[jj];
      const double lo = std::max(xmin_[jj], x[jj] - opt_.moveLimit * range);
      const double hi = std::min(xmax_[jj], x[jj] + opt_.moveLimit * range);
      xnew[jj] = std::min(hi, std::max(lo, xnew[jj]));
    }
    return xnew;
  }

  mutable Vec debugY_;
  mutable Vec debugX_;
  [[nodiscard]] const Vec& lowerAsymptotes() const { return low_; }
  [[nodiscard]] const Vec& upperAsymptotes() const { return upp_; }
  [[nodiscard]] int n() const { return n_; }

  // Primal-dual interior point Newton on the MMA subproblem KKT system:
  //   grad fhat0(x) + Jg^T y + barrier(bounds) = 0
  //   g(x) + s = 0 ,  s_i y_i = mu ,  s > 0 , y > 0
  // The Newton system is condensed onto the primal block with a Schur complement.
  Vec solveDIP(const SubproblemData& sp) const {
    const int n = n_;
    const int m = static_cast<int>(sp.p.size());
    Vec x = sp.x;
    std::vector<double> lo(static_cast<std::size_t>(n)), hi(static_cast<std::size_t>(n));
    for (int j = 0; j < n; ++j) {
      const std::size_t jj = static_cast<std::size_t>(j);
      lo[jj] = std::max(xmin_[jj], sp.low[jj]) + 1e-8;
      hi[jj] = std::min(xmax_[jj], sp.upp[jj]) - 1e-8;
      x[jj] = std::min(hi[jj], std::max(lo[jj], x[jj]));
    }
    std::vector<double> s(static_cast<std::size_t>(m), 1.0), y(static_cast<std::size_t>(m), 1.0);
    double mu = 1.0;
    for (int outer = 0; outer < 80; ++outer) {
      for (int inner = 0; inner < 60; ++inner) {
        Vec rDual(static_cast<std::size_t>(n), 0.0), rPrim(static_cast<std::size_t>(m), 0.0), rCent(static_cast<std::size_t>(m), 0.0);
        Mat Jg = zeroMatrix(m, n);
        Mat Hx = zeroMatrix(n);
        for (int j = 0; j < n; ++j) {
          const std::size_t jj = static_cast<std::size_t>(j);
          double g = derivativeObjective(sp, x, j);
          double h = 2.0 * sp.p0[jj] / std::pow(std::max(sp.upp[jj] - x[jj], 1e-12), 3) +
                     2.0 * sp.q0[jj] / std::pow(std::max(x[jj] - sp.low[jj], 1e-12), 3);
          for (int i = 0; i < m; ++i) {
            const std::size_t ii = static_cast<std::size_t>(i);
            Jg[ii][jj] = derivativeConstraint(sp, x, i, j);
            g += y[ii] * Jg[ii][jj];
            h += y[ii] * (2.0 * sp.p[ii][jj] / std::pow(std::max(sp.upp[jj] - x[jj], 1e-12), 3) +
                          2.0 * sp.q[ii][jj] / std::pow(std::max(x[jj] - sp.low[jj], 1e-12), 3));
          }
          g += mu / std::max(x[jj] - lo[jj], 1e-12) - mu / std::max(hi[jj] - x[jj], 1e-12);
          h += mu / std::pow(std::max(x[jj] - lo[jj], 1e-12), 2) + mu / std::pow(std::max(hi[jj] - x[jj], 1e-12), 2);
          rDual[jj] = g;
          Hx[jj][jj] = std::max(h, 1e-12);
        }
        for (int i = 0; i < m; ++i) {
          const std::size_t ii = static_cast<std::size_t>(i);
          rPrim[ii] = violation(sp, x, i) + s[ii];
          rCent[ii] = s[ii] * y[ii] - mu;
        }
        // W = Y^{-1} S ; condensed primal system (Hx + Jg^T W^{-1} Jg) dx = rhs.
        Mat A = Hx;
        Vec rhs = Vec(static_cast<std::size_t>(n), 0.0);
        for (int j = 0; j < n; ++j) rhs[static_cast<std::size_t>(j)] = -rDual[static_cast<std::size_t>(j)];
        for (int i = 0; i < m; ++i) {
          const std::size_t ii = static_cast<std::size_t>(i);
          const double wInv = std::max(y[ii], 1e-12) / std::max(s[ii], 1e-12);
          const double beta = rPrim[ii] - rCent[ii] / std::max(y[ii], 1e-12);
          for (int j = 0; j < n; ++j) {
            rhs[static_cast<std::size_t>(j)] -= Jg[ii][static_cast<std::size_t>(j)] * wInv * beta;
            for (int k = 0; k < n; ++k)
              A[static_cast<std::size_t>(j)][static_cast<std::size_t>(k)] +=
                  Jg[ii][static_cast<std::size_t>(j)] * wInv * Jg[ii][static_cast<std::size_t>(k)];
          }
        }
        Vec flat(static_cast<std::size_t>(n) * static_cast<std::size_t>(n));
        for (int j = 0; j < n; ++j)
          for (int k = 0; k < n; ++k) flat[static_cast<std::size_t>(j) * static_cast<std::size_t>(n) + static_cast<std::size_t>(k)] = A[static_cast<std::size_t>(j)][static_cast<std::size_t>(k)];
        Vec dx;
        try {
          dx = denseSolve<double>(std::move(flat), rhs, n);
        } catch (...) {
          break;
        }
        Vec dy(static_cast<std::size_t>(m), 0.0), ds(static_cast<std::size_t>(m), 0.0);
        for (int i = 0; i < m; ++i) {
          const std::size_t ii = static_cast<std::size_t>(i);
          double acc = rPrim[ii] - rCent[ii] / std::max(y[ii], 1e-12);
          for (int j = 0; j < n; ++j) acc += Jg[ii][static_cast<std::size_t>(j)] * dx[static_cast<std::size_t>(j)];
          dy[ii] = acc * std::max(y[ii], 1e-12) / std::max(s[ii], 1e-12);
          ds[ii] = (-rCent[ii] - s[ii] * dy[ii]) / std::max(y[ii], 1e-12);
        }
        double alpha = 1.0;
        for (int i = 0; i < m; ++i) {
          const std::size_t ii = static_cast<std::size_t>(i);
          if (ds[ii] < 0.0) alpha = std::min(alpha, -0.95 * s[ii] / ds[ii]);
          if (dy[ii] < 0.0) alpha = std::min(alpha, -0.95 * y[ii] / dy[ii]);
        }
        double maxStep = 0.0;
        for (int j = 0; j < n; ++j) maxStep = std::max(maxStep, std::abs(dx[static_cast<std::size_t>(j)]));
        if (maxStep > 0.0) alpha = std::min(alpha, 1.0);
        if (alpha <= 0.0) break;
        for (int j = 0; j < n; ++j) {
          const std::size_t jj = static_cast<std::size_t>(j);
          x[jj] += alpha * dx[jj];
          x[jj] = std::min(hi[jj] - 1e-11, std::max(lo[jj] + 1e-11, x[jj]));
        }
        for (int i = 0; i < m; ++i) {
          const std::size_t ii = static_cast<std::size_t>(i);
          s[ii] = std::max(1e-13, s[ii] + alpha * ds[ii]);
          y[ii] = std::max(1e-13, y[ii] + alpha * dy[ii]);
        }
        if (maxStep < 1e-11) break;
      }
      mu *= 0.25;
      if (mu < 1e-12) break;
    }
    return x;
  }

  // Dual Newton: the inner minimisation is closed form because the approximation
  // is separable, so the dual dimension is just the number of constraints.
  Vec solveDualNewton(const SubproblemData& sp) const {
    const int n = n_;
    const int m = static_cast<int>(sp.p.size());
    Vec y(static_cast<std::size_t>(m), 0.0);
    Vec xBest = sp.x;
    auto evalDual = [&](const Vec& yy, Vec& xOut, Vec* gradOut) -> double {
      xOut.assign(static_cast<std::size_t>(n), 0.0);
      for (int j = 0; j < n; ++j) {
        const std::size_t jj = static_cast<std::size_t>(j);
        const double L = sp.low[jj];
        const double U = sp.upp[jj];
        double P = sp.p0[jj];
        double Q = sp.q0[jj];
        for (int i = 0; i < m; ++i) {
          P += yy[static_cast<std::size_t>(i)] * sp.p[static_cast<std::size_t>(i)][jj];
          Q += yy[static_cast<std::size_t>(i)] * sp.q[static_cast<std::size_t>(i)][jj];
        }
        P = std::max(P, 0.0);
        Q = std::max(Q, 0.0);
        const double t = std::sqrt(P);
        const double s = std::sqrt(Q);
        double xj;
        if (t + s < 1e-300) {
          xj = sp.x[jj];
        } else {
          xj = (s * U + t * L) / (t + s);
        }
        xj = std::min(std::max(xj, xmin_[jj]), xmax_[jj]);
        xj = std::min(std::max(xj, L + 1e-10), U - 1e-10);
        xOut[jj] = xj;
      }
      double D = sp.c0[0];
      for (int j = 0; j < n; ++j) {
        const std::size_t jj = static_cast<std::size_t>(j);
        D += sp.p0[jj] / (sp.upp[jj] - xOut[jj]) + sp.q0[jj] / (xOut[jj] - sp.low[jj]);
      }
      (void)D;
      if (gradOut) {
        gradOut->assign(static_cast<std::size_t>(m), 0.0);
        for (int i = 0; i < m; ++i) (*gradOut)[static_cast<std::size_t>(i)] = violation(sp, xOut, i);
      }
      // Dual objective value (constant terms omitted, only used for line search).
      double val = 0.0;
      for (int j = 0; j < n; ++j) {
        const std::size_t jj = static_cast<std::size_t>(j);
        double P = sp.p0[jj];
        double Q = sp.q0[jj];
        for (int i = 0; i < m; ++i) {
          P += yy[static_cast<std::size_t>(i)] * sp.p[static_cast<std::size_t>(i)][jj];
          Q += yy[static_cast<std::size_t>(i)] * sp.q[static_cast<std::size_t>(i)][jj];
        }
        val += std::max(P, 0.0) / (sp.upp[jj] - xOut[jj]) + std::max(Q, 0.0) / (xOut[jj] - sp.low[jj]);
      }
      for (int i = 0; i < m; ++i) val += yy[static_cast<std::size_t>(i)] * sp.c[static_cast<std::size_t>(i)];
      return val;
    };

    Vec xCur;
    double best = evalDual(y, xCur, nullptr);
    xBest = xCur;
    for (int it = 0; it < opt_.dualMaxIters; ++it) {
      Vec g;
      const double cur = evalDual(y, xCur, &g);
      if (m == 0) break;
      double gmax = 0.0;
      for (double v : g) gmax = std::max(gmax, std::abs(v));
      if (gmax < opt_.dualTol) break;
      // Hessian of the (negative) dual: B_ik = sum_j Jg_ij Jg_kj / h_j.
      Mat B = zeroMatrix(m);
      for (int i = 0; i < m; ++i) {
        for (int k = i; k < m; ++k) {
          double acc = 0.0;
          for (int j = 0; j < n; ++j) {
            const std::size_t jj = static_cast<std::size_t>(j);
            const double h = 2.0 * (sp.p0[jj] + dotMultipliers(sp, y, j, false)) /
                                 std::pow(std::max(sp.upp[jj] - xCur[jj], 1e-12), 3) +
                             2.0 * (sp.q0[jj] + dotMultipliers(sp, y, j, true)) /
                                 std::pow(std::max(xCur[jj] - sp.low[jj], 1e-12), 3);
            const double Jg_i = derivativeConstraint(sp, xCur, i, j);
            const double Jg_k = derivativeConstraint(sp, xCur, k, j);
            acc += Jg_i * Jg_k / std::max(h, 1e-12);
          }
          B[static_cast<std::size_t>(i)][static_cast<std::size_t>(k)] = acc;
          B[static_cast<std::size_t>(k)][static_cast<std::size_t>(i)] = acc;
        }
      }
      for (int i = 0; i < m; ++i) B[static_cast<std::size_t>(i)][static_cast<std::size_t>(i)] += 1e-10;
      Vec step;
      try {
        step = denseCholeskySolve(B, g);
      } catch (...) {
        break;
      }
      double alpha = 1.0;
      double accepted = best;
      Vec yTry = y;
      Vec xTry;
      for (int ls = 0; ls < 40; ++ls) {
        yTry = y;
        for (int i = 0; i < m; ++i)
          yTry[static_cast<std::size_t>(i)] = std::max(0.0, y[static_cast<std::size_t>(i)] + alpha * step[static_cast<std::size_t>(i)]);
        const double tryVal = evalDual(yTry, xTry, nullptr);
        if (tryVal > cur + 1e-14) {
          accepted = tryVal;
          break;
        }
        alpha *= 0.5;
      }
      if (accepted <= cur + 1e-14) break;
      y = yTry;
      xBest = xTry;
      best = accepted;
      bool atZero = false;
      for (int i = 0; i < m; ++i)
        if (y[static_cast<std::size_t>(i)] <= 1e-12 && g[static_cast<std::size_t>(i)] < 0.0) atZero = true;
      if (atZero) break;
    }
    debugY_ = y;
    debugX_ = xBest;
    return xBest;
  }

  [[nodiscard]] double violation(const SubproblemData& sp, const Vec& x, int i) const {
    const std::size_t ii = static_cast<std::size_t>(i);
    double acc = sp.c[ii];
    for (int j = 0; j < n_; ++j) {
      const std::size_t jj = static_cast<std::size_t>(j);
      acc += sp.p[ii][jj] / std::max(sp.upp[jj] - x[jj], 1e-15) + sp.q[ii][jj] / std::max(x[jj] - sp.low[jj], 1e-15);
    }
    return acc;
  }

  [[nodiscard]] double dotMultipliers(const SubproblemData& sp, const Vec& y, int j, bool upper) const {
    double acc = 0.0;
    for (std::size_t i = 0; i < sp.p.size(); ++i)
      acc += y[i] * (upper ? sp.q[i][static_cast<std::size_t>(j)] : sp.p[i][static_cast<std::size_t>(j)]);
    return acc;
  }

  [[nodiscard]] double derivativeObjective(const SubproblemData& sp, const Vec& x, int j) const {
    const std::size_t jj = static_cast<std::size_t>(j);
    return sp.p0[jj] / std::pow(std::max(sp.upp[jj] - x[jj], 1e-12), 2) -
           sp.q0[jj] / std::pow(std::max(x[jj] - sp.low[jj], 1e-12), 2);
  }

  [[nodiscard]] double derivativeConstraint(const SubproblemData& sp, const Vec& x, int i, int j) const {
    const std::size_t ii = static_cast<std::size_t>(i);
    const std::size_t jj = static_cast<std::size_t>(j);
    return sp.p[ii][jj] / std::pow(std::max(sp.upp[jj] - x[jj], 1e-12), 2) -
           sp.q[ii][jj] / std::pow(std::max(x[jj] - sp.low[jj], 1e-12), 2);
  }

  SubproblemData buildSubproblem(const Vec& x, const MmaProblem& prob) const {
    SubproblemData sp;
    sp.x = x;
    sp.low.resize(static_cast<std::size_t>(n_));
    sp.upp.resize(static_cast<std::size_t>(n_));
    for (int j = 0; j < n_; ++j) {
      const std::size_t jj = static_cast<std::size_t>(j);
      const double range = xmax_[jj] - xmin_[jj];
      double L = std::max(low_[jj], xmin_[jj] - 0.5 * range);
      double U = std::min(upp_[jj], xmax_[jj] + 0.5 * range);
      if (!(U > x[jj] + 1e-7)) U = x[jj] + 1e-3 * range + 1e-7;
      if (!(L < x[jj] - 1e-7)) L = x[jj] - 1e-3 * range - 1e-7;
      sp.low[jj] = L;
      sp.upp[jj] = U;
    }
    const int m = static_cast<int>(prob.f.size());
    sp.p0.resize(static_cast<std::size_t>(n_));
    sp.q0.resize(static_cast<std::size_t>(n_));
    sp.c0.assign(1, 0.0);
    sp.p.resize(static_cast<std::size_t>(m));
    sp.q.resize(static_cast<std::size_t>(m));
    sp.c.assign(static_cast<std::size_t>(m), 0.0);
    const double eps0 = opt_.raaScale * std::max(1.0, std::abs(prob.f0)) / static_cast<double>(n_);
    for (int j = 0; j < n_; ++j) {
      const std::size_t jj = static_cast<std::size_t>(j);
      const double ux = std::max(sp.upp[jj] - x[jj], 1e-8);
      const double xl = std::max(x[jj] - sp.low[jj], 1e-8);
      const double g = prob.df0[jj];
      sp.p0[jj] = ux * ux * std::max(g, 0.0) + eps0;
      sp.q0[jj] = xl * xl * std::max(-g, 0.0) + eps0;
    }
    for (int i = 0; i < m; ++i) {
      const std::size_t ii = static_cast<std::size_t>(i);
      sp.p[ii].resize(static_cast<std::size_t>(n_));
      sp.q[ii].resize(static_cast<std::size_t>(n_));
      const double epsi = opt_.raaScale * std::max(1.0, std::abs(prob.f[ii])) / static_cast<double>(n_);
      double lhs = 0.0;
      for (int j = 0; j < n_; ++j) {
        const std::size_t jj = static_cast<std::size_t>(j);
        const double ux = std::max(sp.upp[jj] - x[jj], 1e-8);
        const double xl = std::max(x[jj] - sp.low[jj], 1e-8);
        const double g = prob.df[ii][jj];
        sp.p[ii][jj] = ux * ux * std::max(g, 0.0) + epsi;
        sp.q[ii][jj] = xl * xl * std::max(-g, 0.0) + epsi;
        lhs += sp.p[ii][jj] / std::max(sp.upp[jj] - x[jj], 1e-12) + sp.q[ii][jj] / std::max(x[jj] - sp.low[jj], 1e-12);
      }
      // g_i(x) = f_i(x^k) + sum_j [p/(U-x) + q/(x-L)] - sum_j [p/(U-x^k) + q/(x^k-L)]
      sp.c[ii] = prob.f[ii] - lhs;
    }
    return sp;
  }

  static Mat zeroMatrix(int rows, int cols = -1) {
    if (cols < 0) cols = rows;
    return Mat(static_cast<std::size_t>(rows), Vec(static_cast<std::size_t>(cols), 0.0));
  }

 private:
  int n_;
  std::vector<double> xmin_, xmax_;
  MmaOptions opt_;
  Vec low_, upp_, xold1_, xold2_;
  bool initialized_ = false;
  bool firstSkip_ = false;
};

// Complete MMA driver (outer loop) for callers that already have analytic
// objective/constraint/gradient callbacks.
struct MmaRunResult {
  Vec x;
  double f0 = 0.0;
  std::vector<double> f;
  int iterations = 0;
  bool converged = false;
  std::vector<double> history;
  double constraintViolation = 0.0;
};

template <class Fn>
MmaRunResult runMma(int n, Vec x, const std::vector<double>& xmin, const std::vector<double>& xmax, Fn&& evaluate,
                    MmaOptions opts = {}) {
  MmaSolver mma(n, xmin, xmax, opts);
  mma.initialize(x);
  MmaRunResult res;
  MmaProblem prob;
  double prev = std::numeric_limits<double>::infinity();
  for (int k = 0; k < opts.maxOuter; ++k) {
    evaluate(x, prob);
    res.f0 = prob.f0;
    res.f = prob.f;
    res.history.push_back(prob.f0);
    const Vec xnew = mma.solveSubproblem(x, prob);
    double change = 0.0;
    for (int j = 0; j < n; ++j) change = std::max(change, std::abs(xnew[static_cast<std::size_t>(j)] - x[static_cast<std::size_t>(j)]));
    double viol = 0.0;
    for (double v : prob.f) viol = std::max(viol, v);
    res.iterations = k + 1;
    res.constraintViolation = viol;
    bool signChange = false;
    for (int j = 0; j < n; ++j)
      if ((xnew[static_cast<std::size_t>(j)] - x[static_cast<std::size_t>(j)]) * (x[static_cast<std::size_t>(j)] - prev) < 0.0) signChange = true;
    (void)signChange;
    x = xnew;
    if (change < opts.tol && k > 3) {
      res.converged = true;
      break;
    }
    prev = prob.f0;
  }
  evaluate(x, prob);
  res.x = x;
  res.f0 = prob.f0;
  res.f = prob.f;
  double viol = 0.0;
  for (double v : res.f) viol = std::max(viol, v);
  res.constraintViolation = viol;
  res.history.push_back(prob.f0);
  return res;
}

}  // namespace mdo
