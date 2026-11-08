// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <stdint.h>
namespace ItlIbss {
enum class BatchMode : uint32_t { Baseline=1, Bootstrap=2, Receive=3, Window=4, TxTransport=5, ControlAdvertisement=6 };
enum class BatchPhase : uint32_t { Idle=0, Waiting=1, Running=2, Recovering=3, Complete=4, Failed=5, Aborted=6 };
constexpr uint32_t BatchVersion=3, BatchMaxRuns=8;
inline bool batchModeValid(uint64_t mode) { return mode>=1 && mode<=6; }
inline bool batchTxMode(BatchMode mode){return mode==BatchMode::TxTransport || mode==BatchMode::ControlAdvertisement;}
inline bool batchTerminal(BatchPhase p) { return p==BatchPhase::Complete || p==BatchPhase::Failed || p==BatchPhase::Aborted; }
// Re-arm requires a clean, finished observation, not just a released radio.
inline bool batchCanStart(uint32_t runs, BatchPhase phase, bool worker, bool owns,
                          bool operation, bool ordinary, bool healthy, bool recovered,
                          bool faults, bool archived, bool queuesEmpty) {
    return runs<BatchMaxRuns && (phase==BatchPhase::Idle || phase==BatchPhase::Complete) &&
        !worker && !owns && !operation && !ordinary && healthy && recovered && !faults && archived && queuesEmpty;
}
struct BatchControl {
    bool enabled=false, abortRequested=false, bootstrapPassed=false, receivePassed=false, txUsed=false;
    uint32_t run=0, archived=0, faultSerial=0;
    BatchMode mode=BatchMode::Baseline;
    BatchPhase phase=BatchPhase::Idle;
    uint64_t requestedNs=0, deadlineNs=0;
    bool active() const { return phase==BatchPhase::Waiting || phase==BatchPhase::Running || phase==BatchPhase::Recovering; }
};
}
