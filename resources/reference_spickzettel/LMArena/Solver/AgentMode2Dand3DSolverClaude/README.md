# Fieldline

A C++20 base for in-memory, coupled thermoelastic finite-element simulation,
with a React workbench that visualizes results produced by the native solver.

## Native solver

The reusable implementation is in `cpp/include/fieldline/solver.hpp`. It
assembles transient heat conduction and plane-stress elasticity on an existing
2D triangular mesh, then advances heat and solves the thermal deformation.
It includes CSR operators, symmetric Dirichlet elimination, IC(0)/Jacobi
preconditioned conjugate gradients, optional OpenMP, and numerical tests.

```sh
cmake -S cpp -B cpp/build -DCMAKE_BUILD_TYPE=Release
cmake --build cpp/build -j
ctest --test-dir cpp/build --output-on-failure
```

See `cpp/README.md` for the API, units, numerical method, limitations, and a
usage example. The solver is one-way coupled thermoelasticity, not a general
nonlinear multiphysics framework.

## Workbench

```sh
npm install
npm run dev
npm run build
```

`cpp/examples/cantilever.cpp` generates the example mesh in memory and writes
the simulation snapshots used by the workbench:

```sh
./cpp/build/fieldline_cantilever > src/data/cantilever.json
```

The web controls explore and export those computed fields. Heat-input changes
use exact linear scaling of the reference solve; the browser does not execute
the C++ code. The downloadable C++ archive is `public/fieldline-cpp-source.zip`.