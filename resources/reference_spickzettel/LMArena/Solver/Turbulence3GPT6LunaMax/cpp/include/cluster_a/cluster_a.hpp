#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <span>

namespace cluster_a {

inline constexpr double kSmall = 1.0e-12;
inline constexpr double kSigmaSB = 5.670374419e-8;

struct Vec3 {
  double x{};
  double y{};
  double z{};

  double& operator[](std::size_t i) { return i == 0 ? x : (i == 1 ? y : z); }
  const double& operator[](std::size_t i) const { return i == 0 ? x : (i == 1 ? y : z); }
};

struct Mat3 {
  std::array<std::array<double, 3>, 3> a{};

  std::array<double, 3>& operator[](std::size_t i) { return a[i]; }
  const std::array<double, 3>& operator[](std::size_t i) const { return a[i]; }
};

inline Vec3 operator+(Vec3 a, Vec3 b) { return {a.x + b.x, a.y + b.y, a.z + b.z}; }
inline Vec3 operator-(Vec3 a, Vec3 b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
inline Vec3 operator*(double s, Vec3 a) { return {s * a.x, s * a.y, s * a.z}; }
inline Vec3 operator*(Vec3 a, double s) { return s * a; }
inline Vec3 operator/(Vec3 a, double s) { return {a.x / s, a.y / s, a.z / s}; }
inline double dot(Vec3 a, Vec3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
inline double norm(Vec3 a) { return std::sqrt(dot(a, a)); }
inline Vec3 matVec(const Mat3& a, Vec3 x) {
  return {a[0][0] * x.x + a[0][1] * x.y + a[0][2] * x.z,
          a[1][0] * x.x + a[1][1] * x.y + a[1][2] * x.z,
          a[2][0] * x.x + a[2][1] * x.y + a[2][2] * x.z};
}
inline Mat3 transpose(const Mat3& a) {
  Mat3 out{};
  for (std::size_t i = 0; i < 3; ++i)
    for (std::size_t j = 0; j < 3; ++j) out[i][j] = a[j][i];
  return out;
}
inline Mat3 matMul(const Mat3& a, const Mat3& b) {
  Mat3 out{};
  for (std::size_t i = 0; i < 3; ++i)
    for (std::size_t j = 0; j < 3; ++j)
      for (std::size_t k = 0; k < 3; ++k) out[i][j] += a[i][k] * b[k][j];
  return out;
}
inline Mat3 diagonal(double x, double y, double z) {
  Mat3 out{};
  out[0][0] = x;
  out[1][1] = y;
  out[2][2] = z;
  return out;
}

// Weighted least squares reconstruction for arbitrary polyhedral neighbour stencils.
inline Vec3 leastSquaresGradient(std::span<const Vec3> offsets,
                                 std::span<const double> scalarDifferences) {
  if (offsets.size() != scalarDifferences.size() || offsets.empty()) return {};

  Mat3 normal{};
  Vec3 rhs{};
  for (std::size_t n = 0; n < offsets.size(); ++n) {
    const Vec3 d = offsets[n];
    const double r2 = dot(d, d);
    if (r2 <= kSmall * kSmall) continue;
    const double w = 1.0 / r2;
    for (std::size_t i = 0; i < 3; ++i) {
      rhs[i] += w * d[i] * scalarDifferences[n];
      for (std::size_t j = 0; j < 3; ++j) normal[i][j] += w * d[i] * d[j];
    }
  }

  double augmented[3][4]{};
  for (std::size_t i = 0; i < 3; ++i) {
    for (std::size_t j = 0; j < 3; ++j) augmented[i][j] = normal[i][j];
    augmented[i][3] = rhs[i];
  }
  for (std::size_t col = 0; col < 3; ++col) {
    std::size_t pivot = col;
    for (std::size_t row = col + 1; row < 3; ++row)
      if (std::abs(augmented[row][col]) > std::abs(augmented[pivot][col])) pivot = row;
    if (std::abs(augmented[pivot][col]) < 1.0e-20) return {};
    for (std::size_t j = col; j < 4; ++j) std::swap(augmented[col][j], augmented[pivot][j]);
    const double divisor = augmented[col][col];
    for (std::size_t j = col; j < 4; ++j) augmented[col][j] /= divisor;
    for (std::size_t row = 0; row < 3; ++row) {
      if (row == col) continue;
      const double factor = augmented[row][col];
      for (std::size_t j = col; j < 4; ++j) augmented[row][j] -= factor * augmented[col][j];
    }
  }
  return {augmented[0][3], augmented[1][3], augmented[2][3]};
}

struct SSTConstants {
  double betaStar{0.09};
  double a1{0.31};
  double beta1{0.075};
  double beta2{0.0828};
  double alpha1{5.0 / 9.0};
  double alpha2{0.44};
  double sigmaK1{0.85};
  double sigmaK2{1.0};
  double sigmaOmega1{0.5};
  double sigmaOmega2{0.856};
  double cdFloor{1.0e-20};
};

struct SSTInput {
  double rho{1.0};
  double nu{1.5e-5};
  double k{1.0e-3};
  double omega{1.0};
  double wallDistance{1.0e-3};
  Vec3 gradK{};
  Vec3 gradOmega{};
  double strainRate{};
  double rawProduction{};
  double volume{1.0};
};

struct SSTClosure {
  double f1{};
  double f2{};
  double cdKomega{};
  double turbulentViscosity{};
  double production{};
  double productionLimit{};
  double sigmaK{};
  double sigmaOmega{};
};

inline SSTClosure sstClosure(const SSTInput& in, const SSTConstants& c = {}) {
  const double rho = std::max(in.rho, kSmall);
  const double nu = std::max(in.nu, kSmall);
  const double k = std::max(in.k, kSmall);
  const double omega = std::max(in.omega, kSmall);
  const double y = std::max(std::abs(in.wallDistance), kSmall);
  const double cd = std::max(2.0 * rho * c.sigmaOmega2 / omega * dot(in.gradK, in.gradOmega), c.cdFloor);
  const double arg1a = std::sqrt(k) / (c.betaStar * omega * y);
  const double arg1b = 500.0 * nu / (y * y * omega);
  const double arg1 = std::min(std::max(arg1a, arg1b),
                               4.0 * rho * c.sigmaOmega2 * k / (cd * y * y));
  const double arg2 = std::max(2.0 * std::sqrt(k) / (c.betaStar * omega * y), arg1b);
  const double f1 = std::tanh(std::pow(std::max(arg1, 0.0), 4.0));
  const double f2 = std::tanh(std::pow(std::max(arg2, 0.0), 2.0));
  const double denominator = std::max(c.a1 * omega, std::max(in.strainRate, 0.0) * f2);
  const double nut = c.a1 * k / std::max(denominator, kSmall);
  const double limit = 10.0 * c.betaStar * rho * k * omega;
  const double sigmaK = f1 * c.sigmaK1 + (1.0 - f1) * c.sigmaK2;
  const double sigmaOmega = f1 * c.sigmaOmega1 + (1.0 - f1) * c.sigmaOmega2;
  return {f1, f2, cd, nut, std::min(in.rawProduction, limit), limit, sigmaK, sigmaOmega};
}

struct SSTDiffusivity {
  double k{};
  double omega{};
};

inline SSTDiffusivity sstEffectiveDiffusivity(const SSTInput& in,
                                              const SSTClosure& closure) {
  const double rho = std::max(in.rho, kSmall);
  const double molecularMu = rho * std::max(in.nu, kSmall);
  const double turbulentMu = rho * closure.turbulentViscosity;
  return {molecularMu + closure.sigmaK * turbulentMu,
          molecularMu + closure.sigmaOmega * turbulentMu};
}

inline void enforceSSTPositivity(double& k, double& omega,
                                 double kFloor = kSmall,
                                 double omegaFloor = kSmall) {
  if (!std::isfinite(k) || k < kFloor) k = kFloor;
  if (!std::isfinite(omega) || omega < omegaFloor) omega = omegaFloor;
}

inline double stressProduction(const Mat3& reynoldsStress,
                               const Mat3& velocityGradient) {
  double production = 0.0;
  for (std::size_t i = 0; i < 3; ++i)
    for (std::size_t j = 0; j < 3; ++j)
      production += reynoldsStress[i][j] * velocityGradient[i][j];
  return production;
}

struct SSTSourceTerms {
  double kDiagonal{};
  double kRhs{};
  double omegaDiagonal{};
  double omegaRhs{};
  double crossDiffusion{};
  double omegaProduction{};
};

// Negative explicit sources are moved to the diagonal; destruction is linearized at P.
inline SSTSourceTerms sstPatankarSources(const SSTInput& in, const SSTClosure& closure,
                                        const SSTConstants& c = {}) {
  const double rho = std::max(in.rho, kSmall);
  const double k = std::max(in.k, kSmall);
  const double omega = std::max(in.omega, kSmall);
  const double volume = std::max(in.volume, 0.0);
  const double beta = closure.f1 * c.beta1 + (1.0 - closure.f1) * c.beta2;
  const double alpha = closure.f1 * c.alpha1 + (1.0 - closure.f1) * c.alpha2;
  SSTSourceTerms out{};

  if (closure.production >= 0.0) out.kRhs += closure.production * volume;
  else out.kDiagonal += -closure.production / k * volume;
  out.kDiagonal += c.betaStar * rho * omega * volume;

  out.omegaProduction = alpha * closure.production /
                        std::max(closure.turbulentViscosity, kSmall);
  if (out.omegaProduction >= 0.0) out.omegaRhs += out.omegaProduction * volume;
  else out.omegaDiagonal += -out.omegaProduction / omega * volume;

  out.crossDiffusion = 2.0 * (1.0 - closure.f1) * rho * c.sigmaOmega2 / omega *
                       dot(in.gradK, in.gradOmega);
  if (out.crossDiffusion >= 0.0) out.omegaRhs += out.crossDiffusion * volume;
  else out.omegaDiagonal += -out.crossDiffusion / omega * volume;

  // The beta* diagonal coupling is retained as requested; the matching RHS keeps
  // the linearized destruction source value exact at the current omega iterate.
  out.omegaDiagonal += (c.betaStar + 2.0 * beta) * rho * omega * volume;
  out.omegaRhs += (c.betaStar + beta) * rho * omega * omega * volume;
  return out;
}

struct SAConstants {
  double cb1{0.1355};
  double cb2{0.622};
  double sigma{2.0 / 3.0};
  double cv1{7.1};
  double cw2{0.3};
  double cw3{2.0};
  double kappa{0.41};
};

struct SAClosure {
  double fv1{};
  double fv2{};
  double modifiedVorticity{};
  double fw{};
  double diffusion{};
  double production{};
  double destruction{};
  double gradientSource{};
};

inline SAClosure saClosure(double nuTilde, double nu, double wallDistance,
                           double vorticityMagnitude, Vec3 gradNuTilde,
                           const SAConstants& c = {}) {
  const double nutilde = std::max(nuTilde, 0.0);
  const double molecularNu = std::max(nu, kSmall);
  const double y = std::max(std::abs(wallDistance), kSmall);
  const double chi = nutilde / molecularNu;
  const double chi3 = chi * chi * chi;
  const double cv13 = c.cv1 * c.cv1 * c.cv1;
  const double fv1 = chi3 / std::max(chi3 + cv13, kSmall);
  const double fv2 = 1.0 - chi / std::max(1.0 + chi * fv1, kSmall);
  const double omega = std::max(std::abs(vorticityMagnitude), kSmall);
  const double stilde = std::max(omega + nutilde * fv2 / (c.kappa * c.kappa * y * y), 0.3 * omega);
  const double r = std::min(nutilde / std::max(stilde * c.kappa * c.kappa * y * y, kSmall), 10.0);
  const double g = r + c.cw2 * (std::pow(r, 6.0) - r);
  const double cw36 = std::pow(c.cw3, 6.0);
  const double fw = g * std::pow((1.0 + cw36) / std::max(std::pow(g, 6.0) + cw36, kSmall), 1.0 / 6.0);
  const double cw1 = c.cb1 / (c.kappa * c.kappa) + (1.0 + c.cb2) / c.sigma;
  const double production = c.cb1 * stilde * nutilde;
  const double destruction = cw1 * fw * (nutilde / y) * (nutilde / y);
  const double gradientSource = c.cb2 / c.sigma * dot(gradNuTilde, gradNuTilde);
  return {fv1, fv2, stilde, fw, molecularNu + nutilde / c.sigma,
          production, destruction, gradientSource};
}

struct SAPatankarTerms {
  double diagonal{};
  double rhs{};
};

inline SAPatankarTerms saPatankarSources(const SAClosure& closure, double nuTilde,
                                        double volume) {
  const double positiveVariable = std::max(nuTilde, kSmall);
  SAPatankarTerms terms{};
  terms.rhs = (closure.production + closure.gradientSource) * std::max(volume, 0.0);
  terms.diagonal = closure.destruction / positiveVariable * std::max(volume, 0.0);
  return terms;
}

inline double waleEddyViscosity(const Mat3& velocityGradient, double filterWidth,
                                double cw = 0.325) {
  Mat3 strain{};
  Mat3 gradientSquared = matMul(velocityGradient, velocityGradient);
  Mat3 deviatoricSquare{};
  double trace = velocityGradient[0][0] + velocityGradient[1][1] + velocityGradient[2][2];
  double strainSquared = 0.0;
  double deviatoricSquared = 0.0;
  for (std::size_t i = 0; i < 3; ++i) {
    for (std::size_t j = 0; j < 3; ++j) {
      strain[i][j] = 0.5 * (velocityGradient[i][j] + velocityGradient[j][i]);
      deviatoricSquare[i][j] = 0.5 * (gradientSquared[i][j] + gradientSquared[j][i]);
      if (i == j) deviatoricSquare[i][j] -= trace * trace / 3.0;
      strainSquared += strain[i][j] * strain[i][j];
      deviatoricSquared += deviatoricSquare[i][j] * deviatoricSquare[i][j];
    }
  }
  const double numerator = std::pow(std::max(deviatoricSquared, 0.0), 1.5);
  const double denominator = std::pow(std::max(strainSquared, 0.0), 2.5) +
                             std::pow(std::max(deviatoricSquared, 0.0), 1.25) + kSmall;
  return std::pow(cw * std::max(filterWidth, 0.0), 2.0) * numerator / denominator;
}

inline double wallNormalDistance(Vec3 cellCenter, Vec3 faceCenter, Vec3 faceArea) {
  const double areaMagnitude = norm(faceArea);
  if (areaMagnitude <= kSmall) return 0.0;
  return std::abs(dot(cellCenter - faceCenter, faceArea) / areaMagnitude);
}

inline double reichardtVelocityPlus(double yPlus, double kappa = 0.41) {
  const double yp = std::max(yPlus, 0.0);
  if (yp < 1.0e-5) return yp;
  const double viscousLog = std::log1p(kappa * yp) / kappa;
  const double damping = 7.8 * (1.0 - std::exp(-yp / 11.0) -
                                 (yp / 11.0) * std::exp(-yp / 3.0));
  return std::max(viscousLog + damping, 0.0);
}

inline double kaderTemperaturePlus(double yPlus, double prandtl) {
  const double yp = std::max(yPlus, 0.0);
  const double pr = std::max(prandtl, 1.0e-8);
  const double gamma = 0.01 * std::pow(pr * yp, 4.0) /
                       std::max(1.0 + 5.0 * std::pow(pr, 3.0) * yp, kSmall);
  const double beta = std::pow(3.85 * std::pow(pr, 1.0 / 3.0) - 1.3, 2.0) + 2.12 * std::log(pr);
  const double logWeight = gamma > 1.0e-12 ? std::exp(-1.0 / gamma) : 0.0;
  return std::max(pr * yp * std::exp(-gamma) +
                  (2.12 * std::log1p(yp) + beta) * logWeight, kSmall);
}

struct WallInput {
  double rho{1.0};
  double nu{1.5e-5};
  double tangentialSpeed{};
  double wallDistance{};
  double prandtl{0.71};
  double cp{1005.0};
  double cellTemperature{300.0};
  double wallTemperature{300.0};
};

struct WallResult {
  double yPlus{};
  double frictionVelocity{};
  double uPlus{};
  double omegaViscous{};
  double omegaLogarithmic{};
  double omegaWall{};
  double shearStress{};
  double temperaturePlus{};
  double heatFlux{};
};

inline double solveFrictionVelocity(double speed, double y, double nu, double kappa = 0.41) {
  const double u = std::max(speed, 0.0);
  if (u <= kSmall) return 0.0;
  const double distance = std::max(y, kSmall);
  const double molecularNu = std::max(nu, kSmall);
  auto residual = [&](double friction) {
    return friction * reichardtVelocityPlus(friction * distance / molecularNu, kappa) - u;
  };
  double lo = 0.0;
  double hi = std::max({u, std::sqrt(u * molecularNu / distance), 1.0e-8});
  for (int i = 0; i < 64 && residual(hi) < 0.0; ++i) hi *= 2.0;
  for (int i = 0; i < 72; ++i) {
    const double mid = 0.5 * (lo + hi);
    if (residual(mid) > 0.0) hi = mid;
    else lo = mid;
  }
  return 0.5 * (lo + hi);
}

inline WallResult allYPlusWallTreatment(const WallInput& in,
                                       const SSTConstants& c = {}) {
  const double rho = std::max(in.rho, kSmall);
  const double nu = std::max(in.nu, kSmall);
  const double y = std::max(std::abs(in.wallDistance), kSmall);
  const double uTau = solveFrictionVelocity(in.tangentialSpeed, y, nu);
  const double yPlus = uTau * y / nu;
  const double uPlus = reichardtVelocityPlus(yPlus);
  const double omegaVis = 6.0 * nu / (c.beta1 * y * y);
  const double omegaLog = uTau / (std::sqrt(c.betaStar) * 0.41 * y);
  const double omegaWall = std::hypot(omegaVis, omegaLog);
  const double tPlus = kaderTemperaturePlus(yPlus, in.prandtl);
  const double heatFlux = rho * std::max(in.cp, 0.0) * uTau *
                          (in.cellTemperature - in.wallTemperature) / tPlus;
  return {yPlus, uTau, uPlus, omegaVis, omegaLog, omegaWall,
          rho * uTau * uTau, tPlus, heatFlux};
}

struct CHTFlux {
  double conductancePerArea{};
  double skewTemperatureJump{};
  double fluxDensity{};
};

inline double chtConductancePerArea(double conductivityF, double alphaF,
                                    double conductivityS, double alphaS) {
  const double gf = std::max(conductivityF, 0.0) * std::max(alphaF, 0.0);
  const double gs = std::max(conductivityS, 0.0) * std::max(alphaS, 0.0);
  const double denom = std::max(gf + gs, kSmall);
  return gf * gs / denom;
}

inline CHTFlux chtInterfaceFlux(double conductivityF, double alphaF,
                                double conductivityS, double alphaS,
                                double temperatureP, double temperatureS,
                                double gradTfDotKcorr, double gradTsDotKcorr) {
  const double af = std::max(alphaF, kSmall);
  const double as = std::max(alphaS, kSmall);
  const double h = chtConductancePerArea(conductivityF, af, conductivityS, as);
  const double skew = gradTfDotKcorr / af + gradTsDotKcorr / as;
  return {h, skew, h * ((temperatureS - temperatureP) + skew)};
}

inline double chtInterfaceTemperature(double conductivityF, double alphaF,
                                      double conductivityS, double alphaS,
                                      double temperatureP, double temperatureS,
                                      double skewNumerator = 0.0) {
  const double gf = std::max(conductivityF, 0.0) * std::max(alphaF, 0.0);
  const double gs = std::max(conductivityS, 0.0) * std::max(alphaS, 0.0);
  return (gf * temperatureP + gs * temperatureS + skewNumerator) /
         std::max(gf + gs, kSmall);
}

struct MonolithicCHTCoupling {
  double conductance{};
  double fluidDiagonal{};
  double fluidToSolid{};
  double solidDiagonal{};
  double solidToFluid{};
  double fluidRhs{};
  double solidRhs{};
};

inline MonolithicCHTCoupling monolithicCHTCoupling(double conductivityF, double alphaF,
                                                   double conductivityS, double alphaS,
                                                   double faceArea, double skewJump) {
  const double conductance = chtConductancePerArea(conductivityF, alphaF,
                                                    conductivityS, alphaS) *
                             std::max(faceArea, 0.0);
  return {conductance, conductance, -conductance, conductance, -conductance,
          conductance * skewJump, -conductance * skewJump};
}

struct PartitionedCHTState {
  double relaxedFluxDensity{};
  double fluidFluxDensity{};
  double solidFluxDensity{};
  double interfaceTemperature{};
  double fluidRobinCoefficient{};
  double solidRobinCoefficient{};
  double fluidRobinTarget{};
  double solidRobinTarget{};
};

inline PartitionedCHTState partitionedRobinCHT(const CHTFlux& flux,
                                               double previousFluxDensity,
                                               double relaxation,
                                               double interfaceTemperature,
                                               double temperatureP,
                                               double temperatureS) {
  const double r = std::clamp(relaxation, 0.0, 1.0);
  const double q = previousFluxDensity + r * (flux.fluxDensity - previousFluxDensity);
  const double h = std::max(flux.conductancePerArea, kSmall);
  return {q, q, -q, interfaceTemperature,
          flux.conductancePerArea, flux.conductancePerArea,
          temperatureP + q / h, temperatureS - q / h};
}

inline Mat3 anisotropicTensor(const Mat3& rotation, Vec3 principalValues) {
  return matMul(matMul(rotation, diagonal(principalValues.x, principalValues.y,
                                          principalValues.z)), transpose(rotation));
}

struct PorousMomentum {
  Vec3 sink{};
  Mat3 implicitJacobian{};
};

inline PorousMomentum porousMomentumSink(const Mat3& darcyTensor,
                                        const Mat3& forchheimerTensor,
                                        Vec3 velocity, double dynamicViscosity,
                                        double density) {
  const double speed = norm(velocity);
  const Vec3 forchheimerVelocity = matVec(forchheimerTensor, velocity);
  const Vec3 darcy = matVec(darcyTensor, velocity);
  PorousMomentum out{};
  out.sink = std::max(dynamicViscosity, 0.0) * darcy +
             (0.5 * std::max(density, 0.0) * speed) * forchheimerVelocity;
  for (std::size_t i = 0; i < 3; ++i)
    for (std::size_t j = 0; j < 3; ++j) {
      out.implicitJacobian[i][j] = std::max(dynamicViscosity, 0.0) * darcyTensor[i][j] +
          0.5 * std::max(density, 0.0) *
          (speed * forchheimerTensor[i][j] +
           (speed > kSmall ? forchheimerVelocity[i] * velocity[j] / speed : 0.0));
    }
  return out;
}

struct P1Source {
  double diffusionCoefficient{};
  double absorption{};
  double source{};
};

inline P1Source p1Source(double diffusionCoefficient, double absorption,
                         double refractiveIndex, double temperature) {
  const double a = std::max(absorption, 0.0);
  const double n = std::max(refractiveIndex, 0.0);
  const double t = std::max(temperature, 0.0);
  return {std::max(diffusionCoefficient, 0.0), a,
          4.0 * a * n * n * kSigmaSB * std::pow(t, 4.0)};
}

struct MarshakBoundary {
  double faceIncidentRadiation{};
  double outwardFlux{};
  double diagonal{};
  double rhs{};
};

inline MarshakBoundary p1MarshakBoundary(double cellIncidentRadiation,
                                         double wallTemperature,
                                         double emissivity,
                                         double diffusionCoefficient,
                                         double wallDistance) {
  const double eps = std::clamp(emissivity, 0.0, 1.0);
  const double gamma = std::max(diffusionCoefficient, 0.0);
  const double distance = std::max(std::abs(wallDistance), kSmall);
  const double hMarshak = eps / std::max(2.0 * (2.0 - eps), kSmall);
  const double gDiff = gamma / distance;
  const double gEff = hMarshak * gDiff / std::max(hMarshak + gDiff, kSmall);
  const double blackBody = 4.0 * kSigmaSB * std::pow(std::max(wallTemperature, 0.0), 4.0);
  const double face = (hMarshak * blackBody + gDiff * cellIncidentRadiation) /
                      std::max(hMarshak + gDiff, kSmall);
  const double flux = gDiff * (cellIncidentRadiation - face);
  return {face, flux, gEff, gEff * blackBody};
}

}  // namespace cluster_a