// =============================================================================
//  CLUSTER A - 5 :: thermal radiation
//   (a) P1 / differential approximation  - elliptic, cheap, exact in the
//       optically thick (diffusion) limit
//   (b) fvDOM - finite-volume discrete ordinates, valid at any optical
//       thickness, no ray effect in the presence of scattering
//
//  P1 transport equation (incident radiation G [W/m^2]):
//      -div( Gamma_rad grad G ) + a G = 4 a n^2 sigma_SB T^4
//      Gamma_rad = 1 / ( 3 (a + sigma_s) )          isotropic scattering
//                 = 1 / ( 3 (a + sigma_s (1-g)) )   linear-anisotropic, g
//  Marshak (Milne-Eddington half-range moment) wall boundary condition:
//      q_w = -Gamma_rad dG/dn = ( eps / (2 (2 - eps)) ) ( 4 sigma_SB T_w^4 - G )
//  Discretised as a *positive* Robin coefficient on the main diagonal, so the
//  P1 matrix is an M-matrix and G >= 0 unconditionally.
//
//  fvDOM: for every ordinate l (direction Omega_l, weight w_l)
//      div( Omega_l I_l ) + (a + sigma_s) I_l
//          = a n^2 sigma T^4 / pi + (sigma_s / (4 pi)) G ,    G = sum_l w_l I_l
//  solved with an upwind face flux (positive coefficients only).
//
//  Analytic reference: 1-D isothermal slab, black cold walls, no scattering
//      G(tau) = 4 sigma T^4 [ 1 - 0.5 ( E2(tau) + E2(tau0 - tau) ) ]
//      q(tau) = 2 sigma T^4 [ E3(tau0 - tau) - E3(tau) ]
// =============================================================================
#pragma once
#include <numeric>
#include "cluster_a_core.hpp"

namespace cla {
namespace rad {

inline constexpr Real sigmaSB = 5.670374419e-8;      // Stefan-Boltzmann

// ---------------------------------------------- Gauss-Legendre nodes on [-1,1]
inline void gaussLegendre(int n, std::vector<Real>& x, std::vector<Real>& w) {
  x.assign(static_cast<std::size_t>(n), 0.0);
  w.assign(static_cast<std::size_t>(n), 0.0);
  for (int i = 0; i < n; ++i) {
    Real z = std::cos(kPi * (i + 0.75) / (n + 0.5)), z1 = 0, pp = 0;
    for (int it = 0; it < 100; ++it) {
      Real p1 = 1, p2 = 0;
      for (int j = 0; j < n; ++j) { const Real p3 = p2; p2 = p1;
        p1 = ((2.0*j + 1.0) * z * p2 - j * p3) / (j + 1); }
      pp = n * (z * p1 - p2) / (z * z - 1.0);
      z1 = z; z = z1 - p1 / pp;
      if (std::abs(z - z1) < 1e-15) break;
    }
    x[static_cast<std::size_t>(i)] = -z;
    w[static_cast<std::size_t>(i)] = 2.0 / ((1.0 - z * z) * pp * pp);
  }
}

// ------------------------------------------------------- exponential integrals
// E_n(x) = int_0^1 mu^{n-2} exp(-x/mu) dmu  (needed by the exact slab solution)
inline Real expIntegral(int n, Real x) {
  static std::vector<Real> t, w;
  if (t.empty()) { gaussLegendre(32, t, w); }
  Real s = 0;
  const Real p = static_cast<Real>(n) - 2.0;
  for (std::size_t i = 0; i < t.size(); ++i) {
    const Real mu = 0.5 * (t[i] + 1.0);
    s += 0.5 * w[i] * std::pow(mu, p) * std::exp(-x / std::max(mu, 1e-12));
  }
  return s;
}

// --------------------------------------------------------------- P1 solver
struct P1Config {
  Real absorption = 1.0;          // a        [1/m]
  Real scattering = 0.0;          // sigma_s  [1/m]
  Real anisotropy = 0.0;          // g (linear anisotropic scattering)
  Real refractive = 1.0;          // n
  Real emissivity = 1.0;          // wall emissivity
  Real Twall      = 300.0;        // surroundings / default wall temperature
  Idx  picard     = 3;            // deferred-correction sweeps (skewness)
  Real gamma() const { return 1.0 / (3.0 * (absorption + scattering * (1.0 - anisotropy))); }
};

struct P1Result {
  Field G;                                 // incident radiation [W/m^2]
  std::vector<Real> qx;                    // |q_r| per cell
  Real gamma{0}, energyImbalance{0}, wallFlux{0};
  Idx  linearIterations{0};
};

//  -div(Gamma grad G) + a G = 4 a n^2 sigma T^4  with Marshak wall conditions.
inline P1Result solveP1(const PolyMesh& m, const Field& T, const P1Config& cfg,
                        const Field& TwallFace = {}) {
  P1Result r;
  r.gamma = cfg.gamma();
  const Real Cm = cfg.emissivity / (2.0 * (2.0 - cfg.emissivity));
  Field Gprev(static_cast<std::size_t>(m.nCells), 0.0);
  std::vector<Vec3> gGrad(static_cast<std::size_t>(m.nCells), Vec3{});
  LinSys L = LinSys::fromMesh(m);

  for (Idx outer = 0; outer < cfg.picard; ++outer) {
    L.clear();
    for (Idx c = 0; c < m.nCells; ++c) {
      L.b[c] += 4.0 * cfg.absorption * cfg.refractive * cfg.refractive * sigmaSB *
                std::pow(std::max(T[c], 0.0), 4.0) * m.cV[c];
      L.add(c, c, cfg.absorption * m.cV[c]);
      for (Idx k = m.csr.start[c]; k < m.csr.start[c + 1]; ++k) {
        const Idx f = m.csr.faces[k];
        const Idx nb = m.other(f, c);
        const Vec3 Sf = m.outward(f, c);          // flip if c is the neighbour
        const Vec3 d = (nb >= 0 ? m.cC[nb] : m.face.C[f]) - m.cC[c];
        const Real aS = norm(Sf);
        const Real alpha = (aS * aS) / std::max(dot(Sf, d), kSmall);
        const Vec3 kcorr = Sf - d * alpha;        // = 0 if orthogonal
        if (nb >= 0) {
          L.add(c, c, r.gamma * alpha);
          L.add(c, nb, -r.gamma * alpha);
          const Vec3 gN = 0.5 * (gGrad[c] + gGrad[nb]);    // deferred correction
          L.b[c] += r.gamma * dot(gN, kcorr);
        } else if (m.face.bc[f] == BC::Symmetry) {
          // zero net radiative flux:  q . n = -Gamma dG/dn = 0  ->  no term
        } else if (m.face.bc[f] == BC::Wall) {
          // Marshak:  q.n = (eps/(2(2-eps))) |S_f| (G_P - 4 sigma T_w^4)
          const Real Twf = TwallFace.empty() ? cfg.Twall : TwallFace[f];
          L.add(c, c, Cm * aS);
          L.b[c] += Cm * aS * 4.0 * sigmaSB * std::pow(std::max(Twf, 0.0), 4.0);
        } else {                                            // black surroundings
          const Real Twf = TwallFace.empty() ? cfg.Twall : TwallFace[f];
          L.add(c, c, r.gamma * alpha);
          L.b[c] += r.gamma * alpha * 4.0 * sigmaSB * std::pow(std::max(Twf, 0.0), 4.0);
        }
      }
    }
    r.linearIterations = L.gaussSeidel(300);
    Field gface(static_cast<std::size_t>(m.face.nFaces), 0.0);
    for (Idx f = 0; f < m.face.nFaces; ++f) gface[f] = L.x[m.face.own[f]];
    gradGauss(m, L.x, gface, gGrad);
    Gprev = L.x;
  }

  r.G = L.x;
  r.qx.assign(static_cast<std::size_t>(m.nCells), 0.0);
  Real imbalance = 0;
  for (Idx c = 0; c < m.nCells; ++c) {
    const Vec3 qr = gGrad[c] * (-r.gamma);
    r.qx[c] = norm(qr);
    imbalance += (cfg.absorption * (4.0 * cfg.refractive * cfg.refractive * sigmaSB *
                 std::pow(std::max(T[c], 0.0), 4.0) - r.G[c])) * m.cV[c];
  }
  Real wf = 0;
  for (Idx f = 0; f < m.face.nFaces; ++f) {
    if (m.face.nbr[f] >= 0) continue;
    if (m.face.bc[f] == BC::Symmetry) continue;   // q.n = 0 by construction
    const Idx c = m.face.own[f];
    const Vec3 qr = gGrad[c] * (-r.gamma);
    imbalance += dot(qr, m.face.S[f]);
    if (m.face.bc[f] == BC::Wall) wf += dot(qr, m.face.S[f]);
  }
  r.energyImbalance = imbalance;
  r.wallFlux = wf;
  return r;
}

// --------------------------------------------------------------- fvDOM solver
struct Ordinate { Vec3 Omega; Real w; };

// Product quadrature: Gauss-Legendre in mu = cos(theta), uniform in azimuth.
// sum_l w_l == 4 pi exactly; symmetric about the z axis; no direction has
// mu == 0 (even node counts), which keeps the upwind sweep well posed.
inline std::vector<Ordinate> makeOrdinates(int nTheta, int nPhi) {
  std::vector<Ordinate> dirs;
  std::vector<Real> mu, w;
  gaussLegendre(nTheta, mu, w);
  const Real dphi = 2.0 * kPi / nPhi;
  for (int k = 0; k < nTheta; ++k) {
    const Real st = std::sqrt(std::max(1.0 - mu[static_cast<std::size_t>(k)] * mu[static_cast<std::size_t>(k)], 0.0));
    for (int j = 0; j < nPhi; ++j) {
      const Real phi = (j + 0.5) * dphi;
      dirs.push_back({Vec3{st * std::cos(phi), st * std::sin(phi), mu[static_cast<std::size_t>(k)]},
                      w[static_cast<std::size_t>(k)] * dphi});
    }
  }
  return dirs;
}

struct DomConfig {
  int  nTheta = 8, nPhi = 16;      // "S8-like": 128 ordinates
  Real absorption = 1.0, scattering = 0.0, refractive = 1.0;
  Real emissivity = 1.0, Twall = 300.0;
  Idx  sweeps = 60;
};

struct DomResult {
  Field G;
  Idx  sweeps{0}, nOrdinates{0};
  Real energyImbalance{0}, wallFlux{0};
};

// Axis index of a (Cartesian aligned) face normal, or -1 if it is oblique.
inline int faceAxis(const PolyMesh& m, Idx f) {
  const Vec3 S = m.face.S[f];
  const Real a = norm(S);
  if (a < kSmall) return -1;
  const Real ax = std::abs(S.x / a), ay = std::abs(S.y / a), az = std::abs(S.z / a);
  if (ax > 0.999) return 0;
  if (ay > 0.999) return 1;
  if (az > 0.999) return 2;
  return -1;
}

inline DomResult solveDOM(const PolyMesh& m, const Field& T, const DomConfig& cfg,
                          const Field& TwallFace = {}) {
  DomResult res;
  const auto dirs = makeOrdinates(cfg.nTheta, cfg.nPhi);
  const std::size_t nd = dirs.size();
  // --- specular reflection map for symmetry planes ------------------------
  // The quadrature is closed under  mu_z -> -mu_z,  phi -> -phi,  phi -> pi-phi
  std::vector<int> reflX(nd, -1), reflY(nd, -1), reflZ(nd, -1);
  for (int k = 0; k < cfg.nTheta; ++k) for (int j = 0; j < cfg.nPhi; ++j) {
    const std::size_t l = static_cast<std::size_t>(k) * cfg.nPhi + j;
    const int kr = cfg.nTheta - 1 - k;
    const int jr = cfg.nPhi - 1 - j;
    const int jh = cfg.nPhi / 2 - 1 - j;
    reflZ[l] = static_cast<int>(kr) * cfg.nPhi + j;
    reflY[l] = k * cfg.nPhi + jr;
    reflX[l] = k * cfg.nPhi + jh;
  }
  std::vector<int> symAxis(static_cast<std::size_t>(m.face.nFaces), -1);
  for (Idx f = 0; f < m.face.nFaces; ++f)
    if (m.face.nbr[f] < 0 && m.face.bc[f] == BC::Symmetry) symAxis[f] = faceAxis(m, f);
  res.nOrdinates = static_cast<Idx>(nd);
  std::vector<Field> I(nd, Field(static_cast<std::size_t>(m.nCells), 0.0));
  Field G(static_cast<std::size_t>(m.nCells), 0.0);
  const Real beta = cfg.absorption + cfg.scattering;

  for (Idx sw = 0; sw < cfg.sweeps; ++sw) {
    std::fill(G.begin(), G.end(), 0.0);
    for (std::size_t l = 0; l < nd; ++l)
      for (Idx c = 0; c < m.nCells; ++c) G[c] += dirs[l].w * I[l][c];
    Real change = 0;
    for (std::size_t l = 0; l < nd; ++l) {
      const Vec3 Om = dirs[l].Omega;
      for (Idx c = 0; c < m.nCells; ++c) {
        Real outflow = 0, rhs = 0;
        for (Idx k = m.csr.start[c]; k < m.csr.start[c + 1]; ++k) {
          const Idx f = m.csr.faces[k];
          const Real fdf = dot(Om, m.outward(f, c));
          const Idx nb = m.other(f, c);
          if (fdf >= 0) outflow += fdf;                     // upwind => own value
          else if (nb >= 0) rhs += -fdf * I[l][nb];
          else if (symAxis[f] >= 0) {
            // specular reflection: incoming in Omega equals outgoing in Omega'
            const int lref = (symAxis[f] == 0 ? reflX[l]
                            : symAxis[f] == 1 ? reflY[l] : reflZ[l]);
            if (lref >= 0) rhs += -fdf * I[static_cast<std::size_t>(lref)][c];
          } else {
            const Real Twf = TwallFace.empty() ? cfg.Twall : TwallFace[f];
            rhs += -fdf * sigmaSB * std::pow(std::max(Twf, 0.0), 4.0) * cfg.emissivity / kPi;
          }
        }
        const Real V = m.cV[c];
        const Real src = cfg.absorption * cfg.refractive * cfg.refractive * sigmaSB *
                         std::pow(std::max(T[c], 0.0), 4.0) / kPi
                       + cfg.scattering * G[c] / (4.0 * kPi);
        const Real den = outflow + beta * V;
        const Real Inew = den > kSmall ? (rhs + src * V) / den : 0.0;
        change = std::max(change, std::abs(Inew - I[l][c]));
        I[l][c] = Inew;
      }
    }
    res.sweeps = sw + 1;
    if (change < 1e-13) break;
  }
  std::fill(G.begin(), G.end(), 0.0);
  for (std::size_t l = 0; l < nd; ++l)
    for (Idx c = 0; c < m.nCells; ++c) G[c] += dirs[l].w * I[l][c];
  res.G = G;
  // global energy balance: emission + wall inflow == absorption
  Real imb = 0;
  for (Idx c = 0; c < m.nCells; ++c)
    imb += (4.0 * cfg.absorption * cfg.refractive * cfg.refractive * sigmaSB *
            std::pow(std::max(T[c],0.0),4.0) - cfg.absorption * G[c]) * m.cV[c];
  Real wf = 0;
  for (std::size_t l = 0; l < nd; ++l) for (Idx f = 0; f < m.face.nFaces; ++f) {
    if (m.face.nbr[f] >= 0) continue;
    if (m.face.bc[f] == BC::Symmetry) continue;   // reflecting: zero net flux
    const Real fdf = dot(dirs[l].Omega, m.face.S[f]);
    if (fdf <= 0) continue;                     // outgoing radiant energy only
    wf  += dirs[l].w * fdf * I[l][m.face.own[f]];
    imb += dirs[l].w * fdf * I[l][m.face.own[f]];
  }
  res.wallFlux = wf;
  res.energyImbalance = imb;
  return res;
}

// ------------------------------------------------- exact 1-D slab solution
struct SlabExact { Real G, q; };
inline SlabExact slabExact(Real tau, Real tau0, Real Tm) {
  const Real s4 = sigmaSB * std::pow(std::max(Tm, 0.0), 4.0);
  return {4.0 * s4 * (1.0 - 0.5 * (expIntegral(2, tau) + expIntegral(2, tau0 - tau))),
          2.0 * s4 * (expIntegral(3, tau0 - tau) - expIntegral(3, tau))};
}

}  // namespace rad
}  // namespace cla
