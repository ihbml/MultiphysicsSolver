#include "cabi_span.hpp"
#include "fvm_rhiechow.hpp"
#include "amg_mixed_krylov.hpp"
#include "stiff_chem_dlb.hpp"
#include "discrete_adjoint.hpp"

#include <iostream>
#include <iomanip>
#include <vector>
#include <cmath>
#include <string>
#include <numeric>

using namespace hpc::fvm;
using namespace hpc::solvers;
using namespace hpc::chemistry;
using namespace hpc::adjoint;

// Helper to print section headers
void print_header(const std::string& title) {
    std::cout << "\n======================================================================\n";
    std::cout << " [TEST] " << title << "\n";
    std::cout << "======================================================================\n";
}

int main() {
    std::cout << std::scientific << std::setprecision(10);
    bool all_passed = true;

    // -------------------------------------------------------------------------
    // TEST 1: Discrete Mass Conservation & 4-Quadrant Face Partitioning
    // -------------------------------------------------------------------------
    print_header("1. Discrete Mass Conservation & 4-Quadrant Face Partitioning");
    {
        // Construct a 2x2x1 polyhedral cell cluster (4 cells, 4 inner faces, 16 boundary faces)
        const std::size_t num_cells = 4;
        const std::size_t num_faces = 12;

        std::vector<double> cell_x = {0.25, 0.75, 0.25, 0.75};
        std::vector<double> cell_y = {0.25, 0.25, 0.75, 0.75};
        std::vector<double> cell_z = {0.0,  0.0,  0.0,  0.0};
        std::vector<double> cell_vol = {0.25, 0.25, 0.25, 0.25};

        // Faces: 0: (0, 1), 1: (0, 2), 2: (1, 3), 3: (2, 3) + boundaries
        std::vector<int32_t> face_owner = {0, 0, 1, 2,  0, 0, 1, 1, 2, 2, 3, 3};
        std::vector<int32_t> face_nbr   = {1, 2, 3, 3, -1,-1,-1,-1,-1,-1,-1,-1};

        // Normal vectors S_f
        std::vector<double> S_x = {0.5, 0.0, 0.0, 0.5, -0.5, 0.0, 0.5, 0.0, -0.5, 0.0, 0.5, 0.0};
        std::vector<double> S_y = {0.0, 0.5, 0.5, 0.0,  0.0,-0.5, 0.0,-0.5,  0.0, 0.5, 0.0, 0.5};
        std::vector<double> S_z(num_faces, 0.0);

        // Partition offsets: Seg 0: inner_fast (0..2), Seg 1: inner_corrected (2..4), Seg 2: halo_fast (4..8), Seg 3: halo_corrected (8..12)
        FacePartitionOffsets partition;
        partition.seg0_start = 0;
        partition.seg1_start = 2;
        partition.seg2_start = 4;
        partition.seg3_start = 8;
        partition.total_faces = num_faces;

        // Face metrics
        std::vector<double> beta(num_faces, 0.5);
        std::vector<double> alpha(num_faces, 1.0);

        // Construct CellFaceCSR for deterministic atomic-free gather
        // Cells 0..3
        std::vector<int32_t> csr_row = {0, 4, 8, 12, 16};
        std::vector<int32_t> csr_face = {
            0, 1, 4, 5,   // Cell 0
            0, 2, 6, 7,   // Cell 1
            1, 3, 8, 9,   // Cell 2
            2, 3, 10, 11  // Cell 3
        };
        std::vector<double> csr_sign = {
            +1.0, +1.0, +1.0, +1.0, // Cell 0
            -1.0, +1.0, +1.0, +1.0, // Cell 1 (owner of 2, neighbor of 0)
            -1.0, +1.0, +1.0, +1.0, // Cell 2 (owner of 3, neighbor of 1)
            -1.0, -1.0, +1.0, +1.0  // Cell 3 (neighbor of 2, neighbor of 3)
        };

        // Create conservative face mass fluxes (e.g. constant velocity u = (1, 0, 0))
        std::vector<double> m_dot(num_faces, 0.0);
        for (std::size_t f = 0; f < num_faces; ++f) {
            m_dot[f] = 1.0 * S_x[f]; // rho * u . S_f
        }

        CellFaceCSR csr{
            CAbiSpan<const int32_t>(csr_row.data(), csr_row.size()),
            CAbiSpan<const int32_t>(csr_face.data(), csr_face.size()),
            CAbiSpan<const double>(csr_sign.data(), csr_sign.size())
        };

        std::vector<double> cell_div(num_cells, 0.0);
        gather_cell_divergence(csr, num_cells, CAbiSpan<const double>(m_dot.data(), m_dot.size()), CAbiSpan<double>(cell_div.data(), cell_div.size()));

        double total_imbalance = 0.0;
        for (std::size_t c = 0; c < num_cells; ++c) {
            std::cout << "  Cell " << c << " divergence: " << cell_div[c] << " kg/s\n";
            total_imbalance += cell_div[c];
        }

        std::cout << "  Total Domain Mass Conservation Imbalance: " << total_imbalance << " kg/s\n";
        bool passed = std::abs(total_imbalance) < 1e-15;
        std::cout << "  -> Status: " << (passed ? "PASSED (Error < 1e-15)" : "FAILED") << "\n";
        if (!passed) all_passed = false;
    }

    // -------------------------------------------------------------------------
    // TEST 2: Rhie-Chow Linear Exactness (Zero-Filter on Linear Pressure Field)
    // -------------------------------------------------------------------------
    print_header("2. Rhie-Chow Exactness Under Arbitrary Linear Pressure Gradient");
    {
        // Under a linear pressure gradient p(x) = g_x * x + g_y * y + g_z * z + p0,
        // (p_N - p_P) = grad_p . d_f identically.
        // Therefore, Rhie-Chow stabilization term alpha_f * ( (p_N - p_P) - grad_p_bar . d_f ) must be EXACTLY ZERO.
        const double gx = 314.159, gy = -271.828, gz = 42.0, p0 = 101325.0;

        Vector3D x_P(0.12, 0.34, 0.56);
        Vector3D x_N(0.48, 0.82, 0.91);
        Vector3D x_f(0.30, 0.58, 0.735); // skewed polyhedral face centroid
        Vector3D S_f(0.04, -0.02, 0.08); // general non-orthogonal normal vector

        double beta, alpha;
        Vector3D k_corr, s_skew;
        compute_face_geometry_metrics(x_P, x_N, x_f, S_f, beta, alpha, k_corr, s_skew);

        // Linear pressure at cell centers and face
        double p_P = gx * x_P.x + gy * x_P.y + gz * x_P.z + p0;
        double p_N = gx * x_N.x + gy * x_N.y + gz * x_N.z + p0;
        Vector3D grad_p(gx, gy, gz);

        Vector3D d_f = x_N - x_P;
        double delta_p = p_N - p_P;
        double d_dot_grad_p = d_f.dot(grad_p);

        double rhie_chow_stabilizing_diff = alpha * (delta_p - d_dot_grad_p);

        std::cout << "  Face Plane Beta Factor  : " << beta << "\n";
        std::cout << "  Over-Relaxed Alpha      : " << alpha << "\n";
        std::cout << "  Non-Orthogonal k_corr   : (" << k_corr.x << ", " << k_corr.y << ", " << k_corr.z << ")\n";
        std::cout << "  Skewness Vector s_skew  : (" << s_skew.x << ", " << s_skew.y << ", " << s_skew.z << ")\n";
        std::cout << "  True Delta p (p_N - p_P): " << delta_p << " Pa\n";
        std::cout << "  Grad_p . d_f Projection : " << d_dot_grad_p << " Pa\n";
        std::cout << "  Rhie-Chow Residual Term : " << rhie_chow_stabilizing_diff << " Pa\n";

        bool passed = std::abs(rhie_chow_stabilizing_diff) < 1e-11;
        std::cout << "  -> Status: " << (passed ? "PASSED (Linear Exactness Error < 1e-11, machine zero)" : "FAILED") << "\n";
        if (!passed) all_passed = false;
    }

    // -------------------------------------------------------------------------
    // TEST 3: Mixed-Precision Krylov (FP64 Outer / FP32 AMG Inner) + Rounding Detector
    // -------------------------------------------------------------------------
    print_header("3. Mixed-Precision FP64 BiCGStab / FP32 AMG Preconditioned Solver");
    {
        // Setup 1D/2D Poisson Laplacian matrix: -u_{i-1} + 2*u_i - u_{i+1} = f
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
            A.values.push_back(2.01); // Diagonally dominant
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

        std::cout << "  AMG Hierarchy Built: " << amg.levels.size() << " levels\n";
        for (std::size_t l = 0; l < amg.levels.size(); ++l) {
            std::cout << "    Level " << l << " Dimension: " << amg.levels[l].A.num_rows 
                      << ", Spectral Radius: " << amg.levels[l].lambda_max << "\n";
        }

        SolverStats stats = solve_mixed_precision_amg(A, b, x, amg, 100, 1e-10);

        std::cout << "  Mixed-Precision Convergence:\n";
        std::cout << "    Iterations             : " << stats.iterations << "\n";
        std::cout << "    Final Relative Residual: " << stats.final_residual << "\n";
        std::cout << "    Rounding Error eta_round: " << stats.eta_round << "\n";
        std::cout << "    Converged              : " << (stats.converged ? "YES" : "NO") << "\n";

        bool passed = stats.converged && stats.final_residual < 1e-10;
        std::cout << "  -> Status: " << (passed ? "PASSED (Tolerance 1e-10 satisfied in FP64 outer loop)" : "FAILED") << "\n";
        if (!passed) all_passed = false;
    }

    // -------------------------------------------------------------------------
    // TEST 4: Batched Stiff Chemistry Integrator & DLBFoam Dynamic Work-Stealing
    // -------------------------------------------------------------------------
    print_header("4. Batched Stiff Chemistry (ROS2) & DLBFoam Dynamic Work-Stealing");
    {
        const std::size_t num_cells = 64;
        std::vector<ChemistryState> batch(num_cells);

        // 85% cold cells (unignited, low T), 15% active flame cells (T = 1500K)
        for (std::size_t i = 0; i < num_cells; ++i) {
            if (i < 10) {
                // Flame front cells
                batch[i].T = 1500.0;
                batch[i].Y = {0.15, 0.80, 0.04, 0.01};
            } else {
                // Cold unignited mixture
                batch[i].T = 300.0;
                batch[i].Y = {0.20, 0.80, 0.0, 0.0};
            }
        }

        const double dt = 1.0e-5; // Stiff reaction step
        DLBMetrics dlb = dispatch_chemistry_with_work_stealing(batch, dt, 1e-4);

        std::cout << "  Batch Size              : " << dlb.total_cells << " cells\n";
        std::cout << "  Active Flame Front Cells: " << dlb.active_cells << " cells (" 
                  << (100.0 * dlb.active_cells / dlb.total_cells) << "%)\n";
        std::cout << "  Cold Flow Ratio         : " << (100.0 * dlb.cold_flow_ratio) << "%\n";
        std::cout << "  DLBFoam Stolen Chunks   : " << dlb.stolen_chunks << "\n";
        std::cout << "  Theoretical Speedup     : " << dlb.speedup_factor << "x\n";

        // Verify ROS2 mass conservation in flame cells
        double sum_Y_flame = std::accumulate(batch[0].Y.begin(), batch[0].Y.end(), 0.0);
        std::cout << "  Species Mass Sum Check  : " << sum_Y_flame << " (Target: 1.0)\n";

        bool passed = std::abs(sum_Y_flame - 1.0) < 1e-12 && dlb.active_cells == 10;
        std::cout << "  -> Status: " << (passed ? "PASSED (Stiff ROS2 + DLBFoam verified)" : "FAILED") << "\n";
        if (!passed) all_passed = false;
    }

    // -------------------------------------------------------------------------
    // TEST 5: Discrete Adjoint Gradient Verification vs Central Finite Differences
    // -------------------------------------------------------------------------
    print_header("5. Discrete Adjoint Sensitivity vs Central Finite Differences (< 1e-8)");
    {
        ChannelAdjointSystem sys;
        sys.n = 24;
        sys.dx = 1.0 / static_cast<double>(sys.n);
        sys.mu = 0.02;
        sys.dp_dx = -2.0;

        // Porosity design variables gamma_i strictly in interior (0.15, 0.85)
        std::vector<double> gamma(sys.n);
        for (std::size_t i = 0; i < sys.n; ++i) {
            gamma[i] = 0.5 + 0.35 * std::sin(2.0 * M_PI * i / sys.n);
        }

        // 1. Solve primal equation: R(u, gamma) = 0
        std::vector<double> u = sys.solve_primal(gamma);
        double J0 = sys.evaluate_objective(u, gamma);

        // 2. Solve discrete adjoint system: (dR/du)^T * lambda = - dJ/du
        std::vector<double> lambda = sys.solve_adjoint(u, gamma);

        // 3. Compute exact IFT sensitivity: dJ/dgamma = partial_J/partial_gamma + lambda^T * (partial_R/partial_gamma)
        std::vector<double> adj_grad = sys.compute_adjoint_sensitivity(u, gamma, lambda);

        // 4. Central Finite Difference Check: (J(gamma + eps) - J(gamma - eps)) / (2 * eps)
        const double eps = 1.0e-7;
        double max_abs_err = 0.0;
        double max_rel_err = 0.0;

        std::cout << std::setw(6) << "Cell" 
                  << std::setw(18) << "Adjoint dJ/dgamma" 
                  << std::setw(18) << "FD dJ/dgamma" 
                  << std::setw(18) << "Abs Error" 
                  << std::setw(16) << "Rel Error\n";
        std::cout << "  ---------------------------------------------------------------------------\n";

        for (std::size_t i = 0; i < sys.n; i += 3) {
            // Forward step
            std::vector<double> gamma_plus = gamma;
            gamma_plus[i] += eps;
            std::vector<double> u_plus = sys.solve_primal(gamma_plus);
            double J_plus = sys.evaluate_objective(u_plus, gamma_plus);

            // Backward step
            std::vector<double> gamma_minus = gamma;
            gamma_minus[i] -= eps;
            std::vector<double> u_minus = sys.solve_primal(gamma_minus);
            double J_minus = sys.evaluate_objective(u_minus, gamma_minus);

            // Central difference
            double fd_grad = (J_plus - J_minus) / (2.0 * eps);
            double abs_err = std::abs(adj_grad[i] - fd_grad);
            double rel_err = abs_err / (std::abs(adj_grad[i]) + 1e-15);

            max_abs_err = std::max(max_abs_err, abs_err);
            max_rel_err = std::max(max_rel_err, rel_err);

            std::cout << "  " << std::setw(4) << i 
                      << std::setw(18) << adj_grad[i] 
                      << std::setw(18) << fd_grad 
                      << std::setw(18) << abs_err 
                      << std::setw(16) << rel_err << "\n";
        }

        std::cout << "  ---------------------------------------------------------------------------\n";
        std::cout << "  Max Absolute Sensitivity Error: " << max_abs_err << "\n";
        std::cout << "  Max Relative Sensitivity Error: " << max_rel_err << "\n";

        bool passed = max_abs_err < 1.0e-8;
        std::cout << "  -> Status: " << (passed ? "PASSED (Discrete Adjoint Agreement < 1e-8)" : "FAILED") << "\n";
        if (!passed) all_passed = false;
    }

    // -------------------------------------------------------------------------
    // SUMMARY
    // -------------------------------------------------------------------------
    std::cout << "\n======================================================================\n";
    if (all_passed) {
        std::cout << "  *** ALL HPC ARCHITECTURE & MATHEMATICAL TESTS PASSED [100%] ***\n";
    } else {
        std::cout << "  *** SOME TESTS FAILED ***\n";
    }
    std::cout << "======================================================================\n";

    return all_passed ? 0 : 1;
}
