#!/usr/bin/env python3
"""Replace graphics binaries in an isolated, verified 20H392 /dev/vdb copy."""
from pathlib import Path
import argparse,hashlib,json,os,plistlib,shutil,subprocess

p=argparse.ArgumentParser(description=__doc__)
p.add_argument('--payload',type=Path,required=True)
p.add_argument('--previous-manifest',type=Path,required=True)
p.add_argument('--report',type=Path,required=True)
p.add_argument('--userland-run',required=True)
p.add_argument('--kernel-run',required=True)
a=p.parse_args()
root=Path('/mnt/in2020-root-supplement')
assert os.geteuid()==0
assert subprocess.check_output(['findmnt','-n','-o','SOURCE,FSTYPE','--target',str(root)],text=True).split()==['/dev/vdb','hfsplus']
assert plistlib.loads((root/'System/Library/CoreServices/SystemVersion.plist').read_bytes())['ProductBuildVersion']=='20H392'
source=a.payload.resolve();assert source in (Path('/home/builder/in2020/gpu-v2-files'), Path('/home/builder/in2020/gpu-v3-files'))
old=json.loads(a.previous_manifest.read_text());expected={}
for line in (source/'SHA256SUMS').read_text().splitlines():
 digest,name=line.split(maxsplit=1);src=(source/name).resolve();assert source in src.parents
 assert hashlib.sha256(src.read_bytes()).hexdigest()==digest
 expected[src.relative_to(source).as_posix()]=digest
names=['lib/libOSMesa.8.dylib','lib/libsweet-gl.dylib','lib/libsweet-ca.dylib','lib/libsweet-compat.dylib','sweet-gpu-test','sweet-gl-test']
previous={f['path']:f['sha256'] for f in old['installed']};installed=[]
# Verify ALL old destination hashes before the first write.
for name in names:
 dest='/usr/local/'+name if name.startswith('lib/') else '/usr/local/bin/'+name
 assert name in expected and dest in previous
 assert hashlib.sha256((root/dest.lstrip('/')).read_bytes()).hexdigest()==previous[dest],dest
jobs=plistlib.loads((root/'System/Library/xpc/launchd.plist').read_bytes())['LaunchDaemons']
for label in old['one_shot_jobs']:
 key='/System/Library/LaunchDaemons/'+label+'.plist'
 assert jobs[key]==plistlib.loads((root/key.lstrip('/')).read_bytes())
 assert jobs[key]['StandardOutPath']==jobs[key]['StandardErrorPath']=='/dev/null'
 assert jobs[key]['RunAtLoad'] and not jobs[key]['KeepAlive']
for name in names:
 dest='/usr/local/'+name if name.startswith('lib/') else '/usr/local/bin/'+name
 f=root/dest.lstrip('/');shutil.copyfile(source/name,f);os.chmod(f,0o755);os.chown(f,0,0)
 assert hashlib.sha256(f.read_bytes()).hexdigest()==expected[name]
 installed.append(dict(path=dest,sha256=expected[name],previous_sha256=previous[dest],size=f.stat().st_size))
report=dict(status='UPDATED_PENDING_FSCK_AND_DEVICE_TEST',userland_run=a.userland_run,kernel_run=a.kernel_run,installed=installed,one_shot_jobs=old['one_shot_jobs'],phone_tested=False,actual_blit_improvement_verified=False,poster_cpu_preview_verified=False,wallpaper_recovery_verified=False,apps_black_screen_fixed=False,preserves_current_shader_fixes=True,assetsd_cause_fixed=False)
a.report.write_text(json.dumps(report,indent=2));print(json.dumps(report,indent=2),flush=True)
