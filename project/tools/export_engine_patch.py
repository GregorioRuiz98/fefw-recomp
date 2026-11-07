"""Export the project's engine modifications without staging or changing the checkout."""
import argparse
from pathlib import Path
import re
import subprocess

p = argparse.ArgumentParser(description=__doc__)
p.add_argument('--engine', type=Path, required=True)
p.add_argument('--out', type=Path, default=Path(__file__).resolve().parents[1]/'patches/engine.patch')
a = p.parse_args()
engine, out = a.engine.resolve(), a.out.resolve()
extra = ['tools/shaders/harness/translator/ampere-'+s+'.patch' for s in ('array-base','cbuf-banks','literal-handles','pool-offsets')]
extra += ['tools/shaders/licenses/nxnative-MIT.txt', 'runtime/src/gfx/nvn/nvn_spatial.spvasm', 'runtime/src/gfx/nvn/nvn_spatial_shader.inc']
extra += ['runtime/include/nxrt/gfx.h', 'runtime/src/gfx/vk_device.cpp',
          'runtime/src/gfx/nvn/nvn_spirv_patch.cpp', 'runtime/src/gfx/nvn/nvn_spirv_patch.h']
known = set(re.findall(r'^diff --git a/(\S+) b/\S+$', out.read_text(encoding='utf-8'), re.M)) | set(extra)


def git(*args, check=True):
    return subprocess.run(['git','-C',str(engine),*args], capture_output=True, check=check)


changed = set(git('diff','--name-only','HEAD','-z').stdout.decode().strip('\0').split('\0')) - {''}
if changed - known:
    raise RuntimeError('Unrelated engine edits preserved; review before exporting: '+str(sorted(changed-known)))
patch = git('diff','--binary','HEAD').stdout
for name in extra:
    if git('ls-files','--error-unmatch',name,check=False).returncode == 0:
        continue
    diff = git('diff','--no-index','--binary','--','/dev/null',name,check=False)
    if diff.returncode != 1:
        raise RuntimeError('Could not export '+name+': '+diff.stderr.decode(errors='replace'))
    patch += diff.stdout
temp = out.with_suffix('.patch.tmp')
temp.write_bytes(patch)
try:
    git('apply','--check','--cached',str(temp))
    git('apply','--reverse','--check',str(temp))
    git('diff','--check')
    temp.replace(out)
finally:
    temp.unlink(missing_ok=True)
print(f'Engine patch verified: {len(patch)} bytes; index and working files preserved')
