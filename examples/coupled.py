"""Map a TurboTUV photolysis channel into a native Fable box model."""
import math
from musica import MICM, SolverState
from musica.mechanism_configuration import Species,Phase,Photolysis,Mechanism
from musica.tuvx import v54

photolysis=v54.get_tuvx_calculator().run(30.,1.)
channel='O3+hv->O2+O(1D)'
# Exact names and identical s-1 units; no positional or unit guessing.
if channel not in photolysis.reaction.values:
    raise ValueError(f'photolysis channel is missing: {channel}')
j=float(photolysis.photolysis_rate_constants.sel(reaction=channel,vertical_edge=0.))
o3,o2,o1d=(Species(name=name) for name in ('O3','O2','O1D'))
gas=Phase(name='gas',species=[o3,o2,o1d])
mechanism=Mechanism(name='Photolysis coupling',species=[o3,o2,o1d],phases=[gas],
                    reactions=[Photolysis(name='ozone',reactants=[o3],products=[o2,o1d],gas_phase=gas)])
solver=MICM(mechanism=mechanism); state=solver.create_state()
state.set_conditions(temperatures=298.,pressures=101325.)
state.set_concentrations({'O3':1e-6})
state.set_user_defined_rate_parameters({'PHOTO.ozone':j})
result=solver.solve(state,60.)
assert result.state==SolverState.Converged
actual=state.get_concentrations()['O3'][0]
assert math.isclose(actual,1e-6*math.exp(-j*60),rel_tol=1e-5)
print(f'{channel}: J={j:.8g} s-1; O3 after 60 s={actual:.10g} mol m-3')
