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
  const double v_bg = st.cell_v_bg[static_cast<size_t>(cell_idx)];
  const int64_t f_beg = st.cell_face_offsets[static_cast<size_t>(cell_idx)];
  const int64_t f_end = st.cell_face_offsets[static_cast<size_t>(cell_idx) + 1];
  if (f_end <= f_beg) return false;

  const size_t nf = static_cast<size_t>(f_end - f_beg);
  std::array<Vec3, 24> stack_sf;
  std::array<Vec3, 24> stack_cf;
  std::vector<Vec3> heap_sf;
  std::vector<Vec3> heap_cf;
  Vec3* f_sf = stack_sf.data();
  Vec3* f_cf = stack_cf.data();
  if (nf > 24) {
    heap_sf.resize(nf);
    heap_cf.resize(nf);
    f_sf = heap_sf.data();
    f_cf = heap_cf.data();
  }

  Vec3 c_approx(0, 0, 0);
  for (size_t k = 0; k < nf; ++k) {
    const size_t f_idx = static_cast<size_t>(st.cell_face_ids[static_cast<size_t>(f_beg) + k]);
    const int64_t* fnodes = st.faces.face_nodes(f_idx);
    const size_t nv = st.faces.face_nv(f_idx);
    auto [sf_owner, cf] = computePolygonNormalAndCentroid(fnodes, nv, coords);
    if (isFaceFolded(fnodes, nv, coords, sf_owner)) {
      return false;
    }
    double sign = (st.faces.owner[f_idx] == cell_idx) ? 1.0 : -1.0;
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

void flipWindingInconsistentSDF(
    const MeshSDF& sdf,
    const std::vector<double>& pts,
    std::vector<double>& phis,
    std::vector<double>& grads
) {
  const int64_t n = static_cast<int64_t>(phis.size());
  if (n <= 0) return;
  std::vector<double> wns(static_cast<size_t>(n), 0.0);
  sdf.winding_number(pts.data(), n, wns.data());
#ifdef _OPENMP
#pragma omp parallel for schedule(static)
#endif
  for (int64_t i = 0; i < n; ++i) {
    bool inside = (wns[static_cast<size_t>(i)] > 0.5);
    double phi = phis[static_cast<size_t>(i)];
    if ((inside && phi > 0.0) || (!inside && phi < 0.0)) {
      phis[static_cast<size_t>(i)] = -phi;
      grads[static_cast<size_t>(3 * i + 0)] = -grads[static_cast<size_t>(3 * i + 0)];
      grads[static_cast<size_t>(3 * i + 1)] = -grads[static_cast<size_t>(3 * i + 1)];
      grads[static_cast<size_t>(3 * i + 2)] = -grads[static_cast<size_t>(3 * i + 2)];
    }
  }
}

Vec3 solveQefJacobi3x3(const double A_in[3][3], const Vec3& b) noexcept {
  double D[3][3] = {
      {A_in[0][0], A_in[0][1], A_in[0][2]},
      {A_in[1][0], A_in[1][1], A_in[1][2]},
      {A_in[2][0], A_in[2][1], A_in[2][2]},
  };
  double V[3][3] = {
      {1.0, 0.0, 0.0},
      {0.0, 1.0, 0.0},
      {0.0, 0.0, 1.0},
  };

  for (int sweep = 0; sweep < 15; ++sweep) {
    int p = 0;
    int q = 1;
    double max_off = std::abs(D[0][1]);
    if (std::abs(D[0][2]) > max_off) {
      max_off = std::abs(D[0][2]);
      p = 0;
      q = 2;
    }
    if (std::abs(D[1][2]) > max_off) {
      max_off = std::abs(D[1][2]);
      p = 1;
      q = 2;
    }
    if (max_off <= 1e-14) break;

    const double apq = D[p][q];
    const double tau = (D[q][q] - D[p][p]) / (2.0 * apq);
    const double t = (tau >= 0.0)
                         ? (1.0 / (tau + std::sqrt(1.0 + tau * tau)))
                         : (-1.0 / (-tau + std::sqrt(1.0 + tau * tau)));
    const double c = 1.0 / std::sqrt(1.0 + t * t);
    const double s = t * c;

    const double app = D[p][p];
    const double aqq = D[q][q];
    D[p][p] = app - t * apq;
    D[q][q] = aqq + t * apq;
    D[p][q] = 0.0;
    D[q][p] = 0.0;

    const int r = 3 - p - q;
    const double arp = D[r][p];
    const double arq = D[r][q];
    D[r][p] = D[p][r] = c * arp - s * arq;
    D[r][q] = D[q][r] = s * arp + c * arq;

    for (int row = 0; row < 3; ++row) {
      const double vp = V[row][p];
      const double vq = V[row][q];
      V[row][p] = c * vp - s * vq;
      V[row][q] = s * vp + c * vq;
    }
  }

  const double lambda_max = std::max({D[0][0], D[1][1], D[2][2]});
  if (lambda_max <= 1e-12) return Vec3(0, 0, 0);

  const double thresh = 0.05 * lambda_max;
  Vec3 delta_q(0, 0, 0);
  for (int m = 0; m < 3; ++m) {
    if (D[m][m] > thresh) {
      Vec3 v_m(V[0][m], V[1][m], V[2][m]);
      delta_q += (dot(v_m, b) / D[m][m]) * v_m;
    }
  }
  return delta_q;
}

}  // namespace

int32_t passSnapWallVertices(HexcoreState& st) {
  auto release_sdf_memory = [&]() {
    st.sdf.reset();
    std::vector<double>().swap(st.shifted_surf_points);
  };

  if (!st.options.snap_cut_cells) {
    release_sdf_memory();
    return 0;
  }

  const int64_t n_faces = st.faces.size();
  std::vector<int64_t> wall_verts;
  std::vector<uint8_t> is_wall_vert(st.node_coords.size(), 0);
  std::vector<uint8_t> is_farfield_vert(st.node_coords.size(), 0);

  for (int64_t f = 0; f < n_faces; ++f) {
    const size_t fs = static_cast<size_t>(f);
    const bool is_ff = (st.faces.flags[fs] & kFlagFarfield) != 0;
    const int64_t* fnodes = st.faces.face_nodes(fs);
    const size_t nv = st.faces.face_nv(fs);
    if (st.faces.neighbour[fs] == -1 && !is_ff) {
      for (size_t k = 0; k < nv; ++k) {
        const int64_t v = fnodes[k];
        if (!is_wall_vert[static_cast<size_t>(v)]) {
          is_wall_vert[static_cast<size_t>(v)] = 1;
          wall_verts.push_back(v);
        }
      }
    } else if (is_ff) {
      for (size_t k = 0; k < nv; ++k) {
        is_farfield_vert[static_cast<size_t>(fnodes[k])] = 1;
      }
    }
  }

  if (wall_verts.empty()) {
    release_sdf_memory();
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
  for (int64_t f = 0; f < n_faces; ++f) {
    const size_t fs = static_cast<size_t>(f);
    const int64_t* fnodes = st.faces.face_nodes(fs);
    const size_t nv = st.faces.face_nv(fs);
    bool touches_wall = false;
    for (size_t k = 0; k < nv; ++k) {
      if (is_wall_vert[static_cast<size_t>(fnodes[k])]) {
        touches_wall = true;
        break;
      }
    }
    if (!touches_wall) continue;
    if (st.faces.owner[fs] >= 0) wall_incident_cells.push_back(st.faces.owner[fs]);
    if (st.faces.neighbour[fs] >= 0) wall_incident_cells.push_back(st.faces.neighbour[fs]);
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
    double h_cell = st.cell_h_min[static_cast<size_t>(cell_idx)];

    auto& wl = wc_wall_indices[slot];
    const int64_t f_beg = st.cell_face_offsets[static_cast<size_t>(cell_idx)];
    const int64_t f_end = st.cell_face_offsets[static_cast<size_t>(cell_idx) + 1];
    for (int64_t ci = f_beg; ci < f_end; ++ci) {
      const size_t f_idx = static_cast<size_t>(st.cell_face_ids[static_cast<size_t>(ci)]);
      const int64_t* fnodes = st.faces.face_nodes(f_idx);
      const size_t nv = st.faces.face_nv(f_idx);
      for (size_t k = 0; k < nv; ++k) {
        int32_t wi = vert_to_wall_idx[static_cast<size_t>(fnodes[k])];
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
    flipWindingInconsistentSDF(*st.sdf, wall_pts, phis, grads);
  }

  // Initial SDF projection clamped to 0.95 * h_local, pinning farfield corners
  std::vector<Vec3> disp(n_wall, Vec3(0, 0, 0));
  std::vector<double> alpha(n_wall, 0.0);
  std::vector<uint8_t> eligible_qef(n_wall, 0);
  for (size_t i = 0; i < n_wall; ++i) {
    if (is_farfield_vert[static_cast<size_t>(wall_verts[i])]) continue;
    Vec3 g(grads[3 * i + 0], grads[3 * i + 1], grads[3 * i + 2]);
    double g2 = g.norm2();
    if (g2 <= 1e-12) continue;
    eligible_qef[i] = 1;
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

  // QEF Dual-Contouring Sharp-Feature Snap (Blueprint §1.2.3)
  {
    const size_t n_probes = n_wall * 6;
    std::vector<double> probe_pts(n_probes * 3, 0.0);
#ifdef _OPENMP
#pragma omp parallel for schedule(static)
#endif
    for (int64_t ii = 0; ii < static_cast<int64_t>(n_wall); ++ii) {
      const size_t i = static_cast<size_t>(ii);
      const Vec3 q_i = orig_wall_coords[i] + disp[i];
      const double delta = 0.25 * h_local[i];
      const Vec3 offsets[6] = {
          Vec3(+delta, 0.0, 0.0), Vec3(-delta, 0.0, 0.0),
          Vec3(0.0, +delta, 0.0), Vec3(0.0, -delta, 0.0),
          Vec3(0.0, 0.0, +delta), Vec3(0.0, 0.0, -delta),
      };
      for (int s = 0; s < 6; ++s) {
        const size_t p_idx = i * 6 + static_cast<size_t>(s);
        const Vec3 p = q_i + offsets[s];
        probe_pts[3 * p_idx + 0] = p.x;
        probe_pts[3 * p_idx + 1] = p.y;
        probe_pts[3 * p_idx + 2] = p.z;
      }
    }

    std::vector<double> probe_phis(n_probes, 0.0);
    std::vector<double> probe_grads(n_probes * 3, 0.0);
    st.sdf->evaluate(probe_pts.data(), static_cast<int64_t>(n_probes), probe_phis.data(), probe_grads.data());
    if (st.is_multi_shell_closed) {
      flipWindingInconsistentSDF(*st.sdf, probe_pts, probe_phis, probe_grads);
    }

    std::atomic<bool> qef_omp_failed{false};
#ifdef _OPENMP
#pragma omp parallel for schedule(static)
#endif
    for (int64_t ii = 0; ii < static_cast<int64_t>(n_wall); ++ii) {
      if (qef_omp_failed.load(std::memory_order_relaxed)) continue;
      try {
        const size_t i = static_cast<size_t>(ii);
        if (!eligible_qef[i]) continue;

        const Vec3 q_i = orig_wall_coords[i] + disp[i];
        std::array<Vec3, 7> normals{};
        std::array<Vec3, 7> plane_pts{};
        int m_planes = 0;

        const Vec3 g0(grads[3 * i + 0], grads[3 * i + 1], grads[3 * i + 2]);
        const double g02 = g0.norm2();
        if (g02 <= 1e-12) continue;
        normals[m_planes] = g0 / std::sqrt(g02);
        plane_pts[m_planes] = orig_wall_coords[i] - (phis[i] / g02) * g0;
        ++m_planes;

        for (int s = 0; s < 6; ++s) {
          const size_t p_idx = i * 6 + static_cast<size_t>(s);
          const Vec3 gk(probe_grads[3 * p_idx + 0], probe_grads[3 * p_idx + 1], probe_grads[3 * p_idx + 2]);
          const double gk2 = gk.norm2();
          if (gk2 > 1e-12) {
            const Vec3 pk(probe_pts[3 * p_idx + 0], probe_pts[3 * p_idx + 1], probe_pts[3 * p_idx + 2]);
            normals[m_planes] = gk / std::sqrt(gk2);
            plane_pts[m_planes] = pk - (probe_phis[p_idx] / gk2) * gk;
            ++m_planes;
          }
        }

        double min_dot = 1.0;
        for (int a = 0; a < m_planes; ++a) {
          for (int b = a + 1; b < m_planes; ++b) {
            min_dot = std::min(min_dot, dot(normals[a], normals[b]));
          }
        }

        // Only apply QEF when normals indicate a sharp crease/corner (< cos 35 deg)
        if (min_dot < 0.819152) {
          double A[3][3] = {{0.0, 0.0, 0.0}, {0.0, 0.0, 0.0}, {0.0, 0.0, 0.0}};
          Vec3 rhs(0.0, 0.0, 0.0);
          for (int k = 0; k < m_planes; ++k) {
            const Vec3& n = normals[k];
            const double d_plane = dot(n, plane_pts[k] - q_i);
            A[0][0] += n.x * n.x;
            A[0][1] += n.x * n.y;
            A[0][2] += n.x * n.z;
            A[1][1] += n.y * n.y;
            A[1][2] += n.y * n.z;
            A[2][2] += n.z * n.z;
            rhs += n * d_plane;
          }
          A[1][0] = A[0][1];
          A[2][0] = A[0][2];
          A[2][1] = A[1][2];

          Vec3 delta_q = solveQefJacobi3x3(A, rhs);
          const double dq_len = delta_q.norm();
          const double max_dq = 0.45 * h_local[i];
          if (dq_len > max_dq && dq_len > 1e-15) {
            delta_q *= (max_dq / dq_len);
          }

          Vec3 d_new = disp[i] + delta_q;
          const double dn_len = d_new.norm();
          const double max_step = 0.95 * h_local[i];
          if (dn_len > max_step && dn_len > 1e-15) {
            d_new *= (max_step / dn_len);
          }
          if (d_new.norm2() > 0.0) {
            disp[i] = d_new;
            alpha[i] = 1.0;
            st.node_coords[static_cast<size_t>(wall_verts[i])] = orig_wall_coords[i] + d_new;
          }
        }
      } catch (...) {
        qef_omp_failed.store(true, std::memory_order_relaxed);
      }
    }
    if (qef_omp_failed.load(std::memory_order_relaxed)) {
      release_sdf_memory();
      return -12;
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
      release_sdf_memory();
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
    const double v_bg = st.cell_v_bg[static_cast<size_t>(cell_idx)];
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

  release_sdf_memory();
  return 0;
}

}  // namespace cfd::hexcore
