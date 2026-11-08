# AWDL version status and reproducibility

Updated: November 8, 2026. This page separates the public source preview from
later local development. It is a maintainer's summary of recorded trials, not
an independent replication report. Raw packet captures and machine-specific
configuration are not published here.

## 版本边界 / Version boundaries

| Version | Availability | Evidence and limitations |
| --- | --- | --- |
| `ac9560-awdl-preview-1` / DDA9 | Public source milestone | Native Mac → AC9560 received 15,340,445 bytes; ZIP integrity passed. Wi-Fi recovered after the trial deadline. [Original test](awdl-preview.md). |
| 19E2 | Subsequent local development; not included in preview-1 | Same boot: one completed file receive and one discovery-only round. Both stopped and recovered. This does not establish long-term reliability. |
| D3BE | Local candidate; not included in preview-1 | RX copy queue 4 → 16, offline tests passed. Hardware throughput/recovery validation pending. No speed improvement claimed. |

DDA9, 19E2 and D3BE are short labels derived from the tested binary's Mach-O
UUID, not Git commit IDs. A build from a source tag need not reproduce the
same binary UUID or hash without the exact build environment.

## What has and has not been demonstrated

| Capability | Status |
| --- | --- |
| Native Mac discovers the AC9560 Hackintosh | Observed during bounded AWDL sessions |
| Native Mac → Hackintosh file receive | Completed through macOS AirDrop, including receive prompt and archive validation |
| About two minutes of sustained visibility | Observed on the native Mac; does not establish indefinite availability |
| Stop and return to infrastructure Wi-Fi | Passed twice in one boot on local 19E2; public DDA9 had delayed recovery |
| Hackintosh → native Mac send | Not validated by these trials |
| iPhone → Hackintosh discovery/transfer | Not achieved in the recorded iPhone share-sheet observations; no successful transfer claimed |
| Seeing same-account devices in the Hackintosh's list | Not proof of an AWDL connection; this was observed before the AWDL work |
| Wi-Fi and AWDL coexistence | Not supported by the tested exclusive-radio session |
| Other Intel adapters, other macOS builds, Tahoe AWDL | Not validated by this result |
| Sleep/wake, long sessions, repeated large transfers | Not established |

## Local 19E2 follow-up

Same receiver configuration as the original DDA9 test: AC9560 (8086:a370),
macOS 15.8.1 (24H32), x86_64, Intel 9000-series API 46 firmware.

- Binary UUID: `19E2A4CD-2B28-3A9F-8766-36D716358D58`.
- Executable SHA-256: `ad302adf1594859f9836fb8fae9351be18aff70a5826b33621dca4b236f7dd1a`.
- File receive: 15,340,445 bytes, 672 ZIP entries, archive integrity passed.
- Received file SHA-256: `46720c0d9287bd16872bd362fc5f5a97b657ec5096236529b8dd903a11220114`.
  This matches an earlier verified receive; the sender's source-file hash was
  not independently read, so this is not a claimed sender-to-receiver hash match.
- Receiver UPLOAD start to completion: 83.421 seconds, about 0.184 MB/s
  (decimal file bytes divided by that interval, not a PHY rate).
- Stop to logged Wi-Fi join: 22.921 seconds in the receive round; 14.473 seconds
  in the subsequent discovery-only round in the same boot.
- Both rounds ended with control retired and zero outstanding DMA reported.

These two rounds support repeatable recovery in that boot, not permanent
resolution of all recovery failures. They do not establish a throughput
improvement over DDA9; the runs were not a controlled speed comparison.

## Throughput investigation

The local 19E2 trace recorded 241 RX queue-full events. After normalizing the
TCP sequence origins, the packet-count difference between sender and receiver
captures was also 241. This supports burst loss at the small receive queue;
it does not identify every individual retransmission's cause.

Long pauses were strongly aligned to the 1.048576-second AWDL supercycle, and
similar pauses appeared on DDA9. Peer scheduling/usable channel overlap is a
working hypothesis, not a proven root cause. PHY rate, host scheduling and
actual radio completion must be distinguished.

D3BE changes only the RX copy queue capacity from 4 to 16. Sixteen is a bounded
experimental choice aligned with the existing drain budget, not an Apple
requirement, hardware limit or established optimum. TX capacity, timing guards,
DMA ownership and recovery rules were not relaxed for this experiment.

## Why no ready-to-install AWDL kext package yet?

目前仅公开源码预览。已完成的实测依赖本地会话控制工具和经过核对的加载环境，
公开树尚未包含完整复现流程。仅安装 kext 不会自动获得持续 AirDrop 接收。
待源码、控制工具、停止恢复步骤及对应实测记录能够一起发布后，再提供实验性二进制包。

The successful trial depended on local control tooling and a specifically
checked loader environment. Stock `kmutil` validation is not established as a
supported loading path. Do not treat a successful compile, a generic CI artifact,
or the inherited load/unload scripts as a validated installation procedure.

A reproducible experimental binary release needs:

1. An immutable source tag identifying the actual candidate, with its SDK and
   build instructions; no substitution of an untested local candidate.
2. Start, stop and status tooling with bounded sessions and documented recovery,
   including the fact that ordinary Wi-Fi is interrupted.
3. A matching hardware result, executable UUID/SHA-256 and archive checksum.
4. Tested installation/rollback instructions for the supported loader and OS,
   with no machine-specific EFI, credentials or firmware keys included.

## Reporting results

For this experimental fork, report the source commit/tag, executable UUID,
macOS version/build, adapter PCI ID, firmware API, transfer direction and peer
OS. Record these outcomes separately:

- Device appeared/disappeared in the system list.
- Service discovery and connection established.
- Receive prompt appeared and was accepted.
- File completed, exact byte count, elapsed time and archive/hash validation.
- Session stopped, infrastructure Wi-Fi rejoined, and a later session could start.

Include failures as well as successes and the total number of attempts. Redact
Apple Account details, SSIDs, MAC/IP addresses, personal file names and payloads
before sharing logs. Do not report this fork's experimental AWDL failures to
upstream as if they were supported upstream features. If Issues are unavailable
on this fork, documentation and code corrections can be submitted as a pull
request; no public issue tracker is assumed here.
