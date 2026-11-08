#include "../../itlwm/awdl/ChannelSequence.hpp"
#include "../../itlwm/awdl/WindowModel.hpp"
#include <assert.h>
#include <initializer_list>
#include <stdio.h>
using namespace ItlAwdl;
int main() {
    for (unsigned encoding : {0u, 1u, 3u}) {
        uint8_t storage[44] = {};
        uint8_t *p = storage + 1; // deliberately unaligned
        p[0] = 15; p[1] = encoding; p[3] = 3; p[4] = p[5] = 255;
        unsigned width = encoding == 0 ? 1 : 2;
        for (unsigned i = 0; i < 16; ++i) {
            p[6 + i * width + (encoding == 1 ? 1 : 0)] = i + 1;
            if (width == 2) p[6 + i * width + (encoding == 1 ? 0 : 1)] = 0x51;
        }
        ChannelSequence result; size_t used;
        const unsigned length = 6 + 16 * width;
        for (unsigned n = 0; n < length; ++n) {
            assert(decodeChannelSequence(p, n, result, used) == ChannelDecode::Truncated);
            assert(used == 0 && result.count == 0);
        }
        assert(decodeChannelSequence(p, length + 3, result, used) == ChannelDecode::Valid);
        assert(used == length && result.count == 16 && result.steps == 4 && result.fill == 65535);
        for (unsigned i = 0; i < 16; ++i) {
            assert(result.entries[i].channel == i + 1);
            assert(result.entries[i].auxiliary == (width == 1 ? 0 : 0x51));
        }
        p[0] = 255;
        assert(decodeChannelSequence(p, length, result, used) == ChannelDecode::Unsupported);
        p[0] = 15; p[1] = 2;
        assert(decodeChannelSequence(p, length, result, used) == ChannelDecode::Unsupported);
    }
    ChannelSequence channels; size_t used;
    assert(decodeChannelSequence(nullptr, 100, channels, used) == ChannelDecode::Truncated);
    SyncParameters s = {}; s.awPeriod = 16; s.presenceMode = 4;
    s.txDownCounter = 32; s.nextAwSequence = 65534;
    WindowPrediction w;
    assert(predictWindow(s, 1000, 1000, 1000000, w));
    assert(w.nextBoundaryGp2 == 33768 && w.nextBoundarySequence == 0);
    assert(predictWindow(s, 1000, 33767, 1000000, w) && w.waitMicroseconds == 1);
    assert(predictWindow(s, 1000, 33768, 1000000, w));
    assert(w.waitMicroseconds == 65536 && w.nextBoundarySequence == 4);
    assert(predictWindow(s, 0xfffffff0u, 0x10u, 1000000, w));
    assert(w.nextBoundaryGp2 == 32752 && w.waitMicroseconds == 32736);
    assert(!predictWindow(s, 1000, 999, 1000000, w));
    assert(!predictWindow(s, 1000, 2001, 1000, w));
    assert(!predictWindow(s, 0, 0, 0x80000000u, w));
    s.txDownCounter = 65;
    assert(!predictWindow(s, 0, 0, 1000, w));
    s.txDownCounter = 0;
    assert(predictWindow(s, 0, 0, 1000, w) && w.waitMicroseconds == 65536);
    s.presenceMode = 0;
    assert(!predictWindow(s, 0, 0, 1000, w));
    s.presenceMode = 255; s.awPeriod = 65535;
    assert(!predictWindow(s, 0, 0, 1000, w));
    // Exhaustive positions over two EAW periods, checked against explicit boundaries.
    s.presenceMode = 4; s.awPeriod = 16; s.txDownCounter = 32; s.nextAwSequence = 7;
    for (uint32_t age = 0; age < 131072; ++age) {
        assert(predictWindow(s, 0, age, 200000, w));
        uint32_t boundary = 32768; unsigned sequence = 8;
        while (boundary <= age) { boundary += 65536; sequence += 4; }
        assert(w.nextBoundaryGp2 == boundary && w.waitMicroseconds == boundary - age);
        assert(w.nextBoundarySequence == sequence);
    }
    puts("channel decoding and offline window model: PASS (ASan/UBSan)");
}
