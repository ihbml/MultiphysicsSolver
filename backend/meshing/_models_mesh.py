"""CSRSequence, UnstructuredMesh, and SPHParticleSet containers."""

from __future__ import annotations

from dataclasses import dataclass, field
from pathlib import Path
from typing import Any, Dict, Iterator, List, Optional, Tuple, Union
import numpy as np

from backend.geometry.models import BoundingBox
from backend.meshing._models_quality import MeshQualityReport, compute_mesh_j_ens


class CSRSequence:
    """Zero-per-item-allocation sequence backed by contiguous CSR (offsets, data) int64 arrays.

    Presents a list-of-1D-arrays interface (`len`, indexing, negative indexing, slicing,
    iteration, equality) while keeping memory contiguous without Python per-row allocations.
    """

    def __init__(self, offsets: np.ndarray, data: np.ndarray) -> None:
        self.offsets = np.asarray(offsets, dtype=np.int64).ravel()
        self.data = np.asarray(data, dtype=np.int64).ravel()

    def __len__(self) -> int:
        return max(0, len(self.offsets) - 1)

    def __bool__(self) -> bool:
        return len(self) > 0

    def __getitem__(self, index: Union[int, np.integer, slice]) -> Union[np.ndarray, List[np.ndarray]]:
        n = len(self)
        if isinstance(index, slice):
            start, stop, step = index.indices(n)
            return [
                self.data[int(self.offsets[i]) : int(self.offsets[i + 1])]
                for i in range(start, stop, step)
            ]
        if isinstance(index, (int, np.integer)):
            idx = int(index)
            if idx < 0:
                idx += n
            if idx < 0 or idx >= n:
                raise IndexError(f"CSRSequence index {index} out of range for length {n}")
            return self.data[int(self.offsets[idx]) : int(self.offsets[idx + 1])]
        raise TypeError(f"CSRSequence indices must be integers or slices, not {type(index).__name__}")

    def __iter__(self) -> Iterator[np.ndarray]:
        n = len(self)
        offs = self.offsets
        dat = self.data
        for i in range(n):
            yield dat[int(offs[i]) : int(offs[i + 1])]

    def __eq__(self, other: object) -> bool:
        if isinstance(other, CSRSequence):
            if len(self) != len(other):
                return False
            if np.array_equal(self.offsets, other.offsets) and np.array_equal(self.data, other.data):
                return True
            return all(np.array_equal(a, b) for a, b in zip(self, other))
        if isinstance(other, (list, tuple)):
            if len(self) != len(other):
                return False
            return all(np.array_equal(a, b) for a, b in zip(self, other))
        return NotImplemented

    def to_list(self) -> List[np.ndarray]:
        """Materialize as a Python list of copied 1D int64 arrays."""
        n = len(self)
        offs = self.offsets
        dat = self.data
        return [dat[int(offs[i]) : int(offs[i + 1])].copy() for i in range(n)]

    def copy(self) -> "CSRSequence":
        """Return a deep copy of this CSRSequence."""
        return CSRSequence(self.offsets.copy(), self.data.copy())

    def __repr__(self) -> str:
        return f"CSRSequence(len={len(self)}, nnz={len(self.data)})"


@dataclass
class UnstructuredMesh:
    """In-Memory Unstructured Mesh in DiFVM Structure of Arrays (SoA) format.

    Guarantees bit-for-bit compatibility with OpenFOAM FoamMesh and native C-ABI.
    """

    points: np.ndarray             # shape (N_p, 3), float64: vertex coordinates
    cells: Union[List[np.ndarray], CSRSequence]  # cell vertex index arrays (list or CSRSequence)
    owner: np.ndarray              # shape (N_f,), int64: owner cell index
    neighbour: np.ndarray          # shape (N_f,), int64: neighbour cell index (-1 on boundary)
    face_areas: np.ndarray         # shape (N_f, 3), float64: S_f vector pointing owner -> neighbour
    face_centres: np.ndarray       # shape (N_f, 3), float64: c_f face centroid
    cell_centres: np.ndarray       # shape (N_c, 3), float64: C_i cell centroid
    cell_volumes: np.ndarray       # shape (N_c,), float64: V_i > 0
    delta_f: np.ndarray            # shape (N_f, 3), float64: orthogonal projection vector
    k_f: np.ndarray                # shape (N_f, 3), float64: non-orthogonal correction, delta_f + k_f == S_f
    boundaries: Dict[str, Tuple[int, int]]  # patch_name -> (start_face, num_faces)
    cell_regions: Optional[np.ndarray] = None  # shape (N_c,), int32: region ID per cell (CHT multi-region)
    baffles: List[Dict[str, Any]] = field(default_factory=list)  # coupledWall / mappedWall twin-face interfaces
    cyclic_pairs: List[Tuple[int, int]] = field(default_factory=list)  # pairs of cyclic boundary face indices
    error_map: Dict[str, np.ndarray] = field(default_factory=dict)  # non_ortho, skewness, aspect_ratio, mask
    metadata: Dict[str, Any] = field(default_factory=dict)
    faces: Optional[Union[List[np.ndarray], CSRSequence]] = None  # Face vertex arrays (ordered by RHR)
    cell_types: Optional[np.ndarray] = None  # shape (N_c,), int32: VTK CellType per cell
    topological_dim: int = 3  # 2 for planar meshes, 3 for volumetric meshes
    face_offsets: Optional[np.ndarray] = None
    face_nodes: Optional[np.ndarray] = None
    cell_offsets: Optional[np.ndarray] = None
    cell_nodes: Optional[np.ndarray] = None

    def __post_init__(self) -> None:
        self.points = np.asarray(self.points, dtype=np.float64)
        self.owner = np.asarray(self.owner, dtype=np.int64)
        self.neighbour = np.asarray(self.neighbour, dtype=np.int64)
        self.face_areas = np.asarray(self.face_areas, dtype=np.float64)
        self.face_centres = np.asarray(self.face_centres, dtype=np.float64)
        self.cell_centres = np.asarray(self.cell_centres, dtype=np.float64)
        self.cell_volumes = np.asarray(self.cell_volumes, dtype=np.float64)
        self.delta_f = np.asarray(self.delta_f, dtype=np.float64)
        self.k_f = np.asarray(self.k_f, dtype=np.float64)
        if self.cell_regions is not None:
            self.cell_regions = np.asarray(self.cell_regions, dtype=np.int32)
        else:
            self.cell_regions = np.zeros(len(self.cell_volumes), dtype=np.int32)
        if self.cell_types is not None:
            self.cell_types = np.asarray(self.cell_types, dtype=np.int32)

        if self.face_offsets is not None:
            self.face_offsets = np.asarray(self.face_offsets, dtype=np.int64)
        if self.face_nodes is not None:
            self.face_nodes = np.asarray(self.face_nodes, dtype=np.int64)
        if self.cell_offsets is not None:
            self.cell_offsets = np.asarray(self.cell_offsets, dtype=np.int64)
        if self.cell_nodes is not None:
            self.cell_nodes = np.asarray(self.cell_nodes, dtype=np.int64)

        if self.face_offsets is not None and self.face_nodes is not None and (self.faces is None or len(self.faces) == 0):
            self.faces = CSRSequence(self.face_offsets, self.face_nodes)
        elif self.faces is not None and not isinstance(self.faces, CSRSequence):
            self.faces = [np.asarray(f, dtype=np.int64) for f in self.faces]

        if self.cell_offsets is not None and self.cell_nodes is not None and (self.cells is None or len(self.cells) == 0):
            self.cells = CSRSequence(self.cell_offsets, self.cell_nodes)

    @property
    def num_regions(self) -> int:
        return int(len(np.unique(self.cell_regions))) if self.cell_regions is not None else 1

    @property
    def is_multiregion(self) -> bool:
        return self.num_regions > 1

    @property
    def num_points(self) -> int:
        return len(self.points)

    @property
    def num_cells(self) -> int:
        return len(self.cell_volumes)

    @property
    def num_faces(self) -> int:
        return len(self.face_areas)

    @property
    def num_internal_faces(self) -> int:
        return int(np.sum(self.neighbour >= 0))

    @property
    def num_boundary_faces(self) -> int:
        return int(np.sum(self.neighbour < 0))

    def get_bounding_box(self) -> BoundingBox:
        """Compute spatial bounding box enclosing all mesh points."""
        return BoundingBox.from_points(self.points)

    def to_openfoam_polymesh(self, case_dir: Union[str, Path], write_binary: bool = False) -> Path:
        """Export mesh to OpenFOAM constant/polyMesh directory."""
        from backend.meshing.polymesh_io import export_openfoam_polymesh
        return export_openfoam_polymesh(self, case_dir=case_dir, write_binary=write_binary)

    def validate(
        self,
        tol_closure: float = 1e-11,
        tol_decomp: float = 1e-14,
        tol_rel: float = 1e-11,
        tol_proj: float = 1e-12,
        tol_relax: float = 1e-10,
        tol_centroid: float = 1e-3,
        max_aspect_ratio: float = 1000.0,
        check_containment: bool = True,
    ) -> bool:
        """Audit physical and topological invariants. Raises ValueError on violation."""
        if np.any(self.cell_volumes <= 0.0):
            bad_idx = np.where(self.cell_volumes <= 0.0)[0]
            raise ValueError(
                f"Mesh has {len(bad_idx)} cells with non-positive volume! Min: {np.min(self.cell_volumes)}"
            )

        internal = self.neighbour >= 0
        n_int = int(np.sum(internal))
        n_faces = self.num_faces

        # 1. Contiguous block ordering: all internal faces must precede boundary faces
        if n_faces > 0:
            if not np.all(self.neighbour[:n_int] >= 0) or not np.all(self.neighbour[n_int:] == -1):
                raise ValueError("Contiguous block ordering violated: all internal faces must precede boundary faces")

        # 1b. Boundary patch coverage validation
        if self.boundaries:
            n_bnd = self.num_boundary_faces
            coverage = np.zeros(n_bnd, dtype=bool)
            total_patch_faces = 0

            for pname, (start_f, count_f) in self.boundaries.items():
                if count_f < 0:
                    raise ValueError(f"Boundary patch '{pname}' has negative count {count_f}")
                if start_f < n_int or start_f + count_f > n_faces:
                    raise ValueError(
                        f"Boundary patch '{pname}' range [{start_f}, {start_f + count_f}) "
                        f"lies outside boundary range [{n_int}, {n_faces})"
                    )
                rel_start = start_f - n_int
                rel_end = rel_start + count_f
                if np.any(coverage[rel_start:rel_end]):
                    raise ValueError(
                        f"Boundary patch '{pname}' overlaps with another patch in range [{start_f}, {start_f + count_f})"
                    )
                coverage[rel_start:rel_end] = True
                total_patch_faces += count_f

            if total_patch_faces != n_bnd:
                raise ValueError(
                    f"Boundary patches cover {total_patch_faces} faces, expected {n_bnd} faces"
                )

        # 2. Strict upper-triangular ordering on internal faces: owner < neighbour
        if n_int > 0:
            if np.any(self.owner[internal] >= self.neighbour[internal]):
                bad = np.where(self.owner[internal] >= self.neighbour[internal])[0]
                raise ValueError(
                    f"Topological invariant violated: owner must be strictly less than neighbour on all internal faces ({len(bad)} violations)"
                )

        # 3. Vectorized scale-invariant Gauss divergence closure theorem
        if n_faces > 0 and self.num_cells > 0:
            cell_surface_area = np.zeros(self.num_cells, dtype=np.float64)
            face_area_mag = np.linalg.norm(self.face_areas, axis=1)
            np.add.at(cell_surface_area, self.owner, face_area_mag)
            np.add.at(cell_surface_area, self.neighbour[internal], face_area_mag[internal])

            closure = np.zeros_like(self.cell_centres)
            np.add.at(closure, self.owner, self.face_areas)
            np.subtract.at(closure, self.neighbour[internal], self.face_areas[internal])

            closure_norm = np.linalg.norm(closure, axis=1)
            rel_closure = np.where(cell_surface_area > 1e-30, closure_norm / cell_surface_area, closure_norm)
            max_rel_closure = float(np.max(rel_closure))

            effective_tol = tol_closure if tol_closure != 1e-11 else tol_rel
            if max_rel_closure > effective_tol:
                raise ValueError(
                    f"Closed-cell Gauss divergence theorem violated: max error = {max_rel_closure:.2e} > {effective_tol:.2e}"
                )

        # 4. Non-orthogonal decomposition identity: delta_f + k_f == S_f
        if n_int > 0:
            faces_to_check = internal
        else:
            faces_to_check = (
                np.any(np.abs(self.k_f) > 1e-12, axis=1)
                if len(self.k_f) > 0
                else np.array([], dtype=bool)
            )

        if (
            np.any(faces_to_check)
            and len(self.face_areas) > 0
            and len(self.delta_f) > 0
            and len(self.k_f) > 0
        ):
            decomp_diff = float(
                np.max(
                    np.linalg.norm(
                        (self.delta_f[faces_to_check] + self.k_f[faces_to_check])
                        - self.face_areas[faces_to_check],
                        axis=1,
                    )
                )
            )
            if decomp_diff > tol_decomp:
                raise ValueError(
                    f"Non-orthogonal decomposition identity failed: max diff = {decomp_diff:.2e} > {tol_decomp:.2e}"
                )

        # 5. True projection identity tests (collinearity and over-relaxation on internal faces)
        if n_int > 0:
            delta_int = self.delta_f[internal]
            d_f = self.cell_centres[self.neighbour[internal]] - self.cell_centres[self.owner[internal]]
            delta_norm = np.linalg.norm(delta_int, axis=1)
            d_norm = np.linalg.norm(d_f, axis=1)
            denom = delta_norm * d_norm
            valid_collin = denom > 1e-30
            if np.any(valid_collin):
                cross_prod = np.cross(delta_int[valid_collin], d_f[valid_collin])
                cross_mag = np.linalg.norm(cross_prod, axis=1)
                collin_err = cross_mag / denom[valid_collin]
                max_collin_err = float(np.max(collin_err))
                if max_collin_err > tol_proj:
                    raise ValueError(
                        f"Projection collinearity violated: max error = {max_collin_err:.2e} > {tol_proj:.2e}"
                    )

            S_int = self.face_areas[internal]
            S_norm_sq = np.sum(S_int * S_int, axis=1)
            cos_theta = np.sum(S_int * d_f, axis=1) / (np.linalg.norm(S_int, axis=1) * d_norm + 1e-30)
            cos_theta = np.clip(cos_theta, -1.0, 1.0)
            theta_deg = np.degrees(np.arccos(cos_theta))
            relax_mask = (theta_deg < 89.0) & (S_norm_sq > 1e-30)
            if np.any(relax_mask):
                dot_delta_S = np.sum(delta_int[relax_mask] * S_int[relax_mask], axis=1)
                rel_relax_err = np.abs(dot_delta_S - S_norm_sq[relax_mask]) / S_norm_sq[relax_mask]
                max_relax_err = float(np.max(rel_relax_err))
                if max_relax_err > tol_relax:
                    raise ValueError(
                        f"Over-relaxation identity delta_f . S_f == ||S_f||^2 violated: max error = {max_relax_err:.2e} > {tol_relax:.2e}"
                    )

        # 6. Cell bounding box containment & elongation check
        if check_containment and self.num_cells > 0 and len(self.points) > 0:
            if self.cells is not None and len(self.cells) == self.num_cells:
                for c_idx, cell_verts in enumerate(self.cells):
                    if len(cell_verts) == 0:
                        continue
                    c_pts = self.points[cell_verts]
                    b_min = np.min(c_pts, axis=0)
                    b_max = np.max(c_pts, axis=0)
                    span = b_max - b_min
                    char_h = float(np.max(span))
                    eps = 1e-5 * max(char_h, 1e-12)
                    C_c = self.cell_centres[c_idx]

                    if np.any(C_c < b_min - eps) or np.any(C_c > b_max + eps):
                        raise ValueError(
                            f"Cell {c_idx} centroid {C_c} lies outside its bounding box "
                            f"[{b_min}, {b_max}] with tolerance {eps:.2e}"
                        )

                    min_span = float(np.min(span))
                    if min_span > 1e-15:
                        elongation = char_h / min_span
                        if elongation > max_aspect_ratio:
                            raise ValueError(
                                f"Cell {c_idx} elongation {elongation:.1f} exceeds maximum allowed {max_aspect_ratio}"
                            )

        # 7. Global Centroid Divergence Theorem balance check
        bnd_mask = self.neighbour < 0
        n_bnd_faces = int(np.sum(bnd_mask))
        if n_bnd_faces > 0 and self.num_cells > 0 and len(self.face_centres) == self.num_faces:
            total_vol = float(np.sum(self.cell_volumes))
            if total_vol > 1e-15:
                M_cells = np.sum(self.cell_centres * self.cell_volumes[:, None], axis=0)
                d_dim = float(max(1, self.topological_dim))
                cf_bnd = self.face_centres[bnd_mask]
                sf_bnd = self.face_areas[bnd_mask]
                if d_dim == 2:
                    x0 = np.mean(self.points, axis=0) if len(self.points) > 0 else np.zeros(3)
                    cf_rel = cf_bnd - x0
                    cf_dot_sf = np.sum(cf_rel * sf_bnd, axis=1)
                    M_boundary = x0 * total_vol + (1.0 / (d_dim + 1.0)) * np.sum(cf_rel * cf_dot_sf[:, None], axis=0)
                else:
                    cf_dot_sf = np.sum(cf_bnd * sf_bnd, axis=1)
                    M_boundary = (1.0 / (d_dim + 1.0)) * np.sum(cf_bnd * cf_dot_sf[:, None], axis=0)

                diff_moment = float(np.linalg.norm(M_cells - M_boundary))
                if len(self.points) > 0:
                    L_char = float(np.linalg.norm(np.max(self.points, axis=0) - np.min(self.points, axis=0)))
                else:
                    L_char = 1.0
                rel_centroid_err = diff_moment / (total_vol * max(L_char, 1e-12))
                if rel_centroid_err > tol_centroid:
                    raise ValueError(
                        f"Global centroid divergence theorem balance violated: "
                        f"rel error = {rel_centroid_err:.2e} > {tol_centroid:.2e} "
                        f"(||M_cells - M_boundary|| = {diff_moment:.4e}, total_vol = {total_vol:.4e})"
                    )

        return True

    def compute_quality_report(self) -> MeshQualityReport:
        """Evaluate and return a structured quality audit report."""
        closure = np.zeros_like(self.cell_centres)
        if self.num_faces > 0 and self.num_cells > 0:
            internal = self.neighbour >= 0
            np.add.at(closure, self.owner, self.face_areas)
            np.subtract.at(closure, self.neighbour[internal], self.face_areas[internal])
        max_closure = float(np.max(np.linalg.norm(closure, axis=1))) if len(closure) > 0 else 0.0

        non_ortho = self.error_map.get(
            "non_orthogonality",
            self.error_map.get("non_ortho", np.zeros(self.num_internal_faces)),
        )
        skewness = self.error_map.get(
            "skewness",
            self.error_map.get("skew", np.zeros(self.num_internal_faces)),
        )
        aspect_ratio = self.error_map.get(
            "aspect_ratio",
            np.ones(self.num_cells),
        )
        mask = self.error_map.get(
            "stabilization_mask",
            self.error_map.get("mask", np.zeros(self.num_internal_faces, dtype=np.int32)),
        )

        tier_counts = {tier: int(np.sum(mask == tier)) for tier in (0, 1, 2, 3)}

        min_vol = float(np.min(self.cell_volumes)) if len(self.cell_volumes) > 0 else 0.0
        max_vol = float(np.max(self.cell_volumes)) if len(self.cell_volumes) > 0 else 0.0
        tot_vol = float(np.sum(self.cell_volumes)) if len(self.cell_volumes) > 0 else 0.0

        j_ens_min = 1.0
        j_ens_mean = 1.0
        inv_pyr = 0
        if self.num_cells > 0 and len(self.cells) == self.num_cells:
            j_ens_arr, inv_pyr = compute_mesh_j_ens(self)
            if len(j_ens_arr) > 0:
                j_ens_min = float(np.min(j_ens_arr))
                j_ens_mean = float(np.mean(j_ens_arr))

        return MeshQualityReport(
            num_cells=self.num_cells,
            num_faces=self.num_faces,
            num_internal_faces=self.num_internal_faces,
            num_boundary_faces=self.num_boundary_faces,
            num_points=self.num_points,
            min_cell_volume=min_vol,
            max_cell_volume=max_vol,
            total_volume=tot_vol,
            max_closed_cell_divergence=max_closure,
            non_ortho_max=float(np.max(non_ortho)) if len(non_ortho) > 0 else 0.0,
            non_ortho_mean=float(np.mean(non_ortho)) if len(non_ortho) > 0 else 0.0,
            skewness_max=float(np.max(skewness)) if len(skewness) > 0 else 0.0,
            skewness_mean=float(np.mean(skewness)) if len(skewness) > 0 else 0.0,
            aspect_ratio_max=float(np.max(aspect_ratio)) if len(aspect_ratio) > 0 else 1.0,
            aspect_ratio_mean=float(np.mean(aspect_ratio)) if len(aspect_ratio) > 0 else 1.0,
            stabilization_tier_counts=tier_counts,
            is_valid=(min_vol > 0.0 and max_closure <= 1e-10 and inv_pyr == 0),
            j_ens_min=j_ens_min,
            j_ens_mean=j_ens_mean,
            inverted_pyramids=inv_pyr,
        )

    def to_gmsh_msh(self, file_path: Union[str, Path]) -> Path:
        """Export this mesh to Gmsh ``.msh`` v4.1 ASCII format."""
        from backend.meshing.mesh_bridge_io import export_gmsh_msh

        return export_gmsh_msh(self, file_path)

    def to_cgns(self, file_path: Union[str, Path]) -> Path:
        """Export this mesh to CGNS HDF5 format."""
        from backend.meshing.mesh_bridge_io import export_cgns

        return export_cgns(self, file_path)

    @classmethod
    def from_gmsh_msh(cls, file_path: Union[str, Path]) -> "UnstructuredMesh":
        """Load an ``UnstructuredMesh`` from a Gmsh ``.msh`` v4.1 file."""
        from backend.meshing.mesh_bridge_io import import_gmsh_msh

        return import_gmsh_msh(file_path)

    @classmethod
    def from_cgns(cls, file_path: Union[str, Path]) -> "UnstructuredMesh":
        """Load an ``UnstructuredMesh`` from a CGNS HDF5 file."""
        from backend.meshing.mesh_bridge_io import import_cgns

        return import_cgns(file_path)


@dataclass
class SPHParticleSet:
    """Multiphysics SPH particle container with boundary support detection and coupling vectors."""

    positions: np.ndarray                     # shape (N, 3), float64
    velocities: np.ndarray                    # shape (N, 3), float64
    masses: np.ndarray                        # shape (N,), float64
    densities: np.ndarray                     # shape (N,), float64
    volumes: np.ndarray                       # shape (N,), float64: V_i = m_i / rho_i
    smoothing_lengths: np.ndarray             # shape (N,), float64: h_i
    div_r: Optional[np.ndarray] = None        # shape (N,), float64: div r indicator
    is_boundary: Optional[np.ndarray] = None  # shape (N,), bool: div r < 0.75 * d
    wall_vectors: Optional[np.ndarray] = None # shape (N, 3), float64: A_iS boundary area vector
    categories: Optional[np.ndarray] = None   # shape (N,), int32: 0=Core, 1=Buffer, 2=Ghost
    metadata: Dict[str, Any] = field(default_factory=dict)

    def __post_init__(self) -> None:
        self.positions = np.asarray(self.positions, dtype=np.float64)
        self.velocities = np.asarray(self.velocities, dtype=np.float64)
        self.masses = np.asarray(self.masses, dtype=np.float64)
        self.densities = np.asarray(self.densities, dtype=np.float64)
        self.volumes = np.asarray(self.volumes, dtype=np.float64)
        self.smoothing_lengths = np.asarray(self.smoothing_lengths, dtype=np.float64)

    @property
    def num_particles(self) -> int:
        return len(self.positions)

    def get_bounding_box(self) -> BoundingBox:
        return BoundingBox.from_points(self.positions)
