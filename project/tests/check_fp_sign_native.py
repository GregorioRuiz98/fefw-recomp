"""Compile lifted FNEG/FABS and compare registers against Unicorn ARM64.

Uses only synthetic instructions and states. Run with the initialized VS environment.
"""
import argparse
import ctypes
import json
from pathlib import Path
import random
import subprocess
import sys

from elftools.elf.elffile import ELFFile
import unicorn
from unicorn import arm64_const as arm


def run(command):
    result = subprocess.run([str(x) for x in command], capture_output=True, text=True)
    if result.returncode:
        raise RuntimeError(result.stdout + result.stderr)


def main():
    p = argparse.ArgumentParser()
    p.add_argument('--engine', required=True, type=Path)
    p.add_argument('--work', required=True, type=Path)
    p.add_argument('--report', required=True, type=Path)
    a = p.parse_args()
    a.work.mkdir(parents=True, exist_ok=True)
    sys.path.insert(0, str(a.engine.resolve() / 'recomp'))
    from nxrecomp.lift import Lifter
    instructions = [f'{op} {shape.replace("N", "0")}, {shape.replace("N", "1")}'
                    for op in ('fneg', 'fabs') for shape in ('hN', 'sN', 'dN', 'vN.4h', 'vN.8h', 'vN.2s', 'vN.4s', 'vN.2d')]
    asm = a.work / 'sign.s'
    asm.write_text('.text\n' + '\n'.join(instructions) + '\n', encoding='ascii')
    obj = a.work / 'sign.o'
    run(['C:/Program Files/LLVM/bin/clang.exe', '-target', 'aarch64-none-elf', '-march=armv8.2-a+fp16', '-c', asm, '-o', obj])
    with obj.open('rb') as stream:
        code = ELFFile(stream).get_section_by_name('.text').data()
    words = [int.from_bytes(code[i:i + 4], 'little') for i in range(0, len(code), 4)]
    source = ['#include <nxrecomp/cxx.h>',
              'extern "C" __declspec(dllexport) void check_sign(int which, const unsigned char* in, unsigned char* out) {',
              'Ctx context{}; Ctx* c = &context; std::memset(&c->v[0], 0xff, 16); std::memcpy(&c->v[1], in, 16);',
              'switch(which) {']
    class Env:
        wanted = {}
        jt = {}
        def call(self, target):
            raise RuntimeError('Unexpected call in a sign instruction')
    for index, word in enumerate(words):
        statement = Lifter(0x1000, 0x2000, Env()).lift(word, 0x1000)
        source.append(f'case {index}: {{ {statement} }} break;')
    source += ['}', 'std::memcpy(out, &c->v[0], 16);', '}']
    cpp = a.work / 'sign.cpp'
    cpp.write_text('\n'.join(source), encoding='utf-8')
    dll = a.work / 'sign.dll'
    run(['C:/Program Files/LLVM/bin/clang-cl.exe', '/nologo', '/LD', '/Od', '/MT', '-std:c++20', '-mcx16',
         '/I' + str(a.engine.resolve() / 'recomp/include'), cpp, '/Fe:' + str(dll),
         '/Fo:' + str(a.work / 'sign.obj')])
    native = ctypes.CDLL(str(dll.resolve()))
    native.check_sign.argtypes = [ctypes.c_int, ctypes.c_void_p, ctypes.c_void_p]
    native.check_sign.restype = None
    uc = unicorn.Uc(unicorn.UC_ARCH_ARM64, unicorn.UC_MODE_ARM)
    uc.ctl_set_cpu_model(arm.UC_CPU_ARM64_MAX)
    uc.mem_map(0x1000, 0x1000)
    uc.mem_write(0x1000, code)
    uc.reg_write(arm.UC_ARM64_REG_CPACR_EL1, 3 << 20)
    rng = random.Random(1729)
    states = [bytes(16), bytes([255]) * 16, bytes.fromhex('00800000007c007e007d00fc00fe00fd')]
    states += [rng.randbytes(16) for _ in range(256)]
    checked, skipped = 0, []
    for index, instruction in enumerate(instructions):
        for state in states:
            uc.reg_write(arm.UC_ARM64_REG_Q0, (1 << 128) - 1)
            uc.reg_write(arm.UC_ARM64_REG_Q1, int.from_bytes(state, 'little'))
            uc.reg_write(arm.UC_ARM64_REG_FPCR, 0)
            uc.reg_write(arm.UC_ARM64_REG_FPSR, 0)
            pc = 0x1000 + index * 4
            try:
                uc.emu_start(pc, pc + 4, count=1)
            except unicorn.UcError as error:
                skipped.append({'instruction': instruction, 'reason': str(error)})
                break
            expected = uc.reg_read(arm.UC_ARM64_REG_Q0).to_bytes(16, 'little')
            input_buffer = (ctypes.c_ubyte * 16).from_buffer_copy(state)
            output_buffer = (ctypes.c_ubyte * 16)()
            native.check_sign(index, input_buffer, output_buffer)
            if bytes(output_buffer) != expected:
                raise AssertionError(f'{instruction}: {state.hex()} native={bytes(output_buffer).hex()} ARM64={expected.hex()}')
            checked += 1
    report = {'instruction_shapes': len(instructions), 'states_per_shape': len(states),
              'comparisons_passed': checked, 'skipped_shapes': skipped, 'oracle': 'Unicorn ARM64 MAX',
              'native_compiler': 'clang-cl 22.1.8, Windows x64', 'includes_fp16': not any('h' in s['instruction'] for s in skipped)}
    a.report.write_text(json.dumps(report, indent=2), encoding='utf-8')
    print(json.dumps(report, indent=2))
    if skipped:
        raise SystemExit(2)


if __name__ == '__main__':
    main()
