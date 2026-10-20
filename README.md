# TMT

MUSICA-compatible C, Fortran, and Python interfaces for the Fable reference
chemistry solver and TurboTUV.

The native integration uses C11, Fortran uses `ISO_C_BINDING`, and Python uses
`ctypes`. Mechanism preparation invokes Fable's Python generator and a C compiler
as child processes; prepared mechanisms execute natively.

Start with [build and usage instructions](docs/BUILD.md), the
[compatibility contract](docs/COMPATIBILITY.md), and
[validation evidence](docs/VALIDATION.md). The [implementation plan](PLAN.md)
records the architecture and completed phases. All integration work belongs in
TMT, preserving the TurboMICM and TurboTUV source repositories.

```sh
python3 -m venv .venv
.venv/bin/python -m pip install -e . pytest
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release \
  -DPython3_EXECUTABLE="$PWD/.venv/bin/python"
cmake --build build -j 8
ctest --test-dir build --output-on-failure
```

Select compatible C/Fortran compilers and an OpenMP runtime as described in the
build guide. Libraries, modules, headers, Python packages, and preparation
support install with `cmake --install build --prefix <destination>`.
