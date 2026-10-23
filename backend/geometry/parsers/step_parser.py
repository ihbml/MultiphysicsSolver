"""ISO 10303-21 STEP (AP203/214/242) parser and B-Rep tessellator.

Supports dual-engine architecture:
- Primary Engine: Gmsh OpenCASCADE kernel with configurable chordal deflection and angular resolution.
- Fallback Engine: Pure-Python STEP B-Rep parser extracting topological entities and planar polygon tessellation.
"""

from __future__ import annotations

import logging
from pathlib import Path
import re
from typing import Any, Dict, List, Optional, Set, Tuple, Union
import numpy as np

from backend.geometry.models import SurfaceMesh
from backend.geometry.parsers.base_parser import BaseParser

logger = logging.getLogger(__name__)

try:
    import gmsh
    HAS_GMSH = True
except ImportError:
    HAS_GMSH = False


class STEPParser(BaseParser):
    """Parser for ISO STEP AP203, AP214, and AP242 CAD files."""

    def __init__(
        self,
        deflection: float = 0.0001,
        angular_tolerance: float = 0.1745,
        min_edge: float = 0.00005,
        max_edge: float = 0.05,
        engine: str = "auto",
    ) -> None:
        """Initialize STEP Parser.

        Args:
            deflection: Linear chordal deflection tolerance in meters (default 0.1 mm).
            angular_tolerance: Angular deflection tolerance in radians (default 10 deg / 0.1745 rad).
            min_edge: Minimum element edge length in meters (default 0.05 mm).
            max_edge: Maximum element edge length in meters (default 50 mm).
            engine: Ingestion engine: 'auto', 'gmsh', or 'brep'.
        """
        self.deflection = deflection
        self.angular_tolerance = angular_tolerance
        self.min_edge = min_edge
        self.max_edge = max_edge
        self.engine = engine.lower()

    def parse(self, file_path: Union[str, Path], **kwargs: Any) -> SurfaceMesh:
        """Parse STEP file into a SurfaceMesh.

        Args:
            file_path: Path to STEP file (.stp or .step).
            **kwargs: Engine overrides (e.g. engine='brep' or 'gmsh', deflection=0.001).

        Returns:
            SurfaceMesh containing vertices, faces, normals, patches, and metadata.
        """
        path = self.validate_file(file_path)
        engine = kwargs.get("engine", self.engine).lower()
        deflection = kwargs.get("deflection", self.deflection)
        angular_tol = kwargs.get("angular_tolerance", self.angular_tolerance)
        min_edge = kwargs.get("min_edge", self.min_edge)
        max_edge = kwargs.get("max_edge", self.max_edge)

        # Detect physical units from STEP header/context
        detected_units = self.detect_units(path)

        if engine == "brep":
            return self._parse_with_brep(path, detected_units)

        if engine == "gmsh":
            if not HAS_GMSH:
                raise RuntimeError("Gmsh is requested but not installed in the active environment.")
            return self._parse_with_gmsh(
                path, deflection, angular_tol, min_edge, max_edge, detected_units
            )

        # engine == "auto":
        # For polyhedral B-Rep models, pure B-Rep gives the exact topological representation.
        # For curved or complex models, Gmsh provides full OpenCASCADE B-Rep tessellation.
        try:
            return self._parse_with_brep(path, detected_units)
        except Exception as ex:
            logger.info("Direct B-Rep parsing fell back to Gmsh tessellation: %s", ex)
            if HAS_GMSH:
                return self._parse_with_gmsh(
                    path, deflection, angular_tol, min_edge, max_edge, detected_units
                )
            raise

    @staticmethod
    def detect_units(path: Path) -> str:
        """Inspect STEP file header to detect length units ('mm', 'm', 'in')."""
        try:
            with open(path, "r", encoding="utf-8", errors="replace") as f:
                content = f.read(65536)  # Read first 64KB
            if "SI_UNIT(.MILLI.,.METRE.)" in content:
                return "mm"
            if "SI_UNIT($,.METRE.)" in content:
                return "m"
            if "'INCH'" in content.upper():
                return "in"
        except Exception:
            pass
        return "mm"  # Default mechanical CAD standard

    def _parse_with_gmsh(
        self,
        path: Path,
        deflection: float,
        angular_tol: float,
        min_edge: float,
        max_edge: float,
        units: str,
    ) -> SurfaceMesh:
        """Parse and tessellate STEP model using Gmsh OpenCASCADE kernel."""
        if not gmsh.isInitialized():
            gmsh.initialize()
        gmsh.clear()

        gmsh.option.setNumber("General.Terminal", 0)
        gmsh.option.setNumber("General.Verbosity", 0)
        gmsh.option.setNumber("Mesh.Algorithm", 6)  # Frontal-Delaunay for 2D surfaces

        # Determine unit scaling factor from meters to CAD model units
        if units == "mm":
            to_model_units = 1000.0
        elif units == "in":
            to_model_units = 1000.0 / 25.4
        else:
            to_model_units = 1.0

        scaled_min = min_edge * to_model_units if min_edge > 0 else 0.0
        scaled_max = max_edge * to_model_units if max_edge > 0 else 0.0

        # Configure chordal and curvature deflection controls
        if deflection > 0:
            pts_per_circle = max(int(2.0 * np.pi / max(angular_tol, 0.01)), 6)
            gmsh.option.setNumber("Mesh.MeshSizeFromCurvature", pts_per_circle)
        if scaled_min > 0:
            gmsh.option.setNumber("Mesh.CharacteristicLengthMin", scaled_min)
        if scaled_max > 0:
            gmsh.option.setNumber("Mesh.CharacteristicLengthMax", scaled_max)

        gmsh.open(str(path))
        gmsh.model.mesh.generate(2)  # 2D surface triangulation

        # Extract nodes (vertices)
        node_tags, coords_flat, _ = gmsh.model.mesh.getNodes()
        if len(node_tags) == 0:
            raise ValueError(f"No mesh nodes generated by Gmsh for: {path}")

        vertices = coords_flat.reshape(-1, 3).astype(np.float64)
        tag_to_idx = {int(tag): idx for idx, tag in enumerate(node_tags)}

        # Extract 2D elements (triangles)
        patches: Dict[str, List[int]] = {}
        all_faces: List[List[int]] = []

        # Query all 2D geometrical surfaces
        surfaces = gmsh.model.getEntities(dim=2)
        face_counter = 0

        for dim, s_tag in surfaces:
            e_types, e_tags, e_node_tags = gmsh.model.mesh.getElements(dim=2, tag=s_tag)
            patch_name = f"patch_{s_tag}"
            patches[patch_name] = []

            for et, ent_list in zip(e_types, e_node_tags):
                if et == 2:  # 3-node triangle
                    tris = np.array(ent_list, dtype=np.int64).reshape(-1, 3)
                    for t in tris:
                        idx0 = tag_to_idx[int(t[0])]
                        idx1 = tag_to_idx[int(t[1])]
                        idx2 = tag_to_idx[int(t[2])]
                        all_faces.append([idx0, idx1, idx2])
                        patches[patch_name].append(face_counter)
                        face_counter += 1

        if not all_faces:
            # If per-surface extraction was empty, fallback to global 2D elements
            e_types, e_tags, e_node_tags = gmsh.model.mesh.getElements(dim=2)
            for et, ent_list in zip(e_types, e_node_tags):
                if et == 2:
                    tris = np.array(ent_list, dtype=np.int64).reshape(-1, 3)
                    for t in tris:
                        all_faces.append(
                            [tag_to_idx[int(t[0])], tag_to_idx[int(t[1])], tag_to_idx[int(t[2])]]
                        )
            patches["solid"] = list(range(len(all_faces)))

        faces_arr = np.array(all_faces, dtype=np.int32)
        patch_arrays = {
            k: np.array(v, dtype=np.int32) for k, v in patches.items() if len(v) > 0
        }

        return SurfaceMesh(
            vertices=vertices,
            faces=faces_arr,
            patches=patch_arrays,
            metadata={"source_file": str(path), "format": "step_gmsh", "units": units},
        )

    def _parse_with_brep(self, path: Path, units: str) -> SurfaceMesh:
        """Parse STEP file using pure-Python ISO 10303-21 B-Rep entity graph traversal."""
        with open(path, "r", encoding="utf-8", errors="replace") as f:
            raw_text = f.read()

        # Remove comments /* ... */
        clean_text = re.sub(r"/\*.*?\*/", "", raw_text, flags=re.DOTALL)
        statements = clean_text.split(";")

        entities: Dict[int, Tuple[str, str]] = {}
        for stmt in statements:
            stmt = stmt.strip()
            if not stmt or not stmt.startswith("#"):
                continue
            m = re.match(
                r"#(\d+)\s*=\s*(?:([A-Za-z0-9_]+)\s*\((.*)\)|(?:\((.*)\)))", stmt, re.DOTALL
            )
            if m:
                eid = int(m.group(1))
                etype = m.group(2) or "COMPLEX"
                args = m.group(3) or m.group(4) or ""
                entities[eid] = (etype.upper(), args)

        # 1. Extract CARTESIAN_POINT entities
        # e.g., CARTESIAN_POINT('', (x, y, z))
        cartesian_points: Dict[int, np.ndarray] = {}
        for eid, (etype, args) in entities.items():
            if etype == "CARTESIAN_POINT":
                coord_match = re.search(r"\(\s*([-\d.eE+]+)\s*,\s*([-\d.eE+]+)\s*,\s*([-\d.eE+]+)\s*\)", args)
                if coord_match:
                    coords = [float(coord_match.group(i)) for i in range(1, 4)]
                    cartesian_points[eid] = np.array(coords, dtype=np.float64)

        # 2. Extract VERTEX_POINT entities
        # e.g., VERTEX_POINT('', #cp_id)
        vertex_points: Dict[int, int] = {}
        for eid, (etype, args) in entities.items():
            if etype == "VERTEX_POINT":
                ref = re.search(r"#(\d+)", args)
                if ref:
                    vertex_points[eid] = int(ref.group(1))

        # 3. Extract EDGE_CURVE entities
        # e.g., EDGE_CURVE('', #start_vp, #end_vp, #curve, .T./.F.)
        edge_curves: Dict[int, Tuple[int, int]] = {}
        for eid, (etype, args) in entities.items():
            if etype == "EDGE_CURVE":
                refs = re.findall(r"#(\d+)", args)
                if len(refs) >= 2:
                    edge_curves[eid] = (int(refs[0]), int(refs[1]))

        # 4. Extract ORIENTED_EDGE entities
        # e.g., ORIENTED_EDGE('', *, *, #edge_curve, .T./.F.)
        oriented_edges: Dict[int, Tuple[int, bool]] = {}
        for eid, (etype, args) in entities.items():
            if etype == "ORIENTED_EDGE":
                edge_ref = re.search(r"#(\d+)", args)
                orient_match = re.search(r"\.([TF])\.", args)
                if edge_ref:
                    is_true = orient_match.group(1) == "T" if orient_match else True
                    oriented_edges[eid] = (int(edge_ref.group(1)), is_true)

        # 5. Extract EDGE_LOOP entities
        # e.g., EDGE_LOOP('', (#oe1, #oe2, ...))
        edge_loops: Dict[int, List[int]] = {}
        for eid, (etype, args) in entities.items():
            if etype == "EDGE_LOOP":
                oe_refs = [int(x) for x in re.findall(r"#(\d+)", args)]
                edge_loops[eid] = oe_refs

        # 6. Extract FACE_OUTER_BOUND / FACE_BOUND entities
        # e.g., FACE_OUTER_BOUND('', #edge_loop, .T.)
        face_bounds: Dict[int, int] = {}
        for eid, (etype, args) in entities.items():
            if etype in ("FACE_OUTER_BOUND", "FACE_BOUND"):
                loop_ref = re.search(r"#(\d+)", args)
                if loop_ref:
                    face_bounds[eid] = int(loop_ref.group(1))

        # 7. Extract ADVANCED_FACE entities
        # e.g., ADVANCED_FACE('', (#bound, ...), #surface, .T./.F.)
        advanced_faces: List[int] = [
            eid for eid, (etype, _) in entities.items() if etype == "ADVANCED_FACE"
        ]

        if not advanced_faces:
            raise ValueError(f"No ADVANCED_FACE entities found in STEP B-Rep file: {path}")

        # Assemble polygon loops and triangulate
        unique_pts: List[np.ndarray] = []
        point_to_idx: Dict[Tuple[float, float, float], int] = {}
        all_triangles: List[List[int]] = []
        patches: Dict[str, List[int]] = {}

        def get_or_add_point(pt: np.ndarray, eps: float = 1e-7) -> int:
            key = (round(float(pt[0]) / eps), round(float(pt[1]) / eps), round(float(pt[2]) / eps))
            if key not in point_to_idx:
                idx = len(unique_pts)
                point_to_idx[key] = idx
                unique_pts.append(pt)
                return idx
            return point_to_idx[key]

        for face_num, af_id in enumerate(advanced_faces):
            _, args = entities[af_id]
            bound_refs = [int(x) for x in re.findall(r"#(\d+)", args)]
            # Find loop reference
            face_pts: List[int] = []

            for b_id in bound_refs:
                if b_id in face_bounds:
                    loop_id = face_bounds[b_id]
                    oe_list = edge_loops.get(loop_id, [])
                    # Traverse oriented edges
                    for oe_id in oe_list:
                        if oe_id in oriented_edges:
                            ec_id, forward = oriented_edges[oe_id]
                            if ec_id in edge_curves:
                                vp_start, vp_end = edge_curves[ec_id]
                                active_vp = vp_start if forward else vp_end
                                cp_id = vertex_points.get(active_vp)
                                if cp_id and cp_id in cartesian_points:
                                    pt_idx = get_or_add_point(cartesian_points[cp_id])
                                    if not face_pts or face_pts[-1] != pt_idx:
                                        face_pts.append(pt_idx)

            # Deduplicate closing vertex if first == last
            if len(face_pts) > 1 and face_pts[0] == face_pts[-1]:
                face_pts.pop()

            if len(face_pts) < 3:
                continue

            # Triangulate planar face via fan triangulation
            patch_name = f"face_{af_id}"
            patches[patch_name] = []

            v0 = face_pts[0]
            for i in range(1, len(face_pts) - 1):
                tri = [v0, face_pts[i], face_pts[i + 1]]
                tri_idx = len(all_triangles)
                all_triangles.append(tri)
                patches[patch_name].append(tri_idx)

        if not all_triangles:
            raise ValueError(f"Failed to extract topological triangles from STEP file: {path}")

        v_arr = np.array(unique_pts, dtype=np.float64)
        f_arr = np.array(all_triangles, dtype=np.int32)
        patch_arrays = {k: np.array(v, dtype=np.int32) for k, v in patches.items() if len(v) > 0}

        return SurfaceMesh(
            vertices=v_arr,
            faces=f_arr,
            patches=patch_arrays,
            metadata={"source_file": str(path), "format": "step_brep", "units": units},
        )
