# Coupled Thermo–Hydro–Mechanical (THM) Multiphysics Engine

A monolithic finite-element engine for coupled poroelasticity: **quasi-static
solid mechanics + single-phase Darcy flow + heat transport**, with thermal
pressurisation, buoyancy, temperature-dependent fluid properties and advective
heat transport. Fortran 2018, OpenMP, no external libraries.

```
src/mod_kinds.f90     kind parameters (wp = double precision)
src/mod_util.f90      wall clock, logging, error handling
src/mod_mesh.f90      hex-8 mesh + compressed face topology, on-the-fly geometry
src/mod_linalg.f90    block-CSR assembly, equilibration, ILU(0)/Jacobi,
                      GMRES(m) / BiCGSTAB / CG
src/mod_layout.f90    node-blocked dof layout, slot maps, greedy colouring
src/mod_physics.f90   element residual + analytic Jacobian
src/mod_solver.f90    assembly, Newton, time stepping, linear solve
app/thm_bench.f90     benchmark / demonstration driver
tests/test_vnv.f90    21 verification checks
```

## Build and run

```sh
make                # optimised (-O3 -march=native -fopenmp)
make debug          # bounds/alias checked, -fcheck=all -g
make test           # verification suite
make run            # benchmark, N=16
./build/thm_bench --n 24 --no-bench      # single run, 24^3 mesh
```

`--n N` mesh resolution · `--precond 0|1|2` none/block-Jacobi/block-ILU(0) ·
`--no-bench` single case · `--decoupled`, `--lin-fluid`, `--no-gravity`,
`--no-alpha`, `--no-advect` physics bisection switches · `--help`.

## Formulation

Unknowns per node (5): `u_x, u_y, u_z, p, T`. Weak form, implicitly
time-discretised with variable-step BDF1/BDF2:

| block | terms |
|---|---|
| momentum | `σ = C:ε − α_B p I − α_T (T−T₀) C:I`, gravity body force |
| fluid mass | `α_B ∂(∇·u)/∂t + (1/M) ∂p/∂t + ∇·v = f`, `v = −(K/μ)(∇p − ρ_f g)` |
| energy | `ρc_p ∂T/∂t + ρ_f c_f v·∇T − ∇·(λ∇T) = q` |

`μ_f(T) = μ₀ exp(1500(1/T − 1/T₀))`, `ρ_f(T) = ρ₀(1 − β_f (T − T₀))`.

The Jacobian is **analytic** — no finite differences, no numerical
linearisation anywhere in the solver. It agrees with a column-by-column finite
difference of the residual to `4.0e-16` (relative to the element matrix norm),
on a single element, a graded mesh and a realistic coupled state.

## Solver

* **Monolithic** 5-dof/node Newton with a backtracking line search
  (residual norm measured on the *free* dofs only — see below).
* **Block-CSR** storage: one dense 5×5 block per (node, neighbour) pair, so the
  element→global scatter is a `5×5` rank-1-free update and matvec streams
  `25 × nnzb` doubles.
* **Symmetric per-dof diagonal equilibration** (`A → S A S`) — essential here:
  the raw Jacobian spans ~17 decades (elasticity 1e9, pressure storage 1e-12).
* **Block-ILU(0)** with internally rescaled 5×5 pivots, falling back to
  block-Jacobi if a pivot set is ill-conditioned.
* **GMRES(40)** right-preconditioned by default; BiCGSTAB and CG selectable.
* **Iterative refinement** on the true residual, so the accuracy guarantee does
  not depend on the preconditioner's quality.
* Thread-coloured race-free OpenMP assembly.

## Verification (`make test`, 21/21)

| check | result |
|---|---|
| total / per-cell / distorted mesh volume | 2.4e-15, 3.3e-16, 1.8e-15 |
| constant-strain patch on graded mesh | 8.5e-14 |
| analytic Jacobian vs finite differences | 6.1e-11 |
| graph colouring, slot maps, symmetry | exact |
| block-Jacobi / block-ILU(0) / CG solution error | 2.7e-14 / 9.8e-15 / 1.4e-14 |
| ILU(0) iteration reduction | 10× fewer than unpreconditioned |
| coupled 5-field solve (converged, scaled residual) | 1.8e-12 |
| reproducibility across repeated solves | 0.0 |
| storage residual of a uniform state | 1.5e-17 |
| global Darcy flux balance | 2.1e-15 |
| spatial order of `p` (MMS) | **2.006 / 2.001** |
| temporal order of BDF2 | **2.338 / 2.076 / 2.018** |

The whole suite also runs clean under `-fcheck=all -fbacktrace`.

## Benchmark: forced convection + thermal pressurisation

Unit cube, 5 dof/node, clamped base, drained / fixed-`T` outer boundary,
injection patch. Rock: `E = 5 GPa, ν = 0.25, α_B = 0.7, K = 10 mD, M = 1 GPa`,
`λ_t = 2.5, ρc_p = 2.4e6`.

| mesh | cells | dofs | Newton its | Krylov its | solve time |
|---|---|---|---|---|---|
| 8³  | 512   | 3 645   | 42 | 5 859  | 1.8 s |
| 16³ | 4 096 | 24 565  | 16 | 10 233 | 17.2 s |
| 24³ | 13 824| 78 125  | 15 | 14 800 | 99.7 s |
| 32³ | 32 768| 179 685 | 14 | 14 400 | 479 s |

Assembly throughput is flat at **0.25–0.28 M element-Jacobians/s** (≈ 2 M
scalar entries/s) across a 64× range of mesh sizes — i.e. the element kernel,
not the data structures, is the limit. The Krylov count saturates at ≈1000
iterations per Newton step, which is the remaining performance bottleneck:
block-ILU(0) does not capture the long-wavelength pressure/elastic coupling, so
a physics-based block preconditioner (algebraic multigrid on the pressure block,
or a Schur-complement approximation) is the obvious next step and is *not*
implemented here.

## Engineering notes (bugs found and fixed during development)

These are recorded because each one is subtle, silent, and cost real debugging
time — they are the kind that reappear.

1. **Newton norm must exclude Dirichlet rows.** The residual at a constrained
   dof is the *reaction force*, not an error; it never goes to zero. Including
   it made the norm impossible to decrease, so the line search rejected every
   step and healthy solves "failed to converge".
2. **Duplicate BC entries.** Geometric BC predicates overlap (a patch touching a
   boundary, a corner on two faces). Two values on one dof mean the constraint
   violation can never be zero. BCs are now deduplicated at init, last-wins.
3. **Viscosity-coupling terms missing the mobility.** `∂v/∂T` through
   `d(1/μ)/dT` must carry `K/μ`, not `1/μ²` — an error of `1/K = 10¹⁴` that
   left the linear solve converging to a garbage step.
4. **Elasticity Jacobian.** `∂σᵢⱼ/∂uₖ = λ δᵢⱼ ∂N/∂xₖ + μ(δᵢₖ ∂N/∂xⱼ + δⱼₖ ∂N/∂xᵢ)`
   requires *component-wise* gradients; the tempting `∇N_a·∇N_b` shorthand is
   only correct in 1-D.
5. **GMRES lucky-breakdown test.** `H(j+1,j)` is zeroed when forming `R`; testing
   it for breakdown makes GMRES report convergence after exactly one iteration.
   The saved `hj1` must be tested instead.
6. **Fortran does not short-circuit `.or.`.** `if (k == p .or. a(k) /= a(k-1))`
   reads out of bounds on the first entry. Invisible at `-O3`, trapped by
   `-fcheck=all`.
7. **Relative-only convergence criteria fail on an already-converged state.**
   If the initial residual is at round-off, `tol·|R₀|` is unreachable. Pair every
   relative tolerance with an absolute floor, an increment test and a stagnation
   test.
8. **Unscaled residual is meaningless for a 17-decade matrix.** `cond(A)` is
   ~1e18 in raw units, so no Krylov method can produce a small true residual
   there. The meaningful, basis-independent criterion is the residual of the
   *equilibrated* system.
9. **Test matrices must be well posed.** Pinning all three displacement
   components everywhere over-constrains the problem and produces
   `cond ~ 1e22`; that was originally mistaken for a solver defect.
