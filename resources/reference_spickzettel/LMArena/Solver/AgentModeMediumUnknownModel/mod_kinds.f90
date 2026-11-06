!================================================================================
! mod_kinds -- precision and integer kind definitions.
!
! Accuracy policy: the working precision `wp` is IEEE binary64 everywhere in the
! solver path.  Single precision `sp` is offered ONLY for mixed-precision
! preconditioning experiments (see mod_linalg), never for the residual or the
! Jacobian, so that Newton convergence and the discrete conservation laws are
! never contaminated by a lower-precision inner product.
!================================================================================
module mod_kinds
   use, intrinsic :: iso_fortran_env, only: real32, real64, int32, int64
   implicit none

   integer, parameter :: wp = real64      !< working precision (all physics)
   integer, parameter :: sp = real32      !< reduced precision (opt-in only)
   integer, parameter :: i4 = int32       !< 32-bit index (CSR arrays)
   integer, parameter :: i8 = int64       !< 64-bit counter (global nnz / flops)

   real(wp), parameter :: ZERO = 0.0_wp
   real(wp), parameter :: ONE  = 1.0_wp
   real(wp), parameter :: TWO  = 2.0_wp
   real(wp), parameter :: HALF = 0.5_wp

   !> Largest |x| accepted before we declare the linear solver diverged.
   real(wp), parameter :: HUGE_TOL = huge(ONE)/1.0e10_wp

end module mod_kinds
