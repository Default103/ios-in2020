# IN2020 v3: native surface targets and static wallpaper recovery

This package retains eight CPUs, 60 Hz and the verified GPU blit display path. Device visual results are pending; build or self-test success does not establish wallpaper or Maps/FaceTime recovery.

## Evidence and changes

The v2 CA capture shows 800 frames averaging 9672 us and 2000 frames averaging 6613 us, with 2–3 us readback, four shared surfaces and no GPU fault. Its remaining rejected targets include padded b3a8 plane zero with bytes-per-element 1, w30r XR storage and unnamed eight-byte half-float storage. The new bridge checks their layout and bounds, accepts the observed b3a8 encoding and converts the added formats through float render targets. The main BGRA fast path and previous shader/color corrections remain.

The exact deployed wallpaper 365 contains a native HeritageStripe configuration but no static image. The previous CPU preview hook was never called in the reviewed capture. An opt-in, one-shot SpringBoard request now uses the native wallpaper color service, taking #90CE7B from that resource for both appearances. This is a visible static color fallback; it does not restore the dynamic stripe renderer. The marker records requested, not visually verified.

Maps/FaceTime recovery also includes a narrowly scoped extension-container ABI correction and standard-range UIKit snapshots. Bounded app constructor, lifecycle and original os_log evidence distinguishes missing app startup from subsequent rendering failures. Full Metal support is not provided.

## Deployment and recovery

The user's latest instruction authorizes direct temporary Fastboot boot. No boot/recovery/userdata partition flash is part of this package. A before-launchd updater accepts only six compiled-in /usr/local library/test destinations; it verifies the old v2 or new version hashes and root ownership before writes, backs up old bytes, stages verified files and atomically renames them. Failure before launchd refuses an unknown or mixed installation; simulated mid-commit failure restores earlier replacements. Current iOS apps/preferences/user data are preserved. These six file replacements persist after temporary boot.

The v3-repair boot performs the update, enables the CA bridge and one-shot wallpaper request. v3-check keeps the update and diagnostics but disables CA injection and wallpaper changes. v3-restore-libs carries the exact old six v2 binaries and restores them, leaving preferences unchanged. Restore is a separate temporary boot, never a userdata flash.

## Validation and device observations

Production updater code passed local transaction regressions for successful update, repeated update, restore, repeated restore, bad owner, corrupted payload, unknown active bytes, interrupted staging and mid-commit rollback. Production pixel helpers passed all 1024 XR code roundtrips, black/white, NaN/bounds and in-place half component ordering. Final Apple SDK builds, downloaded signatures/checksums and payload bounds must pass before boot.

After boot, wait at least 60 seconds after SpringBoard starts, then check the lock/home wallpaper, blur, Maps and FaceTime. Record whether each app opens and whether any chrome/content is visible. Capture the new SWEETLOG through the existing TWRP procedure. Look for updater completion, all four sweet-format-test targets PASSED, actual bridge target formats PASSED, sweet-ui launch/window events and the wallpaper native request. Native request or self-test success alone is not proof of visual recovery. Keep the v2 capture and frozen baseline images.
