# Distributed 3D flow + heat solver

A **prototype for 3D transient incompressible flow coupled to heat transport** on unstructured linear tetrahedral meshes. It uses C++17, MPI and PETSc and accepts application-supplied mesh connectivity and coordinates in memory. A unit-cube tetrahedral mesh generator is included solely for verification.

This is a starting point, not a production-validated CFD engine or a claim of optimal performance. In particular, the discretization and preconditioners have significant limitations described below.

## Build and run

Requirements: CMake 3.16 or later, a C++17 compiler, MPI, pkg-config, and PETSc discoverable as `PETSc` or `petsc` by pkg-config. The tests below used PETSc 3.22.5 with real scalars. PETSc and the application must use compatible MPI implementations.

For example, on a Debian system with the appropriate packages:

```bash
sudo apt install cmake pkg-config libpetsc-real-dev
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j

# Steady shear-flow / linear-temperature smoke test
./build/multiphysics3d
mpirun -n 2 ./build/multiphysics3d -steps 1

# Hydrostatic buoyancy smoke test
./build/multiphysics3d -steps 1 -demo_buoyancy
mpirun -n 2 ./build/multiphysics3d -steps 1 -demo_buoyancy

ctest --test-dir build --output-on-failure
```

When Open MPI detects fewer execution slots than requested, add `--oversubscribe` to `mpirun`. For CTest you can configure with `-DMPIEXEC_PREFLAGS=--oversubscribe`; use your MPI implementation's equivalent when applicable.

The demo defaults to a 4 x 4 x 4 cube grid, 6 tetrahedra per cube and 2 time steps. Change it with `-nx`, `-ny`, `-nz` and `-steps`. PETSc options are prefixed with `flow_` or `heat_`, e.g. `-flow_ksp_monitor -heat_ksp_monitor -log_view`; `-picard_max_it` and `-picard_tol` configure the outer coupling iteration. If a KSP or Picard iteration fails, the partial solution is not an accepted time step.

## Equations and numerical method

```text
rho (du/dt + u · grad u) - mu Laplacian(u) + grad p
    = rho beta (T - Tref) g
-div(u) - pressure stabilization = 0, in the weak form
rho cp (dT/dt + u · grad T) - k Laplacian(T) = 0
```

* Three velocity components, pressure and temperature; continuous linear (P1) tetrahedral shape functions. Constant density, dynamic viscosity, heat capacity, conductivity, expansion coefficient and gravity. Boussinesq coupling enters the momentum equation.
* Backward Euler time stepping. Momentum uses an advecting velocity from the previous Picard iterate; the heat solve uses the newly computed velocity. The sequence is repeated to an update-based convergence criterion.
* The equal-order velocity-pressure pair uses a **simple pressure-Laplacian stabilization**, `-tau * integral(grad q · grad p)`, where `tau = pressure_stabilization * h^2 / mu` and `h` is the longest tetrahedron edge. This is not residual-consistent PSPG and perturbs discrete continuity. The coefficient is problem-dependent; verify mesh convergence and mass balance. For a closed domain with velocity specified everywhere, the demo pins one pressure degree of freedom.
* Momentum and heat advection use plain Galerkin, with **no SUPG or other advection stabilization**. High-Reynolds/high-Peclet cases can oscillate or fail. This does not model turbulence, compressibility, moving meshes, phase change, radiation, nonlinear materials, or adaptive time steps; it contains no heat source or body force other than buoyancy.
* Dirichlet conditions are imposed with RHS lifting. Unspecified boundaries take the natural conditions of this weak form. The application may update the constraint values before each step for time-dependent Dirichlet data.
* Picard convergence uses `max(||delta flow||/(1+||flow||), ||delta T||/(1+||T||))`. This mixes quantities with different units and is **not a scaled nonlinear residual**. Use appropriate units and implement residual, conservation and application-specific acceptance checks for real simulations. Failed steps are not rolled back automatically.

## Connect your in-memory mesh

Replace the demo `main` and its mesh, initialization and checking helpers, or extract the solver into your application's library. Use a single MPI communicator throughout.

1. Populate `LocalMesh` **before** constructing/creating `Solver`. `global_nodes` must be the same on every rank. Node IDs must be contiguous, zero-based, and in `[0, global_nodes)`.
2. `elements` holds the tetrahedra **owned by this rank only**. Each global element must occur exactly once across all ranks. Each `Tetrahedron::n` holds four global node IDs and their `(x,y,z)` coordinates. Shared nodes must have matching coordinates on all ranks. Either tetrahedron orientation is supported; degenerate and extremely ill-conditioned elements are rejected.
3. The application supplies the mesh partition, local element connectivity, consistent coordinates and globally numbered nodes. The solver does not partition the mesh, detect duplicate elements, or validate all connectivity. Element ownership does not have to match PETSc's row ownership.
4. Set the `Physics` fields before constructing `Solver`, then call `solver.create()` collectively. Positive density, viscosity, heat capacity, conductivity, timestep and stabilization are required. Thermal expansion can have either sign. Parameters are currently configured through the C++ API, not command-line flags.
5. After creation, use `VecGetOwnershipRange` on `solver.flow` and `solver.temp`. Each rank must populate `mesh.flow_bc` and `mesh.temperature_bc` with **only the global rows it owns**, once per constrained row. Flow rows are `4*node+0`, `+1`, `+2` for velocity x/y/z and `4*node+3` for pressure. Temperature rows are `node`. Supply physically appropriate velocity, temperature and pressure conditions; in a fully velocity-Dirichlet closed domain, include one pressure reference. Constraints must be consistent across the application. All ranks participate in constraint application, even those with no constrained rows.
6. Initialize `solver.flow` and `solver.temp` using PETSc's owned ranges or `VecSetValues`, followed by vector assembly. Call `solver.step(max_iterations, tolerance)` collectively per time step, handle failure as a failed step, and call `solver.destroy()` collectively when done.

The `-demo_buoyancy` switch applies only to the included test. In an actual application set `Physics::beta`, `reference_temperature`, and `gx`, `gy`, `gz` explicitly and supply your own boundary and initial conditions.

## Parallelism and performance

PETSc distributes solution vectors and sparse matrices. Each rank stores its local elements; PETSc scatters required off-rank values to local element buffers and accumulates off-rank assembly contributions. The demo divides elements without replicating the mesh. The implementation is CPU/MPI only.

The flow defaults to restarted GMRES with block Jacobi; temperature defaults to GMRES with Jacobi. These are baseline choices, **not a guarantee of scalable saddle-point solves**. Use PETSc profiling (`-log_view`) on the target machine; investigate pressure/velocity block or Schur-complement preconditioning and an appropriate AMG heat preconditioner supported by the PETSc build. Accurate setup and scaling must be benchmarked for your own model.

The code favors clarity over peak speed: it uses element-by-element sparse assembly, heuristic preallocation and repeated ghost entries. Potential optimizations include deduplicating halo exchanges, exact sparsity preallocation, graph partitioning/load balancing, reuse of invariant operators, improved block preconditioning, matrix-free kernels and accelerator support. Avoid aggressive floating-point modes such as `-ffast-math` without an accuracy study.

## Verification performed

Two steady, exactly representable smoke tests are included; both initialize the solution to the exact steady state and check that stepping preserves it:

1. `u=(y,0,0)`, `p=0`, `T=y`, no buoyancy, velocity and temperature prescribed on all boundary faces, with one pressure pin.
2. With `-demo_buoyancy`: `u=(y,0,0)`, `p=-0.8z`, `T=1`, `beta=0.8`, `g=(0,0,-1)`. For this manufactured test, pressure is prescribed on all boundary nodes as well as velocity and temperature, so the interior pressure-Laplacian residual vanishes for linear pressure. This special test setup is **not** the recommended generic pressure-boundary prescription.

On PETSc 3.22.5, both tests passed with 1, 2 and 4 MPI ranks on the default mesh, with maximum nodal errors on the order of 1e-11 or less. These tests exercise 3D tetrahedral assembly, both element orientations, nonzero Dirichlet lifting, off-rank contributions, and a constant-temperature buoyancy balance. They do **not** establish transient convergence, spatial convergence, general two-way buoyancy feedback, conservation, scalability or production readiness. Add nontrivial manufactured solutions, mesh/time refinement, conservation tests, serial/parallel comparisons and representative benchmark cases before relying on results.
