// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <sys/errno.h>
namespace ItlIbss {
struct Channel6QueryBudget {bool consumed=false;};
// Transport must implement current-profile GET_CURRENT only. This helper never
// substitutes a country-setting command and never repeats a failed query.
// Used before the first profile command, under the existing command authority.
template<class T> int acquireChannel6Permission(T &t,Channel6QueryBudget &budget) {
    if(!t.healthy())return ECANCELED;
    if(!t.permissionKnown()) {
        if(budget.consumed || !t.canGetCurrent())return ENOTSUP;
        budget.consumed=true;
        const int error=t.getCurrent();
        if(error)return error;
        if(!t.healthy())return ECANCELED;
    }
    return t.allowed()?0:ENOTSUP;
}
}
