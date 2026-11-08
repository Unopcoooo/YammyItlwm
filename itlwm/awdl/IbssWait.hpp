// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <stdint.h>
namespace ItlIbss {
// Transport holds the workloop gate on a worker thread. sleep() atomically
// releases it; interrupt handlers set the predicate and issue a matching wake.
template<class T,class Predicate> int waitFor(T &t,Predicate ready,uint32_t generation,uint64_t deadline) {
    if (!t.allowed()) return EBUSY;
    for (;;) {
        if (!t.current(generation)) return ENXIO;
        if (ready()) return 0; // Completion before sleep must not be lost.
        if (t.expired(deadline)) return EAGAIN;
        int error=t.sleep(deadline);
        if (!t.current(generation)) return ENXIO;
        if (ready()) return 0; // Completion wins a simultaneous timeout.
        if (error) return error;
    }
}
} // namespace ItlIbss
