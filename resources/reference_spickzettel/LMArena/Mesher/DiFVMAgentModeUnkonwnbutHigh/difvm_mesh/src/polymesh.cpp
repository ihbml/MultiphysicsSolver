#include "difvm/polymesh.hpp"

#include <algorithm>
#include <cstdio>
#include <string>

namespace difvm {

std::vector<char> PolyMeshRaw::used_points() const {
  std::vector<char> used(points.size(), 0);
  for (idx v : face_verts) used[static_cast<std::size_t>(v)] = 1;
  return used;
}

void PolyMeshRaw::bounds(Vec3& lo, Vec3& hi) const {
  const std::vector<char> used = used_points();
  lo = Vec3{std::numeric_limits<double>::max(), std::numeric_limits<double>::max(),
            std::numeric_limits<double>::max()};
  hi = -lo;
  for (std::size_t i = 0; i < points.size(); ++i) {
    if (!used[i]) continue;
    lo = vmin(lo, points[i]);
    hi = vmax(hi, points[i]);
  }
  if (lo.x > hi.x) { lo = Vec3{0, 0, 0}; hi = Vec3{0, 0, 0}; }
}

void PolyMeshRaw::validate() const {
  if (points.empty()) fail("PolyMeshRaw: no points");
  if (owner.empty()) fail("PolyMeshRaw: no faces");
  if (face_offsets.size() != owner.size() + 1)
    fail("PolyMeshRaw: face_offsets size " + std::to_string(face_offsets.size()) +
         " != n_faces+1 = " + std::to_string(owner.size() + 1));
  if (face_offsets.back() != static_cast<gidx>(face_verts.size()))
    fail("PolyMeshRaw: face_offsets.back() != face_verts.size()");
  if (neighbour.size() > owner.size())
    fail("PolyMeshRaw: more neighbours than faces");

  const idx np = n_points();
  const gidx nc = static_cast<gidx>(owner.size());
  for (idx f = 0; f < n_faces(); ++f) {
    const idx nv = n_face_verts(f);
    if (nv < 3) fail("PolyMeshRaw: face " + std::to_string(f) + " has < 3 vertices");
    for (idx k = 0; k < nv; ++k) {
      const idx v = face_verts[static_cast<std::size_t>(face_offsets[static_cast<std::size_t>(f)]) +
                              static_cast<std::size_t>(k)];
      if (v < 0 || v >= np)
        fail("PolyMeshRaw: face " + std::to_string(f) + " references point " + std::to_string(v) +
             " outside [0," + std::to_string(np) + ")");
    }
    if (owner[static_cast<std::size_t>(f)] < 0 || owner[static_cast<std::size_t>(f)] >= n_cells)
      fail("PolyMeshRaw: face " + std::to_string(f) + " owner out of range");
    if (f < n_internal() && (neighbour[static_cast<std::size_t>(f)] < 0 ||
                             neighbour[static_cast<std::size_t>(f)] >= n_cells))
      fail("PolyMeshRaw: face " + std::to_string(f) + " neighbour out of range");
  }
  // patch coverage (an empty patch list is legal: generic VTK / STL sources
  // hand over unpaired skin faces which the compiler welds and re-patches)
  if (patches.empty()) return;
  idx cursor = n_internal();
  for (const auto& p : patches) {
    if (p.start != cursor)
      fail("PolyMeshRaw: patch '" + p.name + "' start " + std::to_string(p.start) +
           " != expected " + std::to_string(cursor));
    cursor += p.size;
  }
  if (cursor != n_faces())
    fail("PolyMeshRaw: patches cover " + std::to_string(cursor) + " of " +
         std::to_string(n_faces()) + " faces");
  (void)nc;
}

std::string PolyMeshRaw::summary() const {
  char buf[512];
  Vec3 lo, hi;
  bounds(lo, hi);
  std::snprintf(buf, sizeof(buf),
                "PolyMeshRaw: cells=%lld points=%d faces=%d (internal=%d, boundary=%d) "
                "patches=%zu face-verts=%zu bbox=[%.3g %.3g %.3g]..[%.3g %.3g %.3g]",
                static_cast<long long>(n_cells), n_points(), n_faces(), n_internal(),
                n_faces() - n_internal(), patches.size(), face_verts.size(), lo.x, lo.y, lo.z,
                hi.x, hi.y, hi.z);
  return std::string(buf);
}

PatchRole infer_patch_role(const std::string& name, const std::string& type) {
  auto has = [](const std::string& s, const char* sub) {
    return s.find(sub) != std::string::npos;
  };
  if (type == "empty") return PatchRole::kEmpty;
  if (type == "wedge") return PatchRole::kWedge;
  if (type == "symmetry" || type == "symmetryPlane" || type == "symmetry plane")
    return PatchRole::kSymmetry;
  if (type == "wall" || has(type, "Wall") || has(name, "wall")) return PatchRole::kWall;
  if (has(name, "inlet") || has(name, "in")) return PatchRole::kInlet;
  if (has(name, "outlet") || has(name, "out")) return PatchRole::kOutlet;
  if (has(name, "interface")) return PatchRole::kInterface;
  return PatchRole::kUnknown;
}

}  // namespace difvm
