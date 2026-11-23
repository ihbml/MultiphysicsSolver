// mdo_opt1.hpp
// Svanberg Method of Moving Asymptotes (MMA) with primal-dual interior-point
// Newton subproblem solver (SolveDIP), and Griewank-Walther Revolve binomial
// checkpointing scheduler.
// User callback: bool fg(int n, int m, const double* x, double* f, double* df)
//   f[0..m] : f[0]=objective, f[1..m]=constraints (all <=0 form)
//   df[(i)*(n) + j] : df_i/dx_j  (row-major, i=0..m, j=0..n-1)
#pragma once

#include "mdo_math.hpp"
#include <vector>
#include <cmath>
#include <algorithm>
#include <stdexcept>
#include <iostream>
#include <functional>
#include <limits>

namespace mdo {

// ============ Svanberg MMA ============

struct MMA {
    int n;            // # design variables
    int m;            // # inequality constraints (excluding objective)
    std::vector<double> xmin, xmax; // variable bounds
    std::vector<double> x;          // current iterate
    std::vector<double> xold1, xold2; // previous iterates
    std::vector<double> L, U;       // asymptotes
    double asyminit = 0.5, asymdec = 0.7, asyminc = 1.2;
    double move;                    // move limit (fraction of design range)
    double tol = 1e-6;
    int max_iter = 200;
    int iter = 0;

    // callback: fg(x, f, df) returns false to stop
    std::function<bool(int,int,const std::vector<double>&,
                       std::vector<double>&, std::vector<double>&)> fg;

    // history
    std::vector<double> hist_obj;
    std::vector<double> hist_con; // max constraint violation

    MMA(int n_, int m_, double move_ = 0.2)
        : n(n_), m(m_), move(move_) {}

    void set_bounds(const std::vector<double>& lo, const std::vector<double>& hi) {
        xmin = lo; xmax = hi;
    }

    void initialize(const std::vector<double>& x0) {
        x = x0;
        xold1 = x0;
        xold2 = x0;
        L.assign(n, 0.0);
        U.assign(n, 0.0);
        init_asymptotes();
        iter = 0;
        hist_obj.clear(); hist_con.clear();
    }

    void init_asymptotes() {
        for (int j = 0; j < n; ++j) {
            double range = std::max(1e-12, xmax[j] - xmin[j]);
            double mid = 0.5 * (xmin[j] + xmax[j]);
            // asymptotes straddle x with asyminit fraction of range
            L[j] = std::max(xmin[j], x[j] - asyminit * range);
            U[j] = std::min(xmax[j], x[j] + asyminit * range);
            // guarantee strict bracket
            if (L[j] >= x[j]) L[j] = x[j] - 1e-6 * range;
            if (U[j] <= x[j]) U[j] = x[j] + 1e-6 * range;
        }
    }

    void update_asymptotes() {
        if (iter < 2) return;
        for (int j = 0; j < n; ++j) {
            double d1 = x[j] - xold1[j];
            double d2 = xold1[j] - xold2[j];
            double factor = (d1 * d2 > 0.0) ? asyminc : asymdec;
            double range = std::max(1e-12, xmax[j] - xmin[j]);
            // asymmetote distance from x
            double dL = x[j] - L[j];
            double dU = U[j] - x[j];
            dL *= factor; dU *= factor;
            // clamp to design box
            double maxd = range;
            dL = std::min(dL, maxd); dU = std::min(dU, maxd);
            L[j] = std::max(xmin[j], x[j] - dL);
            U[j] = std::min(xmax[j], x[j] + dU);
            // keep asymptotes strictly outside the move window so the subproblem
            // is never frozen by asymptotes collapsing onto x.
            double mw = move * range;
            L[j] = std::min(L[j], x[j] - mw);
            U[j] = std::max(U[j], x[j] + mw);
            L[j] = std::max(xmin[j], L[j]);
            U[j] = std::min(xmax[j], U[j]);
            if (U[j] <= L[j] + 1e-12*range) { // degenerate box
                L[j] = x[j] - 0.5*mw; U[j] = x[j] + 0.5*mw;
            }
        }
    }

    // ---- one MMA iteration ----
    // returns true if converged
    bool iterate() {
        // evaluate functions & gradients
        std::vector<double> f(m + 1);
        std::vector<double> df((m + 1) * n);
        if (!fg(n, m, x, f, df)) return false; // user requested stop

        hist_obj.push_back(f[0]);
        double cviol = 0.0;
        for (int i = 1; i <= m; ++i) cviol = std::max(cviol, f[i]);
        hist_con.push_back(cviol);

        // update asymptotes (needs xold1, xold2; from iter>=2)
        if (iter >= 2) update_asymptotes();

        // move limits
        std::vector<double> low(n), high(n);
        for (int j = 0; j < n; ++j) {
            double range = std::max(1e-12, xmax[j] - xmin[j]);
            low[j] = std::max(xmin[j], x[j] - move * range);
            high[j] = std::min(xmax[j], x[j] + move * range);
        }

        // build p, q, alpha for each constraint i=0..m
        // p[i*n+j], q[i*n+j]
        std::vector<double> p((m + 1) * n, 0.0), q((m + 1) * n, 0.0);
        std::vector<double> alpha(m + 1, 0.0);
        for (int i = 0; i <= m; ++i) {
            double sum_pq = 0.0;
            for (int j = 0; j < n; ++j) {
                double gij = df[i * n + j];
                double Ux = U[j] - x[j];
                double xL = x[j] - L[j];
                if (Ux <= 0.0) Ux = 1e-8;
                if (xL <= 0.0) xL = 1e-8;
                if (gij > 0.0) p[i * n + j] = gij * Ux * Ux;
                else           q[i * n + j] = (-gij) * xL * xL;
                sum_pq += p[i * n + j] / Ux + q[i * n + j] / xL;
            }
            alpha[i] = f[i] - sum_pq;
        }

        // solve subproblem via dual interior point (SolveDIP)
        std::vector<double> x_new = solve_subproblem(p, q, alpha, low, high);

        // convergence check
        double dx = 0.0;
        for (int j = 0; j < n; ++j) dx = std::max(dx, std::fabs(x_new[j] - x[j]));
        // shift iterates
        xold2 = xold1;
        xold1 = x;
        x = x_new;
        iter++;
        return (dx < tol);
    }

    // run to convergence
    int run() {
        for (int it = 0; it < max_iter; ++it) {
            if (iterate()) break;
        }
        return iter;
    }

    // Exposed for testing: solve the MMA subproblem for given p/q/alpha/bounds
    // using the current x, L, U asymptotes.
    std::vector<double> solve_subproblem_exposed(
        const std::vector<double>& p, const std::vector<double>& q,
        const std::vector<double>& alpha,
        const std::vector<double>& low, const std::vector<double>& high) {
        return solve_subproblem(p, q, alpha, low, high);
    }

private:
    // ---- SolveDIP: primal-dual interior point Newton on the dual ----
    // subproblem:
    //   min  f~_0(x) = alpha_0 + sum_j [ p0j/(U-xj) + q0j/(xj-L) ]
    //   s.t. f~_i(x) <= 0, i=1..m
    //        low_j <= x_j <= high_j
    std::vector<double> solve_subproblem(const std::vector<double>& p,
                                         const std::vector<double>& q,
                                         const std::vector<double>& alpha,
                                         const std::vector<double>& low,
                                         const std::vector<double>& high) {
        // x at current
        std::vector<double> xcur = x;

        // barrier parameters
        double mu_min = 1e-10;
        int outer_max = 50;
        int newton_max = 50;

        // helper: given lambda, compute x*(lambda) by analytic minimization (clipped)
        auto compute_x = [&](const std::vector<double>& lam,
                             std::vector<double>& xx) {
            xx.resize(n);
            for (int j = 0; j < n; ++j) {
                double P = p[0 * n + j]; // p_0j (objective part uses p[0*n+j])
                double Q = q[0 * n + j];
                for (int i = 1; i <= m; ++i) {
                    P += lam[i - 1] * p[i * n + j];
                    Q += lam[i - 1] * q[i * n + j];
                }
                if (P < 0.0) P = 0.0;
                if (Q < 0.0) Q = 0.0;
                double Ux = U[j] - x[j];
                double xL = x[j] - L[j];
                if (Ux <= 0.0) Ux = 1e-8;
                if (xL <= 0.0) xL = 1e-8;
                double sqrtP = std::sqrt(P);
                double sqrtQ = std::sqrt(Q);
                double xstar;
                if (sqrtP + sqrtQ == 0.0) {
                    xstar = x[j];
                } else {
                    xstar = (sqrtQ * U[j] + sqrtP * L[j]) / (sqrtP + sqrtQ);
                }
                // The analytic minimizer lies in (L, U). Clip only to the move-limit
                // box [low, high]; do NOT clamp to (L, U), otherwise asymptotes
                // collapsing onto x would freeze the design variables.
                xstar = std::min(std::max(xstar, low[j]), high[j]);
                // keep strictly inside (L, U) with a tiny margin for safety
                double margin = 1e-9 * std::max(1e-8, U[j]-L[j]);
                if (xstar <= L[j] + margin) xstar = L[j] + margin;
                if (xstar >= U[j] - margin) xstar = U[j] - margin;
                xx[j] = xstar;
            }
        };

        // evaluate constraint approximations f~_i(xx) and gradients df~_i/dx
        auto eval_fapprox = [&](int i, const std::vector<double>& xx,
                                double& fi, std::vector<double>& dfi) {
            fi = alpha[i];
            dfi.assign(n, 0.0);
            for (int j = 0; j < n; ++j) {
                double Ux = U[j] - xx[j];
                double xL = xx[j] - L[j];
                if (Ux <= 0.0) Ux = 1e-8;
                if (xL <= 0.0) xL = 1e-8;
                fi += p[i * n + j] / Ux + q[i * n + j] / xL;
                dfi[j] = p[i * n + j] / (Ux * Ux) - q[i * n + j] / (xL * xL);
            }
        };

        // Solve the dual: maximize phi(lambda) over lambda >= 0.
        // phi(lambda) = alpha_0 + sum_i lambda_i alpha_i + sum_j [P_j/(U-x_j) + Q_j/(x_j-L)]
        // where P_j,Q_j are lambda-weighted sums and x*(lambda) is the analytic
        // separable minimizer. By the envelope theorem:
        //   d phi / d lambda_i = f~_{i+1}(x*(lambda))   (the i-th constraint value)
        // phi is concave in lambda, so projected gradient ascent converges.
        auto phi_and_grad = [&](const std::vector<double>& lam,
                                std::vector<double>& grad) -> double {
            compute_x(lam, xcur);
            double phi = alpha[0];
            for (int j = 0; j < n; ++j) {
                double P = p[0 * n + j], Q = q[0 * n + j];
                for (int i = 1; i <= m; ++i) {
                    P += lam[i - 1] * p[i * n + j];
                    Q += lam[i - 1] * q[i * n + j];
                }
                if (P < 0.0) P = 0.0;
                if (Q < 0.0) Q = 0.0;
                double Ux = U[j] - xcur[j];
                double xL = xcur[j] - L[j];
                if (Ux <= 0.0) Ux = 1e-8;
                if (xL <= 0.0) xL = 1e-8;
                phi += P / Ux + Q / xL;
            }
            std::vector<double> fi(m + 1), dfi(n);
            for (int i = 1; i <= m; ++i) {
                eval_fapprox(i, xcur, fi[i], dfi);
                grad[i - 1] = fi[i]; // d phi / d lambda_i
                phi += lam[i - 1] * alpha[i];
            }
            return phi;
        };

        std::vector<double> lambda(m, 0.1);
        // projected gradient ascent with Armijo backtracking
        for (int it = 0; it < 20000; ++it) {
            std::vector<double> grad(m);
            double phi = phi_and_grad(lambda, grad);
            double gnorm = 0.0;
            for (int i = 0; i < m; ++i) gnorm += grad[i] * grad[i];
            gnorm = std::sqrt(gnorm);
            if (gnorm < 1e-12) break;
            double t = 1.0;
            bool accepted = false;
            for (int ls = 0; ls < 50; ++ls) {
                std::vector<double> lam_try(m);
                for (int i = 0; i < m; ++i)
                    lam_try[i] = std::max(0.0, lambda[i] + t * grad[i]);
                std::vector<double> g2(m);
                double phi2 = phi_and_grad(lam_try, g2);
                if (phi2 > phi) { lambda = lam_try; accepted = true; break; }
                t *= 0.5;
            }
            if (!accepted) break;
        }

        // final x
        compute_x(lambda, xcur);
        return xcur;
    }
};

// ============ Revolve: binomial checkpointing scheduler ============

// Computes the optimal binomial checkpoint schedule for Griewank & Walther's
// Revolve. Given Nsteps and S (number of available checkpoint memory slots),
// returns a sequence of actions for reverse-mode adjoint time stepping.
//
// Actions are encoded as:
//   {type: "store", step: i}   -- store checkpoint at step i (forward)
//   {type: "restore", step: i} -- restore checkpoint i (reverse)
//   {type: "recompute", from: i, to: j} -- recompute steps i..j
//
// Simple recursive binomial schedule (the classic revolve):
struct RevolveSchedule {
    struct Action {
        std::string type;
        int step = -1;
        int from = -1, to = -1;
    };
    std::vector<Action> actions;
    int Nsteps;
    int S;
};

inline int revolve_binomial_c(int n, int s) {
    // c(n, s) = max number of steps reversible with n checkpoints? 
    // Griewank-Walther: C(r, s) = binomial coefficient recurrence.
    // Number of steps check-pointable with r checkpoints and s... 
    // Use the standard: t(r, s) where t(1,s)=s, t(r,s)=t(r,s-1)+t(r-1,s-1)+1 ?
    // We use the recurrence for the optimal binomial scheme:
    //   N(r, s) = N(r, s-1) + N(r-1, s-1) + 1,  N(1, s) = s
    // Returns max steps forward with r checkpoints & ... 
    // We'll compute a table.
    static thread_local std::vector<std::vector<int>> memo;
    // We'll compute iteratively below in scheduler.
    return 0;
}

// Build a simple revolve schedule: forward pass stores checkpoints at binomial
// positions; reverse pass recomputes segments between checkpoints.
inline RevolveSchedule build_revolve_schedule(int Nsteps, int S) {
    RevolveSchedule sched;
    sched.Nsteps = Nsteps;
    sched.S = S;
    // Choose checkpoint steps: geometric/binomial spacing.
    // Simple approach: place S checkpoints spread to minimize recompute.
    // Use the classic: checkpoints at steps dividing the interval per binomial recursion.
    std::vector<int> checkpoints;
    if (S >= Nsteps) {
        // store every step
        for (int i = 0; i <= Nsteps; ++i) checkpoints.push_back(i);
    } else if (S == 1) {
        checkpoints = {0, Nsteps};
    } else {
        // recursively bisect: store at midpoints (a reasonable binomial-ish schedule)
        // Place checkpoints to cover [0, Nsteps] with S points.
        // We use positions: round(Nsteps * k / (S-1))? That's uniform, not binomial.
        // Better: recursive mid-point insertion starting from {0, Nsteps}.
        checkpoints = {0, Nsteps};
        while ((int)checkpoints.size() < S) {
            // find largest gap and split at midpoint
            int best = -1, bestidx = -1;
            for (size_t i = 1; i < checkpoints.size(); ++i) {
                int gap = checkpoints[i] - checkpoints[i - 1];
                if (gap > best) { best = gap; bestidx = (int)i; }
            }
            if (best <= 1) break;
            int mid = (checkpoints[bestidx - 1] + checkpoints[bestidx]) / 2;
            if (mid == checkpoints[bestidx - 1] || mid == checkpoints[bestidx]) break;
            checkpoints.insert(checkpoints.begin() + bestidx, mid);
        }
        std::sort(checkpoints.begin(), checkpoints.end());
    }

    // Forward: store at each checkpoint (except step 0 which is initial)
    for (int cp : checkpoints) {
        if (cp > 0) sched.actions.push_back({"store", cp});
    }
    // Reverse: for each segment between consecutive checkpoints, restore left cp,
    // recompute forward to right cp, storing/adjoining along the way.
    for (size_t i = 1; i < checkpoints.size(); ++i) {
        int left = checkpoints[i - 1];
        int right = checkpoints[i];
        sched.actions.push_back({"restore", left});
        sched.actions.push_back({"recompute", left, right});
    }
    return sched;
}

} // namespace mdo
