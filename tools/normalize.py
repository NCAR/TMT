#!/usr/bin/env python3
"""Translate MUSICA configurations to unchanged Fable generator input.

The returned metadata supplies constant-species parameterizations that Fable's
C runtime supports but its input generator does not serialize. All conversion
is confined to TMT. Unsupported semantic fields are errors.
"""
# Copyright (C) 2026 University Corporation for Atmospheric Research
# SPDX-License-Identifier: Apache-2.0
from __future__ import annotations
import copy
import json
import math
from pathlib import Path

PREFIX = {"PHOTOLYSIS": "PHOTO.", "EMISSION": "EMIS.",
          "FIRST_ORDER_LOSS": "LOSS.", "USER_DEFINED": "USER.", "SURFACE": "SURF."}
ALIASES = {"BRANCHED_NO_RO2": "BRANCHED", "TERNARY_CHEMICAL_ACTIVATION_JPL19": "TERNARY_JPL19"}
RATE_KEYS = {
    "ARRHENIUS": "A B C D E Ea",
    "TROE": "k0_A k0_B k0_C kinf_A kinf_B kinf_C Fc N",
    "TERNARY_CHEMICAL_ACTIVATION": "k0_A k0_B k0_C kinf_A kinf_B kinf_C Fc N",
    "TERNARY_JPL19": "k0_A k0_B k0_C k0_D kinf_A kinf_B kinf_C kinf_D kint_A kint_B kint_C kint_D Fc N",
    "BRANCHED": "X Y a0 n", "TUNNELING": "A B C",
    "TAYLOR_SERIES": "A B C D E Ea coefficients", "REVERSIBLE": "A C k_r",
    "PHOTOLYSIS": "", "EMISSION": "", "FIRST_ORDER_LOSS": "", "USER_DEFINED": "", "SURFACE": "",
}
COMMON_KEYS = {"type", "name", "MUSICA name", "gas phase"}


def reject_extra(doc, allowed, context):
    extra = {key for key in set(doc) - set(allowed) if not key.startswith('__')}
    if extra:
        raise ValueError(f"{context}: unsupported fields: {', '.join(sorted(extra))}")


def number(value, context):
    if isinstance(value, bool):
        raise ValueError(f"{context}: expected a finite number")
    result = float(value)
    if not math.isfinite(result):
        raise ValueError(f"{context}: expected a finite number")
    return result


def identifier(value, context):
    # Fable uses fixed 128-byte C strings and its generator escapes quotes and
    # backslashes, but not newlines/control characters.
    if not isinstance(value, str) or not value or len(value.encode()) >= 128 or any(ord(c) < 32 for c in value):
        raise ValueError(f"{context}: expected a nonempty name shorter than 128 UTF-8 bytes, without control characters")
    return value


def load(path, content=None):
    def pairs(items):
        result = {}
        for key, value in items:
            if key in result:
                raise ValueError(f"duplicate key: {key}")
            result[key] = value
        return result
    text = path.read_text() if content is None else content.decode('utf-8')
    try:
        return json.loads(text, object_pairs_hook=pairs)
    except json.JSONDecodeError:
        import yaml
        # Use Fable's YAML 1.2 numeric resolver so exponents agree with JSON.
        from mechanism import _yaml_loader
        class Loader(_yaml_loader()):
            pass
        def mapping(loader, node):
            loader.flatten_mapping(node)
            return pairs((loader.construct_object(k), loader.construct_object(v)) for k, v in node.value)
        Loader.add_constructor(yaml.resolver.BaseResolver.DEFAULT_MAPPING_TAG, mapping)
        return yaml.load(text, Loader=Loader)


def collect(path, dependencies, stack=()):
    path = path.resolve()
    if path in stack:
        raise ValueError(f"cyclic configuration include: {path}")
    if path in dependencies:
        return {"camp-data": []}
    content = path.read_bytes()
    doc = load(path, content)
    if not isinstance(doc, dict):
        raise ValueError(f"configuration must be a mapping: {path}")
    dependencies[path] = content
    if "version" in doc:
        for section in ('species', 'phases', 'reactions'):
            value = doc.get(section)
            if isinstance(value, dict):
                reject_extra(value, {'files'}, section)
                merged = []
                for relative in value['files']:
                    source = (path.parent / relative).resolve()
                    content = source.read_bytes()
                    part = load(source, content)
                    if not isinstance(part, list): raise ValueError(f'{section} file must contain a sequence: {source}')
                    dependencies[source] = content
                    merged.extend(part)
                doc[section] = merged
        return doc
    if "camp-files" not in doc:
        return doc
    reject_extra(doc, {"camp-files", "camp-data", "initial conditions", "initial rate parameters", "relative tolerance"}, str(path))
    result = {key: copy.deepcopy(value) for key, value in doc.items() if key != "camp-files"}
    result.setdefault("camp-data", [])
    for relative in doc["camp-files"]:
        sub = collect(path.parent / relative, dependencies, (*stack, path))
        if "version" in sub:
            raise ValueError("camp-files cannot include a version 1 mechanism")
        result["camp-data"].extend(sub.get("camp-data", []))
        for key, value in sub.items():
            if key != "camp-data":
                result[key] = value
    return result


def components(value, reactants, modern):
    result = []
    if isinstance(value, dict):
        entries = []
        for name, props in value.items():
            props = props or {}
            reject_extra(props, {"qty", "yield"}, "reaction component")
            entries.append((name, props.get("yield", props.get("qty", 1))))
    else:
        entries = []
        for item in value or []:
            if isinstance(item, str):
                entries.append((item, 1))
            else:
                name_key, qty_key = ("species name", "coefficient") if modern else ("species", "yield")
                reject_extra(item, {name_key, qty_key}, "reaction component")
                entries.append((item[name_key], item.get(qty_key, 1)))
    for name, qty in entries:
        identifier(name, "species")
        qty = number(qty, "stoichiometric coefficient")
        if qty < 0 or (reactants and (qty != int(qty) or qty > 10000)):
            raise ValueError("reactants require nonnegative integer coefficients; products require nonnegative yields")
        if reactants:
            result.extend([name] * int(qty))
        else:
            result.append({"species": name, "yield": qty})
    return result


def reaction(entry, modern):
    result = copy.deepcopy(entry)
    kind = ALIASES.get(result.get("type"), result.get("type"))
    if kind not in RATE_KEYS:
        raise ValueError(f"unsupported reaction type {kind!r}")
    if kind == 'TAYLOR_SERIES' and 'taylor coefficients' in result:
        if 'coefficients' in result: raise ValueError('conflicting Taylor coefficient fields')
        result['coefficients'] = result.pop('taylor coefficients')
    fields = {'reactants', 'products'}
    if kind == 'BRANCHED': fields = {'reactants', 'alkoxy products', 'nitrate products'}
    if kind == 'SURFACE':
        fields |= {'gas-phase reactant', 'gas-phase species', 'gas-phase products', 'reaction probability'}
    if kind in ('EMISSION','FIRST_ORDER_LOSS') and not modern: fields = {'species'}
    if kind == 'EMISSION' and modern: fields = {'products'}
    if kind in PREFIX and kind != 'SURFACE': fields.add('scaling factor')
    reject_extra(result, COMMON_KEYS | fields | set(RATE_KEYS[kind].split()), kind)
    result["type"] = kind
    if result.pop("gas phase", "gas") != "gas":
        raise ValueError("only the gas phase is supported by Fable's generator")
    if "C" in result and "Ea" in result:
        raise ValueError("rate constant cannot specify both C and Ea")
    for key in (set(RATE_KEYS[kind].split()) | {'scaling factor','reaction probability'}) - {'coefficients'}:
        if key in result: result[key] = number(result[key], key)
    if kind == 'TAYLOR_SERIES' and 'Ea' in result:
        result['C'] = -result.pop('Ea') / 1.380649e-23
    if 'coefficients' in result:
        result['coefficients'] = [number(x,'Taylor coefficient') for x in result['coefficients']]
    if kind == 'BRANCHED' and 'n' in result and result['n'] != int(result['n']):
        raise ValueError('branched n must be an integer')
    if len(result.get('coefficients', [])) > 16:
        raise ValueError('Fable supports at most 16 Taylor coefficients')
    for key in ("reactants", "products", "alkoxy products", "nitrate products", "gas-phase products"):
        if key in result:
            result[key] = components(result[key], key == "reactants", modern)
    if kind == 'SURFACE':
        declarations = [[result[key]] for key in ('gas-phase reactant','gas-phase species') if key in result]
        if 'reactants' in result: declarations.append(result['reactants'])
        if not declarations or len(declarations[0]) != 1 or any(x != declarations[0] for x in declarations):
            raise ValueError('surface reaction requires one consistently declared reactant')
        result['gas-phase reactant'] = declarations[0][0]
        result.pop('gas-phase species',None)
        result.pop('reactants',None)
        if 'gas-phase products' in result and 'products' in result:
            if result['gas-phase products'] != result['products']:
                raise ValueError('conflicting surface product fields')
            result.pop('products')
    if kind in PREFIX:
        label = result.get("name") if modern else result.get("MUSICA name")
        result["MUSICA name"] = PREFIX[kind] + identifier(label, "reaction name")
    if "MUSICA name" in result:
        identifier(result["MUSICA name"], "rate label")
    if kind in PREFIX and not result.get("MUSICA name"):
        raise ValueError(f"{kind} requires a rate label")
    if modern and kind in ("EMISSION", "FIRST_ORDER_LOSS"):
        # The modern schema represents these as ordinary components. Fable's
        # USER_DEFINED representation preserves multiple products and yields.
        result["type"] = "USER_DEFINED"
    if "gas-phase species" in result:
        result["gas-phase reactant"] = result.pop("gas-phase species")
    return result


def normalize(path):
    dependencies = {}
    path = Path(path)
    if path.is_dir():
        candidates = [path / name for name in ("config.json", "config.yaml", "config.yml") if (path / name).is_file()]
        if len(candidates) != 1:
            raise ValueError("configuration directory must contain exactly one config.json, config.yaml, or config.yml")
        path = candidates[0]
    doc = collect(path, dependencies)
    modern = "version" in doc
    constants = {}
    if modern:
        version = str(doc['version']).split('.')
        if len(version) != 3 or version[0] != '1' or not all(x.isdigit() for x in version) or int(version[1]) > 4:
            raise ValueError(f"unsupported mechanism schema version {doc['version']}")
        reject_extra(doc, {"version", "name", "species", "phases", "reactions", "relative tolerance", "initial conditions", "initial rate parameters"}, "mechanism")
        phases = doc.get("phases", [])
        if len(phases) != 1 or phases[0].get("name") != "gas":
            raise ValueError("mechanism requires exactly one phase named gas")
        reject_extra(phases[0], {"name", "species"}, "phase")
        by_name = {}
        for original in doc["species"]:
            s = copy.deepcopy(original)
            name = identifier(s["name"], "species name")
            if name in by_name:
                raise ValueError(f"duplicate species {name}")
            c0 = s.pop("constant concentration [mol m-3]", None)
            crho = s.pop("constant mixing ratio [mol mol-1]", None)
            third = s.pop("is third body", False)
            if not isinstance(third, bool): raise ValueError('is third body must be Boolean')
            if sum((c0 is not None, crho is not None, bool(third))) > 1:
                raise ValueError(f"conflicting parameterizations for {name}")
            if c0 is not None or crho is not None:
                constants[name] = [number(c0 or 0, name), number(crho or 0, name)]
            s["third body"] = bool(third or name in constants)
            s["type"] = "CHEM_SPEC"
            by_name[name] = s
        species = []
        for ps in phases[0]["species"]:
            reject_extra(ps, {"name", "diffusion coefficient [m2 s-1]", "density [kg m-3]"}, "phase species")
            name = ps["name"]
            if name not in by_name:
                raise ValueError(f"phase references unknown species {name}")
            s = by_name.pop(name)
            s.update({key: value for key, value in ps.items() if key != "name"})
            species.append(s)
        if by_name:
            raise ValueError(f"species absent from gas phase: {', '.join(by_name)}")
        entries = species + [{"type": "MECHANISM", "name": doc.get("name", "mechanism"),
                              "rate constant units": "mol m-3",
                              "reactions": [reaction(r, True) for r in doc["reactions"]]}]
    else:
        reject_extra(doc, {"camp-data", "initial conditions", "initial rate parameters", "relative tolerance"}, "CAMP mechanism")
        entries = copy.deepcopy(doc["camp-data"])
        for entry in entries:
            if entry.get("type") == "MECHANISM":
                reject_extra(entry, {"type", "name", "reactions", "rate constant units", "phase"}, "CAMP mechanism")
                if entry.get('phase', 'gas') != 'gas': raise ValueError('only the gas phase is supported')
                if entry.get("rate constant units", "molecule cm-3") not in ("molecule cm-3", "mol m-3"):
                    raise ValueError("unsupported rate constant units")
                entry["reactions"] = [reaction(r, False) for r in entry["reactions"]]
                if entry.get('rate constant units', 'molecule cm-3') == 'molecule cm-3':
                    # MUSICA's legacy parser counts all declared reactants,
                    # including parameterized species. Normalize here because
                    # Fable's generator uses the number of state variables.
                    for r in entry['reactions']:
                        n = len(r.get('reactants', []))
                        kind = r['type']
                        powers = {}
                        if kind in ('ARRHENIUS', 'TUNNELING', 'TAYLOR_SERIES', 'REVERSIBLE'): powers = {'A': n-1}
                        elif kind in ('TROE', 'TERNARY_CHEMICAL_ACTIVATION'): powers = {'k0_A': n, 'kinf_A': n-1}
                        elif kind == 'BRANCHED': powers = {'X': n-1}
                        elif kind == 'TERNARY_JPL19': powers = {'k0_A': n-1, 'kinf_A': n-2, 'kint_A': n-1}
                        for key, power in powers.items(): r[key] = number(r.get(key, 1.), key) * (6.02214076e17 ** power)
                    entry['rate constant units'] = 'mol m-3'
            elif entry.get("type") == "RELATIVE_TOLERANCE":
                doc['relative tolerance'] = entry['value']
            elif entry.get("type") != "CHEM_SPEC":
                raise ValueError(f"unsupported CAMP entry type {entry.get('type')}")
        entries = [entry for entry in entries if entry.get('type') != 'RELATIVE_TOLERANCE']
    species_names = []
    for s in entries:
        if s["type"] != "CHEM_SPEC":
            continue
        if not isinstance(s.get('third body',False),bool): raise ValueError('third body must be Boolean')
        if 'tracer type' in s:
            tracer = s.pop('tracer type')
            if tracer == 'THIRD_BODY': s['third body'] = True
            else: s['__tracer type'] = tracer
        species_names.append(identifier(s["name"], "species name"))
        props = {k: v for k, v in s.items() if k not in {"type", "name", "third body", "initial concentration [mol m-3]"}}
        if len(props) > 16:
            raise ValueError("too many species properties for Fable")
        for key, value in props.items():
            identifier(key, "property name")
            if isinstance(value, str):
                identifier(value, "property value")
            elif isinstance(value, (int, float, bool)):
                number(float(value), key)
                if isinstance(value, int) and not -(2**31) <= value < 2**31:
                    s[key] = float(value)
            else:
                raise ValueError(f"unsupported species property {key}")
    if not species_names or len(species_names) != len(set(species_names)):
        raise ValueError("mechanism must contain uniquely named species")
    result = {"camp-data": entries}
    for key in ("initial conditions", "initial rate parameters"):
        if key in doc:
            result[key] = doc[key]
    if 'initial conditions' in result:
        conditions = result['initial conditions']
        reject_extra(conditions, {'temperature', 'pressure'}, 'initial conditions')
        if ('temperature' in conditions) != ('pressure' in conditions):
            raise ValueError('initial conditions require both temperature and pressure')
        for key, value in conditions.items():
            if not key.startswith('__') and number(value, key) <= 0:
                raise ValueError(f'initial {key} must be positive')
    if not modern and 'initial rate parameters' in result:
        labels = {}
        for entry in entries:
            if entry['type'] == 'MECHANISM':
                for r in entry['reactions']:
                    kind = r['type']
                    if kind in PREFIX:
                        prefixed = r['MUSICA name']
                        labels[prefixed[len(PREFIX[kind]):]] = prefixed
        def initial_label(key):
            if key in labels: return labels[key]
            for original in sorted(labels,key=len,reverse=True):
                if key.startswith(original+'.'): return labels[original]+key[len(original):]
            return key
        result['initial rate parameters'] = {initial_label(key): value for key,value in result['initial rate parameters'].items()}
    tolerance = number(doc.get("relative tolerance", 1e-6), "relative tolerance")
    if tolerance <= 0:
        raise ValueError("relative tolerance must be positive")
    return result, {"constants": constants, "relative_tolerance": tolerance}, dependencies
