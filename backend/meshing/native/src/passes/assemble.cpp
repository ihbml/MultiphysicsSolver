#include "hexcore_state.hpp"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <vector>

#ifdef _OPENMP
#include <omp.h>
#endif

namespace cfd::hexcore {

int32_t passAssembleOutput(HexcoreState& st, NativeMeshData& mesh) {
  const int64_t final_n_cells = st.final_n_cells;
  const int64_t total_faces = st.total_faces;
  const int64_t n_internal = st.n_internal;

  mesh.points.resize(3 * st.node_coords.size());
#ifdef _OPENMP
#pragma omp parallel for schedule(static)
#endif
  for (int64_t i = 0; i < static_cast<int64_t>(st.node_coords.size()); ++i) {
    mesh.points[static_cast<size_t>(3 * i + 0)] = st.node_coords[static_cast<size_t>(i)].x + st.origin_shift.x;
    mesh.points[static_cast<size_t>(3 * i + 1)] = st.node_coords[static_cast<size_t>(i)].y + st.origin_shift.y;
    mesh.points[static_cast<size_t>(3 * i + 2)] = st.node_coords[static_cast<size_t>(i)].z + st.origin_shift.z;
  }

  mesh.cell_centres.resize(static_cast<size_t>(3 * final_n_cells));
  mesh.cell_volumes.resize(static_cast<size_t>(final_n_cells));
  mesh.cell_types.resize(static_cast<size_t>(final_n_cells));
  mesh.cell_regions.resize(static_cast<size_t>(final_n_cells), 0);

#ifdef _OPENMP
#pragma omp parallel for schedule(static)
#endif
  for (int64_t new_c = 0; new_c < final_n_cells; ++new_c) {
    int64_t old_c = st.new_to_old_active[static_cast<size_t>(new_c)];
    mesh.cell_centres[static_cast<size_t>(3 * new_c + 0)] = st.cell_cents[static_cast<size_t>(old_c)].x + st.origin_shift.x;
    mesh.cell_centres[static_cast<size_t>(3 * new_c + 1)] = st.cell_cents[static_cast<size_t>(old_c)].y + st.origin_shift.y;
    mesh.cell_centres[static_cast<size_t>(3 * new_c + 2)] = st.cell_cents[static_cast<size_t>(old_c)].z + st.origin_shift.z;
    mesh.cell_volumes[static_cast<size_t>(new_c)] = st.cell_vols[static_cast<size_t>(old_c)];
    mesh.cell_types[static_cast<size_t>(new_c)] = static_cast<int32_t>(st.cell_types[static_cast<size_t>(old_c)]);
    mesh.cell_regions[static_cast<size_t>(new_c)] = st.cell_region[static_cast<size_t>(old_c)];
  }

  mesh.owner.resize(static_cast<size_t>(total_faces));
  mesh.neighbour.resize(static_cast<size_t>(total_faces));
  mesh.face_areas.resize(static_cast<size_t>(3 * total_faces));
  mesh.face_centres.resize(static_cast<size_t>(3 * total_faces));
  mesh.delta_f.resize(static_cast<size_t>(3 * total_faces));
  mesh.k_f.resize(static_cast<size_t>(3 * total_faces));
  mesh.non_ortho.resize(static_cast<size_t>(total_faces));
  mesh.skewness.resize(static_cast<size_t>(total_faces));
  mesh.stab_mask.resize(static_cast<size_t>(total_faces));
  mesh.m_skew.assign(static_cast<size_t>(3 * total_faces), 0.0);
  mesh.k_f_comp.assign(static_cast<size_t>(3 * total_faces), 0.0);
  mesh.beta_upwind.assign(static_cast<size_t>(total_faces), 1.0);
  mesh.omega_cell.assign(static_cast<size_t>(final_n_cells), 1.0);
  mesh.wsrd_alpha.assign(static_cast<size_t>(final_n_cells), 1.0);

  mesh.face_offsets = std::move(st.faces.node_offsets);
  mesh.face_nodes = std::move(st.faces.node_ids);
  std::vector<int64_t>{0}.swap(st.faces.node_offsets);
  std::vector<int64_t>().swap(st.faces.node_ids);

#ifdef _OPENMP
#pragma omp parallel for schedule(static)
#endif
  for (int64_t f = 0; f < total_faces; ++f) {
    const size_t fs = static_cast<size_t>(f);
    mesh.owner[fs] = st.faces.owner[fs];
    mesh.neighbour[fs] = st.faces.neighbour[fs];

    Vec3 sf = st.faces.normal_owner[fs];
    Vec3 cf = st.faces.centre[fs];

    mesh.face_areas[3 * fs + 0] = sf.x;
    mesh.face_areas[3 * fs + 1] = sf.y;
    mesh.face_areas[3 * fs + 2] = sf.z;

    mesh.face_centres[3 * fs + 0] = cf.x + st.origin_shift.x;
    mesh.face_centres[3 * fs + 1] = cf.y + st.origin_shift.y;
    mesh.face_centres[3 * fs + 2] = cf.z + st.origin_shift.z;

    int64_t o = mesh.owner[fs];
    int64_t nb = mesh.neighbour[fs];
    Vec3 co(mesh.cell_centres[static_cast<size_t>(3 * o + 0)],
            mesh.cell_centres[static_cast<size_t>(3 * o + 1)],
            mesh.cell_centres[static_cast<size_t>(3 * o + 2)]);

    if (nb >= 0) {
      Vec3 cnb(mesh.cell_centres[static_cast<size_t>(3 * nb + 0)],
               mesh.cell_centres[static_cast<size_t>(3 * nb + 1)],
               mesh.cell_centres[static_cast<size_t>(3 * nb + 2)]);
      Vec3 df = cnb - co;
      double df_norm = df.norm();
      double sf_norm = sf.norm();
      double sf_dot_df = dot(sf, df);

      Vec3 delta;
      Vec3 k;
      if (std::abs(sf_dot_df) > 1e-15) {
        delta = df * (sf.norm2() / sf_dot_df);
        k = sf - delta;
      } else {
        delta = sf;
        k = Vec3(0, 0, 0);
      }

      mesh.delta_f[3 * fs + 0] = delta.x;
      mesh.delta_f[3 * fs + 1] = delta.y;
      mesh.delta_f[3 * fs + 2] = delta.z;

      mesh.k_f[3 * fs + 0] = k.x;
      mesh.k_f[3 * fs + 1] = k.y;
      mesh.k_f[3 * fs + 2] = k.z;

      double cos_theta = 1.0;
      if (sf_norm > 1e-15 && df_norm > 1e-15) {
        cos_theta = std::clamp(sf_dot_df / (sf_norm * df_norm), -1.0, 1.0);
      }
      double theta_deg = std::acos(cos_theta) * 180.0 / 3.14159265358979323846;
      mesh.non_ortho[fs] = theta_deg;

      double skew = 0.0;
      Vec3 cf_world = cf + st.origin_shift;
      Vec3 x_int = cf_world;
      if (sf_norm > 1e-15 && df_norm > 1e-15) {
        Vec3 nf = sf / sf_norm;
        double df_dot_nf = dot(df, nf);
        if (std::abs(df_dot_nf) > 1e-15) {
          double t = dot(cf_world - co, nf) / df_dot_nf;
          x_int = co + df * t;
          skew = (cf_world - x_int).norm() / df_norm;
        }
      }
      mesh.skewness[fs] = skew;
      Vec3 m_vec = cf_world - x_int;
      mesh.m_skew[3 * fs + 0] = m_vec.x;
      mesh.m_skew[3 * fs + 1] = m_vec.y;
      mesh.m_skew[3 * fs + 2] = m_vec.z;

      double k_norm = k.norm();
      double factor = std::min(1.0, (1.5 * delta.norm()) / (k_norm + 1e-15));
      Vec3 k_comp = k * factor;
      mesh.k_f_comp[3 * fs + 0] = k_comp.x;
      mesh.k_f_comp[3 * fs + 1] = k_comp.y;
      mesh.k_f_comp[3 * fs + 2] = k_comp.z;

      if (sf_dot_df <= 0.0) {
        mesh.stab_mask[fs] = 3;
        mesh.beta_upwind[fs] = 0.0;
      } else {
        double beta_ortho = std::clamp((75.0 - theta_deg) / 35.0, 0.0, 1.0);
        double beta_skew = std::clamp((1.0 - skew) / 0.6, 0.0, 1.0);
        mesh.beta_upwind[fs] = std::min(beta_ortho, beta_skew);
        mesh.stab_mask[fs] =
            (theta_deg >= 75.0 || skew >= 1.0) ? 3
            : ((theta_deg >= 65.0 || skew >= 0.7) ? 2
               : ((theta_deg >= 40.0 || skew >= 0.4) ? 1 : 0));
      }
    } else {
      Vec3 df = (cf + st.origin_shift) - co;
      mesh.delta_f[3 * fs + 0] = df.x;
      mesh.delta_f[3 * fs + 1] = df.y;
      mesh.delta_f[3 * fs + 2] = df.z;
      mesh.k_f[3 * fs + 0] = 0.0;
      mesh.k_f[3 * fs + 1] = 0.0;
      mesh.k_f[3 * fs + 2] = 0.0;
      mesh.non_ortho[fs] = 0.0;
      mesh.skewness[fs] = 0.0;
      mesh.stab_mask[fs] = (dot(sf, df) <= 0.0) ? 3 : 0;
      mesh.beta_upwind[fs] = (dot(sf, df) <= 0.0) ? 0.0 : 1.0;
    }
  }

  std::vector<double> min_beta_per_cell(static_cast<size_t>(final_n_cells), 1.0);
  for (int64_t f = 0; f < total_faces; ++f) {
    const size_t fs = static_cast<size_t>(f);
    int64_t o = mesh.owner[fs];
    int64_t nb = mesh.neighbour[fs];
    if (o >= 0 && o < final_n_cells) {
      min_beta_per_cell[static_cast<size_t>(o)] =
          std::min(min_beta_per_cell[static_cast<size_t>(o)], mesh.beta_upwind[fs]);
    }
    if (nb >= 0 && nb < final_n_cells) {
      min_beta_per_cell[static_cast<size_t>(nb)] =
          std::min(min_beta_per_cell[static_cast<size_t>(nb)], mesh.beta_upwind[fs]);
    }
  }
#ifdef _OPENMP
#pragma omp parallel for schedule(static)
#endif
  for (int64_t c = 0; c < final_n_cells; ++c) {
    mesh.omega_cell[static_cast<size_t>(c)] = std::max(0.1, min_beta_per_cell[static_cast<size_t>(c)]);
  }
  std::vector<double>().swap(min_beta_per_cell);

  // Build mesh.patches before clearing st.faces.patch_id
  const size_t n_bnd_total = static_cast<size_t>(total_faces - n_internal);
  if (n_bnd_total > 0) {
    PatchId current_pid = st.faces.patch_id[static_cast<size_t>(n_internal)];
    if (current_pid == PatchId::INTERNAL) current_pid = PatchId::WALL;
    int64_t start_f = n_internal;
    int64_t count = 0;

    for (size_t i = 0; i < n_bnd_total; ++i) {
      PatchId pid = st.faces.patch_id[static_cast<size_t>(n_internal) + i];
      if (pid == PatchId::INTERNAL) pid = PatchId::WALL;
      if (pid == current_pid) {
        ++count;
      } else {
        const char* pname = patchIdToName(current_pid);
        BoundaryPatchC bp{};
        std::snprintf(bp.name, sizeof(bp.name), "%s", pname);
        std::snprintf(bp.patch_type, sizeof(bp.patch_type), "%s", pname);
        bp.start_face = start_f;
        bp.num_faces = count;
        bp.region_id = 0;
        mesh.patches.push_back(bp);

        current_pid = pid;
        start_f = n_internal + static_cast<int64_t>(i);
        count = 1;
      }
    }

    if (count > 0) {
      const char* pname = patchIdToName(current_pid);
      BoundaryPatchC bp{};
      std::snprintf(bp.name, sizeof(bp.name), "%s", pname);
      std::snprintf(bp.patch_type, sizeof(bp.patch_type), "%s", pname);
      bp.start_face = start_f;
      bp.num_faces = count;
      bp.region_id = 0;
      mesh.patches.push_back(bp);
    }
  }

  // Face metadata arrays in st.faces are no longer needed
  st.faces.clear_and_shrink();

  // Build cell_offsets and cell_nodes directly without st.cell_verts
  // Check HEXAHEDRON cells for 8 distinct corners; promote degenerate ones to POLYHEDRON
#ifdef _OPENMP
#pragma omp parallel for schedule(static)
#endif
  for (int64_t new_c = 0; new_c < final_n_cells; ++new_c) {
    if (mesh.cell_types[static_cast<size_t>(new_c)] != static_cast<int32_t>(CellType::HEXAHEDRON)) {
      continue;
    }
    const int64_t old_c = st.new_to_old_active[static_cast<size_t>(new_c)];
    const auto& hc = st.hex_corners[static_cast<size_t>(old_c)];
    bool distinct = true;
    for (int i = 0; i < 8 && distinct; ++i) {
      for (int j = i + 1; j < 8; ++j) {
        if (hc[static_cast<size_t>(i)] == hc[static_cast<size_t>(j)]) {
          distinct = false;
          break;
        }
      }
    }
    if (!distinct) {
      mesh.cell_types[static_cast<size_t>(new_c)] = static_cast<int32_t>(CellType::POLYHEDRON);
    }
  }

  // 2-pass CSR collection for POLYHEDRON cells from mesh.face_offsets / mesh.face_nodes
  std::vector<int64_t> poly_offsets(static_cast<size_t>(final_n_cells) + 1, 0);
  for (int64_t f = 0; f < total_faces; ++f) {
    const size_t fs = static_cast<size_t>(f);
    const int64_t nv = mesh.face_offsets[fs + 1] - mesh.face_offsets[fs];
    const int64_t o = mesh.owner[fs];
    const int64_t nb = mesh.neighbour[fs];
    if (mesh.cell_types[static_cast<size_t>(o)] == static_cast<int32_t>(CellType::POLYHEDRON)) {
      poly_offsets[static_cast<size_t>(o) + 1] += nv;
    }
    if (nb >= 0 && nb < final_n_cells &&
        mesh.cell_types[static_cast<size_t>(nb)] == static_cast<int32_t>(CellType::POLYHEDRON)) {
      poly_offsets[static_cast<size_t>(nb) + 1] += nv;
    }
  }
  for (int64_t c = 0; c < final_n_cells; ++c) {
    poly_offsets[static_cast<size_t>(c + 1)] += poly_offsets[static_cast<size_t>(c)];
  }
  std::vector<int64_t> poly_raw_nodes(static_cast<size_t>(poly_offsets[static_cast<size_t>(final_n_cells)]));
  std::vector<int64_t> poly_cursor(poly_offsets.begin(), poly_offsets.begin() + final_n_cells);
  for (int64_t f = 0; f < total_faces; ++f) {
    const size_t fs = static_cast<size_t>(f);
    const int64_t f_beg = mesh.face_offsets[fs];
    const int64_t f_end = mesh.face_offsets[fs + 1];
    const int64_t o = mesh.owner[fs];
    const int64_t nb = mesh.neighbour[fs];
    if (mesh.cell_types[static_cast<size_t>(o)] == static_cast<int32_t>(CellType::POLYHEDRON)) {
      int64_t& cur = poly_cursor[static_cast<size_t>(o)];
      for (int64_t k = f_beg; k < f_end; ++k) {
        poly_raw_nodes[static_cast<size_t>(cur++)] = mesh.face_nodes[static_cast<size_t>(k)];
      }
    }
    if (nb >= 0 && nb < final_n_cells &&
        mesh.cell_types[static_cast<size_t>(nb)] == static_cast<int32_t>(CellType::POLYHEDRON)) {
      int64_t& cur = poly_cursor[static_cast<size_t>(nb)];
      for (int64_t k = f_beg; k < f_end; ++k) {
        poly_raw_nodes[static_cast<size_t>(cur++)] = mesh.face_nodes[static_cast<size_t>(k)];
      }
    }
  }
  std::vector<int64_t>().swap(poly_cursor);

  std::vector<int64_t> cell_node_counts(static_cast<size_t>(final_n_cells), 8);
#ifdef _OPENMP
#pragma omp parallel for schedule(static)
#endif
  for (int64_t new_c = 0; new_c < final_n_cells; ++new_c) {
    if (mesh.cell_types[static_cast<size_t>(new_c)] == static_cast<int32_t>(CellType::POLYHEDRON)) {
      auto beg = poly_raw_nodes.begin() + poly_offsets[static_cast<size_t>(new_c)];
      auto end = poly_raw_nodes.begin() + poly_offsets[static_cast<size_t>(new_c) + 1];
      std::sort(beg, end);
      auto u_end = std::unique(beg, end);
      cell_node_counts[static_cast<size_t>(new_c)] = static_cast<int64_t>(u_end - beg);
    }
  }

  mesh.cell_offsets.resize(static_cast<size_t>(final_n_cells + 1), 0);
  for (int64_t c = 0; c < final_n_cells; ++c) {
    mesh.cell_offsets[static_cast<size_t>(c + 1)] =
        mesh.cell_offsets[static_cast<size_t>(c)] + cell_node_counts[static_cast<size_t>(c)];
  }
  mesh.cell_nodes.resize(static_cast<size_t>(mesh.cell_offsets[static_cast<size_t>(final_n_cells)]));

#ifdef _OPENMP
#pragma omp parallel for schedule(static)
#endif
  for (int64_t new_c = 0; new_c < final_n_cells; ++new_c) {
    const int64_t dst_off = mesh.cell_offsets[static_cast<size_t>(new_c)];
    if (mesh.cell_types[static_cast<size_t>(new_c)] == static_cast<int32_t>(CellType::HEXAHEDRON)) {
      const int64_t old_c = st.new_to_old_active[static_cast<size_t>(new_c)];
      const auto& hc = st.hex_corners[static_cast<size_t>(old_c)];
      for (size_t k = 0; k < 8; ++k) {
        mesh.cell_nodes[static_cast<size_t>(dst_off) + k] = hc[k];
      }
    } else {
      const int64_t src_off = poly_offsets[static_cast<size_t>(new_c)];
      const int64_t cnt = cell_node_counts[static_cast<size_t>(new_c)];
      for (int64_t k = 0; k < cnt; ++k) {
        mesh.cell_nodes[static_cast<size_t>(dst_off + k)] = poly_raw_nodes[static_cast<size_t>(src_off + k)];
      }
    }
  }
  std::vector<int64_t>().swap(poly_offsets);
  std::vector<int64_t>().swap(poly_raw_nodes);
  std::vector<int64_t>().swap(cell_node_counts);

  // Edge-based aspect ratio from true polygon face edges (AUD-029) & WSRD alpha
  std::vector<double> cell_min_edge(static_cast<size_t>(final_n_cells), 1e30);
  std::vector<double> cell_max_edge(static_cast<size_t>(final_n_cells), 0.0);
  for (int64_t f = 0; f < total_faces; ++f) {
    const size_t fs = static_cast<size_t>(f);
    const int64_t f_beg = mesh.face_offsets[fs];
    const int64_t f_end = mesh.face_offsets[fs + 1];
    const size_t nv = static_cast<size_t>(f_end - f_beg);
    if (nv < 2) continue;
    const int64_t* fnodes = mesh.face_nodes.data() + f_beg;
    int64_t o = mesh.owner[fs];
    int64_t nb = mesh.neighbour[fs];
    for (size_t k = 0; k < nv; ++k) {
      const Vec3& p1 = st.node_coords[static_cast<size_t>(fnodes[k])];
      const Vec3& p2 = st.node_coords[static_cast<size_t>(fnodes[(k + 1) % nv])];
      double d = (p1 - p2).norm();
      if (d <= 1e-15) continue;
      if (o >= 0 && o < final_n_cells) {
        cell_min_edge[static_cast<size_t>(o)] = std::min(cell_min_edge[static_cast<size_t>(o)], d);
        cell_max_edge[static_cast<size_t>(o)] = std::max(cell_max_edge[static_cast<size_t>(o)], d);
      }
      if (nb >= 0 && nb < final_n_cells) {
        cell_min_edge[static_cast<size_t>(nb)] = std::min(cell_min_edge[static_cast<size_t>(nb)], d);
        cell_max_edge[static_cast<size_t>(nb)] = std::max(cell_max_edge[static_cast<size_t>(nb)], d);
      }
    }
  }

  mesh.aspect_ratio.resize(static_cast<size_t>(final_n_cells), 1.0);
#ifdef _OPENMP
#pragma omp parallel for schedule(static)
#endif
  for (int64_t new_c = 0; new_c < final_n_cells; ++new_c) {
    double min_e = cell_min_edge[static_cast<size_t>(new_c)];
    double max_e = cell_max_edge[static_cast<size_t>(new_c)];
    if (min_e < 1e20 && min_e > 1e-15) {
      mesh.aspect_ratio[static_cast<size_t>(new_c)] = std::max(1.0, max_e / min_e);
    } else {
      mesh.aspect_ratio[static_cast<size_t>(new_c)] = 1.0;
    }

    int64_t old_c = st.new_to_old_active[static_cast<size_t>(new_c)];
    double v_bg = st.cell_v_bg[static_cast<size_t>(old_c)];
    double alpha = std::clamp(st.cell_vols[static_cast<size_t>(old_c)] / (v_bg + 1e-30), 0.0, 1.0);
    mesh.wsrd_alpha[static_cast<size_t>(new_c)] = alpha;
  }

  // Free remaining HexcoreState working buffers
  std::vector<std::array<int64_t, 8>>().swap(st.hex_corners);
  std::vector<double>().swap(st.cell_vols);
  std::vector<Vec3>().swap(st.cell_cents);
  std::vector<Vec3>().swap(st.node_coords);
  std::vector<double>().swap(st.cell_v_bg);
  std::vector<double>().swap(st.cell_h_min);
  std::vector<int16_t>().swap(st.cell_region);
  std::vector<int64_t>().swap(st.new_to_old_active);
  std::vector<CellType>().swap(st.cell_types);

  mesh.topological_dim = 3;
  return 0;
}

}  // namespace cfd::hexcore
