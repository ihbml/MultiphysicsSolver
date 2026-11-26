#include "hexcore_state.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <vector>

#ifdef _OPENMP
#include <omp.h>
#endif

namespace cfd::hexcore {

namespace {

struct KeyCoord {
  QuantizedKey key;
  Vec3 pt;
};

inline bool keyCoordLess(const KeyCoord& a, const KeyCoord& b) noexcept {
  if (a.key < b.key) return true;
  if (b.key < a.key) return false;
  if (a.pt.x != b.pt.x) return a.pt.x < b.pt.x;
  if (a.pt.y != b.pt.y) return a.pt.y < b.pt.y;
  return a.pt.z < b.pt.z;
}

inline void compactKeyCoords(std::vector<KeyCoord>& v) {
  if (v.empty()) return;
  std::sort(v.begin(), v.end(), keyCoordLess);
  auto it = std::unique(v.begin(), v.end(), [](const KeyCoord& a, const KeyCoord& b) noexcept {
    return a.key == b.key;
  });
  v.erase(it, v.end());
}

inline int64_t lookupVertexId(
    const std::vector<QuantizedKey>& keys,
    const Vec3& pt,
    double quant_tol
) noexcept {
  const QuantizedKey k = quantize(pt, quant_tol);
  auto it = std::lower_bound(keys.begin(), keys.end(), k);
  if (it != keys.end() && *it == k) {
    return static_cast<int64_t>(it - keys.begin());
  }
  return -1;
}

}  // namespace

int32_t passExtractFacesAndVertices(HexcoreState& st) {
  const int64_t n_active_cells = st.n_active_cells;
  st.cell_types.assign(static_cast<size_t>(n_active_cells), CellType::HEXAHEDRON);

  std::vector<std::vector<RawQuad>> thread_quads(static_cast<size_t>(st.max_threads));
  std::vector<std::vector<KeyCoord>> thread_verts(static_cast<size_t>(st.max_threads));
  std::atomic<bool> omp_failed{false};

#ifdef _OPENMP
#pragma omp parallel
  {
    try {
      int tid = omp_get_thread_num();
      int n_threads = omp_get_num_threads();
      int64_t chunk = (n_active_cells + n_threads - 1) / n_threads;
      int64_t c_beg = tid * chunk;
      int64_t c_end = std::min(n_active_cells, c_beg + chunk);
      auto& local_quads = thread_quads[static_cast<size_t>(tid)];
      auto& local_verts = thread_verts[static_cast<size_t>(tid)];
      if (c_end > c_beg) {
        local_quads.reserve(static_cast<size_t>(c_end - c_beg) * 4);
        local_verts.reserve(static_cast<size_t>(c_end - c_beg) * 16);
      }
#else
  {
    int64_t c_beg = 0;
    int64_t c_end = n_active_cells;
    auto& local_quads = thread_quads[0];
    auto& local_verts = thread_verts[0];
    local_quads.reserve(static_cast<size_t>(n_active_cells) * 4);
    local_verts.reserve(static_cast<size_t>(n_active_cells) * 16);
#endif
      auto push_pt = [&](const Vec3& p) {
        local_verts.push_back(KeyCoord{quantize(p, st.quant_tol), p});
      };
      size_t next_compact_threshold = 262144;

      for (int64_t c = c_beg; c < c_end; ++c) {
        int64_t node_idx = st.active_leaves[static_cast<size_t>(c)];
        const OctreeNode& cell_a = st.nodes[static_cast<size_t>(node_idx)];
        const Vec3& mn = cell_a.bmin;
        const Vec3& mx = cell_a.bmax;

        push_pt(Vec3(mn.x, mn.y, mn.z));
        push_pt(Vec3(mx.x, mn.y, mn.z));
        push_pt(Vec3(mx.x, mx.y, mn.z));
        push_pt(Vec3(mn.x, mx.y, mn.z));
        push_pt(Vec3(mn.x, mn.y, mx.z));
        push_pt(Vec3(mx.x, mn.y, mx.z));
        push_pt(Vec3(mx.x, mx.y, mx.z));
        push_pt(Vec3(mn.x, mx.y, mx.z));

        for (int dir = 0; dir < 6; ++dir) {
          Vec3 n_dir;
          Vec3 q0, q1, q2, q3;
          double plane_val = 0.0;
          bool is_farfield = false;
          PatchId farfield_patch = PatchId::WALL;

          if (dir == 0) {
            n_dir = Vec3(-1, 0, 0);
            q0 = Vec3(cell_a.bmin.x, cell_a.bmin.y, cell_a.bmin.z);
            q1 = Vec3(cell_a.bmin.x, cell_a.bmin.y, cell_a.bmax.z);
            q2 = Vec3(cell_a.bmin.x, cell_a.bmax.y, cell_a.bmax.z);
            q3 = Vec3(cell_a.bmin.x, cell_a.bmax.y, cell_a.bmin.z);
            plane_val = cell_a.bmin.x;
            if (st.options.domain_mode == 0 && std::abs(plane_val - st.dom_min.x) < st.dom_tol) {
              is_farfield = true;
              farfield_patch = PatchId::INLET;
            }
          } else if (dir == 1) {
            n_dir = Vec3(1, 0, 0);
            q0 = Vec3(cell_a.bmax.x, cell_a.bmin.y, cell_a.bmin.z);
            q1 = Vec3(cell_a.bmax.x, cell_a.bmax.y, cell_a.bmin.z);
            q2 = Vec3(cell_a.bmax.x, cell_a.bmax.y, cell_a.bmax.z);
            q3 = Vec3(cell_a.bmax.x, cell_a.bmin.y, cell_a.bmax.z);
            plane_val = cell_a.bmax.x;
            if (st.options.domain_mode == 0 && std::abs(plane_val - st.dom_max.x) < st.dom_tol) {
              is_farfield = true;
              farfield_patch = PatchId::OUTLET;
            }
          } else if (dir == 2) {
            n_dir = Vec3(0, -1, 0);
            q0 = Vec3(cell_a.bmin.x, cell_a.bmin.y, cell_a.bmin.z);
            q1 = Vec3(cell_a.bmax.x, cell_a.bmin.y, cell_a.bmin.z);
            q2 = Vec3(cell_a.bmax.x, cell_a.bmin.y, cell_a.bmax.z);
            q3 = Vec3(cell_a.bmin.x, cell_a.bmin.y, cell_a.bmax.z);
            plane_val = cell_a.bmin.y;
            if (st.options.domain_mode == 0 && std::abs(plane_val - st.dom_min.y) < st.dom_tol) {
              is_farfield = true;
              farfield_patch = PatchId::SIDES;
            }
          } else if (dir == 3) {
            n_dir = Vec3(0, 1, 0);
            q0 = Vec3(cell_a.bmin.x, cell_a.bmax.y, cell_a.bmin.z);
            q1 = Vec3(cell_a.bmin.x, cell_a.bmax.y, cell_a.bmax.z);
            q2 = Vec3(cell_a.bmax.x, cell_a.bmax.y, cell_a.bmax.z);
            q3 = Vec3(cell_a.bmax.x, cell_a.bmax.y, cell_a.bmin.z);
            plane_val = cell_a.bmax.y;
            if (st.options.domain_mode == 0 && std::abs(plane_val - st.dom_max.y) < st.dom_tol) {
              is_farfield = true;
              farfield_patch = PatchId::SIDES;
            }
          } else if (dir == 4) {
            n_dir = Vec3(0, 0, -1);
            q0 = Vec3(cell_a.bmin.x, cell_a.bmin.y, cell_a.bmin.z);
            q1 = Vec3(cell_a.bmin.x, cell_a.bmax.y, cell_a.bmin.z);
            q2 = Vec3(cell_a.bmax.x, cell_a.bmax.y, cell_a.bmin.z);
            q3 = Vec3(cell_a.bmax.x, cell_a.bmin.y, cell_a.bmin.z);
            plane_val = cell_a.bmin.z;
            if (st.options.domain_mode == 0 && std::abs(plane_val - st.dom_min.z) < st.dom_tol) {
              is_farfield = true;
              farfield_patch = PatchId::BOTTOM;
            }
          } else {
            n_dir = Vec3(0, 0, 1);
            q0 = Vec3(cell_a.bmin.x, cell_a.bmin.y, cell_a.bmax.z);
            q1 = Vec3(cell_a.bmax.x, cell_a.bmin.y, cell_a.bmax.z);
            q2 = Vec3(cell_a.bmax.x, cell_a.bmax.y, cell_a.bmax.z);
            q3 = Vec3(cell_a.bmin.x, cell_a.bmax.y, cell_a.bmax.z);
            plane_val = cell_a.bmax.z;
            if (st.options.domain_mode == 0 && std::abs(plane_val - st.dom_max.z) < st.dom_tol) {
              is_farfield = true;
              farfield_patch = PatchId::TOP;
            }
          }

          Vec3 fc = (q0 + q1 + q2 + q3) * 0.25;
          Vec3 p_probe = fc + n_dir * st.eps_probe;
          int64_t nb_leaf = st.findLeafAtPoint(p_probe);

          auto emit_quad = [&](const Vec3& p0, const Vec3& p1, const Vec3& p2, const Vec3& p3,
                               int64_t o, int64_t nb, PatchId pid, bool ff, bool cut) {
            local_quads.push_back(RawQuad{{p0, p1, p2, p3}, o, nb, pid, ff, cut});
            push_pt(p0);
            push_pt(p1);
            push_pt(p2);
            push_pt(p3);
          };

          if (nb_leaf < 0) {
            PatchId patch = is_farfield ? farfield_patch : PatchId::WALL;
            emit_quad(q0, q1, q2, q3, c, -1, patch, is_farfield, !is_farfield);
          } else if (st.nodes[static_cast<size_t>(nb_leaf)].level > cell_a.level) {
            st.cell_types[static_cast<size_t>(c)] = CellType::POLYHEDRON;

            Vec3 m01 = (q0 + q1) * 0.5;
            Vec3 m12 = (q1 + q2) * 0.5;
            Vec3 m23 = (q2 + q3) * 0.5;
            Vec3 m30 = (q3 + q0) * 0.5;
            Vec3 mc = (q0 + q1 + q2 + q3) * 0.25;

            std::array<std::array<Vec3, 4>, 4> sub_quads = {{
                {q0, m01, mc, m30},
                {m01, q1, m12, mc},
                {mc, m12, q2, m23},
                {m30, mc, m23, q3},
            }};

            for (int sq = 0; sq < 4; ++sq) {
              Vec3 sub_c = (sub_quads[sq][0] + sub_quads[sq][1] + sub_quads[sq][2] + sub_quads[sq][3]) * 0.25;
              Vec3 sub_probe = sub_c + n_dir * st.eps_probe;
              int64_t fine_leaf = st.findLeafAtPoint(sub_probe);

              if (fine_leaf >= 0 && st.nodes[static_cast<size_t>(fine_leaf)].is_active) {
                int64_t fine_c = st.nodes[static_cast<size_t>(fine_leaf)].cell_index;
                int64_t o = std::min(c, fine_c);
                int64_t nb = std::max(c, fine_c);
                if (o == c) {
                  emit_quad(sub_quads[sq][0], sub_quads[sq][1], sub_quads[sq][2], sub_quads[sq][3],
                            o, nb, PatchId::INTERNAL, false, false);
                } else {
                  emit_quad(sub_quads[sq][0], sub_quads[sq][3], sub_quads[sq][2], sub_quads[sq][1],
                            o, nb, PatchId::INTERNAL, false, false);
                }
              } else {
                PatchId patch = is_farfield ? farfield_patch : PatchId::WALL;
                emit_quad(sub_quads[sq][0], sub_quads[sq][1], sub_quads[sq][2], sub_quads[sq][3],
                          c, -1, patch, is_farfield, !is_farfield);
              }
            }
          } else if (!st.nodes[static_cast<size_t>(nb_leaf)].is_active) {
            PatchId patch = is_farfield ? farfield_patch : PatchId::WALL;
            emit_quad(q0, q1, q2, q3, c, -1, patch, is_farfield, !is_farfield);
          } else if (st.nodes[static_cast<size_t>(nb_leaf)].level < cell_a.level) {
            // Fine cell facing active coarse cell: coarse cell emits 4 sub-quads
          } else {
            int64_t nb_c = st.nodes[static_cast<size_t>(nb_leaf)].cell_index;
            if (c < nb_c) {
              emit_quad(q0, q1, q2, q3, c, nb_c, PatchId::INTERNAL, false, false);
            }
          }
        }

        if (local_verts.size() >= next_compact_threshold) {
          compactKeyCoords(local_verts);
          next_compact_threshold = std::max<size_t>(262144, local_verts.size() * 2);
        }
      }
      compactKeyCoords(local_verts);
#ifdef _OPENMP
    } catch (...) {
      omp_failed.store(true, std::memory_order_relaxed);
    }
#endif
  }
  if (omp_failed.load(std::memory_order_relaxed)) {
    return -12;
  }

  size_t total_local_verts = 0;
  for (const auto& tv : thread_verts) {
    total_local_verts += tv.size();
  }
  std::vector<KeyCoord> all_verts;
  all_verts.reserve(total_local_verts);
  for (auto& tv : thread_verts) {
    all_verts.insert(all_verts.end(), tv.begin(), tv.end());
    std::vector<KeyCoord>().swap(tv);
  }
  std::vector<std::vector<KeyCoord>>().swap(thread_verts);

  compactKeyCoords(all_verts);
  const size_t n_verts = all_verts.size();
  std::vector<QuantizedKey> unique_keys(n_verts);
  st.node_coords.resize(n_verts);
#ifdef _OPENMP
#pragma omp parallel for schedule(static)
#endif
  for (int64_t i = 0; i < static_cast<int64_t>(n_verts); ++i) {
    unique_keys[static_cast<size_t>(i)] = all_verts[static_cast<size_t>(i)].key;
    st.node_coords[static_cast<size_t>(i)] = all_verts[static_cast<size_t>(i)].pt;
  }
  std::vector<KeyCoord>().swap(all_verts);

  st.hex_corners.resize(static_cast<size_t>(n_active_cells));
  st.cell_v_bg.resize(static_cast<size_t>(n_active_cells));
  st.cell_h_min.resize(static_cast<size_t>(n_active_cells));
  st.cell_region.resize(static_cast<size_t>(n_active_cells));
  st.cell_approx_center.resize(static_cast<size_t>(n_active_cells));

#ifdef _OPENMP
#pragma omp parallel for schedule(static)
#endif
  for (int64_t c = 0; c < n_active_cells; ++c) {
    const OctreeNode& onode = st.nodes[static_cast<size_t>(st.active_leaves[static_cast<size_t>(c)])];
    const Vec3& mn = onode.bmin;
    const Vec3& mx = onode.bmax;
    const Vec3 sz = onode.size();
    st.cell_v_bg[static_cast<size_t>(c)] = onode.v_bg();
    st.cell_h_min[static_cast<size_t>(c)] = std::min({sz.x, sz.y, sz.z});
    st.cell_region[static_cast<size_t>(c)] = onode.region;
    st.cell_approx_center[static_cast<size_t>(c)] = onode.center();
    st.hex_corners[static_cast<size_t>(c)] = {
        lookupVertexId(unique_keys, Vec3(mn.x, mn.y, mn.z), st.quant_tol),
        lookupVertexId(unique_keys, Vec3(mx.x, mn.y, mn.z), st.quant_tol),
        lookupVertexId(unique_keys, Vec3(mx.x, mx.y, mn.z), st.quant_tol),
        lookupVertexId(unique_keys, Vec3(mn.x, mx.y, mn.z), st.quant_tol),
        lookupVertexId(unique_keys, Vec3(mn.x, mn.y, mx.z), st.quant_tol),
        lookupVertexId(unique_keys, Vec3(mx.x, mn.y, mx.z), st.quant_tol),
        lookupVertexId(unique_keys, Vec3(mx.x, mx.y, mx.z), st.quant_tol),
        lookupVertexId(unique_keys, Vec3(mn.x, mx.y, mx.z), st.quant_tol),
    };
  }

  // Octree nodes and leaf indices are no longer needed by any subsequent pass
  st.nodes.clear();
  std::vector<int64_t>().swap(st.all_leaves);
  std::vector<int64_t>().swap(st.active_leaves);

  std::vector<size_t> quad_offsets(thread_quads.size() + 1, 0);
  for (size_t t = 0; t < thread_quads.size(); ++t) {
    quad_offsets[t + 1] = quad_offsets[t] + thread_quads[t].size();
  }
  const size_t total_raw_quads = quad_offsets.back();

  st.faces.clear_and_shrink();
  st.faces.node_offsets.resize(total_raw_quads + 1);
  st.faces.node_ids.resize(total_raw_quads * 4);
  st.faces.owner.resize(total_raw_quads);
  st.faces.neighbour.resize(total_raw_quads);
  st.faces.normal_owner.resize(total_raw_quads, Vec3(0, 0, 0));
  st.faces.centre.resize(total_raw_quads, Vec3(0, 0, 0));
  st.faces.patch_id.resize(total_raw_quads);
  st.faces.flags.resize(total_raw_quads);

  for (size_t t = 0; t < thread_quads.size(); ++t) {
    const auto& tq = thread_quads[t];
    const size_t base_f = quad_offsets[t];
    const int64_t tq_sz = static_cast<int64_t>(tq.size());
#ifdef _OPENMP
#pragma omp parallel for schedule(static)
#endif
    for (int64_t q = 0; q < tq_sz; ++q) {
      const RawQuad& rq = tq[static_cast<size_t>(q)];
      const size_t f = base_f + static_cast<size_t>(q);
      st.faces.node_offsets[f] = static_cast<int64_t>(f * 4);
      st.faces.node_ids[f * 4 + 0] = lookupVertexId(unique_keys, rq.pts[0], st.quant_tol);
      st.faces.node_ids[f * 4 + 1] = lookupVertexId(unique_keys, rq.pts[1], st.quant_tol);
      st.faces.node_ids[f * 4 + 2] = lookupVertexId(unique_keys, rq.pts[2], st.quant_tol);
      st.faces.node_ids[f * 4 + 3] = lookupVertexId(unique_keys, rq.pts[3], st.quant_tol);
      st.faces.owner[f] = rq.owner;
      st.faces.neighbour[f] = rq.neighbour;
      st.faces.patch_id[f] = rq.patch_id;
      uint8_t fl = 0;
      if (rq.is_farfield) fl |= kFlagFarfield;
      if (rq.is_cut_face) fl |= kFlagCutFace;
      st.faces.flags[f] = fl;
    }
  }
  st.faces.node_offsets[total_raw_quads] = static_cast<int64_t>(total_raw_quads * 4);
  std::vector<std::vector<RawQuad>>().swap(thread_quads);

  // Parallel conformal midpoint insertion (up to 8 passes; AUD-040)
  bool converged = false;
  for (int split_pass = 0; split_pass < 8; ++split_pass) {
    const int64_t nf = st.faces.size();
    std::vector<int64_t> new_counts(static_cast<size_t>(nf), 0);
    std::vector<uint8_t> face_changed(static_cast<size_t>(nf), 0);
    std::atomic<bool> any_inserted{false};
    std::atomic<bool> pass_omp_failed{false};

#ifdef _OPENMP
#pragma omp parallel for schedule(static)
#endif
    for (int64_t f = 0; f < nf; ++f) {
      if (pass_omp_failed.load(std::memory_order_relaxed)) continue;
      try {
        const size_t nv = st.faces.face_nv(static_cast<size_t>(f));
        const int64_t* fnodes = st.faces.face_nodes(static_cast<size_t>(f));
        int64_t out_nv = static_cast<int64_t>(nv);
        bool changed = false;
        for (size_t i = 0; i < nv; ++i) {
          int64_t u = fnodes[i];
          int64_t w = fnodes[(i + 1) % nv];
          Vec3 mid_pt = (st.node_coords[static_cast<size_t>(u)] + st.node_coords[static_cast<size_t>(w)]) * 0.5;
          int64_t mid_id = lookupVertexId(unique_keys, mid_pt, st.quant_tol);
          if (mid_id >= 0 && mid_id != u && mid_id != w) {
            ++out_nv;
            changed = true;
          }
        }
        new_counts[static_cast<size_t>(f)] = out_nv;
        if (changed) {
          face_changed[static_cast<size_t>(f)] = 1;
          any_inserted.store(true, std::memory_order_relaxed);
        }
      } catch (...) {
        pass_omp_failed.store(true, std::memory_order_relaxed);
      }
    }
    if (pass_omp_failed.load(std::memory_order_relaxed)) {
      return -12;
    }
    if (!any_inserted.load(std::memory_order_relaxed)) {
      converged = true;
      break;
    }

    std::vector<int64_t> next_offsets(static_cast<size_t>(nf) + 1, 0);
    for (int64_t f = 0; f < nf; ++f) {
      next_offsets[static_cast<size_t>(f + 1)] = next_offsets[static_cast<size_t>(f)] + new_counts[static_cast<size_t>(f)];
    }
    std::vector<int64_t> next_node_ids(static_cast<size_t>(next_offsets[static_cast<size_t>(nf)]));

#ifdef _OPENMP
#pragma omp parallel for schedule(static)
#endif
    for (int64_t f = 0; f < nf; ++f) {
      if (pass_omp_failed.load(std::memory_order_relaxed)) continue;
      try {
        const size_t nv = st.faces.face_nv(static_cast<size_t>(f));
        const int64_t* fnodes = st.faces.face_nodes(static_cast<size_t>(f));
        int64_t* dst = next_node_ids.data() + next_offsets[static_cast<size_t>(f)];
        if (!face_changed[static_cast<size_t>(f)]) {
          for (size_t i = 0; i < nv; ++i) dst[i] = fnodes[i];
        } else {
          size_t pos = 0;
          for (size_t i = 0; i < nv; ++i) {
            int64_t u = fnodes[i];
            int64_t w = fnodes[(i + 1) % nv];
            dst[pos++] = u;
            Vec3 mid_pt = (st.node_coords[static_cast<size_t>(u)] + st.node_coords[static_cast<size_t>(w)]) * 0.5;
            int64_t mid_id = lookupVertexId(unique_keys, mid_pt, st.quant_tol);
            if (mid_id >= 0 && mid_id != u && mid_id != w) {
              dst[pos++] = mid_id;
            }
          }
        }
      } catch (...) {
        pass_omp_failed.store(true, std::memory_order_relaxed);
      }
    }
    if (pass_omp_failed.load(std::memory_order_relaxed)) {
      return -12;
    }

    st.faces.node_offsets.swap(next_offsets);
    st.faces.node_ids.swap(next_node_ids);
    for (int64_t f = 0; f < nf; ++f) {
      if (face_changed[static_cast<size_t>(f)]) {
        int64_t o = st.faces.owner[static_cast<size_t>(f)];
        int64_t nb = st.faces.neighbour[static_cast<size_t>(f)];
        if (o >= 0) st.cell_types[static_cast<size_t>(o)] = CellType::POLYHEDRON;
        if (nb >= 0) st.cell_types[static_cast<size_t>(nb)] = CellType::POLYHEDRON;
      }
    }
  }
  if (!converged) {
    return -14;
  }

  std::vector<QuantizedKey>().swap(unique_keys);
  rebuildCellToFacesCSR(st);

  return 0;
}

}  // namespace cfd::hexcore
