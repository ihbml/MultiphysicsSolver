#include "flux_reference.hpp"
#include <cmath>
#include <iostream>
#include <stdexcept>
using namespace continuum;

void close(double actual, double expected) {
    if (std::abs(actual-expected) > 1e-11*(1+std::abs(expected)))
        throw std::runtime_error("Flux reference test failed");
}
int main() {
    const Vec3 p{0,0,0}, n{1,0.3,0.1};
    const Vec3 f{0.45,0.24,0.12}, area{1,0.1,0.2};
    const auto g = geometry(p, n, f, area, 1.0);
    if (g.kind != FaceClass::corrected) return 1;

    // A constant scalar has zero diffusive flux.
    close(central_flux(g, 0, 0.7, 3, 3, {0,0,0}), 0);

    // A linear field must be exact even on this skewed face.
    const Vec3 grad{2,-0.5,0.3};
    const auto phi = [&](Vec3 x) { return 4+dot(grad,x); };
    const double m = 0.2, gamma = 0.7;
    const double flux = central_flux(g,m,gamma,phi(p),phi(n),grad);
    close(flux, m*phi(f)-gamma*dot(grad,area));

    // Owner and neighbour contributions cancel face by face.
    const auto a = coefficients(g,m,gamma,false);
    close(a.pp+a.np,0); close(a.pn+a.nn,0);
    close(flux + (-flux), 0);

    const auto up = coefficients(g,100,gamma,true);
    if (up.pn > 0 || up.np > 0) return 2;
    const auto orth = geometry(p,{1,0,0},{0.5,0,0},{1,0,0},1);
    if (orth.kind != FaceClass::fast) return 3;
    if (geometry(p,n,f,area*(-1),1).kind != FaceClass::invalid) return 4;
    std::cout << "PASS: constant, linear, conservation, upwind, geometry\n";
}