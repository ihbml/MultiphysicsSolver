#pragma once
#include "geometry.hpp"

struct FaEdge {
    int a, b, neighbor = -1;
    Vec le;
    double distance = 0;
};
struct FaFace {
    int owner, patch;
    Ids v;
    Vec n, c;
    double area;
    std::vector<FaEdge> edges;
    std::array<double, 9> curvature{};
};
struct FaMesh {
    std::vector<FaFace> faces;
    int boundaryEdges = 0;
    bool uniqueOwners = true;
    FaMesh(const Points &p, const std::vector<Boundary> &boundary, int selected = -1) {
        std::map<std::pair<int, int>, std::vector<std::pair<int, int>>> incidence;
        std::set<int> owners;
        for (const auto &b : boundary) {
            if (selected >= 0 && b.patch != selected)
                continue;
            Vec a = areaVector(p, b.v);
            FaFace f{b.owner, b.patch, b.v, a.unit(), center(p, b.v), a.norm(), {}, {}};
            uniqueOwners &= owners.insert(b.owner).second;
            for (size_t i = 0; i < b.v.size(); i++) {
                int u = b.v[i], v = b.v[(i + 1) % b.v.size()];
                Vec e = p[v] - p[u];
                f.edges.push_back({u, v, -1, e.cross(f.n), 0});
                incidence[std::minmax(u, v)].push_back({faces.size(), i});
            }
            faces.push_back(f);
        }
        for (auto &[edge, uses] : incidence) {
            if (uses.size() > 2)
                throw std::runtime_error("Nonmanifold finite-area edge");
            if (uses.size() == 1) {
                boundaryEdges++;
                continue;
            }
            for (int s = 0; s < 2; s++) {
                auto [i, e] = uses[s];
                int j = uses[1 - s].first;
                auto &f = faces[i];
                auto &g = faces[j];
                auto &ed = f.edges[e];
                if (ed.a == g.edges[uses[1 - s].second].a)
                    throw std::runtime_error("Inconsistent faMesh orientation");
                ed.neighbor = j;
                Vec v = p[ed.b] - p[ed.a], t = v.unit();
                // Unfold both triangles around their common edge. This is the exact
                // local geodesic when the crossing lies on the finite edge.
                Vec a = f.c - p[ed.a], b = g.c - p[ed.a];
                double u = a.dot(t), w = b.dot(t);
                double h = (a - t * u).norm(), k = (b - t * w).norm(),
                       crossing = (u * k + w * h) / (h + k);
                ed.distance = crossing >= 0 && crossing <= v.norm()
                                  ? std::hypot(u - w, h + k)
                                  : std::min(a.norm() + b.norm(),
                                             (f.c - p[ed.b]).norm() + (g.c - p[ed.b]).norm());
                double theta = std::atan2(t.dot(f.n.cross(g.n)), f.n.dot(g.n));
                Vec m = ed.le.unit();
                for (int r = 0; r < 3; r++)
                    for (int c = 0; c < 3; c++)
                        f.curvature[3 * r + c] += .5 * v.norm() * theta * m[r] * m[c] / f.area;
            }
        }
        // Edge counts alone miss two disconnected fans meeting at a single node.
        std::map<int, Ids> stars;
        for (size_t i = 0; i < faces.size(); i++)
            for (int v : faces[i].v)
                stars[v].push_back(i);
        for (auto &[v, incident] : stars) {
            std::set<int> seen;
            Ids todo{incident.front()};
            while (!todo.empty()) {
                int f = todo.back();
                todo.pop_back();
                if (!seen.insert(f).second)
                    continue;
                for (auto &e : faces[f].edges)
                    if ((e.a == v || e.b == v) && e.neighbor >= 0)
                        todo.push_back(e.neighbor);
            }
            if (seen.size() != incident.size())
                throw std::runtime_error("Disconnected finite-area vertex fan");
        }
    }
    // A view into the owning volume field; no copied coupling buffer.
    template <class T> T &ownerValue(std::vector<T> &field, size_t f) const {
        return field.at(faces.at(f).owner);
    }
};