// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <stddef.h>
#include <stdint.h>
#include <string.h>
namespace ItlAwdl {
// Target-pinned layout from macOS IO80211Family:
// IO80211AWDLPeer::fillInCacheParams writes this 0x1d0-byte request.
struct NativePeerCacheControl {
    enum : size_t { Size = 0x1d0, OpcodeOffset = 4, AddressOffset = 8, AddressLength = 6 };
    enum Operation : uint32_t { Add = 0, Delete = 2, Update = 6 };
    enum Completion : uint32_t { AddCompleted = 1, DeleteCompleted = 3 };

    static bool decode(const void *data, size_t length, uint32_t &operation,
                       uint8_t (&address)[AddressLength])
    {
        if (!data || length != Size)
            return false;
        const auto *bytes = static_cast<const uint8_t *>(data);
        memcpy(&operation, bytes + OpcodeOffset, sizeof(operation));
        if (operation != Add && operation != Delete && operation != Update)
            return false;
        memcpy(address, bytes + AddressOffset, AddressLength);
        bool nonzero = false;
        for (size_t i = 0; i < AddressLength; ++i)
            nonzero = nonzero || address[i];
        return nonzero && !(address[0] & 1); // Peers are nonzero unicast addresses.
    }

    static bool makeCompletion(const void *request, size_t length, uint8_t *completion)
    {
        uint32_t operation = 0;
        uint8_t address[AddressLength] = {};
        if (!completion || !decode(request, length, operation, address))
            return false;
        memset(completion, 0, Size);
        const uint32_t completed = operation == Delete ? DeleteCompleted : AddCompleted;
        memcpy(completion + OpcodeOffset, &completed, sizeof(completed));
        memcpy(completion + AddressOffset, address, sizeof(address));
        memcpy(completion + 0xe, static_cast<const uint8_t *>(request) + 0xe, 2);
        return true;
    }
};
}
