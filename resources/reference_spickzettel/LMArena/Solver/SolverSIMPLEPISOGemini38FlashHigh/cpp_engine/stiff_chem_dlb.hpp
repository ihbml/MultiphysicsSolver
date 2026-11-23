#pragma once
#include <vector>
#include <array>
#include <cmath>
#include <atomic>
#include <numeric>
#include <algorithm>
#include <cstdint>
#include <chrono>

namespace hpc::chemistry {

/**
 * @brief 4-Species Reduced Hydrogen/Oxygen Kinetic Model:
 *   Species: [0: H2, 1: O2, 2: OH, 3: H2O]
 *   Reactions:
 *     R1: H2 + O2 <-> 2 OH
 *     R2: OH + H2 <-> H2O + H (simplified to OH + H2 -> H2O)
 *   Demonstrates extreme stiffness with time scales ranging from 10^-8 s to 10^-3 s.
 */
constexpr std::size_t NUM_SPECIES = 4;
constexpr double MOLAR_MASS[NUM_SPECIES] = {2.016e-3, 31.999e-3, 17.008e-3, 18.015e-3}; // kg/mol

struct ChemistryState {
    std::array<double, NUM_SPECIES> Y{0.2, 0.8, 0.0, 0.0}; // Mass fractions
    double T{1200.0};                                      // Temperature [K]
    double rho{1.18};                                      // Density [kg/m^3]
};

/**
 * @brief Ahead-of-Time (AoT) Analytical Sparse Production Rate & Jacobian.
 * Evaluates f = dY/dt and J_kj = d(dY_k/dt)/dY_j.
 */
inline void compute_rates_and_jacobian(
    const ChemistryState& state,
    std::array<double, NUM_SPECIES>& omega_dot,
    std::array<std::array<double, NUM_SPECIES>, NUM_SPECIES>& J
) {
    const double T = state.T;
    const double rho = state.rho;

    // Concentrations C_k = rho * Y_k / W_k [mol/m^3]
    std::array<double, NUM_SPECIES> C;
    for (std::size_t i = 0; i < NUM_SPECIES; ++i) {
        C[i] = std::max(0.0, rho * state.Y[i] / MOLAR_MASS[i]);
    }

    // Arrhenius rate constants
    // R1: H2 + O2 -> 2 OH
    const double k1_f = 1.5e8 * std::exp(-15000.0 / T);
    const double k1_b = 2.0e7 * std::exp(-8000.0 / T);

    // R2: OH + H2 -> H2O (fast radical branching/propagation -> high stiffness)
    const double k2_f = 8.0e9 * std::exp(-3500.0 / T);

    // Reaction rates [mol/(m^3 s)]
    double q1 = k1_f * C[0] * C[1] - k1_b * C[2] * C[2];
    double q2 = k2_f * C[2] * C[0];

    // Net molar production rates: dC_k / dt
    double dC0 = -q1 - q2;
    double dC1 = -q1;
    double dC2 = 2.0 * q1 - q2;
    double dC3 = q2;

    // Convert to mass production rates: omega_dot_k = W_k * dC_k / rho [1/s]
    omega_dot[0] = (MOLAR_MASS[0] / rho) * dC0;
    omega_dot[1] = (MOLAR_MASS[1] / rho) * dC1;
    omega_dot[2] = (MOLAR_MASS[2] / rho) * dC2;
    omega_dot[3] = (MOLAR_MASS[3] / rho) * dC3;

    // Analytical sparse Jacobian d(omega_dot_k) / dY_j
    // dC_j / dY_j = rho / W_j
    for (std::size_t r = 0; r < NUM_SPECIES; ++r) {
        for (std::size_t c = 0; c < NUM_SPECIES; ++c) {
            J[r][c] = 0.0;
        }
    }

    // Derivatives of q1
    double dq1_dY0 = k1_f * (rho / MOLAR_MASS[0]) * C[1];
    double dq1_dY1 = k1_f * C[0] * (rho / MOLAR_MASS[1]);
    double dq1_dY2 = -2.0 * k1_b * C[2] * (rho / MOLAR_MASS[2]);

    // Derivatives of q2
    double dq2_dY0 = k2_f * C[2] * (rho / MOLAR_MASS[0]);
    double dq2_dY2 = k2_f * C[0] * (rho / MOLAR_MASS[2]);

    // Row 0: H2
    J[0][0] = (MOLAR_MASS[0] / rho) * (-dq1_dY0 - dq2_dY0);
    J[0][1] = (MOLAR_MASS[0] / rho) * (-dq1_dY1);
    J[0][2] = (MOLAR_MASS[0] / rho) * (-dq1_dY2 - dq2_dY2);

    // Row 1: O2
    J[1][0] = (MOLAR_MASS[1] / rho) * (-dq1_dY0);
    J[1][1] = (MOLAR_MASS[1] / rho) * (-dq1_dY1);
    J[1][2] = (MOLAR_MASS[1] / rho) * (-dq1_dY2);

    // Row 2: OH
    J[2][0] = (MOLAR_MASS[2] / rho) * (2.0 * dq1_dY0 - dq2_dY0);
    J[2][1] = (MOLAR_MASS[2] / rho) * (2.0 * dq1_dY1);
    J[2][2] = (MOLAR_MASS[2] / rho) * (2.0 * dq1_dY2 - dq2_dY2);

    // Row 3: H2O
    J[3][0] = (MOLAR_MASS[3] / rho) * (dq2_dY0);
    J[3][2] = (MOLAR_MASS[3] / rho) * (dq2_dY2);
}

/**
 * @brief Dense 4x4 linear solver using LU factorization with partial pivoting.
 * Solves (I - gamma * dt * J) * k = rhs.
 */
inline bool solve_dense_4x4(
    std::array<std::array<double, NUM_SPECIES>, NUM_SPECIES> A,
    const std::array<double, NUM_SPECIES>& b,
    std::array<double, NUM_SPECIES>& x
) {
    std::array<int, NUM_SPECIES> p{0, 1, 2, 3};

    // Factorization
    for (std::size_t i = 0; i < NUM_SPECIES; ++i) {
        // Pivot
        std::size_t max_row = i;
        double max_val = std::abs(A[i][i]);
        for (std::size_t r = i + 1; r < NUM_SPECIES; ++r) {
            if (std::abs(A[r][i]) > max_val) {
                max_val = std::abs(A[r][i]);
                max_row = r;
            }
        }
        if (max_val < 1e-30) return false; // Singular

        if (max_row != i) {
            std::swap(A[i], A[max_row]);
            std::swap(p[i], p[max_row]);
        }

        for (std::size_t r = i + 1; r < NUM_SPECIES; ++r) {
            A[r][i] /= A[i][i];
            for (std::size_t c = i + 1; c < NUM_SPECIES; ++c) {
                A[r][c] -= A[r][i] * A[i][c];
            }
        }
    }

    // Forward substitution
    std::array<double, NUM_SPECIES> y;
    for (std::size_t i = 0; i < NUM_SPECIES; ++i) {
        double sum = b[p[i]];
        for (std::size_t j = 0; j < i; ++j) {
            sum -= A[i][j] * y[j];
        }
        y[i] = sum;
    }

    // Backward substitution
    for (int i = static_cast<int>(NUM_SPECIES) - 1; i >= 0; --i) {
        double sum = y[i];
        for (std::size_t j = i + 1; j < NUM_SPECIES; ++j) {
            sum -= A[i][j] * x[j];
        }
        x[i] = sum / A[i][i];
    }

    return true;
}

/**
 * @brief Rosenbrock-W Stiff ODE Integrator (ROS2 / ROW6A).
 * Integrates dY/dt over Delta t with L-stable accuracy for stiff reactions.
 */
inline void integrate_rosenbrock2(
    ChemistryState& state,
    double dt
) {
    const double gamma = 1.0 - 1.0 / std::sqrt(2.0); // 0.2928932188
    std::array<double, NUM_SPECIES> f0;
    std::array<std::array<double, NUM_SPECIES>, NUM_SPECIES> J;

    compute_rates_and_jacobian(state, f0, J);

    // Form system matrix M = I - gamma * dt * J
    std::array<std::array<double, NUM_SPECIES>, NUM_SPECIES> M;
    for (std::size_t i = 0; i < NUM_SPECIES; ++i) {
        for (std::size_t j = 0; j < NUM_SPECIES; ++j) {
            M[i][j] = (i == j ? 1.0 : 0.0) - gamma * dt * J[i][j];
        }
    }

    // Solve stage 1: M * k1 = f0
    std::array<double, NUM_SPECIES> k1;
    if (!solve_dense_4x4(M, f0, k1)) return;

    // Stage 2
    ChemistryState state_stage1 = state;
    for (std::size_t i = 0; i < NUM_SPECIES; ++i) {
        state_stage1.Y[i] += dt * k1[i];
    }
    std::array<double, NUM_SPECIES> f1;
    std::array<std::array<double, NUM_SPECIES>, NUM_SPECIES> J_dummy;
    compute_rates_and_jacobian(state_stage1, f1, J_dummy);

    // RHS2 = f1 - 2 * gamma * dt * J * k1
    std::array<double, NUM_SPECIES> J_k1{0, 0, 0, 0};
    for (std::size_t i = 0; i < NUM_SPECIES; ++i) {
        for (std::size_t j = 0; j < NUM_SPECIES; ++j) {
            J_k1[i] += J[i][j] * k1[j];
        }
    }

    std::array<double, NUM_SPECIES> rhs2;
    for (std::size_t i = 0; i < NUM_SPECIES; ++i) {
        rhs2[i] = f1[i] - 2.0 * gamma * dt * J_k1[i];
    }

    std::array<double, NUM_SPECIES> k2;
    if (!solve_dense_4x4(M, rhs2, k2)) return;

    // Update state Y^{n+1} = Y^n + dt * 0.5 * (k1 + k2)
    double sum_Y = 0.0;
    for (std::size_t i = 0; i < NUM_SPECIES; ++i) {
        state.Y[i] = std::max(0.0, state.Y[i] + dt * 0.5 * (k1[i] + k2[i]));
        sum_Y += state.Y[i];
    }
    // Normalize mass fractions
    if (sum_Y > 1e-15) {
        for (std::size_t i = 0; i < NUM_SPECIES; ++i) state.Y[i] /= sum_Y;
    }
}

/**
 * @brief DLBFoam Dynamic Work-Stealing Active-Cell Compaction Dispatcher.
 */
struct DLBMetrics {
    std::size_t total_cells{0};
    std::size_t active_cells{0};
    std::size_t stolen_chunks{0};
    double speedup_factor{1.0};
    double cold_flow_ratio{0.0};
};

inline DLBMetrics dispatch_chemistry_with_work_stealing(
    std::vector<ChemistryState>& cell_states,
    double dt,
    double eps_react = 1e-5
) {
    const std::size_t n_cells = cell_states.size();
    DLBMetrics metrics;
    metrics.total_cells = n_cells;

    // 1. Active-cell compaction pass: identify cells with ||omega_dot|| > eps_react
    std::vector<int32_t> active_indices;
    active_indices.reserve(n_cells);

    for (std::size_t c = 0; c < n_cells; ++c) {
        std::array<double, NUM_SPECIES> w;
        std::array<std::array<double, NUM_SPECIES>, NUM_SPECIES> J_dummy;
        compute_rates_and_jacobian(cell_states[c], w, J_dummy);

        double rate_norm = 0.0;
        for (double val : w) rate_norm += val * val;
        rate_norm = std::sqrt(rate_norm);

        if (rate_norm > eps_react) {
            active_indices.push_back(static_cast<int32_t>(c));
        }
    }

    metrics.active_cells = active_indices.size();
    metrics.cold_flow_ratio = 1.0 - static_cast<double>(metrics.active_cells) / static_cast<double>(n_cells);

    // 2. Dynamic work-stealing queue simulation
    std::atomic<std::size_t> work_queue_head{0};
    const std::size_t chunk_size = 8;
    const std::size_t num_warps = 8; // Simulated thread warps

    // Each warp executes chunks atomically until queue is exhausted
    std::size_t active_count = active_indices.size();
    while (true) {
        std::size_t chunk_start = work_queue_head.fetch_add(chunk_size);
        if (chunk_start >= active_count) break;

        std::size_t chunk_end = std::min(chunk_start + chunk_size, active_count);
        metrics.stolen_chunks++;

        for (std::size_t idx = chunk_start; idx < chunk_end; ++idx) {
            int32_t c = active_indices[idx];
            // Execute stiff Rosenbrock-W integration
            integrate_rosenbrock2(cell_states[c], dt);
        }
    }

    // Theoretical speedup vs naive all-cell stiff integration
    metrics.speedup_factor = (metrics.cold_flow_ratio > 0.0) 
        ? (1.0 / (1.0 - metrics.cold_flow_ratio + 0.02)) // 2% compaction overhead
        : 1.0;

    return metrics;
}

} // namespace hpc::chemistry
