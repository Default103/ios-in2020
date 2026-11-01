# IN2020 Adreno 650 userland bridge

Pinned source: aTechGeekDev/ios-apollo commit 0784a28634a08b17fb4cb6c3d5fa3c3c2422fa9d.
Original HackiOS MIT attribution is retained in ca-bridge/LICENSE.HackiOS and source headers.
Mesa is fetched at 25.0.7 with SHA256 checked by mesa/build.sh.

Build .github/workflows/in2020-userland.yml on macos-15 with the iPhoneOS SDK.
The artifact contains libOSMesa.8.dylib, libsweet-gl.dylib, libsweet-ca.dylib,
sweet-gpu-test and sweet-gl-test, ad-hoc signed. No Apple firmware or device dumps
are uploaded, and no jailbreak/audio/Bluetooth installer is included.

Kernel patch 0116 supplies the matching msm-style SweetGPU IOSurface import,
GPU fence-aware presentation, planar surfaces, creator-memory surfaces and the
opt-in sweetinject loader. Boot injection requires the corresponding libraries
on the root. Boot without sweetinject retains the software-rendering fallback.
Do not enable this on the phone before packaging and validating both artifacts.

Device operations remain manual at the user's request. No success on IN2020 is
claimed until userspace GPU and CoreAnimation submission counters actually advance.
