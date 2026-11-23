// =====================================================================================
//  mt::Types.hpp  --  Core scalar / vector / tensor types and small utilities.
//
//  Part of "Multiphysics Suite, Cluster A" : Industrial RANS/LES turbulence,
//  All-y+ wall treatment, Multi-Region CHT, Anisotropic porous media, fvDOM radiation.
//
//  Language standard : C++20   (g++ -std=c++20 -O2 verified)
//  Author            : Lead Turbomachinery & Thermal-Fluid Solver Architect
// =====================================================================================
#pragma once

#include <array>
#include <cmath>
#include <cstdint>
#include <limits>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

namespace mt {

// -------------------------------------------------------------------------------------
//  Mathematical constants (SI)
// -------------------------------------------------------------------------------------
inline constexpr double kPi      = 3.14159265358979323846;
inline constexpr double kTwoPi   = 6.28318530717958647692;
inline constexpr double kFourPi  = 12.56637061435917295385;
inline constexpr double kSmall   = 1.0e-300;   // denominator guards
inline constexpr double kTiny    = 1.0e-20;   // "numerically zero"
inline constexpr double kEps     = 1.0e-14;   // general epsilon

// Universal constants
inline constexpr double kStefanBoltzmann = 5.670374419e-8;  // W/m^2/K^4
inline constexpr double kPlanckH         = 6.62607015e-34; // J s
inline constexpr double kBoltzmann       = 1.380649e-23;   // J/K

// -------------------------------------------------------------------------------------
//  Vec3 : small, branch-light 3-vector (value semantics)
// -------------------------------------------------------------------------------------
struct Vec3 {
    double x{0.0}, y{0.0}, z{0.0};

    Vec3() = default;
    constexpr Vec3(double a, double b, double c) : x(a), y(b), z(c) {}

    double  operator[](int i) const { return i == 0 ? x : (i == 1 ? y : z); }
    double& operator[](int i)       { return i == 0 ? x : (i == 1 ? y : z); }
};

inline constexpr Vec3 operator+(const Vec3& a, const Vec3& b) { return {a.x + b.x, a.y + b.y, a.z + b.z}; }
inline constexpr Vec3 operator-(const Vec3& a, const Vec3& b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
inline constexpr Vec3 operator-(const Vec3& a)               { return {-a.x, -a.y, -a.z}; }
inline constexpr Vec3 operator*(const Vec3& a, double s)      { return {a.x * s, a.y * s, a.z * s}; }
inline constexpr Vec3 operator*(double s, const Vec3& a)      { return {a.x * s, a.y * s, a.z * s}; }
inline constexpr Vec3 operator/(const Vec3& a, double s)      { return {a.x / s, a.y / s, a.z / s}; }
inline constexpr Vec3 operator*(const Vec3& a, const Vec3& b) { return {a.x * b.x, a.y * b.y, a.z * b.z}; }
inline Vec3& operator+=(Vec3& a, const Vec3& b) { a.x += b.x; a.y += b.y; a.z += b.z; return a; }
inline Vec3& operator-=(Vec3& a, const Vec3& b) { a.x -= b.x; a.y -= b.y; a.z -= b.z; return a; }
inline Vec3& operator*=(Vec3& a, double s)      { a.x *= s; a.y *= s; a.z *= s; return a; }

inline constexpr double dot(const Vec3& a, const Vec3& b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
inline constexpr Vec3   cross(const Vec3& a, const Vec3& b) {
    return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
}
inline constexpr double mag2(const Vec3& a) { return dot(a, a); }
inline double mag(const Vec3& a) { return std::sqrt(dot(a, a)); }
inline Vec3   unit(const Vec3& a) { const double m = mag(a); return m > kSmall ? a / m : Vec3{0, 0, 0}; }

// -------------------------------------------------------------------------------------
//  SymMat3 : symmetric 3x3 tensor (stresses, conductivity, gradients-of-velocity)
// -------------------------------------------------------------------------------------
struct SymMat3 {
    double xx{0.0}, yy{0.0}, zz{0.0}, xy{0.0}, yz{0.0}, zx{0.0};

    SymMat3() = default;
    SymMat3(double xx_, double yy_, double zz_, double xy_, double yz_, double zx_)
        : xx(xx_), yy(yy_), zz(zz_), xy(xy_), yz(yz_), zx(zx_) {}

    double operator()(int i, int j) const {
        if (i == j) return i == 0 ? xx : (i == 1 ? yy : zz);
        if (i == 0) return j == 1 ? xy : zx;
        if (i == 1) return j == 0 ? xy : yz;
        return j == 0 ? zx : yz;
    }
    double trace() const { return xx + yy + zz; }

    static SymMat3 identity() { return {1.0, 1.0, 1.0, 0.0, 0.0, 0.0}; }
    static SymMat3 zero()    { return {}; }

    SymMat3& operator+=(const SymMat3& o) {
        xx += o.xx; yy += o.yy; zz += o.zz; xy += o.xy; yz += o.yz; zx += o.zx; return *this;
    }
    SymMat3& operator-=(const SymMat3& o) {
        xx -= o.xx; yy -= o.yy; zz -= o.zz; xy -= o.xy; yz -= o.yz; zx -= o.zx; return *this;
    }
    SymMat3& operator*=(double s) {
        xx *= s; yy *= s; zz *= s; xy *= s; yz *= s; zx *= s; return *this;
    }
};
// (guard against accidental misuse of the deleted overload above)
struct Sym3Dummy {};

inline SymMat3 operator+(SymMat3 a, const SymMat3& b) { a += b; return a; }
inline SymMat3 operator-(SymMat3 a, const SymMat3& b) { a -= b; return a; }
inline SymMat3 operator*(SymMat3 a, double s)          { a *= s; return a; }

// -------------------------------------------------------------------------------------
//  Mat3 : full 3x3 (coordinate transforms, anisotropic Darcy tensors)
// -------------------------------------------------------------------------------------
struct Mat3 {
    std::array<std::array<double, 3>, 3> m{{{1, 0, 0}, {0, 1, 0}, {0, 0, 1}}};

    double  operator()(int i, int j) const { return m[static_cast<std::size_t>(i)][static_cast<std::size_t>(j)]; }
    double& operator()(int i, int j)       { return m[static_cast<std::size_t>(i)][static_cast<std::size_t>(j)]; }

    static Mat3 identity() { return Mat3{}; }
    static Mat3 zero()    { Mat3 r; r.m = {{{0, 0, 0}, {0, 0, 0}, {0, 0, 0}}}; return r; }
    static Mat3 diagonal(const Vec3& d) {
        Mat3 r; r.m = {{{d.x, 0, 0}, {0, d.y, 0}, {0, 0, d.z}}}; return r;
    }
    static Mat3 rotation(const Vec3& axis, double angleRad);

    Mat3 transpose() const;
    Mat3 operator*(const Mat3& o) const;
    Vec3 operator*(const Vec3& v) const;
    Mat3 operator*(double s) const;
    double det() const;
};

// -------------------------------------------------------------------------------------
//  Scalar helpers
// -------------------------------------------------------------------------------------
inline constexpr double clampd(double v, double lo, double hi) { return v < lo ? lo : (v > hi ? hi : v); }
inline constexpr double sqr(double v) { return v * v; }
inline constexpr int    signi(double v) { return v < 0.0 ? -1 : (v > 0.0 ? 1 : 0); }
inline double   safeDiv(double a, double b) { return (std::abs(b) < kTiny) ? 0.0 : a / b; }
inline double   smoothStep(double e0, double e1, double x) {
    const double t = clampd((x - e0) / (e1 - e0), 0.0, 1.0);
    return t * t * (3.0 - 2.0 * t);
}

// -------------------------------------------------------------------------------------
//  A dependency-free JSON writer (used by the verification harness to emit the
//  machine-readable report consumed by the interactive engineering workbench).
// -------------------------------------------------------------------------------------
class Json {
public:
    enum class Type { Object, Array, Raw };

    Json() = default;
    static Json object() { Json j; j.type_ = Type::Object; return j; }
    static Json array()  { Json j; j.type_ = Type::Array;  return j; }
    static Json raw(std::string s) { Json j; j.type_ = Type::Raw; j.raw_ = std::move(s); return j; }
    static Json number(double v);
    static Json integer(long long v);
    static Json boolean(bool v);
    static Json string(const std::string& v);

    Json& set(const std::string& key, const Json& v);
    Json& set(const std::string& key, double v)        { return set(key, number(v)); }
    Json& set(const std::string& key, long long v)     { return set(key, integer(v)); }
    Json& set(const std::string& key, int v)           { return set(key, integer(v)); }
    Json& set(const std::string& key, bool v)           { return set(key, boolean(v)); }
    Json& set(const std::string& key, const std::string& v) { return set(key, string(v)); }
    Json& set(const std::string& key, const char* v)    { return set(key, string(std::string(v))); }

    Json& push(const Json& v);
    Json& push(double v) { return push(number(v)); }
    Json& push(long long v) { return push(integer(v)); }
    Json& push(int v) { return push(integer(v)); }

    std::string dump(int indent = 0) const;

private:
    Type type_{Type::Object};
    std::string raw_;
    std::vector<std::pair<std::string, Json>> members_;
    std::vector<Json> elements_;
};

std::string fmt(double v, int prec = 6);

} // namespace mt
