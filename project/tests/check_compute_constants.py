"""Assemble synthetic shaders, build the GPU regression and preserve its actual output."""
import argparse
import json
from pathlib import Path
import subprocess


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--work', type=Path, required=True)
    parser.add_argument('--report', type=Path, required=True)
    args = parser.parse_args()
    work = args.work.resolve()
    target = work / 'tests/compute-c0'
    target.mkdir(parents=True, exist_ok=True)
    assembler = work / 'build/spirv-tools/tools/spirv-as.exe'
    validator = assembler.with_name('spirv-val.exe')
    if not assembler.exists():
        raise SystemExit('Falta spirv-as: compila el target spirv-as del SPIRV-Tools local.')
    template = Path(__file__).with_name('compute_constants.spvasm').read_text(encoding='utf-8')
    for name in ('nvn2', 'maxwell'):
        source = template
        if name == 'maxwell':
            source = source.replace('%row = OpConstant %uint 0', '%row = OpConstant %uint 32')
            for component, old, new in (('nx', 1, 0), ('ny', 2, 1), ('nz', 3, 2)):
                source = source.replace(f'%{component} = OpCompositeExtract %uint %counts {old}',
                                        f'%{component} = OpCompositeExtract %uint %counts {new}')
        assembly = target / (name + '.spvasm')
        binary = target / (name + '.spv')
        assembly.write_text(source, encoding='utf-8')
        subprocess.run([str(assembler), '--target-env', 'vulkan1.3', str(assembly), '-o', str(binary)], check=True)
        subprocess.run([str(validator), '--target-env', 'vulkan1.3', str(binary)], check=True)
    subprocess.run(['cmake', '--build', str(work / 'build/fefw'), '--target', 'fefw_compute_tests', '--parallel', '1'], check=True)
    result = subprocess.run([str(work / 'build/fefw/fefw_compute_tests.exe'), 'compute_constants'],
                            cwd=target, text=True, capture_output=True, timeout=90)
    output = result.stdout + result.stderr
    args.report.with_suffix('.log').write_text(output, encoding='utf-8')
    report = dict(exit_code=result.returncode, synthetic_spirv_validated=2,
                  gpu_cases_passed=output.count('[  OK  ]'), output_log=args.report.with_suffix('.log').name,
                  validation_enabled='validation enabled' in output,
                  scope='Same snapshot builder as NVN replay; two direct and one GPU-written indirect dispatch per layout')
    args.report.write_text(json.dumps(report, indent=2) + '\n', encoding='utf-8')
    print(output)
    if result.returncode:
        raise SystemExit(result.returncode)


if __name__ == '__main__':
    main()
