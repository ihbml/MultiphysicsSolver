// ============================================================================
// mo/core.hpp — Phase-4 Multiphysics Platform, backend/optimization/
// Standalone C++20 core:
//   * dense linear algebra (real + complex, partial-pivot LU, Cholesky)
//   * unstructured finite-volume-method (FVM) graph on jittered node cloud
//   * Helmholtz PDE density filter  -(rmin^2/12) lap(g~) + g~ = g
//   * volume-preserving tanh-Heaviside projection with exact derivative
//   * SIMP and RAMP material interpolation with exact derivatives
//   * Kreisselmeier-Steinhauser (KS) aggregation (shifted, overflow-safe)
//   * conduction adjoint  dJ/dgbar = -k'(gbar_i) * t1^T D_i T
// The whole chain is templated on T in {double, complex<double>} so that
// complex-step gradients of the full pipeline are available for verification.
// ============================================================================
#pragma once
#include <vector>
#include <complex>
#include <cmath>
#include <algorithm>
#include <numeric>
#include <random>
#include <functional>
#include <string>
#include <sstream>
#include <stdexcept>
#include <limits>

namespace mo {

using RVec  = std::vector<double>;
using cplx  = std::complex<double>;
using CVec  = std::vector<cplx>;
template <typename T> using Mat = std::vector<std::vector<T>>;
using RMat  = Mat<double>;
using CMat  = Mat<cplx>;

template <typename T> inline double rabs(const T& z) { return std::abs(z); }
inline double dot(const RVec& a, const RVec& b) {
  double s = 0.0; for (size_t i = 0; i < a.size(); ++i) s += a[i]*b[i]; return s;
}
inline double nrm_inf(const RVec& a) {
  double m = 0.0; for (double v : a) m = std::max(m, std::fabs(v)); return m;
}
inline double nrm2(const RVec& a) { return std::sqrt(dot(a, a)); }
template <typename T> double cnrm2(const std::vector<T>& a) {
  double s = 0.0; for (const T& v : a) s += std::norm(v); return std::sqrt(s);
}

// ---------------------------------------------------------------------------
// Dense LU with partial pivoting (works for double and complex<double>)
// ---------------------------------------------------------------------------
template <typename T>
struct DenseLU {
  int n = 0;
  Mat<T> U;                    // factored form
  Mat<T> A0;                   // original matrix (for iterative refinement)
  std::vector<int> piv;
  void factor(const Mat<T>& A) {
    n = (int)A.size();
    U = A; A0 = A;
    piv.assign(n, 0);
    for (int k = 0; k < n; ++k) {
      int p = k;
      for (int i = k + 1; i < n; ++i) if (rabs(U[i][k]) > rabs(U[p][k])) p = i;
      piv[k] = p;
      if (p != k) std::swap(U[p], U[k]);
      if (rabs(U[k][k]) < 1e-300) throw std::runtime_error("DenseLU: singular matrix");
      for (int i = k + 1; i < n; ++i) {
        U[i][k] /= U[k][k];
        for (int j = k + 1; j < n; ++j) U[i][j] -= U[i][k]*U[k][j];
      }
    }
  }
  void solve_core(const std::vector<T>& b, std::vector<T>& x) const {
    x = b;
    for (int k = 0; k < n; ++k) {
      int p = piv[k];
      if (p != k) std::swap(x[p], x[k]);
      for (int i = k + 1; i < n; ++i) x[i] -= x[k]*U[i][k];
    }
    for (int k = n - 1; k >= 0; --k) {
      for (int j = k + 1; j < n; ++j) x[k] -= U[k][j]*x[j];
      x[k] /= U[k][k];
    }
  }
  // solve + one iterative-refinement pass (drives error to ~eps, not kappa*eps)
  void solve(const std::vector<T>& b, std::vector<T>& x) const {
    solve_core(b, x);
    std::vector<T> r(n), dx;
    for (int i = 0; i < n; ++i) {
      T s(0.0);
      for (int j = 0; j < n; ++j) s += A0[i][j]*x[j];
      r[i] = b[i] - s;
    }
    solve_core(r, dx);
    for (int i = 0; i < n; ++i) x[i] += dx[i];
  }
};

// Cholesky for symmetric positive-definite (double) — used for covariance
struct Cholesky {
  int n = 0; RMat L;
  void factor(const RMat& A, double jitter = 1e-12) {
    n = (int)A.size(); L = A;
    for (int j = 0; j < n; ++j) L[j][j] += jitter;
    for (int i = 0; i < n; ++i)
      for (int j = 0; j <= i; ++j) {
        double s = L[i][j];
        for (int k = 0; k < j; ++k) s -= L[i][k]*L[j][k];
        if (j == i) {
          if (s <= 0) throw std::runtime_error("Cholesky: not positive definite");
          L[i][j] = std::sqrt(s);
        } else L[i][j] = s / L[j][j];
      }
  }
  // y = L x  (lower triangular)
  void apply_L(const RVec& x, RVec& y) const {
    y.assign(n, 0.0);
    for (int i = 0; i < n; ++i) {
      double s = L[i][i]*x[i];
      for (int k = 0; k < i; ++k) s += L[i][k]*x[k];
      y[i] = s;
    }
  }
};

// ---------------------------------------------------------------------------
// RNG helpers
// ---------------------------------------------------------------------------
struct Rng {
  std::mt19937 g;
  explicit Rng(unsigned seed = 12345u) : g(seed) {}
  double u() {
    std::uniform_real_distribution<double> d(0.0, 1.0);
    return d(g);
  }
  double nrm(double mu = 0.0, double s = 1.0) {
    std::normal_distribution<double> d(mu, s);
    return d(g);
  }
};

// ---------------------------------------------------------------------------
// Unstructured FVM graph model (2D steady-state conduction on a graph)
// ---------------------------------------------------------------------------
struct Model {
  int nx = 11, ny = 11, n = 0, seed = 7;
  RVec px, py;                                // node coordinates in [0,1]^2
  struct Edge { int i, j; double d, w0; };    // w0 = geometric face-length/distance
  std::vector<Edge> eg;
  RVec Aarea, davg;                           // cell-area proxy, mean node spacing
  RVec b0;                                    // heat source
  // material
  double hconv = 0.02;             // convective loss (well-posedness, Dirichlet-free)
  double kmin = 1.0, kmax = 10.0, simp_p = 3.0;
  bool use_ramp = false;
  double ramp_g0 = 0.05, ramp_p = 1.0;
  // per-node edge incidence for sparse D_i operators
  std::vector<std::vector<std::pair<int,double>>> adj;  // adj[i] = (j, w0_ij)

  // ---- material interpolation ----
  double kval(double g) const {
    if (!use_ramp) {
      double gg = std::max(g, 1e-9);
      return kmin + (kmax - kmin) * std::pow(gg, simp_p);
    }
    // RAMP: 1/k = 1/kmin - (1/kmin - 1/kmax) * g^{p+1}/(g^p + g0)
    double gg = std::max(g, 1e-12), p = ramp_p, g0 = ramp_g0;
    double fp = std::pow(gg, p);
    double f = std::pow(gg, p + 1) / (fp + g0);
    double inv = 1.0/kmin - (1.0/kmin - 1.0/kmax) * f;
    return 1.0 / inv;
  }
  double kval_der(double g) const {
    if (!use_ramp) {
      double gg = std::max(g, 1e-9);
      return (kmax - kmin) * simp_p * std::pow(gg, simp_p - 1.0);
    }
    double gg = std::max(g, 1e-12), p = ramp_p, g0 = ramp_g0;
    double fp = std::pow(gg, p);
    double den = fp + g0;
    double f = std::pow(gg, p + 1) / den;
    double fder = std::pow(gg, p) * (fp + (p + 1.0) * g0) / (den * den);
    double inv = 1.0/kmin - (1.0/kmin - 1.0/kmax) * f;
    double k = 1.0 / inv;
    return k * k * (1.0/kmin - 1.0/kmax) * fder;
  }
  RVec kfield(const RVec& gbar) const {
    RVec k(n); for (int i = 0; i < n; ++i) k[i] = kval(gbar[i]); return k;
  }

  // ---- graph construction: jittered grid + 7 nearest neighbours ----
  void build(int nx_, int ny_, int seed_ = 7) {
    nx = nx_; ny = ny_; n = nx * ny; seed = seed_;
    px.resize(n); py.resize(n);
    Rng r(seed);
    double h = 1.0 / std::max(nx, ny);
    for (int j = 0; j < ny; ++j)
      for (int i = 0; i < nx; ++i) {
        int idx = j * nx + i;
        double border = (i == 0 || j == 0 || i == nx - 1 || j == ny - 1) ? 0.30 : 0.62;
        px[idx] = (i + 0.5) * h + (r.u() - 0.5) * border * h;
        py[idx] = (j + 0.5) * h + (r.u() - 0.5) * border * h;
      }
    // k nearest neighbours
    const int K = std::min(7, n - 1);
    std::vector<char> has(n * n, 0);
    auto add_edge = [&](int a, int b) {
      if (a > b) std::swap(a, b);
      if (has[a * n + b]) return;
      has[a * n + b] = 1;
      double d = std::hypot(px[b] - px[a], py[b] - py[a]);
      eg.push_back({a, b, d, 0.0});
    };
    for (int a = 0; a < n; ++a) {
      RVec dd(n);
      for (int b = 0; b < n; ++b) dd[b] = std::hypot(px[b] - px[a], py[b] - py[a]);
      std::vector<int> ord(n);
      std::iota(ord.begin(), ord.end(), 0);
      std::partial_sort(ord.begin(), ord.begin() + K, ord.end(),
                        [&](int p, int q) { return dd[p] < dd[q]; });
      int added = 0;
      for (int t = 0; t < n && added < K; ++t) {
        if (ord[t] == a) continue;
        add_edge(a, ord[t]);
        ++added;
      }
    }
    // connectivity repair (union-find)
    std::vector<int> parent(n);
    std::iota(parent.begin(), parent.end(), 0);
    std::function<int(int)> find = [&](int x) {
      while (parent[x] != x) { parent[x] = parent[parent[x]]; x = parent[x]; }
      return x;
    };
    auto unite = [&](int a, int b) { parent[find(a)] = find(b); };
    for (auto& e : eg) unite(e.i, e.j);
    int comp = 0; for (int i = 0; i < n; ++i) if (find(i) == i) ++comp;
    while (comp > 1) {
      double best = 1e300; int ba = -1, bb = -1;
      for (auto& e : eg)
        for (int o = 0; o < n; ++o)
          if (find(e.i) != find(o)) {
            double d = std::hypot(px[o] - px[e.i], py[o] - py[e.i]);
            if (d < best) { best = d; ba = e.i; bb = o; }
          }
      add_edge(ba, bb); unite(ba, bb);
      comp = 0; for (int i = 0; i < n; ++i) if (find(i) == i) ++comp;
    }
    // edge weights w0 = l_ij / d_ij (l_ij: face-length proxy)
    davg.assign(n, 0.0);
    for (auto& e : eg) { davg[e.i] += e.d; davg[e.j] += e.d; }
    std::vector<int> deg(n, 0);
    for (auto& e : eg) { deg[e.i]++; deg[e.j]++; }
    for (int i = 0; i < n; ++i) davg[i] /= std::max(1, deg[i]);
    for (auto& e : eg) {
      double l = 0.5 * (davg[e.i] + davg[e.j]);
      e.w0 = l / e.d;
    }
    adj.assign(n, {});
    Aarea.assign(n, 0.0);
    for (auto& e : eg) {
      adj[e.i].push_back({e.j, e.w0});
      adj[e.j].push_back({e.i, e.w0});
      Aarea[e.i] += 0.5 * e.d * e.w0 * e.d;   // ~ cell area proxy (consistently scaled)
      Aarea[e.j] += 0.5 * e.d * e.w0 * e.d;
    }
    double aSum = std::accumulate(Aarea.begin(), Aarea.end(), 0.0);
    for (auto& a : Aarea) a /= aSum;          // normalized: sum Aarea = 1
    // heat source: base + hot spot near left edge
    b0.assign(n, 1.0);
    for (int i = 0; i < n; ++i) {
      double dx = px[i] - 0.18, dy = py[i] - 0.5;
      b0[i] += 4.0 * std::exp(-(dx*dx + dy*dy) / (2 * 0.14 * 0.14));
    }
  }

  double volume(const RVec& gbar) const {
    return dot(Aarea, gbar);
  }

  // graph Laplacian (Neumann, k = 1)
  RMat laplacian() const {
    RMat L(n, RVec(n, 0.0));
    for (auto& e : eg) { L[e.i][e.j] -= e.w0; L[e.j][e.i] -= e.w0; }
    for (auto& e : eg) { L[e.i][e.i] += e.w0; L[e.j][e.j] += e.w0; }
    return L;
  }
  // Helmholtz filter matrix: I + (rmin^2/12) * L
  RMat helm_mat(double rmin) const {
    RMat H = laplacian();
    double c = rmin * rmin / 12.0;
    for (int i = 0; i < n; ++i)
      for (int j = 0; j < n; ++j) H[i][j] *= c;
    for (int i = 0; i < n; ++i) H[i][i] += 1.0;
    return H;
  }
  // conduction matrix: K_ij = w0_ij (k_i + k_j)/2,  K_ii = sum_{j} K_ij
  RMat cond_mat(const RVec& gbar) const {
    RVec k = kfield(gbar);
    RMat A(n, RVec(n, 0.0));
    for (auto& e : eg) {
      double kbar = 0.5 * (k[e.i] + k[e.j]);
      double K = e.w0 * kbar;
      A[e.i][e.j] += -K; A[e.j][e.i] += -K;
      A[e.i][e.i] += K;  A[e.j][e.j] += K;
    }
    for (int i = 0; i < n; ++i) A[i][i] += hconv;
    return A;
  }
  // flux at each node: f_i = sum_{j in N(i)} K_ij (T_i - T_j)
  RVec fluxes(const RVec& gbar, const RVec& T) const {
    RVec k = kfield(gbar);
    RVec f(n, 0.0);
    for (auto& e : eg) {
      double K = e.w0 * 0.5 * (k[e.i] + k[e.j]);
      f[e.i] += K * (T[e.i] - T[e.j]);
      f[e.j] += K * (T[e.j] - T[e.i]);
    }
    return f;
  }
};

// ---------------------------------------------------------------------------
// Chain pieces (templated for complex-step verification)
// ---------------------------------------------------------------------------

// Helmholtz PDE filter solve: (I + c L) g~ = g
template <typename T>
void helm_solve(const Model& M, double rmin, const std::vector<T>& gamma, std::vector<T>& gtilde) {
  Mat<T> H(M.n, std::vector<T>(M.n, T(0.0)));
  double c = rmin * rmin / 12.0;
  for (int i = 0; i < M.n; ++i) H[i][i] = T(1.0);
  for (auto& e : M.eg) {
    H[e.i][e.j] -= T(c * e.w0);
    H[e.j][e.i] -= T(c * e.w0);
    H[e.i][e.i] += T(c * e.w0);
    H[e.j][e.j] += T(c * e.w0);
  }
  DenseLU<T> lu; lu.factor(H);
  lu.solve(gamma, gtilde);
}

// volume-preserving tanh-Heaviside:
// gbar_i = [tanh(b e) + tanh(b (g~_i - e))] / [tanh(b e) + tanh(b (1-e))]
template <typename T>
void heaviside(const std::vector<T>& gtilde, double betaH, double eta, std::vector<T>& gbar) {
  int n = (int)gtilde.size();
  gbar.resize(n);
  // D = tanh(beta*eta) + tanh(beta*(1-eta)) is the analytic denominator;
  // tanh is entire, so evaluating at complex arguments is branch-safe for the
  // practical continuation range beta <= 64.
  T num0 = std::tanh(T(betaH) * T(eta));
  T den  = num0 + std::tanh(T(betaH) * T(1.0 - eta));
  for (int i = 0; i < n; ++i)
    gbar[i] = (num0 + std::tanh(T(betaH) * (gtilde[i] - T(eta)))) / den;
}
template <typename T>
RVec heaviside_der(const std::vector<T>& gtilde, double betaH, double eta, int nreal = -1) {
  int n = nreal > 0 ? nreal : (int)gtilde.size();
  RVec d(n);
  double D = std::tanh(betaH * eta) + std::tanh(betaH * (1.0 - eta));
  for (int i = 0; i < n; ++i) {
    double a = betaH * (std::real(gtilde[i]) - eta);
    double ch = std::cosh(a);
    d[i] = betaH / (ch * ch) / D;
  }
  return d;
}

// shifted KS aggregation (shift keeps exp() overflow-free)
template <typename T>
T ks_agg(const std::vector<T>& v, double ref, double rho) {
  double mx = ref;
  for (const T& t : v) mx = std::max(mx, std::real(t));
  T sum(0.0);
  for (const T& t : v) sum += std::exp(T(rho) * (t - T(mx)));
  return T(mx) + T(1.0 / rho) * std::log(sum / T((double)v.size()));
}
// weights dKS/dv_i  (real ref; complex-safe)
template <typename T>
std::vector<T> ks_w(const std::vector<T>& v, double ref, double rho) {
  int n = (int)v.size();
  double mx = ref;
  for (const T& t : v) mx = std::max(mx, std::real(t));
  std::vector<T> w(n);
  T sum(0.0);
  for (int i = 0; i < n; ++i) { w[i] = std::exp(T(rho) * (v[i] - T(mx))); sum += w[i]; }
  for (int i = 0; i < n; ++i) w[i] /= sum;
  return w;
}

// analytic (integer-power) material law, templated for complex step
template <typename T>
T ipow(T x, int p) { T r(1); for (int i = 0; i < p; ++i) r *= x; return r; }
template <typename T>
T kval_t(const Model& M, const T& g) {
  if (!M.use_ramp) {
    T gg = g;
    if constexpr (std::is_same_v<T, double>) gg = std::max(gg, 1e-9);
    return T(M.kmin) + T(M.kmax - M.kmin) * ipow(gg, (int)M.simp_p);
  }
  T gg = g;
  if constexpr (std::is_same_v<T, double>) gg = std::max(gg, 1e-12);
  int p = (int)M.ramp_p;
  T fp = ipow(gg, p);
  T f = (gg * fp) / (fp + T(M.ramp_g0));
  T inv = T(1.0 / M.kmin) - (T(1.0 / M.kmin) - T(1.0 / M.kmax)) * f;
  return T(1.0) / inv;
}
// templated conduction matrix (complex-safe)
template <typename T>
Mat<T> cond_mat_t(const Model& M, const std::vector<T>& gbar) {
  int n = M.n;
  std::vector<T> k(n);
  for (int i = 0; i < n; ++i) k[i] = kval_t<T>(M, gbar[i]);
  Mat<T> A(n, std::vector<T>(n, T(0.0)));
  for (auto& e : M.eg) {
    T K = T(e.w0) * T(0.5) * (k[e.i] + k[e.j]);
    A[e.i][e.j] += -K; A[e.j][e.i] += -K;
    A[e.i][e.i] += K;  A[e.j][e.j] += K;
  }
  for (int i = 0; i < n; ++i) A[i][i] += T(M.hconv);
  return A;
}

// ---------------------------------------------------------------------------
// Full chain: gamma -> Helmholtz -> Heaviside -> SIMP/RAMP -> conduction -> J
//   J = ks_agg(T, Tref, rho)      dJ/dT = rho * w   (w = softmax weights)
//   adjoint: t1 = A^{-T} c_T
//   dJ/dgbar_i = -k'(gbar_i) * t1^T D_i T ,   T = A^{-1} b
//   dJ/dgamma  = A_Helm^{-T} ( h'(g~) . dJ/dgbar )
// ---------------------------------------------------------------------------
template <typename T>
struct Chain {
  const Model& M;
  double betaH, rmin, eta, rho, Tref;
  std::vector<T> gtilde, gbar, Tf, dJ_dgbar, dJ_dgamma;
  T J;
  DenseLU<T> lu_cond, lu_condT, lu_helm;
  Mat<T> A_cond, A_helm;

  Chain(const Model& M_, double betaH_, double rmin_, double eta_, double rho_, double Tref_)
      : M(M_), betaH(betaH_), rmin(rmin_), eta(eta_), rho(rho_), Tref(Tref_) {}

  T forward(const std::vector<T>& gamma) {
    helm_solve<T>(M, rmin, gamma, gtilde);
    heaviside<T>(gtilde, betaH, eta, gbar);
    A_cond = cond_mat_t<T>(M, gbar);
    A_helm = cast_mat(M.helm_mat(rmin));
    lu_cond.factor(A_cond);
    lu_condT.factor(transpose_mat(A_cond));
    lu_helm.factor(transpose_mat(A_helm));
    lu_cond.solve(real_vec(M.b0), Tf);
    J = ks_agg<T>(Tf, Tref, rho);
    compute_grad(gamma);
    return J;
  }

  // casting helpers (static; T is double or cplx)
  static Mat<T> cast_mat(const RMat& A) {
    int n = (int)A.size();
    Mat<T> B(n, std::vector<T>(n));
    for (int i = 0; i < n; ++i) for (int j = 0; j < n; ++j) B[i][j] = T(A[i][j]);
    return B;
  }
  static std::vector<T> real_vec(const RVec& v) {
    std::vector<T> r(v.size());
    for (size_t i = 0; i < v.size(); ++i) r[i] = T(v[i]);
    return r;
  }
  static Mat<T> transpose_mat(const Mat<T>& A) {
    int n = (int)A.size();
    Mat<T> B(n, std::vector<T>(n));
    for (int i = 0; i < n; ++i) for (int j = 0; j < n; ++j) B[i][j] = A[j][i];
    return B;
  }

  void compute_grad(const std::vector<T>& gamma) {
    int n = M.n;
    (void)gamma;
    // dJ/dT (KS weights, complex-safe)
    auto w = ks_w<T>(Tf, Tref, rho);
    std::vector<T> c_T(n);
    for (int i = 0; i < n; ++i) c_T[i] = w[i];  // dKS/dT_i = w_i (no extra rho)
    // adjoint t1 = A^{-T} c_T
    std::vector<T> t1;
    lu_condT.solve(c_T, t1);
    // per-node dJ/dgbar_i = -k'(gbar_i) * sum_{(i,j) in E} (w0/2)(t1_i + t1_j)(T_i - T_j)
    RVec kb(n);
    for (int i = 0; i < n; ++i) kb[i] = std::real(gbar[i]);
    dJ_dgbar.resize(n);
    for (int i = 0; i < n; ++i) {
      double s = 0.0;
      for (auto& pr : M.adj[i]) {
        int j = pr.first; double w0 = pr.second;
        s += (w0 * 0.5 * M.kval_der(kb[i]))
           * (std::real(t1[i]) - std::real(t1[j])) * (std::real(Tf[i]) - std::real(Tf[j]));
      }
      dJ_dgbar[i] = T(-s);
    }
    // Heaviside VJP
    auto h = heaviside_der<T>(gtilde, betaH, eta, n);
    std::vector<T> s(n);
    for (int i = 0; i < n; ++i) s[i] = T(h[i]) * dJ_dgbar[i];
    // Helmholtz VJP: dJ/dgamma = A_Helm^{-T} s
    lu_helm.solve(s, dJ_dgamma);
  }
};

// Forward-only chain evaluation (no VJP) — used for complex-step and
// central finite-difference verification of the backward chain.
template <typename T>
T chain_forward_only(const Model& M, const std::vector<T>& gamma,
                     double betaH, double rmin, double eta, double rho,
                     double Tref, std::vector<T>* T_out = nullptr) {
  int n = M.n;
  std::vector<T> gtilde, gbar, Tf;
  helm_solve<T>(M, rmin, gamma, gtilde);
  heaviside<T>(gtilde, betaH, eta, gbar);
  Mat<T> A = cond_mat_t<T>(M, gbar);
  DenseLU<T> lu;
  lu.factor(A);
  std::vector<T> b(n);
  for (int i = 0; i < n; ++i) b[i] = T(M.b0[i]);
  lu.solve(b, Tf);
  if (T_out) *T_out = Tf;
  return ks_agg<T>(Tf, Tref, rho);
}

}  // namespace mo
