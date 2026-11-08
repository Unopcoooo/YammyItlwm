// SPDX-License-Identifier: GPL-2.0-or-later
// Probe51: shared state of the bounded AWDL dwell engine.
//
// ItlIwm drives the dwell loop (it owns the firmware command path). The
// controller layer reads this for diagnostics. Only the explicit
// -itlwm_awdl_control_probe experiment injects an interface link-up after a
// completed dwell; that injection is a control-flow test input, not evidence
// of synchronised AWDL membership, queue readiness or an operational data path.
#pragma once
#include <stdint.h>
namespace ItlAwdl {
// Native start event type is 41 on this kernel: the public header's 42 label
// covers the following extension event. End remains 43.
constexpr uint32_t kAvailabilityWindowStartEvent = 41;
constexpr uint32_t kAvailabilityWindowEndEvent = 43;
typedef void (*AvailabilityWindowEventHandler)(void *context, bool start,
                                                uint32_t firmwareStartGp2,
                                                uint16_t awNumber);
inline void encodeAvailabilityWindowStart(uint8_t event[36], const uint8_t address[6],
                                           uint32_t firmwareStartGp2, uint16_t awNumber) {
    for (unsigned i = 0; i < 36; ++i) event[i] = 0;
    event[0] = uint8_t(awNumber); event[1] = uint8_t(awNumber >> 8);
    for (unsigned i = 0; i < 6; ++i) event[8 + i] = event[14 + i] = address[i];
    event[20] = uint8_t(firmwareStartGp2 >> 24);
    event[21] = uint8_t(firmwareStartGp2 >> 16);
    event[22] = uint8_t(firmwareStartGp2 >> 8);
    event[23] = uint8_t(firmwareStartGp2);
}
inline void encodeAvailabilityWindowEnd(uint8_t event[40], uint32_t firmwareGp2,
                                         uint16_t awNumber) {
    for (unsigned i = 0; i < 40; ++i) event[i] = 0;
    event[0] = uint8_t(awNumber); event[1] = uint8_t(awNumber >> 8);
    event[4] = uint8_t(firmwareGp2 >> 24);
    event[5] = uint8_t(firmwareGp2 >> 16);
    event[6] = uint8_t(firmwareGp2 >> 8);
    event[7] = uint8_t(firmwareGp2);
}
struct EngineState {
    int32_t modeEnabled;      // boot argument requested the bounded engine
    int32_t running;          // engine is inside its bounded window
    int32_t cyclesSubmitted;  // ROC commands submitted
    int32_t cyclesCompleted;  // cycles that reached an END notification
    int32_t cycleFailures;    // cycles that ended in failure/quarantine
    int32_t startsTotal;      // accumulated firmware START notifications
    int32_t endsTotal;        // accumulated firmware END notifications
    int32_t failuresTotal;    // accumulated failure notifications
    int32_t channel;          // dwell channel used by the engine
    int32_t linkUpReported;   // last link value pushed to the family (0/1)
    int64_t deadlineNs;       // end of the bounded window
    AvailabilityWindowEventHandler availabilityWindowEvent;
    void *availabilityWindowContext;
};
extern EngineState gEngine;
}
