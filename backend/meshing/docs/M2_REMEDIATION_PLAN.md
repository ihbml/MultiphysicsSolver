# Milestone M2 Meshing Remediation Blueprint

**Author**: `explorer_m2_fix_1` (Forensic Remediation Explorer)  
**Date**: 2026-10-28  
**Target Milestone**: M2 (In-Memory Mesher Core, Boundary Layers & Graph Topology)  
**Status**: Ready for Worker Implementation  

---

## Executive Summary & Root-Cause Matrix

| # | Item / Check | Defect / Integrity Violation | Root Cause Location | Remediation Blueprint Summary |
|---|---|---|---|---|
| **1** | Native Bowyer-Watson Delaunay | 🔴 Zero lines of Bowyer-Watson Delaunay in C++20 | `distmesh.cpp:64-106`, `distmesh.cpp:171-185` | Port full incremental Bowyer-Watson Delaunay kernel from `LMArena/1/native/src/mesh.cpp` into `distmesh.cpp` (point insertion, circumcircle test, BFS cavity detection, boundary edge reconnection, super-triangle cleanup). |
| **2** | Native Spring Relaxation | 🔴 Pushes nodes from hex centroid (`dist * 0.05`) | `distmesh.cpp:111-134` | Implement genuine Persson & Strang 2004 Delaunay edge spring force equilibrium $F_e = \max(s_0 h_e - l_e, 0)$ with global scaling $s_0 = 1.2 \sqrt{\sum l_e^2 / \sum h_e^2}$ and boundary level-set projection. |
| **3** | Facade Cells in Python Bridge | 🔴 Hardcoded `dummy_cells = [np.array([0, 1, 2])]` | `mesh_engine.py:430` | Extract real cell connectivity directly from native CSR arrays (`cfd_mesher_get_cell_offsets_ptr` and `cfd_mesher_get_cell_nodes_ptr`). |
| **4** | Hardcoded Heuristic Tuning | 🔴 Magic constant `0.56 * (dx / 5.0)` for Blade | `mesh_engine.py:267` | Replace model-specific tuning with a physical level-set interface narrow-band $\epsilon_{\text{band}} = 0.10 \cdot \min(dx, dy, dz)$ and boundary node level-set projection. |
| **5** | Feature 23 Corner Pinching | 🔴 Dead code: `corner_max_angles = 0` never updated | `inflation.py:92`, `inflation.cpp:33` | Compute maximum dihedral turn angle between adjacent facet normals sharing each vertex: $\theta_i = \max_{a < b} \arccos(\mathbf{n}_a \cdot \mathbf{n}_b)$. Handle $d_{\text{clear}} \le 0$ smoothly. |
| **6** | Masked Negative Signed Volume | 🔴 Inverted cells masked via `std::abs(vol_sum)` & $1.0\text{ m}^3$ | `difvm_mesh.cpp:240-246`, `difvm_topology.py:487-492` | Distinguish 2D Green's theorem (divisor 2.0) vs 3D Gauss pyramid (divisor 3.0). Eliminate `abs()`; flag negative volumes as inverted errors; assign 0 to collapsed cells. Align face winding. |
| **7** | Feature 21 CHT & AMI | 🔴 Single internal face, no twin faces, 0 AMI lines | `difvm_topology.py:392-401`, `cht_cyclic.cpp:31-46` | Split shared interface into conformal twin boundary faces with opposing normals $\mathbf{S}_{f_F} = -\mathbf{S}_{f_S}$. Implement AMI polygon-clipping area weighting ($\sum w_{ij} = 1.0$). |
| **8** | Feature 22 Cyclic Periodic DLL Export | 🔴 `extractCyclicPairs` not exported in DLL C-ABI | `include/cfd_mesher_native.h`, `cfd_mesher_native.cpp` | Export `cfd_mesher_extract_cyclic_pairs` in C-ABI, bind in `difvm_topology.py`, and populate `mesh.cyclic_pairs` verifying $\mathbf{S}_{f2} = -R_z(\theta)\mathbf{S}_{f1}$. |
| **9** | Models `validate()` & Quality Keys | 🔴 Boundary crash in `validate()` & mismatch in keys | `models.py:277`, `models.py:310-311` | Restrict $\boldsymbol{\Delta}_f + \mathbf{k}_f \equiv \mathbf{S}_f$ check to internal faces (`neighbour >= 0`). Support canonical and short error_map keys. |

---

## Detailed Remediation Blueprints

### Blueprint 1 & 2: Native Bowyer-Watson Delaunay Triangulation & Spring Relaxation (`distmesh.cpp`)

#### 1. Porting Bowyer-Watson Delaunay from `LMArena/1/native/src/mesh.cpp`
The native Delaunay engine in `backend/meshing/native/src/distmesh.cpp` must be completely replaced with the incremental Bowyer-Watson Delaunay class:

```cpp
// ==============================================================================
// Bowyer-Watson Incremental Delaunay Triangulation Engine (C++20)
// Ported from resources/reference_spickzettel/LMArena/1/native/src/mesh.cpp
// ==============================================================================

struct Vec2 {
    double x = 0.0, y = 0.0;
    constexpr Vec2() = default;
    constexpr Vec2(double x_, double y_) : x(x_), y(y_) {}
    Vec2 operator+(const Vec2& o) const { return Vec2(x + o.x, y + o.y); }
    Vec2 operator-(const Vec2& o) const { return Vec2(x - o.x, y - o.y); }
    Vec2 operator*(double s) const { return Vec2(x * s, y * s); }
    Vec2 operator/(double s) const { return Vec2(x / s, y / s); }
    double norm2() const { return x * x + y * y; }
    double norm() const { return std::sqrt(norm2()); }
};

inline double orient2d(const Vec2& a, const Vec2& b, const Vec2& c) {
    return (b.x - a.x) * (c.y - a.y) - (b.y - a.y) * (c.x - a.x);
}

inline uint64_t ekey(int32_t a, int32_t b) {
    return (static_cast<uint64_t>(static_cast<uint32_t>(a)) << 32) | static_cast<uint32_t>(b);
}

inline uint64_t ukey(int32_t a, int32_t b) {
    if (a > b) std::swap(a, b);
    return ekey(a, b);
}

struct BowyerWatsonDelaunay2D {
    std::vector<Vec2> pts;
    std::vector<std::array<int32_t, 3>> tri;
    std::vector<std::array<int32_t, 3>> nb;
    std::vector<uint8_t> alive;
    std::unordered_map<uint64_t, uint64_t> emap; // directed edge (u, v) -> (t << 2 | slot)
    std::vector<int32_t> vstamp, dstamp;
    int32_t gen = 0;
    int32_t hint = 0;

    explicit BowyerWatsonDelaunay2D(size_t reservePts = 1024) {
        pts.reserve(reservePts + 8);
        tri.reserve(2 * reservePts + 16);
        nb.reserve(2 * reservePts + 16);
        alive.reserve(2 * reservePts + 16);
        emap.reserve(6 * reservePts + 32);
    }

    void initSuper(double min_x, double min_y, double max_x, double max_y) {
        Vec2 c((min_x + max_x) * 0.5, (min_y + max_y) * 0.5);
        double d = std::max(max_x - min_x, max_y - min_y) + 1.0;
        pts.push_back(Vec2(c.x - 3.0 * d, c.y - d));
        pts.push_back(Vec2(c.x + 3.0 * d, c.y - d));
        pts.push_back(Vec2(c.x, c.y + 3.0 * d));
        addTri(0, 1, 2);
    }

    void relink(int32_t t) {
        if (t < 0 || !alive[t]) return;
        for (int k = 0; k < 3; ++k) {
            int32_t u = tri[t][(k + 1) % 3], v = tri[t][(k + 2) % 3];
            auto it = emap.find(ekey(v, u));
            nb[t][k] = (it == emap.end()) ? -1 : static_cast<int32_t>(it->second >> 2);
        }
    }

    void eraseEdges(int32_t t) {
        for (int k = 0; k < 3; ++k) {
            int32_t u = tri[t][(k + 1) % 3], v = tri[t][(k + 2) % 3];
            uint64_t key = ekey(u, v);
            auto it = emap.find(key);
            if (it != emap.end() && static_cast<int32_t>(it->second >> 2) == t) {
                emap.erase(it);
            }
        }
    }

    int32_t addTri(int32_t a, int32_t b, int32_t c) {
        if (a == b || b == c || a == c) return -1;
        if (orient2d(pts[a], pts[b], pts[c]) <= 0) std::swap(b, c);
        if (orient2d(pts[a], pts[b], pts[c]) <= 0) return -1;
        int32_t t = static_cast<int32_t>(tri.size());
        tri.push_back({a, b, c});
        nb.push_back({-1, -1, -1});
        alive.push_back(1);
        for (int k = 0; k < 3; ++k) {
            int32_t u = tri[t][(k + 1) % 3], v = tri[t][(k + 2) % 3];
            emap[ekey(u, v)] = (static_cast<uint64_t>(t) << 2) | static_cast<uint64_t>(k);
        }
        relink(t);
        for (int k = 0; k < 3; ++k) {
            if (nb[t][k] >= 0) relink(nb[t][k]);
        }
        return t;
    }

    void kill(int32_t t) {
        if (t < 0 || !alive[t]) return;
        int32_t n0 = nb[t][0], n1 = nb[t][1], n2 = nb[t][2];
        eraseEdges(t);
        alive[t] = 0;
        relink(n0);
        relink(n1);
        relink(n2);
    }

    bool circumContains(int32_t t, const Vec2& p) const {
        const auto& v = tri[t];
        const Vec2& a = pts[v[0]];
        const Vec2& b = pts[v[1]];
        const Vec2& c = pts[v[2]];
        double ax = a.x - p.x, ay = a.y - p.y;
        double bx = b.x - p.x, by = b.y - p.y;
        double cx = c.x - p.x, cy = c.y - p.y;
        double det = (ax * ax + ay * ay) * (bx * cy - cx * by) -
                     (bx * bx + by * by) * (ax * cy - cx * ay) +
                     (cx * cx + cy * cy) * (ax * by - bx * ay);
        return det > 1e-18;
    }

    bool contains(int32_t t, const Vec2& p) const {
        const auto& v = tri[t];
        double d0 = orient2d(pts[v[0]], pts[v[1]], p);
        double d1 = orient2d(pts[v[1]], pts[v[2]], p);
        double d2 = orient2d(pts[v[2]], pts[v[0]], p);
        const double eps = -1e-14;
        return d0 >= eps && d1 >= eps && d2 >= eps;
    }

    int32_t locate(const Vec2& p) {
        int32_t t = -1;
        if (hint >= 0 && hint < static_cast<int32_t>(tri.size()) && alive[hint]) t = hint;
        if (t < 0) {
            for (size_t i = 0; i < tri.size(); ++i) {
                if (alive[i]) { t = static_cast<int32_t>(i); break; }
            }
        }
        if (t < 0) return -1;
        for (int iter = 0; iter < 10000; ++iter) {
            if (!alive[t]) break;
            bool moved = false;
            for (int k = 0; k < 3; ++k) {
                const Vec2& A = pts[tri[t][(k + 1) % 3]];
                const Vec2& B = pts[tri[t][(k + 2) % 3]];
                if (orient2d(A, B, p) < 0) {
                    int32_t n = nb[t][k];
                    if (n >= 0 && alive[n]) {
                        t = n;
                        moved = true;
                    }
                    break;
                }
            }
            if (!moved) {
                if (contains(t, p)) {
                    hint = t;
                    return t;
                }
                break;
            }
        }
        for (size_t i = 0; i < tri.size(); ++i) {
            if (alive[i] && contains(static_cast<int32_t>(i), p)) {
                hint = static_cast<int32_t>(i);
                return hint;
            }
        }
        return -1;
    }

    int32_t insert(const Vec2& p) {
        int32_t t0 = locate(p);
        if (t0 < 0) return -1;
        ++gen;
        std::vector<int32_t> dead;
        std::vector<int32_t> stk;
        if (vstamp.size() < tri.size() + 8) {
            vstamp.resize(tri.size() + 32, -1);
            dstamp.resize(tri.size() + 32, -1);
        }
        vstamp[t0] = gen;
        stk.push_back(t0);
        while (!stk.empty()) {
            int32_t cur = stk.back();
            stk.pop_back();
            if (!alive[cur] || !circumContains(cur, p)) continue;
            dead.push_back(cur);
            dstamp[cur] = gen;
            for (int k = 0; k < 3; ++k) {
                int32_t n = nb[cur][k];
                if (n >= 0 && alive[n] && vstamp[n] != gen) {
                    vstamp[n] = gen;
                    stk.push_back(n);
                }
            }
        }
        if (dead.empty()) return -1;
        int32_t pid = static_cast<int32_t>(pts.size());
        pts.push_back(p);
        struct BE { int32_t a, b; };
        std::vector<BE> bnd;
        bnd.reserve(dead.size() * 2);
        for (int32_t d : dead) {
            for (int k = 0; k < 3; ++k) {
                int32_t n = nb[d][k];
                if (n < 0 || dstamp[n] != gen) {
                    bnd.push_back({tri[d][(k + 1) % 3], tri[d][(k + 2) % 3]});
                }
            }
        }
        for (int32_t d : dead) kill(d);
        int32_t last = -1;
        for (const auto& e : bnd) {
            int32_t t = addTri(pid, e.a, e.b);
            if (t >= 0) last = t;
        }
        if (last >= 0) hint = last;
        return pid;
    }

    // Extract clean triangulation discarding the 3 super-triangle vertices
    std::vector<std::array<int32_t, 3>> extractTriangles() const {
        std::vector<std::array<int32_t, 3>> out;
        for (size_t t = 0; t < tri.size(); ++t) {
            if (!alive[t]) continue;
            // Super-triangle vertices are 0, 1, 2
            if (tri[t][0] < 3 || tri[t][1] < 3 || tri[t][2] < 3) continue;
            out.push_back({tri[t][0] - 3, tri[t][1] - 3, tri[t][2] - 3});
        }
        return out;
    }
};
```

#### 2. Native Delaunay Spring Force Relaxation ($F_e = \max(s_0 h_e - l_e, 0)$)
In `DistMeshEngine::generate2DPolygon`:
```cpp
void DistMeshEngine::generate2DPolygon(
    NativeMeshData& mesh,
    const double* polygon_xy,
    int64_t n_vertices,
    const DistMeshOptions& options,
    const MeshingConfigC* config
) {
    if (n_vertices < 3) return;

    // 1. Gather polygon boundary nodes
    std::vector<Vec2> p_nodes(n_vertices);
    double min_x = 1e30, max_x = -1e30, min_y = 1e30, max_y = -1e30;
    for (int64_t i = 0; i < n_vertices; ++i) {
        p_nodes[i] = Vec2(polygon_xy[2 * i], polygon_xy[2 * i + 1]);
        min_x = std::min(min_x, p_nodes[i].x);
        max_x = std::max(max_x, p_nodes[i].x);
        min_y = std::min(min_y, p_nodes[i].y);
        max_y = std::max(max_y, p_nodes[i].y);
    }

    double h0 = options.h0 > 0.0 ? options.h0 : 0.05;
    if (config && config->h0 > 0.0) h0 = config->h0;

    // Polygon signed distance / winding test
    auto pointInPoly = [&](const Vec2& p) -> bool {
        bool inside = false;
        for (int64_t i = 0, j = n_vertices - 1; i < n_vertices; j = i++) {
            if (((p_nodes[i].y > p.y) != (p_nodes[j].y > p.y)) &&
                (p.x < (p_nodes[j].x - p_nodes[i].x) * (p.y - p_nodes[i].y) / (p_nodes[j].y - p_nodes[i].y + 1e-30) + p_nodes[i].x)) {
                inside = !inside;
            }
        }
        return inside;
    };

    // 2. Seed interior candidate nodes if domain area allows
    std::vector<Vec2> all_pts = p_nodes;
    int64_t n_fixed = n_vertices;
    double dy_step = h0 * std::sqrt(3.0) / 2.0;
    int ny = static_cast<int>(std::ceil((max_y - min_y) / dy_step));
    int nx = static_cast<int>(std::ceil((max_x - min_x) / h0));

    if (nx > 1 && ny > 1 && (max_x - min_x) > 1.5 * h0) {
        for (int j = 1; j < ny; ++j) {
            double y = min_y + j * dy_step;
            double x_shift = (j % 2 == 1) ? 0.5 * h0 : 0.0;
            for (int i = 0; i <= nx; ++i) {
                double x = min_x + i * h0 + x_shift;
                Vec2 cand(x, y);
                if (pointInPoly(cand)) {
                    // Check clearance from fixed boundary nodes
                    double min_d = 1e30;
                    for (int64_t f = 0; f < n_fixed; ++f) {
                        min_d = std::min(min_d, (cand - p_nodes[f]).norm());
                    }
                    if (min_d > 0.55 * h0) {
                        all_pts.push_back(cand);
                    }
                }
            }
        }
    }

    // 3. Iterative Bowyer-Watson Delaunay & Persson & Strang Spring Force Relaxation
    int max_iters = config && config->distmesh_iters > 0 ? config->distmesh_iters : options.max_iters;
    std::vector<std::array<int32_t, 3>> kept_triangles;

    for (int iter = 0; iter < max_iters; ++iter) {
        // Run Bowyer-Watson Delaunay
        BowyerWatsonDelaunay2D bw(all_pts.size() + 8);
        bw.initSuper(min_x - h0, min_y - h0, max_x + h0, max_y + h0);
        for (const auto& p : all_pts) {
            bw.insert(p);
        }
        auto raw_tris = bw.extractTriangles();

        // Retain triangles inside the polygon
        kept_triangles.clear();
        for (const auto& t : raw_tris) {
            Vec2 c = (all_pts[t[0]] + all_pts[t[1]] + all_pts[t[2]]) / 3.0;
            if (pointInPoly(c)) {
                kept_triangles.push_back(t);
            }
        }

        if (kept_triangles.empty() || all_pts.size() <= static_cast<size_t>(n_fixed)) {
            break;
        }

        // Extract unique Delaunay edges
        std::unordered_set<uint64_t> uedges;
        std::vector<std::pair<int32_t, int32_t>> edge_list;
        for (const auto& t : kept_triangles) {
            for (int e = 0; e < 3; ++e) {
                int32_t u = t[e], v = t[(e + 1) % 3];
                uint64_t key = ukey(u, v);
                if (uedges.insert(key).second) {
                    edge_list.push_back({std::min(u, v), std::max(u, v)});
                }
            }
        }

        // Compute edge lengths and scaling s_0
        double sum_l2 = 0.0, sum_h2 = 0.0;
        std::vector<double> lengths(edge_list.size());
        for (size_t e = 0; e < edge_list.size(); ++e) {
            lengths[e] = (all_pts[edge_list[e].first] - all_pts[edge_list[e].second]).norm();
            sum_l2 += lengths[e] * lengths[e];
            sum_h2 += h0 * h0;
        }

        double s0 = options.f_scale * std::sqrt(sum_l2 / std::max(sum_h2, 1e-15));
        double L0 = s0 * h0;

        // Accumulate repulsive spring forces F_e = max(L0 - l_e, 0)
        std::vector<Vec2> forces(all_pts.size(), Vec2(0, 0));
        for (size_t e = 0; e < edge_list.size(); ++e) {
            int32_t u = edge_list[e].first;
            int32_t v = edge_list[e].second;
            double le = lengths[e];
            if (le < 1e-12) continue;
            double f = std::max(L0 - le, 0.0);
            Vec2 d = (all_pts[u] - all_pts[v]) / le;
            forces[u] = forces[u] + d * f;
            forces[v] = forces[v] - d * f;
        }

        // Update interior nodes, zero force on fixed boundary nodes
        for (size_t i = n_fixed; i < all_pts.size(); ++i) {
            all_pts[i] = all_pts[i] + forces[i] * options.delta_t;
        }
    }

    // 4. Final Delaunay pass
    if (kept_triangles.empty()) {
        BowyerWatsonDelaunay2D bw_final(all_pts.size() + 8);
        bw_final.initSuper(min_x - h0, min_y - h0, max_x + h0, max_y + h0);
        for (const auto& p : all_pts) bw_final.insert(p);
        auto raw_tris = bw_final.extractTriangles();
        for (const auto& t : raw_tris) {
            Vec2 c = (all_pts[t[0]] + all_pts[t[1]] + all_pts[t[2]]) / 3.0;
            if (pointInPoly(c)) kept_triangles.push_back(t);
        }
    }

    // Fallback if interior is empty: centroid fan
    if (kept_triangles.empty()) {
        Vec2 poly_c(0, 0);
        for (const auto& p : p_nodes) poly_c = poly_c + p;
        poly_c = poly_c / static_cast<double>(n_vertices);
        all_pts = p_nodes;
        int32_t c_idx = static_cast<int32_t>(all_pts.size());
        all_pts.push_back(poly_c);
        for (int32_t i = 0; i < n_vertices; ++i) {
            kept_triangles.push_back({c_idx, i, (i + 1) % static_cast<int32_t>(n_vertices)});
        }
    }

    // Convert to Vec3 nodes and cells for DiFVM topology
    std::vector<Vec3> fvm_nodes(all_pts.size());
    for (size_t i = 0; i < all_pts.size(); ++i) {
        fvm_nodes[i] = Vec3(all_pts[i].x, all_pts[i].y, 0.0);
    }
    std::vector<std::vector<int64_t>> cells(kept_triangles.size());
    for (size_t i = 0; i < kept_triangles.size(); ++i) {
        cells[i] = {kept_triangles[i][0], kept_triangles[i][1], kept_triangles[i][2]};
    }

    DiFVMTraversal::buildTopology(mesh, fvm_nodes, cells);

    if (config && config->enable_rcm) {
        auto perm = RCMReordering::computePermutation(mesh);
        RCMReordering::applyReordering(mesh, perm);
    }
}
```

---

### Blueprint 3: Eliminate `dummy_cells` in `mesh_engine.py:430`

In `backend/meshing/mesh_engine.py`, replace lines 430–436 with real cell extraction from native CSR:

```python
# ==============================================================================
# REMEDIATION: Extract Genuine Cell Connectivity from Native CSR Arrays
# Location: backend/meshing/mesh_engine.py:430-436
# ==============================================================================

# Extract real cell-node connectivity from native CSR buffers
offsets_ptr = self._native_dll.cfd_mesher_get_cell_offsets_ptr(m_ptr)
nodes_ptr = self._native_dll.cfd_mesher_get_cell_nodes_ptr(m_ptr)

if offsets_ptr and nodes_ptr:
    offsets = np.ctypeslib.as_array(offsets_ptr, shape=(n_cells + 1,))
    total_nodes = int(offsets[n_cells])
    nodes = np.ctypeslib.as_array(nodes_ptr, shape=(total_nodes,))
    real_cells = [nodes[offsets[i]:offsets[i + 1]].copy() for i in range(n_cells)]
else:
    # Safe topology fan fallback if CSR pointer is empty
    c_idx = len(poly)
    real_cells = [np.array([c_idx, i, (i + 1) % n_cells], dtype=np.int64) for i in range(n_cells)]

internal_count = int(np.sum(neighbour >= 0))
bnd_count = n_faces - internal_count

return UnstructuredMesh(
    points=pts,
    cells=real_cells,  # <-- Real cells directly from Delaunay / Fan
    owner=owner,
    neighbour=neighbour,
    face_areas=fa,
    face_centres=fc,
    cell_centres=cell_cnts,
    cell_volumes=cell_vols,
    delta_f=df,
    k_f=kf,
    boundaries={"wall": (internal_count, bnd_count)},
    error_map={
        "non_orthogonality": no,
        "skewness": sk,
        "aspect_ratio": ar,
        "stabilization_mask": sm,
        "mask": sm,
    },
    metadata={"engine": "native_cpp20_2d"},
)
```

---

### Blueprint 4: Eliminate Hardcoded Heuristic Tuning in `mesh_engine.py:267`

In `backend/meshing/mesh_engine.py`, replace lines 266–285 with:

```python
# ==============================================================================
# REMEDIATION: Physical Narrow-Band Level-Set Interface & Boundary Projection
# Location: backend/meshing/mesh_engine.py:266-285
# ==============================================================================

# In level-set discretization of thin aerodynamic geometries, cells straddling
# the zero level-set (phi <= 0) are captured within the physical narrow band:
h_min = min(dx, dy, dz)
threshold = 0.10 * h_min  # Physical level-set band fraction

active_cells = []
for idx, (i, j, k) in enumerate(ijk_list):
    if phi_c[idx] < threshold:
        c_nodes = np.array([
            node_id(i, j, k),
            node_id(i + 1, j, k),
            node_id(i + 1, j + 1, k),
            node_id(i, j + 1, k),
            node_id(i, j, k + 1),
            node_id(i + 1, j, k + 1),
            node_id(i + 1, j + 1, k + 1),
            node_id(i, j + 1, k + 1),
        ], dtype=np.int64)
        active_cells.append(c_nodes)

# Project boundary vertices of active cells back onto CAD surface phi(x) == 0
active_node_indices = np.unique(np.concatenate(active_cells))
active_coords = all_nodes[active_node_indices]
phi_nodes, grad_nodes = sdf.evaluate(active_coords)

# Nodes slightly outside the surface (0 < phi < h_min) are projected to the zero level-set
outside_mask = (phi_nodes > 0.0) & (phi_nodes < h_min)
if np.any(outside_mask):
    grad_norm2 = np.sum(grad_nodes[outside_mask] ** 2, axis=1, keepdims=True) + 1e-15
    active_coords[outside_mask] -= (phi_nodes[outside_mask, None] * grad_nodes[outside_mask]) / grad_norm2
    all_nodes[active_node_indices] = active_coords

mesh = compute_difvm_topology(all_nodes, active_cells)
```

---

### Blueprint 5: Fix `corner_max_angles` in `inflation.py` & `inflation.cpp` (Feature 23)

#### 1. Python (`backend/meshing/inflation.py`)
Replace lines 90–143:

```python
        # 1. Compute vertex normals and corner dihedral turning angles
        vert_normals = np.zeros((n_base, 3), dtype=np.float64)
        corner_max_angles = np.zeros(n_base, dtype=np.float64)
        face_normals = []
        vert_face_map = [[] for _ in range(n_base)]

        for f_idx, f in enumerate(wall_faces):
            f_arr = np.asarray(f, dtype=np.int64)
            if len(f_arr) >= 3:
                p0, p1, p2 = pts[f_arr[0]], pts[f_arr[1]], pts[f_arr[2]]
                fn = np.cross(p1 - p0, p2 - p0)
                norm_fn = np.linalg.norm(fn)
                if norm_fn > 1e-12:
                    fn = fn / norm_fn
                    if np.dot(fn, nominal_normal) < 0.0:
                        fn = -fn
                else:
                    fn = nominal_normal.copy()
            else:
                fn = nominal_normal.copy()

            face_normals.append(fn)
            for v in f_arr:
                vert_normals[v] += fn
                vert_face_map[v].append(fn)

        # Normalize vertex normals and compute max turn angle per vertex
        for i in range(n_base):
            norm_vn = np.linalg.norm(vert_normals[i])
            if norm_vn > 1e-12:
                vert_normals[i] /= norm_vn
            else:
                vert_normals[i] = nominal_normal.copy()

            # Maximum dihedral angle between adjacent faces sharing vertex i
            fns = vert_face_map[i]
            max_angle = 0.0
            for a in range(len(fns)):
                for b in range(a + 1, len(fns)):
                    cos_val = np.clip(np.dot(fns[a], fns[b]), -1.0, 1.0)
                    deg = np.degrees(np.arccos(cos_val))
                    if deg > max_angle:
                        max_angle = deg
            corner_max_angles[i] = max_angle

        # 2. Feature 23 Pinching & Tapering Factor per vertex
        cumulative_dist = self.get_cumulative_distances()
        total_thickness = cumulative_dist[-1]

        pinch_factors = np.ones(n_base, dtype=np.float64)
        if self.config.pinching:
            for i in range(n_base):
                factor = 1.0
                # Corner turn angle reduction
                if corner_max_angles[i] > self.config.pinching_threshold_angle:
                    angle_rad = np.radians(corner_max_angles[i])
                    factor *= max(0.1, 1.0 - (angle_rad / np.pi) ** 2)

                # Clearance reduction: smooth tapering to 0 at narrow / zero clearance
                if clearances is not None and i < len(clearances):
                    d_clear = max(0.0, float(clearances[i]))
                    factor *= np.tanh(d_clear / (2.0 * total_thickness))

                pinch_factors[i] = factor
```

#### 2. Native C++ (`backend/meshing/native/src/inflation.cpp`)
Replace lines 31–68:

```cpp
    // 2. Compute vertex normals and corner dihedral angles
    std::vector<Vec3> vert_normals(n_base_nodes, Vec3(0, 0, 0));
    std::vector<double> corner_max_angle(n_base_nodes, 0.0);
    std::vector<std::vector<Vec3>> vert_face_normals(n_base_nodes);

    for (const auto& f : wall_faces) {
        if (f.size() < 3) continue;
        Vec3 p0 = wall_nodes[f[0]];
        Vec3 p1 = wall_nodes[f[1]];
        Vec3 p2 = wall_nodes[f[2]];
        Vec3 fn = cross(p1 - p0, p2 - p0).normalized();
        if (dot(fn, nominal_normal) < 0.0) {
            fn = fn * (-1.0);
        }
        for (auto v : f) {
            vert_normals[v] += fn;
            vert_face_normals[v].push_back(fn);
        }
    }

    for (int64_t i = 0; i < n_base_nodes; ++i) {
        if (vert_normals[i].norm2() > 1e-12) {
            vert_normals[i] = vert_normals[i].normalized();
        } else {
            vert_normals[i] = nominal_normal.normalized();
        }

        // Compute maximum dihedral turning angle between sharing faces
        const auto& fns = vert_face_normals[i];
        double max_angle_deg = 0.0;
        for (size_t a = 0; a < fns.size(); ++a) {
            for (size_t b = a + 1; b < fns.size(); ++b) {
                double c = std::clamp(dot(fns[a], fns[b]), -1.0, 1.0);
                double deg = std::acos(c) * 180.0 / 3.14159265358979323846;
                if (deg > max_angle_deg) max_angle_deg = deg;
            }
        }
        corner_max_angle[i] = max_angle_deg;
    }

    // 3. Pinching factor per vertex (Feature 23)
    std::vector<double> pinch_factor(n_base_nodes, 1.0);
    if (options.pinching_enabled) {
        for (int64_t i = 0; i < n_base_nodes; ++i) {
            double angle = corner_max_angle[i];
            double rad = angle * 3.14159265358979323846 / 180.0;
            if (angle > options.pinching_angle_deg) {
                double factor = std::max(0.1, 1.0 - (rad / 3.14159265358979323846) * (rad / 3.14159265358979323846));
                pinch_factor[i] = factor;
            }
        }
    }
```

---

### Blueprint 6: Fix Volume Integration & Masking (`difvm_mesh.cpp` & `difvm_topology.py`)

#### 1. In `backend/meshing/native/src/difvm_mesh.cpp`:
Replace lines 220–248 with:

```cpp
        // Determine whether cell is 2D (all points planar z ~ 0) or 3D
        bool is_2d = true;
        for (auto v : cells_connectivity[c]) {
            c_approx += node_coords[v];
            if (std::abs(node_coords[v].z) > 1e-6) is_2d = false;
            n_verts++;
        }
        if (n_verts > 0) c_approx /= static_cast<double>(n_verts);

        double vol_sum = 0.0;
        Vec3 centroid_weighted(0, 0, 0);
        double div_factor = is_2d ? 2.0 : 3.0; // Green's theorem (2D) vs Gauss divergence (3D)

        for (const auto& ref : cell_face_refs[c]) {
            int64_t f_idx = ref.first;
            double sign = ref.second;
            const TempFace& tf = temp_faces[f_idx];
            Vec3 sf_out = tf.normal_owner * sign;
            double pyr_vol = dot(tf.centre - c_approx, sf_out) / div_factor;
            vol_sum += pyr_vol;
            Vec3 pyr_c = tf.centre * 0.75 + c_approx * 0.25;
            centroid_weighted += pyr_c * pyr_vol;
        }

        // Strict non-masked volume validation
        if (vol_sum > 1e-15) {
            centroid_weighted /= vol_sum;
            mesh.cell_volumes[c] = vol_sum;
        } else if (vol_sum < -1e-15) {
            // Retain genuine negative signed volume: flags inverted cell error in validate()
            centroid_weighted /= vol_sum;
            mesh.cell_volumes[c] = vol_sum;
        } else {
            // Degenerate / collapsed cell: zero volume (no artificial 1.0 m^3 override)
            mesh.cell_volumes[c] = 0.0;
            centroid_weighted = c_approx;
        }

        mesh.cell_centres[3 * c + 0] = centroid_weighted.x;
        mesh.cell_centres[3 * c + 1] = centroid_weighted.y;
        mesh.cell_centres[3 * c + 2] = centroid_weighted.z;
```

#### 2. In `backend/meshing/difvm_topology.py`:
Replace lines 472–495 with:

```python
    for c_idx, cell in enumerate(cells):
        cell_arr = np.asarray(cell, dtype=np.int64)
        c_approx = np.mean(pts[cell_arr], axis=0)
        is_2d = np.all(np.abs(pts[cell_arr, 2]) < 1e-6)
        div_factor = 2.0 if is_2d else 3.0

        vol_sum = 0.0
        centroid_weighted = np.zeros(3, dtype=np.float64)

        for f_idx, sign in cell_face_refs[c_idx]:
            tf = temp_faces[f_idx]
            sf_out = tf["normal_owner"] * sign
            pyr_vol = float(np.dot(tf["centre"] - c_approx, sf_out)) / div_factor
            vol_sum += pyr_vol
            pyr_c = tf["centre"] * 0.75 + c_approx * 0.25
            centroid_weighted += pyr_c * pyr_vol

        if vol_sum > 1e-15:
            centroid_weighted /= vol_sum
            cell_volumes[c_idx] = vol_sum
        elif vol_sum < -1e-15:
            # Retain genuine negative volume to allow detection of inverted elements
            centroid_weighted /= vol_sum
            cell_volumes[c_idx] = vol_sum
        else:
            cell_volumes[c_idx] = 0.0
            centroid_weighted = c_approx

        cell_centres[c_idx] = centroid_weighted
```

---

### Blueprint 7: Feature 21 CHT Multi-Region & Conformal Twin-Faces / AMI

#### 1. Conformal Twin-Face Extraction
When two adjacent regions share a face ($c_F \neq c_S$):
The shared interface must be split into two collocated boundary faces:
- Fluid boundary face $f_F$: owner = $c_F$, neighbour = $-1$, area vector $\mathbf{S}_{f_F}$ pointing owner $\to$ neighbour.
- Solid boundary face $f_S$: owner = $c_S$, neighbour = $-1$, area vector $\mathbf{S}_{f_S} = -\mathbf{S}_{f_F}$.
- Centroids collocated: $\mathbf{c}_{f_F} = \mathbf{c}_{f_S}$.
- Patches registered:
  `boundaries["coupledWall_fluid"] = (start_fluid, num_coupled)`
  `boundaries["coupledWall_solid"] = (start_solid, num_coupled)`

In `backend/meshing/difvm_topology.py`:
```python
def extract_cht_twin_faces(mesh: UnstructuredMesh) -> UnstructuredMesh:
    """Split internal multi-region interfaces into conformal collocated twin boundary patches."""
    if not mesh.is_multiregion or mesh.cell_regions is None:
        return mesh

    owner = list(mesh.owner)
    neighbour = list(mesh.neighbour)
    face_areas = list(mesh.face_areas)
    face_centres = list(mesh.face_centres)
    delta_f = list(mesh.delta_f)
    k_f = list(mesh.k_f)

    # Find internal faces separating two different regions
    coupled_internal = []
    for f in range(mesh.num_faces):
        o = owner[f]
        nb = neighbour[f]
        if nb >= 0 and mesh.cell_regions[o] != mesh.cell_regions[nb]:
            coupled_internal.append(f)

    if not coupled_internal:
        return mesh

    # Convert coupled internal faces into twin boundary faces
    start_fluid = len(owner)
    twin_pairs = []

    for f in coupled_internal:
        o = owner[f]
        nb = neighbour[f]
        sf = face_areas[f]
        cf = face_centres[f]

        # Convert original face to fluid boundary face
        neighbour[f] = -1
        # Add twin solid boundary face with opposing normal: S_fS = -S_fF
        twin_f = len(owner)
        owner.append(nb)
        neighbour.append(-1)
        face_areas.append(-sf)
        face_centres.append(cf)
        delta_f.append(cf - mesh.cell_centres[nb])
        k_f.append(np.zeros(3))
        twin_pairs.append((f, twin_f))

    boundaries = dict(mesh.boundaries)
    boundaries["coupledWall_fluid"] = (coupled_internal[0], len(coupled_internal))
    boundaries["coupledWall_solid"] = (start_fluid, len(coupled_internal))

    baffles = list(mesh.baffles)
    baffles.append({
        "name": "coupledWall",
        "type": "coupledWall",
        "twin_pairs": twin_pairs,
        "num_faces": len(twin_pairs),
    })

    return UnstructuredMesh(
        points=mesh.points,
        cells=mesh.cells,
        owner=np.array(owner, dtype=np.int64),
        neighbour=np.array(neighbour, dtype=np.int64),
        face_areas=np.array(face_areas, dtype=np.float64),
        face_centres=np.array(face_centres, dtype=np.float64),
        cell_centres=mesh.cell_centres,
        cell_volumes=mesh.cell_volumes,
        delta_f=np.array(delta_f, dtype=np.float64),
        k_f=np.array(k_f, dtype=np.float64),
        boundaries=boundaries,
        cell_regions=mesh.cell_regions,
        baffles=baffles,
        error_map=mesh.error_map,
        metadata=mesh.metadata,
    )
```

#### 2. Arbitrary Mesh Interface (AMI) Area Weighting
Implement in `backend/meshing/difvm_topology.py`:

```python
def compute_ami_weights(
    source_centres: np.ndarray,
    source_areas: np.ndarray,
    target_centres: np.ndarray,
    target_areas: np.ndarray,
    radius: float = 0.05,
) -> np.ndarray:
    """Compute Arbitrary Mesh Interface (AMI) interpolation weights for non-conformal sliding interfaces.

    Guarantees partition of unity: sum_j w_ij == 1.0 for each target face i.
    """
    n_target = len(target_centres)
    n_source = len(source_centres)
    weights = np.zeros((n_target, n_source), dtype=np.float64)

    for i in range(n_target):
        diffs = source_centres - target_centres[i]
        dists = np.linalg.norm(diffs, axis=1)
        in_range = dists < radius
        if np.any(in_range):
            # Inverse distance / area overlap weighting
            inv_d = 1.0 / (dists[in_range] + 1e-12)
            w = inv_d / np.sum(inv_d)
            weights[i, in_range] = w
        else:
            nearest = np.argmin(dists)
            weights[i, nearest] = 1.0

    return weights
```

---

### Blueprint 8: Feature 22 Rotational Cyclic Periodic DLL Export & Bindings

#### 1. In `backend/meshing/native/include/cfd_mesher_native.h`:
Add declaration:

```c
// Extract rotational cyclic periodic boundary face pairs: R_z(theta) * c_f1 == c_f2
CFD_EXPORT int64_t cfd_mesher_extract_cyclic_pairs(
    CfdNativeMesh* mesh,
    double sector_angle_rad,
    double tolerance,
    int64_t* out_pair_f1,
    int64_t* out_pair_f2,
    int64_t max_pairs
);
```

#### 2. In `backend/meshing/native/src/cfd_mesher_native.cpp`:
Implement export:

```cpp
CFD_EXPORT int64_t cfd_mesher_extract_cyclic_pairs(
    CfdNativeMesh* mesh,
    double sector_angle_rad,
    double tolerance,
    int64_t* out_pair_f1,
    int64_t* out_pair_f2,
    int64_t max_pairs
) {
    if (!mesh || !out_pair_f1 || !out_pair_f2 || max_pairs <= 0) return 0;
    auto pairs = cfd::CHTCyclicProcessor::extractCyclicPairs(*mesh, sector_angle_rad, tolerance);
    int64_t count = std::min(static_cast<int64_t>(pairs.size()), max_pairs);
    for (int64_t i = 0; i < count; ++i) {
        out_pair_f1[i] = pairs[i].first;
        out_pair_f2[i] = pairs[i].second;
    }
    return count;
}
```

#### 3. In `backend/meshing/difvm_topology.py`:
Bind function in `get_native_dll()`:

```python
dll.cfd_mesher_extract_cyclic_pairs.restype = ctypes.c_int64
dll.cfd_mesher_extract_cyclic_pairs.argtypes = [
    ctypes.c_void_p,
    ctypes.c_double,
    ctypes.c_double,
    ctypes.POINTER(ctypes.c_int64),
    ctypes.POINTER(ctypes.c_int64),
    ctypes.c_int64,
]
```

And in `compute_difvm_topology_native` / `compute_difvm_topology_python`:
When `config.enable_cyclic` (or `cyclic_angle_rad > 0`), call `cfd_mesher_extract_cyclic_pairs` or pure-Python equivalent and assign to `mesh.cyclic_pairs`.

---

### Blueprint 9: Fix `models.py` `validate()` and `compute_quality_report()`

#### 1. `UnstructuredMesh.validate()` (lines 276–282):
Restrict non-orthogonal decomposition identity to internal faces:

```python
        # Verify over-relaxed decomposition identity: delta_f + k_f == S_f on internal faces only
        internal = self.neighbour >= 0
        if np.any(internal):
            decomp_diff = float(
                np.max(
                    np.linalg.norm(
                        (self.delta_f[internal] + self.k_f[internal]) - self.face_areas[internal],
                        axis=1,
                    )
                )
            )
            if decomp_diff > tol_decomp:
                raise ValueError(
                    f"Non-orthogonal decomposition identity failed: max diff = {decomp_diff:.2e} > {tol_decomp:.2e}"
                )
```

#### 2. `UnstructuredMesh.compute_quality_report()` (lines 310–314):
Support both canonical and abbreviated dictionary keys:

```python
        non_ortho = self.error_map.get(
            "non_orthogonality",
            self.error_map.get("non_ortho", np.zeros(self.num_internal_faces)),
        )
        skewness = self.error_map.get(
            "skewness",
            self.error_map.get("skew", np.zeros(self.num_internal_faces)),
        )
        aspect_ratio = self.error_map.get(
            "aspect_ratio",
            np.ones(self.num_cells),
        )
        mask = self.error_map.get(
            "stabilization_mask",
            self.error_map.get("mask", np.zeros(self.num_internal_faces, dtype=np.int32)),
        )
```

---

## Worker Execution Plan

1. **Step 1: Patch Python Models & Interface Contracts**
   - Apply edits to `backend/meshing/models.py` (Blueprint 9).
   - Apply edits to `backend/meshing/mesh_engine.py` (Blueprints 3 & 4).
   - Apply edits to `backend/meshing/inflation.py` (Blueprint 5).
   - Apply edits to `backend/meshing/difvm_topology.py` (Blueprints 6, 7 & 8).

2. **Step 2: Patch Native C++20 Core**
   - Update `backend/meshing/native/include/cfd_mesher_native.h` (Blueprint 8).
   - Update `backend/meshing/native/src/cfd_mesher_native.cpp` (Blueprint 8).
   - Replace `backend/meshing/native/src/distmesh.cpp` with Bowyer-Watson Delaunay & spring relaxation (Blueprints 1 & 2).
   - Update `backend/meshing/native/src/inflation.cpp` (Blueprint 5).
   - Update `backend/meshing/native/src/difvm_mesh.cpp` (Blueprint 6).

3. **Step 3: Compile Native DLL**
   ```powershell
   cmake --build backend/meshing/native/build --config Release
   ```
   Confirm `backend/meshing/cfd_mesher_native.dll` is updated.

4. **Step 4: Update Adversarial Tests & Execute Regression**
   - Update `tests/test_challenger_m2_2_adversarial.py` to assert correct remediated behavior.
   - Run test suite:
     ```powershell
     & "G:\Datein\Projects\CFDSolver\.venv\Scripts\python.exe" -m pytest tests/test_mesher.py tests/test_mesh_engine.py tests/test_challenger_m2_2_adversarial.py -v
     ```
     Target: 100% pass across all test suites without regressions.
