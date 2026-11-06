# Project: Industrial In-Memory Multiphysics Mesher Backbone — Phase 2: Work Package 4

## Architecture & Data Flow
Phase 2 implements Work Package 4 (WP4: Echte 3D-Fluidgebiets- & Mehrbereichs-Vernetzung / Multiphysics Domain Meshing & GCI-Hierarchie) of the CFD meshing subsystem across Python and C++20 according to the Zero-Shortcut Policy, focusing on:
1. **Domain Modes & Farfield Patches (R1)**: Explicit `domain_mode: Literal["external_fluid", "internal_fluid", "solid_only", "cht_coupled"] = "external_fluid"`, configurable `farfield_padding` (5L upstream, 10L downstream, 5L lateral) and `farfield_bbox`, automated boundary patch naming (`inlet`, `outlet`, `bottom`, `top`, `sides`, and surface patches), complete elimination of `len(verts) == 8` and millimeter/resolution clamps.
2. **3D Mosaic Poly-Hexcore & Octree (R2)**: 2:1-balanced adaptive Cartesian octree in fluid domain ($\phi(\mathbf{x}) > 0$), conformal polyhedral transition interface without hanging nodes (ANSYS Mosaic / PUMA principle: splitting quad face into 4 coplanar sub-faces on unrefined neighbor cells yielding `CellType::POLYHEDRON = 42` in DiFVM face-graph).
3. **Cut-Cell Snapping, Small-Cell Merging & WSRD (R3)**: AMIPS-guarded surface snapping along $\nabla\phi$, topological agglomeration of small cut-cells ($V_{\text{cut}} < 0.20 V_{\text{bg}}$) into adjacent fluid cells, Berger & Giuliani (2024) WSRD weights $w_{ij} = \frac{1}{N_i - 1}(1 - \alpha_j / \alpha_{\text{target}})$ for $0.20 \le \alpha_i < 0.50$.
4. **Multiphysics CHT Coupling & GCI Hierarchy (R4)**: Multi-region cell tagging (`cell_regions`), CHT twin-face boundary extraction (`coupledWall_fluid`, `coupledWall_solid`) with opposite normals $\mathbf{S}_{f, \text{solid}} = -\mathbf{S}_{f, \text{fluid}}$, deterministic 3-grid GCI hierarchy ($r \approx 1.3\text{--}1.5$, $\text{GCI} < 1\%$).
5. **Resource Directives & Zero-Shortcut Guard (R5)**: Strict 16-thread limit (`OMP_NUM_THREADS=16`), RAM-safe execution protocol (moderate unit test mesh sizes $N \le 5{,}000$), all 579 existing regression tests 100% green, and independent 3-agent audit committee for Gate WP4.

### Data Flow
```
[CAD STEP / STL / Analytical Geometry / SurfaceMesh]
                         │
                         ├──> [MeshSDF Level-Set & Gradient ∇ϕ]
                         │
                         ▼
        [MeshingConfig: Domain Mode & Farfield]
        ├── external_fluid: Farfield Bounding Box & Inlet/Outlet/Bottom/Top/Sides Patches
        ├── internal_fluid / solid_only: Domain boundary clipping
        └── cht_coupled: Multi-region cell tagging & surface extraction
                         │
                         ▼
        [3D 2:1 Balanced Octree Grid Engine (C++20)]
        ├── Spatial Octree Refinement based on h(x)
        └── Strict 2:1 balancing in all 26 directions
                         │
                         ▼
   [ANSYS Mosaic / PUMA Polyhedral Transition Interface]
   ├── Quad face split into 4 coplanar sub-faces on unrefined cell
   ├── CellType::POLYHEDRON = 42 for transition cells (zero hanging nodes)
   └── Relative Gauss-closure ||∑ S_f|| / ∑ ||S_f|| < 10^-11
                         │
                         ▼
   [Cut-Cell Snapping, Merging & Berger-Giuliani WSRD]
   ├── Snapping along ∇ϕ guarded by E_AMIPS < 10^4 (strictly V_i > 0)
   ├── Small-cell agglomeration (V_cut < 0.20 V_bg) along ∇ϕ
   └── WSRD weights w_ij = 1/(N_i - 1) * (1 - α_j / α_target) for 0.20 <= α_i < 0.50
                         │
                         ▼
   [Multiphysics CHT & Automated 3-Grid GCI Hierarchy]
   ├── CHT twin-faces extraction (coupledWall patches with S_f,B = -S_f,A)
   └── Deterministic coarse/medium/fine 3-grid generation (r in [1.3, 1.5], GCI < 1%)
                         │
                         ▼
         [DiFVMMesh & OpenFOAM Export / Validation]
```

## Feature Inventory
| # | Feature | Description | Milestone | Source |
|---|---|---|---|---|
| F16 | Domain Modes & Farfield Configuration | `domain_mode: Literal["external_fluid", "internal_fluid", "solid_only", "cht_coupled"] = "external_fluid"`, `FarfieldPadding` (5L upstream, 10L downstream, 5L lateral), `farfield_bbox`. Complete removal of `len(verts) == 8` and `max_res = 100` heuristics. | M4.1 | ORIGINAL_REQUEST R1 |
| F17 | Automated Farfield & Body Boundary Patches | Auto-naming `inlet` ($x_{\min}$), `outlet` ($x_{\max}$), `bottom` ($z_{\min}$), `top` ($z_{\max}$), `sides` ($y_{\min}, y_{\max}$), plus inheritance of `SurfaceMesh.patches` (or default `wall`). | M4.1 | ORIGINAL_REQUEST R1 |
| F18 | 3D 2:1 Balanced Octree Grid Engine | Adaptive Cartesian Octree with 2:1 balancing in all 26 directions in fluid domain ($\phi(\mathbf{x}) > 0$). | M4.2 | ORIGINAL_REQUEST R2 |
| F19 | ANSYS Mosaic / PUMA Polyhedral Transitions | Quad face on unrefined neighbor replaced with 4 coplanar sub-faces; `CellType::POLYHEDRON = 42` in DiFVM; zero hanging nodes; Gauss closure $< 10^{-11}$. | M4.2 | ORIGINAL_REQUEST R2 |
| F20 | Cut-Cell Snapping & AMIPS Optimization | Surface conformity clipping and node snapping along $\nabla\phi$ under $E_{\text{AMIPS}} < 10^4$; strictly $V_i > 0$. | M4.2 | ORIGINAL_REQUEST R3 |
| F21 | Small-Cell Merging & Berger-Giuliani WSRD | Topologically agglomerate cut-cells with $V_{\text{cut}} < 0.20 V_{\text{bg}}$ into neighbor along $\nabla\phi$; compute Berger-Giuliani (2024) WSRD weights $w_{ij}$ for $0.20 \le \alpha_i < 0.50$. | M4.2 | ORIGINAL_REQUEST R3 |
| F22 | Multiphysics CHT Multi-Region Coupling | `cell_regions` integer tagging; conformal twin-faces extraction at fluid-solid interfaces (`coupledWall_fluid`, `coupledWall_solid`) with opposite normals and contiguous ordering. | M4.3 | ORIGINAL_REQUEST R4 |
| F23 | Automated 3-Grid GCI Hierarchy Generation | Deterministic generation of 3 grid levels (coarse, medium, fine with $r \in [1.3, 1.5]$) preserving boundary patches and topology; automated GCI calculation ($\text{GCI} < 1\%$). | M4.3 | ORIGINAL_REQUEST R4 |
| F24 | Dual-Track Test Suite `test_wp4_multiphysics_hexcore.py` | Complete test suite covering R1–R4 and CAD benchmarks (`TestCAD.stp`, `BladeFullRev1CFD.stp` with scale=0.001, `snappyMultiRegionHeater`) under 16-thread & RAM-safe protocol. All 579 existing tests pass. | M4.4 | ORIGINAL_REQUEST R5 |
| F25 | Independent 3-Agent Audit Committee for Gate WP4 | Unanimous 3:0 sign-off by Paper-Fidelity Challenger, Code & Software-Architecture Auditor, and Mathematics & Numerics Auditor. | M4.5 | ORIGINAL_REQUEST Gate WP4 |

## Milestones
| # | Name | Scope | Dependencies | Status |
|---|---|---|---|---|
| M4.1 | R1: Domain Modes & Farfield Configuration | Features F16, F17 (`models.py`, `mesh_engine.py`, `difvm_topology.py`) | none | DONE |
| M4.2 | R2 & R3: 3D Mosaic Poly-Hexcore, Snapping & Merging | Features F18, F19, F20, F21 (`octree_hexcore.hpp/cpp`, `cfd_mesher_native.h/cpp`, `difvm_mesh.cpp`, `mesh_engine.py`) | M4.1 | DONE |
| M4.3 | R4: Multiphysics CHT & Automated GCI Hierarchy | Features F22, F23 (`mesh_engine.py`, `difvm_topology.py`, `gci.py`) | M4.1, M4.2 | IN_PROGRESS |
| M4.4 | Dual-Track E2E Test Suite & CAD Benchmarks | Feature F24 (`TEST_INFRA.md`, `TEST_READY.md`, `tests/test_wp4_multiphysics_hexcore.py`) | M4.1, M4.2, M4.3 | DONE (TEST_READY) |
| M4.5 | Gate WP4 Independent 3-Agent Audit Committee | Feature F25 (Independent 3-Agent Audit Committee: Challenger, Code Auditor, Math Auditor) | M4.4 | PLANNED |

## Interface Contracts

### `backend/meshing/models.py`
```python
from dataclasses import dataclass, field
from typing import Literal, Optional, Tuple, List, Dict, Any
import numpy as np

@dataclass
class FarfieldPadding:
    upstream: float = 5.0     # 5.0 * L_ref along -X (inlet)
    downstream: float = 10.0  # 10.0 * L_ref along +X (outlet)
    lateral_y: float = 5.0    # 5.0 * L_ref along +/-Y (sides)
    lateral_z: float = 5.0    # 5.0 * L_ref along +/-Z (bottom/top)

@dataclass
class MeshingConfig:
    domain_mode: Literal["external_fluid", "internal_fluid", "solid_only", "cht_coupled"] = "external_fluid"
    farfield_padding: Optional[FarfieldPadding] = None
    farfield_bbox: Optional[Tuple[Tuple[float, float, float], Tuple[float, float, float]]] = None
    small_cell_ratio: float = 0.20
    wsrd_target_alpha: float = 0.50
    gci_refinement_ratio: float = 1.41421356  # sqrt(2) in [1.3, 1.5]
    # Existing sizing and optimization fields preserved...
```

### `backend/meshing/native/include/octree_hexcore.hpp`
```cpp
#pragma once
#include "difvm_mesh.hpp"
#include <vector>
#include <array>

namespace cfd {

struct OctreeNode {
  std::array<double, 3> min_pt;
  std::array<double, 3> max_pt;
  int level{0};
  bool is_leaf{true};
  int children[8]{-1, -1, -1, -1, -1, -1, -1, -1};
  int cell_index{-1};
};

class OctreeHexcore {
 public:
  explicit OctreeHexcore(const std::array<double, 3>& min_b,
                         const std::array<double, 3>& max_b,
                         double target_h0);
  void balance_2to1();
  NativeMeshData generate_mosaic_mesh(void* sdf_ptr, double small_cell_ratio, double wsrd_target);
};

} // namespace cfd
```

## Code Layout & Exclusive File Ownership
- **Milestone M4.1 Worker owns:**
  - `backend/meshing/models.py`
  - `backend/meshing/mesh_engine.py` (domain mode, farfield padding, patch assignment)
  - `backend/meshing/difvm_topology.py` (patch mapping)
- **Milestone M4.2 Worker owns:**
  - `backend/meshing/native/include/octree_hexcore.hpp`
  - `backend/meshing/native/src/octree_hexcore.cpp`
  - `backend/meshing/native/include/cfd_mesher_native.h`
  - `backend/meshing/native/src/cfd_mesher_native.cpp`
  - `backend/meshing/native/src/difvm_mesh.cpp`
  - `backend/meshing/native/src/distmesh.cpp` (removal of clamps and stub replacement)
  - `backend/meshing/native/CMakeLists.txt`
- **Milestone M4.3 Worker owns:**
  - `backend/meshing/gci.py` (ASME V&V 20 GCI calculation & hierarchy generator)
  - `backend/meshing/difvm_topology.py` (CHT twin-face multi-region integration)
- **Milestone M4.4 Test Writer owns:**
  - `TEST_INFRA.md`
  - `TEST_READY.md`
  - `tests/test_wp4_multiphysics_hexcore.py`

## Mandatory Gate & Audit Rules
1. **Unanimous 3-Agent Audit Committee**: For Gate WP4, an independent 3-agent committee is convened:
   - Agent 1: Paper-Fidelity & Zero-Shortcut Challenger (`teamwork_challenger`)
   - Agent 2: Code & Software-Architecture Auditor (`teamwork_auditor`)
   - Agent 3: Mathematics & Numerics Auditor (`teamwork_critic`)
   100% UNANIMOUS approval (3:0 APPROVE) is strictly required to pass Gate WP4.
2. **Zero-Shortcut Policy**: No dummy stubs, no magic numbers, no heuristics (zero occurrences of `len(verts) == 8` and resolution clamps), no silent fallbacks (`except: pass`).
3. **16-Thread Limit & RAM Protection**: `OMP_NUM_THREADS <= 16`, moderate mesh sizes in unit tests ($N \le 5{,}000$), immediate buffer cleanup via RAII / `cfd_mesher_free` to protect concurrent Ansys Fluent execution.
4. **Physical Invariants**: Strict $V_i > 0$, relative Gauss divergence closure $< 10^{-11}$, true projection identity $< 10^{-12}$.
5. **Regression Baseline**: All 579 existing tests in `tests/` and `backend/geometry/tests/` must remain 100% green.
