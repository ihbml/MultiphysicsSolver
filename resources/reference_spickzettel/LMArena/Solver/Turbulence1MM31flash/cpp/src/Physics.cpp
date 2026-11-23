// =====================================================================================
//  mt::Physics.cpp -- CHT, anisotropic porous media and thermal radiation.
// =====================================================================================
#include "mt/Physics.hpp"

#include "mt/Sparse.hpp"

#include <algorithm>
#include <cmath>

namespace mt {

// =====================================================================================
//  CHT
// =====================================================================================
void CHTCoupling::addConformal(int fluidFace, int solidFace) {
    conformal_.emplace_back(fluidFace, solidFace);
}

void CHTCoupling::addNonConformal(int fluidFace, std::vector<int> solidFaces, std::vector<double> areas) {
    NonConformalPair p;
    p.fluidFace = fluidFace;
    p.solidFaces = std::move(solidFaces);
    p.overlapAreas = std::move(areas);
    nonConformal_.push_back(std::move(p));
}

void CHTCoupling::clear() {
    conformal_.clear();
    nonConformal_.clear();
}

// -------------------------------------------------------------------------------------
//  Harmonic-resistance interface evaluation
//
//     G_f = kappa_f * alpha_f ,  G_s = kappa_s * alpha_s        (per-unit-area admittances)
//     H   = G_f G_s / (G_f + G_s)                                (harmonic conductance)
//     q_{f->s} = H [ (T_S - T_P) + (gradT_f . k_f^corr)/alpha_f
//                                    + (gradT_s . k_s^corr)/alpha_s ]
//     T_interface = ( G_f T_P + G_s T_S + Delta q_skew ) / (G_f + G_s)
//
//  alpha = 1/d is the inverse conduction distance between the cell centre and the
//  interface, so that G = kappa/d is the classical per-unit-area conductance.
//  k^corr = unit(x_face - x_cell) - n_face is the (dimensionless) skew vector: it
//  vanishes for a conformal orthogonal interface and measures the non-orthogonality
//  otherwise.
// -------------------------------------------------------------------------------------
CHTInterfaceState CHTCoupling::evaluate(const Mesh& mesh,
                                        int fluidFace,
                                        const std::vector<int>& solidFaces,
                                        const std::vector<double>& areas,
                                        const std::vector<double>& T,
                                        const std::vector<Vec3>& gradT,
                                        const CHTRegionProperties& fluid,
                                        const CHTRegionProperties& solid) const {
    CHTInterfaceState st;
    const int P = mesh.faces.owner[static_cast<std::size_t>(fluidFace)];
    const Vec3& Sf = mesh.faces.areaVector[static_cast<std::size_t>(fluidFace)];
    const Vec3& xf = mesh.faces.faceCentre[static_cast<std::size_t>(fluidFace)];
    const double Af = mesh.faces.area[static_cast<std::size_t>(fluidFace)];
    const Vec3 nf = mesh.faces.unitNormal[static_cast<std::size_t>(fluidFace)];

    // ---- fluid side -------------------------------------------------------------
    const Vec3& xP = mesh.cellCentre[static_cast<std::size_t>(P)];
    const double df = std::max(std::abs(dot(xP - xf, nf)), 1e-12);
    const double alphaF = 1.0 / df;
    const double Gf = fluid.conductivity * alphaF;
    const Vec3 kfCorr = unit(xf - xP) - nf;

    double fluxAreaSum = 0.0;   // sum over overlaps of A_j * q_j
    double hAreaSum = 0.0;      // sum of A_j * H_j
    double gT = 0.0;            // area weighted (G_f T_P + G_s T_S + skew)
    double gSum = 0.0;          // area weighted (G_f + G_s)
    double skewArea = 0.0;

    const std::size_t n = solidFaces.size();
    for (std::size_t j = 0; j < n; ++j) {
        const int sf = solidFaces[j];
        const double Aj = (j < areas.size()) ? areas[j] : mesh.faces.area[static_cast<std::size_t>(sf)];
        const int S = mesh.faces.owner[static_cast<std::size_t>(sf)];
        // the twin solid face carries the opposite area vector: S_s = -S_f
        const Vec3& xfs = mesh.faces.faceCentre[static_cast<std::size_t>(sf)];
        const Vec3 ns = -nf;
        const Vec3& xS = mesh.cellCentre[static_cast<std::size_t>(S)];
        const double ds = std::max(std::abs(dot(xS - xfs, ns)), 1e-12);
        const double alphaS = 1.0 / ds;
        const double Gs = solid.conductivity * alphaS;
        const double H = (Gf + Gs) > kSmall ? Gf * Gs / (Gf + Gs) : 0.0;

        const Vec3 ksCorr = unit(xfs - xS) - ns;
        const double skew = dot(gradT[static_cast<std::size_t>(P)], kfCorr) * alphaF
                          + dot(gradT[static_cast<std::size_t>(S)], ksCorr) * alphaS;

        const double q = H * ((T[static_cast<std::size_t>(S)] - T[static_cast<std::size_t>(P)]) + skew);

        fluxAreaSum += Aj * q;
        hAreaSum += Aj * H;
        gT += Aj * (Gf * T[static_cast<std::size_t>(P)] + Gs * T[static_cast<std::size_t>(S)] + skew);
        gSum += Aj * (Gf + Gs);
        skewArea += Aj * skew;
    }

    if (Af > kSmall) {
        st.heatFlux = fluxAreaSum / Af;
        st.conductance = hAreaSum / Af;
    }
    st.skewCorrection = (n > 0) ? skewArea / static_cast<double>(n) : 0.0;
    st.interfaceTemperature = (gSum > kSmall) ? gT / gSum : T[static_cast<std::size_t>(P)];
    return st;
}

std::vector<CHTCouplingTerm> CHTCoupling::monolithicTerms(const Mesh& mesh,
                                                           const std::vector<double>& T,
                                                           const std::vector<Vec3>& gradT,
                                                           const std::vector<CHTRegionProperties>& regions,
                                                           int fluidRegion,
                                                           int solidRegion) const {
    std::vector<CHTCouplingTerm> terms;
    const CHTRegionProperties& fluid = regions[static_cast<std::size_t>(fluidRegion)];
    const CHTRegionProperties& solid = regions[static_cast<std::size_t>(solidRegion)];

    auto addTerm = [&](int fluidFace, int solidFace, double area) {
        const int P = mesh.faces.owner[static_cast<std::size_t>(fluidFace)];
        const int S = mesh.faces.owner[static_cast<std::size_t>(solidFace)];
        const Vec3& Sf = mesh.faces.areaVector[static_cast<std::size_t>(fluidFace)];
        const Vec3& xf = mesh.faces.faceCentre[static_cast<std::size_t>(fluidFace)];
        const Vec3 nf = mesh.faces.unitNormal[static_cast<std::size_t>(fluidFace)];
        const Vec3& xP = mesh.cellCentre[static_cast<std::size_t>(P)];
        const double df = std::max(std::abs(dot(xP - xf, nf)), 1e-12);
        const double alphaF = 1.0 / df;
        const double Gf = fluid.conductivity * alphaF;

        const Vec3& xfs = mesh.faces.faceCentre[static_cast<std::size_t>(solidFace)];
        const Vec3 ns = -nf;
        const Vec3& xS = mesh.cellCentre[static_cast<std::size_t>(S)];
        const double ds = std::max(dot(xS - xfs, ns), 1e-12);
        const double alphaS = 1.0 / ds;
        const double Gs = solid.conductivity * alphaS;
        const double H = (Gf + Gs) > kSmall ? Gf * Gs / (Gf + Gs) : 0.0;

        const Vec3 kfCorr = unit(xf - xP) - nf;
        const Vec3 ksCorr = unit(xfs - xS) - ns;
        const double skew = dot(gradT[static_cast<std::size_t>(P)], kfCorr) * alphaF
                          + dot(gradT[static_cast<std::size_t>(S)], ksCorr) * alphaS;

        CHTCouplingTerm t;
        t.fluidCell = P;
        t.solidCell = S;
        t.area = area;
        t.conductance = H;
        t.skewSource = H * area * skew;
        terms.push_back(t);
    };

    for (const auto& pr : conformal_) addTerm(pr.first, pr.second, mesh.faces.area[static_cast<std::size_t>(pr.first)]);
    for (const auto& nc : nonConformal_) {
        for (std::size_t j = 0; j < nc.solidFaces.size(); ++j) {
            const double a = (j < nc.overlapAreas.size()) ? nc.overlapAreas[j]
                                                          : mesh.faces.area[static_cast<std::size_t>(nc.solidFaces[j])];
            addTerm(nc.fluidFace, nc.solidFaces[j], a);
        }
    }
    return terms;
}

void CHTCoupling::relaxPartitioned(double lambda, std::vector<double>& interfaceTemperature) const {
    for (double& v : interfaceTemperature) v = (1.0 - lambda) * v + lambda * v;
}

int CHTCoupling::pairIndexOfFace(int face) const {
    for (std::size_t i = 0; i < conformal_.size(); ++i)
        if (conformal_[i].first == face || conformal_[i].second == face) return static_cast<int>(i);
    const int offset = static_cast<int>(conformal_.size());
    for (std::size_t i = 0; i < nonConformal_.size(); ++i) {
        if (nonConformal_[i].fluidFace == face) return offset + static_cast<int>(i);
        for (int sf : nonConformal_[i].solidFaces) if (sf == face) return offset + static_cast<int>(i);
    }
    return -1;
}

// =====================================================================================
//  Anisotropic porous media
// =====================================================================================
Mat3 PorousZone::anisotropy() const {
    Mat3 R;
    R(0, 0) = axis1.x; R(0, 1) = axis2.x; R(0, 2) = axis3.x;
    R(1, 0) = axis1.y; R(1, 1) = axis2.y; R(1, 2) = axis3.y;
    R(2, 0) = axis1.z; R(2, 1) = axis2.z; R(2, 2) = axis3.z;
    const Mat3 D = Mat3::diagonal(Vec3{d1, d2, d3});
    return R * D * R.transpose();
}

Mat3 PorousZone::inversePermeability() const {
    const Mat3 A = anisotropy();
    Mat3 D;
    for (int i = 0; i < 3; ++i)
        for (int j = 0; j < 3; ++j) D(i, j) = A(i, j) / std::max(permeability, 1e-30);
    return D;
}

void PorousModule::addZone(const PorousZone& zone) {
    zones_.push_back(zone);
    const int zid = static_cast<int>(zones_.size()) - 1;
    int maxCell = -1;
    for (int c : zone.cells) maxCell = std::max(maxCell, c);
    if (maxCell + 1 > static_cast<int>(zoneOfCell_.size()))
        zoneOfCell_.resize(static_cast<std::size_t>(maxCell) + 1, -1);
    for (int c : zone.cells)
        if (c >= 0 && c < static_cast<int>(zoneOfCell_.size())) zoneOfCell_[static_cast<std::size_t>(c)] = zid;
}

Vec3 PorousModule::momentumSink(int cell, const Vec3& U, double rho, double mu) const {
    const int z = zoneOfCell_[static_cast<std::size_t>(cell)];
    if (z < 0) return Vec3{0, 0, 0};
    const PorousZone& zone = zones_[static_cast<std::size_t>(z)];
    const Mat3 D = zone.inversePermeability();
    const double uMag = mag(U);
    const Vec3 darcy = mu * (D * U);                       // mu D u            [N/m^3]
    const Vec3 inertial = 0.5 * rho * uMag * zone.forchheimer * U;  // 0.5 rho |u| F u
    return -(darcy + inertial);
}

Mat3 PorousModule::implicitSinkMatrix(int cell, const Vec3& U, double rho, double mu) const {
    const int z = zoneOfCell_[static_cast<std::size_t>(cell)];
    if (z < 0) return Mat3::zero();
    const PorousZone& zone = zones_[static_cast<std::size_t>(z)];
    Mat3 D = zone.inversePermeability();
    const double uMag = mag(U);
    const double inertial = 0.5 * rho * uMag * zone.forchheimer;
    for (int i = 0; i < 3; ++i) D(i, i) += inertial;
    return D * mu;
}

// =====================================================================================
//  Radiation
// =====================================================================================
std::vector<Vec3> RadiationModule::directions(int order) {
    std::vector<Vec3> dirs;
    if (order <= 2) {
        dirs.push_back(Vec3{1.0, 0.0, 0.0});
        dirs.push_back(Vec3{-1.0, 0.0, 0.0});
        return dirs;
    }
    const int nMu = order / 2;          // polar
    const int nPhi = 2;                 // azimuthal
    for (int ip = 0; ip < nMu; ++ip) {
        const double mu = -1.0 + 2.0 * (ip + 0.5) / nMu;   // uniform in mu
        const double s = std::sqrt(std::max(0.0, 1.0 - mu * mu));
        for (int ia = 0; ia < nPhi; ++ia) {
            const double phi = kTwoPi * ia / nPhi;
            dirs.push_back(Vec3{s * std::cos(phi), s * std::sin(phi), mu});
        }
    }
    return dirs;
}

std::vector<double> RadiationModule::weights(int order) {
    const std::size_t n = directions(order).size();
    std::vector<double> w(n, kFourPi / static_cast<double>(n));
    return w;
}

std::vector<double> RadiationModule::solveP1(const Mesh& mesh,
                                              const std::vector<double>& T,
                                              const RadiationSettings& settings,
                                              const std::vector<double>* wallTemperature) const {
    const int n = mesh.nCells;
    const double ka = std::max(settings.absorptionCoefficient, 0.0);
    const double ks = std::max(settings.scatteringCoefficient, 0.0);
    const double a = ka + ks;
    const double gamma = (a > kSmall) ? 1.0 / (3.0 * a) : 0.0;   // Rosseland diffusion limit

    CsrMatrix A;
    A.resize(n, n);
    std::vector<double> b(static_cast<std::size_t>(n), 0.0);

    for (int c = 0; c < n; ++c) {
        const double V = mesh.cellVolume[static_cast<std::size_t>(c)];
        double diag = a * V;
        b[static_cast<std::size_t>(c)] += 4.0 * a * kPi * kStefanBoltzmann
                                        * std::pow(T[static_cast<std::size_t>(c)], 4.0) * V;
        const int e0 = mesh.cellFaces.begin(c), e1 = mesh.cellFaces.end(c);
        for (int e = e0; e < e1; ++e) {
            const int f = mesh.cellFaces.faceIndex[static_cast<std::size_t>(e)];
            const int nb = mesh.faces.neighbour[static_cast<std::size_t>(f)];
            const double Af = mesh.faces.area[static_cast<std::size_t>(f)];
            const double d = mesh.faces.distance[static_cast<std::size_t>(f)];
            const double af = gamma * Af / std::max(d, 1e-12);
            if (nb >= 0) {
                diag += af;
                A.addEntry(c, nb, -af);
                A.addEntry(nb, c, -af);
            } else {
                const int bc = mesh.faces.bcId[static_cast<std::size_t>(f)];
                if (bc < 0) continue;
                const BoundaryCondition& bcv = mesh.bcs[static_cast<std::size_t>(bc)];
                if (bcv.type != BCType::Wall) continue;
                const double Tw = wallTemperature ? (*wallTemperature)[static_cast<std::size_t>(f)]
                                                  : settings.wallTemperature;
                const double eps = clampd(settings.wallEmissivity, 0.0, 1.0);
                // Marshak boundary condition:  G_w = eps I_b(T_w) / (1 + eps)
                const double Gw = eps * kPi * kStefanBoltzmann * std::pow(Tw, 4.0) / (1.0 + eps);
                diag += af;
                b[static_cast<std::size_t>(c)] += af * Gw;
            }
        }
        A.addEntry(c, c, diag);
    }
    A.compress();
    std::vector<double> G(static_cast<std::size_t>(n), 0.0);
    biCGStab(A, b, G, 1.0e-12, 4000, true);
    return G;
}

DOMResult RadiationModule::solveDOM(const Mesh& mesh,
                                    const std::vector<double>& T,
                                    const RadiationSettings& settings,
                                    const std::vector<double>* wallTemperature) const {
    DOMResult res;
    const int n = mesh.nCells;
    const double ka = std::max(settings.absorptionCoefficient, 0.0);
    const double ks = std::max(settings.scatteringCoefficient, 0.0);
    const int nDir = std::max(settings.quadratureOrder, 2);

    res.directions = directions(nDir);
    res.weights = weights(nDir);
    if (settings.axis == 1) {   // rotate the ordinate set onto the y axis
        for (auto& d : res.directions) std::swap(d.x, d.y);
    }
    const std::size_t nd = res.directions.size();
    res.intensity.assign(nd, std::vector<double>(static_cast<std::size_t>(n), 0.0));

    // initialise with the blackbody intensity of the local gas temperature
    for (std::size_t m = 0; m < nd; ++m)
        for (int c = 0; c < n; ++c)
            res.intensity[m][static_cast<std::size_t>(c)] = kPi * kStefanBoltzmann
                                                          * std::pow(T[static_cast<std::size_t>(c)], 4.0);

    // wall intensities (per face) for the black/grey wall condition
    std::vector<double> wallI(static_cast<std::size_t>(mesh.nFaces()), 0.0);
    for (int f = 0; f < mesh.nFaces(); ++f) {
        const int bc = mesh.faces.bcId[static_cast<std::size_t>(f)];
        if (bc < 0) continue;
        const BoundaryCondition& bcv = mesh.bcs[static_cast<std::size_t>(bc)];
        if (bcv.type != BCType::Wall) continue;
        const double Tw = wallTemperature ? (*wallTemperature)[static_cast<std::size_t>(f)] : settings.wallTemperature;
        wallI[static_cast<std::size_t>(f)] = kPi * kStefanBoltzmann * std::pow(Tw, 4.0);
    }

    // ---- solve the transport equation for every direction -------------------------
    for (std::size_t m = 0; m < nd; ++m) {
        const Vec3& Om = res.directions[m];
        CsrMatrix A;
        A.resize(n, n);
        std::vector<double> b(static_cast<std::size_t>(n), 0.0);
        for (int c = 0; c < n; ++c) {
            const double V = mesh.cellVolume[static_cast<std::size_t>(c)];
            const double Ib = kPi * kStefanBoltzmann * std::pow(T[static_cast<std::size_t>(c)], 4.0);
            // isotropic scattering source:  (ks/4pi) sum_m' w_m' (Om.Om') (I_m' - I_m)
            double src = ka * Ib;
            double sink = 0.0;
            for (std::size_t mp = 0; mp < nd; ++mp) {
                if (mp == m) continue;
                const double coss = dot(Om, res.directions[mp]);
                src += (ks / kFourPi) * res.weights[mp] * coss * res.intensity[mp][static_cast<std::size_t>(c)];
                sink += (ks / kFourPi) * res.weights[mp] * res.intensity[mp][static_cast<std::size_t>(c)];
            }
            double diag = (ka + ks) * V + (src - sink) * V;
            double rhs = (src - sink) * V;
            const int e0 = mesh.cellFaces.begin(c), e1 = mesh.cellFaces.end(c);
            std::vector<std::pair<int, double>> off;
            for (int e = e0; e < e1; ++e) {
                const int f = mesh.cellFaces.faceIndex[static_cast<std::size_t>(e)];
                const int nb = mesh.faces.neighbour[static_cast<std::size_t>(f)];
                const Vec3& n = mesh.faces.unitNormal[static_cast<std::size_t>(f)];
                const double F = dot(Om, n) * mesh.faces.area[static_cast<std::size_t>(f)];
                if (nb >= 0) {
                    if (F > 0.0) diag += F;
                    else off.emplace_back(nb, F);
                } else {
                    const int bc = mesh.faces.bcId[static_cast<std::size_t>(f)];
                    if (bc < 0) continue;
                    const BoundaryCondition& bcv = mesh.bcs[static_cast<std::size_t>(bc)];
                    if (bcv.type != BCType::Wall) continue;
                    double Iwall = wallI[static_cast<std::size_t>(f)];
                    const double eps = clampd(settings.wallEmissivity, 0.0, 1.0);
                    if (eps < 1.0) {
                        double incident = 0.0;
                        const int owner = mesh.faces.owner[static_cast<std::size_t>(f)];
                        for (std::size_t mp = 0; mp < nd; ++mp)
                            if (dot(res.directions[mp], n) < 0.0)
                                incident += res.weights[mp] * res.intensity[mp][static_cast<std::size_t>(owner)] / kFourPi;
                        Iwall = (1.0 - eps) * wallI[static_cast<std::size_t>(f)] + eps * incident;
                    }
                    if (F > 0.0) diag += F;
                    rhs -= F * Iwall;
                }
            }
            A.addEntry(c, c, diag);
            for (const auto& o : off) A.addEntry(c, o.first, o.second);
            b[static_cast<std::size_t>(c)] = rhs;
        }
        A.compress();
        std::vector<double> Im(static_cast<std::size_t>(n), 0.0);
        biCGStab(A, b, Im, 1.0e-12, 4000, true);
        res.intensity[m] = Im;
    }

    // ---- hemispherical total -------------------------------------------------------
    res.G.assign(static_cast<std::size_t>(n), 0.0);
    for (std::size_t m = 0; m < nd; ++m)
        for (int c = 0; c < n; ++c)
            res.G[static_cast<std::size_t>(c)] += res.weights[m] * res.intensity[m][static_cast<std::size_t>(c)];

    // ---- net radiative flux through the slab ends ---------------------------------
    for (int f = 0; f < mesh.nFaces(); ++f) {
        const int bc = mesh.faces.bcId[static_cast<std::size_t>(f)];
        if (bc < 0) continue;
        const BoundaryCondition& bcv = mesh.bcs[static_cast<std::size_t>(bc)];
        if (bcv.type != BCType::Wall) continue;
        const Vec3& n = mesh.faces.unitNormal[static_cast<std::size_t>(f)];
        const int owner = mesh.faces.owner[static_cast<std::size_t>(f)];
        double q = 0.0;
        for (std::size_t m = 0; m < nd; ++m) {
            const double mu = dot(res.directions[m], n);
            if (mu > 0.0) q += res.weights[m] * res.intensity[m][static_cast<std::size_t>(owner)] * mu;
        }
        if (n.y < 0.0) res.netFluxLeft += q * mesh.faces.area[static_cast<std::size_t>(f)];
        else res.netFluxRight += q * mesh.faces.area[static_cast<std::size_t>(f)];
    }
    return res;
}

} // namespace mt
