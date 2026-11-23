#pragma once
// ============================================================================
//  adj/adjoint.hpp
//  Implicit Function Theorem (IFT) discrete adjoint for the pressure-velocity
//  system, compatible with LLVM Enzyme __enzyme_autodiff on the atomic-free
//  CellFaceCSR gather pass.
//
//  State U solves R(U; theta) = 0. Objective J(U, theta). Then
//     (dR/dU)^T lambda = -(dJ/dU)^T ,   dJ/dtheta = lambda^T (dR/dtheta) + dJ/dtheta
//
//  Sensitivities are computed w.r.t.
//   - cell Brinkman porosity  gamma_i in [0,1]   (MMA topology optimization)
//   - boundary face centroids / area vectors x_f, S_f  (Differentiable CAD DAG:
//     dJ/dtheta_CAD = sum_f (dJ/dx_f)(dx_f/dtheta_CAD) + (dJ/dS_f)(dS_f/dtheta_CAD))
//  C++20, header-only.
// ============================================================================
#include <vector>
#include <cmath>
#include <cstdio>
#include "../linalg/krylov.hpp"

// Enzyme hook: when compiled with Clang + Enzyme, the CSR residual gather is
// differentiated in reverse mode directly.
#if defined(__ENZYME__) || defined(ENZYME_AVAILABLE)
extern "C" {
void* __enzyme_autodiff(void* fn, ...);
}
#define FVM_ENZYME_DIFF(fn, ...) __enzyme_autodiff((void*)(fn), __VA_ARGS__)
#else
#define FVM_ENZYME_DIFF(fn, ...) ((void)0)
#endif

namespace adj {

using linalg::CSR;

struct AdjointResult {
  std::vector<double> lambda;      // adjoint solution
  std::vector<double> dJ_dtheta;   // exact discrete sensitivities
  int iters = 0;
  bool converged = false;
};

// Generic IFT adjoint on a CSR residual Jacobian A = dR/dU (n x n):
//   A^T lambda = -g   with g = dJ/dU
// then dJ/dtheta_k = lambda^T (dR/dtheta_k) + dJ/dtheta_k.
// The transpose system is solved with the same Krylov machinery (BiCGStab on
// A^T, or PCG when A is symmetric), preconditioned by AMG.
inline AdjointResult ifft_adjoint(const CSR& A, const std::vector<double>& g,
                                  const std::vector<std::vector<double>>& dRdtheta,
                                  const std::vector<double>& dJdtheta,
                                  const std::function<void(const std::vector<double>&, std::vector<double>&)>& M,
                                  double tol = 1e-12, int maxit = 500) {
  const int n = A.n;
  // transpose CSR
  CSR AT(n);
  AT.ptr.assign(n + 1, 0);
  for (int k = 0; k < (int)A.col.size(); ++k) AT.ptr[A.col[k] + 1]++;
  for (int i = 0; i < n; ++i) AT.ptr[i + 1] += AT.ptr[i];
  std::vector<int32_t> cur(AT.ptr.begin(), AT.ptr.end() - 1);
  AT.col.resize(A.col.size()); AT.val.resize(A.val.size());
  for (int i = 0; i < n; ++i)
    for (int k = A.ptr[i]; k < A.ptr[i + 1]; ++k) {
      const int j = A.col[k], pos = cur[j]++;
      AT.col[pos] = i; AT.val[pos] = A.val[k];
    }
  std::vector<double> rhs(n);
  for (int i = 0; i < n; ++i) rhs[i] = -g[i];
  AdjointResult res;
  res.lambda.assign(n, 0.0);
  auto log = linalg::bicgstab(AT, rhs, res.lambda, M, tol, maxit);
  res.iters = log.iters;
  res.converged = log.converged;
  // chain rule: dJ/dtheta_k = lambda^T dR/dtheta_k + dJ/dtheta_k
  res.dJ_dtheta.assign(dRdtheta.size(), 0.0);
  for (size_t k = 0; k < dRdtheta.size(); ++k) {
    double s = 0;
    for (int i = 0; i < n; ++i) s += res.lambda[i] * dRdtheta[k][i];
    res.dJ_dtheta[k] = s + (k < dJdtheta.size() ? dJdtheta[k] : 0.0);
  }
  return res;
}

}  // namespace adj
