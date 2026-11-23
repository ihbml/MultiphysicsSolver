# Native numerical reference

## Build and run

```sh
python3 native/tools/generate_mechanism.py native/mechanisms/chain.json native/include/generated/chain_abc.hpp
g++ -std=c++20 -O2 -I native/include native/tests/verify.cpp -o verify
./verify
./verify --json > public/verification.json

# Optional threaded work queue and owned-cell loops:
g++ -std=c++20 -O2 -fopenmp -I native/include native/tests/verify.cpp -o verify_omp
OMP_NUM_THREADS=4 ./verify_omp

# CUDA Toolkit 12+; kernels take existing device-resident SoA pointers:
nvcc -std=c++20 -O2 -arch=sm_80 -I native/include -c native/cuda/continuum_cuda.cu -o continuum_cuda.o
```

`native/tools/verify.sh` regenerates the mechanism, executes both CPU builds and refreshes `public/verification.json`. The CUDA translation unit is optional; it was not compiled in the CPU-only reference environment.

## Borrowed array contract

`CAbiSpan<T>` is a POD `{T* data; size_t size;}` borrowed from the caller. `MeshView`, `FlowView`, `CellFaceCSR`, and `CorrectionSoA` contain only spans; field storage remains in the Data Lake. `CellFaceCSR` has `owned_cells+1` offsets and signed incidences. Only `0 <= cell < owned_cells` writes a residual or diagonal. Halo values at `owned_cells <= cell < total_cells` are read-only during the local step.

`split = {0, end(inner_fast), end(inner_corrected), end(halo_fast), end(halo_corrected)}`. `CorrectionSoA` has exactly seven compact arrays of length `|Seg 1| + |Seg 3|`; fast faces never dereference them. The verification mesh with 33 faces allocates two corrected slots (6.06%). `populate_corrections()` fills `skew = x_f - (x_P + beta d)`, `k = S - alpha d`, and the deferred-correction blend. The face order is supplied by the caller; it is not re-sorted.

For a face directed from owner P to neighbor N, require `d . S > 0`:

```text
d = x_N - x_P
beta  = ((x_f - x_P) . S) / (d . S)
alpha = (S . S) / (d . S)
g_f   = rho_f D_f alpha
m_f*  = rho_f [u_beta . S - D_f alpha (p_N - p_P - grad(p)_beta . d)]
```

The pressure bracket is identically zero for every affine `p(x)` even when the face is skew and nonorthogonal. A vector-minus-scalar variant of the Rhie-Chow expression is dimensionally invalid; the scalar pressure jump is compared with `grad(p) . d`. On a corrected face, deferred nonorthogonal pressure-correction flux is `q_f = rho_f D_f (k . grad(p'))`:

```text
A p' = -div(m*) + div(q)
A_i p' = sum_faces g_f (p'_i - p'_neighbor)
m_f = m_f* + g_f (p'_P - p'_N) - q_f
```

Flux boundaries hold `m_bc` fixed; pressure boundaries set `p'_boundary=0`. A closed pure-flux domain fixes cell zero as gauge (the net prescribed boundary flux must vanish). An off-rank neighbor is treated as a fixed ghost pressure correction in this local solve; exchange updated halo state before the next global pressure iteration. `FlowView::halo_grad*` accepts externally exchanged ghost gradients; absent those, a copied owner gradient is linear-exact only for locally constant gradients.

`advance()` supports SIMPLE (one pressure solve, relaxed pressure), PISO (two reconstructed projections), and PIMPLE (multiple outer PISO sweeps). Momentum uses transient storage, face upwind advection, viscosity, SIMP Brinkman drag, and `max(-div(m),0)` diagonal stabilization. Enthalpy uses upwind implicit transport and gradient-free deferred linear interpolation on fast faces; corrected faces use skew/nonorthogonal terms.

`native/cuda/continuum_cuda.cu` provides face reconstruction, deterministic CSR divergence and pressure-operator gathers, corrected-face pressure fluxes, momentum/enthalpy Jacobi sweeps, FP32 L1 smoothing, and CUB-compacted persistent stiff-chemistry workers. Production device fields and their ping-pong Jacobi buffers stay in caller-owned GPU SoA memory; only the temporary chemistry queue is stream-allocated. The CPU AMG hierarchy currently owns its own CSR coarse matrices; a fully device-resident AMG hierarchy and multi-rank pressure exchange are not part of this reference.

## Linear, reaction, and derivative contracts

`linear.hpp` builds strength-filtered deterministic PMIS aggregates, optional smoothed prolongation, and a Galerkin `P^T A P` hierarchy. L1-Jacobi or Chebyshev polynomial smoothing performs parallel sparse row operations. The FP32 V-cycle preconditions FP64 iterative refinement, BiCGStab, and flexible GMRES. `eta_round = ||b-Ax||_inf/(||A||_inf ||x||_inf + ||b||_inf)` is evaluated from the FP64 true residual; `fp32_floor` estimates inner roundoff. Residual plateaus mark `stagnated`, and a plateau below eight inner precision floors marks `round_limited`. `pressure_apply_matrix_free()` is checked against the assembled pressure CSR.

The mechanism generator accepts a mass-conserving mass-action JSON mechanism with arbitrary species/reactions and emits unrolled sources plus an analytical sparse `d(omega)/dY` pattern. `operator_split_bdf2()` does backward-Euler bootstrap, BDF2/Newton thereafter, positivity line search, stiffness-selected substeps, deterministic active-cell prefix compaction, and an atomic fetch-add **only** for the work-stealing chemistry queue. The generated `ChainABC` mechanism is one executable verification case; transport is applied before the operator-split call.

`adjoint.hpp` differentiates a steady collocated Stokes/Brinkman/Rhie-Chow discrete residual with forward duals, assembles a sparse state Jacobian, and solves `R_U^T lambda = -J_U^T`. It evaluates `J_q + lambda^T R_q` for cell porosity and all boundary centroid/normal components, then contracts those arrays with a CAD direction. The three-cell primal and transpose solves in this standalone reference use pivoted dense elimination after sparse assembly; this path is intended for derivative verification, not a large production saddle-point solve. `enzyme_bridge.cpp` exposes the pure owned-cell CSR contraction for optional Enzyme reverse differentiation with `CONTINUUM_USE_ENZYME` and an Enzyme-enabled compiler.

## Assertions in `native/tests/verify.cpp`

- Skew/nonorthogonal affine-pressure Rhie-Chow defect `< 1e-13`, with nonzero response to alternating pressure.
- Pure-flux closed-domain owned-cell continuity `< 1e-11`.
- Matrix-free pressure gather equals pressure CSR to `< 1e-14`.
- FP32 AMG / FP64 IR, BiCGStab, FGMRES converge on a 128-cell Poisson system.
- AoT sparse chemistry Jacobian agrees with central differences, with conservative positive BDF2 chemistry and cold-cell skipping in both 32-cell and 512-cell (OpenMP queue) batches.
- Coupled IFT sensitivities for porosity and both boundary face position/normal, plus their CAD directional contraction, agree with re-solved central finite differences to `< 1e-8`.
- A PISO momentum / Rhie-Chow / pressure / enthalpy step conserves mass.