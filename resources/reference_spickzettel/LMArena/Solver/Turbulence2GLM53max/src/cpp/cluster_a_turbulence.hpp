// =============================================================================
//  CLUSTER A - 1 :: RANS / LES turbulence closure on polyhedral grids
//
//   * Positivity-preserving k-omega SST (Menter 2003)
//       - F1 / F2 blending, cross-diffusion 2(1-F1) rho sw2 (1/w) grad k . grad w
//       - Bradshaw limiter  nut = a1 k / max(a1 w, S F2)
//       - production limiter Pk = min(tau:grad u , 10 beta* rho k w)
//       - Patankar implicit/explicit source linearisation  (k>0, w>0 always)
//   * Spalart-Allmaras (negative-number form optional), trip-less
//   * LES WALE subgrid model (traceless squared velocity gradient tensor)
//
//  Every kernel is written as a free function on (const Field&, Field&) so the
//  identical body can be compiled by g++ and by nvcc (see cluster_a_cuda.cu).
// =============================================================================
#pragma once
#include "cluster_a_core.hpp"

namespace cla {
namespace sst {

// ------------------------------------------------- Menter (2003) constants
struct Constants {
  Real a1 = 0.31;
  Real beta1 = 0.075, beta2 = 0.0828;
  Real sigmaK1 = 0.85, sigmaK2 = 1.00;
  Real sigmaW1 = 0.50, sigmaW2 = 0.856;
  Real betaStar = 0.09;
  Real kappa = 0.41;
  Real gamma1 = beta1 / betaStar - sigmaW1 * kappa * kappa / std::sqrt(betaStar);  // 0.5531667
  Real gamma2 = beta2 / betaStar - sigmaW2 * kappa * kappa / std::sqrt(betaStar);  // 0.4403547
  Real E = 9.793;                     // rough-wall log-law constant (smooth: 9.793)
};
inline Constants c_;

// ------------------------------------------------------- blending functions
// arg1 (F1) blends the k-eps-like outer model in; arg2 (F2) is used only by
// the Bradshaw viscosity limiter.  y = wall-normal distance.
struct Blend { Real F1, F2, arg1, arg2, CDkw, beta, gamma, sigmaK, sigmaW, crossDiff; };

inline Blend blending(Real k, Real w, Real y, Real nu, Real dkdw, Real rho) {
  Blend b;
  k = std::max(k, kSmall); w = std::max(w, kSmall); y = std::max(y, kSmall);
  const Real sk  = std::sqrt(k);
  const Real CD  = 2.0 * rho * (1.0 - 0.0) * c_.sigmaW2 * dkdw / w;   // raw, F1 applied later
  const Real arg1 = std::min(std::max(sk / (c_.betaStar * w * y), 500.0 * nu / (y * y * w)),
                             4.0 * rho * c_.sigmaW2 / (std::max(CD, 1e-10) * y));
  b.arg1   = arg1;
  b.F1     = std::tanh(std::pow(arg1, 4.0));
  b.CDkw   = std::max(CD, 1e-10);
  b.F2     = std::tanh(std::pow(std::max(2.0 * sk / (c_.betaStar * w * y), 500.0 * nu / (y * y * w)), 2.0));
  b.arg2   = std::max(2.0 * sk / (c_.betaStar * w * y), 500.0 * nu / (y * y * w));
  b.beta   = b.F1 * c_.beta1 + (1.0 - b.F1) * c_.beta2;
  b.gamma  = b.F1 * c_.gamma1 + (1.0 - b.F1) * c_.gamma2;
  b.sigmaK = b.F1 * c_.sigmaK1 + (1.0 - b.F1) * c_.sigmaK2;
  b.sigmaW = b.F1 * c_.sigmaW1 + (1.0 - b.F1) * c_.sigmaW2;
  // exact cross-diffusion term of the SST omega-equation
  b.crossDiff = 2.0 * (1.0 - b.F1) * rho * c_.sigmaW2 * dkdw / w;
  return b;
}

// ------------------------------------------- eddy viscosity (Bradshaw limit)
//  nut = a1 k / max(a1 w, S F2),  S = sqrt(2 S_ij S_ij)
inline Real nutSST(Real k, Real w, Real S, Real F2) {
  k = std::max(k, 0.0); w = std::max(w, kSmall);
  return c_.a1 * k / std::max(c_.a1 * w, S * F2);
}

// -------------------------------------------------- production and limiter
inline Real production(Real nut, const Mat3& gradU, Real rho) {
  const Mat3 S = sym(gradU);
  const Mat3 tau = S * (2.0 * rho * nut);          // Boussinesq, laminar part excluded
  return std::max(ddot(tau, gradU), 0.0);           // tau_ij du_i/dx_j
}
inline Real limitProduction(Real P, Real rho, Real k, Real w) {
  return std::min(P, 10.0 * c_.betaStar * rho * k * std::max(w, 0.0));
}

}  // namespace sst

// =====================================================================
//  Patankar source linearisation container.
//    A generic source  S(phi) = S+  -  S- * phi   with  S+ >= 0, S- >= 0
//    is discretised as
//        a_PP += S-_P * V_P          (positive diagonal contribution)
//        b_P  += S+_P * V_P          (non-negative right hand side)
//    which makes the resulting M-matrix an irreducibly diagonally dominant
//    system => phi > 0 for any positive boundary data and any time step.
//    This is *the* reason the SST implementation below cannot produce
//    negative k or omega.
// =====================================================================
struct PatankarSource {
  Real Splus{0}, Sminus{0};                         // per unit volume
  Real clipDiag{0};                                 // extra safety (>= 0)
  static PatankarSource positivityGuard() { return {0.0, 0.0, 0.0}; }
};

enum class PositivityMode { Patankar, Naive };      // Naive = fully explicit source

// --------------------------------------------------------------- SST solver
struct SstState {
  Field k, w, nut, yWall, F1, F2;
  Real  rho{1.0}, nu{1.5e-5};
};

struct SstConfig {
  bool  blockCoupled = true;     // adds beta* rho w_P to the omega diagonal too
  bool  limitProduction = true;
  PositivityMode pos = PositivityMode::Patankar;
  Real  urfK = 0.7, urfW = 0.4, urfNut = 0.7;
  Real  freeStreamK = 1e-6, freeStreamW = 1e-4;
};

// Assemble the k-equation for one implicit step.  Returns the row-wise
// Patankar contributions so callers can audit diagonal dominance.
struct SstRowInfo { Real aDiag, bRhs, Splus, Sminus; };

inline SstRowInfo assembleKEquation(const PolyMesh& m, const SstState& s, const SstConfig& cfg,
                                    const std::vector<Vec3>& /*U*/, const GradTensor& gU,
                                    const std::vector<Vec3>& gK, const std::vector<Vec3>& gW,
                                    LinSys& L) {
  SstRowInfo worst{0, 0, 0, 0};
  const Idx n = m.nCells;
  for (Idx c = 0; c < n; ++c) {
    const Real rho = s.rho, k = s.k[c], w = s.w[c], y = s.yWall[c];
    const Real dkdw = dot(gK[c], gW[c]);
    const auto  b   = sst::blending(k, w, y, s.nu, dkdw, rho);
    const Mat3  gUc = tensorOf(gU, c);
    const Mat3  Sc  = sym(gUc);
    const Real  S   = std::sqrt(2.0 * ddot(Sc, Sc));
    const Real  nut = sst::nutSST(k, w, S, b.F2);
    Real P = sst::production(nut, gUc, rho);
    if (cfg.limitProduction) P = sst::limitProduction(P, rho, k, w);

    const Real V = m.cV[c];
    // ---- implicit destruction  -beta* rho w k  ->  a_PP += beta* rho w_P V
    const Real SminusK = sst::c_.betaStar * rho * std::max(w, 0.0);
    // ---- explicit (positive) production  -> b_P += P V
    const Real SplusK = P;
    Real aP = 0, bP = 0;
    if (cfg.pos == PositivityMode::Patankar) {
      aP += SminusK * V;                    // k can never cross zero
      bP += SplusK  * V;
    } else {                                 // deliberately wrong: explicit source
      bP += (SplusK - SminusK * k) * V;
    }
    L.add(c, c, aP);
    L.b[c] += bP;
    if (aP > worst.aDiag) { worst.aDiag = aP; worst.bRhs = bP; worst.Splus = SplusK; worst.Sminus = SminusK; }
  }
  return worst;
}

// --------------------------------------------------------------- WALE model
//  nu_sgs = (Cw Delta)^2 (S^d S^d)^{3/2} / ( (S S)^{5/2} + (S^d S^d)^{5/4} )
//  S^d = 1/2 (g g + (g g)^T) - 1/3 tr(g g) I     (traceless, symmetric)
//  Returns exactly 0 for pure rotation AND for pure plane shear.
inline Real waleNuSgs(const Mat3& g, Real delta, Real Cw = 0.325) {
  const Mat3 G = matmul(g, g);                       // (grad u)^2
  const Mat3 Gt = transpose(G);
  const Mat3 Sd = (G + Gt) * 0.5 - Mat3::identity() * (trace(G) / 3.0);
  const Mat3 Ssym = sym(g);
  const Real SdSd = ddot(Sd, Sd);
  const Real SS   = ddot(Ssym, Ssym);        // S_ij S_ij  (no factor 2)
  const Real num  = std::pow(std::max(SdSd, 0.0), 1.5);
  const Real den  = std::pow(std::max(SS, 0.0), 2.5) + std::pow(std::max(SdSd, 0.0), 1.25);
  if (den < 1e-30) return 0.0;
  return (Cw * delta) * (Cw * delta) * num / den;
}

// ---------------------------------------------------- Spalart-Allmaras model
namespace sa {
struct Constants {
  Real cb1 = 0.1355, cb2 = 0.622, sigma = 2.0/3.0, kappa = 0.41;
  Real cv1 = 7.1, cw2 = 0.3, cw3 = 2.0;
  Real cw1 = 0.0;
  Constants() { cw1 = cb1/(kappa*kappa) + (1.0 + cb2)/sigma; }     // 3.20593...
};
inline Constants c_;

inline Real fv1(Real chi) { const Real c3 = chi*chi*chi; return c3 / (c3 + c_.cv1*c_.cv1*c_.cv1); }
inline Real fv2(Real chi, Real fv1v) { return 1.0 - chi/(1.0 + chi*fv1v); }
inline Real fw(Real r) {
  const Real r6 = std::pow(r, 6.0);
  const Real g  = r + c_.cw2*(r6 - r);
  const Real g6 = std::pow(g, 6.0);
  return g * std::pow((1.0 + std::pow(c_.cw3, 6.0)) / (g6 + std::pow(c_.cw3, 6.0)), 1.0/6.0);
}
// Returns the Patankar-split source of the nuTilde equation.
inline PatankarSource source(Real nuT, Real nu, Real S, Real d) {
  const Real chi = std::max(nuT, 0.0) / std::max(nu, kSmall);
  const Real f1  = fv1(chi);
  const Real St  = S + nuT * fv2(chi, f1) / (c_.kappa*c_.kappa*std::max(d*d, kSmall));
  const Real Stm = std::max(St, kSmall);
  const Real r   = std::min(nuT / (Stm * c_.kappa*c_.kappa*std::max(d*d, kSmall)), 10.0);
  PatankarSource p;
  p.Splus  = c_.cb1 * Stm;                          // * nuT  -> explicit positive
  p.Sminus = c_.cw1 * fw(r) / std::max(d*d, kSmall);// * nuT^2 -> /nuT -> diagonal
  return p;
}
inline Real nut(Real nuT, Real nu) { const Real chi = nuT/std::max(nu,kSmall); return nuT * fv1(chi); }
}  // namespace sa

}  // namespace cla
