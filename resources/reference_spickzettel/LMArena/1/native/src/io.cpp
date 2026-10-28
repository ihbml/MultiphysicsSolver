// Exporters: VTK/VTU (ParaView), Gmsh 2.2 .msh (SPHinXsys FVM parser), and
// JSON payloads with the full DiFVM-style static graph arrays.
#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <sstream>

#include "core.hpp"

namespace fs = std::filesystem;

namespace mesher {
namespace {

void writeNum(std::ostream& os, Real v) {
  if (!std::isfinite(v)) v = 0;
  os << v;
}

long long fileSize(const std::string& p) {
  std::error_code ec;
  const auto s = fs::file_size(p, ec);
  return ec ? 0 : static_cast<long long>(s);
}

void vtuDataArray(std::ostream& os, const std::string& name, const std::string& type, int comps,
                  const std::function<void(std::ostream&)>& body) {
  os << "<DataArray type=\"" << type << "\" Name=\"" << name << "\" NumberOfComponents=\"" << comps
     << "\" format=\"ascii\">\n";
  body(os);
  os << "</DataArray>\n";
}

}  // namespace

// ------------------------------------------------------------------- VTU -----
void writeVtuMesh(const Mesh& m, const Coupling& c, const std::string& path) {
  std::ofstream os(path);
  if (!os) return;
  os << "<?xml version=\"1.0\"?>\n<VTKFile type=\"UnstructuredGrid\" version=\"0.1\" byte_order=\"LittleEndian\">\n";
  os << "<UnstructuredGrid>\n<Piece NumberOfPoints=\"" << m.nodes.size() << "\" NumberOfCells=\""
     << m.cells.size() << "\">\n";
  os << "<PointData>\n";
  if (!m.dualVol.empty())
    vtuDataArray(os, "dualVolume", "Float64", 1, [&](std::ostream& o) {
      for (size_t i = 0; i < m.nodes.size(); ++i) {
        writeNum(o, m.dualVol[i]);
        o << "\n";
      }
    });
  vtuDataArray(os, "boundaryNode", "Int32", 1, [&](std::ostream& o) {
    for (size_t i = 0; i < m.nodes.size(); ++i) o << (m.patchNodeLoop.empty() ? 0 : m.patchNodeLoop[i]) << "\n";
  });
  os << "</PointData>\n<CellData>\n";
  vtuDataArray(os, "region", "Int32", 1, [&](std::ostream& o) {
    for (size_t i = 0; i < m.cells.size(); ++i)
      o << (c.cellRegion.empty() ? 0 : c.cellRegion[i]) << "\n";
  });
  vtuDataArray(os, "couplingInterface", "Int32", 1, [&](std::ostream& o) {
    for (size_t i = 0; i < m.cells.size(); ++i)
      o << (c.cellCoupling.empty() ? 0 : c.cellCoupling[i]) << "\n";
  });
  vtuDataArray(os, "particleVolumeFraction", "Float64", 1, [&](std::ostream& o) {
    for (size_t i = 0; i < m.cells.size(); ++i) {
      writeNum(o, c.cellVolumeFrac.empty() ? 0.0 : c.cellVolumeFrac[i]);
      o << "\n";
    }
  });
  vtuDataArray(os, "volume", "Float64", 1, [&](std::ostream& o) {
    for (size_t i = 0; i < m.cells.size(); ++i) {
      writeNum(o, m.vol[i]);
      o << "\n";
    }
  });
  os << "</CellData>\n";
  os << "<Points>\n";
  vtuDataArray(os, "points", "Float64", 3, [&](std::ostream& o) {
    for (size_t i = 0; i < m.nodes.size(); ++i) {
      writeNum(o, m.nodes[i].x);
      o << " ";
      writeNum(o, m.nodes[i].y);
      o << " 0\n";
    }
  });
  os << "</Points>\n<Cells>\n";
  vtuDataArray(os, "connectivity", "Int32", 1, [&](std::ostream& o) {
    for (size_t i = 0; i < m.cells.size(); ++i)
      o << m.cells[i][0] << " " << m.cells[i][1] << " " << m.cells[i][2] << "\n";
  });
  vtuDataArray(os, "offsets", "Int32", 1, [&](std::ostream& o) {
    for (size_t i = 0; i < m.cells.size(); ++i) o << (3 * (i + 1)) << "\n";
  });
  vtuDataArray(os, "types", "UInt8", 1, [&](std::ostream& o) {
    for (size_t i = 0; i < m.cells.size(); ++i) o << "5\n";
  });
  os << "</Cells>\n</Piece>\n</UnstructuredGrid>\n</VTKFile>\n";
}

void writeVtuParticles(const Particles& p, const std::string& path) {
  std::ofstream os(path);
  if (!os) return;
  const size_t n = p.pos.size();
  os << "<?xml version=\"1.0\"?>\n<VTKFile type=\"UnstructuredGrid\" version=\"0.1\" byte_order=\"LittleEndian\">\n";
  os << "<UnstructuredGrid>\n<Piece NumberOfPoints=\"" << n << "\" NumberOfCells=\"" << n << "\">\n";
  os << "<PointData>\n";
  vtuDataArray(os, "volume", "Float64", 1, [&](std::ostream& o) {
    for (size_t i = 0; i < n; ++i) { writeNum(o, p.volume[i]); o << "\n"; }
  });
  vtuDataArray(os, "spacing", "Float64", 1, [&](std::ostream& o) {
    for (size_t i = 0; i < n; ++i) { writeNum(o, p.dp[i]); o << "\n"; }
  });
  vtuDataArray(os, "supportRadius", "Float64", 1, [&](std::ostream& o) {
    for (size_t i = 0; i < n; ++i) { writeNum(o, p.h[i]); o << "\n"; }
  });
  vtuDataArray(os, "level", "Int32", 1, [&](std::ostream& o) {
    for (size_t i = 0; i < n; ++i) o << p.level[i] << "\n";
  });
  vtuDataArray(os, "category", "Int32", 1, [&](std::ostream& o) {
    for (size_t i = 0; i < n; ++i) o << p.category[i] << "\n";
  });
  vtuDataArray(os, "region", "Int32", 1, [&](std::ostream& o) {
    for (size_t i = 0; i < n; ++i) o << (p.region.empty() ? 0 : p.region[i]) << "\n";
  });
  vtuDataArray(os, "bcType", "Int32", 1, [&](std::ostream& o) {
    for (size_t i = 0; i < n; ++i) o << p.bcType[i] << "\n";
  });
  // [W1] zero-order consistency diagnostics
  vtuDataArray(os, "divergenceOfPosition", "Float64", 1, [&](std::ostream& o) {
    for (size_t i = 0; i < n; ++i) { writeNum(o, p.divPos[i]); o << "\n"; }
  });
  vtuDataArray(os, "consistencyResidual", "Float64", 1, [&](std::ostream& o) {
    for (size_t i = 0; i < n; ++i) { writeNum(o, p.consistency[i]); o << "\n"; }
  });
  vtuDataArray(os, "boundarySurfaceArea", "Float64", 1, [&](std::ostream& o) {
    for (size_t i = 0; i < n; ++i) { writeNum(o, p.aBndMag[i]); o << "\n"; }
  });
  vtuDataArray(os, "boundarySurfaceNormal", "Float64", 3, [&](std::ostream& o) {
    for (size_t i = 0; i < n; ++i) {
      writeNum(o, p.nBnd[i].x); o << " ";
      writeNum(o, p.nBnd[i].y); o << " 0\n";
    }
  });
  vtuDataArray(os, "zeroOrderConsistencyVector", "Float64", 3, [&](std::ostream& o) {
    for (size_t i = 0; i < n; ++i) {
      writeNum(o, p.aSum[i].x); o << " ";
      writeNum(o, p.aSum[i].y); o << " 0\n";
    }
  });
  vtuDataArray(os, "wallDistance", "Float64", 1, [&](std::ostream& o) {
    for (size_t i = 0; i < n; ++i) { writeNum(o, p.wallDist[i]); o << "\n"; }
  });
  vtuDataArray(os, "kernelCorrectionB", "Float64", 4, [&](std::ostream& o) {
    for (size_t i = 0; i < n; ++i) {
      for (int k = 0; k < 4; ++k) { writeNum(o, p.bMat[i][k]); o << (k < 3 ? " " : "\n"); }
    }
  });
  os << "</PointData>\n<Points>\n";
  vtuDataArray(os, "points", "Float64", 3, [&](std::ostream& o) {
    for (size_t i = 0; i < n; ++i) {
      writeNum(o, p.pos[i].x); o << " ";
      writeNum(o, p.pos[i].y); o << " 0\n";
    }
  });
  os << "</Points>\n<Cells>\n";
  vtuDataArray(os, "connectivity", "Int32", 1, [&](std::ostream& o) {
    for (size_t i = 0; i < n; ++i) o << i << "\n";
  });
  vtuDataArray(os, "offsets", "Int32", 1, [&](std::ostream& o) {
    for (size_t i = 0; i < n; ++i) o << (i + 1) << "\n";
  });
  vtuDataArray(os, "types", "UInt8", 1, [&](std::ostream& o) {
    for (size_t i = 0; i < n; ++i) o << "1\n";
  });
  os << "</Cells>\n</Piece>\n</UnstructuredGrid>\n</VTKFile>\n";
}

// ------------------------------------------------------------------ Gmsh -----
void writeGmsh(const Mesh& m, const std::string& path) {
  std::ofstream os(path);
  if (!os) return;
  os << "$MeshFormat\n2.2 0 8\n$EndMeshFormat\n";
  // physical groups: 1..P boundary patches (lines), P+1 the fluid surface
  const int np = static_cast<int>(m.patchNames.size());
  os << "$PhysicalNames\n" << (np + 1) << "\n";
  for (int i = 0; i < np; ++i) os << "1 " << (i + 1) << " \"" << m.patchNames[i] << "\"\n";
  os << "2 " << (np + 1) << " \"fluid\"\n$EndPhysicalNames\n";
  os << "$Nodes\n" << m.nodes.size() << "\n";
  for (size_t i = 0; i < m.nodes.size(); ++i) {
    os << (i + 1) << " ";
    writeNum(os, m.nodes[i].x);
    os << " ";
    writeNum(os, m.nodes[i].y);
    os << " 0\n";
  }
  os << "$EndNodes\n";
  // boundary edges first (type 1), then triangles (type 2)
  struct BEdge {
    int a, b, patch;
  };
  std::vector<BEdge> bedges;
  bedges.reserve(m.boundaryFaces.size());
  for (const int f : m.boundaryFaces) {
    // recover the two nodes of the face from the owner cell
    const Face& fc = m.faces[f];
    int a = -1, b = -1;
    const auto& tri = m.cells[fc.owner];
    for (int k = 0; k < 3; ++k) {
      const int u = tri[k], v = tri[(k + 1) % 3];
      const V2 mid = (m.nodes[u] + m.nodes[v]) * 0.5;
      if (norm(mid - fc.c) < 1e-12 * std::max(1.0, fc.len)) {
        a = u;
        b = v;
        break;
      }
    }
    if (a < 0) continue;
    bedges.push_back({a, b, fc.patch});
  }
  const long long ne = static_cast<long long>(bedges.size());
  const long long nt = static_cast<long long>(m.cells.size());
  os << "$Elements\n" << (ne + nt) << "\n";
  long long id = 1;
  for (const auto& e : bedges) {
    os << id++ << " 1 2 " << (e.patch + 1) << " " << (e.patch + 1) << " " << (e.a + 1) << " "
       << (e.b + 1) << "\n";
  }
  for (size_t i = 0; i < m.cells.size(); ++i)
    os << id++ << " 2 2 " << (np + 1) << " " << (np + 1) << " " << (m.cells[i][0] + 1) << " "
       << (m.cells[i][1] + 1) << " " << (m.cells[i][2] + 1) << "\n";
  os << "$EndElements\n";
}

// ------------------------------------------------------------------- JSON ----
void writeMeshJson(const Result& r, const std::string& path) {
  std::ofstream os(path);
  if (!os) return;
  const Mesh& m = r.mesh;
  const Particles& p = r.parts;
  const Coupling& c = r.coupling;
  mj::Writer w(1 << 20);
  w.objBegin();
  w.kv("format", "mesher-core/mesh+graph@1");
  w.kv("dim", (long long)m.dim);
  w.key("bbox");
  w.arrBegin();
  w.number(m.box.lo.x); w.number(m.box.lo.y); w.number(m.box.hi.x); w.number(m.box.hi.y);
  w.arrEnd();
  w.key("counts");
  w.objBegin();
  w.kv("nodes", (long long)m.nodes.size());
  w.kv("cells", (long long)m.cells.size());
  w.kv("faces", (long long)m.faces.size());
  w.kv("interiorFaces", (long long)m.interiorFaces.size());
  w.kv("boundaryFaces", (long long)m.boundaryFaces.size());
  w.kv("particles", (long long)p.pos.size());
  w.objEnd();
  // coordinates
  w.key("nodes");
  w.objBegin();
  w.key("x"); w.numberArray(&m.nodeX[0], m.nodes.size(), 2, 0);
  w.key("y"); w.numberArray(&m.nodeX[0], m.nodes.size(), 2, 1);
  w.objEnd();
  w.key("cells");
  w.objBegin();
  w.key("connectivity"); w.intArray(&m.cellNodeFlat[0], m.cellNodeFlat.size());
  w.key("centersX"); w.numberArray(&m.cc[0].x, m.cc.size(), 2, 0);
  w.key("centersY"); w.numberArray(&m.cc[0].x, m.cc.size(), 2, 1);
  w.key("volumes"); w.numberArray(&m.vol[0], m.vol.size());
  w.key("cellFacePtr"); w.intArray(&m.cellFacePtr[0], m.cellFacePtr.size());
  w.key("cellFaceIdx"); w.intArray(&m.cellFaceIdx[0], m.cellFaceIdx.size());
  w.key("nodeCellPtr"); w.intArray(&m.nodeCellPtr[0], m.nodeCellPtr.size());
  w.key("nodeCellIdx"); w.intArray(&m.nodeCellIdx[0], m.nodeCellIdx.size());
  if (!m.dualVol.empty()) { w.key("dualVolumes"); w.numberArray(&m.dualVol[0], m.dualVol.size()); }
  w.objEnd();
  // DiFVM static graph arrays [W3]
  w.key("graph");
  w.objBegin();
  w.kv("layout", "face-major CSR, gather/scatter ready");
  w.key("owner"); w.intArray(&m.gOwner[0], m.gOwner.size());
  w.key("neighbour"); w.intArray(&m.gNeighbour[0], m.gNeighbour.size());
  w.key("patch"); w.intArray(&m.gPatch[0], m.gPatch.size());
  w.key("Sf"); w.numberArray(&m.gSf[0], m.gSf.size());
  w.key("Cf"); w.numberArray(&m.gCf[0], m.gCf.size());
  w.key("magSf"); w.numberArray(&m.gMagSf[0], m.gMagSf.size());
  w.key("deltaCoeffs"); w.numberArray(&m.gDeltaCoeffs[0], m.gDeltaCoeffs.size());
  w.key("weights"); w.numberArray(&m.gWeights[0], m.gWeights.size());
  w.key("nonOrthogonality"); w.numberArray(&m.gNonOrtho[0], m.gNonOrtho.size());
  w.key("nonOrthoCorrection"); w.numberArray(&m.gCorr[0], m.gCorr.size());
  w.key("patches"); w.arrBegin();
  for (size_t i = 0; i < m.patchNames.size(); ++i) {
    w.objBegin();
    w.kv("name", m.patchNames[i]);
    w.kv("bc", m.patchBc[i]);
    w.kv("faces", (long long)m.patchFaceCount[i]);
    w.objEnd();
  }
  w.arrEnd();
  w.objEnd();
  // particles
  if (p.valid) {
    w.key("particles");
    w.objBegin();
    w.kv("kernel", p.kernelName);
    w.kv("supportRadius0", p.kernelSupport);
    w.key("x"); w.numberArray(&p.pos[0].x, p.pos.size(), 2, 0);
    w.key("y"); w.numberArray(&p.pos[0].x, p.pos.size(), 2, 1);
    w.key("volume"); w.numberArray(&p.volume[0], p.volume.size());
    w.key("spacing"); w.numberArray(&p.dp[0], p.dp.size());
    w.key("support"); w.numberArray(&p.h[0], p.h.size());
    w.key("level"); w.intArray(&p.level[0], p.level.size());
    w.key("category"); w.intArray(&p.category[0], p.category.size());
    w.key("region"); w.intArray(&p.region[0], p.region.size());
    w.key("bcType"); w.intArray(&p.bcType[0], p.bcType.size());
    w.key("neighbourPtr"); w.intArray(&p.nbrPtr[0], p.nbrPtr.size());
    w.key("neighbourIdx"); w.intArray(&p.nbrIdx[0], p.nbrIdx.size());
    w.key("neighbourDwdrVj"); w.numberArray(&p.nbrDwdr[0], p.nbrDwdr.size());
    w.key("neighbourR"); w.numberArray(&p.nbrR[0], p.nbrR.size());
    w.objEnd();
    // [W1] boundary storage
    w.key("boundaryStorage");
    w.objBegin();
    w.kv("description",
         "extra storage of the FVM-equivalent boundary algorithm: |A_iS|, n_iS and the wall "
         "distance for every particle whose divergence of position falls below gamma*d");
    w.kv("gamma", r.spec.sph.gammaFactor);
    w.key("divergenceOfPosition"); w.numberArray(&p.divPos[0], p.divPos.size());
    w.key("isBoundary"); w.numberArray(&p.isBoundary[0], p.isBoundary.size());
    w.key("aBndMag"); w.numberArray(&p.aBndMag[0], p.aBndMag.size());
    w.key("aBndX"); w.numberArray(&p.aBnd[0].x, p.aBnd.size(), 2, 0);
    w.key("aBndY"); w.numberArray(&p.aBnd[0].x, p.aBnd.size(), 2, 1);
    w.key("normalX"); w.numberArray(&p.nBnd[0].x, p.nBnd.size(), 2, 0);
    w.key("normalY"); w.numberArray(&p.nBnd[0].x, p.nBnd.size(), 2, 1);
    w.key("wallDistance"); w.numberArray(&p.wallDist[0], p.wallDist.size());
    w.key("correctionB"); w.arrBegin();
    for (size_t i = 0; i < p.bMat.size(); ++i) {
      w.arrBegin();
      for (int k = 0; k < 4; ++k) w.number(p.bMat[i][k]);
      w.arrEnd();
    }
    w.arrEnd();
    w.objEnd();
  }
  // coupling operators [W2]
  if (c.valid) {
    w.key("coupling");
    w.objBegin();
    w.kv("mode", r.spec.cpl.mode);
    w.kv("bufferWidth", c.bufferWidth);
    w.kv("p2mScheme", c.p2mScheme);
    w.kv("m2pScheme", c.m2pScheme);
    w.kv("momentCorrection", r.spec.cpl.momentCorrection);
    w.key("cellRegion"); w.intArray(&c.cellRegion[0], c.cellRegion.size());
    w.key("particleRegion"); w.intArray(&p.region[0], p.region.size());
    w.key("interfaceFaces"); w.intArray(&c.interfaceFaces[0], c.interfaceFaces.size());
    w.key("particleToMesh");
    w.objBegin();
    w.kv("rows", (long long)c.p2m.rows);
    w.kv("cols", (long long)c.p2m.cols);
    w.key("rowPtr"); w.intArray(&c.p2m.rowPtr[0], c.p2m.rowPtr.size());
    w.key("col"); w.intArray(&c.p2m.col[0], c.p2m.col.size());
    w.key("val"); w.numberArray(&c.p2m.val[0], c.p2m.val.size());
    w.objEnd();
    w.key("meshToParticle");
    w.objBegin();
    w.kv("rows", (long long)c.m2p.rows);
    w.kv("cols", (long long)c.m2p.cols);
    w.key("rowPtr"); w.intArray(&c.m2p.rowPtr[0], c.m2p.rowPtr.size());
    w.key("col"); w.intArray(&c.m2p.col[0], c.m2p.col.size());
    w.key("val"); w.numberArray(&c.m2p.val[0], c.m2p.val.size());
    w.objEnd();
    w.objEnd();
  }
  w.objEnd();
  os << w.str();
}

namespace {
bool faceNodes(const Mesh& m, int f, int& a, int& b) {
  const Face& fc = m.faces[f];
  if (fc.owner < 0) return false;
  const auto& tri = m.cells[fc.owner];
  const Real tol = 1e-9 * std::max(1.0, fc.len);
  for (int k = 0; k < 3; ++k) {
    const int u = tri[k], v = tri[(k + 1) % 3];
    const V2 mid = (m.nodes[u] + m.nodes[v]) * 0.5;
    if (norm(mid - fc.c) < tol) {
      a = u;
      b = v;
      return true;
    }
  }
  return false;
}
}  // namespace

// --------------------------------------------------------------- manifest ----
void writeManifest(const Result& r, const std::string& path) {
  std::ofstream os(path);
  if (!os) return;
  const Mesh& m = r.mesh;
  const Particles& p = r.parts;
  const Coupling& c = r.coupling;
  mj::Writer w(1 << 20);
  w.objBegin();
  w.kv("ok", true);
  w.kv("engine", r.engine);
  w.kv("threads", (long long)r.threadsUsed);
  w.kv("caseName", r.spec.name);
  w.kv("totalMs", r.totalMs);
  w.key("timings");
  w.objBegin();
  for (const auto& kv : r.timer.ms) w.kv(kv.first, kv.second);
  w.objEnd();
  // counts
  w.key("counts");
  w.objBegin();
  w.kv("nodes", (long long)m.nodes.size());
  w.kv("cells", (long long)m.cells.size());
  w.kv("faces", (long long)m.faces.size());
  w.kv("interiorFaces", (long long)m.interiorFaces.size());
  w.kv("boundaryFaces", (long long)m.boundaryFaces.size());
  w.kv("patches", (long long)m.patchNames.size());
  w.kv("particles", (long long)p.pos.size());
  w.kv("particleNeighbourPairs", (long long)p.nbrIdx.size());
  w.kv("p2mNnz", (long long)c.p2m.nnz());
  w.kv("m2pNnz", (long long)c.m2p.nnz());
  w.objEnd();
  // throughput
  w.key("throughput");
  w.objBegin();
  Real tGen = 0;
  for (const auto& kv : r.timer.ms) tGen += kv.second;
  w.kv("cellsPerSecond", tGen > 0 ? m.cells.size() / (tGen * 1e-3) : 0.0);
  w.kv("particlesPerSecond", tGen > 0 ? p.pos.size() / (tGen * 1e-3) : 0.0);
  w.kv("pipelineMs", tGen);
  w.objEnd();
  // mesh quality
  w.key("mesh");
  w.objBegin();
  w.kv("valid", m.valid);
  w.kv("domainArea", m.domainArea);
  w.kv("perimeter", m.perimeter);
  w.kv("minVolume", m.minVol);
  w.kv("maxVolume", m.maxVol);
  w.kv("minEdge", m.minEdge);
  w.kv("maxEdge", m.maxEdge);
  w.kv("aspectMin", m.minAspect);
  w.kv("aspectMax", m.maxAspect);
  w.kv("aspectAvg", m.avgAspect);
  w.kv("skewMax", m.maxSkew);
  w.kv("skewAvg", m.avgSkew);
  w.kv("nonOrthoMax", m.maxNonOrtho);
  w.kv("nonOrthoAvg", m.avgNonOrtho);
  w.kv("nonOrthoP99", m.p99NonOrtho);
  w.kv("invertedCells", (long long)m.nNegativeVol);
  w.key("patches");
  w.arrBegin();
  for (size_t i = 0; i < m.patchNames.size(); ++i) {
    Real len = 0;
    for (const int f : m.boundaryFaces)
      if (m.faces[f].patch == static_cast<int>(i)) len += m.faces[f].len;
    w.objBegin();
    w.kv("name", m.patchNames[i]);
    w.kv("bc", m.patchBc[i]);
    w.kv("faces", (long long)m.patchFaceCount[i]);
    w.kv("length", len);
    w.objEnd();
  }
  w.arrEnd();
  w.objEnd();
  // SPH diagnostics [W1]
  w.key("sph");
  w.objBegin();
  w.kv("valid", p.valid);
  w.kv("kernel", p.kernelName);
  w.kv("supportRatio", r.spec.sph.hRatio);
  w.kv("baseSpacing", r.spec.sph.dp);
  w.kv("levels", (long long)r.spec.sph.levels);
  w.kv("nInterior", (long long)p.nInterior);
  w.kv("nBoundary", (long long)p.nBoundary);
  w.kv("nGhost", (long long)p.nGhost);
  w.kv("nCore", (long long)p.nCore);
  w.kv("nBuffer", (long long)p.nBuffer);
  w.kv("meanNeighbours", p.meanNbrs);
  w.kv("spacingCV", p.cvDp);
  w.kv("divPosMean", p.meanDivPos);
  w.kv("divPosMin", p.minDivPos);
  w.kv("gammaThreshold", r.spec.sph.gammaFactor * 2.0);
  w.kv("consistencyMean", p.meanA);
  w.kv("consistencyMax", p.maxA);
  w.kv("consistencyMeanInterior", p.meanAInterior);
  w.kv("consistencyMaxInterior", p.maxAInterior);
  w.kv("consistencyScaleInterior", p.meanAInterior * r.spec.sph.hRatio * r.spec.sph.dp);
  w.kv("smoothingLength", p.kernelSupport * 0.5);
  w.kv("relaxResidualBefore", p.relaxResidual0);
  w.kv("relaxResidualAfter", p.relaxResidual1);
  w.kv("relaxIterations", (long long)p.relaxItersDone);
  // sum_i V_i |A_iS| converges to half the boundary perimeter (the kernel
  // partition of unity integrated over the boundary surface) [W1]
  // sum_i V_i |A_iS| -> half the wetted perimeter of the particle domain [W1].
  // Evaluated on the full particle set (before the adaptive coupling trim).
  w.kv("boundaryAreaSum", r.sphBndAreaFull);
  w.kv("halfPerimeter", 0.5 * m.perimeter);
  w.kv("perimeterErrorRel",
       m.perimeter > 1e-12 ? std::fabs(2.0 * r.sphBndAreaFull - m.perimeter) / m.perimeter : 0.0);
  w.kv("interiorResidualSamples", (long long)p.nInteriorRef);
  w.kv("volumeModel", r.spec.sph.volumeModel == 1 ? "shepard" : "nominal");
  // full particle set vs the whole domain, and (when coupled) the retained
  // particle set vs the adaptive SPH region
  w.kv("particleVolumeSum", r.sphVolumeFull > 0 ? r.sphVolumeFull : p.sphRegionArea);
  w.kv("referenceArea", m.domainArea);
  w.kv("volumeErrorRel",
       m.domainArea > 1e-12 ? std::fabs(r.sphVolumeFull - m.domainArea) / m.domainArea : 0.0);
  w.kv("retainedVolumeSum", p.sphRegionArea);
  w.kv("sphRegionArea", c.coreArea + c.bufferArea);
  w.kv("sphRegionVolumeErrorRel",
       (c.coreArea + c.bufferArea) > 1e-12
           ? std::fabs(p.sphRegionArea - (c.coreArea + c.bufferArea)) / (c.coreArea + c.bufferArea)
           : 0.0);
  w.objEnd();
  // coupling [W2]
  w.key("coupling");
  w.objBegin();
  w.kv("enabled", c.valid);
  w.kv("mode", r.spec.cpl.mode);
  w.kv("bufferWidth", c.bufferWidth);
  w.kv("p2mScheme", c.p2mScheme);
  w.kv("m2pScheme", c.m2pScheme);
  w.kv("momentCorrection", r.spec.cpl.momentCorrection);
  w.kv("quadrature", (long long)r.spec.cpl.quadrature);
  w.kv("fvmCells", (long long)c.nCoreCells);
  w.kv("bufferCells", (long long)c.nBufferCells);
  w.kv("ghostCells", (long long)c.nGhostCells);
  w.kv("interfaceFaces", (long long)c.nInterfaceFaces);
  w.kv("coreParticles", (long long)c.nCoreParticles);
  w.kv("bufferParticles", (long long)c.nBufferParticles);
  w.kv("coreArea", c.coreArea);
  w.kv("bufferArea", c.bufferArea);
  w.kv("p2mNnz", (long long)c.p2m.nnz());
  w.kv("m2pNnz", (long long)c.m2p.nnz());
  w.kv("p2mRows", (long long)c.p2m.rows);
  w.kv("p2mRowSumErrMax", c.p2mRowSumErr);
  w.kv("m2pRowSumErrMax", c.m2pRowSumErr);
  w.key("patchTest");
  w.objBegin();
  w.key("schemes"); w.arrBegin();
  for (const auto& s : c.test.schemes) w.string(s);
  w.arrEnd();
  w.key("fields"); w.arrBegin();
  for (const auto& s : c.test.fields) w.string(s);
  w.arrEnd();
  w.key("relativeL2"); w.arrBegin();
  for (size_t vi = 0; vi < c.test.schemes.size(); ++vi) {
    w.arrBegin();
    for (size_t fi = 0; fi < c.test.fields.size(); ++fi)
      w.number(c.test.l2[vi * c.test.fields.size() + fi]);
    w.arrEnd();
  }
  w.arrEnd();
  w.objEnd();
  w.objEnd();
  // warnings / artifacts
  w.key("warnings"); w.arrBegin();
  for (const auto& s : r.warnings) w.string(s);
  w.arrEnd();
  w.key("artifacts"); w.arrBegin();
  for (const auto& a : r.artifacts) {
    w.objBegin();
    const fs::path fp(a);
    w.kv("name", fp.filename().string());
    w.kv("path", a);
    w.kv("format", fp.extension().string());
    w.kv("bytes", fileSize(a));
    w.objEnd();
  }
  w.arrEnd();
  // preview payload for the web console (decimated)
  if (r.spec.out.preview) {
    const size_t limit = static_cast<size_t>(std::max(2000, r.spec.out.previewLimit));
    w.key("preview");
    w.objBegin();
    w.kv("bboxMinX", m.box.lo.x);
    w.kv("bboxMinY", m.box.lo.y);
    w.kv("bboxMaxX", m.box.hi.x);
    w.kv("bboxMaxY", m.box.hi.y);
    const size_t cs = m.cc.size() > limit ? (m.cc.size() + limit - 1) / limit : 1;
    w.kv("cellStride", (long long)cs);
    w.key("cellX"); w.arrBegin();
    for (size_t i = 0; i < m.cc.size(); i += cs) w.number(m.cc[i].x);
    w.arrEnd();
    w.key("cellY"); w.arrBegin();
    for (size_t i = 0; i < m.cc.size(); i += cs) w.number(m.cc[i].y);
    w.arrEnd();
    w.key("cellRegion"); w.arrBegin();
    for (size_t i = 0; i < m.cc.size(); i += cs)
      w.integer(c.cellRegion.empty() ? 0 : c.cellRegion[i]);
    w.arrEnd();
    // boundary polyline as segment pairs [x0,y0,x1,y1, ...] plus patch ids
    {
      std::vector<int> patches;
      std::vector<Real> segs;
      segs.reserve(m.boundaryFaces.size() * 4);
      patches.reserve(m.boundaryFaces.size());
      for (const int f : m.boundaryFaces) {
        int a = -1, b = -1;
        if (!faceNodes(m, f, a, b)) continue;
        segs.push_back(m.nodes[a].x);
        segs.push_back(m.nodes[a].y);
        segs.push_back(m.nodes[b].x);
        segs.push_back(m.nodes[b].y);
        patches.push_back(m.faces[f].patch);
      }
      w.key("boundarySeg");
      w.numberArray(segs.data(), segs.size());
      w.key("boundaryPatch");
      w.intArray(patches.data(), patches.size());
    }
    const size_t ps = p.pos.size() > limit ? (p.pos.size() + limit - 1) / limit : 1;
    w.kv("particleStride", (long long)ps);
    w.key("particleX"); w.arrBegin();
    for (size_t i = 0; i < p.pos.size(); i += ps) w.number(p.pos[i].x);
    w.arrEnd();
    w.key("particleY"); w.arrBegin();
    for (size_t i = 0; i < p.pos.size(); i += ps) w.number(p.pos[i].y);
    w.arrEnd();
    w.key("particleCategory"); w.arrBegin();
    for (size_t i = 0; i < p.pos.size(); i += ps) w.integer(p.category[i]);
    w.arrEnd();
    w.key("particleRegion"); w.arrBegin();
    for (size_t i = 0; i < p.pos.size(); i += ps) w.integer(p.region.empty() ? 0 : p.region[i]);
    w.arrEnd();
    w.key("particleDivPos"); w.arrBegin();
    for (size_t i = 0; i < p.pos.size(); i += ps) w.number(p.divPos[i]);
    w.arrEnd();
    w.key("particleLevel"); w.arrBegin();
    for (size_t i = 0; i < p.pos.size(); i += ps) w.integer(p.level[i]);
    w.arrEnd();
    w.key("interfaceX"); w.arrBegin();
    for (const int f : c.interfaceFaces) w.number(m.faces[f].c.x);
    w.arrEnd();
    w.key("interfaceY"); w.arrBegin();
    for (const int f : c.interfaceFaces) w.number(m.faces[f].c.y);
    w.arrEnd();
    w.objEnd();
  }
  w.objEnd();
  os << w.str();
}

// ------------------------------------------------------------- artifacts -----
void writeArtifacts(Result& r) {
  const std::string dir = r.spec.out.dir;
  std::error_code ec;
  fs::create_directories(dir, ec);
  const auto want = [&](const std::string& f) {
    return std::find(r.spec.out.formats.begin(), r.spec.out.formats.end(), f) !=
           r.spec.out.formats.end();
  };
  if (r.mesh.valid && want("vtu")) {
    const std::string p = dir + "/mesh.vtu";
    writeVtuMesh(r.mesh, r.coupling, p);
    r.artifacts.push_back(p);
  }
  if (r.parts.valid && want("vtu")) {
    const std::string p = dir + "/particles.vtu";
    writeVtuParticles(r.parts, p);
    r.artifacts.push_back(p);
  }
  if (r.mesh.valid && want("msh")) {
    const std::string p = dir + "/mesh.msh";
    writeGmsh(r.mesh, p);
    r.artifacts.push_back(p);
  }
  if (want("json")) {
    const std::string p = dir + "/mesh.json";
    writeMeshJson(r, p);
    r.artifacts.push_back(p);
  }
}

}  // namespace mesher
