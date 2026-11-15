"""
case_generator.py
=================
Generiert voll funktionsfähige OpenFOAM-Case-Verzeichnisse basierend auf den
aktivierten Physik-Modulen aus der ModuleRegistry.
"""

from __future__ import annotations
import os
from pathlib import Path
from typing import Dict, Any, Optional, List, Union
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
        e_weight: float = 1.0,
        patches: Optional[Dict[str, str]] = None,
        initial_conditions: Optional[Dict[str, Any]] = None,
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

        # 4. 0/ Initial Fields generieren
        self._write_initial_fields(zero_dir, patches, solver_name, initial_conditions)

        # 5. Modulspezifische Dictionaries schreiben
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
        if ranks <= 0:
            raise ValueError(f"mpi_ranks must be a positive integer, got {ranks}")
        if p_weight <= 0 or e_weight <= 0:
            raise ValueError(f"processor weights must be strictly positive, got p_weight={p_weight}, e_weight={e_weight}")

        n_p = min(ranks, 8)
        n_e = max(0, ranks - 8)
        weights = [p_weight] * n_p + [e_weight] * n_e

        weight_lines = []
        for i in range(0, ranks, 8):
            chunk = weights[i:i + 8]
            weight_lines.append("        " + " ".join(f"{w:.2f}" for w in chunk))
        weights_formatted = "\n".join(weight_lines)

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
    // Dynamische Gewichtung passend zu numberOfSubdomains (Audit-H6)
    processorWeights
    (
{weights_formatted}
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
    p
    {
        solver          GAMG;
        smoother        DIC;
        tolerance       1e-7;
        relTol          0.01;
    }
    pFinal
    {
        $p;
        relTol          0;
    }
    p_rgh
    {
        solver          GAMG;
        smoother        DIC;
        tolerance       1e-7;
        relTol          0.01;
    }
    p_rghFinal
    {
        $p_rgh;
        relTol          0;
    }
    U
    {
        solver          smoothSolver;
        smoother        symGaussSeidel;
        tolerance       1e-8;
        relTol          0.1;
    }
    "(h|e|T|k|omega|epsilon|alphat|nut|Yi|Ydefault.*)"
    {
        $U;
    }
    "(U|h|e|T|k|omega|epsilon|alphat|nut|Yi|Ydefault.*)Final"
    {
        $U;
        relTol          0;
    }
}
SIMPLE
{
    nNonOrthogonalCorrectors 0;
    residualControl
    {
        p               1e-5;
        U               1e-5;
        "(h|e|T|k|omega|epsilon)" 1e-5;
    }
}
PIMPLE
{
    nOuterCorrectors 2;
    nCorrectors     2;
    nNonOrthogonalCorrectors 1;
    residualControl
    {
        p               1e-4;
        U               1e-4;
        "(h|e|T|k|omega|epsilon)" 1e-4;
    }
}
relaxationFactors
{
    fields
    {
        p               0.3;
        p_rgh           0.3;
    }
    equations
    {
        U               0.7;
        h               0.7;
        e               0.7;
        T               0.7;
        k               0.7;
        omega           0.7;
        epsilon         0.7;
        "(Yi|Ydefault.*)" 0.7;
    }
}
"""
        with open(system_dir / "fvSolution", "w", encoding="utf-8") as f:
            f.write(content)

    def _write_initial_fields(
        self,
        zero_dir: Path,
        patches: Optional[Dict[str, str]],
        solver_name: str,
        initial_conditions: Optional[Dict[str, Any]] = None,
    ) -> None:
        """Erstellt lauffähige OpenFOAM 0/-Startfelder basierend auf aktiven Modulen."""
        init_conds = dict(initial_conditions or {})
        patch_map = dict(patches or {
            "inlet": "patch",
            "outlet": "patch",
            "wall": "wall",
            "frontAndBack": "empty",
        })

        is_compressible = any(
            s in solver_name.lower()
            for s in ("cht", "rho", "reacting", "buoyant", "sonic", "compressible")
        )

        active_mods = {
            name: mod for name, mod in self.registry.get_all_modules().items()
            if mod.config.enabled
        }

        # 1. Flow fields (U, p)
        # Velocity U
        u_init = init_conds.get("U", "(0 0 0)")
        if isinstance(u_init, (list, tuple)):
            u_init = f"({u_init[0]} {u_init[1]} {u_init[2]})"
        self._write_single_field(
            zero_dir / "U",
            class_name="volVectorField",
            object_name="U",
            dimensions="[0 1 -1 0 0 0 0]",
            internal_field=f"uniform {u_init}",
            patch_map=patch_map,
            field_type="vector",
            field_name="U",
        )

        # Pressure p
        if is_compressible:
            p_dims = "[1 -1 -2 0 0 0 0]"
            p_init = init_conds.get("p", "101325")
        else:
            p_dims = "[0 2 -2 0 0 0 0]"
            p_init = init_conds.get("p", "0")
        self._write_single_field(
            zero_dir / "p",
            class_name="volScalarField",
            object_name="p",
            dimensions=p_dims,
            internal_field=f"uniform {p_init}",
            patch_map=patch_map,
            field_type="scalar",
            field_name="p",
            is_compressible=is_compressible,
        )

        # Dynamic/Buoyant pressure p_rgh
        if is_compressible or "p_rgh" in init_conds:
            p_rgh_init = init_conds.get("p_rgh", "101325")
            self._write_single_field(
                zero_dir / "p_rgh",
                class_name="volScalarField",
                object_name="p_rgh",
                dimensions="[1 -1 -2 0 0 0 0]",
                internal_field=f"uniform {p_rgh_init}",
                patch_map=patch_map,
                field_type="scalar",
                field_name="p_rgh",
                is_compressible=True,
            )

        # 2. Temperature T & thermal alphat
        if "Energy & Heat" in active_mods or is_compressible or "T" in init_conds:
            t_init = init_conds.get("T", "300")
            self._write_single_field(
                zero_dir / "T",
                class_name="volScalarField",
                object_name="T",
                dimensions="[0 0 0 1 0 0 0]",
                internal_field=f"uniform {t_init}",
                patch_map=patch_map,
                field_type="scalar",
                field_name="T",
            )
            self._write_single_field(
                zero_dir / "alphat",
                class_name="volScalarField",
                object_name="alphat",
                dimensions="[1 -1 -1 0 0 0 0]",
                internal_field="uniform 0",
                patch_map=patch_map,
                field_type="scalar",
                field_name="alphat",
            )

        # 3. Turbulence fields (k, omega, epsilon, nut)
        if "Viscous & Turbulence" in active_mods or any(k in init_conds for k in ("k", "omega", "nut")):
            k_init = init_conds.get("k", "0.1")
            omega_init = init_conds.get("omega", "10")
            epsilon_init = init_conds.get("epsilon", "0.01")
            self._write_single_field(
                zero_dir / "k",
                class_name="volScalarField",
                object_name="k",
                dimensions="[0 2 -2 0 0 0 0]",
                internal_field=f"uniform {k_init}",
                patch_map=patch_map,
                field_type="scalar",
                field_name="k",
            )
            self._write_single_field(
                zero_dir / "omega",
                class_name="volScalarField",
                object_name="omega",
                dimensions="[0 0 -1 0 0 0 0]",
                internal_field=f"uniform {omega_init}",
                patch_map=patch_map,
                field_type="scalar",
                field_name="omega",
            )
            self._write_single_field(
                zero_dir / "epsilon",
                class_name="volScalarField",
                object_name="epsilon",
                dimensions="[0 2 -3 0 0 0 0]",
                internal_field=f"uniform {epsilon_init}",
                patch_map=patch_map,
                field_type="scalar",
                field_name="epsilon",
            )
            self._write_single_field(
                zero_dir / "nut",
                class_name="volScalarField",
                object_name="nut",
                dimensions="[0 2 -1 0 0 0 0]",
                internal_field="uniform 0",
                patch_map=patch_map,
                field_type="scalar",
                field_name="nut",
            )

        # 4. Species fields
        if "Species & Chemistry" in active_mods:
            spec_mod = active_mods["Species & Chemistry"]
            species_list = spec_mod.get_required_fields()
            for sp in species_list:
                f_name = sp
                sp_val = init_conds.get(f_name, "0")
                self._write_single_field(
                    zero_dir / f_name,
                    class_name="volScalarField",
                    object_name=f_name,
                    dimensions="[0 0 0 0 0 0 0]",
                    internal_field=f"uniform {sp_val}",
                    patch_map=patch_map,
                    field_type="scalar",
                    field_name="species",
                )

        # 5. Multiphase alpha.water
        if "Multiphase (VoF)" in active_mods or "alpha.water" in init_conds:
            alpha_init = init_conds.get("alpha.water", "0")
            self._write_single_field(
                zero_dir / "alpha.water",
                class_name="volScalarField",
                object_name="alpha.water",
                dimensions="[0 0 0 0 0 0 0]",
                internal_field=f"uniform {alpha_init}",
                patch_map=patch_map,
                field_type="scalar",
                field_name="alpha.water",
            )

    def _write_single_field(
        self,
        file_path: Path,
        class_name: str,
        object_name: str,
        dimensions: str,
        internal_field: str,
        patch_map: Dict[str, str],
        field_type: str,
        field_name: str,
        is_compressible: bool = False,
    ) -> None:
        """Schreibt eine einzelne syntaktisch valide OpenFOAM-Feld-Datei."""
        b_lines = []
        for patch, ptype in patch_map.items():
            p_lower = patch.lower()
            pt_lower = str(ptype).lower()

            if "empty" in pt_lower or "empty" in p_lower or patch == "frontAndBack":
                entry = "        type            empty;"
            elif "symmetry" in pt_lower or "sym" in p_lower:
                entry = "        type            symmetryPlane;"
            elif "cyclic" in pt_lower or "cyclic" in p_lower:
                entry = "        type            cyclic;"
            elif "mapped" in pt_lower or "mapped" in p_lower or "coupled" in p_lower:
                if field_name == "T":
                    entry = (
                        "        type            compressible::turbulentTemperatureCoupledBaffleMixed;\n"
                        "        value           uniform 300;\n"
                        "        Tnbr            T;\n"
                        "        kappaMethod     fluidThermo;"
                    )
                elif field_name == "p_rgh":
                    entry = "        type            fixedFluxPressure;\n        value           uniform 101325;"
                elif field_name == "k":
                    entry = "        type            kqRWallFunction;\n        value           uniform 0.1;"
                elif field_name == "omega":
                    entry = "        type            omegaWallFunction;\n        value           uniform 10;"
                elif field_name == "epsilon":
                    entry = "        type            epsilonWallFunction;\n        value           uniform 0.01;"
                elif field_name == "nut":
                    entry = "        type            nutkWallFunction;\n        value           uniform 0;"
                elif field_name == "alphat":
                    entry = "        type            alphatWallFunction;\n        value           uniform 0;"
                elif field_name == "U":
                    entry = "        type            noSlip;"
                else:
                    entry = "        type            zeroGradient;"
            elif "inlet" in p_lower or "inlet" in pt_lower:
                if field_name == "U":
                    entry = "        type            fixedValue;\n        value           uniform (0 0 0);"
                elif field_name == "p":
                    entry = "        type            zeroGradient;"
                elif field_name == "p_rgh":
                    entry = "        type            fixedFluxPressure;\n        value           uniform 101325;"
                elif field_name == "T":
                    entry = "        type            fixedValue;\n        value           uniform 300;"
                elif field_name == "k":
                    entry = "        type            fixedValue;\n        value           uniform 0.1;"
                elif field_name == "omega":
                    entry = "        type            fixedValue;\n        value           uniform 10;"
                elif field_name == "epsilon":
                    entry = "        type            fixedValue;\n        value           uniform 0.01;"
                elif field_name in ("nut", "alphat"):
                    entry = "        type            calculated;\n        value           uniform 0;"
                elif field_name in ("species", "alpha.water"):
                    entry = "        type            fixedValue;\n        value           uniform 0;"
                else:
                    entry = "        type            zeroGradient;"
            elif "outlet" in p_lower or "outlet" in pt_lower:
                if field_name == "p":
                    p_val = "101325" if is_compressible else "0"
                    entry = f"        type            fixedValue;\n        value           uniform {p_val};"
                elif field_name == "p_rgh":
                    entry = "        type            fixedValue;\n        value           uniform 101325;"
                else:
                    entry = "        type            zeroGradient;"
            elif "wall" in p_lower or "wall" in pt_lower:
                if field_name == "U":
                    entry = "        type            noSlip;"
                elif field_name == "p_rgh":
                    entry = "        type            fixedFluxPressure;\n        value           uniform 101325;"
                elif field_name == "k":
                    entry = "        type            kqRWallFunction;\n        value           uniform 0.1;"
                elif field_name == "omega":
                    entry = "        type            omegaWallFunction;\n        value           uniform 10;"
                elif field_name == "epsilon":
                    entry = "        type            epsilonWallFunction;\n        value           uniform 0.01;"
                elif field_name == "nut":
                    entry = "        type            nutkWallFunction;\n        value           uniform 0;"
                elif field_name == "alphat":
                    entry = "        type            alphatWallFunction;\n        value           uniform 0;"
                else:
                    entry = "        type            zeroGradient;"
            else:
                entry = "        type            zeroGradient;"

            b_lines.append(f"    {patch}\n    {{\n{entry}\n    }}")

        b_content = "\n".join(b_lines)
        file_content = f"""/*--------------------------------*- C++ -*----------------------------------*\\
| =========                 |                                                 |
| \\\\      /  F ield         | OpenFOAM: The Open Source CFD Toolbox           |
|  \\\\    /   O peration     | Datei: 0/{object_name}                          |
|   \\\\  /    A nd           |                                                 |
|    \\\\/     M anipulation  |                                                 |
\\*---------------------------------------------------------------------------*/
FoamFile
{{
    version     2.0;
    format      ascii;
    class       {class_name};
    location    "0";
    object      {object_name};
}}
// * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * //

dimensions      {dimensions};

internalField   {internal_field};

boundaryField
{{
{b_content}
}}

// ************************************************************************* //
"""
        with open(file_path, "w", encoding="utf-8") as f:
            f.write(file_content)
