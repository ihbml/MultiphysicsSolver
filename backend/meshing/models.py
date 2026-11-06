"""Data models for CFDSolver In-Memory Multiphysics Mesher Backbone.

Implements high-performance, vectorized Structure of Arrays (SoA) dataclasses:
- UnstructuredMesh: Collocated cell-centred polyhedral graph mesh (DiFVM & FoamMesh C-ABI)
- SPHParticleSet: Multiphysics SPH particle container with boundary support indicators
- MeshQualityReport: Comprehensive topological and geometric quality audit
- SizingParameters: Adaptive element sizing configuration parameters
"""

from __future__ import annotations

from dataclasses import asdict, dataclass, field
from enum import IntEnum
from pathlib import Path
from typing import Any, Dict, List, Literal, Optional, Tuple, Union
import numpy as np

from backend.geometry.models import BoundingBox


@dataclass
class FarfieldPadding:
    """Relative farfield padding multipliers based on characteristic length L_ref."""

    upstream: float = 5.0     # 5L upstream along -X
    downstream: float = 10.0  # 10L downstream along +X
    lateral_y: float = 5.0    # 5L lateral along +/-Y
    lateral_z: float = 5.0    # 5L lateral along +/-Z

    def to_dict(self) -> Dict[str, Any]:
        return asdict(self)

    @classmethod
    def from_dict(cls, data: Dict[str, Any]) -> "FarfieldPadding":
        return cls(**{k: v for k, v in data.items() if k in cls.__dataclass_fields__})



class CellType(IntEnum):
    """VTK-compatible explicit cell types for unstructured meshes."""

    TRIANGLE = 5       # VTK_TRIANGLE (2D)
    POLYGON = 7        # VTK_POLYGON (2D)
    QUAD = 9           # VTK_QUAD (2D coplanar)
    TETRA = 10         # VTK_TETRA (3D)
    HEXAHEDRON = 12    # VTK_HEXAHEDRON (3D)
    WEDGE = 13         # VTK_WEDGE / Prism (3D)
    PYRAMID = 14       # VTK_PYRAMID (3D)
    POLYHEDRON = 42    # VTK_POLYHEDRON (3D general)


@dataclass
class InflationConfig:
    """Configuration for prismatic boundary layer inflation with Feature 23 pinching."""

    num_layers: int = 5
    first_layer_height: float = 0.001
    growth_rate: float = 1.2
    expansion_ratio: Optional[float] = None  # Alias for growth_rate / backwards compatibility
    transition_ratio: float = 0.5
    pinching: bool = True
    pinching_threshold_angle: float = 60.0

    def __post_init__(self) -> None:
        if self.expansion_ratio is not None:
            self.growth_rate = float(self.expansion_ratio)
        else:
            self.expansion_ratio = float(self.growth_rate)

    def to_dict(self) -> Dict[str, Any]:
        return asdict(self)

    @classmethod
    def from_dict(cls, data: Dict[str, Any]) -> "InflationConfig":
        return cls(**{k: v for k, v in data.items() if k in cls.__dataclass_fields__})


@dataclass
class CurvatureSizingConfig:
    """Configuration for curvature and proximity adaptive sizing."""

    alpha: float = 0.2
    beta: float = 0.25
    h_min: float = 0.005
    h_max: float = 0.1
    growth_rate: float = 1.2
    h0: float = 0.050
    grading: float = 0.350
    eps_curv: float = 1e-6
    max_gap_search: Optional[float] = None
    ray_march_steps: int = 8
    theta_opp_threshold: float = -0.500
    fd_delta: float = 1e-4

    def __post_init__(self) -> None:
        if self.growth_rate != 1.2 and self.grading == 0.350:
            self.grading = self.growth_rate - 1.0 if self.growth_rate > 1.0 else self.growth_rate

    def to_dict(self) -> Dict[str, Any]:
        return asdict(self)

    @classmethod
    def from_dict(cls, data: Dict[str, Any]) -> "CurvatureSizingConfig":
        return cls(**{k: v for k, v in data.items() if k in cls.__dataclass_fields__})

    def to_sizing_parameters(self) -> "SizingParameters":
        return SizingParameters(
            h_min=self.h_min,
            h_max=self.h_max,
            h0=self.h0,
            alpha=self.alpha,
            beta=self.beta,
            eps_curv=self.eps_curv,
            grading=self.grading,
            max_gap_search=self.max_gap_search,
            ray_march_steps=self.ray_march_steps,
            theta_opp_threshold=self.theta_opp_threshold,
            fd_delta=self.fd_delta,
        )


@dataclass
class QualityConfig:
    """Quality acceptance thresholds and solver stabilization configuration."""

    min_orthogonal_quality: float = 0.2
    max_skewness: float = 0.7
    max_aspect_ratio: float = 40.0

    def to_dict(self) -> Dict[str, Any]:
        return asdict(self)

    @classmethod
    def from_dict(cls, data: Dict[str, Any]) -> "QualityConfig":
        return cls(**{k: v for k, v in data.items() if k in cls.__dataclass_fields__})


@dataclass
class MeshingConfig:
    """Declarative unified configuration schema for native mesher and frontend."""

    # WP4 Domain Mode and Farfield Extensions
    domain_mode: Literal["external_fluid", "internal_fluid", "solid_only", "cht_coupled"] = "external_fluid"
    farfield_padding: Optional[FarfieldPadding] = None
    farfield_bbox: Optional[Tuple[Tuple[float, float, float], Tuple[float, float, float]]] = None
    small_cell_ratio: float = 0.20
    wsrd_target_alpha: float = 0.50
    gci_refinement_ratio: float = 1.41421356

    # Existing meshing subsystem configurations
    sizing: CurvatureSizingConfig = field(default_factory=CurvatureSizingConfig)
    inflation: Optional[InflationConfig] = None
    quality: QualityConfig = field(default_factory=QualityConfig)
    rcm_reorder: bool = True
    cht_multiregion: bool = False
    cyclic_symmetry: bool = False
    cyclic_angle_deg: float = 0.0
    distmesh_iters: int = 45

    def to_dict(self) -> Dict[str, Any]:
        return asdict(self)

    @classmethod
    def from_dict(cls, data: Dict[str, Any]) -> "MeshingConfig":
        sizing_data = data.get("sizing", {})
        sizing = CurvatureSizingConfig.from_dict(sizing_data) if isinstance(sizing_data, dict) else sizing_data
        inflation_data = data.get("inflation")
        inflation = (
            InflationConfig.from_dict(inflation_data)
            if isinstance(inflation_data, dict)
            else inflation_data
        )
        quality_data = data.get("quality", {})
        quality = QualityConfig.from_dict(quality_data) if isinstance(quality_data, dict) else quality_data

        farfield_padding_data = data.get("farfield_padding")
        if isinstance(farfield_padding_data, dict):
            farfield_padding = FarfieldPadding.from_dict(farfield_padding_data)
        elif isinstance(farfield_padding_data, FarfieldPadding):
            farfield_padding = farfield_padding_data
        else:
            farfield_padding = None

        farfield_bbox_raw = data.get("farfield_bbox")
        if farfield_bbox_raw is not None:
            if hasattr(farfield_bbox_raw, "min_pt") and hasattr(farfield_bbox_raw, "max_pt"):
                farfield_bbox = (
                    tuple(float(x) for x in farfield_bbox_raw.min_pt),
                    tuple(float(x) for x in farfield_bbox_raw.max_pt),
                )
            elif len(farfield_bbox_raw) == 2:
                farfield_bbox = (
                    tuple(float(x) for x in farfield_bbox_raw[0]),
                    tuple(float(x) for x in farfield_bbox_raw[1]),
                )
            elif len(farfield_bbox_raw) == 6:
                farfield_bbox = (
                    tuple(float(x) for x in farfield_bbox_raw[:3]),
                    tuple(float(x) for x in farfield_bbox_raw[3:]),
                )
            else:
                farfield_bbox = None
        else:
            farfield_bbox = None

        return cls(
            domain_mode=data.get("domain_mode", "external_fluid"),
            farfield_padding=farfield_padding,
            farfield_bbox=farfield_bbox,
            small_cell_ratio=float(data.get("small_cell_ratio", 0.20)),
            wsrd_target_alpha=float(data.get("wsrd_target_alpha", 0.50)),
            gci_refinement_ratio=float(data.get("gci_refinement_ratio", 1.41421356)),
            sizing=sizing if sizing is not None else CurvatureSizingConfig(),
            inflation=inflation,
            quality=quality if quality is not None else QualityConfig(),
            rcm_reorder=data.get("rcm_reorder", True),
            cht_multiregion=data.get("cht_multiregion", False),
            cyclic_symmetry=data.get("cyclic_symmetry", False),
            cyclic_angle_deg=float(data.get("cyclic_angle_deg", 0.0)),
            distmesh_iters=int(data.get("distmesh_iters", 45)),
        )



@dataclass
class SizingParameters:
    """Configuration parameters for adaptive element sizing h(x)."""

    h_min: float = 0.005
    """Strict lower bound on element size (m)."""

    h_max: float = 0.200
    """Strict upper bound on element size (m)."""

    h0: float = 0.050
    """Nominal / background element size (m)."""

    alpha: float = 0.200
    """Curvature resolution factor (rad). h_curv = alpha / max(|kappa|, eps).
    Equivalent to ~31.4 linear elements around a full 360-degree circle."""

    beta: float = 0.250
    """Proximity resolution factor. h_prox = beta * gap_distance.
    Equivalent to 4 elements spanning a narrow gap (1 / beta = 4)."""

    eps_curv: float = 1e-6
    """Curvature regularizer avoiding division-by-zero in planar regions (1/m)."""

    grading: float = 0.350
    """Maximum spatial growth rate ||grad h|| <= grading (dimensionless)."""

    max_gap_search: Optional[float] = None
    """Maximum opposing wall search distance. Defaults to h_max / beta."""

    ray_march_steps: int = 8
    """Number of discrete sampling steps for opposing wall ray marching."""

    theta_opp_threshold: float = -0.500
    """Normal dot product threshold cos(theta) for opposing wall detection."""

    fd_delta: float = 1e-4
    """Finite difference perturbation step for second derivatives (m)."""

    growth_rate: Optional[float] = None
    """Optional growth rate alias for grading (e.g. 1.2 -> 0.20)."""

    def __post_init__(self) -> None:
        if self.growth_rate is not None:
            self.grading = float(self.growth_rate - 1.0 if self.growth_rate > 1.0 else self.growth_rate)
        if self.h_min <= 0.0:
            raise ValueError(f"h_min must be positive, got {self.h_min}")
        if self.h_max < self.h_min:
            raise ValueError(f"h_max ({self.h_max}) cannot be smaller than h_min ({self.h_min})")
        if self.alpha <= 0.0:
            raise ValueError(f"alpha must be positive, got {self.alpha}")
        if self.beta <= 0.0:
            raise ValueError(f"beta must be positive, got {self.beta}")
        if self.grading <= 0.0:
            raise ValueError(f"grading must be positive, got {self.grading}")
        if self.max_gap_search is None:
            self.max_gap_search = self.h_max / self.beta

    def to_curvature_sizing_config(self) -> CurvatureSizingConfig:
        return CurvatureSizingConfig(
            alpha=self.alpha,
            beta=self.beta,
            h_min=self.h_min,
            h_max=self.h_max,
            growth_rate=1.0 + self.grading,
            h0=self.h0,
            grading=self.grading,
            eps_curv=self.eps_curv,
            max_gap_search=self.max_gap_search,
            ray_march_steps=self.ray_march_steps,
            theta_opp_threshold=self.theta_opp_threshold,
            fd_delta=self.fd_delta,
        )

    def to_dict(self) -> Dict[str, Any]:
        return asdict(self)

    @classmethod
    def from_dict(cls, data: Dict[str, Any]) -> "SizingParameters":
        return cls(**{k: v for k, v in data.items() if k in cls.__dataclass_fields__})


@dataclass
class MeshQualityReport:
    """Comprehensive topological and numerical quality report for an UnstructuredMesh."""

    num_cells: int
    num_faces: int
    num_internal_faces: int
    num_boundary_faces: int
    num_points: int
    min_cell_volume: float
    max_cell_volume: float
    total_volume: float
    max_closed_cell_divergence: float
    non_ortho_max: float
    non_ortho_mean: float
    skewness_max: float
    skewness_mean: float
    aspect_ratio_max: float
    aspect_ratio_mean: float
    stabilization_tier_counts: Dict[int, int] = field(default_factory=dict)
    is_valid: bool = True
    details: Dict[str, Any] = field(default_factory=dict)


@dataclass
class UnstructuredMesh:
    """In-Memory Unstructured Mesh in DiFVM Structure of Arrays (SoA) format.

    Guarantees bit-for-bit compatibility with OpenFOAM FoamMesh and native C-ABI.
    """

    points: np.ndarray             # shape (N_p, 3), float64: vertex coordinates
    cells: List[np.ndarray]        # list of (N_v,) vertex index arrays per cell
    owner: np.ndarray              # shape (N_f,), int64: owner cell index
    neighbour: np.ndarray          # shape (N_f,), int64: neighbour cell index (-1 on boundary)
    face_areas: np.ndarray         # shape (N_f, 3), float64: S_f vector pointing owner -> neighbour
    face_centres: np.ndarray       # shape (N_f, 3), float64: c_f face centroid
    cell_centres: np.ndarray       # shape (N_c, 3), float64: C_i cell centroid
    cell_volumes: np.ndarray       # shape (N_c,), float64: V_i > 0
    delta_f: np.ndarray            # shape (N_f, 3), float64: orthogonal projection vector
    k_f: np.ndarray                # shape (N_f, 3), float64: non-orthogonal correction, delta_f + k_f == S_f
    boundaries: Dict[str, Tuple[int, int]] # patch_name -> (start_face, num_faces)
    cell_regions: Optional[np.ndarray] = None # shape (N_c,), int32: region ID per cell (CHT multi-region)
    baffles: List[Dict[str, Any]] = field(default_factory=list) # coupledWall / mappedWall twin-face interfaces
    cyclic_pairs: List[Tuple[int, int]] = field(default_factory=list) # pairs of cyclic boundary face indices (Feature 22)
    error_map: Dict[str, np.ndarray] = field(default_factory=dict) # non_ortho, skewness, aspect_ratio, mask
    metadata: Dict[str, Any] = field(default_factory=dict)
    faces: Optional[List[np.ndarray]] = None # List of face vertex arrays (ordered according to right-hand rule)
    cell_types: Optional[np.ndarray] = None # shape (N_c,), int32: VTK CellType per cell
    topological_dim: int = 3 # 2 for planar meshes, 3 for volumetric meshes

    def __post_init__(self) -> None:
        self.points = np.asarray(self.points, dtype=np.float64)
        self.owner = np.asarray(self.owner, dtype=np.int64)
        self.neighbour = np.asarray(self.neighbour, dtype=np.int64)
        self.face_areas = np.asarray(self.face_areas, dtype=np.float64)
        self.face_centres = np.asarray(self.face_centres, dtype=np.float64)
        self.cell_centres = np.asarray(self.cell_centres, dtype=np.float64)
        self.cell_volumes = np.asarray(self.cell_volumes, dtype=np.float64)
        self.delta_f = np.asarray(self.delta_f, dtype=np.float64)
        self.k_f = np.asarray(self.k_f, dtype=np.float64)
        if self.cell_regions is not None:
            self.cell_regions = np.asarray(self.cell_regions, dtype=np.int32)
        else:
            self.cell_regions = np.zeros(len(self.cell_volumes), dtype=np.int32)
        if self.cell_types is not None:
            self.cell_types = np.asarray(self.cell_types, dtype=np.int32)
        if self.faces is not None:
            self.faces = [np.asarray(f, dtype=np.int64) for f in self.faces]

    @property
    def num_regions(self) -> int:
        return int(len(np.unique(self.cell_regions))) if self.cell_regions is not None else 1

    @property
    def is_multiregion(self) -> bool:
        return self.num_regions > 1

    @property
    def num_points(self) -> int:
        return len(self.points)

    @property
    def num_cells(self) -> int:
        return len(self.cell_volumes)

    @property
    def num_faces(self) -> int:
        return len(self.face_areas)

    @property
    def num_internal_faces(self) -> int:
        return int(np.sum(self.neighbour >= 0))

    @property
    def num_boundary_faces(self) -> int:
        return int(np.sum(self.neighbour < 0))

    def get_bounding_box(self) -> BoundingBox:
        """Compute spatial bounding box enclosing all mesh points."""
        return BoundingBox.from_points(self.points)

    def to_openfoam_polymesh(self, case_dir: Union[str, Path], write_binary: bool = False) -> Path:
        """Export mesh to OpenFOAM constant/polyMesh directory."""
        from backend.meshing.polymesh_io import export_openfoam_polymesh
        return export_openfoam_polymesh(self, case_dir=case_dir, write_binary=write_binary)

    def validate(
        self,
        tol_closure: float = 1e-11,
        tol_decomp: float = 1e-14,
        tol_rel: float = 1e-11,
        tol_proj: float = 1e-12,
        tol_relax: float = 1e-10,
    ) -> bool:
        """Audit physical and topological invariants. Raises ValueError on violation."""
        if np.any(self.cell_volumes <= 0.0):
            bad_idx = np.where(self.cell_volumes <= 0.0)[0]
            raise ValueError(
                f"Mesh has {len(bad_idx)} cells with non-positive volume! Min: {np.min(self.cell_volumes)}"
            )

        internal = self.neighbour >= 0
        n_int = int(np.sum(internal))
        n_faces = self.num_faces

        # 1. Contiguous block ordering: all internal faces must precede boundary faces
        if n_faces > 0:
            if not np.all(self.neighbour[:n_int] >= 0) or not np.all(self.neighbour[n_int:] == -1):
                raise ValueError("Contiguous block ordering violated: all internal faces must precede boundary faces")

        # 1b. Boundary patch coverage validation
        if self.boundaries:
            n_bnd = self.num_boundary_faces
            coverage = np.zeros(n_bnd, dtype=bool)
            total_patch_faces = 0

            for pname, (start_f, count_f) in self.boundaries.items():
                if count_f < 0:
                    raise ValueError(f"Boundary patch '{pname}' has negative count {count_f}")
                if start_f < n_int or start_f + count_f > n_faces:
                    raise ValueError(
                        f"Boundary patch '{pname}' range [{start_f}, {start_f + count_f}) "
                        f"lies outside boundary range [{n_int}, {n_faces})"
                    )
                rel_start = start_f - n_int
                rel_end = rel_start + count_f
                if np.any(coverage[rel_start:rel_end]):
                    raise ValueError(
                        f"Boundary patch '{pname}' overlaps with another patch in range [{start_f}, {start_f + count_f})"
                    )
                coverage[rel_start:rel_end] = True
                total_patch_faces += count_f

            if total_patch_faces != n_bnd:
                raise ValueError(
                    f"Boundary patches cover {total_patch_faces} faces, expected {n_bnd} faces"
                )

        # 2. Strict upper-triangular ordering on internal faces: owner < neighbour
        if n_int > 0:
            if np.any(self.owner[internal] >= self.neighbour[internal]):
                bad = np.where(self.owner[internal] >= self.neighbour[internal])[0]
                raise ValueError(
                    f"Topological invariant violated: owner must be strictly less than neighbour on all internal faces ({len(bad)} violations)"
                )

        # 3. Vectorized scale-invariant Gauss divergence closure theorem
        if n_faces > 0 and self.num_cells > 0:
            cell_surface_area = np.zeros(self.num_cells, dtype=np.float64)
            face_area_mag = np.linalg.norm(self.face_areas, axis=1)
            np.add.at(cell_surface_area, self.owner, face_area_mag)
            np.add.at(cell_surface_area, self.neighbour[internal], face_area_mag[internal])

            closure = np.zeros_like(self.cell_centres)
            np.add.at(closure, self.owner, self.face_areas)
            np.subtract.at(closure, self.neighbour[internal], self.face_areas[internal])

            closure_norm = np.linalg.norm(closure, axis=1)
            rel_closure = np.where(cell_surface_area > 1e-30, closure_norm / cell_surface_area, closure_norm)
            max_rel_closure = float(np.max(rel_closure))

            effective_tol = tol_closure if tol_closure != 1e-11 else tol_rel
            if max_rel_closure > effective_tol:
                raise ValueError(
                    f"Closed-cell Gauss divergence theorem violated: max error = {max_rel_closure:.2e} > {effective_tol:.2e}"
                )

        # 4. Non-orthogonal decomposition identity: delta_f + k_f == S_f
        if n_int > 0:
            faces_to_check = internal
        else:
            # If mesh has only boundary faces, check if k_f is explicitly non-zero
            faces_to_check = (
                np.any(np.abs(self.k_f) > 1e-12, axis=1)
                if len(self.k_f) > 0
                else np.array([], dtype=bool)
            )

        if (
            np.any(faces_to_check)
            and len(self.face_areas) > 0
            and len(self.delta_f) > 0
            and len(self.k_f) > 0
        ):
            decomp_diff = float(
                np.max(
                    np.linalg.norm(
                        (self.delta_f[faces_to_check] + self.k_f[faces_to_check])
                        - self.face_areas[faces_to_check],
                        axis=1,
                    )
                )
            )
            if decomp_diff > tol_decomp:
                raise ValueError(
                    f"Non-orthogonal decomposition identity failed: max diff = {decomp_diff:.2e} > {tol_decomp:.2e}"
                )

        # 5. True projection identity tests (collinearity and over-relaxation on internal faces)
        if n_int > 0:
            delta_int = self.delta_f[internal]
            d_f = self.cell_centres[self.neighbour[internal]] - self.cell_centres[self.owner[internal]]
            delta_norm = np.linalg.norm(delta_int, axis=1)
            d_norm = np.linalg.norm(d_f, axis=1)
            denom = delta_norm * d_norm
            valid_collin = denom > 1e-30
            if np.any(valid_collin):
                cross_prod = np.cross(delta_int[valid_collin], d_f[valid_collin])
                cross_mag = np.linalg.norm(cross_prod, axis=1)
                collin_err = cross_mag / denom[valid_collin]
                max_collin_err = float(np.max(collin_err))
                if max_collin_err > tol_proj:
                    raise ValueError(
                        f"Projection collinearity violated: max error = {max_collin_err:.2e} > {tol_proj:.2e}"
                    )

            # Over-relaxation identity: delta_f . S_f == ||S_f||^2 for theta_f < 89 deg
            S_int = self.face_areas[internal]
            S_norm_sq = np.sum(S_int * S_int, axis=1)
            cos_theta = np.sum(S_int * d_f, axis=1) / (np.linalg.norm(S_int, axis=1) * d_norm + 1e-30)
            cos_theta = np.clip(cos_theta, -1.0, 1.0)
            theta_deg = np.degrees(np.arccos(cos_theta))
            relax_mask = (theta_deg < 89.0) & (S_norm_sq > 1e-30)
            if np.any(relax_mask):
                dot_delta_S = np.sum(delta_int[relax_mask] * S_int[relax_mask], axis=1)
                rel_relax_err = np.abs(dot_delta_S - S_norm_sq[relax_mask]) / S_norm_sq[relax_mask]
                max_relax_err = float(np.max(rel_relax_err))
                if max_relax_err > tol_relax:
                    raise ValueError(
                        f"Over-relaxation identity delta_f . S_f == ||S_f||^2 violated: max error = {max_relax_err:.2e} > {tol_relax:.2e}"
                    )

        return True

    def compute_quality_report(self) -> MeshQualityReport:
        """Evaluate and return a structured quality audit report."""
        closure = np.zeros_like(self.cell_centres)
        if self.num_faces > 0 and self.num_cells > 0:
            internal = self.neighbour >= 0
            np.add.at(closure, self.owner, self.face_areas)
            np.subtract.at(closure, self.neighbour[internal], self.face_areas[internal])
        max_closure = float(np.max(np.linalg.norm(closure, axis=1))) if len(closure) > 0 else 0.0

        non_ortho = self.error_map.get(
            "non_orthogonality",
            self.error_map.get("non_ortho", np.zeros(self.num_internal_faces)),
        )
        skewness = self.error_map.get(
            "skewness",
            self.error_map.get("skew", np.zeros(self.num_internal_faces)),
        )
        aspect_ratio = self.error_map.get(
            "aspect_ratio",
            np.ones(self.num_cells),
        )
        mask = self.error_map.get(
            "stabilization_mask",
            self.error_map.get("mask", np.zeros(self.num_internal_faces, dtype=np.int32)),
        )

        tier_counts = {tier: int(np.sum(mask == tier)) for tier in (0, 1, 2, 3)}

        min_vol = float(np.min(self.cell_volumes)) if len(self.cell_volumes) > 0 else 0.0
        max_vol = float(np.max(self.cell_volumes)) if len(self.cell_volumes) > 0 else 0.0
        tot_vol = float(np.sum(self.cell_volumes)) if len(self.cell_volumes) > 0 else 0.0

        return MeshQualityReport(
            num_cells=self.num_cells,
            num_faces=self.num_faces,
            num_internal_faces=self.num_internal_faces,
            num_boundary_faces=self.num_boundary_faces,
            num_points=self.num_points,
            min_cell_volume=min_vol,
            max_cell_volume=max_vol,
            total_volume=tot_vol,
            max_closed_cell_divergence=max_closure,
            non_ortho_max=float(np.max(non_ortho)) if len(non_ortho) > 0 else 0.0,
            non_ortho_mean=float(np.mean(non_ortho)) if len(non_ortho) > 0 else 0.0,
            skewness_max=float(np.max(skewness)) if len(skewness) > 0 else 0.0,
            skewness_mean=float(np.mean(skewness)) if len(skewness) > 0 else 0.0,
            aspect_ratio_max=float(np.max(aspect_ratio)) if len(aspect_ratio) > 0 else 1.0,
            aspect_ratio_mean=float(np.mean(aspect_ratio)) if len(aspect_ratio) > 0 else 1.0,
            stabilization_tier_counts=tier_counts,
            is_valid=(min_vol > 0.0 and max_closure <= 1e-10),
        )


@dataclass
class SPHParticleSet:
    """Multiphysics SPH particle container with boundary support detection and coupling vectors."""

    positions: np.ndarray                     # shape (N, 3), float64
    velocities: np.ndarray                    # shape (N, 3), float64
    masses: np.ndarray                        # shape (N,), float64
    densities: np.ndarray                     # shape (N,), float64
    volumes: np.ndarray                       # shape (N,), float64: V_i = m_i / rho_i
    smoothing_lengths: np.ndarray             # shape (N,), float64: h_i
    div_r: Optional[np.ndarray] = None        # shape (N,), float64: div r indicator
    is_boundary: Optional[np.ndarray] = None  # shape (N,), bool: div r < 0.75 * d
    wall_vectors: Optional[np.ndarray] = None # shape (N, 3), float64: A_iS boundary area vector
    categories: Optional[np.ndarray] = None   # shape (N,), int32: 0=Core, 1=Buffer, 2=Ghost
    metadata: Dict[str, Any] = field(default_factory=dict)

    def __post_init__(self) -> None:
        self.positions = np.asarray(self.positions, dtype=np.float64)
        self.velocities = np.asarray(self.velocities, dtype=np.float64)
        self.masses = np.asarray(self.masses, dtype=np.float64)
        self.densities = np.asarray(self.densities, dtype=np.float64)
        self.volumes = np.asarray(self.volumes, dtype=np.float64)
        self.smoothing_lengths = np.asarray(self.smoothing_lengths, dtype=np.float64)

    @property
    def num_particles(self) -> int:
        return len(self.positions)

    def get_bounding_box(self) -> BoundingBox:
        return BoundingBox.from_points(self.positions)
