#include <cuda_runtime.h>
#include "face_layout.hpp"
using namespace continuum;

// Pair this geometric skew correction with CENTRAL convection.
// High-order upwind reconstruction needs its own deferred correction.
__global__ void correction_flux(
    Range subset, FaceBaseSoA base, CorrectionSoA corr,
    const double* __restrict__ mdot,
    const double* __restrict__ gamma,
    GradientSoA grad, double* __restrict__ face_correction)
{
    const Index c = subset.begin + blockIdx.x*blockDim.x + threadIdx.x;
    if (c >= subset.end) return;
    const Index f = corr.base_face[c];
    const Index p = base.owner[f], n = base.neighbour[f];
    const double b = base.beta[f];
    const double gx = (1-b)*grad.x[p] + b*grad.x[n];
    const double gy = (1-b)*grad.y[p] + b*grad.y[n];
    const double gz = (1-b)*grad.z[p] + b*grad.z[n];
    const double skew = gx*corr.sx[c] + gy*corr.sy[c] + gz*corr.sz[c];
    const double nonortho = gx*corr.kx[c] + gy*corr.ky[c] + gz*corr.kz[c];

    // Zero the complete output before launching this subset kernel.
    // omega = 1 preserves the full linear geometric correction.
    face_correction[f] = corr.omega[c]*(mdot[f]*skew - gamma[f]*nonortho);
}