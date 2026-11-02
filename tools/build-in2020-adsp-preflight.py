"""Build a temporary staged audio boot, including the gated audible test at 14."""
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
p.add_argument('--adsp-step', type=int, choices=(2, 7, 11, 12, 13, 14, 15), default=2)
p.add_argument("--audio-hal", type=Path)
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
speaker_profile = None
if a.adsp_step >= 14:
    speaker_profile = runpy.run_path(str(Path(__file__).with_name('build-in2020-speaker-profile.py')))['build'](
        (r / 'firmware/in2020-audio-stock-20261102/tfa98xx.cnt').read_bytes())
    node.props.append(('tfa-speaker-ops', 0, speaker_profile))
if a.adsp_step == 15:
    assert a.audio_hal is not None
    import plistlib
    hal = a.audio_hal.resolve()
    checks = {}
    for line in (hal / 'SHA256SUMS').read_text().splitlines():
        sha, name = line.split(maxsplit=1)
        name = name.removeprefix('./')
        assert '..' not in Path(name).parts
        checks[name] = sha
    for i, name in enumerate(('HakiosAudio.driver/HakiosAudio', 'HakiosAudio.driver/Info.plist')):
        data = (hal / name).read_bytes()
        assert digest(data) == checks[name] and 0 < len(data) <= 1024**2
        if i == 0:
            assert struct.unpack_from('<II', data) == (0xfeedfacf, 0x100000c)
            assert struct.unpack_from('<I', data, 8)[0] == 0
        else:
            info = plistlib.loads(data)
            assert info['CFBundleExecutable'] == 'HakiosAudio'
            assert info['CFPlugInFactories']['7C3E5B0A-52D4-4F6B-9E0D-6B1A2D7F0C51'] == 'HakiosAudio_Create'
        node.props.append(('hal-file' + str(i), 0, bytes.fromhex(checks[name]) + data))
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
              firmware=files, adsp_steps=min(a.adsp_step, 11), adsp_milestone=a.adsp_step,
              afe_port_test_enabled=a.adsp_step >= 12, pcm_transport_test_enabled=a.adsp_step == 13,
              audible_tone_test_enabled=a.adsp_step == 14,
              speaker_profile_sha256=digest(speaker_profile) if speaker_profile else None,
              dsp_start_enabled=a.adsp_step >= 7,
              amplifier_identity_probe_enabled=a.adsp_step >= 7,
              speaker_output_implemented=a.adsp_step == 15, camera_capture_implemented=False,
              audio_hal_installer_present=a.adsp_step == 15, library_updater_present=False, temporary_boot_only=True)
(a.out / 'verification.json').write_text(json.dumps(record, indent=2), encoding='utf-8')
for path in (dt, payload, boot):
    path.chmod(0o444)
print('ADSP step', a.adsp_step, record['boot_sha256'], record['cmdline_bytes'], 'command bytes')
