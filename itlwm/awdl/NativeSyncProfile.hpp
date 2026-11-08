// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <stdint.h>
namespace ItlAwdl {
// Fixed single-channel Master profile used by the real host sender/scheduler.
struct NativeSyncProfile {
 static constexpr uint32_t awLength=16,awPeriod=16,awExtension=16,advertisementPeriod=110;
 static constexpr uint32_t presence=4;
 static constexpr bool supportsAdvertisementPeriod(uint32_t tu){return tu==advertisementPeriod||tu==440;}
 static constexpr uint32_t periodUs=awPeriod*presence*1024;
};
}
