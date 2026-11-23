// main.cpp — Poly-Hexcore / Cut-Cell boundary-layer engine: verification driver.
// Build:  g++ -std=c++20 -O2 -o mesher/mesher mesher/main.cpp
// Run:    ./mesher/mesher     (writes src/data/*.json for the web inspector)
#include "core.h"
#include "bvh.h"
#include "quality.h"
#include "stromberg.h"
#include "geometry.h"
#include "engine.h"
#include "finalize.h"
#include "verify.h"
#include <sys/stat.h>
#include <cstdlib>

static int g_fail = 0;
#define CHECK(cond, msg)                                                       \
  do {                                                                         \
    bool ok = (cond);                                                          \
    std::printf("  [%s] %s\n", ok ? "PASS" : "FAIL", msg);                     \
    if (!ok) g_fail++;                                                         \
  } while (0)

// ---------------- unit tests of the quality kernel ----------------
static void unitTests(Stromberg &S) {
  std::printf("== unit tests: quality kernel & Stromberg classification ==\n");
  std::vector<Vec3> P;
  auto mk = [&](std::initializer_list<Vec3> l) {
    P.assign(l.begin(), l.end());
    std::vector<int> n(P.size());
    for (size_t i = 0; i < P.size(); i++) n[i] = (int)i;
    return n;
  };
  // ideal hex
  {
    auto n = mk({{0,0,0},{1,0,0},{1,1,0},{0,1,0},{0,0,1},{1,0,1},{1,1,1},{0,1,1}});
    double j = jensCell(0, P, n);
    CHECK(std::fabs(j - 1.0) < 1e-12, "ideal Hex8 J_ENS == 1");
  }
  // skewed hex
  {
    auto n = mk({{0,0,0},{1,0,0},{1,1,0},{0,1,0},{0.2,0.1,1},{1,0,1},{1,1,1},{0,1,1}});
    double j = jensCell(0, P, n);
    CHECK(j > 0 && j < 1, "skewed Hex8 0 < J_ENS < 1");
  }
  // inverted hex (corner pushed through)
  {
    auto n = mk({{0,0,0},{1,0,0},{1,1,0},{0,1,0},{0,0,1},{1,0,1},{1.5,1.5,-0.5},{0,1,1}});
    double j = jensCell(0, P, n);
    CHECK(j < 0, "inverted Hex8 J_ENS < 0");
  }
  // ideal prism
  {
    double s = std::sqrt(3.0) / 2.0;
    auto n = mk({{0,0,0},{1,0,0},{0.5,s,0},{0,0,1},{1,0,1},{0.5,s,1}});
    double j = jensCell(1, P, n);
    CHECK(std::fabs(j - 1.0) < 1e-12, "ideal Prism6 J_ENS == 1 (opt sqrt(3)/2)");
  }
  // inverted prism
  {
    double s = std::sqrt(3.0) / 2.0;
    auto n = mk({{0,0,0},{1,0,0},{0.5,s,0},{0,0,-1},{1,0,-1},{0.5,s,-1}});
    double j = jensCell(1, P, n);
    CHECK(j < 0, "inverted Prism6 J_ENS < 0");
  }
  // ideal tet
  {
    // regular tet, edge 1
    double h = std::sqrt(2.0 / 3.0);
    auto n = mk({{0,0,0},{1,0,0},{0.5,std::sqrt(3.0)/2,0},{0.5,std::sqrt(3.0)/6,h}});
    double j = jensCell(2, P, n);
    CHECK(std::fabs(j - 1.0) < 1e-12, "regular Tet4 J_ENS == 1 (opt sqrt(2)/2)");
  }
  // good pyramid
  {
    auto n = mk({{0,0,0},{1,0,0},{1,1,0},{0,1,0},{0.5,0.5,1}});
    double j = jensCell(3, P, n);
    CHECK(j > 0, "regular Pyr5 J_ENS > 0");
  }
  // folded (non-convex) pyramid base -> fictitious-hex check flags J_ENS <= 0
  {
    auto n = mk({{0,0,0},{1,0,0},{0.2,0.2,0},{0,1,0},{0.5,0.5,1}});
    double j = jensCell(3, P, n);
    CHECK(j <= 0, "folded Pyr5 base flagged J_ENS <= 0 (fictitious basis-hex check)");
  }
  // Gauss closure on a random-ish hex
  {
    auto n = mk({{0.1,0.2,0.05},{1.2,0.1,0.2},{1.1,1.3,0.1},{0.2,1.1,0.3},
                 {0.3,0.1,1.2},{1.1,0.2,1.1},{1.3,1.2,1.0},{0.1,1.2,1.3}});
    Mesh m;
    m.V = P;
    Mesh::Cell c; c.type = 0; c.n = n;
    evaluateCell(m, c);
    CHECK(c.vol > 0, "random hex positive volume");
    CHECK(c.gauss < 1e-14, "random hex Gauss closure < 1e-14");
  }
  // Stromberg: exactly 23 classes over all 256 masks
  CHECK(S.nClasses == 23, "Stromberg: 256 bitmasks -> exactly 23 ASP classes");
  {
    bool all = true;
    for (int m = 0; m < 256; m++) {
      int c = S.classify(m);
      if (c < 0 || c >= 23) all = false;
      // symmetry invariance: canonical mask maps to same class
      if (S.classify(S.canonical[c]) != c) all = false;
    }
    CHECK(all, "all D in {0..255} classified, classes closed under cube symmetries");
  }
}

// ---------------- JSON export ----------------
static void writeCaseJson(const Engine &E, const Surface *surf, const FaMesh *fa, const Report &R,
                          const BLParams &par, const std::string &path) {
  JsonW j;
  j.beginObj();
  j.key("name"); j.val(E.caseName);
  j.key("params");
  j.beginObj();
  j.key("dy1"); j.val(par.dy1);
  j.key("growth"); j.val(par.r);
  j.key("Nmax"); j.val(par.Nmax);
  j.key("medialFrac"); j.val(par.medialFrac);
  j.key("minDetJ"); j.val(par.minDetJ);
  j.key("visDeg"); j.val(par.visDeg);
  j.key("N"); j.val(E.N);
  j.endObj();
  j.key("vertices");
  j.beginArr();
  for (auto &p : E.mesh.V) {
    j.beginArr();
    j.val(p.x); j.val(p.y); j.val(p.z);
    j.endArr();
  }
  j.endArr();
  // per-surface-vertex info (BL columns)
  j.key("vinfo");
  j.beginArr();
  for (int v = 0; v < (int)surf->V.size(); v++) {
    j.beginObj();
    j.key("active"); j.val(E.blActive[v] != 0);
    j.key("h"); j.val(E.Htot[v]);
    j.key("dmed"); j.val(E.dMed[v]);
    j.key("n"); j.beginArr(); j.val(E.nHat[v].x); j.val(E.nHat[v].y); j.val(E.nHat[v].z); j.endArr();
    j.endObj();
  }
  j.endArr();
  j.key("cells");
  j.beginArr();
  for (auto &c : E.mesh.cells) {
    j.beginObj();
    j.key("t"); j.val(c.type);
    j.key("n"); j.beginArr(); for (int id : c.n) j.val(id); j.endArr();
    j.key("j"); j.val(c.jens);
    j.key("v"); j.val(c.vol);
    j.key("g"); j.val(c.gauss);
    j.key("p"); j.val(c.patch);
    j.key("l"); j.val(c.layer);
    j.endObj();
  }
  j.endArr();
  j.key("surfTris");
  j.beginArr();
  for (int t = 0; t < (int)surf->T.size(); t++) {
    j.beginObj();
    j.key("n"); j.beginArr();
    j.val(surf->T[t][0]); j.val(surf->T[t][1]); j.val(surf->T[t][2]);
    j.endArr();
    j.key("p"); j.val(surf->patch[t]);
    j.key("w"); j.val(surf->isWall[t] != 0);
    j.endObj();
  }
  j.endArr();
  j.key("patchNames"); j.beginArr(); for (auto &s : E.mesh.patchNames) j.val(s); j.endArr();
  if (fa) {
    j.key("faMesh");
    j.beginObj();
    j.key("faces");
    j.beginArr();
    for (auto &f : fa->faces) {
      j.beginObj();
      j.key("tri"); j.val(f.tri);
      j.key("owner"); j.val(f.owner);
      j.key("area"); j.val(f.area);
      j.key("c"); j.beginArr(); j.val(f.c.x); j.val(f.c.y); j.val(f.c.z); j.endArr();
      j.key("n"); j.beginArr(); j.val(f.n.x); j.val(f.n.y); j.val(f.n.z); j.endArr();
      j.key("W"); j.beginArr();
      for (int i = 0; i < 3; i++) { j.beginArr(); j.val(f.W[i][0]); j.val(f.W[i][1]); j.val(f.W[i][2]); j.endArr(); }
      j.endArr();
      j.key("k1"); j.val(f.k1);
      j.key("k2"); j.val(f.k2);
      j.key("H"); j.val(f.H);
      j.endObj();
    }
    j.endArr();
    j.key("edges");
    j.beginArr();
    for (auto &e : fa->edges) {
      j.beginObj();
      j.key("f1"); j.val(e.f1);
      j.key("f2"); j.val(e.f2);
      j.key("le"); j.beginArr(); j.val(e.le.x); j.val(e.le.y); j.val(e.le.z); j.endArr();
      j.key("len"); j.val(e.len);
      j.key("dgeo"); j.val(e.dgeo);
      j.endObj();
    }
    j.endArr();
    j.endObj();
  }
  j.key("report");
  j.beginObj();
  j.key("nVerts"); j.val(R.nVerts);
  j.key("nCells"); j.val(R.nCells);
  j.key("nHex"); j.val(R.nHex);
  j.key("nPrism"); j.val(R.nPrism);
  j.key("minVol"); j.val(R.minVol);
  j.key("minJens"); j.val(R.minJens);
  j.key("maxGauss"); j.val(R.maxGauss);
  j.key("boundaryFaces"); j.val(R.boundaryFaces);
  j.key("interiorFaces"); j.val(R.interiorFaces);
  j.key("badFaces"); j.val(R.badFaces);
  j.key("hangingNodes"); j.val(R.hangingNodes);
  j.key("conformal"); j.val(R.conformal);
  j.key("N"); j.val(R.N);
  j.key("dMedMin"); j.val(R.dMedMin);
  j.key("dMedMax"); j.val(R.dMedMax);
  j.key("Hmin"); j.val(R.Hmin);
  j.key("Hmax"); j.val(R.Hmax);
  j.key("nRetracted"); j.val(R.nRetracted);
  j.key("strombergClasses"); j.val(R.strombergClasses);
  j.key("bgHexes"); j.val(R.bgHexes);
  j.key("bgMixed"); j.val(R.bgMixed);
  j.key("maxRV"); j.val(R.maxRV);
  j.key("faFaces"); j.val(R.faFaces);
  j.key("faEdges"); j.val(R.faEdges);
  j.key("faArea"); j.val(R.faArea);
  j.key("faOwnerBijective"); j.val(R.faOwnerBijective);
  j.endObj();
  j.endObj();
  std::ofstream f(path);
  f << j.str();
  f.close();
  std::printf("  wrote %s (%zu bytes)\n", path.c_str(), j.str().size());
}

static void writeVerificationJson(const std::vector<Report> &reports, const std::string &path) {
  JsonW j;
  j.beginObj();
  j.key("unitTestsFailed"); j.val(g_fail);
  j.key("cases");
  j.beginArr();
  for (auto &R : reports) {
    j.beginObj();
    j.key("name"); j.val(R.caseName);
    j.key("nCells"); j.val(R.nCells);
    j.key("nHex"); j.val(R.nHex);
    j.key("nPrism"); j.val(R.nPrism);
    j.key("minVol"); j.val(R.minVol);
    j.key("minVolPositive"); j.val(R.minVol > 0);
    j.key("minJens"); j.val(R.minJens);
    j.key("minJensAboveQuarter"); j.val(R.minJens > 0.25);
    j.key("maxGauss"); j.val(R.maxGauss);
    j.key("gaussBelow1e-14"); j.val(R.maxGauss < 1e-14);
    j.key("hangingNodes"); j.val(R.hangingNodes);
    j.key("conformal"); j.val(R.conformal);
    j.key("N"); j.val(R.N);
    j.key("dMedMin"); j.val(R.dMedMin);
    j.key("Hmax"); j.val(R.Hmax);
    j.key("nRetracted"); j.val(R.nRetracted);
    j.key("nDetRetract"); j.val(R.nDetRetract);
    j.key("strombergClasses"); j.val(R.strombergClasses);
    j.key("bgMixed"); j.val(R.bgMixed);
    j.key("maxRV"); j.val(R.maxRV);
    j.key("faFaces"); j.val(R.faFaces);
    j.key("faEdges"); j.val(R.faEdges);
    j.key("faOwnerBijective"); j.val(R.faOwnerBijective);
    j.endObj();
  }
  j.endArr();
  j.endObj();
  std::ofstream f(path);
  f << j.str();
  std::printf("  wrote %s\n", path.c_str());
}

static void printReport(const Report &R) {
  std::printf("  cells=%d (hex=%d prism=%d) verts=%d\n", R.nCells, R.nHex, R.nPrism, R.nVerts);
  std::printf("  min V_c = %.6e  (>0: %s)\n", R.minVol, R.minVol > 0 ? "YES" : "NO");
  std::printf("  min J_ENS = %.6f  (>0.25: %s)\n", R.minJens, R.minJens > 0.25 ? "YES" : "NO");
  std::printf("  max ||sum S_f||/V^(2/3) = %.3e  (<1e-14: %s)\n", R.maxGauss, R.maxGauss < 1e-14 ? "YES" : "NO");
  std::printf("  faces: boundary=%d interior=%d bad=%d  hanging nodes=%d  conformal=%s\n",
              R.boundaryFaces, R.interiorFaces, R.badFaces, R.hangingNodes,
              R.conformal ? "YES" : "NO");
  std::printf("  layers N=%d dy1=%.2e r=%.2f  d_medial=[%.4e,%.4e]  H=[%.4e,%.4e] retracted=%d\n",
              R.N, R.dy1, R.r, R.dMedMin, R.dMedMax, R.Hmin, R.Hmax, R.nRetracted);
  std::printf("  Stromberg: classes=%d bgHexes=%d mixed=%d maxRV=%.3e | faMesh faces=%d edges=%d bijective=%s\n",
              R.strombergClasses, R.bgHexes, R.bgMixed, R.maxRV, R.faFaces, R.faEdges,
              R.faOwnerBijective ? "YES" : "NO");
}

int main() {
  mkdir("src/data", 0755);
  Stromberg S;
  S.init();
  unitTests(S);
  std::vector<Report> reports;

  // ================= case 1: narrow twin-plate cooling gap =================
  {
    std::printf("\n== case 1: twin-plate cooling gap (Lx=0.08, gap=0.02, Lz=0.05) ==\n");
    TwinGap g(0.08, 0.02, 0.05, 16, 4, 10);
    Engine E;
    BLParams p;
    p.dy1 = 0.0005; p.r = 1.25; p.Nmax = 6;
    E.setup(&g, "twin_gap", p);
    E.pseudonormals();
    E.smooth();
    E.medial();
    E.extrude();
    coreTwinGap(E, g);
    if (getenv("MESHER_DEBUG")) {
      double lo[3] = {1e9,1e9,1e9}, hi[3] = {-1e9,-1e9,-1e9};
      for (int v = 0; v < 546; v++) for (int a = 0; a < 3; a++) {
        lo[a] = std::min(lo[a], E.mesh.V[v][a]); hi[a] = std::max(hi[a], E.mesh.V[v][a]);
      }
      std::fprintf(stderr, "[dbg] lattice bbox lo=(%.4f,%.4f,%.4f) hi=(%.4f,%.4f,%.4f)\n", lo[0],lo[1],lo[2],hi[0],hi[1],hi[2]);
      // col top of vertex 443's neighbors: print col tops of a few boundary verts
      {
        int i = 1, j = 4, k = 5, nx = 12, ny = 5, nz = 6;
        auto gid2 = [&](int a, int b, int c) { return (c * (ny + 1) + b) * (nx + 1) + a; };
        auto top2 = [&](int a, int b, int c) { return E.mesh.V[E.col[gid2(a,b,c)][E.N]]; };
        auto pr = [&](const char *nm, Vec3 p) { std::fprintf(stderr, "[dbg] %s = (%.5f,%.5f,%.5f)\n", nm, p.x, p.y, p.z); };
        pr("Fx0", top2(0,j,k)); pr("Fx1", top2(nx,j,k)); pr("Fy0", top2(i,0,k)); pr("Fy1", top2(i,ny,k));
        pr("Fz0", top2(i,j,0)); pr("Fz1", top2(i,j,nz));
        pr("c000", top2(0,0,0)); pr("c100", top2(nx,0,0)); pr("c010", top2(0,ny,0)); pr("c110", top2(nx,ny,0));
        pr("c001", top2(0,0,nz)); pr("c101", top2(nx,0,nz)); pr("c011", top2(0,ny,nz)); pr("c111", top2(nx,ny,nz));
        pr("P443", E.mesh.V[443]);
      }
      for (int v : {0, 1, 13, 78, 443}) {
        std::fprintf(stderr, "[dbg] v%d blActive=%d colsize=%zu tops:", v, (int)E.blActive[v], E.col[v].size());
        for (int id : E.col[v]) { auto &p = E.mesh.V[id]; std::fprintf(stderr, " (%.4f,%.4f,%.4f)", p.x, p.y, p.z); }
        std::fprintf(stderr, "\n");
      }
    }
    int pBot = 0, pTop = 1; // patch ids per patchNames order
    FaMesh fa = extractFaMesh(E, {pBot, pTop});
    Report R = verify(E, &fa);
    if (getenv("MESHER_DEBUG")) {
      int worst = 0;
      for (int i = 0; i < (int)E.mesh.cells.size(); i++)
        if (E.mesh.cells[i].jens < E.mesh.cells[worst].jens) worst = i;
      auto &c = E.mesh.cells[worst];
      std::fprintf(stderr, "[dbg] worst cell %d type=%d layer=%d patch=%d jens=%.4f vol=%.3e gauss=%.2e\n", worst, c.type, c.layer, c.patch, c.jens, c.vol, c.gauss);
      for (int id : c.n) { auto &p = E.mesh.V[id]; std::fprintf(stderr, "   v%d (%.6f,%.6f,%.6f)\n", id, p.x, p.y, p.z); }
      int bad = 0;
      for (auto &cc : E.mesh.cells) if (cc.jens < 0.25) bad++;
      std::fprintf(stderr, "[dbg] cells with JENS<0.25: %d / %d\n", bad, (int)E.mesh.cells.size());
      for (auto &cc : E.mesh.cells) if (cc.jens < 0.25) { std::fprintf(stderr, "   bad: type=%d layer=%d patch=%d jens=%.4f\n", cc.type, cc.layer, cc.patch, cc.jens); break; }
    }
    auto inside = [](const Vec3 &p) {
      return p.x >= 0 && p.x <= 0.08 && p.y >= 0 && p.y <= 0.02 && p.z >= 0 && p.z <= 0.05;
    };
    runStromberg(E, S, inside, 0.004, R);
    printReport(R);
    CHECK(R.minVol > 0, "twin_gap: 0 negative volumes");
    CHECK(R.minJens > 0.25, "twin_gap: min J_ENS > 0.25");
    CHECK(R.maxGauss < 1e-14, "twin_gap: Gauss closure < 1e-14");
    CHECK(R.hangingNodes == 0 && R.conformal, "twin_gap: 0 hanging nodes, conformal");
    reports.push_back(R);
    writeCaseJson(E, &g, &fa, R, p, "src/data/twin_gap.json");
  }

  // ================= case 2: stepped L-channel (reflex 270 deg corner) =================
  {
    std::printf("\n== case 2: stepped L-channel (Lx=0.06,H=0.04,w=0.02,h1=0.015,D=0.03) ==\n");
    LChannel g(0.06, 0.04, 0.02, 0.015, 0.03, 5, {10, 5, 10, 5, 5, 5});
    Engine E;
    BLParams p;
    p.dy1 = 0.0008; p.r = 1.25; p.Nmax = 5;
    E.setup(&g, "l_channel", p);
    E.pseudonormals();
    E.smooth();
    E.medial();
    E.extrude();
    coreLChannel(E, g);
    int pLat = 0;
    FaMesh fa = extractFaMesh(E, {pLat});
    Report R = verify(E, &fa);
    if (getenv("MESHER_DEBUG")) {
      int worst = 0;
      for (int i = 0; i < (int)E.mesh.cells.size(); i++)
        if (E.mesh.cells[i].jens < E.mesh.cells[worst].jens) worst = i;
      auto &c = E.mesh.cells[worst];
      std::fprintf(stderr, "[dbg] worst cell %d type=%d layer=%d patch=%d jens=%.4f vol=%.3e gauss=%.2e\n", worst, c.type, c.layer, c.patch, c.jens, c.vol, c.gauss);
      for (int id : c.n) { auto &p = E.mesh.V[id]; std::fprintf(stderr, "   v%d (%.6f,%.6f,%.6f)\n", id, p.x, p.y, p.z); }
      int bad = 0;
      for (auto &cc : E.mesh.cells) if (cc.jens < 0.25) bad++;
      std::fprintf(stderr, "[dbg] cells with JENS<0.25: %d / %d\n", bad, (int)E.mesh.cells.size());
      for (auto &cc : E.mesh.cells) if (cc.jens < 0.25) { std::fprintf(stderr, "   bad: type=%d layer=%d patch=%d jens=%.4f\n", cc.type, cc.layer, cc.patch, cc.jens); break; }
    }
    auto inside = [](const Vec3 &p) {
      if (p.z < 0 || p.z > 0.03) return false;
      if (p.x < 0 || p.x > 0.06 || p.y < 0 || p.y > 0.04) return false;
      return !(p.x > 0.02 && p.y > 0.015); // removed step block
    };
    runStromberg(E, S, inside, 0.004, R);
    printReport(R);
    CHECK(R.minVol > 0, "l_channel: 0 negative volumes");
    CHECK(R.minJens > 0.25, "l_channel: min J_ENS > 0.25");
    CHECK(R.maxGauss < 1e-14, "l_channel: Gauss closure < 1e-14");
    CHECK(R.hangingNodes == 0 && R.conformal, "l_channel: 0 hanging nodes, conformal");
    reports.push_back(R);
    writeCaseJson(E, &g, &fa, R, p, "src/data/l_channel.json");
  }

  writeVerificationJson(reports, "src/data/verification.json");
  std::printf("\n%s (unit test failures: %d)\n", g_fail == 0 ? "ALL CHECKS PASSED" : "SOME CHECKS FAILED", g_fail);
  return g_fail == 0 ? 0 : 1;
}
