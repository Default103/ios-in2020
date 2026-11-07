# IN2020 native camera

Current handoff: [camera adaptation report, 2026-11-07](../../docs/IN2020-camera-handoff-20261107.md).
Read that report before continuing; it distinguishes measured results from hypotheses,
indexes source files and patches, and records build/readback instructions.

## Verified status

- IMX471 front: native XNU RAW10 capture, export, independent length/CRC checking,
  and a recognizable real scene have been demonstrated repeatedly.
- S5K3M5 tele: native RAW export and host CRC verification passed; the user accepted
  the scene preview. Dynamic range/exposure and image quality remain open.
- IMX689 main and IMX586 ultra: offline mode/board/C-PHY preparation only;
  no native frame verification yet.
- The SpringBoard camera test panel appears and front snapshot/read/CRC/main-thread
  update succeed. UIView rasterization also finishes, but the user still cannot see
  the image and subsequent display submissions stop. Visible preview is unresolved.
- No continuous video, calibrated color, automatic exposure/focus, AVFoundation
  integration or stock Camera.app support has been established.

## Entry points

`in2020cam=1` runs the observer. `in2020cam=2` performs an automatic finite native
capture/export. `in2020cam=3` installs the versioned UIKit test library and exposes
`/dev/in2020-camera` for manual front/tele snapshots. Current UI v3 uses an opaque
UIView backing and logs heartbeat/main/raster progress.

`in2020_camera_capture.c` owns the capture and cdev flow; sensor/clocks/dma modules
handle their hardware domains. `in2020_camera_plan.h` validates private modes.
`in2020_camera_user.h` is the shared ioctl ABI. The matching cumulative patches in
`xnu/patches/` are the actual XNU build inputs. Keep sources and patches in sync.

Patch 0149 adds file export to validated system-button captures. Earlier stage 3
builds kept pixels only in RAM: their `exported=1` result flag did not prove a file
existed. Require an actual export path with error=0/residual=0, then independent
readback. Patch 0149 has not yet been verified on the phone at this handoff.

Build with `tools/build-in2020-camera-raw.py` or `tools/build-in2020-camera-system.py`;
private original-module/PHY/loader assets are required locally. Read exact successful
export paths with `tools/read-in2020-native-raw.py`, then create display-only previews
with `tools/preview-in2020-native-raw.py`. Keep images, firmware and logs out of git.

Hardware provenance: OnePlusOSS SM8250 commit
`98719830b0de5584f5588bbe9f4c8304d97a64c6`, measured recovery FDT and verified native
register/readback results. Board constants and unsafe lifetimes from other phones
must not be copied blindly. There is no test-pattern source in the native frame path.
