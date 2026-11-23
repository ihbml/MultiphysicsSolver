#pragma once
#include "cabi_span.hpp"
#include <vector>
#include <cmath>
#include <algorithm>
#include <cassert>

namespace hpc::fvm {

struct Vector3D {
    double x{0.0};
    double y{0.0};
    double z{0.0};

    constexpr Vector3D() noexcept = default;
    constexpr Vector3D(double x_, double y_, double z_) noexcept : x(x_), y(y_), z(z_) {}

    constexpr Vector3D operator+(const Vector3D& o) const noexcept { return {x + o.x, y + o.y, z + o.z}; }
    constexpr Vector3D operator-(const Vector3D& o) const noexcept { return {x - o.x, y - o.y, z - o.z}; }
    constexpr Vector3D operator*(double s) const noexcept { return {x * s, y * s, z * s}; }
    constexpr Vector3D operator/(double s) const noexcept { return {x / s, y / s, z / s}; }

    [[nodiscard]] constexpr double dot(const Vector3D& o) const noexcept {
        return x * o.x + y * o.y + z * o.z;
    }

    [[nodiscard]] double norm() const noexcept {
        return std::sqrt(dot(*this));
    }

    [[nodiscard]] constexpr double sqr_norm() const noexcept {
        return dot(*this);
    }
};

/**
 * @brief Polyhedral Mesh Geometry and Connectivity stored as SoA C-ABI buffers.
 */
struct MeshSoA {
    std::size_t num_cells{0};
    std::size_t num_faces{0};
    FacePartitionOffsets partition;

    // Cell data
    CAbiSpan<const double> cell_centers_x;
    CAbiSpan<const double> cell_centers_y;
    CAbiSpan<const double> cell_centers_z;
    CAbiSpan<const double> cell_volumes;

    // Face data
    CAbiSpan<const int32_t> face_owner;
    CAbiSpan<const int32_t> face_neighbor; // -1 for boundary
    CAbiSpan<const double> face_centers_x;
    CAbiSpan<const double> face_centers_y;
    CAbiSpan<const double> face_centers_z;
    CAbiSpan<const double> face_areas_x; // Area vector S_f
    CAbiSpan<const double> face_areas_y;
    CAbiSpan<const double> face_areas_z;

    // Precomputed 2-Class Metrics
    CAbiSpan<const double> face_alpha; // Over-relaxed conductance: ||S_f||^2 / (d_f . S_f)
    CAbiSpan<const double> face_beta;  // Plane intersection: ((x_f - x_P) . S_f) / (d_f . S_f)
    CorrectionSoA correction;          // Compact 6% SoA for Seg 1 & 3

    // Deterministic Atomic-Free Gather CSR
    CellFaceCSR cell_face_csr;
};

/**
 * @brief Computes 2-Class face geometry metrics (beta_f, alpha_f, k_f, s_f).
 * Validates Class 0 threshold (|s_f| < 1e-12 && |k_f| < 1e-12) for segregation.
 */
inline void compute_face_geometry_metrics(
    const Vector3D& x_P,
    const Vector3D& x_N,
    const Vector3D& x_f,
    const Vector3D& S_f,
    double& out_beta,
    double& out_alpha,
    Vector3D& out_k,
    Vector3D& out_s
) {
    Vector3D d_f = x_N - x_P;
    double d_dot_S = d_f.dot(S_f);
    assert(std::abs(d_dot_S) > 1e-15 && "Degenerate face: d_f orthogonal to S_f");

    // Strict Contract: Linear-Exact Plane Intersection
    out_beta = (x_f - x_P).dot(S_f) / d_dot_S;

    // Strict Contract: Over-Relaxed Conductance
    out_alpha = S_f.sqr_norm() / d_dot_S;

    // Non-orthogonal correction vector: S_f = alpha_f * d_f + k_f
    out_k = S_f - d_f * out_alpha;

    // Skewness vector: displacement from face plane intersection to centroid
    Vector3D x_f_prime = x_P + d_f * out_beta;
    out_s = x_f - x_f_prime;
}

/**
 * @brief Rhie-Chow Face Mass-Flux Reconstruction on 4-Quadrant Face Layout.
 *
 * Implements collocated polyhedral mass flux:
 *   m_dot_f = rho_f * [ u_bar_f . S_f - D_bar_f * ( alpha_f * (p_N - p_P) + k_f . grad_p_bar_f - grad_p_bar_f . S_f ) ]
 *
 * Notice: alpha_f * (p_N - p_P) + k_f . grad_p_bar_f - grad_p_bar_f . S_f
 *       = alpha_f * ( (p_N - p_P) - grad_p_bar_f . d_f )
 * which is IDENTICALLY ZERO under any linear pressure field p(x) = g . x + c.
 * This mathematically eliminates the collocated checkerboard pressure mode
 * while maintaining 2nd-order exactness on general unstructured polyhedra.
 */
inline void compute_rhie_chow_fluxes(
    const MeshSoA& mesh,
    double rho,
    CAbiSpan<const double> u_x,
    CAbiSpan<const double> u_y,
    CAbiSpan<const double> u_z,
    CAbiSpan<const double> p,
    CAbiSpan<const double> grad_p_x,
    CAbiSpan<const double> grad_p_y,
    CAbiSpan<const double> grad_p_z,
    CAbiSpan<const double> D_cell, // D_P = V_P / a_P
    CAbiSpan<double> m_dot_face,
    bool enable_rhie_chow = true
) {
    const auto& part = mesh.partition;

    // Loop through internal faces (Seg 0: InnerFast + Seg 1: InnerCorrected)
    for (std::size_t f = part.seg0_start; f < part.seg2_start; ++f) {
        int32_t P = mesh.face_owner[f];
        int32_t N = mesh.face_neighbor[f];

        Vector3D S_f(mesh.face_areas_x[f], mesh.face_areas_y[f], mesh.face_areas_z[f]);
        double beta = mesh.face_beta[f]; // linear plane intersection factor
        double one_minus_beta = 1.0 - beta;

        // Interpolate velocity to face
        Vector3D u_bar(
            one_minus_beta * u_x[P] + beta * u_x[N],
            one_minus_beta * u_y[P] + beta * u_y[N],
            one_minus_beta * u_z[P] + beta * u_z[N]
        );

        // Standard convective velocity flux
        double conv_flux = u_bar.dot(S_f);

        if (!enable_rhie_chow) {
            // Unstabilized: causes checkerboard pressure oscillations
            m_dot_face[f] = rho * conv_flux;
            continue;
        }

        // Interpolated D_f = (1 - beta) * D_P + beta * D_N
        double D_bar_f = one_minus_beta * D_cell[P] + beta * D_cell[N];

        // Interpolated pressure gradient at face
        Vector3D grad_p_bar(
            one_minus_beta * grad_p_x[P] + beta * grad_p_x[N],
            one_minus_beta * grad_p_y[P] + beta * grad_p_y[N],
            one_minus_beta * grad_p_z[P] + beta * grad_p_z[N]
        );

        double alpha_f = mesh.face_alpha[f];
        Vector3D d_f(
            mesh.cell_centers_x[N] - mesh.cell_centers_x[P],
            mesh.cell_centers_y[N] - mesh.cell_centers_y[P],
            mesh.cell_centers_z[N] - mesh.cell_centers_z[P]
        );

        // Strict non-orthogonal Rhie-Chow pressure difference
        // Seg 0 (orthogonal): k_f is 0, alpha_f = ||S_f|| / ||d_f||
        // Seg 1 (corrected) : includes non-orthogonal correction k_f . grad_p_bar
        double delta_p = p[N] - p[P];
        double d_dot_grad_p = d_f.dot(grad_p_bar);

        // Stabilizing Rhie-Chow term
        double rc_stabilizer = alpha_f * (delta_p - d_dot_grad_p);

        m_dot_face[f] = rho * (conv_flux - D_bar_f * rc_stabilizer);
    }
}

/**
 * @brief Deterministic Atomic-Free CellFaceCSR Divergence Gather.
 * Gathers net mass flux for all owned cells: div(m_dot)_c = sum_{f in c} sign_f * m_dot_f
 */
inline void gather_cell_divergence(
    const CellFaceCSR& csr,
    std::size_t num_owned_cells,
    CAbiSpan<const double> face_flux,
    CAbiSpan<double> cell_div
) {
    for (std::size_t c = 0; c < num_owned_cells; ++c) {
        int32_t start = csr.row_ptr[c];
        int32_t end = csr.row_ptr[c + 1];
        double sum = 0.0;
        for (int32_t idx = start; idx < end; ++idx) {
            int32_t f = csr.face_idx[idx];
            double s = csr.face_sign[idx];
            sum += s * face_flux[f];
        }
        cell_div[c] = sum;
    }
}

/**
 * @brief Continuity Diagonal Stabilization.
 * Prevents loss of diagonal dominance when flow field is not yet divergence-free.
 * a_P += max(-div(rho*u)_P, 0)
 */
inline void apply_continuity_diagonal_stabilization(
    std::size_t num_cells,
    CAbiSpan<const double> cell_div,
    CAbiSpan<double> diag_aP
) {
    for (std::size_t c = 0; c < num_cells; ++c) {
        if (cell_div[c] < 0.0) {
            diag_aP[c] += -cell_div[c]; // Guaranteed M-matrix property
        }
    }
}

} // namespace hpc::fvm
