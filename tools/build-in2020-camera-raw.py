"""Package the finite native XNU IMX471 RAW10 capture test."""
from pathlib import Path
import argparse
import hashlib
import json
import runpy
import struct
import sys

p = argparse.ArgumentParser(description=__doc__)
for name in ('workspace', 'kernel', 'out'):
    p.add_argument('--' + name, type=Path, required=True)
for name in ('kernel-sha256', 'kernel-run', 'kernel-commit'):
    p.add_argument('--' + name, required=True)
p.add_argument('--module',type=Path)
p.add_argument('--phy-registers',type=Path)
a = p.parse_args()
r = a.workspace.resolve()
digest = lambda data: hashlib.sha256(data).hexdigest()
assert digest(a.kernel.read_bytes()) == a.kernel_sha256
base = r / 'builds/xnu-41443746692/in2020-keys-tests/keys-on'
meta = json.loads((base / 'verification.json').read_text(encoding='utf-8'))
assert digest((base / 'in2020-ios16-keys-1-boot.img').read_bytes()) == \
    '70f0d1ae3a4b6c53508cf414887b361c0102024cbaa1606a21fdcdff05833c33'
sys.path.insert(0, str(Path(__file__).resolve().parent))
import adt
import mkpayload
raw = (base / 'in2020.adt').read_bytes()
assert digest(raw) == '81345c45909d3176fc3339d913f4993e16fe3f5b4b2a5e6129d1fd7fd0a13b6b'
root, end = adt.parse(raw)
assert end == len(raw) and not any(c.name in ('in2020-update', 'in2020-adsp') for c in root.children)
import zlib
import re
module = a.module or (r / 'firmware/in2020-camera-stock-20261102/com.qti.sensormodule.truly_imx471.bin')
plan = runpy.run_path(str(Path(__file__).with_name('build-in2020-front-mode.py')))['build'](module.read_bytes())
rows = plan['initialization'] + plan['mode_settings']
body = b''.join(struct.pack('<HBBI',q['address'],q['value'],0,q['delay_us']) for q in rows)
header = b'I20RAW01' + struct.pack('<14I',1,155,57,212,0x471,0x10,4,0x3210,14,2328,1748,0,0,zlib.crc32(body))
assert len(header) == 64 and len(body) == 1696
phy_source = a.phy_registers or (r / 'projects/ios-in2020/camera-reference/kona/cam_csiphy_1_2_1_hwreg.h')
source = phy_source.read_bytes()
assert digest(source) == 'f972041defde9b6ac22c07eb2fb0ec10f2cdec1e4975ea0aa469c57679afbd80'
text = source.decode()
def array(name):
    start = text.index('=',text.index(name)); start = text.index('{',start)
    level = 0
    for end in range(start,len(text)):
        level += (text[end] == '{') - (text[end] == '}')
        if not level: break
    assert not level
    return re.findall(r'\{\s*(0x[0-9a-fA-F]+),\s*(0x[0-9a-fA-F]+),\s*(0x[0-9a-fA-F]+),\s*(CSIPHY_\w+)\s*\}',text[start:end+1])
ops=[]
for name in ('csiphy_common_reg_1_2_1','csiphy_2ph_v1_2_1_reg'):
    records=array(name)
    assert len(records) == (6 if name.startswith('csiphy_common') else 100)
    for offset,value,delay,kind in records:
        if kind in ('CSIPHY_3PH_REGS','CSIPHY_DNP_PARAMS'): continue
        assert int(delay,0)==0 and kind in ('CSIPHY_DEFAULT_PARAMS','CSIPHY_2PH_REGS','CSIPHY_LANE_ENABLE','CSIPHY_SETTLE_CNT_LOWER_BYTE')
        ops.append((int(offset,0),14 if kind=='CSIPHY_SETTLE_CNT_LOWER_BYTE' else int(value,0)))
phy_body=b''.join(struct.pack('<II',*q) for q in ops)
assert 0<len(ops)<=128
node=adt.Node();node.props=[('name',0,b'in2020-camera\0'),('front-plan',0,header+body),('phy-ops',0,struct.pack('<II',len(ops),zlib.crc32(phy_body))+phy_body)];node.children=[]
root.children.append(node)
encoded = adt.serialize(root)
assert len(encoded) < 32 * 1024**2
command = meta['cmdline'] + ' in2020cam=2'
assert 'sweetkeys=1' in command and len(command.encode()) < 608
a.out.mkdir(parents=True, exist_ok=False)
dt = a.out / 'in2020-camera.adt'
dt.write_bytes(encoded)
blob, info = mkpayload.build(str(a.kernel), str(dt), command)
assert info['cputype'] == '0x100000c' and info['cpusub'] == 0
vbase, entry, off, size, end, phys, mem = struct.unpack_from('<7Q', blob, 16)
top = ((end + size + 0x3fff) & ~0x3fff) + 0x4000
assert blob[off:off + size] == encoded and 0 < top - vbase < mem
assert blob[0x48:0x48 + 608].split(b'\0')[0].decode() == command
payload = a.out / 'payload.bin'
payload.write_bytes(blob + bytes((-len(blob)) % 8))
baseline = r / 'builds/baselines/in2020-cpus8-fb60-working-20261101'
old_boot = (baseline / 'in2020-ios16-cpus8-fb60-boot.img').read_bytes()
assert digest(old_boot) == 'bc7664b7d7d015c7a1e74790c95db3e05d6c62b516a32d77530b2e3c587afdd1'
ks = struct.unpack_from('<I', old_boot, 8)[0]
assert old_boot[4096:4096 + ks] == (baseline / 'baseline-loader.Image').read_bytes()
helpers = runpy.run_path(str(r / 'scripts/boot-image-tools.py'))
boot = a.out / 'in2020-ios16-camera-raw-boot.img'
helpers['pack_boot'](r, baseline / 'baseline-loader.Image', payload, boot)
record = dict(status='BUILT_NOT_PHONE_TESTED', kernel_run=a.kernel_run,
              kernel_commit=a.kernel_commit, kernel_sha256=a.kernel_sha256,
              boot_sha256=digest(boot.read_bytes()), boot_size=boot.stat().st_size,
              adt_size=len(encoded), cmdline=command, cmdline_bytes=len(command.encode()),
              camera_stage=2, camera_observer_only=False,
              sensor_power_enabled=True, sensor_register_access=True,
              isp_register_access=True, frame_capture_implemented=True,
              frame_capture_verified=False, profile_sha256=digest(header+body),
              output_width=2328, output_height=1748, output_stride=2912,
              adsp_firmware_present=False, library_updater_present=False,
              temporary_boot_only=True)
(a.out / 'verification.json').write_text(json.dumps(record, indent=2), encoding='utf-8')
for path in (dt, payload, boot):
    path.chmod(0o444)
print('Native camera RAW', record['boot_sha256'], record['cmdline_bytes'], 'command bytes')
