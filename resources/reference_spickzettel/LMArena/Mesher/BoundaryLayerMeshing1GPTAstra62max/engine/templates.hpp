#pragma once
#include "geometry.hpp"
#include "superelements.hpp"

struct SuperElements {
    std::array<Faces, 256> cells;
    std::array<std::bitset<125>, 256> occupancy;
    SuperElements() {
        std::array<bool, 256> seen{};
        for (const auto &prototype : canonical) {
            std::array<int, 3> perm{0, 1, 2};
            do {
                for (int flips = 0; flips < 8; flips++) {
                    Ids ids;
                    for (auto p : referenceNodes) {
                        Vec q{p[perm[0]], p[perm[1]], p[perm[2]]};
                        if (flips & 1)
                            q.x = 1 - q.x;
                        if (flips & 2)
                            q.y = 1 - q.y;
                        if (flips & 4)
                            q.z = 1 - q.z;
                        auto it = std::find_if(referenceNodes.begin(), referenceNodes.end(),
                                               [&](Vec r) { return (q - r).norm() < 1e-12; });
                        if (it == referenceNodes.end())
                            throw std::runtime_error("Invalid template symmetry");
                        ids.push_back(it - referenceNodes.begin());
                    }
                    int mask = 0;
                    Faces placed = prototype;
                    for (auto &v : placed)
                        for (int &i : v) {
                            i = ids[i];
                            if (i < 8)
                                mask |= 1 << i;
                        }
                    if (seen[mask])
                        continue;
                    seen[mask] = true;
                    for (auto &v : placed) {
                        orient(referenceNodes, v);
                        if (jens(referenceNodes, v) <= 0)
                            throw std::runtime_error("Invalid reference element");
                    }
                    cells[mask] = placed;
                }
            } while (std::next_permutation(perm.begin(), perm.end()));
        }
        for (int d = 0; d < 256; d++) {
            if (!seen[d])
                throw std::runtime_error("Incomplete mask library");
            std::vector<std::vector<std::pair<Vec, Vec>>> planes;
            for (auto &v : cells[d]) {
                std::vector<std::pair<Vec, Vec>> ps;
                for (auto &f : cellFaces(v))
                    ps.push_back({referenceNodes[f[0]], areaVector(referenceNodes, f)});
                planes.push_back(ps);
            }
            for (int k = 0; k < 125; k++) {
                Vec q{((k % 5) + .5) / 5, (((k / 5) % 5) + .5) / 5, ((k / 25) + .5) / 5};
                for (auto &ps : planes) {
                    bool inside = true;
                    for (auto &[a, n] : ps)
                        if ((q - a).dot(n) > 1e-12) {
                            inside = false;
                            break;
                        }
                    if (inside) {
                        occupancy[d].set(k);
                        break;
                    }
                }
            }
        }
    }
    int vote(std::bitset<125> target) const {
        int best = INT32_MAX, choice = 0;
        for (int d = 0; d < 256; d++) {
            int missing = (target & ~occupancy[d]).count(),
                excess = (occupancy[d] & ~target).count();
            int cost = missing * missing + excess;
            if (cost < best) {
                best = cost;
                choice = d;
            }
        }
        return choice;
    }
};