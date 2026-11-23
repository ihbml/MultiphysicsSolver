# Cluster A reference kernels

This directory contains the portable C++20 reference kernels, analytical verification cases, and an optional CUDA backend for the Cluster A multiphysics closures. The header is solver-agnostic: map cell data from the Hilbert-ordered `CellFaceCSR`/SoA storage into these closure calls, then assemble the returned Patankar terms into the host or device matrix.

## Verify the CPU reference

```sh
g++ -std=c++20 -O2 -Wall -Wextra -Wpedantic -Icpp/include \
  cpp/src/verification.cpp -o /tmp/cluster-a-verify
/tmp/cluster-a-verify
```

The executable checks polyhedral least-squares reconstruction, SST blending and positive source linearization, SA and WALE closures, all-y+ wall behavior, an analytical convection/fluid/solid/convection resistance chain, monolithic and partitioned twin-face conservation, rotated anisotropic Darcy-Forchheimer drag, and P1/Marshak terms.

## Optional CUDA backend

The `.cu` translation unit provides an atomic-free CSR face gather and device kernels for SST closure/Patankar sources, Spalart-Allmaras, WALE viscosity, all-y+ walls, mapped twin-face CHT flux, anisotropic porous momentum, and P1/Marshak radiation. Build it with a CUDA toolkit using the host compiler supported by that toolkit:

```sh
nvcc -std=c++20 -O2 -Icpp/include -c cpp/cuda/cluster_a_kernels.cu -o cluster_a_kernels.o
```

CUDA compilation is not part of the CPU reference test. `nvcc` was not available in the implementation environment, so this translation unit still needs verification on a CUDA-enabled toolchain before deployment.

## Integration notes

- `sstPatankarSources` returns cell-volume-integrated positive RHS and diagonal increments; add them to the scalar equation before solving. It keeps `k` and `omega` floors at closure evaluation and moves negative explicit sources to the diagonal.
- The requested omega diagonal coupling, `(betaStar + 2 beta) rho omega V`, is used, with a matching RHS correction so the destruction source is exact at the current iterate.
- `chtInterfaceFlux` uses `alpha = 1 / orthogonal distance`; its face-normal convention follows the supplied `(T_S - T_P)` expression. Twin-face assembly must apply the opposite signed flux to the other region.
- The matrix-free reference API does not replace flux interpolation, non-orthogonal correction iteration, boundary-condition scheduling, time integration, or the existing pressure-velocity algorithm. Those remain the host solver's responsibility.