// ============================================================================
//  PolyVV-20 :: Discrete IFT adjoint verification
//   a) Complex-step differentiation (h = 1e-30), zero subtractive cancellation
//   b) Directional Taylor remainder test  R1(eps) = O(eps^2)
//   c) Scalar advection-diffusion FVM residual (templated)
//   d) Collocated momentum-pressure (Rhie-Chow) residual — Stokes MMS
//  Everything runs on the ACTUAL discretized operator; no mocks.
// ============================================================================
#pragma once
#include <complex>
#include <vector>
#include <array>
#include <cmath>
#include <numeric>
#include "assembly.h"

namespace vv {

using CD = std::complex<double>;

// random unit direction in R^k from splitmix64 seed
inline std::vector<std::vector<double>> randomUnitDirections(int k, int m, uint64_t seed) {
  std::vector<std::vector<double>> dirs(m, std::vector<double>(k));
  for (int i = 0; i < m; ++i) {
    double nrm = 0.0;
    for (int j = 0; j < k; ++j) {
      uint64_t h = splitmix64(seed + 1000003ULL * (uint64_t)(i * 16 + j + 1));
      dirs[i][j] = (double)(h >> 11) * (1.0 / 9007199254740992.0) * 2.0 - 1.0;
      nrm += dirs[i][j] * dirs[i][j];
    }
    nrm = std::sqrt(nrm);
    for (int j = 0; j < k; ++j) dirs[i][j] /= nrm;
  }
  return dirs;
}

struct TaylorResult {
  std::vector<double> epsList;
  std::vector<std::vector<double>> resid;   // [direction][eps]
  std::vector<double> slope;                // per-direction log-log LS fit
  double meanSlope = 0, minSlope = 0, maxResid = 0;
};

struct ScalarAdjointOut {
  std::array<double, 3> thetaTest{0.17, 1.35, 1.8};
  std::array<double, 3> thetaDesign{GAMMA_BASE, U0_BASE, SIGMA_BASE};
  std::array<double, 3> adjoint{}, complexStep{}, fd{}, relErrCS{}, relErrFD{};
  double J0 = 0.0;
  TaylorResult taylor;
};

struct RCSystemOut {
  int nCells = 0, dim = 0;
  std::array<double, 2> thetaTest{0.30, 1.6};   // (nu, sigma)
  std::array<double, 2> thetaDesign{0.15, 1.0};
  std::array<double, 2> adjoint{}, complexStep{}, fd{}, relErrCS{}, relErrFD{};
  double J0 = 0.0, residual = 0.0;
  TaylorResult taylor;
};

// ---------------- scalar adjoint driver (8^3 dense) ----------------
inline ScalarAdjointOut runScalarAdjoint(const DistortedMesh& M, const GradientData& G,
                                         const MeshCache& C, const QuadData& Q,
                                         CorrectionFlags flags) {
  using std::complex;
  ScalarAdjointOut R;
  const double th[3] = {R.thetaTest[0], R.thetaTest[1], R.thetaTest[2]};
  auto solveJ = [&](double g, double u, double s) {
    ScalarParams<double> sp{g, u, s};
    Sys<double> S = assembleScalar<double>(M, G, C, Q, sp, flags);
    std::vector<double> A = tripletsToDense(S);
    std::vector<int> perm;
    if (!luFactor(A, S.n, perm)) throw std::runtime_error("LU failed (scalar)");
    std::vector<double> phi = luSolve(A, perm, S.n, S.rhs);
    double J = (double)objectiveJ<double>(M, Q, phi);
    return std::pair<std::vector<double>, double>{phi, J};
  };
  auto solveJcomplex = [&](const std::array<complex<double>, 3>& thc) {
    ScalarParams<complex<double>> sp{thc[0], thc[1], thc[2]};
    Sys<complex<double>> S = assembleScalar<complex<double>>(M, G, C, Q, sp, flags);
    std::vector<complex<double>> A = tripletsToDense(S);
    std::vector<int> perm;
    if (!luFactor(A, S.n, perm)) throw std::runtime_error("LU failed (complex scalar)");
    std::vector<complex<double>> phi = luSolve(A, perm, S.n, S.rhs);
    return objectiveJ<complex<double>>(M, Q, phi);
  };
  auto [phi, J0] = solveJ(th[0], th[1], th[2]);
  R.J0 = J0;
  // gradient of J wrt phi
  const int n = (int)M.cells.size();
  std::vector<double> dJdphi(n);
  for (int i = 0; i < n; ++i) {
    double d = phi[i] - Q.cells[i].phiStar;
    dJdphi[i] = 2.0 * M.cells[i].volume * d;
  }
  // factor A(theta) once, reuse for lambda
  ScalarParams<double> sp{th[0], th[1], th[2]};
  Sys<double> S0 = assembleScalar<double>(M, G, C, Q, sp, flags);
  std::vector<double> A = tripletsToDense(S0);
  std::vector<int> perm;
  luFactor(A, n, perm);
  std::vector<double> lambda = luSolveT(A, perm, n, dJdphi);
  // theta-basis assemblies for exact analytic derivatives.
  // A is affine in (gamma, u0) and independent of sigma (A(0,0,0) = 0):
  //   dA/dgamma = A(1,0,*) , dA/du0 = A(0,1,*) , dA/dsigma = 0.
  // b(gamma,u0,sigma) = sigma*(u0 qDiv - gamma qLap) - constVec(gamma,u0),
  // constVec linear in (gamma,u0):  constVec = gamma*C1 + u0*C2,  C1 = -b(1,0,0), C2 = -b(0,1,0).
  Sys<double> S1 = assembleScalar<double>(M, G, C, Q, ScalarParams<double>{1.0, 0.0, 0.0}, flags);  // rhs = -C1
  Sys<double> S2 = assembleScalar<double>(M, G, C, Q, ScalarParams<double>{0.0, 1.0, 0.0}, flags);  // rhs = -C2
  auto matVec = [&](const Sys<double>& S, const std::vector<double>& x) {
    std::vector<double> y(n, 0.0);
    for (auto& [r, c, v] : S.triplets) y[r] += v * x[c];
    return y;
  };
  auto dotp = [&](const std::vector<double>& a, const std::vector<double>& b) {
    double s = 0; for (int i = 0; i < n; ++i) s += a[i] * b[i]; return s;
  };
  std::vector<double> dAPhi1 = matVec(S1, phi), dAPhi2 = matVec(S2, phi);
  std::vector<double> dbGamma(n), dbU0(n), dbSigma(n);
  for (int i = 0; i < n; ++i) {
    const double qDiv = Q.cells[i].qDiv, qLap = Q.cells[i].qLap;
    const double C1 = -S1.rhs[i];   // constVec(1,0)
    const double C2 = -S2.rhs[i];   // constVec(0,1)
    dbGamma[i] = -th[2] * qLap - C1;
    dbU0[i] = th[2] * qDiv - C2;
    dbSigma[i] = th[1] * qDiv - th[0] * qLap;
  }
  R.adjoint[0] = dotp(lambda, dbGamma) - dotp(lambda, dAPhi1);
  R.adjoint[1] = dotp(lambda, dbU0) - dotp(lambda, dAPhi2);
  R.adjoint[2] = dotp(lambda, dbSigma);  // dA/dsigma = 0
  // ---- complex-step differentiation ----
  const double hcs = 1e-30;
  for (int k = 0; k < 3; ++k) {
    std::array<complex<double>, 3> thc{complex<double>(th[0], 0), complex<double>(th[1], 0), complex<double>(th[2], 0)};
    thc[k] = complex<double>(th[k], hcs);
    complex<double> Jc = solveJcomplex(thc);
    R.complexStep[k] = Jc.imag() / hcs;
  }
  // ---- finite differences (diagnostic contrast) ----
  const double fdEps = 1e-6;
  for (int k = 0; k < 3; ++k) {
    double tp[3] = {th[0], th[1], th[2]}, tm[3] = {th[0], th[1], th[2]};
    tp[k] += fdEps; tm[k] -= fdEps;
    double Jp = solveJ(tp[0], tp[1], tp[2]).second;
    double Jm = solveJ(tm[0], tm[1], tm[2]).second;
    R.fd[k] = (Jp - Jm) / (2.0 * fdEps);
  }
  double jref = 0.0; for (int k = 0; k < 3; ++k) jref = std::max(jref, std::fabs(R.adjoint[k]));
  for (int k = 0; k < 3; ++k) {
    R.relErrCS[k] = std::fabs(R.complexStep[k] - R.adjoint[k]) / std::max(jref, 1e-30);
    R.relErrFD[k] = std::fabs(R.fd[k] - R.adjoint[k]) / std::max(jref, 1e-30);
  }
  // ---- directional Taylor remainder test ----
  auto dirs = randomUnitDirections(3, 8, 0xB5297A4DULL);
  const std::vector<double> epsList{1e-2, 1e-3, 1e-4, 1e-5, 1e-6, 1e-7};
  R.taylor.epsList = epsList;
  double maxRes = 0.0, sumSlope = 0.0, minSlope = 1e30;
  for (const auto& v : dirs) {
    double gv = 0.0; for (int k = 0; k < 3; ++k) gv += R.adjoint[k] * v[k];
    std::vector<double> row;
    std::vector<std::pair<double, double>> pts;
    for (double eps : epsList) {
      double p0 = solveJ(th[0] + eps * v[0], th[1] + eps * v[1], th[2] + eps * v[2]).second;
      double r1 = std::fabs(p0 - R.J0 - eps * gv);
      row.push_back(r1);
      maxRes = std::max(maxRes, r1);
      if (r1 > 0.0) pts.emplace_back(std::log10(eps), std::log10(r1));
    }
    // least-squares slope of log10 resid vs log10 eps (all points)
    double sx = 0, sy = 0, sxx = 0, sxy = 0; int m = (int)pts.size();
    for (auto& [a, b] : pts) { sx += a; sy += b; sxx += a * a; sxy += a * b; }
    double slope = (m >= 2) ? (m * sxy - sx * sy) / (m * sxx - sx * sx) : 0.0;
    R.taylor.slope.push_back(slope);
    sumSlope += slope; minSlope = std::min(minSlope, slope);
    R.taylor.resid.push_back(row);
  }
  R.taylor.meanSlope = sumSlope / (double)dirs.size();
  R.taylor.minSlope = minSlope;
  R.taylor.maxResid = maxRes;
  return R;
}

// ================= Rhie-Chow collocated momentum-pressure (Stokes) ==========
constexpr double NU_REF = 0.15;  // frozen Rhie-Chow coefficient reference viscosity

template <typename T>
struct RCParams { T nu, sigma; };
template <typename T>
struct RCSys {
  int nCells, dim;
  std::vector<std::tuple<int, int, T>> triplets;
  std::vector<T> rhs;
};

// needs per-field WLS datasets (t_c depends on boundary values)
struct RCGradSet {
  GradientData ux, uy, uz, p;
};

inline RCGradSet buildRCGradients(const DistortedMesh& M) {
  std::vector<double> ub(M.faces.size(), 0.0), pb(M.faces.size(), 0.0);
  // boundary face values: U*_q and p* at face centroids
  std::vector<double> uxB(M.faces.size(), 0.0), uyB(M.faces.size(), 0.0), uzB(M.faces.size(), 0.0);
  for (size_t fi = 0; fi < M.faces.size(); ++fi) {
    if (M.faces[fi].cellN >= 0) continue;
    const Vec3 xf = M.faces[fi].xf;
    Vec3 U = uExact(xf.x, xf.y, xf.z);
    uxB[fi] = U.x; uyB[fi] = U.y; uzB[fi] = U.z; pb[fi] = pStokes(xf.x, xf.y, xf.z);
  }
  (void)ub;
  RCGradSet G;
  G.ux = buildGradientData(M, uxB);
  G.uy = buildGradientData(M, uyB);
  G.uz = buildGradientData(M, uzB);
  G.p = buildGradientData(M, pb);
  return G;
}

template <typename T>
RCSys<T> assembleRC(const DistortedMesh& M, const RCGradSet& GS, const MeshCache& C,
                    const QuadData& /*Q*/, const RCParams<T>& sp, bool useCorr) {
  const int nc = (int)M.cells.size();
  const int dim = 4 * nc;
  RCSys<T> out; out.nCells = nc; out.dim = dim; out.rhs.assign(dim, T(0.0));
  std::vector<T> constVec(dim, T(0.0));
  auto add = [&](int r, int c, const T& v) { if (!(v == T(0.0))) out.triplets.emplace_back(r, c, v); };
  const double alphaOrthScale = 1.0; (void)alphaOrthScale;
  auto idx = [](int cell, int q) { return 4 * cell + q; };
  // sum of face alphas per cell (for RC coefficient)
  std::vector<double> sumAlpha(nc, 0.0);
  for (size_t fi = 0; fi < M.faces.size(); ++fi) {
    sumAlpha[M.faces[fi].cellP] += C.fg[fi].alphaOrth;  // reference magnitude
    if (M.faces[fi].cellN >= 0) sumAlpha[M.faces[fi].cellN] += C.fg[fi].alphaOrth;
  }
  auto Lvec = [&](const GradientData& G, int c, int j, std::array<double, 3>& outv, bool& has) {
    LvecOf<T>(G, c, j, outv, has);
  };
  // ---------- momentum rows ----------
  const GradientData* GU[3] = {&GS.ux, &GS.uy, &GS.uz};
  for (int q = 0; q < 3; ++q) {
    const GradientData& Gq = *GU[q];
    for (size_t fi = 0; fi < M.faces.size(); ++fi) {
      const Face& f = M.faces[fi];
      const FaceG& g = C.fg[fi];
      const T nu = sp.nu;
      const double alpha = g.alpha;
      const Vec3 kCorr = useCorr ? g.kCorr : Vec3{};
      const int P = f.cellP;
      // q-th component of boundary Dirichlet data
      double uBq = 0.0;
      if (f.cellN < 0) {
        Vec3 U = uExact(f.xf.x, f.xf.y, f.xf.z);
        uBq = (q == 0 ? U.x : (q == 1 ? U.y : U.z));
      }
      if (f.cellN >= 0) {
        const int N = f.cellN;
        // momentum diffusion: R += nu * S . grad(Uq) = nu [ alpha (Uq_N - Uq_P) + kCorr . gbar ]
        // (this is the PLUS sign of the viscous Laplace term in 0 = -grad p + nu lap U + f)
        const std::array<T, 3> cvec{+nu * (T)kCorr.x, +nu * (T)kCorr.y, +nu * (T)kCorr.z};
        std::vector<int> js{P, N};
        for (const auto& pr : Gq.cells[P].cols) js.push_back(pr.first);
        for (const auto& pr : Gq.cells[N].cols) js.push_back(pr.first);
        std::sort(js.begin(), js.end());
        js.erase(std::unique(js.begin(), js.end()), js.end());
        for (int j : js) {
          std::array<double, 3> lp{0, 0, 0}, ln{0, 0, 0};
          bool hp = false, hn = false;
          Lvec(Gq, P, j, lp, hp); Lvec(Gq, N, j, ln, hn);
          std::array<double, 3> s{(std::array<double, 3>{0.5 * ((hp ? lp[0] : 0.0) + (hn ? ln[0] : 0.0)),
                                                          0.5 * ((hp ? lp[1] : 0.0) + (hn ? ln[1] : 0.0)),
                                                          0.5 * ((hp ? lp[2] : 0.0) + (hn ? ln[2] : 0.0))})};
          T corr = cvec[0] * (T)s[0] + cvec[1] * (T)s[1] + cvec[2] * (T)s[2];
          T base = (j == P ? -nu * (T)alpha : T(0.0)) + (j == N ? nu * (T)alpha : T(0.0));
          T valP = base + corr;
          add(idx(P, q), idx(j, q), valP);
          add(idx(N, q), idx(j, q), -valP);
        }
        // pressure force: -p_f S_q with p_f = 0.5(pP + pN)   (R_mom = ... - sum p_f S)
        const double Sq = (q == 0 ? f.S.x : (q == 1 ? f.S.y : f.S.z));
        add(idx(P, q), idx(P, 3), (T)(-0.5 * Sq));
        add(idx(P, q), idx(N, 3), (T)(-0.5 * Sq));
        add(idx(N, q), idx(P, 3), (T)(0.5 * Sq));
        add(idx(N, q), idx(N, 3), (T)(0.5 * Sq));
        // const part from -nu*kCorr . 0.5(tP + tN)
        Vec3 tP{Gq.cells[P].rhsBoundary[0], Gq.cells[P].rhsBoundary[1], Gq.cells[P].rhsBoundary[2]};
        Vec3 tN{Gq.cells[N].rhsBoundary[0], Gq.cells[N].rhsBoundary[1], Gq.cells[N].rhsBoundary[2]};
        T c0 = (cvec[0] * (T)(tP.x + tN.x) + cvec[1] * (T)(tP.y + tN.y) + cvec[2] * (T)(tP.z + tN.z)) * (T)0.5;
        constVec[idx(P, q)] += c0;
        constVec[idx(N, q)] -= c0;
      } else {
        // boundary face: Dirichlet U_B (known), pressure p_B (known)
        const T Ub = (T)uBq;
        const Vec3 kCorrB = useCorr ? g.kCorr : Vec3{};
        // viscous term: nu [ alpha (Uq_B - Uq_P) + kCorr . g_P ]
        std::array<T, 3> cvec{+nu * (T)kCorrB.x, +nu * (T)kCorrB.y, +nu * (T)kCorrB.z};
        add(idx(P, q), idx(P, q), -nu * (T)alpha);
        std::vector<int> js{P};
        for (const auto& pr : Gq.cells[P].cols) js.push_back(pr.first);
        std::sort(js.begin(), js.end()); js.erase(std::unique(js.begin(), js.end()), js.end());
        for (int j : js) {
          std::array<double, 3> lp{0, 0, 0};
          bool hp = false; Lvec(Gq, P, j, lp, hp);
          if (!hp) continue;
          add(idx(P, q), idx(j, q), cvec[0] * (T)lp[0] + cvec[1] * (T)lp[1] + cvec[2] * (T)lp[2]);
        }
        Vec3 tP{Gq.cells[P].rhsBoundary[0], Gq.cells[P].rhsBoundary[1], Gq.cells[P].rhsBoundary[2]};
        double pB = pStokes(f.xf.x, f.xf.y, f.xf.z);
        double Sq = (q == 0 ? f.S.x : (q == 1 ? f.S.y : f.S.z));
        T c0 = +nu * (T)alpha * Ub
             + cvec[0] * (T)tP.x + cvec[1] * (T)tP.y + cvec[2] * (T)tP.z
             - (T)pB * (T)Sq;
        constVec[idx(P, q)] += c0;
      }
    }
    (void)Gq;
  }
  // ---------- continuity rows (Rhie-Chow face flux) ----------
  for (size_t fi = 0; fi < M.faces.size(); ++fi) {
    const Face& f = M.faces[fi];
    const FaceG& g = C.fg[fi];
    const int P = f.cellP;
    if (f.cellN >= 0) {
      const int N = f.cellN;
      const Vec3 d = g.d;
      double dDotS = g.dDotS;
      double dNorm2 = d.norm2();
      // RC coefficient: C_f = V_f / (NU_REF * sumAlpha)   (frozen reference viscosity)
      double Vavg = 0.5 * (M.cells[P].volume + M.cells[N].volume);
      double asum = 0.5 * (sumAlpha[P] + sumAlpha[N]);
      double Cf = Vavg / (NU_REF * std::max(asum, 1e-30));
      double kd = dDotS / std::max(dNorm2, 1e-30);   // (d.S)/|d|^2
      T CfT(Cf), kdT(kd);
      // 0.5 (U_P + U_N) . S
      for (int q = 0; q < 3; ++q) {
        double Sq = (q == 0 ? f.S.x : (q == 1 ? f.S.y : f.S.z));
        add(idx(P, 3), idx(P, q), (T)(0.5 * Sq));
        add(idx(P, 3), idx(N, q), (T)(0.5 * Sq));
        add(idx(N, 3), idx(P, q), (T)(-0.5 * Sq));
        add(idx(N, 3), idx(N, q), (T)(-0.5 * Sq));
      }
      // pressure direct: -Cf*kd (pN - pP)  =>  col pP += Cf kd ; col pN -= Cf kd
      T coef = CfT * kdT;
      add(idx(P, 3), idx(P, 3), coef);
      add(idx(P, 3), idx(N, 3), -coef);
      add(idx(N, 3), idx(P, 3), -coef);
      add(idx(N, 3), idx(N, 3), coef);
      // gradient coupling: + Cf kd * d . gbar_p ; gbar_p = 0.5 (gp + gn)
      const GradientData& Gp = GS.p;
      std::vector<int> js{P, N};
      for (const auto& pr : Gp.cells[P].cols) js.push_back(pr.first);
      for (const auto& pr : Gp.cells[N].cols) js.push_back(pr.first);
      std::sort(js.begin(), js.end()); js.erase(std::unique(js.begin(), js.end()), js.end());
      Vec3 tP{Gp.cells[P].rhsBoundary[0], Gp.cells[P].rhsBoundary[1], Gp.cells[P].rhsBoundary[2]};
      Vec3 tN{Gp.cells[N].rhsBoundary[0], Gp.cells[N].rhsBoundary[1], Gp.cells[N].rhsBoundary[2]};
      double dtp = d.dot(tP), dtn = d.dot(tN);
      for (int j : js) {
        std::array<double, 3> lp{0, 0, 0}, ln{0, 0, 0};
        bool hp = false, hn = false;
        Lvec(Gp, P, j, lp, hp); Lvec(Gp, N, j, ln, hn);
        double sP = hp ? d.dot(Vec3{lp[0], lp[1], lp[2]}) : 0.0;
        double sN = hn ? d.dot(Vec3{ln[0], ln[1], ln[2]}) : 0.0;
        T corr = coef * (T)(0.5 * (sP + sN));
        add(idx(P, 3), idx(j, 3), corr);
        add(idx(N, 3), idx(j, 3), -corr);
      }
      // const from gradient constants: coef*0.5*(d.tP + d.tN)
      T c0 = coef * (T)(0.5 * (dtp + dtn));
      constVec[idx(P, 3)] += c0;
      constVec[idx(N, 3)] -= c0;
    } else {
      // boundary: Dirichlet velocity U_B -> known face flux, moved to RHS
      Vec3 U = uExact(f.xf.x, f.xf.y, f.xf.z);
      T c0 = (T)U.dot(f.S);
      constVec[idx(P, 3)] += c0;
    }
  }
  // ---------- finalize rhs ----------
  for (int c = 0; c < nc; ++c) {
    Vec3 fc = fStokes(M.cells[c].centroid.x, M.cells[c].centroid.y, M.cells[c].centroid.z, NU_REF);
    double V = M.cells[c].volume;
    out.rhs[idx(c, 0)] = sp.sigma * (T)(fc.x * V) - constVec[idx(c, 0)];
    out.rhs[idx(c, 1)] = sp.sigma * (T)(fc.y * V) - constVec[idx(c, 1)];
    out.rhs[idx(c, 2)] = sp.sigma * (T)(fc.z * V) - constVec[idx(c, 2)];
    out.rhs[idx(c, 3)] = T(0.0) - constVec[idx(c, 3)];
  }
  return out;
}

inline RCSystemOut runRCAdjoint(const DistortedMesh& M, const RCGradSet& GS, const MeshCache& C,
                                const QuadData& Q) {
  RCSystemOut R;
  R.nCells = (int)M.cells.size();
  R.dim = 4 * R.nCells;
  const double th[2] = {R.thetaTest[0], R.thetaTest[1]};
  auto Jof = [&](const std::vector<double>& x) {
    double J = 0.0;
    for (int c = 0; c < R.nCells; ++c) {
      double V = M.cells[c].volume;
      Vec3 U{x[4 * c], x[4 * c + 1], x[4 * c + 2]};
      double du0 = U.x - Q.cells[c].pStarU.x, du1 = U.y - Q.cells[c].pStarU.y, du2 = U.z - Q.cells[c].pStarU.z;
      double dp = x[4 * c + 3] - Q.cells[c].pStarScalar;
      J += V * (du0 * du0 + du1 * du1 + du2 * du2 + dp * dp);
    }
    return J;
  };
  auto solveReal = [&](double nu, double sig) {
    RCParams<double> sp{nu, sig};
    RCSys<double> S = assembleRC<double>(M, GS, C, Q, sp, true);
    std::vector<double> A((size_t)S.dim * S.dim, 0.0);
    for (auto& [r, c, v] : S.triplets) A[(size_t)r * S.dim + c] += v;
    std::vector<int> perm;
    if (!luFactor(A, S.dim, perm)) throw std::runtime_error("RC LU failed");
    std::vector<double> x = luSolve(A, perm, S.dim, S.rhs);
    return std::pair<std::vector<double>, double>{x, Jof(x)};
  };
  auto solveComplex = [&](const std::array<CD, 2>& thc) {
    RCParams<CD> sp{thc[0], thc[1]};
    RCSys<CD> S = assembleRC<CD>(M, GS, C, Q, sp, true);
    std::vector<CD> A((size_t)S.dim * S.dim, CD(0.0, 0.0));
    for (auto& [r, c, v] : S.triplets) A[(size_t)r * S.dim + c] += v;
    std::vector<int> perm;
    if (!luFactor(A, S.dim, perm)) throw std::runtime_error("RC LU failed (complex)");
    std::vector<CD> x = luSolve(A, perm, S.dim, S.rhs);
    CD J = 0.0;
    for (int c = 0; c < R.nCells; ++c) {
      double V = M.cells[c].volume;
      CD du0 = x[4 * c] - Q.cells[c].pStarU.x, du1 = x[4 * c + 1] - Q.cells[c].pStarU.y, du2 = x[4 * c + 2] - Q.cells[c].pStarU.z;
      CD dp = x[4 * c + 3] - Q.cells[c].pStarScalar;
      J += (CD)V * (du0 * du0 + du1 * du1 + du2 * du2 + dp * dp);
    }
    return J;
  };
  auto [x0, J0] = solveReal(th[0], th[1]);
  R.J0 = J0;
  // adjoint
  const int d = R.dim;
  std::vector<double> dJdx(d, 0.0);
  for (int c = 0; c < R.nCells; ++c) {
    double V = M.cells[c].volume;
    dJdx[4 * c] = 2 * V * (x0[4 * c] - Q.cells[c].pStarU.x);
    dJdx[4 * c + 1] = 2 * V * (x0[4 * c + 1] - Q.cells[c].pStarU.y);
    dJdx[4 * c + 2] = 2 * V * (x0[4 * c + 2] - Q.cells[c].pStarU.z);
    dJdx[4 * c + 3] = 2 * V * (x0[4 * c + 3] - Q.cells[c].pStarScalar);
  }
  RCSys<double> S0 = assembleRC<double>(M, GS, C, Q, RCParams<double>{th[0], th[1]}, true);
  std::vector<double> A((size_t)d * d, 0.0);
  for (auto& [r, c, v] : S0.triplets) A[(size_t)r * d + c] += v;
  std::vector<int> perm; luFactor(A, d, perm);
  std::vector<double> lambda = luSolveT(A, perm, d, dJdx);
  // analytic derivatives, exact by construction (A affine in nu with frozen RC coeff):
  //   A(nu,sigma) = A0 + nu * (A(1,0) - A(0,0)) ;  dA/dsigma = 0
  //   b(nu,sigma) = sigma * fhat*V - constVec(nu) ; constVec linear in nu
  //   db/dnu  = b(1,0) - b(0,0) ;  db/dsigma = b(0,1) - b(0,0)
  RCSys<double> S10 = assembleRC<double>(M, GS, C, Q, RCParams<double>{1.0, 0.0}, true);
  RCSys<double> S00 = assembleRC<double>(M, GS, C, Q, RCParams<double>{0.0, 0.0}, true);
  RCSys<double> S01 = assembleRC<double>(M, GS, C, Q, RCParams<double>{0.0, 1.0}, true);
  auto matVec = [&](const RCSys<double>& S, const std::vector<double>& x) {
    std::vector<double> y(d, 0.0);
    for (auto& [r, c, v] : S.triplets) y[r] += v * x[c];
    return y;
  };
  auto dotp = [&](const std::vector<double>& a, const std::vector<double>& b) {
    double s = 0; for (int i = 0; i < d; ++i) s += a[i] * b[i]; return s;
  };
  std::vector<double> dAPhi = matVec(S10, x0);
  std::vector<double> dAPhi0 = matVec(S00, x0);
  for (int i = 0; i < d; ++i) dAPhi[i] -= dAPhi0[i];
  std::vector<double> dbNu(d), dbSig(d);
  for (int i = 0; i < d; ++i) {
    dbNu[i] = S10.rhs[i] - S00.rhs[i];
    dbSig[i] = S01.rhs[i] - S00.rhs[i];
  }
  R.adjoint[0] = dotp(lambda, dbNu) - dotp(lambda, dAPhi);
  R.adjoint[1] = dotp(lambda, dbSig);
  // complex step
  const double hcs = 1e-30;
  for (int k = 0; k < 2; ++k) {
    std::array<CD, 2> thc{CD(th[0], 0), CD(th[1], 0)};
    thc[k] = CD(th[k], hcs);
    CD Jc = solveComplex(thc);
    R.complexStep[k] = Jc.imag() / hcs;
  }
  const double fdEps = 1e-6;
  for (int k = 0; k < 2; ++k) {
    double tp[2] = {th[0], th[1]}, tm[2] = {th[0], th[1]};
    tp[k] += fdEps; tm[k] -= fdEps;
    double Jp = solveReal(tp[0], tp[1]).second, Jm = solveReal(tm[0], tm[1]).second;
    R.fd[k] = (Jp - Jm) / (2.0 * fdEps);
  }
  double jref = std::max(std::fabs(R.adjoint[0]), std::fabs(R.adjoint[1]));
  for (int k = 0; k < 2; ++k) {
    R.relErrCS[k] = std::fabs(R.complexStep[k] - R.adjoint[k]) / std::max(jref, 1e-30);
    R.relErrFD[k] = std::fabs(R.fd[k] - R.adjoint[k]) / std::max(jref, 1e-30);
  }
  // Taylor remainder
  auto dirs = randomUnitDirections(2, 10, 0xD1B54A32DULL);
  const std::vector<double> epsList{1e-2, 1e-3, 1e-4, 1e-5, 1e-6, 1e-7};
  R.taylor.epsList = epsList;
  double maxRes = 0, sumSlope = 0, minSlope = 1e30;
  for (const auto& v : dirs) {
    double gv = R.adjoint[0] * v[0] + R.adjoint[1] * v[1];
    std::vector<double> row;
    std::vector<std::pair<double, double>> pts;
    for (double eps : epsList) {
      double Jp = solveReal(th[0] + eps * v[0], th[1] + eps * v[1]).second;
      double r1 = std::fabs(Jp - R.J0 - eps * gv);
      row.push_back(r1);
      maxRes = std::max(maxRes, r1);
      if (r1 > 0.0) pts.emplace_back(std::log10(eps), std::log10(r1));
    }
    double sx = 0, sy = 0, sxx = 0, sxy = 0; int m = (int)pts.size();
    for (auto& [a, b] : pts) { sx += a; sy += b; sxx += a * a; sxy += a * b; }
    double slope = (m >= 2) ? (m * sxy - sx * sy) / (m * sxx - sx * sx) : 0.0;
    R.taylor.slope.push_back(slope);
    sumSlope += slope; minSlope = std::min(minSlope, slope);
    R.taylor.resid.push_back(row);
  }
  R.taylor.meanSlope = sumSlope / (double)dirs.size();
  R.taylor.minSlope = minSlope;
  R.taylor.maxResid = maxRes;
  return R;
}

}  // namespace vv
