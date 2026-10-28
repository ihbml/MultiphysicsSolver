## 2026-10-28T01:17:03Z
[Message] timestamp=2026-10-28T01:17:03Z sender=4ce92fc5-81ba-402c-a361-d39a59aea46d priority=MESSAGE_PRIORITY_HIGH content=You are explorer_m1_fix_1.
Your working directory is: G:\Datein\Projects\CFDSolver\.agents\teamwork\explorer_m1_fix_1
You MUST read:
1. G:\Datein\Projects\CFDSolver\.agents\teamwork\ORIGINAL_REQUEST.md
2. G:\Datein\Projects\CFDSolver\PROJECT.md
3. G:\Datein\Projects\CFDSolver\.agents\teamwork\orchestrator_2\GATE_STATUS.md
4. G:\Datein\Projects\CFDSolver\.agents\teamwork\reviewer_m1_1\handoff.md
5. G:\Datein\Projects\CFDSolver\.agents\teamwork\challenger_m1_2\handoff.md
6. G:\Datein\Projects\CFDSolver\backend\meshing\geometry_sdf.py

Your mission is to formulate the exact, precise fix strategy and code modifications for the 7 issues identified during the Gate 1 review:
1. In `ProximityRefinement.evaluate_gap_distance`: Support external flow domains where fluid has phi > 0 (e.g. marching along +n_start or outward, detecting opposing solid surfaces where phi <= 0, and handling both inside_is_negative=True and False).
2. In `SDFEngine`: Implement `.evaluate(points: np.ndarray) -> Tuple[np.ndarray, np.ndarray]` per PROJECT.md interface contract.
3. In `CADFeatureFilter`: Ensure `mesh.patches` dictionary is preserved and re-indexed across vertex compaction (`filter_orphan_vertices`) and face pruning (`filter_degenerate_faces`, `extract_dominant_manifold_shell`).
4. In `CADMeshSDF`: Fix surface normal orientation when `inside_is_negative == False` on exact boundary points.
5. In `CADMeshSDF`: Fix scale sensitivity in barycentric projection. Replace hardcoded `det_safe = 1e-15` with a relative, scale-dependent threshold `det_safe = max(1e-20, 1e-12 * (a * c))` so sub-millimeter boundary layer elements (<= 0.1 mm) do not suffer projection errors.
6. In `CADMeshSDF`: Guard against cKDTree out-of-range indexing when query points contain extreme coordinates (>= 1e160 or Inf). Clamp indices to `[0, num_faces - 1]`.
7. In `CADFeatureFilter`: Ensure strict 2-manifold edge propagation in `extract_dominant_manifold_shell` (do not traverse across non-manifold edges with > 2 faces) and implement coincident vertex welding with spatial tolerance.

Write a complete drop-in remediation guide and code diffs in `G:\Datein\Projects\CFDSolver\.agents\teamwork\explorer_m1_fix_1\m1_remediation_plan.md` and `handoff.md`.
Send a handoff message to the orchestrator (conversation ID: 4ce92fc5-81ba-402c-a361-d39a59aea46d).
