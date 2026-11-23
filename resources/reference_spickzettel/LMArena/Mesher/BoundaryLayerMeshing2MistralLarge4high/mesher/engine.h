// engine.h — collision-free medial-axis boundary-layer prism extrusion + conformal
// hexcore stitching + coupled 2D finite-area wall submesh (faMesh) + verification.
//
// Pipeline (Marcon et al. 2019 / Loseille & Loehner 2013 style):
//   1. angle-weighted pseudonormals at boundary vertices
//   2. constrained Laplacian smoothing of the extrusion field n^v, preserving
//      n^v . n_t >= cos(65 deg) for every incident triangle (positive visibility,
//      no fan self-intersection at sharp concave corners)
//   3. medial-axis / opposing-wall clearance d_medial via narrow-band ray/cone
//      casting against the surface BVH
//   4. layered prism extrusion: H_total(v) <= 0.40 d_medial(v) (layer squeezing),
//      prism det(J) >= 0.15 (retraction / front termination), zero inverted prisms
//   5. conformal core fill (structured hexcore matched 1:1 on the outer BL surface)
//   6. faMesh extraction + full verification (volumes, J_ENS, Gauss closure,
//      hanging nodes, conformity)
#pragma once
#include "core.h"
#include "bvh.h"
#include "quality.h"
#include "stromberg.h"
#include <cstdlib>

constexpr double PI = 3.14159265358979323846;

struct BLParams {
  double dy1 = 0.0008;                    // first-layer height dY1
  double r = 1.35;                       // growth ratio
  int Nmax = 6;                          // max layers
  double medialFrac = 0.40;              // H_total <= 0.40 * d_medial
  double minDetJ = 0.22;                 // prism corner det(J) floor (scaled); s>=0.22 => J_ENS>=0.25
  double visDeg = 65.0;                  // visibility cone half-angle complement
  int smoothIters = 12;
};

struct Engine {
  Mesh mesh;
  const Surface *surf = nullptr;
  std::string caseName;
  BLParams par;
  BVH bvh;
  double scale = 1.0;
  int nSurf = 0;                            // number of input surface vertices

  std::vector<char> blActive;               // per surface vertex: belongs to a wall
  std::vector<std::vector<int>> vTris;      // per surface vertex: incident WALL tris
  std::vector<std::vector<int>> vAllTris;   // per surface vertex: all incident tris
  std::vector<std::vector<int>> adj;        // surface-graph adjacency (wall edges)
  std::vector<Vec3> nHat;                   // smoothed extrusion direction per surf vertex
  std::vector<double> dMed;                 // medial clearance per surf vertex
  std::vector<std::vector<int>> col;        // per BL vertex: column of mesh vertex ids
  std::vector<double> hLayer, Htot;         // per BL vertex: last layer height, total height
  int N = 0;                                // global (conformal) layer count
  std::vector<int> tri2cell;                // wall tri -> owner volume cell (layer 1)
  int nRetracted = 0, nClearanceStop = 0, nDetRetract = 0;

  // ---------- setup ----------
  void setup(const Surface *s, const std::string &name, BLParams p) {
    surf = s; caseName = name; par = p;
    mesh.V = s->V;
    mesh.surfT = s->T;
    mesh.surfPatch = s->patch;
    mesh.surfIsWall = s->isWall;
    mesh.patchNames = s->patchNames;
    int nv = (int)mesh.V.size();
    nSurf = nv;
    blActive.assign(nv, 0);
    vTris.assign(nv, {});
    vAllTris.assign(nv, {});
    for (int t = 0; t < (int)s->T.size(); t++)
      for (int k = 0; k < 3; k++) vAllTris[s->T[t][k]].push_back(t);
    for (int t = 0; t < (int)s->T.size(); t++)
      if (s->isWall[t])
        for (int k = 0; k < 3; k++) {
          int v = s->T[t][k];
          blActive[v] = 1;
          vTris[v].push_back(t);
        }
    adj.assign(nv, {});
    {
      std::unordered_map<long long, char> seen;
      for (int t = 0; t < (int)s->T.size(); t++) {
        if (!s->isWall[t]) continue;
        int a[3] = {s->T[t][0], s->T[t][1], s->T[t][2]};
        for (int i = 0; i < 3; i++) {
          long long key = (long long)std::min(a[i], a[(i + 1) % 3]) * nv + std::max(a[i], a[(i + 1) % 3]);
          if (seen[key]++) continue;
          adj[a[i]].push_back(a[(i + 1) % 3]);
          adj[a[(i + 1) % 3]].push_back(a[i]);
        }
      }
    }
    Vec3 lo = mesh.V[0], hi = mesh.V[0];
    for (auto &p : mesh.V) {
      lo.x = std::min(lo.x, p.x); lo.y = std::min(lo.y, p.y); lo.z = std::min(lo.z, p.z);
      hi.x = std::max(hi.x, p.x); hi.y = std::max(hi.y, p.y); hi.z = std::max(hi.z, p.z);
    }
    scale = (hi - lo).norm();
    bvh.build(mesh.V, mesh.surfT);
    nHat.assign(nv, {0, 0, 0});
    dMed.assign(nv, 0);
    col.assign(nv, {});
    hLayer.assign(nv, 0);
    Htot.assign(nv, 0);
    hks.assign(nv, {});
    tri2cell.assign((int)s->T.size(), -1);
  }

  // ---------- 1. angle-weighted pseudonormals ----------
  void pseudonormals() {
    int nv = nSurf;
    for (int v = 0; v < nv; v++) {
      if (!blActive[v]) continue;
      Vec3 n;
      for (int t : vTris[v]) {
        const auto &tri = surf->T[t];
        int i0 = (tri[0] == v) ? 0 : (tri[1] == v) ? 1 : 2;
        Vec3 e1 = mesh.V[tri[(i0 + 1) % 3]] - mesh.V[v];
        Vec3 e2 = mesh.V[tri[(i0 + 2) % 3]] - mesh.V[v];
        double ang = std::atan2(e1.cross(e2).norm(), e1.dot(e2)); // angle at v
        n += surf->triNormal(t) * ang;
      }
      nHat[v] = n.norm() > 0 ? n.normalized() : Vec3(0, 0, 1);
    }
  }

  static Vec3 anyPerp(const Vec3 &n) {
    Vec3 a = std::fabs(n.x) < 0.9 ? Vec3(1, 0, 0) : Vec3(0, 1, 0);
    return n.cross(a).normalized();
  }

  // ---------- 2. constrained Laplacian smoothing (positive visibility) ----------
  void smooth() {
    int nv = nSurf;
    double visCos = std::cos(par.visDeg * PI / 180.0);
    std::vector<Vec3> nn = nHat;
    for (int it = 0; it < par.smoothIters; it++) {
      std::vector<Vec3> prop = nHat;
      for (int v = 0; v < nv; v++) {
        if (!blActive[v]) continue;
        Vec3 s;
        for (int u : adj[v]) s += nHat[u];
        prop[v] = s.norm() > 1e-14 ? s.normalized() : nHat[v];
      }
      // constraint projection: n^v . n_t >= cos(65deg) for all incident wall tris
      for (int v = 0; v < nv; v++) {
        if (!blActive[v]) continue;
        for (int rep = 0; rep < 6; rep++) {
          int worst = -1;
          double worstC = 1.0;
          for (int t : vTris[v]) {
            double c = prop[v].dot(surf->triNormal(t));
            if (c < worstC) { worstC = c; worst = t; }
          }
          if (worst < 0 || worstC >= visCos) break;
          Vec3 nt = surf->triNormal(worst);
          Vec3 t = prop[v] - nt * worstC;
          double tl = t.norm();
          if (tl < 1e-12) { t = anyPerp(nt); tl = 1.0; }
          t = t / tl;
          prop[v] = (nt * visCos + t * std::sqrt(std::max(0.0, 1.0 - visCos * visCos))).normalized();
        }
      }
      nHat = prop;
    }
  }

  // ---------- 3. medial-axis clearance via ray/cone casting ----------
  void medial() {
    int nv = nSurf;
    double eps = 1e-7 * scale, tmin = 1e-6 * scale, ignoreBelow = 1e-3 * scale;
    for (int v = 0; v < nv; v++) {
      if (!blActive[v]) continue;
      Vec3 n = nHat[v];
      Vec3 t1 = anyPerp(n), t2 = n.cross(t1);
      std::unordered_set<int> ignore(vAllTris[v].begin(), vAllTris[v].end());
      double best = 5.0 * scale;
      double th = 32.0 * PI / 180.0; // cone half-angle
      for (int k = 0; k <= 8; k++) {
        Vec3 d;
        if (k == 0) d = n;
        else {
          double phi = 2.0 * PI * (k - 1) / 8.0;
          d = (n * std::cos(th) + (t1 * std::cos(phi) + t2 * std::sin(phi)) * std::sin(th)).normalized();
        }
        double h = bvh.ray(mesh.V[v] + n * eps, d, tmin, nullptr, &ignore, ignoreBelow);
        if (h > 0) best = std::min(best, h);
      }
      dMed[v] = best;
    }
  }

  // raw cumulative height after k layers
  double Hraw(int v, int k) const {
    double h = par.dy1, H = 0;
    for (int j = 1; j <= k; j++) { H += h; h *= par.r; }
    (void)v;
    return H;
  }

  // per-BL-vertex per-layer heights (1..N)
  std::vector<std::vector<double>> hks;

  // ---------- 4. layered prism extrusion with squeezing / termination ----------
  void rebuildColumns(int N_) {
    int nv = nSurf;
    for (int v = 0; v < nv; v++) col[v].clear();
    for (int v = 0; v < nv; v++)
      if (blActive[v]) col[v].push_back(v);
    hks.assign(nv, {});
    Htot.assign(nv, 0);
    nRetracted = 0;
    for (int k = 1; k <= N_; k++) {
      for (int v = 0; v < nv; v++) {
        if (!blActive[v]) continue;
        if ((int)hks[v].size() < k) hks[v].resize(k, 0.0);
        double hraw = par.dy1 * std::pow(par.r, k - 1); // raw geometric growth
        double cap = par.medialFrac * dMed[v] - Htot[v];
        double h = std::min(hraw, std::max(0.0, cap));
        if (h < hraw - 1e-15 * scale) nRetracted++;
        hks[v][k - 1] = h;
        Htot[v] += h;
        Vec3 p = mesh.V[col[v].back()] + nHat[v] * h;
        col[v].push_back(mesh.addVertex(p));
      }
    }
  }

  // min corner scaled det of the prism over tri t at layer k
  double prismDet(const std::array<int, 3> &t, int k) {
    std::vector<int> nd = {col[t[0]][k - 1], col[t[1]][k - 1], col[t[2]][k - 1],
                           col[t[0]][k], col[t[1]][k], col[t[2]][k]};
    std::vector<Vec3> P;
    for (int id : nd) P.push_back(mesh.V[id]);
    std::vector<int> loc = {0, 1, 2, 3, 4, 5}; // local indices into P
    if (signedVolume(P, loc, 1) < 0) { std::swap(loc[1], loc[2]); std::swap(loc[4], loc[5]); }
    double smin = 1e300;
    for (int c = 0; c < 6; c++) {
      Vec3 e0 = P[loc[PRISM_CORNER[c][0]]] - P[loc[c]];
      Vec3 e1 = P[loc[PRISM_CORNER[c][1]]] - P[loc[c]];
      Vec3 e2 = P[loc[PRISM_CORNER[c][2]]] - P[loc[c]];
      double s = e0.norm() * e1.norm() * e2.norm();
      smin = std::min(smin, s > 0 ? det3(e0, e1, e2) / s : -1.0);
    }
    return smin;
  }

  void extrude() {
    int nv = nSurf;
    // per-vertex layer count from medial clearance
    int Nmin = par.Nmax;
    for (int v = 0; v < nv; v++) {
      if (!blActive[v]) continue;
      double cap = par.medialFrac * dMed[v];
      int nv_ = 0;
      for (int k = 1; k <= par.Nmax; k++)
        if (Hraw(v, k) <= cap + 1e-15) nv_ = k; else break;
      if (nv_ == 0) nv_ = 1;
      Nmin = std::min(Nmin, nv_);
    }
    N = Nmin;
    // iterate: retract layer heights until every prism det(J) >= minDetJ, else terminate
    while (true) {
      // fresh raw columns for the current N (discard previous retraction)
      for (int v = 0; v < nv; v++) hks[v].clear();
      rebuildColumns(N);
      bool fail = false;
      for (int k = 1; k <= N && !fail; k++) {
        for (int iter = 0; iter < 12 && !fail; iter++) {
          bool bad = false;
          for (int t = 0; t < (int)surf->T.size(); t++) {
            if (!surf->isWall[t]) continue;
            if ((int)col[surf->T[t][0]].size() <= k) continue;
            double s = prismDet(surf->T[t], k);
            if (s < par.minDetJ) {
              bad = true;
              for (int j = 0; j < 3; j++) {
                int v = surf->T[t][j];
                hks[v][k - 1] *= 0.7;
                nDetRetract++;
                mesh.V[col[v][k]] = mesh.V[col[v][k - 1]] + nHat[v] * hks[v][k - 1];
              }
            }
          }
          if (!bad) break;
          if (iter == 11) {
            fail = true;
            if (getenv("MESHER_DEBUG")) {
              double worst = 1e300; std::array<int,3> wt{};
              for (int t = 0; t < (int)surf->T.size(); t++) {
                if (!surf->isWall[t]) continue;
                if ((int)col[surf->T[t][0]].size() <= k) continue;
                double s = prismDet(surf->T[t], k);
                if (s < worst) { worst = s; wt = surf->T[t]; }
              }
              std::fprintf(stderr, "[dbg] layer %d fail: worst prism s=%.4f tri=(%d,%d,%d) hks=%.3e %.3e %.3e n=(%.3f,%.3f,%.3f)\n",
                           k, worst, wt[0], wt[1], wt[2], hks[wt[0]][k-1], hks[wt[1]][k-1], hks[wt[2]][k-1],
                           nHat[wt[0]].x, nHat[wt[0]].y, nHat[wt[0]].z);
              {
                std::vector<int> nd = {col[wt[0]][k-1], col[wt[1]][k-1], col[wt[2]][k-1],
                                       col[wt[0]][k], col[wt[1]][k], col[wt[2]][k]};
                std::vector<Vec3> P;
                for (int id : nd) P.push_back(mesh.V[id]);
                std::vector<int> loc = {0,1,2,3,4,5};
                if (signedVolume(P, loc, 1) < 0) { std::swap(loc[1], loc[2]); std::swap(loc[4], loc[5]); }
                for (int c = 0; c < 6; c++) {
                  Vec3 e0 = P[loc[PRISM_CORNER[c][0]]] - P[loc[c]];
                  Vec3 e1 = P[loc[PRISM_CORNER[c][1]]] - P[loc[c]];
                  Vec3 e2 = P[loc[PRISM_CORNER[c][2]]] - P[loc[c]];
                  double sc = e0.norm()*e1.norm()*e2.norm();
                  std::fprintf(stderr, "   corner %d s=%.4f  |e|=(%.2e,%.2e,%.2e)\n", c, sc>0?det3(e0,e1,e2)/sc:-1, e0.norm(), e1.norm(), e2.norm());
                }
                for (int j = 0; j < 3; j++) { auto &p = P[j]; auto &q = P[j+3]; std::fprintf(stderr, "   base%d (%.5f,%.5f,%.5f) top%d (%.5f,%.5f,%.5f) n=(%.3f,%.3f,%.3f)\n", j,p.x,p.y,p.z,j,q.x,q.y,q.z, nHat[wt[j]].x, nHat[wt[j]].y, nHat[wt[j]].z); }
              }
            }
          }
        }
      }
      if (!fail) break;
      if (N <= 1) break;
      N--; // terminate the whole front one layer earlier (keeps the front conformal)
      nClearanceStop++;
    }
    // final consistent rebuild with the retracted heights preserved
    for (int v = 0; v < nv; v++) {
      if (!blActive[v]) continue;
      for (int k = 1; k <= N; k++) {
        if ((int)hks[v].size() < k) hks[v].resize(k, 0.0);
        Vec3 p = mesh.V[col[v][k - 1]] + nHat[v] * hks[v][k - 1];
        mesh.V[col[v][k]] = p;
      }
    }
    // final safety: recompute Htot consistently
    for (int v = 0; v < nv; v++) {
      if (!blActive[v]) continue;
      Htot[v] = 0;
      for (int k = 1; k <= N; k++) Htot[v] += (mesh.V[col[v][k]] - mesh.V[col[v][k - 1]]).norm();
    }
    // build prism cells
    for (int t = 0; t < (int)surf->T.size(); t++) {
      if (!surf->isWall[t]) continue;
      for (int k = 1; k <= N; k++) {
        Mesh::Cell c;
        c.type = 1;
        c.n = {col[surf->T[t][0]][k - 1], col[surf->T[t][1]][k - 1], col[surf->T[t][2]][k - 1],
               col[surf->T[t][0]][k], col[surf->T[t][1]][k], col[surf->T[t][2]][k]};
        c.patch = surf->patch[t];
        c.layer = k;
        if (k == 1) tri2cell[t] = (int)mesh.cells.size();
        mesh.cells.push_back(c);
      }
    }
  }
};
