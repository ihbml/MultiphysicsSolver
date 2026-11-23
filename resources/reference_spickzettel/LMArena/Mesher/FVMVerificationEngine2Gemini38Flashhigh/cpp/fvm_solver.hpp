#pragma once
#include "geometry.hpp"
#include "mms.hpp"
#include <vector>
#include <cmath>
#include <iostream>
#include <iomanip>
#include <numbers>

enum class CorrectionMode {
    FULL_CORRECTION = 0,        // Skewness + Over-Relaxed Non-Orthogonal
    NO_SKEWNESS = 1,            // m_skew = 0 (omits skewness correction)
    NO_NON_ORTHOGONAL = 2,      // k_corr = 0 (omits non-orthogonal correction)
    NAIVE_UNCORRECTED = 3       // Omits both skewness and non-orthogonal
};

inline const char* correctionModeToString(CorrectionMode mode) {
    switch (mode) {
        case CorrectionMode::FULL_CORRECTION: return "Full (Class 1 Skewness + Over-Relaxed Non-Ortho)";
        case CorrectionMode::NO_SKEWNESS: return "Missing Skewness Correction (m_skew = 0)";
        case CorrectionMode::NO_NON_ORTHOGONAL: return "Missing Non-Ortho Correction (k_corr = 0)";
        case CorrectionMode::NAIVE_UNCORRECTED: return "Naive Uncorrected (Neither)";
    }
    return "UNKNOWN";
}

struct FVMSolveResult {
    CorrectionMode mode;
    int nCells{0};
    double h{0.0};
    double L2_error{0.0};
    double Linf_error{0.0};
    double p_obs{0.0}; // Order of accuracy
    double domainAvgPhi{0.0};
    double probeCenterPhi{0.0};
    double exactDomainAvgPhi{0.0};
    int linearIters{0};
    int deferredIters{0};
    double solveTimeSec{0.0};
    std::vector<double> phiSolution;
    std::vector<double> phiExact;
    std::vector<double> errorField;
};

// 3x3 symmetric matrix inversion helper
inline bool invert3x3(const double A[3][3], double inv[3][3]) {
    double det = A[0][0] * (A[1][1] * A[2][2] - A[1][2] * A[2][1])
               - A[0][1] * (A[1][0] * A[2][2] - A[1][2] * A[2][0])
               + A[0][2] * (A[1][0] * A[2][1] - A[1][1] * A[2][0]);
    if (std::abs(det) < 1e-15) return false;
    double invDet = 1.0 / det;

    inv[0][0] =  (A[1][1] * A[2][2] - A[1][2] * A[2][1]) * invDet;
    inv[0][1] = -(A[0][1] * A[2][2] - A[0][2] * A[2][1]) * invDet;
    inv[0][2] =  (A[0][1] * A[1][2] - A[0][2] * A[1][1]) * invDet;

    inv[1][0] = -(A[1][0] * A[2][2] - A[1][2] * A[2][0]) * invDet;
    inv[1][1] =  (A[0][0] * A[2][2] - A[0][2] * A[2][0]) * invDet;
    inv[1][2] = -(A[0][0] * A[1][2] - A[0][2] * A[1][0]) * invDet;

    inv[2][0] =  (A[1][0] * A[2][1] - A[1][1] * A[2][0]) * invDet;
    inv[2][1] = -(A[0][0] * A[2][1] - A[0][1] * A[2][0]) * invDet;
    inv[2][2] =  (A[0][0] * A[1][1] - A[0][1] * A[1][0]) * invDet;
    return true;
}

// Sparse Compressed Row Matrix representation for FVM
struct SparseMatrix {
    int n{0};
    std::vector<int> rowPtr;
    std::vector<int> colIdx;
    std::vector<double> values;
    std::vector<int> diagIdx; // index in values of diagonal entry

    void initFromMesh(const Mesh& mesh) {
        n = static_cast<int>(mesh.cells.size());
        rowPtr.assign(n + 1, 0);
        for (int i = 0; i < n; ++i) {
            rowPtr[i + 1] = rowPtr[i] + 1 + static_cast<int>(mesh.cells[i].neighborCells.size());
        }
        int nnz = rowPtr[n];
        colIdx.resize(nnz);
        values.assign(nnz, 0.0);
        diagIdx.resize(n);

        for (int i = 0; i < n; ++i) {
            int ptr = rowPtr[i];
            colIdx[ptr] = i; // Diagonal first
            diagIdx[i] = ptr;
            ptr++;
            for (int nbr : mesh.cells[i].neighborCells) {
                colIdx[ptr++] = nbr;
            }
        }
    }

    void reset() {
        std::fill(values.begin(), values.end(), 0.0);
    }

    void addValue(int row, int col, double val) {
        int start = rowPtr[row];
        int end = rowPtr[row + 1];
        for (int k = start; k < end; ++k) {
            if (colIdx[k] == col) {
                values[k] += val;
                return;
            }
        }
    }

    void matVec(const std::vector<double>& x, std::vector<double>& y) const {
        for (int i = 0; i < n; ++i) {
            double sum = 0.0;
            int start = rowPtr[i];
            int end = rowPtr[i + 1];
            for (int k = start; k < end; ++k) {
                sum += values[k] * x[colIdx[k]];
            }
            y[i] = sum;
        }
    }
};

// BiCGSTAB linear solver with Jacobi Preconditioner
inline int solveBiCGSTAB(const SparseMatrix& A, const std::vector<double>& b, std::vector<double>& x,
                         int maxIters = 800, double tol = 1e-12) {
    int n = A.n;
    std::vector<double> r(n), r_hat(n), p(n), v(n), s(n), t(n), y(n), z(n);
    std::vector<double> Ax(n);

    // Initial residual: r = b - A*x
    A.matVec(x, Ax);
    double bNorm = 0.0;
    double rNorm = 0.0;
    for (int i = 0; i < n; ++i) {
        r[i] = b[i] - Ax[i];
        r_hat[i] = r[i];
        bNorm += b[i] * b[i];
        rNorm += r[i] * r[i];
    }
    bNorm = std::sqrt(bNorm);
    rNorm = std::sqrt(rNorm);
    if (bNorm == 0.0) bNorm = 1.0;
    if (rNorm / bNorm < tol) return 0;

    double rho_1 = 1.0, alpha = 1.0, omega = 1.0;
    int iter = 0;

    // Jacobi preconditioner: invDiag[i] = 1.0 / A[i,i]
    std::vector<double> invDiag(n, 1.0);
    for (int i = 0; i < n; ++i) {
        double d = A.values[A.diagIdx[i]];
        if (std::abs(d) > 1e-15) invDiag[i] = 1.0 / d;
    }

    while (iter < maxIters) {
        iter++;
        double rho = 0.0;
        for (int i = 0; i < n; ++i) rho += r_hat[i] * r[i];
        if (std::abs(rho) < 1e-28) break;

        if (iter == 1) {
            p = r;
        } else {
            double beta = (rho / rho_1) * (alpha / omega);
            for (int i = 0; i < n; ++i) {
                p[i] = r[i] + beta * (p[i] - omega * v[i]);
            }
        }

        // y = M^-1 * p
        for (int i = 0; i < n; ++i) y[i] = invDiag[i] * p[i];
        // v = A * y
        A.matVec(y, v);

        double rhat_dot_v = 0.0;
        for (int i = 0; i < n; ++i) rhat_dot_v += r_hat[i] * v[i];
        if (std::abs(rhat_dot_v) < 1e-28) break;
        alpha = rho / rhat_dot_v;

        // s = r - alpha * v
        for (int i = 0; i < n; ++i) s[i] = r[i] - alpha * v[i];

        // check norm of s
        double sNorm = 0.0;
        for (int i = 0; i < n; ++i) sNorm += s[i] * s[i];
        sNorm = std::sqrt(sNorm);
        if (sNorm / bNorm < tol) {
            for (int i = 0; i < n; ++i) x[i] += alpha * y[i];
            break;
        }

        // z = M^-1 * s
        for (int i = 0; i < n; ++i) z[i] = invDiag[i] * s[i];
        // t = A * z
        A.matVec(z, t);

        double t_dot_s = 0.0, t_dot_t = 0.0;
        for (int i = 0; i < n; ++i) {
            t_dot_s += t[i] * s[i];
            t_dot_t += t[i] * t[i];
        }
        if (t_dot_t < 1e-28) break;
        omega = t_dot_s / t_dot_t;

        for (int i = 0; i < n; ++i) {
            x[i] += alpha * y[i] + omega * z[i];
            r[i] = s[i] - omega * t[i];
        }

        rho_1 = rho;

        double curNorm = 0.0;
        for (int i = 0; i < n; ++i) curNorm += r[i] * r[i];
        if (std::sqrt(curNorm) / bNorm < tol) break;
    }
    return iter;
}

class FVMSolver {
public:
    static FVMSolveResult solveMMS(const Mesh& mesh, double Gamma, CorrectionMode mode) {
        FVMSolveResult res;
        res.mode = mode;
        res.nCells = static_cast<int>(mesh.cells.size());
        res.h = mesh.h;

        int nCells = res.nCells;
        res.phiSolution.assign(nCells, 0.0);
        res.phiExact.resize(nCells);
        res.errorField.resize(nCells);

        for (int c = 0; c < nCells; ++c) {
            res.phiExact[c] = MMSSolution::cellAveragePhi(mesh, c);
        }

        // Precompute WLSQ matrix inversion for each cell
        // Minimize sum w_i^2 (gradP . r_i - deltaPhi_i)^2
        struct WLSQData {
            double invM[3][3]{{0}};
            std::vector<Vec3d> r_nbr;
            std::vector<double> w2_nbr;
            std::vector<int> nbrCellIds; // >= 0: cell id, < 0: boundary face index
            std::vector<Vec3d> x_nbr;
        };

        std::vector<WLSQData> wlsq(nCells);
        for (int c = 0; c < nCells; ++c) {
            const auto& cell = mesh.cells[c];
            double M[3][3] = {{0}};

            for (size_t fi = 0; fi < cell.faceIndices.size(); ++fi) {
                int fid = cell.faceIndices[fi];
                const auto& face = mesh.faces[fid];

                Vec3d targetPt;
                int targetId = -1;

                if (face.neighbor >= 0) {
                    targetId = (face.owner == c) ? face.neighbor : face.owner;
                    targetPt = mesh.cells[targetId].xc;
                } else {
                    targetId = -1 - fid; // boundary face marker
                    targetPt = face.xf;
                }

                Vec3d r = targetPt - cell.xc;
                double dist = r.norm();
                if (dist < 1e-12) continue;
                double w = 1.0 / dist;
                double w2 = w * w;

                wlsq[c].r_nbr.push_back(r);
                wlsq[c].w2_nbr.push_back(w2);
                wlsq[c].nbrCellIds.push_back(targetId);
                wlsq[c].x_nbr.push_back(targetPt);

                M[0][0] += w2 * r.x * r.x;
                M[0][1] += w2 * r.x * r.y;
                M[0][2] += w2 * r.x * r.z;
                M[1][0] += w2 * r.y * r.x;
                M[1][1] += w2 * r.y * r.y;
                M[1][2] += w2 * r.y * r.z;
                M[2][0] += w2 * r.z * r.x;
                M[2][1] += w2 * r.z * r.y;
                M[2][2] += w2 * r.z * r.z;
            }
            invert3x3(M, wlsq[c].invM);
        }

        // Gradients array
        std::vector<Vec3d> cellGrad(nCells, Vec3d{0, 0, 0});

        auto computeGradients = [&](const std::vector<double>& phi) {
            for (int c = 0; c < nCells; ++c) {
                double b[3] = {0, 0, 0};
                double phiP = phi[c];
                const auto& wd = wlsq[c];
                for (size_t k = 0; k < wd.r_nbr.size(); ++k) {
                    double phiN = 0.0;
                    if (wd.nbrCellIds[k] >= 0) {
                        phiN = phi[wd.nbrCellIds[k]];
                    } else {
                        // Boundary face Dirichlet
                        const Vec3d& pt = wd.x_nbr[k];
                        phiN = MMSSolution::phiExact(pt.x, pt.y, pt.z);
                    }
                    double dPhi = phiN - phiP;
                    double w2 = wd.w2_nbr[k];
                    b[0] += w2 * dPhi * wd.r_nbr[k].x;
                    b[1] += w2 * dPhi * wd.r_nbr[k].y;
                    b[2] += w2 * dPhi * wd.r_nbr[k].z;
                }
                cellGrad[c].x = wd.invM[0][0] * b[0] + wd.invM[0][1] * b[1] + wd.invM[0][2] * b[2];
                cellGrad[c].y = wd.invM[1][0] * b[0] + wd.invM[1][1] * b[1] + wd.invM[1][2] * b[2];
                cellGrad[c].z = wd.invM[2][0] * b[0] + wd.invM[2][1] * b[1] + wd.invM[2][2] * b[2];
            }
        };

        // Sparse matrix for implicit FVM operator
        SparseMatrix A;
        A.initFromMesh(mesh);

        // Precompute integrated volumetric source vector
        std::vector<double> S_vol(nCells);
        for (int c = 0; c < nCells; ++c) {
            S_vol[c] = MMSSolution::integrateCellSource(mesh, c, Gamma);
        }

        // Deferred correction Picard loop
        const int maxOuterIters = 60;
        int totalLinearIters = 0;
        int outerIter = 0;

        std::vector<double> phi = res.phiSolution; // start at 0
        std::vector<double> RHS(nCells);

        bool useSkewness = (mode == CorrectionMode::FULL_CORRECTION || mode == CorrectionMode::NO_NON_ORTHOGONAL);
        bool useNonOrtho = (mode == CorrectionMode::FULL_CORRECTION || mode == CorrectionMode::NO_SKEWNESS);

        for (outerIter = 0; outerIter < maxOuterIters; ++outerIter) {
            computeGradients(phi);
            A.reset();
            RHS = S_vol;

            // Assemble face contributions
            for (const auto& f : mesh.faces) {
                int P = f.owner;
                Vec3d u_f = MMSSolution::uExact(f.xf.x, f.xf.y, f.xf.z);
                double mDot = u_f.dot(f.Sf); // mass flow rate

                if (f.neighbor >= 0) {
                    // Internal face between P and N
                    int N = f.neighbor;

                    // 1. Implicit diffusion: Gamma * alpha_f * (phi_P - phi_N)
                    double diffCoeff = Gamma * f.alpha_f;
                    A.addValue(P, P,  diffCoeff);
                    A.addValue(P, N, -diffCoeff);
                    A.addValue(N, N,  diffCoeff);
                    A.addValue(N, P, -diffCoeff);

                    // 2. Implicit convection: First-Order Upwind
                    double convP_to_N = std::max(mDot, 0.0);
                    double convN_to_P = std::min(mDot, 0.0);
                    A.addValue(P, P,  convP_to_N);
                    A.addValue(P, N,  convN_to_P);
                    A.addValue(N, P, -convP_to_N);
                    A.addValue(N, N, -convN_to_P);

                    // 3. Explicit Deferred Convection Correction (Class 1 skewness)
                    // Linear interpolation to intersection point:
                    double phi_prime = (1.0 - f.beta_f) * phi[P] + f.beta_f * phi[N];
                    // Face gradient: distance-weighted
                    Vec3d grad_f = cellGrad[P] * (1.0 - f.beta_f) + cellGrad[N] * f.beta_f;

                    double phi_f = phi_prime;
                    if (useSkewness) {
                        phi_f += grad_f.dot(f.m_skew);
                    }

                    double upwind_flux = convP_to_N * phi[P] + convN_to_P * phi[N];
                    double deferred_conv = mDot * phi_f - upwind_flux;

                    // 4. Explicit Non-Orthogonal Diffusion Correction
                    double nonOrthoCorr = 0.0;
                    if (useNonOrtho) {
                        nonOrthoCorr = -Gamma * grad_f.dot(f.k_corr);
                    }

                    // Net explicit flux from P to N: deferred_conv + nonOrthoCorr
                    // Add to RHS: RHS_P -= NetFlux, RHS_N += NetFlux
                    RHS[P] -= (deferred_conv + nonOrthoCorr);
                    RHS[N] += (deferred_conv + nonOrthoCorr);

                } else {
                    // Boundary face: Dirichlet condition
                    double phi_b = MMSSolution::phiExact(f.xf.x, f.xf.y, f.xf.z);

                    // Diffusion: Gamma * alpha_f * (phi_P - phi_b)
                    double diffCoeff = Gamma * f.alpha_f;
                    A.addValue(P, P, diffCoeff);
                    RHS[P] += diffCoeff * phi_b;

                    if (useNonOrtho) {
                        // Boundary non-ortho correction
                        double nonOrthoCorr = -Gamma * cellGrad[P].dot(f.k_corr);
                        RHS[P] -= nonOrthoCorr;
                    }

                    // Convection on boundary
                    if (mDot > 0.0) {
                        // Outflow: phi_f = phi_P
                        A.addValue(P, P, mDot);
                    } else {
                        // Inflow: phi_f = phi_b
                        RHS[P] -= mDot * phi_b;
                    }
                }
            }

            // Solve linear system
            std::vector<double> phi_new = phi;
            int linIters = solveBiCGSTAB(A, RHS, phi_new, 500, 1e-13);
            totalLinearIters += linIters;

            // Check outer convergence
            double maxDiff = 0.0;
            for (int c = 0; c < nCells; ++c) {
                maxDiff = std::max(maxDiff, std::abs(phi_new[c] - phi[c]));
            }
            phi = std::move(phi_new);

            if (maxDiff < 1e-11 && outerIter >= 2) {
                break;
            }
        }

        res.phiSolution = phi;
        res.linearIters = totalLinearIters;
        res.deferredIters = outerIter + 1;

        // Compute error norms
        double sumSqErr = 0.0;
        double totalVol = 0.0;
        double maxErr = 0.0;

        for (int c = 0; c < nCells; ++c) {
            double err = std::abs(res.phiSolution[c] - res.phiExact[c]);
            res.errorField[c] = res.phiSolution[c] - res.phiExact[c];
            sumSqErr += mesh.cells[c].volume * err * err;
            totalVol += mesh.cells[c].volume;
            maxErr = std::max(maxErr, err);
        }

        res.L2_error = std::sqrt(sumSqErr / totalVol);
        res.Linf_error = maxErr;

        double sumPhi = 0.0;
        double sumExactPhi = 0.0;
        int centerCell = -1;
        double minCenterDist = 1e30;

        for (int c = 0; c < nCells; ++c) {
            sumPhi += mesh.cells[c].volume * res.phiSolution[c];
            sumExactPhi += mesh.cells[c].volume * res.phiExact[c];
            double dist = (mesh.cells[c].xc - Vec3d{0.5, 0.5, 0.5}).norm();
            if (dist < minCenterDist) {
                minCenterDist = dist;
                centerCell = c;
            }
        }
        res.domainAvgPhi = sumPhi / totalVol;
        res.exactDomainAvgPhi = sumExactPhi / totalVol;
        res.probeCenterPhi = (centerCell >= 0) ? res.phiSolution[centerCell] : 0.0;

        return res;
    }
};
