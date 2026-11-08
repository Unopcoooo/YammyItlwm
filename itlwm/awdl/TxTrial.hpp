// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "IbssExperiment.hpp"
#include "BatchPolicy.hpp"
#include "ControlTxEvidence.hpp"
namespace ItlIbss {
// Host/firmware transport completion only: never OTA/peer acceptance or full M2.
inline bool txTrialSucceeded(const ExperimentObservation &d,uint32_t run,BatchMode mode=BatchMode::TxTransport){
 using D=ExperimentObservation;const auto status=d.get(D::TxStatus)&255;
 return batchTxMode(mode) && d.get(D::TxProbeVersion)==(mode==BatchMode::ControlAdvertisement?2u:1u) && d.get(D::TxAttempted)==1 && d.get(D::TxAccepted)==1 &&
  d.get(D::TxSubmitted)==1 && d.get(D::TxCompleted)==1 && d.get(D::TxReclaimed)==1 &&
  d.get(D::TxRun)==run && d.get(D::TxGeneration)==d.get(D::CaptureGeneration) && d.get(D::TxGeneration) &&
  d.get(D::TxPayloadBytes)==(mode==BatchMode::ControlAdvertisement?227u:40u) && (mode!=BatchMode::ControlAdvertisement || controlTxEvidenceMatches(d,run,d.get(D::TxGeneration))) && (status==1 || status==2) && !d.get(D::TxStopReclaimed) &&
  !d.get(D::TxFlushRequested) && !d.get(D::TxFlushFailed) && !d.get(D::TxMismatch) && !d.get(D::TxCancelled) && !d.get(D::TxDeadlineExpired);
}
}
