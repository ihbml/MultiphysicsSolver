// Phase 4 MDO engine CLI.  All numerical work is performed by the standalone
// C++20 implementation in include/mdo_core.hpp and include/mdo_optim.hpp.
// Usage: mdo_engine verify|optimize|shape|revolve|fosm [key=value ...]
#include "../include/mdo_core.hpp"
#include "../include/mdo_optim.hpp"

#include <cstdlib>
#include <cstring>
#include <iomanip>
#include <iostream>
#include <sstream>

using namespace mdo;

static std::map<std::string, std::string> parseArgs(int argc, char** argv) {
  std::map<std::string, std::string> a;
  for (int i = 2; i < argc; ++i) {
    std::string s(argv[i]);
    auto p = s.find('=');
    if (p != std::string::npos) a[s.substr(0, p)] = s.substr(p + 1);
  }
  return a;
}
static double darg(const std::map<std::string, std::string>& a, const std::string& k, double d) {
  auto it = a.find(k); if (it == a.end()) return d;
  try { return std::stod(it->second); } catch (...) { return d; }
}
static int iarg(const std::map<std::string, std::string>& a, const std::string& k, int d) {
  return (int)darg(a, k, d);
}
static std::string jsonString(const std::string& s) {
  std::string out = "\"";
  for (char c : s) {
    if (c == '"' || c == '\\') { out += '\\'; out += c; }
    else if (c == '\n') out += "\\n";
    else out += c;
  }
  return out + "\"";
}
static std::string jv(const Vec& v, int prec = 8) {
  std::ostringstream o; o << std::setprecision(prec) << "[";
  for (size_t i = 0; i < v.size(); ++i) { if (i) o << ','; o << v[i]; }
  o << ']'; return o.str();
}
static std::string jb(bool b) { return b ? "true" : "false"; }

static ChainConfig chainFromArgs(const std::map<std::string, std::string>& a) {
  ChainConfig c;
  c.rmin = darg(a, "rmin", 2.4);
  c.betaH = darg(a, "beta", 8.0);
  c.eta = darg(a, "eta", 0.5);
  c.rampQ = darg(a, "q", 3.0);
  return c;
}
static ThermalConfig thermalFromArgs(const std::map<std::string, std::string>& a) {
  ThermalConfig t;
  t.sMat = darg(a, "sMat", 0.08);
  t.sConv = darg(a, "sConv", 0.10);
  t.hConv = darg(a, "hConv", 0.35);
  return t;
}
static Vec nominalGamma(const CellGraph& g, double base = 0.5) {
  Vec x(g.n);
  for (int i = 0; i < g.n; ++i) {
    double r = std::hypot(g.cx[i] - 0.5, g.cy[i] - 0.5);
    x[i] = std::clamp(base + 0.10 * std::sin(7.0 * g.cx[i]) * std::cos(5.0 * g.cy[i]) - 0.08 * r, 0.05, 0.95);
  }
  return x;
}

// ---------------------------- verification ---------------------------------
static std::string verify(const std::map<std::string, std::string>& a) {
  int nx = iarg(a, "nx", 9), ny = iarg(a, "ny", 8);
  int mcSamples = iarg(a, "samples", 10000);
  ChainConfig cc = chainFromArgs(a);
  ThermalConfig tc = thermalFromArgs(a);
  CellGraph g(nx, ny);
  DesignChain chain(g, cc);
  ThermalModel model(g, cc, tc);
  Vec gamma = nominalGamma(g, 0.5);
  chain.forward(gamma);
  ThermalState st = model.primal(chain);

  // (1) complex-step and central-difference validation of the full chain.
  Vec weight(g.n);
  for (int i = 0; i < g.n; ++i) weight[i] = 0.4 + 0.6 * std::sin(3.1 * g.cx[i]) * std::cos(2.3 * g.cy[i]);
  Vec node(g.n);
  for (int i = 0; i < g.n; ++i) node[i] = chain.dRho[i] * weight[i];
  Vec analytic = chain.vjp(node);
  const double hComplex = 1e-30, hCentral = darg(a, "fdStep", 2.5e-3);
  double maxComplex = 0.0, maxCentral = 0.0;
  auto scalar = [&](const Vec& x) {
    DesignChain c2(g, cc); c2.forward(x);
    return dot(weight, c2.rho);
  };
  for (int j = 0; j < g.n; ++j) {
    CVec xc(g.n);
    for (int i = 0; i < g.n; ++i) xc[i] = Cx(gamma[i], 0.0);
    xc[j] += Cx(0.0, hComplex);
    CVec rhoC = chain.forwardComplex(xc);
    Cx val(0.0, 0.0);
    for (int i = 0; i < g.n; ++i) val += double(weight[i]) * rhoC[i];
    double cs = val.imag() / hComplex;
    // Seven-point central stencil, O(h^6) truncation.
    auto shifted = [&](double s) { Vec z = gamma; z[j] += s; return scalar(z); };
    double cd = (shifted(3 * hCentral) - 9 * shifted(2 * hCentral)
                 + 45 * shifted(hCentral) - 45 * shifted(-hCentral)
                 + 9 * shifted(-2 * hCentral) - shifted(-3 * hCentral)) / (60 * hCentral);
    maxComplex = std::max(maxComplex, std::abs(cs - analytic[j]));
    maxCentral = std::max(maxCentral, std::abs(cd - analytic[j]));
  }

  // (2) KS gradient against complex-step.
  Vec tv(g.n);
  for (int i = 0; i < g.n; ++i) tv[i] = 3.0 * st.T[i] + 0.5 * std::sin(9.0 * g.cx[i]);
  double rhoKS = darg(a, "rhoKS", 6.0);
  KSResult ks = ksAggregate(tv, rhoKS);
  double ksComplexErr = 0.0;
  for (int j = 0; j < g.n; ++j) {
    CVec vc(g.n);
    for (int i = 0; i < g.n; ++i) vc[i] = Cx(tv[i], 0.0);
    vc[j] += Cx(0.0, hComplex);
    Cx v = ksAggregateComplex(vc, rhoKS);
    ksComplexErr = std::max(ksComplexErr, std::abs(v.imag() / hComplex - ks.weight[j]));
  }

  // (3) FOSM variance sensitivity against common-random-number Monte Carlo.
  double sigmaXi = darg(a, "sigmaXi", 0.05);
  double ell = darg(a, "ell", 0.45);
  double kappa = darg(a, "kappa", 2.0);
  auto Sigma = exponentialCovariance(g, sigmaXi, ell);
  Fosm fosm(g, model, chain);
  FosmResult fr = fosm.evaluate(st, Sigma, RobustFunctional::MeanTemperature, kappa, rhoKS);
  McCheck mc = monteCarloVarianceCheck(g, model, cc, gamma, Sigma, fr.gradSigma, fr.sigma,
                                       RobustFunctional::MeanTemperature, rhoKS,
                                       mcSamples, 71237u, 2e-2);

  // (4) Revolve replay accounting.
  int steps = iarg(a, "steps", 48), slots = iarg(a, "slots", 4);
  Revolve rev(steps, slots);
  RevolveResult trace = rev.schedule();

  std::ostringstream o;
  o << "{\"mode\":\"verify\",\"chain\":{"
    << "\"cells\":" << g.n
    << ",\"complexStepMaxError\":" << maxComplex
    << ",\"centralDifferenceMaxError\":" << maxCentral
    << ",\"tolerance\":1e-11"
    << ",\"pass\":" << jb(maxComplex < 1e-11 && maxCentral < 1e-11)
    << "},\"ks\":{\"value\":" << ks.value << ",\"maxValue\":" << ks.maxValue
    << ",\"complexStepMaxError\":" << ksComplexErr
    << ",\"pass\":" << jb(ksComplexErr < 1e-11) << "}"
    << ",\"fosm\":{\"mu\":" << fr.mu << ",\"sigma\":" << fr.sigma
    << ",\"sigmaGradientNorm\":" << norm(fr.gradSigma)
    << ",\"analyticVarianceDerivative\":" << mc.analytic
    << ",\"monteCarloVarianceDerivative\":" << mc.monteCarlo
    << ",\"variancePlus\":" << mc.variancePlus << ",\"varianceMinus\":" << mc.varianceMinus
    << ",\"relativeError\":" << mc.relativeError << ",\"samples\":" << mc.samples
    << ",\"speedupFactor\":" << (2.0 * mc.samples / fr.adjointSolves)
    << ",\"pass\":" << jb(mc.pass) << "}"
    << ",\"revolve\":{\"steps\":" << steps << ",\"slots\":" << slots
    << ",\"forwardCalls\":" << trace.forwardCalls
    << ",\"minimalForwardCalls\":" << rev.minimalForwardCalls()
    << ",\"adjointCalls\":" << trace.adjointCalls
    << ",\"recomputations\":" << trace.recomputations
    << ",\"maxSlotsUsed\":" << trace.maxSlotsUsed
    << ",\"pass\":" << jb(trace.adjointCalls == steps &&
                          trace.forwardCalls == rev.minimalForwardCalls() &&
                          trace.maxSlotsUsed <= slots) << "}"
    << "}";
  return o.str();
}

// ---------------------------- topology -------------------------------------
struct OptSnapshot { double obj = 0, volume = 0, peak = 0, sigma = 0, conVol = 0, conPeak = 0, beta = 0; };

static std::string optimize(const std::map<std::string, std::string>& a) {
  int nx = iarg(a, "nx", 12), ny = iarg(a, "ny", 9);
  int totalIt = iarg(a, "iters", 60);
  if (totalIt < 2) totalIt = 2;
  double vfrac = darg(a, "volFrac", 0.40);
  double rhoKS = darg(a, "rhoKS", 6.0);
  double kappa = darg(a, "kappa", 2.0);
  double sigmaXi = darg(a, "sigmaXi", 0.05);
  double ell = darg(a, "ell", 0.45);
  double peakFactor = darg(a, "peakFactor", 1.06);
  bool continuation = iarg(a, "continuation", 1) != 0;
  ChainConfig cc = chainFromArgs(a);
  ThermalConfig tc = thermalFromArgs(a);
  CellGraph g(nx, ny);
  DesignChain chain(g, cc);
  ThermalModel model(g, cc, tc);
  Vec gamma = nominalGamma(g, vfrac);
  auto Sigma = exponentialCovariance(g, sigmaXi, ell);
  Fosm fosm(g, model, chain);

  chain.forward(gamma);
  ThermalState st0 = model.primal(chain);
  double tLimit = peakFactor * ksAggregate(st0.T, rhoKS).value;

  Vec ones(g.n, 1.0 / g.n);
  MMA mma(g.n, 2, Vec(g.n, 0.02), Vec(g.n, 0.98));
  std::vector<OptSnapshot> hist;
  std::vector<double> betaSchedule = continuation
      ? std::vector<double>{1, 2, 4, 8, 16, 32}
      : std::vector<double>{cc.betaH};
  int perStage = std::max(1, totalIt / (int)betaSchedule.size());
  int iter = 0;
  for (double beta : betaSchedule) {
    cc.betaH = beta; chain.cfg = cc; model.chainCfg = cc;
    for (int k = 0; k < perStage && iter < totalIt; ++k, ++iter) {
      chain.forward(gamma);
      ThermalState st = model.primal(chain);
      FosmResult fr = fosm.evaluate(st, Sigma, RobustFunctional::MeanTemperature, kappa, rhoKS);
      KSResult peak = ksAggregate(st.T, rhoKS);
      double volume = std::accumulate(chain.gammaBar.begin(), chain.gammaBar.end(), 0.0) / g.n;
      Vec dVolume = chain.vjp(ones);
      Vec lambdaPeak = pcg(st.B, peak.weight);
      Vec nodeP = model.edgeGradient(st.dkDgammaBar, lambdaPeak, st.T);
      Vec nodePeak(g.n);
      for (int i = 0; i < g.n; ++i) nodePeak[i] = -nodeP[i];
      Vec dPeak = chain.vjp(nodePeak);
      std::vector<double> cons = {volume - vfrac, peak.value - tLimit};
      std::vector<Vec> dcons = {dVolume, dPeak};
      gamma = mma.step(gamma, fr.phi, fr.gradPhi, cons, dcons);
      hist.push_back({fr.phi, volume, peak.value, fr.sigma, cons[0], cons[1], beta});
    }
  }
  chain.forward(gamma);
  ThermalState finalSt = model.primal(chain);
  FosmResult finalFosm = fosm.evaluate(finalSt, Sigma, RobustFunctional::MeanTemperature, kappa, rhoKS);

  std::ostringstream o;
  o << "{\"mode\":\"optimize\",\"nx\":" << nx << ",\"ny\":" << ny
    << ",\"iterations\":" << (int)hist.size()
    << ",\"objective\":" << finalFosm.phi
    << ",\"initialObjective\":" << (hist.empty() ? 0 : hist[0].obj)
    << ",\"volumeFraction\":" << std::accumulate(chain.gammaBar.begin(), chain.gammaBar.end(), 0.0) / g.n
    << ",\"peakTemperature\":" << ksAggregate(finalSt.T, rhoKS).value
    << ",\"peakLimit\":" << tLimit
    << ",\"sigma\":" << finalFosm.sigma
    << ",\"betaH\":" << chain.cfg.betaH
    << ",\"history\":[";
  for (size_t k = 0; k < hist.size(); ++k) {
    if (k) o << ',';
    o << "{\"obj\":" << hist[k].obj << ",\"vol\":" << hist[k].volume
      << ",\"peak\":" << hist[k].peak << ",\"sigma\":" << hist[k].sigma
      << ",\"cv\":" << hist[k].conVol << ",\"cp\":" << hist[k].conPeak
      << ",\"beta\":" << hist[k].beta << "}";
  }
  o << "],\"density\":" << jv(chain.gammaBar)
    << ",\"gamma\":" << jv(chain.gamma)
    << ",\"temperature\":" << jv(finalSt.T)
    << ",\"dual\":[";
  Vec dual = mma.lastDual();
  for (size_t i = 0; i < dual.size(); ++i) { if (i) o << ','; o << dual[i]; }
  o << "]}";
  return o.str();
}

// ---------------------------- shape ----------------------------------------
static std::string shape(const std::map<std::string, std::string>& a) {
  int dim = std::clamp(iarg(a, "dim", 4), 3, 8);
  ShapeObjective obj; obj.dim = dim;
  HybridOptimizer ho(obj, (unsigned)iarg(a, "seed", 42));
  HybridResult r = ho.run(iarg(a, "islands", 5), iarg(a, "pop", 14),
                          iarg(a, "generations", 45), iarg(a, "particles", 28),
                          iarg(a, "psoIter", 40));
  std::ostringstream o;
  o << "{\"mode\":\"shape\",\"dim\":" << dim
    << ",\"initialBest\":" << r.initialBest
    << ",\"afterMiga\":" << r.afterMiga << ",\"afterPso\":" << r.afterPso
    << ",\"afterNelderMead\":" << r.afterNelderMead << ",\"final\":" << r.final
    << ",\"improvement\":" << (r.initialBest - r.final)
    << ",\"evaluations\":" << r.evaluations
    << ",\"best\":" << jv(r.best)
    << ",\"migaHistory\":" << jv(r.historyMiga)
    << ",\"psoHistory\":" << jv(r.historyPso)
    << ",\"nmHistory\":" << jv(r.historyNm)
    << ",\"lbfgsHistory\":" << jv(r.historyLbfgs)
    << ",\"pareto\":[";
  for (size_t i = 0; i < r.paretoFront.size(); ++i) {
    if (i) o << ',';
    o << '[' << r.paretoFront[i].first << ',' << r.paretoFront[i].second << ']';
  }
  o << "]}";
  return o.str();
}

// ---------------------------- revolve --------------------------------------
static std::string revolveMode(const std::map<std::string, std::string>& a) {
  int steps = iarg(a, "steps", 64), slots = iarg(a, "slots", 5);
  Revolve rev(steps, slots);
  RevolveResult r = rev.schedule();
  std::ostringstream o;
  o << "{\"mode\":\"revolve\",\"steps\":" << steps << ",\"slots\":" << slots
    << ",\"forwardCalls\":" << r.forwardCalls << ",\"minimalForwardCalls\":" << rev.minimalForwardCalls()
    << ",\"recomputations\":" << r.recomputations << ",\"adjointCalls\":" << r.adjointCalls
    << ",\"maxSlotsUsed\":" << r.maxSlotsUsed
    << ",\"memoryReduction\":" << (double(steps) / std::max(1, (int)r.maxSlotsUsed))
    << ",\"trace\":[";
  int maxTrace = std::min((int)r.actions.size(), iarg(a, "trace", 140));
  for (int i = 0; i < maxTrace; ++i) {
    if (i) o << ',';
    o << jsonString(r.actions[i]);
  }
  o << "]}";
  return o.str();
}

// ---------------------------- fosm -----------------------------------------
static std::string fosmMode(const std::map<std::string, std::string>& a) {
  int nx = iarg(a, "nx", 10), ny = iarg(a, "ny", 8);
  ChainConfig cc = chainFromArgs(a);
  ThermalConfig tc = thermalFromArgs(a);
  CellGraph g(nx, ny);
  DesignChain chain(g, cc);
  ThermalModel model(g, cc, tc);
  Vec gamma = nominalGamma(g, 0.5);
  chain.forward(gamma);
  ThermalState st = model.primal(chain);
  double sigmaXi = darg(a, "sigmaXi", 0.05), ell = darg(a, "ell", 0.45);
  double kappa = darg(a, "kappa", 2.0), rhoKS = darg(a, "rhoKS", 6.0);
  auto Sigma = exponentialCovariance(g, sigmaXi, ell);
  Fosm fosm(g, model, chain);
  FosmResult meanRes = fosm.evaluate(st, Sigma, RobustFunctional::MeanTemperature, kappa, rhoKS);
  FosmResult peakRes = fosm.evaluate(st, Sigma, RobustFunctional::KSPeakTemperature, kappa, rhoKS);
  McCheck mc = monteCarloVarianceCheck(g, model, cc, gamma, Sigma, meanRes.gradSigma,
                                       meanRes.sigma, RobustFunctional::MeanTemperature,
                                       rhoKS, iarg(a, "samples", 4000), 99121u, 2e-2);
  std::ostringstream o;
  o << "{\"mode\":\"fosm\",\"cells\":" << g.n
    << ",\"meanMu\":" << meanRes.mu << ",\"meanSigma\":" << meanRes.sigma
    << ",\"meanPhi\":" << meanRes.phi << ",\"peakMu\":" << peakRes.mu
    << ",\"peakSigma\":" << peakRes.sigma << ",\"peakPhi\":" << peakRes.phi
    << ",\"correlationLength\":" << ell << ",\"sigmaXi\":" << sigmaXi
    << ",\"kappa\":" << kappa
    << ",\"gradientMean\":" << jv(meanRes.gradPhi)
    << ",\"gradientPeak\":" << jv(peakRes.gradPhi)
    << ",\"mcAnalytic\":" << mc.analytic << ",\"mcMeasured\":" << mc.monteCarlo
    << ",\"mcRelativeError\":" << mc.relativeError << ",\"mcSamples\":" << mc.samples
    << ",\"speedup\":" << (2.0 * mc.samples / meanRes.adjointSolves)
    << ",\"mcPass\":" << jb(mc.pass) << "}";
  return o.str();
}

int main(int argc, char** argv) {
  std::string mode = argc > 1 ? argv[1] : "verify";
  auto a = parseArgs(argc, argv);
  try {
    std::string out;
    if (mode == "verify") out = verify(a);
    else if (mode == "optimize") out = optimize(a);
    else if (mode == "shape") out = shape(a);
    else if (mode == "revolve") out = revolveMode(a);
    else if (mode == "fosm") out = fosmMode(a);
    else out = "{\"error\":\"unknown mode\"}";
    std::cout << out << '\n';
    return 0;
  } catch (const std::exception& e) {
    std::cout << "{\"error\":" << jsonString(e.what()) << "}\n";
    return 1;
  }
}
