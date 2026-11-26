"""Pure-Python / NumPy meshing fallback routines for MeshEngine."""

from __future__ import annotations

import gzip
from pathlib import Path
import tempfile
from typing import Any, Dict, List, Optional, Tuple, Union
import numpy as np

from backend.geometry.models import SurfaceMesh
from backend.geometry.parsers.stl_parser import STLParser
from backend.geometry.pipeline import CADGeometryEngine
from backend.meshing._engine_zones import (
    compute_farfield_bounds_from_config,
    normalize_closed_shell_orientations,
)
from backend.meshing.difvm_topology import compute_difvm_topology
from backend.meshing.distmesh_core import (
    distmesh_2d,
    ear_clipping_triangulate,
    polygon_signed_distance,
    rcm_reorder,
    subdivide_polygon_edges,
)
from backend.meshing.geometry_sdf import CADMeshSDF
from backend.meshing.models import MeshingConfig, UnstructuredMesh


def build_uniform_hex_grid(
    x_nodes: np.ndarray, y_nodes: np.ndarray, z_nodes: np.ndarray
) -> Tuple[np.ndarray, np.ndarray, np.ndarray]:
    """Construct vectorized Cartesian grid nodes, cell centres, and hexahedral node arrays."""
    nx = len(x_nodes) - 1
    ny = len(y_nodes) - 1
    nz = len(z_nodes) - 1

    zz, yy, xx = np.meshgrid(z_nodes, y_nodes, x_nodes, indexing="ij")
    all_nodes = np.column_stack([xx.ravel(), yy.ravel(), zz.ravel()])

    xc = 0.5 * (x_nodes[:-1] + x_nodes[1:])
    yc = 0.5 * (y_nodes[:-1] + y_nodes[1:])
    zc = 0.5 * (z_nodes[:-1] + z_nodes[1:])
    cz, cy, cx = np.meshgrid(zc, yc, xc, indexing="ij")
    c_arr = np.column_stack([cx.ravel(), cy.ravel(), cz.ravel()])

    kk, jj, ii = np.meshgrid(
        np.arange(nz, dtype=np.int64),
        np.arange(ny, dtype=np.int64),
        np.arange(nx, dtype=np.int64),
        indexing="ij",
    )
    kk = kk.ravel()
    jj = jj.ravel()
    ii = ii.ravel()

    stride_k = (ny + 1) * (nx + 1)
    stride_j = nx + 1
    n000 = kk * stride_k + jj * stride_j + ii
    n100 = n000 + 1
    n110 = n000 + stride_j + 1
    n010 = n000 + stride_j
    n001 = n000 + stride_k
    n101 = n100 + stride_k
    n111 = n110 + stride_k
    n011 = n010 + stride_k

    cells_arr = np.column_stack([n000, n100, n110, n010, n001, n101, n111, n011])
    return all_nodes, c_arr, cells_arr


def detect_oriented_box_axes(verts: np.ndarray, faces: np.ndarray) -> Optional[np.ndarray]:
    """Detect if closed surface mesh represents a general rectangular cuboid / box."""
    if len(faces) < 6 or len(verts) < 4:
        return None
    v0 = verts[faces[:, 0]]
    v1 = verts[faces[:, 1]]
    v2 = verts[faces[:, 2]]
    cross_prod = np.cross(v1 - v0, v2 - v0)
    areas = 0.5 * np.linalg.norm(cross_prod, axis=1)
    valid = areas > 1e-15
    if np.sum(valid) < 6:
        return None
    normals = cross_prod[valid] / (2.0 * areas[valid, None])

    axes: List[np.ndarray] = []
    for n in normals:
        if any(abs(float(np.dot(n, a))) > 0.999 for a in axes):
            continue
        axes.append(n)
        if len(axes) > 3:
            return None

    if len(axes) != 3:
        return None

    u0, u1, u2 = axes
    if (
        abs(float(np.dot(u0, u1))) > 1e-3
        or abs(float(np.dot(u0, u2))) > 1e-3
        or abs(float(np.dot(u1, u2))) > 1e-3
    ):
        return None

    R = np.column_stack([u0, u1, u2])
    if np.linalg.det(R) < 0:
        u2 = -u2
        R = np.column_stack([u0, u1, u2])

    P = verts @ R
    L = np.max(P, axis=0) - np.min(P, axis=0)
    bbox_vol = float(L[0] * L[1] * L[2])
    if bbox_vol <= 1e-15:
        return None

    c_f = (v0 + v1 + v2) / 3.0
    s_f = 0.5 * cross_prod
    surf_vol = abs(float(np.sum(np.sum(c_f * s_f, axis=1))) / 3.0)
    if abs(surf_vol - bbox_vol) / bbox_vol > 1e-3:
        return None

    dir_clusters = sum(1 for axis in (u0, -u0, u1, -u1, u2, -u2) if np.any(np.dot(normals, axis) > 0.99))
    if dir_clusters != 6:
        return None
    return R


def mesh_oriented_box_python(
    verts: np.ndarray,
    R: np.ndarray,
    h0: float,
    active_cfg: MeshingConfig,
) -> UnstructuredMesh:
    """Generate body-fitted structured hexahedral mesh for an oriented rectangular cuboid."""
    if h0 <= 0.0:
        raise ValueError(f"h0 must be positive, got {h0}")

    P = verts @ R
    p_min = np.min(P, axis=0)
    p_max = np.max(P, axis=0)
    L = p_max - p_min
    if np.any(L <= 1e-12):
        raise ValueError(f"Degenerate cuboid with near-zero span: {L}")

    nx = max(2, int(np.round(L[0] / h0)))
    ny = max(2, int(np.round(L[1] / h0)))
    nz = max(2, int(np.round(L[2] / h0)))

    max_cells_limit = int(getattr(active_cfg, "max_cells", 2_000_000))
    total_cells = nx * ny * nz
    if total_cells > max_cells_limit:
        raise ValueError(
            f"Requested resolution ({nx}x{ny}x{nz} = {total_cells} cells) exceeds "
            f"configured limit ({max_cells_limit}). Increase h0 or adjust active_cfg.max_cells."
        )

    x_nodes = np.linspace(p_min[0], p_max[0], nx + 1)
    y_nodes = np.linspace(p_min[1], p_max[1], ny + 1)
    z_nodes = np.linspace(p_min[2], p_max[2], nz + 1)
    local_nodes, _, cells_arr = build_uniform_hex_grid(x_nodes, y_nodes, z_nodes)
    all_pts = local_nodes @ R.T
    all_cells = [cells_arr[idx] for idx in range(len(cells_arr))]

    mesh = compute_difvm_topology(all_pts, all_cells, config=active_cfg)
    if active_cfg.rcm_reorder:
        mesh, _ = rcm_reorder(mesh)
    return mesh


def _graded_axis(
    d_min: float, s_mn: float, s_mx: float, d_max: float, h_core: float, growth_ratio: float
) -> np.ndarray:
    c_min = max(d_min, s_mn - 1.5 * h_core)
    c_max = min(d_max, s_mx + 1.5 * h_core)
    if c_min - d_min < 1.2 * h_core:
        c_min = d_min
    if d_max - c_max < 1.2 * h_core:
        c_max = d_max
    n_core = max(1, int(round((c_max - c_min) / h_core)))
    dx_core = (c_max - c_min) / n_core
    max_h = 8.0 * dx_core

    def _slab(length: float) -> List[float]:
        steps: List[float] = []
        if length <= 1e-12 * h_core:
            return steps
        cur_h = dx_core
        sum_h = 0.0
        while sum_h < length:
            next_h = min(max_h, cur_h * growth_ratio)
            if steps and sum_h + 0.5 * next_h >= length:
                break
            steps.append(next_h)
            sum_h += next_h
            cur_h = next_h
        sc = length / sum_h
        return [s * sc for s in steps]

    left_steps = _slab(c_min - d_min)
    right_steps = _slab(d_max - c_max)
    coords = []
    if left_steps:
        pos = d_min
        coords.append(d_min)
        for s in reversed(left_steps[1:]):
            pos += s
            coords.append(pos)
        coords.append(c_min)
    else:
        coords.append(c_min)
    for idx in range(1, n_core):
        coords.append(c_min + idx * dx_core)
    coords.append(c_max)
    if right_steps:
        pos = c_max
        for s in right_steps[:-1]:
            pos += s
            coords.append(pos)
        coords.append(d_max)
    return np.asarray(coords, dtype=np.float64)


def mesh_external_fluid_python(
    scaled_surf: SurfaceMesh,
    h0: float,
    active_cfg: MeshingConfig,
    ff_min: np.ndarray,
    ff_max: np.ndarray,
) -> UnstructuredMesh:
    """Pure-Python external fluid domain meshing bounded by farfield outer box."""
    sdf = CADMeshSDF.from_surface_mesh(scaled_surf)
    verts = scaled_surf.vertices
    bmin = np.min(verts, axis=0)
    bmax = np.max(verts, axis=0)

    ff_span = ff_max - ff_min
    if float(np.max(ff_span)) / float(h0) > 20.0:
        l_ref = max(float(np.max(bmax - bmin)), 1e-12)
        h_core = max(float(h0), 0.25 * l_ref)
        growth_ratio = float(getattr(active_cfg.sizing, "growth_rate", 1.2))
        if growth_ratio <= 1.0:
            growth_ratio = 1.0 + growth_ratio
        growth_ratio = max(1.05, min(1.35, growth_ratio))

        x_nodes = _graded_axis(float(ff_min[0]), float(bmin[0]), float(bmax[0]), float(ff_max[0]), h_core, growth_ratio)
        y_nodes = _graded_axis(float(ff_min[1]), float(bmin[1]), float(bmax[1]), float(ff_max[1]), h_core, growth_ratio)
        z_nodes = _graded_axis(float(ff_min[2]), float(bmin[2]), float(bmax[2]), float(ff_max[2]), h_core, growth_ratio)
    else:
        nx = max(2, int(np.round(ff_span[0] / h0)))
        ny = max(2, int(np.round(ff_span[1] / h0)))
        nz = max(2, int(np.round(ff_span[2] / h0)))
        x_nodes = np.linspace(ff_min[0], ff_max[0], nx + 1)
        y_nodes = np.linspace(ff_min[1], ff_max[1], ny + 1)
        z_nodes = np.linspace(ff_min[2], ff_max[2], nz + 1)

    nx, ny, nz = len(x_nodes) - 1, len(y_nodes) - 1, len(z_nodes) - 1
    all_nodes, c_arr, cells_arr = build_uniform_hex_grid(x_nodes, y_nodes, z_nodes)
    phi_c, _ = sdf.evaluate(c_arr)

    active_3d = (phi_c >= 0.0).reshape((nz, ny, nx))
    kk, jj, ii = np.where(active_3d)
    stride_k = (ny + 1) * (nx + 1)
    stride_j = nx + 1
    n000 = kk * stride_k + jj * stride_j + ii
    n100 = n000 + 1
    n110 = n000 + stride_j + 1
    n010 = n000 + stride_j
    n001 = n000 + stride_k
    n101 = n100 + stride_k
    n111 = n110 + stride_k
    n011 = n010 + stride_k

    act_cells_arr = np.column_stack([n000, n100, n110, n010, n001, n101, n111, n011])
    active_cells = [act_cells_arr[idx] for idx in range(len(act_cells_arr))]

    boundary_patches: Dict[str, List[List[int]]] = {
        "inlet": [],
        "outlet": [],
        "bottom": [],
        "top": [],
        "sides": [],
    }
    body_patch_names = list(scaled_surf.patches.keys()) if scaled_surf.patches else []
    for bp in body_patch_names:
        boundary_patches[bp] = []
    if not body_patch_names:
        boundary_patches["wall"] = []

    tri_cents_by_patch: Dict[str, np.ndarray] = {}
    for bp in body_patch_names:
        tri_indices = scaled_surf.patches[bp]
        if len(tri_indices) > 0:
            tri_cents_by_patch[bp] = np.mean(verts[scaled_surf.faces[tri_indices]], axis=1)

    def _assign_wall_faces(face_arr: np.ndarray) -> None:
        if len(face_arr) == 0:
            return
        if not body_patch_names:
            boundary_patches["wall"].extend(face_arr.tolist())
            return
        fcs = np.mean(all_nodes[face_arr], axis=1)
        best_dists = np.full(len(face_arr), np.inf, dtype=np.float64)
        best_idx = np.zeros(len(face_arr), dtype=np.int64)
        for p_idx, bp in enumerate(body_patch_names):
            tc = tri_cents_by_patch.get(bp)
            if tc is None or len(tc) == 0:
                continue
            d_min = np.min(np.linalg.norm(tc[None, :, :] - fcs[:, None, :], axis=2), axis=1)
            better = d_min < best_dists
            best_dists[better] = d_min[better]
            best_idx[better] = p_idx
        for row_idx, fn in enumerate(face_arr.tolist()):
            boundary_patches[body_patch_names[int(best_idx[row_idx])]].append(fn)

    specs = [
        ("inlet", ii == 0, lambda m: ~active_3d[kk[~m], jj[~m], ii[~m] - 1], [n000, n010, n011, n001]),
        ("outlet", ii == (nx - 1), lambda m: ~active_3d[kk[~m], jj[~m], ii[~m] + 1], [n100, n101, n111, n110]),
        ("sides", jj == 0, lambda m: ~active_3d[kk[~m], jj[~m] - 1, ii[~m]], [n000, n001, n101, n100]),
        ("sides", jj == (ny - 1), lambda m: ~active_3d[kk[~m], jj[~m] + 1, ii[~m]], [n010, n110, n111, n011]),
        ("bottom", kk == 0, lambda m: ~active_3d[kk[~m] - 1, jj[~m], ii[~m]], [n000, n100, n110, n010]),
        ("top", kk == (nz - 1), lambda m: ~active_3d[kk[~m] + 1, jj[~m], ii[~m]], [n001, n011, n111, n101]),
    ]
    for pname, m_outer, wall_fn, corners in specs:
        m_wall = np.zeros_like(m_outer)
        m_wall[~m_outer] = wall_fn(m_outer)
        if np.any(m_outer):
            boundary_patches[pname].extend(np.column_stack([c[m_outer] for c in corners]).tolist())
        if np.any(m_wall):
            _assign_wall_faces(np.column_stack([c[m_wall] for c in corners]))

    active_boundary_patches = {k: v for k, v in boundary_patches.items() if len(v) > 0}
    mesh = compute_difvm_topology(all_nodes, active_cells, boundary_patches=active_boundary_patches, config=active_cfg)
    if active_cfg.rcm_reorder:
        mesh, _ = rcm_reorder(mesh)
    return mesh


def mesh_cht_coupled_python(
    surface: SurfaceMesh,
    scale: float,
    h0: float,
    active_cfg: MeshingConfig,
) -> UnstructuredMesh:
    """Mesh both fluid and solid domains with conformal CHT twin-face coupling."""
    norm_faces, _ = normalize_closed_shell_orientations(surface.vertices * scale, surface.faces)
    scaled_surf = SurfaceMesh(
        vertices=surface.vertices * scale,
        faces=norm_faces,
        normals=surface.normals.copy() if surface.normals is not None else None,
        patches=dict(surface.patches) if surface.patches else {},
    )
    sdf = CADMeshSDF.from_surface_mesh(scaled_surf)

    geom_bmin = np.min(scaled_surf.vertices, axis=0)
    geom_bmax = np.max(scaled_surf.vertices, axis=0)
    if active_cfg.farfield_bbox is not None or active_cfg.farfield_padding is not None:
        ff_min, ff_max = compute_farfield_bounds_from_config(geom_bmin, geom_bmax, active_cfg)
    else:
        span_body = np.maximum(geom_bmax - geom_bmin, 1e-6)
        ff_min = geom_bmin - 0.5 * span_body
        ff_max = geom_bmax + 0.5 * span_body

    span = ff_max - ff_min
    nx = max(2, int(np.round(span[0] / h0)))
    ny = max(2, int(np.round(span[1] / h0)))
    nz = max(2, int(np.round(span[2] / h0)))

    x_nodes = np.linspace(ff_min[0], ff_max[0], nx + 1)
    y_nodes = np.linspace(ff_min[1], ff_max[1], ny + 1)
    z_nodes = np.linspace(ff_min[2], ff_max[2], nz + 1)
    all_nodes, c_arr, cells_arr = build_uniform_hex_grid(x_nodes, y_nodes, z_nodes)
    phi_c, _ = sdf.evaluate(c_arr)

    regions = np.where(phi_c >= 0.0, 0, 1).astype(np.int32)
    cells = [cells_arr[idx].tolist() for idx in range(len(cells_arr))]

    def node_id(i: int, j: int, k: int) -> int:
        return k * (ny + 1) * (nx + 1) + j * (nx + 1) + i

    boundary_patches: Dict[str, List[List[int]]] = {
        "inlet": [],
        "outlet": [],
        "bottom": [],
        "top": [],
        "sides": [],
    }
    for k in range(nz):
        for j in range(ny):
            for i in range(nx):
                if i == 0:
                    boundary_patches["inlet"].append([node_id(i, j, k), node_id(i, j + 1, k), node_id(i, j + 1, k + 1), node_id(i, j, k + 1)])
                if i == nx - 1:
                    boundary_patches["outlet"].append([node_id(i + 1, j, k), node_id(i + 1, j, k + 1), node_id(i + 1, j + 1, k + 1), node_id(i + 1, j + 1, k)])
                if j == 0:
                    boundary_patches["sides"].append([node_id(i, j, k), node_id(i, j, k + 1), node_id(i + 1, j, k + 1), node_id(i + 1, j, k)])
                if j == ny - 1:
                    boundary_patches["sides"].append([node_id(i, j + 1, k), node_id(i + 1, j + 1, k), node_id(i + 1, j + 1, k + 1), node_id(i, j + 1, k + 1)])
                if k == 0:
                    boundary_patches["bottom"].append([node_id(i, j, k), node_id(i + 1, j, k), node_id(i + 1, j + 1, k), node_id(i, j + 1, k)])
                if k == nz - 1:
                    boundary_patches["top"].append([node_id(i, j, k + 1), node_id(i, j + 1, k + 1), node_id(i + 1, j + 1, k + 1), node_id(i + 1, j, k + 1)])

    active_patches = {k: v for k, v in boundary_patches.items() if len(v) > 0}
    mesh = compute_difvm_topology(all_nodes, cells, regions=regions, boundary_patches=active_patches, config=active_cfg)
    if active_cfg.rcm_reorder:
        mesh, _ = rcm_reorder(mesh)
    return mesh


def _load_region_surface(item: Union[str, Path, SurfaceMesh], parser_stl: STLParser) -> SurfaceMesh:
    if isinstance(item, SurfaceMesh):
        return item
    p = Path(item).resolve()
    if str(p).endswith(".stl.gz"):
        with gzip.open(p, "rb") as gz, tempfile.NamedTemporaryFile(suffix=".stl", delete=False) as tmp:
            tmp.write(gz.read())
            tmp.flush()
            return parser_stl.parse(Path(tmp.name))
    if str(p).endswith(".stl"):
        return parser_stl.parse(p)
    cad_engine = CADGeometryEngine()
    cad_engine.load(p)
    return cad_engine.current_mesh


def mesh_multi_region_python(
    regions: Union[str, Path, Dict[str, Union[str, Path, SurfaceMesh]], List[Union[str, Path, SurfaceMesh]]],
    h0: float,
    scale: float,
    active_cfg: MeshingConfig,
    binary_regions: bool = False,
) -> UnstructuredMesh:
    """Mesh multiple interacting CAD regions with cell_regions and coupledWall twin patches."""
    region_meshes: Dict[str, SurfaceMesh] = {}
    parser_stl = STLParser()

    if isinstance(regions, (str, Path)):
        reg_dir = Path(regions).resolve()
        if not reg_dir.is_dir():
            raise ValueError(f"Multi-region path must be a directory: {reg_dir}")
        files = sorted(list(reg_dir.glob("*.stl")) + list(reg_dir.glob("*.stl.gz")) + list(reg_dir.glob("*.obj")))
        if not files:
            raise ValueError(f"No CAD geometry files found in directory: {reg_dir}")
        for f in files:
            rname = f.name[:-7] if f.name.endswith(".stl.gz") else (f.name[:-4] if f.name.endswith(".stl") else f.stem)
            sm = _load_region_surface(f, parser_stl)
            norm_f, _ = normalize_closed_shell_orientations(sm.vertices * scale, sm.faces)
            region_meshes[rname] = SurfaceMesh(
                vertices=sm.vertices * scale,
                faces=norm_f,
                normals=sm.normals.copy() if sm.normals is not None else None,
                patches=dict(sm.patches) if sm.patches else {},
            )
    elif isinstance(regions, dict):
        for rname, item in regions.items():
            sm = _load_region_surface(item, parser_stl)
            norm_f, _ = normalize_closed_shell_orientations(sm.vertices * scale, sm.faces)
            region_meshes[rname] = SurfaceMesh(
                vertices=sm.vertices * scale,
                faces=norm_f,
                normals=sm.normals.copy() if sm.normals is not None else None,
                patches=dict(sm.patches) if sm.patches else {},
            )
    elif isinstance(regions, list):
        for idx, item in enumerate(regions):
            sm = _load_region_surface(item, parser_stl)
            norm_f, _ = normalize_closed_shell_orientations(sm.vertices * scale, sm.faces)
            region_meshes[f"region_{idx}"] = SurfaceMesh(
                vertices=sm.vertices * scale,
                faces=norm_f,
                normals=sm.normals.copy() if sm.normals is not None else None,
                patches=dict(sm.patches) if sm.patches else {},
            )
    else:
        raise TypeError(f"Unsupported regions input type: {type(regions)}")

    if not region_meshes:
        raise ValueError("No valid region meshes found for multi-region meshing.")

    bmin = np.min([sm.get_bounding_box().min_pt for sm in region_meshes.values()], axis=0)
    bmax = np.max([sm.get_bounding_box().max_pt for sm in region_meshes.values()], axis=0)
    span = np.maximum(bmax - bmin, 1e-6)

    nx = max(2, int(np.round(span[0] / h0)))
    ny = max(2, int(np.round(span[1] / h0)))
    nz = max(2, int(np.round(span[2] / h0)))
    x_nodes = np.linspace(bmin[0], bmax[0], nx + 1)
    y_nodes = np.linspace(bmin[1], bmax[1], ny + 1)
    z_nodes = np.linspace(bmin[2], bmax[2], nz + 1)
    all_nodes, c_arr, cells_arr = build_uniform_hex_grid(x_nodes, y_nodes, z_nodes)
    cells = [cells_arr[idx].tolist() for idx in range(len(cells_arr))]

    r_names = list(region_meshes.keys())
    bbox_vols = []
    phi_cols = []
    for rname in r_names:
        bb = region_meshes[rname].get_bounding_box()
        bbox_vols.append(float(np.prod(np.maximum(bb.max_pt - bb.min_pt, 1e-12))))
        sdf = CADMeshSDF.from_surface_mesh(region_meshes[rname])
        phi, _ = sdf.evaluate(c_arr)
        phi_cols.append(phi)
    phi_mat = np.column_stack(phi_cols)
    bbox_vols_arr = np.asarray(bbox_vols, dtype=np.float64)

    min_phi = np.min(phi_mat, axis=1)
    best_region_idx = np.argmin(phi_mat, axis=1)
    for i in range(len(c_arr)):
        inside = np.where(phi_mat[i] <= 1e-9)[0]
        if len(inside) > 1:
            best_region_idx[i] = int(inside[np.argmin(bbox_vols_arr[inside])])
        elif len(inside) == 1:
            best_region_idx[i] = int(inside[0])

    h_min = min(span[0] / nx, span[1] / ny, span[2] / nz)
    active_mask = min_phi < 0.15 * h_min
    active_indices = [i for i in range(len(cells)) if active_mask[i]] or list(range(len(cells)))
    active_cells = [cells[i] for i in active_indices]

    regions_list = []
    for idx in active_indices:
        rname = r_names[best_region_idx[idx]]
        if binary_regions:
            reg_id = 0 if ("air" in rname.lower() or "fluid" in rname.lower()) else 1
        else:
            reg_id = best_region_idx[idx]
        regions_list.append(reg_id)
    regions_arr = np.array(regions_list, dtype=np.int32)

    mesh = compute_difvm_topology(all_nodes, active_cells, regions=regions_arr, config=active_cfg)
    if active_cfg.rcm_reorder:
        mesh, _ = rcm_reorder(mesh)
    return mesh


def mesh_complex_surface_python(
    surface: SurfaceMesh,
    scale: float,
    h0: float,
    active_cfg: MeshingConfig,
) -> UnstructuredMesh:
    """Mesh complex 3D aerodynamic surfaces via SDF level set."""
    norm_faces, shell_stats = normalize_closed_shell_orientations(surface.vertices * scale, surface.faces)
    scaled_surf = SurfaceMesh(
        vertices=surface.vertices * scale,
        faces=norm_faces,
        normals=surface.normals.copy() if surface.normals is not None else None,
    )
    sdf = CADMeshSDF.from_surface_mesh(scaled_surf)

    bmin = np.min(scaled_surf.vertices, axis=0)
    bmax = np.max(scaled_surf.vertices, axis=0)
    span = np.maximum(bmax - bmin, 1e-6)
    nx = max(2, int(np.ceil(span[0] / float(h0))))
    ny = max(2, int(np.ceil(span[1] / float(h0))))
    nz = max(2, int(np.ceil(span[2] / float(h0))))

    x_nodes = np.linspace(bmin[0], bmax[0], nx + 1)
    y_nodes = np.linspace(bmin[1], bmax[1], ny + 1)
    z_nodes = np.linspace(bmin[2], bmax[2], nz + 1)
    all_nodes, c_arr, cells_arr = build_uniform_hex_grid(x_nodes, y_nodes, z_nodes)
    phi_c, _ = sdf.evaluate(c_arr)

    threshold = float(getattr(active_cfg, "levelset_threshold", 0.0))
    active_cells = [cells_arr[idx] for idx in range(len(cells_arr)) if phi_c[idx] <= threshold]

    mesh = compute_difvm_topology(all_nodes, active_cells, config=active_cfg)
    mesh.metadata["shell_orientation"] = shell_stats
    mesh.metadata["surface_open_edges"] = int(shell_stats["surface_open_edges"])
    if active_cfg.rcm_reorder:
        mesh, _ = rcm_reorder(mesh)
    return mesh


def mesh_from_points_cells_python(
    verts: np.ndarray,
    h0: float,
    active_cfg: MeshingConfig,
) -> UnstructuredMesh:
    """Fallback box mesher from bounding box of points."""
    if h0 <= 0.0:
        raise ValueError(f"h0 must be positive, got {h0}")
    bmin = np.min(verts, axis=0)
    bmax = np.max(verts, axis=0)
    span = bmax - bmin

    nx = max(2, int(np.round(span[0] / h0)))
    ny = max(2, int(np.round(span[1] / h0)))
    nz = max(2, int(np.round(span[2] / h0)))

    x_nodes = np.linspace(bmin[0], bmax[0], nx + 1)
    y_nodes = np.linspace(bmin[1], bmax[1], ny + 1)
    z_nodes = np.linspace(bmin[2], bmax[2], nz + 1)
    all_nodes, _, cells_arr = build_uniform_hex_grid(x_nodes, y_nodes, z_nodes)
    all_cells = [cells_arr[idx] for idx in range(len(cells_arr))]

    mesh = compute_difvm_topology(all_nodes, all_cells, config=active_cfg)
    if active_cfg.rcm_reorder:
        mesh, _ = rcm_reorder(mesh)
    return mesh


def mesh_2d_polygon_python(
    poly: np.ndarray,
    h0: float,
    active_cfg: Optional[MeshingConfig],
    use_native: bool,
) -> UnstructuredMesh:
    """Pure-Python 2D polygon mesher via constrained DistMesh and ear-clipping."""
    if h0 > 0.0:
        bnd_pts, _, _ = subdivide_polygon_edges(poly, h0)
        bbox = (
            float(np.min(poly[:, 0]) - h0),
            float(np.min(poly[:, 1]) - h0),
            float(np.max(poly[:, 0]) + h0),
            float(np.max(poly[:, 1]) + h0),
        )
        all_pts, cells_arr = distmesh_2d(
            fd=lambda p: polygon_signed_distance(p, poly),
            fh=lambda p: np.full(len(p), h0),
            h0=h0,
            bbox=bbox,
            pfix=bnd_pts,
            max_iters=active_cfg.distmesh_iters if active_cfg else 45,
        )
        if len(cells_arr) == 0:
            all_pts, cells_arr = ear_clipping_triangulate(bnd_pts if len(bnd_pts) >= 3 else poly)
    else:
        all_pts, cells_arr = ear_clipping_triangulate(poly)

    cells = [cells_arr[i].copy() for i in range(len(cells_arr))]
    mesh = compute_difvm_topology(all_pts, cells, use_native=use_native)
    mesh.metadata["engine"] = "pure_python_fallback" if not use_native else "native_cpp20_2d"
    if active_cfg and active_cfg.rcm_reorder:
        mesh, _ = rcm_reorder(mesh)
    return mesh
