#!/usr/bin/env python3
"""Install validated graphics artifacts into the isolated /dev/vdb root copy."""
from pathlib import Path
import argparse
import hashlib
import json
import os
import plistlib
import shutil
import stat
import subprocess


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--payload', type=Path, required=True)
    parser.add_argument('--report', type=Path, required=True)
    args = parser.parse_args()
    root = Path('/mnt/in2020-root-supplement')
    assert os.geteuid() == 0
    mount = subprocess.check_output(
        ['findmnt', '-n', '-o', 'SOURCE,FSTYPE', '--target', str(root)], text=True).split()
    assert mount == ['/dev/vdb', 'hfsplus'], mount
    assert plistlib.loads((root/'System/Library/CoreServices/SystemVersion.plist').read_bytes())['ProductBuildVersion'] == '20H392'
    source = args.payload.resolve()
    assert source == Path('/home/builder/in2020/gpu-files')
    expected = {}
    for line in (source/'SHA256SUMS').read_text().splitlines():
        digest, name = line.split(maxsplit=1)
        path = (source/name).resolve()
        assert source in path.parents
        assert hashlib.sha256(path.read_bytes()).hexdigest() == digest
        expected[path.relative_to(source).as_posix()] = digest
    names = ['lib/libOSMesa.8.dylib', 'lib/libsweet-gl.dylib',
             'lib/libsweet-ca.dylib', 'lib/libsweet-compat.dylib',
             'sweet-gpu-test', 'sweet-gl-test']
    installed = []
    for name in names:
        assert name in expected
        relative = 'usr/local/'+name if name.startswith('lib/') else 'usr/local/bin/'+name
        dest = root/relative
        assert not dest.exists(), f'Preserve existing file: {dest}'
        dest.parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(source/name, dest)
        os.chown(dest, 0, 0)
        os.chmod(dest, 0o755)
        assert hashlib.sha256(dest.read_bytes()).hexdigest() == expected[name]
        installed.append(dict(path='/'+relative, sha256=expected[name], size=dest.stat().st_size))
    path = root/'System/Library/xpc/launchd.plist'
    before = path.stat()
    launchd = plistlib.loads(path.read_bytes())
    jobs = launchd['LaunchDaemons']
    added = []
    for executable in ('sweet-gpu-test', 'sweet-gl-test'):
        label = 'com.in2020.'+executable
        key = '/System/Library/LaunchDaemons/'+label+'.plist'
        assert key not in jobs
        job = dict(Label=label, ProgramArguments=['/usr/local/bin/'+executable],
                   RunAtLoad=True, KeepAlive=False, UserName='root',
                   # Existing SWEET launchd cannot open /dev/console for jobs.
                   # sweetnull tees these processes' /dev/null writes to the log.
                   StandardOutPath='/dev/null', StandardErrorPath='/dev/null',
                   ProcessType='Background')
        jobs[key] = job
        # Keep the standalone definition too, for inspection and non-cache boots.
        standalone = root/key.lstrip('/')
        assert not standalone.exists()
        standalone.write_bytes(plistlib.dumps(job, fmt=plistlib.FMT_BINARY))
        os.chown(standalone, 0, 0)
        os.chmod(standalone, 0o644)
        added.append(label)
    path.write_bytes(plistlib.dumps(launchd, fmt=plistlib.FMT_BINARY, sort_keys=False))
    os.chown(path, before.st_uid, before.st_gid)
    os.chmod(path, stat.S_IMODE(before.st_mode))
    assert len(plistlib.loads(path.read_bytes())['LaunchDaemons']) == len(jobs)
    report = dict(status='FILES_INSTALLED_PENDING_FSCK_AND_PHONE_TEST',
                  userland_run='41329886029', userland_commit='84baaf27eafaae8ed4d463058ff525ba271e7c20',
                  kernel_run='41329071725', installed=installed, one_shot_jobs=added,
                  phone_tested=False, gpu_gui_acceleration_verified=False,
                  assetsd_cause_fixed=False,
                  assetsd_evidence='Captures original ObjC exception description and throw stack when thrown; not swallowed; no phone capture yet',
                  poster_repair='Guard duplicate first-time setup only when existing configuration is present',
                  existing_apps_and_wallpaper_preserved=True)
    args.report.write_text(json.dumps(report, indent=2), encoding='utf-8')
    print(json.dumps(report, indent=2), flush=True)


if __name__ == '__main__':
    main()
