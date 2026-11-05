"""Check native front-camera overlay/source consistency."""
from pathlib import Path
import re
r=Path(__file__).resolve().parents[1]
p=(r/'xnu/patches/0131-in2020-camera-front-preflight.patch').read_text()
for name in ('in2020_camera.c','in2020_camera_board.h'):
    section=next(s for s in re.split(r'(?=^--- )',p,flags=re.M) if '\n+++ b/bsd/dev/'+name+'\n' in s)
    assert section.startswith('--- /dev/null\n')
    contents='\n'.join(s[1:] for s in section.splitlines()[3:] if s.startswith('+'))+'\n'
    assert contents==(r/'in2020/camera'/name).read_text(),name
print('Native camera preflight overlay/source PASS')
