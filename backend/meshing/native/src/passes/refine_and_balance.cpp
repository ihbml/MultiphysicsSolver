#include "hexcore_state.hpp"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <vector>

#ifdef _OPENMP
#include <omp.h>
#endif

namespace cfd::hexcore {

int32_t passRefineAndBalance(HexcoreState& st) {
  std::vector<int64_t> frontier(st.n_root);
  for (size_t i = 0; i < st.n_root; ++i) frontier[i] = static_cast<int64_t>(i);

  for (int32_t pass = 0; pass < st.effective_max_depth; ++pass) {
    size_t n_eval = frontier.size();
    if (n_eval == 0) break;

    std::vector<double> leaf_centers(n_eval * 3);
#ifdef _OPENMP
#pragma omp parallel for schedule(static)
#endif
    for (int64_t k = 0; k < static_cast<int64_t>(n_eval); ++k) {
      const Vec3 c = st.nodes[static_cast<size_t>(frontier[static_cast<size_t>(k)])].center();
      leaf_centers[static_cast<size_t>(3 * k + 0)] = c.x;
      leaf_centers[static_cast<size_t>(3 * k + 1)] = c.y;
      leaf_centers[static_cast<size_t>(3 * k + 2)] = c.z;
    }

    std::vector<double> phis(n_eval);
    st.sdf->evaluate(leaf_centers.data(), static_cast<int64_t>(n_eval), phis.data(), nullptr);

    std::vector<uint8_t> should_split(n_eval, 0);
#ifdef _OPENMP
#pragma omp parallel for schedule(static)
#endif
    for (int64_t k = 0; k < static_cast<int64_t>(n_eval); ++k) {
      int64_t idx = frontier[static_cast<size_t>(k)];
      const Vec3 nsz = st.nodes[static_cast<size_t>(idx)].size();
      double max_edge = std::max({nsz.x, nsz.y, nsz.z});
      if (st.options.h_min > 0.0 && max_edge * 0.5 < 0.48 * st.options.h_min) {
        continue;
      }
      double diag = nsz.norm();
      double band_factor =
          0.75 + 0.15 * std::clamp(0.25 - st.options.alpha_curv, 0.0, 0.25)
               + 0.15 * std::clamp(0.25 - st.options.beta_prox, 0.0, 0.25);
      if (std::abs(phis[static_cast<size_t>(k)]) <= band_factor * diag) {
        should_split[static_cast<size_t>(k)] = 1;
      }
    }

    std::vector<int64_t> split_ids;
    split_ids.reserve(n_eval);
    for (size_t k = 0; k < n_eval; ++k) {
      if (should_split[k]) {
        split_ids.push_back(frontier[k]);
      }
    }
    std::vector<int64_t> next_frontier;
    if (!st.subdivideBatch(split_ids, next_frontier)) {
      return -12;
    }
    frontier.swap(next_frontier);
  }

  std::vector<int64_t> leaves;
  leaves.reserve(st.nodes.size());
  for (size_t i = 0; i < st.nodes.size(); ++i) {
    if (st.nodes[i].is_leaf) leaves.push_back(static_cast<int64_t>(i));
  }

  int max_threads = 1;
#ifdef _OPENMP
  max_threads = std::max(1, omp_get_max_threads());
#endif
  st.max_threads = max_threads;

  int balance_iterations = 0;
  bool converged = false;
  while (balance_iterations < 32) {
    ++balance_iterations;
    std::vector<std::vector<int64_t>> thread_splits(static_cast<size_t>(max_threads));
    std::atomic<bool> omp_failed{false};

#ifdef _OPENMP
#pragma omp parallel
    {
      try {
        int tid = omp_get_thread_num();
        auto& local_splits = thread_splits[static_cast<size_t>(tid)];
#pragma omp for schedule(dynamic, 64)
        for (int64_t li = 0; li < static_cast<int64_t>(leaves.size()); ++li) {
          if (omp_failed.load(std::memory_order_relaxed)) continue;
          int64_t node_i = leaves[static_cast<size_t>(li)];
          if (!st.nodes[static_cast<size_t>(node_i)].is_leaf) continue;
          int32_t lev_a = st.nodes[static_cast<size_t>(node_i)].level;
          Vec3 c = st.nodes[static_cast<size_t>(node_i)].center();
          Vec3 h = st.nodes[static_cast<size_t>(node_i)].size() * 0.5;

          for (int di = -1; di <= 1; ++di) {
            for (int dj = -1; dj <= 1; ++dj) {
              for (int dk = -1; dk <= 1; ++dk) {
                if (di == 0 && dj == 0 && dk == 0) continue;
                Vec3 p_probe(
                    c.x + di * (h.x + st.eps_probe),
                    c.y + dj * (h.y + st.eps_probe),
                    c.z + dk * (h.z + st.eps_probe)
                );
                int64_t nb = st.findLeafAtPoint(p_probe);
                if (nb >= 0 && st.nodes[static_cast<size_t>(nb)].is_leaf) {
                  if (lev_a - st.nodes[static_cast<size_t>(nb)].level > 1) {
                    local_splits.push_back(nb);
                  }
                }
              }
            }
          }
        }
      } catch (...) {
        omp_failed.store(true, std::memory_order_relaxed);
      }
    }
#else
    {
      auto& local_splits = thread_splits[0];
      for (size_t li = 0; li < leaves.size(); ++li) {
        int64_t node_i = leaves[li];
        if (!st.nodes[static_cast<size_t>(node_i)].is_leaf) continue;
        int32_t lev_a = st.nodes[static_cast<size_t>(node_i)].level;
        Vec3 c = st.nodes[static_cast<size_t>(node_i)].center();
        Vec3 h = st.nodes[static_cast<size_t>(node_i)].size() * 0.5;
        for (int di = -1; di <= 1; ++di) {
          for (int dj = -1; dj <= 1; ++dj) {
            for (int dk = -1; dk <= 1; ++dk) {
              if (di == 0 && dj == 0 && dk == 0) continue;
              Vec3 p_probe(
                  c.x + di * (h.x + st.eps_probe),
                  c.y + dj * (h.y + st.eps_probe),
                  c.z + dk * (h.z + st.eps_probe)
              );
              int64_t nb = st.findLeafAtPoint(p_probe);
              if (nb >= 0 && st.nodes[static_cast<size_t>(nb)].is_leaf) {
                if (lev_a - st.nodes[static_cast<size_t>(nb)].level > 1) {
                  local_splits.push_back(nb);
                }
              }
            }
          }
        }
      }
    }
#endif
    if (omp_failed.load(std::memory_order_relaxed)) {
      return -12;
    }

    std::vector<int64_t> to_split;
    for (const auto& ts : thread_splits) {
      to_split.insert(to_split.end(), ts.begin(), ts.end());
    }
    if (to_split.empty()) {
      converged = true;
      break;
    }

    std::sort(to_split.begin(), to_split.end());
    to_split.erase(std::unique(to_split.begin(), to_split.end()), to_split.end());

    std::vector<int64_t> split_ids;
    split_ids.reserve(to_split.size());
    for (int64_t nb : to_split) {
      if (st.nodes[static_cast<size_t>(nb)].is_leaf) {
        split_ids.push_back(nb);
      }
    }
    std::vector<int64_t> new_children;
    if (!st.subdivideBatch(split_ids, new_children)) {
      return -12;
    }

    std::vector<int64_t> next_leaves;
    next_leaves.reserve(leaves.size() + new_children.size());
    for (int64_t l : leaves) {
      if (st.nodes[static_cast<size_t>(l)].is_leaf) next_leaves.push_back(l);
    }
    next_leaves.insert(next_leaves.end(), new_children.begin(), new_children.end());
    leaves.swap(next_leaves);
  }
  if (!converged) {
    return -13;
  }

  return 0;
}

}  // namespace cfd::hexcore
