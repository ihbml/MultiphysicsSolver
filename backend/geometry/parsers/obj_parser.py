"""Wavefront OBJ parser with n-gon polygon triangulation and patch mapping."""

from __future__ import annotations

from pathlib import Path
from typing import Any, Dict, List, Optional, Tuple, Union
import numpy as np

from backend.geometry.models import SurfaceMesh
from backend.geometry.parsers.base_parser import BaseParser


class OBJParser(BaseParser):
    """Parser for Wavefront OBJ (.obj) 3D surface mesh files."""

    def parse(self, file_path: Union[str, Path], **kwargs: Any) -> SurfaceMesh:
        """Parse an OBJ file into a SurfaceMesh.

        Triangulates polygons (quads, n-gons) into planar triangles and maps
        groups/objects to CFD boundary patches.

        Args:
            file_path: Path to the .obj file.
            **kwargs: Additional parser options.

        Returns:
            SurfaceMesh containing vertices, faces, normals, and patches.

        Raises:
            FileNotFoundError: If file not found.
            ValueError: If file is malformed or indices are out of bounds.
        """
        path = self.validate_file(file_path)

        vertices: List[List[float]] = []
        vertex_normals: List[List[float]] = []
        faces: List[List[int]] = []
        face_normals: List[List[float]] = []

        patches: Dict[str, List[int]] = {}
        current_patch = "default"

        with open(path, "r", encoding="utf-8", errors="replace") as f:
            for line_num, line in enumerate(f, start=1):
                line_clean = line.strip()
                if not line_clean or line_clean.startswith("#"):
                    continue

                tokens = line_clean.split()
                prefix = tokens[0]

                if prefix == "v":
                    # Geometric vertex: v x y z [w]
                    if len(tokens) < 4:
                        raise ValueError(f"Malformed vertex at line {line_num} in {path}: {line_clean}")
                    vertices.append([float(tokens[1]), float(tokens[2]), float(tokens[3])])

                elif prefix == "vn":
                    # Vertex normal: vn i j k
                    if len(tokens) >= 4:
                        vertex_normals.append([float(tokens[1]), float(tokens[2]), float(tokens[3])])

                elif prefix in ("g", "o"):
                    # Group or Object name
                    if len(tokens) > 1:
                        current_patch = tokens[1]
                    else:
                        current_patch = "default"
                    if current_patch not in patches:
                        patches[current_patch] = []

                elif prefix == "usemtl":
                    # If no group was specified, material can define the patch
                    if current_patch == "default" and len(tokens) > 1:
                        current_patch = tokens[1]
                        if current_patch not in patches:
                            patches[current_patch] = []

                elif prefix == "f":
                    # Face definition: f v1[/vt1[/vn1]] v2 v3 ...
                    if len(tokens) < 4:
                        raise ValueError(f"Face with fewer than 3 vertices at line {line_num} in {path}")

                    v_indices: List[int] = []
                    vn_indices: List[int] = []

                    for token in tokens[1:]:
                        parts = token.split("/")
                        # Vertex index
                        raw_v = int(parts[0])
                        if raw_v > 0:
                            v_idx = raw_v - 1
                        elif raw_v < 0:
                            v_idx = len(vertices) + raw_v
                        else:
                            raise ValueError(f"Invalid 0 vertex index at line {line_num} in {path}")

                        if v_idx < 0 or v_idx >= len(vertices):
                            raise ValueError(f"Vertex index {raw_v} out of bounds at line {line_num} in {path}")
                        v_indices.append(v_idx)

                        # Check if vertex normal index is present
                        if len(parts) >= 3 and parts[2]:
                            raw_vn = int(parts[2])
                            if raw_vn > 0:
                                vn_idx = raw_vn - 1
                            elif raw_vn < 0:
                                vn_idx = len(vertex_normals) + raw_vn
                            else:
                                vn_idx = -1
                            if 0 <= vn_idx < len(vertex_normals):
                                vn_indices.append(vn_idx)

                    # Triangulate polygons: fan triangulation
                    # If vertices are [v0, v1, v2, ..., vk-1]
                    # Triangles are (v0, v1, v2), (v0, v2, v3), ..., (v0, vi, vi+1)
                    k = len(v_indices)
                    for i in range(1, k - 1):
                        tri = [v_indices[0], v_indices[i], v_indices[i + 1]]
                        face_idx = len(faces)
                        faces.append(tri)
                        patches.setdefault(current_patch, []).append(face_idx)

                        # If vertex normals were provided for all vertices in triangle, average them
                        if len(vn_indices) == k:
                            n0 = np.array(vertex_normals[vn_indices[0]])
                            n1 = np.array(vertex_normals[vn_indices[i]])
                            n2 = np.array(vertex_normals[vn_indices[i + 1]])
                            avg_n = n0 + n1 + n2
                            norm_val = np.linalg.norm(avg_n)
                            if norm_val > 1e-12:
                                face_normals.append((avg_n / norm_val).tolist())
                            else:
                                face_normals.append([0.0, 0.0, 0.0])

        if not vertices or not faces:
            raise ValueError(f"No valid vertices or faces found in OBJ file: {path}")

        v_array = np.array(vertices, dtype=np.float64)
        f_array = np.array(faces, dtype=np.int32)

        patch_arrays = {
            name: np.array(indices, dtype=np.int32)
            for name, indices in patches.items()
            if len(indices) > 0
        }

        fn_array = None
        if len(face_normals) == len(faces):
            fn_array = np.array(face_normals, dtype=np.float64)
            if not np.any(np.abs(fn_array) > 1e-12):
                fn_array = None

        return SurfaceMesh(
            vertices=v_array,
            faces=f_array,
            normals=fn_array,
            patches=patch_arrays,
            metadata={"source_file": str(path), "format": "obj"},
        )
