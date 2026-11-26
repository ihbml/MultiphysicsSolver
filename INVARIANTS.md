# INVARIANTS.md — Normative Mathematical & Topological Mesh Invariants

Every mesh produced by the native C++20 core or Python pipeline must satisfy these invariants. All checks are implemented independently from raw vertex coordinates (`points`) and face-node loops (`faces` / `face_offsets` + `face_nodes`) in [`backend/meshing/invariants.py`](backend/meshing/invariants.py).

| ID | Mathematical Statement | Tolerance | Checker Function |
|---|---|---|---|
| **I-01** | Closed-cell Gauss surface vector sum: $\max_c \frac{\|\sum_{f \in \partial c} \sigma_{cf}\mathbf{S}_f\|_2}{\sum_{f \in \partial c} \|\mathbf{S}_f\|_2}$ | $< 10^{-11}$ | `check_closure` |
| **I-02** | Strictly positive cell volumes: $V_c > 0\;\forall c$ (computed independently from Newell triangle fans) | exact ($> 10^{-18}$) | `check_positive_volume` |
| **I-03** | Global domain volume conservation: $\frac{|\sum_c V_c - V_{\text{analytic}}|}{V_{\text{analytic}}}$ (or stored vs. recomputed $\sum_c V_c$) | $< 10^{-10}$ rel. | `check_volume_sum` |
| **I-04** | Upper-triangular LDU face ordering: `owner[f] < neighbour[f]` for $f < n_{\text{int}}$, lexicographically sorted by `(owner, neighbour)`, and `neighbour[f] == -1` for $f \ge n_{\text{int}}$ | exact | `check_face_order` |
| **I-05** | Stored geometry parity: recomputed $(\mathbf{S}_f, \mathbf{x}_f, V_c, \mathbf{x}_c)$ matches stored arrays | $< 10^{-11}$ rel. | `check_stored_geometry` |
| **I-06** | Over-relaxed non-orthogonal vector identity: $\|\boldsymbol{\Delta}_f + \mathbf{k}_f - \mathbf{S}_f\|_2 / \|\mathbf{S}_f\|_2$ on internal faces | $< 10^{-12}$ | `check_decomposition` |
| **I-07** | Contiguous & partition-complete boundary patches: `[start_face, start_face + num_faces)` partitions `[n_internal, n_faces)` with zero gaps, overlaps, or empty-string fallbacks | exact | `check_patch_ranges` |
| **I-08** | Cell-local edge manifoldness: every directed edge $(u \to v)$ of cell $c$'s oriented boundary faces has a unique reverse edge $(v \to u)$ (zero T-junctions / hanging nodes) | exact | `check_edge_manifold` |
| **I-09** | CHT Twin-Face conservation: $\mathbf{S}_f(a \to b) = -\mathbf{S}_f(b \to a)$, equal face counts, and $\mathbf{x}_f(a) = \mathbf{x}_f(b)$ | $< 10^{-12}$ | `check_twin_faces` |
| **I-10** | Thread determinism: `mesh_fingerprint(mesh)` is identical for `OMP_NUM_THREADS=1` and multi-threaded execution | bit-identical | `check_thread_determinism` |
| **I-11** | Linear gradient / Gauss moment tensor identity: $\sum_{f \in \partial c} \sigma_{cf}\,\mathbf{x}_f \otimes \mathbf{S}_f = V_c\,\mathbf{I}_3$ on planar-faced cells | $< 10^{-10}$ rel. | `check_moment_identity` |
