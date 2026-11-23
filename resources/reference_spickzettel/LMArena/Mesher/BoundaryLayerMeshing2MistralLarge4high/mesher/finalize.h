// finalize.h — conformal core stitching, faMesh extraction, verification, reports.
#pragma once
#include "engine.h"
#include "geometry.h"

// ---------- conformal core: twin-plate gap (structured Hexcore, 1:1 on BL front) ----------
inline void coreTwinGap(Engine &E, TwinGap &g) {
  int nx = g.nx, ny = g.ny, nz = g.nz;
  auto G = [&](int i, int j, int k) { return (k * (ny + 1) + j) * (nx + 1) + i; };
  auto gid = [&](int i, int j, int k) { return g.id(i, j, k); };
  std::vector<int> gvid((nx + 1) * (ny + 1) * (nz + 1));
  for (int k = 0; k <= nz; k++)
    for (int j = 0; j <= ny; j++)
      for (int i = 0; i <= nx; i++)
        gvid[G(i, j, k)] = g.onSurface(i, j, k) ? E.col[gid(i, j, k)][E.N] : gid(i, j, k);
  auto top = [&](int i, int j, int k) { return E.mesh.V[E.col[gid(i, j, k)][E.N]]; };
  // trilinear blend of the 8 corner columns
  auto cornerBlend = [&](double xi, double eta, double zeta) {
    Vec3 c000 = top(0, 0, 0), c100 = top(nx, 0, 0), c010 = top(0, ny, 0), c110 = top(nx, ny, 0),
        c001 = top(0, 0, nz), c101 = top(nx, 0, nz), c011 = top(0, ny, nz), c111 = top(nx, ny, nz);
    return (1 - xi) * (1 - eta) * (1 - zeta) * c000 + xi * (1 - eta) * (1 - zeta) * c100 +
           (1 - xi) * eta * (1 - zeta) * c010 + xi * eta * (1 - zeta) * c110 +
           (1 - xi) * (1 - eta) * zeta * c001 + xi * (1 - eta) * zeta * c101 +
           (1 - xi) * eta * zeta * c011 + xi * eta * zeta * c111;
  };
  auto bil = [](const Vec3 &a, const Vec3 &b, const Vec3 &c, const Vec3 &d, double s, double t) {
    return (1 - s) * (1 - t) * a + s * (1 - t) * b + (1 - s) * t * c + s * t * d;
  };
  for (int k = 1; k < nz; k++)
    for (int j = 1; j < ny; j++)
      for (int i = 1; i < nx; i++) {
        double xi = (double)i / nx, eta = (double)j / ny, zeta = (double)k / nz;
        // transfinite (successive-correction) interpolation: exact on all 6 boundary faces
        Vec3 Fx0 = top(0, j, k), Fx1 = top(nx, j, k), Fy0 = top(i, 0, k), Fy1 = top(i, ny, k),
            Fz0 = top(i, j, 0), Fz1 = top(i, j, nz);
        Vec3 P = cornerBlend(xi, eta, zeta) +
                 (1 - xi) * (Fx0 - bil(top(0, 0, 0), top(0, ny, 0), top(0, 0, nz), top(0, ny, nz), eta, zeta)) +
                 xi * (Fx1 - bil(top(nx, 0, 0), top(nx, ny, 0), top(nx, 0, nz), top(nx, ny, nz), eta, zeta)) +
                 (1 - eta) * (Fy0 - bil(top(0, 0, 0), top(nx, 0, 0), top(0, 0, nz), top(nx, 0, nz), xi, zeta)) +
                 eta * (Fy1 - bil(top(0, ny, 0), top(nx, ny, 0), top(0, ny, nz), top(nx, ny, nz), xi, zeta)) +
                 (1 - zeta) * (Fz0 - bil(top(0, 0, 0), top(nx, 0, 0), top(0, ny, 0), top(nx, ny, 0), xi, eta)) +
                 zeta * (Fz1 - bil(top(0, 0, nz), top(nx, 0, nz), top(0, ny, nz), top(nx, ny, nz), xi, eta));
        E.mesh.V[gid(i, j, k)] = P; // interior lattice vertex, used only by the core
      }
  for (int k = 0; k < nz; k++)
    for (int j = 0; j < ny; j++)
      for (int i = 0; i < nx; i++) {
        Mesh::Cell c;
        c.type = 0;
        c.n = {gvid[G(i, j, k)], gvid[G(i + 1, j, k)], gvid[G(i + 1, j + 1, k)], gvid[G(i, j + 1, k)],
               gvid[G(i, j, k + 1)], gvid[G(i + 1, j, k + 1)], gvid[G(i + 1, j + 1, k + 1)],
               gvid[G(i, j + 1, k + 1)]};
        c.patch = -1;
        c.layer = -1;
        E.mesh.cells.push_back(c);
      }
}

// ---------- conformal core: L-channel ----------
// The offset (prism-top) cross-section is split along the diagonal v0->v3 (the reflex
// corner) into two quadrilateral patches meshed with 2D Coons (transfinite) interpolation
// whose boundary curves are exactly the BL front; extruded in z -> all-hex core, 1:1 on
// the BL front, zero hanging nodes. Requires ndiv = {n0,n1,n0,n1,n1,n1}.
inline void coreLChannel(Engine &E, LChannel &g) {
  int NB = g.NB, nz = g.nz;
  const std::vector<int> &nd = g.ndiv;
  std::vector<int> vb(6, 0);
  for (int e = 1; e < 6; e++) vb[e] = vb[e - 1] + nd[e - 1];
  int ndi = nd[1]; // diagonal divisions (= n1 = n3 = n4 = n5)
  std::vector<Vec3> P2(NB);
  for (int b = 0; b < NB; b++) P2[b] = E.mesh.V[E.col[g.vid(b, 0)][E.N]];
  auto diagPt = [&](int i) { return P2[vb[0]] * (1.0 - (double)i / ndi) + P2[vb[3]] * ((double)i / ndi); };
  auto ct = [&](int b, int j) { return E.col[g.vid(b, j)][E.N]; };

  // one Coons patch: grid (ns+1)x(nt+1); boundaryPoint(i,j) returns polygon boundary index
  // or -1 (diagonal/interior). cornerB = {q00,q10,q11,q01} boundary indices or -1.
  auto buildPatch = [&](int ns, int nt, const Vec3 &q0, const Vec3 &q1, const Vec3 &q2, const Vec3 &q3,
                        auto B, auto R, auto T, auto L, auto bnd) {
    // 2D positions (x,y)
    std::vector<std::vector<Vec3>> P(ns + 1, std::vector<Vec3>(nt + 1));
    for (int i = 0; i <= ns; i++)
      for (int j = 0; j <= nt; j++) {
        double s = (double)i / ns, t = (double)j / nt;
        Vec3 Bij = B(i), Rij = R(j), Tij = T(i), Lij = L(j);
        P[i][j] = (1 - t) * Bij + t * Tij + (1 - s) * Lij + s * Rij -
                  ((1 - s) * (1 - t) * q0 + s * (1 - t) * q1 + s * t * q2 + (1 - s) * t * q3);
      }
    // 3D vertex ids per z-level
    std::vector<std::vector<std::vector<int>>> id3(nz + 1,
                                                    std::vector<std::vector<int>>(nt + 1, std::vector<int>(ns + 1, -1)));
    for (int k = 0; k <= nz; k++)
      for (int j = 0; j <= nt; j++)
        for (int i = 0; i <= ns; i++) {
          int b = bnd(i, j);
          if (b >= 0) id3[k][j][i] = ct(b, k); // BL front vertex (conformal, shared)
          else id3[k][j][i] = E.mesh.addVertex({P[i][j].x, P[i][j].y, g.D * k / nz});
        }
    for (int k = 0; k < nz; k++)
      for (int j = 0; j < nt; j++)
        for (int i = 0; i < ns; i++) {
          Mesh::Cell c;
          c.type = 0;
          c.n = {id3[k][j][i], id3[k][j][i + 1], id3[k][j + 1][i + 1], id3[k][j + 1][i],
                 id3[k + 1][j][i], id3[k + 1][j][i + 1], id3[k + 1][j + 1][i + 1], id3[k + 1][j + 1][i]};
          c.patch = -1;
          c.layer = -1;
          E.mesh.cells.push_back(c);
        }
  };

  // region A (bottom): q0=v0 q1=v1 q2=v2 q3=v3 ; grid ns=n0, nt=n1
  buildPatch(
      nd[0], nd[1], P2[vb[0]], P2[vb[1]], P2[vb[2]], P2[vb[3]],
      [&](int i) { return P2[vb[0] + i]; },                          // bottom: edge 0
      [&](int j) { return P2[vb[1] + j]; },                          // right:  edge 1
      [&](int i) { return P2[vb[2] + (nd[2] - i)]; },                // top:    edge 2 (q3->q2)
      [&](int j) { return diagPt(j); },                              // left:   diagonal
      [&](int i, int j) -> int {                                     // boundary point?
        if (j == 0) return vb[0] + i;
        if (i == nd[0]) return vb[1] + j;
        if (j == nd[1]) return vb[2] + (nd[2] - i);
        return -1;
      });
  // region B (top-left): q0=v0 q1=v3 q2=v4 q3=v5 ; grid ns=ndi, nt=n5
  buildPatch(
      ndi, nd[5], P2[vb[0]], P2[vb[3]], P2[vb[4]], P2[vb[5]],
      [&](int i) { return diagPt(i); },                              // bottom: diagonal
      [&](int j) { return P2[vb[3] + j]; },                          // right:  edge 3
      [&](int i) { return P2[vb[4] + (nd[4] - i)]; },                // top:    edge 4 (q3->q2)
      [&](int j) { return P2[vb[5] + (nd[5] - j)]; },                // left:   edge 5 (q0->q3)
      [&](int i, int j) -> int {
        if (i == ndi) return vb[3] + j;
        if (j == nd[5]) return vb[4] + (nd[4] - i);
        if (i == 0) return (vb[5] + (nd[5] - j)) % NB;
        return -1;
      });
}

// ---------- Jacobi eigenvalues of a symmetric 3x3 ----------
inline void jacobi3(const double A[3][3], double eig[3]) {
  double a[3][3];
  for (int i = 0; i < 3; i++)
    for (int j = 0; j < 3; j++) a[i][j] = A[i][j];
  for (int sweep = 0; sweep < 50; sweep++) {
    double off = std::fabs(a[0][1]) + std::fabs(a[0][2]) + std::fabs(a[1][2]);
    if (off < 1e-18) break;
    for (int p = 0; p < 2; p++)
      for (int q = p + 1; q < 3; q++) {
        if (std::fabs(a[p][q]) < 1e-300) continue;
        double theta = 0.5 * std::atan2(2 * a[p][q], a[q][q] - a[p][p]);
        double c = std::cos(theta), s = std::sin(theta);
        for (int k = 0; k < 3; k++) {
          double akp = a[k][p], akq = a[k][q];
          a[k][p] = c * akp - s * akq;
          a[k][q] = s * akp + c * akq;
        }
        for (int k = 0; k < 3; k++) {
          double apk = a[p][k], aqk = a[q][k];
          a[p][k] = c * apk - s * aqk;
          a[q][k] = s * apk + c * aqk;
        }
      }
  }
  eig[0] = a[0][0]; eig[1] = a[1][1]; eig[2] = a[2][2];
}

// ---------- coupled 2D finite-area wall submesh (faMesh) ----------
struct FaMesh {
  struct Face {
    int tri = -1, owner = -1;
    double area = 0;
    Vec3 c, n;
    double W[3][3] = {{0, 0, 0}, {0, 0, 0}, {0, 0, 0}};
    double k1 = 0, k2 = 0, H = 0;
  };
  struct Edge {
    int f1 = -1, f2 = -1;
    Vec3 le;       // edge length vector, in tangent plane, normal to the surface edge
    double len = 0, dgeo = 0;
  };
  std::vector<Face> faces;
  std::vector<Edge> edges;
  std::unordered_map<long long, int> edgeOf;
};

inline FaMesh extractFaMesh(Engine &E, const std::vector<int> &patchIds) {
  FaMesh fa;
  std::vector<char> want(E.mesh.patchNames.size(), 0);
  for (int p : patchIds) want[p] = 1;
  std::unordered_map<int, int> tri2face;
  for (int t = 0; t < (int)E.surf->T.size(); t++) {
    if (!E.surf->isWall[t] || !want[E.surf->patch[t]]) continue;
    FaMesh::Face f;
    f.tri = t;
    auto &tr = E.surf->T[t];
    const Vec3 &a = E.mesh.V[tr[0]], &b = E.mesh.V[tr[1]], &c = E.mesh.V[tr[2]];
    f.n = (b - a).cross(c - a).normalized();
    f.area = 0.5 * (b - a).cross(c - a).norm();
    f.c = (a + b + c) / 3.0;
    f.owner = E.tri2cell[t];
    tri2face[t] = (int)fa.faces.size();
    fa.faces.push_back(f);
  }
  int NF = (int)fa.faces.size();
  auto ekey = [&](int a, int b) { return (long long)std::min(a, b) * NF + std::max(a, b); };
  // register edges
  for (int fi = 0; fi < NF; fi++) {
    auto &tr = E.surf->T[fa.faces[fi].tri];
    for (int k = 0; k < 3; k++) {
      int a = tr[k], b = tr[(k + 1) % 3];
      long long key = ekey(a, b);
      if (!fa.edgeOf.count(key)) {
        FaMesh::Edge e;
        e.f1 = fi;
        fa.edgeOf[key] = (int)fa.edges.size();
        fa.edges.push_back(e);
      }
    }
  }
  // face -> incident edge ids (and neighbor faces)
  std::vector<std::vector<std::pair<int, int>>> fEdges(NF); // (edge id, neighbor face or -1)
  for (int fi = 0; fi < NF; fi++) {
    auto &tr = E.surf->T[fa.faces[fi].tri];
    for (int k = 0; k < 3; k++) {
      int a = tr[k], b = tr[(k + 1) % 3];
      int ei = fa.edgeOf[ekey(a, b)];
      fEdges[fi].push_back({ei, -1});
    }
  }
  // assign f1/f2 and compute Le, lengths, geodesic (chord) distances
  {
    std::unordered_map<long long, std::vector<int>> edgeFaces;
    for (int fi = 0; fi < NF; fi++) {
      auto &tr = E.surf->T[fa.faces[fi].tri];
      for (int k = 0; k < 3; k++) {
        int a = tr[k], b = tr[(k + 1) % 3];
        edgeFaces[ekey(a, b)].push_back(fi);
      }
    }
    for (auto &kv : edgeFaces) {
      int ei = fa.edgeOf[kv.first];
      auto &e = fa.edges[ei];
      e.f1 = kv.second[0];
      e.f2 = kv.second.size() > 1 ? kv.second[1] : -1;
      auto &tr = E.surf->T[fa.faces[e.f1].tri];
      int a = -1, b = -1;
      for (int k = 0; k < 3; k++)
        if (ekey(tr[k], tr[(k + 1) % 3]) == kv.first) { a = tr[k]; b = tr[(k + 1) % 3]; }
      Vec3 ev = E.mesh.V[b] - E.mesh.V[a];
      e.len = ev.norm();
      Vec3 n1 = fa.faces[e.f1].n;
      Vec3 m = (e.f2 >= 0) ? (n1 + fa.faces[e.f2].n) : n1;
      if (m.norm() < 1e-12) m = n1;
      m = m.normalized();
      Vec3 le = m.cross(ev); // |le| = |ev|, in tangent plane, normal to edge
      if (e.f2 >= 0) {
        if (le.dot(fa.faces[e.f2].c - fa.faces[e.f1].c) < 0) le = -le;
        e.dgeo = (fa.faces[e.f2].c - fa.faces[e.f1].c).norm(); // geodesic edge distance
      } else {
        Vec3 mid = (E.mesh.V[a] + E.mesh.V[b]) / 2.0;
        if (le.dot(mid - fa.faces[e.f1].c) < 0) le = -le;
        e.dgeo = 0;
      }
      e.le = le;
      for (int fi : kv.second) {
        for (auto &pe : fEdges[fi])
          if (pe.first == ei) pe.second = (fi == e.f1) ? e.f2 : e.f1;
      }
    }
  }
  // curvature tensor per face: W = (1/(2A)) sum_e |e| (n_nb - n_f) x t_e
  for (int fi = 0; fi < NF; fi++) {
    auto &f = fa.faces[fi];
    auto &tr = E.surf->T[f.tri];
    double W[3][3] = {{0, 0, 0}, {0, 0, 0}, {0, 0, 0}};
    for (int k = 0; k < 3; k++) {
      int a = tr[k], b = tr[(k + 1) % 3];
      int ei = fa.edgeOf[ekey(a, b)];
      Vec3 ev = E.mesh.V[b] - E.mesh.V[a];
      Vec3 th = ev.normalized();
      int nb = -1;
      for (auto &pe : fEdges[fi])
        if (pe.first == ei) nb = pe.second;
      Vec3 dn = (nb >= 0) ? fa.faces[nb].n - f.n : Vec3(0, 0, 0);
      double len = ev.norm();
      for (int i = 0; i < 3; i++)
        for (int j = 0; j < 3; j++) W[i][j] += len * dn[i] * th[j];
    }
    double s = 2.0 * f.area > 0 ? 1.0 / (2.0 * f.area) : 0.0;
    double S[3][3];
    for (int i = 0; i < 3; i++)
      for (int j = 0; j < 3; j++) {
        f.W[i][j] = W[i][j] * s;
        S[i][j] = 0.5 * (f.W[i][j] + f.W[j][i]);
      }
    double eig[3];
    jacobi3(S, eig);
    std::sort(eig, eig + 3, [](double a, double b) { return std::fabs(a) > std::fabs(b); });
    f.k1 = eig[0]; f.k2 = eig[1]; f.H = 0.5 * (S[0][0] + S[1][1] + S[2][2]);
  }
  return fa;
}
