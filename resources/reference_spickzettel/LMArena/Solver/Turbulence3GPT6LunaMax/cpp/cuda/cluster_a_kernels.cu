#include "cluster_a/cuda_kernels.cuh"

#include <math.h>

namespace {

__device__ __forceinline__ double positive(double value) {
  return fmax(value, 1.0e-30);
}

__device__ __forceinline__ double reichardtDevice(double yPlus) {
  const double yp = fmax(yPlus, 0.0);
  if (yp < 1.0e-5) return yp;
  constexpr double kappa = 0.41;
  return fmax(log1p(kappa * yp) / kappa +
              7.8 * (1.0 - exp(-yp / 11.0) - (yp / 11.0) * exp(-yp / 3.0)), 0.0);
}

__device__ __forceinline__ double kaderDevice(double yPlus, double prandtl) {
  const double yp = fmax(yPlus, 0.0);
  const double pr = positive(prandtl);
  const double gamma = 0.01 * pow(pr * yp, 4.0) /
                       positive(1.0 + 5.0 * pow(pr, 3.0) * yp);
  const double beta = pow(3.85 * pow(pr, 1.0 / 3.0) - 1.3, 2.0) + 2.12 * log(pr);
  const double logWeight = gamma > 1.0e-12 ? exp(-1.0 / gamma) : 0.0;
  return positive(pr * yp * exp(-gamma) +
                  (2.12 * log1p(yp) + beta) * logWeight);
}

__device__ __forceinline__ double frictionVelocityDevice(double speed, double y, double nu) {
  const double u = fmax(speed, 0.0);
  if (u <= 1.0e-30) return 0.0;
  const double distance = positive(y);
  const double molecularNu = positive(nu);
  double low = 0.0;
  double high = fmax(fmax(u, sqrt(u * molecularNu / distance)), 1.0e-8);
  for (int i = 0; i < 64; ++i) {
    const double yp = high * distance / molecularNu;
    if (high * reichardtDevice(yp) >= u) break;
    high *= 2.0;
  }
  for (int i = 0; i < 72; ++i) {
    const double mid = 0.5 * (low + high);
    if (mid * reichardtDevice(mid * distance / molecularNu) > u) high = mid;
    else low = mid;
  }
  return 0.5 * (low + high);
}

}  // namespace

extern "C" __global__ void clusterA_gather_cell_flux(
    int cellCount, const int* cellFaceOffsets, const int* faceIds,
    const signed char* faceSigns, const double* faceFlux, double* cellFlux) {
  const int cell = blockIdx.x * blockDim.x + threadIdx.x;
  if (cell >= cellCount) return;
  double sum = 0.0;
  for (int item = cellFaceOffsets[cell]; item < cellFaceOffsets[cell + 1]; ++item)
    sum += static_cast<double>(faceSigns[item]) * faceFlux[faceIds[item]];
  cellFlux[cell] = sum;
}

extern "C" __global__ void clusterA_sst_closure(
    int cellCount, const double* rho, const double* nu, const double* k,
    const double* omega, const double* wallDistance, const double* gradKDotGradOmega,
    const double* strainRate, const double* rawProduction, double* f1, double* f2,
    double* turbulentViscosity, double* limitedProduction) {
  const int cell = blockIdx.x * blockDim.x + threadIdx.x;
  if (cell >= cellCount) return;
  constexpr double betaStar = 0.09;
  constexpr double a1 = 0.31;
  constexpr double sigmaOmega2 = 0.856;
  const double density = positive(rho[cell]);
  const double molecularNu = positive(nu[cell]);
  const double kineticEnergy = positive(k[cell]);
  const double specificDissipation = positive(omega[cell]);
  const double y = positive(fabs(wallDistance[cell]));
  const double cd = fmax(2.0 * density * sigmaOmega2 / specificDissipation *
                             gradKDotGradOmega[cell], 1.0e-20);
  const double argA = sqrt(kineticEnergy) / (betaStar * specificDissipation * y);
  const double argB = 500.0 * molecularNu / (y * y * specificDissipation);
  const double arg1 = fmin(fmax(argA, argB),
                           4.0 * density * sigmaOmega2 * kineticEnergy / (cd * y * y));
  const double arg2 = fmax(2.0 * sqrt(kineticEnergy) / (betaStar * specificDissipation * y), argB);
  f1[cell] = tanh(pow(fmax(arg1, 0.0), 4.0));
  f2[cell] = tanh(pow(fmax(arg2, 0.0), 2.0));
  turbulentViscosity[cell] = a1 * kineticEnergy /
      fmax(a1 * specificDissipation, fmax(strainRate[cell], 0.0) * f2[cell]);
  const double productionLimit = 10.0 * betaStar * density * kineticEnergy * specificDissipation;
  limitedProduction[cell] = fmin(rawProduction[cell], productionLimit);
}

extern "C" __global__ void clusterA_sst_patankar_sources(
    int cellCount, const double* rho, const double* k, const double* omega,
    const double* volume, const double* f1, const double* turbulentViscosity,
    const double* gradKDotGradOmega, const double* limitedProduction,
    double* kDiagonal, double* kRhs, double* omegaDiagonal, double* omegaRhs) {
  const int cell = blockIdx.x * blockDim.x + threadIdx.x;
  if (cell >= cellCount) return;
  constexpr double betaStar = 0.09;
  constexpr double beta1 = 0.075;
  constexpr double beta2 = 0.0828;
  constexpr double alpha1 = 5.0 / 9.0;
  constexpr double alpha2 = 0.44;
  constexpr double sigmaOmega2 = 0.856;
  const double density = positive(rho[cell]);
  const double kineticEnergy = positive(k[cell]);
  const double specificDissipation = positive(omega[cell]);
  const double cellVolume = fmax(volume[cell], 0.0);
  const double blend = fmin(fmax(f1[cell], 0.0), 1.0);
  const double beta = blend * beta1 + (1.0 - blend) * beta2;
  const double alpha = blend * alpha1 + (1.0 - blend) * alpha2;
  const double production = limitedProduction[cell];
  const double productionOmega = alpha * production / positive(turbulentViscosity[cell]);
  const double crossDiffusion = 2.0 * (1.0 - blend) * density * sigmaOmega2 /
      specificDissipation * gradKDotGradOmega[cell];
  kDiagonal[cell] = betaStar * density * specificDissipation * cellVolume +
      fmax(-production, 0.0) / kineticEnergy * cellVolume;
  kRhs[cell] = fmax(production, 0.0) * cellVolume;
  omegaDiagonal[cell] = (betaStar + 2.0 * beta) * density * specificDissipation * cellVolume +
      fmax(-productionOmega, 0.0) / specificDissipation * cellVolume +
      fmax(-crossDiffusion, 0.0) / specificDissipation * cellVolume;
  omegaRhs[cell] = (betaStar + beta) * density * specificDissipation * specificDissipation * cellVolume +
      fmax(productionOmega, 0.0) * cellVolume + fmax(crossDiffusion, 0.0) * cellVolume;
}

extern "C" __global__ void clusterA_sa_closure(
    int cellCount, const double* nuTilde, const double* nu,
    const double* wallDistance, const double* vorticityMagnitude,
    const double* gradientMagnitudeSquared, double* fv1, double* fv2,
    double* modifiedVorticity, double* fw, double* effectiveDiffusion,
    double* production, double* destruction, double* gradientSource) {
  const int cell = blockIdx.x * blockDim.x + threadIdx.x;
  if (cell >= cellCount) return;
  constexpr double cb1 = 0.1355;
  constexpr double cb2 = 0.622;
  constexpr double sigma = 2.0 / 3.0;
  constexpr double cv1 = 7.1;
  constexpr double cw2 = 0.3;
  constexpr double cw3 = 2.0;
  constexpr double kappa = 0.41;
  const double nutilde = fmax(nuTilde[cell], 0.0);
  const double molecularNu = positive(nu[cell]);
  const double y = positive(fabs(wallDistance[cell]));
  const double chi = nutilde / molecularNu;
  const double chi3 = chi * chi * chi;
  const double cv13 = cv1 * cv1 * cv1;
  fv1[cell] = chi3 / positive(chi3 + cv13);
  fv2[cell] = 1.0 - chi / positive(1.0 + chi * fv1[cell]);
  const double omega = positive(fabs(vorticityMagnitude[cell]));
  const double stilde = fmax(omega + nutilde * fv2[cell] /
      (kappa * kappa * y * y), 0.3 * omega);
  modifiedVorticity[cell] = stilde;
  const double r = fmin(nutilde / positive(stilde * kappa * kappa * y * y), 10.0);
  const double g = r + cw2 * (pow(r, 6.0) - r);
  const double cw36 = pow(cw3, 6.0);
  fw[cell] = g * pow((1.0 + cw36) / positive(pow(g, 6.0) + cw36), 1.0 / 6.0);
  const double cw1 = cb1 / (kappa * kappa) + (1.0 + cb2) / sigma;
  effectiveDiffusion[cell] = molecularNu + nutilde / sigma;
  production[cell] = cb1 * stilde * nutilde;
  destruction[cell] = cw1 * fw[cell] * (nutilde / y) * (nutilde / y);
  gradientSource[cell] = cb2 / sigma * fmax(gradientMagnitudeSquared[cell], 0.0);
}

extern "C" __global__ void clusterA_wale_viscosity(
    int cellCount, const double* velocityGradient, const double* filterWidth,
    double* subgridViscosity) {
  const int cell = blockIdx.x * blockDim.x + threadIdx.x;
  if (cell >= cellCount) return;
  double g[3][3];
  double g2[3][3]{};
  double strain[3][3]{};
  double sd[3][3]{};
  double trace = 0.0;
  for (int i = 0; i < 3; ++i) {
    for (int j = 0; j < 3; ++j) g[i][j] = velocityGradient[cell * 9 + i * 3 + j];
    trace += g[i][i];
  }
  for (int i = 0; i < 3; ++i) {
    for (int j = 0; j < 3; ++j) {
      for (int m = 0; m < 3; ++m) g2[i][j] += g[i][m] * g[m][j];
      strain[i][j] = 0.5 * (g[i][j] + g[j][i]);
      sd[i][j] = 0.5 * (g2[i][j] + g2[j][i]) -
                 (i == j ? trace * trace / 3.0 : 0.0);
    }
  }
  double s2 = 0.0;
  double d2 = 0.0;
  for (int i = 0; i < 3; ++i)
    for (int j = 0; j < 3; ++j) {
      s2 += strain[i][j] * strain[i][j];
      d2 += sd[i][j] * sd[i][j];
    }
  constexpr double cw = 0.325;
  const double numerator = pow(fmax(d2, 0.0), 1.5);
  const double denominator = pow(fmax(s2, 0.0), 2.5) + pow(fmax(d2, 0.0), 1.25) + 1.0e-30;
  const double delta = fmax(filterWidth[cell], 0.0);
  subgridViscosity[cell] = pow(cw * delta, 2.0) * numerator / denominator;
}

extern "C" __global__ void clusterA_all_y_plus_wall(
    int faceCount, const double* rho, const double* nu, const double* tangentialSpeed,
    const double* wallDistance, const double* prandtl, const double* cp,
    const double* cellTemperature, const double* wallTemperature, double* yPlus,
    double* frictionVelocity, double* omegaWall, double* wallShear,
    double* wallHeatFlux) {
  const int face = blockIdx.x * blockDim.x + threadIdx.x;
  if (face >= faceCount) return;
  constexpr double beta1 = 0.075;
  constexpr double betaStar = 0.09;
  constexpr double kappa = 0.41;
  const double density = positive(rho[face]);
  const double molecularNu = positive(nu[face]);
  const double distance = positive(fabs(wallDistance[face]));
  const double uTau = frictionVelocityDevice(tangentialSpeed[face], distance, molecularNu);
  const double yp = uTau * distance / molecularNu;
  const double omegaVis = 6.0 * molecularNu / (beta1 * distance * distance);
  const double omegaLog = uTau / (sqrt(betaStar) * kappa * distance);
  const double tPlus = kaderDevice(yp, prandtl[face]);
  yPlus[face] = yp;
  frictionVelocity[face] = uTau;
  omegaWall[face] = hypot(omegaVis, omegaLog);
  wallShear[face] = density * uTau * uTau;
  wallHeatFlux[face] = density * fmax(cp[face], 0.0) * uTau *
                       (cellTemperature[face] - wallTemperature[face]) / tPlus;
}

extern "C" __global__ void clusterA_cht_flux(
    int faceCount, const double* conductivityF, const double* alphaF,
    const double* conductivityS, const double* alphaS, const double* temperatureP,
    const double* temperatureS, const double* gradTfDotKcorr,
    const double* gradTsDotKcorr, double* fluxDensity) {
  const int face = blockIdx.x * blockDim.x + threadIdx.x;
  if (face >= faceCount) return;
  const double af = positive(alphaF[face]);
  const double as = positive(alphaS[face]);
  const double gf = fmax(conductivityF[face], 0.0) * af;
  const double gs = fmax(conductivityS[face], 0.0) * as;
  const double conductance = gf * gs / positive(gf + gs);
  const double skew = gradTfDotKcorr[face] / af + gradTsDotKcorr[face] / as;
  fluxDensity[face] = conductance * (temperatureS[face] - temperatureP[face] + skew);
}

extern "C" __global__ void clusterA_porous_momentum(
    int cellCount, const double* darcyTensor, const double* forchheimerTensor,
    const double* velocity, const double* dynamicViscosity, const double* density,
    double* momentumSink, double* implicitJacobian) {
  const int cell = blockIdx.x * blockDim.x + threadIdx.x;
  if (cell >= cellCount) return;
  double speedSquared = 0.0;
  double dTimesU[3]{};
  double fTimesU[3]{};
  for (int i = 0; i < 3; ++i) {
    const double ui = velocity[cell * 3 + i];
    speedSquared += ui * ui;
    for (int j = 0; j < 3; ++j) {
      const int ij = cell * 9 + i * 3 + j;
      dTimesU[i] += darcyTensor[ij] * velocity[cell * 3 + j];
      fTimesU[i] += forchheimerTensor[ij] * velocity[cell * 3 + j];
    }
  }
  const double speed = sqrt(speedSquared);
  const double mu = fmax(dynamicViscosity[cell], 0.0);
  const double rhoCell = fmax(density[cell], 0.0);
  for (int i = 0; i < 3; ++i) {
    momentumSink[cell * 3 + i] = mu * dTimesU[i] + 0.5 * rhoCell * speed * fTimesU[i];
    for (int j = 0; j < 3; ++j) {
      const int ij = cell * 9 + i * 3 + j;
      const double forchheimerDerivative = speed * forchheimerTensor[ij] +
          (speed > 1.0e-30 ? fTimesU[i] * velocity[cell * 3 + j] / speed : 0.0);
      implicitJacobian[ij] = mu * darcyTensor[ij] + 0.5 * rhoCell * forchheimerDerivative;
    }
  }
}

extern "C" __global__ void clusterA_p1_source(
    int cellCount, const double* diffusionCoefficient, const double* absorption,
    const double* refractiveIndex, const double* temperature,
    double* diffusionOut, double* reactionOut, double* sourceOut) {
  const int cell = blockIdx.x * blockDim.x + threadIdx.x;
  if (cell >= cellCount) return;
  const double a = fmax(absorption[cell], 0.0);
  const double n = fmax(refractiveIndex[cell], 0.0);
  const double t = fmax(temperature[cell], 0.0);
  diffusionOut[cell] = fmax(diffusionCoefficient[cell], 0.0);
  reactionOut[cell] = a;
  sourceOut[cell] = 4.0 * a * n * n * 5.670374419e-8 * pow(t, 4.0);
}

extern "C" __global__ void clusterA_p1_marshak(
    int faceCount, const double* cellIncidentRadiation, const double* wallTemperature,
    const double* emissivity, const double* diffusionCoefficient,
    const double* wallDistance, double* faceIncidentRadiation, double* outwardFlux,
    double* diagonal, double* rhs) {
  const int face = blockIdx.x * blockDim.x + threadIdx.x;
  if (face >= faceCount) return;
  constexpr double sigmaSB = 5.670374419e-8;
  const double eps = fmin(fmax(emissivity[face], 0.0), 1.0);
  const double h = eps / positive(2.0 * (2.0 - eps));
  const double gamma = fmax(diffusionCoefficient[face], 0.0);
  const double distance = positive(fabs(wallDistance[face]));
  const double gDiff = gamma / distance;
  const double gEff = h * gDiff / positive(h + gDiff);
  const double blackBody = 4.0 * sigmaSB * pow(fmax(wallTemperature[face], 0.0), 4.0);
  const double faceValue = (h * blackBody + gDiff * cellIncidentRadiation[face]) /
                           positive(h + gDiff);
  faceIncidentRadiation[face] = faceValue;
  outwardFlux[face] = gDiff * (cellIncidentRadiation[face] - faceValue);
  diagonal[face] = gEff;
  rhs[face] = gEff * blackBody;
}