from .utils import species_ordering, user_defined_rate_parameters_ordering
from .state import State
from .solver_result import SolverState, SolverStats, SolverResult
from .solver import SolverType
from .solver_parameters import RosenbrockSolverParameters, BackwardEulerSolverParameters
from .micm import MICM
from .conditions import Conditions

def _version():
    import ctypes
    from .._native import lib, String
    value = String()
    lib.MicmVersion(ctypes.byref(value))
    try:
        return value.text()
    finally:
        lib.DeleteString(ctypes.byref(value))

__version__ = _version()
del _version
