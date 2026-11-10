"""Build a signed Android diagnostics APK with the official SDK/NDK tools.

No downloads, Gradle, game files, third-party Java libraries or global changes.
The private test signing key is created in the output workspace, never in source.
"""
import argparse
import base64
import hashlib
import json
import os
from pathlib import Path
import shutil
import struct
import subprocess
import sys
import zipfile


def run(args, env=None):
    subprocess.run([str(a) for a in args], check=True, env=env)


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--sdk',required=True,type=Path)
    parser.add_argument('--ndk',required=True,type=Path)
    parser.add_argument('--java-home',required=True,type=Path)
    parser.add_argument('--work',required=True,type=Path)
    parser.add_argument('--project',type=Path)
    parser.add_argument('--abi',choices=['arm64-v8a','x86_64'],default='arm64-v8a')
    parser.add_argument('--api',type=int,default=36)
    parser.add_argument('--build-tools',default='36.0.0')
    args=parser.parse_args()
    source=Path(__file__).resolve().parent
    project=(args.project or (source.parent if (source.parent/'supported-build.json').exists() else source.parent/'project')).resolve()
    work=args.work.resolve();work.mkdir(parents=True,exist_ok=True)
    if work.is_relative_to(source):raise ValueError('Keep build output outside the Android source directory')
    sdk=args.sdk.resolve();ndk=args.ndk.resolve();java=args.java_home.resolve()/'bin'
    tools=sdk/'build-tools'/args.build_tools;android_jar=sdk/f'platforms/android-{args.api}/android.jar'
    for p in [java/'java.exe',java/'javac.exe',tools/'aapt2.exe',tools/'lib/d8.jar',tools/'lib/apksigner.jar',android_jar,ndk/'build/cmake/android.toolchain.cmake']:
        if not p.is_file():raise ValueError('Missing build prerequisite: '+str(p))
    env=dict(os.environ,JAVA_HOME=str(args.java_home.resolve()))
    output=work/args.abi;output.mkdir(exist_ok=True)
    cmake=output/'native'
    run(['cmake','-S',source,'-B',cmake,'-G','Ninja',f'-DCMAKE_TOOLCHAIN_FILE={ndk}/build/cmake/android.toolchain.cmake',
         f'-DANDROID_ABI={args.abi}','-DANDROID_PLATFORM=android-30','-DANDROID_STL=c++_static','-DCMAKE_BUILD_TYPE=Release',
         f'-DPython3_EXECUTABLE={sys.executable}'],env)
    run(['cmake','--build',cmake,'--parallel','2'],env)
    res=output/'res';res.mkdir(exist_ok=True)
    shutil.copytree(source/'res',res,dirs_exist_ok=True)
    icon=json.loads((project/'src/fefw_icon.json').read_text(encoding='utf-8'))
    ico=base64.b64decode(icon['ico_base64'])
    assert hashlib.sha256(ico).hexdigest()==icon['ico_sha256']
    png=None
    for i in range(struct.unpack_from('<H',ico,4)[0]):
        width,height,_,_,_,_,size,offset=struct.unpack_from('<BBBBHHII',ico,6+16*i)
        if width==height==0:png=ico[offset:offset+size]
    assert png and png.startswith(b'\x89PNG\r\n\x1a\n')
    (res/'drawable').mkdir(exist_ok=True);(res/'drawable/app_icon.png').write_bytes(png)
    assets=output/'assets';assets.mkdir(exist_ok=True)
    shutil.copy2(project/'supported-build.json',assets/'supported-build.json')
    shutil.copy2(project/'LICENSE',assets/'LICENSE.txt')
    shutil.copy2(ndk/'NOTICE',assets/'NDK-NOTICE.txt')
    shutil.copy2(ndk/'NOTICE.toolchain',assets/'NDK-TOOLCHAIN-NOTICE.txt')
    dependency=source/'runtime/third_party/zbic'
    (assets/'ZBIC-NOTICES.txt').write_text('\n\n'.join((dependency/name).read_text(encoding='utf-8')
        for name in ['README.md','LICENSE','COPYING']),encoding='utf-8')
    run([tools/'aapt2.exe','compile','--dir',res,'-o',output/'resources.zip'],env)
    generated=output/'generated';generated.mkdir(exist_ok=True)
    unsigned=output/'unsigned.apk'
    run([tools/'aapt2.exe','link','-o',unsigned,'-I',android_jar,'--manifest',source/'AndroidManifest.xml',
         '--java',generated,'-A',assets,'--version-code','7','--version-name','0.1.6-android-threads-ipc',
         '--min-sdk-version','30','--target-sdk-version',str(args.api),output/'resources.zip'],env)
    classes=output/'classes';classes.mkdir(exist_ok=True)
    java_files=sorted((source/'java').rglob('*.java'))+sorted(generated.rglob('*.java'))
    javac_args=output/'javac.args'
    javac_args.write_text('\n'.join('"'+str(p).replace('\\','/')+'"' for p in java_files),encoding='utf-8')
    run([java/'javac.exe','-encoding','UTF-8','--release','8','-classpath',android_jar,'-d',classes,'@'+str(javac_args)],env)
    class_jar=output/'classes.jar'
    with zipfile.ZipFile(class_jar,'w') as z:
        for p in sorted(classes.rglob('*.class')):z.write(p,p.relative_to(classes).as_posix())
    dex=output/'dex';dex.mkdir(exist_ok=True)
    run([java/'java.exe','-cp',tools/'lib/d8.jar','com.android.tools.r8.D8','--release','--min-api','30','--lib',android_jar,'--output',dex,class_jar],env)
    library=output/'libfefw_android.so'
    shutil.copy2(cmake/'libfefw_android.so',library)
    run([ndk/'toolchains/llvm/prebuilt/windows-x86_64/bin/llvm-strip.exe','--strip-unneeded',library],env)
    with zipfile.ZipFile(unsigned,'a') as z:
        z.write(dex/'classes.dex','classes.dex')
        z.write(library,f'lib/{args.abi}/libfefw_android.so',compress_type=zipfile.ZIP_STORED)
    aligned=output/'aligned.apk'
    run([tools/'zipalign.exe','-f','-P','16','4',unsigned,aligned],env)
    key=work/'android-diagnostics.p12'
    if not key.exists():
        run([java/'keytool.exe','-genkeypair','-noprompt','-keystore',key,'-storetype','PKCS12','-storepass','android',
             '-alias','androiddebugkey','-keyalg','RSA','-keysize','2048','-validity','3650','-dname','CN=FEFW Android Diagnostics,O=Local Development,C=ES'],env)
    apk=output/f'FEFW-Android-Diagnostics-{args.abi}-0.1.6.apk'
    run([java/'java.exe','-jar',tools/'lib/apksigner.jar','sign','--ks',key,'--ks-key-alias','androiddebugkey',
         '--ks-pass','pass:android','--v1-signing-enabled','false','--v2-signing-enabled','true',
         '--v3-signing-enabled','true','--out',apk,aligned],env)
    run([java/'java.exe','-jar',tools/'lib/apksigner.jar','verify','--verbose',apk],env)
    run([tools/'zipalign.exe','-c','-P','16','4',apk],env)
    with zipfile.ZipFile(apk) as z:
        assert set(z.namelist())=={'AndroidManifest.xml','classes.dex','resources.arsc','res/drawable/app_icon.png',
            'assets/supported-build.json','assets/LICENSE.txt','assets/NDK-NOTICE.txt','assets/NDK-TOOLCHAIN-NOTICE.txt',
            'assets/ZBIC-NOTICES.txt',f'lib/{args.abi}/libfefw_android.so'}
    digest=hashlib.sha256(apk.read_bytes()).hexdigest()
    (apk.with_suffix('.apk.sha256')).write_text(digest+'  '+apk.name+'\n',encoding='utf-8')
    report={'kind':'android-platform-diagnostics-only','apk':str(apk),'sha256':digest,'abi':args.abi,
        'target_fps':30,'pacing_scope':'diagnostic-colour-test-only',
        'memory_backend_available':True,'module_loader_available':True,'bootstrap_kernel_available':True,
        'guest_worker_threads_available':True,'ipc_transport_available':True,'log_manager_available':True,
        'runtime_port_stage':'guest-threads-synchronization-ipc-sm-lm',
        'game_runtime_available':False,'game_files_included':False,'signed':True,'zip_alignment_16kb':True}
    (output/'build-report.json').write_text(json.dumps(report,indent=2)+'\n',encoding='utf-8')
    print(json.dumps(report))


if __name__=='__main__':main()
