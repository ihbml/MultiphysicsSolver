// =============================================================================
// cluster_a_kernels.cu -- CUDA batch kernels for Cluster A
//
// The same MP_HD device functions used on the host (mp_turbulence_cht.hpp) are
// launched over the face / cell SoA arrays of the FVM engine. One thread per
// element, coalesced SoA access, no atomics on the per-element update path.
//
// Build (requires CUDA toolkit; NOT compiled in the CPU verification sandbox):
//   nvcc -std=c++20 -O3 -arch=sm_80 --expt-relaxed-constexpr \
//        -I../include cluster_a_kernels.cu -c
// =============================================================================
#include "mp_turbulence_cht.hpp"

namespace mp::gpu {

// Per-boundary-face All-y+ wall update: friction velocity, wall shear, wall
// heat flux and blended omega_P. Faces are independent -> embarrassingly parallel.
__global__ void wallFunctionKernel(int nFaces, const real* __restrict__ yP,
                                   const real* __restrict__ nu, const real* __restrict__ rho,
                                   const real* __restrict__ cp, const real* __restrict__ Up,
                                   const real* __restrict__ dT, real* __restrict__ tauW,
                                   real* __restrict__ qW, real* __restrict__ omegaP,
                                   real* __restrict__ uTau, real Pr) {
  const int f = blockIdx.x * blockDim.x + threadIdx.x;
  if (f >= nFaces) return;
  const real y = fmaxr(yP[f], 1e-300);
  const real ut = friction_velocity(Up[f], y, nu[f]);
  uTau[f] = ut;
  tauW[f] = wall_shear(rho[f], ut);
  qW[f] = wall_heat_flux(rho[f], cp[f], ut, dT[f], ut * y / nu[f], Pr);
  omegaP[f] = all_yplus_omega(y, nu[f], ut);
}

// Per-cell WALE sub-grid viscosity from the velocity-gradient SoA (9 comps).
__global__ void waleKernel(int nCells, const real* __restrict__ gradU /* [9][nCells] */,
                           const real* __restrict__ volume, real* __restrict__ nuSgs) {
  const int c = blockIdx.x * blockDim.x + threadIdx.x;
  if (c >= nCells) return;
  Mat3 g;
  for (int i = 0; i < 3; ++i)
    for (int j = 0; j < 3; ++j) g[i][j] = gradU[(3 * i + j) * nCells + c];
  nuSgs[c] = wale_nu_sgs(g, les_delta_from_volume(volume[c]));
}

// Per-cell positive SST k/omega update (Patankar-linearised, unconditionally positive).
__global__ void sstUpdateKernel(int nCells, real* __restrict__ k, real* __restrict__ omega,
                                const real* __restrict__ S2, const real* __restrict__ rho,
                                const real* __restrict__ volume, const real* __restrict__ F1,
                                const real* __restrict__ gradKdotGradW,
                                const real* __restrict__ wallDist, const real* __restrict__ nu,
                                real dt) {
  const int c = blockIdx.x * blockDim.x + threadIdx.x;
  if (c >= nCells) return;
  const real F1c = F1[c];
  const real beta = sst_beta(F1c);
  const real gamma = sst_gamma(F1c);
  const real F2c = sst_F2(k[c], omega[c], nu[c], wallDist[c]);
  const real nut = sst_nut(k[c], omega[c], std::sqrt(S2[c]), F2c);
  const real Pk = sst_production(rho[c], nut, S2[c], k[c], omega[c]);
  const real kNew = sst_update_k(k[c], omega[c], rho[c], volume[c], dt, Pk);
  const real wNew = sst_update_omega(omega[c], rho[c], volume[c], dt, beta, gamma, S2[c], F1c,
                                     gradKdotGradW[c]);
  k[c] = kNew;
  omega[c] = wNew;
}

}  // namespace mp::gpu
