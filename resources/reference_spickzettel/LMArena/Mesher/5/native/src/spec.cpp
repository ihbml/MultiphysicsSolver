// Specification parsing: turns the JSON job description into a typed Spec.
#include <cmath>

#include "core.hpp"

namespace mesher {

namespace {

V2 vecOf(const mj::Value& v, const V2& def) {
  if (!v.isArr() || v.size() < 2) return def;
  return V2(v.vecElem(0, def.x), v.vecElem(1, def.y));
}

BoundaryRule parseRule(const mj::Value& r) {
  BoundaryRule out;
  out.name = r.at("name").asString("wall");
  out.bc = r.at("bc").asString(out.name);
  out.where = r.at("where").asString("all");
  out.a = vecOf(r.at("from"), vecOf(r.at("center"), V2(0, 0)));
  out.b = vecOf(r.at("to"), V2(0, 0));
  out.radius = r.at("radius").asDouble(0.0);
  out.priority = r.at("priority").asInt(0);
  return out;
}

RefineTarget parseTarget(const mj::Value& t) {
  RefineTarget out;
  const std::string shape = t.at("shape").asString(t.at("kind").asString("point"));
  if (shape == "circle") out.kind = 1;
  else if (shape == "box") out.kind = 2;
  else out.kind = 0;
  out.c = vecOf(t.at("center"), V2(0, 0));
  out.r = t.at("radius").asDouble(0.0);
  out.lo = vecOf(t.at("min"), V2(0, 0));
  out.hi = vecOf(t.at("max"), V2(0, 0));
  out.hNear = t.at("hNear").asDouble(t.at("size").asDouble(0.0));
  out.width = t.at("width").asDouble(0.0);
  out.level = t.at("level").asInt(t.at("levels").asInt(1));
  return out;
}

std::vector<std::string> parseStringArray(const mj::Value& v, std::vector<std::string> def) {
  std::vector<std::string> out;
  if (const auto* a = v.asArray()) {
    for (const auto& e : *a) out.push_back(e.asString());
    return out;
  }
  if (v.type == mj::Type::String) {
    out.push_back(v.str);
    return out;
  }
  return def;
}

}  // namespace

int bcTypeFromName(const std::string& s) {
  if (s == "noslip" || s == "wall" || s == "wall_noslip" || s == "nonSlip") return 0;
  if (s == "slip" || s == "reflecting" || s == "freeSlip" || s == "wall_slip") return 1;
  if (s == "inflow" || s == "inlet" || s == "velocityInlet") return 2;
  if (s == "outflow" || s == "outlet" || s == "pressureOutlet") return 3;
  if (s == "farfield") return 4;
  if (s == "nonreflecting" || s == "nonReflecting" || s == "nscbc") return 5;
  if (s == "symmetry" || s == "axis") return 6;
  if (s == "moving" || s == "movingWall") return 7;
  return 0;
}

std::string bcNameFromType(int t) {
  switch (t) {
    case 0: return "wall";
    case 1: return "reflecting";
    case 2: return "inflow";
    case 3: return "outflow";
    case 4: return "farfield";
    case 5: return "nonreflecting";
    case 6: return "symmetry";
    case 7: return "moving";
    default: return "wall";
  }
}

Spec parseSpec(const mj::Value& root) {
  Spec spec;
  spec.raw = root;
  spec.name = root.at("name").asString(root.at("case").asString("case"));

  // ---- geometry ---------------------------------------------------------
  spec.geomJson = root.at("geometry");
  if (spec.geomJson.isNull()) spec.geomJson = mj::Value::object();
  spec.geom = buildSdf(spec.geomJson);
  AABB b = sdfBounds(spec.geomJson);
  const mj::Value dom = root.at("domain");
  if (!dom.isNull()) {
    const V2 lo = vecOf(dom.at("min"), V2(-1e30, -1e30));
    const V2 hi = vecOf(dom.at("max"), V2(1e30, 1e30));
    b.lo = V2(std::max(b.lo.x, lo.x), std::max(b.lo.y, lo.y));
    b.hi = V2(std::min(b.hi.x, hi.x), std::min(b.hi.y, hi.y));
  }
  if (!b.valid() || std::fabs(b.hi.x) > 1e20 || std::fabs(b.hi.y) > 1e20 ||
      (b.hi.x - b.lo.x) < 1e-12) {
    b.lo = V2(-1, -1);
    b.hi = V2(1, 1);
  }
  spec.domain = b;

  // ---- FVM --------------------------------------------------------------
  const mj::Value f = root.at("fvm");
  FvmSpec& fs = spec.fvm;
  fs.enabled = f.at("enabled").asBool(true);
  fs.base = f.at("baseCellSize").asDouble(f.at("cellSize").asDouble(0.05));
  fs.minSize = f.at("minCellSize").asDouble(0.0);
  if (fs.minSize <= 0) fs.minSize = fs.base * 0.0625;
  fs.minSize = std::max(fs.minSize, fs.base * 1e-3);
  fs.contourSamples = f.at("contourSamples").asInt(0);
  fs.smoothIters = f.at("smoothingIters").asInt(30);
  fs.smoothWeight = f.at("smoothingWeight").asDouble(0.5);
  const mj::Value bl = f.at("boundaryLayer");
  fs.blCount = bl.at("count").asInt(bl.asInt(0));
  fs.blFirst = bl.at("firstHeight").asDouble(0.0);
  fs.blGrowth = bl.at("growth").asDouble(1.2);
  fs.blPatch = bl.at("patch").asString("wall");
  fs.blOnlyMatching = bl.at("onlyMatchingPatch").asBool(true);
  fs.vertexDual = f.at("vertexDual").asBool(false);
  if (const auto* a = f.at("refinement").at("targets").asArray())
    for (const auto& t : *a) fs.targets.push_back(parseTarget(t));
  if (const auto* a = f.at("patchRules").asArray())
    for (const auto& r : *a) fs.rules.push_back(parseRule(r));
  if (fs.rules.empty()) {
    BoundaryRule d;
    d.name = "wall";
    d.bc = "wall";
    d.where = "all";
    fs.rules.push_back(d);
  }
  std::stable_sort(fs.rules.begin(), fs.rules.end(),
                   [](const BoundaryRule& a, const BoundaryRule& b) { return a.priority > b.priority; });

  // ---- SPH --------------------------------------------------------------
  const mj::Value sp = root.at("sph");
  SphSpec& ss = spec.sph;
  ss.enabled = sp.at("enabled").asBool(true);
  ss.dp = sp.at("particleSpacing").asDouble(sp.at("dp").asDouble(0.05));
  const std::string kn = sp.at("kernel").asString("wendlandC2");
  ss.kernel = (kn == "cubicSpline" || kn == "cubic") ? 1 : (kn == "quartic" ? 2 : 0);
  ss.hRatio = sp.at("supportRatio").asDouble(sp.at("hRatio").asDouble(2.6));
  const mj::Value rx = sp.at("relaxation");
  ss.relax = rx.at("enabled").asBool(true);
  ss.relaxIters = rx.at("iterations").asInt(400);
  ss.relaxCoeff = rx.at("coefficient").asDouble(0.35);
  ss.snapBoundary = rx.at("boundarySnap").asBool(true);
  ss.bandFactor = rx.at("bandFactor").asDouble(0.6);
  ss.levels = std::max(1, sp.at("levels").asInt(1));
  const mj::Value lv = sp.at("levelField");
  const std::string lk = lv.at("kind").asString("off");
  ss.levelKind = (lk == "distance") ? 1 : (lk == "core" ? 2 : 0);
  ss.levelRadius = lv.at("radius").asDouble(0.0);
  if (const auto* a = lv.at("targets").asArray())
    for (const auto& t : *a) ss.levelTargets.push_back(parseTarget(t));
  const std::string vm = sp.at("volumeModel").asString("nominal");
  ss.volumeModel = (vm == "shepard") ? 1 : 0;
  ss.gradientCorrection = sp.at("gradientCorrection").asBool(true);
  ss.gammaFactor = sp.at("gammaFactor").asDouble(0.75);
  ss.boundaryLayers = sp.at("boundaryLayers").asInt(2);

  // ---- coupling ---------------------------------------------------------
  const mj::Value c = root.at("coupling");
  CouplingSpec& cs = spec.cpl;
  cs.enabled = c.at("enabled").asBool(false);
  cs.mode = c.at("mode").asString("adaptive");
  const std::string ck = c.at("coreRegion").at("shape").asString("circle");
  cs.coreKind = ck == "box" ? 2 : (ck == "all" ? 3 : 1);
  cs.coreCenter = vecOf(c.at("coreRegion").at("center"), spec.domain.center());
  cs.coreRadius = c.at("coreRegion").at("radius").asDouble(0.25 * norm(spec.domain.size()));
  cs.coreLo = vecOf(c.at("coreRegion").at("min"), spec.domain.lo);
  cs.coreHi = vecOf(c.at("coreRegion").at("max"), spec.domain.hi);
  cs.buffer = c.at("bufferWidth").asDouble(0.0);
  cs.p2m = c.at("particleToMesh").asString("opmis");
  cs.m2p = c.at("meshToParticle").asString("opmis");
  const mj::Value it = c.at("interpolation");
  if (!it.isNull()) {
    cs.p2m = it.at("particleToMesh").asString(cs.p2m);
    cs.m2p = it.at("meshToParticle").asString(cs.m2p);
    cs.momentCorrection = it.at("momentCorrection").asBool(true);
    cs.quadrature = it.at("quadrature").asInt(3);
  }
  cs.patchTest = c.at("patchTest").asBool(true);

  // ---- output -----------------------------------------------------------
  const mj::Value o = root.at("output");
  spec.out.dir = o.at("dir").asString(".");
  spec.out.formats = parseStringArray(o.at("formats"), {"vtu", "msh", "json"});
  spec.out.previewLimit = o.at("previewLimit").asInt(20000);
  spec.out.preview = o.at("preview").asBool(true);
  spec.threads = root.at("threads").asInt(0);
  return spec;
}

}  // namespace mesher
