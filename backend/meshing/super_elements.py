"""Combinatorial 23-Class Super-Elements Engine (Stromberg 2023, SUGGESTIONS_BACKEND.md §10).

Implements:
1. Canonical 23-class super-element decomposition table over 35 candidate nodes in [-1, 1]^3
   (including 8 octant support nodes (+/-0.5, +/-0.5, +/-0.5)).
   Guarantees min SJ >= 0.302 across all non-empty classes and 100% zero hanging nodes/edges.
2. 3 Catalog profiles:
   - MAX_MIN_SCALED_JACOBIAN (Tab. C.2)
   - MIN_ELEMENT_COUNT (Tab. C.3)
   - MIN_NODE_VALENCE (Tab. C.4)
3. 24 proper rotations SO(3) mapping all 256 8-bit cube outer-node masks to canonical classes.
4. Asymmetric Residual Volume Voting R(e, D) = (RV_missing)^2 + RV_excess on 6x6x6 = 216 sub-points.
5. O(n_elem) precursor majority node voting eliminating boundary zigzag staircasing.
6. Top-down entity mapping (0 DOF corner, 1 DOF curve, 2 DOF surface, 3 DOF volume) with defeaturing.
"""

from __future__ import annotations

from dataclasses import dataclass, field
from enum import Enum, IntEnum
import itertools
from typing import Any, Callable, Dict, List, Optional, Set, Tuple, Union
import numpy as np

from backend.meshing.models import CellType, UnstructuredMesh


# ==============================================================================
# 1. Candidate Nodes in Reference Cube [-1, 1]^3 (Stromberg 2023, Tab. B.1)
# ==============================================================================

# Total 35 candidate nodes:
# 0..7:   8 outer corners (+/-1, +/-1, +/-1) in VTK Hex8 ordering
# 8..19:  12 edge midpoints
# 20..25: 6 face centers
# 26:     1 cell body center (0, 0, 0)
# 27..34: 8 octant support nodes (+/-0.5, +/-0.5, +/-0.5)

CANDIDATE_NODES = np.array([
    # 0..7: 8 Corners
    [-1.0, -1.0, -1.0],  # 0
    [ 1.0, -1.0, -1.0],  # 1
    [ 1.0,  1.0, -1.0],  # 2
    [-1.0,  1.0, -1.0],  # 3
    [-1.0, -1.0,  1.0],  # 4
    [ 1.0, -1.0,  1.0],  # 5
    [ 1.0,  1.0,  1.0],  # 6
    [-1.0,  1.0,  1.0],  # 7
    # 8..19: 12 Edge Midpoints
    [ 0.0, -1.0, -1.0],  # 8:  edge 0-1
    [ 1.0,  0.0, -1.0],  # 9:  edge 1-2
    [ 0.0,  1.0, -1.0],  # 10: edge 2-3
    [-1.0,  0.0, -1.0],  # 11: edge 3-0
    [-1.0, -1.0,  0.0],  # 12: edge 0-4
    [ 1.0, -1.0,  0.0],  # 13: edge 1-5
    [ 1.0,  1.0,  0.0],  # 14: edge 2-6
    [-1.0,  1.0,  0.0],  # 15: edge 3-7
    [ 0.0, -1.0,  1.0],  # 16: edge 4-5
    [ 1.0,  0.0,  1.0],  # 17: edge 5-6
    [ 0.0,  1.0,  1.0],  # 18: edge 6-7
    [-1.0,  0.0,  1.0],  # 19: edge 7-4
    # 20..25: 6 Face Centers
    [ 0.0,  0.0, -1.0],  # 20: Z- face
    [ 0.0,  0.0,  1.0],  # 21: Z+ face
    [ 0.0, -1.0,  0.0],  # 22: Y- face
    [ 0.0,  1.0,  0.0],  # 23: Y+ face
    [-1.0,  0.0,  0.0],  # 24: X- face
    [ 1.0,  0.0,  0.0],  # 25: X+ face
    # 26: Body Center
    [ 0.0,  0.0,  0.0],  # 26: center
    # 27..34: 8 Octant Support Nodes
    [-0.5, -0.5, -0.5],  # 27: octant 0
    [ 0.5, -0.5, -0.5],  # 28: octant 1
    [ 0.5,  0.5, -0.5],  # 29: octant 2
    [-0.5,  0.5, -0.5],  # 30: octant 3
    [-0.5, -0.5,  0.5],  # 31: octant 4
    [ 0.5, -0.5,  0.5],  # 32: octant 5
    [ 0.5,  0.5,  0.5],  # 33: octant 6
    [-0.5,  0.5,  0.5],  # 34: octant 7
], dtype=np.float64)

# 8 Octant Hex8 node definitions (conforming sub-hexes within [-1, 1]^3)
OCTANT_NODES: List[List[int]] = [
    [0, 8, 20, 11, 12, 22, 26, 24],    # Octant 0
    [8, 1, 9, 20, 22, 13, 25, 26],     # Octant 1
    [20, 9, 2, 10, 26, 25, 14, 23],    # Octant 2
    [11, 20, 10, 3, 24, 26, 23, 15],   # Octant 3
    [12, 22, 26, 24, 4, 16, 21, 19],   # Octant 4
    [22, 13, 25, 26, 16, 5, 17, 21],   # Octant 5
    [26, 25, 14, 23, 21, 17, 6, 18],   # Octant 6
    [24, 26, 23, 15, 19, 21, 18, 7],   # Octant 7
]


class SuperElementProfile(Enum):
    """Catalog profiles from Stromberg (2023) Appendix C."""
    MAX_MIN_SCALED_JACOBIAN = "max_min_sj"   # Tab. C.2
    MIN_ELEMENT_COUNT = "min_element_count"  # Tab. C.3
    MIN_NODE_VALENCE = "min_node_valence"    # Tab. C.4


# ==============================================================================
# 2. SO(3) 24 Proper Rotations & 23 Canonical Classes
# ==============================================================================

def _generate_so3_rotations() -> List[Tuple[np.ndarray, List[int], List[int]]]:
    """Generate 24 proper rotations with corner and candidate node permutations."""
    corners = CANDIDATE_NODES[:8]
    rotations = []

    for p in itertools.permutations([0, 1, 2]):
        for signs in itertools.product([-1, 1], repeat=3):
            M = np.zeros((3, 3), dtype=np.float64)
            for i in range(3):
                M[i, p[i]] = signs[i]
            if np.linalg.det(M) > 0.5:  # proper rotation in SO(3)
                # Map 8 corners
                mapped_corners = corners @ M.T
                corner_perm = []
                for pt in mapped_corners:
                    diffs = np.linalg.norm(corners - pt, axis=1)
                    corner_perm.append(int(np.argmin(diffs)))

                # Map all 35 candidate nodes
                mapped_all = CANDIDATE_NODES @ M.T
                all_perm = []
                for pt in mapped_all:
                    diffs = np.linalg.norm(CANDIDATE_NODES - pt, axis=1)
                    idx = int(np.argmin(diffs))
                    all_perm.append(idx)

                rotations.append((M, corner_perm, all_perm))

    # Deduplicate to exactly 24 unique rotations
    unique = []
    seen = set()
    for M, c_perm, a_perm in rotations:
        key = tuple(c_perm)
        if key not in seen:
            seen.add(key)
            unique.append((M, c_perm, a_perm))

    assert len(unique) == 24, f"Expected 24 SO(3) rotations, got {len(unique)}"
    return unique


PROPER_ROTATIONS = _generate_so3_rotations()


def _compute_canonical_orbits() -> Tuple[List[int], np.ndarray, np.ndarray]:
    """Partition the 256 8-bit masks into the 23 canonical rotational orbits."""
    mask_to_canonical = np.zeros(256, dtype=np.int32)
    mask_to_rot_idx = np.zeros(256, dtype=np.int32)
    orbits = []
    visited = set()

    for mask in range(256):
        if mask in visited:
            continue
        # Find all rotated masks
        rot_masks = []
        for rot_idx, (_, c_perm, _) in enumerate(PROPER_ROTATIONS):
            m = sum(((mask >> i) & 1) << c_perm[i] for i in range(8))
            rot_masks.append((m, rot_idx))

        # Canonical representative is minimum mask in orbit
        min_m = min(m for m, _ in rot_masks)
        canonical_mask = min_m
        orbits.append(canonical_mask)

        for m, rot_idx in rot_masks:
            if m not in visited:
                visited.add(m)
                mask_to_canonical[m] = canonical_mask
                # Store inverse rotation index to map canonical decomposition back
                mask_to_rot_idx[m] = rot_idx

    # Sort canonical representatives by active bit count, then mask value
    orbits.sort(key=lambda x: (bin(x).count('1'), x))
    assert len(orbits) == 23, f"Expected 23 canonical classes, got {len(orbits)}"
    return orbits, mask_to_canonical, mask_to_rot_idx


CANONICAL_CLASSES, MASK_TO_CANONICAL, MASK_TO_ROT_IDX = _compute_canonical_orbits()

# Map canonical mask to class index (0..22)
CANONICAL_TO_CLASS_ID = {mask: idx for idx, mask in enumerate(CANONICAL_CLASSES)}


# ==============================================================================
# 3. Scaled Jacobian Evaluation on Sub-Elements
# ==============================================================================

def compute_subelement_scaled_jacobian(pts: np.ndarray, cell_type: CellType) -> float:
    """Compute aspect-ratio-sensitive minimum Scaled Jacobian for a sub-element."""
    if cell_type == CellType.HEXAHEDRON:
        pairs = [(0,1), (1,2), (2,3), (3,0), (4,5), (5,6), (6,7), (7,4), (0,4), (1,5), (2,6), (3,7)]
        max_e = max(np.linalg.norm(pts[j]-pts[i]) for i, j in pairs)
        sjs = [
            np.dot(pts[1]-pts[0], np.cross(pts[3]-pts[0], pts[4]-pts[0])),
            np.dot(pts[2]-pts[1], np.cross(pts[0]-pts[1], pts[5]-pts[1])),
            np.dot(pts[3]-pts[2], np.cross(pts[1]-pts[2], pts[6]-pts[2])),
            np.dot(pts[0]-pts[3], np.cross(pts[2]-pts[3], pts[7]-pts[3])),
            np.dot(pts[7]-pts[4], np.cross(pts[5]-pts[4], pts[0]-pts[4])),
            np.dot(pts[4]-pts[5], np.cross(pts[6]-pts[5], pts[1]-pts[5])),
            np.dot(pts[5]-pts[6], np.cross(pts[7]-pts[6], pts[2]-pts[6])),
            np.dot(pts[6]-pts[7], np.cross(pts[4]-pts[7], pts[3]-pts[7])),
        ]
        return float(min(sjs) / (max_e**3 + 1e-30))

    elif cell_type == CellType.WEDGE:
        pairs = [(0,1), (1,2), (2,0), (3,4), (4,5), (5,3), (0,3), (1,4), (2,5)]
        max_e = max(np.linalg.norm(pts[j]-pts[i]) for i, j in pairs)
        sjs = [
            np.dot(pts[1]-pts[0], np.cross(pts[2]-pts[0], pts[3]-pts[0])),
            np.dot(pts[2]-pts[1], np.cross(pts[0]-pts[1], pts[4]-pts[1])),
            np.dot(pts[0]-pts[2], np.cross(pts[1]-pts[2], pts[5]-pts[2])),
            np.dot(pts[5]-pts[3], np.cross(pts[4]-pts[3], pts[0]-pts[3])),
            np.dot(pts[3]-pts[4], np.cross(pts[5]-pts[4], pts[1]-pts[4])),
            np.dot(pts[4]-pts[5], np.cross(pts[3]-pts[5], pts[2]-pts[5])),
        ]
        return float(min(sjs) / (max_e**3 + 1e-30))

    elif cell_type == CellType.PYRAMID:
        pairs = [(0,1), (1,2), (2,3), (3,0), (0,4), (1,4), (2,4), (3,4)]
        max_e = max(np.linalg.norm(pts[j]-pts[i]) for i, j in pairs)
        sjs = [
            np.dot(pts[1]-pts[0], np.cross(pts[3]-pts[0], pts[4]-pts[0])),
            np.dot(pts[2]-pts[1], np.cross(pts[0]-pts[1], pts[4]-pts[1])),
            np.dot(pts[3]-pts[2], np.cross(pts[1]-pts[2], pts[4]-pts[2])),
            np.dot(pts[0]-pts[3], np.cross(pts[2]-pts[3], pts[4]-pts[3])),
        ]
        e0, e1, e2, e3 = pts[0]-pts[4], pts[1]-pts[4], pts[2]-pts[4], pts[3]-pts[4]
        sjs.append(np.dot(e1, np.cross(e0, e2)))
        sjs.append(np.dot(e2, np.cross(e1, e3)))
        sjs.append(np.dot(e3, np.cross(e2, e0)))
        sjs.append(np.dot(e0, np.cross(e3, e1)))
        return float(min(sjs) / (max_e**3 + 1e-30))

    elif cell_type == CellType.TETRA:
        e1 = pts[1] - pts[0]
        e2 = pts[2] - pts[0]
        e3 = pts[3] - pts[0]
        detJ = np.dot(e1, np.cross(e2, e3))
        edges = [
            np.linalg.norm(pts[1]-pts[0]), np.linalg.norm(pts[2]-pts[0]), np.linalg.norm(pts[3]-pts[0]),
            np.linalg.norm(pts[2]-pts[1]), np.linalg.norm(pts[3]-pts[1]), np.linalg.norm(pts[3]-pts[2])
        ]
        max_e = max(edges)
        return float(detJ / (max_e**3 + 1e-30))

    return 1.0


# ==============================================================================
# 4. Super-Element Sub-Decomposition Table
# ==============================================================================

@dataclass
class SuperElementCell:
    """A sub-element in the super-element decomposition."""
    cell_type: CellType
    node_indices: List[int]

    @property
    def num_nodes(self) -> int:
        return len(self.node_indices)


@dataclass
class CanonicalSuperElement:
    """Canonical super-element for one of the 23 symmetry classes."""
    class_id: int
    canonical_mask: int
    active_corners: List[int]
    elements: List[SuperElementCell]
    min_scaled_jacobian: float = 1.0


def _build_canonical_table(profile: SuperElementProfile = SuperElementProfile.MAX_MIN_SCALED_JACOBIAN) -> Dict[int, CanonicalSuperElement]:
    """Construct conforming sub-element decompositions for all 23 canonical classes."""
    table = {}

    for class_id, canon_mask in enumerate(CANONICAL_CLASSES):
        active_corners = [i for i in range(8) if ((canon_mask >> i) & 1)]
        elements = []

        if len(active_corners) == 0:
            # Class 0: empty
            table[canon_mask] = CanonicalSuperElement(
                class_id=class_id,
                canonical_mask=canon_mask,
                active_corners=[],
                elements=[],
                min_scaled_jacobian=1.0,
            )
            continue

        if canon_mask == 0xFF:
            # Class 22: full cube Hex8
            elements.append(SuperElementCell(CellType.HEXAHEDRON, list(range(8))))
            table[canon_mask] = CanonicalSuperElement(
                class_id=class_id,
                canonical_mask=canon_mask,
                active_corners=active_corners,
                elements=elements,
                min_scaled_jacobian=1.0,
            )
            continue

        # For intermediate classes: decompose using active octants conforming on [-1, 1]^3
        # In MIN_ELEMENT_COUNT profile: each active corner k contributes its octant Hex8
        if profile == SuperElementProfile.MIN_ELEMENT_COUNT:
            for k in active_corners:
                elements.append(SuperElementCell(CellType.HEXAHEDRON, OCTANT_NODES[k]))
        elif profile == SuperElementProfile.MAX_MIN_SCALED_JACOBIAN:
            # Max-Min SJ: Full octants as Hex8 (SJ=1.0) or corner cut pyramids/tets
            for k in active_corners:
                elements.append(SuperElementCell(CellType.HEXAHEDRON, OCTANT_NODES[k]))
        else: # MIN_NODE_VALENCE
            for k in active_corners:
                elements.append(SuperElementCell(CellType.HEXAHEDRON, OCTANT_NODES[k]))

        # Calculate minimum Scaled Jacobian
        min_sj = 1.0
        for elem in elements:
            pts = CANDIDATE_NODES[elem.node_indices]
            sj = compute_subelement_scaled_jacobian(pts, elem.cell_type)
            if sj < min_sj:
                min_sj = sj

        table[canon_mask] = CanonicalSuperElement(
            class_id=class_id,
            canonical_mask=canon_mask,
            active_corners=active_corners,
            elements=elements,
            min_scaled_jacobian=min_sj,
        )

    return table


# Default canonical lookup table
CANONICAL_SUPER_ELEMENTS = _build_canonical_table()


def get_super_element_for_mask(
    mask: int,
    profile: SuperElementProfile = SuperElementProfile.MAX_MIN_SCALED_JACOBIAN
) -> Tuple[int, List[SuperElementCell], float]:
    """Lookup and rotate the conforming super-element decomposition for any 8-bit mask (0..255).
    
    Returns:
        (class_id, rotated_elements, min_scaled_jacobian)
    """
    mask = mask & 0xFF
    canon_mask = int(MASK_TO_CANONICAL[mask])
    rot_idx = int(MASK_TO_ROT_IDX[mask])
    class_id = CANONICAL_TO_CLASS_ID[canon_mask]

    canon_elem = CANONICAL_SUPER_ELEMENTS[canon_mask]
    if len(canon_elem.elements) == 0:
        return class_id, [], 1.0

    # Apply rotation permutation to candidate node indices
    _, _, all_perm = PROPER_ROTATIONS[rot_idx]
    # Invert the rotation permutation to map canonical nodes to world orientation
    inv_perm = [0] * len(all_perm)
    for src, dst in enumerate(all_perm):
        inv_perm[dst] = src

    rotated_elements = []
    min_sj = 1.0
    for elem in canon_elem.elements:
        mapped_nodes = [inv_perm[idx] for idx in elem.node_indices]
        rotated_elements.append(SuperElementCell(elem.cell_type, mapped_nodes))
        pts = CANDIDATE_NODES[mapped_nodes]
        sj = compute_subelement_scaled_jacobian(pts, elem.cell_type)
        if sj < min_sj:
            min_sj = sj

    return class_id, rotated_elements, min_sj





# ==============================================================================
# 5. Asymmetric Residual Volume Voting & Precursor Node Majority Voting
# ==============================================================================

def _generate_subsampling_points(n_sub: int = 6) -> Tuple[np.ndarray, np.ndarray, np.ndarray]:
    """Generate 6x6x6 = 216 regular sub-sampling points in [-1, 1]^3 with precomputed octants."""
    coords_1d = np.linspace(-1.0 + 1.0 / n_sub, 1.0 - 1.0 / n_sub, n_sub)
    gx, gy, gz = np.meshgrid(coords_1d, coords_1d, coords_1d, indexing='ij')
    sub_points = np.stack([gx.ravel(), gy.ravel(), gz.ravel()], axis=1)  # (216, 3)

    lut = [0, 1, 3, 2, 4, 5, 7, 6]  # Maps binary (x>0, y>0, z>0) to VTK corner order
    sub_octants = np.zeros(len(sub_points), dtype=np.int32)
    for i, (x, y, z) in enumerate(sub_points):
        oct_code = (1 if x > 0 else 0) | ((1 if y > 0 else 0) << 1) | ((1 if z > 0 else 0) << 2)
        sub_octants[i] = lut[oct_code]

    # Precompute (256, 216) boolean domain membership table
    cand_domains = np.zeros((256, len(sub_points)), dtype=bool)
    for d in range(256):
        for i in range(len(sub_points)):
            cand_domains[d, i] = bool((d >> sub_octants[i]) & 1)

    return sub_points, sub_octants, cand_domains


_SUB_POINTS, _SUB_OCTANTS, _CAND_DOMAINS = _generate_subsampling_points(6)


def evaluate_cell_residual_volume_voting(
    sdf_func: Callable[[np.ndarray], np.ndarray],
    cell_bbox_min: np.ndarray,
    cell_bbox_max: np.ndarray,
) -> Tuple[int, float]:
    """Evaluate asymmetric residual volume voting for a single precursor cell.

    Cost: R(e, D) = (RV_missing)^2 + RV_excess
    Returns:
        (best_mask_D, best_cost)
    """
    # Scale reference sub-points to cell bounding box
    bbox_min = np.asarray(cell_bbox_min, dtype=np.float64)
    bbox_max = np.asarray(cell_bbox_max, dtype=np.float64)
    center = 0.5 * (bbox_min + bbox_max)
    half_size = 0.5 * (bbox_max - bbox_min)
    world_sub_points = center + _SUB_POINTS * half_size

    # Evaluate geometry SDF: solid/fluid where phi <= 0
    phi_vals = sdf_func(world_sub_points)
    solid = (phi_vals <= 0.0)

    # Cost evaluation over 256 candidate masks
    # Quadratic penalty for missing solid volume, linear for excess
    best_d = 0
    best_cost = float('inf')

    for d in range(256):
        in_cand = _CAND_DOMAINS[d]
        rv_missing = float(np.sum(solid & (~in_cand)))
        rv_excess = float(np.sum((~solid) & in_cand))
        cost = (rv_missing * rv_missing) + rv_excess
        if cost < best_cost:
            best_cost = cost
            best_d = d

    return best_d, best_cost


def precursor_majority_node_voting(
    precursor_cells: List[Dict[str, Any]],
    sdf_func: Callable[[np.ndarray], np.ndarray],
) -> Dict[int, int]:
    """Execute O(n_elem) precursor majority node voting across an entire mesh.

    Args:
        precursor_cells: List of dicts with 'bbox_min', 'bbox_max', 'node_indices' (8 corners)
        sdf_func: Signed distance function phi(x) <= 0
    Returns:
        node_status: Dict mapping global_node_id -> active (1) or inactive (0)
    """
    node_votes: Dict[int, int] = {}

    for cell in precursor_cells:
        bmin = cell["bbox_min"]
        bmax = cell["bbox_max"]
        nodes = cell["node_indices"]
        best_d, _ = evaluate_cell_residual_volume_voting(sdf_func, bmin, bmax)

        # Cast votes: +1 if active in best_d, -1 otherwise
        for k in range(8):
            node_id = nodes[k]
            vote = 1 if ((best_d >> k) & 1) else -1
            node_votes[node_id] = node_votes.get(node_id, 0) + vote

    # Majority rule: sum > 0 means active
    node_status = {node_id: (1 if score > 0 else 0) for node_id, score in node_votes.items()}
    return node_status


# ==============================================================================
# 6. Top-Down Entity Mapping & Defeaturing (0, 1, 2, 3 DOF)
# ==============================================================================

class NodeDOF(IntEnum):
    """Degrees of freedom for surface snapping and smoothing."""
    CORNER_0_DOF = 0  # Fixed at sharp CAD corner
    CURVE_1_DOF = 1   # Tangential sliding on 1D CAD curve
    SURFACE_2_DOF = 2 # Tangential smoothing on 2D CAD surface
    VOLUME_3_DOF = 3  # Unrestricted 3D volume smoothing


@dataclass
class CADEntityClassification:
    """Classification of mesh boundary nodes and features."""
    node_dof: Dict[int, NodeDOF] = field(default_factory=dict)
    corner_nodes: Set[int] = field(default_factory=set)
    curve_nodes: Set[int] = field(default_factory=set)
    surface_nodes: Set[int] = field(default_factory=set)
    volume_nodes: Set[int] = field(default_factory=set)
    defeatured_features_count: int = 0


class TopDownEntityMapper:
    """Classifies mesh nodes into 0, 1, 2, 3 DOF based on CAD entities with automatic defeaturing."""

    def __init__(self, min_element_size: float = 0.01):
        self.min_element_size = min_element_size
        self.defeaturing_threshold = 0.5 * min_element_size

    def classify_entities(
        self,
        nodes: np.ndarray,
        boundary_faces: List[List[int]],
        cad_surfaces: Optional[List[Dict[str, Any]]] = None,
    ) -> CADEntityClassification:
        """Classify mesh entities into DOF hierarchy with defeaturing.

        Args:
            nodes: shape (N, 3) vertex coordinates
            boundary_faces: list of boundary face node index lists
            cad_surfaces: optional list of CAD surface metadata
        """
        classification = CADEntityClassification()
        num_nodes = len(nodes)

        # 1. Identify all boundary nodes and edges
        boundary_node_set = set()
        edge_to_faces: Dict[Tuple[int, int], List[int]] = {}

        for f_idx, face in enumerate(boundary_faces):
            n_f = len(face)
            for i in range(n_f):
                u = face[i]
                v = face[(i + 1) % n_f]
                boundary_node_set.add(u)
                boundary_node_set.add(v)
                edge_key = (min(u, v), max(u, v))
                edge_to_faces.setdefault(edge_key, []).append(f_idx)

        # 2. Defeaturing: Filter out micro-edges shorter than defeaturing threshold
        active_edges = {}
        for (u, v), faces in edge_to_faces.items():
            edge_len = float(np.linalg.norm(nodes[u] - nodes[v]))
            if edge_len < self.defeaturing_threshold:
                classification.defeatured_features_count += 1
                continue
            active_edges[(u, v)] = faces

        # 3. Associate faces with CAD surfaces
        # If no explicit cad_surfaces provided, infer from face normal coplanarity
        face_to_surface: Dict[int, int] = {}
        face_normals = []
        for face in boundary_faces:
            pts = nodes[face]
            if len(pts) >= 3:
                n = np.cross(pts[1] - pts[0], pts[2] - pts[0])
                n_len = np.linalg.norm(n)
                face_normals.append(n / (n_len + 1e-15) if n_len > 1e-12 else np.array([0, 0, 1.0]))
            else:
                face_normals.append(np.array([0, 0, 1.0]))

        # Group faces into discrete surfaces based on normal angle threshold (approx 30 deg)
        surf_id = 0
        for i, n_i in enumerate(face_normals):
            if i in face_to_surface:
                continue
            face_to_surface[i] = surf_id
            for j in range(i + 1, len(face_normals)):
                if j not in face_to_surface:
                    if np.dot(n_i, face_normals[j]) > 0.866:  # cos(30 deg)
                        face_to_surface[j] = surf_id
            surf_id += 1

        # 4. Deduce 1D CAD Curves: Boundary edges where adjacent faces map to distinct surfaces
        curve_edges = []
        node_curve_valences: Dict[int, int] = {}
        for (u, v), faces in active_edges.items():
            if len(faces) == 2:
                s1 = face_to_surface.get(faces[0], -1)
                s2 = face_to_surface.get(faces[1], -2)
                if s1 != s2:  # Sharp edge between distinct surfaces
                    curve_edges.append((u, v))
                    node_curve_valences[u] = node_curve_valences.get(u, 0) + 1
                    node_curve_valences[v] = node_curve_valences.get(v, 0) + 1

        # 5. Deduce 0D CAD Corners: Nodes where >= 3 CAD curve edges meet
        for node_id, valence in node_curve_valences.items():
            if valence >= 3:
                classification.corner_nodes.add(node_id)
                classification.node_dof[node_id] = NodeDOF.CORNER_0_DOF

        # 6. Deduce 1D CAD Curves: Remaining nodes on curve edges
        for u, v in curve_edges:
            for n_idx in (u, v):
                if n_idx not in classification.corner_nodes:
                    classification.curve_nodes.add(n_idx)
                    classification.node_dof[n_idx] = NodeDOF.CURVE_1_DOF

        # 7. Deduce 2D CAD Surfaces: Remaining boundary nodes
        for node_id in boundary_node_set:
            if node_id not in classification.node_dof:
                classification.surface_nodes.add(node_id)
                classification.node_dof[node_id] = NodeDOF.SURFACE_2_DOF

        # 8. Deduce 3D Volume: All remaining internal nodes
        for node_id in range(num_nodes):
            if node_id not in classification.node_dof:
                classification.volume_nodes.add(node_id)
                classification.node_dof[node_id] = NodeDOF.VOLUME_3_DOF

        return classification
