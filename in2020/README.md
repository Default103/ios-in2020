# IN2020 bring-up modules

Experimental OnePlus 8 Pro IN2020 / SM8250 adaptation. **No IN2020 XNU or iOS boot has been demonstrated.** The repository root README describes upstream Redmi Note 10 Pro results, not this phone.

These modules are independent work in progress:

- `in2020_board.h`: hardware constants supported by the collected board evidence. Actual active touch driver is Samsung SEC S6SY761 at I2C address 0x48. The registered ST FTS and Synaptics alternatives are not bound on the tested device.
- `in2020_probe.c`: CPU identification/timer register probe into caller-supplied RAM. Not a bootable loader. A future entry stub must establish valid stack, exception handling and logging, and check system-register access permissions.
- `sec_event.c` / `sec_event.h`: bounded SEC coordinate-event packet decoder, with no I2C operations, power control, firmware updates or storage access.
- `sec_event_test.c`: synthetic host test fixtures; not captured device packets. Tests remain enabled under optimized compilation.

Protocol reference: OnePlusOSS/android_kernel_oneplus_sm8250 commit `98719830b0de5584f5588bbe9f4c8304d97a64c6`, `drivers/oneplus/input/touchscreen/samsung/s6sy761/sec_drivers_s6sy761.h` and `.c`. This published source branch is a reference, not a claim of bit-for-bit correspondence to the running firmware.

Host test with a C compiler:

```sh
cc -O2 -Wall -Wextra -Werror sec_event.c sec_event_test.c -o sec_event_test
./sec_event_test
```

ARM64 compile check with the portable Zig 0.15.2 toolchain:

```sh
zig cc -target aarch64-freestanding-none -O2 -ffreestanding -nostdlib -mgeneral-regs-only -Wall -Wextra -Werror -c sec_event.c -o sec_event.o
zig cc -target aarch64-freestanding-none -O2 -ffreestanding -nostdlib -mgeneral-regs-only -Wall -Wextra -Werror -c in2020_probe.c -o in2020_probe.o
```

Both host tests and ARM64 compilation passed on Windows. No module has been run on the phone. Screen is a Samsung ANA6706 DSC command-mode panel; safe bootloader scanout refresh, memory ownership, interrupt setup, UFS support and iOS HID integration remain pending.

Phone partition images, private logs, serial numbers, device-tree dumps and Apple firmware/userspace are deliberately not included.
