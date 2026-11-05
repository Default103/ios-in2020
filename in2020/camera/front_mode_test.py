"""Test independent sensor metadata/register checks; no vendor data or device."""
from pathlib import Path
import copy
import runpy

code = runpy.run_path(str(Path(__file__).resolve().parents[2] / 'tools/build-in2020-front-mode.py'))
mode = dict(index=1, register_records=57, inferred_fps=30.1,
            raw_csi_fields=[4, 0, 227, 14, 0, 0], raw_timing_fields=[206930000, 2560, 2685],
            streams=[dict(raw_fields=[0, 43, 0, 0, 2328, 1748, 10, 1])])

def row(a, v):
    return dict(address=a, value=v, delay_us=0)

init = [row(0x136, 0x13), row(0x137, 0x33)] + [row(0x8000 + n, 0) for n in range(153)]
setting = [row(0x112, 10), row(0x113, 10), row(0x114, 3)]
for reg, value in ((0x34c, 2328), (0x34e, 1748), (0x342, 2560), (0x340, 2685)):
    setting += [row(reg, value >> 8), row(reg + 1, value & 255)]
setting += [row(0x9000 + n, 0) for n in range(57 - len(setting))]
assert code['mode_gate'](mode, init, setting)['stride'] == 2944
cases = []
for addr, value in ((0x34d, 0), (0x112, 12), (0x114, 1), (0x341, 0)):
    s = copy.deepcopy(setting)
    next(r for r in s if r['address'] == addr)['value'] = value
    cases.append((mode, init, s))
for bad in (row(0x100, 1), row(0xa00, 0), row(0x1234, 256), dict(address=0x1234, value=0, delay_us=10001)):
    s = copy.deepcopy(setting);s[-1] = bad;cases.append((mode, init, s))
for key, value in (('index', 2), ('inferred_fps', 60.02), ('raw_csi_fields', [2, 0]),
                   ('raw_timing_fields', [206930000, 5120, 2685])):
    m = copy.deepcopy(mode);m[key] = value;cases.append((m, init, setting))
for m, i, s in cases:
    try:
        code['mode_gate'](m, i, s)
    except ValueError:
        pass
    else:
        raise AssertionError('unsafe/mismatching mode accepted')
try:
    code['build'](b'unpinned input')
except ValueError:
    pass
else:
    raise AssertionError('unpinned module accepted')
print('Front mode geometry/format/clock/stream/OTP/bounds gate PASS')
