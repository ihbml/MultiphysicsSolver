# VECTOR Cluster C: C++20 Numerical Reference

This is a compilable, dependency-free reference implementation with executable
numerical verification. It is not a calibrated production solver, a flight
certification artifact, or a battery-safety certification artifact.

## Compile and Run

```sh
g++ -std=c++20 -O2 cluster_c.cpp -o cluster_c
./cluster_c --verify
./cluster_c --verify --json > verification.json
./cluster_c --demo battery > battery.csv
./cluster_c --demo fwh > pressure.csv
./cluster_c --demo mhd > hartmann.csv
./cluster_c --demo tps > tps.csv
```

From the web project root, `bash scripts/verify-native.sh` recompiles the native
reference, runs every verification case and all four demonstrations, and refreshes
the downloadable JSON verification report.

Define `CLUSTER_C_LIBRARY` to compile without `main`. The public C-linkage entry
points return zero on success and -1 on failure. Retrieve a thread-local message
with `cluster_c_last_error()`. Do not allow simultaneous mutation of the same
state spans. State can be partially updated after an error; checkpoint before
steps that may fail.

## Execution Boundary

The web workbench executes independent TypeScript demonstrators. It does not
pretend that a native C++ process or a remote HPC cluster is connected. The native
verification report is produced by actually compiling and running the C++ source
at development/build time. Browser verification runs locally on demand. The two
implementations have intentionally different spatial/time discretizations; their
trajectories are not claimed to be identical.

## ABI and Topology

The proprietary CAbiSpan definition was not supplied. This file defines a small,
documented adapter, not an assertion of compatibility with an unknown ABI:

```cpp
struct CAbiSpan { double* data; uint64_t size; };
struct CAbiIndexSpan { const int32_t* data; uint64_t size; };
```

Coordinates, face centers, area vectors, and non-orthogonal correction vectors
are each three independent contiguous arrays. Cell fields have `nc` entries;
face fields have `nf` entries. State spans are non-owning; the caller retains
ownership and lifetime responsibility. Faces have one owner and a neighbor, or
`neighbor == -1` for a boundary. A face area vector points outward from its owner.

All coordinates are meters, temperature kelvin, time seconds, pressure pascals,
conductivity S/m, and heat volumetric W/m3 unless explicitly named `source_W`.
`face_current` is integrated current in amperes, not A/m2. `J_P` is A/m2.

For internal center displacement `d = xN - xP`, use the over-relaxed decomposition

```
alpha = dot(S,S) / dot(S,d)
k_corr = S - alpha*d
```

For a Dirichlet boundary use `d = xf - xP`. Positive `dot(S,d)`, valid geometry,
closed cells, and meaningful face centroids are the caller's responsibility.
The sample mesh generator supplies an affine-skewed 3D finite-volume topology.

`project_flux` assembles one conservative owner-neighbor flux stencil, solves it
with diagonally preconditioned conjugate gradients, and iterates the deferred
non-orthogonal correction. All-Neumann systems pin one cell and check source/flux
compatibility. Disconnected domains need separate gauges and are not supported
by the single-gauge adapter. Highly skewed meshes may require relaxation or a
better mesh; a failure raises an error rather than silently declaring convergence.

Face interpolation uses owner-neighbor normal-distance weights, including the
resistance-weighted harmonic diffusion coefficient. Face fluxes are assembled
once, with opposite signs for the two adjacent cells.

## Battery

`ecm_step` advances a discharge-positive 2-RC Thevenin circuit. SOC uses exact
coulomb counting for constant current; RC branches use the exact exponential
for coefficients frozen over each step. Resistances depend on SOC/temperature.

```
dSOC/dt = -I/(3600 Q_Ah)
Vterm = Uocv - I R0 - V1 - V2
q_rev = I T dUocv/dT
signed_irr = I (Vterm - Uocv)
heat_generation = -signed_irr + q_rev
```

The requested signed irreversible expression is negative on discharge with this
voltage convention. The opposite sign is used as heat generation. This is an
overpotential-heat approximation, not a claim that capacitor storage is itself
irreversible: the exact branch dissipation would be V1^2/R1 + V2^2/R2, with the
remaining I(V1+V2) accounted for as RC stored-energy change. The entropic
coefficient here is an illustrative effective value. Replace the OCV and material
laws with your chemistry's calibrated tables. Current should be terminated at
SOC/voltage limits by the calling BMS; clamping SOC is not an electrochemical
cutoff model.

The OCV includes the same linear temperature coefficient used in `q_rev`; its
derivative is independently checked. The entropic source uses the plus-sign law
explicitly requested here. Many discharge-positive Bernardi conventions instead
write thermodynamic reversible heat as `-I*T*dUocv/dT`. If integrating such a
material table or energy convention, change the entropic heat sign explicitly;
do not assume that a signed heat coefficient and an OCV derivative are
interchangeable. No chemistry-specific entropy convention is silently inferred.

Four illustrative Hatchard/Spotnitz-style stages are SEI consumption, anode /
solvent consumption, autocatalytic positive-electrode conversion, and electrolyte
consumption. The Patankar updates are

```
c_new = c_old / (1 + dt*k(T_new))
alpha_new = (alpha_old + dt*k(T_new)*alpha_old)
            / (1 + dt*k(T_new)*alpha_old)
```

`implicit_abuse` solves the coupled future-temperature closure by bisection inside
the available chemical-energy budget. Every heat increment uses the actual
reaction extent, not an unbounded explicit rate. Reaction bounds and heat closure
hold even for large dt. These are first-order, operator-split updates; positivity
does NOT imply time accuracy. Nonlinear runaway can have multiple implicit roots;
use time refinement to resolve the onset, not a single enormous step.

`battery_step` applies per-cell sources directly on SoA state, then solves the
3D FVM thermal diffusion equation implicitly. A Robin sink represents the coolant
heat-transfer boundary. The reference does not solve a separate coolant Navier-
Stokes domain, venting, an internal short circuit, or chemistry-specific gas flow.
The browser uses a transparent 48-zone conductance-network cooling approximation.

## FWH / Curle and Spectra

`farassat1a` includes thickness and loading far/near-field terms, source velocity,
acceleration, normal-rate, surface-area-rate, density-rate, fluid acceleration,
pressure-rate, and optional viscous traction. Solid and permeable source terms are

```
Q = rho0*vn + rho*(un-vn)      [second term only for permeable surfaces]
L = p*n - viscous_traction + rho*u*(un-vn)
```

Pressure is gauge pressure relative to the acoustic reference atmosphere. Source
derivatives must be provided in source time, consistently with geometry motion.
The formulation uses `D = 1 - M dot rhat`; it is positive for a subsonic surface,
so it equals the requested absolute Doppler factor. Sonic/supersonic motion and
an observer lying on a patch are explicitly rejected, not numerically clipped.

`FwhHistory::observer` finds retarded time independently for each quadrature
patch by monotone arrival-time bisection and linearly interpolates history.
Surface time histories must cover the full retarded interval. Out-of-history
contributions are zero; pad the record and crop receiver signals before spectral
analysis. `fwh_patches` extracts arbitrary selected owner-oriented face geometry
and pressure/pressure-rate arrays from the FVM data lake. Set fluid/motion fields
for a permeable/moving surface. Quadrupole volume noise, propagation through a
nonuniform medium, and acoustic scattering are outside this surface-source model.

The FFT is an in-place complex radix-2 Cooley-Tukey implementation. The analyzer
removes the mean and uses a symmetric Hann window. The one-sided PSD is

```
S[k] = endpoint_factor * |FFT(w*(p-mean))|^2 / (fs*sum(w^2))
```

DC and Nyquist are not doubled. Integrate PSD with `df = fs/N`. 1/3-octave bands
use base-2 exact centers and half-open band edges. OASPL is unweighted,
20 log10(time-domain RMS / 20e-6). Finite-record Hann PSD integration is a
window-weighted variance estimate, not exactly the unwindowed variance for every
nonstationary signal. The browser source is a stationary harmonic patch, not a
CFD-generated rotor calculation or a quantitative 3D wavefield solution.

Only octave bands whose complete upper edge lies at or below Nyquist are
reported. Narrowband logarithmic PSD is dB/Hz relative to (20 microPa)^2/Hz;
integrated band levels and OASPL are dB relative to 20 microPa.

## Low-Re_m MHD

`electric_projection` performs four explicit computational stages: build the
motional EMF; solve potential; reconstruct the conservative face current with
the same non-orthogonal stencil; reconstruct cell current by face moments and
compute J cross B and |J|^2/sigma. Boundary conditions can be specified through
the C++ API; `cluster_c_mhd_project` uses electrically insulating boundaries.
`mhd_momentum_step` adds a body-force predictor, Lorentz update, and a conservative
incompressible face-flux projection. A hydrodynamic viscous/advection predictor
can be supplied through the acceleration field. Projected face velocity flux,
not reinterpolated cell velocity, must drive continuity and transport.

`mhd_thermal_step` additionally inserts `joule_heat * cell_volume` into the shared
implicit FVM energy equation along with any other supplied cell heat in watts.
The verification suite checks the resulting integrated thermal-energy increment.

This is a Ni-style current-conservative reference, not an assertion of a complete
production implementation of every term in Ni's published four-step flow scheme.
The Lorentz time update is explicit and therefore has a magnetic damping
stability/accuracy restriction, approximately dt < 2*rho/(sigma*|B|^2) for the
local zero-potential case. A production high-Ha time integrator needs implicit
electromagnetic feedback. Electric potential itself is solved elliptically.

The current moment formula conserves charge when the face-flux solve converges.
For valid planar polyhedra it exactly reproduces constant currents. It does NOT,
on its own, guarantee exact Hartmann layer balance on arbitrary collocated meshes.
Boundary-layer resolution, consistent pressure/Lorentz discretization, and
hydrodynamic coupling remain necessary.

The Hartmann reference test independently discretizes the fully developed channel
equation with no-slip walls. It compares numerical and analytical normalized
profiles for Ha = 1,10,25,50,100, and checks second-order grid convergence. This
is not presented as an end-to-end 3D Hartmann simulation validation.

## TPS, Pyrolysis, and ALE

`pyrolysis` advances a positive virgin-to-char density with Arrhenius kinetics,
reaction order n >= 1, and a Patankar-diagonalized frozen nonlinear factor.
`pyrolysis_sources` applies the volumetric gas and endothermic formation sources
on arbitrary 3D SoA cell arrays. `multistage_pyrolysis` and the C-linkage
`cluster_c_pyrolysis_stages` advance multiple constituent-density spans, each with
its own A, E, order, Gamma, char density, and reaction enthalpy. Their gas/enthalpy
increments are summed, and an optional inert contribution in bulk density is
preserved. Constituent density must be expressed per unit bulk volume, and the
initial bulk density must include all constituents and any inert material.

`pyrolysis_gas_projection` solves quasi-steady Darcy continuity on the polyhedral
topology, with mass-conservative face fluxes, supplied permeability, gas density,
viscosity, and pressure boundary conditions. `gas_enthalpy_outflow` is conservative
upwind transport relative to a specified enthalpy reference. `porous_solid_step`
couples the single-stage SoA source, Darcy blowing, gas sensible-enthalpy transport,
and implicit FVM solid-energy solve in 3D on a fixed mesh. The source, gas
properties, and solid heat capacity are frozen during the split step. This is not
a fully compressible multicomponent gas/species model. Net wall heat supplied to
this function must use the same enthalpy convention and must not double-count
the gas enthalpy already carried by its boundary flux.

`wall_balance` brackets and solves

```
q_incident = k*(Tw-Tinterior)/distance
           + epsilon*sigmaSB*(Tw^4-Tinf^4)
           + m_gas*h_pyrolysis + m_char*L_sublimation
```

The energy partition uses illustrative effective enthalpies and a kinetic char
mass-loss law. Recession velocity is `(m_char/rho_wall)*n_material`, with the
normal explicitly chosen into the remaining material. Reversing a mesh's normal
convention requires reversing this sign.

`TpsColumn` is a one-dimensional moving-mesh demonstration: implicit conduction,
Arrhenius solid conversion, conservative upwind gas enthalpy transport, a nonlinear
wall solve, and ALE recession. The first mesh step uses Euler and subsequent
node positions use BDF2. It terminates before the solid is consumed/inverted.
The moving-mesh column is distinct from the 3D fixed-mesh porous-solid kernel.
They are not presented as an already-validated monolithic 3D ALE TPS solver.

For a signed swept face volume s:

```
G_mesh = (3*s[n->n+1] - s[n-1->n]) / (2*dt)
sum(outward G_mesh) = (3*V[n+1] - 4*V[n] + V[n-1]) / (2*dt)
```

`enforce_dgcl` projects geometric mesh fluxes on a generic FVM topology to enforce
this cellwise identity while preserving specified boundary motion. The calling
3D mesh-motion code must supply actual polyhedral swept volumes and valid updated
geometry. The projection utility does not invent unknown face-vertex connectivity.

When ordered face-vertex connectivity IS supplied, `CAbiPolyConnectivity` contains
cell count, owner/neighbor spans, face offsets, and a flat face-node index span.
Node coordinates and velocities remain SoA arrays. `move_ale_nodes` and the C ABI
`cluster_c_ale_nodes` implement BDF2 nodal motion. `polyhedral_ale_flux` integrates
actual moving triangular surface elements: vertex trajectories are linear in
each interval, so the area vector is quadratic and Simpson swept-volume
quadrature is exact. Two consecutive swept volumes form the BDF2 mesh flux.
`rebuild_polyhedral_geometry` recomputes oriented volumes, cell and face centroids,
face area vectors, over-relaxed alpha, and correction vectors. The C entry point
`cluster_c_ale_geometry` writes the new geometry and BDF2 face flux directly into
the supplied spans. Polygon vertex order must give the outward owner normal.
Reconstructed FVM faces must be planar; warped faces are rejected and must be
triangulated. The caller must propagate boundary recession velocities to interior
nodes and enforce mesh quality/remeshing policy.

The test suite checks exact 3D swept-volume DGCL on a deforming tetrahedron and
the constant-current moment identity after rebuilding the ALE geometry. These
are geometry/operator tests, not validation of an entire moving 3D TPS trajectory.
The browser TPS uses a fixed mesh and reports an integrated recession diagnostic.

## Verification Scope

The executable checks closed-form circuit integration, source positivity,
reaction-energy closure, conservative diffusion, stationary Curle and Doppler
limits, retarded time, FFT normalization, OASPL, the Hartmann profile and
convergence rate, skew-cell current moments, a manufactured non-orthogonal
potential, discrete charge balance, Lorentz/Joule work, pyrolysis bounds,
multi-stage mass loss, 3D porous blowing, conservative gas enthalpy, surface energy
closure, BDF2 DGCL, exact moving-polyhedron swept volumes, and uniform-state ALE
preservation.

These checks are numerical evidence for specific invariants and reduced test
problems. They are not a proof of all coupled nonlinear solutions and not
experimental validation. Refinement studies, calibrated materials, real mesh
integration, coolant coupling, and domain-specific validation are still required
before engineering use.

## References

- Farassat, F., Derivation of Formulations 1 and 1A of Farassat,
  NASA/TM-2007-214853: https://ntrs.nasa.gov/citations/20070010579
- Ni et al., A current density conservative scheme for incompressible MHD flows
  at a low magnetic Reynolds number, Journal of Computational Physics 227 (2007).
  https://bpb-us-w2.wpmucdn.com/research.seas.ucla.edu/dist/d/39/files/2019/08/JCP-v227-NiCurrentPart1.pdf
- Modeling and Simulation of the Thermal Runaway Behavior of Cylindrical Li-Ion
  Cells, Energies 9(4), 292 (2016): https://doi.org/10.3390/en9040292

Parameters are illustrative; references do not imply author endorsement or
experimental validation of this implementation.
