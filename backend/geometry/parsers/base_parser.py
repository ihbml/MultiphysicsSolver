"""Abstract Base Parser for 3D CAD and surface mesh formats."""

from __future__ import annotations

from abc import ABC, abstractmethod
from pathlib import Path
from typing import Any, Union

from backend.geometry.models import SurfaceMesh


class BaseParser(ABC):
    """Abstract base class for all geometry and CAD format parsers."""

    @abstractmethod
    def parse(self, file_path: Union[str, Path], **kwargs: Any) -> SurfaceMesh:
        """Parse geometry from a file into a triangulated SurfaceMesh.

        Args:
            file_path: Path to the CAD / mesh file.
            **kwargs: Format-specific parser configuration options.

        Returns:
            SurfaceMesh containing vertices, faces, normals, and patch definitions.

        Raises:
            FileNotFoundError: If the file does not exist.
            ValueError: If the file is invalid, corrupt, or unsupported.
        """
        pass

    @staticmethod
    def validate_file(file_path: Union[str, Path]) -> Path:
        """Validate that the file path exists and is a regular file."""
        p = Path(file_path)
        if not p.exists():
            raise FileNotFoundError(f"CAD file not found: {p}")
        if not p.is_file():
            raise ValueError(f"Path is not a regular file: {p}")
        return p
