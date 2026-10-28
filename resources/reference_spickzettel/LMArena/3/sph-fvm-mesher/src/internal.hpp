#pragma once

#include "hybridmesh/hybridmesh.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <functional>
#include <limits>
#include <numeric>
#include <queue>
#include <stdexcept>
#include <unordered_map>
#include <utility>

namespace hybridmesh::detail {

inline Vec3 cross(Vec3 a, Vec3 b) {
    return {a.y*b.z-a.z*b.y, a.z*b.x-a.x*b.z, a.x*b.y-a.y*b.x};
}
inline double norm2(Vec3 a) { return dot(a, a); }
inline Vec3 minimum(Vec3 a, Vec3 b) {
    return {std::min(a.x,b.x), std::min(a.y,b.y), std::min(a.z,b.z)};
}
inline Vec3 maximum(Vec3 a, Vec3 b) {
    return {std::max(a.x,b.x), std::max(a.y,b.y), std::max(a.z,b.z)};
}
inline double clamp(double x, double a, double b) { return std::max(a, std::min(x,b)); }
inline bool finite(Vec3 p) { return std::isfinite(p.x) && std::isfinite(p.y) && std::isfinite(p.z); }
inline Index checked_index(std::size_t n) {
    if (n >= no_index) throw std::runtime_error("too many entities for 32-bit indices");
    return static_cast<Index>(n);
}
inline std::size_t checked_product(std::initializer_list<std::size_t> values) {
    std::size_t result = 1;
    for (std::size_t x : values) {
        if (x != 0 && result > std::numeric_limits<std::size_t>::max()/x)
            throw std::runtime_error("mesh dimensions overflow");
        result *= x;
    }
    return result;
}

using FaceKey = std::vector<Index>;
inline FaceKey face_key(std::vector<Index> nodes) {
    std::sort(nodes.begin(), nodes.end());
    if (std::adjacent_find(nodes.begin(), nodes.end()) != nodes.end())
        throw std::runtime_error("repeated vertex within a face");
    return nodes;
}
struct FaceKeyHash {
    std::size_t operator()(const FaceKey& v) const {
        std::size_t h = 1469598103934665603ull;
        for (Index i : v) h = (h ^ std::hash<Index>{}(i)) * 1099511628211ull;
        return h;
    }
};

struct RawCell { std::vector<Index> vertices; std::uint8_t vtk_type; };
struct RawFace { std::vector<Index> vertices; Index owner, neighbor, patch; };
using BoundaryTags = std::unordered_map<FaceKey, Index, FaceKeyHash>;
Mesh assemble_elements(int dimension, std::vector<Vec3> nodes,
                       const std::vector<RawCell>& cells, BoundaryTags tags,
                       std::vector<Patch> patches);
Mesh assemble_faces(std::vector<Vec3> nodes, std::vector<RawFace> faces,
                    std::vector<Patch> patches);

// Row-major dense matrix of dimension 1..4. Gaussian elimination with pivoting;
// returns false for rank-deficient, badly scaled, or non-finite moments.
inline bool inverse(const std::array<double,16>& m, int n, std::array<double,16>& out) {
    std::array<double,32> aug{};
    double scale = 0;
    for (int i=0; i<n; ++i) for (int j=0; j<n; ++j) {
        aug[static_cast<std::size_t>(i*8+j)] = m[static_cast<std::size_t>(i*4+j)];
        scale = std::max(scale, std::abs(m[static_cast<std::size_t>(i*4+j)]));
    }
    if (!(scale > 0) || !std::isfinite(scale)) return false;
    for (int i=0; i<n; ++i) aug[static_cast<std::size_t>(i*8+4+i)] = 1;
    for (int c=0; c<n; ++c) {
        int pivot = c;
        for (int r=c+1; r<n; ++r)
            if (std::abs(aug[static_cast<std::size_t>(r*8+c)]) >
                std::abs(aug[static_cast<std::size_t>(pivot*8+c)])) pivot=r;
        if (std::abs(aug[static_cast<std::size_t>(pivot*8+c)]) <= 1e-11*scale) return false;
        if (pivot != c) for (int j=0; j<8; ++j)
            std::swap(aug[static_cast<std::size_t>(pivot*8+j)],aug[static_cast<std::size_t>(c*8+j)]);
        const double diag = aug[static_cast<std::size_t>(c*8+c)];
        for (int j=0; j<8; ++j) aug[static_cast<std::size_t>(c*8+j)] /= diag;
        for (int r=0; r<n; ++r) if (r != c) {
            const double f = aug[static_cast<std::size_t>(r*8+c)];
            for (int j=0; j<8; ++j) aug[static_cast<std::size_t>(r*8+j)] -= f*aug[static_cast<std::size_t>(c*8+j)];
        }
    }
    out.fill(0);
    for (int i=0; i<n; ++i) for (int j=0; j<n; ++j) {
        const double v = aug[static_cast<std::size_t>(i*8+4+j)];
        if (!std::isfinite(v)) return false;
        out[static_cast<std::size_t>(i*4+j)] = v;
    }
    return true;
}

// KD tree over a provided subset of points; returned IDs are original point IDs.
class KDTree {
public:
    KDTree(const std::vector<Vec3>& points, std::vector<Index> ids, int dimension)
        : points_(points), ids_(std::move(ids)), dimension_(dimension) {
        nodes_.reserve(ids_.size());
        root_ = build(0, ids_.size(), 0);
    }
    std::vector<Index> nearest(Vec3 query, std::size_t k) const {
        if (k == 0) return {};
        using Candidate = std::pair<double,Index>; // max heap: furthest distance on top
        std::priority_queue<Candidate> best;
        visit(root_, query, k, best);
        std::vector<Candidate> sorted;
        sorted.reserve(best.size());
        while (!best.empty()) { sorted.push_back(best.top()); best.pop(); }
        std::sort(sorted.begin(),sorted.end());
        std::vector<Index> result;
        result.reserve(sorted.size());
        for (const auto& [distance,id] : sorted) { (void)distance; result.push_back(id); }
        return result;
    }
private:
    struct Node { Index id; int left=-1, right=-1, axis=0; };
    const std::vector<Vec3>& points_;
    std::vector<Index> ids_;
    std::vector<Node> nodes_;
    int dimension_, root_ = -1;
    int build(std::size_t begin, std::size_t end, int depth) {
        if (begin == end) return -1;
        const int axis=depth%dimension_;
        const std::size_t mid=(begin+end)/2;
        std::nth_element(ids_.begin()+static_cast<std::ptrdiff_t>(begin),
                         ids_.begin()+static_cast<std::ptrdiff_t>(mid),
                         ids_.begin()+static_cast<std::ptrdiff_t>(end),
                         [&](Index a, Index b) { return points_[a][static_cast<std::size_t>(axis)] <
                                                      points_[b][static_cast<std::size_t>(axis)]; });
        const int node = static_cast<int>(nodes_.size());
        nodes_.push_back({ids_[mid],-1,-1,axis});
        const int left=build(begin,mid,depth+1);
        const int right=build(mid+1,end,depth+1);
        nodes_[static_cast<std::size_t>(node)].left=left;
        nodes_[static_cast<std::size_t>(node)].right=right;
        return node;
    }
    void visit(int node, Vec3 p, std::size_t k,
               std::priority_queue<std::pair<double,Index>>& best) const {
        if (node < 0) return;
        const auto& n=nodes_[static_cast<std::size_t>(node)];
        const Vec3 q=points_[n.id];
        const double d2=norm2(p-q);
        if (best.size() < k) best.emplace(d2,n.id);
        else if (d2 < best.top().first || (d2 == best.top().first && n.id < best.top().second)) {
            best.pop(); best.emplace(d2,n.id);
        }
        const double delta=p[static_cast<std::size_t>(n.axis)] - q[static_cast<std::size_t>(n.axis)];
        visit(delta < 0 ? n.left : n.right,p,k,best);
        if (best.size() < k || delta*delta <= best.top().first)
            visit(delta < 0 ? n.right : n.left,p,k,best);
    }
};

} // namespace hybridmesh::detail
