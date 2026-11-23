// core.h — Vec3, mesh containers, JSON helpers for the Poly-Hexcore / Cut-Cell engine
#pragma once
#include <cmath>
#include <cstdint>
#include <vector>
#include <array>
#include <string>
#include <map>
#include <unordered_map>
#include <algorithm>
#include <fstream>
#include <iostream>
#include <sstream>
#include <cstdio>
#include <cassert>

struct Vec3 {
  double x = 0, y = 0, z = 0;
  Vec3() {}
  Vec3(double a, double b, double c) : x(a), y(b), z(c) {}
  Vec3 operator+(const Vec3 &o) const { return {x + o.x, y + o.y, z + o.z}; }
  Vec3 operator-(const Vec3 &o) const { return {x - o.x, y - o.y, z - o.z}; }
  Vec3 operator*(double s) const { return {x * s, y * s, z * s}; }
  Vec3 operator/(double s) const { return {x / s, y / s, z / s}; }
  Vec3 &operator+=(const Vec3 &o) { x += o.x; y += o.y; z += o.z; return *this; }
  Vec3 operator-() const { return {-x, -y, -z}; }
  double dot(const Vec3 &o) const { return x * o.x + y * o.y + z * o.z; }
  Vec3 cross(const Vec3 &o) const { return {y * o.z - z * o.y, z * o.x - x * o.z, x * o.y - y * o.x}; }
  double operator[](int i) const { return i == 0 ? x : i == 1 ? y : z; }
  double &operator[](int i) { return i == 0 ? x : i == 1 ? y : z; }
  double norm() const { return std::sqrt(dot(*this)); }
  double norm2() const { return dot(*this); }
  Vec3 normalized() const { double n = norm(); return n > 0 ? *this / n : *this; }
};
inline Vec3 operator*(double s, const Vec3 &v) { return v * s; }
inline double det3(const Vec3 &a, const Vec3 &b, const Vec3 &c) { return a.dot(b.cross(c)); }

// ---------------- Mesh ----------------
// Cell types: 0 = Hex8, 1 = Prism6, 2 = Tet4, 3 = Pyr5
struct Mesh {
  std::vector<Vec3> V;
  struct Cell {
    int type = 0;
    std::vector<int> n;   // node ids (hex8 / prism6 / tet4 / pyr5 canonical order)
    int patch = -1;       // originating wall patch (-1 = core)
    int layer = -1;       // boundary-layer index (1..N), -1 = core
    double vol = 0.0;
    double jens = 1.0;
    double gauss = 0.0;   // ||sum S_f|| / V^(2/3)
  };
  std::vector<Cell> cells;

  // input watertight surface
  std::vector<std::array<int, 3>> surfT;
  std::vector<int> surfPatch;   // per surface triangle
  std::vector<char> surfIsWall; // per surface triangle: gets a boundary layer
  std::vector<std::string> patchNames;

  int addVertex(const Vec3 &p) { V.push_back(p); return (int)V.size() - 1; }
};

// canonical outward face loops (node indices local to the cell)
inline std::vector<std::vector<int>> hexFaces() {
  return {{0, 3, 2, 1}, {4, 5, 6, 7}, {0, 1, 5, 4}, {1, 2, 6, 5}, {2, 3, 7, 6}, {3, 0, 4, 7}};
}
inline std::vector<std::vector<int>> prismFaces() {
  // base 0,1,2 (outward), top 3,4,5, sides
  return {{0, 2, 1}, {3, 4, 5}, {0, 1, 4, 3}, {1, 2, 5, 4}, {2, 0, 3, 5}};
}
inline std::vector<std::vector<int>> tetFaces() {
  return {{0, 2, 1}, {0, 1, 3}, {1, 2, 3}, {2, 0, 3}};
}
inline std::vector<std::vector<int>> pyrFaces() {
  // base quad 0,1,2,3 (outward), apex 4
  return {{0, 3, 2, 1}, {0, 1, 4}, {1, 2, 4}, {2, 3, 4}, {3, 0, 4}};
}
inline const std::vector<std::vector<int>> &cellFaces(int type) {
  static const std::vector<std::vector<int>> h = hexFaces(), p = prismFaces(),
                                             t = tetFaces(), y = pyrFaces();
  static const std::vector<std::vector<int>> empty;
  switch (type) {
    case 0: return h;
    case 1: return p;
    case 2: return t;
    case 3: return y;
  }
  return empty;
}

// ---------------- JSON writer ----------------
struct JsonW {
  std::ostringstream o;
  std::vector<bool> first; // one entry per open container
  bool noComma = false;
  void sep() {
    if (noComma) { noComma = false; return; }
    if (!first.empty()) { if (first.back()) first.back() = false; else o << ','; }
  }
  void beginObj() { sep(); o << '{'; first.push_back(true); }
  void endObj() { o << '}'; first.pop_back(); }
  void beginArr() { sep(); o << '['; first.push_back(true); }
  void endArr() { o << ']'; first.pop_back(); }
  void key(const std::string &k) { sep(); o << "\"" << k << "\":"; noComma = true; }
  void val(double v) { sep(); char b[64]; std::snprintf(b, 64, "%.10g", v); o << b; }
  void val(long long v) { sep(); o << v; }
  void val(int v) { sep(); o << v; }
  void val(bool v) { sep(); o << (v ? "true" : "false"); }
  void val(const std::string &s) { sep(); o << "\"" << s << "\""; }
  std::string str() const { return o.str(); }
};
