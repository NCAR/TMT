import ctypes as C
from dataclasses import dataclass
from enum import IntEnum
class SolverState(IntEnum):
    NotYetCalled = 0
    Running = 1
    Converged = 2
    ConvergenceExceededMaxSteps = 3
    StepSizeTooSmall = 4
    RepeatedlySingularMatrix = 5
    NaNDetected = 6
    InfDetected = 7
    AcceptingUnconvergedIntegration = 8
    ConstraintInitializationFailed = 9
class SolverStats(C.Structure):
    _fields_ = [(key, C.c_int64) for key in ("function_calls", "jacobian_updates", "number_of_steps", "accepted", "rejected", "decompositions", "solves", "constraint_init_iterations")] + [("final_time", C.c_double)]
    def __repr__(self):
        return 'SolverStats(' + ', '.join(f'{k}={getattr(self,k)}' for k,_ in self._fields_) + ')'
@dataclass
class SolverResult:
    state: SolverState = SolverState.NotYetCalled
    stats: SolverStats = None
    def __post_init__(self):
        if self.stats is None: self.stats = SolverStats()
