"""Installed example configuration lookup."""
import os
from pathlib import Path
import sys

def find_config_path(*parts):
    roots=[]
    if os.environ.get('TMT_CONFIGS'): roots.append(Path(os.environ['TMT_CONFIGS']))
    roots.extend([Path(__file__).parent/'configs',Path(__file__).resolve().parents[2]/'build/_deps/musica/configs',Path(sys.prefix)/'share/tmt/configs'])
    roots.extend(p/'share/tmt/configs' for p in list(Path(__file__).resolve().parents)[:5])
    for root in roots:
        result=root.joinpath(*parts)
        if result.exists(): return str(result)
    raise FileNotFoundError('configuration not found: '+'/'.join(parts))
