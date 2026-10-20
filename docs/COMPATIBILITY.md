# Compatibility contract

TMT targets the MUSICA revision in `dependencies.lock`, for gas chemistry and
photolysis. Recompile consumers against TMT. Opaque objects and Fortran module
files from another implementation are not interchangeable. This is source/API
compatibility within the boundaries below, not a binary replacement for every
MUSICA component.

| Surface | Implementation | Verification |
| --- | --- | --- |
| Strings, errors, mappings, configuration | C11 and matching Fortran modules | Pinned Fortran utility tests; C consumers |
| TUV C models, maps, grids, profiles, radiators, host radiation | Owned handles and retained references | Direct TurboTUV comparison for v54/TS1, JSON/YAML, five solar angles; host update and lifetime tests |
| TUV Fortran | Adapted public modules | Pinned integration test unchanged |
| TUV Python | ctypes classes, NumPy views, xarray results | 36 pinned unit tests; TMT numerical, ownership, update, and concurrency tests |
| TUV v54 / vTS1 helpers | Pinned pure Python helpers | Both run; rate comparison with configured models at relative tolerance 5e-6 |
| Mechanism preparation | C process driver, Python translation, unchanged Fable generator, compiler, cache, loader | Cold preparation, reuse/invalidation, concurrency, failure isolation, moved artifacts |
| MICM C | Solver/state lifecycle, conditions, properties, ordering, parameters, statistics | Direct generic/generated Fable and analytical decay; standard/vector layouts with 1/3/33 cells |
| MICM Fortran | Adapted modules plus Fable profile selection | Pinned integration test; all three profiles against analytical decay |
| MICM Python solver/state/parameters/results | ctypes and adapted public State methods | All 46 pinned solver/state tests; method, tolerance-reset, concurrency, lifetime tests |
| Python gas mechanism authoring | Portable records, JSON/YAML serialization and parser | Round trips, included-file solver input, constant species, Taylor coefficients, upstream fixtures |
| Additional Fable methods | L2, L3r, fourth-order shifted extrapolation | Analytical decay; full TS1 adapter/direct comparison for all profiles |
| Coupling and installation | CMake, pkg-config, native libraries, modules, Python packages | Relocated installation and moved prepared mechanisms in all three languages |

[C_FUNCTIONS.md](C_FUNCTIONS.md) inventories named reference C entry points;
the installed headers give signatures. [VALIDATION.md](VALIDATION.md) records
commands, tolerances, timings, and evidence limits.

## Public surfaces and ownership

| Family | C declarations | Fortran modules | Python imports |
| --- | --- | --- | --- |
| Common | `musica/musica.h`, `musica/utils/error.h` | `musica`, `musica_util` | Errors become exceptions; `musica.utils` provides resource paths |
| Chemistry | `musica/micm.h` | `musica_micm`, `musica_state` | `MICM`, `State`, `Conditions`, `SolverType`, solver parameters/results |
| Photolysis | `musica/tuvx.h` | `musica_tuvx` and grid/profile/radiator/map modules | `TUVX`, `Grid`, `Profile`, `Radiator`, maps, `RadiationFieldUpdater` |
| Authoring | `Chemistry.configuration` contains serialized text | File/prepared-directory constructor | `musica.mechanism_configuration`: species, phase, components, twelve gas reaction classes, `Mechanism`, `Reactions`, `Version`, `parse` |
| TMT extensions | `tmt/prepare.h`, `tmt/fable.h` | `micm%configure_fable(profile,error)` | `tmt.prepare`, `tmt.configure_fable` |

C output/error records start zeroed. Strings, mappings, parameter copies, and
handles returned to the caller require their matching Delete/Free routine.
Pointer getters borrow storage; they do not transfer the allocation. A chemistry
state retains its solver and module. Map getters return owned references;
deleting a map/model does not invalidate retained objects. Python NumPy views
retain native storage after wrapper closure. Fortran pointer views require a
live state.

File and string constructors accept JSON/YAML. Chemistry source initialization
invokes preparation; prepared directories only load native code. Python mechanism
objects are serialized through that same path. The C `Chemistry` container
explicitly replaces a foreign mechanism object with serialized configuration.

Independent chemistry states may solve concurrently with immutable solver
parameters. Do not mutate parameters during a solve or share a mutable state
between simultaneous solves. Independent TUV models may run concurrently;
shared host arrays/maps require caller synchronization.

## Chemistry values, layout, and methods

Concentrations and air density are mol m-3, temperature K, pressure Pa, and
intervals seconds. Version 1 kinetics use SI units. Legacy CAMP molecular units
are converted explicitly, including parameterized reactants and the legacy
Troe/ternary conventions. Custom labels retain `PHOTO.`, `EMIS.`, `LOSS.`,
`USER.`, and `SURF.` prefixes. Surface parameters include radius and particle
number suffixes. Artifacts record normalized input and input digests.

Standard arrays are cell-major. Vector arrays use
`((cell / width) * n_variables + variable) * width + cell % width`, padded to a
whole vector. `GetVectorSize` reports 1 for standard order and 2 for vector
order. Returned strides apply within one vector group.
`GetMaximumNumberOfGridCells` reports one group for vector solvers so Fortran
callers can use affine strides. C/Python may create multiple groups using the
block indexing formula. Public concentration and rate-parameter addresses stay
stable across solves; TMT copies between public and Fable storage.

Three-stage Rosenbrock, DAE4, DAE6, and backward Euler each have standard/vector
variants. Python defaults to standard-order three-stage Rosenbrock. Explicit
L2/L3r/extrapolation profiles change controls and tolerances; select them before
creating states. Use 32-cell groups to reproduce reference grouping. State size
affects the shared error norm.

Concentration edits and parameter changes reset retained step history. Condition
changes invalidate matrix data. Rate-only updates preserve warm steps and update
custom rate constants. Resetting absolute tolerances restores mechanism defaults
on existing states. Failed integration returns the native status and partial
state/statistics; invalid arguments fail before integration. Check both error
and solver status.

## TUV values and layout

Solar zenith input is **degrees**, following the pinned native interface
(`src/tuvx/interface.F90:347`). Its Python/Fortran documentation incorrectly
labels this input radians; TMT corrects the label. The Earth-Sun argument is the
backend's multiplicative radiation scaling factor. Raw radiation components
are dimensionless; dataset attributes use `units="1"`. Photolysis rates are s-1.

C radiation arrays use component-fast ordering: direct, upwelling diffuse,
downwelling diffuse, then vertical level, then wavelength. Python exposes
`(wavelength, vertical_level, component)`. Rate arrays use vertical-level-fast
ordering; Python exposes `(reaction, vertical_level)` and analogous heating/dose
dimensions. Returned datasets are independent snapshots.

Configured grids have read-only setters. Configured profiles/radiators follow
the reference's host-access restrictions. Host objects synchronize before each
run, including direct NumPy edits. Host radiation accepts optional irradiance
arrays; omission produces zero irradiance.

## Explicit boundaries

- Gas chemistry has one phase named `gas`. Constant concentrations, constant
  mixing ratios, and third bodies are supported. Aerosol systems, emissions
  inventories, and other component models need separate adapters; their sections
  and external objects are rejected. Emission and loss reaction types work.
- The pinned lambda registry belongs to its browser interface, without a public
  native registration contract used by these wrappers. `LAMBDA` configurations
  are rejected. No Python callback runs during integration. External-model
  algebraic constraints require separate adapters.
- CUDA/other accelerators are unavailable and return errors. Fable limits names
  to less than 128 UTF-8 bytes, species properties to 16, and Taylor coefficients
  to 16. Unsupported semantic fields fail; `__` metadata is retained in the
  normalized configuration.
- Portable authoring supports gas records and keyword constructors. Its `parse`
  helper reads inline version 1 records. Solver preparation additionally supports
  CAMP includes and version 1 section file lists. Foreign-object introspection
  and unrelated component authoring are unavailable.
- TurboTUV represents radiators with optical depth, single-scattering albedo,
  and asymmetry. Setting independent higher Legendre moments is rejected;
  derived higher moments follow powers of asymmetry.
- Source cache hits still run Python for normalization and the compiler with
  `--version` for identity checks. Prepared deployment starts neither tool.
  Failures retain isolated diagnostics; incompatible artifacts fail rather
  than silently falling back to generic kernels.
- macOS arm64 GNU and Apple Clang builds were exercised. POSIX/Linux code is
  present but was not executed in this validation. Windows is unsupported.

Fortran adaptations include error-header paths, the `size_t` state-creation
binding, double-precision final time, corrected solar-angle documentation, and
additional method constants/profile selection. Python serialization retains
relative tolerance and emits canonical reaction-component field names.
