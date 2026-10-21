"""Configuration compatibility used by existing CheMPAS-A mechanisms."""
import json
import os
from pathlib import Path
import sys

import pytest

ROOT = Path(__file__).resolve().parents[2]
DEPS = Path(os.environ.get('TMT_TEST_DEPS', ROOT / 'build' / '_deps'))
sys.path.insert(0, str(DEPS / 'fable' / 'tools'))
sys.path.insert(0, str(ROOT / 'tools'))
from normalize import load, normalize


def test_yaml_chemical_names_and_shorthand_phase(tmp_path):
    source = tmp_path / 'mechanism.yaml'
    source.write_text('''version: "1.0.0"
species:
  - name: NO
    __do advect: true
    __absolute tolerance: 1e-10
    __label: on
  - name: NO2
    __do advect: false
phases:
  - name: gas
    species:
      - NO
      - name: NO2
        density [kg m-3]: 1.0
reactions:
  - type: PHOTOLYSIS
    name: NO2
    reactants: [{species name: NO2}]
    products: [{species name: NO}]
''')
    normalized, metadata, _ = normalize(source)
    species = normalized['camp-data'][:2]
    assert [entry['name'] for entry in species] == ['NO', 'NO2']
    assert species[0]['__do advect'] is True
    assert species[1]['__do advect'] is False
    assert species[0]['__absolute tolerance'] == 1e-10
    assert species[0]['__label'] == 'on'
    assert species[1]['density [kg m-3]'] == 1.0
    reaction = normalized['camp-data'][2]['reactions'][0]
    assert reaction['MUSICA name'] == 'PHOTO.NO2'
    assert reaction['products'] == [{'species': 'NO', 'yield': 1.0}]

    canonical = load(source)
    canonical['phases'][0]['species'][0] = {'name': 'NO'}
    reference = tmp_path / 'mechanism.json'
    reference.write_text(json.dumps(canonical))
    reference_normalized, reference_metadata, _ = normalize(reference)
    assert normalized == reference_normalized
    assert metadata == reference_metadata


@pytest.mark.parametrize('entry', [42, True, None])
def test_reject_invalid_phase_species(tmp_path, entry):
    data = json.loads((ROOT / 'tests/configs/decay.json').read_text())
    data['phases'][0]['species'][0] = entry
    path = tmp_path / 'invalid.json'
    path.write_text(json.dumps(data))
    with pytest.raises(ValueError, match='phase species must be a name or a mapping'):
        normalize(path)


def test_yaml_loader_does_not_change_pyyaml_defaults(tmp_path):
    import yaml
    path = tmp_path / 'names.yaml'
    path.write_text('name: NO\n')
    assert load(path)['name'] == 'NO'
    assert yaml.safe_load(path.read_text())['name'] is False
