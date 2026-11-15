# continuum

A local-first engineering notebook for CFD and multiphysics solver development.
Built with React, TypeScript, Vite, Tailwind CSS, Framer Motion and Prism.

## Run

- `npm install`
- `npm run dev`
- `npm run build`
- `npx tsc --noEmit`

## Features

- Twelve technical notes with linked primary sources.
- Twelve corrections to the supplied solver architecture discussion.
- Nine copyable and downloadable C++20, CUDA, MPI and PETSc snippets.
- A searchable directory of twelve open-source libraries.
- An illustrative, interactive MeshErrorMap surface visualization.
- Bookmarks, implementation progress and appearance saved in localStorage.
- Markdown notebook, individual source file and architecture exports.
- Keyboard search, browser-history routing and responsive navigation.

## Verify

Run browser tests with `npx playwright test --workers=2` after installing Chromium
with `npx playwright install chromium`. Some Linux environments also require
`npx playwright install-deps chromium`.

Run the independent numerical reference with:

`c++ -std=c++20 -O2 -Wall -Wextra -pedantic src/snippets/reference_test.cpp -o /tmp/continuum-test && /tmp/continuum-test`

The CPU reference checks constant and linear fields, paired flux conservation,
upwind coefficient signs, and fast/invalid geometry classification. The layout
header has also been compiled independently.

## Boundaries

This is a complete reference web application, not a CFD solver. CUDA kernels
are illustrative and have not been compiled with nvcc or executed on a GPU.
The MPI scheduling file is explicitly pseudocode. The mesh visualization uses
a synthetic surface quality field, not an actual volumetric discretization
error estimate. No performance improvements are guaranteed.

No remote account, simulation service or backend is required. Local preferences
are private to the current browser. Source links point to upstream projects;
version-specific support and licensing should be checked before integration.

DM Sans and JetBrains Mono are bundled locally under the SIL Open Font License.
Their license files are in `public/fonts/`.