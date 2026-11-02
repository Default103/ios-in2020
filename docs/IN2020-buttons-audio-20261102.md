# IN2020 buttons and speaker adaptation

## Buttons

Patch 0122 replaces the inherited Redmi keys driver with PM8150 wiring from
this phone's recovery FDT. `sweetkeys=1` enables polling; existing experiment
images contain `sweetkeys=0` and will continue to disable it until rebuilt.

| Function | Source | HID key |
| --- | --- | --- |
| Power / lock / wake | SID0 PON 0x0810 bit0, active high | 0 (Consumer Power) |
| Volume up | SID0 GPIO6 0xc510 bit0, active low | 1 (VolumeIncrement) |
| Volume down | SID0 GPIO7 0xc610 bit0, active low | 2 (VolumeDecrement) |

GPIO phandle 0x624 resolves to PM8150 pinctrl@c000, not TLMM. PMIC GPIO numbering
starts at 1, so GPIO6/7 are peripherals c5/c6. GPIO type, subtype, enable and
input direction are checked without reconfiguration. Both GPIOs are independent;
volume down is not also emitted from the PON RESIN input.

The v5 core register at +4 is INT_EN, not an APID count. The 512-entry table is
bounded by the measured core resource length 0x1100. The driver verifies the
observed arbiter version 0x50010000 and uses EE0 observer EXT_READL transactions
only. The sole MMIO write issues a read command; it does not write PMIC values.
Polling is every 20ms plus processing; two valid samples debounce an edge.
Five failed polls release held HID keys once; valid reads must debounce again.
Firmware reset policy is preserved. Display handling remains with backboardd
and the existing IOMFB RequestPowerChange implementation (black framebuffer,
not a physical panel suspend driver).

Source reference: OnePlusOSS/android_kernel_oneplus_sm8250,
98719830b0de5584f5588bbe9f4c8304d97a64c6:
drivers/input/misc/qpnp-power-on.c,
drivers/pinctrl/qcom/pinctrl-spmi-gpio.c,
drivers/spmi/spmi-pmic-arb.c. Register facts were checked against those sources;
no Linux driver implementation was copied into the new driver.

## Speakers: incomplete, no output claimed

Live recovery FDT lists two NXP TFA98xx devices at I2C 0x884000, addresses
0x34 (right, reset TLMM91) and 0x35 (left, reset TLMM99). In the current TWRP
they enumerate as 0-0034/0-0035 named tfa98xx, but have no bound driver.
`/proc/asound/cards` has no cards. Enumeration does not prove chip revision or
functional audio. The stock vendor contains audio_tfa9894.ko and tfa98xx.cnt;
this identifies the intended driver family, not a measured hardware ID.

Read-only collection on 2026-11-02 mounted vendor_a at a private /tmp directory
using ext4 ro,noload and unmounted it after capture. Original mixer_paths.xml,
audio_platform_info.xml, audio_policy_configuration.xml, tfa98xx.cnt and the
module are stored privately under M:/ios-in2020/firmware/in2020-audio-stock-20261102.
Their SHA256 manifest and raw recovery evidence remain outside git.

The Apollo reference uses Cirrus CS35L41 amplifiers; its amplifier programming
and calibration cannot be applied to these NXP devices. Current IN2020 XNU has
no imported audio DSP/PCM driver or iOS hardware audio HAL. Remaining work:

1. Resolve the IN2020-selected mixer path and obtain matched Qualcomm ADSP
   firmware, memory reservations, clocks, power, IPC and AFE routing.
2. Implement the TFA9894 control transport, verify silicon IDs and parse the
   matched container; validate device calibration and speaker protection.
3. Connect bounded PCM buffers to the DSP and the iOS audio HAL, including
   start/stop, mute, volume and fault shutdown.
4. Test protected low-level playback, then iOS media/volume and lock/wake.

No speaker enable, calibration write, module loading, root image update or
partition flash was performed. The speakers are not adapted yet.

## Validation and pinned audio source

Local optimized Windows key-state fixtures passed. The full host CI job
41443511757 passed the key-state tests under address/undefined sanitizers and
verified exact source/patch consistency. This does not exercise PMIC hardware.
Kernel CI 41443275083 remained queued on macos-15; a macos-15-intel runner choice
was added to allow the full kernel compile to proceed on the alternate pool.

The separate official audio repository is
https://github.com/OnePlusOSS/android_vendor_qcom_opensource_audio_kernel_sm8250,
Android 11 commit 0ac91e8996028893ddca8295e59d187c1cc8a281. Its kona.c names both
TFA DAI components at 0-0034 and 0-0035, on tertiary MI2S playback. The TFA
source calls send_tfa_cal_apr and send_tfa_cal_in_band, implemented by q6afe.c;
this establishes an external Qualcomm DSP dependency, not just an I2C amp switch.
The matched source was downloaded with per-file hashes to the private reference
directory; it was not added to this repository.

`tools/inspect-tfa-container.py` inventories metadata without applying tuning.
The actual stock container is 3861 bytes, version 3_01, CRC valid, SHA256
 ae54fd5a741ef2de1b157421bf820dfa208e5cfdce939b60c76d77bc7c6cf8ed.
Its device P is address 0x34 with receiver/speaker/calibrate.cal profiles;
device Q is 0x35 with speaker/speaker/calibrate.cal profiles. Both declared
hardware IDs are zero, so the container cannot establish measured chip identity.
Truncation, CRC corruption and a CRC-correct out-of-bounds device pointer were
rejected by the inspector. This validates metadata only, not all tuning payloads.


After USB reconnection, the mounted read-only modem firmware supplied adsp.mdt
and adsp.b00 through adsp.b18: 20 files, 17141636 bytes total. All 18 PT_LOAD
segments have file sizes within memory sizes; nonempty segment files match the
ELF sizes. Their link span 0x89500000..0x8ba00000 is exactly 0x02500000, matching
the live FDT ADSP reservation size at 0x8fe00000. All load segments carry the
relocatable flag. The candidate relocation delta is 0x06900000; this is an
offline layout check only. Secure authentication, memory assignment and execution
have not been attempted. Private hashes/segment/layout records are beside the
firmware under adsp/. No firmware has been published.

The keys boot builder verifies the frozen eight-core baseline loader, v4 boot
hash and new kernel hash; retains the v4 graphics injection and 120Hz software
cadence; uses the original GPU ADT without the library update node; emits keys-on
and keys-off variants. A host packaging regression using the old kernel passed
with 588-byte command lines and all memory/ABI bounds checks. These old-kernel
host fixtures are explicitly marked DO-NOT-BOOT and are not adapted images.


A separate local compile of the actual new pm8150_keys.c against the downloaded
XNU build tree and generated kernel headers passed for plain ARM64 Mach-O
(5160-byte object). Only the inherited NDEBUG macro redefinition warning was
reported. This checks kernel API declarations and ARM64 assembly, not full
linking or device behavior. The source used by the cloud kernel build remains
836f5f6; later commits add documentation/packaging only.

## Final kernel and temporary boot, 2026-11-02

Kernel CI 41443746692 succeeded in 18m59s, source
836f5f6b688db22c5032cc1de364bcb32270ae26. Downloaded SHA256SUMS matched:
4ba3f5fe6c64445066241c33c57f54694e9fe46a73b3a8c96f8fdd2ed7b989dc.
The host/check workflow 41443511757 also succeeded; its QEMU job uses the
previous kernel and does not exercise the new PMIC driver.

Actual new-kernel images are under
M:/ios-in2020/builds/xnu-41443746692/in2020-keys-tests/:

| Variant | SHA256 | Bytes |
| --- | --- | --- |
| keys-on | 70f0d1ae3a4b6c53508cf414887b361c0102024cbaa1606a21fdcdff05833c33 | 18460672 |
| keys-off | 2a69a4e0f070eaad09e90754c2ec15ac67759c1b8d46c5ecad29f5fcf0034f1c | 18460672 |

Both have 588-byte commands, no ADT library-update node, 8 CPUs, unchanged GPU
firmware and v4 graphics injection, and 120Hz software vsync. Physical panel mode
was not changed. The binary ADT, loader, kernel ABI, payload geometry and boot
header were checked. The generated files are read-only on the host.

The unique recovery device reported kona, was rebooted to bootloader, and its
product/download-size were checked. The keys-on image hash was rechecked and
`fastboot boot` returned Sending OKAY / Booting OKAY in 0.509s. No partition
flash was issued. This proves bootloader acceptance only. The user was asked to
check power lock/wake and volume +/- in iOS; results are pending. No phone
command was issued after the accepted boot. Audio is still unimplemented.
