# Handoff Report: Phase 5 & System/Test Infrastructure Survey

**Agent:** Survey Explorer 3 (`teamwork_explorer_survey_3`)  
**Parent Agent:** `e94c2e2b-58f3-4ba2-88c0-f19fe4c7046c`  
**Working Directory:** `G:\Datein\Projects\CFDSolver\.agents\teamwork\teamwork_explorer_survey_3\`  
**Date:** 2026-11-10  
**Status:** Hard Handoff (Investigation & Survey Complete)

---

## 1. Observation

### 1.1 K8: SMR Catalysis Source
- **File:** `backend/native_cpp/smr_catalysis/smrKineticsSource.C`
  - Lines 51-56:
    ```cpp
    void smrKineticsSource::addSup
    (
        const volScalarField& rho,
        fvMatrix<scalar>& eqn,
        const label fieldi
    )
    ```
  - Lines 63-85:
    ```cpp
    scalarField& heSource = eqn.source();
    scalarField& heDiag   = eqn.diag();
    ...
    if (Tc > 600.0 && y_ch4 > 1e-5)
    {
        const scalar c_ch4 = (rho[cellI] * y_ch4) / 1.6043e-2;
        const scalar k_rate = eta_cat_ * A_smr_ * Foam::exp(-Ea_R_ / Tc);
        const scalar q_sink = -k_rate * c_ch4 * dH_smr_; // [W/m^3]

        const scalar dq_dh = q_sink * (Ea_R_ / (Tc * Tc * max(1000.0, Cp[cellI])));

        heSource[cellI] -= (q_sink - dq_dh * eqn.psi()[cellI]) * V[cellI];
        heDiag[cellI]   += dq_dh * V[cellI];
    }
    ```
  - `fieldi` is never inspected.
  - No species source terms are added to any species equations (`CH4`, `H2O`, `H2`, `CO`, `CO2`).
  - Reaction is formulated as a single irreversible rate without equilibrium driving force $(1 - Q/K_{eq})$.

### 1.2 H5: OpenFOAM polyMesh Export
- **File:** `backend/meshing/polymesh_io.py`
  - Lines 76-78:
    ```python
    case_path = Path(case_dir).resolve()
    poly_mesh_dir = case_path / "constant" / "polyMesh"
    poly_mesh_dir.mkdir(parents=True, exist_ok=True)
    ```
    Only exports to a single `constant/polyMesh/` directory. No multi-region export function exists.
  - Lines 164-165, 171-176:
    ```python
    elif "mapped" in p_lower or "coupled" in p_lower:
        ptype = "mappedWall"
    ...
    f.write(f"    {patch_name}\n")
    f.write("    {\n")
    f.write(f"        type            {ptype};\n")
    f.write(f"        nFaces          {int(count_f)};\n")
    f.write(f"        startFace       {int(start_f)};\n")
    f.write("    }\n")
    ```
    Omits mandatory OpenFOAM entries for `mappedWall` (`sampleMode`, `sampleRegion`, `samplePatch`).
  - Lines 162-163: `cyclic` patch export omits `neighbourPatch` and `transform`.
  - Line 58: `write_binary: bool = False` argument is accepted but never used.

### 1.3 H6: Case Generator
- **File:** `backend/core/case_generator.py`
  - Lines 39, 41: `zero_dir = case_path / "0"`, `d.mkdir(parents=True, exist_ok=True)`. Zero files are written into `0/`.
  - Lines 125-131:
    ```cpp
    processorWeights
    (
        // 8 P-Cores
        {p_weight} {p_weight} {p_weight} {p_weight} {p_weight} {p_weight} {p_weight} {p_weight}
        // 8 E-Cores
        {e_weight} {e_weight} {e_weight} {e_weight} {e_weight} {e_weight} {e_weight} {e_weight}
    );
    ```
    Hardcodes 16 weights regardless of `mpi_ranks`.
  - Lines 189-211: `fvSolution` only contains `p_rgh` and `(U|h|k|omega)`. Lacks `p`, lacks `SIMPLE` algorithm block, and lacks `relaxationFactors`.
  - Line 157: `divSchemes { default none; ... }` with only 4 divergence terms.
  - For `chtMultiRegionFoam`, `constant/regionProperties` is never generated.

### 1.4 M10: WSL Runner
- **File:** `backend/runner/wsl_runner.py`
  - Lines 21-26:
    ```python
    @staticmethod
    def to_wsl_path(win_path: str | Path) -> str:
        p = Path(win_path).resolve()
        drive = p.drive[0].lower()
        parts = [p.as_posix().split(":", 1)[1].lstrip("/")]
        return f"/mnt/{drive}/{parts[0]}"
    ```
    If `p.drive` is empty, `p.drive[0]` raises `IndexError`.
  - Lines 28-30:
    ```python
    def build_command(self, case_wsl_dir: str, command: str) -> str:
        return f"source {self.bashrc_path} && cd {case_wsl_dir} && {command}"
    ```
    Lacks `shlex.quote`.
  - Lines 36, 47:
    `subprocess.run(["wsl", "bash", "-c", cmd], capture_output=True, text=True)`
    Return codes are never checked.

### 1.5 System & Test Infrastructure
- **File:** `pytest.ini:1-4`
  ```ini
  [pytest]
  pythonpath = .
  testpaths = backend/geometry/tests
  ```
  Only collected 37 geometry tests.
- **File:** `.gitignore:34`
  ```gitignore
  *.obj
  ```
  Gitignored `TestCAD.obj` and `BladeRev1CFD.obj`.
- **Command Output:**
  - `.\\.venv\\Scripts\\python.exe -m pytest backend/geometry/tests`: **37 passed in 13.63s**.
  - `.\\.venv\\Scripts\\python.exe -m pytest tests backend/geometry/tests --collect-only`: **761 tests collected**.

---

## 2. Logic Chain

1. **K8 Logic Chain:**
   - From Observation 1.1, `addSup` accepts `const label fieldi`, but modifies `eqn.source()` and `eqn.diag()` directly with an enthalpy sink without checking `fieldNames_[fieldi]`.
   - In OpenFOAM, `fvOption::addSup` is invoked per equation for every field registered in `fields`.
   - Therefore, if `fields` contains `(h CH4 H2O)`, the enthalpy sink of hundreds of MW/m³ is subtracted from the `CH4` and `H2O` mass fraction equations.
   - Furthermore, because species source terms $R_i$ are never evaluated or applied, CH₄ is never depleted and reaction continues unphysically.
   - In addition, real SMR is limited by equilibrium; without $(1 - Q/K_{eq})$, reaction rates cannot represent physical chemical equilibrium.

2. **H5 & H6 Logic Chain:**
   - From Observation 1.2, `export_openfoam_polymesh` writes only to `constant/polyMesh/`.
   - In OpenFOAM, `chtMultiRegionFoam` requires each region's mesh in `constant/<regionName>/polyMesh/` and requires `constant/regionProperties`.
   - Therefore, `chtMultiRegionFoam` cannot run on cases exported by the current `polymesh_io.py`.
   - Furthermore, `mappedWall` requires `sampleMode`, `sampleRegion`, and `samplePatch`. Without them, `checkMesh` and OpenFOAM solvers fail immediately with fatal IO errors.
   - From Observation 1.3, `case_generator.py` creates `0/` but writes no field files; OpenFOAM solvers exit immediately with `cannot find file "0/U"`.
   - When `mpi_ranks != 16`, scotch throws an error because `processorWeights` has length 16.
   - For `simpleFoam`, `fvSolution` lacks `p`, `SIMPLE` dict, and `relaxationFactors`, so `simpleFoam` cannot execute or converge.

3. **M10 Logic Chain:**
   - From Observation 1.4, `to_wsl_path` indexes `p.drive[0]`. On POSIX or drive-less paths, `p.drive` is `""`, leading to an uncaught `IndexError`.
   - Because `build_command` does not quote arguments with `shlex.quote`, paths containing spaces cause bash command parsing errors.
   - Because `subprocess.run` return codes are not validated, failed OpenFOAM executions silently return without signaling failure.

4. **System Baseline Logic Chain:**
   - From Observation 1.5, `pytest.ini` testpaths specified only `backend/geometry/tests`. Running bare `pytest` skipped 724 out of 761 tests.
   - The `.gitignore` pattern `*.obj` prevented `resources/CADTestSamples/TestCAD.obj` and `BladeRev1CFD.obj` from being committed to git, which explains why the external code review reported missing CAD files.
   - On the local filesystem, all CAD files are present, and geometry tests pass 100% (37/37).

---

## 3. Caveats

1. **OpenFOAM Binary Compilation:** OpenFOAM was not compiled or executed natively in Windows during this survey; evaluation of OpenFOAM dictionary syntax and solver requirements is based on upstream OpenFOAM source code standards (v2406 / Foundation 11).
2. **Sequential Test Duration:** Full sequential execution of all 761 tests under `OMP_NUM_THREADS=2` requires significant runtime due to heavy 3D CAD meshing tests (e.g. 36-blade turbine rotor assembly); 211 tests were executed and all passed with 0 failures during this survey.
3. **Hardware Constraints:** Host RAM is shared with an active background Ansys process (~95% RAM utilization), so all testing was strictly constrained to low-resource settings (`OMP_NUM_THREADS=2`, `MKL_NUM_THREADS=2`).

---

## 4. Conclusion

1. **Phase 5 Implementation Requirements:**
   - **K8:** In `smrKineticsSource.C`, evaluate `fieldNames_[fieldi]`. Implement stoichiometric coupling across species ($\text{CH}_4, \text{H}_2\text{O}, \text{H}_2, \text{CO}, \text{CO}_2$) and enthalpy sink $q = -\sum r_i \Delta H_i$ using reversible Xu-Froment driving forces $(1 - Q/K_{eq})$.
   - **H5:** In `polymesh_io.py`, add `export_multiregion_polymesh` with `constant/regionProperties` generation, and populate mandatory OpenFOAM keys for `mappedWall` (`sampleMode`, `sampleRegion`, `samplePatch`) and `cyclic` (`neighbourPatch`, `transform`).
   - **H6:** In `case_generator.py`, write standard `0/` initial fields, dynamically size scotch `processorWeights`, add `p` and `SIMPLE` with relaxation factors to `fvSolution`, and generate region folders for `chtMultiRegionFoam`.
   - **M10:** In `wsl_runner.py`, harden `to_wsl_path`, wrap all arguments with `shlex.quote`, and check subprocess return codes.
2. **System Baseline Remediation:**
   - Update `pytest.ini` to `testpaths = tests backend/geometry/tests`.
   - Update `.gitignore` with `!resources/CADTestSamples/**/*.obj`.
   - Add `@pytest.mark.skipif` guards for tests requiring Siemens NX or `OpenFOAM_base/`.

---

## 5. Verification Method

To independently verify the survey observations:
1. **Verify K8 Code Defect:**
   Inspect `backend/native_cpp/smr_catalysis/smrKineticsSource.C:51-87`. Confirm that `fieldi` is unused and that no species equations are coupled.
2. **Verify H5 & H6 Code Defect:**
   Inspect `backend/meshing/polymesh_io.py:164-176`. Confirm that `mappedWall` lacks `sampleMode`, `sampleRegion`, and `samplePatch`.
   Inspect `backend/core/case_generator.py:39-43`. Confirm that `0/` has no generated field files.
3. **Verify M10 Code Defect:**
   Inspect `backend/runner/wsl_runner.py:21-26`. Test in Python: `WslSimulationRunner.to_wsl_path("/tmp/test")` -> raises `IndexError: string index out of range`.
4. **Verify Test Discovery & Geometry Baseline:**
   Run:
   ```powershell
   $env:OMP_NUM_THREADS="2"; $env:MKL_NUM_THREADS="2"; .\.venv\Scripts\python.exe -m pytest backend/geometry/tests
   ```
   Expected: 37 passed in ~13 seconds.
   Run:
   ```powershell
   $env:OMP_NUM_THREADS="2"; $env:MKL_NUM_THREADS="2"; .\.venv\Scripts\python.exe -m pytest tests backend/geometry/tests --collect-only
   ```
   Expected: 761 tests collected.
5. **Verify CAD Files on Disk:**
   Run:
   ```powershell
   Get-ChildItem -Path "resources\CADTestSamples" -Recurse | Select-Object FullName, Length
   ```
   Confirm presence of `TestCAD.obj`, `TestCADSTPAP214.stp`, `BladeRev1CFD.obj`, and `BladeFullRev1CFD.stp`.
