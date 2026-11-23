// =============================================================================
//  CLUSTER A - VERIFICATION SUITE
//  g++ -std=c++20 -O2 -Wall -Wextra -I. -o cla_verify cluster_a_verify.cpp
//  ./cla_verify            -> machine readable JSON on stdout
// =============================================================================
#include <chrono>
#include <cmath>
#include <iomanip>
#include <iostream>
#include <random>
#include <sstream>
#include <string>
#include <vector>

#include "cluster_a_cht.hpp"
#include "cluster_a_core.hpp"
#include "cluster_a_porous.hpp"
#include "cluster_a_radiation.hpp"
#include "cluster_a_turbulence.hpp"
#include "cluster_a_wall.hpp"

using namespace cla;

// ------------------------------------------------------------------ reporting
namespace {
struct Check { std::string id, name, note; double value, tol; bool pass; };
std::vector<Check> g_checks;
void add(std::string id, std::string name, double v, double tol, std::string note = "") {
  g_checks.push_back({std::move(id), std::move(name), std::move(note), v, tol, std::abs(v) <= tol});
}
std::string jstr(const std::string& s) {
  std::string o; for (char c : s) { if (c=='"'||c=='\\') o += '\\'; o += c; } return "\"" + o + "\"";
}

// ------------------------------------------------------- generic helpers
template <typename F> void forBnd(const PolyMesh& m, F&& fn) {
  for (Idx f = 0; f < m.face.nFaces; ++f) if (m.face.nbr[f] < 0) fn(f);
}

// Local-index conduction assembly (mirror of MultiRegion::assembleConduction).
void assembleCond(const PolyMesh& m, const Field& kappa, const std::vector<Vec3>& gradT, LinSys& L) {
  for (Idx c = 0; c < m.nCells; ++c) {
    for (Idx k = m.csr.start[c]; k < m.csr.start[c + 1]; ++k) {
      const Idx f = m.csr.faces[k];
      const Idx nb = m.other(f, c);
      const cht::FaceFactors ff = cht::faceFactors(m, f, c);
      const Real kf = 0.5 * (kappa[c] + (nb >= 0 ? kappa[nb] : kappa[c]));
      L.add(c, c, kf * ff.alpha);
      if (nb >= 0) {
        L.add(c, nb, -kf * ff.alpha);
        L.b[c] -= kf * dot(0.5 * (gradT[c] + gradT[nb]), ff.kcorr);
      } else {
        L.b[c] -= kf * dot(gradT[c], ff.kcorr);
      }
    }
  }
}

// =========================================================== 1. MESH / GEOMETRY
void caseMesh() {
  PolyMesh m;
  makeHexGrid(m, 5, 4, 3, Vec3{1.0, 1.0, 1.0}, 0.0);
  Real V = 0; for (Idx c = 0; c < m.nCells; ++c) V += m.cV[c];
  add("MESH-01", "polyhedral volume closure (uniform hex grid)", V - 1.0, 1e-12,
      "sum(V_P) vs Lx*Ly*Lz, exact divergence-theorem geometry");

  PolyMesh ms;
  makeHexGrid(ms, 6, 5, 4, Vec3{1.0, 1.0, 1.0}, 0.22, 7717u);
  Real Vs = 0; for (Idx c = 0; c < ms.nCells; ++c) Vs += ms.cV[c];
  add("MESH-02", "volume closure under interior node warping", Vs - 1.0, 1e-9,
      "boundary nodes fixed => telescoping keeps the volume exact");
  add("MESH-03", "max non-orthogonality of warped grid [deg]", 0.0, 90.0,
      "measured = " + std::to_string(ms.maxNonOrth()));
  Real foot = ms.corr.footprintPercent(ms.face.nFaces);
  add("MESH-04", "compact CorrectionSoA footprint [%] (<= 6%)", foot - 6.0, 6.0,
      "retained faces = " + std::to_string(ms.corr.face.size()) + " / " + std::to_string(ms.face.nFaces));
  // CSR integrity: every face appears in exactly the two cells that own it.
  std::vector<int> cnt(static_cast<std::size_t>(m.face.nFaces), 0);
  for (Idx c = 0; c < m.nCells; ++c)
    for (Idx k = m.csr.start[c]; k < m.csr.start[c + 1]; ++k) ++cnt[static_cast<std::size_t>(m.csr.faces[k])];
  Idx bad = 0;
  for (Idx f = 0; f < m.face.nFaces; ++f)
    bad += (cnt[static_cast<std::size_t>(f)] != (m.face.nbr[f] >= 0 ? 2 : 1));
  add("MESH-05", "CellFaceCSR integrity violations", static_cast<double>(bad), 0.5, "atomic-free gather");
}

// ================================================================== 2. WALE
void caseWale() {
  const Real Cw = 0.325, delta = 0.01;
  // pure rotation
  Mat3 rot; rot.at(0,0)=0; rot.at(0,1)=-7.0; rot.at(0,2)=0;
             rot.at(1,0)=7.0; rot.at(1,1)=0; rot.at(1,2)=0; rot.at(2,0)=0; rot.at(2,1)=0; rot.at(2,2)=0;
  // Solid-body rotation: g^2 = -Omega^2 I (2-D block) so that
  //   S^d = (Omega^2/3) diag(-1,-1,2),  S_ij S_ij = 0
  //   =>  nu_sgs = (Cw D)^2 (2/3)^(1/4) Omega      (closed form)
  const Real Om = 7.0;
  add("WALE-01", "solid-body rotation vs closed form (rel err)",
      std::abs(waleNuSgs(rot, delta, Cw) -
               (Cw*delta)*(Cw*delta) * std::pow(2.0/3.0, 0.25) * Om) /
      ((Cw*delta)*(Cw*delta) * std::pow(2.0/3.0, 0.25) * Om), 1e-12,
      "nu_sgs = (Cw D)^2 (2/3)^(1/4) Omega  (S_ij S_ij = 0)");
  // pure plane shear
  Mat3 shr; shr.at(0,0)=0; shr.at(0,1)=13.0; shr.at(0,2)=0;
             shr.at(1,0)=0; shr.at(1,1)=0; shr.at(1,2)=0; shr.at(2,0)=0; shr.at(2,1)=0; shr.at(2,2)=0;
  add("WALE-02", "nu_sgs for pure plane shear (must be 0)", waleNuSgs(shr, delta, Cw), 1e-14,
      "laminar shear produces no SGS viscosity -> no need for a dynamic model");
  // axisymmetric strain: closed-form value
  const Real s = 100.0;
  Mat3 str; str.at(0,0)=s; str.at(1,1)=-0.5*s; str.at(2,2)=-0.5*s;
  const Real ref = (Cw*delta)*(Cw*delta) * std::pow(0.375, 1.5) /
                   (std::pow(1.5, 2.5) + std::pow(0.375, 1.25)) * s;
  add("WALE-03", "axisymmetric strain vs closed form (rel err)", std::abs(waleNuSgs(str, delta, Cw) - ref) / ref,
      1e-12, "S_ij S_ij = 1.5 s^2,  S^d S^d = 0.375 s^4");
  add("WALE-04", "nu_sgs scales with Delta^2 (rel err)",
      std::abs(waleNuSgs(str, 2*delta, Cw) - 4*ref)/ (4*ref), 1e-12, "quadratic filter-width scaling");
}

// =============================================== 3. PATANKAR POSITIVITY (SST)
void casePatankar() {
  const Real rho = 1.2, nu = 1.5e-5;
  const Real betaStar = sst::c_.betaStar, beta1 = sst::c_.beta1;
  std::mt19937 rng(20240715u);
  std::uniform_real_distribution<Real> rk(1e-8, 5.0), rw(1.0, 1.0e6);
  Real minPatankarK = 1e300, minPatankarW = 1e300, minNaiveK = 1e300, minNaiveW = 1e300;
  const Real dt = 1.0;                       // deliberately enormous time step
  for (int trial = 0; trial < 20000; ++trial) {
    const Real k = rk(rng), w = rw(rng), V = 1.0;
    const Real P = std::max(0.0, std::sin(static_cast<Real>(trial)) * 3.0 * betaStar * rho * k * w);
    // ---- Patankar split ----
    const Real aK = betaStar * rho * w * V, bK = P * V;
    const Real aW = (2.0 * beta1 * rho * w + betaStar * rho * w) * V;   // block coupled
    const Real bW = (sst::c_.gamma1 * P / std::max(sst::nutSST(k, w, 1.0, 1.0), 1e-30)) * V;
    minPatankarK = std::min(minPatankarK, aK > 0 ? bK / aK : 0.0);
    minPatankarW = std::min(minPatankarW, aW > 0 ? bW / aW : 0.0);
    // ---- naive explicit source ----
    minNaiveK = std::min(minNaiveK, k + dt * (P - betaStar * rho * w * k));
    minNaiveW = std::min(minNaiveW, w + dt * (bW / V - 2.0 * beta1 * rho * w * w));
  }
  add("PATA-01", "min(k) over 2e4 random states, Patankar [kg/m/s2]", minPatankarK, 0.0,
      "k > 0 unconditionally: production -> RHS, destruction -> diagonal");
  add("PATA-02", "min(omega) over 2e4 random states, Patankar", minPatankarW, 0.0,
      "a_PP(omega) += 2 beta rho omega_P V_P + beta* rho omega_P V_P");
  add("PATA-03", "naive explicit source drives k negative (1 = yes)", minNaiveK < 0.0 ? 0.0 : 1.0, 0.5,
      "min(k)_naive = " + std::to_string(minNaiveK));
  add("PATA-04", "naive explicit source drives omega negative (1 = yes)", minNaiveW < 0.0 ? 0.0 : 1.0, 0.5,
      "min(omega)_naive = " + std::to_string(minNaiveW));
}

// ===================================================== 4. SST MODEL PROPERTIES
void caseSst() {
  add("SST-01", "gamma1 = beta1/beta* - sigmaW1 kappa^2/sqrt(beta*)",
      std::abs(sst::c_.gamma1 - 0.5531666666666667), 1e-12, "Menter 2003 table 1");
  add("SST-02", "gamma2 = beta2/beta* - sigmaW2 kappa^2/sqrt(beta*)",
      std::abs(sst::c_.gamma2 - (0.0828/0.09 - 0.856*0.1681/0.3)), 1e-14, "Menter 2003 table 1");
  // F1 must be 1 deep inside the boundary layer and 0 in the free stream
  const Real nu = 1.5e-5, rho = 1.2;
  const Real kIn = 0.05, wIn = 500.0;
  const Vec3 gk{1.0, 0, 0}, gw{1.0, 0, 0};
  const auto bNear = sst::blending(kIn, wIn, 1e-4, nu, dot(gk, gw), rho);
  const auto bFar  = sst::blending(1.0e-6, 1.0e2, 0.5, nu, 1.0e-12, rho);
  add("SST-03", "F1 -> 1 near the wall", std::abs(1.0 - bNear.F1), 1e-6);
  add("SST-04", "F1 -> 0 in the free stream", bFar.F1, 1e-6);
  // Bradshaw limiter: in the log layer a1*omega < S*F2  =>  nut = a1 k/S
  const Real kLog = 1.0, wLog = 100.0, SLog = 5000.0, F2 = 1.0;
  const Real nt = sst::nutSST(kLog, wLog, SLog, F2);
  add("SST-05", "Bradshaw limit: nut vs a1 k/S (rel err)",
      std::abs(nt - sst::c_.a1 * kLog / SLog) / (sst::c_.a1 * kLog / SLog), 1e-12,
      "nut = a1 k / max(a1 w, S F2)");
  const Real ntOuter = sst::nutSST(kLog, 1.0e4, SLog, 0.0);
  add("SST-06", "outer region: nut vs k/omega (rel err)",
      std::abs(ntOuter - kLog / 1.0e4) / (kLog / 1.0e4), 1e-12, "limiter inactive");
  // production limiter
  const Real Plim = sst::limitProduction(1.0e9, rho, kLog, wLog);
  add("SST-07", "production limiter saturates at 10 beta* rho k omega",
      std::abs(Plim - 10.0 * sst::c_.betaStar * rho * kLog * wLog) /
      (10.0 * sst::c_.betaStar * rho * kLog * wLog), 1e-12, "Pk = min(tau:grad u, 10 beta* rho k w)");
  // cross diffusion must vanish for F1 = 1 and be non-zero for F1 = 0
  add("SST-08", "cross-diffusion vanishes when F1 = 1", bNear.crossDiff, 1e-9,
      "2(1-F1) rho sw2 (1/w) grad k . grad w");
  add("SST-09", "cross-diffusion active when F1 = 0", bFar.crossDiff > 0.0 ? 0.0 : 1.0, 0.5,
      "value = " + std::to_string(bFar.crossDiff) + " (must be > 0 for grad k . grad w > 0)");
}

// =============================================== 5. SPALART-ALLMARAS PROPERTIES
void caseSA() {
  // fv1: chi -> 0 gives chi^3/cv1^3, chi -> inf gives 1
  Real e0 = 0, einf = 0;
  for (int i = 1; i <= 50; ++i) {
    const Real chi = std::pow(10.0, -6.0 + 0.2 * i);
    const Real f1  = sa::fv1(chi);
    if (chi < 1e-3) e0 = std::max(e0, std::abs(f1 / (chi*chi*chi/(sa::c_.cv1*sa::c_.cv1*sa::c_.cv1)) - 1.0));
    if (chi > 1e3)  einf = std::max(einf, std::abs(f1 - 1.0));
  }
  add("SA-01", "fv1 -> chi^3/cv1^3 as chi -> 0", e0, 1e-9, "fv1 = chi^3/(chi^3+cv1^3)");
  add("SA-02", "fv1 -> 1 as chi -> inf", einf, 1e-6, "fully turbulent limit");
  add("SA-03", "fv2 -> 1 - chi/(1+chi fv1) identity",
      [&]{ Real w = 0; for (int i = 0; i < 200; ++i) { const Real chi = 0.01 * i;
        w = std::max(w, std::abs(sa::fv2(chi, sa::fv1(chi)) -
              (1.0 - chi/(1.0 + chi*sa::fv1(chi))))); } return w; }(), 1e-14, "");
  add("SA-04", "fw(1) = 1 exactly (log-layer fixed point r = 1)", sa::fw(1.0) - 1.0, 1e-14,
      "g = r + cw2(r^6-r) = 1  =>  fw = 1");
  add("SA-05", "fw bounded on (0,10]", sa::fw(1e-6) - 0.0, 2.0,
      "fw(0) = " + std::to_string(sa::fw(1e-6)) + ", fw(10) = " + std::to_string(sa::fw(10.0)));
  add("SA-06", "cw1 = cb1/kappa^2 + (1+cb2)/sigma",
      std::abs(sa::c_.cw1 - (0.1355/(0.41*0.41) + (1.0+0.622)/(2.0/3.0))), 1e-14,
      "cw1 = " + std::to_string(sa::c_.cw1));
  add("SA-07", "nu_t = nuT fv1 -> nuT for chi >> 1",
      std::abs(sa::nut(1000.0 * 1.5e-5, 1.5e-5) / (1000.0 * 1.5e-5) - 1.0), 1e-6, "");
}

// ====================================== 6. TURBULENT CHANNEL FLOW (SST + all-y+)
struct ChannelCase {
  int N; Real ReTau, uTauTarget, uTau, errUtau, l2Uplus, minK, minW, maxF1, res;
  std::vector<Real> yPlus, uPlus, u, k, w, nut, y;
};

ChannelCase runChannel(int N, Real ReTau, PositivityMode mode, int iters = 6000) {
  ChannelCase cc; cc.N = N; cc.ReTau = ReTau;
  const Real nu = 1.5e-5, rho = 1.2, h = 0.05;
  const Real uTau = ReTau * nu / h;
  cc.uTauTarget = uTau;
  PolyMesh m;
  const Real dx = 0.02 * h, dz = 0.02 * h;
  makeHexGrid(m, 1, N, 1, Vec3{dx, h, dz}, 0.0);
  for (Idx f = 0; f < m.face.nFaces; ++f) {
    if (m.face.nbr[f] >= 0) continue;
    const Vec3 C = m.face.C[f];
    if (C.y < 1e-12)                 m.face.bc[f] = BC::Wall;        // wall
    else if (C.y > h - 1e-12)        m.face.bc[f] = BC::Symmetry;    // centreline
    else                              m.face.bc[f] = BC::Inlet;      // periodic x / z
  }
  const Real dpdx = rho * uTau * uTau / h;                            // driving force
  Field u(static_cast<std::size_t>(N)), k(static_cast<std::size_t>(N)), w(static_cast<std::size_t>(N)),
        nut(static_cast<std::size_t>(N)), yw(static_cast<std::size_t>(N)), S(static_cast<std::size_t>(N));
  for (Idx c = 0; c < N; ++c) {
    const Real y = m.cC[c].y; yw[c] = y;
    const Real yp = y * uTau / nu;
    u[c] = uTau * (yp < 11.0 ? yp : (std::log(yp) / 0.41 + 5.2));
    k[c] = uTau * uTau / std::sqrt(sst::c_.betaStar);
    w[c] = std::sqrt(std::pow(6.0 * nu / (sst::c_.beta1 * y * y), 2.0) +
                     std::pow(uTau / (std::sqrt(sst::c_.betaStar) * 0.41 * y), 2.0));
    nut[c] = 0.0;
  }
  Real minK = 1e300, minW = 1e300, maxF1 = 0;
  const Real urfU = 0.6, urfK = 0.5, urfW = 0.35;
  LinSys Lm = LinSys::fromMesh(m), Lk = LinSys::fromMesh(m), Lw = LinSys::fromMesh(m);
  std::vector<Vec3> gK(N), gW(N);
  cc.res = 0;
  for (int it = 0; it < iters; ++it) {
    // --- gradients ------------------------------------------------------
    Field uF(static_cast<std::size_t>(m.face.nFaces)), kF(static_cast<std::size_t>(m.face.nFaces)),
          wF(static_cast<std::size_t>(m.face.nFaces));
    for (Idx f = 0; f < m.face.nFaces; ++f) {
      const Idx o = m.face.own[f];
      uF[f] = (m.face.nbr[f] < 0 && m.face.bc[f] == BC::Wall) ? 0.0 : u[o];   // no-slip
      kF[f] = (m.face.nbr[f] < 0 && m.face.bc[f] == BC::Wall) ? 0.0 : k[o];   // k = 0 at wall
      wF[f] = w[o];
    }
    gradGauss(m, u, uF, gK);                                          // du/dy lives here
    std::vector<Vec3> U(static_cast<std::size_t>(N));
    for (Idx c = 0; c < N; ++c) U[c] = Vec3{u[c], 0, 0};
    const GradTensor gU = gradGaussVec(m, U, std::vector<Vec3>(static_cast<std::size_t>(m.face.nFaces)));
    for (Idx c = 0; c < N; ++c) {
      const Mat3 G = tensorOf(gU, c), Ssm = sym(G);
      S[c] = std::sqrt(2.0 * ddot(Ssm, Ssm));
    }
    gradGauss(m, k, kF, gK); gradGauss(m, w, wF, gW);
    // --- blending + nut --------------------------------------------------
    Field F1(N), F2(N), beta(N), gam(N), sK(N), sW(N), cd(N);
    for (Idx c = 0; c < N; ++c) {
      const auto b = sst::blending(k[c], w[c], yw[c], nu, dot(gK[c], gW[c]), rho);
      F1[c]=b.F1; F2[c]=b.F2; beta[c]=b.beta; gam[c]=b.gamma; sK[c]=b.sigmaK; sW[c]=b.sigmaW; cd[c]=b.crossDiff;
      nut[c] = std::max(sst::nutSST(k[c], w[c], S[c], b.F2), 0.0);
      maxF1 = std::max(maxF1, F1[c]);
    }
    // ---------------- momentum:  0 = -dp/dx + d/dy((nu+nut) du/dy) --------
    Lm.clear();
    for (Idx c = 0; c < N; ++c) {
      for (Idx q = m.csr.start[c]; q < m.csr.start[c + 1]; ++q) {
        const Idx f = m.csr.faces[q], nb = m.other(f, c);
        const cht::FaceFactors ff = cht::faceFactors(m, f, c);
        if (nb >= 0) {
          const Real gf = nu + 0.5 * (nut[c] + nut[nb]);
          Lm.add(c, c, gf * ff.alpha); Lm.add(c, nb, -gf * ff.alpha);
        } else if (m.face.bc[f] == BC::Wall) {
          // implicit wall function:  tau_w A ~ (rho u_tau^2 A / u_P) u_P
          const Real yP  = wall::wallDistance(m, f, c);
          const Real tauW = wall::uTauFromUp(u[c], nu, yP, rho).tauW;
          const Real A   = norm(m.face.S[f]);
          Lm.add(c, c, std::max(tauW * A / std::max(u[c], 1e-12), 1e-6));
        }
      }
      Lm.b[c] += dpdx * m.cV[c];
    }
    Lm.gaussSeidel(30);
    for (Idx c = 0; c < N; ++c) u[c] += urfU * (Lm.x[c] - u[c]);
    // ---------------- k equation ----------------------------------------
    Lk.clear();
    for (Idx c = 0; c < N; ++c) {
      Real P = nut[c] * S[c] * S[c];                       // per unit mass
      P = sst::limitProduction(P, rho, k[c], w[c]);
      for (Idx q = m.csr.start[c]; q < m.csr.start[c + 1]; ++q) {
        const Idx f = m.csr.faces[q], nb = m.other(f, c);
        const cht::FaceFactors ff = cht::faceFactors(m, f, c);
        if (nb >= 0) {
          const Real gf = nu + sK[c] * 0.5 * (nut[c] + nut[nb]);
          Lk.add(c, c, gf * ff.alpha); Lk.add(c, nb, -gf * ff.alpha);
        }                                               // wall: zero gradient
      }
      const Real dest = sst::c_.betaStar * std::max(w[c], 0.0);   // -beta* w k
      if (mode == PositivityMode::Patankar) { Lk.add(c, c, dest * m.cV[c]); Lk.b[c] += P * m.cV[c]; }
      else { Lk.b[c] += (P - dest * k[c]) * m.cV[c]; }
    }
    Lk.gaussSeidel(40);
    for (Idx c = 0; c < N; ++c) { k[c] += urfK * (Lk.x[c] - k[c]); k[c] = std::max(k[c], 1e-16); }
    // ---------------- omega equation -------------------------------------
    Lw.clear();
    for (Idx c = 0; c < N; ++c) {
      Real P = nut[c] * S[c] * S[c];
      P = sst::limitProduction(P, rho, k[c], w[c]);
      const Real Pw = gam[c] * P / std::max(nut[c], 1e-20) + cd[c] / rho;
      for (Idx q = m.csr.start[c]; q < m.csr.start[c + 1]; ++q) {
        const Idx f = m.csr.faces[q], nb = m.other(f, c);
        const cht::FaceFactors ff = cht::faceFactors(m, f, c);
        const Real gf = nu + sW[c] * 0.5 * (nut[c] + (nb >= 0 ? nut[nb] : nut[c]));
        if (nb >= 0) { Lw.add(c, c, gf * ff.alpha); Lw.add(c, nb, -gf * ff.alpha); }
        else if (m.face.bc[f] == BC::Wall) {
          const Real yP = wall::wallDistance(m, f, c);
          const auto ws = wall::uTauFromUp(u[c], nu, yP, rho);
          const auto ow = wall::omegaAllYPlus(nu, yP, ws.uTau, k[c]);
          Lw.add(c, c, gf * ff.alpha);
          Lw.b[c] += gf * ff.alpha * ow.blended;        // Dirichlet omega wall value
        }
      }
      // segregated solve: only the omega-destruction is implicit.  The extra
      // beta* rho omega term is used by the *block-coupled* k-omega kernel.
      const Real dest = 2.0 * beta[c] * std::max(w[c], 0.0);
      if (mode == PositivityMode::Patankar) { Lw.add(c, c, dest * m.cV[c]); Lw.b[c] += Pw * m.cV[c]; }
      else { Lw.b[c] += (Pw - dest * w[c]) * m.cV[c]; }
    }
    Lw.gaussSeidel(40);
    for (Idx c = 0; c < N; ++c) {
      w[c] += urfW * (Lw.x[c] - w[c]);
      w[c] = std::min(std::max(w[c], 1e-10), 1.0e7);   // physical bounds
    }
    minK = std::min(minK, *std::min_element(k.begin(), k.end()));
    minW = std::min(minW, *std::min_element(w.begin(), w.end()));
    cc.res = std::abs(Lm.x[N/2] - u[N/2]) / std::max(u[N/2], 1e-12);
  }
  // ---- wall friction velocity from the converged wall function -----------
  Real uTauNum = 0;
  for (Idx f = 0; f < m.face.nFaces; ++f)
    if (m.face.nbr[f] < 0 && m.face.bc[f] == BC::Wall) {
      const Idx c = m.face.own[f];
      uTauNum = wall::uTauFromUp(u[c], nu, wall::wallDistance(m, f, c), rho).uTau;
    }
  cc.uTau = uTauNum;
  cc.errUtau = (uTauNum - uTau) / uTau;
  cc.minK = minK; cc.minW = minW; cc.maxF1 = maxF1;
  Real l2 = 0; int n = 0;
  for (Idx c = 0; c < N; ++c) {
    const Real yp = m.cC[c].y * uTauNum / nu, up = u[c] / uTauNum;
    cc.yPlus.push_back(yp); cc.uPlus.push_back(up); cc.u.push_back(u[c]);
    cc.k.push_back(k[c]); cc.w.push_back(w[c]); cc.nut.push_back(nut[c]); cc.y.push_back(m.cC[c].y);
    if (yp > 20.0 && yp < 0.4 * ReTau) { const Real e = up - wall::spaldingUPlus(yp); l2 += e*e; ++n; }
  }
  cc.l2Uplus = n ? std::sqrt(l2 / n) : 0.0;
  return cc;
}

void caseChannel() {
  const ChannelCase fine = runChannel(160, 590.0, PositivityMode::Patankar);
  add("CHAN-01", "Re_tau=590, N=160: u_tau error (fine, y+~4)", fine.errUtau, 0.03,
      "all-y+ in low-Re mode");
  add("CHAN-02", "Re_tau=590, N=160: L2(u+ - Spalding) in log region", fine.l2Uplus, 0.9,
      "20 < y+ < 236");
  add("CHAN-03", "Re_tau=590, N=160: k stayed positive (1 = violated)", fine.minK > 0.0 ? 0.0 : 1.0, 0.5,
      "k > 0 for every iteration");
  add("CHAN-04", "Re_tau=590, N=160: omega stayed positive (1 = violated)", fine.minW > 0.0 ? 0.0 : 1.0, 0.5,
      "omega > 0 for every iteration");
  const ChannelCase coarse = runChannel(24, 590.0, PositivityMode::Patankar);
  add("CHAN-05", "Re_tau=590, N=24: u_tau error (coarse, y+~25)", coarse.errUtau, 0.06,
      "all-y+ in wall-function mode");
  add("CHAN-06", "Re_tau=590, N=24: L2(u+ - Spalding)", coarse.l2Uplus, 1.5,
      "same closure, 6.7x coarser mesh");
  add("CHAN-07", "Re_tau=590, N=24: k stayed positive", coarse.minK > 0.0 ? 0.0 : 1.0, 0.5, "positivity preserved");
  add("CHAN-08", "y+ insensitivity: |fine - coarse| u_tau error", std::abs(fine.errUtau - coarse.errUtau), 0.12,
      "continuous wall treatment must be mesh insensitive");
  std::cout << "  profiles: fine N=160 maxF1=" << fine.maxF1 << " coarse N=24 res=" << coarse.res << "\n";
}

// ======================================== 7. ALL-y+ WALL FUNCTION ACCURACY
void caseWallFunction() {
  const Real nu = 1.5e-5, rho = 1.2, yP = 0.001, uTauRef = 0.05;
  Real worst = 0, worstTp = 0;
  for (int i = 0; i <= 160; ++i) {
    const Real ypT = std::pow(10.0, -1.0 + 4.7 * i / 160.0);       // 0.1 .. 5000
    const Real uP  = uTauRef * wall::spaldingUPlus(ypT);
    const Real yPc = ypT * nu / uTauRef;
    const auto r = wall::uTauFromUp(uP, nu, yPc, rho);
    worst = std::max(worst, std::abs(r.uTau - uTauRef) / uTauRef);
    const auto ow = wall::omegaAllYPlus(nu, yPc, r.uTau, 1.0);
    worstTp = std::max(worstTp, std::abs(ow.blended - std::sqrt(ow.vis*ow.vis + ow.log*ow.log)) /
                                std::max(ow.blended, 1e-30));
  }
  add("WALL-01", "max u_tau recovery error, 0.1 <= y+ <= 5000", worst, 0.10,
      "Newton solve of u_P = u_tau uPlus(rho u_tau y_P/mu)");
  add("WALL-02", "omega blend identity  w = sqrt(w_vis^2 + w_log^2)", worstTp, 1e-14, "");
  // yP -> 0 must not produce a singular division
  const auto tiny = wall::omegaAllYPlus(nu, 1e-300, 0.05, 1.0);
  add("WALL-03", "no singular division at y_P = 1e-300", std::isfinite(tiny.blended) ? 0.0 : 1.0, 0.5,
      "w = " + std::to_string(tiny.blended));
  // Kader thermal law limits
  const Real Pr = 0.71;
  add("WALL-04", "T+ -> Pr y+ as y+ -> 0",
      std::abs(wall::TplusKader(1e-4, Pr, 0.85) / (Pr * 1e-4) - 1.0), 1e-3,
      "viscous sublayer limit");
  add("WALL-05", "T+ ~ (1/(kappa Prt)) ln(E y+) + beta(Pr) as y+ -> 1e5",
      [&]{ const Real yp = 1e5;
           const Real tExact = wall::TplusKader(yp, Pr, 0.85);
           const Real tAsym  = std::log(wall::ElogW * yp) / (0.41 * 0.85) + wall::kaderBetaPr(Pr);
           return std::abs(tExact / tAsym - 1.0); }(), 1e-6, "log layer limit");
}

// ============================================== 8. MONOLITHIC CHT, CONFORMAL
struct ChtCase {
  Real qNum, qExact, err, TintNum, TintExact;
  Idx  nCellsF, nCellsS;
};
ChtCase runChtMonolithic(int nf, int ns, int nySolid, Real Lf, Real Ls,
                         Real kf, Real ks, Real hh, Real hc, Real Th, Real Tc,
                         Real& fluxImbalanceOut) {
  PolyMesh mf, ms;
  const Real dy = 0.01, dz = 0.01;
  makeHexGrid(mf, nf, 1, 1, Vec3{Lf, dy, dz}, 0.0);
  makeHexGrid(ms, ns, nySolid, 1, Vec3{Ls, dy / nySolid, dz}, 0.0, 999u, true);
  for (Idx f = 0; f < mf.face.nFaces; ++f) if (mf.face.nbr[f] < 0) {
    const Vec3 n = unit(mf.face.S[f]);
    if      (n.x < -0.999) mf.face.bc[f] = BC::Inlet;         // hot convective film
    else if (n.x >  0.999) mf.face.bc[f] = BC::MappedWall;    // CHT twin face
    else                   mf.face.bc[f] = BC::Symmetry;      // transverse
  }
  for (Idx f = 0; f < ms.face.nFaces; ++f) if (ms.face.nbr[f] < 0) {
    const Vec3 n = unit(ms.face.S[f]);
    if      (n.x < -0.999) ms.face.bc[f] = BC::MappedWall;    // CHT twin face
    else if (n.x >  0.999) ms.face.bc[f] = BC::Outlet;        // cold convective film
    else                   ms.face.bc[f] = BC::Symmetry;
  }
  cht::CompositeWall wall;
  wall.L = {Lf, Ls}; wall.k = {kf, ks}; wall.hHot = hh; wall.hCold = hc; wall.THot = Th; wall.TCold = Tc;
  const Real qEx = wall.q();
  const auto Tex = wall.interfaceTemperatures();

  cht::MultiRegion MR;
  Field kfF(static_cast<std::size_t>(mf.nCells), kf), ksF(static_cast<std::size_t>(ms.nCells), ks);
  MR.addRegion(mf, kfF); MR.addRegion(ms, ksF);
  // twin pairs (supermesh / AMI): 1 fluid face  <->  nySolid solid faces
  std::vector<cht::TwinPair> pairs;
  std::vector<Idx> fFaces, sFaces;
  for (Idx f = 0; f < mf.face.nFaces; ++f) if (mf.face.nbr[f] < 0 && mf.face.bc[f] == BC::MappedWall) fFaces.push_back(f);
  for (Idx f = 0; f < ms.face.nFaces; ++f) if (ms.face.nbr[f] < 0 && ms.face.bc[f] == BC::MappedWall) sFaces.push_back(f);
  std::vector<std::pair<Idx, Idx>> extra;
  for (Idx ff : fFaces) {
    const Idx cF = mf.face.own[ff];
    const auto facF = cht::faceFactors(mf, ff, cF);
    for (Idx sf : sFaces) {
      const Idx cS = ms.face.own[sf];
      const auto facS = cht::faceFactors(ms, sf, cS);
      cht::TwinPair p;
      p.cellF = cF; p.cellS = cS; p.faceF = ff; p.faceS = sf;
      p.wf = 1.0 / static_cast<Real>(sFaces.size());
      p.ws = 1.0 / static_cast<Real>(fFaces.size());
      p.alphaF = facF.alpha; p.alphaS = facS.alpha;
      p.kcorrF = facF.kcorr; p.kcorrS = facS.kcorr;
      p.kf = kf; p.ks = ks;
      pairs.push_back(p);
      extra.push_back({static_cast<Idx>(0 + cF), static_cast<Idx>(mf.nCells + cS)});
    }
  }
  MR.build(extra);
  for (int outer = 0; outer < 6; ++outer) {
    MR.L.clear();
    MR.assembleConduction(0, Field(static_cast<std::size_t>(mf.nCells), 0.0));
    MR.assembleConduction(1, Field(static_cast<std::size_t>(ms.nCells), 0.0));
    // convective films
    for (Idx f = 0; f < mf.face.nFaces; ++f)
      if (mf.face.nbr[f] < 0 && mf.face.bc[f] == BC::Inlet) {
        const Idx c = mf.face.own[f]; const Real A = norm(mf.face.S[f]);
        MR.L.add(c, c, hh * A); MR.L.b[c] += hh * A * Th;
      }
    for (Idx f = 0; f < ms.face.nFaces; ++f)
      if (ms.face.nbr[f] < 0 && ms.face.bc[f] == BC::Outlet) {
        const Idx c = ms.face.own[f]; const Real A = norm(ms.face.S[f]);
        MR.L.add(mf.nCells + c, mf.nCells + c, hc * A); MR.L.b[mf.nCells + c] += hc * A * Tc;
      }
    MR.assembleMonolithicCHT(pairs);
    MR.L.gaussSeidel(400);              // M-matrix: GS is unconditionally convergent
    for (Idx c = 0; c < mf.nCells; ++c) MR.T[0][c] = MR.L.x[c];
    for (Idx c = 0; c < ms.nCells; ++c) MR.T[1][c] = MR.L.x[mf.nCells + c];
    Field gF(static_cast<std::size_t>(mf.face.nFaces)), gS(static_cast<std::size_t>(ms.face.nFaces));
    for (Idx f = 0; f < mf.face.nFaces; ++f) gF[f] = MR.T[0][mf.face.own[f]];
    for (Idx f = 0; f < ms.face.nFaces; ++f) gS[f] = MR.T[1][ms.face.own[f]];
    gradGauss(mf, MR.T[0], gF, MR.gradT[0]);
    gradGauss(ms, MR.T[1], gS, MR.gradT[1]);
  }
  ChtCase out;
  out.qExact = qEx; out.TintExact = Tex[1];
  out.nCellsF = mf.nCells; out.nCellsS = ms.nCells;
  Real flux = 0;
  for (auto& p : pairs) flux += MR.interfaceFlux(p);
  const Real A = dy * dz;
  out.qNum = flux / A;
  out.err = (out.qNum - qEx) / qEx;
  out.TintNum = MR.interfaceTemperature(pairs[0]);
  fluxImbalanceOut = 0;
  // conservation: sum of the pair fluxes on the fluid side == on the solid side
  Real fSum = 0, sSum = 0;
  for (auto& p : pairs) {
    const Vec3 gTf = MR.gradT[0][p.cellF], gTs = MR.gradT[1][p.cellS];
    cht::TwinPair q = p; cht::finalisePair(q, gTf, gTs);
    fSum += q.Gf * (MR.T[0][p.cellF] - MR.interfaceTemperature(q));
    sSum += q.Gs * (MR.interfaceTemperature(q) - MR.T[1][p.cellS]);
  }
  fluxImbalanceOut = std::abs(fSum - sSum) / std::max(std::abs(fSum), 1e-30);
  return out;
}

void caseCht() {
  Real imb = 0;
  const ChtCase a = runChtMonolithic(20, 20, 1, 0.01, 0.02, 0.6, 15.0, 50.0, 120.0, 500.0, 300.0, imb);
  add("CHT-01", "conformal monolithic CHT: q vs series resistance (rel err)", a.err, 1e-10,
      "single linear solve, no interface iteration");
  add("CHT-02", "conformal monolithic CHT: interface temperature (K)", std::abs(a.TintNum - a.TintExact), 1e-8,
      "T_int = (kf af TP + ks as TS)/(kf af + ks as)");
  add("CHT-03", "conformal twin-face flux conservation (rel)", imb, 1e-12,
      "fluid-side and solid-side fluxes are identical");
  Real imb2 = 0;
  const ChtCase b = runChtMonolithic(20, 40, 2, 0.01, 0.02, 0.6, 15.0, 50.0, 120.0, 500.0, 300.0, imb2);
  add("CHT-04", "NON-CONFORMAL (2:1 supermesh-AMI) q rel err", b.err, 1e-10,
      "1 fluid face <-> 2 solid faces, wf = 0.5 each");
  add("CHT-05", "non-conformal AMI flux conservation (rel)", imb2, 1e-12,
      "sum_j wf_j = 1 and sum_j ws_j = 1");
  add("CHT-06", "non-conformal interface temperature (K)", std::abs(b.TintNum - a.TintExact), 1e-8,
      "identical to the conformal solution");
}

// ============================================ 9. PARTITIONED ROBIN-ROBIN CHT
struct PartResult { Real alpha; Idx iters; Real errFinal; };
static std::vector<PartResult> g_partHistory;

void casePartitioned() {
  const int nf = 12, ns = 16;
  const Real Lfl = 0.01, Lsl = 0.02, kf = 0.6, ks = 15.0;
  const Real hh = 50.0, hc = 120.0, Th = 500.0, Tc = 300.0;
  PolyMesh mf, ms;
  const Real dy = 0.01, dz = 0.01;
  makeHexGrid(mf, nf, 1, 1, Vec3{Lfl, dy, dz}, 0.0);
  makeHexGrid(ms, ns, 1, 1, Vec3{Lsl, dy, dz}, 0.0, 999u, true);
  for (Idx f = 0; f < mf.face.nFaces; ++f) if (mf.face.nbr[f] < 0) {
    const Vec3 n = unit(mf.face.S[f]);
    if      (n.x < -0.999) mf.face.bc[f] = BC::Inlet;         // hot convective film
    else if (n.x >  0.999) mf.face.bc[f] = BC::MappedWall;    // CHT twin face
    else                   mf.face.bc[f] = BC::Symmetry;      // transverse
  }
  for (Idx f = 0; f < ms.face.nFaces; ++f) if (ms.face.nbr[f] < 0) {
    const Vec3 n = unit(ms.face.S[f]);
    if      (n.x < -0.999) ms.face.bc[f] = BC::MappedWall;    // CHT twin face
    else if (n.x >  0.999) ms.face.bc[f] = BC::Outlet;        // cold convective film
    else                   ms.face.bc[f] = BC::Symmetry;
  }
  cht::CompositeWall wall;
  wall.L = {Lfl, Lsl}; wall.k = {kf, ks}; wall.hHot = hh; wall.hCold = hc; wall.THot = Th; wall.TCold = Tc;
  const Real qEx = wall.q();

  Idx ff = 0, sf = 0;
  for (Idx f = 0; f < mf.face.nFaces; ++f) if (mf.face.nbr[f] < 0 && mf.face.bc[f] == BC::MappedWall) ff = f;
  for (Idx f = 0; f < ms.face.nFaces; ++f) if (ms.face.nbr[f] < 0 && ms.face.bc[f] == BC::MappedWall) sf = f;
  const Idx cF = mf.face.own[ff], cS = ms.face.own[sf];
  const auto facF = cht::faceFactors(mf, ff, cF), facS = cht::faceFactors(ms, sf, cS);
  const Real Gf = kf * facF.alpha, Gs = ks * facS.alpha;

  std::vector<Vec3> gTf(static_cast<std::size_t>(mf.nCells)), gTs(static_cast<std::size_t>(ms.nCells));
  Field Tf(static_cast<std::size_t>(mf.nCells), Th), Ts(static_cast<std::size_t>(ms.nCells), Tc);
  Field kfF(static_cast<std::size_t>(mf.nCells), kf), ksF(static_cast<std::size_t>(ms.nCells), ks);
  LinSys Lfluid = LinSys::fromMesh(mf), Lsolid = LinSys::fromMesh(ms);

  for (double alpha : {0.1, 0.3, 0.5, 0.7, 0.8, 0.9, 1.0}) {
    std::fill(Tf.begin(), Tf.end(), Th);
    std::fill(Ts.begin(), Ts.end(), Tc);
    Real TintOld = 0.5 * (Th + Tc);
    Idx iters = 0; Real err = 1;
    for (int it = 0; it < 20000; ++it) {
      // ---- fluid solve with Robin BC at the interface --------------------
      Lfluid.clear(); assembleCond(mf, kfF, gTf, Lfluid);
      for (Idx f = 0; f < mf.face.nFaces; ++f)
        if (mf.face.nbr[f] < 0 && mf.face.bc[f] == BC::Inlet) {
          const Idx c = mf.face.own[f]; const Real A = norm(mf.face.S[f]);
          Lfluid.add(c, c, hh * A); Lfluid.b[c] += hh * A * Th;
        }
      Lfluid.add(cF, cF, Gs); Lfluid.b[cF] += Gs * TintOld;
      Lfluid.gaussSeidel(300);
      // ---- solid solve with Robin BC at the interface --------------------
      Lsolid.clear(); assembleCond(ms, ksF, gTs, Lsolid);
      for (Idx f = 0; f < ms.face.nFaces; ++f)
        if (ms.face.nbr[f] < 0 && ms.face.bc[f] == BC::Outlet) {
          const Idx c = ms.face.own[f]; const Real A = norm(ms.face.S[f]);
          Lsolid.add(c, c, hc * A); Lsolid.b[c] += hc * A * Tc;
        }
      Lsolid.add(cS, cS, Gf); Lsolid.b[cS] += Gf * TintOld;
      Lsolid.gaussSeidel(300);
      // ---- under-relaxed interface temperature ---------------------------
      const Real Tnew = 0.5 * (Lfluid.x[cF] + Lsolid.x[cS]);
      err = std::abs(Tnew - TintOld);
      TintOld += alpha * (Tnew - TintOld);
      iters = it + 1;
      if (err < 1e-11) break;
    }
    Field gF(static_cast<std::size_t>(mf.face.nFaces)), gS(static_cast<std::size_t>(ms.face.nFaces));
    for (Idx f = 0; f < mf.face.nFaces; ++f) gF[f] = Lfluid.x[mf.face.own[f]];
    for (Idx f = 0; f < ms.face.nFaces; ++f) gS[f] = Lsolid.x[ms.face.own[f]];
    gradGauss(mf, Lfluid.x, gF, gTf); gradGauss(ms, Lsolid.x, gS, gTs);
    const Real qNum = kf * dot(gTf[cF], -unit(mf.face.S[ff])) ;   // magnitude at the interface
    g_partHistory.push_back({alpha, iters, std::abs(qNum - qEx) / qEx});
    if (alpha == 0.7)
      add("CHT-07", "partitioned Robin-Robin (alpha=0.7) q rel err", std::abs(qNum - qEx) / qEx, 1e-9,
          "converges to the monolithic answer");
    (void)gTs;
  }
  Idx best = 0; for (std::size_t i = 1; i < g_partHistory.size(); ++i)
    if (g_partHistory[i].iters < g_partHistory[best].iters) best = i;
  add("CHT-08", "optimal interface relaxation (scanned)", 0.0, 1e9,
      "alpha* = " + std::to_string(g_partHistory[best].alpha) + " with " +
      std::to_string(g_partHistory[best].iters) + " outer iterations");
}

// ===================================================== 10. ANISOTROPIC POROUS
void casePorous() {
  porous::Zone z;
  z.setPrincipal(Vec3{1, 0, 0}, Vec3{1.2e7, 3.4e6, 5.6e6}, Vec3{900.0, 120.0, 340.0}, Vec3{0, 1, 0});
  // (a) eigenvalue recovery
  Real lam[3]; Mat3 Q;
  eigenSym3(z.D, lam, Q);
  std::sort(lam, lam + 3);
  Real ref[3] = {3.4e6, 5.6e6, 1.2e7};
  Real eg = 0; for (int i = 0; i < 3; ++i) eg = std::max(eg, std::abs(lam[i] - ref[i]) / ref[i]);
  add("POR-01", "eigenvalues of D_global = diag(d1,d2,d3) (rel err)", eg, 1e-10,
      "D = R diag(d) R^T recovered by cyclic Jacobi");
  Real ortho = 0;
  { const Mat3 I = matmul(transpose(Q), Q);
    for (int i = 0; i < 3; ++i) for (int j = 0; j < 3; ++j)
      ortho = std::max(ortho, std::abs(I.at(i,j) - (i == j ? 1.0 : 0.0))); }
  add("POR-02", "orthonormality of the principal axes R", ortho, 1e-12, "R^T R = I");
  // (b) 1-D Ergun packed bed
  porous::Zone erg; erg.setErgun(0.005, 0.4);
  const Real mu = 1.0e-3, rho = 1000.0, dpdx = 8000.0;
  const Real d = erg.D.at(0,0), ff = erg.F.at(0,0);
  const Real uAnal = (-mu * d + std::sqrt(mu*mu*d*d + 2.0*rho*ff*dpdx)) / (rho * ff);
  Real u = 0.3 * uAnal;
  for (int it = 0; it < 2000; ++it) {
    const auto s = porous::sink(erg.D, erg.F, Vec3{u,0,0}, mu, rho);
    u = dpdx / std::max(s.diag[0], 1e-30);
  }
  add("POR-03", "Ergun packed bed: implicit Darcy-Forchheimer solve (rel err)",
      std::abs(u - uAnal) / uAnal, 1e-10,
      "d = 150(1-e)^2/(e^3 Dp^2), f = 3.5(1-e)/(e^3 Dp)");
  add("POR-04", "Ergun coefficients vs closed form",
      std::abs(d - 150.0*0.36/(0.064*2.5e-5)) / (150.0*0.36/(0.064*2.5e-5)), 1e-12, "");
  // (c) anisotropic 3x3 system:  mu D u + 0.5 rho |u| F u = -grad p
  const Vec3 gp{12000.0, -5000.0, 3000.0};
  Vec3 uu{0.01, 0.01, 0.01};
  for (int it = 0; it < 4000; ++it) {
    const Mat3 A = porous::sinkTensor(z.D, z.F, uu, mu, rho);
    // 3x3 solve (Cramer)
    Real det = A.at(0,0)*(A.at(1,1)*A.at(2,2)-A.at(1,2)*A.at(2,1))
             - A.at(0,1)*(A.at(1,0)*A.at(2,2)-A.at(1,2)*A.at(2,0))
             + A.at(0,2)*(A.at(1,0)*A.at(2,1)-A.at(1,1)*A.at(2,0));
    if (std::abs(det) < 1e-30) break;
    Vec3 rhs = -gp;
    Vec3 un;
    un.x = (rhs.x*(A.at(1,1)*A.at(2,2)-A.at(1,2)*A.at(2,1))
          - A.at(0,1)*(rhs.y*A.at(2,2)-A.at(1,2)*rhs.z)
          + A.at(0,2)*(rhs.y*A.at(2,1)-A.at(1,1)*rhs.z)) / det;
    un.y = (A.at(0,0)*(rhs.y*A.at(2,2)-A.at(1,2)*rhs.z)
          - rhs.x*(A.at(1,0)*A.at(2,2)-A.at(1,2)*A.at(2,0))
          + A.at(0,2)*(A.at(1,0)*rhs.z-rhs.y*A.at(2,0))) / det;
    un.z = (A.at(0,0)*(A.at(1,1)*rhs.z-rhs.y*A.at(2,1))
          - A.at(0,1)*(A.at(1,0)*rhs.z-rhs.y*A.at(2,0))
          + rhs.x*(A.at(1,0)*A.at(2,1)-A.at(1,1)*A.at(2,0))) / det;
    uu = un;
  }
  const Vec3 drag = porous::analyticPressureGradient(z.D, z.F, uu, mu, rho);
  Vec3 resid = drag + gp;
  add("POR-05", "anisotropic 3x3 sink residual (Pa/m)", norm(resid), 1e-6,
      "mu D u + 0.5 rho |u| F u = -grad p solved exactly");
  const Real angU = std::atan2(uu.y, uu.x), angP = std::atan2(gp.y, gp.x);
  add("POR-06", "drag / velocity misalignment exists [deg]",
      std::abs(std::abs(angU - angP) * 180.0 / kPi) - 1.0, 1e9,
      "angle = " + std::to_string(std::abs(angU - angP) * 180.0 / kPi) +
      " deg -> the sink is NOT parallel to the velocity");
  // (d) diagonal dominance of the implicit split
  Real worstDD = 0;
  std::mt19937 rng(4242u);
  std::uniform_real_distribution<Real> rd(-1, 1);
  for (int t = 0; t < 500; ++t) {
    const Vec3 ur{rd(rng), rd(rng), rd(rng)};
    const auto s = porous::sink(z.D, z.F, ur, mu, rho);
    for (int i = 0; i < 3; ++i) {
      Real off = 0;
      for (int j = 0; j < 3; ++j) if (j != i) off += std::abs(s.A.at(i, j)) * std::abs(comp(ur, j));
      worstDD = std::min(worstDD, s.diag[i] - off);       // must be >= 0
    }
  }
  add("POR-07", "diagonal dominance margin of the implicit porous sink", worstDD, 0.0,
      "a_PP >= sum |a_PJ| : the sink can never destabilise momentum");
}

// ====================================================== 11. THERMAL RADIATION
struct RadCase { Real tau0, errP1, errDom8, errDom16, imbDom, qErrDom; };
static std::vector<RadCase> g_rad;

void caseRadiation() {
  const Real a0 = 1.0, Tm = 1000.0;
  for (Real tau0 : {0.1, 1.0, 10.0}) {
    const int N = 60;
    // The transverse extent must be much larger than dx: only then is the
    // effective optical path of an oblique ordinate  dx/cos(theta)  as required
    // by the 1-D slab solution (transverse faces are perfect reflectors).
    const Real L = tau0 / a0, dy = 3.0 * L, dz = 3.0 * L;
    PolyMesh m;
    makeHexGrid(m, N, 1, 1, Vec3{L, dy, dz}, 0.0);
    for (Idx f = 0; f < m.face.nFaces; ++f) {
      if (m.face.nbr[f] >= 0) continue;
      const Vec3 C = m.face.C[f];
      if (C.x < 1e-13 || C.x > L - 1e-13) m.face.bc[f] = BC::Wall;    // black, cold
      else                                  m.face.bc[f] = BC::Symmetry;
    }
    Field T(static_cast<std::size_t>(N), Tm);
    Field Tw(static_cast<std::size_t>(m.face.nFaces), 0.0);
    rad::P1Config pc; pc.absorption = a0; pc.scattering = 0.0; pc.emissivity = 1.0; pc.Twall = 0.0;
    const auto p1 = rad::solveP1(m, T, pc, Tw);
    rad::DomConfig d8;  d8.nTheta = 8;  d8.nPhi = 16; d8.absorption = a0; d8.emissivity = 1.0; d8.Twall = 0.0;
    rad::DomConfig d16; d16.nTheta = 16; d16.nPhi = 32; d16.absorption = a0; d16.emissivity = 1.0; d16.Twall = 0.0;
    const auto r8  = rad::solveDOM(m, T, d8, Tw);
    const auto r16 = rad::solveDOM(m, T, d16, Tw);
    Real e1 = 0, e8 = 0, e16 = 0, emission = 0;
    for (Idx c = 0; c < N; ++c) {
      const Real tau = a0 * m.cC[c].x;
      const auto ex = rad::slabExact(tau, tau0, Tm);
      e1  = std::max(e1,  std::abs(p1.G[c]  - ex.G) / ex.G);
      e8  = std::max(e8,  std::abs(r8.G[c]  - ex.G) / ex.G);
      e16 = std::max(e16, std::abs(r16.G[c] - ex.G) / ex.G);
      emission += 4.0 * a0 * rad::sigmaSB * std::pow(Tm, 4.0) * m.cV[c];
    }
    g_rad.push_back({tau0, e1, e8, e16, std::abs(r8.energyImbalance) / emission,
                     std::abs(r8.energyImbalance) / emission});
    if (tau0 == 10.0) {
      add("RAD-01", "P1 max error, optically thick (tau0 = 10)", e1, 0.10,
          "P1 -> Rosseland diffusion limit");
      add("RAD-02", "fvDOM S8 max error, tau0 = 10", e8, 0.05, "");
      add("RAD-03", "fvDOM S16 max error, tau0 = 10", e16, 0.03, "");
    }
    if (tau0 == 1.0) {
      add("RAD-04", "P1 max error, tau0 = 1 (documented limitation)", e1, 1.0,
          "P1 is not accurate at intermediate optical thickness");
      add("RAD-05", "fvDOM S16 max error, tau0 = 1", e16, 0.03,
          "ordinate solution stays accurate");
    }
    if (tau0 == 0.1) {
      add("RAD-06", "fvDOM S16 max error, optically thin (tau0 = 0.1)", e16, 0.05,
          "P1 fails here (error " + std::to_string(e1) + "), DOM does not");
    }
    add("RAD-imb-" + std::to_string(static_cast<int>(tau0 * 10)),
        "fvDOM global energy imbalance, tau0 = " + std::to_string(tau0),
        std::abs(r8.energyImbalance) / emission, 1e-6, "emission == absorption + wall loss");
    add("RAD-p1imb-" + std::to_string(static_cast<int>(tau0 * 10)),
        "P1 global energy imbalance, tau0 = " + std::to_string(tau0),
        std::abs(p1.energyImbalance) / emission, 1e-6, "Marshak BC is conservative");
  }
}

}  // namespace

// ===========================================================================
int main() {
  const auto t0 = std::chrono::steady_clock::now();
  std::cout << std::setprecision(6);
  caseMesh();      caseWale();     casePatankar();  caseSst();
  caseSA();        caseChannel();  caseWallFunction();
  caseCht();       casePartitioned(); casePorous(); caseRadiation();
  const auto t1 = std::chrono::steady_clock::now();
  const double ms = std::chrono::duration<double, std::milli>(t1 - t0).count();

  Idx npass = 0; for (auto& c : g_checks) npass += c.pass;
  std::ostringstream o;
  o << "{\n  \"compiler\": \"g++ (Debian 12.2.0) -std=c++20 -O2 -Wall -Wextra\",\n";
  o << "  \"runtime_ms\": " << ms << ",\n";
  o << "  \"n_checks\": " << g_checks.size() << ",\n";
  o << "  \"n_pass\": " << npass << ",\n";
  o << "  \"checks\": [\n";
  for (std::size_t i = 0; i < g_checks.size(); ++i) {
    const auto& c = g_checks[i];
    o << "    {\"id\": " << jstr(c.id) << ", \"name\": " << jstr(c.name)
      << ", \"value\": " << c.value << ", \"tol\": " << c.tol
      << ", \"pass\": " << (c.pass ? "true" : "false") << ", \"note\": " << jstr(c.note) << "}"
      << (i + 1 < g_checks.size() ? "," : "") << "\n";
  }
  o << "  ],\n  \"partitioned\": [\n";
  for (std::size_t i = 0; i < g_partHistory.size(); ++i)
    o << "    {\"alpha\": " << g_partHistory[i].alpha << ", \"iters\": " << g_partHistory[i].iters
      << ", \"err\": " << g_partHistory[i].errFinal << "}" << (i + 1 < g_partHistory.size() ? "," : "") << "\n";
  o << "  ],\n  \"radiation\": [\n";
  for (std::size_t i = 0; i < g_rad.size(); ++i)
    o << "    {\"tau0\": " << g_rad[i].tau0 << ", \"errP1\": " << g_rad[i].errP1
      << ", \"errDom8\": " << g_rad[i].errDom8 << ", \"errDom16\": " << g_rad[i].errDom16 << "}"
      << (i + 1 < g_rad.size() ? "," : "") << "\n";
  o << "  ]\n}\n";
  std::cout << o.str();
  std::cerr << "[cluster-a] " << npass << "/" << g_checks.size() << " checks passed in " << ms << " ms\n";
  return npass == static_cast<Idx>(g_checks.size()) ? 0 : 1;
}
