"""Build a temporary ADSP milestone boot; no speaker output or camera capture."""
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
p.add_argument('--adsp-step', type=int, choices=(2, 7, 11), default=2)
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
firmware = r / 'firmware/in2020-audio-stock-20261102/adsp'
manifest = (firmware / 'manifest.json').read_bytes()
assert digest(manifest) == '593eeba4ac18cac95181cbc6a75d6e7d40775bff31d0b1671540222b5ee9e503'
node = adt.Node()
node.props = [('name', 0, b'in2020-adsp\0')]
node.children = []
files = []
for item in json.loads(manifest):
    name = item['file']
    assert name == 'adsp.mdt' or (name.startswith('adsp.b') and len(name) == 8 and name[6:].isdigit())
    data = (firmware / name).read_bytes()
    assert len(data) == item['bytes'] and digest(data) == item['sha256']
    node.props.append((name, 0, data))
    files.append(item)
assert len(files) == 20
root.children.append(node)
encoded = adt.serialize(root)
assert len(encoded) < 32 * 1024**2
command = meta['cmdline'] + ' in2020adsp=' + str(a.adsp_step)
assert 'sweetkeys=1' in command and len(command.encode()) < 608
a.out.mkdir(parents=True, exist_ok=False)
dt = a.out / 'in2020-adsp.adt'
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
boot = a.out / ('in2020-ios16-adsp-readonly-boot.img' if a.adsp_step == 2 else
                'in2020-ios16-adsp-step' + str(a.adsp_step) + '-boot.img')
helpers['pack_boot'](r, baseline / 'baseline-loader.Image', payload, boot)
record = dict(status='BUILT_NOT_PHONE_TESTED', kernel_run=a.kernel_run,
              kernel_commit=a.kernel_commit, kernel_sha256=a.kernel_sha256,
              boot_sha256=digest(boot.read_bytes()), boot_size=boot.stat().st_size,
              adt_size=len(encoded), cmdline=command, cmdline_bytes=len(command.encode()),
              firmware=files, adsp_steps=a.adsp_step, dsp_start_enabled=a.adsp_step >= 7,
              amplifier_identity_probe_enabled=a.adsp_step >= 7,
              speaker_output_implemented=False, camera_capture_implemented=False,
              library_updater_present=False, temporary_boot_only=True)
(a.out / 'verification.json').write_text(json.dumps(record, indent=2), encoding='utf-8')
for path in (dt, payload, boot):
    path.chmod(0o444)
print('ADSP step', a.adsp_step, record['boot_sha256'], record['cmdline_bytes'], 'command bytes')
