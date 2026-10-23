"""High-performance Binary and ASCII STL parser with spatial vertex welding."""

from __future__ import annotations

from pathlib import Path
import struct
from typing import Any, Dict, List, Optional, Tuple, Union
import numpy as np

from backend.geometry.models import SurfaceMesh
from backend.geometry.parsers.base_parser import BaseParser

try:
    from scipy.spatial import cKDTree
    HAS_CKDTREE = True
except ImportError:
    HAS_CKDTREE = False


class STLParser(BaseParser):
    """Parser for Stereolithography (.stl) CAD files in binary or ASCII formats."""

    def __init__(self, weld_tolerance: float = 1e-7, relative_weld: bool = False) -> None:
        """Initialize STL parser.

        Args:
            weld_tolerance: Spatial welding distance tolerance (in model units, default 1e-7).
            relative_weld: If True, weld_tolerance is scaled by the bounding box diagonal.
        """
        self.weld_tolerance = weld_tolerance
        self.relative_weld = relative_weld

    def parse(self, file_path: Union[str, Path], **kwargs: Any) -> SurfaceMesh:
        """Parse an STL file into a SurfaceMesh with topological vertex welding.

        Args:
            file_path: Path to STL file.
            **kwargs: Optional overrides:
                weld_tolerance (float): Override weld tolerance.
                relative_weld (bool): Override relative weld flag.

        Returns:
            SurfaceMesh containing welded vertices, indexed triangular faces, normals, and patches.
        """
        path = self.validate_file(file_path)
        weld_tol = kwargs.get("weld_tolerance", self.weld_tolerance)
        rel_weld = kwargs.get("relative_weld", self.relative_weld)

        file_size = path.stat().st_size
        if file_size < 84:
            raise ValueError(f"STL file is too small ({file_size} bytes, min 84 bytes expected): {path}")

        with open(path, "rb") as f:
            header_80 = f.read(80)
            count_bytes = f.read(4)

        num_facets = struct.unpack("<I", count_bytes)[0]
        expected_binary_size = 84 + 50 * num_facets

        # Deterministic Binary vs ASCII detection
        if file_size == expected_binary_size:
            # Strictly binary STL
            return self._parse_binary(path, num_facets, weld_tol, rel_weld)

        # Check if ASCII
        is_ascii = False
        try:
            with open(path, "r", encoding="utf-8", errors="replace") as f:
                first_line = f.readline().strip().lower()
                if first_line.startswith("solid"):
                    is_ascii = True
        except Exception:
            is_ascii = False

        if is_ascii:
            return self._parse_ascii(path, weld_tol, rel_weld)

        # If not ASCII and size didn't match expected binary size
        raise ValueError(
            f"Corrupted or invalid STL file '{path}': size {file_size} does not match "
            f"binary header count {num_facets} (expected {expected_binary_size} bytes), "
            f"and file does not begin with ASCII 'solid'."
        )

    def _parse_binary(
        self, path: Path, num_facets: int, weld_tol: float, rel_weld: bool
    ) -> SurfaceMesh:
        """Parse binary STL format using fast numpy buffer operations."""
        facet_dtype = np.dtype(
            [
                ("normal", "<f4", (3,)),
                ("vertices", "<f4", (3, 3)),
                ("attr", "<u2"),
            ]
        )

        with open(path, "rb") as f:
            f.seek(84)
            data = np.fromfile(f, dtype=facet_dtype, count=num_facets)

        if len(data) != num_facets:
            raise ValueError(
                f"Binary STL read error in '{path}': expected {num_facets} facets, read {len(data)}"
            )

        normals = data["normal"].astype(np.float64)
        raw_triangles = data["vertices"].astype(np.float64)  # shape (N, 3, 3)

        # Flatten vertices for welding: shape (3N, 3)
        flat_vertices = raw_triangles.reshape(-1, 3)
        vertices, faces = self.weld_vertices(flat_vertices, weld_tol, rel_weld)

        # If normals are zero or degenerate, compute from faces
        mesh = SurfaceMesh(
            vertices=vertices,
            faces=faces,
            normals=normals if np.any(np.abs(normals) > 1e-12) else None,
            patches={"solid": np.arange(len(faces), dtype=np.int32)},
            metadata={"source_file": str(path), "format": "stl_binary"},
        )
        return mesh

    def _parse_ascii(self, path: Path, weld_tol: float, rel_weld: bool) -> SurfaceMesh:
        """Parse ASCII STL format with support for multiple named solids."""
        triangles: List[List[List[float]]] = []
        face_normals: List[List[float]] = []
        patches: Dict[str, List[int]] = {}

        current_solid = "default"
        current_normal: Optional[List[float]] = None
        current_verts: List[List[float]] = []
        face_index = 0

        with open(path, "r", encoding="utf-8", errors="replace") as f:
            for line in f:
                line_clean = line.strip()
                if not line_clean:
                    continue
                tokens = line_clean.split()
                first = tokens[0].lower()

                if first == "solid":
                    current_solid = tokens[1] if len(tokens) > 1 else "solid"
                    if current_solid not in patches:
                        patches[current_solid] = []
                elif first == "facet":
                    # facet normal ni nj nk
                    if len(tokens) >= 5 and tokens[1].lower() == "normal":
                        current_normal = [float(tokens[2]), float(tokens[3]), float(tokens[4])]
                    else:
                        current_normal = [0.0, 0.0, 0.0]
                    current_verts = []
                elif first == "vertex":
                    if len(tokens) >= 4:
                        current_verts.append([float(tokens[1]), float(tokens[2]), float(tokens[3])])
                elif first == "endfacet":
                    if len(current_verts) == 3:
                        triangles.append(current_verts)
                        face_normals.append(current_normal if current_normal else [0.0, 0.0, 0.0])
                        patches.setdefault(current_solid, []).append(face_index)
                        face_index += 1
                    current_verts = []
                    current_normal = None
                elif first == "endsolid":
                    pass

        if not triangles:
            raise ValueError(f"No valid facets found in ASCII STL file: {path}")

        raw_triangles = np.array(triangles, dtype=np.float64)  # (N, 3, 3)
        flat_vertices = raw_triangles.reshape(-1, 3)
        vertices, faces = self.weld_vertices(flat_vertices, weld_tol, rel_weld)

        patch_arrays = {name: np.array(indices, dtype=np.int32) for name, indices in patches.items()}

        raw_normals = np.array(face_normals, dtype=np.float64)
        use_normals = raw_normals if np.any(np.abs(raw_normals) > 1e-12) else None

        return SurfaceMesh(
            vertices=vertices,
            faces=faces,
            normals=use_normals,
            patches=patch_arrays,
            metadata={"source_file": str(path), "format": "stl_ascii"},
        )

    @staticmethod
    def weld_vertices(
        flat_vertices: np.ndarray, eps: float = 1e-7, relative: bool = False
    ) -> Tuple[np.ndarray, np.ndarray]:
        """Topological spatial vertex welding using spatial hashing / k-d tree.

        Args:
            flat_vertices: Array of points of shape (3*F, 3).
            eps: Spatial tolerance distance.
            relative: If True, eps is scaled by the diagonal of the bounding box.

        Returns:
            Tuple of (unique_vertices, faces) where unique_vertices is (V, 3) and faces is (F, 3).
        """
        num_pts = len(flat_vertices)
        if num_pts == 0:
            return np.array([], dtype=np.float64), np.empty((0, 3), dtype=np.int32)
        if num_pts % 3 != 0:
            raise ValueError(f"Expected multiple of 3 vertices, got {num_pts}")
        num_faces = num_pts // 3

        if relative:
            min_pt = np.min(flat_vertices, axis=0)
            max_pt = np.max(flat_vertices, axis=0)
            diag = float(np.linalg.norm(max_pt - min_pt))
            eps = max(eps * diag, 1e-12)

        if HAS_CKDTREE:
            tree = cKDTree(flat_vertices)
            pairs = tree.query_pairs(r=eps)

            parent = list(range(num_pts))

            def find(i: int) -> int:
                path = []
                while parent[i] != i:
                    path.append(i)
                    i = parent[i]
                for p in path:
                    parent[p] = i
                return i

            for u, v in pairs:
                ru, rv = find(u), find(v)
                if ru != rv:
                    parent[rv] = ru

            root_to_idx: Dict[int, int] = {}
            unique_vertices: List[np.ndarray] = []
            faces = np.empty((num_faces, 3), dtype=np.int32)

            for i in range(num_pts):
                r = find(i)
                v_idx = root_to_idx.get(r)
                if v_idx is None:
                    v_idx = len(unique_vertices)
                    root_to_idx[r] = v_idx
                    unique_vertices.append(flat_vertices[r])
                faces[i // 3, i % 3] = v_idx

            return np.array(unique_vertices, dtype=np.float64), faces
        else:
            # 27-neighborhood spatial hash fallback
            inv_eps = 1.0 / max(eps, 1e-15)
            grid = np.floor(flat_vertices * inv_eps).astype(np.int64)
            unique_vertices_list: List[np.ndarray] = []
            faces = np.empty((num_faces, 3), dtype=np.int32)
            cell_map: Dict[Tuple[int, int, int], List[int]] = {}
            eps_sq = eps * eps

            for i in range(num_pts):
                pt = flat_vertices[i]
                gx, gy, gz = grid[i]
                matched_idx: Optional[int] = None

                for dx in (-1, 0, 1):
                    if matched_idx is not None:
                        break
                    for dy in (-1, 0, 1):
                        if matched_idx is not None:
                            break
                        for dz in (-1, 0, 1):
                            ck = (int(gx + dx), int(gy + dy), int(gz + dz))
                            if ck in cell_map:
                                for cand in cell_map[ck]:
                                    diff = pt - unique_vertices_list[cand]
                                    if np.dot(diff, diff) <= eps_sq:
                                        matched_idx = cand
                                        break
                                if matched_idx is not None:
                                    break

                if matched_idx is None:
                    matched_idx = len(unique_vertices_list)
                    unique_vertices_list.append(pt)
                    cell_key = (int(gx), int(gy), int(gz))
                    if cell_key not in cell_map:
                        cell_map[cell_key] = []
                    cell_map[cell_key].append(matched_idx)

                faces[i // 3, i % 3] = matched_idx

            return np.array(unique_vertices_list, dtype=np.float64), faces
