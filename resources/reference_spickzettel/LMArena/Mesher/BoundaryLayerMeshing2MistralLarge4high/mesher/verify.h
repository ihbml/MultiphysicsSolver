// verify.h — full mesh verification: positive volumes, J_ENS, Gauss closure,
// hanging-node / conformity checks, and the Stromberg background hexcore run.
#pragma once
#include "engine.h"
#include "finalize.h"

struct Report {
  std::string caseName;
  int nVerts = 0, nCells = 0, nHex = 0, nPrism = 0, nTet = 0, nPyr = 0;
  double minVol = 1e300, minJens = 1e300, maxGauss = 0;
  int minJensCell = -1;
  int boundaryFaces = 0, interiorFaces = 0, badFaces = 0;
  int hangingNodes = 0;
  bool conformal = false;
  int N = 0;
  double dy1 = 0, r = 0;
  double dMedMin = 1e300, dMedMax = 0, Hmin = 1e300, Hmax = 0;
  int nRetracted = 0, nDetRetract = 0;
  // stromberg
  int strombergClasses = 0, bgHexes = 0, bgMixed = 0, bgInside = 0;
  double maxRV = 0;
  // faMesh
  int faFaces = 0, faEdges = 0, faBoundaryEdges = 0;
  double faArea = 0;
  bool faOwnerBijective = false;
};

inline Report verify(Engine &E, const FaMesh *fa = nullptr) {
  Report R;
  R.caseName = E.caseName;
  for (auto &c : E.mesh.cells) evaluateCell(E.mesh, c);
  R.nVerts = (int)E.mesh.V.size();
  R.nCells = (int)E.mesh.cells.size();
  for (auto &c : E.mesh.cells) {
    if (c.type == 0) R.nHex++;
    if (c.type == 1) R.nPrism++;
    if (c.type == 2) R.nTet++;
    if (c.type == 3) R.nPyr++;
    R.minVol = std::min(R.minVol, c.vol);
    if (c.jens < R.minJens) { R.minJens = c.jens; R.minJensCell = (int)(&c - &E.mesh.cells[0]); }
    R.maxGauss = std::max(R.maxGauss, c.gauss);
  }
  // conformity: every triangulated cell face shared by exactly 1 (boundary) or 2 (interior)
  std::map<std::array<int, 3>, int> faceCount;
  for (auto &c : E.mesh.cells) {
    Vec3 cen = cellCentroid(E.mesh.V, c.n);
    for (auto &t : cellTris(E.mesh.V, c.n, c.type, cen)) {
      std::array<int, 3> key = t;
      std::sort(key.begin(), key.end());
      int &cnt = faceCount[key];
      cnt++;
      if (cnt == 1) R.boundaryFaces++;
      else if (cnt == 2) R.interiorFaces++;
      else R.badFaces++;
    }
  }
  R.conformal = (R.badFaces == 0);
  // hanging-node check: no vertex lies strictly inside any mesh edge
  {
    std::vector<std::pair<int, int>> edges;
    {
      std::map<std::pair<int, int>, char> eseen;
      for (auto &c : E.mesh.cells)
        for (auto &f : cellFaces(c.type))
          for (size_t i = 0; i < f.size(); i++) {
            int a = c.n[f[i]], b = c.n[f[(i + 1) % f.size()]];
            auto key = std::minmax(a, b);
            if (eseen[key]++) continue;
            edges.push_back(key);
          }
    }
    double cell = E.scale / 64.0;
    std::unordered_map<long long, std::vector<int>> grid;
    auto gkey = [&](const Vec3 &p) {
      long long ix = (long long)std::floor(p.x / cell), iy = (long long)std::floor(p.y / cell),
                iz = (long long)std::floor(p.z / cell);
      return (ix * 73856093) ^ (iy * 19349663) ^ (iz * 83492791);
    };
    for (int v = 0; v < (int)E.mesh.V.size(); v++) grid[gkey(E.mesh.V[v])].push_back(v);
    double tol = 1e-9 * E.scale;
    for (auto &e : edges) {
      const Vec3 &a = E.mesh.V[e.first], &b = E.mesh.V[e.second];
      Vec3 ab = b - a;
      double len2 = ab.norm2();
      if (len2 < tol * tol) continue;
      Vec3 lo(std::min(a.x, b.x), std::min(a.y, b.y), std::min(a.z, b.z));
      Vec3 hi(std::max(a.x, b.x), std::max(a.y, b.y), std::max(a.z, b.z));
      for (long long dx = (long long)std::floor(lo.x / cell) - 1; dx <= (long long)std::floor(hi.x / cell) + 1; dx++)
        for (long long dy = (long long)std::floor(lo.y / cell) - 1; dy <= (long long)std::floor(hi.y / cell) + 1; dy++)
          for (long long dz = (long long)std::floor(lo.z / cell) - 1; dz <= (long long)std::floor(hi.z / cell) + 1; dz++) {
            long long key = (dx * 73856093) ^ (dy * 19349663) ^ (dz * 83492791);
            auto it = grid.find(key);
            if (it == grid.end()) continue;
            for (int v : it->second) {
              if (v == e.first || v == e.second) continue;
              Vec3 ap = E.mesh.V[v] - a;
              double t = ap.dot(ab) / len2;
              if (t < 1e-9 || t > 1 - 1e-9) continue;
              double d2 = (ap - ab * t).norm2();
              if (d2 < tol * tol) R.hangingNodes++;
            }
          }
    }
  }
  // boundary-layer stats
  R.N = E.N;
  R.dy1 = E.par.dy1;
  R.r = E.par.r;
  R.nRetracted = E.nRetracted;
  R.nDetRetract = E.nDetRetract;
  int nv = (int)E.mesh.V.size() > 0 ? (int)E.dMed.size() : 0;
  for (int v = 0; v < nv; v++) {
    if (!E.blActive[v]) continue;
    R.dMedMin = std::min(R.dMedMin, E.dMed[v]);
    R.dMedMax = std::max(R.dMedMax, E.dMed[v]);
    R.Hmin = std::min(R.Hmin, E.Htot[v]);
    R.Hmax = std::max(R.Hmax, E.Htot[v]);
  }
  if (fa) {
    R.faFaces = (int)fa->faces.size();
    R.faEdges = (int)fa->edges.size();
    for (auto &e : fa->edges) {
      if (e.f2 < 0) R.faBoundaryEdges++;
      R.faArea += 0; // area accumulated below
    }
    for (auto &f : fa->faces) R.faArea += f.area;
    std::vector<char> seen(R.nCells + 1, 0);
    bool bij = true;
    for (auto &f : fa->faces) {
      if (f.owner < 0 || f.owner >= R.nCells) bij = false;
      else if (seen[f.owner]) bij = false;
      else seen[f.owner] = 1;
    }
    R.faOwnerBijective = bij;
  }
  return R;
}

// ---------- Stromberg background hexcore run ----------
template <typename InsideFn>
inline void runStromberg(Engine &E, Stromberg &S, InsideFn inside, double hbg, Report &R) {
  Vec3 lo = E.mesh.V[0], hi = E.mesh.V[0];
  for (auto &p : E.mesh.V) {
    lo.x = std::min(lo.x, p.x); lo.y = std::min(lo.y, p.y); lo.z = std::min(lo.z, p.z);
    hi.x = std::max(hi.x, p.x); hi.y = std::max(hi.y, p.y); hi.z = std::max(hi.z, p.z);
  }
  R.strombergClasses = S.nClasses;
  int nx = std::max(1, (int)std::ceil((hi.x - lo.x) / hbg)), ny = std::max(1, (int)std::ceil((hi.y - lo.y) / hbg)),
      nz = std::max(1, (int)std::ceil((hi.z - lo.z) / hbg));
  std::vector<int> clsCount(23, 0);
  for (int k = 0; k < nz; k++)
    for (int j = 0; j < ny; j++)
      for (int i = 0; i < nx; i++) {
        Vec3 blo(lo.x + i * hbg, lo.y + j * hbg, lo.z + k * hbg);
        Vec3 bhi(lo.x + (i + 1) * hbg, lo.y + (j + 1) * hbg, lo.z + (k + 1) * hbg);
        bool ins[8];
        int mask = 0;
        for (int c = 0; c < 8; c++) {
          Vec3 p(bhi.x - (c & 1) * hbg, bhi.y - ((c >> 1) & 1) * hbg, bhi.z - ((c >> 2) & 1) * hbg);
          ins[c] = inside(p);
          if (ins[c]) mask |= 1 << c;
        }
        R.bgHexes++;
        int cls = S.classify(mask);
        clsCount[cls]++;
        if (mask == 0 || mask == 255) { if (mask == 255) R.bgInside++; continue; }
        R.bgMixed++;
        double rvm, rve, rv;
        int c2;
        S.residualVote(blo, bhi, ins, inside, rvm, rve, rv, c2);
        R.maxRV = std::max(R.maxRV, rv);
      }
  (void)clsCount;
}
