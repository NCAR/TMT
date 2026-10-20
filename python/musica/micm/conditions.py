import ctypes as C
from ..constants import GAS_CONSTANT
class Conditions(C.Structure):
    _fields_ = [("temperature", C.c_double), ("pressure", C.c_double), ("air_density", C.c_double)]
    def __init__(self, temperature=None, pressure=None, air_density=None):
        super().__init__()
        if temperature is not None: self.temperature = temperature
        if pressure is not None: self.pressure = pressure
        if air_density is not None: self.air_density = air_density
        elif temperature is not None and pressure is not None:
            self.air_density = pressure / (GAS_CONSTANT * temperature)
