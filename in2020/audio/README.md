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
The native kernel port currently exposes only `in2020adsp=1` (SMEM/IPCC survey)
and `in2020adsp=2` (survey plus embedded firmware validation). Higher values
are rejected. The generic `adsp=1` / `adsp_step` entry point is not installed.
The power-lock callback panics if called, rather than silently pretending that
shared RPMh arbitration is implemented. No DSP authentication/start or amplifier
enable is reachable through this port's boot entry point. It produces diagnostics,
not speaker output. Original later-stage source is retained for further review,
not authorized as a working IN2020 hardware configuration.

Firmware comes only from `/in2020-adsp` in the ADT. The host builder verifies
all captured hashes; no firmware is added to git or installed on the root.
The ELF32/Hexagon header gate checks size, endianness, machine, program-header
layout and count before walking any segments. Firmware segment fit and sizes
are then checked by the inherited walker. Map failures stop read-only inspection.

Remaining audio work: validate initial DSP survey, review IN2020 AOP/RPMh/SCM,
implement shared power arbitration and staged DSP startup, TFA9874 control and
protection, AFE/PCM playback, then the iOS AudioServerPlugIn bridge. Do not use
Apollo's CS35L41 amplifier tables. No claim of working audio is made.
