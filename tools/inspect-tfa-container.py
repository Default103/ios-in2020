"""Read-only NXP container inventory. Does not program or calibrate an amplifier.

Layout reference: OnePlusOSS/android_vendor_qcom_opensource_audio_kernel_sm8250
0ac91e8996028893ddca8295e59d187c1cc8a281, tfa98xx_parameters.h/tfa_container.c.
Reports device/profile metadata only, not validation of executable tuning data.
"""
import argparse
import hashlib
import json
from pathlib import Path
import struct
import zlib


def inspect(data):
    def require(ok, message):
        if not ok:
            raise ValueError(message)

    def span(offset, length):
        require(0 <= offset <= len(data) and 0 <= length <= len(data) - offset,
                'container offset/length out of bounds')
        return data[offset:offset + length]

    def number(fmt, offset):
        return struct.unpack(fmt, span(offset, struct.calcsize(fmt)))[0]

    def desc(offset, expected=None):
        word = number('<I', offset)
        position, kind = word & 0xffffff, word >> 24
        if expected is not None:
            require(kind == expected, 'unexpected descriptor type')
        return position, kind

    def name(offset):
        position, _ = desc(offset, 3)
        span(position, 1)
        end = data.find(b'\0', position, min(len(data), position + 256))
        require(end >= position, 'unterminated or oversized name')
        return data[position:end].decode('ascii', errors='strict')

    span(0, 46)
    require(data[:2] == b'PM', 'not a parameters container')
    require(number('<I', 6) == len(data), 'declared length mismatch')
    require(zlib.crc32(data[14:]) == number('<I', 10), 'CRC mismatch')
    devices, profiles, live = struct.unpack('<HHH', span(40, 6))
    require(0 < devices <= 16, 'unsupported device count')
    span(46, devices * 4)
    result = dict(bytes=len(data), sha256=hashlib.sha256(data).hexdigest(),
                  version=data[2:6].decode('ascii'), crc_valid=True,
                  declared_profiles=profiles, declared_live_data=live, devices=[])
    for i in range(devices):
        pos, _ = desc(46 + i * 4, 0)
        length, bus, address, function, devid = struct.unpack('<BBBBI', span(pos, 8))
        span(pos + 12, length * 4)
        device = dict(name=name(pos + 8), bus=bus, address=hex(address),
                      function=function, declared_device_id=hex(devid), profiles=[])
        for index in range(length):
            p, kind = desc(pos + 12 + index * 4)
            # Other descriptor payloads (register patches, files, commands)
            # are intentionally opaque; this is not a firmware loader.
            if kind != 1:
                continue
            count, group, ident = struct.unpack('<BBH', span(p, 4))
            require(ident == 0x1234 and count >= 1, 'invalid profile header')
            span(p + 8, (count - 1) * 4)
            device['profiles'].append(dict(name=name(p + 4), group=group))
        result['devices'].append(device)
    return result


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('container', type=Path)
    args = parser.parse_args()
    print(json.dumps(inspect(args.container.read_bytes()), indent=2))
