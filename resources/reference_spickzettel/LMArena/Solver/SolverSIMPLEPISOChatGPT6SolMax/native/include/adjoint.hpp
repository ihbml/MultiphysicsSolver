#pragma once

#include "continuum.hpp"

namespace continuum::adjoint {

// Forward duals make the discrete residual/geometry derivatives exact to FP64 rounding.
struct Dual {
  double v = 0, d = 0;
  Dual() = default;
  Dual(double value, double tangent = 0) : v(value), d(tangent) {}
};
inline Dual operator+(Dual a, Dual b) { return {a.v + b.v, a.d + b.d}; }
inline Dual operator-(Dual a, Dual b) { return {a.v - b.v, a.d - b.d}; }
inline Dual operator-(Dual a) { return {-a.v, -a.d}; }
inline Dual operator*(Dual a, Dual b) { return {a.v * b.v, a.d * b.v + a.v * b.d}; }
inline Dual operator/(Dual a, Dual b) { return {a.v / b.v, (a.d * b.v - a.v * b.d) / (b.v * b.v)}; }
inline double value(double x) { return x; }
inline double value(Dual x) { return x.v; }
inline double tangent(double) { return 0; }
inline double tangent(Dual x) { return x.d; }

template <class T> struct Design {
  std::vector<T> gamma;
  std::vector<Vec3<T>> center, area;
};

template <class T> Design<T> cast_design(const MeshView& m) {
  Design<T> q;
  q.gamma.assign(m.owned_cells, T(1.0));
  q.center.resize(m.face_count);
  q.area.resize(m.face_count);
  for (int f = 0; f < m.face_count; ++f) {
    auto x = m.center(f), s = m.area(f);
    q.center[f] = {T(x.x), T(x.y), T(x.z)};
    q.area[f] = {T(s.x), T(s.y), T(s.z)};
  }
  return q;
}

template <class T> Vec3<T> position(const MeshView& m, int i) {
  return {T(m.cx[i]), T(m.cy[i]), T(m.cz[i])};
}

template <class T> struct FaceGeometry { Vec3<T> d, s; T beta, alpha; };
template <class T> FaceGeometry<T> face_geometry(const MeshView& m, const Design<T>& q, int f) {
  const int p = m.owner[f], n = m.neighbor[f];
  auto d = (n >= 0 ? position<T>(m, n) : q.center[f]) - position<T>(m, p);
  auto s = q.area[f];
  T ds = dot(d, s);
  if (!(value(ds) > 0)) throw std::domain_error("adjoint: nonpositive d dot S");
  return {d, s, n >= 0 ? dot(q.center[f] - position<T>(m, p), s) / ds : T(1), norm2(s) / ds};
}

template <class T> T simp_drag(T gamma) {
  T solid = T(1) - gamma;
  return T(0.001) + T(99.999) * solid * solid * solid;
}

template <class T> Vec3<T> solve3(T a[3][3], T b[3]) {
  T tr = a[0][0] + a[1][1] + a[2][2];
  for (int i = 0; i < 3; ++i) a[i][i] = a[i][i] + T(1e-14) * tr + T(1e-15);
  for (int k = 0; k < 3; ++k) {
    int pivot = k;
    for (int i = k + 1; i < 3; ++i)
      if (std::abs(value(a[i][k])) > std::abs(value(a[pivot][k]))) pivot = i;
    for (int j = k; j < 3; ++j) std::swap(a[k][j], a[pivot][j]);
    std::swap(b[k], b[pivot]);
    for (int i = k + 1; i < 3; ++i) {
      T factor = a[i][k] / a[k][k];
      for (int j = k + 1; j < 3; ++j) a[i][j] = a[i][j] - factor * a[k][j];
      b[i] = b[i] - factor * b[k];
    }
  }
  T result[3]{};
  for (int i = 2; i >= 0; --i) {
    T v = b[i];
    for (int j = i + 1; j < 3; ++j) v = v - a[i][j] * result[j];
    result[i] = v / a[i][i];
  }
  return {result[0], result[1], result[2]};
}

template <class T> std::vector<Vec3<T>> pressure_gradient(const MeshView& m, const Design<T>& q,
                                                           const std::vector<T>& state) {
  std::vector<Vec3<T>> grad(m.owned_cells);
  for (int i = 0; i < m.owned_cells; ++i) {
    T a[3][3]{}, b[3]{};
    for (int k = m.cell_faces.offsets[i]; k < m.cell_faces.offsets[i + 1]; ++k) {
      int f = m.cell_faces.faces[k];
      int j = m.owner[f] == i ? m.neighbor[f] : m.owner[f];
      Vec3<T> delta;
      T dp;
      if (j >= 0 && j < m.owned_cells) {
        delta = position<T>(m, j) - position<T>(m, i);
        dp = state[4 * j + 3] - state[4 * i + 3];
      } else if (j < 0 && m.boundary[f] == static_cast<uint8_t>(Boundary::pressure)) {
        delta = q.center[f] - position<T>(m, i);
        dp = T(m.pressure_bc[f]) - state[4 * i + 3];
      } else continue;
      T length = T(1) / (norm2(delta) + T(1e-25));
      T d[3]{delta.x, delta.y, delta.z};
      for (int u = 0; u < 3; ++u) {
        b[u] = b[u] + length * d[u] * dp;
        for (int v = 0; v < 3; ++v) a[u][v] = a[u][v] + length * d[u] * d[v];
      }
    }
    grad[i] = solve3(a, b);
  }
  return grad;
}

// Steady discrete Stokes/Brinkman + Rhie-Chow continuity. Each cell owns its
// residual row; face data are read through CellFaceCSR, never scattered.
template <class T> std::vector<T> residual(const MeshView& m, const Design<T>& q,
                                           const std::vector<T>& state, double dt = 0.5,
                                           double mu = 0.15) {
  const int n = m.owned_cells;
  if (static_cast<int>(state.size()) != 4 * n || static_cast<int>(q.gamma.size()) != n)
    throw std::invalid_argument("coupled residual state size");
  auto grad = pressure_gradient(m, q, state);
  std::vector<T> diag(n), mobility(n), face_flux(m.face_count, T(0));
  for (int i = 0; i < n; ++i) {
    diag[i] = T(m.volume[i] / dt) + simp_drag(q.gamma[i]) * T(m.volume[i]);
    for (int k = m.cell_faces.offsets[i]; k < m.cell_faces.offsets[i + 1]; ++k) {
      int f = m.cell_faces.faces[k];
      if (m.neighbor[f] >= 0 || m.boundary[f] == static_cast<uint8_t>(Boundary::flux))
        diag[i] = diag[i] + T(mu) * face_geometry(m, q, f).alpha;
    }
    mobility[i] = T(m.volume[i]) / diag[i];
  }
  for (int f = 0; f < m.face_count; ++f) {
    int p = m.owner[f], nb = m.neighbor[f];
    if (m.boundary[f] == static_cast<uint8_t>(Boundary::flux)) {
      face_flux[f] = T(m.mass_bc[f]); continue;
    }
    auto geo = face_geometry(m, q, f);
    Vec3<T> u{state[4 * p], state[4 * p + 1], state[4 * p + 2]};
    auto gp = grad[p];
    T d = mobility[p], dp = T(m.pressure_bc[f]) - state[4 * p + 3];
    if (nb >= 0 && nb < n) {
      auto un = Vec3<T>{state[4 * nb], state[4 * nb + 1], state[4 * nb + 2]};
      u = u + (un - u) * geo.beta;
      gp = gp + (grad[nb] - gp) * geo.beta;
      d = d + (mobility[nb] - d) * geo.beta;
      dp = state[4 * nb + 3] - state[4 * p + 3];
    }
    face_flux[f] = dot(u, geo.s) - d * geo.alpha * (dp - dot(gp, geo.d));
  }
  std::vector<T> out(4 * n, T(0));
  for (int i = 0; i < n; ++i) {
    const T volume = T(m.volume[i]);
    out[4 * i] = diag[i] * state[4 * i] + volume * (grad[i].x - T(0.28));
    out[4 * i + 1] = diag[i] * state[4 * i + 1] + volume * grad[i].y;
    out[4 * i + 2] = diag[i] * state[4 * i + 2] + volume * grad[i].z;
    for (int k = m.cell_faces.offsets[i]; k < m.cell_faces.offsets[i + 1]; ++k) {
      int f = m.cell_faces.faces[k], other = m.owner[f] == i ? m.neighbor[f] : m.owner[f];
      out[4 * i + 3] = out[4 * i + 3] + T(m.cell_faces.signs[k]) * face_flux[f];
      if (other >= 0 && other < n) {
        T diff = T(mu) * face_geometry(m, q, f).alpha;
        for (int c = 0; c < 3; ++c) out[4 * i + c] = out[4 * i + c] - diff * state[4 * other + c];
      }
    }
  }
  return out;
}

template <class T> T objective(const MeshView& m, const Design<T>& q, const std::vector<T>& u) {
  T result = T(0);
  for (int i = 0; i < m.owned_cells; ++i) {
    T ux = u[4 * i] - T(0.21);
    result = result + T(m.volume[i] * 0.5) * (ux * ux + u[4 * i + 1] * u[4 * i + 1] +
                                             u[4 * i + 2] * u[4 * i + 2]);
    result = result + T(0.004) * u[4 * i + 3] * u[4 * i + 3] + T(0.01) * q.gamma[i] * q.gamma[i];
  }
  for (int f = 0; f < m.face_count; ++f) if (m.neighbor[f] < 0) {
    result = result + T(0.001) * norm2(q.center[f]) + T(0.002) * norm2(q.area[f]);
  }
  return result;
}

template <class T> linear::CSR<double> jacobian(const MeshView& m, const Design<T>& q,
                                                const std::vector<double>& u) {
  const int n = static_cast<int>(u.size());
  Design<Dual> dq;
  dq.gamma.reserve(q.gamma.size());
  for (auto v : q.gamma) dq.gamma.push_back(Dual(value(v)));
  for (auto v : q.center) dq.center.push_back({Dual(value(v.x)), Dual(value(v.y)), Dual(value(v.z))});
  for (auto v : q.area) dq.area.push_back({Dual(value(v.x)), Dual(value(v.y)), Dual(value(v.z))});
  std::vector<std::map<int, double>> rows(n);
  std::vector<Dual> state(n);
  for (int col = 0; col < n; ++col) {
    for (int j = 0; j < n; ++j) state[j] = Dual(u[j], j == col ? 1 : 0);
    auto r = residual(m, dq, state);
    for (int i = 0; i < n; ++i) if (std::abs(r[i].d) > 1e-15) rows[i][col] = r[i].d;
  }
  return linear::from_rows(rows);
}

inline std::vector<double> solve_primal(const MeshView& m, const Design<double>& q) {
  std::vector<double> u(4 * m.owned_cells, 0);
  for (int iteration = 0; iteration < 3; ++iteration) {
    auto r = residual(m, q, u);
    if (linear::norm(r) < 1e-13) return u;
    auto j = jacobian(m, q, u);
    for (auto& v : r) v = -v;
    auto du = linear::dense_solve(j, r);
    for (size_t i = 0; i < u.size(); ++i) u[i] += du[i];
  }
  if (linear::norm(residual(m, q, u)) > 1e-11) throw std::runtime_error("primal Newton solve failed");
  return u;
}

enum class Parameter { gamma, centroid_x, centroid_y, centroid_z, normal_x, normal_y, normal_z };

inline Design<Dual> seed_design(const Design<double>& q, Parameter type, int index) {
  Design<Dual> out;
  for (size_t i = 0; i < q.gamma.size(); ++i) out.gamma.push_back({q.gamma[i], type == Parameter::gamma && static_cast<int>(i) == index ? 1.0 : 0.0});
  for (size_t i = 0; i < q.center.size(); ++i) {
    bool active = static_cast<int>(i) == index;
    out.center.push_back({{q.center[i].x, active && type == Parameter::centroid_x ? 1.0 : 0.0},
                          {q.center[i].y, active && type == Parameter::centroid_y ? 1.0 : 0.0},
                          {q.center[i].z, active && type == Parameter::centroid_z ? 1.0 : 0.0}});
    out.area.push_back({{q.area[i].x, active && type == Parameter::normal_x ? 1.0 : 0.0},
                        {q.area[i].y, active && type == Parameter::normal_y ? 1.0 : 0.0},
                        {q.area[i].z, active && type == Parameter::normal_z ? 1.0 : 0.0}});
  }
  return out;
}

struct Sensitivities {
  double value = 0;
  std::vector<double> state, lambda, gamma;
  std::vector<Vec3<double>> centroid, normal;
};

inline Sensitivities differentiate(const MeshView& m, const Design<double>& q) {
  Sensitivities out;
  out.state = solve_primal(m, q);
  out.value = objective(m, q, out.state);
  auto jac = jacobian(m, q, out.state);
  auto jac_t = linear::transpose(jac, jac.n);
  std::vector<double> ju(out.state.size());
  auto dq = seed_design(q, Parameter::gamma, -1);
  for (size_t c = 0; c < ju.size(); ++c) {
    std::vector<Dual> state;
    for (size_t i = 0; i < ju.size(); ++i) state.push_back({out.state[i], i == c ? 1.0 : 0.0});
    ju[c] = -tangent(objective(m, dq, state));
  }
  out.lambda = linear::dense_solve(jac_t, ju);
  auto param = [&](Parameter type, int index) {
    auto seeded = seed_design(q, type, index);
    std::vector<Dual> state;
    for (double v : out.state) state.push_back({v, 0});
    auto r = residual(m, seeded, state);
    double derivative = tangent(objective(m, seeded, state));
    for (size_t i = 0; i < r.size(); ++i) derivative += out.lambda[i] * tangent(r[i]);
    return derivative;
  };
  for (int i = 0; i < m.owned_cells; ++i) out.gamma.push_back(param(Parameter::gamma, i));
  out.centroid.resize(m.face_count);
  out.normal.resize(m.face_count);
  for (int f = 0; f < m.face_count; ++f) if (m.neighbor[f] < 0) {
    out.centroid[f] = {param(Parameter::centroid_x, f), param(Parameter::centroid_y, f),
                       param(Parameter::centroid_z, f)};
    out.normal[f] = {param(Parameter::normal_x, f), param(Parameter::normal_y, f),
                     param(Parameter::normal_z, f)};
  }
  return out;
}

struct CadDirection {
  std::vector<double> gamma;
  std::vector<Vec3<double>> centroid, normal;
};

inline double cad_chain(const Sensitivities& d, const CadDirection& direction) {
  double result = 0;
  for (size_t i = 0; i < d.gamma.size(); ++i) result += d.gamma[i] * direction.gamma[i];
  for (size_t i = 0; i < d.centroid.size(); ++i) {
    result += dot(d.centroid[i], direction.centroid[i]);
    result += dot(d.normal[i], direction.normal[i]);
  }
  return result;
}

} // namespace continuum::adjoint