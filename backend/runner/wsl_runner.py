"""
wsl_runner.py
=============
Headless Orchestrator für OpenFOAM-Simulationen unter WSL2.
Steuert MPI-Prozesse, extrahiert Residuen in Echtzeit und überwacht Divergenz.
"""

from __future__ import annotations

import math
import os
import re
import shlex
import subprocess
from dataclasses import dataclass, field
from pathlib import Path
from typing import Any, Dict, Generator, Iterable, List, Optional, Union


@dataclass
class ResidualEntry:
    """Repräsentiert die Konvergenz-Residuen einer gelösten Feldgleichung."""
    field: str
    initial_residual: float
    final_residual: float
    iterations: int


@dataclass
class IterationTelemetry:
    """Telemetriedaten eines OpenFOAM-Zeitschritts / einer Iteration."""
    time: float
    residuals: Dict[str, ResidualEntry] = field(default_factory=dict)
    courant_mean: Optional[float] = None
    courant_max: Optional[float] = None
    nan_detected: bool = False
    fatal_error: Optional[str] = None
    raw_lines: List[str] = field(default_factory=list)


class WslSimulationRunner:
    """Führt OpenFOAM-Workflows via WSL2 headless aus und parst Output mit strikter Fehlerprüfung."""

    TIME_PATTERN = re.compile(r"^Time\s*=\s*([0-9eE\.\+\-]+)", re.M)
    SOLVING_PATTERN = re.compile(
        r"Solving for\s+([a-zA-Z0-9_\.\-]+),\s+Initial residual\s*=\s*([0-9eE\.\+\-]+|nan|NaN),\s+Final residual\s*=\s*([0-9eE\.\+\-]+|nan|NaN),\s+No Iterations\s+(\d+)"
    )
    COURANT_PATTERN = re.compile(
        r"Courant Number\s+mean:\s*([0-9eE\.\+\-]+|nan|NaN)\s+max:\s*([0-9eE\.\+\-]+|nan|NaN)"
    )
    NAN_PATTERN = re.compile(
        r"\b(NaN|nan|Infinity|inf)\b|Floating point exception|FOAM FATAL",
        re.IGNORECASE
    )

    def __init__(self, bashrc_path: str = "/usr/lib/openfoam/openfoam/etc/bashrc"):
        self.bashrc_path = bashrc_path
        self._default_bashrc = "/usr/lib/openfoam/openfoam/etc/bashrc"

    @staticmethod
    def to_wsl_path(win_path: Union[str, Path]) -> str:
        """Konvertiert Pfade sicher nach WSL (/mnt/<drive>/... oder POSIX).

        Unterstützt Windows-Pfade mit Laufwerksbuchstaben (G:\\...), Pfade ohne
        Laufwerksbuchstaben, relative Pfade und native POSIX-Pfade (/mnt/...).
        """
        raw_str = str(win_path).strip()
        if not raw_str:
            return ""

        # Wenn bereits ein POSIX-Pfad übergeben wurde
        if raw_str.startswith("/"):
            return raw_str.replace("\\", "/")

        p_str = raw_str.replace("\\", "/")

        # Match Windows Laufwerksbuchstabe wie C:/ oder G:/
        m = re.match(r"^([a-zA-Z]):/(.*)$", p_str)
        if m:
            drive = m.group(1).lower()
            rest = m.group(2).lstrip("/")
            return f"/mnt/{drive}/{rest}"

        # Drive-letter-less Pfad: Resolven
        try:
            p = Path(win_path).resolve()
            if p.drive:
                drive = p.drive[0].lower()
                parts = p.as_posix().split(":", 1)[1].lstrip("/")
                return f"/mnt/{drive}/{parts}"
            else:
                return p.as_posix()
        except Exception:
            return p_str

    def build_command(self, case_wsl_dir: str, command: str) -> str:
        """Verknüpft OpenFOAM-Umgebungsvariablen mit dem gewünschten Befehl unter Nutzung von shlex.quote."""
        quoted_bashrc = shlex.quote(self.bashrc_path)
        quoted_dir = shlex.quote(case_wsl_dir)
        return f"source {quoted_bashrc} && cd {quoted_dir} && {command}"

    def run_mesh(
        self,
        case_dir: Union[str, Path],
        mesh_command: str = "blockMesh",
        check: bool = True
    ) -> subprocess.CompletedProcess:
        """Erstellt das Gitter (z. B. blockMesh / snappyHexMesh).

        Args:
            case_dir: Verzeichnis des OpenFOAM-Cases.
            mesh_command: Auszuführender Meshing-Befehl.
            check: Wenn True, löst ein Exit-Code != 0 einen RuntimeError aus.

        Returns:
            CompletedProcess mit stdout und stderr.

        Raises:
            RuntimeError: Wenn der Meshing-Befehl fehlschlägt und check=True ist.
        """
        wsl_case = self.to_wsl_path(case_dir)
        if self.bashrc_path == self._default_bashrc:
            quoted_dir = shlex.quote(wsl_case)
            cmd = (
                "for rc in /usr/lib/openfoam/openfoam*/etc/bashrc "
                "/opt/openfoam*/etc/bashrc \"$HOME\"/OpenFOAM/*/etc/bashrc; do "
                "if [ -f \"$rc\" ]; then source \"$rc\" >/dev/null 2>&1 || true; break; fi; "
                "done; "
                f"cd {quoted_dir} && {mesh_command}"
            )
        else:
            cmd = self.build_command(wsl_case, mesh_command)
        proc = subprocess.run(["wsl", "bash", "-c", cmd], capture_output=True, text=True)
        if check and proc.returncode != 0:
            raise RuntimeError(
                f"Mesh generation '{mesh_command}' failed with exit code {proc.returncode}:\n"
                f"STDOUT:\n{proc.stdout}\nSTDERR:\n{proc.stderr}"
            )
        return proc

    def run_parallel(
        self,
        case_dir: Union[str, Path],
        solver: str,
        ranks: int = 16,
        check: bool = True
    ) -> subprocess.CompletedProcess:
        """Zerlegt den Case, startet MPI und rekonstruiert am Ende mit Fehlerprüfung.

        Args:
            case_dir: Verzeichnis des OpenFOAM-Cases.
            solver: Name des Solvers (z. B. simpleFoam, chtMultiRegionFoam).
            ranks: Anzahl der MPI-Ränge (muss > 0 sein).
            check: Wenn True, löst ein Exit-Code != 0 einen RuntimeError aus.

        Returns:
            CompletedProcess mit stdout und stderr.

        Raises:
            ValueError: Wenn ranks <= 0 ist.
            RuntimeError: Wenn die Simulation fehlschlägt und check=True ist.
        """
        if ranks <= 0:
            raise ValueError(f"mpi_ranks must be a positive integer, got {ranks}")

        wsl_case = self.to_wsl_path(case_dir)
        quoted_solver = shlex.quote(solver)
        full_script = (
            f"decomposePar -force && "
            f"mpirun -np {ranks} {quoted_solver} -parallel > log.{quoted_solver} 2>&1 && "
            f"reconstructPar -latestTime"
        )
        cmd = self.build_command(wsl_case, full_script)
        proc = subprocess.run(["wsl", "bash", "-c", cmd], capture_output=True, text=True)
        if check and proc.returncode != 0:
            raise RuntimeError(
                f"Parallel simulation '{solver}' (np={ranks}) failed with exit code {proc.returncode}:\n"
                f"STDOUT:\n{proc.stdout}\nSTDERR:\n{proc.stderr}"
            )
        return proc

    def parse_log_stream(
        self,
        lines: Iterable[str],
        raise_on_divergence: bool = False
    ) -> Generator[IterationTelemetry, None, None]:
        """Parst OpenFOAM-Logzeilen streaming-fähig und liefert IterationTelemetry pro Zeitschritt.

        Extrahiert:
        - Time: Aktuelle Simulationszeit
        - Courant-Zahlen: mean und max
        - Residuen: Initial residual, Final residual, Iterations pro Feld
        - Divergenz / NaN: Überwachung von ungültigen Werten und FATAL ERROR
        """
        curr: Optional[IterationTelemetry] = None

        for line in lines:
            # Zeitschritt-Beginn erkennen
            t_match = re.search(r"^Time\s*=\s*([0-9eE\.\+\-]+)", line)
            if t_match:
                if curr is not None:
                    yield curr
                try:
                    t_val = float(t_match.group(1))
                except ValueError:
                    t_val = 0.0
                curr = IterationTelemetry(time=t_val, raw_lines=[line])
                continue

            if curr is None:
                continue

            curr.raw_lines.append(line)

            # Courant-Zahl
            c_match = self.COURANT_PATTERN.search(line)
            if c_match:
                mean_str, max_str = c_match.group(1), c_match.group(2)
                try:
                    curr.courant_mean = float(mean_str)
                except ValueError:
                    curr.courant_mean = float("nan")
                    curr.nan_detected = True
                try:
                    curr.courant_max = float(max_str)
                    if math.isnan(curr.courant_max) or math.isinf(curr.courant_max):
                        curr.nan_detected = True
                except ValueError:
                    curr.courant_max = float("nan")
                    curr.nan_detected = True

            # Residuen parsen
            s_match = self.SOLVING_PATTERN.search(line)
            if s_match:
                field_name = s_match.group(1)
                init_str = s_match.group(2)
                final_str = s_match.group(3)
                iter_count = int(s_match.group(4))

                try:
                    init_res = float(init_str)
                    if math.isnan(init_res) or math.isinf(init_res):
                        curr.nan_detected = True
                except ValueError:
                    init_res = float("nan")
                    curr.nan_detected = True

                try:
                    final_res = float(final_str)
                    if math.isnan(final_res) or math.isinf(final_res):
                        curr.nan_detected = True
                except ValueError:
                    final_res = float("nan")
                    curr.nan_detected = True

                curr.residuals[field_name] = ResidualEntry(
                    field=field_name,
                    initial_residual=init_res,
                    final_residual=final_res,
                    iterations=iter_count,
                )

            # NaN oder fatal error im Output erkennen
            if self.NAN_PATTERN.search(line):
                curr.nan_detected = True
                if "FOAM FATAL" in line or "exception" in line.lower():
                    curr.fatal_error = line.strip()

            if raise_on_divergence and curr.nan_detected:
                raise FloatingPointError(
                    f"Divergence or NaN detected during simulation at Time = {curr.time}: {line.strip()}"
                )

        if curr is not None:
            yield curr

    def parse_log_text(self, log_text: str, raise_on_divergence: bool = False) -> List[IterationTelemetry]:
        """Parst einen kompletten Log-String und gibt alle Zeitschritte zurück."""
        return list(self.parse_log_stream(log_text.splitlines(), raise_on_divergence=raise_on_divergence))

    def parse_log_file(self, log_path: Union[str, Path], raise_on_divergence: bool = False) -> List[IterationTelemetry]:
        """Liest eine Log-Datei und gibt alle geparsten IterationTelemetry-Einträge zurück."""
        p = Path(log_path).resolve()
        if not p.is_file():
            raise FileNotFoundError(f"OpenFOAM log file not found: {log_path}")
        with open(p, "r", encoding="utf-8", errors="replace") as f:
            return list(self.parse_log_stream(f, raise_on_divergence=raise_on_divergence))
