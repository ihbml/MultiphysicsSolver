// DiFVM Mesher — OpenFOAM polyMesh reader/writer (drop-in compatible).
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include "difvm/io.hpp"

namespace difvm {
namespace {

std::string read_file(const std::string& path) {
  std::ifstream in(path, std::ios::binary);
  if (!in) fail("cannot open '" + path + "'");
  std::ostringstream ss;
  ss << in.rdbuf();
  return ss.str();
}

bool file_exists(const std::string& p) {
  std::ifstream in(p, std::ios::binary);
  return static_cast<bool>(in);
}

// ---------------------------------------------------------------------------
// Minimal OpenFOAM tokenizer: strips // and /* */ comments, splits on
// whitespace and on the structural characters ( ) { } ;
// ---------------------------------------------------------------------------
class FoamLexer {
 public:
  explicit FoamLexer(const std::string& text) : s_(text) {}

  bool next(std::string& tok) {
    skip_ws();
    if (i_ >= s_.size()) return false;
    const char c = s_[i_];
    if (c == '(' || c == ')' || c == '{' || c == '}' || c == ';') {
      tok.assign(1, c);
      ++i_;
      return true;
    }
    const std::size_t b = i_;
    while (i_ < s_.size()) {
      const char d = s_[i_];
      if (std::isspace(static_cast<unsigned char>(d)) || d == '(' || d == ')' || d == '{' || d == '}' || d == ';')
        break;
      ++i_;
    }
    tok.assign(s_, b, i_ - b);
    return true;
  }

  // Skips an entire "FoamFile { ... }" (or any) dictionary header.
  void skip_dictionary_header() {
    std::size_t save = i_;
    std::string tok;
    if (!next(tok)) { i_ = save; return; }
    if (tok != "FoamFile") { i_ = save; return; }
    int depth = 0;
    while (next(tok)) {
      if (tok == "{") ++depth;
      else if (tok == "}") { if (--depth <= 0) break; }
    }
  }

  void expect(const char* what) {
    std::string tok;
    if (!next(tok) || tok != what)
      fail(std::string("OpenFOAM parse error: expected '") + what + "', got '" + tok + "'");
  }

  // Reads "<count> (" and returns count.
  long open_list() {
    std::string tok;
    if (!next(tok)) fail("OpenFOAM parse error: unexpected EOF");
    // optional typed wrapper e.g. "List<label>" / "vectorField"
    while (!is_number(tok)) {
      if (tok == "(") fail("OpenFOAM parse error: list without size");
      if (!next(tok)) fail("OpenFOAM parse error: unexpected EOF in list header");
    }
    const long n = std::strtol(tok.c_str(), nullptr, 10);
    expect("(");
    return n;
  }
  void close_list() { expect(")"); }

  bool peek_is(char c) {
    const std::size_t save = i_;
    std::string tok;
    const bool ok = next(tok);
    i_ = save;
    return ok && tok.size() == 1 && tok[0] == c;
  }

  bool read_number(double& v) {
    std::string tok;
    if (!next(tok)) return false;
    char* end = nullptr;
    v = std::strtod(tok.c_str(), &end);
    return end != tok.c_str();
  }
  bool read_long(long& v) {
    std::string tok;
    if (!next(tok)) return false;
    char* end = nullptr;
    v = std::strtol(tok.c_str(), &end, 10);
    return end != tok.c_str();
  }
  std::string read_word() {
    std::string tok;
    next(tok);
    return tok;
  }

  static bool is_number(const std::string& t) {
    if (t.empty()) return false;
    char* end = nullptr;
    std::strtod(t.c_str(), &end);
    return end == t.c_str() + t.size();
  }

 private:
  void skip_ws() {
    for (;;) {
      while (i_ < s_.size() && std::isspace(static_cast<unsigned char>(s_[i_]))) ++i_;
      if (i_ + 1 < s_.size() && s_[i_] == '/' && s_[i_ + 1] == '/') {
        while (i_ < s_.size() && s_[i_] != '\n') ++i_;
        continue;
      }
      if (i_ + 1 < s_.size() && s_[i_] == '/' && s_[i_ + 1] == '*') {
        i_ += 2;
        while (i_ + 1 < s_.size() && !(s_[i_] == '*' && s_[i_ + 1] == '/')) ++i_;
        i_ = std::min(s_.size(), i_ + 2);
        continue;
      }
      break;
    }
  }

  const std::string& s_;
  std::size_t i_ = 0;
};

std::vector<Vec3> read_points(const std::string& path) {
  const std::string text = read_file(path);
  FoamLexer lex(text);
  lex.skip_dictionary_header();
  const long n = lex.open_list();
  std::vector<Vec3> pts;
  pts.reserve(static_cast<std::size_t>(std::max(0L, n)));
  double x, y, z;
  for (long i = 0; i < n; ++i) {
    // "(" x y z ")"  or compact "x y z"
    if (lex.peek_is('(')) lex.expect("(");
    if (!lex.read_number(x) || !lex.read_number(y) || !lex.read_number(z))
      fail("OpenFOAM points: malformed entry " + std::to_string(i) + " in " + path);
    if (lex.peek_is(')')) lex.expect(")");
    pts.push_back(Vec3{x, y, z});
  }
  return pts;
}

// faces: "<n> ( k (v0 .. vk-1) k (...) )"  (also accepts compact form)
void read_faces(const std::string& path, std::vector<gidx>& offs, std::vector<idx>& verts) {
  const std::string text = read_file(path);
  FoamLexer lex(text);
  lex.skip_dictionary_header();
  const long n = lex.open_list();
  offs.clear();
  verts.clear();
  offs.reserve(static_cast<std::size_t>(std::max(0L, n)) + 1);
  long i = 0;
  while (i < n) {
    offs.push_back(static_cast<gidx>(verts.size()));
    long k = 0;
    if (lex.peek_is('(')) {
      // compact form: no per-face size, faces run until the list ends
      lex.expect("(");
      long v;
      while (!lex.peek_is(')') && lex.read_long(v)) verts.push_back(static_cast<idx>(v));
      lex.expect(")");
      ++i;
      continue;
    }
    if (!lex.read_long(k)) fail("OpenFOAM faces: missing size for face " + std::to_string(i));
    if (k < 3 || k > 4096) fail("OpenFOAM faces: implausible face size " + std::to_string(k));
    lex.expect("(");
    for (long j = 0; j < k; ++j) {
      long v;
      if (!lex.read_long(v)) fail("OpenFOAM faces: malformed face " + std::to_string(i));
      verts.push_back(static_cast<idx>(v));
    }
    lex.expect(")");
    ++i;
  }
  offs.push_back(static_cast<gidx>(verts.size()));
}

std::vector<idx> read_labels(const std::string& path) {
  const std::string text = read_file(path);
  FoamLexer lex(text);
  lex.skip_dictionary_header();
  const long n = lex.open_list();
  std::vector<idx> out;
  out.reserve(static_cast<std::size_t>(std::max(0L, n)));
  for (long i = 0; i < n; ++i) {
    long v;
    if (!lex.read_long(v)) fail("OpenFOAM labels: malformed entry in " + path);
    out.push_back(static_cast<idx>(v));
  }
  return out;
}

std::vector<PatchInfo> read_boundary(const std::string& path) {
  const std::string text = read_file(path);
  FoamLexer lex(text);
  lex.skip_dictionary_header();
  const long n = lex.open_list();
  std::vector<PatchInfo> patches;
  patches.reserve(static_cast<std::size_t>(std::max(0L, n)));
  for (long i = 0; i < n; ++i) {
    PatchInfo pi;
    pi.name = lex.read_word();
    lex.expect("{");
    std::string tok;
    while (lex.next(tok)) {
      if (tok == "}") break;
      if (tok == "type") {
        pi.type = lex.read_word();
        lex.expect(";");
      } else if (tok == "nFaces") {
        long v;
        if (!lex.read_long(v)) fail("OpenFOAM boundary: bad nFaces");
        pi.size = static_cast<idx>(v);
        lex.expect(";");
      } else if (tok == "startFace") {
        long v;
        if (!lex.read_long(v)) fail("OpenFOAM boundary: bad startFace");
        pi.start = static_cast<idx>(v);
        lex.expect(";");
      } else if (tok == "(") {
        // sub-dictionaries such as inGroups( ... ) / physicalType ...
        long depth = 1;
        std::string t2;
        while (depth > 0 && lex.next(t2)) {
          if (t2 == "(") ++depth;
          else if (t2 == ")") --depth;
        }
        lex.expect(";");
      } else if (tok == ";") {
        continue;
      } else {
        // unknown keyword + value(s) until ';' or nested group
        std::string t2;
        while (lex.next(t2)) {
          if (t2 == ";") break;
          if (t2 == "(") {
            long depth = 1;
            std::string t3;
            while (depth > 0 && lex.next(t3)) {
              if (t3 == "(") ++depth;
              else if (t3 == ")") --depth;
            }
            break;
          }
          if (t2 == "}") return patches;  // malformed: bail out
        }
      }
    }
    pi.role = infer_patch_role(pi.name, pi.type);
    patches.push_back(std::move(pi));
  }
  return patches;
}

std::string case_mesh_dir(const std::string& dir) {
  if (file_exists(dir + "/constant/polyMesh/points")) return dir + "/constant/polyMesh";
  if (file_exists(dir + "/polyMesh/points")) return dir + "/polyMesh";
  if (file_exists(dir + "/points")) return dir;
  fail("no polyMesh found in '" + dir + "' (looked for constant/polyMesh, polyMesh, .)");
}

void write_foam_header(std::ofstream& out, const char* cls, const char* obj) {
  out << "/*--------------------------------*- C++ -*----------------------------------*\\\n"
      << "| =========                 |                                                 |\n"
      << "| \\\\      /  F ield         | DiFVM Mesher export (OpenFOAM drop-in)          |\n"
      << "|  \\\\    /   O peration     |                                                 |\n"
      << "|   \\\\  /    A nd           |                                                 |\n"
      << "|    \\\\/     M anipulation  |                                                 |\n"
      << "\\*---------------------------------------------------------------------------*/\n"
      << "FoamFile\n{\n"
      << "    version     2.0;\n"
      << "    format      ascii;\n"
      << "    class       " << cls << ";\n"
      << "    object      " << obj << ";\n"
      << "}\n"
      << "// * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * //\n\n";
}

}  // namespace

// ---------------------------------------------------------------------------
PolyMeshRaw read_openfoam_case(const std::string& case_dir) {
  const std::string dir = case_mesh_dir(case_dir);
  PolyMeshRaw raw;
  raw.points = read_points(dir + "/points");
  read_faces(dir + "/faces", raw.face_offsets, raw.face_verts);
  raw.owner = read_labels(dir + "/owner");
  raw.neighbour = read_labels(dir + "/neighbour");
  raw.patches = read_boundary(dir + "/boundary");

  gidx nc = 0;
  for (idx c : raw.owner) nc = std::max<gidx>(nc, static_cast<gidx>(c) + 1);
  for (idx c : raw.neighbour) nc = std::max<gidx>(nc, static_cast<gidx>(c) + 1);
  raw.n_cells = nc;
  // Drop zero-face patches (legal in OpenFOAM, meaningless here).
  if (!raw.patches.empty()) {
    std::vector<PatchInfo> keep;
    gidx expect_start = raw.n_internal();
    for (auto& p : raw.patches) {
      if (p.start < expect_start) p.start = static_cast<idx>(expect_start);
      if (p.size > 0) keep.push_back(p);
      expect_start = p.start + p.size;
    }
    raw.patches.swap(keep);
  }
  return raw;
}

void write_openfoam_case(const std::string& case_dir, const GraphMesh& gm) {
  const std::string dir = case_dir + "/constant/polyMesh";
  if (std::system((std::string("mkdir -p '") + dir + "'").c_str()) != 0)
    fail("cannot create directory " + dir);

  {  // points
    std::ofstream out(dir + "/points");
    if (!out) fail("cannot write points");
    write_foam_header(out, "vectorField", "points");
    out << gm.points.size() << "\n(\n";
    out.setf(std::ios::scientific);
    out.precision(15);
    for (const Vec3& p : gm.points) out << "(" << p.x << " " << p.y << " " << p.z << ")\n";
    out << ")\n";
  }
  {  // faces
    std::ofstream out(dir + "/faces");
    if (!out) fail("cannot write faces");
    write_foam_header(out, "faceList", "faces");
    out << gm.n_faces << "\n(\n";
    for (gidx f = 0; f < gm.n_faces; ++f) {
      const gidx b = gm.fv_offsets[static_cast<std::size_t>(f)];
      const gidx e = gm.fv_offsets[static_cast<std::size_t>(f) + 1];
      out << (e - b) << "(";
      for (gidx k = b; k < e; ++k) out << (k > b ? " " : "") << gm.fv[static_cast<std::size_t>(k)];
      out << ")\n";
    }
    out << ")\n";
  }
  auto write_labels = [&](const char* name, const std::vector<idx>& v, gidx n) {
    std::ofstream out(dir + "/" + name);
    if (!out) fail(std::string("cannot write ") + name);
    write_foam_header(out, "labelList", name);
    out << n << "\n(\n";
    for (gidx i = 0; i < n; ++i) out << v[static_cast<std::size_t>(i)] << "\n";
    out << ")\n";
  };
  write_labels("owner", std::vector<idx>(gm.owner.begin(), gm.owner.end()), gm.n_faces);
  {
    std::vector<idx> nb(gm.neighbour.begin(), gm.neighbour.begin() + static_cast<std::ptrdiff_t>(gm.n_internal));
    write_labels("neighbour", nb, gm.n_internal);
  }
  {  // boundary
    std::ofstream out(dir + "/boundary");
    if (!out) fail("cannot write boundary");
    write_foam_header(out, "polyBoundaryMesh", "boundary");
    out << gm.patches.size() << "\n(\n";
    for (const auto& p : gm.patches) {
      out << "    " << p.name << "\n    {\n"
          << "        type            " << p.type << ";\n"
          << "        nFaces          " << p.size << ";\n"
          << "        startFace       " << p.start << ";\n"
          << "    }\n";
    }
    out << ")\n";
  }
  log_info("wrote OpenFOAM case: " + dir);
}

}  // namespace difvm
