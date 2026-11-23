// =============================================================================
// cluster_a.hpp -- Cluster A of the Multiphysics Suite
//
//   * Positivity-preserving k-omega SST (Menter 2003): F1/F2 blending, cross
//     diffusion, Bradshaw-limited eddy viscosity, production limiter, and
//     Patankar point-implicit destruction (k>0, omega>0 unconditionally).
//   * LES WALE subgrid viscosity using the traceless squared-gradient tensor S^d.
//   * Continuous All-y+ wall treatment (Kader/Menter blend, u+ and T+),
//     wall-normal distance, wall shear / heat flux, blended omega_P.
//   * CHT harmonic-resistance interface flux and interface temperature,
//     monolithic 1D unified energy matrix, and Robin-Robin partitioned
//     under-relaxed iteration.
//   * Anisotropic Darcy-Forchheimer tensors D = R diag(d) R^T, F = R diag(f) R^T.
//   * P1 radiation: -div(Gamma grad G) + a G = 4 a n^2 sigma T^4 with Marshak BC.
//
// Compile:  g++ -std=c++20 -O2 -Wall -Wextra -pedantic cpp/verify.cpp -o verify
// CUDA:     every scalar kernel is CA_HD (__host__ __device__ under nvcc).
//           Under g++ the macro expands to nothing, so the same source is used.
// =============================================================================
#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <stdexcept>
#include <vector>

#if defined(__CUDACC__)
#define CA_HD __host__ __device__
#else
#define CA_HD
#endif

namespace cfd::clusterA {

inline constexpr double kPi = 3.14159265358979323846;

using Vec3 = std::array<double, 3>;
using Mat3 = std::array<std::array<double, 3>, 3>;

// ----------------------------------------------------------------------------
// Small dense linear algebra (3x3 / 3-vectors)
// ----------------------------------------------------------------------------
CA_HD inline double dot(const Vec3& a, const Vec3& b) {
    return a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
}
CA_HD inline double norm(const Vec3& a) { return std::sqrt(dot(a, a)); }
CA_HD inline Vec3 sub(const Vec3& a, const Vec3& b) {
    return {a[0] - b[0], a[1] - b[1], a[2] - b[2]};
}
CA_HD inline Vec3 matvec(const Mat3& A, const Vec3& x) {
    Vec3 y{};
    for (int i = 0; i < 3; ++i)
        y[i] = A[i][0] * x[0] + A[i][1] * x[1] + A[i][2] * x[2];
    return y;
}
CA_HD inline Mat3 matmul(const Mat3& A, const Mat3& B) {
    Mat3 C{};
    for (int i = 0; i < 3; ++i)
        for (int j = 0; j < 3; ++j) {
            double s = 0.0;
            for (int k = 0; k < 3; ++k) s += A[i][k] * B[k][j];
            C[i][j] = s;
        }
    return C;
}
CA_HD inline Mat3 transpose(const Mat3& A) {
    Mat3 T{};
    for (int i = 0; i < 3; ++i)
        for (int j = 0; j < 3; ++j) T[i][j] = A[j][i];
    return T;
}
CA_HD inline double trace(const Mat3& A) { return A[0][0] + A[1][1] + A[2][2]; }

// Anisotropic tensor  D = R diag(d) R^T  (R orthonormal: principal axes = columns of R)
CA_HD inline Mat3 orientedTensor(const Mat3& R, const Vec3& d) {
    Mat3 Dd{};
    for (int i = 0; i < 3; ++i) Dd[i][i] = d[i];
    return matmul(matmul(R, Dd), transpose(R));
}

// Rotation matrix from intrinsic Z-Y-Z Euler angles (radians).
CA_HD inline Mat3 rotationZYZ(double a, double b, double c) {
    const double ca = std::cos(a), sa = std::sin(a);
    const double cb = std::cos(b), sb = std::sin(b);
    const double cc = std::cos(c), sc = std::sin(c);
    Mat3 Rz1{{{ca, -sa, 0}, {sa, ca, 0}, {0, 0, 1}}};
    Mat3 Ry{{{cb, 0, sb}, {0, 1, 0}, {-sb, 0, cb}}};
    Mat3 Rz2{{{cc, -sc, 0}, {sc, cc, 0}, {0, 0, 1}}};
    return matmul(matmul(Rz1, Ry), Rz2);
}

// Thomas algorithm: a = sub-diagonal (a[0] unused), b = diagonal, c = super-diagonal.
inline std::vector<double> thomas(std::vector<double> a, std::vector<double> b,
                                  std::vector<double> c, std::vector<double> d) {
    const std::size_t n = b.size();
    if (n == 0 || a.size() != n || c.size() != n || d.size() != n)
        throw std::invalid_argument("thomas: size mismatch");
    for (std::size_t i = 1; i < n; ++i) {
        const double m = a[i] / b[i - 1];
        b[i] -= m * c[i - 1];
        d[i] -= m * d[i - 1];
    }
    std::vector<double> x(n);
    x[n - 1] = d[n - 1] / b[n - 1];
    for (std::size_t i = n - 1; i-- > 0;) x[i] = (d[i] - c[i] * x[i + 1]) / b[i];
    return x;
}

// ============================================================================
// 1. k-omega SST (Menter 2003)
// ============================================================================
struct SSTConstants {
    double betaStar = 0.09;
    double a1 = 0.31;
    double kappa = 0.41;
    double sigK1 = 0.85, sigW1 = 0.5, beta1 = 0.075;
    double sigK2 = 1.0, sigW2 = 0.856, beta2 = 0.0828;
};

// Local flow/mesh data needed by the blending functions (kinematic units).
struct SSTLocal {
    double k;             // turbulent kinetic energy   [m^2/s^2]
    double omega;         // specific dissipation       [1/s]
    double nu;            // kinematic viscosity        [m^2/s]
    double S;             // strain magnitude sqrt(2 S_ij S_ij)  [1/s]
    double y;             // wall distance              [m]
    double gradKdotGradW; // grad k . grad omega        [1/m^4 * m^2/s^2 ...]
};

// Blending function F1 (near-wall = 1, free stream = 0).
CA_HD inline double sstF1(const SSTLocal& f, const SSTConstants& c) {
    const double w = std::max(f.omega, 1e-300);
    const double y = std::max(f.y, 1e-300);
    const double y2 = y * y;
    const double sk = std::sqrt(std::max(f.k, 0.0));
    const double cdkw = std::max(2.0 * c.sigW2 * f.gradKdotGradW / w, 1e-10);
    const double arg1 = std::min(std::max(sk / (c.betaStar * w * y), 500.0 * f.nu / (y2 * w)),
                                 4.0 * c.sigW2 * f.k / (cdkw * y2));
    const double a4 = arg1 * arg1 * arg1 * arg1;
    return std::tanh(a4);
}

// Blending function F2 (used by the Bradshaw-limited eddy viscosity).
CA_HD inline double sstF2(const SSTLocal& f, const SSTConstants& c) {
    const double w = std::max(f.omega, 1e-300);
    const double y = std::max(f.y, 1e-300);
    const double sk = std::sqrt(std::max(f.k, 0.0));
    const double arg2 = std::max(2.0 * sk / (c.betaStar * w * y), 500.0 * f.nu / (y * y * w));
    const double t = std::tanh(arg2 * arg2);
    return t;
}

// Bradshaw-limited eddy viscosity  nu_t = a1 k / max(a1 omega, S F2)
CA_HD inline double sstEddyViscosity(double k, double omega, double S, double F2,
                                     const SSTConstants& c) {
    return c.a1 * std::max(k, 0.0) / std::max(c.a1 * omega, S * F2);
}

// Production limiter  P_k = min(tau:grad u, 10 beta* k omega),  tau:grad u = nu_t S^2
CA_HD inline double sstProductionLimited(double nut, double S, double k, double omega,
                                         const SSTConstants& c) {
    return std::min(nut * S * S, 10.0 * c.betaStar * k * omega);
}

// Blended model coefficient phi = F1 phi1 + (1-F1) phi2.
CA_HD inline double blend(double F1, double phi1, double phi2) {
    return F1 * phi1 + (1.0 - F1) * phi2;
}

struct KOmegaUpdate {
    double k;
    double omega;
};

// Patankar point-implicit update of one cell for k and omega (per unit volume times V/dt).
//   k-equation:      (V/dt + beta* omega_0) k     = (V/dt) k_0 + P_k V
//   omega-equation:  (V/dt + 2 beta omega_0) w    = (V/dt) w_0 + gamma S^2 V + CD V
// Destruction is linearized implicitly; positive production goes to the RHS.
// Because the diagonal is positive and the RHS is non-negative, k,omega > 0 for ANY dt.
// A negative cross-diffusion term is moved to the diagonal (Patankar), never the RHS.
CA_HD inline KOmegaUpdate patankarKOmegaStep(double k0, double w0, double Pk, double Pw_prod,
                                             double crossDiff, double betaCoef, double VoverDt,
                                             const SSTConstants& c) {
    const double w = std::max(w0, 1e-300);
    const double k_src = std::max(k0, 0.0) * VoverDt + std::max(Pk, 0.0);
    const double kDiag = VoverDt + c.betaStar * w;             // beta* omega_P V added to aPP
    double wRhs = std::max(w0, 0.0) * VoverDt + std::max(Pw_prod, 0.0);
    double wDiag = VoverDt + 2.0 * betaCoef * w;               // 2 beta omega_P V (omega^2 linearized)
    // Cross-diffusion: positive -> RHS, negative -> implicit diagonal (Patankar).
    if (crossDiff >= 0.0) {
        wRhs += crossDiff;
    } else {
        wDiag += -crossDiff / w;
    }
    KOmegaUpdate out{};
    out.k = k_src / kDiag;
    out.omega = wRhs / wDiag;
    return out;
}

// Explicit (non-positivity-preserving) update used only for comparison.
CA_HD inline KOmegaUpdate explicitKOmegaStep(double k0, double w0, double Pk, double Pw_prod,
                                             double dt, double betaCoef, const SSTConstants& c) {
    return {k0 + dt * (Pk - c.betaStar * w0 * k0),
            w0 + dt * (Pw_prod - betaCoef * w0 * w0)};
}

// ============================================================================
// 2. LES WALE subgrid-scale viscosity
// ============================================================================
// g[i][j] = du_i/dx_j.  Returns nu_sgs = (Cw Delta)^2 (Sd:Sd)^{3/2} /
//                                      ((S:S)^{5/2} + (Sd:Sd)^{5/4}).
CA_HD inline double waleNuSgs(const Mat3& g, double delta, double Cw = 0.325) {
    const Mat3 g2 = matmul(g, g);             // squared velocity gradient tensor
    const double tr2 = trace(g2);
    double SS = 0.0, SdSd = 0.0;
    for (int i = 0; i < 3; ++i)
        for (int j = 0; j < 3; ++j) {
            const double S_ij = 0.5 * (g[i][j] + g[j][i]);
            const double Sd_ij = 0.5 * (g2[i][j] + g2[j][i]) - ((i == j) ? tr2 / 3.0 : 0.0);
            SS += S_ij * S_ij;
            SdSd += Sd_ij * Sd_ij;
        }
    const double num = std::pow(SdSd, 1.5);
    const double den = std::pow(SS, 2.5) + std::pow(SdSd, 1.25);
    if (den <= 1e-300) return 0.0;
    const double cd = Cw * delta;
    return cd * cd * num / den;
}

// ============================================================================
// 3. Continuous All-y+ wall treatment (Kader / Menter blend)
// ============================================================================
struct WallLaw {
    double kappa = 0.41;
    double E = 9.8;
    double Pr = 0.71;
    double Prt = 0.85;
    double beta1 = 0.075;
    double betaStar = 0.09;
};

// Blending parameter Gamma(y+) = 0.01 y+^4 / (1 + 5 y+)
CA_HD inline double kaderGamma(double yp) {
    return 0.01 * yp * yp * yp * yp / (1.0 + 5.0 * yp);
}

// Kader blended velocity:  u+ = y+ e^{-G} + (1/kappa) ln(1 + E y+) e^{-1/G}
CA_HD inline double uPlus(double yp, const WallLaw& L) {
    if (yp <= 0.0) return 0.0;
    const double G = kaderGamma(yp);
    const double viscous = yp * std::exp(-G);
    const double log = (G > 0.0) ? std::log1p(L.E * yp) / L.kappa * std::exp(-1.0 / G) : 0.0;
    return viscous + log;
}

// Jayatilaka thermal sublayer function P(Pr, Prt).
CA_HD inline double jayatilakaP(const WallLaw& L) {
    const double r = L.Pr / L.Prt;
    return 9.24 * (std::pow(r, 0.75) - 1.0) * (1.0 + 0.28 * std::exp(-0.007 * r));
}

// Blended thermal law:  T+ = Pr y+ e^{-G} + [ (Prt/kappa) ln(1+E y+) + P ] e^{-1/G}
CA_HD inline double tPlus(double yp, const WallLaw& L) {
    if (yp <= 0.0) return 0.0;
    const double G = kaderGamma(yp);
    const double viscous = L.Pr * yp * std::exp(-G);
    const double log = (G > 0.0)
                           ? (L.Prt / L.kappa * std::log1p(L.E * yp) + jayatilakaP(L)) * std::exp(-1.0 / G)
                           : 0.0;
    return viscous + log;
}

// Wall-normal distance  y_P = (x_P - x_f) . S_f / |S_f|   (absolute value).
CA_HD inline double wallNormalDistance(const Vec3& xP, const Vec3& xf, const Vec3& Sf) {
    return std::abs(dot(sub(xP, xf), Sf)) / norm(Sf);
}

// Solve  U = u_tau * u+(y u_tau / nu)  for u_tau (monotone, bisection).
inline double solveUtau(double U, double y, double nu, const WallLaw& L) {
    if (U <= 0.0) return 0.0;
    auto f = [&](double ut) { return ut * uPlus(y * ut / nu, L) - U; };
    double lo = 0.0, hi = U;
    while (f(hi) < 0.0) hi *= 2.0;
    for (int it = 0; it < 200; ++it) {
        const double mid = 0.5 * (lo + hi);
        if (f(mid) < 0.0) lo = mid; else hi = mid;
        if (hi - lo <= 1e-14 * hi) break;
    }
    return 0.5 * (lo + hi);
}

struct WallState {
    double utau;    // friction velocity
    double yplus;   // y+ of the wall-adjacent cell
    double tauW;    // wall shear stress   rho u_tau^2
    double qW;      // wall heat flux (into fluid positive) rho cp u_tau (Tw - TP)/T+
    double omegaP;  // blended wall-cell omega  sqrt(omega_vis^2 + omega_log^2)
};

// Evaluates the All-y+ wall state for a wall-adjacent cell.  yMin prevents the
// singular division y_P -> 0 in omega_vis = 6 nu / (beta1 y^2).
inline WallState allYplusWall(double U, double yP, double nu, double rho, double cp,
                              double Tw, double TP, const WallLaw& L, double yMin = 1e-8) {
    const double y = std::max(std::abs(yP), yMin);
    WallState s{};
    s.utau = solveUtau(U, y, nu, L);
    s.yplus = y * s.utau / nu;
    s.tauW = rho * s.utau * s.utau;
    const double Tp = tPlus(s.yplus, L);
    s.qW = (Tp > 0.0) ? rho * cp * s.utau * (Tw - TP) / Tp : 0.0;
    const double wVis = 6.0 * nu / (L.beta1 * y * y);
    const double wLog = s.utau / (std::sqrt(L.betaStar) * L.kappa * y);
    s.omegaP = std::sqrt(wVis * wVis + wLog * wLog);
    return s;
}

// ============================================================================
// 4. Conjugate heat transfer (harmonic resistance interface)
// ============================================================================
// Harmonic interface conductance  kappa_f alpha_f kappa_s alpha_s / (kappa_f alpha_f + kappa_s alpha_s)
CA_HD inline double harmonicConductance(double kf, double af, double ks, double as) {
    const double a = kf * af, b = ks * as;
    return a * b / (a + b);
}

// q_{f->s} = G [ (TS-TP) + grad Tf . kcorr_f / af + grad Ts . kcorr_s / as ]
CA_HD inline double chtInterfaceFlux(double TP, double TS, double kf, double af, double ks, double as,
                                     const Vec3& gradTf, const Vec3& kcorrF,
                                     const Vec3& gradTs, const Vec3& kcorrS) {
    const double G = harmonicConductance(kf, af, ks, as);
    return G * ((TS - TP) + dot(gradTf, kcorrF) / af + dot(gradTs, kcorrS) / as);
}

// T_interface = (kf af TP + ks as TS + dq_skew) / (kf af + ks as)
CA_HD inline double chtInterfaceTemp(double TP, double TS, double kf, double af, double ks,
                                     double as, double dqSkew = 0.0) {
    const double a = kf * af, b = ks * as;
    return (a * TP + b * TS + dqSkew) / (a + b);
}

// 1D conduction segments (fluid and solid share the same unified matrix).
struct Segment {
    double k;  // conductivity
    double L;  // length
    int n;     // number of cells
};

// Boundary condition: Dirichlet (T = value) or Robin (q = h (value - T)).
struct Boundary {
    bool dirichlet;
    double value;
    double h;
};
inline Boundary dirichletBC(double T) { return {true, T, 0.0}; }
inline Boundary robinBC(double h, double Tinf) { return {false, Tinf, h}; }

struct Solution1D {
    std::vector<double> x;  // cell centres
    std::vector<double> T;  // cell temperatures
    double qLeft;           // heat flux entering the domain at x=0
    double qRight;          // heat flux leaving the domain at x=L
};

namespace detail {
inline double bcConductance(const Boundary& b, double k, double dx) {
    if (b.dirichlet) return 2.0 * k / dx;
    return 1.0 / (dx / (2.0 * k) + 1.0 / b.h);
}
}  // namespace detail

// Monolithic 1D finite-volume conduction on a multi-material chain.
// Face conductance between cells i,i+1 = 1/(dx_i/(2k_i) + dx_{i+1}/(2k_{i+1})).
inline Solution1D solveConduction1D(const std::vector<Segment>& segs, Boundary left, Boundary right) {
    std::vector<double> k, dx;
    for (const auto& s : segs)
        for (int i = 0; i < s.n; ++i) {
            k.push_back(s.k);
            dx.push_back(s.L / s.n);
        }
    const std::size_t N = k.size();
    if (N == 0) throw std::invalid_argument("solveConduction1D: empty mesh");

    std::vector<double> a(N, 0.0), b(N, 0.0), c(N, 0.0), d(N, 0.0);
    for (std::size_t i = 0; i + 1 < N; ++i) {
        const double G = 1.0 / (dx[i] / (2.0 * k[i]) + dx[i + 1] / (2.0 * k[i + 1]));
        b[i] += G;
        b[i + 1] += G;
        c[i] -= G;
        a[i + 1] -= G;
    }
    const double GL = detail::bcConductance(left, k[0], dx[0]);
    const double GR = detail::bcConductance(right, k[N - 1], dx[N - 1]);
    b[0] += GL;
    d[0] += GL * left.value;
    b[N - 1] += GR;
    d[N - 1] += GR * right.value;

    Solution1D out;
    out.T = thomas(a, b, c, d);
    out.x.resize(N);
    double xs = 0.0;
    for (std::size_t i = 0; i < N; ++i) {
        out.x[i] = xs + 0.5 * dx[i];
        xs += dx[i];
    }
    out.qLeft = GL * (left.value - out.T[0]);
    out.qRight = GR * (out.T[N - 1] - right.value);
    return out;
}

struct PartitionedResult {
    Solution1D fluid;
    Solution1D solid;
    std::vector<double> residual;  // interface-temperature change per iteration
    int iterations = 0;
    bool converged = false;
};

// Robin-Robin partitioned CHT with Aitken-accelerated under-relaxation.
// Each region sees the other region's adjacent-cell temperature through a Robin
// BC whose conductance is that region's half-cell resistance, so the fixed point
// is identical to the monolithic matrix. The interface unknown is the solid trace
// x = T_s(interface); the map x -> g(x) is solved with an Aitken-accelerated
// relaxation  x_{k+1} = x_k + w_k r_k,  w_k = -w_{k-1} r_{k-1} / (r_k - r_{k-1}),
// with initial relaxation w_0 = theta.
inline PartitionedResult solvePartitionedRobinRobin(const std::vector<Segment>& fluidSegs,
                                                    Boundary fluidOuter,
                                                    const std::vector<Segment>& solidSegs,
                                                    Boundary solidOuter, double theta,
                                                    int maxIter, double tol) {
    const Segment& fl = fluidSegs.back();
    const Segment& sl = solidSegs.front();
    const double dxf = fl.L / fl.n;
    const double dxs = sl.L / sl.n;
    // Robin conductances: fluid side sees 1/(dxf/2kf + dxs/2ks) = harmonic face.
    const double hFluid = 2.0 * sl.k / dxs;  // fluid side, reference = solid first cell
    const double hSolid = 2.0 * fl.k / dxf;  // solid side, reference = fluid last cell

    PartitionedResult R;
    double x = solidOuter.value;  // interface solid-trace guess
    double w = theta, rPrev = 0.0;
    for (int it = 0; it < maxIter; ++it) {
        R.fluid = solveConduction1D(fluidSegs, fluidOuter, robinBC(hFluid, x));
        const double TfNew = R.fluid.T.back();
        R.solid = solveConduction1D(solidSegs, robinBC(hSolid, TfNew), solidOuter);
        const double g = R.solid.T.front();
        const double r = g - x;
        R.residual.push_back(std::abs(r));
        R.iterations = it + 1;
        if (std::abs(r) < tol) {
            R.converged = true;
            break;
        }
        if (it > 0 && std::abs(r - rPrev) > 1e-300) w = -w * rPrev / (r - rPrev);
        rPrev = r;
        x += w * r;
    }
    return R;
}

// ============================================================================
// 5. Anisotropic Darcy-Forchheimer porous zones
// ============================================================================
// Momentum sink per unit volume:  S_u = -(mu D + 0.5 rho |u| F) u
CA_HD inline Vec3 porousSink(const Mat3& D, const Mat3& F, const Vec3& u, double mu, double rho) {
    const double umag = norm(u);
    Mat3 M{};
    for (int i = 0; i < 3; ++i)
        for (int j = 0; j < 3; ++j) M[i][j] = mu * D[i][j] + 0.5 * rho * umag * F[i][j];
    const Vec3 Mu = matvec(M, u);
    return {-Mu[0], -Mu[1], -Mu[2]};
}

// Solve 1D Darcy-Forchheimer  (mu/K) u + (rho C2 / 2) u^2 = |dp/dx| for u >= 0.
CA_HD inline double darcyForchheimerVelocity(double gradP, double mu, double rho, double K, double C2) {
    const double a = mu / K;
    const double b = 0.5 * rho * C2;
    if (gradP <= 0.0) return 0.0;
    if (b <= 0.0) return gradP / a;
    return (-a + std::sqrt(a * a + 4.0 * b * gradP)) / (2.0 * b);
}

// ============================================================================
// 6. P1 radiation with Marshak boundary conditions (1D finite volume)
// ============================================================================
// Gamma = 1 / (3 (a + sigma_s)).  Equation: -div(Gamma grad G) + a G = S,  S = 4 a n^2 sigma T^4.
// Marshak (epsilon = 1, derived from the half-range moment condition):
//        G + 2 Gamma dG/dn = G_w   on each wall (dn = outward normal).
// Discretely this is a boundary conductance  Gamma_b = 2 Gamma / (dx + 4 Gamma).
inline double p1Gamma(double a, double sigmaS = 0.0) { return 1.0 / (3.0 * (a + sigmaS)); }

inline std::vector<double> solveP1Slab1D(int N, double L, double a, double sigmaS,
                                         const std::vector<double>& Geq, double GwLeft,
                                         double GwRight) {
    if (static_cast<int>(Geq.size()) != N) throw std::invalid_argument("solveP1Slab1D: Geq size");
    const double dx = L / N;
    const double G = p1Gamma(a, sigmaS);
    const double Gb = 2.0 * G / (dx + 4.0 * G);
    std::vector<double> lo(N, 0.0), di(N, 0.0), up(N, 0.0), rhs(N, 0.0);
    for (int i = 0; i < N; ++i) {
        di[i] = a * dx;
        rhs[i] = a * Geq[i] * dx;   // S dx with S = a * G_eq
        if (i > 0) { di[i] += G / dx; lo[i] = -G / dx; }
        if (i + 1 < N) { di[i] += G / dx; up[i] = -G / dx; }
    }
    di[0] += Gb;  rhs[0] += Gb * GwLeft;
    di[N - 1] += Gb; rhs[N - 1] += Gb * GwRight;
    return thomas(lo, di, up, rhs);
}

// Closed-form solution for uniform G_eq, same Marshak walls (continuous).
inline double p1AnalyticUniform(double x, double L, double a, double sigmaS, double Geq,
                                double Gw) {
    const double G = p1Gamma(a, sigmaS);
    const double lam = std::sqrt(a / G);
    const double A = (Gw - Geq) / (std::cosh(lam * L / 2.0) + 2.0 * G * lam * std::sinh(lam * L / 2.0));
    return Geq + A * std::cosh(lam * (x - L / 2.0));
}

}  // namespace cfd::clusterA
