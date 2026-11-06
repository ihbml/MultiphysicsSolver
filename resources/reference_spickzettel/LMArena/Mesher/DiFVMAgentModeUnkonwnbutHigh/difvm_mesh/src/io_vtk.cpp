// DiFVM Mesher — VTK legacy / XML readers + writers.
#include <cstdio>
#include <cstring>
#include <fstream>
#include <map>
#include <sstream>
#include <string>
#include <vector>

#include "difvm/io.hpp"

namespace difvm {
namespace {

std::string slurp(const std::string& path) {
  std::ifstream in(path, std::ios::binary);
  if (!in) fail("cannot open '" + path + "'");
  std::ostringstream ss;
  ss << in.rdbuf();
  return ss.str();
}

// ---------------------------------------------------------------------------
// VTK cell type -> local face tables (local vertex numbering per VTK spec)
// ---------------------------------------------------------------------------
struct CellFaces {
  std::vector<std::vector<int>> faces;
};

CellFaces cell_face_table(int vtk_type) {
  switch (vtk_type) {
    case 5:   return {{{0, 1, 2}}};                                        // triangle
    case 9:   return {{{0, 1, 2, 3}}};                                     // quad
    case 10:  return {{{0, 2, 1}, {0, 1, 3}, {1, 2, 3}, {0, 3, 2}}};       // tetra
    case 12:  return {{{0, 4, 7, 3}, {1, 2, 6, 5}, {0, 1, 5, 4},
                       {3, 7, 6, 2}, {0, 3, 2, 1}, {4, 5, 6, 7}}};         // hexahedron
    case 13:  return {{{0, 2, 1}, {3, 4, 5}, {0, 1, 4, 3},
                       {1, 2, 5, 4}, {2, 0, 3, 5}}};                       // wedge
    case 14:  return {{{0, 3, 2, 1}, {0, 1, 4}, {1, 2, 4},
                       {2, 3, 4}, {3, 0, 4}}};                             // pyramid
    default:  fail("unsupported VTK cell type " + std::to_string(vtk_type));
  }
  return {};
}

// ---------------------------------------------------------------------------
// XML helpers (ASCII inline data only)
// ---------------------------------------------------------------------------
std::string xml_section(const std::string& xml, const std::string& tag) {
  const std::string open = "<" + tag;
  std::size_t b = xml.find(open);
  if (b == std::string::npos) return {};
  b = xml.find('>', b);
  if (b == std::string::npos) return {};
  const std::string close = "</" + tag + ">";
  const std::size_t e = xml.find(close, b);
  if (e == std::string::npos) return {};
  return xml.substr(b + 1, e - b - 1);
}

// Extracts all ASCII DataArray payloads inside a section, concatenated.
std::vector<double> xml_numbers(const std::string& section) {
  std::vector<double> out;
  std::size_t pos = 0;
  while (true) {
    const std::size_t a = section.find("<DataArray", pos);
    if (a == std::string::npos) break;
    const std::size_t tag_end = section.find('>', a);
    if (tag_end == std::string::npos) break;
    const std::string attrs = section.substr(a, tag_end - a);
    const std::size_t b = section.find("</DataArray>", tag_end);
    if (b == std::string::npos) break;
    if (attrs.find("ascii") == std::string::npos)
      fail("VTK XML: only format=\"ascii\" is supported (binary/appended not implemented)");
    const std::string payload = section.substr(tag_end + 1, b - tag_end - 1);
    std::istringstream ss(payload);
    double v;
    while (ss >> v) out.push_back(v);
    pos = b + 12;
  }
  return out;
}

std::vector<long> xml_ints(const std::string& section) {
  const std::vector<double> d = xml_numbers(section);
  std::vector<long> out;
  out.reserve(d.size());
  for (double v : d) out.push_back(static_cast<long>(v));
  return out;
}

}  // namespace

// ---------------------------------------------------------------------------
// Legacy .vtk ASCII unstructured grid reader
// ---------------------------------------------------------------------------
static PolyMeshRaw read_vtk_legacy(const std::string& path) {
  const std::string text = slurp(path);
  PolyMeshRaw raw;
  std::istringstream in(text);
  std::string tok;

  std::vector<Vec3> pts;
  std::vector<std::vector<long>> cells;
  std::vector<int> types;

  while (in >> tok) {
    if (tok == "POINTS") {
      long n = 0;
      std::string dtype;
      in >> n >> dtype;
      pts.resize(static_cast<std::size_t>(std::max(0L, n)));
      for (long i = 0; i < n; ++i) in >> pts[static_cast<std::size_t>(i)].x >> pts[static_cast<std::size_t>(i)].y >>
          pts[static_cast<std::size_t>(i)].z;
    } else if (tok == "CELLS") {
      long n = 0, total = 0;
      in >> n >> total;
      (void)total;
      cells.resize(static_cast<std::size_t>(std::max(0L, n)));
      for (long i = 0; i < n; ++i) {
        long k = 0;
        in >> k;
        cells[static_cast<std::size_t>(i)].resize(static_cast<std::size_t>(std::max(0L, k)));
        for (long j = 0; j < k; ++j) in >> cells[static_cast<std::size_t>(i)][static_cast<std::size_t>(j)];
      }
    } else if (tok == "CELL_TYPES") {
      long n = 0;
      in >> n;
      types.resize(static_cast<std::size_t>(std::max(0L, n)));
      for (long i = 0; i < n; ++i) in >> types[static_cast<std::size_t>(i)];
    } else if (tok == "POINT_DATA" || tok == "CELL_DATA" || tok == "VECTORS" || tok == "SCALARS") {
      break;  // mesh ingested; field data ignored
    }
  }
  if (pts.empty() || cells.empty()) fail("VTK legacy: no POINTS/CELLS found in " + path);
  if (types.size() != cells.size()) fail("VTK legacy: CELL_TYPES count mismatch");

  raw.points = std::move(pts);
  gidx ncells = static_cast<gidx>(cells.size());
  raw.n_cells = ncells;
  raw.face_offsets.reserve(static_cast<std::size_t>(ncells) * 6 + 1);
  std::vector<idx> verts_tmp;
  for (gidx c = 0; c < ncells; ++c) {
    const auto& cv = cells[static_cast<std::size_t>(c)];
    const CellFaces table = cell_face_table(types[static_cast<std::size_t>(c)]);
    for (const auto& f : table.faces) {
      raw.face_offsets.push_back(static_cast<gidx>(raw.face_verts.size()));
      for (int lv : f) {
        if (lv < 0 || static_cast<std::size_t>(lv) >= cv.size()) fail("VTK legacy: bad local vertex");
        raw.face_verts.push_back(static_cast<idx>(cv[static_cast<std::size_t>(lv)]));
      }
      raw.owner.push_back(c);
    }
  }
  raw.face_offsets.push_back(static_cast<gidx>(raw.face_verts.size()));
  // no neighbour list, no patches: build_graph_mesh welds coincident faces
  return raw;
}

// ---------------------------------------------------------------------------
// XML .vtu reader (ascii)
// ---------------------------------------------------------------------------
static PolyMeshRaw read_vtu(const std::string& path) {
  const std::string xml = slurp(path);
  PolyMeshRaw raw;
  const std::vector<double> pts_d = xml_numbers(xml_section(xml, "Points"));
  if (pts_d.size() % 3 != 0) fail("vtu: bad Points array");
  raw.points.reserve(pts_d.size() / 3);
  for (std::size_t i = 0; i + 2 < pts_d.size(); i += 3)
    raw.points.push_back(Vec3{pts_d[i], pts_d[i + 1], pts_d[i + 2]});

  const std::string cells_sec = xml_section(xml, "Cells");
  const std::vector<long> conn = xml_ints(xml_section(cells_sec, "DataArray"));
  // The helper above concatenates everything; instead fetch arrays by Name.
  auto array_by_name = [&](const std::string& name) {
    std::vector<double> out;
    std::size_t pos = 0;
    while (true) {
      const std::size_t a = cells_sec.find("<DataArray", pos);
      if (a == std::string::npos) break;
      const std::size_t tag_end = cells_sec.find('>', a);
      const std::string attrs = cells_sec.substr(a, tag_end - a);
      const std::size_t b = cells_sec.find("</DataArray>", tag_end);
      if (b == std::string::npos) break;
      if (attrs.find("Name=\"" + name + "\"") != std::string::npos) {
        std::istringstream ss(cells_sec.substr(tag_end + 1, b - tag_end - 1));
        double v;
        while (ss >> v) out.push_back(v);
      }
      pos = b + 12;
    }
    return out;
  };
  std::vector<double> offs_d = array_by_name("offsets");
  std::vector<double> types_d = array_by_name("types");
  std::vector<double> faces_d = array_by_name("faces");
  std::vector<double> foffs_d = array_by_name("faceoffsets");
  std::vector<double> conn_d = array_by_name("connectivity");
  if (conn_d.empty()) conn_d = xml_numbers(xml_section(cells_sec, "DataArray"));
  (void)conn;
  if (offs_d.empty() || types_d.empty()) fail("vtu: missing offsets/types");
  const gidx ncells = static_cast<gidx>(offs_d.size());
  raw.n_cells = ncells;

  std::size_t cbegin = 0;
  for (gidx c = 0; c < ncells; ++c) {
    const std::size_t cend = static_cast<std::size_t>(offs_d[static_cast<std::size_t>(c)]);
    const int type = static_cast<int>(types_d[static_cast<std::size_t>(c)]);
    if (type == 42) {  // polyhedron
      if (faces_d.empty() || foffs_d.empty()) fail("vtu: polyhedron without faces/faceoffsets");
      std::size_t fp = (c == 0) ? 0 : static_cast<std::size_t>(foffs_d[static_cast<std::size_t>(c - 1)]);
      const std::size_t fend = static_cast<std::size_t>(foffs_d[static_cast<std::size_t>(c)]);
      const long nfaces = static_cast<long>(faces_d[fp++]);
      for (long fi = 0; fi < nfaces; ++fi) {
        const long np = static_cast<long>(faces_d[fp++]);
        raw.face_offsets.push_back(static_cast<gidx>(raw.face_verts.size()));
        for (long k = 0; k < np; ++k) raw.face_verts.push_back(static_cast<idx>(faces_d[fp++]));
        raw.owner.push_back(c);
      }
      if (fp != fend) fail("vtu: faceoffsets inconsistent");
    } else {
      std::vector<idx> cv;
      for (std::size_t k = cbegin; k < cend; ++k) cv.push_back(static_cast<idx>(conn_d[k]));
      const CellFaces table = cell_face_table(type);
      for (const auto& f : table.faces) {
        raw.face_offsets.push_back(static_cast<gidx>(raw.face_verts.size()));
        for (int lv : f) raw.face_verts.push_back(cv[static_cast<std::size_t>(lv)]);
        raw.owner.push_back(c);
      }
    }
    cbegin = cend;
  }
  raw.face_offsets.push_back(static_cast<gidx>(raw.face_verts.size()));
  return raw;
}

PolyMeshRaw read_vtk(const std::string& path) {
  const std::string text = slurp(path);
  if (text.find("<VTKFile") != std::string::npos) return read_vtu(path);
  return read_vtk_legacy(path);
}

// ---------------------------------------------------------------------------
// Writers
// ---------------------------------------------------------------------------
void write_vtu(const std::string& path, const GraphMesh& gm) {
  std::ofstream out(path);
  if (!out) fail("cannot write " + path);
  out.setf(std::ios::scientific);
  out.precision(10);
  out << "<?xml version=\"1.0\"?>\n<VTKFile type=\"UnstructuredGrid\" version=\"1.0\" byte_order=\"LittleEndian\">\n"
      << "<UnstructuredGrid>\n<Piece NumberOfPoints=\"" << gm.points.size() << "\" NumberOfCells=\"" << gm.n_cells << "\">\n";

  out << "<Points>\n<DataArray type=\"Float64\" NumberOfComponents=\"3\" format=\"ascii\">\n";
  for (const Vec3& p : gm.points) out << p.x << " " << p.y << " " << p.z << "\n";
  out << "</DataArray>\n</Points>\n";

  // polyhedron cells: connectivity lists the unique points per cell
  std::vector<idx> cell_pts;
  std::vector<std::int64_t> conn_offsets;
  {
    std::vector<std::int64_t> stamp(gm.points.size(), -1);
    conn_offsets.reserve(static_cast<std::size_t>(gm.n_cells));
    std::int64_t total = 0;
    for (gidx i = 0; i < gm.n_cells; ++i) {
      for (gidx k = gm.cf_offsets[static_cast<std::size_t>(i)]; k < gm.cf_offsets[static_cast<std::size_t>(i) + 1]; ++k) {
        const idx f = gm.cf_faces[static_cast<std::size_t>(k)];
        for (gidx j = gm.fv_offsets[static_cast<std::size_t>(f)]; j < gm.fv_offsets[static_cast<std::size_t>(f) + 1]; ++j) {
          const idx v = gm.fv[static_cast<std::size_t>(j)];
          if (stamp[static_cast<std::size_t>(v)] == static_cast<std::int64_t>(i)) continue;
          stamp[static_cast<std::size_t>(v)] = static_cast<std::int64_t>(i);
          cell_pts.push_back(v);
          ++total;
        }
      }
      conn_offsets.push_back(total);
    }
  }
  out << "<Cells>\n";
  out << "<DataArray type=\"Int64\" Name=\"connectivity\">\n";
  for (idx v : cell_pts) out << v << "\n";
  out << "</DataArray>\n<DataArray type=\"Int64\" Name=\"offsets\">\n";
  for (auto v : conn_offsets) out << v << "\n";
  out << "</DataArray>\n<DataArray type=\"UInt8\" Name=\"types\">\n";
  for (gidx i = 0; i < gm.n_cells; ++i) out << "42\n";
  out << "</DataArray>\n";

  // faces array: per cell: nfaces, then (npts, pt ids...)
  std::vector<std::int64_t> faces, faceoffs;
  faces.reserve(static_cast<std::size_t>(gm.n_faces) * 6);
  faceoffs.reserve(static_cast<std::size_t>(gm.n_cells));
  for (gidx i = 0; i < gm.n_cells; ++i) {
    const gidx nf = gm.cf_offsets[static_cast<std::size_t>(i) + 1] - gm.cf_offsets[static_cast<std::size_t>(i)];
    faces.push_back(nf);
    for (gidx k = gm.cf_offsets[static_cast<std::size_t>(i)]; k < gm.cf_offsets[static_cast<std::size_t>(i) + 1]; ++k) {
      const idx f = gm.cf_faces[static_cast<std::size_t>(k)];
      const gidx b = gm.fv_offsets[static_cast<std::size_t>(f)];
      const gidx e = gm.fv_offsets[static_cast<std::size_t>(f) + 1];
      faces.push_back(e - b);
      for (gidx j = b; j < e; ++j) faces.push_back(gm.fv[static_cast<std::size_t>(j)]);
    }
    faceoffs.push_back(static_cast<std::int64_t>(faces.size()));
  }
  out << "<DataArray type=\"Int64\" Name=\"faces\">\n";
  for (auto v : faces) out << v << "\n";
  out << "</DataArray>\n<DataArray type=\"Int64\" Name=\"faceoffsets\">\n";
  for (auto v : faceoffs) out << v << "\n";
  out << "</DataArray>\n</Cells>\n";

  auto cell_field = [&](const char* name, auto getter) {
    out << "<DataArray type=\"Float64\" Name=\"" << name << "\" format=\"ascii\">\n";
    for (gidx i = 0; i < gm.n_cells; ++i) out << getter(i) << "\n";
    out << "</DataArray>\n";
  };
  out << "<CellData>\n";
  cell_field("volume", [&](gidx i) { return gm.vol[static_cast<std::size_t>(i)]; });
  cell_field("omega_shield", [&](gidx i) { return gm.omega[static_cast<std::size_t>(i)]; });
  cell_field("theta_max_deg", [&](gidx i) { return gm.cell_theta[static_cast<std::size_t>(i)]; });
  cell_field("skew_max", [&](gidx i) { return gm.cell_skew[static_cast<std::size_t>(i)]; });
  cell_field("vol_ratio", [&](gidx i) { return gm.vol_ratio[static_cast<std::size_t>(i)]; });
  cell_field("quality", [&](gidx i) { return static_cast<double>(gm.cell_quality[static_cast<std::size_t>(i)]); });
  out << "</CellData>\n</Piece>\n</UnstructuredGrid>\n</VTKFile>\n";
  log_info("wrote " + path);
}

void write_vtk_surface(const std::string& path, const GraphMesh& gm) {
  std::ofstream out(path);
  if (!out) fail("cannot write " + path);
  out.setf(std::ios::scientific);
  out.precision(10);
  // boundary faces only, with a vertex index map
  std::vector<idx> remap(gm.points.size(), -1);
  std::vector<idx> used;
  std::vector<std::vector<idx>> polys;
  std::vector<double> patch_id;
  for (gidx f = gm.n_internal; f < gm.n_faces; ++f) {
    std::vector<idx> poly;
    for (gidx j = gm.fv_offsets[static_cast<std::size_t>(f)]; j < gm.fv_offsets[static_cast<std::size_t>(f) + 1]; ++j) {
      const idx v = gm.fv[static_cast<std::size_t>(j)];
      if (remap[static_cast<std::size_t>(v)] < 0) {
        remap[static_cast<std::size_t>(v)] = static_cast<idx>(used.size());
        used.push_back(v);
      }
      poly.push_back(remap[static_cast<std::size_t>(v)]);
    }
    polys.push_back(std::move(poly));
    patch_id.push_back(static_cast<double>(gm.patch_of[static_cast<std::size_t>(f)]));
  }
  out << "# vtk DataFile Version 3.0\nDiFVM boundary skin\nASCII\nDATASET POLYDATA\n";
  out << "POINTS " << used.size() << " double\n";
  for (idx v : used) out << gm.points[static_cast<std::size_t>(v)].x << " "
                         << gm.points[static_cast<std::size_t>(v)].y << " "
                         << gm.points[static_cast<std::size_t>(v)].z << "\n";
  std::size_t total = 0;
  for (const auto& p : polys) total += p.size() + 1;
  out << "POLYGONS " << polys.size() << " " << total << "\n";
  for (const auto& p : polys) {
    out << p.size();
    for (idx v : p) out << " " << v;
    out << "\n";
  }
  out << "CELL_DATA " << polys.size() << "\nSCALARS patch_id double 1\nLOOKUP_TABLE default\n";
  for (double p : patch_id) out << p << "\n";
  log_info("wrote " + path);
}

void write_vtk_points(const std::string& path, const GraphMesh& gm) {
  std::ofstream out(path);
  if (!out) fail("cannot write " + path);
  out.setf(std::ios::scientific);
  out.precision(8);
  out << "# vtk DataFile Version 3.0\nDiFVM cell centres (MeshErrorMap)\nASCII\nDATASET UNSTRUCTURED_GRID\n";
  out << "POINTS " << gm.n_cells << " double\n";
  for (gidx i = 0; i < gm.n_cells; ++i) {
    const Vec3 c = gm.cf.get(static_cast<std::size_t>(i));
    out << c.x << " " << c.y << " " << c.z << "\n";
  }
  out << "CELLS " << gm.n_cells << " " << gm.n_cells * 2 << "\n";
  for (gidx i = 0; i < gm.n_cells; ++i) out << "1 " << i << "\n";
  out << "CELL_TYPES " << gm.n_cells << "\n";
  for (gidx i = 0; i < gm.n_cells; ++i) out << "1\n";
  auto field = [&](const char* name, auto getter) {
    out << "SCALARS " << name << " double 1\nLOOKUP_TABLE default\n";
    for (gidx i = 0; i < gm.n_cells; ++i) out << getter(i) << "\n";
  };
  out << "POINT_DATA " << gm.n_cells << "\n";
  field("volume", [&](gidx i) { return gm.vol[static_cast<std::size_t>(i)]; });
  field("omega", [&](gidx i) { return gm.omega[static_cast<std::size_t>(i)]; });
  field("theta_max_deg", [&](gidx i) { return gm.cell_theta[static_cast<std::size_t>(i)]; });
  field("skew_max", [&](gidx i) { return gm.cell_skew[static_cast<std::size_t>(i)]; });
  log_info("wrote " + path);
}

// ---------------------------------------------------------------------------
MeshFormat detect_format(const std::string& path) {
  auto ends_with = [](const std::string& s, const char* suf) {
    const std::size_t n = std::strlen(suf);
    return s.size() >= n && s.compare(s.size() - n, n, suf) == 0;
  };
  if (ends_with(path, ".difvmmesh") || ends_with(path, ".dfvm")) return MeshFormat::kBinary;
  if (ends_with(path, ".vtk")) return MeshFormat::kVtkLegacy;
  if (ends_with(path, ".vtu")) return MeshFormat::kVtu;
  {
    std::ifstream in(path + "/constant/polyMesh/points");
    if (in) return MeshFormat::kOpenFOAMCase;
    std::ifstream in2(path + "/points");
    if (in2) return MeshFormat::kOpenFOAMCase;
  }
  return MeshFormat::kUnknown;
}

PolyMeshRaw load_polymesh(const std::string& path, MeshFormat fmt) {
  if (fmt == MeshFormat::kUnknown) fmt = detect_format(path);
  switch (fmt) {
    case MeshFormat::kOpenFOAMCase: return read_openfoam_case(path);
    case MeshFormat::kVtkLegacy:
    case MeshFormat::kVtu: {
      PolyMeshRaw raw = read_vtk(path);
      return raw;
    }
    case MeshFormat::kBinary: fail("load_polymesh: binary input must be read with read_binary()");
    default: fail("cannot detect mesh format of '" + path + "'");
  }
}

}  // namespace difvm
