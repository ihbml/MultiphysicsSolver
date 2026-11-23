#include "continuum.hpp"

namespace continuum::adjoint {

// Pure scalar contraction over the same owned-cell gather as the primal residual.
// Enzyme differentiates face_flux in reverse without any atomic face scatter.
extern "C" double contracted_csr_flux(int owned, const int* offsets, const int* faces,
                                       const int8_t* signs, const double* face_flux,
                                       const double* lambda) {
  double result = 0;
  for (int cell = 0; cell < owned; ++cell) {
    double divergence = 0;
    for (int k = offsets[cell]; k < offsets[cell + 1]; ++k)
      divergence += static_cast<double>(signs[k]) * face_flux[faces[k]];
    result += lambda[cell] * divergence;
  }
  return result;
}

#ifdef CONTINUUM_USE_ENZYME
extern "C" {
  extern int enzyme_const;
  extern int enzyme_dup;
  void __enzyme_autodiff(void*, ...);
}

extern "C" void csr_flux_vjp_enzyme(int owned, const int* offsets, const int* faces,
                                    const int8_t* signs, const double* flux,
                                    const double* lambda, double* dflux) {
  __enzyme_autodiff((void*)contracted_csr_flux,
                    enzyme_const, owned,
                    enzyme_const, offsets,
                    enzyme_const, faces,
                    enzyme_const, signs,
                    enzyme_dup, flux, dflux,
                    enzyme_const, lambda);
}
#endif

} // namespace continuum::adjoint