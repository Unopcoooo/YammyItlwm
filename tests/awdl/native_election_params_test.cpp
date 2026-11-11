#include "NativeElectionParams.hpp"
#include "NativeDiscoverySession.hpp"
#include "NativeSearchOwner.hpp"
#include <assert.h>
#include <stdint.h>
#include <string.h>

static void put32(uint8_t *p, uint32_t value) {
    for (unsigned i = 0; i < 4; ++i) p[i] = uint8_t(value >> (8 * i));
}

int main() {
    const uint8_t master[6] = {0x32, 0x87, 0x58, 0xbd, 0x8c, 0x58};
    uint8_t raw[ItlAwdl::kNativeElectionParamsSize] = {};
    memcpy(raw + 0x16, master, sizeof(master));
    put32(raw + 0x1c, 535);
    put32(raw + 0x20, 535);
    ItlAwdl::NativeElectionParams parsed;
    assert(ItlAwdl::parseNativeElectionParams(raw, sizeof(raw), parsed));
    assert(!memcmp(parsed.master, master, sizeof(master)));
    assert(parsed.masterMetric == 535 && parsed.selfMetric == 535);
    assert(!ItlAwdl::parseNativeElectionParams(raw, sizeof(raw) - 1, parsed));

    raw[0x16] |= 1;
    assert(!ItlAwdl::parseNativeElectionParams(raw, sizeof(raw), parsed));
    raw[0x16] &= 0xfe;
    put32(raw + 0x1c, 0);
    assert(!ItlAwdl::parseNativeElectionParams(raw, sizeof(raw), parsed));

    uint8_t metricWire[ItlAwdl::kNativeElectionMetricSize] = {};
    put32(metricWire + 4, 65);
    metricWire[8] = 1;
    ItlAwdl::NativeElectionMetric metric;
    assert(ItlAwdl::parseNativeElectionMetric(metricWire, sizeof(metricWire), metric));
    assert(metric.metric == 65 && metric.overrideMetric);
    assert(!ItlAwdl::parseNativeElectionMetric(metricWire, sizeof(metricWire) - 1, metric));
    put32(metricWire + 4, 0);
    assert(!ItlAwdl::parseNativeElectionMetric(metricWire, sizeof(metricWire), metric));

    ItlAwdl::NativeSearchOwner search;
    search.epoch = 7;
    search.configuredOnce = true;
    search.phase = ItlAwdl::NativeSearchOwner::Phase::Failed;
    search.failure = EIO;
    assert(ItlAwdl::cleanFailedSearchRetired(search, true, true));
    assert(!ItlAwdl::cleanFailedSearchRetired(search, false, true));
    assert(!ItlAwdl::cleanFailedSearchRetired(search, true, false));
    search.stopRequested = true;
    assert(!ItlAwdl::cleanFailedSearchRetired(search, true, true));
    search.stopRequested = false;
    search.failure = 0;
    assert(!ItlAwdl::cleanFailedSearchRetired(search, true, true));

    ItlAwdl::NativeDiscoverySession discovery;
    discovery.used = true;
    discovery.epoch = 7;
    discovery.closed = true;
    assert(discovery.canRearmData(7));
    assert(!discovery.canRearm(7));
    discovery.controlRetired = true;
    assert(discovery.canRearm(7));
    return 0;
}
