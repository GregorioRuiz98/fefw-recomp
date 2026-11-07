"""Refresh shaders that use the verified 32768-texture/512-sampler pool contract."""
import argparse
import csv
import json
import re
import os
import hashlib
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
    proof = json.loads((project / 'reports/texture-pools-gpu.json').read_text())
    cases = proof.get('cases', [])
    if not proof.get('passed') or len(cases) != 2 or any(c['exit_code'] or c['passed'] != 1 for c in cases):
        raise SystemExit('Ejecuta PoolTest antes de admitir shaders con pools dinamicos.')
    for name, expected in proof.get('source_sha256', {}).items():
        if hashlib.sha256((work / name).read_bytes()).hexdigest() != expected:
            raise SystemExit('El runtime cambio despues de PoolTest; repite la prueba GPU.')
    if len(proof.get('source_sha256', {})) != 6:
        raise SystemExit('Falta la huella del runtime en PoolTest.')
    corpus = work / 'shaders/corpus-expanded'
    _, current = read_rows(work / 'shaders/translated-sdk22/results.tsv')
    selected = {r['hash'] for r in current if r['status'] == 'unresolved-resource' or 'poolSize' in r.get('draw_time_queries', '')}
    small = work / 'shaders/corpus-pools'
    out = work / 'shaders/translated-pools'
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
        '--batch', small, '--out', out, '--jobs', '1', '--no-val')], check=True, env=dict(os.environ, FEFW_SHADER_POOL_CONTRACT='32768x512'))
    _, updated = read_rows(out / 'results.tsv')
    assert len(updated) == len(selected)
    assert all(r['status'] == 'ok' for r in updated)
    for row in updated:
        if row['status'] != 'ok': continue
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
        if replacements[digest]['status'] != 'ok': continue
        shutil.copyfile(out / (digest + '.spv'), main_output / (digest + '.spv'))
    command = [str(work / 'venv/Scripts/python.exe'), str(project / 'tools/validate_shaders.py'),
        '--input', str(out), '--out', str(work / 'shaders/validated-sdk22'),
        '--validator', str(work / 'build/spirv-tools/tools/spirv-val.exe'), '--report', str(args.report),
        '--merge', '--runtime-single-descriptors', '--verified-uniform-array-matrix', '--verified-pool-arrays']
    subprocess.run(command, check=True)


if __name__ == '__main__':
    main()
