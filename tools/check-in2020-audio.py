"""Check the source overlay installed by the staged ADSP kernel patch."""
from pathlib import Path
import re
root = Path(__file__).resolve().parents[1]
patch = (root / 'xnu/patches/0123-in2020-adsp-readonly-preflight.patch').read_text()
for name in ('in2020_adsp.c', 'in2020_audio_glue.c', 'hakios.audio.h',
             'hakios.input.h', 'in2020_mdt.h'):
    section = patch.split('+++ b/bsd/dev/' + name + '\n', 1)[1]
    section = re.split(r'^--- ', section, maxsplit=1, flags=re.M)[0]
    body = '\n'.join(line[1:] for line in section.splitlines() if line.startswith('+')) + '\n'
    assert body == (root / 'in2020/audio' / name).read_text(), name
print('ADSP kernel patch/source consistency PASS')
