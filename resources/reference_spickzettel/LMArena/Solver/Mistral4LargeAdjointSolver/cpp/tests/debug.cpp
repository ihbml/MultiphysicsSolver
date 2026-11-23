#include <cstdio>
#include <vector>
#include <cmath>
#include "../include/fvm/core.hpp"
#include "../include/linalg/krylov.hpp"
#include "../include/linalg/amg.hpp"
#include "../include/chem/chemistry.hpp"
using namespace fvm;

int main() {
  // debug checkerboard gradients
  {
    Lake1D lk; lk.build(8, 1.0, 0.0);
    const double c = 0.7;
    for (int i = 0; i < lk.L.n_cells; ++i) lk.p[i] = c * ((i % 2) ? -1.0 : 1.0);
    printf("vol[0]=%.6f cx:", lk.vol[0]);
    for (int i = 0; i < 4; ++i) printf(" %.4f", lk.cx[i]);
    printf("\nfx:");
    for (int f = 0; f < 5; ++f) printf(" %.4f", lk.fx[f]);
    printf("\nowner/nb:");
    for (int f = 0; f < lk.L.n_faces; ++f) printf(" (%d,%d)", lk.owner[f], lk.neighbour[f]);
    printf("\ngrad:");
    for (int i = 0; i < lk.L.n_cells; ++i) {
      auto g = grad_gg(lk.L, lk.p.data(), i);
      printf(" %.4f", g.x);
    }
    printf("\ntheta:");
    for (int f = 0; f < lk.L.n_faces; ++f) printf(" %.3f", theta_face(lk.L, f));
    printf("\n");
  }
  // debug AMG build timing
  {
    const int N = 48;
    const int n = N * N;
    std::vector<std::vector<std::pair<int, double>>> rows(n);
    auto id = [&](int i, int j) { return i * N + j; };
    for (int i = 0; i < N; ++i)
      for (int j = 0; j < N; ++j) {
        rows[id(i, j)].push_back({id(i, j), 4.0});
        if (i > 0) rows[id(i, j)].push_back({id(i - 1, j), -1.0});
        if (i < N - 1) rows[id(i, j)].push_back({id(i + 1, j), -1.0});
        if (j > 0) rows[id(i, j)].push_back({id(i, j - 1), -1.0});
        if (j < N - 1) rows[id(i, j)].push_back({id(i, j + 1), -1.0});
      }
    linalg::CSR A = linalg::CSR::from_rows(n, rows);
    printf("poisson built n=%d nnz=%zu\n", A.n, A.val.size());
    fflush(stdout);
    {
      auto S = amg::strong_neighbours(A, 0.25);
      int c0 = 24 * N + 24;
      printf("S[%d] size=%zu:", c0, S[c0].size());
      for (int j : S[c0]) printf(" %d", j);
      printf("\nrow %d:", c0);
      for (int k = A.ptr[c0]; k < A.ptr[c0 + 1]; ++k) printf(" (%d,%.1f)", A.col[k], A.val[k]);
      printf("\n");
      fflush(stdout);
    }
    auto agg = amg::aggregate(A, 0.25);
    int nc = *std::max_element(agg.begin(), agg.end()) + 1;
    printf("aggregation: nc=%d\n", nc);
    {
      std::vector<int> hist(10, 0);
      std::vector<int> cnt(nc, 0);
      for (int a : agg) cnt[a]++;
      for (int c : cnt) hist[std::min(9, c)]++;
      printf("agg size histogram (1..9+):");
      for (int i = 0; i < 10; ++i) printf(" %d:%d", i == 9 ? 9 : i + 1, hist[i]);
      printf("\n");
      fflush(stdout);
    }
    fflush(stdout);
    auto P = amg::smooth_prolongation(A, agg, nc, true);
    printf("P built\n"); fflush(stdout);
    auto Ac = amg::galerkin_P(A, P, nc);
    printf("galerkin: nc=%d nnz=%zu\n", Ac.n, Ac.val.size()); fflush(stdout);
    amg::Hierarchy H;
    H.build(A, 64, 0.25, true);
    printf("hierarchy levels=%d:", H.n_levels());
    for (auto& lv : H.levels) printf(" %d", lv.n);
    printf("\n"); fflush(stdout);
    std::vector<double> b(n, 1.0), x(n, 0.0);
    auto M = [&](const std::vector<double>& r, std::vector<double>& z) { H.apply<float>(r, z); };
    auto lg = linalg::pcg(A, b, x, M, 1e-12, 300);
    printf("pcg iters=%d conv=%d final=%.3e\n", lg.iters, (int)lg.converged, lg.relres.back());
    std::vector<double> xr(n, 0.0);
    auto lr = linalg::iterative_refinement_fp32(A, b, xr, M, 1e-10, 30);
    printf("refine iters=%d conv=%d hist:", lr.iters, (int)lr.converged);
    for (double v : lr.relres) printf(" %.2e", v);
    printf("\n");
  }
  // debug chemistry Jacobian
  {
    chem::Mechanism mech;
    mech.ns = 3; mech.nr = 2;
    chem::Reaction r1, r2;
    r1.nuR = {1,0,0}; r1.nuP = {0,1,0}; r1.A = 5.0e1;
    r2.nuR = {0,1,0}; r2.nuP = {0,0,1}; r2.A = 3.0e7;
    mech.rx = {r1, r2};
    std::vector<double> Y = {0.4, 0.35, 0.25}, J;
    mech.jacobian_dense(Y.data(), 1000.0, J);
    printf("J analytic:\n");
    for (int i = 0; i < 3; ++i) { for (int j = 0; j < 3; ++j) printf(" %12.4e", J[i*3+j]); printf("\n"); }
    const double h = 1e-7;
    for (int j = 0; j < 3; ++j) {
      std::vector<double> Yp = Y, Ym = Y, wp(3), wm(3);
      Yp[j] += h; Ym[j] -= h;
      mech.omega(Yp.data(), 1000.0, wp.data());
      mech.omega(Ym.data(), 1000.0, wm.data());
      printf("FD col %d:", j);
      for (int k = 0; k < 3; ++k) printf(" %12.4e", (wp[k]-wm[k])/(2*h));
      printf("\n");
    }
  }
  // debug adjoint
  {
    const int N = 8;
    Lake1D lk; lk.build(N, 1.0, 2.0);
    for (int i = 0; i < N; ++i) lk.gamma[i] = 0.3 + 0.07 * i;
    std::vector<double> q(N);
    for (int i = 0; i < N; ++i) q[i] = 0.1 + 0.05 * i;
    auto assemble = [&](std::vector<double>& gamma) {
      std::vector<std::vector<std::pair<int,double>>> rows(N);
      std::vector<double> b = q;
      for (int f = 0; f < lk.L.n_faces; ++f) {
        const int P = lk.owner[f], Nb = lk.neighbour[f];
        const double al = alpha_face(lk.L, f);
        if (Nb >= 0) {
          const double gf = 0.5 * (gamma[P] + gamma[Nb]);
          const double c = al * gf;
          rows[P].push_back({P, c}); rows[P].push_back({Nb, -c});
          rows[Nb].push_back({Nb, c}); rows[Nb].push_back({P, -c});
        } else {
          rows[P].push_back({P, al * gamma[P]});
        }
      }
      return std::make_pair(linalg::CSR::from_rows(N, rows), b);
    };
    auto [A0, b0] = assemble(lk.gamma);
    std::vector<double> u(N, 0.0);
    auto Id = [](const std::vector<double>& r, std::vector<double>& z) { z = r; };
    linalg::pcg(A0, b0, u, Id, 1e-15, 1000);
    std::vector<double> lam(N, 0.0), rhs(N);
    for (int i = 0; i < N; ++i) rhs[i] = -u[i];
    linalg::pcg(A0, rhs, lam, Id, 1e-15, 1000);
    printf("u:"); for (double v : u) printf(" %.6f", v); printf("\n");
    printf("lam:"); for (double v : lam) printf(" %.6f", v); printf("\n");
    auto obj = [&](const std::vector<double>& uu) { double s = 0; for (double v : uu) s += 0.5*v*v; return s; };
    std::vector<double> dJ(N, 0.0);
    for (int f = 0; f < lk.L.n_faces; ++f) {
      const int P = lk.owner[f], Nb = lk.neighbour[f];
      const double al = alpha_face(lk.L, f);
      if (Nb >= 0) {
        const double contrib = 0.5 * al * (lam[P] - lam[Nb]) * (u[P] - u[Nb]);
        dJ[P] += contrib; dJ[Nb] += contrib;
      } else {
        dJ[P] += al * lam[P] * u[P];
      }
    }
    for (int k = 0; k < N; ++k) {
      const double h = 1e-6;
      std::vector<double> gp = lk.gamma, gm = lk.gamma;
      gp[k] += h; gm[k] -= h;
      auto [Ap, bp] = assemble(gp);
      auto [Am, bm] = assemble(gm);
      std::vector<double> up(N,0), um(N,0);
      linalg::pcg(Ap, bp, up, Id, 1e-15, 1000);
      linalg::pcg(Am, bm, um, Id, 1e-15, 1000);
      double fd = (obj(up) - obj(um)) / (2*h);
      printf("k=%d adj=%.10e fd=%.10e err=%.3e\n", k, dJ[k], fd, std::fabs(dJ[k]-fd));
    }
    // geometry: dJ/dx_f right boundary face
    {
      const int f = lk.L.n_faces - 1;
      const int P = lk.owner[f];
      const double d0 = lk.fx[f] - lk.cx[P];
      const double dalpha = -1.0 / (2.0 * d0 * d0);
      double adj = lam[P] * lk.gamma[P] * u[P] * dalpha;
      auto J_at = [&](double xf) {
        double save = lk.fx[f];
        lk.fx[f] = xf;
        auto [Ax, bx] = assemble(lk.gamma);
        std::vector<double> ux(N, 0.0);
        linalg::pcg(Ax, bx, ux, Id, 1e-15, 1000);
        lk.fx[f] = save;
        return obj(ux);
      };
      const double h = 1e-6;
      double fd = (J_at(lk.fx[f] + h) - J_at(lk.fx[f] - h)) / (2*h);
      printf("dJ/dx_f: adj=%.10e fd=%.10e  al_b=%.6f d0=%.6f\n", adj, fd, alpha_face(lk.L, f), d0);
    }
  }
  return 0;
}
