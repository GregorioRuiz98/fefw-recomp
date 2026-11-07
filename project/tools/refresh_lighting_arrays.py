"""Retranslate and validate only the two Lighting arrays covered by the GPU regression."""
import argparse
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
    proof = json.loads((project / 'reports/texture-arrays-gpu.json').read_text())
    if proof['exit_code'] != 0 or proof['passed'] != 1:
        raise SystemExit('Run ArrayTest and fix failures before accepting these shaders.')
    corpus = work / 'shaders/corpus-expanded'
    selected = {'bb2933bb746192a8', 'e8d644717c8e4c1e'}
    small = work / 'shaders/corpus-lighting-arrays'
    out = work / 'shaders/translated-lighting-arrays'
    fields, rows = read_rows(corpus / 'unique.tsv')
    rows = [r for r in rows if r['code_sha1_16'] in selected]
    assert len(rows) == 2
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
    assert len(updated) == 2 and all(r['status'] == 'ok' and 'texTextureCube[c6:208x32]' in r['resources'] for r in updated)
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
    for digest in sorted(selected):
        command += ['--verified-uniform-array-hash', digest]
    subprocess.run(command, check=True)


if __name__ == '__main__':
    main()
