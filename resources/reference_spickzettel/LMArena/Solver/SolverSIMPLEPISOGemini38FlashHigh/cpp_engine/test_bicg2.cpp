#include "amg_mixed_krylov.hpp"
#include <iostream>

using namespace hpc::solvers;

// Flexible Conjugate Gradient with FP32 AMG Preconditioner
SolverStats solve_mixed_precision_fcg(
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

    for (std::size_t i = 0; i < n; ++i) r[i] = b[i] - Ax[i];
    double r_norm = 0.0;
    for (double val : r) r_norm += val * val;
    r_norm = std::sqrt(r_norm);

    std::vector<float> r_fp32(n);
    std::vector<float> z_fp32(n);
    std::vector<double> z(n);
    std::vector<double> p(n, 0.0);
    std::vector<double> Ap(n);

    double rho_old = 1.0;
    double A_frobenius = A.frobenius_norm();

    for (int iter = 0; iter < max_iters; ++iter) {
        stats.iterations = iter + 1;
        stats.final_residual = r_norm / b_norm;

        if (stats.final_residual < tol) {
            stats.converged = true;
            break;
        }

        // 1. FP32 AMG Preconditioner: z = M^-1 * r
        for (std::size_t i = 0; i < n; ++i) r_fp32[i] = static_cast<float>(r[i]);
        amg.v_cycle(r_fp32, z_fp32);
        for (std::size_t i = 0; i < n; ++i) z[i] = static_cast<double>(z_fp32[i]);

        // Rounding error detector
        double diff_sq = 0.0, x_sq = 0.0;
        for (std::size_t i = 0; i < n; ++i) {
            double diff = r[i] - static_cast<double>(r_fp32[i]);
            diff_sq += diff * diff;
            x_sq += x[i] * x[i];
        }
        stats.eta_round = std::sqrt(diff_sq) / (b_norm + A_frobenius * std::sqrt(x_sq));

        // 2. FCG / PCG step
        double rho_new = 0.0;
        for (std::size_t i = 0; i < n; ++i) rho_new += r[i] * z[i];

        if (iter == 0) {
            p = z;
        } else {
            double beta = rho_new / rho_old;
            for (std::size_t i = 0; i < n; ++i) {
                p[i] = z[i] + beta * p[i];
            }
        }

        A.spmv(p.data(), Ap.data());
        double p_Ap = 0.0;
        for (std::size_t i = 0; i < n; ++i) p_Ap += p[i] * Ap[i];

        if (std::abs(p_Ap) < 1e-25) break;
        double alpha = rho_new / p_Ap;

        for (std::size_t i = 0; i < n; ++i) {
            x[i] += alpha * p[i];
            r[i] -= alpha * Ap[i];
        }

        rho_old = rho_new;

        // Recompute true FP64 residual periodically to eliminate drift
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

int main() {
    const std::size_t N = 64;
    CsrMatrix<double> A;
    A.num_rows = N;
    A.num_cols = N;
    A.row_ptr.resize(N + 1, 0);

    for (std::size_t i = 0; i < N; ++i) {
        if (i > 0) {
            A.col_idx.push_back(static_cast<int32_t>(i - 1));
            A.values.push_back(-1.0);
        }
        A.col_idx.push_back(static_cast<int32_t>(i));
        A.values.push_back(2.01);
        if (i + 1 < N) {
            A.col_idx.push_back(static_cast<int32_t>(i + 1));
            A.values.push_back(-1.0);
        }
        A.row_ptr[i + 1] = static_cast<int32_t>(A.values.size());
    }

    std::vector<double> b(N, 1.0);
    std::vector<double> x(N, 0.0);

    MixedPrecisionAMG amg;
    amg.setup(A);

    SolverStats stats = solve_mixed_precision_fcg(A, b, x, amg, 50, 1e-10);
    std::cout << "Converged: " << stats.converged << " iters: " << stats.iterations 
              << " res: " << stats.final_residual << " eta_round: " << stats.eta_round << std::endl;
    return 0;
}
