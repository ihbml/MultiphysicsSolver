// Debug 4: manual Jacobian print for one cell
#include <iostream>
#include <iomanip>
#include <cmath>
#include "../engine/mesh.h"
using namespace vv;
using namespace std;

int main() {
  const int N = 8;
  DistortedMesh M = buildMesh(N, 0.0, 12345, 1.0);
  const Cell& c = M.cells[0];
  Vec3 V[8];
  for (int i = 0; i < 8; ++i) V[i] = M.nodes[c.v[i]];
  for (int i = 0; i < 8; ++i) cout << "node " << i << " (" << V[i].x << "," << V[i].y << "," << V[i].z << ")\n";
  const double g = 1.0 / sqrt(3.0);
  double r = g, s = g, t = g;
  Vec3 xq, dxr, dxs, dxt;
  for (int i = 0; i < 8; ++i) {
    double sr = (i & 1) ? 1.0 : -1.0, ss = (i & 2) ? 1.0 : -1.0, st = (i & 4) ? 1.0 : -1.0;
    double Nsh = (1 + sr * r) * (1 + ss * s) * (1 + st * t) / 8.0;
    double Nr = sr * (1 + ss * s) * (1 + st * t) / 8.0;
    double Ns = (1 + sr * r) * ss * (1 + st * t) / 8.0;
    double Nt = (1 + sr * r) * (1 + ss * s) * st / 8.0;
    xq = xq + V[i] * Nsh;
    dxr = dxr + V[i] * Nr;
    dxs = dxs + V[i] * Ns;
    dxt = dxt + V[i] * Nt;
  }
  cout << "xq = (" << xq.x << "," << xq.y << "," << xq.z << ")\n";
  cout << "dxr= (" << dxr.x << "," << dxr.y << "," << dxr.z << ") |dxr| = " << dxr.norm() << "\n";
  cout << "dxs= (" << dxs.x << "," << dxs.y << "," << dxs.z << ")\n";
  cout << "dxt= (" << dxt.x << "," << dxt.y << "," << dxt.z << ")\n";
  cout << "detJ= " << dxr.dot(dxs.cross(dxt)) << "  (h/2)^3 = " << pow((1.0 / 16), 3) << "\n";
  return 0;
}
