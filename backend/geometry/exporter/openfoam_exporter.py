"""OpenFOAM surface exporter for constant/triSurface/ and snappyHexMeshDict configuration."""

from __future__ import annotations

import logging
from pathlib import Path
import struct
from typing import Dict, List, Optional, Union
import numpy as np

from backend.geometry.models import SurfaceMesh

logger = logging.getLogger(__name__)


class OpenFOAMExporter:
    """Exports surface meshes to OpenFOAM-compliant case structures and configurations."""

    @classmethod
    def export(
        cls,
        mesh: SurfaceMesh,
        case_dir: Union[str, Path],
        filename: Optional[str] = None,
        export_format: str = "stl",
        binary: bool = False,
    ) -> Path:
        """Export SurfaceMesh to <case_dir>/constant/triSurface/<filename>.

        Args:
            mesh: Scaled, watertight SurfaceMesh.
            case_dir: Root directory of the OpenFOAM simulation case.
            filename: Target file name (e.g. 'geometry.stl'). Default inferred from format.
            export_format: 'stl' or 'obj'.
            binary: If True and format is 'stl', export as binary STL. Otherwise ASCII with named patch blocks.

        Returns:
            Path to the written surface file in constant/triSurface/.

        Raises:
            ValueError: If export format is unsupported or file writing fails.
        """
        c_dir = Path(case_dir)
        tri_surface_dir = c_dir / "constant" / "triSurface"
        tri_surface_dir.mkdir(parents=True, exist_ok=True)

        fmt = export_format.lower()
        if filename is None:
            filename = f"surface.{fmt}"

        out_path = tri_surface_dir / filename

        if fmt == "stl":
            if binary:
                cls.write_stl_binary(mesh, out_path)
            else:
                cls.write_stl_ascii(mesh, out_path)
        elif fmt == "obj":
            cls.write_obj(mesh, out_path)
        else:
            raise ValueError(f"Unsupported export format '{export_format}'. Expected 'stl' or 'obj'.")

        if not out_path.exists() or out_path.stat().st_size == 0:
            raise IOError(f"Failed to write exported surface to {out_path}")

        logger.info("Successfully exported OpenFOAM surface to: %s (%d bytes)", out_path, out_path.stat().st_size)
        return out_path

    @classmethod
    def write_stl_ascii(cls, mesh: SurfaceMesh, out_path: Path) -> None:
        """Write ASCII STL with multi-solid blocks preserving boundary patch definitions."""
        v = mesh.vertices
        f = mesh.faces
        n = mesh.normals if mesh.normals is not None else mesh.compute_face_normals()

        # Group faces by patch
        patches = mesh.patches
        if not patches:
            patches = {"solid": np.arange(len(f), dtype=np.int32)}

        with open(out_path, "w", encoding="utf-8") as out:
            for patch_name, face_indices in patches.items():
                out.write(f"solid {patch_name}\n")
                for fi in face_indices:
                    norm = n[fi]
                    tri = f[fi]
                    v0 = v[tri[0]]
                    v1 = v[tri[1]]
                    v2 = v[tri[2]]

                    out.write(f"  facet normal {norm[0]:.6e} {norm[1]:.6e} {norm[2]:.6e}\n")
                    out.write("    outer loop\n")
                    out.write(f"      vertex {v0[0]:.6e} {v0[1]:.6e} {v0[2]:.6e}\n")
                    out.write(f"      vertex {v1[0]:.6e} {v1[1]:.6e} {v1[2]:.6e}\n")
                    out.write(f"      vertex {v2[0]:.6e} {v2[1]:.6e} {v2[2]:.6e}\n")
                    out.write("    endloop\n")
                    out.write("  endfacet\n")
                out.write(f"endsolid {patch_name}\n")

    @classmethod
    def write_stl_binary(cls, mesh: SurfaceMesh, out_path: Path) -> None:
        """Write binary STL format."""
        v = mesh.vertices.astype(np.float32)
        f = mesh.faces
        n = (mesh.normals if mesh.normals is not None else mesh.compute_face_normals()).astype(np.float32)

        num_facets = len(f)
        header = b"OpenFOAM triSurfaceMesh binary STL"[:80].ljust(80, b"\0")

        with open(out_path, "wb") as out:
            out.write(header)
            out.write(struct.pack("<I", num_facets))

            facet_dtype = np.dtype(
                [
                    ("normal", "<f4", (3,)),
                    ("vertices", "<f4", (3, 3)),
                    ("attr", "<u2"),
                ]
            )

            records = np.empty(num_facets, dtype=facet_dtype)
            records["normal"] = n
            records["vertices"][:, 0] = v[f[:, 0]]
            records["vertices"][:, 1] = v[f[:, 1]]
            records["vertices"][:, 2] = v[f[:, 2]]
            records["attr"] = 0

            records.tofile(out)

    @classmethod
    def write_obj(cls, mesh: SurfaceMesh, out_path: Path) -> None:
        """Write Wavefront OBJ with group patch blocks."""
        v = mesh.vertices
        f = mesh.faces

        with open(out_path, "w", encoding="utf-8") as out:
            out.write("# OpenFOAM triSurface OBJ export\n")
            for pt in v:
                out.write(f"v {pt[0]:.6e} {pt[1]:.6e} {pt[2]:.6e}\n")

            patches = mesh.patches
            if not patches:
                patches = {"default": np.arange(len(f), dtype=np.int32)}

            for patch_name, face_indices in patches.items():
                out.write(f"g {patch_name}\n")
                for fi in face_indices:
                    tri = f[fi]
                    # OBJ is 1-based indexing
                    out.write(f"f {tri[0] + 1} {tri[1] + 1} {tri[2] + 1}\n")

    @classmethod
    def generate_snappyhexmesh_dict_block(
        cls,
        mesh: SurfaceMesh,
        filename: str = "geometry.stl",
        geometry_name: Optional[str] = None,
    ) -> str:
        """Generate OpenFOAM snappyHexMeshDict geometry dictionary block with named boundary patches.

        Args:
            mesh: SurfaceMesh containing patch assignments.
            filename: Name of the surface mesh file in constant/triSurface/.
            geometry_name: Top-level geometry name (default derived from filename).

        Returns:
            Formatted OpenFOAM dictionary block string.
        """
        if geometry_name is None:
            geometry_name = Path(filename).stem

        lines: List[str] = [
            "geometry",
            "{",
            f"    {filename}",
            "    {",
            "        type triSurfaceMesh;",
            f"        name {geometry_name};",
        ]

        patch_names = sorted(list(mesh.patches.keys())) if mesh.patches else ["solid"]
        lines.append("        regions")
        lines.append("        {")
        for pname in patch_names:
            lines.append(f"            {pname}")
            lines.append("            {")
            lines.append(f"                name {pname};")
            lines.append("            }")
        lines.append("        }")
        lines.append("    }")
        lines.append("};")

        return "\n".join(lines)
