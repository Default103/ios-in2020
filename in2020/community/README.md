# Community driver integration status — 2026-10-31

No replacement phone image has been produced or started. No phone partition was written for this work. Camera adaptation has not started; the requested touch/power/GPU joint hardware acceptance is pending.

## Completed

- All 88 supplied package checksums match. Available historical source-lock inputs match their hashes; eight listed external inputs are absent.
- Pristine package copied with notices/licenses to projects/ios-in2020/integration/community-20261031/vendor; original source-share folder unchanged.
- Isolated Linux host tests: 17 of 22 supplied Python suites pass using an explicitly recorded GCC backend. Five cannot complete because generated inputs are absent (GPU generator/resource inventory/overlay, charger presence header, TSENS board header). This is not a hardware test. Direct supplementary SEC event and HFI helper tests both pass.
- Added portable community SEC decoder -> bounded queue -> current IN2020 pixel-coordinate/HID sink adapter. Host test covers press/move/release, endpoints, UP-priority burst coalescing and invalid coordinates. Its queue supports only contact zero. Packet processing must drain to HID between incoming packets; serialized worker use is required.
- ARM64 freestanding objects compiled for adapter, SEC decoder, queue and bq battery cache. Declaration-only string header supplies prototypes for kernel libkern linkage checks; no libc or kernel was linked. Live touch transport and live battery sampling producer are not connected.

## Required integration inputs

The package targets a resident UEFI CPU0 loader with XNU dispatched onto other application CPUs, plus Hakios/Mesa interfaces. Our working phone image uses a bare-metal Linux Image shim and ios-sweet XNU on CPU0. Do not enable the supplied seven-core flags or unmodified UMI GPU driver in the current image.

Please obtain matching open source/build inputs, without private Apple payloads or device-unique credentials:

1. Full loader/EFI source and exact commit/config, including prepare_native_touch_gpu_smp_loader.py and the round70/90 build/payload scripts.
2. Matching XNU patches and exported shared input/battery ABI (sys/hakios.input.h), loader-return/log helpers, HakiosBatteryReport/SetValidity, HakiosGPU service and MiniIOSurface integration.
3. Mesa/hakios-drm source patches, build config and userspace loading/injection interface.
4. prepare_gpu_candidate.py, generate_gpu_profile.py, board-resources.json, actual adapted candidate-overlay and generated board headers (charger_presence_pin.h / tsens_board.h where relevant). A source lock alone does not provide these files.
5. Authoritative target GPU ZAP placement for this phone/firmware. The supplied generated header uses 0x8e21a000 + 0x2000; our captured recovery FDT marks CDSP reserved memory at [0x8d500000,0x8e900000). The proposed ZAP range lies inside that reservation. This is an unresolved resource overlap, not proof the GPU is unusable. GPU/GMU/SMMU main MMIO and chip ID 0x06050002 do match the captured phone resources.

## Hardware acceptance before camera work

Touch: confirm chip identity, powered bus, real finger swipe/unlock/navigation and clean stop/restart. Battery: real bq samples consumed by the current XNU IOPMPowerSource; no fabricated external-power or charging claims. GPU: adapted resource/SMMU/firmware checks, actual FD650 hardware rendering/readback and stop cleanup. Joint test must use the same image/run. Contributor historical round90 documentation is context, not proof for our image.

Host logs: integration/community-20261031/host-results. ARM64/bridge checks: integration/community-20261031/build/portable-integration.json. Original known-working quiet GUI image remains untouched.
