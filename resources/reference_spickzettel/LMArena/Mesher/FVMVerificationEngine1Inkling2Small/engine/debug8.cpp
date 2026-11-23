// Debug 8: Rhie-Chow system diagnostics
#include <iostream>
#include <iomanip>
#include "../engine/mesh.h"
#include "../engine/adjoint.h"
using namespace vv;
using namespace std;

int main() {
  DistortedMesh M = calibrateAndBuild(6, 0x5EEDuLL, 30.0, 0.35);
  MeshCache C = buildMeshCache(M);
  QuadData Q = buildQuadData(M);
  RCGradSet GS = buildRCGradients(M);
  cout << "cells=" << M.cells.size() << " maxAng=" << M.maxAngleDeg << "\n";
  const int nc = M.cells.size(), d = 4 * nc;
  RCParams<double> sp{0.3, 1.6};
  RCSys<double> S = assembleRC<double>(M, GS, C, Q, sp, true);
  vector<double> A((size_t)d * d, 0.0);
  for (auto& [r, c, v] : S.triplets) A[(size_t)r * d + c] += v;
  vector<int> perm;
  bool ok = luFactor(A, d, perm);
  cout << "LU ok=" << ok << "\n";
  vector<double> x = luSolve(A, perm, d, S.rhs);
  double umax = 0, pmin = 1e30, pmax = -1e30;
  for (int i = 0; i < nc; ++i) {
    for (int q = 0; q < 3; ++q) umax = max(umax, fabs(x[4 * i + q]));
    pmin = min(pmin, x[4 * i + 3]); pmax = max(pmax, x[4 * i + 3]);
  }
  cout << "|U|max=" << umax << " p in [" << pmin << "," << pmax << "]\n";
  // residual
  vector<double> r = S.rhs;
  for (int i = 0; i < d; ++i) r[i] = -r[i];
  for (auto& [rr, cc, v] : S.triplets) r[rr] += v * x[cc];
  double rn = 0; for (double v : r) rn = max(rn, fabs(v));
  cout << "max|residual| = " << rn << "\n";
  // sample cells: U vs U*
  for (int i : {0, 100, 200}) {
    cout << "cell " << i << " U=(" << x[4*i] << "," << x[4*i+1] << "," << x[4*i+2] << ") p=" << x[4*i+3]
         << " | U* ctr=(" << Q.cells[i].pStarU.x << "," << Q.cells[i].pStarU.y << "," << Q.cells[i].pStarU.z << ") p*=" << Q.cells[i].pStarScalar << "\n";
  }
  // design point check: nu=NU_REF, sigma=1 -> J should be near truncation error
  RCParams<double> sp2{NU_REF, 1.0};
  RCSys<double> S2 = assembleRC<double>(M, GS, C, Q, sp2, true);
  vector<double> A2((size_t)d * d, 0.0);
  for (auto& [r, c, v] : S2.triplets) A2[(size_t)r * d + c] += v;
  vector<int> perm2;
  luFactor(A2, d, perm2);
  vector<double> x2 = luSolve(A2, perm2, d, S2.rhs);
  double J2 = 0;
  for (int i = 0; i < nc; ++i) {
    double V = M.cells[i].volume;
    double du0 = x2[4*i] - Q.cells[i].pStarU.x, du1 = x2[4*i+1] - Q.cells[i].pStarU.y, du2 = x2[4*i+2] - Q.cells[i].pStarU.z;
    double dp = x2[4*i+3] - Q.cells[i].pStarScalar;
    J2 += V * (du0*du0 + du1*du1 + du2*du2 + dp*dp);
  }
  double umax2 = 0, pmax2 = 0;
  for (int i = 0; i < nc; ++i) { for (int q = 0; q < 3; ++q) umax2 = max(umax2, fabs(x2[4*i+q])); pmax2 = max(pmax2, fabs(x2[4*i+3])); }
  cout << "design J=" << J2 << " |U|max=" << umax2 << " |p|max=" << pmax2 << "\n";
  return 0;
}
