"""Set up a pinned engine and private Python environment in the chosen workspace."""
import argparse
import hashlib
from pathlib import Path
import shutil
import subprocess
import sys

PIN = '2c29515b23a6d78d967775fd7e9f24f50402cca7'
PROJECT = Path(__file__).resolve().parents[1]
PREVIOUS_CONFIG_SHA256 = '133e98f3911000e82c788cb5a97932ff879a24c4c2521f78b0e1f8eec2db8891'


def run(*args, **kwargs):
    subprocess.run([str(x) for x in args], check=True, **kwargs)


def main():
    p = argparse.ArgumentParser()
    p.add_argument('--work', required=True, type=Path)
    p.add_argument('--engine-only', action='store_true', help='Update only the pinned engine/config; reuse existing tools')
    a = p.parse_args()
    work = a.work.resolve()
    engine = work / 'engine'
    work.mkdir(parents=True, exist_ok=True)
    if not engine.exists():
        run('git', 'clone', 'https://github.com/drakolordx7/pokopia-recomp.git', engine)
        run('git', '-C', engine, 'checkout', '--detach', PIN)
    head = subprocess.check_output(['git', '-C', str(engine), 'rev-parse', 'HEAD'], text=True).strip()
    if head != PIN:
        raise RuntimeError(f'Engine is at {head}, expected {PIN}; existing checkout was preserved')
    config = engine / 'recomp' / 'games' / 'fefw' / 'config.py'
    wanted_config = (PROJECT / 'config.py').read_bytes()
    if config.exists() and config.read_bytes() != wanted_config and hashlib.sha256(config.read_bytes()).hexdigest() != PREVIOUS_CONFIG_SHA256:
        raise RuntimeError('Existing Fire Emblem configuration differs; it was preserved')
    patch = PROJECT / 'patches' / 'engine.patch'
    check = subprocess.run(['git', '-C', str(engine), 'apply', '--check', str(patch)], capture_output=True)
    if check.returncode == 0:
        run('git', '-C', engine, 'apply', patch)
    else:
        reverse = subprocess.run(['git', '-C', str(engine), 'apply', '--reverse', '--check', str(patch)], capture_output=True)
        if reverse.returncode:
            previous = None
            for name in ('engine-preview8.patch', 'engine-preview7.patch', 'engine-preview6.patch', 'engine-preview5.patch', 'engine-preview3.patch', 'engine-preview2.patch', 'engine-preview1.patch'):
                candidate = PROJECT / 'patches' / name
                if candidate.is_file() and subprocess.run(
                        ['git', '-C', str(engine), 'apply', '--reverse', '--check', str(candidate)], capture_output=True).returncode == 0:
                    previous = candidate
                    break
            clean_index = subprocess.run(['git', '-C', str(engine), 'diff', '--cached', '--quiet'], capture_output=True)
            new_check = subprocess.run(['git', '-C', str(engine), 'apply', '--cached', '--check', str(patch)], capture_output=True)
            if previous is None or clean_index.returncode or new_check.returncode:
                raise RuntimeError('Engine changes conflict with engine.patch; no changes were overwritten')
            run('git', '-C', engine, 'apply', '--reverse', previous)
            try:
                run('git', '-C', engine, 'apply', patch)
            except Exception:
                run('git', '-C', engine, 'apply', previous)
                raise
    config.parent.mkdir(parents=True, exist_ok=True)
    config.write_bytes(wanted_config)
    if a.engine_only:
        print('Pinned engine and native patch configuration are up to date')
        return
    python = work / 'venv' / 'Scripts' / 'python.exe'
    if not python.exists():
        run(sys.executable, '-m', 'venv', work / 'venv')
    run(python, '-m', 'pip', 'install', '-r', PROJECT / 'requirements.txt')
    print(f'Engine and environment prepared in {work}')


if __name__ == '__main__':
    main()
