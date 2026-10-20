"""MUSICA public solver methods implemented through the TMT C API."""
import ctypes as C
import json
import os
from .._native import Object, Handle, lib, call, String, D
from . import _bindings as native
from .solver import SolverType
from .solver_parameters import RosenbrockSolverParameters, BackwardEulerSolverParameters
from .solver_result import SolverResult, SolverState, SolverStats
from .state import State


def vector_size(solver_type):
    result = lib.GetVectorSize(int(solver_type))
    if not result: raise ValueError('unsupported solver type')
    return result


class MICM(Object):
    _kind='Micm'
    def __init__(self, config_path=None, mechanism=None, solver_type=None, solver_parameters=None, external_models=None):
        self._solver_type=SolverType.rosenbrock_standard_order if solver_type is None else SolverType(solver_type)
        self._vector_size=vector_size(self._solver_type)
        if config_path is None and mechanism is None:
            raise ValueError('Either config_path or mechanism must be provided.')
        if config_path is not None and mechanism is not None:
            raise ValueError('Only one of config_path or mechanism must be provided.')
        if external_models:
            raise NotImplementedError('External models require a TMT native adapter; foreign backend objects cannot be attached.')
        if config_path is not None:
            pointer=call('CreateMicm',os.fsencode(config_path),int(self._solver_type))
        else:
            pointer=call('CreateMicmFromConfigString',json.dumps(mechanism.serialize(),allow_nan=False).encode(),int(self._solver_type))
        self._handle=Handle(pointer,'DeleteMicm')
        if solver_parameters is not None: self.set_solver_parameters(solver_parameters)
    def solver_type(self): return self._solver_type
    @property
    def uses_kernels(self): return lib.TmtMicmUsesKernels(self._ptr)
    def create_state(self, number_of_grid_cells=1):
        return State(self,number_of_grid_cells,self._vector_size)
    def solve(self,state,time_step):
        if not isinstance(state,State): raise TypeError('state must be an instance of State.')
        if not isinstance(time_step,(int,float)): raise TypeError('time_step must be an int or float.')
        text=String(); stats=SolverStats()
        call('MicmSolve',self._ptr,state.get_internal_state()._ptr,time_step,C.byref(text),C.byref(stats))
        try:
            name=''.join(c for c in text.text() if c.isalnum()).lower()
            value=next((x for x in SolverState if x.name.lower()==name),None)
            if value is None: raise RuntimeError('unknown solver result: '+text.text())
            return SolverResult(value,stats)
        finally: lib.DeleteString(C.byref(text))
    def set_solver_parameters(self,params):
        if isinstance(params,RosenbrockSolverParameters): kind,typ='Rosenbrock',native.RosParameters
        elif isinstance(params,BackwardEulerSolverParameters): kind,typ='BackwardEuler',native.BeParameters
        else: raise TypeError('params must be RosenbrockSolverParameters or BackwardEulerSolverParameters')
        record=typ(); owners=[]
        for key,ctype in typ._fields_:
            if key.startswith('num_'): continue
            value=getattr(params,key)
            if key in ('absolute_tolerances','time_step_reductions'):
                value=[] if value is None else list(value)
                array=(D*len(value))(*value); owners.append(array)
                setattr(record,key,array); setattr(record,'num_'+key,len(value))
            else:
                if ctype is C.c_size_t and (not isinstance(value,int) or value<0): raise ValueError(key+' must be nonnegative integer')
                setattr(record,key,value)
        call('Set'+kind+'SolverParameters',self._ptr,C.byref(record))
    def get_solver_parameters(self):
        be=self._solver_type in (SolverType.backward_euler,SolverType.backward_euler_standard_order)
        kind,typ,public=('BackwardEuler',native.BeParameters,BackwardEulerSolverParameters) if be else ('Rosenbrock',native.RosParameters,RosenbrockSolverParameters)
        record=typ(); call('Get'+kind+'SolverParameters',self._ptr,C.byref(record))
        try:
            values={}
            for key,_ in typ._fields_:
                if key.startswith('num_'): continue
                value=getattr(record,key)
                if key in ('absolute_tolerances','time_step_reductions'):
                    value=list(value[:getattr(record,'num_'+key)]) if value else None
                values[key]=value
            return public(**values)
        finally: getattr(lib,'Free'+kind+'SolverParameters')(C.byref(record))
    def get_species_property(self,species_name,property_name,property_type=float):
        kinds={float:'Double',int:'Int',bool:'Bool',str:'String'}
        if property_type not in kinds: raise TypeError('property_type must be float, int, bool, or str')
        args=(self._ptr,species_name.encode(),property_name.encode())
        if property_type is not str: return call('GetSpeciesProperty'+kinds[property_type],*args)
        value=String(); call('GetSpeciesPropertyString',*args,C.byref(value))
        try: return value.text()
        finally: lib.DeleteString(C.byref(value))
