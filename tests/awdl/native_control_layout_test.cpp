#include <cstddef>
#include <cassert>
#include <cstdio>
#include <cstring>
#include "../../include/Airport/apple80211_ioctl.h"

int main() {
#if defined(AIRPORT_WCL) && __IO80211_TARGET == __MAC_15_2
    static_assert(sizeof(apple80211_awdl_sync_state) == 24, "target state");
    static_assert(sizeof(apple80211_awdl_sync_params) == 24, "target params");
    static_assert(sizeof(apple80211_awdl_election_metric) == 12, "target metric");
    // Independent byte vectors bind fields to the target instruction offsets.
    apple80211_awdl_sync_state state{};
    unsigned char wire[24]{};
    state.state = 3; wire[4] = 3;
    const uint8_t address[6] = {2, 3, 4, 5, 6, 7};
    memcpy(state.target_address, address, 6); memcpy(wire + 8, address, 6);
    state.mode_parameter = 0x12345678;
    wire[16] = 0x78; wire[17] = 0x56; wire[18] = 0x34; wire[19] = 0x12;
    state.opmode_flags = 0x20; wire[20] = 0x20;
    assert(!memcmp(&state, wire, sizeof(wire)));
    apple80211_awdl_election_metric metric{};
    metric.metric = 0x12345678; metric.override_metric = 1;
    const uint8_t expected[12] = {0,0,0,0,0x78,0x56,0x34,0x12,1,0,0,0};
    assert(!memcmp(&metric, expected, sizeof(expected)));
    apple80211_awdl_sync_params params{};
    params.non_election_broadcast_period = 0x12345678;
    memset(wire, 0, sizeof(wire));
    wire[20] = 0x78; wire[21] = 0x56; wire[22] = 0x34; wire[23] = 0x12;
    assert(!memcmp(&params, wire, sizeof(wire)));
    puts("PASS actual target header: 24/24/12 sizes, asserted offsets, independent little-endian vectors; layout only");
#else
    static_assert(sizeof(apple80211_awdl_sync_state) == 8, "legacy state");
    static_assert(sizeof(apple80211_awdl_sync_params) == 20, "legacy params");
    static_assert(sizeof(apple80211_awdl_election_metric) == 8, "legacy metric");
    puts("PASS legacy header sizes remain 8/20/8");
#endif
}
