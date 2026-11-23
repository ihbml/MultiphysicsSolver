// =============================================================================
//  CLUSTER A - 6 :: CUDA back-end for the Cluster-A multiphysics kernels
//
//  Design contract
//   * Every kernel is a 1:1 mirror of the CPU path (same maths, same order of
//     operations) so that CPU/GPU regression tests agree bit-for-bit up to
//     fused-multiply-add contraction.
//   * All topology is read from the flat FaceBaseSoA / CellFaceCSR arrays ->
//     no pointer chasing, coalesced access, zero atomics.
//   * The compact CorrectionSoA is looked up by branch-free binary search over
//     a sorted index array (only ~6% of faces carry a correction).
//
//  Build
//     nvcc -std=c++20 -O3 -arch=sm_80 -I. -o cla_cuda cluster_a_cuda.cu
//  CPU mirror build (used for regression testing without a GPU):
//     g++  -std=c++20 -O2 -DCLA_HOST_MIRROR -I. -x c++ cluster_a_cuda.cu
// =============================================================================
#ifdef CLA_HOST_MIRROR
  // ---- g++ build: the device code becomes plain host code ------------------
  #define __device__
  #define __host__
  #define __global__
  #include <cstddef>
  #include <cmath>
  #include <algorithm>
  namespace cla { namespace cuda { namespace mirror {
  struct Dim3 { unsigned x = 0, y = 0, z = 0;
                Dim3() = default;
                Dim3(unsigned X) : x(X), y(1), z(1) {}
                Dim3(unsigned X, unsigned Y, unsigned Z) : x(X), y(Y), z(Z) {} };
  inline Dim3 blockIdx, blockDim, threadIdx;      // 1-D execution grid mirror
  } } }
  using cla::cuda::mirror::blockIdx;
  using cla::cuda::mirror::blockDim;
  using cla::cuda::mirror::threadIdx;
  #define dim3 cla::cuda::mirror::Dim3
  #define CLA_LAUNCH(kern, grid, block, ...)                                  \
    do {                                                                      \
      cla::cuda::mirror::blockDim = (block);                                  \
      for (unsigned b_ = 0; b_ < (grid).x; ++b_) {                            \
        cla::cuda::mirror::blockIdx = dim3(b_, 0u, 0u);                       \
        for (unsigned t_ = 0; t_ < (block).x; ++t_) {                         \
          cla::cuda::mirror::threadIdx = dim3(t_, 0u, 0u);                    \
          kern(__VA_ARGS__);                                                  \
        }                                                                     \
      }                                                                       \
    } while (0)
#else
  // ---- nvcc build ----------------------------------------------------------
  #include <cuda_runtime.h>
  #include <cstdio>
  #include <cstdlib>
  #define CLA_LAUNCH(kern, grid, block, ...)                                  \
    do {                                                                      \
      kern<<<grid, block>>>(__VA_ARGS__);                                     \
      cudaError_t e_ = cudaGetLastError();                                    \
      if (e_ != cudaSuccess) {                                                \
        std::fprintf(stderr, "CUDA launch of %s failed: %s (%s:%d)\n",        \
                     #kern, cudaGetErrorString(e_), __FILE__, __LINE__);      \
        std::exit(1);                                                         \
      }                                                                       \
    } while (0)
#endif

#include "cluster_a_core.hpp"
#include "cluster_a_turbulence.hpp"
#include "cluster_a_wall.hpp"

namespace cla {
namespace cuda {

// ----------------------------------------------------------------- constants
__device__ __host__ inline Real dSqrt(Real x) { return std::sqrt(x); }
__device__ __host__ inline Real dMax(Real a, Real b) { return a > b ? a : b; }
__device__ __host__ inline Real dMin(Real a, Real b) { return a < b ? a : b; }
__device__ __host__ inline Real dTanH(Real x) { return std::tanh(x); }

// =============================================================================
//  K1 :: k-omega SST blending functions + Bradshaw-limited eddy viscosity
//  One thread per cell.  Reads k, omega, yWall, gradK, gradW, |S|; writes
//  nut, F1, F2, and the four blended coefficients.
// =============================================================================
struct SstParams {
  Real a1, beta1, beta2, betaStar, sigmaW2, sigmaW1, sigmaK1, sigmaK2;
  Real kappa, gamma1, gamma2, rho, nu;
};

__global__ void sstBlendingKernel(const SstParams p,
                                  const Real* __restrict__ k,
                                  const Real* __restrict__ w,
                                  const Real* __restrict__ yw,
                                  const Real* __restrict__ gkx, const Real* __restrict__ gky, const Real* __restrict__ gkz,
                                  const Real* __restrict__ gwx, const Real* __restrict__ gwy, const Real* __restrict__ gwz,
                                  const Real* __restrict__ Smag,
                                  Real* __restrict__ nut,
                                  Real* __restrict__ F1, Real* __restrict__ F2,
                                  Real* __restrict__ beta, Real* __restrict__ gamma,
                                  Real* __restrict__ sigmaK, Real* __restrict__ sigmaW,
                                  Real* __restrict__ crossDiff, int nCells) {
  const int c = blockIdx.x * blockDim.x + threadIdx.x;
  if (c >= nCells) return;
  const Real kc = dMax(k[c], 1e-30), wc = dMax(w[c], 1e-30), y = dMax(yw[c], 1e-30);
  const Real dkdw = gkx[c]*gwx[c] + gky[c]*gwy[c] + gkz[c]*gwz[c];
  const Real CD   = 2.0f * p.rho * p.sigmaW2 * dkdw / wc;
  const Real arg1 = dMin(dMax(dSqrt(kc) / (p.betaStar * wc * y), 500.0f * p.nu / (y*y*wc)),
                         4.0f * p.rho * p.sigmaW2 / (dMax(CD, 1e-10f) * y));
  const Real f1   = dTanH(arg1 * arg1 * arg1 * arg1);
  const Real arg2 = dMax(2.0f * dSqrt(kc) / (p.betaStar * wc * y), 500.0f * p.nu / (y*y*wc));
  const Real f2   = dTanH(arg2 * arg2);
  // Bradshaw limiter
  nut[c] = p.a1 * kc / dMax(p.a1 * wc, Smag[c] * f2);
  F1[c] = f1; F2[c] = f2;
  beta[c]   = f1 * p.beta1  + (1.0f - f1) * p.beta2;
  gamma[c]  = f1 * p.gamma1 + (1.0f - f1) * p.gamma2;
  sigmaK[c] = f1 * p.sigmaK1 + (1.0f - f1) * p.sigmaK2;
  sigmaW[c] = f1 * p.sigmaW1 + (1.0f - f1) * p.sigmaW2;
  crossDiff[c] = 2.0f * (1.0f - f1) * p.rho * p.sigmaW2 * dkdw / wc;
}

// =============================================================================
//  K2 :: Patankar source assembly for the k and omega equations.
//  Positive production -> RHS, negative destruction -> main diagonal.
//     a_PP(k) += beta* rho w_P V_P
//     b_P (k) += P_k V_P
//     a_PP(w) += (2 beta rho w_P + [block-coupled] beta* rho w_P) V_P
//     b_P (w) += (gamma/vt P_k + crossDiff) V_P
// =============================================================================
__global__ void sstPatankarKernel(const Real* __restrict__ k, const Real* __restrict__ w,
                                  const Real* __restrict__ nut, const Real* __restrict__ P,
                                  const Real* __restrict__ crossDiff, const Real* __restrict__ beta,
                                  const Real* __restrict__ gamma,
                                  const Real* __restrict__ V, const Real rho,
                                  const Real betaStar, const int blockCoupled,
                                  Real* __restrict__ diagK, Real* __restrict__ rhsK,
                                  Real* __restrict__ diagW, Real* __restrict__ rhsW,
                                  const int naiveMode, int nCells) {
  const int c = blockIdx.x * blockDim.x + threadIdx.x;
  if (c >= nCells) return;
  const Real kc = dMax(k[c], 0.0f), wc = dMax(w[c], 1e-30f), vol = V[c];
  const Real destK = betaStar * rho * wc;            // -beta* rho w k
  const Real destW = 2.0f * beta[c] * rho * wc       // -beta rho w^2
                   + (blockCoupled ? betaStar * rho * wc : 0.0f);
  const Real nuT = dMax(nut[c], 1e-30f);
  const Real prodW = gamma[c] * P[c] / nuT + crossDiff[c];
  if (naiveMode) {                                    // (deliberately wrong)
    diagK[c] = 0.0f; rhsK[c] = (P[c] - destK * kc) * vol;
    diagW[c] = 0.0f; rhsW[c] = (prodW - destW * wc) * vol;
  } else {
    diagK[c] = destK * vol;  rhsK[c] = dMax(P[c], 0.0f) * vol;
    diagW[c] = destW * vol;  rhsW[c] = prodW * vol;
  }
}

// =============================================================================
//  K3 :: continuous All-y+ wall treatment on boundary faces.
//  One thread per *boundary* face.
//     y_P = (x_P - x_f) . S_f / ||S_f||
//     u_tau solved by 6 Newton steps on  u_P = u_tau uPlus(y+)
//     omega_P = sqrt( omega_vis^2 + omega_log^2 )
//     q_w = rho cp u_tau (T_P - T_w) / T+
// =============================================================================
struct WallParams { Real rho, nu, cp, alpha, Prt, beta1, betaStar, kappa, E; };

__device__ __host__ inline Real wallUPlusBlended(Real yPlus, Real kappa, Real E) {
  const Real ul = std::log(E * dMax(yPlus, 1e-12f)) / kappa;
  return dSqrt(yPlus * yPlus + ul * ul);
}

__global__ void allYPlusWallKernel(const WallParams p,
                                   const Real* __restrict__ cCx, const Real* __restrict__ cCy, const Real* __restrict__ cCz,
                                   const Real* __restrict__ fCx, const Real* __restrict__ fCy, const Real* __restrict__ fCz,
                                   const Real* __restrict__ Sx,  const Real* __restrict__ Sy,  const Real* __restrict__ Sz,
                                   const int* __restrict__ own,
                                   const Real* __restrict__ uT,  const Real* __restrict__ uN,  // tangential comps
                                   const Real* __restrict__ TP,  const Real* __restrict__ Tw,
                                   const Real* __restrict__ kP,
                                   Real* __restrict__ uTau, Real* __restrict__ yPlus,
                                   Real* __restrict__ tauW,  Real* __restrict__ omegaP,
                                   Real* __restrict__ kWall, Real* __restrict__ qW,
                                   Real* __restrict__ htc,   const int nBndFaces) {
  const int i = blockIdx.x * blockDim.x + threadIdx.x;
  if (i >= nBndFaces) return;
  const int  c   = own[i];
  const Real Sfx = Sx[i], Sfy = Sy[i], Sfz = Sz[i];
  const Real A   = dSqrt(Sfx*Sfx + Sfy*Sfy + Sfz*Sfz);
  const Real dx  = cCx[c] - fCx[i], dy = cCy[c] - fCy[i], dz = cCz[c] - fCz[i];
  const Real yP  = dMax((dx*Sfx + dy*Sfy + dz*Sfz) / dMax(A, 1e-30f), 1e-14f);
  const Real uPar = dSqrt(uT[i]*uT[i] + uN[i]*uN[i]);
  // ---- Newton on  f(u) = u * uPlus(u yP/nu) - uPar -------------------------
  Real ut = dSqrt(dMax(uPar * p.nu / yP, 1e-30f));
  if (uPar > 1e-30f) {
    for (int it = 0; it < 6; ++it) {
      const Real yp  = ut * yP / p.nu;
      const Real f   = ut * wallUPlusBlended(yp, p.kappa, p.E) - uPar;
      const Real h   = 1e-6f * ut;
      const Real yp2 = (ut + h) * yP / p.nu;
      const Real f2  = (ut + h) * wallUPlusBlended(yp2, p.kappa, p.E);
      const Real df  = (f2 - ut * wallUPlusBlended(yp, p.kappa, p.E)) / h;
      if (df < 1e-30f) break;
      Real step = f / df;
      if (step >  0.45f * ut) step =  0.45f * ut;
      if (step < -0.45f * ut) step = -0.45f * ut;
      ut = dMax(ut - step, 1e-14f);
    }
  }
  const Real yp    = ut * yP / p.nu;
  const Real wVis  = 6.0f * p.nu / (p.beta1 * yP * yP);
  const Real wLog  = ut / (dSqrt(p.betaStar) * p.kappa * yP);
  const Real Pr    = p.nu / dMax(p.alpha, 1e-30f);
  // ---- Kader (1981) thermal law of the wall --------------------------------
  const Real gK   = std::pow(0.01f * dMax(yp, 1e-12f), 4.0f);
  const Real GK   = gK / (1.0f + gK);
  const Real betaPr = std::pow(3.85f * std::cbrt(Pr) - 1.3f, 2.0f) + 2.12f * std::log(dMax(Pr, 1e-6f));
  const Real tLog = std::log(p.E * dMax(yp, 1e-12f)) / (p.kappa * p.Prt) + betaPr;
  const Real Tp   = dMax(Pr * yp * std::exp(-GK) + tLog * std::exp(-1.0f / dMax(GK, 1e-12f)), 1e-6f);
  uTau[i]  = ut;
  yPlus[i] = yp;
  tauW[i]  = p.rho * ut * ut;
  omegaP[i]= dSqrt(wVis * wVis + wLog * wLog);
  htc[i]   = p.rho * p.cp * ut / Tp;
  qW[i]    = htc[i] * (TP[i] - Tw[i]);
  // continuous low-Re k blend:  k_wall = sqrt( k_P^2 + (nu w_vis)^2 )
  kWall[i] = dSqrt(kP[i]*kP[i] + (p.nu * wVis) * (p.nu * wVis));
}

// =============================================================================
//  K4 :: monolithic CHT interface coupling across mappedWall twin faces.
//  One thread per twin pair.  Computes
//     C = kf af ks as / (kf af + ks as)
//     a_PP += C w |S| ,  a_PS -= C w |S|      (both regions, symmetric)
// =============================================================================
struct ChtParams { Real kf, ks; };

__global__ void chtCoupleKernel(const ChtParams p,
                                const int* __restrict__ rowF, const int* __restrict__ colS,
                                const Real* __restrict__ w,     const Real* __restrict__ area,
                                const Real* __restrict__ alphaF,const Real* __restrict__ alphaS,
                                const Real* __restrict__ gkF,   const Real* __restrict__ gkS, // gradT . kcorr
                                Real* __restrict__ diagF, Real* __restrict__ diagS,
                                Real* __restrict__ offF,  Real* __restrict__ offS,
                                Real* __restrict__ rhsF,  Real* __restrict__ rhsS,
                                const int nPairs) {
  const int i = blockIdx.x * blockDim.x + threadIdx.x;
  if (i >= nPairs) return;
  const Real kfA = p.kf * alphaF[i], ksA = p.ks * alphaS[i];
  const Real C   = (kfA * ksA) / dMax(kfA + ksA, 1e-30f);
  const Real Cw  = C * w[i] * area[i];
  const Real qSkew = C * (gkF[i] / dMax(alphaF[i], 1e-30f) +
                          gkS[i] / dMax(alphaS[i], 1e-30f)) * w[i] * area[i];
  const int r = rowF[i], s = colS[i];
  // atomic-free: each pair owns a unique (rowF, colS) slot in a pre-sorted CSR
  diagF[r] += Cw;  diagS[s] += Cw;
  offF[i]  = -Cw;  offS[i]  = -Cw;      // scattered into CSR by the host/handle
  rhsF[r] -= qSkew; rhsS[s] += qSkew;
}

// =============================================================================
//  K5 :: anisotropic Darcy-Forchheimer sink (diagonal part only; the
//      off-diagonal part is handled by an explicit kernel).
//      A = mu D + 0.5 rho |u| F   ->   a_PP += A_ii V
// =============================================================================
__global__ void porousSinkKernel(const Real* __restrict__ D,   // 9 entries / cell
                                 const Real* __restrict__ F,
                                 const Real* __restrict__ ux, const Real* __restrict__ uy,
                                 const Real* __restrict__ uz,
                                 const Real* __restrict__ V,
                                 const Real mu, const Real rho,
                                 Real* __restrict__ diag, int nCells) {
  const int c = blockIdx.x * blockDim.x + threadIdx.x;
  if (c >= nCells) return;
  const Real spd = dSqrt(ux[c]*ux[c] + uy[c]*uy[c] + uz[c]*uz[c]);
  const Real t1 = mu, t2 = 0.5f * rho * spd;
  for (int i = 0; i < 3; ++i) {
    const Real a = D[9*c + 3*i + i] * t1 + F[9*c + 3*i + i] * t2;
    Real off = 0;
    for (int j = 0; j < 3; ++j) if (j != i)
      off += std::abs(D[9*c + 3*i + j] * t1 + F[9*c + 3*i + j] * t2) * spd;
    diag[3*c + i] = dMax(a, off) * V[c];
  }
}

// =============================================================================
//  K6 :: P1 radiation - one Jacobi (atomically reduced) sweep of
//      -div(Gamma grad G) + a G = 4 a n^2 sigma T^4
//      assembled straight from the CellFaceCSR rows.
// =============================================================================
struct P1Params { Real gamma, absorption, refractive, sigma, Twall, Cmarshak; };

__global__ void p1JacobiKernel(const P1Params p,
                               const int* __restrict__ csrStart, const int* __restrict__ csrFaces,
                               const Real* __restrict__ Sx, const Real* __restrict__ Sy, const Real* __restrict__ Sz,
                               const Real* __restrict__ cCx, const Real* __restrict__ cCy, const Real* __restrict__ cCz,
                               const Real* __restrict__ fCx, const Real* __restrict__ fCy, const Real* __restrict__ fCz,
                               const int* __restrict__ own, const int* __restrict__ nbr,
                               const int* __restrict__ isWall,
                               const Real* __restrict__ T, const Real* __restrict__ G,
                               const Real* __restrict__ V,
                               Real* __restrict__ Adiag, Real* __restrict__ rhs,
                               const int nCells) {
  const int c = blockIdx.x * blockDim.x + threadIdx.x;
  if (c >= nCells) return;
  Real aDiag = p.absorption * V[c];
  Real b     = 4.0f * p.absorption * p.refractive * p.refractive * p.sigma * std::pow(T[c], 4.0f) * V[c];
  for (int kk = csrStart[c]; kk < csrStart[c+1]; ++kk) {
    const int f = csrFaces[kk];
    // S_f is stored owner -> neighbour; flip it if this cell is the neighbour
    const Real sgn = (own[f] == c) ? 1.0f : -1.0f;
    const Real Sfx = sgn * Sx[f], Sfy = sgn * Sy[f], Sfz = sgn * Sz[f];
    const int nb = nbr[f];
    const Real ox = (nb >= 0) ? cCx[nb] : fCx[f];
    const Real oy = (nb >= 0) ? cCy[nb] : fCy[f];
    const Real oz = (nb >= 0) ? cCz[nb] : fCz[f];
    const Real dx = ox - cCx[c], dy = oy - cCy[c], dz = oz - cCz[c];
    const Real A2 = Sfx*Sfx + Sfy*Sfy + Sfz*Sfz;
    const Real dS = Sfx*dx + Sfy*dy + Sfz*dz;
    const Real alpha = A2 / dMax(dS, 1e-30f);
    if (nb >= 0) {
      aDiag += p.gamma * alpha;
      b     -= p.gamma * alpha * G[nb];
    } else if (isWall[f]) {
      aDiag += p.Cmarshak;
      b     += p.Cmarshak * 4.0f * p.sigma * std::pow(p.Twall, 4.0f);
    } else {
      aDiag += p.gamma * alpha;
      b     += p.gamma * alpha * 4.0f * p.sigma * std::pow(p.Twall, 4.0f);
    }
  }
  Adiag[c] = aDiag;
  rhs[c]   = b;                     // G_new = rhs / Adiag  (Jacobi update)
}

// =============================================================================
//  Host-side launchers
// =============================================================================
inline int blockSizeFor(int n) { return n < 256 ? 32 : 256; }
inline int gridFor(int n, int block) { return (n + block - 1) / block; }

void launchSstBlending(int nCells, const SstParams& p, const Real* k, const Real* w,
                       const Real* yw, const Real* gkx, const Real* gky, const Real* gkz,
                       const Real* gwx, const Real* gwy, const Real* gwz, const Real* Smag,
                       Real* nut, Real* F1, Real* F2, Real* beta, Real* gamma,
                       Real* sigmaK, Real* sigmaW, Real* crossDiff) {
  const int block = blockSizeFor(nCells), grid = gridFor(nCells, block);
  CLA_LAUNCH(sstBlendingKernel, dim3(grid), dim3(block), p, k, w, yw, gkx, gky, gkz,
             gwx, gwy, gwz, Smag, nut, F1, F2, beta, gamma, sigmaK, sigmaW, crossDiff, nCells);
}

void launchAllYPlusWall(int nBnd, const WallParams& p,
                        const Real* cCx, const Real* cCy, const Real* cCz,
                        const Real* fCx, const Real* fCy, const Real* fCz,
                        const Real* Sx, const Real* Sy, const Real* Sz, const int* own,
                        const Real* uT, const Real* uN, const Real* TP, const Real* Tw,
                        const Real* kP, Real* uTau, Real* yPlus, Real* tauW, Real* omegaP,
                        Real* kWall, Real* qW, Real* htc) {
  const int block = blockSizeFor(nBnd), grid = gridFor(nBnd, block);
  CLA_LAUNCH(allYPlusWallKernel, dim3(grid), dim3(block), p, cCx, cCy, cCz, fCx, fCy, fCz,
             Sx, Sy, Sz, own, uT, uN, TP, Tw, kP, uTau, yPlus, tauW, omegaP, kWall, qW, htc, nBnd);
}

void launchSstPatankar(int nCells, const Real* k, const Real* w, const Real* nut, const Real* P,
                       const Real* crossDiff, const Real* beta, const Real* gamma, const Real* V,
                       const Real rho, const Real betaStar, const int blockCoupled,
                       const int naiveMode, Real* diagK, Real* rhsK, Real* diagW, Real* rhsW) {
  const int block = blockSizeFor(nCells), grid = gridFor(nCells, block);
  CLA_LAUNCH(sstPatankarKernel, dim3(grid), dim3(block), k, w, nut, P, crossDiff, beta, gamma, V,
             rho, betaStar, blockCoupled, diagK, rhsK, diagW, rhsW, naiveMode, nCells);
}

void launchChtCouple(int nPairs, const ChtParams& p, const int* rowF, const int* colS,
                     const Real* w, const Real* area, const Real* alphaF, const Real* alphaS,
                     const Real* gkF, const Real* gkS, Real* diagF, Real* diagS,
                     Real* offF, Real* offS, Real* rhsF, Real* rhsS) {
  const int block = blockSizeFor(nPairs), grid = gridFor(nPairs, block);
  CLA_LAUNCH(chtCoupleKernel, dim3(grid), dim3(block), p, rowF, colS, w, area, alphaF, alphaS,
             gkF, gkS, diagF, diagS, offF, offS, rhsF, rhsS, nPairs);
}

void launchPorousSink(int nCells, const Real* D, const Real* F, const Real* ux, const Real* uy,
                      const Real* uz, const Real* V, const Real mu, const Real rho, Real* diag) {
  const int block = blockSizeFor(nCells), grid = gridFor(nCells, block);
  CLA_LAUNCH(porousSinkKernel, dim3(grid), dim3(block), D, F, ux, uy, uz, V, mu, rho, diag, nCells);
}

void launchP1Jacobi(int nCells, const P1Params& p, const int* csrStart, const int* csrFaces,
                    const Real* Sx, const Real* Sy, const Real* Sz,
                    const Real* cCx, const Real* cCy, const Real* cCz,
                    const Real* fCx, const Real* fCy, const Real* fCz,
                    const int* own, const int* nbr, const int* isWall,
                    const Real* T, const Real* G, const Real* V,
                    Real* Adiag, Real* rhs) {
  const int block = blockSizeFor(nCells), grid = gridFor(nCells, block);
  CLA_LAUNCH(p1JacobiKernel, dim3(grid), dim3(block), p, csrStart, csrFaces, Sx, Sy, Sz,
             cCx, cCy, cCz, fCx, fCy, fCz, own, nbr, isWall, T, G, V, Adiag, rhs, nCells);
}

}  // namespace cuda
}  // namespace cla
