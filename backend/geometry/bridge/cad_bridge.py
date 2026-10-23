"""CAD Bridge Manager coordinating native CAD translation and open standard fallbacks."""

from __future__ import annotations

import logging
from pathlib import Path
from typing import Dict, List, Optional, Union

from backend.geometry.bridge.nx_translator import (
    NativeCADBridgeUnavailableError,
    NXTranslator,
)

logger = logging.getLogger(__name__)


class CADBridge:
    """Manages CAD system detection, native format translation, and fallback routing."""

    SUPPORTED_OPEN_FORMATS = {".stp", ".step", ".stl", ".obj"}
    NATIVE_FORMATS = {".prt", ".catpart", ".sldprt", ".ipt"}

    def __init__(self, nx_base_dir: Optional[Union[str, Path]] = None) -> None:
        """Initialize CADBridge with optional custom NX path."""
        self.nx_translator = NXTranslator(nx_base_dir=nx_base_dir)

    def detect_installations(self) -> Dict[str, Optional[Path]]:
        """Probe the host environment for local CAD installations.

        Returns:
            Dictionary of CAD system name to installation path (or None if absent).
        """
        installations: Dict[str, Optional[Path]] = {
            "Siemens_NX": self.nx_translator.nx_base_dir,
            "Autodesk_Inventor": None,
            "Ansys": None,
        }

        # Check Autodesk Inventor
        for p in [Path(r"C:\Program Files\Autodesk\Inventor 2026"), Path(r"C:\Program Files\Autodesk\Inventor")]:
            if p.is_dir():
                installations["Autodesk_Inventor"] = p
                break

        # Check Ansys
        for p in [Path(r"C:\Program Files\ANSYS Inc\v252"), Path(r"C:\Program Files\ANSYS Inc")]:
            if p.is_dir():
                installations["Ansys"] = p
                break

        return installations

    def can_translate(self, file_path: Union[str, Path]) -> bool:
        """Check if the given file format can be processed directly or translated.

        Args:
            file_path: Path to the input CAD geometry file.

        Returns:
            True if supported natively or via CAD translator, False otherwise.
        """
        ext = Path(file_path).suffix.lower()
        if ext in self.SUPPORTED_OPEN_FORMATS:
            return True
        if ext == ".prt" and self.nx_translator.is_available:
            return True
        return False

    def translate(
        self,
        input_file: Union[str, Path],
        output_format: str = "step",
        output_file: Optional[Union[str, Path]] = None,
        allow_fallback: bool = True,
        timeout: float = 120.0,
    ) -> Path:
        """Translate native CAD file into an open standard format, with graceful fallback.

        Args:
            input_file: Path to source geometry.
            output_format: Desired target format ('step', 'stl', 'obj').
            output_file: Optional explicit target path.
            allow_fallback: If True, fall back to existing open standards on translator failure.
            timeout: Subprocess timeout in seconds.

        Returns:
            Path to the resulting open standard file.

        Raises:
            NativeCADBridgeUnavailableError: If native translation fails and no fallback exists.
            FileNotFoundError: If input file does not exist.
        """
        in_path = Path(input_file).resolve()
        if not in_path.exists():
            raise FileNotFoundError(f"Input file not found: {in_path}")

        ext = in_path.suffix.lower()

        # If already an open standard, return directly
        if ext in self.SUPPORTED_OPEN_FORMATS:
            return in_path

        # If .prt, attempt NX translation
        if ext == ".prt":
            try:
                if not self.nx_translator.is_available:
                    raise NativeCADBridgeUnavailableError(
                        "Siemens NX is not detected on this system."
                    )
                out_stp, _ = self.nx_translator.translate_prt_to_step(
                    prt_path=in_path,
                    output_step=output_file,
                    protocol="AP214",
                    timeout=timeout,
                )
                logger.info("Successfully translated %s to %s via Siemens NX.", in_path.name, out_stp.name)
                return out_stp
            except Exception as ex:
                logger.warning(
                    "Native CAD translation failed for %s: %s", in_path.name, ex
                )
                if not allow_fallback:
                    raise

                fallback_path = self._find_fallback(in_path)
                if fallback_path:
                    logger.info(
                        "Gracefully falling back to open standard file: %s", fallback_path.name
                    )
                    return fallback_path
                raise NativeCADBridgeUnavailableError(
                    f"Native translation failed ({ex}) and no open standard fallback file "
                    f"was found in {in_path.parent}."
                ) from ex

        # Other native formats (CATPart, etc.)
        if allow_fallback:
            fallback = self._find_fallback(in_path)
            if fallback:
                logger.info("Using fallback open standard file for %s: %s", in_path.name, fallback.name)
                return fallback

        raise NativeCADBridgeUnavailableError(
            f"Unsupported native format '{ext}' for file {in_path.name} without available translator."
        )

    def _find_fallback(self, source_path: Path) -> Optional[Path]:
        """Search the directory of the source file for existing open standard representations."""
        stem = source_path.stem
        parent = source_path.parent

        # 1. Exact stem matches (.stp, .step, .stl, .obj)
        for ext in [".stp", ".step", ".stl", ".obj"]:
            candidate = parent / f"{stem}{ext}"
            if candidate.exists() and candidate.stat().st_size > 0:
                return candidate

        # 2. Case-insensitive pattern matches (e.g. TestCADSTPAP214.stp for TestCAD.prt)
        for cand in parent.glob(f"{stem}*"):
            if cand.suffix.lower() in self.SUPPORTED_OPEN_FORMATS and cand.stat().st_size > 0:
                return cand

        return None
