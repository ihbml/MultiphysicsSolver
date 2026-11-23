#pragma once
// ============================================================================
//  linalg/krylov.hpp
//  CSR algebra + FP64 outer Krylov solvers (PCG / BiCGStab / FGMRES with
//  flexible preconditioning) + iterative refinement + a-posteriori
//  stagnation & rounding-error detector  eta_round.
//  C++20, header-only, deterministic (fixed reduction order).
// ============================================================================
#include <cstdint>
#include <cmath>
#include <vector>
#include <functional>
#include <cstdio>
#include <algorithm>

namespace linalg {

struct CSR {
  int n = 0;
  std::vector<int32_t> ptr, col;
  std::vector<double> val;
  CSR() = default;
  explicit CSR(int n_) : n(n_), ptr(n_ + 1, 0) {}
  // build from row-wise (col,val) lists; merges duplicates, sorts columns
  static CSR from_rows(int n, const std::vector<std::vector<std::pair<int, double>>>& rows) {
    CSR A(n);
    for (int i = 0; i < n; ++i) {
      auto r = rows[i];
      std::sort(r.begin(), r.end());
      for (auto& pr : r) {
        if (!A.col.empty() && A.ptr[i + 1] == (int)A.col.size() && A.col.back() == pr.first)
          A.val.back() += pr.second;  // duplicate column within this row: merge
        else { A.col.push_back(pr.first); A.val.push_back(pr.second); }
      }
      A.ptr[i + 1] = (int32_t)A.col.size();
    }
    return A;
  }
  void matvec(const double* x, double* y) const {
    for (int i = 0; i < n; ++i) {
      double acc = 0.0;
      for (int k = ptr[i]; k < ptr[i + 1]; ++k) acc += val[k] * x[col[k]];
      y[i] = acc;
    }
  }
  double frobenius() const {
    double s = 0;
    for (double v : val) s += v * v;
    return std::sqrt(s);
  }
};

inline double dot(const std::vector<double>& a, const std::vector<double>& b) {
  double s = 0;
  for (size_t i = 0; i < a.size(); ++i) s += a[i] * b[i];
  return s;
}
inline double norm2(const std::vector<double>& a) { return std::sqrt(dot(a, a)); }
inline void axpy(double a, const std::vector<double>& x, std::vector<double>& y) {
  for (size_t i = 0; i < y.size(); ++i) y[i] += a * x[i];
}

// dense LU with partial pivoting (coarse AMG level / small systems)
inline bool dense_solve(std::vector<std::vector<double>> A, std::vector<double> b,
                        std::vector<double>& x) {
  const int n = (int)b.size();
  x.assign(n, 0);
  for (int k = 0; k < n; ++k) {
    int piv = k;
    for (int i = k + 1; i < n; ++i) if (std::fabs(A[i][k]) > std::fabs(A[piv][k])) piv = i;
    if (std::fabs(A[piv][k]) < 1e-300) return false;
    std::swap(A[piv], A[k]); std::swap(b[piv], b[k]);
    for (int i = k + 1; i < n; ++i) {
      double m = A[i][k] / A[k][k];
      for (int j = k; j < n; ++j) A[i][j] -= m * A[k][j];
      b[i] -= m * b[k];
    }
  }
  for (int i = n - 1; i >= 0; --i) {
    double s = b[i];
    for (int j = i + 1; j < n; ++j) s -= A[i][j] * x[j];
    x[i] = s / A[i][i];
  }
  return true;
}

// ------------------------------------------- a-posteriori rounding floor ---
// eta_round estimates the residual level below which further reduction is
// impossible because it is dominated by FP64 rounding:
//   eta_round = C * eps * ( ||A||_F ||x|| + ||b|| )
// with C a safety constant. If ||r|| <= eta_round the iteration has
// stagnated at the rounding limit (a-posteriori stagnation detector).
inline double eta_round_estimate(const CSR& A, const std::vector<double>& b,
                                 const std::vector<double>& x, double safety = 8.0) {
  const double eps = 2.220446049250313e-16;
  return safety * eps * (A.frobenius() * norm2(x) + norm2(b));
}

struct KrylovLog {
  std::vector<double> relres;  // per-iteration relative residual history
  int iters = 0;
  bool converged = false;
  bool rounding_stagnation = false;
  double eta_round = 0.0;
};

// ---------------------------------------------------------------- PCG ------
// FP64 preconditioned conjugate gradient (SPD). Preconditioner M may run in
// FP32 internally (mixed precision): it is just a linear operator here.
inline KrylovLog pcg(const CSR& A, const std::vector<double>& b, std::vector<double>& x,
                     const std::function<void(const std::vector<double>&, std::vector<double>&)>& M,
                     double tol, int maxit, bool log = true) {
  const int n = A.n;
  std::vector<double> r(n), z(n), p(n), Ap(n);
  A.matvec(x.data(), r.data());
  for (int i = 0; i < n; ++i) r[i] = b[i] - r[i];
  double bnorm = std::max(norm2(b), 1e-300);
  KrylovLog lg;
  M(r, z);
  p = z;
  double rz = dot(r, z);
  for (int it = 0; it < maxit; ++it) {
    double rn = norm2(r);
    lg.relres.push_back(rn / bnorm);
    lg.eta_round = eta_round_estimate(A, b, x);
    if (rn <= lg.eta_round) { lg.rounding_stagnation = true; lg.iters = it; lg.converged = true; break; }
    if (rn / bnorm < tol) { lg.iters = it; lg.converged = true; break; }
    A.matvec(p.data(), Ap.data());
    double pAp = dot(p, Ap);
    double alpha = rz / pAp;
    axpy(alpha, p, x);
    axpy(-alpha, Ap, r);
    M(r, z);
    double rz2 = dot(r, z);
    double beta = rz2 / rz;
    for (int i = 0; i < n; ++i) p[i] = z[i] + beta * p[i];
    rz = rz2;
  }
  lg.iters = (int)lg.relres.size();
  if (log) { /* caller prints */ }
  return lg;
}

// ------------------------------------------------------------ BiCGStab -----
inline KrylovLog bicgstab(const CSR& A, const std::vector<double>& b, std::vector<double>& x,
                          const std::function<void(const std::vector<double>&, std::vector<double>&)>& M,
                          double tol, int maxit) {
  const int n = A.n;
  std::vector<double> r(n), r0(n), v(n), p(n), ph(n), s(n), t(n), z(n), z2(n);
  A.matvec(x.data(), r.data());
  for (int i = 0; i < n; ++i) r[i] = b[i] - r[i];
  r0 = r;
  double bnorm = std::max(norm2(b), 1e-300), rho = 1, alpha = 1, omega = 1;
  KrylovLog lg;
  p = r; v.assign(n, 0);
  for (int it = 0; it < maxit; ++it) {
    double rn = norm2(r);
    lg.relres.push_back(rn / bnorm);
    lg.eta_round = eta_round_estimate(A, b, x);
    if (rn <= lg.eta_round) { lg.rounding_stagnation = true; lg.converged = true; break; }
    if (rn / bnorm < tol) { lg.converged = true; break; }
    double rho1 = dot(r0, r);
    if (std::fabs(rho1) < 1e-300) break;
    double beta = (rho1 / rho) * (alpha / omega);
    for (int i = 0; i < n; ++i) p[i] = r[i] + beta * (p[i] - omega * v[i]);
    M(p, ph);
    A.matvec(ph.data(), v.data());
    alpha = rho1 / dot(r0, v);
    for (int i = 0; i < n; ++i) s[i] = r[i] - alpha * v[i];
    M(s, z);
    A.matvec(z.data(), t.data());
    double tt = dot(t, t);
    omega = (tt > 0) ? dot(t, s) / tt : 0.0;
    for (int i = 0; i < n; ++i) { x[i] += alpha * ph[i] + omega * z[i]; r[i] = s[i] - omega * t[i]; }
    rho = rho1;
    if (std::fabs(omega) < 1e-300) break;
  }
  lg.iters = (int)lg.relres.size();
  return lg;
}

// --------------------------------------------------------------- FGMRES ----
// Flexible GMRES(m) with restarts; the preconditioner may vary per iteration
// (used here with the FP32 AMG V-cycle inner solve inside the FP64 outer loop).
// Modified Gram-Schmidt Arnoldi + Givens QR on the Hessenberg matrix.
inline KrylovLog fgmres(const CSR& A, const std::vector<double>& b, std::vector<double>& x,
                        const std::function<void(const std::vector<double>&, std::vector<double>&)>& M,
                        double tol, int maxit, int m = 30) {
  const int n = A.n;
  KrylovLog lg;
  const double bnorm = std::max(norm2(b), 1e-300);
  std::vector<double> r(n);
  A.matvec(x.data(), r.data());
  for (int i = 0; i < n; ++i) r[i] = b[i] - r[i];
  int total = 0;
  while (total < maxit) {
    const double rn = norm2(r);
    lg.relres.push_back(rn / bnorm);
    lg.eta_round = eta_round_estimate(A, b, x);
    if (rn <= lg.eta_round) { lg.rounding_stagnation = true; lg.converged = true; break; }
    if (rn / bnorm < tol) { lg.converged = true; break; }

    std::vector<std::vector<double>> V(m + 1, std::vector<double>(n, 0.0));
    std::vector<std::vector<double>> Z(m, std::vector<double>(n, 0.0));
    std::vector<std::vector<double>> H(m + 1, std::vector<double>(m, 0.0));
    std::vector<double> cs(m, 0.0), sn(m, 0.0), g(m + 1, 0.0);
    g[0] = rn;
    for (int i = 0; i < n; ++i) V[0][i] = r[i] / rn;
    int j = 0;
    for (; j < m && total < maxit; ++j, ++total) {
      M(V[j], Z[j]);                       // flexible: fresh preconditioner each step
      std::vector<double> w(n);
      A.matvec(Z[j].data(), w.data());
      for (int i = 0; i <= j; ++i) {      // MGS
        H[i][j] = dot(w, V[i]);
        for (int k = 0; k < n; ++k) w[k] -= H[i][j] * V[i][k];
      }
      H[j + 1][j] = norm2(w);
      if (H[j + 1][j] > 1e-300)
        for (int k = 0; k < n; ++k) V[j + 1][k] = w[k] / H[j + 1][j];
      for (int i = 0; i < j; ++i) {        // apply previous Givens rotations
        const double t = H[i][j];
        H[i][j] = cs[i] * t + sn[i] * H[i + 1][j];
        H[i + 1][j] = -sn[i] * t + cs[i] * H[i + 1][j];
      }
      const double hh = std::hypot(H[j][j], H[j + 1][j]);  // new rotation
      cs[j] = H[j][j] / hh; sn[j] = H[j + 1][j] / hh;
      H[j][j] = cs[j] * H[j][j] + sn[j] * H[j + 1][j];
      H[j + 1][j] = 0.0;
      g[j + 1] = -sn[j] * g[j];
      g[j] = cs[j] * g[j];
      lg.relres.push_back(std::fabs(g[j + 1]) / bnorm);
      if (std::fabs(g[j + 1]) / bnorm < tol) { ++j; break; }
    }
    // back-substitution y = H^{-1} g, then x += V_j Z_j y  (flexible update)
    std::vector<double> y(j, 0.0);
    for (int i = j - 1; i >= 0; --i) {
      double s = g[i];
      for (int k = i + 1; k < j; ++k) s -= H[i][k] * y[k];
      y[i] = s / H[i][i];
    }
    for (int k = 0; k < n; ++k) {
      double acc = 0;
      for (int i = 0; i < j; ++i) acc += V[i][k] * 0.0;  // placeholder removed below
      (void)acc;
    }
    for (int i = 0; i < j; ++i)
      for (int k = 0; k < n; ++k) x[k] += y[i] * Z[i][k];
    A.matvec(x.data(), r.data());
    for (int i = 0; i < n; ++i) r[i] = b[i] - r[i];
    if (j < m) break;  // happy breakdown
  }
  lg.iters = (int)lg.relres.size();
  return lg;
}

// ------------------------------------- mixed-precision iterative refinement -
// Outer loop in FP64; inner correction solves in FP32 through the AMG
// preconditioner. Terminates on tol or on the eta_round rounding floor.
inline KrylovLog iterative_refinement_fp32(const CSR& A, const std::vector<double>& b,
                                          std::vector<double>& x,
                                          const std::function<void(const std::vector<double>&, std::vector<double>&)>& M,
                                          double tol, int max_outer, double omega = 0.6) {
  KrylovLog lg;
  const double bnorm = std::max(norm2(b), 1e-300);
  std::vector<double> r(A.n);
  for (int it = 0; it < max_outer; ++it) {
    A.matvec(x.data(), r.data());
    for (int i = 0; i < A.n; ++i) r[i] = b[i] - r[i];   // FP64 residual
    const double rn = norm2(r);
    lg.relres.push_back(rn / bnorm);
    lg.eta_round = eta_round_estimate(A, b, x);
    if (rn <= lg.eta_round) { lg.rounding_stagnation = true; lg.converged = true; break; }
    if (rn / bnorm < tol) { lg.converged = true; break; }
    // inner correction equation A dx = r solved approximately in FP32:
    // FP32 PCG with the FP32 AMG V-cycle as preconditioner
    std::vector<float> dx(A.n, 0.0), rf(A.n), z(A.n), p(A.n), Ap(A.n);
    for (int i = 0; i < A.n; ++i) rf[i] = (float)r[i];
    auto Mf = [&](const std::vector<float>& rin, std::vector<float>& zout) {
      std::vector<double> rd(rin.begin(), rin.end()), zd(rin.size());
      M(rd, zd);
      zout.resize(rin.size());
      for (size_t i = 0; i < rin.size(); ++i) zout[i] = (float)zd[i];
    };
    auto matvecf = [&](const std::vector<float>& xv, std::vector<float>& y) {
      for (int i = 0; i < A.n; ++i) {
        float acc = 0;
        for (int k = A.ptr[i]; k < A.ptr[i + 1]; ++k) acc += (float)A.val[k] * xv[A.col[k]];
        y[i] = acc;
      }
    };
    {
      std::vector<float> rr = rf;
      matvecf(dx, Ap);
      for (int i = 0; i < A.n; ++i) rr[i] = rf[i] - Ap[i];
      double s0 = 0; for (float v : rr) s0 += (double)v * v;
      const float rnorm0 = (float)std::max(std::sqrt(s0), 1e-30);
      Mf(rr, z);
      p = z;
      float rz = 0; for (int i = 0; i < A.n; ++i) rz += rr[i] * z[i];
      for (int it = 0; it < 25; ++it) {
        float rn = 0; for (float v : rr) rn += v * v; rn = std::sqrt(rn);
        if (rn / rnorm0 < 0.02f) break;   // loose inner tolerance: FP32 inner only
        matvecf(p, Ap);
        float pAp = 0; for (int i = 0; i < A.n; ++i) pAp += p[i] * Ap[i];
        float alpha = rz / pAp;
        for (int i = 0; i < A.n; ++i) { dx[i] += alpha * p[i]; rr[i] -= alpha * Ap[i]; }
        Mf(rr, z);
        float rz2 = 0; for (int i = 0; i < A.n; ++i) rz2 += rr[i] * z[i];
        float beta = rz2 / rz;
        for (int i = 0; i < A.n; ++i) p[i] = z[i] + beta * p[i];
        rz = rz2;
      }
    }
    for (int i = 0; i < A.n; ++i) x[i] += omega * (double)dx[i];  // FP64 correction
  }
  lg.iters = (int)lg.relres.size();
  return lg;
}

}  // namespace linalg
