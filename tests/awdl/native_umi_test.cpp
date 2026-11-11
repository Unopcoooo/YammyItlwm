#include "NativeUmi.hpp"
#include <assert.h>
#include <stdint.h>
#include <string.h>

int main() {
    const uint8_t peer[6] = {0xb6, 0x13, 0xe4, 0x11, 0x80, 0x7a};
    const uint8_t source[6] = {0x9e, 0xff, 0x52, 0x3e, 0x20, 0xf5};
    const uint8_t body[] = {4, 0x49, 0, 6, 0x2c, 0, 6, 0};
    uint8_t frame[64] = {};
    size_t length = 0;
    assert(ItlAwdl::buildNativeUmiFrame(peer, source, body, sizeof(body),
                                        0xa4a475a1, 0x934, frame, sizeof(frame),
                                        length));
    assert(length == 48);
    const uint8_t expected[40] = {
        0xd0, 0, 0x30, 0,
        0xb6, 0x13, 0xe4, 0x11, 0x80, 0x7a,
        0x9e, 0xff, 0x52, 0x3e, 0x20, 0xf5,
        0x00, 0x25, 0x00, 0xff, 0x94, 0x73,
        0x40, 0x93,
        0x7f, 0x00, 0x17, 0xf2, 0x08, 0x10, 0x03, 0x00,
        0xa1, 0x75, 0xa4, 0xa4, 0xa1, 0x75, 0xa4, 0xa4,
    };
    assert(!memcmp(frame, expected, sizeof(expected)));
    assert(!memcmp(frame + 40, body, sizeof(body)));

    uint8_t broadcast[6] = {0xff, 0xff, 0xff, 0xff, 0xff, 0xff};
    assert(!ItlAwdl::buildNativeUmiFrame(broadcast, source, body, sizeof(body),
                                         1, 0, frame, sizeof(frame), length));
    uint8_t empty[1800] = {};
    assert(!ItlAwdl::buildNativeUmiFrame(peer, source, empty,
                                         ItlAwdl::kNativeUmiPayloadMax + 1,
                                         1, 0, frame, sizeof(frame), length));
    assert(!ItlAwdl::buildNativeUmiFrame(peer, source, body, sizeof(body),
                                         1, 0, frame, 47, length));
    return 0;
}
