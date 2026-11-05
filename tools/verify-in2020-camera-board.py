"""Resolve the IN2020 front camera route from measured FDT, without hardware I/O."""
from pathlib import Path
import argparse
import hashlib
import json
import re
import struct


def require(ok, why):
    if not ok:
        raise ValueError(why)


def inspect(fdt, pinctrl):
    def cells(key, count=None):
        require(key in fdt, 'missing ' + key)
        raw = bytes.fromhex(fdt[key]['hex'])
        require(len(raw) % 4 == 0, 'unaligned cells: ' + key)
        values = list(struct.unpack('>' + 'I' * (len(raw) // 4), raw))
        require(count is None or len(values) == count, 'wrong cell count: ' + key)
        return values

    def strings(key):
        raw = bytes.fromhex(fdt[key]['hex'])
        require(raw.endswith(b'\0'), 'unterminated string: ' + key)
        return raw[:-1].decode('ascii').split('\0')

    handles = {}
    for key in fdt:
        if key.endswith('/phandle'):
            value = cells(key, 1)[0]
            require(value and value not in handles, 'duplicate/null phandle')
            handles[value] = key.rsplit('/', 1)[0]

    sensor = '/soc/qcom,cci@ac50000/qcom,cam-sensor@2'
    cci = sensor.rsplit('/', 1)[0]
    require(cells(sensor + '/cell-index', 1) == [2], 'wrong sensor slot')
    require(cells(sensor + '/csiphy-sd-index', 1) == [3], 'wrong CSI PHY')
    master = cells(sensor + '/cci-master', 1)[0]
    require(master == 1, 'wrong front CCI master')
    require(cells(sensor + '/clock-rates', 1) == [19200000], 'wrong MCLK rate')
    require(cells(cci + '/reg', 2)[0] == 0x0ac50000, 'wrong CCI base')
    cci_gpios = cells(cci + '/gpios', 12)
    sensor_gpios = cells(sensor + '/gpios', 9)
    controller = sensor_gpios[0]
    require(handles[controller] == '/soc/pinctrl@f000000', 'wrong GPIO controller')
    require(all(v == controller for v in cci_gpios[::3] + sensor_gpios[::3]), 'mixed GPIO controllers')
    require(all(v == 0 for v in cci_gpios[2::3] + sensor_gpios[2::3]), 'unsupported GPIO flags')
    expected = [('MCLK3', sensor_gpios[1]), ('RESET', sensor_gpios[4]), ('VANA', sensor_gpios[7]),
                ('CCI1-M1-SDA', cci_gpios[6 * master + 1]),
                ('CCI1-M1-SCL', cci_gpios[6 * master + 4])]
    require([v for _, v in expected] == [97, 78, 24, 107, 108], 'unexpected front GPIO route')
    tiles = {'NORTH': 0x900000, 'SOUTH': 0x500000, 'WEST': 0x100000}
    pins = []
    for name, gpio in expected:
        matches = re.findall(r'\[' + str(gpio) + r'\]\s*=\s*PINGROUP\(' + str(gpio) +
                             r',\s*(NORTH|SOUTH|WEST),([^;]*?)\)', pinctrl)
        require(len(matches) == 1, 'missing/ambiguous GPIO group')
        tile, functions = matches[0]
        functions = [v.strip() for v in functions.split(',')]
        if name == 'MCLK3':
            require(functions[0] == 'cam_mclk', 'MCLK mux mismatch')
        if name.startswith('CCI'):
            require(functions[0] == 'cci_i2c', 'CCI mux mismatch')
        pins.append(dict(name=name, gpio=gpio, tile=tile,
                         base=0xf000000 + tiles[tile] + gpio * 4096))
    rails = []
    for supply, target, resource, low, high in (
        ('cam_vio', 'pm8009_l7', 'ldof7', 1800000, 1800000),
        ('cam_vdig', 'pm8009_l3', 'ldof3', 1056000, 1056000),
        ('cam_vana', 'pm8150a_bob', 'bobc1', 3008000, 3960000)):
        ref = cells(sensor + '/' + supply + '-supply', 1)[0]
        require(ref in handles, 'missing supply phandle')
        node = handles[ref]
        require(strings(node + '/regulator-name') == [target], 'wrong regulator target')
        require(strings(node.rsplit('/', 1)[0] + '/qcom,resource-name') == [resource], 'wrong RPMh resource')
        require(cells(node + '/regulator-min-microvolt', 1) == [low] and
                cells(node + '/regulator-max-microvolt', 1) == [high], 'unexpected supply range')
        rails.append(dict(name=supply, regulator=target, resource=resource, min_uv=low, max_uv=high))
    blocks = []
    for node, compatible, base, size in (
        ('/soc/qcom,camcc@ad00000', 'qcom,camcc-kona-v2', 0xad00000, 0x10000),
        ('/soc/qcom,csiphy@ac70000', 'qcom,csiphy-v1.2.1', 0xac70000, 0x2000),
        ('/soc/qcom,csid-lite0@acd9200', 'qcom,csid-lite480', 0xacd9200, 0x1000),
        ('/soc/qcom,ife-lite0@acd9000', 'qcom,vfe-lite480', 0xacd9000, 0x2200)):
        require(compatible in strings(node + '/compatible'), 'wrong camera IP generation')
        require(cells(node + '/reg', 2) == [base, size], 'wrong camera MMIO range')
        blocks.append(dict(node=node, compatible=compatible, base=base, size=size))
    smmu = '/soc/qcom,cam_smmu/msm_cam_smmu_ife'
    entries = cells(smmu + '/iommus', 24)
    sids = entries[1::3]
    require(sids == [0x800, 0x801, 0x840, 0x841, 0xc00, 0xc01, 0xc40, 0xc41], 'wrong camera stream IDs')
    require(entries[2::3] == [0x400] * 8, 'wrong camera SID mask')
    require(len(set(entries[::3])) == 1, 'mixed camera IOMMUs')
    require(cells(smmu + '/qcom,iommu-dma-addr-pool', 2) == [0x7400000, 0xd8c00000], 'wrong camera IOVA pool')
    return dict(status='BOARD_RESOLVED_NOT_NATIVE_CAMERA_TESTED', slot=2, sensor='IMX471',
                cci_base=0xac50000, cci_master=1, phy=3, mclk_hz=19200000,
                pins=pins, rails=rails, blocks=blocks, sids=sids, sid_mask=0x400,
                iova_begin=0x7400000, iova_size=0xd8c00000, frame_capture=False)


def verify_header(board, text):
    values = {key: int(value, 0) for key, value in
              re.findall(r'^#define (\w+) (0x[0-9a-fA-F]+|\d+)u?\b', text, re.M)}
    expected = {'IN2020_CAMERA_SLOT': board['slot'], 'IN2020_CAMERA_CCI': board['cci_base'],
                'IN2020_CAMERA_MASTER': board['cci_master'], 'IN2020_CAMERA_PHY': board['phy'],
                'IN2020_CAMERA_MCLK_HZ': board['mclk_hz'], 'IN2020_CAMERA_SID_MASK': board['sid_mask'],
                'IN2020_CAMERA_IOVA_BEGIN': board['iova_begin'], 'IN2020_CAMERA_IOVA_SIZE': board['iova_size'],
                'IN2020_CAMERA_CAMCC': board['blocks'][0]['base'],
                'IN2020_CAMERA_CAMCC_SIZE': board['blocks'][0]['size'],
                'IN2020_CAMERA_PHY_BASE': board['blocks'][1]['base'],
                'IN2020_CAMERA_CSID_LITE': board['blocks'][2]['base'],
                'IN2020_CAMERA_IFE_LITE': board['blocks'][3]['base'],
                'IN2020_CAMERA_VIO_UV': board['rails'][0]['min_uv'],
                'IN2020_CAMERA_VDIG_UV': board['rails'][1]['min_uv'],
                'IN2020_CAMERA_VANA_MIN_UV': board['rails'][2]['min_uv'],
                'IN2020_CAMERA_VANA_MAX_UV': board['rails'][2]['max_uv']}
    for key, value in expected.items():
        require(values.get(key) == value, 'kernel board mismatch: ' + key)
    for key, rail in zip(('VIO', 'VDIG', 'VANA'), board['rails']):
        require('#define IN2020_CAMERA_' + key + '_RESOURCE "' + rail['resource'] + '"' in text,
                'kernel rail resource mismatch')
    sid_text = re.search(r'in2020_camera_sids\[8\]=\{([^}]+)\}', text)
    require(sid_text and [int(s, 0) for s in sid_text[1].split(',')] == board['sids'], 'kernel SID mismatch')
    actual = [(int(g), int(base, 16), name) for g, base, name in
              re.findall(r'\{(\d+),(0x[0-9a-f]+),"([^"]+)"\}', text)]
    require(actual == [(p['gpio'], p['base'], p['name']) for p in board['pins']], 'kernel GPIO map mismatch')


if __name__ == '__main__':
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--fdt', type=Path, required=True)
    p.add_argument('--pinctrl', type=Path, required=True)
    p.add_argument('--out', type=Path, required=True)
    a = p.parse_args()
    source = a.pinctrl.read_bytes()
    require(hashlib.sha256(source).hexdigest() == '87014b0ea67da00fb7c78f543c0030647a546797a15f3192657d0ca5840820ff',
            'unpinned kona pinctrl source')
    board = inspect(json.loads(a.fdt.read_text(encoding='utf-8')), source.decode())
    verify_header(board, (Path(__file__).resolve().parents[1] / 'in2020/camera/in2020_camera_board.h').read_text())
    a.out.write_text(json.dumps(board, indent=2), encoding='utf-8')
    print('Front route validated: CCI1-M1 GPIO107/108, CSIPHY3, CSID/VFE480; no camera writes')
