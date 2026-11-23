// =====================================================================================
//  mt::Sparse.cpp -- CSR / ILU(0) / BiCGStab implementation
// =====================================================================================
#include "mt/Sparse.hpp"

#include <algorithm>
#include <cmath>
#include <numeric>

namespace mt {

void CsrMatrix::resize(int rows, int cols) {
    nrows = rows; ncols = cols;
    rowPtr.assign(static_cast<std::size_t>(rows) + 1, 0);
    colIdx.clear(); values.clear();
    acc_.assign(static_cast<std::size_t>(rows), {});
}

void CsrMatrix::clear() {
    nrows = ncols = 0;
    rowPtr.clear(); colIdx.clear(); values.clear();
    acc_.clear();
}

void CsrMatrix::addEntry(int row, int col, double value) {
    if (row < 0 || row >= nrows || value == 0.0) return;
    acc_[static_cast<std::size_t>(row)].emplace_back(col, value);
}

void CsrMatrix::compress() {
    // sort every row by column index and merge the duplicates
    colIdx.clear(); values.clear();
    rowPtr.assign(static_cast<std::size_t>(nrows) + 1, 0);
    for (int r = 0; r < nrows; ++r) {
        auto& row = acc_[static_cast<std::size_t>(r)];
        std::sort(row.begin(), row.end(),
                  [](const std::pair<int, double>& a, const std::pair<int, double>& b) { return a.first < b.first; });
        for (std::size_t i = 0; i < row.size();) {
            std::size_t j = i;
            double v = 0.0;
            while (j < row.size() && row[j].first == row[i].first) { v += row[j].second; ++j; }
            if (v != 0.0) {
                colIdx.push_back(row[i].first);
                values.push_back(v);
            }
            i = j;
        }
        rowPtr[static_cast<std::size_t>(r) + 1] = static_cast<int>(colIdx.size());
    }
    acc_.clear();
}

std::vector<double> CsrMatrix::multiply(const std::vector<double>& x) const {
    std::vector<double> y(static_cast<std::size_t>(nrows), 0.0);
    for (int r = 0; r < nrows; ++r) {
        double s = 0.0;
        for (int e = rowPtr[static_cast<std::size_t>(r)]; e < rowPtr[static_cast<std::size_t>(r) + 1]; ++e)
            s += values[static_cast<std::size_t>(e)] * x[static_cast<std::size_t>(colIdx[static_cast<std::size_t>(e)])];
        y[static_cast<std::size_t>(r)] = s;
    }
    return y;
}

void CsrMatrix::multiplyAdd(const std::vector<double>& x, const std::vector<double>& b,
                            std::vector<double>& y) const {
    y = multiply(x);
    for (int i = 0; i < nrows; ++i) y[static_cast<std::size_t>(i)] += b[static_cast<std::size_t>(i)];
}

double CsrMatrix::diagonal(int row) const {
    for (int e = rowPtr[static_cast<std::size_t>(row)]; e < rowPtr[static_cast<std::size_t>(row) + 1]; ++e)
        if (colIdx[static_cast<std::size_t>(e)] == row) return values[static_cast<std::size_t>(e)];
    return 0.0;
}

double CsrMatrix::rowSum(int row) const {
    double s = 0.0;
    for (int e = rowPtr[static_cast<std::size_t>(row)]; e < rowPtr[static_cast<std::size_t>(row) + 1]; ++e)
        s += values[static_cast<std::size_t>(e)];
    return s;
}

// -------------------------------------------------------------------------------------
//  ILU(0)
// -------------------------------------------------------------------------------------
bool Ilu0Preconditioner::build(const CsrMatrix& A) {
    active_ = false;
    const int n = A.nrows;
    rowPtr = A.rowPtr; colIdx = A.colIdx; values = A.values;
    diag.assign(static_cast<std::size_t>(n), 0.0);
    for (int r = 0; r < n; ++r) {
        const int r0 = rowPtr[static_cast<std::size_t>(r)], r1 = rowPtr[static_cast<std::size_t>(r) + 1];
        for (int e = r0; e < r1; ++e) {
            if (colIdx[static_cast<std::size_t>(e)] != r) continue;
            const double d = values[static_cast<std::size_t>(e)];
            if (std::abs(d) < 1e-300) return false;
            diag[static_cast<std::size_t>(r)] = d;
            break;
        }
        if (diag[static_cast<std::size_t>(r)] == 0.0) return false;
    }
    // factorise in place (ILU(0): only the existing sparsity pattern is used)
    for (int r = 0; r < n; ++r) {
        const int r0 = rowPtr[static_cast<std::size_t>(r)], r1 = rowPtr[static_cast<std::size_t>(r) + 1];
        for (int e = r0; e < r1; ++e) {
            const int c = colIdx[static_cast<std::size_t>(e)];
            if (c >= r) break;
            const double piv = values[static_cast<std::size_t>(e)];
            // row update : A[r][c] -= L[r][c] * U^{-1}[c][*]
            const int c0 = rowPtr[static_cast<std::size_t>(c)], c1 = rowPtr[static_cast<std::size_t>(c) + 1];
            for (int f = c0; f < c1; ++f) {
                const int cf = colIdx[static_cast<std::size_t>(f)];
                if (cf < c) continue;
                // find the position of (r, cf) in row r
                for (int g = e + 1; g < r1; ++g) {
                    if (colIdx[static_cast<std::size_t>(g)] == cf) {
                        values[static_cast<std::size_t>(g)] -= piv * values[static_cast<std::size_t>(f)] / diag[static_cast<std::size_t>(c)];
                        break;
                    }
                }
            }
        }
        // scale the strictly-lower part
        for (int e = r0; e < r1; ++e) {
            const int c = colIdx[static_cast<std::size_t>(e)];
            if (c >= r) break;
            values[static_cast<std::size_t>(e)] /= diag[static_cast<std::size_t>(c)];
        }
    }
    active_ = true;
    return true;
}

void Ilu0Preconditioner::apply(const std::vector<double>& r, std::vector<double>& z) const {
    if (!active_) { z = r; return; }
    const int n = static_cast<int>(rowPtr.size()) - 1;
    z.assign(static_cast<std::size_t>(n), 0.0);
    // forward substitution (L, unit diagonal)
    for (int i = 0; i < n; ++i) {
        double s = r[static_cast<std::size_t>(i)];
        for (int e = rowPtr[static_cast<std::size_t>(i)]; e < rowPtr[static_cast<std::size_t>(i) + 1]; ++e) {
            const int c = colIdx[static_cast<std::size_t>(e)];
            if (c >= i) break;
            s -= values[static_cast<std::size_t>(e)] * z[static_cast<std::size_t>(c)];
        }
        z[static_cast<std::size_t>(i)] = s;
    }
    // back substitution (U)
    for (int i = n - 1; i >= 0; --i) {
        double s = z[static_cast<std::size_t>(i)];
        double d = 0.0;
        for (int e = rowPtr[static_cast<std::size_t>(i)]; e < rowPtr[static_cast<std::size_t>(i) + 1]; ++e) {
            const int c = colIdx[static_cast<std::size_t>(e)];
            if (c == i) { d = values[static_cast<std::size_t>(e)]; continue; }
            if (c < i) continue;
            s -= values[static_cast<std::size_t>(e)] * z[static_cast<std::size_t>(c)];
        }
        z[static_cast<std::size_t>(i)] = (std::abs(d) > 1e-300) ? s / d : 0.0;
    }
}

// -------------------------------------------------------------------------------------
//  BiCGStab
// -------------------------------------------------------------------------------------
SolveReport biCGStab(const CsrMatrix& A, const std::vector<double>& b, std::vector<double>& x,
                     double rtol, int maxIter, bool useIlu) {
    SolveReport rep;
    const int n = A.nrows;
    x.assign(static_cast<std::size_t>(n), 0.0);
    Ilu0Preconditioner pre;
    if (useIlu) pre.build(A);

    const auto dot = [](const std::vector<double>& a, const std::vector<double>& c) {
        double s = 0.0; for (std::size_t i = 0; i < a.size(); ++i) s += a[i] * c[i]; return s;
    };
    auto norm = [&](const std::vector<double>& a) {
        double s = 0.0; for (double v : a) s += v * v; return std::sqrt(s);
    };

    std::vector<double> r(static_cast<std::size_t>(n)), r0(static_cast<std::size_t>(n));
    std::vector<double> p(static_cast<std::size_t>(n)), v(static_cast<std::size_t>(n));
    std::vector<double> s(static_cast<std::size_t>(n)), t(static_cast<std::size_t>(n));
    std::vector<double> ph, sh;

    A.multiplyAdd(x, b, r);
    for (int i = 0; i < n; ++i) r0[static_cast<std::size_t>(i)] = r[static_cast<std::size_t>(i)];
    const double bnorm = norm(b);
    if (bnorm < 1e-300) { rep.converged = true; return rep; }

    double rhoOld = 1.0, alpha = 1.0, omega = 1.0;
    double rho = dot(r0, r);
    if (std::abs(rho) < 1e-300) rho = 1e-300;

    for (int it = 0; it < maxIter; ++it) {
        double rhoNew = dot(r0, r);
        if (std::abs(rhoNew) < 1e-300) rhoNew = 1e-300;
        const double beta = (rhoNew / rho) * (alpha / omega);
        rho = rhoNew;
        for (int i = 0; i < n; ++i) p[static_cast<std::size_t>(i)] = r[static_cast<std::size_t>(i)] + beta * (p[static_cast<std::size_t>(i)] - omega * v[static_cast<std::size_t>(i)]);
        pre.apply(p, ph);
        v = A.multiply(ph);
        const double denom = dot(r0, v);
        if (std::abs(denom) < 1e-300) break;
        alpha = rho / denom;
        for (int i = 0; i < n; ++i) s[static_cast<std::size_t>(i)] = r[static_cast<std::size_t>(i)] - alpha * v[static_cast<std::size_t>(i)];
        const double snorm = norm(s);
        if (snorm / bnorm < rtol) {
            for (int i = 0; i < n; ++i) x[static_cast<std::size_t>(i)] += alpha * ph[static_cast<std::size_t>(i)];
            A.multiplyAdd(x, b, r);
            rep.iterations = it + 1;
            rep.residual = norm(r) / bnorm;
            rep.converged = rep.residual < rtol;
            return rep;
        }
        pre.apply(s, sh);
        t = A.multiply(sh);
        const double tt = dot(t, t);
        omega = (tt > 1e-300) ? dot(t, s) / tt : 0.0;
        for (int i = 0; i < n; ++i) x[static_cast<std::size_t>(i)] += alpha * ph[static_cast<std::size_t>(i)] + omega * sh[static_cast<std::size_t>(i)];
        for (int i = 0; i < n; ++i) r[static_cast<std::size_t>(i)] = s[static_cast<std::size_t>(i)] - omega * t[static_cast<std::size_t>(i)];
        rep.iterations = it + 1;
        rep.residual = norm(r) / bnorm;
        if (rep.residual < rtol) { rep.converged = true; return rep; }
        if (std::abs(omega) < 1e-300) break;
    }
    A.multiplyAdd(x, b, r);
    rep.residual = norm(r) / bnorm;
    rep.converged = rep.residual < rtol;
    return rep;
}

SolveReport gaussSeidel(const CsrMatrix& A, const std::vector<double>& b, std::vector<double>& x,
                         int sweeps, double rtol) {
    SolveReport rep;
    const int n = A.nrows;
    x.assign(static_cast<std::size_t>(n), 0.0);
    for (int s = 0; s < sweeps; ++s) {
        for (int r = 0; r < n; ++r) {
            double sum = b[static_cast<std::size_t>(r)];
            double d = 0.0;
            for (int e = A.rowPtr[static_cast<std::size_t>(r)]; e < A.rowPtr[static_cast<std::size_t>(r) + 1]; ++e) {
                const int c = A.colIdx[static_cast<std::size_t>(e)];
                if (c == r) d = A.values[static_cast<std::size_t>(e)];
                else sum -= A.values[static_cast<std::size_t>(e)] * x[static_cast<std::size_t>(c)];
            }
            if (std::abs(d) > 1e-300) x[static_cast<std::size_t>(r)] = sum / d;
        }
        if (s % 10 == 9 || s == sweeps - 1) {
            std::vector<double> rvec;
            A.multiplyAdd(x, b, rvec);
            double nr = 0.0, nb = 0.0;
            for (int i = 0; i < n; ++i) { nr += rvec[static_cast<std::size_t>(i)] * rvec[static_cast<std::size_t>(i)]; nb += b[static_cast<std::size_t>(i)] * b[static_cast<std::size_t>(i)]; }
            rep.residual = std::sqrt(nr) / std::max(std::sqrt(nb), 1e-300);
            rep.iterations = s + 1;
            if (rep.residual < rtol) { rep.converged = true; break; }
        }
    }
    return rep;
}

} // namespace mt
