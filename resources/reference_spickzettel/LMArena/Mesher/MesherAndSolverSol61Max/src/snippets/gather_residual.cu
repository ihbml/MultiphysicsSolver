#include <cuda_runtime.h>
#include "face_layout.hpp"
using namespace continuum;

// One thread per owned cell, stable incident-face ordering.
// face_flux contains one consistently oriented flux per face.
__global__ void gather_residual(
    Index owned_cells, CellFaceCSR adjacency,
    const double* __restrict__ face_flux,
    double* __restrict__ residual)
{
    const Index cell = blockIdx.x*blockDim.x + threadIdx.x;
    if (cell >= owned_cells) return;
    double sum = 0.0;
    for (std::int64_t k = adjacency.offsets[cell];
         k < adjacency.offsets[cell+1]; ++k) {
        sum += adjacency.orientation[k]*face_flux[adjacency.faces[k]];
    }
    residual[cell] += sum;
}

// Do not run another writer on residual concurrently.
// Across ranks: assign one owner to each shared face and exchange
// its flux, or prove that duplicate evaluations agree consistently.