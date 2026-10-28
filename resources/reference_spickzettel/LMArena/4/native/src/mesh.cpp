#include "engine.hpp"

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <sstream>

namespace fvmspm {
namespace {

std::string trim(const std::string &s) {
  size_t a = s.find_first_not_of(" \t\r\n");
  if (a == std::string::npos) return "";
  size_t b = s.find_last_not_of(" \t\r\n");
  return s.substr(a, b - a + 1);
}

std::vector<std::string> splitTokens(const std::string &s) {
  std::vector<std::string> out;
  std::string cur;
  for (char ch : s) {
    if (ch == ',' || ch == ' ' || ch == '\t' || ch == ';') {
      if (!cur.empty()) {
        out.push_back(cur);
        cur.clear();
      }
    } else {
      cur.push_back(ch);
    }
  }
  if (!cur.empty()) out.push_back(cur);
  return out;
}

bool insideObstacle(const std::vector<Obstacle> &obs, double x, double y, double inset) {
  for (const auto &o : obs) {
    if (o.type == 0) {
      const double dx = x - o.a, dy = y - o.b;
      if (dx * dx + dy * dy <= (o.c - inset) * (o.c - inset)) return true;
    } else {
      if (x >= o.a + inset && x <= o.c - inset && y >= o.b + inset && y <= o.d - inset) return true;
    }
  }
  return false;
}

// Signed distance to the solid domain (negative inside).
double solidDistance(const std::vector<Obstacle> &obs, double x, double y) {
  double best = 1e30;
  for (const auto &o : obs) {
    if (o.type == 0) {
      best = std::min(best, std::sqrt((x - o.a) * (x - o.a) + (y - o.b) * (y - o.b)) - o.c);
    } else {
      const double dx = std::max(std::max(o.a - x, x - o.c), 0.0);
      const double dy = std::max(std::max(o.b - y, y - o.d), 0.0);
      const double outside = std::sqrt(dx * dx + dy * dy);
      const double inside = std::min(std::min(x - o.a, o.c - x), std::min(y - o.b, o.d - y));
      best = std::min(best, outside > 0.0 ? outside : inside);
    }
  }
  return best;
}

double obstacleArea(const std::vector<Obstacle> &obs) {
  double a = 0.0;
  for (const auto &o : obs) {
    if (o.type == 0)
      a += M_PI * o.c * o.c;
    else
      a += std::max(0.0, (o.c - o.a)) * std::max(0.0, (o.d - o.b));
  }
  return a;
}

using Poly = std::vector<Vec2>;

// Sutherland-Hodgman clip, keep side where dot(p - a, n) <= 0.
void clipHalfPlane(Poly &poly, Vec2 a, Vec2 n, Poly &scratch) {
  scratch.clear();
  const size_t m = poly.size();
  if (m == 0) return;
  for (size_t i = 0; i < m; ++i) {
    const Vec2 p = poly[i];
    const Vec2 q = poly[(i + 1) % m];
    const double dp = dot(p - a, n);
    const double dq = dot(q - a, n);
    if (dp <= 0.0) scratch.push_back(p);
    if ((dp < 0.0 && dq > 0.0) || (dp > 0.0 && dq < 0.0)) {
      const double t = dp / (dp - dq);
      scratch.push_back(p + (q - p) * t);
    }
  }
  poly.swap(scratch);
}

double polygonArea(const Poly &p) {
  double s = 0.0;
  for (size_t i = 0; i < p.size(); ++i) {
    const Vec2 a = p[i], b = p[(i + 1) % p.size()];
    s += cross(a, b);
  }
  return 0.5 * s;
}

Vec2 polygonCentroid(const Poly &p) {
  double a = 0.0, cx = 0.0, cy = 0.0;
  for (size_t i = 0; i < p.size(); ++i) {
    const Vec2 u = p[i], v = p[(i + 1) % p.size()];
    const double c = cross(u, v);
    a += c;
    cx += (u.x + v.x) * c;
    cy += (u.y + v.y) * c;
  }
  if (std::fabs(a) < 1e-30) {
    Vec2 s{};
    for (const auto &q : p) s = s + q;
    return p.empty() ? Vec2{} : s * (1.0 / static_cast<double>(p.size()));
  }
  return {cx / (3.0 * a), cy / (3.0 * a)};
}

struct Seed {
  Vec2 p;
  int ghost = 0;
};

class PointGrid {
 public:
  PointGrid(double xmin, double xmax, double ymin, double ymax, double h)
      : xmin_(xmin), ymin_(ymin), h_(h) {
    nx_ = std::max(1, static_cast<int>(std::ceil((xmax - xmin) / h_)));
    ny_ = std::max(1, static_cast<int>(std::ceil((ymax - ymin) / h_)));
    bins_.resize(static_cast<size_t>(nx_) * ny_);
  }
  void insert(const Vec2 &p, int id) {
    const int i = std::clamp(static_cast<int>((p.x - xmin_) / h_), 0, nx_ - 1);
    const int j = std::clamp(static_cast<int>((p.y - ymin_) / h_), 0, ny_ - 1);
    bins_[static_cast<size_t>(j) * nx_ + i].push_back(id);
  }
  template <typename F>
  void query(double x, double y, double radius, F &&fn) const {
    const int i0 = std::clamp(static_cast<int>(std::floor((x - radius - xmin_) / h_)), 0, nx_ - 1);
    const int i1 = std::clamp(static_cast<int>(std::floor((x + radius - xmin_) / h_)), 0, nx_ - 1);
    const int j0 = std::clamp(static_cast<int>(std::floor((y - radius - ymin_) / h_)), 0, ny_ - 1);
    const int j1 = std::clamp(static_cast<int>(std::floor((y + radius - ymin_) / h_)), 0, ny_ - 1);
    for (int j = j0; j <= j1; ++j)
      for (int i = i0; i <= i1; ++i)
        for (int id : bins_[static_cast<size_t>(j) * nx_ + i]) fn(id);
  }
  int sizeX() const { return nx_; }
  int sizeY() const { return ny_; }
  const std::vector<int> &bin(int i, int j) const {
    return bins_[static_cast<size_t>(j) * nx_ + i];
  }

 private:
  double xmin_, ymin_, h_;
  int nx_ = 1, ny_ = 1;
  std::vector<std::vector<int>> bins_;
};

// Deterministic hash -> deterministic meshes for identical parameters.
inline double hash01(int a, int b, int salt) {
  uint64_t z = static_cast<uint64_t>(a) * 0x9E3779B97F4A7C15ULL +
               static_cast<uint64_t>(b) * 0xC2B2AE3D27D4EB4FULL +
               static_cast<uint64_t>(salt) * 0x165667B19E3779F9ULL;
  z ^= z >> 30;
  z *= 0xBF58476D1CE4E5B9ULL;
  z ^= z >> 27;
  z *= 0x94D049BB133111EBULL;
  z ^= z >> 31;
  return static_cast<double>(z % 1000000ULL) / 1000000.0;
}

std::vector<Seed> generateSeeds(const Config &cfg, MeshOptions &opts, double &hOut) {
  const double domain = (cfg.xmax - cfg.xmin) * (cfg.ymax - cfg.ymin);
  const double solid = obstacleArea(cfg.obstacles);
  double fluid = std::max(domain - solid, 1e-9);
  double h = cfg.spacing > 0.0 ? cfg.spacing
                               : std::sqrt(fluid / std::max(200, cfg.target) * 1.15);
  h = std::clamp(h, 1e-3 * std::sqrt(domain), 0.5 * std::sqrt(domain));
  hOut = h;
  opts.spacing = h;
  opts.jitter = cfg.jitter;
  opts.wallLayer = cfg.wallLayer;

  std::vector<Seed> seeds;
  seeds.reserve(static_cast<size_t>(cfg.target) * 2);

  const double dx = h, dy = h * std::sqrt(3.0) / 2.0;
  const int ny = static_cast<int>(std::ceil((cfg.ymax - cfg.ymin) / dy)) + 2;
  const int nx = static_cast<int>(std::ceil((cfg.xmax - cfg.xmin) / dx)) + 2;
  const double jitter = std::clamp(cfg.jitter, 0.0, 0.45);
  for (int j = 0; j < ny; ++j) {
    for (int i = 0; i < nx; ++i) {
      const double x = cfg.xmin + (i + 0.5 + 0.5 * (j & 1)) * dx;
      const double y = cfg.ymin + (j + 0.5) * dy;
      if (x <= cfg.xmin || x >= cfg.xmax || y <= cfg.ymin || y >= cfg.ymax) continue;
      Vec2 p{x, y};
      if (jitter > 0.0) {
        p.x += (hash01(i, j, 11) - 0.5) * 2.0 * jitter * h;
        p.y += (hash01(i, j, 29) - 0.5) * 2.0 * jitter * h;
      }
      p.x = std::clamp(p.x, cfg.xmin + 0.12 * h, cfg.xmax - 0.12 * h);
      p.y = std::clamp(p.y, cfg.ymin + 0.12 * h, cfg.ymax - 0.12 * h);
      if (insideObstacle(cfg.obstacles, p.x, p.y, 0.0)) continue;
      if (!cfg.obstacles.empty() && solidDistance(cfg.obstacles, p.x, p.y) > -0.2 * h &&
          solidDistance(cfg.obstacles, p.x, p.y) < 0.18 * h)
        continue;
      seeds.push_back({p, 0});
    }
  }

  // Refined wall ring (boundary layer style seeding next to solid/box walls).
  if (cfg.wallLayer > 0.0) {
    const double hs = h * 0.55;
    const double off = 0.3 * h;
    auto addRing = [&](double x, double y) {
      if (x <= cfg.xmin + 0.05 * h || x >= cfg.xmax - 0.05 * h) return;
      if (y <= cfg.ymin + 0.05 * h || y >= cfg.ymax - 0.05 * h) return;
      if (insideObstacle(cfg.obstacles, x, y, 0.05 * h)) return;
      seeds.push_back({{x, y}, 0});
    };
    for (double y = cfg.ymin + off; y < cfg.ymax; y += hs) {
      addRing(cfg.xmin + off, y);
      addRing(cfg.xmax - off, y);
    }
    for (double x = cfg.xmin + off; x < cfg.xmax; x += hs) {
      addRing(x, cfg.ymin + off);
      addRing(x, cfg.ymax - off);
    }
    for (const auto &o : cfg.obstacles) {
      if (o.type != 0) continue;
      const double circumference = 2.0 * M_PI * (o.c + off);
      const int n = std::max(8, static_cast<int>(circumference / hs));
      for (int k = 0; k < n; ++k) {
        const double th = 2.0 * M_PI * k / n;
        addRing(o.a + (o.c + off) * std::cos(th), o.b + (o.c + off) * std::sin(th));
      }
    }
  }

  // Interior (ghost) seeds carve the solid region out of the Voronoi diagram so
  // that every fluid cell stays convex while the wall stays conforming.
  for (const auto &o : cfg.obstacles) {
    if (o.type == 0) {
      const double hg = h * 0.85;
      for (double y = o.b - o.c; y <= o.b + o.c; y += hg) {
        for (double x = o.a - o.c; x <= o.a + o.c; x += hg) {
          const double ddx = x - o.a, ddy = y - o.b;
          if (ddx * ddx + ddy * ddy <= (o.c - 0.05 * h) * (o.c - 0.05 * h))
            seeds.push_back({{x, y}, 1});
        }
      }
      const double circumference = 2.0 * M_PI * (o.c - 0.25 * h);
      const int n = std::max(8, static_cast<int>(circumference / (0.5 * h)));
      for (int k = 0; k < n; ++k) {
        const double th = 2.0 * M_PI * k / n;
        seeds.push_back({{o.a + (o.c - 0.25 * h) * std::cos(th),
                          o.b + (o.c - 0.25 * h) * std::sin(th)},
                         1});
      }
    } else {
      const double hg = h * 0.85;
      for (double y = o.b + 0.05 * h; y <= o.d - 0.05 * h; y += hg)
        for (double x = o.a + 0.05 * h; x <= o.c - 0.05 * h; x += hg)
          seeds.push_back({{x, y}, 1});
    }
  }

  // Remove duplicates / near-duplicates (uniform grid with 0.4h acceptance).
  std::vector<Seed> unique;
  unique.reserve(seeds.size());
  PointGrid grid(cfg.xmin, cfg.xmax, cfg.ymin, cfg.ymax, h);
  for (const auto &s : seeds) {
    bool keep = true;
    grid.query(s.p.x, s.p.y, 0.4 * h, [&](int id) {
      if (!keep) return;
      const auto &o = unique[static_cast<size_t>(id)];
      const double dd = (o.p.x - s.p.x) * (o.p.x - s.p.x) + (o.p.y - s.p.y) * (o.p.y - s.p.y);
      if (dd < 0.16 * h * h && o.ghost == s.ghost) keep = false;
    });
    if (!keep) continue;
    unique.push_back(s);
    grid.insert(s.p, static_cast<int>(unique.size()) - 1);
  }
  return unique;
}

}  // namespace

Config parseConfig(const std::string &text) {
  Config cfg;
  std::istringstream in(text);
  std::string line;
  auto setIf = [](const std::string &tok, double &target) {
    if (!tok.empty()) target = std::atof(tok.c_str());
  };
  while (std::getline(in, line)) {
    const size_t c = line.find('#');
    if (c != std::string::npos) line = line.substr(0, c);
    line = trim(line);
    if (line.empty()) continue;
    const size_t eq = line.find('=');
    if (eq == std::string::npos) continue;
    const std::string key = trim(line.substr(0, eq));
    const auto t = splitTokens(line.substr(eq + 1));
    auto f = [&](size_t i) { return i < t.size() ? t[i] : std::string(); };
    if (key == "op") cfg.op = f(0);
    else if (key == "xmin") setIf(f(0), cfg.xmin);
    else if (key == "xmax") setIf(f(0), cfg.xmax);
    else if (key == "ymin") setIf(f(0), cfg.ymin);
    else if (key == "ymax") setIf(f(0), cfg.ymax);
    else if (key == "target") cfg.target = std::max(16, std::atoi(f(0).c_str()));
    else if (key == "spacing") setIf(f(0), cfg.spacing);
    else if (key == "jitter") setIf(f(0), cfg.jitter);
    else if (key == "relax") cfg.relax = std::clamp(std::atoi(f(0).c_str()), 0, 20);
    else if (key == "walllayer") setIf(f(0), cfg.wallLayer);
    else if (key == "obstacle") {
      Obstacle o;
      if (f(0) == "circle" || f(0) == "0") {
        o.type = 0;
        o.a = std::atof(f(1).c_str());
        o.b = std::atof(f(2).c_str());
        o.c = std::atof(f(3).c_str());
        if (o.c > 0) cfg.obstacles.push_back(o);
      } else if (f(0) == "rect" || f(0) == "1") {
        o.type = 1;
        o.a = std::atof(f(1).c_str());
        o.b = std::atof(f(2).c_str());
        o.c = std::atof(f(3).c_str());
        o.d = std::atof(f(4).c_str());
        if (o.c > o.a && o.d > o.b) cfg.obstacles.push_back(o);
      }
    } else if (key == "ic") cfg.ic = f(0);
    else if (key == "bcleft") cfg.bcLeft = f(0);
    else if (key == "bcright") cfg.bcRight = f(0);
    else if (key == "bctop") cfg.bcTop = f(0);
    else if (key == "bcbottom") cfg.bcBottom = f(0);
    else if (key == "wallclosure") cfg.wallClosure = f(0);
    else if (key == "gamma") setIf(f(0), cfg.gamma);
    else if (key == "rho0") setIf(f(0), cfg.rho0);
    else if (key == "p0") setIf(f(0), cfg.p0);
    else if (key == "mach") setIf(f(0), cfg.mach);
    else if (key == "aoa") setIf(f(0), cfg.aoa);
    else if (key == "cfl") setIf(f(0), cfg.cfl);
    else if (key == "steps") cfg.steps = std::clamp(std::atoi(f(0).c_str()), 1, 4000);
    else if (key == "order") cfg.order = std::atoi(f(0).c_str()) >= 2 ? 2 : 1;
    else if (key == "snapshots") cfg.snapshots = std::clamp(std::atoi(f(0).c_str()), 0, 12);
    else if (key == "kappa") setIf(f(0), cfg.kappa);
    else if (key == "sensitivity") cfg.sensitivity = std::atoi(f(0).c_str());
    else if (key == "threads") cfg.threads = std::max(0, std::atoi(f(0).c_str()));
    else if (key == "benchmark") cfg.benchmark = std::atoi(f(0).c_str());
    else if (key == "interpolation") cfg.interpolation = std::atoi(f(0).c_str());
  }
  if (cfg.xmax <= cfg.xmin) cfg.xmax = cfg.xmin + 1.0;
  if (cfg.ymax <= cfg.ymin) cfg.ymax = cfg.ymin + 1.0;
  cfg.gamma = std::max(1.05, cfg.gamma);
  cfg.cfl = std::clamp(cfg.cfl, 0.02, 0.9);
  return cfg;
}

Mesh buildMesh(const Config &cfg, MeshOptions &opts) {
  Mesh mesh;
  mesh.domainArea = (cfg.xmax - cfg.xmin) * (cfg.ymax - cfg.ymin);
  mesh.solidArea = obstacleArea(cfg.obstacles);
  std::vector<Seed> seeds = generateSeeds(cfg, opts, mesh.spacing);
  const double h = mesh.spacing;

  // Uniform bin grid over the seed cloud for candidate selection.
  PointGrid grid(cfg.xmin, cfg.xmax, cfg.ymin, cfg.ymax, h);
  for (size_t i = 0; i < seeds.size(); ++i) grid.insert(seeds[i].p, static_cast<int>(i));

  std::vector<Poly> polys(seeds.size());
  std::vector<Vec2> centroids(seeds.size());
  std::vector<double> areas(seeds.size(), 0.0);
  std::vector<int> candidates;
  candidates.reserve(64);

  const double diag = std::hypot(cfg.xmax - cfg.xmin, cfg.ymax - cfg.ymin);
  Poly scratch;

  for (size_t idx = 0; idx < seeds.size(); ++idx) {
    const Vec2 s = seeds[idx].p;
    Poly poly{{cfg.xmin, cfg.ymin}, {cfg.xmax, cfg.ymin}, {cfg.xmax, cfg.ymax}, {cfg.xmin, cfg.ymax}};
    double r = 2.5 * h;
    for (int iter = 0; iter < 6; ++iter) {
      poly = Poly{{cfg.xmin, cfg.ymin}, {cfg.xmax, cfg.ymin}, {cfg.xmax, cfg.ymax}, {cfg.xmin, cfg.ymax}};
      candidates.clear();
      grid.query(s.x, s.y, r, [&](int id) {
        if (id == static_cast<int>(idx)) return;
        const Vec2 o = seeds[static_cast<size_t>(id)].p;
        const double dx = o.x - s.x, dy = o.y - s.y;
        if (dx * dx + dy * dy <= 4.0 * r * r) candidates.push_back(id);
      });
      for (int id : candidates) {
        const Vec2 o = seeds[static_cast<size_t>(id)].p;
        const Vec2 n{2.0 * (o.x - s.x), 2.0 * (o.y - s.y)};
        const Vec2 mid{(s.x + o.x) * 0.5, (s.y + o.y) * 0.5};
        clipHalfPlane(poly, mid, n, scratch);
        if (poly.size() < 3) break;
      }
      double maxDist = 0.0;
      for (const auto &v : poly) maxDist = std::max(maxDist, norm(v - s));
      if (maxDist < 0.45 * r || r > 4.0 * diag) break;
      r *= 2.0;
    }
    if (poly.size() < 3) continue;
    const double a = polygonArea(poly);
    if (a <= 1e-14 * h * h) continue;
    if (a < 0.0) {
      std::reverse(poly.begin(), poly.end());
      areas[idx] = -a;
    } else {
      areas[idx] = a;
    }
    polys[idx] = std::move(poly);
    centroids[idx] = polygonCentroid(polys[idx]);
  }

  // Lloyd-style relaxation (SPH particle relaxation for zero-order consistency).
  for (int it = 0; it < cfg.relax; ++it) {
    for (size_t i = 0; i < seeds.size(); ++i) {
      if (seeds[i].ghost || polys[i].empty()) continue;
      Vec2 target = centroids[i];
      const Vec2 d = target - seeds[i].p;
      const double scale = 0.6;
      Vec2 np{seeds[i].p.x + d.x * scale, seeds[i].p.y + d.y * scale};
      np.x = std::clamp(np.x, cfg.xmin + 0.2 * h, cfg.xmax - 0.2 * h);
      np.y = std::clamp(np.y, cfg.ymin + 0.2 * h, cfg.ymax - 0.2 * h);
      if (insideObstacle(cfg.obstacles, np.x, np.y, 0.0)) continue;
      seeds[i].p = np;
    }
    // Rebuild with the relaxed positions.
    for (size_t i = 0; i < seeds.size(); ++i) {
      polys[i].clear();
      areas[i] = 0.0;
    }
    PointGrid g2(cfg.xmin, cfg.xmax, cfg.ymin, cfg.ymax, h);
    for (size_t i = 0; i < seeds.size(); ++i) g2.insert(seeds[i].p, static_cast<int>(i));
    for (size_t idx = 0; idx < seeds.size(); ++idx) {
      const Vec2 s = seeds[idx].p;
      Poly poly{{cfg.xmin, cfg.ymin}, {cfg.xmax, cfg.ymin}, {cfg.xmax, cfg.ymax}, {cfg.xmin, cfg.ymax}};
      double r = 2.5 * h;
      for (int iter = 0; iter < 6; ++iter) {
        poly = Poly{{cfg.xmin, cfg.ymin}, {cfg.xmax, cfg.ymin}, {cfg.xmax, cfg.ymax}, {cfg.xmin, cfg.ymax}};
        g2.query(s.x, s.y, r, [&](int id) {
          if (id == static_cast<int>(idx)) return;
          const Vec2 o = seeds[static_cast<size_t>(id)].p;
          const Vec2 n{2.0 * (o.x - s.x), 2.0 * (o.y - s.y)};
          const Vec2 mid{(s.x + o.x) * 0.5, (s.y + o.y) * 0.5};
          clipHalfPlane(poly, mid, n, scratch);
        });
        double maxDist = 0.0;
        for (const auto &v : poly) maxDist = std::max(maxDist, norm(v - s));
        if (maxDist < 0.45 * r || r > 4.0 * diag) break;
        r *= 2.0;
      }
      if (poly.size() < 3) continue;
      double a = polygonArea(poly);
      if (a < 0.0) {
        std::reverse(poly.begin(), poly.end());
        a = -a;
      }
      if (a <= 1e-14 * h * h) continue;
      polys[idx] = std::move(poly);
      areas[idx] = a;
      centroids[idx] = polygonCentroid(polys[idx]);
    }
  }

  // Keep only fluid cells: a cell is fluid when its centroid is outside solid.
  std::vector<int> remap(seeds.size(), -1);
  for (size_t i = 0; i < seeds.size(); ++i) {
    if (polys[i].size() < 3) continue;
    const Vec2 c = centroids[i];
    if (insideObstacle(cfg.obstacles, c.x, c.y, 0.0)) continue;
    if (c.x <= cfg.xmin || c.x >= cfg.xmax || c.y <= cfg.ymin || c.y >= cfg.ymax) continue;
    remap[i] = static_cast<int>(mesh.cells.size());
    Cell cell;
    cell.sx = seeds[i].p.x;
    cell.sy = seeds[i].p.y;
    cell.cx = c.x;
    cell.cy = c.y;
    cell.area = areas[i];
    mesh.cells.push_back(std::move(cell));
  }

  // Shared vertex / face topology.
  const double ext = std::max(cfg.xmax - cfg.xmin, cfg.ymax - cfg.ymin);
  const double eps = ext * 1e-8;
  std::unordered_map<long long, int> vmap;
  vmap.reserve(mesh.cells.size() * 8);
  struct EdgeKey {
    int a, b;
    bool operator==(const EdgeKey &o) const { return a == o.a && b == o.b; }
  };
  struct EdgeHash {
    size_t operator()(const EdgeKey &k) const {
      return std::hash<long long>()((static_cast<long long>(k.a) << 32) ^ k.b);
    }
  };
  std::unordered_map<EdgeKey, int, EdgeHash> fmap;
  fmap.reserve(mesh.cells.size() * 6);

  auto vertexId = [&](const Vec2 &p) {
    const long long qx = static_cast<long long>(std::llround(p.x / eps));
    const long long qy = static_cast<long long>(std::llround(p.y / eps));
    const long long key = (static_cast<long long>(static_cast<uint32_t>(qx)) << 32) |
                          static_cast<long long>(static_cast<uint32_t>(qy) & 0xffffffffU);
    auto it = vmap.find(key);
    if (it != vmap.end()) return it->second;
    const int id = static_cast<int>(mesh.verts.size());
    mesh.verts.push_back(p);
    vmap.emplace(key, id);
    return id;
  };

  for (size_t oldIdx = 0; oldIdx < seeds.size(); ++oldIdx) {
    const int newIdx = remap[oldIdx];
    if (newIdx < 0) continue;
    const Poly &poly = polys[oldIdx];
    const size_t m = poly.size();
    for (size_t k = 0; k < m; ++k) {
      const Vec2 p0 = poly[k], p1 = poly[(k + 1) % m];
      const int a = vertexId(p0);
      const int b = vertexId(p1);
      if (a == b) continue;
      const double len = norm(p1 - p0);
      if (len < 1e-12 * ext) continue;
      const Vec2 d = p1 - p0;
      const Vec2 nrm{d.y / len, -d.x / len};  // outward for CCW polygons
      const EdgeKey key{std::min(a, b), std::max(a, b)};
      auto it = fmap.find(key);
      if (it == fmap.end()) {
        Face f;
        f.v0 = a;
        f.v1 = b;
        f.owner = newIdx;
        f.len = len;
        f.nx = nrm.x;
        f.ny = nrm.y;
        f.cx = 0.5 * (p0.x + p1.x);
        f.cy = 0.5 * (p0.y + p1.y);
        const bool onBox = f.cx <= cfg.xmin + 1e-6 * ext || f.cx >= cfg.xmax - 1e-6 * ext ||
                           f.cy <= cfg.ymin + 1e-6 * ext || f.cy >= cfg.ymax - 1e-6 * ext;
        f.bc = onBox ? 1 : 2;
        fmap.emplace(key, static_cast<int>(mesh.faces.size()));
        mesh.faces.push_back(f);
      } else {
        Face &f = mesh.faces[static_cast<size_t>(it->second)];
        if (f.nbr >= 0 || f.owner == newIdx) continue;
        f.nbr = newIdx;
        f.bc = 0;
        // Orient from owner to neighbour.
        const Vec2 toNbr{mesh.cells[static_cast<size_t>(f.nbr)].cx - mesh.cells[static_cast<size_t>(f.owner)].cx,
                         mesh.cells[static_cast<size_t>(f.nbr)].cy - mesh.cells[static_cast<size_t>(f.owner)].cy};
        if (dot(Vec2{f.nx, f.ny}, toNbr) < 0.0) {
          f.nx = -f.nx;
          f.ny = -f.ny;
        }
      }
    }
  }

  mesh.faces.erase(std::remove_if(mesh.faces.begin(), mesh.faces.end(),
                                  [](const Face &f) { return f.owner < 0; }),
                   mesh.faces.end());

  // Drop degenerate / sliver cells before topology finalisation.
  std::vector<int> keep(mesh.cells.size(), 1);
  for (size_t i = 0; i < mesh.cells.size(); ++i)
    if (mesh.cells[i].area < 1e-2 * h * h) keep[i] = 0;
  if (std::find(keep.begin(), keep.end(), 0) != keep.end()) {
    std::vector<int> newIdx(mesh.cells.size(), -1);
    std::vector<Cell> kept;
    for (size_t i = 0; i < mesh.cells.size(); ++i) {
      if (!keep[i]) continue;
      newIdx[i] = static_cast<int>(kept.size());
      kept.push_back(mesh.cells[i]);
    }
    std::vector<Face> keptFaces;
    for (auto f : mesh.faces) {
      if (!keep[static_cast<size_t>(f.owner)]) continue;
      f.owner = newIdx[static_cast<size_t>(f.owner)];
      if (f.nbr >= 0) {
        if (!keep[static_cast<size_t>(f.nbr)]) {
          f.nbr = -1;
          f.bc = f.bc == 0 ? 2 : f.bc;
        } else {
          f.nbr = newIdx[static_cast<size_t>(f.nbr)];
        }
      }
      keptFaces.push_back(f);
    }
    mesh.cells.swap(kept);
    mesh.faces.swap(keptFaces);
  }

  buildCsr(mesh);

  // Geometric quality metrics (walked through the CSR cell->face graph).
  const int ncells = static_cast<int>(mesh.cells.size());
  for (int i = 0; i < ncells; ++i) {
    auto &cell = mesh.cells[static_cast<size_t>(i)];
    double rIn = 1e30, rOut = 0.0, areaSum = 0.0;
    for (int k = mesh.offsets[static_cast<size_t>(i)]; k < mesh.offsets[static_cast<size_t>(i) + 1]; ++k) {
      const auto &f = mesh.faces[static_cast<size_t>(mesh.indices[static_cast<size_t>(k)])];
      const Vec2 mid{f.cx - cell.cx, f.cy - cell.cy};
      rIn = std::min(rIn, norm(mid));
      rOut = std::max(rOut, norm(mid));
      areaSum += f.len;
    }
    cell.quality = (rOut > 0.0 && std::isfinite(rIn)) ? std::clamp(rIn / rOut, 0.0, 1.0) : 0.0;
    if (cell.area <= 0.0) cell.area = std::max(1e-18, 0.25 * areaSum * areaSum);
  }

  computeSphConsistency(mesh, cfg.kappa);  return mesh;
}

void buildCsr(Mesh &mesh) {
  const int nc = static_cast<int>(mesh.cells.size());
  mesh.offsets.assign(static_cast<size_t>(nc) + 1, 0);
  for (const auto &f : mesh.faces) {
    mesh.offsets[static_cast<size_t>(f.owner) + 1]++;
    if (f.nbr >= 0) mesh.offsets[static_cast<size_t>(f.nbr) + 1]++;
  }
  for (int i = 0; i < nc; ++i) mesh.offsets[static_cast<size_t>(i) + 1] += mesh.offsets[static_cast<size_t>(i)];
  mesh.indices.assign(static_cast<size_t>(mesh.offsets[static_cast<size_t>(nc)]), 0);
  std::vector<int> cursor(mesh.offsets.begin(), mesh.offsets.end() - 1);
  for (size_t fi = 0; fi < mesh.faces.size(); ++fi) {
    const auto &f = mesh.faces[fi];
    mesh.indices[static_cast<size_t>(cursor[static_cast<size_t>(f.owner)]++)] = static_cast<int>(fi);
    if (f.nbr >= 0) mesh.indices[static_cast<size_t>(cursor[static_cast<size_t>(f.nbr)]++)] = static_cast<int>(fi);
  }
}

void computeSphConsistency(Mesh &mesh, double /*kappa*/) {
  const int n = static_cast<int>(mesh.cells.size());
  if (n == 0) return;
  const double h = mesh.spacing > 0.0 ? mesh.spacing : 1.0;
  double sum = 0.0, maxv = 0.0;
  for (int i = 0; i < n; ++i) {
    const Vec2 ci{mesh.cells[static_cast<size_t>(i)].cx, mesh.cells[static_cast<size_t>(i)].cy};
    double rx = 0.0, ry = 0.0, s = 0.0, norm2 = 0.0;
    for (int k = mesh.offsets[static_cast<size_t>(i)]; k < mesh.offsets[static_cast<size_t>(i) + 1]; ++k) {
      const auto &f = mesh.faces[static_cast<size_t>(mesh.indices[static_cast<size_t>(k)])];
      const int j = (f.owner == i) ? f.nbr : f.owner;
      if (j >= 0) {
        const Vec2 cj{mesh.cells[static_cast<size_t>(j)].cx, mesh.cells[static_cast<size_t>(j)].cy};
        const Vec2 dx = cj - ci;
        const double vol = mesh.cells[static_cast<size_t>(j)].area;
        const KernelGrad g = wendlandGrad(dx, h);
        rx += vol * g.gx;
        ry += vol * g.gy;
        s += vol * std::hypot(g.gx, g.gy);
        norm2 += vol * wendland(norm(dx), h);
      } else {
        // Virtual boundary particle mirrored across the wall face (FVM
        // boundary-equivalent contribution of the Eulerian SPH formulation).
        const Vec2 nrm{f.nx, f.ny};
        const Vec2 mid{f.cx, f.cy};
        const double dist = dot(ci - mid, nrm);
        const Vec2 ghost = ci - nrm * (2.0 * dist);
        const Vec2 dx = ghost - ci;
        const double vol = mesh.cells[static_cast<size_t>(i)].area * 0.75;
        const KernelGrad g = wendlandGrad(dx, h);
        rx += vol * g.gx;
        ry += vol * g.gy;
        s += vol * std::hypot(g.gx, g.gy);
        norm2 += vol * wendland(norm(dx), h);
      }
    }
    double def = (s > 1e-30) ? std::hypot(rx, ry) / s : 0.0;
    def = std::clamp(def, 0.0, 1.0);
    mesh.cells[static_cast<size_t>(i)].consistency = def;
    mesh.cells[static_cast<size_t>(i)].kernelSum = norm2;
    sum += def;
    maxv = std::max(maxv, def);
  }
  (void)sum;
  (void)maxv;
}

InterpolationStats particleMeshInterpolation(const Config &cfg, const Mesh &mesh, const State &state) {
  InterpolationStats st;
  const int nc = static_cast<int>(mesh.cells.size());
  if (nc == 0) return st;
  const auto t0 = std::chrono::high_resolution_clock::now();
  const double h = mesh.spacing;
  // Particle cloud: the SPH seed cloud carrying Lagrangian parcels of the FVM
  // state. Support-normalised conservative weights (partition of unity).
  std::vector<double> mass(static_cast<size_t>(nc), 0.0);
  std::vector<double> volume(static_cast<size_t>(nc), 0.0);
  std::vector<double> density(static_cast<size_t>(nc), 0.0);
  double totalMassP = 0.0;
  long long nnz = 0, stencilCount = 0;

  PointGrid grid(cfg.xmin, cfg.xmax, cfg.ymin, cfg.ymax, h);
  for (int c = 0; c < nc; ++c) grid.insert(Vec2{mesh.cells[static_cast<size_t>(c)].cx, mesh.cells[static_cast<size_t>(c)].cy}, c);

  std::vector<double> raw;
  std::vector<int> ids;
  for (int p = 0; p < nc; ++p) {
    const Vec2 xp{mesh.cells[static_cast<size_t>(p)].sx, mesh.cells[static_cast<size_t>(p)].sy};
    const double mp = state.rho[static_cast<size_t>(p)] * mesh.cells[static_cast<size_t>(p)].area;
    totalMassP += mp;
    raw.clear();
    ids.clear();
    double wsum = 0.0;
    grid.query(xp.x, xp.y, 2.0 * h, [&](int c) {
      const Vec2 dx = xp - Vec2{mesh.cells[static_cast<size_t>(c)].cx, mesh.cells[static_cast<size_t>(c)].cy};
      const double w = wendland(norm(dx), h) * mesh.cells[static_cast<size_t>(c)].area;
      if (w <= 0.0) return;
      ids.push_back(c);
      raw.push_back(w);
      wsum += w;
    });
    if (ids.empty() || wsum <= 0.0) continue;
    for (size_t k = 0; k < ids.size(); ++k) {
      const double wk = raw[k] / wsum;
      mass[static_cast<size_t>(ids[k])] += mp * wk;
    }
    nnz += static_cast<long long>(ids.size());
    stencilCount += static_cast<long long>(ids.size());
  }
  double err2 = 0.0, errInf = 0.0, wsum = 0.0;
  for (int c = 0; c < nc; ++c) {
    const double vol = mesh.cells[static_cast<size_t>(c)].area;
    volume[static_cast<size_t>(c)] = vol;
    density[static_cast<size_t>(c)] = vol > 0.0 ? mass[static_cast<size_t>(c)] / vol : 0.0;
    const double e = density[static_cast<size_t>(c)] - state.rho[static_cast<size_t>(c)];
    err2 += e * e * vol;
    errInf = std::max(errInf, std::fabs(e));
    wsum += density[static_cast<size_t>(c)] * vol;
  }
  st.nnz = static_cast<int>(nnz);
  st.meanStencil = stencilCount > 0 ? static_cast<double>(stencilCount) / nc : 0.0;
  st.conservationError = totalMassP > 0.0 ? std::fabs(wsum - totalMassP) / totalMassP : 0.0;
  st.l2Error = std::sqrt(err2 / std::max(1e-30, mesh.domainArea - mesh.solidArea));
  st.linfError = errInf;
  st.sparsity = nc > 0 ? static_cast<double>(nnz) / (static_cast<double>(nc) * nc) : 0.0;
  st.timeMs = std::chrono::duration<double, std::milli>(std::chrono::high_resolution_clock::now() - t0).count();
  return st;
}

}  // namespace fvmspm
