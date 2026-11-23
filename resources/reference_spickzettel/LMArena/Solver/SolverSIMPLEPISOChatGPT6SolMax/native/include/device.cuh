#pragma once

#include <cstdint>
#include <cuda_runtime.h>

namespace continuum::cuda {

// All pointers refer to caller-owned device SoA buffers, including halo fields.
struct DeviceMesh {
  int owned_cells, total_cells, face_count;
  int split[5];
  const int *owner, *neighbor, *offsets, *faces;
  const int8_t* signs;
  const uint8_t* boundary;
  const double *cx, *cy, *cz, *volume, *fx, *fy, *fz, *sx, *sy, *sz;
  const double *pressure_bc, *mass_bc, *enthalpy_bc;
  const double *skewx, *skewy, *skewz, *kx, *ky, *kz, *omega; // compact corrected quadrants only
};

struct DeviceFlow {
  const double *ux, *uy, *uz, *pressure, *rho, *d;
  const double *gradx, *grady, *gradz;
  double* face_mass;
};

struct DeviceChemistry {
  double** species; // device array of device pointers, species[k][cell]
  const double *temperature, *density;
  int cells;
  int *active, *failed, *max_substeps; // caller-owned device scalar outputs
};

struct DeviceMomentum {
  const double *oldx, *oldy, *oldz, *iterx, *itery, *iterz;
  const double *forcex, *forcey, *forcez, *gamma, *viscosity;
  double *nextx, *nexty, *nextz, *mobility;
  double dt, relaxation;
};

struct DeviceEnergy {
  const double *old, *iterate, *conductivity, *gradx, *grady, *gradz;
  double *next, *deferred_flux; // face_count scratch, then owned-cell Jacobi output
  double dt;
};

cudaError_t launch_rhie_chow(DeviceMesh mesh, DeviceFlow flow, cudaStream_t stream = nullptr);
cudaError_t launch_csr_divergence(DeviceMesh mesh, const double* mass, double* divergence,
                                  cudaStream_t stream = nullptr);
cudaError_t launch_pressure_operator(DeviceMesh mesh, DeviceFlow flow, const double* x,
                                     double* ax, bool anchored, cudaStream_t stream = nullptr);
cudaError_t launch_pressure_face_correction(DeviceMesh mesh, DeviceFlow flow, const double* pc,
                                            const double* gradx, const double* grady, const double* gradz,
                                            cudaStream_t stream = nullptr);
cudaError_t launch_l1_jacobi(int n, const int* row, const int* col, const float* a,
                             const float* b, const float* x, const float* l1, float* next,
                             float omega, cudaStream_t stream = nullptr);
cudaError_t launch_momentum_sweep(DeviceMesh mesh, DeviceFlow flow, DeviceMomentum fields,
                                  cudaStream_t stream = nullptr);
cudaError_t launch_energy_deferred(DeviceMesh mesh, DeviceFlow flow, DeviceEnergy fields,
                                   cudaStream_t stream = nullptr);
cudaError_t launch_energy_sweep(DeviceMesh mesh, DeviceFlow flow, DeviceEnergy fields,
                                cudaStream_t stream = nullptr);
cudaError_t launch_chain_chemistry(DeviceChemistry batch, double dt, double threshold,
                                   cudaStream_t stream = nullptr);

} // namespace continuum::cuda