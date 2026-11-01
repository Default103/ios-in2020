#!/usr/bin/env python3
"""Supplement an isolated IN2020 HFSX root copy; never runs on the phone.

Promotes stock staged bundles into /Applications with preserved files/signatures,
selects an existing simpler wallpaper collection, and emits an audit manifest.
"""
from pathlib import Path
import argparse,hashlib,json,os,plistlib,stat,struct,subprocess


def unencrypted_arm64(path):
    with path.open('rb') as f:
        head=f.read(32)
        if head[:4]!=b'\xcf\xfa\xed\xfe':
            return False,'not a thin arm64 Mach-O'
        _,cpu,subtype,kind,ncmds,cmdbytes,flags,reserved=struct.unpack('<8I',head)
        if cpu!=0x100000c or (subtype & 0xff)!=0 or kind!=2:
            return False,'wrong CPU or non-executable Mach-O'
        commands=f.read(cmdbytes)
    at=0
    for _ in range(ncmds):
        command,size=struct.unpack_from('<II',commands,at)
        if size<8 or at+size>len(commands):
            return False,'invalid Mach-O command bounds'
        if command in (0x21,0x2c) and struct.unpack_from('<I',commands,at+16)[0]:
            return False,'encrypted executable'
        at+=size
    return True,'arm64 Mach-O, cryptid absent or zero'


def check_link_copy(source,dest):
    files=0;links=0;logical_bytes=0
    for path in source.rglob('*'):
        relative=path.relative_to(source);copy=dest/relative;st=path.lstat();ct=copy.lstat()
        assert stat.S_IFMT(st.st_mode)==stat.S_IFMT(ct.st_mode),relative
        assert (st.st_uid,st.st_gid,stat.S_IMODE(st.st_mode))==(ct.st_uid,ct.st_gid,stat.S_IMODE(ct.st_mode)),relative
        if path.is_symlink():
            assert os.readlink(path)==os.readlink(copy),relative;links+=1
        elif path.is_file():
            # Same filesystem + inode proves exact bytes without rehashing gigabytes.
            assert (st.st_dev,st.st_ino,st.st_size)==(ct.st_dev,ct.st_ino,ct.st_size),relative
            files+=1;logical_bytes+=st.st_size
    return dict(files=files,symlinks=links,logical_bytes=logical_bytes)


def main():
    ap=argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--root',type=Path,required=True)
    ap.add_argument('--report',type=Path,required=True)
    args=ap.parse_args();root=args.root.resolve()
    assert os.geteuid()==0
    assert str(root)=='/mnt/in2020-root-supplement'
    mounted=subprocess.check_output(['findmnt','-n','-o','SOURCE,FSTYPE','--target',str(root)],text=True).split()
    assert mounted==['/dev/vdb','hfsplus'],mounted
    version=plistlib.loads((root/'System/Library/CoreServices/SystemVersion.plist').read_bytes())
    assert version['ProductBuildVersion']=='20H392'
    staged=root/'private/var/staged_system_apps';applications=root/'Applications'
    assert staged.is_dir() and applications.is_dir()
    report=dict(status='OFFLINE_SUPPLEMENT_IN_PROGRESS',build='20H392',promoted=[],skipped=[],phone_tested=False,registration_verified=False)
    identifiers=set()
    bundles=sorted(staged.glob('*.app'));assert len(bundles)==32
    for source in bundles:
        info=plistlib.loads((source/'Info.plist').read_bytes())
        identifier=info['CFBundleIdentifier'];executable=info['CFBundleExecutable']
        assert '/' not in executable and executable not in ('','.','..')
        assert identifier not in identifiers;identifiers.add(identifier)
        valid,reason=unencrypted_arm64(source/executable)
        if not valid:
            report['skipped'].append(dict(bundle=source.name,reason=reason));continue
        dest=applications/source.name
        assert not dest.exists(),f'Preserve existing application: {dest}'
        subprocess.run(['rsync','-aH','--numeric-ids','--link-dest='+str(source),str(source)+'/',str(dest)+'/'],check=True)
        verified=check_link_copy(source,dest)
        assert (dest/'Info.plist').read_bytes()==(source/'Info.plist').read_bytes()
        report['promoted'].append(dict(bundle=source.name,identifier=identifier,executable=executable,
            requirements=info.get('UIRequiredDeviceCapabilities',[]),tags=info.get('SBAppTags',[]),
            executable_sha256=hashlib.sha256((source/executable).read_bytes()).hexdigest(),**verified))
        print('PROMOTED',source.name,verified,flush=True)
    # Use a resource already described by this firmware's native wallpaper schema.
    wallpaper=root/'Library/Wallpaper'
    collection=plistlib.loads((wallpaper/'Collections/Classic_Stripes.wallpaperCollection/WallpaperCollection.plist').read_bytes())
    resource=plistlib.loads((wallpaper/'Stills/Any/365.Heritage_Stripe_Green.wallpaper/Wallpaper.plist').read_bytes())
    assert 365 in collection['order'] and resource['identifier']==365
    assert resource['assets']['lockAndHome']['default']['type']=='HeritageStripe'
    path=wallpaper/'DefaultWallpapers~iphone.plist';before=path.read_bytes();data=plistlib.loads(before);mode=path.stat()
    selection=dict(wallpaperIdentifier=365,collectionIdentifier=collection['identifier'])
    report['wallpaper']=dict(previous_default=data.get('default'),selection=selection,
        resource='Library/Wallpaper/Stills/Any/365.Heritage_Stripe_Green.wallpaper',
        resource_type='HeritageStripe',runtime_rendering_still_required=True,
        cpbitmap_or_poster_database_fabricated=False)
    data['default']=selection
    data['iPhone10,3']={'default':selection}
    encoded=plistlib.dumps(data,fmt=plistlib.FMT_BINARY,sort_keys=False)
    path.write_bytes(encoded);os.chown(path,mode.st_uid,mode.st_gid);os.chmod(path,stat.S_IMODE(mode.st_mode))
    assert plistlib.loads(path.read_bytes())['iPhone10,3']['default']==selection
    report['status']='OFFLINE_FILES_SUPPLEMENTED_PENDING_FSCK_AND_PHONE_TEST'
    report['limitations']=[
        'App bundles are promoted to the same system location as existing visible apps; runtime LaunchServices registration is not proven.',
        'Original staged sources remain; signatures/Info.plist/requirements are unchanged; required hardware/network may remain unavailable.',
        'Wallpaper default selection points to an existing simpler native resource; rendering and first-boot selection still need phone validation.',
        'No generated legacy cpbitmap or guessed iOS16 PosterBoard database is installed.'
    ]
    args.report.write_text(json.dumps(report,indent=2),encoding='utf-8')
    print(json.dumps(dict(promoted=len(report['promoted']),skipped=report['skipped'],wallpaper=selection)),flush=True)


if __name__=='__main__':
    main()
