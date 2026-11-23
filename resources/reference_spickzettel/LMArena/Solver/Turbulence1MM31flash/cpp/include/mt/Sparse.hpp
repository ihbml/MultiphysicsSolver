// =====================================================================================
//  mt::Sparse.hpp -- CSR matrices, ILU(0) preconditioner and Krylov solvers.
//
//  The solver kernel is intentionally allocation-light: the matrix is assembled with
//  a row-wise accumulator (no atomics, deterministic ordering) and compressed into
//  CSR once per equation.
//
//  Language standard : C++20
// =====================================================================================
#pragma once

#include "mt/Types.hpp"

#include <vector>

namespace mt {

// -------------------------------------------------------------------------------------
//  Compressed sparse row matrix
// -------------------------------------------------------------------------------------
class CsrMatrix {
public:
    int nrows = 0, ncols = 0;
    std::vector<int>    rowPtr;
    std::vector<int>    colIdx;
    std::vector<double> values;

    void resize(int rows, int cols);
    void clear();
    //!< add (accumulate) an entry; the entries may be added in any order
    void addEntry(int row, int col, double value);
    //!< compress the accumulated rows into CSR form (duplicates are summed)
    void compress();
    ///< y = A x
    std::vector<double> multiply(const std::vector<double>& x) const;
    ///< y = A x + b
    void multiplyAdd(const std::vector<double>& x, const std::vector<double>& b, std::vector<double>& y) const;
    double diagonal(int row) const;
    double rowSum(int row) const;
    int    nnz() const { return static_cast<int>(values.size()); }

private:
    //!< row-wise accumulator (order independent)
    std::vector<std::vector<std::pair<int, double>>> acc_;
};

// -------------------------------------------------------------------------------------
//  ILU(0) preconditioner (incomplete LU, zero fill) with a Jacobi fallback
// -------------------------------------------------------------------------------------
class Ilu0Preconditioner {
public:
    bool build(const CsrMatrix& A);
    void apply(const std::vector<double>& r, std::vector<double>& z) const;
    bool active() const { return active_; }

private:
    bool active_ = false;
    std::vector<int>    rowPtr;
    std::vector<int>    colIdx;
    std::vector<double> values;   // L (unit diagonal) and U interleaved per row
    std::vector<double> diag;     // U diagonal
};

// -------------------------------------------------------------------------------------
//  Krylov solvers
// -------------------------------------------------------------------------------------
struct SolveReport {
    int    iterations = 0;
    double residual = 0.0;      //!< final ||b - Ax|| / ||b||
    bool   converged = false;
};

SolveReport biCGStab(const CsrMatrix& A, const std::vector<double>& b, std::vector<double>& x,
                     double rtol = 1e-10, int maxIter = 2000, bool useIlu = true);

SolveReport gaussSeidel(const CsrMatrix& A, const std::vector<double>& b, std::vector<double>& x,
                         int sweeps = 500, double rtol = 1e-10);

} // namespace mt
