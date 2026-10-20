# Validation evidence

Validation date: October 19, 2026. Host: macOS 27.0.1, arm64. Source revisions
are pinned in `dependencies.lock`. All builds, generated inputs, caches, logs,
and installations used TMT-owned directories. TMT made no changes to siblings.
TurboMICM main advanced independently from `7f338d9` to `ea4b615` with a run-log
commit during validation; its scenario script/defaults still match the recorded
input revision and digests. Its pre-existing untracked `.claude/` and
`admin/__pycache__/` remained. MUSICA and TurboTUV heads and clean status stayed
unchanged.

## GNU build and interface tests

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_C_COMPILER=/opt/homebrew/bin/gcc-15 \
  -DCMAKE_Fortran_COMPILER=/opt/homebrew/bin/gfortran-15 \
  -DPython3_EXECUTABLE="$PWD/.venv/bin/python"
cmake --build build -j 8
ctest --test-dir build --output-on-failure
```

GCC/GFortran 15.2.0; Python 3.13.13, NumPy 2.5.4, PyYAML 6.0.3, xarray
2026.9.0, pytest 9.1.1. All **12 CTest suites pass**. The Python suites contain
36 unchanged upstream TUV tests, 46 unchanged upstream MICM solver/state tests,
7 TMT TUV tests, 11 preparation tests, and 15 TMT chemistry tests. The coupled
example is an additional CTest command.

The utility, MICM, and TUV Fortran tests come from the pinned MUSICA snapshot.
Only error-header include paths in the utility/MICM test sources are adapted;
the TUV test source is unchanged. An additional Fortran test exercises all three
Fable profiles. C assertions remain enabled in release tests.

| Check | Criterion and outcome |
| --- | --- |
| TUV C, v54 and TS1, JSON and YAML | All photolysis/heating/dose values match direct TurboTUV bitwise; all six raw radiation components compare exactly at 0, 30, 85, 90, 110 degrees and radiation scaling 1.03 |
| TUV host operations | Profile edits affect subsequent runs; radiation arrays and optional irradiance behave as specified; retained maps/objects/views survive owner release; independent models agree |
| Pinned Python v54/vTS1 helpers | Photolysis/heating/dose rates agree with full configured models within relative 5e-6 plus absolute 1e-20 at 30 degrees; helper/native profile interpolation paths differ |
| C chemistry | Generated kernels are active; direct generic Fable parity within `1e-12*max(abs(reference),1e-20)` for widths 1/2 and 1/3/33 cells over three repeated steps |
| Compatible chemistry methods | All eight standard/vector method variants converge on analytical decay; relative error below 5e-5 over ten 0.01-second steps |
| Fable L2/L3r/extrapolation | C-backed Python and Fortran solutions satisfy 0.5% relative error for analytical decay over ten 1-second steps |
| State/ownership | Stable native pointers, partial vector groups, independent concurrent states, retained Python views/conditions, changed/reset tolerances, mismatched solver rejection |
| Preparation | Concurrent identical requests publish one artifact; unchanged cache preserves module timestamp; changed input/includes invalidate; source strings reuse cache; moved artifacts load; bad manifests/tools/configurations fail without publication |
| Coupling | v54 ozone photolysis label maps to `PHOTO.ozone`; chemistry agrees with exponential decay using that rate |

Full modern TS1 also passes the C generated-versus-generic comparison:

```sh
build/test_micm build/_deps/musica/configs/v1/ts1/ts1.json \
  'build/test mechanism cache'
```

The full-day profile comparison below uses the Fable reference's legacy TS1
configuration. Both comparisons exercise all state species, not selected tracers.

## Second compiler and sanitizers

```sh
cmake -S . -B build-clang -DCMAKE_BUILD_TYPE=Debug \
  -DCMAKE_C_COMPILER=/usr/bin/clang -DTMT_BUILD_FORTRAN=OFF \
  -DTMT_SANITIZE=ON -DTMT_DEPENDENCY_DIR="$PWD/build/_deps" \
  -DPython3_EXECUTABLE="$PWD/.venv/bin/python" \
  -DOpenMP_C_FLAGS='-Xpreprocessor -fopenmp' \
  -DOpenMP_C_LIB_NAMES=omp \
  -DOpenMP_omp_LIBRARY=/opt/homebrew/opt/libomp/lib/libomp.dylib \
  -DOpenMP_C_INCLUDE_DIR=/opt/homebrew/opt/libomp/include \
  -DCMAKE_OSX_SYSROOT=/Applications/Xcode.app/Contents/Developer/Platforms/MacOSX.platform/Developer/SDKs/MacOSX.sdk
cmake --build build-clang -j 8
ASAN_OPTIONS=detect_leaks=0 UBSAN_OPTIONS=halt_on_error=1 \
  ctest --test-dir build-clang -R '^(micm_c|tuv_c)$' --output-on-failure
```

Apple Clang 21.0.0: both C suites pass with AddressSanitizer and undefined-behavior
instrumentation, including generated chemistry. LeakSanitizer is unavailable
in this host runtime; no leak-sanitizer result is claimed. Fortran/Python suites
were run with the GNU build. The explicit Xcode SDK avoids a mismatch with this
machine's default CommandLineTools SDK; no system configuration was changed.

## Installed consumers and relocation

```sh
cmake --install build --prefix "$PWD/build/install"
.venv/bin/python tests/check_install.py build/install \
  --work build/install-check --cc /opt/homebrew/bin/gcc-15 \
  --fc /opt/homebrew/bin/gfortran-15
```

This passes after copying the installation to a new prefix containing spaces.
CMake and pkg-config independently build C and Fortran consumers. The installed
Python package finds its relocated native library and preparation resources.
A prepared artifact is copied to another directory; C, Fortran, and Python then
solve with `TMT_PYTHON=/no/python` and `TMT_CC=/no/compiler`. Native C/Fortran
consumers therefore need no interpreter or compiler during prepared loading.
The installed coupled example also runs successfully. OpenMP and compiler
runtime libraries remain normal platform dependencies.

## TS1 profile comparison and timing

The scenario-generation inputs come from TurboMICM main revision
`7f338d90a29894466ea006f0d061049e4dc84ac7`. It is a reference input source only;
the backend remains the selected Fable revision. These commands read siblings
and write only under TMT:

```sh
.venv/bin/python ../TurboMICM/tier3/scenarios/make_scenario.py \
  build/diurnal-10min --cells 1000 --seed 101 --dt 600 --steps 144 \
  --diurnal --snapshot-every 36 \
  --defaults ../TurboMICM/reference/ts1_defaults.csv
build/benchmark_micm build/_deps/fable/configs/ts1.json \
  'build/benchmark final cache' build/diurnal-10min/cells.csv \
  build/diurnal-10min/photolysis.csv 600 144 > build/benchmark.json
```

Use a new empty cache directory to measure cold preparation. An existing valid
cache makes the first reported preparation time a cache hit. The benchmark
checks every species in every cell after every step for finiteness, nonnegative
concentrations, and adapter/direct agreement within relative 1e-10 with a
1e-14 mol m-3 denominator floor. It uses 32-cell groups, vector width 2, identical
generated kernels, profile parameters, initial conditions, and photolysis input.

All three profiles pass: **30,096,000 values per profile, 90,288,000 total**, with
zero maximum relative difference. Raw timings, input digests, compiler/native
identity, and preparation provenance are in
[evidence/fable-ts1.json](evidence/fable-ts1.json).

Cold preparation took 66.17 seconds; source-cache validation took 0.093 seconds.
Prepared startup took 0.010–0.013 seconds.

| Profile | TMT calls, seconds | Direct solver calls, seconds | Estimated concentration copies, seconds |
| --- | ---: | ---: | ---: |
| L2 | 1.135 | 1.035 | 0.0040 |
| L3r | 1.368 | 1.264 | 0.0040 |
| Extrapolation | 3.333 | 3.231 | 0.0040 |

The timing file separates cold preparation, source-cache startup, prepared
startup, TMT solve calls, direct solver calls, and estimated concentration-copy
cost. TMT timings include its validation, packing, and rate-update work; direct
solver timings exclude the explicit custom-rate update immediately before the
call. Copy cost is an isolated bandwidth estimate for two concentration copies,
scaled to the number of steps, not a full decomposition of overhead.

These are single observations on a shared workstation, not statistically
controlled speedup claims. Generated and generic Fable comparison verifies the
adapter, layout, and kernel path. It does not independently establish the
scientific accuracy of Fable's published operating points across all mechanisms
or atmospheric conditions. No Linux or Windows execution is claimed. See the
compatibility contract for excluded component models and API differences.
