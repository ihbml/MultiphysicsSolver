#include <cuda_runtime.h>
#include "face_layout.hpp"
using namespace continuum;

// Launch on ALL four face ranges, not only the fast ranges.
// Central convection is not bounded at high cell Peclet numbers.
template<bool Upwind>
__global__ void base_coefficients(
    Range range, FaceBaseSoA faces,
    const double* __restrict__ mdot,
    const double* __restrict__ gamma,
    CoefficientsSoA a)
{
    const Index f = range.begin + blockIdx.x*blockDim.x + threadIdx.x;
    if (f >= range.end) return;

    const double m = mdot[f];
    const double D = gamma[f]*faces.alpha[f];
    double p, n;
    if constexpr (Upwind) {
        p = fmax(m, 0.0) + D;
        n = fmin(m, 0.0) - D;
    } else {
        p = m*(1.0 - faces.beta[f]) + D;
        n = m*faces.beta[f] - D;
    }

    // One face owns its four output entries: no scatter atomics.
    a.pp[f] = p;  a.pn[f] = n;
    a.np[f] = -p; a.nn[f] = -n;
}

template __global__ void base_coefficients<true>(
    Range, FaceBaseSoA, const double*, const double*, CoefficientsSoA);
template __global__ void base_coefficients<false>(
    Range, FaceBaseSoA, const double*, const double*, CoefficientsSoA);