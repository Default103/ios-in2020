# IN2020 audio, staged port

The measured amplifiers are **TFA9874 revision 0x0c74**, at 0x34 and 0x35.
Recovery read both identity registers without reset, amplifier programming or
calibration writes. `audio_tfa9894.ko` is the multi-chip driver's filename;
it does not establish the chip model. Preserve this distinction in later work.

This directory adapts the ADSP implementation from aTechGeekDev/ios-apollo
0784a28634a08b17fb4cb6c3d5fa3c3c2422fa9d, patch 0138. That implementation
attributes its base to JopangJovan/hackios-xnu-kernel-umi / HackiOS (APSL 2.0).
Original attribution is retained; the accompanying license is LICENSE.APSL.

The IN2020 carveout is 0x8fe00000 + 0x02500000, not the Mi 10's address.
The native kernel port exposes `in2020adsp=1` (SMEM/IPCC survey), `=2` (survey
plus embedded firmware validation), `=7` (start then stop the DSP), and `=11`
(keep the DSP/APR link up), and `=12` (APR then a silent AFE port test).
Other values are rejected. The generic `adsp=1` /
`adsp_step` entry point is not installed. Steps 1/2 passed on the phone; the
Milestones 7 and 11 now passed on device; 12 requires its own validation. No amplifier enable or PCM
sink is installed. These milestones do not imply working speaker output.

The inherited standalone TCS0 writer has been removed. The IN2020 RPMh module
retains the tested touch VRM API and replaces the build's previous RPMh module
with one shared implementation. Its private transaction engine supports 2 ARC
or the original 3 VRM commands using only an empty idle TCS1; TCS0 stays firmware
owned. Locking, fresh completion checks, IRQ masking/restoration and transaction
quarantine are shared. The touch resource/voltage whitelist is unchanged.
Only exact ARC levels 0/384 for lcx.lvl and lmx.lvl are accepted, with strict
live command-DB bounds, slave class, addresses, uniqueness and auxiliary-level
validation. An absent or malformed resource table prevents the power request.

Milestones 7/11 also perform bounded native TFA identity and OTP-status reads
through GPIO44/45. Only addresses 0x34/0x35 and registers 0x03/0xf0/0xf5 are
accessible during the initial probe; the AFE gate can additionally read register
0x00 to require PWDN=1 and AMPE=0 on both chips. The bus
must initially be idle with an expected pin function. Open-drain transactions
never drive high, have a 100ms deadline, and restore both pin configurations and
output latches. No reset GPIO, amplifier data register, OTP programming or
calibration routine is exposed. MTP/R25 values are raw diagnostics and must not
be interpreted if the associated read-success flag is false.

Firmware comes only from `/in2020-adsp` in the ADT. The host builder verifies
all captured hashes; no firmware is added to git or installed on the root.
The ELF32/Hexagon header gate checks size, endianness, machine, program-header
layout and count before walking any segments. Firmware segment fit and sizes
are then checked by the inherited walker. Map failures stop read-only inspection.

Remaining audio work: validate staged DSP authentication/start and APR on device, TFA9874 control and
protection, AFE/PCM playback, then the iOS AudioServerPlugIn bridge. Do not use
Apollo's CS35L41 amplifier tables. No claim of working audio is made.
# IN2020 AFE packet preparation

`in2020_afe_packet.h` prepares only fixed tertiary MI2S packets (port 0x1004,
48 kHz, stereo, 24-bit samples, SD1, CPU master; clock 0x104 at 3.072 MHz,
COUPLE_NO). It has no transport or hardware access and is not installed in
the earlier step-7/11 kernel. New overlay 0125 installs `in2020_afe.c`, called
only after APR readiness with milestone 12. `afe_packet_test.c` checks independent wire fixtures and
invalid actions. DSP acceptance, pin mux, AFE start/stop and PCM remain untested.
The measured GPIO137 data1 mux is function 3; GPIO133/134/135 use function 1.
Do not copy the reference phone's primary TDM configuration or amplifier driver.

The port diagnostic snapshots all four pin configurations/latches before any
write, accepts only GPIO or the intended MI2S mux, and restores pins after
acknowledged port/clock shutdown. Every APR command checks opcode, payload
length, echoed command and zero status. A start timeout still triggers STOP;
uncertain shutdown preserves the peripheral mux and starts no further stream.
GPIO sampling reports activity only, not measured clock frequency. Amplifier
power-down is read again afterward. No amplifier register data is written.

