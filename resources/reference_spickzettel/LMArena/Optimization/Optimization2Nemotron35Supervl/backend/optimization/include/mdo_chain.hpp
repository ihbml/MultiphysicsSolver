// mdo_chain.hpp
// Helmholtz PDE density filter + Heaviside projection + KS aggregation
// + exact backward VJP chain rule. All on an unstructured FVM graph.
// Math:
//   Helmholtz: -(h)^2 nabla^2 g~ + g~ = g,  h = rmin/(2*sqrt(3))
//   FVM:       A * g~ = g .* V,   A_ii = V_i + sum T_ij,  A_ij = -T_ij,  T_ij = h^2 * Aface_ij / dist_ij
//   Heaviside: gbar_i = (tanh(b*eta) + tanh(b*(g~_i - eta))) / (tanh(b*eta) + tanh(b*(1-eta)))
//   KS:        g_KS = Tmax + (1/rho) ln( (1/N) sum exp(rho*(T_i - Tmax)) )
//   VJP:       dJ/dg = A^{-T} ( dgbar/dg~  odot  dJ/dgbar )   (A symmetric => A^{-T}=A^{-1})
#pragma once

#include "mdo_math.hpp"
#include <vector>
#include <cmath>
#include <numeric>
#include <algorithm>
#include <stdexcept>
#include <iostream>

namespace mdo {

// ---------- unstructured FVM graph ----------//
struct FVMGraph {
    int n;                           // number of cells
    std::vector<double> vol;         // cell volume V_i
    // adjacency: for each cell i, list of (neighbor j, face_area, distance)
    std::vector<std::vector<int>>    adj;
    std::vector<std::vector<double>> face_area;
    std::vector<std::vector<double>> dist;
};

// ---------- Helmholtz filter ----------//

struct HelmholtzFilter {
    double rmin;
    double h2;                       // (rmin/(2*sqrt(3)))^2
    CSR A;                           // SPD filter matrix
    std::vector<double> vol;         // cell volumes (needed for correct VJP scaling)
    bool built = false;

    explicit HelmholtzFilter(double rmin_) : rmin(rmin_), h2((rmin_ / (2.0 * std::sqrt(3.0))) *
                                                          (rmin_ / (2.0 * std::sqrt(3.0)))) {}

    void build(const FVMGraph& g) {
        int n = g.n;
        vol = g.vol;
        A.n = n;
        A.row_ptr.assign(n + 1, 0);
        // count nnz: diagonal + one entry per directed adjacency (symmetric graph)
        long long nnz = 0;
        for (int i = 0; i < n; ++i) {
            nnz += 1; // diagonal
            nnz += (long long)g.adj[i].size();
        }
        A.col_idx.reserve(nnz);
        A.val.reserve(nnz);

        // Build row by row. Store diagonal last or first; we just push diag + off-diagonals.
        // Use temporary per-row storage then commit.
        std::vector<int>   cols;
        std::vector<double> vals;

        for (int i = 0; i < n; ++i) {
            A.row_ptr[i] = (int)A.col_idx.size();
            cols.clear(); vals.clear();
            double diag = g.vol[i];
            // off-diagonal entries -T_ij
            for (size_t e = 0; e < g.adj[i].size(); ++e) {
                int j = g.adj[i][e];
                double T = h2 * g.face_area[i][e] / g.dist[i][e];
                diag += T; // A_ii accumulates
                cols.push_back(j);
                vals.push_back(-T);
            }
            // diagonal
            cols.push_back(i);
            vals.push_back(diag);
            // sort by column index for cleanliness (optional)
            // commit
            for (size_t e = 0; e < cols.size(); ++e) {
                A.col_idx.push_back(cols[e]);
                A.val.push_back(vals[e]);
            }
        }
        A.row_ptr[n] = (int)A.col_idx.size();
        built = true;
    }

    // Solve A x = rhs directly (CG, SPD)
    void solve(const std::vector<double>& rhs, std::vector<double>& x,
               double tol = 1e-12, int max_iter = 5000) const {
        int it = cg_solve(A, rhs, x, tol, max_iter);
        (void)it;
    }

    // Filtering operation: solve A g~ = g .* vol
    void filter(const std::vector<double>& g, const FVMGraph& graph,
                std::vector<double>& gfilt) const {
        if (!built) throw std::runtime_error("HelmholtzFilter not built");
        std::vector<double> rhs(A.n);
        for (int i = 0; i < A.n; ++i) rhs[i] = g[i] * graph.vol[i];
        solve(rhs, gfilt);
    }

    // VJP: given dJ/dgbar and dgbar/dg~, compute dJ/dg.
    // Filter: A g~ = g .* vol  =>  dg~/dg = A^{-1} diag(vol).
    // dJ/dg = diag(vol) * A^{-T} ( dgbar/dg~ odot dJ/dgbar ).
    // A is symmetric (A^{-T}=A^{-1}), so dJ/dg = vol odot ( A^{-1} (heav_deriv odot dJ/dgbar) ).
    // vol must match the graph used at build(); store it here.
    void vjp(const std::vector<double>& dJ_dgbar,
             const std::vector<double>& heav_deriv,
             const std::vector<double>& vol,
             std::vector<double>& dJ_dg) const {
        if (!built) throw std::runtime_error("HelmholtzFilter not built");
        std::vector<double> rhs(A.n);
        for (int i = 0; i < A.n; ++i) rhs[i] = heav_deriv[i] * dJ_dgbar[i];
        std::vector<double> tmp;
        solve(rhs, tmp);
        dJ_dg.assign(A.n, 0.0);
        for (int i = 0; i < A.n; ++i) dJ_dg[i] = vol[i] * tmp[i];
    }
};

// ---------- Heaviside projection (smooth, volume preserving) ----------//

struct HeavisideProjection {
    double beta;   // continuation parameter beta_H
    double eta;    // threshold in [0,1]
    double den;    // tanh(beta*eta) + tanh(beta*(1-eta))

    HeavisideProjection(double beta_, double eta_)
        : beta(beta_), eta(eta_) {
        den = std::tanh(beta * eta) + std::tanh(beta * (1.0 - eta));
        if (den == 0.0) den = 1e-300;
    }

    // project filtered g~ -> gbar
    void project(const std::vector<double>& gfilt, std::vector<double>& gbar) const {
        gbar.resize(gfilt.size());
        double t1 = std::tanh(beta * eta);
        double t2 = std::tanh(beta * (1.0 - eta));
        for (size_t i = 0; i < gfilt.size(); ++i) {
            double num = t1 + std::tanh(beta * (gfilt[i] - eta));
            gbar[i] = num / (t1 + t2);
        }
    }

    // derivative dgbar_i / dg~_i
    void derivative(const std::vector<double>& gfilt, std::vector<double>& dgbar_dgfilt) const {
        dgbar_dgfilt.resize(gfilt.size());
        double t1 = std::tanh(beta * eta);
        double t2 = std::tanh(beta * (1.0 - eta));
        double den = t1 + t2;
        for (size_t i = 0; i < gfilt.size(); ++i) {
            double z = beta * (gfilt[i] - eta);
            double sech2 = 1.0 - std::tanh(z) * std::tanh(z);
            dgbar_dgfilt[i] = beta * sech2 / den;
        }
    }
};

// Continuation schedule beta_H in {1,2,4,8,16,32}
inline std::vector<double> heaviside_continuation_schedule() {
    return {1.0, 2.0, 4.0, 8.0, 16.0, 32.0};
}

// ---------- KS aggregation (shifted, overflow-safe) ----------//

struct KSAggregation {
    double rho;
    KSAggregation(double rho_) : rho(rho_) {}

    // value and gradient w.r.t. T
    // returns g, and fills grad (d g / d T_i)
    double evaluate(const std::vector<double>& T, std::vector<double>& grad) const {
        int n = (int)T.size();
        if (n == 0) { grad.clear(); return 0.0; }
        double Tmax = *std::max_element(T.begin(), T.end());
        // shifted exponents
        double sum = 0.0;
        std::vector<double> e(n);
        for (int i = 0; i < n; ++i) {
            e[i] = std::exp(rho * (T[i] - Tmax));
            sum += e[i];
        }
        double g = Tmax + (1.0 / rho) * std::log(sum / (double)n);
        // gradient: dg/dT_i = e_i / sum
        grad.resize(n);
        double inv_sum = 1.0 / sum;
        for (int i = 0; i < n; ++i) grad[i] = e[i] * inv_sum;
        return g;
    }
};

} // namespace mdo
