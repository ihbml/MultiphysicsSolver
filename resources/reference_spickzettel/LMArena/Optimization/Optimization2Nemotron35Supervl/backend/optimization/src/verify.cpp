// verify.cpp
// Mathematical verification of the MDO Phase 4 C++20 backend.
// Build: g++ -std=c++20 -O2 -I include src/verify.cpp -o verify
#include "mdo_math.hpp"
#include "mdo_chain.hpp"
#include "mdo_opt1.hpp"
#include "mdo_opt2.hpp"
#include <complex>
#include <random>
#include <iomanip>
#include <iostream>
#include <cmath>

using namespace mdo;

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

static int g_failures = 0;
static inline void check(bool ok, const char* name) {
    std::cout << std::setw(52) << std::left << name << ": "
              << (ok ? "PASS" : "FAIL") << "\n";
    if (!ok) g_failures++;
}

// ---- simple 1D chain FVM graph ----
static FVMGraph make_chain_graph(int n, double L) {
    FVMGraph g; g.n = n;
    double dx = L / (double)(n - 1);
    g.vol.assign(n, dx);
    g.adj.assign(n, {});
    g.face_area.assign(n, {});
    g.dist.assign(n, {});
    for (int i = 0; i < n; ++i) {
        if (i > 0) { g.adj[i].push_back(i - 1); g.face_area[i].push_back(1.0); g.dist[i].push_back(dx); }
        if (i < n - 1) { g.adj[i].push_back(i + 1); g.face_area[i].push_back(1.0); g.dist[i].push_back(dx); }
    }
    return g;
}

int main() {
    std::cout << "=== MDO Phase 4: C++20 Backend Verification ===\n\n";

    // =====================================================================
    // TEST 1: Helmholtz filter + Heaviside + VJP chain rule vs complex-step
    // Target: max |dJ/dg_VJP - dJ/dg_CS| < 1e-11
    // =====================================================================
    {
        int n = 40;
        FVMGraph graph = make_chain_graph(n, 1.0);
        HelmholtzFilter filt(0.12);
        filt.build(graph);

        std::mt19937 rng(1234);
        std::uniform_real_distribution<double> ud(0.0, 1.0);
        std::vector<double> g(n);
        for (int i = 0; i < n; ++i) g[i] = 0.3 + 0.4 * ud(rng);

        std::vector<double> gfilt;
        filt.filter(g, graph, gfilt);

        HeavisideProjection hep(8.0, 0.5);
        std::vector<double> gbar, heavd;
        hep.project(gfilt, gbar);
        hep.derivative(gfilt, heavd);

        std::vector<double> w(n);
        for (int i = 0; i < n; ++i) w[i] = ud(rng);
        // J = sum w_i * gbar_i  => dJ/dgbar = w

        std::vector<double> dJdg_vjp;
        filt.vjp(w, heavd, graph.vol, dJdg_vjp);

        // complex-step reference (h=1e-100 avoids CG dot-product underflow while
        // complex-step truncation is O(h^2) and negligible)
        double h = 1e-100;
        std::vector<double> dJdg_cs(n, 0.0);
        double t1 = std::tanh(hep.beta * hep.eta);
        double t2 = std::tanh(hep.beta * (1.0 - hep.eta));
        for (int j = 0; j < n; ++j) {
            std::vector<std::complex<double>> gc(n);
            for (int i = 0; i < n; ++i) gc[i] = std::complex<double>(g[i], 0.0);
            gc[j] += std::complex<double>(0.0, h);
            // filter complex: A real SPD -> solve real & imag parts separately; rhs = gc .* vol
            std::vector<double> rhs_r(n), rhs_i(n);
            for (int i = 0; i < n; ++i) { rhs_r[i] = std::real(gc[i]) * graph.vol[i]; rhs_i[i] = std::imag(gc[i]) * graph.vol[i]; }
            std::vector<double> sr(n), si(n);
            filt.solve(rhs_r, sr);
            filt.solve(rhs_i, si);
            std::vector<std::complex<double>> gfc(n);
            for (int i = 0; i < n; ++i) gfc[i] = std::complex<double>(sr[i], si[i]);
            // heaviside complex projection
            std::vector<std::complex<double>> gbc(n);
            for (int i = 0; i < n; ++i)
                gbc[i] = (t1 + std::tanh(hep.beta * (gfc[i] - std::complex<double>(hep.eta, 0.0)))) / (t1 + t2);
            std::complex<double> Jc(0.0, 0.0);
            for (int i = 0; i < n; ++i) Jc += std::complex<double>(w[i], 0.0) * gbc[i];
            dJdg_cs[j] = std::imag(Jc) / h;
        }
        double err = 0.0, scale = 0.0;
        for (int j = 0; j < n; ++j) {
            err = std::max(err, std::fabs(dJdg_vjp[j] - dJdg_cs[j]));
            scale = std::max(scale, std::fabs(dJdg_cs[j]));
        }
        double rel = (scale > 0.0) ? err / scale : err;
        std::cout << std::scientific << std::setprecision(6);
        std::cout << "[VJP] max abs err = " << err << ", max rel err = " << rel
                  << " (target abs < 1e-11)\n";
        check(err < 1e-11, "TEST1 VJP chain rule vs complex-step");
    }

    // =====================================================================
    // TEST 2: Heaviside projection derivative vs complex-step
    // =====================================================================
    {
        int n = 10;
        std::vector<double> gfilt(n);
        std::mt19937 rng(7);
        std::uniform_real_distribution<double> ud(0.0, 1.0);
        for (int i = 0; i < n; ++i) gfilt[i] = ud(rng);
        for (double beta : {1.0, 2.0, 8.0, 32.0}) {
            HeavisideProjection hep(beta, 0.5);
            std::vector<double> dg;
            hep.derivative(gfilt, dg);
            double h = 1e-200;
            double err = 0.0;
            double t1 = std::tanh(hep.beta * hep.eta);
            double t2 = std::tanh(hep.beta * (1.0 - hep.eta));
            for (int i = 0; i < n; ++i) {
                std::complex<double> zc(gfilt[i], h);
                std::complex<double> proj = (t1 + std::tanh(hep.beta * (zc - std::complex<double>(hep.eta, 0.0)))) / (t1 + t2);
                double cs = std::imag(proj) / h;
                err = std::max(err, std::fabs(dg[i] - cs));
            }
            std::cout << std::scientific << std::setprecision(6);
            std::cout << "[Heaviside beta=" << beta << "] max err = " << err << "\n";
            check(err < 1e-10, "TEST2 Heaviside derivative");
        }
    }

    // =====================================================================
    // TEST 3: KS aggregation gradient vs finite difference
    // =====================================================================
    {
        int n = 25;
        std::vector<double> T(n);
        std::mt19937 rng(99);
        std::uniform_real_distribution<double> ud(0.0, 10.0);
        for (int i = 0; i < n; ++i) T[i] = ud(rng);
        KSAggregation ks(50.0);
        std::vector<double> grad;
        double g = ks.evaluate(T, grad);
        double h = 1e-7;
        double err = 0.0;
        for (int i = 0; i < n; ++i) {
            std::vector<double> Tp = T, Tm = T;
            Tp[i] += h; Tm[i] -= h;
            double gp = ks.evaluate(Tp, grad); (void)grad;
            double gm = ks.evaluate(Tm, grad); (void)grad;
            double cs = (gp - gm) / (2.0 * h);
            err = std::max(err, std::fabs(grad[i] - cs));
        }
        std::cout << std::scientific << std::setprecision(6);
        std::cout << "[KS] value=" << g << " max grad err = " << err << "\n";
        check(err < 1e-6, "TEST3 KS aggregation gradient");
    }

    // =====================================================================
    // TEST 4: Svanberg MMA on a small constrained problem
    // min (x1-2)^2 + (x2-1)^2  s.t. x1+x2 <= 2.5, 0<=x<=3
    // Expected optimum: x=(1.75, 0.75), f=0.125
    // =====================================================================
    {
        int n = 2, m = 1;
        MMA opt(n, m, 0.2);
        opt.set_bounds({0.0, 0.0}, {3.0, 3.0});
        opt.max_iter = 500;
        opt.tol = 1e-9;
        opt.initialize({2.5, 2.5});
        opt.fg = [&](int nn, int mm, const std::vector<double>& x,
                     std::vector<double>& f, std::vector<double>& df) -> bool {
            f.assign(mm + 1, 0.0);
            df.assign((mm + 1) * nn, 0.0);
            f[0] = (x[0] - 2.0) * (x[0] - 2.0) + (x[1] - 1.0) * (x[1] - 1.0);
            df[0 * nn + 0] = 2.0 * (x[0] - 2.0);
            df[0 * nn + 1] = 2.0 * (x[1] - 1.0);
            f[1] = x[0] + x[1] - 2.5;
            df[1 * nn + 0] = 1.0;
            df[1 * nn + 1] = 1.0;
            return true;
        };
        int it = opt.run();
        double f = opt.hist_obj.empty() ? 0.0 : opt.hist_obj.back();
        double x1 = opt.x[0], x2 = opt.x[1];
        double cviol = x1 + x2 - 2.5;
        double err_x = std::max(std::fabs(x1 - 1.75), std::fabs(x2 - 0.75));
        double err_f = std::fabs(f - 0.125);
        std::cout << std::scientific << std::setprecision(6);
        std::cout << "[MMA] it=" << it << " x=(" << x1 << "," << x2 << ") f=" << f
                  << " cviol=" << cviol << " (expect 1.75,0.75, 0.125)\n";
        check(err_x < 1e-2 && err_f < 1e-2 && cviol < 1e-2, "TEST4 MMA convergence");
    }

    // =====================================================================
    // TEST 5: FOSM robust gradient vs finite difference
    // g = sum (gamma_j - xi_j)^2, xi ~ N(mu, Sigma)
    // =====================================================================
    {
        int n = 2, nxi = 2;
        std::vector<double> mu = {1.0, 2.0};
        std::vector<double> coords = {0.0, 0.0, 1.0, 0.0}; // 2 points in 1D
        UncertaintyModel um;
        um.build(nxi, mu, coords, 1, 0.5, 1.0); // sigma=0.5, ell=1.0
        FOSM fosm(2.0);
        fosm.um = um;

        std::vector<double> gamma = {1.5, 2.5};
        auto dg_dg = [&](const std::vector<double>& g_) {
            std::vector<double> d(n);
            for (int j = 0; j < n; ++j) d[j] = 2.0 * (g_[j] - mu[j]);
            return d;
        };
        auto dg_dxi = [&](const std::vector<double>& g_) {
            std::vector<double> d(nxi);
            for (int j = 0; j < nxi; ++j) d[j] = -2.0 * (g_[j] - mu[j]);
            return d;
        };
        // second-order term: d2g/(d gamma_j d xi_i) = -2 delta_ij
        // d2g_dg_dxi_v[j] = sum_i (-2 delta_ij) v_xi_i = -2 v_xi_j
        auto second_order_term = [&](const std::vector<double>& dgdx) {
            std::vector<double> Sig_dg = um.apply_Sigma(dgdx);
            double var = dot(dgdx, Sig_dg);
            double sg = std::sqrt(std::max(var, 0.0));
            std::vector<double> v(nxi);
            if (sg > 1e-14) for (int i = 0; i < nxi; ++i) v[i] = Sig_dg[i] / sg;
            std::vector<double> out(n, 0.0);
            for (int j = 0; j < n; ++j) out[j] = -2.0 * v[j];
            return out;
        };
        auto gval = [&](const std::vector<double>& gg) {
            double s = 0.0;
            for (int j = 0; j < n; ++j) s += (gg[j] - mu[j]) * (gg[j] - mu[j]);
            return s;
        };

        std::vector<double> dgdg = dg_dg(gamma);
        std::vector<double> dgdx = dg_dxi(gamma);
        std::vector<double> d2term = second_order_term(dgdx);
        std::vector<double> dPhi;
        double Phi = fosm.evaluate_full(gval(gamma), dgdg, dgdx, d2term, dPhi);
        (void)Phi;

        // finite difference of Phi(gamma)
        double h = 1e-6;
        double err = 0.0;
        for (int j = 0; j < n; ++j) {
            std::vector<double> gp = gamma, gm = gamma;
            gp[j] += h; gm[j] -= h;
            auto dgdg_p = dg_dg(gp); auto dgdx_p = dg_dxi(gp); auto d2_p = second_order_term(dgdx_p);
            auto dgdg_m = dg_dg(gm); auto dgdx_m = dg_dxi(gm); auto d2_m = second_order_term(dgdx_m);
            double Pp = fosm.evaluate_full(gval(gp), dgdg_p, dgdx_p, d2_p, dPhi);
            double Pm = fosm.evaluate_full(gval(gm), dgdg_m, dgdx_m, d2_m, dPhi);
            double cs = (Pp - Pm) / (2.0 * h);
            err = std::max(err, std::fabs(dPhi[j] - cs));
        }
        std::cout << std::scientific << std::setprecision(6);
        std::cout << "[FOSM] max grad err vs FD = " << err << "\n";
        check(err < 1e-4, "TEST5 FOSM gradient vs finite-difference");
    }

    // =====================================================================
    // TEST 6: Hybrid optimizer on Rastrigin (multimodal) -> near global min
    // =====================================================================
    {
        int n = 2;
        auto rastrigin = [&](const std::vector<double>& x) {
            double f = 10.0 * n;
            for (double v : x) f += v * v - 10.0 * std::cos(2.0 * M_PI * v);
            return f;
        };
        std::vector<double> lo = {-5.12, -5.12}, hi = {5.12, 5.12};
        HybridOptimizer hyb(n);
        hyb.f = rastrigin;
        double fbest; std::vector<double> xbest;
        hyb.optimize(lo, hi, fbest, xbest);
        std::cout << std::scientific << std::setprecision(6);
        std::cout << "[HYBRID] Rastrigin fbest=" << fbest << " x=(" << xbest[0] << "," << xbest[1] << ") (global=0 at 0,0)\n";
        check(fbest < 0.5, "TEST6 Hybrid optimizer near-global");
    }

    std::cout << "\n=== " << (g_failures == 0 ? "ALL TESTS PASSED" : std::to_string(g_failures) + " TEST(S) FAILED") << " ===\n";
    return g_failures == 0 ? 0 : 1;
}
