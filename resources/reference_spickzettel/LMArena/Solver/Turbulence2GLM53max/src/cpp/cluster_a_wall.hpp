// =============================================================================
//  CLUSTER A - 2 :: continuous All-y+ blended wall treatment
//
//  Wall-normal distance at a boundary face:
//      y_P = ( x_P - x_f ) . S_f / || S_f ||
//  (exact for arbitrary polyhedral wall faces: it is the distance along the
//   face-normal direction from the face centroid to the cell centroid).
//
//  Blending (Menter / Kader style, no singular division at y_P -> 0):
//      w_vis = 6 nu / (beta1 y_P^2)
//      w_log = u_tau / ( sqrt(beta*) kappa y_P )
//      w_P   = sqrt( w_vis^2 + w_log^2 )
//  u+ : viscous  u+ = y+                 log  u+ = 1/kappa ln(E y+)
//       blended u+ = sqrt( u+_visc^2 + u+_log^2 )     (continuous, monotone)
//  T+ : Kader (1981) two-layer exponential blend
//       T+ = Pr y+ exp(-G) + [ (1/(kappa Prt)) ln(E y+) + beta(Pr) ] exp(-1/G)
//       G  = (0.01 y+)^4 / (1 + (0.01 y+)^4)
//       beta(Pr) = (3.85 Pr^(1/3) - 1.3)^2 + 2.12 ln(Pr)
//
//  Reference (verification) law: Spalding (1961) implicit law of the wall.
// =============================================================================
#pragma once
#include "cluster_a_core.hpp"
#include "cluster_a_turbulence.hpp"

namespace cla {
namespace wall {

inline constexpr Real kappaW = 0.41;
inline constexpr Real BW     = 5.20;
inline constexpr Real EW     = 9.793;   // e^(kappa*5.56); kept for reference
inline constexpr Real ElogW  = 8.43341;  // e^(kappa*B), consistent with B = 5.20

// ---------------------------------------------------------- exact y_P of a face
inline Real wallDistance(const PolyMesh& m, Idx f, Idx c) {
  const Vec3 Sf = m.face.S[f];
  const Real a  = norm(Sf);
  if (a < kSmall) return 0.0;
  return dot(m.cC[c] - m.face.C[f], Sf) / a;
}

// ---------------------------------------------------------------- omega blend
struct OmegaWall { Real vis, log, blended, yStar, yPlus; };

inline OmegaWall omegaAllYPlus(Real nu, Real yP, Real uTau, Real k,
                               Real beta1 = 0.075, Real betaStar = 0.09,
                               Real kappa = kappaW) {
  OmegaWall o;
  yP = std::max(yP, 1e-12);
  o.vis = 6.0 * nu / (beta1 * yP * yP);
  o.log = uTau / (std::sqrt(betaStar) * kappa * yP);
  o.blended = std::sqrt(o.vis * o.vis + o.log * o.log);
  o.yStar   = std::sqrt(std::max(k, 0.0)) * yP / nu;          // y*
  o.yPlus   = uTau * yP / nu;
  return o;
}

// ------------------------------------------------------------ velocity profile
inline Real uPlusViscous(Real yPlus) { return yPlus; }
inline Real uPlusLog(Real yPlus, Real kappa = kappaW, Real E = ElogW) {
  return std::log(E * std::max(yPlus, 1e-12)) / kappa;
}
// ---------------------------------------------------------------------------
// Continuous (Kader / Reichardt type) blend used for the *velocity* profile.
//     u+ = y+ exp(-G) + u+_log exp(-1/G) ,   G = (y+ / 11.63)^3
// G = 0 at the wall  -> u+ = y+   (exact viscous sublayer)
// G -> inf in the log layer -> u+ = (1/kappa) ln(E y+)
// Monotone, C-infinity and free of the buffer-layer overshoot that a naive
// sqrt(u_vis^2 + u_log^2) blend produces.
// ---------------------------------------------------------------------------
inline Real blendGamma(Real yPlus, Real yPlusBlend = 11.63, Real power = 3.0) {
  return std::pow(std::max(yPlus, 1e-12) / yPlusBlend, power);
}
inline Real uPlusBlended(Real yPlus, Real kappa = kappaW, Real E = ElogW) {
  const Real G  = blendGamma(yPlus);
  const Real uv = uPlusViscous(yPlus);
  const Real ul = uPlusLog(std::max(yPlus, 1e-12), kappa, E);
  return uv * std::exp(-G) + ul * std::exp(-1.0 / std::max(G, 1e-12));
}

// Kader (1981) thermal law of the wall.
inline Real kaderBetaPr(Real Pr) { return std::pow(3.85 * std::cbrt(Pr) - 1.3, 2.0) + 2.12 * std::log(Pr); }
inline Real TplusKader(Real yPlus, Real Pr, Real Prt = 0.85, Real kappa = kappaW, Real E = ElogW) {
  const Real yp   = std::max(yPlus, 1e-12);
  const Real G    = blendGamma(yp);                                 // 0 -> inf
  const Real tLog = std::log(E * yp) / (kappa * Prt) + kaderBetaPr(Pr);
  return Pr * yp * std::exp(-G) + tLog * std::exp(-1.0 / std::max(G, 1e-12));
}
// ------------------------------------------------- Spalding law (verification)
//   y+ = u+ + e^{-kappa B} [ e^{kappa u+} - 1 - ku+ - (ku+)^2/2 - (ku+)^3/6 ]
inline Real spaldingYPlus(Real uPlus, Real kappa = kappaW, Real B = 5.2) {
  const Real ku = kappa * uPlus, c = std::exp(-kappa * B);
  return uPlus + c * (std::exp(ku) - 1.0 - ku - 0.5 * ku * ku - ku * ku * ku / 6.0);
}
// Inversion by bracketed bisection: y+(u+) is strictly increasing in u+ and
// y+(u+) > u+ for all u+ >= 0, so the root is always bracketed by [0, y+].
inline Real spaldingUPlus(Real yPlus, Real kappa = kappaW, Real B = 5.2) {
  if (yPlus <= 0.0) return 0.0;
  Real lo = 0.0, hi = yPlus;
  for (int i = 0; i < 200; ++i) {
    const Real mid = 0.5 * (lo + hi);
    if (spaldingYPlus(mid, kappa, B) < yPlus) lo = mid; else hi = mid;
    if (hi - lo < 1e-15 * (1.0 + hi)) break;
  }
  return 0.5 * (lo + hi);
}

// -------------------------------------------------------- wall shear stress
// Newton solve of   |u_P| = u_tau * uPlus( rho u_tau y_P / mu )
// Robust, positive, and uses the *blended* profile so it is valid for any y+.
struct WallShear { Real uTau, yPlus, tauW, uPlus, iterations; };

inline WallShear uTauFromUp(Real uP, Real nu, Real yP, Real rho,
                            Real kappa = kappaW, Real E = EW) {
  WallShear w{0, 0, 0, 0, 0};
  yP = std::max(yP, 1e-12);
  uP = std::abs(uP);
  Real uTau = std::max(std::sqrt(uP * nu / yP), 1e-12);      // viscous guess
  if (uP < 1e-30) { w.uTau = 0; w.yPlus = 0; w.tauW = 0; w.uPlus = 0; return w; }
  // f(u) = u * uPlus( u yP / nu ) - uP     (numerically differentiated)
  auto F = [&](Real u) { return u * uPlusBlended(u * yP / nu, kappa, E) - uP; };
  for (int it = 0; it < 100; ++it) {
    w.iterations = it + 1;
    const Real f  = F(uTau);
    const Real h  = 1e-7 * uTau + 1e-300;
    const Real df = (F(uTau + h) - F(uTau - h)) / (2.0 * h);
    if (std::abs(df) < kSmall) break;
    uTau -= std::clamp(f / df, -0.45 * uTau, 0.45 * uTau);
    uTau = std::max(uTau, 1e-14);
    if (std::abs(f) < 1e-13 * (1.0 + uP)) break;
  }
  w.uTau = uTau; w.yPlus = uTau * yP / nu; w.uPlus = uP / uTau;
  w.tauW = rho * uTau * uTau;
  return w;
}

// ----------------------------------------------------------- wall heat flux
//   q_w = rho cp u_tau (T_P - T_w) / T+
struct WallHeat { Real qW, Tplus, htc, yPlus; };
inline WallHeat wallHeatFlux(Real TP, Real Tw, Real rho, Real cp, Real nu, Real alpha,
                             Real uTau, Real yP, Real Prt = 0.85) {
  WallHeat r;
  const Real Pr  = nu / std::max(alpha, kSmall);
  const Real yPm = std::max(yP, 1e-12);
  r.yPlus = uTau * yPm / nu;
  r.Tplus = std::max(TplusKader(r.yPlus, Pr, Prt), 1e-6);
  r.qW    = rho * cp * uTau * (TP - Tw) / r.Tplus;
  r.htc   = rho * cp * uTau / r.Tplus;
  return r;
}

// -------------------------------------------- full boundary-face wall assembly
// Returns everything the momentum / k / omega / energy equations need from a
// wall face so that no equation ever sees a singular 1/y_P term.
struct WallFaceData {
  Real yP, uTau, yPlus, tauW, omegaVis, omegaLog, omegaP, kWall, qW, Tplus, htc, nutWall;
};

inline WallFaceData wallFace(const PolyMesh& m, Idx f, Idx c, Vec3 uP, Real rho, Real nu,
                             Real TP, Real Tw, Real cp, Real alpha, Real kP,
                             Real Prt = 0.85, Real beta1 = 0.075, Real betaStar = 0.09) {
  WallFaceData d{};
  const Real yP = std::max(wallDistance(m, f, c), 1e-14);
  const Vec3 Sf = m.face.S[f];
  const Vec3 n  = unit(Sf);                              // points out of the domain
  const Real uPar = norm(uP - n * dot(uP, n));           // tangential velocity
  const WallShear ws = uTauFromUp(uPar, nu, yP, rho);
  const OmegaWall  ow = omegaAllYPlus(nu, yP, ws.uTau, kP, beta1, betaStar);
  const WallHeat   wh = wallHeatFlux(TP, Tw, rho, cp, nu, alpha, ws.uTau, yP, Prt);

  d.yP = yP; d.uTau = ws.uTau; d.yPlus = ws.yPlus; d.tauW = ws.tauW;
  d.omegaVis = ow.vis; d.omegaLog = ow.log; d.omegaP = ow.blended;
  // Wall-adjacent k.  In the viscous sublayer SST gives nut = k/w and the
  // equilibrium nut ~ nu, hence k_vis = nu * w_vis;  blend continuously:
  //   k_wall = sqrt( k_P^2 + (nu w_vis)^2 )
  const Real kVis = nu * ow.vis;
  d.kWall = std::sqrt(kP * kP + kVis * kVis);
  d.qW = wh.qW; d.Tplus = wh.Tplus; d.htc = wh.htc;
  d.nutWall = 0.0;                                       // nut = 0 at the wall
  return d;
}

}  // namespace wall
}  // namespace cla
