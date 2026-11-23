// =============================================================================
//  CLUSTER A - 3 :: multi-region conjugate heat transfer across mappedWall
//                   twin faces (conformal + non-conformal supermesh-AMI)
//
//  Over-relapped face decomposition (exact, orthogonal-consistent):
//      d_f   = x_f - x_P                       (cell centre -> interface face)
//      a_f   = |S_f|^2 / (S_f . d_f)           ( > 0 always, = |S_f|/d_f orth.)
//      k_f^corr = S_f - a_f d_f                ( = 0 on orthogonal meshes )
//
//  Harmonic-resistance coupled flux (fluid cell P  <->  solid cell S):
//      C    = k_f a_f k_s a_s / (k_f a_f + k_s a_s)
//      q_f->s = C [ (T_S - T_P) + (gradT_f . k_f^corr)/a_f
//                          + (gradT_s . k_s^corr)/a_s ]
//      T_interface = ( k_f a_f T_P + k_s a_s T_S + dq_skew ) / (k_f a_f + k_s a_s )
//      dq_skew = k_f (gradT_f . k_f^corr) + k_s (gradT_s . k_s^corr)
//
//  Two solution strategies are provided:
//   (a) MONOLITHIC : a_PS^CHT is inserted directly into one unified CSR
//       energy matrix  =>  steady CHT converges in a *single* linear solve.
//   (b) PARTITIONED : under-relaxed Robin-Robin interface BC for transient
//       multi-timescale CHT (fluid at dt=1e-4 s, solid at dt=10 s).
// =============================================================================
#pragma once
#include "cluster_a_core.hpp"
#include "cluster_a_wall.hpp"

namespace cla {
namespace cht {

// --------------------------------------------------------- geometric factors
struct FaceFactors {
  Real  alpha{0};            // a_f  [m]
  Vec3  kcorr{0,0,0};        // k_f^corr [m^2]
  Vec3  d{0,0,0};            // d_f [m]
  Real  area{0};             // |S_f| [m^2]
};
inline FaceFactors faceFactors(const PolyMesh& m, Idx f, Idx c) {
  FaceFactors ff;
  const Vec3 Sf = m.outward(f, c);            // flip if c is the neighbour!
  ff.area = norm(Sf);
  ff.d    = m.face.C[f] - m.cC[c];
  const Real dS = dot(Sf, ff.d);
  ff.alpha = std::abs(dS) < kSmall ? ff.area / std::max(norm(ff.d), kSmall)
                                   : (ff.area * ff.area) / dS;
  ff.kcorr = Sf - ff.d * ff.alpha;
  return ff;
}

// --------------------------------------------------------------- twin coupling
// One entry per (fluid face, solid face) *overlap* of the supermesh / AMI.
// Conservation requires  sum_j wf_j = 1  over every fluid face and
//                         sum_j ws_j = 1  over every solid face.
// Face conductances (W/K):
//      G_f = k_f alpha_f wf ,     G_s = k_s alpha_s ws
//      C   = G_f G_s / (G_f + G_s)                     (harmonic resistance)
// Interface flux and temperature (exact, both signs consistent):
//      q_f->s = C [ (T_S - T_P) + (gradT_f . k_f^corr)/alpha_f
//                              + (gradT_s . k_s^corr)/alpha_s ]
//      T_int  = ( G_f T_P + G_s T_S + dq_skew ) / (G_f + G_s)
//      dq_skew= k_f wf (gradT_f . k_f^corr) + k_s ws (gradT_s . k_s^corr)
struct TwinPair {
  Idx  cellF{0}, cellS{0};       // local cell indices inside each region
  Idx  faceF{0}, faceS{0};       // local face indices inside each region
  Real wf{1.0}, ws{1.0};         // AMI weights
  Real alphaF{0}, alphaS{0};     // full-face alpha = |S|^2/(S.d)  [m]
  Real kf{0}, ks{0};             // conductivities [W/m/K]
  Vec3 kcorrF{0,0,0}, kcorrS{0,0,0};   // skew vectors S - alpha d
  Real Gf{0}, Gs{0}, C{0};       // conductances / harmonic coupling [W/K]
  Real gradFkcorr{0}, gradSkcorr{0};
  Real dqSkew{0};
};

inline void finalisePair(TwinPair& p, const Vec3& gTf, const Vec3& gTs) {
  p.Gf = p.kf * p.alphaF * p.wf;
  p.Gs = p.ks * p.alphaS * p.ws;
  p.C  = (p.Gf * p.Gs) / std::max(p.Gf + p.Gs, kSmall);
  p.gradFkcorr = dot(gTf, p.kcorrF);
  p.gradSkcorr = dot(gTs, p.kcorrS);
  p.dqSkew     = p.kf * p.wf * p.gradFkcorr + p.ks * p.ws * p.gradSkcorr;
}

// --------------------------------------------------- multi-region CSR system
struct MultiRegion {
  std::vector<const PolyMesh*> region;
  std::vector<Idx>  offset;                    // offset[r] -> first global row
  std::vector<Field> T;                        // per region temperature
  std::vector<Field> kappa;                    // per region conductivity
  std::vector<std::vector<Vec3>> gradT;
  Idx nGlobal = 0;
  LinSys L;

  void addRegion(const PolyMesh& m, const Field& k) {
    offset.push_back(nGlobal);
    region.push_back(&m);
    T.emplace_back(m.nCells, 300.0);
    gradT.emplace_back(m.nCells, Vec3{});
    kappa.push_back(k);
    nGlobal += m.nCells;
  }
  Idx g(Idx r, Idx c) const { return offset[r] + c; }

  void build(const std::vector<std::pair<Idx, Idx>>& extra = {}) {
    // Build one unified CSR pattern spanning all regions (monolithic CHT).
    std::vector<std::vector<Idx>> row(static_cast<std::size_t>(nGlobal));
    for (Idx r = 0; r < static_cast<Idx>(region.size()); ++r) {
      const PolyMesh& m = *region[r];
      for (Idx c = 0; c < m.nCells; ++c) {
        const Idx gr = g(r, c);
        row[gr].push_back(gr);
        for (Idx k = m.csr.start[c]; k < m.csr.start[c + 1]; ++k)
          if (m.other(m.csr.faces[k], c) >= 0) row[gr].push_back(g(r, m.other(m.csr.faces[k], c)));
      }
    }
    for (auto& [a, b] : extra) { row[a].push_back(b); row[b].push_back(a); }
    for (auto& r : row) { std::sort(r.begin(), r.end()); r.erase(std::unique(r.begin(), r.end()), r.end()); }
    L.n = nGlobal;
    L.rstart.assign(static_cast<std::size_t>(nGlobal) + 1, 0);
    for (Idx i = 0; i < nGlobal; ++i) L.rstart[i + 1] = L.rstart[i] + static_cast<Idx>(row[i].size());
    L.cols.resize(L.rstart[nGlobal]); L.vals.assign(L.rstart[nGlobal], 0.0);
    for (Idx i = 0; i < nGlobal; ++i) for (std::size_t k = 0; k < row[i].size(); ++k) L.cols[L.rstart[i] + k] = row[i][k];
    L.b.assign(nGlobal, 0.0); L.x.assign(nGlobal, 0.0); L.diag.assign(nGlobal, 0.0);
  }

  // Laplacian  -div(k grad T)  for one region, assembled into the global rows
  void assembleConduction(Idx r, const Field& source) {
    const PolyMesh& m = *region[r];
    for (Idx c = 0; c < m.nCells; ++c) {
      const Idx gr = g(r, c);
      for (Idx k = m.csr.start[c]; k < m.csr.start[c + 1]; ++k) {
        const Idx f = m.csr.faces[k];
        const Idx nb = m.other(f, c);
        const FaceFactors ff = faceFactors(m, f, c);
        const Real kf = 0.5 * (kappa[r][c] + (nb >= 0 ? kappa[r][nb] : kappa[r][c]));
        const Real aij = kf * ff.alpha;                 // orthogonal part
        const Vec3 kc  = ff.kcorr;
        L.add(gr, gr, aij);
        if (nb >= 0) {
          L.add(gr, g(r, nb), -aij);
          // deferred (explicit) non-orthogonal correction -> right hand side
          const Vec3 gN = 0.5 * (gradT[r][c] + gradT[r][nb]);
          L.b[gr] -= kf * dot(gN, kc);
        } else {
          L.b[gr] -= kf * dot(gradT[r][c], kc);
        }
      }
      L.b[gr] += source[c] * m.cV[c];
    }
  }

  // ------------------------------------------------ MONOLITHIC CHT couplings
  // Inserts  a_PS^CHT = -C w  and  a_PP^CHT = +C w  directly into the unified
  // matrix (plus the skewness right-hand side).  Steady state is then reached
  // in one linear solve - no outer iteration on the interface at all.
  // ------------------------------------------------ MONOLITHIC CHT couplings
  // Inserts  a_PS^CHT = -C  and  a_PP^CHT = +C  directly into the unified
  // matrix (plus the skewness right-hand side).  The matrix is then symmetric,
  // negative definite and *strongly* diagonally dominant in the interface rows,
  // so steady CHT is reached in a single linear solve - no outer iteration on
  // the interface temperature at all.
  void assembleMonolithicCHT(std::vector<TwinPair>& pairs) {
    for (auto& p : pairs) {
      finalisePair(p, gradT[0][p.cellF], gradT[1][p.cellS]);
      const Idx grf = g(0, p.cellF), grs = g(1, p.cellS);
      L.add(grf, grf,  p.C);
      L.add(grs, grs,  p.C);
      L.add(grf, grs, -p.C);
      L.add(grs, grf, -p.C);
      // skew correction is explicit, symmetric and exactly conservative
      const Real qSkew = p.C * (p.gradFkcorr / std::max(p.alphaF, kSmall) +
                                p.gradSkcorr / std::max(p.alphaS, kSmall));
      L.b[grf] -= qSkew;
      L.b[grs] += qSkew;
    }
  }

  Real interfaceFlux(const TwinPair& p) const {
    return p.C * ((T[1][p.cellS] - T[0][p.cellF]) +
                  p.gradFkcorr / std::max(p.alphaF, kSmall) +
                  p.gradSkcorr / std::max(p.alphaS, kSmall));
  }
  Real interfaceTemperature(const TwinPair& p) const {
    return (p.Gf * T[0][p.cellF] + p.Gs * T[1][p.cellS] + p.dqSkew) /
           std::max(p.Gf + p.Gs, kSmall);
  }
};

// ------------------------------------------------- PARTITIONED Robin-Robin
// Interface condition written as a Robin (mixed) BC on each side:
//      fluid :  q = C ( T_P - T_int^* )      solid :  q = C ( T_S - T_int^* )
// with T_int^* the predictor from the opposite region and under-relaxation
//      T_int^{n+1} = (1-a) T_int^n + a T_int^{n+1,*}
struct PartitionedCHT {
  Real alpha{0.7};                 // interface relaxation
  Real tolerance{1e-10};
  Idx  maxOuter{400};
  std::vector<Real> residualHistory;
  Idx  iterations{0};
  bool converged{false};
};

// One Robin-Robin sweep on the fluid region only (the solid mirror is obtained
// by swapping the arguments).  Returns the residual of the interface flux.
inline Real robinRobinSweep(LinSys& Lf, const std::vector<Idx>& rows,
                            const std::vector<Real>& Cw,
                            const std::vector<Real>& Tneighbour) {
  Real res = 0;
  for (std::size_t i = 0; i < rows.size(); ++i) {
    Lf.add(rows[i], rows[i], Cw[i]);
    Lf.b[rows[i]] += Cw[i] * Tneighbour[i];
    res = std::max(res, std::abs(Cw[i] * Tneighbour[i]));
  }
  return res;
}

// ------------------------------------------------------ analytic composite wall
// Series resistance of n layers with convective films on both faces:
//   q = (T_h - T_c) / ( 1/h_h + sum_i L_i/k_i + 1/h_c )
struct CompositeWall {
  std::vector<Real> L, k;                     // thickness [m], conductivity
  std::vector<std::string> name;
  Real hHot{0}, hCold{0}, THot{0}, TCold{0};  // convective films [W/m2K], [K]

  Real totalResistance() const {
    Real R = 0;
    if (hHot  > 0) R += 1.0 / hHot;
    if (hCold > 0) R += 1.0 / hCold;
    for (std::size_t i = 0; i < L.size(); ++i) R += L[i] / k[i];
    return R;
  }
  Real q() const { return (THot - TCold) / totalResistance(); }
  // Temperature at the interface between layer i-1 and layer i (i=0 -> hot face)
  std::vector<Real> interfaceTemperatures() const {
    std::vector<Real> T(static_cast<std::size_t>(L.size()) + 1);
    const Real flux = q();
    Real Tcur = THot;
    if (hHot > 0) Tcur -= flux / hHot;
    T[0] = Tcur;
    for (std::size_t i = 0; i < L.size(); ++i) { Tcur -= flux * L[i] / k[i]; T[i + 1] = Tcur; }
    return T;
  }
};

}  // namespace cht
}  // namespace cla
