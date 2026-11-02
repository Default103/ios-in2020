"""Verify cumulative ADSP/RPMh kernel overlays against the reviewed sources."""
from pathlib import Path
import re
root = Path(__file__).resolve().parents[1]
names = ('in2020_adsp.c', 'in2020_audio_glue.c', 'hakios.audio.h',
         'hakios.input.h', 'in2020_mdt.h', 'in2020_rpmh.c', 'in2020_arc.h', 'in2020_tfa.c',
         'in2020_afe.c', 'in2020_afe_packet.h')
state = {}
for patch in sorted((root / 'xnu/patches').glob('*.patch')):
    if int(patch.name[:4]) < 123:
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
    assert '\n'.join(state[name]) + '\n' == (root / 'in2020/audio' / name).read_text(encoding='utf-8'), name
print('ADSP/RPMh cumulative kernel patch/source consistency PASS')
