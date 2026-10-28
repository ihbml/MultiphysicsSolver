"""Data models for CFDSolver In-Memory Multiphysics Mesher Backbone.

Implements high-performance, vectorized Structure of Arrays (SoA) dataclasses:
- UnstructuredMesh: Collocated cell-centred polyhedral graph mesh (DiFVM & FoamMesh C-ABI)
- SPHParticleSet: Multiphysics SPH particle container with boundary support indicators
- MeshQualityReport: Comprehensive topological and geometric quality audit
- SizingParameters: Adaptive element sizing configuration parameters
"""

from __future__ import annotations

from dataclasses import asdict, dataclass, field
from typing import Any, Dict, List, Optional, Tuple, Union
import numpy as np

from backend.geometry.models import BoundingBox


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

    def to_dict(self) -> Dict[str, Any]:
        return asdict(self)

    @classmethod
    def from_dict(cls, data: Dict[str, Any]) -> "CurvatureSizingConfig":
        return cls(**{k: v for k, v in data.items() if k in cls.__dataclass_fields__})


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
        return cls(
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

    def __post_init__(self) -> None:
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

    def validate(self, tol_closure: float = 1e-11, tol_decomp: float = 1e-14) -> bool:
        """Audit physical and topological invariants. Raises ValueError on violation."""
        if np.any(self.cell_volumes <= 0.0):
            bad_idx = np.where(self.cell_volumes <= 0.0)[0]
            raise ValueError(
                f"Mesh has {len(bad_idx)} cells with non-positive volume! Min: {np.min(self.cell_volumes)}"
            )

        # Verify over-relaxed decomposition identity: delta_f + k_f == S_f on internal faces
        # (or all faces if the mesh has no internal faces, e.g. single-cell test meshes)
        internal = self.neighbour >= 0
        faces_to_check = internal if np.any(internal) else np.ones(self.num_faces, dtype=bool)
        if len(self.face_areas) > 0 and len(self.delta_f) > 0 and len(self.k_f) > 0:
            decomp_diff = float(
                np.max(
                    np.linalg.norm(
                        (self.delta_f[faces_to_check] + self.k_f[faces_to_check]) - self.face_areas[faces_to_check],
                        axis=1,
                    )
                )
            )
            if decomp_diff > tol_decomp:
                raise ValueError(
                    f"Non-orthogonal decomposition identity failed: max diff = {decomp_diff:.2e} > {tol_decomp:.2e}"
                )

        # Verify closed-cell Gauss divergence: sum sigma_cf * S_f == 0
        closure = np.zeros_like(self.cell_centres)
        for f in range(self.num_faces):
            o = self.owner[f]
            closure[o] += self.face_areas[f]
            nb = self.neighbour[f]
            if nb >= 0:
                closure[nb] -= self.face_areas[f]
        max_closure = float(np.max(np.linalg.norm(closure, axis=1)))
        if max_closure > tol_closure:
            raise ValueError(
                f"Closed-cell Gauss divergence theorem violated: max error = {max_closure:.2e} > {tol_closure:.2e}"
            )

        return True

    def compute_quality_report(self) -> MeshQualityReport:
        """Evaluate and return a structured quality audit report."""
        closure = np.zeros_like(self.cell_centres)
        for f in range(self.num_faces):
            o = self.owner[f]
            closure[o] += self.face_areas[f]
            nb = self.neighbour[f]
            if nb >= 0:
                closure[nb] -= self.face_areas[f]
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
