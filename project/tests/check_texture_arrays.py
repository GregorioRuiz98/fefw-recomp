"""Create synthetic NVN2 assets and test arrays through the actual recording/replay path."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import struct
import subprocess


def main():
    p = argparse.ArgumentParser()
    p.add_argument('--work', required=True, type=Path)
    p.add_argument('--literal-handles', action='store_true')
    p.add_argument('--report', required=True, type=Path)
    args = p.parse_args()
    work = args.work.resolve()
    target = work / 'tests/texture-arrays'
    target.mkdir(exist_ok=True, parents=True)
    source = Path(__file__).with_name('texture_array.spvasm')
    tools = work / 'build/spirv-tools/tools'
    code = b'FEFW array test!'
    digest = hashlib.sha1(code).hexdigest()[:16]
    control = bytearray(0xd8)
    for offset, value in ((0, 0x98761234), (4, 201), (0x38, len(code)), (0x44, len(code)),
                          (0x58, 1), (0x5c, 1), (0x60, 1)):
        struct.pack_into('<I', control, offset, value)
    struct.pack_into('<H', control, 0x56, 5)
    control[0xb2] = 1
    (target / 'code.bin').write_bytes(code)
    (target / 'ctrl.bin').write_bytes(control)
    subprocess.run(['cmake', '--build', str(work / 'build/fefw'), '--target', 'fefw_array_tests', '--parallel', '1'], check=True)
    cases = []
    logs = []
    # Exercise every ordinary NVN uniform slot, both supported sampled-image shapes,
    # and zero/nonzero table offsets. Each process starts with a fresh shader cache.
    for bank in ((6,) if args.literal_handles else range(3, 19)):
        for cube in (False, True):
            for base in ((0, 7, 31) if args.literal_handles else (0, 0x300, 0x340)):
                kind = 'TextureCube' if cube else 'Texture2D'
                label = f'{kind}-c{bank}-base{base:x}'
                assembly = source.read_text(encoding='utf-8').replace('cp_t_cb6_d0', f'cp_t_cb{bank}_{base // 4:x}')
                if not cube:
                    assembly = assembly.replace(' %float Cube ', ' %float 2D ')
                    assembly = assembly.replace('%v3f = OpTypeVector %float 3', '%v3f = OpTypeVector %float 2')
                    assembly = assembly.replace('%v3f %f1 %f0 %f0', '%v3f %f0 %f0')
                word = base // 4
                if args.literal_handles:
                    word = 0x40000000 | base | ((1 if base == 7 and not cube else 0) << 14)
                    assembly = assembly.replace(f'cp_t_cb{bank}_{base // 4:x}', f'cp_t_cb0_{word:x}')
                    if not cube:
                        assembly = assembly.replace('%f1 = OpConstant %float 1', '%f1 = OpConstant %float 1\n%f2 = OpConstant %float 2')
                        assembly = assembly.replace('%v3f %f0 %f0', '%v3f %f2 %f2')
                    assembly = assembly.replace('%textures = OpVariable %ptr_tex_array', '%textures = OpVariable %ptr_sampled')
                    assembly = assembly.replace('%sample_ptr = OpAccessChain %ptr_sampled %textures %index', '')
                    assembly = assembly.replace('%sampler = OpLoad %sampled %sample_ptr', '%sampler = OpLoad %sampled %textures')
                asm = target / 'case.spvasm'
                asm.write_text(assembly, encoding='utf-8')
                (target / 'results.tsv').write_text('hash\tresources\n' + digest +
                    f'\tsb[c0+0x4]=s1b0 tex{kind}[c{0 if args.literal_handles else bank}:{word}' + ('' if args.literal_handles else 'x32') + ']=s2b640\n', encoding='utf-8')
                binary = target / (digest + '.spv')
                subprocess.run([str(tools / 'spirv-as.exe'), '--target-env', 'vulkan1.3', str(asm), '-o', str(binary)], check=True)
                subprocess.run([str(tools / 'spirv-val.exe'), '--target-env', 'vulkan1.3', str(binary)], check=True)
                env = dict(os.environ, FEFW_ARRAY_BANK=str(bank), FEFW_ARRAY_BASE=str(0 if args.literal_handles else base), FEFW_ARRAY_CUBE=str(int(cube)), FEFW_LITERAL_TEXTURE=str(base if args.literal_handles else -1))
                result = subprocess.run([str(work / 'build/fefw/fefw_array_tests.exe'), 'texture_arrays'], cwd=target,
                                        env=env, capture_output=True, text=True, timeout=90)
                output = result.stdout + result.stderr
                logs.append(label + '\n' + output)
                case = dict(name=label, exit_code=result.returncode, passed=output.count('[  OK  ]'))
                cases.append(case)
                print(label, case['exit_code'], flush=True)
                if result.returncode or case['passed'] != 1:
                    break
            if cases[-1]['exit_code'] or cases[-1]['passed'] != 1: break
        if cases[-1]['exit_code'] or cases[-1]['passed'] != 1: break
    success = len(cases) == (6 if args.literal_handles else 96) and all(c['exit_code'] == 0 and c['passed'] == 1 for c in cases)
    args.report.with_suffix('.log').write_text('\n'.join(logs), encoding='utf-8')
    args.report.write_text(json.dumps(dict(exit_code=0 if success else 1, passed=1 if success else 0,
        cases=cases, verified_samples=len(cases)*64 if success else 0,
        validation_layer_enabled=all('Vulkan validation layer: enabled' in log for log in logs),
        synthetic_shader_validated=True, scope=('Literal handles: pool texture 0/7/31, samplers 0 and 1 (distinct wrap behavior), Cube and 2D' if args.literal_handles else 'NVN recording/replay: 16 uniform banks, cube and 2D, base 0, 0x300 and 0x340, all 32 indices and reversed rebinding')), indent=2) + '\n', encoding='utf-8')
    if not success:
        print(logs[-1])
        raise SystemExit(1)



if __name__ == '__main__':
    main()
