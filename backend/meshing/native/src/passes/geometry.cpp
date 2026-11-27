#include "hexcore_state.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <vector>

#ifdef _OPENMP
#include <omp.h>
#endif

namespace cfd::hexcore {

int32_t passComputeGeometryAndSplitWarped(HexcoreState& st) {
  const int64_t n_active_cells = st.n_active_cells;
  const int64_t orig_n_faces = st.faces.size();

#ifdef _OPENMP
#pragma omp parallel for schedule(static)
#endif
  for (int64_t f = 0; f < orig_n_faces; ++f) {
    const size_t fs = static_cast<size_t>(f);
    const int64_t* fnodes = st.faces.face_nodes(fs);
    const size_t nv = st.faces.face_nv(fs);
    auto [normal, centre] = computePolygonNormalAndCentroid(fnodes, nv, st.node_coords);
    st.faces.normal_owner[fs] = normal;
    st.faces.centre[fs] = centre;
    if (isFaceFolded(fnodes, nv, st.node_coords, normal)) {
      st.faces.flags[fs] |= kFlagFolded;
    } else {
      st.faces.flags[fs] &= static_cast<uint8_t>(~kFlagFolded);
    }
  }

  // Split non-planar / warped or folded polygonal faces (nv >= 4) into strictly planar triangles
  bool any_split = false;
  for (int64_t f = 0; f < orig_n_faces; ++f) {
    const size_t fs = static_cast<size_t>(f);
    const size_t nv = st.faces.face_nv(fs);
    if (nv < 4) continue;
    const Vec3 fn = st.faces.normal_owner[fs];
    const Vec3 fc = st.faces.centre[fs];
    const double area = fn.norm();
    if (area <= 1e-24) continue;
    const Vec3 unit_n = fn / area;
    const int64_t* fnodes = st.faces.face_nodes(fs);
    double max_dev = 0.0;
    for (size_t k = 0; k < nv; ++k) {
      max_dev = std::max(max_dev, std::abs(dot(st.node_coords[static_cast<size_t>(fnodes[k])] - fc, unit_n)));
    }
    const bool is_folded = (st.faces.flags[fs] & kFlagFolded) != 0;
    if (!is_folded && max_dev <= 1e-11 * std::sqrt(area)) {
      continue;
    }

    any_split = true;
    const int64_t orig_owner = st.faces.owner[fs];
    const int64_t orig_neighbour = st.faces.neighbour[fs];
    const PatchId orig_pid = st.faces.patch_id[fs];
    const uint8_t orig_fl = static_cast<uint8_t>(st.faces.flags[fs] & ~kFlagFolded);

    // Copy original face nodes before append_face potentially reallocates st.faces.node_ids
    std::array<int64_t, 16> stack_nodes;
    std::vector<int64_t> heap_nodes;
    const int64_t* orig_nodes_ptr = nullptr;
    if (nv <= stack_nodes.size()) {
      for (size_t k = 0; k < nv; ++k) stack_nodes[k] = fnodes[k];
      orig_nodes_ptr = stack_nodes.data();
    } else {
      heap_nodes.assign(fnodes, fnodes + nv);
      orig_nodes_ptr = heap_nodes.data();
    }

    // Mark original warped face dead; append strictly planar triangles
    st.faces.owner[fs] = -1;
    st.faces.neighbour[fs] = -1;

    auto append_tri = [&](int64_t a, int64_t b, int64_t c) {
      const Vec3& pa = st.node_coords[static_cast<size_t>(a)];
      const Vec3& pb = st.node_coords[static_cast<size_t>(b)];
      const Vec3& pc = st.node_coords[static_cast<size_t>(c)];
      const Vec3 tri_n = cross(pb - pa, pc - pa) * 0.5;
      const Vec3 tri_c = (pa + pb + pc) / 3.0;
      const int64_t tri[3] = {a, b, c};
      st.faces.append_face(tri, 3, orig_owner, orig_neighbour, orig_pid, orig_fl, tri_n, tri_c);
    };

    if (nv == 4) {
      const int64_t n0 = orig_nodes_ptr[0];
      const int64_t n1 = orig_nodes_ptr[1];
      const int64_t n2 = orig_nodes_ptr[2];
      const int64_t n3 = orig_nodes_ptr[3];

      auto tri_normal = [&](int64_t a, int64_t b, int64_t c) {
        const Vec3& pa = st.node_coords[static_cast<size_t>(a)];
        const Vec3& pb = st.node_coords[static_cast<size_t>(b)];
        const Vec3& pc = st.node_coords[static_cast<size_t>(c)];
        return cross(pb - pa, pc - pa) * 0.5;
      };

      const double min02 = std::min(dot(tri_normal(n0, n1, n2), unit_n), dot(tri_normal(n0, n2, n3), unit_n));
      const double min13 = std::min(dot(tri_normal(n1, n2, n3), unit_n), dot(tri_normal(n1, n3, n0), unit_n));

      if (min13 > min02) {
        append_tri(n1, n2, n3);
        append_tri(n1, n3, n0);
      } else {
        append_tri(n0, n1, n2);
        append_tri(n0, n2, n3);
      }
    } else {
      const int64_t c_vid = static_cast<int64_t>(st.node_coords.size());
      st.node_coords.push_back(fc);
      for (size_t i = 0; i < nv; ++i) {
        append_tri(orig_nodes_ptr[i], orig_nodes_ptr[(i + 1) % nv], c_vid);
      }
    }

    if (orig_owner >= 0) st.cell_types[static_cast<size_t>(orig_owner)] = CellType::POLYHEDRON;
    if (orig_neighbour >= 0) st.cell_types[static_cast<size_t>(orig_neighbour)] = CellType::POLYHEDRON;
  }

  if (any_split) {
    rebuildCellToFacesCSR(st);
  }

#ifdef _OPENMP
#pragma omp parallel for schedule(static)
#endif
  for (int64_t c = 0; c < n_active_cells; ++c) {
    const int64_t f_beg = st.cell_face_offsets[static_cast<size_t>(c)];
    const int64_t f_end = st.cell_face_offsets[static_cast<size_t>(c) + 1];
    if (f_end > f_beg) {
      Vec3 c_approx(0, 0, 0);
      for (int64_t ci = f_beg; ci < f_end; ++ci) {
        const size_t f_idx = static_cast<size_t>(st.cell_face_ids[static_cast<size_t>(ci)]);
        c_approx += st.faces.centre[f_idx];
      }
      st.cell_approx_center[static_cast<size_t>(c)] = c_approx / static_cast<double>(f_end - f_beg);
    }
  }

  st.cell_vols.assign(static_cast<size_t>(n_active_cells), 0.0);
  st.cell_cents.assign(static_cast<size_t>(n_active_cells), Vec3(0, 0, 0));

#ifdef _OPENMP
#pragma omp parallel for schedule(static)
#endif
  for (int64_t c = 0; c < n_active_cells; ++c) {
    const Vec3 c_approx = st.cell_approx_center[static_cast<size_t>(c)];
    double vol_sum = 0.0;
    Vec3 cent_sum(0, 0, 0);

    const int64_t f_beg = st.cell_face_offsets[static_cast<size_t>(c)];
    const int64_t f_end = st.cell_face_offsets[static_cast<size_t>(c) + 1];
    for (int64_t ci = f_beg; ci < f_end; ++ci) {
      const size_t f_idx = static_cast<size_t>(st.cell_face_ids[static_cast<size_t>(ci)]);
      const double sign = (st.faces.owner[f_idx] == c) ? 1.0 : -1.0;
      const int64_t* fnodes = st.faces.face_nodes(f_idx);
      const size_t nv = st.faces.face_nv(f_idx);
      if (nv >= 3) {
        Vec3 c_avg(0, 0, 0);
        for (size_t k = 0; k < nv; ++k) {
          c_avg += st.node_coords[static_cast<size_t>(fnodes[k])];
        }
        c_avg /= static_cast<double>(nv);
        for (size_t k = 0; k < nv; ++k) {
          const Vec3& p_a = st.node_coords[static_cast<size_t>(fnodes[k])];
          const Vec3& p_b = st.node_coords[static_cast<size_t>(fnodes[(k + 1) % nv])];
          const Vec3 tri_s = cross(p_a - c_avg, p_b - c_avg) * (0.5 * sign);
          const Vec3 tri_c = (c_avg + p_a + p_b) / 3.0;
          const double pyr_vol = dot(tri_c - c_approx, tri_s) / 3.0;
          vol_sum += pyr_vol;
          const Vec3 pyr_c = tri_c * 0.75 + c_approx * 0.25;
          cent_sum += pyr_c * pyr_vol;
        }
      } else {
        const Vec3 sf_out = st.faces.normal_owner[f_idx] * sign;
        const Vec3& fc = st.faces.centre[f_idx];
        const double pyr_vol = dot(fc - c_approx, sf_out) / 3.0;
        vol_sum += pyr_vol;
        const Vec3 pyr_c = fc * 0.75 + c_approx * 0.25;
        cent_sum += pyr_c * pyr_vol;
      }
    }

    st.cell_vols[static_cast<size_t>(c)] = vol_sum;
    if (std::abs(vol_sum) > 1e-15) {
      st.cell_cents[static_cast<size_t>(c)] = cent_sum / vol_sum;
    } else {
      st.cell_cents[static_cast<size_t>(c)] = c_approx;
      st.cell_vols[static_cast<size_t>(c)] = 0.0;
    }
  }

  return 0;
}

}  // namespace cfd::hexcore
