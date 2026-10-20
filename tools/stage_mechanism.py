#!/usr/bin/env python3
"""Preparation stage invoked by TMT C; no subprocesses are started here."""
# Copyright (C) 2026 University Corporation for Atmospheric Research
# SPDX-License-Identifier: Apache-2.0
import argparse
import hashlib
import json
import platform
import sys
from pathlib import Path


def digest(data):
    return hashlib.sha256(data).hexdigest()


def main():
    parser = argparse.ArgumentParser()
    for arg in ("input", "work", "fable", "runtime", "compiler", "native-id", "width", "mode", "ordering", "sysroot", "sanitize"):
        parser.add_argument("--" + arg, required=True)
    args = parser.parse_args()
    fable, work = Path(args.fable), Path(args.work)
    sys.path.insert(0, str(fable / "tools"))
    from normalize import normalize, identifier, number
    import mechanism
    normalized, metadata, dependencies = normalize(args.input)
    normalized_text = json.dumps(normalized, indent=2, allow_nan=False)
    (work / "normalized.json").write_text(normalized_text)
    # Validate before publishing or generating. This also catches unknown
    # species, missing kinetic parameters, and Fable's sparse compilation limits.
    parsed = mechanism.read(work / "normalized.json")
    if not parsed.reactions:
        raise ValueError("a generated mechanism requires at least one reaction")
    compiled = mechanism.compile_mechanism(parsed, ordering=args.ordering)
    for label in compiled.custom_labels:
        identifier(label, "generated custom rate label")
    for label, value in parsed.rate_parameters.items():
        if label not in compiled.custom_labels:
            raise ValueError(f"unknown initial rate parameter: {label}")
        number(value, label)
    for species in parsed.species:
        if species.initial_concentration is not None:
            if species.third_body:
                raise ValueError(f"initial concentration cannot override parameterized species {species.name}")
            number(species.initial_concentration, species.name)
    sources = [*sorted((fable / "tools").glob("*.py")), Path(__file__), Path(__file__).with_name("normalize.py")]
    manifest = {
        "abi": 1, "native_identity": args.native_id,
        "input_sha256": digest(normalized_text.encode()),
        "inputs": {str(p): digest(data) for p, data in dependencies.items()},
        "tools": {p.name: digest(p.read_bytes()) for p in sources},
        "runtime_sha256": digest(Path(args.runtime).read_bytes()),
        "compiler": str(Path(args.compiler).resolve()),
        "compiler_sha256": digest(Path(args.compiler).read_bytes()),
        "compiler_version": (work / "compiler.log").read_text(),
        "platform": platform.system(), "architecture": platform.machine(),
        "width": int(args.width), "mode": args.mode, "ordering": args.ordering,
        "precision": "binary64", "flags": ["-std=c11", "-O3", "-fPIC", "-ffp-contract=off"],
        "sysroot": args.sysroot, "sanitize": args.sanitize,
        **metadata,
    }
    identity = dict(manifest)
    # Temporary filenames and checkout locations do not change chemistry.
    identity["inputs"] = sorted(manifest["inputs"].values())
    key = digest(json.dumps(identity, sort_keys=True, allow_nan=False).encode())
    manifest["cache_key"] = key
    (work / "manifest.json").write_text(json.dumps(manifest, indent=2) + "\n")
    (work / "key.txt").write_text(key + "\n")
    patches = []
    for name, (c0, crho) in metadata["constants"].items():
        patches.append(f'  if (!strcmp(s->name, {json.dumps(name)})) tmc_species_set_parameterization(s, {c0!r}, 0, 0, {crho!r});')
    wrapper = '''#include <string.h>
#include <tmt/mechanism.h>
#include "mechanism.h"
static void patch_species(tmc_species *s) {
  (void)s;
PATCHES
}
static tmc_system system_export(void) {
  tmc_system s = mechanism_system();
  for (int i = 0; i < s.gas_phase.n_species; ++i) patch_species(&s.gas_phase.species[i].species);
  return s;
}
static int reactions_export(tmc_reaction **out) {
  int n = mechanism_reactions(out);
  for (int i = 0; i < n; ++i) {
    for (int j = 0; j < (*out)[i].n_reactants; ++j) patch_species(&(*out)[i].reactants[j]);
    for (int j = 0; j < (*out)[i].n_products; ++j) patch_species(&(*out)[i].products[j].species);
  }
  return n;
}
const TmtMechanismV1 *tmt_mechanism_v1(void) {
  static const TmtMechanismV1 api = {
    TMT_MECHANISM_ABI, sizeof(TmtMechanismV1), NATIVE_ID, CACHE_KEY, WIDTH, RTOL,
    system_export, reactions_export, mechanism_initial_state, mechanism_kernels
  };
  return &api;
}
'''
    for old, new in {"PATCHES": "\n".join(patches), "NATIVE_ID": json.dumps(args.native_id),
                     "CACHE_KEY": json.dumps(key), "WIDTH": args.width,
                     "RTOL": repr(metadata["relative_tolerance"])}.items():
        wrapper = wrapper.replace(old, new)
    (work / "export.c").write_text(wrapper)


if __name__ == "__main__":
    main()
