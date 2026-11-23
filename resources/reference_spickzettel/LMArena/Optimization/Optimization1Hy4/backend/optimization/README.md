# Phase 4 — standalone C++20 MDO engine

Real, dependency-free implementations (g++ -std=c++20 -O2) behind a JSON-producing CLI:

```
make
./build/mdo_engine verify  nx=9 ny=8 samples=2500
./build/mdo_engine optimize nx=12 ny=9 iters=60 beta=8 volFrac=0.4
./build/mdo_engine fosm    nx=10 ny=8 samples=4000 kappa=2
./build/mdo_engine revolve steps=64 slots=5
./build/mdo_engine shape   islands=5 pop=14 generations=45 dim=4
```

## What is implemented

* `include/mdo_core.hpp` — unstructured FVM cell graph; sparse SPD Helmholtz operator
  `A = I + (r_min/(2√3))² L`; Jacobi PCG; the complete chain
  `γ → γ̃ → γ̄ → ρ` with RAMP/SIMP regularization, volume-preserving Heaviside
  (β_H continuation) and the **exact** reverse VJP
  `A⁻ᵀ((∂γ̄/∂γ̃) ⊙ dJ/dγ̄)`; overflow-safe shifted KS aggregation with softmax gradient;
  symmetric thermo-fluid state `B(ρ,ξ)T=f` with edgewise analytic adjoints.
* `include/mdo_optim.hpp` — Svanberg MMA (asymptote update with asyminit/dec/inc,
  p_ij/q_ij convex separability, closed-form primal minimizer and log-barrier
  primal-dual Newton SolveDIP); optimal binomial Revolve checkpointing (DP cost and
  recursive SAVE/RESTORE/FORWARD/ADJOINT trace); generalized FOSM robustness with
  spatial exponential covariance, `Φ_g = μ_g + κσ_g`, and the exact variance
  sensitivity from two adjoint solves; MIGA (ring migration, NSGA-II rank/crowding,
  SBX, polynomial mutation) + PSO with warm-start Nelder-Mead and two-loop L-BFGS.
* `src/main.cpp` — CLI and verification harness. `verify` checks the whole filter
  chain against **complex-step** (h = 1e-30) and central differences (<1e-11 target),
  KS against complex-step, FOSM variance gradient against common-random-number Monte
  Carlo, and Revolve forward/adjoint accounting.

The engine is invoked by the Next.js cockpit through `src/app/api/engine/route.ts`;
all parameters come from the interactive UI, nothing is hard-coded in the browser.
