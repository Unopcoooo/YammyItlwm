#include "../../itlwm/awdl/BootOptions.hpp"
#include <assert.h>
#include <stdio.h>
#include <stddef.h>
extern "C" int appleBootarg(const char *,const char *,void *,int);
static const char *arguments;
static int parse(const char *key,void *value,int size) { return appleBootarg(arguments,key,value,size); }
int main() {
    arguments="-itlwm_awdl_probe -itlwm_awdl_roc_once=6";
    int value=0;
    assert(parse("-itlwm_awdl_roc_once",&value,sizeof(value)) && value==1);
    assert(ItlAwdl::rocChannelOption(parse)==0);
    arguments="-itlwm_awdl_probe itlwm_awdl_roc_once=6";
    assert(ItlAwdl::rocChannelOption(parse)==6);
    arguments="itlwm_awdl_roc_once=44"; assert(ItlAwdl::rocChannelOption(parse)==44);
    arguments="itlwm_awdl_roc_once=149"; assert(ItlAwdl::rocChannelOption(parse)==149);
    arguments="itlwm_awdl_roc_once=0"; assert(ItlAwdl::rocChannelOption(parse)==0);
    arguments="itlwm_awdl_roc_once=153"; assert(ItlAwdl::rocChannelOption(parse)==0);
    arguments="itlwm_awdl_roc_once"; assert(ItlAwdl::rocChannelOption(parse)==0);
    arguments="itlwm_awdl_roc_once=-6"; assert(ItlAwdl::rocChannelOption(parse)==0);
    arguments="itlwm_awdl_roc_once=garbage"; assert(ItlAwdl::rocChannelOption(parse)==0);
    arguments="prefixitlwm_awdl_roc_once=6"; assert(ItlAwdl::rocChannelOption(parse)==0);
    arguments="-itlwm_awdl_probe"; assert(ItlAwdl::rocChannelOption(parse)==0);
    arguments="-itlwm_awdl_dwell=16";
    assert(parse("-itlwm_awdl_dwell",&value,sizeof(value)) && value==1);
    puts("actual Apple XNU parser + production boot-option helper: dashed boolean regression and numeric channels PASS");
}
