"""
case_generator.py
=================
Generiert voll funktionsfähige OpenFOAM-Case-Verzeichnisse basierend auf den
aktivierten Physik-Modulen aus der ModuleRegistry.
"""

from __future__ import annotations
import os
from pathlib import Path
from typing import Dict, Any, Optional
from ..physics_modules.registry import ModuleRegistry


class CaseGenerator:
    """Verwaltet und generiert OpenFOAM-Fallordner."""

    def __init__(self, registry: ModuleRegistry):
        self.registry = registry

    def create_case(
        self,
        case_name: str,
        target_dir: str,
        solver_name: str = "chtMultiRegionFoam",
        end_time: float = 1.0,
        delta_t: float = 1e-4,
        write_interval: float = 0.05,
        mpi_ranks: int = 16,
        p_weight: float = 1.5,
        e_weight: float = 1.0
    ) -> Path:
        """
        Erstellt einen vollständigen OpenFOAM-Case mit abgestimmter Hardware- und Physik-Konfiguration.
        """
        case_path = Path(target_dir) / case_name
        system_dir = case_path / "system"
        constant_dir = case_path / "constant"
        zero_dir = case_path / "0"

        for d in [system_dir, constant_dir, zero_dir]:
            d.mkdir(parents=True, exist_ok=True)

        # 1. controlDict erstellen
        self._write_control_dict(system_dir, solver_name, end_time, delta_t, write_interval)

        # 2. decomposeParDict (optimiert für hybride CPUs wie i7-14700KF)
        self._write_decompose_par_dict(system_dir, mpi_ranks, p_weight, e_weight)

        # 3. fvSchemes & fvSolution Basis
        self._write_fv_schemes(system_dir)
        self._write_fv_solution(system_dir)

        # 4. Modulspezifische Dictionaries schreiben
        for mod_name, mod in self.registry.get_all_modules().items():
            if mod.config.enabled:
                entries = mod.generate_dictionary_entries(str(case_path))
                for rel_path, content in entries.items():
                    target_file = case_path / rel_path
                    target_file.parent.mkdir(parents=True, exist_ok=True)
                    with open(target_file, "w", encoding="utf-8") as f:
                        f.write(content)

        return case_path

    def _write_control_dict(self, system_dir: Path, solver: str, end_time: float, delta_t: float, write_interval: float):
        content = f"""/*--------------------------------*- C++ -*----------------------------------*\\
| =========                 |                                                 |
| \\\\      /  F ield         | OpenFOAM: The Open Source CFD Toolbox           |
|  \\\\    /   O peration     | Datei: system/controlDict                       |
\\*---------------------------------------------------------------------------*/
FoamFile
{{
    version     2.0;
    format      ascii;
    class       dictionary;
    location    "system";
    object      controlDict;
}}

application     {solver};
startFrom       startTime;
startTime       0;
stopAt          endTime;
endTime         {end_time};
deltaT          {delta_t};
writeControl    adjustableRunTime;
writeInterval   {write_interval};
purgeWrite      5;
writeFormat     ascii;
writePrecision  8;
writeCompression off;
timeFormat      general;
timePrecision   6;
runTimeModifiable true;
adjustTimeStep  yes;
maxCo           0.8;
"""
        with open(system_dir / "controlDict", "w", encoding="utf-8") as f:
            f.write(content)

    def _write_decompose_par_dict(self, system_dir: Path, ranks: int, p_weight: float, e_weight: float):
        content = f"""/*--------------------------------*- C++ -*----------------------------------*\\
| =========                 |                                                 |
| \\\\      /  F ield         | OpenFOAM: The Open Source CFD Toolbox           |
|  \\\\    /   O peration     | Datei: system/decomposeParDict                  |
|   \\\\  /    A nd           | Zweck: Hybride Lastverteilung (P-Core / E-Core) |
\\*---------------------------------------------------------------------------*/
FoamFile
{{
    version     2.0;
    format      ascii;
    class       dictionary;
    location    "system";
    object      decomposeParDict;
}}

numberOfSubdomains  {ranks};
method              scotch;

scotchCoeffs
{{
    // Gewichtung: P-Cores erhalten höheren Lastanteil als E-Cores
    // Verhindert Straggler-Effekte an MPI-Synchronisationsbarrieren
    processorWeights
    (
        // 8 P-Cores
        {p_weight} {p_weight} {p_weight} {p_weight} {p_weight} {p_weight} {p_weight} {p_weight}
        // 8 E-Cores
        {e_weight} {e_weight} {e_weight} {e_weight} {e_weight} {e_weight} {e_weight} {e_weight}
    );
}}
"""
        with open(system_dir / "decomposeParDict", "w", encoding="utf-8") as f:
            f.write(content)

    def _write_fv_schemes(self, system_dir: Path):
        content = """/*--------------------------------*- C++ -*----------------------------------*\\
FoamFile
{
    version     2.0;
    format      ascii;
    class       dictionary;
    location    "system";
    object      fvSchemes;
}
ddtSchemes
{
    default         Euler;
}
gradSchemes
{
    default         Gauss linear;
}
divSchemes
{
    default         none;
    div(phi,U)      Gauss limitedLinearV 1;
    div(phi,h)      Gauss limitedLinear 1;
    div(phi,k)      Gauss limitedLinear 1;
    div(phi,omega)  Gauss limitedLinear 1;
}
laplacianSchemes
{
    default         Gauss linear corrected;
}
interpolationSchemes
{
    default         linear;
}
snGradSchemes
{
    default         corrected;
}
"""
        with open(system_dir / "fvSchemes", "w", encoding="utf-8") as f:
            f.write(content)

    def _write_fv_solution(self, system_dir: Path):
        content = """/*--------------------------------*- C++ -*----------------------------------*\\
FoamFile
{
    version     2.0;
    format      ascii;
    class       dictionary;
    location    "system";
    object      fvSolution;
}
solvers
{
    p_rgh
    {
        solver          GAMG;
        smoother        DIC;
        tolerance       1e-7;
        relTol          0.01;
    }
    "(U|h|k|omega)"
    {
        solver          smoothSolver;
        smoother        symGaussSeidel;
        tolerance       1e-8;
        relTol          0.1;
    }
}
PIMPLE
{
    nOuterCorrectors 2;
    nCorrectors     2;
    nNonOrthogonalCorrectors 1;
}
"""
        with open(system_dir / "fvSolution", "w", encoding="utf-8") as f:
            f.write(content)
