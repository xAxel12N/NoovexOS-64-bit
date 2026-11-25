#include <stdio.h>
#include <string.h>
#include "include/nvx/core/perf_codec.h"

static int fails, checks;
#define CK(c,msg) do{ checks++; if(c)printf("ok %s\n",msg); else { fails++; printf("FAIL %s (line %d)\n",msg,__LINE__); } }while(0)

int main(void){
    CK(perf_map_attr(0,0)==PERF_EV_CYCLES,"hw cpu-cycles");
    CK(perf_map_attr(0,1)==PERF_EV_INSTR,"hw instructions");
    CK(perf_map_attr(0,2)==PERF_EV_CREF,"hw cache-references");
    CK(perf_map_attr(0,3)==PERF_EV_CMISS,"hw cache-misses");
    CK(perf_map_attr(0,4)==PERF_EV_BRANCH,"hw branch-instructions");
    CK(perf_map_attr(0,5)==PERF_EV_BMISS,"hw branch-misses");
    CK(perf_map_attr(0,6)==-1&&perf_map_attr(0,8)==-1,"bus-cycles/stalled not offered");
    CK(perf_map_attr(0,9)==PERF_EV_CYCLES,"ref-cycles maps to cycles");
    CK(perf_map_attr(0,10)==-1&&perf_map_attr(0,~0ULL)==-1,"out of range hw config");
    CK(perf_map_attr(1,0)==PERF_EV_CPUCLK&&perf_map_attr(1,1)==PERF_EV_TASKCLK,"sw cpu-clock task-clock");
    CK(perf_map_attr(1,2)==-1,"sw page-faults not offered");
    CK(perf_map_attr(2,0)==-1&&perf_map_attr(3,0)==-1&&perf_map_attr(5,0)==-1,"tracepoint/hw_cache/breakpoint rejected");
    CK(perf_map_attr(4,0x003C)==PERF_EV_CYCLES&&perf_map_attr(4,0x412E)==PERF_EV_CMISS&&perf_map_attr(4,0x1234)==-1,"raw intel codes");
    CK(perf_evsel(PERF_V_INTEL,PERF_EV_CYCLES)==0x003C&&perf_evsel(PERF_V_INTEL,PERF_EV_CMISS)==0x412E,"intel event selects");
    CK(perf_evsel(PERF_V_AMD,PERF_EV_CYCLES)==0x0076&&perf_evsel(PERF_V_AMD,PERF_EV_BMISS)==0x00C3,"amd event selects");
    CK(perf_evsel(PERF_V_NONE,0)==0&&perf_evsel(PERF_V_INTEL,-1)==0&&perf_evsel(PERF_V_INTEL,PERF_EV_N)==0,"no vendor / bad event");
    CK(perf_intel_avail(0,7,PERF_EV_CYCLES)&&perf_intel_avail(0,7,PERF_EV_BMISS),"all events with full vector");
    CK(!perf_intel_avail(1u<<4,7,PERF_EV_CMISS)&&perf_intel_avail(1u<<4,7,PERF_EV_CREF),"ebx bit hides llc miss");
    CK(!perf_intel_avail(0,4,PERF_EV_CMISS)&&perf_intel_avail(0,4,PERF_EV_CREF),"short vector length hides later events");
    unsigned long long c=perf_ctl_bits(0x412E,1);
    CK((c&0xFF)==0x2E&&((c>>8)&0xFF)==0x41&&(c>>16&1)&&(c>>17&1)&&(c>>20&1)&&(c>>22&1),"ctl bits usr os int en");
    CK(!((perf_ctl_bits(0x3C,0)>>20)&1),"counting has no pmi");
    CK(perf_reload(1000,48)==((1ULL<<48)-1000),"reload 48 bit");
    CK(perf_reload(0,48)==((1ULL<<48)-1),"reload zero period is one");
    CK(perf_reload(~0ULL,48)==((1ULL<<48)-((1ULL<<47)-1)),"reload clamps to half range");
    CK(perf_reload(5,64)==0ULL-5,"reload 64 bit");
    unsigned long long o[4]; memset(o,0,sizeof o);
    CK(perf_read_fmt(0,42,1,2,3,o,4)==8&&o[0]==42,"read plain value");
    CK(perf_read_fmt(PERF_FMT_ENABLED|PERF_FMT_RUNNING|PERF_FMT_ID,42,10,9,7,o,4)==32&&o[1]==10&&o[2]==9&&o[3]==7,"read with times and id");
    CK(perf_read_fmt(PERF_FMT_ID,42,10,9,7,o,4)==16&&o[1]==7,"read with id only");
    CK(perf_read_fmt(PERF_FMT_ENABLED|PERF_FMT_RUNNING|PERF_FMT_ID,1,1,1,1,o,2)==-1,"read refuses short buffer");
    printf(fails?"PERF %d of %d failed\n":"PERF all ok (%d checks)\n",fails?fails:checks,checks);
    return fails!=0; }
