// SPDX-License-Identifier: MIT
// MDO topopt backend -- dense/sparse linear algebra kernels.
// Self contained C++20. No external dependencies.
#pragma once

#include <algorithm>
#include <cmath>
#include <complex>
#include <cstddef>
#include <limits>
#include <stdexcept>
#include <type_traits>
#include <utility>
#include <vector>

namespace mdo {

using Vec = std::vector<double>;
using Mat = std::vector<std::vector<double>>;

inline double dot(const Vec& a, const Vec& b) {
  double s = 0.0;
  for (std::size_t i = 0; i < a.size(); ++i) s += a[i] * b[i];
  return s;
}
inline double norm2(const Vec& a) { return std::sqrt(dot(a, a)); }
inline double maxAbs(const Vec& a) {
  double s = 0.0;
  for (double v : a) s = std::max(s, std::abs(v));
  return s;
}
inline Vec axpy(double alpha, const Vec& x, Vec y) {
  for (std::size_t i = 0; i < x.size(); ++i) y[i] += alpha * x[i];
  return y;
}
inline Vec scaled(const Vec& x, double a) {
  Vec y(x.size());
  for (std::size_t i = 0; i < x.size(); ++i) y[i] = a * x[i];
  return y;
}
inline Vec hadamard(const Vec& a, const Vec& b) {
  Vec y(a.size());
  for (std::size_t i = 0; i < a.size(); ++i) y[i] = a[i] * b[i];
  return y;
}

// ---------------------------------------------------------------- triplets
struct Triplet {
  int row;
  int col;
  double val;
};

// Compressed sparse row matrix (real).
struct Csr {
  int n = 0;
  std::vector<int> rowptr;
  std::vector<int> colind;
  std::vector<double> val;

  void build(int size, std::vector<Triplet> trip) {
    n = size;
    for (auto& t : trip) {
      if (t.row < 0 || t.row >= n || t.col < 0 || t.col >= n)
        throw std::runtime_error("Csr::build index out of range");
    }
    std::sort(trip.begin(), trip.end(), [](const Triplet& a, const Triplet& b) {
      return a.row < b.row || (a.row == b.row && a.col < b.col);
    });
    rowptr.assign(static_cast<std::size_t>(n) + 1, 0);
    colind.clear();
    val.clear();
    int cur = 0;
    for (const auto& t : trip) {
      if (t.row != cur) {
        while (cur < t.row) rowptr[static_cast<std::size_t>(++cur)] = static_cast<int>(val.size());
      }
      if (!colind.empty() && colind.back() == t.col && static_cast<int>(val.size()) > rowptr[static_cast<std::size_t>(cur)]) {
        val.back() += t.val;
      } else {
        colind.push_back(t.col);
        val.push_back(t.val);
      }
    }
    while (cur < n) rowptr[static_cast<std::size_t>(++cur)] = static_cast<int>(val.size());
  }

  void matvec(const Vec& x, Vec& y) const {
    for (int i = 0; i < n; ++i) {
      double s = 0.0;
      for (int k = rowptr[static_cast<std::size_t>(i)]; k < rowptr[static_cast<std::size_t>(i + 1)]; ++k)
        s += val[static_cast<std::size_t>(k)] * x[static_cast<std::size_t>(colind[static_cast<std::size_t>(k)])];
      y[static_cast<std::size_t>(i)] = s;
    }
  }

  Csr transpose() const {
    Csr t;
    t.n = n;
    t.rowptr.assign(static_cast<std::size_t>(n) + 1, 0);
    t.colind.resize(colind.size());
    t.val.resize(val.size());
    for (int i = 0; i < n; ++i)
      for (int k = rowptr[static_cast<std::size_t>(i)]; k < rowptr[static_cast<std::size_t>(i + 1)]; ++k)
        t.rowptr[static_cast<std::size_t>(colind[static_cast<std::size_t>(k)] + 1)] += 1;
    for (int i = 0; i < n; ++i) t.rowptr[static_cast<std::size_t>(i + 1)] += t.rowptr[static_cast<std::size_t>(i)];
    std::vector<int> next(t.rowptr.begin(), t.rowptr.end() - 1);
    for (int i = 0; i < n; ++i) {
      for (int k = rowptr[static_cast<std::size_t>(i)]; k < rowptr[static_cast<std::size_t>(i + 1)]; ++k) {
        int j = colind[static_cast<std::size_t>(k)];
        int dst = next[static_cast<std::size_t>(j)]++;
        t.colind[static_cast<std::size_t>(dst)] = i;
        t.val[static_cast<std::size_t>(dst)] = val[static_cast<std::size_t>(k)];
      }
    }
    return t;
  }

  Vec diag() const {
    Vec d(static_cast<std::size_t>(n), 0.0);
    for (int i = 0; i < n; ++i)
      for (int k = rowptr[static_cast<std::size_t>(i)]; k < rowptr[static_cast<std::size_t>(i + 1)]; ++k)
        if (colind[static_cast<std::size_t>(k)] == i) d[static_cast<std::size_t>(i)] += val[static_cast<std::size_t>(k)];
    return d;
  }
};

struct SolveInfo {
  int iters = 0;
  double residual = 0.0;
  bool converged = false;
};

// Jacobi preconditioned conjugate gradients (SPD systems).
inline SolveInfo pcg(const Csr& A, const Vec& b, Vec& x, double tol = 1e-11, int maxit = 4000) {
  const int n = A.n;
  x.assign(static_cast<std::size_t>(n), 0.0);
  Vec r = b, z(static_cast<std::size_t>(n)), p(static_cast<std::size_t>(n));
  Vec Ap(static_cast<std::size_t>(n));
  Vec dinv(static_cast<std::size_t>(n), 1.0);
  Vec diag = A.diag();
  for (int i = 0; i < n; ++i)
    if (std::abs(diag[static_cast<std::size_t>(i)]) > 1e-300)
      dinv[static_cast<std::size_t>(i)] = 1.0 / diag[static_cast<std::size_t>(i)];
  const double bnorm = std::max(norm2(b), 1e-300);
  for (int i = 0; i < n; ++i) z[static_cast<std::size_t>(i)] = dinv[static_cast<std::size_t>(i)] * r[static_cast<std::size_t>(i)];
  p = z;
  double rz = dot(r, z);
  SolveInfo info;
  for (int it = 0; it < maxit; ++it) {
    A.matvec(p, Ap);
    const double pAp = dot(p, Ap);
    if (!(pAp > 0.0)) break;
    const double alpha = rz / pAp;
    x = axpy(alpha, p, x);
    r = axpy(-alpha, Ap, r);
    const double rn = norm2(r);
    info.iters = it + 1;
    info.residual = rn / bnorm;
    if (info.residual < tol) {
      info.converged = true;
      return info;
    }
    for (int i = 0; i < n; ++i) z[static_cast<std::size_t>(i)] = dinv[static_cast<std::size_t>(i)] * r[static_cast<std::size_t>(i)];
    const double rzNew = dot(r, z);
    const double beta = rzNew / std::max(rz, 1e-300);
    for (int i = 0; i < n; ++i)
      p[static_cast<std::size_t>(i)] = z[static_cast<std::size_t>(i)] + beta * p[static_cast<std::size_t>(i)];
    rz = rzNew;
  }
  info.converged = info.residual < tol;
  return info;
}

// Jacobi preconditioned BiCGSTAB for non-symmetric systems (upwind advection).
inline SolveInfo bicgstab(const Csr& A, const Vec& b, Vec& x, double tol = 1e-12, int maxit = 4000) {
  const int n = A.n;
  const std::size_t N = static_cast<std::size_t>(n);
  Vec diag = A.diag();
  auto precond = [&](const Vec& v) {
    Vec o(N);
    for (int i = 0; i < n; ++i) {
      const double d = diag[static_cast<std::size_t>(i)];
      o[static_cast<std::size_t>(i)] = std::abs(d) > 1e-300 ? v[static_cast<std::size_t>(i)] / d : v[static_cast<std::size_t>(i)];
    }
    return o;
  };
  x.assign(N, 0.0);
  Vec r = b, rhat = b;
  Vec rho_prev(1, 1.0), alpha(1, 1.0), omega(1, 1.0);
  double rho = 1.0, alp = 1.0, om = 1.0;
  Vec v(N, 0.0), p(N, 0.0);
  SolveInfo info;
  const double bnorm = std::max(norm2(b), 1e-300);
  for (int it = 0; it < maxit; ++it) {
    const double rhoNew = dot(rhat, r);
    if (std::abs(rhoNew) < 1e-300) break;
    rho_prev[0] = rho;
    if (it == 0) p.assign(N, 0.0);
    const double beta = (rhoNew / (rho_prev[0] == 0.0 ? 1e-300 : rho_prev[0])) *
                        (it == 0 ? 1.0 : (alp / (om == 0.0 ? 1e-300 : om)));
    if (it > 0) {
      for (std::size_t i = 0; i < N; ++i) p[i] = r[i] + beta * (p[i] - om * v[i]);
    } else {
      p = r;
    }
    rho = rhoNew;
    Vec ph = precond(p);
    A.matvec(ph, v);
    const double denom = dot(rhat, v);
    if (std::abs(denom) < 1e-300) break;
    alp = rho / denom;
    Vec s(N);
    for (std::size_t i = 0; i < N; ++i) s[i] = r[i] - alp * v[i];
    if (norm2(s) / bnorm < tol) {
      x = axpy(alp, ph, x);
      info.iters = it + 1;
      info.residual = norm2(s) / bnorm;
      info.converged = true;
      return info;
    }
    Vec sh = precond(s);
    Vec t(N);
    A.matvec(sh, t);
    const double tt = dot(t, t);
    if (tt < 1e-300) break;
    om = dot(t, s) / tt;
    x = axpy(alp, ph, x);
    x = axpy(om, sh, x);
    r = s;
    r = axpy(-om, t, r);
    info.iters = it + 1;
    info.residual = norm2(r) / bnorm;
    if (info.residual < tol) {
      info.converged = true;
      return info;
    }
    (void)alpha;
    (void)omega;
  }
  info.converged = info.residual < tol;
  return info;
}


// Jacobi preconditioned restarted GMRES. Robust for the non-symmetric
// advection operators and for systems with Dirichlet row replacement.
inline SolveInfo gmres(const Csr& A, const Vec& b, Vec& x, int restart = 40, double tol = 1e-12, int maxit = 4000) {
  const int n = A.n;
  const std::size_t N = static_cast<std::size_t>(n);
  Vec diag = A.diag();
  Vec dinv(N, 1.0);
  for (int i = 0; i < n; ++i) {
    const double d = diag[static_cast<std::size_t>(i)];
    if (std::abs(d) > 1e-300) dinv[static_cast<std::size_t>(i)] = 1.0 / d;
  }
  auto precond = [&](const Vec& v) {
    Vec o(N);
    for (int i = 0; i < n; ++i) o[static_cast<std::size_t>(i)] = dinv[static_cast<std::size_t>(i)] * v[static_cast<std::size_t>(i)];
    return o;
  };
  x.assign(N, 0.0);
  const double bnorm = std::max(norm2(b), 1e-300);
  SolveInfo info;
  Vec Ax(N);
  int total = 0;
  while (total < maxit) {
    A.matvec(x, Ax);
    Vec r(N);
    for (int i = 0; i < n; ++i) r[static_cast<std::size_t>(i)] = b[static_cast<std::size_t>(i)] - Ax[static_cast<std::size_t>(i)];
    const double beta = norm2(r);
    info.residual = beta / bnorm;
    if (info.residual < tol) {
      info.converged = true;
      info.iters = total;
      return info;
    }
    const int m = std::min(restart, maxit - total);
    std::vector<Vec> V(static_cast<std::size_t>(m + 1), Vec(N, 0.0));
    Mat H(static_cast<std::size_t>(m + 1), Vec(static_cast<std::size_t>(m + 1), 0.0));
    std::vector<double> cs(static_cast<std::size_t>(m + 1), 0.0), sn(static_cast<std::size_t>(m + 1), 0.0),
        g(static_cast<std::size_t>(m + 1), 0.0);
    for (int i = 0; i < n; ++i) V[0][static_cast<std::size_t>(i)] = r[static_cast<std::size_t>(i)] / beta;
    g[0] = beta;
    int k = 0;
    for (; k < m; ++k) {
      const Vec z = precond(V[static_cast<std::size_t>(k)]);
      Vec w(N);
      A.matvec(z, w);
      for (int i = 0; i <= k; ++i) {
        const double h = dot(w, V[static_cast<std::size_t>(i)]);
        H[static_cast<std::size_t>(i)][static_cast<std::size_t>(k)] = h;
        w = axpy(-h, V[static_cast<std::size_t>(i)], w);
      }
      H[static_cast<std::size_t>(k + 1)][static_cast<std::size_t>(k)] = norm2(w);
      if (H[static_cast<std::size_t>(k + 1)][static_cast<std::size_t>(k)] > 1e-14) {
        V[static_cast<std::size_t>(k + 1)] = scaled(w, 1.0 / H[static_cast<std::size_t>(k + 1)][static_cast<std::size_t>(k)]);
      }
      for (int i = 0; i < k; ++i) {
        const double t = cs[static_cast<std::size_t>(i)] * H[static_cast<std::size_t>(i)][static_cast<std::size_t>(k)] +
                         sn[static_cast<std::size_t>(i)] * H[static_cast<std::size_t>(i + 1)][static_cast<std::size_t>(k)];
        H[static_cast<std::size_t>(i + 1)][static_cast<std::size_t>(k)] =
            -sn[static_cast<std::size_t>(i)] * H[static_cast<std::size_t>(i)][static_cast<std::size_t>(k)] +
            cs[static_cast<std::size_t>(i)] * H[static_cast<std::size_t>(i + 1)][static_cast<std::size_t>(k)];
        H[static_cast<std::size_t>(i)][static_cast<std::size_t>(k)] = t;
      }
      const double hk = H[static_cast<std::size_t>(k)][static_cast<std::size_t>(k)];
      const double hk1 = H[static_cast<std::size_t>(k + 1)][static_cast<std::size_t>(k)];
      const double den = std::sqrt(hk * hk + hk1 * hk1);
      if (den < 1e-300) {
        ++k;
        break;
      }
      cs[static_cast<std::size_t>(k)] = hk / den;
      sn[static_cast<std::size_t>(k)] = hk1 / den;
      H[static_cast<std::size_t>(k)][static_cast<std::size_t>(k)] = den;
      H[static_cast<std::size_t>(k + 1)][static_cast<std::size_t>(k)] = 0.0;
      g[static_cast<std::size_t>(k + 1)] = -sn[static_cast<std::size_t>(k)] * g[static_cast<std::size_t>(k)];
      g[static_cast<std::size_t>(k)] = cs[static_cast<std::size_t>(k)] * g[static_cast<std::size_t>(k)];
      ++total;
      if (std::abs(g[static_cast<std::size_t>(k + 1)]) / bnorm < tol) {
        ++k;
        break;
      }
    }
    Vec y(static_cast<std::size_t>(k), 0.0);
    for (int i = k - 1; i >= 0; --i) {
      double s2 = g[static_cast<std::size_t>(i)];
      for (int j = i + 1; j < k; ++j) s2 -= H[static_cast<std::size_t>(i)][static_cast<std::size_t>(j)] * y[static_cast<std::size_t>(j)];
      const double hii = H[static_cast<std::size_t>(i)][static_cast<std::size_t>(i)];
      y[static_cast<std::size_t>(i)] = std::abs(hii) > 1e-300 ? s2 / hii : 0.0;
    }
    for (int j = 0; j < k; ++j) {
      const Vec z = precond(V[static_cast<std::size_t>(j)]);
      x = axpy(y[static_cast<std::size_t>(j)], z, x);
    }
    if (k == 0) break;
  }
  A.matvec(x, Ax);
  double res = 0.0;
  for (int i = 0; i < n; ++i)
    res += (b[static_cast<std::size_t>(i)] - Ax[static_cast<std::size_t>(i)]) * (b[static_cast<std::size_t>(i)] - Ax[static_cast<std::size_t>(i)]);
  info.residual = std::sqrt(res) / bnorm;
  info.iters = total;
  info.converged = info.residual < tol;
  return info;
}

// ------------------------------------------------------------- dense kernels
// Gaussian elimination with partial pivoting. Templated so the same routine can
// be instantiated for std::complex<double> during complex-step verification.
template <class S>
std::vector<S> denseSolve(std::vector<S> A, std::vector<S> b, int n) {
  std::vector<int> piv(static_cast<std::size_t>(n));
  for (int i = 0; i < n; ++i) piv[static_cast<std::size_t>(i)] = i;
  for (int k = 0; k < n; ++k) {
    int best = k;
    double bestv = std::abs(A[static_cast<std::size_t>(k) * static_cast<std::size_t>(n) + static_cast<std::size_t>(k)]);
    for (int i = k + 1; i < n; ++i) {
      const double v = std::abs(A[static_cast<std::size_t>(i) * static_cast<std::size_t>(n) + static_cast<std::size_t>(k)]);
      if (v > bestv) {
        bestv = v;
        best = i;
      }
    }
    if (bestv < 1e-300) throw std::runtime_error("denseSolve: singular matrix");
    if (best != k) {
      for (int j = 0; j < n; ++j)
        std::swap(A[static_cast<std::size_t>(k) * static_cast<std::size_t>(n) + static_cast<std::size_t>(j)],
                  A[static_cast<std::size_t>(best) * static_cast<std::size_t>(n) + static_cast<std::size_t>(j)]);
      std::swap(b[static_cast<std::size_t>(k)], b[static_cast<std::size_t>(best)]);
    }
    const S pivot = A[static_cast<std::size_t>(k) * static_cast<std::size_t>(n) + static_cast<std::size_t>(k)];
    for (int i = k + 1; i < n; ++i) {
      const S f = A[static_cast<std::size_t>(i) * static_cast<std::size_t>(n) + static_cast<std::size_t>(k)] / pivot;
      if (f == S(0)) continue;
      A[static_cast<std::size_t>(i) * static_cast<std::size_t>(n) + static_cast<std::size_t>(k)] = S(0);
      for (int j = k + 1; j < n; ++j)
        A[static_cast<std::size_t>(i) * static_cast<std::size_t>(n) + static_cast<std::size_t>(j)] -=
            f * A[static_cast<std::size_t>(k) * static_cast<std::size_t>(n) + static_cast<std::size_t>(j)];
      b[static_cast<std::size_t>(i)] -= f * b[static_cast<std::size_t>(k)];
    }
  }
  std::vector<S> x(static_cast<std::size_t>(n), S(0));
  for (int i = n - 1; i >= 0; --i) {
    S s = b[static_cast<std::size_t>(i)];
    for (int j = i + 1; j < n; ++j)
      s -= A[static_cast<std::size_t>(i) * static_cast<std::size_t>(n) + static_cast<std::size_t>(j)] * x[static_cast<std::size_t>(j)];
    x[static_cast<std::size_t>(i)] = s / A[static_cast<std::size_t>(i) * static_cast<std::size_t>(n) + static_cast<std::size_t>(i)];
  }
  return x;
}

// Cholesky-based dense SPD solve (used by the MMA dual Newton system).
inline Vec denseCholeskySolve(Mat A, Vec b) {
  const int n = static_cast<int>(b.size());
  for (int i = 0; i < n; ++i) {
    for (int j = 0; j <= i; ++j) {
      double s = A[static_cast<std::size_t>(i)][static_cast<std::size_t>(j)];
      for (int k = 0; k < j; ++k)
        s -= A[static_cast<std::size_t>(i)][static_cast<std::size_t>(k)] * A[static_cast<std::size_t>(j)][static_cast<std::size_t>(k)];
      if (i == j) {
        A[static_cast<std::size_t>(i)][static_cast<std::size_t>(j)] = std::sqrt(std::max(s, 1e-14));
      } else {
        A[static_cast<std::size_t>(i)][static_cast<std::size_t>(j)] = s / A[static_cast<std::size_t>(j)][static_cast<std::size_t>(j)];
      }
    }
  }
  Vec y(static_cast<std::size_t>(n));
  for (int i = 0; i < n; ++i) {
    double s = b[static_cast<std::size_t>(i)];
    for (int k = 0; k < i; ++k) s -= A[static_cast<std::size_t>(i)][static_cast<std::size_t>(k)] * y[static_cast<std::size_t>(k)];
    y[static_cast<std::size_t>(i)] = s / A[static_cast<std::size_t>(i)][static_cast<std::size_t>(i)];
  }
  Vec x(static_cast<std::size_t>(n));
  for (int i = n - 1; i >= 0; --i) {
    double s = y[static_cast<std::size_t>(i)];
    for (int k = i + 1; k < n; ++k) s -= A[static_cast<std::size_t>(k)][static_cast<std::size_t>(i)] * x[static_cast<std::size_t>(k)];
    x[static_cast<std::size_t>(i)] = s / A[static_cast<std::size_t>(i)][static_cast<std::size_t>(i)];
  }
  return x;
}

// Symmetric 2x2 eigen-decomposition helper.
inline std::pair<double, double> eigen2(double a, double b, double d) {
  const double tr = a + d;
  const double disc = std::sqrt(std::max((a - d) * (a - d) + 4.0 * b * b, 0.0));
  return {0.5 * (tr - disc), 0.5 * (tr + disc)};
}

}  // namespace mdo
