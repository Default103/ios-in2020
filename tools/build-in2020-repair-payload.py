#!/usr/bin/env python3
"""Build hash-gated in-place repair/restore temporary boot images; no flashing."""
from pathlib import Path
import argparse,hashlib,json,runpy,struct,sys
ap=argparse.ArgumentParser(description=__doc__)
for arg in ['workspace','kernel','old-userland','new-userland','out']:ap.add_argument('--'+arg,type=Path,required=True)
for arg in ['kernel-run','userland-run','kernel-commit','userland-commit']:ap.add_argument('--'+arg,required=True)
a=ap.parse_args();r=a.workspace.resolve();base=r/'builds/baselines/in2020-cpus8-fb60-working-20261101'
sys.path.insert(0,str(Path(__file__).resolve().parent));import adt,mkpayload
frozen=(base/'in2020-ios16-cpus8-fb60-boot.img').read_bytes()
assert hashlib.sha256(frozen).hexdigest()=='bc7664b7d7d015c7a1e74790c95db3e05d6c62b516a32d77530b2e3c587afdd1'
ks=struct.unpack_from('<I',frozen,8)[0]
assert frozen[4096:4096+ks]==(base/'baseline-loader.Image').read_bytes()
assert a.old_userland.resolve()==r/'builds/userland-41338508982'
old_manifest=json.loads((r/'builds/ios16-root-20H392-in2020-gpu-blit-preview-20261101/graphics-v2-manifest.json').read_text(encoding='utf-8'))
old_expected={x['path']:x['sha256'] for x in old_manifest['installed']}
items=['lib/libOSMesa.8.dylib','lib/libsweet-gl.dylib','lib/libsweet-ca.dylib','lib/libsweet-compat.dylib','sweet-gpu-test','sweet-gl-test']
records=[];data=[]
for name in items:
 old=(a.old_userland/name).read_bytes();new=(a.new_userland/name).read_bytes();path='/usr/local/'+name if name.startswith('lib/') else '/usr/local/bin/'+name
 oh=hashlib.sha256(old).digest();nh=hashlib.sha256(new).digest();assert oh.hex()==old_expected[path]
 assert 0<len(new)<=32*1024**2 and new[:4]==bytes.fromhex('cffaedfe')
 records.append(dict(path=path,old_sha256=oh.hex(),new_sha256=nh.hex(),old_size=len(old),new_size=len(new)))
 data.append((oh,nh,old,new))
# Verify the complete downloaded checksum manifest, not just the carried files.
for folder in (a.old_userland,a.new_userland):
 for line in (folder/'SHA256SUMS').read_text(encoding='utf-8').splitlines():
  digest,name=line.split(maxsplit=1);path=(folder/name).resolve();assert folder.resolve() in path.parents
  assert hashlib.sha256(path.read_bytes()).hexdigest()==digest
out=a.out.resolve();out.mkdir(exist_ok=False);helpers=runpy.run_path(str(r/'scripts/boot-image-tools.py'))
original=(base/'in2020-cpus8.adt').read_bytes();cmd=json.loads((base/'verification.json').read_text(encoding='utf-8'))['cmdline'].replace('sweetlog=0','sweetlog=1')+' sweetperf=5'
compat='/usr/local/lib/libsweet-compat.dylib:PosterBoard,assetsd,SpringBoard,Maps,FaceTime'
old_compat='/usr/local/lib/libsweet-compat.dylib:PosterBoard,assetsd'
for name,mode,inject,wall in [('v3-repair',0,'/usr/local/lib/libsweet-ca.dylib:backboardd;'+compat,True),('v3-check',0,compat,False),('v3-restore-libs',1,'/usr/local/lib/libsweet-ca.dylib:backboardd;'+old_compat,False)]:
 folder=out/name;folder.mkdir();tree,end=adt.parse(original);assert end==len(original)
 node=adt.Node();node.props=[('name',0,b'in2020-update\0'),('mode',0,struct.pack('<I',mode))]
 for i,(oh,nh,old,new) in enumerate(data):node.props.append(('file'+str(i),0,oh+nh+(old if mode else new)))
 assert not any(c.name=='in2020-update' for c in tree.children);tree.children.append(node)
 encoded=adt.serialize(tree);tree.children.pop();assert adt.serialize(tree)==original
 adt_path=folder/'in2020-repair.adt';adt_path.write_bytes(encoded)
 args=cmd+' sweetinject='+inject+' sweetnull=backboardd,PosterBoard,assetsd,sweet-gpu-test,sweet-gl-test,Maps,FaceTime,SpringBoard'+(' sweetwall=1' if wall else '')
 assert len(args.encode())<608,len(args.encode())
 payload,info=mkpayload.build(str(a.kernel),str(adt_path),args);payload+=bytes((-len(payload))%8)
 assert info['cputype']=='0x100000c' and info['cpusub']==0
 fields=struct.unpack_from('<7Q',payload,16);vbase,entry,off,size,kend,phys,mem=fields
 assert payload[off:off+size]==encoded and size<32*1024**2
 top=((kend+size+0x3fff)&~0x3fff)+0x4000;assert 0<top-vbase<mem
 payload_path=folder/'payload-phone.bin';payload_path.write_bytes(payload)
 boot=folder/('in2020-ios16-'+name+'-boot.img');helpers['pack_boot'](r,base/'baseline-loader.Image',payload_path,boot)
 record=dict(status='READY_NOT_PHONE_TESTED',mode=name,update_action=mode,kernel_run=a.kernel_run,userland_run=a.userland_run,kernel_commit=a.kernel_commit,userland_commit=a.userland_commit,kernel_sha256=hashlib.sha256(a.kernel.read_bytes()).hexdigest(),boot_sha256=hashlib.sha256(boot.read_bytes()).hexdigest(),boot_size=boot.stat().st_size,adt_size=size,cmdline=args,cmdline_bytes=len(args.encode()),kernel_top_offset=top-vbase,kernel_memory_size=mem,files=records,userdata_partition_flash=False,in_place_library_update=True,backups_retained=True)
 (folder/'verification.json').write_text(json.dumps(record,indent=2),encoding='utf-8');print(name,record['boot_sha256'],'cmd',len(args.encode()),'bootbytes',record['boot_size'])
 for path in (boot,payload_path,adt_path):path.chmod(0o444)
(out/'update-manifest.json').write_text(json.dumps(dict(kernel_run=a.kernel_run,userland_run=a.userland_run,files=records),indent=2),encoding='utf-8')
