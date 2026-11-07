"""Retranslate uniform arrays and publish only contracts covered by the GPU matrix."""
import argparse
import csv
import json
import re
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
    expected = {f'{kind}-c{bank}-base{base:x}' for bank in range(3, 19)
                for kind in ('TextureCube', 'Texture2D') for base in (0, 0x300, 0x340)}
    cases = proof.get('cases', [])
    if (proof['exit_code'] != 0 or proof['passed'] != 1 or
        {c['name'] for c in cases} != expected or len(cases) != len(expected) or
        any(c['exit_code'] != 0 or c['passed'] != 1 for c in cases)):
        raise SystemExit('Run ArrayTest and fix failures before accepting these shaders.')
    corpus = work / 'shaders/corpus-expanded'
    _, current = read_rows(work / 'shaders/translated-sdk22/results.tsv')
    selected = {r['hash'] for r in current if r['status'] == 'ok' and re.search(r'tex\S+x32\]', r['resources'])}
    small = work / 'shaders/corpus-uniform-arrays'
    out = work / 'shaders/translated-uniform-arrays'
    fields, rows = read_rows(corpus / 'unique.tsv')
    rows = [r for r in rows if r['code_sha1_16'] in selected]
    assert len(rows) == len(selected) and rows
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
    assert len(updated) == len(selected) and all(r['status'] == 'ok' for r in updated)
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
        '--merge', '--runtime-single-descriptors', '--verified-uniform-array-matrix']
    subprocess.run(command, check=True)


if __name__ == '__main__':
    main()
