// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <stdint.h>
namespace ItlAwdl {
// Explicit bounded receive validation; not an automatic boot or perpetual radio grant.
constexpr uint64_t kNativeReceiveValidationNs=300000000000ULL;
constexpr uint64_t nativeReceiveLimit(bool validation){return validation?kNativeReceiveValidationNs:20000000000ULL;}
}
