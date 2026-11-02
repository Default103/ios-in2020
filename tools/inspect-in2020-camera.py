"""Generate a bounded camera board inventory from measured recovery evidence.

No sensor power-up, stream command, register table inference or calibration write.
Vendor module names are candidates; QUERY_CAP does not identify sensor silicon.
"""
import argparse
import hashlib
import json
from pathlib import Path
import re
import struct


def inspect(fdt, query, firmware):
    slots = {}
    for line in query.splitlines():
        if not line.startswith('CAM ') or 'query_errno' in line or 'open_errno' in line:
            continue
        fields = dict((k, int(v)) for k, v in re.findall(r'(\w+)=(\d+)', line))
        slot = fields['slot']
        if slot in slots:
            raise ValueError('duplicate camera slot')
        node = next((p[:-len('/cell-index')] for p, value in fdt.items()
                     if '/qcom,cam-sensor@' in p and p.endswith('/cell-index')
                     and int(value['hex'], 16) == slot), None)
        if node is None:
            raise ValueError('queried slot missing from device tree')
        props = {}
        for name in ('reg', 'cci-master', 'csiphy-sd-index', 'clock-rates', 'gpios',
                     'gpio-reset', 'gpio-vana', 'gpio-vdig', 'gpio-custom1',
                     'rgltr-cntrl-support', 'rgltr-min-voltage', 'rgltr-max-voltage',
                     'rgltr-load-current', 'regulator-names', 'clock-names'):
            value = fdt.get(node + '/' + name)
            if value is not None:
                props[name] = {k: value[k] for k in ('hex', 'strings') if k in value}
        fields.update(dt_node=node, dt_properties=props)
        slots[slot] = fields
    if len(slots) != 5:
        raise ValueError('expected five measured camera slots')
    modules = []
    for path in sorted(firmware.glob('com.qti.sensormodule.*.bin')):
        data = path.read_bytes()
        if len(data) < 64 or not data.startswith(b'QTI Chromatix Header\0'):
            raise ValueError('unknown sensor module header')
        if struct.unpack_from('<I', data, 28)[0] != len(data):
            raise ValueError('sensor module declared length mismatch')
        modules.append(dict(file=path.name, bytes=len(data), sha256=hashlib.sha256(data).hexdigest(),
                            has_sensor_driver_data=b'sensorDriverData\0' in data,
                            has_camera_module_data=b'cameraModuleData\0' in data,
                            payload_semantics_validated=False))
    return dict(status='BOARD_INVENTORY_ONLY_NO_FRAME_CAPTURE',
                silicon_id_probed=False, slots=[slots[k] for k in sorted(slots)],
                candidate_modules=modules)


if __name__ == '__main__':
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--fdt', type=Path, required=True)
    p.add_argument('--query', type=Path, required=True)
    p.add_argument('--firmware', type=Path, required=True)
    p.add_argument('--out', type=Path, required=True)
    a = p.parse_args()
    result = inspect(json.loads(a.fdt.read_text(encoding='utf-8')),
                     a.query.read_text(encoding='utf-8'), a.firmware)
    a.out.write_text(json.dumps(result, indent=2), encoding='utf-8')
    print('camera board inventory:', len(result['slots']), 'slots,',
          len(result['candidate_modules']), 'bounded candidate module headers')
