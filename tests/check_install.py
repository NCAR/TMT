#!/usr/bin/env python3
"""Check a relocated installation using CMake, pkg-config, and all three APIs."""
import argparse
import json
import os
from pathlib import Path
import shlex
import shutil
import subprocess
import sys
import tempfile


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('prefix', type=Path)
    parser.add_argument('--work', type=Path, required=True)
    parser.add_argument('--cc', required=True)
    parser.add_argument('--fc', required=True)
    args = parser.parse_args()
    args.work.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix='installed-consumer-', dir=args.work.resolve()) as temp:
        work = Path(temp)
        prefix = work / 'relocated prefix'
        shutil.copytree(args.prefix.resolve(), prefix)
        env = {k: v for k, v in os.environ.items() if not k.startswith('TMT_')}
        packages = list((prefix / 'lib').glob('python*/site-packages'))
        assert len(packages) == 1, 'expected one installed Python package directory'
        env.update(PYTHONPATH=str(packages[0]), TMT_CACHE=str(work / 'cache'),
                   TMT_PYTHON=sys.executable, TMT_CC=args.cc,
                   PKG_CONFIG_PATH=str(prefix / 'lib/pkgconfig'))

        def run(argv, **kwargs):
            return subprocess.run([str(x) for x in argv], cwd=work, env=env, check=True, **kwargs)

        examples = prefix / 'share/tmt/examples'
        run(['cmake', '-S', examples, '-B', work / 'consumer',
             f'-DCMAKE_PREFIX_PATH={prefix}', f'-DCMAKE_C_COMPILER={args.cc}',
             f'-DCMAKE_Fortran_COMPILER={args.fc}'], stdout=subprocess.DEVNULL)
        run(['cmake', '--build', work / 'consumer'], stdout=subprocess.DEVNULL)
        flags = shlex.split(run(['pkg-config', '--cflags', '--libs', 'tmt'], capture_output=True, text=True).stdout)
        run([args.cc, examples / 'chemistry.c', *flags, f'-Wl,-rpath,{prefix}/lib',
             '-lm', '-o', work / 'pkgconfig-c'])
        flags = shlex.split(run(['pkg-config', '--cflags', '--libs', 'tmt-fortran'], capture_output=True, text=True).stdout)
        run([args.fc, examples / 'chemistry.F90', *flags, f'-Wl,-rpath,{prefix}/lib',
             '-o', work / 'pkgconfig-fortran'])
        program = '''import json,sys
from pathlib import Path
import musica
from musica._native import lib
from tmt import prepare
prefix=Path(sys.argv[1])
assert Path(musica.__file__).is_relative_to(prefix)
assert Path(lib._name).is_relative_to(prefix)
print(json.dumps(prepare(prefix/'share/tmt/examples/decay.json',width=1)))
'''
        artifact = Path(json.loads(run([sys.executable, '-c', program, prefix], capture_output=True, text=True).stdout))
        deployed = work / 'deployed mechanism'
        shutil.copytree(artifact, deployed)
        env.update(TMT_PYTHON='/no/python', TMT_CC='/no/compiler')
        for executable in ('consumer/chemistry_c', 'consumer/chemistry_fortran', 'pkgconfig-c', 'pkgconfig-fortran'):
            run([work / executable, deployed])
        run([sys.executable, '-c', '''from musica import MICM
import sys,math
m=MICM(sys.argv[1]); assert m.uses_kernels
s=m.create_state(); s.set_conditions(temperatures=300,pressures=100000); s.set_concentrations({'A':1})
m.solve(s,1); assert abs(s.get_concentrations()['A'][0]-math.exp(-.2))<1e-6
''', deployed])
        env.update(TMT_PYTHON=sys.executable, TMT_CC=args.cc)
        run([sys.executable, examples / 'coupled.py'])
        print('Relocated CMake, pkg-config, C, Fortran, Python, preparation, and coupled consumers passed')


if __name__ == '__main__':
    main()
