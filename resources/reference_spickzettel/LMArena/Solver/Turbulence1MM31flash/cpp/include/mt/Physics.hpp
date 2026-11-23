// =====================================================================================
//  mt::Physics.hpp -- Cluster A physics modules
//
//    * CHT   : multi-region conjugate heat transfer over conformal and non-conformal
//              (Supermesh-AMI) twin faces with an exact harmonic resistance.
//    * POROUS: anisotropic Darcy-Forchheimer momentum sinks in a rotated local frame.
//    * RADIATION: P1 (diffusion) and fvDOM (discrete ordinates) thermal radiation.
//
//  Language standard : C++20
// =====================================================================================
#pragma once

#include "mt/Mesh.hpp"
#include "mt/Types.hpp"

#include <map>
#include <string>
#include <vector>

namespace mt {

// =====================================================================================
//  Conjugate Heat Transfer
// =====================================================================================
struct CHTRegionProperties {
    int         id = 0;
    std::string name = "region";
    double      conductivity = 50.0;    //!< k  [W/(m K)]
    double      density = 7800.0;       //!< rho [kg/m^3]
    double      specificHeat = 500.0;   //!< cp [J/(kg K)]
    double alpha() const { return conductivity / std::max(density * specificHeat, 1e-30); }
};

struct CHTInterfaceState {
    double heatFlux = 0.0;             //!< q_{f->s} [W/m^2] (positive = into the fluid)
    double interfaceTemperature = 0.0; //!< T_interface [K]
    double conductance = 0.0;          //!< harmonic interface conductance H [W/(m^2 K)]
    double skewCorrection = 0.0;       //!< Delta q_skew [K]
};

//! One coupling term of the monolithic cross-region energy matrix
struct CHTCouplingTerm {
    int    fluidCell = -1;
    int    solidCell = -1;
    double area = 0.0;          //!< overlap area [m^2]
    double conductance = 0.0;    //!< H [W/(m^2 K)]
    double skewSource = 0.0;    //!< H * area * Delta q_skew [W]
};

class CHTCoupling {
public:
    struct NonConformalPair {
        int                     fluidFace = -1;
        std::vector<int>        solidFaces;
        std::vector<double>     overlapAreas;
    };

    void addConformal(int fluidFace, int solidFace);
    void addNonConformal(int fluidFace, std::vector<int> solidFaces, std::vector<double> areas);
    void clear();

    const std::vector<std::pair<int, int>>& conformal() const { return conformal_; }
    const std::vector<NonConformalPair>&     nonConformal() const { return nonConformal_; }
    int size() const { return static_cast<int>(conformal_.size() + nonConformal_.size()); }

    //!< Evaluate the interface state of one fluid face against a set of solid faces.
    CHTInterfaceState evaluate(const Mesh& mesh,
                               int fluidFace,
                               const std::vector<int>& solidFaces,
                               const std::vector<double>& areas,
                               const std::vector<double>& T,
                               const std::vector<Vec3>& gradT,
                               const CHTRegionProperties& fluid,
                               const CHTRegionProperties& solid) const;

    //!< Monolithic cross-region coupling terms for the unified energy matrix.
    std::vector<CHTCouplingTerm> monolithicTerms(const Mesh& mesh,
                                                 const std::vector<double>& T,
                                                 const std::vector<Vec3>& gradT,
                                                 const std::vector<CHTRegionProperties>& regions,
                                                 int fluidRegion,
                                                 int solidRegion) const;

    //!< Partitioned (Robin-Robin) under-relaxed interface temperature update.
    void relaxPartitioned(double lambda, std::vector<double>& interfaceTemperature) const;

    //!< index of the coupling pair that owns a face (-1 if the face is not a twin)
    int pairIndexOfFace(int face) const;

    double partitionRelaxation = 0.6;

private:
    std::vector<std::pair<int, int>> conformal_;
    std::vector<NonConformalPair>     nonConformal_;
};

// =====================================================================================
//  Anisotropic porous media (Darcy-Forchheimer)
// =====================================================================================
struct PorousZone {
    int         id = 0;
    std::string name = "porous";
    std::vector<int> cells;
    double permeability = 1.0e-9;      //!< K [m^2] (reference value)
    double forchheimer = 1.0e3;        //!< F [1/m] inertial coefficient
    double porosity = 0.4;
    //!< anisotropy ratios d_i = K_i / K (dimensionless)
    double d1 = 1.0, d2 = 1.0, d3 = 1.0;
    //!< local frame (columns of R)
    Vec3   axis1{1.0, 0.0, 0.0};
    Vec3   axis2{0.0, 1.0, 0.0};
    Vec3   axis3{0.0, 0.0, 1.0};

    //!< D_global = R diag(d1,d2,d3) R^T
    Mat3 anisotropy() const;
    //!< inverse permeability tensor D/K [1/m^2]
    Mat3 inversePermeability() const;
};

class PorousModule {
public:
    void setCellCount(int n) { zoneOfCell_.assign(static_cast<std::size_t>(n), -1); }
    void addZone(const PorousZone& zone);
    int  zoneOfCell(int cell) const { return zoneOfCell_[static_cast<std::size_t>(cell)]; }
    bool isPorous(int cell) const { return zoneOfCell_[static_cast<std::size_t>(cell)] >= 0; }
    const std::vector<PorousZone>& zones() const { return zones_; }

    //!< Darcy-Forchheimer sink  mu D u + 0.5 rho |u| F u  [N/m^3]
    Vec3 momentumSink(int cell, const Vec3& U, double rho, double mu) const;
    //!< Diagonalised implicit form: mu D/K + 0.5 rho |u| F I  [kg/(m^3 s)]
    Mat3 implicitSinkMatrix(int cell, const Vec3& U, double rho, double mu) const;

private:
    std::vector<PorousZone> zones_;
    std::vector<int>        zoneOfCell_;
};

// =====================================================================================
//  Thermal radiation : P1 and fvDOM
// =====================================================================================
struct RadiationSettings {
    double absorptionCoefficient = 0.0;  //!< kappa_a [1/m]
    double scatteringCoefficient = 0.0;  //!< kappa_s [1/m]
    double wallEmissivity = 1.0;         //!< epsilon [-]
    double wallTemperature = 1000.0;      //!< T_w [K] (used when the BC has no datum)
    int    quadratureOrder = 2;           //!< number of discrete ordinates in the slab test
    int    axis = 1;                      //!< slab axis (0 = x, 1 = y) for the ordinate set
    bool   p1 = true;                     //!< solve the P1 diffusion equation
    bool   dom = true;                    //!< solve the fvDOM transport equation
};

struct DOMResult {
    std::vector<Vec3>              directions;   //!< unit vectors
    std::vector<double>            weights;      //!< solid angle weights [sr]
    std::vector<std::vector<double>> intensity;   //!< per direction intensity field
    std::vector<double>            G;            //!< hemispherical total [W/m^2/sr]
    double                         netFluxLeft = 0.0;
    double                         netFluxRight = 0.0;
};

class RadiationModule {
public:
    //!< P1 elliptic equation:  -div(Gamma grad G) + a G = 4 a n^2 sigma T^4
    //!< with Marshak wall boundary conditions.
    std::vector<double> solveP1(const Mesh& mesh,
                                const std::vector<double>& T,
                                const RadiationSettings& settings,
                                const std::vector<double>* wallTemperature = nullptr) const;

    //!< fvDOM (finite-volume discrete ordinates) radiative transfer.
    DOMResult solveDOM(const Mesh& mesh,
                       const std::vector<double>& T,
                       const RadiationSettings& settings,
                       const std::vector<double>* wallTemperature = nullptr) const;

    //!< Angular quadrature (Gauss-Legendre in mu, uniform in phi).
    static std::vector<Vec3>    directions(int order);
    static std::vector<double>  weights(int order);

    static double blackbodyIntensity(double T) { return kPi * kStefanBoltzmann * T * T * T * T; }
};

} // namespace mt
