// FVM / SPH Mesher + solver core.
// Implements:
//  - Voronoi (finite-volume) mesh generation from SPH-like particle seeds,
//    with cell-centred graph (CSR) topology, wall/box/obstacle face tagging.
//  - Eulerian-SPH zero-order consistency analysis (Wendland kernel) and a
//    boundary-consistent wall closure for the FVM Riemann problem.
//  - Graph-based, vectorised compressible FVM solver (HLLC) with optional
//    MUSCL / Barth-Jespersen limiting.
//  - Conservative particle<->mesh interpolation operator (P2M / M2P).
//  - Design sensitivities of a goal functional (finite difference through
//    the vectorised graph kernel).
#pragma once

#include <cmath>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

namespace fvmspm {

struct Vec2 {
  double x = 0.0, y = 0.0;
};

inline Vec2 operator+(Vec2 a, Vec2 b) { return {a.x + b.x, a.y + b.y}; }
inline Vec2 operator-(Vec2 a, Vec2 b) { return {a.x - b.x, a.y - b.y}; }
inline Vec2 operator*(Vec2 a, double s) { return {a.x * s, a.y * s}; }
inline double dot(Vec2 a, Vec2 b) { return a.x * b.x + a.y * b.y; }
inline double cross(Vec2 a, Vec2 b) { return a.x * b.y - a.y * b.x; }
inline double norm(Vec2 a) { return std::sqrt(dot(a, a)); }

struct Obstacle {
  int type = 0;  // 0 = circle, 1 = axis aligned rectangle
  double a = 0, b = 0, c = 0, d = 0;
};

// ---------------------------------------------------------------------------
// Configuration (simple line oriented `key=tokens...` protocol, no external
// dependencies so that the core can be spawned as a plain subprocess).
// ---------------------------------------------------------------------------
struct Config {
  std::string op = "mesh";
  double xmin = 0.0, xmax = 1.0, ymin = 0.0, ymax = 1.0;
  int target = 2000;
  double spacing = 0.0;
  double jitter = 0.12;
  int relax = 2;
  double wallLayer = 0.0;  // >0 adds a refined wall ring of seeds
  std::vector<Obstacle> obstacles;

  // solver
  std::string ic = "shock_obstacle";
  std::string bcLeft = "inlet", bcRight = "outlet", bcTop = "wall", bcBottom = "wall";
  std::string wallClosure = "sph";  // "sph" | "mirror"
  double gamma = 1.4, rho0 = 1.0, p0 = 1.0, mach = 2.0, aoa = 0.0;
  double cfl = 0.35;
  int steps = 300;
  int order = 1;
  int snapshots = 6;
  double kappa = 1.0;      // boundary consistency coefficient scaling
  int sensitivity = 0;
  int threads = 0;
  int benchmark = 0;
  int interpolation = 1;
};

Config parseConfig(const std::string &text);

struct Face {
  int v0 = 0, v1 = 0;
  int owner = -1, nbr = -1;
  double len = 0.0, cx = 0.0, cy = 0.0, nx = 0.0, ny = 0.0;
  int bc = 0;  // 0 interior, 1 domain wall, 2 obstacle wall, 3 inlet, 4 outlet
};

struct Cell {
  double sx = 0.0, sy = 0.0;      // seed (SPH particle position)
  double cx = 0.0, cy = 0.0;      // geometric centroid
  double area = 0.0;
  double quality = 0.0;           // 2*r_in / r_circ, 1 = perfect hexagon
  double consistency = 0.0;       // zero-order consistency deficiency
  double kernelSum = 0.0;
  int ghost = 0;
  std::vector<int> faces;
};

struct Mesh {
  std::vector<Vec2> verts;
  std::vector<Cell> cells;
  std::vector<Face> faces;
  std::vector<int> offsets;  // CSR, size cells+1
  std::vector<int> indices;
  double spacing = 0.0;
  double domainArea = 0.0;
  double solidArea = 0.0;
};

struct MeshOptions {
  double spacing = 0.0;
  double jitter = 0.12;
  int relax = 0;
  double wallLayer = 0.0;
};

Mesh buildMesh(const Config &cfg, MeshOptions &opts);
void buildCsr(Mesh &mesh);
void computeSphConsistency(Mesh &mesh, double kappa);

// ---------------------------------------------------------------------------
// Wendland C2 kernel in 2D, compact support radius 2h.
// ---------------------------------------------------------------------------
inline double wendland(double r, double h) {
  const double q = r / h;
  if (q >= 2.0) return 0.0;
  const double t = 1.0 - 0.5 * q;
  return (7.0 / (4.0 * M_PI * h * h)) * t * t * t * t * (2.0 * q + 1.0);
}

struct KernelGrad {
  double gx = 0.0, gy = 0.0;
};

inline KernelGrad wendlandGrad(Vec2 dx, double h) {
  const double r = norm(dx);
  if (r < 1e-14 || r >= 2.0 * h) return {};
  const double q = r / h;
  const double t = 1.0 - 0.5 * q;
  // dW/dr for W = (7/(4 pi h^2)) t^4 (2q+1)
  const double dWdr = (7.0 / (4.0 * M_PI * h * h * h)) *
                      (-4.0 * t * t * t * 2.0 * 0.5 * (2.0 * q + 1.0) + t * t * t * t * 2.0);
  return {dWdr * dx.x / r, dWdr * dx.y / r};
}

struct State {
  std::vector<double> rho, u, v, p;
  int n = 0;
};

struct Snapshot {
  int step = 0;
  double time = 0.0, dt = 0.0;
  double mass = 0.0, momentum = 0.0, energy = 0.0;
  double maxMach = 0.0, minRho = 0.0, maxRho = 0.0;
  double drag = 0.0, lift = 0.0;
  std::vector<float> rho, u, v, p, ma;
};

struct InterpolationStats {
  int nnz = 0;
  double meanStencil = 0.0;
  double conservationError = 0.0;
  double l2Error = 0.0;
  double linfError = 0.0;
  double sparsity = 0.0;
  double timeMs = 0.0;
};

struct SolveResult {
  std::vector<Snapshot> snapshots;
  InterpolationStats interp;
  double drag = 0.0, lift = 0.0, totalMass = 0.0, totalEnergy = 0.0;
  double finalTime = 0.0;
  double steps = 0.0;
  double wallDeficiencyMean = 0.0, wallDeficiencyMax = 0.0;
  double avgDt = 0.0;
  double solveMs = 0.0;
  double facesPerSecond = 0.0;
  double gradMach = 0.0, gradKappa = 0.0;
  int sensitivityRuns = 0;
  double sensitivityMs = 0.0;
  std::vector<std::vector<double>> surface;  // obstacle face data: cx,cy,nx,ny,len,p
};

SolveResult solve(const Config &cfg, const Mesh &mesh);

// Conservative particle <-> mesh interpolation check (paper 2 kernel:
// optimised particle-mesh interpolation with support-normalised weights).
InterpolationStats particleMeshInterpolation(const Config &cfg, const Mesh &mesh,
                                            const State &state);

// ---------- JSON serialisation helpers ----------
std::string meshToJson(const Mesh &mesh, const Config &cfg, double buildMs,
                       double seedCount, double cellCount);
std::string solveToJson(const SolveResult &res, const Mesh &mesh, const Config &cfg);
std::string stateToJson(const State &state, const Mesh &mesh);
std::string probeResidual(const Config &cfg, const Mesh &mesh);

}  // namespace fvmspm
