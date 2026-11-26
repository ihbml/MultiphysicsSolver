#include "hexcore_state.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <unordered_map>
#include <vector>

#ifdef _OPENMP
#include <omp.h>
#endif

namespace cfd::hexcore {

namespace {

// Check all 8 right-handed corner tetrahedra of a canonical VTK Hex8 (0..7).
bool hasPositiveHex8CornerTets(
    const std::array<int64_t, 8>& hc,
    const std::vector<Vec3>& node_coords,
    double v_bg
) {
  static constexpr int kCorners[8][4] = {
      {0, 1, 3, 4},
      {1, 2, 0, 5},
      {2, 3, 1, 6},
      {3, 0, 2, 7},
      {4, 7, 5, 0},
      {5, 4, 6, 1},
      {6, 5, 7, 2},
      {7, 6, 4, 3},
  };
  const double min_det = 1e-12 * v_bg;
  for (int k = 0; k < 8; ++k) {
    const Vec3& p0 = node_coords[static_cast<size_t>(hc[static_cast<size_t>(kCorners[k][0])])];
    const Vec3& p1 = node_coords[static_cast<size_t>(hc[static_cast<size_t>(kCorners[k][1])])];
    const Vec3& p2 = node_coords[static_cast<size_t>(hc[static_cast<size_t>(kCorners[k][2])])];
    const Vec3& p3 = node_coords[static_cast<size_t>(hc[static_cast<size_t>(kCorners[k][3])])];
    if (dot(cross(p1 - p0, p2 - p0), p3 - p0) <= min_det) {
      return false;
    }
  }
  return true;
}

bool isCellValidAtCurrentCoords(
    const HexcoreState& st,
    int64_t cell_idx,
    const std::vector<Vec3>& coords
) {
  const double v_bg = st.nodes[static_cast<size_t>(st.active_leaves[static_cast<size_t>(cell_idx)])].v_bg();
  const auto& face_list = st.cell_to_faces[static_cast<size_t>(cell_idx)];
  if (face_list.empty()) return false;

  const size_t nf = face_list.size();
  std::vector<Vec3> f_sf(nf);
  std::vector<Vec3> f_cf(nf);
  Vec3 c_approx(0, 0, 0);

  for (size_t k = 0; k < nf; ++k) {
    const auto& mf = st.mosaic_faces[static_cast<size_t>(face_list[k])];
    auto [sf_owner, cf] = computePolygonNormalAndCentroid(mf.nodes, coords);
    if (isFaceFolded(mf.nodes, coords, sf_owner)) {
      return false;
    }
    double sign = (mf.owner == cell_idx) ? 1.0 : -1.0;
    f_sf[k] = sf_owner * sign;
    f_cf[k] = cf;
    c_approx += cf;
  }
  c_approx /= static_cast<double>(nf);

  double vol_sum = 0.0;
  Vec3 cent_sum(0, 0, 0);
  for (size_t k = 0; k < nf; ++k) {
    double pyr_vol = dot(f_cf[k] - c_approx, f_sf[k]) / 3.0;
    vol_sum += pyr_vol;
    Vec3 pyr_c = f_cf[k] * 0.75 + c_approx * 0.25;
    cent_sum += pyr_c * pyr_vol;
  }
  if (vol_sum <= 0.02 * v_bg) {
    return false;
  }
  Vec3 c_cent = cent_sum / vol_sum;

  // Guarantees inverted_pyramids == 0 for both HEXAHEDRON and POLYHEDRON
  const double min_pyr = 1e-9 * v_bg;
  for (size_t k = 0; k < nf; ++k) {
    double pyr_vol = dot(f_cf[k] - c_cent, f_sf[k]) / 3.0;
    if (pyr_vol <= min_pyr) {
      return false;
    }
  }

  return true;
}

}  // namespace

int32_t passSnapWallVertices(HexcoreState& st) {
  if (!st.options.snap_cut_cells) {
    return 0;
  }

  std::vector<int64_t> wall_verts;
  std::vector<uint8_t> is_wall_vert(st.node_coords.size(), 0);
  std::vector<uint8_t> is_farfield_vert(st.node_coords.size(), 0);

  for (const auto& mf : st.mosaic_faces) {
    if (mf.neighbour == -1 && !mf.is_farfield) {
      for (int64_t v : mf.nodes) {
        if (!is_wall_vert[static_cast<size_t>(v)]) {
          is_wall_vert[static_cast<size_t>(v)] = 1;
          wall_verts.push_back(v);
        }
      }
    } else if (mf.is_farfield) {
      for (int64_t v : mf.nodes) {
        is_farfield_vert[static_cast<size_t>(v)] = 1;
      }
    }
  }

  if (wall_verts.empty()) {
    return 0;
  }
  std::sort(wall_verts.begin(), wall_verts.end());
  const size_t n_wall = wall_verts.size();

  std::vector<int32_t> vert_to_wall_idx(st.node_coords.size(), -1);
  for (size_t i = 0; i < n_wall; ++i) {
    vert_to_wall_idx[static_cast<size_t>(wall_verts[i])] = static_cast<int32_t>(i);
  }

  // Build O(n_wall) incidence lists restricted to cells touching >= 1 wall vertex
  std::vector<int64_t> wall_incident_cells;
  wall_incident_cells.reserve(n_wall * 4);
  for (const auto& mf : st.mosaic_faces) {
    bool touches_wall = false;
    for (int64_t v : mf.nodes) {
      if (is_wall_vert[static_cast<size_t>(v)]) {
        touches_wall = true;
        break;
      }
    }
    if (!touches_wall) continue;
    if (mf.owner >= 0) wall_incident_cells.push_back(mf.owner);
    if (mf.neighbour >= 0) wall_incident_cells.push_back(mf.neighbour);
  }
  std::sort(wall_incident_cells.begin(), wall_incident_cells.end());
  wall_incident_cells.erase(
      std::unique(wall_incident_cells.begin(), wall_incident_cells.end()),
      wall_incident_cells.end());

  const size_t n_wc = wall_incident_cells.size();
  std::vector<std::vector<int32_t>> wc_wall_indices(n_wc);
  std::vector<std::vector<int32_t>> wv_to_wc(n_wall);
  std::vector<double> h_local(n_wall, 1e30);

  for (size_t slot = 0; slot < n_wc; ++slot) {
    int64_t cell_idx = wall_incident_cells[slot];
    const Vec3 onode_sz = st.nodes[static_cast<size_t>(st.active_leaves[static_cast<size_t>(cell_idx)])].size();
    double h_cell = std::min({onode_sz.x, onode_sz.y, onode_sz.z});

    auto& wl = wc_wall_indices[slot];
    for (int64_t f_idx : st.cell_to_faces[static_cast<size_t>(cell_idx)]) {
      for (int64_t v : st.mosaic_faces[static_cast<size_t>(f_idx)].nodes) {
        int32_t wi = vert_to_wall_idx[static_cast<size_t>(v)];
        if (wi >= 0) {
          wl.push_back(wi);
        }
      }
    }
    std::sort(wl.begin(), wl.end());
    wl.erase(std::unique(wl.begin(), wl.end()), wl.end());
    for (int32_t wi : wl) {
      wv_to_wc[static_cast<size_t>(wi)].push_back(static_cast<int32_t>(slot));
      h_local[static_cast<size_t>(wi)] = std::min(h_local[static_cast<size_t>(wi)], h_cell);
    }
  }

  std::vector<Vec3> orig_wall_coords(n_wall);
  std::vector<double> wall_pts(n_wall * 3);
  for (size_t i = 0; i < n_wall; ++i) {
    const Vec3& p = st.node_coords[static_cast<size_t>(wall_verts[i])];
    orig_wall_coords[i] = p;
    wall_pts[3 * i + 0] = p.x;
    wall_pts[3 * i + 1] = p.y;
    wall_pts[3 * i + 2] = p.z;
  }

  std::vector<double> phis(n_wall, 0.0);
  std::vector<double> grads(n_wall * 3, 0.0);
  st.sdf->evaluate(wall_pts.data(), static_cast<int64_t>(n_wall), phis.data(), grads.data());
  if (st.is_multi_shell_closed) {
    std::vector<double> wall_wns(n_wall, 0.0);
    st.sdf->winding_number(wall_pts.data(), static_cast<int64_t>(n_wall), wall_wns.data());
#ifdef _OPENMP
#pragma omp parallel for schedule(static)
#endif
    for (int64_t i = 0; i < static_cast<int64_t>(n_wall); ++i) {
      bool inside = (wall_wns[static_cast<size_t>(i)] > 0.5);
      double phi = phis[static_cast<size_t>(i)];
      if ((inside && phi > 0.0) || (!inside && phi < 0.0)) {
        phis[static_cast<size_t>(i)] = -phi;
        grads[static_cast<size_t>(3 * i + 0)] = -grads[static_cast<size_t>(3 * i + 0)];
        grads[static_cast<size_t>(3 * i + 1)] = -grads[static_cast<size_t>(3 * i + 1)];
        grads[static_cast<size_t>(3 * i + 2)] = -grads[static_cast<size_t>(3 * i + 2)];
      }
    }
  }

  // Simultaneous target displacement clamped to 0.90 * h_local, pinning farfield corners
  std::vector<Vec3> disp(n_wall, Vec3(0, 0, 0));
  std::vector<double> alpha(n_wall, 0.0);
  for (size_t i = 0; i < n_wall; ++i) {
    if (is_farfield_vert[static_cast<size_t>(wall_verts[i])]) continue;
    Vec3 g(grads[3 * i + 0], grads[3 * i + 1], grads[3 * i + 2]);
    double g2 = g.norm2();
    if (g2 <= 1e-12) continue;
    Vec3 d = -(phis[i] / g2) * g;
    double d_len = d.norm();
    double max_step = 0.95 * h_local[i];
    if (d_len > max_step && d_len > 1e-15) {
      d *= (max_step / d_len);
    }
    if (d.norm2() > 0.0) {
      disp[i] = d;
      alpha[i] = 1.0;
      st.node_coords[static_cast<size_t>(wall_verts[i])] = orig_wall_coords[i] + d;
    }
  }

  // Synchronized damped relaxation over wall_incident_cells (eliminates 8-corner domino rollback, AUD-002)
  std::vector<int32_t> dirty_wc(n_wc);
  for (size_t slot = 0; slot < n_wc; ++slot) dirty_wc[slot] = static_cast<int32_t>(slot);

  std::vector<uint8_t> cell_failed(n_wc, 0);
  std::vector<uint8_t> vert_needs_damp(n_wall, 0);
  std::vector<uint8_t> wc_queued(n_wc, 0);

  constexpr int kMaxRelaxIters = 16;
  for (int iter = 0; iter < kMaxRelaxIters && !dirty_wc.empty(); ++iter) {
    const int64_t n_dirty = static_cast<int64_t>(dirty_wc.size());
    std::atomic<bool> omp_failed{false};

#ifdef _OPENMP
#pragma omp parallel for schedule(dynamic, 32)
#endif
    for (int64_t di = 0; di < n_dirty; ++di) {
      if (omp_failed.load(std::memory_order_relaxed)) continue;
      try {
        int32_t slot = dirty_wc[static_cast<size_t>(di)];
        int64_t cell_idx = wall_incident_cells[static_cast<size_t>(slot)];
        bool ok = isCellValidAtCurrentCoords(st, cell_idx, st.node_coords);
        cell_failed[static_cast<size_t>(slot)] = ok ? 0 : 1;
      } catch (...) {
        omp_failed.store(true, std::memory_order_relaxed);
      }
    }
    if (omp_failed.load(std::memory_order_relaxed)) {
      return -12;
    }

    bool any_damped = false;
    std::vector<int32_t> damped_verts;
    for (int32_t slot : dirty_wc) {
      if (!cell_failed[static_cast<size_t>(slot)]) continue;
      cell_failed[static_cast<size_t>(slot)] = 0;
      for (int32_t wi : wc_wall_indices[static_cast<size_t>(slot)]) {
        if (alpha[static_cast<size_t>(wi)] > 0.0 && !vert_needs_damp[static_cast<size_t>(wi)]) {
          vert_needs_damp[static_cast<size_t>(wi)] = 1;
          damped_verts.push_back(wi);
        }
      }
    }

    if (damped_verts.empty()) {
      break;
    }

    std::vector<int32_t> next_dirty;
    for (int32_t wi : damped_verts) {
      vert_needs_damp[static_cast<size_t>(wi)] = 0;
      double next_a = alpha[static_cast<size_t>(wi)] * 0.6;
      if (iter + 1 == kMaxRelaxIters || next_a < 0.05) {
        next_a = 0.0;
      }
      if (next_a != alpha[static_cast<size_t>(wi)]) {
        alpha[static_cast<size_t>(wi)] = next_a;
        st.node_coords[static_cast<size_t>(wall_verts[static_cast<size_t>(wi)])] =
            orig_wall_coords[static_cast<size_t>(wi)] + next_a * disp[static_cast<size_t>(wi)];
        any_damped = true;
        for (int32_t nb_slot : wv_to_wc[static_cast<size_t>(wi)]) {
          if (!wc_queued[static_cast<size_t>(nb_slot)]) {
            wc_queued[static_cast<size_t>(nb_slot)] = 1;
            next_dirty.push_back(nb_slot);
          }
        }
      }
    }

    for (int32_t slot : next_dirty) {
      wc_queued[static_cast<size_t>(slot)] = 0;
    }
    if (!any_damped) {
      break;
    }
    dirty_wc.swap(next_dirty);
  }

  // Promote snapped corner-hexes whose Hex8 corner Jacobian flattened (while remaining valid polyhedra)
  for (int64_t cell_idx : wall_incident_cells) {
    if (st.cell_types[static_cast<size_t>(cell_idx)] != CellType::HEXAHEDRON) continue;
    const double v_bg = st.nodes[static_cast<size_t>(st.active_leaves[static_cast<size_t>(cell_idx)])].v_bg();
    const auto& hc = st.hex_corners[static_cast<size_t>(cell_idx)];
    if (!hasPositiveHex8CornerTets(hc, st.node_coords, v_bg)) {
      st.cell_types[static_cast<size_t>(cell_idx)] = CellType::POLYHEDRON;
      continue;
    }
    std::vector<int64_t> hnodes(hc.begin(), hc.end());
    if (computeCellJENS(st.node_coords, hnodes, CellType::HEXAHEDRON) <= 1e-6) {
      st.cell_types[static_cast<size_t>(cell_idx)] = CellType::POLYHEDRON;
    }
  }

  return 0;
}

}  // namespace cfd::hexcore
