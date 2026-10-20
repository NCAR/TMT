import gc
import json
import math
from concurrent.futures import ThreadPoolExecutor
from pathlib import Path
import numpy as np
import pytest
from musica import MICM,SolverType,SolverState,RosenbrockSolverParameters
from musica.mechanism_configuration import Species,Phase,Arrhenius,Photolysis,Mechanism,Version,parse
from tmt import prepare
ROOT=Path(__file__).resolve().parents[2]

@pytest.fixture
def cache(tmp_path,monkeypatch):
    monkeypatch.setenv('TMT_CACHE',str(tmp_path/'cache'))
    return tmp_path

def test_authoring_round_trip(cache):
    a,b=Species(name='A',absolute_tolerance=1e-14),Species(name='B')
    gas=Phase(name='gas',species=[a,b])
    mech=Mechanism(name='round trip',version=Version(1,0,0),species=[a,b],phases=[gas],
                   relative_tolerance=1e-7,reactions=[Arrhenius(A=.2,reactants=[a],products=[b],gas_phase=gas,other_properties={'__citation':'test'})])
    for ext in ('json','yaml','yml'):
        path=cache/('roundtrip.'+ext); mech.export(path); restored=parse(path)
        assert restored.serialize()==mech.serialize()
        model=MICM(mechanism=restored); state=model.create_state();state.set_conditions(temperatures=300,pressures=100000)
        state.set_concentrations({'A':1}); model.solve(state,1)
        assert state.get_concentrations()['A'][0]==pytest.approx(math.exp(-.2),rel=1e-6)
        assert model.get_solver_parameters().relative_tolerance==1e-7


def test_taylor_coefficients_are_preserved(cache):
    from musica.mechanism_configuration import TaylorSeries
    a,b=Species(name='A'),Species(name='B'); gas=Phase(name='gas',species=[a,b])
    mech=Mechanism(species=[a,b],phases=[gas],reactions=[TaylorSeries(
        reactants=[a],products=[b],gas_phase=gas,taylor_coefficients=[.1,1./3000.])])
    path=cache/'taylor.json'; mech.export(path)
    restored=parse(path)
    assert restored.reactions.taylor_series[0].taylor_coefficients==[.1,1./3000.]
    m=MICM(mechanism=restored); s=m.create_state()
    s.set_conditions(temperatures=300,pressures=100000); s.set_concentrations({'A':1.})
    assert m.solve(s,1).state==SolverState.Converged
    assert s.get_concentrations()['A'][0]==pytest.approx(math.exp(-.2),rel=1e-5)

@pytest.mark.parametrize('solver_type',[SolverType.rosenbrock,SolverType.rosenbrock_standard_order])
def test_concurrent_states_and_view_lifetimes(cache,solver_type):
    m=MICM(ROOT/'tests/configs/decay.json',solver_type=solver_type)
    def run(n):
        s=m.create_state(n);s.set_conditions(temperatures=[300]*n,pressures=[100000]*n)
        s.set_concentrations({'A':[1]*n})
        for _ in range(5): assert m.solve(s,.2).state==SolverState.Converged
        return s.get_concentrations()['A']
    with ThreadPoolExecutor(max_workers=4) as pool:
        results=list(pool.map(run,[1,3,9,33]))
    for values in results: np.testing.assert_allclose(values,math.exp(-.2),rtol=1e-6)
    s=m.create_state(); native=s.get_internal_state()
    view=np.asarray(native.concentrations); cond=native.conditions[0]
    native.close(); m.close(); del s,native,m;gc.collect()
    view[0]=9.; cond.temperature=333.
    assert view[0]==9. and cond.temperature==333.


def test_wrong_solver_and_parameter_updates(cache):
    m=MICM(ROOT/'tests/configs/decay.json'); other=MICM(ROOT/'tests/configs/decay.json')
    s=m.create_state();s.set_conditions(temperatures=300,pressures=100000);s.set_concentrations({'A':1})
    with pytest.raises(ValueError,match='invalid solver'): other.solve(s,1)
    m.set_solver_parameters(RosenbrockSolverParameters(relative_tolerance=1e-9,absolute_tolerances=[1e-14]*2))
    assert m.solve(s,1).state==SolverState.Converged
    assert s.get_concentrations()['A'][0]==pytest.approx(math.exp(-.2),rel=1e-8)
    with pytest.raises(ValueError,match='tolerance'):
        m.set_solver_parameters(RosenbrockSolverParameters(absolute_tolerances=[1]))


def test_restore_default_tolerances_on_existing_state(cache):
    m=MICM(ROOT/'tests/configs/decay.json')
    m.set_solver_parameters(RosenbrockSolverParameters(absolute_tolerances=[1.]*2))
    old=m.create_state()
    old.set_conditions(temperatures=300,pressures=100000)
    old.set_concentrations({'A':1e-8,'B':0})
    m.solve(old,1)
    m.set_solver_parameters(RosenbrockSolverParameters(absolute_tolerances=None))
    new=m.create_state()
    for s in (old,new):
        s.set_conditions(temperatures=300,pressures=100000)
        s.set_concentrations({'A':1e-8,'B':0})
    first,second=m.solve(old,10),m.solve(new,10)
    assert first.stats.number_of_steps==second.stats.number_of_steps
    assert old.get_concentrations()==new.get_concentrations()


@pytest.mark.parametrize('solver_type',[t for t in SolverType if t not in (SolverType.undefined,SolverType.cuda_rosenbrock)])
def test_public_solver_methods(cache,solver_type):
    m=MICM(ROOT/'tests/configs/decay.json',solver_type=solver_type)
    s=m.create_state(3)
    s.set_conditions(temperatures=[300]*3,pressures=[100000]*3)
    s.set_concentrations({'A':[1]*3})
    for _ in range(10):
        assert m.solve(s,.01).state==SolverState.Converged
    np.testing.assert_allclose(s.get_concentrations()['A'],math.exp(-.02),rtol=5e-5)


def test_reject_semantic_loss(cache):
    data=json.loads((ROOT/'tests/configs/decay.json').read_text());data['reactions'][0]['unknown kinetics']=2
    path=cache/'bad.json';path.write_text(json.dumps(data))
    with pytest.raises(ValueError): prepare(path,cache_directory=cache/'badcache')
    data['reactions'][0].pop('unknown kinetics');data['reactions'][0]['reactants'][0]['coefficient']=.5
    path.write_text(json.dumps(data))
    with pytest.raises(ValueError): prepare(path,cache_directory=cache/'badcache')
