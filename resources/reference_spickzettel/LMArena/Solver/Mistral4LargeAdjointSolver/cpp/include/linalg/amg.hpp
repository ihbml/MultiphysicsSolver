#pragma once
// ============================================================================
//  linalg/amg.hpp
//  Matrix-free / CSR algebraic multigrid:
//   - strength-of-connection graph + PMIS-style greedy maximal independent
//     set aggregation (hemispherically weighted: nodes picked by minimum
//     unassigned strong-neighbour count)
//   - unsmoothed or smoothed aggregation prolongation P (damped Jacobi sweep)
//   - Galerkin coarse operator A_c = P^T A P (CSR triple product)
//   - V-cycle with L1-Jacobi (GPU-friendly, no sequential GS bottleneck) or
//     Chebyshev polynomial smoothers
//   - mixed precision: hierarchy stored in FP64, V-cycle arithmetic in FP32
//     when instantiated with T=float (inner preconditioner of the FP64 outer
//     Krylov loop).
//  C++20, header-only.
// ============================================================================
#include <cstdint>
#include <cmath>
#include <vector>
#include <algorithm>
#include <numeric>
#include "krylov.hpp"

namespace amg {

using linalg::CSR;

struct Level {
  CSR A;                        // operator on this level
  CSR P;                        // prolongation to this level from the finer one (n_fine x n_coarse)
  int n = 0;
  // L1-Jacobi / Chebyshev smoother data
  std::vector<double> l1;       // L1 row sums  sum_j |a_ij|
  std::vector<double> diag;
  double lambda_max = 1.0;      // estimate for Chebyshev / smoothing parameter
};

// strength of connection: |a_ij| >= theta * max_k |a_ik|  (diagonal always strong)
inline std::vector<std::vector<int>> strong_neighbours(const CSR& A, double theta = 0.25) {
  const int n = A.n;
  std::vector<std::vector<int>> S(n);
  for (int i = 0; i < n; ++i) {
    double mx = 0;
    for (int k = A.ptr[i]; k < A.ptr[i + 1]; ++k) mx = std::max(mx, std::fabs(A.val[k]));
    for (int k = A.ptr[i]; k < A.ptr[i + 1]; ++k) {
      const int j = A.col[k];
      if (j == i || std::fabs(A.val[k]) >= theta * mx) S[i].push_back(j);
    }
  }
  return S;
}

// PMIS-style aggregation: greedy MIS over the strong graph. The root of each
// aggregate is the unassigned node with the MOST unassigned strong neighbours
// (hemispherical weighting -> large aggregates, aggressive coarsening).
inline std::vector<int> aggregate(const CSR& A, double theta = 0.25) {
  const int n = A.n;
  auto S = strong_neighbours(A, theta);
  std::vector<int> agg(n, -1);
  std::vector<char> assigned(n, 0);
  std::vector<int> order(n);
  std::iota(order.begin(), order.end(), 0);
  int nagg = 0;
  bool progress = true;
  while (progress) {
    progress = false;
    // pick unassigned node with most unassigned strong neighbours
    int best = -1, bestcnt = -1;
    for (int i : order) {
      if (assigned[i]) continue;
      int c = 0;
      for (int j : S[i]) if (!assigned[j]) ++c;
      if (c > bestcnt) { bestcnt = c; best = i; }
    }
    if (best < 0) break;
    progress = true;
    agg[best] = nagg;
    assigned[best] = 1;
    for (int j : S[best]) if (!assigned[j]) { agg[j] = nagg; assigned[j] = 1; }
    ++nagg;
  }
  for (int i = 0; i < n; ++i) if (agg[i] < 0) agg[i] = nagg++;
  return agg;
}

// tentative prolongation P (n x nc): P[i, agg[i]] = 1
inline CSR tentative_P(const std::vector<int>& agg, int nc) {
  const int n = (int)agg.size();
  CSR P(n);
  P.col.resize(n); P.val.assign(n, 1.0);
  for (int i = 0; i < n; ++i) P.col[i] = agg[i];
  for (int i = 0; i <= n; ++i) P.ptr[i] = i;
  P.n = n;
  // represent as n x nc sparse: store transposed-friendly layout; we keep row-major
  // with one entry per row. For RAP we use the agg map directly.
  (void)nc;
  return P;
}

// Galerkin coarse operator A_c = P^T A P via aggregation map (CSR)
inline CSR galerkin(const CSR& A, const std::vector<int>& agg, int nc) {
  std::vector<std::vector<std::pair<int, double>>> rows(nc);
  for (int i = 0; i < A.n; ++i) {
    const int ci = agg[i];
    for (int k = A.ptr[i]; k < A.ptr[i + 1]; ++k) {
      rows[ci].push_back({agg[A.col[k]], A.val[k]});
    }
  }
  return CSR::from_rows(nc, rows);
}

// smoothed aggregation: P <- (I - omega D^{-1} A) P_tentative, one damped
// Jacobi sweep; omega = 2/(3 lambda_max(D^{-1}A)) estimated by power iteration.
inline std::vector<std::vector<std::pair<int, double>>> smooth_prolongation(
    const CSR& A, const std::vector<int>& agg, int nc, bool smoothed = true) {
  const int n = A.n;
  std::vector<double> dinv(n);
  for (int i = 0; i < n; ++i) {
    double d = 0;
    for (int k = A.ptr[i]; k < A.ptr[i + 1]; ++k) if (A.col[k] == i) d = A.val[k];
    dinv[i] = (d != 0) ? 1.0 / d : 0.0;
  }
  // lambda_max(D^{-1} A) by power iteration
  double lmax = 1.0;
  if (smoothed) {
    std::vector<double> x(n, 1.0), y(n);
    for (int it = 0; it < 30; ++it) {
      for (int i = 0; i < n; ++i) {
        double acc = 0;
        for (int k = A.ptr[i]; k < A.ptr[i + 1]; ++k) acc += A.val[k] * x[A.col[k]];
        y[i] = dinv[i] * acc;
      }
      double nn = linalg::norm2(y);
      if (nn == 0) break;
      for (auto& v : y) v /= nn;
      x = y;
      lmax = nn;
    }
    lmax = std::max(lmax, 1e-12);
  }
  const double omega = smoothed ? 2.0 / (3.0 * lmax) : 0.0;
  // rows of P: P[i] = e_{agg[i]} - omega dinv[i] * A[i,:]
  std::vector<std::vector<std::pair<int, double>>> P(n);
  for (int i = 0; i < n; ++i) {
    P[i].push_back({agg[i], 1.0});
    if (smoothed)
      for (int k = A.ptr[i]; k < A.ptr[i + 1]; ++k)
        P[i].push_back({agg[A.col[k]], -omega * dinv[i] * A.val[k]});
    // merge duplicates
    std::sort(P[i].begin(), P[i].end());
    std::vector<std::pair<int, double>> m;
    for (auto& pr : P[i]) {
      if (!m.empty() && m.back().first == pr.first) m.back().second += pr.second;
      else m.push_back(pr);
    }
    P[i] = m;
  }
  return P;
}

// CSR from row lists of P (n x nc)
inline CSR csr_from_rows(int n, int nc, const std::vector<std::vector<std::pair<int, double>>>& rows) {
  CSR P; P.n = n;
  P.ptr.assign(n + 1, 0);
  for (int i = 0; i < n; ++i) {
    for (auto& pr : rows[i]) { P.col.push_back(pr.first); P.val.push_back(pr.second); }
    P.ptr[i + 1] = (int32_t)P.col.size();
  }
  (void)nc;
  return P;
}

// A_c = P^T A P with P given as row lists (n x nc):
//   A_c[c1][c2] = sum_i P[i][c1] * (A P)[i][c2]
inline CSR galerkin_P(const CSR& A, const std::vector<std::vector<std::pair<int, double>>>& P, int nc) {
  const int n = A.n;
  std::vector<std::vector<std::pair<int, double>>> AP(n);
  for (int i = 0; i < n; ++i) {
    std::vector<std::pair<int, double>> ap;
    for (int k = A.ptr[i]; k < A.ptr[i + 1]; ++k) {
      const int j = A.col[k];
      for (auto& pr : P[j]) ap.push_back({pr.first, A.val[k] * pr.second});
    }
    std::sort(ap.begin(), ap.end());
    for (auto& pr : ap) {
      if (!AP[i].empty() && AP[i].back().first == pr.first) AP[i].back().second += pr.second;
      else AP[i].push_back(pr);
    }
  }
  std::vector<std::vector<std::pair<int, double>>> rc(nc);
  for (int i = 0; i < n; ++i)
    for (auto& pc : P[i])
      for (auto& q : AP[i]) rc[pc.first].push_back({q.first, pc.second * q.second});
  return CSR::from_rows(nc, rc);
}

struct Hierarchy {
  std::vector<Level> levels;  // levels[0] = finest
  bool smoothed = true;

  void build(const CSR& A, int coarse_threshold = 64, double theta = 0.25, bool smoothed_ = true) {
    smoothed = smoothed_;
    CSR Af = A;
    while (true) {
      Level lv;
      lv.A = Af;
      lv.n = Af.n;
      lv.l1.assign(Af.n, 0.0);
      lv.diag.assign(Af.n, 0.0);
      for (int i = 0; i < Af.n; ++i) {
        double s = 0, d = 0;
        for (int k = Af.ptr[i]; k < Af.ptr[i + 1]; ++k) {
          s += std::fabs(Af.val[k]);
          if (Af.col[k] == i) d = Af.val[k];
        }
        lv.l1[i] = s; lv.diag[i] = d;
      }
      // lambda_max estimate (Gershgorin on D^{-1}A) for Chebyshev
      double lmax = 0;
      for (int i = 0; i < Af.n; ++i) {
        double s = 0;
        for (int k = Af.ptr[i]; k < Af.ptr[i + 1]; ++k)
          if (Af.col[k] != i) s += std::fabs(Af.val[k] * (lv.diag[i] != 0 ? 1.0 / lv.diag[i] : 0));
        lmax = std::max(lmax, 1.0 + s);
      }
      lv.lambda_max = std::max(lmax, 1e-12);
      levels.push_back(std::move(lv));
      if (Af.n <= coarse_threshold) break;
      auto agg = aggregate(Af, theta);
      int nc = *std::max_element(agg.begin(), agg.end()) + 1;
      // stop when coarsening becomes too weak (ratio > 0.7): the current level
      // is then the coarsest and is solved directly by dense LU in the V-cycle
      if (nc > 0.7 * Af.n) break;
      // safeguard: if coarsening is weak, fall back to unsmoothed tentative P
      // to avoid coarse-operator nnz blow-up
      bool use_smooth = smoothed && (nc <= 0.7 * Af.n);
      auto P = smooth_prolongation(Af, agg, nc, use_smooth);
      Level& fine = levels.back();
      fine.P = csr_from_rows(Af.n, nc, P);
      // store P row-lists for RAP: rebuild from csr
      std::vector<std::vector<std::pair<int, double>>> Prows(Af.n);
      for (int i = 0; i < Af.n; ++i)
        for (int k = fine.P.ptr[i]; k < fine.P.ptr[i + 1]; ++k)
          Prows[i].push_back({fine.P.col[k], fine.P.val[k]});
      Af = galerkin_P(Af, Prows, nc);
    }
  }

  int n_levels() const { return (int)levels.size(); }

  // L1-Jacobi smoother sweep (GPU-friendly: fully parallel, no dependencies):
  //   x <- x + omega * r / L1_i
  template <typename T>
  void l1_jacobi_sweep(const Level& lv, std::vector<T>& x, const std::vector<T>& b, T omega) const {
    const int n = lv.n;
    for (int i = 0; i < n; ++i) {
      T acc = 0;
      for (int k = lv.A.ptr[i]; k < lv.A.ptr[i + 1]; ++k) acc += (T)lv.A.val[k] * x[lv.A.col[k]];
      T r = b[i] - acc;
      x[i] += omega * r / (T)lv.l1[i];
    }
  }

  // Chebyshev polynomial smoother (degree q) of D^{-1} r; needs lambda_max.
  template <typename T>
  void chebyshev_sweep(const Level& lv, std::vector<T>& x, const std::vector<T>& b, int q = 3) const {
    const int n = lv.n;
    const double lmax = lv.lambda_max;
    const double d = lmax / 2.0, c = lmax / 2.0;  // interval [0, lmax] -> [-1,1]
    std::vector<T> r(n);
    for (int i = 0; i < n; ++i) {
      T acc = 0;
      for (int k = lv.A.ptr[i]; k < lv.A.ptr[i + 1]; ++k) acc += (T)lv.A.val[k] * x[lv.A.col[k]];
      r[i] = (b[i] - acc) / (T)lv.diag[i];
    }
    std::vector<T> p(n), z(n);
    double sigma = d / c;
    for (int i = 0; i < n; ++i) { p[i] = r[i] / (T)sigma; }
    for (int i = 0; i < n; ++i) x[i] += p[i];
    for (int k = 1; k < q; ++k) {
      for (int i = 0; i < n; ++i) {
        T acc = 0;
        for (int kk = lv.A.ptr[i]; kk < lv.A.ptr[i + 1]; ++kk) acc += (T)lv.A.val[kk] * p[lv.A.col[kk]];
        z[i] = ((b[i] - acc) / (T)lv.diag[i] - (T)(d - c) * 0.0) ;  // r(p)
        z[i] = (b[i] - acc) / (T)lv.diag[i];
      }
      const double sigma1 = 1.0 / (2.0 * (d / c) - sigma);
      for (int i = 0; i < n; ++i) {
        T pn = (T)(2.0 * sigma1) * z[i] - (T)sigma * p[i];
        x[i] += pn;
        p[i] = pn;
      }
      sigma = sigma1;
    }
  }

  // restrict: r_c = P^T r   (P stored row-major n_fine x n_coarse)
  template <typename T>
  void restrict(const Level& fine, const std::vector<T>& rf, std::vector<T>& rc) const {
    int ncoarse = 0;
    for (auto c : fine.P.col) ncoarse = std::max(ncoarse, (int)c + 1);
    rc.assign(ncoarse, 0);
    for (int i = 0; i < fine.n; ++i)
      for (int k = fine.P.ptr[i]; k < fine.P.ptr[i + 1]; ++k)
        rc[fine.P.col[k]] += (T)fine.P.val[k] * rf[i];
  }
  // prolong: e_f = P e_c
  template <typename T>
  void prolong(const Level& fine, const std::vector<T>& ec, std::vector<T>& ef) const {
    ef.assign(fine.n, 0);
    for (int i = 0; i < fine.n; ++i)
      for (int k = fine.P.ptr[i]; k < fine.P.ptr[i + 1]; ++k)
        ef[i] += (T)fine.P.val[k] * ec[fine.P.col[k]];
  }

  // one V-cycle: solve A_l x = b approximately, arithmetic in T (float for the
  // FP32 inner preconditioner). Smoother: L1-Jacobi (default) or Chebyshev.
  template <typename T>
  void vcycle(int l, std::vector<T>& x, const std::vector<T>& b, int pre = 2, int post = 2,
              bool chebyshev = false) const {
    const Level& lv = levels[l];
    if (l == (int)levels.size() - 1) {
      // coarsest level: direct dense solve
      std::vector<std::vector<double>> Ad(lv.n, std::vector<double>(lv.n, 0.0));
      for (int i = 0; i < lv.n; ++i)
        for (int k = lv.A.ptr[i]; k < lv.A.ptr[i + 1]; ++k) Ad[i][lv.A.col[k]] = lv.A.val[k];
      std::vector<double> bb(lv.n), xx(lv.n);
      for (int i = 0; i < lv.n; ++i) bb[i] = b[i];
      if (linalg::dense_solve(Ad, bb, xx))
        for (int i = 0; i < lv.n; ++i) x[i] = (T)xx[i];
      return;
    }
    std::vector<T> r(lv.n);
    for (int s = 0; s < pre; ++s) {
      if (chebyshev) chebyshev_sweep<T>(lv, x, b, 3);
      else l1_jacobi_sweep<T>(lv, x, b, (T)1.0);
    }
    for (int i = 0; i < lv.n; ++i) {
      T acc = 0;
      for (int k = lv.A.ptr[i]; k < lv.A.ptr[i + 1]; ++k) acc += (T)lv.A.val[k] * x[lv.A.col[k]];
      r[i] = b[i] - acc;
    }
    std::vector<T> rc, ec(levels[l + 1].n, 0);
    restrict<T>(lv, r, rc);
    vcycle<T>(l + 1, ec, rc, pre, post, chebyshev);
    std::vector<T> ef;
    prolong<T>(lv, ec, ef);
    for (int i = 0; i < lv.n; ++i) x[i] += ef[i];
    for (int s = 0; s < post; ++s) {
      if (chebyshev) chebyshev_sweep<T>(lv, x, b, 3);
      else l1_jacobi_sweep<T>(lv, x, b, (T)1.0);
    }
  }

  // preconditioner apply: M^{-1} b ~= V-cycle(0, b) with zero initial guess,
  // computed in scalar type T (float => FP32 inner, double => FP64).
  template <typename T>
  void apply(const std::vector<double>& b, std::vector<double>& out) const {
    std::vector<T> x(levels[0].n, 0), bb(levels[0].n);
    for (size_t i = 0; i < b.size(); ++i) bb[i] = (T)b[i];
    vcycle<T>(0, x, bb);
    out.assign(levels[0].n, 0);
    for (int i = 0; i < levels[0].n; ++i) out[i] = (double)x[i];
  }
};

}  // namespace amg
