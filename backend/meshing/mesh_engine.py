"""Unified High-Level In-Memory Mesh Engine Facade & Pure-Python Fallback.

Orchestrates native C++20 acceleration via cfd_mesher_native and focused submodules:
- _engine_zones.py: closed-shell orientation normalization, CHT shell zones, farfield bounds, Lp metrics
- _engine_python_fallback.py: pure-Python/NumPy fallback mesh generators
"""

from __future__ import annotations

import copy
import ctypes
import logging
import os
from pathlib import Path
from typing import Any, Dict, List, Optional, Tuple, Union
import numpy as np

from backend.geometry.models import SurfaceMesh
from backend.geometry.pipeline import CADGeometryEngine
from backend.meshing._engine_python_fallback import (
    detect_oriented_box_axes,
    mesh_2d_polygon_python,
    mesh_cht_coupled_python,
    mesh_complex_surface_python,
    mesh_external_fluid_python,
    mesh_from_points_cells_python,
    mesh_multi_region_python,
    mesh_oriented_box_python,
)
from backend.meshing._engine_zones import (
    _connected_face_components,
    _decompose_cht_shell_zones,
    _generalized_winding_number,
    _signed_shell_volume,
    compute_farfield_bounding_box,
    compute_farfield_bounds_from_config,
    compute_lp_metric_tensor,
    default_config_for_h0,
    normalize_closed_shell_orientations,
    reconstruct_p2_edge_midpoints,
)
from backend.meshing.difvm_topology import (
    MeshingConfigC,
    extract_cht_twin_faces,
    generate_hexcore_native,
    get_native_dll,
    get_native_load_error,
)
from backend.meshing.distmesh_core import (
    canonicalize_polygon_ccw,
    ear_clipping_triangulate,
    rcm_reorder,
)
from backend.meshing.gci import GCIHierarchy, generate_3grid_gci_hierarchy
from backend.meshing.inflation import BoundaryLayerGenerator
from backend.meshing.models import (
    InflationConfig,
    MeshingConfig,
    UnstructuredMesh,
)
from backend.meshing.native_loader import _env_strict_native, raise_native_status_error

logger = logging.getLogger(__name__)


class MeshEngine:
    """Unified in-memory mesher facade supporting both native C++20 and pure-Python execution."""

    def __init__(
        self,
        use_native: bool = True,
        config: Optional[MeshingConfig] = None,
        strict_native: Optional[bool] = None,
        allow_python_fallback: bool = False,
    ) -> None:
        self.use_native = use_native
        self.config = config
        self.allow_python_fallback = allow_python_fallback
        env_strict = _env_strict_native()
        if strict_native is None:
            self.strict_native = env_strict or (use_native and not allow_python_fallback)
        else:
            self.strict_native = env_strict or bool(strict_native)

        if self.strict_native and not use_native:
            raise RuntimeError("strict_native=True requires use_native=True.")
        self._native_dll = get_native_dll(strict_native=self.strict_native) if use_native else None
        if self.strict_native and self._native_dll is None:
            load_err = get_native_load_error()
            raise RuntimeError(f"Strict native mode enabled but native mesher DLL could not be loaded: {load_err}")

    @property
    def has_native_core(self) -> bool:
        """Check if compiled native C++20 library is active."""
        return self._native_dll is not None

    def mesh_cad_file(
        self,
        cad_path: Union[str, Path],
        h0: Optional[float] = None,
        scale: float = 1.0,
        config: Optional[MeshingConfig] = None,
        domain_mode: Optional[str] = None,
    ) -> UnstructuredMesh:
        """Ingest, filter, and mesh a CAD geometry file (.stp, .stl, .obj, .prt)."""
        path = Path(cad_path).resolve()
        if not path.exists():
            raise FileNotFoundError(f"CAD file not found: {path}")

        base_cfg = config if config is not None else self.config
        if h0 is None:
            if base_cfg is not None and base_cfg.sizing is not None and base_cfg.sizing.h0 > 0.0:
                eff_h0 = float(base_cfg.sizing.h0)
                active_cfg = base_cfg
            elif base_cfg is not None:
                eff_h0 = 0.05
                active_cfg = base_cfg
            else:
                eff_h0 = 0.05
                active_cfg = default_config_for_h0(eff_h0)
        else:
            eff_h0 = float(h0)
            active_cfg = base_cfg if base_cfg is not None else default_config_for_h0(eff_h0)

        if domain_mode is not None:
            active_cfg = MeshingConfig.from_dict({**active_cfg.to_dict(), "domain_mode": domain_mode})

        if path.is_dir():
            return self.mesh_multi_region(path, h0=eff_h0, scale=scale, config=active_cfg)

        cad_engine = CADGeometryEngine()
        cad_engine.load(path)
        sm: SurfaceMesh = cad_engine.current_mesh
        if sm is None or len(sm.vertices) == 0:
            raise ValueError(f"Failed to extract 2-manifold surface mesh from CAD file: {path}")

        verts = sm.vertices * scale
        norm_faces, shell_stats = normalize_closed_shell_orientations(verts, sm.faces)
        is_solid_only = (active_cfg.domain_mode == "solid_only")

        if is_solid_only and not (self.use_native and self.has_native_core):
            R = self._detect_oriented_box_axes(verts, norm_faces)
            if R is not None:
                mesh = self._mesh_oriented_box(verts, R, h0=eff_h0, active_cfg=active_cfg)
                mesh.metadata["shell_orientation"] = shell_stats
                mesh.metadata["surface_open_edges"] = int(shell_stats["surface_open_edges"])
                return mesh

        clean_mesh = SurfaceMesh(
            vertices=verts,
            faces=norm_faces,
            normals=sm.normals.copy() if sm.normals is not None else None,
            patches=dict(sm.patches) if sm.patches else {},
        )
        return self.mesh_hexcore(
            clean_mesh,
            scale=1.0,
            h0=eff_h0,
            domain_mode=active_cfg.domain_mode,
            config=active_cfg,
        )

    def generate_gci_hierarchy(
        self,
        cad_path: Union[str, Path, SurfaceMesh],
        base_h0: float = 0.05,
        r: float = 1.41421356,
        scale: float = 1.0,
        config: Optional[MeshingConfig] = None,
        **kwargs: Any,
    ) -> GCIHierarchy:
        """Generate a deterministic 3-grid GCI hierarchy (fine, medium, coarse) per ASME V&V 20-2009."""
        active_cfg = (
            config
            if config is not None
            else (self.config if self.config is not None else default_config_for_h0(base_h0))
        )

        def _generator(h: float) -> UnstructuredMesh:
            if isinstance(cad_path, SurfaceMesh):
                return self.mesh_hexcore(cad_path, h0=h, scale=scale, config=active_cfg, **kwargs)
            p = Path(cad_path).resolve()
            if p.is_dir():
                return self.mesh_multi_region(p, h0=h, scale=scale, config=active_cfg)
            return self.mesh_cad_file(p, h0=h, scale=scale, config=active_cfg, **kwargs)

        return generate_3grid_gci_hierarchy(_generator, base_h0=base_h0, r=r)

    def mesh_hexcore(
        self,
        surface: SurfaceMesh,
        h0: float = 0.05,
        scale: float = 1.0,
        domain_mode: Optional[str] = None,
        farfield_bbox: Optional[Union[Tuple[float, float, float, float, float, float], Tuple[Tuple[float, float, float], Tuple[float, float, float]]]] = None,
        config: Optional[MeshingConfig] = None,
    ) -> UnstructuredMesh:
        """Generate 3D 2:1 balanced adaptive Cartesian Octree and Mosaic Poly-Hexcore mesh."""
        valid_modes = {"external_fluid", "internal_fluid", "solid_only", "cht_coupled"}
        if domain_mode is not None and domain_mode not in valid_modes:
            raise ValueError(f"Unsupported domain_mode {domain_mode!r}; expected one of {sorted(valid_modes)}")

        active_cfg = (
            config
            if config is not None
            else (
                self.config
                if self.config is not None
                else default_config_for_h0(h0, domain_mode=domain_mode or "external_fluid")
            )
        )
        effective_domain_mode = domain_mode if domain_mode is not None else active_cfg.domain_mode
        if effective_domain_mode not in valid_modes:
            raise ValueError(
                f"Unsupported config.domain_mode {effective_domain_mode!r}; expected one of {sorted(valid_modes)}"
            )

        scaled_verts = surface.vertices * scale
        norm_faces, shell_stats = normalize_closed_shell_orientations(scaled_verts, surface.faces)
        scaled_surf = SurfaceMesh(
            vertices=scaled_verts,
            faces=norm_faces,
            normals=surface.normals.copy() if surface.normals is not None else None,
            patches=dict(surface.patches) if surface.patches else {},
        )
        if effective_domain_mode == "external_fluid" and farfield_bbox is None:
            bmin = np.min(scaled_surf.vertices, axis=0)
            bmax = np.max(scaled_surf.vertices, axis=0)
            ff_min, ff_max = self._compute_farfield_bounds(bmin, bmax, active_cfg)
            farfield_bbox = (
                float(ff_min[0]),
                float(ff_min[1]),
                float(ff_min[2]),
                float(ff_max[0]),
                float(ff_max[1]),
                float(ff_max[2]),
            )

        is_strict = self.strict_native or _env_strict_native()
        if self.use_native and self.has_native_core and len(scaled_surf.faces) > 0:
            try:
                if effective_domain_mode == "cht_coupled":
                    cht_zones = _decompose_cht_shell_zones(scaled_surf)
                    if len(cht_zones) >= 2:
                        solid_cfg = copy.copy(active_cfg)
                        solid_cfg.rcm_reorder = False
                        outer_faces = np.concatenate(
                            [
                                z_f
                                for z_name, z_f in cht_zones
                                if z_name.startswith("solid_0")
                                or (z_name.startswith("solid_") and not z_name.startswith("solid_insert_"))
                                or z_name.startswith("duct_")
                            ]
                        )
                        native_mesh = generate_hexcore_native(
                            surf_points=scaled_surf.vertices,
                            surf_faces=scaled_surf.faces[outer_faces],
                            h0=h0,
                            domain_mode="solid_only",
                            farfield_bbox=farfield_bbox,
                            config=solid_cfg,
                        )
                        verts_arr = np.asarray(scaled_surf.vertices, dtype=np.float64)
                        faces_arr = np.asarray(scaled_surf.faces, dtype=np.int32)
                        if native_mesh.num_cells > 0:
                            regions = np.zeros(native_mesh.num_cells, dtype=np.int32)
                            for reg_id in range(1, len(cht_zones)):
                                _z_name, z_f_idx = cht_zones[reg_id]
                                if len(z_f_idx) < 4:
                                    continue
                                wn = np.abs(
                                    _generalized_winding_number(
                                        verts_arr, faces_arr[z_f_idx], native_mesh.cell_centres
                                    )
                                )
                                regions[wn > 0.5] = reg_id
                            native_mesh.cell_regions = regions
                        zone_names = [z_name for z_name, _ in cht_zones]
                        native_mesh.metadata["cht_zone_names"] = zone_names
                        native_mesh.metadata["shell_orientation"] = shell_stats
                        native_mesh.metadata["surface_open_edges"] = int(shell_stats["surface_open_edges"])
                        out_mesh = extract_cht_twin_faces(native_mesh)
                        out_mesh.metadata.setdefault("cht_zone_names", zone_names)
                        out_mesh.metadata.setdefault("shell_orientation", shell_stats)
                        out_mesh.metadata.setdefault("surface_open_edges", int(shell_stats["surface_open_edges"]))
                        return out_mesh

                native_mesh = generate_hexcore_native(
                    surf_points=scaled_surf.vertices,
                    surf_faces=scaled_surf.faces,
                    h0=h0,
                    domain_mode=effective_domain_mode,
                    farfield_bbox=farfield_bbox,
                    config=active_cfg,
                )
                if active_cfg.rcm_reorder and not native_mesh.metadata.get("rcm_reordered", False):
                    native_mesh, _ = rcm_reorder(native_mesh)
                native_mesh.metadata["shell_orientation"] = shell_stats
                native_mesh.metadata["surface_open_edges"] = int(shell_stats["surface_open_edges"])
                return native_mesh
            except MemoryError:
                raise
            except Exception as exc:
                if is_strict:
                    raise RuntimeError(f"Strict native mode enabled and generate_hexcore_native failed: {exc}") from exc
                logger.error(f"Native hexcore failed, falling back to Python: {exc}", exc_info=True)
        elif is_strict:
            load_err = get_native_load_error()
            raise RuntimeError(
                f"Strict native mode enabled but native mesher core is unavailable or surface is empty (error: {load_err})."
            )

        if effective_domain_mode == "cht_coupled":
            mesh = self._mesh_cht_coupled(scaled_surf, scale=1.0, h0=h0, active_cfg=active_cfg)
        elif effective_domain_mode == "solid_only":
            mesh = self._mesh_complex_surface(scaled_surf, scale=1.0, h0=h0, active_cfg=active_cfg)
        else:
            mesh = self._mesh_external_fluid(scaled_surf, scale=1.0, h0=h0, active_cfg=active_cfg)
        mesh.metadata["shell_orientation"] = shell_stats
        mesh.metadata["surface_open_edges"] = int(shell_stats["surface_open_edges"])
        return mesh

    @staticmethod
    def _detect_oriented_box_axes(verts: np.ndarray, faces: np.ndarray) -> Optional[np.ndarray]:
        return detect_oriented_box_axes(verts, faces)

    def _mesh_oriented_box(
        self,
        verts: np.ndarray,
        R: np.ndarray,
        h0: float,
        active_cfg: MeshingConfig,
    ) -> UnstructuredMesh:
        return mesh_oriented_box_python(verts, R, h0=h0, active_cfg=active_cfg)

    @staticmethod
    def _compute_farfield_bounds(
        geom_bmin: np.ndarray,
        geom_bmax: np.ndarray,
        config: MeshingConfig,
    ) -> Tuple[np.ndarray, np.ndarray]:
        return compute_farfield_bounds_from_config(geom_bmin, geom_bmax, config)

    def _mesh_external_fluid(
        self,
        surface: SurfaceMesh,
        scale: float,
        h0: float,
        active_cfg: MeshingConfig,
    ) -> UnstructuredMesh:
        scaled_verts = surface.vertices * scale
        norm_faces, shell_stats = normalize_closed_shell_orientations(scaled_verts, surface.faces)
        scaled_surf = SurfaceMesh(
            vertices=scaled_verts,
            faces=norm_faces,
            normals=surface.normals.copy() if surface.normals is not None else None,
            patches=dict(surface.patches) if surface.patches else {},
        )
        bmin = np.min(scaled_surf.vertices, axis=0)
        bmax = np.max(scaled_surf.vertices, axis=0)
        ff_min, ff_max = self._compute_farfield_bounds(bmin, bmax, active_cfg)

        is_strict = self.strict_native or _env_strict_native()
        if self.use_native and self.has_native_core and len(scaled_surf.faces) > 0:
            try:
                ff_bbox = (
                    float(ff_min[0]), float(ff_min[1]), float(ff_min[2]),
                    float(ff_max[0]), float(ff_max[1]), float(ff_max[2]),
                )
                native_mesh = generate_hexcore_native(
                    surf_points=scaled_surf.vertices,
                    surf_faces=scaled_surf.faces,
                    h0=h0,
                    domain_mode=active_cfg.domain_mode,
                    farfield_bbox=ff_bbox,
                    config=active_cfg,
                )
                if active_cfg.rcm_reorder and not native_mesh.metadata.get("rcm_reordered", False):
                    native_mesh, _ = rcm_reorder(native_mesh)
                native_mesh.metadata["shell_orientation"] = shell_stats
                native_mesh.metadata["surface_open_edges"] = int(shell_stats["surface_open_edges"])
                return native_mesh
            except MemoryError:
                raise
            except Exception as exc:
                if is_strict:
                    raise RuntimeError(f"Strict native mode enabled and native hexcore mesher failed: {exc}") from exc
                logger.error(f"Native hexcore mesher failed, falling back to Python: {exc}", exc_info=True)
        elif is_strict:
            load_err = get_native_load_error()
            raise RuntimeError(
                f"Strict native mode enabled but native mesher core is unavailable or surface is empty (error: {load_err})."
            )

        mesh = mesh_external_fluid_python(scaled_surf, h0=h0, active_cfg=active_cfg, ff_min=ff_min, ff_max=ff_max)
        mesh.metadata["shell_orientation"] = shell_stats
        mesh.metadata["surface_open_edges"] = int(shell_stats["surface_open_edges"])
        return mesh

    def _mesh_cht_coupled(
        self,
        surface: SurfaceMesh,
        scale: float,
        h0: float,
        active_cfg: MeshingConfig,
    ) -> UnstructuredMesh:
        return mesh_cht_coupled_python(surface, scale=scale, h0=h0, active_cfg=active_cfg)

    def mesh_multi_region(
        self,
        regions: Union[str, Path, Dict[str, Union[str, Path, SurfaceMesh]], List[Union[str, Path, SurfaceMesh]]],
        h0: float = 0.05,
        scale: float = 1.0,
        config: Optional[MeshingConfig] = None,
        binary_regions: bool = False,
    ) -> UnstructuredMesh:
        """Mesh multiple interacting CAD regions (e.g. snappyMultiRegionHeater)."""
        active_cfg = (
            config
            if config is not None
            else (self.config if self.config is not None else default_config_for_h0(h0, domain_mode="cht_coupled"))
        )
        return mesh_multi_region_python(
            regions=regions,
            h0=h0,
            scale=scale,
            active_cfg=active_cfg,
            binary_regions=binary_regions,
        )

    def _mesh_complex_surface(
        self,
        surface: SurfaceMesh,
        scale: float,
        h0: float,
        active_cfg: MeshingConfig,
    ) -> UnstructuredMesh:
        return mesh_complex_surface_python(surface, scale=scale, h0=h0, active_cfg=active_cfg)

    def _mesh_from_points_cells(
        self,
        verts: np.ndarray,
        h0: float,
        active_cfg: MeshingConfig,
    ) -> UnstructuredMesh:
        return mesh_from_points_cells_python(verts, h0=h0, active_cfg=active_cfg)

    def mesh_2d_polygon(
        self,
        polygon_xy: np.ndarray,
        config: Optional[MeshingConfig] = None,
    ) -> UnstructuredMesh:
        """Generate 2D unstructured mesh from a 2D boundary polygon."""
        active_cfg = config if config is not None else self.config
        poly, _, _ = canonicalize_polygon_ccw(polygon_xy)
        n_verts = len(poly)

        h0 = 0.0
        if active_cfg is not None and active_cfg.sizing and active_cfg.sizing.h0 > 0.0:
            h0 = active_cfg.sizing.h0
        elif active_cfg is not None and active_cfg.sizing and active_cfg.sizing.h_min and active_cfg.sizing.h_min > 0.0:
            h0 = active_cfg.sizing.h_min * 2.0

        is_strict = self.strict_native or _env_strict_native()
        if self.use_native and self._native_dll is not None:
            m_ptr = self._native_dll.cfd_mesher_create()
            if m_ptr:
                try:
                    cfg_c = MeshingConfigC()
                    cfg_c.enable_rcm = 1 if (active_cfg and active_cfg.rcm_reorder) else 0
                    cfg_c.distmesh_iters = active_cfg.distmesh_iters if active_cfg else 45
                    cfg_c.h0 = h0
                    if active_cfg and active_cfg.sizing:
                        cfg_c.h_min = float(active_cfg.sizing.h_min) if active_cfg.sizing.h_min is not None else h0 * 0.25
                        cfg_c.h_max = float(active_cfg.sizing.h_max) if active_cfg.sizing.h_max is not None else h0 * 4.0
                        cfg_c.grading = active_cfg.sizing.grading

                    self._native_dll.cfd_mesher_generate_2d_polygon.restype = ctypes.c_int32
                    self._native_dll.cfd_mesher_generate_2d_polygon.argtypes = [
                        ctypes.c_void_p,
                        ctypes.POINTER(ctypes.c_double),
                        ctypes.c_int64,
                        ctypes.POINTER(MeshingConfigC),
                    ]
                    res = self._native_dll.cfd_mesher_generate_2d_polygon(
                        m_ptr,
                        poly.ctypes.data_as(ctypes.POINTER(ctypes.c_double)),
                        n_verts,
                        ctypes.byref(cfg_c),
                    )
                    if res == 0:
                        n_cells = self._native_dll.cfd_mesher_get_n_cells(m_ptr)
                        n_faces = self._native_dll.cfd_mesher_get_n_faces(m_ptr)
                        n_points = self._native_dll.cfd_mesher_get_n_points(m_ptr)

                        owner = np.copy(np.ctypeslib.as_array(self._native_dll.cfd_mesher_get_owner_ptr(m_ptr), shape=(n_faces,)))
                        neighbour = np.copy(np.ctypeslib.as_array(self._native_dll.cfd_mesher_get_neighbour_ptr(m_ptr), shape=(n_faces,)))
                        pts = np.copy(np.ctypeslib.as_array(self._native_dll.cfd_mesher_get_points_ptr(m_ptr), shape=(n_points, 3)))
                        cell_vols = np.copy(np.ctypeslib.as_array(self._native_dll.cfd_mesher_get_cell_volumes_ptr(m_ptr), shape=(n_cells,)))
                        cell_cnts = np.copy(np.ctypeslib.as_array(self._native_dll.cfd_mesher_get_cell_centres_ptr(m_ptr), shape=(n_cells, 3)))
                        fa = np.copy(np.ctypeslib.as_array(self._native_dll.cfd_mesher_get_face_areas_ptr(m_ptr), shape=(n_faces, 3)))
                        fc = np.copy(np.ctypeslib.as_array(self._native_dll.cfd_mesher_get_face_centres_ptr(m_ptr), shape=(n_faces, 3)))
                        df = np.copy(np.ctypeslib.as_array(self._native_dll.cfd_mesher_get_delta_f_ptr(m_ptr), shape=(n_faces, 3)))
                        kf = np.copy(np.ctypeslib.as_array(self._native_dll.cfd_mesher_get_k_f_ptr(m_ptr), shape=(n_faces, 3)))
                        no = np.copy(np.ctypeslib.as_array(self._native_dll.cfd_mesher_get_non_orthogonality_ptr(m_ptr), shape=(n_faces,)))
                        sk = np.copy(np.ctypeslib.as_array(self._native_dll.cfd_mesher_get_skewness_ptr(m_ptr), shape=(n_faces,)))
                        ar = np.copy(np.ctypeslib.as_array(self._native_dll.cfd_mesher_get_aspect_ratio_ptr(m_ptr), shape=(n_cells,)))
                        sm = np.copy(np.ctypeslib.as_array(self._native_dll.cfd_mesher_get_stabilization_mask_ptr(m_ptr), shape=(n_faces,)))

                        offsets_ptr = self._native_dll.cfd_mesher_get_cell_offsets_ptr(m_ptr)
                        nodes_ptr = self._native_dll.cfd_mesher_get_cell_nodes_ptr(m_ptr)
                        cell_offsets = None
                        cell_nodes = None
                        real_cells: List[np.ndarray] = []
                        if offsets_ptr and nodes_ptr:
                            cell_offsets = np.array(
                                np.ctypeslib.as_array(offsets_ptr, shape=(n_cells + 1,)), dtype=np.int64, copy=True
                            )
                            total_nodes = int(cell_offsets[-1])
                            cell_nodes = np.array(
                                np.ctypeslib.as_array(nodes_ptr, shape=(total_nodes,)), dtype=np.int64, copy=True
                            ) if total_nodes > 0 else np.empty(0, dtype=np.int64)
                        else:
                            _, ear_tris = ear_clipping_triangulate(poly)
                            real_cells = [ear_tris[i].copy() for i in range(len(ear_tris))]

                        internal_count = int(np.sum(neighbour >= 0))
                        bnd_count = n_faces - internal_count
                        return UnstructuredMesh(
                            points=pts,
                            cells=real_cells,
                            owner=owner,
                            neighbour=neighbour,
                            face_areas=fa,
                            face_centres=fc,
                            cell_centres=cell_cnts,
                            cell_volumes=cell_vols,
                            delta_f=df,
                            k_f=kf,
                            boundaries={"wall": (internal_count, bnd_count)},
                            error_map={
                                "non_orthogonality": no,
                                "non_ortho": no,
                                "skewness": sk,
                                "skew": sk,
                                "aspect_ratio": ar,
                                "stabilization_mask": sm,
                                "mask": sm,
                            },
                            metadata={"engine": "native_cpp20_2d"},
                            cell_offsets=cell_offsets,
                            cell_nodes=cell_nodes,
                        )
                    else:
                        if is_strict:
                            raise_native_status_error(self._native_dll, m_ptr, "cfd_mesher_generate_2d_polygon", res)
                        logger.error(f"Native 2D mesher failed with code {res}")
                except MemoryError:
                    raise
                except Exception as e:
                    if is_strict:
                        raise RuntimeError(f"Strict native mode enabled and native 2D mesher execution failed: {e}") from e
                    logger.error("Native 2D mesher execution failed: %s, falling back to pure Python", e, exc_info=True)
                finally:
                    self._native_dll.cfd_mesher_free(m_ptr)
        elif is_strict:
            load_err = get_native_load_error()
            raise RuntimeError(f"Strict native mode enabled but native mesher core is unavailable (error: {load_err}).")

        return mesh_2d_polygon_python(poly, h0=h0, active_cfg=active_cfg, use_native=self.use_native)

    def generate_mesh(
        self,
        geometry_or_sdf: Any,
        config: Optional[MeshingConfig] = None,
    ) -> UnstructuredMesh:
        """Unified mesh generation dispatcher."""
        active_cfg = config if config is not None else self.config
        if isinstance(geometry_or_sdf, (str, Path)):
            return self.mesh_cad_file(geometry_or_sdf, config=active_cfg)
        elif isinstance(geometry_or_sdf, np.ndarray) and geometry_or_sdf.ndim == 2 and geometry_or_sdf.shape[1] == 2:
            return self.mesh_2d_polygon(geometry_or_sdf, config=active_cfg)
        elif isinstance(geometry_or_sdf, SurfaceMesh):
            if active_cfg is not None:
                cfg_cad = active_cfg
                if getattr(cfg_cad.sizing, "h0", 0.0) > 0.0:
                    h0_val = float(cfg_cad.sizing.h0)
                elif getattr(cfg_cad.sizing, "h_min", None) is not None:
                    h0_val = float(cfg_cad.sizing.h_min) * 4.0
                else:
                    h0_val = 0.05
            else:
                h0_val = 0.05
                cfg_cad = default_config_for_h0(h0_val)
            return self.mesh_hexcore(
                geometry_or_sdf,
                scale=1.0,
                h0=h0_val,
                domain_mode=cfg_cad.domain_mode,
                config=cfg_cad,
            )
        else:
            raise TypeError(f"Unsupported geometry type for generate_mesh: {type(geometry_or_sdf)}")

    def inflate_boundary_layers(
        self,
        wall_points: np.ndarray,
        wall_faces: List[Union[np.ndarray, List[int]]],
        config: Optional[InflationConfig] = None,
        nominal_normal: Optional[np.ndarray] = None,
        clearances: Optional[np.ndarray] = None,
    ) -> UnstructuredMesh:
        """Generate structured prismatic boundary layers extruded from physical wall facets."""
        active_cfg = (
            config
            if config is not None
            else (self.config.inflation if (self.config and self.config.inflation is not None) else InflationConfig())
        )
        generator = BoundaryLayerGenerator(active_cfg)
        return generator.inflate_layers(
            wall_points=wall_points,
            wall_faces=wall_faces,
            nominal_normal=nominal_normal,
            clearances=clearances,
        )


__all__ = [
    "MeshEngine",
    "compute_farfield_bounding_box",
    "compute_lp_metric_tensor",
    "reconstruct_p2_edge_midpoints",
    "normalize_closed_shell_orientations",
    "_connected_face_components",
    "_signed_shell_volume",
    "_generalized_winding_number",
    "_decompose_cht_shell_zones",
]
