import argparse,hashlib,json,os,struct,subprocess
from pathlib import Path
p=argparse.ArgumentParser();p.add_argument('--work',type=Path,required=True);p.add_argument('--report',type=Path,required=True);a=p.parse_args()
w=a.work.resolve();folder=w/'tests/texture-arrays';folder.mkdir(parents=True,exist_ok=True)
code=b'FEFW array test!';digest=hashlib.sha1(code).hexdigest()[:16];ctrl=bytearray(0xd8)
for offset,value in ((0,0x98761234),(4,201),(0x38,len(code)),(0x44,len(code)),(0x58,32),(0x5c,1),(0x60,1)): struct.pack_into('<I',ctrl,offset,value)
struct.pack_into('<H',ctrl,0x56,5);ctrl[0xb2]=1
(folder/'code.bin').write_bytes(code);(folder/'ctrl.bin').write_bytes(ctrl)
subprocess.run(['cmake','--build',str(w/'build/fefw'),'--target','fefw_array_tests','--parallel','1'],check=True)
cases=[];logs=[]
for cube in (False,True):
 asm=Path(__file__).with_name('texture_pool.spvasm').read_text(encoding='utf-8-sig')
 if not cube:
  asm=asm.replace(' %float Cube ',' %float 2D ').replace('%v3f = OpTypeVector %float 3','%v3f = OpTypeVector %float 2').replace('%f1 = OpConstant %float 1','%f1 = OpConstant %float 2').replace('%v3f %f1 %f0 %f0','%v3f %f1 %f1')
 source=folder/'pool.spvasm';source.write_text(asm,encoding='utf-8');spv=folder/(digest+'.spv')
 for tool,tail in [('spirv-as.exe',['-o',str(spv)]),('spirv-val.exe',[])]:
  subprocess.run([str(w/'build/spirv-tools/tools'/tool),'--target-env','vulkan1.3',str(source if tool=='spirv-as.exe' else spv)]+tail,check=True)
 kind='TextureCube' if cube else 'Texture2D'
 (folder/'results.tsv').write_text(f'hash\tresources\n{digest}\tcb6=s0b7 sb[c0+0x4]=s1b0 tex{kind}[c0:1073741824x32768]=s2b640 texNone[c0:1073741824x512]=s2b641\n',encoding='utf-8')
 env=dict(os.environ,FEFW_POOL_ARRAY='1',FEFW_ARRAY_BANK='6',FEFW_ARRAY_BASE='0',FEFW_ARRAY_CUBE=str(int(cube)),FEFW_LITERAL_TEXTURE='-1')
 run=subprocess.run([str(w/'build/fefw/fefw_array_tests.exe'),'texture_arrays'],cwd=folder,env=env,capture_output=True,text=True,timeout=90)
 logs.append(run.stdout+run.stderr);cases.append(dict(shape=kind,exit_code=run.returncode,passed=run.stdout.count('[  OK  ]')));print(cases[-1],flush=True)
 if run.returncode:print(logs[-1]);break
passed=len(cases)==2 and all(c['exit_code']==0 and c['passed']==1 for c in cases)
fingerprints={str(path.relative_to(w)).replace('\\','/'):hashlib.sha256(path.read_bytes()).hexdigest() for path in [w/'engine/runtime/src/gfx/nvn'/name for name in ('nvn_replay.cpp','nvn_pipeline.cpp','nvn_shaders.cpp','nvn_shaders.h','nvn_gpu.cpp','nvn_gpu_texture.cpp')]}
a.report.with_suffix('.log').write_text('\n'.join(logs),encoding='utf-8');a.report.write_text(json.dumps(dict(passed=passed,source_sha256=fingerprints,cases=cases,samples=128 if passed else 0,validation_layer_enabled=all('Vulkan validation layer: enabled' in x for x in logs)),indent=2),encoding='utf-8')
raise SystemExit(0 if passed else 1)
