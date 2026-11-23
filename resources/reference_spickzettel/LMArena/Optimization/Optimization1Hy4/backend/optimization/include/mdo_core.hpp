// Phase 4 - MDO core: unstructured FVM graph, Helmholtz filter, SIMP/RAMP
// regularization chain, Heaviside projection, KS aggregation and the
// symmetric thermo-fluid state equation with analytic adjoint derivatives.
// Standalone C++20, no external dependencies.
#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <complex>
#include <cstddef>
#include <functional>
#include <limits>
#include <numeric>
#include <random>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace mdo {

using Vec = std::vector<double>;
using Cx = std::complex<double>;
using CVec = std::vector<Cx>;

inline double clamp01(double v) { return std::min(1.0, std::max(0.0, v)); }
inline double dot(const Vec& a, const Vec& b) {
  return std::inner_product(a.begin(), a.end(), b.begin(), 0.0);
}
inline double norm(const Vec& a) { return std::sqrt(std::max(0.0, dot(a, a))); }
inline Vec operator*(double s, const Vec& a) {
  Vec y(a.size()); for (size_t i = 0; i < a.size(); ++i) y[i] = s * a[i]; return y;
}
inline Vec operator/(const Vec& a, double s) {
  Vec y(a.size()); for (size_t i = 0; i < a.size(); ++i) y[i] = a[i] / s; return y;
}

// ---------------------------------------------------------------------------
// Sparse symmetric operator on the unstructured cell graph (CSR-like rows).
// Only edges are stored; the diagonal is assembled from edge weights plus an
// optional mass term, so A and A^T are identical by construction.
// ---------------------------------------------------------------------------
struct GraphOperator {
  int n = 0;
  std::vector<std::vector<std::pair<int, double>>> rows; // col, value
  Vec diag;
  Vec invDiag;

  int size() const { return n; }

  Vec apply(const Vec& x) const {
    Vec y(n, 0.0);
    for (int i = 0; i < n; ++i) {
      y[i] += diag[i] * x[i];
      for (const auto& [j, a] : rows[i]) y[i] += a * x[j];
    }
    return y;
  }
  Vec applyT(const Vec& x) const { return apply(x); } // symmetric

  void finish() {
    invDiag.assign(n, 1.0);
    for (int i = 0; i < n; ++i) {
      if (std::abs(diag[i]) < 1e-300) throw std::runtime_error("singular diagonal");
      invDiag[i] = 1.0 / diag[i];
    }
  }
};

// Jacobi-preconditioned conjugate gradients; converges only for SPD systems.
inline Vec pcg(const GraphOperator& A, const Vec& b, double tol = 1e-15,
               int maxit = 6000) {
  const int n = A.size();
  Vec x(n, 0.0), r = b, z(n), p(n), Ap(n);
  double rz = 0.0, bnorm = std::sqrt(std::inner_product(b.begin(), b.end(), b.begin(), 0.0));
  if (bnorm < 1e-300) return x;
  for (int i = 0; i < n; ++i) { z[i] = A.invDiag[i] * r[i]; p[i] = z[i]; rz += r[i] * z[i]; }
  for (int k = 0; k < maxit; ++k) {
    Ap = A.apply(p);
    double denom = std::inner_product(p.begin(), p.end(), Ap.begin(), 0.0);
    if (denom <= 0.0) throw std::runtime_error("PCG: operator is not positive definite");
    double alpha = rz / denom;
    for (int i = 0; i < n; ++i) { x[i] += alpha * p[i]; r[i] -= alpha * Ap[i]; }
    double rnorm = std::sqrt(std::inner_product(r.begin(), r.end(), r.begin(), 0.0));
    if (rnorm < tol * bnorm) return x;
    double rzNew = 0.0;
    for (int i = 0; i < n; ++i) { z[i] = A.invDiag[i] * r[i]; rzNew += r[i] * z[i]; }
    double beta = rzNew / rz;
    for (int i = 0; i < n; ++i) p[i] = z[i] + beta * p[i];
    rz = rzNew;
  }
  throw std::runtime_error("PCG did not converge");
}

// Complex right hand sides are solved by two real PCG solves (A is real).
inline CVec solveComplex(const GraphOperator& A, const CVec& b) {
  Vec br(A.size()), bi(A.size());
  for (int i = 0; i < A.size(); ++i) { br[i] = b[i].real(); bi[i] = b[i].imag(); }
  Vec xr = pcg(A, br), xi = pcg(A, bi);
  CVec out(A.size());
  for (int i = 0; i < A.size(); ++i) out[i] = Cx(xr[i], xi[i]);
  return out;
}

// ---------------------------------------------------------------------------
// Unstructured FVM cell graph: a Cartesian cell complex with face neighbours,
// Neumann boundary faces (only interior faces contribute flux).
// ---------------------------------------------------------------------------
struct CellGraph {
  int nx, ny, n;
  Vec cx, cy;
  std::vector<std::array<int, 4>> nb; // -1 for boundary
  std::vector<std::pair<int, int>> edges;

  CellGraph(int nx_, int ny_) : nx(nx_), ny(ny_), n(nx_ * ny_) {
    if (nx < 2 || ny < 2) throw std::runtime_error("grid must be at least 2x2");
    cx.resize(n); cy.resize(n); nb.resize(n);
    for (int j = 0; j < ny; ++j) for (int i = 0; i < nx; ++i) {
      int k = j * nx + i;
      cx[k] = (i + 0.5) / nx; cy[k] = (j + 0.5) / ny;
      nb[k] = {-1, -1, -1, -1};
      if (i > 0) nb[k][0] = k - 1;
      if (i < nx - 1) nb[k][1] = k + 1;
      if (j > 0) nb[k][2] = k - nx;
      if (j < ny - 1) nb[k][3] = k + nx;
      for (int d = 0; d < 4; ++d) if (nb[k][d] > k) edges.emplace_back(k, nb[k][d]);
    }
  }

  // A = I + alpha * L, alpha = (r_min / (2 sqrt(3)))^2 / h^2 with h = 1 cell.
  GraphOperator helmholtz(double rmin) const {
    if (rmin <= 0) throw std::runtime_error("rmin must be positive");
    double alpha = (rmin / (2.0 * std::sqrt(3.0))) * (rmin / (2.0 * std::sqrt(3.0)));
    GraphOperator A; A.n = n; A.rows.assign(n, {}); A.diag.assign(n, 1.0);
    for (auto [i, j] : edges) {
      A.rows[i].push_back({j, -alpha});
      A.rows[j].push_back({i, -alpha});
      A.diag[i] += alpha; A.diag[j] += alpha;
    }
    A.finish();
    return A;
  }
};

// ---------------------------------------------------------------------------
// Three-field chain: gamma -> Helmholtz gamma~ -> Heaviside gamma-bar -> rho.
// dJ/dgamma = A^{-T} ( (d gamma-bar / d gamma~) * dJ/d gamma-bar ).
// ---------------------------------------------------------------------------
struct ChainConfig {
  double rmin = 2.5;
  double betaH = 8.0;
  double eta = 0.5;
  double rampQ = 3.0;
  double rhoMin = 1e-3;
  double rhoMax = 1.0;
};

inline double heaviside(double t, double beta, double eta) {
  const double a = std::tanh(beta * eta);
  return (a + std::tanh(beta * (t - eta))) / (a + std::tanh(beta * (1.0 - eta)));
}
inline double heavisideD(double t, double beta, double eta) {
  const double s = std::tanh(beta * (t - eta));
  const double den = std::tanh(beta * eta) + std::tanh(beta * (1.0 - eta));
  return beta * (1.0 - s * s) / den;
}
inline Cx heavisideC(Cx t, double beta, double eta) {
  const double a = std::tanh(beta * eta);
  return (a + std::tanh(beta * (t - eta))) / (a + std::tanh(beta * (1.0 - eta)));
}
inline double rampDensity(double x, const ChainConfig& c) {
  const double q = c.rampQ;
  return c.rhoMin + (c.rhoMax - c.rhoMin) * (q * x) / (1.0 + (q - 1.0) * x);
}
inline double rampDensityD(double x, const ChainConfig& c) {
  const double q = c.rampQ;
  double d = 1.0 + (q - 1.0) * x;
  return (c.rhoMax - c.rhoMin) * q / (d * d);
}
inline Cx rampDensityC(Cx x, const ChainConfig& c) {
  const double q = c.rampQ;
  return c.rhoMin + (c.rhoMax - c.rhoMin) * (q * x) / (1.0 + (q - 1.0) * x);
}

struct DesignChain {
  const CellGraph& graph;
  ChainConfig cfg;
  GraphOperator A;
  Vec gamma, gammaTilde, gammaBar, rho, dBar, dRho;

  DesignChain(const CellGraph& g, ChainConfig c = {}) : graph(g), cfg(c), A(g.helmholtz(c.rmin)) {
    gamma.assign(g.n, 0.5);
  }

  void forward(const Vec& g) {
    if ((int)g.size() != graph.n) throw std::runtime_error("gamma size mismatch");
    gamma = g;
    gammaTilde = pcg(A, g);
    gammaBar.resize(graph.n); rho.resize(graph.n);
    dBar.resize(graph.n); dRho.resize(graph.n);
    for (int i = 0; i < graph.n; ++i) {
      gammaBar[i] = clamp01(heaviside(gammaTilde[i], cfg.betaH, cfg.eta));
      dBar[i] = heavisideD(gammaTilde[i], cfg.betaH, cfg.eta);
      rho[i] = rampDensity(gammaBar[i], cfg);
      dRho[i] = rampDensityD(gammaBar[i], cfg);
    }
  }

  // Exact reverse-mode VJP through projection and the symmetric Helmholtz solve.
  Vec vjp(const Vec& dJdBar) const {
    Vec v(graph.n);
    for (int i = 0; i < graph.n; ++i) v[i] = dJdBar[i] * dBar[i];
    return pcg(A, v); // A^{-T} v (A is symmetric; applyT is the adjoint path)
  }

  // Complex-step forward evaluation for machine-precision gradient checks.
  CVec forwardComplex(const CVec& g) const {
    CVec gt = solveComplex(A, g), gb(graph.n), rh(graph.n);
    for (int i = 0; i < graph.n; ++i) {
      gb[i] = heavisideC(gt[i], cfg.betaH, cfg.eta);
      rh[i] = rampDensityC(gb[i], cfg);
    }
    return rh;
  }
};

// ---------------------------------------------------------------------------
// Shifted Kreisselmeier-Steinhauser aggregation (overflow-safe).
// ---------------------------------------------------------------------------
struct KSResult { double value = 0.0; double maxValue = 0.0; Vec weight; };

inline KSResult ksAggregate(const Vec& v, double rhoKS) {
  if (v.empty()) throw std::runtime_error("empty KS vector");
  double mx = *std::max_element(v.begin(), v.end());
  Vec w(v.size()); double sum = 0.0;
  for (size_t i = 0; i < v.size(); ++i) {
    w[i] = std::exp(rhoKS * (v[i] - mx));
    sum += w[i];
  }
  double val = mx + std::log(sum / double(v.size())) / rhoKS;
  for (auto& e : w) e /= sum; // d KS / d v_i = softmax_i
  return {val, mx, w};
}
inline Cx ksAggregateComplex(const CVec& v, double rhoKS) {
  if (v.empty()) throw std::runtime_error("empty KS vector");
  double mx = -std::numeric_limits<double>::infinity();
  for (const auto& z : v) mx = std::max(mx, z.real());
  Cx sum(0.0, 0.0);
  for (const auto& z : v) sum += std::exp(double(rhoKS) * (z - Cx(mx, 0.0)));
  return Cx(mx, 0.0) + std::log(sum / double(v.size())) / double(rhoKS);
}

// ---------------------------------------------------------------------------
// Steady thermo-fluid reduced state on the FVM graph:
//   B(rho, xi) T = f,  B = sum_edges c_ij (e_i-e_j)(e_i-e_j)^T + h I,
//   c_ij = (k_i + k_j)/2, k_i = k(rho_i) (1 + sMat xi_i), h = h0(1+sConv xi_bc).
// B is SPD, therefore adjoint and primal use the same operator.
// ---------------------------------------------------------------------------
struct ThermalConfig {
  double kMin = 0.08, kMax = 2.4;
  double hConv = 0.35;
  double sMat = 0.08, sConv = 0.10;
  double source = 1.0;
};

struct ThermalState {
  Vec T, kbase, dkDgammaBar;
  GraphOperator B;
};

class ThermalModel {
 public:
  const CellGraph& graph;
  ChainConfig chainCfg;
  ThermalConfig tc;
  Vec f_;

  ThermalModel(const CellGraph& g, ChainConfig c = {}, ThermalConfig t = {})
      : graph(g), chainCfg(c), tc(t) {
    f_.assign(g.n, 0.0);
    for (int i = 0; i < g.n; ++i)
      f_[i] = tc.source * (0.35 + 0.65 * std::sin(2.7 * g.cx[i]) * std::cos(2.1 * g.cy[i]) + 0.3 * g.cy[i]);
  }

  const Vec& source() const { return f_; }
  int stochasticDim() const { return graph.n + 1; }

  double kOf(double rho) const {
    double t = (rho - chainCfg.rhoMin) / std::max(1e-12, chainCfg.rhoMax - chainCfg.rhoMin);
    double q = chainCfg.rampQ;
    // invert B-spline-free RAMP mapping to recover the projected fraction
    double den = 1.0 - (1.0 - q) * t;
    return tc.kMin + (tc.kMax - tc.kMin) * (den > 1e-12 ? t / den : t);
  }
  double dkDrho(double rho) const {
    double q = chainCfg.rampQ;
    double x = rhoBarFromRho(rho);
    return (tc.kMax - tc.kMin) * (q * chainCfg.rampQ > 0 ? 1.0 : 1.0) /
           std::max(1e-12, std::pow(1.0 + (q - 1.0) * x, 2.0)) / rampDensityD(x, chainCfg);
  }

  // k(gamma-bar) directly (same rational law as RAMP density, rescaled).
  double kFromBar(double gb) const {
    double q = chainCfg.rampQ;
    return tc.kMin + (tc.kMax - tc.kMin) * (q * gb) / (1.0 + (q - 1.0) * gb);
  }
  double dkFromBar(double gb) const {
    double q = chainCfg.rampQ, d = 1.0 + (q - 1.0) * gb;
    return (tc.kMax - tc.kMin) * q / (d * d);
  }
  double rhoBarFromRho(double rho) const {
    double q = chainCfg.rampQ;
    double a = (rho - chainCfg.rhoMin) / std::max(1e-12, chainCfg.rhoMax - chainCfg.rhoMin);
    double den = 1.0 - (1.0 - q) * a;
    return std::abs(den) < 1e-12 ? a : a / den;
  }

  GraphOperator assemble(const Vec& kbase, double convFactor = 1.0) const {
    GraphOperator B; B.n = graph.n; B.rows.assign(graph.n, {}); B.diag.assign(graph.n, tc.hConv * convFactor);
    for (auto [i, j] : graph.edges) {
      double c = 0.5 * (kbase[i] + kbase[j]);
      B.rows[i].push_back({j, -c}); B.rows[j].push_back({i, -c});
      B.diag[i] += c; B.diag[j] += c;
    }
    B.finish();
    return B;
  }

  ThermalState primal(const DesignChain& chain, const Vec& xi = {}) const {
    Vec kbase(graph.n);
    for (int i = 0; i < graph.n; ++i) kbase[i] = kFromBar(chain.gammaBar[i]);
    double conv = 1.0;
    if (!xi.empty()) conv *= (1.0 + tc.sConv * xi.back());
    for (int i = 0; i < graph.n; ++i)
      if (!xi.empty()) kbase[i] *= (1.0 + tc.sMat * xi[i]);
    GraphOperator B = assemble(kbase, conv);
    ThermalState st;
    st.kbase = kbase; st.B = B;
    st.T = pcg(B, f_);
    st.dkDgammaBar.resize(graph.n);
    for (int i = 0; i < graph.n; ++i) st.dkDgammaBar[i] = dkFromBar(chain.gammaBar[i]);
    return st;
  }

  // Edgewise contraction u^T (dB/dgamma[delta]) v, assembled in O(nnz).
  Vec edgeGradient(const Vec& dkDgamma, const Vec& u, const Vec& v) const {
    Vec w(graph.n, 0.0);
    for (auto [i, j] : graph.edges) {
      double p = (u[i] - u[j]) * (v[i] - v[j]);
      w[i] += 0.5 * dkDgamma[i] * p;
      w[j] += 0.5 * dkDgamma[j] * p;
    }
    return w;
  }
  double edgeDot(const Vec& dkDgamma, const Vec& u, const Vec& v) const {
    double s = 0.0;
    for (auto [i, j] : graph.edges)
      s += 0.5 * (dkDgamma[i] + dkDgamma[j]) * (u[i] - u[j]) * (v[i] - v[j]);
    return s;
  }

  // Sensitivity of stress-like peak quantity: derivative of softmax weights.
  Vec weightTangent(const Vec& w, const Vec& dT, double rhoKS) const {
    Vec dw(graph.n);
    double mean = 0.0;
    for (int i = 0; i < graph.n; ++i) mean += w[i] * dT[i];
    for (int i = 0; i < graph.n; ++i) dw[i] = rhoKS * w[i] * (dT[i] - mean);
    return dw;
  }
};

// Covariance kernel Sigma_ij = sigma^2 exp(-||x_i-x_j||/ell) (+ jitter).
inline std::vector<Vec> exponentialCovariance(const CellGraph& g, double sigma,
                                              double ell, double jitter = 1e-8) {
  const int m = g.n + 1; // last coordinate is the convection BC uncertainty
  std::vector<Vec> S(m, Vec(m));
  for (int i = 0; i < g.n; ++i) for (int j = 0; j < g.n; ++j) {
    double d = std::hypot(g.cx[i] - g.cx[j], g.cy[i] - g.cy[j]);
    S[i][j] = sigma * sigma * std::exp(-d / std::max(1e-8, ell));
    if (i == j) S[i][j] += jitter;
  }
  for (int i = 0; i < g.n; ++i) { S[i][g.n] = 0.0; S[g.n][i] = 0.0; }
  S[g.n][g.n] = sigma * sigma + jitter;
  return S;
}

inline Vec matVec(const std::vector<Vec>& S, const Vec& v) {
  Vec y(v.size(), 0.0);
  for (size_t i = 0; i < v.size(); ++i) for (size_t j = 0; j < v.size(); ++j) y[i] += S[i][j] * v[j];
  return y;
}

// Cholesky factor for correlated Gaussian sampling.
inline std::vector<Vec> cholesky(const std::vector<Vec>& S) {
  int n = (int)S.size(); std::vector<Vec> L(n, Vec(n, 0.0));
  for (int i = 0; i < n; ++i) for (int j = 0; j <= i; ++j) {
    double s = S[i][j];
    for (int k = 0; k < j; ++k) s -= L[i][k] * L[j][k];
    if (i == j) {
      if (s <= 0) throw std::runtime_error("covariance is not positive definite");
      L[i][i] = std::sqrt(s);
    } else L[i][j] = s / L[j][j];
  }
  return L;
}

} // namespace mdo
