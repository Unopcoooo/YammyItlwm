// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <stdint.h>
namespace ItlIbss {
constexpr uint64_t StaSecondNs = 1000000000ULL;
constexpr uint64_t OrdinaryJoinNs = 30 * StaSecondNs;
constexpr uint64_t NativeSearchJoinNs = 60 * StaSecondNs;
constexpr uint64_t OrdinaryObservationNs = 60 * StaSecondNs;
constexpr uint64_t NativeSearchObservationNs = 75 * StaSecondNs;
}
