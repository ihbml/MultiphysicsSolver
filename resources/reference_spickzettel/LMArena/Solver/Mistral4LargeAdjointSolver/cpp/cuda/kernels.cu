// ============================================================================
//  cuda/kernels.cu
//  CUDA kernels for the zero-copy SoA solver. Compile with:
//    nvcc -std=c++20 -O3 -arch=sm_80 --expt-relaxed-constexpr -c kernels.cu
//  The CellFaceCSR gather kernels are ATOMIC-FREE by construction: one thread
//  per owned cell writes only its own residual slot; face fluxes are computed
//  once per face (one thread per face) and read by both adjacent cells.
//  Only the chemistry compaction queue uses an atomic ticket (work stealing).
// ============================================================================
#include <cstdint>
#include <cstddef>
#include "../include/fvm/core.hpp"

namespace fvm_cuda {

using fvm::CAbiSpan;

// ---- deterministic atomic-free CellFaceCSR continuity residual gather -----
// one thread per owned cell; face list grouped by quadrant seg 0..3
__global__ void csr_continuity_kernel(const CAbiSpan L, const double* mdot, double* D) {
  const int c = blockIdx.x * blockDim.x + threadIdx.x;
  if (c >= L.n_owned) return;
  double acc = 0.0;
  for (int k = L.cf_ptr[c]; k < L.cf_ptr[c + 1]; ++k) {
    const int f = L.cf_list[k];
    acc += (L.owner[f] == c ? 1.0 : -1.0) * mdot[f];
  }
  D[c] = acc;  // single writer per cell: no atomics needed
}

// ---- Rhie-Chow face mass flux: one thread per face -------------------------
// m_f = rho_f [ ubar_f.S_f - Dbar_f alpha_f (p_N - p_P - gbar_f.d_f) ]
__global__ void rhie_chow_flux_kernel(const CAbiSpan L, const double* p, const double* gpx,
                                      const double* gpy, const double* gpz,
                                      const double* Dbar, double* mdot) {
  const int f = blockIdx.x * blockDim.x + threadIdx.x;
  if (f >= L.n_faces) return;
  const int P = L.owner[f], N = L.neighbour[f];
  const double sx = L.sx[f], sy = L.sy[f], sz = L.sz[f];
  const double S2 = sx * sx + sy * sy + sz * sz;
  // d_f = x_N - x_P (mirrored ghost on boundary)
  double dx = (N >= 0) ? L.cx[N] - L.cx[P] : 2.0 * (L.fx[f] - L.cx[P]);
  double dy = (N >= 0) ? L.cy[N] - L.cy[P] : 2.0 * (L.fy[f] - L.cy[P]);
  double dz = (N >= 0) ? L.cz[N] - L.cz[P] : 2.0 * (L.fz[f] - L.cz[P]);
  const double dS = dx * sx + dy * sy + dz * sz;
  const double alpha = S2 / dS;                                   // over-relaxed conductance
  const double th = ((L.fx[f] - L.cx[P]) * dx + (L.fy[f] - L.cy[P]) * dy + (L.fz[f] - L.cz[P]) * dz)
                    / (dx * dx + dy * dy + dz * dz);                 // linear-exact interp weight
  const double gbx = gpx[P] * (1.0 - th) + (N >= 0 ? gpx[N] : gpx[P]) * th;
  const double gby = gpy[P] * (1.0 - th) + (N >= 0 ? gpy[N] : gpy[P]) * th;
  const double gbz = gpz[P] * (1.0 - th) + (N >= 0 ? gpz[N] : gpz[P]) * th;
  const double pN = (N >= 0) ? p[N] : p[P];
  const double gbarS = gbx * sx + gby * sy + gbz * sz;
  const double Gf = gbarS + alpha * (pN - p[P] - (gbx * dx + gby * dy + gbz * dz));
  const double ubarS = (L.u[P] * (1.0 - th) + (N >= 0 ? L.u[N] : L.u[P]) * th) * sx
                     + (L.v[P] * (1.0 - th) + (N >= 0 ? L.v[N] : L.v[P]) * th) * sy
                     + (L.w[P] * (1.0 - th) + (N >= 0 ? L.w[N] : L.w[P]) * th) * sz;
  const double rho_f = (N >= 0) ? 0.5 * (L.rho[P] + L.rho[N]) : L.rho[P];
  mdot[f] = rho_f * (ubarS - Dbar[f] * (Gf - gbarS));
}

// ---- L1-Jacobi AMG smoother on device (fully parallel, GPU-friendly) ------
__global__ void l1_jacobi_kernel(const int n, const int32_t* ptr, const int32_t* col,
                                 const double* val, const double* l1, const double* b,
                                 double* x, double omega) {
  const int i = blockIdx.x * blockDim.x + threadIdx.x;
  if (i >= n) return;
  double acc = 0.0;
  for (int k = ptr[i]; k < ptr[i + 1]; ++k) acc += val[k] * x[col[k]];
  x[i] += omega * (b[i] - acc) / l1[i];
}

// ---- Chebyshev polynomial smoother (degree q) of D^{-1} r -----------------
__global__ void chebyshev_kernel(const int n, const int32_t* ptr, const int32_t* col,
                                 const double* val, const double* diag, const double* b,
                                 double* x, double* p, double* z, double lmax, int q) {
  const int i = blockIdx.x * blockDim.x + threadIdx.x;
  if (i >= n) return;
  const double d = lmax * 0.5, c = lmax * 0.5;
  double sigma = d / c;
  // r = D^{-1}(b - A x); p = r / sigma; x += p
  double acc = 0.0;
  for (int k = ptr[i]; k < ptr[i + 1]; ++k) acc += val[k] * x[col[k]];
  double r = (b[i] - acc) / diag[i];
  p[i] = r / sigma;
  x[i] += p[i];
  for (int k = 1; k < q; ++k) {
    __syncthreads();  // p is read after full update (ping-pong buffers in caller)
    acc = 0.0;
    for (int j = ptr[i]; j < ptr[i + 1]; ++j) acc += val[j] * p[col[j]];
    z[i] = (b[i] - acc) / diag[i];
    double sigma1 = 1.0 / (2.0 * (d / c) - sigma);
    double pn = 2.0 * sigma1 * z[i] - sigma * p[i];
    x[i] += pn;
    p[i] = pn;
    sigma = sigma1;
  }
}

// ---- batched per-cell stiff chemistry with dynamic work stealing ----------
// Compacted active queue + atomic ticket (DLBFoam-style stealing of stiff
// flame/catalysis cells by idle warps). Each claimed cell is integrated with
// the L-stable 2-stage Rosenbrock-W (ROS2) scheme using the AoT analytical
// Jacobian of the mechanism. Mechanism layout (device arrays, ns <= 8):
//   nuR/nruP [nr][ns] int stoichiometry, kArr[nr] Arrhenius rates at cell T.
__device__ inline void mech_omega(const double* y, const int* nuR, const int* nuP,
                                   const double* kArr, int ns, int nr, double* w) {
  for (int k = 0; k < ns; ++k) w[k] = 0.0;
  for (int r = 0; r < nr; ++r) {
    double rate = kArr[r];
    for (int k = 0; k < ns; ++k)
      if (nuR[r * ns + k] > 0) {
        double p = 1.0;
        for (int e = 0; e < nuR[r * ns + k]; ++e) p *= fmax(y[k], 0.0);
        rate *= p;
      }
    for (int k = 0; k < ns; ++k) w[k] += (double)(nuP[r * ns + k] - nuR[r * ns + k]) * rate;
  }
}
__device__ inline void mech_jac(const double* y, const int* nuR, const int* nuP,
                                const double* kArr, int ns, int nr, double* J) {
  for (int i = 0; i < ns * ns; ++i) J[i] = 0.0;
  for (int r = 0; r < nr; ++r) {
    double rate = kArr[r];
    for (int k = 0; k < ns; ++k)
      if (nuR[r * ns + k] > 0) {
        double p = 1.0;
        for (int e = 0; e < nuR[r * ns + k]; ++e) p *= fmax(y[k], 0.0);
        rate *= p;
      }
    for (int j = 0; j < ns; ++j) {
      if (nuR[r * ns + j] <= 0) continue;
      const double drdYj = (y[j] > 1e-300) ? nuR[r * ns + j] * rate / y[j] : 0.0;
      for (int k = 0; k < ns; ++k) {
        const int nu = nuP[r * ns + k] - nuR[r * ns + k];
        if (nu != 0) J[k * ns + j] += nu * drdYj;
      }
    }
  }
}
__device__ inline bool solve3(double* A, double* b, int n, double* x) {  // Gaussian elimination
  for (int k = 0; k < n; ++k) {
    int piv = k;
    for (int i = k + 1; i < n; ++i) if (fabs(A[i * n + k]) > fabs(A[piv * n + k])) piv = i;
    if (fabs(A[piv * n + k]) < 1e-300) return false;
    if (piv != k) {
      for (int j = 0; j < n; ++j) { double t = A[piv * n + j]; A[piv * n + j] = A[k * n + j]; A[k * n + j] = t; }
      double t = b[piv]; b[piv] = b[k]; b[k] = t;
    }
    for (int i = k + 1; i < n; ++i) {
      double m = A[i * n + k] / A[k * n + k];
      for (int j = k; j < n; ++j) A[i * n + j] -= m * A[k * n + j];
      b[i] -= m * b[k];
    }
  }
  for (int i = n - 1; i >= 0; --i) {
    double s = b[i];
    for (int j = i + 1; j < n; ++j) s -= A[i * n + j] * x[j];
    x[i] = s / A[i * n + i];
  }
  return true;
}
__global__ void chem_batched_kernel(const int* queue, const int n_active, int* ticket,
                                    const double* Y0, double* Y, int ns, int nr,
                                    const int* nuR, const int* nuP, const double* kArr,
                                    double h, int nsteps) {
  constexpr double gamma = 1.0 - 1.0 / 1.4142135623730951;
  constexpr double b1 = 1.2071067811865475, b2 = -0.20710678118654752;
  while (true) {
    const int t = atomicAdd(ticket, 1);          // DLBFoam-style work stealing
    if (t >= n_active) return;
    const int cell = queue[t];
    double y[8], yn[8], K1[8], K2[8], f[8], J[64], M[64], rhs[8], y1[8];
    for (int k = 0; k < ns; ++k) y[k] = yn[k] = Y0[cell * ns + k];
    for (int s = 0; s < nsteps; ++s) {
      mech_omega(yn, nuR, nuP, kArr, ns, nr, f);
      mech_jac(yn, nuR, nuP, kArr, ns, nr, J);
      for (int i = 0; i < ns; ++i)
        for (int j = 0; j < ns; ++j) M[i * ns + j] = (i == j ? 1.0 : 0.0) - gamma * h * J[i * ns + j];
      for (int i = 0; i < ns; ++i) rhs[i] = f[i];
      if (!solve3(M, rhs, ns, K1)) return;
      for (int i = 0; i < ns; ++i) y1[i] = yn[i] + h * K1[i];
      mech_omega(y1, nuR, nuP, kArr, ns, nr, f);
      for (int i = 0; i < ns; ++i) {
        double jk = 0;
        for (int j = 0; j < ns; ++j) jk += J[i * ns + j] * K1[j];
        rhs[i] = f[i] - gamma * h * jk;
      }
      if (!solve3(M, rhs, ns, K2)) return;
      for (int i = 0; i < ns; ++i) y[i] = yn[i] + h * (b1 * K1[i] + b2 * K2[i]);
      for (int k = 0; k < ns; ++k) yn[k] = y[k];
    }
    for (int k = 0; k < ns; ++k) Y[cell * ns + k] = y[k];
  }
}

// ---- host launchers ---------------------------------------------------------
inline void launch_continuity(const CAbiSpan& L, const double* mdot, double* D, cudaStream_t s = 0) {
  const int block = 256;
  const int grid = (L.n_owned + block - 1) / block;
  csr_continuity_kernel<<<grid, block, 0, s>>>(L, mdot, D);
}
inline void launch_rhie_chow(const CAbiSpan& L, const double* p, const double* gpx,
                             const double* gpy, const double* gpz, const double* Dbar,
                             double* mdot, cudaStream_t s = 0) {
  const int block = 256;
  const int grid = (L.n_faces + block - 1) / block;
  rhie_chow_flux_kernel<<<grid, block, 0, s>>>(L, p, gpx, gpy, gpz, Dbar, mdot);
}

}  // namespace fvm_cuda
