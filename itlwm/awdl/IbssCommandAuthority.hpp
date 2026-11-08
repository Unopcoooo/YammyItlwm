// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <stdint.h>
namespace ItlIbss {
// Positive permission retained across a synchronous firmware sleep. A boolean
// "exclusive command in progress" would incorrectly admit unrelated threads.
class CommandAuthority {
    uintptr_t owner = 0;
    uint32_t depth = 0; // Only the owning thread reads/writes depth.
public:
    bool idle() const { return !__atomic_load_n(&owner,__ATOMIC_SEQ_CST); }
    bool enter(uintptr_t thread) {
        if (!thread) return false;
        uintptr_t expected = 0;
        if (!__atomic_compare_exchange_n(&owner,&expected,thread,false,
                                         __ATOMIC_SEQ_CST,__ATOMIC_SEQ_CST)) {
            if (expected != thread || depth == UINT32_MAX) return false;
        }
        ++depth; return true;
    }
    void leave(uintptr_t thread) {
        if (!allows(thread) || !depth) return;
        if (!--depth) __atomic_store_n(&owner,uintptr_t(0),__ATOMIC_SEQ_CST);
    }
    bool allows(uintptr_t thread) const {
        return thread && __atomic_load_n(&owner,__ATOMIC_SEQ_CST) == thread;
    }
};
class CommandScope {
    CommandAuthority &authority; uintptr_t thread; bool held;
public:
    CommandScope(CommandAuthority &a, uintptr_t t) : authority(a),thread(t),held(a.enter(t)) {}
    ~CommandScope() { if (held) authority.leave(thread); }
    explicit operator bool() const { return held; }
    CommandScope(const CommandScope &) = delete;
    CommandScope &operator=(const CommandScope &) = delete;
};
} // namespace ItlIbss
