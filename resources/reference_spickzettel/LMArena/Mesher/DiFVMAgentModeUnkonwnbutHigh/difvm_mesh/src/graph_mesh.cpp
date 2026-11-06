// DiFVM Mesher — mesh compiler: PolyMeshRaw  ->  GraphMesh + MeshErrorMap.
#include "difvm/graph_mesh.hpp"

#include <array>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <numeric>
#include <unordered_map>

namespace difvm {
namespace {

// ---------------------------------------------------------------------------
// Face geometry primitives (planar / warped polygons)
// ---------------------------------------------------------------------------
struct FaceGeom {
  Vec3 Sf{};         // signed area vector (owner-outward)
  Vec3 xf{};         // area-weighted centroid
  double sum_tri = 0.0;  // sum of |triangle area vectors| (planar pole detection)
};

// Area vector of a polygon:  Sf = 1/2 sum_k (x_k x x_{k+1})
inline Vec3 polygon_area_vector(const Vec3* p, const idx* v, int n) {
  Vec3 s{0, 0, 0};
  Vec3 a = p[v[0]];
  for (int k = 1; k + 1 < n; ++k) {
    const Vec3 b = p[v[k]];
    const Vec3 c = p[v[k + 1]];
    s += cross(b - a, c - a);
  }
  return s * 0.5;
}

// Area-weighted centroid via a triangle fan about vertex 0 (projection-free).
inline FaceGeom face_geometry(const Vec3* p, const idx* v, int n) {
  FaceGeom g;
  const Vec3 a = p[v[0]];
  Vec3 acc{0, 0, 0};
  double wsum = 0.0, tri = 0.0;
  for (int k = 1; k + 1 < n; ++k) {
    const Vec3 b = p[v[k]];
    const Vec3 c = p[v[k + 1]];
    const Vec3 av = cross(b - a, c - a) * 0.5;
    const double w = norm(av);
    acc += (a + b + c) * (w / 3.0);
    wsum += w;
    tri += w;
  }
  g.Sf = polygon_area_vector(p, v, n);
  g.xf = (wsum > 0.0) ? acc / wsum : a;
  g.sum_tri = tri;
  return g;
}

// ---------------------------------------------------------------------------
// Small CSR helper built incrementally (counting sort, no per-row vectors)
// ---------------------------------------------------------------------------
struct CsrBuilder {
  std::vector<gidx> offsets;
  std::vector<idx> items;

  void build(gidx n, const std::vector<std::pair<idx, idx>>& ij) {
    offsets.assign(static_cast<std::size_t>(n) + 1, 0);
    for (const auto& e : ij) offsets[static_cast<std::size_t>(e.first) + 1]++;
    for (gidx i = 0; i < n; ++i) offsets[static_cast<std::size_t>(i) + 1] += offsets[static_cast<std::size_t>(i)];
    items.resize(ij.size());
    std::vector<gidx> cursor(offsets.begin(), offsets.end() - 1);
    for (const auto& e : ij) items[static_cast<std::size_t>(cursor[static_cast<std::size_t>(e.first)]++)] = e.second;
  }
};

// ---------------------------------------------------------------------------
// 64-bit hash of a sorted vertex tuple (face identity for welding)
// ---------------------------------------------------------------------------
inline std::uint64_t hash_verts(const std::vector<idx>& v) {
  std::uint64_t h = 1469598103934665603ull;
  for (idx x : v) {
    h ^= static_cast<std::uint64_t>(static_cast<std::uint32_t>(x)) + 0x9e3779b97f4a7c15ull + (h << 6) + (h >> 2);
    h *= 1099511628211ull;
  }
  return h;
}

struct PointKeyHash {
  std::size_t operator()(const std::array<std::int64_t, 3>& k) const noexcept {
    std::uint64_t h = 1469598103934665603ull;
    for (auto c : k) {
      h ^= static_cast<std::uint64_t>(c);
      h *= 1099511628211ull;
      h ^= h >> 29;
    }
    return static_cast<std::size_t>(h);
  }
};

}  // namespace

// ---------------------------------------------------------------------------
// Memory accounting
// ---------------------------------------------------------------------------
std::size_t GraphMesh::memory_bytes() const {
  std::size_t b = 0;
  auto vecbytes = [&b](const auto& v) { b += v.size() * sizeof(typename std::decay_t<decltype(v)>::value_type); };
  vecbytes(cf.x); vecbytes(cf.y); vecbytes(cf.z);
  vecbytes(vol); vecbytes(cell_quality); vecbytes(omega); vecbytes(vol_ratio);
  vecbytes(cell_skew); vecbytes(cell_theta);
  vecbytes(owner); vecbytes(neighbour); vecbytes(patch_of);
  vecbytes(Sf.x); vecbytes(Sf.y); vecbytes(Sf.z);
  vecbytes(xf.x); vecbytes(xf.y); vecbytes(xf.z);
  vecbytes(d_ij.x); vecbytes(d_ij.y); vecbytes(d_ij.z);
  vecbytes(delta_f.x); vecbytes(delta_f.y); vecbytes(delta_f.z);
  vecbytes(k_f.x); vecbytes(k_f.y); vecbytes(k_f.z);
  vecbytes(k_f_comp.x); vecbytes(k_f_comp.y); vecbytes(k_f_comp.z);
  vecbytes(m_skew.x); vecbytes(m_skew.y); vecbytes(m_skew.z);
  vecbytes(magSf); vecbytes(mag_d); vecbytes(cos_ortho); vecbytes(theta);
  vecbytes(non_ortho_corr); vecbytes(skewness); vecbytes(beta); vecbytes(face_warp);
  vecbytes(fv_offsets); vecbytes(fv); b += points.size() * sizeof(Vec3);
  vecbytes(cf_offsets); vecbytes(cf_faces); vecbytes(nn_offsets); vecbytes(nn_cells); vecbytes(nn_sign);
  return b;
}

void GraphMesh::clear() { *this = GraphMesh{}; }

// Normalised tanh transition: 1 at x=0, 0.5 at x=x0, ->0 for x >> x0.
// Unlike 0.5*(1-tanh(...)) it is *exactly* 1 for a perfect cell, so a good
// mesh is never artificially damped.
inline double tanh_guard(double x, double x0, double w) {
  const double s = std::tanh(std::max(x0, 1e-12) / std::max(w, 1e-12));
  const double v = (std::tanh((x0 - x) / std::max(w, 1e-12)) + s) / (2.0 * s);
  return clampd(v, 0.0, 1.0);
}

// ---------------------------------------------------------------------------
// Compiler
// ---------------------------------------------------------------------------
GraphMesh build_graph_mesh(const PolyMeshRaw& raw_in, const BuildOptions& opts) {
  Timer total;
  raw_in.validate();
  const int nt = opts.threads > 0 ? opts.threads : num_threads();

  GraphMesh gm;
  gm.err_params = opts.err;
  gm.n_cells = raw_in.n_cells;

  // ---------------- 1. points: weld + prune -------------------------------
  std::vector<Vec3> pts(raw_in.points.begin(), raw_in.points.end());
  std::vector<idx> point_map(pts.size());
  std::iota(point_map.begin(), point_map.end(), 0);
  Vec3 lo{}, hi{};
  raw_in.bounds(lo, hi);
  const double diag = norm(hi - lo);
  if (diag <= 0.0) fail("build_graph_mesh: degenerate bounding box");

  if (opts.merge_duplicate_points) {
    const double tol = std::max(opts.merge_tolerance_rel * diag, 1e-300);
    const double inv = 1.0 / tol;
    std::unordered_map<std::array<std::int64_t, 3>, idx, PointKeyHash> table;
    table.reserve(pts.size());
    std::vector<Vec3> unique;
    unique.reserve(pts.size());
    for (std::size_t i = 0; i < pts.size(); ++i) {
      const std::array<std::int64_t, 3> key{
          static_cast<std::int64_t>(std::llround(pts[i].x * inv)),
          static_cast<std::int64_t>(std::llround(pts[i].y * inv)),
          static_cast<std::int64_t>(std::llround(pts[i].z * inv))};
      auto it = table.find(key);
      if (it == table.end()) {
        const idx id = static_cast<idx>(unique.size());
        table.emplace(key, id);
        unique.push_back(pts[i]);
        point_map[i] = id;
      } else {
        point_map[i] = it->second;
      }
    }
    if (unique.size() != pts.size()) {
      log_verbose("welded " + std::to_string(pts.size() - unique.size()) + " duplicate points (tol=" +
                  std::to_string(tol) + ")");
    }
    pts.swap(unique);
  }

  // ---------------- 2. face connectivity: remap + clean -------------------
  const idx nf_in = raw_in.n_faces();
  std::vector<gidx> foff(nf_in + 1, 0);
  std::vector<idx> fverts;
  fverts.reserve(raw_in.face_verts.size());
  for (idx f = 0; f < nf_in; ++f) {
    foff[static_cast<std::size_t>(f)] = static_cast<gidx>(fverts.size());
    const idx nv = raw_in.n_face_verts(f);
    const idx* vin = raw_in.face_begin(f);
    idx prev = -1;
    for (idx k = 0; k < nv; ++k) {
      const idx v = point_map[static_cast<std::size_t>(vin[k])];
      if (v == prev) continue;              // collapse repeated vertices
      fverts.push_back(v);
      prev = v;
    }
    // wraparound duplicate
    while (fverts.size() > static_cast<std::size_t>(foff[static_cast<std::size_t>(f)]) + 2 &&
           fverts.back() == fverts[static_cast<std::size_t>(foff[static_cast<std::size_t>(f)])]) {
      fverts.pop_back();
    }
    if (fverts.size() - static_cast<std::size_t>(foff[static_cast<std::size_t>(f)]) < 3) {
      fail("face " + std::to_string(f) + " degenerates to <3 distinct vertices");
    }
  }
  foff[nf_in] = static_cast<gidx>(fverts.size());

  // ---------------- 3. cell-pair + patch bookkeeping ----------------------
  std::vector<idx> c0(raw_in.owner.begin(), raw_in.owner.end());
  std::vector<idx> c1(static_cast<std::size_t>(nf_in), -1);
  for (gidx f = 0; f < raw_in.n_internal(); ++f) {
    idx a = raw_in.owner[static_cast<std::size_t>(f)];
    idx b = raw_in.neighbour[static_cast<std::size_t>(f)];
    if (b < a) std::swap(a, b);
    c0[static_cast<std::size_t>(f)] = a;
    c1[static_cast<std::size_t>(f)] = b;
  }
  std::vector<idx> fpatch(static_cast<std::size_t>(nf_in), -1);
  std::vector<PatchInfo> patches = raw_in.patches;
  {
    gidx cursor = raw_in.n_internal();
    for (std::size_t p = 0; p < patches.size(); ++p) {
      const gidx beg = cursor;
      const gidx end = beg + patches[p].size;
      if (end > nf_in) fail("patch '" + patches[p].name + "' exceeds face count");
      for (gidx f = beg; f < end; ++f) fpatch[static_cast<std::size_t>(f)] = static_cast<idx>(p);
      cursor = end;
    }
    if (cursor != nf_in && !patches.empty())
      fail("patch faces do not cover the mesh: " + std::to_string(cursor) + "/" + std::to_string(nf_in));
  }

  // ---------------- 4. weld coincident faces ------------------------------
  // Two faces with the same vertex set but opposite owners are the same
  // interface (typical for STL/VTK derived meshes or double-written patches).
  std::vector<idx> keep_of(static_cast<std::size_t>(nf_in));
  std::iota(keep_of.begin(), keep_of.end(), 0);
  std::vector<char> alive(static_cast<std::size_t>(nf_in), 1);
  gidx n_merged_faces = 0;
  gidx n_duplicate_faces = 0;
  if (opts.merge_duplicate_points) {
    std::unordered_map<std::uint64_t, std::vector<idx>> buckets;
    buckets.reserve(static_cast<std::size_t>(nf_in) * 2);
    std::vector<idx> tmp;
    for (idx f = 0; f < nf_in; ++f) {
      tmp.assign(fverts.begin() + foff[static_cast<std::size_t>(f)], fverts.begin() + foff[static_cast<std::size_t>(f) + 1]);
      std::sort(tmp.begin(), tmp.end());
      buckets[hash_verts(tmp)].push_back(f);
    }
    for (auto& kv : buckets) {
      auto& ids = kv.second;
      if (ids.size() < 2) continue;
      for (std::size_t i = 0; i < ids.size(); ++i) {
        const idx fi = ids[i];
        if (!alive[static_cast<std::size_t>(fi)]) continue;
        for (std::size_t j = i + 1; j < ids.size(); ++j) {
          const idx fj = ids[j];
          if (!alive[static_cast<std::size_t>(fj)]) continue;
          tmp.assign(fverts.begin() + foff[static_cast<std::size_t>(fi)], fverts.begin() + foff[static_cast<std::size_t>(fi) + 1]);
          std::vector<idx> tmp2(fverts.begin() + foff[static_cast<std::size_t>(fj)], fverts.begin() + foff[static_cast<std::size_t>(fj) + 1]);
          std::sort(tmp.begin(), tmp.end());
          std::sort(tmp2.begin(), tmp2.end());
          if (tmp != tmp2) continue;
          const idx a0 = c0[static_cast<std::size_t>(fi)], a1 = c1[static_cast<std::size_t>(fi)];
          const idx b0 = c0[static_cast<std::size_t>(fj)], b1 = c1[static_cast<std::size_t>(fj)];
          if (a1 >= 0 || b1 >= 0) {  // one side already internal -> pure duplicate
            alive[static_cast<std::size_t>(fj)] = 0;
            keep_of[static_cast<std::size_t>(fj)] = fi;
            ++n_duplicate_faces;
            continue;
          }
          if (a0 == b0) {  // same cell on both sides -> spurious skin face
            alive[static_cast<std::size_t>(fj)] = 0;
            ++n_duplicate_faces;
            continue;
          }
          // merge into an internal face: owner = min cell
          c0[static_cast<std::size_t>(fi)] = std::min(a0, b0);
          c1[static_cast<std::size_t>(fi)] = std::max(a0, b0);
          fpatch[static_cast<std::size_t>(fi)] = -1;
          alive[static_cast<std::size_t>(fj)] = 0;
          ++n_merged_faces;
        }
      }
    }
    if (n_merged_faces || n_duplicate_faces) {
      log_verbose("face welding: " + std::to_string(n_merged_faces) + " interior interfaces, " +
                  std::to_string(n_duplicate_faces) + " duplicate skins");
    }
  }

  // ---------------- 5. canonical face ordering ---------------------------
  // internal faces first (sorted by owner -> solver locality), then boundary
  // faces grouped by patch and sorted by owner.  This keeps the OpenFOAM
  // convention (patches are contiguous ranges at the tail of the face list).
  std::vector<idx> order;         // output face -> source face
  std::vector<idx> order_patch;   // output face -> patch id (-1 = internal)
  order.reserve(static_cast<std::size_t>(nf_in));
  order_patch.reserve(static_cast<std::size_t>(nf_in));

  std::vector<idx> internal_ids, boundary_ids;
  internal_ids.reserve(static_cast<std::size_t>(nf_in));
  boundary_ids.reserve(static_cast<std::size_t>(nf_in));
  for (idx f = 0; f < nf_in; ++f) {
    if (!alive[static_cast<std::size_t>(f)]) continue;
    (c1[static_cast<std::size_t>(f)] >= 0 ? internal_ids : boundary_ids).push_back(f);
  }
  std::sort(internal_ids.begin(), internal_ids.end(), [&](idx a, idx b) {
    if (c0[static_cast<std::size_t>(a)] != c0[static_cast<std::size_t>(b)])
      return c0[static_cast<std::size_t>(a)] < c0[static_cast<std::size_t>(b)];
    return c1[static_cast<std::size_t>(a)] < c1[static_cast<std::size_t>(b)];
  });
  for (idx f : internal_ids) {
    order.push_back(f);
    order_patch.push_back(-1);
  }

  std::vector<PatchInfo> patches_out;
  patches_out.reserve(patches.size() + 1);
  for (std::size_t p = 0; p < patches.size(); ++p) {
    std::vector<idx> ids;
    for (idx f : boundary_ids)
      if (fpatch[static_cast<std::size_t>(f)] == static_cast<idx>(p)) ids.push_back(f);
    if (ids.empty()) continue;   // empty patches are dropped
    std::sort(ids.begin(), ids.end(), [&](idx a, idx b) {
      return c0[static_cast<std::size_t>(a)] < c0[static_cast<std::size_t>(b)];
    });
    for (idx f : ids) {
      order.push_back(f);
      order_patch.push_back(static_cast<idx>(p));
    }
    patches_out.push_back(patches[p]);
  }
  {  // boundary faces without a declared patch (VTK sources) -> defaultFaces
    std::vector<idx> orphans;
    for (idx f : boundary_ids)
      if (fpatch[static_cast<std::size_t>(f)] < 0) orphans.push_back(f);
    if (!orphans.empty()) {
      std::sort(orphans.begin(), orphans.end(), [&](idx a, idx b) {
        return c0[static_cast<std::size_t>(a)] < c0[static_cast<std::size_t>(b)];
      });
      PatchInfo pi;
      pi.name = "defaultFaces";
      pi.type = "patch";
      pi.role = PatchRole::kUnknown;
      const idx pid = static_cast<idx>(patches_out.size());
      patches_out.push_back(pi);
      for (idx f : orphans) {
        order.push_back(f);
        order_patch.push_back(pid);
      }
    }
  }

  // ---------------- 5b. rebuild face arrays in canonical order -----------
  const gidx nf = static_cast<gidx>(order.size());
  if (nf == 0) fail("build_graph_mesh: no faces left after canonicalisation");
  gm.n_faces = nf;
  gm.owner.resize(static_cast<std::size_t>(nf));
  gm.neighbour.resize(static_cast<std::size_t>(nf));
  gm.patch_of.resize(static_cast<std::size_t>(nf));
  gm.fv_offsets.assign(static_cast<std::size_t>(nf) + 1, 0);
  gm.fv.clear();
  gm.fv.reserve(fverts.size());
  for (gidx i = 0; i < nf; ++i) {
    const idx src = order[static_cast<std::size_t>(i)];
    gm.fv_offsets[static_cast<std::size_t>(i)] = static_cast<gidx>(gm.fv.size());
    for (gidx k = foff[static_cast<std::size_t>(src)]; k < foff[static_cast<std::size_t>(src) + 1]; ++k)
      gm.fv.push_back(fverts[static_cast<std::size_t>(k)]);
    gm.owner[static_cast<std::size_t>(i)] = c0[static_cast<std::size_t>(src)];
    gm.neighbour[static_cast<std::size_t>(i)] = c1[static_cast<std::size_t>(src)];
    gm.patch_of[static_cast<std::size_t>(i)] = order_patch[static_cast<std::size_t>(i)];
  }
  gm.fv_offsets[static_cast<std::size_t>(nf)] = static_cast<gidx>(gm.fv.size());

  gidx n_internal = static_cast<gidx>(internal_ids.size());
  gm.n_internal = n_internal;
  gm.n_boundary = nf - n_internal;
  {
    gidx cursor = n_internal;
    for (std::size_t p = 0; p < patches_out.size(); ++p) {
      patches_out[p].start = static_cast<idx>(cursor);
      gidx c = cursor;
      while (c < nf && gm.patch_of[static_cast<std::size_t>(c)] == static_cast<idx>(p)) ++c;
      patches_out[p].size = static_cast<idx>(c - cursor);
      cursor = c;
    }
    if (cursor != nf) fail("patch ranges do not cover the boundary faces");
  }
  gm.patches = std::move(patches_out);

  // ---------------- 6. point pruning -------------------------------------
  if (opts.remove_unused_points) {
    std::vector<idx> remap(pts.size(), -1);
    idx next = 0;
    for (idx& v : gm.fv) {
      if (remap[static_cast<std::size_t>(v)] < 0) remap[static_cast<std::size_t>(v)] = next++;
      v = remap[static_cast<std::size_t>(v)];
    }
    std::vector<Vec3> pruned(static_cast<std::size_t>(next));
    for (std::size_t i = 0; i < pts.size(); ++i)
      if (remap[i] >= 0) pruned[static_cast<std::size_t>(remap[i])] = pts[i];
    pts.swap(pruned);
  }
  gm.points = pts;

  const gidx nc = gm.n_cells;

  // ---------------- 7. cell -> face CSR ----------------------------------
  {
    std::vector<gidx> off(static_cast<std::size_t>(nc) + 1, 0);
    for (gidx f = 0; f < nf; ++f) {
      off[static_cast<std::size_t>(gm.owner[static_cast<std::size_t>(f)]) + 1]++;
      if (gm.neighbour[static_cast<std::size_t>(f)] >= 0)
        off[static_cast<std::size_t>(gm.neighbour[static_cast<std::size_t>(f)]) + 1]++;
    }
    for (gidx i = 0; i < nc; ++i) off[static_cast<std::size_t>(i) + 1] += off[static_cast<std::size_t>(i)];
    gm.cf_faces.resize(static_cast<std::size_t>(off[static_cast<std::size_t>(nc)]));
    std::vector<gidx> cursor(off.begin(), off.end() - 1);
    for (gidx f = 0; f < nf; ++f) {
      gm.cf_faces[static_cast<std::size_t>(cursor[static_cast<std::size_t>(gm.owner[static_cast<std::size_t>(f)])]++)] = static_cast<idx>(f);
      const idx nb = gm.neighbour[static_cast<std::size_t>(f)];
      if (nb >= 0) gm.cf_faces[static_cast<std::size_t>(cursor[static_cast<std::size_t>(nb)]++)] = static_cast<idx>(f);
    }
    gm.cf_offsets = std::move(off);
  }

  // ---------------- 8. reference points + face orientation ----------------
  const Vec3* P = gm.points.data();
  std::vector<Vec3> ref(static_cast<std::size_t>(nc));
  parallel_for(nc, [&](gidx i) {
    Vec3 s{0, 0, 0};
    double w = 0.0;
    for (gidx k = gm.cf_offsets[static_cast<std::size_t>(i)]; k < gm.cf_offsets[static_cast<std::size_t>(i) + 1]; ++k) {
      const idx f = gm.cf_faces[static_cast<std::size_t>(k)];
      for (gidx j = gm.fv_offsets[static_cast<std::size_t>(f)]; j < gm.fv_offsets[static_cast<std::size_t>(f) + 1]; ++j) {
        s += P[gm.fv[static_cast<std::size_t>(j)]];
        w += 1.0;
      }
    }
    ref[static_cast<std::size_t>(i)] = (w > 0.0) ? s / w : Vec3{0, 0, 0};
  }, nt);

  if (opts.orient_faces && !opts.assume_oriented) {
    std::vector<char> flip(static_cast<std::size_t>(nf), 0);
    parallel_for(nf, [&](gidx f) {
      const idx nv = static_cast<idx>(gm.fv_offsets[static_cast<std::size_t>(f) + 1] - gm.fv_offsets[static_cast<std::size_t>(f)]);
      const Vec3 Sf = polygon_area_vector(P, gm.fv.data() + gm.fv_offsets[static_cast<std::size_t>(f)], nv);
      const FaceGeom g = face_geometry(P, gm.fv.data() + gm.fv_offsets[static_cast<std::size_t>(f)], nv);
      const idx nb = gm.neighbour[static_cast<std::size_t>(f)];
      double s;
      if (nb >= 0) {
        s = dot(Sf, ref[static_cast<std::size_t>(nb)] - ref[static_cast<std::size_t>(gm.owner[static_cast<std::size_t>(f)])]);
      } else {
        s = dot(Sf, g.xf - ref[static_cast<std::size_t>(gm.owner[static_cast<std::size_t>(f)])]);
      }
      if (s < 0.0) flip[static_cast<std::size_t>(f)] = 1;
    }, nt);

    gidx n_flipped = 0;
    for (gidx f = 0; f < nf; ++f) {
      if (!flip[static_cast<std::size_t>(f)]) continue;
      ++n_flipped;
      std::reverse(gm.fv.begin() + gm.fv_offsets[static_cast<std::size_t>(f)],
                   gm.fv.begin() + gm.fv_offsets[static_cast<std::size_t>(f) + 1]);
      // reverse also keeps the OpenFOAM "twist" semantics for neighbours
    }
    if (n_flipped) log_verbose("re-oriented " + std::to_string(n_flipped) + " faces outward");
  }

  // ---------------- 9. face geometry -------------------------------------
  gm.Sf.resize(static_cast<std::size_t>(nf));
  gm.xf.resize(static_cast<std::size_t>(nf));
  gm.magSf.resize(static_cast<std::size_t>(nf));
  gm.face_warp.assign(static_cast<std::size_t>(nf), 0.0);
  parallel_for(nf, [&](gidx f) {
    const idx nv = static_cast<idx>(gm.fv_offsets[static_cast<std::size_t>(f) + 1] - gm.fv_offsets[static_cast<std::size_t>(f)]);
    const FaceGeom g = face_geometry(P, gm.fv.data() + gm.fv_offsets[static_cast<std::size_t>(f)], nv);
    const double mag = norm(g.Sf);
    gm.Sf.set(static_cast<std::size_t>(f), g.Sf);
    gm.xf.set(static_cast<std::size_t>(f), g.xf);
    gm.magSf[static_cast<std::size_t>(f)] = mag;
    gm.face_warp[static_cast<std::size_t>(f)] =
        (g.sum_tri > 0.0) ? std::max(0.0, 1.0 - mag / g.sum_tri) : 0.0;
  }, nt);

  // ---------------- 10. cell volume + centroid (tetra decomposition) ------
  gm.cf.resize(static_cast<std::size_t>(nc));
  gm.vol.assign(static_cast<std::size_t>(nc), 0.0);
  parallel_for(nc, [&](gidx i) {
    const Vec3 p0 = ref[static_cast<std::size_t>(i)];
    double V = 0.0;
    Vec3 cx{0, 0, 0};
    for (gidx k = gm.cf_offsets[static_cast<std::size_t>(i)]; k < gm.cf_offsets[static_cast<std::size_t>(i) + 1]; ++k) {
      const idx f = gm.cf_faces[static_cast<std::size_t>(k)];
      const idx nv = static_cast<idx>(gm.fv_offsets[static_cast<std::size_t>(f) + 1] - gm.fv_offsets[static_cast<std::size_t>(f)]);
      const idx* v = gm.fv.data() + gm.fv_offsets[static_cast<std::size_t>(f)];
      const Vec3 xf = gm.xf.get(static_cast<std::size_t>(f));
      // the face vertex order is outward for the owner; the neighbour sees the
      // face with the opposite orientation, which flips each tetrahedron's sign
      const bool owner_side = (gm.owner[static_cast<std::size_t>(f)] == i);
      for (idx t = 0; t < nv; ++t) {
        const Vec3 a = xf;
        const Vec3 b = P[v[t]];
        const Vec3 c = P[v[(t + 1) % nv]];
        const double vt = owner_side ? dot(a - p0, cross(b - p0, c - p0)) / 6.0
                                     : dot(a - p0, cross(c - p0, b - p0)) / 6.0;
        V += vt;
        cx += (p0 + a + b + c) * (vt * 0.25);
      }
    }
    if (std::fabs(V) > 1e-300) {
      gm.cf.set(static_cast<std::size_t>(i), cx / V);
    } else {
      gm.cf.set(static_cast<std::size_t>(i), p0);
    }
    gm.vol[static_cast<std::size_t>(i)] = V;
  }, nt);

  // ---------------- 11. MeshErrorMap --------------------------------------
  const MeshErrorMapParams& ep = opts.err;
  const double theta_crit = ep.theta_crit_deg * M_PI / 180.0;
  const double theta_w = ep.theta_width_deg * M_PI / 180.0;
  const double cos_floor = std::cos(89.0 * M_PI / 180.0);   // guard |cos| -> 0

  gm.d_ij.resize(static_cast<std::size_t>(nf));
  gm.delta_f.resize(static_cast<std::size_t>(nf));
  gm.k_f.resize(static_cast<std::size_t>(nf));
  gm.k_f_comp.resize(static_cast<std::size_t>(nf));
  gm.m_skew.resize(static_cast<std::size_t>(nf));
  gm.mag_d.assign(static_cast<std::size_t>(nf), 0.0);
  gm.cos_ortho.assign(static_cast<std::size_t>(nf), 0.0);
  gm.theta.assign(static_cast<std::size_t>(nf), 0.0);
  gm.non_ortho_corr.assign(static_cast<std::size_t>(nf), 0.0);
  gm.skewness.assign(static_cast<std::size_t>(nf), 0.0);
  gm.beta.assign(static_cast<std::size_t>(nf), 1.0);

  parallel_for(nf, [&](gidx f) {
    const std::size_t fi = static_cast<std::size_t>(f);
    const idx o = gm.owner[fi];
    const idx nb = gm.neighbour[fi];
    const Vec3 S = gm.Sf.get(fi);
    const Vec3 xfc = gm.xf.get(fi);
    const Vec3 ci = gm.cf.get(static_cast<std::size_t>(o));
    const Vec3 d = (nb >= 0) ? (gm.cf.get(static_cast<std::size_t>(nb)) - ci) : (xfc - ci);
    const double md = norm(d);
    gm.d_ij.set(fi, d);
    gm.mag_d[fi] = md;

    const double mag = gm.magSf[fi];
    double cos_t = (md > 0.0 && mag > 0.0) ? dot(S, d) / (mag * md) : 0.0;
    cos_t = clampd(cos_t, -1.0, 1.0);
    gm.cos_ortho[fi] = cos_t;
    const double th = std::acos(cos_t);
    gm.theta[fi] = th * 180.0 / M_PI;

    // over-relaxed decomposition (DiFVM Eq. 15)
    const double cos_c = (std::fabs(cos_t) > cos_floor) ? std::fabs(cos_t) : cos_floor;
    // delta_f = (|Sf| / |S_hat_f . d_hat_ij|) * d_hat_ij   (over-relaxed, DiFVM Eq. 15)
    const Vec3 delta = (md > 0.0) ? d * (mag / (cos_c * md)) : Vec3{0, 0, 0};
    gm.delta_f.set(fi, delta);
    gm.k_f.set(fi, S - delta);
    gm.non_ortho_corr[fi] = mag / cos_c;

    // skewness vector m_f^skew = xf - x_int,  x_int = ci + t d
    const double denom = dot(d, S);
    double t = 0.0;
    if (std::fabs(denom) > 1e-300) t = dot(xfc - ci, S) / denom;
    const Vec3 xint = ci + d * t;
    const Vec3 m = xfc - xint;
    gm.m_skew.set(fi, m);
    const double sigma = (md > 0.0) ? norm(m) / md : 0.0;
    gm.skewness[fi] = sigma;

    // smooth quality guards (normalised tanh: exactly 1 at zero defect,
    // 0.5 at the threshold, 0 beyond the transition band)
    const double f_theta = tanh_guard(th, theta_crit, theta_w);
    const double f_skew = tanh_guard(sigma, ep.skew_sigma0, ep.skew_width);
    const double beta = clampd(f_theta * f_skew, ep.beta_min, 1.0);
    gm.beta[fi] = beta;
    // damped non-orthogonal correction vector (zero above the critical angle)
    gm.k_f_comp.set(fi, (S - delta) * f_theta);
  }, nt);

  // cell aggregates + shield omega_i
  gm.cell_theta.assign(static_cast<std::size_t>(nc), 0.0);
  gm.cell_skew.assign(static_cast<std::size_t>(nc), 0.0);
  gm.omega.assign(static_cast<std::size_t>(nc), 1.0);
  gm.vol_ratio.assign(static_cast<std::size_t>(nc), 1.0);
  gm.cell_quality.assign(static_cast<std::size_t>(nc), 1.0f);

  std::vector<double> min_nb_vol(static_cast<std::size_t>(nc), std::numeric_limits<double>::max());
  parallel_for(nc, [&](gidx i) {
    const std::size_t ii = static_cast<std::size_t>(i);
    const double vi = std::fabs(gm.vol[ii]);
    double vmin = vi;
    double th_max = 0.0, sk_max = 0.0, beta_min_cell = 1.0;
    for (gidx k = gm.cf_offsets[ii]; k < gm.cf_offsets[ii + 1]; ++k) {
      const idx f = gm.cf_faces[static_cast<std::size_t>(k)];
      const std::size_t fi = static_cast<std::size_t>(f);
      th_max = std::max(th_max, gm.theta[fi]);
      sk_max = std::max(sk_max, gm.skewness[fi]);
      beta_min_cell = std::min(beta_min_cell, gm.beta[fi]);
      const idx nb = (gm.owner[fi] == i) ? gm.neighbour[fi] : gm.owner[fi];
      if (nb >= 0) vmin = std::min(vmin, std::fabs(gm.vol[static_cast<std::size_t>(nb)]));
    }
    gm.cell_theta[ii] = th_max;
    gm.cell_skew[ii] = sk_max;
    const double ratio = (vmin > 0.0) ? vi / vmin : 1.0;
    gm.vol_ratio[ii] = ratio;
    const double gamma = 1.0 / (1.0 + std::log(std::max(ratio, 1.0)) / std::log(std::max(ep.vol_ratio_scale, 1.01)));
    gm.omega[ii] = clampd(beta_min_cell * gamma, ep.omega_min, 1.0);
    const double q = clamp01(std::cos(th_max * M_PI / 180.0)) * clamp01(1.0 - 2.0 * sk_max);
    gm.cell_quality[ii] = static_cast<float>(q);
  }, nt);

  // ---------------- 12. cell -> cell adjacency ----------------------------
  if (opts.build_adjacency) {
    std::vector<gidx> off(static_cast<std::size_t>(nc) + 1, 0);
    for (gidx f = 0; f < gm.n_internal; ++f) {
      off[static_cast<std::size_t>(gm.owner[static_cast<std::size_t>(f)]) + 1]++;
      off[static_cast<std::size_t>(gm.neighbour[static_cast<std::size_t>(f)]) + 1]++;
    }
    for (gidx i = 0; i < nc; ++i) off[static_cast<std::size_t>(i) + 1] += off[static_cast<std::size_t>(i)];
    gm.nn_cells.resize(static_cast<std::size_t>(off[static_cast<std::size_t>(nc)]));
    gm.nn_sign.resize(gm.nn_cells.size());
    std::vector<gidx> cursor(off.begin(), off.end() - 1);
    for (gidx f = 0; f < gm.n_internal; ++f) {
      const idx o = gm.owner[static_cast<std::size_t>(f)];
      const idx nb = gm.neighbour[static_cast<std::size_t>(f)];
      gm.nn_cells[static_cast<std::size_t>(cursor[static_cast<std::size_t>(o)])] = nb;
      gm.nn_sign[static_cast<std::size_t>(cursor[static_cast<std::size_t>(o)]++)] = 1;
      gm.nn_cells[static_cast<std::size_t>(cursor[static_cast<std::size_t>(nb)])] = o;
      gm.nn_sign[static_cast<std::size_t>(cursor[static_cast<std::size_t>(nb)]++)] = -1;
    }
    gm.nn_offsets = std::move(off);
  }

  log_verbose("mesh compiled in " + std::to_string(total.seconds() * 1e3) + " ms");
  return gm;
}

// ---------------------------------------------------------------------------
// Face permutation + boundary re-patching
// ---------------------------------------------------------------------------
void permute_faces(GraphMesh& gm, const std::vector<idx>& order) {
  const gidx nf = gm.n_faces;
  if (static_cast<gidx>(order.size()) != nf) fail("permute_faces: order size mismatch");

  auto permute_idx = [](auto& v, const std::vector<idx>& p) {
    auto out = v;
    for (std::size_t i = 0; i < p.size(); ++i) out[i] = v[static_cast<std::size_t>(p[i])];
    v.swap(out);
  };
  auto permute_double = permute_idx;
  auto permute_v3 = [](Vec3SoA& f, const std::vector<idx>& p) {
    Vec3SoA out;
    out.resize(f.size());
    for (std::size_t i = 0; i < p.size(); ++i) {
      const std::size_t s = static_cast<std::size_t>(p[i]);
      out.x[i] = f.x[s];
      out.y[i] = f.y[s];
      out.z[i] = f.z[s];
    }
    f = std::move(out);
  };

  permute_idx(gm.owner, order);
  permute_idx(gm.neighbour, order);
  permute_idx(gm.patch_of, order);
  permute_v3(gm.Sf, order);
  permute_v3(gm.xf, order);
  permute_v3(gm.d_ij, order);
  permute_v3(gm.delta_f, order);
  permute_v3(gm.k_f, order);
  permute_v3(gm.k_f_comp, order);
  permute_v3(gm.m_skew, order);
  permute_double(gm.magSf, order);
  permute_double(gm.mag_d, order);
  permute_double(gm.cos_ortho, order);
  permute_double(gm.theta, order);
  permute_double(gm.non_ortho_corr, order);
  permute_double(gm.skewness, order);
  permute_double(gm.beta, order);
  permute_double(gm.face_warp, order);
  {
    std::vector<gidx> nf_off(static_cast<std::size_t>(nf) + 1, 0);
    std::vector<idx> nf_fv;
    nf_fv.reserve(gm.fv.size());
    for (gidx i = 0; i < nf; ++i) {
      const idx src = order[static_cast<std::size_t>(i)];
      nf_off[static_cast<std::size_t>(i)] = static_cast<gidx>(nf_fv.size());
      for (gidx k = gm.fv_offsets[static_cast<std::size_t>(src)]; k < gm.fv_offsets[static_cast<std::size_t>(src) + 1]; ++k)
        nf_fv.push_back(gm.fv[static_cast<std::size_t>(k)]);
    }
    nf_off[static_cast<std::size_t>(nf)] = static_cast<gidx>(nf_fv.size());
    gm.fv_offsets = std::move(nf_off);
    gm.fv = std::move(nf_fv);
  }
  {
    idx cursor = static_cast<idx>(gm.n_internal);
    for (auto& p : gm.patches) {
      p.start = cursor;
      cursor += p.size;
    }
  }
  // rebuild cell -> face and cell -> cell adjacency
  const gidx nc = gm.n_cells;
  {
    std::vector<gidx> off(static_cast<std::size_t>(nc) + 1, 0);
    for (gidx f = 0; f < nf; ++f) {
      off[static_cast<std::size_t>(gm.owner[static_cast<std::size_t>(f)]) + 1]++;
      if (gm.neighbour[static_cast<std::size_t>(f)] >= 0)
        off[static_cast<std::size_t>(gm.neighbour[static_cast<std::size_t>(f)]) + 1]++;
    }
    for (gidx i = 0; i < nc; ++i) off[static_cast<std::size_t>(i) + 1] += off[static_cast<std::size_t>(i)];
    gm.cf_faces.resize(static_cast<std::size_t>(off[static_cast<std::size_t>(nc)]));
    std::vector<gidx> cur(off.begin(), off.end() - 1);
    for (gidx f = 0; f < nf; ++f) {
      gm.cf_faces[static_cast<std::size_t>(cur[static_cast<std::size_t>(gm.owner[static_cast<std::size_t>(f)])]++)] = static_cast<idx>(f);
      const idx nb = gm.neighbour[static_cast<std::size_t>(f)];
      if (nb >= 0) gm.cf_faces[static_cast<std::size_t>(cur[static_cast<std::size_t>(nb)]++)] = static_cast<idx>(f);
    }
    gm.cf_offsets = std::move(off);
  }
  {
    std::vector<gidx> off(static_cast<std::size_t>(nc) + 1, 0);
    for (gidx f = 0; f < gm.n_internal; ++f) {
      off[static_cast<std::size_t>(gm.owner[static_cast<std::size_t>(f)]) + 1]++;
      off[static_cast<std::size_t>(gm.neighbour[static_cast<std::size_t>(f)]) + 1]++;
    }
    for (gidx i = 0; i < nc; ++i) off[static_cast<std::size_t>(i) + 1] += off[static_cast<std::size_t>(i)];
    gm.nn_cells.resize(static_cast<std::size_t>(off[static_cast<std::size_t>(nc)]));
    gm.nn_sign.resize(gm.nn_cells.size());
    std::vector<gidx> cur(off.begin(), off.end() - 1);
    for (gidx f = 0; f < gm.n_internal; ++f) {
      const idx o = gm.owner[static_cast<std::size_t>(f)];
      const idx nb = gm.neighbour[static_cast<std::size_t>(f)];
      gm.nn_cells[static_cast<std::size_t>(cur[static_cast<std::size_t>(o)])] = nb;
      gm.nn_sign[static_cast<std::size_t>(cur[static_cast<std::size_t>(o)]++)] = 1;
      gm.nn_cells[static_cast<std::size_t>(cur[static_cast<std::size_t>(nb)])] = o;
      gm.nn_sign[static_cast<std::size_t>(cur[static_cast<std::size_t>(nb)]++)] = -1;
    }
    gm.nn_offsets = std::move(off);
  }
}

void auto_patch_boundary(GraphMesh& gm, const Vec3& lo, const Vec3& hi, double tol_rel,
                         const std::string& interface_name, const std::string& interface_type) {
  const double diag = norm(hi - lo);
  const double tol = std::max(tol_rel * diag, 1e-300);
  const char* names[6] = {"xmin", "xmax", "ymin", "ymax", "zmin", "zmax"};
  std::vector<PatchInfo> patches;
  for (int s = 0; s < 6; ++s) {
    PatchInfo p;
    p.name = names[s];
    p.type = "patch";
    p.role = PatchRole::kUnknown;
    patches.push_back(std::move(p));
  }
  PatchInfo iface;
  iface.name = interface_name;
  iface.type = interface_type;
  iface.role = PatchRole::kWall;
  patches.push_back(std::move(iface));

  std::vector<idx> label(static_cast<std::size_t>(gm.n_faces), -1);
  for (gidx f = gm.n_internal; f < gm.n_faces; ++f) {
    const std::size_t fi = static_cast<std::size_t>(f);
    // is the face entirely on one of the six box planes?
    int side = -1;
    const gidx b = gm.fv_offsets[fi], e = gm.fv_offsets[fi + 1];
    for (int s = 0; s < 6 && side < 0; ++s) {
      const int axis = s / 2;
      const bool use_max = (s % 2) == 1;
      const double target = use_max ? (axis == 0 ? hi.x : (axis == 1 ? hi.y : hi.z))
                                    : (axis == 0 ? lo.x : (axis == 1 ? lo.y : lo.z));
      bool on_plane = true;
      for (gidx k = b; k < e && on_plane; ++k) {
        const Vec3& p = gm.points[static_cast<std::size_t>(gm.fv[static_cast<std::size_t>(k)])];
        const double v = (axis == 0) ? p.x : (axis == 1 ? p.y : p.z);
        if (std::fabs(v - target) > tol) on_plane = false;
      }
      if (on_plane) side = s;
    }
    label[fi] = static_cast<idx>(side < 0 ? 6 : side);
  }

  // reorder boundary faces so that each patch is contiguous
  std::vector<idx> order;
  order.reserve(static_cast<std::size_t>(gm.n_faces));
  for (gidx f = 0; f < gm.n_internal; ++f) order.push_back(static_cast<idx>(f));
  std::vector<PatchInfo> patches_out;
  for (std::size_t p = 0; p < patches.size(); ++p) {
    std::vector<idx> ids;
    for (gidx f = gm.n_internal; f < gm.n_faces; ++f)
      if (label[static_cast<std::size_t>(f)] == static_cast<idx>(p)) ids.push_back(static_cast<idx>(f));
    if (ids.empty()) continue;
    std::sort(ids.begin(), ids.end(), [&](idx a, idx b) {
      return gm.owner[static_cast<std::size_t>(a)] < gm.owner[static_cast<std::size_t>(b)];
    });
    order.insert(order.end(), ids.begin(), ids.end());
    patches_out.push_back(patches[p]);
  }
  if (static_cast<gidx>(order.size()) != gm.n_faces) fail("auto_patch_boundary: lost boundary faces");

  // relabel patch_of for the new order before permuting
  std::vector<idx> new_patch(static_cast<std::size_t>(gm.n_faces), -1);
  idx cursor = static_cast<idx>(gm.n_internal);
  for (std::size_t p = 0; p < patches_out.size(); ++p) {
    gidx c = cursor;
    while (c < gm.n_faces && label[static_cast<std::size_t>(order[static_cast<std::size_t>(c)])] == static_cast<idx>(p)) {
      new_patch[static_cast<std::size_t>(c)] = static_cast<idx>(p);
      ++c;
      ++patches_out[p].size;
    }
    cursor = c;
  }
  permute_faces(gm, order);
  for (std::size_t i = 0; i < new_patch.size(); ++i)
    if (new_patch[i] >= 0) gm.patch_of[i] = new_patch[i];
  gm.patches = std::move(patches_out);
}

// ---------------------------------------------------------------------------
// Audit
// ---------------------------------------------------------------------------
std::string MeshAudit::to_string(bool verbose) const {
  char buf[2048];
  std::string s;
  std::snprintf(buf, sizeof(buf),
      "cells=%lld faces=%lld (internal=%lld boundary=%lld)\n"
      "  volume: total=%.9g min=%.6g max=%.6g  negative=%lld zero=%lld\n"
      "  bbox: [%.6g %.6g %.6g] .. [%.6g %.6g %.6g]  boundary_area=%.9g\n"
      "  closure: max|sum Sf|/sum|Sf| = %.3e  (open cells=%lld)\n"
      "  non-orthogonality: max=%.3f deg  mean=%.3f deg  p95=%.3f deg\n"
      "  skewness: max=%.4f mean=%.4f   face_warp_max=%.3e\n"
      "  MeshErrorMap: beta min=%.3f mean=%.3f (cells with beta<0.5: %lld)  omega min=%.3f mean=%.3f\n"
      "  volume ratio: max=%.3f",
      static_cast<long long>(n_cells), static_cast<long long>(n_faces),
      static_cast<long long>(n_faces - n_boundary_faces), static_cast<long long>(n_boundary_faces),
      total_volume, min_volume, max_volume, static_cast<long long>(n_negative_cells),
      static_cast<long long>(n_zero_volume_cells), lo.x, lo.y, lo.z, hi.x, hi.y, hi.z, boundary_area,
      max_closure_residual, static_cast<long long>(n_open_cells), max_non_ortho_deg, mean_non_ortho_deg,
      p95_non_ortho_deg, max_skewness, mean_skewness, max_face_warp, min_beta, mean_beta,
      static_cast<long long>(n_cells_beta_below_0p5), min_omega, mean_omega, max_vol_ratio);
  s = buf;
  if (verbose) {
    s += "\n  max_face_area_mismatch=" + std::to_string(max_face_area_mismatch);
  }
  return s;
}

bool MeshAudit::ok() const {
  return n_negative_cells == 0 && n_zero_volume_cells == 0 && max_closure_residual < 1e-8;
}

MeshAudit audit_mesh(const GraphMesh& gm, int threads) {
  const gidx nc = gm.n_cells, nf = gm.n_faces;
  const int nt = threads > 0 ? threads : num_threads();
  MeshAudit a;
  a.n_cells = nc;
  a.n_faces = nf;
  a.n_boundary_faces = gm.n_boundary;
  a.min_volume = std::numeric_limits<double>::max();
  a.max_volume = -a.min_volume;

  // per-cell scatter (race free: every cell is written by exactly one worker)
  std::vector<Vec3> closure(static_cast<std::size_t>(nc), Vec3{0, 0, 0});
  std::vector<double> abs_area(static_cast<std::size_t>(nc), 0.0);
  parallel_for(nc, [&](gidx i) {
    const std::size_t ii = static_cast<std::size_t>(i);
    Vec3 s{0, 0, 0};
    double aa = 0.0;
    for (gidx k = gm.cf_offsets[ii]; k < gm.cf_offsets[ii + 1]; ++k) {
      const idx f = gm.cf_faces[static_cast<std::size_t>(k)];
      const std::size_t fi = static_cast<std::size_t>(f);
      Vec3 S = gm.Sf.get(fi);
      if (gm.neighbour[fi] == i) S = -S;   // this cell is the neighbour side
      s += S;
      aa += gm.magSf[fi];
    }
    closure[ii] = s;
    abs_area[ii] = aa;
  }, nt);

  double sum_theta = 0.0, sum_skew = 0.0, sum_beta = 0.0, sum_omega = 0.0, sum_area = 0.0;
  std::atomic<double> max_theta{0.0}, max_skew{0.0}, max_warp{0.0}, min_beta{1.0}, max_ratio{1.0}, min_ratio{1.0};
  std::atomic<gidx> n_neg{0}, n_zero{0}, n_open{0}, n_lowbeta{0}, n_lowomega{0};
  std::atomic<double> min_omega{1.0}, min_vol{std::numeric_limits<double>::max()}, max_vol{0.0};

  parallel_for(nc, [&](gidx i) {
    const std::size_t ii = static_cast<std::size_t>(i);
    const double v = gm.vol[ii];
    if (v < 0.0) n_neg.fetch_add(1, std::memory_order_relaxed);
    if (std::fabs(v) <= 1e-300) n_zero.fetch_add(1, std::memory_order_relaxed);
    double cur = min_vol.load(std::memory_order_relaxed);
    while (v < cur && !min_vol.compare_exchange_weak(cur, v, std::memory_order_relaxed)) {}
    cur = max_vol.load(std::memory_order_relaxed);
    while (v > cur && !max_vol.compare_exchange_weak(cur, v, std::memory_order_relaxed)) {}
    if (gm.omega.size()) {
      if (gm.omega[ii] < 0.5) n_lowomega.fetch_add(1, std::memory_order_relaxed);
      double c = min_omega.load(std::memory_order_relaxed);
      while (gm.omega[ii] < c && !min_omega.compare_exchange_weak(c, gm.omega[ii], std::memory_order_relaxed)) {}
    }
    double c = max_ratio.load(std::memory_order_relaxed);
    while (gm.vol_ratio[ii] > c && !max_ratio.compare_exchange_weak(c, gm.vol_ratio[ii], std::memory_order_relaxed)) {}
    c = min_ratio.load(std::memory_order_relaxed);
    while (gm.vol_ratio[ii] < c && !min_ratio.compare_exchange_weak(c, gm.vol_ratio[ii], std::memory_order_relaxed)) {}
  }, nt);

  for (gidx i = 0; i < nc; ++i) {
    const std::size_t ii = static_cast<std::size_t>(i);
    const double aa = abs_area[ii];
    if (aa > 0.0) {
      const double r = norm(closure[ii]) / aa;
      a.max_closure_residual = std::max(a.max_closure_residual, r);
      if (r > 1e-8) ++a.n_open_cells;
    }
    a.total_volume += gm.vol[ii];
  }
  a.min_volume = min_vol.load();
  a.max_volume = max_vol.load();
  a.n_negative_cells = n_neg.load();
  a.n_zero_volume_cells = n_zero.load();
  a.max_vol_ratio = max_ratio.load();
  a.min_vol_ratio = min_ratio.load();
  a.n_cells_beta_below_0p5 = n_lowbeta.load();

  std::vector<double> thetas(static_cast<std::size_t>(nf), 0.0);
  for (gidx f = 0; f < nf; ++f) {
    const std::size_t fi = static_cast<std::size_t>(f);
    sum_theta += gm.theta[fi];
    sum_skew += gm.skewness[fi];
    sum_beta += gm.beta[fi];
    sum_area += gm.magSf[fi];
    thetas[fi] = gm.theta[fi];
    double c = max_theta.load(std::memory_order_relaxed);
    while (gm.theta[fi] > c && !max_theta.compare_exchange_weak(c, gm.theta[fi], std::memory_order_relaxed)) {}
    c = max_skew.load(std::memory_order_relaxed);
    while (gm.skewness[fi] > c && !max_skew.compare_exchange_weak(c, gm.skewness[fi], std::memory_order_relaxed)) {}
    c = max_warp.load(std::memory_order_relaxed);
    while (gm.face_warp[fi] > c && !max_warp.compare_exchange_weak(c, gm.face_warp[fi], std::memory_order_relaxed)) {}
    c = min_beta.load(std::memory_order_relaxed);
    while (gm.beta[fi] < c && !min_beta.compare_exchange_weak(c, gm.beta[fi], std::memory_order_relaxed)) {}
  }
  for (gidx i = 0; i < nc; ++i) {
    sum_omega += gm.omega[static_cast<std::size_t>(i)];
    double bmin = 1.0;
    for (gidx k = gm.cf_offsets[static_cast<std::size_t>(i)]; k < gm.cf_offsets[static_cast<std::size_t>(i) + 1]; ++k)
      bmin = std::min(bmin, gm.beta[static_cast<std::size_t>(gm.cf_faces[static_cast<std::size_t>(k)])]);
    if (bmin < 0.5) ++a.n_cells_beta_below_0p5;
  }
  a.max_non_ortho_deg = max_theta.load();
  a.mean_non_ortho_deg = (nf > 0) ? sum_theta / static_cast<double>(nf) : 0.0;
  a.max_skewness = max_skew.load();
  a.mean_skewness = (nf > 0) ? sum_skew / static_cast<double>(nf) : 0.0;
  a.max_face_warp = max_warp.load();
  a.min_beta = min_beta.load();
  a.mean_beta = (nf > 0) ? sum_beta / static_cast<double>(nf) : 1.0;
  a.min_omega = min_omega.load();
  a.mean_omega = (nc > 0) ? sum_omega / static_cast<double>(nc) : 1.0;
  a.boundary_area = sum_area;
  (void)n_lowomega;

  if (!thetas.empty()) {
    const std::size_t k = static_cast<std::size_t>(0.95 * static_cast<double>(thetas.size() - 1));
    std::nth_element(thetas.begin(), thetas.begin() + static_cast<std::ptrdiff_t>(k), thetas.end());
    a.p95_non_ortho_deg = thetas[k];
  }

  // bbox of cell centres
  a.lo = Vec3{std::numeric_limits<double>::max(), std::numeric_limits<double>::max(), std::numeric_limits<double>::max()};
  a.hi = -a.lo;
  for (gidx i = 0; i < nc; ++i) {
    const Vec3 c = gm.cf.get(static_cast<std::size_t>(i));
    a.lo = vmin(a.lo, c);
    a.hi = vmax(a.hi, c);
  }
  return a;
}

std::string quality_histogram(const GraphMesh& gm, int bins) {
  if (gm.n_faces == 0) return "";
  std::string out;
  auto hist = [&](const char* name, const auto& v, double lo, double hi, int nb) {
    std::vector<double> counts(static_cast<std::size_t>(nb), 0.0);
    for (double x : v) {
      int b = static_cast<int>((x - lo) / (hi - lo) * nb);
      b = std::max(0, std::min(nb - 1, b));
      counts[static_cast<std::size_t>(b)] += 1.0;
    }
    out += std::string("  ") + name + "\n";
    char buf[256];
    for (int b = 0; b < nb; ++b) {
      const double frac = counts[static_cast<std::size_t>(b)] / static_cast<double>(v.size());
      const int bar = static_cast<int>(std::lround(frac * 60.0));
      const double b0 = lo + (hi - lo) * b / nb, b1 = lo + (hi - lo) * (b + 1) / nb;
      std::snprintf(buf, sizeof(buf), "    [%6.2f,%6.2f) %6.2f%% ", b0, b1, frac * 100.0);
      out += buf;
      out += std::string(static_cast<std::size_t>(std::max(0, bar)), '#');
      out += "\n";
    }
  };
  hist("non-orthogonality [deg]", gm.theta, 0.0, 90.0, bins);
  hist("skewness [-]          ", gm.skewness, 0.0, 2.0, bins);
  hist("upwind blend beta [-] ", gm.beta, 0.0, 1.0, bins);
  return out;
}

}  // namespace difvm
