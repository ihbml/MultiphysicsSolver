#include "rcm.hpp"
#include <queue>
#include <algorithm>
#include <cmath>

namespace cfd {

std::vector<int64_t> RCMReordering::computePermutation(const NativeMeshData& mesh) {
    int64_t n_cells = mesh.n_cells();
    if (n_cells <= 1) {
        return {0};
    }

    // 1. Build adjacency graph from internal faces
    std::vector<std::vector<int64_t>> adj(n_cells);
    int64_t n_faces = mesh.n_faces();
    for (int64_t f = 0; f < n_faces; ++f) {
        int64_t o = mesh.owner[f];
        int64_t nb = mesh.neighbour[f];
        if (nb >= 0) {
            adj[o].push_back(nb);
            adj[nb].push_back(o);
        }
    }

    for (int64_t c = 0; c < n_cells; ++c) {
        std::sort(adj[c].begin(), adj[c].end());
        adj[c].erase(std::unique(adj[c].begin(), adj[c].end()), adj[c].end());
    }

    std::vector<int64_t> degrees(n_cells);
    for (int64_t c = 0; c < n_cells; ++c) {
        degrees[c] = static_cast<int64_t>(adj[c].size());
    }

    // Sort neighbors by ascending degree
    for (int64_t c = 0; c < n_cells; ++c) {
        std::sort(adj[c].begin(), adj[c].end(), [&](int64_t a, int64_t b) {
            if (degrees[a] != degrees[b]) return degrees[a] < degrees[b];
            return a < b;
        });
    }

    std::vector<bool> visited(n_cells, false);
    std::vector<int64_t> cm_order;
    cm_order.reserve(n_cells);

    // Handle all components
    while (static_cast<int64_t>(cm_order.size()) < n_cells) {
        // Find unvisited node with minimum degree
        int64_t start_node = -1;
        int64_t min_deg = std::numeric_limits<int64_t>::max();
        for (int64_t c = 0; c < n_cells; ++c) {
            if (!visited[c] && degrees[c] < min_deg) {
                min_deg = degrees[c];
                start_node = c;
            }
        }
        if (start_node < 0) break;

        std::queue<int64_t> q;
        q.push(start_node);
        visited[start_node] = true;

        while (!q.empty()) {
            int64_t u = q.front();
            q.pop();
            cm_order.push_back(u);

            for (auto v : adj[u]) {
                if (!visited[v]) {
                    visited[v] = true;
                    q.push(v);
                }
            }
        }
    }

    // 2. Reverse Cuthill-McKee
    std::vector<int64_t> rcm_order(n_cells);
    for (int64_t i = 0; i < n_cells; ++i) {
        rcm_order[i] = cm_order[n_cells - 1 - i];
    }

    return rcm_order;
}

int64_t RCMReordering::computeBandwidth(const NativeMeshData& mesh) {
    int64_t n_faces = mesh.n_faces();
    int64_t max_diff = 0;
    for (int64_t f = 0; f < n_faces; ++f) {
        int64_t o = mesh.owner[f];
        int64_t nb = mesh.neighbour[f];
        if (nb >= 0) {
            max_diff = std::max(max_diff, std::abs(o - nb));
        }
    }
    return max_diff;
}

void RCMReordering::applyReordering(NativeMeshData& mesh, const std::vector<int64_t>& perm) {
    int64_t n_cells = mesh.n_cells();
    if (n_cells <= 1 || perm.size() != static_cast<size_t>(n_cells)) return;

    // Unpack nodes and cells
    int64_t n_nodes = mesh.n_points();
    std::vector<Vec3> node_coords(n_nodes);
    for (int64_t i = 0; i < n_nodes; ++i) {
        node_coords[i] = Vec3(mesh.points[3 * i], mesh.points[3 * i + 1], mesh.points[3 * i + 2]);
    }

    std::vector<std::vector<int64_t>> old_cells(n_cells);
    for (int64_t c = 0; c < n_cells; ++c) {
        int64_t off0 = mesh.cell_offsets[c];
        int64_t off1 = mesh.cell_offsets[c + 1];
        for (int64_t k = off0; k < off1; ++k) {
            old_cells[c].push_back(mesh.cell_nodes[k]);
        }
    }

    std::vector<int32_t> old_regions = mesh.cell_regions;

    // perm[new_idx] = old_idx
    std::vector<std::vector<int64_t>> new_cells(n_cells);
    std::vector<int32_t> new_regions(n_cells, 0);

    for (int64_t new_idx = 0; new_idx < n_cells; ++new_idx) {
        int64_t old_idx = perm[new_idx];
        new_cells[new_idx] = old_cells[old_idx];
        if (!old_regions.empty()) {
            new_regions[new_idx] = old_regions[old_idx];
        }
    }

    // Rebuild topology with reordered cells
    DiFVMTraversal::buildTopology(mesh, node_coords, new_cells, new_regions);
}

} // namespace cfd
