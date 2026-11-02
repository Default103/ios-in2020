"""Synthetic boundary fixtures; no proprietary sensor payloads are required."""
from pathlib import Path
import runpy
import struct

inspect = runpy.run_path(str(Path(__file__).resolve().parents[1] /
                            'tools/inspect-qti-sensor-v2.py'))['inspect']


def fixture():
    root = [0] * 24
    root[4:11] = [2, 0x20, 2, 2, 0x16, 0x471, 0xffff]
    root[15:19] = [1, 3, 1, 4]
    mode = bytearray(408)
    struct.pack_into('<3Q', mode, 0, 0, 1, 10)
    struct.pack_into('<d', mode, 88, 30.0)
    struct.pack_into('<2Q', mode, 152, 1, 5)
    items = [
        ('sensorDriverData', struct.pack('<24Q', *root)),
        ('sensorName', b'synthetic\0'),
        ('powerSetting', struct.pack('<3Q', 8, 1, 1)),
        ('powerSetting', struct.pack('<3Q', 8, 0, 1)),
        ('regSetting', struct.pack('<10Q', 0, 6, 0x100, 1, 7, 2, 1, 0, 0, 8)),
        ('slaveAddr', b''), ('registerData', struct.pack('<Q', 0)),
        ('delayUs', b''), ('resolutionData', bytes(mode)),
        ('streamConfiguration', struct.pack('<8Q', 0, 0x2b, 0, 0, 640, 480, 10, 1)),
        ('moduleConfiguration', struct.pack('<40Q', 3, *([0] * 39))),
    ]
    symbols, pool, offsets = bytearray(), bytearray(), {}
    for ident, (name, value) in enumerate(items, 1):
        offsets[name] = len(pool)
        symbols += struct.pack('<Q32sQQQQ', ident, name.encode(), ident == 1,
                               0, len(pool), len(value))
        pool += value
    header = bytearray(160)
    header[:21] = b'QTI Chromatix Header\0'
    parser = b'Parameter Parser V2.0.0 fixture\0'
    header[40:40 + len(parser)] = parser
    struct.pack_into('<II', header, 152, 160, 2)
    payload = 208 + len(symbols)
    table = struct.pack('<6Q', 0, 208, len(symbols), 1, payload, len(pool))
    data = header + table + symbols + pool
    struct.pack_into('<I', data, 28, len(data))
    return data, payload, offsets


def reject(data):
    try:
        inspect(data)
    except ValueError:
        return
    raise AssertionError('malformed fixture accepted')


data, pool, offsets = fixture()
result = inspect(data)
assert result['sensor_name'] == 'synthetic'
assert result['module_configuration_camera_id'] == 3
assert result['resolution_candidates'][0]['inferred_fps'] == 30
assert result['resolution_candidates'][0]['streams'][0]['raw_fields'][4:6] == [640, 480]
assert not result['register_tables_executed']
for length in range(len(data)):
    truncated = bytearray(data[:length])
    if length >= 32:
        struct.pack_into('<I', truncated, 28, length)
    reject(truncated)

for at, fmt, value in [
    (152, '<I', len(data)), (156, '<I', 0xffffffff),
    (160 + 24, '<Q', 0),  # duplicate section kind
    (208 + 64, '<Q', len(data)),  # root extends beyond pool
    (pool + offsets['regSetting'] + 32, '<Q', 999),
    (pool + offsets['regSetting'] + 40, '<Q', 0),
    (pool + offsets['resolutionData'] + 8, '<Q', 9),
    (pool + offsets['resolutionData'] + 16, '<Q', 999),
    (pool + offsets['resolutionData'] + 88, '<d', float('nan')),
    (pool + offsets['resolutionData'] + 88, '<d', float('inf')),
    (pool + offsets['resolutionData'] + 88, '<d', 0.0),
    (pool + offsets['resolutionData'] + 152, '<Q', 2),
    (pool + offsets['resolutionData'] + 160, '<Q', 999),
    (pool + offsets['moduleConfiguration'], '<Q', 32),
]:
    corrupt = bytearray(data)
    struct.pack_into(fmt, corrupt, at, value)
    reject(corrupt)
print('V2 sensor parser: valid synthetic mode, all truncations and malformed references PASS')
