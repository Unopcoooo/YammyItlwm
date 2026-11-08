# Intel AC9560 原生 AirDrop 开发路线

## 开发思路

以 AirportItlwm 的 Sequoia 驱动为基础，实验会话先独占 AC9560 的无线电，再逐步打通 AWDL 同步与信道调度、对端和服务发现、数据收发。驱动负责无线链路，文件接收仍由 macOS 系统 AirDrop 处理。

**owly 是重点参考项目。** 它的 Linux 实现为理解 AWDL 帧、信道行为和非 Apple 网卡的互操作提供了参考。相关的 omdrop-owl 项目还有一份 AC9560 在 Linux 上使用独占无线电完成双向传输的用户报告，这为先验证独占模式提供了依据。macOS 端通过 AirportItlwm 接入系统无线栈，并在本机单独验证。[owly](https://github.com/t4t5/owly) · [AC9560 Linux 实测报告](https://github.com/t4t5/omdrop-owl/issues/41)

## 当前里程碑

2026 年 11 月 8 日，AC9560 在 macOS Sequoia 15.8.1（24H32）上，通过系统 AirDrop 从 MacBook 接收了一个 15.3 MB 的 ZIP。双方记录到传输完成，收到的文件通过了 ZIP 完整性检查。会话结束后，普通 Wi-Fi 超过试验时限才恢复。[实测记录](awdl-preview.md)

## 下一步

1. **先把接收和恢复做稳。** 从普通 Wi-Fi 已连接的状态出发，多轮验证发现、接收、退出和重新联网，记录成功率及恢复耗时。用固件扫描提交和完成记录定位恢复延迟。
2. **扩大互操作验证。** 接收流程稳定后，验证黑苹果向 MacBook 发送，再单独验证与 iPhone 的发送和接收。
3. **评估与普通 Wi-Fi 并行。** 验证连接路由器时能否维持 AWDL 调度，并测量传输期间的连接稳定性和吞吐。

# Intel AC9560 native AirDrop development roadmap

## Approach

The project builds on the AirportItlwm Sequoia driver. An experimental session first takes exclusive use of the AC9560 radio, then brings up AWDL synchronization and channel scheduling, peer and service discovery, and data transfer. The driver provides the wireless link, while macOS AirDrop handles file reception.

**owly was a key technical reference.** Its Linux implementation informed our understanding of AWDL frames, channel behavior, and interoperability on non-Apple Wi-Fi cards. A related omdrop-owl user report describes bidirectional transfers on the AC9560 under Linux with exclusive use of the radio. That result motivated testing exclusive mode first. The macOS implementation integrates with the system wireless stack through AirportItlwm and is validated separately on hardware. [owly](https://github.com/t4t5/owly) · [AC9560 Linux test report](https://github.com/t4t5/omdrop-owl/issues/41)

## Verified milestone

On November 8, 2026, the AC9560 received a 15.3 MB ZIP from a MacBook through system AirDrop on macOS Sequoia 15.8.1 (24H32). Both endpoints recorded completion, and the received archive passed its integrity check. Infrastructure Wi-Fi recovered after the trial deadline. [Test record](awdl-preview.md)

## Next steps

1. **Make receive and recovery repeatable.** Start each trial connected to infrastructure Wi-Fi. Repeat discovery, receive, session teardown, and reconnection. Record success rates and recovery times, and use firmware scan submission and completion records to investigate delays.
2. **Expand interoperability testing.** Once receive is repeatable, test sending from the Hackintosh to the MacBook, then test both directions with an iPhone.
3. **Evaluate coexistence with infrastructure Wi-Fi.** Check whether AWDL scheduling works while associated with an access point, and measure connection stability and throughput during transfers.
