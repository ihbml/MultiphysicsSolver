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
        A.values.push_back(2.5);
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

    std::cout << "Levels: " << amg.levels.size() << std::endl;
    for (size_t l = 0; l < amg.levels.size(); ++l) {
        std::cout << "L" << l << " rows=" << amg.levels[l].A.num_rows << " lambda_max=" << amg.levels[l].lambda_max << std::endl;
    }

    std::vector<float> rf(N, 1.0f);
    std::vector<float> zf(N, 0.0f);
    amg.v_cycle(rf, zf);

    std::cout << "zf[0]=" << zf[0] << " zf[N/2]=" << zf[N/2] << std::endl;

    return 0;
}
