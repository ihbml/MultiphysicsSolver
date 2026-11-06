// Eulerian SPH particle generation following
//   [W1] Wang, Haidn, Hu - FVM boundary algorithm inside Eulerian SPH.
//
// What is produced here is exactly the "inner configuration" data set an
// EiSPH/FVM solver needs, plus the extra boundary storage introduced in [W1]:
//
//   * body-fitted particle distribution from the particle relaxation technique
//     (drives sum_j V_j grad W_ij -> 0 for particles away from the boundary)
//   * zero-order consistency residual  A_i    = sum_j V_j grad W_ij
//   * divergence of position           div r_i ~ d, detector div r_i < gamma*d
//   * boundary surface contribution    A_iS   = -sum_P A_iP with |A_iS|, n_iS and
//     the wall distance stored in the slots normally used by (dW/dr)V_j and e_ij
//   * kernel-gradient correction matrix B_i = (sum_j V_j r_ij (x) grad W_ij)^-1
//   * adaptive particle refinement (nested 4-way split of the Cartesian lattice)
#include <algorithm>
#include <chrono>
#include <cmath>
#include <unordered_set>

#include "core.hpp"

#ifdef _OPENMP
#include <omp.h>
#endif

namespace mesher {

// ------------------------------------------------------------------ kernels ----
KernelInfo kernelInfo(int id) {
  switch (id) {
    case 1:
      return KernelInfo{1, "cubicSpline", 40.0 / (7.0 * kPi), 1.0};
    case 2:
      return KernelInfo{2, "wendlandC4", 3.0 / kPi, 1.0};
    default:
      return KernelInfo{0, "wendlandC2", 7.0 / kPi, 1.0};
  }
}

Real kernelNormConst(int id, Real R) {
  const Real s = kernelInfo(id).norm;
  return s / (R * R);  // 2D normalisation, R = support radius
}

Real kernelW(int id, Real r, Real R) {
  const Real q = r / R;
  if (q >= 1.0) return 0.0;
  const Real c = kernelNormConst(id, R);
  switch (id) {
    case 1: {
      const Real s = 2.0 * q;
      const Real f = (s <= 1.0) ? (1.0 - 1.5 * s * s + 0.75 * s * s * s)
                                : (0.25 * std::pow(2.0 - s, 3));
      return c * f;
    }
    case 2: {
      const Real o = 1.0 - q;
      const Real o6 = o * o * o * o * o * o;
      return c * o6 * (3.0 + 18.0 * q + 35.0 * q * q);
    }
    default: {
      const Real o = 1.0 - q;
      const Real o4 = o * o * o * o;
      return c * o4 * (1.0 + 4.0 * q);
    }
  }
}

Real kernelDwdr(int id, Real r, Real R) {
  const Real q = r / R;
  if (q >= 1.0 || R <= 0) return 0.0;
  const Real c = kernelNormConst(id, R) / R;
  switch (id) {
    case 1: {
      const Real s = 2.0 * q;
      const Real fp = (s <= 1.0) ? (-3.0 * s + 2.25 * s * s) : (-0.75 * (2.0 - s) * (2.0 - s));
      return c * fp * 2.0;
    }
    case 2: {
      const Real o = 1.0 - q;
      const Real o5 = o * o * o * o * o;
      return c * (-56.0 * q * o5 * (1.0 + 5.0 * q));
    }
    default: {
      const Real o = 1.0 - q;
      const Real o3 = o * o * o;
      return c * (-20.0 * q * o3);
    }
  }
}

Real sdBoxPublic(const V2& p, const V2& lo, const V2& hi);

namespace {

struct LatticeParticle {
  V2 p;
  int level = 0;
};

int levelAt(const SphSpec& ss, const V2& p, const Sdf& sdf, const CouplingSpec& cs) {
  if (ss.levels <= 1 || ss.levelKind == 0) return 0;
  int lv = 0;
  if (ss.levelKind == 2) {
    const Real d = (cs.coreKind == 2)
                       ? sdBoxPublic(p, cs.coreLo, cs.coreHi)
                       : (norm(p - cs.coreCenter) - cs.coreRadius);
    if (d < 0) lv = ss.levels - 1;
    else if (d < cs.buffer) lv = std::max(0, ss.levels - 2);
    return lv;
  }
  for (const auto& t : ss.levelTargets) {
    Real d = 0;
    if (t.kind == 1) d = std::fabs(norm(p - t.c) - t.r);
    else if (t.kind == 2) d = sdBoxPublic(p, t.lo, t.hi);
    else d = norm(p - t.c);
    const Real rad = t.width > 0 ? t.width : std::max(t.r, 1e-9);
    if (d <= rad) lv = std::max(lv, std::clamp(t.level, 0, ss.levels - 1));
  }
  (void)sdf;
  return lv;
}

}  // namespace

// Forward declaration used by levelAt (defined in geometry.cpp as a lambda-free
// helper); kept local to avoid polluting the public header.
Real sdBoxPublic(const V2& p, const V2& lo, const V2& hi) {
  const V2 c = (lo + hi) * 0.5, b = (hi - lo) * 0.5;
  const V2 d(std::fabs(p.x - c.x) - b.x, std::fabs(p.y - c.y) - b.y);
  const V2 dm(std::max(d.x, 0.0), std::max(d.y, 0.0));
  return norm(dm) + std::min(std::max(d.x, d.y), 0.0);
}

// ------------------------------------------------------- particle generation ----
Particles buildSphParticles(const Spec& spec, const ContourLoops& contours, Timer& timer,
                            std::vector<std::string>& warnings) {
  Particles P;
  const SphSpec& ss = spec.sph;
  const Sdf& sdf = spec.geom;
  if (!ss.enabled) return P;
  const Real dp0 = std::max(ss.dp, 1e-9);
  const KernelInfo ki = kernelInfo(ss.kernel);
  P.kernelName = ki.name;

  // ---- nested Cartesian lattice with adaptive refinement -----------------
  std::vector<LatticeParticle> pts;
  {
    ScopedTimer st(timer, "sphLattice");
    const AABB box = contours.box.valid() ? contours.box : spec.domain;
    const Real m = dp0 * 2.0;
    const V2 lo(box.lo.x - m, box.lo.y - m), hi(box.hi.x + m, box.hi.y + m);
    const int nx = static_cast<int>(std::ceil((hi.x - lo.x) / dp0)) + 1;
    const int ny = static_cast<int>(std::ceil((hi.y - lo.y) / dp0)) + 1;
    std::vector<LatticeParticle> cur;
    cur.reserve(static_cast<size_t>(nx) * ny / 2 + 16);
    for (int j = 0; j < ny; ++j)
      for (int i = 0; i < nx; ++i) {
        const V2 p(lo.x + (i + 0.5) * dp0, lo.y + (j + 0.5) * dp0);
        const Real s = sdf(p);
        if (s > 0.25 * dp0) continue;  // clearly outside the fluid domain
        LatticeParticle lp;
        lp.p = p;
        lp.level = levelAt(ss, p, sdf, spec.cpl);
        cur.push_back(lp);
      }
    // nested 4-way refinement, one level at a time (transition band of one
    // coarse support radius keeps neighbouring levels within one jump)
    for (int lv = 1; lv < ss.levels; ++lv) {
      const Real dpc = dp0 / std::pow(2.0, lv - 1);
      const Real dpf = dp0 / std::pow(2.0, lv);
      const Real band = ss.hRatio * dpc;
      std::vector<LatticeParticle> next;
      next.reserve(cur.size() * 2);
      // first pass: mark the transition band
      std::vector<char> mark(cur.size(), 0);
      Grid g;
      std::vector<V2> tmpPos(cur.size());
      for (size_t i = 0; i < cur.size(); ++i) tmpPos[i] = cur[i].p;
      g.build(box, std::max(dpc, 1e-9), cur.size(), tmpPos.data());
      for (size_t i = 0; i < cur.size(); ++i) {
        if (cur[i].level < lv) continue;
        g.forNear(cur[i].p, band, [&](int j) {
          if (cur[j].level < lv) mark[j] = 1;
        });
      }
      for (size_t i = 0; i < cur.size(); ++i) {
        const int want = std::max(cur[i].level, mark[i] ? lv : 0);
        if (want < lv) {
          next.push_back(cur[i]);
          continue;
        }
        const V2& c = cur[i].p;
        const V2 off[4] = {V2(-0.5, -0.5), V2(0.5, -0.5), V2(-0.5, 0.5), V2(0.5, 0.5)};
        for (int k = 0; k < 4; ++k) {
          LatticeParticle lp;
          lp.p = c + off[k] * (0.5 * dpf);
          // keep the requested level of the size field: a child inside a
          // finer zone must stay eligible for the next refinement pass
          lp.level = std::max(lv, levelAt(ss, lp.p, sdf, spec.cpl));
          const Real s = sdf(lp.p);
          if (s > 0.25 * dpf) continue;
          next.push_back(lp);
        }
      }
      cur.swap(next);
    }
    pts.swap(cur);
    if (pts.empty()) {
      warnings.push_back("SPH lattice is empty: particle spacing too large for the domain");
      return P;
    }
  }

  const size_t n = pts.size();
  P.pos.resize(n);
  P.level.resize(n);
  P.dp.resize(n);
  P.h.resize(n);
  P.volume.resize(n);
  P.category.resize(n, PCAT_INTERIOR);
  P.bcType.assign(n, -1);
  P.patch.assign(n, -1);
  P.region.assign(n, 0);
#ifdef _OPENMP
#pragma omp parallel for schedule(static)
#endif
  for (long long i = 0; i < static_cast<long long>(n); ++i) {
    const Real dpi = dp0 / std::pow(2.0, pts[i].level);
    P.pos[i] = pts[i].p;
    P.level[i] = pts[i].level;
    P.dp[i] = dpi;
    P.h[i] = ss.hRatio * dpi;
    P.volume[i] = dpi * dpi;  // nominal volume (2D)
  }
  for (size_t i = 0; i < n; ++i) P.box.expand(pts[i].p);
  P.kernelSupport = ss.hRatio * dp0;

  // ---- body-fitted particle relaxation [W1 eq. 19] ----------------------
  if (ss.relax && ss.relaxIters > 0) {
    ScopedTimer st(timer, "sphRelaxation");
    const Real Rmax = *std::max_element(P.h.begin(), P.h.end());
    Grid g;
    std::vector<V2> newPos(n);
    std::vector<Real> residual(n, 0.0);
    Real r0 = 0, r1 = 0;
    const int iters = ss.relaxIters;
    for (int it = 0; it < iters; ++it) {
      g.build(P.box, Rmax, n, P.pos.data());
      const Real cool = 1.0 - 0.6 * static_cast<Real>(it) / std::max(1, iters - 1);
      const Real C = ss.relaxCoeff * cool;
#ifdef _OPENMP
#pragma omp parallel for if(n > 256) schedule(static)
#endif
      for (long long i = 0; i < static_cast<long long>(n); ++i) {
        V2 s(0, 0);
        Real sn = 0, rep = 0;
        V2 repV(0, 0);
        const Real Ri = P.h[i];
        const Real Vi = P.volume[i];
        const Real dpi = P.dp[i];
        g.forNear(P.pos[i], Rmax, [&](int j) {
          if (j == i) return;
          const V2 rij = P.pos[i] - P.pos[j];
          const Real Rij = 0.5 * (Ri + P.h[j]);
          const Real r2 = norm2(rij);
          if (r2 >= Rij * Rij || r2 < 1e-28) return;
          const Real r = std::sqrt(r2);
          const Real dw = kernelDwdr(ss.kernel, r, Rij);
          const V2 e = rij / r;
          s += e * (dw * P.volume[j]);
          sn += std::fabs(dw) * P.volume[j];
          // short range repulsion (keeps the relaxed distribution regular)
          const Real rmin = 0.75 * std::min(dpi, P.dp[j]);
          if (r < rmin) {
            const Real f = (rmin - r) / rmin;
            repV += e * (f * 0.5);
            rep = std::max(rep, f);
          }
        });
        (void)Vi;
        (void)sn;
        // CFL-limited descent step: |dx| <= C * dp_i, direction away from the
        // neighbour cloud (repulsive), which drives sum_j V_j gradW_ij -> 0 for
        // particles with complete kernel support and pushes the outer particles
        // onto the wall (body-fitted distribution, [W1]).
        V2 dx(0, 0);
        const Real nrm = norm(s);
        if (nrm > 1e-16) dx = (s / nrm) * (-C * dpi * std::min(1.0, nrm * Ri));
        const Real rn = norm(repV);
        if (rn > 1e-16) dx += (repV / std::max(rn, 1.0)) * (0.5 * C * dpi * std::min(1.0, rn));
        residual[i] = nrm;
        newPos[i] = P.pos[i] + dx;
      }
      Real sum = 0;
      long long nRef = 0;
#ifdef _OPENMP
#pragma omp parallel for if(n > 256) reduction(+ : sum, nRef) schedule(static)
#endif
      for (long long i = 0; i < static_cast<long long>(n); ++i) {
        const bool interiorRef = -sdf(P.pos[i]) > 1.05 * Rmax;
        V2 p = newPos[i];
        const Real d = sdf(p);
        const Real band = ss.bandFactor * P.dp[i];
        if (d > -band) {
          // project onto the boundary surface (body-fitted) and freeze the
          // normal component: boundary particles may only slide tangentially
          V2 g2 = sdf.grad(p, 1e-6);
          const Real gn = norm(g2);
          if (gn > 1e-14) {
            const V2 nh = g2 / gn;
            const V2 q = p - nh * (d / gn);
            if (ss.snapBoundary) {
              p = q;
            } else {
              const V2 cur = P.pos[i];
              const Real dc = sdf(cur);
              V2 gc = sdf.grad(cur, 1e-6);
              const Real gcn = std::max(norm(gc), 1e-14);
              const V2 n0 = gc / gcn;
              const V2 tang = (p - cur) - n0 * dot(p - cur, n0);
              p = cur + tang - n0 * std::max(0.0, dc);
            }
          }
        } else if (d > 0) {
          V2 g2 = sdf.grad(p, 1e-6);
          const Real gn = std::max(norm(g2), 1e-14);
          p = p - g2 * (d / gn);
        }
        P.pos[i] = p;
        if (interiorRef) {
          sum += residual[i];
          nRef++;
        }
      }
      const Real denom = static_cast<Real>(std::max<long long>(1, nRef));
      if (it == 0) r0 = sum / denom;
      r1 = sum / denom;
      // the box may grow slightly
      for (size_t i = 0; i < n; ++i) P.box.expand(P.pos[i]);
    }
    P.relaxResidual0 = r0;
    P.relaxResidual1 = r1;
    P.relaxItersDone = iters;
  }

  // Shepard volumes (optional) and neighbour CSR --------------------------
  computeConsistency(spec, P, timer);

  finalizeParticleStats(spec, P);

  P.valid = true;
  P.dim = 2;
  (void)contours;
  return P;
}

// ------------------------------------------- consistency + boundary storage ----
void computeConsistency(const Spec& spec, Particles& P, Timer& timer) {
  ScopedTimer st(timer, "sphConsistency");
  const SphSpec& ss = spec.sph;
  const Sdf& sdf = spec.geom;
  const size_t n = P.pos.size();
  if (!n) return;
  const Real Rmax = *std::max_element(P.h.begin(), P.h.end());

  P.aSum.assign(n, V2(0, 0));
  P.aBnd.assign(n, V2(0, 0));
  P.aBndMag.assign(n, 0.0);
  P.nBnd.assign(n, V2(0, 0));
  P.divPos.assign(n, 0.0);
  P.isBoundary.assign(n, 0.0);
  P.wallDist.assign(n, 0.0);
  P.consistency.assign(n, 0.0);
  P.bMat.assign(n, {1, 0, 0, 1});
  P.nbrPtr.assign(n + 1, 0);
  P.nbrCount.assign(n, 0);

  if (ss.volumeModel == 1) {
    // Shepard-normalised volumes: V_i = 1 / sum_j W_ij
    Grid g;
    g.build(P.box, Rmax, n, P.pos.data());
    std::vector<Real> wsum(n, 0.0);
#ifdef _OPENMP
#pragma omp parallel for if(n > 256) schedule(static)
#endif
    for (long long i = 0; i < static_cast<long long>(n); ++i) {
      Real s = kernelW(ss.kernel, 0.0, P.h[i]);
      g.forNear(P.pos[i], Rmax, [&](int j) {
        if (j == i) return;
        const Real r = norm(P.pos[i] - P.pos[j]);
        const Real Rij = 0.5 * (P.h[i] + P.h[j]);
        if (r < Rij) s += kernelW(ss.kernel, r, Rij);
      });
      wsum[i] = s;
    }
    for (size_t i = 0; i < n; ++i)
      P.volume[i] = wsum[i] > 1e-30 ? 1.0 / wsum[i] : P.dp[i] * P.dp[i];
  }

  Grid g;
  g.build(P.box, Rmax, n, P.pos.data());

  // pass 1: neighbour counts -> CSR prefix sum
#ifdef _OPENMP
#pragma omp parallel for if(n > 256) schedule(static)
#endif
  for (long long i = 0; i < static_cast<long long>(n); ++i) {
    int cnt = 0;
    const Real Rq = P.h[i];
    g.forNear(P.pos[i], Rmax, [&](int j) {
      if (j == i) return;
      const Real Rij = 0.5 * (Rq + P.h[j]);
      if (norm2(P.pos[i] - P.pos[j]) < Rij * Rij) cnt++;
    });
    P.nbrCount[i] = cnt;
    P.nbrPtr[i + 1] = cnt;
  }
  for (size_t i = 0; i < n; ++i) P.nbrPtr[i + 1] += P.nbrPtr[i];
  const size_t nnz = P.nbrPtr[n];
  P.nbrIdx.assign(nnz, 0);
  P.nbrDwdr.assign(nnz, 0.0);
  P.nbrR.assign(nnz, 0.0);
  std::vector<int> cursor(P.nbrPtr.begin(), P.nbrPtr.begin() + n);

  // pass 2: fill the inner configuration and all consistency quantities
#ifdef _OPENMP
#pragma omp parallel for if(n > 256) schedule(static)
#endif
  for (long long i = 0; i < static_cast<long long>(n); ++i) {
    V2 A(0, 0);
    Real divR = 0;
    Real m00 = 0, m01 = 0, m10 = 0, m11 = 0;
    int k = cursor[i];
    const V2& pi = P.pos[i];
    g.forNear(pi, Rmax, [&](int j) {
      if (j == i) return;
      const V2 rij = pi - P.pos[j];
      const Real Rij = 0.5 * (P.h[i] + P.h[j]);
      const Real r2 = norm2(rij);
      if (r2 >= Rij * Rij || r2 < 1e-28) return;
      const Real r = std::sqrt(r2);
      const V2 e = rij / r;
      const Real dw = kernelDwdr(ss.kernel, r, Rij);
      const Real wV = dw * P.volume[j];
      P.nbrIdx[k] = j;
      P.nbrDwdr[k] = wV;
      P.nbrR[k] = r;
      ++k;
      A += e * wV;                       // sum_j V_j grad W_ij
      divR += -r * wV;                   // divergence of position
      const Real gx = dw * e.x, gy = dw * e.y;
      const Real rx = -rij.x, ry = -rij.y;  // x_j - x_i
      m00 += P.volume[j] * rx * gx;
      m01 += P.volume[j] * rx * gy;
      m10 += P.volume[j] * ry * gx;
      m11 += P.volume[j] * ry * gy;
    });
    P.aSum[i] = A;
    P.divPos[i] = divR;
    P.consistency[i] = norm(A);
    // [W1] boundary surface flux contribution stored in the inner configuration
    const V2 Ab(-A.x, -A.y);
    P.aBnd[i] = Ab;
    P.aBndMag[i] = norm(Ab);
    P.nBnd[i] = P.aBndMag[i] > 1e-30 ? Ab / P.aBndMag[i] : V2(0, 0);
    // kernel-gradient correction matrix B_i = M_i^-1  (first order consistency)
    if (ss.gradientCorrection) {
      const Real det = m00 * m11 - m01 * m10;
      if (std::fabs(det) > 1e-14) {
        P.bMat[i] = {m11 / det, -m01 / det, -m10 / det, m00 / det};
      } else {
        P.bMat[i] = {1, 0, 0, 1};
      }
    }
    const Real d = sdf(pi);
    P.wallDist[i] = -d;  // positive inside the fluid domain
  }

  // boundary condition tags from the patch rules (nearest wall projection)
  {
    const Real diag = norm(spec.domain.size());
    for (size_t i = 0; i < n; ++i) {
      if (P.divPos[i] >= ss.gammaFactor * 2.0) continue;
      V2 q = P.pos[i];
      const Real d = sdf(q);
      if (d < 0) {
        V2 gr = sdf.grad(q, 1e-6);
        const Real gn = std::max(norm(gr), 1e-14);
        q = q - gr * (d / gn);
      }
      size_t best = static_cast<size_t>(-1);
      const Real tol = 1e-6 * diag;
      for (size_t ri = 0; ri < spec.fvm.rules.size(); ++ri) {
        const BoundaryRule& r = spec.fvm.rules[ri];
        const Real t2 = r.radius > 0 ? std::max(tol * 100, r.radius * 1e-3) : tol * 100;
        bool hit = false;
        if (r.where == "all") hit = true;
        else if (r.where == "xmin") hit = std::fabs(q.x - spec.domain.lo.x) < t2;
        else if (r.where == "xmax") hit = std::fabs(q.x - spec.domain.hi.x) < t2;
        else if (r.where == "ymin") hit = std::fabs(q.y - spec.domain.lo.y) < t2;
        else if (r.where == "ymax") hit = std::fabs(q.y - spec.domain.hi.y) < t2;
        else if (r.where == "xrange") hit = q.x >= r.a.x - tol && q.x <= r.b.x + tol;
        else if (r.where == "yrange") hit = q.y >= r.a.y - tol && q.y <= r.b.y + tol;
        else if (r.where == "circle") hit = std::fabs(norm(q - r.a) - r.radius) < t2;
        if (hit) {
          best = ri;
          break;
        }
      }
      if (best < spec.fvm.rules.size()) {
        P.bcType[i] = bcTypeFromName(spec.fvm.rules[best].bc);
        P.patch[i] = static_cast<int>(best);
      } else {
        P.bcType[i] = 0;
        P.patch[i] = -1;
      }
    }
    // canonical boundary-condition table, indexed by the stored bc type
    P.bcNames.clear();
    for (int t = 0; t <= 7; ++t) P.bcNames.push_back(bcNameFromType(t));
  }
}

// ------------------------------------------------ particle statistics --------
void finalizeParticleStats(const Spec& spec, Particles& P) {
  const size_t n = P.pos.size();
  if (!n) return;
  const Real gamma = spec.sph.gammaFactor * 2.0;  // d = 2 [W1]
  P.nInterior = P.nBoundary = P.nGhost = P.nCore = P.nBuffer = 0;
  Real sumA = 0, maxA = 0, sumDiv = 0, minDiv = kInf, bndArea = 0, sumAI = 0, maxAI = 0;
  int nRef = 0;
  P.box = AABB();
  for (size_t i = 0; i < n; ++i) {
    P.box.expand(P.pos[i]);
    if (P.category[i] == PCAT_GHOST) {
      P.nGhost++;
      continue;
    }
    if (P.category[i] == PCAT_CORE) P.nCore++;
    else if (P.category[i] == PCAT_BUFFER) P.nBuffer++;
    // [W1] divergence-of-position boundary detector
    if (P.divPos[i] < gamma) {
      P.isBoundary[i] = 1.0;
      P.nBoundary++;
      if (P.category[i] == PCAT_INTERIOR) P.category[i] = PCAT_BOUNDARY;
    } else {
      P.isBoundary[i] = 0.0;
      P.nInterior++;
      // particles in the coupling buffer have their kernel support completed
      // by mesh ghost cells [W2], so they are excluded from the interior
      // zero-order consistency statistics
      if (P.region.empty() || P.region[i] != 2) {
        sumAI += P.consistency[i];
        maxAI = std::max(maxAI, P.consistency[i]);
        nRef++;
      }
    }
    // sum_i V_i |A_iS| -> half the boundary perimeter [W1 verification]
    bndArea += P.volume[i] * P.aBndMag[i];
    sumA += P.consistency[i];
    maxA = std::max(maxA, P.consistency[i]);
    sumDiv += P.divPos[i];
    minDiv = std::min(minDiv, P.divPos[i]);
  }
  P.sumA = sumA;
  P.maxA = maxA;
  P.meanA = sumA / n;
  P.nInteriorRef = nRef;
  P.meanAInterior = nRef ? sumAI / nRef : 0;
  P.maxAInterior = maxAI;
  P.meanDivPos = sumDiv / n;
  P.minDivPos = minDiv == kInf ? 0 : minDiv;
  P.bndAreaSum = bndArea;
  Real vsum = 0, meanDp = 0;
  for (size_t i = 0; i < n; ++i) {
    vsum += P.volume[i];
    meanDp += P.dp[i];
  }
  P.sphRegionArea = vsum;
  meanDp /= n;
  Real var = 0;
  for (size_t i = 0; i < n; ++i) var += (P.dp[i] - meanDp) * (P.dp[i] - meanDp);
  P.cvDp = meanDp > 0 ? std::sqrt(var / n) / meanDp : 0;
  Real nn = 0;
  for (size_t i = 0; i < n; ++i) nn += (P.nbrPtr[i + 1] - P.nbrPtr[i]);
  P.meanNbrs = nn / n;
}

}  // namespace mesher
