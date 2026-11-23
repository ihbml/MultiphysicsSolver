#include <iostream>
#include <iomanip>
#include "../engine/mesh.h"
#include "../engine/assembly.h"
using namespace vv;
using namespace std;
static void runLevel(int N, double u0, const CorrectionFlags& flags, DistortedMesh* pre=nullptr) {
  (void)pre;
  DistortedMesh M = buildMesh(N, 0.55, 20240917ULL, 1.0);  // fixed strong distortion scale 1.0
  // cap scale for very distorted: skip invalid check here (calibration handled in main)
  MeshCache C = buildMeshCache(M);
  QuadData Q = buildQuadData(M);
  GradientData G = buildGradientData(M, C.phiB);
  ScalarParams<double> sp{0.1, u0, 1.0};
  Sys<double> S = assembleScalar<double>(M, G, C, Q, sp, flags);
  vector<double> phi;
  if (N <= 8) {
    vector<double> A = tripletsToDense(S);
    vector<int> perm;
    if (!luFactor(A, S.n, perm)) { cout << "LU FAIL u0=" << u0 << "\n"; return; }
    phi = luSolve(A, perm, S.n, S.rhs);
  } else {
    CSR A = tripletsToCSR(S);
    BiCGResult br = bicgstab(A, S.rhs, 1e-13, 30000);
    phi = br.x;
    if (!br.converged) cerr << "warn: not converged u0=" << u0 << " N=" << N << "\n";
  }
  double se = 0, vc = 0;
  for (size_t i = 0; i < M.cells.size(); ++i) { double e = phi[i] - Q.cells[i].phiStar; se += M.cells[i].volume * e * e; vc += M.cells[i].volume; }
  cout << "u0=" << u0 << " N=" << N << " maxAng=" << M.maxAngleDeg << " L2=" << setprecision(5) << sqrt(se / vc) << "\n";
}
int main() {
  CorrectionFlags full{false,true,true}, noSkew{false,false,true}, noCorr{false,true,false};
  for (double u0 : {1.0, 2.0, 4.0}) {
    for (int N : {8, 16}) { runLevel(N, u0, full); runLevel(N, u0, noSkew); runLevel(N, u0, noCorr); }
    cout << "\n";
  }
  return 0;
}
