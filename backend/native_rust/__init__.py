"""Native Rust modules and high-speed I/O interfaces for CFDSolver."""

from .foam_reader import FoamReader, FoamMesh, FoamBoundaryPatch

__all__ = ["FoamReader", "FoamMesh", "FoamBoundaryPatch"]
