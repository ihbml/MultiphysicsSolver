#pragma once
#include "vec3.hpp"
#include <vector>
#include <string>
#include <cmath>
#include <algorithm>
#include <numbers>

enum class FaceClass {
    REGULAR = 0,
    BOUNDARY = 1,
    SKEWED = 2,
    NON_ORTHOGONAL = 3,
    HIGH_ASPECT = 4,
    INVALID = 5
};

inline const char* faceClassToString(FaceClass c) {
    switch (c) {
        case FaceClass::REGULAR: return "REGULAR";
        case FaceClass::BOUNDARY: return "BOUNDARY";
        case FaceClass::SKEWED: return "SKEWED";
        case FaceClass::NON_ORTHOGONAL: return "NON_ORTHOGONAL";
        case FaceClass::HIGH_ASPECT: return "HIGH_ASPECT";
        case FaceClass::INVALID: return "INVALID";
    }
    return "UNKNOWN";
}

struct Face {
    int id{-1};
    int owner{-1};      // Cell P
    int neighbor{-1};   // Cell N (-1 for boundary)
    std::vector<int> vertexIndices;

    Vec3d Sf{0, 0, 0};       // Face area vector pointing P -> N
    Vec3d xf{0, 0, 0};       // Face centroid
    double area{0.0};        // ||Sf||

    // Geometric decomposition terms
    Vec3d df{0, 0, 0};       // xN - xP
    double dfMag{0.0};
    double beta_f{0.5};      // Plane-intersection weight
    double alpha_f{0.0};     // Over-relaxed diffusion scalar: ||Sf||^2 / (df . Sf)
    Vec3d m_skew{0, 0, 0};   // Skewness vector: xf - (xP + beta_f * df)
    Vec3d k_corr{0, 0, 0};   // Non-orthogonal correction vector: Sf - alpha_f * df
    double nonOrthoAngleDeg{0.0}; // Angle between df and Sf in degrees
    double skewnessMetric{0.0};   // ||m_skew|| / ||df||
    FaceClass classification{FaceClass::REGULAR};

    // Boundary info
    int patchId{-1}; // 0: -x, 1: +x, 2: -y, 3: +y, 4: -z, 5: +z
};

struct Cell {
    int id{-1};
    Vec3d xc{0, 0, 0};       // Centroid
    double volume{0.0};      // Volume
    std::vector<int> faceIndices;
    std::vector<int> faceDirections; // +1 if outward normal is Sf, -1 if outward is -Sf
    std::vector<int> neighborCells;
};

struct Mesh {
    int nx{0}, ny{0}, nz{0};
    double h{0.0};
    double alphaSkew{0.0};
    std::vector<Vec3d> vertices;
    std::vector<Face> faces;
    std::vector<Cell> cells;

    // Summary statistics
    double minVol{1e30}, maxVol{-1e30}, avgVol{0.0};
    double maxNonOrthoAngle{0.0}, avgNonOrthoAngle{0.0};
    double maxSkewness{0.0}, avgSkewness{0.0};
    int invalidFaceCount{0};
};

// Deterministic 3D mesh generator on [0, 1]^3 with controlled skewness and non-orthogonality
inline Mesh generateDistortedHexMesh(int n, double alphaSkew = 0.28) {
    Mesh mesh;
    mesh.nx = n;
    mesh.ny = n;
    mesh.nz = n;
    mesh.h = 1.0 / n;
    mesh.alphaSkew = alphaSkew;

    const double h = mesh.h;
    const int nx = n, ny = n, nz = n;
    const int numV = (nx + 1) * (ny + 1) * (nz + 1);
    mesh.vertices.resize(numV);

    auto vIdx = [nx, ny](int i, int j, int k) {
        return i + (nx + 1) * (j + (ny + 1) * k);
    };

    const double pi = std::numbers::pi;

    // Vertex coordinates with smooth and deterministic pseudo-random perturbation
    for (int k = 0; k <= nz; ++k) {
        double z0 = k * h;
        for (int j = 0; j <= ny; ++j) {
            double y0 = j * h;
            for (int i = 0; i <= nx; ++i) {
                double x0 = i * h;
                int idx = vIdx(i, j, k);

                // Interior vertices get perturbed, boundary vertices stay on boundary planes
                Vec3d pert{0, 0, 0};
                if (i > 0 && i < nx && j > 0 && j < ny && k > 0 && k < nz) {
                    // Smooth 3D wave perturbation inducing high face non-orthogonality
                    double s1 = std::sin(pi * x0) * std::sin(2.0 * pi * y0) * std::cos(pi * z0);
                    double s2 = std::cos(pi * x0) * std::sin(pi * y0) * std::sin(2.0 * pi * z0);
                    double s3 = std::sin(2.0 * pi * x0) * std::cos(pi * y0) * std::sin(pi * z0);

                    // High-frequency deterministic pseudo-random modulation
                    double prng1 = std::sin(12.9898 * (i + 1) + 78.233 * (j + 1) + 37.719 * (k + 1));
                    double prng2 = std::sin(39.346 * (i + 1) + 11.135 * (j + 1) + 83.155 * (k + 1));
                    double prng3 = std::sin(73.156 * (i + 1) + 52.842 * (j + 1) + 19.471 * (k + 1));
                    double r1 = prng1 - std::floor(prng1) - 0.5;
                    double r2 = prng2 - std::floor(prng2) - 0.5;
                    double r3 = prng3 - std::floor(prng3) - 0.5;

                    // Induce non-orthogonality up to 45 deg and strong face skewness
                    double shear = 0.5 * std::sin(pi * (x0 + y0));
                    pert.x = alphaSkew * h * (0.70 * s1 + 0.30 * r1 + 0.40 * shear);
                    pert.y = alphaSkew * h * (0.70 * s2 + 0.30 * r2 - 0.40 * shear);
                    pert.z = alphaSkew * h * (0.70 * s3 + 0.30 * r3 + 0.30 * std::cos(pi * z0));
                } else {
                    // On boundary planes, preserve domain bounds [0, 1]^3 while perturbing tangential coordinates
                    if (i > 0 && i < nx) {
                        double st = std::sin(pi * x0);
                        if (j > 0 && j < ny) pert.y = 0.5 * alphaSkew * h * st;
                        if (k > 0 && k < nz) pert.z = 0.5 * alphaSkew * h * st;
                    }
                }

                mesh.vertices[idx] = Vec3d{x0 + pert.x, y0 + pert.y, z0 + pert.z};
            }
        }
    }

    // Cells
    const int numCells = nx * ny * nz;
    mesh.cells.resize(numCells);
    auto cIdx = [nx, ny](int i, int j, int k) {
        return i + nx * (j + ny * k);
    };

    for (int k = 0; k < nz; ++k) {
        for (int j = 0; j < ny; ++j) {
            for (int i = 0; i < nx; ++i) {
                int cid = cIdx(i, j, k);
                mesh.cells[cid].id = cid;
            }
        }
    }

    // Build unique faces:
    // X-faces: (nx + 1) * ny * nz
    // Y-faces: nx * (ny + 1) * nz
    // Z-faces: nx * ny * (nz + 1)

    // Helper to compute face centroid and area vector
    auto computeFaceGeometry = [&](Face& f) {
        int nVerts = static_cast<int>(f.vertexIndices.size());
        Vec3d center{0, 0, 0};
        for (int vi : f.vertexIndices) {
            center += mesh.vertices[vi];
        }
        center /= static_cast<double>(nVerts);
        f.xf = center;

        Vec3d Sf{0, 0, 0};
        for (int vi = 0; vi < nVerts; ++vi) {
            const Vec3d& p0 = mesh.vertices[f.vertexIndices[vi]];
            const Vec3d& p1 = mesh.vertices[f.vertexIndices[(vi + 1) % nVerts]];
            Sf += (p0 - center).cross(p1 - center) * 0.5;
        }
        f.Sf = Sf;
        f.area = Sf.norm();
    };

    // 1. Internal & Boundary X-faces (normal pointing roughly +x)
    for (int k = 0; k < nz; ++k) {
        for (int j = 0; j < ny; ++j) {
            for (int i = 0; i <= nx; ++i) {
                Face f;
                f.id = static_cast<int>(mesh.faces.size());
                // Vertices of face at x = i: counter-clockwise when viewed from +x
                // v0=(i, j, k), v1=(i, j+1, k), v2=(i, j+1, k+1), v3=(i, j, k+1)
                f.vertexIndices = {
                    vIdx(i, j, k),
                    vIdx(i, j + 1, k),
                    vIdx(i, j + 1, k + 1),
                    vIdx(i, j, k + 1)
                };

                if (i == 0) {
                    // Left boundary face (outward normal for cell is -x, so Sf pointing +x means cell is neighbor or owner)
                    // Let owner = 0, neighbor = -1, invert Sf or keep Sf pointing domain outward?
                    // Convention: Sf points from owner to neighbor.
                    // For boundary face, owner is the boundary cell, Sf points OUTWARD into environment.
                    f.owner = cIdx(0, j, k);
                    f.neighbor = -1;
                    f.patchId = 0;
                    // Outward is -x, so reverse vertices
                    std::reverse(f.vertexIndices.begin(), f.vertexIndices.end());
                } else if (i == nx) {
                    // Right boundary face: outward normal is +x
                    f.owner = cIdx(nx - 1, j, k);
                    f.neighbor = -1;
                    f.patchId = 1;
                } else {
                    // Interior face: owner = cell(i-1), neighbor = cell(i)
                    f.owner = cIdx(i - 1, j, k);
                    f.neighbor = cIdx(i, j, k);
                }

                computeFaceGeometry(f);
                mesh.faces.push_back(f);
            }
        }
    }

    // 2. Y-faces (normal pointing roughly +y)
    for (int k = 0; k < nz; ++k) {
        for (int j = 0; j <= ny; ++j) {
            for (int i = 0; i < nx; ++i) {
                Face f;
                f.id = static_cast<int>(mesh.faces.size());
                // Vertices at y = j: CCW when viewed from +y
                // v0=(i, j, k), v1=(i, j, k+1), v2=(i+1, j, k+1), v3=(i+1, j, k)
                f.vertexIndices = {
                    vIdx(i, j, k),
                    vIdx(i, j, k + 1),
                    vIdx(i + 1, j, k + 1),
                    vIdx(i + 1, j, k)
                };

                if (j == 0) {
                    f.owner = cIdx(i, 0, k);
                    f.neighbor = -1;
                    f.patchId = 2; // -y boundary
                    std::reverse(f.vertexIndices.begin(), f.vertexIndices.end());
                } else if (j == ny) {
                    f.owner = cIdx(i, ny - 1, k);
                    f.neighbor = -1;
                    f.patchId = 3; // +y boundary
                } else {
                    f.owner = cIdx(i, j - 1, k);
                    f.neighbor = cIdx(i, j, k);
                }

                computeFaceGeometry(f);
                mesh.faces.push_back(f);
            }
        }
    }

    // 3. Z-faces (normal pointing roughly +z)
    for (int k = 0; k <= nz; ++k) {
        for (int j = 0; j < ny; ++j) {
            for (int i = 0; i < nx; ++i) {
                Face f;
                f.id = static_cast<int>(mesh.faces.size());
                // Vertices at z = k: CCW when viewed from +z
                // v0=(i, j, k), v1=(i+1, j, k), v2=(i+1, j+1, k), v3=(i, j+1, k)
                f.vertexIndices = {
                    vIdx(i, j, k),
                    vIdx(i + 1, j, k),
                    vIdx(i + 1, j + 1, k),
                    vIdx(i, j + 1, k)
                };

                if (k == 0) {
                    f.owner = cIdx(i, j, 0);
                    f.neighbor = -1;
                    f.patchId = 4; // -z boundary
                    std::reverse(f.vertexIndices.begin(), f.vertexIndices.end());
                } else if (k == nz) {
                    f.owner = cIdx(i, j, nz - 1);
                    f.neighbor = -1;
                    f.patchId = 5; // +z boundary
                } else {
                    f.owner = cIdx(i, j, k - 1);
                    f.neighbor = cIdx(i, j, k);
                }

                computeFaceGeometry(f);
                mesh.faces.push_back(f);
            }
        }
    }

    // Assign faces to cells
    for (const auto& f : mesh.faces) {
        int fid = f.id;
        int P = f.owner;
        mesh.cells[P].faceIndices.push_back(fid);
        mesh.cells[P].faceDirections.push_back(1); // Sf is outward from P
        if (f.neighbor >= 0) {
            int N = f.neighbor;
            mesh.cells[N].faceIndices.push_back(fid);
            mesh.cells[N].faceDirections.push_back(-1); // -Sf is outward from N
            mesh.cells[P].neighborCells.push_back(N);
            mesh.cells[N].neighborCells.push_back(P);
        }
    }

    // Compute cell volumes and centroids via tetrahedral decomposition
    // For each cell, we take an initial point (average of face centers) and decompose each face into triangles
    for (auto& c : mesh.cells) {
        Vec3d x0{0, 0, 0};
        for (int fid : c.faceIndices) {
            x0 += mesh.faces[fid].xf;
        }
        x0 /= static_cast<double>(c.faceIndices.size());

        double totalVol = 0.0;
        Vec3d weightedCentroid{0, 0, 0};

        for (size_t fi = 0; fi < c.faceIndices.size(); ++fi) {
            int fid = c.faceIndices[fi];
            int dir = c.faceDirections[fi];
            const auto& face = mesh.faces[fid];
            const auto& fVerts = face.vertexIndices;
            int nV = static_cast<int>(fVerts.size());

            // Decompose face into triangles with face centroid face.xf
            for (int vi = 0; vi < nV; ++vi) {
                Vec3d vA = mesh.vertices[fVerts[vi]];
                Vec3d vB = mesh.vertices[fVerts[(vi + 1) % nV]];
                if (dir < 0) std::swap(vA, vB); // ensure outward triangle orientation

                // Tetrahedron with vertices x0, face.xf, vA, vB
                // Volume = 1/6 * | (vA - x0) . ((vB - x0) x (face.xf - x0)) |
                Vec3d v1 = face.xf - x0;
                Vec3d v2 = vA - x0;
                Vec3d v3 = vB - x0;
                double tetVol = (v1.dot(v2.cross(v3))) / 6.0;

                // Centroid of tetrahedron
                Vec3d tetCentroid = (x0 + face.xf + vA + vB) * 0.25;

                totalVol += tetVol;
                weightedCentroid += tetCentroid * tetVol;
            }
        }

        c.volume = std::abs(totalVol);
        c.xc = weightedCentroid / c.volume;

        mesh.minVol = std::min(mesh.minVol, c.volume);
        mesh.maxVol = std::max(mesh.maxVol, c.volume);
        mesh.avgVol += c.volume;
    }
    mesh.avgVol /= numCells;

    // Now compute secondary geometric decomposition on all faces:
    // df = xN - xP
    // beta_f = (xf - xP) . Sf / (df . Sf)
    // alpha_f = ||Sf||^2 / (df . Sf)
    // m_skew = xf - (xP + beta_f * df)
    // k_corr = Sf - alpha_f * df
    double nonOrthoSum = 0.0;
    double skewSum = 0.0;
    int internalFaceCount = 0;

    for (auto& f : mesh.faces) {
        int P = f.owner;
        const Vec3d& xP = mesh.cells[P].xc;

        if (f.neighbor >= 0) {
            int N = f.neighbor;
            const Vec3d& xN = mesh.cells[N].xc;
            f.df = xN - xP;
            f.dfMag = f.df.norm();

            double dfDotSf = f.df.dot(f.Sf);
            double cosTheta = dfDotSf / (f.dfMag * f.area);
            cosTheta = std::clamp(cosTheta, -1.0, 1.0);
            f.nonOrthoAngleDeg = std::acos(cosTheta) * (180.0 / pi);

            f.beta_f = (f.xf - xP).dot(f.Sf) / dfDotSf;
            f.alpha_f = (f.area * f.area) / dfDotSf;

            Vec3d xf_prime = xP + f.df * f.beta_f;
            f.m_skew = f.xf - xf_prime;
            f.k_corr = f.Sf - f.df * f.alpha_f;
            f.skewnessMetric = f.m_skew.norm() / f.dfMag;

            // Classification
            if (cosTheta <= 1e-8) {
                f.classification = FaceClass::INVALID;
                mesh.invalidFaceCount++;
            } else if (f.nonOrthoAngleDeg > 20.0) {
                f.classification = FaceClass::NON_ORTHOGONAL;
            } else if (f.skewnessMetric > 0.08) {
                f.classification = FaceClass::SKEWED;
            } else {
                f.classification = FaceClass::REGULAR;
            }

            nonOrthoSum += f.nonOrthoAngleDeg;
            skewSum += f.skewnessMetric;
            mesh.maxNonOrthoAngle = std::max(mesh.maxNonOrthoAngle, f.nonOrthoAngleDeg);
            mesh.maxSkewness = std::max(mesh.maxSkewness, f.skewnessMetric);
            internalFaceCount++;
        } else {
            // Boundary face: df is vector from xP to xf
            f.df = f.xf - xP;
            f.dfMag = f.df.norm();
            double dfDotSf = f.df.dot(f.Sf);
            f.alpha_f = (f.area * f.area) / dfDotSf;
            f.beta_f = 1.0;
            f.m_skew = Vec3d{0, 0, 0};
            f.k_corr = f.Sf - f.df * f.alpha_f;
            f.classification = FaceClass::BOUNDARY;
            double cosTheta = dfDotSf / (f.dfMag * f.area);
            cosTheta = std::clamp(cosTheta, -1.0, 1.0);
            f.nonOrthoAngleDeg = std::acos(cosTheta) * (180.0 / pi);
        }
    }

    if (internalFaceCount > 0) {
        mesh.avgNonOrthoAngle = nonOrthoSum / internalFaceCount;
        mesh.avgSkewness = skewSum / internalFaceCount;
    }

    return mesh;
}
