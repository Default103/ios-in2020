# IN2020 native camera

The five sensors have been identified through the recovery kernel. This native
XNU implementation is still in bring-up; no native frame or iOS camera preview
has been demonstrated.

The first target is the fixed-focus IMX471 front sensor: slot2, CCI1 master1,
CSI PHY3. `in2020_camera_board.h` is checked against the measured FDT by
`tools/verify-in2020-camera-board.py`. GPIO107/108 are this master's SDA/SCL;
GPIO103/104 belong to a different controller/master route. MCLK3 uses GPIO97,
reset GPIO78, front VANA gate GPIO24. PM8009 supplies use RPMh resources ldof3
(1056000uV) and ldof7 (1800000uV); the shared pm8150a BOB resource is bobc1,
with a 3008000..3960000uV range. The BOB must not be confused with touch's
ldoa13 or the unrelated reference phone's I2C PM8008.

Live FDT selects CSIPHY1.2.1 and CSID/VFE480, not the earlier prepared 170
headers. Front PHY3 is 0xac70000; selected future raw route is CSID-lite0
0xacd9200 / IFE-lite0 0xacd9000. Camera IOMMU SID mask is 0x400, not the
reference phone's 0x4e0. Actual SIDs and IOVA pool are in the checked header.
The raw route still requires its own clock/power/SMMU and frame verification.

Patch0131 adds a dormant driver. Only `in2020cam=1` starts its observer thread.
It reads the five pin states and PMIC observer registers at candidate PM8009
peripherals, with arbiter-version and unique APID checks. Its only MMIO writes
are bounded SPMI EXT_READL observer commands; it does not configure PMICs,
pins, supplies, clocks, sensors, ISP or DMA. Unsupported stages are rejected.
No camera device node or fake frames are exposed.

Next: build this overlay after the pending speaker test, temporarily boot the
camera preflight and review actual rail identities/values and reset state.
Then add isolated front power-up, 19.2MHz MCLK3, bounded CCI read of ID0x0471
and cleanup. Capture follows with a bounded 2328x1748 RAW10 mode, subject to
independent module/register checks, CSI480 setup and shared SMMU allocation.
The candidate mode has not been executed. Sensor tables and calibration stay
local; board code, tests and documentation may be published.

Provenance: OnePlusOSS/android_kernel_oneplus_sm8250 commit
98719830b0de5584f5588bbe9f4c8304d97a64c6, recovery FDT and validated identity
probes. Relevant CCI/480 source files are pinned in local camera-reference/kona/
isp480-manifest.json. HakiOS a3384e03 camera code was reviewed as a reference;
its board constants and unsafe buffer lifetimes are not imported.

`tools/build-in2020-front-mode.py` extracts only the pinned measured IMX471
module into a local candidate plan. The mode's dimensions, line/frame timing,
RAW10 format and four-lane setting are checked independently against register
values. It retains 155 init and 57 mode records, rejects stream-control/OTP
registers and unsupported widths/operations/delays. No stream-on is included
and the tool performs no hardware I/O. Its output remains private and has not
been executed on the sensor. Synthetic gate tests run in host CI.


Stage 2 (`in2020cam=2`) integrates native front-camera power, MCLK3,
bounded CCI configuration, CSI480/IFE-lite Bus v3 and one SMMU-backed frame.
It exports full 2328x1748 MIPI RAW10 (2912-byte stride) to a unique
`/private/var/tmp/in2020-front-raw10-<machtime>.bin`. The 64-byte little-endian
I20FRAME header includes geometry, CRC32 and capture time. Export requires
fresh DMA completion, intact guards, changed payload, clean shutdown and
stable CRC. There is no test-pattern source. Buffers remain wired, and
uncertain cleanup retains the isolated mapping until reboot.

Build with `tools/build-in2020-camera-raw.py` and a kernel containing patch
0132. Private sensor/PHY data is inserted locally into ADT, never committed.
Host checks and cross-compilation pass; actual native frame capture remains
unverified. Earlier stage-1-only next steps above are superseded by this
integrated capture at the user's request.

After returning from the native boot, retrieve the exact successful export
path recorded in the kernel log using `tools/read-in2020-native-raw.py`.
It reads bounded userdata blocks from root TWRP without mounting or writing
the partition, resolves the HFS+ catalog and checks the complete frame CRC.
The camera itself runs only in XNU. The reader refuses unsupported overflow
extents, ambiguous paths, unexpected geometry and incomplete payloads.
