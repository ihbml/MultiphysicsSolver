// =============================================================================
// mp_turbulence_cht.hpp  --  Cluster A of the Multiphysics Suite
//
//   * k-omega SST (Menter 2003): F1/F2 blending, cross-diffusion, Bradshaw
//     eddy-viscosity limiter, production limiter 10 beta* rho k omega
//   * Patankar-linearised, unconditionally positive k / omega / nu~ updates
//     (positive production -> RHS, destruction -> diagonal, Newton linearisation
//     of the omega^2 sink giving aPP += 2 beta rho omega_P V)
//   * Spalart-Allmaras one-equation source terms
//   * LES WALE sub-grid viscosity from the traceless squared gradient tensor
//   * Kader all-y+ blended wall functions (u+, T+, omega_P) with a monotone,
//     singularity-free u_tau solve
//   * Harmonic-resistance CHT interface, monolithic 1-D energy matrix and
//     Robin-Robin partitioned, under-relaxed coupling
//   * Anisotropic Darcy-Forchheimer tensors  D = R diag(d) R^T,  F = R diag(f) R^T
//   * 1-D P1 radiation with Marshak boundary conditions (black walls)
//
// Every numerical kernel is written with the MP_HD macro so the same source is
// usable from host C++20 (g++ -std=c++20 -O2) and from CUDA device code (nvcc).
// =============================================================================
#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <vector>

#if defined(__CUDACC__)
#  define MP_HD __host__ __device__
#else
#  define MP_HD
#endif

namespace mp {

using real = double;
using Vec3 = std::array<real, 3>;
using Mat3 = std::array<Vec3, 3>;

// -----------------------------------------------------------------------------
// Model constants
// -----------------------------------------------------------------------------
namespace C {
inline constexpr real betaStar = 0.09;
inline constexpr real beta1 = 0.075;
inline constexpr real beta2 = 0.0828;
inline constexpr real sigmaK1 = 0.85;
inline constexpr real sigmaK2 = 1.0;
inline constexpr real sigmaW1 = 0.5;
inline constexpr real sigmaW2 = 0.856;
inline constexpr real a1 = 0.31;
inline constexpr real kappa = 0.41;
inline constexpr real Elog = 9.8;      // log-law roughness constant
inline constexpr real Prt = 0.85;      // turbulent Prandtl number
inline constexpr real Cw = 0.325;      // WALE constant
inline constexpr real sigmaSB = 5.670374419e-8;

// Spalart-Allmaras
inline constexpr real saCb1 = 0.1355;
inline constexpr real saCb2 = 0.622;
inline constexpr real saSigma = 2.0 / 3.0;
inline constexpr real saCv1 = 7.1;
inline constexpr real saCw2 = 0.3;
inline constexpr real saCw3 = 2.0;
}  // namespace C

// -----------------------------------------------------------------------------
// Small helpers (device-safe)
// -----------------------------------------------------------------------------
MP_HD inline real fminr(real a, real b) { return a < b ? a : b; }
MP_HD inline real fmaxr(real a, real b) { return a > b ? a : b; }
MP_HD inline real sq(real x) { return x * x; }

MP_HD inline real dot3(const Vec3& a, const Vec3& b) {
  return a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
}
MP_HD inline real norm3(const Vec3& a) { return std::sqrt(dot3(a, a)); }

MP_HD inline Mat3 mat3_zero() { return Mat3{{{0, 0, 0}, {0, 0, 0}, {0, 0, 0}}}; }

MP_HD inline Mat3 mat3_mul(const Mat3& A, const Mat3& B) {
  Mat3 R = mat3_zero();
  for (int i = 0; i < 3; ++i)
    for (int j = 0; j < 3; ++j)
      for (int k = 0; k < 3; ++k) R[i][j] += A[i][k] * B[k][j];
  return R;
}

MP_HD inline Mat3 mat3_T(const Mat3& A) {
  Mat3 R = mat3_zero();
  for (int i = 0; i < 3; ++i)
    for (int j = 0; j < 3; ++j) R[i][j] = A[j][i];
  return R;
}

MP_HD inline Vec3 mat3_vec(const Mat3& A, const Vec3& x) {
  Vec3 y{0, 0, 0};
  for (int i = 0; i < 3; ++i)
    for (int j = 0; j < 3; ++j) y[i] += A[i][j] * x[j];
  return y;
}

MP_HD inline Mat3 mat3_add(const Mat3& A, const Mat3& B) {
  Mat3 R = mat3_zero();
  for (int i = 0; i < 3; ++i)
    for (int j = 0; j < 3; ++j) R[i][j] = A[i][j] + B[i][j];
  return R;
}

MP_HD inline Mat3 mat3_scale(const Mat3& A, real s) {
  Mat3 R = mat3_zero();
  for (int i = 0; i < 3; ++i)
    for (int j = 0; j < 3; ++j) R[i][j] = s * A[i][j];
  return R;
}

// Closed-form 3x3 inverse via adjugate (used for implicit 3x3 momentum blocks).
MP_HD inline Mat3 mat3_inverse(const Mat3& m) {
  const real c00 = m[1][1] * m[2][2] - m[1][2] * m[2][1];
  const real c01 = -(m[1][0] * m[2][2] - m[1][2] * m[2][0]);
  const real c02 = m[1][0] * m[2][1] - m[1][1] * m[2][0];
  const real c10 = -(m[0][1] * m[2][2] - m[0][2] * m[2][1]);
  const real c11 = m[0][0] * m[2][2] - m[0][2] * m[2][0];
  const real c12 = -(m[0][0] * m[2][1] - m[0][1] * m[2][0]);
  const real c20 = m[0][1] * m[1][2] - m[0][2] * m[1][1];
  const real c21 = -(m[0][0] * m[1][2] - m[0][2] * m[1][0]);
  const real c22 = m[0][0] * m[1][1] - m[0][1] * m[1][0];
  const real det = m[0][0] * c00 + m[0][1] * c01 + m[0][2] * c02;
  const real id = 1.0 / det;
  return Mat3{{{c00 * id, c10 * id, c20 * id},
                {c01 * id, c11 * id, c21 * id},
                {c02 * id, c12 * id, c22 * id}}};
}

// -----------------------------------------------------------------------------
// 1. k-omega SST (Menter 2003)
// -----------------------------------------------------------------------------

// Cross-diffusion CD_kw = max(2 rho sigma_w2 (1/omega) grad k . grad w, 1e-20)
MP_HD inline real sst_CDkw(real rho, real omega, real gradKdotGradW) {
  return fmaxr(2.0 * rho * C::sigmaW2 * gradKdotGradW / omega, 1e-20);
}

// First blending function: arg1 = min( max(sqrt(k)/(b* w y), 500 nu/(y^2 w)),
//                                       4 rho sigma_w2 k / (CD y^2) )
MP_HD inline real sst_F1(real k, real omega, real rho, real nu, real y, real CDkw) {
  const real sk = std::sqrt(fmaxr(k, 0.0));
  const real t1 = sk / (C::betaStar * omega * y);
  const real t2 = 500.0 * nu / (y * y * omega);
  const real t3 = 4.0 * rho * C::sigmaW2 * fmaxr(k, 0.0) / (CDkw * y * y);
  const real arg1 = fminr(fmaxr(t1, t2), t3);
  const real a4 = arg1 * arg1 * arg1 * arg1;
  return std::tanh(a4);
}

// Second blending function: arg2 = min( max(2 sqrt(k)/(b* w y), 500 nu/(y^2 w)), 100 )
MP_HD inline real sst_F2(real k, real omega, real nu, real y) {
  const real sk = std::sqrt(fmaxr(k, 0.0));
  const real t1 = 2.0 * sk / (C::betaStar * omega * y);
  const real t2 = 500.0 * nu / (y * y * omega);
  const real arg2 = fminr(fmaxr(t1, t2), 100.0);
  return std::tanh(arg2 * arg2);
}

// Blended model coefficients
MP_HD inline real sst_sigmaK(real F1) { return F1 * C::sigmaK1 + (1 - F1) * C::sigmaK2; }
MP_HD inline real sst_sigmaW(real F1) { return F1 * C::sigmaW1 + (1 - F1) * C::sigmaW2; }
MP_HD inline real sst_beta(real F1) { return F1 * C::beta1 + (1 - F1) * C::beta2; }
MP_HD inline real sst_gamma(real F1) {
  const real g1 = C::beta1 / C::betaStar - C::sigmaW1 * C::kappa * C::kappa / std::sqrt(C::betaStar);
  const real g2 = C::beta2 / C::betaStar - C::sigmaW2 * C::kappa * C::kappa / std::sqrt(C::betaStar);
  return F1 * g1 + (1 - F1) * g2;
}

// Bradshaw-limited kinematic eddy viscosity  nu_t = a1 k / max(a1 w, S F2),
// with S = sqrt(2 S_ij S_ij).
MP_HD inline real sst_nut(real k, real omega, real S, real F2) {
  return C::a1 * k / fmaxr(C::a1 * omega, S * F2);
}

// Production with limiter: P_k = min(mu_t S^2, 10 b* rho k w) (density-weighted).
MP_HD inline real sst_production(real rho, real nut, real S2, real k, real omega) {
  return fminr(rho * nut * S2, 10.0 * C::betaStar * rho * k * omega);
}

// Patankar k-equation point update (backward Euler, implicit destruction):
//   (V/dt + b* rho w V) k_new = V/dt k_old + P_k V
MP_HD inline real sst_update_k(real kOld, real omega, real rho, real V, real dt, real Pk) {
  const real kO = fmaxr(kOld, 1e-30);
  const real aP = V / dt + C::betaStar * rho * omega * V;
  const real b = V / dt * kO + Pk * V;
  return b / aP;
}

// Patankar omega-equation point update with Newton linearisation of the
// destruction  -beta rho w^2 V :  -beta rho (2 w_P w_new - w_P^2) V
//   (V/dt + 2 beta rho w_P V) w_new = V/dt w_old + gamma rho S^2 V
//                                     + beta rho w_P^2 V + cross V
MP_HD inline real sst_update_omega(real wOld, real rho, real V, real dt, real beta,
                                   real gamma, real S2, real F1, real gradKdotGradW) {
  const real wO = fmaxr(wOld, 1e-30);
  const real cross = (1.0 - F1) * fmaxr(2.0 * rho * C::sigmaW2 * gradKdotGradW / wO, 0.0) * V;
  const real aP = V / dt + 2.0 * beta * rho * wO * V;
  const real b = V / dt * wO + gamma * rho * S2 * V + beta * rho * wO * wO * V + cross;
  return b / aP;
}

// -----------------------------------------------------------------------------
// 2. Spalart-Allmaras (one-equation) source terms and positive point update
// -----------------------------------------------------------------------------
MP_HD inline real sa_fv1(real chi) {
  const real c3 = chi * chi * chi;
  return c3 / (c3 + C::saCv1 * C::saCv1 * C::saCv1);
}

MP_HD inline real sa_Stilde(real nut, real nu, real Omega, real d) {
  const real chi = nut / nu;
  const real fv1 = sa_fv1(chi);
  const real fv2 = 1.0 - chi / (1.0 + chi * fv1);
  const real S = Omega + nut * fv2 / (C::kappa * C::kappa * d * d);
  return fmaxr(S, 0.3 * Omega);  // Spalart-Shur realisability clip
}

MP_HD inline real sa_fw(real nut, real Stilde, real d) {
  const real r = fminr(nut / (fmaxr(Stilde, 1e-300) * C::kappa * C::kappa * d * d), 10.0);
  const real g = r + C::saCw2 * (r * r * r * r * r * r - r);
  const real c6 = C::saCw3 * C::saCw3 * C::saCw3 * C::saCw3 * C::saCw3 * C::saCw3;
  return g * std::pow((1.0 + c6) / (g * g * g * g * g * g + c6), 1.0 / 6.0);
}

// Patankar SA point update: destruction cw1 fw (nu~/d)^2 nu~ linearised on nu~_P.
MP_HD inline real sa_update(real nutOld, real nu, real Omega, real d, real V, real dt) {
  const real nO = fmaxr(nutOld, 1e-30);
  const real S = sa_Stilde(nO, nu, Omega, d);
  const real cw1 = C::saCb1 / (C::kappa * C::kappa) + (1 + C::saCb2) / C::saSigma;
  const real fw = sa_fw(nO, S, d);
  const real aP = V / dt + cw1 * fw * nO / (d * d) * V;
  const real b = V / dt * nO + C::saCb1 * S * nO * V;
  return b / aP;
}

// -----------------------------------------------------------------------------
// 3. LES WALE sub-grid viscosity
//    g_ij = du_i/dx_j,  g2 = g.g,  S^d = sym(g2) - (1/3) delta tr(g2)
//    nu_sgs = (Cw Delta)^2 (Sd:Sd)^{3/2} / ((S:S)^{5/2} + (Sd:Sd)^{5/4})
// -----------------------------------------------------------------------------
MP_HD inline real wale_nu_sgs(const Mat3& g, real delta) {
  const Mat3 g2 = mat3_mul(g, g);
  const real tr = g2[0][0] + g2[1][1] + g2[2][2];
  Mat3 Sd = mat3_zero();
  Mat3 S = mat3_zero();
  real SdSd = 0, SS = 0;
  for (int i = 0; i < 3; ++i)
    for (int j = 0; j < 3; ++j) {
      Sd[i][j] = 0.5 * (g2[i][j] + g2[j][i]) - (i == j ? tr / 3.0 : 0.0);
      S[i][j] = 0.5 * (g[i][j] + g[j][i]);
      SdSd += Sd[i][j] * Sd[i][j];
      SS += S[i][j] * S[i][j];
    }
  const real num = std::pow(SdSd, 1.5);
  const real den = std::pow(SS, 2.5) + std::pow(SdSd, 1.25);
  if (den <= 1e-300) return 0.0;
  const real cwd = C::Cw * delta;
  return cwd * cwd * num / den;
}

// Filter width from cell volume (isotropic equivalent)
MP_HD inline real les_delta_from_volume(real V) { return std::cbrt(V); }

// -----------------------------------------------------------------------------
// 4. Kader all-y+ blended wall functions
// -----------------------------------------------------------------------------

// Wall-normal distance y_P = (x_P - x_f) . S_f / |S_f|
MP_HD inline real wall_normal_distance(const Vec3& xP, const Vec3& xf, const Vec3& Sf) {
  const Vec3 d{xP[0] - xf[0], xP[1] - xf[1], xP[2] - xf[2]};
  return dot3(d, Sf) / norm3(Sf);
}

// Kader blending parameter  Gamma(y+) = 0.01 y+^4 / (1 + 5 y+)
MP_HD inline real kader_gamma(real yp) {
  return 0.01 * yp * yp * yp * yp / (1.0 + 5.0 * yp);
}

// u+ = exp(-Gamma) y+ + exp(-1/Gamma) (1/kappa) ln(E y+)
MP_HD inline real all_yplus_uplus(real yp) {
  if (yp <= 0.0) return 0.0;
  const real G = kader_gamma(yp);
  const real Gs = fmaxr(G, 1e-300);
  const real ulog = std::log(C::Elog * yp) / C::kappa;
  return std::exp(-G) * yp + std::exp(-1.0 / Gs) * ulog;
}

// Kader temperature profile:  T+ = Pr y+ e^{-G} + (2.12 ln y+ + beta(Pr)) e^{-1/G}
MP_HD inline real kader_beta_T(real Pr) {
  const real t = 3.85 * std::cbrt(Pr) - 1.3;
  return t * t + 2.12 * std::log(Pr);
}

MP_HD inline real all_yplus_Tplus(real yp, real Pr) {
  if (yp <= 0.0) return 0.0;
  const real G = kader_gamma(yp);
  const real Gs = fmaxr(G, 1e-300);
  return Pr * yp * std::exp(-G) + (2.12 * std::log(yp) + kader_beta_T(Pr)) * std::exp(-1.0 / Gs);
}

// Blended wall-vorticity:  omega_P = sqrt(omega_vis^2 + omega_log^2)
//   omega_vis = 6 nu / (beta1 y^2),   omega_log = u_tau / (sqrt(b*) kappa y)
// Finite as y -> 0 (the viscous term dominates), so no division by zero.
MP_HD inline real all_yplus_omega(real y, real nu, real utau) {
  const real yy = fmaxr(y, 1e-300);
  const real wv = 6.0 * nu / (C::beta1 * yy * yy);
  const real wl = utau / (std::sqrt(C::betaStar) * C::kappa * yy);
  return std::sqrt(wv * wv + wl * wl);
}

// Solve  u_tau * u+(u_tau y / nu) = U_P  for u_tau.
// The left side is strictly increasing in u_tau, so bracketing + bisection is
// unconditionally convergent (no Newton overshoot, no singularity at y -> 0).
MP_HD inline real friction_velocity(real Up, real y, real nu) {
  if (Up <= 0.0) return 0.0;
  auto f = [&](real ut) { return ut * all_yplus_uplus(ut * y / nu) - Up; };
  real lo = 0.0;
  real hi = Up;
  for (int it = 0; it < 60 && f(hi) < 0.0; ++it) hi *= 2.0;
  for (int it = 0; it < 200; ++it) {
    const real mid = 0.5 * (lo + hi);
    if (f(mid) < 0.0) lo = mid; else hi = mid;
    if (hi - lo <= 1e-15 * hi) break;
  }
  return 0.5 * (lo + hi);
}

// Wall shear stress   tau_w = rho u_tau^2
MP_HD inline real wall_shear(real rho, real utau) { return rho * utau * utau; }

// Wall heat flux  q_w = rho cp u_tau (T_w - T_P) / T+(y+)  (positive: wall hotter)
MP_HD inline real wall_heat_flux(real rho, real cp, real utau, real dT, real yp, real Pr) {
  const real tp = all_yplus_Tplus(yp, Pr);
  if (tp <= 1e-300) return 0.0;
  return rho * cp * utau * dT / tp;
}

// -----------------------------------------------------------------------------
// 5. Conjugate heat transfer: harmonic-resistance interface
//    conductance  G = (kf af)(ks as) / (kf af + ks as),  alpha = A / distance
// -----------------------------------------------------------------------------
struct CHTFace {
  real kappaF, alphaF, kappaS, alphaS;
};

MP_HD inline real cht_conductance(const CHTFace& f) {
  const real a = f.kappaF * f.alphaF;
  const real b = f.kappaS * f.alphaS;
  return a * b / (a + b);
}

// q = G [ (TS - TP) + gradTf.kcorr_f / alphaF + gradTs.kcorr_s / alphaS ]
MP_HD inline real cht_flux(const CHTFace& f, real TP, real TS, real gradTfDotK, real gradTsDotK) {
  return cht_conductance(f) * ((TS - TP) + gradTfDotK / f.alphaF + gradTsDotK / f.alphaS);
}

// T_interface = (kf af TP + ks as TS + dq_skew) / (kf af + ks as)
MP_HD inline real cht_interface_temperature(const CHTFace& f, real TP, real TS, real dqSkew) {
  const real a = f.kappaF * f.alphaF;
  const real b = f.kappaS * f.alphaS;
  return (a * TP + b * TS + dqSkew) / (a + b);
}

// Thomas algorithm for a tridiagonal system (a: sub, b: diag, c: super, d: rhs)
inline std::vector<real> thomas(std::vector<real> a, std::vector<real> b,
                                std::vector<real> c, std::vector<real> d) {
  const std::size_t n = b.size();
  if (n == 0) return {};
  for (std::size_t i = 1; i < n; ++i) {
    const real m = a[i] / b[i - 1];
    b[i] -= m * c[i - 1];
    d[i] -= m * d[i - 1];
  }
  std::vector<real> x(n);
  x[n - 1] = d[n - 1] / b[n - 1];
  for (std::size_t i = n - 1; i-- > 0;) x[i] = (d[i] - c[i] * x[i + 1]) / b[i];
  return x;
}

struct Layer {
  real L;  // thickness [m]
  real k;  // conductivity [W/m/K]
  int n;   // number of cells
};

// External boundary: convective (h, Tinf). h = 0 -> adiabatic. Use h = 1e30 for Dirichlet.
struct Boundary {
  real h;
  real Tinf;
};

// Half-cell boundary conductance per unit area: 1 / (1/h + dx/(2k))
inline real boundary_conductance(real h, real dx, real k) {
  if (h <= 0.0) return 0.0;
  return 1.0 / (1.0 / h + dx / (2.0 * k));
}

struct CHTSolution {
  std::vector<real> x;  // cell centres
  std::vector<real> T;  // cell temperatures
  real q;               // heat flux entering from the left [W/m^2]
};

// Monolithic energy matrix: all regions assembled into ONE tridiagonal system.
// Inter-region faces use the harmonic-resistance conductance (kappa alpha form).
inline CHTSolution solve_layers(const std::vector<Layer>& layers, Boundary left, Boundary right) {
  std::vector<real> dx, kk, xc;
  real xpos = 0.0;
  for (const auto& L : layers)
    for (int i = 0; i < L.n; ++i) {
      const real h = L.L / L.n;
      dx.push_back(h);
      kk.push_back(L.k);
      xc.push_back(xpos + 0.5 * h);
      xpos += h;
    }
  const std::size_t N = dx.size();
  std::vector<real> a(N, 0.0), b(N, 0.0), c(N, 0.0), d(N, 0.0);
  for (std::size_t i = 0; i + 1 < N; ++i) {
    const real G = 1.0 / (dx[i] / (2.0 * kk[i]) + dx[i + 1] / (2.0 * kk[i + 1]));
    b[i] += G;
    b[i + 1] += G;
    c[i] = -G;
    a[i + 1] = -G;
  }
  const real GL = boundary_conductance(left.h, dx.front(), kk.front());
  const real GR = boundary_conductance(right.h, dx.back(), kk.back());
  b.front() += GL;
  d.front() += GL * left.Tinf;
  b.back() += GR;
  d.back() += GR * right.Tinf;

  CHTSolution sol;
  sol.T = thomas(a, b, c, d);
  sol.x = xc;
  sol.q = GL * (left.Tinf - sol.T.front());
  return sol;
}

// Analytic series-resistance flux for a convective/conductive composite wall.
inline real composite_flux_analytic(const std::vector<Layer>& layers, Boundary left, Boundary right) {
  real R = (left.h > 0 ? 1.0 / left.h : 0.0) + (right.h > 0 ? 1.0 / right.h : 0.0);
  for (const auto& L : layers) R += L.L / L.k;
  return (left.Tinf - right.Tinf) / R;
}

struct PartitionedResult {
  int iterations;
  real interfaceMismatch;  // |T_f,b - T_s,b| at exit
  real Tinterface;
  real q;
  bool converged;
};

// Robin-Robin partitioned coupling with under-relaxation on the Robin data.
//   Fluid sees Robin BC (gamma, Theta_f), solid sees Robin BC (gamma, Theta_s):
//     Theta_f <- T_bs - q_in_s/gamma,     Theta_s <- T_bf + q_out_f/gamma
//   with Theta <- (1-omega) Theta + omega Theta_new at each sweep.
inline PartitionedResult solve_partitioned_robin(const Layer& fluid, const Layer& solid,
                                                 Boundary extL, Boundary extR, real gamma,
                                                 real omega, real tol, int maxIter) {
  const real dxf = fluid.L / fluid.n;
  const real dxs = solid.L / solid.n;
  real Thf = 0.5 * (extL.Tinf + extR.Tinf);  // fluid-side Robin data
  real Ths = Thf;                            // solid-side Robin data
  PartitionedResult out{0, 0, 0, 0, false};
  for (int it = 1; it <= maxIter; ++it) {
    // Fluid sub-problem with Robin BC on the right
    auto F = solve_layers({fluid}, extL, Boundary{gamma, Thf});
    const real Tlast = F.T.back();
    const real GRf = boundary_conductance(gamma, dxf, fluid.k);
    const real qout = GRf * (Tlast - Thf);
    const real Tbf = Tlast - qout * dxf / (2.0 * fluid.k);

    // Solid sub-problem with Robin BC on the left
    auto S = solve_layers({solid}, Boundary{gamma, Ths}, extR);
    const real Tfirst = S.T.front();
    const real GLs = boundary_conductance(gamma, dxs, solid.k);
    const real qin = GLs * (Ths - Tfirst);
    const real Tbs = Tfirst + qin * dxs / (2.0 * solid.k);

    const real mismatch = Tbf - Tbs;
    out.iterations = it;
    out.interfaceMismatch = mismatch;
    out.Tinterface = 0.5 * (Tbf + Tbs);
    out.q = qin;
    if (std::abs(mismatch) <= tol * std::max<real>(1.0, std::abs(Tbs))) {
      out.converged = true;
      break;
    }
    const real ThfNew = Tbs - qin / gamma;
    const real ThsNew = Tbf + qout / gamma;
    Thf = (1.0 - omega) * Thf + omega * ThfNew;
    Ths = (1.0 - omega) * Ths + omega * ThsNew;
  }
  return out;
}

// -----------------------------------------------------------------------------
// 6. Anisotropic Darcy-Forchheimer porous zones
//    -grad p = ( mu D + 1/2 rho |u| F ) u,
//    D = R diag(1/K1,1/K2,1/K3) R^T,   F = R diag(C2_1,C2_2,C2_3) R^T
// -----------------------------------------------------------------------------
MP_HD inline Mat3 rotation_z(real theta) {
  const real c = std::cos(theta), s = std::sin(theta);
  return Mat3{{{c, -s, 0}, {s, c, 0}, {0, 0, 1}}};
}

// R diag(d) R^T  -- the local-to-global tensor transformation
MP_HD inline Mat3 oriented_tensor(const Mat3& R, const Vec3& d) {
  Mat3 T = mat3_zero();
  for (int i = 0; i < 3; ++i)
    for (int j = 0; j < 3; ++j)
      for (int k = 0; k < 3; ++k) T[i][j] += R[i][k] * d[k] * R[j][k];
  return T;
}

struct PorousZone {
  Mat3 D;  // viscous (permeability-inverse) tensor
  Mat3 F;  // inertial (Forchheimer) tensor
};

// Implicit 3x3 sink tensor  mu D + 1/2 rho |u| F  (added to aPP as V * sink)
MP_HD inline Mat3 porous_sink(const PorousZone& z, real mu, real rho, const Vec3& u) {
  const real speed = norm3(u);
  return mat3_add(mat3_scale(z.D, mu), mat3_scale(z.F, 0.5 * rho * speed));
}

// Pressure gradient magnitude vector (-grad p) for a given superficial velocity
MP_HD inline Vec3 porous_grad_p(const PorousZone& z, real mu, real rho, const Vec3& u) {
  return mat3_vec(porous_sink(z, mu, rho, u), u);
}

// Scalar inverse of  a u + b u^2 = G  (b > 0, G >= 0), stable form
inline real forchheimer_velocity(real G, real a, real b) {
  if (G <= 0.0) return 0.0;
  return 2.0 * G / (a + std::sqrt(a * a + 4.0 * b * G));
}

// -----------------------------------------------------------------------------
// 7. P1 radiation (1-D slab, grey absorbing medium, black Marshak walls)
//
//   -div(Gamma grad G) + a G = 4 a sigma T^4,   Gamma = 1/(3a)
//   Marshak (black wall, outward x):  G_w - 2 Gamma dG/dn_in = 4 sigma T_w^4
//
// Finite-volume discretisation: the wall half-cell flux is eliminated exactly
// through the Marshak relation, so the boundary row is an (I + Robin) row.
// -----------------------------------------------------------------------------
inline std::vector<real> solve_p1_slab(const std::vector<real>& Tcell, real L, real a,
                                       real Tw0, real Tw1) {
  const std::size_t N = Tcell.size();
  const real h = L / static_cast<real>(N);
  const real Gam = 1.0 / (3.0 * a);
  const real cR = 4.0 * Gam / h;  // Marshak conductance ratio
  const real sw0 = 4.0 * C::sigmaSB * Tw0 * Tw0 * Tw0 * Tw0;
  const real sw1 = 4.0 * C::sigmaSB * Tw1 * Tw1 * Tw1 * Tw1;
  std::vector<real> A(N, 0.0), B(N, 0.0), Cc(N, 0.0), D(N, 0.0);
  const real inv_h2 = 1.0 / (h * h);
  for (std::size_t i = 0; i < N; ++i) {
    const real s = 4.0 * C::sigmaSB * Tcell[i] * Tcell[i] * Tcell[i] * Tcell[i];
    B[i] = 2.0 * Gam * inv_h2 + a;
    if (i > 0) {
      A[i] = -Gam * inv_h2;
    }
    if (i + 1 < N) {
      Cc[i] = -Gam * inv_h2;
    }
    D[i] = a * s;
  }
  // Left Marshak wall: eliminate G_w -> coefficient on G_0 and RHS source
  {
    const real f = 2.0 * Gam * inv_h2 / (1.0 + cR);
    B[0] = Gam * inv_h2 + f + a;
    D[0] += f * sw0;
  }
  // Right Marshak wall (symmetric)
  {
    const real f = 2.0 * Gam * inv_h2 / (1.0 + cR);
    B[N - 1] = Gam * inv_h2 + f + a;
    D[N - 1] += f * sw1;
  }
  return thomas(A, B, Cc, D);
}

// Continuum solution for uniform gas temperature Tg and Marshak walls at Tw (both sides)
inline real p1_analytic_G(real x, real L, real a, real Tg, real Tw) {
  const real sg = 4.0 * C::sigmaSB * Tg * Tg * Tg * Tg;
  const real sw = 4.0 * C::sigmaSB * Tw * Tw * Tw * Tw;
  const real Gam = 1.0 / (3.0 * a);
  const real lam = std::sqrt(3.0) * a;
  const real A = (sw - sg) / (std::cosh(lam * L / 2.0) + 2.0 * Gam * lam * std::sinh(lam * L / 2.0));
  return sg + A * std::cosh(lam * (x - L / 2.0));
}

}  // namespace mp
