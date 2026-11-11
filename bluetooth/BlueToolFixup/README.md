# Matched BlueToolFixup E2CB source

Base: crazyhulk/BlueToolFixup-FixA2DP-BLE commit d8bd62a781d04f5593d03258f4092ec9403e7805, retaining upstream BrcmPatchRAM attribution and this directory's LICENSE. The 24H32 patterns were independently relocated and checked against the tested local x86_64 bluetoothd. No Apple system binary is distributed.

Five patches select standard advertising sets, LE5 event mask, per-set random address, extended scans, and the actual extended-create-connection input. Both file-page offsets and full original bytes must match; the Build check is deferred until bluetoothd page validation. The initial fifth-patch getter attempt failed on hardware and is not in this release. E2CB is the corrected result.

Place a pinned MacKernelSDK checkout and a compatible Lilu.kext development SDK in this directory (same dependency layout as BrcmPatchRAM), then build the BlueToolFixup target in BrcmPatchRAM.xcodeproj with Release and CODE_SIGNING_ALLOWED=NO. The tested Lilu runtime was1.7.2. Do not mix arbitrary newer binary patterns or OS Builds with this package.

Host test `test-advsets.cpp` requires a matching original/unpatched 24H32 bluetoothd input. Reading the running system file may expose already modified cached pages, so its hash is not a reliable unpatched input. No unpatched Apple binary is included. The frozen E2CB binary SHA-256 and runtime identity are in docs/awdl-beta-manifest.json at the repository root.
