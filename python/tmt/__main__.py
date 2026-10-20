import argparse
from . import prepare

def main():
    p=argparse.ArgumentParser(description='Prepare a native Fable mechanism with TMT')
    p.add_argument('configuration'); p.add_argument('--cache-directory'); p.add_argument('--python'); p.add_argument('--compiler')
    p.add_argument('--width',type=int,default=2); p.add_argument('--mode',choices=['exact','fast'],default='exact')
    p.add_argument('--ordering',choices=['markowitz','minimum-fill'],default='markowitz')
    args=vars(p.parse_args()); config=args.pop('configuration')
    print(prepare(config,**{k:v for k,v in args.items() if v is not None}))
if __name__=='__main__': main()
