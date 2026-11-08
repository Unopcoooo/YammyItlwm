// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <libkern/c++/OSObject.h>
#include "DataPort.hpp"
namespace ItlAwdl {extern DataPort<OSObject> gDataPort;}
struct ItlAwdlDataCall {
    enum Kind { Bind, Stop, Tx, Rx, NativeReady, Quiesce, Maintenance, BsdTx, NativeSyncQuery, NativeSyncDisable, NativeSyncEnable } kind;
    OSObject *owner=nullptr,*pool=nullptr,*tx=nullptr,*rx=nullptr;
    uint8_t local[6]={};
    bool (*linkAction)(OSObject *,bool)=nullptr;
    int (*bsdInputAction)(OSObject *,const uint8_t *,size_t)=nullptr;
    const void *packets=nullptr;unsigned count=0,result=0;
};
