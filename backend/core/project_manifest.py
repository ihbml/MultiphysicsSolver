"""Declarative ``project.toml`` Manifest & SHA-256 ``cache/*.mphys`` Invalidation (§13).

Provides deterministic TOML serialization/deserialization of solver+mesher setup
alongside canonical SHA-256 hashing for cache invalidation against ``*.mphys``
binary mesh/field caches.
"""

from __future__ import annotations

import hashlib
import json
import tomllib
from dataclasses import asdict, dataclass, field, is_dataclass
from pathlib import Path
from typing import Any, Dict, Optional, Union

import numpy as np


def _canonicalize_for_hash(obj: Any) -> Any:
    """Convert arbitrarily nested configs/arrays/paths into deterministic JSON-safe objects."""
    if is_dataclass(obj) and not isinstance(obj, type):
        return _canonicalize_for_hash(asdict(obj))
    if isinstance(obj, Path):
        return obj.as_posix()
    if isinstance(obj, np.ndarray):
        return {
            "__ndarray__": True,
            "dtype": str(obj.dtype),
            "shape": list(obj.shape),
            "sha256": hashlib.sha256(np.ascontiguousarray(obj).tobytes()).hexdigest(),
        }
    if isinstance(obj, (np.floating, float)):
        val = float(obj)
        if np.isnan(val):
            return "NaN"
        return float(f"{val:.15g}")
    if isinstance(obj, (np.integer, int)) and not isinstance(obj, bool):
        return int(obj)
    if isinstance(obj, (np.bool_, bool)):
        return bool(obj)
    if isinstance(obj, dict):
        return {str(k): _canonicalize_for_hash(v) for k, v in sorted(obj.items(), key=lambda kv: str(kv[0]))}
    if isinstance(obj, (list, tuple)):
        return [_canonicalize_for_hash(v) for v in obj]
    return obj


def compute_project_cache_hash(
    config: Union[str, Path, Dict[str, Any], Any],
    *,
    extra: Optional[Dict[str, Any]] = None,
) -> str:
    """Compute a deterministic 64-hex-char SHA-256 digest for project cache invalidation.

    Args:
        config: ``project.toml`` path, TOML text string, configuration dict, or dataclass.
        extra: Optional extra metadata dictionary folded into the canonical hash.

    Returns:
        Lowercase SHA-256 hex digest string (64 characters).
    """
    payload_obj: Any
    if isinstance(config, Path) or (isinstance(config, str) and Path(config).is_file()):
        raw_text = Path(config).read_text(encoding="utf-8")
        try:
            payload_obj = tomllib.loads(raw_text)
        except tomllib.TOMLDecodeError:
            payload_obj = {"raw_text": raw_text}
    elif isinstance(config, str):
        try:
            payload_obj = tomllib.loads(config)
        except tomllib.TOMLDecodeError:
            payload_obj = {"raw_text": config}
    else:
        payload_obj = config

    envelope: Dict[str, Any] = {"config": _canonicalize_for_hash(payload_obj)}
    if extra is not None:
        envelope["extra"] = _canonicalize_for_hash(extra)

    canonical_json = json.dumps(envelope, sort_keys=True, separators=(",", ":"), ensure_ascii=True)
    return hashlib.sha256(canonical_json.encode("utf-8")).hexdigest()


def _format_toml_value(val: Any) -> str:
    """Serialize a Python scalar/sequence to TOML syntax."""
    if isinstance(val, bool):
        return "true" if val else "false"
    if isinstance(val, (int, np.integer)):
        return str(int(val))
    if isinstance(val, (float, np.floating)):
        return repr(float(val))
    if isinstance(val, (list, tuple)):
        return "[" + ", ".join(_format_toml_value(x) for x in val) + "]"
    escaped = str(val).replace("\\", "\\\\").replace('"', '\\"')
    return f'"{escaped}"'


@dataclass
class ProjectManifest:
    """Declarative CFDSolver project.toml manifest."""

    project_name: str = "cfd_project"
    version: str = "1.0"
    cad_path: str = ""
    solver: str = "simpleFoam"
    domain_mode: str = "internal_fluid"
    meshing: Dict[str, Any] = field(default_factory=dict)
    physics: Dict[str, Any] = field(default_factory=dict)
    boundaries: Dict[str, Any] = field(default_factory=dict)
    cache_hash: str = ""

    def compute_hash(self) -> str:
        """Compute canonical SHA-256 hash excluding mutable ``cache_hash``."""
        data = asdict(self)
        data.pop("cache_hash", None)
        return compute_project_cache_hash(data)


def save_project_toml(manifest: Union[ProjectManifest, Dict[str, Any]], path: Union[str, Path]) -> Path:
    """Serialize a ``ProjectManifest`` or dict to a ``project.toml`` file."""
    out_path = Path(path)
    out_path.parent.mkdir(parents=True, exist_ok=True)
    if isinstance(manifest, ProjectManifest):
        data = asdict(manifest)
        if not data.get("cache_hash"):
            data["cache_hash"] = manifest.compute_hash()
    else:
        data = dict(manifest)

    top_lines = []
    sec_blocks = []
    for key, val in data.items():
        if isinstance(val, dict):
            sec_lines = [f"[{key}]"]
            for sk, sv in val.items():
                if sv is not None:
                    sec_lines.append(f"{sk} = {_format_toml_value(sv)}")
            sec_blocks.append("\n".join(sec_lines))
        elif val is not None:
            top_lines.append(f"{key} = {_format_toml_value(val)}")

    text = "\n".join(top_lines + ([""] if top_lines and sec_blocks else []) + sec_blocks) + "\n"
    out_path.write_text(text, encoding="utf-8")
    return out_path


def load_project_toml(path: Union[str, Path]) -> ProjectManifest:
    """Load and parse a ``project.toml`` manifest file."""
    in_path = Path(path)
    if not in_path.is_file():
        raise FileNotFoundError(f"project.toml not found: {in_path}")
    raw = tomllib.loads(in_path.read_text(encoding="utf-8"))
    return ProjectManifest(
        project_name=str(raw.get("project_name", "cfd_project")),
        version=str(raw.get("version", "1.0")),
        cad_path=str(raw.get("cad_path", "")),
        solver=str(raw.get("solver", "simpleFoam")),
        domain_mode=str(raw.get("domain_mode", "internal_fluid")),
        meshing=dict(raw.get("meshing", {})),
        physics=dict(raw.get("physics", {})),
        boundaries=dict(raw.get("boundaries", {})),
        cache_hash=str(raw.get("cache_hash", "")),
    )


__all__ = [
    "ProjectManifest",
    "compute_project_cache_hash",
    "load_project_toml",
    "save_project_toml",
]
