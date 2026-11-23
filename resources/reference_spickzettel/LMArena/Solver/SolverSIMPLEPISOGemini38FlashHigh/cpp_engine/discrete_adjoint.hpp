#pragma once
#include "cabi_span.hpp"
#include <vector>
#include <cmath>
#include <array>
#include <numeric>
#include <algorithm>
#include <iostream>
#include <iomanip>
#include <cassert>

namespace hpc::adjoint {

/**
 * @brief Brinkman Darcy Porosity Interpolation (RAMP model).
 * kappa(gamma) = alpha_max * (1 - gamma) / (1 + q * gamma)
 * gamma = 1: fluid (kappa = 0)
 * gamma = 0: solid (kappa = alpha_max)
 */
struct BrinkmanModel {
    double alpha_max{1.0e4}; // Max Darcy drag
    double q{0.1};           // RAMP penalty curvature

    [[nodiscard]] double kappa(double gamma) const noexcept {
        gamma = std::clamp(gamma, 0.0, 1.0);
        return alpha_max * (1.0 - gamma) / (1.0 + q * gamma);
    }

    [[nodiscard]] double dkappa_dgamma(double gamma) const noexcept {
        gamma = std::clamp(gamma, 0.0, 1.0);
        double denom = 1.0 + q * gamma;
        // d/dgamma [ (1 - gamma) / (1 + q * gamma) ] = -(1 + q) / (1 + q * gamma)^2
        return -alpha_max * (1.0 + q) / (denom * denom);
    }
};

/**
 * @brief Discrete 1D/2D Stokes/Navier-Stokes System with Brinkman Friction for Adjoint Verification.
 *
 * Residual equation for cell i:
 *   R_i(u, gamma) = -mu * (u_{i+1} - 2*u_i + u_{i-1})/dx^2 + dp/dx + kappa(gamma_i) * u_i = 0
 *
 * Objective J(u, gamma): Total power dissipation
 *   J = sum_i [ mu * ((u_{i+1} - u_{i-1})/(2*dx))^2 + kappa(gamma_i) * u_i^2 ] * dx
 */
struct ChannelAdjointSystem {
    std::size_t n{32};
    double dx{1.0 / 32.0};
    double mu{0.01};
    double dp_dx{-1.0}; // Pressure gradient driving the flow
    BrinkmanModel brinkman;

    // Solve Primal Equation R(u, gamma) = 0
    std::vector<double> solve_primal(const std::vector<double>& gamma) const {
        std::vector<double> u(n, 0.0);
        // Tridiagonal system A * u = b
        // -mu/dx^2 * u_{i-1} + (2*mu/dx^2 + kappa_i) * u_i - mu/dx^2 * u_{i+1} = -dp/dx
        const double diag_diff = 2.0 * mu / (dx * dx);
        const double off_diff = -mu / (dx * dx);

        std::vector<double> diag(n);
        std::vector<double> rhs(n, -dp_dx);

        for (std::size_t i = 0; i < n; ++i) {
            diag[i] = diag_diff + brinkman.kappa(gamma[i]);
        }

        // Thomas algorithm for tridiagonal solve
        std::vector<double> c_prime(n, 0.0);
        std::vector<double> d_prime(n, 0.0);

        c_prime[0] = off_diff / diag[0];
        d_prime[0] = rhs[0] / diag[0];

        for (std::size_t i = 1; i < n; ++i) {
            double denom = diag[i] - off_diff * c_prime[i - 1];
            if (i < n - 1) {
                c_prime[i] = off_diff / denom;
            }
            d_prime[i] = (rhs[i] - off_diff * d_prime[i - 1]) / denom;
        }

        u[n - 1] = d_prime[n - 1];
        for (int i = static_cast<int>(n) - 2; i >= 0; --i) {
            u[i] = d_prime[i] - c_prime[i] * u[i + 1];
        }

        return u;
    }

    // Evaluate Objective J(u, gamma)
    double evaluate_objective(const std::vector<double>& u, const std::vector<double>& gamma) const {
        double J = 0.0;
        for (std::size_t i = 0; i < n; ++i) {
            double u_prev = (i > 0) ? u[i - 1] : 0.0;
            double u_next = (i + 1 < n) ? u[i + 1] : 0.0;
            double dudx = (u_next - u_prev) / (2.0 * dx);

            // Dissipation = viscous shear dissipation + Darcy drag dissipation
            double diss = mu * dudx * dudx + brinkman.kappa(gamma[i]) * u[i] * u[i];
            J += diss * dx;
        }
        return J;
    }

    // Evaluate dJ/du
    std::vector<double> compute_dJ_du(const std::vector<double>& u, const std::vector<double>& gamma) const {
        std::vector<double> dJ_du(n, 0.0);
        for (std::size_t i = 0; i < n; ++i) {
            // Darcy part
            dJ_du[i] += 2.0 * brinkman.kappa(gamma[i]) * u[i] * dx;

            // Viscous part contribution via chain rule on central difference
            // J_visc = sum_k mu * ((u_{k+1} - u_{k-1})/(2*dx))^2 * dx
            // dJ_visc/du_i = mu/(2*dx) * [ (u_i - u_{i-2})/(2*dx) * (-1) + (u_{i+2} - u_i)/(2*dx) * (+1) ]
            double term = 0.0;
            if (i + 1 < n) {
                double u_kp1 = (i + 2 < n) ? u[i + 2] : 0.0;
                term += -2.0 * mu * ((u_kp1 - u[i]) / (2.0 * dx)) / (2.0 * dx);
            }
            if (i > 0) {
                double u_km1 = (i >= 2) ? u[i - 2] : 0.0;
                term += 2.0 * mu * ((u[i] - u_km1) / (2.0 * dx)) / (2.0 * dx);
            }
            dJ_du[i] += term * dx;
        }
        return dJ_du;
    }

    // Solve Adjoint Linear System: (dR/du)^T * lambda = - (dJ/du)^T
    // Notice dR/du is symmetric here:
    // (dR/du)_{i,i} = 2*mu/dx^2 + kappa_i
    // (dR/du)_{i,i+/-1} = -mu/dx^2
    std::vector<double> solve_adjoint(const std::vector<double>& u, const std::vector<double>& gamma) const {
        std::vector<double> rhs = compute_dJ_du(u, gamma);
        for (double& val : rhs) val = -val; // - dJ/du

        const double diag_diff = 2.0 * mu / (dx * dx);
        const double off_diff = -mu / (dx * dx);

        std::vector<double> diag(n);
        for (std::size_t i = 0; i < n; ++i) {
            diag[i] = diag_diff + brinkman.kappa(gamma[i]);
        }

        // Thomas algorithm for adjoint
        std::vector<double> c_prime(n, 0.0);
        std::vector<double> d_prime(n, 0.0);
        std::vector<double> lambda(n, 0.0);

        c_prime[0] = off_diff / diag[0];
        d_prime[0] = rhs[0] / diag[0];

        for (std::size_t i = 1; i < n; ++i) {
            double denom = diag[i] - off_diff * c_prime[i - 1];
            if (i < n - 1) {
                c_prime[i] = off_diff / denom;
            }
            d_prime[i] = (rhs[i] - off_diff * d_prime[i - 1]) / denom;
        }

        lambda[n - 1] = d_prime[n - 1];
        for (int i = static_cast<int>(n) - 2; i >= 0; --i) {
            lambda[i] = d_prime[i] - c_prime[i] * lambda[i + 1];
        }

        return lambda;
    }

    // Total Sensitivity dJ / d_gamma_i = dJ/dgamma_i + lambda^T * (dR/dgamma_i)
    std::vector<double> compute_adjoint_sensitivity(
        const std::vector<double>& u,
        const std::vector<double>& gamma,
        const std::vector<double>& lambda
    ) const {
        std::vector<double> sens(n, 0.0);
        for (std::size_t i = 0; i < n; ++i) {
            double dk_dg = brinkman.dkappa_dgamma(gamma[i]);

            // Partial derivative dJ / dgamma_i
            double dJ_dgamma = dk_dg * u[i] * u[i] * dx;

            // Transpose product: lambda^T * (dR / dgamma_i)
            // dR_k / dgamma_i = delta_ki * dk_dg * u_i
            double lambda_dR = lambda[i] * (dk_dg * u[i]);

            sens[i] = dJ_dgamma + lambda_dR;
        }
        return sens;
    }

    // CAD Normal & Centroid Sensitivities: dJ / d(S_f), dJ / d(x_f)
    // Feeds the Differentiable CAD DAG chain rule
    struct CadBoundarySensitivity {
        double dJ_dS_normal{0.0};
        double dJ_dx_centroid{0.0};
    };

    CadBoundarySensitivity compute_cad_boundary_sensitivity(
        double face_normal_area,
        double lambda_boundary,
        double u_boundary,
        double p_boundary
    ) const {
        CadBoundarySensitivity s;
        // dR_boundary / dS_f = p_boundary + rho * u_boundary^2
        s.dJ_dS_normal = lambda_boundary * (p_boundary + u_boundary * u_boundary);
        // dR_boundary / dx_f = viscous gradient projection
        s.dJ_dx_centroid = lambda_boundary * (mu * u_boundary / (dx * dx));
        return s;
    }
};

} // namespace hpc::adjoint
