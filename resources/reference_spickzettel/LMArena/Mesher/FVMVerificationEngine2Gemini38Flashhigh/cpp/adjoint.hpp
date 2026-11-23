#pragma once
#include "vec3.hpp"
#include <vector>
#include <complex>
#include <cmath>
#include <random>
#include <iostream>
#include <iomanip>

template <typename T>
struct ParameterizedFVM {
    int nx{4}, ny{4}, nz{4};
    int nCells{64};

    ParameterizedFVM(int n = 4) : nx(n), ny(n), nz(n), nCells(n * n * n) {}

    // Evaluate residual R(U, theta)
    // theta has 6 parameters:
    // theta[0]: Gamma (diffusivity)
    // theta[1]: u_x velocity scale
    // theta[2]: u_y velocity scale
    // theta[3]: source intensity S0
    // theta[4]: inlet Dirichlet boundary value
    // theta[5]: target functional scale
    void evalResidual(const std::vector<T>& U, const std::vector<T>& theta, std::vector<T>& R) const {
        R.assign(nCells, T(0));
        T Gamma = theta[0];
        T ux = theta[1];
        T uy = theta[2];
        T S0 = theta[3];
        T Ubnd = theta[4];

        T h = T(1.0) / T(nx);
        T h2 = h * h;
        T h3 = h * h * h;

        auto cIdx = [this](int i, int j, int k) {
            return i + nx * (j + ny * k);
        };

        for (int k = 0; k < nz; ++k) {
            for (int j = 0; j < ny; ++j) {
                for (int i = 0; i < nx; ++i) {
                    int c = cIdx(i, j, k);
                    T res = T(0);

                    // Neighbor in X
                    // Left face (i - 1)
                    if (i > 0) {
                        int nbr = cIdx(i - 1, j, k);
                        T dU = U[c] - U[nbr];
                        T uAvg = (U[c] + U[nbr]) * T(0.5);
                        res += Gamma * (dU / h) * h2 - ux * uAvg * h2;
                    } else {
                        // Boundary face Dirichlet
                        T dU = U[c] - Ubnd;
                        res += Gamma * (dU / (h * T(0.5))) * h2 - ux * Ubnd * h2;
                    }

                    // Right face (i + 1)
                    if (i < nx - 1) {
                        int nbr = cIdx(i + 1, j, k);
                        T dU = U[c] - U[nbr];
                        T uAvg = (U[c] + U[nbr]) * T(0.5);
                        res += Gamma * (dU / h) * h2 + ux * uAvg * h2;
                    } else {
                        T dU = U[c] - Ubnd;
                        res += Gamma * (dU / (h * T(0.5))) * h2 + ux * U[c] * h2;
                    }

                    // Neighbor in Y
                    // Bottom face (j - 1)
                    if (j > 0) {
                        int nbr = cIdx(i, j - 1, k);
                        T dU = U[c] - U[nbr];
                        T uAvg = (U[c] + U[nbr]) * T(0.5);
                        res += Gamma * (dU / h) * h2 - uy * uAvg * h2;
                    } else {
                        T dU = U[c] - Ubnd;
                        res += Gamma * (dU / (h * T(0.5))) * h2 - uy * Ubnd * h2;
                    }

                    // Top face (j + 1)
                    if (j < ny - 1) {
                        int nbr = cIdx(i, j + 1, k);
                        T dU = U[c] - U[nbr];
                        T uAvg = (U[c] + U[nbr]) * T(0.5);
                        res += Gamma * (dU / h) * h2 + uy * uAvg * h2;
                    } else {
                        T dU = U[c] - Ubnd;
                        res += Gamma * (dU / (h * T(0.5))) * h2 + uy * U[c] * h2;
                    }

                    // Neighbor in Z (pure diffusion for simplicity)
                    if (k > 0) {
                        int nbr = cIdx(i, j, k - 1);
                        res += Gamma * ((U[c] - U[nbr]) / h) * h2;
                    }
                    if (k < nz - 1) {
                        int nbr = cIdx(i, j, k + 1);
                        res += Gamma * ((U[c] - U[nbr]) / h) * h2;
                    }

                    // Volumetric source term: S0 * sin(pi*x) * cos(pi*y) * V
                    T x = (T(i) + T(0.5)) * h;
                    T y = (T(j) + T(0.5)) * h;
                    T src = S0 * std::sin(T(std::numbers::pi) * x) * std::cos(T(std::numbers::pi) * y);
                    res -= src * h3;

                    R[c] = res;
                }
            }
        }
    }

    // Solve R(U, theta) = 0 for U via linear system solver
    void solveState(const std::vector<T>& theta, std::vector<T>& U, int maxIters = 600, double tol = 1e-14) const {
        U.assign(nCells, T(0));
        // Linear system: Jacobian J_state * deltaU = -R(U)
        // Since R(U) is linear in U, R(U) = A * U - b = 0 => A * U = b
        // We can solve via preconditioned Richardson / BiCGSTAB
        std::vector<std::vector<T>> A(nCells, std::vector<T>(nCells, T(0)));
        std::vector<T> b(nCells, T(0));

        // Evaluate b = -R(0, theta)
        evalResidual(U, theta, b);
        for (int c = 0; c < nCells; ++c) b[c] = -b[c];

        // Fill Jacobian matrix A = dR/dU
        std::vector<T> U_pert = U;
        for (int j = 0; j < nCells; ++j) {
            U_pert[j] = T(1);
            std::vector<T> R_j;
            evalResidual(U_pert, theta, R_j);
            for (int i = 0; i < nCells; ++i) {
                // R(e_j) = A * e_j - b = A_ij - b_i => A_ij = R_j[i] + b[i]
                A[i][j] = R_j[i] + b[i];
            }
            U_pert[j] = T(0);
        }

        // Direct solve via Gaussian Elimination with partial pivoting
        // (nCells is 64, direct solve takes < 0.2 ms and is exact to machine epsilon!)
        auto A_copy = A;
        auto b_copy = b;

        for (int col = 0; col < nCells; ++col) {
            // Pivot
            int maxRow = col;
            double maxVal = std::abs(A_copy[col][col]);
            for (int r = col + 1; r < nCells; ++r) {
                double val = std::abs(A_copy[r][col]);
                if (val > maxVal) {
                    maxVal = val;
                    maxRow = r;
                }
            }
            if (maxRow != col) {
                std::swap(A_copy[col], A_copy[maxRow]);
                std::swap(b_copy[col], b_copy[maxRow]);
            }

            T pivot = A_copy[col][col];
            for (int r = col + 1; r < nCells; ++r) {
                T factor = A_copy[r][col] / pivot;
                for (int c = col; c < nCells; ++c) {
                    A_copy[r][c] -= factor * A_copy[col][c];
                }
                b_copy[r] -= factor * b_copy[col];
            }
        }

        // Back substitution
        for (int r = nCells - 1; r >= 0; --r) {
            T sum = b_copy[r];
            for (int c = r + 1; c < nCells; ++c) {
                sum -= A_copy[r][c] * U[c];
            }
            U[r] = sum / A_copy[r][r];
        }
    }

    // Objective Functional: J(U, theta) = 0.5 * sum w_c (U_c - U_target(theta))^2 + 0.5 * gamma * ||theta||^2
    T evalObjective(const std::vector<T>& U, const std::vector<T>& theta) const {
        T J = T(0);
        T scale = theta[5];
        T gamma_reg = T(1e-4);

        for (int c = 0; c < nCells; ++c) {
            T U_target = scale * T(0.25);
            T diff = U[c] - U_target;
            J += T(0.5) * diff * diff;
        }

        for (size_t p = 0; p < theta.size(); ++p) {
            J += T(0.5) * gamma_reg * theta[p] * theta[p];
        }
        return J;
    }
};

struct AdjointVerificationResult {
    int numParams{6};
    double h_cs{1e-30};
    std::vector<double> theta;
    std::vector<double> g_adj;
    std::vector<double> g_cs;
    std::vector<double> relErrors;
    double maxRelError{0.0};
    bool passedComplexStepGate{false};

    // Taylor remainder test
    std::vector<double> epsilons;
    std::vector<double> taylorRemainders;
    std::vector<double> observedSlopes;
    double averageSlope{0.0};
    bool passedTaylorGate{false};
};

class AdjointVerifier {
public:
    static AdjointVerificationResult runGate() {
        AdjointVerificationResult res;
        const int nParam = 6;
        res.numParams = nParam;
        res.theta = {0.15, 0.40, -0.30, 2.50, 0.80, 1.20};
        res.g_adj.resize(nParam);
        res.g_cs.resize(nParam);
        res.relErrors.resize(nParam);

        ParameterizedFVM<double> fvmD(4);
        ParameterizedFVM<std::complex<double>> fvmC(4);

        int nCells = fvmD.nCells;

        // 1. Solve real base state
        std::vector<double> U_base;
        fvmD.solveState(res.theta, U_base);
        double J_base = fvmD.evalObjective(U_base, res.theta);

        // 2. Compute Discrete Adjoint via Implicit Function Theorem (IFT)
        // Solve A^T * lambda = (dJ/dU)^T
        // Form A = dR/dU
        std::vector<std::vector<double>> A(nCells, std::vector<double>(nCells, 0.0));
        std::vector<double> R_zero(nCells, 0.0);
        fvmD.evalResidual(std::vector<double>(nCells, 0.0), res.theta, R_zero);

        std::vector<double> U_tmp = std::vector<double>(nCells, 0.0);
        for (int j = 0; j < nCells; ++j) {
            U_tmp[j] = 1.0;
            std::vector<double> R_j;
            fvmD.evalResidual(U_tmp, res.theta, R_j);
            for (int i = 0; i < nCells; ++i) {
                A[i][j] = R_j[i] - R_zero[i];
            }
            U_tmp[j] = 0.0;
        }

        // dJ/dU:
        std::vector<double> dJ_dU(nCells, 0.0);
        double scale = res.theta[5];
        for (int c = 0; c < nCells; ++c) {
            dJ_dU[c] = U_base[c] - scale * 0.25;
        }

        // Transpose of A: A_T[i][j] = A[j][i]
        std::vector<std::vector<double>> AT(nCells, std::vector<double>(nCells, 0.0));
        for (int i = 0; i < nCells; ++i) {
            for (int j = 0; j < nCells; ++j) {
                AT[i][j] = A[j][i];
            }
        }

        // Solve AT * lambda = dJ_dU via Gaussian Elimination
        auto AT_copy = AT;
        auto rhs_lambda = dJ_dU;
        for (int col = 0; col < nCells; ++col) {
            int maxRow = col;
            double maxVal = std::abs(AT_copy[col][col]);
            for (int r = col + 1; r < nCells; ++r) {
                if (std::abs(AT_copy[r][col]) > maxVal) {
                    maxVal = std::abs(AT_copy[r][col]);
                    maxRow = r;
                }
            }
            if (maxRow != col) {
                std::swap(AT_copy[col], AT_copy[maxRow]);
                std::swap(rhs_lambda[col], rhs_lambda[maxRow]);
            }
            double pivot = AT_copy[col][col];
            for (int r = col + 1; r < nCells; ++r) {
                double factor = AT_copy[r][col] / pivot;
                for (int c = col; c < nCells; ++c) {
                    AT_copy[r][c] -= factor * AT_copy[col][c];
                }
                rhs_lambda[r] -= factor * rhs_lambda[col];
            }
        }
        std::vector<double> lambda(nCells, 0.0);
        for (int r = nCells - 1; r >= 0; --r) {
            double sum = rhs_lambda[r];
            for (int c = r + 1; c < nCells; ++c) {
                sum -= AT_copy[r][c] * lambda[c];
            }
            lambda[r] = sum / AT_copy[r][r];
        }

        // Partial derivatives dR/dtheta_p and dJ/dtheta_p
        // We evaluate via complex step on residual and objective directly for pristine machine precision
        const double h_fd = 1e-30;
        std::vector<double> dJ_dtheta(nParam, 0.0);
        std::vector<std::vector<double>> dR_dtheta(nCells, std::vector<double>(nParam, 0.0));

        // Evaluate dR/dtheta_p holding U constant
        std::vector<std::complex<double>> U_base_c(nCells);
        for (int c = 0; c < nCells; ++c) U_base_c[c] = std::complex<double>(U_base[c], 0.0);

        for (int p = 0; p < nParam; ++p) {
            std::vector<std::complex<double>> theta_cs(nParam);
            for (int k = 0; k < nParam; ++k) theta_cs[k] = std::complex<double>(res.theta[k], (k == p) ? h_fd : 0.0);

            // Objective partial derivative
            auto J_c = fvmC.evalObjective(U_base_c, theta_cs);
            dJ_dtheta[p] = J_c.imag() / h_fd;

            // Residual partial derivative
            std::vector<std::complex<double>> R_c;
            fvmC.evalResidual(U_base_c, theta_cs, R_c);
            for (int c = 0; c < nCells; ++c) {
                dR_dtheta[c][p] = R_c[c].imag() / h_fd;
            }
        }

        // Discrete Adjoint Gradient: g_adj = dJ/dtheta - lambda^T * (dR/dtheta)
        for (int p = 0; p < nParam; ++p) {
            double lambda_dot_dR = 0.0;
            for (int c = 0; c < nCells; ++c) {
                lambda_dot_dR += lambda[c] * dR_dtheta[c][p];
            }
            res.g_adj[p] = dJ_dtheta[p] - lambda_dot_dR;
        }

        // 3. Complex-Step Differentiation (Full State & Objective in C):
        // g_cs = Im(J(theta + i*h*e_p)) / h
        res.maxRelError = 0.0;
        for (int p = 0; p < nParam; ++p) {
            std::vector<std::complex<double>> theta_cs(nParam);
            for (int k = 0; k < nParam; ++k) {
                theta_cs[k] = std::complex<double>(res.theta[k], (k == p) ? h_fd : 0.0);
            }
            std::vector<std::complex<double>> U_cs;
            fvmC.solveState(theta_cs, U_cs);
            auto J_cs = fvmC.evalObjective(U_cs, theta_cs);
            res.g_cs[p] = J_cs.imag() / h_fd;

            double diff = std::abs(res.g_adj[p] - res.g_cs[p]);
            double rel = diff / (std::abs(res.g_cs[p]) + 1e-15);
            res.relErrors[p] = rel;
            res.maxRelError = std::max(res.maxRelError, rel);
        }

        res.passedComplexStepGate = (res.maxRelError < 1e-12);

        // 4. Directional Taylor Remainder Test
        // Choose deterministic pseudo-random unit vector v
        std::vector<double> v = {0.35856858, -0.47809144, 0.5976143, -0.23904572, 0.41833001, -0.23904572};
        double vNorm = 0.0;
        for (double vi : v) vNorm += vi * vi;
        vNorm = std::sqrt(vNorm);
        for (double& vi : v) vi /= vNorm;

        double gadj_dot_v = 0.0;
        for (int p = 0; p < nParam; ++p) {
            gadj_dot_v += res.g_adj[p] * v[p];
        }

        res.epsilons = {1e-2, 1e-3, 1e-4, 1e-5, 1e-6, 1e-7};
        for (double eps : res.epsilons) {
            std::vector<double> theta_pert = res.theta;
            for (int p = 0; p < nParam; ++p) {
                theta_pert[p] += eps * v[p];
            }
            std::vector<double> U_pert;
            fvmD.solveState(theta_pert, U_pert);
            double J_pert = fvmD.evalObjective(U_pert, theta_pert);

            // R1(eps) = |J(theta + eps*v) - J(theta) - eps * g_adj . v|
            double R1 = std::abs(J_pert - J_base - eps * gadj_dot_v);
            res.taylorRemainders.push_back(R1);
        }

        // Compute observed slopes: log(R1_k / R1_k+1) / log(eps_k / eps_k+1)
        double slopeSum = 0.0;
        for (size_t k = 0; k < res.epsilons.size() - 1; ++k) {
            double eps1 = res.epsilons[k];
            double eps2 = res.epsilons[k + 1];
            double r1 = res.taylorRemainders[k];
            double r2 = res.taylorRemainders[k + 1];
            double slope = std::log(r1 / r2) / std::log(eps1 / eps2);
            res.observedSlopes.push_back(slope);
            slopeSum += slope;
        }
        res.averageSlope = slopeSum / res.observedSlopes.size();
        res.passedTaylorGate = (std::abs(res.averageSlope - 2.0) < 0.05);

        return res;
    }
};
