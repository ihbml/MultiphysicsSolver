// Debug 3: quadrature & source sanity
#include <iostream>
#include <iomanip>
#include "../engine/mesh.h"
#include "../engine/assembly.h"
using namespace vv;
using namespace std;

int main() {
  const int N = 8;
  DistortedMesh M = buildMesh(N, 0.0, 12345, 1.0);
  QuadData Q = buildQuadData(M);
  // check volume per cell vs h^3
  double vmin = 1e30, vmax = 0, sum = 0;
  for (size_t i = 0; i < M.cells.size(); ++i) {
    // recompute quad volume: use qLap vs known? just print min/max of a few diagnostics
    (void)i;
  }
  (void)vmin; (void)vmax; (void)sum;
  // print qLap and analytic for a few cells
  for (int idx = 0; idx < (int)M.cells.size(); idx += 73) {
    const Cell& c = M.cells[idx];
    Vec3 ctr = c.centroid;
    double ana = lapPhi(ctr.x, ctr.y, ctr.z) * c.volume;
    cout << "cell " << idx << " ctr=(" << ctr.x << "," << ctr.y << "," << ctr.z << ") V=" << c.volume
         << " qLap=" << Q.cells[idx].qLap << " ana*V=" << ana << " ratio=" << Q.cells[idx].qLap / ana << "\n";
  }
  // total source integral vs analytic: integral of S over domain must match
  double totQ = 0;
  for (size_t i = 0; i < M.cells.size(); ++i) totQ += Q.cells[i].qLap;
  // analytic total: integral of lapPhi over [0,1]^3 = (1/4-2pi^2) * int sin(pi x)cos(pi y)e^{z/2}
  double anaTot = (0.25 - 2 * vv::PI * vv::PI) * (2.0 / vv::PI) * (0.0) * (2 * (exp(0.5) - 1));
  cout << "sum qLap = " << totQ << " analytic total = " << anaTot << "\n";
  // div phi integral total: int (pi/2) sin(2 pi x) cos(pi z) e^{z/2} over domain = 0
  double totD = 0; for (size_t i = 0; i < M.cells.size(); ++i) totD += Q.cells[i].qDiv;
  cout << "sum qDiv = " << totD << "\n";
  return 0;
}
