"""Synthetic front-route bounds and wiring tests; no device access."""
from pathlib import Path
import copy
import runpy
import struct

code = runpy.run_path(str(Path(__file__).resolve().parents[2] / 'tools/verify-in2020-camera-board.py'))
inspect, verify_header = code['inspect'], code['verify_header']
fdt = {}

def cells(key, *values):
    fdt[key] = {'hex': struct.pack('>' + 'I' * len(values), *values).hex()}

def strings(key, *values):
    fdt[key] = {'hex': ('\0'.join(values) + '\0').encode().hex()}

sensor = '/soc/qcom,cci@ac50000/qcom,cam-sensor@2'
cci = sensor.rsplit('/', 1)[0]
cells('/soc/pinctrl@f000000/phandle', 1)
for key, value in [('cell-index', 2), ('csiphy-sd-index', 3), ('cci-master', 1), ('clock-rates', 19200000)]:
    cells(sensor + '/' + key, value)
cells(cci + '/reg', 0xac50000, 0x1000)
cells(cci + '/gpios', 1, 105, 0, 1, 106, 0, 1, 107, 0, 1, 108, 0)
cells(sensor + '/gpios', 1, 97, 0, 1, 78, 0, 1, 24, 0)
for ident, supply, regulator, resource, low, high in (
    (2, 'cam_vio', 'pm8009_l7', 'ldof7', 1800000, 1800000),
    (3, 'cam_vdig', 'pm8009_l3', 'ldof3', 1056000, 1056000),
    (4, 'cam_vana', 'pm8150a_bob', 'bobc1', 3008000, 3960000)):
    parent = '/soc/rsc@18200000/rpmh-regulator-' + resource
    node = parent + '/regulator-' + regulator
    cells(node + '/phandle', ident)
    strings(node + '/regulator-name', regulator)
    strings(parent + '/qcom,resource-name', resource)
    cells(node + '/regulator-min-microvolt', low)
    cells(node + '/regulator-max-microvolt', high)
    cells(sensor + '/' + supply + '-supply', ident)
for node, compatible, base, size in (
    ('/soc/qcom,camcc@ad00000', 'qcom,camcc-kona-v2', 0xad00000, 0x10000),
    ('/soc/qcom,csiphy@ac70000', 'qcom,csiphy-v1.2.1', 0xac70000, 0x2000),
    ('/soc/qcom,csid-lite0@acd9200', 'qcom,csid-lite480', 0xacd9200, 0x1000),
    ('/soc/qcom,ife-lite0@acd9000', 'qcom,vfe-lite480', 0xacd9000, 0x2200)):
    strings(node + '/compatible', compatible)
    cells(node + '/reg', base, size)
smmu = '/soc/qcom,cam_smmu/msm_cam_smmu_ife'
cells(smmu + '/iommus', *[v for sid in (0x800, 0x801, 0x840, 0x841, 0xc00, 0xc01, 0xc40, 0xc41) for v in (5, sid, 0x400)])
cells(smmu + '/qcom,iommu-dma-addr-pool', 0x7400000, 0xd8c00000)
pinctrl = '\n'.join(f'[{n}] = PINGROUP({n}, {tile}, {function}, NA)' for n, tile, function in (
    (97, 'NORTH', 'cam_mclk'), (78, 'NORTH', 'gpio'), (24, 'SOUTH', 'gpio'),
    (107, 'NORTH', 'cci_i2c'), (108, 'NORTH', 'cci_i2c')))
board = inspect(fdt, pinctrl)
header = (Path(__file__).parent / 'in2020_camera_board.h').read_text()
verify_header(board, header)

mutations = [
    (sensor + '/cci-master', '00000000'),
    (sensor + '/clock-rates', '016e3600'),
    (sensor + '/cam_vdig-supply', 'ffffffff'),
    (sensor + '/gpios', '00'),
    ('/soc/qcom,csid-lite0@acd9200/compatible', b'qcom,csid170\0'.hex()),
    (smmu + '/qcom,iommu-dma-addr-pool', '0000000000000000'),
    ('/soc/rsc@18200000/rpmh-regulator-ldof7/qcom,resource-name', b'ldoc1\0'.hex()),
]
for key, value in mutations:
    bad = copy.deepcopy(fdt)
    bad[key]['hex'] = value
    try:
        inspect(bad, pinctrl)
    except (ValueError, KeyError):
        pass
    else:
        raise AssertionError(key)
for bad in (header.replace('0x0f96b000', '0x0f967000'), header.replace('"ldof3"', '"ldoc3"'),
            header.replace('0x400u', '0x4e0u')):
    try:
        verify_header(board, bad)
    except ValueError:
        pass
    else:
        raise AssertionError('kernel header mismatch accepted')
print('Camera route fixture, wrong-master/IP/rail/pool/header rejection PASS')
