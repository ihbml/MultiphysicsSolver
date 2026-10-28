// Adaptive SPH-FVM coupling following
//   [W2] Zhang, Ming, Wang, Liang - adaptive SPH-FVM with an optimized
//        particle-mesh interpolation scheme (OPMIS).
//
// Domain split:  core (particles only)  |  buffer/overlap (particles provide
// the state of the mesh ghost cells)  |  mesh (pure FVM, far field / non
// reflecting boundaries live here).
//
// Interpolation operators are emitted as CSR matrices so a solver can apply
// them with a single segmented gather/scatter, and they are verified with a
// patch test on analytic fields:
//   nearest          - zeroth order, no conservation
//   shepard          - partition of unity, fails on linear fields
//   overlap (OPMIS)  - kernel/cell overlap quadrature: conservative transfer
//   overlap+moment   - OPMIS with the linear reproducing (moment) correction,
//                      exact for linear fields -> no spurious pressure waves
//                      when a shock crosses the coupling interface
#include <algorithm>
#include <cmath>

#include "core.hpp"

#ifdef _OPENMP
#include <omp.h>
#endif

namespace mesher {
namespace {

enum class Scheme { Nearest, Shepard, Overlap };

struct Quad {
  V2 p[7];
  Real w[7];
  int n = 1;
};

Quad cellQuadrature(const Mesh& m, int c, int order) {
  Quad q;
  const V2& a = m.nodes[m.cells[c][0]];
  const V2& b = m.nodes[m.cells[c][1]];
  const V2& d = m.nodes[m.cells[c][2]];
  const V2 cen = (a + b + d) / 3.0;
  if (order <= 1) {
    q.n = 1;
    q.p[0] = cen;
    q.w[0] = 1.0;
    return q;
  }
  if (order <= 3) {  // 3-point midpoint rule, exact for quadratics
    q.n = 3;
    q.p[0] = (a + b) * 0.5;
    q.p[1] = (b + d) * 0.5;
    q.p[2] = (d + a) * 0.5;
    q.w[0] = q.w[1] = q.w[2] = 1.0 / 3.0;
    return q;
  }
  // 7 point degree-3 rule (barycentric)
  const Real b1[3] = {0.05971563, 0.47014218, 0.47014218};
  const Real b2[3] = {0.79742699, 0.10128651, 0.10128651};
  q.n = 7;
  q.p[0] = cen;
  q.w[0] = 0.225;
  auto bary = [&](const Real* bb) { return a * bb[0] + b * bb[1] + d * bb[2]; };
  const Real w1 = 0.13239415, w2 = 0.12593918;
  for (int k = 0; k < 3; ++k) {
    Real t[3] = {b1[k], b1[(k + 1) % 3], b1[(k + 2) % 3]};
    q.p[1 + k] = bary(t);
    q.w[1 + k] = w1;
    Real u[3] = {b2[k], b2[(k + 1) % 3], b2[(k + 2) % 3]};
    q.p[4 + k] = bary(u);
    q.w[4 + k] = w2;
  }
  return q;
}

Scheme schemeFromName(const std::string& s) {
  if (s == "nearest") return Scheme::Nearest;
  if (s == "shepard") return Scheme::Shepard;
  return Scheme::Overlap;
}

Real coreDistance(const CouplingSpec& cs, const V2& p) {
  switch (cs.coreKind) {
    case 2: {
      const V2 c = (cs.coreLo + cs.coreHi) * 0.5, b = (cs.coreHi - cs.coreLo) * 0.5;
      const V2 d(std::fabs(p.x - c.x) - b.x, std::fabs(p.y - c.y) - b.y);
      const V2 dm(std::max(d.x, 0.0), std::max(d.y, 0.0));
      return norm(dm) + std::min(std::max(d.x, d.y), 0.0);
    }
    case 3:
      return -1e30;
    default:
      return norm(p - cs.coreCenter) - cs.coreRadius;
  }
}

Real analyticField(int id, const V2& p) {
  switch (id) {
    case 0: return 1.0;
    case 1: return p.x;
    case 2: return p.y;
    case 3: return p.x * p.x + p.y * p.y;
    case 4: return std::sin(2 * kPi * p.x) * std::sin(2 * kPi * p.y);
    default: return 1.0;
  }
}

// Assemble one particle->mesh operator row set.
struct OpBuild {
  CsrMatrix op;
  Real rowSumErr = 0;
  Real maxWeight = 0;
  int rowsNoSupport = 0;
};

OpBuild assembleP2M(const Mesh& mesh, const Particles& parts, const std::vector<int>& rows,
                    Scheme scheme, bool correct, int quadOrder, int kernel) {
  OpBuild out;
  const size_t nr = rows.size();
  out.op.rows = static_cast<int>(mesh.cells.size());
  out.op.cols = static_cast<int>(parts.pos.size());
  out.op.rowPtr.assign(mesh.cells.size() + 1, 0);
  if (nr == 0 || parts.pos.empty()) return out;

  const Real Rmax = *std::max_element(parts.h.begin(), parts.h.end());
  AABB pbox;
  for (auto& p : parts.pos) pbox.expand(p);
  Grid g;
  g.build(pbox, std::max(Rmax, 1e-9), parts.pos.size(), parts.pos.data());

  std::vector<std::vector<std::pair<int, Real>>> local(nr);
  std::vector<Real> rowErr(nr, 0.0);
  std::vector<int> noSupport(nr, 0);
  Real maxW = 0;

#ifdef _OPENMP
#pragma omp parallel for schedule(dynamic, 32)
#endif
  for (long long ri = 0; ri < static_cast<long long>(nr); ++ri) {
    const int c = rows[ri];
    const V2 xc = mesh.cc[c];
    const Real cellR = 0.6 * std::sqrt(std::max(mesh.vol[c], 1e-30));
    Quad q = cellQuadrature(mesh, c, quadOrder);
    std::vector<int> cand;
    g.forNear(xc, Rmax + cellR, [&](int p) { cand.push_back(p); });
    if (cand.empty()) {
      noSupport[ri] = 1;
      continue;
    }
    std::vector<Real> w(cand.size(), 0.0);
    if (scheme == Scheme::Nearest) {
      int best = -1;
      Real bd = kInf;
      for (size_t k = 0; k < cand.size(); ++k) {
        const Real d = norm(parts.pos[cand[k]] - xc);
        if (d < bd) {
          bd = d;
          best = static_cast<int>(k);
        }
      }
      if (best >= 0) w[best] = 1.0;
    } else if (scheme == Scheme::Shepard) {
      for (size_t k = 0; k < cand.size(); ++k) {
        const int p = cand[k];
        const Real r = norm(parts.pos[p] - xc);
        const Real R = std::max(parts.h[p], 1e-12);
        if (r < R) w[k] = parts.volume[p] * kernelW(kernel, r, R);
      }
    } else {
      // OPMIS: integrate the particle kernel over the cell (overlap volume)
      for (size_t k = 0; k < cand.size(); ++k) {
        const int p = cand[k];
        const Real R = std::max(parts.h[p], 1e-12);
        Real acc = 0;
        for (int qi = 0; qi < q.n; ++qi) {
          const Real r = norm(parts.pos[p] - q.p[qi]);
          if (r < R) acc += q.w[qi] * kernelW(kernel, r, R);
        }
        w[k] = acc * parts.volume[p];
      }
    }
    Real sum = 0;
    for (auto v : w) sum += v;
    if (sum <= 1e-300) {
      noSupport[ri] = 1;
      continue;
    }
    for (auto& v : w) v /= sum;
    if (correct && scheme != Scheme::Nearest) {
      // linear reproducing (moment) correction: w' = w (c0 + c . d)
      Real S0 = 0;
      V2 S1(0, 0);
      Real m00 = 0, m01 = 0, m11 = 0;
      for (size_t k = 0; k < cand.size(); ++k) {
        const V2 d = parts.pos[cand[k]] - xc;
        S0 += w[k];
        S1 += d * w[k];
        m00 += w[k] * d.x * d.x;
        m01 += w[k] * d.x * d.y;
        m11 += w[k] * d.y * d.y;
      }
      const Real det = m00 * m11 - m01 * m01;
      if (std::fabs(det) > 1e-24 && S0 > 1e-12) {
        const V2 x(m11 * S1.x - m01 * S1.y, -m01 * S1.x + m00 * S1.y);
        const V2 MinvS1(x.x / det, x.y / det);
        const Real denom = S0 - dot(S1, MinvS1);
        if (std::fabs(denom) > 1e-14) {
          const Real c0 = 1.0 / denom;
          const V2 cc = MinvS1 * (-c0);
          std::vector<Real> wc(cand.size(), 0.0);
          bool ok = true;
          for (size_t k = 0; k < cand.size(); ++k) {
            const V2 d = parts.pos[cand[k]] - xc;
            const Real f = c0 + dot(cc, d);
            wc[k] = w[k] * f;
            if (wc[k] < -0.25) ok = false;
          }
          if (ok) w.swap(wc);
        }
      }
    }
    Real s2 = 0;
    for (size_t k = 0; k < cand.size(); ++k) {
      if (std::fabs(w[k]) < 1e-14) continue;
      local[ri].push_back({cand[k], w[k]});
      s2 += w[k];
      maxW = std::max(maxW, std::fabs(w[k]));
    }
    rowErr[ri] = std::fabs(s2 - 1.0);
  }

  // CSR pack
  for (size_t ri = 0; ri < nr; ++ri) {
    out.op.rowPtr[rows[ri] + 1] = static_cast<int>(local[ri].size());
    out.rowSumErr = std::max(out.rowSumErr, rowErr[ri]);
    if (noSupport[ri]) out.rowsNoSupport++;
  }
  for (size_t c = 0; c < mesh.cells.size(); ++c) out.op.rowPtr[c + 1] += out.op.rowPtr[c];
  const size_t nnz = out.op.rowPtr[mesh.cells.size()];
  out.op.col.resize(nnz);
  out.op.val.resize(nnz);
  for (size_t ri = 0; ri < nr; ++ri) {
    int k = out.op.rowPtr[rows[ri]];
    for (auto& pr : local[ri]) {
      out.op.col[k] = pr.first;
      out.op.val[k] = pr.second;
      ++k;
    }
  }
  out.maxWeight = maxW;
  return out;
}

OpBuild assembleM2P(const Mesh& mesh, const Particles& parts, const std::vector<int>& rows,
                    bool correct, int kernel) {
  OpBuild out;
  out.op.rows = static_cast<int>(parts.pos.size());
  out.op.cols = static_cast<int>(mesh.cells.size());
  out.op.rowPtr.assign(parts.pos.size() + 1, 0);
  if (rows.empty() || mesh.cells.empty()) return out;
  const Real Rmax = *std::max_element(parts.h.begin(), parts.h.end());
  Grid g;
  g.build(mesh.box, std::max(Rmax, 2.0 * std::sqrt(std::max(mesh.maxVol, 1e-30))),
          mesh.cc.size(), mesh.cc.data());

  std::vector<std::vector<std::pair<int, Real>>> local(rows.size());
  std::vector<Real> rowErr(rows.size(), 0.0);
#ifdef _OPENMP
#pragma omp parallel for schedule(dynamic, 32)
#endif
  for (long long ri = 0; ri < static_cast<long long>(rows.size()); ++ri) {
    const int p = rows[ri];
    const V2 xp = parts.pos[p];
    const Real R = std::max(parts.h[p], 1e-12);
    std::vector<int> cand;
    g.forNear(xp, R, [&](int c) { cand.push_back(c); });
    std::vector<Real> w(cand.size(), 0.0);
    Real sum = 0;
    for (size_t k = 0; k < cand.size(); ++k) {
      const Real r = norm(mesh.cc[cand[k]] - xp);
      if (r >= R) continue;
      w[k] = mesh.vol[cand[k]] * kernelW(kernel, r, R);
      sum += w[k];
    }
    if (sum <= 1e-300) continue;
    for (auto& v : w) v /= sum;
    if (correct) {
      Real S0 = 0;
      V2 S1(0, 0);
      Real m00 = 0, m01 = 0, m11 = 0;
      for (size_t k = 0; k < cand.size(); ++k) {
        if (w[k] == 0) continue;
        const V2 d = mesh.cc[cand[k]] - xp;
        S0 += w[k];
        S1 += d * w[k];
        m00 += w[k] * d.x * d.x;
        m01 += w[k] * d.x * d.y;
        m11 += w[k] * d.y * d.y;
      }
      const Real det = m00 * m11 - m01 * m01;
      if (std::fabs(det) > 1e-24) {
        const V2 x(m11 * S1.x - m01 * S1.y, -m01 * S1.x + m00 * S1.y);
        const V2 MinvS1(x.x / det, x.y / det);
        const Real denom = S0 - dot(S1, MinvS1);
        if (std::fabs(denom) > 1e-14) {
          const Real c0 = 1.0 / denom;
          const V2 cc = MinvS1 * (-c0);
          std::vector<Real> wc(cand.size(), 0.0);
          bool ok = true;
          for (size_t k = 0; k < cand.size(); ++k) {
            if (w[k] == 0) continue;
            const Real f = c0 + dot(cc, mesh.cc[cand[k]] - xp);
            wc[k] = w[k] * f;
            if (wc[k] < -0.25) ok = false;
          }
          if (ok) w.swap(wc);
        }
      }
    }
    Real s2 = 0;
    for (size_t k = 0; k < cand.size(); ++k) {
      if (std::fabs(w[k]) < 1e-14) continue;
      local[ri].push_back({cand[k], w[k]});
      s2 += w[k];
    }
    rowErr[ri] = std::fabs(s2 - 1.0);
  }
  for (size_t ri = 0; ri < rows.size(); ++ri) {
    out.op.rowPtr[rows[ri] + 1] = static_cast<int>(local[ri].size());
    out.rowSumErr = std::max(out.rowSumErr, rowErr[ri]);
  }
  for (size_t p = 0; p < parts.pos.size(); ++p) out.op.rowPtr[p + 1] += out.op.rowPtr[p];
  const size_t nnz = out.op.rowPtr[parts.pos.size()];
  out.op.col.resize(nnz);
  out.op.val.resize(nnz);
  for (size_t ri = 0; ri < rows.size(); ++ri) {
    int k = out.op.rowPtr[rows[ri]];
    for (auto& pr : local[ri]) {
      out.op.col[k] = pr.first;
      out.op.val[k] = pr.second;
      ++k;
    }
  }
  return out;
}

Real patchError(const CsrMatrix& op, const std::vector<Real>& srcValues,
                const std::vector<int>& rows, const Mesh& mesh, const Particles& parts, int field,
                bool rowsAreCells) {
  Real num = 0, den = 0;
  for (const int r : rows) {
    Real acc = 0;
    for (int k = op.rowPtr[r]; k < op.rowPtr[r + 1]; ++k) acc += op.val[k] * srcValues[op.col[k]];
    const V2 x = rowsAreCells ? mesh.cc[r] : parts.pos[r];
    const Real ex = analyticField(field, x);
    num += (acc - ex) * (acc - ex);
    den += ex * ex;
  }
  if (rows.empty()) return 0;
  return std::sqrt(num / rows.size()) / std::max(1e-14, std::sqrt(den / rows.size()));
}

}  // namespace

Coupling buildCoupling(const Spec& spec, const Mesh& mesh, Particles& parts, Timer& timer,
                       std::vector<std::string>& warnings) {
  Coupling C;
  const CouplingSpec& cs = spec.cpl;
  if (!cs.enabled) return C;
  C.p2mScheme = cs.p2m;
  C.m2pScheme = cs.m2p;

  const Real dp0 = spec.sph.dp;
  C.bufferWidth = cs.buffer > 0 ? cs.buffer : spec.sph.hRatio * dp0;

  // ---- adaptive domain split -------------------------------------------
  {
    ScopedTimer st(timer, "couplingRegions");
    if (mesh.valid) {
      C.cellRegion.assign(mesh.cells.size(), 0);
      C.cellCoupling.assign(mesh.cells.size(), 0);
      C.cellVolumeFrac.assign(mesh.cells.size(), 0.0);
      Real coreArea = 0, bufferArea = 0;
#ifdef _OPENMP
#pragma omp parallel for reduction(+ : coreArea, bufferArea) schedule(static)
#endif
      for (long long i = 0; i < static_cast<long long>(mesh.cells.size()); ++i) {
        const Real d = coreDistance(cs, mesh.cc[i]);
        if (d < 0) {
          C.cellRegion[i] = 2;
          coreArea += mesh.vol[i];
        } else if (d < C.bufferWidth) {
          C.cellRegion[i] = 1;
          bufferArea += mesh.vol[i];
        }
      }
      C.coreArea = coreArea;
      C.bufferArea = bufferArea;
      for (size_t f = 0; f < mesh.faces.size(); ++f) {
        const Face& fc = mesh.faces[f];
        if (fc.neighbour < 0) continue;
        const int ro = C.cellRegion[fc.owner], rn = C.cellRegion[fc.neighbour];
        if (ro != rn) {
          C.interfaceFaces.push_back(static_cast<int>(f));
          C.cellCoupling[fc.owner] = 1;
          C.cellCoupling[fc.neighbour] = 1;
        }
      }
      for (size_t i = 0; i < mesh.cells.size(); ++i) {
        if (C.cellRegion[i] == 2) C.nGhostCells++;
        else if (C.cellRegion[i] == 1) C.nBufferCells++;
        else C.nCoreCells++;
      }
      C.nInterfaceFaces = static_cast<int>(C.interfaceFaces.size());
    }

    // adaptive particle domain: drop particles the mesh now covers [W2]
    if (parts.valid && cs.mode != "sphOnly") {
      const size_t n = parts.pos.size();
      std::vector<int> keep;
      keep.reserve(n);
      std::vector<int> newRegion(n, 0);
      for (size_t i = 0; i < n; ++i) {
        const Real d = coreDistance(cs, parts.pos[i]);
        if (d < 0) {
          newRegion[i] = 1;
          keep.push_back(static_cast<int>(i));
        } else if (d < C.bufferWidth) {
          newRegion[i] = 2;
          keep.push_back(static_cast<int>(i));
        }
      }
      const size_t removed = n - keep.size();
      if (removed > 0 && cs.mode == "adaptive") {
        auto gather = [&](auto& vec) {
          using VecT = std::decay_t<decltype(vec)>;
          using T = typename VecT::value_type;
          std::vector<T> out;
          out.reserve(keep.size());
          for (const int i : keep) out.push_back(vec[i]);
          vec.swap(out);
        };
        gather(parts.pos);
        gather(parts.volume);
        gather(parts.dp);
        gather(parts.h);
        gather(parts.level);
        gather(parts.category);
        gather(parts.bcType);
        gather(parts.patch);
        std::vector<int> reg;
        reg.reserve(keep.size());
        for (const int i : keep) reg.push_back(newRegion[i]);
        parts.region.swap(reg);
        warnings.push_back("adaptive particle domain removed " + std::to_string(removed) +
                           " particles now covered by the FVM region");
        computeConsistency(spec, parts, timer);
      } else {
        parts.region.swap(newRegion);
      }
      for (size_t i = 0; i < parts.region.size(); ++i) {
        if (parts.region[i] == 1) parts.category[i] = PCAT_CORE;
        else if (parts.region[i] == 2) parts.category[i] = PCAT_BUFFER;
        else parts.category[i] = PCAT_INTERIOR;
      }
    }
  }

  if (!mesh.valid || !parts.valid) return C;

  // ---- operators --------------------------------------------------------
  std::vector<int> cplCells, cplParts;
  for (size_t c = 0; c < mesh.cells.size(); ++c)
    if (C.cellRegion[c] == 1 || C.cellRegion[c] == 2) cplCells.push_back(static_cast<int>(c));
  for (size_t p = 0; p < parts.pos.size(); ++p)
    if (parts.region[p] == 1 || parts.region[p] == 2) cplParts.push_back(static_cast<int>(p));
  if (cplCells.empty() || cplParts.empty()) {
    warnings.push_back("coupling region is empty: no overlap between particles and mesh");
    C.valid = true;
    return C;
  }

  {
    ScopedTimer st(timer, "couplingOperators");
    const Scheme sch = schemeFromName(cs.p2m);
    OpBuild primary = assembleP2M(mesh, parts, cplCells, sch, cs.momentCorrection, cs.quadrature,
                                  spec.sph.kernel);
    C.p2m = std::move(primary.op);
    C.p2mRowSumErr = primary.rowSumErr;
    OpBuild back = assembleM2P(mesh, parts, cplParts, cs.momentCorrection, spec.sph.kernel);
    C.m2p = std::move(back.op);
    C.m2pRowSumErr = back.rowSumErr;

    // particle volume fraction of each coupling cell (conservation diagnostic)
    for (const int c : cplCells) {
      Real v = 0;
      for (int k = C.p2m.rowPtr[c]; k < C.p2m.rowPtr[c + 1]; ++k) v += parts.volume[C.p2m.col[k]];
      C.cellVolumeFrac[c] = mesh.vol[c] > 0 ? std::min(2.0, v / mesh.vol[c]) : 0;
    }

    // ---- patch test over all schemes -----------------------------------
    if (cs.patchTest) {
      ScopedTimer st2(timer, "couplingPatchTest");
      struct Variant {
        std::string name;
        Scheme scheme;
        bool corr;
      };
      const std::vector<Variant> variants = {
          {"nearest", Scheme::Nearest, false},
          {"shepard", Scheme::Shepard, false},
          {"shepard+moment", Scheme::Shepard, true},
          {"opmis(overlap)", Scheme::Overlap, false},
          {"opmis+moment", Scheme::Overlap, true},
      };
      const std::vector<std::string> fieldNames = {"1", "x", "y", "x^2+y^2", "sin*sin"};
      C.test.schemes.clear();
      C.test.fields = fieldNames;
      C.test.l2.assign(variants.size() * fieldNames.size(), 0.0);
      std::vector<Real> pvals(parts.pos.size());
      for (size_t vi = 0; vi < variants.size(); ++vi) {
        C.test.schemes.push_back(variants[vi].name);
        OpBuild ob = assembleP2M(mesh, parts, cplCells, variants[vi].scheme, variants[vi].corr,
                                 cs.quadrature, spec.sph.kernel);
        for (size_t fi = 0; fi < fieldNames.size(); ++fi) {
          for (size_t i = 0; i < parts.pos.size(); ++i)
            pvals[i] = analyticField(static_cast<int>(fi), parts.pos[i]);
          C.test.l2[vi * fieldNames.size() + fi] =
              patchError(ob.op, pvals, cplCells, mesh, parts, static_cast<int>(fi), true);
        }
      }
    }
  }

  for (size_t p = 0; p < parts.pos.size(); ++p)
    if (parts.region[p] == 2) C.interfaceParticles.push_back(static_cast<int>(p));
  C.nCoreParticles = parts.nCore;
  C.nBufferParticles = parts.nBuffer;

  C.valid = true;
  return C;
}

}  // namespace mesher
