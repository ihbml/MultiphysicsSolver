!================================================================================
! thm_bench -- driver + benchmark harness for the coupled THM engine.
!
! PROBLEM: 3-D forced convection, Biot poroelasticity with thermal pressurisation
!
!   * unit cube, HEX8 mesh, 5 unknowns per node (u_x,u_y,u_z,p,T)
!   * cold fluid is injected through a patch on the left face (p = +dp, T = Tc)
!   * drained, fixed-temperature outer boundary, rigidly clamped base
!   * temperature-dependent viscosity and density  -> buoyancy + a genuinely
!     nonlinear right-hand side, so Newton convergence is not trivially linear
!
! Run modes
!   thm_bench                 default: correctness report + scaling table
!   thm_bench --n 32          single mesh size, full time integration
!   thm_bench --help
!================================================================================
program thm_bench
   use mod_kinds
   use mod_util
   use mod_mesh
   use mod_layout
   use mod_linalg
   use mod_physics
   use mod_solver
   use omp_lib, only: omp_get_max_threads
   implicit none

   !--- problem parameters ---------------------------------------------------
   real(wp), parameter :: L        = 1.0_wp
   real(wp), parameter :: DP_INJ   = 2.0e4_wp      !< injection overpressure [Pa]
   real(wp), parameter :: T_INJ    = 283.15_wp     !< injected fluid temp     [K]
   real(wp), parameter :: T_INIT   = 353.15_wp     !< initial reservoir temp  [K]
   real(wp), parameter :: T_BASE   = 373.15_wp     !< hot base               [K]
   real(wp), parameter :: Q_HOT    = 2.0e4_wp      !< basal heat flux        [W/m3]

   integer :: n = 16, precond_arg = 1
   logical :: decoupled = .false., no_gravity = .false., no_alpha = .false., &
              no_advect = .false., lin_fluid = .false., no_alpha_t = .false.
   logical :: bench = .true., run_one = .false.
   character(len=32) :: arg
   integer :: nargs, i
   real(wp) :: vol

   nargs = command_argument_count()
   do i = 1, nargs
      call get_command_argument(i, arg)
      select case (trim(arg))
      case ('--help','-h')
         write(*,'(a)') ' thm_bench [--n N] [--no-bench]'
         stop
      case ('--no-bench')
         bench = .false.
      case ('--decoupled')
         decoupled = .true.
         run_one = .true.
      case ('--no-gravity')
         no_gravity = .true.
         run_one = .true.
      case ('--no-alpha')
         no_alpha = .true.
         run_one = .true.
      case ('--no-advect')
         no_advect = .true.
         run_one = .true.
      case ('--lin-fluid')
         lin_fluid = .true.
         run_one = .true.
      case ('--no-alpha-t')
         no_alpha_t = .true.
         run_one = .true.
      case ('--precond')
         call get_command_argument(i+1, arg)
         read(arg,*) precond_arg
         run_one = .true.
      case ('--n')
         call get_command_argument(i+1, arg)
         read(arg,*) n
         run_one = .true.
      end select
   end do

   write(*,'(a)') ''
   write(*,'(a)') ' ================================================================'
   write(*,'(a)') '  COUPLED THM MULTIPHYSICS ENGINE  --  Fortran 2018'
   write(*,'(a)') ' ================================================================'
   write(*,'(a,i3)') '  OpenMP threads available : ', omp_get_max_threads()
   write(*,'(a)') ''

   if (run_one) then
      call run_case(n, .true.)
   else if (bench) then
      call run_scaling()
   else
      call run_case(n, .true.)
   end if
   write(*,'(a)') ''

contains

   !===========================================================================
   subroutine run_scaling()
      integer :: sizes(4), k, nn
      sizes = [8, 16, 24, 32]
      write(*,'(a)') '  SCALING STUDY (3 Newton-bounded time steps each)'
      write(*,'(a)') '  ----------------------------------------------------------------'
      write(*,'(a)') '    n     cells     dofs     asm-elems/s   krylov-it  newton-it   cpu(s)'
      write(*,'(a)') '  ----------------------------------------------------------------'
      do k = 1, size(sizes)
         call run_case(sizes(k), .false.)
      end do
      write(*,'(a)') '  ----------------------------------------------------------------'
   end subroutine run_scaling

   !===========================================================================
   subroutine run_case(nn, verbose)
      integer, intent(in) :: nn
      logical, intent(in) :: verbose

      type(mesh_t)        :: m
      type(engine_state_t):: st
      type(props_t)       :: pr
      type(bc_set_t)      :: bs
      type(solver_opts_t) :: opts
      real(wp) :: t0, tgen, tl, tn, vol, ndt
      integer :: i, id, g
      real(wp) :: x(3)

      call log_level_set(merge(2, 1, verbose))

      t0 = wall_time()
      call generate_hex8(m, nn, nn, nn, L, L, L)
      tgen = wall_time() - t0

      call setup_props(pr)
      if (decoupled) then
         pr%alphaB = ZERO; pr%alphaT = ZERO; pr%nonlinear_fluid = .false.
         pr%gravity_on = .false.; pr%advect_heat = .false.
      end if
      if (no_gravity) pr%gravity_on = .false.
      if (no_alpha) then
         pr%alphaB = ZERO; pr%alphaT = ZERO
      end if
      if (no_advect) pr%advect_heat = .false.
      if (lin_fluid) pr%nonlinear_fluid = .false.
      if (no_alpha_t) pr%alphaT = ZERO
      if (decoupled .or. no_gravity .or. no_alpha .or. no_advect .or. lin_fluid .or. no_alpha_t) call props_init(pr)
      call setup_bcs(m, bs)

      !--- initial state: hydrostatic-ish, hot -----------------------------
      block
         real(wp), allocatable :: x0(:)
         allocate(x0(5*m%nnodes))
         do i = 1, m%nnodes
            g = 5*(i-1)
            x(1:3) = m%xyz(:,i)
            x0(g+1:g+3) = ZERO
            ! initial pressure: hydrostatic column + a mild overpressure at the base
            x0(g+4) = pr%rho_f*9.81_wp*(L - x(3)) + 1.0e4_wp*x(3)**2
            ! initial temperature: hot base cooling towards the top
            x0(g+5) = T_INIT + 40.0_wp*exp(-3.0_wp*x(3))
         end do
         call engine_init(st, m, pr, bs, x0)
      end block

      opts%dt          = 1.0_wp
      opts%dt_max      = 1.0_wp
      opts%t_end       = merge(5.0_wp, 2.0_wp, verbose)
      opts%newton_tol  = 1.0e-9_wp
      opts%lin_tol     = 1.0e-10_wp
      opts%newton_max  = 20
      opts%lin_max     = 400
      opts%adaptive    = .false.        ! keep the benchmark comparable
      opts%precond     = precond_arg
      opts%line_search = .true.
      st%verbose_newton = verbose

      t0 = wall_time()
      if (verbose) st%verbose_newton = .true.
      call engine_run(st, opts)
      tl = wall_time() - t0

      vol = sum(st%mesh%volume)
      write(*,'(a)') ''
      write(*,'(a,i6,a,i8,a,i9,a)') '  mesh ', nn, '^3 -> cells=', m%ncells, &
         '  dofs=', st%lay%ndof, ''
      write(*,'(a,f8.1,a,f8.2)') '  mesh generation        : ', tgen, ' s   (', &
         real(m%ncells,wp)/max(tgen,tiny(ONE))/1.0e6_wp
      write(*,'(a,f8.2,a)')      '  total solve wall time  : ', tl, ' s'
      write(*,'(a,f8.3,a)')      '  mean per time step     : ', tl/max(st%step,1), ' s'
      write(*,'(a,i9,a,f10.1)')  '  assembly calls         : ', st%n_asm, &
         '   avg ', st%asm_time/max(st%n_asm,1), ' s'
      write(*,'(a,i9)')          '  total Newton iterations: ', st%total_newton_iters
      write(*,'(a,i9,a,f8.1)')   '  total Krylov iterations: ', st%total_lin_iters, &
         '   avg/Newton ', real(st%total_lin_iters,wp)/max(st%total_newton_iters,1)
      write(*,'(a,f10.1,a)')     '  matrix non-zeros (blk) : ', real(st%A%nnz,wp), &
         ''
      ! throughput summary (used by the scaling table)
      write(*,'(a,f10.3,a,f10.1,a)') '  >> ', real(st%n_asm*m%ncells,wp)/max(st%asm_time,tiny(ONE))/1.0e6_wp, &
         ' M element-residuals/s (jac)', real(st%total_lin_iters,wp), ' krylov it total'

      if (verbose) call report_fields(st)

      call engine_destroy(st)
      call mesh_destroy(m)
   end subroutine run_case

   !===========================================================================
   subroutine setup_props(pr)
      type(props_t), intent(inout) :: pr
      pr%E        = 5.0e9_wp         ! 5 GPa   -- compliant reservoir rock
      pr%nu       = 0.25_wp
      pr%alphaB   = 0.7_wp
      pr%alphaT   = 2.0e-5_wp
      pr%rho_s    = 2700.0_wp
      pr%rho_b    = 2500.0_wp
      pr%Kperm    = 1.0e-14_wp       ! 10 mD   -- permeable sandstone
      pr%mu_f     = 5.0e-4_wp        ! 0.5 cP cold water
      pr%rho_f    = 1000.0_wp
      pr%cf       = 4180.0_wp
      pr%beta_f   = 3.0e-4_wp
      pr%biotM    = 1.0e9_wp
      pr%lambda_t = 2.5_wp
      pr%rho_cp   = 2.4e6_wp
      pr%T0       = T_INIT
      pr%g_vec    = [ZERO, ZERO, -9.81_wp]
      pr%gravity_on      = .false.
      pr%advect_heat     = .true.
      pr%nonlinear_fluid = .true.
      ! per-cell source: hot basement
      call props_init(pr)
   end subroutine setup_props

   !===========================================================================
   subroutine setup_bcs(m, bs)
      type(mesh_t),  intent(in) :: m
      type(bc_set_t),intent(inout) :: bs
      integer :: id
      real(wp) :: x(3)

      bs%n = 0
      do id = 1, m%nnodes
         x = m%xyz(:,id)
         !--- clamped base (z = 0) -----------------------------------------
         if (abs(x(3)) < 1.0e-12_wp) then
            call bc_add(bs, id, 1, ZERO)
            call bc_add(bs, id, 2, ZERO)
            call bc_add(bs, id, 3, ZERO)
         end if
         !--- boundaries: drained + fixed temperature ------------------------
         if (any(abs(x) < 1.0e-12_wp) .or. any(abs(x - L) < 1.0e-12_wp)) then
            call bc_add(bs, id, 4, ZERO)
            call bc_add(bs, id, 5, merge(T_BASE, 340.0_wp, abs(x(3)) < 1.0e-12_wp))
         end if
         !--- injection patch on the left face (x = 0, around the axis) ------
         if (abs(x(1)) < 1.0e-12_wp) then
            if ((x(2)-0.5_wp)**2 + (x(3)-0.5_wp)**2 < 0.20_wp**2) then
               call bc_add(bs, id, 4, DP_INJ)
               call bc_add(bs, id, 5, T_INJ)
            end if
         end if
      end do
      !--- optional: keep the top face free to move (default already free) ---
   end subroutine setup_bcs

   !===========================================================================
   subroutine report_fields(st)
      type(engine_state_t), intent(in) :: st
      real(wp) :: umax, pmax, pmin, tmax, tmin, divu
      integer :: i
      umax = ZERO; pmax = -huge(ONE); pmin = huge(ONE); tmax = pmax; tmin = pmin
      do i = 1, st%mesh%nnodes
         umax = max(umax, maxval(abs(st%X(5*(i-1)+1:5*(i-1)+3))))
         pmax = max(pmax, st%X(5*(i-1)+4));  pmin = min(pmin, st%X(5*(i-1)+4))
         tmax = max(tmax, st%X(5*(i-1)+5));  tmin = min(tmin, st%X(5*(i-1)+5))
      end do
      write(*,'(a)') '  ----------------------------------------------------------------'
      write(*,'(a,es11.3,a)') '  |u|_max        = ', umax, ' m'
      write(*,'(a,es11.3,a,es11.3,a)') '  p range        = [', pmin, ', ', pmax, '] Pa'
      write(*,'(a,f9.3,a,f9.3,a)') '  T range        = [', tmin, ', ', tmax, '] K'
      write(*,'(a)') '  ----------------------------------------------------------------'
   end subroutine report_fields

end program thm_bench
