"""Retranslate and validate the five Lighting compute shaders covered by numerical GPU regressions."""
import argparse
import hashlib
import csv
import json
from pathlib import Path
import shutil
import subprocess


def read_rows(path):
    with path.open(encoding='utf-8-sig') as stream:
        reader = csv.DictReader(stream, delimiter='\t')
        return reader.fieldnames, list(reader)


def write_rows(path, fields, rows):
    with path.open('w', encoding='utf-8', newline='') as stream:
        writer = csv.DictWriter(stream, fieldnames=fields, delimiter='\t')
        writer.writeheader()
        writer.writerows(rows)


def main():
    p = argparse.ArgumentParser()
    p.add_argument('--work', required=True, type=Path)
    p.add_argument('--report', required=True, type=Path)
    args = p.parse_args()
    work = args.work.resolve()
    project = Path(__file__).resolve().parents[1]
    proof = json.loads((project / 'reports/compute-numeric-gpu.json').read_text())
    if proof['exit_code'] != 0 or proof['passed'] != 1:
        raise SystemExit('Ejecuta NumericTest y corrige sus fallos antes de admitir estos shaders.')
    for name, digest in proof['source_sha256'].items():
        if hashlib.sha256((work / 'engine' / name).read_bytes()).hexdigest() != digest:
            raise SystemExit('El traductor ha cambiado desde NumericTest; repite la prueba.')
    corpus = work / 'shaders/corpus-expanded'
    selected = {'09b666147b1b1593', '5f55a1c6d53b8f42', '729c628ae2cf96d0', '98acab991e64f3ba', 'bcbd9fdfde78003c'}
    small = work / 'shaders/corpus-compute-five'
    out = work / 'shaders/translated-compute-five'
    fields, rows = read_rows(corpus / 'unique.tsv')
    rows = [r for r in rows if r['code_sha1_16'] in selected]
    assert len(rows) == 5
    for folder in ('code', 'ctrl'):
        (small / folder).mkdir(parents=True, exist_ok=True)
    for row in rows:
        for folder, digest in (('code', row['code_sha1_16']), ('ctrl', row['first_ctrl'])):
            shutil.copyfile(corpus / folder / (digest + '.bin'), small / folder / (digest + '.bin'))
    write_rows(small / 'unique.tsv', fields, rows)
    subprocess.run([str(x) for x in (work / 'toolchains/dotnet/dotnet.exe',
        work / 'engine/tools/shaders/build/bin/Release/net10.0/spirv_harness.dll',
        '--batch', small, '--out', out, '--jobs', '1', '--no-val')], check=True)
    _, updated = read_rows(out / 'results.tsv')
    assert len(updated) == 5 and all(r['status'] == 'ok' and r['stage'] == 'compute' for r in updated)
    for row in updated:
        subprocess.run([str(work / 'build/spirv-tools/tools/spirv-val.exe'), '--target-env', 'vulkan1.3',
                        str(out / (row['hash'] + '.spv'))], check=True)
    main_output = work / 'shaders/translated-sdk22'
    fields, all_rows = read_rows(main_output / 'results.tsv')
    replacements = {r['hash']: r for r in updated}
    for row in all_rows:
        if row['hash'] in replacements:
            row.update(replacements[row['hash']])
    write_rows(main_output / 'results.tsv', fields, all_rows)
    for digest in replacements:
        shutil.copyfile(out / (digest + '.spv'), main_output / (digest + '.spv'))
    command = [str(work / 'venv/Scripts/python.exe'), str(project / 'tools/validate_shaders.py'),
        '--input', str(out), '--out', str(work / 'shaders/validated-sdk22'),
        '--validator', str(work / 'build/spirv-tools/tools/spirv-val.exe'), '--report', str(args.report),
        '--merge', '--runtime-single-descriptors']
    subprocess.run(command, check=True)


if __name__ == '__main__':
    main()
