// Native Sequoia 15.8.1 factories. Keep queue/pool classes opaque: the legacy
// local headers do not describe their current vtables or allocation sizes.
#ifndef ItlAwdlResources_hpp
#define ItlAwdlResources_hpp
class IOSkywalkTxSubmissionQueue;
class IOSkywalkRxCompletionQueue;
class IOSkywalkPacket;
struct ItlPoolOptions {
    uint32_t packetCount, bufferCount, bufferSize, maxBuffersPerPacket;
    uint32_t memorySegmentSize, poolFlags;
    uint64_t reserved;
};
static_assert(sizeof(ItlPoolOptions) == 0x20, "Native pool options ABI");
using ItlTxAction = unsigned (*)(OSObject *, IOSkywalkTxSubmissionQueue *, IOSkywalkPacket *const *, unsigned, void *);
using ItlRxAction = unsigned (*)(OSObject *, IOSkywalkRxCompletionQueue *, IOSkywalkPacket **, unsigned, void *);
extern "C" OSObject *ItlNativePool(const char *, OSObject *, unsigned, const ItlPoolOptions *)
    __asm__("__ZN25IOSkywalkPacketBufferPool8withNameEPKcP8OSObjectjPKNS_11PoolOptionsE");
extern "C" OSObject *ItlNativeTxQueue(IOSkywalkPacketBufferPool *, unsigned, unsigned, OSObject *, ItlTxAction, void *, unsigned)
    __asm__("__ZN26IOSkywalkTxSubmissionQueue8withPoolEP25IOSkywalkPacketBufferPooljjP8OSObjectPFjS3_PS_PKP15IOSkywalkPacketjPvES9_j");
extern "C" OSObject *ItlNativeRxQueue(IOSkywalkPacketBufferPool *, unsigned, unsigned, OSObject *, ItlRxAction, void *, unsigned)
    __asm__("__ZN26IOSkywalkRxCompletionQueue8withPoolEP25IOSkywalkPacketBufferPooljjP8OSObjectPFjS3_PS_PP15IOSkywalkPacketjPvES8_j");
#endif
