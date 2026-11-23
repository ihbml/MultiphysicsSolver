// ============================================================================
//  PolyVV-20 :: engine driver — runs the full ASME V&V 20-2009 campaign and
//  exports REAL empirical results to public/results.json (consumed by the
//  interactive visualization app). No mocked curves anywhere.
//    g++ -std=c++20 -O2 engine/main.cpp -o engine/polyvv20
// ============================================================================
#include <chrono>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>
#include <cmath>
#include <algorithm>
#include "mesh.h"
#include "assembly.h"
#include "adjoint.h"
#include "audit.h"

using namespace vv;

static std::string fmt(double v) {
  if (!std::isfinite(v)) return "null";
  char buf[64];
  std::snprintf(buf, sizeof(buf), "%.12g", v);
  return buf;
}

struct Timer {
  std::chrono::steady_clock::time_point t0 = std::chrono::steady_clock::now();
  double sec() const {
    return std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
  }
};

struct Hist {
  std::vector<double> edges;
  std::vector<int> counts;
};
inline Hist makeHist(double lo, double hi, int nbins, const std::vector<double>& vals) {
  Hist h;
  h.counts.assign(nbins, 0);
  h.edges.resize(nbins + 1);
  for (int i = 0; i <= nbins; ++i) h.edges[i] = lo + (hi - lo) * i / nbins;
  for (double v : vals) {
    if (!std::isfinite(v)) continue;
    int b = (int)((v - lo) / (hi - lo) * nbins);
    b = std::clamp(b, 0, nbins - 1);
    h.counts[b]++;
  }
  return h;
}

struct LevelOut {
  DistortedMesh M;
  MeshCache C;
  // stats
  double cosMin = 1, cosMean = 0, skewMax = 0, skewMean = 0, kCorrMax = 0, betaDevMax = 0;
  double volMin = 0, volMax = 0;
  int cnt[4] = {0, 0, 0, 0};
  Hist histAngle, histCos, histSkew, histK;
};

static LevelOut collectLevel(const DistortedMesh& M, const MeshCache& C) {
  LevelOut L;
  L.M = M;  // copies (meshes are small enough at 8/16; N=32 ~ 36k nodes fine)
  L.C = C;
  double cosSum = 0, skewSum = 0;
  std::vector<double> angles, coss, skews, ks;
  L.volMin = 1e30; L.volMax = 0;
  for (const auto& c : M.cells) { L.volMin = std::min(L.volMin, c.volume); L.volMax = std::max(L.volMax, c.volume); }
  for (size_t fi = 0; fi < M.faces.size(); ++fi) {
    const Face& f = M.faces[fi];
    const FaceG& g = C.fg[fi];
    double ang = std::acos(std::clamp(f.cosTheta, -1.0, 1.0)) * 180.0 / PI;
    angles.push_back(ang); coss.push_back(f.cosTheta);
    double sk = g.mSkew.norm() / M.h; skews.push_back(sk);
    double kk = g.kCorr.norm() / std::max(g.S.norm(), 1e-30); ks.push_back(kk);
    L.cosMin = std::min(L.cosMin, f.cosTheta); cosSum += f.cosTheta;
    L.skewMax = std::max(L.skewMax, sk); skewSum += sk;
    L.kCorrMax = std::max(L.kCorrMax, kk);
    L.betaDevMax = std::max(L.betaDevMax, std::fabs(g.beta - 0.5));
    L.cnt[(int)f.cls]++;
  }
  L.cosMean = cosSum / M.faces.size();
  L.skewMean = skewSum / M.faces.size();
  L.histAngle = makeHist(0.0, 50.0, 25, angles);
  L.histCos = makeHist(-0.05, 1.0, 42, coss);
  L.histSkew = makeHist(0.0, 0.9, 36, skews);
  L.histK = makeHist(0.0, 1.0, 40, ks);
  return L;
}

struct MMSVariantOut {
  const char* id;
  const char* name;
  bool skew, corr, legacy;
  std::vector<double> L2, Linf;
  std::vector<int> iters;
  std::vector<double> relResid;
  double p12 = 0, p23 = 0, gci21L2 = 0, gci32L2 = 0, gci21Linf = 0, gci32Linf = 0;
};

static double gci(double eCoarse, double eFine, double p) {
  // ASME V&V 20-2009, Fs = 1.25 (3 grids), r = 2
  double eps = std::fabs(eCoarse - eFine) / std::max(eFine, 1e-300);
  double rp = std::pow(2.0, p) - 1.0;
  return (rp > 0.0) ? 1.25 * eps / rp : 0.0;
}

static void writeHist(std::ostringstream& j, const Hist& h) {
  j << "{\"edges\":[";
  for (size_t i = 0; i < h.edges.size(); ++i) { j << fmt(h.edges[i]); if (i + 1 < h.edges.size()) j << ","; }
  j << "],\"counts\":[";
  for (size_t i = 0; i < h.counts.size(); ++i) { j << h.counts[i]; if (i + 1 < h.counts.size()) j << ","; }
  j << "]}";
}

int main() {
  Timer total;
  const uint64_t SEED = 20240917ULL;
  std::ostringstream log;

  // ================= 1) MESH FAMILY (8^3, 16^3, 32^3) =================
  Timer tMesh;
  const int Ns[3] = {8, 16, 32};
  std::vector<LevelOut> levels;
  for (int N : Ns) {
    DistortedMesh M = calibrateAndBuild(N, SEED, 45.0, 0.55);
    MeshCache C = buildMeshCache(M);
    levels.push_back(collectLevel(M, C));
    std::cerr << "[mesh] N=" << N << " cells=" << M.cells.size() << " maxAngle=" << M.maxAngleDeg
              << "deg alphaUsed=" << M.alphaUsed << " invalidFaces=" << M.invalidFaces
              << " minVolRatio=" << M.minVolumeRatio << std::endl;
  }
  // viewer geometry: full N=8 mesh (small)
  const DistortedMesh& M8 = levels[0].M;

  // ================= 2) MMS CONVERGENCE STUDY =================
  Timer tMMS;
  struct VariantDef { const char* id; const char* name; bool legacy, skew, corr; };
  const VariantDef variantDefs[4] = {
    {"V0", "Legacy orthogonal operator (alpha=|S|/|d|, beta=1/2, k=0, m^skew=0)", true, false, false},
    {"V1", "Class-1 skewness m^skew only (k^corr omitted)", false, true, false},
    {"V2", "Non-orthogonal correction k^corr only (m^skew omitted)", false, false, true},
    {"V3", "Full: Class-1 skewness + implicit over-relaxed NOC (m^skew + k^corr)", false, true, true},
  };
  std::vector<MMSVariantOut> variantOuts;
  for (const auto& vd : variantDefs) {
    MMSVariantOut Vo;
    Vo.id = vd.id; Vo.name = vd.name; Vo.skew = vd.skew; Vo.corr = vd.corr; Vo.legacy = vd.legacy;
    int li = 0;
    for (int N : Ns) {
      const DistortedMesh& M = levels[li].M;
      const MeshCache& C = levels[li].C;
      QuadData Q = buildQuadData(M);
      GradientData G = buildGradientData(M, C.phiB);
      CorrectionFlags flags{vd.legacy, vd.skew, vd.corr};
      ScalarParams<double> sp{GAMMA_BASE, U0_BASE, SIGMA_BASE};
      Timer tSolve;
      Sys<double> S = assembleScalar<double>(M, G, C, Q, sp, flags);
      double L2 = -1, Linf = -1, relResid = -1;
      int iters = -1;
      if (N <= 8) {
        std::vector<double> A = tripletsToDense(S);
        std::vector<int> perm;
        if (!luFactor(A, S.n, perm)) { std::cerr << "LU fail\n"; continue; }
        std::vector<double> phi = luSolve(A, perm, S.n, S.rhs);
        std::vector<double> r(S.n);
        for (auto& [rr, cc, v] : S.triplets) r[rr] += v * phi[cc];
        double rn = 0, bn = 0; for (int i = 0; i < S.n; ++i) { r[i] -= S.rhs[i]; rn += r[i] * r[i]; bn += S.rhs[i] * S.rhs[i]; }
        relResid = std::sqrt(rn / std::max(bn, 1e-300));
        iters = -2;  // direct solve
        double se = 0, vc = 0; Linf = 0;
        for (size_t i = 0; i < M.cells.size(); ++i) {
          double e = phi[i] - Q.cells[i].phiStar;
          se += M.cells[i].volume * e * e; vc += M.cells[i].volume;
          Linf = std::max(Linf, std::fabs(e));
        }
        L2 = std::sqrt(se / vc);
      } else {
        CSR A = tripletsToCSR(S);
        BiCGResult br = bicgstab(A, S.rhs, 1e-13, 20000);
        relResid = br.relResid; iters = br.iters;
        std::vector<double> phi = br.x;
        double se = 0, vc = 0; Linf = 0;
        for (size_t i = 0; i < M.cells.size(); ++i) {
          double e = phi[i] - Q.cells[i].phiStar;
          se += M.cells[i].volume * e * e; vc += M.cells[i].volume;
          Linf = std::max(Linf, std::fabs(e));
        }
        L2 = std::sqrt(se / vc);
      }
      Vo.L2.push_back(L2); Vo.Linf.push_back(Linf);
      Vo.iters.push_back(iters); Vo.relResid.push_back(relResid);
      std::cerr << "[mms] variant=" << vd.id << " N=" << N << " L2=" << L2 << " Linf=" << Linf
                << " relResid=" << relResid << " iters=" << iters << " t=" << tSolve.sec() << "s" << std::endl;
      ++li;
    }
    Vo.p12 = std::log2(std::max(Vo.L2[0], 1e-30) / std::max(Vo.L2[1], 1e-30));
    Vo.p23 = std::log2(std::max(Vo.L2[1], 1e-30) / std::max(Vo.L2[2], 1e-30));
    Vo.gci21L2 = gci(Vo.L2[1], Vo.L2[2], Vo.p23);
    Vo.gci32L2 = gci(Vo.L2[0], Vo.L2[1], Vo.p12);
    Vo.gci21Linf = gci(Vo.Linf[1], Vo.Linf[2], Vo.p23);
    Vo.gci32Linf = gci(Vo.Linf[0], Vo.Linf[1], Vo.p12);
    variantOuts.push_back(Vo);
  }

  // ================= 3) ADJOINT VERIFICATION =================
  Timer tAdj;
  // scalar adjoint on N=8
  QuadData Q8 = buildQuadData(M8);
  GradientData G8 = buildGradientData(M8, levels[0].C.phiB);
  CorrectionFlags full{false, true, true};
  ScalarAdjointOut adjS = runScalarAdjoint(M8, G8, levels[0].C, Q8, full);
  std::cerr << "[adjoint:scalar] g_adj=(" << adjS.adjoint[0] << "," << adjS.adjoint[1] << "," << adjS.adjoint[2]
            << ") maxRelCS=" << *std::max_element(adjS.relErrCS.begin(), adjS.relErrCS.end())
            << " taylorSlope=" << adjS.taylor.meanSlope << std::endl;
  // Rhie-Chow Stokes on a lightly distorted N=6 mesh
  DistortedMesh Mrc = calibrateAndBuild(6, SEED ^ 0x5EED, 30.0, 0.35);
  MeshCache Crc = buildMeshCache(Mrc);
  QuadData Qrc = buildQuadData(Mrc);
  RCGradSet Grc = buildRCGradients(Mrc);
  RCSystemOut adjR = runRCAdjoint(Mrc, Grc, Crc, Qrc);
  std::cerr << "[adjoint:rc] g_adj=(" << adjR.adjoint[0] << "," << adjR.adjoint[1]
            << ") maxRelCS=" << std::max(adjR.relErrCS[0], adjR.relErrCS[1])
            << " taylorSlope=" << adjR.taylor.meanSlope << std::endl;

  // ================= 4) PATHOLOGY TORTURE SUITE =================
  Timer tTorture;
  AuditSummary audit = runTortureSuite();
  std::cerr << "[torture] " << audit.detected << "/" << audit.total << " defects detected, NaN count=" << audit.nanCount << std::endl;

  // ================= 5) JSON EXPORT =================
  std::ostringstream j;
  j.precision(12);
  j << "{\n";
  j << "\"meta\":{";
  j << "\"engine\":\"PolyVV-20 / ASME V&V 20-2009\",";
  j << "\"build\":\"g++ -std=c++20 -O2\",\"compiler\":\"" << __VERSION__ << "\",";
  j << "\"seed\":" << SEED << ",\"asme\":\"ASME V&V 20-2009\",";
  j << "\"gamma\":" << fmt(GAMMA_BASE) << ",\"u0\":" << fmt(U0_BASE) << ",\"sigma\":" << fmt(SIGMA_BASE) << ",";
  j << "\"grids\":[8,16,32],\"gciFs\":1.25,\"generatedUTC\":\"" << __DATE__ << " " << __TIME__ << "\"";
  j << "},\n";
  // ---- meshes ----
  j << "\"meshes\":{\"levels\":[\n";
  for (size_t li = 0; li < levels.size(); ++li) {
    const LevelOut& L = levels[li];
    const DistortedMesh& M = L.M;
    const MeshCache& C = L.C;
    (void)C;
    j << "{\"N\":" << M.N << ",\"h\":" << fmt(M.h) << ",\"cells\":" << M.cells.size()
      << ",\"faces\":" << M.faces.size() << ",\"nodes\":" << M.nodes.size()
      << ",\"alphaUsed\":" << fmt(M.alphaUsed) << ",\"maxAngleDeg\":" << fmt(M.maxAngleDeg)
      << ",\"minVolumeRatio\":" << fmt(M.minVolumeRatio) << ",\"invalidFaces\":" << M.invalidFaces
      << ",\"cosThetaMin\":" << fmt(L.cosMin) << ",\"cosThetaMean\":" << fmt(L.cosMean)
      << ",\"skewMaxRatio\":" << fmt(L.skewMax) << ",\"skewMeanRatio\":" << fmt(L.skewMean)
      << ",\"kCorrMaxRatio\":" << fmt(L.kCorrMax) << ",\"betaDevMax\":" << fmt(L.betaDevMax)
      << ",\"volMin\":" << fmt(L.volMin) << ",\"volMax\":" << fmt(L.volMax)
      << ",\"classCounts\":{\"ok\":" << L.cnt[0] << ",\"nonortho\":" << L.cnt[1]
      << ",\"severe\":" << L.cnt[2] << ",\"invalid\":" << L.cnt[3] << "},";
    j << "\"histAngle\":"; writeHist(j, L.histAngle); j << ",";
    j << "\"histCos\":"; writeHist(j, L.histCos); j << ",";
    j << "\"histSkew\":"; writeHist(j, L.histSkew); j << ",";
    j << "\"histKCorr\":"; writeHist(j, L.histK);
    j << "}" << (li + 1 < levels.size() ? ",\n" : "\n");
  }
  j << "],\n";
  // viewer payload (N=8): nodes, cells, faces(cos, cls)
  j << "\"viewer\":{\"N\":8,\"nodes\":[";
  for (size_t i = 0; i < M8.nodes.size(); ++i) {
    j << fmt(M8.nodes[i].x) << "," << fmt(M8.nodes[i].y) << "," << fmt(M8.nodes[i].z);
    if (i + 1 < M8.nodes.size()) j << ",";
  }
  j << "],\"cells\":[";
  for (size_t i = 0; i < M8.cells.size(); ++i) {
    const auto& v = M8.cells[i].v;
    j << "[";
    for (int q = 0; q < 8; ++q) { j << v[q]; if (q + 1 < 8) j << ","; }
    j << "]";
    if (i + 1 < M8.cells.size()) j << ",";
  }
  j << "],\"faces\":[";
  for (size_t i = 0; i < M8.faces.size(); ++i) {
    const Face& f = M8.faces[i];
    j << "{\"v\":[";
    for (size_t q = 0; q < f.v.size(); ++q) { j << f.v[q]; if (q + 1 < f.v.size()) j << ","; }
    j << "],\"cos\":" << fmt(f.cosTheta) << ",\"cls\":" << (int)f.cls << ",\"boundary\":" << (f.cellN < 0 ? "true" : "false") << "}";
    if (i + 1 < M8.faces.size()) j << ",";
  }
  j << "]}},\n";
  // ---- MMS ----
  j << "\"mms\":{\"reference\":{\"gamma\":" << fmt(GAMMA_BASE) << ",\"u0\":" << fmt(U0_BASE) << ",\"sigma\":" << fmt(SIGMA_BASE) << "},\"variants\":[\n";
  for (size_t vi = 0; vi < variantOuts.size(); ++vi) {
    const auto& V = variantOuts[vi];
    j << "{\"id\":\"" << V.id << "\",\"name\":\"" << V.name << "\",\"skew\":" << (V.skew ? "true" : "false")
      << ",\"corr\":" << (V.corr ? "true" : "false") << ",\"legacy\":" << (V.legacy ? "true" : "false")
      << ",\"L2\":[";
    for (size_t i = 0; i < V.L2.size(); ++i) { j << fmt(V.L2[i]); if (i + 1 < V.L2.size()) j << ","; }
    j << "],\"Linf\":[";
    for (size_t i = 0; i < V.Linf.size(); ++i) { j << fmt(V.Linf[i]); if (i + 1 < V.Linf.size()) j << ","; }
    j << "],\"solverIters\":[";
    for (size_t i = 0; i < V.iters.size(); ++i) { j << V.iters[i]; if (i + 1 < V.iters.size()) j << ","; }
    j << "],\"solverRelResid\":[";
    for (size_t i = 0; i < V.relResid.size(); ++i) { j << fmt(V.relResid[i]); if (i + 1 < V.relResid.size()) j << ","; }
    j << "],\"pObs\":[" << fmt(V.p12) << "," << fmt(V.p23) << "],"
      << "\"gci21L2\":" << fmt(V.gci21L2) << ",\"gci32L2\":" << fmt(V.gci32L2) << ","
      << "\"gci21Linf\":" << fmt(V.gci21Linf) << ",\"gci32Linf\":" << fmt(V.gci32Linf) << "}"
      << (vi + 1 < variantOuts.size() ? ",\n" : "\n");
  }
  j << "]},\n";
  // ---- adjoint ----
  auto dumpTaylor = [&](const TaylorResult& T) {
    j << "{\"eps\":[";
    for (size_t i = 0; i < T.epsList.size(); ++i) { j << fmt(T.epsList[i]); if (i + 1 < T.epsList.size()) j << ","; }
    j << "],\"resid\":[";
    for (size_t i = 0; i < T.resid.size(); ++i) {
      j << "[";
      for (size_t k = 0; k < T.resid[i].size(); ++k) { j << fmt(T.resid[i][k]); if (k + 1 < T.resid[i].size()) j << ","; }
      j << "]";
      if (i + 1 < T.resid.size()) j << ",";
    }
    j << "],\"slopes\":[";
    for (size_t i = 0; i < T.slope.size(); ++i) { j << fmt(T.slope[i]); if (i + 1 < T.slope.size()) j << ","; }
    j << "],\"meanSlope\":" << fmt(T.meanSlope) << ",\"minSlope\":" << fmt(T.minSlope)
      << ",\"maxResid\":" << fmt(T.maxResid) << "}";
  };
  j << "\"adjoint\":{\"hcs\":1e-30,\"scalar\":{";
  j << "\"thetaDesign\":[" << fmt(GAMMA_BASE) << "," << fmt(U0_BASE) << "," << fmt(SIGMA_BASE) << "],";
  j << "\"thetaTest\":[" << fmt(adjS.thetaTest[0]) << "," << fmt(adjS.thetaTest[1]) << "," << fmt(adjS.thetaTest[2]) << "],";
  j << "\"J0\":" << fmt(adjS.J0) << ",";
  auto dumpArr = [&](const std::array<double, 3>& a) {
    j << "[" << fmt(a[0]) << "," << fmt(a[1]) << "," << fmt(a[2]) << "]";
  };
  j << "\"adjoint\":"; dumpArr(adjS.adjoint); j << ",\"complexStep\":"; dumpArr(adjS.complexStep);
  j << ",\"fd\":"; dumpArr(adjS.fd); j << ",\"relErrCS\":"; dumpArr(adjS.relErrCS); j << ",\"relErrFD\":"; dumpArr(adjS.relErrFD);
  j << ",\"taylor\":"; dumpTaylor(adjS.taylor);
  j << "},\"rhieChow\":{";
  j << "\"mesh\":{\"N\":6,\"alphaUsed\":" << fmt(Mrc.alphaUsed) << ",\"maxAngleDeg\":" << fmt(Mrc.maxAngleDeg)
    << ",\"cells\":" << Mrc.cells.size() << ",\"dim\":" << adjR.dim << "},";
  j << "\"thetaDesign\":[" << fmt(adjR.thetaDesign[0]) << "," << fmt(adjR.thetaDesign[1]) << "],";
  j << "\"thetaTest\":[" << fmt(adjR.thetaTest[0]) << "," << fmt(adjR.thetaTest[1]) << "],";
  j << "\"J0\":" << fmt(adjR.J0) << ",";
  auto dumpArr2 = [&](const std::array<double, 2>& a) { j << "[" << fmt(a[0]) << "," << fmt(a[1]) << "]"; };
  j << "\"adjoint\":"; dumpArr2(adjR.adjoint); j << ",\"complexStep\":"; dumpArr2(adjR.complexStep);
  j << ",\"fd\":"; dumpArr2(adjR.fd); j << ",\"relErrCS\":"; dumpArr2(adjR.relErrCS); j << ",\"relErrFD\":"; dumpArr2(adjR.relErrFD);
  j << ",\"taylor\":"; dumpTaylor(adjR.taylor);
  j << "}},\n";
  // ---- torture ----
  j << "\"torture\":{\"defects\":[\n";
  for (size_t i = 0; i < audit.defects.size(); ++i) {
    const DefectResult& d = audit.defects[i];
    j << "{\"id\":\"" << d.id << "\",\"name\":\"" << d.name << "\",\"classification\":\"" << d.classification << "\","
      << "\"coord\":[" << fmt(d.coord[0]) << "," << fmt(d.coord[1]) << "," << fmt(d.coord[2]) << "],"
      << "\"detected\":" << (d.detected ? "true" : "false") << ",\"detector\":\"" << d.detector
      << "\",\"detail\":\"" << d.detail << "\"}" << (i + 1 < audit.defects.size() ? ",\n" : "\n");
  }
  j << "],\"summary\":{\"total\":" << audit.total << ",\"detected\":" << audit.detected
    << ",\"detectionRate\":" << fmt((double)audit.detected / std::max(audit.total, 1))
    << ",\"nanCount\":" << audit.nanCount << ",\"segfaults\":0,\"auditCompleted\":true}},\n";
  // ---- timing / environment ----
  j << "\"environment\":{";
  j << "\"compiler\":\"g++ (Debian 12.2.0)\",\"flags\":\"-std=c++20 -O2\",";
  j << "\"cmakeFree\":true,\"wallTimeSecTotal\":" << fmt(total.sec()) << ",";
  j << "\"wallTimeSecMesh\":" << fmt(tMesh.sec()) << ",\"wallTimeSecMMS\":" << fmt(tMMS.sec())
    << ",\"wallTimeSecAdjoint\":" << fmt(tAdj.sec()) << ",\"wallTimeSecTorture\":" << fmt(tTorture.sec());
  j << "}\n";
  j << "}\n";

  std::ofstream out("public/results.json");
  out << j.str();
  out.close();
  std::cerr << "[done] public/results.json written. Total wall time: " << total.sec() << "s\n";
  return 0;
}
