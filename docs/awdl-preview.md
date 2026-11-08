# November 8, 2026 AirDrop receive test

| Field | Observed value |
| --- | --- |
| Receiver | x86_64 Hackintosh, Intel Core i5-9300H |
| OS | macOS Sequoia 15.8.1 (24H32) |
| Wireless adapter | Intel Wireless-AC 9560 (8086:a370), iwm |
| Firmware | Intel 9000-series, API 46 |
| Direction | MacBook → Hackintosh |
| Received file size | 15,340,445 bytes |
| Time from first upload event to all bytes received | About 75 seconds |
| ZIP integrity | All entries readable; sender-side hash comparison was not performed |
| Wi-Fi recovery | Missed the trial deadline; infrastructure Wi-Fi returned later |
| Tested kext version | AirportItlwm 2.4.0 |
| Tested kext Mach-O UUID | DDA9D34A-5221-3869-9E4C-1F7CFDC06639 |
| Tested kext SHA-256 | `506f50f30739f0f7587bcd9332f53c7d2e8b442ee289b441e198df0e2bce71af` |
| Upstream driver baseline | `d695c971420cc8af0daa286dcd3297e8236c0a63` |

The receiver recorded all upload bytes at 16:03:49 local time and the
unpack response at 16:03:50. The sender recorded the response at 16:03:52.
The record summarizes a single completed trial.
