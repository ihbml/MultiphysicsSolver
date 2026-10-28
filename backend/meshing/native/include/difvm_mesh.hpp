#pragma once

#include <vector>
#include <array>
#include <cmath>
#include <string>
#include <algorithm>
#include <cstdint>
#include <limits>
#include "cfd_mesher_native.h"

namespace cfd {

struct Vec3 {
    double x = 0.0, y = 0.0, z = 0.0;
    
    constexpr Vec3() = default;
    constexpr Vec3(double a, double b, double c = 0.0) : x(a), y(b), z(c) {}
    
    Vec3& operator+=(const Vec3& o) { x += o.x; y += o.y; z += o.z; return *this; }
    Vec3& operator-=(const Vec3& o) { x -= o.x; y -= o.y; z -= o.z; return *this; }
    Vec3& operator*=(double s) { x *= s; y *= s; z *= s; return *this; }
    Vec3& operator/=(double s) { double inv = 1.0 / s; x *= inv; y *= inv; z *= inv; return *this; }
    
    double norm2() const { return x * x + y * y + z * z; }
    double norm() const { return std::sqrt(norm2()); }
    
    Vec3 normalized() const {
        double n = norm();
        return n > 1e-15 ? Vec3(x / n, y / n, z / n) : Vec3(0, 0, 0);
    }
};

inline Vec3 operator+(Vec3 a, const Vec3& b) { return a += b; }
inline Vec3 operator-(Vec3 a, const Vec3& b) { return a -= b; }
inline Vec3 operator*(Vec3 a, double s) { return a *= s; }
inline Vec3 operator*(double s, Vec3 a) { return a *= s; }
inline Vec3 operator/(Vec3 a, double s) { return a /= s; }

inline double dot(const Vec3& a, const Vec3& b) {
    return a.x * b.x + a.y * b.y + a.z * b.z;
}

inline Vec3 cross(const Vec3& a, const Vec3& b) {
    return Vec3(
        a.y * b.z - a.z * b.y,
        a.z * b.x - a.x * b.z,
        a.x * b.y - a.y * b.x
    );
}

// Native Mesh Storage Class implementing CfdNativeMesh
struct NativeMeshData {
    std::vector<double> points;         // 3 * n_points
    std::vector<int64_t> owner;         // n_faces
    std::vector<int64_t> neighbour;     // n_faces (-1 for boundary)
    std::vector<double> face_areas;     // 3 * n_faces (S_f)
    std::vector<double> face_centres;   // 3 * n_faces (c_f)
    std::vector<double> cell_centres;   // 3 * n_cells (C_i)
    std::vector<double> cell_volumes;   // n_cells (V_i > 0)
    std::vector<double> delta_f;        // 3 * n_faces
    std::vector<double> k_f;            // 3 * n_faces
    
    // Cell connectivity CSR
    std::vector<int64_t> cell_offsets;  // n_cells + 1
    std::vector<int64_t> cell_nodes;    // flattened node indices
    
    // Quality metrics & Error Map
    std::vector<double> non_ortho;      // n_faces (degrees)
    std::vector<double> skewness;       // n_faces
    std::vector<double> aspect_ratio;   // n_cells
    std::vector<int32_t> stab_mask;     // n_faces (0, 1, 2, 3)
    
    // CHT & Multi-region
    std::vector<int32_t> cell_regions;  // n_cells
    std::vector<BoundaryPatchC> patches;

    int64_t n_points() const { return static_cast<int64_t>(points.size() / 3); }
    int64_t n_cells() const { return static_cast<int64_t>(cell_volumes.size()); }
    int64_t n_faces() const { return static_cast<int64_t>(owner.size()); }
    int64_t n_internal_faces() const {
        int64_t count = 0;
        for (auto nb : neighbour) {
            if (nb >= 0) count++;
        }
        return count;
    }
    
    void clear() {
        points.clear();
        owner.clear();
        neighbour.clear();
        face_areas.clear();
        face_centres.clear();
        cell_centres.clear();
        cell_volumes.clear();
        delta_f.clear();
        k_f.clear();
        cell_offsets.clear();
        cell_nodes.clear();
        non_ortho.clear();
        skewness.clear();
        aspect_ratio.clear();
        stab_mask.clear();
        cell_regions.clear();
        patches.clear();
    }
};

// Polyhedral mesh builder and metrics evaluator
class DiFVMTraversal {
public:
    static void buildTopology(
        NativeMeshData& mesh,
        const std::vector<Vec3>& node_coords,
        const std::vector<std::vector<int64_t>>& cells_connectivity,
        const std::vector<int32_t>& regions = {}
    );

    static double computeMaxDivergenceClosure(const NativeMeshData& mesh);
    static double computeMaxDecompositionError(const NativeMeshData& mesh);
};

} // namespace cfd

struct CfdNativeMesh {
    cfd::NativeMeshData data;
};
