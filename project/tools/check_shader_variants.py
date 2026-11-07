"""Compare generated SPIR-V and binding contracts for ambiguous NVN2 controls.

Only promotes a code identity when every distinct observed control layout passes
spirv-val and produces byte-identical SPIR-V and resource/query contracts.
"""
import argparse
import csv
import json
from pathlib import Path
import shutil
import subprocess
from validate_shaders import runtime_rejection


def read_rows(path):
    with path.open(encoding='utf-8-sig') as stream:
        reader = csv.DictReader(stream, delimiter='\t')
        return reader.fieldnames, list(reader)


def main():
    p = argparse.ArgumentParser()
    p.add_argument('--corpus', type=Path, required=True)
    p.add_argument('--work', type=Path, required=True)
    p.add_argument('--cache', type=Path, required=True)
    p.add_argument('--dotnet', type=Path, required=True)
    p.add_argument('--harness', type=Path, required=True)
    p.add_argument('--validator', type=Path, required=True)
    p.add_argument('--report', type=Path, required=True)
    p.add_argument('--runtime-single-descriptors', action='store_true')
    p.add_argument('--verified-uniform-array-matrix', action='store_true')
    a = p.parse_args()
    fields, variants = read_rows(a.corpus / 'variants.tsv')
    groups = {}
    for row in variants:
        ctrl = (a.corpus / 'ctrl' / (row['first_ctrl'] + '.bin')).read_bytes()
        groups.setdefault(row['code_sha1_16'], {}).setdefault(ctrl[0x30:0xD8], row)
    cache_fields, cached = read_rows(a.cache / 'results.tsv')
    cached = {r['hash']: r for r in cached}
    results = []
    for code_hash, controls in sorted(groups.items()):
        if len(controls) < 2:
            continue
        cached.pop(code_hash, None)
        previous = a.cache / (code_hash + '.spv')
        if previous.exists():
            previous.unlink()
        outputs = []
        failures = []
        for index, source in enumerate(controls.values()):
            corpus = a.work / code_hash / str(index)
            out = corpus / 'translated'
            for folder, digest in (('code', code_hash), ('ctrl', source['first_ctrl'])):
                (corpus / folder).mkdir(parents=True, exist_ok=True)
                shutil.copyfile(a.corpus / folder / (digest + '.bin'), corpus / folder / (digest + '.bin'))
            row = dict(source, occurrences='1', first_glslc='')
            with (corpus / 'unique.tsv').open('w', newline='', encoding='utf-8') as stream:
                writer = csv.DictWriter(stream, fieldnames=list(row), delimiter='\t')
                writer.writeheader()
                writer.writerow(row)
            command = [a.dotnet.resolve(), a.harness.resolve(), '--batch', corpus.resolve(), '--out', out.resolve(),
                       '--jobs', '1', '--spirv-val', a.validator.resolve(), '--target-env', 'vulkan1.3']
            run = subprocess.run([str(s) for s in command], capture_output=True, text=True, timeout=180)
            (corpus / 'translation.log').write_text(run.stdout + run.stderr, encoding='utf-8')
            if run.returncode:
                failures.append({'profile': index, 'error': f'translator exited {run.returncode}: {run.stdout}{run.stderr}'})
                continue
            result_path = out / 'results.tsv'
            if not result_path.exists():
                failures.append({'profile': index, 'error': run.stdout + run.stderr})
                continue
            _, result_rows = read_rows(result_path)
            result = result_rows[0]
            spv = out / (code_hash + '.spv')
            if result['status'] not in ('ok', 'ok-retry') or not spv.is_file():
                failures.append({'profile': index, 'status': result['status'], 'error': result['error']})
                continue
            # Run the validator independently as well, never trusting the harness label alone.
            valid = subprocess.run([str(a.validator.resolve()), '--target-env', 'vulkan1.3', str(spv.resolve())],
                                   capture_output=True, text=True, timeout=30)
            if valid.returncode:
                failures.append({'profile': index, 'error': valid.stderr})
                continue
            contract = tuple(result.get(k, '') for k in ('stage', 'resources', 'draw_time_queries', 'glslc_reflection'))
            reason = runtime_rejection(result.get('resources', ''), allow_uniform_matrix=a.verified_uniform_array_matrix) if a.runtime_single_descriptors else ''
            if reason:
                failures.append({'profile': index, 'error': reason})
                continue
            outputs.append((spv.read_bytes(), contract, result, spv))
        identical = not failures and len(outputs) == len(controls) and all(
            o[0] == outputs[0][0] and o[1] == outputs[0][1] for o in outputs)
        if identical:
            shutil.copyfile(outputs[0][3], a.cache / (code_hash + '.spv'))
            cached[code_hash] = outputs[0][2]
        results.append({'hash': code_hash, 'control_profiles': len(controls), 'equivalent_validated_output': identical,
                        'failures': failures})
        print(f'{code_hash}: {len(controls)} profiles, equivalent={identical}', flush=True)
    with (a.cache / 'results.tsv').open('w', newline='', encoding='utf-8') as stream:
        writer = csv.DictWriter(stream, fieldnames=cache_fields, delimiter='\t')
        writer.writeheader()
        writer.writerows(cached[h] for h in sorted(cached))
    report = {'codes_checked': len(results), 'codes_promoted': sum(r['equivalent_validated_output'] for r in results),
              'results': results}
    a.report.write_text(json.dumps(report, indent=2), encoding='utf-8')


if __name__ == '__main__':
    main()
