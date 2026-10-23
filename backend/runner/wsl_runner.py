"""
wsl_runner.py
=============
Headless Orchestrator für OpenFOAM-Simulationen unter WSL2.
Steuert MPI-Prozesse, extrahiert Residuen in Echtzeit und überwacht Divergenz.
"""

from __future__ import annotations
import subprocess
from pathlib import Path
from typing import Generator, Dict, Any, Optional


class WslSimulationRunner:
    """Führt OpenFOAM-Workflows via WSL2 headless aus und parst Output."""

    def __init__(self, bashrc_path: str = "/usr/lib/openfoam/openfoam/etc/bashrc"):
        self.bashrc_path = bashrc_path

    @staticmethod
    def to_wsl_path(win_path: str | Path) -> str:
        """Konvertiert Windows-Pfade (z. B. G:\\Datein\\...) nach /mnt/g/Datein/..."""
        p = Path(win_path).resolve()
        drive = p.drive[0].lower()
        parts = [p.as_posix().split(":", 1)[1].lstrip("/")]
        return f"/mnt/{drive}/{parts[0]}"

    def build_command(self, case_wsl_dir: str, command: str) -> str:
        """Verknüpft OpenFOAM-Umgebungsvariablen mit dem gewünschten Befehl."""
        return f"source {self.bashrc_path} && cd {case_wsl_dir} && {command}"

    def run_mesh(self, case_dir: str | Path) -> subprocess.CompletedProcess:
        """Erstellt das Gitter (z. B. blockMesh / snappyHexMesh)."""
        wsl_case = self.to_wsl_path(case_dir)
        cmd = self.build_command(wsl_case, "blockMesh")
        return subprocess.run(["wsl", "bash", "-c", cmd], capture_output=True, text=True)

    def run_parallel(self, case_dir: str | Path, solver: str, ranks: int = 16) -> subprocess.CompletedProcess:
        """Zerlegt den Case, startet MPI und rekonstruiert am Ende."""
        wsl_case = self.to_wsl_path(case_dir)
        full_script = (
            f"decomposePar -force && "
            f"mpirun -np {ranks} {solver} -parallel > log.{solver} 2>&1 && "
            f"reconstructPar -latestTime"
        )
        cmd = self.build_command(wsl_case, full_script)
        return subprocess.run(["wsl", "bash", "-c", cmd], capture_output=True, text=True)
