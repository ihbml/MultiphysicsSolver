// Debug harness: uniform mesh sanity tests for the FVM discretization
#include <iostream>
#include <iomanip>
#include "../engine/mesh.h"
#include "../engine/assembly.h"
#include "../engine/adjoint.h"
using namespace vv;
using namespace std;

static void runCase(const char* name, int N, double alpha, double gamma, double u0, double sigma) {
  DistortedMesh M = buildMesh(N, alpha, 12345, 1.0);  // alpha=0 -> uniform
  MeshCache C = buildMeshCache(M);
  QuadData Q = buildQuadData(M);
  GradientData G = buildGradientData(M, C.phiB);
  CorrectionFlags flags{false, true, true};
  ScalarParams<double> sp{gamma, u0, sigma};
  Sys<double> S = assembleScalar<double>(M, G, C, Q, sp, flags);
  // dense solve for small N; bicgstab otherwise
  vector<double> phi;
  if (N <= 10) {
    vector<double> A = tripletsToDense(S);
    vector<int> perm;
    if (!luFactor(A, S.n, perm)) { cout << "LU FAIL\n"; return; }
    phi = luSolve(A, perm, S.n, S.rhs);
  } else {
    CSR A = tripletsToCSR(S);
    BiCGResult br = bicgstab(A, S.rhs, 1e-13, 20000);
    phi = br.x;
    cerr << name << " bicgstab iters=" << br.iters << " relres=" << br.relResid << "\n";
  }
  double se = 0, vc = 0, linf = 0;
  size_t worst = 0;
  for (size_t i = 0; i < M.cells.size(); ++i) {
    double e = phi[i] - Q.cells[i].phiStar;
    se += M.cells[i].volume * e * e; vc += M.cells[i].volume;
    if (fabs(e) > linf) { linf = fabs(e); worst = i; }
  }
  cout << fixed << setprecision(6) << "[" << name << "] N=" << N << " alpha=" << alpha
       << " L2=" << sqrt(se / vc) << " Linf=" << linf << "\n";
}

int main() {
  // uniform diffusion-only (u=0): manufactured phi = sin(pi x)cos(pi y)e^{z/2}
  runCase("diff-only uniform 8", 8, 0.0, 0.1, 0.0, 1.0);
  runCase("diff-only uniform 16", 16, 0.0, 0.1, 0.0, 1.0);
  runCase("advdiff uniform 8", 8, 0.0, 0.1, 1.0, 1.0);
  runCase("advdiff uniform 16", 16, 0.0, 0.1, 1.0, 1.0);
  runCase("advdiff uniform 32", 32, 0.0, 0.1, 1.0, 1.0);
  // distorted
  runCase("advdiff distorted 8", 8, 0.55, 0.1, 1.0, 1.0);
  runCase("advdiff distorted 16", 16, 0.55, 0.1, 1.0, 1.0);
  // pure diffusion distorted
  runCase("diff distorted 8", 8, 0.55, 0.1, 0.0, 1.0);
  runCase("diff distorted 16", 16, 0.55, 0.1, 0.0, 1.0);
  return 0;
}
