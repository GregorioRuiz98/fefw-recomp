"""Check completed local translation reports before resuming a cancelled build."""
import argparse
import json
from pathlib import Path

MODULES = ('main', 'sdk', 'subsdk0', 'subsdk1', 'subsdk2', 'rtld')


def lift_ready(work):
    try:
        executable = work/'build/fefw/fefw.exe'
        compiled = executable.stat().st_mtime_ns if executable.is_file() else 0
        for name in MODULES:
            report = work / 'lift' / (name + '-lift.json')
            data = json.loads(report.read_text(encoding='utf-8'))
            if data['module'] != name or not data['files']:
                return False
            files = [work/'lift'/name/('module_'+name+'.cpp')]
            files += [work/'lift'/name/file for file in data['files']]
            completed = max(compiled, report.stat().st_mtime_ns)
            if any(not f.is_file() or f.stat().st_size == 0 or f.stat().st_mtime_ns > completed for f in files):
                return False
        return True
    except (OSError, ValueError, KeyError, TypeError):
        return False


if __name__ == '__main__':
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--work', required=True, type=Path)
    a = p.parse_args()
    raise SystemExit(0 if lift_ready(a.work) else 1)
