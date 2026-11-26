#include "hexcore_state.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <string>
#include <vector>

#ifdef _OPENMP
#include <omp.h>
#endif

namespace cfd::hexcore {

int32_t passExtractFacesAndVertices(HexcoreState& st) {
  int64_t n_active_cells = st.n_active_cells;
  st.cell_types.assign(static_cast<size_t>(n_active_cells), CellType::HEXAHEDRON);
  std::vector<std::vector<RawQuad>> thread_quads(static_cast<size_t>(st.max_threads));
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
      if (c_end > c_beg) {
        local_quads.reserve(static_cast<size_t>(c_end - c_beg) * 4);
      }
#else
  {
    int64_t c_beg = 0;
    int64_t c_end = n_active_cells;
    auto& local_quads = thread_quads[0];
    local_quads.reserve(static_cast<size_t>(n_active_cells) * 4);
#endif
      for (int64_t c = c_beg; c < c_end; ++c) {
        int64_t node_idx = st.active_leaves[static_cast<size_t>(c)];
        const OctreeNode& cell_a = st.nodes[static_cast<size_t>(node_idx)];

        for (int dir = 0; dir < 6; ++dir) {
          Vec3 n_dir;
          Vec3 q0, q1, q2, q3;
          double plane_val = 0.0;
          bool is_farfield = false;
          std::string farfield_patch;

          if (dir == 0) {
            n_dir = Vec3(-1, 0, 0);
            q0 = Vec3(cell_a.bmin.x, cell_a.bmin.y, cell_a.bmin.z);
            q1 = Vec3(cell_a.bmin.x, cell_a.bmin.y, cell_a.bmax.z);
            q2 = Vec3(cell_a.bmin.x, cell_a.bmax.y, cell_a.bmax.z);
            q3 = Vec3(cell_a.bmin.x, cell_a.bmax.y, cell_a.bmin.z);
            plane_val = cell_a.bmin.x;
            if (st.options.domain_mode == 0 && std::abs(plane_val - st.dom_min.x) < st.dom_tol) {
              is_farfield = true;
              farfield_patch = "inlet";
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
              farfield_patch = "outlet";
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
              farfield_patch = "sides";
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
              farfield_patch = "sides";
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
              farfield_patch = "bottom";
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
              farfield_patch = "top";
            }
          }

          Vec3 fc = (q0 + q1 + q2 + q3) * 0.25;
          Vec3 p_probe = fc + n_dir * st.eps_probe;
          int64_t nb_leaf = st.findLeafAtPoint(p_probe);

          if (nb_leaf < 0) {
            std::string patch = is_farfield ? farfield_patch : "wall";
            local_quads.push_back({{q0, q1, q2, q3}, c, -1, patch, is_farfield, !is_farfield});
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
                  local_quads.push_back({{sub_quads[sq][0], sub_quads[sq][1], sub_quads[sq][2], sub_quads[sq][3]}, o, nb, "", false, false});
                } else {
                  local_quads.push_back({{sub_quads[sq][0], sub_quads[sq][3], sub_quads[sq][2], sub_quads[sq][1]}, o, nb, "", false, false});
                }
              } else {
                std::string patch = is_farfield ? farfield_patch : "wall";
                local_quads.push_back({{sub_quads[sq][0], sub_quads[sq][1], sub_quads[sq][2], sub_quads[sq][3]}, c, -1, patch, is_farfield, !is_farfield});
              }
            }
          } else if (!st.nodes[static_cast<size_t>(nb_leaf)].is_active) {
            std::string patch = is_farfield ? farfield_patch : "wall";
            local_quads.push_back({{q0, q1, q2, q3}, c, -1, patch, is_farfield, !is_farfield});
          } else if (st.nodes[static_cast<size_t>(nb_leaf)].level < cell_a.level) {
            // Fine cell facing active coarse cell: coarse cell emits 4 sub-quads
          } else {
            int64_t nb_c = st.nodes[static_cast<size_t>(nb_leaf)].cell_index;
            if (c < nb_c) {
              local_quads.push_back({{q0, q1, q2, q3}, c, nb_c, "", false, false});
            }
          }
        }
      }
#ifdef _OPENMP
    } catch (...) {
      omp_failed.store(true, std::memory_order_relaxed);
    }
#endif
  }
  if (omp_failed.load(std::memory_order_relaxed)) {
    return -12;
  }

  st.vmap.clear();
  st.vmap.reserve(static_cast<size_t>(n_active_cells) * 2);
  st.node_coords.clear();
  st.node_coords.reserve(static_cast<size_t>(n_active_cells) * 2);

  auto get_or_add_vertex = [&](const Vec3& pt) -> int64_t {
    QuantizedKey k = quantize(pt, st.quant_tol);
    auto it = st.vmap.find(k);
    if (it != st.vmap.end()) {
      return it->second;
    }
    int64_t id = static_cast<int64_t>(st.node_coords.size());
    st.node_coords.push_back(pt);
    st.vmap[k] = id;
    return id;
  };

  st.hex_corners.resize(static_cast<size_t>(n_active_cells));
  for (int64_t c = 0; c < n_active_cells; ++c) {
    const OctreeNode& onode = st.nodes[static_cast<size_t>(st.active_leaves[static_cast<size_t>(c)])];
    const Vec3& mn = onode.bmin;
    const Vec3& mx = onode.bmax;
    st.hex_corners[static_cast<size_t>(c)] = {
        get_or_add_vertex(Vec3(mn.x, mn.y, mn.z)),
        get_or_add_vertex(Vec3(mx.x, mn.y, mn.z)),
        get_or_add_vertex(Vec3(mx.x, mx.y, mn.z)),
        get_or_add_vertex(Vec3(mn.x, mx.y, mn.z)),
        get_or_add_vertex(Vec3(mn.x, mn.y, mx.z)),
        get_or_add_vertex(Vec3(mx.x, mn.y, mx.z)),
        get_or_add_vertex(Vec3(mx.x, mx.y, mx.z)),
        get_or_add_vertex(Vec3(mn.x, mx.y, mx.z)),
    };
  }

  size_t total_raw_quads = 0;
  for (const auto& tq : thread_quads) total_raw_quads += tq.size();

  st.mosaic_faces.clear();
  st.mosaic_faces.reserve(total_raw_quads);
  st.cell_to_faces.assign(static_cast<size_t>(n_active_cells), {});

  for (const auto& tq : thread_quads) {
    for (const auto& rq : tq) {
      MosaicFace mf;
      mf.nodes = {
          get_or_add_vertex(rq.pts[0]),
          get_or_add_vertex(rq.pts[1]),
          get_or_add_vertex(rq.pts[2]),
          get_or_add_vertex(rq.pts[3]),
      };
      mf.owner = rq.owner;
      mf.neighbour = rq.neighbour;
      mf.patch_name = rq.patch_name;
      mf.is_farfield = rq.is_farfield;
      mf.is_cut_face = rq.is_cut_face;

      int64_t f_idx = static_cast<int64_t>(st.mosaic_faces.size());
      st.mosaic_faces.push_back(std::move(mf));
      st.cell_to_faces[static_cast<size_t>(rq.owner)].push_back(f_idx);
      if (rq.neighbour >= 0) {
        st.cell_to_faces[static_cast<size_t>(rq.neighbour)].push_back(f_idx);
      }
    }
  }

  for (int split_pass = 0; split_pass < 3; ++split_pass) {
    bool any_inserted = false;
    for (auto& mf : st.mosaic_faces) {
      std::vector<int64_t> refined_nodes;
      refined_nodes.reserve(mf.nodes.size() * 2);
      size_t nv = mf.nodes.size();
      bool face_changed = false;
      for (size_t i = 0; i < nv; ++i) {
        int64_t u = mf.nodes[i];
        int64_t w = mf.nodes[(i + 1) % nv];
        refined_nodes.push_back(u);
        Vec3 mid_pt = (st.node_coords[static_cast<size_t>(u)] + st.node_coords[static_cast<size_t>(w)]) * 0.5;
        auto it_mid = st.vmap.find(quantize(mid_pt, st.quant_tol));
        if (it_mid != st.vmap.end() && it_mid->second != u && it_mid->second != w) {
          refined_nodes.push_back(it_mid->second);
          face_changed = true;
          any_inserted = true;
        }
      }
      if (face_changed) {
        mf.nodes.swap(refined_nodes);
        if (mf.owner >= 0) {
          st.cell_types[static_cast<size_t>(mf.owner)] = CellType::POLYHEDRON;
        }
        if (mf.neighbour >= 0) {
          st.cell_types[static_cast<size_t>(mf.neighbour)] = CellType::POLYHEDRON;
        }
      }
    }
    if (!any_inserted) break;
  }

  return 0;
}

}  // namespace cfd::hexcore
