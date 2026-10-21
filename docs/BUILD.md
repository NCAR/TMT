# Build and use TMT

Requirements: CMake 3.20+, a C11 compiler and matching OpenMP runtime, Python
3.10+ with PyYAML, and optionally a Fortran compiler. Python wrappers also need
NumPy and xarray. The three sibling Git repositories must contain the commits
in `dependencies.lock`. Their branches and working trees are not modified.

```sh
python3 -m venv .venv
.venv/bin/python -m pip install -e . pytest
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release \
  -DPython3_EXECUTABLE="$PWD/.venv/bin/python"
cmake --build build -j 8
ctest --test-dir build --output-on-failure
cmake --install build --prefix "$PWD/build/install"
```

On this Mac the GNU build uses `-DCMAKE_C_COMPILER=/opt/homebrew/bin/gcc-15`
and `-DCMAKE_Fortran_COMPILER=/opt/homebrew/bin/gfortran-15`. For Apple Clang,
select a matching SDK with `CMAKE_OSX_SYSROOT` and set the OpenMP include,
flags, and library explicitly if CMake cannot locate Homebrew libomp. See
`VALIDATION.md` for the verified commands. `TMT_BUILD_FORTRAN=OFF` builds only C
and Python support. `TMT_SANITIZE=ON` enables address and undefined-behavior
instrumentation in the libraries, generated mechanisms, and C tests.

`TMT_REPOSITORIES` overrides the sibling repository parent. `TMT_DEPENDENCY_DIR`
selects extracted snapshots; pre-extracted directories with the matching
`.tmt-source.json` marker can be supplied for offline builds. Nothing is fetched
from the network by configuration. Keep snapshot contents immutable.

## Native consumers

The installed CMake package provides `TMT::tmt`, `TMT::tmt_fable`, and optional
`TMT::tmt_fortran`, plus `musica::musica` and `musica::musica-fortran` aliases.
Use `find_package(TMT REQUIRED)`. Installed pkg-config modules are `tmt` and
`tmt-fortran`. The examples directory contains standalone CMake consumers.
Fortran modules must be rebuilt with the consumer's compiler.

```sh
cmake -S build/install/share/tmt/examples -B build/consumer \
  -DCMAKE_PREFIX_PATH="$PWD/build/install"
cmake --build build/consumer
TMT_CACHE="$PWD/build/cache" build/consumer/chemistry_c \
  build/install/share/tmt/examples/decay.json
```

C callers initialize output/error structs to zero and release returned values
with the matching Delete/Free function. Model/object/state handles own native
references. See `COMPATIBILITY.md` for layouts and thread ownership.

## Python and preparation

Use `PYTHONPATH=python` in the source tree, or install with pip alongside the
native installation. CMake also installs the packages under
`lib/python<version>/site-packages`; add that location to `PYTHONPATH` when using
a custom prefix. `TMT_LIBRARY` can identify `libtmt.so` or `libtmt.dylib` explicitly.
`TMT_CONFIGS` can identify the installed example configuration directory.

```sh
PYTHONPATH=python .venv/bin/python -m tmt tests/configs/decay.json \
  --width 1 --cache-directory "$PWD/build/cache"
PYTHONPATH=python TMT_CACHE="$PWD/build/cache" .venv/bin/python examples/coupled.py
```

The preparation command prints an artifact **directory**. Pass that directory
as `MICM(config_path=...)`, to `CreateMicm`, or to the Fortran `micm_t`
constructor. Prepared loading starts no subprocess. Source input starts Python
for normalization/cache validation and the compiler with `--version`. A cache
miss also starts Fable's generator and compiles the generated source.
Diagnostics and input manifests remain beside the artifact.
By default, width 1 is required by standard-order solvers and width 2 by
vector-order solvers. The Tier 1 opt-in below is the exception.

Native `TmtPrepareOptions` and Python `tmt.prepare` allow explicit tool/source/
runtime/cache paths. Environment equivalents are `TMT_PYTHON`, `TMT_CC`,
`TMT_TOOLS`, `TMT_FABLE`, `TMT_INCLUDE`, `TMT_FABLE_LIBRARY`, and `TMT_CACHE`.
Compiler overrides are executable paths, without embedded flags. `TMT_SYSROOT`
overrides a configured SDK. No shell interprets these paths. The default cache
is `$HOME/.cache/tmt/mechanisms`; set `TMT_CACHE` for project-local outputs.

### Tier 1 groups behind a standard-order interface

Set `TMT_MICM_TIER1=1` before constructing a `RosenbrockStandardOrder` solver
to select fast kernels, minimum-fill ordering and width 2. The adapter divides
each state into independent groups of at most 32 cells, packing and unpacking
its stable row-major public concentration and custom-parameter arrays. Hosts
using ordinary cell/species strides need no source changes. Other solver types
retain their existing behavior. Without this environment setting, defaults
remain unchanged.

The method remains three-stage Rosenbrock with the MICM step controller and
the host's relative and per-species absolute tolerances. Each group controls
its own steps and error norm, so concentrations need not be bitwise identical
to a single large state. Validate the application's accuracy requirements.
Prepared artifacts used with this setting must have width 2; prepare them with
fast mode and minimum-fill ordering to reproduce this configuration. The
`configure_fable` profiles cannot be combined with this opt-in.

Returned counters sum work across the internal groups. A successful call
publishes all groups and reports the requested final time. If any group fails,
public concentrations remain unchanged and final time is zero; a subsequent
call restarts all groups from that public state.

For per-call timing, set `TMT_MICM_TRACE_DIR` to an existing writable directory.
Each state that solves writes a separate CSV with MPI rank (from `PMI_RANK`),
cell/group counts, wall seconds and summed solver counters. Timings include
validation, packing, solving and unpacking; they exclude writing the trace
itself. Trace output is optional and is only available with this Tier 1 path.

`from tmt import configure_fable; configure_fable(solver, 'extrapolation')`
selects the recommended reference method **before creating states**. `l2` and
`l3r` select the other exposed reference operating points. The C equivalent is
`TmtConfigureFable` in `tmt/fable.h`; Fortran uses
`call solver%configure_fable(FableExtrapolation,error)`. Use groups of 32 cells to reproduce the
reference's grouping; state size controls the shared error norm. Defaults keep
the compatible Rosenbrock method. Profile choice changes tolerances as well as
the method, so inspect `get_solver_parameters()` when recording a simulation.

Installed native libraries locate each other through relative runtime paths.
Generated modules bind to the matching Fable runtime already loaded by TMT.
Move an artifact with its manifest, and deploy it with a matching TMT build on
the same platform/architecture. Build identity mismatches are errors.
