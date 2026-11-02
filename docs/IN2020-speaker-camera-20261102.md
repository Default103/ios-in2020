# IN2020 speaker / camera bring-up

The user confirmed working physical keys and returned to Fastboot, then requested
speaker and camera adaptation. The verified TWRP image was temporarily booted
(Sending/Booting OKAY, 1.642s); no partition flash was issued.

## Working input baseline

Kernel 41443746692 / boot SHA256
70f0d1ae3a4b6c53508cf414887b361c0102024cbaa1606a21fdcdff05833c33
is now user-confirmed for keys. Read-only persistent-log capture contains 423913
valid bytes, 18 flushes, no wrap; raw SHA256
c5845a32d547432a4292691cf86a9533c60615bbc740fe9eb8d9cb21d9ca62e6.
PM8150 GPIO6/7 are enabled MV inputs, APIDs power/up/down = 13/25/26.
Captured power edges are followed by display off/on; volume-up edges are present.
Volume-down was not in this captured interval; the user's working-key report is
separate evidence. Last held states near capture end are not proof of stuck keys.

## Measured amplifier identity corrects the earlier module-name inference

`in2020/media_inventory.c`, built as a static ARM64 Linux executable, read register
0x03 from I2C bus 0 at both 0x34 and 0x35: **0x0c74** for each. The pinned original
driver identifies low byte 0x74 as **TFA9874**, while 0x94 is TFA9894. The module
filename audio_tfa9894.ko covers multiple chips and was not proof of TFA9894.
There was no bound amplifier driver. The only I2C write was the register index
before reading two bytes; no reset, tuning, calibration or speaker-enable data
was written. These measurements supersede any informal TFA9894 hardware label.

The existing stock container is still the matching phone input. The external
DSP dependency remains: TFA host commands go through Q6 AFE on tertiary MI2S.
Apollo's CS35L41 driver is incompatible. The new native XNU ADSP staged source
uses IN2020's measured 0x8fe00000/0x02500000 carveout, embedded firmware only and
a hardened ELF32 Hexagon gate. Only survey/firmware-validation steps 1 and 2 are
enabled; full DSP startup and speaker output are not implemented yet.
See in2020/audio/README.md for provenance and limits.

## Camera query results

The recovery tool issued CAM_QUERY_CAP (0x101) only, using the original 24-byte
cam_control and 40-byte sensor-capability ABI. It did not issue acquire, configure,
probe, stream, calibration or power commands. The original close callback may
perform shutdown if a sensor is active; in this recovery session no camera HAL
or sensor stream was started by our work.

| Slot | Subdevice | Roll/yaw | CSI PHY | Actuator | EEPROM | OIS |
| --- | --- | --- | --- | --- | --- | --- |
| 0 | 11 | 90/180 | 0 | 0 | 0 | 0 |
| 1 | 12 | 270/0 | 1 | 2 | 3 | absent |
| 4 | 13 | 90/180 | 4 | absent | 4 | absent |
| 3 | 14 | 90/180 | 2 | 1 | 1 | 1 |
| 2 | 15 | 270/0 | 3 | absent | 2 | absent |

All slots report non-secure. QUERY_CAP describes wiring, not sensor silicon ID.
Stock vendor has sensor libraries IMX689, IMX586, IMX471, S5K3M5 and GC5035;
seven sensormodule candidates (including IMX689 MP and IMX586 BG alternatives)
were copied privately. Their Chromatix header lengths match actual file sizes.
This does not establish which alternative is fitted or validate register tables.
CAMERA_ICP.elf was also captured. Thirteen files total 8994296 bytes, with per-file
hashes. No camera image has been captured; XNU camera/ISP and iOS camera-device
bridges remain to be implemented after board and sensor-ID bring-up.

Private evidence: M:/ios-in2020/hardware/speaker-camera-20261102 and
M:/ios-in2020/firmware/in2020-camera-stock-20261102. Vendor was mounted ext4
ro,noload only while reading, then unmounted. Phone remains TWRP until a reviewed
diagnostic boot is ready. Calibration files and original sensor blobs stay local.
