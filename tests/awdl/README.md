# AWDL RX observer, experiment 1

This is a bounded wire-envelope parser and opt-in IWM RX observer, not an AWDL implementation. It does not create awdl0, transmit frames, elect a master, schedule channels, or implement AirDrop. It does not change receive filters or enable monitor mode.

The parser was newly written from documented wire facts. Layout reference: https://github.com/seemoo-lab/owl/blob/master/src/frame.h and src/rx.c; exact reference checkout SHA is in the development manifest. No OWL source is linked into this driver. OWL is GPL-3.0-or-later; this implementation is GPL-2.0-or-later. Vendor action category 127, Apple OUI 00:17:f2, AWDL type 8, version 0x10, PSF subtype 0 and MIF subtype 3. Fixed body header 16 bytes, followed by TLVs with 8-bit type and little-endian 16-bit length.

`Valid` means the recognized envelope and TLV bounds are valid; it does not validate the semantics/length of each known TLV. Flags mean TLV presence only. Null/short/unaligned input, fragment/protection/DS flags, HT control, unknown vendor/version/subtype, truncated TLVs, zero-length TLVs, oversized lengths are covered. One million deterministic malformed TLV input cases run with ASan and UBSan. This is not a claim of one million independent protocol scenarios or formal proof. libFuzzer harness is included, but this local Xcode lacks its runtime library; no libFuzzer run is claimed.

The probe hooks IWM iwm_rx_frame before net80211 filtering. It only parses complete contiguous mbufs up to 4096 bytes with FCS removed, consumes no packets, allocates no receive buffers, and retains no frame pointers. RX observer state is owned by the IWM RX workloop, initialized before interrupts. It does not log MAC addresses, names, payloads, or keys.

Boot arg `-itlwm_awdl_probe` opts in. Without it, the observer returns immediately. It logs an enable message, one RX-path-active message and at most eight structurally valid AWDL summaries per attach. No persistent service or polling is installed. Absence of AWDL logs is inconclusive: channel mismatch, existing firmware filters, unsupported versions or frame shape can hide packets. In particular, the current station filter does not unconditionally enable IWM_MAC_FILTER_IN_CONTROL_AND_MGMT; this is a follow-up investigation, not enabled by this patch.

No real hardware validation has occurred for this build. Use an isolated boot medium and verify normal Wi-Fi and sleep before interpreting results.


## Probe 2

Counters continue after the eight log samples. AWDLRxProbe is a custom OSObject property serialized as an OSDictionary only on query; each counter is accessed via relaxed lock-free 64-bit atomics. The object owns no HAL/controller pointer and is separately retained by the property and HAL, preventing a dangling callback after detach. HAL allocation failure disables observation. Serialization failure releases temporary objects. No packet-path allocations or extra timers are introduced. RX counters describe driver entry observations, not necessarily unique radio frames. This version is compiled but awaiting boot validation.


## Probe 3

Adds opt-in -itlwm_awdl_mgmt (also requires -itlwm_awdl_probe) to append the existing firmware IWM_MAC_FILTER_IN_CONTROL_AND_MGMT bit to station MAC context ADD/MODIFY commands. It does not append CRC/FCS, request data promiscuity, enter monitor mode, change channels or transmit AWDL. Host command result counters do not establish firmware RF behavior. Additional counters bucket valid envelopes by net80211 RUN/other state and receive channel metadata 6/44/149/other. These buckets do not identify peers, deduplicate retransmissions or prove scanning state. Builds and source diff checks passed; runtime validation pending.
