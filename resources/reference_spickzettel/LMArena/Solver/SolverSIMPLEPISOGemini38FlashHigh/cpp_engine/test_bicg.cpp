#include "amg_mixed_krylov.hpp"
#include <iostream>

using namespace hpc::solvers;

int main() {
    const std::size_t N = 16;
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
        A.values.push_back(2.1);
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

    SolverStats stats = solve_mixed_precision_amg(A, b, x, amg, 20, 1e-10);
    std::cout << "Converged: " << stats.converged << " iters: " << stats.iterations 
              << " res: " << stats.final_residual << " eta_round: " << stats.eta_round << std::endl;
    return 0;
}
