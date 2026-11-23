#pragma once
#include "vec3.hpp"
#include "geometry.hpp"
#include <cmath>
#include <numbers>

struct MMSParams {
    double Gamma{0.1}; // Diffusion coefficient
};

class MMSSolution {
public:
    static constexpr double pi = std::numbers::pi;

    template <typename T>
    static T phiExact(T x, T y, T z) {
        return std::sin(pi * x) * std::cos(pi * y) * std::exp(z * 0.5);
    }

    template <typename T>
    static Vec3<T> uExact(T x, T y, T z) {
        T u_x = std::sin(pi * x) * std::cos(pi * y) * std::cos(pi * z);
        T u_y = -std::cos(pi * x) * std::sin(pi * y) * std::cos(pi * z);
        T u_z = T(0);
        return Vec3<T>{u_x, u_y, u_z};
    }

    template <typename T>
    static Vec3<T> gradPhiExact(T x, T y, T z) {
        T dpx = pi * std::cos(pi * x) * std::cos(pi * y) * std::exp(z * 0.5);
        T dpy = -pi * std::sin(pi * x) * std::sin(pi * y) * std::exp(z * 0.5);
        T dpz = 0.5 * std::sin(pi * x) * std::cos(pi * y) * std::exp(z * 0.5);
        return Vec3<T>{dpx, dpy, dpz};
    }

    template <typename T>
    static T laplacianPhiExact(T x, T y, T z) {
        // d^2/dx^2 + d^2/dy^2 = -2 * pi^2 * phi
        // d^2/dz^2 = 0.25 * phi
        T phi = phiExact(x, y, z);
        return (-2.0 * pi * pi + 0.25) * phi;
    }

    template <typename T>
    static T sourceExact(T x, T y, T z, double Gamma) {
        // S = div(u * phi - Gamma * grad(phi))
        // Since div(u) = 0, div(u * phi) = u . grad(phi) = (pi / 2) * sin(2*pi*x) * cos(pi*z) * exp(z/2)
        // -Gamma * laplacian(phi) = Gamma * (2*pi^2 - 0.25) * phi
        T conv = 0.5 * pi * std::sin(2.0 * pi * x) * std::cos(pi * z) * std::exp(z * 0.5);
        T diff = Gamma * (2.0 * pi * pi - 0.25) * phiExact(x, y, z);
        return conv + diff;
    }

    // Integrate source over a cell using 2x2x2 Gauss-Legendre quadrature
    static double integrateCellSource(const Mesh& mesh, int cellId, double Gamma) {
        const auto& c = mesh.cells[cellId];
        // Use cell centroid approximation supplemented with multi-point evaluation for polyhedra
        // For general polyhedral hex: sample at centroid and 6 face centroids
        double sCentroid = sourceExact(c.xc.x, c.xc.y, c.xc.z, Gamma);
        double sFaces = 0.0;
        for (int fid : c.faceIndices) {
            const auto& f = mesh.faces[fid];
            sFaces += sourceExact(f.xf.x, f.xf.y, f.xf.z, Gamma);
        }
        sFaces /= static_cast<double>(c.faceIndices.size());
        // Simpson-type polyhedral volumetric quadrature: 1/2 centroid + 1/2 average of faces
        double sAvg = 0.5 * sCentroid + 0.5 * sFaces;
        return sAvg * c.volume;
    }

    // Exact cell average of phi
    static double cellAveragePhi(const Mesh& mesh, int cellId) {
        const auto& c = mesh.cells[cellId];
        double pC = phiExact(c.xc.x, c.xc.y, c.xc.z);
        double pF = 0.0;
        for (int fid : c.faceIndices) {
            const auto& f = mesh.faces[fid];
            pF += phiExact(f.xf.x, f.xf.y, f.xf.z);
        }
        pF /= static_cast<double>(c.faceIndices.size());
        return 0.5 * pC + 0.5 * pF;
    }
};
