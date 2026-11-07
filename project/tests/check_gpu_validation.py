"""Own integer texture, slice-view and presentation lifetime regressions."""
import argparse
import hashlib
import json
import struct
import subprocess
from pathlib import Path

p = argparse.ArgumentParser()
p.add_argument('--work', type=Path, required=True)
p.add_argument('--report', type=Path, required=True)
a = p.parse_args()
w = a.work.resolve()
folder = w / 'tests/integer-texture'
folder.mkdir(parents=True, exist_ok=True)
code = b'FEFW integer texture regression'
digest = hashlib.sha1(code).hexdigest()[:16]
ctrl = bytearray(0xd8)
for offset, value in ((0,0x98761234),(4,201),(0x38,len(code)),(0x44,len(code)),(0x58,1),(0x5c,1),(0x60,1)):
    struct.pack_into('<I',ctrl,offset,value)
struct.pack_into('<H',ctrl,0x56,5)
ctrl[0xb2] = 1
ctrl[0xb3] = 2
(folder/'code.bin').write_bytes(code)
(folder/'ctrl.bin').write_bytes(ctrl)
(folder/'results.tsv').write_text(f'hash\tresources\n{digest}\tsb[c0+0x4]=s1b0\n',encoding='utf-8')
spv = folder/(digest+'.spv')
subprocess.run([str(w/'build/spirv-tools/tools/spirv-as.exe'),'--target-env','vulkan1.3',str(Path(__file__).with_name('integer_texture.spvasm')),'-o',str(spv)],check=True)
subprocess.run([str(w/'build/spirv-tools/tools/spirv-val.exe'),'--target-env','vulkan1.3',str(spv)],check=True)
subprocess.run(['cmake','--build',str(w/'build/fefw'),'--target','fefw_array_tests','--parallel','1'],check=True)
r = subprocess.run([str(w/'build/fefw/fefw_array_tests.exe'),'gpu_validation'],capture_output=True,text=True,timeout=90,cwd=folder)
a.report.with_suffix('.log').write_text(r.stdout+r.stderr,encoding='utf-8')
passed = r.returncode == 0 and r.stdout.count('[  OK  ]') == 3
for name in ('signed.spv','unsigned.spv'):
    subprocess.run([str(w/'build/spirv-tools/tools/spirv-val.exe'),'--target-env','vulkan1.3',str(folder/name)],check=True)
a.report.write_text(json.dumps(dict(passed=passed,cases=3,exit_code=r.returncode,
    validation_layer_available='validation is not installed' not in r.stderr),indent=2),encoding='utf-8')
print(r.stdout+r.stderr)
raise SystemExit(0 if passed else 1)
