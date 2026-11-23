// mdo_math.hpp
// Minimal, dependency-free dense/sparse linear algebra for MDO.
// - Dense column-major vectors/matrices (std::vector<double>)
// - Compressed Sparse Row (CSR) symmetric positive-definite solver via Conjugate Gradient
// - Dense Cholesky (for small normal-equation systems)
// - Finite-difference / complex-step utilities
#pragma once

#include <vector>
#include <cmath>
#include <stdexcept>
#include <iostream>
#include <numeric>
#include <algorithm>
#include <complex>

namespace mdo {

// ---------- basic vector helpers ----------

inline double dot(const std::vector<double>& a, const std::vector<double>& b) {
    double s = 0.0;
    for (size_t i = 0; i < a.size(); ++i) s += a[i] * b[i];
    return s;
}

inline void axpy(double alpha, const std::vector<double>& x, std::vector<double>& y) {
    for (size_t i = 0; i < x.size(); ++i) y[i] += alpha * x[i];
}

inline void scale(std::vector<double>& x, double alpha) {
    for (auto& v : x) v *= alpha;
}

inline std::vector<double> operator+(const std::vector<double>& a, const std::vector<double>& b) {
    std::vector<double> r(a.size());
    for (size_t i = 0; i < a.size(); ++i) r[i] = a[i] + b[i];
    return r;
}

inline std::vector<double> operator-(const std::vector<double>& a, const std::vector<double>& b) {
    std::vector<double> r(a.size());
    for (size_t i = 0; i < a.size(); ++i) r[i] = a[i] - b[i];
    return r;
}

inline std::vector<double> operator*(double alpha, const std::vector<double>& x) {
    std::vector<double> r(x.size());
    for (size_t i = 0; i < x.size(); ++i) r[i] = alpha * x[i];
    return r;
}

inline std::vector<double> hadamard(const std::vector<double>& a, const std::vector<double>& b) {
    std::vector<double> r(a.size());
    for (size_t i = 0; i < a.size(); ++i) r[i] = a[i] * b[i];
    return r;
}

inline double norm2(const std::vector<double>& x) { return std::sqrt(dot(x, x)); }

inline double max_abs(const std::vector<double>& x) {
    double m = 0.0;
    for (double v : x) m = std::max(m, std::fabs(v));
    return m;
}

// ---------- CSR sparse matrix (symmetric, for Helmholtz filter) ----------

struct CSR {
    std::vector<int> row_ptr;   // size n+1
    std::vector<int> col_idx;   // size nnz
    std::vector<double> val;    // size nnz
    int n = 0;

    void matvec(const std::vector<double>& x, std::vector<double>& y) const {
        std::fill(y.begin(), y.end(), 0.0);
        for (int i = 0; i < n; ++i) {
            double s = 0.0;
            for (int k = row_ptr[i]; k < row_ptr[i + 1]; ++k)
                s += val[k] * x[col_idx[k]];
            y[i] = s;
        }
    }
};

// Conjugate Gradient for SPD systems A x = b.
// Returns number of iterations; fills x. Stops when ||r|| < tol*||b|| or max_iter.
int cg_solve(const CSR& A, const std::vector<double>& b, std::vector<double>& x,
             double tol = 1e-12, int max_iter = 5000) {
    int n = A.n;
    x.assign(n, 0.0);
    std::vector<double> r = b;             // r = b - A*x (x=0)
    std::vector<double> p = r;
    std::vector<double> Ap(n);
    double rdot = dot(r, r);
    double bnorm = norm2(b);
    if (bnorm == 0.0) return 0;
    double target = tol * bnorm;
    if (std::sqrt(rdot) < target) return 0;
    for (int it = 0; it < max_iter; ++it) {
        A.matvec(p, Ap);
        double pAp = dot(p, Ap);
        if (pAp == 0.0) break;
        double alpha = rdot / pAp;
        axpy(alpha, p, x);
        axpy(-alpha, Ap, r);
        double rdot_new = dot(r, r);
        if (std::sqrt(rdot_new) < target) return it + 1;
        double beta = rdot_new / rdot;
        for (int i = 0; i < n; ++i) p[i] = r[i] + beta * p[i];
        rdot = rdot_new;
    }
    return max_iter;
}

// ---------- dense Cholesky solve (A symmetric PD), in-place-ish ---------//
// Solves A x = b, returns x. A is n*n row-major.
std::vector<double> cholesky_solve(const std::vector<double>& A_rm,
                                   const std::vector<double>& b) {
    int n = (int)b.size();
    std::vector<double> A = A_rm; // working copy (will be destroyed -> L)
    std::vector<double> x = b;
    // L L^T = A, store L in lower triangle of A
    for (int i = 0; i < n; ++i) {
        for (int j = 0; j <= i; ++j) {
            double s = A[i * n + j];
            for (int k = 0; k < j; ++k) s -= A[i * n + k] * A[j * n + k];
            if (i == j) {
                if (s <= 0.0) throw std::runtime_error("cholesky: not PD");
                A[i * n + i] = std::sqrt(s);
            } else {
                A[i * n + j] = s / A[j * n + j];
            }
        }
    }
    // forward substitution L y = x
    for (int i = 0; i < n; ++i) {
        double s = x[i];
        for (int k = 0; k < i; ++k) s -= A[i * n + k] * x[k];
        x[i] = s / A[i * n + i];
    }
    // back substitution L^T x = y
    for (int i = n - 1; i >= 0; --i) {
        double s = x[i];
        for (int k = i + 1; k < n; ++k) s -= A[k * n + i] * x[k];
        x[i] = s / A[i * n + i];
    }
    return x;
}

// ---------- complex-step step size ---------//
inline double complex_step_h() { return 1e-200; } // complex step doesn't suffer cancellation

} // namespace mdo
