program fable_profiles
  use iso_fortran_env, only: real64
  use musica_micm, only: micm_t, RosenbrockStandardOrder, solver_stats_t, FableL2, FableExtrapolation
  use musica_state, only: state_t
  use musica_util, only: error_t, string_t
  implicit none
  type(micm_t), pointer :: solver
  type(state_t), pointer :: state
  type(error_t) :: error
  type(string_t) :: result
  type(solver_stats_t) :: stats
  character(4096) :: path
  integer :: a, profile, step
  call get_command_argument(1,path)
  do profile=FableL2,FableExtrapolation
    solver => micm_t(trim(path),RosenbrockStandardOrder,error)
    if (.not.error%is_success()) stop 1
    call solver%configure_fable(profile,error)
    if (.not.error%is_success()) stop 2
    state => solver%get_state(1,error)
    if (.not.error%is_success()) stop 3
    state%conditions(1)%temperature=300._real64
    state%conditions(1)%pressure=100000._real64
    state%conditions(1)%air_density=40._real64
    a=state%species_ordering%index('A',error)
    state%concentrations(a)=1._real64
    do step=1,10
      call solver%solve(1._real64,state,result,stats,error)
      if (.not.error%is_success()) stop 4
      if (stats%final_time()/=1._real64) stop 5
    end do
    if (abs(state%concentrations(a)/exp(-2._real64)-1._real64)>0.005_real64) stop 6
    call solver%configure_fable(profile,error)
    if (error%is_success()) stop 7
    deallocate(state,solver)
  end do
  print *, 'All three Fable profiles passed through Fortran'
end program
