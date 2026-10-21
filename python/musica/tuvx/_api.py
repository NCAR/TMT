"""MUSICA photolysis interfaces backed by the TMT C library."""
import ctypes as C
import os
import numpy as np
import xarray as xr
from .._native import (P, Z, D, DP, Error, String, Mappings, Object, Handle,
                       bind, call, encoded, array, ptr, view, mapping, lib)

EP = C.POINTER(Error)
for kind in ("Grid", "Profile", "Radiator", "GridMap", "ProfileMap", "RadiatorMap", "Tuvx", "RadiationFieldUpdater"):
    bind("Delete" + kind, None, P, EP)
for kind in ("Grid", "Profile", "Radiator"):
    bind("Get" + kind + "Name", C.c_char_p, P, EP)
for kind in ("Grid", "Profile"):
    bind("Get" + kind + "Units", C.c_char_p, P, EP)
    bind("Get" + kind + "NumberOfSections", Z, P, EP)
bind("CreateGrid", P, C.c_char_p, C.c_char_p, Z, EP)
bind("CreateProfile", P, C.c_char_p, C.c_char_p, P, EP)
bind("CreateRadiator", P, C.c_char_p, P, P, EP)
for kind, fields in (("Grid", ("Edges", "Midpoints")),
                     ("Profile", ("EdgeValues", "MidpointValues", "LayerDensities"))):
    for field in fields:
        bind("Set" + kind + field, None, P, DP, Z, EP)
        bind("Get" + kind + field + "Pointer", DP, P, EP)
for field in ("OpticalDepths", "SingleScatteringAlbedos"):
    bind("SetRadiator" + field, None, P, DP, Z, Z, EP)
    bind("GetRadiator" + field + "Pointer", DP, P, EP)
bind("SetRadiatorAsymmetryFactors", None, P, DP, Z, Z, Z, EP)
bind("GetRadiatorAsymmetryFactorsPointer", DP, P, EP)
for axis in ("Height", "Wavelength"):
    bind("GetRadiatorNumberOf" + axis + "Sections", Z, P, EP)
bind("SetProfileExoLayerDensity", None, P, D, EP)
bind("CalculateProfileExoLayerDensity", None, P, D, EP)
bind("GetProfileExoLayerDensity", D, P, EP)
for kind in ("Grid", "Profile", "Radiator"):
    bind("Create" + kind + "Map", P, EP)
    bind("Add" + kind, None, P, P, EP)
    args = (P, C.c_char_p) if kind == "Radiator" else (P, C.c_char_p, C.c_char_p)
    bind("Get" + kind, P, *args, EP)
    bind("Remove" + kind, None, *args, EP)
    bind("Get" + kind + "ByIndex", P, P, Z, EP)
    bind("Remove" + kind + "ByIndex", None, P, Z, EP)
    bind("GetNumberOf" + kind + "s", Z, P, EP)
    bind("Get" + kind + "Map", P, P, EP)
bind("CreateTuvx", P, C.c_char_p, P, P, P, EP)
bind("CreateTuvxFromConfigString", P, C.c_char_p, P, P, P, EP)
bind("TmtTuvxDimensions", None, P, *([C.POINTER(Z)] * 5), EP)
bind("RunTuvx", None, P, D, D, *([DP] * 5), EP)
for kind in ("PhotolysisRateConstants", "HeatingRates", "DoseRates"):
    bind("Get" + kind + "Ordering", None, P, C.POINTER(Mappings), EP)
bind("GetRadiationFieldUpdater", P, P, EP)
bind("UpdateRadiationField", None, P, *([DP] * 6), Z, Z, EP)
bind("TuvxVersion", None, C.POINTER(String))


def _array_property(kind, field, shape):
    def get(self):
        return view(call("Get" + kind + field + "Pointer", self._ptr), shape(self), self._handle)
    def set_value(self, value):
        try:
            values = array(value, shape(self))
        except ValueError as exc:
            if kind == "Grid":
                size = "num_sections + 1" if field == "Edges" else "num_sections"
                raise ValueError(f"Array size must be {size}") from exc
            if kind == "Profile":
                size = "num_sections + 1" if field == "EdgeValues" else "num_sections"
                raise ValueError(f"Array size must be {size}") from exc
            raise ValueError(f"Array shape must be {shape(self)}") from exc
        dims = (values.size,) if kind != "Radiator" else (values.shape[1], values.shape[0])
        if field == "AsymmetryFactors":
            dims += (1,)
        call("Set" + kind + field, self._ptr, ptr(values), *dims)
    return property(get, set_value)


class Grid(Object):
    _kind = "Grid"

    def __init__(self, *, name, units, num_sections=None, edges=None, midpoints=None):
        if num_sections is None:
            if edges is not None:
                num_sections = len(edges) - 1
            elif midpoints is not None:
                num_sections = len(midpoints)
            else:
                raise ValueError("At least one of num_sections, edges, or midpoints must be provided")
        if not isinstance(num_sections, (int, np.integer)) or num_sections < 0:
            raise ValueError("num_sections must be a nonnegative integer")
        self._handle = Handle(call("CreateGrid", encoded(name), encoded(units), num_sections), "DeleteGrid")
        if edges is not None:
            self.edges = edges
        if midpoints is not None:
            self.midpoints = midpoints

    name = property(lambda s: call("GetGridName", s._ptr).decode())
    units = property(lambda s: call("GetGridUnits", s._ptr).decode())
    num_sections = property(lambda s: call("GetGridNumberOfSections", s._ptr))
    edges = _array_property("Grid", "Edges", lambda s: (s.num_sections + 1,))
    midpoints = _array_property("Grid", "Midpoints", lambda s: (s.num_sections,))

    def __len__(self):
        return self.num_sections

    def __contains__(self, value):
        return self.edges[0] <= value <= self.edges[-1]

    def __bool__(self):
        return bool(self.name) and self.num_sections > 0

    def __str__(self):
        return f"Grid(name={self.name}, units={self.units}, num_sections={self.num_sections})"

    def __repr__(self):
        return str(self)[:-1] + f", edges={self.edges}, midpoints={self.midpoints})"

    def __eq__(self, other):
        if not isinstance(other, Grid):
            return NotImplemented
        return self.name == other.name and self.units == other.units and np.array_equal(self.edges, other.edges) and np.array_equal(self.midpoints, other.midpoints)


class Profile(Object):
    _kind = "Profile"

    def __init__(self, *, name, units, grid, edge_values=None, midpoint_values=None,
                 layer_densities=None, calculate_layer_densities=False, exo_layer_density=0.0):
        self._handle = Handle(call("CreateProfile", encoded(name), encoded(units), grid._ptr), "DeleteProfile")
        if edge_values is not None:
            self.edge_values = edge_values
        if midpoint_values is not None:
            self.midpoint_values = midpoint_values
        if layer_densities is not None:
            self.layer_densities = layer_densities
        elif calculate_layer_densities:
            self.calculate_layer_densities(grid)
        if exo_layer_density is not None:
            self.exo_layer_density = exo_layer_density

    name = property(lambda s: call("GetProfileName", s._ptr).decode())
    units = property(lambda s: call("GetProfileUnits", s._ptr).decode())
    number_of_sections = property(lambda s: call("GetProfileNumberOfSections", s._ptr))
    edge_values = _array_property("Profile", "EdgeValues", lambda s: (s.number_of_sections + 1,))
    midpoint_values = _array_property("Profile", "MidpointValues", lambda s: (s.number_of_sections,))
    layer_densities = _array_property("Profile", "LayerDensities", lambda s: (s.number_of_sections,))
    exo_layer_density = property(lambda s: call("GetProfileExoLayerDensity", s._ptr),
                                 lambda s, v: call("SetProfileExoLayerDensity", s._ptr, v))

    def calculate_exo_layer_density(self, scale_height):
        call("CalculateProfileExoLayerDensity", self._ptr, scale_height)

    def calculate_layer_densities(self, grid, conv=None):
        if conv is None:
            conv = 1e5 if (grid.name, grid.units, self.units) == ("height", "km", "molecule cm-3") else 1
        self.layer_densities = self.midpoint_values * np.diff(grid.edges) * conv

    def __len__(self):
        return self.number_of_sections

    def __bool__(self):
        return bool(self.name) and self.number_of_sections > 0

    def __str__(self):
        return f"Profile(name={self.name}, units={self.units}, number_of_sections={self.number_of_sections})"

    def __repr__(self):
        return str(self)[:-1] + f", edge_values={self.edge_values}, midpoint_values={self.midpoint_values}, layer_densities={self.layer_densities}, exo_layer_density={self.exo_layer_density})"

    def __eq__(self, other):
        if not isinstance(other, Profile):
            return NotImplemented
        return self.name == other.name and self.units == other.units and self.exo_layer_density == other.exo_layer_density and all(np.array_equal(getattr(self, attr), getattr(other, attr)) for attr in ("edge_values", "midpoint_values", "layer_densities"))


class Radiator(Object):
    _kind = "Radiator"

    def __init__(self, *, name, height_grid, wavelength_grid, optical_depths=None,
                 single_scattering_albedos=None, asymmetry_factors=None):
        self._handle = Handle(call("CreateRadiator", encoded(name), height_grid._ptr, wavelength_grid._ptr), "DeleteRadiator")
        for attr, value in (("optical_depths", optical_depths), ("single_scattering_albedos", single_scattering_albedos), ("asymmetry_factors", asymmetry_factors)):
            if value is not None:
                setattr(self, attr, value)

    name = property(lambda s: call("GetRadiatorName", s._ptr).decode())
    number_of_height_sections = property(lambda s: call("GetRadiatorNumberOfHeightSections", s._ptr))
    number_of_wavelength_sections = property(lambda s: call("GetRadiatorNumberOfWavelengthSections", s._ptr))
    _shape = lambda s: (s.number_of_wavelength_sections, s.number_of_height_sections)
    optical_depths = _array_property("Radiator", "OpticalDepths", _shape)
    single_scattering_albedos = _array_property("Radiator", "SingleScatteringAlbedos", _shape)
    asymmetry_factors = _array_property("Radiator", "AsymmetryFactors", _shape)

    def __bool__(self):
        return bool(self.name) and self.number_of_height_sections > 0 and self.number_of_wavelength_sections > 0

    def __str__(self):
        return f"Radiator(name={self.name}, num_height_sections={self.number_of_height_sections}, num_wavelength_sections={self.number_of_wavelength_sections})"

    def __repr__(self):
        return str(self)[:-1] + f", optical_depths={self.optical_depths}, single_scattering_albedos={self.single_scattering_albedos}, asymmetry_factors={self.asymmetry_factors})"

    def __eq__(self, other):
        if not isinstance(other, Radiator):
            return NotImplemented
        return self.name == other.name and all(np.array_equal(getattr(self, attr), getattr(other, attr)) for attr in ("optical_depths", "single_scattering_albedos", "asymmetry_factors"))


class _Map(Object):
    def __init__(self):
        self._handle = Handle(call("Create" + self._kind), "Delete" + self._kind)

    def _add(self, value):
        if not isinstance(value, self._item):
            raise TypeError(f"Expected {self._item.__name__}")
        call("Add" + self._item._kind, self._ptr, value._ptr)

    def _get(self, *key):
        return self._item._from_handle(call("Get" + self._item._kind, self._ptr, *(encoded(k) for k in key)))

    def _get_index(self, index):
        if index < 0:
            raise IndexError(index)
        return self._item._from_handle(call("Get" + self._item._kind + "ByIndex", self._ptr, index))

    def _remove_index(self, index):
        if index < 0:
            raise IndexError(index)
        call("Remove" + self._item._kind + "ByIndex", self._ptr, index)

    def __len__(self):
        return call("GetNumberOf" + self._item._kind + "s", self._ptr)

    def _key(self, item):
        return item.name if self._item is Radiator else (item.name, item.units)

    def __iter__(self):
        for i in range(len(self)):
            yield self._key(self._get_index(i))

    def __getitem__(self, key):
        if self._item is Radiator:
            if not isinstance(key, str):
                raise TypeError("Radiator key must be a name")
            args = (key,)
        else:
            if not isinstance(key, tuple) or len(key) != 2 or not all(isinstance(x, str) for x in key):
                raise TypeError("Key must be a (name, units) tuple")
            args = key
        try:
            return self._get(*args)
        except ValueError as exc:
            raise KeyError(key) from exc

    def __setitem__(self, key, item):
        if self._item is Radiator:
            if not isinstance(key, str):
                raise TypeError("Radiator key must be a name")
        elif not isinstance(key, tuple) or len(key) != 2 or not all(isinstance(x, str) for x in key):
            raise TypeError("Key must be a (name, units) tuple")
        if not isinstance(item, self._item):
            raise TypeError(f"Expected {self._item.__name__}")
        if key != self._key(item):
            raise ValueError("Map key does not match object name/units")
        self._add(item)

    def __contains__(self, key):
        try:
            self[key]
            return True
        except (KeyError, TypeError):
            return False

    def __delitem__(self, key):
        for i, existing in enumerate(self):
            if existing == key:
                self._remove_index(i)
                return
        raise KeyError(key)

    def keys(self):
        return list(self)

    def values(self):
        return [self._get_index(i) for i in range(len(self))]

    def items(self):
        return [(self._key(v), v) for v in self.values()]

    def clear(self):
        while len(self):
            self._remove_index(len(self) - 1)

    def __str__(self):
        return f"{self._kind}(num_{self._item._kind.lower()}s={len(self)})"

    def __repr__(self):
        return f"{self._kind}({self._item._kind.lower()}s={list(self)})"


class GridMap(_Map):
    _kind, _item = "GridMap", Grid
    add_grid, get_grid = _Map._add, _Map._get
    get_grid_by_index, remove_grid_by_index = _Map._get_index, _Map._remove_index
    get_number_of_grids = _Map.__len__


class ProfileMap(_Map):
    _kind, _item = "ProfileMap", Profile
    add_profile, get_profile = _Map._add, _Map._get
    get_profile_by_index, remove_profile_by_index = _Map._get_index, _Map._remove_index
    get_number_of_profiles = _Map.__len__


class RadiatorMap(_Map):
    _kind, _item = "RadiatorMap", Radiator
    add_radiator, get_radiator = _Map._add, _Map._get
    get_radiator_by_index, remove_radiator_by_index = _Map._get_index, _Map._remove_index
    get_number_of_radiators = _Map.__len__


class RadiationFieldUpdater(Object):
    _kind = "RadiationFieldUpdater"

    def update(self, direct_actinic_flux, upward_actinic_flux, downward_actinic_flux,
               direct_irradiance=None, upward_irradiance=None, downward_irradiance=None):
        values = [direct_actinic_flux, upward_actinic_flux, downward_actinic_flux,
                  direct_irradiance, upward_irradiance, downward_irradiance]
        if any(v is None for v in values[:3]):
            raise ValueError("All three actinic flux components are required")
        arrays = [array(v, self._shape) if v is not None else None for v in values]
        call("UpdateRadiationField", self._ptr, *(ptr(v) for v in arrays), self._shape[1], self._shape[0])


class TUVX(Object):
    _kind = "Tuvx"

    def __init__(self, grid_map, profile_map, radiator_map, config_path=None, config_string=None):
        if (config_path is None) == (config_string is None):
            raise ValueError("Exactly one of config_path and config_string is required")
        maps = (grid_map._ptr, profile_map._ptr, radiator_map._ptr)
        if config_path is not None:
            if not os.path.isfile(config_path):
                raise FileNotFoundError(config_path)
            handle = call("CreateTuvx", encoded(config_path), *maps)
        else:
            handle = call("CreateTuvxFromConfigString", encoded(config_string), *maps)
        self._handle = Handle(handle, "DeleteTuvx")
        shape = [Z() for _ in range(5)]
        call("TmtTuvxDimensions", self._ptr, *(C.byref(n) for n in shape))
        self._shape = tuple(n.value for n in shape)

    photolysis_rate_names = property(lambda s: mapping("GetPhotolysisRateConstantsOrdering", s._ptr))
    heating_rate_names = property(lambda s: mapping("GetHeatingRatesOrdering", s._ptr))
    dose_rate_names = property(lambda s: mapping("GetDoseRatesOrdering", s._ptr))

    def get_grid_map(self):
        return GridMap._from_handle(call("GetGridMap", self._ptr))

    def get_profile_map(self):
        return ProfileMap._from_handle(call("GetProfileMap", self._ptr))

    def get_radiator_map(self):
        return RadiatorMap._from_handle(call("GetRadiatorMap", self._ptr))

    def get_radiation_field_updater(self):
        pointer = call("GetRadiationFieldUpdater", self._ptr)
        if not pointer:
            return None
        updater = RadiationFieldUpdater._from_handle(pointer)
        updater._shape = (self._shape[1], self._shape[0])
        return updater

    def run(self, sza, earth_sun_distance):
        """Run with zenith angle in radians and multiplicative Earth-Sun scaling."""
        nz, nw, np_, nh, nd = self._shape
        photo, heat, dose = (np.empty((n, nz)) for n in (np_, nh, nd))
        flux, irr = np.empty((nw, nz, 3)), np.empty((nw, nz, 3))
        call("RunTuvx", self._ptr, sza, earth_sun_distance, *(ptr(v) for v in (photo, heat, dose, flux, irr)))
        grids = self.get_grid_map()
        height, wave = grids["height", "km"], grids["wavelength", "nm"]
        return xr.Dataset(
            data_vars={
                "photolysis_rate_constants": (("reaction", "vertical_edge"), photo, {"units": "s^-1"}),
                "heating_rates": (("heating_rate", "vertical_edge"), heat, {"units": "K s^-1"}),
                "dose_rates": (("dose_rate", "vertical_edge"), dose, {"units": "W m^-2"}),
                "actinic_flux": (("wavelength_midpoint", "vertical_edge", "component"), flux, {"units": "1"}),
                "spectral_irradiance": (("wavelength_midpoint", "vertical_edge", "component"), irr, {"units": "1"}),
                "solar_zenith_angle": ((), sza, {"units": "radians"}),
                "earth_sun_distance": ((), earth_sun_distance, {"units": "1", "description": "Multiplicative Earth-Sun radiation scaling factor"}),
            }, coords={
                "reaction": list(self.photolysis_rate_names), "heating_rate": list(self.heating_rate_names),
                "dose_rate": list(self.dose_rate_names),
                "vertical_midpoint": ("vertical_midpoint", height.midpoints.copy(), {"units": "km"}),
                "vertical_edge": ("vertical_edge", height.edges.copy(), {"units": "km"}),
                "wavelength_midpoint": ("wavelength_midpoint", wave.midpoints.copy(), {"units": "nm"}),
                "wavelength_edge": ("wavelength_edge", wave.edges.copy(), {"units": "nm"}),
                "component": ["direct", "upwelling", "downwelling"],
            })

    def get_photolysis_rate_constant(self, reaction_name, photolysis_rates):
        return photolysis_rates[self.photolysis_rate_names[reaction_name], :]

    def get_heating_rate(self, reaction_name, heating_rates):
        return heating_rates[self.heating_rate_names[reaction_name], :]

    def get_dose_rate(self, dose_name, dose_rates):
        return dose_rates[self.dose_rate_names[dose_name], :]


def get_tuvx_version():
    value = String()
    lib.TuvxVersion(C.byref(value))
    try:
        return value.text()
    finally:
        lib.DeleteString(C.byref(value))
