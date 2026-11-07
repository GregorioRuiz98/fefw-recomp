"""Set up a pinned engine and private Python environment in the chosen workspace."""
import argparse
from pathlib import Path
import shutil
import subprocess
import sys

PIN = '2c29515b23a6d78d967775fd7e9f24f50402cca7'
PROJECT = Path(__file__).resolve().parents[1]


def run(*args, **kwargs):
    subprocess.run([str(x) for x in args], check=True, **kwargs)


def main():
    p = argparse.ArgumentParser()
    p.add_argument('--work', required=True, type=Path)
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
    patch = PROJECT / 'patches' / 'engine.patch'
    check = subprocess.run(['git', '-C', str(engine), 'apply', '--check', str(patch)], capture_output=True)
    if check.returncode == 0:
        run('git', '-C', engine, 'apply', patch)
    else:
        reverse = subprocess.run(['git', '-C', str(engine), 'apply', '--reverse', '--check', str(patch)], capture_output=True)
        if reverse.returncode:
            raise RuntimeError('Engine changes conflict with engine.patch; no changes were overwritten')
    config = engine / 'recomp' / 'games' / 'fefw' / 'config.py'
    if config.exists() and config.read_bytes() != (PROJECT / 'config.py').read_bytes():
        raise RuntimeError('Existing Fire Emblem configuration differs; it was preserved')
    config.parent.mkdir(parents=True, exist_ok=True)
    shutil.copyfile(PROJECT / 'config.py', config)
    python = work / 'venv' / 'Scripts' / 'python.exe'
    if not python.exists():
        run(sys.executable, '-m', 'venv', work / 'venv')
    run(python, '-m', 'pip', 'install', '-r', PROJECT / 'requirements.txt')
    print(f'Engine and environment prepared in {work}')


if __name__ == '__main__':
    main()
