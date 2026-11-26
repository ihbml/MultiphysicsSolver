#include "hexcore_state.hpp"

#include <vector>

#ifdef _OPENMP
#include <omp.h>
#endif

namespace cfd::hexcore {

int32_t passClassifyLeaves(
    HexcoreState& st,
    const double* surf_points,
    int64_t n_points,
    const int32_t* surf_faces,
    int64_t n_faces
) {
  (void)surf_points;
  st.all_leaves.clear();
  st.all_leaves.reserve(st.nodes.size());
  for (size_t i = 0; i < st.nodes.size(); ++i) {
    if (st.nodes[i].is_leaf) st.all_leaves.push_back(static_cast<int64_t>(i));
  }
  int64_t n_leaves = static_cast<int64_t>(st.all_leaves.size());
  if (n_leaves == 0) return -1;

  std::vector<double> leaf_centers(static_cast<size_t>(n_leaves) * 3);
#ifdef _OPENMP
#pragma omp parallel for schedule(static)
#endif
  for (int64_t k = 0; k < n_leaves; ++k) {
    const Vec3 c = st.nodes[static_cast<size_t>(st.all_leaves[static_cast<size_t>(k)])].center();
    leaf_centers[static_cast<size_t>(3 * k + 0)] = c.x;
    leaf_centers[static_cast<size_t>(3 * k + 1)] = c.y;
    leaf_centers[static_cast<size_t>(3 * k + 2)] = c.z;
  }

  std::vector<double> leaf_phis(static_cast<size_t>(n_leaves), 0.0);
  std::vector<double> leaf_grads(static_cast<size_t>(n_leaves) * 3, 0.0);
  st.sdf->evaluate(leaf_centers.data(), n_leaves, leaf_phis.data(), leaf_grads.data());

  st.is_multi_shell_closed = false;
  if (st.is_closed_surface && st.surf_signed_vol > 0.0 && n_points > 4) {
    std::vector<int32_t> parent(static_cast<size_t>(n_points));
    for (int64_t i = 0; i < n_points; ++i) parent[static_cast<size_t>(i)] = static_cast<int32_t>(i);
    auto find_root = [&](int32_t u) -> int32_t {
      while (parent[static_cast<size_t>(u)] != u) {
        parent[static_cast<size_t>(u)] = parent[static_cast<size_t>(parent[static_cast<size_t>(u)])];
        u = parent[static_cast<size_t>(u)];
      }
      return u;
    };
    for (int64_t f = 0; f < n_faces; ++f) {
      int32_t i0 = surf_faces[3 * f + 0];
      int32_t i1 = surf_faces[3 * f + 1];
      int32_t i2 = surf_faces[3 * f + 2];
      if (i0 < 0 || i1 < 0 || i2 < 0 || i0 >= n_points || i1 >= n_points || i2 >= n_points) continue;
      int32_t r0 = find_root(i0);
      int32_t r1 = find_root(i1);
      int32_t r2 = find_root(i2);
      if (r0 != r1) parent[static_cast<size_t>(r1)] = r0;
      if (r0 != r2) parent[static_cast<size_t>(r2)] = r0;
    }
    int32_t first_root = -1;
    for (int64_t f = 0; f < n_faces; ++f) {
      int32_t i0 = surf_faces[3 * f + 0];
      if (i0 < 0 || i0 >= n_points) continue;
      int32_t r = find_root(i0);
      if (first_root < 0) {
        first_root = r;
      } else if (r != first_root) {
        st.is_multi_shell_closed = true;
        break;
      }
    }
  }

  if (st.is_multi_shell_closed) {
    std::vector<int64_t> wn_leaf_pos;
    std::vector<double> wn_pts;
    wn_leaf_pos.reserve(static_cast<size_t>(n_leaves));
    wn_pts.reserve(static_cast<size_t>(n_leaves) * 3);
    for (int64_t k = 0; k < n_leaves; ++k) {
      const OctreeNode& ln = st.nodes[static_cast<size_t>(st.all_leaves[static_cast<size_t>(k)])];
      const Vec3 ln_size = ln.size();
      const Vec3 ln_center = ln.center();
      double pad = ln_size.norm();
      if (ln_center.x >= st.s_min.x - pad && ln_center.x <= st.s_max.x + pad &&
          ln_center.y >= st.s_min.y - pad && ln_center.y <= st.s_max.y + pad &&
          ln_center.z >= st.s_min.z - pad && ln_center.z <= st.s_max.z + pad) {
        wn_leaf_pos.push_back(k);
        wn_pts.push_back(ln_center.x);
        wn_pts.push_back(ln_center.y);
        wn_pts.push_back(ln_center.z);
      }
    }
    if (!wn_leaf_pos.empty()) {
      std::vector<double> wns(wn_leaf_pos.size(), 0.0);
      st.sdf->winding_number(wn_pts.data(), static_cast<int64_t>(wn_leaf_pos.size()), wns.data());
#ifdef _OPENMP
#pragma omp parallel for schedule(static)
#endif
      for (int64_t j = 0; j < static_cast<int64_t>(wn_leaf_pos.size()); ++j) {
        int64_t k = wn_leaf_pos[static_cast<size_t>(j)];
        bool inside = (wns[static_cast<size_t>(j)] > 0.5);
        double phi = leaf_phis[static_cast<size_t>(k)];
        if (inside && phi > 0.0) {
          leaf_phis[static_cast<size_t>(k)] = -phi;
          leaf_grads[static_cast<size_t>(3 * k + 0)] = -leaf_grads[static_cast<size_t>(3 * k + 0)];
          leaf_grads[static_cast<size_t>(3 * k + 1)] = -leaf_grads[static_cast<size_t>(3 * k + 1)];
          leaf_grads[static_cast<size_t>(3 * k + 2)] = -leaf_grads[static_cast<size_t>(3 * k + 2)];
        } else if (!inside && phi < 0.0) {
          leaf_phis[static_cast<size_t>(k)] = -phi;
          leaf_grads[static_cast<size_t>(3 * k + 0)] = -leaf_grads[static_cast<size_t>(3 * k + 0)];
          leaf_grads[static_cast<size_t>(3 * k + 1)] = -leaf_grads[static_cast<size_t>(3 * k + 1)];
          leaf_grads[static_cast<size_t>(3 * k + 2)] = -leaf_grads[static_cast<size_t>(3 * k + 2)];
        }
      }
    }
  }

  auto classify_all = [&](bool use_thin_fallback) {
#ifdef _OPENMP
#pragma omp parallel for schedule(static)
#endif
    for (int64_t k = 0; k < n_leaves; ++k) {
      OctreeNode& ln = st.nodes[static_cast<size_t>(st.all_leaves[static_cast<size_t>(k)])];
      double phi = leaf_phis[static_cast<size_t>(k)];
      double band = use_thin_fallback ? (0.5 * ln.size().norm()) : 0.0;
      bool active = false;
      int16_t reg = 0;

      if (st.options.domain_mode == 0) {
        if (phi >= -band) {
          active = true;
          reg = 0;
        }
      } else if (st.options.domain_mode == 2) {
        if (phi <= band) {
          active = true;
          reg = 1;
        }
      } else if (st.options.domain_mode == 3) {
        active = true;
        reg = (phi >= 0.0) ? 0 : 1;
      } else {
        if (phi <= band) {
          active = true;
          reg = 0;
        }
      }
      ln.is_active = active ? 1 : 0;
      ln.region = reg;
    }

    st.active_leaves.clear();
    st.active_grad_c.clear();
    st.active_leaves.reserve(static_cast<size_t>(n_leaves));
    st.active_grad_c.reserve(static_cast<size_t>(n_leaves));
    for (int64_t k = 0; k < n_leaves; ++k) {
      int64_t node_i = st.all_leaves[static_cast<size_t>(k)];
      if (st.nodes[static_cast<size_t>(node_i)].is_active) {
        st.nodes[static_cast<size_t>(node_i)].cell_index = static_cast<int64_t>(st.active_leaves.size());
        st.active_leaves.push_back(node_i);
        st.active_grad_c.emplace_back(
            leaf_grads[static_cast<size_t>(3 * k + 0)],
            leaf_grads[static_cast<size_t>(3 * k + 1)],
            leaf_grads[static_cast<size_t>(3 * k + 2)]
        );
      }
    }
  };

  // Strict sign activation eliminates over-activation into solid interior (AUD-002);
  // fall back to half-diag band only if strict sign leaves zero active cells on thin sheets.
  classify_all(false);
  if (st.active_leaves.empty()) {
    classify_all(true);
  }

  st.n_active_cells = static_cast<int64_t>(st.active_leaves.size());
  if (st.n_active_cells == 0) {
    return -1;
  }
  return 0;
}

}  // namespace cfd::hexcore
