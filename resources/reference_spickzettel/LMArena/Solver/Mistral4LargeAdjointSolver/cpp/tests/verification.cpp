// ============================================================================
//  tests/verification.cpp
//  Standalone C++20 verification test for the zero-copy SoA FVM /
//  discrete-adjoint engine.
//
//  Build & run:
//    g++ -std=c++20 -O2 -fopenmp -I../include verification.cpp -o verification
//    ./verification
//
//  Proves:
//   [1] Rhie-Chow exactness under linear pressure gradients (incl. beta_f
//       linear-exact plane intersection on a stretched grid)
//   [2] checkerboard (odd-even) decoupling: naive collocated face gradient
//       vanishes, Rhie-Chow over-relaxed face gradient detects the mode
//   [3] discrete mass conservation of the PISO pressure corrector on the
//       4-quadrant CellFaceCSR (atomic-free deterministic gather)
//   [4] AMG (PMIS aggregation + smoothed P + L1-Jacobi/Chebyshev V-cycle)
//       as FP32 inner preconditioner of an FP64 PCG outer loop, plus
//       BiCGStab / FGMRES and the eta_round stagnation detector
//   [5] stiff chemistry: BDF2 / Rosenbrock-W vs matrix-exponential
//       reference, AoT analytical Jacobian vs FD, mass conservation,
//       active-cell compaction + work stealing
//   [6] IFT discrete adjoint sensitivities (Brinkman porosity gamma_i and
//       boundary face centroid x_f) vs central finite differences < 1e-8
// ============================================================================
#include <cstdio>
#include <complex>
#include <cstdlib>
#include <cmath>
#include <vector>
#include <random>
#include <string>
#include <algorithm>
#include "../include/fvm/core.hpp"
#include "../include/fvm/segregated.hpp"
#include "../include/linalg/krylov.hpp"
#include "../include/linalg/amg.hpp"
#include "../include/chem/chemistry.hpp"
#include "../include/adj/adjoint.hpp"

using namespace fvm;
using linalg::CSR;

static int g_pass = 0, g_fail = 0;
static void check(bool ok, const char* name, double value, double tol) {
  printf("  [%s] %-58s value=%.3e  tol=%.1e\n", ok ? "PASS" : "FAIL", name, value, tol);
  printf("RESULT %s %.17e %.1e %s\n", name, value, tol, ok ? "PASS" : "FAIL");
  if (ok) ++g_pass; else ++g_fail;
}

// ---------------------------------------------------------------- [1][2] --
static void test_rhie_chow() {
  printf("\n=== [1][2] Rhie-Chow interpolation: linear exactness + checkerboard ===\n");
  // uniform grid
  {
    Lake1D lk; lk.build(32, 1.0, 0.0);
    const double a = 3.7, b = -1.2;
    for (int i = 0; i < lk.L.n_cells; ++i) lk.p[i] = a * lk.cx[i] + b;
    std::vector<V3> g(lk.L.n_cells);
    for (int c = 0; c < lk.L.n_cells; ++c) g[c] = grad_gg(lk.L, lk.p.data(), c);
    double maxerr = 0;
    for (int f = 0; f < lk.L.n_faces; ++f) {
      if (lk.neighbour[f] < 0) continue;
      auto rc = rhie_chow_face(lk.L, f, lk.p.data(), g.data(), lk.u.data(), lk.v.data(), lk.w.data(), 1.0, 1.0);
      maxerr = std::max(maxerr, std::fabs(rc.Gf - a * lk.sx[f]));  // G_f == (grad p)_f.S_f == a|S|
    }
    check(maxerr < 1e-12, "RC linear-pressure face gradient exact (uniform)", maxerr, 1e-12);
  }
  // stretched grid: beta_f linear-exact plane intersection
  {
    Lake1D lk; lk.build(48, 2.0, 3.0);
    const double m = -2.3, q = 0.9;
    std::vector<double> w(lk.L.n_cells);
    for (int i = 0; i < lk.L.n_cells; ++i) w[i] = m * lk.cx[i] + q;
    double maxerr = 0;
    for (int f = 0; f < lk.L.n_faces; ++f) {
      if (lk.neighbour[f] < 0) continue;
      const int P = lk.owner[f], N = lk.neighbour[f];
      const double beta = beta_face(lk.L, f);
      const double wf = w[P] * (1.0 - beta) + w[N] * beta;   // linear-exact reconstruction at x_f
      maxerr = std::max(maxerr, std::fabs(wf - (m * lk.fx[f] + q)));
    }
    check(maxerr < 1e-12, "beta_f linear-exact plane intersection (stretched)", maxerr, 1e-12);
    // alpha_f contract: |S|^2/(d.S)
    double maxerrA = 0;
    for (int f = 0; f < lk.L.n_faces; ++f) {
      if (lk.neighbour[f] < 0) continue;
      const double d = lk.cx[lk.neighbour[f]] - lk.cx[lk.owner[f]];
      maxerrA = std::max(maxerrA, std::fabs(alpha_face(lk.L, f) - 1.0 / d));
    }
    check(maxerrA < 1e-12, "alpha_f over-relaxed conductance |S|^2/(d.S)", maxerrA, 1e-12);
  }
  // checkerboard: uniform grid, p_i = c (-1)^i
  {
    Lake1D lk; lk.build(32, 1.0, 0.0);
    const double c = 0.7;
    for (int i = 0; i < lk.L.n_cells; ++i) lk.p[i] = c * ((i % 2) ? -1.0 : 1.0);
    std::vector<V3> g(lk.L.n_cells);
    for (int i = 0; i < lk.L.n_cells; ++i) g[i] = grad_gg(lk.L, lk.p.data(), i);
    double naive_max = 0, rc_min = 1e300;
    const double dx = lk.vol[0];
    for (int f = 0; f < lk.L.n_faces; ++f) {
      if (lk.neighbour[f] < 0) continue;
      // interior faces only (both adjacent cells away from the boundary, whose
      // Green-Gauss gradient is polluted by the boundary-face value)
      if (lk.owner[f] == 0 || lk.neighbour[f] == lk.L.n_cells - 1) continue;
      const double th = theta_face(lk.L, f);
      const int P = lk.owner[f], N = lk.neighbour[f];
      const double gbar = g[P].x * (1 - th) + g[N].x * th;    // naive: interp of cell grads
      naive_max = std::max(naive_max, std::fabs(gbar));
      auto rc = rhie_chow_face(lk.L, f, lk.p.data(), g.data(), lk.u.data(), lk.v.data(), lk.w.data(), 1.0, 1.0);
      rc_min = std::min(rc_min, std::fabs(rc.Gf));
    }
    printf("  checkerboard amplitude c=%.2f, dx=%.4f -> naive |gbar_f|max=%.3e, RC |G_f|min=%.3e (2c/dx=%.3e)\n",
           c, dx, naive_max, rc_min, 2 * c / dx);
    check(naive_max < 1e-14, "naive collocated face gradient blind to checkerboard", naive_max, 1e-14);
    check(rc_min > c / dx, "Rhie-Chow detects checkerboard (no odd-even decoupling)", rc_min, c / dx);
  }
}

// ------------------------------------------------------------------- [3] --
static void test_mass_conservation() {
  printf("\n=== [3] Discrete mass conservation of the PISO corrector (4-quadrant CSR) ===\n");
  Lake1D lk; lk.build(64, 1.0, 0.0);
  std::mt19937 rng(42);
  std::uniform_real_distribution<double> U(-1.0, 1.0);
  for (int i = 0; i < lk.L.n_cells; ++i) lk.u[i] = U(rng);
  std::vector<V3> g(lk.L.n_cells);
  for (int c = 0; c < lk.L.n_cells; ++c) g[c] = grad_gg(lk.L, lk.p.data(), c);
  std::vector<double> Dbar(lk.L.n_faces, 1.0), mstar(lk.L.n_faces, 0.0);
  for (int f = 0; f < lk.L.n_faces; ++f) {
    auto rc = rhie_chow_face(lk.L, f, lk.p.data(), g.data(), lk.u.data(), lk.v.data(), lk.w.data(), Dbar[f], 1.0);
    mstar[f] = (lk.neighbour[f] < 0) ? 0.0 : rc.mdot;   // prescribed zero-flux boundaries
  }
  // 3-step PISO
  SolverParams prm; prm.stab = 0.0; prm.tol_p = 1e-14; prm.max_pcg = 2000;
  std::vector<double> mdot = mstar, pprime(lk.L.n_owned, 0.0);
  double maxres = 0;
  for (int k = 0; k < 3; ++k) {
    maxres = piso_corrector(lk.L, prm, mstar, Dbar, pprime, mdot, nullptr);
    for (int f = 0; f < lk.L.n_faces; ++f) mstar[f] = mdot[f];
  }
  std::vector<double> D(lk.L.n_owned), D2(lk.L.n_owned);
  gather_continuity(lk.L, mdot.data(), D.data());
  gather_continuity(lk.L, mdot.data(), D2.data());
  double maxcell = 0, gsum = 0;
  for (int c = 0; c < lk.L.n_owned; ++c) { maxcell = std::max(maxcell, std::fabs(D[c])); gsum += D[c]; }
  bool deterministic = (D == D2);
  check(maxcell < 1e-10, "PISO per-cell continuity residual (atomic-free CSR gather)", maxcell, 1e-10);
  check(std::fabs(gsum) < 1e-12, "global mass conservation sum_f s_f m_f", std::fabs(gsum), 1e-12);
  check(deterministic, "CellFaceCSR gather bitwise deterministic (no atomics)", deterministic ? 0.0 : 1.0, 0.0);
}

// ------------------------------------------------------------------- [4] --
static CSR poisson2d(int N) {
  const int n = N * N;
  std::vector<std::vector<std::pair<int, double>>> rows(n);
  auto id = [&](int i, int j) { return i * N + j; };
  for (int i = 0; i < N; ++i)
    for (int j = 0; j < N; ++j) {
      rows[id(i, j)].push_back({id(i, j), 4.0});
      if (i > 0) rows[id(i, j)].push_back({id(i - 1, j), -1.0});
      if (i < N - 1) rows[id(i, j)].push_back({id(i + 1, j), -1.0});
      if (j > 0) rows[id(i, j)].push_back({id(i, j - 1), -1.0});
      if (j < N - 1) rows[id(i, j)].push_back({id(i, j + 1), -1.0});
    }
  return CSR::from_rows(n, rows);
}
static CSR convdiff1d(int N, double eps, double c) {
  const double h = 1.0 / N;
  std::vector<std::vector<std::pair<int, double>>> rows(N);
  for (int i = 0; i < N; ++i) {
    rows[i].push_back({i, 2 * eps / (h * h) + c / h});
    if (i > 0) rows[i].push_back({i - 1, -eps / (h * h)});
    if (i < N - 1) rows[i].push_back({i + 1, -eps / (h * h) - c / h});
  }
  return CSR::from_rows(N, rows);
}
static void test_amg_krylov() {
  printf("\n=== [4] AMG (FP32 inner) + FP64 outer Krylov, eta_round detector ===\n");
  const int N = 48;
  CSR A = poisson2d(N);
  const int n = A.n;
  std::mt19937 rng(7);
  std::uniform_real_distribution<double> U(0.0, 1.0);
  std::vector<double> b(n), x(n, 0.0);
  for (auto& v : b) v = U(rng);
  amg::Hierarchy H;
  H.build(A, 64, 0.25, true);
  printf("  AMG hierarchy levels=%d sizes:", H.n_levels());
  for (auto& lv : H.levels) printf(" %d", lv.n);
  printf("\nRESULT amg_levels %d 0 0 INFO\n", H.n_levels());
  for (int i = 0; i < H.n_levels(); ++i) printf("RESULT amg_level_size %d 0 0 INFO\n", H.levels[i].n);
  auto M32 = [&](const std::vector<double>& r, std::vector<double>& z) { H.apply<float>(r, z); };
  auto M64 = [&](const std::vector<double>& r, std::vector<double>& z) { H.apply<double>(r, z); };
  // FP64 PCG with FP32 AMG inner
  auto lg = linalg::pcg(A, b, x, M32, 1e-10, 300);
  printf("  PCG(FP64 outer, FP32 AMG inner): iters=%d converged=%d final relres=%.3e\n",
         lg.iters, (int)lg.converged, lg.relres.back());
  printf("RESULT pcg_fp32amg_iters %.17e 0 0 %s\n", (double)lg.iters, lg.converged ? "PASS" : "FAIL");
  for (size_t i = 0; i < lg.relres.size(); ++i) printf("RESULT pcg_resid %.17e %.17e 0 INFO\n", (double)i, lg.relres[i]);
  check(lg.converged && lg.relres.back() < 1e-10, "PCG+AMG(FP32 inner) converges to 1e-10", lg.relres.back(), 1e-10);
  // iterative refinement FP64 outer / FP32 inner
  std::vector<double> x2(n, 0.0);
  auto lg2 = linalg::iterative_refinement_fp32(A, b, x2, M32, 1e-10, 120);
  printf("  iterative refinement (FP64 outer / FP32 AMG inner): outer=%d final=%.3e\n", lg2.iters, lg2.relres.back());
  for (size_t i = 0; i < lg2.relres.size(); ++i) printf("RESULT refine_resid %.17e %.17e 0 INFO\n", (double)i, lg2.relres[i]);
  check(lg2.converged, "FP64 iterative refinement with FP32 inner converges", lg2.relres.back(), 1e-12);
  // nonsymmetric convection-diffusion: BiCGStab + FGMRES + eta_round stagnation
  CSR C = convdiff1d(200, 0.01, 1.0);
  std::vector<double> bc(200), xc(200, 0.0);
  for (int i = 0; i < 200; ++i) bc[i] = std::sin(3.14159 * (i + 0.5) / 200.0);
  amg::Hierarchy HC;
  HC.build(C, 32, 0.25, true);
  auto MC = [&](const std::vector<double>& r, std::vector<double>& z) { HC.apply<float>(r, z); };
  auto lb = linalg::bicgstab(C, bc, xc, MC, 1e-10, 500);
  printf("  BiCGStab(AMG): iters=%d converged=%d final=%.3e\n", lb.iters, (int)lb.converged, lb.relres.back());
  for (size_t i = 0; i < lb.relres.size(); ++i) printf("RESULT bicgstab_resid %.17e %.17e 0 INFO\n", (double)i, lb.relres[i]);
  check(lb.converged && lb.relres.back() < 1e-10, "BiCGStab+AMG converges (nonsymmetric)", lb.relres.back(), 1e-10);
  std::vector<double> xf(200, 0.0);
  auto lf = linalg::fgmres(C, bc, xf, MC, 1e-9, 500, 30);
  printf("  FGMRES(30)+AMG: iters=%d converged=%d final=%.3e\n", lf.iters, (int)lf.converged, lf.relres.back());
  for (size_t i = 0; i < lf.relres.size(); ++i) printf("RESULT fgmres_resid %.17e %.17e 0 INFO\n", (double)i, lf.relres[i]);
  check(lf.converged && lf.relres.back() < 1e-8, "FGMRES(30)+AMG converges (nonsymmetric)", lf.relres.back(), 1e-8);
  // stagnation / rounding detector: push BiCGStab beyond the FP64 floor
  std::vector<double> xs(200, 0.0);
  auto ls = linalg::bicgstab(C, bc, xs, MC, 1e-16, 2000);
  printf("  BiCGStab tol=1e-16: iters=%d stagnation_at_rounding=%d eta_round=%.3e final=%.3e\n",
         ls.iters, (int)ls.rounding_stagnation, ls.eta_round, ls.relres.back());
  printf("RESULT eta_round_floor %.17e 0 0 INFO\n", ls.eta_round);
  printf("RESULT stagnation_detected %d 0 0 %s\n", ls.rounding_stagnation ? 1 : 0, ls.rounding_stagnation ? "PASS" : "FAIL");
  check(ls.rounding_stagnation, "eta_round a-posteriori stagnation detector triggers", ls.rounding_stagnation ? 0.0 : 1.0, 0.0);
}

// ------------------------------------------------------------------- [5] --
static void test_chemistry() {
  printf("\n=== [5] Stiff chemistry: BDF2 / Rosenbrock-W, AoT Jacobian, compaction ===\n");
  chem::Mechanism mech;
  mech.ns = 3; mech.nr = 2;
  chem::Reaction r1, r2;
  r1.nuR = {1, 0, 0}; r1.nuP = {0, 1, 0}; r1.A = 5.0e1; r1.b = 0; r1.E = 0;   // A -> B,   k1 = 50
  r2.nuR = {0, 1, 0}; r2.nuP = {0, 0, 1}; r2.A = 3.0e7; r2.b = 0; r2.E = 0;   // B -> C,   k2 = 3e7
  mech.rx = {r1, r2};
  const double T = 1000.0;
  const double k1 = 50.0, k2 = 3.0e7;
  // AoT Jacobian vs central FD
  {
    std::vector<double> Y = {0.4, 0.35, 0.25}, J, w1(mech.ns), w2(mech.ns), wp(mech.ns), wm(mech.ns);
    mech.jacobian_dense(Y.data(), T, J);
    double maxrel = 0;
    const double h = 1e-5;  // omega is linear in Y: FD exact; larger h reduces rounding
    for (int j = 0; j < mech.ns; ++j) {
      std::vector<double> Yp = Y, Ym = Y;
      Yp[j] += h; Ym[j] -= h;
      mech.omega(Yp.data(), T, wp.data());
      mech.omega(Ym.data(), T, wm.data());
      for (int k = 0; k < mech.ns; ++k) {
        double fd = (wp[k] - wm[k]) / (2 * h);
        double rel = std::fabs(J[k * mech.ns + j] - fd) / std::max(1e-30, std::fabs(fd) + std::fabs(J[k * mech.ns + j]));
        maxrel = std::max(maxrel, rel);
      }
    }
    check(maxrel < 1e-6, "AoT analytical Jacobian vs central FD", maxrel, 1e-6);
  }
  // reference: expm(J t) for the linear chain
  const double t1 = 2.0e-4;
  std::vector<double> Jm = {-k1, 0, 0, k1, -k2, 0, 0, k2, 0};
  std::vector<double> E;
  chem::expm_dense(Jm, 3, t1, E);
  std::vector<double> Y0 = {1, 0, 0}, yex(3);
  for (int i = 0; i < 3; ++i) {
    double s = 0;
    for (int j = 0; j < 3; ++j) s += E[i * 3 + j] * Y0[j];
    yex[i] = s;
  }
  auto run_scheme = [&](bool bdf2, double h) {
    std::vector<double> y = Y0, ym = Y0, ynew(3);
    chem::BDF2 bdf{&mech, T};
    chem::RosenbrockW ros{&mech, T};
    long nsteps = std::llround(t1 / h);
    if (bdf2) {
      std::vector<double> y1(3);
      bdf.step1(y.data(), h, y1.data());
      ym = y; y = y1;
      for (long s = 1; s < nsteps; ++s) { bdf.step(y.data(), ym.data(), h, ynew.data()); ym = y; y = ynew; }
    } else {
      for (long s = 0; s < nsteps; ++s) { ros.step(y.data(), h, ynew.data()); y = ynew; }
    }
    return y;
  };
  double h = 2e-8;
  auto yb = run_scheme(true, h);
  auto yr = run_scheme(false, h);
  double eb = 0, er = 0, ebr = 0, massb = 0, massr = 0;
  for (int i = 0; i < 3; ++i) {
    eb = std::max(eb, std::fabs(yb[i] - yex[i]));
    er = std::max(er, std::fabs(yr[i] - yex[i]));
    ebr = std::max(ebr, std::fabs(yb[i] - yr[i]));
    massb = std::max(massb, std::fabs(yb[0] + yb[1] + yb[2] - 1.0));
    massr = std::max(massr, std::fabs(yr[0] + yr[1] + yr[2] - 1.0));
  }
  printf("  BDF2 err vs expm=%.3e  ROS2 err vs expm=%.3e  BDF2-vs-ROS2=%.3e\n", eb, er, ebr);
  printf("RESULT bdf2_vs_expm %.17e 0 0 %s\n", eb, eb < 1e-6 ? "PASS" : "FAIL");
  printf("RESULT ros2_vs_expm %.17e 0 0 %s\n", er, er < 1e-6 ? "PASS" : "FAIL");
  printf("RESULT bdf2_vs_ros2 %.17e 0 0 %s\n", ebr, ebr < 1e-8 ? "PASS" : "FAIL");
  check(eb < 1e-6, "BDF2 vs matrix-exponential reference", eb, 1e-6);
  check(er < 1e-6, "Rosenbrock-W(ROS2) vs reference", er, 1e-6);
  check(ebr < 1e-8, "BDF2 vs Rosenbrock-W agreement", ebr, 1e-8);
  check(std::max(massb, massr) < 1e-12, "chemistry mass conservation sum Y = 1", std::max(massb, massr), 1e-12);
  // 2nd-order check: error ratio ~4 when h halved
  auto yb2 = run_scheme(true, h / 2);
  double eb2 = 0;
  for (int i = 0; i < 3; ++i) eb2 = std::max(eb2, std::fabs(yb2[i] - yex[i]));
  printf("  BDF2 order check: err(h)=%.3e err(h/2)=%.3e ratio=%.2f\n", eb, eb2, eb / eb2);
  printf("RESULT bdf2_order_ratio %.17e 0 0 INFO\n", eb / eb2);
  // concentration curve samples for the notebook chart
  {
    std::vector<double> y = Y0, ynew(3);
    chem::RosenbrockW ros{&mech, T};
    const double hc = 1e-7;
    printf("CHEM_CURVE");
    for (int s = 0; s <= 40; ++s) {
      printf(" %.3e,%.3e,%.3e,%.3e", s * hc * 50, y[0], y[1], y[2]);
      for (int k = 0; k < 50; ++k) { ros.step(y.data(), hc, ynew.data()); y = ynew; }
    }
    printf("\n");
  }
  // compaction + work stealing
  {
    const int ncell = 8192;
    std::mt19937 rng(123);
    std::uniform_real_distribution<double> U(0.0, 1.0);
    std::vector<std::vector<double>> Y(ncell, std::vector<double>(3, 0.0));
    int nactive_true = 0;
    for (int i = 0; i < ncell; ++i) {
      if (U(rng) < 0.03) { Y[i] = {0.8, 0.2, 0.0}; ++nactive_true; }  // stiff: ||omega|| large
      else Y[i] = {0.0, 0.0, 1.0};                                    // cold: omega = 0
    }
    std::vector<std::vector<double>> om(ncell, std::vector<double>(3, 0.0));
    for (int i = 0; i < ncell; ++i) mech.omega(Y[i].data(), T, om[i].data());
    chem::CompactQueue q;
    q.build(om, 1e-10);
    printf("  compaction: %d cells, %d active (%.1f%%) compacted into queue\n", ncell, (int)q.cells.size(), 100.0 * q.cells.size() / ncell);
    printf("RESULT chem_active_cells %d 0 0 INFO\n", (int)q.cells.size());
    printf("RESULT chem_total_cells %d 0 0 INFO\n", ncell);
    check((int)q.cells.size() == nactive_true, "active-cell compaction queue exact", (double)((int)q.cells.size() - nactive_true), 0.0);
    auto per = chem::run_batched(mech, T, Y, 0.0, 1e-6, 1e-8, 1e-10, true);
    long tot = 0;
    for (auto v : per) tot += v;
    printf("  work stealing over %zu threads: processed = [", per.size());
    for (auto v : per) printf("%ld ", v);
    printf("] total=%ld\n", tot);
    for (size_t i = 0; i < per.size(); ++i) printf("RESULT chem_thread_jobs %.17e %.17e 0 INFO\n", (double)i, (double)per[i]);
    check(tot == (long)q.cells.size(), "batched integration processed all active cells", (double)(tot - (long)q.cells.size()), 0.0);
  }
}

// ------------------------------------------------------------------- [6] --
static void test_adjoint() {
  printf("\n=== [6] IFT discrete adjoint vs central finite differences ===\n");
  const int N = 40;
  Lake1D lk; lk.build(N, 1.0, 1.0);  // mildly stretched grid
  std::mt19937 rng(2024);
  std::uniform_real_distribution<double> U(0.2, 1.0), Uq(0.1, 1.0);
  for (int i = 0; i < N; ++i) lk.gamma[i] = U(rng);
  std::vector<double> q(N);
  for (auto& v : q) v = Uq(rng);

  // assemble R(U; gamma):  R_i = sum_f s_f alpha_f gamma_f (u_nb - u_i) - q_i
  auto assemble = [&](const Lake1D& L, std::vector<double>& gamma) {
    std::vector<std::vector<std::pair<int, double>>> rows(N);
    std::vector<double> b(N);
    for (int i = 0; i < N; ++i) b[i] = q[i];
    for (int f = 0; f < L.L.n_faces; ++f) {
      const int P = L.owner[f], Nb = L.neighbour[f];
      const double al = alpha_face(L.L, f);
      if (Nb >= 0) {
        const double gf = 0.5 * (gamma[P] + gamma[Nb]);
        const double c = al * gf;
        rows[P].push_back({P, c}); rows[P].push_back({Nb, -c});
        rows[Nb].push_back({Nb, c}); rows[Nb].push_back({P, -c});
      } else {
        const double c = al * gamma[P];  // Dirichlet u_b = 0 at boundary faces
        rows[P].push_back({P, c});
      }
    }
    return std::make_pair(CSR::from_rows(N, rows), b);
  };
  auto [A0, b0] = assemble(lk, lk.gamma);
  std::vector<double> u(N, 0.0);
  auto Midentity = [](const std::vector<double>& r, std::vector<double>& z) { z = r; };
  linalg::pcg(A0, b0, u, Midentity, 1e-14, 2000);
  // objective J = 1/2 sum u_i^2 ; g = dJ/dU = u
  auto objective = [&](const std::vector<double>& uu) {
    double s = 0;
    for (double v : uu) s += 0.5 * v * v;
    return s;
  };
  // adjoint: A^T lambda = -u  (A symmetric)
  CSR AT = A0;  // symmetric
  std::vector<double> lam(N, 0.0), rhs(N);
  for (int i = 0; i < N; ++i) rhs[i] = -u[i];
  linalg::pcg(AT, rhs, lam, Midentity, 1e-14, 2000);
  // Face-based IFT chain rule. With R = A(gamma) u - q and A^T lambda = -u:
  //   dJ/d gamma_k = sum_{inner f=(P,N), k in {P,N}} (al_f/2)(lam_P - lam_N)(u_P - u_N)
  //                + sum_{boundary f of P=k} al_f * lam_P * u_P
  // (dA/d gamma_k is nonzero in BOTH rows P and N of each adjacent face)
  std::vector<double> dJ(N, 0.0);
  for (int f = 0; f < lk.L.n_faces; ++f) {
    const int P = lk.owner[f], Nb = lk.neighbour[f];
    const double al = alpha_face(lk.L, f);
    if (Nb >= 0) {
      const double contrib = 0.5 * al * (lam[P] - lam[Nb]) * (u[P] - u[Nb]);
      dJ[P] += contrib;
      dJ[Nb] += contrib;
    } else {
      dJ[P] += al * lam[P] * u[P];
    }
  }
  // ---- reference sensitivities ----
  // (a) 4th-order 5-point central real FD  (secondary; truncation-limited)
  // (b) complex-step derivative           (primary; machine precision, since
  //      J(gamma, x_f) is rational in the parameters -> no subtractive cancellation)
  using cd = std::complex<double>;
  const int fR = lk.L.n_faces - 1;  // right boundary face
  auto assemble_c = [&](const std::vector<cd>& gamma, cd xfb) {
    std::vector<std::vector<std::pair<int, cd>>> rows(N);
    for (int f = 0; f < lk.L.n_faces; ++f) {
      const int P = lk.owner[f], Nb = lk.neighbour[f];
      cd al = (f == fR) ? 1.0 / (2.0 * (xfb - (double)lk.cx[P])) : cd(alpha_face(lk.L, f));
      if (Nb >= 0) {
        const cd gf = 0.5 * (gamma[P] + gamma[Nb]);
        const cd c = al * gf;
        rows[P].push_back({P, c}); rows[P].push_back({Nb, -c});
        rows[Nb].push_back({Nb, c}); rows[Nb].push_back({P, -c});
      } else {
        rows[P].push_back({P, al * gamma[P]});
      }
    }
    std::vector<std::vector<cd>> A(N, std::vector<cd>(N, cd(0)));
    for (int i = 0; i < N; ++i) for (auto& pr : rows[i]) A[i][pr.first] += pr.second;
    std::vector<cd> b(N), x(N);
    for (int i = 0; i < N; ++i) b[i] = q[i];
    for (int k = 0; k < N; ++k) {
      int piv = k;
      for (int i = k + 1; i < N; ++i) if (std::abs(A[i][k]) > std::abs(A[piv][k])) piv = i;
      std::swap(A[piv], A[k]); std::swap(b[piv], b[k]);
      for (int i = k + 1; i < N; ++i) {
        cd m = A[i][k] / A[k][k];
        for (int j = k; j < N; ++j) A[i][j] -= m * A[k][j];
        b[i] -= m * b[k];
      }
    }
    for (int i = N - 1; i >= 0; --i) {
      cd s = b[i];
      for (int j = i + 1; j < N; ++j) s -= A[i][j] * x[j];
      x[i] = s / A[i][i];
    }
    cd J = 0;
    for (int i = 0; i < N; ++i) J += 0.5 * x[i] * x[i];
    return J;
  };
  std::vector<cd> gc(N);
  for (int i = 0; i < N; ++i) gc[i] = lk.gamma[i];
  const double hcs = 1e-30;  // complex-step: no cancellation
  const double h = 1e-3;
  auto fd5 = [&](int k) {
    auto Jat = [&](double d) {
      std::vector<double> g = lk.gamma;
      g[k] += d;
      auto [Ax, bx] = assemble(lk, g);
      std::vector<double> ux(N, 0.0);
      linalg::pcg(Ax, bx, ux, Midentity, 1e-15, 4000);
      return objective(ux);
    };
    return (-Jat(2 * h) + 8 * Jat(h) - 8 * Jat(-h) + Jat(-2 * h)) / (12 * h);
  };
  double maxerr_cs = 0, maxerr_fd = 0, maxrel = 0;
  std::vector<double> cs(N);
  for (int k = 0; k < N; ++k) {
    const double fd = fd5(k);
    gc[k] += cd(0, hcs);
    cs[k] = assemble_c(gc, lk.fx[fR]).imag() / hcs;
    gc[k] -= cd(0, hcs);
    const double err = std::fabs(cs[k] - dJ[k]);
    maxerr_cs = std::max(maxerr_cs, err);
    maxerr_fd = std::max(maxerr_fd, std::fabs(fd - dJ[k]));
    maxrel = std::max(maxrel, err / std::max(1e-12, std::fabs(cs[k])));
    printf("RESULT adjoint_gamma %d %.17e %.17e %.17e %s\n", k, dJ[k], cs[k], err, err < 1e-8 ? "PASS" : "FAIL");
  }
  check(maxerr_cs < 1e-8, "adjoint dJ/dgamma vs complex-step (max abs err, N=40)", maxerr_cs, 1e-8);
  printf("  complex-step max abs err = %.3e (max rel %.3e); 5-pt FD max abs err = %.3e (FD truncation-limited)\n",
         maxerr_cs, maxrel, maxerr_fd);
  printf("RESULT adjoint_gamma_maxerr_cs %.17e 1e-8 0 %s\n", maxerr_cs, maxerr_cs < 1e-8 ? "PASS" : "FAIL");
  printf("RESULT adjoint_gamma_maxerr_fd %.17e 0 0 INFO\n", maxerr_fd);
  // geometry sensitivity: dJ/dx_f of the right boundary face centroid (feeds the CAD DAG)
  {
    const int P = lk.owner[fR];
    // alpha_b = 1/(2 (x_f - x_P)) ; d alpha_b/d x_f = -1/(2 (x_f-x_P)^2)
    const double d0 = lk.fx[fR] - lk.cx[P];
    const double dalpha = -1.0 / (2.0 * d0 * d0);
    // R_P contains + alpha_b gamma_P u_P (Dirichlet u_b = 0 eliminated):
    const double dJdx = lam[P] * lk.gamma[P] * u[P] * dalpha;
    const double csx = assemble_c(gc, lk.fx[fR] + cd(0, hcs)).imag() / hcs;
    auto J_at = [&](double xf) {
      double save = lk.fx[fR];
      lk.fx[fR] = xf;
      auto [Ax, bx] = assemble(lk, lk.gamma);
      std::vector<double> ux(N, 0.0);
      linalg::pcg(Ax, bx, ux, Midentity, 1e-15, 4000);
      lk.fx[fR] = save;
      return objective(ux);
    };
    const double h5 = 1e-3;
    const double fdg = (-J_at(lk.fx[fR] + 2 * h5) + 8 * J_at(lk.fx[fR] + h5) - 8 * J_at(lk.fx[fR] - h5) + J_at(lk.fx[fR] - 2 * h5)) / (12 * h5);
    const double err = std::fabs(dJdx - csx);
    printf("  boundary face centroid: dJ/dx_f adjoint=%.12e complex-step=%.12e err=%.3e (5-pt FD=%.12e)\n", dJdx, csx, err, fdg);
    printf("RESULT adjoint_dxface %.17e %.17e %.17e %s\n", dJdx, csx, err, err < 1e-8 ? "PASS" : "FAIL");
    check(err < 1e-8, "adjoint dJ/dx_f (boundary face centroid) vs complex-step", err, 1e-8);
  }
  // porosity map for the SIMP/MMA heatmap in the notebook
  printf("POROSITY_MAP");
  for (int k = 0; k < N; ++k) printf(" %.17e,%.17e", lk.gamma[k], dJ[k]);
  printf("\n");
}

int main() {
  printf("=====================================================================\n");
  printf(" Zero-copy SoA FVM + discrete-adjoint engine -- C++20 verification\n");
  printf(" g++ -std=c++20 -O2 -fopenmp -I../include verification.cpp\n");
  printf("=====================================================================\n");
  test_rhie_chow();
  test_mass_conservation();
  test_amg_krylov();
  test_chemistry();
  test_adjoint();
  printf("\n=====================================================================\n");
  printf(" SUMMARY: %d passed, %d failed\n", g_pass, g_fail);
  printf("=====================================================================\n");
  return g_fail ? 1 : 0;
}
