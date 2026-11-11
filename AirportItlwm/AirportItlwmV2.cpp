//
//  AirportItlwmV2.cpp
//  AirportItlwm-Sonoma
//
//  Created by qcwap on 2023/6/27.
//  Copyright © 2023 钟先耀. All rights reserved.
//

#include "AirportItlwmV2.hpp"
// Native BSD publication with explicitly bounded data sessions.
#include "ItlAwdlInterface.hpp"
#ifdef AIRPORT_WCL
#include "../itlwm/hal_iwm/ItlIwm.hpp"
#endif
#include "../itlwm/awdl/EngineState.hpp"   // Probe51: shared bounded-engine state
#include "../itlwm/awdl/PeerTable.hpp"    // Probe53: real peer-cache capacity
#include "../itlwm/awdl/SyncTemplate.hpp"
#include "../itlwm/awdl/TemplateRuntime.hpp"
#include <IOKit/IOUserClient.h>
#include "ItlAwdlResources.hpp"
#include "../itlwm/awdl/DataDriver.hpp"
#include "../itlwm/awdl/NativeControlAdapter.hpp"
#include "../itlwm/awdl/NativePeerCacheControl.hpp"
#include "../itlwm/awdl/NativeElectionParams.hpp"
#include "../itlwm/awdl/NativeUmi.hpp"
#include <kern/thread_call.h>
#include "../itlwm/awdl/NativeBsdBinding.hpp"
#include "../itlwm/awdl/NativePeerRegistration.hpp"
#include <net/if_dl.h>
#include <net/route.h>

IO80211WorkQueue *_fWorkloop;
IOCommandGate *_fCommandGate;
ItlAwdl::DataPort<OSObject> ItlAwdl::gDataPort = {};
#include <sys/_netstat.h>
#include <sys/errno.h>
#include <sys/sockio.h>
#include <net/if_media.h>
#include <net/kpi_interfacefilter.h>
#include <netinet/in.h>
#include <crypto/sha1.h>
#include <net80211/ieee80211_priv.h>
#include <net80211/ieee80211_var.h>

#include "AirportItlwmSkywalkInterface.hpp"
#include "IOPCIEDeviceWrapper.hpp"

// Probe36: the native legacy bridge (IOSkywalkLegacyEthernet) does create a
// real IOSkywalkLegacyEthernetInterface whose BSD Name is awdl0, and
// IONetworkStack matches it, but that class overrides attachToDataLinkLayer
// without calling the base implementation - and the base implementation is the
// only caller of ifnet_attach() in this kernel collection. Nudge it once so the
// ifnet is actually attached, then read back the kernel's own view.
extern "C" IOReturn ItlNativeAttachToDataLinkLayer(void *self, unsigned int options, void *arg)
    __asm__("__ZN18IONetworkInterface21attachToDataLinkLayerEjPv");
extern "C" int ItlIfnetFindByName(const char *name, void **ifpp)
    __asm__("_ifnet_find_by_name");
extern "C" int ItlIfnetRelease(void *interface) __asm__("_ifnet_release");
extern "C" void *ItlNativeGetIfnet(const void *interface)
    __asm__("__ZNK18IONetworkInterface8getIfnetEv");
extern "C" void ItlNativeDetachFromDataLinkLayer(void *self, unsigned int options, void *arg)
    __asm__("__ZN18IONetworkInterface23detachFromDataLinkLayerEjPv");
extern "C" bool ItlNativeSetInterfaceSubType(void *self, uint32_t subtype)
    __asm__("__ZN18IONetworkInterface19setInterfaceSubTypeEj");
extern "C" bool ItlNativeSetInterfaceFlags(void *self, uint16_t set, uint16_t clear)
    __asm__("__ZN18IONetworkInterface8setFlagsEtt");
// The legacy IONetworkInterface::setExtendedFlags is a no-op on24H32.
extern "C" uint32_t ItlIfnetEflags(ifnet_t) __asm__("_ifnet_eflags");
extern "C" errno_t ItlIfnetSetEflags(ifnet_t,uint32_t,uint32_t) __asm__("_ifnet_set_eflags");
extern "C" IO80211VirtualInterface *ItlNativePmVirtualInterface(void *)
    __asm__("__ZN18IO80211PeerManager19getVirtualInterfaceEv");
static_assert(APPLE80211_VIF_AWDL == 6, "Sequoia target AWDL role ABI");

static int gItlAwdlNameClears = 0;
static bool gItlAwdlControlProbe = false;

// Probe42 observes protocol entry without writing unverified payloads or
// acknowledging actions that the adapter cannot perform. Atomic counters are
// read by the watchdog; no concurrent string-ring access is needed.
// Total, gated, session, manager, matching buffer, parsed.
uint64_t ItlAwdl::gNativeMifRevision=0;
static volatile SInt32 gServiceMifStages[6]={};
static volatile SInt32 gItlAwdlProtocolCounts[ItlAwdlInterface::ProtocolMethodCount] = {};

void ItlAwdlInterface::recordProtocol(unsigned method)
{
    OSIncrementAtomic(&probeProtocolCalls);
    if (method < ProtocolMethodCount)
        OSIncrementAtomic(&gItlAwdlProtocolCounts[method]);
}

IOReturn ItlAwdlInterface::unsupportedProtocol(unsigned method)
{
    if(method==41||method==56)ItlAwdl::invalidateOob();
    recordProtocol(method);
    return kIOReturnUnsupported;
}

// The native request wrapper requires exactly 12 bytes; both the native peer
// manager and the Broadcom getter use enabled at offset 8. The latter writes
// only enabled, leaving the caller's version/opaque word unchanged. Follow
// that contract: no invented meaning or output for the opaque word.
static_assert(sizeof(apple80211_awdl_sync_enabled) == 12, "Native sync query size");
static_assert(__builtin_offsetof(apple80211_awdl_sync_enabled, enabled) == 8, "Native sync enabled offset");
static IOReturn awdlNativeSyncControl(ItlAwdlInterface *interface,ItlAwdlDataCall::Kind kind,bool &enabled)
{
    enabled=false;
    auto controller=OSDynamicCast(AirportItlwm,interface->getProvider());
    if(!controller)return kIOReturnNotReady;
    ItlAwdlDataCall call={kind};
    call.owner=interface;
    const auto result=controller->awdlDataDispatch(call);
    enabled=result==kIOReturnSuccess&&call.result!=0;return result;
}

IOReturn ItlAwdlInterface::getAWDL_SYNC_ENABLED(apple80211_awdl_sync_enabled *data)
{
    recordProtocol(5);
    if (!data)
        return kIOReturnBadArgument;
    bool nativeEnabled=false;
    const auto nativeResult=awdlNativeSyncControl(this,ItlAwdlDataCall::NativeSyncQuery,nativeEnabled);
    const bool auxEnabled=gItlAwdlControlProbe&&ItlAwdl::gEngine.running&&ItlAwdl::gEngine.endsTotal>0;
    data->enabled=nativeEnabled||auxEnabled;
    setProperty("AWDLTraceSyncNativeQueryResult",uint32_t(nativeResult),32);
    setProperty("AWDLTraceSyncQueryResult", static_cast<uint32_t>(kIOReturnSuccess), 32);
    setProperty("AWDLTraceSyncEnabled", data->enabled != 0);
    return kIOReturnSuccess;
}

IOReturn ItlAwdlInterface::setAWDL_SYNC_ENABLED(apple80211_awdl_sync_enabled *data)
{
    recordProtocol(30);
    if (!data) return kIOReturnBadArgument;
    setProperty("AWDLTraceSyncSetRequested",data->enabled!=0);
    IOReturn result=kIOReturnSuccess;
    if(data->enabled){
        const bool auxEnabled=gItlAwdlControlProbe&&ItlAwdl::gEngine.running&&ItlAwdl::gEngine.endsTotal>0;
        if(!auxEnabled){
            bool unused=false;result=awdlNativeSyncControl(this,ItlAwdlDataCall::NativeSyncEnable,unused);
        }
    }else if(ItlAwdl::gEngine.running)result=kIOReturnUnsupported;
    else{
        bool unused=false;result=awdlNativeSyncControl(this,ItlAwdlDataCall::NativeSyncDisable,unused);
    }
    setProperty("AWDLTraceSyncSetResult",uint32_t(result),32);
    return result;
}

static_assert(sizeof(apple80211_awdl_master_channel) == 8, "Native master-channel payload");
static_assert(__builtin_offsetof(apple80211_awdl_master_channel, master_channel) == 4, "Native master-channel offset");
IOReturn ItlAwdlInterface::setAWDL_MASTER_CHANNEL(apple80211_awdl_master_channel *data)
{
    recordProtocol(36);
    if (!data) return kIOReturnBadArgument;
    setProperty("AWDLTraceMasterChannelRequested", data->master_channel, 32);
    auto controller = OSDynamicCast(AirportItlwm, getProvider());
    auto hal = controller ? controller->fHalService : nullptr;
    if (!hal || probeStopDone) return kIOReturnNotReady;
    hal->retain();
    const IOReturn status = hal->setAwdlMasterChannel(data->master_channel);
    hal->release();
    setProperty("AWDLTraceMasterChannelResult", static_cast<uint32_t>(status), 32);
    setProperty("AWDLTraceMasterChannelPolicyInstalled", status == kIOReturnSuccess);
    return status;
}

// Keep only the verified-prefix configuration mirrors. Requests with a
// mismatched legacy payload must not masquerade as applied backend state.
static uint8_t gItlAwdlBssid[APPLE80211_ADDR_LEN] = {};
static uint8_t gItlAwdlBssidPeer[APPLE80211_ADDR_LEN] = {};
static bool gItlAwdlBssidValid = false;
static uint32_t gItlAwdlSyncFrameTemplateLength = 0;
static uint8_t gItlAwdlSyncFrameTemplate[512] = {};
static bool gItlAwdlSyncFrameTemplateValid = false;
static char gItlAwdlTrafficName[153] = {};
static uint32_t gItlAwdlTrafficActive = 0;
static uint32_t gItlAwdlTrafficRegistrations = 0;
// Probe53: election id handed over by the system, and this driver's real
// peer-table capacity (ItlAwdl::kMaxPeers) for the cache-size query.
static uint32_t gItlAwdlElectionId = 0;
static bool gItlAwdlElectionIdValid = false;
static apple80211_awdl_sync_channel_sequence gItlAwdlChannelSequence = {};
static bool gItlAwdlChannelSequenceValid = false;
// Role configuration is not backend readiness; no sync-state shadow.
static apple80211_awdl_presence_mode gItlAwdlPresenceMode = {APPLE80211_VERSION, 4};
static uint32_t gItlAwdlMasterChannel = 6;

IOReturn ItlAwdlInterface::getAWDL_SYNC_PARAMS(apple80211_awdl_sync_params *data)
{
    recordProtocol(4);
    if(!data)return kIOReturnBadArgument;
    auto root=OSDynamicCast(AirportItlwm,getProvider());auto hal=root?OSDynamicCast(ItlIwm,root->fHalService):nullptr;
    if(!hal||probeStopDone)return kIOReturnUnsupported;
    hal->retain();const auto result=hal->awdlNativeSyncParams(data,sizeof(*data),this,false);hal->release();return result;
}
IOReturn ItlAwdlInterface::setAWDL_SYNC_PARAMS(apple80211_awdl_sync_params *data)
{
    recordProtocol(29);
    if(!data)return kIOReturnBadArgument;
    setProperty("AWDLTraceSyncParamsRequest",static_cast<void*>(data),static_cast<unsigned>(sizeof(*data)));
    auto root=OSDynamicCast(AirportItlwm,getProvider());auto hal=root?OSDynamicCast(ItlIwm,root->fHalService):nullptr;
    IOReturn result=kIOReturnUnsupported;
    if(hal&&!probeStopDone){hal->retain();result=hal->awdlNativeSyncParams(data,sizeof(*data),this,true);hal->release();}
    setProperty("AWDLTraceSyncParamsResult",uint32_t(result),32);return result;
}

IOReturn ItlAwdlInterface::getAWDL_MASTER_CHANNEL(apple80211_awdl_master_channel *data)
{
    recordProtocol(11);
    if (!data) return kIOReturnBadArgument;
    auto controller=OSDynamicCast(AirportItlwm,getProvider());
    auto hal=controller?OSDynamicCast(ItlIwm,controller->fHalService):nullptr;
    if(hal){
        if(probeStopDone)return kIOReturnNotReady;
        bool reserved=false;uint32_t channel=0;hal->retain();
        const auto result=hal->awdlNativeSearchReadMasterChannel(channel,reserved);hal->release();
        if(result!=kIOReturnSuccess)return result;
        if(reserved){data->version=APPLE80211_VERSION;data->master_channel=channel;return kIOReturnSuccess;}
    }
    data->version = APPLE80211_VERSION;
    data->master_channel = gItlAwdlMasterChannel;
    return kIOReturnSuccess;
}

IOReturn ItlAwdlInterface::getAWDL_SYNC_STATE(apple80211_awdl_sync_state *data)
{
    recordProtocol(12);
    if (!data) return kIOReturnBadArgument;
    auto root=OSDynamicCast(AirportItlwm,getProvider());
    auto hal=root?OSDynamicCast(ItlIwm,root->fHalService):nullptr;
    if(!hal||probeStopDone)return kIOReturnUnsupported;
    hal->retain();const auto result=hal->awdlNativeSyncRole(data,sizeof(*data),this,false);hal->release();
    return result;
}

IOReturn ItlAwdlInterface::getAWDL_PRESENCE_MODE(apple80211_awdl_presence_mode *data)
{
    recordProtocol(14);
    if (!data) return kIOReturnBadArgument;
    *data = gItlAwdlPresenceMode;
    return kIOReturnSuccess;
}

IOReturn ItlAwdlInterface::getAWDL_SYNCHRONIZATION_CHANNEL_SEQUENCE(
        apple80211_awdl_sync_channel_sequence *data)
{
    recordProtocol(9);
    if (!data) return kIOReturnBadArgument;
    *data = gItlAwdlChannelSequenceValid ? gItlAwdlChannelSequence :
        apple80211_awdl_sync_channel_sequence{};
    data->version = APPLE80211_VERSION;
    return kIOReturnSuccess;
}

IOReturn ItlAwdlInterface::setAWDL_ELECTION_ID(apple80211_awdl_election_id *data)
{
    recordProtocol(32);
    if (!data)
        return kIOReturnBadArgument;
    gItlAwdlElectionId = data->election_id;
    gItlAwdlElectionIdValid = true;
    setProperty("AWDLProbeElectionId", (unsigned long long)gItlAwdlElectionId, 32);
    return kIOReturnSuccess;
}

IOReturn ItlAwdlInterface::getAWDL_ELECTION_ID(apple80211_awdl_election_id *data)
{
    recordProtocol(7);
    if (!data)
        return kIOReturnBadArgument;
    // Only answer with an id the system actually supplied.
    if (!gItlAwdlElectionIdValid)
        return kIOReturnUnsupported;
    data->election_id = gItlAwdlElectionId;
    return kIOReturnSuccess;
}

IOReturn ItlAwdlInterface::getPEER_CACHE_MAXIMUM_SIZE(apple80211_peer_cache_maximum_size *data)
{
    recordProtocol(10);
    if (!data)
        return kIOReturnBadArgument;
    // This is the real capacity of the peer table this driver maintains.
    data->max_peers = ItlAwdl::kMaxPeers;
    setProperty("AWDLProbePeerCacheMaximum", (unsigned long long)ItlAwdl::kMaxPeers, 32);
    return kIOReturnSuccess;
}

IOReturn ItlAwdlInterface::setPEER_CACHE_CONTROL(apple80211_peer_cache_control *data)
{
    recordProtocol(40);
    if (!data)
        return kIOReturnBadArgument;
    uint32_t operation = 0;
    uint8_t address[ItlAwdl::NativePeerCacheControl::AddressLength] = {};
    if (!ItlAwdl::NativePeerCacheControl::decode(
            data, ItlAwdl::NativePeerCacheControl::Size, operation, address))
        return kIOReturnUnsupported;
    auto manager = probeCopyPeerManager();
    const bool nativePeerManager = manager &&
        !strcmp(manager->getMetaClass()->getClassName(), "IO80211AWDLPeerManager") &&
        ItlNativePmVirtualInterface(manager) == this;
    if (manager)
        manager->release();
    if (!nativePeerManager)
        return kIOReturnNotReady;
    auto controller = OSDynamicCast(AirportItlwm, getProvider());
    uint8_t completion[ItlAwdl::NativePeerCacheControl::Size] = {};
    if (!controller || !ItlAwdl::NativePeerCacheControl::makeCompletion(
            data, ItlAwdl::NativePeerCacheControl::Size, completion))
        return kIOReturnNotReady;
    // Intel has no matching firmware cache ioctl. The native PeerManager owns
    // this request; report the software cache acceptance through its event path.
    setProperty("AWDLProbePeerCacheOpcode", operation, 32);
    setProperty("AWDLProbePeerCachePeer", address, sizeof(address));
    const IOReturn posted = controller->postMessage(
        this, 0x33, completion, sizeof(completion), true);
    setProperty("AWDLProbePeerCacheResult", static_cast<uint32_t>(posted), 32);
    if (posted != kIOReturnSuccess)
        return posted;
    IOLog("itlwm-awdl: peer cache op %u for %02x:%02x:%02x:%02x:%02x:%02x completed\n",
        operation, address[0], address[1], address[2], address[3], address[4], address[5]);
    return kIOReturnSuccess;
}

IOReturn ItlAwdlInterface::setAWDL_OOB_REQUEST(apple80211_awdl_oob_request *request)
{
    recordProtocol(41);
    setProperty("AWDLNativeUmiLastEntry", 1, 8);
    auto finish = [this](IOReturn result, uint32_t reason) {
        setProperty("AWDLNativeUmiLastEntryReason", reason, 32);
        setProperty("AWDLNativeUmiEntryResult", uint32_t(result), 32);
        return result;
    };
#ifdef AIRPORT_WCL
    auto loop = getWorkLoop();
    if (!request || !loop || !loop->inGate() || probeStopDone || isInactive())
        return finish(kIOReturnUnsupported, 1);
    auto controller = OSDynamicCast(AirportItlwm, getProvider());
    auto hal = controller ? OSDynamicCast(ItlIwm, controller->fHalService) : nullptr;
    if (!hal || !hal->awdlNativeControlReadable(loop) ||
        ItlAwdl::gDataPort.owner != this || ItlAwdl::gDataPort.releasePending)
        return finish(kIOReturnNotReady, 2);
    auto manager = probeCopyPeerManager();
    if (!manager) return finish(kIOReturnNotReady, 3);
    const bool nativeManager = !strcmp(manager->getMetaClass()->getClassName(),
                                       "IO80211AWDLPeerManager") &&
        ItlNativePmVirtualInterface(manager) == this;
    if (!nativeManager) {
        manager->release();
        return finish(kIOReturnNotReady, 4);
    }
    const uint8_t *priv = nullptr;
    memcpy(&priv, reinterpret_cast<const uint8_t *>(manager) + 0x20, sizeof(priv));
    const auto header = reinterpret_cast<const uint8_t *>(request);
    uint16_t length = 0;
    const uint8_t *supplied = nullptr;
    memcpy(&length, header + 0x28, sizeof(length));
    memcpy(&supplied, header + 0x30, sizeof(supplied));
    uint32_t liveLength = 0;
    if (priv) memcpy(&liveLength, priv + 0x2808, sizeof(liveLength));
    const bool valid = priv && supplied == priv + 0x2358 && length &&
        length <= ItlAwdl::kNativeUmiPayloadMax && liveLength == length &&
        ItlAwdl::nativeUmiUnicast(header + 4);
    if (!valid) {
        manager->release();
        return finish(kIOReturnUnsupported, 5);
    }
    setProperty("AWDLNativeUmiRequestPeer", const_cast<uint8_t *>(header + 4), 6);
    const auto result = hal->awdlNativeUmiRequest(header + 4, supplied, length, 1);
    manager->release();
    setProperty("AWDLNativeUmiRequestResult", uint32_t(result), 32);
    setProperty("AWDLNativeUmiRequestLength", length, 16);
    if (result != kIOReturnSuccess) return finish(result, 6);
    IOLog("itlwm-awdl: queued native UMI for %02x:%02x:%02x:%02x:%02x:%02x (%u bytes)\n",
          header[4], header[5], header[6], header[7], header[8], header[9], length);
    return finish(kIOReturnSuccess, 7);
#else
    return finish(kIOReturnUnsupported, 8);
#endif
}

IOReturn ItlAwdlInterface::setAWDL_ELECTION_METRIC(apple80211_awdl_election_metric *data)
{
    recordProtocol(34);
    if (!data) return kIOReturnBadArgument;
    auto root = OSDynamicCast(AirportItlwm, getProvider());
    auto hal = root ? OSDynamicCast(ItlIwm, root->fHalService) : nullptr;
    IOReturn result = kIOReturnNotReady;
    if (hal && !probeStopDone) {
        hal->retain();
        result = hal->awdlNativeElectionMetric(data, sizeof(*data), this);
        hal->release();
    }
    setProperty("AWDLNativeElectionMetricResult", uint32_t(result), 32);
    return result;
}

IOReturn ItlAwdlInterface::setAWDL_ELECTION_RSSI_THRESHOLDS(
        apple80211_awdl_election_rssi_thresholds *data)
{
    recordProtocol(38);
    if (!data) return kIOReturnBadArgument;
    // These are driver configuration inputs; Intel has no matching firmware
    // threshold command, so retain them as the values reported back to family.
    setProperty("AWDLProbeElectionRssi1", data->unk1, 32);
    setProperty("AWDLProbeElectionRssi2", data->unk2, 32);
    setProperty("AWDLProbeElectionRssi3", data->unk3, 32);
    return kIOReturnSuccess;
}

IOReturn ItlAwdlInterface::setAWDL_EXTENSION_STATE_MACHINE_PARAMETERS(
        apple80211_awdl_extension_state_machine_parameter *data)
{
    recordProtocol(31);
    if (!data) return kIOReturnBadArgument;
    setProperty("AWDLProbeExtensionState1", data->unk1, 32);
    setProperty("AWDLProbeExtensionState2", data->unk2, 32);
    setProperty("AWDLProbeExtensionState3", data->unk3, 32);
    setProperty("AWDLProbeExtensionState4", data->unk4, 32);
    return kIOReturnSuccess;
}

IOReturn ItlAwdlInterface::setAWDL_SYNCHRONIZATION_CHANNEL_SEQUENCE(
        apple80211_awdl_sync_channel_sequence *data)
{
    recordProtocol(35);
    if (!data) return kIOReturnBadArgument;
    setProperty("AWDLProbeSequenceEncoding", data->encoding, 8);
    setProperty("AWDLProbeSequenceLength", data->length, 8);
    setProperty("AWDLProbeSequenceSteps", data->step_count, 8);
    setProperty("AWDLProbeSequenceFill", data->fill_channel, 8);
    if(!ItlAwdl::templateLock())return kIOReturnBusy;
    auto &sync=ItlAwdl::gTemplateRuntime.sync;
    if(!ItlAwdl::decodeNativeSequence(reinterpret_cast<const uint8_t *>(data),sizeof(*data),sync)){
        ItlAwdl::templateUnlock();return kIOReturnUnsupported;
    }
    ++ItlAwdl::gTemplateRuntime.syncRevision;
    ItlAwdl::templateUnlock();
    setProperty("AWDLRuntimeNativeSequence",data,sizeof(*data));
    gItlAwdlChannelSequence = *data;
    gItlAwdlChannelSequenceValid = true;
    return kIOReturnSuccess;
}

IOReturn ItlAwdlInterface::setAWDL_SYNC_STATE(apple80211_awdl_sync_state *data)
{
    recordProtocol(37);
    if (!data) return kIOReturnBadArgument;
    setProperty("AWDLTraceSyncStateRequest",static_cast<void *>(data),static_cast<unsigned int>(sizeof(*data)));
    setProperty("AWDLTraceSyncStateRequestBytes",static_cast<uint32_t>(sizeof(*data)),32);
    auto root=OSDynamicCast(AirportItlwm,getProvider());
    auto hal=root?OSDynamicCast(ItlIwm,root->fHalService):nullptr;IOReturn result=kIOReturnUnsupported;
    if(hal&&!probeStopDone){
        hal->retain();
        result=hal->awdlNativeSyncRole(data,sizeof(*data),this,true);hal->release();
    }
    setProperty("AWDLTraceSyncStateSetResult",static_cast<uint32_t>(result),32);
    return result;
}

IOReturn ItlAwdlInterface::setAWDL_ELECTION_PARAMS(
        apple80211_awdl_election_params *data)
{
    recordProtocol(43);
    if (!data) return kIOReturnBadArgument;
    setProperty("AWDLNativeElectionRequestBytes",
        static_cast<uint32_t>(ItlAwdl::kNativeElectionParamsSize), 32);
    auto root = OSDynamicCast(AirportItlwm, getProvider());
    auto hal = root ? OSDynamicCast(ItlIwm, root->fHalService) : nullptr;
    IOReturn result = kIOReturnNotReady;
    if (hal && !probeStopDone) {
        hal->retain();
        result = hal->awdlNativeElectionParams(data,
            ItlAwdl::kNativeElectionParamsSize, this);
        hal->release();
    }
    setProperty("AWDLNativeElectionResult", uint32_t(result), 32);
    return result;
}

IOReturn ItlAwdlInterface::setAWDL_PRESENCE_MODE(apple80211_awdl_presence_mode *data)
{
    recordProtocol(39);
    if (!data) return kIOReturnBadArgument;
    if(!data->mode||data->mode>4||65536u%(data->mode*16))return kIOReturnUnsupported;
    if(!ItlAwdl::templateLock())return kIOReturnBusy;
    if(ItlAwdl::gTemplateRuntime.sync.presence!=data->mode){
        ItlAwdl::gTemplateRuntime.sync.presence=data->mode;++ItlAwdl::gTemplateRuntime.syncRevision;
    }
    ItlAwdl::templateUnlock();
    gItlAwdlPresenceMode = *data;
    setProperty("AWDLProbePresenceModeRequested", data->mode, 32);
    return kIOReturnSuccess;
}

IOReturn ItlAwdlInterface::setP2P_COEX_PROTECTION(apple80211_p2p_coex_protection *data)
{
    recordProtocol(59);
    if (!data) return kIOReturnBadArgument;
    return kIOReturnSuccess;
}

IOReturn ItlAwdlInterface::getAWDL_ELECTION_METRIC(apple80211_awdl_election_metric *data)
{
    recordProtocol(8);
    if (!data) return kIOReturnBadArgument;
    auto root = OSDynamicCast(AirportItlwm, getProvider());
    auto hal = root ? OSDynamicCast(ItlIwm, root->fHalService) : nullptr;
    if (!hal || probeStopDone) return kIOReturnNotReady;
    uint32_t metric = 0;
    uint8_t overrideMetric = 0;
    hal->retain();
    const IOReturn result = hal->awdlNativeElectionMetricRead(metric, overrideMetric);
    hal->release();
    if (result != kIOReturnSuccess) return result;
    data->metric = metric;
    setProperty("AWDLNativeElectionMetricRead", metric, 32);
    setProperty("AWDLNativeElectionMetricOverrideRead", overrideMetric, 8);
    return kIOReturnSuccess;
}

IOReturn ItlAwdlInterface::setAWDL_BSSID(apple80211_awdl_bssid *data)
{
    recordProtocol(33);
    if (!data)
        return kIOReturnBadArgument;
    memcpy(gItlAwdlBssid, data->bssid, APPLE80211_ADDR_LEN);
    memcpy(gItlAwdlBssidPeer, data->unk_mac, APPLE80211_ADDR_LEN);
    gItlAwdlBssidValid = true;
    setProperty("AWDLProbeBssidSet", true);
    setProperty("AWDLProbeBssid", gItlAwdlBssid, APPLE80211_ADDR_LEN);
    return kIOReturnSuccess;
}

IOReturn ItlAwdlInterface::setAWDL_SYNC_FRAME_TEMPLATE(apple80211_awdl_sync_frame_template *data)
{
    recordProtocol(42);
    if (!data)
        return kIOReturnBadArgument;
    const uint32_t length = data->payload_len;
    if (length > sizeof(gItlAwdlSyncFrameTemplate) || (length && !data->payload))
        return kIOReturnBadArgument;
    const uint8_t *frame = static_cast<const uint8_t *>(data->payload);
    if (!ItlAwdl::publishSyncTemplate(frame, length))
        return kIOReturnBadArgument;
    gItlAwdlSyncFrameTemplateLength = length;
    if (length && data->payload)
        memcpy(gItlAwdlSyncFrameTemplate, data->payload, length);
    gItlAwdlSyncFrameTemplateValid = true;
    setProperty("AWDLProbeSyncFrameTemplateLength", (unsigned long long)length, 32);
    setProperty("AWDLProbeSyncTemplateLooksLikeAction",
        ItlAwdl::syncTemplateFrameUsable(frame, length));
    if (length >= 24) {
        const uint16_t fc = uint16_t(frame[0]) | (uint16_t(frame[1]) << 8);
        char a1[18], a2[18], a3[18], head[97] = {};
        snprintf(a1, sizeof(a1), "%02x:%02x:%02x:%02x:%02x:%02x",
            frame[4], frame[5], frame[6], frame[7], frame[8], frame[9]);
        snprintf(a2, sizeof(a2), "%02x:%02x:%02x:%02x:%02x:%02x",
            frame[10], frame[11], frame[12], frame[13], frame[14], frame[15]);
        snprintf(a3, sizeof(a3), "%02x:%02x:%02x:%02x:%02x:%02x",
            frame[16], frame[17], frame[18], frame[19], frame[20], frame[21]);
        for (unsigned i = 0; i < 48 && i < length; ++i)
            snprintf(head + i * 2, 3, "%02x", frame[i]);
        setProperty("AWDLProbeSyncTemplateFC", fc, 16);
        setProperty("AWDLProbeSyncTemplateA1", a1);
        setProperty("AWDLProbeSyncTemplateA2", a2);
        setProperty("AWDLProbeSyncTemplateA3", a3);
        setProperty("AWDLProbeSyncTemplateHead", head);
    }
    return kIOReturnSuccess;
}

IOReturn ItlAwdlInterface::setAWDL_PEER_TRAFFIC_REGISTRATION(apple80211_awdl_peer_traffic_registration *data)
{
    recordProtocol(44);
    if (!data)
        return kIOReturnBadArgument;
    if (data->name_len > sizeof(data->name)) return kIOReturnBadArgument;
    const uint32_t nameLength = data->name_len < sizeof(gItlAwdlTrafficName) - 1 ?
        data->name_len : uint32_t(sizeof(gItlAwdlTrafficName) - 1);
    memcpy(gItlAwdlTrafficName, data->name, nameLength);
    gItlAwdlTrafficName[nameLength] = 0;
    gItlAwdlTrafficActive = data->active;
    ++gItlAwdlTrafficRegistrations;
    setProperty("AWDLProbeTrafficName", gItlAwdlTrafficName);
    setProperty("AWDLProbeTrafficActive", (unsigned long long)gItlAwdlTrafficActive, 32);
    setProperty("AWDLProbeTrafficRegistrations",
        (unsigned long long)gItlAwdlTrafficRegistrations, 32);
#ifdef AIRPORT_WCL
    // This driver owns the AWDL peer table; the native table has no matching
    // entry, so forwarding returns kIOReturnNotFound and aborts AirDrop ASK.
    setProperty("AWDLProbeTrafficForwarded",false);
    setProperty("AWDLProbeTrafficDeferred",false);
    setProperty("AWDLProbeTrafficResult",
        (unsigned long long)(uint32_t)kIOReturnSuccess,32);
    return kIOReturnSuccess;
#else
    return kIOReturnSuccess;
#endif
}

IOReturn ItlAwdlInterface::newUserClient(task_t task, void *security, unsigned int type,
                                      OSDictionary *properties, IOUserClient **handler)
{
    static volatile SInt32 calls = 0;
    setProperty("AWDLTraceUserClientCalls", static_cast<unsigned long long>(static_cast<uint32_t>(OSIncrementAtomic(&calls)) + 1), 32);
    setProperty("AWDLTraceUserClientType", type, 32);
    const IOReturn result = handler
        ? IO80211VirtualInterface::newUserClient(task, security, type, properties, handler)
        : kIOReturnBadArgument;
    setProperty("AWDLTraceUserClientResult", static_cast<unsigned long long>(static_cast<uint32_t>(result)), 32);
    setProperty("AWDLTraceUserClientReturned", result == kIOReturnSuccess && handler && *handler);
    if (result == kIOReturnSuccess && handler && *handler)
        setProperty("AWDLTraceUserClientClass", (*handler)->getMetaClass()->getClassName());
    if (type < 8) {
        char key[48];
        snprintf(key, sizeof(key), "AWDLTraceClientType%uResult", type);
        setProperty(key, static_cast<unsigned long long>(static_cast<uint32_t>(result)), 32);
    }
    // Ownership of the returned client is exactly the native caller contract.
    return result;
}

UInt32 ItlAwdlInterface::getFeatureFlags()
{
    const UInt32 flags = IO80211VirtualInterface::getFeatureFlags();
    setProperty("AWDLTraceFeatureFlags", flags, 32);
    return flags;
}

IOReturn ItlAwdlInterface::setMacAddress(ether_addr &address)
{
    setProperty("AWDLSetMacEntry", true);
    const IO80211LinkState state = linkState();
    setProperty("AWDLSetMacLinkState", uint32_t(state), 32);
    if (address.octet[0] & 1) {
        setProperty("AWDLSetMacResult", uint32_t(kIOReturnBadArgument), 32);
        return kIOReturnBadArgument;
    }
    bool nonzero = false;
    for (unsigned i = 0; i < 6; ++i)
        nonzero |= address.octet[i] != 0;
    if (!nonzero) {
        setProperty("AWDLSetMacResult", uint32_t(kIOReturnBadArgument), 32);
        return kIOReturnBadArgument;
    }
    auto net = getBSDInterface();
    if (!net) {
        setProperty("AWDLSetMacResult", uint32_t(kIOReturnNotReady), 32);
        return kIOReturnNotReady;
    }
    const IOReturn result = IO80211SkywalkInterface::setHardwareAddress(&address);
    setProperty("AWDLSetMacHardwareResult", uint32_t(result), 32);
    if (result != kIOReturnSuccess) {
        setProperty("AWDLSetMacResult", uint32_t(result), 32);
        return result;
    }
    const errno_t bsdResult = ifnet_set_lladdr(net, address.octet, IEEE80211_ADDR_LEN);
    setProperty("AWDLSetMacBSDResult", uint32_t(bsdResult), 32);
    if (bsdResult) {
        setProperty("AWDLSetMacResult", uint32_t(kIOReturnError), 32);
        return kIOReturnError;
    }
    setProperty("AWDLSetMacResult", uint32_t(kIOReturnSuccess), 32);
    setProperty("AWDLUpdatedMacAddress", address.octet, IEEE80211_ADDR_LEN);
    return kIOReturnSuccess;
}

void ItlAwdlInterface::postAvailabilityWindowEvent(bool start, uint32_t firmwareStartGp2,
                                                    uint16_t awNumber)
{
    const uint64_t address = getSelfMacAddr();
    if (!address)
        return;
    const auto &radio=ItlAwdl::gTemplateRadio;
    setProperty("AWDLRuntimePsfSubmitted",radio.psfSubmitted,32);
    setProperty("AWDLRuntimeMifSubmitted",radio.mifSubmitted,32);
    setProperty("AWDLRuntimePsfCompleted",radio.psfCompleted,32);
    setProperty("AWDLRuntimeMifCompleted",radio.mifCompleted,32);
    setProperty("AWDLRuntimeLastSkip",radio.lastSkip,32);
    setProperty("AWDLRuntimeEpochGp2",radio.clock.epochGp2,32);
    setProperty("AWDLRuntimeEpochAw",radio.clock.epochAw,16);
    setProperty("AWDLRuntimeEpochHostNs",radio.clock.epochHostNs,64);
    setProperty("AWDLRuntimeClockRevision",radio.clock.revision,32);
    setProperty("AWDLRuntimeClockGeneration",radio.clock.generation,32);
    setProperty("AWDLRuntimeTxGp2",radio.lastTxGp2,32);
    setProperty("AWDLRuntimeStartGp2",radio.lastStartGp2,32);
    setProperty("AWDLRuntimeTargetGp2",radio.targetGp2,32);
    setProperty("AWDLRuntimeTxAw",radio.lastAw,16);
    setProperty("AWDLRuntimeTxDown",radio.lastDown,16);
    if(radio.lastLength)setProperty("AWDLRuntimeLastFrame",const_cast<uint8_t *>(radio.lastFrame),radio.lastLength);
    static volatile SInt32 starts = 0, ends = 0;
    if (start) {
        uint8_t event[36] = {};
        ItlAwdl::encodeAvailabilityWindowStart(
            event, reinterpret_cast<const uint8_t *>(&address), firmwareStartGp2, awNumber);
        const SInt32 count = static_cast<SInt32>(OSIncrementAtomic(&starts)) + 1;
        setProperty("AWDLTraceAwStartEventCount", static_cast<unsigned long long>(count), 32);
        setProperty("AWDLTraceAwStartGp2", firmwareStartGp2, 32);
        postMessage(ItlAwdl::kAvailabilityWindowStartEvent, event, sizeof(event), true);
    } else {
        uint8_t event[40] = {};
        ItlAwdl::encodeAvailabilityWindowEnd(event, firmwareStartGp2, awNumber);
        const SInt32 count = static_cast<SInt32>(OSIncrementAtomic(&ends)) + 1;
        setProperty("AWDLTraceAwEndEventCount", static_cast<unsigned long long>(count), 32);
        setProperty("AWDLTraceAwEndGp2", firmwareStartGp2, 32);
        postMessage(ItlAwdl::kAvailabilityWindowEndEvent, event, sizeof(event), true);
    }
}

bool AirportItlwm::isCommandProhibited(int command)
{
    // These entry counters locate the family boundary; they do not claim an
    // ioctl succeeded or that a particular process reached a lower handler.
    static volatile SInt32 counts[8] = {};
    static const int commands[] = {12, 94, 95, 96, 97, 117, 132, 458};
    static const char *const keys[] = {
        "AWDLTraceCmd12Checks", "AWDLTraceCmd94Checks", "AWDLTraceCmd95Checks",
        "AWDLTraceCmd96Checks", "AWDLTraceCmd97Checks", "AWDLTraceCmd117Checks",
        "AWDLTraceCmd132Checks", "AWDLTraceCmd458Checks"
    };
    for (unsigned i = 0; i < 8; ++i) {
        if (command == commands[i]) {
            const uint32_t count = static_cast<uint32_t>(OSIncrementAtomic(&counts[i])) + 1;
            setProperty(keys[i], static_cast<unsigned long long>(count), 32);
            break;
        }
    }
    return false;
}

#define super IO80211Controller
OSDefineMetaClassAndStructors(AirportItlwm, IO80211Controller);
OSDefineMetaClassAndStructors(CTimeout, OSObject)
#undef super
#define super IO80211AWDLProtocol
OSDefineMetaClassAndStructors(ItlAwdlInterface, IO80211AWDLProtocol)
#undef super
#define super IO80211Controller

// Sequoia 15.8.1 teardown, with an explicit stop/free BPF ownership boundary.
// Native free() omits BPF allocations and dereferences the Skywalk expansion
// before checking it. Handle partial init and clean BPF exactly once here.
void ItlAwdlInterface::free()
{
    if (ItlAwdl::gEngine.availabilityWindowContext == this) {
        ItlAwdl::gEngine.availabilityWindowEvent = nullptr;
        ItlAwdl::gEngine.availabilityWindowContext = nullptr;
    }
    // A bound filter holds this object until its last detached callback. Its
    // callbacks are drained by the matching base-interface detach before stop.
    if (probeBSDLock) {
        IOLockFree(probeBSDLock);
        probeBSDLock = nullptr;
    }
    // Transfer ownership to a local before the base frees this object.
    OSNumber *marker = probeFreeMarker;
    probeFreeMarker = nullptr;
    if (marker)
        marker->setValue(1);
    void *skywalkExpansion = nullptr;
    void *virtualExpansion = nullptr;
    memcpy(&skywalkExpansion, reinterpret_cast<const uint8_t *>(this) + 0x110, sizeof(void *));
    memcpy(&virtualExpansion, reinterpret_cast<const uint8_t *>(this) + 0x118, sizeof(void *));
    if (virtualExpansion && !probeBpfFreed)
        IO80211VirtualInterface::freeBpfResources();
    if (skywalkExpansion)
        IO80211VirtualInterface::free();
    else
        IOSkywalkEthernetInterface::free();
    // Do not access this after the base call. The independently retained marker
    // survives deallocation and remains visible on the controller's registry.
    if (marker) {
        marker->setValue(2);
        marker->release();
    }
}

// Adapt the legacy ifnet to native Skywalk media queries and
// multicast membership notifications. It neither replaces the native BSD pointer nor enables packet I/O.
struct ItlAwdlBSDControl {
    ItlAwdlInterface *owner;
    IORegistryEntry *legacy;
    ifnet_t interface;
    bool closing;
    uint32_t calls;
    uint32_t multicastCalls;
    uint32_t outputCalls,outputQueued,outputRejected;
};

// OpenBSD net80211 also defines ifmediareq (64-bit media words) and the
// SIOCGIFMEDIA macro. Never pass that ABI to the Darwin native handler.
static constexpr unsigned long kItlDarwinGetMedia = 0xc02c6938UL;
static constexpr unsigned long kItlDarwinAddMulti = 0x80206931UL;
static constexpr unsigned long kItlDarwinDelMulti = 0x80206932UL;
static constexpr uint32_t kItlDarwinWifiMedia = 0x80; // IEEE80211 | AUTO in Darwin
#pragma pack(push, 4)
struct ItlDarwinIfmediareq {
    char ifm_name[16];
    int32_t ifm_current, ifm_mask, ifm_status, ifm_active, ifm_count;
    uint64_t ifm_ulist;
};
#pragma pack(pop)
static_assert(sizeof(ItlDarwinIfmediareq) == 44, "Darwin media ioctl size");
static_assert(__builtin_offsetof(ItlDarwinIfmediareq, ifm_current) == 16, "Darwin media current offset");
static_assert(__builtin_offsetof(ItlDarwinIfmediareq, ifm_status) == 24, "Darwin media status offset");
static_assert(kItlDarwinGetMedia == (0xc0000000UL | (sizeof(ItlDarwinIfmediareq) << 16) | 0x6938), "Darwin media ioctl encoding");

static errno_t awdlBSDDataOutput(void *cookie,ifnet_t interface,protocol_family_t protocol,mbuf_t *packet)
{
    auto context=static_cast<ItlAwdlBSDControl *>(cookie);auto owner=context->owner;
    IOLockLock(owner->probeBSDLock);
    const bool live=!context->closing&&!owner->probeStopDone&&interface==context->interface;
    if(live){owner->retain();owner->setProperty("AWDLBSDOutputCalls",++context->outputCalls,32);}
    IOLockUnlock(owner->probeBSDLock);
    if(!live)return ENETDOWN;
    errno_t result=EINVAL;
    if(protocol==AF_INET6&&packet&&*packet&&(mbuf_flags(*packet)&MBUF_PKTHDR)){
        const size_t n=mbuf_pkthdr_len(*packet);
        if(n>=54&&n<=ItlAwdl::kEtherDataMax){
            uint8_t bytes[ItlAwdl::kEtherDataMax];
            result=mbuf_copydata(*packet,0,n,bytes);
            if(!result){
                auto controller=OSDynamicCast(AirportItlwm,owner->getProvider());
                ItlAwdlDataCall call={ItlAwdlDataCall::BsdTx};call.owner=owner;call.packets=bytes;call.count=unsigned(n);
                result=controller&&controller->awdlDataDispatch(call)==kIOReturnSuccess?int(call.result):ENETDOWN;
            }
        }
    }
    IOLockLock(owner->probeBSDLock);
    owner->setProperty("AWDLBSDOutputQueued",result?context->outputQueued:++context->outputQueued,32);
    owner->setProperty("AWDLBSDOutputRejected",result?++context->outputRejected:context->outputRejected,32);
    owner->setProperty("AWDLBSDOutputLastError",uint32_t(result),32);
    IOLockUnlock(owner->probeBSDLock);
    owner->release();
    if(result)return result; // DLIL frees on error; our queue has not consumed the mbuf.
    mbuf_freem(*packet);*packet=nullptr;return EJUSTRETURN; // A private byte copy is queued.
}

static int awdlBSDDataInput(OSObject *object,const uint8_t *eth,size_t n)
{
    auto owner=OSDynamicCast(ItlAwdlInterface,object);
    if(!owner||!eth||n<54||n>ItlAwdl::kEtherDataMax||ItlAwdl::dataBe16(eth+12)!=0x86dd)return EINVAL;
    auto interface=owner->probeCopyBoundBSD();if(!interface)return ENETDOWN;
    mbuf_t packet=nullptr;unsigned chunks=1;
    errno_t result=mbuf_allocpacket(MBUF_DONTWAIT,n,&chunks,&packet);
    if(!result){
        result=mbuf_copyback(packet,0,n,eth,MBUF_DONTWAIT);
        if(!result)result=mbuf_pkthdr_setrcvif(packet,interface);
        if(!result){
            mbuf_pkthdr_setheader(packet,mbuf_data(packet));mbuf_adj(packet,14);
            // KPI consumes/frees the packet even on error. Never free it twice.
            auto input=packet;packet=nullptr;result=ifnet_input(interface,input,nullptr);
        }
    }
    if(packet)mbuf_freem(packet);
    ItlIfnetRelease(interface);
    static volatile SInt32 calls=0,delivered=0;
    owner->setProperty("AWDLBSDInputCalls",uint32_t(OSIncrementAtomic(&calls))+1,32);
    if(!result)owner->setProperty("AWDLBSDInputSubmitted",uint32_t(OSIncrementAtomic(&delivered))+1,32);
    owner->setProperty("AWDLBSDInputLastError",uint32_t(result),32);
    return result;
}

static errno_t awdlBSDControlIoctl(void *cookie, ifnet_t interface,
                                protocol_family_t, unsigned long command, void *argument)
{
    const bool mediaQuery = command == kItlDarwinGetMedia;
    const bool multicastUpdate = command == kItlDarwinAddMulti || command == kItlDarwinDelMulti;
    if (!mediaQuery && !multicastUpdate)
        return EOPNOTSUPP;
    auto context = static_cast<ItlAwdlBSDControl *>(cookie);
    auto owner = context->owner;
    IOLockLock(owner->probeBSDLock);
    if (context->closing || owner->probeStopDone || interface != context->interface || (mediaQuery && !argument)) {
        IOLockUnlock(owner->probeBSDLock);
        return EOPNOTSUPP;
    }
    // Native multicast updates use the supplied ifnet's kernel membership list;
    // the asynchronous notification carries no argument buffer. Neither path
    // enables the link or modifies the infrastructure interface's IWM filter.
    const IOReturn status = mediaQuery ?
        owner->IOSkywalkNetworkInterface::processBSDCommand(
            interface, static_cast<unsigned int>(command), argument) :
        owner->IOSkywalkEthernetInterface::processBSDCommand(
            interface, static_cast<unsigned int>(command), argument);
    if (mediaQuery) {
        owner->setProperty("AWDLTraceMediaQueryCalls", ++context->calls, 32);
        owner->setProperty("AWDLTraceMediaQueryResult", static_cast<uint32_t>(status), 32);
        if (status == 0) {
            auto media = static_cast<ItlDarwinIfmediareq *>(argument);
            owner->setProperty("AWDLTraceMediaCurrent", static_cast<uint32_t>(media->ifm_current), 32);
            owner->setProperty("AWDLTraceMediaStatus", static_cast<uint32_t>(media->ifm_status), 32);
        }
    } else {
        owner->setProperty("AWDLTraceMulticastUpdateCalls", ++context->multicastCalls, 32);
        owner->setProperty("AWDLTraceMulticastUpdateCommand", command, 32);
        owner->setProperty("AWDLTraceMulticastUpdateResult", static_cast<uint32_t>(status), 32);
    }
    IOLockUnlock(owner->probeBSDLock);
    return status == 0 ? EJUSTRETURN : (status > 0 && status < 4096 ? status : EIO);
}

static void awdlBSDControlDetached(void *cookie, ifnet_t)
{
    // KPI contract: last callback, after all filter invocations have drained.
    auto context = static_cast<ItlAwdlBSDControl *>(cookie);
    auto owner = context->owner;
    IOLockLock(owner->probeBSDLock);
    if (owner->probeBSDControl == context)
        owner->probeBSDControl = nullptr;
    owner->setProperty("AWDLProbeBSDControlDetached", true);
    IOLockUnlock(owner->probeBSDLock);
    ifnet_release(context->interface);
    context->legacy->release();
    IOFree(context, sizeof(*context));
    owner->release();
}

//24H32/XNU: AWDL implies LOCALNET_PRIVATE, ND6ALT and RESTRICTED_RECV.
// Identity persists until our ifnet is destroyed; it does not publish linkUP or a peer.
static constexpr uint32_t kItlAwdlIdentityMask=0x001e0000;
static errno_t awdlBSDInstallIdentity(ifnet_t interface,uint32_t &before,bool &changed)
{
    changed=false;
    before=ItlIfnetEflags(interface);
    const auto identity=before&kItlAwdlIdentityMask;
    // Reject a pre-existing partial policy instead of adopting or overwriting it.
    if(identity && identity!=kItlAwdlIdentityMask)return EINVAL;
    if(identity==kItlAwdlIdentityMask)return 0;
    const auto error=ItlIfnetSetEflags(interface,kItlAwdlIdentityMask,kItlAwdlIdentityMask);
    if(error)return error;
    changed=true;
    return (ItlIfnetEflags(interface)&kItlAwdlIdentityMask)==kItlAwdlIdentityMask?0:EIO;
}
static errno_t awdlBSDRollbackIdentity(ifnet_t interface,uint32_t before,bool changed)
{
    if(!changed)return 0;
    // Roll back only our complete policy, preserving unrelated concurrent flags.
    if((ItlIfnetEflags(interface)&kItlAwdlIdentityMask)!=kItlAwdlIdentityMask)return EBUSY;
    const auto error=ItlIfnetSetEflags(interface,before&kItlAwdlIdentityMask,kItlAwdlIdentityMask);
    if(error)return error;
    return (ItlIfnetEflags(interface)&kItlAwdlIdentityMask)==(before&kItlAwdlIdentityMask)?0:EIO;
}

bool ItlAwdlInterface::probeBindBSDControl(ifnet_t interface, IORegistryEntry *legacy)
{
    if (!probeBSDLock || !interface || !legacy ||
        ItlNativeGetIfnet(legacy)!=interface || getProperty("AWDLIdentityRollbackFailed"))
        return false;
    IOLockLock(probeBSDLock);
    if (probeBSDControl || probeStopDone || isInactive()) {
        const bool ready = !probeStopDone && !isInactive() && probeBSDControl &&
            !probeBSDControl->closing && probeBSDControl->interface == interface &&
            probeBSDControl->legacy == legacy &&
            (ItlIfnetEflags(interface)&kItlAwdlIdentityMask)==kItlAwdlIdentityMask;
        IOLockUnlock(probeBSDLock);
        return ready;
    }
    auto context = static_cast<ItlAwdlBSDControl *>(IOMalloc(sizeof(ItlAwdlBSDControl)));
    if (!context) {
        IOLockUnlock(probeBSDLock);
        return false;
    }
    bzero(context, sizeof(*context));
    const errno_t refResult = ifnet_reference(interface);
    if (refResult) {
        IOFree(context, sizeof(*context));
        IOLockUnlock(probeBSDLock);
        return false;
    }
    bzero(context,sizeof(*context));
    context->owner = this;
    context->legacy = legacy;
    context->interface = interface;
    retain();
    legacy->retain();
    struct iff_filter filter = {};
    filter.iff_cookie = context;
    filter.iff_name = "itl-awdl-native-control";
    filter.iff_output = awdlBSDDataOutput;
    filter.iff_ioctl = awdlBSDControlIoctl;
    filter.iff_detached = awdlBSDControlDetached;
    interface_filter_t filterRef = nullptr;
    uint32_t previousFlags=0;bool identityChanged=false;
    errno_t result=awdlBSDInstallIdentity(interface,previousFlags,identityChanged);
    setProperty("AWDLIdentityBefore",previousFlags,32);
    setProperty("AWDLIdentityInstallResult",static_cast<uint32_t>(result),32);
    if(!result)result=iflt_attach(interface,&filter,&filterRef);
    if(result){
        const auto rollback=awdlBSDRollbackIdentity(interface,previousFlags,identityChanged);
        setProperty("AWDLIdentityRollbackResult",static_cast<uint32_t>(rollback),32);
        if(rollback)setProperty("AWDLIdentityRollbackFailed",true);
    }
    setProperty("AWDLIdentityAfter",ItlIfnetEflags(interface),32);
    setProperty("AWDLProbeBSDControlAttachResult", static_cast<uint32_t>(result), 32);
    if (result == 0)
        probeBSDControl = context;
    IOLockUnlock(probeBSDLock);
    if (result) {
        ifnet_release(interface);
        legacy->release();
        IOFree(context, sizeof(*context));
        release();
        return false;
    }
    // This interface was checked by pointer identity against awdl0. Publish
    // only its real name, without claiming a native getBSDInterface binding.
    setBSDName("awdl0");
    setProperty("IOInterfaceName", "awdl0");
    legacy->setProperty("BSD Name", "awdl0");
    legacy->setProperty("IOInterfaceUnit", 0ULL, 32);
    setProperty("AWDLProbeBSDControlReady", true);
    registerService();
    return true;
}

void ItlAwdlInterface::probeUnbindBSDControl()
{
    if (!probeBSDLock)
        return;
    IOLockLock(probeBSDLock);
    auto context = probeBSDControl;
    IORegistryEntry *legacy = context && !context->closing ? context->legacy : nullptr;
    if (legacy) {
        context->closing = true;
        legacy->retain();
    }
    IOLockUnlock(probeBSDLock);
    if (legacy) {
        // Pair our qualified base attach with the same base detach. The native
        // legacy override is a no-op. Base detach waits for ifnet callbacks;
        // automatic filter detach invokes the final callback and drops refs.
        // Never hold probeBSDLock while waiting, and never detach a stale filter.
        ItlNativeDetachFromDataLinkLayer(legacy, 0, nullptr);
        legacy->release();
    }
}

bool ItlAwdlInterface::probeHasPeerManager() const
{
    const uint8_t *expansion = nullptr;
    void *peer = nullptr;
    memcpy(&expansion, reinterpret_cast<const uint8_t *>(this) + 0x110, sizeof(expansion));
    if (expansion)
        memcpy(&peer, expansion + 0x28, sizeof(peer));
    return peer != nullptr;
}

// These target-pinned native getters only read state; they never configure or enable AWDL.
extern "C" bool ItlNativeAwdlConfigured(void *)
    __asm__("__ZN22IO80211AWDLPeerManager16isAwdlConfiguredEv");
extern "C" bool ItlNativeAwdlConfigureInProgress(void *)
    __asm__("__ZN22IO80211AWDLPeerManager25isAwdlConfigureInProgressEv");

bool ItlAwdlInterface::probeNativeControlPermits(bool start){
    if(!probeBSDLock)return false;
    IOLockLock(probeBSDLock);const bool allowed=probeControlPin.permits(start);
    IOLockUnlock(probeBSDLock);return allowed;
}
bool ItlAwdlInterface::probeNativeControlPin(uint64_t epoch){
    auto loop=getWorkLoop();auto &port=ItlAwdl::gDataPort;
    if(!loop||!loop->inGate()||!probeBSDLock||port.owner!=this||port.releasePending||
       port.nativeSession||port.tx.enabled||!port.tx.empty()||port.hardware.pending||port.rxCount||
       !port.queueActivation.idle()||port.linkUp||port.linkBusy||port.linkDownPending)return false;
    IOLockLock(probeBSDLock);
    const bool accepted=!probeStopDone&&!isInactive()&&probeControlPin.acquire(epoch);
    if(accepted)retain();
    IOLockUnlock(probeBSDLock);return accepted;
}
bool ItlAwdlInterface::probeNativeControlUnpin(uint64_t epoch,bool confirmedDown){
    auto loop=getWorkLoop();if(!loop||!loop->inGate()||!probeBSDLock)return false;
    IOLockLock(probeBSDLock);const bool retired=probeControlPin.release(epoch,confirmedDown);
    IOLockUnlock(probeBSDLock);
    if(retired)release();return retired;
}
bool ItlAwdlInterface::probeNativeControlStopHandoff(uint64_t epoch){
    auto loop=getWorkLoop();if(!loop||!loop->inGate()||!probeBSDLock)return false;
    IOLockLock(probeBSDLock);const bool handed=probeControlPin.handoff(epoch);
    IOLockUnlock(probeBSDLock);if(handed)release();return handed;
}
bool ItlAwdlInterface::probeNativeControlClose(){
    if(!probeBSDLock)return probeControlPin.close(); // No pin can be acquired without this lock.
    IOLockLock(probeBSDLock);const bool drained=probeControlPin.close();
    IOLockUnlock(probeBSDLock);return drained;
}

OSObject *ItlAwdlInterface::probeCopyPeerManager(bool requireActive){
    if(probeStopDone||(requireActive&&isInactive())||!getWorkLoop()||!getWorkLoop()->inGate())return nullptr;
    const uint8_t *expansion=nullptr;OSObject *peer=nullptr;
    memcpy(&expansion,reinterpret_cast<const uint8_t *>(this)+0x110,sizeof(expansion));
    if(expansion)memcpy(&peer,expansion+0x28,sizeof(peer));
    if(peer)peer->retain();
    return peer;
}

extern "C" uint32_t ItlNativeRemoveDuplicatesRx(OSObject *)
    __asm__("__ZN18IO80211PeerManager21getRemoveDuplicatesRxEv");
bool ItlAwdlInterface::probeDuplicatePolicy(bool &enabled){
    auto manager=probeCopyPeerManager();
    if(!manager)return false;
    const bool valid=!strcmp(manager->getMetaClass()->getClassName(),"IO80211AWDLPeerManager");
    if(valid)enabled=ItlNativeRemoveDuplicatesRx(manager)!=0;
    manager->release();return valid;
}

// Sample only the current PM-owned primary MI buffer while a bounded native
// discovery session holds this interface. No caller-owned pointer is retained.
IOReturn ItlAwdlInterface::setAWDL_OOB_AUTO_REQUEST(apple80211_awdl_oob_request *request){
    recordProtocol(50);
    setProperty("AWDLNativeUmiLastEntry", 2, 8);
    auto finish = [this](IOReturn result, uint32_t reason) {
        setProperty("AWDLNativeUmiLastEntryReason", reason, 32);
        setProperty("AWDLNativeUmiAutoEntryResult", uint32_t(result), 32);
        return result;
    };
    OSIncrementAtomic(&gServiceMifStages[0]);
    const auto revision=ItlAwdl::invalidateOob();
#ifdef AIRPORT_WCL
    auto loop=getWorkLoop();
    if(!request||!loop||!loop->inGate()||probeStopDone||isInactive())
        return finish(kIOReturnUnsupported,1);
    OSIncrementAtomic(&gServiceMifStages[1]);
    auto controller=OSDynamicCast(AirportItlwm,getProvider());
    auto hal=controller?OSDynamicCast(ItlIwm,controller->fHalService):nullptr;
    if(!hal||!hal->awdlNativeControlReadable(loop)||ItlAwdl::gDataPort.owner!=this||
       ItlAwdl::gDataPort.releasePending)return finish(kIOReturnUnsupported,2);
    auto &d=hal->awdlNativeDiscovery;
    if(!d.open||d.closed||d.owner!=reinterpret_cast<uintptr_t>(this)||
       d.epoch!=hal->awdlNativeSearch.epoch)return finish(kIOReturnUnsupported,3);
    OSIncrementAtomic(&gServiceMifStages[2]);
    auto pm=probeCopyPeerManager();
    if(!pm)return finish(kIOReturnUnsupported,4);
    const uint8_t *supplied=nullptr;
    size_t count=0;
    bool offered=false;
    if(!strcmp(pm->getMetaClass()->getClassName(),"IO80211AWDLPeerManager")&&
       ItlNativePmVirtualInterface(pm)==this&&ItlNativeAwdlConfigured(pm)){
        OSIncrementAtomic(&gServiceMifStages[3]);
        const uint8_t *priv=nullptr;
        memcpy(&priv,reinterpret_cast<const uint8_t *>(pm)+0x20,sizeof(priv));
        const auto header=reinterpret_cast<const uint8_t *>(request);
        count=ItlAwdl::dataLe16(header+0x28);
        memcpy(&supplied,header+0x30,sizeof(supplied));
        uint32_t liveLength=0;
        if(priv&&count&&count<=1200&&supplied==priv+0x2358){
            memcpy(&liveLength,priv+0x2808,sizeof(liveLength));
            if(liveLength==count){
                uint64_t now=0;absolutetime_to_nanoseconds(mach_absolute_time(),&now);
                OSIncrementAtomic(&gServiceMifStages[4]);
                // Parse a private, immutable copy. Never parse changing length fields twice
                // in a buffer owned by another component.
                OSData *sample=OSData::withBytes(priv+0x2358,unsigned(count));
                if(sample){
                    const auto bytes=static_cast<const uint8_t *>(sample->getBytesNoCopy());
                    if(d.matches(reinterpret_cast<uintptr_t>(this),hal->awdlNativeSearch.epoch,d.generation,now)&&
                       d.mifService.offer(bytes,count,d.local,revision,d.epoch,d.generation,now)){
                        OSIncrementAtomic(&gServiceMifStages[5]);
                        offered=true;
                    }
                    sample->release();
                }
            }
        }
    }
    if(!offered){
        pm->release();
        return finish(kIOReturnUnsupported,5);
    }
    setProperty("AWDLNativeUmiAutoLength",count,16);
    pm->release();
#endif
    // Auto MI is periodic control-plane work. Once its service snapshot is
    // accepted, report success even when this request was multicast-only.
    return finish(kIOReturnSuccess,6);
}

IOReturn AirportItlwm::awdlPublishNativeControlState(){
    auto interface=OSDynamicCast(ItlAwdlInterface,fAWDLInterface);
    auto hal=OSDynamicCast(ItlIwm,fHalService);
    if(!_fWorkloop||!_fWorkloop->inGate()||!interface||!hal||!hal->awdlNativeControlReadable(_fWorkloop)||
       interface->isInactive()||interface->probeStopDone||interface->getWorkLoop()!=_fWorkloop||
       ItlAwdl::gDataPort.owner!=interface||ItlAwdl::gDataPort.releasePending)return kIOReturnNotReady;
    const auto begin=mach_absolute_time();
    auto manager=interface->probeCopyPeerManager();
    const bool present=manager!=nullptr;
    const bool valid=manager&&!strcmp(manager->getMetaClass()->getClassName(),"IO80211AWDLPeerManager");
    const bool configured=valid&&ItlNativeAwdlConfigured(manager);
    const bool configuring=valid&&ItlNativeAwdlConfigureInProgress(manager);
    auto nativeVirtual=valid?ItlNativePmVirtualInterface(manager):nullptr;
    const bool matches=nativeVirtual==interface;
    const uint32_t link=matches?uint32_t(static_cast<IO80211SkywalkInterface *>(interface)->linkState()):UINT32_MAX;
    if(manager)manager->release();
    const bool roleAccepted=hal->awdlNativeSyncRoleReady(),roleConfirmed=hal->awdlNativeSyncRoleConfirmed();
    // Observational samples are not a transaction across the native framework's state machine.
    const char *keys[]={"Version","StartTicks","EndTicks","PeerManagerPresent","PeerManagerClassValid",
        "ConfiguredKnown","Configured","ConfigureInProgress","VirtualMatchesOwner","VirtualLinkKnown","VirtualLinkState",
        "SearchEpoch","SearchPhase","BackendEnabled","NativeRoleAccepted","NativeRoleTxConfirmed"};
    const uint64_t values[]={1,begin,mach_absolute_time(),present,valid,valid,configured,configuring,matches,matches,link,
        hal->awdlNativeSearch.epoch,uint32_t(hal->awdlNativeSearch.phase),hal->awdlNativeSearchEnabled(),roleAccepted,roleConfirmed};
    auto d=OSDictionary::withCapacity(16);if(!d)return kIOReturnNoMemory;
    bool ok=true;for(unsigned n=0;n<16&&ok;++n){auto value=OSNumber::withNumber(values[n],64);if(!value){ok=false;break;}ok=d->setObject(keys[n],value);value->release();}
    if(ok)ok=setProperty("AWDLNativeControlState",d);d->release();
    return ok?kIOReturnSuccess:kIOReturnNoMemory;
}

ifnet_t ItlAwdlInterface::probeCopyBoundBSD()
{
    if (!probeBSDLock)
        return nullptr;
    void *found = nullptr;
    if (ItlIfnetFindByName("awdl0", &found) || !found)
        return nullptr;
    IOLockLock(probeBSDLock);
    auto context = probeBSDControl;
    const bool ready = !probeStopDone && !isInactive() && context && !context->closing &&
        context->owner == this && context->interface == found && context->legacy &&
        ItlNativeGetIfnet(context->legacy) == found;
    IOLockUnlock(probeBSDLock);
    if(!ready){ItlIfnetRelease(found);return nullptr;}
    return static_cast<ifnet_t>(found);
}

bool ItlAwdlInterface::probeHasBoundBSD()
{
    auto interface=probeCopyBoundBSD();
    if(interface)ItlIfnetRelease(interface);
    return interface!=nullptr;
}

struct ItlNativeBsdOps {
    void *bound(OSObject *owner){return static_cast<ItlAwdlInterface *>(owner)->getBSDInterface();}
    bool retainInterface(void *interface){return ifnet_reference(static_cast<ifnet_t>(interface))==0;}
    void releaseInterface(void *interface){ifnet_release(static_cast<ifnet_t>(interface));}
    bool running(void *interface){return (ifnet_flags(static_cast<ifnet_t>(interface))&IFF_RUNNING)!=0;}
    bool setRunning(void *interface,bool up){return ifnet_set_flags(static_cast<ifnet_t>(interface),up?IFF_RUNNING:0,IFF_RUNNING)==0;}
    int prepare(OSObject *owner,void *interface){
        auto awdl=static_cast<ItlAwdlInterface *>(owner);
        // The existing Ethernet bridge already owns its framing/media setup.
        // This base API binds the borrowed ifnet, sets registered MTU/readiness.
        const auto result=awdl->IOSkywalkNetworkInterface::prepareBSDInterface(static_cast<ifnet_t>(interface),0);
        awdl->setProperty("AWDLNativeBSDPrepareResult",uint32_t(result),32);return result;
    }
    void finalize(OSObject *owner,void *interface){
        static_cast<ItlAwdlInterface *>(owner)->IOSkywalkNetworkInterface::finalizeBSDInterface(static_cast<ifnet_t>(interface),0);
    }
};
static ItlAwdl::NativeBsdBinding<OSObject> awdlNativeBsdBinding;
extern "C" errno_t ifnet_notice_node_presence_v2(ifnet_t,struct sockaddr *,struct sockaddr_dl *,int32_t,int,int,u_int8_t[48]);
extern "C" errno_t ifnet_notice_node_absence(ifnet_t,struct sockaddr *);
struct rtentry;
extern "C" int rtrequest_scoped(int,struct sockaddr *,struct sockaddr *,struct sockaddr *,int,struct rtentry **,unsigned int);
struct ItlNativePeerOps:ItlNativeBsdOps {
    int notice(void *value,const ItlAwdl::NativePeerKey &key,bool present){
        static_assert(sizeof(sockaddr_in6)==28&&offsetof(sockaddr_in6,sin6_addr)==8,"IPv6 node KPI ABI");
        static_assert(sizeof(sockaddr_dl)==20&&offsetof(sockaddr_dl,sdl_data)==8,"link node KPI ABI");
        auto net=static_cast<ifnet_t>(value);if(!net||!key.valid())return EINVAL;
        const auto index=ifnet_index(net);if(!index||index>65535)return EINVAL;
        sockaddr_in6 address={};address.sin6_len=sizeof(address);address.sin6_family=AF_INET6;
        memcpy(address.sin6_addr.s6_addr,key.ip,16);address.sin6_scope_id=index;
        if(!present){
            auto sa=reinterpret_cast<sockaddr *>(&address);
            const int notified=ifnet_notice_node_absence(net,sa);if(notified)return notified;
            // Explicit AutoPeer owns an initially absent host in this sole-writer
            // experiment. nd6_lookup's fallback creates no WASCLONED flag, so
            // node-absence can silently leave it; delete only this scoped host.
            // shortcut: concurrent external replacement needs a separate ownership contract.
            const int removed=rtrequest_scoped(RTM_DELETE,sa,nullptr,nullptr,RTF_HOST,nullptr,index);
            return removed==ESRCH?0:removed;
        }
        sockaddr_dl link={};link.sdl_len=sizeof(link);link.sdl_family=AF_LINK;
        link.sdl_index=uint16_t(index);link.sdl_type=ifnet_type(net);link.sdl_alen=6;memcpy(LLADDR(&link),key.mac,6);
        // No AirDrop service hint is invented by the bounded node experiment.
        uint8_t service[48]={};
        return ifnet_notice_node_presence_v2(net,reinterpret_cast<sockaddr *>(&address),&link,0,0,0,service);
    }
    int present(void *value,const ItlAwdl::NativePeerKey &key){return notice(value,key,true);}
    int absent(void *value,const ItlAwdl::NativePeerKey &key){return notice(value,key,false);}
};
static ItlAwdl::NativePeerRegistration<OSObject> awdlNativePeer;
static ItlAwdl::NativePeerRegistration<OSObject> awdlDiscoveryPeers[ItlAwdl::NativeDiscoverySession::PeerCapacity];

// The actual two-argument virtual-interface implementation returns bool on this
// kernel; the legacy local declaration says void. Do not infer success from it.
extern "C" bool ItlNativeVirtualLink(void *,IO80211LinkState,unsigned)
    __asm__("__ZN23IO80211VirtualInterface12setLinkStateE16IO80211LinkStatej");
static bool awdlOwnsProbeIPv6(const uint8_t source[16]);
static bool awdlNativeLink(OSObject *owner,bool up){
    auto interface=OSDynamicCast(ItlAwdlInterface,owner);if(!interface)return false;
    auto &port=ItlAwdl::gDataPort;
    if(up){
        const uint64_t current=interface->getSelfMacAddr();
        const unsigned rejected=gItlAwdlControlProbe?1:
            (port.owner!=owner||!port.nativeSession)?2:
            memcmp(&current,port.registeredLocal,6)?3:
            !awdlOwnsProbeIPv6(port.nativeSource)?4:
            !interface->probeHasBoundBSD()?5:
            !interface->probeHasPeerManager()?6:
            !ItlAwdl::nativeDataQueuesReady(port.txQueue,port.rxQueue)?7:0;
        interface->setProperty("AWDLNativeLinkReject",rejected,32);
        if(rejected)return false;
        auto ifnet=interface->probeCopyBoundBSD();ItlNativeBsdOps ops;
        const bool prepared=ifnet&&awdlNativeBsdBinding.start(ops,owner,ifnet);
        if(ifnet)ItlIfnetRelease(ifnet);
        interface->setProperty("AWDLNativeBSDPrepared",prepared);
        if(!prepared){interface->setProperty("AWDLNativeLinkReject",9,32);return false;}
        const auto enabled=interface->IOSkywalkEthernetInterface::enable(0);
        interface->setProperty("AWDLNativeEnableResult",uint32_t(enabled),32);
        if(enabled!=kIOReturnSuccess){interface->setProperty("AWDLNativeLinkReject",8,32);return false;}
    }
    const auto runningStatus=interface->setRunningState(up);
    const bool running=runningStatus==0;
    const bool linked=ItlNativeVirtualLink(interface,up?kIO80211NetworkLinkUp:kIO80211NetworkLinkDown,0);
    interface->setProperty("AWDLNativeRunningResult",uint32_t(runningStatus),32);
    interface->setProperty("AWDLNativeVirtualLinkResult",linked);
    bool flagged=true;
    if(up){
        ItlNativeBsdOps ops;
        flagged=running&&linked&&port.nativeSession&&!port.linkDownPending&&awdlNativeBsdBinding.publishRunning(ops);
        interface->setProperty("AWDLNativePeerRequested",port.nativeAutoPeer);
        if(flagged&&port.nativeAutoPeer){
            ItlAwdl::NativePeerKey key;memcpy(key.mac,port.nativePeerMAC,6);memcpy(key.ip,port.nativePeerIP,16);
            key.generation=port.nativePeerGeneration;key.epoch=port.nativePeerEpoch;
            ItlNativePeerOps peerOps;
            flagged=awdlNativePeer.start(peerOps,owner,awdlNativeBsdBinding.interface,key)&&port.nativeSession&&!port.linkDownPending;
            interface->setProperty("AWDLNativePeerPresentResult",uint32_t(awdlNativePeer.presentResult),32);
            interface->setProperty("AWDLNativePeerGeneration",key.generation,32);
            interface->setProperty("AWDLNativePeerEpoch",key.epoch,32);
        }
        interface->setProperty("AWDLNativePeerReady",port.nativeAutoPeer&&awdlNativePeer.active&&flagged);
        interface->setProperty("AWDLNativeBSDRunningPublished",flagged);
        if(!flagged)interface->setProperty("AWDLNativeLinkReject",10,32);
    }
    bool finalized=true;
    if(!up){
        ItlNativeBsdOps ops;
        ItlNativePeerOps peerOps;
        finalized=awdlNativePeer.idle()||(awdlNativePeer.owner==owner&&awdlNativePeer.stop(peerOps));
        interface->setProperty("AWDLNativePeerReady",false);
        interface->setProperty("AWDLNativePeerWithdrawalResult",uint32_t(awdlNativePeer.absentResult),32);
        interface->setProperty("AWDLNativePeerNoticeIdle",awdlNativePeer.idle());
        // Activation belongs to this bounded data session even if the native
        // BSD attachment itself was borrowed. Ignore disable's void-like ABI.
        if(finalized&&awdlNativeBsdBinding.owner==owner&&ops.bound(owner)==awdlNativeBsdBinding.interface)
            interface->IOSkywalkNetworkInterface::disable(0);
        finalized=finalized&&(!awdlNativeBsdBinding.owner||awdlNativeBsdBinding.owner==owner)&&awdlNativeBsdBinding.stop(ops);
        interface->setProperty("AWDLNativeBSDFinalized",finalized);
    }
    interface->setProperty("AWDLNativeLinkPublished",up&&running&&linked&&flagged);
    return running&&linked&&flagged&&finalized;
}
static volatile SInt32 awdlQueueCalls = 0;
static unsigned awdlProbeTx(OSObject *owner, IOSkywalkTxSubmissionQueue *queue, IOSkywalkPacket *const *packets, unsigned count, void *)
{
    OSIncrementAtomic(&awdlQueueCalls);
    auto interface=OSDynamicCast(ItlAwdlInterface,owner);
    auto controller=interface?OSDynamicCast(AirportItlwm,interface->getProvider()):nullptr;
    if(!controller)return 0;
    ItlAwdlDataCall call={ItlAwdlDataCall::Tx};call.owner=owner;
    call.tx=reinterpret_cast<OSObject *>(queue);call.packets=packets;call.count=count;
    return controller->awdlDataDispatch(call)==kIOReturnSuccess?call.result:0;
}
static unsigned awdlProbeRx(OSObject *owner, IOSkywalkRxCompletionQueue *queue, IOSkywalkPacket **packets, unsigned count, void *)
{
    OSIncrementAtomic(&awdlQueueCalls);
    auto interface=OSDynamicCast(ItlAwdlInterface,owner);
    auto controller=interface?OSDynamicCast(AirportItlwm,interface->getProvider()):nullptr;
    if(!controller)return 0;
    ItlAwdlDataCall call={ItlAwdlDataCall::Rx};call.owner=owner;
    call.rx=reinterpret_cast<OSObject *>(queue);call.packets=packets;call.count=count;
    return controller->awdlDataDispatch(call)==kIOReturnSuccess?call.result:0;
}

IOReturn AirportItlwm::awdlDataDispatch(ItlAwdlDataCall &call)
{
    call.result=0;
    auto gate=_fCommandGate;auto loop=_fWorkloop;
    if(!gate||!loop)return kIOReturnNotReady;
    gate->retain();loop->retain();
    const IOReturn status=gate->runAction([](OSObject *object,void *arg,void*,void*,void*)->IOReturn{
        auto self=static_cast<AirportItlwm *>(object);auto &call=*static_cast<ItlAwdlDataCall *>(arg);
        auto &port=ItlAwdl::gDataPort;
        if(call.kind==ItlAwdlDataCall::Bind){
            auto interface=OSDynamicCast(ItlAwdlInterface,call.owner);
            if(!interface||interface->getProvider()!=self||!port.bind(call.owner,call.pool,call.tx,call.rx,call.local))return kIOReturnNotReady;
            port.linkAction=call.linkAction;port.bsdInputAction=call.bsdInputAction;
            port.controlGuard=[](OSObject *owner,bool start)->bool{
                auto interface=OSDynamicCast(ItlAwdlInterface,owner);
                return interface&&interface->probeNativeControlPermits(start);
            };return kIOReturnSuccess;
        }
        if(call.kind==ItlAwdlDataCall::Stop){
            if(port.owner!=call.owner)return kIOReturnSuccess;
            auto interface=OSDynamicCast(ItlAwdlInterface,call.owner);
            if(interface)interface->probeNativeControlClose();
            port.stop(false,ECANCELED);
            auto hal=OSDynamicCast(ItlIwm,self->fHalService);
            if(hal){if(hal->awdlNativeSearchReserved())hal->awdlNativeSearchRequest(true);hal->awdlIbssNativeQuiesce();hal->awdlIbssNativeQueuesStop();}
            if(port.unbind())return kIOReturnSuccess;
            port.releasePending=true;return kIOReturnBusy;
        }
        if(call.kind==ItlAwdlDataCall::Quiesce){
            if(port.owner!=call.owner)return kIOReturnNotReady;
            port.stop(false,ECANCELED);
            // The native virtual interface may quiesce independently. The
            // explicit host lease remains the search owner until Stop.
            auto hal=OSDynamicCast(ItlIwm,self->fHalService);if(hal)hal->awdlIbssNativeQuiesce();
            return kIOReturnSuccess;
        }
        if(call.kind==ItlAwdlDataCall::Maintenance){
            call.owner=nullptr;
            if(!port.nativeSession&&port.linkDownPending)port.publishNativeLink(false);
            if(!port.nativeSession){auto hal=OSDynamicCast(ItlIwm,self->fHalService);if(hal)hal->awdlIbssNativeQueuesStop();}
            // Transfer a temporary reference across the gate; native stop runs outside it.
            if(port.owner&&port.releasePending){port.owner->retain();call.owner=port.owner;}
            return kIOReturnSuccess;
        }
        if(call.kind==ItlAwdlDataCall::NativeSyncQuery||call.kind==ItlAwdlDataCall::NativeSyncDisable||call.kind==ItlAwdlDataCall::NativeSyncEnable){
            if(!call.owner||port.owner!=call.owner||self->fAWDLInterface!=call.owner)return kIOReturnNotReady;
            auto hal=OSDynamicCast(ItlIwm,self->fHalService);
            if(call.kind==ItlAwdlDataCall::NativeSyncEnable){
                if(!hal||port.releasePending||!self->awdlControlPermitsEnable())return kIOReturnNotReady;
                // Preserve an existing diagnostic native data session; never substitute a search for it.
                if(port.nativeSession||port.tx.enabled){
                    const auto result=port.nativeSession&&port.linkUp&&!port.linkBusy&&!port.linkDownPending&&
                        hal->awdlIbssNativeLeaseReason()==0?kIOReturnSuccess:kIOReturnNotReady;
                    if(result==kIOReturnSuccess)hal->awdlNativeSyncRequested=true;
                    return result;
                }
                const auto result=hal->awdlNativeSearchEnable();
                if(result==kIOReturnSuccess)hal->awdlNativeSyncRequested=true;
                return result;
            }
            if(call.kind==ItlAwdlDataCall::NativeSyncDisable){
                if(port.tx.enabled&&!port.nativeSession)return kIOReturnUnsupported;
                if(hal)hal->awdlNativeSyncRequested=false;
                // Native protocol disable retires only the native diagnostic
                // session. The explicit host lease remains the data owner.
                if(port.nativeSession||(hal&&hal->awdlIbssNativePending())){
                    if(!hal)return kIOReturnNotReady;
                    port.stop(false,ECANCELED);hal->awdlIbssNativeQuiesce();hal->awdlIbssNativeQueuesStop();
                }
                return kIOReturnSuccess;
            }
            call.result=hal&&!port.releasePending&&((port.linkUp&&!port.linkBusy&&!port.linkDownPending&&
                hal->awdlIbssNativeLeaseReason()==0)||(hal->awdlNativeSyncRequested&&hal->awdlNativeSearchEnabled()&&
                    (!hal->awdlNativeControlTx.nativeRoleAccepted||hal->awdlNativeSyncRoleReady())));
            return kIOReturnSuccess;
        }
        if(port.owner!=call.owner||self->fAWDLInterface!=call.owner)return kIOReturnNotReady;
        auto hal=OSDynamicCast(ItlIwm,self->fHalService);ItlAwdl::DataContext context;
        if(call.kind==ItlAwdlDataCall::BsdTx){
            if(!hal)return kIOReturnNotReady;
            call.result=unsigned(hal->awdlNativeDiscovery.used&&!port.nativeSession?
                hal->awdlNativeDiscoveryOffer(call.owner,static_cast<const uint8_t *>(call.packets),call.count):
                hal->awdlIbssNativeOfferBsd(static_cast<const uint8_t *>(call.packets),call.count));
            return kIOReturnSuccess;
        }
        if(call.kind==ItlAwdlDataCall::NativeReady&&hal&&hal->awdlNativeDiscovery.used&&
           !memcmp(call.local,port.registeredLocal,6)&&hal->awdlNativeDiscoveryReady())return kIOReturnSuccess;
        if(!port.tx.enabled||!hal||!hal->awdlDataContext(context))return kIOReturnNotReady;
        if(context.nowNs>=port.tx.deadlineNs||context.generation!=port.tx.generation){port.stop(false,ECANCELED);return kIOReturnNotReady;}
        if(call.kind==ItlAwdlDataCall::NativeReady)
            return context.radioKind==ItlAwdl::DataRadioKind::ExclusiveIbss&&
                !memcmp(call.local,port.registeredLocal,6)&&!memcmp(port.tx.local,port.registeredLocal,6)&&port.ready(context)&&
                ItlAwdl::nativeDataQueuesReady(port.txQueue,port.rxQueue)?kIOReturnSuccess:kIOReturnNotReady;
        if(call.kind==ItlAwdlDataCall::Tx){
            call.result=port.offer(call.owner,call.tx,static_cast<OSObject *const *>(call.packets),call.count,context);
            if(call.result)hal->awdlIbssNativeWake();
        }else if(call.kind==ItlAwdlDataCall::Rx)
            call.result=port.poll(call.owner,call.rx,static_cast<OSObject **>(const_cast<void *>(call.packets)),call.count,context);
        return kIOReturnSuccess;
    },&call);
    loop->release();gate->release();return status;
}

IOReturn ItlAwdlInterface::enable(unsigned int options)
{
    static volatile SInt32 calls = 0;
    setProperty("AWDLTraceEnableCalls", static_cast<uint32_t>(OSIncrementAtomic(&calls)) + 1, 32);
    setProperty("AWDLTraceEnableOptions", options, 32);
    // Do not use Virtual/Skywalk enable wrappers: they discard Ethernet's
    // result. Preserve the real readiness/enable error without publishing UP.
    const IOReturn result = IOSkywalkEthernetInterface::enable(options);
    setProperty("AWDLTraceEnableResult", static_cast<uint32_t>(result), 32);
    setProperty("AWDLProbeEnableRejected", result != kIOReturnSuccess);
    return result;
}

// Opening an IOService is not a BSD UP request and need not be its last client.
// Keep instrumentation, but do not enable/disable the adapter from open/close.
bool ItlAwdlInterface::open(IOService *client, IOOptionBits options, void *arg)
{
    static volatile SInt32 calls = 0;
    setProperty("AWDLTraceOpenCalls", static_cast<uint32_t>(OSIncrementAtomic(&calls)) + 1, 32);
    const bool result = IO80211VirtualInterface::open(client, options, arg);
    setProperty("AWDLTraceOpenSuperResult", result);
    return result;
}
void ItlAwdlInterface::close(IOService *client, IOOptionBits options)
{
    static volatile SInt32 calls = 0;
    setProperty("AWDLTraceCloseCalls", static_cast<uint32_t>(OSIncrementAtomic(&calls)) + 1, 32);
    IO80211VirtualInterface::close(client, options);
}

SInt32 ItlAwdlInterface::setInterfaceEnable(bool enabled)
{
    static volatile SInt32 calls = 0;
    setProperty("AWDLTraceBSDEnableCalls", static_cast<uint32_t>(OSIncrementAtomic(&calls)) + 1, 32);
    setProperty("AWDLTraceBSDEnableRequested", enabled);
    // This BSD-facing entry point otherwise starts native virtual-interface
    // timers and reports success even without an implemented data path.
    auto controller=OSDynamicCast(AirportItlwm,getProvider());
    ItlAwdlDataCall call={enabled?ItlAwdlDataCall::NativeReady:ItlAwdlDataCall::Quiesce};call.owner=this;
    if(enabled){const uint64_t address=getSelfMacAddr();memcpy(call.local,&address,6);}
    const bool ready=controller&&controller->awdlDataDispatch(call)==kIOReturnSuccess;
    setProperty("AWDLProbeBSDEnableRejected",enabled&&!ready);
    return enabled?(ready?0:ENOTSUP):0;
}

IOReturn ItlAwdlInterface::prepareBSDInterface(struct __ifnet *interface, unsigned int options)
{
    const IOReturn result = IO80211VirtualInterface::prepareBSDInterface(interface, options);
    setProperty("AWDLProbeBSDPrepareResult", static_cast<unsigned long long>(static_cast<uint32_t>(result)), 32);
    setProperty("AWDLProbeBSDPresent", getBSDInterface() != nullptr);
    IOService *provider = getProvider();
    if (provider) {
        provider->setProperty("AWDLProbeBSDPrepareResult", static_cast<unsigned long long>(static_cast<uint32_t>(result)), 32);
        provider->setProperty("AWDLProbeBSDPresent", getBSDInterface() != nullptr);
        provider->setProperty("AWDLVifInit", result == kIOReturnSuccess ? "bsd-prepared-v34" : "bsd-prepare-failed-v34");
    }
    // Preparation is only one stage; success still requires an attached BSD
    // client and an actual ifconfig entry after the native nexus completes.
    return result;
}

void ItlAwdlInterface::stop(IOService *provider)
{
    // Called by IOKit after the BSD client's didTerminate/finalize closes it.
    // Native stop then tears down the peer, logical link and retained pools.
    stopProbe(provider);
    setProperty("AWDLProbeStopped", probeStopDone);
}

bool ItlAwdlInterface::probeRegisterResources(IOService *provider, const ether_addr &address)
{
    probeBSDLock = IOLockAlloc();
    if (!probeBSDLock)
        return false;
    // Native factory owns allocation and initialization. Pool type 0 creates
    // IOSkywalkNetworkPacket objects; no DMA mapping or hardware submission.
    const ItlPoolOptions options = {128, 128, 2048, 1, 4096, 0, 0};
    OSObject *pool = ItlNativePool("itl-awdl-probe34", this, 0, &options);
    provider->setProperty("AWDLProbePoolCreated", pool != nullptr);
    OSObject *tx = nullptr;
    OSObject *rx = nullptr;
    IOReturn result = kIOReturnNoMemory;
    bool retained = false;
    bool registered = false;
    if (pool) {
        auto nativePool = reinterpret_cast<IOSkywalkPacketBufferPool *>(pool);
        // Factory parameters: ring capacity, queue index, owner, callback,
        // context, options. Neither queue is attached to an active workloop.
        tx = ItlNativeTxQueue(nativePool, 64, 0, this, awdlProbeTx, nullptr, 0);
        rx = ItlNativeRxQueue(nativePool, 64, 0, this, awdlProbeRx, nullptr, 0);
    }
    provider->setProperty("AWDLProbeTxQueueCreated", tx != nullptr);
    provider->setProperty("AWDLProbeRxQueueCreated", rx != nullptr);
    if (pool && tx && rx) {
        const unsigned poolRefs = pool->getRetainCount();
        const unsigned txRefs = tx->getRetainCount();
        const unsigned rxRefs = rx->getRetainCount();
        IOSkywalkEthernetInterface::RegistrationInfo info = {};
        if (initRegistrationInfo(&info, 1, sizeof(info))) {
            // Offsets verified against native init/copyRegistrationInfo and
            // NetworkInterface accessors. No native object fields are written.
            const char *prefix = "awdl";
            const uint32_t unit = 0;
            // initRegistrationInfo defaults to Ethernet media. This is a Wi-Fi
            // virtual interface; report its type without claiming a live link.
            const uint32_t media = kItlDarwinWifiMedia;
            memcpy(info.bytes + 0x24, &media, sizeof(media));
            memcpy(info.bytes + 0x28, &media, sizeof(media));
            memcpy(info.bytes + 0x30, &prefix, sizeof(prefix));
            memcpy(info.bytes + 0x38, &unit, sizeof(unit));
            memcpy(info.bytes + 0x108, &address, sizeof(address));
            IOSkywalkPacketQueue *queues[] = {
                reinterpret_cast<IOSkywalkPacketQueue *>(tx),
                reinterpret_cast<IOSkywalkPacketQueue *>(rx)
            };
            auto nativePool = reinterpret_cast<IOSkywalkPacketBufferPool *>(pool);
            result = registerEthernetInterface(&info, queues, 2, nativePool, nativePool, 0);
            registered = result == kIOReturnSuccess;
        } else {
            result = kIOReturnBadArgument;
        }
        // Native registration must own the resources before factory references
        // are dropped. Probe33 already verified the inverse stop/release path.
        retained = registered && pool->getRetainCount() > poolRefs &&
            tx->getRetainCount() > txRefs && rx->getRetainCount() > rxRefs;
    }
    if(retained){
        auto controller=OSDynamicCast(AirportItlwm,provider);
        ItlAwdlDataCall call={ItlAwdlDataCall::Bind};call.owner=this;call.pool=pool;call.tx=tx;call.rx=rx;
        memcpy(call.local,&address,6);
        call.linkAction=awdlNativeLink;
        call.bsdInputAction=awdlBSDDataInput;
        retained=controller&&controller->awdlDataDispatch(call)==kIOReturnSuccess;
        provider->setProperty("AWDLDataPortBound",retained);
    }
    if (!retained)
        stopProbe(provider);
    provider->setProperty("AWDLProbeResourceRegisterResult", static_cast<unsigned long long>(static_cast<uint32_t>(result)), 32);
    provider->setProperty("AWDLProbeResourcesRegistered", registered);
    provider->setProperty("AWDLProbeResourcesRetained", retained);
    provider->setProperty("AWDLProbeQueueCalls", static_cast<unsigned long long>(awdlQueueCalls), 32);
    OSSafeReleaseNULL(rx);
    OSSafeReleaseNULL(tx);
    OSSafeReleaseNULL(pool);
    return registered && retained;
}

// Native Skywalk stop assumes a peer manager exists. Before that milestone,
// start has only created controller/workqueue references and optional logs.
// Native free owns the references; this branch closes the log resources.
void ItlAwdlInterface::stopProbe(IOService *provider)
{
    const bool controlDrained=probeNativeControlClose();
    setProperty("AWDLControlStopPending",!controlDrained);
    auto controller=OSDynamicCast(AirportItlwm,provider);
    if(controller){
        ItlAwdlDataCall call={ItlAwdlDataCall::Stop};call.owner=this;
        if(controller->awdlDataDispatch(call)==kIOReturnBusy){
            setProperty("AWDLDataStopPendingDMA",true);return;
        }
    }

    if(!controlDrained)return; // Stop never tears native resources out from under a control call.
    if (probeBSDLock)
        IOLockLock(probeBSDLock);
    const bool alreadyStopped = probeStopDone;
    probeStopDone = true;
    if (probeBSDLock)
        IOLockUnlock(probeBSDLock);
    if (alreadyStopped)
        return;
    setProperty("AWDLDataStopPendingDMA",false);
    setProperty("AWDLProbeStopped",true);
    probeUnbindBSDControl();
    uint8_t *expansion = nullptr;
    memcpy(&expansion, reinterpret_cast<const uint8_t *>(this) + 0x110, sizeof(expansion));
    OSObject *peer = nullptr;
    if (expansion)
        memcpy(&peer, expansion + 0x28, sizeof(peer));
    if (peer) {
        IO80211VirtualInterface::stop(provider);
        probeBpfFreed = true; // Virtual stop already calls freeBpfResources.
        static_cast<IO80211Controller *>(provider)->setAwdlInterface(nullptr);
    } else if (expansion) {
        CCPipe *pipe = nullptr;
        OSObject *stream = nullptr;
        memcpy(&pipe, expansion + 0x80, sizeof(pipe));
        memcpy(&stream, expansion + 0x78, sizeof(stream));
        if (pipe) {
            pipe->stopPipe();
            pipe->release();
        }
        if (stream)
            stream->release();
        pipe = nullptr;
        stream = nullptr;
        memcpy(expansion + 0x80, &pipe, sizeof(pipe));
        memcpy(expansion + 0x78, &stream, sizeof(stream));
    }
}


struct ItlNativeControlJob {
    AirportItlwm *controller;ItlAwdlInterface *interface;ItlIwm *hal;
    IOCommandGate *gate;IOWorkLoop *loop;uint64_t epoch;
    bool firstUp=false,publishedUp=false,executing=false,executingUp=false,quarantined=false,retiring=false,handedOff=false;uint64_t serial=0;IO80211VirtualInterface *stoppedChildReference=nullptr;
};
bool AirportItlwm::awdlControlHasJob(){
    if(!fAwdlControlLock)return false;
    IOLockLock(fAwdlControlLock);const bool pending=fAwdlControlJob!=nullptr;
    IOLockUnlock(fAwdlControlLock);return pending;
}
bool AirportItlwm::awdlControlPermitsEnable(){
    if(!fAwdlControlLock)return false;
    IOLockLock(fAwdlControlLock);
    const bool allowed=!fAwdlControlClosing&&(!fAwdlControlJob||!fAwdlControlJob->quarantined);
    IOLockUnlock(fAwdlControlLock);return allowed;
}
bool AirportItlwm::awdlControlHasRunningJob(){
    if(!fAwdlControlLock)return false;
    IOLockLock(fAwdlControlLock);const bool pending=fAwdlControlJob&&!fAwdlControlJob->quarantined;
    IOLockUnlock(fAwdlControlLock);return pending;
}
void AirportItlwm::awdlControlClose(){
    if(!fAwdlControlLock)return;
    IOLockLock(fAwdlControlLock);fAwdlControlClosing=true;
    if(fAwdlControlJob)fAwdlControlJob->interface->probeNativeControlClose();
    IOLockUnlock(fAwdlControlLock);
}
bool AirportItlwm::willTerminate(IOService *provider,IOOptionBits options){
    awdlControlClose();return super::willTerminate(provider,options);
}
bool AirportItlwm::didTerminate(IOService *provider,IOOptionBits options,bool *defer){
    awdlControlClose();
    if(fAwdlControlLock){
        IOLockLock(fAwdlControlLock);
        if(fAwdlControlJob&&!fAwdlControlJob->quarantined){
            if(!fAwdlControlDeferredProvider){
                provider->retain();fAwdlControlDeferredProvider=provider;fAwdlControlDeferredOptions=options;
            }
            // IOService owns scheduling stop. Do not wait here or call stop later ourselves.
            *defer=true;IOLockUnlock(fAwdlControlLock);return true;
        }
        IOLockUnlock(fAwdlControlLock);
    }
    return super::didTerminate(provider,options,defer);
}
bool ItlNativeControlRoleAllowed(OSObject *root,OSObject *owner,uint64_t epoch,bool entering){
    auto controller=OSDynamicCast(AirportItlwm,root);
    return controller&&controller->awdlControlAllowsRole(owner,epoch,entering);
}
// SYNC_PARAMS may arrive from wifip2pd after the initial virtual link call returns.
bool AirportItlwm::awdlControlAllowsWarmParams(OSObject *owner,uint64_t epoch){
    if(!_fWorkloop||!_fWorkloop->inGate()||!awdlControlAllowsRole(owner,epoch,false))return false;
    auto interface=OSDynamicCast(ItlAwdlInterface,owner);
    if(!interface||interface->isInactive()||interface->probeStopDone)return false;
    IOLockLock(fAwdlControlLock);
    auto job=fAwdlControlJob;
    const bool current=job && job->interface==interface && job->epoch==epoch && job->serial==1 &&
        !job->retiring && !job->quarantined && !fAwdlControlClosing;
    const bool inFirstUp=current && job->executing && job->firstUp;
    const bool published=current && !job->executing && job->publishedUp;
    IOLockUnlock(fAwdlControlLock);
    if(inFirstUp)return true;
    if(!published||interface->linkState()!=kIO80211NetworkLinkUp)return false;
    auto manager=interface->probeCopyPeerManager();
    const bool ready=manager && !strcmp(manager->getMetaClass()->getClassName(),"IO80211AWDLPeerManager") &&
        ItlNativePmVirtualInterface(manager)==interface && ItlNativeAwdlConfigured(manager) &&
        !ItlNativeAwdlConfigureInProgress(manager);
    if(manager)manager->release();
    return ready;
}
bool ItlNativeWarmParamsAllowed(OSObject *root,OSObject *owner,uint64_t epoch){
    auto controller=OSDynamicCast(AirportItlwm,root);
    return controller && controller->awdlControlAllowsWarmParams(owner,epoch);
}
// Role state is admitted while the retained transaction is executing or after
// its confirmed link-up has been published. Native election reconfiguration
// arrives after the first up call and must still use the same pinned job.
bool AirportItlwm::awdlControlAllowsRole(OSObject *owner,uint64_t epoch,bool entering){
    if(!owner||!epoch||!fAwdlControlLock)return false;
    IOLockLock(fAwdlControlLock);auto job=fAwdlControlJob;
    const bool allowed=!fAwdlControlClosing&&job&&job->epoch==epoch&&job->interface==owner&&
        fAWDLInterface==owner&&job->hal==OSDynamicCast(ItlIwm,fHalService)&&job->serial&&
        (job->executing||job->publishedUp)&&
        !job->retiring&&!job->quarantined;
    IOLockUnlock(fAwdlControlLock);return allowed;
}
bool AirportItlwm::awdlControlPrepare(uint64_t epoch){
    if(!epoch||!fAwdlControlLock)return false;
    auto job=static_cast<ItlNativeControlJob*>(IOMalloc(sizeof(ItlNativeControlJob)));if(!job)return false;
    IOLockLock(fAwdlControlLock);
    bool accepted=false;
    // Close and admission share this lock: releaseAll cannot remove these resources
    // between qualification and committing the retained job.
    if(!fAwdlControlClosing&&!fAwdlControlJob&&_fCommandGate&&_fWorkloop&&_fWorkloop->inGate()){
        auto interface=OSDynamicCast(ItlAwdlInterface,fAWDLInterface);
        auto hal=OSDynamicCast(ItlIwm,fHalService);
        if(interface&&hal&&hal->awdlNativeSearch.matches(epoch)&&hal->awdlNativeSearchEnabled()&&
           interface->probeNativeControlPin(epoch)){
            *job={this,interface,hal,_fCommandGate,_fWorkloop,epoch};
            retain();interface->retain();hal->retain();job->gate->retain();job->loop->retain();
            fAwdlControlJob=job;accepted=true;
        }
    }
    IOLockUnlock(fAwdlControlLock);
    if(!accepted)IOFree(job,sizeof(*job));return accepted;
}
uint64_t AirportItlwm::awdlControlNativeBegin(uint64_t epoch,bool up){
    if(!fAwdlControlLock)return 0;
    IOLockLock(fAwdlControlLock);auto current=fAwdlControlJob;
    auto gate=current&&current->epoch==epoch?current->gate:nullptr;if(gate)gate->retain();
    IOLockUnlock(fAwdlControlLock);if(!gate)return 0;
    struct Request{uint64_t epoch,serial=0;bool up;}request{epoch,0,up};
    gate->runAction([](OSObject *object,void *arg,void*,void*,void*)->IOReturn{
        auto self=static_cast<AirportItlwm*>(object);auto &request=*static_cast<Request*>(arg);
        IOLockLock(self->fAwdlControlLock);auto job=self->fAwdlControlJob;
        if(job&&job->epoch==request.epoch&&!job->quarantined&&!job->retiring&&!job->executing&&
           job->serial!=UINT64_MAX&&(!request.up||(!self->fAwdlControlClosing&&
           job->hal->awdlNativeSearch.matches(request.epoch)&&job->hal->awdlNativeSearchEnabled()))){
            job->firstUp=request.up && job->serial==0;
            if(job->firstUp)job->publishedUp=false;
            job->executingUp=request.up;
            job->executing=true;request.serial=++job->serial;
        }
        IOLockUnlock(self->fAwdlControlLock);return kIOReturnSuccess;
    },&request);
    gate->release();return request.serial;
}
bool AirportItlwm::awdlControlNativeEnd(uint64_t epoch,uint64_t serial,bool linked){
    if(!fAwdlControlLock)return false;
    IOLockLock(fAwdlControlLock);auto job=fAwdlControlJob;
    const bool valid=job&&job->epoch==epoch&&serial&&job->serial==serial&&job->executing;
    if(valid){
        // Discovery and peer publication issue later UP transactions. They must
        // not erase the confirmed first link-up that authorizes native role work.
        if(job->firstUp)job->publishedUp=linked;
        else if(!job->executingUp)job->publishedUp=false;
        job->firstUp=false;
        job->executingUp=false;
        job->executing=false;
    }
    IOLockUnlock(fAwdlControlLock);return valid;
}
bool AirportItlwm::awdlControlComplete(uint64_t epoch,bool confirmedDown){
    if(!fAwdlControlLock)return false;
    IOLockLock(fAwdlControlLock);auto current=fAwdlControlJob;
    auto gate=current&&current->epoch==epoch?current->gate:nullptr;
    auto loop=gate?current->loop:nullptr;if(gate){gate->retain();loop->retain();}
    IOLockUnlock(fAwdlControlLock);if(!gate)return false;
    if(loop->inGate()||loop->onThread()){loop->release();gate->release();return false;}
    struct Completion{uint64_t epoch;bool down;ItlNativeControlJob *job=nullptr;IOService *provider=nullptr;IOOptionBits options=0;}done{epoch,confirmedDown};
    const auto result=gate->runAction([](OSObject *object,void *arg,void*,void*,void*)->IOReturn{
        auto self=static_cast<AirportItlwm*>(object);auto &done=*static_cast<Completion*>(arg);
        IOLockLock(self->fAwdlControlLock);auto job=self->fAwdlControlJob;
        if(!job||job->epoch!=done.epoch||job->executing||job->retiring||job->quarantined){IOLockUnlock(self->fAwdlControlLock);return kIOReturnNotReady;}
        if(!done.down){
            job->quarantined=true;
            done.provider=self->fAwdlControlDeferredProvider;done.options=self->fAwdlControlDeferredOptions;
            self->fAwdlControlDeferredProvider=nullptr;
            IOLockUnlock(self->fAwdlControlLock);return kIOReturnNotReady;
        }
        job->retiring=true;IOLockUnlock(self->fAwdlControlLock);
        if(!job->interface->probeNativeControlUnpin(done.epoch,true)){
            IOLockLock(self->fAwdlControlLock);job->retiring=false;IOLockUnlock(self->fAwdlControlLock);
            return kIOReturnNotReady;
        }
        IOLockLock(self->fAwdlControlLock);
        done.job=job;self->fAwdlControlJob=nullptr;
        done.provider=self->fAwdlControlDeferredProvider;done.options=self->fAwdlControlDeferredOptions;
        self->fAwdlControlDeferredProvider=nullptr;
        IOLockUnlock(self->fAwdlControlLock);return kIOReturnSuccess;
    },&done);
    loop->release();gate->release();
    if(done.provider){
        bool defer=false;super::didTerminate(done.provider,done.options,&defer);done.provider->release();
    }
    if(result!=kIOReturnSuccess||!done.job)return false;
    auto job=done.job;job->interface->release();job->hal->release();job->gate->release();job->loop->release();
    auto controller=job->controller;IOFree(job,sizeof(*job));controller->release();return true;
}

bool AirportItlwm::awdlControlHandoffStop(IO80211VirtualInterface *interface){
    if(!awdlControlHasJob())return true;
    if(!fAwdlControlPhysicalStop||!_fCommandGate)return false;
    return _fCommandGate->runAction([](OSObject *object,void *arg,void*,void*,void*)->IOReturn{
        auto self=static_cast<AirportItlwm*>(object);
        IOLockLock(self->fAwdlControlLock);auto job=self->fAwdlControlJob;
        const bool valid=job&&job->quarantined&&!job->executing&&!job->retiring&&
            self->fAwdlControlClosing&&job->interface==arg;
        if(valid)job->retiring=true;
        IOLockUnlock(self->fAwdlControlLock);if(!valid)return kIOReturnNotReady;
        const bool handed=job->interface->probeNativeControlStopHandoff(job->epoch);
        IOLockLock(self->fAwdlControlLock);job->handedOff=handed;
        if(!handed)job->retiring=false;
        IOLockUnlock(self->fAwdlControlLock);return handed?kIOReturnSuccess:kIOReturnNotReady;
    },interface)==kIOReturnSuccess;
}
bool AirportItlwm::awdlControlRetireStop(){
    if(!awdlControlHasJob())return true;
    ItlNativeControlJob *retired=nullptr;
    const auto result=_fCommandGate->runAction([](OSObject *object,void *arg,void*,void*,void*)->IOReturn{
        auto self=static_cast<AirportItlwm*>(object);IOLockLock(self->fAwdlControlLock);auto job=self->fAwdlControlJob;
        const bool valid=job&&job->quarantined&&job->handedOff&&!job->executing&&
            job->interface->probeStopDone&&!self->fAwdlControlDeferredProvider;
        if(valid){*static_cast<ItlNativeControlJob**>(arg)=job;self->fAwdlControlJob=nullptr;}
        IOLockUnlock(self->fAwdlControlLock);return valid?kIOReturnSuccess:kIOReturnNotReady;
    },&retired);
    if(result!=kIOReturnSuccess||!retired)return false;
    retired->interface->release();if(retired->stoppedChildReference)retired->stoppedChildReference->release();
    retired->hal->release();retired->gate->release();retired->loop->release();
    auto controller=retired->controller;IOFree(retired,sizeof(*retired));controller->release();return true;
}

// The diagnostic callout owns the adapter; no global stale owner survives final stop.
class ItlNativeControlCallout;
struct ItlNativeControlOps {
    using Owner=ItlAwdlInterface;
    using Adapter=ItlAwdl::NativeControlAdapter<ItlNativeControlOps>;
    using Sample=Adapter::Sample;
    ItlNativeControlCallout *job=nullptr;
    bool inGate();template<class F>void withGate(F action);
    Sample sample(Owner *owner);bool pin(Owner *owner);void unpin(Owner *owner);
    bool publish(Owner *owner);bool withdraw(Owner *owner);
};
class ItlNativeControlCallout:public OSObject {
    OSDeclareDefaultStructors(ItlNativeControlCallout)
public:
    AirportItlwm *controller=nullptr;IOCommandGate *gate=nullptr;IOWorkLoop *loop=nullptr;
    ItlAwdlInterface *interface=nullptr;thread_call_t call=nullptr;uint64_t epoch=0;
    void *controlBsd=nullptr;
    bool completionRequested=false,gateFault=false;unsigned upCalls=0,downCalls=0;
    OSObject *before=nullptr,*during=nullptr,*after=nullptr;
    bool discoveryAttempted=false,discoveryBound=false,discoveryFailed=false;
    void discoveryPump();
    void trace(unsigned state,bool retired);
    bool init() override {return OSObject::init();}
    bool prepareControlBsd(){
        if(controlBsd)return true;
        if(!interface)return false;
        auto net=interface->probeCopyBoundBSD();
        if(!net)return false;
        const auto result=interface->IOSkywalkNetworkInterface::prepareBSDInterface(net,0);
        interface->setProperty("AWDLNativeActiveSocketsBSDPrepared",result==kIOReturnSuccess);
        if(result!=kIOReturnSuccess){ItlIfnetRelease(net);return false;}
        controlBsd=net;
        return true;
    }
    void dropControlBsd(){
        if(!controlBsd)return;
        const bool borrowed=awdlNativeBsdBinding.owner==interface&&awdlNativeBsdBinding.interface==controlBsd;
        if(interface&&!borrowed)interface->IOSkywalkNetworkInterface::finalizeBSDInterface(
            static_cast<ifnet_t>(controlBsd),0);
        ItlIfnetRelease(static_cast<ifnet_t>(controlBsd));controlBsd=nullptr;
    }
    ItlNativeControlOps ops;ItlNativeControlOps::Adapter adapter;
    static void run(void *argument,void *);
    void free() override {
        if(call)thread_call_free(call);
        dropControlBsd();
        OSSafeReleaseNULL(before);OSSafeReleaseNULL(during);OSSafeReleaseNULL(after);
        OSSafeReleaseNULL(gate);OSSafeReleaseNULL(loop);OSSafeReleaseNULL(controller);
        OSObject::free();
    }
};
OSDefineMetaClassAndStructors(ItlNativeControlCallout,OSObject)
void ItlNativeControlCallout::trace(unsigned state,bool retired){
    auto d=OSDictionary::withCapacity(10);if(!d)return;
    const char *keys[]={"Version","Epoch","State","UpCalls","DownCalls","Retired","GateFault"};
    const uint64_t values[]={1,epoch,state,upCalls,downCalls,retired,gateFault};bool ok=true;
    for(unsigned i=0;i<7&&ok;++i){auto n=OSNumber::withNumber(values[i],64);if(!n){ok=false;break;}ok=d->setObject(keys[i],n);n->release();}
    if(before)ok=ok&&d->setObject("Before",before);
    if(during)ok=ok&&d->setObject("During",during);
    if(after)ok=ok&&d->setObject("After",after);
    IOLockLock(controller->fAwdlControlLock);
    if(ok&&controller->fAwdlControlLastTrialEpoch==epoch)controller->setProperty("AWDLNativeControlTrial",d);
    IOLockUnlock(controller->fAwdlControlLock);d->release();
}
bool ItlNativeControlOps::inGate(){return job->loop->inGate();}
template<class F>void ItlNativeControlOps::withGate(F action){
    const auto result=job->gate->runAction([](OSObject*,void *arg,void*,void*,void*)->IOReturn{
        (*static_cast<F*>(arg))();return kIOReturnSuccess;
    },&action);
    if(result!=kIOReturnSuccess)job->gateFault=true;
}
ItlNativeControlOps::Sample ItlNativeControlOps::sample(Owner *owner){
    Sample s;auto root=job->controller;auto hal=OSDynamicCast(ItlIwm,root->fHalService);
    uint64_t absoluteTick=mach_absolute_time();absolutetime_to_nanoseconds(absoluteTick,&s.now);
    if(hal)s.search=hal->awdlNativeSearch;
    uint64_t mask=0;
#define ITL_CONTROL_SAMPLE(bit,value) do{if(value)mask|=uint64_t(1)<<(bit);}while(0)
    ITL_CONTROL_SAMPLE(0,inGate());
    ITL_CONTROL_SAMPLE(1,owner&&owner==job->interface);
    ITL_CONTROL_SAMPLE(2,owner&&root&&root->fAWDLInterface==owner);
    ITL_CONTROL_SAMPLE(3,hal);
    ITL_CONTROL_SAMPLE(4,root&&root->awdlControlPermitsEnable());
    ITL_CONTROL_SAMPLE(5,owner&&!owner->isInactive());
    ITL_CONTROL_SAMPLE(6,owner&&!owner->probeStopDone);
    ITL_CONTROL_SAMPLE(7,owner&&ItlAwdl::gDataPort.owner==owner);
    ITL_CONTROL_SAMPLE(8,!ItlAwdl::gDataPort.releasePending);
    ITL_CONTROL_SAMPLE(9,hal&&hal->awdlNativeSearchEnabled());
    bool managerPresent=false,managerClass=false,managerMatches=false;
    if(owner&&hal&&(mask&0x3fe)==0x3fe){
        auto manager=owner->probeCopyPeerManager();
        managerPresent=manager!=nullptr;
        managerClass=manager&&!strcmp(manager->getMetaClass()->getClassName(),"IO80211AWDLPeerManager");
        managerMatches=managerClass&&ItlNativePmVirtualInterface(manager)==owner;
        if(manager)manager->release();
    }
    ITL_CONTROL_SAMPLE(10,managerPresent);
    ITL_CONTROL_SAMPLE(11,managerClass);
    ITL_CONTROL_SAMPLE(12,managerMatches);
#undef ITL_CONTROL_SAMPLE
    s.qualified=(mask&0x1fff)==0x1fff;
    root->setProperty("AWDLNativeControlSampleMask",mask,64);
    root->setProperty("AWDLNativeControlSampleUpCalls",job->upCalls,32);
    root->setProperty("AWDLNativeControlSampleQualified",s.qualified);
    // Native protocol role changes must not tear down the host-owned data
    // lease. The control worker retires on its own; only search stop or a
    // confirmed native disable closes the underlying link.
    return s;
}
bool ItlNativeControlOps::pin(Owner *owner){
    return inGate()&&owner==job->interface&&owner->linkState()==kIO80211NetworkLinkDown&&
        job->controller->awdlControlPrepare(job->epoch);
}
void ItlNativeControlOps::unpin(Owner *owner){
    // Adapter is gated; final IOService acknowledgement must be outside that gate.
    if(inGate()&&owner==job->interface)job->completionRequested=true;
}
bool ItlNativeControlOps::publish(Owner *owner){
    if(inGate())return false;
    const auto serial=job->controller->awdlControlNativeBegin(job->epoch,true);if(!serial)return false;
    ++job->upCalls;const bool bsdPrepared=job->prepareControlBsd();
    const bool accepted=bsdPrepared&&ItlNativeVirtualLink(owner,kIO80211NetworkLinkUp,1);
    const bool linked=owner->linkState()==kIO80211NetworkLinkUp;
    const bool ended=job->controller->awdlControlNativeEnd(job->epoch,serial,accepted&&linked);
    withGate([&]{
        if(job->controller->awdlPublishNativeControlState()==kIOReturnSuccess){
            job->during=job->controller->getProperty("AWDLNativeControlState");if(job->during)job->during->retain();
        }
    });
    return accepted&&linked&&ended;
}
bool ItlNativeControlOps::withdraw(Owner *owner){
    if(inGate())return false;
    const auto serial=job->controller->awdlControlNativeBegin(job->epoch,false);if(!serial)return false;
    ++job->downCalls;const bool accepted=ItlNativeVirtualLink(owner,kIO80211NetworkLinkDown,1);
    const bool linked=owner->linkState()==kIO80211NetworkLinkDown;
    const bool ended=job->controller->awdlControlNativeEnd(job->epoch,serial);
    if(ended)job->dropControlBsd();
    return accepted&&linked&&ended;
}
// Native BSD calls and network input run outside HAL/PM gates under the same
// lifecycle pin as control publication. The worker alone owns firmware and DMA.
void ItlNativeControlCallout::discoveryPump(){
    if(loop->inGate()){gateFault=true;return;}
    bool ready=false;ops.withGate([&]{
        auto hal=OSDynamicCast(ItlIwm,controller->fHalService);
        const uint64_t address=interface->getSelfMacAddr();
        ready=!discoveryFailed&&!interface->isInactive()&&!interface->probeStopDone&&hal&&!memcmp(&address,hal->awdlNativeSearch.local,6)&&controller->fAWDLInterface==interface&&controller->awdlControlPermitsEnable()&&
            hal->awdlNativeSearch.matches(epoch)&&hal->awdlNativeDiscoveryReady();
        if(ready){
            bool removeDuplicates=false;
            ready=interface->probeDuplicatePolicy(removeDuplicates);
            if(ready){
                hal->awdlNativeDiscovery.removeDuplicatesRx=removeDuplicates;
                interface->setProperty("AWDLDiscoveryRemoveDuplicatesRx",removeDuplicates);
            }
        }
        if(!ready&&hal&&hal->awdlNativeSearch.matches(epoch))hal->awdlNativeDiscoveryStop();
    });
    if(discoveryBound&&!ready){
        // Media availability is distinct from IFF_RUNNING. Withdraw immediately,
        // including when peer teardown must retry while retaining DMA ownership.
        interface->reportLinkStatus(1,0);
        const auto serial=controller->awdlControlNativeBegin(epoch,false);
        if(!serial){gateFault=true;return;}
        ItlNativePeerOps peerOps;
        bool withdrawn=true;int absentResult=0;
        for(auto &peer:awdlDiscoveryPeers){const bool done=peer.idle()||(peer.owner==interface&&peer.stop(peerOps));
            withdrawn&=done;if(peer.absentResult)absentResult=peer.absentResult;}
        interface->setProperty("AWDLDiscoveryPeerWithdrawalResult",uint32_t(absentResult),32);
        interface->setProperty("AWDLDiscoveryPeerNoticeIdle",withdrawn);
        if(!withdrawn){controller->awdlControlNativeEnd(epoch,serial);gateFault=true;return;}
        ops.withGate([&]{auto hal=OSDynamicCast(ItlIwm,controller->fHalService);
            if(hal&&hal->awdlNativeDiscovery.epoch==epoch){for(auto &peer:hal->awdlNativeDiscovery.peers)peer.published=false;hal->awdlNativeDiscovery.updatePeers();hal->awdlNativeDiscoveryPublish();}});
        const bool running=interface->setRunningState(false)==0;
        ItlNativeBsdOps bsdOps;
        if(awdlNativeBsdBinding.owner==interface&&bsdOps.bound(interface)==awdlNativeBsdBinding.interface)
            interface->IOSkywalkNetworkInterface::disable(0);
        const bool stopped=awdlNativeBsdBinding.owner==interface&&awdlNativeBsdBinding.stop(bsdOps);
        const bool ended=controller->awdlControlNativeEnd(epoch,serial);
        interface->setProperty("AWDLDiscoveryBSDRunning",false);
        interface->setProperty("AWDLDiscoveryBSDFinalized",running&&stopped&&ended);
        if(!running||!stopped||!ended){gateFault=true;return;}
        discoveryBound=false;
    }
    if(ready&&!discoveryAttempted){
        discoveryAttempted=true;
        const auto serial=controller->awdlControlNativeBegin(epoch,true);
        if(!serial){gateFault=true;return;}
        auto net=interface->probeCopyBoundBSD();ItlNativeBsdOps bsdOps;
        bool ok=net&&!bsdOps.running(net)&&awdlNativeBsdBinding.idle()&&awdlNativeBsdBinding.start(bsdOps,interface,net);
        discoveryBound=awdlNativeBsdBinding.owner==interface;
        if(net)ItlIfnetRelease(net);
        if(ok)ops.withGate([&]{
            auto hal=OSDynamicCast(ItlIwm,controller->fHalService);uint64_t tick=mach_absolute_time(),now=0;absolutetime_to_nanoseconds(tick,&now);
            ok=hal&&hal->awdlNativeDiscoveryReady()&&hal->awdlNativeDiscovery.begin(
                reinterpret_cast<uintptr_t>(interface),epoch,uint32_t(hal->com.sc_generation),hal->awdlNativeSearch.local,now,hal->awdlNativeSearch.deadlineNs,hal->awdlNativeSearch.receiveValidation());
        });
        if(ok){const auto result=interface->IOSkywalkEthernetInterface::enable(0);
            interface->setProperty("AWDLDiscoveryEnableResult",uint32_t(result),32);ok=result==kIOReturnSuccess;}

        if(ok){const auto result=interface->setRunningState(true);interface->setProperty("AWDLDiscoveryRunningResult",uint32_t(result),32);ok=result==0;}
        if(ok)ops.withGate([&]{
            auto hal=OSDynamicCast(ItlIwm,controller->fHalService);
            ok=hal&&!interface->isInactive()&&!interface->probeStopDone&&controller->awdlControlPermitsEnable()&&hal->awdlNativeDiscoveryOpen(interface,epoch);
        });
        if(ok)ok=awdlNativeBsdBinding.publishRunning(bsdOps)&&
            (ifnet_flags(static_cast<ifnet_t>(awdlNativeBsdBinding.interface))&(IFF_UP|IFF_RUNNING))==(IFF_UP|IFF_RUNNING);
        if(ok)ops.withGate([&]{
            auto hal=OSDynamicCast(ItlIwm,controller->fHalService);
            ok=hal&&!interface->isInactive()&&!interface->probeStopDone&&controller->awdlControlPermitsEnable()&&hal->awdlNativeDiscoveryReady()&&hal->awdlNativeDiscovery.open;
        });
        // Rapport checks SIOCGIFMEDIA before attempting a local AWDL path.
        // Publish only after the real backend, BSD binding and epoch checks pass.
        if(ok)interface->reportLinkStatus(3,kItlDarwinWifiMedia);
        const bool ended=controller->awdlControlNativeEnd(epoch,serial);
        if(!ok||!ended)interface->reportLinkStatus(1,0);
        interface->setProperty("AWDLDiscoveryBSDRunning",ok&&ended);
        if(!ok||!ended){
            discoveryFailed=true;
            ops.withGate([&]{auto hal=OSDynamicCast(ItlIwm,controller->fHalService);if(hal)hal->awdlNativeDiscoveryStop();});
            // Keep the job pinned until the next pump rolls back even partial binding.
            if(!ended)gateFault=true;
        }
    }
    if(!discoveryBound||!ready)return;
    for(unsigned peerSlot=0;peerSlot<ItlAwdl::NativeDiscoverySession::PeerCapacity;++peerSlot){
    auto &registration=awdlDiscoveryPeers[peerSlot];
    ItlAwdl::NativePeerKey peerKey;bool publishPeer=false;
    ops.withGate([&]{auto hal=OSDynamicCast(ItlIwm,controller->fHalService);if(!hal)return;
        auto &d=hal->awdlNativeDiscovery;
        publishPeer=hal->awdlNativeDiscoveryReady()&&d.open&&!d.closed&&d.epoch==epoch&&d.peers[peerSlot].prepared&&!d.peers[peerSlot].published;
        if(publishPeer)peerKey=d.peers[peerSlot].key;
    });
    if(publishPeer){
        const auto serial=controller->awdlControlNativeBegin(epoch,true);
        if(!serial){gateFault=true;return;}
        ItlNativePeerOps peerOps;
        bool ok=registration.idle()&&registration.start(peerOps,interface,awdlNativeBsdBinding.interface,peerKey);
        interface->setProperty("AWDLDiscoveryPeerPresentResult",uint32_t(registration.presentResult),32);
        ops.withGate([&]{auto hal=OSDynamicCast(ItlIwm,controller->fHalService);
            ok=ok&&!interface->isInactive()&&!interface->probeStopDone&&controller->awdlControlPermitsEnable()&&hal&&hal->awdlNativeDiscoveryReady()&&hal->awdlNativeDiscovery.open&&!hal->awdlNativeDiscovery.closed&&
                hal->awdlNativeDiscovery.epoch==epoch&&hal->awdlNativeDiscovery.peers[peerSlot].prepared&&
                hal->awdlNativeDiscovery.peers[peerSlot].key.generation==peerKey.generation&&hal->awdlNativeDiscovery.peers[peerSlot].key.epoch==peerKey.epoch&&
                !memcmp(hal->awdlNativeDiscovery.peers[peerSlot].key.mac,peerKey.mac,6);
            if(ok){hal->awdlNativeDiscovery.peers[peerSlot].published=true;hal->awdlNativeDiscovery.updatePeers();}
        });
        if(!ok){
            if(registration.owner==interface)registration.stop(peerOps);
            discoveryFailed=true;
            ops.withGate([&]{auto hal=OSDynamicCast(ItlIwm,controller->fHalService);if(hal){
                ++hal->awdlNativeDiscovery.peerFailures;hal->awdlNativeDiscoveryStop();hal->awdlNativeSearch.fail(epoch,EIO);}});
        }
        if(!controller->awdlControlNativeEnd(epoch,serial)){gateFault=true;return;}
        interface->setProperty("AWDLDiscoveryPeerReady",ok);
        if(!ok)return;
    }
    }
    bool deliveredBatch=false;
    for(unsigned i=0;i<16;++i){
        ItlAwdl::NativeDiscoverySession::Packet packet;bool have=false;
        ops.withGate([&]{
            auto hal=OSDynamicCast(ItlIwm,controller->fHalService);
            if(hal&&hal->awdlNativeDiscoveryReady()&&hal->awdlProbeCounters)
                {uint64_t tick=mach_absolute_time(),now=0;absolutetime_to_nanoseconds(tick,&now);
                 ItlAwdl::DataContext model;hal->awdlProbeCounters->dataModelView(model);
                 have=hal->awdlNativeDiscovery.takeRx(now,model.modelEpoch,packet);}
        });
        if(!have)break;
        // The claim above and this delivery finish before this dispatcher can
        // withdraw BSD or unpin the owner; no raw RX mbuf crosses the gate.
        const int result=awdlBSDDataInput(interface,packet.bytes,packet.length);
        ops.withGate([&]{
            auto hal=OSDynamicCast(ItlIwm,controller->fHalService);if(!hal)return;
            if(result)++hal->awdlNativeDiscovery.inputErrors;else{
                ++hal->awdlNativeDiscovery.delivered;
                if(!(packet.bytes[0]&1))++hal->awdlNativeDiscovery.peerDelivered;
            }
            deliveredBatch=true;
        });
    }
    if(deliveredBatch)ops.withGate([&]{
        auto hal=OSDynamicCast(ItlIwm,controller->fHalService);
        if(hal&&hal->awdlNativeSearch.matches(epoch))hal->awdlNativeDiscoveryPublish();
    });
}
void ItlNativeControlCallout::run(void *argument,void *){
    auto job=static_cast<ItlNativeControlCallout*>(argument);
    job->trace(2,false);
    for(;;){
        job->discoveryPump();
        job->adapter.pump(job->ops);
        job->discoveryPump();
        bool failed=false;job->ops.withGate([&]{failed=job->adapter.quarantined(job->ops);});
        if(job->completionRequested||failed||job->gateFault){
            job->ops.withGate([&]{
                if(job->controller->awdlPublishNativeControlState()==kIOReturnSuccess){
                    job->after=job->controller->getProperty("AWDLNativeControlState");if(job->after)job->after->retain();
                }
            });
            const bool retired=job->controller->awdlControlComplete(job->epoch,job->completionRequested&&!job->gateFault);
            if(retired && !job->gateFault && !job->discoveryFailed && !job->discoveryBound && job->downCalls==1){
                job->ops.withGate([&]{
                    auto hal=OSDynamicCast(ItlIwm,job->controller->fHalService);
                    if(hal && hal->awdlNativeSearch.matches(job->epoch) && hal->awdlNativeDiscovery.epoch==job->epoch)
                        hal->awdlNativeDiscovery.controlRetired=true;
                });
            }
            job->trace(retired?3:4,retired);
            // Completion has released the controller job; this callout still owns
            // its own controller/gate/loop references until this final release.
            job->release();return;
        }
        // RX has four copied slots; promptly drain bursts without publishing per packet.
        // The retained job is still the sole BSD publisher/deliverer/withdrawer.
        IOSleep(job->discoveryBound ? 1 : 25);
    }
}
IOReturn AirportItlwm::awdlControlSubmitDiagnostic(uint64_t epoch){
    if(!epoch||!fAwdlControlLock)return kIOReturnBadArgument;
    auto context=new ItlNativeControlCallout;if(!context)return kIOReturnNoMemory;
    if(!context->init()){context->release();return kIOReturnNoMemory;}
    context->ops.job=context;context->epoch=epoch;
    context->call=thread_call_allocate(ItlNativeControlCallout::run,context);
    if(!context->call){context->release();return kIOReturnNoResources;}
    IOLockLock(fAwdlControlLock);
    const bool ready=epoch>fAwdlControlLastTrialEpoch&&!fAwdlControlClosing&&!fAwdlControlJob&&_fCommandGate&&_fWorkloop&&_fWorkloop->inGate();
    if(ready){
        context->controller=this;retain();context->gate=_fCommandGate;context->gate->retain();
        context->loop=_fWorkloop;context->loop->retain();context->interface=OSDynamicCast(ItlAwdlInterface,fAWDLInterface);
    }
    IOLockUnlock(fAwdlControlLock);
    if(!ready||!context->interface||!context->adapter.begin(context->ops,context->interface)){
        context->release();return kIOReturnNotReady;
    }
    IOLockLock(fAwdlControlLock);fAwdlControlLastTrialEpoch=epoch;IOLockUnlock(fAwdlControlLock);
    if(awdlPublishNativeControlState()==kIOReturnSuccess){
        context->before=getProperty("AWDLNativeControlState");if(context->before)context->before->retain();
    }
    context->trace(1,false);
    // All resources and pins committed. Nothing fallible follows submission.
    thread_call_enter(context->call);return kIOReturnSuccess;
}

void AirportItlwm::releaseAll()
{
    awdlControlClose();
    if(awdlControlHasJob()){setProperty("AWDLControlReleaseBlocked",true);return;}
#ifdef AIRPORT_WCL
    if (fNetIf)
        static_cast<AirportItlwmSkywalkInterface *>(fNetIf)->stopWCL();
    OSSafeReleaseNULL(driverFaultReporter);
    OSSafeReleaseNULL(driverCoreFaultReporter);
    OSSafeReleaseNULL(driverFaultStream);
    OSSafeReleaseNULL(driverLogger);
    // Stop only successfully started pipes, while their owner workloop exists.
    if (driverSnapshotsPipeStarted) {
        driverSnapshotsPipe->CCPipe::stopPipe();
        driverSnapshotsPipeStarted = false;
    }
    if (driverDataPathPipeStarted) {
        driverDataPathPipe->CCPipe::stopPipe();
        driverDataPathPipeStarted = false;
    }
    if (driverLogPipeStarted) {
        driverLogPipe->CCPipe::stopPipe();
        driverLogPipeStarted = false;
    }
#endif
    OSSafeReleaseNULL(driverLogPipe);
    OSSafeReleaseNULL(driverDataPathPipe);
    OSSafeReleaseNULL(driverSnapshotsPipe);
#ifndef AIRPORT_WCL
    OSSafeReleaseNULL(driverFaultReporter);
#endif
    if (fHalService) {
        fHalService->release();
        fHalService = NULL;
    }
    if (_fWorkloop) {
        if (_fCommandGate) {
//            _fCommandGate->disable();
            _fWorkloop->removeEventSource(_fCommandGate);
            _fCommandGate->release();
            _fCommandGate = NULL;
        }
        if (scanSource) {
            scanSource->cancelTimeout();
            scanSource->disable();
            _fWorkloop->removeEventSource(scanSource);
            scanSource->release();
            scanSource = NULL;
        }
        if (fWatchdogWorkLoop && watchdogTimer) {
            watchdogTimer->cancelTimeout();
            fWatchdogWorkLoop->removeEventSource(watchdogTimer);
            watchdogTimer->release();
            watchdogTimer = NULL;
            fWatchdogWorkLoop->release();
            fWatchdogWorkLoop = NULL;
        }
        _fWorkloop->release();
        _fWorkloop = NULL;
    }
    unregistPM();
}

void AirportItlwm::
eventHandler(struct ieee80211com *ic, int msgCode, void *data)
{
    AirportItlwm *that = OSDynamicCast(AirportItlwm, ic->ic_ac.ac_if.controller);
#ifdef AIRPORT_WCL
    if (!that)
        return;
#endif
    IO80211SkywalkInterface *interface = that->fNetIf;
    if (!interface)
        return;
#ifdef AIRPORT_WCL
    static_cast<AirportItlwmSkywalkInterface *>(interface)->handleWCLEvent(msgCode, data);
#else
    switch (msgCode) {
        case IEEE80211_EVT_COUNTRY_CODE_UPDATE:
            interface->postMessage(APPLE80211_M_COUNTRY_CODE_CHANGED, NULL, 0, 0);
            break;
        case IEEE80211_EVT_STA_ASSOC_DONE:
            interface->postMessage(APPLE80211_M_ASSOC_DONE, NULL, 0, 0);
            break;
        case IEEE80211_EVT_STA_DEAUTH:
            interface->postMessage(APPLE80211_M_DEAUTH_RECEIVED, NULL, 0, 0);
            break;
        default:
            break;
    }
#endif
}

void AirportItlwm::watchdogAction(IOTimerEventSource *timer)
{
    ItlAwdlDataCall nativeMaintenance={ItlAwdlDataCall::Maintenance};
    if(awdlDataDispatch(nativeMaintenance)==kIOReturnSuccess&&nativeMaintenance.owner){
        auto interface=OSDynamicCast(ItlAwdlInterface,nativeMaintenance.owner);
        if(interface)interface->stopProbe(this);
        nativeMaintenance.owner->release();
    }
    struct _ifnet *ifp = &fHalService->get80211Controller()->ic_ac.ac_if;
    (*ifp->if_watchdog)(ifp);

    // Every tick: how often the system has knocked on the (still stubbed)
    // Apple80211 AWDL control methods.
    if (fAWDLInterface && !fAWDLInterface->isInactive()) {
        // Probe51: reflect the bounded AWDL dwell engine that the HAL layer runs.
        // The interface's link is reported "up" only while that engine has really
        // completed dwell cycles; when it stops, or never started, the link goes
        // back to down. No RUNNING/media state is fabricated here.
        {
            // Probe52: the link claim now rests on accumulated firmware
            // notifications (a real START/END), not on "the session went idle".
            const bool engineUp = gItlAwdlControlProbe && ItlAwdl::gEngine.running != 0 &&
                                  ItlAwdl::gEngine.endsTotal > 0;
            setProperty("AWDLProbeLinkStateIsTestInput", gItlAwdlControlProbe);
            setProperty("AWDLProbeEngineMode", ItlAwdl::gEngine.modeEnabled != 0);
            setProperty("AWDLProbeEngineActive", ItlAwdl::gEngine.running != 0);
            setProperty("AWDLProbeControlLinkUp", engineUp);
            setProperty("AWDLProbeEngineChannel",
                (unsigned long long)(uint32_t)ItlAwdl::gEngine.channel, 32);
            setProperty("AWDLProbeEngineSubmitted",
                (unsigned long long)(uint32_t)ItlAwdl::gEngine.cyclesSubmitted, 32);
            setProperty("AWDLProbeEngineCompleted",
                (unsigned long long)(uint32_t)ItlAwdl::gEngine.cyclesCompleted, 32);
            setProperty("AWDLProbeEngineFailures",
                (unsigned long long)(uint32_t)ItlAwdl::gEngine.cycleFailures, 32);
            setProperty("AWDLProbeEngineStartsTotal",
                (unsigned long long)(uint32_t)ItlAwdl::gEngine.startsTotal, 32);
            setProperty("AWDLProbeEngineEndsTotal",
                (unsigned long long)(uint32_t)ItlAwdl::gEngine.endsTotal, 32);
            setProperty("AWDLProbeEngineFailuresTotal",
                (unsigned long long)(uint32_t)ItlAwdl::gEngine.failuresTotal, 32);
            const int wantUp = engineUp ? 1 : 0;
            if (gItlAwdlControlProbe && ItlAwdl::gEngine.linkUpReported != wantUp) {
                ItlAwdl::gEngine.linkUpReported = wantUp;
                static_cast<ItlAwdlInterface *>(fAWDLInterface)->setLinkState(
                    wantUp ? kIO80211NetworkLinkUp : kIO80211NetworkLinkDown, 0);
                setProperty("AWDLProbeEngineLinkReported",
                    (unsigned long long)(uint32_t)wantUp, 32);
            }
        }
        setProperty("AWDLProbeProtocolCalls",
            (unsigned long long)(uint32_t)static_cast<ItlAwdlInterface *>(fAWDLInterface)->probeProtocolCalls, 32);
        uint32_t counts[ItlAwdlInterface::ProtocolMethodCount];
        uint32_t total = 0;
        char summary[512] = {};
        size_t used = 0;
        for (unsigned i = 0; i < ItlAwdlInterface::ProtocolMethodCount; ++i) {
            counts[i] = static_cast<uint32_t>(OSAddAtomic(0, &gItlAwdlProtocolCounts[i]));
            total += counts[i];
            if (counts[i] && used + 64 < sizeof(summary)) {
                const int n = snprintf(summary + used, sizeof(summary) - used, "%s=%u ",
                                       ItlAwdlInterface::protocolMethodName(i), counts[i]);
                if (n > 0 && static_cast<size_t>(n) < sizeof(summary) - used)
                    used += static_cast<size_t>(n);
            }
        }
        setProperty("AWDLProbeIoctlTotal", static_cast<unsigned long long>(total), 32);
        setProperty("AWDLProbeIoctlCounts", summary);
        setProperty("AWDLProbeIoctlCountsRaw", counts, sizeof(counts));
        uint32_t serviceStages[6];
        for(unsigned i=0;i<6;++i)serviceStages[i]=uint32_t(OSAddAtomic(0,&gServiceMifStages[i]));
        setProperty("AWDLServiceMifStages",serviceStages,sizeof(serviceStages));
        setProperty("AWDLProbeNativeIfnetPresent", fAWDLInterface->getBSDInterface() != nullptr);
        // Keep the family's AWDL slot pointing at our interface.
        setAwdlInterface(fAWDLInterface);
        // The Skywalk BSD client writes "BSD Name"/"IOInterfaceName" onto this
        // object whenever it is re-matched, which makes SystemConfiguration see
        // two registry objects claiming awdl0 ("more than one interface matches
        // awdl0"). The comparable 80211 interface for en3 carries neither, and
        // the real ifnet already owns the name, so drop them here.
        if (fAWDLInterface->getProperty("BSD Name")) {
            fAWDLInterface->removeProperty("BSD Name");
            ++gItlAwdlNameClears;
        }
        // IOInterfaceName is required by wifip2pd. Unlike BSD Name, it does
        // not claim a second BSD interface; retain the verified control name.
        setProperty("AWDLProbeNameClears", (unsigned long long)(uint32_t)gItlAwdlNameClears, 32);
    }

    // Matching is asynchronous. Retain and validate both bridge objects, only
    // call base attach once per AWDL instance, and always release ifnet lookups.
    if (fAWDLInterface && !fAWDLInterface->isInactive() &&
        !fAWDLInterface->getProperty("AWDLProbeBridgeAttached")) {
        OSNumber *prior = OSDynamicCast(OSNumber, fAWDLInterface->getProperty("AWDLProbeAttachTries"));
        const uint32_t tries = prior ? prior->unsigned32BitValue() : 0;
        if (tries < 30) {
            fAWDLInterface->setProperty("AWDLProbeAttachTries", tries + 1, 32);
            setProperty("AWDLProbeNudgeTries", tries + 1, 32);
            const IORegistryPlane *plane = IORegistryEntry::getPlane("IOService");
            IORegistryEntry *bridge = plane ? fAWDLInterface->copyChildEntry(plane) : nullptr;
            const bool bridgeOK = bridge && bridge->metaCast("IOSkywalkLegacyEthernet");
            IORegistryEntry *legacy = bridgeOK ? bridge->copyChildEntry(plane) : nullptr;
            const bool legacyOK = legacy && legacy->metaCast("IOSkywalkLegacyEthernetInterface");
            setProperty("AWDLProbeBridgePresent", bridgeOK);
            setProperty("AWDLProbeLegacyPresent", legacyOK);
            if (bridge)
                setProperty("AWDLProbeBridgeClass", bridge->getMetaClass()->getClassName());
            if (legacy)
                setProperty("AWDLProbeLegacyClass", legacy->getMetaClass()->getClassName());
            void *found = nullptr;
            int findResult = ItlIfnetFindByName("awdl0", &found);
            // A name collision must never trigger a second attachment.
            // The base method ALLOCATES the ifnet; requiring a non-null
            // getIfnet() here would prevent the allocation itself (probe42).
            if (!found && legacyOK &&
                !fAWDLInterface->getProperty("AWDLProbeBaseAttachAttempted")) {
                // Darwin family Ethernet/subfamily Wi-Fi, like the working
                // infrastructure bridge. Set before ifnet allocation; the API
                // refuses changing subtype once an ifnet already exists.
                const bool subtypeOK = ItlNativeSetInterfaceSubType(legacy, 3);
                // Kernel IPv6 and membership admission require IFF_MULTICAST.
                // Keep RUNNING clear: membership support is not radio readiness.
                const bool multicastOK = ItlNativeSetInterfaceFlags(legacy, 0x8000, 0);
                setProperty("AWDLProbeWifiSubTypeSet", subtypeOK);
                setProperty("AWDLProbeMulticastFlagSet", multicastOK);
                if (subtypeOK && multicastOK) {
                    fAWDLInterface->setProperty("AWDLProbeBaseAttachAttempted", true);
                    const IOReturn result = ItlNativeAttachToDataLinkLayer(legacy, 0, nullptr);
                    setProperty("AWDLProbeBaseAttachResult", static_cast<unsigned long long>(static_cast<uint32_t>(result)), 32);
                    findResult = ItlIfnetFindByName("awdl0", &found);
                }
            }
            void *legacyIfnet = legacyOK ? ItlNativeGetIfnet(legacy) : nullptr;
            setProperty("AWDLProbeLegacyIfnetPresent", legacyIfnet != nullptr);
            const bool foundOurs = found && found == legacyIfnet;
            setProperty("AWDLProbeIfnetFindErrno", static_cast<unsigned long long>(static_cast<uint32_t>(findResult)), 32);
            setProperty("AWDLProbeIfnetFound", findResult == 0 && foundOurs);
            setProperty("AWDLProbeIfnetNameConflict", found && !foundOurs);
            setProperty("AWDLProbeNativeIfnetMatches", foundOurs && fAWDLInterface->getBSDInterface() == found);
            if (findResult == 0 && foundOurs) {
                auto awdl = static_cast<ItlAwdlInterface *>(fAWDLInterface);
                const bool controlReady = awdl->probeBindBSDControl(
                    reinterpret_cast<ifnet_t>(found), legacy);
                setProperty("AWDLProbeBSDControlReady", controlReady);
                if (controlReady)
                    fAWDLInterface->setProperty("AWDLProbeBridgeAttached", true);
            }
            if (found)
                ItlIfnetRelease(found);
            OSSafeReleaseNULL(legacy);
            OSSafeReleaseNULL(bridge);
        }
    }

    watchdogTimer->setTimeoutMS(kWatchDogTimerPeriod);
}

void AirportItlwm::fakeScanDone(OSObject *owner, IOTimerEventSource *sender)
{
#ifndef AIRPORT_WCL
    UInt32 msg = 0;
#endif
    AirportItlwm *that = (AirportItlwm *)owner;
#ifdef AIRPORT_WCL
    if (that->fNetIf)
        static_cast<AirportItlwmSkywalkInterface *>(that->fNetIf)->completeWCLScan();
#else
    that->fNetIf->postMessage(APPLE80211_M_SCAN_DONE, &msg, 4, 0);
#endif
}

bool AirportItlwm::init(OSDictionary *properties)
{
    XYLog("%s\n", __PRETTY_FUNCTION__);
    bool ret = super::init(properties);
    if(ret){fAwdlControlLock=IOLockAlloc();if(!fAwdlControlLock)return false;}
    awdlSyncEnable = true;
    power_state = 0;
    memset(geo_location_cc, 0, sizeof(geo_location_cc));
    return ret;
}

IOService* AirportItlwm::probe(IOService *provider, SInt32 *score)
{
    XYLog("%s\n", __PRETTY_FUNCTION__);
    IOPCIEDeviceWrapper *wrapper = OSDynamicCast(IOPCIEDeviceWrapper, provider);
    if (!wrapper) {
        XYLog("%s Not a IOPCIEDeviceWrapper instance\n", __FUNCTION__);
        return NULL;
    }
    pciNub = wrapper->pciNub;
    fHalService = wrapper->fHalService;
    if (!pciNub || !fHalService) {
        XYLog("%s Not a valid IOPCIEDeviceWrapper instance\n", __FUNCTION__);
        return NULL;
    }
    return super::probe(provider, score);
}

#define LOWER32(x)  ((uint64_t)(x) & 0xffffffff)
#define HIGHER32(x) ((uint64_t)(x) >> 32)

bool AirportItlwm::
initCCLogs()
{
    CCPipeOptions driverLogOptions = { 0 };
    driverLogOptions.pipe_type = 0;
    driverLogOptions.log_data_type = 1;
    driverLogOptions.pipe_size = 0x200000;
    driverLogOptions.min_log_size_notify = 0xccccc;
    driverLogOptions.notify_threshold = 1000;
    strlcpy(driverLogOptions.file_name, "Itlwm_Logs", sizeof(driverLogOptions.file_name));
    snprintf(driverLogOptions.name, sizeof(driverLogOptions.name), "wlan%d", 0);
    strlcpy(driverLogOptions.directory_name, "WiFi", sizeof(driverLogOptions.directory_name));
    driverLogOptions.pad9 = 0x1000000;
    driverLogOptions.pad10 = 2;
    driverLogOptions.file_options = 0;
    driverLogOptions.log_policy = 0;
    driverLogPipe = CCPipe::withOwnerNameCapacity(this, "com.zxystd.AirportItlwm", "DriverLogs", &driverLogOptions);
    XYLog("%s driverLogPipeRet %d\n", __FUNCTION__, driverLogPipe != NULL);
#ifdef AIRPORT_WCL
    if (!driverLogPipe || !getWorkLoop())
        return false;
    // The factory only initializes the pipe. startPipe creates its event sources.
    // Qualify the native method: do not dispatch through the legacy CCPipe header.
    driverLogPipeStarted = driverLogPipe->CCPipe::startPipe();
    if (!driverLogPipeStarted)
        return false;
    CCStreamOptions loggerOptions = {};
    CCStream *logger = CCStream::withPipeAndName(driverLogPipe, "AirportItlwm", &loggerOptions);
    driverLogger = OSDynamicCast(CCLogStream, logger);
    if (!driverLogger) {
        OSSafeReleaseNULL(logger);
        return false;
    }
#endif

    memset(&driverLogOptions, 0, sizeof(driverLogOptions));
    driverLogOptions.pipe_type = 0;
    driverLogOptions.log_data_type = 0;
    driverLogOptions.pipe_size = 0x200000;
    driverLogOptions.min_log_size_notify = 0xccccc;
    driverLogOptions.notify_threshold = 1000;
    strlcpy(driverLogOptions.file_name, "AppleBCMWLAN_Datapath", sizeof(driverLogOptions.file_name));
    strlcpy(driverLogOptions.directory_name, "WiFi", sizeof(driverLogOptions.directory_name));
    driverLogOptions.pad9 = HIGHER32(0x202800000);
    driverLogOptions.pad10 = LOWER32(0x202800000);
    driverLogOptions.file_options = 0;
    driverLogOptions.log_policy = 0;
    driverDataPathPipe = CCPipe::withOwnerNameCapacity(this, "com.zxystd.AirportItlwm", "DatapathEvents", &driverLogOptions);
    XYLog("%s driverDataPathPipeRet %d\n", __FUNCTION__, driverDataPathPipe != NULL);
#ifdef AIRPORT_WCL
    if (!driverDataPathPipe)
        return false;
    driverDataPathPipeStarted = driverDataPathPipe->CCPipe::startPipe();
    if (!driverDataPathPipeStarted)
        return false;
#endif

    memset(&driverLogOptions, 0, sizeof(driverLogOptions));
    driverLogOptions.pipe_type = 0x200000001;
    driverLogOptions.log_data_type = 2;
    strlcpy(driverLogOptions.file_name, "StateSnapshots", sizeof(driverLogOptions.file_name));
    strlcpy(driverLogOptions.name, "0", sizeof(driverLogOptions.name));
    strlcpy(driverLogOptions.directory_name, "WiFi", sizeof(driverLogOptions.directory_name));
    driverLogOptions.pipe_size = 128;
    driverSnapshotsPipe = CCPipe::withOwnerNameCapacity(this, "com.zxystd.AirportItlwm", "StateSnapshots", &driverLogOptions);
    XYLog("%s driverSnapshotsPipeRet %d\n", __FUNCTION__, driverSnapshotsPipe != NULL);
#ifdef AIRPORT_WCL
    if (!driverSnapshotsPipe)
        return false;
    driverSnapshotsPipeStarted = driverSnapshotsPipe->CCPipe::startPipe();
    if (!driverSnapshotsPipeStarted)
        return false;
#endif

    CCStreamOptions faultReportOptions = { 0 };
    faultReportOptions.stream_type = 1;
    faultReportOptions.console_level = 0xFFFFFFFFFFFFFFFF;
#ifdef AIRPORT_WCL
    driverFaultStream = CCStream::withPipeAndName(driverSnapshotsPipe, "FaultReporter", &faultReportOptions);
    CCDataStream *dataStream = OSDynamicCast(CCDataStream, driverFaultStream);
    if (!dataStream || !_fWorkloop) {
        return false;
    }
    driverCoreFaultReporter = CCFaultReporter::withStreamWorkloop(dataStream, _fWorkloop);
    if (!driverCoreFaultReporter) {
        return false;
    }
    driverFaultReporter = IO80211FaultReporter::allocWithParams(driverCoreFaultReporter);
    if (!OSDynamicCast(CommonFaultReporter, driverFaultReporter)) {
        return false;
    }
#else
    driverFaultReporter = CCStream::withPipeAndName(driverSnapshotsPipe, "FaultReporter", &faultReportOptions);
#endif
    XYLog("%s driverFaultReporterRet %d\n", __FUNCTION__, driverFaultReporter != NULL);
    return driverLogPipe && driverDataPathPipe && driverSnapshotsPipe && driverFaultReporter;
}

IO80211VirtualInterface *AirportItlwm::
createVirtualInterface(ether_addr *ether, UInt role)
{
    setProperty("VifCreateRole", (unsigned long long)role, 32);
    // Use the role constant from this target's ABI; do not accept other roles.
    if (role == APPLE80211_VIF_AWDL) {
        // Probe39: the family dispatches VIRTUAL_IF_CREATE to the infra interface,
        // whose handler calls IO80211Controller::attachVirtualInterface(), which
        // calls this factory. Allocate and initialize here; attaching, starting,
        // registering the data-path resources and publishing stay in the handler.
        ItlAwdlInterface *inf = new ItlAwdlInterface;
        if (!inf) {
            setProperty("VifCreateResult", "factory-alloc-failed");
            return nullptr;
        }
        if (!inf->IO80211VirtualInterface::init(this, ether, APPLE80211_VIF_AWDL, "awdl")) {
            inf->release();
            setProperty("VifCreateResult", "factory-init-failed");
            return nullptr;
        }
        ItlAwdl::gEngine.availabilityWindowEvent =
            [](void *context, bool start, uint32_t firmwareStartGp2, uint16_t awNumber) {
                static_cast<ItlAwdlInterface *>(context)->postAvailabilityWindowEvent(
                    start, firmwareStartGp2, awNumber);
            };
        ItlAwdl::gEngine.availabilityWindowContext = inf;
        setProperty("VifCreateResult", "factory-created-v43");
        return inf;
    }
    return super::createVirtualInterface(ether, role);
}

// Stage marker for start(). Written to the controller and to the PCI node: the
// PCI node is published even when the controller is not, and an unpublished
// controller hides its own properties, which made an earlier round of markers
// unreadable. This function runs during early boot, where kprintf output does not
// reach the unified log, so properties are the only usable channel.
#define ItlStage(s) do { setProperty("ItlStage", (s)); if (pciNub) pciNub->setProperty("ItlStage", (s)); } while (0)

bool AirportItlwm::start(IOService *provider)
{
#if __IO80211_TARGET == __MAC_15_2
    if (version_major != 24 || version_minor < 2)
        return false;
#endif
    setProperty("AWDLRuntimeServiceSchema",1,32);
    setProperty("AWDLRuntimeArmSchema",1,32);
    setProperty("AWDLSingleDataProbeSchema",1,32);
    setProperty("AWDLIbssDataProbeSchema",1,32);
    setProperty("AWDLIbssNativeProbeSchema",1,32);
    setProperty("AWDLNativeSearchSchema",1,32);
    setProperty("AWDLNativeReceiveTrialSchema",1,32);
    setProperty("AWDLNativeSyncProbeSchema",1,32);
    setProperty("AWDLNativeControlStateSchema",1,32);
    setProperty("AWDLNativeControlTrialSchema",1,32);
    setProperty("AWDLNativeControlTxSchema",1,32);
    setProperty("AWDLNativeControlCycleSchema",1,32);
    setProperty("AWDLNativePeerRegistrationSchema",1,32);
    setProperty("AWDLIbssSmallBatchSchema",1,32);
    setProperty("AWDLListenerProbeSchema",1,32);
    setProperty("AWDLListenerTxProbeSchema",1,32);
    setProperty("AWDLListenerTxEvidenceSchema",1,32);
    setProperty("AWDLListenerPairProbeSchema",1,32);
    setProperty("AWDLIbssEchoProbeSchema",1,32);
    setProperty("AWDLIbssReceiveProbeSchema",1,32);
    XYLog("%s\n", __PRETTY_FUNCTION__);
    struct IOSkywalkEthernetInterface::RegistrationInfo registInfo;
    int boot_value = 0;

    UInt8 builtIn = 0;
    setProperty("built-in", OSData::withBytes(&builtIn, sizeof(builtIn)));
    setProperty("DriverKitDriver", kOSBooleanFalse);
    // Stage markers: this function runs during early boot, where kprintf output
    // does not reach the unified log, so progress is reported as registry
    // properties that can be read at any time. The last marker reached says which
    // step the driver got to.
    ItlStage("1-entered");
#ifdef AIRPORT_WCL
    if (!initCCLogs()) {
        releaseAll();
        return false;
    }
#endif
    if (!super::start(provider)) {
#ifdef AIRPORT_WCL
        releaseAll();
#endif
        return false;
    }
    pciNub->setBusMasterEnable(true);
    pciNub->setIOEnable(true);
    ItlStage("2-super-started");
    pciNub->setMemoryEnable(true);
    pciNub->configWrite8(0x41, 0);
    if (pciNub->requestPowerDomainState(kIOPMPowerOn,
                                        (IOPowerConnection *) getParentEntry(gIOPowerPlane), IOPMLowestState) != IOPMNoErr) {
        super::stop(provider);
        return false;
    }
    if (initPCIPowerManagment(pciNub) == false) {
        super::stop(pciNub);
        return false;
    }
    if (_fWorkloop == NULL) {
        XYLog("No _fWorkloop!!\n");
        super::stop(pciNub);
        releaseAll();
        return false;
    }
    _fCommandGate = IOCommandGate::commandGate(this, (IOCommandGate::Action)AirportItlwm::tsleepHandler);
    if (_fCommandGate == 0) {
        XYLog("No command gate!!\n");
        super::stop(pciNub);
        releaseAll();
        return false;
    }
    _fWorkloop->addEventSource(_fCommandGate);
    const IONetworkMedium *primaryMedium;
    if (!createMediumTables(&primaryMedium) ||
        !setCurrentMedium(primaryMedium) || !setSelectedMedium(primaryMedium)) {
        XYLog("setup medium fail\n");
        releaseAll();
        return false;
    }
    fHalService->initWithController(this, _fWorkloop, _fCommandGate);
    fHalService->get80211Controller()->ic_event_handler = eventHandler;

    if (PE_parse_boot_argn("-novht", &boot_value, sizeof(boot_value)))
        fHalService->get80211Controller()->ic_userflags |= IEEE80211_F_NOVHT;
    if (PE_parse_boot_argn("-noht40", &boot_value, sizeof(boot_value)))
        fHalService->get80211Controller()->ic_userflags |= IEEE80211_F_NOHT40;

#ifdef AIRPORT_WCL
    int he_enabled = 0;
    if (PE_parse_boot_argn("itlwm_he", &he_enabled, sizeof(he_enabled)) && he_enabled == 1)
        fHalService->get80211Controller()->ic_userflags |= IEEE80211_F_HEON;
#endif

    if (!fHalService->attach(pciNub)) {
        XYLog("attach fail\n");
        super::stop(pciNub);
        releaseAll();
        return false;
    }
    fWatchdogWorkLoop = IOWorkLoop::workLoop();
    if (fWatchdogWorkLoop == NULL) {
        XYLog("init watchdog workloop fail\n");
        fHalService->detach(pciNub);
        super::stop(pciNub);
        releaseAll();
        return false;
    }
    watchdogTimer = IOTimerEventSource::timerEventSource(this, OSMemberFunctionCast(IOTimerEventSource::Action, this, &AirportItlwm::watchdogAction));
    if (!watchdogTimer) {
        XYLog("init watchdog fail\n");
        fHalService->detach(pciNub);
        super::stop(pciNub);
        releaseAll();
        return false;
    }
    fWatchdogWorkLoop->addEventSource(watchdogTimer);
    scanSource = IOTimerEventSource::timerEventSource(this, &fakeScanDone);
#ifdef AIRPORT_WCL
    if (!scanSource || _fWorkloop->addEventSource(scanSource) != kIOReturnSuccess) {
        super::stop(provider);
        releaseAll();
        return false;
    }
#else
    _fWorkloop->addEventSource(scanSource);
#endif
    scanSource->enable();

    fNetIf = new AirportItlwmSkywalkInterface;
    ItlStage("3-hal-attached");
#ifdef AIRPORT_WCL
    if (!fNetIf || !static_cast<AirportItlwmSkywalkInterface *>(fNetIf)->init(this)) {
#else
    if (!fNetIf->init(this)) {
#endif
        XYLog("Skywalk interface init fail\n");
        super::stop(provider);
        releaseAll();
        return false;
    }
    fNetIf->setInterfaceRole(1);
    fNetIf->setInterfaceId(1);

#ifndef AIRPORT_WCL
    if (!initCCLogs()) {
        XYLog("CCLog init fail\n");
        super::stop(provider);
        releaseAll();
        return false;
    }
#endif
    if (!fNetIf->attach(this)) {
        XYLog("attach to service fail\n");
        super::stop(provider);
        releaseAll();
        return false;
    }
    if (!attachInterface(fNetIf, this)) {
        XYLog("attach to interface fail\n");
        super::stop(provider);
        releaseAll();
        return false;
    }
    ItlStage("4-netif-attached");
    if (!IONetworkController::attachInterface((IONetworkInterface **)&bsdInterface,
#ifdef AIRPORT_WCL
                                             false
#else
                                             true
#endif
                                             )) {
        XYLog("attach to IONetworkController interface fail\n");
        super::stop(provider);
        releaseAll();
        return false;
    }
    ItlStage("5-controller-interface-attached");
    memset(&registInfo, 0, sizeof(registInfo));
    if (!fNetIf->initRegistrationInfo(&registInfo, 1, sizeof(registInfo))) {
        XYLog("initRegistrationInfo fail\n");
        super::stop(provider);
        releaseAll();
        return false;
    }
    ItlStage("5a-reginfo-done");
#ifdef AIRPORT_WCL
    fNetIf->mExpansionData->fRegistrationInfo = (struct IOSkywalkNetworkInterface::RegistrationInfo *)IOMalloc(sizeof(struct IOSkywalkNetworkInterface::RegistrationInfo));
    fNetIf->mExpansionData2->fRegistrationInfo = (struct IOSkywalkEthernetInterface::RegistrationInfo *)IOMalloc(sizeof(struct IOSkywalkEthernetInterface::RegistrationInfo));
    if (!fNetIf->mExpansionData->fRegistrationInfo || !fNetIf->mExpansionData2->fRegistrationInfo) {
        super::stop(provider);
        releaseAll();
        return false;
    }
    ItlStage("5b-mallocs-done");
    memcpy(fNetIf->mExpansionData->fRegistrationInfo, &registInfo, sizeof(registInfo));
    memcpy(fNetIf->mExpansionData2->fRegistrationInfo, &registInfo, sizeof(registInfo));
    if (fNetIf->getInterfaceRole() == 1)
        fNetIf->deferBSDAttach(true);
    // Compare relocated targets without calling them or exporting addresses.
    extern const uintptr_t nativeSkywalkVtable[] __asm__("__ZTV23IO80211SkywalkInterface");
    extern const uintptr_t nativeVirtualVtable[] __asm__("__ZTV23IO80211VirtualInterface");
    const uintptr_t *actualVtable = *reinterpret_cast<const uintptr_t *const *>(fNetIf);
    const bool peerMatchesSkywalk = actualVtable[0xb00 / sizeof(uintptr_t)] == nativeSkywalkVtable[2 + 0xb00 / sizeof(uintptr_t)];
    const bool peerMatchesVirtual = actualVtable[0xb00 / sizeof(uintptr_t)] == nativeVirtualVtable[2 + 0xb00 / sizeof(uintptr_t)];
    setProperty("AbiPeerMatchesSkywalk", peerMatchesSkywalk);
    setProperty("AbiPeerMatchesVirtual", peerMatchesVirtual);
    if (pciNub) {
        pciNub->setProperty("AbiPeerMatchesSkywalk", peerMatchesSkywalk);
        pciNub->setProperty("AbiPeerMatchesVirtual", peerMatchesVirtual);
    }
    ItlStage("5c-calling-netif-start");
    if (!fNetIf->start(this)) {
        super::stop(provider);
        releaseAll();
        return false;
    }
    ItlStage("5d-netif-start-ok");

#else
    if (!fNetIf->initRegistrationInfo(&registInfo, 1, sizeof(registInfo))) {
        XYLog("initRegistrationInfo fail\n");
        super::stop(provider);
        releaseAll();
        return false;
    }
    fNetIf->mExpansionData->fRegistrationInfo = (struct IOSkywalkNetworkInterface::RegistrationInfo *)IOMalloc(sizeof(struct IOSkywalkNetworkInterface::RegistrationInfo));
    fNetIf->mExpansionData2->fRegistrationInfo = (struct IOSkywalkEthernetInterface::RegistrationInfo *)IOMalloc(sizeof(struct IOSkywalkEthernetInterface::RegistrationInfo));
    memcpy(fNetIf->mExpansionData->fRegistrationInfo, &registInfo, sizeof(registInfo));
    memcpy(fNetIf->mExpansionData2->fRegistrationInfo, &registInfo, sizeof(registInfo));
    if (fNetIf->getInterfaceRole() == 1)
        fNetIf->deferBSDAttach(true);
    fNetIf->start(this);

#endif
    setLinkStatus(kIONetworkLinkValid);
    ItlStage("6-netif-started");
    if (TAILQ_EMPTY(&fHalService->get80211Controller()->ic_ess))
        fHalService->get80211Controller()->ic_flags |= IEEE80211_F_AUTO_JOIN;
#ifdef AIRPORT_WCL
    bsdInterface->registerService();
    ItlStage("7-bsd-published");
#endif
    registerService();
    ItlStage("8-controller-registered");
    // Retain the successfully initialized AWDL interface for native BSD matching.
    ItlStage("9-reached-awdl");
    // Probe42 retains probe41's boot-creation policy as an explicit experiment.
    // This timing is not established as a requirement by the legacy reference.
    // -itlwm_awdl_nobootcreate selects the on-demand-only comparison.
    setProperty("AWDLTraceCompiledRole", APPLE80211_VIF_AWDL, 32);
    setProperty("AWDLProbeProtocolPolicy", "typed-config;master-policy-to-HAL;53-unsupported");
    int controlProbe = 0;
    gItlAwdlControlProbe = PE_parse_boot_argn("-itlwm_awdl_control_probe", &controlProbe, sizeof(controlProbe));
    setProperty("AWDLProbeLinkStateIsTestInput", gItlAwdlControlProbe);
    {
        int itlNoBootCreate = 0;
        PE_parse_boot_argn("-itlwm_awdl_nobootcreate", &itlNoBootCreate, sizeof(itlNoBootCreate));
        if (itlNoBootCreate) {
            setProperty("AWDLVifInit", "boot-create-disabled-v43");
            return true;
        }
    }
    IOEthernetAddress primary;
    if (getHardwareAddress(&primary) != kIOReturnSuccess) {
        setProperty("AWDLVifInit", "no-hardware-address-v34");
    } else {
        ether_addr address;
        memcpy(&address, &primary, sizeof(address));
        address.octet[0] = (address.octet[0] | 0x02) & 0xfe;
        address.octet[5] ^= 0x01;
        for (unsigned cycle = 1; cycle <= 1; ++cycle) {
            setProperty("AWDLProbeCycle", cycle, 32);
            OSNumber *marker = OSNumber::withNumber(0ULL, 32);
            if (!marker) {
                setProperty("AWDLVifInit", "marker-allocation-failed-v34");
                break;
            }
            const char *markerKey = cycle == 1 ? "AWDLProbeFree1" : "AWDLProbeFree2";
            setProperty(markerKey, marker);
            setProperty("AWDLVifInit", "allocating-v34");
            ItlAwdlInterface *probe = new ItlAwdlInterface;
            if (!probe) {
                marker->release();
                setProperty("AWDLVifInit", "allocation-failed-v34");
                break;
            }
            marker->retain();
            probe->probeFreeMarker = marker;
            setProperty("AWDLVifInit", "initializing-v34");
            const bool initialized = probe->IO80211VirtualInterface::init(
                this, &address, APPLE80211_VIF_AWDL, "awdl");
            setProperty("AWDLProbeInitOK", initialized);
            bool attached = false;
            bool detached = false;
            bool providerOK = false;
            bool familyPlaneOK = false;
            bool started = false;
            bool peerCreated = false;
            bool resourcesOK = false;
            bool peerCleared = false;
            bool nativeAwdlCleared = false;
            const IORegistryPlane *familyPlane = IORegistryEntry::getPlane("IO80211Plane");
            if (initialized) {
                setProperty("AWDLVifInit", "attaching-v34");
                attached = probe->IO80211VirtualInterface::attach(this);
                setProperty("AWDLProbeAttachOK", attached);
                providerOK = probe->getProvider() == this;
                familyPlaneOK = familyPlane && probe->getParentEntry(familyPlane) == this;
                setProperty("AWDLProbeProviderOK", providerOK);
                setProperty("AWDLProbeFamilyPlaneOK", familyPlaneOK);
                if (attached && providerOK && familyPlaneOK) {
                    setProperty("AWDLVifInit", "starting-v34");
                    started = probe->IO80211VirtualInterface::start(this);
                    setProperty("AWDLProbeStartOK", started);
                    peerCreated = probe->probeHasPeerManager();
                    setProperty("AWDLProbePeerManagerCreated", peerCreated);
                    if (started && peerCreated) {
                        setProperty("AWDLVifInit", "registering-resources-v34");
                        resourcesOK = probe->probeRegisterResources(this, address);
                    }
                    setProperty("AWDLProbeResourcesReady", resourcesOK);
                    if (resourcesOK) {
                        // Transfer the allocation reference to the controller.
                        // Native registration owns its pools and logical links.
                        fAWDLInterface = probe;
                        // The family answers Apple80211 virtual-interface queries
                        // from its own controller slot (expansion +0x188), not from
                        // the driver's member. Without this the role=awdl lookup
                        // keeps failing even though awdl0 exists.
                        setAwdlInterface(probe);
                        setProperty("AWDLProbeAwdlIfRegistered", true);
                        setProperty("AWDLProbeRoleValue",
                            (unsigned long long)(uint32_t)probe->getInterfaceRole(), 32);
                        probe->setProperty("AWDLProbeDataPath", "unsupported");
                        // Two native BSD paths exist in IOSkywalkFamily. The Skywalk
                        // client (IOSkywalkNetworkBSDClient) needs IOInterfaceUnit and
                        // dies in kern_nexus_controller_alloc_net_provider_instance,
                        // which is a stub on this kernel. The legacy bridge
                        // (IOSkywalkLegacyEthernet) refuses providers that already
                        // carry IOInterfaceUnit. Publish without it so the bridge,
                        // which creates a real IOEthernetInterface for IONetworkStack,
                        // is the path under test.
                        setProperty("AWDLProbeUnitPropBeforePublish",
                            probe->getProperty("IOInterfaceUnit") != nullptr);
                        setProperty("AWDLProbePrefixPropBeforePublish",
                            probe->getProperty("IOInterfaceNamePrefix") != nullptr);
                        probe->removeProperty("IOInterfaceUnit");
                        setProperty("AWDLProbeUnitPropCleared",
                            probe->getProperty("IOInterfaceUnit") == nullptr);
                        setProperty("AWDLVifInit", "bsd-publish-requested-v43");
                        setProperty("AWDLProbePublishRequested", true);
                        marker->release();
                        // Native deferBSDAttach(false) removes the deferral
                        // property AND calls registerService asynchronously.
                        probe->IOSkywalkNetworkInterface::deferBSDAttach(false);
                        return true;
                    }
                    setProperty("AWDLProbeBSDPresent", probe->getBSDInterface() != nullptr);
                    setProperty("AWDLVifInit", "stopping-v34");
                    probe->stopProbe(this);
                    setProperty("AWDLProbeStopReturned", true);
                    peerCleared = !probe->probeHasPeerManager();
                    const uint8_t *controllerExpansion = nullptr;
                    void *nativeAwdl = nullptr;
                    memcpy(&controllerExpansion, reinterpret_cast<const uint8_t *>(this) + 0x120, sizeof(controllerExpansion));
                    if (controllerExpansion)
                        memcpy(&nativeAwdl, controllerExpansion + 0x188, sizeof(nativeAwdl));
                    nativeAwdlCleared = controllerExpansion && !nativeAwdl;
                    setProperty("AWDLProbePeerManagerCleared", peerCleared);
                    setProperty("AWDLProbeNativeAwdlCleared", nativeAwdlCleared);
                    setProperty("AWDLProbeProtocolCalls", (unsigned long long)probe->probeProtocolCalls, 32);
                }
                setProperty("AWDLVifInit", "detaching-v34");
                // Virtual attach adds the IO80211 plane before IOService attach;
                // undo its partial attachment even when the latter fails.
                probe->IO80211VirtualInterface::detach(this);
                detached = probe->getProvider() == nullptr &&
                    (!familyPlane || probe->getParentEntry(familyPlane) == nullptr);
                setProperty("AWDLProbeDetachOK", detached);
            }
            setProperty("AWDLVifInit", "cleaning-v34");
            deRegisterInterestedDriver(probe);
            const bool opened = isOpen(probe);
            setProperty("AWDLProbeControllerOpened", opened);
            if (opened)
                close(probe);
            const bool closed = !isOpen(probe);
            setProperty("AWDLProbeControllerClosed", closed);
            setProperty("AWDLProbeRefsBeforeRelease", probe->getRetainCount(), 32);
            probe->release();
            const bool freed = marker->unsigned32BitValue() == 2;
            marker->release();
            if (!initialized || !attached || !providerOK || !familyPlaneOK || !started || !peerCreated || !resourcesOK || !peerCleared || !nativeAwdlCleared || !detached || !closed) {
                setProperty("AWDLVifInit", "cycle-failed-v34");
                break;
            }
            if (!freed) {
                setProperty("AWDLVifInit", "final-free-pending-v34");
                break;
            }
            setProperty("AWDLProbeCompletedCycles", cycle, 32);
            setProperty("AWDLVifInit", "resource-cycle-ok-v34");
        }
    }
    return true;
}

static bool awdlOwnsProbeIPv6(const uint8_t source[16]) {
    static_assert(AF_INET6==30&&sizeof(sockaddr_in6)==28&&offsetof(sockaddr_in6,sin6_addr)==8,"Darwin IPv6 KPI ABI");
    void *found=nullptr;if(ItlIfnetFindByName("awdl0",&found)||!found)return false;
    ifaddr_t *addresses=nullptr;bool matched=false;
    if(!ifnet_get_address_list_family(reinterpret_cast<ifnet_t>(found),&addresses,AF_INET6)&&addresses){
        for(unsigned i=0;i<256&&addresses[i];++i){sockaddr_in6 address={};
            if(ifaddr_address(addresses[i],reinterpret_cast<sockaddr *>(&address),sizeof(address)))continue;
            if(address.sin6_len!=sizeof(address)||address.sin6_family!=AF_INET6)continue;
            auto ip=address.sin6_addr.s6_addr;
            if(ip[0]!=0xfe||ip[1]!=0x80)continue;
            ip[2]=ip[3]=0; // Darwin may embed the scope index in a link-local address.
            if(!memcmp(ip,source,16)){matched=true;break;}
        }
    }
    if(addresses)ifnet_free_address_list(addresses);ItlIfnetRelease(found);return matched;
}

// Versioned, administrator-only experimental control. No raw firmware commands.
IOReturn AirportItlwm::setProperties(OSObject *properties) {
    auto root=OSDynamicCast(OSDictionary,properties);
    auto ibssProbe=root?root->getObject("AWDLIbssDataProbe"):nullptr;
    auto listenerProbe=root?root->getObject("AWDLListenerProbe"):nullptr;
    auto ibssReceive=root?root->getObject("AWDLIbssReceiveProbe"):nullptr;
    auto ibssCancel=root?root->getObject("AWDLIbssDataProbeCancel"):nullptr;
    auto scheduledProbe=root?root->getObject("AWDLIbssScheduledProbe"):nullptr;
    auto echoProbe=root?root->getObject("AWDLIbssEchoProbe"):nullptr;
    auto listenerTxProbe=root?root->getObject("AWDLListenerTxProbe"):nullptr;
    auto pairProbe=root?root->getObject("AWDLListenerPairProbe"):nullptr;
    auto search=root?root->getObject("AWDLNativeSearchCommand"):nullptr;
    if(search){
        if(IOUserClient::clientHasPrivilege(current_task(),kIOClientPrivilegeAdministrator)!=kIOReturnSuccess)return kIOReturnNotPrivileged;
        auto d=OSDynamicCast(OSDictionary,search);
        if(!d||root->getCount()!=1||d->getCount()!=3)return kIOReturnBadArgument;
        auto v=OSDynamicCast(OSNumber,d->getObject("Version"));auto c=OSDynamicCast(OSNumber,d->getObject("Command"));
        auto e=OSDynamicCast(OSNumber,d->getObject("Epoch"));
        if(!v||v->unsigned64BitValue()!=1||!c||c->unsigned64BitValue()>9||!e||
           ((c->unsigned64BitValue()==0||c->unsigned64BitValue()==3||c->unsigned64BitValue()==6||c->unsigned64BitValue()==7||c->unsigned64BitValue()==8)?e->unsigned64BitValue()==0:e->unsigned64BitValue()!=0))return kIOReturnBadArgument;
        if(!_fCommandGate)return kIOReturnNotReady;
        if(c->unsigned32BitValue()==0){
            auto hal=OSDynamicCast(ItlIwm,fHalService);
            if(!hal)return kIOReturnNotReady;
            __atomic_store_n(&hal->awdlHostSearchStopEpoch,e->unsigned64BitValue(),__ATOMIC_SEQ_CST);
            __atomic_store_n(&hal->awdlHostSearchStopPending,1,__ATOMIC_SEQ_CST);
            setProperty("AWDLNativeSearchStopPendingEpoch",e->unsigned64BitValue(),64);
            return kIOReturnSuccess;
        }
        struct Request{uint32_t command;uint64_t epoch;}request{c->unsigned32BitValue(),e->unsigned64BitValue()};
        return _fCommandGate->runAction([](OSObject *object,void *arg,void*,void*,void*)->IOReturn{
            auto self=static_cast<AirportItlwm *>(object);auto request=static_cast<Request *>(arg);
            auto hal=OSDynamicCast(ItlIwm,self->fHalService);if(!hal)return kIOReturnNotReady;
            if(request->command!=0&&(!self->fAWDLInterface||self->fAWDLInterface->isInactive()||ItlAwdl::gDataPort.owner!=self->fAWDLInterface))return kIOReturnNotReady;
            if(request->command==5)return self->awdlPublishNativeControlState();
            if(request->command==6)return self->awdlControlSubmitDiagnostic(request->epoch);
            if(request->command==7||request->command==8)return hal->awdlNativeControlTxArm(request->epoch,request->command==8);
            if((request->command==1||request->command==9)&&!self->awdlControlPermitsEnable())return kIOReturnNotReady;
            if(request->command==9)return hal->awdlNativeSearchRequest(false,0,true);
            // Test aliases exercise the real protocol methods, not the external PeerManager ioctl wrapper.
            if(request->command>=2){
                if(request->command==3&&request->epoch!=hal->awdlNativeSearch.epoch)return kIOReturnNotReady;
                auto interface=OSDynamicCast(ItlAwdlInterface,self->fAWDLInterface);if(!interface)return kIOReturnNotReady;
                apple80211_awdl_sync_enabled value={};value.enabled=request->command==2;
                if(request->command==4){
                    const auto result=interface->getAWDL_SYNC_ENABLED(&value);
                    return result==kIOReturnSuccess?(value.enabled?kIOReturnSuccess:kIOReturnNotReady):result;
                }
                return interface->setAWDL_SYNC_ENABLED(&value);
            }
            return hal->awdlNativeSearchRequest(request->command==0,request->epoch);
        },&request);
    }
    auto nativeProbe=root?root->getObject("AWDLIbssNativeProbe"):nullptr;
    if(ibssProbe||ibssCancel||listenerProbe||ibssReceive||scheduledProbe||pairProbe||echoProbe||listenerTxProbe||nativeProbe){
        if(IOUserClient::clientHasPrivilege(current_task(),kIOClientPrivilegeAdministrator)!=kIOReturnSuccess)return kIOReturnNotPrivileged;
        if(root->getCount()!=1||!_fCommandGate)return kIOReturnBadArgument;
        struct Request {ItlAwdl::SingleDataRequest data;uint8_t other[6]={},otherIp[16]={};bool native=false,autoPeer=false;bool listenerTx=false;bool pair=false;bool cancel=false;bool listener=false;bool passiveIbss=false;unsigned mode=0,echoProfile=0;} request;
        if(ibssCancel){auto n=OSDynamicCast(OSNumber,ibssCancel);if(!n||n->unsigned64BitValue()!=1)return kIOReturnBadArgument;request.cancel=true;}
        else {
            request.native=nativeProbe!=nullptr;request.listenerTx=listenerTxProbe!=nullptr;
            request.listener=listenerProbe!=nullptr;request.passiveIbss=ibssReceive!=nullptr;auto d=OSDynamicCast(OSDictionary,nativeProbe?nativeProbe:(listenerTxProbe?listenerTxProbe:(pairProbe?pairProbe:(echoProbe?echoProbe:(scheduledProbe?scheduledProbe:(ibssReceive?ibssReceive:(listenerProbe?listenerProbe:ibssProbe)))))));if(!d)return kIOReturnBadArgument;
            if(pairProbe){
                if(d->getCount()!=6)return kIOReturnBadArgument;
                auto mac=OSDynamicCast(OSData,d->getObject("OtherMAC"));auto ip=OSDynamicCast(OSData,d->getObject("OtherIPv6"));
                if(!mac||mac->getLength()!=6||!ip||ip->getLength()!=16)return kIOReturnBadArgument;
                request.pair=true;memcpy(request.other,mac->getBytesNoCopy(),6);memcpy(request.otherIp,ip->getBytesNoCopy(),16);
            }else if(d->getCount()!=(nativeProbe?(d->getObject("AutoPeer")?6:5):(echoProbe?6:(scheduledProbe?5:4))))return kIOReturnBadArgument;
            if(nativeProbe&&d->getObject("AutoPeer")){
                if(d->getObject("AutoPeer")!=kOSBooleanTrue)return kIOReturnBadArgument;
                request.autoPeer=true;
            }
            if(nativeProbe){auto source=OSDynamicCast(OSData,d->getObject("SourceIPv6"));if(!source||source->getLength()!=16)return kIOReturnBadArgument;memcpy(request.data.sourceIp,source->getBytesNoCopy(),16);}
            if(echoProbe){auto n=OSDynamicCast(OSNumber,d->getObject("EchoProfile"));if(!n||n->unsigned64BitValue()<1||n->unsigned64BitValue()>3)return kIOReturnBadArgument;request.echoProfile=n->unsigned32BitValue();}
            if(scheduledProbe||echoProbe){auto mode=OSDynamicCast(OSNumber,d->getObject("Mode"));if(!mode||(!echoProbe&&mode->unsigned64BitValue()<1)||mode->unsigned64BitValue()>(echoProbe?3:4))return kIOReturnBadArgument;request.mode=mode->unsigned32BitValue();}
            auto v=OSDynamicCast(OSNumber,d->getObject("Version"));auto token=OSDynamicCast(OSNumber,d->getObject("Token"));
            auto mac=OSDynamicCast(OSData,d->getObject("PeerMAC"));auto ip=OSDynamicCast(OSData,d->getObject("PeerIPv6"));
            if(!v||v->unsigned64BitValue()!=1||!token||!token->unsigned64BitValue()||!mac||mac->getLength()!=6||!ip||ip->getLength()!=16)return kIOReturnBadArgument;
            request.data.token=token->unsigned64BitValue();memcpy(request.data.peer,mac->getBytesNoCopy(),6);memcpy(request.data.peerIp,ip->getBytesNoCopy(),16);
        }
        return _fCommandGate->runAction([](OSObject *owner,void *arg,void*,void*,void*)->IOReturn{
            auto self=static_cast<AirportItlwm *>(owner);auto p=static_cast<Request *>(arg);auto hal=OSDynamicCast(ItlIwm,self->fHalService);
            if(p->native){
                auto inf=OSDynamicCast(ItlAwdlInterface,self->fAWDLInterface);
                if(!hal||!inf||inf->isInactive()||ItlAwdl::gDataPort.owner!=inf||gItlAwdlControlProbe||!awdlOwnsProbeIPv6(p->data.sourceIp))return kIOReturnNotReady;
                const uint64_t current=inf->getSelfMacAddr();
                if(!current||!p->data.token||p->data.token>0x7fffffffffffffffULL)return kIOReturnBadArgument;
                if(p->autoPeer&&!ItlAwdl::nativePeerIdentity(p->data.peer,p->data.peerIp))return kIOReturnBadArgument;
                const auto result=hal->awdlIbssNativeRequest(p->data,reinterpret_cast<const uint8_t *>(&current));
                if(result==kIOReturnSuccess&&p->autoPeer){
                    auto &port=ItlAwdl::gDataPort;port.nativeAutoPeer=true;
                    memcpy(port.nativePeerMAC,p->data.peer,6);memcpy(port.nativePeerIP,p->data.peerIp,16);
                }
                return result;
            }
            if(p->listenerTx)return hal?hal->awdlListenerTxRequest(p->data):kIOReturnNotReady;
            if(p->echoProfile)return hal?hal->awdlIbssEchoRequest(p->data,p->mode,p->echoProfile):kIOReturnNotReady;
            if(p->pair)return hal?hal->awdlListenerPairRequest(p->data,p->other,p->otherIp):kIOReturnNotReady;
            return hal?(p->passiveIbss?hal->awdlIbssReceiveOnlyRequest(p->data):(p->listener?hal->awdlListenerRequest(p->data):hal->awdlIbssDataRequest(p->data,p->cancel,p->mode))):kIOReturnNotReady;
        },&request);
    }
    auto dataProbe=root?root->getObject("AWDLSingleDataProbe"):nullptr;
    auto cancelProbe=root?root->getObject("AWDLSingleDataProbeCancel"):nullptr;
    if(dataProbe||cancelProbe){
        if(IOUserClient::clientHasPrivilege(current_task(),kIOClientPrivilegeAdministrator)!=kIOReturnSuccess)return kIOReturnNotPrivileged;
        if(root->getCount()!=1||!_fCommandGate)return kIOReturnBadArgument;
        if(cancelProbe){
            auto value=OSDynamicCast(OSNumber,cancelProbe);if(!value||value->unsigned64BitValue()!=1)return kIOReturnBadArgument;
            return _fCommandGate->runAction([](OSObject *owner,void*,void*,void*,void*)->IOReturn{
                ItlAwdl::gSingleDataProbe.cancel();auto self=static_cast<AirportItlwm *>(owner);
                auto hal=OSDynamicCast(ItlIwm,self->fHalService);if(hal)hal->awdlSingleDataProbePublish();return kIOReturnSuccess;
            });
        }
        auto d=OSDynamicCast(OSDictionary,dataProbe);if(!d||d->getCount()!=5)return kIOReturnBadArgument;
        auto version=OSDynamicCast(OSNumber,d->getObject("Version"));auto token=OSDynamicCast(OSNumber,d->getObject("Token"));
        auto peer=OSDynamicCast(OSData,d->getObject("PeerMAC"));auto peerIp=OSDynamicCast(OSData,d->getObject("PeerIPv6"));
        auto sourceIp=OSDynamicCast(OSData,d->getObject("SourceIPv6"));
        if(!version||version->unsigned64BitValue()!=1||!token||!token->unsigned64BitValue()||!peer||peer->getLength()!=6||
           !peerIp||peerIp->getLength()!=16||!sourceIp||sourceIp->getLength()!=16)return kIOReturnBadArgument;
        ItlAwdl::SingleDataRequest request;request.token=token->unsigned64BitValue();
        memcpy(request.peer,peer->getBytesNoCopy(),6);memcpy(request.peerIp,peerIp->getBytesNoCopy(),16);memcpy(request.sourceIp,sourceIp->getBytesNoCopy(),16);
        return _fCommandGate->runAction([](OSObject *owner,void *arg,void*,void*,void*)->IOReturn{
            auto self=static_cast<AirportItlwm *>(owner);auto request=static_cast<ItlAwdl::SingleDataRequest *>(arg);
            auto inf=self->fAWDLInterface;auto hal=OSDynamicCast(ItlIwm,self->fHalService);
            if(!inf||inf->isInactive()||!hal)return kIOReturnNotReady;
            if(!awdlOwnsProbeIPv6(request->sourceIp))return kIOReturnBadArgument;
            const uint64_t local=inf->getSelfMacAddr();if(!local)return kIOReturnNotReady;
            return hal->awdlArmSingleDataProbe(*request,reinterpret_cast<const uint8_t *>(&local));
        },&request);
    }
    auto arm=root?OSDynamicCast(OSNumber,root->getObject("AWDLArmValidation")):nullptr;
    if(arm){
        if(IOUserClient::clientHasPrivilege(current_task(),kIOClientPrivilegeAdministrator)!=kIOReturnSuccess)
            return kIOReturnNotPrivileged;
        if(root->getCount()!=1||arm->unsigned64BitValue()!=1)return kIOReturnBadArgument;
        auto hal=OSDynamicCast(ItlIwm,fHalService);
        return hal?hal->awdlArmValidation():kIOReturnUnsupported;
    }
    auto serviceObject=root?root->getObject("AWDLServiceSnapshot"):nullptr;
    if(serviceObject){
        if(IOUserClient::clientHasPrivilege(current_task(),kIOClientPrivilegeAdministrator)!=kIOReturnSuccess)
            return kIOReturnNotPrivileged;
        auto d=OSDynamicCast(OSDictionary,serviceObject);
        if(!d||root->getCount()!=1||d->getCount()!=7)return kIOReturnBadArgument;
        auto version=OSDynamicCast(OSNumber,d->getObject("Version"));
        auto source=OSDynamicCast(OSData,d->getObject("Source"));
        auto instance=OSDynamicCast(OSString,d->getObject("Instance"));
        auto host=OSDynamicCast(OSString,d->getObject("Host"));
        auto port=OSDynamicCast(OSNumber,d->getObject("Port"));
        auto txt=OSDynamicCast(OSData,d->getObject("TXT"));
        auto active=OSDynamicCast(OSBoolean,d->getObject("Active"));
        if(!version||version->unsigned64BitValue()!=1||!source||source->getLength()!=6||
           !instance||instance->getLength()>63||!host||host->getLength()>63||!port||
           port->unsigned64BitValue()>65535||!txt||txt->getLength()>256||!active)return kIOReturnBadArgument;
        struct Request { OSData *source;OSString *instance,*host;OSNumber *port;OSData *txt;bool active; } req={source,instance,host,port,txt,active->isTrue()};
        if(!_fCommandGate)return kIOReturnNotReady;
        return _fCommandGate->runAction([](OSObject *object,void *arg,void*,void*,void*)->IOReturn{
            auto self=static_cast<AirportItlwm *>(object);auto r=static_cast<Request *>(arg);
            auto inf=self->fAWDLInterface;
            if(!inf||inf->isInactive())return kIOReturnNotReady;
            const uint64_t local=inf->getSelfMacAddr();
            if(!local||memcmp(&local,r->source->getBytesNoCopy(),6))return kIOReturnBadArgument;
            ItlAwdl::TemplateService service={};
            uint64_t time,now;clock_get_uptime(&time);absolutetime_to_nanoseconds(time,&now);
            memcpy(service.source,&local,6);service.expiresNs=now+5000000000ULL;
            if(r->active){
                strlcpy(service.instance,r->instance->getCStringNoCopy(),sizeof(service.instance));
                strlcpy(service.host,r->host->getCStringNoCopy(),sizeof(service.host));
                service.port=r->port->unsigned32BitValue();service.txtLength=r->txt->getLength();
                if(service.txtLength)memcpy(service.txt,r->txt->getBytesNoCopy(),service.txtLength);service.expiresNs=now+5000000000ULL;
                if(!ItlAwdl::templateServiceValid(service,now))return kIOReturnBadArgument;
            }
            if(!ItlAwdl::templateLock())return kIOReturnBusy;
            ItlAwdl::gTemplateRuntime.service=service;ItlAwdl::templateUnlock();
            self->setProperty("AWDLRuntimeServiceActive",r->active);
            self->setProperty("AWDLRuntimeServiceInstance",r->instance);
            self->setProperty("AWDLRuntimeServiceHost",r->host);
            self->setProperty("AWDLRuntimeServicePort",r->port);
            return kIOReturnSuccess;
        },&req);
    }
    auto object=root?root->getObject("AWDLBatchCommand"):nullptr;
    if(!object)return super::setProperties(properties);
    if(IOUserClient::clientHasPrivilege(current_task(),kIOClientPrivilegeAdministrator)!=kIOReturnSuccess)
        return kIOReturnNotPrivileged;
    auto d=OSDynamicCast(OSDictionary,object);
    if(!d || root->getCount()!=1 || d->getCount()!=4)return kIOReturnBadArgument;
    const char *keys[]={"Version","Command","Run","Mode"};uint32_t v[4];
    for(unsigned i=0;i<4;++i) {
        auto n=OSDynamicCast(OSNumber,d->getObject(keys[i]));
        if(!n || n->unsigned64BitValue()>UINT32_MAX)return kIOReturnBadArgument;
        v[i]=n->unsigned32BitValue();
    }
    if(v[0]!=ItlIbss::BatchVersion)return kIOReturnUnsupported;
    auto hal=OSDynamicCast(ItlIwm,fHalService);
    return hal?hal->awdlBatchRequest(v[1],v[2],v[3]):kIOReturnUnsupported;
}

void AirportItlwm::stop(IOService *provider)
{
    awdlControlClose();
    if(awdlControlHasRunningJob()){
        bool defer=false;didTerminate(provider,0,&defer);
        setProperty("AWDLControlStopDeferred",true);return;
    }
    if(fAwdlControlPhysicalStop&&awdlControlHasJob()){setProperty("AWDLControlStopDeferred",true);return;}
    XYLog("%s\n", __PRETTY_FUNCTION__);XYLog("%s\n", __PRETTY_FUNCTION__);
    // IOKit normally terminates children before invoking this stop. Cover the
    // explicit-stop path too: clients must finalize before native resource stop.
    // HAL notifications and service updates use this same workloop gate.
    // Withdraw callbacks and transfer our reference before terminating outside
    // the gate: synchronous termination may need other workloop actions.
    IO80211VirtualInterface *awdlToStop = nullptr;
    if (_fCommandGate) {
        _fCommandGate->runAction([](OSObject *owner, void *arg, void *, void *, void *) -> IOReturn {
            auto self = static_cast<AirportItlwm *>(owner);
            ItlAwdl::gEngine.modeEnabled = 0;
            ItlAwdl::gSingleDataProbe.cancel();
            ItlAwdl::gEngine.availabilityWindowEvent = nullptr;
            ItlAwdl::gEngine.availabilityWindowContext = nullptr;
            *static_cast<IO80211VirtualInterface **>(arg) = self->fAWDLInterface;
            self->fAWDLInterface = nullptr;
            return kIOReturnSuccess;
        }, &awdlToStop);
    }
    if (awdlToStop) {
        if (!awdlToStop->isInactive())
            awdlToStop->terminate(kIOServiceRequired | kIOServiceSynchronous);
    }
    struct _ifnet *ifp = &fHalService->get80211Controller()->ic_ac.ac_if;
#ifdef AIRPORT_WCL
    if (fNetIf)
        static_cast<AirportItlwmSkywalkInterface *>(fNetIf)->stopWCL();
#endif
    super::stop(provider);
    disableAdapter(bsdInterface);
    setLinkStatus(kIONetworkLinkValid);
    fHalService->detach(pciNub);
    if(awdlControlHasJob())fAwdlControlPhysicalStop=true;
    // The physical HAL stop has now supplied DMA-stop evidence. Retry child
    // finalization while the command gate/workloop still exist.
    if(awdlToStop){
        auto interface=OSDynamicCast(ItlAwdlInterface,awdlToStop);
        const bool handed=awdlControlHandoffStop(awdlToStop);
        if(handed&&interface)interface->stopProbe(this);
        if(!handed||!awdlControlRetireStop()){
            setProperty("AWDLControlFinalStopQuarantined",true);
            if(watchdogTimer){watchdogTimer->cancelTimeout();watchdogTimer->disable();}
            if(scanSource){scanSource->cancelTimeout();scanSource->disable();}
            IOLockLock(fAwdlControlLock);
            if(fAwdlControlJob)fAwdlControlJob->stoppedChildReference=awdlToStop;
            IOLockUnlock(fAwdlControlLock);
            // Track the transferred child reference and retain all job resources. No repeat HAL detach.
            return;
        }
        OSSafeReleaseNULL(awdlToStop);
    }
    ether_ifdetach(ifp);
    detachInterface(fNetIf, true);
    OSSafeReleaseNULL(fNetIf);
    releaseAll();
}

void AirportItlwm::free()
{
    if(fAwdlControlLock){IOLockFree(fAwdlControlLock);fAwdlControlLock=nullptr;}
    XYLog("%s\n", __PRETTY_FUNCTION__);
    if (fHalService != NULL) {
        fHalService->release();
        fHalService = NULL;
    }
    if (syncFrameTemplate != NULL && syncFrameTemplateLength > 0) {
        IOFree(syncFrameTemplate, syncFrameTemplateLength);
        syncFrameTemplateLength = 0;
        syncFrameTemplate = NULL;
    }
    if (roamProfile != NULL) {
        IOFree(roamProfile, sizeof(struct apple80211_roam_profile_band_data));
        roamProfile = NULL;
    }
    if (btcProfile != NULL) {
        IOFree(btcProfile, sizeof(struct apple80211_btc_profiles_data));
        btcProfile = NULL;
    }
    super::free();
}

bool AirportItlwm::createWorkQueue()
{
    XYLog("%s %d\n", __FUNCTION__, _fWorkloop != 0);
    return _fWorkloop != 0;
}

#ifdef AIRPORT_WCL
IOWorkLoop *AirportItlwm::getWorkLoop() const
{
    return _fWorkloop;
}
#endif

IO80211WorkQueue *AirportItlwm::getWorkQueue()
#ifdef AIRPORT_WCL
const
#endif
{
    return _fWorkloop;
}

#ifdef AIRPORT_WCL
CommonFaultReporter *AirportItlwm::getFaultReporterFromDriver()
#else
void *AirportItlwm::getFaultReporterFromDriver()
#endif
{
    return driverFaultReporter;
}

// Probe49: read-only counters for the family's per-virtual-interface enable
// bookkeeping. super:: keeps the native behaviour unchanged.
SInt32 AirportItlwm::enableVirtualInterface(IO80211VirtualInterface *interface)
{
    static volatile SInt32 calls = 0;
    setProperty("AWDLTraceEnableVifCalls", static_cast<uint32_t>(OSIncrementAtomic(&calls)) + 1, 32);
    if (interface && interface->getMetaClass())
        setProperty("AWDLTraceEnableVifClass", interface->getMetaClass()->getClassName());
    return super::enableVirtualInterface(interface);
}

SInt32 AirportItlwm::disableVirtualInterface(IO80211VirtualInterface *interface)
{
    static volatile SInt32 calls = 0;
    setProperty("AWDLTraceDisableVifCalls", static_cast<uint32_t>(OSIncrementAtomic(&calls)) + 1, 32);
    if (interface && interface->getMetaClass())
        setProperty("AWDLTraceDisableVifClass", interface->getMetaClass()->getClassName());
    return super::disableVirtualInterface(interface);
}

IOReturn AirportItlwm::enable(IO80211SkywalkInterface *netif)
{
    // Probe49: which interface does the family ask the controller to enable?
    {
        static volatile SInt32 calls = 0;
        setProperty("AWDLTraceControllerEnableCalls", static_cast<uint32_t>(OSIncrementAtomic(&calls)) + 1, 32);
        if (netif && netif->getMetaClass())
            setProperty("AWDLTraceControllerEnableClass", netif->getMetaClass()->getClassName());
    }
    XYLog("%s\n", __PRETTY_FUNCTION__);
    super::enable(netif);
    _fCommandGate->enable();
    if (power_state)
        enableAdapter(bsdInterface);
    return kIOReturnSuccess;
}

IOReturn AirportItlwm::disable(IO80211SkywalkInterface *netif)
{
    {
        static volatile SInt32 calls = 0;
        setProperty("AWDLTraceControllerDisableCalls", static_cast<uint32_t>(OSIncrementAtomic(&calls)) + 1, 32);
        if (netif && netif->getMetaClass())
            setProperty("AWDLTraceControllerDisableClass", netif->getMetaClass()->getClassName());
    }
    XYLog("%s\n", __PRETTY_FUNCTION__);
    super::disable(netif);
    setLinkStatus(kIONetworkLinkValid);
    return kIOReturnSuccess;
}

bool AirportItlwm::configureInterface(IONetworkInterface *netif)
{
    IONetworkData *nd;
    struct _ifnet *ifp = &fHalService->get80211Controller()->ic_ac.ac_if;

    if (super::configureInterface(netif) == false) {
        XYLog("super failed\n");
        return false;
    }

    nd = netif->getParameter(kIONetworkStatsKey);
    if (!nd || !(fpNetStats = (IONetworkStats *)nd->getBuffer())) {
        XYLog("network statistics buffer unavailable?\n");
        return false;
    }
    ifp->netStat = fpNetStats;
    ether_ifattach(ifp, OSDynamicCast(IOEthernetInterface, netif));
    fpNetStats->collisions = 0;
#ifdef __PRIVATE_SPI__
#ifdef AIRPORT_WCL
    if (netif->configureOutputPullModel(fHalService->getDriverInfo()->getTxQueueSize(),
        kIONetworkWorkLoopSynchronous, 0,
        IOEthernetInterface::kOutputPacketSchedulingModelNormal, 0) != kIOReturnSuccess)
        return false;
#else
    netif->configureOutputPullModel(fHalService->getDriverInfo()->getTxQueueSize(), 0, 0, IOEthernetInterface::kOutputPacketSchedulingModelNormal, 0);
#endif
#endif

    return true;
}

IONetworkInterface *AirportItlwm::createInterface()
{
    AirportItlwmEthernetInterface *netif = new AirportItlwmEthernetInterface;
    if (!netif)
        return NULL;
    if (!netif->initWithSkywalkInterfaceAndProvider(this, fNetIf)) {
        netif->release();
        return NULL;
    }
    return netif;
}

bool AirportItlwm::createMediumTables(const IONetworkMedium **primary)
{
    IONetworkMedium    *medium;

    OSDictionary *mediumDict = OSDictionary::withCapacity(2);
    if (mediumDict == NULL) {
        XYLog("Cannot allocate OSDictionary\n");
        return false;
    }

    medium = IONetworkMedium::medium(kIOMediumIEEE80211, 54000000);
    IONetworkMedium::addMedium(mediumDict, medium);
    medium->release();
    if (primary) {
        *primary = medium;
    }
    medium = IONetworkMedium::medium(kIOMediumIEEE80211None, 0);
    IONetworkMedium::addMedium(mediumDict, medium);
    medium->release();

    bool result = publishMediumDictionary(mediumDict);
    if (!result) {
        XYLog("Cannot publish medium dictionary!\n");
    }

    mediumDict->release();
    return result;
}

IOReturn AirportItlwm::selectMedium(const IONetworkMedium *medium) {
    setSelectedMedium(medium);
    return kIOReturnSuccess;
}

bool AirportItlwm::
setLinkStatus(UInt32 status, const IONetworkMedium * activeMedium, UInt64 speed, OSData * data)
{
    struct _ifnet *ifq = &fHalService->get80211Controller()->ic_ac.ac_if;
    if (status == currentStatus) {
        return true;
    }
    bool ret = super::setLinkStatus(status, activeMedium, speed, data);
    currentStatus = status;
    if (fNetIf) {
        if (status & kIONetworkLinkActive) {
#ifdef __PRIVATE_SPI__
            bsdInterface->startOutputThread();
#endif
            getCommandGate()->runAction(setLinkStateGated, (void *)kIO80211NetworkLinkUp, (void *)0);
//            fNetIf->setLinkQualityMetric(100);
        } else if (!(status & kIONetworkLinkNoNetworkChange)) {
#ifdef __PRIVATE_SPI__
            bsdInterface->stopOutputThread();
            bsdInterface->flushOutputQueue();
#endif
            ifq_flush(&ifq->if_snd);
            mq_purge(&fHalService->get80211Controller()->ic_mgtq);
            getCommandGate()->runAction(setLinkStateGated, (void *)kIO80211NetworkLinkDown, (void *)fHalService->get80211Controller()->ic_deauth_reason);
        }
    }
    return ret;
}

IOReturn AirportItlwm::
setLinkStateGated(OSObject *target, void *arg0, void *arg1, void *arg2, void *arg3)
{
    AirportItlwm *that = OSDynamicCast(AirportItlwm, target);
#ifdef AIRPORT_WCL
    that->bsdInterface->setLinkState((IO80211LinkState)(uint64_t)arg0);
    static_cast<AirportItlwmSkywalkInterface *>(that->fNetIf)->handleWCLLink(
        (IO80211LinkState)(uint64_t)arg0 == kIO80211NetworkLinkUp);
    return kIOReturnSuccess;
#else
    IOReturn ret = that->fNetIf->setLinkState((IO80211LinkState)(uint64_t)arg0, (unsigned int)(uint64_t)arg1);
    that->fNetIf->setRunningState((IO80211LinkState)(uint64_t)arg0 == kIO80211NetworkLinkUp);
    that->fNetIf->postMessage(APPLE80211_M_LINK_CHANGED, NULL, 0, false);
    that->fNetIf->postMessage(APPLE80211_M_BSSID_CHANGED, NULL, 0, false);
    that->fNetIf->postMessage(APPLE80211_M_SSID_CHANGED, NULL, 0, false);
    if ((IO80211LinkState)(uint64_t)arg0 == kIO80211NetworkLinkUp) {
        that->fNetIf->reportLinkStatus(3, 0x80);
    } else {
        that->fNetIf->reportLinkStatus(1, 0);
    }
    that->bsdInterface->setLinkState((IO80211LinkState)(uint64_t)arg0);
    return ret;
#endif
}

#ifdef __PRIVATE_SPI__
IOReturn AirportItlwm::outputStart(IONetworkInterface *interface, IOOptionBits options)
{
    struct _ifnet *ifp = &fHalService->get80211Controller()->ic_ac.ac_if;
    mbuf_t m = NULL;
#ifdef AIRPORT_WCL
    if (!ifp->if_snd.queue)
        return kIOReturnNoResources;
    if (!ifq_empty(&ifp->if_snd))
        (*ifp->if_start)(ifp);
    const uint32_t capacity = ifp->if_snd.queue->getCapacity();
    const uint32_t queued = ifq_len(&ifp->if_snd);
    if (ifq_is_oactive(&ifp->if_snd) || queued >= capacity)
        return kIOReturnNoResources;
    const uint32_t count = MIN(32U, capacity - queued);
    if (interface->dequeueOutputPackets(count, &m) != kIOReturnSuccess || !m)
        return kIOReturnSuccess;
    while (m) {
        mbuf_t next = mbuf_nextpkt(m);
        mbuf_setnextpkt(m, NULL);
        enqueuePacket(m);
        m = next;
    }
    (*ifp->if_start)(ifp);
    if (ifq_is_oactive(&ifp->if_snd))
        return kIOReturnNoResources;
    return kIOReturnSuccess;
#else
    if (ifq_is_oactive(&ifp->if_snd))
        return kIOReturnNoResources;
    while (kIOReturnSuccess == interface->dequeueOutputPackets(1, &m)) {
        if (outputPacket(m, NULL)!= kIOReturnOutputSuccess ||
            ifq_is_oactive(&ifp->if_snd))
            return kIOReturnNoResources;
    }
    return kIOReturnSuccess;
#endif
}

IOReturn AirportItlwm::networkInterfaceNotification(
                    IONetworkInterface * interface,
                    uint32_t              type,
                    void *                  argument )
{
    XYLog("%s\n", __FUNCTION__);
    return kIOReturnSuccess;
}
#endif

extern const char* hexdump(uint8_t *buf, size_t len);

#ifdef AIRPORT_WCL
UInt32 AirportItlwm::outputPacket(mbuf_t m, void *param)
{
    IOReturn status = _fCommandGate ? _fCommandGate->runAction(outputPacketGated, m) : kIOReturnNotReady;
    if (status != kIOReturnOutputSuccess && status != kIOReturnOutputDropped) {
        if (m && mbuf_type(m) != MBUF_TYPE_FREE)
            freePacket(m);
        return kIOReturnOutputDropped;
    }
    return status;
}

IOReturn AirportItlwm::outputPacketGated(OSObject *owner, void *arg0, void *, void *, void *)
{
    auto self = static_cast<AirportItlwm *>(owner);
    UInt32 status = self->enqueuePacket(static_cast<mbuf_t>(arg0));
    auto ifp = &self->fHalService->get80211Controller()->ic_ac.ac_if;
    if (ifp->if_snd.queue)
        (*ifp->if_start)(ifp);
    return status;
}

UInt32 AirportItlwm::enqueuePacket(mbuf_t m)
{
    auto ic = fHalService->get80211Controller();
    auto ifp = &ic->ic_ac.ac_if;
    if (ic->ic_state == IEEE80211_S_RUN && ifp->if_snd.queue &&
        m && mbuf_type(m) != MBUF_TYPE_FREE && (mbuf_flags(m) & MBUF_PKTHDR) &&
        ifp->if_snd.queue->lockEnqueue(m))
        return kIOReturnOutputSuccess;
    if (ifp->netStat)
        ++ifp->netStat->outputErrors;
    if (m && mbuf_type(m) != MBUF_TYPE_FREE)
        freePacket(m);
    return kIOReturnOutputDropped;
}
#else
UInt32 AirportItlwm::outputPacket(mbuf_t m, void *param)
{
//    XYLog("%s\n", __FUNCTION__);
    IOReturn ret = kIOReturnOutputSuccess;
    struct _ifnet *ifp = &fHalService->get80211Controller()->ic_ac.ac_if;
    
    if (fHalService->get80211Controller()->ic_state != IEEE80211_S_RUN || ifp->if_snd.queue == NULL) {
        if (m && mbuf_type(m) != MBUF_TYPE_FREE)
            freePacket(m);
        return kIOReturnOutputDropped;
    }
    if (m == NULL) {
        XYLog("%s m==NULL!!\n", __FUNCTION__);
        ifp->netStat->outputErrors++;
        ret = kIOReturnOutputDropped;
    }
    if (!(mbuf_flags(m) & MBUF_PKTHDR) ){
        XYLog("%s pkthdr is NULL!!\n", __FUNCTION__);
        ifp->netStat->outputErrors++;
        freePacket(m);
        ret = kIOReturnOutputDropped;
    }
    if (mbuf_type(m) == MBUF_TYPE_FREE) {
        XYLog("%s mbuf is FREE!!\n", __FUNCTION__);
        ifp->netStat->outputErrors++;
        ret = kIOReturnOutputDropped;
    }
    size_t len = mbuf_len(m);
    ether_header_t *eh = (ether_header_t *)mbuf_data(m);
    if (len >= sizeof(ether_header_t) && eh->ether_type == htons(ETHERTYPE_PAE)) { // EAPOL packet
        const char* dump = hexdump((uint8_t*)mbuf_data(m), len);
        XYLog("output EAPOL packet, len: %zu, data: %s\n", len, dump ? dump : "Failed to allocate memory");
        if (dump)
            IOFree((void*)dump, 3 * len + 1);
    }
    if (!ifp->if_snd.queue->lockEnqueue(m)) {
        freePacket(m);
        ret = kIOReturnOutputDropped;
    }
    (*ifp->if_start)(ifp);
    return ret;
}
#endif

const OSString * AirportItlwm::newVendorString() const
{
    return OSString::withCString("Apple");
}

const OSString * AirportItlwm::newModelString() const
{
    return OSString::withCString(fHalService->getDriverInfo()->getFirmwareName());
}

IOReturn AirportItlwm::getHardwareAddress(IOEthernetAddress *addrP)
{
    if (IEEE80211_ADDR_EQ(etheranyaddr, fHalService->get80211Controller()->ic_myaddr))
        return kIOReturnError;
    else {
        IEEE80211_ADDR_COPY(addrP, fHalService->get80211Controller()->ic_myaddr);
        return kIOReturnSuccess;
    }
}

IOReturn AirportItlwm::setHardwareAddress(const void *addrP, UInt32 addrBytes)
{
    if (!fNetIf || !addrP)
        return kIOReturnError;
#ifdef AIRPORT_WCL
    const UInt8 *address = static_cast<const UInt8 *>(addrP);
    auto ic = fHalService->get80211Controller();
    bool initializing = (ic->ic_if.if_flags & IFF_UP) && !(ic->ic_if.if_flags & IFF_RUNNING);
    auto update = AirportWCL::planMacUpdate(ic->ic_myaddr, address, addrBytes, initializing);
    auto sourceHal=OSDynamicCast(ItlIwm,fHalService);
    if(sourceHal)sourceHal->awdlIbssLifecycleSource(ItlIbss::LifeCode::MacDecision,uint32_t(update));
    if (update == AirportWCL::MacUpdate::Invalid)
        return kIOReturnBadArgument;
    if (update == AirportWCL::MacUpdate::Busy) {
        return kIOReturnBusy;
    }
    if (!bsdInterface || !bsdInterface->getIfnet())
        return kIOReturnNotReady;
    // WCL calls this directly, bypassing IOEthernetInterface's normal MAC ioctl.
    // Update the BSD address used by DHCP/ARP as well as the firmware address.
    errno_t error = ifnet_set_lladdr(bsdInterface->getIfnet(), address, addrBytes);
    if (error)
        return kIOReturnError;
    bsdInterface->setProperty(kIOMACAddress, const_cast<UInt8 *>(address), addrBytes);
    // WCL replays the current address after every driver-available notification.
    // Keep BSD synchronized, but do not restart firmware for a replay.
    if (update == AirportWCL::MacUpdate::Unchanged) {
        return kIOReturnSuccess;
    }
#endif
    if_setlladdr(&fHalService->get80211Controller()->ic_ac.ac_if, (const UInt8 *)addrP);
#ifdef AIRPORT_WCL
    if (ic->ic_if.if_flags & IFF_RUNNING) {
        if(sourceHal)sourceHal->awdlIbssLifecycleSource(ItlIbss::LifeCode::MacRestart);
        ic->ic_wcl_mac_reconfig = true;
#else
    if (fHalService->get80211Controller()->ic_state > IEEE80211_S_INIT) {
#endif
        fHalService->disable(bsdInterface);
        fHalService->enable(bsdInterface);
    }
    return kIOReturnSuccess;
}

UInt32 AirportItlwm::getFeatures() const
{
    return fHalService->getDriverInfo()->supportedFeatures();
}

IOReturn AirportItlwm::setPromiscuousMode(IOEnetPromiscuousMode mode)
{
    return kIOReturnSuccess;
}

IOReturn AirportItlwm::setMulticastMode(IOEnetMulticastMode mode)
{
    return kIOReturnSuccess;
}

IOReturn AirportItlwm::setMulticastList(IOEthernetAddress* addr, UInt32 len)
{
    return fHalService->getDriverController()->setMulticastList(addr, len);
}

IOReturn AirportItlwm::getPacketFilters(const OSSymbol *group, UInt32 *filters) const
{
    IOReturn    rtn = kIOReturnSuccess;
    if (group == gIOEthernetWakeOnLANFilterGroup && magicPacketSupported)
        *filters = kIOEthernetWakeOnMagicPacket;
    else if (group == gIONetworkFilterGroup)
        *filters = kIOPacketFilterMulticast | kIOPacketFilterPromiscuous;
    else
        rtn = IOEthernetController::getPacketFilters(group, filters);
    return rtn;
}

SInt32 AirportItlwm::
enableFeature(IO80211FeatureCode code, void *data)
{
    if (code == kIO80211Feature80211n) {
        return 0;
    }
    return 102;
}

bool AirportItlwm::getLogPipes(CCPipe**logPipe, CCPipe**eventPipe, CCPipe**snapshotsPipe)
{
    bool ret = false;
    if (logPipe) {
        *logPipe = driverLogPipe;
        ret = true;
    }
    if (eventPipe) {
        *eventPipe = driverDataPathPipe;
        ret = true;
    }
    if (snapshotsPipe) {
        *snapshotsPipe = driverSnapshotsPipe;
        ret = true;
    }
    return ret;
}

#define APPLE80211_CAPA_AWDL_FEATURE_AUTO_UNLOCK    0x00000004
#define APPLE80211_CAPA_AWDL_FEATURE_WOW            0x00000080

IOReturn AirportItlwm::
getCARD_CAPABILITIES(OSObject *object,
                                     struct apple80211_capability_data *cd)
{
    uint32_t caps = fHalService->get80211Controller()->ic_caps;
    memset(cd, 0, sizeof(struct apple80211_capability_data));

    if (caps & IEEE80211_C_WEP)
        cd->capabilities[0] |= 1 << APPLE80211_CAP_WEP;
    if (caps & IEEE80211_C_RSN)
        cd->capabilities[0] |= 1 << APPLE80211_CAP_TKIP | 1 << APPLE80211_CAP_AES_CCM;
    // Disable not implemented capabilities
    // if (caps & IEEE80211_C_PMGT)
    //     cd->capabilities[0] |= 1 << APPLE80211_CAP_PMGT;
    // if (caps & IEEE80211_C_IBSS)
    //     cd->capabilities[0] |= 1 << APPLE80211_CAP_IBSS;
    // if (caps & IEEE80211_C_HOSTAP)
    //     cd->capabilities[0] |= 1 << APPLE80211_CAP_HOSTAP;
    // AES not enabled, like on Apple cards

    if (caps & IEEE80211_C_SHSLOT)
        cd->capabilities[1] |= 1 << (APPLE80211_CAP_SHSLOT - 8);
    if (caps & IEEE80211_C_SHPREAMBLE)
        cd->capabilities[1] |= 1 << (APPLE80211_CAP_SHPREAMBLE - 8);
    if (caps & IEEE80211_C_RSN)
        cd->capabilities[1] |= 1 << (APPLE80211_CAP_WPA1 - 8) | 1 << (APPLE80211_CAP_WPA2 - 8) | 1 << (APPLE80211_CAP_TKIPMIC - 8);
    // Disable not implemented capabilities
    // if (caps & IEEE80211_C_TXPMGT)
    //     cd->capabilities[1] |= 1 << (APPLE80211_CAP_TXPMGT - 8);
    // if (caps & IEEE80211_C_MONITOR)
    //     cd->capabilities[1] |= 1 << (APPLE80211_CAP_MONITOR - 8);
    // WPA not enabled, like on Apple cards

    cd->version = APPLE80211_VERSION;
#ifdef AIRPORT_WCL
    // WPA2 can use TKIP for group traffic even when unicast uses CCMP.
    // net80211 handles TKIP/MIC in software; hiding it makes Apple80211
    // reject these networks before it submits an association request.
    cd->capabilities[0] = (caps & IEEE80211_C_RSN) ?
        (1 << APPLE80211_CAP_AES_CCM) | (1 << APPLE80211_CAP_TKIP) : 0;
    cd->capabilities[1] &= ~(1 << (APPLE80211_CAP_WPA1 - 8));
    cd->capabilities[5] = 0x40; // Scan completion events.
    // Sequoia 15.8.1 wifip2pd: the AWDL creation caller passes role 6 and
    // capability index 0x20 to the gate before command 94. Advertise only
    // this entry point in the explicitly enabled probe. Protocol methods and
    // data-path enable continue to return unsupported until implemented.
    int awdlProbeFlag = 0;
    const bool awdlCreationGate = PE_parse_boot_argn(
        "-itlwm_awdl_probe", &awdlProbeFlag, sizeof(awdlProbeFlag));
    if (awdlCreationGate)
        cd->capabilities[0x20 / 8] |= 1u << (0x20 % 8);
    setProperty("AWDLTraceCreationCapabilityEnabled", awdlCreationGate);
    cd->capabilities[2] = (1 << (APPLE80211_CAP_WME - 16)) |
        (1 << (APPLE80211_CAP_SHORT_GI_40MHZ - 16)) |
        (1 << (APPLE80211_CAP_SHORT_GI_20MHZ - 16));
    // Tahoe WCLDeviceConfiguration::isHeSupported reads byte 17, bit 7.
    // WCL uses this capability together with the BSS HE IE for PHY reporting.
    if (caps & IEEE80211_C_HE)
        cd->capabilities[17] |= 0x80;
#else
    cd->capabilities[2] = 0xFF; // BURST, WME, SHORT_GI_40MHZ, SHORT_GI_20MHZ, WOW, TSN, ?, ?
    cd->capabilities[3] = 0x2B;
    cd->capabilities[5] = 0x40;
    cd->capabilities[6] = (
//                           1 |    //MFP capable
                           0x8 |
                           0x4 |
                           0x80
                           );
    *(uint16_t *)&cd->capabilities[8] = 0x201;
//
//    cd->capabilities[2] |= 0x10;
//    cd->capabilities[5] |= 0x1;
//
//    cd->capabilities[2] |= 0x2;
//
//    cd->capabilities[3] |= 0x20;
//
//    cd->capabilities[0] |= 0x80;
//
//    cd->capabilities[3] |= 0x80;
//    cd->capabilities[4] |= 0x4;
//
//    cd->capabilities[4] |= 0x1;
//    cd->capabilities[3] |= 0x1;
//    cd->capabilities[6] |= 0x8;
//
//    cd->capabilities[3] |= 3;
//    cd->capabilities[4] |= 2;
//    cd->capabilities[6] |= 0x10;
//    cd->capabilities[5] |= 0x20;
//    cd->capabilities[5] |= 0x80;
//
//    if (cd->capabilities[6] & 0x20) {
//        cd->capabilities[2] |= 8;
//    }
//    cd->capabilities[5] |= 8;
//    cd->capabilities[8] |= 2;
//
//    cd->capabilities[11] |= (2 | 4 | 8 | 0x10 | 0x20 | 0x40 | 0x80);

#endif
#ifdef AIRPORT_WCL
    static volatile SInt32 queryCount = 0;
    const uint32_t count = static_cast<uint32_t>(OSIncrementAtomic(&queryCount)) + 1;
    setProperty("AWDLTraceCardCapabilityCalls", static_cast<unsigned long long>(count), 32);
    setProperty("AWDLTraceCardCapabilities", cd->capabilities, sizeof(cd->capabilities));
    if (object)
        setProperty("AWDLTraceCardCapabilityObject", object->getMetaClass()->getClassName());
#endif

    return kIOReturnSuccess;
}

IOReturn AirportItlwm::
getDRIVER_VERSION(OSObject *object,
                                  struct apple80211_version_data *hv)
{
    if (!hv)
        return kIOReturnError;
    hv->version = APPLE80211_VERSION;
    snprintf(hv->string, sizeof(hv->string), "itlwm: %s%s fw: %s", ITLWM_VERSION, GIT_COMMIT, fHalService->getDriverInfo()->getFirmwareVersion());
    hv->string_len = strlen(hv->string);
    return kIOReturnSuccess;
}

IOReturn AirportItlwm::
getHARDWARE_VERSION(OSObject *object,
                                    struct apple80211_version_data *hv)
{
    if (!hv)
        return kIOReturnError;
    hv->version = APPLE80211_VERSION;
    strncpy(hv->string, fHalService->getDriverInfo()->getFirmwareVersion(), sizeof(hv->string));
    hv->string_len = strlen(fHalService->getDriverInfo()->getFirmwareVersion());
    return kIOReturnSuccess;
}

IOReturn AirportItlwm::
getCOUNTRY_CODE(OSObject *object,
                                struct apple80211_country_code_data *cd)
{
    char user_override_cc[3];
    const char *cc_fw = fHalService->getDriverInfo()->getFirmwareCountryCode();

    if (!cd)
        return kIOReturnError;
    cd->version = APPLE80211_VERSION;
    memset(user_override_cc, 0, sizeof(user_override_cc));
    PE_parse_boot_argn("itlwm_cc", user_override_cc, 3);
    /* user_override_cc > firmware_cc > geo_location_cc */
    strncpy((char*)cd->cc, user_override_cc[0] ? user_override_cc : ((cc_fw[0] == 'Z' && cc_fw[1] == 'Z' && geo_location_cc[0]) ? geo_location_cc : cc_fw), sizeof(cd->cc));
    return kIOReturnSuccess;
}

IOReturn AirportItlwm::
setCOUNTRY_CODE(OSObject *object, struct apple80211_country_code_data *data)
{
#ifdef AIRPORT_WCL
    if (!data)
        return kIOReturnBadArgument;
    if (data->cc[0] != 'x' && data->cc[0] != 'X' &&
        memcmp(geo_location_cc, data->cc, sizeof(geo_location_cc)) != 0) {
        memcpy(geo_location_cc, data->cc, sizeof(geo_location_cc));
        if (fNetIf)
            static_cast<AirportItlwmSkywalkInterface *>(fNetIf)->handleWCLEvent(
                IEEE80211_EVT_COUNTRY_CODE_UPDATE, nullptr);
    }
#else
    XYLog("%s cc=%s\n", __FUNCTION__, data->cc);
    if (data && data->cc[0] != 120 && data->cc[0] != 88) {
        memcpy(geo_location_cc, data->cc, sizeof(geo_location_cc));
        fNetIf->postMessage(APPLE80211_M_COUNTRY_CODE_CHANGED, NULL, 0, 0);
    }
#endif
    return kIOReturnSuccess;
}

IOReturn AirportItlwm::
getPOWER(OSObject *object,
                         struct apple80211_power_data *pd)
{
    if (!pd)
        return kIOReturnError;
    pd->version = APPLE80211_VERSION;
    pd->num_radios = 4;
    pd->power_state[0] = power_state;
    pd->power_state[1] = power_state;
    pd->power_state[2] = power_state;
    pd->power_state[3] = power_state;
    return kIOReturnSuccess;
}

IOReturn AirportItlwm::
setPOWER(OSObject *object,
                         struct apple80211_power_data *pd)
{
#ifdef AIRPORT_WCL
    if (!pd)
        return kIOReturnError;
    IOLog("itlwm: setPOWER: num_radios[%d]  power_state(0:%u  1:%u  2:%u  3:%u)\n", pd->num_radios, pd->power_state[0], pd->power_state[1], pd->power_state[2], pd->power_state[3]);
    if (pd->num_radios > 0) {
        bool isRunning = (fHalService->get80211Controller()->ic_ac.ac_if.if_flags & IFF_RUNNING) != 0;
        if (pd->power_state[0] == 0) {
            fHalService->get80211Controller()->ic_wcl_mac_reconfig = false;
            changePowerStateToPriv(kPowerStateOff);
            if (fHalService->get80211Controller()->ic_ac.ac_if.if_flags & (IFF_UP | IFF_RUNNING)) {
                net80211_ifstats(fHalService->get80211Controller());
                disableAdapter(bsdInterface);
            }
        } else {
            changePowerStateToPriv(kPowerStateOn);
            if (!isRunning)
                enableAdapter(bsdInterface);
        }
        power_state = (pd->power_state[0]);
    }

    return kIOReturnSuccess;
#else
    if (!pd)
        return kIOReturnError;
    IOLog("itlwm: setPOWER: num_radios[%d]  power_state(0:%u  1:%u  2:%u  3:%u)\n", pd->num_radios, pd->power_state[0], pd->power_state[1], pd->power_state[2], pd->power_state[3]);
    if (pd->num_radios > 0) {
        bool isRunning = (fHalService->get80211Controller()->ic_ac.ac_if.if_flags & (IFF_UP | IFF_RUNNING)) != 0;
        if (pd->power_state[0] == 0) {
            changePowerStateToPriv(1);
            if (isRunning) {
                net80211_ifstats(fHalService->get80211Controller());
                disableAdapter(bsdInterface);
            }
        } else {
            changePowerStateToPriv(2);
            if (!isRunning)
                enableAdapter(bsdInterface);
        }
        power_state = (pd->power_state[0]);
    }

    return kIOReturnSuccess;
#endif
}

SInt32 AirportItlwm::apple80211_ioctl(IO80211SkywalkInterface *interface,unsigned long cmd,void *data, bool b1, bool b2)
{
    if (!ml_at_interrupt_context())
        XYLog("%s cmd: %s b1: %d b2: %d\n", __FUNCTION__, convertApple80211IOCTLToString((unsigned int)cmd), b1, b2);
#ifdef AIRPORT_WCL
    return kIOReturnUnsupported;
#else
    return super::apple80211_ioctl(interface, cmd, data, b1, b2);
#endif
}

SInt32 AirportItlwm::apple80211SkywalkRequest(UInt request,int cmd,IO80211SkywalkInterface *interface,void *data)
{
    if (!ml_at_interrupt_context())
        XYLog("%s 1 cmd: %s request: %d\n", __FUNCTION__, convertApple80211IOCTLToString(cmd), request);
    return kIOReturnUnsupported;
}

SInt32 AirportItlwm::apple80211SkywalkRequest(UInt request,int cmd,IO80211SkywalkInterface *interface,void *data,void *)
{
    if (!ml_at_interrupt_context())
        XYLog("%s 2 cmd: %s request: %d\n", __FUNCTION__, convertApple80211IOCTLToString(cmd), request);
    return kIOReturnUnsupported;
}

IOReturn AirportItlwm::enableAdapter(IONetworkInterface *netif)
{
#ifdef AIRPORT_WCL
    IOReturn status = fHalService->enable(netif);
    if (status != kIOReturnSuccess)
        return status;
#else
    fHalService->enable(netif);
#endif
    watchdogTimer->setTimeoutMS(kWatchDogTimerPeriod);
    watchdogTimer->enable();
    return kIOReturnSuccess;
}

void AirportItlwm::disableAdapter(IONetworkInterface *netif)
{
    watchdogTimer->cancelTimeout();
    watchdogTimer->disable();
    fHalService->disable(netif);
}

IOReturn AirportItlwm::
tsleepHandler(OSObject* owner, void* arg0, void* arg1, void* arg2, void* arg3)
{
    AirportItlwm* dev = OSDynamicCast(AirportItlwm, owner);
    if (dev == 0)
        return kIOReturnError;

    if (arg1 == 0) {
        if (_fCommandGate->commandSleep(arg0, THREAD_INTERRUPTIBLE) == THREAD_AWAKENED)
            return kIOReturnSuccess;
        else
            return kIOReturnTimeout;
    } else {
        AbsoluteTime deadline;
        clock_interval_to_deadline((*(int*)arg1), kNanosecondScale, reinterpret_cast<uint64_t*> (&deadline));
        if (_fCommandGate->commandSleep(arg0, deadline, THREAD_INTERRUPTIBLE) == THREAD_AWAKENED)
            return kIOReturnSuccess;
        else
            return kIOReturnTimeout;
    }
}

bool AirportItlwm::initPCIPowerManagment(IOPCIDevice *provider)
{
    UInt16 reg16;

    reg16 = provider->configRead16(kIOPCIConfigCommand);

    reg16 |= ( kIOPCICommandBusMaster       |
               kIOPCICommandMemorySpace     |
               kIOPCICommandMemWrInvalidate );

    reg16 &= ~kIOPCICommandIOSpace;  // disable I/O space

    provider->configWrite16( kIOPCIConfigCommand, reg16 );
    provider->findPCICapability(kIOPCIPowerManagementCapability,
                                &pmPCICapPtr);
    if (pmPCICapPtr) {
        UInt16 pciPMCReg = provider->configRead32( pmPCICapPtr ) >> 16;
        if (pciPMCReg & kPCIPMCPMESupportFromD3Cold)
            magicPacketSupported = true;
        provider->configWrite16((pmPCICapPtr + 4), 0x8000 );
        IOSleep(10);
    }
    return true;
}

static IOPMPowerState powerStateArray[kPowerStateCount] =
{
    {1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
    {1, kIOPMDeviceUsable, kIOPMPowerOn, kIOPMPowerOn, 0, 0, 0, 0, 0, 0, 0, 0}
};

void AirportItlwm::unregistPM()
{
    if (powerOffThreadCall) {
        thread_call_free(powerOffThreadCall);
        powerOffThreadCall = NULL;
    }
    if (powerOnThreadCall) {
        thread_call_free(powerOnThreadCall);
        powerOnThreadCall = NULL;
    }
}

IOReturn AirportItlwm::setPowerState(unsigned long powerStateOrdinal, IOService *policyMaker)
{
    IOReturn result = IOPMAckImplied;

    if (pmPowerState == powerStateOrdinal)
        return result;
    switch (powerStateOrdinal) {
        case kPowerStateOff:
            if (powerOffThreadCall) {
                retain();
                if (thread_call_enter(powerOffThreadCall))
                    release();
                result = 5000000;
            }
            break;
        case kPowerStateOn:
            if (powerOnThreadCall) {
                retain();
                if (thread_call_enter(powerOnThreadCall))
                    release();
                result = 5000000;
            }
            break;

        default:
            break;
    }
    return result;
}

IOReturn AirportItlwm::setWakeOnMagicPacket(bool active)
{
    magicPacketEnabled = active;
    return kIOReturnSuccess;
}

static void handleSetPowerStateOff(thread_call_param_t param0,
                             thread_call_param_t param1)
{
    AirportItlwm *self = (AirportItlwm *)param0;

    if (param1 == 0)
    {
        self->getCommandGate()->runAction((IOCommandGate::Action)
                                           handleSetPowerStateOff,
                                           (void *) 1);
    }
    else
    {
        self->setPowerStateOff();
        self->release();
    }
}

static void handleSetPowerStateOn(thread_call_param_t param0,
                            thread_call_param_t param1)
{
    AirportItlwm *self = (AirportItlwm *) param0;

    if (param1 == 0)
    {
        self->getCommandGate()->runAction((IOCommandGate::Action)
                                           handleSetPowerStateOn,
                                           (void *) 1);
    }
    else
    {
        self->setPowerStateOn();
        self->release();
    }
}

IOReturn AirportItlwm::registerWithPolicyMaker(IOService *policyMaker)
{
    IOReturn ret;

    pmPowerState = kPowerStateOn;
    pmPolicyMaker = policyMaker;

    powerOffThreadCall = thread_call_allocate(
                                            (thread_call_func_t)handleSetPowerStateOff,
                                            (thread_call_param_t)this);
    powerOnThreadCall  = thread_call_allocate(
                                            (thread_call_func_t)handleSetPowerStateOn,
                                              (thread_call_param_t)this);
    ret = pmPolicyMaker->registerPowerDriver(this,
                                             powerStateArray,
                                             kPowerStateCount);
    return ret;
}

void AirportItlwm::setPowerStateOff()
{
    XYLog("%s\n", __FUNCTION__);
    pmPowerState = kPowerStateOff;
    disableAdapter(bsdInterface);
    pmPolicyMaker->acknowledgeSetPowerState();
}

void AirportItlwm::setPowerStateOn()
{
    XYLog("%s\n", __FUNCTION__);
    pmPowerState = kPowerStateOn;
    pmPolicyMaker->acknowledgeSetPowerState();
}
