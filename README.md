# TMT

MUSICA-compatible C, Fortran, and Python interfaces for the Fable reference
chemistry solver and TurboTUV.

The native integration uses C11, Fortran uses `ISO_C_BINDING`, and Python uses
`ctypes`. Mechanism preparation invokes Fable's Python generator and a C compiler
as child processes; prepared mechanisms execute natively.

See [the implementation plan](PLAN.md) for selected revisions, architecture,
phases, and verification gates. All integration work belongs in TMT, preserving
the TurboMICM and TurboTUV source repositories.
