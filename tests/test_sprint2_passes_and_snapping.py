"""Fast L0 Verification Gate for Sprint 2 Pass Architecture, AUD-002 Snapping, AUD-031 Translation & AUD-029 Aspect Ratio."""

from __future__ import annotations

import os
import numpy as np
import pytest

from backend.meshing.invariants import assert_fvm_invariants, mesh_fingerprint
from backend.meshing.mesh_engine import MeshEngine
from backend.meshing.models import CurvatureSizingConfig, FarfieldPadding, MeshingConfig
from tests.test_sprint1_audit_and_invariants import _make_box, _make_icosphere

pytestmark = pytest.mark.l0


def test_aud002_sphere_wall_snapping_converges_on_refinement() -> None:
    sphere = _make_icosphere(radius=0.5, center=(0.0, 0.0, 0.0), subdivisions=3)
    analytic_area = 4.0 * np.pi * (0.5 ** 2)
    analytic_vol = (4.0 / 3.0) * np.pi * (0.5 ** 3)
    pad = FarfieldPadding(upstream=2.0, downstream=2.0, lateral_y=2.0, lateral_z=2.0)

    area_errs: list[float] = []
    vol_errs: list[float] = []
    snapped_fracs: list[float] = []

    for h0 in (0.20, 0.10):
        cfg = MeshingConfig(
            domain_mode="external_fluid",
            farfield_padding=pad,
            sizing=CurvatureSizingConfig(h0=h0, h_min=h0 * 0.5, h_max=h0 * 2.0),
        )
        mesh = MeshEngine(use_native=True, strict_native=True).mesh_hexcore(
            sphere, h0=h0, domain_mode="external_fluid", config=cfg
        )
        assert_fvm_invariants(
            mesh,
            expect_patches=("inlet", "outlet", "sides", "top", "bottom", "wall"),
        )
        w_start, w_cnt = mesh.boundaries["wall"]
        wall_area = float(np.linalg.norm(mesh.face_areas[w_start : w_start + w_cnt], axis=1).sum())
        area_errs.append(abs(wall_area - analytic_area) / analytic_area)

        pmin = mesh.points.min(axis=0)
        pmax = mesh.points.max(axis=0)
        dom_vol = float(np.prod(pmax - pmin))
        body_vol = dom_vol - float(mesh.cell_volumes.sum())
        vol_errs.append(abs(body_vol - analytic_vol) / analytic_vol)

        wall_vids = np.unique(
            np.concatenate([mesh.faces[i] for i in range(w_start, w_start + w_cnt)])
        )
        radii = np.linalg.norm(mesh.points[wall_vids], axis=1)
        snapped_fracs.append(float(np.mean(np.abs(radii - 0.5) <= 0.15 * h0)))

    assert area_errs[1] < area_errs[0], (
        f"AUD-002 regression: wall area error did not shrink on refinement: {area_errs}"
    )
    assert area_errs[1] < 0.05, f"Fine wall area error too high: {area_errs}"
    assert vol_errs[1] < 0.06, f"Fine body volume error too high: {vol_errs}"
    assert snapped_fracs[1] > 0.85, f"Snapped vertex fraction too low: {snapped_fracs}"


def test_aud031_translation_invariance_1e6() -> None:
    shift = (1.0e6, -2.0e6, 3.0e6)
    s0 = _make_icosphere(radius=0.5, center=(0.0, 0.0, 0.0), subdivisions=2)
    s1 = _make_icosphere(radius=0.5, center=shift, subdivisions=2)

    cfg = MeshingConfig(
        domain_mode="external_fluid",
        farfield_padding=FarfieldPadding(upstream=2.0, downstream=2.0, lateral_y=2.0, lateral_z=2.0),
        sizing=CurvatureSizingConfig(h0=0.25, h_min=0.125, h_max=0.50),
    )
    eng = MeshEngine(use_native=True, strict_native=True)
    m0 = eng.mesh_hexcore(s0, h0=0.25, domain_mode="external_fluid", config=cfg)
    m1 = eng.mesh_hexcore(s1, h0=0.25, domain_mode="external_fluid", config=cfg)

    assert_fvm_invariants(m0, expect_patches=("inlet", "outlet", "sides", "top", "bottom", "wall"))
    assert_fvm_invariants(
        m1,
        stored_tol=1e-6,
        expect_patches=("inlet", "outlet", "sides", "top", "bottom", "wall"),
    )

    assert m0.num_cells == m1.num_cells
    fp0 = mesh_fingerprint(m0)
    fp1 = mesh_fingerprint(m1)
    assert fp0["counts"] == fp1["counts"]
    assert fp0["patches"] == fp1["patches"]
    assert fp0["regions"] == fp1["regions"]

    vol0 = float(m0.cell_volumes.sum())
    vol1 = float(m1.cell_volumes.sum())
    assert abs(vol0 - vol1) / vol0 < 1e-10

    ws0, wc0 = m0.boundaries["wall"]
    ws1, wc1 = m1.boundaries["wall"]
    area0 = float(np.linalg.norm(m0.face_areas[ws0 : ws0 + wc0], axis=1).sum())
    area1 = float(np.linalg.norm(m1.face_areas[ws1 : ws1 + wc1], axis=1).sum())
    assert abs(area0 - area1) / area0 < 1e-10


def test_aud029_unit_cube_aspect_ratio_is_unity() -> None:
    box = _make_box((0.5, 0.5, 0.5))
    cfg = MeshingConfig(
        domain_mode="internal_fluid",
        sizing=CurvatureSizingConfig(h0=0.25, h_min=0.25, h_max=0.25),
    )
    mesh = MeshEngine(use_native=True, strict_native=True).mesh_hexcore(
        box, h0=0.25, domain_mode="internal_fluid", config=cfg
    )
    assert_fvm_invariants(mesh, expect_patches=("wall",))
    ar = np.asarray(mesh.error_map["aspect_ratio"], dtype=np.float64)
    assert len(ar) == mesh.num_cells
    assert float(np.max(np.abs(ar - 1.0))) < 1e-12


def test_i10_thread_determinism_1_vs_4_threads() -> None:
    sphere = _make_icosphere(radius=0.5, center=(0.0, 0.0, 0.0), subdivisions=2)
    eng = MeshEngine(use_native=True, strict_native=True)
    cfg = MeshingConfig(
        domain_mode="external_fluid",
        farfield_padding=FarfieldPadding(upstream=2.0, downstream=2.0, lateral_y=2.0, lateral_z=2.0),
        sizing=CurvatureSizingConfig(h0=0.20, h_min=0.10, h_max=0.40),
    )
    prev = os.environ.get("OMP_NUM_THREADS")
    try:
        os.environ["OMP_NUM_THREADS"] = "1"
        m1 = eng.mesh_hexcore(sphere, h0=0.20, domain_mode="external_fluid", config=cfg)
        os.environ["OMP_NUM_THREADS"] = "4"
        m4 = eng.mesh_hexcore(sphere, h0=0.20, domain_mode="external_fluid", config=cfg)
    finally:
        if prev is None:
            os.environ.pop("OMP_NUM_THREADS", None)
        else:
            os.environ["OMP_NUM_THREADS"] = prev

    assert mesh_fingerprint(m1) == mesh_fingerprint(m4)
