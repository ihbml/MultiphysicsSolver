// g++ -std=c++20 -O2 octree_hexcore.cpp -o hexalab
// Verified analytic fixtures, not a general-purpose BREP importer.
#include "engine/fa_mesh.hpp"
#include "engine/templates.hpp"
#include <memory>

struct Params {
    int geometry = 0, layers = 6;
    double first = .18, growth = 1.22;
};
struct Result {
    Points p;
    std::vector<Cell> cells;
    std::vector<Boundary> boundary;
    Params par;
    std::array<int, 9> counts{};
    std::array<int, 4> dofs{};
    int squeezed = 0, faceCount = 0, unmatched = 0, nonmanifold = 0, negative = 0, retries = 0;
    double minQ = 1, minV = 1e100, maxClosure = 0, meanQ = 0, totalV = 0, expectedV = 0,
           minVisibility = 1, minPrism = 1, minStar = 1e100, elapsed = 0, maxRatio = 0;
    Vec extent;
    std::string id;
    int surfaceVertices = 0, coarseParents = 0, coarseHexes = 0, hangingNodes = 0;
};
static const SuperElements &library() {
    static SuperElements s;
    return s;
}
static const char *patchNames[] = {"floor", "ceiling", "front", "outlet", "back", "inlet"};

static Result generate(Params par) {
    auto start = std::chrono::steady_clock::now();
    Result m;
    m.par = par;
    if (par.geometry < 0 || par.geometry > 1 || par.layers < 1 || par.layers > 12 ||
        !(par.first >= .015 && par.first <= 2.) || !(par.growth >= 1 && par.growth <= 1.6))
        throw std::runtime_error("Layer parameters outside supported bounds");
    const auto &lib = library();
    bool gap = par.geometry == 1;
    const int nx = 40, ny = gap ? 16 : 16, nz = gap ? 4 : 20, stepX = 14, stepZ = 8;
    const Vec h{2., 2., gap ? 1.25 : 2.};
    m.extent = {nx * h.x, ny * h.y, nz * h.z};
    auto inside = [&](Vec p) {
        return p.x >= 0 && p.y >= 0 && p.z >= 0 && p.x <= m.extent.x && p.y <= m.extent.y &&
               p.z <= m.extent.z && (gap || p.x <= stepX * h.x || p.z <= stepZ * h.z);
    };
    std::map<std::array<int, 3>, int> grid;
    Points original;
    Faces blocks;
    auto node = [&](int x, int y, int z) {
        std::array<int, 3> k{x, y, z};
        auto [it, newNode] = grid.emplace(k, original.size());
        if (newNode)
            original.push_back({x * h.x, y * h.y, z * h.z});
        return it->second;
    };
    // All geometry planes in the two benchmarks coincide with precursor planes.
    // One shared corner vote per vertex avoids incompatible per-cell decisions.
    struct Macro {
        std::array<int, 3> origin;
        int vote;
    };
    std::vector<Macro> macros;
    std::map<std::array<int, 3>, std::array<int, 2>> votes;
    std::map<std::string, int> voteCache;
    for (int z = 0; z < nz; z += 2)
        for (int y = 0; y < ny; y += 2)
            for (int x = 0; x < nx; x += 2) {
                std::bitset<125> target;
                for (int j = 0; j < 125; j++)
                    target[j] = inside({h.x * (x + 2 * ((j % 5) + .5) / 5),
                                        h.y * (y + 2 * (((j / 5) % 5) + .5) / 5),
                                        h.z * (z + 2 * ((j / 25) + .5) / 5)});
                // Air blocks do not vote away a sharp boundary already represented exactly.
                if (target.none())
                    continue;
                auto str = target.to_string();
                if (!voteCache.count(str))
                    voteCache[str] = lib.vote(target);
                int d = voteCache[str];
                macros.push_back({{x, y, z}, d});
                for (int i = 0; i < 8; i++) {
                    Vec a = referenceNodes[i];
                    votes[{x + int(2 * a.x), y + int(2 * a.y), z + int(2 * a.z)}][(d >> i) & 1]++;
                }
            }
    for (auto &macro : macros) {
        auto [x, y, z] = macro.origin;
        int d = 0;
        for (int i = 0; i < 8; i++) {
            auto a = referenceNodes[i];
            auto v = votes.at({x + int(2 * a.x), y + int(2 * a.y), z + int(2 * a.z)});
            if (v[1] >= v[0])
                d |= 1 << i;
        }
        if (d != 255)
            throw std::runtime_error("Fixture is not aligned to its precursor");
        for (auto v : lib.cells[d]) {
            for (auto &i : v) {
                auto a = referenceNodes[i];
                i = node(x + int(2 * a.x), y + int(2 * a.y), z + int(2 * a.z));
            }
            blocks.push_back(v);
        }
    }
    m.expectedV = (gap ? nx * ny * nz : (stepX * nz + (nx - stepX) * stepZ) * ny) * h.x * h.y * h.z;
    std::map<Ids, int> sides;
    for (auto &b : blocks)
        for (auto &f : cellFaces(b))
            sides[key(f)]++;
    std::vector<Triangle> tris;
    std::vector<int> patches;
    std::vector<Ids> incident(original.size());
    std::vector<std::set<int>> graph(original.size()), volumeGraph(original.size());
    std::vector<Vec> normals(original.size());
    Ids wall;
    for (auto &b : blocks)
        for (auto f : cellFaces(b)) {
            for (int i = 0; i < 4; i++) {
                volumeGraph[f[i]].insert(f[(i + 1) % 4]);
                volumeGraph[f[(i + 1) % 4]].insert(f[i]);
            }
            if (sides.at(key(f)) != 1)
                continue;
            std::rotate(f.begin(), std::min_element(f.begin(), f.end()), f.end());
            for (int k = 1; k <= 2; k++) {
                std::array<int, 3> v{f[0], f[k], f[k + 1]};
                Vec a = original[v[0]], b0 = original[v[1]], c = original[v[2]];
                Vec n = (c - a).cross(b0 - a).unit();
                int patch = n.z > .9    ? 0
                            : n.z < -.9 ? 1
                            : n.y > .9  ? 2
                            : n.x < -.9 ? 3
                            : n.y < -.9 ? 4
                                        : 5;
                int tid = tris.size();
                tris.push_back({v, n, (a + b0 + c) / 3.});
                patches.push_back(patch);
                for (int j = 0; j < 3; j++) {
                    int u = v[j], v1 = v[(j + 1) % 3], v2 = v[(j + 2) % 3];
                    Vec e = original[v1] - original[u], f2 = original[v2] - original[u];
                    double angle = std::atan2(e.cross(f2).norm(), e.dot(f2));
                    normals[u] = normals[u] + n * angle;
                    incident[u].push_back(tid);
                    graph[u].insert(v1);
                    graph[u].insert(v2);
                }
            }
        }
    for (size_t i = 0; i < original.size(); i++)
        if (!incident[i].empty())
            wall.push_back(i);
    // Top-down mapping on analytic CAD planes. Independent support normals fix
    // three, two, or one coordinates for corners, edges, or surface vertices.
    for (int dof = 0; dof < 4; dof++)
        for (size_t v = 0; v < original.size(); v++) {
            std::map<int, double> constraints;
            for (int t : incident[v]) {
                Vec n = tris[t].n;
                int axis = std::abs(n.x) > .9 ? 0 : std::abs(n.y) > .9 ? 1 : 2;
                constraints[axis] = tris[t].c[axis];
            }
            if (3 - int(constraints.size()) != dof)
                continue;
            m.dofs[dof]++;
            auto a = original[v];
            double values[] = {a.x, a.y, a.z};
            for (auto [k, val] : constraints)
                values[k] = val;
            original[v] = {values[0], values[1], values[2]};
        }
    for (int v : wall)
        normals[v] = normals[v].unit();
    const double cos65 = std::cos(65. * std::acos(-1.) / 180.);
    auto visible = [&](int v, Vec d) {
        double value = 1;
        for (int ti : incident[v])
            value = std::min(value, d.dot(tris[ti].n));
        return value;
    };
    // A reentrant trihedral corner can give one face 270 degrees of angle
    // weight. Project that seed into the intersection of the visibility caps.
    for (int v : wall)
        for (int pass = 0; pass < 80 && visible(v, normals[v]) < cos65; pass++) {
            for (int ti : incident[v]) {
                Vec n = tris[ti].n;
                double s = normals[v].dot(n);
                if (s >= cos65)
                    continue;
                Vec tangent = normals[v] - n * s;
                if (tangent.norm() < 1e-14)
                    throw std::runtime_error("Incompatible opposing face normals");
                double c = cos65 + 1e-9;
                normals[v] = n * c + tangent.unit() * std::sqrt(1 - c * c);
            }
        }
    for (int pass = 0; pass < 5; pass++) {
        auto next = normals;
        for (int v : wall) {
            Vec sum{};
            for (int j : graph[v])
                sum = sum + normals[j];
            if (sum.norm() < 1e-12)
                continue;
            sum = sum.unit();
            for (double w = .42; w > 1e-5; w *= .5) {
                Vec d = (normals[v] * (1 - w) + sum * w).unit();
                if (visible(v, d) >= cos65) {
                    next[v] = d;
                    break;
                }
            }
        }
        normals.swap(next);
    }
    BVH tree(original, tris);
    std::vector<double> clearance(original.size()), height(original.size()), fractions{0};
    double total = 0, dh = par.first;
    for (int k = 0; k < par.layers; k++) {
        total += dh;
        fractions.push_back(total);
        dh *= par.growth;
    }
    for (auto &f : fractions)
        f /= total;
    for (int v : wall) {
        m.minVisibility = std::min(m.minVisibility, visible(v, normals[v]));
        clearance[v] = tree.clearance(v, normals[v]);
        height[v] = std::min({total, .4 * clearance[v], .85 * std::min({h.x, h.y, h.z})});
        if (height[v] < total - 1e-12)
            m.squeezed++;
    }
    if (m.minVisibility < cos65 - 1e-12)
        throw std::runtime_error("Empty visibility cone; extrusion rejected");
    std::vector<int> depth(original.size(), 100);
    for (int v : wall)
        depth[v] = 0;
    for (int n = 0; n < 3; n++)
        for (size_t i = 0; i < depth.size(); i++)
            if (depth[i] == n)
                for (int j : volumeGraph[i])
                    depth[j] = std::min(depth[j], n + 1);
    // Coarsen complete 2x2x2 siblings only in the fixed Cartesian interior.
    // There are exactly two levels, so all neighboring leaves are 2:1 balanced.
    std::vector<bool> coarse(macros.size()), fineVertex(original.size());
    auto midpoint = [&](const Ids &v) {
        Vec c = center(original, v);
        return grid.at({int(std::lround(c.x / h.x)), int(std::lround(c.y / h.y)),
                        int(std::lround(c.z / h.z))});
    };
    std::vector<Ids> macroCorners;
    for (size_t i = 0; i < macros.size(); i++) {
        auto [x, y, z] = macros[i].origin;
        Ids corners;
        bool deep = true;
        for (int c = 0; c < 8; c++) {
            auto q = referenceNodes[c];
            int v = grid.at({x + int(2 * q.x), y + int(2 * q.y), z + int(2 * q.z)});
            corners.push_back(v);
            deep &= depth[v] >= 3;
        }
        macroCorners.push_back(corners);
        coarse[i] = deep;
        if (deep)
            m.coarseParents++;
        else
            for (int j = 0; j < 8; j++)
                for (int v : blocks[8 * i + j])
                    fineVertex[v] = true;
    }
    std::vector<Cell> coarseCells;
    for (size_t i = 0; i < macros.size(); i++)
        if (coarse[i]) {
            const auto &b = macroCorners[i];
            Faces partition;
            bool split = false;
            for (const auto &f : cellFaces(b)) {
                int fc = midpoint(f);
                Ids mids, ring;
                bool cutEdge = false;
                for (int j = 0; j < 4; j++) {
                    int md = midpoint({f[j], f[(j + 1) % 4]});
                    mids.push_back(md);
                    ring.push_back(f[j]);
                    if (fineVertex[md]) {
                        ring.push_back(md);
                        cutEdge = true;
                    }
                }
                if (fineVertex[fc]) {
                    for (int j = 0; j < 4; j++)
                        partition.push_back({f[j], mids[j], fc, mids[(j + 3) % 4]});
                    split = true;
                } else if (cutEdge) {
                    for (size_t j = 0; j < ring.size(); j++)
                        partition.push_back({fc, ring[j], ring[(j + 1) % ring.size()]});
                    split = true;
                } else
                    partition.push_back(f);
            }
            if (!split) {
                coarseCells.push_back({b, 0});
                m.coarseHexes++;
            } else {
                int apex = midpoint(b);
                for (auto f : partition) {
                    std::reverse(f.begin(), f.end());
                    f.push_back(apex);
                    coarseCells.push_back({f, -1});
                }
            }
        }
    std::vector<std::vector<int>> rings;
    bool accepted = false;
    for (int attempt = 0; attempt < 10 && !accepted; attempt++) {
        m.p = original;
        m.cells.clear();
        m.boundary.clear();
        m.minPrism = 1;
        rings.assign(par.layers + 1, Ids(original.size(), -1));
        std::vector<Vec> displacement(original.size());
        for (int v : wall)
            displacement[v] = normals[v] * height[v];
        // Only the transition band moves. Hexes beyond three graph rings stay Cartesian.
        for (int pass = 0; pass < 40; pass++) {
            auto next = displacement;
            for (size_t i = 0; i < original.size(); i++)
                if (depth[i] > 0 && depth[i] < 3) {
                    Vec sum{};
                    for (int j : volumeGraph[i])
                        sum = sum + displacement[j];
                    next[i] = sum / double(volumeGraph[i].size());
                }
            displacement.swap(next);
        }
        for (size_t i = 0; i < m.p.size(); i++)
            m.p[i] = original[i] + displacement[i];
        for (int v : wall) {
            for (int k = 0; k < par.layers; k++) {
                rings[k][v] = m.p.size();
                m.p.push_back(original[v] + displacement[v] * fractions[k]);
            }
            rings.back()[v] = v;
        }
        std::set<int> bad;
        for (size_t t = 0; t < tris.size(); t++) {
            auto [a, b, c] = tris[t].v;
            for (int k = 0; k < par.layers; k++) {
                Ids v{rings[k][a],     rings[k][c],     rings[k][b],
                      rings[k + 1][a], rings[k + 1][c], rings[k + 1][b]};
                double bound = prismJacobianBound(m.p, v);
                m.minPrism = std::min(m.minPrism, bound);
                if (bound < .15 || jens(m.p, v) <= .25 || integrate(m.p, v).minStar <= 0) {
                    bad.insert(a);
                    bad.insert(b);
                    bad.insert(c);
                }
                if (k == 0)
                    m.boundary.push_back({{v[0], v[2], v[1]}, int(m.cells.size()), patches[t]});
                m.cells.push_back({v, k + 1});
            }
        }
        m.cells.insert(m.cells.end(), coarseCells.begin(), coarseCells.end());
        for (size_t bi = 0; bi < blocks.size(); bi++) {
            if (coarse[bi / 8])
                continue;
            auto &b = blocks[bi];
            auto fs = cellFaces(b);
            bool touch = false;
            for (auto &f : fs)
                touch |= sides.at(key(f)) == 1;
            if (!touch) {
                m.cells.push_back({b, 0});
                continue;
            }
            int apex = m.p.size();
            m.p.push_back(center(m.p, b));
            for (auto f : fs) {
                std::rotate(f.begin(), std::min_element(f.begin(), f.end()), f.end());
                if (sides.at(key(f)) == 1) {
                    for (int k = 1; k <= 2; k++)
                        m.cells.push_back({{f[0], f[k + 1], f[k], apex}, -1});
                } else {
                    std::reverse(f.begin(), f.end());
                    f.push_back(apex);
                    m.cells.push_back({f, -1});
                }
            }
        }
        bool coreBad = false;
        for (auto &c : m.cells) {
            c.quality = jens(m.p, c.v);
            auto r = integrate(m.p, c.v);
            c.volume = r.volume;
            c.closure = r.closure;
            if (c.layer <= 0 && (c.quality <= .25 || r.minStar <= 0))
                coreBad = true;
        }
        if (coreBad)
            bad.insert(wall.begin(), wall.end());
        if (bad.empty())
            accepted = true;
        else {
            for (int i : bad)
                height[i] *= .7;
            m.retries++;
        }
    }
    if (!accepted)
        throw std::runtime_error("Quality gate rejected extrusion after ten retractions");
    // Remove obsolete sibling vertices while preserving the canonical diagonal order.
    std::vector<bool> used(m.p.size());
    for (auto &c : m.cells)
        for (int v : c.v)
            used[v] = true;
    Ids remap(m.p.size(), -1);
    Points compact;
    for (size_t i = 0; i < m.p.size(); i++)
        if (used[i]) {
            remap[i] = compact.size();
            compact.push_back(m.p[i]);
        }
    for (auto &c : m.cells)
        for (int &v : c.v)
            v = remap[v];
    for (auto &b : m.boundary)
        for (int &v : b.v)
            v = remap[v];
    m.p.swap(compact);
    for (int v : wall)
        m.maxRatio = std::max(m.maxRatio, height[v] / clearance[v]);
    m.surfaceVertices = wall.size();
    struct Use {
        Ids face;
        int count = 0;
    };
    std::map<Ids, Use> uses;
    std::map<Ids, int> expected;
    for (auto &b : m.boundary)
        expected[key(b.v)] = b.owner;
    for (size_t i = 0; i < m.cells.size(); i++) {
        auto &c = m.cells[i];
        m.counts[c.v.size()]++;
        m.minQ = std::min(m.minQ, c.quality);
        m.minV = std::min(m.minV, c.volume);
        m.meanQ += c.quality;
        m.totalV += c.volume;
        m.maxClosure = std::max(m.maxClosure, c.closure);
        if (!(c.volume > 0) || !std::isfinite(c.quality) || !std::isfinite(c.closure))
            m.negative++;
        m.minStar = std::min(m.minStar, integrate(m.p, c.v).minStar);
        for (auto &f : cellFaces(c.v)) {
            auto &u = uses[key(f)];
            if (u.count == 0)
                u.face = f;
            else if (!reversedCycle(f, u.face))
                m.nonmanifold++;
            u.count++;
        }
    }
    m.faceCount = uses.size();
    for (auto &[k, u] : uses) {
        if (u.count == 1 && !expected.count(k))
            m.unmatched++;
        if (u.count > 2)
            m.nonmanifold++;
        if (expected.count(k) && u.count != 1)
            m.unmatched++;
    }
    for (auto &[k, id] : expected)
        if (!uses.count(k))
            m.unmatched++;
    // Independently detect vertices strictly inside unsplit geometric edges.
    // Candidate points are localized by spatial bins; this also catches hanging
    // edges that contain the midpoint of a finer neighboring edge chain.
    std::set<std::pair<int, int>> edges;
    for (auto &[k, u] : uses)
        for (size_t j = 0; j < u.face.size(); j++)
            edges.insert(std::minmax(u.face[j], u.face[(j + 1) % u.face.size()]));
    const double binSize = 2., eps = 1e-10;
    std::map<std::array<int, 3>, Ids> bins;
    for (size_t j = 0; j < m.p.size(); j++) {
        auto q = m.p[j];
        bins[{int(std::floor(q.x / binSize)), int(std::floor(q.y / binSize)),
              int(std::floor(q.z / binSize))}]
            .push_back(j);
    }
    for (auto [a, b] : edges) {
        Vec p = m.p[a], q = m.p[b], d = q - p;
        double l2 = d.dot(d);
        if (l2 < eps * eps)
            throw std::runtime_error("Collapsed geometric edge");
        int lo[3], hi[3];
        for (int k = 0; k < 3; k++) {
            lo[k] = std::floor((std::min(p[k], q[k]) - eps) / binSize);
            hi[k] = std::floor((std::max(p[k], q[k]) + eps) / binSize);
        }
        for (int i = lo[0]; i <= hi[0]; i++)
            for (int j = lo[1]; j <= hi[1]; j++)
                for (int k = lo[2]; k <= hi[2]; k++) {
                    auto it = bins.find({i, j, k});
                    if (it == bins.end())
                        continue;
                    for (int v : it->second) {
                        if (v == a || v == b)
                            continue;
                        Vec u = m.p[v] - p;
                        double t = u.dot(d) / l2;
                        if (t > 0 && t < 1) {
                            Vec r = u - d * t;
                            if (r.dot(r) < eps * eps)
                                m.hangingNodes++;
                        }
                    }
                }
    }
    FaMesh fa(m.p, m.boundary);
    m.meanQ /= m.cells.size();
    if (m.negative || m.unmatched || m.nonmanifold || m.hangingNodes || m.maxRatio > .4 ||
        m.minPrism < .15 || m.minQ <= .25 || !(m.maxClosure < 1e-14) || m.minStar <= 0 ||
        fa.boundaryEdges || !fa.uniqueOwners || std::abs(m.totalV / m.expectedV - 1) > 1e-10)
        throw std::runtime_error(
            "Mesh rejected: volume, Jacobian, closure, or topology test failed");
    uint32_t hash = 2166136261u;
    for (auto &p : m.p)
        for (int j = 0; j < 3; j++)
            hash = (hash ^ uint32_t(std::llround(p[j] * 1e6))) * 16777619u;
    std::ostringstream ss;
    ss << std::hex << std::setw(8) << std::setfill('0') << hash;
    m.id = ss.str();
    m.elapsed =
        std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
    return m;
}

template <class T> static void arrayJSON(std::ostream &o, const T &a) {
    o << '[';
    bool first = true;
    for (const auto &v : a) {
        if (!first)
            o << ',';
        first = false;
        o << v;
    }
    o << ']';
}
static void vecJSON(std::ostream &o, Vec v) { o << '[' << v.x << ',' << v.y << ',' << v.z << ']'; }
static void writeMesh(std::ostream &o, const Result &m) {
    o << std::setprecision(17) << "{\"version\":\"1.0.0\",\"id\":\"" << m.id
      << "\",\"geometry\":" << m.par.geometry << ",\"extent\":";
    vecJSON(o, m.extent);
    o << ",\"params\":{\"layers\":" << m.par.layers << ",\"first\":" << m.par.first
      << ",\"growth\":" << m.par.growth << "},\"stats\":{\"hangingNodes\":" << m.hangingNodes
      << ",\"coarseParents\":" << m.coarseParents << ",\"coarseHexes\":" << m.coarseHexes << ",";
    o << "\"cells\":" << m.cells.size() << ",\"vertices\":" << m.p.size()
      << ",\"faces\":" << m.faceCount << ",\"wallFaces\":" << m.boundary.size()
      << ",\"minJ\":" << m.minQ << ",\"meanJ\":" << m.meanQ << ",\"minVolume\":" << m.minV
      << ",\"maxClosure\":" << m.maxClosure;
    o << ",\"negative\":" << m.negative << ",\"unmatched\":" << m.unmatched
      << ",\"nonmanifold\":" << m.nonmanifold << ",\"minVisibility\":" << m.minVisibility
      << ",\"minPrism\":" << m.minPrism << ",\"minStar\":" << m.minStar
      << ",\"clearanceRatio\":" << m.maxRatio << ",\"volume\":" << m.totalV
      << ",\"expectedVolume\":" << m.expectedV << ",\"elapsedMs\":" << m.elapsed
      << ",\"squeezed\":" << m.squeezed << ",\"retries\":" << m.retries << ",\"counts\":";
    arrayJSON(o, m.counts);
    o << ",\"dofs\":";
    arrayJSON(o, m.dofs);
    o << "},\"points\":[";
    for (size_t i = 0; i < m.p.size(); i++) {
        if (i)
            o << ',';
        vecJSON(o, m.p[i]);
    }
    o << "],\"cells\":[";
    for (size_t i = 0; i < m.cells.size(); i++) {
        if (i)
            o << ',';
        arrayJSON(o, m.cells[i].v);
    }
    o << "],\"quality\":[";
    for (size_t i = 0; i < m.cells.size(); i++) {
        if (i)
            o << ',';
        o << m.cells[i].quality;
    }
    o << "],\"volumes\":[";
    for (size_t i = 0; i < m.cells.size(); i++) {
        if (i)
            o << ',';
        o << m.cells[i].volume;
    }
    o << "],\"layers\":[";
    for (size_t i = 0; i < m.cells.size(); i++) {
        if (i)
            o << ',';
        o << m.cells[i].layer;
    }
    o << "],\"boundary\":[";
    for (size_t i = 0; i < m.boundary.size(); i++) {
        auto &b = m.boundary[i];
        if (i)
            o << ',';
        o << '[' << b.owner << ',' << b.patch;
        for (int v : b.v)
            o << ',' << v;
        o << ']';
    }
    o << "]}";
}
static void writeFA(std::ostream &o, const Result &m, int patch) {
    if (patch < -1 || patch > 5)
        throw std::runtime_error("Unknown wall patch");
    FaMesh fa(m.p, m.boundary, patch);
    o << std::setprecision(17) << "{\"mesh\":\"" << m.id << "\",\"patch\":\""
      << (patch < 0 ? "all" : patchNames[patch])
      << "\",\"units\":\"mm\",\"boundaryEdges\":" << fa.boundaryEdges
      << ",\"uniqueOwners\":" << (fa.uniqueOwners ? "true" : "false") << ",\"faces\":[";
    for (size_t i = 0; i < fa.faces.size(); i++) {
        auto &f = fa.faces[i];
        if (i)
            o << ',';
        o << "{\"face_to_volume_owner_cell\":" << f.owner << ",\"vertices\":";
        arrayJSON(o, f.v);
        o << ",\"points\":[";
        for (size_t k = 0; k < f.v.size(); k++) {
            if (k)
                o << ',';
            vecJSON(o, m.p[f.v[k]]);
        }
        o << "],\"patch\":" << f.patch << ",\"area\":" << f.area << ",\"normal\":";
        vecJSON(o, f.n);
        o << ",\"curvature\":";
        arrayJSON(o, f.curvature);
        o << ",\"edges\":[";
        for (size_t j = 0; j < f.edges.size(); j++) {
            if (j)
                o << ',';
            auto &e = f.edges[j];
            o << "{\"a\":" << e.a << ",\"b\":" << e.b << ",\"neighbor\":" << e.neighbor
              << ",\"Le\":";
            vecJSON(o, e.le);
            o << ",\"geodesic\":" << e.distance << '}';
        }
        o << "]}";
    }
    o << "]}";
}
static void selfTest() {
    const auto &l = library();
    if (canonical.size() != 23 || l.vote(std::bitset<125>{}) != 0 ||
        l.vote(std::bitset<125>{}.set()) != 255)
        throw std::runtime_error("Template voting regression");
    for (int mask = 0; mask < 256; mask++)
        for (auto &c : l.cells[mask]) {
            auto r = integrate(referenceNodes, c);
            if (!(r.volume > 0 && r.closure < 1e-14 && jens(referenceNodes, c) > 0))
                throw std::runtime_error("Invalid transformed template");
        }
    Points cube(referenceNodes.begin(), referenceNodes.begin() + 8);
    if (std::abs(jens(cube, {0, 1, 2, 3, 4, 5, 6, 7}) - 1) > 1e-12)
        throw std::runtime_error("Unit hex regression");
    Points folded = {{0, 0, 0}, {1, 1, 0}, {0, 1, 0}, {1, 0, 0}, {.5, .5, 1}};
    if (jens(folded, {0, 1, 2, 3, 4}) > 0)
        throw std::runtime_error("Folded pyramid accepted");
    Points concave = {{0, 0, 0}, {1, 0, 0}, {.2, .2, 0}, {0, 1, 0}, {.5, .5, 1}};
    if (jens(concave, {0, 1, 2, 3, 4}) > 0)
        throw std::runtime_error("Concave pyramid accepted");
    if (integrate(cube, {0, 2, 1, 4}).volume >= 0)
        throw std::runtime_error("Signed volume regression");
    Points idealTet = {{0, 0, 0},
                       {1, 0, 0},
                       {.5, std::sqrt(3.) / 2, 0},
                       {.5, std::sqrt(3.) / 6, std::sqrt(2. / 3)}};
    Points idealPyr = {{0, 0, 0}, {1, 0, 0}, {1, 1, 0}, {0, 1, 0}, {.5, .5, std::sqrt(.5)}};
    Points idealPri = {{0, 0, 0}, {1, 0, 0}, {.5, std::sqrt(3.) / 2, 0},
                       {0, 0, 1}, {1, 0, 1}, {.5, std::sqrt(3.) / 2, 1}};
    if (std::abs(jens(idealTet, {0, 1, 2, 3}) - 1) > 1e-12 ||
        std::abs(jens(idealPyr, {0, 1, 2, 3, 4}) - 1) > 1e-12 ||
        std::abs(jens(idealPri, {0, 1, 2, 3, 4, 5}) - 1) > 1e-12)
        throw std::runtime_error("Ideal mixed-element normalization regression");
    std::cout << "PASS: 23 canonical templates, 256 masks, signed volumes, folded and concave "
                 "pyramid bases.\n";
    for (int g = 0; g < 2; g++) {
        auto m = generate({g, 6, .18, 1.22});
        std::cout << "PASS " << (g ? "twin-plate" : "stepped-L") << ": cells=" << m.cells.size()
                  << " min_V=" << m.minV << " min_JENS=" << m.minQ
                  << " max_closure=" << m.maxClosure << " negative=" << m.negative
                  << " unmatched=" << m.unmatched << " min_wedge_bound=" << m.minPrism << '\n';
    }
}

#ifdef __EMSCRIPTEN__
#include <emscripten.h>
static std::unique_ptr<Result> active;
static std::string response;
extern "C" EMSCRIPTEN_KEEPALIVE const char *build_mesh(int geometry, int layers, double first,
                                                       double growth) {
    try {
        auto out = std::make_unique<Result>(generate({geometry, layers, first, growth}));
        std::ostringstream s;
        writeMesh(s, *out);
        active = std::move(out);
        response = s.str();
    } catch (const std::exception &e) {
        response = std::string("{\"error\":\"") + e.what() + "\"}";
    }
    return response.c_str();
}
extern "C" EMSCRIPTEN_KEEPALIVE const char *extract_surface(int patch) {
    try {
        if (!active)
            throw std::runtime_error("Build the volume mesh first");
        std::ostringstream s;
        writeFA(s, *active, patch);
        response = s.str();
    } catch (const std::exception &e) {
        response = std::string("{\"error\":\"") + e.what() + "\"}";
    }
    return response.c_str();
}
#else
int main(int argc, char **argv) {
    try {
        Params p;
        std::string output = "mesh.json", fa;
        int patch = -1;
        for (int i = 1; i < argc; i++) {
            std::string arg = argv[i];
            if (arg == "--self-test") {
                selfTest();
                return 0;
            }
            if (arg == "--help") {
                std::cout << "hexalab [--geometry l-channel|twin-plate] [--layers 1..12] "
                             "[--first-height mm] [--growth 1..1.6] [--out mesh.json] [--fa-out "
                             "fa.json] [--patch -1..5] [--self-test]\n";
                return 0;
            }
            if (++i >= argc)
                throw std::runtime_error("Missing option value");
            std::string val = argv[i];
            if (arg == "--geometry") {
                if (val != "l-channel" && val != "twin-plate")
                    throw std::runtime_error("Unknown geometry");
                p.geometry = val == "twin-plate";
            } else if (arg == "--layers")
                p.layers = std::stoi(val);
            else if (arg == "--first-height")
                p.first = std::stod(val);
            else if (arg == "--growth")
                p.growth = std::stod(val);
            else if (arg == "--out")
                output = val;
            else if (arg == "--fa-out")
                fa = val;
            else if (arg == "--patch")
                patch = std::stoi(val);
            else
                throw std::runtime_error("Unknown option: " + arg);
        }
        auto m = generate(p);
        std::ofstream out(output);
        if (!out)
            throw std::runtime_error("Cannot open mesh output");
        writeMesh(out, m);
        if (!fa.empty()) {
            std::ofstream f(fa);
            if (!f)
                throw std::runtime_error("Cannot open faMesh output");
            writeFA(f, m, patch);
        }
        std::cout << "Verified " << m.cells.size() << " cells; JENS >= " << m.minQ
                  << ", closure <= " << m.maxClosure << ", wrote " << output << '\n';
        return 0;
    } catch (const std::exception &e) {
        std::cerr << "REJECTED: " << e.what() << '\n';
        return 1;
    }
}
#endif