"""Execute translated synthetic Ampere compute instructions with independent integer oracles."""
import argparse, hashlib, json, random, struct, subprocess
from pathlib import Path

def ins(lo, hi=0): return struct.pack('<QQ',lo,hi)
def ldc(r,b,off=0,idx=255,typ=4): return ins(0xb82|7<<12|r<<16|idx<<24|b<<54|off<<38,typ<<9)
def mov(r,n): return ins(0x802|7<<12|r<<16|(n&0xffffffff)<<32,0xf00)
def stg(r,off): return ins(0x386|7<<12|20<<24|r<<32|off<<40,1<<8|4<<9|7<<17|1<<20)
def lea(r,a,b,shift): return ins(0x211|7<<12|r<<16|a<<24|b<<32,255|shift<<11|7<<17)
def imad(kind,rd,a,b,c,carry=7,x=False,neg=False,signed=False,pin=7):
    return ins({'wide':0x225,'hi':0x227,'low':0x224}[kind]|7<<12|rd<<16|a<<24|b<<32,c|carry<<17|int(x)<<10|int(neg)<<11|int(signed)<<9|pin<<23)
def cvt(kind,rd,src,rounding=0):
    return ins((0x311 if kind=='f2i' else 0x312)|7<<12|rd<<16|src<<32,
               (3<<11|2<<20|3<<14|1<<16) if kind=='f2i' else (2<<11|3<<20|rounding<<14))
def iset(rd,a,b,cmp=2): return ins(0x20c|7<<12|a<<24|b<<32,rd<<17|7<<20|7<<23|cmp<<12)
def branch(current,target,p=0,neg=True):
    delta=(target-current-16)//4
    return ins(0x947|p<<12|int(neg)<<15|((delta&((1<<48)-1))<<34 & ((1<<64)-1)),7<<23|((delta&((1<<48)-1))>>30))

def u64float(n,mode):
    if not n:return 0
    e=n.bit_length()-1; sh=max(e-23,0); q=n>>sh; rem=n-(q<<sh)
    inc=bool(rem) if mode==2 else (bool(sh) and (rem>(1<<(sh-1)) or (rem==(1<<(sh-1)) and q&1))) if mode==0 else False
    q+=inc
    if e<23:q<<=23-e
    return ((e+127)<<23)+(q-(1<<23))
def floatu64(b):
    e=(b>>23)&255;m=b&0x7fffff
    if e==255 and m:return 1<<63
    if b>>31:return 0
    if e>=191:return (1<<64)-1
    if e<127:return 0
    sh=e-150;m|=1<<23
    return m<<sh if sh>=0 else m>>-sh

def main():
    p=argparse.ArgumentParser();p.add_argument('--work',type=Path,required=True);p.add_argument('--report',type=Path,required=True);a=p.parse_args()
    w=a.work.resolve();out=w/'tests/compute-numeric';out.mkdir(parents=True,exist_ok=True)
    rng=random.Random(20261104); cases=[]
    def emit(name,body,inputs,expected,local=1,custom=False):
        # 32 bytes per test row; c3 inputs are loaded at runtime, never constant folded.
        prefix=ins(0x919|7<<12|30<<16,37<<8)+mov(31,0)+lea(29,30,31,5)
        prefix+=ldc(4,3,0,29,5)+ldc(6,3,8,29,5)+ldc(20,0,0x90,typ=5)+lea(20,29,20,0)
        code=(b'' if custom else prefix)+body+ins(0x794d,0x000fea0003800000)
        ctrl=bytearray(0xe0)
        for off,val in ((0,0x98761234),(4,201),(0x30,len(ctrl)),(0x38,len(code)),(0x40,len(code)),(0x44,len(code)),(0x58,local),(0x5c,1),(0x60,1),(0x50,16)):struct.pack_into('<I',ctrl,off,val)
        struct.pack_into('<H',ctrl,0x56,5)
        (out/'code.bin').write_bytes(code);(out/'ctrl.bin').write_bytes(ctrl)
        (out/(name+'.sass.bin')).write_bytes(code);(out/(name+'.ctrl.bin')).write_bytes(ctrl)
        cmd=[w/'toolchains/dotnet/dotnet.exe',w/'engine/tools/shaders/build/bin/Release/net10.0/spirv_harness.dll',out/'code.bin',out/'ctrl.bin','-o',out/(name+'.spv')]
        result=subprocess.run(list(map(str,cmd)),capture_output=True,text=True,timeout=45)
        if result.returncode:raise RuntimeError(result.stdout+result.stderr)
        subprocess.run([str(w/'build/spirv-tools/tools/spirv-val.exe'),'--target-env','vulkan1.3',str(out/(name+'.spv'))],check=True)
        words=struct.unpack('<'+'I'*((out/(name+'.spv')).stat().st_size//4),(out/(name+'.spv')).read_bytes())
        cursor=5; atomics=0
        while cursor<len(words):
            atomics += (words[cursor]&65535)==230
            cursor += words[cursor]>>16
        assert atomics == (1 if name.startswith('cas_') else 0), (name,atomics)
        def blob(rows):return b''.join(struct.pack('<8I',*(list(row)+[0xcdcdcdcd]*(8-len(row)))) for row in rows)
        (out/(name+'.input')).write_bytes(blob(inputs));(out/(name+'.expected')).write_bytes(blob(expected))
        cases.append((name,len(inputs),len(expected)))
    values={0,1,(1<<64)-1,16777217,6442451201}
    for bit in range(64):
        for d in (-1,0,1):
            n=(1<<bit)+d
            if 0<=n<1<<64:values.add(n)
    values=sorted(values)+[rng.getrandbits(64) for _ in range(1000)]
    for mode in range(4):
        emit('i2f'+str(mode),cvt('i2f',4,4,mode)+stg(4,0),[[n&0xffffffff,n>>32,0,0] for n in values],[[u64float(n,mode)] for n in values])
    floats=[0,0x80000000,1,0x007fffff,0x00800000,0x3f7fffff,0x3f800000,0x3fc00000,0x5f7fffff,0x5f800000,0x7f800000,0xff800000,0x7fc00000,0xff800001]
    floats += [(e<<23)|m for e in range(256) for m in (0,1,0x7fffff)]
    floats += [rng.getrandbits(32) for _ in range(1000)]
    emit('f2i',cvt('f2i',4,4)+stg(4,0)+stg(5,4),[[n,0,0,0] for n in floats],[[floatu64(n)&0xffffffff,floatu64(n)>>32] for n in floats])
    rows=[[a,b,cl,ch] for a,b,cl,ch in ((0,0,0,0),(0xffffffff,0xffffffff,0xffffffff,0xffffffff),(1,1,0xffffffff,0xffffffff),(1,1,0xffffffff,0),(0x80000000,2,0,0))]
    rows += [[rng.getrandbits(32) for _ in range(4)] for _ in range(1000)]
    for kind in ('wide','hi'):
      for signed in (False,True):
       for cin in (None,0,1):
        body=imad(kind,6,4,5,6,carry=0,x=cin is not None,signed=signed,pin=7)
        if cin==0:
            lo,hi=struct.unpack('<QQ',body);body=ins(lo,hi|1<<26)
        # SEL materializes carry after destination overlaps the addend.
        body+=mov(10,1)+mov(11,0)+ins(0x207|7<<12|12<<16|10<<24|11<<32,0<<23)
        body+=stg(6,0)+(stg(7,4) if kind=='wide' else b'')+stg(12,8)
        expected=[]
        for av,bv,cl,ch in rows:
            aa=av-(1<<32) if signed and av>>31 else av;bb=bv-(1<<32) if signed and bv>>31 else bv
            total=((aa*bb)&((1<<64)-1))+(ch<<32)+cl+(cin or 0)
            expected.append([total&0xffffffff,(total>>32)&0xffffffff,int(total>>64!=0)] if kind=='wide' else [(total>>32)&0xffffffff,0xcdcdcdcd,int(total>>64!=0)])
        emit(f'imad_{kind}_{int(signed)}_{cin}',body,rows,expected)
    for cin in (0,1):
        body=imad('low',6,4,5,6,x=True,neg=True,signed=True)
        if cin==0:
            lo,hi=struct.unpack('<QQ',body);body=ins(lo,hi|1<<26)
        emit('imad_not_'+str(cin),body+stg(6,0),rows,[[(av*bv+(~cl)+cin)&0xffffffff] for av,bv,cl,ch in rows])
    # CAS success and failure, including expected/destination and replacement/destination aliasing.
    for alias in (4,5,6):
        body=ins(0x388|7<<12|255<<24|6<<32,4<<9)
        body+=ins(0x38d|7<<12|alias<<16|255<<24|4<<32,5|4<<9)
        body+=stg(alias,0)+ins(0x984|7<<12|8<<16|255<<24,4<<9)+stg(8,4)
        rr=[[0,11,0,0],[7,12,7,0],[6,12,7,0],[0xffffffff,0,0xffffffff,0]]
        emit('cas_alias'+str(alias),body,rr,[[int(c==v),n if c==v else v] for c,n,v,_ in rr])
    # 64 contending invocations retry until each has added one to shared memory.
    body=ins(0x919|7<<12|30<<16,33<<8)+mov(31,0)+iset(0,30,31)
    store=ins(0x388|0<<12|255<<24|31<<32,4<<9);body+=store+ins(0xb1d|7<<12)
    body+=mov(5,1)
    loop=len(body)
    body+=ins(0x984|7<<12|4<<16|255<<24,4<<9)
    body+=imad('low',6,5,5,4)
    body+=ins(0x38d|7<<12|6<<16|255<<24|4<<32,6|4<<9)
    body+=iset(0,6,5)
    body+=branch(len(body),loop)
    body+=ins(0xb1d|7<<12)+iset(0,30,31)
    # All lanes reach the barrier; only lane zero publishes the sum.
    body+=ins(0x984|7<<12|8<<16|255<<24,4<<9)+ldc(20,0,0x90,typ=5)
    lo,hi=struct.unpack('<QQ',stg(8,0));body+=ins(lo&~(7<<12),hi)
    emit('cas_contention',body,[[0,0,0,0]],[[64]],local=64,custom=True)
    (out/'cases.txt').write_text(''.join(f'{n} {count} {ecount}\n' for n,count,ecount in cases))
    subprocess.run(['cmake','--build',str(w/'build/fefw'),'--target','fefw_numeric_tests','--parallel','1'],check=True)
    result=subprocess.run([str(w/'build/fefw/fefw_numeric_tests.exe'),'compute_numeric'],cwd=out,capture_output=True,text=True,timeout=90)
    log=result.stdout+result.stderr;print(log);a.report.with_suffix('.log').write_text(log)
    a.report.write_text(json.dumps(dict(exit_code=result.returncode,shader_modules=len(cases),numeric_rows=sum(c[1] for c in cases),passed=log.count('[  OK  ]'),validation_layer_enabled='validation enabled' in log, source_sha256={name:hashlib.sha256((w/'engine'/name).read_bytes()).hexdigest() for name in ('tools/shaders/ampere/frontend/AmpereEmit.cs','tools/shaders/harness/translator/spirv-call-args.patch')}),indent=2)+'\n')
    if result.returncode:raise SystemExit(result.returncode)
if __name__=='__main__':main()
