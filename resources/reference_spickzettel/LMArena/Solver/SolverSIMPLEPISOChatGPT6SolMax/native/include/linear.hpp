#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <map>
#include <numeric>
#include <stdexcept>
#include <utility>
#include <vector>

namespace continuum::linear {

template <class T> struct CSR {
  int n = 0;
  std::vector<int> row, col;
  std::vector<T> val;

  std::vector<T> multiply(const std::vector<T>& x) const {
    if (!col.empty() && *std::max_element(col.begin(), col.end()) >= static_cast<int>(x.size()))
      throw std::invalid_argument("CSR vector size");
    std::vector<T> y(n, T(0));
    #pragma omp parallel for if(n > 2048)
    for (int i = 0; i < n; ++i)
      for (int k = row[i]; k < row[i + 1]; ++k) y[i] += val[k] * x[col[k]];
    return y;
  }

  T infinity_norm() const {
    T m = 0;
    for (int i = 0; i < n; ++i) {
      T s = 0;
      for (int k = row[i]; k < row[i + 1]; ++k) s += std::abs(val[k]);
      m = std::max(m, s);
    }
    return m;
  }
};

template <class T> CSR<T> from_rows(const std::vector<std::map<int, T>>& rows) {
  CSR<T> a;
  a.n = static_cast<int>(rows.size());
  a.row.push_back(0);
  for (const auto& r : rows) {
    for (auto [j, v] : r) {
      if (std::abs(v) > std::numeric_limits<T>::epsilon() * T(0.01)) {
        a.col.push_back(j);
        a.val.push_back(v);
      }
    }
    a.row.push_back(static_cast<int>(a.col.size()));
  }
  return a;
}

template <class T> CSR<T> transpose(const CSR<T>& a, int cols) {
  std::vector<std::map<int, T>> rows(cols);
  for (int i = 0; i < a.n; ++i)
    for (int k = a.row[i]; k < a.row[i + 1]; ++k) rows[a.col[k]][i] += a.val[k];
  return from_rows(rows);
}

inline double dot(const std::vector<double>& a, const std::vector<double>& b) {
  double s = 0;
  for (size_t i = 0; i < a.size(); ++i) s += a[i] * b[i];
  return s;
}
inline double norm(const std::vector<double>& a) { return std::sqrt(dot(a, a)); }

template <class T> std::vector<T> dense_solve(const CSR<T>& a, std::vector<T> b) {
  const int n = a.n;
  std::vector<T> m(static_cast<size_t>(n) * n, T(0));
  for (int i = 0; i < n; ++i)
    for (int k = a.row[i]; k < a.row[i + 1]; ++k) m[static_cast<size_t>(i) * n + a.col[k]] += a.val[k];
  for (int k = 0; k < n; ++k) {
    int pivot = k;
    for (int i = k + 1; i < n; ++i)
      if (std::abs(m[static_cast<size_t>(i) * n + k]) > std::abs(m[static_cast<size_t>(pivot) * n + k])) pivot = i;
    if (std::abs(m[static_cast<size_t>(pivot) * n + k]) < T(1e-18)) throw std::runtime_error("singular matrix");
    if (pivot != k) {
      for (int j = k; j < n; ++j) std::swap(m[static_cast<size_t>(k) * n + j], m[static_cast<size_t>(pivot) * n + j]);
      std::swap(b[k], b[pivot]);
    }
    const T d = m[static_cast<size_t>(k) * n + k];
    for (int i = k + 1; i < n; ++i) {
      const T f = m[static_cast<size_t>(i) * n + k] / d;
      for (int j = k + 1; j < n; ++j) m[static_cast<size_t>(i) * n + j] -= f * m[static_cast<size_t>(k) * n + j];
      b[i] -= f * b[k];
    }
  }
  std::vector<T> x(n, T(0));
  for (int i = n - 1; i >= 0; --i) {
    T s = b[i];
    for (int j = i + 1; j < n; ++j) s -= m[static_cast<size_t>(i) * n + j] * x[j];
    x[i] = s / m[static_cast<size_t>(i) * n + i];
  }
  return x;
}

enum class Smoother { l1_jacobi, chebyshev };
struct AMGOptions {
  bool smoothed_aggregation = true;
  Smoother smoother = Smoother::chebyshev;
  float strength = 0.25f;
  int min_coarse = 10;
  int max_levels = 12;
};

class AMG {
  struct Level {
    CSR<float> a, p, pt;
    std::vector<float> l1;
  };
  std::vector<Level> levels_;
  AMGOptions options_;

  static std::vector<int> aggregate(const CSR<float>& a, float threshold) {
    const int n = a.n;
    std::vector<std::vector<int>> strong(n);
    for (int i = 0; i < n; ++i) {
      float largest = 0;
      for (int k = a.row[i]; k < a.row[i + 1]; ++k)
        if (a.col[k] != i) largest = std::max(largest, std::abs(a.val[k]));
      for (int k = a.row[i]; k < a.row[i + 1]; ++k)
        if (a.col[k] != i && largest > 0 && std::abs(a.val[k]) >= threshold * largest) {
          strong[i].push_back(a.col[k]);
          strong[a.col[k]].push_back(i);
        }
    }
    for (auto& neighbors : strong) {
      std::sort(neighbors.begin(), neighbors.end());
      neighbors.erase(std::unique(neighbors.begin(), neighbors.end()), neighbors.end());
    }
    std::vector<double> priority(n);
    for (int i = 0; i < n; ++i) {
      uint32_t h = static_cast<uint32_t>(i + 1) * 2654435761u;
      h ^= h >> 16;
      priority[i] = static_cast<double>(strong[i].size()) + double(h) / 4294967296.0;
    }
    std::vector<int> state(n, -1);
    while (std::find(state.begin(), state.end(), -1) != state.end()) {
      std::vector<int> seeds;
      for (int i = 0; i < n; ++i) if (state[i] == -1) {
        bool maximal = true;
        for (int j : strong[i])
          if (state[j] == -1 && (priority[j] > priority[i] || (priority[j] == priority[i] && j > i))) {
            maximal = false; break;
          }
        if (maximal) seeds.push_back(i);
      }
      for (int i : seeds) state[i] = 1;
      for (int i : seeds) for (int j : strong[i]) if (state[j] == -1) state[j] = 0;
    }
    std::vector<int> seed_id(n, -1);
    int nc = 0;
    for (int i = 0; i < n; ++i) if (state[i] == 1) seed_id[i] = nc++;
    std::vector<int> agg = seed_id;
    for (int i = 0; i < n; ++i) if (agg[i] < 0) {
      float best = -1;
      for (int k = a.row[i]; k < a.row[i + 1]; ++k) {
        int j = a.col[k];
        if (seed_id[j] >= 0 && std::abs(a.val[k]) > best) {
          best = std::abs(a.val[k]); agg[i] = seed_id[j];
        }
      }
      if (agg[i] < 0) agg[i] = nc++;
    }
    return agg;
  }

  void smooth(const Level& lev, std::vector<float>& x, const std::vector<float>& b, int steps) const {
    for (int it = 0; it < steps; ++it) {
      const auto ax = lev.a.multiply(x);
      float tau = 0.8f;
      if (options_.smoother == Smoother::chebyshev) {
        constexpr float pi = 3.14159265358979323846f;
        constexpr float lo = 0.08f, hi = 1.02f;
        tau = 1.0f / (0.5f * (hi + lo) - 0.5f * (hi - lo) *
                       std::cos(pi * (2.0f * it + 1.0f) / (2.0f * steps)));
      }
      #pragma omp parallel for if(lev.a.n > 2048)
      for (int i = 0; i < lev.a.n; ++i) x[i] += tau * (b[i] - ax[i]) / lev.l1[i];
    }
  }

  void cycle(int depth, const std::vector<float>& b, std::vector<float>& x) const {
    const auto& lev = levels_[depth];
    if (depth + 1 == static_cast<int>(levels_.size())) {
      x = dense_solve(lev.a, b);
      return;
    }
    smooth(lev, x, b, 3);
    auto ax = lev.a.multiply(x);
    std::vector<float> r(b.size());
    for (size_t i = 0; i < b.size(); ++i) r[i] = b[i] - ax[i];
    auto rc = lev.pt.multiply(r);
    std::vector<float> ec(rc.size(), 0.0f);
    cycle(depth + 1, rc, ec);
    auto correction = lev.p.multiply(ec);
    for (size_t i = 0; i < x.size(); ++i) x[i] += correction[i];
    smooth(lev, x, b, 3);
  }

public:
  explicit AMG(const CSR<double>& a, AMGOptions options = {}) : options_(options) {
    if (a.n == 0 || a.row.size() != static_cast<size_t>(a.n + 1)) throw std::invalid_argument("empty AMG matrix");
    CSR<float> first{a.n, a.row, a.col, {}};
    first.val.reserve(a.val.size());
    for (double v : a.val) first.val.push_back(static_cast<float>(v));
    levels_.push_back({std::move(first), {}, {}, {}});
    for (int depth = 0; depth < options_.max_levels; ++depth) {
      auto& lev = levels_.back();
      lev.l1.resize(lev.a.n);
      for (int i = 0; i < lev.a.n; ++i) {
        float sum = 0;
        for (int k = lev.a.row[i]; k < lev.a.row[i + 1]; ++k) sum += std::abs(lev.a.val[k]);
        lev.l1[i] = std::max(sum, 1e-20f);
      }
      if (lev.a.n <= options_.min_coarse || depth + 1 == options_.max_levels) break;
      const auto agg = aggregate(lev.a, options_.strength);
      const int nc = *std::max_element(agg.begin(), agg.end()) + 1;
      if (nc >= static_cast<int>(0.85 * lev.a.n)) break;
      std::vector<std::map<int, float>> prows(lev.a.n);
      for (int i = 0; i < lev.a.n; ++i) prows[i][agg[i]] = 1.0f;
      if (options_.smoothed_aggregation) {
        for (int i = 0; i < lev.a.n; ++i) {
          float diag = 0;
          for (int k = lev.a.row[i]; k < lev.a.row[i + 1]; ++k)
            if (lev.a.col[k] == i) diag = lev.a.val[k];
          if (std::abs(diag) < 1e-20f) continue;
          for (int k = lev.a.row[i]; k < lev.a.row[i + 1]; ++k)
            prows[i][agg[lev.a.col[k]]] -= 0.66f * lev.a.val[k] / diag;
        }
      }
      lev.p = from_rows(prows);
      lev.pt = transpose(lev.p, nc);
      std::vector<std::map<int, float>> coarse(nc);
      for (int i = 0; i < lev.a.n; ++i)
        for (int pk = lev.p.row[i]; pk < lev.p.row[i + 1]; ++pk)
          for (int ak = lev.a.row[i]; ak < lev.a.row[i + 1]; ++ak) {
            int j = lev.a.col[ak];
            for (int qk = lev.p.row[j]; qk < lev.p.row[j + 1]; ++qk)
              coarse[lev.p.col[pk]][lev.p.col[qk]] += lev.p.val[pk] * lev.a.val[ak] * lev.p.val[qk];
          }
      levels_.push_back({from_rows(coarse), {}, {}, {}});
    }
  }

  int levels() const { return static_cast<int>(levels_.size()); }

  std::vector<double> apply(const std::vector<double>& r, int cycles = 2) const {
    if (static_cast<int>(r.size()) != levels_.front().a.n) throw std::invalid_argument("AMG rhs size");
    double scale = 0;
    for (double v : r) scale = std::max(scale, std::abs(v));
    if (scale == 0) return std::vector<double>(r.size(), 0.0);
    std::vector<float> b(r.size()), x(r.size(), 0.0f);
    for (size_t i = 0; i < r.size(); ++i) b[i] = static_cast<float>(r[i] / scale);
    for (int i = 0; i < cycles; ++i) cycle(0, b, x);
    std::vector<double> out(r.size());
    for (size_t i = 0; i < r.size(); ++i) out[i] = static_cast<double>(x[i]) * scale;
    return out;
  }
};

struct SolveReport {
  std::vector<double> x;
  std::vector<double> residual_history;
  int iterations = 0;
  bool converged = false;
  bool stagnated = false;
  bool round_limited = false;
  double eta_round = 0; // FP64 a-posteriori normwise backward error.
  double fp32_floor = 0; // Predicted inner precision floor before refinement.
};

inline void assess(const CSR<double>& a, const std::vector<double>& b, SolveReport& out) {
  const auto ax = a.multiply(out.x);
  double rn = 0, xn = 0, bn = 0;
  for (int i = 0; i < a.n; ++i) {
    rn = std::max(rn, std::abs(b[i] - ax[i]));
    xn = std::max(xn, std::abs(out.x[i]));
    bn = std::max(bn, std::abs(b[i]));
  }
  const double denom = a.infinity_norm() * xn + bn + std::numeric_limits<double>::min();
  out.eta_round = rn / denom;
  out.fp32_floor = std::numeric_limits<float>::epsilon() * a.infinity_norm() * xn / denom;
  out.round_limited = out.stagnated && out.eta_round <= 8.0 * out.fp32_floor;
}

inline SolveReport iterative_refinement(const CSR<double>& a, const std::vector<double>& b,
                                        const AMG& pre, double tol = 1e-11, int max_steps = 50) {
  SolveReport out;
  out.x.assign(a.n, 0);
  const double target = tol * std::max(norm(b), 1e-30);
  for (int it = 0; it < max_steps; ++it) {
    const auto ax = a.multiply(out.x);
    std::vector<double> r(a.n);
    for (int i = 0; i < a.n; ++i) r[i] = b[i] - ax[i];
    const double residual = norm(r);
    out.residual_history.push_back(residual);
    if (residual <= target) { out.converged = true; break; }
    if (it >= 7 && residual >= 0.995 * out.residual_history[it - 6]) {
      out.stagnated = true; break;
    }
    const auto correction = pre.apply(r, 2);
    for (int i = 0; i < a.n; ++i) out.x[i] += correction[i];
    ++out.iterations;
  }
  assess(a, b, out);
  return out;
}

inline SolveReport mixed_bicgstab(const CSR<double>& a, const std::vector<double>& b,
                                 const AMG& pre, double tol = 1e-11, int max_steps = 200) {
  SolveReport out;
  out.x.assign(a.n, 0);
  std::vector<double> r = b, r0 = b, p(a.n, 0), v(a.n, 0);
  double rho_old = 1, alpha = 1, omega = 1;
  const double target = tol * std::max(norm(b), 1e-30);
  for (int it = 0; it < max_steps; ++it) {
    double rn = norm(r);
    out.residual_history.push_back(rn);
    if (rn <= target) { out.converged = true; break; }
    if (it >= 8 && rn >= 0.995 * out.residual_history[it - 6]) { out.stagnated = true; break; }
    double rho = dot(r0, r);
    if (std::abs(rho) < 1e-290 || std::abs(omega) < 1e-290) { out.stagnated = true; break; }
    double beta = (rho / rho_old) * (alpha / omega);
    for (int i = 0; i < a.n; ++i) p[i] = r[i] + beta * (p[i] - omega * v[i]);
    auto ph = pre.apply(p, 1);
    v = a.multiply(ph);
    double den = dot(r0, v);
    if (std::abs(den) < 1e-290) { out.stagnated = true; break; }
    alpha = rho / den;
    std::vector<double> s(a.n);
    for (int i = 0; i < a.n; ++i) s[i] = r[i] - alpha * v[i];
    if (norm(s) <= target) {
      for (int i = 0; i < a.n; ++i) out.x[i] += alpha * ph[i];
      ++out.iterations; out.converged = true; break;
    }
    auto sh = pre.apply(s, 1);
    auto t = a.multiply(sh);
    const double tt = dot(t, t);
    if (tt < 1e-290) { out.stagnated = true; break; }
    omega = dot(t, s) / tt;
    for (int i = 0; i < a.n; ++i) {
      out.x[i] += alpha * ph[i] + omega * sh[i];
      r[i] = s[i] - omega * t[i];
    }
    rho_old = rho;
    ++out.iterations;
    if (it % 8 == 7) {
      auto ax = a.multiply(out.x);
      for (int i = 0; i < a.n; ++i) r[i] = b[i] - ax[i];
    }
  }
  assess(a, b, out);
  return out;
}

// Flexible GMRES retains every independently preconditioned search direction.
inline SolveReport mixed_fgmres(const CSR<double>& a, const std::vector<double>& b,
                               const AMG& pre, double tol = 1e-11, int max_steps = 120, int restart = 24) {
  SolveReport out;
  out.x.assign(a.n, 0);
  const double target = tol * std::max(norm(b), 1e-30);
  while (out.iterations < max_steps) {
    auto ax = a.multiply(out.x);
    std::vector<double> r(a.n);
    for (int i = 0; i < a.n; ++i) r[i] = b[i] - ax[i];
    double beta = norm(r);
    out.residual_history.push_back(beta);
    if (beta <= target) { out.converged = true; break; }
    std::vector<std::vector<double>> v(1, r), z;
    for (double& q : v[0]) q /= beta;
    std::vector<std::vector<double>> h(restart + 1, std::vector<double>(restart, 0));
    std::vector<double> cs(restart), sn(restart), g(restart + 1, 0);
    g[0] = beta;
    int used = 0;
    for (int j = 0; j < restart && out.iterations < max_steps; ++j) {
      z.push_back(pre.apply(v[j], 1));
      auto w = a.multiply(z[j]);
      for (int i = 0; i <= j; ++i) {
        h[i][j] = dot(w, v[i]);
        for (int k = 0; k < a.n; ++k) w[k] -= h[i][j] * v[i][k];
      }
      h[j + 1][j] = norm(w);
      if (h[j + 1][j] > 1e-30) {
        for (double& q : w) q /= h[j + 1][j];
        v.push_back(std::move(w));
      } else v.push_back(std::vector<double>(a.n, 0));
      for (int i = 0; i < j; ++i) {
        double a0 = h[i][j], a1 = h[i + 1][j];
        h[i][j] = cs[i] * a0 + sn[i] * a1;
        h[i + 1][j] = -sn[i] * a0 + cs[i] * a1;
      }
      const double len = std::hypot(h[j][j], h[j + 1][j]);
      cs[j] = len ? h[j][j] / len : 1;
      sn[j] = len ? h[j + 1][j] / len : 0;
      h[j][j] = len;
      h[j + 1][j] = 0;
      g[j + 1] = -sn[j] * g[j];
      g[j] *= cs[j];
      used = j + 1;
      ++out.iterations;
      out.residual_history.push_back(std::abs(g[j + 1]));
      if (std::abs(g[j + 1]) <= target) break;
    }
    std::vector<double> y(used, 0);
    for (int i = used - 1; i >= 0; --i) {
      double s = g[i];
      for (int j = i + 1; j < used; ++j) s -= h[i][j] * y[j];
      if (std::abs(h[i][i]) < 1e-30) { out.stagnated = true; break; }
      y[i] = s / h[i][i];
    }
    if (out.stagnated) break;
    for (int j = 0; j < used; ++j)
      for (int i = 0; i < a.n; ++i) out.x[i] += z[j][i] * y[j];
    if (out.residual_history.size() > 8) {
      auto last = out.residual_history.back();
      if (last >= 0.995 * out.residual_history[out.residual_history.size() - 7]) {
        out.stagnated = true; break;
      }
    }
  }
  if (!out.converged) {
    auto ax = a.multiply(out.x);
    for (int i = 0; i < a.n; ++i) ax[i] = b[i] - ax[i];
    out.converged = norm(ax) <= target;
  }
  assess(a, b, out);
  return out;
}

} // namespace continuum::linear