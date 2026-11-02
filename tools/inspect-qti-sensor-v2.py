"""Inventory the measured QTI Parameter Parser V2 sensor-module container.

Layout derived from this phone's own seven module files. Does not execute tables.
Probe field interpretations are candidates pending independent schema validation;
power enum numbers are deliberately left untranslated and must not be programmed.
"""
from pathlib import Path
import argparse
import hashlib
import json
import math
import struct


def inspect(data):
    def require(test, message):
        if not test:
            raise ValueError(message)

    def span(offset, size):
        require(0 <= offset <= len(data) and 0 <= size <= len(data) - offset,
                'file range out of bounds')
        return data[offset:offset + size]

    require(160 <= len(data) <= 32 * 1024**2, 'unsupported file length')
    require(data.startswith(b'QTI Chromatix Header\0'), 'wrong container magic')
    require(struct.unpack_from('<I', data, 28)[0] == len(data), 'declared size mismatch')
    require(data[40:80].split(b'\0')[0].startswith(b'Parameter Parser V2.0.0 '),
            'unsupported parser layout')
    table, count = struct.unpack_from('<II', data, 152)
    require(table >= 160 and 1 <= count <= 32, 'invalid section table')
    span(table, count * 24)
    sections = {}
    for i in range(count):
        kind, offset, size = struct.unpack('<QQQ', span(table + i * 24, 24))
        require(kind not in sections and offset >= table + count * 24,
                'duplicate or overlapping table section')
        span(offset, size)
        require(all(offset + size <= a or a + n <= offset for a, n in sections.values()),
                'overlapping sections')
        sections[kind] = offset, size
    require(0 in sections and 1 in sections, 'missing symbols or payload')
    symbols_at, symbols_size = sections[0]
    payload_at, payload_size = sections[1]
    require(symbols_size % 72 == 0 and 0 < symbols_size // 72 <= 65536,
            'invalid symbol table length')
    symbols = {}
    for offset in range(symbols_at, symbols_at + symbols_size, 72):
        ident, raw_name, kind, mode, start, size = struct.unpack('<Q32sQQQQ', span(offset, 72))
        require(ident > 0 and ident not in symbols, 'duplicate symbol id')
        require(start <= payload_size and size <= payload_size - start, 'symbol outside payload')
        name = raw_name.split(b'\0', 1)[0].decode('ascii', errors='strict')
        symbols[ident] = dict(name=name, kind=kind, mode=mode, offset=start, size=size)

    def payload(item):
        return span(payload_at + item['offset'], item['size'])

    roots = [s for s in symbols.values() if s['kind'] == 1 and s['name'] == 'sensorDriverData']
    require(len(roots) == 1 and roots[0]['size'] >= 24 * 8, 'missing sensor root')
    root = struct.unpack_from('<24Q', payload(roots[0]))
    name_symbol = symbols.get(root[4])
    require(name_symbol is not None and name_symbol['name'] == 'sensorName', 'invalid sensor name reference')
    name = payload(name_symbol).split(b'\0', 1)[0].decode('ascii', errors='strict')
    configurations = [s for s in symbols.values() if s['name'] == 'moduleConfiguration']
    require(len(configurations) <= 1, 'ambiguous module configuration')
    camera_id = None
    if configurations:
        require(configurations[0]['size'] == 320, 'unsupported module configuration shape')
        camera_id = struct.unpack_from('<Q', payload(configurations[0]))[0]
        require(camera_id <= 31, 'invalid module camera id')

    def powers(count, ident):
        symbol = symbols.get(ident)
        require(symbol is not None and symbol['name'] == 'powerSetting' and 0 < count <= 64,
                'invalid power array reference')
        require(symbol['size'] == count * 24, 'unexpected power array shape')
        content = payload(symbol)
        return [list(struct.unpack_from('<3Q', content, i * 24)) for i in range(count)]

    tables = []
    for ident, symbol in symbols.items():
        if symbol['name'] != 'regSetting' or not symbol['size']:
            continue
        require(symbol['size'] % 80 == 0, 'unsupported register command shape')
        content = payload(symbol)
        words = 0
        operations = set()
        for start in range(0, len(content), 80):
            slave_n, slave_id, reg, data_n, data_id, addr_type, data_type, op, delay_n, delay_id = \
                struct.unpack_from('<10Q', content, start)
            for n, ref, expected in ((slave_n, slave_id, 'slaveAddr'),
                                     (data_n, data_id, 'registerData'),
                                     (delay_n, delay_id, 'delayUs')):
                child = symbols.get(ref)
                require(child is not None and child['name'] == expected and
                        n <= 65536 and child['size'] == n * 8,
                        'invalid register-array reference')
            require(reg <= 0xffffffff and 1 <= addr_type <= 4 and 1 <= data_type <= 4,
                    'unsupported register address/data width')
            words += data_n
            operations.add(op)
        tables.append(dict(symbol=ident, records=len(content) // 80, data_words=words,
                           raw_operation_values=sorted(operations),
                           structural_references_validated=True,
                           command_semantics_validated=False))

    # V2 resolution records have 51 64-bit fields. Resolve counted references
    # before reporting candidates. These are not executable capture settings.
    resolutions = [s for s in symbols.values() if s['name'] == 'resolutionData']
    require(len(resolutions) == 1, 'missing or ambiguous resolution array')
    modes_raw = payload(resolutions[0])
    require(0 < len(modes_raw) <= 64 * 408 and len(modes_raw) % 408 == 0,
            'unsupported V2 resolution shape')
    modes = []
    for offset in range(0, len(modes_raw), 408):
        fields = struct.unpack_from('<51Q', modes_raw, offset)
        fps = struct.unpack_from('<d', modes_raw, offset + 11 * 8)[0]
        stream = symbols.get(fields[2])
        table = symbols.get(fields[20])
        require(stream is not None and stream['name'] == 'streamConfiguration' and
                0 < fields[1] <= 8 and stream['size'] == fields[1] * 64,
                'invalid resolution stream reference')
        require(table is not None and table['name'] == 'regSetting' and
                0 < fields[19] <= 65536 and table['size'] == fields[19] * 80,
                'invalid resolution register reference')
        require(math.isfinite(fps) and 0 < fps <= 1000,
                'invalid inferred resolution frame rate')
        streams = []
        for at in range(0, stream['size'], 64):
            values = struct.unpack_from('<8Q', payload(stream), at)
            streams.append(dict(raw_fields=list(values)))
        modes.append(dict(index=len(modes), inferred_fps=fps,
                          raw_timing_fields=list(fields[3:11]),
                          raw_csi_fields=list(fields[12:18]),
                          register_symbol=fields[20], register_records=fields[19],
                          streams=streams, capture_semantics_validated=False))

    return dict(status='OFFLINE_LAYOUT_INFERRED_NOT_SENSOR_PROBED',
                sha256=hashlib.sha256(data).hexdigest(), sensor_name=name,
                symbol_count=len(symbols), sections=len(sections),
                module_configuration_camera_id=camera_id,
                inferred_probe_fields=dict(slave_address_field=root[5], addr_type_field=root[6],
                                           data_type_field=root[7], id_register_field=root[8],
                                           expected_id_field=root[9], id_mask_field=root[10]),
                raw_power_up_triplets=powers(root[15], root[16]),
                raw_power_down_triplets=powers(root[17], root[18]),
                register_table_inventory=tables,
                resolution_candidates=modes,
                power_enum_translation_validated=False, register_tables_executed=False)


if __name__ == '__main__':
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('module', type=Path)
    p.add_argument('--out', type=Path)
    a = p.parse_args()
    result = inspect(a.module.read_bytes())
    encoded = json.dumps(result, indent=2)
    if a.out:
        a.out.write_text(encoded, encoding='utf-8')
    else:
        print(encoded)
