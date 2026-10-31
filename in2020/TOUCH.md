# SEC input adaptation phase 1

Portable decoder + tracker only; hardware transport and HID integration are pending.

Raw inclusive sensor maxima: X 1439, Y 3168, from captured IN2020 Android input and SEC DT properties. Current boot framebuffer: 1080 x 2376. Scaling maps these inclusive endpoints to X 1079, Y 2375 without Redmi coordinates. A missing press followed by MOVE can start a contact; invalid packets do not mutate state; a release retains the last good coordinates. Drain release_next after transport loss or event-queue loss.

Validation: ten contacts, extrema, duplicate moves, invalid coordinates, recovery release and 200000 pseudorandom events; Windows host test passed with assertions enabled at O2 and warnings-as-errors. Decoder and tracker also cross-compiled as freestanding ARM64. No claim of hardware I2C operation.

Planned integration: verify QUP13 FIFO firmware and clocks first, then use only the SEC event protocol at 0x48. No flash/calibration/test-mode commands. Preserve existing generic HID dispatch and match its coordinate crop to the boot framebuffer. No generic Redmi regulator or pin programming on IN2020.
