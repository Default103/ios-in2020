"""Extract the pinned IMX471 2328x1748 candidate into a private capture plan.

This does not communicate with a sensor or emit a stream-on command. Register
semantics beyond the checked mode fields still need native hardware validation.
"""
from pathlib import Path
import argparse
import hashlib
import json
import runpy
import struct

PINNED = 'b2b80b9be31ede0209f63f00d2138b6900548fc6c5bd25b997c7f38d5972cba7'


def require(ok, message):
    if not ok:
        raise ValueError(message)


def mode_gate(mode, init, registers):
    require(mode['index'] == 1 and mode['register_records'] == 57, 'unsupported initial mode')
    require(29 <= mode['inferred_fps'] <= 31, 'unsupported initial frame rate')
    require(mode['raw_csi_fields'][:2] == [4, 0], 'only four-lane D-PHY supported')
    require(len(mode['streams']) == 1 and mode['streams'][0]['raw_fields'] ==
            [0, 43, 0, 0, 2328, 1748, 10, 1], 'unsupported RAW10 stream')
    require(len(init) == 155 and len(registers) == 57, 'unexpected table counts')
    for table in (init, registers):
        for item in table:
            require(set(item) == {'address', 'value', 'delay_us'}, 'unknown register record fields')
            reg, value, delay = item['address'], item['value'], item['delay_us']
            require(isinstance(reg, int) and 0 <= reg <= 65535 and
                    isinstance(value, int) and 0 <= value <= 255 and
                    isinstance(delay, int) and 0 <= delay <= 10000, 'register record out of bounds')
            require(reg != 0x0100 and not 0x0a00 <= reg <= 0x0aff, 'stream control/OTP access rejected')
    init_values = {v['address']: v['value'] for v in init}
    values = {v['address']: v['value'] for v in registers}
    require(init_values.get(0x0136) == 0x13 and init_values.get(0x0137) == 0x33, 'wrong input clock')
    require([values.get(r) for r in (0x0112, 0x0113, 0x0114)] == [10, 10, 3], 'RAW10/lane registers disagree')
    def word(reg):
        require(reg in values and reg + 1 in values, 'missing geometry byte')
        return values[reg] * 256 + values[reg + 1]
    require((word(0x034c), word(0x034e), word(0x0342), word(0x0340)) ==
            (2328, 1748, 2560, 2685), 'mode geometry/timing registers disagree')
    require(mode['raw_timing_fields'][1:3] == [2560, 2685], 'metadata timing disagrees')
    return dict(width=2328, height=1748, raw_bits=10, lanes=4, line_length=2560,
                frame_length=2685, mclk_hz=19200000, stride=(2328 * 5 // 4 + 63) & ~63,
                mode_index=1, frame_rate_candidate=mode['inferred_fps'])


def build(data):
    require(hashlib.sha256(data).hexdigest() == PINNED, 'not the measured IMX471 module')
    inspect = runpy.run_path(str(Path(__file__).with_name('inspect-qti-sensor-v2.py')))['inspect']
    report = inspect(data)
    require(report['sensor_name'] == 'imx471' and report['module_configuration_camera_id'] == 2,
            'wrong sensor/slot')
    table, count = struct.unpack_from('<II', data, 152)
    sections = {kind: (start, size) for kind, start, size in
                (struct.unpack_from('<QQQ', data, table + i * 24) for i in range(count))}
    at, size = sections[0]
    payload, _ = sections[1]
    symbols = {}
    for offset in range(at, at + size, 72):
        ident, name, kind, mode, start, length = struct.unpack_from('<Q32sQQQQ', data, offset)
        symbols[ident] = (name.split(b'\0')[0].decode(), payload + start, length)
    def values(ident, expected):
        name, start, length = symbols[ident]
        require(name == expected and length % 8 == 0, 'wrong register child')
        return list(struct.unpack_from('<' + 'Q' * (length // 8), data, start))
    def registers(ident):
        name, start, length = symbols[ident]
        require(name == 'regSetting' and length % 80 == 0, 'wrong register array')
        result = []
        for offset in range(start, start + length, 80):
            sn, sid, reg, n, ref, address_type, data_type, op, dn, delay_ref = struct.unpack_from('<10Q', data, offset)
            require(sn == 0 and values(sid, 'slaveAddr') == [] and n == 1 and dn == 1 and
                    address_type == 2 and data_type == 1 and op == 0, 'unsupported bus width/operation/count')
            result.append(dict(address=reg, value=values(ref, 'registerData')[0],
                               delay_us=values(delay_ref, 'delayUs')[0]))
        return result
    wrappers = [v for v in symbols.values() if v[0] == 'initSettings']
    require(len(wrappers) == 1 and wrappers[0][2] == 24, 'ambiguous init wrapper')
    delay, n, ref = struct.unpack_from('<3Q', data, wrappers[0][1])
    require(delay == 0 and n == 155, 'unsupported init wrapper')
    init = registers(ref)
    mode = report['resolution_candidates'][1]
    setting = registers(mode['register_symbol'])
    checked = mode_gate(mode, init, setting)
    return dict(status='OFFLINE_FRONT_MODE_CHECKED_NOT_EXECUTED', module_sha256=PINNED,
                sensor='IMX471', slot=2, cci=1, master=1, slave_address_7bit=0x10,
                mode=checked, initialization=init, mode_settings=setting,
                stream_on_included=False, register_tables_executed=False,
                native_frame_captured=False)


if __name__ == '__main__':
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--module', type=Path, required=True)
    p.add_argument('--out', type=Path, required=True)
    a = p.parse_args()
    result = build(a.module.read_bytes())
    a.out.write_text(json.dumps(result, indent=2), encoding='utf-8')
    print('IMX471 mode1: dimensions/timing/RAW10/lanes agree; 155 init + 57 mode records; not executed')
