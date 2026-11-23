"""Crash-Replay Diagnostic Flight Recorder (``crash_replay.toml``) and AMIPS Metric (§13.1 2.A).

Emits deterministic TOML reproducer bundles whenever ``FaceClass::invalid``,
NaN cell values, or topological inversions occur, alongside tetrahedral
AMIPS distortion energy diagnostics.
"""

from __future__ import annotations

from datetime import datetime, timezone
from pathlib import Path
from typing import Any, Dict, Optional, Sequence, Union

import numpy as np


def amips_tet(
    p0: np.ndarray,
    p1: np.ndarray,
    p2: np.ndarray,
    p3: np.ndarray,
) -> float:
    """Compute tetrahedral AMIPS shape distortion energy ``tr(J^T J) / det(J)^{2/3}``.

    Normalized by an equilateral reference tetrahedron so an ideal regular
    tetrahedron achieves ``3.0`` and inverted/degenerate elements return ``inf``.
    """
    a0 = np.asarray(p0, dtype=np.float64)
    a1 = np.asarray(p1, dtype=np.float64)
    a2 = np.asarray(p2, dtype=np.float64)
    a3 = np.asarray(p3, dtype=np.float64)

    e_mat = np.column_stack([a1 - a0, a2 - a0, a3 - a0])
    # Reference equilateral tetrahedron Jacobian inverse
    s_ideal_inv = np.array(
        [
            [1.0, -1.0 / np.sqrt(3.0), -1.0 / np.sqrt(6.0)],
            [0.0, 2.0 / np.sqrt(3.0), -1.0 / np.sqrt(6.0)],
            [0.0, 0.0, 3.0 / np.sqrt(6.0)],
        ],
        dtype=np.float64,
    )
    j_mat = e_mat @ s_ideal_inv
    det_j = float(np.linalg.det(j_mat))
    if not np.isfinite(det_j) or det_j <= 1e-30:
        return float("inf")
    frob_sq = float(np.sum(j_mat * j_mat))
    return frob_sq / (det_j ** (2.0 / 3.0))


def _toml_repr(val: Any) -> str:
    """Format a scalar/array cleanly for TOML output."""
    if isinstance(val, bool):
        return "true" if val else "false"
    if isinstance(val, (int, np.integer)):
        return str(int(val))
    if isinstance(val, (float, np.floating)):
        if np.isnan(val):
            return "nan"
        if np.isinf(val):
            return "inf" if val > 0 else "-inf"
        return repr(float(val))
    if isinstance(val, np.ndarray):
        val = val.tolist()
    if isinstance(val, (list, tuple)):
        return "[" + ", ".join(_toml_repr(x) for x in val) + "]"
    escaped = str(val).replace("\\", "\\\\").replace('"', '\\"')
    return f'"{escaped}"'


def write_crash_replay(
    file_path: Union[str, Path],
    *,
    reason: str = "FaceClass::invalid",
    cell_id: int = 0,
    face_id: Optional[int] = None,
    amips_energy: Optional[float] = None,
    vertices: Optional[Sequence[Sequence[float]]] = None,
    extra: Optional[Dict[str, Any]] = None,
) -> Path:
    """Write a ``crash_replay.toml`` diagnostic reproducer bundle.

    Args:
        file_path: Output TOML path.
        reason: Root failure trigger identifier (e.g. ``FaceClass::invalid``).
        cell_id: Failing cell index.
        face_id: Optional failing face index.
        amips_energy: Optional AMIPS energy value for the failing element.
        vertices: Optional element vertex coordinates.
        extra: Optional extra key/value diagnostics.

    Returns:
        Resolved ``Path`` of the emitted TOML bundle.
    """
    out_path = Path(file_path)
    out_path.parent.mkdir(parents=True, exist_ok=True)
    ts = datetime.now(timezone.utc).isoformat()

    lines = [
        "[crash_replay]",
        f"timestamp_utc = {_toml_repr(ts)}",
        f"reason = {_toml_repr(reason)}",
        f"cell_id = {int(cell_id)}",
    ]
    if face_id is not None:
        lines.append(f"face_id = {int(face_id)}")
    if amips_energy is not None:
        lines.append(f"amips_energy = {_toml_repr(float(amips_energy))}")
    if vertices is not None:
        lines.append(f"vertices = {_toml_repr([list(v) for v in vertices])}")
    if extra:
        lines.append("")
        lines.append("[diagnostics]")
        for k, v in sorted(extra.items()):
            lines.append(f"{k} = {_toml_repr(v)}")

    out_path.write_text("\n".join(lines) + "\n", encoding="utf-8")
    return out_path


__all__ = [
    "amips_tet",
    "write_crash_replay",
]
