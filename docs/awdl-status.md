# AWDL Beta 状态与证据 / Status and evidence

更新：2026-11-11。Beta 1 = AirportItlwm r26（D6ED）、BlueToolFixup E2CB、辅助服务r26c。短标识来自Mach-O UUID，不是Git提交号。源码重编译不保证复现相同UUID或二进制哈希。

| 项目 | 验证范围 |
|---|---|
| 硬件 / 系统 | 仅一张AC9560（8086:a370）/ Intel BT USB 8087:0AAA / macOS15.8.1 24H32 / x86_64；Intel9000 API46固件 |
| 同账号 iPhone 双向传输 | 已成功，仍有历史失败与时延波动；同账号自动接收不要求手点接受 |
| 同账号 iPad 常驻条目发送 | 早期17A1+E2CB已有成功样本；本次r26没有重新做iPad回归 |
| 不同账号原生 Mac 发现与收发 | 历史成功；本次r26未重新覆盖所有不同账号设备 |
| 原生接收开关、同次启动重开 | 本轮两次手动开启和关闭都通过；到期/失败/睡眠的全部组合未验证完 |
| 普通Wi-Fi共存 | 未实现；当前为有界独占 |
| 高速与稳定性 | 未完成；不适合作为稳定日用版 |
| 其他Intel型号 / OS Build | 未验证；蓝牙补丁精确限定24H32 |

## 2026-11-11 当前版本实测

- 17:28:07，本机→同账号iPhone：340字节，系统send-finish result=0、transport=awdl，TransferResult success=true；用户观察约三秒。ASK响应约1.06秒。
- 随后新会话，iPhone→本机：记录到三次TransferResult（sender=0）success=true；官方速度指标约257294、210108、85136 bytes/s。指标来自不同文件/事务，不构成受控吞吐基准。
- 两轮手动Off均StopRequested=1、StopReason=1、Failure=0、WorkerPending=0、RecoveryState=2、RecoveryError/Fault/Gap=0。停用到恢复仍有等待，不能说立即恢复。
- 接收成功后仍出现No receive displayable present，进度条展示不保证。
- 同轮原生sendUnicastMI仍可能返回NotReady；文件成功不代表完整原生UMI路径已全部通过。
- 本轮未对所有源/收文件逐个做端到端SHA-256核对。文件完整性与协议完成、系统UI结果分别记录。

## 历史回归不能略去

r7扩大软件重试恶化后已撤回；r11注册接入曾导致Wi-Fi重置；r22数据触发UMI旁路经调用图审计撤回且未实机加载。当前保留了真实DMA完成、取消和恢复门控。历史r25曾发生手机已收到而Mac本地ASK超时显示拒绝，r26对此仅有改进样本，不能说永久解决。

最早源码预览标签ac9560-awdl-preview-1对应DDA9接收里程碑，与此Beta是不同快照。原报告保留在[awdl-preview.md](awdl-preview.md)。

## 反馈

请给出版本/tag、kext UUID、macOS Build、PCI/USB ID、对端OS、是否同账号、方向、文件字节数、耗时、是否实际收到、UI是否一致、停止到Wi-Fi恢复耗时。未测试的型号请明确标“新硬件验证”。提交日志前移除账号、MAC/IP、SSID、证书和个人文件内容；不要上传整套EFI或原始完整抓包。此实验问题请在本fork交流，避免当成上游已支持功能报告。
