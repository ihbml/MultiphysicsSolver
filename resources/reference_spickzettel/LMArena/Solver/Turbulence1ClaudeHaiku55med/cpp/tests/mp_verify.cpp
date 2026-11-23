// =============================================================================
// mp_verify.cpp -- analytical / invariant verification of Cluster A kernels
// Build:  g++ -std=c++20 -O2 -Wall -Wextra -pedantic -I../include mp_verify.cpp -o mp_verify
// Run  :  ./mp_verify        (exit code 0 == all checks passed)
// =============================================================================
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <random>
#include <vector>

#include "mp_turbulence_cht.hpp"

using namespace mp;

static constexpr double kPi = 3.14159265358979323846;

static int g_failures = 0;
static int g_total = 0;

static void check(const char* name, bool ok, double value, double tol) {
  ++g_total;
  if (!ok) ++g_failures;
  std::printf("  [%s] %-62s value=% .3e  tol=%.1e\n", ok ? "PASS" : "FAIL", name, value, tol);
}

// ---------------------------------------------------------------- SST / F1-F2
static void test_sst_blending() {
  std::puts("\n== k-omega SST blending & eddy viscosity ==");
  const real nu = 1e-6, y = 1e-5;
  const real omegaVis = 6.0 * nu / (C::beta1 * y * y);  // viscous-sublayer wall omega
  const real k = 1e-8;
  const real CD = sst_CDkw(1.0, omegaVis, 0.0);
  const real F1 = sst_F1(k, omegaVis, 1.0, nu, y, CD);
  const real F2 = sst_F2(k, omegaVis, nu, y);
  check("F1 -> 1 in viscous sublayer (k-omega branch)", F1 > 0.999, F1, 1e-3);
  check("F2 -> 1 in viscous sublayer", F2 > 0.999, F2, 1e-3);

  // Far field: F1 must drop well below 1 for a free-stream-like cell.
  const real yF = 10.0, kF = 1e-4, wF = 1.0;
  const real CDf = sst_CDkw(1.0, wF, 0.0);
  const real F1far = sst_F1(kF, wF, 1.0, 1e-5, yF, CDf);
  check("F1 decays to ~0 in free-stream (ratio-of-scales branch)", F1far < 1e-2, F1far, 1e-2);

  // Bradshaw limiter: nu_t <= k / omega for every shear rate (since a1 k / (a1 w))
  std::mt19937 rng(7);
  std::uniform_real_distribution<real> U(0.01, 10.0);
  bool ok = true;
  real worst = 0.0;
  for (int i = 0; i < 2000; ++i) {
    const real kk = U(rng), ww = U(rng), SS = U(rng) * 100.0, FF2 = U(rng) / 10.0;
    const real nut = sst_nut(kk, ww, SS, std::min(FF2, 1.0));
    const real ratio = nut / (kk / ww);
    worst = std::max(worst, ratio);
    ok = ok && ratio <= 1.0 + 1e-12;
  }
  check("Bradshaw limiter nu_t <= k/omega (2000 random states)", ok, worst, 1.0);
}

// ----------------------------------------------------- Positivity (Patankar)
static void test_positivity() {
  std::puts("\n== Unconditional positivity (Patankar linearisation) ==");
  std::mt19937 rng(11);
  std::uniform_real_distribution<real> U(0.0, 1.0);
  bool okK = true, okW = true;
  real minK = 1e300, minW = 1e300;
  for (int i = 0; i < 5000; ++i) {
    const real kOld = std::pow(10.0, -6.0 + 12.0 * U(rng));
    const real wOld = std::pow(10.0, -3.0 + 9.0 * U(rng));
    const real S2 = std::pow(10.0, 6.0 * U(rng));       // extreme strain rates
    const real dt = std::pow(10.0, 9.0 * U(rng));       // massively stiff steps
    const real rho = 1.0, V = 1.0;
    const real nut = sst_nut(kOld, wOld, std::sqrt(S2), 1.0);
    const real Pk = sst_production(rho, nut, S2, kOld, wOld);
    const real kNew = sst_update_k(kOld, wOld, rho, V, dt, Pk);
    const real wNew = sst_update_omega(wOld, rho, V, dt, C::beta1, sst_gamma(1.0), S2, 1.0, 0.0);
    okK = okK && kNew > 0.0;
    okW = okW && wNew > 0.0;
    minK = std::min(minK, kNew);
    minW = std::min(minW, wNew);
  }
  check("k_new > 0 for 5000 stiff states (dt*b*w up to 1e9)", okK, minK, 0.0);
  check("omega_new > 0 for 5000 stiff states (Newton omega^2 sink)", okW, minW, 0.0);

  // Contrast: explicit Euler on the same destruction term goes negative.
  const real kO = 1.0, wO = 10.0, dtStiff = 2.0;  // dt*b*w = 1.8 > 1
  const real explicitK = kO + dtStiff * (0.0 - C::betaStar * wO * kO);
  check("explicit Euler violates k>0 at dt*b*w = 1.8 (motivates Patankar)",
        explicitK < 0.0, explicitK, 0.0);

  // Patankar steady limit: k -> P/(b* w) when production balances destruction
  // Constant production, fixed omega: the steady state is k* = P / (b* w).
  real k = 1e-3;
  const real w = 5.0, P = 0.5, dt = 0.5;
  for (int n = 0; n < 4000; ++n) k = sst_update_k(k, w, 1.0, 1.0, dt, P);
  const real kStar = P / (C::betaStar * w);
  const real rel = std::abs(k - kStar) / kStar;
  check("Patankar k marches to steady state k* = P/(b* w)", rel < 1e-8, rel, 1e-8);
}

// ------------------------------------------------------------------- WALE
static void test_wale() {
  std::puts("\n== LES WALE sub-grid viscosity ==");
  const Mat3 shear{{{0, 3.0, 0}, {0, 0, 0}, {0, 0, 0}}};
  const real nuShear = wale_nu_sgs(shear, 0.1);
  check("WALE vanishes for pure shear (no Sd, so nu_sgs = 0)", std::abs(nuShear) < 1e-14,
        nuShear, 1e-14);

  const Mat3 strain{{{2.0, 0, 0}, {0, -2.0, 0}, {0, 0, 0}}};
  const real n1 = wale_nu_sgs(strain, 0.1);
  check("WALE positive for pure strain", n1 > 0.0, n1, 0.0);

  // Homogeneity: g -> lambda g scales nu_sgs linearly with lambda.
  Mat3 g{{{0.3, -1.1, 0.4}, {0.8, 0.2, -0.5}, {-0.6, 0.9, -0.5}}};
  const real base = wale_nu_sgs(g, 0.05);
  const real s = 3.7;
  const real scaled = wale_nu_sgs(mat3_scale(g, s), 0.05);
  check("WALE homogeneous of degree 1 in |grad u|", std::abs(scaled / (s * base) - 1.0) < 1e-10,
        scaled / (s * base) - 1.0, 1e-10);

  // Quadratic in Delta
  const real nd = wale_nu_sgs(g, 0.1) / wale_nu_sgs(g, 0.05);
  check("WALE quadratic in filter width Delta", std::abs(nd - 4.0) < 1e-10, nd - 4.0, 1e-10);
}

// ------------------------------------------------------------- SA
static void test_sa() {
  std::puts("\n== Spalart-Allmaras source terms ==");
  const real fv1Big = sa_fv1(1e6);
  check("SA fv1 -> 1 for large chi (fully turbulent)", std::abs(fv1Big - 1.0) < 1e-12,
        fv1Big - 1.0, 1e-12);
  const real nut = sa_update(1e-3, 1e-5, 50.0, 0.01, 1.0, 1e6);
  check("SA Patankar update positive under stiff step", nut > 0.0, nut, 0.0);
}

// ------------------------------------------------------- All-y+ wall functions
static void test_wall_functions() {
  std::puts("\n== All-y+ blended wall functions ==");
  const real kap = C::kappa;
  check("u+(y+ -> 0) = y+ (viscous sublayer)",
        std::abs(all_yplus_uplus(1e-4) - 1e-4) < 1e-8, all_yplus_uplus(1e-4) - 1e-4, 1e-8);

  bool mono = true;
  real prev = 0.0;
  for (int i = 1; i <= 5000; ++i) {
    const real yp = 0.1 + 0.1 * i;  // 0.1 .. 500
    const real up = all_yplus_uplus(yp);
    mono = mono && up > prev;
    prev = up;
  }
  check("u+(y+) strictly monotone on 0.1 <= y+ <= 500", mono, 0.0, 0.0);

  const real yp = 200.0;
  const real logErr = std::abs(all_yplus_uplus(yp) - std::log(C::Elog * yp) / kap);
  check("log-law recovered in fully turbulent region (y+=200, rel.)",
        logErr / std::abs(std::log(C::Elog * yp) / kap) < 1e-3,
        logErr / std::abs(std::log(C::Elog * yp) / kap), 1e-3);

  // Log-layer slope: du+/dln(y+) = 1/kappa
  const real d1 = all_yplus_uplus(150.0), d2 = all_yplus_uplus(300.0);
  const real slope = (d2 - d1) / std::log(2.0);
  check("log-layer slope du+/dln(y+) = 1/kappa (Kader residual, rel.)",
        std::abs(slope - 1.0 / kap) * kap < 5e-3, std::abs(slope - 1.0 / kap) * kap, 5e-3);

  // Friction velocity round trip
  const real nu = 1.5e-5, y = 5e-4, utauTrue = 0.5;
  const real Up = utauTrue * all_yplus_uplus(utauTrue * y / nu);
  const real utau = friction_velocity(Up, y, nu);
  check("u_tau round-trip from U_P (bisection, singularity-free)",
        std::abs(utau - utauTrue) / utauTrue < 1e-10, std::abs(utau - utauTrue) / utauTrue, 1e-10);

  // omega_P finite at y -> 0 and equals the viscous value there
  const real wNear = all_yplus_omega(1e-12, nu, utauTrue);
  const real wVis = 6.0 * nu / (C::beta1 * 1e-24);
  check("omega_P finite as y_P -> 0 (blended, no singularity)",
        std::isfinite(wNear) && std::abs(wNear / wVis - 1.0) < 1e-6, wNear / wVis - 1.0, 1e-6);

  // Wall shear and heat flux consistency: q_w = rho cp u_tau dT / T+
  const real rho = 1.2, cp = 1005.0, Pr = 0.71;
  const real tw = wall_shear(rho, utauTrue);
  check("tau_w = rho u_tau^2", std::abs(tw - rho * utauTrue * utauTrue) < 1e-14,
        std::abs(tw - rho * utauTrue * utauTrue), 1e-14);
  const real qw = wall_heat_flux(rho, cp, utauTrue, 10.0, 60.0, Pr);
  const real qwRef = rho * cp * utauTrue * 10.0 / all_yplus_Tplus(60.0, Pr);
  check("q_w = rho cp u_tau dT / T+ (Kader temperature law)",
        std::abs(qw - qwRef) / std::abs(qwRef) < 1e-12, 0.0, 1e-12);
}

// ----------------------------------------------------------- CHT
static void test_cht() {
  std::puts("\n== Conjugate heat transfer (monolithic & partitioned) ==");
  const std::vector<Layer> layers{{0.02, 0.6, 40}, {0.005, 50.0, 20}};
  const Boundary L{500.0, 350.0}, R{100.0, 300.0};

  const real qAn = composite_flux_analytic(layers, L, R);
  const CHTSolution sol = solve_layers(layers, L, R);
  const real errQ = std::abs(sol.q - qAn) / std::abs(qAn);
  check("monolithic CHT flux == series-resistance analytic (conv-cond-conv)", errQ < 1e-10, errQ,
        1e-10);

  // Interface temperature from harmonic formula equals analytic
  const std::size_t nf = 40;  // fluid cells
  const real dxf = 0.02 / 40.0, dxs = 0.005 / 20.0;
  const CHTFace face{0.6, 2.0 / dxf, 50.0, 2.0 / dxs};
  const real Ti = cht_interface_temperature(face, sol.T[nf - 1], sol.T[nf], 0.0);
  const real TiAn = L.Tinf - sol.q * (1.0 / L.h + 0.02 / 0.6);
  check("interface temperature (kf af TP + ks as TS)/(kf af + ks as) analytic",
        std::abs(Ti - TiAn) < 1e-8, Ti - TiAn, 1e-8);

  // Dirichlet composite: flux equals (T0 - TL)/sum(L/k)
  const Boundary D0{1e30, 400.0}, DL{1e30, 300.0};
  const CHTSolution dir = solve_layers(layers, D0, DL);
  const real qDir = (400.0 - 300.0) / (0.02 / 0.6 + 0.005 / 50.0);
  check("Dirichlet composite wall flux == DeltaT / sum(L/k)",
        std::abs(dir.q - qDir) / qDir < 1e-9, std::abs(dir.q - qDir) / qDir, 1e-9);

  // Partitioned Robin-Robin: converges to the monolithic answer
  const real gamma = 1.0 / (0.5 * (0.02 / 0.6 + 0.005 / 50.0));
  const auto part = solve_partitioned_robin(Layer{0.02, 0.6, 40}, Layer{0.005, 50.0, 20}, L, R,
                                            gamma, 1.0, 1e-12, 500);
  const real errP = std::abs(part.q - sol.q) / std::abs(sol.q);
  std::printf("      (partitioned Robin-Robin: %d sweeps, omega=1.0)\n", part.iterations);
  check("Robin-Robin partitioned flux == monolithic (omega=1)", part.converged && errP < 1e-8, errP,
        1e-8);

  const auto partR = solve_partitioned_robin(Layer{0.02, 0.6, 40}, Layer{0.005, 50.0, 20}, L, R,
                                             gamma, 0.5, 1e-12, 2000);
  const real errR = std::abs(partR.q - sol.q) / std::abs(sol.q);
  std::printf("      (partitioned Robin-Robin: %d sweeps, omega=0.5)\n", partR.iterations);
  check("Robin-Robin under-relaxed (omega=0.5) converges to monolithic",
        partR.converged && errR < 1e-8, errR, 1e-8);
}

// ------------------------------------------------------------- Porous
static void test_porous() {
  std::puts("\n== Anisotropic Darcy-Forchheimer porous zones ==");
  const real mu = 1.8e-5, rho = 1.2;
  const Mat3 I = {{{1, 0, 0}, {0, 1, 0}, {0, 0, 1}}};
  const PorousZone iso{oriented_tensor(I, {1e7, 1e7, 1e7}), oriented_tensor(I, {100, 100, 100})};

  // 1-D Darcy-Forchheimer: dp/dx = mu u / K + rho C2 u^2 / 2
  const Vec3 u{1.0, 0.0, 0.0};
  const real gAn = mu * 1e7 * 1.0 + 0.5 * rho * 100.0 * 1.0;
  const Vec3 gp = porous_grad_p(iso, mu, rho, u);
  check("Darcy-Forchheimer dp/dL matches mu u/K + rho C2 u^2/2",
        std::abs(gp[0] - gAn) / gAn < 1e-12, std::abs(gp[0] - gAn) / gAn, 1e-12);

  const real uBack = forchheimer_velocity(gAn, mu * 1e7, 0.5 * rho * 100.0);
  check("Forchheimer inverse recovers superficial velocity", std::abs(uBack - 1.0) < 1e-12,
        uBack - 1.0, 1e-12);

  // Anisotropic: principal axes rotated by 30 degrees about z
  const Mat3 R = rotation_z(kPi / 6.0);
  const Vec3 dInvK{1e7, 1e6, 1e5}, dC2{100.0, 200.0, 300.0};
  const PorousZone aniso{oriented_tensor(R, dInvK), oriented_tensor(R, dC2)};
  const Vec3 e1{R[0][0], R[1][0], R[2][0]};  // principal axis 1 in global frame
  const Vec3 e2{R[0][1], R[1][1], R[2][1]};  // principal axis 2 in global frame
  const real s = 0.8;
  const Vec3 uAxis{s * e1[0], s * e1[1], s * e1[2]};
  const Vec3 gAxis = porous_grad_p(aniso, mu, rho, uAxis);
  const real speed = s;
  const real gExpect = (mu * 1e7 + 0.5 * rho * speed * 100.0) * s;
  const real projAxis = dot3(gAxis, e1);
  check("anisotropic flow along principal axis 1 uses K1, C2_1",
        std::abs(projAxis - gExpect) / gExpect < 1e-12, std::abs(projAxis - gExpect) / gExpect,
        1e-12);

  const Vec3 uCross{s * e2[0], s * e2[1], s * e2[2]};
  const Vec3 gCross = porous_grad_p(aniso, mu, rho, uCross);
  const real gCrossExp = (mu * 1e6 + 0.5 * rho * speed * 200.0) * s;
  check("anisotropic flow along principal axis 2 uses K2, C2_2",
        std::abs(dot3(gCross, e2) - gCrossExp) / gCrossExp < 1e-12,
        std::abs(dot3(gCross, e2) - gCrossExp) / gCrossExp, 1e-12);

  // Tensor properties: symmetric, trace = sum of principal values
  const Mat3& T = aniso.D;
  real asym = 0.0;
  for (int i = 0; i < 3; ++i)
    for (int j = 0; j < 3; ++j) asym = std::max(asym, std::abs(T[i][j] - T[j][i]));
  check("oriented permeability tensor is symmetric", asym < 1e-6 * 1e7, asym, 1e-6 * 1e7);
  const real tr = T[0][0] + T[1][1] + T[2][2];
  check("tr(R diag(d) R^T) = sum(d)", std::abs(tr - (1e7 + 1e6 + 1e5)) / 1.1e7 < 1e-12,
        tr - 1.11e7, 1e-5);

  // 3x3 implicit momentum block inversion
  const Mat3 A = porous_sink(aniso, mu, rho, uAxis);
  const Mat3 Ai = mat3_inverse(A);
  const Mat3 P = mat3_mul(A, Ai);
  real err = 0.0;
  for (int i = 0; i < 3; ++i)
    for (int j = 0; j < 3; ++j) err = std::max(err, std::abs(P[i][j] - (i == j ? 1.0 : 0.0)));
  check("3x3 implicit sink block inverse satisfies A A^-1 = I", err < 1e-9, err, 1e-9);
}

// ------------------------------------------------------------- P1 radiation
static void test_p1() {
  std::puts("\n== P1 radiation with Marshak walls ==");
  const real a = 1.0, L = 1.0, Tg = 1000.0, Tw = 600.0;
  auto solveN = [&](int N, real Tgas) {
    std::vector<real> T(N, Tgas);
    return solve_p1_slab(T, L, a, Tw, Tw);
  };
  {
    const auto G = solveN(40, Tg);
    const real sG = 4.0 * C::sigmaSB * Tg * Tg * Tg * Tg;
    real eMax = 0.0;
    const real h = L / 40.0;
    for (int i = 0; i < 40; ++i) {
      const real x = (i + 0.5) * h;
      eMax = std::max(eMax, std::abs(G[i] - p1_analytic_G(x, L, a, Tg, Tw)) / sG);
    }
    check("P1 FVM vs analytic cosh solution (N=40, relative to 4 sigma Tg^4)", eMax < 5e-3, eMax,
          5e-3);
  }
  {
    real e40 = 0.0, e80 = 0.0;
    for (int N : {40, 80}) {
      const auto G = solveN(N, Tg);
      const real h = L / N, sG = 4.0 * C::sigmaSB * Tg * Tg * Tg * Tg;
      real e = 0.0;
      for (int i = 0; i < N; ++i)
        e = std::max(e, std::abs(G[i] - p1_analytic_G((i + 0.5) * h, L, a, Tg, Tw)));
      (N == 40 ? e40 : e80) = e / sG;
    }
    const real order = std::log2(e40 / e80);
    check("P1 spatial order of accuracy ~2 (Marshak walls)", order > 1.8 && order < 2.2, order, 0.2);
  }
  {
    // Marshak emissive balance: thin optically-thin gas -> G approaches wall emission
    const auto G = solveN(40, Tg);
    const real sw = 4.0 * C::sigmaSB * Tw * Tw * Tw * Tw;
    check("G bounded between wall and gas emissive power",
          G.front() > std::min(sw, 4.0 * C::sigmaSB * Tg * Tg * Tg * Tg) - 1.0 &&
              G.front() < std::max(sw, 4.0 * C::sigmaSB * Tg * Tg * Tg * Tg) + 1.0,
          G.front(), 0.0);
  }
}

int main() {
  std::puts("Cluster A verification: SST / SA / WALE / All-y+ / CHT / Porous / P1");
  test_sst_blending();
  test_positivity();
  test_wale();
  test_sa();
  test_wall_functions();
  test_cht();
  test_porous();
  test_p1();
  std::printf("\n%d / %d checks passed\n", g_total - g_failures, g_total);
  return g_failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
