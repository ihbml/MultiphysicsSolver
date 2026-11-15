# Dispatch Mandate: Orchestrator Generation 10

## Objectives
1. Complete Milestone M5D (Features F64 & F65):
   - Verify/finish Feature F64 (Fast IO extensions in `backend/native_rust/foam_fast_io`).
   - Implement Feature F65 (Backend JSON-RPC 2.0 / NDJSON IPC service covering all 16 endpoints, 7-element SI dimensions, Paket 1 & 2 binary buffers, Paket 4 live NDJSON telemetry).
   - Write and execute comprehensive test suite `tests/test_fast_io_and_ipc_m5d.py`.
   - Convene 3-member gate review committee (`auditor_m5d`, `critic_m5d`, `reviewer_m5d`) and achieve unanimous 3:0 PASS certification.
2. Complete Milestone M6_FINAL (Feature F66):
   - Run full sequential regression test suite across all modules.
   - Run comprehensive full-performance stress test on both:
     a) User CAD fixtures (`resources/CADTestSamples/ComplexGeometryExample/BladeFullRev1CFD.stp`, `BladeRev1CFD.obj`, `resources/CADTestSamples/TestCAD.obj`, `TestCADSTPAP214.stp`)
     b) Complex industrial geometries from `OpenFOAM_base/openfoam/tutorials/resources/geometry` (handling `.stl.gz` and `.obj.gz` transparently).
   - Convene final 3-member gate review committee (`auditor_m6_final`, `critic_m6_final`, `reviewer_m6_final`) and achieve unanimous 3:0 PASS certification.
3. Report project completion to Sentinel for final Git commit, push, and victory verification.

## Execution Rules
- Unlocked hardware: Ansys is no longer running. Full CPU threads and full mesh resolution permitted.
- Strict backend boundary: Zero frontend UI (`egui`), shaders, or `wgpu` renderers.
- Zero-Shortcut Policy: Genuine implementations only; fail-fast exceptions before crashes; no heuristic clamps; no swallowed errors.
- Unanimous 3:0 PASS required for both M5D and M6_FINAL.


## 2026-11-14T23:16:50Z
From: 5c95e7e5-efcd-4486-8f6a-022d520361f2
Content:
USER ADDITION for M5D (`backend/core/materials.py` & `materials.list`):
The user requested extending `Material` and `MaterialDatabase` in `backend/core/materials.py` so custom materials AND arbitrary/extended physical properties can be added effortlessly:
1. **Extensible `extra_properties: Dict[str, Any]` passthrough**: When loading `.json` files from `resources/materials_db/` (or via `Material`), capture any additional/custom JSON keys (including custom `unit_si` overrides) into `extra_properties` and serialize them cleanly in `to_dict()` / `materials.list` instead of dropping unknown keys.
2. **Categorized Physical Properties Schema / Optional Fields**: Support optional structured property groups or canonical optional fields covering Surface (`specific_surface_area`, `surface_energy`, `surface_tension`), Electrical (`electrical_conductivity`, `electrical_resistivity`, `permittivity`, `dielectric_constant`, `dielectric_strength`, `seebeck_coefficient`, `capacitance`, etc.), Magnetic (`magnetic_permeability`, `curie_temperature`, `magnetocaloric_coefficient`, `hall_coefficient`, etc.), Mechanical (`youngs_modulus`, `bulk_modulus`, `shear_modulus`, `poissons_ratio`, `yield_strength`, `tensile_strength`, `compressive_strength`, `fracture_toughness`, `hardness`, `speed_of_sound`, `mass_diffusivity`, `surface_roughness`, `friction_coefficient`, `acoustical_absorption`, etc.), Optical (`refractive_index`, `emissivity`, `absorptivity`, `reflectivity`, `transmittance`), Thermal (`thermal_expansion_coeff`, `thermal_diffusivity`, `melting_point`, `boiling_point`, `critical_temperature`, `triple_point`, `glass_transition_temperature`, `heat_of_vaporization`, `vapor_pressure`, `flash_point`), Manufacturing (`castability`, `machinability_rating`), and Radiological (`attenuation_coefficient`, `half_life`, `neutron_cross_section`, `specific_activity`) — stored cleanly with SI units (either as optional typed fields or categorized `properties: Dict[str, Dict[str, Any]]` + `extra_properties` passthrough) and a `MaterialDatabase.add_material(..., persist: bool = False)` helper so new materials and new properties can be added anytime via JSON or API.

Please convey this requirement to your active worker for M5D and ensure unit tests in `tests/test_fast_io_and_ipc_m5d.py` cover these extended materials capabilities.


## 2026-11-14T23:19:49Z
From: 5c95e7e5-efcd-4486-8f6a-022d520361f2
Content:
USER CLARIFICATION on `Material` properties:
All extended/categorized properties MUST be strictly optional (`None` / omitted by default) — a material does NOT need all properties populated to be valid in `MaterialDatabase`. Only the specific properties required by the active solver/physics modules need to be present when validating a case (e.g., provide `Material.missing_properties(required: Sequence[str]) -> List[str]` so `case.validate` checks only what the active program/modules actually require).

Please ensure `Material` implements this `missing_properties` helper and that properties default to `None`.
