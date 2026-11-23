// stromberg.h — Stromberg (2023) 23 asymmetric super-element (ASP) patterns.
// An 8-node hexahedron crossing the boundary is described by an 8-bit inside/outside
// mask D in {0..255}. Modulo the 48-element symmetry group of the cube the 256 masks
// fall into exactly 23 equivalence classes (verified at runtime: classCount==23).
// Asymmetric Residual Volume voting:  RV(e,D) = RV_missing^2 + RV_excess,
// with RV_missing = volume of the background hex outside the domain and
// RV_excess  = volume of the domain inside the hex not covered by keeping the hex.
#pragma once
#include "core.h"

struct Stromberg {
  std::vector<std::array<int, 8>> perms; // 48 cube symmetries: new corner j <- old corner perms[g][j]
  std::unordered_map<int, int> classOf;  // mask -> class id (0..22)
  std::vector<int> canonical;            // class id -> canonical mask
  int nClasses = 0;

  static int corner(int bx, int by, int bz) { return bx + 2 * by + 4 * bz; }

  void init() {
    // generate the 48 signed axis permutations of the cube corners
    int axes[6][3] = {{0, 1, 2}, {0, 2, 1}, {1, 0, 2}, {1, 2, 0}, {2, 0, 1}, {2, 1, 0}};
    for (auto &ax : axes)
      for (int f0 = 0; f0 < 2; f0++)
        for (int f1 = 0; f1 < 2; f1++)
          for (int f2 = 0; f2 < 2; f2++) {
            std::array<int, 8> p;
            for (int i = 0; i < 8; i++) {
              int b[3] = {(i >> 0) & 1, (i >> 1) & 1, (i >> 2) & 1};
              int nb[3] = {f0 ? 1 - b[ax[0]] : b[ax[0]], f1 ? 1 - b[ax[1]] : b[ax[1]],
                           f2 ? 1 - b[ax[2]] : b[ax[2]]};
              p[i] = corner(nb[0], nb[1], nb[2]);
            }
            perms.push_back(p);
          }
    // keep only proper rotations (det of the linear part = +1): 24 elements -> 23 classes.
    // The corner map is affine: nb[k] = flip_k(b[ax[k]]), linear part M[k][ax[k]] = +/-1,
    // so det(M) = sign(perm(ax)) * (-1)^(f0+f1+f2).
    std::vector<std::array<int, 8>> rot;
    for (int ax0 = 0; ax0 < 3; ax0++)
      for (int ax1 = 0; ax1 < 3; ax1++)
        for (int ax2 = 0; ax2 < 3; ax2++) {
          if (ax0 == ax1 || ax0 == ax2 || ax1 == ax2) continue;
          int ax[3] = {ax0, ax1, ax2};
          int inv = (ax[0] > ax[1]) + (ax[0] > ax[2]) + (ax[1] > ax[2]);
          int permSign = (inv % 2) ? -1 : 1;
          for (int f0 = 0; f0 < 2; f0++)
            for (int f1 = 0; f1 < 2; f1++)
              for (int f2 = 0; f2 < 2; f2++) {
                int det = permSign * ((f0 + f1 + f2) % 2 ? -1 : 1);
                if (det <= 0) continue;
                std::array<int, 8> p;
                for (int i = 0; i < 8; i++) {
                  int b[3] = {(i >> 0) & 1, (i >> 1) & 1, (i >> 2) & 1};
                  int f[3] = {f0, f1, f2};
                  int nb[3] = {f[0] ? 1 - b[ax[0]] : b[ax[0]], f[1] ? 1 - b[ax[1]] : b[ax[1]],
                               f[2] ? 1 - b[ax[2]] : b[ax[2]]};
                  p[i] = corner(nb[0], nb[1], nb[2]);
                }
                rot.push_back(p);
              }
        }
    perms = rot;
    std::map<int, int> canon2class;
    for (int mask = 0; mask < 256; mask++) {
      int best = 255;
      for (auto &p : perms) {
        int m = 0;
        for (int j = 0; j < 8; j++)
          if ((mask >> p[j]) & 1) m |= 1 << j;
        best = std::min(best, m);
      }
      if (!canon2class.count(best)) {
        int id = (int)canon2class.size();
        canon2class[best] = id;
        canonical.push_back(best);
      }
      classOf[mask] = canon2class[best];
    }
    nClasses = (int)canon2class.size();
  }
  int classify(int mask) const { return classOf.at(mask & 255); }

  // RV voting for a background hex [lo,hi] with per-corner inside flags.
  // Volumes estimated by sub-sampling the hex trilinearly (ns^3 samples).
  template <typename InsideFn>
  void residualVote(const Vec3 &lo, const Vec3 &hi, const bool ins8[8], InsideFn &&inside,
                    double &rvMissing, double &rvExcess, double &rv, int &cls, int ns = 4) const {
    double vol = (hi.x - lo.x) * (hi.y - lo.y) * (hi.z - lo.z);
    int mask = 0;
    for (int i = 0; i < 8; i++)
      if (ins8[i]) mask |= 1 << i;
    cls = classify(mask);
    int nOut = 0, nIn = 0, tot = 0;
    for (int i = 0; i < ns; i++)
      for (int j = 0; j < ns; j++)
        for (int k = 0; k < ns; k++) {
          double u = (i + 0.5) / ns, v = (j + 0.5) / ns, w = (k + 0.5) / ns;
          Vec3 p(lo.x + u * (hi.x - lo.x), lo.y + v * (hi.y - lo.y), lo.z + w * (hi.z - lo.z));
          tot++;
          if (inside(p)) nIn++; else nOut++;
        }
    rvMissing = vol * nOut / (double)tot;             // part of hex outside domain
    rvExcess = vol * nIn / (double)tot * (mask != 255 ? 1.0 : 0.0); // excess vs pure hex
    rv = rvMissing * rvMissing + rvExcess;
  }
};
