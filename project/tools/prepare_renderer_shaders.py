"""Generate the shader cache with the same pinned source as the graphics runtime."""
import argparse
import csv
import hashlib
import json
from pathlib import Path
import subprocess

from bootstrap import RUNTIME_PIN

REVISION = 'goldoire-v015-2'


def digest(path):
    with path.open('rb') as stream:
        return hashlib.file_digest(stream, 'sha256').hexdigest()


def corpus_hashes(corpus):
    with (corpus / 'unique.tsv').open(encoding='utf-8-sig', newline='') as stream:
        rows = list(csv.DictReader(stream, delimiter='\t'))
    # CUDA reconstruction kernels are replaced by the runtime's native upscaler.
    # They are not NVN programs and must not be counted as missing Vulkan shaders.
    hashes = set()
    for row in rows:
        control = corpus / 'ctrl' / (row['first_ctrl'] + '.bin')
        with control.open('rb') as stream:
            if stream.read(4) == b'CUDA':
                continue
        hashes.add(row['code_sha1_16'])
    if not hashes or any(len(h) != 16 or any(c not in '0123456789abcdef' for c in h) for h in hashes):
        raise ValueError('Invalid or empty shader corpus')
    return hashes


def cache_current(cache, hashes):
    try:
        stamp = json.loads((cache / 'renderer-source.json').read_text(encoding='utf-8'))
        files = stamp['files']
        return (stamp['revision'] == REVISION and stamp['runtime_commit'] == RUNTIME_PIN
                and set(files) == hashes
                and (cache / 'renderer-source.ini').read_text(encoding='utf-8') == cache_ini(len(files))
                and all(digest(cache / (h + '.spv')) == sha for h, sha in files.items()))
    except (OSError, ValueError, KeyError, TypeError):
        return False


def cache_ini(count):
    return f'[cache]\ncommit={RUNTIME_PIN}\ncount={count}\n'


def verify_outputs(cache, hashes, validator):
    with (cache / 'results.tsv').open(encoding='utf-8-sig', newline='') as stream:
        rows = {row['hash']: row for row in csv.DictReader(stream, delimiter='\t')}
    if not hashes.issubset(rows):
        raise ValueError('Translated shader results do not cover the complete corpus')
    files = {}
    errors = []
    for n, h in enumerate(sorted(hashes), 1):
        path = cache / (h + '.spv')
        if not rows[h]['status'].startswith('ok') or not path.is_file():
            errors.append({'hash': h, 'error': rows[h].get('error', 'Missing shader')})
            continue
        result = subprocess.run([str(validator), '--target-env', 'vulkan1.3',
                                 '--scalar-block-layout', str(path)], capture_output=True, text=True)
        if result.returncode:
            errors.append({'hash': h, 'error': result.stdout + result.stderr})
        else:
            files[h] = digest(path)
        if n % 1000 == 0:
            print(f'Validated {n}/{len(hashes)} shaders', flush=True)
    (cache / 'renderer-validation.json').write_text(json.dumps(
        {'total': len(hashes), 'passed': len(files), 'errors': errors}, indent=2) + '\n', encoding='utf-8')
    if errors:
        raise ValueError(f'{len(errors)} shaders failed; see renderer-validation.json')
    # Only keep the verified NVN programs in the runtime cache.
    for path in cache.glob('*.spv'):
        if path.stem not in hashes:
            path.unlink()
    stamp = {'revision': REVISION, 'runtime_commit': RUNTIME_PIN, 'files': files}
    temporary = cache / 'renderer-source.json.preparing'
    temporary.write_text(json.dumps(stamp, indent=2) + '\n', encoding='utf-8')
    temporary.replace(cache / 'renderer-source.json')
    temporary = cache / 'renderer-source.ini.preparing'
    temporary.write_text(cache_ini(len(files)), encoding='utf-8')
    temporary.replace(cache / 'renderer-source.ini')


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--work', type=Path, required=True)
    p.add_argument('--dotnet', type=Path, required=True)
    p.add_argument('--romfs', type=Path)
    p.add_argument('--validate-only', action='store_true')
    a = p.parse_args()
    work = a.work.resolve()
    corpus = work / 'shaders/goldoire-corpus'
    cache = work / 'shaders/goldoire-v015'
    renderer = work / 'renderer-engine'
    if not (corpus / 'unique.tsv').is_file():
        if not a.romfs:
            raise ValueError('Provide --romfs for the initial upstream shader extraction')
        subprocess.run([str(work / 'venv/Scripts/python.exe'), '-I',
                        str(renderer / 'tools/shaders/ampere/extract_corpus.py'),
                        '--romfs', str(a.romfs.resolve()), '--main', str(work / 'prepared/exefs/main'),
                        '--exefs', str(work / 'prepared/exefs'), '--out', str(corpus),
                        '--jobs', '1', '--progress'], check=True)
    hashes = corpus_hashes(corpus)
    if cache_current(cache, hashes):
        print(f'Upstream runtime shader cache verified: {len(hashes)} shaders', flush=True)
        return
    for name in ('renderer-source.json', 'renderer-source.ini'):
        (cache / name).unlink(missing_ok=True)
    if not a.validate_only:
        project = renderer / 'tools/shaders/harness/SpirvHarness.csproj'
        subprocess.run([str(a.dotnet), 'build', str(project), '-c', 'Release', '--no-restore', '-m:1',
                        '-nodeReuse:false', '-p:Platform=AnyCPU', '-p:UseSharedCompilation=false',
                        '-p:PatchPython=' + str(work / 'venv/Scripts/python.exe')], check=True)
        harness = renderer / 'tools/shaders/build/bin/Release/net10.0/spirv_harness.dll'
        subprocess.run([str(a.dotnet), str(harness), '--batch', str(corpus), '--out', str(cache),
                        '--jobs', '1', '--no-val'], check=True)
    verify_outputs(cache, hashes, work / 'build/spirv-tools/tools/spirv-val.exe')
    print(f'Upstream runtime shader cache ready: {len(hashes)} shaders', flush=True)


if __name__ == '__main__':
    main()
