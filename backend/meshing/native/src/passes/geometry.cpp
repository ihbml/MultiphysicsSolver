#include "hexcore_state.hpp"

#include <algorithm>
#include <cmath>
#include <vector>

#ifdef _OPENMP
#include <omp.h>
#endif

namespace cfd::hexcore {

int32_t passComputeGeometryAndSplitWarped(HexcoreState& st) {
  int64_t n_active_cells = st.n_active_cells;

#ifdef _OPENMP
#pragma omp parallel for schedule(static)
#endif
  for (int64_t f = 0; f < static_cast<int64_t>(st.mosaic_faces.size()); ++f) {
    auto [normal, centre] = computePolygonNormalAndCentroid(st.mosaic_faces[static_cast<size_t>(f)].nodes, st.node_coords);
    st.mosaic_faces[static_cast<size_t>(f)].normal_owner = normal;
    st.mosaic_faces[static_cast<size_t>(f)].centre = centre;
    st.mosaic_faces[static_cast<size_t>(f)].is_folded = isFaceFolded(st.mosaic_faces[static_cast<size_t>(f)].nodes, st.node_coords, normal);
  }

  // Split non-planar / warped or folded polygonal faces (nv >= 4) into strictly planar triangles
  {
    size_t orig_n_faces = st.mosaic_faces.size();
    for (size_t f = 0; f < orig_n_faces; ++f) {
      size_t nv = st.mosaic_faces[f].nodes.size();
      if (nv < 4) continue;
      Vec3 fn = st.mosaic_faces[f].normal_owner;
      Vec3 fc = st.mosaic_faces[f].centre;
      double area = fn.norm();
      if (area <= 1e-24) continue;
      Vec3 unit_n = fn / area;
      double max_dev = 0.0;
      for (int64_t vid : st.mosaic_faces[f].nodes) {
        max_dev = std::max(max_dev, std::abs(dot(st.node_coords[static_cast<size_t>(vid)] - fc, unit_n)));
      }
      if (!st.mosaic_faces[f].is_folded && max_dev <= 1e-11 * std::sqrt(area)) {
        continue;
      }

      MosaicFace orig = st.mosaic_faces[f];
      auto make_tri_face = [&](int64_t a, int64_t b, int64_t c) -> MosaicFace {
        MosaicFace tf;
        tf.nodes = {a, b, c};
        tf.owner = orig.owner;
        tf.neighbour = orig.neighbour;
        tf.patch_name = orig.patch_name;
        tf.is_farfield = orig.is_farfield;
        tf.is_cut_face = orig.is_cut_face;
        tf.is_folded = false;
        const Vec3& pa = st.node_coords[static_cast<size_t>(a)];
        const Vec3& pb = st.node_coords[static_cast<size_t>(b)];
        const Vec3& pc = st.node_coords[static_cast<size_t>(c)];
        tf.normal_owner = cross(pb - pa, pc - pa) * 0.5;
        tf.centre = (pa + pb + pc) / 3.0;
        return tf;
      };

      if (nv == 4) {
        int64_t n0 = orig.nodes[0], n1 = orig.nodes[1], n2 = orig.nodes[2], n3 = orig.nodes[3];
        MosaicFace t02_a = make_tri_face(n0, n1, n2);
        MosaicFace t02_b = make_tri_face(n0, n2, n3);
        double min02 = std::min(dot(t02_a.normal_owner, unit_n), dot(t02_b.normal_owner, unit_n));

        MosaicFace t13_a = make_tri_face(n1, n2, n3);
        MosaicFace t13_b = make_tri_face(n1, n3, n0);
        double min13 = std::min(dot(t13_a.normal_owner, unit_n), dot(t13_b.normal_owner, unit_n));

        if (min13 > min02) {
          st.mosaic_faces[f] = std::move(t13_a);
          int64_t new_f = static_cast<int64_t>(st.mosaic_faces.size());
          st.mosaic_faces.push_back(std::move(t13_b));
          if (orig.owner >= 0) st.cell_to_faces[static_cast<size_t>(orig.owner)].push_back(new_f);
          if (orig.neighbour >= 0) st.cell_to_faces[static_cast<size_t>(orig.neighbour)].push_back(new_f);
        } else {
          st.mosaic_faces[f] = std::move(t02_a);
          int64_t new_f = static_cast<int64_t>(st.mosaic_faces.size());
          st.mosaic_faces.push_back(std::move(t02_b));
          if (orig.owner >= 0) st.cell_to_faces[static_cast<size_t>(orig.owner)].push_back(new_f);
          if (orig.neighbour >= 0) st.cell_to_faces[static_cast<size_t>(orig.neighbour)].push_back(new_f);
        }
      } else {
        int64_t c_vid = static_cast<int64_t>(st.node_coords.size());
        st.node_coords.push_back(fc);
        st.mosaic_faces[f] = make_tri_face(orig.nodes[0], orig.nodes[1], c_vid);
        for (size_t i = 1; i < nv; ++i) {
          MosaicFace extra = make_tri_face(orig.nodes[i], orig.nodes[(i + 1) % nv], c_vid);
          int64_t new_f = static_cast<int64_t>(st.mosaic_faces.size());
          st.mosaic_faces.push_back(std::move(extra));
          if (orig.owner >= 0) st.cell_to_faces[static_cast<size_t>(orig.owner)].push_back(new_f);
          if (orig.neighbour >= 0) st.cell_to_faces[static_cast<size_t>(orig.neighbour)].push_back(new_f);
        }
      }

      if (orig.owner >= 0) st.cell_types[static_cast<size_t>(orig.owner)] = CellType::POLYHEDRON;
      if (orig.neighbour >= 0) st.cell_types[static_cast<size_t>(orig.neighbour)] = CellType::POLYHEDRON;
    }
  }

  st.cell_approx_center.assign(static_cast<size_t>(n_active_cells), Vec3(0, 0, 0));
#ifdef _OPENMP
#pragma omp parallel for schedule(static)
#endif
  for (int64_t c = 0; c < n_active_cells; ++c) {
    Vec3 c_approx(0, 0, 0);
    int64_t count = 0;
    for (int64_t f_idx : st.cell_to_faces[static_cast<size_t>(c)]) {
      c_approx += st.mosaic_faces[static_cast<size_t>(f_idx)].centre;
      ++count;
    }
    st.cell_approx_center[static_cast<size_t>(c)] =
        (count > 0) ? (c_approx / static_cast<double>(count))
                    : st.nodes[static_cast<size_t>(st.active_leaves[static_cast<size_t>(c)])].center();
  }

  st.cell_vols.assign(static_cast<size_t>(n_active_cells), 0.0);
  st.cell_cents.assign(static_cast<size_t>(n_active_cells), Vec3(0, 0, 0));

#ifdef _OPENMP
#pragma omp parallel for schedule(static)
#endif
  for (int64_t c = 0; c < n_active_cells; ++c) {
    Vec3 c_approx = st.cell_approx_center[static_cast<size_t>(c)];
    double vol_sum = 0.0;
    Vec3 cent_sum(0, 0, 0);

    for (int64_t f_idx : st.cell_to_faces[static_cast<size_t>(c)]) {
      const auto& mf = st.mosaic_faces[static_cast<size_t>(f_idx)];
      double sign = (mf.owner == c) ? 1.0 : -1.0;
      Vec3 sf_out = mf.normal_owner * sign;
      double pyr_vol = dot(mf.centre - c_approx, sf_out) / 3.0;
      vol_sum += pyr_vol;
      Vec3 pyr_c = mf.centre * 0.75 + c_approx * 0.25;
      cent_sum += pyr_c * pyr_vol;
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
