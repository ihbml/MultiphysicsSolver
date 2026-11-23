#pragma once

#include "linear.hpp"
#include <array>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <vector>

namespace continuum {

// Plain spans are borrowed views over the caller's SoA allocation. No field is copied.
template <class T> struct CAbiSpan {
  T* data = nullptr;
  size_t size = 0;
  T& operator[](size_t i) const { return data[i]; }
};
template <class T> CAbiSpan<T> view(std::vector<T>& a) { return {a.data(), a.size()}; }
template <class T> CAbiSpan<const T> view(const std::vector<T>& a) { return {a.data(), a.size()}; }
template <class T> CAbiSpan<const T> readonly(CAbiSpan<T> a) { return {a.data, a.size}; }

template <class T> struct Vec3 {
  T x{}, y{}, z{};
  Vec3 operator+(const Vec3& b) const { return {x + b.x, y + b.y, z + b.z}; }
  Vec3 operator-(const Vec3& b) const { return {x - b.x, y - b.y, z - b.z}; }
  Vec3 operator*(T b) const { return {x * b, y * b, z * b}; }
  Vec3 operator/(T b) const { return {x / b, y / b, z / b}; }
};
template <class T> T dot(Vec3<T> a, Vec3<T> b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
template <class T> T norm2(Vec3<T> a) { return dot(a, a); }

enum class Boundary : uint8_t { internal = 0, flux = 1, pressure = 2 };
enum class Segment : int { inner_fast = 0, inner_corrected = 1, halo_fast = 2, halo_corrected = 3 };

struct CorrectionSoA {
  CAbiSpan<double> sx, sy, sz; // x_face - (x_owner + beta * d)
  CAbiSpan<double> kx, ky, kz; // S - alpha * d
  CAbiSpan<double> omega;      // bounded deferred-correction blending [0, 1]
};
struct CellFaceCSR {
  CAbiSpan<const int> offsets, faces;
  CAbiSpan<const int8_t> signs; // +1 at owner, -1 at owned neighbor
};

struct MeshView {
  int owned_cells = 0, total_cells = 0, face_count = 0;
  std::array<int, 5> split{}; // [0, end seg0, end seg1, end seg2, end seg3]
  CAbiSpan<const int> owner, neighbor; // neighbor == -1 at a physical boundary
  CAbiSpan<const double> cx, cy, cz, volume;
  CAbiSpan<const double> fx, fy, fz, sx, sy, sz;
  CAbiSpan<const uint8_t> boundary;
  CAbiSpan<const double> pressure_bc, mass_bc, enthalpy_bc;
  CellFaceCSR cell_faces;
  CorrectionSoA correction;

  Vec3<double> cell(int i) const { return {cx[i], cy[i], cz[i]}; }
  Vec3<double> center(int f) const { return {fx[f], fy[f], fz[f]}; }
  Vec3<double> area(int f) const { return {sx[f], sy[f], sz[f]}; }
  bool corrected(int f) const {
    return (f >= split[1] && f < split[2]) || (f >= split[3] && f < split[4]);
  }
  int correction_index(int f) const {
    if (f >= split[1] && f < split[2]) return f - split[1];
    if (f >= split[3] && f < split[4]) return split[2] - split[1] + f - split[3];
    throw std::invalid_argument("fast face has no CorrectionSoA entry");
  }
};

inline void validate(const MeshView& m) {
  if (m.owned_cells < 1 || m.total_cells < m.owned_cells || m.face_count < 1 || m.split[0] != 0 || m.split[4] != m.face_count)
    throw std::invalid_argument("invalid mesh extents");
  for (int i = 0; i < 4; ++i)
    if (m.split[i] > m.split[i + 1]) throw std::invalid_argument("unordered face quadrants");
  if (m.owner.size != static_cast<size_t>(m.face_count) || m.neighbor.size != m.owner.size ||
      m.cell_faces.offsets.size != static_cast<size_t>(m.owned_cells + 1) ||
      m.cell_faces.faces.size != m.cell_faces.signs.size ||
      m.cell_faces.offsets[m.owned_cells] != static_cast<int>(m.cell_faces.faces.size))
    throw std::invalid_argument("invalid face/CSR spans");
  const size_t nf = static_cast<size_t>(m.face_count), cells = static_cast<size_t>(m.total_cells);
  if (m.cx.size < cells || m.cy.size < cells || m.cz.size < cells || m.volume.size < cells ||
      m.fx.size < nf || m.fy.size < nf || m.fz.size < nf ||
      m.sx.size < nf || m.sy.size < nf || m.sz.size < nf ||
      m.boundary.size < nf || m.pressure_bc.size < nf || m.mass_bc.size < nf || m.enthalpy_bc.size < nf ||
      m.cell_faces.offsets[0] != 0)
    throw std::invalid_argument("incomplete mesh SoA spans");
  const size_t nc = static_cast<size_t>(m.split[2] - m.split[1] + m.split[4] - m.split[3]);
  if (m.correction.sx.size != nc || m.correction.sy.size != nc || m.correction.sz.size != nc ||
      m.correction.kx.size != nc || m.correction.ky.size != nc || m.correction.kz.size != nc ||
      m.correction.omega.size != nc) throw std::invalid_argument("CorrectionSoA must be compact");
  std::vector<int> owner_entries(m.face_count, 0), neighbor_entries(m.face_count, 0);
  for (int f = 0; f < m.face_count; ++f) {
    if (m.owner[f] < 0 || m.owner[f] >= m.owned_cells || m.neighbor[f] >= m.total_cells ||
        (f < m.split[2] && (m.neighbor[f] < 0 || m.neighbor[f] >= m.owned_cells)))
      throw std::invalid_argument("face quadrant / owner contract violated");
    if ((m.neighbor[f] < 0 && m.boundary[f] == static_cast<uint8_t>(Boundary::internal)) ||
        (m.neighbor[f] >= 0 && m.boundary[f] != static_cast<uint8_t>(Boundary::internal)))
      throw std::invalid_argument("boundary flag / neighbor mismatch");
  }
  for (int i = 0; i < m.owned_cells; ++i) {
    if (!(m.volume[i] > 0) || m.cell_faces.offsets[i] > m.cell_faces.offsets[i + 1])
      throw std::invalid_argument("invalid volume or CSR row");
    for (int k = m.cell_faces.offsets[i]; k < m.cell_faces.offsets[i + 1]; ++k) {
      int f = m.cell_faces.faces[k], s = m.cell_faces.signs[k];
      if (f < 0 || f >= m.face_count || !((s == 1 && m.owner[f] == i) || (s == -1 && m.neighbor[f] == i)))
        throw std::invalid_argument("CellFaceCSR incidence mismatch");
      if (s == 1) ++owner_entries[f]; else ++neighbor_entries[f];
    }
  }
  for (int f = 0; f < m.face_count; ++f)
    if (owner_entries[f] != 1 || neighbor_entries[f] != (m.neighbor[f] >= 0 && m.neighbor[f] < m.owned_cells ? 1 : 0))
      throw std::invalid_argument("every owned face incidence must appear exactly once");
}

struct Geometry {
  Vec3<double> d, s;
  double beta, alpha;
};

inline Geometry geometry(const MeshView& m, int f) {
  const int p = m.owner[f], n = m.neighbor[f];
  const Vec3<double> d = (n >= 0 ? m.cell(n) : m.center(f)) - m.cell(p);
  const auto s = m.area(f);
  const double ds = dot(d, s);
  if (!(ds > 0)) throw std::domain_error("d dot S must be positive (owner orientation)");
  return {d, s, n >= 0 ? dot(m.center(f) - m.cell(p), s) / ds : 1.0, norm2(s) / ds};
}

inline void populate_corrections(const MeshView& m, double omega = 0.8) {
  for (int f = 0; f < m.face_count; ++f) if (m.corrected(f)) {
    const int c = m.correction_index(f);
    const auto geo = geometry(m, f);
    const auto skew = m.center(f) - (m.cell(m.owner[f]) + geo.d * geo.beta);
    const auto nonorth = geo.s - geo.d * geo.alpha;
    m.correction.sx[c] = skew.x; m.correction.sy[c] = skew.y; m.correction.sz[c] = skew.z;
    m.correction.kx[c] = nonorth.x; m.correction.ky[c] = nonorth.y; m.correction.kz[c] = nonorth.z;
    m.correction.omega[c] = omega;
  }
}

struct FlowView {
  CAbiSpan<double> ux, uy, uz, pressure, enthalpy, d, face_mass;
  CAbiSpan<const double> rho, cp, gamma, force_x, force_y, force_z;
  CAbiSpan<const double> viscosity, conductivity; // per-face dynamic mu and k / cp
  CAbiSpan<const double> halo_gradx, halo_grady, halo_gradz; // optional ghost pressure gradients
};

inline void validate(const MeshView& m, const FlowView& f) {
  const size_t cells = static_cast<size_t>(m.total_cells), faces = static_cast<size_t>(m.face_count);
  if (f.ux.size < cells || f.uy.size < cells || f.uz.size < cells || f.pressure.size < cells ||
      f.enthalpy.size < cells || f.d.size < cells || f.rho.size < cells || f.cp.size < cells ||
      f.gamma.size < cells || f.force_x.size < cells || f.force_y.size < cells || f.force_z.size < cells ||
      f.face_mass.size < faces || f.viscosity.size < faces || f.conductivity.size < faces)
    throw std::invalid_argument("incomplete flow SoA spans");
  for (int i = 0; i < m.total_cells; ++i)
    if (!(f.rho[i] > 0) || !(f.cp[i] > 0) || f.gamma[i] < 0 || f.gamma[i] > 1)
      throw std::invalid_argument("invalid density, heat capacity or SIMP gamma");
  for (int j = 0; j < m.face_count; ++j)
    if (f.viscosity[j] < 0 || f.conductivity[j] < 0)
      throw std::invalid_argument("negative diffusion coefficient");
}

inline double brinkman(double gamma, double min_drag = 0.001, double max_drag = 100.0, int simp = 3) {
  if (gamma < 0 || gamma > 1) throw std::domain_error("gamma must be in [0,1]");
  return min_drag + (max_drag - min_drag) * std::pow(1.0 - gamma, simp);
}

// Rank-deficient stencils (e.g. a 1D line) retain the resolved gradient components.
inline Vec3<double> solve_normal_equations(double a[3][3], double b[3]) {
  double trace = a[0][0] + a[1][1] + a[2][2];
  for (int i = 0; i < 3; ++i) a[i][i] += 1e-15 * std::max(trace, 1.0);
  for (int k = 0; k < 3; ++k) {
    int pivot = k;
    for (int i = k + 1; i < 3; ++i) if (std::abs(a[i][k]) > std::abs(a[pivot][k])) pivot = i;
    for (int j = k; j < 3; ++j) std::swap(a[k][j], a[pivot][j]);
    std::swap(b[k], b[pivot]);
    double div = a[k][k];
    for (int i = k + 1; i < 3; ++i) {
      double q = a[i][k] / div;
      for (int j = k + 1; j < 3; ++j) a[i][j] -= q * a[k][j];
      b[i] -= q * b[k];
    }
  }
  double x[3]{};
  for (int i = 2; i >= 0; --i) {
    double q = b[i];
    for (int j = i + 1; j < 3; ++j) q -= a[i][j] * x[j];
    x[i] = q / a[i][i];
  }
  return {x[0], x[1], x[2]};
}

inline std::vector<Vec3<double>> gradients(const MeshView& m, CAbiSpan<const double> field,
                                           bool use_boundary_values = true, bool zero_boundary = false) {
  std::vector<Vec3<double>> result(m.total_cells);
  #pragma omp parallel for if(m.owned_cells > 2048)
  for (int i = 0; i < m.owned_cells; ++i) {
    double a[3][3]{}, b[3]{};
    for (int k = m.cell_faces.offsets[i]; k < m.cell_faces.offsets[i + 1]; ++k) {
      int f = m.cell_faces.faces[k];
      int j = m.owner[f] == i ? m.neighbor[f] : m.owner[f];
      Vec3<double> delta;
      double dv;
      if (j >= 0) { delta = m.cell(j) - m.cell(i); dv = field[j] - field[i]; }
      else if (use_boundary_values && m.boundary[f] == static_cast<uint8_t>(Boundary::pressure)) {
        delta = m.center(f) - m.cell(i); dv = (zero_boundary ? 0.0 : m.pressure_bc[f]) - field[i];
      } else continue;
      const double d[3]{delta.x, delta.y, delta.z};
      const double w = 1.0 / std::max(norm2(delta), 1e-25);
      for (int q = 0; q < 3; ++q) {
        b[q] += w * d[q] * dv;
        for (int l = 0; l < 3; ++l) a[q][l] += w * d[q] * d[l];
      }
    }
    result[i] = solve_normal_equations(a, b);
  }
  return result;
}

inline std::vector<Vec3<double>> pressure_gradients(const MeshView& m, const FlowView& flow) {
  auto result = gradients(m, readonly(flow.pressure));
  if (flow.halo_gradx.size >= static_cast<size_t>(m.total_cells) &&
      flow.halo_grady.size >= static_cast<size_t>(m.total_cells) &&
      flow.halo_gradz.size >= static_cast<size_t>(m.total_cells)) {
    for (int i = m.owned_cells; i < m.total_cells; ++i)
      result[i] = {flow.halo_gradx[i], flow.halo_grady[i], flow.halo_gradz[i]};
  } else {
    // The caller can provide exchanged ghost gradients; this fallback is
    // linear-exact for a locally constant pressure gradient at the interface.
    std::vector<uint8_t> assigned(m.total_cells, 0);
    for (int f = m.split[2]; f < m.face_count; ++f) {
      int n = m.neighbor[f];
      if (n >= m.owned_cells && !assigned[n]) { result[n] = result[m.owner[f]]; assigned[n] = 1; }
    }
  }
  return result;
}

inline double face_conductance(const MeshView& m, const FlowView& flow, int f) {
  const auto geo = geometry(m, f);
  const int p = m.owner[f], n = m.neighbor[f];
  const double beta = n >= 0 ? geo.beta : 0.0;
  const double rho = flow.rho[p] + (n >= 0 ? beta * (flow.rho[n] - flow.rho[p]) : 0.0);
  const double d = flow.d[p] + (n >= 0 ? beta * (flow.d[n] - flow.d[p]) : 0.0);
  return rho * d * geo.alpha;
}

inline void rhie_chow_faces(const MeshView& m, const FlowView& flow,
                            const std::vector<Vec3<double>>& gradp) {
  #pragma omp parallel for if(m.face_count > 4096)
  for (int f = 0; f < m.face_count; ++f) {
    if (m.boundary[f] == static_cast<uint8_t>(Boundary::flux)) {
      flow.face_mass[f] = m.mass_bc[f]; continue;
    }
    const int p = m.owner[f], n = m.neighbor[f];
    const auto geo = geometry(m, f);
    const double beta = n >= 0 ? geo.beta : 0.0;
    const double rho = flow.rho[p] + (n >= 0 ? beta * (flow.rho[n] - flow.rho[p]) : 0.0);
    const double d = flow.d[p] + (n >= 0 ? beta * (flow.d[n] - flow.d[p]) : 0.0);
    Vec3<double> u{flow.ux[p], flow.uy[p], flow.uz[p]};
    Vec3<double> gp = gradp[p];
    double dp = m.pressure_bc[f] - flow.pressure[p];
    if (n >= 0) {
      u = u + (Vec3<double>{flow.ux[n], flow.uy[n], flow.uz[n]} - u) * beta;
      gp = gp + (gradp[n] - gp) * beta;
      dp = flow.pressure[n] - flow.pressure[p];
    }
    // For p(x)=a+b.x the bracket vanishes on skew and nonorthogonal faces.
    flow.face_mass[f] = rho * (dot(u, geo.s) - d * geo.alpha * (dp - dot(gp, geo.d)));
  }
}

inline std::vector<double> gather_divergence(const MeshView& m, CAbiSpan<const double> face_flux) {
  std::vector<double> out(m.owned_cells, 0);
  #pragma omp parallel for if(m.owned_cells > 2048)
  for (int i = 0; i < m.owned_cells; ++i)
    for (int k = m.cell_faces.offsets[i]; k < m.cell_faces.offsets[i + 1]; ++k)
      out[i] += static_cast<double>(m.cell_faces.signs[k]) * face_flux[m.cell_faces.faces[k]];
  return out;
}

inline linear::CSR<double> pressure_matrix(const MeshView& m, const FlowView& flow, bool& anchored) {
  anchored = true;
  for (int f = 0; f < m.face_count; ++f)
    if (m.boundary[f] == static_cast<uint8_t>(Boundary::pressure) || m.neighbor[f] >= m.owned_cells) anchored = false;
  std::vector<std::map<int, double>> rows(m.owned_cells);
  for (int i = 0; i < m.owned_cells; ++i) {
    if (anchored && i == 0) { rows[i][i] = 1.0; continue; }
    for (int k = m.cell_faces.offsets[i]; k < m.cell_faces.offsets[i + 1]; ++k) {
      const int f = m.cell_faces.faces[k];
      if (m.boundary[f] == static_cast<uint8_t>(Boundary::flux)) continue;
      const int other = m.owner[f] == i ? m.neighbor[f] : m.owner[f];
      const double g = face_conductance(m, flow, f);
      rows[i][i] += g;
      if (other >= 0 && other < m.owned_cells && !(anchored && other == 0)) rows[i][other] -= g;
    }
  }
  return linear::from_rows(rows);
}

inline std::vector<double> pressure_apply_matrix_free(const MeshView& m, const FlowView& flow,
                                                      CAbiSpan<const double> x, bool anchored) {
  std::vector<double> ax(m.owned_cells, 0);
  #pragma omp parallel for if(m.owned_cells > 2048)
  for (int i = 0; i < m.owned_cells; ++i) {
    if (anchored && i == 0) { ax[i] = x[i]; continue; }
    for (int k = m.cell_faces.offsets[i]; k < m.cell_faces.offsets[i + 1]; ++k) {
      int f = m.cell_faces.faces[k];
      if (m.boundary[f] == static_cast<uint8_t>(Boundary::flux)) continue;
      int other = m.owner[f] == i ? m.neighbor[f] : m.owner[f];
      double neighbor_value = other >= 0 && other < m.owned_cells && !(anchored && other == 0) ? x[other] : 0.0;
      ax[i] += face_conductance(m, flow, f) * (x[i] - neighbor_value);
    }
  }
  return ax;
}

struct PressureReport { double max_divergence = 0; linear::SolveReport solve; };

inline PressureReport correct_pressure(const MeshView& m, FlowView flow, double pressure_relax = 1.0,
                                       int nonorth_sweeps = 2) {
  bool anchored = false;
  const auto a = pressure_matrix(m, flow, anchored);
  const linear::AMG amg(a);
  const auto div = gather_divergence(m, readonly(flow.face_mass));
  std::vector<double> pc(m.total_cells, 0.0), rhs(m.owned_cells);
  PressureReport report;
  for (int pass = 0; pass < std::max(1, nonorth_sweeps); ++pass) {
    for (int i = 0; i < m.owned_cells; ++i) rhs[i] = -div[i];
    if (pass > 0) {
      auto grad = gradients(m, readonly(view(pc)), true, true);
      std::vector<double> explicit_flux(m.face_count, 0.0);
      for (int f = 0; f < m.face_count; ++f) if (m.corrected(f) && m.boundary[f] != static_cast<uint8_t>(Boundary::flux)) {
        int p = m.owner[f], n = m.neighbor[f], c = m.correction_index(f);
        double beta = geometry(m, f).beta;
        auto gp = grad[p];
        if (n >= 0 && n < m.owned_cells) gp = gp + (grad[n] - gp) * beta;
        Vec3<double> k{m.correction.kx[c], m.correction.ky[c], m.correction.kz[c]};
        explicit_flux[f] = face_conductance(m, flow, f) / geometry(m, f).alpha * dot(k, gp);
      }
      auto src = gather_divergence(m, readonly(view(explicit_flux)));
      for (int i = 0; i < m.owned_cells; ++i) rhs[i] += src[i];
    }
    if (anchored) rhs[0] = 0;
    report.solve = linear::iterative_refinement(a, rhs, amg, 1e-12, 60);
    if (!report.solve.converged) report.solve = linear::mixed_fgmres(a, rhs, amg, 1e-12, 120);
    for (int i = 0; i < m.owned_cells; ++i) pc[i] = report.solve.x[i];
  }
  auto gradpc = gradients(m, readonly(view(pc)), true, true);
  for (int f = 0; f < m.face_count; ++f) {
    if (m.boundary[f] == static_cast<uint8_t>(Boundary::flux)) continue;
    const int p = m.owner[f], n = m.neighbor[f];
    double correction = face_conductance(m, flow, f) * (pc[p] - (n >= 0 ? pc[n] : 0.0));
    if (m.corrected(f)) {
      const int c = m.correction_index(f);
      auto gp = gradpc[p];
      if (n >= 0 && n < m.owned_cells) gp = gp + (gradpc[n] - gp) * geometry(m, f).beta;
      Vec3<double> k{m.correction.kx[c], m.correction.ky[c], m.correction.kz[c]};
      correction -= face_conductance(m, flow, f) / geometry(m, f).alpha * dot(k, gp);
    }
    flow.face_mass[f] += correction;
  }
  for (int i = 0; i < m.owned_cells; ++i) {
    flow.pressure[i] += pressure_relax * pc[i];
    flow.ux[i] -= flow.d[i] * gradpc[i].x;
    flow.uy[i] -= flow.d[i] * gradpc[i].y;
    flow.uz[i] -= flow.d[i] * gradpc[i].z;
  }
  for (double residual : gather_divergence(m, readonly(flow.face_mass)))
    report.max_divergence = std::max(report.max_divergence, std::abs(residual));
  return report;
}

inline void momentum_predictor(const MeshView& m, FlowView flow, double dt, double relax = 1.0,
                               int sweeps = 4) {
  auto gradp = gradients(m, readonly(flow.pressure));
  std::vector<double> oldx(flow.ux.data, flow.ux.data + m.total_cells);
  std::vector<double> oldy(flow.uy.data, flow.uy.data + m.total_cells);
  std::vector<double> oldz(flow.uz.data, flow.uz.data + m.total_cells);
  const auto div = gather_divergence(m, readonly(flow.face_mass));
  std::vector<double> ax = oldx, ay = oldy, az = oldz;
  for (int step = 0; step < sweeps; ++step) {
    std::vector<double> bx = ax, by = ay, bz = az;
    #pragma omp parallel for if(m.owned_cells > 2048)
    for (int i = 0; i < m.owned_cells; ++i) {
      double mass = flow.rho[i] * m.volume[i] / dt;
      double diag = mass + brinkman(flow.gamma[i]) * m.volume[i] + std::max(-div[i], 0.0);
      Vec3<double> rhs{mass * oldx[i] + m.volume[i] * (flow.force_x[i] - gradp[i].x),
                       mass * oldy[i] + m.volume[i] * (flow.force_y[i] - gradp[i].y),
                       mass * oldz[i] + m.volume[i] * (flow.force_z[i] - gradp[i].z)};
      for (int k = m.cell_faces.offsets[i]; k < m.cell_faces.offsets[i + 1]; ++k) {
        int f = m.cell_faces.faces[k];
        int other = m.owner[f] == i ? m.neighbor[f] : m.owner[f];
        double mf = m.cell_faces.signs[k] * flow.face_mass[f];
        double diffusion = flow.viscosity[f] * geometry(m, f).alpha;
        diag += diffusion + std::max(mf, 0.0);
        if (other >= 0) {
          double off = diffusion + std::max(-mf, 0.0);
          rhs = rhs + Vec3<double>{ax[other], ay[other], az[other]} * off;
        }
      }
      flow.d[i] = m.volume[i] / diag;
      bx[i] = (1 - relax) * ax[i] + relax * rhs.x / diag;
      by[i] = (1 - relax) * ay[i] + relax * rhs.y / diag;
      bz[i] = (1 - relax) * az[i] + relax * rhs.z / diag;
    }
    ax.swap(bx); ay.swap(by); az.swap(bz);
  }
  for (int i = 0; i < m.owned_cells; ++i) {
    flow.ux[i] = ax[i]; flow.uy[i] = ay[i]; flow.uz[i] = az[i];
  }
}

inline void enthalpy_predictor(const MeshView& m, FlowView flow, double dt, int sweeps = 12) {
  std::vector<double> old(flow.enthalpy.data, flow.enthalpy.data + m.total_cells);
  std::vector<double> h = old;
  auto grad_h = gradients(m, readonly(flow.enthalpy), false);
  std::vector<double> deferred(m.face_count, 0.0);
  for (int f = 0; f < m.face_count; ++f) {
    const int p = m.owner[f], n = m.neighbor[f];
    if (n < 0) continue;
    const double beta = geometry(m, f).beta;
    const double hf = old[p] + beta * (old[n] - old[p]);
    const double hu = flow.face_mass[f] >= 0 ? old[p] : old[n];
    // Class 0: gradient-free deferred upwind; corrected classes alone touch compact SoA.
    double high = hf, omega = 0.6;
    if (m.corrected(f)) {
      int c = m.correction_index(f);
      auto g = n < m.owned_cells ? grad_h[p] + (grad_h[n] - grad_h[p]) * beta : grad_h[p];
      high += dot(g, Vec3<double>{m.correction.sx[c], m.correction.sy[c], m.correction.sz[c]});
      omega = m.correction.omega[c];
      Vec3<double> kvec{m.correction.kx[c], m.correction.ky[c], m.correction.kz[c]};
      deferred[f] -= flow.conductivity[f] * dot(kvec, g);
    }
    deferred[f] += flow.face_mass[f] * omega * (high - hu);
  }
  const auto defer_source = gather_divergence(m, readonly(view(deferred)));
  for (int sweep = 0; sweep < sweeps; ++sweep) {
    std::vector<double> next = h;
    #pragma omp parallel for if(m.owned_cells > 2048)
    for (int i = 0; i < m.owned_cells; ++i) {
      const double storage = flow.rho[i] * m.volume[i] / dt;
      double diag = storage, rhs = storage * old[i] - defer_source[i];
      for (int k = m.cell_faces.offsets[i]; k < m.cell_faces.offsets[i + 1]; ++k) {
        int f = m.cell_faces.faces[k], other = m.owner[f] == i ? m.neighbor[f] : m.owner[f];
        double mf = m.cell_faces.signs[k] * flow.face_mass[f];
        double diffusion = flow.conductivity[f] * geometry(m, f).alpha;
        diag += diffusion + std::max(mf, 0.0);
        if (other >= 0) rhs += (diffusion + std::max(-mf, 0.0)) * h[other];
        else rhs += (diffusion + std::max(-mf, 0.0)) * m.enthalpy_bc[f];
      }
      next[i] = rhs / diag;
    }
    h.swap(next);
  }
  for (int i = 0; i < m.owned_cells; ++i) flow.enthalpy[i] = h[i];
}

enum class Coupling { simple, piso, pimple };
struct FlowOptions {
  Coupling method = Coupling::piso;
  double dt = 0.01;
  int outer = 2;
  int nonorth_sweeps = 2;
};
struct FlowReport { std::vector<double> continuity; int pressure_solves = 0; };

inline FlowReport advance(const MeshView& m, FlowView flow, FlowOptions config = {}) {
  validate(m);
  validate(m, flow);
  if (!(config.dt > 0)) throw std::invalid_argument("dt must be positive");
  FlowReport result;
  int outer = config.method == Coupling::pimple ? std::max(1, config.outer) : 1;
  int corrections = config.method == Coupling::simple ? 1 : 2;
  for (int iteration = 0; iteration < outer; ++iteration) {
    momentum_predictor(m, flow, config.dt, config.method == Coupling::simple ? 0.7 : 1.0);
    rhie_chow_faces(m, flow, pressure_gradients(m, flow));
    for (int pass = 0; pass < corrections; ++pass) {
      // A PISO repeat reconstructs from the newly corrected cell state before
      // the next projection, rather than solving an already-zero right side.
      if (pass > 0) rhie_chow_faces(m, flow, pressure_gradients(m, flow));
      auto report = correct_pressure(m, flow, config.method == Coupling::simple ? 0.3 : 1.0,
                                     config.nonorth_sweeps);
      result.continuity.push_back(report.max_divergence);
      ++result.pressure_solves;
    }
    enthalpy_predictor(m, flow, config.dt);
  }
  return result;
}

} // namespace continuum