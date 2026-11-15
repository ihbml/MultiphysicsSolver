#pragma once
#include <cstdint>

namespace continuum {
using Index = std::int32_t;
struct Range { Index begin{}, end{}; };

// Each base face appears exactly once, including corrected faces.
// "Inner" means the complete operator stencil is locally ready.
// A face between two owned cells may still depend on a field halo.
struct FaceRanges {
    Range inner_fast, inner_corrected;
    Range halo_fast, halo_corrected;
    Range correction_inner, correction_halo;
};

struct FaceBaseSoA {
    const Index* owner;
    const Index* neighbour;
    const double* alpha;
    const double* beta;
};

// These arrays contain only corrected faces, not all base faces.
struct CorrectionSoA {
    const Index* base_face;
    const double* sx;
    const double* sy;
    const double* sz;
    const double* kx;
    const double* ky;
    const double* kz;
    const double* omega;
};

struct GradientSoA { const double *x, *y, *z; };
struct CoefficientsSoA { double *pp, *pn, *np, *nn; };

// Only locally owned cells have rows; remote neighbours are ghosts.
struct CellFaceCSR {
    const std::int64_t* offsets;
    const Index* faces;
    const std::int8_t* orientation;
};
} // namespace continuum