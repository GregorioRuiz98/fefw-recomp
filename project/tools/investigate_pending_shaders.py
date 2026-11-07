"""Read-only shader triage; keeps disassembly in work and exports metadata only."""
import argparse
from collections import Counter
import csv
import hashlib
import json
from pathlib import Path
import re
import struct
import subprocess


def rows(path, key):
    with path.open(encoding='utf-8-sig', newline='') as stream:
        return {row[key]: row for row in csv.DictReader(stream, delimiter='\t')}


def f32(value):
    return struct.unpack('<f', struct.pack('<f', value))[0]


def exact_unsigned_f32(value, upward=False):
    """Integer reference avoids binary64 intermediate rounding for uint64."""
    shift = max(0, value.bit_length() - 24)
    unit = 1 << shift
    quotient, remainder = divmod(value, unit)
    if upward:
        quotient += remainder != 0
    elif remainder * 2 > unit or (remainder * 2 == unit and quotient & 1):
        quotient += 1
    return quotient * unit


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--work', type=Path, required=True)
    parser.add_argument('--report', type=Path, required=True)
    args = parser.parse_args()
    work = args.work.resolve()
    corpus = work / 'shaders/corpus-expanded'
    sources = rows(corpus / 'unique.tsv', 'code_sha1_16')
    translated = rows(work / 'shaders/translated-sdk22/results.tsv', 'hash')
    missing = json.loads((args.report.parent / 'boot-sdk22-missing-shaders.json').read_text(encoding='utf-8-sig'))
    cache = rows(work / 'shaders/validated-sdk22/results.tsv', 'hash')
    disassembler = next((work / 'toolchains/nvdisasm').rglob('nvdisasm.exe'))
    private = work / 'shader-investigation'
    private.mkdir(exist_ok=True)
    additional_checks = []
    for digest, meta in sources.items():
        if meta['stage'] != 'cs':
            continue
        data = (corpus / 'code' / (digest + '.bin')).read_bytes()
        assert hashlib.sha1(data).hexdigest()[:16] == digest
        ctrl = (corpus / 'ctrl' / (meta['first_ctrl'] + '.bin')).read_bytes()
        start = 0 if meta['stage'] == 'cs' else 0x80
        end = min(len(data), struct.unpack_from('<I', ctrl, 0x38)[0]) & ~15
        raw = private / (digest + '.instructions.bin')
        raw.write_bytes(data[start:end])
        process = subprocess.run([str(disassembler), '-b', 'SM86', '-ndf', '-base', hex(start), str(raw)], capture_output=True, text=True, timeout=30)
        (private / (digest + '.sass')).write_text(process.stdout, encoding='utf-8')
        additional_checks.append(dict(hash=digest, source=meta['first_source'],
                                      disassembly_ok=process.returncode == 0,
                                      reads_c0_byte_4='c[0x0][0x4]' in process.stdout,
                                      reads_c0_byte_8='c[0x0][0x8]' in process.stdout,
                                      scaled_shared_accesses=len(re.findall(r'(?:LDS|STS)[^\n]*\[R\d+\.X(?:4|8|16)', process.stdout)),
                                      admitted=digest in cache))
    details = []
    for entry in missing['shaders']:
        digest = entry['hash']
        meta, result = sources[digest], translated[digest]
        code = corpus / 'code' / (digest + '.bin')
        data = code.read_bytes()
        assert hashlib.sha1(data).hexdigest()[:16] == digest
        ctrl = (corpus / 'ctrl' / (meta['first_ctrl'] + '.bin')).read_bytes()
        stage = struct.unpack_from('<H', ctrl, 0x56)[0]
        start = 0 if stage == 5 else 0x80
        end = min(len(data), struct.unpack_from('<I', ctrl, 0x38)[0]) & ~15
        raw = private / (digest + '.instructions.bin')
        raw.write_bytes(data[start:end])
        process = subprocess.run([str(disassembler), '-b', 'SM86', '-ndf', '-base', hex(start), str(raw)], capture_output=True, text=True, timeout=30)
        (private / (digest + '.sass')).write_text(process.stdout, encoding='utf-8')
        (private / (digest + '.stderr')).write_text(process.stderr, encoding='utf-8')
        lines = process.stdout.splitlines()
        texture = [line.strip() for line in lines if any(op in line for op in (' TEX', ' TXQ', ' TLD', 'LDC'))]
        numeric = [line.strip() for line in lines if any(op in line for op in ('F2I.', 'I2F.', 'IMAD.WIDE', 'IMAD.HI', 'ATOMS.CAS'))]
        details.append(dict(hash=digest, stage=meta['stage'], source=meta['first_source'],
                            bnsh_offset=meta['bnsh_offset'], status=result['status'],
                            pool_fallback='poolSize' in result['draw_time_queries'],
                            missing_handle='Failed to find handle source' in result['translator_messages'],
                            resources=result['resources'], queries=result['draw_time_queries'],
                            admitted=digest in cache, disassembly_ok=process.returncode == 0,
                            disassembler_exit=process.returncode, instruction_start=hex(start), instruction_end=hex(end),
                            texture_instruction_count=len(texture), numeric_instruction_count=len(numeric)))
    unresolved = [r for r in translated.values() if r['status'] == 'unresolved-resource']
    groups = Counter()
    for r in unresolved:
        flags = []
        if 'poolSize' in r['draw_time_queries']:
            flags.append('pool-fallback')
        if 'Failed to find handle source' in r['translator_messages']:
            flags.append('missing-handle')
        groups['+'.join(flags) or 'other'] += 1
    # Check the public two-float conversion before considering it for this port.
    vectors = []
    for value, upward in ((16777217, True), (6442451201, False)):
        lo, hi = value & 0xffffffff, value >> 32
        approximate = f32(f32(f32(hi) * f32(4294967296)) + f32(lo))
        expected = exact_unsigned_f32(value, upward)
        vectors.append(dict(input=value, rounding='RP' if upward else 'RN',
                            public_two_float_result=int(approximate), expected_integer_value=expected,
                            matches=approximate == expected))
    report = dict(scope='Static local analysis; no new GPU execution or cache promotion',
                  corpus_unique=sum(1 for _ in (corpus / 'code').glob('*.bin')),
                  representative_profiles=len(sources), active_cache=len(cache),
                  unresolved_categories=dict(groups), boot_requested=len(details),
                  boot_statuses=dict(Counter(x['status'] for x in details)),
                  boot_unresolved_categories=dict(Counter(
                      ('pool-fallback' if x['pool_fallback'] else 'missing-handle' if x['missing_handle'] else 'other')
                      for x in details if x['status'] == 'unresolved-resource')),
                  boot_sources=dict(Counter(x['source'] for x in details)),
                  complete_disassemblies=sum(x['disassembly_ok'] for x in details),
                  compute_census=dict(total=len(additional_checks),
                      complete_disassemblies=sum(x['disassembly_ok'] for x in additional_checks),
                      reads_dispatch_xy=[x for x in additional_checks if x['reads_c0_byte_4'] or x['reads_c0_byte_8']],
                      uses_scaled_shared=[x for x in additional_checks if x['scaled_shared_accesses']]),
                  uint64_conversion_counterexamples=vectors, shaders=details)
    args.report.write_text(json.dumps(report, indent=2, ensure_ascii=False) + '\n', encoding='utf-8')
    print(json.dumps({k: v for k, v in report.items() if k != 'shaders'}, indent=2))


if __name__ == '__main__':
    main()
