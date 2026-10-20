import ctypes as C
from .._native import P, Z, D, I, DP, Error, String, Mappings, Object, Handle, bind, call, view
from .conditions import Conditions
from .solver_result import SolverStats
EP = C.POINTER(Error)
for name in ('CreateMicm', 'CreateMicmFromConfigString', 'TmtCreateMicmPrepared'):
    bind(name, P, C.c_char_p, I, EP)
bind('CreateMicmState', P, P, Z, EP)
for name in ('DeleteMicm', 'DeleteState'): bind(name, None, P, EP)
bind('GetVectorSize', Z, I)
bind('GetMaximumNumberOfGridCells', Z, P)
bind('TmtMicmUsesKernels', C.c_bool, P)
bind('MicmSolve', None, P, P, D, C.POINTER(String), C.POINTER(SolverStats), EP)
bind('MicmVersion', None, C.POINTER(String))
bind('GetConditionsPointer', C.POINTER(Conditions), P, C.POINTER(Z), EP)
for name in ('GetOrderedConcentrationsPointer', 'GetOrderedRateParametersPointer'):
    bind(name, DP, P, C.POINTER(Z), EP)
for name in ('GetSpeciesOrdering', 'GetUserDefinedRateParametersOrdering'):
    bind(name, None, P, C.POINTER(Mappings), EP)
for name, typ in [('Double',D),('Int',I),('Bool',C.c_bool)]:
    bind('GetSpeciesProperty'+name, typ, P, C.c_char_p, C.c_char_p, EP)
bind('GetSpeciesPropertyString', None, P, C.c_char_p, C.c_char_p, C.POINTER(String), EP)
class RosParameters(C.Structure):
    _fields_ = [('relative_tolerance',D),('absolute_tolerances',DP),('num_absolute_tolerances',Z),('h_min',D),('h_max',D),('h_start',D),('max_number_of_steps',Z),('constraint_init_max_iterations',Z),('constraint_init_tolerance',D)]
class BeParameters(C.Structure):
    _fields_ = [('relative_tolerance',D),('absolute_tolerances',DP),('num_absolute_tolerances',Z),('max_number_of_steps',Z),('time_step_reductions',DP),('num_time_step_reductions',Z)]
for kind, struct in [('Rosenbrock',RosParameters),('BackwardEuler',BeParameters)]:
    for action in ('Get','Set'): bind(action+kind+'SolverParameters',None,P,C.POINTER(struct),EP)
    bind('Free'+kind+'SolverParameters',None,C.POINTER(struct))
class NativeState(Object):
    _kind = 'State'
    @property
    def concentrations(self):
        n=Z(); p=call('GetOrderedConcentrationsPointer',self._ptr,C.byref(n))
        return view(p,(n.value,),self._handle)
    @property
    def user_defined_rate_parameters(self):
        n=Z(); p=call('GetOrderedRateParametersPointer',self._ptr,C.byref(n))
        return view(p,(n.value,),self._handle)
    @property
    def conditions(self):
        n=Z(); p=call('GetConditionsPointer',self._ptr,C.byref(n))
        array=(Conditions*n.value).from_address(C.addressof(p.contents))
        array._tmt_owner=self._handle
        return array

def create_state(solver, number_of_grid_cells):
    if isinstance(number_of_grid_cells,bool) or not isinstance(number_of_grid_cells,int) or number_of_grid_cells < 1:
        raise ValueError('number_of_grid_cells must be a positive integer')
    return NativeState._from_handle(call('CreateMicmState',solver._ptr,number_of_grid_cells))
