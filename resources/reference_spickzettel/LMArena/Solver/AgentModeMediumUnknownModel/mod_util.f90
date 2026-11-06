!================================================================================
! mod_util -- wall clock, error/assert machinery, progress logging, memory report.
!
! Deliberately free of I/O in the hot path: `log_msg` is only ever called at
! Newton-iteration / time-step granularity, never per element.
!================================================================================
module mod_util
   use mod_kinds
   use iso_fortran_env, only: output_unit, error_unit
   implicit none
   private

   public :: wp, i4, i8
   public :: wall_time, tic, toc
   public :: log_msg, log_level_set
   public :: engine_abort
   public :: fmt_eng

   integer, save :: log_level = 2      !< 0=silent 1=warn 2=info 3=debug 4=trace
   real(wp), save :: t_start = ZERO

contains

   !---------------------------------------------------------------------------
   real(wp) function wall_time() result(t)
      integer(i8) :: c, rate
      call system_clock(c, rate)
      t = real(c, wp)/real(rate, wp)
   end function wall_time

   subroutine tic()
      t_start = wall_time()
   end subroutine tic

   real(wp) function toc()
      toc = wall_time() - t_start
   end function toc

   !---------------------------------------------------------------------------
   subroutine log_level_set(lvl)
      integer, intent(in) :: lvl
      log_level = lvl
   end subroutine log_level_set

   subroutine log_msg(msg, lvl)
      character(len=*), intent(in) :: msg
      integer, intent(in), optional :: lvl
      integer :: l
      l = 2
      if (present(lvl)) l = lvl
      if (l > log_level) return
      write(output_unit,'(a)') trim(msg)
      flush(output_unit)
   end subroutine log_msg

   !---------------------------------------------------------------------------
   !> Abort the engine.  Called only for conditions from which continuing would
   !> silently produce a wrong answer (negative Jacobian, singular row, ...).
   subroutine engine_abort(msg, where)
      character(len=*), intent(in) :: msg
      character(len=*), intent(in), optional :: where
      if (present(where)) then
         write(error_unit,'(a)') 'FATAL ['//trim(where)//'] : '//trim(msg)
      else
         write(error_unit,'(a)') 'FATAL : '//trim(msg)
      end if
      flush(error_unit)
      error stop 1
   end subroutine engine_abort

   !---------------------------------------------------------------------------
   !> Compact engineering/scientific formatting for benchmark tables.
   pure function fmt_eng(x, width) result(s)
      real(wp), intent(in) :: x
      integer, intent(in), optional :: width
      character(len=32) :: s
      integer :: w
      w = 14
      if (present(width)) w = width
      if (x == ZERO) then
         write(s,'(es12.4)') x
      else if (abs(x) < 1.0e-3_wp .or. abs(x) >= 1.0e4_wp) then
         write(s,'(es12.4)') x
      else
         write(s,'(f12.5)') x
      end if
      s = adjustl(s)
   end function fmt_eng

end module mod_util
