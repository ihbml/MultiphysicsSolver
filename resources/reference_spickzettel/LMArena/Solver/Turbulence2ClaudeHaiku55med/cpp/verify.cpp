// =============================================================================
// verify.cpp -- analytical verification suite for Cluster A.
//
//   g++ -std=c++20 -O2 -Wall -Wextra -pedantic cpp/verify.cpp -o verify
//   ./verify                       # human-readable report
//   ./verify --json out.json       # also writes machine-readable results
//
// Exit status is non-zero if any check fails.
// =============================================================================
#include "cluster_a.hpp"

#include <cstdio>
#include <cstring>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <string>

using namespace cfd::clusterA;

namespace {

struct Check {
    std::string group;
    std::string name;
    std::string expected;
    double measured;
    double reference;
    double tolerance;  // relative (if reference != 0) else absolute
    bool pass;
};

std::vector<Check> g_checks;

void check(const std::string& group, const std::string& name, const std::string& expected,
           double measured, double reference, double tol) {
    double err = (std::abs(reference) > 1e-30) ? std::abs(measured - reference) / std::abs(reference)
                                               : std::abs(measured - reference);
    g_checks.push_back({group, name, expected, measured, reference, tol, err <= tol});
}

void checkTrue(const std::string& group, const std::string& name, const std::string& expected,
               bool ok, double measured = 0.0) {
    g_checks.push_back({group, name, expected, measured, 0.0, 0.0, ok});
}

std::string jsonEscape(const std::string& s) {
    std::string o;
    for (char c : s) {
        if (c == '"' || c == '\\') o += '\\';
        o += c;
    }
    return o;
}

}  // namespace

int main(int argc, char** argv) {
    std::string jsonPath;
    for (int i = 1; i < argc; ++i)
        if (std::strcmp(argv[i], "--json") == 0 && i + 1 < argc) jsonPath = argv[++i];

    const SSTConstants sst;
    const WallLaw law;

    // ------------------------------------------------------------------ SST
    {
        SSTLocal nearWall{0.1, 1e4, 1e-5, 10.0, 1e-6, 0.0};
        SSTLocal farField{0.1, 1.0, 1e-5, 10.0, 10.0, 0.0};
        const double F1w = sstF1(nearWall, sst);
        const double F1f = sstF1(farField, sst);
        check("SST", "F1 near wall -> 1", "F1 > 0.999", F1w, 1.0, 1e-3);
        checkTrue("SST", "F1 far field small", "F1 < 0.05", F1f < 0.05, F1f);

        bool monotone = true, bounded = true;
        double prev = 2.0;
        for (int i = 0; i <= 200; ++i) {
            SSTLocal f{0.1, 1.0, 1e-5, 10.0, std::pow(10.0, -6.0 + 0.04 * i), 0.0};
            const double F1 = sstF1(f, sst);
            bounded = bounded && (F1 >= 0.0 && F1 <= 1.0);
            monotone = monotone && (F1 <= prev + 1e-12);
            prev = F1;
        }
        checkTrue("SST", "F1 in [0,1]", "bounded", bounded);
        checkTrue("SST", "F1 monotone in wall distance", "non-increasing", monotone);

        const double F2 = 1.0, S = 10.0, k = 2.0, w = 1.0;
        const double nut = sstEddyViscosity(k, w, S, F2, sst);
        check("SST", "Bradshaw-limited nu_t = a1 k/(S F2)", "0.062", nut, sst.a1 * k / (S * F2), 1e-12);

        const double Pk = sstProductionLimited(1000.0, S, 0.1, 1.0, sst);
        check("SST", "Production limiter caps P_k", "10 beta* k w", Pk, 10.0 * sst.betaStar * 0.1 * 1.0, 1e-12);
    }

    // ----------------------------------------- Positivity (Patankar vs explicit)
    {
        bool allPositive = true;
        double worst = 1e300;
        for (double dt : {1e-6, 1e-3, 1.0, 1e3, 1e6, 1e9}) {
            // V/dt = 1/dt: k^{n+1}, omega^{n+1} from a strongly destructive state
            KOmegaUpdate u = patankarKOmegaStep(1.0, 1.0, 0.0, 0.0, -5.0, sst.beta1, 1.0 / dt, sst);
            allPositive = allPositive && (u.k > 0.0) && (u.omega > 0.0) && std::isfinite(u.k) &&
                          std::isfinite(u.omega);
            worst = std::min(worst, std::min(u.k, u.omega));
        }
        checkTrue("Positivity", "Patankar k,omega > 0 for dt in [1e-6,1e9]", "min > 0", allPositive, worst);

        KOmegaUpdate e = explicitKOmegaStep(1.0, 1.0, 0.0, 0.0, 100.0, sst.beta1, sst);
        checkTrue("Positivity", "explicit Euler k goes negative (reference)", "k < 0 (fails)", e.k < 0.0, e.k);

        // Steady limit: V/dt -> 0 gives k = P_k / (beta* omega)
        KOmegaUpdate s = patankarKOmegaStep(0.0, 2.0, 0.5, 0.0, 0.0, sst.beta1, 1e-14, sst);
        check("Positivity", "steady limit k = Pk/(beta* w)", "0.5/(0.09*2)", s.k, 0.5 / (sst.betaStar * 2.0), 1e-6);
    }

    // ----------------------------------------------------------------- WALE
    {
        Mat3 shear{{{0, 2.0, 0}, {0, 0, 0}, {0, 0, 0}}};
        check("WALE", "pure shear -> nu_sgs = 0", "0", waleNuSgs(shear, 0.01), 0.0, 1e-12);
        Mat3 g{{{0.3, 1.2, -0.4}, {0.7, -0.1, 0.5}, {-0.2, 0.9, -0.2}}};
        const double n1 = waleNuSgs(g, 0.01);
        const double n2 = waleNuSgs(g, 0.02);
        check("WALE", "nu_sgs ~ Delta^2 scaling", "4.0", n2 / n1, 4.0, 1e-10);
        checkTrue("WALE", "nu_sgs >= 0 (general gradient)", "non-negative", n1 >= 0.0, n1);
    }

    // ------------------------------------------------------- All-y+ wall law
    {
        check("AllYplus", "u+(y+=1) -> 1 (viscous)", "~1", uPlus(1.0, law), 1.0, 0.01);
        check("AllYplus", "u+(y+=0.1) -> 0.1 (viscous)", "~0.1", uPlus(0.1, law), 0.1, 0.01);
        const double logLaw100 = std::log(law.E * 100.0) / law.kappa;
        check("AllYplus", "u+(y+=100) log-law", "(1/k) ln(E y+)", uPlus(100.0, law), logLaw100, 0.01);
        const double logLaw300 = std::log(law.E * 300.0) / law.kappa;
        check("AllYplus", "u+(y+=300) log-law", "(1/k) ln(E y+)", uPlus(300.0, law), logLaw300, 1e-3);
        const double logLaw50 = std::log(law.E * 50.0) / law.kappa;
        check("AllYplus", "u+(y+=50) log-law", "(1/k) ln(E y+)", uPlus(50.0, law), logLaw50, 0.02);

        // Inverse problem: recover u_tau from U = u_tau u+(y u_tau / nu)
        const double nu = 1.5e-5, y = 1.0e-3, utau = 0.6;
        const double U = utau * uPlus(y * utau / nu, law);
        check("AllYplus", "solve u_tau from velocity (round trip)", "0.6", solveUtau(U, y, nu, law), utau, 1e-9);

        // Smooth wall state for a range of y: no NaN, positive omega, y->0 finite.
        bool finite = true;
        for (double yy : {1e-12, 1e-9, 1e-6, 1e-4, 1e-2}) {
            WallState ws = allYplusWall(5.0, yy, nu, 1.2, 1005.0, 350.0, 300.0, law);
            finite = finite && std::isfinite(ws.omegaP) && ws.omegaP > 0.0 && std::isfinite(ws.qW);
        }
        checkTrue("AllYplus", "omega_P, q_w finite for y_P -> 0", "finite", finite);

        // Wall shear tau_w = rho u_tau^2 consistency with log-law:  U/u_tau = (1/k) ln(E y+)
        WallState ws = allYplusWall(10.0, 5e-4, 1.5e-5, 1.2, 1005.0, 350.0, 300.0, law);
        check("AllYplus", "tau_w = rho u_tau^2", "rho*utau^2", ws.tauW, 1.2 * ws.utau * ws.utau, 1e-12);

        const double dist = wallNormalDistance({0.0, 0.0, 0.01}, {0.0, 0.0, 0.0}, {0.0, 0.0, 2.0});
        check("AllYplus", "wall-normal distance y_P", "0.01", dist, 0.01, 1e-12);
    }

    // ------------------------------------------------------------------- CHT
    {
        const double df = 0.002, ds = 0.004, kf = 0.6, ks = 45.0;
        const double Gser = 1.0 / (df / kf + ds / ks);
        const double Gharm = harmonicConductance(kf, 1.0 / df, ks, 1.0 / ds);
        check("CHT", "harmonic conductance = series resistance", "1/(df/kf+ds/ks)", Gharm, Gser, 1e-12);

        Vec3 zero{0, 0, 0};
        const double qf = chtInterfaceFlux(300.0, 350.0, kf, 1.0 / df, ks, 1.0 / ds, zero, zero, zero, zero);
        check("CHT", "interface flux = G (TS-TP)", "G*50", qf, Gser * 50.0, 1e-12);
        const double Ti = chtInterfaceTemp(300.0, 350.0, kf, 1.0 / df, ks, 1.0 / ds);
        // Interface temperature must satisfy continuity of flux on both sides
        const double qFluid = (kf / df) * (Ti - 300.0);
        const double qSolid = (ks / ds) * (350.0 - Ti);
        check("CHT", "interface continuity q_f = q_s", "equal", qFluid, qSolid, 1e-10);

        // Composite wall: fluid (Robin inlet) | solid (Robin outlet)
        std::vector<Segment> fluid{{kf, 0.02, 40}};
        std::vector<Segment> solid{{ks, 0.005, 20}};
        const double h1 = 1000.0, Ta = 400.0, h2 = 50.0, Tb = 300.0;
        std::vector<Segment> all{{kf, 0.02, 40}, {ks, 0.005, 20}};
        Solution1D mono = solveConduction1D(all, robinBC(h1, Ta), robinBC(h2, Tb));
        const double qAn = (Ta - Tb) / (1.0 / h1 + 0.02 / kf + 0.005 / ks + 1.0 / h2);
        check("CHT", "monolithic composite q (Robin-Robin)", "analytic series", mono.qLeft, qAn, 1e-9);
        check("CHT", "monolithic q conservation (left=right)", "equal", mono.qRight, mono.qLeft, 1e-9);

        // Piecewise-linear analytic temperature (Dirichlet ends)
        Solution1D dir = solveConduction1D(all, dirichletBC(400.0), dirichletBC(300.0));
        const double Rtot = 0.02 / kf + 0.005 / ks;
        double maxErr = 0.0;
        for (std::size_t i = 0; i < dir.x.size(); ++i) {
            const double x = dir.x[i];
            double Tan;
            if (x <= 0.02) Tan = 400.0 - (100.0 * (0.02 / kf) / Rtot) * (x / 0.02);
            else Tan = 400.0 - 100.0 * (0.02 / kf) / Rtot - 100.0 * ((x - 0.02) / ks) / Rtot;
            maxErr = std::max(maxErr, std::abs(dir.T[i] - Tan));
        }
        check("CHT", "composite Dirichlet profile vs analytic (max |dT|)", "~0 K", maxErr, 0.0, 1e-8);

        // Robin-Robin partitioned, under-relaxed, vs monolithic
        PartitionedResult pr = solvePartitionedRobinRobin(fluid, robinBC(h1, Ta), solid, robinBC(h2, Tb),
                                                          0.7, 400, 1e-10);
        double maxDev = 0.0;
        for (std::size_t i = 0; i < pr.fluid.T.size(); ++i)
            maxDev = std::max(maxDev, std::abs(pr.fluid.T[i] - mono.T[i]));
        for (std::size_t i = 0; i < pr.solid.T.size(); ++i)
            maxDev = std::max(maxDev, std::abs(pr.solid.T[i] - mono.T[pr.fluid.T.size() + i]));
        checkTrue("CHT", "partitioned Robin-Robin converges", "converged", pr.converged, pr.iterations);
        check("CHT", "partitioned == monolithic (max |dT|)", "~0 K", maxDev, 0.0, 1e-6);
    }

    // ---------------------------------------------------------- Porous media
    {
        const Vec3 d{1000.0, 4000.0, 9000.0};          // 1/K along principal axes
        const Mat3 R = rotationZYZ(0.3, 0.7, 1.1);
        const Mat3 D = orientedTensor(R, d);
        check("Porous", "trace(R diag R^T) = sum d", "sum", trace(D), d[0] + d[1] + d[2], 1e-12);
        // Principal direction: D (R e_i) = d_i (R e_i)
        Vec3 ax{R[0][1], R[1][1], R[2][1]};
        Vec3 Dax = matvec(D, ax);
        check("Porous", "principal axis eigen-relation", "d_2", Dax[0] / ax[0], d[1], 1e-10);

        // Ergun packed bed <-> Darcy-Forchheimer parameters
        const double eps = 0.4, dp = 0.005, mu = 1.8e-5, rho = 1.2;
        const double K = std::pow(eps, 3) * dp * dp / (150.0 * std::pow(1.0 - eps, 2));
        const double C2 = 3.5 * (1.0 - eps) / (std::pow(eps, 3) * dp);
        const double u = 0.8;
        const double gradErgun = 150.0 * mu * std::pow(1.0 - eps, 2) / (std::pow(eps, 3) * dp * dp) * u +
                                 1.75 * rho * (1.0 - eps) / (std::pow(eps, 3) * dp) * u * u;
        const double uRec = darcyForchheimerVelocity(gradErgun, mu, rho, K, C2);
        check("Porous", "Ergun pressure drop -> velocity inversion", "0.8 m/s", uRec, u, 1e-10);

        // Darcy limit (C2 = 0): u = K dp/dx / mu
        const double uD = darcyForchheimerVelocity(2.0, mu, rho, 1e-8, 0.0);
        check("Porous", "Darcy limit u = K g / mu", "K g/mu", uD, 1e-8 * 2.0 / mu, 1e-12);

        // Sink along principal axis 1 for a flow aligned with it
        Mat3 F = orientedTensor(R, {10.0, 20.0, 30.0});
        Vec3 uAx{ax[0] * 2.0, ax[1] * 2.0, ax[2] * 2.0};
        Vec3 S = porousSink(D, F, uAx, mu, rho);
        const double expectedMag = (mu * d[1] + 0.5 * rho * 2.0 * 20.0) * 2.0;
        check("Porous", "sink along principal axis", "|(mu d + 0.5 rho |u| f) u|", norm(S), expectedMag, 1e-10);
    }

    // ----------------------------------------------------------- P1 radiation
    {
        const int N = 400;
        const double L = 1.0, a = 1.0, Geq = 0.5, Gw = 1.2;
        std::vector<double> src(N, Geq);
        std::vector<double> G = solveP1Slab1D(N, L, a, 0.0, src, Gw, Gw);
        double err = 0.0;
        for (int i = 0; i < N; ++i) {
            const double x = (i + 0.5) * L / N;
            err = std::max(err, std::abs(G[i] - p1AnalyticUniform(x, L, a, 0.0, Geq, Gw)));
        }
        check("P1", "Marshak slab max |G - analytic|", "< 1e-3", err, 0.0, 1e-3);

        std::vector<double> G0 = solveP1Slab1D(N, L, a, 0.0, std::vector<double>(N, Geq), Geq, Geq);
        double flat = 0.0;
        for (double g : G0) flat = std::max(flat, std::abs(g - Geq));
        check("P1", "equilibrium walls -> uniform G = 4 n^2 sigma T^4", "G = Geq", flat, 0.0, 1e-9);
    }

    // ------------------------------------------------------------ report
    int failed = 0;
    std::cout << "\nCluster A verification (g++ -std=c++20 -O2)\n";
    std::cout << std::string(96, '-') << "\n";
    std::cout << std::left << std::setw(12) << "group" << std::setw(50) << "check" << std::setw(22)
              << "expected" << std::right << std::setw(12) << "result" << "  status\n";
    std::cout << std::string(96, '-') << "\n";
    for (const auto& c : g_checks) {
        if (!c.pass) ++failed;
        std::cout << std::left << std::setw(12) << c.group << std::setw(50) << c.name << std::setw(22)
                  << c.expected << std::right << std::setw(12) << std::setprecision(6) << std::scientific
                  << c.measured << "  " << (c.pass ? "PASS" : "FAIL") << "\n";
        std::cout << std::defaultfloat;
    }
    std::cout << std::string(96, '-') << "\n";
    std::cout << (g_checks.size() - failed) << "/" << g_checks.size() << " checks passed\n";

    if (!jsonPath.empty()) {
        std::ofstream js(jsonPath);
        js << std::setprecision(10);
        js << "{\n  \"checks\": [\n";
        for (std::size_t i = 0; i < g_checks.size(); ++i) {
            const auto& c = g_checks[i];
            js << "    {\"group\":\"" << jsonEscape(c.group) << "\",\"name\":\"" << jsonEscape(c.name)
               << "\",\"expected\":\"" << jsonEscape(c.expected) << "\",\"measured\":" << c.measured
               << ",\"reference\":" << c.reference << ",\"tolerance\":" << c.tolerance
               << ",\"pass\":" << (c.pass ? "true" : "false") << "}" << (i + 1 < g_checks.size() ? "," : "")
               << "\n";
        }
        js << "  ],\n";

        // Datasets for the workbench: Kader u+/T+ curves and P1 profile.
        js << "  \"wallLaw\": [\n";
        for (int i = 0; i <= 60; ++i) {
            const double yp = std::pow(10.0, -1.0 + 3.7 * i / 60.0);  // 0.1 .. 500
            js << "    [" << yp << "," << uPlus(yp, law) << "," << tPlus(yp, law) << ","
               << std::log(law.E * yp) / law.kappa << "]" << (i < 60 ? "," : "") << "\n";
        }
        js << "  ],\n";

        const int N = 50;
        std::vector<double> src(N, 0.5);
        std::vector<double> G = solveP1Slab1D(N, 1.0, 1.0, 0.0, src, 1.2, 1.2);
        js << "  \"p1\": [";
        for (int i = 0; i < N; ++i) js << "[" << (i + 0.5) / N << "," << G[i] << "]" << (i + 1 < N ? "," : "");
        js << "],\n";

        Solution1D comp = solveConduction1D({{0.6, 0.02, 40}, {45.0, 0.005, 20}}, robinBC(1000.0, 400.0),
                                             robinBC(50.0, 300.0));
        js << "  \"composite\": [";
        for (std::size_t i = 0; i < comp.x.size(); ++i)
            js << "[" << comp.x[i] << "," << comp.T[i] << "]" << (i + 1 < comp.x.size() ? "," : "");
        js << "],\n";

        std::vector<Segment> fluid{{0.6, 0.02, 40}};
        std::vector<Segment> solid{{45.0, 0.005, 20}};
        PartitionedResult pr = solvePartitionedRobinRobin(fluid, robinBC(1000.0, 400.0), solid,
                                                          robinBC(50.0, 300.0), 0.7, 400, 1e-10);
        js << "  \"partitionedResidual\": [";
        for (std::size_t i = 0; i < pr.residual.size(); ++i)
            js << pr.residual[i] << (i + 1 < pr.residual.size() ? "," : "");
        js << "],\n  \"failed\": " << failed << "\n}\n";
        std::cout << "wrote " << jsonPath << "\n";
    }

    return failed == 0 ? 0 : 1;
}
