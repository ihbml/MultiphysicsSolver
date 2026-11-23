#include "device.cuh"
#include "generated/chain_abc.hpp"
#include <cub/cub.cuh>
#include <cmath>

namespace continuum::cuda {
namespace {

struct FaceGeometry { double dx, dy, dz, sx, sy, sz, beta, alpha; };

__device__ FaceGeometry geometry(DeviceMesh m, int f) {
  const int p = m.owner[f], n = m.neighbor[f];
  const double dx = (n >= 0 ? m.cx[n] : m.fx[f]) - m.cx[p];
  const double dy = (n >= 0 ? m.cy[n] : m.fy[f]) - m.cy[p];
  const double dz = (n >= 0 ? m.cz[n] : m.fz[f]) - m.cz[p];
  const double ds = dx * m.sx[f] + dy * m.sy[f] + dz * m.sz[f];
  const double beta = n < 0 ? 1.0 :
      ((m.fx[f] - m.cx[p]) * m.sx[f] + (m.fy[f] - m.cy[p]) * m.sy[f] +
       (m.fz[f] - m.cz[p]) * m.sz[f]) / ds;
  return {dx, dy, dz, m.sx[f], m.sy[f], m.sz[f], beta,
          (m.sx[f] * m.sx[f] + m.sy[f] * m.sy[f] + m.sz[f] * m.sz[f]) / ds};
}

__device__ double conductance(DeviceMesh m, DeviceFlow flow, int f, FaceGeometry g) {
  const int p = m.owner[f], n = m.neighbor[f];
  const double beta = n < 0 ? 0.0 : g.beta;
  const double rho = flow.rho[p] + (n < 0 ? 0.0 : beta * (flow.rho[n] - flow.rho[p]));
  const double mobility = flow.d[p] + (n < 0 ? 0.0 : beta * (flow.d[n] - flow.d[p]));
  return rho * mobility * g.alpha;
}

__global__ void rhie_chow_kernel(DeviceMesh m, DeviceFlow flow) {
  const int f = blockIdx.x * blockDim.x + threadIdx.x;
  if (f >= m.face_count) return;
  if (m.boundary[f] == 1) { flow.face_mass[f] = m.mass_bc[f]; return; }
  const int p = m.owner[f], n = m.neighbor[f];
  const auto g = geometry(m, f);
  const double beta = n >= 0 ? g.beta : 0.0;
  const double rho = flow.rho[p] + (n >= 0 ? beta * (flow.rho[n] - flow.rho[p]) : 0.0);
  const double d = flow.d[p] + (n >= 0 ? beta * (flow.d[n] - flow.d[p]) : 0.0);
  const double ux = flow.ux[p] + (n >= 0 ? beta * (flow.ux[n] - flow.ux[p]) : 0.0);
  const double uy = flow.uy[p] + (n >= 0 ? beta * (flow.uy[n] - flow.uy[p]) : 0.0);
  const double uz = flow.uz[p] + (n >= 0 ? beta * (flow.uz[n] - flow.uz[p]) : 0.0);
  const double gx = flow.gradx[p] + (n >= 0 ? beta * (flow.gradx[n] - flow.gradx[p]) : 0.0);
  const double gy = flow.grady[p] + (n >= 0 ? beta * (flow.grady[n] - flow.grady[p]) : 0.0);
  const double gz = flow.gradz[p] + (n >= 0 ? beta * (flow.gradz[n] - flow.gradz[p]) : 0.0);
  const double dp = n < 0 ? m.pressure_bc[f] - flow.pressure[p] : flow.pressure[n] - flow.pressure[p];
  flow.face_mass[f] = rho * (ux * g.sx + uy * g.sy + uz * g.sz -
                       d * g.alpha * (dp - gx * g.dx - gy * g.dy - gz * g.dz));
}

__global__ void divergence_kernel(DeviceMesh m, const double* flux, double* out) {
  const int i = blockIdx.x * blockDim.x + threadIdx.x;
  if (i >= m.owned_cells) return;
  double sum = 0;
  for (int k = m.offsets[i]; k < m.offsets[i + 1]; ++k)
    sum += static_cast<double>(m.signs[k]) * flux[m.faces[k]];
  out[i] = sum;
}

__global__ void pressure_operator_kernel(DeviceMesh m, DeviceFlow flow, const double* x,
                                         double* ax, bool anchored) {
  const int i = blockIdx.x * blockDim.x + threadIdx.x;
  if (i >= m.owned_cells) return;
  if (anchored && i == 0) { ax[i] = x[i]; return; }
  double sum = 0;
  for (int k = m.offsets[i]; k < m.offsets[i + 1]; ++k) {
    const int f = m.faces[k];
    if (m.boundary[f] == 1) continue;
    const int other = m.owner[f] == i ? m.neighbor[f] : m.owner[f];
    const double xn = other >= 0 && other < m.owned_cells && !(anchored && other == 0) ? x[other] : 0.0;
    const auto g = geometry(m, f);
    sum += conductance(m, flow, f, g) * (x[i] - xn);
  }
  ax[i] = sum;
}

__global__ void face_correction_kernel(DeviceMesh m, DeviceFlow flow, const double* pc,
                                       const double* gx, const double* gy, const double* gz) {
  const int f = blockIdx.x * blockDim.x + threadIdx.x;
  if (f >= m.face_count || m.boundary[f] == 1) return;
  const int p = m.owner[f], n = m.neighbor[f];
  const auto g = geometry(m, f);
  const double conduct = conductance(m, flow, f, g);
  double correction = conduct * (pc[p] - (n < 0 ? 0.0 : pc[n]));
  const int c = f >= m.split[1] && f < m.split[2] ? f - m.split[1] :
                f >= m.split[3] && f < m.split[4] ? m.split[2] - m.split[1] + f - m.split[3] : -1;
  if (c >= 0) {
    const double beta = n >= 0 && n < m.owned_cells ? g.beta : 0.0;
    const double gradx = gx[p] + (beta ? beta * (gx[n] - gx[p]) : 0.0);
    const double grady = gy[p] + (beta ? beta * (gy[n] - gy[p]) : 0.0);
    const double gradz = gz[p] + (beta ? beta * (gz[n] - gz[p]) : 0.0);
    correction -= conduct / g.alpha * (m.kx[c] * gradx + m.ky[c] * grady + m.kz[c] * gradz);
  }
  flow.face_mass[f] += correction;
}

__global__ void jacobi_kernel(int n, const int* row, const int* col, const float* a,
                              const float* b, const float* x, const float* l1,
                              float* next, float omega) {
  const int i = blockIdx.x * blockDim.x + threadIdx.x;
  if (i >= n) return;
  float ax = 0;
  for (int k = row[i]; k < row[i + 1]; ++k) ax += a[k] * x[col[k]];
  next[i] = x[i] + omega * (b[i] - ax) / l1[i];
}

__global__ void momentum_kernel(DeviceMesh m, DeviceFlow flow, DeviceMomentum fields) {
  const int i = blockIdx.x * blockDim.x + threadIdx.x;
  if (i >= m.owned_cells) return;
  const double storage = flow.rho[i] * m.volume[i] / fields.dt;
  double div = 0;
  for (int k = m.offsets[i]; k < m.offsets[i + 1]; ++k)
    div += m.signs[k] * flow.face_mass[m.faces[k]];
  const double solid = 1.0 - fields.gamma[i];
  double diag = storage + (0.001 + 99.999 * solid * solid * solid) * m.volume[i] + ::fmax(-div, 0.0);
  double bx = storage * fields.oldx[i] + m.volume[i] * (fields.forcex[i] - flow.gradx[i]);
  double by = storage * fields.oldy[i] + m.volume[i] * (fields.forcey[i] - flow.grady[i]);
  double bz = storage * fields.oldz[i] + m.volume[i] * (fields.forcez[i] - flow.gradz[i]);
  for (int k = m.offsets[i]; k < m.offsets[i + 1]; ++k) {
    const int f = m.faces[k], other = m.owner[f] == i ? m.neighbor[f] : m.owner[f];
    const double mass = m.signs[k] * flow.face_mass[f];
    const double diffusion = fields.viscosity[f] * geometry(m, f).alpha;
    diag += diffusion + ::fmax(mass, 0.0);
    if (other >= 0) {
      const double neighbor = diffusion + ::fmax(-mass, 0.0);
      bx += neighbor * fields.iterx[other];
      by += neighbor * fields.itery[other];
      bz += neighbor * fields.iterz[other];
    }
  }
  const double relax = fields.relaxation;
  fields.mobility[i] = m.volume[i] / diag;
  fields.nextx[i] = (1.0 - relax) * fields.iterx[i] + relax * bx / diag;
  fields.nexty[i] = (1.0 - relax) * fields.itery[i] + relax * by / diag;
  fields.nextz[i] = (1.0 - relax) * fields.iterz[i] + relax * bz / diag;
}

__global__ void energy_deferred_kernel(DeviceMesh m, DeviceFlow flow, DeviceEnergy fields) {
  const int f = blockIdx.x * blockDim.x + threadIdx.x;
  if (f >= m.face_count) return;
  const int p = m.owner[f], n = m.neighbor[f];
  if (n < 0) { fields.deferred_flux[f] = 0; return; }
  const auto g = geometry(m, f);
  const double hf = fields.old[p] + g.beta * (fields.old[n] - fields.old[p]);
  const double hu = flow.face_mass[f] >= 0 ? fields.old[p] : fields.old[n];
  double high = hf, blend = 0.6, nonorth = 0;
  const int c = f >= m.split[1] && f < m.split[2] ? f - m.split[1] :
                f >= m.split[3] && f < m.split[4] ? m.split[2] - m.split[1] + f - m.split[3] : -1;
  if (c >= 0) {
    const double beta = n < m.owned_cells ? g.beta : 0.0;
    const double gx = fields.gradx[p] + (n < m.owned_cells ? beta * (fields.gradx[n] - fields.gradx[p]) : 0.0);
    const double gy = fields.grady[p] + (n < m.owned_cells ? beta * (fields.grady[n] - fields.grady[p]) : 0.0);
    const double gz = fields.gradz[p] + (n < m.owned_cells ? beta * (fields.gradz[n] - fields.gradz[p]) : 0.0);
    high += gx * m.skewx[c] + gy * m.skewy[c] + gz * m.skewz[c];
    nonorth = -fields.conductivity[f] * (gx * m.kx[c] + gy * m.ky[c] + gz * m.kz[c]);
    blend = m.omega[c];
  }
  fields.deferred_flux[f] = flow.face_mass[f] * blend * (high - hu) + nonorth;
}

__global__ void energy_kernel(DeviceMesh m, DeviceFlow flow, DeviceEnergy fields) {
  const int i = blockIdx.x * blockDim.x + threadIdx.x;
  if (i >= m.owned_cells) return;
  const double storage = flow.rho[i] * m.volume[i] / fields.dt;
  double diag = storage, rhs = storage * fields.old[i];
  for (int k = m.offsets[i]; k < m.offsets[i + 1]; ++k) {
    const int f = m.faces[k], other = m.owner[f] == i ? m.neighbor[f] : m.owner[f];
    const double mass = m.signs[k] * flow.face_mass[f];
    const double diffusion = fields.conductivity[f] * geometry(m, f).alpha;
    diag += diffusion + ::fmax(mass, 0.0);
    rhs -= m.signs[k] * fields.deferred_flux[f];
    rhs += (diffusion + ::fmax(-mass, 0.0)) * (other >= 0 ? fields.iterate[other] : m.enthalpy_bc[f]);
  }
  fields.next[i] = rhs / diag;
}

__global__ void chemistry_flags(chemistry::ChainABC tag, DeviceChemistry batch,
                                double threshold, int* indices, int* flags) {
  const int i = blockIdx.x * blockDim.x + threadIdx.x;
  if (i >= batch.cells) return;
  double y[chemistry::ChainABC::nspecies], w[chemistry::ChainABC::nspecies];
  double jac[chemistry::ChainABC::nnz];
  for (int k = 0; k < chemistry::ChainABC::nspecies; ++k) y[k] = batch.species[k][i];
  tag.eval(batch.temperature[i], batch.density[i], y, w, jac);
  indices[i] = i;
  flags[i] = 0;
  for (double source : w) if (::fabs(source) > threshold) flags[i] = 1;
}

template <class K> __device__ bool implicit_step(double t, double rho, double h,
                                                    const double* old, const double* previous,
                                                    bool bdf2, double* next) {
  constexpr int n = K::nspecies;
  const double coeff = bdf2 ? 1.5 : 1.0;
  for (int k = 0; k < n; ++k) next[k] = old[k];
  for (int it = 0; it < 20; ++it) {
    double w[n], jac[K::nnz], a[n * n]{}, rhs[n];
    K::eval(t, rho, next, w, jac);
    double error = 0;
    for (int i = 0; i < n; ++i) {
      const double r = coeff * next[i] - (bdf2 ? 2 * old[i] - 0.5 * previous[i] : old[i]) - h * w[i];
      rhs[i] = -r;
      error = ::fmax(error, ::fabs(r));
      a[i * n + i] = coeff;
      for (int k = K::row_start(i); k < K::row_start(i + 1); ++k)
        a[i * n + K::column(k)] -= h * jac[k];
    }
    if (error < 2e-12) return true;
    for (int k = 0; k < n; ++k) {
      int pivot = k;
      for (int i = k + 1; i < n; ++i) if (::fabs(a[i * n + k]) > ::fabs(a[pivot * n + k])) pivot = i;
      if (::fabs(a[pivot * n + k]) < 1e-24) return false;
      if (pivot != k) {
        for (int j = k; j < n; ++j) { double z = a[k * n + j]; a[k * n + j] = a[pivot * n + j]; a[pivot * n + j] = z; }
        double z = rhs[k]; rhs[k] = rhs[pivot]; rhs[pivot] = z;
      }
      for (int i = k + 1; i < n; ++i) {
        const double factor = a[i * n + k] / a[k * n + k];
        for (int j = k + 1; j < n; ++j) a[i * n + j] -= factor * a[k * n + j];
        rhs[i] -= factor * rhs[k];
      }
    }
    for (int i = n - 1; i >= 0; --i) {
      for (int j = i + 1; j < n; ++j) rhs[i] -= a[i * n + j] * rhs[j];
      rhs[i] /= a[i * n + i];
    }
    double step = 1;
    for (int i = 0; i < n; ++i) if (rhs[i] < 0) step = ::fmin(step, 0.99 * ::fmax(next[i], 0.0) / -rhs[i]);
    if (step < 1e-12) return false;
    for (int i = 0; i < n; ++i) next[i] += step * rhs[i];
  }
  return false;
}

template <class K> __global__ void persistent_chemistry(DeviceChemistry batch, const int* queue,
                                                           int* head, double dt) {
  constexpr int n = K::nspecies;
  for (;;) {
    const int slot = atomicAdd(head, 1); // Work-stealing queue; FVM gather itself has no atomics.
    if (slot >= *batch.active) return;
    const int cell = queue[slot];
    const double t = batch.temperature[cell], rho = batch.density[cell];
    double original[n], source[n], jac[K::nnz];
    for (int k = 0; k < n; ++k) original[k] = batch.species[k][cell];
    K::eval(t, rho, original, source, jac);
    double stiffness = 0;
    for (int i = 0; i < n; ++i)
      for (int k = K::row_start(i); k < K::row_start(i + 1); ++k)
        if (K::column(k) == i) stiffness = ::fmax(stiffness, ::fabs(jac[k]));
    int steps = static_cast<int>(::ceil(dt * stiffness / 0.8));
    steps = steps < 1 ? 1 : steps > 4096 ? 4096 : steps;
    bool success = false;
    double old[n], previous[n], next[n];
    for (;;) {
      for (int k = 0; k < n; ++k) old[k] = previous[k] = original[k];
      success = true;
      for (int s = 0; s < steps; ++s) {
        if (!implicit_step<K>(t, rho, dt / steps, old, previous, s > 0, next)) { success = false; break; }
        for (int k = 0; k < n; ++k) { previous[k] = old[k]; old[k] = next[k]; }
      }
      if (success || steps >= 4096) break;
      steps = steps * 2 > 4096 ? 4096 : steps * 2;
    }
    if (!success) { atomicAdd(batch.failed, 1); continue; }
    atomicMax(batch.max_substeps, steps);
    for (int k = 0; k < n; ++k) batch.species[k][cell] = old[k];
  }
}

} // namespace

cudaError_t launch_rhie_chow(DeviceMesh m, DeviceFlow flow, cudaStream_t stream) {
  rhie_chow_kernel<<<(m.face_count + 255) / 256, 256, 0, stream>>>(m, flow);
  return cudaGetLastError();
}
cudaError_t launch_csr_divergence(DeviceMesh m, const double* mass, double* divergence, cudaStream_t stream) {
  divergence_kernel<<<(m.owned_cells + 255) / 256, 256, 0, stream>>>(m, mass, divergence);
  return cudaGetLastError();
}
cudaError_t launch_pressure_operator(DeviceMesh m, DeviceFlow flow, const double* x,
                                     double* ax, bool anchored, cudaStream_t stream) {
  pressure_operator_kernel<<<(m.owned_cells + 255) / 256, 256, 0, stream>>>(m, flow, x, ax, anchored);
  return cudaGetLastError();
}
cudaError_t launch_pressure_face_correction(DeviceMesh m, DeviceFlow flow, const double* pc,
                                            const double* gx, const double* gy, const double* gz,
                                            cudaStream_t stream) {
  face_correction_kernel<<<(m.face_count + 255) / 256, 256, 0, stream>>>(m, flow, pc, gx, gy, gz);
  return cudaGetLastError();
}
cudaError_t launch_l1_jacobi(int n, const int* row, const int* col, const float* a,
                             const float* b, const float* x, const float* l1, float* next,
                             float omega, cudaStream_t stream) {
  jacobi_kernel<<<(n + 255) / 256, 256, 0, stream>>>(n, row, col, a, b, x, l1, next, omega);
  return cudaGetLastError();
}

cudaError_t launch_momentum_sweep(DeviceMesh m, DeviceFlow flow, DeviceMomentum fields, cudaStream_t stream) {
  momentum_kernel<<<(m.owned_cells + 255) / 256, 256, 0, stream>>>(m, flow, fields);
  return cudaGetLastError();
}
cudaError_t launch_energy_deferred(DeviceMesh m, DeviceFlow flow, DeviceEnergy fields, cudaStream_t stream) {
  energy_deferred_kernel<<<(m.face_count + 255) / 256, 256, 0, stream>>>(m, flow, fields);
  return cudaGetLastError();
}
cudaError_t launch_energy_sweep(DeviceMesh m, DeviceFlow flow, DeviceEnergy fields, cudaStream_t stream) {
  energy_kernel<<<(m.owned_cells + 255) / 256, 256, 0, stream>>>(m, flow, fields);
  return cudaGetLastError();
}

cudaError_t launch_chain_chemistry(DeviceChemistry batch, double dt, double threshold, cudaStream_t stream) {
  if (batch.cells <= 0) return cudaSuccess;
  int *indices = nullptr, *flags = nullptr, *queue = nullptr, *head = nullptr;
  void* temporary = nullptr;
  size_t bytes = 0;
  #define CUDA_TRY(call) do { cudaError_t err = (call); if (err != cudaSuccess) return err; } while (0)
  CUDA_TRY(cudaMallocAsync(reinterpret_cast<void**>(&indices), batch.cells * sizeof(int), stream));
  CUDA_TRY(cudaMallocAsync(reinterpret_cast<void**>(&flags), batch.cells * sizeof(int), stream));
  CUDA_TRY(cudaMallocAsync(reinterpret_cast<void**>(&queue), batch.cells * sizeof(int), stream));
  CUDA_TRY(cudaMallocAsync(reinterpret_cast<void**>(&head), sizeof(int), stream));
  CUDA_TRY(cudaMemsetAsync(head, 0, sizeof(int), stream));
  CUDA_TRY(cudaMemsetAsync(batch.failed, 0, sizeof(int), stream));
  CUDA_TRY(cudaMemsetAsync(batch.max_substeps, 0, sizeof(int), stream));
  chemistry_flags<<<(batch.cells + 255) / 256, 256, 0, stream>>>(chemistry::ChainABC{}, batch, threshold, indices, flags);
  CUDA_TRY(cudaGetLastError());
  CUDA_TRY(cub::DeviceSelect::Flagged(nullptr, bytes, indices, flags, queue, batch.active, batch.cells, stream));
  CUDA_TRY(cudaMallocAsync(&temporary, bytes, stream));
  CUDA_TRY(cub::DeviceSelect::Flagged(temporary, bytes, indices, flags, queue, batch.active, batch.cells, stream));
  persistent_chemistry<chemistry::ChainABC><<<(batch.cells + 127) / 128, 128, 0, stream>>>(batch, queue, head, dt);
  CUDA_TRY(cudaGetLastError());
  CUDA_TRY(cudaFreeAsync(temporary, stream));
  CUDA_TRY(cudaFreeAsync(head, stream));
  CUDA_TRY(cudaFreeAsync(queue, stream));
  CUDA_TRY(cudaFreeAsync(flags, stream));
  CUDA_TRY(cudaFreeAsync(indices, stream));
  #undef CUDA_TRY
  return cudaSuccess;
}

} // namespace continuum::cuda