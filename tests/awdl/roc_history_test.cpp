#include "../../itlwm/awdl/RocHistory.hpp"
#include <atomic>
#include <cassert>
#include <cstdio>
#include <thread>
using H=ItlAwdl::RocHistory;
int main() {
    H h;H::Row row;
    assert(!h.read(0,row)&&!h.read(H::Capacity,row));
    for(unsigned n=1;n<=3*H::Capacity;++n){for(unsigned j=0;j<H::Count;++j)row.v[j]=n*100+j;h.append(row);}
    unsigned count=0;
    for(unsigned i=0;i<H::Capacity;++i){assert(h.read(i,row));assert(row.v[H::Serial]>=2*H::Capacity+1&&row.v[H::Serial]<=3*H::Capacity);++count;}
    assert(count==H::Capacity);
    // A busy/changed record is skipped and never exposed as a partial row.
    __atomic_store_n(&h.slots[0].version,5u,__ATOMIC_SEQ_CST);assert(!h.read(0,row)&&!row.v[0]);
    H concurrent;std::atomic<bool> done{false};std::atomic<unsigned> snapshots{0};
    std::thread writer([&]{for(unsigned n=1;n<=100000;++n){H::Row v;for(unsigned j=1;j<H::Count;++j)v.v[j]=n^(0x13579u*j);concurrent.append(v);}done.store(true);});
    do {for(unsigned i=0;i<H::Capacity;++i){H::Row v;if(!concurrent.read(i,v))continue;const auto n=v.v[H::Serial];for(unsigned j=1;j<H::Count;++j)assert(v.v[j]==(n^(0x13579u*j)));++snapshots;}} while(!done.load());
    writer.join();assert(snapshots.load()>0);
    puts("ROC history: bounded overwrite, empty/busy rejection and concurrent coherent row snapshots PASS");
}
