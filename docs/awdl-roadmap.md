# 下一步与参考来源

Beta首先验证完整功能链路；接下来并行于证据充分的边界内继续推进：

1. 稳定同账号冷/热启动、多个peer、重复会话和原生UMI前置状态，解决成功/拒绝结果不一致与接收进度UI问题。
2. 优化取消、停止后普通Wi-Fi恢复及睡眠唤醒。五分钟是当前保护，不是最终交互设计。
3. 研究单卡STA/AWDL共存，消除隔空投送期间普通网络中断。
4. 依据双端抓包、ACK/队列/可用时隙证据调优吞吐；不会把扩大队列或重试数本身当速度提升。
5. 社区扩展硬件/系统测试矩阵。其他型号不自动继承AC9560的支持声明。

现有Intel IWM数据面仍是主体。Owly的Intel/AWDL实现、Broadcom社区驱动和Apple原生控制面用于核对协议和系统契约，不照搬其他硬件的寄存器、固件或DMA实现。

感谢 [OpenIntelWireless/itlwm](https://github.com/OpenIntelWireless/itlwm)、[laobamac/itlwm](https://github.com/laobamac/itlwm)、[t4t5/owly](https://github.com/t4t5/owly) 及固定Owly研究版本9a676e11对应的社区工作、[BrcmPatchRAM](https://github.com/acidanthera/BrcmPatchRAM)、[BlueToolFixup-FixA2DP-BLE](https://github.com/crazyhulk/BlueToolFixup-FixA2DP-BLE)、BroadcomVTD-Tahoe、SimpleBCM和AppleBCMWLANCompanion。引用不代表这些项目或Apple为本Beta背书。许可证与各自署名保留；不分发Apple系统二进制或个人EFI。
