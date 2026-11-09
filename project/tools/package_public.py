"""Build a strictly allowlisted public configurator/source package, never a game package."""
import argparse
import hashlib
import json
from pathlib import Path
import re
import shutil
import zipfile

ROOT_FILES = {
    'CMakeLists.txt', 'config.py', 'requirements.txt', 'supported-build.json',
    'LICENSE', 'DEPENDENCIAS.md', 'CONTROLES-MANDO.md', 'INCIDENCIAS-Y-CONFIGURADOR.md', '.gitignore',
    'FireEmblemRecomp.ps1', 'PUBLIC-README.md', 'LEGAL.md', 'NOTICE.md',
    'THIRD_PARTY_NOTICES.md', 'CONTRIBUTING.md', 'GITHUB-PUBLISH.md', 'UPSTREAM-INTEGRATION.md',
}
PUBLIC_ROOT_DOCUMENTS = {'LICENSE', 'LEGAL.md', 'NOTICE.md', 'THIRD_PARTY_NOTICES.md', 'CONTRIBUTING.md', 'GITHUB-PUBLISH.md'}
ROOT_DIRS = {'src', 'tools', 'tests', 'licenses', 'patches'}
SUFFIXES = {'.cpp', '.h', '.in', '.rc', '.manifest', '.py', '.ps1', '.md', '.txt', '.json', '.patch', '.tt', '.comp', '.vert', '.frag', '.glsl', '.spvasm'}
SKIP_DIRS = {'__pycache__', 'bin', 'obj', 'build', '.venv', '.git'}
DENIED_PARTS = {'exefs', 'romfs', 'lift', 'shader_cache', 'corpus', 'corpus-expanded', 'save', 'reports', 'prepared'}
DENIED_SUFFIXES = {'.nsp', '.nca', '.nso', '.xci', '.nsz', '.xcz', '.spv', '.bin', '.png', '.jpg', '.webm', '.wav', '.ogg', '.lib', '.obj', '.pdb', '.dll', '.exe', '.bmp', '.keys', '.pem', '.key', '.7z', '.rar'}


def allowed_source(path):
    parts = path.parts
    if path.name in {'local-build.json', 'fire-emblem-local.json', 'package.json'}:
        return False
    if path.name.lower() == '.env' or path.name.lower().startswith('.env.'):
        return False
    if any(p.lower() in SKIP_DIRS for p in parts):
        return False
    if len(parts) == 1:
        return parts[0] in ROOT_FILES
    return parts[0] in ROOT_DIRS and path.suffix.lower() in SUFFIXES


def audit_name(name):
    p = Path(name)
    if p.is_absolute() or '..' in p.parts or '\\' in name or re.match(r'^[a-zA-Z]:', name):
        raise ValueError(f'Unsafe public path: {name}')
    if name == 'fefw_setup.exe':
        return
    if any(part.lower() in DENIED_PARTS for part in p.parts):
        raise ValueError(f'Private directory in public package: {name}')
    if p.suffix.lower() in DENIED_SUFFIXES or re.match(r'^(c_\d+|module_(main|sdk|rtld|subsdk\d+))\.cpp$', p.name):
        raise ValueError(f'Private/generated artifact in public package: {name}')
    if name in {'README.md', '.gitignore', 'public-manifest.json'} | PUBLIC_ROOT_DOCUMENTS:
        return
    if name in {'tools/Prepare-Game.ps1', 'tools/Prepare-Dependencies.ps1'}:
        return
    if len(p.parts) < 2 or p.parts[0] != 'project' or not allowed_source(Path(*p.parts[1:])):
        raise ValueError(f'File is not on the public allowlist: {name}')


def audit_payload(name, payload):
    audit_name(name)
    if name == 'fefw_setup.exe':
        if not payload.startswith(b'MZ') or len(payload) > 10 * 1024 * 1024:
            raise ValueError('The standalone configurator must be a small independent Windows executable')
        return
    if payload[:4] in {b'NSO0', b'\x03\x02\x23\x07', b'NCZ\x00'}:
        raise ValueError(f'Game or shader binary content: {name}')
    # Release sources must not contain the developer's local dump paths.
    text = payload.decode('utf-8-sig')
    if re.search(r'[A-Za-z]:[\\/]+Users[\\/]+[^\s/\\]+', text, re.I):
        raise ValueError(f'Personal absolute path in public source: {name}')


def create(source, setup_exe, destination):
    source = source.resolve()
    destination = destination.resolve()
    if destination == source or destination.is_relative_to(source):
        raise ValueError('Public output must be outside the source tree')
    if destination.exists() and any(destination.iterdir()):
        raise ValueError('Use an empty public destination; existing files are preserved')
    files = {}
    for path in source.rglob('*'):
        if not path.is_file():
            continue
        rel = path.relative_to(source)
        if not allowed_source(rel):
            continue
        if path.is_symlink() or not path.resolve().is_relative_to(source):
            raise ValueError(f'External source link: {rel}')
        data = path.read_bytes()
        if path.suffix.lower() == '.ps1':
            data = data.decode('utf-8-sig').encode('utf-8-sig')  # Windows PowerShell 5.1
        files['project/' + rel.as_posix()] = data
    files['README.md'] = (source / 'PUBLIC-README.md').read_bytes()
    for name in PUBLIC_ROOT_DOCUMENTS:
        if (source / name).is_file():
            files[name] = (source / name).read_bytes()
    files['fefw_setup.exe'] = setup_exe.read_bytes()
    for name in ('Prepare-Game.ps1', 'Prepare-Dependencies.ps1'):
        path = source / 'tools' / name
        if path.is_file():
            files['tools/' + name] = path.read_text(encoding='utf-8-sig').encode('utf-8-sig')
    files['.gitignore'] = b'/work/\n/exefs/\n/romfs/\n/shader_cache/\n/save/\n/fefw.exe\n/fefw_setup.exe\n/fefw.map\n/fefw.pdb\n/fefw.ini\n/fefw.features\n/controls.ini\n/preparation.*\n**/__pycache__/\n*.pyc\n*.log\n*.spv\n*.keys\n*.nsp\n*.nca\n*.nso\n*.xci\n*.nsz\n*.xcz\n*.dll\n*.pdb\n*.obj\n*.lib\n*.zip\n*.7z\n*.rar\n.env\n.env.*\n'
    for name, data in files.items():
        audit_payload(name, data)
    manifest = {
        'kind': 'public-tools-and-configurator-only',
        'game_content_included': False,
        'game_executable_generation': 'local-from-user-supplied-folder',
        'files': [{'path': name, 'bytes': len(data), 'sha256': hashlib.sha256(data).hexdigest()}
                  for name, data in sorted(files.items())],
    }
    files['public-manifest.json'] = (json.dumps(manifest, indent=2) + '\n').encode()
    for name, data in files.items():
        target = destination / name
        target.parent.mkdir(parents=True, exist_ok=True)
        target.write_bytes(data)
    return manifest


def verify(destination):
    manifest = json.loads((destination / 'public-manifest.json').read_text(encoding='utf-8'))
    expected = {f['path']: f for f in manifest['files']}
    actual = {p.relative_to(destination).as_posix() for p in destination.rglob('*') if p.is_file()}
    if actual != set(expected) | {'public-manifest.json'}:
        raise ValueError('The public directory contains missing or additional files')
    for name, item in expected.items():
        data = (destination / name).read_bytes()
        audit_payload(name, data)
        if len(data) != item['bytes'] or hashlib.sha256(data).hexdigest() != item['sha256']:
            raise ValueError(f'Public integrity failure: {name}')
    return manifest


def main():
    p = argparse.ArgumentParser()
    p.add_argument('--source', type=Path, default=Path(__file__).resolve().parents[1])
    p.add_argument('--setup-exe', type=Path)
    p.add_argument('--out', type=Path, required=True)
    p.add_argument('--verify-only', action='store_true')
    p.add_argument('--zip', type=Path)
    a = p.parse_args()
    if not a.verify_only:
        if not a.setup_exe:
            p.error('--setup-exe is required when creating a package')
        create(a.source, a.setup_exe, a.out)
    manifest = verify(a.out)
    if a.zip:
        if a.zip.resolve().is_relative_to(a.out.resolve()):
            raise ValueError('The ZIP must be outside the audited public directory')
        with zipfile.ZipFile(a.zip, 'w', zipfile.ZIP_DEFLATED) as archive:
            for path in sorted(a.out.rglob('*')):
                if path.is_file():
                    archive.write(path, path.relative_to(a.out).as_posix())
        with zipfile.ZipFile(a.zip) as archive:
            for name in archive.namelist():
                audit_payload(name, archive.read(name))
    print(json.dumps({'public_files': len(manifest['files']), 'game_content_included': False, 'integrity': 'passed'}))


if __name__ == '__main__':
    main()
