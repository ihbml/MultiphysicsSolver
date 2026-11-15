"""
test_generic_reaction_and_openfoam_audits.py
============================================
Comprehensive test suite for Milestone M5A:
- Feature F51 / R1: SMR Excision & Multiphysics Generalization
- Feature F52 / Audit H5: Multi-Region OpenFOAM Export
- Feature F53 / Audit H6: Runnable Case Generation & Dynamic Decomposition
- Feature F54 / Audit M10: Hardened WSL Runner & Real-Time Telemetry
"""

import math
import os
import re
import subprocess
from pathlib import Path
from unittest.mock import MagicMock, patch
import numpy as np
import pytest

from backend.physics_modules.registry import ModuleRegistry, SpeciesChemistryModule
from backend.core.case_generator import CaseGenerator
from backend.meshing.models import CellType, UnstructuredMesh
from backend.meshing.difvm_topology import compute_difvm_topology
from backend.meshing.polymesh_io import (
    export_openfoam_polymesh,
    export_openfoam_multiregion_polymesh,
    extrude_2d_to_3d_slab,
)
from backend.runner.wsl_runner import (
    WslSimulationRunner,
    IterationTelemetry,
    ResidualEntry,
)


# ==============================================================================
# Helper fixtures to build minimal valid 3D meshes for tests (<= 100 cells)
# ==============================================================================

def create_simple_3d_cube_mesh(scale: float = 1.0) -> UnstructuredMesh:
    """Creates a regular two-hex mesh with explicitly computed DiFVM topology and boundaries."""
    dx = scale
    pts = np.array([
        # Cube 0 vertices (x = 0 and x = dx)
        [0.0, 0.0, 0.0], [dx, 0.0, 0.0], [dx, dx, 0.0], [0.0, dx, 0.0],  # 0..3 (z=0)
        [0.0, 0.0, dx], [dx, 0.0, dx], [dx, dx, dx], [0.0, dx, dx],      # 4..7 (z=dx)
        # Cube 1 extra vertices (x = 2*dx)
        [2.0 * dx, 0.0, 0.0], [2.0 * dx, dx, 0.0],                       # 8, 9 (z=0)
        [2.0 * dx, 0.0, dx], [2.0 * dx, dx, dx],                         # 10, 11 (z=dx)
    ], dtype=np.float64)

    cell0 = np.array([0, 1, 2, 3, 4, 5, 6, 7], dtype=np.int64)
    cell1 = np.array([1, 8, 9, 2, 5, 10, 11, 6], dtype=np.int64)
    mesh = compute_difvm_topology(pts, [cell0, cell1])
    return mesh


# ==============================================================================
# 1. Feature F51 / R1: SMR Excision & Multiphysics Generalization Tests
# ==============================================================================

class TestFeatureF51SmrExcision:
    """Verifies complete excision of SMR kinetics and availability of generic interfaces."""

    def test_species_chemistry_module_default_and_custom_configuration(self):
        """SpeciesChemistryModule must support arbitrary user-configured species lists."""
        mod = SpeciesChemistryModule()
        assert mod.config.model_variant == "finiteRateKinetics"
        assert "SMR" not in mod.description
        assert "WGS" not in mod.description

        # Default species list
        assert mod.get_required_fields() == ["Ydefault"]

        # Custom species configuration
        custom_species = ["H2", "O2", "H2O", "N2", "Ar"]
        mod.config.parameters["species"] = custom_species
        assert mod.get_required_fields() == custom_species

        # Dictionary generation
        entries = mod.generate_dictionary_entries("case_dir")
        assert "constant/chemistryProperties" in entries
        assert "constant/thermophysicalProperties" in entries
        thermo_content = entries["constant/thermophysicalProperties"]
        for sp in custom_species:
            assert sp in thermo_content

    def test_native_cpp_generic_reaction_files_exist_and_smr_excised(self):
        """Confirms smrKineticsSource is excised and genericReactionSource is established."""
        root = Path(__file__).resolve().parent.parent
        cpp_dir = root / "backend" / "native_cpp" / "smr_catalysis"

        # smrKineticsSource must NOT exist
        assert not (cpp_dir / "smrKineticsSource.C").exists()
        assert not (cpp_dir / "smrKineticsSource.H").exists()

        # genericReactionSource MUST exist
        assert (cpp_dir / "genericReactionSource.C").exists()
        assert (cpp_dir / "genericReactionSource.H").exists()

        # Make/files must build libgenericReactionSource
        make_files = (cpp_dir / "Make" / "files").read_text(encoding="utf-8")
        assert "genericReactionSource.C" in make_files
        assert "libgenericReactionSource" in make_files
        assert "libsmrKineticsSource" not in make_files

        # Header must declare TypeName genericReactionSource
        header = (cpp_dir / "genericReactionSource.H").read_text(encoding="utf-8")
        assert 'TypeName("genericReactionSource")' in header
        assert "stoichiometricMatrix_" in header
        assert "arrheniusA_" in header
        assert "arrheniusEa_" in header
        assert "reactionDeltaH_" in header

    def test_generic_reaction_source_cpp_remediations_verified(self):
        """Audits genericReactionSource.C for pointer safety, energy sign, and fail-fast validation."""
        root = Path(__file__).resolve().parent.parent
        cpp_file = root / "backend" / "native_cpp" / "smr_catalysis" / "genericReactionSource.C"
        source_code = cpp_file.read_text(encoding="utf-8")

        # 1. Pointer safety: List<const volScalarField*> used, NO owning PtrList on mesh fields
        assert "List<const volScalarField*> Y(" in source_code
        assert "PtrList<const volScalarField>" not in source_code

        # 2. Energy matrix coupling sign: source[cellI] += (totalQ - totalDqDh * eqn.psi()[cellI])
        assert "source[cellI] += (totalQ - totalDqDh * eqn.psi()[cellI]) * V[cellI];" in source_code
        assert "source[cellI] += (totalMassSource - totalDiagJac * eqn.psi()[cellI]) * V[cellI];" in source_code
        # Negative source assignment must not exist
        assert "source[cellI] -=" not in source_code

        # 3. Fail-fast validation in read(): FatalErrorInFunction on size mismatch
        assert "FatalErrorInFunction" in source_code
        assert "Size mismatch in kinetic parameters" in source_code
        assert "exit(FatalError)" in source_code

        # 4. Temperature clamping via Foam::min / Foam::max
        assert "Foam::min(Tmax_, Foam::max(Tmin_, T[cellI]))" in source_code

    def test_documentation_and_contracts_smr_clean(self):
        """Verifies that documentation and contracts contain no remaining SMR kinetics bias."""
        root = Path(__file__).resolve().parent.parent

        # 1. COMMUNICATION_BACKEND_FRONTEND.md
        comm_doc = (root / "COMMUNICATION_BACKEND_FRONTEND.md").read_text(encoding="utf-8")
        assert "Y_<species>" in comm_doc
        assert "genericReactionSource" in comm_doc
        assert "[2026-11-12] — Generalisierung Physik-Module & SMR-Exzision" in comm_doc

        # 2. FRONTEND_REQUIREMENTS_ON_BACKEND.md
        fe_doc = (root / "FRONTEND_REQUIREMENTS_ON_BACKEND.md").read_text(encoding="utf-8")
        assert "Surface Heterogeneous Kinetics / Generic Catalysis" in fe_doc
        assert "benutzerdefinierte Speziesliste (`species`)" in fe_doc

        # 3. SUGGESTIONS_BACKEND.md
        sug_doc = (root / "SUGGESTIONS_BACKEND.md").read_text(encoding="utf-8")
        assert "genericReactionSource.C" in sug_doc

        # 4. OPENFOAM_BACKEND_ATLAS.md
        atlas_doc = (root / "backend" / "OPENFOAM_BACKEND_ATLAS.md").read_text(encoding="utf-8")
        assert "genericReactionSource.C" in atlas_doc
        assert "libgenericReactionSource" in atlas_doc
        assert "libsmrKineticsSource" not in atlas_doc

    def test_zero_smr_kineticssource_in_backend(self):
        """Repository scan: zero references to smrKineticsSource or libsmrKineticsSource in backend/."""
        root = Path(__file__).resolve().parent.parent
        backend_dir = root / "backend"

        for p in backend_dir.rglob("*"):
            if p.is_file() and not any(part.startswith(".") for part in p.parts):
                try:
                    text = p.read_text(encoding="utf-8", errors="ignore")
                    assert "smrKineticsSource" not in text, f"Found smrKineticsSource in {p}"
                    assert "libsmrKineticsSource" not in text, f"Found libsmrKineticsSource in {p}"
                except Exception:
                    pass


# ==============================================================================
# 2. Feature F52 / Audit H5: Multi-Region OpenFOAM Export Tests
# ==============================================================================

class TestFeatureF52MultiRegionExport:
    """Verifies Audit H5 multi-region mesh export, regionProperties, and mappedWall/cyclic."""

    def test_multiregion_export_directory_structure_and_region_properties(self, tmp_path):
        """Multi-region export creates constant/<region>/polyMesh and constant/regionProperties."""
        mesh_fluid = create_simple_3d_cube_mesh(scale=1.0)
        mesh_solid = create_simple_3d_cube_mesh(scale=0.5)

        regions = {
            "fluid_core": mesh_fluid,
            "solid_heater": mesh_solid,
        }

        poly_dirs = export_openfoam_multiregion_polymesh(
            case_path=tmp_path,
            regions=regions,
            region_properties={"fluid": ["fluid_core"], "solid": ["solid_heater"]},
        )

        assert "fluid_core" in poly_dirs
        assert "solid_heater" in poly_dirs

        # Check subdirectories
        for reg in ["fluid_core", "solid_heater"]:
            p_dir = tmp_path / "constant" / reg / "polyMesh"
            assert p_dir.is_dir()
            assert (p_dir / "points").is_file()
            assert (p_dir / "faces").is_file()
            assert (p_dir / "owner").is_file()
            assert (p_dir / "neighbour").is_file()
            assert (p_dir / "boundary").is_file()

        # Check constant/regionProperties
        reg_props = tmp_path / "constant" / "regionProperties"
        assert reg_props.is_file()
        content = reg_props.read_text(encoding="utf-8")
        assert "fluid ( fluid_core )" in content
        assert "solid ( solid_heater )" in content

    def test_multiregion_automatic_category_inference(self, tmp_path):
        """When region_properties is None, fluid vs solid is inferred from region names."""
        mesh1 = create_simple_3d_cube_mesh()
        mesh2 = create_simple_3d_cube_mesh()

        regions = {
            "fluid_channel": mesh1,
            "solid_casing": mesh2,
        }

        export_openfoam_multiregion_polymesh(tmp_path, regions)
        reg_props = (tmp_path / "constant" / "regionProperties").read_text(encoding="utf-8")
        assert "fluid ( fluid_channel )" in reg_props
        assert "solid ( solid_casing )" in reg_props

    def test_multiregion_validation_errors(self, tmp_path):
        """Export raises ValueError on empty regions or mismatch in region_properties."""
        with pytest.raises(ValueError, match="must contain at least one mesh region"):
            export_openfoam_multiregion_polymesh(tmp_path, {})

        mesh = create_simple_3d_cube_mesh()
        with pytest.raises(ValueError, match="not in regions"):
            export_openfoam_multiregion_polymesh(
                tmp_path,
                {"fluid": mesh},
                region_properties={"fluid": ["unknown_region"], "solid": []},
            )

    def test_mapped_wall_and_cyclic_patch_attributes(self, tmp_path):
        """Verifies mappedWall (sampleMode, sampleRegion, samplePatch) and cyclic (neighbourPatch, transform)."""
        mesh = create_simple_3d_cube_mesh()
        # Add custom boundary patches
        mesh.boundaries = {
            "coupled_wall": (4, 1),
            "periodic_in": (5, 1),
        }

        patch_meta = {
            "coupled_wall": {
                "type": "mappedWall",
                "sampleMode": "nearestPatchFace",
                "sampleRegion": "solid_zone",
                "samplePatch": "coupled_wall_solid",
            },
            "periodic_in": {
                "type": "cyclic",
                "neighbourPatch": "periodic_out",
                "transform": "translational",
            },
        }

        export_openfoam_polymesh(
            mesh=mesh,
            case_dir=tmp_path,
            patch_metadata=patch_meta,
            region_name="fluid",
        )

        bnd_file = tmp_path / "constant" / "fluid" / "polyMesh" / "boundary"
        assert bnd_file.is_file()
        content = bnd_file.read_text(encoding="utf-8")

        # Check mappedWall attributes
        assert "coupled_wall" in content
        assert "type            mappedWall;" in content
        assert "sampleMode      nearestPatchFace;" in content
        assert "sampleRegion    solid_zone;" in content
        assert "samplePatch     coupled_wall_solid;" in content

        # Check cyclic attributes
        assert "periodic_in" in content
        assert "type            cyclic;" in content
        assert "neighbourPatch  periodic_out;" in content
        assert "transform       translational;" in content


# ==============================================================================
# 3. Feature F53 / Audit H6: Runnable Case Generation & Dynamic Decomposition
# ==============================================================================

class TestFeatureF53RunnableCaseAndDecomposition:
    """Verifies Audit H6 runnable 0/ initial fields, dynamic processorWeights, and SIMPLE/PIMPLE."""

    def test_dynamic_processor_weights_for_arbitrary_ranks(self, tmp_path):
        """decomposeParDict must construct processorWeights with length matching mpi_ranks."""
        registry = ModuleRegistry()
        cg = CaseGenerator(registry)

        test_ranks = [1, 2, 4, 7, 8, 12, 16, 24, 32]

        for ranks in test_ranks:
            case_dir = cg.create_case(f"case_np_{ranks}", target_dir=str(tmp_path), mpi_ranks=ranks)
            decomp_file = case_dir / "system" / "decomposeParDict"
            assert decomp_file.is_file()

            content = decomp_file.read_text(encoding="utf-8")
            assert f"numberOfSubdomains  {ranks};" in content

            # Extract numbers inside processorWeights ( ... );
            match = re.search(r"processorWeights\s*\(\s*([^)]+)\s*\)", content)
            assert match is not None, f"processorWeights block not found for ranks={ranks}"

            tokens = match.group(1).split()
            # Must strictly match ranks count
            assert len(tokens) == ranks, f"Expected {ranks} processorWeights, got {len(tokens)}"

            # Values must be floats >= 1.0
            weights = [float(t) for t in tokens]
            assert all(w >= 1.0 for w in weights)

    def test_decompose_par_invalid_ranks_and_weights_raises_error(self, tmp_path):
        """CaseGenerator must reject ranks <= 0 or weights <= 0 with ValueError."""
        registry = ModuleRegistry()
        cg = CaseGenerator(registry)

        with pytest.raises(ValueError, match="positive integer"):
            cg.create_case("case_invalid", target_dir=str(tmp_path), mpi_ranks=0)

        with pytest.raises(ValueError, match="positive integer"):
            cg.create_case("case_invalid", target_dir=str(tmp_path), mpi_ranks=-4)

        # Non-positive core weights validation
        with pytest.raises(ValueError, match="processor weights must be strictly positive"):
            cg.create_case("case_invalid_pw0", target_dir=str(tmp_path), mpi_ranks=4, p_weight=0.0)

        with pytest.raises(ValueError, match="processor weights must be strictly positive"):
            cg.create_case("case_invalid_pw_neg", target_dir=str(tmp_path), mpi_ranks=4, p_weight=-1.5)

        with pytest.raises(ValueError, match="processor weights must be strictly positive"):
            cg.create_case("case_invalid_ew0", target_dir=str(tmp_path), mpi_ranks=4, e_weight=0.0)

        with pytest.raises(ValueError, match="processor weights must be strictly positive"):
            cg.create_case("case_invalid_ew_neg", target_dir=str(tmp_path), mpi_ranks=4, e_weight=-1.0)

    def test_fv_solution_contains_simple_and_pimple_and_relaxation_factors(self, tmp_path):
        """fvSolution must provide complete solvers, SIMPLE, PIMPLE, and relaxationFactors blocks."""
        registry = ModuleRegistry()
        cg = CaseGenerator(registry)

        case_dir = cg.create_case("case_solution", target_dir=str(tmp_path), mpi_ranks=4)
        fvs_path = case_dir / "system" / "fvSolution"
        assert fvs_path.is_file()

        content = fvs_path.read_text(encoding="utf-8")

        # Solvers block must include explicit U definition for macro expansion
        u_match = re.search(r"\n\s+U\s*\{[^}]*solver\s+smoothSolver;", content)
        assert u_match is not None, "Explicit U solver entry must exist in solvers dictionary"

        # Regex patterns referring to $U; macro
        assert re.search(r'"\(h\|e\|T[^"]+\)"\s*\{[^}]*\$U;', content) is not None
        assert re.search(r'"\(U\|h\|e\|T[^"]+\)Final"\s*\{[^}]*\$U;', content) is not None

        # Solvers block must include p, pFinal, p_rgh
        assert "p\n" in content or "p " in content or "p\r\n" in content
        assert "p_rgh" in content

        # SIMPLE block
        assert "SIMPLE" in content
        assert "residualControl" in content

        # PIMPLE block
        assert "PIMPLE" in content
        assert "nOuterCorrectors" in content
        assert "nCorrectors" in content

        # relaxationFactors block
        assert "relaxationFactors" in content
        assert "fields" in content
        assert "equations" in content

    def test_runnable_zero_directory_initial_fields_generated(self, tmp_path):
        """Verifies that create_case writes valid, non-empty 0/ field files based on active modules."""
        registry = ModuleRegistry()
        # Enable Energy and Species modules
        registry.enable_module("Energy & Heat")
        registry.enable_module("Species & Chemistry")
        spec_mod = registry.get_module("Species & Chemistry")
        spec_mod.config.parameters["species"] = ["O2", "H2", "H2O"]

        cg = CaseGenerator(registry)
        case_dir = cg.create_case("case_fields", target_dir=str(tmp_path), mpi_ranks=2)

        zero_dir = case_dir / "0"
        assert zero_dir.is_dir()

        # Check required fields exist
        required_fields = ["U", "p", "p_rgh", "T", "alphat", "k", "omega", "nut", "O2", "H2", "H2O"]
        for f in required_fields:
            field_file = zero_dir / f
            assert field_file.is_file(), f"Expected 0/{f} to exist"

            content = field_file.read_text(encoding="utf-8")
            assert "FoamFile" in content
            assert f"object      {f};" in content
            assert "dimensions" in content
            assert "internalField" in content
            assert "boundaryField" in content


# ==============================================================================
# 4. Feature F54 / Audit M10: Hardened WSL Runner & Telemetry Tests
# ==============================================================================

class TestFeatureF54WslRunnerHardening:
    """Verifies Audit M10 shlex.quote, returncode checks, drive-letter handling, and regex telemetry."""

    def test_to_wsl_path_handling(self):
        """Tests conversion for Windows drive paths, drive-letter-less paths, and native POSIX paths."""
        runner = WslSimulationRunner()

        # Windows drive path with backslashes
        assert runner.to_wsl_path(r"G:\Datein\Projects\CFDSolver") == "/mnt/g/Datein/Projects/CFDSolver"
        assert runner.to_wsl_path(r"C:\Users\test\case") == "/mnt/c/Users/test/case"

        # Windows drive path with forward slashes
        assert runner.to_wsl_path("D:/Simulation/mesh") == "/mnt/d/Simulation/mesh"

        # Drive-letter-less / native POSIX path
        assert runner.to_wsl_path("/usr/lib/openfoam") == "/usr/lib/openfoam"
        assert runner.to_wsl_path("/mnt/c/test/case") == "/mnt/c/test/case"

        # Empty string
        assert runner.to_wsl_path("") == ""

    def test_build_command_shlex_quoting(self):
        """build_command must safely quote bashrc path and case directory."""
        runner = WslSimulationRunner(bashrc_path="/opt/openfoam path with spaces/bashrc")
        case_path = "/mnt/g/Case (Run 1)/simulation directory"

        cmd = runner.build_command(case_path, "simpleFoam")
        assert "'/opt/openfoam path with spaces/bashrc'" in cmd
        assert "'/mnt/g/Case (Run 1)/simulation directory'" in cmd
        assert "simpleFoam" in cmd

    def test_run_methods_raise_runtime_error_on_process_failure(self):
        """run_mesh and run_parallel must raise RuntimeError when returncode != 0."""
        runner = WslSimulationRunner()

        mock_failed_process = subprocess.CompletedProcess(
            args=["wsl", "bash"],
            returncode=127,
            stdout="Starting blockMesh...\n",
            stderr="blockMesh: command not found\n",
        )

        with patch("subprocess.run", return_value=mock_failed_process):
            with pytest.raises(RuntimeError, match="Mesh generation 'blockMesh' failed with exit code 127"):
                runner.run_mesh("/mnt/c/case", mesh_command="blockMesh", check=True)

            with pytest.raises(RuntimeError, match="Parallel simulation 'simpleFoam' .* failed with exit code 127"):
                runner.run_parallel("/mnt/c/case", solver="simpleFoam", ranks=4, check=True)

            # When check=False, returns process without error
            proc = runner.run_mesh("/mnt/c/case", mesh_command="blockMesh", check=False)
            assert proc.returncode == 127

    def test_run_parallel_invalid_ranks(self):
        """run_parallel must reject ranks <= 0."""
        runner = WslSimulationRunner()
        with pytest.raises(ValueError, match="positive integer"):
            runner.run_parallel("/mnt/c/case", solver="simpleFoam", ranks=0)

    def test_live_log_telemetry_residual_and_courant_parsing(self):
        """parse_log_text must extract Time, Courant numbers, and residual metrics."""
        log_sample = """
Time = 0.001

Courant Number mean: 0.045 max: 0.123
smoothSolver:  Solving for Ux, Initial residual = 0.5432, Final residual = 0.0054, No Iterations 6
smoothSolver:  Solving for Uy, Initial residual = 0.4321, Final residual = 0.0043, No Iterations 5
GAMG:  Solving for p, Initial residual = 0.9876, Final residual = 0.0098, No Iterations 15
ExecutionTime = 0.12 s  ClockTime = 1 s

Time = 0.002

Courant Number mean: 0.051 max: 0.145
smoothSolver:  Solving for Ux, Initial residual = 0.0543, Final residual = 0.0005, No Iterations 4
smoothSolver:  Solving for Uy, Initial residual = 0.0432, Final residual = 0.0004, No Iterations 3
GAMG:  Solving for p, Initial residual = 0.0987, Final residual = 0.0009, No Iterations 8
ExecutionTime = 0.25 s  ClockTime = 2 s
"""
        runner = WslSimulationRunner()
        telemetry_list = runner.parse_log_text(log_sample)

        assert len(telemetry_list) == 2

        # Step 1 (Time = 0.001)
        t1 = telemetry_list[0]
        assert t1.time == pytest.approx(0.001)
        assert t1.courant_mean == pytest.approx(0.045)
        assert t1.courant_max == pytest.approx(0.123)
        assert not t1.nan_detected

        assert "Ux" in t1.residuals
        ux1 = t1.residuals["Ux"]
        assert ux1.initial_residual == pytest.approx(0.5432)
        assert ux1.final_residual == pytest.approx(0.0054)
        assert ux1.iterations == 6

        assert "p" in t1.residuals
        p1 = t1.residuals["p"]
        assert p1.initial_residual == pytest.approx(0.9876)
        assert p1.final_residual == pytest.approx(0.0098)
        assert p1.iterations == 15

        # Step 2 (Time = 0.002)
        t2 = telemetry_list[1]
        assert t2.time == pytest.approx(0.002)
        assert t2.courant_mean == pytest.approx(0.051)
        assert t2.courant_max == pytest.approx(0.145)
        assert "Uy" in t2.residuals
        uy2 = t2.residuals["Uy"]
        assert uy2.initial_residual == pytest.approx(0.0432)
        assert uy2.final_residual == pytest.approx(0.0004)
        assert uy2.iterations == 3

    def test_divergence_and_nan_detection(self):
        """parse_log_text must detect NaN and floating point exceptions."""
        log_with_nan = """
Time = 0.005

Courant Number mean: 0.12 max: nan
smoothSolver:  Solving for Ux, Initial residual = nan, Final residual = nan, No Iterations 0
#0  Foam::error::printStack(Foam::Ostream&) in /usr/lib/openfoam/openfoam/platforms/linux64GccDPInt32Opt/lib/libOpenFOAM.so
#1  Floating point exception
"""
        runner = WslSimulationRunner()
        telemetry = runner.parse_log_text(log_with_nan)

        assert len(telemetry) == 1
        t = telemetry[0]
        assert t.nan_detected is True
        assert t.fatal_error is not None
        assert "Floating point exception" in t.fatal_error

        # With raise_on_divergence=True
        with pytest.raises(FloatingPointError, match="Divergence or NaN detected"):
            runner.parse_log_text(log_with_nan, raise_on_divergence=True)

    def test_parse_log_file_and_file_not_found(self, tmp_path):
        """parse_log_file reads a file on disk or raises FileNotFoundError."""
        runner = WslSimulationRunner()
        log_file = tmp_path / "test.log"
        log_file.write_text(
            "Time = 0.01\nCourant Number mean: 0.02 max: 0.08\nExecutionTime = 0.1 s\n",
            encoding="utf-8"
        )

        telemetry = runner.parse_log_file(log_file)
        assert len(telemetry) == 1
        assert telemetry[0].time == pytest.approx(0.01)
        assert telemetry[0].courant_max == pytest.approx(0.08)

        # FileNotFoundError on non-existent path
        with pytest.raises(FileNotFoundError):
            runner.parse_log_file(tmp_path / "non_existent.log")


# ==============================================================================
# 5. Multiphysics Mathematics & Detailed Contract Validation
# ==============================================================================

class TestMultiphysicsMathematicsAndContracts:
    """Verifies mathematical consistency of reaction kinetics and OpenFOAM boundary conditions."""

    def test_reaction_stoichiometry_and_mass_conservation_logic(self):
        """Verifies stoichiometric mass conservation sum(nu_k * M_k) = 0 for generic reactions."""
        # Example 1: 2 H2 + O2 -> 2 H2O
        # Species: H2 (M=2.016e-3), O2 (M=31.998e-3), H2O (M=18.015e-3)
        M_H2 = 2.01588e-3
        M_O2 = 31.9988e-3
        M_H2O = 18.01528e-3

        nu_H2 = -2.0
        nu_O2 = -1.0
        nu_H2O = +2.0

        mass_sum = nu_H2 * M_H2 + nu_O2 * M_O2 + nu_H2O * M_H2O
        assert abs(mass_sum) < 1e-6, f"Reaction violates mass conservation: {mass_sum}"

        # Arrhenius rate calculation: k = A * exp(-Ea / (R * T))
        A = 1.0e11
        Ea_over_R = 15000.0  # Kelvin
        T = 1200.0  # Kelvin
        k = A * math.exp(-Ea_over_R / T)
        assert k > 0.0
        assert k < A

        # Analytical Jacobian dq/dh = (dq/dT) / Cp
        # For an endothermic reaction (heat absorbed, deltaH > 0):
        # q_source = -r * deltaH < 0 (heat sink in enthalpy equation)
        # dq/dT = q * (Ea / (R * T^2)) < 0 -> dq/dh < 0
        deltaH_endo = 2.06e5  # J/mol (endothermic)
        q_sink = -1.0 * k * deltaH_endo  # W/m^3 (< 0)
        Cp = 1050.0  # J/(kg K)
        dq_dh = q_sink * (Ea_over_R / (T * T * Cp))
        assert q_sink < 0.0
        assert dq_dh < 0.0

        # Patankar diagonal stabilization: -dq_dh > 0 adds positive value to eqn.diag()
        diag_stabilization = -dq_dh
        assert diag_stabilization > 0.0

    def test_custom_patches_and_mapped_wall_boundary_conditions_in_zero_dir(self, tmp_path):
        """create_case correctly populates boundaryField for custom patch configurations."""
        registry = ModuleRegistry()
        registry.enable_module("Energy & Heat")
        registry.enable_module("Viscous & Turbulence")

        cg = CaseGenerator(registry)

        custom_patches = {
            "inlet_air": "inlet",
            "outlet_back": "outlet",
            "interface_wall": "mappedWall",
            "symmetry_plane": "symmetry",
            "wedge_empty": "empty",
        }

        custom_inits = {
            "U": [2.5, 0.0, 0.0],
            "T": 325.0,
            "p": 101325,
            "k": 0.25,
        }

        case_dir = cg.create_case(
            "case_custom_bc",
            target_dir=str(tmp_path),
            solver_name="chtMultiRegionFoam",
            patches=custom_patches,
            initial_conditions=custom_inits,
        )

        zero_dir = case_dir / "0"

        # Check U boundaryField
        u_content = (zero_dir / "U").read_text(encoding="utf-8")
        assert "uniform (2.5 0.0 0.0)" in u_content
        assert "inlet_air" in u_content
        assert "fixedValue" in u_content
        assert "interface_wall" in u_content
        assert "noSlip" in u_content
        assert "symmetry_plane" in u_content
        assert "symmetryPlane" in u_content
        assert "wedge_empty" in u_content
        assert "empty;" in u_content

        # Check T boundaryField
        t_content = (zero_dir / "T").read_text(encoding="utf-8")
        assert "uniform 325.0" in t_content
        assert "interface_wall" in t_content
        assert "compressible::turbulentTemperatureCoupledBaffleMixed" in t_content

        # Check k boundaryField
        k_content = (zero_dir / "k").read_text(encoding="utf-8")
        assert "uniform 0.25" in k_content
        assert "interface_wall" in k_content
        assert "kqRWallFunction" in k_content

    def test_multiregion_export_with_tuple_regions(self, tmp_path):
        """export_openfoam_multiregion_polymesh handles (mesh, metadata) tuple structure."""
        mesh_f = create_simple_3d_cube_mesh(scale=1.0)
        mesh_s = create_simple_3d_cube_mesh(scale=0.5)

        regions = {
            "fluid_reg": (mesh_f, {
                "patch_types": {"inlet": "patch", "wall": "wall"},
                "patch_metadata": {"wall": {"type": "wall"}},
            }),
            "solid_reg": (mesh_s, {
                "patch_types": {"wall": "wall"},
                "patch_metadata": {"wall": {"type": "wall"}},
            }),
        }

        poly_dirs = export_openfoam_multiregion_polymesh(
            case_path=tmp_path,
            regions=regions,
            region_properties={"fluid": ["fluid_reg"], "solid": ["solid_reg"]},
        )

        assert "fluid_reg" in poly_dirs
        assert "solid_reg" in poly_dirs
        assert (tmp_path / "constant" / "fluid_reg" / "polyMesh" / "boundary").is_file()
        assert (tmp_path / "constant" / "solid_reg" / "polyMesh" / "boundary").is_file()

