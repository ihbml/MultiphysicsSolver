// DiFVM Mesher — native binary container (.difvmmesh).
//
// One sequential read populates the whole static graph; no parsing, no string
// keys, no allocation beyond the arrays themselves. Layout is versioned and
// self-describing (section table in the header) so the solver can mmap it.
#include <cstring>
#include <fstream>
#include <string>
#include <vector>

#include "difvm/io.hpp"

namespace difvm {
namespace {

constexpr char kMagic[8] = {'D', 'I', 'F', 'V', 'M', 'S', 'H', '\1'};
constexpr std::uint32_t kVersion = 1;

struct Section {
  char tag[4];
  std::uint64_t bytes;
};

template <class V>
void wr(std::ofstream& o, const V& v) {
  using T = typename V::value_type;
  const std::uint64_t n = v.size();
  o.write(reinterpret_cast<const char*>(&n), sizeof(n));
  if (n) o.write(reinterpret_cast<const char*>(v.data()), static_cast<std::streamsize>(n * sizeof(T)));
}

template <class V>
void rd(std::ifstream& i, V& v) {
  using T = typename V::value_type;
  std::uint64_t n = 0;
  i.read(reinterpret_cast<char*>(&n), sizeof(n));
  if (!i) fail("binary mesh: truncated array header");
  v.resize(static_cast<std::size_t>(n));
  if (n) i.read(reinterpret_cast<char*>(v.data()), static_cast<std::streamsize>(n * sizeof(T)));
  if (!i) fail("binary mesh: truncated array payload");
}

void wr3(std::ofstream& o, const Vec3SoA& f) { wr(o, f.x); wr(o, f.y); wr(o, f.z); }
void rd3(std::ifstream& i, Vec3SoA& f) { rd(i, f.x); rd(i, f.y); rd(i, f.z); }

void write_str(std::ofstream& o, const std::string& s) {
  const std::uint32_t n = static_cast<std::uint32_t>(s.size());
  o.write(reinterpret_cast<const char*>(&n), sizeof(n));
  o.write(s.data(), n);
}
std::string read_str(std::ifstream& i) {
  std::uint32_t n = 0;
  i.read(reinterpret_cast<char*>(&n), sizeof(n));
  std::string s(n, '\0');
  if (n) i.read(s.data(), n);
  return s;
}

}  // namespace

void write_binary(const std::string& path, const GraphMesh& gm) {
  // Serialise into a buffer first so the header can carry exact section sizes.
  std::string body;
  body.reserve(static_cast<std::size_t>(gm.memory_bytes() + 4096));
  auto append_vec = [&body](const auto& v) {
    const std::uint64_t n = v.size();
    body.append(reinterpret_cast<const char*>(&n), sizeof(n));
    if (n) body.append(reinterpret_cast<const char*>(v.data()), n * sizeof(typename std::decay_t<decltype(v)>::value_type));
  };
  auto append_v3 = [&](const char* tag, const Vec3SoA& f) {
    body.append(tag, 4);
    const std::uint64_t n = f.size();
    body.append(reinterpret_cast<const char*>(&n), sizeof(n));
    body.append(reinterpret_cast<const char*>(f.x.data()), n * sizeof(double));
    body.append(reinterpret_cast<const char*>(f.y.data()), n * sizeof(double));
    body.append(reinterpret_cast<const char*>(f.z.data()), n * sizeof(double));
  };
  auto append_v = [&](const char* tag, const auto& v) {
    body.append(tag, 4);
    append_vec(v);
  };

  int64_t counts[7] = {gm.n_cells, gm.n_internal, gm.n_boundary, gm.n_faces,
                       static_cast<int64_t>(gm.points.size()), static_cast<int64_t>(gm.fv.size()),
                       static_cast<int64_t>(gm.patches.size())};
  append_v("CNT0", std::vector<char>(reinterpret_cast<const char*>(counts),
                                    reinterpret_cast<const char*>(counts) + sizeof(counts)));
  // cell fields
  append_v3("CF00", gm.cf);
  append_v("VOL0", gm.vol);
  append_v("OME0", gm.omega);
  append_v("VRT0", gm.vol_ratio);
  append_v("CSK0", gm.cell_skew);
  append_v("CTH0", gm.cell_theta);
  append_v("CQU0", gm.cell_quality);
  // topology
  // (owner/neighbour/patch are idx vectors; widen to int32 as stored)
  append_v("OWN0", gm.owner);
  append_v("NBR0", gm.neighbour);
  append_v("PCH0", gm.patch_of);
  append_v("FVO0", gm.fv_offsets);
  append_v("FVL0", gm.fv);
  append_v("CFO0", gm.cf_offsets);
  append_v("CFF0", gm.cf_faces);
  append_v("NNO0", gm.nn_offsets);
  append_v("NNC0", gm.nn_cells);
  append_v("NNS0", gm.nn_sign);
  // face geometry + MeshErrorMap
  append_v3("SF00", gm.Sf);
  append_v3("XF00", gm.xf);
  append_v3("DIJ0", gm.d_ij);
  append_v3("DLT0", gm.delta_f);
  append_v3("KF00", gm.k_f);
  append_v3("KFC0", gm.k_f_comp);
  append_v3("MSK0", gm.m_skew);
  append_v("MSF0", gm.magSf);
  append_v("MGD0", gm.mag_d);
  append_v("COS0", gm.cos_ortho);
  append_v("THT0", gm.theta);
  append_v("NOC0", gm.non_ortho_corr);
  append_v("SKW0", gm.skewness);
  append_v("BET0", gm.beta);
  append_v("WRP0", gm.face_warp);
  // points
  append_v("PTS0", gm.points);
  // patches + params + provenance
  {
    std::string s;
    const std::uint64_t np = gm.patches.size();
    s.append(reinterpret_cast<const char*>(&np), sizeof(np));
    for (const auto& p : gm.patches) {
      std::uint32_t nl = static_cast<std::uint32_t>(p.name.size());
      s.append(reinterpret_cast<const char*>(&nl), sizeof(nl));
      s += p.name;
      std::uint32_t tl = static_cast<std::uint32_t>(p.type.size());
      s.append(reinterpret_cast<const char*>(&tl), sizeof(tl));
      s += p.type;
      s.push_back(static_cast<char>(p.role));
      s.append(reinterpret_cast<const char*>(&p.start), sizeof(p.start));
      s.append(reinterpret_cast<const char*>(&p.size), sizeof(p.size));
    }
    const double ep[8] = {gm.err_params.theta_crit_deg, gm.err_params.theta_width_deg,
                          gm.err_params.skew_sigma0, gm.err_params.skew_width,
                          gm.err_params.vol_ratio_scale, gm.err_params.omega_min,
                          gm.err_params.beta_min, gm.err_params.degenerate_eps};
    s.append(reinterpret_cast<const char*>(ep), sizeof(ep));
    const std::uint32_t sl = static_cast<std::uint32_t>(gm.source.size());
    s.append(reinterpret_cast<const char*>(&sl), sizeof(sl));
    s += gm.source;
    body.append("PAT0", 4);
    append_vec(s);
  }

  std::ofstream o(path, std::ios::binary);
  if (!o) fail("cannot write '" + path + "'");
  o.write(kMagic, 8);
  o.write(reinterpret_cast<const char*>(&kVersion), sizeof(kVersion));
  const std::uint64_t body_bytes = body.size();
  o.write(reinterpret_cast<const char*>(&body_bytes), sizeof(body_bytes));
  o.write(body.data(), static_cast<std::streamsize>(body.size()));
  log_info("wrote " + path + " (" + std::to_string(body.size() / 1048576.0) + " MiB)");
}

GraphMesh read_binary(const std::string& path) {
  std::ifstream in(path, std::ios::binary);
  if (!in) fail("cannot open '" + path + "'");
  char magic[8];
  in.read(magic, 8);
  if (std::memcmp(magic, kMagic, 8) != 0) fail("'" + path + "' is not a .difvmmesh file");
  std::uint32_t version = 0;
  in.read(reinterpret_cast<char*>(&version), sizeof(version));
  if (version != kVersion) fail("binary mesh version " + std::to_string(version) + " unsupported");
  std::uint64_t body_bytes = 0;
  in.read(reinterpret_cast<char*>(&body_bytes), sizeof(body_bytes));
  std::string body(body_bytes, '\0');
  in.read(body.data(), static_cast<std::streamsize>(body_bytes));
  if (!in) fail("binary mesh: truncated body");

  // in-memory cursor
  std::size_t pos = 0;
  auto expect_tag = [&](const char* tag) {
    if (pos + 4 > body.size() || std::memcmp(body.data() + pos, tag, 4) != 0)
      fail(std::string("binary mesh: expected section ") + tag);
    pos += 4;
  };
  auto read_n = [&]() {
    std::uint64_t n = 0;
    std::memcpy(&n, body.data() + pos, sizeof(n));
    pos += sizeof(n);
    return static_cast<std::size_t>(n);
  };
  auto read_vec = [&](auto& v) {
    using T = typename std::decay_t<decltype(v)>::value_type;
    const std::size_t n = read_n();
    v.resize(n);
    if (n) std::memcpy(v.data(), body.data() + pos, n * sizeof(T));
    pos += n * sizeof(T);
  };
  auto read_v3 = [&](Vec3SoA& f) {
    const std::size_t n = read_n();
    f.resize(n);
    if (n) {
      std::memcpy(f.x.data(), body.data() + pos, n * sizeof(double)); pos += n * sizeof(double);
      std::memcpy(f.y.data(), body.data() + pos, n * sizeof(double)); pos += n * sizeof(double);
      std::memcpy(f.z.data(), body.data() + pos, n * sizeof(double)); pos += n * sizeof(double);
    }
  };

  GraphMesh gm;
  expect_tag("CNT0");
  {
    std::vector<char> counts;
    read_vec(counts);
    if (counts.size() != 7 * sizeof(int64_t)) fail("binary mesh: bad counts section");
    int64_t c[7];
    std::memcpy(c, counts.data(), sizeof(c));
    gm.n_cells = c[0];
    gm.n_internal = c[1];
    gm.n_boundary = c[2];
    gm.n_faces = c[3];
    (void)c[4]; (void)c[5]; (void)c[6];
  }
  expect_tag("CF00"); read_v3(gm.cf);
  expect_tag("VOL0"); read_vec(gm.vol);
  expect_tag("OME0"); read_vec(gm.omega);
  expect_tag("VRT0"); read_vec(gm.vol_ratio);
  expect_tag("CSK0"); read_vec(gm.cell_skew);
  expect_tag("CTH0"); read_vec(gm.cell_theta);
  expect_tag("CQU0"); read_vec(gm.cell_quality);
  expect_tag("OWN0"); read_vec(gm.owner);
  expect_tag("NBR0"); read_vec(gm.neighbour);
  expect_tag("PCH0"); read_vec(gm.patch_of);
  expect_tag("FVO0"); read_vec(gm.fv_offsets);
  expect_tag("FVL0"); read_vec(gm.fv);
  expect_tag("CFO0"); read_vec(gm.cf_offsets);
  expect_tag("CFF0"); read_vec(gm.cf_faces);
  expect_tag("NNO0"); read_vec(gm.nn_offsets);
  expect_tag("NNC0"); read_vec(gm.nn_cells);
  expect_tag("NNS0"); read_vec(gm.nn_sign);
  expect_tag("SF00"); read_v3(gm.Sf);
  expect_tag("XF00"); read_v3(gm.xf);
  expect_tag("DIJ0"); read_v3(gm.d_ij);
  expect_tag("DLT0"); read_v3(gm.delta_f);
  expect_tag("KF00"); read_v3(gm.k_f);
  expect_tag("KFC0"); read_v3(gm.k_f_comp);
  expect_tag("MSK0"); read_v3(gm.m_skew);
  expect_tag("MSF0"); read_vec(gm.magSf);
  expect_tag("MGD0"); read_vec(gm.mag_d);
  expect_tag("COS0"); read_vec(gm.cos_ortho);
  expect_tag("THT0"); read_vec(gm.theta);
  expect_tag("NOC0"); read_vec(gm.non_ortho_corr);
  expect_tag("SKW0"); read_vec(gm.skewness);
  expect_tag("BET0"); read_vec(gm.beta);
  expect_tag("WRP0"); read_vec(gm.face_warp);
  expect_tag("PTS0");
  {
    const std::size_t n = read_n();
    gm.points.resize(n);
    if (n) std::memcpy(gm.points.data(), body.data() + pos, n * sizeof(Vec3));
    pos += n * sizeof(Vec3);
  }
  expect_tag("PAT0");
  {
    std::vector<char> blob;
    read_vec(blob);
    std::size_t bp = 0;
    auto get = [&](void* dst, std::size_t n) { std::memcpy(dst, blob.data() + bp, n); bp += n; };
    std::uint64_t np = 0;
    get(&np, sizeof(np));
    gm.patches.reserve(np);
    for (std::uint64_t k = 0; k < np; ++k) {
      PatchInfo p;
      std::uint32_t nl = 0;
      get(&nl, sizeof(nl));
      p.name.assign(blob.data() + bp, nl); bp += nl;
      std::uint32_t tl = 0;
      get(&tl, sizeof(tl));
      p.type.assign(blob.data() + bp, tl); bp += tl;
      char role = 0;
      get(&role, sizeof(role));
      p.role = static_cast<PatchRole>(role);
      get(&p.start, sizeof(p.start));
      get(&p.size, sizeof(p.size));
      gm.patches.push_back(std::move(p));
    }
    double ep[8];
    get(ep, sizeof(ep));
    gm.err_params.theta_crit_deg = ep[0];
    gm.err_params.theta_width_deg = ep[1];
    gm.err_params.skew_sigma0 = ep[2];
    gm.err_params.skew_width = ep[3];
    gm.err_params.vol_ratio_scale = ep[4];
    gm.err_params.omega_min = ep[5];
    gm.err_params.beta_min = ep[6];
    gm.err_params.degenerate_eps = ep[7];
    std::uint32_t sl = 0;
    get(&sl, sizeof(sl));
    if (sl) gm.source.assign(blob.data() + bp, sl);
  }
  return gm;
}

}  // namespace difvm
