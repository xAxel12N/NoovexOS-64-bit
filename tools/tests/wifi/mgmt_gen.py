import sys
# Builds a host test of the v10.95 management-frame send path: the real driver functions are cut out of
# rtw8821ce.c and run against a simulated card (tools/tests/wifi/mgmt_main.c).
src=open(sys.argv[1]).read(); hdr=open(sys.argv[2]).read()
def fn(s,sig):
    i=s.index(sig); j=s.index('{',i); d=0; k=j
    while True:
        c=s[k]
        if c=='{': d+=1
        elif c=='}':
            d-=1
            if d==0: return s[i:k+1]
        k+=1
defs=[l for l in hdr.splitlines() if l.startswith('#define RTW_JLOG_')]
code=('#include <stdio.h>\n#include <string.h>\n#include <stdlib.h>\n'
      'typedef unsigned char u8; typedef unsigned short u16; typedef unsigned u32; typedef unsigned long long u64;\n'
      +'\n'.join(defs)+'\n'
      'u8* g_bering=0; u32 g_benum=0; u32 g_beidx=0; u8* g_mgring=0; u32 g_mgnum=0; u32 g_mgidx=0;\n'
      'u32 rtw_r32(u32 m,u32 r); void rtw_w16(u32 m,u32 r,u16 v); void rtw_delay_us(u32 us); void rtw_txdesc_rate(u8* p,int mg); void cpu_wmb(void);\n'
      'void* dma_alloc_pages(int n);\nstatic u32 rtw_txd_sz(void){ return 48u; }\n')
code+='\n'.join(fn(src,s) for s in ['void rtw_desc_csum(','u8* rtw_tx_buf(','int rtw_tx_room('])+'\n'
i=src.index('char g_jlog['); j=src.index('int rtw_tx_mgmt(u32 m')
code+=src[i:j]
code+=fn(src,'void rtw_tx_mgmt_drain(')+'\n'+fn(src,'const char* rtw_mg_last_name(')+'\n'
open(sys.argv[3],'w').write(code+'\n'+open(sys.argv[4]).read())
