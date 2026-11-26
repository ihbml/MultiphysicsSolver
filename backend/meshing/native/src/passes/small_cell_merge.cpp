#include "hexcore_state.hpp"

#include <vector>

namespace cfd::hexcore {

namespace {

struct ExtraFaceNode {
  int64_t face_id;
  int64_t next;
};

}  // namespace

int32_t passMergeSmallCells(HexcoreState& st) {
  const int64_t n_active_cells = st.n_active_cells;

  if (st.options.merge_small_cells) {
    // Intrusive per-cell linked list for faces reassigned to neighbor cells during merges
    std::vector<int64_t> extra_head(static_cast<size_t>(n_active_cells), -1);
    std::vector<ExtraFaceNode> extra_pool;

    auto for_each_cell_face = [&](int64_t cell, auto&& fn) {
      const int64_t f_beg = st.cell_face_offsets[static_cast<size_t>(cell)];
      const int64_t f_end = st.cell_face_offsets[static_cast<size_t>(cell) + 1];
      for (int64_t ci = f_beg; ci < f_end; ++ci) {
        fn(st.cell_face_ids[static_cast<size_t>(ci)]);
      }
      int64_t link = extra_head[static_cast<size_t>(cell)];
      while (link >= 0) {
        const ExtraFaceNode& node = extra_pool[static_cast<size_t>(link)];
        fn(node.face_id);
        link = node.next;
      }
    };

    auto add_extra_face = [&](int64_t cell, int64_t f_idx) {
      const int64_t link_idx = static_cast<int64_t>(extra_pool.size());
      extra_pool.push_back(ExtraFaceNode{f_idx, extra_head[static_cast<size_t>(cell)]});
      extra_head[static_cast<size_t>(cell)] = link_idx;
    };

    for (int64_t c = 0; c < n_active_cells; ++c) {
      const double v_bg = st.cell_v_bg[static_cast<size_t>(c)];
      const double alpha = st.cell_vols[static_cast<size_t>(c)] / (v_bg + 1e-30);

      if (alpha < st.options.small_cell_ratio && alpha > 0.0) {
        const Vec3 grad = st.active_grad_c[static_cast<size_t>(c)];
        int64_t best_nb = -1;
        double max_align = -1e30;

        for_each_cell_face(c, [&](int64_t f_idx) {
          const size_t fs = static_cast<size_t>(f_idx);
          const int64_t o = st.faces.owner[fs];
          const int64_t nb = st.faces.neighbour[fs];
          if (o < 0 && nb < 0) return;
          if (o != c && nb != c) return;
          const int64_t other = (o == c) ? nb : o;
          if (other >= 0 && other != c && other < n_active_cells &&
              st.cell_vols[static_cast<size_t>(other)] > 1e-15) {
            const Vec3 dir_nb = (st.cell_cents[static_cast<size_t>(other)] - st.cell_cents[static_cast<size_t>(c)]).normalized();
            const double align = dot(dir_nb, grad);
            if (align > max_align) {
              max_align = align;
              best_nb = other;
            }
          }
        });

        if (best_nb >= 0) {
          const double orig_nb_vol = st.cell_vols[static_cast<size_t>(best_nb)];
          st.cell_vols[static_cast<size_t>(best_nb)] += st.cell_vols[static_cast<size_t>(c)];
          st.cell_cents[static_cast<size_t>(best_nb)] =
              (st.cell_cents[static_cast<size_t>(best_nb)] * orig_nb_vol +
               st.cell_cents[static_cast<size_t>(c)] * st.cell_vols[static_cast<size_t>(c)]) /
              (st.cell_vols[static_cast<size_t>(best_nb)] + 1e-30);
          st.cell_types[static_cast<size_t>(best_nb)] = CellType::POLYHEDRON;

          for_each_cell_face(c, [&](int64_t f_idx) {
            const size_t fs = static_cast<size_t>(f_idx);
            const int64_t o = st.faces.owner[fs];
            const int64_t nb = st.faces.neighbour[fs];
            if ((o == c && nb == best_nb) || (o == best_nb && nb == c)) {
              st.faces.owner[fs] = -1;
              st.faces.neighbour[fs] = -1;
            } else if (o == c) {
              st.faces.owner[fs] = best_nb;
              add_extra_face(best_nb, f_idx);
            } else if (nb == c) {
              st.faces.neighbour[fs] = best_nb;
              add_extra_face(best_nb, f_idx);
            }
          });

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

  // Free transient per-cell CSR & approximation arrays no longer needed downstream
  std::vector<int64_t>().swap(st.cell_face_offsets);
  std::vector<int64_t>().swap(st.cell_face_ids);
  std::vector<Vec3>().swap(st.cell_approx_center);
  std::vector<Vec3>().swap(st.active_grad_c);

  if (st.final_n_cells == 0) {
    return -1;
  }

  return 0;
}

}  // namespace cfd::hexcore
