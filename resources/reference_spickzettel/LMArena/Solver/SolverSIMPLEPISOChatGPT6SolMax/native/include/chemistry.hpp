#pragma once

#include "continuum.hpp"
#include <atomic>
#include <array>
#include <cmath>
#include <limits>
#include <vector>

namespace continuum::chemistry {

// Kinetics is an AoT-generated class with rowptr/col and analytic eval(T,rho,Y,w,J).
template <class Kinetics> struct BatchView {
  int cells = 0;
  CAbiSpan<double>* species = nullptr; // species[k][cell]: borrowed SoA arrays
  CAbiSpan<const double> temperature, density;
};

template <size_t N> bool solve_local(std::array<double, N * N> a, std::array<double, N>& b) {
  for (size_t k = 0; k < N; ++k) {
    size_t pivot = k;
    for (size_t i = k + 1; i < N; ++i)
      if (std::abs(a[i * N + k]) > std::abs(a[pivot * N + k])) pivot = i;
    if (std::abs(a[pivot * N + k]) < 1e-24) return false;
    if (pivot != k) {
      for (size_t j = k; j < N; ++j) std::swap(a[k * N + j], a[pivot * N + j]);
      std::swap(b[k], b[pivot]);
    }
    for (size_t i = k + 1; i < N; ++i) {
      const double factor = a[i * N + k] / a[k * N + k];
      for (size_t j = k + 1; j < N; ++j) a[i * N + j] -= factor * a[k * N + j];
      b[i] -= factor * b[k];
    }
  }
  for (int i = static_cast<int>(N) - 1; i >= 0; --i) {
    for (size_t j = i + 1; j < N; ++j) b[i] -= a[i * N + j] * b[j];
    b[i] /= a[i * N + i];
  }
  return true;
}

template <class Kinetics> bool bdf_step(double t, double rho, double dt,
                                       const std::array<double, Kinetics::nspecies>& old,
                                       const std::array<double, Kinetics::nspecies>& previous,
                                       bool bdf2, std::array<double, Kinetics::nspecies>& next) {
  constexpr size_t n = Kinetics::nspecies;
  constexpr size_t nnz = Kinetics::nnz;
  const double a = bdf2 ? 1.5 : 1.0;
  next = old;
  for (int it = 0; it < 20; ++it) {
    std::array<double, n> w{}, residual{}, delta{};
    std::array<double, nnz> sparse{};
    Kinetics::eval(t, rho, next.data(), w.data(), sparse.data());
    std::array<double, n * n> mat{};
    double err = 0;
    for (size_t i = 0; i < n; ++i) {
      residual[i] = a * next[i] - (bdf2 ? 2.0 * old[i] - 0.5 * previous[i] : old[i]) - dt * w[i];
      delta[i] = -residual[i];
      err = std::max(err, std::abs(residual[i]));
      mat[i * n + i] = a;
      for (int k = Kinetics::rowptr[i]; k < Kinetics::rowptr[i + 1]; ++k)
        mat[i * n + Kinetics::col[k]] -= dt * sparse[k];
    }
    if (err < 2e-12) return true;
    if (!solve_local<n>(mat, delta)) return false;
    double step = 1;
    for (size_t i = 0; i < n; ++i)
      if (delta[i] < 0) step = std::min(step, 0.99 * std::max(next[i], 0.0) / -delta[i]);
    if (step < 1e-12) return false;
    for (size_t i = 0; i < n; ++i) next[i] += step * delta[i];
  }
  return false;
}

template <class Kinetics> int integrate_cell(double temperature, double rho, double dt,
                                              std::array<double, Kinetics::nspecies>& y,
                                              int max_substeps = 4096) {
  constexpr size_t n = Kinetics::nspecies;
  std::array<double, n> w{};
  std::array<double, Kinetics::nnz> jac{};
  Kinetics::eval(temperature, rho, y.data(), w.data(), jac.data());
  double stiffness = 0;
  for (size_t i = 0; i < n; ++i)
    for (int k = Kinetics::rowptr[i]; k < Kinetics::rowptr[i + 1]; ++k)
      if (Kinetics::col[k] == static_cast<int>(i)) stiffness = std::max(stiffness, std::abs(jac[k]));
  int steps = std::clamp(static_cast<int>(std::ceil(dt * stiffness / 0.8)), 1, max_substeps);
  const auto initial = y;
  for (;;) {
    auto old = initial, previous = initial;
    bool success = true;
    for (int s = 0; s < steps; ++s) {
      std::array<double, n> next{};
      if (!bdf_step<Kinetics>(temperature, rho, dt / steps, old, previous, s > 0, next)) {
        success = false; break;
      }
      previous = old; old = next;
    }
    if (success) { y = old; return steps; }
    if (steps >= max_substeps) return -1;
    steps = std::min(steps * 2, max_substeps);
  }
}

struct BatchReport {
  int active = 0, idle = 0, max_substeps = 0, failed = 0;
};

template <class Kinetics> BatchReport operator_split_bdf2(const BatchView<Kinetics>& batch,
                                                            double dt, double react_epsilon = 1e-8) {
  constexpr size_t n = Kinetics::nspecies;
  if (batch.cells < 0 || !batch.species || dt <= 0) throw std::invalid_argument("chemistry batch");
  std::vector<int> flag(batch.cells, 0), prefix(batch.cells + 1, 0);
  #pragma omp parallel for if(batch.cells > 256)
  for (int i = 0; i < batch.cells; ++i) {
    std::array<double, n> y{}, w{};
    std::array<double, Kinetics::nnz> jac{};
    for (size_t k = 0; k < n; ++k) y[k] = batch.species[k][i];
    Kinetics::eval(batch.temperature[i], batch.density[i], y.data(), w.data(), jac.data());
    for (double v : w) if (std::abs(v) > react_epsilon) { flag[i] = 1; break; }
  }
  for (int i = 0; i < batch.cells; ++i) prefix[i + 1] = prefix[i] + flag[i];
  std::vector<int> queue(prefix.back());
  for (int i = 0; i < batch.cells; ++i) if (flag[i]) queue[prefix[i]] = i;
  BatchReport report;
  report.active = static_cast<int>(queue.size());
  report.idle = batch.cells - report.active;
  std::atomic<int> next{0};
  int max_substeps = 0, failed = 0;
  #pragma omp parallel reduction(max:max_substeps) reduction(+:failed) if(batch.cells > 256)
  {
    int local_max = 0, local_failed = 0;
    for (;;) {
      const int slot = next.fetch_add(1, std::memory_order_relaxed);
      if (slot >= static_cast<int>(queue.size())) break;
      const int cell = queue[slot];
      std::array<double, n> y{};
      for (size_t k = 0; k < n; ++k) y[k] = batch.species[k][cell];
      int steps = integrate_cell<Kinetics>(batch.temperature[cell], batch.density[cell], dt, y);
      if (steps < 0) { ++local_failed; continue; }
      local_max = std::max(local_max, steps);
      for (size_t k = 0; k < n; ++k) batch.species[k][cell] = y[k];
    }
    max_substeps = std::max(max_substeps, local_max);
    failed += local_failed;
  }
  report.max_substeps = max_substeps;
  report.failed = failed;
  return report;
}

} // namespace continuum::chemistry