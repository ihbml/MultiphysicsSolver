#include "hexcore_state.hpp"

#include <algorithm>
#include <atomic>
#include <vector>

#ifdef _OPENMP
#include <omp.h>
#endif

namespace cfd::hexcore {

int32_t passMergeSmallCells(HexcoreState& st) {
  int64_t n_active_cells = st.n_active_cells;

  st.cell_verts.assign(static_cast<size_t>(n_active_cells), {});
  std::atomic<bool> omp_failed{false};
#ifdef _OPENMP
#pragma omp parallel for schedule(static)
#endif
  for (int64_t c = 0; c < n_active_cells; ++c) {
    if (omp_failed.load(std::memory_order_relaxed)) continue;
    try {
      for (int64_t f_idx : st.cell_to_faces[static_cast<size_t>(c)]) {
        for (int64_t v : st.mosaic_faces[static_cast<size_t>(f_idx)].nodes) {
          st.cell_verts[static_cast<size_t>(c)].push_back(v);
        }
      }
      std::sort(st.cell_verts[static_cast<size_t>(c)].begin(), st.cell_verts[static_cast<size_t>(c)].end());
      st.cell_verts[static_cast<size_t>(c)].erase(
          std::unique(st.cell_verts[static_cast<size_t>(c)].begin(), st.cell_verts[static_cast<size_t>(c)].end()),
          st.cell_verts[static_cast<size_t>(c)].end());
    } catch (...) {
      omp_failed.store(true, std::memory_order_relaxed);
    }
  }
  if (omp_failed.load(std::memory_order_relaxed)) {
    return -12;
  }

  if (st.options.merge_small_cells) {
    for (int64_t c = 0; c < n_active_cells; ++c) {
      double v_bg = st.nodes[static_cast<size_t>(st.active_leaves[static_cast<size_t>(c)])].v_bg();
      double alpha = st.cell_vols[static_cast<size_t>(c)] / (v_bg + 1e-30);

      if (alpha < st.options.small_cell_ratio && alpha > 0.0) {
        Vec3 grad = st.active_grad_c[static_cast<size_t>(c)];
        int64_t best_nb = -1;
        double max_align = -1e30;

        for (int64_t f_idx : st.cell_to_faces[static_cast<size_t>(c)]) {
          const auto& mf = st.mosaic_faces[static_cast<size_t>(f_idx)];
          int64_t other = (mf.owner == c) ? mf.neighbour : mf.owner;
          if (other >= 0 && other < n_active_cells &&
              st.cell_vols[static_cast<size_t>(other)] > 1e-15) {
            Vec3 dir_nb = (st.cell_cents[static_cast<size_t>(other)] - st.cell_cents[static_cast<size_t>(c)]).normalized();
            double align = dot(dir_nb, grad);
            if (align > max_align) {
              max_align = align;
              best_nb = other;
            }
          }
        }

        if (best_nb >= 0) {
          double orig_nb_vol = st.cell_vols[static_cast<size_t>(best_nb)];
          st.cell_vols[static_cast<size_t>(best_nb)] += st.cell_vols[static_cast<size_t>(c)];
          st.cell_cents[static_cast<size_t>(best_nb)] =
              (st.cell_cents[static_cast<size_t>(best_nb)] * orig_nb_vol +
               st.cell_cents[static_cast<size_t>(c)] * st.cell_vols[static_cast<size_t>(c)]) /
              (st.cell_vols[static_cast<size_t>(best_nb)] + 1e-30);
          st.cell_types[static_cast<size_t>(best_nb)] = CellType::POLYHEDRON;

          st.cell_verts[static_cast<size_t>(best_nb)].insert(
              st.cell_verts[static_cast<size_t>(best_nb)].end(),
              st.cell_verts[static_cast<size_t>(c)].begin(),
              st.cell_verts[static_cast<size_t>(c)].end());
          std::sort(st.cell_verts[static_cast<size_t>(best_nb)].begin(),
                    st.cell_verts[static_cast<size_t>(best_nb)].end());
          st.cell_verts[static_cast<size_t>(best_nb)].erase(
              std::unique(st.cell_verts[static_cast<size_t>(best_nb)].begin(),
                          st.cell_verts[static_cast<size_t>(best_nb)].end()),
              st.cell_verts[static_cast<size_t>(best_nb)].end());

          for (int64_t f_idx : st.cell_to_faces[static_cast<size_t>(c)]) {
            auto& mf = st.mosaic_faces[static_cast<size_t>(f_idx)];
            if (mf.owner == c && mf.neighbour == best_nb) {
              mf.owner = -1;
              mf.neighbour = -1;
            } else if (mf.owner == best_nb && mf.neighbour == c) {
              mf.owner = -1;
              mf.neighbour = -1;
            } else if (mf.owner == c) {
              mf.owner = best_nb;
              st.cell_to_faces[static_cast<size_t>(best_nb)].push_back(f_idx);
            } else if (mf.neighbour == c) {
              mf.neighbour = best_nb;
              st.cell_to_faces[static_cast<size_t>(best_nb)].push_back(f_idx);
            }
          }

          st.cell_vols[static_cast<size_t>(c)] = 0.0;
        }
      }
    }
  }

  st.old_to_new_cell.assign(static_cast<size_t>(n_active_cells), -1);
  st.new_to_old_active.clear();
  st.new_to_old_active.reserve(static_cast<size_t>(n_active_cells));
  st.final_n_cells = 0;
  for (int64_t c = 0; c < n_active_cells; ++c) {
    if (st.cell_vols[static_cast<size_t>(c)] > 1e-15) {
      st.old_to_new_cell[static_cast<size_t>(c)] = st.final_n_cells++;
      st.new_to_old_active.push_back(c);
    }
  }
  if (st.final_n_cells == 0) {
    return -1;
  }

  return 0;
}

}  // namespace cfd::hexcore
