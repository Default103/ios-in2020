"""Verify that the cumulative kernel patch installs the reviewed IN2020 sources."""
from pathlib import Path
import re

root = Path(__file__).resolve().parents[1]
text = (root / 'xnu/patches/0122-in2020-pm8150-keys.patch').read_text()
for target, source in [('sweet_keys.c', 'pm8150_keys.c'),
                       ('in2020_keys_state.h', 'pm8150_keys_state.h')]:
    section = text.split('+++ b/bsd/dev/' + target + '\n', 1)[1]
    section = section.split('--- ', 1)[0]
    # The keys replacement is a full-file hunk followed, if needed, by a
    # tail hunk retaining the unchanged #endif. Apply to the known old file.
    if target == 'sweet_keys.c':
        old = (root / 'xnu/patches/0060-sweet-keys-touch-drivers.patch').read_text()
        old = old.split('+++ b/bsd/dev/sweet_keys.c\n', 1)[1].split('--- /dev/null', 1)[0]
        lines = [line[1:] for line in old.splitlines() if line.startswith('+')]
    else:
        lines = []
    delta = 0
    for hunk in re.split(r'(?=^@@ )', section, flags=re.M):
        if not hunk.startswith('@@ '): continue
        header, *body = hunk.splitlines()
        match = re.match(r'@@ -(\d+)(?:,\d+)? \+(\d+)(?:,\d+)? @@', header)
        before = [line[1:] for line in body if line.startswith((' ', '-'))]
        after = [line[1:] for line in body if line.startswith((' ', '+'))]
        start = max(0, int(match[1]) - 1) + delta
        assert lines[start:start + len(before)] == before, header
        lines[start:start + len(before)] = after
        delta += len(after) - len(before)
    assert '\n'.join(lines) + '\n' == (root / 'in2020' / source).read_text(), source
print('IN2020 keys kernel patch/source consistency PASS')
