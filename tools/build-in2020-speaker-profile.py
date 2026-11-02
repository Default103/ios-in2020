"""Derive private speaker-only register operations from this phone's pinned CNT.
No calibration profile, DSP patch, EEPROM/OTP data or captured payload enters git.
"""
import hashlib
import runpy
import struct
import zlib
from pathlib import Path


def build(data):
    if hashlib.sha256(data).hexdigest() != 'ae54fd5a741ef2de1b157421bf820dfa208e5cfdce939b60c76d77bc7c6cf8ed':
        raise ValueError('unrecognized phone speaker container')
    runpy.run_path(str(Path(__file__).with_name('inspect-tfa-container.py')))['inspect'](data)
    def desc(at):
        word = struct.unpack_from('<I', data, at)[0]
        return word & 0xffffff, word >> 24
    def name(at):
        p, kind = desc(at)
        assert kind == 3
        return data[p:data.index(0, p)].decode('ascii')
    def operations(at, count):
        result = []
        for i in range(count):
            p, kind = desc(at + i * 4)
            if kind == 2:
                reg, value, mask = struct.unpack_from('<BHH', data, p)
            elif kind == 16:
                value, field = struct.unpack_from('<HH', data, p)
                reg, shift, width = field >> 8, (field >> 4) & 15, (field & 15) + 1
                assert shift + width <= 16 and value < 1 << width
                mask = ((1 << width) - 1) << shift
                value <<= shift
            elif kind in (1, 5, 17):
                continue  # profile reference, dummy patch, default marker
            else:
                raise ValueError('unsupported speaker descriptor')
            result.append(struct.pack('<4H', reg, mask, value, 0))
        return result
    body = bytearray(struct.pack('<I', 2))
    for i, address in enumerate((0x34, 0x35)):
        p, kind = desc(46 + i * 4)
        assert kind == 0 and data[p + 2] == address
        ops = operations(p + 12, data[p])
        speakers = []
        for k in range(data[p]):
            q, kind = desc(p + 12 + k * 4)
            if kind == 1 and name(q + 4) == 'speaker':
                speakers.append(operations(q + 8, data[q] - 1))
        assert len(ops) == 51 and speakers and len(speakers[0]) == 6
        ops += speakers[0]
        body += struct.pack('<HH', address, len(ops)) + b''.join(ops)
    return b'TFSP0001' + struct.pack('<II', 16 + len(body), zlib.crc32(body)) + body
