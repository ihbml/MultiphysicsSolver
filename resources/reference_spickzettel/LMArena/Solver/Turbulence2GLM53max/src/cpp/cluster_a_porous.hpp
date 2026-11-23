// =============================================================================
//  CLUSTER A - 4 :: anisotropic Darcy-Forchheimer porous zones
//
//  Local (principal) coordinate transformation of the resistance tensors:
//      D_global = R diag(d1,d2,d3) R^T ,      F_global = R diag(f1,f2,f3) R^T
//  Momentum sink, added *implicitly* to the momentum diagonal:
//      S = - mu D u - 1/2 rho |u| F u
//  Implicit treatment (unconditionally stable, no added Courant limit):
//      a_PP += ( mu D_ii + 0.5 rho |u| F_ii ) V      (diagonal part)
//      b_P  += ( mu D + 0.5 rho|u| F )_ij u_j V  , i != j   (explicit, small)
//  Ergun packed-bed coefficients:
//      d = 150 (1-eps)^2 / (eps^3 Dp^2) ,  f = 3.5 (1-eps) / (eps^3 Dp)
//      =>  -dp/dx = mu d u + 0.5 rho f |u| u      (Ergun equation, exactly)
// =============================================================================
#pragma once
#include "cluster_a_core.hpp"

namespace cla {
namespace porous {

// Rotation matrix from a streamwise unit vector e1 (and an optional e2 hint).
inline Mat3 rotationFromAxes(Vec3 e1, Vec3 e2hint = {0, 1, 0}) {
  Vec3 a = unit(e1);
  Vec3 b = unit(e2hint - a * dot(e2hint, a));
  if (norm(b) < 1e-6) { b = unit(Vec3{0, 0, 1} - a * dot(Vec3{0, 0, 1}, a)); }
  const Vec3 c = cross(a, b);
  Mat3 R;                                        // columns = a, b, c
  R.at(0,0)=a.x; R.at(0,1)=b.x; R.at(0,2)=c.x;
  R.at(1,0)=a.y; R.at(1,1)=b.y; R.at(1,2)=c.y;
  R.at(2,0)=a.z; R.at(2,1)=b.z; R.at(2,2)=c.z;
  return R;
}

struct Zone {
  Mat3 D{Mat3::zero()};                          // Darcy  [1/m^2]
  Mat3 F{Mat3::zero()};                          // Forchheimer [1/m]
  Mat3 R{Mat3::identity()};                      // principal axes
  Real porosity{1.0};
  bool anisotropic() const {
    return std::abs(D.at(0,1)) + std::abs(D.at(0,2)) + std::abs(D.at(1,2)) > 1e-14;
  }
  // Build the global tensors from principal values + rotation.
  void setPrincipal(Vec3 e1, Vec3 darcy, Vec3 forch, Vec3 e2hint = {0, 1, 0}) {
    R = rotationFromAxes(e1, e2hint);
    Mat3 Dl = Mat3::zero(), Fl = Mat3::zero();
    Dl.at(0,0)=darcy.x; Dl.at(1,1)=darcy.y; Dl.at(2,2)=darcy.z;
    Fl.at(0,0)=forch.x; Fl.at(1,1)=forch.y; Fl.at(2,2)=forch.z;
    D = matmul(R, matmul(Dl, transpose(R)));
    F = matmul(R, matmul(Fl, transpose(R)));
  }
  void setErgun(Real Dp, Real eps) {              // isotropic Ergun packed bed
    porosity = eps;
    const Real d = 150.0 * (1.0 - eps) * (1.0 - eps) / (eps * eps * eps * Dp * Dp);
    const Real f = 3.5  * (1.0 - eps)             / (eps * eps * eps * Dp);
    setPrincipal(Vec3{1,0,0}, Vec3{d,d,d}, Vec3{f,f,f});
  }
};

// Implicit sink assembly.  A = mu D_global + 0.5 rho |u| F_global
// (negative-definite for D >= 0, F >= 0, so the split below is unconditionally
//  stable and never destroys diagonal dominance of the momentum matrix).
struct SinkTerms {
  Real diag[3]{0, 0, 0};        // implicit contribution to a_PP per component
  Vec3 explicitPart{0, 0, 0};   // off-diagonal part, treated explicitly
  Mat3 A{Mat3::zero()};         // full sink tensor (diagnostics)
};

inline Mat3 sinkTensor(const Mat3& Dg, const Mat3& Fg, Vec3 u, Real mu, Real rho) {
  return Dg * mu + Fg * (0.5 * rho * norm(u));
}

inline SinkTerms sink(const Mat3& Dg, const Mat3& Fg, Vec3 u, Real mu, Real rho) {
  SinkTerms s;
  const Mat3 A = sinkTensor(Dg, Fg, u, mu, rho);
  s.A = A;
  for (int i = 0; i < 3; ++i) {
    s.diag[i] = A.at(i, i);
    Real& tgt = comp(s.explicitPart, i);
    for (int j = 0; j < 3; ++j) if (j != i) tgt -= A.at(i, j) * comp(u, j);
  }
  // Diagonal-dominance guard for strongly anisotropic tensors: the off-diagonal
  // magnitude is moved onto the main diagonal (Picard / Jacobian split), so the
  // implicit sink can never destabilise the momentum matrix.
  const Real spd = std::max(norm(u), 1e-30);
  for (int i = 0; i < 3; ++i) {
    Real offMag = 0;
    for (int j = 0; j < 3; ++j) if (j != i) offMag += std::abs(A.at(i, j)) * spd;
    s.diag[i] = std::max(s.diag[i], offMag);
  }
  return s;
}

// Analytic 1-D pressure gradient through an anisotropic bed.
inline Vec3 analyticPressureGradient(const Mat3& Dg, const Mat3& Fg,
                                     Vec3 u, Real mu, Real rho) {
  return (Dg * u) * mu + (Fg * u) * (0.5 * rho * norm(u));
}

}  // namespace porous
}  // namespace cla
