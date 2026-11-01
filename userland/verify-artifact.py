#!/usr/bin/env python3
"""Fail packaging when a produced iOS binary needs an omitted non-system library."""
from pathlib import Path
import subprocess,sys
root=Path(sys.argv[1]);missing=[]
for path in sorted(root.rglob('*')):
 if not path.is_file():continue
 if path.read_bytes()[:4]!=b'\xcf\xfa\xed\xfe':continue
 p=subprocess.run(['otool','-L',str(path)],capture_output=True,text=True,check=True)
 for line in p.stdout.splitlines()[1:]:
  dep=line.strip().split(' (',1)[0]
  if dep.startswith(('/System/Library/','/usr/lib/')):continue
  name=Path(dep).name
  if dep.startswith('/usr/local/lib/') and (root/'lib'/name).is_file():continue
  # The first dependency of a dylib can be its own install name.
  if path.name==name and dep=='/usr/local/lib/'+name:continue
  missing.append((str(path),dep))
if missing:raise SystemExit('Unpackaged or nonportable dependencies: '+repr(missing))
print('PASS: all non-system Mach-O dependencies are packaged')
