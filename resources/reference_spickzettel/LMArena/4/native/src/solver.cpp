#include "engine.hpp"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <string>

#ifdef _OPENMP
#include <omp.h>
#endif

namespace fvmspm {
namespace {

struct Fields {
  std::vector<double> rho, u, v, p;
  int n = 0;
  void init(int size) {
    n = size;
    rho.assign(size, 1.0);
    u.assign(size, 0.0);
    v.assign(size, 0.0);
    p.assign(size, 1.0);
  }
};

inline double soundSpeed(double rho, double p, double gamma) {
  return std::sqrt(std::max(1e-12, gamma * p / rho));
}


enum BcType { BC_WALL = 0, BC_INLET = 1, BC_OUTLET = 2, BC_FARFIELD = 3 };

struct BcConfig {
  int left = BC_INLET, right = BC_OUTLET, top = BC_WALL, bottom = BC_WALL;
};

BcType parseBc(const std::string &s) {
  if (s == "inlet" || s == "farfield") return s == "inlet" ? BC_INLET : BC_FARFIELD;
  if (s == "outlet" || s == "transmissive") return BC_OUTLET;
  return BC_WALL;
}

struct BcFace {
  int type;  // index into config arrays
  double nx, ny, cx, cy, len;
};

double primitiveGoal(const Mesh &mesh, const Fields &f, const Config &cfg,
                     const std::vector<double> &faceP, double *dragOut, double *liftOut) {
  double drag = 0.0, lift = 0.0;
  for (size_t fi = 0; fi < mesh.faces.size(); ++fi) {
    const auto &face = mesh.faces[fi];
    if (face.nbr >= 0 || face.bc != 2) continue;
    const double pw = faceP[fi];
    drag += pw * face.nx * face.len;
    lift += pw * face.ny * face.len;
  }
  double ke = 0.0;
  for (int i = 0; i < f.n; ++i)
    ke += 0.5 * f.rho[static_cast<size_t>(i)] * (f.u[static_cast<size_t>(i)] * f.u[static_cast<size_t>(i)] +
                                                 f.v[static_cast<size_t>(i)] * f.v[static_cast<size_t>(i)]) *
          mesh.cells[static_cast<size_t>(i)].area;
  if (dragOut) *dragOut = drag;
  if (liftOut) *liftOut = lift;
  return drag * (cfg.obstacles.empty() ? 0.0 : 1.0) + ke;
}

void initFields(const Config &cfg, const Mesh &mesh, Fields &f) {
  f.init(static_cast<int>(mesh.cells.size()));
  const double g = cfg.gamma;
  const double a0 = soundSpeed(cfg.rho0, cfg.p0, g);
  const double uin = cfg.mach * a0 * std::cos(cfg.aoa);
  const double vin = cfg.mach * a0 * std::sin(cfg.aoa);
  const double xmid = 0.5 * (cfg.xmin + cfg.xmax);

  for (int i = 0; i < f.n; ++i) {
    const double x = mesh.cells[static_cast<size_t>(i)].cx;
    const double y = mesh.cells[static_cast<size_t>(i)].cy;
    size_t k = static_cast<size_t>(i);
    if (cfg.ic == "sod" || cfg.ic == "shock_tube") {
      if (x < xmid) {
        f.rho[k] = 1.0;
        f.p[k] = 1.0;
      } else {
        f.rho[k] = 0.125;
        f.p[k] = 0.1;
      }
      f.u[k] = 0.0;
      f.v[k] = 0.0;
    } else if (cfg.ic == "explosion") {
      const double r = std::hypot(x - xmid, y - 0.5 * (cfg.ymin + cfg.ymax));
      f.rho[k] = r < 0.08 ? 1.0 : 0.15;
      f.p[k] = r < 0.08 ? 10.0 : 0.2;
      f.u[k] = 0.0;
      f.v[k] = 0.0;
    } else if (cfg.ic == "vortex") {
      const double dx = x - xmid, dy = y - 0.5 * (cfg.ymin + cfg.ymax);
      const double beta = 5.0;
      const double r2 = dx * dx + dy * dy;
      const double uu = uin - beta * dy / (2.0 * M_PI) * std::exp(0.5 * (1.0 - r2));
      const double vv = vin + beta * dx / (2.0 * M_PI) * std::exp(0.5 * (1.0 - r2));
      const double T = 1.0 - (g - 1.0) * beta * beta / (8.0 * g * M_PI * M_PI) * std::exp(1.0 - r2);
      f.u[k] = uu;
      f.v[k] = vv;
      f.rho[k] = std::pow(std::max(1e-6, T), 1.0 / (g - 1.0));
      f.p[k] = std::pow(std::max(1e-6, T), g / (g - 1.0));
    } else {  // shock_obstacle / uniform freestream
      f.rho[k] = cfg.rho0;
      f.p[k] = cfg.p0;
      f.u[k] = uin;
      f.v[k] = vin;
    }
  }
}

// HLLC approximate Riemann solver, normal n points from L to R.
inline void hllcFlux(const double rhoL, const double uL, const double vL, const double pL,
                     const double rhoR, const double uR, const double vR, const double pR,
                     double nx, double ny, double gamma, double *flux) {
  const double unL = uL * nx + vL * ny;
  const double unR = uR * nx + vR * ny;
  const double aL = soundSpeed(rhoL, pL, gamma);
  const double aR = soundSpeed(rhoR, pR, gamma);

  const double SL = std::min(unL - aL, unR - aR);
  const double SR = std::max(unL + aL, unR + aR);

  const double EL = pL / (gamma - 1.0) + 0.5 * rhoL * (uL * uL + vL * vL);
  const double ER = pR / (gamma - 1.0) + 0.5 * rhoR * (uR * uR + vR * vR);

  auto physical = [&](double rho, double u, double v, double p, double un, double E, double *F) {
    F[0] = rho * un;
    F[1] = rho * u * un + p * nx;
    F[2] = rho * v * un + p * ny;
    F[3] = un * (E + p);
  };

  double FL[4], FR[4];
  physical(rhoL, uL, vL, pL, unL, EL, FL);
  physical(rhoR, uR, vR, pR, unR, ER, FR);

  if (SL >= 0.0) {
    for (int c = 0; c < 4; ++c) flux[c] = FL[c];
    return;
  }
  if (SR <= 0.0) {
    for (int c = 0; c < 4; ++c) flux[c] = FR[c];
    return;
  }
  const double denom = rhoL * (SL - unL) - rhoR * (SR - unR);
  const double SM = (std::fabs(denom) < 1e-14)
                        ? 0.5 * (unL + unR)
                        : (pR - pL + rhoL * unL * (SL - unL) - rhoR * unR * (SR - unR)) / denom;

  const double dSL = std::fabs(SL - unL) < 1e-12 ? 1e-12 : (SL - unL);
  const double dSR = std::fabs(SR - unR) < 1e-12 ? 1e-12 : (SR - unR);
  auto preserveSign = [](double v) {
    if (std::fabs(v) < 1e-14) return v < 0.0 ? -1e-14 : 1e-14;
    return v;
  };
  double rhoStarL = rhoL * dSL / preserveSign(SL - SM);
  double rhoStarR = rhoR * dSR / preserveSign(SR - SM);
  rhoStarL = std::max(1e-14, rhoStarL);
  rhoStarR = std::max(1e-14, rhoStarR);

  const double pStarL = pL + rhoL * (SL - unL) * (SM - unL);
  const double pStarR = pR + rhoR * (SR - unR) * (SM - unR);

  const double ELstar = rhoStarL *
      (EL / std::max(1e-14, rhoL) + (SM - unL) * (SM + pL / (rhoL * dSL)));
  const double ERstar = rhoStarR *
      (ER / std::max(1e-14, rhoR) + (SM - unR) * (SM + pR / (rhoR * dSR)));

  const double UL[4] = {rhoL, rhoL * uL, rhoL * vL, EL};
  const double UR[4] = {rhoR, rhoR * uR, rhoR * vR, ER};
  const double UstarL[4] = {rhoStarL, rhoStarL * (uL + (SM - unL) * nx),
                            rhoStarL * (vL + (SM - unL) * ny), ELstar};
  const double UstarR[4] = {rhoStarR, rhoStarR * (uR + (SM - unR) * nx),
                            rhoStarR * (vR + (SM - unR) * ny), ERstar};

  if (SM >= 0.0) {
    for (int c = 0; c < 4; ++c) flux[c] = FL[c] + SL * (UstarL[c] - UL[c]);
  } else {
    for (int c = 0; c < 4; ++c) flux[c] = FR[c] + SR * (UstarR[c] - UR[c]);
  }
  (void)pStarL;
  (void)pStarR;
}

struct RunSummary {
  Fields final;
  double drag = 0.0, lift = 0.0, mass = 0.0, energy = 0.0;
  double wallDefMean = 0.0, wallDefMax = 0.0;
  double avgDt = 0.0, time = 0.0;
  double wallMs = 0.0;
  int snapshotsCount = 0;
  std::vector<std::vector<double>> surface;
};

void classifyFaces(const Config &cfg, const Mesh &mesh, std::vector<BcType> &bcs) {
  const double ext = std::max(cfg.xmax - cfg.xmin, cfg.ymax - cfg.ymin);
  const BcConfig sides{parseBc(cfg.bcLeft), parseBc(cfg.bcRight), parseBc(cfg.bcTop),
                       parseBc(cfg.bcBottom)};
  bcs.assign(mesh.faces.size(), BC_WALL);
  for (size_t fi = 0; fi < mesh.faces.size(); ++fi) {
    const auto &f = mesh.faces[fi];
    if (f.nbr >= 0) continue;
    if (f.bc == 2) {
      bcs[fi] = BC_WALL;
      continue;
    }
    const double dxmin = std::fabs(f.cx - cfg.xmin);
    const double dxmax = std::fabs(f.cx - cfg.xmax);
    const double dymin = std::fabs(f.cy - cfg.ymin);
    const double dymax = std::fabs(f.cy - cfg.ymax);
    const double m = std::min(std::min(dxmin, dxmax), std::min(dymin, dymax));
    if (m > 1e-6 * ext) {
      bcs[fi] = BC_WALL;
    } else if (m == dxmin) {
      bcs[fi] = static_cast<BcType>(sides.left);
    } else if (m == dxmax) {
      bcs[fi] = static_cast<BcType>(sides.right);
    } else if (m == dymin) {
      bcs[fi] = static_cast<BcType>(sides.bottom);
    } else {
      bcs[fi] = static_cast<BcType>(sides.top);
    }
  }
}

// Vectorised graph kernel (DiFVM style):
//   pass 1  -> limited primitive gradients (Green-Gauss + Barth-Jespersen)
//   pass 2  -> HLLC numerical flux per face, stored in a face-contiguous array
//   pass 3  -> gather/scatter the face fluxes onto cells through the CSR graph
// None of the three passes has a loop-carried dependency, so the whole
// operator is OpenMP parallel and auto-vectorisable.
void assembleResidual(const Config &cfg, const Mesh &mesh, const Fields &f,
                      const std::vector<BcType> &bcs, std::vector<double> &res,
                      std::vector<double> &faceP, double &maxMach, double &drag,
                      double &lift, double &dtMin, double *wallP) {
  const int nc = f.n;
  const int nf = static_cast<int>(mesh.faces.size());
  const double gamma = cfg.gamma;
  res.assign(static_cast<size_t>(nc) * 4, 0.0);
  faceP.assign(static_cast<size_t>(nf), 0.0);
  std::vector<double> faceFlux(static_cast<size_t>(nf) * 4, 0.0);
  const bool secondOrder = cfg.order >= 2;
  const bool sphClosure = cfg.wallClosure == "sph";

  // ---- pass 1: limited gradient of (rho, u, v, p) per cell ----
  std::vector<double> grad(static_cast<size_t>(nc) * 8, 0.0);
  if (secondOrder) {
#pragma omp parallel for schedule(static)
    for (int i = 0; i < nc; ++i) {
      const auto &cell = mesh.cells[static_cast<size_t>(i)];
      double gx[4] = {0, 0, 0, 0}, gy[4] = {0, 0, 0, 0};
      const double prim[4] = {f.rho[static_cast<size_t>(i)], f.u[static_cast<size_t>(i)],
                              f.v[static_cast<size_t>(i)], f.p[static_cast<size_t>(i)]};
      for (int k = mesh.offsets[static_cast<size_t>(i)]; k < mesh.offsets[static_cast<size_t>(i) + 1]; ++k) {
        const auto &face = mesh.faces[static_cast<size_t>(mesh.indices[static_cast<size_t>(k)])];
        const int j = (face.owner == i) ? face.nbr : face.owner;
        const double sgn = (face.owner == i) ? 1.0 : -1.0;
        for (int c = 0; c < 4; ++c) {
          double value = prim[c];
          if (j >= 0) {
            const double other = (c == 0 ? f.rho[static_cast<size_t>(j)]
                                         : c == 1 ? f.u[static_cast<size_t>(j)]
                                         : c == 2 ? f.v[static_cast<size_t>(j)] : f.p[static_cast<size_t>(j)]);
            value = 0.5 * (value + other);
          }
          gx[c] += sgn * face.len * value * face.nx;
          gy[c] += sgn * face.len * value * face.ny;
        }
      }
      const double invA = cell.area > 1e-30 ? 1.0 / cell.area : 0.0;
      double lim = 1.0;
      for (int k = mesh.offsets[static_cast<size_t>(i)]; k < mesh.offsets[static_cast<size_t>(i) + 1]; ++k) {
        const auto &face = mesh.faces[static_cast<size_t>(mesh.indices[static_cast<size_t>(k)])];
        const int j = (face.owner == i) ? face.nbr : face.owner;
        const double dx = (face.cx - cell.cx) * invA;
        const double dy = (face.cy - cell.cy) * invA;
        for (int c = 0; c < 4; ++c) {
          const double dphi = gx[c] * dx + gy[c] * dy;
          if (std::fabs(dphi) < 1e-14) continue;
          const double vi = prim[c];
          const double vj = (j >= 0 ? (c == 0 ? f.rho[static_cast<size_t>(j)]
                                              : c == 1 ? f.u[static_cast<size_t>(j)]
                                              : c == 2 ? f.v[static_cast<size_t>(j)] : f.p[static_cast<size_t>(j)])
                                    : vi);
          const double lo = std::min(vi, vj), hi = std::max(vi, vj);
          const double ratio = dphi > 0.0 ? (hi - vi) / dphi : (lo - vi) / dphi;
          lim = std::min(lim, ratio);
        }
      }
      lim = std::clamp(lim, 0.0, 1.0);
      const size_t b = static_cast<size_t>(i) * 8;
      grad[b + 0] = gx[0] * invA * lim;
      grad[b + 1] = gy[0] * invA * lim;
      grad[b + 2] = gx[1] * invA * lim;
      grad[b + 3] = gy[1] * invA * lim;
      grad[b + 4] = gx[2] * invA * lim;
      grad[b + 5] = gy[2] * invA * lim;
      grad[b + 6] = gx[3] * invA * lim;
      grad[b + 7] = gy[3] * invA * lim;
    }
  }

  // ---- pass 1b: least-squares gradients used by the wall closure ----
  // For boundary cells the wall normal gradient is extrapolated across the
  // mirror point; the extra virtual-particle support requested by the
  // Eulerian-SPH consistency condition enters through (1 + deficiency).
  std::vector<double> bgrad(static_cast<size_t>(nc) * 4, 0.0);
  {
#pragma omp parallel for schedule(static)
    for (int i = 0; i < nc; ++i) {
      bool hasWall = false;
      for (int k = mesh.offsets[static_cast<size_t>(i)]; k < mesh.offsets[static_cast<size_t>(i) + 1]; ++k) {
        const auto &face = mesh.faces[static_cast<size_t>(mesh.indices[static_cast<size_t>(k)])];
        if ((face.owner == i ? face.nbr : face.owner) < 0) {
          hasWall = true;
          break;
        }
      }
      if (!hasWall) continue;
      double a11 = 0, a12 = 0, a22 = 0, br1 = 0, br2 = 0, bp1 = 0, bp2 = 0;
      const auto &ci = mesh.cells[static_cast<size_t>(i)];
      for (int k = mesh.offsets[static_cast<size_t>(i)]; k < mesh.offsets[static_cast<size_t>(i) + 1]; ++k) {
        const auto &face = mesh.faces[static_cast<size_t>(mesh.indices[static_cast<size_t>(k)])];
        const int j = (face.owner == i) ? face.nbr : face.owner;
        if (j < 0) continue;
        const auto &cj = mesh.cells[static_cast<size_t>(j)];
        const double dx = cj.cx - ci.cx, dy = cj.cy - ci.cy;
        const double w = 1.0 / std::max(1e-12, dx * dx + dy * dy);
        a11 += w * dx * dx;
        a12 += w * dx * dy;
        a22 += w * dy * dy;
        const double drho = f.rho[static_cast<size_t>(j)] - f.rho[static_cast<size_t>(i)];
        const double dp = f.p[static_cast<size_t>(j)] - f.p[static_cast<size_t>(i)];
        br1 += w * dx * drho;
        br2 += w * dy * drho;
        bp1 += w * dx * dp;
        bp2 += w * dy * dp;
      }
      const double det = a11 * a22 - a12 * a12;
      if (std::fabs(det) < 1e-30) continue;
      const double inv = 1.0 / det;
      const size_t b = static_cast<size_t>(i) * 4;
      bgrad[b + 0] = (a22 * br1 - a12 * br2) * inv;
      bgrad[b + 1] = (a11 * br2 - a12 * br1) * inv;
      bgrad[b + 2] = (a22 * bp1 - a12 * bp2) * inv;
      bgrad[b + 3] = (a11 * bp2 - a12 * bp1) * inv;
    }
  }

  const double a0 = soundSpeed(cfg.rho0, cfg.p0, gamma);
  const double uIn = cfg.mach * a0 * std::cos(cfg.aoa);
  const double vIn = cfg.mach * a0 * std::sin(cfg.aoa);
  double localMaxMach = 0.0, localDrag = 0.0, localLift = 0.0;

  // ---- pass 2: face loop (write-only per face: no races) ----
#pragma omp parallel for schedule(static) reduction(max : localMaxMach) reduction(+ : localDrag, localLift)
  for (int fi = 0; fi < nf; ++fi) {
    const auto &face = mesh.faces[fi];
    const int o = face.owner;
    const int nb = face.nbr;
    const double nx = face.nx, ny = face.ny;
    const double rhoO = f.rho[static_cast<size_t>(o)];
    const double uO = f.u[static_cast<size_t>(o)];
    const double vO = f.v[static_cast<size_t>(o)];
    const double pO = f.p[static_cast<size_t>(o)];

    double rhoL = rhoO, uL = uO, vL = vO, pL = pO;
    const double dxO = face.cx - mesh.cells[static_cast<size_t>(o)].cx;
    const double dyO = face.cy - mesh.cells[static_cast<size_t>(o)].cy;
    if (secondOrder) {
      const size_t b = static_cast<size_t>(o) * 8;
      rhoL += grad[b + 0] * dxO + grad[b + 1] * dyO;
      uL += grad[b + 2] * dxO + grad[b + 3] * dyO;
      vL += grad[b + 4] * dxO + grad[b + 5] * dyO;
      pL += grad[b + 6] * dxO + grad[b + 7] * dyO;
      rhoL = std::max(1e-8, rhoL);
      pL = std::max(1e-8, pL);
    }

    double rhoR, uR, vR, pR;
    if (nb >= 0) {
      rhoR = f.rho[static_cast<size_t>(nb)];
      uR = f.u[static_cast<size_t>(nb)];
      vR = f.v[static_cast<size_t>(nb)];
      pR = f.p[static_cast<size_t>(nb)];
      if (secondOrder) {
        const size_t b = static_cast<size_t>(nb) * 8;
        const double dx = face.cx - mesh.cells[static_cast<size_t>(nb)].cx;
        const double dy = face.cy - mesh.cells[static_cast<size_t>(nb)].cy;
        rhoR += grad[b + 0] * dx + grad[b + 1] * dy;
        uR += grad[b + 2] * dx + grad[b + 3] * dy;
        vR += grad[b + 4] * dx + grad[b + 5] * dy;
        pR += grad[b + 6] * dx + grad[b + 7] * dy;
        rhoR = std::max(1e-8, rhoR);
        pR = std::max(1e-8, pR);
      }
    } else {
      const BcType bc = bcs[static_cast<size_t>(fi)];
      rhoR = rhoO;
      pR = pO;
      if (bc == BC_WALL) {
        const double un = uO * nx + vO * ny;
        uR = uO - 2.0 * un * nx;
        vR = vO - 2.0 * un * ny;
        if (sphClosure) {
          // Mirror state + first-order extrapolation across the wall. Cells
          // missing kernel support receive the additional virtual-particle
          // contribution through the deficiency factor (1 + kappa*def).
          const auto &cellO = mesh.cells[static_cast<size_t>(o)];
          const double dist = (cellO.cx - face.cx) * nx + (cellO.cy - face.cy) * ny;
          const double def = cellO.consistency;
          const double scale = 2.0 * dist * cfg.kappa * (1.0 + def);
          const size_t b = static_cast<size_t>(o) * 4;
          rhoR = rhoO + scale * (bgrad[b + 0] * nx + bgrad[b + 1] * ny);
          pR = pO + scale * (bgrad[b + 2] * nx + bgrad[b + 3] * ny);
          rhoR = std::max(1e-8, rhoR);
          pR = std::max(1e-8, pR);
        }
      } else if (bc == BC_INLET || bc == BC_FARFIELD) {
        rhoR = cfg.rho0;
        pR = cfg.p0;
        uR = uIn;
        vR = vIn;
      } else {  // outlet, zero gradient unless backflow
        const double un = uO * nx + vO * ny;
        if (un < 0.0) {
          rhoR = cfg.rho0;
          pR = cfg.p0;
          uR = uIn;
          vR = vIn;
        } else {
          uR = uO;
          vR = vO;
        }
      }
    }

    double flux[4];
    hllcFlux(rhoL, uL, vL, pL, rhoR, uR, vR, pR, nx, ny, gamma, flux);
    for (int c = 0; c < 4; ++c) faceFlux[static_cast<size_t>(fi) * 4 + static_cast<size_t>(c)] = flux[c];

    // Face pressure for the aerodynamic force bookkeeping.
    const double aL = soundSpeed(rhoL, pL, gamma);
    const double aR = soundSpeed(rhoR, pR, gamma);
    const double unL = uL * nx + vL * ny;
    const double unR = uR * nx + vR * ny;
    const double SL = std::min(unL - aL, unR - aR);
    const double SR = std::max(unL + aL, unR + aR);
    double pFace = 0.5 * (pL + pR);
    const double denom = rhoL * (SL - unL) - rhoR * (SR - unR);
    if (std::fabs(denom) > 1e-14) {
      const double SM = (pR - pL + rhoL * unL * (SL - unL) - rhoR * unR * (SR - unR)) / denom;
      pFace = SM >= 0.0 ? pL + rhoL * (SL - unL) * (SM - unL)
                        : pR + rhoR * (SR - unR) * (SM - unR);
    }
    pFace = std::max(0.0, pFace);
    faceP[static_cast<size_t>(fi)] = pFace;
    if (nb < 0 && face.bc == 2) {
      // Pressure acts on the solid along the fluid cell's outward normal.
      localDrag += pFace * nx * face.len;
      localLift += pFace * ny * face.len;
    }

    const double machO = std::sqrt(uO * uO + vO * vO) / std::max(1e-12, soundSpeed(rhoO, pO, gamma));
    const double machR = std::sqrt(uR * uR + vR * vR) / std::max(1e-12, soundSpeed(rhoR, pR, gamma));
    localMaxMach = std::max(localMaxMach, std::max(machO, machR));
  }

  // ---- pass 3: gather/scatter onto the cell graph ----
#pragma omp parallel for schedule(static)
  for (int i = 0; i < nc; ++i) {
    for (int k = mesh.offsets[static_cast<size_t>(i)]; k < mesh.offsets[static_cast<size_t>(i) + 1]; ++k) {
      const int fi = mesh.indices[static_cast<size_t>(k)];
      const auto &face = mesh.faces[static_cast<size_t>(fi)];
      const double sgn = (face.owner == i) ? -1.0 : 1.0;  // outward flux balance
      const double scale = sgn * face.len;
      const size_t fb = static_cast<size_t>(fi) * 4;
      const size_t cb = static_cast<size_t>(i) * 4;
      for (int c = 0; c < 4; ++c) res[cb + static_cast<size_t>(c)] += scale * faceFlux[fb + static_cast<size_t>(c)];
    }
  }

  // ---- CFL time step from the spectral radius of each cell ----
  double localDt = 1e300;
#pragma omp parallel for schedule(static) reduction(min : localDt)
  for (int i = 0; i < nc; ++i) {
    double sum = 0.0;
    const double ci = soundSpeed(f.rho[static_cast<size_t>(i)], f.p[static_cast<size_t>(i)], gamma);
    const double sp = std::sqrt(f.u[static_cast<size_t>(i)] * f.u[static_cast<size_t>(i)] +
                                f.v[static_cast<size_t>(i)] * f.v[static_cast<size_t>(i)]) + ci;
    for (int k = mesh.offsets[static_cast<size_t>(i)]; k < mesh.offsets[static_cast<size_t>(i) + 1]; ++k)
      sum += mesh.faces[static_cast<size_t>(mesh.indices[static_cast<size_t>(k)])].len * sp;
    if (sum > 1e-30) localDt = std::min(localDt, mesh.cells[static_cast<size_t>(i)].area / sum);
  }
  maxMach = localMaxMach;
  drag = localDrag;
  lift = localLift;
  dtMin = localDt;
  if (wallP) *wallP = 0.0;
}

void applyResidual(const Mesh &mesh, const Fields &f, const std::vector<double> &res,
                   double dt, Fields &out, bool mixOld, const Fields &old, double stageWeight,
                   double gamma) {
  const int nc = f.n;
  out.init(nc);
#pragma omp parallel for schedule(static)
  for (int i = 0; i < nc; ++i) {
    const double area = std::max(1e-30, mesh.cells[static_cast<size_t>(i)].area);
    const size_t b = static_cast<size_t>(i) * 4;
    double U[4];
    const double rho = f.rho[static_cast<size_t>(i)];
    const double u = f.u[static_cast<size_t>(i)];
    const double v = f.v[static_cast<size_t>(i)];
    const double p = f.p[static_cast<size_t>(i)];
    U[0] = rho;
    U[1] = rho * u;
    U[2] = rho * v;
    const double g = gamma;
    U[3] = p / (g - 1.0) + 0.5 * rho * (u * u + v * v);
    for (int c = 0; c < 4; ++c) U[c] += dt * res[b + static_cast<size_t>(c)] / area;
    if (mixOld) {
      const double w = stageWeight;
      U[0] = w * old.rho[static_cast<size_t>(i)] + (1.0 - w) * U[0];
      U[1] = w * old.rho[static_cast<size_t>(i)] * old.u[static_cast<size_t>(i)] + (1.0 - w) * U[1];
      U[2] = w * old.rho[static_cast<size_t>(i)] * old.v[static_cast<size_t>(i)] + (1.0 - w) * U[2];
      U[3] = w * (old.p[static_cast<size_t>(i)] / (g - 1.0) +
                  0.5 * old.rho[static_cast<size_t>(i)] *
                      (old.u[static_cast<size_t>(i)] * old.u[static_cast<size_t>(i)] +
                       old.v[static_cast<size_t>(i)] * old.v[static_cast<size_t>(i)])) +
             (1.0 - w) * U[3];
    }
    U[0] = std::max(1e-10, U[0]);
    out.rho[static_cast<size_t>(i)] = U[0];
    out.u[static_cast<size_t>(i)] = U[1] / U[0];
    out.v[static_cast<size_t>(i)] = U[2] / U[0];
    double pNew = (U[3] - 0.5 * (U[1] * U[1] + U[2] * U[2]) / U[0]) * (g - 1.0);
    pNew = std::max(1e-10, std::min(pNew, 1e12));
    out.p[static_cast<size_t>(i)] = pNew;
  }
}

RunSummary runCore(const Config &cfg, const Mesh &mesh, int snapshots,
                   std::vector<Snapshot> &snaps, Fields *initial) {
  RunSummary summary;
  Fields f;
  initFields(cfg, mesh, f);
  if (initial) f = *initial;
  std::vector<BcType> bcs;
  classifyFaces(cfg, mesh, bcs);
  std::vector<double> res, faceP;
  const auto t0 = std::chrono::high_resolution_clock::now();
  const int nc = f.n;
  const int snapshotEvery = snapshots > 0 ? std::max(1, cfg.steps / snapshots) : cfg.steps + 1;
  double time = 0.0, dtSum = 0.0, maxMach = 0.0, drag = 0.0, lift = 0.0;

  auto totalMass = [&]() {
    double m = 0.0;
    for (int i = 0; i < nc; ++i) m += f.rho[static_cast<size_t>(i)] * mesh.cells[static_cast<size_t>(i)].area;
    return m;
  };
  auto totalEnergy = [&]() {
    double e = 0.0;
    for (int i = 0; i < nc; ++i) {
      const double rho = f.rho[static_cast<size_t>(i)];
      const double u = f.u[static_cast<size_t>(i)], v = f.v[static_cast<size_t>(i)];
      e += (f.p[static_cast<size_t>(i)] / (cfg.gamma - 1.0) + 0.5 * rho * (u * u + v * v)) *
           mesh.cells[static_cast<size_t>(i)].area;
    }
    return e;
  };

  auto makeSnapshot = [&](int step, double dt) {
    Snapshot s;
    s.step = step;
    s.time = time;
    s.dt = dt;
    s.rho.resize(static_cast<size_t>(nc));
    s.u.resize(static_cast<size_t>(nc));
    s.v.resize(static_cast<size_t>(nc));
    s.p.resize(static_cast<size_t>(nc));
    s.ma.resize(static_cast<size_t>(nc));
    s.minRho = 1e300;
    s.maxRho = -1e300;
    s.maxMach = 0.0;
    for (int i = 0; i < nc; ++i) {
      const double rho = f.rho[static_cast<size_t>(i)];
      const double u = f.u[static_cast<size_t>(i)], v = f.v[static_cast<size_t>(i)];
      const double p = f.p[static_cast<size_t>(i)];
      s.rho[static_cast<size_t>(i)] = static_cast<float>(rho);
      s.u[static_cast<size_t>(i)] = static_cast<float>(u);
      s.v[static_cast<size_t>(i)] = static_cast<float>(v);
      s.p[static_cast<size_t>(i)] = static_cast<float>(p);
      const double ma = std::sqrt(u * u + v * v) / std::max(1e-12, soundSpeed(rho, p, cfg.gamma));
      s.ma[static_cast<size_t>(i)] = static_cast<float>(ma);
      s.minRho = std::min(s.minRho, rho);
      s.maxRho = std::max(s.maxRho, rho);
      s.maxMach = std::max(s.maxMach, ma);
    }
    s.drag = drag;
    s.lift = lift;
    s.mass = totalMass();
    s.energy = totalEnergy();
    s.momentum = 0.0;
    snaps.push_back(std::move(s));
  };

  double lastDt = 0.0;
  {
    // Prime the CFL estimate so the first step is not wasted.
    std::vector<double> res0, fp0;
    double jd = 0.0, jl = 0.0, jm = 0.0;
    assembleResidual(cfg, mesh, f, bcs, res0, fp0, jm, jd, jl, lastDt, nullptr);
  }
  if (snapshots > 0) makeSnapshot(0, 0.0);
  for (int step = 1; step <= cfg.steps; ++step) {
    double wallDummy = 0.0;
    assembleResidual(cfg, mesh, f, bcs, res, faceP, maxMach, drag, lift, lastDt, &wallDummy);
    const double dt = cfg.cfl * lastDt;
    Fields stage;
    stage.init(f.n);
    applyResidual(mesh, f, res, dt, stage, false, f, 0.0, cfg.gamma);
    std::vector<double> res2, faceP2;
    double dummyDt = 0.0;
    assembleResidual(cfg, mesh, stage, bcs, res2, faceP2, maxMach, drag, lift, dummyDt, &wallDummy);
    Fields mixed;
    applyResidual(mesh, stage, res2, dt, mixed, true, f, 0.5, cfg.gamma);
    f = std::move(mixed);
    time += dt;
    dtSum += dt;
    if (snapshots > 0 && (step % snapshotEvery == 0 || step == cfg.steps)) {
      makeSnapshot(step, dt);
    }
  }
  summary.final = f;
  summary.mass = totalMass();
  summary.energy = totalEnergy();
  summary.drag = drag;
  summary.lift = lift;
  summary.time = time;
  summary.avgDt = cfg.steps > 0 ? dtSum / cfg.steps : 0.0;
  summary.snapshotsCount = static_cast<int>(snaps.size());
  double s = 0.0, mx = 0.0;
  for (int i = 0; i < nc; ++i) {
    const double d = mesh.cells[static_cast<size_t>(i)].consistency;
    s += d;
    mx = std::max(mx, d);
  }
  summary.wallDefMean = nc > 0 ? s / nc : 0.0;
  summary.wallDefMax = mx;
  summary.surface.clear();
  for (size_t fi = 0; fi < mesh.faces.size(); ++fi) {
    const auto &face = mesh.faces[fi];
    if (face.nbr >= 0 || face.bc != 2) continue;
    summary.surface.push_back({face.cx, face.cy, face.nx, face.ny, face.len,
                               faceP.empty() ? 0.0 : faceP[fi]});
  }
  const auto t1 = std::chrono::high_resolution_clock::now();
  summary.wallMs = std::chrono::duration<double, std::milli>(t1 - t0).count();
  return summary;
}

}  // namespace

SolveResult solve(const Config &cfg, const Mesh &mesh) {
  SolveResult out;
  std::vector<Snapshot> snaps;
  const auto t0 = std::chrono::high_resolution_clock::now();
  RunSummary run = runCore(cfg, mesh, cfg.snapshots, snaps, nullptr);
  out.snapshots = std::move(snaps);
  out.drag = run.drag;
  out.lift = run.lift;
  out.totalMass = run.mass;
  out.totalEnergy = run.energy;
  out.finalTime = run.time;
  out.steps = cfg.steps;
  out.wallDeficiencyMean = run.wallDefMean;
  out.wallDeficiencyMax = run.wallDefMax;
  out.avgDt = run.avgDt;
  out.solveMs = run.wallMs;
  out.surface = run.surface;
  const double faceOps = static_cast<double>(mesh.faces.size()) * cfg.steps * 2.0;
  out.facesPerSecond = out.solveMs > 1e-9 ? faceOps / (out.solveMs * 1e-3) : 0.0;

  if (cfg.interpolation) {
    State st;
    st.n = run.final.n;
    st.rho = run.final.rho;
    st.u = run.final.u;
    st.v = run.final.v;
    st.p = run.final.p;
    out.interp = particleMeshInterpolation(cfg, mesh, st);
  }

  if (cfg.sensitivity && cfg.steps > 4) {
    const auto s0 = std::chrono::high_resolution_clock::now();
    Config base = cfg;
    base.snapshots = 0;
    base.interpolation = 0;
    base.sensitivity = 0;
    base.steps = std::max(8, cfg.steps / 2);
    std::vector<Snapshot> tmp;
    std::vector<double> faceP;
    {
      RunSummary r = runCore(base, mesh, 0, tmp, nullptr);
      std::vector<BcType> bcs;
      classifyFaces(base, mesh, bcs);
      std::vector<double> res;
      double mm = 0, dd = 0, ll = 0, dtm = 0;
      assembleResidual(base, mesh, r.final, bcs, res, faceP, mm, dd, ll, dtm, nullptr);
    }
    auto evaluate = [&](const Config &cc) {
      std::vector<Snapshot> t2;
      RunSummary r = runCore(cc, mesh, 0, t2, nullptr);
      std::vector<BcType> bcs;
      classifyFaces(cc, mesh, bcs);
      std::vector<double> res, fP;
      double mm = 0, dd = 0, ll = 0, dtm = 0;
      assembleResidual(cc, mesh, r.final, bcs, res, fP, mm, dd, ll, dtm, nullptr);
      return primitiveGoal(mesh, r.final, cc, fP, &dd, &ll);
    };
    Config c1 = base, c2 = base;
    const double dm = std::max(1e-4, base.mach * 0.05);
    c1.mach = base.mach + dm;
    c2.mach = base.mach - dm;
    const double jp = evaluate(c1), jm = evaluate(c2);
    out.gradMach = (jp - jm) / (2.0 * dm);
    Config c3 = base, c4 = base;
    const double dk = 0.05;
    c3.kappa = base.kappa + dk;
    c4.kappa = std::max(0.0, base.kappa - dk);
    const double kp = evaluate(c3), km = evaluate(c4);
    out.gradKappa = (kp - km) / (2.0 * dk);
    out.sensitivityRuns = 4;
    const auto s1 = std::chrono::high_resolution_clock::now();
    out.sensitivityMs = std::chrono::duration<double, std::milli>(s1 - s0).count();
  }

  const auto t1 = std::chrono::high_resolution_clock::now();
  out.solveMs = std::chrono::duration<double, std::milli>(t1 - t0).count();
  return out;
}

std::string probeResidual(const Config &cfg, const Mesh &mesh) {
  Fields f;
  initFields(cfg, mesh, f);
  std::vector<BcType> bcs;
  classifyFaces(cfg, mesh, bcs);
  std::vector<double> res, faceP;
  double m = 0, d = 0, l = 0, dtm = 0;
  assembleResidual(cfg, mesh, f, bcs, res, faceP, m, d, l, dtm, nullptr);
  double maxRho = 0, maxMom = 0, maxE = 0;
  int worst = -1;
  const double n = static_cast<double>(mesh.cells.size());
  for (size_t i = 0; i < mesh.cells.size(); ++i) {
    const double inv = 1.0 / std::max(1e-30, mesh.cells[i].area);
    const double r0 = std::fabs(res[i * 4 + 0]) * inv;
    const double r1 = std::fabs(res[i * 4 + 1]) * inv;
    const double r2 = std::fabs(res[i * 4 + 2]) * inv;
    const double r3 = std::fabs(res[i * 4 + 3]) * inv;
    if (r1 + r2 > maxMom) { maxMom = r1 + r2; worst = static_cast<int>(i); }
    maxRho = std::max(maxRho, r0);
    maxE = std::max(maxE, r3);
  }
  std::string out = "{\"cells\":" + std::to_string(mesh.cells.size()) +
                    ",\"maxRhoRes\":" + std::to_string(maxRho) +
                    ",\"maxMomRes\":" + std::to_string(maxMom) +
                    ",\"maxERes\":" + std::to_string(maxE) +
                    ",\"dt\":" + std::to_string(dtm);
  int nonZeroMass = 0;
  double minArea = 1e30;
  for (size_t i = 0; i < mesh.cells.size(); ++i) {
    if (std::fabs(res[i * 4 + 0]) > 1e-12) nonZeroMass++;
    minArea = std::min(minArea, mesh.cells[i].area);
  }
  out += ",\"nonZeroMassRes\":" + std::to_string(nonZeroMass) +
         ",\"minArea\":" + std::to_string(minArea);
  if (worst >= 0) {
    const auto &c = mesh.cells[static_cast<size_t>(worst)];
    out += ",\"worst\":[" + std::to_string(c.cx) + "," + std::to_string(c.cy) + "," +
           std::to_string(c.area) + "," + std::to_string(c.quality) + "]";
    out += ",\"worstBc\":";
    std::string tags;
    for (int k = mesh.offsets[static_cast<size_t>(worst)]; k < mesh.offsets[static_cast<size_t>(worst) + 1]; ++k) {
      const auto &fc = mesh.faces[static_cast<size_t>(mesh.indices[static_cast<size_t>(k)])];
      tags += std::to_string(fc.bc) + ":" + std::to_string(static_cast<int>(bcs[static_cast<size_t>(mesh.indices[static_cast<size_t>(k)])])) + " ";
    }
    out += "\"" + tags + "\"";
    out += ",\"worstRes\":[";
    for (int c2 = 0; c2 < 4; ++c2) {
      out += std::to_string(res[static_cast<size_t>(worst) * 4 + static_cast<size_t>(c2)]) + (c2 < 3 ? "," : "");
    }
    out += "]";
  }
  out += ",\"stateRange\":[";
  double rmin = 1e300, rmax = -1e300, umin = 1e300, umax = -1e300, pmin = 1e300, pmax = -1e300;
  for (int i = 0; i < f.n; ++i) {
    rmin = std::min(rmin, f.rho[i]); rmax = std::max(rmax, f.rho[i]);
    umin = std::min(umin, f.u[i]); umax = std::max(umax, f.u[i]);
    pmin = std::min(pmin, f.p[i]); pmax = std::max(pmax, f.p[i]);
  }
  out += std::to_string(rmin) + "," + std::to_string(rmax) + "," + std::to_string(umin) + "," +
         std::to_string(umax) + "," + std::to_string(pmin) + "," + std::to_string(pmax) + "]";
  double test[4];
  const auto &f0 = mesh.faces[0];
  hllcFlux(1.0, 0.0, 0.0, 1.0, 1.0, 0.0, 0.0, 1.0, f0.nx, f0.ny, 1.4, test);
  out += ",\"faceFluxTest\":[" + std::to_string(test[0]) + "," + std::to_string(test[1]) + "," +
         std::to_string(test[2]) + "," + std::to_string(test[3]) + "]";
  out += ",\"face0\":[" + std::to_string(f0.nx) + "," + std::to_string(f0.ny) + "," +
         std::to_string(f0.len) + "," + std::to_string(faceP[0]) + "]";
  out += "}";
  (void)n;
  return out;
}

// ---------------------------------------------------------------------------
// JSON serialisation
// ---------------------------------------------------------------------------
namespace {
void appendNum(std::string &s, double v) {
  char buf[40];
  if (!std::isfinite(v)) v = 0.0;
  std::snprintf(buf, sizeof(buf), "%.9g", v);
  s += buf;
}
void appendKey(std::string &s, const char *k) {
  s += '"';
  s += k;
  s += "\":";
}
}  // namespace

std::string meshToJson(const Mesh &mesh, const Config &cfg, double buildMs,
                       double seedCount, double cellCount) {
  std::string s;
  s.reserve(mesh.cells.size() * 400 + mesh.faces.size() * 120);
  double minQ = 1.0, sumQ = 0.0, maxQ = 0.0, totalArea = 0.0, consSum = 0.0, consMax = 0.0;
  int boundaryFaces = 0, obstacleFaces = 0;
  for (const auto &c : mesh.cells) {
    minQ = std::min(minQ, c.quality);
    maxQ = std::max(maxQ, c.quality);
    sumQ += c.quality;
    totalArea += c.area;
    consSum += c.consistency;
    consMax = std::max(consMax, c.consistency);
  }
  for (const auto &f : mesh.faces) {
    if (f.nbr < 0) boundaryFaces++;
    if (f.nbr < 0 && f.bc == 2) obstacleFaces++;
  }
  int minVerts = 1000, maxVerts = 0;
  double minEdge = 1e300, maxEdge = 0.0;
  s += "{";
  appendKey(s, "meta");
  s += "{";
  appendKey(s, "cellCount");
  appendNum(s, static_cast<double>(mesh.cells.size()));
  s += ",";
  appendKey(s, "vertexCount");
  appendNum(s, static_cast<double>(mesh.verts.size()));
  s += ",";
  appendKey(s, "faceCount");
  appendNum(s, static_cast<double>(mesh.faces.size()));
  s += ",";
  appendKey(s, "interiorFaces");
  appendNum(s, static_cast<double>(mesh.faces.size() - boundaryFaces));
  s += ",";
  appendKey(s, "boundaryFaces");
  appendNum(s, boundaryFaces);
  s += ",";
  appendKey(s, "obstacleFaces");
  appendNum(s, obstacleFaces);
  s += ",";
  appendKey(s, "seedCount");
  appendNum(s, seedCount);
  s += ",";
  appendKey(s, "spacing");
  appendNum(s, mesh.spacing);
  s += ",";
  appendKey(s, "target");
  appendNum(s, cfg.target);
  s += ",";
  appendKey(s, "relaxIterations");
  appendNum(s, cfg.relax);
  s += ",";
  appendKey(s, "jitter");
  appendNum(s, cfg.jitter);
  s += ",";
  appendKey(s, "wallLayer");
  appendNum(s, cfg.wallLayer);
  s += ",";
  appendKey(s, "wallClosure");
  s += '"';
  s += cfg.wallClosure;
  s += "\",";
  appendKey(s, "buildMs");
  appendNum(s, buildMs);
  s += ",";
  appendKey(s, "builder");
  s += "\"C++17 Voronoi/Lloyd half-plane clipping, OpenMP\"}";

  s += ",";
  appendKey(s, "domain");
  s += "{\"xmin\":" + std::to_string(cfg.xmin) + ",\"xmax\":" + std::to_string(cfg.xmax) +
       ",\"ymin\":" + std::to_string(cfg.ymin) + ",\"ymax\":" + std::to_string(cfg.ymax) + "},";

  appendKey(s, "stats");
  s += "{";
  appendKey(s, "minQuality");
  appendNum(s, minQ);
  s += ",";
  appendKey(s, "meanQuality");
  appendNum(s, mesh.cells.empty() ? 0.0 : sumQ / static_cast<double>(mesh.cells.size()));
  s += ",";
  appendKey(s, "maxQuality");
  appendNum(s, maxQ);
  s += ",";
  appendKey(s, "coveredArea");
  appendNum(s, totalArea);
  s += ",";
  appendKey(s, "domainArea");
  appendNum(s, mesh.domainArea);
  s += ",";
  appendKey(s, "solidArea");
  appendNum(s, mesh.solidArea);
  s += ",";
  appendKey(s, "coverage");
  appendNum(s, mesh.domainArea > 0 ? totalArea / (mesh.domainArea - mesh.solidArea) : 0.0);
  s += ",";
  appendKey(s, "meanConsistencyDeficiency");
  appendNum(s, mesh.cells.empty() ? 0.0 : consSum / static_cast<double>(mesh.cells.size()));
  s += ",";
  appendKey(s, "maxConsistencyDeficiency");
  appendNum(s, consMax);
  s += ",";
  appendKey(s, "meanStencil");
  appendNum(s, (mesh.cells.empty() ? 0.0
                                  : static_cast<double>(mesh.indices.size()) /
                                        static_cast<double>(mesh.cells.size())));
  s += "},";

  appendKey(s, "cells");
  s += "[";
  for (size_t i = 0; i < mesh.cells.size(); ++i) {
    const auto &c = mesh.cells[i];
    const int faceCountI = mesh.offsets[i + 1] - mesh.offsets[i];
    minVerts = std::min(minVerts, faceCountI);
    maxVerts = std::max(maxVerts, faceCountI);
    if (i) s += ",";
    s += "[";
    appendNum(s, c.cx);
    s += ",";
    appendNum(s, c.cy);
    s += ",";
    appendNum(s, c.area);
    s += ",";
    appendNum(s, c.quality);
    s += ",";
    appendNum(s, c.consistency);
    s += ",";
    appendNum(s, static_cast<double>(faceCountI));
    s += "]";
  }
  s += "],";

  appendKey(s, "verts");
  s += "[";
  for (size_t i = 0; i < mesh.verts.size(); ++i) {
    if (i) s += ",";
    appendNum(s, mesh.verts[i].x);
    s += ",";
    appendNum(s, mesh.verts[i].y);
  }
  s += "],";

  appendKey(s, "faces");
  s += "[";
  for (size_t i = 0; i < mesh.faces.size(); ++i) {
    const auto &f = mesh.faces[i];
    minEdge = std::min(minEdge, f.len);
    maxEdge = std::max(maxEdge, f.len);
    if (i) s += ",";
    s += "[";
    s += std::to_string(f.v0);
    s += ",";
    s += std::to_string(f.v1);
    s += ",";
    s += std::to_string(f.owner);
    s += ",";
    s += std::to_string(f.nbr);
    s += ",";
    appendNum(s, f.len);
    s += ",";
    appendNum(s, f.cx);
    s += ",";
    appendNum(s, f.cy);
    s += ",";
    s += std::to_string(f.bc);
    s += ",";
    appendNum(s, f.nx);
    s += ",";
    appendNum(s, f.ny);
    s += "]";
  }
  s += "],";

  appendKey(s, "graph");
  s += "{\"offsets\":[";
  for (size_t i = 0; i < mesh.offsets.size(); ++i) {
    if (i) s += ",";
    s += std::to_string(mesh.offsets[i]);
  }
  s += "],\"indices\":[";
  for (size_t i = 0; i < mesh.indices.size(); ++i) {
    if (i) s += ",";
    s += std::to_string(mesh.indices[i]);
  }
  s += "]},";

  appendKey(s, "cellArray");
  s += "{\"mindims\":[" + std::to_string(minVerts) + "," + std::to_string(maxVerts) +
       "],\"minEdge\":" + std::to_string(minEdge) + ",\"maxEdge\":" + std::to_string(maxEdge) + "}";
  s += "}";
  (void)cellCount;
  return s;
}

std::string solveToJson(const SolveResult &res, const Mesh &mesh, const Config &cfg) {
  std::string s;
  s.reserve(1024 * 512);
  s += "{";
  appendKey(s, "meta");
  s += "{\"cellCount\":" + std::to_string(mesh.cells.size()) + ",\"steps\":" +
       std::to_string(cfg.steps) + ",\"order\":" + std::to_string(cfg.order) +
       ",\"scheme\":\"HLLC\",\"wallClosure\":\"" + cfg.wallClosure + "\",\"ic\":\"" + cfg.ic +
       "\",\"mach\":" + std::to_string(cfg.mach) + ",\"cfl\":" + std::to_string(cfg.cfl) +
       ",\"gamma\":" + std::to_string(cfg.gamma) + ",\"kappa\":" + std::to_string(cfg.kappa) + "},";

  appendKey(s, "stats");
  s += "{";
  appendKey(s, "finalTime");
  appendNum(s, res.finalTime);
  s += ",";
  appendKey(s, "avgDt");
  appendNum(s, res.avgDt);
  s += ",";
  appendKey(s, "solveMs");
  appendNum(s, res.solveMs);
  s += ",";
  appendKey(s, "faceEvalsPerSecond");
  appendNum(s, res.facesPerSecond);
  s += ",";
  appendKey(s, "totalMass");
  appendNum(s, res.totalMass);
  s += ",";
  appendKey(s, "totalEnergy");
  appendNum(s, res.totalEnergy);
  s += ",";
  appendKey(s, "drag");
  appendNum(s, res.drag);
  s += ",";
  appendKey(s, "lift");
  appendNum(s, res.lift);
  s += ",";
  appendKey(s, "consistencyDeficiencyMean");
  appendNum(s, res.wallDeficiencyMean);
  s += ",";
  appendKey(s, "consistencyDeficiencyMax");
  appendNum(s, res.wallDeficiencyMax);
  s += ",";
  appendKey(s, "gradMach");
  appendNum(s, res.gradMach);
  s += ",";
  appendKey(s, "gradKappa");
  appendNum(s, res.gradKappa);
  s += ",";
  appendKey(s, "sensitivityRuns");
  appendNum(s, res.sensitivityRuns);
  s += ",";
  appendKey(s, "sensitivityMs");
  appendNum(s, res.sensitivityMs);
  s += "},";

  appendKey(s, "interpolation");
  s += "{";
  appendKey(s, "nnz");
  appendNum(s, res.interp.nnz);
  s += ",";
  appendKey(s, "meanStencil");
  appendNum(s, res.interp.meanStencil);
  s += ",";
  appendKey(s, "sparsity");
  appendNum(s, res.interp.sparsity);
  s += ",";
  appendKey(s, "conservationError");
  appendNum(s, res.interp.conservationError);
  s += ",";
  appendKey(s, "l2Error");
  appendNum(s, res.interp.l2Error);
  s += ",";
  appendKey(s, "linfError");
  appendNum(s, res.interp.linfError);
  s += ",";
  appendKey(s, "timeMs");
  appendNum(s, res.interp.timeMs);
  s += "},";

  appendKey(s, "surface");
  s += "[";
  for (size_t i = 0; i < res.surface.size(); ++i) {
    if (i) s += ",";
    s += "[";
    for (size_t k = 0; k < res.surface[i].size(); ++k) {
      if (k) s += ",";
      appendNum(s, res.surface[i][k]);
    }
    s += "]";
  }
  s += "],";

  appendKey(s, "snapshots");
  s += "[";
  for (size_t si = 0; si < res.snapshots.size(); ++si) {
    const auto &snap = res.snapshots[si];
    if (si) s += ",";
    s += "{";
    appendKey(s, "step");
    appendNum(s, snap.step);
    s += ",";
    appendKey(s, "time");
    appendNum(s, snap.time);
    s += ",";
    appendKey(s, "dt");
    appendNum(s, snap.dt);
    s += ",";
    appendKey(s, "mass");
    appendNum(s, snap.mass);
    s += ",";
    appendKey(s, "energy");
    appendNum(s, snap.energy);
    s += ",";
    appendKey(s, "drag");
    appendNum(s, snap.drag);
    s += ",";
    appendKey(s, "lift");
    appendNum(s, snap.lift);
    s += ",";
    appendKey(s, "maxMach");
    appendNum(s, snap.maxMach);
    s += ",";
    appendKey(s, "minRho");
    appendNum(s, snap.minRho);
    s += ",";
    appendKey(s, "maxRho");
    appendNum(s, snap.maxRho);
    s += ",";
    appendKey(s, "rho");
    s += "[";
    for (size_t i = 0; i < snap.rho.size(); ++i) {
      if (i) s += ",";
      appendNum(s, snap.rho[i]);
    }
    s += "],";
    appendKey(s, "u");
    s += "[";
    for (size_t i = 0; i < snap.u.size(); ++i) {
      if (i) s += ",";
      appendNum(s, snap.u[i]);
    }
    s += "],";
    appendKey(s, "v");
    s += "[";
    for (size_t i = 0; i < snap.v.size(); ++i) {
      if (i) s += ",";
      appendNum(s, snap.v[i]);
    }
    s += "],";
    appendKey(s, "p");
    s += "[";
    for (size_t i = 0; i < snap.p.size(); ++i) {
      if (i) s += ",";
      appendNum(s, snap.p[i]);
    }
    s += "],";
    appendKey(s, "Ma");
    s += "[";
    for (size_t i = 0; i < snap.ma.size(); ++i) {
      if (i) s += ",";
      appendNum(s, snap.ma[i]);
    }
    s += "]}";
  }
  s += "]}";
  return s;
}

std::string stateToJson(const State &, const Mesh &) { return "{}"; }

}  // namespace fvmspm
