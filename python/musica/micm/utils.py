import numpy as np
from .._native import mapping

def _state(state):
    return state.get_internal_state() if hasattr(state, 'get_internal_state') else state

def species_ordering(state):
    return mapping('GetSpeciesOrdering', _state(state)._ptr)

def user_defined_rate_parameters_ordering(state):
    return mapping('GetUserDefinedRateParametersOrdering', _state(state)._ptr)

def is_scalar_number(x):
    return isinstance(x,(int,float,np.number)) and not isinstance(x,bool)
