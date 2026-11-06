// DiFVM Mesher — core types, SoA field storage, parallel primitives.
// C++20, no external dependencies. All hot data is 64-byte aligned SoA.
#pragma once

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

namespace difvm {

using idx = std::int32_t;    // cell / face / vertex index  (max 2.1e9)
using gidx = std::int64_t;   // CSR offsets (number of faces * verts can exceed 2^31)

// ---------------------------------------------------------------------------
// Errors
// ---------------------------------------------------------------------------
class MeshError : public std::runtime_error {
 public:
  explicit MeshError(const std::string& what) : std::runtime_error(what) {}
};

[[noreturn]] inline void fail(const std::string& msg) { throw MeshError(msg); }

// ---------------------------------------------------------------------------
// Vector3d (plain aggregate -> can live in registers, trivially copyable)
// ---------------------------------------------------------------------------
struct Vec3 {
  double x = 0.0, y = 0.0, z = 0.0;

  constexpr Vec3() = default;
  constexpr Vec3(double x_, double y_, double z_) : x(x_), y(y_), z(z_) {}

  constexpr Vec3 operator+(const Vec3& o) const { return {x + o.x, y + o.y, z + o.z}; }
  constexpr Vec3 operator-(const Vec3& o) const { return {x - o.x, y - o.y, z - o.z}; }
  constexpr Vec3 operator-() const { return {-x, -y, -z}; }
  constexpr Vec3 operator*(double s) const { return {x * s, y * s, z * s}; }
  constexpr Vec3 operator/(double s) const { return {x / s, y / s, z / s}; }
  Vec3& operator+=(const Vec3& o) { x += o.x; y += o.y; z += o.z; return *this; }
  Vec3& operator-=(const Vec3& o) { x -= o.x; y -= o.y; z -= o.z; return *this; }
  Vec3& operator*=(double s) { x *= s; y *= s; z *= s; return *this; }
};

constexpr inline double dot(const Vec3& a, const Vec3& b) {
  return a.x * b.x + a.y * b.y + a.z * b.z;
}
constexpr inline Vec3 cross(const Vec3& a, const Vec3& b) {
  return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
}
inline double norm2(const Vec3& a) { return dot(a, a); }
inline double norm(const Vec3& a) { return std::sqrt(dot(a, a)); }
inline Vec3 normalized(const Vec3& a) {
  const double n = norm(a);
  return (n > 0.0) ? a / n : Vec3{0.0, 0.0, 0.0};
}
inline Vec3 vmin(const Vec3& a, const Vec3& b) {
  return {std::min(a.x, b.x), std::min(a.y, b.y), std::min(a.z, b.z)};
}
inline Vec3 vmax(const Vec3& a, const Vec3& b) {
  return {std::max(a.x, b.x), std::max(a.y, b.y), std::max(a.z, b.z)};
}
inline double absmax_comp(const Vec3& a) {
  return std::max(std::fabs(a.x), std::max(std::fabs(a.y), std::fabs(a.z)));
}

// ---------------------------------------------------------------------------
// 64-byte aligned allocator -> SoA buffers start on cache lines
// ---------------------------------------------------------------------------
template <class T>
struct AlignedAllocator {
  static constexpr std::size_t kAlign = 64;
  using value_type = T;
  AlignedAllocator() noexcept = default;
  template <class U>
  AlignedAllocator(const AlignedAllocator<U>&) noexcept {}
  T* allocate(std::size_t n) {
    if (n == 0) return nullptr;
    void* p = std::aligned_alloc(kAlign, ((n * sizeof(T) + kAlign - 1) / kAlign) * kAlign);
    if (!p) throw std::bad_alloc();
    return static_cast<T*>(p);
  }
  void deallocate(T* p, std::size_t) noexcept { std::free(p); }
  template <class U>
  bool operator==(const AlignedAllocator<U>&) const noexcept { return true; }
  template <class U>
  bool operator!=(const AlignedAllocator<U>&) const noexcept { return false; }
};

template <class T>
using AlignedVector = std::vector<T, AlignedAllocator<T>>;

// Structure-of-Arrays triple of coordinates (true SoA: three contiguous lanes)
struct Vec3SoA {
  AlignedVector<double> x, y, z;

  void resize(std::size_t n) { x.resize(n); y.resize(n); z.resize(n); }
  std::size_t size() const { return x.size(); }
  void reserve(std::size_t n) { x.reserve(n); y.reserve(n); z.reserve(n); }
  void clear() { x.clear(); y.clear(); z.clear(); }
  void shrink_to_fit() { x.shrink_to_fit(); y.shrink_to_fit(); z.shrink_to_fit(); }

  inline Vec3 get(std::size_t i) const { return {x[i], y[i], z[i]}; }
  inline void set(std::size_t i, const Vec3& v) { x[i] = v.x; y[i] = v.y; z[i] = v.z; }
  inline void push_back(const Vec3& v) { x.push_back(v.x); y.push_back(v.y); z.push_back(v.z); }
  // Permute in place given dst->src map (dst[i] = src[perm[i]])
  void permute(const std::vector<idx>& perm);
  double* data() { return x.data(); }
};

// ---------------------------------------------------------------------------
// Parallel for: deterministic static chunking + atomic work stealing tail.
// Chunking keeps false sharing low and makes results bit-reproducible for
// reductions that use per-thread accumulators.
// ---------------------------------------------------------------------------
inline int hardware_threads() {
  const unsigned hc = std::thread::hardware_concurrency();
  return hc == 0 ? 1 : static_cast<int>(hc);
}

inline int& global_thread_count() {
  static int n = hardware_threads();
  return n;
}
inline void set_num_threads(int n) { global_thread_count() = std::max(1, n); }
inline int num_threads() { return global_thread_count(); }

template <class Fn>
void parallel_for(gidx n, Fn&& fn, int nthreads = 0) {
  if (n <= 0) return;
  if (nthreads <= 0) nthreads = num_threads();
  nthreads = static_cast<int>(std::min<gidx>(nthreads, n));
  if (nthreads == 1) {
    for (gidx i = 0; i < n; ++i) fn(i);
    return;
  }
  std::atomic<gidx> next{0};
  const gidx chunk = std::max<gidx>(1, n / (static_cast<gidx>(nthreads) * 32));
  auto worker = [&]() {
    for (;;) {
      const gidx begin = next.fetch_add(chunk, std::memory_order_relaxed);
      if (begin >= n) return;
      const gidx end = std::min(n, begin + chunk);
      for (gidx i = begin; i < end; ++i) fn(i);
    }
  };
  std::vector<std::thread> pool;
  pool.reserve(static_cast<std::size_t>(nthreads - 1));
  for (int t = 0; t < nthreads - 1; ++t) pool.emplace_back(worker);
  worker();
  for (auto& th : pool) th.join();
}

// ---------------------------------------------------------------------------
// Small utilities
// ---------------------------------------------------------------------------
inline double clamp01(double v) { return v < 0.0 ? 0.0 : (v > 1.0 ? 1.0 : v); }
inline double clampd(double v, double lo, double hi) { return v < lo ? lo : (v > hi ? hi : v); }

template <class T>
inline void append_bytes(std::string& out, const T& v) {
  out.append(reinterpret_cast<const char*>(&v), sizeof(T));
}

// Simple wall clock timer (monotonic).
class Timer {
 public:
  Timer() : t0_(std::chrono::steady_clock::now()) {}
  double seconds() const {
    return std::chrono::duration<double>(std::chrono::steady_clock::now() - t0_).count();
  }
  void reset() { t0_ = std::chrono::steady_clock::now(); }

 private:
  std::chrono::steady_clock::time_point t0_;
};

// Minimal logging (stderr, with levels).
enum class LogLevel { kQuiet = 0, kInfo = 1, kVerbose = 2 };
inline LogLevel& log_level() {
  static LogLevel l = LogLevel::kInfo;
  return l;
}
inline void log_info(const std::string& s) {
  if (log_level() >= LogLevel::kInfo) std::fprintf(stderr, "%s\n", s.c_str());
}
inline void log_verbose(const std::string& s) {
  if (log_level() >= LogLevel::kVerbose) std::fprintf(stderr, "  %s\n", s.c_str());
}
inline void log_warn(const std::string& s) {
  if (log_level() >= LogLevel::kInfo) std::fprintf(stderr, "warning: %s\n", s.c_str());
}

}  // namespace difvm
