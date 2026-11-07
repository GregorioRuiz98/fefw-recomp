"""Inspect a local, extracted game without copying or modifying its files."""
import argparse
import collections
import hashlib
import json
from pathlib import Path
import re
import struct
import sys


def inspect_npdm(path):
    return read_npdm(path.read_bytes())


def read_npdm(data):
    if len(data) < 0x80 or data[:4] != b'META':
        raise ValueError('Invalid NPDM header')
    aci, aci_size = struct.unpack_from('<II', data, 0x70)
    if aci < 0x80 or aci_size < 0x38 or aci + aci_size > len(data) or data[aci:aci + 4] != b'ACI0':
        raise ValueError('Invalid NPDM ACI0 range')
    return {
        'name': data[0x20:0x30].split(b'\0')[0].decode('utf-8', 'replace'),
        'program_id': f'{struct.unpack_from("<Q", data, aci + 0x10)[0]:016X}',
        'main_thread_priority': data[0xE],
        'main_thread_core': data[0xF],
        'main_thread_stack': struct.unpack_from('<I', data, 0x1C)[0],
        'system_resource_size': struct.unpack_from('<I', data, 0x14)[0],
    }


def inspect_nso(path):
    from nxrecomp.image import Image
    from nxrecomp.nso import sdk_version
    data = path.read_bytes()
    if len(data) < 0x100 or data[:4] != b'NSO0':
        raise ValueError(f'{path.name}: invalid NSO')
    image = Image.from_nso(data, 0x7100000000, name=path.name)
    symbols = image.symbols()
    undefined = sorted({s[0] for s in symbols if s[0] and s[3] == 0})
    exported = sorted({s[0] for s in symbols if s[0] and s[3] != 0})
    flags = struct.unpack_from('<I', data, 0xC)[0]
    checks = []
    for i, (offset, size, _) in enumerate(image.segments):
        digest = hashlib.sha256(image.mem[offset:offset + size]).digest()
        checked = bool(flags & (1 << (i + 3)))
        match = digest == data[0xA0 + i * 0x20:0xC0 + i * 0x20]
        checks.append({'offset': offset, 'size': size, 'hash_enabled': checked, 'hash_matches': match})
        if checked and not match:
            raise ValueError(f'{path.name}: segment {i} hash mismatch')
    evidence = {}
    for label, pattern in {
        'sdk_tags': rb'NintendoSDK_[A-Za-z0-9_./-]+',
        'engine_markers': rb'(?:UnityPlayer|IL2CPP|UnrealEngine|KTGL|NintendoWare|FireEmblem|Fire Emblem)[\x20-\x7e]{0,90}',
    }.items():
        evidence[label] = sorted({m.group().decode('ascii') for m in re.finditer(pattern, image.mem)})[:25]
    return {
        'name': path.name, 'file_size': len(data), 'sha256': hashlib.sha256(data).hexdigest(),
        'build_id': data[0x40:0x60].hex().upper(), 'text_size': image.text_size,
        'memory_size': image.end - image.base, 'segments': checks, 'sdk_version': sdk_version(image.mem),
        'imports': undefined, 'exports': exported, 'evidence': evidence,
    }


def resolve_dump(root):
    if (root / 'exefs' / 'main').is_file():
        return root
    candidates = sorted(p.parent.parent for p in root.glob('updates/*/exefs/main'))
    if len(candidates) != 1:
        raise ValueError('Specify one extracted version containing exefs/main and romfs')
    return candidates[0]


def inspect_dump(root):
    root = resolve_dump(root.resolve())
    exefs = root / 'exefs'
    if not (root / 'romfs').is_dir():
        raise ValueError('Missing romfs directory')
    modules = [inspect_nso(p) for p in sorted(exefs.iterdir()) if p.is_file() and p.name != 'main.npdm']
    files = [p for p in (root / 'romfs').rglob('*') if p.is_file()]
    extensions = collections.Counter(''.join(p.suffixes).lower() or '<none>' for p in files)
    shader_files = [p for p in files if 'shader' in p.relative_to(root).as_posix().lower() or '.bfsha' in p.name]
    return {
        'dump_root': str(root), 'npdm': inspect_npdm(exefs / 'main.npdm'), 'modules': modules,
        'romfs': {'file_count': len(files), 'total_bytes': sum(p.stat().st_size for p in files),
                  'extensions': dict(extensions.most_common()),
                  'shader_files': [{'path': p.relative_to(root / 'romfs').as_posix(), 'size': p.stat().st_size} for p in shader_files]},
        'limitations': ['Folder name does not verify title or version',
                        'Presence of an update folder does not prove the base and update are merged',
                        'NSO integrity does not demonstrate translation or runtime compatibility'],
    }


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--dump', type=Path, required=True)
    parser.add_argument('--engine', type=Path, required=True)
    parser.add_argument('--out', type=Path, required=True)
    args = parser.parse_args()
    sys.path.insert(0, str(args.engine.resolve() / 'recomp'))
    report = inspect_dump(args.dump)
    args.out.parent.mkdir(parents=True, exist_ok=True)
    args.out.write_text(json.dumps(report, indent=2, ensure_ascii=False), encoding='utf-8')
    print(json.dumps({'npdm': report['npdm'], 'modules': [
        {k: m[k] for k in ('name', 'build_id', 'text_size', 'memory_size', 'sdk_version', 'evidence')}
        for m in report['modules']], 'romfs': {k: v for k, v in report['romfs'].items() if k != 'shader_files'}}, indent=2))


if __name__ == '__main__':
    main()
