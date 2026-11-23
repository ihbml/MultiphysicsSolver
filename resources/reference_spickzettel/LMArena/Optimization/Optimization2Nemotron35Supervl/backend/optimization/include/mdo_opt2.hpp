// mdo_opt2.hpp
// Generalized FOSM robust optimization + Two-stage hybrid global/local optimizer
// (MIGA + PSO -> Nelder-Mead + L-BFGS).
#pragma once

#include "mdo_math.hpp"
#include "mdo_chain.hpp"
#include <vector>
#include <cmath>
#include <algorithm>
#include <numeric>
#include <random>
#include <functional>
#include <iostream>
#include <limits>
#include <stdexcept>

namespace mdo {

// ============ Generalized FOSM Robust Optimization ============

// Uncertainty model: xi ~ N(mu, Sigma), Sigma_ij = sigma^2 exp(-||x_i-x_j||/ell)
struct UncertaintyModel {
    std::vector<double> mu;        // mean of xi (size nxi)
    std::vector<double> coords;    // spatial coords of each uncertain parameter (size nxi*dim) for covariance
    int dim = 1;
    double sigma = 0.0;            // std dev
    double ell_corr = 1.0;         // correlation length
    std::vector<std::vector<double>> Sigma; // covariance matrix (nxi x nxi)
    bool built = false;

    void build(int nxi, const std::vector<double>& mu_,
               const std::vector<double>& coords_, int dim_,
               double sigma_, double ell_) {
        mu = mu_; coords = coords_; dim = dim_; sigma = sigma_; ell_corr = ell_;
        Sigma.assign(nxi, std::vector<double>(nxi, 0.0));
        for (int i = 0; i < nxi; ++i) {
            for (int j = 0; j < nxi; ++j) {
                double d = 0.0;
                for (int k = 0; k < dim; ++k) {
                    double diff = coords[i * dim + k] - coords[j * dim + k];
                    d += diff * diff;
                }
                d = std::sqrt(d);
                Sigma[i][j] = sigma * sigma * std::exp(-d / ell_corr);
            }
        }
        built = true;
    }

    // v = Sigma * w
    std::vector<double> apply_Sigma(const std::vector<double>& w) const {
        int nxi = (int)mu.size();
        std::vector<double> v(nxi, 0.0);
        for (int i = 0; i < nxi; ++i)
            for (int j = 0; j < nxi; ++j)
                v[i] += Sigma[i][j] * w[j];
        return v;
    }
};

// FOSM robust value & sensitivity.
// g(gamma, xi): constraint/objective scalar.
// dg/dgamma (size n), dg/dxi (size nxi) provided by user (via adjoints).
// Returns Phi = mu_g + kappa * sigma_g, and dPhi/dgamma.
struct FOSM {
    double kappa;
    UncertaintyModel um;
    FOSM(double kappa_) : kappa(kappa_) {}

    // mu_g = g(gamma, mu_xi) evaluated at mean (user supplies value & grads)
    // dg_dg: dg/dgamma (n)
    // dg_dxi: dg/dxi at mean (nxi)
    // Returns Phi and fills dPhi_dg (n)
    double evaluate(double mu_g,
                    const std::vector<double>& dg_dg,
                    const std::vector<double>& dg_dxi,
                    std::vector<double>& dPhi_dg) const {
        int nxi = (int)um.mu.size();
        // sigma_g = sqrt( dg_dxi^T Sigma dg_dxi )
        std::vector<double> Sig_dg = um.apply_Sigma(dg_dxi);
        double var = dot(dg_dxi, Sig_dg);
        if (var < 0.0) var = 0.0;
        double sigma_g = std::sqrt(var);
        double Phi = mu_g + kappa * sigma_g;

        // d sigma_g / d gamma = (1/sigma_g) * (dg_dxi/dgamma)^T Sigma dg_dxi
        // We need dg_dxi/dgamma: the derivative of the stochastic gradient w.r.t. design.
        // Via tangent-over-adjoint: v_xi = (1/sigma_g) Sigma dg_dxi (size nxi)
        // d sigma_g/d gamma = v_xi^T * (d/d gamma)(dg/dxi)  -- needs 2nd order.
        // The prompt: compute using ONE additional second-order adjoint / tangent-over-adjoint
        // vector-Hessian product along v_xi.
        // We model: d2g/(d gamma d xi) applied to v_xi -> a vector of size n:
        //   d sigma_g/d gamma_j = sum_i v_xi_i * d^2 g / (d gamma_j d xi_i)
        // User supplies: hessian_times_vector(gamma_j direction? ) 
        // Simpler API: user provides function hv_vxi(j) = sum_i d2g/(dg_j dxi_i) * v_xi_i
        // Here we just compute the deterministic part and leave the second-order term
        // to be provided by the caller through dPhi_dg += second_order_term.
        // Default: dPhi_dg = dg_dg (deterministic) ; robust term handled by caller hook.
        dPhi_dg = dg_dg;
        return Phi;
    }

    // Full robust sensitivity including the second-order term:
    // dPhi/dgamma = dg/dgamma + kappa * (d sigma_g/d gamma)
    // d sigma_g/d gamma_j = v_xi^T * (d^2 g / d gamma_j d xi)
    // user provides d2g_dg_dxi_v: for each design var j, sum_i d2g/(dg_j dxi_i)*v_xi_i
    double evaluate_full(double mu_g,
                         const std::vector<double>& dg_dg,
                         const std::vector<double>& dg_dxi,
                         const std::vector<double>& d2g_dg_dxi_v, // size n: d sigma term numerator
                         std::vector<double>& dPhi_dg) const {
        int nxi = (int)um.mu.size();
        std::vector<double> Sig_dg = um.apply_Sigma(dg_dxi);
        double var = dot(dg_dxi, Sig_dg);
        if (var < 0.0) var = 0.0;
        double sigma_g = std::sqrt(var);
        double Phi = mu_g + kappa * sigma_g;

        std::vector<double> v_xi;
        if (sigma_g > 1e-14) {
            v_xi = Sig_dg;
            scale(v_xi, 1.0 / sigma_g);
        } else {
            v_xi.assign((int)um.mu.size(), 0.0);
        }

        // d sigma_g / d gamma_j = v_xi . (d^2 g / d gamma_j d xi)
        // d2g_dg_dxi_v[j] = sum_i d2g/(d g_j d xi_i) * v_xi_i  (provided by caller via 2nd-order adjoint)
        dPhi_dg.resize(dg_dg.size());
        for (size_t j = 0; j < dg_dg.size(); ++j) {
            dPhi_dg[j] = dg_dg[j] + kappa * d2g_dg_dxi_v[j];
        }
        return Phi;
    }
};

// ============ Nelder-Mead Downhill Simplex ==========//

struct NelderMead {
    std::function<double(const std::vector<double>&)> f;
    int n;
    std::vector<double> x;       // best point
    double fx;
    double tol = 1e-8;
    int max_iter = 1000;
    int iter = 0;

    NelderMead(int n_) : n(n_) {}

    double minimize(std::vector<double> x0,
                    double alpha=1.0, double gamma=2.0, double rho=0.5, double sigma=0.5) {
        x = x0;
        // build simplex: x0 + 0.05 * unit_j
        std::vector<std::vector<double>> simplex(n + 1, std::vector<double>(n));
        std::vector<double> fvals(n + 1);
        simplex[0] = x0;
        fvals[0] = f(x0);
        for (int i = 0; i < n; ++i) {
            simplex[i + 1] = x0;
            simplex[i + 1][i] += 0.05;
            fvals[i + 1] = f(simplex[i + 1]);
        }
        for (iter = 0; iter < max_iter; ++iter) {
            // sort by f
            std::vector<int> idx(n + 1);
            std::iota(idx.begin(), idx.end(), 0);
            std::sort(idx.begin(), idx.end(), [&](int a, int b){ return fvals[a] < fvals[b]; });
            // check convergence: spread of f and simplex size
            double fspread = fvals[idx[n]] - fvals[idx[0]];
            double xsize = 0.0;
            for (int i = 0; i < n; ++i) xsize = std::max(xsize, std::fabs(simplex[idx[n]][i] - simplex[idx[0]][i]));
            if (fspread < tol && xsize < tol) break;
            // centroid of best n points
            std::vector<double> c(n, 0.0);
            for (int i = 0; i < n; ++i)
                for (int k = 0; k < n; ++k) c[k] += simplex[idx[i]][k];
            for (int k = 0; k < n; ++k) c[k] /= (double)n;
            // worst point
            const auto& xh = simplex[idx[n]];
            // reflect
            std::vector<double> xr(n);
            for (int k = 0; k < n; ++k) xr[k] = c[k] + alpha * (c[k] - xh[k]);
            double fr = f(xr);
            if (fvals[idx[0]] <= fr && fr < fvals[idx[n - 1]]) {
                simplex[idx[n]] = xr; fvals[idx[n]] = fr; continue;
            }
            // expand
            if (fr < fvals[idx[0]]) {
                std::vector<double> xe(n);
                for (int k = 0; k < n; ++k) xe[k] = c[k] + gamma * (c[k] - xh[k]);
                double fe = f(xe);
                if (fe < fr) { simplex[idx[n]] = xe; fvals[idx[n]] = fe; }
                else { simplex[idx[n]] = xr; fvals[idx[n]] = fr; }
                continue;
            }
            // contract
            if (fr < fvals[idx[n]]) { // outside contraction
                std::vector<double> xc(n);
                for (int k = 0; k < n; ++k) xc[k] = c[k] + rho * (c[k] - xh[k]);
                double fc = f(xc);
                if (fc <= fr) { simplex[idx[n]] = xc; fvals[idx[n]] = fc; continue; }
            } else { // inside contraction
                std::vector<double> xc(n);
                for (int k = 0; k < n; ++k) xc[k] = c[k] - rho * (c[k] - xh[k]);
                double fc = f(xc);
                if (fc < fvals[idx[n]]) { simplex[idx[n]] = xc; fvals[idx[n]] = fc; continue; }
            }
            // shrink
            for (int i = 1; i <= n; ++i) {
                int ii = idx[i];
                for (int k = 0; k < n; ++k)
                    simplex[ii][k] = simplex[idx[0]][k] + sigma * (simplex[ii][k] - simplex[idx[0]][k]);
                fvals[ii] = f(simplex[ii]);
            }
        }
        // best point
        int best = 0;
        for (int i = 1; i <= n; ++i) if (fvals[i] < fvals[best]) best = i;
        x = simplex[best];
        fx = fvals[best];
        return fx;
    }
};

// ============ L-BFGS (unconstrained, limited memory) ==========//

struct LBFGS {
    std::function<double(const std::vector<double>&, std::vector<double>&)> fg; // f, grad
    int n;
    int m_mem = 10; // memory
    std::vector<double> x;
    double fx;
    std::vector<double> g;
    double tol = 1e-8;
    int max_iter = 500;
    int iter = 0;

    LBFGS(int n_) : n(n_) {}

    double minimize(const std::vector<double>& x0) {
        x = x0;
        g.assign(n, 0.0);
        fx = fg(x, g);
        double gnorm = norm2(g);
        if (gnorm < tol) { iter = 0; return fx; }

        std::vector<double> s(n), y(n), rho_;
        std::vector<std::vector<double>> s_hist, y_hist;
        std::vector<double> rho_hist;

        std::vector<double> x_new = x;
        std::vector<double> g_new = g;
        double f_new = fx;

        for (iter = 0; iter < max_iter; ++iter) {
            // search direction via two-loop recursion
            std::vector<double> q = g_new;
            std::vector<double> alpha(s_hist.size(), 0.0);
            for (int i = (int)s_hist.size() - 1; i >= 0; --i) {
                alpha[i] = rho_hist[i] * dot(s_hist[i], q);
                axpy(-alpha[i], y_hist[i], q);
            }
            double gamma_k = 1.0;
            if (!s_hist.empty()) {
                gamma_k = dot(s_hist.back(), y_hist.back()) / dot(y_hist.back(), y_hist.back());
                if (gamma_k <= 0.0) gamma_k = 1.0;
            }
            scale(q, gamma_k); // z = gamma * q
            std::vector<double> z = q;
            for (size_t i = 0; i < s_hist.size(); ++i) {
                double beta = rho_hist[i] * dot(y_hist[i], z);
                axpy(alpha[i] - beta, s_hist[i], z);
            }
            std::vector<double> p(n);
            for (int i = 0; i < n; ++i) p[i] = -z[i]; // p = -z (descent)

            // line search (strong wolfe-ish backtracking)
            double f0 = f_new;
            double g0_dot_p = dot(g_new, p);
            if (g0_dot_p >= 0.0) break; // not a descent direction
            double t = 1.0;
            double c1 = 1e-4;
            bool ls_ok = false;
            for (int ls = 0; ls < 40; ++ls) {
                std::vector<double> x_try = x_new;
                axpy(t, p, x_try);
                std::vector<double> g_try(n);
                double f_try = fg(x_try, g_try);
                if (f_try <= f0 + c1 * t * g0_dot_p) {
                    x_new = x_try; f_new = f_try; g_new = g_try;
                    ls_ok = true; break;
                }
                t *= 0.5;
            }
            if (!ls_ok) break;

            // update s, y
            std::vector<double> s_new = x_new - x; // wait, x is old
            // Actually s = x_new - x_old
            std::vector<double> s_vec(n), y_vec(n);
            for (int i = 0; i < n; ++i) {
                s_vec[i] = x_new[i] - x[i];
                y_vec[i] = g_new[i] - g[i];
            }
            double ys = dot(y_vec, s_vec);
            if (ys > 1e-12) {
                s_hist.push_back(s_vec);
                y_hist.push_back(y_vec);
                rho_hist.push_back(1.0 / ys);
                if ((int)s_hist.size() > m_mem) {
                    s_hist.erase(s_hist.begin());
                    y_hist.erase(y_hist.begin());
                    rho_hist.erase(rho_hist.begin());
                }
            }
            x = x_new;
            g = g_new;
            fx = f_new;
            if (norm2(g) < tol) break;
        }
        return fx;
    }
};

// ============ Particle Swarm Optimization (PSO) ==========//

struct PSOResult {
    std::vector<double> xbest;
    double fbest;
    std::vector<double> history;
};

struct PSO {
    std::function<double(const std::vector<double>&)> f;
    int n;
    int n_particles;
    int max_iter;
    double w = 0.729;      // inertia
    double c1 = 1.49445;   // cognitive
    double c2 = 1.49445;   // social
    std::vector<double> lo, hi;
    unsigned seed = 12345;

    PSO(int n_, int n_particles_, int max_iter_)
        : n(n_), n_particles(n_particles_), max_iter(max_iter_) {}

    PSOResult minimize(const std::vector<double>& lo_, const std::vector<double>& hi_) {
        lo = lo_; hi = hi_;
        std::mt19937 rng(seed);
        std::vector<std::vector<double>> x(n_particles, std::vector<double>(n));
        std::vector<std::vector<double>> v(n_particles, std::vector<double>(n, 0.0));
        std::vector<std::vector<double>> pbest(n_particles, std::vector<double>(n));
        std::vector<double> pbest_f(n_particles);
        std::vector<double> xbest(n);
        double best_f = std::numeric_limits<double>::infinity();

        for (int i = 0; i < n_particles; ++i) {
            for (int j = 0; j < n; ++j) {
                std::uniform_real_distribution<double> ud(lo[j], hi[j]);
                x[i][j] = ud(rng);
                v[i][j] = 0.0;
            }
            double fi = f(x[i]);
            pbest[i] = x[i];
            pbest_f[i] = fi;
            if (fi < best_f) { best_f = fi; xbest = x[i]; }
        }

        std::vector<double> history;
        for (int it = 0; it < max_iter; ++it) {
            for (int i = 0; i < n_particles; ++i) {
                for (int j = 0; j < n; ++j) {
                    std::uniform_real_distribution<double> ud(0.0, 1.0);
                    double r1 = ud(rng), r2 = ud(rng);
                    v[i][j] = w * v[i][j] + c1 * r1 * (pbest[i][j] - x[i][j])
                                        + c2 * r2 * (xbest[j] - x[i][j]);
                    x[i][j] += v[i][j];
                    x[i][j] = std::min(std::max(x[i][j], lo[j]), hi[j]);
                }
                double fi = f(x[i]);
                if (fi < pbest_f[i]) { pbest_f[i] = fi; pbest[i] = x[i]; }
                if (fi < best_f) { best_f = fi; xbest = x[i]; }
            }
            history.push_back(best_f);
        }
        PSOResult r; r.xbest = xbest; r.fbest = best_f; r.history = history;
        return r;
    }
};

// ============ Multi-Island Genetic Algorithm (MIGA) ==========//

struct Individual {
    std::vector<double> x;
    double f;
    std::vector<double> rank_crowd; // [rank, crowding_distance]
    bool dominated = false;
};

struct MIGAResult {
    std::vector<double> xbest;
    double fbest;
    std::vector<double> history;
};

struct MIGA {
    std::function<double(const std::vector<double>&)> f;
    int n;
    int n_islands;
    int pop_per_island;
    int max_iter;
    int migration_interval;
    std::vector<double> lo, hi;
    unsigned seed = 2024;

    MIGA(int n_, int n_islands_, int pop_per_island_, int max_iter_)
        : n(n_), n_islands(n_islands_), pop_per_island(pop_per_island_), max_iter(max_iter_) {}

    // SBX crossover + polynomial mutation
    void sbx_polynomial(const std::vector<double>& a, const std::vector<double>& b,
                        std::vector<double>& c1, std::vector<double>& c2,
                        std::mt19937& rng, double eta_c=15.0, double eta_m=20.0, double pm=0.1) {
        int n = (int)a.size();
        c1.resize(n); c2.resize(n);
        std::uniform_real_distribution<double> ud(0.0, 1.0);
        for (int j = 0; j < n; ++j) {
            double r = ud(rng);
            double beta;
            if (r <= 0.5) beta = std::pow(2.0 * r, 1.0 / (eta_c + 1.0));
            else          beta = std::pow(1.0 / (2.0 * (1.0 - r)), 1.0 / (eta_c + 1.0));
            c1[j] = 0.5 * ((1 + beta) * a[j] + (1 - beta) * b[j]);
            c2[j] = 0.5 * ((1 - beta) * a[j] + (1 + beta) * b[j]);
            // polynomial mutation
            if (ud(rng) < pm) {
                double r2 = ud(rng);
                double deltaq;
                if (r2 < 0.5) {
                    deltaq = std::pow(2.0 * r2, 1.0 / (eta_m + 1.0)) - 1.0;
                } else {
                    deltaq = 1.0 - std::pow(2.0 * (1.0 - r2), 1.0 / (eta_m + 1.0));
                }
                c1[j] += deltaq * (hi[j] - lo[j]);
                c1[j] = std::min(std::max(c1[j], lo[j]), hi[j]);
            }
            if (ud(rng) < pm) {
                double r2 = ud(rng);
                double deltaq;
                if (r2 < 0.5) {
                    deltaq = std::pow(2.0 * r2, 1.0 / (eta_m + 1.0)) - 1.0;
                } else {
                    deltaq = 1.0 - std::pow(2.0 * (1.0 - r2), 1.0 / (eta_m + 1.0));
                }
                c2[j] += deltaq * (hi[j] - lo[j]);
                c2[j] = std::min(std::max(c2[j], lo[j]), hi[j]);
            }
        }
    }

    // NSGA-II style: single objective here -> rank by front (trivial: all front 1),
    // crowding distance based on objective values. We just sort by objective.
    MIGAResult minimize(const std::vector<double>& lo_, const std::vector<double>& hi_) {
        lo = lo_; hi = hi_;
        std::mt19937 rng(seed);
        std::vector<std::vector<std::vector<double>>> islands(n_islands);
        std::vector<std::vector<double>> island_f(n_islands);
        for (int i = 0; i < n_islands; ++i) {
            islands[i].assign(pop_per_island, std::vector<double>(n));
            island_f[i].assign(pop_per_island, 0.0);
            for (int k = 0; k < pop_per_island; ++k) {
                for (int j = 0; j < n; ++j) {
                    std::uniform_real_distribution<double> ud(lo[j], hi[j]);
                    islands[i][k][j] = ud(rng);
                }
                island_f[i][k] = f(islands[i][k]);
            }
        }

        std::vector<double> history;
        double global_best = std::numeric_limits<double>::infinity();
        std::vector<double> global_x(n);

        auto update_global = [&]() {
            for (int i = 0; i < n_islands; ++i)
                for (int k = 0; k < pop_per_island; ++k)
                    if (island_f[i][k] < global_best) {
                        global_best = island_f[i][k];
                        global_x = islands[i][k];
                    }
        };
        update_global();

        for (int it = 0; it < max_iter; ++it) {
            for (int i = 0; i < n_islands; ++i) {
                // tournament selection, SBX, mutation -> new generation
                std::vector<std::vector<double>> newpop(pop_per_island, std::vector<double>(n));
                std::vector<double> newf(pop_per_island);
                std::uniform_real_distribution<double> ud01(0.0, 1.0);
                std::uniform_int_distribution<int> uid(0, pop_per_island - 1);
                for (int k = 0; k < pop_per_island; k += 2) {
                    // parents via tournament
                    auto tournament = [&]() -> int {
                        int best = uid(rng);
                        for (int t = 0; t < 2; ++t) {
                            int cand = uid(rng);
                            if (island_f[i][cand] < island_f[i][best]) best = cand;
                        }
                        return best;
                    };
                    int p1 = tournament(), p2 = tournament();
                    std::vector<double> c1, c2;
                    sbx_polynomial(islands[i][p1], islands[i][p2], c1, c2, rng);
                    newpop[k] = c1;
                    newf[k] = f(c1);
                    if (k + 1 < pop_per_island) {
                        newpop[k + 1] = c2;
                        newf[k + 1] = f(c2);
                    }
                }
                // elitism: keep best of old, fill rest with new
                // combine old+new, select pop_per_island best
                std::vector<int> order(pop_per_island * 2);
                std::vector<std::vector<double>> combined(pop_per_island * 2, std::vector<double>(n));
                std::vector<double> combinedf(pop_per_island * 2);
                for (int k = 0; k < pop_per_island; ++k) {
                    combined[k] = islands[i][k]; combinedf[k] = island_f[i][k];
                    combined[pop_per_island + k] = newpop[k]; combinedf[pop_per_island + k] = newf[k];
                }
                std::iota(order.begin(), order.end(), 0);
                std::sort(order.begin(), order.end(), [&](int a, int b){ return combinedf[a] < combinedf[b]; });
                for (int k = 0; k < pop_per_island; ++k) {
                    islands[i][k] = combined[order[k]];
                    island_f[i][k] = combinedf[order[k]];
                }
            }
            // migration: ring topology, send best to neighbor
            if (migration_interval > 0 && it % migration_interval == 0 && it > 0) {
                for (int i = 0; i < n_islands; ++i) {
                    int dst = (i + 1) % n_islands;
                    // find best in island i
                    int bi = 0;
                    for (int k = 1; k < pop_per_island; ++k)
                        if (island_f[i][k] < island_f[i][bi]) bi = k;
                    // find worst in dst
                    int wd = 0;
                    for (int k = 1; k < pop_per_island; ++k)
                        if (island_f[dst][wd] < island_f[dst][k]) wd = k;
                    if (island_f[i][bi] < island_f[dst][wd]) {
                        islands[dst][wd] = islands[i][bi];
                        island_f[dst][wd] = island_f[i][bi];
                    }
                }
            }
            update_global();
            history.push_back(global_best);
        }

        MIGAResult r; r.xbest = global_x; r.fbest = global_best; r.history = history;
        return r;
    }
};

// ============ Two-Stage Hybrid Optimizer ==========//

// Stage 1: MIGA + PSO to locate global basin.
// Stage 2: warm-start Nelder-Mead then L-BFGS for local convergence.
struct HybridOptimizer {
    int n;
    std::function<double(const std::vector<double>&)> f;
    std::vector<double> lo, hi;

    // stage results
    MIGAResult miga_res;
    PSOResult pso_res;
    NelderMead nm_res;
    LBFGS lbfgs_res;

    HybridOptimizer(int n_) : n(n_), nm_res(n_), lbfgs_res(n_) {}

    double optimize(const std::vector<double>& lo_, const std::vector<double>& hi_,
                    double& fbest, std::vector<double>& xbest) {
        lo = lo_; hi = hi_;
        // Stage 1a: MIGA
        MIGA miga(n, 4, 20, 60);
        miga.f = f; miga.lo = lo; miga.hi = hi;
        miga_res = miga.minimize(lo, hi);
        // Stage 1b: PSO
        PSO pso(n, 30, 80);
        pso.f = f; pso.lo = lo; pso.hi = hi;
        pso_res = pso.minimize(lo, hi);

        // pick best of the two as local start
        std::vector<double> x0;
        double f0;
        if (miga_res.fbest <= pso_res.fbest) { x0 = miga_res.xbest; f0 = miga_res.fbest; }
        else { x0 = pso_res.xbest; f0 = pso_res.fbest; }

        // Stage 2a: Nelder-Mead warm start
        NelderMead nm(n);
        nm.f = f;
        nm_res.x = x0;
        nm.fx = f0;
        double fnm = nm.minimize(x0);
        x0 = nm.x; f0 = nm.fx;

        // Stage 2b: L-BFGS polish (objective-only f wrapped with FD gradient)
        LBFGS bfgs(n);
        double hfd = 1e-6;
        bfgs.fg = [&](const std::vector<double>& x, std::vector<double>& gr) {
            double ff = f(x);
            for (int i = 0; i < n; ++i) {
                std::vector<double> xp = x, xm = x;
                xp[i] += hfd; xm[i] -= hfd;
                gr[i] = (f(xp) - f(xm)) / (2.0 * hfd);
            }
            return ff;
        };
        double flb = bfgs.minimize(x0);

        fbest = std::min({fnm, flb});
        xbest = bfgs.x;
        return fbest;
    }
};

} // namespace mdo
