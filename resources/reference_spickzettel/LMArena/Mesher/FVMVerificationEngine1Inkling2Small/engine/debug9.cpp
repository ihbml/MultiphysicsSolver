// Debug 9: residual of RC assembly AT the manufactured solution
#include <iostream>
#include <iomanip>
#include <vector>
#include <cmath>
#include "../engine/mesh.h"
#include "../engine/adjoint.h"
using namespace vv;
using namespace std;

int main() {
  DistortedMesh M = calibrateAndBuild(6, 0x5EEDuLL, 30.0, 0.35);
  MeshCache C = buildMeshCache(M);
  QuadData Q = buildQuadData(M);
  RCGradSet GS = buildRCGradients(M);
  const int nc = M.cells.size(), d = 4 * nc;
  // design point
  RCParams<double> sp{NU_REF, 1.0};
  RCSys<double> S = assembleRC<double>(M, GS, C, Q, sp, true);
  vector<double> xEx(d);
  for (int i = 0; i < nc; ++i) {
    Vec3 U = uExact(M.cells[i].centroid.x, M.cells[i].centroid.y, M.cells[i].centroid.z);
    xEx[4 * i] = U.x; xEx[4 * i + 1] = U.y; xEx[4 * i + 2] = U.z;
    xEx[4 * i + 3] = pStokes(M.cells[i].centroid.x, M.cells[i].centroid.y, M.cells[i].centroid.z);
  }
  vector<double> r = S.rhs;
  for (int i = 0; i < d; ++i) r[i] = -r[i];
  for (auto& [rr, cc, v] : S.triplets) r[rr] += v * xEx[cc];
  double rMomMax = 0, rCntMax = 0;
  size_t worstMom = 0, worstCnt = 0;
  for (int i = 0; i < nc; ++i) {
    for (int q = 0; q < 3; ++q) {
      double v = fabs(r[4 * i + q]);
      if (v > rMomMax) { rMomMax = v; worstMom = 4 * i + q; }
    }
    double v = fabs(r[4 * i + 3]);
    if (v > rCntMax) { rCntMax = v; worstCnt = 4 * i + 3; }
  }
  cout << "residual@exact: momentum max = " << rMomMax << " (row " << worstMom << ")\n";
  cout << "residual@exact: continuity max = " << rCntMax << " (row " << worstCnt << ")\n";
  Vec3 cw = M.cells[worstMom / 4].centroid;
  cout << "worst mom cell (" << cw.x << "," << cw.y << "," << cw.z << ")\n";
  Vec3 cc2 = M.cells[worstCnt / 4].centroid;
  cout << "worst cnt cell (" << cc2.x << "," << cc2.y << "," << cc2.z << ")\n";
  // print residual rows of a few cells fully
  for (int i : {0, 1}) {
    cout << "cell " << i << " residual rows: m=(" << r[4*i] << "," << r[4*i+1] << "," << r[4*i+2] << ") cnt=" << r[4*i+3] << "\n";
  }
  // compare against reference BC: check a boundary mom row const parts
  return 0;
}
