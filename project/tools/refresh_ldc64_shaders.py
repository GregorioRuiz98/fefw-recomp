"""Refresh locally generated shaders whose wide constant load overwrites its address."""
import argparse
import csv
import hashlib
import json
import os
from pathlib import Path
import shutil
import struct
import subprocess

REVISION = 'ldc64-source-snapshot-1'


def overlapping_loads(code, stage):
    """Return normal LDC.64/ULDC.64 instructions that overwrite their own index."""
    result = []
    start = 0 if stage == 'cs' else 0x80
    for pc in range(start, len(code) - 15, 16):
        lo, hi = struct.unpack_from('<QQ', code, pc)
        opcode = lo & 0xfff
        if opcode not in (0xb82, 0xabb) or (hi >> 9) & 7 != 5:
            continue
        if opcode == 0xb82 and (hi >> 14) & 3:
            continue  # segmented addresses are already evaluated before stores
        limit = 63 if opcode == 0xabb else 255
        destination, index = (lo >> 16) & limit, (lo >> 24) & limit
        if destination == index and index < limit:
            result.append(pc)
    return result


def read_rows(path):
    with path.open(encoding='utf-8-sig', newline='') as stream:
        reader = csv.DictReader(stream, delimiter='\t')
        return reader.fieldnames, list(reader)


def write_rows(path, fields, rows):
    temporary = path.with_suffix(path.suffix + '.preparing')
    with temporary.open('w', encoding='utf-8', newline='') as stream:
        writer = csv.DictWriter(stream, fieldnames=fields, delimiter='\t')
        writer.writeheader()
        writer.writerows(rows)
    temporary.replace(path)


def cache_current(cache, source_hash):
    try:
        stamp = json.loads((cache / 'ldc64-refresh.json').read_text(encoding='utf-8'))
        return (stamp['revision'] == REVISION and stamp['translator_sha256'] == source_hash
                and isinstance(stamp['files'], dict) and bool(stamp['files'])
                and all(hashlib.sha256((cache / name).read_bytes()).hexdigest() == digest
                        for name, digest in stamp['files'].items()))
    except (OSError, ValueError, KeyError, TypeError):
        return False


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--work', type=Path, required=True)
    parser.add_argument('--report', type=Path, required=True)
    args = parser.parse_args()
    work = args.work.resolve()
    project = Path(__file__).resolve().parents[1]
    cache = work / 'shaders/validated-sdk22'
    emitter = work / 'engine/tools/shaders/ampere/frontend/AmpereEmit.cs'
    source_hash = hashlib.sha256(emitter.read_bytes()).hexdigest()
    if cache_current(cache, source_hash):
        print('Wide constant-load shader correction is already prepared.')
        return
    # Test the actual translator and GPU with synthetic inputs before changing a cache.
    subprocess.run([str(work / 'venv/Scripts/python.exe'),
                    str(project / 'tests/check_compute_numeric.py'), '--work', str(work),
                    '--case-prefix', 'ldc64_', '--report', str(args.report.with_suffix('.numeric.json'))], check=True)
    corpus = work / 'shaders/corpus-expanded'
    fields, source_rows = read_rows(corpus / 'unique.tsv')
    selected = [row for row in source_rows if overlapping_loads(
        (corpus / 'code' / (row['code_sha1_16'] + '.bin')).read_bytes(), row['stage'])]
    if not selected:
        raise RuntimeError('No wide-load shader candidates found in the supported game corpus.')
    small = work / 'shaders/corpus-ldc64'
    output = work / 'shaders/translated-ldc64'
    for kind in ('code', 'ctrl'):
        (small / kind).mkdir(parents=True, exist_ok=True)
    for row in selected:
        for kind, key in (('code', 'code_sha1_16'), ('ctrl', 'first_ctrl')):
            name = row[key] + '.bin'
            shutil.copyfile(corpus / kind / name, small / kind / name)
    write_rows(small / 'unique.tsv', fields, selected)
    replacements = {}
    if selected:
        subprocess.run([str(work / 'toolchains/dotnet/dotnet.exe'),
                        str(work / 'engine/tools/shaders/build/bin/Release/net10.0/spirv_harness.dll'),
                        '--batch', str(small), '--out', str(output), '--jobs', '1', '--no-val'], check=True,
                       env=dict(os.environ, FEFW_SHADER_POOL_CONTRACT='32768x512'))
        _, translated = read_rows(output / 'results.tsv')
        replacements = {row['hash']: row for row in translated}
        assert set(replacements) == {row['code_sha1_16'] for row in selected}
    _, cached = read_rows(cache / 'results.tsv')
    old = {row['hash']: row for row in cached}
    # Keep the existing, audited descriptor contract; change only instruction semantics.
    for digest, row in replacements.items():
        assert digest in old and row['status'] in ('ok', 'ok-retry'), digest
        assert sorted(row['resources'].split()) == sorted(old[digest]['resources'].split()), digest
        subprocess.run([str(work / 'build/spirv-tools/tools/spirv-val.exe'), '--target-env',
                        'vulkan1.3', str(output / (digest + '.spv'))], check=True)
    updated_files = {}
    for destination in (work / 'shaders/translated-sdk22', cache):
        target_fields, target_rows = read_rows(destination / 'results.tsv')
        assert set(replacements) <= {row['hash'] for row in target_rows}
        for digest in replacements:
            target = destination / (digest + '.spv')
            temporary = target.with_suffix('.spv.preparing')
            shutil.copyfile(output / target.name, temporary)
            temporary.replace(target)
            updated_files[target.name] = hashlib.sha256(target.read_bytes()).hexdigest()
        write_rows(destination / 'results.tsv', target_fields,
                   [replacements.get(row['hash'], row) for row in target_rows])
    report = {'revision': REVISION, 'translator_sha256': source_hash,
              'refreshed_modules': len(replacements), 'files': updated_files,
              'descriptor_contracts_preserved': True, 'spirv_validated': len(replacements)}
    payload = json.dumps(report, indent=2) + '\n'
    args.report.write_text(payload, encoding='utf-8')
    stamp = cache / 'ldc64-refresh.json.preparing'
    stamp.write_text(payload, encoding='utf-8')
    stamp.replace(cache / 'ldc64-refresh.json')
    print(f'Wide constant-load correction: {len(replacements)} local shader modules refreshed.')


if __name__ == '__main__':
    main()
