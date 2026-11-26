"""CHT twin-face extraction, AMI supermesh coupling, and cyclic boundary pairing."""

from __future__ import annotations

from typing import Dict, List, Optional, Tuple
import numpy as np

from backend.meshing.models import CSRSequence, UnstructuredMesh


def extract_cht_twin_faces(mesh: UnstructuredMesh) -> UnstructuredMesh:
    """Split internal multi-region interfaces into conformal collocated twin boundary patches.

    Reindexes all faces so that the internal face block [0, n_int_clean) is contiguous
    with neighbour >= 0, followed by existing boundary patches, followed by twin boundary
    patches coupledWall_region0_to_1 and coupledWall_region1_to_0.
    """
    if not mesh.is_multiregion or mesh.cell_regions is None:
        return mesh

    owner = mesh.owner
    neighbour = mesh.neighbour
    cell_regions = mesh.cell_regions
    n_faces = mesh.num_faces

    clean_internal: List[int] = []
    coupled_internal: List[int] = []
    original_boundary: List[int] = []

    for f in range(n_faces):
        o = owner[f]
        nb = neighbour[f]
        if nb >= 0:
            if cell_regions[o] == cell_regions[nb]:
                clean_internal.append(f)
            else:
                coupled_internal.append(f)
        else:
            original_boundary.append(f)

    if not coupled_internal:
        return mesh

    n_clean_int = len(clean_internal)
    n_coupled = len(coupled_internal)
    n_orig_bnd = len(original_boundary)

    old_to_new_bnd: Dict[int, int] = {}
    for new_b_idx, old_f in enumerate(original_boundary):
        old_to_new_bnd[old_f] = n_clean_int + new_b_idx

    new_boundaries: Dict[str, Tuple[int, int]] = {}
    for p_name, (start_f, count_f) in mesh.boundaries.items():
        if count_f > 0 and start_f in old_to_new_bnd:
            new_start = old_to_new_bnd[start_f]
            new_boundaries[p_name] = (new_start, count_f)

    pairs_map: Dict[Tuple[int, int], List[int]] = {}
    for f in coupled_internal:
        ro = int(cell_regions[owner[f]])
        rnb = int(cell_regions[neighbour[f]])
        pair = (min(ro, rnb), max(ro, rnb))
        pairs_map.setdefault(pair, []).append(f)

    ordered_pairs = sorted(pairs_map.keys())
    total_new_faces = n_clean_int + n_orig_bnd + 2 * n_coupled

    new_owner = np.zeros(total_new_faces, dtype=np.int64)
    new_neighbour = np.full(total_new_faces, -1, dtype=np.int64)
    new_face_areas = np.zeros((total_new_faces, 3), dtype=np.float64)
    new_face_centres = np.zeros((total_new_faces, 3), dtype=np.float64)
    new_delta_f = np.zeros((total_new_faces, 3), dtype=np.float64)
    new_k_f = np.zeros((total_new_faces, 3), dtype=np.float64)

    has_faces = mesh.faces is not None
    faces_clean = [np.asarray(mesh.faces[f], dtype=np.int64) for f in clean_internal] if has_faces else []
    faces_orig_bnd = [np.asarray(mesh.faces[f], dtype=np.int64) for f in original_boundary] if has_faces else []
    faces_coupled: List[np.ndarray] = []

    for i, f in enumerate(clean_internal):
        new_owner[i] = owner[f]
        new_neighbour[i] = neighbour[f]
        new_face_areas[i] = mesh.face_areas[f]
        new_face_centres[i] = mesh.face_centres[f]
        new_delta_f[i] = mesh.delta_f[f]
        new_k_f[i] = mesh.k_f[f]

    for i, f in enumerate(original_boundary):
        target = n_clean_int + i
        new_owner[target] = owner[f]
        new_neighbour[target] = -1
        new_face_areas[target] = mesh.face_areas[f]
        new_face_centres[target] = mesh.face_centres[f]
        new_delta_f[target] = mesh.delta_f[f]
        new_k_f[target] = mesh.k_f[f]

    current_bnd_offset = n_clean_int + n_orig_bnd
    is_two_region = (len(np.unique(cell_regions)) == 2)
    baffles = []
    coupled_f_to_fa: Dict[int, int] = {}
    coupled_f_to_fb: Dict[int, int] = {}

    for pair in ordered_pairs:
        pair_faces = pairs_map[pair]
        n_p = len(pair_faces)
        start_a = current_bnd_offset
        start_b = start_a + n_p

        zone_names_meta = mesh.metadata.get("cht_zone_names")
        if is_two_region and pair == ordered_pairs[0]:
            z0_is_solid = (
                isinstance(zone_names_meta, (list, tuple))
                and len(zone_names_meta) > max(pair[0], pair[1])
                and "solid" in str(zone_names_meta[pair[0]]).lower()
                and "solid" not in str(zone_names_meta[pair[1]]).lower()
            )
            if z0_is_solid:
                patch_name_a = "coupledWall_solid"
                patch_name_b = "coupledWall_fluid"
            else:
                patch_name_a = "coupledWall_fluid"
                patch_name_b = "coupledWall_solid"
        elif (
            isinstance(zone_names_meta, (list, tuple))
            and len(zone_names_meta) > max(pair[0], pair[1])
        ):
            patch_name_a = f"coupledWall_{zone_names_meta[pair[0]]}_to_{zone_names_meta[pair[1]]}"
            patch_name_b = f"coupledWall_{zone_names_meta[pair[1]]}_to_{zone_names_meta[pair[0]]}"
        else:
            patch_name_a = f"coupledWall_region{pair[0]}_to_{pair[1]}"
            patch_name_b = f"coupledWall_region{pair[1]}_to_{pair[0]}"

        new_boundaries[patch_name_a] = (start_a, n_p)
        new_boundaries[patch_name_b] = (start_b, n_p)

        twin_pairs_p = []
        faces_pair_a: List[np.ndarray] = []
        faces_pair_b: List[np.ndarray] = []
        for i, f in enumerate(pair_faces):
            fa = start_a + i
            fb = start_b + i
            twin_pairs_p.append((fa, fb))
            coupled_f_to_fa[f] = fa
            coupled_f_to_fb[f] = fb

            o = owner[f]
            nb = neighbour[f]
            sf = mesh.face_areas[f]
            cf = mesh.face_centres[f]

            if cell_regions[o] <= cell_regions[nb]:
                cell_a, cell_b = o, nb
                sf_a = sf
                sf_b = -sf
                fn_a = np.asarray(mesh.faces[f], dtype=np.int64) if has_faces else None
                fn_b = np.ascontiguousarray(mesh.faces[f][::-1], dtype=np.int64) if has_faces else None
            else:
                cell_a, cell_b = nb, o
                sf_a = -sf
                sf_b = sf
                fn_a = np.ascontiguousarray(mesh.faces[f][::-1], dtype=np.int64) if has_faces else None
                fn_b = np.asarray(mesh.faces[f], dtype=np.int64) if has_faces else None

            new_owner[fa] = cell_a
            new_neighbour[fa] = -1
            new_face_areas[fa] = sf_a
            new_face_centres[fa] = cf
            new_delta_f[fa] = cf - mesh.cell_centres[cell_a]
            new_k_f[fa] = np.zeros(3, dtype=np.float64)
            if has_faces and fn_a is not None:
                faces_pair_a.append(fn_a)

            new_owner[fb] = cell_b
            new_neighbour[fb] = -1
            new_face_areas[fb] = sf_b
            new_face_centres[fb] = cf
            new_delta_f[fb] = cf - mesh.cell_centres[cell_b]
            new_k_f[fb] = np.zeros(3, dtype=np.float64)
            if has_faces and fn_b is not None:
                faces_pair_b.append(fn_b)

        if has_faces:
            faces_coupled.extend(faces_pair_a)
            faces_coupled.extend(faces_pair_b)

        baffles.append({
            "name": patch_name_a,
            "type": "coupledWall",
            "twin_pairs": twin_pairs_p,
            "start_face_a": start_a,
            "start_face_b": start_b,
            "num_faces": n_p,
            "region_a": pair[0],
            "region_b": pair[1],
        })
        current_bnd_offset += 2 * n_p

    new_face_offsets: Optional[np.ndarray] = None
    new_face_nodes: Optional[np.ndarray] = None
    new_faces: Optional[List[np.ndarray]] = None
    if has_faces:
        all_new_faces = faces_clean + faces_orig_bnd + faces_coupled
        if isinstance(mesh.faces, CSRSequence) or mesh.face_offsets is not None:
            lengths = np.array([len(fn) for fn in all_new_faces], dtype=np.int64)
            new_face_offsets = np.zeros(len(lengths) + 1, dtype=np.int64)
            np.cumsum(lengths, out=new_face_offsets[1:])
            new_face_nodes = (
                np.concatenate(all_new_faces) if new_face_offsets[-1] > 0 else np.empty(0, dtype=np.int64)
            )
        else:
            new_faces = all_new_faces

    new_error_map = {}
    for k, v in mesh.error_map.items():
        if isinstance(v, np.ndarray) and len(v) == n_faces:
            if v.ndim == 1:
                new_v = np.zeros(total_new_faces, dtype=v.dtype)
                new_v[:n_clean_int] = v[clean_internal]
                new_v[n_clean_int:n_clean_int + n_orig_bnd] = v[original_boundary]
                for f, fa in coupled_f_to_fa.items():
                    new_v[fa] = v[f]
                for f, fb in coupled_f_to_fb.items():
                    new_v[fb] = v[f]
            elif v.ndim == 2:
                new_v = np.zeros((total_new_faces, v.shape[1]), dtype=v.dtype)
                new_v[:n_clean_int] = v[clean_internal]
                new_v[n_clean_int:n_clean_int + n_orig_bnd] = v[original_boundary]
                for f, fa in coupled_f_to_fa.items():
                    new_v[fa] = v[f]
                for f, fb in coupled_f_to_fb.items():
                    new_v[fb] = -v[f] if k in ("m_skew", "k_f_comp") else v[f]
            new_error_map[k] = new_v
        else:
            new_error_map[k] = v

    return UnstructuredMesh(
        points=mesh.points,
        cells=mesh.cells,
        owner=new_owner,
        neighbour=new_neighbour,
        face_areas=new_face_areas,
        face_centres=new_face_centres,
        cell_centres=mesh.cell_centres,
        cell_volumes=mesh.cell_volumes,
        delta_f=new_delta_f,
        k_f=new_k_f,
        boundaries=new_boundaries,
        cell_regions=mesh.cell_regions,
        baffles=baffles,
        cyclic_pairs=mesh.cyclic_pairs,
        error_map=new_error_map,
        faces=new_faces,
        cell_types=mesh.cell_types,
        topological_dim=mesh.topological_dim,
        metadata=dict(mesh.metadata),
        face_offsets=new_face_offsets,
        face_nodes=new_face_nodes,
        cell_offsets=mesh.cell_offsets,
        cell_nodes=mesh.cell_nodes,
    )


def _polygon_area_2d(poly: np.ndarray) -> float:
    """Compute 2D polygon area via Shoelace formula."""
    if len(poly) < 3:
        return 0.0
    x = poly[:, 0]
    y = poly[:, 1]
    return 0.5 * abs(float(np.dot(x, np.roll(y, -1)) - np.dot(y, np.roll(x, -1))))


def _clip_polygon_sutherland_hodgman(subject_poly: np.ndarray, clip_poly: np.ndarray) -> np.ndarray:
    """Sutherland-Hodgman polygon clipping for supermesh intersection."""
    if len(subject_poly) < 3 or len(clip_poly) < 3:
        return np.zeros((0, 2), dtype=np.float64)

    area_c = 0.5 * (
        np.dot(clip_poly[:, 0], np.roll(clip_poly[:, 1], -1))
        - np.dot(clip_poly[:, 1], np.roll(clip_poly[:, 0], -1))
    )
    cp = clip_poly if area_c > 0 else clip_poly[::-1]

    output = subject_poly.copy()
    n_cp = len(cp)

    for k in range(n_cp):
        if len(output) == 0:
            break
        p1 = cp[k]
        p2 = cp[(k + 1) % n_cp]
        edge = p2 - p1
        normal = np.array([-edge[1], edge[0]], dtype=np.float64)

        input_list = output
        output_list = []
        n_in = len(input_list)

        for i in range(n_in):
            s = input_list[i]
            e = input_list[(i + 1) % n_in]

            s_in = bool(np.dot(s - p1, normal) >= -1e-12)
            e_in = bool(np.dot(e - p1, normal) >= -1e-12)

            if e_in:
                if not s_in:
                    d = e - s
                    denom = float(np.dot(d, normal))
                    if abs(denom) > 1e-14:
                        t = float(np.dot(p1 - s, normal)) / denom
                        output_list.append(s + t * d)
                output_list.append(e)
            elif s_in:
                d = e - s
                denom = float(np.dot(d, normal))
                if abs(denom) > 1e-14:
                    t = float(np.dot(p1 - s, normal)) / denom
                    output_list.append(s + t * d)

        output = np.array(output_list, dtype=np.float64)

    return output


def compute_ami_weights(
    source_centres: np.ndarray,
    source_areas: np.ndarray,
    target_centres: np.ndarray,
    target_areas: np.ndarray,
    radius: float = 0.05,
    source_polygons: Optional[List[np.ndarray]] = None,
    target_polygons: Optional[List[np.ndarray]] = None,
) -> np.ndarray:
    """Compute Arbitrary Mesh Interface (AMI) area-conservative interpolation weights."""
    source_centres = np.asarray(source_centres, dtype=np.float64)
    target_centres = np.asarray(target_centres, dtype=np.float64)
    source_areas = np.asarray(source_areas, dtype=np.float64)
    target_areas = np.asarray(target_areas, dtype=np.float64)
    n_target = len(target_centres)
    n_source = len(source_centres)
    weights = np.zeros((n_target, n_source), dtype=np.float64)

    if n_target == 0 or n_source == 0:
        return weights

    src_a = np.linalg.norm(source_areas, axis=-1) if source_areas.ndim > 1 else np.abs(source_areas)
    tgt_a = np.linalg.norm(target_areas, axis=-1) if target_areas.ndim > 1 else np.abs(target_areas)

    if source_polygons is not None and len(source_polygons) > 0:
        all_pts_list = [np.asarray(p, dtype=np.float64) for p in source_polygons]
        if target_polygons is not None and len(target_polygons) > 0:
            all_pts_list.extend([np.asarray(p, dtype=np.float64) for p in target_polygons])
        all_pts = np.vstack(all_pts_list)
        spans = np.max(all_pts, axis=0) - np.min(all_pts, axis=0)
    elif source_areas.ndim > 1 and source_areas.shape[1] == 3 and np.any(np.abs(source_areas) > 1e-12):
        avg_normal = np.mean(np.abs(source_areas), axis=0)
        norm_axis = int(np.argmax(avg_normal))
        spans = np.ones(3, dtype=np.float64)
        spans[norm_axis] = 0.0
        all_pts = np.vstack([source_centres, target_centres])
    else:
        all_pts = np.vstack([source_centres, target_centres])
        spans = np.max(all_pts, axis=0) - np.min(all_pts, axis=0)

    if spans[0] <= 1e-5:
        u1, u2 = np.array([0.0, 1.0, 0.0]), np.array([0.0, 0.0, 1.0])
    elif spans[1] <= 1e-5:
        u1, u2 = np.array([1.0, 0.0, 0.0]), np.array([0.0, 0.0, 1.0])
    elif spans[2] <= 1e-5:
        u1, u2 = np.array([1.0, 0.0, 0.0]), np.array([0.0, 1.0, 0.0])
    else:
        mean_pt = np.mean(all_pts, axis=0)
        cov = (all_pts - mean_pt).T @ (all_pts - mean_pt)
        _, eigvecs = np.linalg.eigh(cov)
        u1 = eigvecs[:, 2]
        u2 = eigvecs[:, 1]

    def _to_2d_polys(
        polys: Optional[List[np.ndarray]], centres: np.ndarray, areas: np.ndarray
    ) -> List[np.ndarray]:
        out: List[np.ndarray] = []
        if polys is not None:
            for poly in polys:
                poly_arr = np.asarray(poly, dtype=np.float64)
                out.append(np.column_stack([poly_arr @ u1, poly_arr @ u2]))
        else:
            for idx in range(len(centres)):
                c1 = float(np.dot(centres[idx], u1))
                c2 = float(np.dot(centres[idx], u2))
                h = np.sqrt(max(float(areas[idx]), 1e-12)) / 2.0
                out.append(
                    np.array([[c1 - h, c2 - h], [c1 + h, c2 - h], [c1 + h, c2 + h], [c1 - h, c2 + h]], dtype=np.float64)
                )
        return out

    tgt_polys_2d = _to_2d_polys(target_polygons, target_centres, tgt_a)
    src_polys_2d = _to_2d_polys(source_polygons, source_centres, src_a)

    for i in range(n_target):
        p_tgt = tgt_polys_2d[i]
        c_tgt = target_centres[i]
        dists = np.linalg.norm(source_centres - c_tgt, axis=1)

        overlap_areas = np.zeros(n_source, dtype=np.float64)
        for j in range(n_source):
            h_tgt = np.sqrt(max(float(tgt_a[i]), 1e-12))
            h_src = np.sqrt(max(float(src_a[j]), 1e-12))
            if dists[j] > (h_tgt + h_src) * 1.5 and dists[j] > radius:
                continue

            inter_poly = _clip_polygon_sutherland_hodgman(p_tgt, src_polys_2d[j])
            overlap_areas[j] = _polygon_area_2d(inter_poly)

        total_overlap = float(np.sum(overlap_areas))
        if total_overlap > 1e-15:
            weights[i] = overlap_areas / total_overlap
        else:
            nearest = int(np.argmin(dists))
            weights[i, nearest] = 1.0

    return weights


def extract_cyclic_pairs(
    mesh: UnstructuredMesh,
    sector_angle_rad: float,
    tolerance: float = 1e-4,
) -> List[Tuple[int, int]]:
    """Extract rotational cyclic periodic boundary face pairs: R_z(theta) * c_f1 == c_f2."""
    cos_t = np.cos(sector_angle_rad)
    sin_t = np.sin(sector_angle_rad)
    r_z = np.array([
        [cos_t, -sin_t, 0.0],
        [sin_t,  cos_t, 0.0],
        [0.0,    0.0,   1.0],
    ], dtype=np.float64)

    bnd_faces = np.where(mesh.neighbour < 0)[0]
    pairs = []
    for i, f1 in enumerate(bnd_faces):
        c1 = mesh.face_centres[f1]
        rot_c1 = np.dot(r_z, c1)
        for j, f2 in enumerate(bnd_faces):
            if i == j:
                continue
            c2 = mesh.face_centres[f2]
            if np.linalg.norm(rot_c1 - c2) < tolerance:
                pairs.append((int(f1), int(f2)))
                break
    return pairs
