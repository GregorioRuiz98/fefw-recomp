"""Export input/audio compatibility adaptations; preserve the upstream graphics backend."""
import argparse
from pathlib import Path
import subprocess
import tempfile

from bootstrap import RUNTIME_PIN

ALLOWED = {
    'runtime/include/nxrt/platform.h',
    'runtime/src/gfx/platform.cpp',
    'runtime/src/gfx/play/play.cpp',
    'runtime/src/gfx/play/play_types.h',
    'runtime/src/gfx/play/play_ui.cpp',
    'runtime/src/services/audio.cpp',
    'runtime/src/services/audren/Server/BehaviourInfo.h',
    'runtime/src/services/audren/Server/StateUpdater.h',
    'runtime/src/services/audren/audren_types.h',
}


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--engine', type=Path, required=True)
    a = p.parse_args()
    def git(*args):
        return subprocess.check_output(['git', '-C', str(a.engine.resolve()), *args])
    if git('rev-parse', 'HEAD').decode().strip() != RUNTIME_PIN:
        raise ValueError('Unexpected upstream runtime revision')
    changed = set(git('diff', '--name-only', 'HEAD').decode().splitlines())
    if changed - ALLOWED:
        raise ValueError('Unreviewed runtime changes: ' + str(sorted(changed - ALLOWED)))
    out = Path(__file__).resolve().parents[1] / 'patches/renderer.patch'
    # Upstream platform.cpp contains a literal NUL. Its forced text attributes
    # would put that byte in a textual diff; export this one source file as an
    # ASCII Git binary patch instead. Both sides remain open-source C++.
    platform = 'runtime/src/gfx/platform.cpp'
    patch = git('diff', '--binary', 'HEAD', '--', '.', ':!' + platform)
    with tempfile.TemporaryDirectory(prefix='runtime-source-', dir=out.parent) as directory:
        scratch = Path(directory)
        subprocess.run(['git', '-C', str(a.engine.resolve()), 'init', '--quiet', str(scratch)], check=True)
        (scratch / '.gitattributes').write_text('* -diff\n', encoding='utf-8')
        (scratch / 'before.bin').write_bytes(git('show', 'HEAD:' + platform))
        (scratch / 'after.bin').write_bytes((a.engine.resolve() / platform).read_bytes())
        result = subprocess.run(['git', '-C', str(scratch), 'diff', '--no-index', '--binary',
                                 '--', 'before.bin', 'after.bin'], capture_output=True)
        if result.returncode != 1 or b'GIT binary patch' not in result.stdout:
            raise ValueError('Could not export the upstream platform source safely')
        patch += (f'diff --git a/{platform} b/{platform}\n'.encode() +
                  result.stdout.split(b'\n', 1)[1])
    if b'\x00' in patch:
        raise ValueError('Unexpected non-text content in adaptation patch')
    temporary = out.with_suffix('.patch.preparing')
    temporary.write_bytes(patch)
    try:
        git('apply', '--cached', '--check', str(temporary))
        git('apply', '--reverse', '--check', str(temporary))
        temporary.replace(out)
    finally:
        temporary.unlink(missing_ok=True)
    print(f'Runtime adaptation patch verified: {len(patch)} bytes; graphics backend unchanged')


if __name__ == '__main__':
    main()
