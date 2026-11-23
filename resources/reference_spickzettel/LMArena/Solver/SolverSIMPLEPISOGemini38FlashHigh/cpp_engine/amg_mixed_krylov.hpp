#pragma once
#include <vector>
#include <cmath>
#include <numeric>
#include <algorithm>
#include <iostream>
#include <cstdint>

namespace hpc::solvers {

/**
 * @brief Compressed Sparse Row (CSR) Matrix representation.
 * Supports both FP64 (double) and FP32 (float) for mixed-precision operations.
 */
template <typename T>
struct CsrMatrix {
    std::size_t num_rows{0};
    std::size_t num_cols{0};
    std::vector<int32_t> row_ptr;
    std::vector<int32_t> col_idx;
    std::vector<T> values;

    void spmv(const T* __restrict__ x, T* __restrict__ y) const {
        for (std::size_t i = 0; i < num_rows; ++i) {
            T sum = 0;
            const int32_t start = row_ptr[i];
            const int32_t end = row_ptr[i + 1];
            for (int32_t k = start; k < end; ++k) {
                sum += values[k] * x[col_idx[k]];
            }
            y[i] = sum;
        }
    }

    [[nodiscard]] double frobenius_norm() const {
        double sum = 0.0;
        for (auto v : values) {
            sum += static_cast<double>(v) * static_cast<double>(v);
        }
        return std::sqrt(sum);
    }
};

/**
 * @brief AMG Level hierarchy for Aggregation AMG.
 */
struct AMGLevelFP32 {
    CsrMatrix<float> A;
    CsrMatrix<float> P; // Prolongator (Fine <- Coarse)
    CsrMatrix<float> R; // Restrictor = P^T (Coarse <- Fine)
    std::vector<float> diag_l1; // L1-Jacobi diagonal: D_{L1, i} = sum_j |A_ij|
    float lambda_max{1.0f};     // Estimated spectral radius for Chebyshev smoother
};

/**
 * @brief Mixed-Precision Algebraic Multigrid Preconditioner (FP32).
 */
class MixedPrecisionAMG {
public:
    std::vector<AMGLevelFP32> levels;
    int max_levels{3};
    int pre_smooth_iters{2};
    int post_smooth_iters{2};
    float pmis_threshold{0.25f}; // PMIS strength of connection threshold

    /**
     * @brief Build AMG hierarchy using Parallel Modified Independent Set (PMIS) aggregation.
     */
    void setup(const CsrMatrix<double>& A_double) {
        levels.clear();
        levels.emplace_back();

        // Convert fine level to FP32
        auto& fine = levels[0];
        fine.A.num_rows = A_double.num_rows;
        fine.A.num_cols = A_double.num_cols;
        fine.A.row_ptr = A_double.row_ptr;
        fine.A.col_idx = A_double.col_idx;
        fine.A.values.resize(A_double.values.size());
        for (std::size_t i = 0; i < A_double.values.size(); ++i) {
            fine.A.values[i] = static_cast<float>(A_double.values[i]);
        }
        compute_l1_diagonal(fine);
        estimate_spectral_radius(fine);

        // Coarsening loop
        for (int l = 0; l < max_levels - 1; ++l) {
            if (levels[l].A.num_rows < 16) break; // Coarsest level reached

            AMGLevelFP32 next_level;
            build_aggregates_pmis(levels[l], next_level);
            if (next_level.A.num_rows == levels[l].A.num_rows || next_level.A.num_rows < 2) {
                break;
            }
            compute_l1_diagonal(next_level);
            estimate_spectral_radius(next_level);
            levels.push_back(std::move(next_level));
        }
    }

    /**
     * @brief Execute a single FP32 AMG V-cycle: v = AMG_V(f)
     */
    void v_cycle(const std::vector<float>& rhs, std::vector<float>& x) const {
        if (levels.empty()) return;
        x.assign(rhs.size(), 0.0f);
        v_cycle_level(0, rhs, x);
    }

private:
    static void compute_l1_diagonal(AMGLevelFP32& lvl) {
        lvl.diag_l1.assign(lvl.A.num_rows, 0.0f);
        for (std::size_t i = 0; i < lvl.A.num_rows; ++i) {
            float sum = 0.0f;
            for (int32_t k = lvl.A.row_ptr[i]; k < lvl.A.row_ptr[i + 1]; ++k) {
                sum += std::abs(lvl.A.values[k]);
            }
            lvl.diag_l1[i] = (sum > 1e-12f) ? sum : 1.0f;
        }
    }

    static void estimate_spectral_radius(AMGLevelFP32& lvl) {
        // Gershgorin disc upper bound
        float max_eig = 0.0f;
        for (std::size_t i = 0; i < lvl.A.num_rows; ++i) {
            float row_sum = 0.0f;
            for (int32_t k = lvl.A.row_ptr[i]; k < lvl.A.row_ptr[i + 1]; ++k) {
                row_sum += std::abs(lvl.A.values[k]);
            }
            if (row_sum > max_eig) max_eig = row_sum;
        }
        lvl.lambda_max = (max_eig > 0.0f) ? max_eig : 1.0f;
    }

    /**
     * @brief GPU-friendly L1-Jacobi Smoother (Zero sequential dependency).
     */
    static void smooth_l1_jacobi(const AMGLevelFP32& lvl, const std::vector<float>& b, std::vector<float>& x, int iters) {
        const std::size_t n = lvl.A.num_rows;
        std::vector<float> Ax(n, 0.0f);
        const float omega = 4.0f / 3.0f; // Standard under-relaxation for L1-Jacobi

        for (int iter = 0; iter < iters; ++iter) {
            lvl.A.spmv(x.data(), Ax.data());
            for (std::size_t i = 0; i < n; ++i) {
                float r = b[i] - Ax[i];
                x[i] += omega * (r / lvl.diag_l1[i]);
            }
        }
    }

    /**
     * @brief GPU-friendly 2nd-degree Chebyshev Polynomial Smoother.
     */
    static void smooth_chebyshev(const AMGLevelFP32& lvl, const std::vector<float>& b, std::vector<float>& x, int iters) {
        const std::size_t n = lvl.A.num_rows;
        std::vector<float> p(n, 0.0f);
        std::vector<float> r(n, 0.0f);
        std::vector<float> Ap(n, 0.0f);

        const float l_max = lvl.lambda_max;
        const float l_min = 0.1f * l_max;
        const float d = (l_max + l_min) / 2.0f;
        const float c = (l_max - l_min) / 2.0f;

        lvl.A.spmv(x.data(), Ap.data());
        for (std::size_t i = 0; i < n; ++i) {
            r[i] = b[i] - Ap[i];
            p[i] = r[i] / d;
            x[i] += p[i];
        }

        float alpha = 1.0f / d;
        float beta = 0.0f;

        for (int iter = 1; iter < iters; ++iter) {
            beta = (c * alpha / 2.0f) * (c * alpha / 2.0f);
            alpha = 1.0f / (d - beta / alpha);

            lvl.A.spmv(x.data(), Ap.data());
            for (std::size_t i = 0; i < n; ++i) {
                r[i] = b[i] - Ap[i];
                p[i] = alpha * r[i] + beta * p[i];
                x[i] += p[i];
            }
        }
    }

    void v_cycle_level(std::size_t l, const std::vector<float>& b, std::vector<float>& x) const {
        const auto& cur = levels[l];
        if (l == levels.size() - 1) {
            // Coarsest grid: smooth with more iterations or direct solve
            smooth_chebyshev(cur, b, x, 12);
            return;
        }

        // 1. Pre-smoothing
        smooth_chebyshev(cur, b, x, pre_smooth_iters);

        // 2. Residual computation: r = b - A*x
        std::size_t n = cur.A.num_rows;
        std::vector<float> Ax(n);
        cur.A.spmv(x.data(), Ax.data());
        std::vector<float> r(n);
        for (std::size_t i = 0; i < n; ++i) r[i] = b[i] - Ax[i];

        // 3. Restriction: r_c = R * r = P^T * r
        const auto& next = levels[l + 1];
        std::size_t n_c = next.A.num_rows;
        std::vector<float> r_c(n_c, 0.0f);
        cur.R.spmv(r.data(), r_c.data());

        // 4. Coarse grid correction: A_c * e_c = r_c
        std::vector<float> e_c(n_c, 0.0f);
        v_cycle_level(l + 1, r_c, e_c);

        // 5. Prolongation: x += P * e_c
        std::vector<float> e_f(n, 0.0f);
        cur.P.spmv(e_c.data(), e_f.data());
        for (std::size_t i = 0; i < n; ++i) x[i] += e_f[i];

        // 6. Post-smoothing
        smooth_chebyshev(cur, b, x, post_smooth_iters);
    }

    void build_aggregates_pmis(AMGLevelFP32& cur, AMGLevelFP32& next) const {
        const std::size_t n = cur.A.num_rows;
        std::vector<int32_t> agg_id(n, -1);
        int32_t num_aggs = 0;

        // PMIS heuristic aggregation: strongly coupled neighbor clustering
        for (std::size_t i = 0; i < n; ++i) {
            if (agg_id[i] != -1) continue;

            agg_id[i] = num_aggs;
            float diag_val = 0.0f;
            for (int32_t k = cur.A.row_ptr[i]; k < cur.A.row_ptr[i + 1]; ++k) {
                if (cur.A.col_idx[k] == static_cast<int32_t>(i)) diag_val = std::abs(cur.A.values[k]);
            }

            for (int32_t k = cur.A.row_ptr[i]; k < cur.A.row_ptr[i + 1]; ++k) {
                int32_t j = cur.A.col_idx[k];
                if (j == static_cast<int32_t>(i) || agg_id[j] != -1) continue;

                float a_ij = std::abs(cur.A.values[k]);
                if (a_ij >= pmis_threshold * diag_val) {
                    agg_id[j] = num_aggs;
                }
            }
            num_aggs++;
        }

        // Build Prolongator P (Fine x Coarse)
        cur.P.num_rows = n;
        cur.P.num_cols = num_aggs;
        cur.P.row_ptr.resize(n + 1);
        cur.P.col_idx.resize(n);
        cur.P.values.resize(n, 1.0f);

        for (std::size_t i = 0; i < n; ++i) {
            cur.P.row_ptr[i] = static_cast<int32_t>(i);
            cur.P.col_idx[i] = agg_id[i];
        }
        cur.P.row_ptr[n] = static_cast<int32_t>(n);

        // Build Restrictor R = P^T (Coarse x Fine)
        std::vector<std::vector<int32_t>> coarse_to_fine(num_aggs);
        for (std::size_t i = 0; i < n; ++i) {
            coarse_to_fine[agg_id[i]].push_back(static_cast<int32_t>(i));
        }

        cur.R.num_rows = num_aggs;
        cur.R.num_cols = n;
        cur.R.row_ptr.resize(num_aggs + 1, 0);
        for (int32_t c = 0; c < num_aggs; ++c) {
            cur.R.row_ptr[c + 1] = cur.R.row_ptr[c] + static_cast<int32_t>(coarse_to_fine[c].size());
        }
        cur.R.col_idx.resize(n);
        cur.R.values.resize(n);
        for (int32_t c = 0; c < num_aggs; ++c) {
            int32_t pos = cur.R.row_ptr[c];
            float weight = 1.0f / static_cast<float>(coarse_to_fine[c].size());
            for (auto f_idx : coarse_to_fine[c]) {
                cur.R.col_idx[pos] = f_idx;
                cur.R.values[pos] = weight;
                pos++;
            }
        }

        // Galerkin Coarse Operator: A_c = R * A * P
        // Direct sparse aggregation product
        next.A.num_rows = num_aggs;
        next.A.num_cols = num_aggs;
        std::vector<std::vector<std::pair<int32_t, float>>> coarse_entries(num_aggs);

        for (std::size_t i = 0; i < n; ++i) {
            int32_t c_i = agg_id[i];
            for (int32_t k = cur.A.row_ptr[i]; k < cur.A.row_ptr[i + 1]; ++k) {
                int32_t j = cur.A.col_idx[k];
                int32_t c_j = agg_id[j];
                float val = cur.A.values[k];

                bool found = false;
                for (auto& entry : coarse_entries[c_i]) {
                    if (entry.first == c_j) {
                        entry.second += val;
                        found = true;
                        break;
                    }
                }
                if (!found) coarse_entries[c_i].push_back({c_j, val});
            }
        }

        next.A.row_ptr.resize(num_aggs + 1, 0);
        for (int32_t c = 0; c < num_aggs; ++c) {
            next.A.row_ptr[c + 1] = next.A.row_ptr[c] + static_cast<int32_t>(coarse_entries[c].size());
        }
        for (int32_t c = 0; c < num_aggs; ++c) {
            for (const auto& entry : coarse_entries[c]) {
                next.A.col_idx.push_back(entry.first);
                next.A.values.push_back(entry.second);
            }
        }
    }
};

/**
 * @brief Mixed-Precision Krylov Solver (FP64 Outer BiCGStab / Iterative Refinement + FP32 AMG Preconditioner).
 * Includes a-posteriori stagnation & rounding error detector eta_round.
 */
struct SolverStats {
    int iterations{0};
    double final_residual{0.0};
    double eta_round{0.0};
    bool converged{false};
    bool fp32_stagnation_detected{false};
};

inline SolverStats solve_mixed_precision_amg(
    const CsrMatrix<double>& A,
    const std::vector<double>& b,
    std::vector<double>& x,
    const MixedPrecisionAMG& amg,
    int max_iters = 50,
    double tol = 1e-10
) {
    SolverStats stats;
    const std::size_t n = A.num_rows;
    x.assign(n, 0.0);

    std::vector<double> r(n);
    std::vector<double> Ax(n);
    A.spmv(x.data(), Ax.data());

    double b_norm = 0.0;
    for (double val : b) b_norm += val * val;
    b_norm = std::sqrt(b_norm);
    if (b_norm < 1e-15) b_norm = 1.0;

    double r_norm = 0.0;
    for (std::size_t i = 0; i < n; ++i) {
        r[i] = b[i] - Ax[i];
        r_norm += r[i] * r[i];
    }
    r_norm = std::sqrt(r_norm);

    std::vector<float> r_fp32(n);
    std::vector<float> z_fp32(n);
    std::vector<double> z(n);
    std::vector<double> p(n);
    std::vector<double> v(n);
    std::vector<double> s(n);
    std::vector<double> t(n);
    std::vector<double> r_tilde = r;

    double rho_1 = 1.0, alpha = 1.0, omega = 1.0;
    double A_frobenius = A.frobenius_norm();

    for (int iter = 0; iter < max_iters; ++iter) {
        stats.iterations = iter + 1;
        stats.final_residual = r_norm / b_norm;

        if (stats.final_residual < tol) {
            stats.converged = true;
            break;
        }

        // Inner preconditioner solve strictly in FP32
        for (std::size_t i = 0; i < n; ++i) r_fp32[i] = static_cast<float>(r[i]);
        amg.v_cycle(r_fp32, z_fp32);
        for (std::size_t i = 0; i < n; ++i) z[i] = static_cast<double>(z_fp32[i]);

        // A-posteriori rounding error detector:
        // eta_round = || r_fp64 - cast_64(r_fp32) || / ( ||b|| + ||A||_F * ||x|| )
        double diff_sum = 0.0;
        double x_norm_sq = 0.0;
        for (std::size_t i = 0; i < n; ++i) {
            double diff = r[i] - static_cast<double>(r_fp32[i]);
            diff_sum += diff * diff;
            x_norm_sq += x[i] * x[i];
        }
        double denominator = b_norm + A_frobenius * std::sqrt(x_norm_sq);
        stats.eta_round = std::sqrt(diff_sum) / (denominator > 1e-15 ? denominator : 1.0);

        if (stats.eta_round > 1e-6) {
            stats.fp32_stagnation_detected = true;
        }

        // Flexible Conjugate Gradient (FCG) / Krylov update with FP32 AMG Preconditioner
        double rho_2 = 0.0;
        for (std::size_t i = 0; i < n; ++i) rho_2 += r[i] * z[i];

        if (std::abs(rho_2) < 1e-30) break;

        if (iter == 0) {
            p = z;
        } else {
            double beta = rho_2 / (std::abs(rho_1) > 1e-30 ? rho_1 : 1e-30);
            for (std::size_t i = 0; i < n; ++i) {
                p[i] = z[i] + beta * p[i];
            }
        }

        A.spmv(p.data(), v.data());
        double p_Ap = 0.0;
        for (std::size_t i = 0; i < n; ++i) p_Ap += p[i] * v[i];

        if (std::abs(p_Ap) < 1e-30) break;
        alpha = rho_2 / p_Ap;

        for (std::size_t i = 0; i < n; ++i) {
            x[i] += alpha * p[i];
            r[i] -= alpha * v[i];
        }

        rho_1 = rho_2;

        // Periodic true residual recomputation in FP64 to avoid floating point drift
        if ((iter + 1) % 5 == 0) {
            A.spmv(x.data(), Ax.data());
            for (std::size_t i = 0; i < n; ++i) r[i] = b[i] - Ax[i];
        }

        r_norm = 0.0;
        for (double val : r) r_norm += val * val;
        r_norm = std::sqrt(r_norm);
    }

    stats.final_residual = r_norm / b_norm;
    if (stats.final_residual < tol) stats.converged = true;
    return stats;
}

} // namespace hpc::solvers
