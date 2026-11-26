#include "linear_octree.hpp"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <limits>
#include <memory>

#ifdef _OPENMP
#include <omp.h>
#endif

namespace cfd::octree {

namespace {

inline int64_t find_axis_idx(const std::vector<double>& c, double val, int64_t n) noexcept {
  if (n <= 1) return 0;
  auto it = std::upper_bound(c.begin() + 1, c.end() - 1, val);
  return std::clamp<int64_t>(static_cast<int64_t>(it - c.begin()) - 1, 0, n - 1);
}

inline double lattice_to_coord(const std::vector<double>& c, int64_t n_axis, int64_t idx19) noexcept {
  const int64_t r = idx19 >> kMaxOctreeLevel;
  const uint32_t rem = static_cast<uint32_t>(idx19 & kLatticeMask);
  if (r >= n_axis) return c[static_cast<size_t>(n_axis)];
  if (rem == 0) return c[static_cast<size_t>(r)];
  const double c0 = c[static_cast<size_t>(r)], c1 = c[static_cast<size_t>(r + 1)];
  return c0 + (c1 - c0) * (static_cast<double>(rem) * (1.0 / static_cast<double>(kLatticePerRoot)));
}

constexpr int kDirDelta[6][3] = {{-1,0,0},{1,0,0},{0,-1,0},{0,1,0},{0,0,-1},{0,0,1}};
constexpr int kFaceCorners[6][4] = {{0,4,6,2},{1,3,7,5},{0,1,5,4},{2,6,7,3},{0,2,3,1},{4,5,7,6}};
constexpr uint16_t kFarfieldPatch[6] = {2u, 3u, 4u, 4u, 5u, 6u};

template <typename T>
inline void prefix_sum_exclusive(std::vector<T>& v) noexcept {
  for (size_t i = 0; i + 1 < v.size(); ++i) v[i + 1] += v[i];
}

template <typename Fn>
int32_t omp_for_range(int64_t count, bool dynamic_sched, Fn&& fn) {
  std::atomic<bool> failed{false};
#ifdef _OPENMP
#pragma omp parallel
  {
    try {
      int32_t prev_facet = -1;
      if (dynamic_sched) {
#pragma omp for schedule(dynamic, 8)
        for (int64_t i = 0; i < count; ++i) { if (!failed.load(std::memory_order_relaxed)) fn(i, prev_facet); }
      } else {
#pragma omp for schedule(static)
        for (int64_t i = 0; i < count; ++i) { if (!failed.load(std::memory_order_relaxed)) fn(i, prev_facet); }
      }
    } catch (...) { failed.store(true, std::memory_order_relaxed); }
  }
#else
  (void)dynamic_sched;
  int32_t prev_facet = -1;
  for (int64_t i = 0; i < count; ++i) fn(i, prev_facet);
#endif
  return failed.load(std::memory_order_relaxed) ? -2 : 0;
}

template <typename MaskRead, typename TrackPeak>
int32_t subdivide_impl(std::vector<Leaf>& leaves, MaskRead&& is_split, size_t extra_bytes, TrackPeak&& track_peak) {
  const int64_t n = static_cast<int64_t>(leaves.size());
  if (n == 0) return 0;
  constexpr int64_t kBlock = 4096;
  const int64_t nb = (n + kBlock - 1) / kBlock;
  std::vector<int64_t> offsets(static_cast<size_t>(nb) + 1, 0);
  if (omp_for_range(nb, false, [&](int64_t b, int32_t&) {
        int64_t cnt = 0;
        for (int64_t i = b * kBlock, end = std::min(n, (b + 1) * kBlock); i < end; ++i) cnt += is_split(i) ? 8 : 1;
        offsets[static_cast<size_t>(b) + 1] = cnt;
      }) != 0) return -2;
  prefix_sum_exclusive(offsets);
  const int64_t total_next = offsets[static_cast<size_t>(nb)];
  if (total_next == n) return 0;

  std::vector<Leaf> next_leaves(static_cast<size_t>(total_next));
  track_peak(next_leaves.capacity() * sizeof(Leaf) + extra_bytes);
  if (omp_for_range(nb, false, [&](int64_t b, int32_t&) {
        int64_t dst = offsets[static_cast<size_t>(b)];
        for (int64_t i = b * kBlock, end = std::min(n, (b + 1) * kBlock); i < end; ++i) {
          const Leaf& lf = leaves[static_cast<size_t>(i)];
          if (!is_split(i)) { next_leaves[static_cast<size_t>(dst++)] = lf; continue; }
          const uint8_t cl = static_cast<uint8_t>(leaf_level(lf.key) + 1u);
          const uint64_t bm = leaf_morton(lf.key), step = uint64_t{1} << (3u * (kMaxOctreeLevel - cl));
          for (uint64_t c = 0; c < 8u; ++c) next_leaves[static_cast<size_t>(dst++)] = Leaf{make_leaf_key(bm + c * step, cl), lf.root, lf.zone, lf.flags, 0u};
        }
      }) != 0) return -2;
  leaves.swap(next_leaves);
  std::vector<Leaf>().swap(next_leaves);
  return 0;
}

template <typename Fn>
void visit_26_neighbors(int64_t ox, int64_t oy, int64_t oz, int64_t sz, Fn&& fn) {
  const int64_t h = sz >> 1;
  for (int di = -1; di <= 1; ++di) for (int dj = -1; dj <= 1; ++dj) for (int dk = -1; dk <= 1; ++dk) if (di || dj || dk)
    fn((di < 0) ? (ox - 1) : (di > 0 ? (ox + sz) : (ox + h)),
       (dj < 0) ? (oy - 1) : (dj > 0 ? (oy + sz) : (oy + h)),
       (dk < 0) ? (oz - 1) : (dk > 0 ? (oz + sz) : (oz + h)));
}

}  // namespace

void LinearMortonForest::update_peak_bytes(size_t extra) noexcept {
  auto cap_b = [](const auto& v) noexcept { return static_cast<uint64_t>(v.capacity()) * sizeof(v[0]); };
  const uint64_t cur = cap_b(leaves_) + cap_b(sample_keys_) + cap_b(sample_offsets_) + cap_b(leaf_cell_id_) +
      cap_b(leaf_vertex_offset_) + cap_b(vertex_coords_) + cap_b(regular_faces_) + cap_b(general_faces_) +
      cap_b(x_coords_) + cap_b(y_coords_) + cap_b(z_coords_) + static_cast<uint64_t>(sdf_bytes_ + extra);
  if (cur > peak_bytes_) peak_bytes_ = cur;
}

void LinearMortonForest::build_sample_index() {
  const size_t n = leaves_.size();
  if (n == 0) { sample_keys_.clear(); sample_offsets_.clear(); return; }
  const size_t s = std::min(kNumSamples, n);
  sample_keys_.resize(s); sample_offsets_.resize(s + 1);
  for (size_t i = 0; i < s; ++i) {
    const size_t idx = (i * n) / s;
    sample_keys_[i] = MortonSampleKey{leaves_[idx].root, leaves_[idx].key};
    sample_offsets_[i] = idx;
  }
  sample_offsets_[s] = n;
}

void LinearMortonForest::parallel_sort_leaves() {
  const size_t n = leaves_.size();
  if (n <= 1) return;
#ifdef _OPENMP
  const int nt = std::max(1, omp_get_max_threads());
  if (nt <= 1 || n < 16384) { std::sort(leaves_.begin(), leaves_.end(), leaf_less); return; }
  const size_t p = static_cast<size_t>(nt);
  std::vector<std::vector<Leaf>> buckets(p * p);
  std::vector<Leaf> splitters;
  std::atomic<bool> failed{false};
#pragma omp parallel num_threads(nt)
  {
    try {
      const size_t tid = static_cast<size_t>(omp_get_thread_num()), beg = (tid * n) / p, end = ((tid + 1) * n) / p;
      std::sort(leaves_.begin() + static_cast<ptrdiff_t>(beg), leaves_.begin() + static_cast<ptrdiff_t>(end), leaf_less);
#pragma omp barrier
#pragma omp single
      {
        std::vector<Leaf> smp; smp.reserve(p * p);
        for (size_t t = 0; t < p; ++t) for (size_t k = 0, tb = (t * n) / p, len = ((t + 1) * n) / p - tb; k < p && len > 0; ++k) smp.push_back(leaves_[tb + (k * len) / p]);
        std::sort(smp.begin(), smp.end(), leaf_less);
        for (size_t k = 1; k < p; ++k) splitters.push_back(smp[(k * smp.size()) / p]);
      }
      auto cur = leaves_.begin() + static_cast<ptrdiff_t>(beg), fin = leaves_.begin() + static_cast<ptrdiff_t>(end);
      for (size_t b = 0; b + 1 < p; ++b) { auto nxt = std::lower_bound(cur, fin, splitters[b], leaf_less); buckets[tid * p + b].assign(cur, nxt); cur = nxt; }
      buckets[tid * p + (p - 1)].assign(cur, fin);
    } catch (...) { failed.store(true, std::memory_order_relaxed); }
  }
  if (failed.load(std::memory_order_relaxed)) { std::sort(leaves_.begin(), leaves_.end(), leaf_less); return; }
  std::vector<size_t> offs(p + 1, 0);
  for (size_t b = 0; b < p; ++b) {
    size_t sz = 0; for (size_t t = 0; t < p; ++t) sz += buckets[t * p + b].size();
    offs[b + 1] = sz;
  }
  prefix_sum_exclusive(offs);
#pragma omp parallel num_threads(nt)
  {
    try {
      const size_t b = static_cast<size_t>(omp_get_thread_num());
      auto dst = leaves_.begin() + static_cast<ptrdiff_t>(offs[b]);
      for (size_t t = 0; t < p; ++t) { const auto& src = buckets[t * p + b]; std::copy(src.begin(), src.end(), dst); dst += static_cast<ptrdiff_t>(src.size()); }
      std::sort(leaves_.begin() + static_cast<ptrdiff_t>(offs[b]), leaves_.begin() + static_cast<ptrdiff_t>(offs[b + 1]), leaf_less);
    } catch (...) { failed.store(true, std::memory_order_relaxed); }
  }
  if (failed.load(std::memory_order_relaxed)) std::sort(leaves_.begin(), leaves_.end(), leaf_less);
#else
  std::sort(leaves_.begin(), leaves_.end(), leaf_less);
#endif
}

int64_t LinearMortonForest::find_leaf(uint32_t root, uint64_t key) const noexcept {
  if (leaves_.empty() || sample_keys_.empty()) return -1;
  auto sit = std::upper_bound(sample_keys_.begin(), sample_keys_.end(), MortonSampleKey{root, key},
      [](const MortonSampleKey& t, const MortonSampleKey& s) noexcept { return key_pair_less(t.root, t.key, s.root, s.key); });
  if (sit == sample_keys_.begin()) return -1;
  const size_t b = static_cast<size_t>(sit - sample_keys_.begin()) - 1, lo = sample_offsets_[b], hi = sample_offsets_[b + 1];
  const Leaf tgt{key, root, 0, 0, 0};
  auto lit = std::upper_bound(leaves_.begin() + static_cast<ptrdiff_t>(lo), leaves_.begin() + static_cast<ptrdiff_t>(hi), tgt, leaf_less);
  if (lit == leaves_.begin() + static_cast<ptrdiff_t>(lo)) return -1;
  const size_t pos = static_cast<size_t>(lit - leaves_.begin()) - 1;
  const Leaf& cand = leaves_[pos];
  if (cand.root != root) return -1;
  const uint32_t shift = 3u * static_cast<uint32_t>(kMaxOctreeLevel - leaf_level(cand.key));
  return ((leaf_morton(key) >> shift) == (leaf_morton(cand.key) >> shift)) ? static_cast<int64_t>(pos) : -1;
}

int64_t LinearMortonForest::find_leaf_at_point(const Vec3& p) const noexcept {
  if (p.x < dom_min_.x || p.x > dom_max_.x || p.y < dom_min_.y || p.y > dom_max_.y ||
      p.z < dom_min_.z || p.z > dom_max_.z || nx_ <= 0 || ny_ <= 0 || nz_ <= 0) return -1;
  const int64_t rx = find_axis_idx(x_coords_, p.x, nx_), ry = find_axis_idx(y_coords_, p.y, ny_), rz = find_axis_idx(z_coords_, p.z, nz_);
  auto loc = [](const std::vector<double>& c, int64_t r, double v) noexcept -> uint32_t {
    const double c0 = c[static_cast<size_t>(r)], c1 = c[static_cast<size_t>(r + 1)];
    const double frac = (c1 > c0) ? ((v - c0) / (c1 - c0)) : 0.0;
    return static_cast<uint32_t>(std::clamp<int64_t>(static_cast<int64_t>(std::floor(frac * static_cast<double>(kLatticePerRoot))), 0, kLatticePerRoot - 1));
  };
  return find_leaf(static_cast<uint32_t>((rz * ny_ + ry) * nx_ + rx),
                   make_leaf_key(morton3_encode(loc(x_coords_, rx, p.x), loc(y_coords_, ry, p.y), loc(z_coords_, rz, p.z)), 31u));
}

int64_t LinearMortonForest::find_leaf_for_voxel(int64_t vx, int64_t vy, int64_t vz) const noexcept {
  if (vx < 0 || vy < 0 || vz < 0 || vx >= nx_ * kLatticePerRoot || vy >= ny_ * kLatticePerRoot || vz >= nz_ * kLatticePerRoot) return -1;
  const uint32_t root = static_cast<uint32_t>(((vz >> kMaxOctreeLevel) * ny_ + (vy >> kMaxOctreeLevel)) * nx_ + (vx >> kMaxOctreeLevel));
  return find_leaf(root, make_leaf_key(morton3_encode(static_cast<uint32_t>(vx & kLatticeMask), static_cast<uint32_t>(vy & kLatticeMask), static_cast<uint32_t>(vz & kLatticeMask)), 31u));
}

void LinearMortonForest::leaf_lattice_origin(const Leaf& lf, int64_t& ox, int64_t& oy, int64_t& oz, int64_t& sz) const noexcept {
  const int64_t rx = static_cast<int64_t>(lf.root % static_cast<uint32_t>(nx_)), rem = static_cast<int64_t>(lf.root / static_cast<uint32_t>(nx_));
  uint32_t ix = 0, iy = 0, iz = 0; morton3_decode(leaf_morton(lf.key), ix, iy, iz);
  sz = int64_t{1} << (kMaxOctreeLevel - leaf_level(lf.key));
  ox = (rx << kMaxOctreeLevel) + static_cast<int64_t>(ix);
  oy = ((rem % ny_) << kMaxOctreeLevel) + static_cast<int64_t>(iy);
  oz = ((rem / ny_) << kMaxOctreeLevel) + static_cast<int64_t>(iz);
}

Vec3 LinearMortonForest::leaf_center(const Leaf& lf) const noexcept {
  int64_t ox = 0, oy = 0, oz = 0, sz = 0; leaf_lattice_origin(lf, ox, oy, oz, sz);
  const int64_t h = sz >> 1;
  if (h > 0) return Vec3(lattice_to_coord(x_coords_, nx_, ox + h), lattice_to_coord(y_coords_, ny_, oy + h), lattice_to_coord(z_coords_, nz_, oz + h));
  return Vec3(0.5 * (lattice_to_coord(x_coords_, nx_, ox) + lattice_to_coord(x_coords_, nx_, ox + 1)),
              0.5 * (lattice_to_coord(y_coords_, ny_, oy) + lattice_to_coord(y_coords_, ny_, oy + 1)),
              0.5 * (lattice_to_coord(z_coords_, nz_, oz) + lattice_to_coord(z_coords_, nz_, oz + 1)));
}

Vec3 LinearMortonForest::leaf_size(const Leaf& lf) const noexcept {
  int64_t ox = 0, oy = 0, oz = 0, sz = 0; leaf_lattice_origin(lf, ox, oy, oz, sz);
  return Vec3(lattice_to_coord(x_coords_, nx_, ox + sz) - lattice_to_coord(x_coords_, nx_, ox),
              lattice_to_coord(y_coords_, ny_, oy + sz) - lattice_to_coord(y_coords_, ny_, oy),
              lattice_to_coord(z_coords_, nz_, oz + sz) - lattice_to_coord(z_coords_, nz_, oz));
}

int32_t LinearMortonForest::subdivide_marked(const uint8_t* split_mask) {
  return subdivide_impl(leaves_, [split_mask](int64_t i) noexcept { return split_mask[i] != 0; }, leaves_.size(), [this](size_t s) { update_peak_bytes(s); });
}

int32_t LinearMortonForest::subdivide_marked_atomic(const std::atomic<uint8_t>* split_mask) {
  return subdivide_impl(leaves_, [split_mask](int64_t i) noexcept { return split_mask[i].load(std::memory_order_relaxed) != 0; }, leaves_.size(), [this](size_t s) { update_peak_bytes(s); });
}

bool LinearMortonForest::verify_2to1_balance() const {
  std::atomic<bool> ok{true};
  const int32_t rc = omp_for_range(static_cast<int64_t>(leaves_.size()), true, [&](int64_t i, int32_t&) {
    if (!ok.load(std::memory_order_relaxed)) return;
    const Leaf& lf = leaves_[static_cast<size_t>(i)];
    const int32_t lev_a = static_cast<int32_t>(leaf_level(lf.key));
    if (lev_a <= 1) return;
    int64_t ox = 0, oy = 0, oz = 0, sz = 0; leaf_lattice_origin(lf, ox, oy, oz, sz);
    visit_26_neighbors(ox, oy, oz, sz, [&](int64_t vx, int64_t vy, int64_t vz) {
      if (!ok.load(std::memory_order_relaxed)) return;
      const int64_t nb = find_leaf_for_voxel(vx, vy, vz);
      if (nb >= 0 && lev_a - static_cast<int32_t>(leaf_level(leaves_[static_cast<size_t>(nb)].key)) > 1) ok.store(false, std::memory_order_relaxed);
    });
  });
  return (rc == 0) && ok.load(std::memory_order_relaxed);
}

void LinearMortonForest::find_corner_owner(
    int64_t ci, int64_t cj, int64_t ck,
    int64_t& out_best_idx, uint8_t& out_best_corner, bool& out_touches_active) const noexcept {
  uint32_t best_root = std::numeric_limits<uint32_t>::max();
  uint64_t best_key = std::numeric_limits<uint64_t>::max();
  out_best_idx = -1; out_best_corner = 0; out_touches_active = false;
  for (int sx = -1; sx <= 0; ++sx) for (int sy = -1; sy <= 0; ++sy) for (int sz = -1; sz <= 0; ++sz) {
    const int64_t nb = find_leaf_for_voxel(ci + sx, cj + sy, ck + sz);
    if (nb < 0) continue;
    const Leaf& m = leaves_[static_cast<size_t>(nb)];
    if (m.flags & kLeafFlagActive) out_touches_active = true;
    int64_t mx = 0, my = 0, mz = 0, msz = 0;
    leaf_lattice_origin(m, mx, my, mz, msz);
    const int64_t dx = ci - mx, dy = cj - my, dz = ck - mz;
    if ((dx != 0 && dx != msz) || (dy != 0 && dy != msz) || (dz != 0 && dz != msz)) continue;
    if (out_best_idx < 0 || key_pair_less(m.root, m.key, best_root, best_key)) {
      best_root = m.root; best_key = m.key; out_best_idx = nb;
      out_best_corner = static_cast<uint8_t>(((dz != 0) ? 4u : 0u) | ((dy != 0) ? 2u : 0u) | ((dx != 0) ? 1u : 0u));
    }
  }
}

int32_t LinearMortonForest::lookup_corner_vertex(int64_t ci, int64_t cj, int64_t ck) const noexcept {
  int64_t best_idx = -1; uint8_t best_c = 0; bool touches_active = false;
  find_corner_owner(ci, cj, ck, best_idx, best_c, touches_active);
  if (best_idx < 0) return -1;
  const uint8_t low = static_cast<uint8_t>((1u << best_c) - 1u);
  return leaf_vertex_offset_[static_cast<size_t>(best_idx)] +
         static_cast<int32_t>(popcount8(static_cast<uint8_t>(leaves_[static_cast<size_t>(best_idx)].owned & low)));
}

int32_t LinearMortonForest::assign_vertices_and_extract_faces(int64_t n_cells) {
  (void)n_cells;
  const int64_t n_leaves = static_cast<int64_t>(leaves_.size());
  if (omp_for_range(n_leaves, true, [&](int64_t i, int32_t&) {
        Leaf& lf = leaves_[static_cast<size_t>(i)];
        int64_t ox = 0, oy = 0, oz = 0, sz = 0;
        leaf_lattice_origin(lf, ox, oy, oz, sz);
        uint8_t mask = 0;
        for (uint8_t c = 0; c < 8u; ++c) {
          int64_t best_idx = -1; uint8_t best_c = 0; bool touches_active = false;
          find_corner_owner(ox + ((c & 1u) ? sz : 0), oy + ((c & 2u) ? sz : 0), oz + ((c & 4u) ? sz : 0), best_idx, best_c, touches_active);
          if (touches_active && best_idx == i) mask |= static_cast<uint8_t>(1u << c);
        }
        lf.owned = mask;
      }) != 0) return -2;

  leaf_vertex_offset_.resize(static_cast<size_t>(n_leaves));
  constexpr int64_t kBlock = 4096;
  const int64_t nb = (n_leaves + kBlock - 1) / kBlock;
  std::vector<int32_t> v_offs(static_cast<size_t>(nb) + 1, 0);
  if (omp_for_range(nb, false, [&](int64_t b, int32_t&) {
        int32_t sum = 0;
        for (int64_t i = b * kBlock, end = std::min(n_leaves, (b + 1) * kBlock); i < end; ++i)
          sum += static_cast<int32_t>(popcount8(leaves_[static_cast<size_t>(i)].owned));
        v_offs[static_cast<size_t>(b) + 1] = sum;
      }) != 0) return -2;
  prefix_sum_exclusive(v_offs);
  vertex_coords_.resize(static_cast<size_t>(v_offs[static_cast<size_t>(nb)]));
  update_peak_bytes();

  if (omp_for_range(nb, false, [&](int64_t b, int32_t&) {
        int32_t vc = v_offs[static_cast<size_t>(b)];
        for (int64_t i = b * kBlock, end = std::min(n_leaves, (b + 1) * kBlock); i < end; ++i) {
          leaf_vertex_offset_[static_cast<size_t>(i)] = vc;
          const Leaf& lf = leaves_[static_cast<size_t>(i)];
          if (lf.owned == 0) continue;
          int64_t ox = 0, oy = 0, oz = 0, sz = 0;
          leaf_lattice_origin(lf, ox, oy, oz, sz);
          for (uint8_t c = 0; c < 8u; ++c) if (lf.owned & static_cast<uint8_t>(1u << c)) {
            vertex_coords_[static_cast<size_t>(vc++)] = Vec3(
                lattice_to_coord(x_coords_, nx_, ox + ((c & 1u) ? sz : 0)) + origin_shift_.x,
                lattice_to_coord(y_coords_, ny_, oy + ((c & 2u) ? sz : 0)) + origin_shift_.y,
                lattice_to_coord(z_coords_, nz_, oz + ((c & 4u) ? sz : 0)) + origin_shift_.z);
          }
        }
      }) != 0) return -2;

  std::vector<int64_t> reg_offs(static_cast<size_t>(nb) + 1, 0), gen_offs(static_cast<size_t>(nb) + 1, 0);
  auto visit = [&](int64_t i, auto&& emit_reg, auto&& emit_gen) {
    const Leaf& lf = leaves_[static_cast<size_t>(i)];
    const bool a_act = (lf.flags & kLeafFlagActive) != 0;
    const int32_t cid_a = leaf_cell_id_[static_cast<size_t>(i)];
    const uint8_t lev_a = leaf_level(lf.key);
    int64_t ox = 0, oy = 0, oz = 0, sz = 0;
    leaf_lattice_origin(lf, ox, oy, oz, sz);
    const int64_t half = sz >> 1;
    for (int dir = 0; dir < 6; ++dir) {
      const int64_t nbi = find_leaf_for_voxel(
          (kDirDelta[dir][0] < 0) ? (ox - 1) : (kDirDelta[dir][0] > 0 ? (ox + sz) : (ox + half)),
          (kDirDelta[dir][1] < 0) ? (oy - 1) : (kDirDelta[dir][1] > 0 ? (oy + sz) : (oy + half)),
          (kDirDelta[dir][2] < 0) ? (oz - 1) : (kDirDelta[dir][2] > 0 ? (oz + sz) : (oz + half)));
      if (nbi < 0) { if (a_act) emit_gen(i, dir, ox, oy, oz, sz, cid_a, -1, kFarfieldPatch[dir], uint16_t{2}); continue; }
      const Leaf& lnb = leaves_[static_cast<size_t>(nbi)];
      const bool b_act = (lnb.flags & kLeafFlagActive) != 0;
      const uint8_t lev_b = leaf_level(lnb.key);
      const int32_t cid_b = leaf_cell_id_[static_cast<size_t>(nbi)];
      if (a_act && b_act) {
        if (lev_a == lev_b && (dir == 1 || dir == 3 || dir == 5)) {
          emit_reg(RegularFace{std::min(cid_a, cid_b), std::max(cid_a, cid_b),
                               make_regular_face_meta(static_cast<uint8_t>(dir), lev_a, cid_a > cid_b ? 1u : 0u)});
        } else if (lev_a > lev_b) emit_gen(i, dir, ox, oy, oz, sz, cid_a, cid_b, uint16_t{0}, uint16_t{1});
      } else if (a_act && !b_act && lev_a >= lev_b) {
        emit_gen(i, dir, ox, oy, oz, sz, cid_a, -1, uint16_t{1}, uint16_t{4});
      } else if (!a_act && b_act && lev_a > lev_b) {
        emit_gen(i, dir, ox, oy, oz, sz, cid_b, -1, uint16_t{1}, uint16_t{4});
      }
    }
  };

  if (omp_for_range(nb, true, [&](int64_t b, int32_t&) {
        int64_t rc = 0, gc = 0;
        for (int64_t i = b * kBlock, end = std::min(n_leaves, (b + 1) * kBlock); i < end; ++i)
          visit(i, [&](const RegularFace&) noexcept { ++rc; },
                [&](int64_t, int, int64_t, int64_t, int64_t, int64_t, int32_t, int32_t, uint16_t, uint16_t) noexcept { ++gc; });
        reg_offs[static_cast<size_t>(b) + 1] = rc; gen_offs[static_cast<size_t>(b) + 1] = gc;
      }) != 0) return -2;

  prefix_sum_exclusive(reg_offs); prefix_sum_exclusive(gen_offs);
  regular_faces_.resize(static_cast<size_t>(reg_offs[static_cast<size_t>(nb)]));
  general_faces_.resize(static_cast<size_t>(gen_offs[static_cast<size_t>(nb)]));
  update_peak_bytes();
  return omp_for_range(nb, true, [&](int64_t b, int32_t&) {
    int64_t rp = reg_offs[static_cast<size_t>(b)], gp = gen_offs[static_cast<size_t>(b)];
    auto w_reg = [&](const RegularFace& rf) noexcept { regular_faces_[static_cast<size_t>(rp++)] = rf; };
    auto w_gen = [&](int64_t li, int dir, int64_t ox, int64_t oy, int64_t oz, int64_t sz,
                     int32_t o, int32_t nbi, uint16_t pid, uint16_t fl) noexcept {
      std::array<int32_t, 4> nodes{};
      for (int k = 0; k < 4; ++k) {
        const int c = kFaceCorners[dir][k];
        nodes[static_cast<size_t>(k)] = lookup_corner_vertex(ox + ((c & 1) ? sz : 0), oy + ((c & 2) ? sz : 0), oz + ((c & 4) ? sz : 0));
      }
      int32_t fo = o, fnb = nbi;
      if (nbi >= 0 && o > nbi) { fo = nbi; fnb = o; std::swap(nodes[1], nodes[3]); }
      else if (nbi < 0 && o != leaf_cell_id_[static_cast<size_t>(li)]) { std::swap(nodes[1], nodes[3]); }
      general_faces_[static_cast<size_t>(gp++)] = GeneralFace{nodes, fo, fnb, pid, fl};
    };
    for (int64_t i = b * kBlock, end = std::min(n_leaves, (b + 1) * kBlock); i < end; ++i) visit(i, w_reg, w_gen);
  });
}

int32_t LinearMortonForest::build(
    const double* surf_points, int64_t n_points, const int32_t* surf_faces, int64_t n_faces,
    double h0, int32_t max_depth, const double* farfield_bbox, MortonBuildStats* out_stats) {
  const auto t0 = std::chrono::steady_clock::now();
  if (!surf_points || !surf_faces || n_points < 3 || n_faces <= 0 || !std::isfinite(h0) || h0 <= 0.0 || max_depth < 0) return -1;

  Vec3 raw_min(1e30, 1e30, 1e30), raw_max(-1e30, -1e30, -1e30);
  for (int64_t i = 0; i < n_points; ++i) {
    const Vec3 p(surf_points[3 * i], surf_points[3 * i + 1], surf_points[3 * i + 2]);
    raw_min.x = std::min(raw_min.x, p.x); raw_min.y = std::min(raw_min.y, p.y); raw_min.z = std::min(raw_min.z, p.z);
    raw_max.x = std::max(raw_max.x, p.x); raw_max.y = std::max(raw_max.y, p.y); raw_max.z = std::max(raw_max.z, p.z);
  }
  origin_shift_ = (raw_min + raw_max) * 0.5;
  std::vector<double> shifted(static_cast<size_t>(n_points) * 3);
  for (int64_t i = 0; i < n_points; ++i) {
    shifted[static_cast<size_t>(3 * i + 0)] = surf_points[3 * i + 0] - origin_shift_.x;
    shifted[static_cast<size_t>(3 * i + 1)] = surf_points[3 * i + 1] - origin_shift_.y;
    shifted[static_cast<size_t>(3 * i + 2)] = surf_points[3 * i + 2] - origin_shift_.z;
  }
  const Vec3 s_min = raw_min - origin_shift_, s_max = raw_max - origin_shift_;
  MeshSDF sdf(shifted.data(), n_points, surf_faces, n_faces);
  sdf_bytes_ = sdf.memory_bytes(); std::vector<double>().swap(shifted);

  bool external_domain = true;
  if (farfield_bbox) {
    dom_min_ = Vec3(farfield_bbox[0], farfield_bbox[1], farfield_bbox[2]) - origin_shift_;
    dom_max_ = Vec3(farfield_bbox[3], farfield_bbox[4], farfield_bbox[5]) - origin_shift_;
    if (dom_min_.x >= dom_max_.x || dom_min_.y >= dom_max_.y || dom_min_.z >= dom_max_.z) return -1;
    external_domain = (dom_min_.x < s_min.x - 0.1 * h0 || dom_max_.x > s_max.x + 0.1 * h0 ||
                       dom_min_.y < s_min.y - 0.1 * h0 || dom_max_.y > s_max.y + 0.1 * h0 ||
                       dom_min_.z < s_min.z - 0.1 * h0 || dom_max_.z > s_max.z + 0.1 * h0);
  } else {
    const Vec3 sp = s_max - s_min; const double lr = std::max({sp.x, sp.y, sp.z, 1e-12});
    dom_min_ = Vec3(s_min.x - 5.0 * lr, s_min.y - 5.0 * lr, s_min.z - 5.0 * lr);
    dom_max_ = Vec3(s_max.x + 10.0 * lr, s_max.y + 5.0 * lr, s_max.z + 5.0 * lr);
  }

  const Vec3 ds = dom_max_ - dom_min_;
  const double dom_max_span = std::max({ds.x, ds.y, ds.z});
  effective_max_depth_ = std::clamp<int32_t>(max_depth, 0, kMaxOctreeLevel);
  double h_root = h0;
  auto init_axes = [&](double hr) {
    if (dom_max_span / hr > 64.0) {
      x_coords_ = detail::build_graded_axis(dom_min_.x, s_min.x, s_max.x, dom_max_.x, hr);
      y_coords_ = detail::build_graded_axis(dom_min_.y, s_min.y, s_max.y, dom_max_.y, hr);
      z_coords_ = detail::build_graded_axis(dom_min_.z, s_min.z, s_max.z, dom_max_.z, hr);
    } else {
      auto uni = [](double d0, double d1, double h) {
        const int64_t nu = std::max<int64_t>(1, static_cast<int64_t>(std::llround((d1 - d0) / h)));
        std::vector<double> c(static_cast<size_t>(nu) + 1);
        for (int64_t i = 0; i <= nu; ++i) c[static_cast<size_t>(i)] = d0 + ((d1 - d0) * i) / nu;
        return c;
      };
      x_coords_ = uni(dom_min_.x, dom_max_.x, hr); y_coords_ = uni(dom_min_.y, dom_max_.y, hr); z_coords_ = uni(dom_min_.z, dom_max_.z, hr);
    }
    nx_ = static_cast<int64_t>(x_coords_.size()) - 1; ny_ = static_cast<int64_t>(y_coords_.size()) - 1; nz_ = static_cast<int64_t>(z_coords_.size()) - 1;
  };
  init_axes(h_root);
  while ((nx_ * ny_ * nz_) > 4000000 && effective_max_depth_ < kMaxOctreeLevel) { h_root *= 2.0; ++effective_max_depth_; init_axes(h_root); }
  if (nx_ <= 0 || ny_ <= 0 || nz_ <= 0) return -1;
  n_root_ = static_cast<uint32_t>(nx_ * ny_ * nz_); peak_bytes_ = 0; leaves_.resize(n_root_);
  if (omp_for_range(static_cast<int64_t>(n_root_), false, [&](int64_t r, int32_t&) {
        leaves_[static_cast<size_t>(r)] = Leaf{make_leaf_key(0u, 0u), static_cast<uint32_t>(r), 0u, kLeafFlagActive, 0u};
      }) != 0) return -2;
  update_peak_bytes();

  for (int32_t pass = 0; pass < effective_max_depth_; ++pass) {
    const int64_t n = static_cast<int64_t>(leaves_.size());
    std::vector<uint8_t> split_mask(static_cast<size_t>(n), 0); update_peak_bytes(split_mask.capacity());
    if (omp_for_range(n, true, [&](int64_t i, int32_t& prev_facet) {
          const Leaf& lf = leaves_[static_cast<size_t>(i)];
          if (static_cast<int32_t>(leaf_level(lf.key)) == pass &&
              std::abs(sdf.evaluate_point(leaf_center(lf), prev_facet)) <= 0.85 * leaf_size(lf).norm()) split_mask[static_cast<size_t>(i)] = 1u;
        }) != 0) return -2;
    if (subdivide_marked(split_mask.data()) != 0) return -2;
  }

  for (int iter = 0; iter < 32; ++iter) {
    build_sample_index();
    const int64_t n = static_cast<int64_t>(leaves_.size());
    auto split_mask = std::make_unique<std::atomic<uint8_t>[]>(static_cast<size_t>(n));
    for (int64_t i = 0; i < n; ++i) split_mask[static_cast<size_t>(i)].store(0u, std::memory_order_relaxed);
    std::atomic<bool> any_split{false}; update_peak_bytes(static_cast<size_t>(n));
    if (omp_for_range(n, true, [&](int64_t i, int32_t&) {
          const Leaf& lf = leaves_[static_cast<size_t>(i)];
          const int32_t lev_a = static_cast<int32_t>(leaf_level(lf.key));
          if (lev_a <= 1) return;
          int64_t ox = 0, oy = 0, oz = 0, sz = 0; leaf_lattice_origin(lf, ox, oy, oz, sz);
          visit_26_neighbors(ox, oy, oz, sz, [&](int64_t vx, int64_t vy, int64_t vz) {
            const int64_t nb = find_leaf_for_voxel(vx, vy, vz);
            if (nb >= 0 && lev_a - static_cast<int32_t>(leaf_level(leaves_[static_cast<size_t>(nb)].key)) > 1) {
              split_mask[static_cast<size_t>(nb)].store(1u, std::memory_order_relaxed); any_split.store(true, std::memory_order_relaxed);
            }
          });
        }) != 0) return -2;
    if (!any_split.load(std::memory_order_relaxed)) break;
    if (subdivide_marked_atomic(split_mask.get()) != 0) return -2;
  }

  build_sample_index();
  const bool balanced_ok = verify_2to1_balance();
  const int64_t n_leaves = static_cast<int64_t>(leaves_.size());
  if (omp_for_range(n_leaves, true, [&](int64_t i, int32_t& prev_facet) {
        Leaf& lf = leaves_[static_cast<size_t>(i)];
        const double diag = leaf_size(lf).norm(), phi = sdf.evaluate_point(leaf_center(lf), prev_facet);
        const bool active = external_domain ? (phi >= -0.5 * diag) : (phi <= 0.5 * diag);
        lf.flags = (active ? kLeafFlagActive : 0u) | (std::abs(phi) <= 0.6 * diag ? kLeafFlagCut : 0u);
      }) != 0) return -2;

  leaf_cell_id_.resize(static_cast<size_t>(n_leaves));
  int32_t n_cells = 0;
  for (int64_t i = 0; i < n_leaves; ++i) leaf_cell_id_[static_cast<size_t>(i)] = (leaves_[static_cast<size_t>(i)].flags & kLeafFlagActive) ? n_cells++ : -1;
  update_peak_bytes();
  if (assign_vertices_and_extract_faces(n_cells) != 0) return -2;
  const double elapsed = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
  update_peak_bytes();
  if (out_stats) {
    *out_stats = MortonBuildStats{n_leaves, static_cast<int64_t>(n_cells), static_cast<int64_t>(regular_faces_.size()),
                                  static_cast<int64_t>(general_faces_.size()), static_cast<int64_t>(vertex_coords_.size()),
                                  peak_bytes_, elapsed, balanced_ok ? 1 : 0};
  }
  return 0;
}

}  // namespace cfd::octree
