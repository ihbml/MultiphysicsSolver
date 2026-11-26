"""Configuration schemas and element enums for the CFDSolver meshing backbone."""

from __future__ import annotations

from dataclasses import asdict, dataclass, field
from enum import IntEnum
from typing import Any, Dict, Literal, Optional, Tuple


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
    h_min: Optional[float] = None
    h_max: Optional[float] = None
    growth_rate: float = 1.2
    h0: float = 0.050
    grading: float = 0.350
    eps_curv: float = 1e-6
    max_gap_search: Optional[float] = None
    ray_march_steps: int = 8
    theta_opp_threshold: float = -0.500
    fd_delta: float = 1e-4

    def __post_init__(self) -> None:
        if self.h_min is None:
            self.h_min = float(self.h0) * 0.25
        if self.h_max is None:
            self.h_max = float(self.h0) * 4.0
        if self.growth_rate != 1.2 and self.grading == 0.350:
            self.grading = self.growth_rate - 1.0 if self.growth_rate > 1.0 else self.growth_rate

    def to_dict(self) -> Dict[str, Any]:
        return asdict(self)

    @classmethod
    def from_dict(cls, data: Dict[str, Any]) -> "CurvatureSizingConfig":
        return cls(**{k: v for k, v in data.items() if k in cls.__dataclass_fields__})

    def to_sizing_parameters(self) -> "SizingParameters":
        return SizingParameters(
            h_min=float(self.h_min) if self.h_min is not None else float(self.h0) * 0.25,
            h_max=float(self.h_max) if self.h_max is not None else float(self.h0) * 4.0,
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
