#include "cluster_a/cluster_a.hpp"

#include <array>
#include <cmath>
#include <cstdlib>
#include <iomanip>
#include <iostream>
#include <string_view>

namespace {

int checks = 0;

void require(bool condition, std::string_view message) {
  ++checks;
  if (!condition) {
    std::cerr << "FAIL: " << message << '\n';
    std::exit(EXIT_FAILURE);
  }
}

void near(double actual, double expected, double tolerance, std::string_view message) {
  const double scale = std::max({1.0, std::abs(actual), std::abs(expected)});
  require(std::abs(actual - expected) <= tolerance * scale, message);
}

void verifyPolyhedralGradient() {
  using namespace cluster_a;
  const std::array<Vec3, 6> offsets{{{1, 0, 0}, {-1, 0, 0}, {0, 2, 0},
                                      {0, -2, 0}, {0, 0, 3}, {0, 0, -3}}};
  constexpr Vec3 exact{2.5, -1.25, 0.75};
  std::array<double, offsets.size()> differences{};
  for (std::size_t i = 0; i < offsets.size(); ++i) differences[i] = dot(exact, offsets[i]);
  const Vec3 reconstructed = leastSquaresGradient(offsets, differences);
  near(reconstructed.x, exact.x, 1.0e-12, "polyhedral least-squares gradient x");
  near(reconstructed.y, exact.y, 1.0e-12, "polyhedral least-squares gradient y");
  near(reconstructed.z, exact.z, 1.0e-12, "polyhedral least-squares gradient z");
}

void verifySSTAndSA() {
  using namespace cluster_a;
  SSTInput in{};
  in.rho = 1.18;
  in.nu = 1.55e-5;
  in.k = 0.08;
  in.omega = 42.0;
  in.wallDistance = 7.0e-4;
  in.gradK = {0.2, 0.1, 0.0};
  in.gradOmega = {4.0, -2.0, 0.5};
  in.strainRate = 85.0;
  in.rawProduction = 1.0e5;
  in.volume = 0.004;
  const SSTClosure closure = sstClosure(in);
  require(closure.f1 >= 0.0 && closure.f1 <= 1.0, "SST F1 is bounded");
  require(closure.f2 >= 0.0 && closure.f2 <= 1.0, "SST F2 is bounded");
  require(closure.turbulentViscosity > 0.0, "SST eddy viscosity is positive");
  near(closure.production, closure.productionLimit, 1.0e-12, "SST production limiter");
  const SSTDiffusivity diffusivity = sstEffectiveDiffusivity(in, closure);
  require(diffusivity.k > 0.0 && diffusivity.omega > 0.0, "SST blended diffusivities are positive");
  const SSTSourceTerms source = sstPatankarSources(in, closure);
  require(source.kDiagonal > 0.0 && source.kRhs > 0.0, "k Patankar sink and source are positive");
  require(source.omegaDiagonal > 0.0 && source.omegaRhs > 0.0,
          "omega Patankar sink and source are positive");
  const double expectedCross = 2.0 * (1.0 - closure.f1) * in.rho * 0.856 /
                               in.omega * dot(in.gradK, in.gradOmega);
  near(source.crossDiffusion, expectedCross, 1.0e-12, "SST cross-diffusion coefficient");

  SSTInput pathological = in;
  pathological.k = -1.0;
  pathological.omega = 0.0;
  pathological.wallDistance = 0.0;
  pathological.rawProduction = -5.0;
  const SSTClosure bounded = sstClosure(pathological);
  require(std::isfinite(bounded.turbulentViscosity) && bounded.turbulentViscosity > 0.0,
          "SST closure remains finite at invalid iterates");
  const SSTSourceTerms negative = sstPatankarSources(pathological, bounded);
  require(negative.kDiagonal > 0.0 && negative.omegaDiagonal > 0.0,
          "negative SST sources are implicitized");
  double kAfterSolve = -1.0;
  double omegaAfterSolve = 0.0;
  enforceSSTPositivity(kAfterSolve, omegaAfterSolve);
  require(kAfterSolve > 0.0 && omegaAfterSolve > 0.0, "post-solve SST floors are unconditional");
  Mat3 stress{};
  Mat3 gradient{};
  stress[0][1] = 3.0;
  gradient[0][1] = 4.0;
  near(stressProduction(stress, gradient), 12.0, 1.0e-12, "stress-gradient production contraction");

  const SAClosure sa = saClosure(2.0e-4, 1.5e-5, 8.0e-4, 70.0, {0.2, -0.1, 0.05});
  require(sa.fv1 >= 0.0 && sa.fv1 <= 1.0, "SA fv1 is bounded");
  require(sa.diffusion > 0.0 && sa.destruction >= 0.0, "SA diffusion and destruction are nonnegative");
  const SAPatankarTerms saTerms = saPatankarSources(sa, 2.0e-4, 0.01);
  require(saTerms.diagonal > 0.0 && saTerms.rhs > 0.0, "SA Patankar terms preserve positivity");

  Mat3 shear{};
  shear[0][1] = 5.0;
  near(waleEddyViscosity(shear, 0.01), 0.0, 1.0e-12, "WALE vanishes for simple shear");
  Mat3 mixed{};
  mixed[0][1] = 3.0;
  mixed[1][0] = -0.7;
  mixed[1][2] = 1.4;
  mixed[2][0] = 0.3;
  const double wale = waleEddyViscosity(mixed, 0.01);
  require(std::isfinite(wale) && wale >= 0.0, "WALE viscosity is finite and nonnegative");
}

void verifyAllYPlusWallTreatment() {
  using namespace cluster_a;
  constexpr double nu = 1.5e-5;
  constexpr double targetFrictionVelocity = 0.45;
  for (double requestedYPlus : {0.1, 5.0, 30.0, 500.0}) {
    const double y = requestedYPlus * nu / targetFrictionVelocity;
    const double speed = targetFrictionVelocity * reichardtVelocityPlus(requestedYPlus);
    WallInput input{};
    input.rho = 1.2;
    input.nu = nu;
    input.tangentialSpeed = speed;
    input.wallDistance = y;
    input.prandtl = 0.71;
    input.cellTemperature = 330.0;
    input.wallTemperature = 300.0;
    const WallResult wall = allYPlusWallTreatment(input);
    near(wall.yPlus, requestedYPlus, 2.0e-7, "all-y+ friction velocity recovery");
    require(wall.omegaWall > 0.0 && wall.shearStress > 0.0,
            "all-y+ wall omega and shear are positive");
    require(std::isfinite(wall.heatFlux) && wall.heatFlux > 0.0,
            "Kader wall heat flux is finite");
    if (requestedYPlus < 0.2) {
      const double conductiveFlux = input.rho * input.cp * input.nu *
                                    (input.cellTemperature - input.wallTemperature) /
                                    (input.prandtl * y);
      near(wall.heatFlux, conductiveFlux, 2.0e-3,
           "Kader sublayer recovers molecular wall conduction");
    }
  }

  const double logLaw = std::log(9.8 * 1000.0) / 0.41;
  near(reichardtVelocityPlus(1000.0), logLaw, 0.01, "Reichardt profile reaches the log layer");
  near(wallNormalDistance({0.0, 0.0, 0.01}, {0.0, 0.0, 0.0}, {0.0, 0.0, -2.0}),
       0.01, 1.0e-12, "wall distance is orientation independent");

  WallInput zeroDistance{};
  zeroDistance.wallDistance = 0.0;
  zeroDistance.tangentialSpeed = 2.0;
  const WallResult guarded = allYPlusWallTreatment(zeroDistance);
  require(std::isfinite(guarded.omegaWall) && std::isfinite(guarded.heatFlux),
          "wall treatment guards zero distance");
}

void verifyCompositeCHT() {
  using namespace cluster_a;
  constexpr double hot = 400.0;
  constexpr double cold = 300.0;
  constexpr double hi = 100.0;
  constexpr double ho = 25.0;
  constexpr double lf = 0.01;
  constexpr double ls = 0.02;
  constexpr double kf = 0.6;
  constexpr double ks = 15.0;
  const double totalResistance = 1.0 / hi + lf / kf + ls / ks + 1.0 / ho;
  const double analyticFlux = (hot - cold) / totalResistance;
  const double tfCell = hot - analyticFlux * (1.0 / hi + 0.5 * lf / kf);
  const double tsCell = cold + analyticFlux * (1.0 / ho + 0.5 * ls / ks);
  const double alphaF = 2.0 / lf;
  const double alphaS = 2.0 / ls;

  const CHTFlux interface = chtInterfaceFlux(kf, alphaF, ks, alphaS,
                                             tfCell, tsCell, 0.0, 0.0);
  near(-interface.fluxDensity, analyticFlux, 1.0e-12,
       "composite convection-conduction interface flux");
  const CHTFlux corrected = chtInterfaceFlux(kf, alphaF, ks, alphaS,
                                             tfCell, tsCell, 0.1, -0.2);
  near(corrected.skewTemperatureJump, 0.1 / alphaF - 0.2 / alphaS,
       1.0e-12, "CHT non-orthogonal correction scaling");
  const double analyticInterfaceTemperature = hot - analyticFlux * (1.0 / hi + lf / kf);
  near(chtInterfaceTemperature(kf, alphaF, ks, alphaS, tfCell, tsCell),
       analyticInterfaceTemperature, 1.0e-12, "harmonic CHT interface temperature");

  const MonolithicCHTCoupling matrix = monolithicCHTCoupling(kf, alphaF, ks, alphaS, 1.0, 0.0);
  near(matrix.fluidDiagonal, matrix.solidDiagonal, 1.0e-12, "monolithic CHT diagonal symmetry");
  near(matrix.fluidToSolid + matrix.fluidDiagonal, 0.0, 1.0e-12, "fluid CHT row conservation");
  near(matrix.solidToFluid + matrix.solidDiagonal, 0.0, 1.0e-12, "solid CHT row conservation");
  const PartitionedCHTState partitioned = partitionedRobinCHT(interface, 0.0, 0.5,
                                                              analyticInterfaceTemperature,
                                                              tfCell, tsCell);
  near(partitioned.relaxedFluxDensity, 0.5 * interface.fluxDensity, 1.0e-12,
       "partitioned Robin flux under-relaxation");
  near(partitioned.fluidFluxDensity + partitioned.solidFluxDensity, 0.0, 1.0e-12,
       "partitioned twin-face flux conservation");
  near(interface.conductancePerArea * (partitioned.fluidRobinTarget - tfCell),
       partitioned.fluidFluxDensity, 1.0e-12, "fluid Robin target recovers relaxed flux");
  near(interface.conductancePerArea * (partitioned.solidRobinTarget - tsCell),
       partitioned.solidFluxDensity, 1.0e-12, "solid Robin target recovers opposite relaxed flux");
}

void verifyPorousAndRadiation() {
  using namespace cluster_a;
  constexpr double angle = 0.7853981633974483;
  Mat3 rotation{};
  rotation[0][0] = std::cos(angle);
  rotation[0][1] = -std::sin(angle);
  rotation[1][0] = std::sin(angle);
  rotation[1][1] = std::cos(angle);
  rotation[2][2] = 1.0;
  const Mat3 anisotropic = anisotropicTensor(rotation, {1.0, 4.0, 9.0});
  near(anisotropic[0][0], 2.5, 1.0e-12, "rotated anisotropic tensor diagonal");
  near(anisotropic[0][1], -1.5, 1.0e-12, "rotated anisotropic tensor cross term");

  const Mat3 darcy = diagonal(1000.0, 0.0, 0.0);
  const Mat3 forchheimer = diagonal(10.0, 0.0, 0.0);
  const PorousMomentum porous = porousMomentumSink(darcy, forchheimer,
                                                   {0.2, 0.0, 0.0}, 1.0e-3, 1.2);
  near(porous.sink.x, 0.44, 1.0e-12, "Darcy-Forchheimer pressure-drop benchmark");
  near(porous.implicitJacobian[0][0], 3.4, 1.0e-12,
       "implicit porous momentum Jacobian");

  const P1Source p1 = p1Source(0.12, 0.8, 1.0, 1000.0);
  near(p1.source, 4.0 * 0.8 * kSigmaSB * std::pow(1000.0, 4.0),
       1.0e-12, "P1 thermal emission source");
  const MarshakBoundary wall = p1MarshakBoundary(1.0e4, 900.0, 0.8, 0.12, 0.02);
  require(wall.diagonal > 0.0 && wall.rhs > 0.0 && std::isfinite(wall.outwardFlux),
          "P1 Marshak wall boundary is finite and implicit");
  near(wall.outwardFlux, wall.diagonal * 1.0e4 - wall.rhs, 1.0e-12,
       "P1 Marshak boundary matches its Robin matrix row");
}

}  // namespace

int main() {
  verifyPolyhedralGradient();
  verifySSTAndSA();
  verifyAllYPlusWallTreatment();
  verifyCompositeCHT();
  verifyPorousAndRadiation();
  std::cout << "Cluster A reference verification: " << checks << " checks passed\n";
  std::cout << "Benchmarks: composite CHT | Reichardt/Kader all-y+ | Darcy-Forchheimer | P1 Marshak\n";
  return EXIT_SUCCESS;
}