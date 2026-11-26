#include "hexcore_state.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <map>
#include <queue>
#include <set>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace cfd::hexcore {

std::vector<int64_t> computeFastRCM(int64_t n_cells, const std::vector<MosaicFace>& internal_faces) {
  std::vector<int64_t> perm(static_cast<size_t>(n_cells));
  for (int64_t i = 0; i < n_cells; ++i) perm[static_cast<size_t>(i)] = i;
  if (n_cells <= 1 || internal_faces.empty()) return perm;

  std::vector<int64_t> deg(static_cast<size_t>(n_cells), 0);
  for (const auto& mf : internal_faces) {
    if (mf.owner >= 0 && mf.neighbour >= 0 && mf.owner != mf.neighbour) {
      ++deg[static_cast<size_t>(mf.owner)];
      ++deg[static_cast<size_t>(mf.neighbour)];
    }
  }

  std::vector<int64_t> offsets(static_cast<size_t>(n_cells) + 1, 0);
  for (int64_t c = 0; c < n_cells; ++c) {
    offsets[static_cast<size_t>(c + 1)] = offsets[static_cast<size_t>(c)] + deg[static_cast<size_t>(c)];
  }
  std::vector<int64_t> adj(static_cast<size_t>(offsets[static_cast<size_t>(n_cells)]));
  std::vector<int64_t> cursor = offsets;
  for (const auto& mf : internal_faces) {
    int64_t o = mf.owner;
    int64_t nb = mf.neighbour;
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
  std::vector<MosaicFace> raw_internal_faces;
  raw_internal_faces.reserve(st.mosaic_faces.size());
  st.final_boundary_faces.clear();
  st.final_boundary_faces.reserve(st.mosaic_faces.size() / 4 + 64);

  for (auto& mf : st.mosaic_faces) {
    if (mf.owner < 0 && mf.neighbour < 0) continue;

    int64_t new_o = (mf.owner >= 0) ? st.old_to_new_cell[static_cast<size_t>(mf.owner)] : -1;
    int64_t new_nb = (mf.neighbour >= 0) ? st.old_to_new_cell[static_cast<size_t>(mf.neighbour)] : -1;

    if (new_o >= 0 && new_nb >= 0) {
      if (new_o == new_nb) {
        continue;
      }
      if (new_o > new_nb) {
        std::swap(new_o, new_nb);
        mf.normal_owner = -mf.normal_owner;
        std::reverse(mf.nodes.begin(), mf.nodes.end());
      }
      mf.owner = new_o;
      mf.neighbour = new_nb;
      raw_internal_faces.push_back(std::move(mf));
    } else if (new_o >= 0 && new_nb < 0) {
      mf.owner = new_o;
      mf.neighbour = -1;
      st.final_boundary_faces.push_back(std::move(mf));
    } else if (new_nb >= 0 && new_o < 0) {
      mf.owner = new_nb;
      mf.neighbour = -1;
      mf.normal_owner = -mf.normal_owner;
      std::reverse(mf.nodes.begin(), mf.nodes.end());
      st.final_boundary_faces.push_back(std::move(mf));
    }
  }

  size_t n_raw_int = raw_internal_faces.size();
  std::vector<size_t> raw_order(n_raw_int);
  for (size_t i = 0; i < n_raw_int; ++i) raw_order[i] = i;
  std::sort(raw_order.begin(), raw_order.end(),
            [&](size_t a, size_t b) {
              if (raw_internal_faces[a].owner != raw_internal_faces[b].owner) {
                return raw_internal_faces[a].owner < raw_internal_faces[b].owner;
              }
              return raw_internal_faces[a].neighbour < raw_internal_faces[b].neighbour;
            });

  st.final_internal_faces.clear();
  st.final_internal_faces.reserve(n_raw_int);
  for (size_t idx = 0; idx < n_raw_int;) {
    size_t first_i = raw_order[idx];
    size_t run_end = idx + 1;
    while (run_end < n_raw_int &&
           raw_internal_faces[raw_order[run_end]].owner == raw_internal_faces[first_i].owner &&
           raw_internal_faces[raw_order[run_end]].neighbour == raw_internal_faces[first_i].neighbour) {
      ++run_end;
    }
    if (run_end - idx == 1) {
      st.final_internal_faces.push_back(raw_internal_faces[first_i]);
    } else {
      bool all_coplanar = true;
      Vec3 n0 = raw_internal_faces[first_i].normal_owner;
      Vec3 c0 = raw_internal_faces[first_i].centre;
      double a0 = n0.norm();
      if (a0 <= 1e-24) {
        all_coplanar = false;
      } else {
        Vec3 u0 = n0 / a0;
        double plane_tol = 1e-11 * std::sqrt(a0 * static_cast<double>(run_end - idx));
        for (size_t g = idx; g < run_end && all_coplanar; ++g) {
          const auto& fk = raw_internal_faces[raw_order[g]];
          double ak = fk.normal_owner.norm();
          if (ak <= 1e-24 || dot(fk.normal_owner / ak, u0) < 1.0 - 1e-10) {
            all_coplanar = false;
            break;
          }
          for (int64_t vid : fk.nodes) {
            if (std::abs(dot(st.node_coords[static_cast<size_t>(vid)] - c0, u0)) > plane_tol) {
              all_coplanar = false;
              break;
            }
          }
        }
      }

      if (!all_coplanar) {
        for (size_t g = idx; g < run_end; ++g) {
          st.final_internal_faces.push_back(raw_internal_faces[raw_order[g]]);
        }
      } else {
        MosaicFace merged;
        merged.owner = raw_internal_faces[first_i].owner;
        merged.neighbour = raw_internal_faces[first_i].neighbour;
        merged.patch_name = "internal";
        merged.is_farfield = false;
        merged.is_cut_face = false;
        merged.is_folded = false;

        Vec3 sum_normal(0, 0, 0);
        Vec3 sum_centre(0, 0, 0);
        double sum_area = 0.0;

        std::map<std::pair<int64_t, int64_t>, int> edge_counts;
        for (size_t g = idx; g < run_end; ++g) {
          const auto& f = raw_internal_faces[raw_order[g]];
          sum_normal += f.normal_owner;
          double a = f.normal_owner.norm();
          sum_centre += f.centre * a;
          sum_area += a;
          if (f.is_folded) merged.is_folded = true;

          size_t nv = f.nodes.size();
          for (size_t i = 0; i < nv; ++i) {
            int64_t u = f.nodes[i];
            int64_t v = f.nodes[(i + 1) % nv];
            edge_counts[{u, v}]++;
          }
        }

        merged.normal_owner = sum_normal;
        if (sum_area > 1e-15) {
          merged.centre = sum_centre / sum_area;
        } else {
          merged.centre = raw_internal_faces[first_i].centre;
        }

        std::vector<std::pair<int64_t, int64_t>> perimeter_edges;
        for (const auto& [edge, count] : edge_counts) {
          int64_t u = edge.first;
          int64_t v = edge.second;
          auto it_rev = edge_counts.find({v, u});
          int rev_count = (it_rev != edge_counts.end()) ? it_rev->second : 0;
          if (count > rev_count) {
            for (int k = 0; k < count - rev_count; ++k) {
              perimeter_edges.push_back({u, v});
            }
          }
        }

        std::vector<int64_t> cycle;
        if (!perimeter_edges.empty()) {
          std::unordered_map<int64_t, int64_t> next_node;
          for (const auto& e : perimeter_edges) {
            next_node[e.first] = e.second;
          }
          int64_t start = perimeter_edges[0].first;
          int64_t curr = start;
          std::unordered_set<int64_t> visited;
          while (visited.insert(curr).second) {
            cycle.push_back(curr);
            auto it = next_node.find(curr);
            if (it == next_node.end()) break;
            curr = it->second;
            if (curr == start) break;
          }
        }

        if (cycle.size() >= 3) {
          merged.nodes = std::move(cycle);
        } else {
          std::set<int64_t> all_n;
          for (size_t g = idx; g < run_end; ++g) {
            for (int64_t v : raw_internal_faces[raw_order[g]].nodes) all_n.insert(v);
          }
          merged.nodes.assign(all_n.begin(), all_n.end());
        }

        st.final_internal_faces.push_back(std::move(merged));
      }
    }
    idx = run_end;
  }

  if (st.options.enable_rcm && st.final_n_cells > 1) {
    std::vector<int64_t> perm = computeFastRCM(st.final_n_cells, st.final_internal_faces);
    std::vector<int64_t> inv_perm(static_cast<size_t>(st.final_n_cells));
    std::vector<int64_t> rcm_to_old_active(static_cast<size_t>(st.final_n_cells));
    for (int64_t new_c = 0; new_c < st.final_n_cells; ++new_c) {
      int64_t old_final_c = perm[static_cast<size_t>(new_c)];
      inv_perm[static_cast<size_t>(old_final_c)] = new_c;
      rcm_to_old_active[static_cast<size_t>(new_c)] = st.new_to_old_active[static_cast<size_t>(old_final_c)];
    }
    st.new_to_old_active.swap(rcm_to_old_active);

    size_t n_int_f = st.final_internal_faces.size();
    std::vector<size_t> f_order(n_int_f);
    for (size_t f = 0; f < n_int_f; ++f) {
      MosaicFace& mf = st.final_internal_faces[f];
      int64_t o = inv_perm[static_cast<size_t>(mf.owner)];
      int64_t nb = inv_perm[static_cast<size_t>(mf.neighbour)];
      if (o > nb) {
        std::swap(o, nb);
        mf.normal_owner = -mf.normal_owner;
        std::reverse(mf.nodes.begin(), mf.nodes.end());
      }
      mf.owner = o;
      mf.neighbour = nb;
      f_order[f] = f;
    }

    std::sort(f_order.begin(), f_order.end(),
              [&](size_t a, size_t b) {
                if (st.final_internal_faces[a].owner != st.final_internal_faces[b].owner) {
                  return st.final_internal_faces[a].owner < st.final_internal_faces[b].owner;
                }
                return st.final_internal_faces[a].neighbour < st.final_internal_faces[b].neighbour;
              });

    std::vector<MosaicFace> sorted_int_faces;
    sorted_int_faces.reserve(n_int_f);
    for (size_t idx_f : f_order) {
      sorted_int_faces.push_back(st.final_internal_faces[idx_f]);
    }
    st.final_internal_faces.swap(sorted_int_faces);

    for (size_t f = 0; f < st.final_boundary_faces.size(); ++f) {
      MosaicFace& mf = st.final_boundary_faces[f];
      mf.owner = inv_perm[static_cast<size_t>(mf.owner)];
    }
  }

  size_t n_bnd_f = st.final_boundary_faces.size();
  std::vector<size_t> bnd_order(n_bnd_f);
  for (size_t f = 0; f < n_bnd_f; ++f) {
    if (st.final_boundary_faces[f].patch_name.empty()) {
      st.final_boundary_faces[f].patch_name = "wall";
    }
    bnd_order[f] = f;
  }
  std::stable_sort(bnd_order.begin(), bnd_order.end(),
                   [&](size_t a, size_t b) {
                     return st.final_boundary_faces[a].patch_name < st.final_boundary_faces[b].patch_name;
                   });
  std::vector<MosaicFace> sorted_bnd_faces;
  sorted_bnd_faces.reserve(n_bnd_f);
  for (size_t idx_f : bnd_order) {
    sorted_bnd_faces.push_back(st.final_boundary_faces[idx_f]);
  }
  st.final_boundary_faces.swap(sorted_bnd_faces);

  st.all_faces.clear();
  st.all_faces.reserve(st.final_internal_faces.size() + st.final_boundary_faces.size());
  for (auto& f : st.final_internal_faces) st.all_faces.push_back(std::move(f));
  for (auto& f : st.final_boundary_faces) st.all_faces.push_back(std::move(f));

  st.total_faces = static_cast<int64_t>(st.all_faces.size());
  st.n_internal = static_cast<int64_t>(st.final_internal_faces.size());

  return 0;
}

}  // namespace cfd::hexcore
