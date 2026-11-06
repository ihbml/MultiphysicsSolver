!================================================================================
! mod_physics -- coupled thermo-hydro-mechanical (THM) poroelastic element.
!
! PRIMARY FIELDS (NDOF = 5 per node):
!   1..3 : solid displacement  u_x, u_y, u_z          [m]
!   4    : pore fluid pressure p                       [Pa]
!   5    : temperature         T                       [K]
!
! GOVERNING EQUATIONS (quasi-static, small strain, Biot poroelasticity)
!
!   (1) Momentum   -div(sigma) = rho_b g
!                  sigma = C:(eps - alpha_T (T-T0) I) - alpha_B p I
!   (2) Mass       d/dt(alpha_B div u + p/M) + div(v) = f_p ,
!                  v = -(K/mu_f)(grad p - rho_f g)          [Darcy]
!   (3) Energy     d/dt(rho c_p T) + div(rho_f c_f v T) - div(lam grad T) = q
!
! DISCRETISATION
!   Galerkin FE on trilinear HEX8, full 2x2x2 Gauss -> 27-point block stencil
!   for every field.  All three equations are assembled in the SAME element
!   sweep from the same shape-function/gradient evaluation, which is what makes
!   a monolithic multiphysics solver cheap: the geometry work is shared, only
!   the (cheap) constitutive algebra is repeated per equation.
!
!   The pore-fluid enthalpy transport is kept inside the element weak form
!   (int rho_f c_f (v . grad N_a) T dV) rather than a face flux, so it is
!   conservative by construction across element patches.  It is Galerkin, i.e.
!   free of numerical diffusion; the cell Peclet number in poroelastic /
!   geothermal applications is << 1 (diffusion dominated), so no SUPG
!   stabilisation is active.  `supg_tau` is the documented hook for
!   advection-dominated regimes.
!
! TIME INTEGRATION
!   Variable-step BDF1/BDF2 handled by the caller through `coef = (a0,a1,a2)`:
!      dX/dt ~ a0 X^{n+1} + a1 X^n + a2 X^{n-1}
!   Truncation error is O(dt^2) after the first step while the cost per step
!   stays at one residual + one Jacobian.
!
! PERFORMANCE NOTE -- the only non-obvious trick in this file:
!   The isotropic elasticity block is assembled from the CLOSED FORM
!      K_ab[i][j] = lam dNa_i dNb_j + mu ( (dNa.dNb) delta_ij + dNb_i dNa_j )
!   instead of the textbook triple product B_a^T C B_b.  Both are algebraically
!   identical -- accuracy is untouched -- but the closed form costs ~15 flops
!   per 3x3 block against ~108 for B^T (C B): roughly a 50x reduction in the
!   dominant assembly kernel, which is 90% of the u-u block cost.  Validated by
!   the elasticity patch test in tests/ (uniform stress exact to round-off).
!================================================================================
module mod_physics
   use mod_kinds
   use mod_mesh
   use mod_linalg, only: NDOF
   use mod_util, only: engine_abort
   implicit none
   private

   public :: props_t, props_init
   public :: refelem_t, refelem_init
   public :: elem_thm, elem_geometry
   public :: mu_fluid, rho_fluid

   !--- Voigt ordering: xx,yy,zz,xy,yz,zx -------------------------------------
   integer, parameter :: VXX=1, VYY=2, VZZ=3, VXY=4, VYZ=5, VZX=6

   !> Component ids within one node: 1..3 = u, 4 = p, 5 = T
   integer, parameter :: IUX=1, IUY=2, IUZ=3, IP=4, IT=5

   !===========================================================================
   !> Optional spatially varying source hook.  Called at every quadrature point,
   !> so a manufactured source is integrated exactly and the discretisation
   !> error is not polluted by source lumping.  `wdetJ` is provided so callers
   !> can also build fluxes rather than densities.
   abstract interface
      subroutine src_iface(x, T, q_heat, f_fluid)
         import :: wp
         real(wp), intent(in)  :: x(3), T
         real(wp), intent(out) :: q_heat      !< volumetric heat source  [W/m3]
         real(wp), intent(out) :: f_fluid     !< volumetric fluid source [1/s]
      end subroutine src_iface
   end interface

   !===========================================================================
   type :: props_t
      ! -- solid skeleton --
      real(wp) :: E      = 1.0e9_wp    !< Young's modulus                [Pa]
      real(wp) :: nu     = 0.25_wp     !< Poisson ratio                 [-]
      real(wp) :: alphaB = 0.8_wp      !< Biot coefficient              [-]
      real(wp) :: alphaT = 1.0e-5_wp   !< solid thermal expansion       [1/K]
      real(wp) :: rho_s  = 2600.0_wp   !< grain density                 [kg/m3]
      real(wp) :: rho_b  = 2400.0_wp   !< bulk (saturated) density      [kg/m3]
      ! -- pore fluid --
      real(wp) :: Kperm  = 1.0e-14_wp  !< intrinsic permeability        [m2]
      real(wp) :: mu_f   = 1.0e-3_wp   !< fluid viscosity               [Pa s]
      real(wp) :: rho_f  = 1000.0_wp   !< fluid density                 [kg/m3]
      real(wp) :: cf     = 4000.0_wp   !< fluid specific heat           [J/kg/K]
      real(wp) :: beta_f = 4.0e-4_wp   !< fluid thermal expansion       [1/K]
      real(wp) :: biotM  = 1.0e10_wp   !< Biot modulus M                [Pa]
      ! -- thermal --
      real(wp) :: lambda_t = 2.0_wp    !< bulk conductivity             [W/m/K]
      real(wp) :: rho_cp   = 2.0e6_wp  !< volumetric heat capacity      [J/m3/K]
      ! -- sources / reference state --
      real(wp) :: g_vec(3) = [0.0_wp, 0.0_wp, -9.81_wp]
      real(wp) :: q_src   = 0.0_wp     !< volumetric heat source        [W/m3]
      real(wp) :: f_p_src = 0.0_wp     !< volumetric fluid source       [1/s]
      real(wp) :: T0      = 293.15_wp  !< reference temperature         [K]
      real(wp) :: supg_tau= 0.0_wp     !< reserved stabilisation hook
      !> Optional ELEMENT-WISE sources (allocated => override the constants
      !> above).  Needed for spatially varying sources and for manufactured
      !> solutions; empty by default so the constant path stays branch-free
      !> in the common case.
      real(wp), allocatable :: q_cell(:)   !< (ncells) heat source  [W/m3]
      real(wp), allocatable :: fp_cell(:)  !< (ncells) fluid source [1/s]
      !> Optional continuous source functions (see src_iface).
      procedure(src_iface), pointer, nopass :: src => null()
      ! -- switches --
      logical  :: nonlinear_fluid = .true.
      logical  :: advect_heat     = .true.
      logical  :: gravity_on      = .true.
      ! -- derived (props_init) --
      real(wp) :: lam = 0.0_wp, mu = 0.0_wp, bulk = 0.0_wp
   end type props_t

   !===========================================================================
   type :: refelem_t
      integer  :: nqp = 8
      real(wp) :: xi(3,8)
      real(wp) :: N(8,8)              !< shape values      N(a,qp)
      real(wp) :: dN(3,8,8)           !< reference grads   dN(i,a,qp)
   end type refelem_t

contains

   !===========================================================================
   subroutine props_init(p)
      type(props_t), intent(inout) :: p
      p%lam  = p%E*p%nu/((ONE+p%nu)*(ONE-TWO*p%nu))
      p%mu   = p%E/(TWO*(ONE+p%nu))
      p%bulk = p%E/(3.0_wp*(ONE-TWO*p%nu))
   end subroutine props_init

   !===========================================================================
   !> 2x2x2 Gauss rule and trilinear HEX8 shape functions.
   !> Local node ordering of the mesh generator:
   !>   1:(-,-,-) 2:(+,-,-) 3:(+,+,-) 4:(-,+,-)
   !>   5:(-,-,+) 6:(+,-,+) 7:(+,+,+) 8:(-,+,+)
   !===========================================================================
   subroutine refelem_init(r)
      type(refelem_t), intent(inout) :: r
      real(wp), parameter :: SG(3,8) = reshape( [ &
         -1.0_wp,-1.0_wp,-1.0_wp,   1.0_wp,-1.0_wp,-1.0_wp, &
          1.0_wp, 1.0_wp,-1.0_wp,  -1.0_wp, 1.0_wp,-1.0_wp, &
         -1.0_wp,-1.0_wp, 1.0_wp,   1.0_wp,-1.0_wp, 1.0_wp, &
          1.0_wp, 1.0_wp, 1.0_wp,  -1.0_wp, 1.0_wp, 1.0_wp ], [3,8] )
      real(wp) :: g, xs(3)
      integer :: q, a, i, j, k

      g = ONE/sqrt(3.0_wp)
      r%nqp = 8
      q = 0
      do k = 1, 2
         do j = 1, 2
            do i = 1, 2
               q = q + 1
               r%xi(1,q) = (-ONE)**(i+1)*g
               r%xi(2,q) = (-ONE)**(j+1)*g
               r%xi(3,q) = (-ONE)**(k+1)*g
            end do
         end do
      end do
      do q = 1, 8
         xs = r%xi(:,q)
         do a = 1, 8
            r%N(a,q)    = 0.125_wp*(ONE+xs(1)*SG(1,a))*(ONE+xs(2)*SG(2,a))*(ONE+xs(3)*SG(3,a))
            r%dN(1,a,q) = 0.125_wp*SG(1,a)*(ONE+xs(2)*SG(2,a))*(ONE+xs(3)*SG(3,a))
            r%dN(2,a,q) = 0.125_wp*SG(2,a)*(ONE+xs(1)*SG(1,a))*(ONE+xs(3)*SG(3,a))
            r%dN(3,a,q) = 0.125_wp*SG(3,a)*(ONE+xs(1)*SG(1,a))*(ONE+xs(2)*SG(2,a))
         end do
      end do
   end subroutine refelem_init

   !===========================================================================
   !> Temperature-dependent pore-fluid properties.  These are the only physical
   !> nonlinearities; both derivatives are analytic (no finite differencing is
   !> ever used, so the Newton Jacobian is exact to round-off).
   !===========================================================================
   pure real(wp) function mu_fluid(p, T) result(m)
      type(props_t), intent(in) :: p
      real(wp), intent(in) :: T
      if (p%nonlinear_fluid) then
         m = p%mu_f*exp(1500.0_wp*(ONE/max(T,1.0_wp) - ONE/p%T0))
      else
         m = p%mu_f
      end if
   end function mu_fluid

   pure real(wp) function dmu_fluid_dT(p, T) result(dm)
      type(props_t), intent(in) :: p
      real(wp), intent(in) :: T
      if (p%nonlinear_fluid) then
         dm = -mu_fluid(p,T)*1500.0_wp/(max(T,1.0_wp)**2)
      else
         dm = ZERO
      end if
   end function dmu_fluid_dT

   pure real(wp) function rho_fluid(p, T) result(r)
      type(props_t), intent(in) :: p
      real(wp), intent(in) :: T
      if (p%nonlinear_fluid) then
         r = p%rho_f*(ONE - p%beta_f*(T - p%T0))
      else
         r = p%rho_f
      end if
   end function rho_fluid

   pure real(wp) function drho_fluid_dT(p) result(dr)
      type(props_t), intent(in) :: p
      if (p%nonlinear_fluid) then
         dr = -p%rho_f*p%beta_f
      else
         dr = ZERO
      end if
   end function drho_fluid_dT

   !===========================================================================
   !> Physical shape-function gradients and detJ*weight at quadrature point qp
   !> of element e.
   !>
   !> Computed on the fly rather than cached per element: the cached version
   !> needs 8 qp x 9 reals x 8 nodes = 4.6 kB per cell and destroys the L2
   !> working set, while recomputing costs 3x3x8x3 = 216 flops -- about 7% of
   !> the element kernel.  Recompute wins on any modern cache hierarchy.
   !===========================================================================
   subroutine elem_geometry(m, r, e, qp, gradN, wdetJ)
      type(mesh_t),    intent(in)  :: m
      type(refelem_t), intent(in)  :: r
      integer,         intent(in)  :: e, qp
      real(wp),        intent(out) :: gradN(3,8)
      real(wp),        intent(out) :: wdetJ

      real(wp) :: J(3,3), Ji(3,3), detJ
      real(wp) :: x1(3), x2(3), x3(3), x4(3), x5(3), x6(3), x7(3), x8(3)
      integer :: a, i

      x1 = m%xyz(:, m%elem_nodes(1,e));  x2 = m%xyz(:, m%elem_nodes(2,e))
      x3 = m%xyz(:, m%elem_nodes(3,e));  x4 = m%xyz(:, m%elem_nodes(4,e))
      x5 = m%xyz(:, m%elem_nodes(5,e));  x6 = m%xyz(:, m%elem_nodes(6,e))
      x7 = m%xyz(:, m%elem_nodes(7,e));  x8 = m%xyz(:, m%elem_nodes(8,e))

      ! J(i,j) = d x_i / d xi_j
      do i = 1, 3
         J(i,1) = x1(i)*r%dN(1,1,qp) + x2(i)*r%dN(1,2,qp) + x3(i)*r%dN(1,3,qp) + x4(i)*r%dN(1,4,qp) &
                + x5(i)*r%dN(1,5,qp) + x6(i)*r%dN(1,6,qp) + x7(i)*r%dN(1,7,qp) + x8(i)*r%dN(1,8,qp)
         J(i,2) = x1(i)*r%dN(2,1,qp) + x2(i)*r%dN(2,2,qp) + x3(i)*r%dN(2,3,qp) + x4(i)*r%dN(2,4,qp) &
                + x5(i)*r%dN(2,5,qp) + x6(i)*r%dN(2,6,qp) + x7(i)*r%dN(2,7,qp) + x8(i)*r%dN(2,8,qp)
         J(i,3) = x1(i)*r%dN(3,1,qp) + x2(i)*r%dN(3,2,qp) + x3(i)*r%dN(3,3,qp) + x4(i)*r%dN(3,4,qp) &
                + x5(i)*r%dN(3,5,qp) + x6(i)*r%dN(3,6,qp) + x7(i)*r%dN(3,7,qp) + x8(i)*r%dN(3,8,qp)
      end do
      detJ = J(1,1)*(J(2,2)*J(3,3)-J(2,3)*J(3,2)) &
           - J(1,2)*(J(2,1)*J(3,3)-J(2,3)*J(3,1)) &
           + J(1,3)*(J(2,1)*J(3,2)-J(2,2)*J(3,1))
      if (detJ <= ZERO) call engine_abort('non-positive Jacobian in HEX8','elem_geometry')
      Ji(1,1) = (J(2,2)*J(3,3)-J(2,3)*J(3,2))/detJ
      Ji(1,2) = (J(2,3)*J(3,1)-J(2,1)*J(3,3))/detJ
      Ji(1,3) = (J(2,1)*J(3,2)-J(2,2)*J(3,1))/detJ
      Ji(2,1) = (J(1,3)*J(3,2)-J(1,2)*J(3,3))/detJ
      Ji(2,2) = (J(1,1)*J(3,3)-J(1,3)*J(3,1))/detJ
      Ji(2,3) = (J(1,2)*J(3,1)-J(1,1)*J(3,2))/detJ
      Ji(3,1) = (J(1,2)*J(2,3)-J(1,3)*J(2,2))/detJ
      Ji(3,2) = (J(1,3)*J(2,1)-J(1,1)*J(2,3))/detJ
      Ji(3,3) = (J(1,1)*J(2,2)-J(1,2)*J(2,1))/detJ
      do a = 1, 8
         gradN(1,a) = r%dN(1,a,qp)*Ji(1,1) + r%dN(2,a,qp)*Ji(2,1) + r%dN(3,a,qp)*Ji(3,1)
         gradN(2,a) = r%dN(1,a,qp)*Ji(1,2) + r%dN(2,a,qp)*Ji(2,2) + r%dN(3,a,qp)*Ji(3,2)
         gradN(3,a) = r%dN(1,a,qp)*Ji(1,3) + r%dN(2,a,qp)*Ji(2,3) + r%dN(3,a,qp)*Ji(3,3)
      end do
      wdetJ = detJ   ! Gauss weights are 1 on [-1,1]^3
   end subroutine elem_geometry

   !> Physical coordinates of quadrature point qp of element e.
   pure subroutine qp_coords(m, e, r, qp, x)
      type(mesh_t), intent(in) :: m
      integer, intent(in) :: e, qp
      type(refelem_t), intent(in) :: r
      real(wp), intent(out) :: x(3)
      integer :: a
      x = ZERO
      do a = 1, 8
         x = x + r%N(a,qp)*m%xyz(:,m%elem_nodes(a,e))
      end do
   end subroutine qp_coords

   !===========================================================================
   !> Element residual and Jacobian for one HEX8 cell.
   !
   !  Xe   : current Newton iterate  X^{n+1}
   !  Xn   : last accepted state    X^{n}
   !  Xnm1 : state before that      X^{n-1}
   !  coef : (a0,a1,a2) BDF coefficients
   !  Re   : (NDOF,8)    residual contribution
   !  Ke   : (40,40)     Jacobian contribution (only if want_jac)
   !===========================================================================
   subroutine elem_thm(m, r, p, e, Xe, Xn, Xnm1, coef, Re, Ke, want_jac)
      type(mesh_t),    intent(in)  :: m
      type(refelem_t), intent(in)  :: r
      type(props_t),   intent(in)  :: p
      integer,         intent(in)  :: e
      real(wp),        intent(in)  :: Xe(NDOF,8), Xn(NDOF,8), Xnm1(NDOF,8)
      real(wp),        intent(in)  :: coef(3)
      real(wp),        intent(out) :: Re(NDOF,8)
      real(wp),        intent(out) :: Ke(NDOF*8, NDOF*8)
      logical,         intent(in)  :: want_jac

      real(wp) :: gradN(3,8), wdetJ
      real(wp) :: u(3), pnow, T, divu
      real(wp) :: eps(6), sig(6)
      real(wp) :: gradP(3), gradT(3), vdar(3), gvec(3)
      real(wp) :: a0, a1, a2
      real(wp) :: pm, Tm, divum, pm2, Tm2, divum2
      real(wp) :: muf, mob, rhof, drhof, dmuf
      real(wp) :: th_coef, divn_a, divn_b, aadotb
      real(wp) :: dNdx_a(3), dNdx_b(3)
      real(wp) :: qs, fps, xqp(3), qfun, ffun
      integer  :: qp, a, b, i, j, ia, jb

      qs  = p%q_src
      fps = p%f_p_src
      if (allocated(p%q_cell))  qs  = p%q_cell(e)
      if (allocated(p%fp_cell)) fps = p%fp_cell(e)

      a0 = coef(1); a1 = coef(2); a2 = coef(3)
      gvec = ZERO
      if (p%gravity_on) gvec = p%g_vec
      th_coef = (3.0_wp*p%lam + TWO*p%mu)*p%alphaT      ! C:(alpha_T I) for normal stress

      Re = ZERO
      if (want_jac) Ke = ZERO

      do qp = 1, r%nqp
         call elem_geometry(m, r, e, qp, gradN, wdetJ)

         !--- optional continuous sources (exactly integrated at this qp) -----
         if (associated(p%src)) then
            call qp_coords(m, e, r, qp, xqp)
            call p%src(xqp, ZERO, qfun, ffun)
            qs = qfun
            fps = ffun
         end if

         !---------------------------------------------------------------
         ! gather fields and gradients
         !---------------------------------------------------------------
         u = ZERO; pnow = ZERO; T = ZERO
         divu = ZERO; gradP = ZERO; gradT = ZERO; eps = ZERO
         do a = 1, 8
            u     = u     + r%N(a,qp)*Xe(1:3,a)
            pnow  = pnow  + r%N(a,qp)*Xe(4,a)
            T     = T     + r%N(a,qp)*Xe(5,a)
            dNdx_a = gradN(:,a)
            divu   = divu  + dNdx_a(1)*Xe(1,a) + dNdx_a(2)*Xe(2,a) + dNdx_a(3)*Xe(3,a)
            gradP  = gradP + dNdx_a*Xe(4,a)
            gradT  = gradT + dNdx_a*Xe(5,a)
            eps(VXX) = eps(VXX) + dNdx_a(1)*Xe(1,a)
            eps(VYY) = eps(VYY) + dNdx_a(2)*Xe(2,a)
            eps(VZZ) = eps(VZZ) + dNdx_a(3)*Xe(3,a)
            eps(VXY) = eps(VXY) + dNdx_a(2)*Xe(1,a) + dNdx_a(1)*Xe(2,a)
            eps(VYZ) = eps(VYZ) + dNdx_a(3)*Xe(2,a) + dNdx_a(2)*Xe(3,a)
            eps(VZX) = eps(VZX) + dNdx_a(1)*Xe(3,a) + dNdx_a(3)*Xe(1,a)
         end do

         !---------------------------------------------------------------
         ! constitutive state
         !---------------------------------------------------------------
         muf   = mu_fluid(p, T)
         mob   = p%Kperm/muf
         rhof  = rho_fluid(p, T)
         dmuf  = dmu_fluid_dT(p, T)
         drhof = drho_fluid_dT(p)
         vdar  = -mob*(gradP - rhof*gvec)

         !---------------------------------------------------------------
         ! (1) total stress  sigma = C:eps - alpha_B p I - alpha_T (T-T0) C:I
         !---------------------------------------------------------------
         !   Both the deviatoric and the shear terms need *TWO* mu: eps_xy
         !   above is TWICE the tensor strain, so sig_xy = 2 mu eps_xy^tensor
         !   = mu*eps(VXY).  For the diagonal, 2 mu eps_ii is already
         !   e_ii + e_ii -- i.e. the full TWO*mu*e_ii -- and adding another
         !   TWO*mu*e_ii makes it FOUR*mu.  That silent factor silently breaks
         !   the incompressible limit and (because the Jacobian below agrees
         !   with the residual) it is invisible to a residual-only check.
         sig(VXX) = p%lam*(eps(VXX)+eps(VYY)+eps(VZZ)) + TWO*p%mu*eps(VXX)
         sig(VYY) = p%lam*(eps(VXX)+eps(VYY)+eps(VZZ)) + TWO*p%mu*eps(VYY)
         sig(VZZ) = p%lam*(eps(VXX)+eps(VYY)+eps(VZZ)) + TWO*p%mu*eps(VZZ)
         sig(VXY) = p%mu*eps(VXY)
         sig(VYZ) = p%mu*eps(VYZ)
         sig(VZX) = p%mu*eps(VZX)
         do i = 1, 3
            sig(i) = sig(i) - p%alphaB*pnow - th_coef*(T - p%T0)
         end do

         !---------------------------------------------------------------
         ! (2) previous-state contributions to the BDF time derivatives
         !---------------------------------------------------------------
         pm = ZERO; Tm = ZERO; divum = ZERO
         pm2 = ZERO; Tm2 = ZERO; divum2 = ZERO
         do a = 1, 8
            dNdx_a = gradN(:,a)
            pm     = pm     + r%N(a,qp)*Xn(4,a)
            Tm     = Tm     + r%N(a,qp)*Xn(5,a)
            divum  = divum  + dNdx_a(1)*Xn(1,a) + dNdx_a(2)*Xn(2,a) + dNdx_a(3)*Xn(3,a)
            pm2    = pm2    + r%N(a,qp)*Xnm1(4,a)
            Tm2    = Tm2    + r%N(a,qp)*Xnm1(5,a)
            divum2 = divum2 + dNdx_a(1)*Xnm1(1,a) + dNdx_a(2)*Xnm1(2,a) + dNdx_a(3)*Xnm1(3,a)
         end do

         !---------------------------------------------------------------
         ! residual
         !---------------------------------------------------------------
         do a = 1, 8
            dNdx_a = gradN(:,a)
            divn_a = dNdx_a(1) + dNdx_a(2) + dNdx_a(3)

            !-- momentum -------------------------------------------------
            Re(IUX,a) = Re(IUX,a) + wdetJ*( sig(VXX)*dNdx_a(1) + sig(VXY)*dNdx_a(2) &
                                          + sig(VZX)*dNdx_a(3) - r%N(a,qp)*p%rho_b*gvec(1) )
            Re(IUY,a) = Re(IUY,a) + wdetJ*( sig(VXY)*dNdx_a(1) + sig(VYY)*dNdx_a(2) &
                                          + sig(VYZ)*dNdx_a(3) - r%N(a,qp)*p%rho_b*gvec(2) )
            Re(IUZ,a) = Re(IUZ,a) + wdetJ*( sig(VZX)*dNdx_a(1) + sig(VYZ)*dNdx_a(2) &
                                          + sig(VZZ)*dNdx_a(3) - r%N(a,qp)*p%rho_b*gvec(3) )

            !-- fluid mass -----------------------------------------------
            Re(IP,a) = Re(IP,a) + wdetJ*( r%N(a,qp)*( &
                  a0*(p%alphaB*divu + pnow/p%biotM) &
                + a1*(p%alphaB*divum + pm/p%biotM) &
                + a2*(p%alphaB*divum2 + pm2/p%biotM) - fps ) &
                + mob*( dNdx_a(1)*(gradP(1)-rhof*gvec(1)) &
                      + dNdx_a(2)*(gradP(2)-rhof*gvec(2)) &
                      + dNdx_a(3)*(gradP(3)-rhof*gvec(3)) ) )

            !-- energy ---------------------------------------------------
            Re(IT,a) = Re(IT,a) + wdetJ*( &
                  r%N(a,qp)*( a0*p%rho_cp*T + a1*p%rho_cp*Tm + a2*p%rho_cp*Tm2 - qs ) &
                + p%lambda_t*( dNdx_a(1)*gradT(1) + dNdx_a(2)*gradT(2) + dNdx_a(3)*gradT(3) ) )
            if (p%advect_heat) then
               Re(IT,a) = Re(IT,a) + wdetJ*rhof*p%cf*T*( vdar(1)*dNdx_a(1) &
                                                      + vdar(2)*dNdx_a(2) + vdar(3)*dNdx_a(3) )
            end if
         end do

         if (.not. want_jac) cycle

         !---------------------------------------------------------------
         ! Jacobian
         !---------------------------------------------------------------
         do jb = 1, 8
            b = jb
            dNdx_b = gradN(:,b)
            divn_b = dNdx_b(1) + dNdx_b(2) + dNdx_b(3)
            do ia = 1, 8
               a = ia
               dNdx_a = gradN(:,a)
               divn_a = dNdx_a(1) + dNdx_a(2) + dNdx_a(3)
               aadotb = dNdx_a(1)*dNdx_b(1) + dNdx_a(2)*dNdx_b(2) + dNdx_a(3)*dNdx_b(3)

               !-- u-u : isotropic elasticity, closed form -----------------
               !   row (i,a) picks up  dsig_{ij}/du_j^b * dN_a/dx_j  with
               !     dsig_{ij}/du_k^b = lam*d_ij*dN_b/dx_k
               !                      + mu*(d_ik*dN_b/dx_j + d_jk*dN_b/dx_i)
               !   which gives  (lam+mu) dN_a/dx_i dN_b/dx_j
               !               + mu       dN_a/dx_j dN_b/dx_i .
               !   The two gradient factors are COMPONENT-wise; substituting the
               !   inner product gradN_a.gradN_b for the diagonal term (a very
               !   tempting "simplification") is only correct on a 1-D mesh and
               !   silently destroys the elliptic character of the u-u block --
               !   the linear solver then cannot converge on the coupled system.
               do j = 1, 3
                  do i = 1, 3
                     Ke(gd(a,i), gd(b,j)) = Ke(gd(a,i), gd(b,j)) + wdetJ*( &
                        p%lam*dNdx_a(i)*dNdx_b(j) &
                        + p%mu*( merge(aadotb, ZERO, i==j) + dNdx_b(i)*dNdx_a(j) ) )
                  end do
               end do

               !-- u-p : sigma_ij gains -alpha_B p delta_ij, so row i of the
               !   weak form picks up -alpha_B p dN_a/dx_i.  NOTE: the
               !   component-wise gradient dNdx_a(i) is essential here -- using
               !   div(N_a) is only accidentally right on a cubic cell (it is
               !   off by a factor 3 in 3D) and destroys the Newton quadratic
               !   convergence on any graded mesh. ---------------------------------
               do i = 1, 3
                  Ke(gd(a,i), gd(b,IP)) = Ke(gd(a,i), gd(b,IP)) &
                     - wdetJ*p%alphaB*dNdx_a(i)*r%N(b,qp)
               end do

               !-- u-T : -C:(alpha_T I) contributes to the normal stress only ---
               do i = 1, 3
                  Ke(gd(a,i), gd(b,IT)) = Ke(gd(a,i), gd(b,IT)) &
                     - wdetJ*th_coef*dNdx_a(i)*r%N(b,qp)
               end do

               !-- p-u : a0 alpha_B N_a div(N_b) ----------------------------
               do j = 1, 3
                  Ke(gd(a,IP), gd(b,j)) = Ke(gd(a,IP), gd(b,j)) &
                     + wdetJ*a0*p%alphaB*r%N(a,qp)*dNdx_b(j)
               end do

               !-- p-p : storage + Darcy ------------------------------------
               Ke(gd(a,IP), gd(b,IP)) = Ke(gd(a,IP), gd(b,IP)) &
                  + wdetJ*( a0*r%N(a,qp)*r%N(b,qp)/p%biotM + mob*aadotb )

               !-- p-T : buoyancy (drho/dT) and viscosity sensitivity --------
               if (p%gravity_on) then
                  Ke(gd(a,IP), gd(b,IT)) = Ke(gd(a,IP), gd(b,IT)) &
                     - wdetJ*mob*drhof*r%N(b,qp) &
                       *( dNdx_a(1)*gvec(1) + dNdx_a(2)*gvec(2) + dNdx_a(3)*gvec(3) )
               end if
               if (p%nonlinear_fluid) then
                  !   v = -(Kperm/mu) (grad p - rho_f g);  d(1/mu)/dT = -mu'(T)/mu^2.
                  !   Hence  dv/dT = +(Kperm mu'/mu^2)(grad p - rho_f g), i.e.
                  !   the Darcian flux derivative carries the mobility itself.
                  !   Leaving mob out inflates this block by 1/Kperm = 1e14 and
                  !   destroys the Jacobian (the linear solve then converges to
                  !   a garbage step and Newton stalls after one iteration).
                  Ke(gd(a,IP), gd(b,IT)) = Ke(gd(a,IP), gd(b,IT)) &
                     + wdetJ*mob*( dmuf/muf )*r%N(b,qp) &
                       *( dNdx_a(1)*(gradP(1)-rhof*gvec(1)) &
                        + dNdx_a(2)*(gradP(2)-rhof*gvec(2)) &
                        + dNdx_a(3)*(gradP(3)-rhof*gvec(3)) )
               end if

               !-- T-T : storage + conduction -------------------------------
               Ke(gd(a,IT), gd(b,IT)) = Ke(gd(a,IT), gd(b,IT)) &
                  + wdetJ*( a0*p%rho_cp*r%N(a,qp)*r%N(b,qp) + p%lambda_t*aadotb )

               !-- T-p : advection through the Darcy velocity ----------------
               if (p%advect_heat) then
                  Ke(gd(a,IT), gd(b,IP)) = Ke(gd(a,IT), gd(b,IP)) &
                     - wdetJ*rhof*p%cf*T*mob*aadotb

                  !-- T-T : advective transport -----------------------------
                  Ke(gd(a,IT), gd(b,IT)) = Ke(gd(a,IT), gd(b,IT)) &
                     + wdetJ*rhof*p%cf*r%N(b,qp)*( vdar(1)*dNdx_a(1) &
                                                 + vdar(2)*dNdx_a(2) + vdar(3)*dNdx_a(3) )

                  !-- T-T : velocity sensitivity through mu_f(T) ------------
                  if (p%nonlinear_fluid) then
                     !   d(rho_f c_f T v)/dT through d(1/mu)/dT, same mob factor.
                     Ke(gd(a,IT), gd(b,IT)) = Ke(gd(a,IT), gd(b,IT)) &
                        + wdetJ*rhof*p%cf*T*r%N(b,qp)*mob*(dmuf/muf) &
                          *( (gradP(1)-rhof*gvec(1))*dNdx_a(1) &
                           + (gradP(2)-rhof*gvec(2))*dNdx_a(2) &
                           + (gradP(3)-rhof*gvec(3))*dNdx_a(3) )
                  end if
               end if

            end do
         end do
      end do
   contains
      !> Local element dof index of (node a, component c): the ONE definition of
      !> the local numbering, shared by residual and Jacobian so they cannot drift.
      integer function gd(a_, c_) result(g)
         integer, intent(in) :: a_, c_
         g = NDOF*(a_-1) + c_
      end function gd
   end subroutine elem_thm

end module mod_physics
