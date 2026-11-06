# Basic 3D Eulerian two-fluid prototype

This **separate prototype** starts a two-fluid multiphase extension of the earlier solver. It does not yet integrate the earlier temperature solver. It evolves gas and liquid volume fractions and separate velocities on a distributed tetrahedral mesh, with shared pressure, gravity and interphase drag. A Gaussian gas-rich region in a closed unit cube is used as a rising-bubble **demonstration**, not as a validated bubble benchmark.

**Important: this is experimental numerical infrastructure, not a physically validated CFD solver.** The pressure discretization uses a two-point face approximation that omits non-orthogonal corrections; general tetrahedra are non-orthogonal, including the demo mesh. This can cause substantial errors. Viscosity, surface tension, turbulence, phase change and energy transport are absent, among other limitations below. Passing the smoke tests is not evidence of predictive accuracy. Do not use these results for engineering decisions.

## Build and run

Requires a C++17 compiler, CMake >=3.16, MPI, pkg-config and a compatible PETSc installation with **real scalars**; tested with PETSc 3.22.5. PETSc and the application must use compatible MPI implementations.

```bash
# Example package names on Debian
sudo apt install cmake pkg-config libpetsc-real-dev
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j

# 4 x 4 x 4 cubes, 6 tetrahedra per cube; 10 time steps
./build/two_fluid_3d -steps 10
mpirun -n 2 ./build/two_fluid_3d -steps 10

# Write final cell fields for ParaView
mpirun -n 2 ./build/two_fluid_3d -steps 10 -write_vtk
# Open two_fluid_final.pvtu; keep the rank*.vtu files alongside it.

ctest --test-dir build --output-on-failure
```

If Open MPI detects too few slots for local tests, add `--oversubscribe` to `mpirun`; for CTest configure with `-DMPIEXEC_PREFLAGS=--oversubscribe`. Python 3 is optional for the serial/parallel field-comparison test.

Demo controls: `-n` (grid subdivisions per side; default 4), `-steps` (10), `-dt` (0.001), `-rho_l` (1000), `-rho_g` (10), `-drag` (1000), `-gravity` (-9.81, z direction). Parameters are consistent with SI units if coordinates are interpreted in metres: densities kg/m³, time s, gravity m/s², and drag coefficient kg/(m³·s). `-pressure_ksp_monitor` and other `pressure_` PETSc options control the pressure solve. Time stepping is explicit for transport; the code stops if its mixture transport CFL exceeds 0.9. This is not a complete stability guarantee. The low-order limiter may reduce slip transport substantially; it is not a validated physical closure.

## What is implemented

* **Two interpenetrating Eulerian phases**, with cell-centered gas volume fraction `alpha_g`, liquid fraction `1-alpha_g`, separate three-component velocities and one shared pressure. Both phase densities are constant; there is no mass transfer.
* A phase-fraction and phase-momentum **donor-cell flux update**. The mixture flux is pressure-projected and split into an upwind mixture contribution and a relative-velocity slip contribution. Outgoing phase-volume budgets limit slip flux. This is intended to conserve gas volume and keep phase fractions bounded on the tested closed domain, subject to pressure-solve and floating-point tolerances. The limiter is numerical, not a surface-tracking or interface-sharpening model.
* Gravity and local implicit interphase drag. The force on gas is `K*(u_l-u_g)` and the opposite force acts on liquid, with `K = drag * alpha_g * (1-alpha_g)` per mixture volume. This simple coefficient is a placeholder, not a calibrated bubble drag law.
* A PETSc pressure solve with a constant-pressure nullspace and local cell-centered velocity correction. External faces are impermeable; there are no inflow or outflow boundary conditions. Pressure is gauge-relative (zero-mean), not an absolute thermodynamic pressure.
* Distributed state vectors, face-neighbor halo exchange and off-rank pressure matrix entries. MPI ranks own disjoint cell ranges. The solver consumes prebuilt local geometry and neighbor data, but the **demo mesh builder temporarily replicates the global mesh on every rank** and is therefore not suitable for large meshes.
* Optional final ASCII VTK `.vtu` piece files and a `.pvtu` index, with cell-centered gas fraction, relative pressure, liquid velocity and gas velocity. `example_final.csv` contains the per-cell results from the 2-rank, 10-step default demonstration. Pressure is from the last projection, followed by the phase transport update; it is not a fully converged end-of-step pressure.

## What is deliberately missing or approximate

This is **not yet a complete Eulerian–Eulerian two-fluid model**. Among the missing features are viscous stress and wall shear, non-orthogonal pressure/gradient correction, interface pressure terms and regularization, turbulent stresses, surface tension, realistic drag/bubble-size closures, heat transfer, mass transfer, phase appearance/disappearance treatment, species transport and particle/spray models. External boundaries are closed and impermeable. The phase velocity is set to zero when its volume fraction falls below `1e-12`, potentially discarding tiny amounts of phase momentum. Phase-fraction bounds are checked with a `1e-10` tolerance and are not repaired by conservative redistribution.

The collocated pressure, phase momentum and velocity reconstruction are only a simplified split scheme. The solver reports the divergence of the **projected face mixture flux**, before the subsequent momentum transport; that number does not prove that reconstructed cell velocities satisfy continuity or that momentum and energy errors are acceptable. The pressure projection omits non-orthogonal correction, and the advection/drag limiter is strongly numerical. The split scheme has no proof of stability for arbitrary density ratios, drag strengths, meshes or time steps. A volume-conserving, rising centroid is only a sanity check.

## Supplying an existing distributed mesh

Replace `make_demo_mesh` and `initial_bubble` in `main` with your application code. Each rank supplies `Mesh::cells` for its own cells and the same global `Mesh::total`. The local cells must appear in contiguous global-ID order matching PETSc row ownership: `mesh.begin` through `mesh.begin + cells.size() - 1`. PETSc ownership is determined by the local cell counts on all ranks.

Each `Cell` needs its global `id`, four vertex coordinates (used by VTK), centroid, positive volume and four `Face` records. For an interior face, `neighbor` is the global ID of the other cell; `area_vector` points out of the current cell and has magnitude equal to face area; `distance` is the positive projection of the center-to-center vector onto that outward unit normal. A boundary face uses `neighbor=-1` and an outward area vector. Both owners of a shared face must provide matching reciprocal neighbor IDs, opposite area vectors, consistent area and projected distance. The cells must form one connected, closed domain; the provided pressure nullspace assumes exactly one connected component.

The application is responsible for partitioning, building/matching face adjacency, consistent geometry and boundary semantics. The solver checks local IDs, volume, face area and basic neighbor ranges but **does not verify reciprocal faces, manifoldness, connectivity, halo consistency or full mesh quality**. Arbitrary mesh types, open boundaries and non-orthogonal consistency are not implemented.

## Verification performed

With PETSc 3.22.5, the default 384-cell case ran for 10 steps with 1, 2 and 4 MPI ranks. The gas centroid rose from approximately 0.31545 m to 0.32373 m, while relative gas-volume change was approximately 1e-15 or less and the observed phase fractions stayed within [0,1]. The default zero-gravity case remained stationary. A 1-versus-2-rank output comparison passed; maximum field differences were approximately `2.7e-13` for gas fraction, `2.4e-8 Pa` for pressure, `1.6e-13 m/s` for liquid velocity and `2.4e-10 m/s` for gas velocity.

These tests check implementation behavior only. Before using a two-fluid solver for realistic applications, add analytical advection and drag-relaxation tests, independent mass and momentum budgets, hydrostatic and non-orthogonal-mesh tests, mesh/time refinement, serial/parallel scaling, and a reference rising-bubble benchmark after implementing the required viscosity and surface-tension models. Only then should cavitation, evaporation, energy and species coupling be layered on top with conservative interphase source terms.
