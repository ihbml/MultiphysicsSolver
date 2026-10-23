"""Siemens NX 1926 CLI Translator Bridge for native .prt geometry conversion."""

from __future__ import annotations

import logging
import os
from pathlib import Path
import re
import subprocess
from typing import Dict, Optional, Tuple, Union

logger = logging.getLogger(__name__)


class NativeCADBridgeUnavailableError(Exception):
    """Raised when native CAD bridge is not installed or cannot execute."""
    pass


class NXLicenseError(NativeCADBridgeUnavailableError):
    """Raised when Siemens NX license checkout fails."""
    pass


class NXTranslationError(NativeCADBridgeUnavailableError, RuntimeError):
    """Raised when Siemens NX translation process fails or outputs no solids."""
    pass


class NXTranslator:
    """Automates native Siemens NX 1926 translators (step214ug, step203ug, step242)."""

    DEFAULT_SEARCH_PATHS = [
        Path(r"C:\Program Files\Siemens\NX1926"),
        Path(r"C:\Program Files\Siemens\NX"),
    ]

    def __init__(self, nx_base_dir: Optional[Union[str, Path]] = None) -> None:
        """Initialize NX Translator.

        Args:
            nx_base_dir: Optional explicit base directory for Siemens NX installation.
        """
        self.nx_base_dir = self.detect_nx_installation(nx_base_dir)

    @classmethod
    def detect_nx_installation(cls, explicit_path: Optional[Union[str, Path]] = None) -> Optional[Path]:
        """Detect Siemens NX installation using environment, registry, and filesystem search.

        Returns:
            Path to NX root directory if found, otherwise None.
        """
        # 1. Explicit path (if provided, do not fall back to other paths)
        if explicit_path is not None:
            p = Path(explicit_path)
            if p.is_dir() and (p / "STEP214UG" / "step214ug.cmd").exists():
                return p
            return None

        # 2. Environment variable UGII_BASE_DIR
        env_ugii = os.environ.get("UGII_BASE_DIR")
        if env_ugii:
            p = Path(env_ugii)
            if p.is_dir() and (p / "STEP214UG" / "step214ug.cmd").exists():
                return p

        # 3. Check Windows Registry (if on Windows)
        if os.name == "nt":
            try:
                import winreg
                for subkey in (r"SOFTWARE\Siemens\NX", r"SOFTWARE\Unigraphics Solutions\NX"):
                    for root in (winreg.HKEY_LOCAL_MACHINE, winreg.HKEY_CURRENT_USER):
                        try:
                            with winreg.OpenKey(root, subkey) as key:
                                val, _ = winreg.QueryValueEx(key, "UGII_BASE_DIR")
                                if val:
                                    p = Path(val)
                                    if p.is_dir() and (p / "STEP214UG" / "step214ug.cmd").exists():
                                        return p
                        except OSError:
                            continue
            except Exception:
                pass

        # 4. Standard filesystem search paths
        for path in cls.DEFAULT_SEARCH_PATHS:
            if path.is_dir() and (path / "STEP214UG" / "step214ug.cmd").exists():
                return path

        return None

    @property
    def is_available(self) -> bool:
        """Return True if a valid Siemens NX installation is detected."""
        return self.nx_base_dir is not None

    def build_environment(self) -> Dict[str, str]:
        """Construct required NX environment variables with mandatory trailing backslashes."""
        if not self.nx_base_dir:
            raise NativeCADBridgeUnavailableError("Siemens NX installation not detected.")

        nx_dir = str(self.nx_base_dir)
        env = dict(os.environ)

        # Mandatory NX environment settings
        env["UGII_BASE_DIR"] = nx_dir
        env["UGII_ROOT_DIR"] = str(self.nx_base_dir / "UGII")

        # Crucial: trailing backslash is mandatory on STEP214UG_DIR and ROSE
        step214_dir = str(self.nx_base_dir / "STEP214UG") + os.sep
        step203_dir = str(self.nx_base_dir / "STEP203UG") + os.sep

        env["STEP214UG_DIR"] = step214_dir
        env["STEP203UG_DIR"] = step203_dir
        env["ROSE_DB"] = step214_dir
        env["ROSE"] = step214_dir

        # Add NXBIN and UGII to PATH
        nxbin = str(self.nx_base_dir / "NXBIN")
        ugii = str(self.nx_base_dir / "UGII")
        current_path = env.get("PATH", "")
        env["PATH"] = f"{nxbin};{ugii};{current_path}"

        return env

    def translate_prt_to_step(
        self,
        prt_path: Union[str, Path],
        output_step: Optional[Union[str, Path]] = None,
        protocol: str = "AP214",
        timeout: float = 120.0,
    ) -> Tuple[Path, Path]:
        """Translate native .prt part to STEP using step214ug.cmd / step203ug.cmd.

        Args:
            prt_path: Path to native .prt part file.
            output_step: Path to output STEP file (default: same name with .stp).
            protocol: 'AP214' or 'AP203'.
            timeout: Subprocess execution timeout in seconds (default 120s).

        Returns:
            Tuple of (output_step_path, log_file_path).

        Raises:
            NativeCADBridgeUnavailableError: If NX is not installed.
            NXLicenseError: If license checkout fails.
            NXTranslationError: If conversion fails or 0 solids processed.
        """
        if not self.is_available or not self.nx_base_dir:
            raise NativeCADBridgeUnavailableError(
                "Siemens NX 1926 is not available on this system. Unable to convert .prt natively."
            )

        prt = Path(prt_path).resolve()
        if not prt.exists():
            raise FileNotFoundError(f"Input .prt file not found: {prt}")

        if output_step is None:
            output_step = prt.with_suffix(".stp")
        out_stp = Path(output_step).resolve()
        out_log = out_stp.with_suffix(".log")

        # Select translator wrapper
        protocol_upper = protocol.upper()
        if protocol_upper in ("AP203", "203"):
            cmd_script = self.nx_base_dir / "STEP203UG" / "step203ug.cmd"
        else:
            cmd_script = self.nx_base_dir / "STEP214UG" / "step214ug.cmd"

        if not cmd_script.exists():
            raise NativeCADBridgeUnavailableError(f"NX translator script not found: {cmd_script}")

        env = self.build_environment()

        # Command syntax: cmd.exe /c ""{cmd_script}" "{prt}" o "{out_stp}" l "{out_log}"""
        cli_command = [str(cmd_script), str(prt), "o", str(out_stp), "l", str(out_log)]

        logger.info("Executing NX translator: %s", " ".join(cli_command))

        try:
            result = subprocess.run(
                cli_command,
                env=env,
                capture_output=True,
                text=True,
                timeout=timeout,
                shell=True,
                check=False,
            )
        except subprocess.TimeoutExpired as ex:
            raise NXTranslationError(
                f"Siemens NX translator timed out after {timeout} seconds on: {prt}"
            ) from ex

        # Parse log file for authoritative verification
        self._audit_nx_log(out_log, result.stdout, result.stderr)

        if not out_stp.exists() or out_stp.stat().st_size == 0:
            raise NXTranslationError(
                f"NX translation completed but output STEP file was not generated: {out_stp}"
            )

        return out_stp, out_log

    def _audit_nx_log(self, log_path: Path, stdout: str, stderr: str) -> None:
        """Parse NX translator log and stdout/stderr to verify conversion success."""
        combined_text = stdout + "\n" + stderr
        if log_path.exists():
            try:
                with open(log_path, "r", encoding="utf-8", errors="replace") as f:
                    combined_text += "\n" + f.read()
            except Exception as ex:
                logger.warning("Could not read NX translator log file %s: %s", log_path, ex)

        # 1. Check for license errors
        if "License Error" in combined_text or "Cannot connect to license server" in combined_text:
            license_server = os.environ.get("SPLM_LICENSE_SERVER", "unknown")
            raise NXLicenseError(
                f"Siemens NX license checkout failed. Check license server ({license_server})."
            )

        # 2. Check for solids processed
        # e.g.: "Number of solids processed without problems : 1"
        # or: "Entities Processed: Body: 1"
        processed_match = re.search(
            r"Number of solids processed without problems\s*:\s*(\d+)", combined_text, re.IGNORECASE
        )
        if not processed_match:
            processed_match = re.search(
                r"Entities Processed:\s*Body:\s*(\d+)", combined_text, re.IGNORECASE
            )

        if processed_match:
            num_solids = int(processed_match.group(1))
            if num_solids < 1:
                raise NXTranslationError(
                    f"Siemens NX processed 0 solids without problems (log: {log_path})"
                )
            logger.info("Siemens NX successfully processed %d solid(s).", num_solids)
        else:
            # Check if 100% completed
            if "100% Completed" not in combined_text and "SUMMARY" not in combined_text:
                raise NXTranslationError(
                    f"NX translation log does not confirm successful processing: {combined_text[-500:]}"
                )
