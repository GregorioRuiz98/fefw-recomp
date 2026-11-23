"""Verify the actual standalone release in an isolated installation, including updates."""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess
import tempfile
import zipfile


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--launcher', type=Path, required=True)
    parser.add_argument('--zip', type=Path, required=True)
    parser.add_argument('--work', type=Path, required=True)
    parser.add_argument('--report', type=Path, required=True)
    args = parser.parse_args()
    args.work.mkdir(parents=True, exist_ok=True)
    checks = []
    def check(ok, name):
        if not ok:
            raise AssertionError(name)
        checks.append(name)
    payload = args.zip.read_bytes()
    check(payload in args.launcher.read_bytes(), 'launcher embeds the exact reviewed public ZIP')
    with zipfile.ZipFile(args.zip) as archive:
        manifest = json.loads(archive.read('public-manifest.json'))
    check(manifest['game_content_included'] is False, 'public payload excludes game content')
    with tempfile.TemporaryDirectory(prefix='release-test-', dir=args.work) as temporary:
        install = Path(temporary) / 'installation with spaces'
        def extract():
            result = subprocess.run([str(args.launcher.resolve()), '--extract-only', '--game', str(install)], capture_output=True, timeout=90)
            check(result.returncode == 0, 'standalone extraction exit 0')
        extract()
        for item in manifest['files']:
            check(digest(install/item['path']) == item['sha256'], 'installed hash: '+item['path'])
        saves = install/'save/profile/Auto'
        saves.parent.mkdir(parents=True)
        saves.write_bytes(b'own synthetic saved-game fixture')
        config = install/'fefw.ini'
        config.write_text('[fs]\nsave_dir=save/profile\n[custom]\nkeep=1\n')
        controls = install/'controls.ini'
        controls.write_text('[pad]\ndeadzone=0.19\n')
        game = install/'fefw.exe'
        game.write_bytes(b'own game-build placeholder; must survive updates')
        originals={p:digest(p) for p in (saves,config,controls,game)}
        extract()
        check(all(digest(p)==sha for p,sha in originals.items()), 'same-release restart preserves game, saves and both INIs')
        (install/'bootstrap.version').write_text('previous bootstrap version')
        extract()
        check(all(digest(p)==sha for p,sha in originals.items()), 'bootstrap update preserves game, saves and both INIs')
    report={'passed':True,'checks':len(checks),'archive_sha256':digest(args.zip),'launcher_sha256':digest(args.launcher),'archive_bytes':len(payload),'launcher_bytes':args.launcher.stat().st_size,'game_content_included':False}
    args.report.write_text(json.dumps(report,indent=2))
    print(json.dumps(report))


if __name__=='__main__':
    main()
