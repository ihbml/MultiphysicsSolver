"""E2E Mesher Verification Suite for CFDSolver.

Provides opaque-box requirement-driven test infrastructure across Tiers 1-4:
- ``conftest``: Pytest marker registration, canonical Tier 4 asset paths,
  session-cached CAD surface loaders, native DLL env isolation, and fail-fast
  environment requirement fixtures.
- ``oracles``: Independent mathematical, topological, HDF5, polyMesh, CHT, and
  subprocess verification oracles.
- ``runner``: Standalone CLI test runner wrapping pytest with tier/marker
  filtering and JSON summary output.
"""

from __future__ import annotations

__all__ = [
    "conftest",
    "oracles",
    "runner",
]
