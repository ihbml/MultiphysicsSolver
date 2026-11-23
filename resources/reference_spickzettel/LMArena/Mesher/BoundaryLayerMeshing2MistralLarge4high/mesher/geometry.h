// geometry.h — watertight analytic wall surfaces for the two verification cases:
//   1) "l_channel"  : stepped L-channel (extruded L cross-section, reflex 270 deg corner)
//   2) "twin_gap"   : narrow twin-plate cooling gap (thin box channel)
// Surface normals are oriented INTO the fluid domain. Meshes are structured lattices so
// the conformal core can reuse vertex ids (zero hanging nodes by construction).
#pragma once
#include "core.h"

struct Surface {
  std::vector<Vec3> V;
  std::vector<std::array<int, 3>> T;
  std::vector<int> patch;    // per tri
  std::vector<char> isWall;  // per tri
  std::vector<std::string> patchNames;
  Vec3 triNormal(int t) const {
    return (V[T[t][1]] - V[T[t][0]]).cross(V[T[t][2]] - V[T[t][0]]).normalized();
  }
  int addPatch(const std::string &s) {
    patchNames.push_back(s);
    return (int)patchNames.size() - 1;
  }
};

// ---------------- twin-plate cooling gap: box [0,Lx]x[0,G]x[0,Lz] ----------------
// lattice id(i,j,k), i in [0..nx], j in [0..ny], k in [0..nz]
struct TwinGap : Surface {
  int nx, ny, nz;
  double Lx, G, Lz;
  int id(int i, int j, int k) const { return (k * (ny + 1) + j) * (nx + 1) + i; }
  TwinGap(double Lx_, double G_, double Lz_, int nx_, int ny_, int nz_)
      : nx(nx_), ny(ny_), nz(nz_), Lx(Lx_), G(G_), Lz(Lz_) {
    V.resize((nx + 1) * (ny + 1) * (nz + 1));
    for (int k = 0; k <= nz; k++)
      for (int j = 0; j <= ny; j++)
        for (int i = 0; i <= nx; i++)
          V[id(i, j, k)] = {Lx * i / nx, G * j / ny, Lz * k / nz};
    int pBot = addPatch("plate_bottom"), pTop = addPatch("plate_top");
    int pX0 = addPatch("wall_x0"), pX1 = addPatch("wall_x1");
    int pZ0 = addPatch("wall_z0"), pZ1 = addPatch("wall_z1");
    auto quad = [&](int a, int b, int c, int d, int ptag, const Vec3 &wantN) {
      // two triangles, fixed diagonal (a,c)
      std::array<int, 3> ts[2] = {{a, b, c}, {a, c, d}};
      for (auto &t : ts) {
        Vec3 n = (V[t[1]] - V[t[0]]).cross(V[t[2]] - V[t[0]]).normalized();
        if (n.dot(wantN) < 0) std::swap(t[1], t[2]);
        T.push_back(t); patch.push_back(ptag); isWall.push_back(1);
      }
    };
    for (int k = 0; k < nz; k++)
      for (int j = 0; j < ny; j++) {
        quad(id(0, j, k), id(0, j + 1, k), id(0, j + 1, k + 1), id(0, j, k + 1), pX0, {1, 0, 0});
        quad(id(nx, j, k), id(nx, j, k + 1), id(nx, j + 1, k + 1), id(nx, j + 1, k), pX1, {-1, 0, 0});
      }
    for (int k = 0; k < nz; k++)
      for (int i = 0; i < nx; i++) {
        quad(id(i, 0, k), id(i + 1, 0, k), id(i + 1, 0, k + 1), id(i, 0, k + 1), pBot, {0, 1, 0});
        quad(id(i, ny, k), id(i, ny, k + 1), id(i + 1, ny, k + 1), id(i + 1, ny, k), pTop, {0, -1, 0});
      }
    for (int j = 0; j < ny; j++)
      for (int i = 0; i < nx; i++) {
        quad(id(i, j, 0), id(i, j + 1, 0), id(i + 1, j + 1, 0), id(i + 1, j, 0), pZ0, {0, 0, 1});
        quad(id(i, j, nz), id(i + 1, j, nz), id(i + 1, j + 1, nz), id(i, j + 1, nz), pZ1, {0, 0, -1});
      }
  }
  // is this lattice vertex on the surface?
  bool onSurface(int i, int j, int k) const {
    return i == 0 || i == nx || j == 0 || j == ny || k == 0 || k == nz;
  }
};

// ---------------- stepped L-channel: L polygon extruded along z ----------------
// polygon (CCW): (0,0),(Lx,0),(Lx,h1),(w,h1),(w,H),(0,H); reflex corner at (w,h1).
struct LChannel : Surface {
  double Lx, H, w, h1, D;
  int nz;
  std::vector<std::array<double, 2>> P; // polygon vertices
  std::vector<int> ndiv;                // subdivisions per polygon edge
  int NB = 0;                           // total boundary points
  std::vector<std::array<double, 2>> B; // boundary points (CCW)
  std::vector<int> bEdge;               // boundary point -> edge id
  int vid(int b, int j) const { return j * NB + b; } // b: boundary point, j: z level
  LChannel(double Lx_, double H_, double w_, double h1_, double D_, int nz_,
           std::vector<int> ndiv_)
      : Lx(Lx_), H(H_), w(w_), h1(h1_), D(D_), nz(nz_), ndiv(std::move(ndiv_)) {
    P = {{0, 0}, {Lx, 0}, {Lx, h1}, {w, h1}, {w, H}, {0, H}};
    int M = (int)P.size();
    for (int e = 0; e < M; e++) {
      auto a = P[e], b = P[(e + 1) % M];
      for (int s = 0; s < ndiv[e]; s++) {
        double t = (double)s / ndiv[e];
        B.push_back({a[0] + t * (b[0] - a[0]), a[1] + t * (b[1] - a[1])});
        bEdge.push_back(e);
      }
    }
    NB = (int)B.size();
    V.resize(NB * (nz + 1));
    for (int j = 0; j <= nz; j++)
      for (int b = 0; b < NB; b++)
        V[vid(b, j)] = {B[b][0], B[b][1], D * j / nz};
    int pLat = addPatch("wall_lateral"), pIn = addPatch("inlet"), pOut = addPatch("outlet");
    // lateral faces (wall) — grid (edge divisions) x (nz), fixed diagonal
    for (int e = 0; e < M; e++) {
      double dx = P[(e + 1) % M][0] - P[e][0], dy = P[(e + 1) % M][1] - P[e][1];
      double len = std::hypot(dx, dy);
      Vec3 wantN(-dy / len, dx / len, 0); // left normal of CCW edge = into fluid
      int b0 = 0;
      for (int ee = 0; ee < e; ee++) b0 += ndiv[ee];
      for (int s = 0; s < ndiv[e]; s++)
        for (int j = 0; j < nz; j++) {
          int b1 = (b0 + s + 1) % NB;
          int a = vid(b0 + s, j), b = vid(b1, j), c = vid(b1, j + 1), d = vid(b0 + s, j + 1);
          std::array<int, 3> ts[2] = {{a, b, c}, {a, c, d}};
          for (auto &t : ts) {
            Vec3 n = (V[t[1]] - V[t[0]]).cross(V[t[2]] - V[t[0]]).normalized();
            if (n.dot(wantN) < 0) std::swap(t[1], t[2]);
            T.push_back(t); patch.push_back(pLat); isWall.push_back(1);
          }
        }
    }
    // end caps (inlet/outlet, not walls): fan from boundary point 0
    for (int m = 1; m + 1 < NB; m++) {
      std::array<int, 3> tIn = {vid(0, 0), vid(m, 0), vid(m + 1, 0)};   // +z into fluid
      T.push_back(tIn); patch.push_back(pIn); isWall.push_back(0);
      std::array<int, 3> tOut = {vid(0, nz), vid(m + 1, nz), vid(m, nz)}; // -z into fluid
      T.push_back(tOut); patch.push_back(pOut); isWall.push_back(0);
    }
  }
};
