"""Portable mechanism authoring; serialization is the native API boundary."""
# Copyright (C) 2026 University Corporation for Atmospheric Research
# SPDX-License-Identifier: Apache-2.0
from dataclasses import dataclass
from enum import Enum
from pathlib import Path
import copy
import json
from ..constants import BOLTZMANN

@dataclass
class Version:
    major: int = 1
    minor: int = 0
    patch: int = 0
    def to_string(self): return f'{self.major}.{self.minor}.{self.patch}'
    def __str__(self): return self.to_string()

class Record:
    fields = {}
    defaults = {}
    def _init(self, values):
        self.other_properties = dict(values.pop('other_properties',None) or {})
        unknown = set(values)-set(self.fields)
        if unknown: raise TypeError('unknown fields: '+', '.join(sorted(unknown)))
        for key in self.fields:
            value=values.get(key)
            setattr(self,key,copy.deepcopy(self.defaults.get(key)) if value is None else value)
    def serialize(self):
        result={wire:getattr(self,key) for key,wire in self.fields.items() if getattr(self,key) is not None and getattr(self,key)!=''}
        if set(result)&set(self.other_properties): raise ValueError('other_properties cannot override a defined field')
        result.update(self.other_properties)
        return result
    def __repr__(self): return type(self).__name__+'('+', '.join(f'{k}={getattr(self,k)!r}' for k in self.fields)+')'
    def __eq__(self,other): return type(self) is type(other) and self.serialize()==other.serialize()

class Species(Record):
    fields={'absolute_tolerance':'absolute tolerance','constant_concentration_mol_m3':'constant concentration [mol m-3]',
            'constant_mixing_ratio_mol_mol':'constant mixing ratio [mol mol-1]','is_third_body':'is third body',
            'molecular_weight_kg_mol':'molecular weight [kg mol-1]','name':'name'}
    defaults={'is_third_body':False,'name':''}
    def __init__(self,absolute_tolerance=None,constant_concentration_mol_m3=None,constant_mixing_ratio_mol_mol=None,
                 is_third_body=False,molecular_weight_kg_mol=None,name=None,other_properties=None):
        self._init({k:v for k,v in locals().items() if k!='self'})

class PhaseSpecies(Record):
    fields={'name':'name','diffusion_coefficient_m2_s':'diffusion coefficient [m2 s-1]','density_kg_m3':'density [kg m-3]'}
    def __init__(self,species=None,name=None,*,diffusion_coefficient_m2_s=None,density_kg_m3=None,other_properties=None):
        if (species is None)==(name is None): raise ValueError('PhaseSpecies requires exactly one of name or species')
        self._init(dict(name=species.name if species is not None else name,diffusion_coefficient_m2_s=diffusion_coefficient_m2_s,
                        density_kg_m3=density_kg_m3,other_properties=other_properties))

class Phase(Record):
    fields={'name':'name','species':'species'}
    def __init__(self,name=None,species=None,other_properties=None):
        self._init(dict(name=name,species=[] if species is None else list(species),other_properties=other_properties))
    def serialize(self):
        result=super().serialize()
        result['species']=[(PhaseSpecies(species=x) if isinstance(x,Species) else x).serialize() for x in self.species]
        return result

class ReactionComponent(Record):
    fields={'name':'species name','coefficient':'coefficient'}
    defaults={'name':'','coefficient':1.0}
    def __init__(self,name=None,coefficient=1.0,other_properties=None): self._init(dict(name=name,coefficient=coefficient,other_properties=other_properties))

def component(value):
    if isinstance(value,ReactionComponent): return value
    if isinstance(value,Species): return ReactionComponent(value.name)
    if isinstance(value,tuple) and len(value)==2: return ReactionComponent(value[0].name,value[1])
    if isinstance(value,dict):
        return ReactionComponent(value.get('species name',value.get('name')),value.get('coefficient',1))
    raise TypeError('reaction components require Species, ReactionComponent, or (Species, coefficient)')

REACTION_DEFINITIONS = {
    'Arrhenius': ('ARRHENIUS', dict(A=1.,B=0.,C=0.,D=300.,E=0.)),
    'Troe': ('TROE', dict(k0_A=1.,k0_B=0.,k0_C=0.,kinf_A=1.,kinf_B=0.,kinf_C=0.,Fc=.6,N=1.)),
    'TernaryChemicalActivation': ('TERNARY_CHEMICAL_ACTIVATION', dict(k0_A=1.,k0_B=0.,k0_C=0.,kinf_A=1.,kinf_B=0.,kinf_C=0.,Fc=.6,N=1.)),
    'TernaryChemicalActivationJPL19': ('TERNARY_CHEMICAL_ACTIVATION_JPL19',dict(k0_A=1.,k0_B=0.,k0_C=0.,k0_D=298.,kinf_A=1.,kinf_B=0.,kinf_C=0.,kinf_D=298.,kint_A=1.,kint_B=0.,kint_C=0.,kint_D=298.,Fc=.6,N=1.)),
    'Tunneling': ('TUNNELING',dict(A=1.,B=0.,C=0.)),
    'TaylorSeries': ('TAYLOR_SERIES',dict(A=1.,B=0.,C=0.,D=300.,E=0.,taylor_coefficients=[1.])),
    'Branched': ('BRANCHED_NO_RO2',dict(X=1.,Y=0.,a0=1.,n=0)),
    'Photolysis': ('PHOTOLYSIS',dict(scaling_factor=1.)),
    'Emission': ('EMISSION',dict(scaling_factor=1.)),
    'FirstOrderLoss': ('FIRST_ORDER_LOSS',dict(scaling_factor=1.)),
    'UserDefined': ('USER_DEFINED',dict(scaling_factor=1.)),
    'Surface': ('SURFACE',dict(reaction_probability=1.)),
}
ReactionType=Enum('ReactionType',{name:i for i,name in enumerate(REACTION_DEFINITIONS)})
COMPONENT_LISTS={'reactants','products','alkoxy_products','nitrate_products','gas_phase_products'}
WIRE={'scaling_factor':'scaling factor','reaction_probability':'reaction probability','gas_phase':'gas phase','taylor_coefficients':'taylor coefficients',
      'alkoxy_products':'alkoxy products','nitrate_products':'nitrate products','gas_phase_products':'gas-phase products','gas_phase_species':'gas-phase species'}
class Reaction(Record):
    def __init__(self,name=None,**kwargs):
        kwargs['name']=name
        if 'Ea' in kwargs:
            if kwargs.get('C') is not None: raise ValueError('Cannot specify both C and Ea.')
            kwargs['C']=-kwargs.pop('Ea')/BOLTZMANN
        phase=kwargs.get('gas_phase')
        if isinstance(phase,Phase): kwargs['gas_phase']=phase.name
        self._init(kwargs)
        for field in COMPONENT_LISTS & self.fields.keys():
            setattr(self,field,[component(x) for x in getattr(self,field)])
        if 'gas_phase_species' in self.fields and self.gas_phase_species is not None:
            if isinstance(self.gas_phase_species,str): self.gas_phase_species=ReactionComponent(self.gas_phase_species)
            else: self.gas_phase_species=component(self.gas_phase_species)
    @property
    def type(self): return ReactionType[type(self).__name__]
    def serialize(self):
        result=super().serialize(); result['type']=self.wire_type
        for field in COMPONENT_LISTS & self.fields.keys():
            result[self.fields[field]]=[component(x).serialize() for x in getattr(self,field)]
        if 'gas_phase_species' in self.fields and self.gas_phase_species is not None:
            result['gas-phase species']=component(self.gas_phase_species).name
        return result

for name,(wire,kinetics) in REACTION_DEFINITIONS.items():
    components={'reactants':[],'products':[]}
    if name=='Emission': components={'products':[]}
    if name=='Branched': components={'reactants':[],'alkoxy_products':[],'nitrate_products':[]}
    if name=='Surface': components={'gas_phase_species':None,'gas_phase_products':[]}
    defaults=dict(name='',gas_phase='gas',**kinetics,**components)
    cls=type(name,(Reaction,),{'wire_type':wire,'fields':{key:WIRE.get(key,key) for key in defaults},'defaults':defaults,'__module__':__name__})
    globals()[name]=cls

GROUPS={'arrhenius':'Arrhenius','branched':'Branched','emission':'Emission','first_order_loss':'FirstOrderLoss',
        'photolysis':'Photolysis','surface':'Surface','taylor_series':'TaylorSeries','ternary_chemical_activation':'TernaryChemicalActivation',
        'ternary_chemical_activation_jpl19':'TernaryChemicalActivationJPL19','troe':'Troe','tunneling':'Tunneling','user_defined':'UserDefined'}
class Reactions:
    def __init__(self,reactions=None):
        self.reactions=list(reactions or [])
        if not all(isinstance(x,Reaction) for x in self.reactions): raise TypeError('unsupported reaction object')
    def __iter__(self): return iter(self.reactions)
    def __len__(self): return len(self.reactions)
    def __getattr__(self,key):
        if key not in GROUPS: raise AttributeError(key)
        return [r for r in self.reactions if type(r).__name__==GROUPS[key]]

class Mechanism:
    def __init__(self,name=None,version=None,relative_tolerance=None,species=None,phases=None,reactions=None,aerosol=None,emissions=None):
        if aerosol is not None or emissions is not None:
            raise NotImplementedError('Aerosol and emissions models require separate native adapters')
        self.name=name or ''; self.version=version or Version(); self.relative_tolerance=1e-6 if relative_tolerance is None else relative_tolerance
        self.species=list(species or []); self.phases=list(phases or []); self.reactions=reactions
        self.aerosol=None; self.emissions=None
    @property
    def reactions(self): return self._reactions
    @reactions.setter
    def reactions(self,value): self._reactions=value if isinstance(value,Reactions) else Reactions(value)
    def serialize(self):
        return dict(name=self.name,version=self.version.to_string(),species=[s.serialize() for s in self.species],
                    phases=[p.serialize() for p in self.phases],reactions=[r.serialize() for r in self.reactions],
                    **{'relative tolerance':self.relative_tolerance})
    def export(self,file_path):
        path=Path(file_path)
        if path.suffix.lower() not in ('.json','.yaml','.yml'): raise ValueError('Allowable write formats are .json, .yaml, and .yml')
        path.parent.mkdir(parents=True,exist_ok=True)
        data=self.serialize()
        if path.suffix.lower()=='.json': path.write_text(json.dumps(data,indent=2,allow_nan=False)+'\n')
        else:
            import yaml
            path.write_text(yaml.safe_dump(data,sort_keys=False))

def _record(cls,data):
    reverse={wire:key for key,wire in cls.fields.items()}
    kwargs={reverse[k]:v for k,v in data.items() if k in reverse}
    kwargs['other_properties']={k:v for k,v in data.items() if k not in reverse and k!='type'}
    return cls(**kwargs)

def parse(path):
    path=Path(path)
    if path.suffix.lower()=='.json': data=json.loads(path.read_text())
    else:
        import yaml
        data=yaml.safe_load(path.read_text())
    if not isinstance(data,dict) or not str(data.get('version','')).startswith('1.'):
        raise ValueError('mechanism authoring parser requires version 1 configuration; MICM also accepts legacy CAMP files')
    allowed={'name','version','relative tolerance','species','phases','reactions'}
    if set(data)-allowed: raise NotImplementedError('unsupported mechanism fields: '+', '.join(sorted(set(data)-allowed)))
    classes={cls.wire_type:cls for cls in (globals()[name] for name in REACTION_DEFINITIONS)}
    species=[_record(Species,s) for s in data.get('species',[])]
    phases=[]
    for p in data.get('phases',[]):
        phases.append(Phase(name=p['name'],species=[_record(PhaseSpecies,s) for s in p['species']],other_properties={k:v for k,v in p.items() if k not in ('name','species')}))
    reactions=[]
    for r in data.get('reactions',[]):
        if r['type'] not in classes: raise NotImplementedError('unsupported reaction type '+r['type'])
        reactions.append(_record(classes[r['type']],r))
    return Mechanism(name=data.get('name'),version=Version(*map(int,data['version'].split('.'))),relative_tolerance=data.get('relative tolerance'),
                     species=species,phases=phases,reactions=reactions)
