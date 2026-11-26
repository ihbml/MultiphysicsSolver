#include "hexcore_state.hpp"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

#ifdef _OPENMP
#include <omp.h>
#endif

namespace cfd::hexcore {

int32_t passAssembleOutput(HexcoreState& st, NativeMeshData& mesh) {
  int64_t final_n_cells = st.final_n_cells;
  int64_t total_faces = st.total_faces;
  int64_t n_internal = st.n_internal;

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
    mesh.cell_regions[static_cast<size_t>(new_c)] =
        st.nodes[static_cast<size_t>(st.active_leaves[static_cast<size_t>(old_c)])].region;
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

  mesh.face_offsets.resize(static_cast<size_t>(total_faces + 1), 0);
  for (int64_t f = 0; f < total_faces; ++f) {
    mesh.face_offsets[static_cast<size_t>(f + 1)] =
        mesh.face_offsets[static_cast<size_t>(f)] + static_cast<int64_t>(st.all_faces[static_cast<size_t>(f)].nodes.size());
  }
  mesh.face_nodes.resize(static_cast<size_t>(mesh.face_offsets[static_cast<size_t>(total_faces)]));

#ifdef _OPENMP
#pragma omp parallel for schedule(static)
#endif
  for (int64_t f = 0; f < total_faces; ++f) {
    int64_t off = mesh.face_offsets[static_cast<size_t>(f)];
    const auto& fnodes = st.all_faces[static_cast<size_t>(f)].nodes;
    for (size_t k = 0; k < fnodes.size(); ++k) {
      mesh.face_nodes[static_cast<size_t>(off) + k] = fnodes[k];
    }

    mesh.owner[static_cast<size_t>(f)] = st.all_faces[static_cast<size_t>(f)].owner;
    mesh.neighbour[static_cast<size_t>(f)] = st.all_faces[static_cast<size_t>(f)].neighbour;

    Vec3 sf = st.all_faces[static_cast<size_t>(f)].normal_owner;
    Vec3 cf = st.all_faces[static_cast<size_t>(f)].centre;

    mesh.face_areas[static_cast<size_t>(3 * f + 0)] = sf.x;
    mesh.face_areas[static_cast<size_t>(3 * f + 1)] = sf.y;
    mesh.face_areas[static_cast<size_t>(3 * f + 2)] = sf.z;

    mesh.face_centres[static_cast<size_t>(3 * f + 0)] = cf.x + st.origin_shift.x;
    mesh.face_centres[static_cast<size_t>(3 * f + 1)] = cf.y + st.origin_shift.y;
    mesh.face_centres[static_cast<size_t>(3 * f + 2)] = cf.z + st.origin_shift.z;

    int64_t o = mesh.owner[static_cast<size_t>(f)];
    int64_t nb = mesh.neighbour[static_cast<size_t>(f)];
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

      mesh.delta_f[static_cast<size_t>(3 * f + 0)] = delta.x;
      mesh.delta_f[static_cast<size_t>(3 * f + 1)] = delta.y;
      mesh.delta_f[static_cast<size_t>(3 * f + 2)] = delta.z;

      mesh.k_f[static_cast<size_t>(3 * f + 0)] = k.x;
      mesh.k_f[static_cast<size_t>(3 * f + 1)] = k.y;
      mesh.k_f[static_cast<size_t>(3 * f + 2)] = k.z;

      double cos_theta = 1.0;
      if (sf_norm > 1e-15 && df_norm > 1e-15) {
        cos_theta = std::clamp(sf_dot_df / (sf_norm * df_norm), -1.0, 1.0);
      }
      double theta_deg = std::acos(cos_theta) * 180.0 / 3.14159265358979323846;
      mesh.non_ortho[static_cast<size_t>(f)] = theta_deg;

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
      mesh.skewness[static_cast<size_t>(f)] = skew;
      Vec3 m_vec = cf_world - x_int;
      mesh.m_skew[static_cast<size_t>(3 * f + 0)] = m_vec.x;
      mesh.m_skew[static_cast<size_t>(3 * f + 1)] = m_vec.y;
      mesh.m_skew[static_cast<size_t>(3 * f + 2)] = m_vec.z;

      double k_norm = k.norm();
      double factor = std::min(1.0, (1.5 * delta.norm()) / (k_norm + 1e-15));
      Vec3 k_comp = k * factor;
      mesh.k_f_comp[static_cast<size_t>(3 * f + 0)] = k_comp.x;
      mesh.k_f_comp[static_cast<size_t>(3 * f + 1)] = k_comp.y;
      mesh.k_f_comp[static_cast<size_t>(3 * f + 2)] = k_comp.z;

      if (sf_dot_df <= 0.0) {
        mesh.stab_mask[static_cast<size_t>(f)] = 3;
        mesh.beta_upwind[static_cast<size_t>(f)] = 0.0;
      } else {
        double beta_ortho = std::clamp((75.0 - theta_deg) / 35.0, 0.0, 1.0);
        double beta_skew = std::clamp((1.0 - skew) / 0.6, 0.0, 1.0);
        mesh.beta_upwind[static_cast<size_t>(f)] = std::min(beta_ortho, beta_skew);
        mesh.stab_mask[static_cast<size_t>(f)] =
            (theta_deg >= 75.0 || skew >= 1.0) ? 3
            : ((theta_deg >= 65.0 || skew >= 0.7) ? 2
               : ((theta_deg >= 40.0 || skew >= 0.4) ? 1 : 0));
      }
    } else {
      Vec3 df = (cf + st.origin_shift) - co;
      mesh.delta_f[static_cast<size_t>(3 * f + 0)] = df.x;
      mesh.delta_f[static_cast<size_t>(3 * f + 1)] = df.y;
      mesh.delta_f[static_cast<size_t>(3 * f + 2)] = df.z;
      mesh.k_f[static_cast<size_t>(3 * f + 0)] = 0.0;
      mesh.k_f[static_cast<size_t>(3 * f + 1)] = 0.0;
      mesh.k_f[static_cast<size_t>(3 * f + 2)] = 0.0;
      mesh.non_ortho[static_cast<size_t>(f)] = 0.0;
      mesh.skewness[static_cast<size_t>(f)] = 0.0;
      mesh.stab_mask[static_cast<size_t>(f)] = (dot(sf, df) <= 0.0) ? 3 : 0;
      mesh.beta_upwind[static_cast<size_t>(f)] = (dot(sf, df) <= 0.0) ? 0.0 : 1.0;
    }
  }

  std::vector<double> min_beta_per_cell(static_cast<size_t>(final_n_cells), 1.0);
  for (int64_t f = 0; f < total_faces; ++f) {
    int64_t o = mesh.owner[static_cast<size_t>(f)];
    int64_t nb = mesh.neighbour[static_cast<size_t>(f)];
    if (o >= 0 && o < final_n_cells) {
      min_beta_per_cell[static_cast<size_t>(o)] =
          std::min(min_beta_per_cell[static_cast<size_t>(o)], mesh.beta_upwind[static_cast<size_t>(f)]);
    }
    if (nb >= 0 && nb < final_n_cells) {
      min_beta_per_cell[static_cast<size_t>(nb)] =
          std::min(min_beta_per_cell[static_cast<size_t>(nb)], mesh.beta_upwind[static_cast<size_t>(f)]);
    }
  }
#ifdef _OPENMP
#pragma omp parallel for schedule(static)
#endif
  for (int64_t c = 0; c < final_n_cells; ++c) {
    mesh.omega_cell[static_cast<size_t>(c)] = std::max(0.1, min_beta_per_cell[static_cast<size_t>(c)]);
  }

  std::vector<std::vector<int64_t>> cell_nodes_temp(static_cast<size_t>(final_n_cells));
  for (int64_t f = 0; f < total_faces; ++f) {
    int64_t o = mesh.owner[static_cast<size_t>(f)];
    if (mesh.cell_types[static_cast<size_t>(o)] == static_cast<int32_t>(CellType::POLYHEDRON)) {
      for (int64_t v : st.all_faces[static_cast<size_t>(f)].nodes) {
        cell_nodes_temp[static_cast<size_t>(o)].push_back(v);
      }
    }
    int64_t nb = mesh.neighbour[static_cast<size_t>(f)];
    if (nb >= 0 && nb < final_n_cells &&
        mesh.cell_types[static_cast<size_t>(nb)] == static_cast<int32_t>(CellType::POLYHEDRON)) {
      for (int64_t v : st.all_faces[static_cast<size_t>(f)].nodes) {
        cell_nodes_temp[static_cast<size_t>(nb)].push_back(v);
      }
    }
  }

  std::atomic<bool> omp_failed{false};
#ifdef _OPENMP
#pragma omp parallel for schedule(static)
#endif
  for (int64_t new_c = 0; new_c < final_n_cells; ++new_c) {
    if (omp_failed.load(std::memory_order_relaxed)) continue;
    try {
      int64_t old_c = st.new_to_old_active[static_cast<size_t>(new_c)];
      if (mesh.cell_types[static_cast<size_t>(new_c)] == static_cast<int32_t>(CellType::HEXAHEDRON)) {
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
        if (distinct) {
          cell_nodes_temp[static_cast<size_t>(new_c)].assign(hc.begin(), hc.end());
        } else {
          mesh.cell_types[static_cast<size_t>(new_c)] = static_cast<int32_t>(CellType::POLYHEDRON);
          auto& cv = cell_nodes_temp[static_cast<size_t>(new_c)];
          cv.insert(cv.end(), st.cell_verts[static_cast<size_t>(old_c)].begin(), st.cell_verts[static_cast<size_t>(old_c)].end());
          std::sort(cv.begin(), cv.end());
          cv.erase(std::unique(cv.begin(), cv.end()), cv.end());
        }
      } else {
        auto& cv = cell_nodes_temp[static_cast<size_t>(new_c)];
        cv.insert(cv.end(), st.cell_verts[static_cast<size_t>(old_c)].begin(), st.cell_verts[static_cast<size_t>(old_c)].end());
        std::sort(cv.begin(), cv.end());
        cv.erase(std::unique(cv.begin(), cv.end()), cv.end());
      }
    } catch (...) {
      omp_failed.store(true, std::memory_order_relaxed);
    }
  }
  if (omp_failed.load(std::memory_order_relaxed)) {
    return -12;
  }

  mesh.cell_offsets.resize(static_cast<size_t>(final_n_cells + 1), 0);
  for (int64_t c = 0; c < final_n_cells; ++c) {
    mesh.cell_offsets[static_cast<size_t>(c + 1)] =
        mesh.cell_offsets[static_cast<size_t>(c)] + static_cast<int64_t>(cell_nodes_temp[static_cast<size_t>(c)].size());
  }
  mesh.cell_nodes.resize(static_cast<size_t>(mesh.cell_offsets[static_cast<size_t>(final_n_cells)]));

#ifdef _OPENMP
#pragma omp parallel for schedule(static)
#endif
  for (int64_t c = 0; c < final_n_cells; ++c) {
    int64_t off = mesh.cell_offsets[static_cast<size_t>(c)];
    const auto& cn = cell_nodes_temp[static_cast<size_t>(c)];
    for (size_t k = 0; k < cn.size(); ++k) {
      mesh.cell_nodes[static_cast<size_t>(off) + k] = cn[k];
    }
  }

  // Edge-based aspect ratio from true polygon face edges (AUD-029) & WSRD alpha
  std::vector<double> cell_min_edge(static_cast<size_t>(final_n_cells), 1e30);
  std::vector<double> cell_max_edge(static_cast<size_t>(final_n_cells), 0.0);
  for (int64_t f = 0; f < total_faces; ++f) {
    const auto& fnodes = st.all_faces[static_cast<size_t>(f)].nodes;
    const size_t nv = fnodes.size();
    if (nv < 2) continue;
    int64_t o = mesh.owner[static_cast<size_t>(f)];
    int64_t nb = mesh.neighbour[static_cast<size_t>(f)];
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
    double v_bg = st.nodes[static_cast<size_t>(st.active_leaves[static_cast<size_t>(old_c)])].v_bg();
    double alpha = std::clamp(st.cell_vols[static_cast<size_t>(old_c)] / (v_bg + 1e-30), 0.0, 1.0);
    mesh.wsrd_alpha[static_cast<size_t>(new_c)] = alpha;
  }

  const size_t n_bnd_total = static_cast<size_t>(total_faces - n_internal);
  if (n_bnd_total > 0) {
    std::string current_patch = st.all_faces[static_cast<size_t>(n_internal)].patch_name;
    if (current_patch.empty()) current_patch = "wall";
    int64_t start_f = n_internal;
    int64_t count = 0;

    for (size_t i = 0; i < n_bnd_total; ++i) {
      std::string p_name = st.all_faces[static_cast<size_t>(n_internal) + i].patch_name;
      if (p_name.empty()) p_name = "wall";
      if (p_name == current_patch) {
        count++;
      } else {
        BoundaryPatchC bp{};
        snprintf(bp.name, sizeof(bp.name), "%s", current_patch.c_str());
        snprintf(bp.patch_type, sizeof(bp.patch_type), "%s", current_patch.c_str());
        bp.start_face = start_f;
        bp.num_faces = count;
        bp.region_id = 0;
        mesh.patches.push_back(bp);

        current_patch = p_name;
        start_f = n_internal + static_cast<int64_t>(i);
        count = 1;
      }
    }

    if (count > 0) {
      BoundaryPatchC bp{};
      snprintf(bp.name, sizeof(bp.name), "%s", current_patch.c_str());
      snprintf(bp.patch_type, sizeof(bp.patch_type), "%s", current_patch.c_str());
      bp.start_face = start_f;
      bp.num_faces = count;
      bp.region_id = 0;
      mesh.patches.push_back(bp);
    }
  }

  mesh.topological_dim = 3;
  return 0;
}

}  // namespace cfd::hexcore
