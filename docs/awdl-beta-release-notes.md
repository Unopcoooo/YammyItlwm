# AC9560 Native AirDrop — Experimental Beta 1 (r26)

这是一份**完整链路验证 Beta**。在同一张 Intel Wireless-AC 9560（8086:a370）、Intel蓝牙8087:0AAA、macOS Sequoia15.8.1（24H32）上，已实测同Apple Account的iPhone↔黑苹果双向文件传输和两轮手动开关。历史版本还验证过与不同账号原生Mac的发现/收发；本次r26未把所有历史设备重新回归一遍。

## 包含什么
- AirportItlwm r26：D6ED6749-6BCD-332E-871C-667166716DEE。
- BlueToolFixup E2CB：E2CB71B5-BA78-39EB-99AD-24F585C1C38A。
- r26c用户态接收开关辅助服务、匹配源码、安装/回滚说明及SHA-256。
- IntelBluetoothFirmware没有定制；按文档使用上游依赖。不是一份可覆盖整机的EFI。

## 使用前必读
**仅一张AC9560实测。** 其他网卡、蓝牙芯片、macOS Build和Tahoe均未验证。当前独占无线，开启AirDrop模式时普通Wi-Fi中断；每次由Finder手动Off→Everyone启动，最多五分钟，作为防误开保护。用户手动关闭或到期后走退役恢复；恢复可能耗时几十秒甚至更久。

速度很慢，连接/ASK延迟仍会波动。小文件曾约三秒成功，但不能作为吞吐承诺；接收可能没有进度条，历史也有手机已收到而Mac误报拒绝。冷启动、多轮/睡眠稳定性和原生UMI全部完成尚未验收。请使用可恢复的实验环境，勿将上游网卡支持表视为本Beta的AWDL兼容表。

我刚毕业，个人设备预算有限，暂时没法购买很多型号网卡测试。这次算是抛砖引玉，欢迎有设备、有经验的朋友带着版本、硬件ID和可复现结果一起参与。接下来继续推进Wi-Fi共存、稳定性、恢复速度与传输性能调优。

## English
Full-path experimental beta: native same-account iPhone↔AC9560 file transfers and a manual receive switch have succeeded on one physical adapter, on Sequoia15.8.1 build24H32. Exclusive radio; ordinary Wi-Fi disconnects. Five-minute maximum lease, slow transfers, variable latency, incomplete progress UI and historical result inconsistencies. No broader hardware/OS compatibility or daily-use stability is claimed. The recently graduated maintainer cannot afford a broad hardware test matrix; community testing and contributions are welcome. Coexistence, reliability and speed remain ongoing work.

Read the bundled installation/rollback guide. Two matched kexts **plus the user-space helper** are required for this tested workflow. Keep your own EFI/identity; no personal EFI or Apple system binary is included. Verify archive and binary hashes. Matching source and original licenses are included as a separate source asset.
