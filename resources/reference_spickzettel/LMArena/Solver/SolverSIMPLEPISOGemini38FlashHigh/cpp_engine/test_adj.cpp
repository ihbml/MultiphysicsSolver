#include "discrete_adjoint.hpp"
#include <iostream>
#include <iomanip>
#include <cmath>

using namespace hpc::adjoint;

int main() {
    ChannelAdjointSystem sys;
    sys.n = 24;
    sys.dx = 1.0 / static_cast<double>(sys.n);
    sys.mu = 0.02;
    sys.dp_dx = -2.0;

    std::vector<double> gamma(sys.n);
    for (std::size_t i = 0; i < sys.n; ++i) {
        gamma[i] = 0.5 + 0.35 * std::sin(2.0 * M_PI * i / sys.n);
    }

    std::vector<double> u = sys.solve_primal(gamma);
    std::vector<double> lambda = sys.solve_adjoint(u, gamma);
    std::vector<double> adj_grad = sys.compute_adjoint_sensitivity(u, gamma, lambda);

    const double eps = 1.0e-7;
    double max_abs_err = 0.0;
    double max_rel_err = 0.0;

    for (std::size_t i = 0; i < sys.n; ++i) {
        std::vector<double> g_p = gamma;
        g_p[i] += eps;
        double J_p = sys.evaluate_objective(sys.solve_primal(g_p), g_p);

        std::vector<double> g_m = gamma;
        g_m[i] -= eps;
        double J_m = sys.evaluate_objective(sys.solve_primal(g_m), g_m);

        double fd = (J_p - J_m) / (2.0 * eps);
        double err = std::abs(adj_grad[i] - fd);
        double rel = err / (std::abs(adj_grad[i]) + 1e-15);

        max_abs_err = std::max(max_abs_err, err);
        max_rel_err = std::max(max_rel_err, rel);
    }

    std::cout << "Max Abs Error: " << max_abs_err << std::endl;
    std::cout << "Max Rel Error: " << max_rel_err << std::endl;
    return (max_abs_err < 1e-8) ? 0 : 1;
}
