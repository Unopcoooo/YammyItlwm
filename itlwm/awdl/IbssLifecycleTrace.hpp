// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "IbssExperiment.hpp"
namespace ItlIbss {
enum class LifeCode : uint32_t {
    MacDecision=0x50001, MacRestart=0x50002, EnableEnter=0x50003, DisableEnter=0x50004,
    StopEnter=0x50005, StopAdvanced=0x50006, InitEnter=0x50007, InitAdvanced=0x50008,
    InitTaskEnter=0x50009, InitTaskDenied=0x5000a, IrqRfKill=0x5000b,
    IrqSwError=0x5000c, IrqHwError=0x5000d, WatchdogReset=0x5000e,
    RecoveryCancelled=0x5000f, InitTaskResult=0x50010, IrqDmaError=0x50011
};
enum LifeContext : uint32_t { InGate=1,OnEventThread=2,OwnsRadio=4,MacPending=8,PublishedPhase=16 };
inline ExperimentRows::Row lifecycleRow(LifeCode code,uint64_t ns,uint32_t generation,
    uint32_t phase,uint32_t ifFlags,uint32_t driverFlags,uint32_t netState,uint32_t nextState,
    uint32_t context,uint32_t detail,uint32_t threadTag=0) {
    using H=ExperimentRows;H::Row row;const auto us=ns/1000;
    row.v[H::Kind]=13;row.v[H::Code]=uint32_t(code);row.v[H::HostUsLow]=uint32_t(us);
    row.v[H::HostUsHigh]=uint32_t(us>>32);row.v[H::Generation]=generation;row.v[H::Phase]=phase;
    row.v[H::Flags]=ifFlags;row.v[H::Status]=driverFlags;row.v[H::Valid]=context;
    row.v[H::Detail0]=(netState&255)|((nextState&255)<<8);row.v[H::Detail1]=detail;row.v[H::MacContext]=threadTag;
    return row;
}
// Kind 14 stores numeric firmware diagnostics only, never host/device pointers.
enum class FaultRead : uint32_t { Valid=0,Empty=1,InvalidPointer=2,ReadFailed=3 };
struct FaultSnapshot {
    uint32_t valid=0,errorId=0,data1=0,data2=0,data3=0;
    uint32_t hostCommand=0,lastCommand=0,major=0,minor=0;
};
inline ExperimentRows::Row firmwareFaultRow(uint32_t core,FaultRead status,const FaultSnapshot &v,
    uint64_t ns,uint32_t generation,uint32_t phase) {
    using H=ExperimentRows;H::Row row;const auto us=ns/1000;
    row.v[H::Kind]=14;row.v[H::Code]=core;row.v[H::Result]=uint32_t(status);
    row.v[H::HostUsLow]=uint32_t(us);row.v[H::HostUsHigh]=uint32_t(us>>32);
    row.v[H::Generation]=generation;row.v[H::Phase]=phase;
    row.v[H::Flags]=v.valid;row.v[H::Gp2]=v.errorId;
    row.v[H::Status]=v.data1;row.v[H::Valid]=v.data2;row.v[H::Length]=v.data3;
    row.v[H::Channel]=v.hostCommand;row.v[H::MacContext]=v.lastCommand;
    row.v[H::Detail0]=v.major;row.v[H::Detail1]=v.minor;
    return row;
}
} // namespace ItlIbss
