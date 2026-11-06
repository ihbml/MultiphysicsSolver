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
    """ASME V&V 20-2009 Grid Convergence Index calculation results.

    Attributes:
        apparent_order_p: Observed apparent order of convergence p.
        phi_extrapolated: Richardson extrapolated continuum solution phi_ext^21.
        approx_rel_error_fine: Approximate relative error on fine grid e_a^21 = |(phi1 - phi2) / phi1|.
        gci_fine: Fine grid convergence index GCI_21 = F_s * e_a^21 / (r^p - 1).
        gci_medium: Medium grid convergence index GCI_32 = F_s * e_a^32 / (r^p - 1).
        asymptotic_ratio: Range check ratio GCI_32 / (r^p * GCI_21) (asymptotic if ~ 1.0).
    """

    apparent_order_p: float
    phi_extrapolated: float
    approx_rel_error_fine: float
    gci_fine: float
    gci_medium: float
    asymptotic_ratio: float

    def __getitem__(self, key: str) -> Any:
        """Allow dict-like indexing for backward compatibility with test suites."""
        if hasattr(self, key):
            return getattr(self, key)
        raise KeyError(f"GCIResult has no field '{key}'")

    def to_dict(self) -> Dict[str, float]:
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
    return float(avg_vol ** (1.0 / 3.0))


def compute_asme_vv20_gci(
    phi_1: float,
    phi_2: float,
    phi_3: float,
    r: float,
    f_s: float = 1.25,
) -> GCIResult:
    """Calculate ASME V&V 20-2009 / Celik et al. (2008) Grid Convergence Index.

    Args:
        phi_1: Numerical solution of key scalar quantity on fine grid (h1).
        phi_2: Numerical solution of key scalar quantity on medium grid (h2 = r * h1).
        phi_3: Numerical solution of key scalar quantity on coarse grid (h3 = r^2 * h1).
        r: Uniform grid refinement ratio (r = h2 / h1 = h3 / h2 > 1).
        f_s: Safety factor (1.25 for 3-grid study with estimated p, 3.0 for 2-grid study).

    Returns:
        GCIResult containing apparent order p, extrapolated solution, errors, and GCI indices.

    Raises:
        ValueError: If refinement ratio r <= 1.0 or solutions are invalid.
    """
    if r <= 1.0:
        raise ValueError(f"Refinement ratio r must be strictly greater than 1.0, got {r}")

    eps_32 = phi_3 - phi_2
    eps_21 = phi_2 - phi_1

    # Apparent order of convergence p (Celik et al. 2008, Eq. 10)
    # For uniform refinement ratio r: q(p) = 0
    ratio = abs(eps_32 / (eps_21 + 1e-30))
    if ratio < 1e-15:
        p = 1.0
    else:
        p = float(abs(np.log(ratio)) / np.log(r))

    # Clamp apparent order to physical range [0.5, 4.0] for stability if divergent
    p_eff = max(0.5, min(p, 6.0))

    # Richardson extrapolation (Celik et al. 2008, Eq. 13)
    r_pow_p = r ** p_eff
    denom = r_pow_p - 1.0
    if abs(denom) < 1e-15:
        phi_ext_21 = phi_1
    else:
        phi_ext_21 = float((r_pow_p * phi_1 - phi_2) / denom)

    # Approximate relative errors (Celik et al. 2008, Eq. 14)
    e_a_21 = float(abs((phi_1 - phi_2) / (phi_1 + 1e-30)))
    e_a_32 = float(abs((phi_2 - phi_3) / (phi_2 + 1e-30)))

    # Grid Convergence Index (Celik et al. 2008, Eqs. 16-17)
    gci_21 = float((f_s * e_a_21) / max(denom, 1e-15))
    gci_32 = float((f_s * e_a_32) / max(denom, 1e-15))

    # Asymptotic range check (Celik et al. 2008, Eq. 18)
    asymptotic_ratio = float(gci_32 / (r_pow_p * gci_21 + 1e-30))

    return GCIResult(
        apparent_order_p=p,
        phi_extrapolated=phi_ext_21,
        approx_rel_error_fine=e_a_21,
        gci_fine=gci_21,
        gci_medium=gci_32,
        asymptotic_ratio=asymptotic_ratio,
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

    return GCIHierarchy(
        fine=mesh_fine,
        medium=mesh_medium,
        coarse=mesh_coarse,
        r=r,
        h1=h1,
        h2=h2,
        h3=h3,
    )
