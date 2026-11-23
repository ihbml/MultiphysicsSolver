#pragma once
#include <cstddef>
#include <cstdint>
#include <concepts>
#include <span>
#include <vector>
#include <stdexcept>
#include <array>
#include <cmath>

namespace hpc::fvm {

/**
 * @brief Zero-copy C-ABI compatible span for Strided Structure-of-Arrays (SoA) Data Lake.
 * Enables direct zero-copy memory mapping from CUDA unified memory, pinned host memory,
 * or IPC shared memory without data marshaling.
 */
template <typename T>
struct CAbiSpan {
    T* data_{nullptr};
    std::size_t size_{0};
    std::size_t stride_{1}; // In elements of T (e.g., 1 for contiguous, N for interleaved/pitched)

    constexpr CAbiSpan() noexcept = default;
    constexpr CAbiSpan(T* ptr, std::size_t count, std::size_t stride = 1) noexcept
        : data_(ptr), size_(count), stride_(stride) {}

    [[nodiscard]] constexpr T& operator[](std::size_t idx) const noexcept {
        return data_[idx * stride_];
    }

    [[nodiscard]] constexpr T* data() const noexcept { return data_; }
    [[nodiscard]] constexpr std::size_t size() const noexcept { return size_; }
    [[nodiscard]] constexpr std::size_t stride() const noexcept { return stride_; }
    [[nodiscard]] constexpr bool empty() const noexcept { return size_ == 0; }

    // Subspan creation
    [[nodiscard]] constexpr CAbiSpan<T> subspan(std::size_t offset, std::size_t count) const {
        if (offset + count > size_) throw std::out_of_range("CAbiSpan::subspan out of range");
        return CAbiSpan<T>(data_ + offset * stride_, count, stride_);
    }
};

/**
 * @brief 4-Quadrant Face Partitioning Layout.
 * Quadrants are topologically and geometrically segregated to eliminate branching
 * inside GPU warp/vector execution units:
 *   - Seg 0: inner_fast      -> purely orthogonal internal faces (|s_f| < eps, |k_f| < eps)
 *   - Seg 1: inner_corrected -> skewed / non-orthogonal internal faces (requires CorrectionSoA)
 *   - Seg 2: halo_fast       -> orthogonal halo/boundary faces
 *   - Seg 3: halo_corrected  -> non-orthogonal halo/boundary faces
 */
enum class FaceQuadrant : uint8_t {
    Seg0_InnerFast = 0,
    Seg1_InnerCorrected = 1,
    Seg2_HaloFast = 2,
    Seg3_HaloCorrected = 3
};

struct FacePartitionOffsets {
    std::size_t seg0_start{0}; // inner_fast
    std::size_t seg1_start{0}; // inner_corrected
    std::size_t seg2_start{0}; // halo_fast
    std::size_t seg3_start{0}; // halo_corrected
    std::size_t total_faces{0};

    [[nodiscard]] constexpr std::size_t seg0_size() const noexcept { return seg1_start - seg0_start; }
    [[nodiscard]] constexpr std::size_t seg1_size() const noexcept { return seg2_start - seg1_start; }
    [[nodiscard]] constexpr std::size_t seg2_size() const noexcept { return seg3_start - seg2_start; }
    [[nodiscard]] constexpr std::size_t seg3_size() const noexcept { return total_faces - seg3_start; }
    [[nodiscard]] constexpr std::size_t inner_faces() const noexcept { return seg2_start; }
    [[nodiscard]] constexpr std::size_t boundary_faces() const noexcept { return total_faces - seg2_start; }
};

/**
 * @brief Compact 6% CorrectionSoA for Seg 1 and Seg 3 faces.
 * Storing only 7 floats per corrected face saves 94% memory bandwidth compared to full tensors:
 *   - sx, sy, sz: Skewness vector s_f = x_f - (x_P + beta_f * d_f)
 *   - kx, ky, kz: Non-orthogonal correction vector k_f = S_f - alpha_f * d_f
 *   - omega     : Geometric weighting factor beta_f = ((x_f - x_P) . S_f) / (d_f . S_f)
 */
struct CorrectionSoA {
    CAbiSpan<double> sx;
    CAbiSpan<double> sy;
    CAbiSpan<double> sz;
    CAbiSpan<double> kx;
    CAbiSpan<double> ky;
    CAbiSpan<double> kz;
    CAbiSpan<double> omega; // beta_f linear-exact plane intersection
};

/**
 * @brief Deterministic Atomic-Free CellFaceCSR structure.
 * Maps cells (0 <= cell < owned_cells) to their incident faces and direct sign multiplier.
 * Enables zero-atomic vectorization on GPUs and OpenMP threads.
 */
struct CellFaceCSR {
    CAbiSpan<const int32_t> row_ptr;   // cell_count + 1
    CAbiSpan<const int32_t> face_idx;  // face references for cell
    CAbiSpan<const double>  face_sign; // +1.0 if cell is owner, -1.0 if cell is neighbor
};

} // namespace hpc::fvm
