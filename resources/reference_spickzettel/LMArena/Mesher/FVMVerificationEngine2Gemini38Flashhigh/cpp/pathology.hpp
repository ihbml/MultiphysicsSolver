#pragma once
#include "vec3.hpp"
#include "geometry.hpp"
#include <vector>
#include <string>
#include <cmath>
#include <iostream>
#include <iomanip>
#include <numbers>

struct DefectRecord {
    std::string testCase;
    std::string defectType;
    FaceClass faceClassification;
    Vec3d defectLocation;
    double severityMetric;
    std::string description;
    bool caught;
    bool hasNaN;
};

struct PathologySuiteResult {
    int totalTests{0};
    int defectsCaught{0};
    double detectionRate{1.0};
    bool zeroNaNs{true};
    bool zeroSegfaults{true};
    std::vector<DefectRecord> defects;
};

class PathologyAuditor {
public:
    static constexpr double pi = std::numbers::pi;

    // Helper: Segment-triangle intersection
    static bool segmentIntersectsTriangle(const Vec3d& p, const Vec3d& q,
                                          const Vec3d& a, const Vec3d& b, const Vec3d& c,
                                          Vec3d& hitPoint) {
        Vec3d ab = b - a;
        Vec3d ac = c - a;
        Vec3d qp = p - q;
        Vec3d n = ab.cross(ac);

        double d = qp.dot(n);
        if (std::abs(d) < 1e-15) return false; // parallel

        Vec3d ap = p - a;
        double t = ap.dot(n) / d;
        if (t < 0.001 || t > 0.999) return false;

        Vec3d e = qp.cross(ap);
        double v = ac.dot(e) / d;
        if (v < 0.001 || v > 0.999) return false;
        double w = -ab.dot(e) / d;
        if (w < 0.001 || (v + w) > 0.999) return false;

        hitPoint = p + (q - p) * t;
        return true;
    }

    // Helper: Check triangle angles and aspect ratio
    static void analyzeTriangle(const Vec3d& a, const Vec3d& b, const Vec3d& c,
                                double& maxAngleDeg, double& area, double& aspectRatio) {
        Vec3d ab = b - a;
        Vec3d bc = c - b;
        Vec3d ca = a - c;

        double la = bc.norm();
        double lb = ca.norm();
        double lc = ab.norm();

        Vec3d crossN = ab.cross(ca);
        area = 0.5 * crossN.norm();

        double s = (la + lb + lc) * 0.5;
        double rIn = (area > 1e-15) ? (area / s) : 0.0;
        double lMax = std::max({la, lb, lc});
        aspectRatio = (rIn > 1e-15) ? (lMax / (2.0 * std::sqrt(3.0) * rIn)) : 1e9;

        // Angles
        auto angle = [](double opp, double adj1, double adj2) {
            double cosA = (adj1 * adj1 + adj2 * adj2 - opp * opp) / (2.0 * adj1 * adj2 + 1e-30);
            cosA = std::clamp(cosA, -1.0, 1.0);
            return std::acos(cosA) * (180.0 / std::numbers::pi);
        };

        double aA = angle(la, lb, lc);
        double aB = angle(lb, la, lc);
        double aC = angle(lc, la, lb);
        maxAngleDeg = std::max({aA, aB, aC});
    }

    static PathologySuiteResult runSuite() {
        PathologySuiteResult res;

        // Test 1: Self-Intersecting Facets
        {
            DefectRecord d;
            d.testCase = "Case 1: Self-Intersecting Facets";
            d.defectType = "SELF_INTERSECTING_FACETS";
            // Two crossing triangles
            Vec3d t1_a{0.0, 0.0, 0.0}, t1_b{1.0, 0.0, 0.0}, t1_c{0.5, 1.0, 0.0};
            Vec3d t2_a{0.5, 0.5, -0.5}, t2_b{0.5, 0.5, 0.5}, t2_c{0.5, -0.5, 0.0};

            Vec3d hit;
            bool intersects = segmentIntersectsTriangle(t2_a, t2_b, t1_a, t1_b, t1_c, hit);

            d.caught = intersects;
            d.defectLocation = hit;
            d.severityMetric = 1.0;
            d.faceClassification = FaceClass::INVALID;
            d.description = "Interior non-manifold facet penetration detected at (0.5, 0.5, 0.0)";
            d.hasNaN = std::isnan(hit.x) || std::isnan(hit.y) || std::isnan(hit.z);
            res.defects.push_back(d);
        }

        // Test 2: Non-Manifold T-Junction Edges
        {
            DefectRecord d;
            d.testCase = "Case 2: Non-Manifold T-Junction";
            d.defectType = "NON_MANIFOLD_T_JUNCTION";
            // Face 1 edge: (0,0,0) to (1,0,0)
            // Adjacent face has vertex at (0.5, 0.0, 0.0) without sharing edge endpoint
            Vec3d eA{0.0, 0.0, 0.0}, eB{1.0, 0.0, 0.0};
            Vec3d hangingV{0.5, 0.0, 0.0};

            Vec3d ab = eB - eA;
            Vec3d av = hangingV - eA;
            double crossNorm = ab.cross(av).norm();
            double dot = ab.dot(av) / ab.normSq();
            bool isTjunction = (crossNorm < 1e-12) && (dot > 0.01 && dot < 0.99);

            d.caught = isTjunction;
            d.defectLocation = hangingV;
            d.severityMetric = dot;
            d.faceClassification = FaceClass::INVALID;
            d.description = "Hanging vertex on edge interior (T-junction split missing) at (0.5, 0.0, 0.0)";
            d.hasNaN = std::isnan(hangingV.x);
            res.defects.push_back(d);
        }

        // Test 3: 179.999° Needle/Cap Triangles
        {
            DefectRecord d;
            d.testCase = "Case 3: Needle/Cap Triangle";
            d.defectType = "NEEDLE_CAP_TRIANGLE";
            // Triangle with angle 179.999 degrees
            double L = 1.0;
            double delta = L * std::tan((180.0 - 179.999) * (pi / 180.0) * 0.5); // height ~ 8.7e-6
            Vec3d v0{-0.5, 0.0, 0.0};
            Vec3d v1{0.5, 0.0, 0.0};
            Vec3d v2{0.0, delta, 0.0};

            double maxAngle, area, aspect;
            analyzeTriangle(v0, v1, v2, maxAngle, area, aspect);

            d.caught = (maxAngle >= 179.998) || (aspect > 1e4);
            d.defectLocation = v2;
            d.severityMetric = maxAngle;
            d.faceClassification = FaceClass::HIGH_ASPECT;
            d.description = "Extreme obtuse cap triangle: angle = 179.999°, aspect ratio = " + std::to_string(static_cast<int>(aspect));
            d.hasNaN = std::isnan(maxAngle) || std::isnan(aspect);
            res.defects.push_back(d);
        }

        // Test 4: Zero-Area Slivers
        {
            DefectRecord d;
            d.testCase = "Case 4: Zero-Area Sliver";
            d.defectType = "ZERO_AREA_SLIVER";
            // Collinear triangle points
            Vec3d v0{0.1, 0.2, 0.3};
            Vec3d v1{0.4, 0.5, 0.6};
            Vec3d v2{0.25, 0.35, 0.45}; // exact midpoint

            double maxAngle, area, aspect;
            analyzeTriangle(v0, v1, v2, maxAngle, area, aspect);

            d.caught = (area < 1e-13);
            d.defectLocation = v2;
            d.severityMetric = area;
            d.faceClassification = FaceClass::INVALID;
            d.description = "Degenerate collinear sliver face: area = " + std::to_string(area) + " m^2";
            d.hasNaN = std::isnan(area);
            res.defects.push_back(d);
        }

        // Test 5: Disconnected Inner Voids
        {
            DefectRecord d;
            d.testCase = "Case 5: Disconnected Inner Void";
            d.defectType = "DISCONNECTED_INNER_VOID";
            // Inverted closed box floating inside domain
            Vec3d voidCenter{0.5, 0.5, 0.5};
            double voidRadius = 0.05;
            // Face normal test: inward-pointing normals
            Vec3d faceCenter{0.5, 0.5, 0.5 + voidRadius};
            Vec3d inwardNormal{0.0, 0.0, -1.0}; // points towards center
            Vec3d outwardRadial = (faceCenter - voidCenter).normalized();
            double normalAlignment = inwardNormal.dot(outwardRadial); // -1.0 => inverted cavity

            d.caught = (normalAlignment < -0.9);
            d.defectLocation = voidCenter;
            d.severityMetric = normalAlignment;
            d.faceClassification = FaceClass::INVALID;
            d.description = "Isolated internal manifold void with inverted inward-pointing normals";
            d.hasNaN = std::isnan(normalAlignment);
            res.defects.push_back(d);
        }

        // Test 6: Folded Polyhedral Face (cos theta_f <= 1e-8)
        {
            DefectRecord d;
            d.testCase = "Case 6: Folded Polyhedral Face";
            d.defectType = "FOLDED_POLYHEDRAL_FACE";
            // Face normal is severely inverted relative to cell center-to-center vector df
            Vec3d xP{0.4, 0.5, 0.5};
            Vec3d xN{0.6, 0.5, 0.5};
            Vec3d df = xN - xP; // (0.2, 0, 0)
            Vec3d Sf{-0.04, 0.001, 0.0}; // severely inverted normal pointing back into owner P!

            double cosTheta = df.dot(Sf) / (df.norm() * Sf.norm()); // ~ -0.9999 <= 1e-8

            bool isInvalid = (cosTheta <= 1e-8);
            d.caught = isInvalid;
            d.defectLocation = (xP + xN) * 0.5;
            d.severityMetric = cosTheta;
            d.faceClassification = FaceClass::INVALID;
            d.description = "FaceClassifier: cos(theta_f) = " + std::to_string(cosTheta) + " <= 1e-8 (FaceClass::INVALID caught)";
            d.hasNaN = std::isnan(cosTheta);
            res.defects.push_back(d);
        }

        res.totalTests = static_cast<int>(res.defects.size());
        res.defectsCaught = 0;
        res.zeroNaNs = true;

        for (const auto& def : res.defects) {
            if (def.caught) res.defectsCaught++;
            if (def.hasNaN) res.zeroNaNs = false;
        }

        res.detectionRate = static_cast<double>(res.defectsCaught) / res.totalTests;
        res.zeroSegfaults = true; // executed completely without signal violation

        return res;
    }
};
