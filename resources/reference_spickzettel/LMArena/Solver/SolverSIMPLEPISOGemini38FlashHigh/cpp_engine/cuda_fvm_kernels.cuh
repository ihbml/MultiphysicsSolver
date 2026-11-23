#pragma once
/**
 * @file cuda_fvm_kernels.cuh
 * @brief Zero-copy CUDA kernels operating on C-ABI SoA Data Lake and 4-Quadrant Face Partition.
 * Compatible with LLVM Enzyme (__enzyme_autodiff) and CUDA 12+ compute capability 8.0+.
 */

#include "cabi_span.hpp"
#include <cuda_runtime.h>

namespace hpc::cuda {

/**
 * @brief CUDA Kernel: Rhie-Chow Face Mass-Flux Evaluation on Seg 0 (inner_fast).
 * Seg 0 faces have zero non-orthogonality and zero skewness.
 * Eliminates warp divergence by running on pure orthogonal face quadrants.
 */
__global__ void kernel_rhie_chow_seg0_fast(
    std::size_t seg0_size,
    const int32_t* __restrict__ face_owner,
    const int32_t* __restrict__ face_neighbor,
    const double* __restrict__ face_area_x,
    const double* __restrict__ face_area_y,
    const double* __restrict__ face_area_z,
    const double* __restrict__ face_alpha,
    const double* __restrict__ face_beta,
    const double* __restrict__ u_x,
    const double* __restrict__ u_y,
    const double* __restrict__ u_z,
    const double* __restrict__ p,
    const double* __restrict__ grad_p_x,
    const double* __restrict__ grad_p_y,
    const double* __restrict__ grad_p_z,
    const double* __restrict__ D_cell,
    const double* __restrict__ cell_x,
    const double* __restrict__ cell_y,
    const double* __restrict__ cell_z,
    double rho,
    double* __restrict__ m_dot_face
) {
    std::size_t f = blockIdx.x * blockDim.x + threadIdx.x;
    if (f >= seg0_size) return;

    int32_t P = face_owner[f];
    int32_t N = face_neighbor[f];

    double beta = face_beta[f];
    double one_m_beta = 1.0 - beta;

    // Face velocity interpolation
    double uf_x = one_m_beta * u_x[P] + beta * u_x[N];
    double uf_y = one_m_beta * u_y[P] + beta * u_y[N];
    double uf_z = one_m_beta * u_z[P] + beta * u_z[N];

    double conv_flux = uf_x * face_area_x[f] + uf_y * face_area_y[f] + uf_z * face_area_z[f];

    // Face D factor
    double Df = one_m_beta * D_cell[P] + beta * D_cell[N];

    // Face pressure gradient
    double gpf_x = one_m_beta * grad_p_x[P] + beta * grad_p_x[N];
    double gpf_y = one_m_beta * grad_p_y[P] + beta * grad_p_y[N];
    double gpf_z = one_m_beta * grad_p_z[P] + beta * grad_p_z[N];

    double dx = cell_x[N] - cell_x[P];
    double dy = cell_y[N] - cell_y[P];
    double dz = cell_z[N] - cell_z[P];

    double dp = p[N] - p[P];
    double d_dot_gp = dx * gpf_x + dy * gpf_y + dz * gpf_z;

    double alpha_f = face_alpha[f];
    double rc_stabilizer = alpha_f * (dp - d_dot_gp);

    m_dot_face[f] = rho * (conv_flux - Df * rc_stabilizer);
}

/**
 * @brief CUDA Kernel: Atomic-Free CellFaceCSR Divergence Gather.
 * Each CUDA thread processes one owned cell; gathers across faces incident to cell.
 * Zero atomic operations -> optimal memory throughput.
 */
__global__ void kernel_gather_cell_divergence_csr(
    std::size_t num_owned_cells,
    const int32_t* __restrict__ row_ptr,
    const int32_t* __restrict__ face_idx,
    const double* __restrict__ face_sign,
    const double* __restrict__ face_flux,
    double* __restrict__ cell_div
) {
    std::size_t c = blockIdx.x * blockDim.x + threadIdx.x;
    if (c >= num_owned_cells) return;

    int32_t start = row_ptr[c];
    int32_t end = row_ptr[c + 1];

    double div = 0.0;
    #pragma unroll 4
    for (int32_t k = start; k < end; ++k) {
        int32_t f = face_idx[k];
        double s = face_sign[k];
        div += s * face_flux[f];
    }
    cell_div[c] = div;
}

/**
 * @brief CUDA Kernel: GPU L1-Jacobi Smoother for FP32 AMG.
 * Fully parallel across all matrix rows without sequential dependencies.
 */
__global__ void kernel_l1_jacobi_smooth_fp32(
    std::size_t num_rows,
    const int32_t* __restrict__ row_ptr,
    const int32_t* __restrict__ col_idx,
    const float* __restrict__ values,
    const float* __restrict__ diag_l1,
    const float* __restrict__ b,
    const float* __restrict__ x_in,
    float* __restrict__ x_out,
    float omega
) {
    std::size_t i = blockIdx.x * blockDim.x + threadIdx.x;
    if (i >= num_rows) return;

    int32_t start = row_ptr[i];
    int32_t end = row_ptr[i + 1];

    float Ax = 0.0f;
    for (int32_t k = start; k < end; ++k) {
        Ax += values[k] * x_in[col_idx[k]];
    }

    float residual = b[i] - Ax;
    x_out[i] = x_in[i] + omega * (residual / diag_l1[i]);
}

} // namespace hpc::cuda
