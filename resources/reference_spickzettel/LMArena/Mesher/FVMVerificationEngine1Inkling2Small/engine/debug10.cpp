// Debug 10: RC system sensitivity on uniform vs distorted mesh
#include <iostream>
#include <iomanip>
#include "../engine/mesh.h"
#include "../engine/adjoint.h"
using namespace vv;
using namespace std;

template <typename T>
using std::max;

int main() {
  for (double alpha : {0.0, 0.15, 0.3}) {
    DistortedMesh M = buildMesh(6, alpha, 0x5EEDuLL, 1.0);
    MeshCache C = buildMeshCache(M);
    QuadData Q = buildQuadData(M);
    RCGradSet GS = buildRCGradients(M);
    const int nc = M.cells.size(), d = 4 * nc;
    RCParams<double> sp{NU_REF, 1.0};
    RCSys<double> S = assembleRC<double>(M, GS, C, Q, sp, true);
    vector<double> A((size_t)d * d, 0.0);
    for (auto& [r, c, v] : S.triplets) A[(size_t)r * d + c] += v;
    vector<int> perm; luFactor(A, d, perm);
    vector<double> x = luSolve(A, perm, d, S.rhs);
    double J = 0, umax = 0, pmax = 0;
    for (int i = 0; i < nc; ++i) {
      double V = M.cells[i].volume;
      double du0 = x[4*i] - Q.cells[i].pStarU.x, du1 = x[4*i+1] - Q.cells[i].pStarU.y, du2 = x[4*i+2] - Q.cells[i].pStarU.z;
      double dp = x[4*i+3] - Q.cells[i].pStarScalar;
      J += V * (du0*du0 + du1*du1 + du2*du2 + dp*dp);
      umax = max(umax, max(max(fabs(x[4*i]), fabs(x[4*i+1])), fabs(x[4*i+2])));
      pmax = max(pmax, fabs(x[4*i+3]));
    }
    cout << "alpha=" << alpha << " maxAng=" << M.maxAngleDeg << " deg   design J=" << scientific << J
         << "  |U|max=" << umax << "  |p|max=" << pmax << "\n";
  }
  return 0;
}
