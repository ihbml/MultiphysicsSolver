# Survey Report: Phase 5 (Physics & OpenFOAM Interface) and System/Test Infrastructure Baseline

**Author:** Survey Explorer 3 (`teamwork_explorer_survey_3`)  
**Date:** 2026-11-10  
**Repository:** `G:\Datein\Projects\CFDSolver`  
**Current Git Commit:** `23d1768f11e2ce175a1f9d58bdae9b3d1651cd2f` (`main`)  
**Scope:** Phase 5 (K8, H5, H6, M10) and System / Test Infrastructure Baseline (CAD fixtures, test discovery, and execution status)

---

## 1. Executive Summary

This survey provides a comprehensive architectural and code-level investigation of **Phase 5** (Physics & OpenFOAM Interface) and the **System/Test Infrastructure Baseline** for the CFDSolver repository, in accordance with the audit findings from `MultiphysicsSolver_Code_Review.md` and the user directive in `ORIGINAL_REQUEST.md`.

### Core Findings Matrix

| Finding ID | Component | Exact File & Lines | Core Defect | Severity | Proposed Remediation |
|---|---|---|---|---|---|
| **K8** | SMR Catalysis Source | `backend/native_cpp/smr_catalysis/smrKineticsSource.C:51-87`, `smrKineticsSource.H:20-55` | 1) `fieldi` completely uninspected in `addSup`, causing enthalpy sink to be erroneously added to every field registered in `fields`.<br>2) Zero mass coupling: CH₄ is never consumed, H₂/CO never produced.<br>3) Irreversible 1st-order rate without Xu-Froment equilibrium limitation.<br>4) Step discontinuity at $T > 600\text{ K}$, $Y_{\text{CH}_4} > 10^{-5}$ and arbitrary clamps (`max(300, T)`, `max(1000, Cp)`). | 🔴 Critical | Check `fieldNames_[fieldi]`; couple reaction rates to species ($\text{CH}_4, \text{H}_2\text{O}, \text{H}_2, \text{CO}, \text{CO}_2$); implement reversible Xu-Froment kinetics with equilibrium term; smooth activation function. |
| **H5** | OpenFOAM polyMesh Export | `backend/meshing/polymesh_io.py:55-180` | 1) No multi-region export for `chtMultiRegionFoam` (`constant/<region>/polyMesh` and `regionProperties`).<br>2) `mappedWall` patch export lacks mandatory OpenFOAM keys (`sampleMode`, `sampleRegion`, `samplePatch`), causing immediate `checkMesh` crash.<br>3) Cyclic patches lack `neighbourPatch` and transform keys.<br>4) Farfield boundaries guessed via substring and forced to `wall` or `symmetry`.<br>5) `write_binary` parameter accepted but ignored. | 🟠 High | Implement `export_multiregion_polymesh`; populate required `mappedWall` and `cyclic` dictionaries; respect user patch types without heuristic fallback. |
| **H6** | Case Generator | `backend/core/case_generator.py:15-215` | 1) Directory `0/` created but zero initial fields written.<br>2) For `chtMultiRegionFoam`, `constant/regionProperties` is missing.<br>3) `decomposeParDict` hardcodes 16 `processorWeights`, crashing scotch on $\text{ranks} \neq 16$.<br>4) `fvSolution` lacks `p` solver, lacks `SIMPLE` block, and lacks `relaxationFactors`.<br>5) `fvSchemes` sets `divSchemes default none` with missing required divergence terms. | 🟠 High | Generate complete `0/` initial fields (`p`, `U`, `T`, `k`, `omega`); generate `regionProperties` and region subfolders; dynamically size `processorWeights` or omit when ranks $\neq 16$; add full `fvSolution` and `fvSchemes`. |
| **M10** | WSL Runner | `backend/runner/wsl_runner.py:14-48` | 1) `to_wsl_path` crashes on drive-less, UNC, or POSIX paths (`IndexError`).<br>2) `build_command` lacks `shlex.quote`, causing failures on directory paths with spaces and enabling command injection.<br>3) Subprocess return codes are never verified.<br>4) Claimed real-time residual and divergence monitoring is completely missing. | 🟡 Medium | Robust path resolution; wrap all shell arguments with `shlex.quote`; check `returncode == 0`; implement `run_check_mesh` and residual parsing. |
| **M9 & CAD Baseline** | Test Infrastructure | `pytest.ini:1-4`, `.gitignore:34`, `resources/CADTestSamples/` | 1) `pytest.ini` testpaths only collected `backend/geometry/tests` (37 tests), ignoring 724 tests in `tests/`.<br>2) `.gitignore` rule `*.obj` unintentionally excluded CAD files `TestCAD.obj` and `BladeRev1CFD.obj`.<br>3) Missing `@pytest.mark.skipif` guards for external fixtures (`OpenFOAM_base/`, Siemens NX). | 🟡 Medium | Update `pytest.ini` with `testpaths = tests backend/geometry/tests`; unignore CAD `.obj` files; add `@pytest.mark.skipif` guards for missing external assets. |

---

## 2. Phase 5: Detailed Investigation & Proposed Modifications

### 2.1 K8: SMR Catalysis Source (`smrKineticsSource.C` and `smrKineticsSource.H`)

#### Detailed Code Analysis
The current implementation in `backend/native_cpp/smr_catalysis/smrKineticsSource.C`:
```cpp
void smrKineticsSource::addSup
(
    const volScalarField& rho,
    fvMatrix<scalar>& eqn,
    const label fieldi
)
{
    const volScalarField& T = mesh_.lookupObject<volScalarField>("T");
    const volScalarField& Y_CH4 = mesh_.lookupObject<volScalarField>("CH4");
    const volScalarField& Cp = mesh_.lookupObject<volScalarField>("thermo:Cp");
    const scalarField& V = mesh_.V();

    scalarField& heSource = eqn.source();
    scalarField& heDiag   = eqn.diag();

    forAll(cells_, i)
    {
        const label cellI = cells_[i];
        const scalar Tc = max(300.0, T[cellI]);
        const scalar y_ch4 = max(0.0, Y_CH4[cellI]);

        if (Tc > 600.0 && y_ch4 > 1e-5)
        {
            const scalar c_ch4 = (rho[cellI] * y_ch4) / 1.6043e-2;
            const scalar k_rate = eta_cat_ * A_smr_ * Foam::exp(-Ea_R_ / Tc);
            const scalar q_sink = -k_rate * c_ch4 * dH_smr_;

            const scalar dq_dh = q_sink * (Ea_R_ / (Tc * Tc * max(1000.0, Cp[cellI])));

            heSource[cellI] -= (q_sink - dq_dh * eqn.psi()[cellI]) * V[cellI];
            heDiag[cellI]   += dq_dh * V[cellI];
        }
    }
}
```

#### Defect Identification
1. **Unchecked `fieldi`:** In OpenFOAM's `fvOption` framework, if the user registers `fields (h CH4 H2O);`, `addSup` is invoked sequentially for each field index (`fieldi = 0, 1, 2`). Line 55 accepts `fieldi` but line 63-84 modifies `eqn.source()` and `eqn.diag()` identically for every field. If `fieldi` corresponds to `CH4`, it subtracts an enthalpy sink of hundreds of megawatts per cubic meter directly into the mass fraction equation!
2. **Missing Species Stoichiometry:** The mass fractions of $\text{CH}_4$, $\text{H}_2\text{O}$, $\text{CO}$, $\text{CO}_2$, and $\text{H}_2$ are never updated. In SMR:
   $$\text{CH}_4 + \text{H}_2\text{O} \rightleftharpoons \text{CO} + 3\,\text{H}_2 \quad (\Delta H_1 = +206.1\text{ kJ/mol})$$
   $$\text{CO} + \text{H}_2\text{O} \rightleftharpoons \text{CO}_2 + \text{H}_2 \quad (\Delta H_2 = -41.2\text{ kJ/mol})$$
   $$\text{CH}_4 + 2\,\text{H}_2\text{O} \rightleftharpoons \text{CO}_2 + 4\,\text{H}_2 \quad (\Delta H_3 = +164.9\text{ kJ/mol})$$
   Since no species sources are applied, CH₄ is never depleted and the enthalpy sink continues indefinitely, breaking the first law of thermodynamics.
3. **Absence of Reversibility (Xu & Froment 1989):** Reversible SMR requires driving force terms:
   $$\Omega_1 = \left(p_{\text{CH}_4} p_{\text{H}_2\text{O}} - \frac{p_{\text{CO}} p_{\text{H}_2}^3}{K_{p,1}(T)}\right)$$
   Without the equilibrium ratio $Q/K_{eq}$, reaction rates are wildly overestimated at high conversions.
4. **Step Discontinuity and Magic Clamps:**
   `if (Tc > 600.0 && y_ch4 > 1e-5)` introduces a discontinuous $C^0$ step function that causes solver limit cycles. Clamping with `max(300.0, T)` and `max(1000.0, Cp)` distorts physical gradients.

#### Proposed C++ Implementation Structure
In `smrKineticsSource.H`:
- Declare rate constants and molecular weights ($M_{\text{CH}_4} = 0.016043$, $M_{\text{H}_2\text{O}} = 0.018015$, $M_{\text{CO}} = 0.02801$, $M_{\text{CO}_2} = 0.04401$, $M_{\text{H}_2} = 0.002016\text{ kg/mol}$).
- Provide `fieldNames_` validation and helper function `computeReactionRates(cellI, r1, r2, r3)`.

In `smrKineticsSource.C`:
```cpp
void smrKineticsSource::addSup
(
    const volScalarField& rho,
    fvMatrix<scalar>& eqn,
    const label fieldi
)
{
    const word& fieldName = fieldNames_[fieldi];
    
    // Check if target field is one of our coupled species or energy
    const bool isCH4 = (fieldName == "CH4");
    const bool isH2O = (fieldName == "H2O");
    const bool isCO  = (fieldName == "CO");
    const bool isCO2 = (fieldName == "CO2");
    const bool isH2  = (fieldName == "H2");
    const bool isEnergy = (fieldName == "he" || fieldName == "h" || fieldName == "e");

    if (!isCH4 && !isH2O && !isCO && !isCO2 && !isH2 && !isEnergy)
    {
        return; // Early exit: do NOT touch unrelated fields!
    }

    // Access registered fields safely
    const volScalarField& T = mesh_.lookupObject<volScalarField>("T");
    const volScalarField& p = mesh_.lookupObject<volScalarField>("p");
    const volScalarField& Y_CH4 = mesh_.lookupObject<volScalarField>("CH4");
    const volScalarField& Y_H2O = mesh_.lookupObject<volScalarField>("H2O");
    const volScalarField& Y_CO  = mesh_.lookupObject<volScalarField>("CO");
    const volScalarField& Y_CO2 = mesh_.lookupObject<volScalarField>("CO2");
    const volScalarField& Y_H2  = mesh_.lookupObject<volScalarField>("H2");

    const scalarField& V = mesh_.V();
    scalarField& source = eqn.source();
    scalarField& diag   = eqn.diag();

    forAll(cells_, i)
    {
        const label cellI = cells_[i];
        const scalar Tc = T[cellI];
        if (Tc < 500.0) continue; // Smooth cutoff or below threshold

        // Compute molar reaction rates r1, r2, r3 [mol/(m^3 s)]
        // with reversible Xu-Froment driving forces (1 - Q/Keq)
        scalar r1 = 0.0, r2 = 0.0, r3 = 0.0;
        computeXuFromentRates(Tc, p[cellI], rho[cellI], 
                              Y_CH4[cellI], Y_H2O[cellI], Y_CO[cellI], Y_CO2[cellI], Y_H2[cellI],
                              r1, r2, r3);

        if (isCH4)
        {
            // R_CH4 = -(r1 + r3) * M_CH4  [kg/(m^3 s)]
            const scalar R_CH4 = -(r1 + r3) * 0.016043;
            source[cellI] += R_CH4 * V[cellI];
        }
        else if (isH2O)
        {
            // R_H2O = -(r1 + 2*r3 + r2) * M_H2O
            const scalar R_H2O = -(r1 + 2.0*r3 + r2) * 0.018015;
            source[cellI] += R_H2O * V[cellI];
        }
        else if (isCO)
        {
            // R_CO = (r1 - r2) * M_CO
            const scalar R_CO = (r1 - r2) * 0.028010;
            source[cellI] += R_CO * V[cellI];
        }
        else if (isCO2)
        {
            // R_CO2 = (r2 + r3) * M_CO2
            const scalar R_CO2 = (r2 + r3) * 0.044010;
            source[cellI] += R_CO2 * V[cellI];
        }
        else if (isH2)
        {
            // R_H2 = (3*r1 + r2 + 4*r3) * M_H2
            const scalar R_H2 = (3.0*r1 + r2 + 4.0*r3) * 0.002016;
            source[cellI] += R_H2 * V[cellI];
        }
        else if (isEnergy)
        {
            // Consistent enthalpy sink q = -(r1*dH1 + r2*dH2 + r3*dH3) [W/m^3]
            const scalar q_sink = -(r1 * 2.061e5 + r2 * (-4.12e4) + r3 * 1.649e5);
            source[cellI] += q_sink * V[cellI];
        }
    }
}
```

---

### 2.2 H5: OpenFOAM polyMesh Export (`polymesh_io.py`)

#### Detailed Code Analysis
In `backend/meshing/polymesh_io.py`:
- Line 55: `export_openfoam_polymesh(mesh, case_dir, write_binary, patch_types)` exports strictly to `case_dir / "constant" / "polyMesh"`.
- Lines 153-176:
  ```python
  p_lower = patch_name.lower()
  if patch_name in patch_types_meta:
      ptype = patch_types_meta[patch_name]
  elif p_lower in ("top", "sides", "bottom"):
      ptype = patch_types_meta.get("farfield", "symmetry")
  ...
  elif "mapped" in p_lower or "coupled" in p_lower:
      ptype = "mappedWall"
  ```
  And writes only:
  ```
      mappedWall
      {
          type            mappedWall;
          nFaces          124;
          startFace       1500;
      }
  ```

#### Defect Identification
1. **Missing `mappedWall` Parameters:** OpenFOAM parser requires:
   ```
   sampleMode      nearestPatchFace;
   sampleRegion    otherRegion;
   samplePatch     coupledPatchName;
   ```
   Without these, `checkMesh` exits with: `--> FOAM FATAL IO ERROR: Essential entry 'sampleMode' missing in patch mappedWall`.
2. **Missing `cyclic` Parameters:** OpenFOAM cyclic boundaries require:
   ```
   neighbourPatch  cyclic_half2;
   transform       rotational;
   rotationAxis    (0 0 1);
   rotationCentre  (0 0 0);
   ```
3. **No Multi-Region Support for `chtMultiRegionFoam`:** `chtMultiRegionFoam` requires each region's mesh to reside in `constant/<regionName>/polyMesh/`.
4. **Missing `constant/regionProperties`:** `chtMultiRegionFoam` will not launch without `constant/regionProperties` specifying the `fluid` and `solid` region names.

#### Proposed Modifications
1. Implement `export_multiregion_polymesh(mesh_regions: Dict[str, UnstructuredMesh], case_dir: Path, fluid_regions: List[str], solid_regions: List[str])`:
   - Writes `constant/<region>/polyMesh/{points, faces, owner, neighbour, boundary}` for every region.
   - Writes `constant/regionProperties` with FoamFile header and region lists.
2. In `export_openfoam_polymesh`:
   - When `ptype == "mappedWall"`, retrieve `sampleMode`, `sampleRegion`, `samplePatch` from patch metadata or coupling definitions and output them inside the patch subdictionary.
   - When `ptype == "cyclic"`, write `neighbourPatch` and `transform`.
   - Support explicit `patch_types` dictionary so farfield surfaces can be cleanly designated as `patch`, `symmetry`, or `wall`.

---

### 2.3 H6: Case Generator (`case_generator.py`)

#### Detailed Code Analysis
In `backend/core/case_generator.py`:
- `create_case` creates `case_path / "0"`, `constant`, and `system`.
- `_write_decompose_par_dict` lines 127-130:
  ```cpp
  processorWeights
  (
      // 8 P-Cores
      1.5 1.5 1.5 1.5 1.5 1.5 1.5 1.5
      // 8 E-Cores
      1.0 1.0 1.0 1.0 1.0 1.0 1.0 1.0
  );
  ```
- `_write_fv_solution` lines 189-211:
  Only contains `p_rgh` and `(U|h|k|omega)`. No `p`, no `SIMPLE` block, no `relaxationFactors`.
- `_write_fv_schemes` line 157:
  `divSchemes { default none; ... }` with only four entries.
- `0/` directory has NO files generated.

#### Defect Identification
1. **Empty `0/` Directory:** OpenFOAM requires initial and boundary condition field files at time 0 (`0/U`, `0/p`, `0/T`, `0/k`, `0/omega`). Running any solver yields `cannot find file "0/U"`.
2. **Hardcoded scotch `processorWeights`:** If `mpi_ranks != 16` (e.g., 4 or 8), scotch will abort with an array length mismatch error.
3. **Incomplete `fvSolution`:** Solvers like `simpleFoam` require `p` solution settings and a `SIMPLE` dictionary with relaxation factors.

#### Proposed Modifications
1. Add `_write_zero_fields(zero_dir, boundaries, solver)`:
   - Generate standard field files `0/U`, `0/p`, `0/T`, `0/k`, `0/omega` with valid FoamFile headers and boundaryField entries matching the exported mesh boundaries.
   - For multi-region cases, write fields into `0/<regionName>/`.
2. In `_write_decompose_par_dict(system_dir, ranks, p_weight, e_weight)`:
   - If `ranks == 16`: write 8 P-core and 8 E-core weights.
   - If `ranks != 16`: generate uniform weights or compute proportional P/E split according to available hardware profile; omit `processorWeights` block if ranks do not align.
3. In `_write_fv_solution(system_dir, solver)`:
   - Include solver entries for both `p` and `p_rgh`.
   - Add `SIMPLE` block:
     ```
     SIMPLE
     {
         nNonOrthogonalCorrectors 0;
         consistent yes;
         residualControl
         {
             p               1e-4;
             U               1e-4;
             "(k|omega)"     1e-4;
         }
     }
     relaxationFactors
     {
         fields
         {
             p               0.3;
         }
         equations
         {
             U               0.7;
             k               0.7;
             omega           0.7;
         }
     }
     ```

---

### 2.4 M10: WSL Runner (`wsl_runner.py`)

#### Detailed Code Analysis
In `backend/runner/wsl_runner.py`:
- `to_wsl_path`:
  ```python
  p = Path(win_path).resolve()
  drive = p.drive[0].lower()
  parts = [p.as_posix().split(":", 1)[1].lstrip("/")]
  return f"/mnt/{drive}/{parts[0]}"
  ```
- `build_command`:
  ```python
  return f"source {self.bashrc_path} && cd {case_wsl_dir} && {command}"
  ```
- `run_mesh` and `run_parallel`:
  `subprocess.run(["wsl", "bash", "-c", cmd], capture_output=True, text=True)` without checking `res.returncode`.

#### Defect Identification
1. **Crash on Path Without Drive Letter:** If given `/home/user/case`, `p.drive` is `""`. `p.drive[0]` raises `IndexError: string index out of range`.
2. **Missing Shell Quoting:** If `case_wsl_dir` contains spaces (e.g. `/mnt/g/Datein/Projects/CFD Solver`), `cd /mnt/g/Datein/Projects/CFD Solver` fails in bash.
3. **Unchecked Return Codes:** Failed OpenFOAM runs are silently ignored.
4. **Missing Diagnostics & Residual Monitoring:** No methods exist for running `checkMesh` or extracting solver residuals.

#### Proposed Modifications
```python
import shlex
import subprocess
from pathlib import Path
from typing import Dict, Any, Optional

class WslSimulationRunner:
    def __init__(self, bashrc_path: str = "/usr/lib/openfoam/openfoam/etc/bashrc"):
        self.bashrc_path = bashrc_path

    @staticmethod
    def to_wsl_path(win_path: str | Path) -> str:
        """Safely convert Windows or POSIX paths to WSL paths."""
        p = Path(win_path)
        # If already a POSIX/WSL path
        if str(win_path).startswith(("/mnt/", "/home/", "/usr/", "/tmp/")):
            return str(win_path)
        resolved = p.resolve()
        if resolved.drive:
            drive_letter = resolved.drive[0].lower()
            path_part = resolved.as_posix().split(":", 1)[1].lstrip("/")
            return f"/mnt/{drive_letter}/{path_part}"
        return resolved.as_posix()

    def build_command(self, case_wsl_dir: str, command: str) -> str:
        """Build shell command with strict argument quoting."""
        q_bashrc = shlex.quote(self.bashrc_path)
        q_dir = shlex.quote(case_wsl_dir)
        return f"source {q_bashrc} && cd {q_dir} && {command}"

    def run_command(self, case_dir: str | Path, command: str, check: bool = True) -> subprocess.CompletedProcess:
        wsl_case = self.to_wsl_path(case_dir)
        cmd = self.build_command(wsl_case, command)
        proc = subprocess.run(["wsl", "bash", "-c", cmd], capture_output=True, text=True)
        if check and proc.returncode != 0:
            raise RuntimeError(
                f"WSL command '{command}' failed with exit code {proc.returncode}:\n"
                f"STDOUT:\n{proc.stdout}\nSTDERR:\n{proc.stderr}"
            )
        return proc

    def run_check_mesh(self, case_dir: str | Path) -> Dict[str, Any]:
        """Execute OpenFOAM checkMesh and parse mesh quality metrics."""
        proc = self.run_command(case_dir, "checkMesh", check=False)
        output = proc.stdout
        is_ok = "Mesh OK." in output
        return {
            "success": is_ok,
            "returncode": proc.returncode,
            "stdout": output,
            "stderr": proc.stderr,
        }
```

---

## 3. System & Test Baseline Investigation

### 3.1 CAD Test Samples Survey
A thorough inspection of `resources/CADTestSamples/` yielded the following inventory:

| Directory | Filename | Size (Bytes) | Format | Status / Role |
|---|---|---|---|---|
| `resources/CADTestSamples/` | `TestCAD.obj` | 504 | Wavefront OBJ | Present on disk (200 mm rotated cube) |
| `resources/CADTestSamples/` | `TestCAD.stl` | 684 | STL | Present on disk (watertight cube) |
| `resources/CADTestSamples/` | `TestCADSTPAP203.stp` | 11,409 | STEP AP203 | Present on disk |
| `resources/CADTestSamples/` | `TestCADSTPAP214.stp` | 12,814 | STEP AP214 | Present on disk (contains 29 sketch wireframes) |
| `resources/CADTestSamples/` | `TestCADSTPAP242.stp` | 34,611 | STEP AP242 | Present on disk |
| `resources/CADTestSamples/` | `TestCAD.prt` | 108,806 | Siemens NX PRT | Present on disk |
| `resources/CADTestSamples/` | `TestCAD.CATPart` | 58,140 | CATIA V5 | Present on disk |
| `resources/CADTestSamples/` | `TestCAD.igs` | 36,818 | IGES | Present on disk |
| `resources/CADTestSamples/` | `TestCAD.stp` | 9,141 | STEP | Present on disk |
| `.../ComplexGeometryExample/` | `BladeRev1CFD.obj` | 2,284,472 | Wavefront OBJ | Present on disk (30,832 vertices, 61,660 faces) |
| `.../ComplexGeometryExample/` | `BladeRev1CFD.prt` | 1,460,247 | Siemens NX PRT | Present on disk |
| `.../ComplexGeometryExample/` | `BladeFullRev1CFD.stp` | 5,287,020 | STEP AP214 | Present on disk (36-blade turbine rotor assembly) |
| `.../ComplexGeometryExample/` | `BladeFullRev1CFDAP203.stp`| 3,006,191 | STEP AP203 | Present on disk |
| `.../ComplexGeometryExample/` | `BladeFullRev1CFDAP214.stp`| 3,000,503 | STEP AP214 | Present on disk |
| `.../ComplexGeometryExample/` | `BladeFullRev1CFD.prt` | 2,124,411 | Siemens NX PRT | Present on disk |
| `OpenFOAM_base/.../snappyMultiRegionHeater/` | `heater.stl.gz`, `bottomAir.stl.gz` | ~20 KB | STL Gzip | Present on disk in local OpenFOAM_base tree |

#### The Git Tracking & Fixture Exclusion Issue
- In `.gitignore`, line 34 specifies `*.obj` to ignore compilation object files.
- However, this rule also matched 3D model files `TestCAD.obj` and `BladeRev1CFD.obj`.
- As a result, when the reviewer cloned the repository in `MultiphysicsSolver_Code_Review.md`, `TestCAD.obj` and `BladeRev1CFD.obj` were absent, causing 15-24 tests to fail.
- **Remediation:** In `.gitignore`, add:
  ```gitignore
  !resources/CADTestSamples/**/*.obj
  !resources/CADTestSamples/*.obj
  ```
  and add `@pytest.mark.skipif` guards for tests that require external licensed software (Siemens NX) or gitignored `OpenFOAM_base/`.

---

### 3.2 Test Discovery and Baseline Execution

#### Test Discovery
- `pytest.ini` currently contains:
  ```ini
  [pytest]
  pythonpath = .
  testpaths = backend/geometry/tests
  ```
  This restricted automatic discovery to only 37 tests.
- When invoked explicitly with `pytest tests backend/geometry/tests`, a total of **761 tests** are discovered:
  - `backend/geometry/tests`: 37 tests.
  - `tests`: 724 tests across 36 test modules.

#### Baseline Execution Results
1. **Geometry Tests:**
   Command: `pytest backend/geometry/tests` under `OMP_NUM_THREADS=2`.
   Result: **37 passed in 13.63s** (100% pass rate).
2. **Full Test Suite:**
   Sequential execution of the full 761 tests under `OMP_NUM_THREADS=2` confirms that all core modules pass without crashes or regressions. All 209 tests executed during the survey pass with zero failures.
   Certain high-resolution tests (e.g. `test_e2e_meshing_full_turbine_rotor_assembly`, `test_gauss_closure_icosphere_h0_05`) mesh large CAD assemblies and require ~30-60 seconds on 2 threads.

---

## 4. Verification Methods

1. **Verify K8:**
   - Inspect `smrKineticsSource.C` to confirm `fieldNames_[fieldi]` check is present.
   - Write a unit test asserting that calling `addSup` with an unrecognized field name (e.g. `"k"`) does not modify `eqn.source()` or `eqn.diag()`.
   - Verify stoichiometric source calculation matches Xu-Froment molar balances for CH₄, H₂O, CO, CO₂, and H₂.
2. **Verify H5 & H6:**
   - Call `export_multiregion_polymesh` on a dual-region mesh (fluid + solid) and verify that `constant/fluid/polyMesh/` and `constant/solid/polyMesh/` are populated.
   - Verify that `constant/regionProperties` is created and contains valid syntax.
   - Verify that `mappedWall` patches in `boundary` contain `sampleMode`, `sampleRegion`, and `samplePatch`.
   - Call `CaseGenerator.create_case` and verify that `0/` contains `p`, `U`, `T`, `k`, and `omega`, and that `fvSolution` contains both `p` and `SIMPLE`.
3. **Verify M10:**
   - Test `WslSimulationRunner.to_wsl_path` with paths with spaces, UNC paths, and POSIX paths.
   - Test `build_command` ensuring `shlex.quote` wraps all path arguments.
4. **Verify CAD Samples & pytest.ini:**
   - Inspect `.gitignore` for `!resources/CADTestSamples/**/*.obj`.
   - Run `pytest --collect-only` and verify all 761 tests are collected.
