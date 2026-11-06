# Dead Ends & Failed Approaches Log

| Iteration | Approach Tried | Why It Failed | Files Touched |
|---|---|---|---|
| 1 | Ad-hoc parallelogram quad recovery heuristic in `CADMeshSDF.__init__` and magic `1e30`/`1e60` clamping in C++ AABB tree | Quad recovery violates Zero-Shortcut Policy (hallucinates faces to heal broken test fixture). In C++ `sdf_engine.cpp`, queries > 1e30 prune tree, return facet -1, and cause out-of-bounds array reads in `Pseudonormals::evaluate`, flipping sign to interior. Must use `std::numeric_limits<double>::infinity()`, explicit facet bounds checks, and fix test fixture directly. | `backend/meshing/geometry_sdf.py`, `backend/meshing/native/src/sdf_engine.cpp` |
