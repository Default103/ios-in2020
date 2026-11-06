"""Package stage3 UIKit front/tele snapshots; module tables and UI stay local."""
from pathlib import Path
import argparse,hashlib,json,subprocess,sys,struct,runpy
p=argparse.ArgumentParser(description=__doc__)
for name in ('workspace','kernel','ui','out'):p.add_argument('--'+name,type=Path,required=True)
for name in ('kernel-sha256','kernel-run','kernel-commit','ui-sha256'):p.add_argument('--'+name,required=True)
a=p.parse_args();r=a.workspace.resolve();digest=lambda b:hashlib.sha256(b).hexdigest()
assert digest(a.ui.read_bytes())==a.ui_sha256 and 0<a.ui.stat().st_size<1024*1024-32
assert digest(a.kernel.read_bytes())==a.kernel_sha256
a.out.mkdir(parents=True,exist_ok=False)
sys.path.insert(0,str(Path(__file__).resolve().parent));import adt,mkpayload
roots=[]
for sensor in ('front','tele'):
 folder=a.out/(sensor+'-profile')
 subprocess.run([sys.executable,str(Path(__file__).with_name('build-in2020-camera-raw.py')),
  '--workspace',str(r),'--kernel',str(a.kernel),'--kernel-sha256',a.kernel_sha256,
  '--kernel-run',a.kernel_run,'--kernel-commit',a.kernel_commit,'--sensor',sensor,'--out',str(folder)],check=True)
 raw=(folder/'in2020-camera.adt').read_bytes();root,end=adt.parse(raw);assert end==len(raw)
 roots.append(root)
root=roots[0];front=next(c for c in root.children if c.name=='in2020-camera');tele=next(c for c in roots[1].children if c.name=='in2020-camera')
for old,new in [('front-plan','tele-plan'),('phy-ops','tele-phy-ops')]:
 prop=next(q for q in tele.props if q[0]==old);front.props.append((new,prop[1],prop[2]))
front.props.append(('ui-dylib',0,bytes.fromhex(a.ui_sha256)+a.ui.read_bytes()))
encoded=adt.serialize(root);assert len(encoded)<32*1024**2
meta=json.loads((a.out/'front-profile/verification.json').read_text());command=meta['cmdline'].replace('in2020cam=2','in2020cam=3')
assert 'in2020cam=3' in command and 'SpringBoard' in command and len(command.encode())<608
assert 'in2020adsp' not in command
path=a.out/'in2020-camera-system.adt';path.write_bytes(encoded)
blob,info=mkpayload.build(str(a.kernel),str(path),command);assert info['cputype']=='0x100000c' and info['cpusub']==0
vbase,entry,off,size,end,phys,mem=struct.unpack_from('<7Q',blob,16)
assert blob[off:off+size]==encoded and 0<((end+size+0x3fff)&~0x3fff)+0x4000-vbase<mem
payload=a.out/'payload.bin';payload.write_bytes(blob+bytes((-len(blob))%8))
base=r/'builds/baselines/in2020-cpus8-fb60-working-20261101'
assert digest((base/'in2020-ios16-cpus8-fb60-boot.img').read_bytes())=='bc7664b7d7d015c7a1e74790c95db3e05d6c62b516a32d77530b2e3c587afdd1'
ks=struct.unpack_from('<I',(base/'in2020-ios16-cpus8-fb60-boot.img').read_bytes(),8)[0]
assert (base/'in2020-ios16-cpus8-fb60-boot.img').read_bytes()[4096:4096+ks]==(base/'baseline-loader.Image').read_bytes()
boot=a.out/'in2020-ios16-camera-system-boot.img'
runpy.run_path(str(r/'scripts/boot-image-tools.py'))['pack_boot'](r,base/'baseline-loader.Image',payload,boot)
record=dict(status='BUILT_NOT_PHONE_TESTED',kernel_run=a.kernel_run,kernel_commit=a.kernel_commit,kernel_sha256=a.kernel_sha256,
 ui_sha256=a.ui_sha256,boot_sha256=digest(boot.read_bytes()),boot_size=boot.stat().st_size,cmdline=command,
 camera_stage=3,sensors=['front IMX471','tele S5K3M5'],manual_snapshots=True,continuous_preview=False,
 ios_ui_verified=False,temporary_boot_only=True,adsp_firmware_present=False)
(a.out/'verification.json').write_text(json.dumps(record,indent=2))
print(json.dumps(record,indent=2))
