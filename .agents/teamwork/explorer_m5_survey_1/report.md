# Survey & Audit Investigation Report: SMR Generalization & Phase 5 Audits (H5, H6, M10, H9)

**Author:** explorer_m5_survey_1  
**Date:** 2026-11-12  
**Milestone:** Phase 5 Survey & Investigation (R1, R4)  
**Status:** COMPLETE  

---

## 1. Executive Summary

This investigation delivers the technical roadmap and code analysis required to:
1. **Completely excise SMR-Xu-Froment kinetics (Audit-K8)** across all documentation, requirements, frontend communication contracts, and codebase in favor of generic, data-driven multiphysics and configurable species chemistry interfaces (Requirement R1).
2. **Resolve open Phase 5 Audits (H5, H6, M10, H9)** in `polymesh_io.py`, `case_generator.py`, `registry.py`, `wsl_runner.py`, and `moving_mesh.py` under the Zero-Shortcut Policy and strict hardware limits.

Key takeaways:
- **SMR References Inventory:** Exactly 13 workspace files and 22 `.agents` metadata files reference SMR, Xu-Froment, K8, or hardcoded methane kinetics. The active code references reside exclusively in `backend/native_cpp/smr_catalysis/` (`smrKineticsSource.C`, `smrKineticsSource.H`, `Make/files`) and `backend/physics_modules/registry.py` (`SpeciesChemistryModule`).
- **Audit H5 (Multi-Region OpenFOAM Export):** `backend/meshing/polymesh_io.py` currently exports only single-region meshes (`constant/polyMesh`). It lacks multi-region directory export (`constant/<region>/polyMesh`), `constant/regionProperties` generation, and critical patch dictionary parameters for `mappedWall` (`sampleMode`, `sampleRegion`, `samplePatch`) and `cyclic` (`neighbourPatch`, `transform`).
- **Audit H6 (Runnable Case Generation):** `backend/core/case_generator.py` creates the `0/` folder but generates no initial field files, crashes `decomposePar` whenever `mpi_ranks != 16` due to hardcoded 16-element `processorWeights`, and lacks `SIMPLE` solver blocks and relaxation factors required for steady-state solvers.
- **Audit M10 (WSL Runner Hardening & Telemetry):** `backend/runner/wsl_runner.py` uses unquoted shell strings without `shlex.quote`, silently ignores failed process return codes (`proc.returncode != 0`), and lacks the real-time residual/divergence parsing promised in its docstrings.
- **Audit H9 (Moving Mesh DGCL & Boundary Protection):** `backend/meshing/moving_mesh.py` uses a crude arithmetic average $\mathbf{w}_f \cdot \bar{\mathbf{S}}_f$ for swept volume in `verify_dgcl` instead of 3D Simpson's rule $\Delta V_f = \frac{1}{6}\Delta\mathbf{x}_f \cdot (\mathbf{S}^n + 4\mathbf{S}^{n+1/2} + \mathbf{S}^{n+1})$ and lacks BDF2 time stepping. It also relies on a geometric heuristic (`dists > 0.85 * max_d`) to identify fixed boundaries.

---

## 2. Exhaustive SMR & K8 Reference Inventory (Requirement R1)

An exhaustive case-insensitive regex scan (`\bsmr\b`, `xu[-_ ]froment`, `\bfroment\b`, `\bK8\b`, `steam methane`, `smrkineticssource`, `smr_catalysis`, `\bch4\b`) was performed across the repository.

### 2.1 Active Codebase Files
| File Path | Line(s) | Current Context / Hardcoding | Required Action |
| :--- | :--- | :--- | :--- |
| `backend/native_cpp/smr_catalysis/smrKineticsSource.C` | 1–90 | Implements `smrKineticsSource` with hardcoded constants: `A_smr_ = 7.67e6`, `Ea_R_ = 12748.9`, `dH_smr_ = 2.061e5`, `eta_cat_ = 0.902`, explicit field lookup `mesh_.lookupObject<volScalarField>("CH4")`, and uncoupled energy sink without species mass balance. | Replace with generic, data-driven reaction fvOption (`genericReactionSource` or `configurableKineticsSource`) where species names, stoichiometry, rate parameters, and Jacobian diagonals are configured via OpenFOAM dictionaries. |
| `backend/native_cpp/smr_catalysis/smrKineticsSource.H` | 1–60 | Declares `smrKineticsSource` class inheriting from `cellSetOption`. | Replace with generic header for data-driven reactions. |
| `backend/native_cpp/smr_catalysis/Make/files` | 1–3 | Compiles `smrKineticsSource.C` into `libsmrKineticsSource`. | Rename to generic reaction source target (`genericReactionSource.C`, `libgenericReactionSource`). |
| `backend/physics_modules/registry.py` | 93 | `description="Mehrkomponententransport, Finite-Rate-Reaktionskinetik, Wandkatalyse (SMR/WGS) und Verbrennung."` | Remove `(SMR/WGS)` from description; describe as generic multi-component transport and configurable reaction kinetics. |
| `backend/physics_modules/registry.py` | 101 | `return ["Ydefault", "CH4", "H2O", "H2", "CO", "CO2", "N2"]` in `SpeciesChemistryModule.get_required_fields()` | Replace hardcoded methane species list with dynamic list from `self.config.parameters.get("species", ["Ydefault"])`. |

### 2.2 Requirements, Specifications & Architectural Documentation
| File Path | Line(s) | Current Content | Required Action |
| :--- | :--- | :--- | :--- |
| `SUGGESTIONS_BACKEND.md` | 48, 59, 429–433 | Section 9.4.1 titled *"SMR-Katalysequelle physikalisch konsistent neu aufsetzen (`smrKineticsSource.C`, Audit-K8)"* proposing reversible Xu-Froment kinetics $(1 - Q/K_{eq})$. | Replace Section 9.4.1 with generic data-driven reaction kinetics interface and multi-species mass/energy conservation. |
| `FRONTEND_REQUIREMENTS_ON_BACKEND.md` | 164 | *"Reaktionsmodell (`Finite-Rate / Eddy-Dissipation`, `Surface Heterogeneous Kinetics / SMR`, `Inert Species Transport`), Pfad zur Chemkin-/OpenFOAM-`reactions`-Datei, aktive Speziesliste (`CH4`, `H2O`, `CO`, `H2`, `CO2`, `N2`), Auswahl der katalytischen `cellZone` / `patch`."* | Remove SMR and hardcoded species; specify generic heterogeneous / homogeneous kinetics and user-defined species list. |
| `COMMUNICATION_BACKEND_FRONTEND.md` | 56 | Field table lists: `CH4`, `H2`, `CO` with description *"Spezies-Massenanteile"*. | Replace with generic `Y_<species>` or configurable list `Y_0, Y_1, ...`. |
| `COMMUNICATION_BACKEND_FRONTEND.md` | 70 | Changelog lists: *"3. `backend/native_cpp/smr_catalysis/`: C++ `fvOption` für endotherme Wandkatalyse mit Jacobi-stabilisierter Hauptdiagonale"*. | Update and add new handover log entry documenting excision of SMR and availability of generic multiphysics chemistry. |
| `backend/OPENFOAM_BACKEND_ATLAS.md` | 24, 177, 252, 254, 274, 277, 283 | References to `smr_catalysis/`, `smrKineticsSource.C`, and `libsmrKineticsSource.so`. | Generalize to generic C++ reaction fvOption library (`libgenericReactionSource.so`). |
| `backend/README.md` | 38 | `smr_catalysis/ # C++ fvOptions mit analytischer Jacobi-Stabilisierung (wmake)` | Rename to generic reaction kinetics library. |
| `ROADMAP.md` | 37 | Mentions Audit-K8. | Update to reflect generic multiphysics chemistry completion. |
| `docs/TEAMWORK_DEVELOPMENT_HISTORY.md` | 147 | Mentions Xu-Froment SMR kinetics in Phase 5 table. | Update description to generic multiphysics/species chemistry generalization. |

### 2.3 Frontend Design References (Informational)
| File Path | Line(s) | Content | Note |
| :--- | :--- | :--- | :--- |
| `frontend/design_reference/palettes.json` | 84 | `"recommended_fields": ["H2", "CH4", "CO", ...]` | UI palette color recommendation tags. Can remain or be generalized to `["Y_species", ...]`. |
| `frontend/design_reference/DESIGN_REFERENCE.md` | 70 | Color mapping description mentioning species. | UI design reference. |

---

## 3. Generic Multiphysics & Configurable Chemistry Architecture

### 3.1 Python Backend Interface: `SpeciesChemistryModule`
In `backend/physics_modules/registry.py`:
- **Dynamic Species Configuration:**
  ```python
  class SpeciesChemistryModule(BasePhysicsModule):
      def __init__(self):
          super().__init__(
              name="Species & Chemistry",
              category="Reactions",
              description=(
                  "Mehrkomponententransport, finite-rate Reaktionskinetik, "
                  "heterogene Oberflächenkatalyse und Gleichgewichtschemie."
              ),
          )
          self.config.model_variant = "finiteRateKinetics"
          self.config.parameters = {
              "species": ["Ydefault"],
              "reactions_file": "constant/reactions",
              "thermo_file": "constant/thermophysicalProperties",
              "reaction_model": "laminarArrhenius",  # or eddyDissipation, PaSR, heterogeneousCatalysis
          }

      def get_supported_solvers(self) -> List[str]:
          return ["reactingFoam", "chtMultiRegionFoam", "rhoReactingFoam", "buoyantReactingFoam"]

      def get_required_fields(self) -> List[str]:
          species_list = self.config.parameters.get("species", ["Ydefault"])
          return list(species_list)

      def generate_dictionary_entries(self, case_dir: str) -> Dict[str, str]:
          species = self.config.parameters.get("species", ["Ydefault"])
          specie_names = " ".join(species)
          return {
              "constant/chemistryProperties": (
                  "chemistryType\n{\n    method ode;\n    solver EulerImplicit;\n}\n"
                  "chemistry on;\ninitialChemicalTimeStep 1e-07;\n"
              ),
              "constant/thermophysicalProperties": (
                  "thermoType\n{\n"
                  "    type            heRhoThermo;\n"
                  "    mixture         multiComponentMixture;\n"
                  "    transport       sutherland;\n"
                  "    thermo          janaf;\n"
                  "    energy          sensibleEnthalpy;\n"
                  "    equationOfState perfectGas;\n"
                  "    specie          specie;\n"
                  "}\n\n"
                  f"species\n(\n    {specie_names}\n);\n"
              ),
          }
  ```

### 3.2 Native OpenFOAM C++ Interface: `genericReactionSource`
In `backend/native_cpp/reaction_kinetics/` (replacing `smr_catalysis/`):
- **Universal Data-Driven `fvOption`:**
  - Reads configuration dynamically from `coeffs`:
    - `species` list (e.g. `(A B C D)`)
    - Stoichiometric coefficients $\nu_k$ (negative for reactants, positive for products)
    - Arrhenius rate parameters: $A$ (pre-exponential), $\beta$ (temperature exponent), $E_a/R$ (activation temperature)
    - Reaction enthalpy: $\Delta H_r$
    - Reversible equilibrium constants or polynomial coefficients
    - Active `cellZone` or `patch`
  - In `addSup(const volScalarField& rho, fvMatrix<scalar>& eqn, const label fieldi)`:
    - Inspects `eqn.psi().name()` or `fieldNames_[fieldi]` to know which equation is being addressed.
    - If `fieldName == "he"` or `fieldName == "h"` or `fieldName == "e"`:
      Calculates reaction rate $r$ and applies total energy source $S_h = -\sum r \cdot \Delta H_r$ with implicit temperature derivative $\frac{\partial S_h}{\partial T}\frac{1}{C_p}$ added to `eqn.diag()`.
    - If `fieldName == species_k`:
      Applies mass source $S_k = \nu_k M_k r$ with implicit diagonal stabilization $-\frac{\partial S_k}{\partial Y_k}$ added to `eqn.diag()`.
    - Enforces conservation of mass $\sum_k S_k = 0$ across all participating species.

---

## 4. Phase 5 Audits Investigation (H5, H6, M10, H9)

### 4.1 Audit H5: Multi-Region Export in `backend/meshing/polymesh_io.py`
#### Current Code Observation
- Lines 55–180 implement `export_openfoam_polymesh(mesh, case_dir, ...)`.
- Output path is fixed to `case_dir / "constant" / "polyMesh"`.
- In lines 150–176, the boundary file writer generates:
  ```foam
  patch_name
  {
      type        mappedWall;
      nFaces      100;
      startFace   500;
  }
  ```
  It completely omits `sampleMode nearestPatchFace;`, `sampleRegion <region>;`, `samplePatch <patch>;`.
- For `cyclic`, it omits `neighbourPatch <partner>;` and `transform noOrdering;`.
- No method exists to export multi-region CHT setups for `chtMultiRegionFoam` or generate `constant/regionProperties`.

#### Required Solution
1. Add `export_openfoam_multiregion_polymesh(submeshes: Dict[str, UnstructuredMesh], case_dir, fluid_regions, solid_regions)`:
   - For each region, writes mesh into `constant/<region_name>/polyMesh/`.
   - Generates `constant/regionProperties`:
     ```foam
     regions
     (
         fluid ( <fluid_names> )
         solid ( <solid_names> )
     );
     ```
2. Enhance `boundary` generation in `export_openfoam_polymesh`:
   - Support `patch_metadata: Dict[str, Dict[str, Any]]` where caller specifies:
     - For `mappedWall`: `sampleMode`, `sampleRegion`, `samplePatch`.
     - For `cyclic`: `neighbourPatch`, `transform`.
   - Include these properties cleanly in the OpenFOAM boundary dictionary.

---

### 4.2 Audit H6: Runnable Case Generation & Dynamic Decomposition
#### Current Code Observation
- In `backend/core/case_generator.py`:
  - Lines 39–42: `zero_dir = case_path / "0"; zero_dir.mkdir(...)`. **No files are ever created in `0/`!** A newly created OpenFOAM case has zero fields and will immediately exit with fatal error on solver launch.
  - Lines 125–131: `processorWeights` is hardcoded to 16 values (8 P-core weights, 8 E-core weights):
    ```foam
    processorWeights
    (
        1.5 1.5 1.5 1.5 1.5 1.5 1.5 1.5
        1.0 1.0 1.0 1.0 1.0 1.0 1.0 1.0
    );
    ```
    If `mpi_ranks` is set to any number other than 16 (e.g. 2, 4, 8, 32), OpenFOAM `decomposePar` crashes with:
    `Fatal Error: size of processorWeights (16) does not match numberOfSubdomains (N)`.
  - Lines 180–214: `_write_fv_solution` only writes solvers for `p_rgh` and `(U|h|k|omega)` and only a `PIMPLE` dictionary. Incompressible steady-state solvers (`simpleFoam`) fail due to lack of `SIMPLE` dictionary, lack of `relaxationFactors` block, and lack of `p` solver.

#### Required Solution
1. **Dynamic `processorWeights`**:
   - Construct weights dynamically based on `ranks`, allocating $\min(\text{ranks}, 8)$ to P-cores and remainder to E-cores (or uniform 1.0 if not on hybrid architecture).
   - Ensure `len(weights) == ranks`.
2. **Comprehensive `fvSolution`**:
   - Support solver mode dispatch (`steady` -> `SIMPLE`, `transient` -> `PIMPLE`).
   - Add `SIMPLE` block with `residualControl` (`p 1e-5; U 1e-5; ...`).
   - Add `relaxationFactors` block for `fields` (`p 0.3; ...`) and `equations` (`U 0.7; k 0.7; omega 0.7; ...`).
   - Provide standard solvers for both `p` and `p_rgh`.
3. **Automated `0/` Initial Field Files**:
   - Create standard field generator for required fields (`U`, `p`, `p_rgh`, `T`, `k`, `omega`, `nut`, species `Y_*`).
   - Write standard OpenFOAM FoamFile header, `dimensions`, `internalField`, and `boundaryField` matching mesh patches (`inlet`, `outlet`, `wall`, `symmetry`, `empty`, `frontAndBack`).

---

### 4.3 Audit M10: WSL Simulation Runner Hardening & Telemetry
#### Current Code Observation
- In `backend/runner/wsl_runner.py`:
  - Line 28: `def build_command(self, case_wsl_dir: str, command: str) -> str: return f"source {self.bashrc_path} && cd {case_wsl_dir} && {command}"`.
    No path quoting! Paths with spaces, brackets, or shell meta-characters fail or create command injection vectors.
  - Lines 36 and 47: `return subprocess.run(...)`.
    The return code is never checked! If `blockMesh`, `decomposePar`, or the solver fails, the function silently returns a `CompletedProcess` with non-zero exit code.
  - Zero residual parsing or divergence extraction is implemented. The class is a bare 47-line wrapper despite docstrings promising real-time residual extraction and divergence monitoring.

#### Required Solution
1. **Security & Path Hardening**:
   - Import `shlex`.
   - Use `shlex.quote(case_wsl_dir)`, `shlex.quote(self.bashrc_path)`, and validate paths.
   - Enforce exit code verification (`check=True` or explicit `if proc.returncode != 0: raise RuntimeError(...)` with detailed stderr capture).
2. **Real-Time Telemetry & Residual Extraction**:
   - Implement `stream_run(case_dir, solver, ranks)` yielding structured telemetry:
     ```python
     @dataclass
     class IterationTelemetry:
         time: float
         iteration: int
         residuals: Dict[str, float]
         courant_max: Optional[float]
         execution_time: float
         nan_detected: bool
     ```
   - Parse OpenFOAM log output line by line using robust regexes:
     - `Time = ([\d\.\+eE-]+)`
     - `Solving for (\w+), Initial residual = ([\d\.\+eE-]+), Final residual = ([\d\.\+eE-]+), No Iterations (\d+)`
     - `Courant Number mean: ([\d\.\+eE-]+) max: ([\d\.\+eE-]+)`
     - Check for `NaN` / `Floating point exception` / `Fatal error` and immediately raise or flag divergence.

---

### 4.4 Audit H9: Moving Mesh Swept Volume (Simpson's Rule & BDF2 DGCL)
#### Current Code Observation
- In `backend/meshing/moving_mesh.py`:
  - Lines 645–650 in `verify_dgcl`:
    ```python
    s_mid = 0.5 * (mesh_n.face_areas + mesh_n1.face_areas)
    w_f = (mesh_n1.face_centres - mesh_n.face_centres) / dt
    flux = np.sum(w_f * s_mid, axis=1)
    ```
    This calculates swept volume as $\mathbf{w}_f \cdot \bar{\mathbf{S}}_f$ using midpoint arithmetic area.
    In 3D, for a moving facet whose vertices undergo non-linear or affine motion, the exact volume swept by a polygon face between $t^n$ and $t^{n+1}$ requires numerical integration along the trajectory.
  - Line 643: `d_vol_dt = (v_n1 - v_n) / dt`.
    Uses 1st order backward difference. Paper 6 (`DiscreteGCL_1709.04788v4.pdf`) specifies that for higher-order ALE schemes (such as BDF2), the DGCL must be computed using BDF2 time stepping:
    $$\dot{V}_f^{n+1} = \frac{3\Delta V_f^{n, n+1} - \Delta V_f^{n-1, n}}{2\Delta t}$$
  - Lines 444–449 in `deform_mesh`:
    ```python
    if not np.any(fixed_mask):
        moving_centroid = np.mean(moving_pts, axis=0)
        dists_from_moving = np.linalg.norm(points - moving_centroid, axis=1)
        max_d = np.max(dists_from_moving)
        fixed_mask = dists_from_moving > 0.85 * max_d
    ```
    Uses an arbitrary heuristic (`0.85 * max_d`) to identify fixed boundaries when none are explicitly provided, which violates the Zero-Shortcut Policy.

#### Required Solution
1. **Paper 6 Exact 3D Swept Volume via Simpson's Rule**:
   - For a moving face $f$ between positions $\mathbf{x}_f^n$ and $\mathbf{x}_f^{n+1}$, calculate the intermediate face configuration at $t^{n+1/2} = \frac{1}{2}(\mathbf{x}^n + \mathbf{x}^{n+1})$.
   - Evaluate face area vectors $\mathbf{S}_f^n$, $\mathbf{S}_f^{n+1/2}$, and $\mathbf{S}_f^{n+1}$.
   - Exact swept volume increment:
     $$\Delta V_f^{n, n+1} = \frac{1}{6}(\mathbf{x}_f^{n+1} - \mathbf{x}_f^n) \cdot \left(\mathbf{S}_f^n + 4\mathbf{S}_f^{n+1/2} + \mathbf{S}_f^{n+1}\right)$$
2. **BDF2-DGCL Time Stepping**:
   - When history mesh $t^{n-1}$ is available:
     $$\dot{V}_f^{n+1} = \frac{3\Delta V_f^{n, n+1} - \Delta V_f^{n-1, n}}{2\Delta t}$$
     $$V_c^{n+1} - \frac{4}{3}V_c^n + \frac{1}{3}V_c^{n-1} = \frac{2\Delta t}{3} \sum_{f \in \partial c} \sigma_{cf} \dot{V}_f^{n+1}$$
   - When only $t^n$ and $t^{n+1}$ are given (1st order / start-up):
     $$\dot{V}_f^{n+1} = \frac{\Delta V_f^{n, n+1}}{\Delta t}$$
3. **Topological Boundary Detection**:
   - Replace `0.85 * max_d` heuristic with topological boundary inspection: all nodes belonging to faces with `neighbour == -1` that are not in `moving_mask` are fixed Dirichlet nodes ($\mathbf{u} = \mathbf{0}$).
   - Call `deformed_mesh.validate()` to guarantee positive volumes and Euler characteristic invariance.

---

## 5. Specification of Handover Log Updates in `COMMUNICATION_BACKEND_FRONTEND.md`

The following exact text must be added to `COMMUNICATION_BACKEND_FRONTEND.md` in Section 3:

```markdown
### [2026-11-12] — Generalisierung Physik-Module & SMR-Exzision (Backend -> Frontend)
* **Autor:** Backend Teamwork
* **Was wurde implementiert?**
  1. **SMR-Exzision & Generalisierung (Audit-K8, R1):** Alle spezifischen SMR-Xu-Froment-Abhängigkeiten wurden restlos aus dem Backend und den Schnittstellen entfernt.
  2. **Konfigurierbare Spezies-Kinetik:** `SpeciesChemistryModule` in `backend/physics_modules/registry.py` unterstützt nun beliebige Spezieslisten (`species_names`), Mechanismen-Dateien (Chemkin / OpenFOAM `reactions`) und Reaktionsmodelle (Laminar Arrhenius, EDC, PaSR, Heterogene Oberflächenkatalyse).
  3. **Generische C++ Reaktions-fvOption:** Ersatz von `smrKineticsSource` durch datengesteuerte Reaktionsquellterme mit analytischer Jacobi-Hauptdiagonale für beliebige Reaktionen.
  4. **Multi-Region CHT-Export (Audit-H5):** `backend/meshing/polymesh_io.py` unterstützt nun getrennte Regionen (`constant/<region>/polyMesh`), `constant/regionProperties` sowie vollständige `mappedWall`- und `cyclic`-Patch-Attribute.
  5. **Lauffähige OpenFOAM-Cases (Audit-H6):** `CaseGenerator` erzeugt vollständige `0/`-Startfelder, dynamische `processorWeights` passend zu beliebigen `mpi_ranks` und vollständige `fvSolution`-Blöcke für `SIMPLE` (stationär) und `PIMPLE` (transient).
  6. **Gehärteter WSL-Runner (Audit-M10):** `WslSimulationRunner` nutzt `shlex.quote`, prüft Return-Codes strikt und liefert Live-Residuen-/Divergenz-Streaming.
  7. **DGCL-treue Netzbewegung (Audit-H9):** `moving_mesh.py` implementiert Simpson-Swept-Volume und BDF2-DGCL für bewegte Gitter ohne künstliche Massenquellen.
* **Hinweis an das Frontend:**
  * Im Viewport-Feldkatalog wird `CH4, H2, CO` durch dynamische Speziesfelder `Y_<species>` ersetzt.
  * Das Cockpit kann über `module.config.parameters["species"]` beliebige Spezieslisten an das Backend übergeben.
```

And in Section 2.B (Field table):
Replace line 56:
`| CH4, H2, CO | Spezies-Massenanteile | kg/kg | Species & Chemistry | Skalar [0, 1] |`
With:
`| Y_<species> (z. B. Y_O2, Y_H2) | Spezies-Massenanteile | kg/kg | Species & Chemistry | Skalar [0, 1] |`

---

## 6. Actionable Implementation Plan for Workers

1. **Worker 1 (SMR Excision & Multiphysics Chemistry):**
   - Refactor `backend/physics_modules/registry.py` to make `SpeciesChemistryModule` fully configurable.
   - Replace `backend/native_cpp/smr_catalysis/` with generic reaction fvOption or remove obsolete SMR files.
   - Update `COMMUNICATION_BACKEND_FRONTEND.md`, `FRONTEND_REQUIREMENTS_ON_BACKEND.md`, `backend/OPENFOAM_BACKEND_ATLAS.md`, and `SUGGESTIONS_BACKEND.md`.
2. **Worker 2 (Phase 5 Audits H5 & H6):**
   - Extend `backend/meshing/polymesh_io.py` with multi-region export and full `mappedWall`/`cyclic` dictionary attributes.
   - Overhaul `backend/core/case_generator.py` to generate complete `0/` fields, dynamic `processorWeights`, and `SIMPLE`/`PIMPLE` `fvSolution`.
3. **Worker 3 (Phase 5 Audits M10 & H9):**
   - Harden `backend/runner/wsl_runner.py` with `shlex.quote`, return code checks, and streaming regex residual parser.
   - Implement Simpson's swept volume rule and BDF2-DGCL in `backend/meshing/moving_mesh.py:verify_dgcl` and eliminate the boundary heuristic.
