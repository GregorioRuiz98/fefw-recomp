"""Validate the selected build and prepare private uncompressed NSOs for nxrt."""
import argparse
import hashlib
import json
from pathlib import Path
import shutil
import struct
import sys

BASES = {'main': 0x7100000000, 'sdk': 0x7500000000, 'subsdk0': 0x7600000000,
         'subsdk1': 0x7610000000, 'subsdk2': 0x7620000000, 'rtld': 0x7700000000}


def uncompressed_nso(data):
    from nxrecomp.nso import nso_info
    info = nso_info(data)
    prefix_end = min(struct.unpack_from('<I', data, h)[0] for h in (0x10, 0x20, 0x30))
    out = bytearray(data[:prefix_end])
    flags = struct.unpack_from('<I', data, 12)[0]
    struct.pack_into('<I', out, 12, flags & ~0x87)
    for i, seg in enumerate(info['segments']):
        offset = (len(out) + 0xFFF) & ~0xFFF
        out.extend(bytes(offset - len(out)))
        out.extend(seg['data'])
        struct.pack_into('<I', out, 0x10 + 0x10 * i, offset)
        struct.pack_into('<I', out, 0x60 + 4 * i, seg['size'])
    checked = nso_info(out)
    if not all(s['sha_ok'] for s in checked['segments']):
        raise ValueError('Normalized NSO failed its original segment hashes')
    return bytes(out)


def prepare(dump, engine, output, manifest):
    if output == dump or output.is_relative_to(dump) or dump.is_relative_to(output):
        raise ValueError('Output must be outside the original dump directory')
    sys.path.insert(0, str(engine / 'recomp'))
    expected = json.loads(manifest.read_text(encoding='utf-8'))
    from inspect_dump import inspect_npdm
    npdm = inspect_npdm(dump / 'exefs' / 'main.npdm')
    if npdm['program_id'] != expected['program_id']:
        raise ValueError('Title ID does not match the supported build')
    metadata = {'dump_root': str(dump), 'npdm': npdm, 'modules': []}
    # Verify every input before writing any module.
    sources = {}
    for name, want in expected['modules'].items():
        data = (dump / 'exefs' / name).read_bytes()
        if hashlib.sha256(data).hexdigest() != want['sha256']:
            raise ValueError(f'{name}: source SHA-256 does not match supported build')
        sources[name] = data
    output.mkdir(parents=True, exist_ok=True)
    for name, data in sources.items():
        normalized = uncompressed_nso(data)
        target = output / name
        if not target.exists() or hashlib.sha256(target.read_bytes()).digest() != hashlib.sha256(normalized).digest():
            tmp = target.with_suffix('.tmp')
            tmp.write_bytes(normalized)
            tmp.replace(target)
        metadata['modules'].append({'name': name, 'base': f'0x{BASES[name]:x}',
                                    'source_sha256': hashlib.sha256(data).hexdigest(),
                                    'normalized_sha256': hashlib.sha256(normalized).hexdigest()})
        print(f'{name}: normalized and verified ({len(normalized)} bytes)', flush=True)
    shutil.copyfile(dump / 'exefs' / 'main.npdm', output / 'main.npdm')
    (output / 'prepared.json').write_text(json.dumps(metadata, indent=2), encoding='utf-8')


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--dump', required=True, type=Path)
    parser.add_argument('--engine', required=True, type=Path)
    parser.add_argument('--out', required=True, type=Path)
    parser.add_argument('--manifest', type=Path, default=Path(__file__).resolve().parents[1] / 'supported-build.json')
    a = parser.parse_args()
    prepare(a.dump.resolve(), a.engine.resolve(), a.out.resolve(), a.manifest)


if __name__ == '__main__':
    main()
