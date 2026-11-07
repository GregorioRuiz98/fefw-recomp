"""Bounded SDK 22 ARM64 probes; fallback evidence, not REA provider Evidence.

Reads the prepared private SDK, relocates it in Unicorn, and executes only the
selected revision/feature routines. Does not launch the game or write its files.
"""
from pathlib import Path
import sys,struct,hashlib,json,argparse
p=argparse.ArgumentParser()
p.add_argument('--engine',type=Path,required=True)
p.add_argument('--exefs',type=Path,required=True)
p.add_argument('--report',type=Path,required=True)

args=p.parse_args()
sys.path.insert(0,str((args.engine/'recomp').resolve()))
from nxrecomp.image import Image
import unicorn as u
from unicorn import arm64_const as a
B=0x7500000000; RAM=0x10000000; STOP=RAM+0x10000; SP=RAM+0x1f000
source=args.exefs/'sdk'
if hashlib.sha256(source.read_bytes()).hexdigest() != '74f3c43f8f7ccf0be2ab1134d530753c33cb0043d5611598ad7d1db887eaad37':
 raise ValueError('SDK identity differs from the build examined in this investigation')
im=Image.from_nso(source.read_bytes(),B,'sdk'); syms=list(im.symbols())
uc=u.Uc(u.UC_ARCH_ARM64,u.UC_MODE_ARM); uc.ctl_set_cpu_model(a.UC_CPU_ARM64_MAX)
uc.mem_map(B,(im.end-B+4095)&~4095); uc.mem_write(B,bytes(im.mem)); uc.mem_map(RAM,0x20000)
missing={}
for off,info,add in list(im.relocs())+list(im.jmprel()):
 typ=info&0xffffffff; idx=info>>32
 if typ==0x403: value=B+add
 elif typ in (0x101,0x401,0x402):
  sym=syms[idx]; value=B+sym[4]+add if sym[3] else 0
  if not value: missing[B+off]=sym[0]
 else: continue
 uc.mem_write(B+off,struct.pack('<Q',value&0xffffffffffffffff))
def call(off,*args):
 for i in range(31): uc.reg_write(a.UC_ARM64_REG_X0+i,0)
 uc.reg_write(a.UC_ARM64_REG_SP,SP); uc.reg_write(a.UC_ARM64_REG_X30,STOP)
 for i,v in enumerate(args): uc.reg_write(a.UC_ARM64_REG_X0+i,v)
 try: uc.emu_start(B+off,STOP,timeout=2000000,count=200000)
 except u.UcError as e: raise RuntimeError(f'{e}, pc={uc.reg_read(a.UC_ARM64_REG_PC):x}') from e
 if uc.reg_read(a.UC_ARM64_REG_PC)!=STOP: raise RuntimeError('oracle instruction/time budget reached')
 return uc.reg_read(a.UC_ARM64_REG_X0)
if __name__=='__main__':
 out={'engine':'Unicorn ARM64 MAX','sdk_normalized_sha256':hashlib.sha256(source.read_bytes()).hexdigest(),'functions':{}}
 for name,typ,bind,sec,off,size in sorted(syms,key=lambda s:s[4]):
  if typ!=2 or not sec: continue
  if name.endswith('CheckValidRevisionEj') or name.endswith('GetRevisionNumberEj') or ('BehaviorInfo' in name and 0x2478bc<=off<0x247d60):
   vals={}
   for rev in (0x31564552,0x3f564552,0x40564552,0x40764552,0x41564552,0,0xffffffff):
    uc.mem_write(RAM,struct.pack('<IIQ',0x40564552,rev,0))
    arg=rev if name.endswith('Ej') else RAM
    try: vals[f'0x{rev:08x}']=call(off,arg)&0xffffffff
    except RuntimeError as e: vals[f'0x{rev:08x}']={'error':str(e)}
   out['functions'][name]={'offset':hex(off),'code_sha256':hashlib.sha256(bytes(im.mem[off:off+size])).hexdigest(),'values':vals}
 args.report.parent.mkdir(parents=True,exist_ok=True)
 args.report.write_text(json.dumps(out,indent=2))
 print(json.dumps({'functions':len(out['functions']),'cases':sum(len(f['values']) for f in out['functions'].values()),'errors':sum(isinstance(v,dict) for f in out['functions'].values() for v in f['values'].values())}))
