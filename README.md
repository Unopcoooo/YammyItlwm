# 有1吗？

## Intel AC9560 原生 AirDrop 接收实验

在 macOS Sequoia 15.8.1（24H32）上，Intel Wireless-AC 9560（PCI 8086:a370）通过系统 AirDrop 从 MacBook 成功接收了一个 15.3 MB 的 ZIP 文件。这个源码预览基于 AirportItlwm 的 Sequoia 驱动。实验会话独占无线电，实测中普通 Wi-Fi 超过试验时限才恢复。

[源码预览](https://github.com/Unopcoooo/itlwm/releases/tag/ac9560-awdl-preview-1) · [实测记录](docs/awdl-preview.md) · [开发路线与 owly 致谢](docs/awdl-roadmap.md) · [版本状态与发布说明](docs/awdl-status.md)

**实验限制与风险。** 当前版本没有用户可自行操作的 AirDrop 接收开关。实测时，我通过 Codex 调用项目的本地开发工具，手动开启约两分钟的 AWDL 接收窗口。会话期间会独占无线电，普通 Wi-Fi 可能暂时中断，退出后也可能延迟恢复。

本版本仅供实验和试用，按现状提供，安全性、稳定性和兼容性均无保证。请在可恢复的测试环境中使用，并自行承担试用风险。

### 已验证的范围

- **原生 Mac → 黑苹果接收**：系统发现、接收确认、文件传输和 ZIP 完整性检查已通过；不是只出现设备头像。
- **公开源码预览与后续本地版本分开记录**：`ac9560-awdl-preview-1` 对应 DDA9 里程碑。后续 19E2 已在同一次开机内通过两轮停止恢复；D3BE 接收队列候选仅通过离线验证，尚待实测。后两者不属于这个预览标签。
- **性能仍在优化**：15.3 MB 文件的已记录传输约需 75–83 秒，不是高速 AirDrop。后续本地测试发现接收队列溢出和周期性停顿，尚未证明优化后的吞吐。
- **尚未验证**：黑苹果发送、iPhone 双向互通、普通 Wi-Fi 与 AWDL 同时工作、其他 Intel 型号及 Tahoe 上的 AWDL。

**下载说明：目前是源码预览，不是安装即用的 AirDrop 驱动包。** 公开树尚未提供实测所用的完整会话控制工具；仅编译并安装 kext 不能复现实验。`scripts/load.sh` / `unload.sh` 是继承的 itlwm 开发脚本，不是本实验的启动或恢复步骤。详见[版本状态与发布说明](docs/awdl-status.md)。

## Any tops here?

### Experimental native AirDrop receive on Intel Wireless-AC 9560

[Source preview](https://github.com/Unopcoooo/itlwm/releases/tag/ac9560-awdl-preview-1) · [Development roadmap and owly credits](docs/awdl-roadmap.md)

On November 8, 2026, an Intel Wireless-AC 9560 Hackintosh received a
15,340,445-byte ZIP from a MacBook through macOS AirDrop over AWDL. The
receiver logged the completed upload and extraction, the sender received
the completion response, and the received ZIP passed an archive integrity
check. See the [test record](docs/awdl-preview.md).

This source preview is based on [laobamac/itlwm](https://github.com/laobamac/itlwm)
and its AirportItlwm Sequoia target. The verified configuration was macOS
Sequoia 15.8.1 (24H32), Intel Wireless-AC 9560 (PCI 8086:a370), Intel
Core i5-9300H, and Intel 9000-series API 46 firmware.

The experimental session takes exclusive use of the radio. In the verified
run, infrastructure Wi-Fi did not recover within the trial deadline and
became available later. The result covers MacBook → Hackintosh receive in
this configuration. Reverse transfers, iPhone interoperability, other
adapters, and concurrent infrastructure Wi-Fi remain unverified for this
preview.

**Experimental limitations and risks.** This preview has no user-facing toggle for AirDrop receive. In the hardware test, I used Codex to run the project's local development tooling and manually open an AWDL receive window of about two minutes. The session takes exclusive use of the radio, so normal Wi-Fi may be interrupted and may take time to recover afterward.

This preview is provided as is for experimentation and testing. Security, stability, and compatibility are not guaranteed. Use a test environment you can recover and proceed at your own risk.

### What the result establishes

Native Mac → Hackintosh discovery, the system receive prompt, completed file
reception and ZIP validation have been observed. A device icon alone is not
counted as a successful transfer. Recorded 15.3 MB transfers took roughly
75–83 seconds; high throughput is not yet established.

The public `ac9560-awdl-preview-1` tag records the DDA9 milestone. Subsequent
local build 19E2 passed two stop/recovery rounds in one boot. D3BE is an
RX-queue candidate with offline validation only, pending hardware testing.
Neither later build is included in that preview tag. See the
[version status, evidence and release requirements](docs/awdl-status.md).

This is a source preview, not an install-and-use AirDrop package. The complete
session-control tooling used in the hardware trials is not included. Building
and installing the kext alone does not reproduce the trial. The inherited
`scripts/load.sh` and `scripts/unload.sh` target a different itlwm development
workflow; they are not instructions for starting or restoring this AWDL session.

## Build from source

With Xcode and Python 3 installed, clone the preview branch first:

```sh
git clone --branch awdl/sequoia-ac9560-preview https://github.com/Unopcoooo/itlwm.git
cd itlwm
git clone https://github.com/acidanthera/MacKernelSDK.git MacKernelSDK
git -C MacKernelSDK checkout 3f750085caa17ec3a7880f11c11bf4f48cd6a164
xcodebuild -project itlwm.xcodeproj -scheme AirportItlwm-Sequoia \
  -configuration Release ARCHS=x86_64 CODE_SIGNING_ALLOWED=NO build
```

The published tree is for development review. The hardware run used an
explicit experimental receive session and native service discovery.

---

[![Version](https://img.shields.io/github/v/tag/laobamac/itlwm?style=flat&label=Downloads)](https://github.com/laobamac/itlwm/releases)
# itlwm — inherited upstream documentation

The sections below describe the upstream driver targets. Their downloads and
compatibility claims do not establish support for this experimental AWDL branch.

**An Intel Wi-Fi Adapter Kernel Extension for macOS, based on the OpenBSD Project.**

## Documentation

We highly recommend exploring our documentation before using this Kernel Extension:

- [Intro](https://OpenIntelWireless.github.io/itlwm)
- [Compatibility](https://openintelwireless.github.io/itlwm/Compat)
- [FAQ](https://openintelwireless.github.io/itlwm/FAQ)

## AirportItlwm on macOS Sequoia

The `AirportItlwm-Sequoia` target supports macOS Sequoia 15.2 and later on x86_64
through the native Wi-Fi interface. It has been tested on macOS 15.8 (24H23).
Sequoia and Tahoe use separate ABI headers and build products.

With the same MacKernelSDK checkout described below:

```sh
xcodebuild -project itlwm.xcodeproj -scheme AirportItlwm-Sequoia \
  -configuration Release ARCHS=x86_64 CODE_SIGNING_ALLOWED=NO build
```

Use this kext only on Darwin 24.2.0–24.99.99. Keep the Tahoe kext restricted to
Darwin 25.x when both systems share an OpenCore configuration. HE remains opt-in
with `itlwm_he=1`, and the Tahoe feature limitations below also apply to Sequoia.

## AirportItlwm on macOS 26




This fork supports macOS Tahoe 26.x (x86_64) through the native Wi-Fi interface,
including scanning, connecting, network switching, and private Wi-Fi addresses.

- **Wi-Fi 6 (802.11ax / HE) is disabled by default.**
  Add `itlwm_he=1` to enable it on supported adapters.
- WPA2-Personal and WPA2/WPA3 transition networks with optional PMF are supported.
  WPA3-only, mandatory PMF, AWDL / AirDrop, and MLO are not supported.
- Device support follows the upstream hardware table; Tahoe compatibility still
  requires validation on each adapter.

Download Sequoia or Tahoe **Release** and **Debug** builds from [this fork's Releases](https://github.com/laobamac/itlwm/releases).
CI builds both systems and updates the alpha release after all builds pass on `main`.

To build locally, install Xcode and Python 3, and check out MacKernelSDK commit
`3f750085caa17ec3a7880f11c11bf4f48cd6a164` in `MacKernelSDK/`:

```sh
xcodebuild -project itlwm.xcodeproj -scheme AirportItlwm-Tahoe \
  -configuration Release ARCHS=x86_64 CODE_SIGNING_ALLOWED=NO build
```

## Questions and Issues

Check out our [FAQ Page](https://openintelwireless.github.io/itlwm/FAQ) for more info.

If you have other questions or feedback, feel free to [![Join the chat at https://gitter.im/OpenIntelWireless/itlwm](https://badges.gitter.im/OpenIntelWireless/itlwm.svg)](https://gitter.im/OpenIntelWireless/itlwm?utm_source=badge&utm_medium=badge&utm_campaign=pr-badge&utm_content=badge).

For this experimental AWDL fork, use the [test-report checklist](docs/awdl-status.md#reporting-results). Do not send fork-specific AWDL failures to upstream as if they were supported upstream features. The upstream links above remain useful for ordinary Wi-Fi documentation.

## Credits

- [Acidanthera](https://github.com/acidanthera) for [MacKernelSDK](https://github.com/acidanthera/MacKernelSDK)
- [Apple](https://www.apple.com) for [macOS](https://www.apple.com/macos)
- [AppleIntelWiFi](https://github.com/AppleIntelWiFi) for [Black80211-Catalina](https://github.com/AppleIntelWiFi/Black80211-Catalina)
- [ErrorErrorError](https://github.com/ErrorErrorError) for UserClient bug fixes
- [Intel](https://www.intel.com) for [Wireless Adapter Firmwares](https://www.intel.com/content/www/us/en/support/articles/000005511/network-and-io/wireless.html) and [iwlwifi](https://wireless.wiki.kernel.org/en/users/drivers/iwlwifi)
- [Linux](https://www.kernel.org) for [iwlwifi](https://wireless.wiki.kernel.org/en/users/drivers/iwlwifi)
- [mercurysquad](https://github.com/mercurysquad) for [Voodoo80211](https://github.com/mercurysquad/Voodoo80211)
- [OpenBSD](https://openbsd.org) for [net80211, iwn, iwm, and iwx](https://github.com/openbsd/src)
- [pigworlds](https://github.com/OpenIntelWireless/itlwm/commits?author=pigworlds) for DVM devices support, MIRA bug fixes, and Tx aggregation for MVM Gen 1 devices
- [rpeshkov](https://github.com/rpeshkov) for [black80211](https://github.com/rpeshkov/black80211)
- [usr-sse2](https://github.com/usr-sse2) for implementing the usage of Apple RSN Supplicant and bug fixes
- [zxystd](https://github.com/zxystd) for developing [itlwm](https://github.com/OpenIntelWireless/itlwm)

## Acknowledgements

- [@penghubingzhou](https://github.com/startpenghubingzhou)
- [@Bat.bat](https://github.com/williambj1)
- [@iStarForever](https://github.com/XStar-Dev)
- [@stevezhengshiqi](https://github.com/stevezhengshiqi)
- [@DogAndPot](https://github.com/DogAndPot) for providing resources and help for system configuration
- [@Daliansky](https://github.com/Daliansky) for providing Wi-Fi cards
