#include <cassert>
#include <cstring>
#include <cstdio>
#include <fstream>
#include <iterator>
#include <vector>
#include "BrcmPatchRAM/AdvSets24H32.hpp"
int main(int argc, char **argv) {
    char bootBuild[256] = {};
    assert(!advSetsBuildMatches24H32(nullptr));
    assert(!advSetsBuildMatches24H32(bootBuild));
    strcpy(bootBuild, "24H32");
    assert(advSetsBuildMatches24H32(bootBuild));
    for (const char *bad : {"24H3", "24H321", "24H33", "24G720", "24H32x"})
        assert(!advSetsBuildMatches24H32(bad));
    std::ifstream input(argc > 1 ? argv[1] : "/usr/sbin/bluetoothd", std::ios::binary);
    assert(input.good());
    std::vector<unsigned char> bytes((std::istreambuf_iterator<char>(input)), {});
    auto original = bytes;
    unsigned total = 0, counts[5] = {};
    for (size_t off = 0; off + 4096 <= bytes.size(); off += 4096) {
        auto applied = applyAdvSets24H32(bytes.data() + off, 4096, off);
        total |= applied;
        for (unsigned i = 0; i < 5; ++i) if (applied & (1U << i)) ++counts[i];
        assert(applyAdvSets24H32(bytes.data() + off, 4096, off) == 0);
    }
    assert(total == 31);
    for (auto count : counts) assert(count == 1);
    size_t differences = 0;
    for (size_t i = 0; i < bytes.size(); ++i) differences += bytes[i] != original[i];
    assert(differences == 21);
    // The ARM slice's identical mask must remain intact.
    assert(memcmp(bytes.data() + 0x12b997b, original.data() + 0x12b997b, 16) == 0);
    std::vector<unsigned char> page(4096, 0xcc);
    for (unsigned i = 0; i < 5; ++i) {
        const auto &patch = advSetPatches24H32[i];
        auto offset = patch.fileOffset & 4095ULL;
        auto base = patch.fileOffset & ~4095ULL;
        std::fill(page.begin(), page.end(), 0xcc);
        memcpy(page.data() + offset, patch.original, patch.size);
        auto saved = page;
        assert(applyAdvSets24H32(page.data(), page.size(), base + 4096) == 0);
        assert(page == saved);
        assert(applyAdvSets24H32(page.data(), 4095, base) == 0);
        assert(page == saved);
        assert(applyAdvSets24H32(page.data(), 8192, base) == 0);
        assert(page == saved);
        assert(applyAdvSets24H32(page.data(), page.size(), base) == (1U << i));
        memcpy(page.data() + offset, patch.original, patch.size);
        page[offset] ^= 1;
        saved = page;
        assert(applyAdvSets24H32(page.data(), page.size(), base) == 0);
        assert(page == saved);
    }
    assert(applyAdvSets24H32(nullptr, 4096, 0) == 0);
    std::puts("PASS: 5/5 x86 file-page matches, 21 changed bytes; ARM isolation, replay, mismatch, wrong-offset and page-size checks");
}
