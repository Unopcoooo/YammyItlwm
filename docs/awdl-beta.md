# Beta 1 安装、开关与回滚

## 范围

仅验证macOS Sequoia 15.8.1（24H32）、x86_64、AC9560（8086:a370）和Intel Bluetooth USB 8087:0AAA。其他环境未经验证。蓝牙补丁在其他Build不会启用；不要把此包称为所有Intel网卡的通用AirDrop修复。

当前测试环境通过OpenCore加载并允许实验私有接口/带私有entitlement的辅助程序。标准kmutil加载和默认SIP/AMFI策略未验证。此安装器不修改安全策略或EFI，不提供复制整机EFI的方案。已有可回退EFI及本机所需安全/驱动配置是实验前提。

## 包内构成

- `Kexts/AirportItlwm.kext`：r26，AWDL接口/peer、BSD RX/TX、时隙调度、DMA生命周期和安全恢复。
- `Kexts/BlueToolFixup.kext`：E2CB，24H32的Intel标准广播集、事件掩码、随机地址、扩展扫描/连接适配；保留原有A2DP补丁，但A2DP启动参数不是AirDrop验收结论。
- `helper/`：r26c的开关观察、300秒租约策略、状态/控制工具和launchd描述。**这部分在用户态，不在kext内。**
- `manifest.json`及`SHA256SUMS.txt`：确切版本与校验值。release源码快照另提供。

上游依赖不重复打包：[Lilu](https://github.com/acidanthera/Lilu/releases)、[IntelBluetoothFirmware](https://github.com/OpenIntelWireless/IntelBluetoothFirmware/releases)及其IntelBTPatcher。测试机分别为Lilu1.7.2、IntelBluetoothFirmware/IntelBTPatcher2.4.0；USB端口/蓝牙映射应先在本机正确工作。不要同时加载两个BlueToolFixup或两个AirportItlwm，也不要将itlwm+HeliPort与本AirportItlwm重复驱动同一硬件。

## 手动配置自己的EFI

先保存当前能启动和联网的EFI与原kext，准备本机可用的恢复引导。**不要替换成开发者的完整EFI或复制SMBIOS/序列号。**

1. 用包中两份kext替换自己EFI/OC/Kexts里的对应项，核对Kernel/Add启用和依赖顺序：Lilu、Intel蓝牙固件/patcher、BlueToolFixup、AirportItlwm按原依赖加载。测试机AirportItlwm Darwin范围为24.2.0–24.99.99；这里实际只验证24H32。
2. 在自己config.plist的boot-args追加本实验使用的AWDL参数（保留原有本机参数）：

   ```text
   -itlwm_awdl_probe -itlwm_awdl_nobootcreate -itlwm_awdl_engine itlwm_awdl_engine_channel=6 -itlwm_awdl_template_tx -itlwm_awdl_mgmt -itlwm_awdl_rxtrace -btlfxadv24h32
   ```

   这是本机实测参数组合，不代表每个诊断开关都是最终产品必需。不要加入历史`itlwm_ibss_probe=1`、batch或one-shot启动参数；不要复制与本实验无关的显卡、调试、AMFI等本机参数。
3. 若OpenCore策略会保留旧NVRAM boot-args，要按自己的引导配置确认新值实际生效；不要为此默认Reset NVRAM。测试机没有额外启用Kernel/Patch，OpenCore可执行文件也未因r26改写。
4. 手动重启，用`kmutil showloaded --list-only`核对AirportItlwm UUID `D6ED6749-6BCD-332E-871C-667166716DEE` 和BlueToolFixup UUID `E2CB71B5-BA78-39EB-99AD-24F585C1C38A`。先确认普通Wi-Fi和蓝牙仍可用。

## 安装辅助服务

需要Python3；系统测试使用`/usr/bin/python3`，自行安装的python3也用于执行以下安装检查。安装前关闭接收并等旧租约退役。解压文件夹后在终端运行（把路径换成实际解压目录）：

```sh
python3 /path/to/ac9560-awdl-beta-1-r26/install-helper.py /path/to/ac9560-awdl-beta-1-r26
sudo python3 /path/to/ac9560-awdl-beta-1-r26/install-helper.py /path/to/ac9560-awdl-beta-1-r26 --apply
```

第一行只检查，不写系统。第二行只安装匹配的helper/launchd文件并备份旧辅助服务，不改EFI、不重启、不自动开启AWDL。脚本要求当前已加载指定r26且没有活动租约。包内二进制为本机ad-hoc签名实验产物，未做Apple公证；默认安全策略可执行性未保证。源文件和构建脚本在`tools/awdl-receive-switch/`。本次打包已验证安装器的只读检查、篡改拒绝和辅助程序编译；新安装器的实际写入/回滚分支尚未实机执行，现有成功样本使用此前已安装的同版辅助服务。

## 开启、停止与状态

1. Finder「隔空投送」先设为“没有人/关闭接收”，再切“所有人”。由用户手动切换，服务启动时如果已经是所有人不会自动接管无线。
2. 等待设备出现，再从系统分享菜单发送，或让对端发送。普通Wi-Fi这时中断，不要以“无法联网汇报”为故障证据。
3. 单次无线租约最多300秒，从租约准入开始计算，包含启动时间。配套DNS-SD工具自身浏览上限仍为120秒，原生Finder/分享页另有浏览者；五分钟不是保证持续可见或一定传完的承诺。切回Off也会请求提前停止；退役和普通Wi-Fi重新关联可能需要几十秒甚至更久。
4. 超时后界面可能仍显示“所有人”，但无线租约已关闭；要开始新一轮，先等恢复完成，再手动Off→Everyone。不要连续快速点击或同时手动发其他开发命令。

```sh
/Library/PrivilegedHelperTools/org.unopco.awdl-receive-switch/native-search-probe --status
launchctl print system/org.unopco.awdl-receive-switch
```

完成退役的典型证据为WorkerPending=0、BackendEnabled=0及恢复状态；不要只凭界面、能上网或某一个计数覆盖DMA/队列保护。失败时先Off并保存状态，不强制继续新实验。

## 停用 / 回滚

先切Off并等退役。`sudo launchctl bootout system/org.unopco.awdl-receive-switch`停止服务。将自己的旧kext和boot-args从EFI备份恢复，再手动重启；仅替换EFI中kext不会改变当前内存已加载驱动。需要撤销helper时可在服务停止后移走 `/Library/LaunchDaemons/org.unopco.awdl-receive-switch.plist` 和 `/Library/PrivilegedHelperTools/org.unopco.awdl-receive-switch/`，保留备份。安装器辅助服务备份位于`/Library/Application Support/Unopco-AWDL/Backups/`。

本机历史失败不应靠删除身份数据库、伪造peer、无条件返回成功、放宽DMA所有权或自动重启来掩盖。出现无法恢复的内核/固件状态时停止实验，用已准备的旧EFI引导。
