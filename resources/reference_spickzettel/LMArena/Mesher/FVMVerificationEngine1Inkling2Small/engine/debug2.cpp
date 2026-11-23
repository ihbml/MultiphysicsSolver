// Debug harness 2: residual at exact vs computed solution
#include <iostream>
#include <iomanip>
#include "../engine/mesh.h"
#include "../engine/assembly.h"
using namespace vv;
using namespace std;

int main() {
  const int N = 8;
  DistortedMesh M = buildMesh(N, 0.0, 12345, 1.0);  // uniform
  MeshCache C = buildMeshCache(M);
  QuadData Q = buildQuadData(M);
  GradientData G = buildGradientData(M, C.phiB);
  CorrectionFlags flags{false, true, true};
  ScalarParams<double> sp{0.1, 0.0, 1.0};  // pure diffusion
  Sys<double> S = assembleScalar<double>(M, G, C, Q, sp, flags);
  vector<double> A = tripletsToDense(S);
  vector<int> perm;
  luFactor(A, S.n, perm);
  vector<double> phi = luSolve(A, perm, S.n, S.rhs);
  // residual at computed solution
  vector<double> rc(S.n, 0.0), re(S.n, 0.0);
  vector<double> phiStar(S.n);
  for (int i = 0; i < S.n; ++i) phiStar[i] = Q.cells[i].phiStar;
  for (auto& [r, c, v] : S.triplets) { rc[r] += v * phi[c]; re[r] += v * phiStar[c]; }
  double rcMax = 0, reMax = 0, rhsMax = 0;
  for (int i = 0; i < S.n; ++i) {
    rcMax = max(rcMax, fabs(rc[i] - S.rhs[i]));
    reMax = max(reMax, fabs(re[i] - S.rhs[i]));
    rhsMax = max(rhsMax, fabs(S.rhs[i]));
  }
  cout << "max|rhs| = " << rhsMax << "\n";
  cout << "residual @ computed = " << rcMax << "\n";
  cout << "residual @ phi*     = " << reMax << "\n";
  // volume-weighted L2 error of computed and consistency defect of phi*
  double se = 0, vc = 0;
  for (size_t i = 0; i < M.cells.size(); ++i) { double e = phi[i] - phiStar[i]; se += M.cells[i].volume * e * e; vc += M.cells[i].volume; }
  cout << "L2 err = " << sqrt(se / vc) << "\n";
  // worst-error cell info
  size_t worst = 0; double linf = 0;
  for (size_t i = 0; i < M.cells.size(); ++i) { double e = fabs(phi[i] - phiStar[i]); if (e > linf) { linf = e; worst = i; } }
  Vec3 cw = M.cells[worst].centroid;
  cout << "worst cell at (" << cw.x << "," << cw.y << "," << cw.z << ") phi=" << phi[worst] << " phi*=" << phiStar[worst] << "\n";
  // error on a z-line: cells with k=0 vs k=N/2 vs k=N-1 at (0.5,0.5)
  for (int kz : {0, N / 2, N - 1}) {
    int i = M.cellId(N / 2, N / 2, kz);
    cout << "cell k=" << kz << " z=" << M.cells[i].centroid.z << " phi=" << phi[i] << " phi*=" << phiStar[i] << "\n";
  }
  // sum of volumes check and sum phi* vs phi
  double sphi = 0, sphiStar = 0;
  for (int i = 0; i < S.n; ++i) { sphi += phi[i]; sphiStar += phiStar[i]; }
  cout << "sum phi = " << sphi << "  sum phi* = " << sphiStar << "\n";
  return 0;
}
