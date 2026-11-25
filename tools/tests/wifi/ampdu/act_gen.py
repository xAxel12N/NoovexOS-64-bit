import sys
src=open(sys.argv[1]).read()
def fn(a):
    i=src.index(a); j=src.index('{',i); d=0; k=j
    while True:
        c=src[k]
        if c=='{': d+=1
        elif c=='}':
            d-=1
            if d==0: return src[i:k+1]
        k+=1
def blk(a,b): i=src.index(a); return src[i:src.index(b,i)]
code=('#include <stdio.h>\n#include <stdlib.h>\n#include <string.h>\n'
      '#include "lib/crypto/nvxcrypto.c"\n#include "lib/crypto/nvxtls.c"\n#include "lib/crypto/nvxcrypto2.c"\n#include "lib/crypto/nvxx509.c"\n#include "lib/crypto/nvxroots.c"\n#include "lib/crypto/nvxwpa.c"\n'
      'void* kmalloc(unsigned long n){ return malloc(n); }\nvoid kfree(void* p){ free(p); }\nvoid tls_prepare(void){}\ni64 tls_clock_now(void){ return 0; }\n'
      '#include "net/wifi/wifi_ampdu.c"\n'
      'static u8 g_wnd_mac[6], g_wnd_bssid[6], g_wnd_tk[16]; static u64 g_wnd_tx_pn=1, g_wnd_mpn=0; static int g_wnd_mdec; volatile u64 g_ticks; int g_wnd_up=1, g_wnd_open=0, g_wnd_qos=1, g_wpa_mfp=0;\n'
      'static u32 g_wnd_mmio=0, g_wnd_tx_frames=0, g_wnd_rx_frames=0; static int g_wnd_htp=-1, g_wnd_ra_seen=0, g_wnd_barf_set=0; int g_wnd_barf=1;\n'
      'int iwx_ba_rx(int tid,u16 ssn,u16 win,int start){ (void)tid; (void)ssn; (void)win; (void)start; return 0; }\n'
      'static int g_gen=0, g_batx_ret=32, g_batx_calls=0; static int wifi_hw_generic(void){ return g_gen; } int iwx_ba_tx(int tid,u16 ssn,u16 win,int start){ (void)tid; (void)ssn; (void)win; (void)start; g_batx_calls++; return g_batx_ret; }\n'
      'static u8 g_txf[8][128]; static int g_txl[8], g_txn;\n'
      'static void wnd_tx_mgmt(const u8* f,int n){ if(g_txn<8){ memcpy(g_txf[g_txn],f,(size_t)n); g_txl[g_txn++]=n; } }\n'
      'static u32 rtw_r32(u32 m,u32 r){ (void)m; (void)r; return 0; } static void rtw_w32(u32 m,u32 r,u32 v){ (void)m; (void)r; (void)v; } static void rtw_w16(u32 m,u32 r,u16 v){ (void)m; (void)r; (void)v; }\n')
code+=blk('static void ccmp_nonce(','u32 g_wnd_dead')+blk('static int wnd_eq6(','static void wnd_tx_mgmt(')+blk('static int wnd_mfp(','static void wnd_rx_mgmt(')+'\nstatic int g_wnd_lost=0, g_downs=0; static void wnd_down(void){ g_wnd_up=0; g_wsq_on=0; g_downs++; }\n'+fn('void wnd_saq_tick(')
open(sys.argv[2],'w').write(code+'\n'+open(sys.argv[3]).read())
