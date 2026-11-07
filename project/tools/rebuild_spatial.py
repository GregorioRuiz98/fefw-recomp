"""Rebuild the project's own auxiliary shader; no game input is used."""
import argparse
from pathlib import Path
import struct
import subprocess
import tempfile

p = argparse.ArgumentParser(description=__doc__)
p.add_argument('--engine', type=Path, required=True)
p.add_argument('--spirv-tools', type=Path, required=True)
a = p.parse_args()
root = a.engine / 'runtime/src/gfx/nvn'
with tempfile.TemporaryDirectory(dir=Path.cwd()) as temp:
    blob = Path(temp) / 'spatial.spv'
    for name, args in [('spirv-as', [str(root/'nvn_spatial.spvasm'), '-o', str(blob)]),
                       ('spirv-val', [str(blob)])]:
        subprocess.run([str(a.spirv_tools/(name+'.exe')), '--target-env', 'vulkan1.0', *args], check=True)
    data = blob.read_bytes()
    words = struct.unpack('<'+'I'*(len(data)//4), data)
    lines = ['// SPDX-License-Identifier: GPL-3.0-or-later',
             '// Generated from nvn_spatial.spvasm by tools/rebuild_spatial.py.',
             'constexpr uint32_t kSpatialShader[] = {']
    lines += ['    '+', '.join(f'0x{w:08x}' for w in words[i:i+8])+',' for i in range(0,len(words),8)]
    (root/'nvn_spatial_shader.inc').write_text('\n'.join(lines+['};', '']), encoding='utf-8')
