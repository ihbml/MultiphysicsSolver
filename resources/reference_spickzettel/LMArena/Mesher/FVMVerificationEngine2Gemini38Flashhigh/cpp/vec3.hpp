#pragma once
#include <cmath>
#include <complex>
#include <iostream>
#include <type_traits>

template <typename T = double>
struct Vec3 {
    T x{0}, y{0}, z{0};

    constexpr Vec3() = default;
    constexpr Vec3(T x_, T y_, T z_) : x(x_), y(y_), z(z_) {}

    template <typename U>
    explicit Vec3(const Vec3<U>& other)
        : x(static_cast<T>(other.x)), y(static_cast<T>(other.y)), z(static_cast<T>(other.z)) {}

    Vec3 operator+(const Vec3& o) const { return {x + o.x, y + o.y, z + o.z}; }
    Vec3 operator-(const Vec3& o) const { return {x - o.x, y - o.y, z - o.z}; }
    Vec3 operator*(T s) const { return {x * s, y * s, z * s}; }
    Vec3 operator/(T s) const { return {x / s, y / s, z / s}; }

    Vec3& operator+=(const Vec3& o) { x += o.x; y += o.y; z += o.z; return *this; }
    Vec3& operator-=(const Vec3& o) { x -= o.x; y -= o.y; z -= o.z; return *this; }
    Vec3& operator*=(T s) { x *= s; y *= s; z *= s; return *this; }
    Vec3& operator/=(T s) { x /= s; y /= s; z /= s; return *this; }

    T dot(const Vec3& o) const { return x * o.x + y * o.y + z * o.z; }

    Vec3 cross(const Vec3& o) const {
        return {
            y * o.z - z * o.y,
            z * o.x - x * o.z,
            x * o.y - y * o.x
        };
    }

    T normSq() const { return dot(*this); }

    auto norm() const {
        if constexpr (std::is_same_v<T, std::complex<double>>) {
            return std::sqrt(dot(*this));
        } else {
            return std::sqrt(normSq());
        }
    }

    Vec3 normalized() const {
        auto n = norm();
        if (n == T(0)) return *this;
        return *this / n;
    }
};

template <typename T>
Vec3<T> operator*(T s, const Vec3<T>& v) {
    return v * s;
}

using Vec3d = Vec3<double>;
using Vec3c = Vec3<std::complex<double>>;
