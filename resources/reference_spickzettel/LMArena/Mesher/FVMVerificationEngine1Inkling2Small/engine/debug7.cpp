// Debug 7: verify affinity + transpose solve + adjoint identity numerically
#include <iostream>
#include <iomanip>
#include <cmath>
#include "../engine/mesh.h"
#include "../engine/assembly.h"
using namespace vv;
using namespace std;

static vector<double> matVecDense(const vector<double>& A, int n, const vector<double>& x) {
  vector<double> y(n, 0.0);
  for (int r = 0; r < n; ++r) for (int c = 0; c < n; ++c) y[r] += A[(size_t)r * n + c] * x[c];
  return y;
}

int main() {
  DistortedMesh M = calibrateAndBuild(8, 20240917ULL, 45.0, 0.55);
  MeshCache C = buildMeshCache(M);
  QuadData Q = buildQuadData(M);
  GradientData G = buildGradientData(M, C.phiB);
  CorrectionFlags flags{false, true, true};
  const int n = (int)M.cells.size();
  // affinity of A: A(g,u) == g A(1,0) + u A(0,1)
  ScalarParams<double> p10{1.0, 0.0, 0.0}, p01{0.0, 1.0, 0.0}, pgf{0.3, 0.7, 0.0}, pgf0{0.3, 0.7, 0.0};
  (void)pgf0;
  Sys<double> s10 = assembleScalar<double>(M, G, C, Q, p10, flags);
  Sys<double> s01 = assembleScalar<double>(M, G, C, Q, p01, flags);
  Sys<double> sgf = assembleScalar<double>(M, G, C, Q, pgf, flags);
  vector<double> A10 = tripletsToDense(s10), A01 = tripletsToDense(s01), Agf = tripletsToDense(sgf);
  double affErr = 0;
  for (size_t i = 0; i < A10.size(); ++i) affErr = max(affErr, fabs(Agf[i] - (0.3 * A10[i] + 0.7 * A01[i])));
  cout << "A affinity residual: " << affErr << "\n";
  // A(0,0) == 0?
  Sys<double> s00 = assembleScalar<double>(M, G, C, Q, ScalarParams<double>{0.0, 0.0, 0.0}, flags);
  double z = 0; for (auto& [r, c, v] : s00.triplets) z += fabs(v);
  cout << "A(0,0) nonzero sum: " << z << "\n";
  // transpose solve: A^T x = b  =>  A (x^T^T) check via dense transpose
  vector<double> A = Agf;
  vector<int> perm;
  luFactor(A, n, perm);
  vector<double> b(n);
  for (int i = 0; i < n; ++i) b[i] = sin(i * 1.7) * 0.5 + 0.25;
  vector<double> xt = luSolveT(A, perm, n, b);
  // check: A^T xt == b using fresh copy
  vector<double> Af = tripletsToDense(sgf);
  vector<double> At(n * (size_t)n);
  for (int r = 0; r < n; ++r) for (int c = 0; c < n; ++c) At[(size_t)c * n + r] = Af[(size_t)r * n + c];
  vector<double> chk = matVecDense(At, n, xt);
  double terr = 0; for (int i = 0; i < n; ++i) terr = max(terr, fabs(chk[i] - b[i]));
  cout << "A^T solve residual: " << terr << "\n";
  // b affinity check: b(g,u,s) == ?
  Sys<double> b100 = assembleScalar<double>(M, G, C, Q, ScalarParams<double>{1.0, 0.0, 1.0}, flags);  // -qLap - C1
  Sys<double> b010 = assembleScalar<double>(M, G, C, Q, ScalarParams<double>{0.0, 1.0, 1.0}, flags);  // qDiv - C2
  Sys<double> b200 = assembleScalar<double>(M, G, C, Q, ScalarParams<double>{0.17, 1.35, 1.0}, flags);
  Sys<double> b300 = assembleScalar<double>(M, G, C, Q, ScalarParams<double>{0.17, 1.35, 0.0}, flags);
  double bErr = 0;
  for (int i = 0; i < n; ++i) {
    double lin = 1.0 * b100.rhs[i] + 0.0;  // placeholder
    (void)lin;
    bErr = max(bErr, fabs(b200.rhs[i] - (0.17 * b100.rhs[i] + 1.35 * b010.rhs[i])));
  }
  cout << "b(0.17,1.35,1) vs 0.17*B10 + 1.35*B01: " << bErr << "\n";
  double bErr2 = 0;
  for (int i = 0; i < n; ++i) bErr2 = max(bErr2, fabs(b200.rhs[i] - (b300.rhs[i] + 1.0 * (b200.rhs[i] - b300.rhs[i]))));
  cout << "b sigma weight sanity: " << bErr2 << "\n";
  // direct FD of rhs wrt sigma
  Sys<double> b201 = assembleScalar<double>(M, G, C, Q, ScalarParams<double>{0.17, 1.35, 1.001}, flags);
  Sys<double> b200z = assembleScalar<double>(M, G, C, Q, ScalarParams<double>{0.17, 1.35, 0.999}, flags);
  double dsigma = 0;
  for (int i = 0; i < n; ++i) dsigma = max(dsigma, fabs((b201.rhs[i] - b200z.rhs[i]) / 0.002 - (b200.rhs[i] - b300.rhs[i])));
  cout << "d b / d sigma FD check: " << dsigma << "\n";
  return 0;
}
