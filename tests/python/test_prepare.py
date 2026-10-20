import ctypes as C
import json
import os
from concurrent.futures import ThreadPoolExecutor
from pathlib import Path
import shutil
import subprocess
import sys
import time
import pytest
from musica import MICM, SolverType
from tmt import prepare

ROOT=Path(__file__).resolve().parents[2]
SOURCE=ROOT/'tests/configs/decay.json'


def test_cache_invalidation_concurrency_and_relocation(tmp_path):
    cache=tmp_path/'cache with spaces'
    source=tmp_path/'input with spaces.json'; shutil.copyfile(SOURCE,source)
    with ThreadPoolExecutor(max_workers=4) as pool:
        paths=list(pool.map(lambda _:prepare(source,cache_directory=cache,width=1),range(4)))
    assert len(set(paths))==1
    artifact=Path(paths[0]); timestamp=(artifact/'mechanism.so').stat().st_mtime_ns
    assert prepare(source,cache_directory=cache,width=1)==str(artifact)
    assert (artifact/'mechanism.so').stat().st_mtime_ns==timestamp
    assert not list(cache.glob('.prepare-*'))
    data=json.loads(source.read_text()); data['reactions'][0]['A']=.3; source.write_text(json.dumps(data))
    assert prepare(source,cache_directory=cache,width=1)!=str(artifact)
    moved=tmp_path/'prepared deployment'; shutil.copytree(artifact,moved)
    environment=dict(os.environ,TMT_PYTHON='/does/not/exist',TMT_CC='/does/not/exist')
    program='''from musica import MICM
m=MICM(config_path=__import__('sys').argv[1]); assert m.uses_kernels
s=m.create_state(); s.set_conditions(temperatures=300,pressures=100000); s.set_concentrations({'A':1})
assert m.solve(s,1).stats.final_time == 1
'''
    subprocess.run([sys.executable,'-c',program,str(moved)],env=environment,check=True)


def test_failures_do_not_publish(tmp_path):
    bad=tmp_path/'bad.json'; bad.write_text('{"version":"1.0.0","unexpected":1}')
    with pytest.raises(ValueError,match='diagnostics'):
        prepare(bad,cache_directory=tmp_path/'badcache')
    assert not list((tmp_path/'badcache').glob('*/mechanism.so'))
    compiler=tmp_path/'failing compiler'
    compiler.write_text('#!/bin/sh\nif [ "$1" = --version ]; then echo test; exit 0; fi\nexit 19\n')
    compiler.chmod(0o700)
    with pytest.raises(ValueError,match=r'19.*diagnostics'):
        prepare(SOURCE,cache_directory=tmp_path/'compilecache',compiler=compiler)
    assert not list((tmp_path/'compilecache').glob('*/mechanism.so'))
    with pytest.raises(ValueError,match='cannot start'):
        prepare(SOURCE,cache_directory=tmp_path/'missingpython',python='/no/python')
    with pytest.raises(ValueError,match='unsupported generator'):
        prepare(SOURCE,cache_directory=tmp_path/'unsupported',width=3)


def test_manifest_mismatch(tmp_path):
    path=Path(prepare(SOURCE,cache_directory=tmp_path,width=1))
    manifest=json.loads((path/'manifest.json').read_text()); manifest['native_identity']='bad'
    (path/'manifest.json').write_text(json.dumps(manifest))
    with pytest.raises(ValueError,match='incompatible'):
        MICM(path)
    with pytest.raises(ValueError,match='incompatible'):
        prepare(SOURCE,cache_directory=tmp_path,width=1)


@pytest.mark.parametrize('field,value',[
    ('initial rate parameters',{'missing':1.}),
    ('initial conditions',{'temperature':300.}),
    ('initial conditions',{'temperature':300.,'pressure':100000.,'humidity':.5}),
])
def test_reject_unknown_initial_values(field,value,tmp_path):
    data=json.loads(SOURCE.read_text()); data[field]=value
    path=tmp_path/'invalid.json'; path.write_text(json.dumps(data))
    with pytest.raises(ValueError,match='diagnostics'):
        prepare(path,cache_directory=tmp_path/'cache')
    assert not list((tmp_path/'cache').glob('*/mechanism.so'))


def test_modern_includes_and_constant_species(tmp_path):
    data=json.loads(SOURCE.read_text())
    data['version']='1.1.0'
    data['species'].append({'name':'constant','constant concentration [mol m-3]':2.})
    data['phases'][0]['species'].append({'name':'constant'})
    data['reactions'][0]['reactants'].append({'species name':'constant','coefficient':1})
    (tmp_path/'species.json').write_text(json.dumps(data['species']))
    data['species']={'files':['species.json']}
    path=tmp_path/'config.json'; path.write_text(json.dumps(data))
    artifact=prepare(path,cache_directory=tmp_path/'cache',width=1)
    m=MICM(artifact); s=m.create_state(); s.set_conditions(temperatures=300,pressures=100000)
    assert 'constant' not in s.get_species_ordering()
    s.set_concentrations({'A':1}); m.solve(s,1)
    import math
    assert s.get_concentrations()['A'][0]==pytest.approx(math.exp(-.4),rel=1e-6)
    species=json.loads((tmp_path/'species.json').read_text()); species[-1]['constant concentration [mol m-3]']=3
    (tmp_path/'species.json').write_text(json.dumps(species))
    assert prepare(path,cache_directory=tmp_path/'cache',width=1)!=artifact


def test_string_preparation_reuses_cache(tmp_path,monkeypatch):
    monkeypatch.setenv('TMT_CACHE',str(tmp_path))
    from musica.mechanism_configuration import Species,Phase,Arrhenius,Mechanism
    a,b=Species(name='A'),Species(name='B'); gas=Phase(name='gas',species=[a,b])
    mechanism=Mechanism(species=[a,b],phases=[gas],reactions=[Arrhenius(A=.2,reactants=[a],products=[b],gas_phase=gas)])
    for _ in range(3):
        m=MICM(mechanism=mechanism); assert m.uses_kernels
    assert len(list(tmp_path.glob('*/mechanism.so')))==1

@pytest.mark.parametrize('profile',['l2','l3r','extrapolation'])
def test_reference_profiles(profile,tmp_path,monkeypatch):
    monkeypatch.setenv('TMT_CACHE',str(tmp_path))
    from tmt import configure_fable
    import math
    m=MICM(SOURCE); configure_fable(m,profile)
    s=m.create_state(); s.set_conditions(temperatures=300,pressures=100000); s.set_concentrations({'A':1})
    for _ in range(10): assert m.solve(s,1).stats.final_time==1
    assert s.get_concentrations()['A'][0]==pytest.approx(math.exp(-2),rel=.005)
    with pytest.raises(ValueError,match='without existing states'): configure_fable(m,profile)
