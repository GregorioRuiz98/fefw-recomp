"""Generate only own synthetic shaders and check narrow integers in graphics replay."""
import argparse,hashlib,json,struct,subprocess
from pathlib import Path
p=argparse.ArgumentParser();p.add_argument('--work',required=True,type=Path);p.add_argument('--report',required=True,type=Path);a=p.parse_args()
w=a.work.resolve();folder=w/'tests/integer-graphics';folder.mkdir(parents=True,exist_ok=True)
rows=['hash\tresources']
for name,stage in [('vert',0),('frag',1),('cube',1)]:
 code=('FEFW own integer graphics '+name).encode();digest=hashlib.sha1(code).hexdigest()[:16];ctrl=bytearray(0xd8)
 for off,value in [(0,0x98761234),(4,201),(0x38,len(code)),(0x44,len(code))]:struct.pack_into('<I',ctrl,off,value)
 struct.pack_into('<H',ctrl,0x56,stage)
 (folder/(name+'.code')).write_bytes(code);(folder/(name+'.ctrl')).write_bytes(ctrl)
 output=folder/(digest+'.spv')
 subprocess.run([str(w/'build/spirv-tools/tools/spirv-as.exe'),'--target-env','vulkan1.3',str(Path(__file__).with_name('integer_graphics.'+name+'.spvasm')),'-o',str(output)],check=True)
 subprocess.run([str(w/'build/spirv-tools/tools/spirv-val.exe'),'--target-env','vulkan1.3',str(output)],check=True)
 rows.append(digest+'\t')
(folder/'results.tsv').write_text('\n'.join(rows)+'\n')
subprocess.run(['cmake','--build',str(w/'build/fefw'),'--target','fefw_array_tests','--parallel','2'],check=True)
r=subprocess.run([str(w/'build/fefw/fefw_array_tests.exe'),'graphics_numeric'],capture_output=True,text=True,timeout=60,cwd=folder)
a.report.with_suffix('.log').write_text(r.stdout+r.stderr,encoding='utf-8');a.report.write_text(json.dumps({'passed':r.returncode==0,'cases':1,'exit_code':r.returncode},indent=2))
print(r.stdout+r.stderr);raise SystemExit(r.returncode)
