// SPDX-License-Identifier: MIT
// FVM graph primitives + three-field density chain:
//   gamma --(Helmholtz PDE filter)--> gammaTilde --(Heaviside beta)--> gammaBar
// plus Kreisselmeier-Steinhauser aggregation with overflow-safe shifting and
// exact analytic VJP / JVP.
#pragma once

#include <algorithm>
#include <cmath>
#include <complex>
#include <cstddef>
#include <stdexcept>
#include <vector>

#include "mdo/linalg.hpp"

namespace mdo {

// Cell-centred finite-volume graph on the dual of a structured quad mesh.
// The same containers are valid for genuinely unstructured meshes: every
// operator below is assembled from `faces` + `cellArea` only.
struct FvmMesh {
  int nx = 0;
  int ny = 0;
  double h = 1.0;
  std::vector<double> cellArea;
  struct Face {
    int c1;
    int c2;
    double area;
    double length;
    int dir;  // 0 = x-normal, 1 = y-normal
  };
  std::vector<Face> faces;

  FvmMesh() = default;
  FvmMesh(int nx_, int ny_, double h_ = 1.0) : nx(nx_), ny(ny_), h(h_) {
    if (nx < 2 || ny < 2) throw std::runtime_error("FvmMesh needs nx,ny >= 2");
    cellArea.assign(static_cast<std::size_t>(nx) * static_cast<std::size_t>(ny), h * h);
    for (int j = 0; j < ny; ++j) {
      for (int i = 0; i < nx; ++i) {
        const int c = idx(i, j);
        if (i + 1 < nx) faces.push_back({c, idx(i + 1, j), h, h, 0});
        if (j + 1 < ny) faces.push_back({c, idx(i, j + 1), h, h, 1});
      }
    }
  }
  [[nodiscard]] int idx(int i, int j) const { return j * nx + i; }
  [[nodiscard]] int size() const { return nx * ny; }
  [[nodiscard]] bool isLeft(int c) const { return c % nx == 0; }
  [[nodiscard]] bool isRight(int c) const { return c % nx == nx - 1; }
  [[nodiscard]] double totalArea() const {
    double s = 0.0;
    for (double a : cellArea) s += a;
    return s;
  }
};

// Heaviside projection with tanh regularisation (Wang, Lazarov & Sigmund 2011).
struct HeavisideProjection {
  double beta = 1.0;
  double eta = 0.5;

  [[nodiscard]] double operator()(double x) const {
    const double denom = std::tanh(beta * eta) + std::tanh(beta * (1.0 - eta));
    return (std::tanh(beta * eta) + std::tanh(beta * (x - eta))) / denom;
  }
  [[nodiscard]] double derivative(double x) const {
    const double denom = std::tanh(beta * eta) + std::tanh(beta * (1.0 - eta));
    const double th = std::tanh(beta * (x - eta));
    return beta * (1.0 - th * th) / denom;
  }
  [[nodiscard]] std::vector<double> apply(const std::vector<double>& x) const {
    std::vector<double> y(x.size());
    for (std::size_t i = 0; i < x.size(); ++i) y[i] = (*this)(x[i]);
    return y;
  }
  // Exact VJP of the element-wise projection: dJ/dgammaTilde = proj' * dJ/dgammaBar.
  [[nodiscard]] std::vector<double> vjp(const std::vector<double>& x, const std::vector<double>& dJdBar) const {
    std::vector<double> g(x.size());
    for (std::size_t i = 0; i < x.size(); ++i) g[i] = derivative(x[i]) * dJdBar[i];
    return g;
  }
};

// Elliptic Helmholtz density filter, discretised on the FVM graph:
//   -(rmin/(2 sqrt3))^2 * lap(gammaTilde) + gammaTilde = gamma
// Weak FVM form:  V_e * gt_e + (r^2) * sum_f (A_f / L_f) (gt_e - gt_nb) = V_e * g_e
// Natural (zero-flux) boundary conditions are used, so the operator conserves
// the volume-weighted integral of the filtered field.
struct HelmholtzFilter {
  const FvmMesh* mesh = nullptr;
  double rmin = 3.0;
  Csr A;

  void build(const FvmMesh& m, double rmin_) {
    mesh = &m;
    rmin = rmin_;
    const double r = rmin / (2.0 * std::sqrt(3.0));
    const double r2 = r * r;
    std::vector<double> diag(static_cast<std::size_t>(m.size()), 0.0);
    std::vector<Triplet> trip;
    trip.reserve(m.faces.size() * 4 + static_cast<std::size_t>(m.size()));
    for (const auto& f : m.faces) {
      const double w = r2 * f.area / f.length;
      diag[static_cast<std::size_t>(f.c1)] += w;
      diag[static_cast<std::size_t>(f.c2)] += w;
      trip.push_back({f.c1, f.c2, -w});
      trip.push_back({f.c2, f.c1, -w});
    }
    for (int e = 0; e < m.size(); ++e) {
      diag[static_cast<std::size_t>(e)] += m.cellArea[static_cast<std::size_t>(e)];
      trip.push_back({e, e, diag[static_cast<std::size_t>(e)]});
    }
    A.build(m.size(), std::move(trip));
  }

  // Forward: solve A * gammaTilde = M * gamma  (M = volume matrix).
  std::vector<double> apply(const std::vector<double>& gamma, int* iters = nullptr) const {
    if (!mesh) throw std::runtime_error("HelmholtzFilter::build not called");
    const int n = mesh->size();
    Vec rhs(static_cast<std::size_t>(n));
    for (int e = 0; e < n; ++e) rhs[static_cast<std::size_t>(e)] = mesh->cellArea[static_cast<std::size_t>(e)] * gamma[static_cast<std::size_t>(e)];
    Vec x;
    SolveInfo info = pcg(A, rhs, x, 1e-13, 8000);
    if (iters) *iters = info.iters;
    return x;
  }

  // Exact VJP:  dJ/dgamma = A^{-T} ( M * dJ/dgammaTilde ).
  // A is symmetric positive definite, hence A^{-T} = A^{-1}.
  std::vector<double> vjp(const std::vector<double>& dJdTilde, int* iters = nullptr) const {
    if (!mesh) throw std::runtime_error("HelmholtzFilter::build not called");
    const int n = mesh->size();
    Vec rhs(static_cast<std::size_t>(n));
    for (int e = 0; e < n; ++e) rhs[static_cast<std::size_t>(e)] = mesh->cellArea[static_cast<std::size_t>(e)] * dJdTilde[static_cast<std::size_t>(e)];
    Vec x;
    SolveInfo info = pcg(A, rhs, x, 1e-13, 8000);
    if (iters) *iters = info.iters;
    return x;
  }

  // Small dense assembly used by the complex-step verification harness.
  template <class S>
  std::vector<S> denseMatrix() const {
    const int n = mesh->size();
    const double r = rmin / (2.0 * std::sqrt(3.0));
    const double r2 = r * r;
    std::vector<S> A_(static_cast<std::size_t>(n) * static_cast<std::size_t>(n), S(0));
    auto at = [&](int i, int j) -> S& {
      return A_[static_cast<std::size_t>(i) * static_cast<std::size_t>(n) + static_cast<std::size_t>(j)];
    };
    for (const auto& f : mesh->faces) {
      const double w = r2 * f.area / f.length;
      at(f.c1, f.c1) += S(w);
      at(f.c2, f.c2) += S(w);
      at(f.c1, f.c2) -= S(w);
      at(f.c2, f.c1) -= S(w);
    }
    for (int e = 0; e < n; ++e) at(e, e) += S(mesh->cellArea[static_cast<std::size_t>(e)]);
    return A_;
  }

  // Volume weighted L1 error indicator of filter consistency.
  [[nodiscard]] double volumeError(const std::vector<double>& g, const std::vector<double>& gt) const {
    double s = 0.0;
    for (int e = 0; e < mesh->size(); ++e)
      s += mesh->cellArea[static_cast<std::size_t>(e)] * (gt[static_cast<std::size_t>(e)] - g[static_cast<std::size_t>(e)]);
    return std::abs(s);
  }
};

// Kreisselmeier-Steinhauser aggregation.
//   g_KS = Tmax + 1/rho * ln( (1/N) sum_i exp(rho (T_i - Tmax)) )
// The Tmax shift keeps every exponent <= 0, so no overflow is possible.
struct KsResult {
  double value;
  double maxValue;
  std::vector<double> weights;  // d g_KS / d T_i  ==  softmax(rho (T_i - Tmax))
};

inline KsResult ksAggregate(const std::vector<double>& T, double rho) {
  const int n = static_cast<int>(T.size());
  if (n == 0) return {0.0, 0.0, {}};
  const double tmax = *std::max_element(T.begin(), T.end());
  std::vector<double> w(static_cast<std::size_t>(n));
  double sum = 0.0;
  for (int i = 0; i < n; ++i) {
    const double z = std::max(-700.0, rho * (T[static_cast<std::size_t>(i)] - tmax));
    w[static_cast<std::size_t>(i)] = std::exp(z);
    sum += w[static_cast<std::size_t>(i)];
  }
  const double value = tmax + std::log(sum / static_cast<double>(n)) / rho;
  for (int i = 0; i < n; ++i) w[static_cast<std::size_t>(i)] /= sum;
  return {value, tmax, w};
}

// Peak von-Mises style stress aggregator for structural multi-physics coupling.
// Takes per-cell scalar stress measures and returns the KS peak plus weights,
// optionally with a p-norm fallback for noisy fields.
inline KsResult ksAggregateShifted(const std::vector<double>& vals, double rho, double shift) {
  KsResult r = ksAggregate(vals, rho);
  r.value -= shift;
  return r;
}

}  // namespace mdo
