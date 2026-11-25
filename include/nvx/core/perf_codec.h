#ifndef NVX_CORE_PERF_CODEC_H
#define NVX_CORE_PERF_CODEC_H

#define PERF_EV_CYCLES 0
#define PERF_EV_INSTR 1
#define PERF_EV_CREF 2
#define PERF_EV_CMISS 3
#define PERF_EV_BRANCH 4
#define PERF_EV_BMISS 5
#define PERF_EV_N 6
#define PERF_EV_CPUCLK 16
#define PERF_EV_TASKCLK 17

#define PERF_V_NONE 0
#define PERF_V_INTEL 1
#define PERF_V_AMD 2

#define PERF_SAMPLE_IP (1u<<0)
#define PERF_SAMPLE_TID (1u<<1)
#define PERF_SAMPLE_TIME (1u<<2)
#define PERF_SAMPLE_CALLCHAIN (1u<<5)
#define PERF_FMT_ENABLED (1u<<0)
#define PERF_FMT_RUNNING (1u<<1)
#define PERF_FMT_ID (1u<<2)

static inline int perf_map_attr(unsigned type,unsigned long long cfg){
    if(type==0){ static const signed char m[10]={PERF_EV_CYCLES,PERF_EV_INSTR,PERF_EV_CREF,PERF_EV_CMISS,PERF_EV_BRANCH,PERF_EV_BMISS,-1,-1,-1,PERF_EV_CYCLES}; return cfg<10?m[cfg]:-1; }
    if(type==1){ if(cfg==0)return PERF_EV_CPUCLK; if(cfg==1)return PERF_EV_TASKCLK; return -1; }
    if(type==4){ unsigned e=(unsigned)(cfg&0xFF), u=(unsigned)((cfg>>8)&0xFF); if(e==0x3C&&u==0)return PERF_EV_CYCLES; if(e==0xC0&&u==0)return PERF_EV_INSTR; if(e==0x2E&&u==0x4F)return PERF_EV_CREF; if(e==0x2E&&u==0x41)return PERF_EV_CMISS; if(e==0xC4&&u==0)return PERF_EV_BRANCH; if(e==0xC5&&u==0)return PERF_EV_BMISS; return -1; }
    return -1; }

static inline unsigned perf_evsel(int vendor,int ev){
    static const unsigned short in[PERF_EV_N]={0x003C,0x00C0,0x4F2E,0x412E,0x00C4,0x00C5};
    static const unsigned short am[PERF_EV_N]={0x0076,0x00C0,0xFF60,0x0964,0x00C2,0x00C3};
    if(ev<0||ev>=PERF_EV_N)return 0;
    if(vendor==PERF_V_INTEL)return in[ev];
    if(vendor==PERF_V_AMD)return am[ev];
    return 0; }

static inline int perf_intel_avail(unsigned ebx_mask,unsigned veclen,int ev){
    static const signed char bit[PERF_EV_N]={0,1,3,4,5,6};
    if(ev<0||ev>=PERF_EV_N)return 0;
    if((unsigned)bit[ev]>=veclen)return 0;
    return !((ebx_mask>>bit[ev])&1); }

static inline unsigned long long perf_ctl_bits(unsigned sel,int pmi){ return (unsigned long long)(sel&0xFF)|((unsigned long long)((sel>>8)&0xFF)<<8)|(1ULL<<16)|(1ULL<<17)|(pmi?(1ULL<<20):0)|(1ULL<<22); }

static inline unsigned long long perf_reload(unsigned long long period,int width){ unsigned long long m=width>=64?~0ULL:((1ULL<<width)-1); if(!period)period=1; if(period>(m>>1))period=m>>1; return (0ULL-period)&m; }

static inline int perf_read_fmt(unsigned fmt,unsigned long long val,unsigned long long en,unsigned long long run,unsigned long long id,unsigned long long* out,int cap){
    int n=0; if(cap<1)return -1; out[n++]=val;
    if(fmt&PERF_FMT_ENABLED){ if(n>=cap)return -1; out[n++]=en; }
    if(fmt&PERF_FMT_RUNNING){ if(n>=cap)return -1; out[n++]=run; }
    if(fmt&PERF_FMT_ID){ if(n>=cap)return -1; out[n++]=id; }
    return n*8; }

#endif
