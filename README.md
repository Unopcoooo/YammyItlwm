# Intel AC9560 原生 AirDrop · Experimental Beta 1

macOS Sequoia **15.8.1（24H32）/ x86_64 / Intel Wireless-AC 9560** 的原生 AirDrop 实验实现。已实现**黑苹果与苹果设备之间的原生 AirDrop 双向文件传输**，包括**同 Apple Account 设备的发送与接收**，以及**不同 Apple Account 设备之间的互相发现与互传**。使用系统 Finder / 分享菜单，没有第三方文件传输协议替代。

**这次发布的意义是打通原生发现、连接和文件收发链路，不是已经达到日常可用的性能、稳定性或兼容范围。**

[Beta 使用与安装说明](docs/awdl-beta.md) · [已知问题与实测](docs/awdl-status.md) · [后续路线与致谢](docs/awdl-roadmap.md) · [最早接收里程碑](docs/awdl-preview.md)

## 请先了解这些限制

- 只在开发者手中的 **一张 AC9560 / 一台电脑** 上实测过。其他 Intel 型号、不同蓝牙芯片、其他 macOS Build、Tahoe 均未完成验证；上游支持某网卡不代表本实验的 AWDL 也支持。
- **独占无线电**：进入 AirDrop 模式时普通 Wi-Fi 会中断；暂不支持 Wi-Fi 与 AWDL 共存。请预留恢复方式。
- Finder 的“允许这些人发现我”从“没有人/关闭接收”切到“所有人”，启动一次**最多五分钟**的会话。五分钟是防误开和风险控制的实验保护，不是硬件或 Apple 协议限制；到期后需重新手动 Off → Everyone。
- 可以手动关闭，但停止和普通 Wi-Fi 恢复可能需要几十秒，历史也出现过更长延迟。界面显示“所有人”不代表租约已经启动。
- **速度很慢，延迟仍有波动**。小文件曾约三秒成功，也有几十秒等待或超时；不能把单次快速成功当吞吐承诺。
- 接收可能成功但不显示进度条；历史还出现手机实际收到、Mac 却显示已拒绝的结果不一致。睡眠唤醒、多轮及大文件稳定性仍需开发。
- 包含针对 **24H32** 的私有接口和蓝牙兼容补丁；只面向能备份、还原 EFI 的实验用户。当前包不自动关闭 SIP/AMFI，也不承诺默认安全策略或任意 OpenCore 环境都能加载。

## 关于这个项目

我刚毕业，个人开发和测试预算有限，暂时没办法购置很多不同型号的网卡来做兼容性验证。这次发布算是抛砖引玉，希望把已有的链路、源码和问题公开出来，欢迎有设备和驱动经验的朋友参与验证、排错和改进。请把它当作一个共同推进的起点。

开发基于 [laobamac/itlwm](https://github.com/laobamac/itlwm) / [OpenIntelWireless](https://github.com/OpenIntelWireless/itlwm)，参考 [Owly](https://github.com/t4t5/owly)、Broadcom 社区实现及 Apple 原生行为；各项目的硬件代码不能直接互换。蓝牙补丁基于 [BrcmPatchRAM](https://github.com/acidanthera/BrcmPatchRAM) 和 [crazyhulk 的社区工作](https://github.com/crazyhulk/BlueToolFixup-FixA2DP-BLE)。原有许可证与署名保留。

## What this beta demonstrates

Native AirDrop file transfers in both directions between the Intel AC9560 Hackintosh and Apple devices, including sending and receiving with devices on the same Apple Account, and mutual discovery and file transfers with devices on different Apple Accounts. The beta also includes a manual Finder receive switch and a bounded five-minute session. **Broader hardware compatibility and stable cold-start transfers are not guaranteed.**

Only one physical AC9560 has been tested, on macOS 15.8.1 build 24H32. The radio is exclusive: infrastructure Wi-Fi disconnects during AWDL. Coexistence, reliability, sleep/wake behavior, progress UI, and throughput remain ongoing work. A three-second small-file success is not a speed benchmark. The maintainer recently graduated and cannot afford a broad hardware test matrix; reproducible community reports and code contributions are welcome.

The beta needs **two matched kexts plus a user-space helper**. The helper observes the native receive setting; the kext owns the bounded radio/DMA lifecycle. Do not replace your entire EFI or copy machine identity/security settings from another computer. See [the installation and rollback guide](docs/awdl-beta.md) before trying it.

## Build and verification

Xcode and Python 3 are required. Pin MacKernelSDK to the revision used by the existing source preview:

```sh
git clone https://github.com/acidanthera/MacKernelSDK.git MacKernelSDK
git -C MacKernelSDK checkout 3f750085caa17ec3a7880f11c11bf4f48cd6a164
xcodebuild -project itlwm.xcodeproj -scheme AirportItlwm-Sequoia \
  -configuration Release ARCHS=x86_64 CODE_SIGNING_ALLOWED=NO build
python3 scripts/test-awdl-beta.py
bash tools/awdl-receive-switch/build.sh
```

The release contains the exact tested binaries. Rebuilding changes metadata/UUIDs and requires reviewing and updating the helper's UUID/hash pins; do not mix arbitrary CI artifacts with the frozen helper. BlueToolFixup source/build notes are in [bluetooth/BlueToolFixup](bluetooth/BlueToolFixup/README.md). The tagged source corresponds to the tested implementation; byte-for-byte reproducible builds are not claimed.

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
