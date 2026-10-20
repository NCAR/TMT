"""TMT mechanism preparation using the same C driver as native hosts."""
import ctypes as C
import os
from musica._native import bind, call, String, Error, lib
class PrepareOptions(C.Structure):
    _fields_=[(name,C.c_char_p) for name in ('python','compiler','tools_directory','fable_directory','include_directory','runtime_library','cache_directory')]+[('width',C.c_int),('mode',C.c_char_p),('ordering',C.c_char_p)]
bind('TmtPrepareMechanism',None,C.c_char_p,C.POINTER(PrepareOptions),C.POINTER(String),C.POINTER(Error))
def prepare(config_path, *, width=2, mode='exact', ordering='markowitz', **paths):
    options=PrepareOptions(); owners=[]
    for key,value in paths.items():
        if key not in dict(PrepareOptions._fields_) or key in ('width','mode','ordering'): raise TypeError('unknown preparation option '+key)
        encoded=os.fsencode(value); owners.append(encoded); setattr(options,key,encoded)
    options.width=width; options.mode=mode.encode(); options.ordering=ordering.encode()
    result=String()
    call('TmtPrepareMechanism',os.fsencode(config_path),C.byref(options),C.byref(result))
    try: return result.text()
    finally: lib.DeleteString(C.byref(result))

bind('TmtConfigureFable',None,C.c_void_p,C.c_int,C.POINTER(Error))
def configure_fable(solver, profile='extrapolation'):
    """Select Fable L2, L3r, or extrapolation before creating any states."""
    profiles={'l2':1,'l3r':2,'extrapolation':3}
    if profile not in profiles: raise ValueError('profile must be l2, l3r, or extrapolation')
    call('TmtConfigureFable',solver._ptr,profiles[profile])
