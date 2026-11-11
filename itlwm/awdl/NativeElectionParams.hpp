// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <stddef.h>
#include <stdint.h>
#include <string.h>

namespace ItlAwdl {
constexpr size_t kNativeElectionParamsSize = 56;
constexpr size_t kNativeElectionMetricSize = 12;
struct NativeElectionParams {
    uint8_t master[6] = {};
    uint32_t masterMetric = 0;
    uint32_t selfMetric = 0;
};
struct NativeElectionMetric {
    uint32_t metric = 0;
    bool overrideMetric = false;
};
inline uint32_t nativeElectionRead32(const uint8_t *p) {
    return uint32_t(p[0]) | uint32_t(p[1]) << 8 |
        uint32_t(p[2]) << 16 | uint32_t(p[3]) << 24;
}
// The target PM resolves override policy first and forwards a nonzero metric.
inline bool parseNativeElectionMetric(const void *raw, size_t length,
                                      NativeElectionMetric &out) {
    out = {};
    if (!raw || length != kNativeElectionMetricSize) return false;
    const auto *p = static_cast<const uint8_t *>(raw);
    out.metric = nativeElectionRead32(p + 4);
    out.overrideMetric = p[8] != 0;
    return out.metric != 0;
}
// 24H32 programChipWithNewMaster builds this request; offsets are from the
// native caller and match its logged master/root/self metric values.
inline bool parseNativeElectionParams(const void *raw, size_t length,
                                      NativeElectionParams &out) {
    out = {};
    if (!raw || length != kNativeElectionParamsSize) return false;
    const auto *p = static_cast<const uint8_t *>(raw);
    memcpy(out.master, p + 0x16, 6);
    out.masterMetric = nativeElectionRead32(p + 0x1c);
    out.selfMetric = nativeElectionRead32(p + 0x20);
    if (!out.masterMetric || !out.selfMetric || (out.master[0] & 1)) return false;
    uint8_t any = 0;
    for (unsigned i = 0; i < 6; ++i) any |= out.master[i];
    return any != 0;
}
}
