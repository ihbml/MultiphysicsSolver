#pragma once

#if defined(__CUDACC__)
#include <cuda_runtime.h>

extern "C" __global__ void clusterA_gather_cell_flux(
    int cellCount, const int* cellFaceOffsets, const int* faceIds,
    const signed char* faceSigns, const double* faceFlux, double* cellFlux);

extern "C" __global__ void clusterA_sst_closure(
    int cellCount, const double* rho, const double* nu, const double* k,
    const double* omega, const double* wallDistance, const double* gradKDotGradOmega,
    const double* strainRate, const double* rawProduction, double* f1, double* f2,
    double* turbulentViscosity, double* limitedProduction);

extern "C" __global__ void clusterA_sst_patankar_sources(
    int cellCount, const double* rho, const double* k, const double* omega,
    const double* volume, const double* f1, const double* turbulentViscosity,
    const double* gradKDotGradOmega, const double* limitedProduction,
    double* kDiagonal, double* kRhs, double* omegaDiagonal, double* omegaRhs);

extern "C" __global__ void clusterA_sa_closure(
    int cellCount, const double* nuTilde, const double* nu,
    const double* wallDistance, const double* vorticityMagnitude,
    const double* gradientMagnitudeSquared, double* fv1, double* fv2,
    double* modifiedVorticity, double* fw, double* effectiveDiffusion,
    double* production, double* destruction, double* gradientSource);

extern "C" __global__ void clusterA_wale_viscosity(
    int cellCount, const double* velocityGradient, const double* filterWidth,
    double* subgridViscosity);

extern "C" __global__ void clusterA_all_y_plus_wall(
    int faceCount, const double* rho, const double* nu, const double* tangentialSpeed,
    const double* wallDistance, const double* prandtl, const double* cp,
    const double* cellTemperature, const double* wallTemperature, double* yPlus,
    double* frictionVelocity, double* omegaWall, double* wallShear,
    double* wallHeatFlux);

extern "C" __global__ void clusterA_cht_flux(
    int faceCount, const double* conductivityF, const double* alphaF,
    const double* conductivityS, const double* alphaS, const double* temperatureP,
    const double* temperatureS, const double* gradTfDotKcorr,
    const double* gradTsDotKcorr, double* fluxDensity);

extern "C" __global__ void clusterA_porous_momentum(
    int cellCount, const double* darcyTensor, const double* forchheimerTensor,
    const double* velocity, const double* dynamicViscosity, const double* density,
    double* momentumSink, double* implicitJacobian);

extern "C" __global__ void clusterA_p1_source(
    int cellCount, const double* diffusionCoefficient, const double* absorption,
    const double* refractiveIndex, const double* temperature,
    double* diffusionOut, double* reactionOut, double* sourceOut);

extern "C" __global__ void clusterA_p1_marshak(
    int faceCount, const double* cellIncidentRadiation, const double* wallTemperature,
    const double* emissivity, const double* diffusionCoefficient,
    const double* wallDistance, double* faceIncidentRadiation, double* outwardFlux,
    double* diagonal, double* rhs);
#endif