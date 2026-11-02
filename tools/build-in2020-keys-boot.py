"""Package a verified kernel as temporary keys-on/off boot images; no phone IO."""
from pathlib import Path
import argparse
import hashlib
import json
import runpy
import struct
import sys

parser = argparse.ArgumentParser(description=__doc__)
for arg in ('workspace', 'kernel', 'out'):
    parser.add_argument('--' + arg, type=Path, required=True)
for arg in ('kernel-sha256', 'kernel-run', 'kernel-commit'):
    parser.add_argument('--' + arg, required=True)
args = parser.parse_args()
workspace = args.workspace.resolve()
base = workspace / 'builds/baselines/in2020-cpus8-fb60-working-20261101'
v4 = workspace / 'builds/xnu-41360566477/in2020-wallpaper-120hz-tests/v4-repair'
digest = lambda b: hashlib.sha256(b).hexdigest()
kernel = args.kernel.read_bytes()
assert digest(kernel) == args.kernel_sha256
original = (base / 'in2020-ios16-cpus8-fb60-boot.img').read_bytes()
assert digest(original) == 'bc7664b7d7d015c7a1e74790c95db3e05d6c62b516a32d77530b2e3c587afdd1'
loader = base / 'baseline-loader.Image'
ks = struct.unpack_from('<I', original, 8)[0]
assert original[4096:4096 + ks] == loader.read_bytes()
old = json.loads((v4 / 'verification.json').read_text(encoding='utf-8'))
assert digest((v4 / 'in2020-ios16-v4-repair-boot.img').read_bytes()) == old['boot_sha256']
cmd = old['cmdline'].replace(' sweetwall=1', '')
assert cmd.count('sweetkeys=0') == 1
sys.path.insert(0, str(Path(__file__).resolve().parent))
import adt
import mkpayload
tree = (base / 'in2020-cpus8.adt').read_bytes()
root, consumed = adt.parse(tree)
assert consumed == len(tree) and not any(c.name == 'in2020-update' for c in root.children)
assert len(next(c for c in root.children if c.name == 'cpus').children) == 8
assert any(c.name == 'sweet-gpu' for c in root.children)
helpers = runpy.run_path(str(workspace / 'scripts/boot-image-tools.py'))
args.out.mkdir(parents=True, exist_ok=False)
for enabled in (1, 0):
    out = args.out / ('keys-on' if enabled else 'keys-off')
    out.mkdir()
    adt_path = out / 'in2020.adt'
    adt_path.write_bytes(tree)
    command = cmd.replace('sweetkeys=0', 'sweetkeys=' + str(enabled))
    assert len(command.encode()) < 608
    blob, info = mkpayload.build(str(args.kernel), str(adt_path), command)
    assert info['cputype'] == '0x100000c' and info['cpusub'] == 0
    vbase, entry, offset, size, end, phys, memory = struct.unpack_from('<7Q', blob, 16)
    top = ((end + size + 0x3fff) & ~0x3fff) + 0x4000
    assert blob[offset:offset + size] == tree and 0 < top - vbase < memory
    assert blob[0x48:0x48 + 608].split(b'\0')[0].decode() == command
    payload = out / 'payload.bin'
    payload.write_bytes(blob + bytes((-len(blob)) % 8))
    boot = out / ('in2020-ios16-keys-' + str(enabled) + '-boot.img')
    helpers['pack_boot'](workspace, loader, payload, boot)
    record = dict(status='BUILT_NOT_PHONE_TESTED', keys_enabled=enabled,
                  kernel_run=args.kernel_run, kernel_commit=args.kernel_commit,
                  kernel_sha256=args.kernel_sha256, boot_sha256=digest(boot.read_bytes()),
                  boot_size=boot.stat().st_size, cmdline=command,
                  cmdline_bytes=len(command.encode()), adt_sha256=digest(tree),
                  library_updater_present=False, software_vsync_hz=120,
                  physical_panel_mode_changed=False, speaker_output_implemented=False,
                  temporary_boot_only=True)
    (out / 'verification.json').write_text(json.dumps(record, indent=2), encoding='utf-8')
    for path in (adt_path, payload, boot):
        path.chmod(0o444)
    print(out.name, record['boot_sha256'], record['cmdline_bytes'])
