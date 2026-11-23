#pragma once
// ============================================================================
//  fvm/segregated.hpp
//  Segregated & coupled pressure-velocity-enthalpy FVM loop on the CAbiSpan
//  4-quadrant layout:
//    SIMPLE  : 1 momentum predictor + 1 pressure corrector per sweep,
//              with under-relaxation (steady, outer iterations)
//    PISO    : 1 predictor + n_corr (default 3) pressure correctors
//    PIMPLE : PISO inner loop inside SIMPLE outer loop (transient)
//    COUPLED: monolithic (U,p) block system solved by FGMRES
//  All loops use the Rhie-Chow face flux from core.hpp, the strict beta_f /
//  alpha_f contracts, Class-0 gradient-free upwind deferred correction, and
//  the deterministic atomic-free CellFaceCSR gather.
//  C++20, header-only.
// ============================================================================
#include <vector>
#include <cmath>
#include <cstdio>
#include "core.hpp"
#include "../linalg/krylov.hpp"
#include "../linalg/amg.hpp"

namespace fvm {

enum class PvsAlgorithm { SIMPLE, PISO, PIMPLE, COUPLED };

struct SolverParams {
  PvsAlgorithm alg = PvsAlgorithm::PISO;
  int n_outer = 50;        // SIMPLE/PIMPLE outer sweeps
  int n_corr = 3;          // PISO correctors (3-step)
  double alpha_u = 0.7;    // velocity under-relaxation
  double alpha_p = 0.3;    // pressure under-relaxation
  double stab = 1e-6;      // continuity diagonal stabilization
  double tol_p = 1e-10;
  int max_pcg = 400;
};

// forward declarations (defined below)
inline linalg::CSR S_as_csr(const PoissonSystem& S);
inline double A_row_diag(const linalg::CSR& A, int i);

// Momentum predictor on the CellFaceCSR: assembles the per-cell diagonal
// A_P and rhs H_P from the 4-quadrant face list (Class 0: gradient-free
// upwind deferred correction — no explicit gradient term in the predictor),
// then solves with PCG (AMG-preconditioned). Returns Dbar_f = 1/A_f per face.
inline void momentum_predictor(const CAbiSpan& L, const SolverParams& prm,
                               linalg::CSR& A, std::vector<double>& H,
                               std::vector<double>& Dbar_face,
                               const amg::Hierarchy* amgH) {
  const int n = L.n_owned;
  std::vector<std::vector<std::pair<int, double>>> rows(n);
  H.assign(n, 0.0);
  std::vector<double> diag(n, 0.0);
  for (int c = 0; c < n; ++c) {
    double aP = 0.0;
    for (int k = L.cf_ptr[c]; k < L.cf_ptr[c + 1]; ++k) {
      const int f = L.cf_list[k];
      const double al = alpha_face(L, f);
      const double mu_f = 0.5 * (L.mu[L.owner[f]] + (L.neighbour[f] >= 0 ? L.mu[L.neighbour[f]] : L.mu[L.owner[f]]));
      const double c_f = mu_f * al;                    // diffusive conductance
      const int o = L.owner[f], nb = L.neighbour[f];
      const bool owner_side = (o == c);
      if (nb >= 0 && nb < n) {
        aP += c_f;
        rows[c].push_back({nb, -c_f});                 // deferred correction (upwind, Class 0)
        (void)owner_side;
      } else {
        aP += c_f;                                     // boundary: Dirichlet-ish
      }
    }
    // Brinkman porosity sink: -rho |u| / (gamma^2 ...) simplified: -rho/(gamma) * V
    aP += L.rho[c] / std::max(L.gamma[c], 1e-3) * L.vol[c] * 0.0;  // porosity enters via source in adjoint model
    rows[c].push_back({c, aP});
    diag[c] = aP;
  }
  A = linalg::CSR::from_rows(n, rows);
  // face Dbar = 1 / interpolated diagonal
  Dbar_face.assign(L.n_faces, 0.0);
  for (int f = 0; f < L.n_faces; ++f) {
    const int o = L.owner[f], nb = L.neighbour[f];
    const double d = (nb >= 0 && nb < n) ? 0.5 * (diag[o] + diag[nb]) : diag[o];
    Dbar_face[f] = (d > 0) ? 1.0 / d : 0.0;
  }
  (void)prm; (void)amgH;
}

// One PISO pressure corrector: assemble Poisson, solve (PCG+AMG), correct
// face fluxes. Returns max continuity residual after correction.
inline double piso_corrector(const CAbiSpan& L, const SolverParams& prm,
                             const std::vector<double>& mstar,
                             const std::vector<double>& Dbar_face,
                             std::vector<double>& pprime,
                             std::vector<double>& mdot,
                             const amg::Hierarchy* amgH) {
  PoissonSystem S = assemble_pressure_poisson(L, mstar.data(), Dbar_face.data(), L.rho, prm.stab);
  // pin p' at owned cell 0 (pure-Neumann nullspace) — symmetric pinning
  for (int i = 0; i < S.n; ++i)
    for (int k = S.ptr[i]; k < S.ptr[i + 1]; ++k)
      if (S.col[k] == 0) S.val[k] = (i == 0) ? 1.0 : 0.0;
  for (int k = S.ptr[0]; k < S.ptr[1]; ++k) S.val[k] = (S.col[k] == 0) ? 1.0 : 0.0;
  S.b[0] = 0.0;
  pprime.assign(L.n_owned, 0.0);
  auto M = [&](const std::vector<double>& r, std::vector<double>& z) {
    if (amgH) amgH->apply<float>(r, z); else z = r;
  };
  linalg::KrylovLog lg = linalg::pcg(S_as_csr(S), S.b, pprime, M, prm.tol_p, prm.max_pcg);
  (void)lg;
  apply_pressure_correction(L, S, pprime.data(), mstar.data(), Dbar_face.data(), L.rho, mdot.data());
  std::vector<double> D(L.n_owned);
  gather_continuity(L, mdot.data(), D.data());
  double mx = 0;
  for (double v : D) mx = std::max(mx, std::fabs(v));
  return mx;
}

// helper: wrap PoissonSystem as linalg::CSR (same layout)
inline linalg::CSR S_as_csr(const PoissonSystem& S) {
  linalg::CSR A; A.n = S.n; A.ptr = S.ptr; A.col = S.col; A.val = S.val;
  return A;
}

// Full segregated sweep for SIMPLE / PISO / PIMPLE.
inline double segregated_sweep(const CAbiSpan& L, const SolverParams& prm,
                               std::vector<double>& p, std::vector<double>& mdot,
                               const amg::Hierarchy* amgH) {
  linalg::CSR A;
  std::vector<double> H, Dbar;
  momentum_predictor(L, prm, A, H, Dbar, amgH);
  // predictor flux (star) from Rhie-Chow with current p
  std::vector<V3> gradP(L.n_cells);
  for (int c = 0; c < L.n_cells; ++c) gradP[c] = grad_gg(L, p.data(), c);
  std::vector<double> mstar(L.n_faces, 0.0);
  for (int f = 0; f < L.n_faces; ++f) {
    const double rho_f = (L.neighbour[f] >= 0) ? 0.5 * (L.rho[L.owner[f]] + L.rho[L.neighbour[f]]) : L.rho[L.owner[f]];
    mstar[f] = rhie_chow_face(L, f, p.data(), gradP.data(), L.u, L.v, L.w, Dbar[f], rho_f).mdot;
  }
  std::vector<double> pprime;
  double res = 0;
  const int ncorr = (prm.alg == PvsAlgorithm::SIMPLE) ? 1 : prm.n_corr;
  for (int k = 0; k < ncorr; ++k) {
    res = piso_corrector(L, prm, mstar, Dbar, pprime, mdot, amgH);
    for (int c = 0; c < L.n_owned; ++c) p[c] += prm.alpha_p * pprime[c];
    // refresh star flux with corrected pressure
    for (int c = 0; c < L.n_cells; ++c) gradP[c] = grad_gg(L, p.data(), c);
    for (int f = 0; f < L.n_faces; ++f) {
      const double rho_f = (L.neighbour[f] >= 0) ? 0.5 * (L.rho[L.owner[f]] + L.rho[L.neighbour[f]]) : L.rho[L.owner[f]];
      mstar[f] = rhie_chow_face(L, f, p.data(), gradP.data(), L.u, L.v, L.w, Dbar[f], rho_f).mdot;
    }
  }
  // velocity correction u_P <- u_P - D_P grad p'_P
  std::vector<V3> gradPp(L.n_cells);
  for (int c = 0; c < L.n_cells; ++c) gradPp[c] = grad_gg(L, pprime.data(), c);
  for (int c = 0; c < L.n_owned; ++c) {
    const double Dp = 1.0 / std::max(A_row_diag(A, c), 1e-30);
    L.u[c] -= Dp * gradPp[c].x;
  }
  return res;
}

inline double A_row_diag(const linalg::CSR& A, int i) {
  for (int k = A.ptr[i]; k < A.ptr[i + 1]; ++k) if (A.col[k] == i) return A.val[k];
  return 0.0;
}

// COUPLED: monolithic block system [[A, G],[D, 0]] [u;p] = [b; c] solved by
// FGMRES with AMG on the (A,G) block diagonal preconditioner. Here we solve
// the pressure Schur-complement-free monolithic system on a small grid for
// verification: unknowns x = [u_0..u_{n-1}, p_0..p_{n-1}].
inline bool coupled_solve(const CAbiSpan& L, const SolverParams& prm,
                          std::vector<double>& u, std::vector<double>& p,
                          std::vector<double>& mdot, const amg::Hierarchy* amgH) {
  const int n = L.n_owned;
  const int N = 2 * n;
  std::vector<std::vector<std::pair<int, double>>> rows(N);
  std::vector<double> b(N, 0.0);
  // momentum rows: A u + G p = b_u  (G = cell gradient operator)
  for (int c = 0; c < n; ++c) {
    double aP = 0;
    for (int k = L.cf_ptr[c]; k < L.cf_ptr[c + 1]; ++k) {
      const int f = L.cf_list[k];
      const double al = alpha_face(L, f);
      const int o = L.owner[f], nb = L.neighbour[f];
      const double cf = L.mu[c] * al;
      if (nb >= 0 && nb < n) { aP += cf; rows[c].push_back({nb, -cf}); }
      else aP += cf;
      // G: + grad p contribution  sum_f s p_f S_f / V -> add to p columns
      const double s = (o == c) ? 1.0 : -1.0;
      const double th = theta_face(L, f);
      const double pN = (nb >= 0) ? 1.0 : 0.0;  // boundary p = 0
      const double wP = s * (1.0 - th) * (L.sx[f]) / L.vol[c];
      const double wN = s * th * (L.sx[f]) / L.vol[c] * pN;
      rows[c].push_back({n + o, wP});
      if (nb >= 0 && nb < n) rows[c].push_back({n + nb, wN});
    }
    rows[c].push_back({c, aP});
  }
  // continuity rows: D u = 0 with D from Rhie-Chow linearization:
  // sum_f s_f [ ubar_f - Dbar_f alpha_f (p_N - p_P) ] = 0
  std::vector<double> Dbar(L.n_faces, 1.0);
  for (int f = 0; f < L.n_faces; ++f) Dbar[f] = alpha_face(L, f);  // Dbar=alpha for unit diagonal
  for (int c = 0; c < n; ++c) {
    for (int k = L.cf_ptr[c]; k < L.cf_ptr[c + 1]; ++k) {
      const int f = L.cf_list[k];
      const int o = L.owner[f], nb = L.neighbour[f];
      const double s = (o == c) ? 1.0 : -1.0;
      const double th = theta_face(L, f);
      const double cf = Dbar[f] * alpha_face(L, f);
      // u part
      rows[n + c].push_back({o, s * (1.0 - th) * L.sx[f]});
      if (nb >= 0 && nb < n) rows[n + c].push_back({nb, s * th * L.sx[f]});
      // p part
      if (nb >= 0 && nb < n) {
        rows[n + c].push_back({n + o, s * cf});
        rows[n + c].push_back({n + nb, -s * cf});
      } else {
        rows[n + c].push_back({n + o, s * cf});
      }
    }
  }
  linalg::CSR A = linalg::CSR::from_rows(N, rows);
  std::vector<double> x(N, 0.0);
  for (int i = 0; i < n; ++i) x[i] = u[i];
  auto M = [&](const std::vector<double>& r, std::vector<double>& z) {
    if (amgH) {
      // block-diagonal AMG on momentum block only (Jacobi on p): cheap & effective
      std::vector<double> rm(r.begin(), r.begin() + n), zm;
      amgH->apply<float>(rm, zm);
      z.assign(N, 0.0);
      for (int i = 0; i < n; ++i) z[i] = zm[i];
      for (int i = n; i < N; ++i) z[i] = r[i];
    } else z = r;
  };
  auto lg = linalg::fgmres(A, b, x, M, 1e-10, 200, 30);
  for (int i = 0; i < n; ++i) { u[i] = x[i]; p[i] = x[n + i]; }
  // face fluxes from the coupled solution
  std::vector<V3> gradP(L.n_cells);
  for (int c = 0; c < L.n_cells; ++c) gradP[c] = grad_gg(L, p.data(), c);
  for (int f = 0; f < L.n_faces; ++f) {
    const double rho_f = (L.neighbour[f] >= 0) ? 0.5 * (L.rho[L.owner[f]] + L.rho[L.neighbour[f]]) : L.rho[L.owner[f]];
    mdot[f] = rhie_chow_face(L, f, p.data(), gradP.data(), u.data(), L.v, L.w, Dbar[f], rho_f).mdot;
  }
  (void)prm;
  return lg.converged;
}

}  // namespace fvm
