#!/usr/bin/env python3
"""Exercise production rate-init/TX-status and gate helper without hardware."""
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]

def function(text, signature):
    start = text.index(signature)
    brace = text.index('{', start)
    depth = 1
    end = brace + 1
    while depth:
        depth += (text[end] == '{') - (text[end] == '}')
        end += 1
    return text[start:end]

header = (ROOT / 'itlwm/hal_iwm/ItlIwm.hpp').read_text()
rates = (ROOT / 'itlwm/hal_iwm/rs.cpp').read_text()
helper = function(header, '    bool inRateControlGate()')
helper += '\n    template <typename Action>\n'
helper += function(header, '    IOReturn runRateControlAction(')
gate = function(rates, 'static IOReturn rs_run_gated(')
init = function(rates, 'void iwl_mvm_rs_rate_init(')
status = function(rates, 'void iwl_mvm_rs_tx_status(')
fixture = r'''
#include <cassert>
using IOReturn = int;
constexpr int kIOReturnSuccess = 0;
struct OSObject {};
struct Loop { bool held=false; bool inGate(){return held;} };
struct Gate {
    Loop *loop; int calls=0; bool reject=false;
    IOReturn runAction(IOReturn (*action)(OSObject *,void *,void *,void *,void *),void *arg){
        ++calls; if(reject)return -1;
        assert(!loop->held); loop->held=true;
        int result=action(nullptr,arg,nullptr,nullptr,nullptr);
        loop->held=false; return result;
    }
};
struct ItlIwm { Loop loop; Gate gate{&loop};
    Loop *getMainWorkLoop(){return &loop;}
    Gate *getMainCommandGate(){return &gate;}
HELPER
};
struct iwm_softc { ItlIwm *owner; };
#define container_of(sc, type, field) ((sc)->owner)
struct ieee80211_node {};
struct ieee80211_tx_info {};
enum nl80211_band { band0 };
static int initializations=0, completions=0;
static void rs_drv_rate_init(iwm_softc *sc,ieee80211_node *,nl80211_band){
    assert(sc->owner->loop.held); ++initializations;
}
static void __iwl_mvm_rs_tx_status(iwm_softc *sc,ieee80211_node *,int,ieee80211_tx_info *,bool){
    assert(sc->owner->loop.held); ++completions;
}
template <typename Action>
GATE
INIT
STATUS
int main(){
    ItlIwm driver; iwm_softc sc{&driver}; ieee80211_node node;
    iwl_mvm_rs_rate_init(&sc,&node,band0,true);
    iwl_mvm_rs_tx_status(&sc,&node,0,nullptr,false);
    assert(initializations==1 && completions==1 && driver.gate.calls==2);
    driver.loop.held=true;
    iwl_mvm_rs_rate_init(&sc,&node,band0,true);
    iwl_mvm_rs_tx_status(&sc,&node,0,nullptr,false);
    assert(initializations==2 && completions==2 && driver.gate.calls==2);
    driver.loop.held=false; driver.gate.reject=true;
    iwl_mvm_rs_rate_init(&sc,&node,band0,true);
    assert(initializations==2 && !driver.loop.held);
}
'''
for key, value in [('HELPER', helper), ('GATE', gate), ('INIT', init), ('STATUS', status)]:
    fixture = fixture.replace(key, value)
with tempfile.TemporaryDirectory() as directory:
    source = Path(directory) / 'test.cpp'
    binary = Path(directory) / 'test'
    source.write_text(fixture)
    subprocess.run(['clang++', '-std=c++14', '-DAIRPORT_VTD', '-fsanitize=address,undefined',
                    str(source), '-o', str(binary)], check=True)
    subprocess.run([str(binary)], check=True)
print('PASS: production init/TX status serialize outside gate, reenter safely, and honor rejection')
