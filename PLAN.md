# TMT implementation plan

Initial plan: October 19, 2026. Implementation status is tracked below.

## Objective and agreed architecture

Provide MUSICA-compatible C, Fortran, and Python interfaces for the Fable
reference chemistry solver and TurboTUV. All integration work belongs in TMT;
the sibling repositories remain unchanged.

- C11 implements the native API, adapters, process execution, and library loading.
- Fortran uses `ISO_C_BINDING` and compatible public modules and types.
- Python uses `ctypes` and compatible public classes, including mechanism authoring.
- Preserve Fable's Python generator; start it and the C compiler as child processes.
- Support prepared mechanisms and preparation during initialization, with caching.
- Execute the solver natively; no embedded interpreter or preparation in the solve loop.
- Implement TurboTUV first, then Fable, then verify coupled operation and installation.

```text
C callers ---------------------------+
Fortran through ISO_C_BINDING --------+--> TMT C API --> Fable / TurboTUV
Python through ctypes ---------------+

Mechanism input --> translator --> Fable generator --> C compiler
  --> compiled mechanism and manifest --> TMT loader --> native C solves
```

## Source baseline and ownership

| Component | Revision | Role |
| --- | --- | --- |
| MUSICA | `8f0d981e2ef31fad90870ffd1eed88d0cf78fe92` | API and behavior reference |
| TurboMICM, `tier3/fable/max` | `cb0f8a3020f1c0b9ec7e0ad079e79b4f4a8f9bdd` | Selected Fable reference solution |
| TurboTUV | `8d6f03de17e2f51952f0d2c7cf7f6248b7b31b7a` | Photolysis backend |

The selected Fable branch has its source and generator at the repository root;
it is distinct from `fable/` on TurboMICM's `main` branch. Extract pinned sources
into TMT-owned build storage, initially using read-only `git archive` operations.
Do not switch branches, create worktrees, or run source-mutating build targets
in sibling repositories. Record source identity in a dependency manifest and
retain license notices and attribution.

Use CMake build rules owned by TMT for unchanged native sources. Support explicit
dependency-directory overrides. Build outputs, generated files, logs, caches,
packaging, documentation, and tests all belong in TMT-owned locations.

Implemented layout:

```text
cmake/                  dependency builds and installation
dependencies.lock       pinned source identities
include/tmt/            native API and mechanism export contract
include/musica/         C-compatible declarations
src/common/             errors, strings, maps, ownership
src/tuv/                TurboTUV adapters
src/micm/               Fable solver/state adapters
src/prepare/            process runner, cache, loader
fortran/                compatible modules
python/                 classes and ctypes bindings
tools/                  configuration translation and preparation
tests/                  consumer, numerical, and packaging checks
examples/               photolysis, chemistry, coupled examples
docs/                   compatibility matrix and validation evidence
build/                  extracted sources and build products
```

## Compatibility contract

Inventory the pinned MUSICA C, Fortran, and Python public surfaces separately.
Record names, signatures, units, array order, ownership, mutation, error behavior,
and a verification case for each entry. Include construction from files and
strings, mechanism objects, solver parameters, state operations, properties,
named rates, grids/profiles/radiators, output datasets, and lifecycle behavior.

Use TMT-owned opaque handles. Live objects from another implementation are not
interchangeable. Identify entry points needing an explicit conversion during
the inventory. Compile C consumers against actual C headers and rebuild Fortran
modules with the consumer's compiler. Target source compatibility; any binary
compatibility claim requires a separate layout and calling-convention audit.

Keep reference and TMT compatibility installations separate to avoid symbol and
package collisions. Provide compatible `musica` Python imports in the TMT
installation, with TMT preparation utilities under `tmt`.

Unsupported methods or configuration constructs must produce explicit errors,
never silent substitutions or discarded fields. Inventory callbacks, constraints,
and external-model hooks without implying implementation of other MUSICA
components or accelerator backends. Track incomplete compatibility visibly.

## Mechanism preparation

### Processes

The C preparation driver launches a configured Python executable using an
argument array. On macOS/Linux use `posix_spawn`, file actions for diagnostics,
and `waitpid`. Use absolute paths and preserve the caller's working directory.
No shell is needed. After successful generation, invoke the C compiler through
the same process abstraction.

Fable's existing generator command is equivalent to this single invocation:

```text
python3 <fable-source>/tools/generate.py <normalized-config>
        <work-dir>/mechanism --name mechanism --width <width>
        --mode <mode> --ordering <ordering>
```

It writes `mechanism.c` and `mechanism.h`. Compile them with a small TMT-owned
export wrapper. Capture translation, generation, compiler, and loader diagnostics
separately. Handle interrupted waits and unsuccessful or signaled exits. Keep
failed work isolated from valid cache entries. Test spaces in paths.

### Translation and exports

Implement a TMT Python translator from supported MUSICA JSON/YAML and serialized
mechanism objects to Fable's input format. Preserve species properties, phases,
constant species, third bodies, stoichiometry, reaction types, rate labels,
units, and initial values. Resolve included files relative to the source file
and record every input dependency. Audit photolysis/custom-rate naming carefully.
Reject constructs that cannot be represented faithfully. Register live callbacks
separately from portable configuration data.

Define a versioned C export contract for generated metadata, system/reaction
construction, initial values, and kernels. Use one compatible Fable runtime
shared by TMT and loaded mechanisms. Specify allocation/destruction ownership.
Keep each loaded module alive while any associated solver or state exists.

### Cache and deployment

Cache identity includes input contents and dependencies, translator identity,
Fable revision, generator arguments, TMT export version, compiler identity,
flags, architecture, precision, vector width, and native-library identity.
Build in a temporary directory and publish validated entries atomically.
Coordinate concurrent preparation of an identical entry. Do not publish failures.

Verify that generated kernels are actually accepted by Fable; report any width
or fingerprint mismatch and generic fallback. Measure the optimized path.

- Prepared deployment loads a compiled artifact and manifest without invoking
  Python or a compiler. Native hosts need neither tool installed.
- Initialization preparation takes source configuration, checks the cache, and
  generates/compiles on a miss. This mode needs Python and a compatible compiler.
- Source-config cache hits without Python require native validation of recorded
  source dependencies; otherwise document that normalization still needs Python.
- Prepare before launching batch workers when practical; each process loads the
  artifact into its own address space and owns its state.

## Phases and gates

### P0: Foundation and inventory

Build pinned Fable and TurboTUV sources with CMake, optional Fortran, explicit
OpenMP settings, and their numerical compilation requirements. Implement common
C types and errors and write the compatibility matrix.

Gate: native libraries and a C consumer build; provenance is recorded; sibling
state is unchanged. Maintain this check throughout implementation.

### P1: TurboTUV C interface

Implement model construction, run/destruction, grid/profile/radiator maps and
host updates, host radiation, named rates, dimensions, and errors. Preserve
component order, units, distance scaling, and borrowed-data lifetimes.

Gate: full-model and host-update cases, repeated runs, and independent models
match direct TurboTUV calls and pinned MUSICA behavior at documented tolerances.

### P2: TurboTUV Fortran and Python interfaces

Implement compatible Fortran modules and Python classes, including array owners,
exceptions, maps, and labeled output datasets. Reuse suitable high-level code
only where it fits the agreed implementation languages.

Gate: equivalent examples in all three languages agree; mutation and lifecycle
tests exercise the public interfaces rather than only internal helpers.

### P3: Fable preparation

Implement translation, processes, generation, compilation, export wrapper,
manifest, cache, loader, and preparation CLI. Use the same engine for the native
initialization path. Begin with a small mechanism and then TS1 and included files.

Gate: cold preparation, reuse, invalidation, concurrent requests, failure paths,
and prepared native loading without development tools pass.

### P4: Fable C solver/state interface

Implement lifecycle, conditions, properties, species/custom-rate ordering,
concentration and rate mutation, tolerances, statistics, and method mapping.
Reconcile public state layouts with vector grouping and padding; use direct
views where valid and explicit packing otherwise. Keep promised pointers stable.

Expose additional Fable methods through explicit options while preserving
compatibility defaults. Preserve integration history where required and define
behavior after state edits, parameter changes, repeated calls, and failed solves.
Implement supported callback and constraint mappings from the inventory.

Gate: small mechanisms and TS1 match direct Fable at identical settings; test
single/multiple cells and partial vector groups. Verify kernel activation and
the relevant numerical criteria for every supported method.

### P5: Fable Fortran and Python interfaces

Implement compatible modules, types, finalizers, mechanism-authoring classes,
serialization, solver parameters, states, results, and errors through the C API.
Set all ctypes signatures explicitly and retain owners for borrowed arrays and
callbacks. Define callback synchronization before permitting worker-thread use.

Gate: applicable MUSICA examples run with TMT; all languages agree on state
updates, conditions, rates, results, diagnostics, and resource lifetime.

### P6: Coupling, packaging, and evidence

Add a coupled example mapping TurboTUV photolysis names and units to Fable custom
rates before advancing chemistry. Install libraries, headers, Fortran modules,
Python code, preparation scripts, notices, and relocatable mechanism support.
Provide CMake/pkg-config consumers and test outside the build tree.

Gate: clean installed examples in all languages and both preparation modes pass.
Record cold generation/compile time, cache-hit startup, solve throughput, and
state-copy overhead separately. Compare direct backends under identical settings.

## Verification and completion

Compare TMT with each direct backend for adapter correctness, with pinned MUSICA
for API behavior, and with numerical references for method accuracy. Accelerated
Fable methods need their own stated accuracy criteria; interface compatibility
does not imply bitwise numerical equivalence.

Run focused tests per phase, applicable upstream integration/verification cases,
native sanitizers, and supported GNU/LLVM builds with matched Fortran/OpenMP
libraries. Cover invalid names/dimensions/configurations, preparation failures,
cache mismatch, repeated construction/destruction, and concurrent states.
Reference software can execute in a separate validation installation.

Done means the declared MICM/TUV compatibility matrix is closed, examples and
installation work in all three languages, preparation/caching is verified,
numerical and performance evidence is recorded, and sibling repositories remain
unchanged. Keep missing features visible; a small working example is not full
completion.

## Progress

- [x] P0 Foundation and inventory
- [x] P1 TurboTUV C
- [x] P2 TurboTUV Fortran/Python
- [x] P3 Fable preparation
- [x] P4 Fable C
- [x] P5 Fable Fortran/Python
- [x] P6 Coupling, packaging, verification

Completed October 19, 2026 for the gas-chemistry/photolysis contract in
[COMPATIBILITY.md](docs/COMPATIBILITY.md). That contract records foreign-object
conversions, supported methods, API differences, and unsupported components.
The callback inventory found no portable native lambda registration contract;
external-model constraints require separate adapters. No implementation was
added to the sibling repositories.

[VALIDATION.md](docs/VALIDATION.md) records upstream tests, numerical criteria,
sanitizers, compiler coverage, relocated installation, and performance evidence.
It distinguishes adapter parity from independent scientific validation and
identifies untested platforms. [BUILD.md](docs/BUILD.md) gives usage commands.
