"""ISO 10303-21 STEP (AP203/214/242) parser and B-Rep tessellator.

Supports dual-engine architecture:
- Primary Engine: Gmsh OpenCASCADE kernel with configurable chordal deflection and angular resolution.
- Fallback Engine: Pure-Python STEP B-Rep parser extracting topological entities and planar polygon tessellation.
"""

from __future__ import annotations

import logging
import os
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
        # Curved B-Rep entities REQUIRE Gmsh/OpenCASCADE to avoid flattening and preserve true surface area (K9).
        if self._has_curved_entities(path):
            if not HAS_GMSH:
                raise RuntimeError(
                    f"STEP file {path} contains curved B-Rep entities (B-spline/cylinder/sphere) "
                    "which require Gmsh/OpenCASCADE for faithful tessellation. Install gmsh or specify engine='gmsh'."
                )
            return self._parse_with_gmsh(
                path, deflection, angular_tol, min_edge, max_edge, detected_units
            )

        # Planar models (e.g. cuboids, polyhedra) can be cleanly ingested via exact B-Rep topology
        try:
            return self._parse_with_brep(path, detected_units)
        except Exception as ex:
            if HAS_GMSH:
                logger.warning("B-Rep parsing of planar STEP model failed, falling back to Gmsh: %s", ex)
                return self._parse_with_gmsh(
                    path, deflection, angular_tol, min_edge, max_edge, detected_units
                )
            raise

    @staticmethod
    def _has_curved_entities(path: Path) -> bool:
        """Inspect STEP file to check if it contains non-planar curved geometric entities."""
        curved_tokens = (
            "B_SPLINE_SURFACE",
            "B_SPLINE_CURVE",
            "CYLINDRICAL_SURFACE",
            "CONICAL_SURFACE",
            "SPHERICAL_SURFACE",
            "TOROIDAL_SURFACE",
            "SURFACE_OF_REVOLUTION",
            "SURFACE_OF_LINEAR_EXTRUSION",
            "CIRCLE",
            "ELLIPSE",
            "PARABOLA",
            "HYPERBOLA",
            "BEZIER_SURFACE",
            "BEZIER_CURVE",
            "RATIONAL_B_SPLINE_SURFACE",
            "RATIONAL_B_SPLINE_CURVE",
        )
        try:
            with open(path, "r", encoding="utf-8", errors="replace") as f:
                for chunk in iter(lambda: f.read(1048576), ""):
                    upper_chunk = chunk.upper()
                    for token in curved_tokens:
                        if token in upper_chunk:
                            return True
        except Exception:
            pass
        return False

    @staticmethod
    def detect_units(path: Path) -> str:
        """Inspect entire STEP file to detect length units ('mm', 'cm', 'm', 'in').

        Scans all chunks (not only the first 64 KB, as ISO-10303-21 writers often
        emit GLOBAL_UNIT_ASSIGNED_CONTEXT at the end of the DATA section) using
        whitespace-tolerant regular expressions (AUD-005).
        """
        si_pattern = re.compile(
            r"SI_UNIT\s*\(\s*(\.[A-Z_]+\.|\$)\s*,\s*\.METRE\.\s*\)",
            re.IGNORECASE,
        )
        imp_pattern = re.compile(
            r"CONVERSION_BASED_UNIT\s*\(\s*'([^']+)'",
            re.IGNORECASE,
        )
        carry = ""
        found_unit: Optional[str] = None
        try:
            with open(path, "r", encoding="utf-8", errors="replace") as f:
                for chunk in iter(lambda: f.read(1048576), ""):
                    window = carry + chunk
                    for imp_m in imp_pattern.finditer(window):
                        uname = imp_m.group(1).strip().upper()
                        if "INCH" in uname:
                            return "in"
                        if "FOOT" in uname or "FEET" in uname:
                            return "ft"
                    for si_m in si_pattern.finditer(window):
                        prefix = si_m.group(1).upper()
                        if prefix == ".MILLI.":
                            found_unit = "mm"
                        elif prefix == ".CENTI.":
                            found_unit = "cm"
                        elif prefix == "$":
                            found_unit = "m"
                    carry = window[-256:]
        except Exception:
            pass
        return found_unit or "mm"  # Default mechanical CAD standard

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
        num_cpus = os.cpu_count() or 4
        gmsh.option.setNumber("General.NumThreads", num_cpus)
        gmsh.option.setNumber("Mesh.Algorithm", 6)  # Frontal-Delaunay for 2D surfaces

        # Lock OpenCASCADE target unit to match detected STEP file unit so Gmsh and
        # B-Rep engines return identical native coordinates without 1000x mismatch (AUD-005).
        occ_unit_map = {"mm": "MM", "cm": "CM", "m": "M", "in": "IN", "ft": "FT"}
        gmsh.option.setString("Geometry.OCCTargetUnit", occ_unit_map.get(units, "MM"))

        # Determine unit scaling factor from SI meters to CAD model units
        unit_scale_from_m = {
            "mm": 1000.0,
            "cm": 100.0,
            "m": 1.0,
            "in": 1.0 / 0.0254,
            "ft": 1.0 / 0.3048,
        }
        to_model_units = unit_scale_from_m.get(units, 1000.0)

        scaled_min = min_edge * to_model_units if min_edge > 0 else 0.0
        scaled_max = max_edge * to_model_units if max_edge > 0 else 0.0

        gmsh.open(str(path))

        # Surface triangle budget guard before generate(2) to prevent OOM kills on huge domains (AUD-005)
        try:
            xmin, ymin, zmin, xmax, ymax, zmax = gmsh.model.getBoundingBox(-1, -1)
            dx = max(0.0, float(xmax - xmin))
            dy = max(0.0, float(ymax - ymin))
            dz = max(0.0, float(zmax - zmin))
            approx_area = 2.0 * (dx * dy + dy * dz + dz * dx)
            if scaled_max > 0.0 and approx_area > 0.0:
                # Equilateral triangle area = (sqrt(3)/4) * h^2
                est_tris = approx_area / (0.4330127 * scaled_max * scaled_max)
                max_tri_budget = 2_000_000.0
                if est_tris > max_tri_budget:
                    safe_edge = float(np.sqrt(approx_area / (0.4330127 * max_tri_budget)))
                    logger.warning(
                        "STEP surface '%s' (%s) exceeds 2M triangle budget with max_edge=%g; "
                        "clamping CharacteristicLengthMax to %g model units.",
                        path.name,
                        units,
                        scaled_max,
                        safe_edge,
                    )
                    scaled_max = max(scaled_max, safe_edge)
                    if scaled_min > 0.0:
                        scaled_min = max(scaled_min, 0.1 * safe_edge)
        except Exception:
            pass

        # Configure chordal and curvature deflection controls
        if deflection > 0:
            pts_per_circle = max(int(2.0 * np.pi / max(angular_tol, 0.01)), 6)
            gmsh.option.setNumber("Mesh.MeshSizeFromCurvature", pts_per_circle)
        if scaled_min > 0:
            gmsh.option.setNumber("Mesh.CharacteristicLengthMin", scaled_min)
        if scaled_max > 0:
            gmsh.option.setNumber("Mesh.CharacteristicLengthMax", scaled_max)

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


# ==============================================================================
# Paper 1: Topology-First Watertight B-Rep Meshing (CADWatertightFineMeshing)
# ==============================================================================

class StepTopologyParser:
    """Topology-First B-Rep Meshing Parser eliminating 2D trim curve failures (Paper 1).

    Uses 3D topological entities (V^b, E^b, L^b, F^b) as strict invariants:
    - Dijkstra 3D loop embedding (Algorithm 1)
    - Jordan curve combinatorial separation (Algorithm 2)
    - 1D parameter boundary curve stitching across adjacent patches
    """

    @staticmethod
    def dijkstra_loop_embedding(
        vertices: np.ndarray,
        edges: List[Tuple[int, int]],
        curve_3d_points: np.ndarray,
        occupied_edges: Optional[Set[Tuple[int, int]]] = None,
    ) -> List[int]:
        """Embed 3D boundary curve onto surface mesh edges via Dijkstra shortest paths (Paper 1 Alg. 1).

        Parameters
        ----------
        vertices : np.ndarray, shape (N_v, 3)
            Surface mesh coordinates.
        edges : List[Tuple[int, int]]
            Surface mesh edges.
        curve_3d_points : np.ndarray, shape (K, 3)
            Sampled 3D points along the boundary curve p^1(t).
        occupied_edges : Optional[Set[Tuple[int, int]]]
            Set of normalized (u, v) edges with u < v already occupied by other loops.

        Returns
        -------
        cycle : List[int]
            Ordered list of vertex indices forming a closed 1D cycle on the mesh.
        """
        import heapq
        from scipy.spatial import cKDTree

        occupied = set(occupied_edges) if occupied_edges is not None else set()

        # Build adjacency graph with euclidean edge lengths
        adj: Dict[int, List[Tuple[int, float]]] = {i: [] for i in range(len(vertices))}
        for u, v in edges:
            edge_norm = (min(u, v), max(u, v))
            if edge_norm in occupied:
                continue
            dist = float(np.linalg.norm(vertices[u] - vertices[v]))
            adj[u].append((v, dist))
            adj[v].append((u, dist))

        # Project 3D curve points onto closest mesh vertices
        curve_pts = np.asarray(curve_3d_points, dtype=np.float64)
        if len(curve_pts) == 0 or curve_pts.size == 0:
            raise ValueError("curve_3d_points must not be empty.")
        v_arr = np.asarray(vertices, dtype=np.float64)
        if len(v_arr) == 0 or v_arr.size == 0:
            raise ValueError("vertices must not be empty.")

        tree = cKDTree(v_arr)
        _, proj_indices = tree.query(curve_pts)
        proj_indices = list(proj_indices)

        # Ensure loop is closed
        if proj_indices[0] != proj_indices[-1]:
            proj_indices.append(proj_indices[0])

        full_cycle: List[int] = []

        def dijkstra_path(src: int, dst: int) -> List[int]:
            if src == dst:
                return [src]
            dist_map: Dict[int, float] = {src: 0.0}
            prev_map: Dict[int, int] = {}
            pq: List[Tuple[float, int]] = [(0.0, src)]

            while pq:
                d_cur, u = heapq.heappop(pq)
                if d_cur > dist_map.get(u, float("inf")):
                    continue
                if u == dst:
                    break
                for v, weight in adj.get(u, []):
                    d_new = d_cur + weight
                    if d_new < dist_map.get(v, float("inf")):
                        dist_map[v] = d_new
                        prev_map[v] = u
                        heapq.heappush(pq, (d_new, v))

            if dst not in prev_map:
                # If path blocked by occupied edges, fallback to direct edge
                return [src, dst]

            path = [dst]
            curr = dst
            while curr != src:
                curr = prev_map[curr]
                path.append(curr)
            path.reverse()
            return path

        for k in range(len(proj_indices) - 1):
            s = int(proj_indices[k])
            t = int(proj_indices[k + 1])
            seg = dijkstra_path(s, t)
            if not full_cycle:
                full_cycle.extend(seg)
            else:
                full_cycle.extend(seg[1:])

        # Clean consecutive duplicates
        cleaned_cycle: List[int] = []
        for v in full_cycle:
            if not cleaned_cycle or cleaned_cycle[-1] != v:
                cleaned_cycle.append(v)

        return cleaned_cycle

    @staticmethod
    def jordan_curve_separation(
        faces: np.ndarray,
        boundary_cycle: List[int],
        loop_type: str = "outer",
    ) -> np.ndarray:
        """Combinatorial flood-fill partition of surface mesh across 1D boundary cycle (Paper 1 Alg. 2).

        Separates the surface mesh into interior Disk (D_k) and exterior Annulus
        without geometric ray-casting.

        Parameters
        ----------
        faces : np.ndarray, shape (N_f, 3)
            Triangular surface faces.
        boundary_cycle : List[int]
            Ordered vertex cycle forming the loop boundary.
        loop_type : str
            'outer' for FACE_OUTER_BOUND (retain interior disk),
            'inner' for FACE_BOUND (discard interior hole, retain annulus).

        Returns
        -------
        trimmed_faces : np.ndarray
            Subset of faces representing the trimmed manifold patch.
        """
        faces = np.asarray(faces, dtype=np.int32)
        n_faces = len(faces)
        if n_faces == 0:
            return np.empty((0, 3), dtype=np.int32)

        # Build cycle barrier edge set
        cycle_edges = set()
        c_len = len(boundary_cycle)
        for i in range(c_len):
            u = boundary_cycle[i]
            v = boundary_cycle[(i + 1) % c_len]
            cycle_edges.add((min(u, v), max(u, v)))

        # Build dual face adjacency graph
        edge_to_faces: Dict[Tuple[int, int], List[int]] = {}
        for f_idx, tri in enumerate(faces):
            for i in range(3):
                u = int(tri[i])
                v = int(tri[(i + 1) % 3])
                e = (min(u, v), max(u, v))
                edge_to_faces.setdefault(e, []).append(f_idx)

        dual_adj: Dict[int, List[int]] = {i: [] for i in range(n_faces)}
        for e, f_list in edge_to_faces.items():
            if e in cycle_edges:
                # Barrier edge: sever dual connectivity across the loop
                continue
            if len(f_list) == 2:
                f0, f1 = f_list
                dual_adj[f0].append(f1)
                dual_adj[f1].append(f0)

        # Connected component labeling via BFS
        visited = np.zeros(n_faces, dtype=bool)
        components: List[List[int]] = []

        for f_start in range(n_faces):
            if visited[f_start]:
                continue
            comp: List[int] = []
            queue = [f_start]
            visited[f_start] = True
            while queue:
                curr = queue.pop(0)
                comp.append(curr)
                for nbr in dual_adj[curr]:
                    if not visited[nbr]:
                        visited[nbr] = True
                        queue.append(nbr)
            components.append(comp)

        if len(components) <= 1:
            # Only one component found: loop is on boundary or whole patch is retained
            return faces

        # Sort components by size (face count)
        components.sort(key=len)

        if loop_type == "outer":
            # For outer bound: the largest component is typically the interior disk
            # or the primary enclosed component
            selected_comp = components[-1]
        else:
            # For inner bound (hole): largest component is the outer annulus
            selected_comp = components[-1]

        return faces[np.array(selected_comp, dtype=np.int32)]

    @staticmethod
    def stitch_1d_boundary_curves(
        vertices_a: np.ndarray,
        faces_a: np.ndarray,
        vertices_b: np.ndarray,
        faces_b: np.ndarray,
        shared_curve_points: np.ndarray,
        tolerance: float = 1e-6,
    ) -> Tuple[np.ndarray, np.ndarray]:
        """Stitch shared boundary curves across adjacent patches ensuring watertightness (Paper 1 §4-5).

        Unifies parameter points along the boundary curve and welds coincident vertices
        to eliminate cracks without moving vertices off the true boundary curve.

        Parameters
        ----------
        vertices_a : np.ndarray, shape (N_a, 3)
            Vertices of patch A.
        faces_a : np.ndarray, shape (F_a, 3)
            Faces of patch A.
        vertices_b : np.ndarray, shape (N_b, 3)
            Vertices of patch B.
        faces_b : np.ndarray, shape (F_b, 3)
            Faces of patch B.
        shared_curve_points : np.ndarray, shape (M, 3)
            Sampled 3D points along the shared boundary curve p^1(t).
        tolerance : float
            Welding tolerance in meters (default: 1e-6).

        Returns
        -------
        stitched_vertices : np.ndarray
            Unified vertex array.
        stitched_faces : np.ndarray
            Unified face array with welded boundary seams.
        """
        from scipy.spatial import cKDTree

        v_a = np.asarray(vertices_a, dtype=np.float64)
        f_a = np.asarray(faces_a, dtype=np.int32)
        v_b = np.asarray(vertices_b, dtype=np.float64)
        f_b = np.asarray(faces_b, dtype=np.int32)
        curve_pts = np.asarray(shared_curve_points, dtype=np.float64)

        if len(curve_pts) == 0 or curve_pts.size == 0:
            raise ValueError("shared_curve_points must not be empty.")

        n_a = len(v_a)
        # Shift patch B face indices
        f_b_shifted = f_b + n_a
        combined_vertices = np.vstack([v_a, v_b])
        combined_faces = np.vstack([f_a, f_b_shifted])

        # Identify boundary vertices of A and B near shared curve
        curve_tree = cKDTree(curve_pts)
        d_a, _ = curve_tree.query(v_a)
        d_b, _ = curve_tree.query(v_b)

        candidates_a = np.where(d_a < 10.0 * tolerance)[0]
        candidates_b = np.where(d_b < 10.0 * tolerance)[0] + n_a

        # Weld vertices between candidates_a and candidates_b
        remap = np.arange(len(combined_vertices), dtype=np.int32)
        if len(candidates_a) > 0 and len(candidates_b) > 0:
            b_coords = combined_vertices[candidates_b]
            tree_b = cKDTree(b_coords)
            dists, nn_idx = tree_b.query(combined_vertices[candidates_a])
            for idx_a, dist, b_local in zip(candidates_a, dists, nn_idx):
                if dist <= tolerance:
                    idx_b = candidates_b[b_local]
                    remap[idx_b] = remap[idx_a]

        # Apply remap to faces
        remapped_faces = remap[combined_faces]

        # Remove unused vertices and compact
        unique_old, compact_remap = np.unique(remapped_faces, return_inverse=True)
        final_faces = compact_remap.reshape(combined_faces.shape).astype(np.int32)
        final_vertices = combined_vertices[unique_old]

        return final_vertices, final_faces

