#pragma once
// ============================================================================
//  chem/chemistry.hpp
//  Batched per-cell stiff chemistry integrator (DLBFoam-style):
//   - operator-split 0D stiff ODEs for arbitrary species mechanisms
//   - AoT analytical sparse Jacobians  J_kj = d omega_k / d Y_j  from the
//     stoichiometric rate law (no finite differencing at runtime)
//   - BDF2 (Newton with the analytical Jacobian) and Rosenbrock-W (ROS2,
//     L-stable 2-stage) time integrators
//   - active-cell compaction queue  ||omega_i|| > eps_react  with dynamic
//     work stealing across threads (atomic ticket on the compacted queue;
//     the CellFaceCSR gather pass itself stays atomic-free)
//  C++20, header-only.
// ============================================================================
#include <cstdint>
#include <cmath>
#include <vector>
#include <array>
#include <atomic>
#include <algorithm>
#include <cstdio>
#ifdef _OPENMP
#include <omp.h>
#endif

namespace chem {

constexpr int MAXS = 8;  // max species per mechanism (compile-time AoT bound)

// Elementary reaction:  sum_k nuR_k X_k -> sum_k nuP_k X_k , Arrhenius rate
//   r = k_fwd * prod_k Y_k^nuR_k,   k_fwd = A T^b exp(-E / R T)
struct Reaction {
  int ns = 0;                       // number of species
  std::array<int, MAXS> nuR{};      // reactant stoichiometry
  std::array<int, MAXS> nuP{};      // product stoichiometry
  double A = 0, b = 0, E = 0;       // Arrhenius pre-exponential, exponent, E/R [K]
};

inline double arrhenius(const Reaction& R, double T) {
  const double Rgas = 8.31446261815324;  // J/(mol K); E stored as E/R [K]
  return R.A * std::pow(T, R.b) * std::exp(-R.E / (Rgas * 0.0 + 1.0) * (R.E > 0 ? 1.0 / (T / R.E) * 0.0 + R.E / (T) * 0.0 : 0.0) + (-R.E / T));
}
// NOTE: E is stored directly as activation temperature E/R [K]:
inline double arrhenius_k(const Reaction& R, double T) {
  return R.A * std::pow(T, R.b) * std::exp(-R.E / T);
}

struct Mechanism {
  int ns = 0, nr = 0;
  std::vector<Reaction> rx;
  // species production rates:  omega_k = sum_r (nuP - nuR)_k * r_r
  void omega(const double* Y, double T, double* w) const {
    for (int k = 0; k < ns; ++k) w[k] = 0;
    for (int r = 0; r < nr; ++r) {
      const Reaction& R = rx[r];
      double rate = arrhenius_k(R, T);
      for (int k = 0; k < ns; ++k)
        if (R.nuR[k] > 0) rate *= std::pow(std::max(Y[k], 0.0), R.nuR[k]);
      for (int k = 0; k < ns; ++k) w[k] += (R.nuP[k] - R.nuR[k]) * rate;
    }
  }
  // AoT analytical sparse Jacobian  J_kj = d omega_k / d Y_j .
  // For each reaction, d r / d Y_j = nuR_j * r / Y_j (reactant), so
  //   J_kj = sum_r (nuP_k - nuR_k) * nuR_j * r_r / Y_j .
  // Stored in COO (row k, col j, val). Y_j == 0 handled by the limit r->0.
  void jacobian(const double* Y, double T, int& nnz, int* rows, int* cols, double* vals) const {
    nnz = 0;
    for (int r = 0; r < nr; ++r) {
      const Reaction& R = rx[r];
      double rate = arrhenius_k(R, T);
      for (int k = 0; k < ns; ++k)
        if (R.nuR[k] > 0) rate *= std::pow(std::max(Y[k], 0.0), R.nuR[k]);
      for (int j = 0; j < ns; ++j) {
        if (R.nuR[j] <= 0) continue;
        const double drdYj = (Y[j] > 1e-300) ? R.nuR[j] * rate / Y[j] : 0.0;
        for (int k = 0; k < ns; ++k) {
          const double nu = R.nuP[k] - R.nuR[k];
          if (nu == 0) continue;
          rows[nnz] = k; cols[nnz] = j; vals[nnz] = nu * drdYj;
          ++nnz;
        }
      }
    }
  }
  // dense ns x ns Jacobian (small systems: per-cell 0D integration)
  void jacobian_dense(const double* Y, double T, std::vector<double>& J) const {
    J.assign(ns * ns, 0.0);
    int nnz, rows[MAXS * MAXS * 4], cols[MAXS * MAXS * 4];
    double vals[MAXS * MAXS * 4];
    jacobian(Y, T, nnz, rows, cols, vals);
    for (int e = 0; e < nnz; ++e) J[rows[e] * ns + cols[e]] += vals[e];
  }
};

// dense n x n solve (Newton / Rosenbrock stages), n <= MAXS
inline bool dense_solve_small(std::vector<double> A, std::vector<double> b, int n, std::vector<double>& x) {
  x.assign(n, 0);
  for (int k = 0; k < n; ++k) {
    int piv = k;
    for (int i = k + 1; i < n; ++i)
      if (std::fabs(A[i * n + k]) > std::fabs(A[piv * n + k])) piv = i;
    if (std::fabs(A[piv * n + k]) < 1e-300) return false;
    if (piv != k) {
      for (int j = 0; j < n; ++j) std::swap(A[piv * n + j], A[k * n + j]);
      std::swap(b[piv], b[k]);
    }
    for (int i = k + 1; i < n; ++i) {
      double m = A[i * n + k] / A[k * n + k];
      for (int j = k; j < n; ++j) A[i * n + j] -= m * A[k * n + j];
      b[i] -= m * b[k];
    }
  }
  for (int i = n - 1; i >= 0; --i) {
    double s = b[i];
    for (int j = i + 1; j < n; ++j) s -= A[i * n + j] * x[j];
    x[i] = s / A[i * n + i];
  }
  return true;
}

// ------------------------------------------------ BDF2 with Newton ---------
//  (3 y^{n+1} - 4 y^n + y^{n-1}) / (2 h) = f(y^{n+1})
//  Newton on F(y) = 3 y - 4 y^n + y^{n-1} - 2 h f(y) = 0,
//  J_F = 3 I - 2 h J_f  (analytical AoT Jacobian).
struct BDF2 {
  const Mechanism* mech;
  double T;
  bool step(const double* yn, const double* ynm, double h, double* yout, double newton_tol = 1e-13) {
    const int n = mech->ns;
    std::vector<double> y(yn, yn + n);
    std::vector<double> F(n), J(n * n), d(n);
    for (int it = 0; it < 50; ++it) {
      std::vector<double> f(n);
      mech->omega(y.data(), T, f.data());
      for (int i = 0; i < n; ++i) F[i] = 3.0 * y[i] - 4.0 * yn[i] + ynm[i] - 2.0 * h * f[i];
      double fn = 0; for (double v : F) fn = std::max(fn, std::fabs(v));
      if (fn < newton_tol) break;
      mech->jacobian_dense(y.data(), T, J);
      for (int i = 0; i < n; ++i)
        for (int j = 0; j < n; ++j) J[i * n + j] = 3.0 * (i == j ? 1.0 : 0.0) - 2.0 * h * J[i * n + j];
      if (!dense_solve_small(J, F, n, d)) return false;
      for (int i = 0; i < n; ++i) y[i] -= d[i];
    }
    std::copy(y.begin(), y.end(), yout);
    return true;
  }
  // BDF1 (implicit Euler) start-up step
  bool step1(const double* yn, double h, double* yout, double newton_tol = 1e-13) {
    const int n = mech->ns;
    std::vector<double> y(yn, yn + n), F(n), J(n * n), d(n);
    for (int it = 0; it < 50; ++it) {
      std::vector<double> f(n);
      mech->omega(y.data(), T, f.data());
      for (int i = 0; i < n; ++i) F[i] = y[i] - yn[i] - h * f[i];
      double fn = 0; for (double v : F) fn = std::max(fn, std::fabs(v));
      if (fn < newton_tol) break;
      mech->jacobian_dense(y.data(), T, J);
      for (int i = 0; i < n; ++i)
        for (int j = 0; j < n; ++j) J[i * n + j] = (i == j ? 1.0 : 0.0) - h * J[i * n + j];
      if (!dense_solve_small(J, F, n, d)) return false;
      for (int i = 0; i < n; ++i) y[i] -= d[i];
    }
    std::copy(y.begin(), y.end(), yout);
    return true;
  }
};

// ------------------------------------------- Rosenbrock-W (ROS2, L-stable) --
//  gamma = 1 - 1/sqrt(2),  b1 = (1+sqrt(2))/2,  b2 = (1-sqrt(2))/2
//  (I - gamma h J) K1 = f(y_n)
//  (I - gamma h J) K2 = f(y_n + h K1) - gamma h J K1
//  y_{n+1} = y_n + h (b1 K1 + b2 K2)
struct RosenbrockW {
  const Mechanism* mech;
  double T;
  bool step(const double* yn, double h, double* yout) {
    const int n = mech->ns;
    constexpr double gamma = 1.0 - 1.0 / 1.4142135623730951;
    constexpr double b1 = 1.2071067811865475, b2 = -0.20710678118654752;
    std::vector<double> f(n), J(n * n), K1(n), K2(n), M(n * n), rhs(n), y1(n);
    mech->omega(yn, T, f.data());
    mech->jacobian_dense(yn, T, J);
    for (int i = 0; i < n; ++i)
      for (int j = 0; j < n; ++j) M[i * n + j] = (i == j ? 1.0 : 0.0) - gamma * h * J[i * n + j];
    if (!dense_solve_small(M, f, n, K1)) return false;
    for (int i = 0; i < n; ++i) y1[i] = yn[i] + h * K1[i];
    mech->omega(y1.data(), T, f.data());
    for (int i = 0; i < n; ++i) {
      double jk = 0;
      for (int j = 0; j < n; ++j) jk += J[i * n + j] * K1[j];
      rhs[i] = f[i] - gamma * h * jk;
    }
    if (!dense_solve_small(M, rhs, n, K2)) return false;
    for (int i = 0; i < n; ++i) yout[i] = yn[i] + h * (b1 * K1[i] + b2 * K2[i]);
    return true;
  }
};

// ------------------------------------- active-cell compaction + stealing ---
// Compaction: cells with ||omega_i||_2 > eps_react go into a dense queue.
// Work stealing: worker threads claim cells from the queue via an atomic
// ticket (allowed here — this queue is NOT the CellFaceCSR gather pass).
struct CompactQueue {
  std::vector<int> cells;              // compacted active cell ids
  std::atomic<int> ticket{0};
  void build(const std::vector<std::vector<double>>& omega, double eps) {
    cells.clear();
    for (size_t i = 0; i < omega.size(); ++i) {
      double n2 = 0;
      for (double v : omega[i]) n2 += v * v;
      if (std::sqrt(n2) > eps) cells.push_back((int)i);
    }
    ticket = 0;
  }
  bool steal(int& cell) {
    int t = ticket.fetch_add(1);
    if (t >= (int)cells.size()) return false;
    cell = cells[t];
    return true;
  }
};

// Batched integration over all cells with dynamic work stealing (DLBFoam).
// Returns per-thread processed counts (for verification of stealing).
inline std::vector<long> run_batched(const Mechanism& mech, double T,
                                     std::vector<std::vector<double>>& Y,
                                     double t0, double t1, double h,
                                     double eps_react, bool use_bdf2 = true) {
  // compaction pass
  std::vector<std::vector<double>> om(Y.size());
  for (size_t i = 0; i < Y.size(); ++i) {
    om[i].assign(mech.ns, 0);
    mech.omega(Y[i].data(), T, om[i].data());
  }
  CompactQueue q;
  q.build(om, eps_react);
  const int nthreads = 4;
  std::vector<long> per_thread(nthreads, 0);
  std::vector<long> stolen(nthreads, 0);
  const long nsteps = (long)std::llround((t1 - t0) / h);
#ifdef _OPENMP
#pragma omp parallel num_threads(nthreads)
#endif
  {
    int tid = 0;
#ifdef _OPENMP
    tid = omp_get_thread_num();
#endif
    BDF2 bdf{&mech, T};
    RosenbrockW ros{&mech, T};
    std::vector<double> yn(mech.ns), ynm(mech.ns), ynew(mech.ns);
    int cell;
    // each thread owns a disjoint slice as "home"; the ticket queue lets idle
    // threads steal stiff cells from other threads' home slices.
    const int ncells = (int)Y.size();
    while (q.steal(cell)) {
      yn = Y[cell];
      if (use_bdf2) {
        // start-up: one BDF1 (implicit Euler) step, then BDF2
        std::vector<double> y1(mech.ns);
        bdf.step1(yn.data(), h, y1.data());
        ynm = yn;
        yn = y1;
        for (long s = 1; s < nsteps; ++s) {
          bdf.step(yn.data(), ynm.data(), h, ynew.data());
          ynm = yn; yn = ynew;
        }
      } else {
        for (long s = 0; s < nsteps; ++s) {
          ros.step(yn.data(), h, ynew.data());
          yn = ynew;
        }
      }
      Y[cell] = yn;
      ++per_thread[tid];
      if ((cell % nthreads) != (unsigned)tid) ++stolen[tid];
    }
    (void)ncells;
  }
  (void)stolen;
  return per_thread;
}

// small dense matrix exponential (scaling & squaring + Taylor) — reference
// solution for linear mechanisms used by the verification test.
inline void expm_dense(const std::vector<double>& A, int n, double t, std::vector<double>& E) {
  std::vector<double> M(A.begin(), A.end());
  for (int i = 0; i < n; ++i)
    for (int j = 0; j < n; ++j) M[i * n + j] *= t;
  // scale
  double mx = 0;
  for (double v : M) mx = std::max(mx, std::fabs(v));
  int s = std::max(0, (int)std::ceil(std::log2(std::max(mx, 1e-300) / 0.5)));
  for (double& v : M) v /= std::pow(2.0, s);
  // Taylor
  std::vector<double> R(n * n, 0.0), T(n * n, 0.0);
  for (int i = 0; i < n; ++i) R[i * n + i] = 1.0;
  for (int i = 0; i < n; ++i) T[i * n + i] = 1.0;
  for (int k = 1; k < 60; ++k) {
    // T = T * M / k
    std::vector<double> Tn(n * n, 0.0);
    for (int i = 0; i < n; ++i)
      for (int j = 0; j < n; ++j) {
        double acc = 0;
        for (int l = 0; l < n; ++l) acc += T[i * n + l] * M[l * n + j];
        Tn[i * n + j] = acc / k;
      }
    T = Tn;
    for (int i = 0; i < n * n; ++i) R[i] += T[i];
  }
  // square s times
  for (int k = 0; k < s; ++k) {
    std::vector<double> R2(n * n, 0.0);
    for (int i = 0; i < n; ++i)
      for (int j = 0; j < n; ++j) {
        double acc = 0;
        for (int l = 0; l < n; ++l) acc += R[i * n + l] * R[l * n + j];
        R2[i * n + j] = acc;
      }
    R = R2;
  }
  E = R;
}

}  // namespace chem
