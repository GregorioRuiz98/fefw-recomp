"""Audit the staged public source snapshot and reachable Git history, without game data."""
import argparse
import hashlib
import json
from pathlib import Path
import re
import subprocess
from package_public import audit_name, audit_payload

REPO_FILES = {'.gitattributes', 'source-manifest.json', '.github/workflows/public-audit.yml'}
SECRETS = [re.compile(r'gh[pousr]_[A-Za-z0-9]{30,255}'),
           re.compile(r'github_pat_[A-Za-z0-9_]{30,255}'),
           re.compile(r'-----BEGIN (?:RSA |EC |OPENSSH )?PRIVATE KEY-----')]
MAX_SOURCE_BYTES = 2 * 1024 * 1024
ANDROID_SOURCE = re.compile(
    r'^android/(?:README\.md|build\.py|CMakeLists\.txt|AndroidManifest\.xml|'
    r'native/[A-Za-z0-9_]+\.(?:cpp|h)|'
    r'runtime/(?:README\.md|'
    r'include/nxrt/(?:memory|abi|module|dispatch|nxmodule|log|config|gauge|hooks|eh|kernel|svc|game|hipc|ipc|android_kernel|android_trap|android_session|android_time)\.h|'
    r'include/nxrecomp/(?:ctx|cxx|cxx_fp|cxx_ldst|cxx_lse|cxx_neon1|cxx_neon2|fpexact|neon)\.h|'
    r'src/(?:(?:memory_android|module|nso|nxmodule|dispatch_android|abi_dispatch_android|abi_platform_android|log_android|config|ini_merge|gauges_android|session_android|hooks|catch_call|handlers|typeinfo|unwind|kernel_android|svc_android|svc_memory_android|svc_info_android|svc_process_android|kernel_sync_android|svc_threads_android|hipc|ipc|ipc_sm|ipc_stub|svc_ipc_android|log_service_android)\.cpp|(?:elf|eh_internal|kernel_android_internal)\.h|svc_names\.inc)|'
    r'tests/(?:test_(?:memory|nxmodule|loader|dispatch_android|nso_android|hooks|eh|eh_abi3|eh_typeinfo|abi_android|svc_android|kernel_sync|hipc|ipc|threads_android|log_service_android)\.cpp|test_framework\.h|test_runner\.(?:cpp|h)|fixture\.c|prepare_fixtures\.py|data/hipc_vectors\.h)|'
    r'third_party/zbic/(?:README\.md|LICENSE|COPYING|zstd\.(?:c|h)|zstd_errors\.h))|'
    r'java/io/github/gregorioruiz98/fefw/[A-Za-z0-9_]+\.java|'
    r'res/values/[A-Za-z0-9_]+\.xml)$')


def git(repo, *args):
    return subprocess.check_output(['git', '-C', str(repo), *args], stderr=subprocess.PIPE)


def check_name(name):
    if not name or any(ord(c) < 32 for c in name):
        raise ValueError('Invalid source filename')
    if name == 'fefw_setup.exe':
        raise ValueError('The Git repository is source-only; keep the configurator in its reviewed release ZIP')
    if name not in REPO_FILES and not ANDROID_SOURCE.fullmatch(name):
        audit_name(name)
    if name == 'public-manifest.json':
        raise ValueError('Use the source-only manifest for the Git repository')
    if re.search(r'(?:^|/)c_\d+\.cpp$', name):
        raise ValueError('Generated game source is not public source: ' + name)


def check_content(name, data):
    check_name(name)
    # This one unmodified, pinned open-source amalgamation is slightly over 2 MiB.
    pinned_decoder = name == 'android/runtime/third_party/zbic/zstd.c' and hashlib.sha256(data).hexdigest() == '6f8e89a93f760ab2e9e48f8bbac766e254aa748a343e495eaa896ac441772e1f'
    if (len(data) > MAX_SOURCE_BYTES and not pinned_decoder) or b'\x00' in data or data.startswith(b'MZ'):
        raise ValueError('Binary or oversized content in public source: ' + name)
    if name in REPO_FILES or ANDROID_SOURCE.fullmatch(name):
        text = data.decode('utf-8-sig')
    else:
        audit_payload(name, data)
        text = data.decode('utf-8-sig')
    if re.search(r'[A-Za-z]:[\\/]+Users[\\/]+[^\s/\\]+', text, re.I):
        raise ValueError('Personal home path in public source: ' + name)
    if any(pattern.search(text) for pattern in SECRETS):
        raise ValueError('Credential-like content in public source: ' + name)
    if any(ord(c) < 32 and c not in '\n\r\t' for c in text):
        raise ValueError('Unexpected control character in source: ' + name)


def index_files(repo):
    result = {}
    for entry in git(repo, 'ls-files', '--stage', '-z').split(b'\0'):
        if not entry:
            continue
        metadata, raw_name = entry.split(b'\t', 1)
        mode, sha, stage = metadata.decode().split()
        name = raw_name.decode('utf-8')
        check_name(name)
        if mode not in {'100644', '100755'} or stage != '0':
            raise ValueError('Symlink, submodule or unresolved index entry: ' + name)
        data = git(repo, 'cat-file', 'blob', sha)
        check_content(name, data)
        result[name] = data
    if not result:
        raise ValueError('The staged public snapshot is empty')
    return result


def make_manifest(files):
    return {'kind': 'public-source-only', 'game_content_included': False,
            'files': [{'path': name, 'bytes': len(data), 'sha256': hashlib.sha256(data).hexdigest()}
                      for name, data in sorted(files.items()) if name != 'source-manifest.json']}


def audit(repo, history=False, write_manifest=False):
    repo = Path(repo).resolve()
    files = index_files(repo)
    expected = make_manifest(files)
    if write_manifest:
        (repo / 'source-manifest.json').write_text(json.dumps(expected, indent=2) + '\n', encoding='utf-8')
    else:
        if 'source-manifest.json' not in files or json.loads(files['source-manifest.json']) != expected:
            raise ValueError('The staged source manifest is missing or does not match the staged files')
    commits = []
    seen = set()
    if history:
        commits = git(repo, 'rev-list', '--all').decode().splitlines()
        for commit in commits:
            for entry in git(repo, 'ls-tree', '-r', '-z', commit).split(b'\0'):
                if not entry:
                    continue
                metadata, raw_name = entry.split(b'\t', 1)
                mode, kind, sha = metadata.decode().split()
                name = raw_name.decode('utf-8')
                check_name(name)
                if kind != 'blob' or mode not in {'100644', '100755'}:
                    raise ValueError('Unsupported historical Git entry: ' + name)
                if sha not in seen:
                    check_content(name, git(repo, 'cat-file', 'blob', sha))
                    seen.add(sha)
    return {'verdict': 'PASS', 'staged_files': len(files), 'source_bytes': sum(map(len, files.values())),
            'reachable_commits_checked': len(commits), 'historical_blobs_checked': len(seen),
            'unreadable_count': 0, 'game_content_included': False,
            'scope': 'Index and reachable Git objects; provenance and legal review remain separate'}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--repo', type=Path, required=True)
    parser.add_argument('--history', action='store_true')
    parser.add_argument('--write-manifest', action='store_true')
    parser.add_argument('--report', type=Path)
    args = parser.parse_args()
    try:
        report = audit(args.repo, args.history, args.write_manifest)
    except (OSError, ValueError, UnicodeError, subprocess.CalledProcessError) as error:
        report = {'verdict': 'FAIL', 'error': str(error), 'unreadable_count': 'unknown'}
    if args.report:
        args.report.write_text(json.dumps(report, indent=2) + '\n', encoding='utf-8')
    print(json.dumps(report))
    raise SystemExit(0 if report['verdict'] == 'PASS' else 1)


if __name__ == '__main__':
    main()
