"""ASME V&V 20-2009 Grid Convergence Index (GCI) & 3-Grid Hierarchy Engine.

Implements the standard numerical verification procedure according to:
- ASME V&V 20-2009: Standard for Verification and Validation in Computational
  Fluid Dynamics and Heat Transfer.
- Celik, Ghia, Roache, Freitas, Coleman, & Raad (2008): "Procedure for Estimation
  and Reporting of Uncertainty Due to Discretization in CFD Applications",
  ASME J. Fluids Eng. 130(7):078001.

Key capabilities:
1. `GCIResult`: Structured container for convergence metrics (apparent order p,
   Richardson extrapolated solution, approximate error e_a, fine and medium GCI,
   and asymptotic convergence ratio). Supports both attribute and dictionary access.
2. `compute_asme_vv20_gci`: Exact implementation of the 5-step ASME V&V 20 procedure.
3. `GCIHierarchy`: Deterministic 3-grid hierarchy container (fine, medium, coarse).
4. `generate_3grid_gci_hierarchy`: Automated generator and validator ensuring
   identical boundary patch sets, topological consistency, and strictly positive volumes.
"""

from __future__ import annotations

from dataclasses import asdict, dataclass
import logging
from typing import Any, Callable, Dict, Iterator, List, Optional, Tuple, Union
import warnings

import numpy as np

from backend.meshing.models import UnstructuredMesh

logger = logging.getLogger(__name__)


@dataclass
class GCIResult:
    """ASME V&V 20-2009 / Celik et al. (2008) Grid Convergence Index calculation results.

    Attributes:
        apparent_order_p: Observed apparent order of convergence p (nan on divergence).
        phi_extrapolated: Richardson extrapolated continuum solution phi_ext^21.
        approx_rel_error_fine: Approximate relative error on fine grid e_a^21 = |(phi1 - phi2) / phi1|.
        gci_fine: Fine grid convergence index GCI_21 = F_s * e_a^21 / (r21^p - 1) (inf on divergence).
        gci_medium: Medium grid convergence index GCI_32 = F_s * e_a^32 / (r32^p - 1).
        asymptotic_ratio: Range check ratio GCI_32 / (r21^p * GCI_21) (asymptotic if ~ 1.0).
        convergence_type: Convergence classification: 'monotonic', 'oscillatory', or 'divergence'.
    """

    apparent_order_p: float
    phi_extrapolated: float
    approx_rel_error_fine: float
    gci_fine: float
    gci_medium: float
    asymptotic_ratio: float
    convergence_type: str = "monotonic"

    def __getitem__(self, key: str) -> Any:
        """Allow dict-like indexing for backward compatibility with test suites."""
        if hasattr(self, key):
            return getattr(self, key)
        raise KeyError(f"GCIResult has no field '{key}'")

    def to_dict(self) -> Dict[str, Any]:
        """Convert result to dictionary of metrics."""
        return asdict(self)


@dataclass
class GCIHierarchy:
    """Deterministic 3-grid hierarchy container holding fine, medium, and coarse meshes.

    Provides tuple unpacking (fine, medium, coarse) and named attributes.
    """

    fine: UnstructuredMesh
    medium: UnstructuredMesh
    coarse: UnstructuredMesh
    r: float
    h1: float
    h2: float
    h3: float
    r21: Optional[float] = None
    r32: Optional[float] = None

    def __iter__(self) -> Iterator[UnstructuredMesh]:
        """Iterate in order: fine (Grid 1), medium (Grid 2), coarse (Grid 3)."""
        return iter((self.fine, self.medium, self.coarse))

    def __getitem__(self, index: int) -> UnstructuredMesh:
        """Access grid by index: 0=fine, 1=medium, 2=coarse."""
        return (self.fine, self.medium, self.coarse)[index]

    def __len__(self) -> int:
        return 3

    @property
    def mesh_fine(self) -> UnstructuredMesh:
        return self.fine

    @property
    def mesh_medium(self) -> UnstructuredMesh:
        return self.medium

    @property
    def mesh_coarse(self) -> UnstructuredMesh:
        return self.coarse


def compute_grid_representative_size(mesh: UnstructuredMesh) -> float:
    """Calculate representative cell size h according to ASME V&V 20-2009.

    Formula (Eq. 1):
        h = ( 1/N * sum_{i=1}^N V_i )^(1/3)
    """
    if mesh.num_cells == 0:
        raise ValueError("Cannot compute representative grid size for empty mesh.")
    avg_vol = float(np.mean(mesh.cell_volumes))
    if avg_vol <= 0.0:
        raise ValueError(f"Mesh has non-positive mean cell volume: {avg_vol}")
    topo = getattr(mesh, "topological_dim", 3)
    dim = max(1, int(topo)) if topo is not None else 3
    return float(avg_vol ** (1.0 / dim))


def compute_asme_vv20_gci(
    phi_1: float,
    phi_2: float,
    phi_3: float,
    r21: Optional[float] = None,
    r32: Optional[float] = None,
    f_s: float = 1.25,
    r: Optional[float] = None,
) -> GCIResult:
    """Calculate ASME V&V 20-2009 / Celik et al. (2008) Grid Convergence Index.

    Full formulation per Celik et al. (2008):
    1. Grid refinement ratios: r21 = h2 / h1 > 1, r32 = h3 / h2 > 1.
    2. Convergence ratio: R = eps_21 / eps_32 = (phi2 - phi1) / (phi3 - phi2).
       - Monotonic convergence: 0 < R < 1.
       - Oscillatory convergence: -1 < R < 0.
       - Divergence: |R| >= 1.0 (flags apparent_order_p=nan, gci_fine=inf).
    3. Apparent order p:
       - Uniform refinement (r21 == r32): p = ln|1/R| / ln(r21).
       - Non-uniform refinement (r21 != r32): fixed-point iteration with transcendental q(p).
    4. Exact Richardson extrapolation and GCI without artificial clamps.

    Args:
        phi_1: Numerical solution on fine grid (h1).
        phi_2: Numerical solution on medium grid (h2).
        phi_3: Numerical solution on coarse grid (h3).
        r21: Grid refinement ratio h2 / h1 > 1 (or uniform ratio r).
        r32: Grid refinement ratio h3 / h2 > 1 (defaults to r21 if omitted).
        f_s: Safety factor (1.25 for 3-grid study, 3.0 for 2-grid study).
        r: Backward-compatible keyword for uniform refinement ratio.

    Returns:
        GCIResult containing apparent order p, extrapolated solution, errors, GCI, and convergence_type.

    Raises:
        ValueError: If refinement ratio <= 1.0 or parameters invalid.
    """
    if r21 is None:
        if r is not None:
            r21 = float(r)
        else:
            raise ValueError("Refinement ratio r21 (or r) must be provided.")
    else:
        r21 = float(r21)

    if r32 is None:
        r32 = r21
    else:
        r32 = float(r32)

    if r21 <= 1.0:
        raise ValueError(f"Refinement ratio r21 must be strictly greater than 1.0, got {r21}")
    if r32 <= 1.0:
        raise ValueError(f"Refinement ratio r32 must be strictly greater than 1.0, got {r32}")

    eps_32 = float(phi_3 - phi_2)
    eps_21 = float(phi_2 - phi_1)

    # Check for zero step / identical solutions
    if abs(eps_21) < 1e-15 and abs(eps_32) < 1e-15:
        return GCIResult(
            apparent_order_p=0.0,
            phi_extrapolated=float(phi_1),
            approx_rel_error_fine=0.0,
            gci_fine=0.0,
            gci_medium=0.0,
            asymptotic_ratio=1.0,
            convergence_type="monotonic",
        )

    if abs(eps_32) < 1e-15:
        # eps_32 == 0 while eps_21 != 0 -> infinite ratio R -> divergence
        return GCIResult(
            apparent_order_p=float("nan"),
            phi_extrapolated=float("nan"),
            approx_rel_error_fine=float(abs(eps_21 / (phi_1 + 1e-30))),
            gci_fine=float("inf"),
            gci_medium=float("inf"),
            asymptotic_ratio=float("nan"),
            convergence_type="divergence",
        )

    # Convergence ratio R (Celik et al. 2008)
    R = eps_21 / eps_32

    # Divergence detection: |R| >= 1.0 (error expands on refinement)
    if abs(R) >= 1.0:
        return GCIResult(
            apparent_order_p=float("nan"),
            phi_extrapolated=float("nan"),
            approx_rel_error_fine=float(abs(eps_21 / (phi_1 + 1e-30))),
            gci_fine=float("inf"),
            gci_medium=float("inf"),
            asymptotic_ratio=float("nan"),
            convergence_type="divergence",
        )

    convergence_type = "oscillatory" if R < 0.0 else "monotonic"

    # Apparent order of convergence p (Celik et al. 2008, Eq. 10)
    inv_R = abs(1.0 / R)
    if abs(r21 - r32) < 1e-6:
        # Uniform refinement ratio
        p = float(np.log(inv_R) / np.log(r21))
    else:
        # Non-uniform refinement ratio: fixed-point iteration
        s = 1.0 if R > 0.0 else -1.0
        p = float(np.log(inv_R) / np.log(r21))
        for _ in range(100):
            num = r21 ** p - s
            den = r32 ** p - s
            if den <= 0.0 or num <= 0.0:
                break
            q = float(np.log(num / den))
            p_next = float((1.0 / np.log(r21)) * abs(np.log(inv_R) + q))
            if abs(p_next - p) < 1e-6:
                p = p_next
                break
            p = p_next

    # Richardson extrapolation (Celik et al. 2008, Eq. 13)
    r21_pow_p = float(r21 ** p)
    denom21 = r21_pow_p - 1.0
    if abs(denom21) < 1e-15:
        phi_ext_21 = float(phi_1)
    else:
        phi_ext_21 = float((r21_pow_p * phi_1 - phi_2) / denom21)

    # Approximate relative errors (Celik et al. 2008, Eq. 14)
    e_a_21 = float(abs((phi_1 - phi_2) / (phi_1 + 1e-30)))
    e_a_32 = float(abs((phi_2 - phi_3) / (phi_2 + 1e-30)))

    # Grid Convergence Index (Celik et al. 2008, Eqs. 16-17)
    gci_21 = float((f_s * e_a_21) / max(denom21, 1e-15))
    r32_pow_p = float(r32 ** p)
    denom32 = r32_pow_p - 1.0
    gci_32 = float((f_s * e_a_32) / max(denom32, 1e-15))

    # Asymptotic range check (Celik et al. 2008, Eq. 18)
    asymptotic_ratio = float(gci_32 / (r21_pow_p * gci_21 + 1e-30))

    return GCIResult(
        apparent_order_p=p,
        phi_extrapolated=phi_ext_21,
        approx_rel_error_fine=e_a_21,
        gci_fine=gci_21,
        gci_medium=gci_32,
        asymptotic_ratio=asymptotic_ratio,
        convergence_type=convergence_type,
    )


def generate_3grid_gci_hierarchy(
    mesh_generator_func: Callable[[float], UnstructuredMesh],
    base_h0: float,
    r: float = 1.41421356,
) -> GCIHierarchy:
    """Generate and verify a deterministic 3-grid GCI hierarchy.

    Generates three systematically refined grids:
    - Fine grid:   h1 = base_h0
    - Medium grid: h2 = r * base_h0
    - Coarse grid: h3 = r^2 * base_h0

    Verifies the following mathematical and topological invariants across all 3 levels:
    1. Strictly positive cell volumes: for all cells c in each mesh, V_c > 0.
    2. Preservation of boundary patch topology: mesh.boundaries.keys() are identical.
    3. Conformal topological dimension: topological_dim is consistent.
    4. Exact relative Gauss closure and projection identities via mesh.validate().

    Args:
        mesh_generator_func: Callable accepting nominal element size h (float) and
            returning an UnstructuredMesh.
        base_h0: Target nominal element size on the fine grid (h1 > 0).
        r: Grid refinement ratio in [1.3, 1.5] (nominally sqrt(2) ~ 1.41421356).

    Returns:
        GCIHierarchy containing (fine, medium, coarse) meshes satisfying all invariants.

    Raises:
        ValueError: If base_h0 <= 0, r <= 1.0, volumes are non-positive,
            patch sets mismatch, or topology validation fails.
    """
    if base_h0 <= 0.0:
        raise ValueError(f"base_h0 must be strictly positive, got {base_h0}")
    if r <= 1.0:
        raise ValueError(f"Refinement ratio r must be strictly greater than 1.0, got {r}")

    if r < 1.3 or r > 1.5:
        warnings.warn(
            f"Grid refinement ratio r={r:.4f} is outside recommended ASME V&V 20-2009 "
            f"range [1.3, 1.5]. Results may have higher uncertainty.",
            UserWarning,
            stacklevel=2,
        )

    h1 = float(base_h0)
    h2 = float(base_h0 * r)
    h3 = float(base_h0 * (r ** 2))

    logger.info("Generating 3-grid GCI hierarchy: h1=%.5f (fine), h2=%.5f (med), h3=%.5f (coarse)", h1, h2, h3)

    # Deterministic generation
    mesh_coarse = mesh_generator_func(h3)
    mesh_medium = mesh_generator_func(h2)
    mesh_fine = mesh_generator_func(h1)

    meshes = [("fine", mesh_fine, h1), ("medium", mesh_medium, h2), ("coarse", mesh_coarse, h3)]

    # 1. Strictly positive cell volumes & mesh validation
    for name, m, h in meshes:
        if m.num_cells == 0:
            raise ValueError(f"GCI {name} mesh at h={h:.5f} generated 0 cells.")
        min_vol = float(np.min(m.cell_volumes))
        if min_vol <= 0.0:
            raise ValueError(
                f"GCI {name} mesh at h={h:.5f} violated positivity of volume: "
                f"min(V) = {min_vol:.2e} <= 0"
            )
        # Validate FVM graph, upper-triangular ordering, and Gauss closure
        m.validate()

    # 2. Boundary patch preservation
    patches_fine = set(mesh_fine.boundaries.keys())
    patches_medium = set(mesh_medium.boundaries.keys())
    patches_coarse = set(mesh_coarse.boundaries.keys())

    if patches_fine != patches_medium or patches_fine != patches_coarse:
        diff_med = patches_fine.symmetric_difference(patches_medium)
        diff_coarse = patches_fine.symmetric_difference(patches_coarse)
        raise ValueError(
            f"Boundary patch mismatch across 3-grid GCI hierarchy: "
            f"fine={patches_fine}, medium={patches_medium}, coarse={patches_coarse}. "
            f"Diff(fine, med)={diff_med}, Diff(fine, coarse)={diff_coarse}"
        )

    # 3. Topological dimension consistency
    if not (mesh_fine.topological_dim == mesh_medium.topological_dim == mesh_coarse.topological_dim):
        raise ValueError(
            f"Topological dimension mismatch across GCI hierarchy: "
            f"fine={mesh_fine.topological_dim}, medium={mesh_medium.topological_dim}, "
            f"coarse={mesh_coarse.topological_dim}"
        )

    logger.info(
        "GCI hierarchy verified successfully: %d patches preserved across all 3 levels, "
        "cells=(fine:%d, med:%d, coarse:%d).",
        len(patches_fine),
        mesh_fine.num_cells,
        mesh_medium.num_cells,
        mesh_coarse.num_cells,
    )

    # Calculate effective grid refinement ratios from actual cell counts
    topo_dim = getattr(mesh_fine, "topological_dim", 3)
    dim = float(max(1, int(topo_dim))) if topo_dim is not None else 3.0
    n1 = float(mesh_fine.num_cells)
    n2 = float(mesh_medium.num_cells)
    n3 = float(mesh_coarse.num_cells)
    r21_eff = float((n1 / n2) ** (1.0 / dim)) if (n2 > 0 and n1 > 0) else r
    r32_eff = float((n2 / n3) ** (1.0 / dim)) if (n3 > 0 and n2 > 0) else r

    return GCIHierarchy(
        fine=mesh_fine,
        medium=mesh_medium,
        coarse=mesh_coarse,
        r=r,
        h1=h1,
        h2=h2,
        h3=h3,
        r21=r21_eff,
        r32=r32_eff,
    )
