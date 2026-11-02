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
