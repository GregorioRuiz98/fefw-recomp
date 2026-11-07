"""Validate every translated module and publish only valid ones to a private cache."""
import argparse
import collections
import csv
import json
import re
from pathlib import Path
import shutil
import subprocess


def runtime_rejection(resources, allow_verified_uniform_arrays=False, allow_uniform_matrix=False, allow_pool_arrays=False):
    """Keep unverified arrays excluded; the audited Lighting contract is explicitly opt-in."""
    if re.search(r'(?:tex|img)\S*x(?:0|[2-9]|[1-9]\d+)\]', resources):
        arrays = re.findall(r'(?:tex|img)\S*x(?:0|[2-9]|[1-9]\d+)\]', resources)
        def verified(token):
            if allow_pool_arrays and token in ('texTexture2D[c0:1073741824x32768]',
                    'texTextureCube[c0:1073741824x32768]', 'texNone[c0:1073741824x512]'):
                return True
            if allow_verified_uniform_arrays and token == 'texTextureCube[c6:208x32]':
                return True
            match = re.fullmatch(r'texTexture(?:Cube|2D)\[c(\d+):(0|192|208)x32\]', token)
            return bool(allow_uniform_matrix and match and 3 <= int(match[1]) <= 18)
        if not all(verified(token) for token in arrays):
            return 'descriptor arrays outside the verified runtime contract'
    for kind, bank, word, count in re.findall(r'((?:tex|img)\w+)\[c(-?\d+):(\d+)(?:x(\d+))?\]', resources):
        value = int(word)
        if allow_pool_arrays and bank == '0' and value == 1073741824 and ((kind in ('texTexture2D', 'texTextureCube') and count == '32768') or (kind == 'texNone' and count == '512')):
            continue
        if value >= 1 << 28 and not (kind in ('texTexture2D', 'texTextureCube') and bank == '0'
                and (not count or count == '1') and value & 0xfc000000 == 0x40000000):
            return 'unverified packed texture handle contract'
    if any(not -1 <= int(slot) < 32 for slot in re.findall(r'\[c(-?\d+):', resources)):
        return 'packed or unknown texture constant-bank contract'
    return ''


def main():
    p = argparse.ArgumentParser()
    p.add_argument('--input', type=Path, required=True)
    p.add_argument('--out', type=Path, required=True)
    p.add_argument('--validator', type=Path, required=True)
    p.add_argument('--report', type=Path, required=True)
    p.add_argument('--merge', action='store_true', help='Keep previously validated cache entries when adding another corpus')
    p.add_argument('--runtime-single-descriptors', action='store_true', help='Exclude descriptor contracts the current NVN replay cannot populate')
    p.add_argument('--verified-uniform-array-hash', action='append', default=[],
                   help='Opt in an audited shader hash to the tested c6+0x340, 32-cube contract')
    p.add_argument('--verified-uniform-array-matrix', action='store_true',
                   help='Admit the tested 32-entry Cube/2D uniform arrays, banks 3..18, offsets 0/192/208 words')
    p.add_argument('--verified-pool-arrays', action='store_true')
    a = p.parse_args()
    with (a.input / 'results.tsv').open(encoding='utf-8-sig') as stream:
        reader = csv.DictReader(stream, delimiter='\t')
        columns = reader.fieldnames
        rows = list(reader)
    a.out.mkdir(parents=True, exist_ok=True)
    accepted, invalid, deferred = {}, [], []
    valid_count = 0
    if a.merge and (a.out / 'results.tsv').is_file():
        with (a.out / 'results.tsv').open(encoding='utf-8-sig') as stream:
            accepted = {r['hash']: r for r in csv.DictReader(stream, delimiter='\t')}
    for row in rows:
        accepted.pop(row['hash'], None)
        shader = a.input / (row['hash'] + '.spv')
        if row['status'] not in ('ok', 'ok-retry') or not shader.exists():
            continue
        check = subprocess.run([str(a.validator.resolve()), '--target-env', 'vulkan1.3', str(shader.resolve())],
                               capture_output=True, text=True, timeout=30)
        if check.returncode:
            invalid.append({'hash': row['hash'], 'error': check.stderr.strip() or check.stdout.strip()})
            continue
        valid_count += 1
        reason = runtime_rejection(row.get('resources', ''), row['hash'] in a.verified_uniform_array_hash, a.verified_uniform_array_matrix, a.verified_pool_arrays) if a.runtime_single_descriptors else ''
        if reason:
            deferred.append({'hash': row['hash'], 'reason': reason})
            continue
        shutil.copyfile(shader, a.out / shader.name)
        accepted[row['hash']] = row
    with (a.out / 'results.tsv').open('w', encoding='utf-8', newline='') as stream:
        writer = csv.DictWriter(stream, fieldnames=columns, delimiter='\t')
        writer.writeheader()
        writer.writerows(accepted[h] for h in sorted(accepted))
    # Runtime lookup uses the hash-named file too. An old successful build
    # must not survive after its current translation has been rejected.
    for stale in a.out.glob('*.spv'):
        if stale.stem not in accepted:
            stale.unlink()
    report = {'translation_statuses': dict(collections.Counter(r['status'] for r in rows)),
              'validated_modules': len(accepted), 'invalid_modules': invalid, 'target_environment': 'vulkan1.3',
              'valid_spirv_candidates': valid_count, 'runtime_deferred_modules': deferred,
              'rendering_verified': False}
    a.report.write_text(json.dumps(report, indent=2), encoding='utf-8')
    print(json.dumps(report, indent=2))


if __name__ == '__main__':
    main()
