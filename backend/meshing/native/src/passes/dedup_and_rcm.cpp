#include "hexcore_state.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>
#include <map>
#include <queue>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace cfd::hexcore {

std::vector<int64_t> computeFastRCM(
    int64_t n_cells,
    const std::vector<int64_t>& internal_face_indices,
    const std::vector<int64_t>& face_owners,
    const std::vector<int64_t>& face_neighbours
) {
  std::vector<int64_t> perm(static_cast<size_t>(n_cells));
  for (int64_t i = 0; i < n_cells; ++i) perm[static_cast<size_t>(i)] = i;
  if (n_cells <= 1 || internal_face_indices.empty()) return perm;

  std::vector<int64_t> deg(static_cast<size_t>(n_cells), 0);
  for (int64_t f : internal_face_indices) {
    const int64_t o = face_owners[static_cast<size_t>(f)];
    const int64_t nb = face_neighbours[static_cast<size_t>(f)];
    if (o >= 0 && nb >= 0 && o != nb) {
      ++deg[static_cast<size_t>(o)];
      ++deg[static_cast<size_t>(nb)];
    }
  }

  std::vector<int64_t> offsets(static_cast<size_t>(n_cells) + 1, 0);
  for (int64_t c = 0; c < n_cells; ++c) {
    offsets[static_cast<size_t>(c + 1)] = offsets[static_cast<size_t>(c)] + deg[static_cast<size_t>(c)];
  }
  std::vector<int64_t> adj(static_cast<size_t>(offsets[static_cast<size_t>(n_cells)]));
  std::vector<int64_t> cursor = offsets;
  for (int64_t f : internal_face_indices) {
    const int64_t o = face_owners[static_cast<size_t>(f)];
    const int64_t nb = face_neighbours[static_cast<size_t>(f)];
    if (o >= 0 && nb >= 0 && o != nb) {
      adj[static_cast<size_t>(cursor[static_cast<size_t>(o)]++)] = nb;
      adj[static_cast<size_t>(cursor[static_cast<size_t>(nb)]++)] = o;
    }
  }

  for (int64_t c = 0; c < n_cells; ++c) {
    auto beg = adj.begin() + offsets[static_cast<size_t>(c)];
    auto end = adj.begin() + offsets[static_cast<size_t>(c + 1)];
    std::sort(beg, end);
    auto u_end = std::unique(beg, end);
    deg[static_cast<size_t>(c)] = static_cast<int64_t>(u_end - beg);
  }

  for (int64_t c = 0; c < n_cells; ++c) {
    auto beg = adj.begin() + offsets[static_cast<size_t>(c)];
    auto end = beg + deg[static_cast<size_t>(c)];
    std::sort(beg, end, [&](int64_t a, int64_t b) {
      if (deg[static_cast<size_t>(a)] != deg[static_cast<size_t>(b)]) {
        return deg[static_cast<size_t>(a)] < deg[static_cast<size_t>(b)];
      }
      return a < b;
    });
  }

  std::vector<uint8_t> visited(static_cast<size_t>(n_cells), 0);
  std::vector<int64_t> cm_order;
  cm_order.reserve(static_cast<size_t>(n_cells));
  std::queue<int64_t> q;

  while (static_cast<int64_t>(cm_order.size()) < n_cells) {
    int64_t start_node = -1;
    int64_t min_deg = (std::numeric_limits<int64_t>::max)();
    for (int64_t c = 0; c < n_cells; ++c) {
      if (!visited[static_cast<size_t>(c)] && deg[static_cast<size_t>(c)] < min_deg) {
        min_deg = deg[static_cast<size_t>(c)];
        start_node = c;
      }
    }
    if (start_node < 0) break;

    visited[static_cast<size_t>(start_node)] = 1;
    q.push(start_node);
    while (!q.empty()) {
      int64_t u = q.front();
      q.pop();
      cm_order.push_back(u);
      int64_t beg = offsets[static_cast<size_t>(u)];
      int64_t end = beg + deg[static_cast<size_t>(u)];
      for (int64_t idx = beg; idx < end; ++idx) {
        int64_t v = adj[static_cast<size_t>(idx)];
        if (!visited[static_cast<size_t>(v)]) {
          visited[static_cast<size_t>(v)] = 1;
          q.push(v);
        }
      }
    }
  }

  for (int64_t i = 0; i < n_cells; ++i) {
    perm[static_cast<size_t>(i)] = cm_order[static_cast<size_t>(n_cells - 1 - i)];
  }
  return perm;
}

int32_t passDeduplicateAndRcm(HexcoreState& st) {
  const int64_t n_src_faces = st.faces.size();
  std::vector<int64_t> raw_int_idx;
  std::vector<int64_t> bnd_idx;
  raw_int_idx.reserve(static_cast<size_t>(n_src_faces));
  bnd_idx.reserve(static_cast<size_t>(n_src_faces) / 4 + 64);

  for (int64_t f = 0; f < n_src_faces; ++f) {
    const size_t fs = static_cast<size_t>(f);
    const int64_t old_o = st.faces.owner[fs];
    const int64_t old_nb = st.faces.neighbour[fs];
    if (old_o < 0 && old_nb < 0) continue;

    int64_t new_o = (old_o >= 0) ? st.old_to_new_cell[static_cast<size_t>(old_o)] : -1;
    int64_t new_nb = (old_nb >= 0) ? st.old_to_new_cell[static_cast<size_t>(old_nb)] : -1;

    if (new_o >= 0 && new_nb >= 0) {
      if (new_o == new_nb) {
        st.faces.owner[fs] = -1;
        st.faces.neighbour[fs] = -1;
        continue;
      }
      if (new_o > new_nb) {
        std::swap(new_o, new_nb);
        st.faces.normal_owner[fs] = -st.faces.normal_owner[fs];
        int64_t* fn = st.faces.face_nodes_mut(fs);
        std::reverse(fn, fn + st.faces.face_nv(fs));
      }
      st.faces.owner[fs] = new_o;
      st.faces.neighbour[fs] = new_nb;
      raw_int_idx.push_back(f);
    } else if (new_o >= 0 && new_nb < 0) {
      st.faces.owner[fs] = new_o;
      st.faces.neighbour[fs] = -1;
      bnd_idx.push_back(f);
    } else if (new_nb >= 0 && new_o < 0) {
      st.faces.owner[fs] = new_nb;
      st.faces.neighbour[fs] = -1;
      st.faces.normal_owner[fs] = -st.faces.normal_owner[fs];
      int64_t* fn = st.faces.face_nodes_mut(fs);
      std::reverse(fn, fn + st.faces.face_nv(fs));
      bnd_idx.push_back(f);
    } else {
      st.faces.owner[fs] = -1;
      st.faces.neighbour[fs] = -1;
    }
  }
  std::vector<int64_t>().swap(st.old_to_new_cell);

  const size_t n_raw_int = raw_int_idx.size();
  std::sort(raw_int_idx.begin(), raw_int_idx.end(),
            [&](int64_t a, int64_t b) {
              const int64_t oa = st.faces.owner[static_cast<size_t>(a)];
              const int64_t ob = st.faces.owner[static_cast<size_t>(b)];
              if (oa != ob) return oa < ob;
              const int64_t nba = st.faces.neighbour[static_cast<size_t>(a)];
              const int64_t nbb = st.faces.neighbour[static_cast<size_t>(b)];
              if (nba != nbb) return nba < nbb;
              return a < b;
            });

  std::vector<int64_t> final_int_idx;
  final_int_idx.reserve(n_raw_int);

  for (size_t idx = 0; idx < n_raw_int;) {
    const size_t first_f = static_cast<size_t>(raw_int_idx[idx]);
    const int64_t run_o = st.faces.owner[first_f];
    const int64_t run_nb = st.faces.neighbour[first_f];
    size_t run_end = idx + 1;
    while (run_end < n_raw_int) {
      const size_t rf = static_cast<size_t>(raw_int_idx[run_end]);
      if (st.faces.owner[rf] != run_o || st.faces.neighbour[rf] != run_nb) break;
      ++run_end;
    }

    if (run_end - idx == 1) {
      final_int_idx.push_back(static_cast<int64_t>(first_f));
    } else {
      bool all_coplanar = true;
      const Vec3 n0 = st.faces.normal_owner[first_f];
      const Vec3 c0 = st.faces.centre[first_f];
      const double a0 = n0.norm();
      if (a0 <= 1e-24) {
        all_coplanar = false;
      } else {
        const Vec3 u0 = n0 / a0;
        const double plane_tol = 1e-11 * std::sqrt(a0 * static_cast<double>(run_end - idx));
        for (size_t g = idx; g < run_end && all_coplanar; ++g) {
          const size_t gf = static_cast<size_t>(raw_int_idx[g]);
          const Vec3 nk = st.faces.normal_owner[gf];
          const double ak = nk.norm();
          if (ak <= 1e-24 || dot(nk / ak, u0) < 1.0 - 1e-10) {
            all_coplanar = false;
            break;
          }
          const int64_t* fn = st.faces.face_nodes(gf);
          const size_t nv = st.faces.face_nv(gf);
          for (size_t k = 0; k < nv; ++k) {
            if (std::abs(dot(st.node_coords[static_cast<size_t>(fn[k])] - c0, u0)) > plane_tol) {
              all_coplanar = false;
              break;
            }
          }
        }
      }

      bool merged_ok = false;
      if (all_coplanar) {
        Vec3 sum_normal(0, 0, 0);
        Vec3 sum_centre(0, 0, 0);
        double sum_area = 0.0;
        bool any_folded = false;

        std::map<std::pair<int64_t, int64_t>, int> edge_counts;
        for (size_t g = idx; g < run_end; ++g) {
          const size_t gf = static_cast<size_t>(raw_int_idx[g]);
          const Vec3 fn_vec = st.faces.normal_owner[gf];
          const double a = fn_vec.norm();
          sum_normal += fn_vec;
          sum_centre += st.faces.centre[gf] * a;
          sum_area += a;
          if ((st.faces.flags[gf] & kFlagFolded) != 0) any_folded = true;

          const int64_t* fnodes = st.faces.face_nodes(gf);
          const size_t nv = st.faces.face_nv(gf);
          for (size_t i = 0; i < nv; ++i) {
            edge_counts[{fnodes[i], fnodes[(i + 1) % nv]}]++;
          }
        }

        std::vector<std::pair<int64_t, int64_t>> perimeter_edges;
        for (const auto& [edge, count] : edge_counts) {
          const int64_t u = edge.first;
          const int64_t v = edge.second;
          auto it_rev = edge_counts.find({v, u});
          const int rev_count = (it_rev != edge_counts.end()) ? it_rev->second : 0;
          if (count > rev_count) {
            for (int k = 0; k < count - rev_count; ++k) {
              perimeter_edges.push_back({u, v});
            }
          }
        }

        // Strict single-cycle perimeter validation (AUD-028)
        bool valid_cycle = (perimeter_edges.size() >= 3);
        std::vector<int64_t> cycle;
        if (valid_cycle) {
          std::unordered_map<int64_t, int> out_deg;
          std::unordered_map<int64_t, int> in_deg;
          std::unordered_map<int64_t, int64_t> next_node;
          out_deg.reserve(perimeter_edges.size());
          in_deg.reserve(perimeter_edges.size());
          next_node.reserve(perimeter_edges.size());

          for (const auto& e : perimeter_edges) {
            if (++out_deg[e.first] > 1 || ++in_deg[e.second] > 1) {
              valid_cycle = false;
              break;
            }
            next_node[e.first] = e.second;
          }

          if (valid_cycle) {
            for (const auto& [v, od] : out_deg) {
              auto it_in = in_deg.find(v);
              if (od != 1 || it_in == in_deg.end() || it_in->second != 1) {
                valid_cycle = false;
                break;
              }
            }
            for (const auto& [v, id] : in_deg) {
              auto it_out = out_deg.find(v);
              if (id != 1 || it_out == out_deg.end() || it_out->second != 1) {
                valid_cycle = false;
                break;
              }
            }
          }

          if (valid_cycle) {
            const int64_t start = perimeter_edges[0].first;
            int64_t curr = start;
            std::unordered_set<int64_t> visited;
            visited.reserve(perimeter_edges.size());
            cycle.reserve(perimeter_edges.size());
            while (visited.insert(curr).second) {
              cycle.push_back(curr);
              auto it = next_node.find(curr);
              if (it == next_node.end()) break;
              curr = it->second;
              if (curr == start) break;
            }
            if (cycle.size() != perimeter_edges.size() || cycle.size() < 3 || curr != start) {
              valid_cycle = false;
            }
          }
        }

        if (valid_cycle) {
          const Vec3 merged_centre = (sum_area > 1e-15) ? (sum_centre / sum_area) : c0;
          const uint8_t merged_fl = any_folded ? kFlagFolded : 0;
          const int64_t new_f = st.faces.append_face(
              cycle.data(),
              cycle.size(),
              run_o,
              run_nb,
              PatchId::INTERNAL,
              merged_fl,
              sum_normal,
              merged_centre
          );
          final_int_idx.push_back(new_f);
          merged_ok = true;
        }
      }

      if (!merged_ok) {
        for (size_t g = idx; g < run_end; ++g) {
          final_int_idx.push_back(raw_int_idx[g]);
        }
      }
    }
    idx = run_end;
  }
  std::vector<int64_t>().swap(raw_int_idx);

  if (st.options.enable_rcm && st.final_n_cells > 1) {
    std::vector<int64_t> perm = computeFastRCM(
        st.final_n_cells, final_int_idx, st.faces.owner, st.faces.neighbour
    );
    std::vector<int64_t> inv_perm(static_cast<size_t>(st.final_n_cells));
    std::vector<int64_t> rcm_to_old_active(static_cast<size_t>(st.final_n_cells));
    for (int64_t new_c = 0; new_c < st.final_n_cells; ++new_c) {
      int64_t old_final_c = perm[static_cast<size_t>(new_c)];
      inv_perm[static_cast<size_t>(old_final_c)] = new_c;
      rcm_to_old_active[static_cast<size_t>(new_c)] = st.new_to_old_active[static_cast<size_t>(old_final_c)];
    }
    st.new_to_old_active.swap(rcm_to_old_active);

    for (int64_t f : final_int_idx) {
      const size_t fs = static_cast<size_t>(f);
      int64_t o = inv_perm[static_cast<size_t>(st.faces.owner[fs])];
      int64_t nb = inv_perm[static_cast<size_t>(st.faces.neighbour[fs])];
      if (o > nb) {
        std::swap(o, nb);
        st.faces.normal_owner[fs] = -st.faces.normal_owner[fs];
        int64_t* fn = st.faces.face_nodes_mut(fs);
        std::reverse(fn, fn + st.faces.face_nv(fs));
      }
      st.faces.owner[fs] = o;
      st.faces.neighbour[fs] = nb;
    }

    std::sort(final_int_idx.begin(), final_int_idx.end(),
              [&](int64_t a, int64_t b) {
                const int64_t oa = st.faces.owner[static_cast<size_t>(a)];
                const int64_t ob = st.faces.owner[static_cast<size_t>(b)];
                if (oa != ob) return oa < ob;
                const int64_t nba = st.faces.neighbour[static_cast<size_t>(a)];
                const int64_t nbb = st.faces.neighbour[static_cast<size_t>(b)];
                if (nba != nbb) return nba < nbb;
                return a < b;
              });

    for (int64_t f : bnd_idx) {
      const size_t fs = static_cast<size_t>(f);
      st.faces.owner[fs] = inv_perm[static_cast<size_t>(st.faces.owner[fs])];
    }
  }

  for (int64_t f : bnd_idx) {
    const size_t fs = static_cast<size_t>(f);
    if (st.faces.patch_id[fs] == PatchId::INTERNAL) {
      st.faces.patch_id[fs] = PatchId::WALL;
    }
  }
  std::stable_sort(bnd_idx.begin(), bnd_idx.end(),
                   [&](int64_t a, int64_t b) {
                     return std::strcmp(
                         patchIdToName(st.faces.patch_id[static_cast<size_t>(a)]),
                         patchIdToName(st.faces.patch_id[static_cast<size_t>(b)])
                     ) < 0;
                   });

  const size_t n_final_total = final_int_idx.size() + bnd_idx.size();
  size_t total_node_ids = 0;
  for (int64_t f : final_int_idx) total_node_ids += st.faces.face_nv(static_cast<size_t>(f));
  for (int64_t f : bnd_idx) total_node_ids += st.faces.face_nv(static_cast<size_t>(f));

  FaceStore final_faces;
  final_faces.reserve(n_final_total, total_node_ids);
  for (int64_t f : final_int_idx) {
    const size_t fs = static_cast<size_t>(f);
    final_faces.append_face(
        st.faces.face_nodes(fs),
        st.faces.face_nv(fs),
        st.faces.owner[fs],
        st.faces.neighbour[fs],
        st.faces.patch_id[fs],
        st.faces.flags[fs],
        st.faces.normal_owner[fs],
        st.faces.centre[fs]
    );
  }
  for (int64_t f : bnd_idx) {
    const size_t fs = static_cast<size_t>(f);
    final_faces.append_face(
        st.faces.face_nodes(fs),
        st.faces.face_nv(fs),
        st.faces.owner[fs],
        st.faces.neighbour[fs],
        st.faces.patch_id[fs],
        st.faces.flags[fs],
        st.faces.normal_owner[fs],
        st.faces.centre[fs]
    );
  }

  st.n_internal = static_cast<int64_t>(final_int_idx.size());
  st.total_faces = static_cast<int64_t>(n_final_total);

  std::vector<int64_t>().swap(final_int_idx);
  std::vector<int64_t>().swap(bnd_idx);
  st.faces.clear_and_shrink();
  st.faces = std::move(final_faces);

  return 0;
}

}  // namespace cfd::hexcore
