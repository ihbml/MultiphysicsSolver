// Debug 5: tune MMS configuration for the full operator (V3) across levels
#include <iostream>
#include <iomanip>
#include "../engine/mesh.h"
#include "../engine/assembly.h"
using namespace vv;
using namespace std;

static void runLevel(int N, double target, double gamma, uint64_t seed) {
  DistortedMesh M = calibrateAndBuild(N, seed, target, 0.55);
  MeshCache C = buildMeshCache(M);
  QuadData Q = buildQuadData(M);
  GradientData G = buildGradientData(M, C.phiB);
  CorrectionFlags flags{false, true, true};
  ScalarParams<double> sp{gamma, 1.0, 1.0};
  Sys<double> S = assembleScalar<double>(M, G, C, Q, sp, flags);
  vector<double> phi;
  if (N <= 8) {
    vector<double> A = tripletsToDense(S);
    vector<int> perm;
    luFactor(A, S.n, perm);
    phi = luSolve(A, perm, S.n, S.rhs);
  } else {
    CSR A = tripletsToCSR(S);
    BiCGResult br = bicgstab(A, S.rhs, 1e-13, 20000);
    phi = br.x;
  }
  double se = 0, vc = 0, linf = 0;
  for (size_t i = 0; i < M.cells.size(); ++i) {
    double e = phi[i] - Q.cells[i].phiStar;
    se += M.cells[i].volume * e * e; vc += M.cells[i].volume;
    linf = max(linf, fabs(e));
  }
  cout << "N=" << N << " target=" << target << " gamma=" << gamma << " maxAng=" << M.maxAngleDeg
       << " L2=" << setprecision(5) << sqrt(se / vc) << " Linf=" << linf << "\n";
}

int main() {
  const uint64_t seed = 20240917ULL;
  for (double target : {39.0, 36.0, 33.0})
    for (int N : {8, 16, 32}) runLevel(N, target, 0.1, seed);
  return 0;
}
