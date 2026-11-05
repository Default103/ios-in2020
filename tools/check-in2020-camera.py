"""Verify cumulative ADSP/RPMh kernel overlays against the reviewed sources."""
from pathlib import Path
import re
root = Path(__file__).resolve().parents[1]
names = ('in2020_camera.c', 'in2020_camera_board.h', 'in2020_camera_capture.c', 'in2020_camera_clocks.c', 'in2020_camera_dma.c', 'in2020_camera_plan.h', 'in2020_camera_raw.h', 'in2020_camera_sensor.c')
state = {}
for patch in sorted((root / 'xnu/patches').glob('*.patch')):
    if int(patch.name[:4]) < 131:
        continue
    text = patch.read_text(encoding='utf-8')
    for section in re.split(r'(?=^--- (?:a/|/dev/null))', text, flags=re.M):
        lines = section.splitlines()
        if len(lines) < 2 or not lines[1].startswith('+++ b/bsd/dev/'):
            continue
        name = lines[1].removeprefix('+++ b/bsd/dev/')
        if name not in names:
            continue
        if lines[0] == '--- /dev/null':
            assert name not in state, name
            state[name] = []
        result = state[name]
        delta = 0
        for hunk in re.split(r'(?=^@@ )', '\n'.join(lines[2:]), flags=re.M):
            if not hunk.startswith('@@ '):
                continue
            header, *body = hunk.splitlines()
            match = re.match(r'@@ -(\d+)(?:,(\d+))? \+(\d+)(?:,(\d+))? @@', header)
            assert match, header
            old = [s[1:] for s in body if s.startswith((' ', '-'))]
            new = [s[1:] for s in body if s.startswith((' ', '+'))]
            assert len(old) == int(match[2] or 1) and len(new) == int(match[4] or 1), header
            at = max(0, int(match[1]) - 1) + delta
            assert result[at:at + len(old)] == old, (name, header)
            result[at:at + len(old)] = new
            delta += len(new) - len(old)
for name in names:
    assert '\n'.join(state[name]) + '\n' == (root / 'in2020/camera' / name).read_text(encoding='utf-8'), name
print('Camera cumulative kernel patch/source consistency PASS')
