"""Extract NVN2 stages from this build's BF SHA / BNSH archives.

Layout reference: maierfelix/bnsh-decoder BNSH.ksy; NVN2 stage descriptors
and offsets independently checked against the local build. No reflection rewrite.
"""
import argparse
import collections
import csv
import hashlib
import json
from pathlib import Path
import struct
import sys

import zstandard

STAGES = ['vs', 'tcs', 'tes', 'gs', 'ps', 'cs']
CONTROL_STAGES = [0, 3, 4, 2, 1, 5]


def sources(romfs, exefs=None, engine=None, include_extra_containers=False):
    paths = sorted((romfs / 'Shaders').glob('*.bfsha.zst'))
    if include_extra_containers:
        paths += sorted((romfs / 'Effect').rglob('*.ptcl.zst'))
        paths += sorted((romfs / 'Layout').rglob('*.arc.zst'))
    for path in paths:
        data = zstandard.ZstdDecompressor().decompress(path.read_bytes(), max_output_size=512 * 1024 * 1024)
        if path.name.endswith('.bfsha.zst') and data[:4] != b'FSHA':
            raise ValueError(f'{path.name}: missing FSHA header')
        yield path.relative_to(romfs).as_posix(), data
    if exefs is not None:
        sys.path.insert(0, str(engine.resolve() / 'recomp'))
        from nxrecomp.image import Image
        for name in ('main', 'sdk', 'subsdk0', 'subsdk1', 'subsdk2', 'rtld'):
            image = Image.from_nso((exefs / name).read_bytes(), 0x7100000000, name)
            yield f'exefs/{name}', image.mem


def checked_slice(data, offset, size):
    if offset < 0 or size < 0 or offset > len(data) or size > len(data) - offset:
        raise ValueError(f'BNSH range outside archive: {offset:#x}+{size:#x}')
    return data[offset:offset + size]


def read_u64(data, offset):
    return struct.unpack('<Q', checked_slice(data, offset, 8))[0]


def extract_bnsh(data):
    if checked_slice(data, 0, 4) != b'BNSH' or checked_slice(data, 12, 2) != b'\xff\xfe':
        raise ValueError('Not a little-endian BNSH archive')
    # Both versions occur in this dump. Their descriptors pass the same bounds,
    # size and NVN2 control checks below; never infer compatibility from version alone.
    if checked_slice(data, 8, 4) not in (b'\x00\x04\x02\x00', b'\x01\x04\x02\x00'):
        raise ValueError('Unsupported BNSH version (expected 2.4.0 or 2.4.1)')
    size = struct.unpack('<I', checked_slice(data, 28, 4))[0]
    data = checked_slice(data, 0, size)
    if checked_slice(data, 0x60, 4) != b'grsc':
        raise ValueError('Missing grsc block')
    count = struct.unpack('<I', checked_slice(data, 0x7C, 4))[0]
    variations = read_u64(data, 0x80)
    checked_slice(data, variations, count * 0x40)
    for i in range(count):
        program = read_u64(data, variations + 0x40 * i + 0x10)
        if not program:
            continue
        checked_slice(data, program, 0x60)
        for slot, stage in enumerate(STAGES):
            descriptor = read_u64(data, program + 8 + slot * 8)
            if not descriptor:
                continue
            control_offset = read_u64(data, descriptor + 8)
            code_offset = read_u64(data, descriptor + 16)
            code_size, control_size = struct.unpack('<II', checked_slice(data, descriptor + 24, 8))
            control = checked_slice(data, control_offset, control_size)
            code = checked_slice(data, code_offset, code_size)
            if len(control) < 0xD8 or control[:8] != b'\x34\x12\x76\x98\xc9\0\0\0':
                raise ValueError('Unsupported NVN2 control header')
            if struct.unpack_from('<I', control, 0x30)[0] != control_size:
                raise ValueError('Control size mismatch')
            if struct.unpack_from('<I', control, 0x44)[0] != code_size:
                raise ValueError('Code size mismatch')
            if struct.unpack_from('<H', control, 0x56)[0] != CONTROL_STAGES[slot]:
                raise ValueError('Shader stage mismatch')
            yield i, stage, control, code


def main():
    p = argparse.ArgumentParser()
    p.add_argument('--romfs', type=Path, required=True)
    p.add_argument('--out', type=Path, required=True)
    p.add_argument('--report', type=Path, required=True)
    p.add_argument('--exefs', type=Path, help='Also extract embedded BNSH archives from normalized NSOs')
    p.add_argument('--engine', type=Path)
    p.add_argument('--include-extra-containers', action='store_true',
                   help='Also scan Effect/**/*.ptcl.zst and Layout/**/*.arc.zst for embedded BNSH')
    a = p.parse_args()
    if bool(a.exefs) != bool(a.engine):
        p.error('--exefs and --engine must be supplied together')
    for name in ('code', 'ctrl'):
        (a.out / name).mkdir(parents=True, exist_ok=True)
    unique, occurrences, archive_reports = {}, collections.Counter(), []
    control_semantics = collections.defaultdict(set)
    for source_name, data in sources(a.romfs, a.exefs, a.engine, a.include_extra_containers):
        start, count, archives = 0, 0, 0
        while (offset := data.find(b'BNSH', start)) >= 0:
            for variation, stage, ctrl, code in extract_bnsh(data[offset:]):
                code_hash = hashlib.sha1(code).hexdigest()[:16]
                ctrl_hash = hashlib.sha1(ctrl).hexdigest()[:16]
                occurrences[code_hash] += 1
                control_semantics[code_hash].add(ctrl[0x30:0xD8])
                # A code identity alone does not prove control/reflection equivalence.
                key = (code_hash, ctrl_hash)
                unique.setdefault(key, {'code_sha1_16': code_hash, 'stage': stage, 'code_size': hex(len(code)),
                               'ctrl_size': hex(len(ctrl)), 'first_ctrl': ctrl_hash,
                               'first_source': source_name, 'bnsh_offset': hex(offset), 'variation': variation})
                for folder, digest, blob in (('code', code_hash, code), ('ctrl', ctrl_hash, ctrl)):
                    path = a.out / folder / f'{digest}.bin'
                    if path.exists() and path.read_bytes() != blob:
                        raise ValueError('Hash collision in shader corpus')
                    if not path.exists():
                        path.write_bytes(blob)
                count += 1
            size = struct.unpack_from('<I', data, offset + 28)[0]
            start = offset + size
            archives += 1
        archive_reports.append({'file': source_name, 'bnsh_archives': archives, 'stage_occurrences': count})
        print(f'{source_name}: {archives} BNSH, {count} stages', flush=True)
    if not unique:
        raise ValueError('No supported shader stages found')
    fields = list(next(iter(unique.values())))
    # Keep all control variants for review, then make the harness's one-row-per-code index.
    with (a.out / 'variants.tsv').open('w', newline='', encoding='utf-8') as stream:
        writer = csv.DictWriter(stream, fieldnames=fields, delimiter='\t')
        writer.writeheader()
        writer.writerows(unique[k] for k in sorted(unique))
    representatives = {}
    for key in sorted(unique):
        representatives.setdefault(key[0], dict(unique[key]))
    fields += ['occurrences', 'ctrl_variants', 'first_glslc']
    with (a.out / 'unique.tsv').open('w', newline='', encoding='utf-8') as stream:
        writer = csv.DictWriter(stream, fieldnames=fields, delimiter='\t')
        writer.writeheader()
        for code_hash, row in representatives.items():
            if len(control_semantics[code_hash]) > 1:
                continue  # Do not silently select a control profile that may alter translation.
            row.update(occurrences=occurrences[code_hash],
                       ctrl_variants=sum(1 for k in unique if k[0] == code_hash), first_glslc='')
            writer.writerow(row)
    report = {'archives': archive_reports, 'unique_code_blocks': len(occurrences),
              'unique_code_control_pairs': len(unique), 'stage_occurrences': sum(occurrences.values()),
              'harness_ready_codes': sum(len(v) == 1 for v in control_semantics.values()),
              'codes_with_different_control_semantics': sorted(h for h, controls in control_semantics.items() if len(controls) > 1),
              'scope': 'romfs/Shaders/*.bfsha.zst' +
                       ('; Effect/**/*.ptcl.zst; Layout/**/*.arc.zst' if a.include_extra_containers else '') +
                       ('; embedded BNSH in all six NSOs' if a.exefs else '') +
                       '; other containers and reflection not fully inventoried',
              'spirv_translation': 'not_run', 'runtime_rendering': 'not_run'}
    a.report.parent.mkdir(parents=True, exist_ok=True)
    a.report.write_text(json.dumps(report, indent=2), encoding='utf-8')
    print(json.dumps(report, indent=2))


if __name__ == '__main__':
    main()
