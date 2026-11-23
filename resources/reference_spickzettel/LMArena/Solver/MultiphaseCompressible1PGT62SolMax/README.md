# Cluster B: Multiphysics Workbench

The workbench opens a native C++ reference run for each of four benchmarks. Play or step through that run, select a physical field, probe a cell, or download the diagnostics. To solve a different configuration, change the settings and choose **Run Simulation**. The browser creates a fresh instance of the same C++ core at interactive resolution using a WebAssembly worker. It does not emulate CUDA.

## Build and run

```sh
npm install
npm run dev
npm run build

make          # build/native/cluster-b, C++20 CPU executable
make verify   # assertion-based CPU tests, thresholds and measured values
make cuda     # CUDA 12+, change CUDA_ARCH to match the GPU
make cuda-check
```

The native CPU targets do not require CUDA, Emscripten, OpenFOAM, or npm. They can also be built directly:

```sh
g++ -std=c++20 -O2 native/main.cpp -o cluster-b
g++ -std=c++20 -O2 native/tests.cpp -o cluster-b-tests
./cluster-b-tests
./cluster-b bubble bubble.cbf 1 hllc stiffened 0.42 1 1
./cluster-b sod sod.cbf 2 ausm ideal
```

`make site-assets` regenerates the replay archives, the measured report, the downloadable sources, and the WebAssembly bundle. It requires Emscripten, GNU Make, and Python 3. To run the CUDA face-flux backend, replace the CPU executable with `build/native/cluster-b-cuda`. A missing CUDA device or driver skips the parity suite with exit code 77; this does not indicate a GPU test pass.

## Numerical methods

| Component | Implementation |
| --- | --- |
| `native/physics.hpp` | Ideal and stiffened-gas closure, stable single-phase Peng-Robinson with caloric departure energy and thermodynamic sound speed, frozen-volume mechanically equilibrated mixture, Schnerr-Sauer mass exchange, HLLC/HLLE and AUSM+-up |
| `native/fvm.hpp` | Weighted least-squares MUSCL reconstruction using oriented polyhedral faces, smooth Venkatakrishnan slope limiting, SSP-RK2, conservative face exchange and admissibility fallback |
| `native/hybrid.hpp` | Truncated-support ghost completion, pivoted 3x3 kernel moment inversion and affine bidirectional state transfer. The planar benchmark inverts the physical 2x2 subspace. |
| `native/film.hpp` | Finite-area mass, tangential momentum and enthalpy, gas stress and pressure-gradient loads, gravity, viscous drag and evaporation. Drag-integrated droplet trajectories, segment-wall intersections, and a sub-boiling Bai-Gosman regime map. |
| `native/cuda_backend.cu` | Batched face fluxes integrated into the native driver; separate device batches for local source and consistency operations. Mesh assembly, time integration and trajectory bookkeeping are on the host. |

**All-Mach flux.** The face Mach limiter is `min(max(M_L, M_R, 0.001), 1)`. In the HLLC star pressure, let `A_L = rho_L (u_L - S_L)` and `A_R = rho_R (S_R - u_R)`. The pressure's velocity-jump term is `A_L A_R (u_L - u_R) / (A_L + A_R)`. The all-Mach flux scales that term in the normal pressure and energy flux, retaining physical acoustic wave bounds and pressure-jump stabilization. A pressure sensor restores dissipation at strong discontinuities. This is a flux-preconditioned variant with the requested Mach-based limiter, not a verbatim reproduction of Wang et al.'s velocity-jump-based limiter. AUSM+-up uses Liou's Mach and pressure polynomials, pressure and velocity diffusion, and critical interface sound speed for ideal gas. The non-ideal EOS path uses thermodynamic sound speeds at the face.

**Reconstruction.** `Face` contains the neighbor indices, oriented unit normal, face measure, and separate offsets from the owner and neighbor centers. This also allows periodic faces to supply wrapped offsets. Limiters regularize extrema and the direction switch using a smooth norm; their regularization scales with cell size. The limiter has no hard sign or extremum branches, but EOS admissibility, shock switches and rollback make the full integrator piecewise differentiable. Enzyme itself has not been run.

**Zero-order completion.** For `G = sum(V_j grad W_ij)`, the ghost has vector area `A = -G`, radial weight `-|A|`, and direction `-A/|A|`. If `|A| = 0`, its weight and direction are zero. Any fixed pressure/velocity momentum-flux tensor `T` gives `T (G + A) = 0`. The difference-form kernel gradient in the hybrid path uses this same zero-order cancellation for constant data. The curved truncation test also rebuilds `A` from its radial weight and direction, which incurs floating-point roundoff. The sub-`1e-15` bound applies to the specified nondimensional test, not to all possible scalings or nonconstant wall data.

**Moving hybrid interface.** The local state transfer uses `B = inv(sum((x_j - x_i) tensor grad W_ij V_j))`; the gradient correction acts by `B^T` with this outer-product convention. Applying it to affine differences gives the exact gradient whenever the stencil is full rank. The moving patch follows the vapor centroid, reseeds translated particles on the grid at cell crossings, and applies particle-to-face reconstruction with a smooth transition. The shipped implementation is an OPMIS-inspired conservative hybrid: it keeps the finite-volume control-volume backbone and uses moving SPH samples to reconstruct flux states. It does not implement Zhang et al.'s separate mesh-free Riemann-SPH integrator or reproduce their published convergence study. The affine transfer test is exact up to roundoff; limiter activation and the EOS projection at a phase jump can change the reconstructed trace.

**Thermophysical scope.** The bubble deck is a nondimensional demonstrator, not a calibrated water/steam collapse experiment. For the PR variant, the liquid is stiffened gas and the vapor uses PR. The trace-phase volume projection keeps the PR branch dilute when partial mass and volume fraction become inconsistent across the smeared interface. Conserved total mass, momentum and energy are not clipped by this projection. The PR implementation is not a two-phase flash calculation. The film consumes supplied gas-wall loads, records evaporated mass in a vapor ledger, and removes enthalpy from the film. There is no two-way solve with a resolved 3D gas field in this deck. Splash parcel generation is deterministic and mass-conservative, not a calibrated atomization model.

## Verification

`native/tests.cpp` checks Sod against an exact Riemann solution, acoustics against a translated linear pulse, and MUSCL convergence with a transported smooth entropy wave. Bubble checks are numerical invariants and features (mass and energy budgets, vapor area decrease, patch motion), not validation against a published bubble experiment. The film test closes the ledger over wall film, active and escaped droplets, vapor, and injected mass. The four case reports and component checks are generated by `native/report.py` into `public/data/verification.json`. CPU reference measurements are full precision. Display fields in the browser replay use per-frame 13-bit quantization; raw native `.cbf` fields are float32.

- Toro, Spruce and Spears, [Restoration of the contact surface in the HLL-Riemann solver](https://doi.org/10.1007/BF01414629).
- Liou, [A sequel to AUSM, part II: AUSM+-up for all speeds](https://doi.org/10.1016/j.jcp.2005.09.020).
- Wang et al., [Extended Eulerian SPH and its realization of FVM](https://arxiv.org/abs/2309.01596).
- Zhang et al., [An adaptive SPH-FVM method with optimized particle-mesh interpolation](https://www.repository.cam.ac.uk/handle/1810/383137).
- Bai and Gosman, [Development of methodology for spray impingement simulation](https://doi.org/10.4271/950283).