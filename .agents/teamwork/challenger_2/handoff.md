# Handoff Report — Challenger 2 (Parsers, Scaling & Real CAD Samples)

**Author:** Empirical Challenger 2 (`challenger_2`)  
**Date:** 2026-10-19  
**Working Directory:** `G:\Datein\Projects\CFDSolver\.agents\teamwork\challenger_2`  
**Verdict:** **REJECT** (Critical volume invariant check defect and unhandled NaN/Inf scale factor in `scaler.py`)

---

## 1. Observation

Direct observations and empirical evidence gathered during adversarial verification:

1. **Bug in `backend/geometry/transformation/scaler.py` line 99**:
   ```python
   # Line 90 (Surface area):
   expected_area = orig_area * (factor**2)
   if orig_area > 1e-14:
       area_rel_err = abs(scaled_area - expected_area) / expected_area

   # Line 97-103 (Volume):
   expected_vol = orig_vol * (factor**3)
   if abs(orig_vol) > 1e-14:
       vol_rel_err = abs(scaled_vol - expected_vol) / abs(orig_vol)
       if vol_rel_err > 1e-7:
           raise ValueError(
               f"Volume scaling invariant violation: relative error = {vol_rel_err} > 1e-7"
           )
   ```
   - Running `scaler.scale(mesh, scale_factor=1000.0, verify_invariants=True)` on `TestCADSTPAP214.stp`:
     `orig_vol = 8000000.000000018`, `s**3 = 1000000000.0`, `expected_vol = 8000000000000018.0`, `scaled_vol = 8000000000000019.0`.
     True relative error: `abs(scaled_vol - expected_vol) / abs(expected_vol) = 1.25e-16` (IEEE 754 float64 machine epsilon).
     Evaluated `vol_rel_err` in `scaler.py`: `1.0 / 8000000.0 = 1.25e-7`.
     Command output:
     `ValueError: Volume scaling invariant violation: relative error = 1.2499999999999973e-07 > 1e-7`
   - Blind spot during downscaling ($s = 0.001$, mm $\to$ m):
     For `orig_vol = 8_000_000.0`, `expected_vol = 0.008`.
     If `scaled_vol = 0.08` (a 900% erroneous volume), `vol_rel_err` in `scaler.py` is:
     `abs(0.08 - 0.008) / 8_000_000.0 = 9.0e-9 <= 1e-7`.
     `GeometryScaler` passes the 900% corrupted volume without warning or error.

2. **Validation Defect in `backend/geometry/transformation/scaler.py` line 54-56**:
   ```python
   factor = scale_factor if scale_factor is not None else self.default_scale_factor
   if factor <= 0.0:
       raise ValueError(f"Scaling factor must be strictly positive, got {factor}")
   ```
   - Executing `scaler.scale(mesh, scale_factor=float("nan"))`:
     `float("nan") <= 0.0` is `False`.
     Vertices become `NaN`, invariant comparisons evaluate to `False`, function exits with exit code 0 and logs:
     `Scale verification passed: factor=nan, BBox span=[nan nan nan], Volume=nan m^3`.
   - Executing `scaler.scale(mesh, scale_factor=float("inf"))`:
     `float("inf") <= 0.0` is `False`.
     Returns corrupted mesh without raising an error.

3. **Voxel Hashing Rounding Boundary Artifact in `backend/geometry/parsers/stl_parser.py` line 208**:
   ```python
   grid_coords = np.round(flat_vertices * inv_eps).astype(np.int64)
   ```
   Two vertices $p_1 = 0.5\epsilon + 10^{-16}$ and $p_2 = 0.5\epsilon - 10^{-16}$ separated by $2 \times 10^{-16}\text{ m}$ round to `1` and `0` respectively, failing to weld and producing 5 vertices instead of 4.

4. **Real CAD Reference Sample Conformance (`resources/CADTestSamples/`)**:
   All 6 files ingested, scaled ($s = 0.001$), and audited:
   - `TestCAD.obj`: 8 vertices, 12 faces, $\chi = 2$, open edges = 0, multi edges = 0, BBox $[-0.115846, 0.115846]^2 \times [0, 0.2]$ m, volume $0.0080000000\text{ m}^3$ (residual $2.39 \times 10^{-11}$).
   - `TestCAD.stl`: 8 vertices, 12 faces, $\chi = 2$, open edges = 0, multi edges = 0, BBox $[-0.115846, 0.115846]^2 \times [0, 0.2]$ m, volume $0.0080000004\text{ m}^3$ (residual $4.07 \times 10^{-10}$).
   - `TestCADSTPAP203.stp`: 8 vertices, 12 faces, $\chi = 2$, open edges = 0, multi edges = 0, BBox $[-0.115846, 0.115846]^2 \times [0, 0.2]$ m, volume $0.0080000000\text{ m}^3$ (residual $1.91 \times 10^{-17}$).
   - `TestCADSTPAP214.stp`: 8 vertices, 12 faces, $\chi = 2$, open edges = 0, multi edges = 0, BBox $[-0.115846, 0.115846]^2 \times [0, 0.2]$ m, volume $0.0080000000\text{ m}^3$ (residual $1.91 \times 10^{-17}$).
   - `TestCADSTPAP242.stp`: 8 vertices, 12 faces, $\chi = 2$, open edges = 0, multi edges = 0, BBox $[-0.115846, 0.115846]^2 \times [0, 0.2]$ m, volume $0.0080000000\text{ m}^3$ (residual $1.91 \times 10^{-17}$).
   - `TestCAD.prt`: via `CADBridge`, 8 vertices, 12 faces, $\chi = 2$, open edges = 0, multi edges = 0, volume $0.0080000000\text{ m}^3$.

5. **Test Suite Execution**:
   - `tests/test_adversarial_challenger_2.py`: **35 / 35 tests passed**.

---

## 2. Logic Chain

1. From Observation 1, `area_rel_err` is normalized by `expected_area`, but `vol_rel_err` is normalized by `abs(orig_vol)`.
2. Dividing by `orig_vol` scales the apparent error by $s^3$:
   $$\frac{|V_{\text{scaled}} - V_{\text{expected}}|}{V_{\text{orig}}} = \frac{|V_{\text{scaled}} - V_{\text{expected}}|}{V_{\text{expected}}} \cdot s^3$$
3. For $s = 1000.0$, $s^3 = 10^9$. The evaluated `vol_rel_err` is $10^9$ times larger than the true relative error. Even when floating-point precision is at the theoretical limit ($1.25 \times 10^{-16}$), this formula produces $1.25 \times 10^{-7} > 1.0 \times 10^{-7}$, causing an illegitimate invariant failure.
4. For $s = 0.001$, $s^3 = 10^{-9}$. The evaluated `vol_rel_err` is $10^9$ times smaller than the true relative error. A 900% volume error yields $9.0 \times 10^{-9} \le 1.0 \times 10^{-7}$, effectively disabling invariant checking for the primary CAD mm $\to$ CFD m pipeline.
5. From Observation 2, Python comparison `float("nan") <= 0.0` returns `False`. The check `if factor <= 0.0:` fails to detect NaN or Inf, resulting in silent propagation of corrupted floating-point data through the geometry engine.
6. Therefore, `backend/geometry/transformation/scaler.py` contains critical defects that violate both robustness and correctness requirements.

---

## 3. Caveats

- The core parser modules (`STLParser`, `OBJParser`, `STEPParser`) and the Siemens NX CAD bridge (`CADBridge`, `NXTranslator`) passed all adversarial boundary, corruption, and fallback tests without failure.
- The mathematical operations of vertex coordinate scaling preserve volume as $s^3$ and area as $s^2$ to machine precision ($2.17 \times 10^{-16} < 10^{-14}$) on 64-bit IEEE 754 floats. The defect is strictly in the validation assertion formula and input argument validation in `scaler.py`.

---

## 4. Conclusion

**Verdict: REJECT**

The CAD Ingestion Engine cannot be approved for release until the two vulnerabilities in `backend/geometry/transformation/scaler.py` are resolved:

### Required Remediation (Implementer Action):
1. **Fix Volume Invariant Normalization (`backend/geometry/transformation/scaler.py`, line 99)**:
   Change:
   ```python
   vol_rel_err = abs(scaled_vol - expected_vol) / abs(orig_vol)
   ```
   To:
   ```python
   vol_rel_err = abs(scaled_vol - expected_vol) / abs(expected_vol)
   ```
2. **Fix Scale Factor Validation (`backend/geometry/transformation/scaler.py`, line 55)**:
   Change:
   ```python
   if factor <= 0.0:
   ```
   To:
   ```python
   if not np.isfinite(factor) or factor <= 0.0:
   ```

---

## 5. Verification Method

To independently verify all findings and confirm remediation:

1. **Run the Challenger 2 Adversarial Test Suite**:
   ```powershell
   .venv\Scripts\pytest tests/test_adversarial_challenger_2.py -v
   ```
2. **Inspect the Exposed Vulnerability Tests**:
   - `test_vulnerability_scaler_formula_divides_by_orig_vol_instead_of_expected_vol`
   - `test_vulnerability_scaler_permits_nan_and_inf_scale_factor`
3. **Invalidation Conditions**:
   This rejection is invalidated and can be converted to an **APPROVE** once lines 55 and 99 of `backend/geometry/transformation/scaler.py` are patched and `tests/test_adversarial_challenger_2.py` passes with updated strict assertions on `scale_factor=float("nan")` and `scale_factor=1000.0`.
