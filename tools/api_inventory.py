#!/usr/bin/env python3
"""Report named public C functions from the pinned interface and TMT headers."""
import argparse
from pathlib import Path
import re
p=argparse.ArgumentParser();p.add_argument('reference',type=Path);p.add_argument('--output',type=Path,required=True);a=p.parse_args()
root=Path(__file__).resolve().parents[1]
sources={'common':['utils/util.hpp','utils/string.hpp','utils/error_handler.hpp','musica_c_interface.hpp'],
         'chemistry':['micm/micm_c_interface.hpp','micm/state_c_interface.hpp'],
         'photolysis':[str(p.relative_to(a.reference/'include/musica')) for p in sorted((a.reference/'include/musica/tuvx').glob('*.hpp'))]}
our='\n'.join(path.read_text() for path in (root/'include/musica').rglob('*.h'))
lines=['# Public C function inventory','', 'Reference: MUSICA revision in `dependencies.lock`. Internal helpers and native object methods are excluded.', '', '| Family | Function | TMT declaration |', '| --- | --- | --- |']
missing=[]
for family,paths in sources.items():
    names=set()
    for path in paths:
        text=(a.reference/'include/musica'/path).read_text()
        text=re.sub(r'/\*.*?\*/|//[^\n]*','',text,flags=re.S)
        blocks=[]
        for block in re.finditer(r'extern\s+"C"\s*\{',text):
            depth,end=1,block.end()
            while depth and end<len(text):
                depth += (text[end]=='{') - (text[end]=='}')
                end += 1
            blocks.append(text[block.end():end-1])
        text='\n'.join(blocks)
        for name in re.findall(r'\b([A-Z_][A-Za-z0-9_]*)\s*\([^;{}]*\)\s*;',text):
            if name.startswith('Internal') or name in {'ToString','ToMapping','GetVectorSize'}: continue
            names.add(name)
    if family=='chemistry': names.add('GetVectorSize')
    for name in sorted(names):
        found=bool(re.search(r'\b'+re.escape(name)+r'\s*\(',our))
        lines.append(f'| {family} | `{name}` | {"yes" if found else "native helper; inspect"} |')
        if not found: missing.append(name)
a.output.write_text('\n'.join(lines)+'\n')
print('Functions needing review:', ', '.join(missing) or 'none')
