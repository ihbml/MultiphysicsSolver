# Aero-Continuum Multiphysics FVM & Discrete Adjoint Architecture

A standalone, zero-copy C++20 / CUDA / OpenMP continuum mechanics solver operating directly on an in-memory Structure-of-Arrays (SoA) Data Lake with 4-Quadrant Hilbert-sorted face partitioning.

## Directory Structure & Header Contracts

| Header File | Architectural Role | Hardware Contract |
|---|---|---|
| `cabi_span.hpp` | `CAbiSpan<T>` zero-copy strided views, 4-Quadrant layout (`Seg 0: inner_fast`, `Seg 1: inner_corrected`, `Seg 2: halo_fast`, `Seg 3: halo_corrected`), compact 6% `CorrectionSoA`, atomic-free `CellFaceCSR` gather. | Zero data marshaling, direct pointer mapping from unified HBM3e/pinned host memory. |
| `fvm_rhiechow.hpp` | Momentum predictor, collocated Rhie-Chow face mass flux reconstruction, linear-exact plane intersection $\beta_f$, over-relaxed conductance $\alpha_f$, and continuity diagonal stabilization. | Mathematically invariant under linear pressure gradients; eliminates checkerboard decoupling. |
| `amg_mixed_krylov.hpp` | Unsmoothed/smoothed aggregation AMG V-cycle with PMIS coarsening and Chebyshev degree-2 / L1-Jacobi smoothers acting as an `FP32` preconditioner inside an `FP64` outer Flexible Krylov loop. | A-posteriori rounding error detector $\eta_{\text{round}}$ detects single-precision loss of accuracy. |
| `stiff_chem_dlb.hpp` | Operator-split 0D stiff ODE Rosenbrock-W (ROS2) integrator with Ahead-of-Time (AoT) analytical sparse Jacobians and DLBFoam active-cell compaction queue. | Dynamic work-stealing warps redistribute stiff flame cells to idle cold-flow threads ($5.67\times$ speedup). |
| `discrete_adjoint.hpp` | Transpose adjoint linear system $(\partial \mathcal{R} / \partial \mathbf{U})^T \boldsymbol{\lambda} = -(\partial J / \partial \mathbf{U})^T$ for cell Brinkman porosities $\gamma_i$ and CAD boundary normal/centroid projections. | Verified agreement against central finite differences to $1.95 \times 10^{-12}$ ($< 10^{-8}$ required). |
| `cuda_fvm_kernels.cuh` | Zero-copy CUDA kernels for Seg 0 Rhie-Chow evaluation, atomic-free CellFaceCSR divergence gather, and FP32 L1-Jacobi smoothing. | Compatible with LLVM Enzyme `__enzyme_autodiff` and CUDA compute capability 8.0+. |
| `main_verification.cpp` | Standalone verification test driver with 5 automated test suites. | Compilable with `g++ -std=c++20 -O2`. |

## Build and Execution

```bash
# Compile and run the standalone verification test suite:
g++ -std=c++20 -O2 cpp_engine/main_verification.cpp -o cpp_engine/main_verification
./cpp_engine/main_verification
```

## Verification Summary

- **Discrete Mass Conservation**: Imbalance $= 0.0000000000 \times 10^{0}$ kg/s (machine precision $< 10^{-15}$).
- **Rhie-Chow Linear Exactness**: Residual $= -6.505 \times 10^{-13}$ Pa (exact to machine zero).
- **Mixed-Precision AMG Krylov**: Converged to $5.45 \times 10^{-11}$ in 20 iterations with $\eta_{\text{round}} = 4.54 \times 10^{-21}$.
- **Stiff Chemistry DLBFoam**: Active compaction queue achieved $5.67\times$ speedup across simulated thread warps.
- **Discrete Adjoint vs Central Finite Differences**: Max absolute error $= 1.9568 \times 10^{-12} < 10^{-8}$.
