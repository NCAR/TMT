program chemistry
  use iso_fortran_env, only: real64
  use musica_micm, only: micm_t, RosenbrockStandardOrder, solver_stats_t
  use musica_state, only: state_t
  use musica_util, only: error_t, string_t
  implicit none
  type(micm_t), pointer :: solver
  type(state_t), pointer :: state
  type(error_t) :: error
  type(string_t) :: result
  type(solver_stats_t) :: stats
  character(4096) :: path
  integer :: a
  call get_command_argument(1,path)
  solver => micm_t(trim(path),RosenbrockStandardOrder,error)
  if (.not.error%is_success()) stop 1
  state => solver%get_state(1,error)
  if (.not.error%is_success()) stop 2
  state%conditions(1)%temperature=300._real64
  state%conditions(1)%pressure=100000._real64
  state%conditions(1)%air_density=100000._real64/(8.31446261815324_real64*300._real64)
  a=state%species_ordering%index('A',error)
  state%concentrations(a)=1._real64
  call solver%solve(1._real64,state,result,stats,error)
  if (.not.error%is_success()) stop 3
  if (abs(state%concentrations(a)-exp(-.2_real64))>1e-6_real64) stop 4
  print *, result%get_char_array(), state%concentrations(a)
  deallocate(state,solver)
end program
