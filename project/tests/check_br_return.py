"""Compile synthetic BR/RET functions and verify return, local jump and tail call."""
import argparse
import ctypes
import json
from pathlib import Path
import struct
import subprocess
import sys
import unicorn
from unicorn import arm64_const as arm

p = argparse.ArgumentParser()
p.add_argument('--engine', type=Path, required=True)
p.add_argument('--work', type=Path, required=True)
p.add_argument('--report', type=Path, required=True)
a = p.parse_args()
a.work.mkdir(parents=True, exist_ok=True)
sys.path.insert(0, str(a.engine.resolve() / 'recomp'))
from nxrecomp.image import Image
from nxrecomp.program import Env, lift_function

movz = lambda reg, imm: 0xd2800000 | imm << 5 | reg
cases = [
    [0xaa1e03e9, movz(30,0x999), 0xaa0903fe, 0xd61f03c0],
    [0xaa1e03e9, movz(30,0x1014), 0xd61f03c0, 0, 0xd503201f, movz(0,42), 0xaa0903fe, 0xd65f03c0],
    [movz(30,0x3000), 0xd61f03c0],
    [0xd61f0180],  # ordinary BR X12 remains an indirect tail call
]
source = ['#include <nxrecomp/cxx.h>', 'static uint64_t called, trapped;',
          'extern "C" void call_indirect(Ctx* c, uint64_t target) { called=target; c->x[0]=77; }',
          'extern "C" void guest_trap(const char*, uint64_t) { trapped++; }']
for i, words in enumerate(cases):
    raw = struct.pack('<' + 'I' * len(words), *words)
    env = Env(Image('synthetic', 0x1000, raw, (0,len(raw))), full=False)
    env.ends[0x1000] = 0x1000 + len(raw)
    env.starts = [0x1000]
    text, _, bad, _ = lift_function(env, 0x1000)
    if bad:
        raise RuntimeError(bad)
    source.append(text.replace('f_1000(', f'check_{i}('))
source += ['extern "C" __declspec(dllexport) void run_case(int which, uint64_t lr, uint64_t* out) {',
           'Ctx c{}; c.x[30]=lr; c.x[12]=0x3000; called=trapped=0;',
           'switch(which) { ' + ' '.join(f'case {i}: check_{i}(&c); break;' for i in range(len(cases))) + ' }',
           'out[0]=c.x[0]; out[1]=called; out[2]=trapped; out[3]=c.x[30]; }']
cpp = a.work / 'br_return.cpp'
cpp.write_text('\n'.join(source), encoding='utf-8')
dll = a.work / 'br_return.dll'
r = subprocess.run(['C:/Program Files/LLVM/bin/clang-cl.exe', '/nologo', '/LD', '/Od', '/MT', '-std:c++20', '-mcx16',
    '/I' + str(a.engine.resolve() / 'recomp/include'), str(cpp.resolve()), '/Fe:' + str(dll.resolve()),
    '/Fo:' + str((a.work / 'br_return.obj').resolve())], capture_output=True, text=True)
a.report.with_suffix('.log').write_text(r.stdout+r.stderr, encoding='utf-8')
if r.returncode:
    raise RuntimeError(r.stdout+r.stderr)
native = ctypes.CDLL(str(dll.resolve()))
native.run_case.argtypes = [ctypes.c_int,ctypes.c_uint64,ctypes.POINTER(ctypes.c_uint64)]
native.run_case.restype = None
count = 0
oracle_cases = 0
for lr in (0x2000,0x4444,0x71001bcc04):
    for i in range(len(cases)):
        out = (ctypes.c_uint64*4)()
        native.run_case(i,lr,out)
        expected = (0,0,0,lr) if i==0 else ((42,0,0,lr) if i==1 else (77,0x3000,0,0x3000 if i==2 else lr))
        if tuple(out) != expected:
            raise AssertionError((i,hex(lr),tuple(out),expected))
        if i < 2:
            uc = unicorn.Uc(unicorn.UC_ARCH_ARM64, unicorn.UC_MODE_ARM)
            uc.mem_map(0x1000,0x1000)
            uc.mem_write(0x1000,struct.pack('<'+'I'*len(cases[i]),*cases[i]))
            uc.reg_write(arm.UC_ARM64_REG_LR,lr)
            uc.emu_start(0x1000,lr,count=100)
            if uc.reg_read(arm.UC_ARM64_REG_PC) != lr or uc.reg_read(arm.UC_ARM64_REG_X0) != out[0]:
                raise AssertionError('Native BR behavior differs from ARM64')
            oracle_cases += 1
        count += 1
a.report.write_text(json.dumps(dict(passed=True,native_cases=count,arm64_comparisons=oracle_cases,scenarios=['saved LR return','local jump','X30 tail call','ordinary indirect branch']),indent=2),encoding='utf-8')
print(f'{count} native BR return scenarios passed')
