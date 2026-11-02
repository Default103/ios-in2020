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


## Physical main/front identity and native ADSP preflight results

The bounded V2 inspector decoded six candidate files' register-array shapes:
IMX586/BG 19 tables each (2471 records), IMX689 standard 37/4721, IMX689 MP
37/3751, S5K3M5 22/1172, IMX471 10/332. GC5035 has an unsupported width field
and is rejected; its register tables were not executed. This is structural
validation, not validation of every register's semantics.

The fixed recovery camera-probe tool was then reviewed against the pinned
OnePlus command handler and tested for ABI/packet bounds. It submits only
sensor ID-probe packets plus the exact stock power-up/down sequence. The original
kernel selects regulator defaults from this device tree, reads the identity and
powers down. A standard main-module attempt matched the chip but failed the
vendor check (actual vendor register 0x18 = 0x10). The correct MP variant then
succeeded: slot 0, slave field 0x34, chip 0x0689. Main is therefore IMX689 MP.
Front probe also succeeded: slot 2 / CSI3, slave field 0x20, chip 0x0471 (IMX471).
Both runs logged dropping the regulator links after matching. No stream or
calibration command was submitted. Closing the request-manager fd logs the
stock driver's generic userspace-close warning and idle OIS callbacks; this was
not a camera-process crash or a request to program OIS.

Native ADSP kernel 41480178308, source
0c8c4311f7351ee85aed46e369d14a14a0c9b652, built successfully; kernel SHA256
4933ad0da15dce8a877a67edaf64a4c2a4f02216e49e8af09c614341ef31423e matched.
Host/check workflow 41480180730 also passed. The temporary read-only preflight
boot is 35655680 bytes, SHA256
b85db966b862e6be20f81fad5447495ed78209900d6430e2bd3e80047d82c6ee,
601 command bytes, ADT 17237148 bytes, no root updater. Fastboot accepted it in
0.902s. User returned to Fastboot; verified temporary TWRP boot accepted in
1.651s. Persistent log captured read-only: 396410 bytes / 16 flushes / no wrap,
raw SHA256 7dee0168b4ba19d2f40726dfe749ecfe06c7595e5d028d7329a6325154aec30d.

The phone ran steps 1 and 2 successfully. Apps/ADSP SMEM partition exists at
0x80acf000, size 0x30000, initially empty; SMP2P and GLINK items were absent.
IPCC received-ID read 0xffffffff (recorded, not treated as proof of a mapping
fault). Firmware was whole: 8220-byte MDT including 7528 hash bytes, 18 loadable
segments / 17125196 file bytes, span 0x02500000; relocation from 0x89500000 to
0x8fe00000 passed. Driver ended with `nothing written`. Keys initialized and
SEC touch became active. This validates preflight only, not DSP authentication,
startup, APR/PCM, sound or camera frames.

Further power review found that the generic reference hardcodes TCS0, while this
phone's tested driver reserves TCS0 for firmware and uses only TCS1. No reference
power writes have been enabled. Recovery has neither CONFIG_DEBUG_FS nor
CONFIG_DEVMEM; attempts to read command-DB by those unavailable interfaces
produced no data. Future ARC resource lookup must validate the live command DB
inside XNU and share the existing TCS1 completion/quarantine policy. Phone left
in TWRP while source work continues.

## Next native DSP test and camera mode preparation

Source 655a2b9d43adf13b3a05ca8cbe63b1b220e7a235 starts a new staged DSP
test: workflow 41490492816 (Intel macOS kernel) and 41490491676 (host checks).
The latter's host job passed; QEMU and the native build must be recorded
separately. Step 7 must be tested and its log reviewed before step 11.
This stage replaces the reference's TCS0 writer with the shared TCS1 engine,
validates LCX/LMX exact level indices in command DB, and reads only TFA9874
identity/OTP status. It does not configure AFE/PCM or enable amplifiers.

Further offline V2 parsing resolves resolution-to-stream and resolution-to-
register-table references with exact lengths (408-byte resolution, 64-byte
stream, 80-byte register records). Six supported modules pass; GC5035 remains
rejected. Main IMX689 MP has nine mode candidates and front IMX471 three.
Raw timing/CSI fields and candidate frame rates are reported, without generating
executable capture settings. Synthetic tests cover all file truncations,
malformed references/counts and non-finite frame rates. These checks do not
prove a working CSI link or camera frame.

Live FDT supply resolution shows main VDIG pm8009_l1 at 1104000 uV, front
VDIG pm8009_l3 at 1056000 uV, shared VIO pm8009_l7 at 1800000 uV.
Both VANA references point to pm8150a_bob with sensor GPIO gates; main also
uses pm8150_s5/custom1. Do not substitute the reference camera's PM8008 driver.
CAMCC clock IDs map main to MCLK0 (RCG 0x5000, branch 0x5018) and front
to MCLK3 (0x5054/0x506c), 19.2 MHz TCXO entry in the pinned original
camcc-kona.c. No camera clock or supply was programmed from XNU.

For the future speaker stream, measured FDT names tertiary MI2S pins 133
(bit clock), 135 (word select), 134/137 (data0/1), with active drive 8 mA,
no pull. Mixer paths select two channels S24_LE; original machine driver
defaults to 48 kHz, and audio ION stream is 0x1801 with SID mask 0xf.
These are configuration inputs, not evidence that the clocks or PCM run.

The follow-up recovery `media_inventory --tfa-status` mode skips all camera
opens, checks that neither amplifier has a bound driver, confirms revision
0x0c74, then reads only registers 0x00, 0xf0 and 0xf5. Both devices returned
power control 0x0001 (PWDN set, AMPE clear) and MTP 0x0003 (OTC and EX set).
Both R25 raw values were nonzero; the per-device values remain in private
hardware evidence. No resistance conversion or acoustic/health claim is made.
No amplifier register data, reset or OTP command was written.

Vendor was briefly mounted ro,noload to list DSP dynamic libraries and then
unmounted. No separately named TFA library appeared in that inventory; this is
not proof none is needed. Captured stock adsp.b11 does contain `tfadsp_bgth`,
`capi_v2_tfadsp_rx.c` and `capi_v2_tfadsp_tx.c` strings. This supports examining
the built-in TFADSP path, but only live AFE responses can establish availability.
AFE packet source da931fe passed sanitizer host/QEMU workflow 41492629387;
its QEMU portion uses the previous kernel and does not test physical MI2S.

## DSP-start test boot accepted; runtime log pending

Native run 41490492816 succeeded in 12m57s on the Intel runner, source
655a2b9d43adf13b3a05ca8cbe63b1b220e7a235. Actual CI log contains the
0124 overlay and compilation of the new ADSP, shared RPMh and TFA modules,
with zero failed patch hunks. Downloaded kernel hash matches its manifest:
82f60d91efd3ebbb50e2fb18e833cb2c9701ba173c40642a05fa8042d8ff3520.

Step 7 boot image: 35672064 bytes, SHA256
728e1f4fd89d8d4d4abea2a3bee67af6d5e4451d6765de7a922f113e97d6e354.
Step 11: same size, SHA256
9df59cc311e6b88ae21b2cb0ed8bdddb18145a3c057f8b3f9bad80a09db6a8e8.
Both carry the same pinned firmware/17237148-byte ADT, 601/602-byte commands,
and no library updater. The existing keys, eight CPUs and graphics baseline
are retained. Both are temporary boot artifacts outside git.

After confirming unique root TWRP and no userdata/vendor mount, recovery
rebooted to bootloader. Unique Kona, 805306368-byte download limit and step-7
hash were verified. `fastboot boot` returned Sending/Booting OKAY in 0.905s.
Only step 7 was booted. Runtime DSP start/stop, ARC and native amplifier reads
remain unverified until the user returns to Fastboot and the persistent log
is captured. Step 11 must wait for that review. No speaker output or camera
frame is claimed; automatic return is not yet implemented.

## Native DSP start/stop PASS; APR test runtime pending

User returned to Fastboot. Verified temporary TWRP boot accepted in 1.646s.
Read-only log capture `logs/adsp-start7-20261102` contains 377045 bytes,
15 flushes, no wrap; raw SHA256
15bfc589f31d93fb508bfad0975e7171746fdeabfe4a805a6f482e6e2616e66c.
All seven steps completed. AOP mailbox handshake succeeded. TrustZone accepted
the signature and memory placement; 18 segments were copied/cleared with zero
readback differences. Live ARC lookup found LCX 0x30030 index 6 and LMX
0x30040 index 4 for level 384; shared TCS1 completed both the request and
the subsequent level-0 release. TCS0 was not used by this implementation.

TrustZone authenticated and started ADSP; `slave-kernel=0x6` reported ready
after 29.003ms and requested its own power. At test end DSP acknowledged stop
after 55.691ms, TrustZone shutdown returned success, and AOP accepted load-state
off. Native TFA reads matched both recovery identities and raw MTP/R25 values.
This confirms DSP startup/cleanup and amplifier read transport, not PCM sound.

After reviewing those results, the already-built step-11 image was temporarily
booted (Sending/Booting OKAY, 0.907s). User was asked to run about two minutes
and return to Fastboot. APR/GLINK/audio-service results are still pending that
log. No amplifier enable, speaker playback or camera stream was attempted.

## APR confirmed and remaining physical camera identities matched

Step-11 read-only log: 378200 bytes, 12 flushes, no wrap, raw SHA256
9896b382ee7057686d7c845ce508f965bcd2675f162761ba989fd5fbb709eff2.
GLINK agreed features 1; IPCRTR and apr_audio_svc opened both ways. QRTR node
5 reported eight services. APR core state was 1 after one question/2ms;
service versions include AFE 0x260000, ASM 0x70002, ADM 0x70000. All eleven
steps completed and the DSP/link remained on. No ADSP-step failure, kernel
panic or RPMh timeout was found in this capture. This confirms communication,
not speaker playback.

Silent AFE source 860e994 is building as run 41496215362; host/check run
41496215906 passed. The new milestone 12 uses the original tertiary MI2S
packet shapes after proving both amplifiers off, with pin snapshot/restore
and explicit STOP/clock-off response checks. Its hardware result is pending.

Recovery probes now establish this mapping:

| Slot | CSI PHY | Physical ID | Slave address field | Identity |
| --- | --- | --- | --- | --- |
| 0 | 0 | 0x0689 | 0x34 | IMX689 MP, main (earlier vendor check passed) |
| 1 | 1 | 0x0586 | 0x20 | IMX586, ultra-wide (BG subtype not determined) |
| 2 | 3 | 0x0471 | 0x20 | IMX471, front |
| 3 | 2 | 0x30d5 | 0x5a | S5K3M5, telephoto |
| 4 | 4 | 0x5035 | 0x6e | GC5035, color-filter camera |

The first telephoto attempt incorrectly used slot 1 and NACKed; the kernel
powered it down. Inspection of each stock module's 320-byte
`moduleConfiguration` exposed camera IDs 1 for IMX586 and 3 for S5K3M5.
The corrected telephoto slot 3 and ultra-wide slot 1 probes both matched,
as did color-filter slot 4. Every successful run logged dropping regulator
links afterward. Fixed packets use the stock module's power sequences;
no configure/stream, EEPROM/OIS or OTP/calibration commands were submitted.
The parser now reports the module camera ID to prevent name/order guesses.

GC5035's ID/power metadata use byte addressing at ID register 0xf0, word
data and a 24MHz clock. Only those metadata were used for its probe;
its unsupported register-table width remains rejected by the V2 inspector.
These five identity matches do not establish any XNU capture or ISP support.
