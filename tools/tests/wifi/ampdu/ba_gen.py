import sys
src=open(sys.argv[1]).read()
def fn(s,sig):
    i=s.index(sig); j=s.index('{',i); d=0; k=j
    while True:
        c=s[k]
        if c=='{': d+=1
        elif c=='}':
            d-=1
            if d==0: return s[i:k+1]
        k+=1
i=src.index('static u8 g_amq['); j=src.index('static int wnd_rx_eth(')
code=('#include <stdio.h>\n#include <stdlib.h>\n#include <string.h>\n#include "include/nvx/base.h"\n'
      'void* kmalloc(unsigned long n){ return malloc(n); }\nvoid kfree(void* p){ free(p); }\n'
      '#include "net/wifi/wifi_ampdu.c"\n#define RTW_RXBUF 8192u\n'
      'static u8 g_wnd_mac[6], g_wnd_bssid[6]; static u64 g_wnd_rx_pn_uni[17], g_wnd_rx_pn_grp[4]; volatile u64 g_ticks;\n'
      'u64 g_wnd_beacon_t; static u8 g_wnd_tk[16], g_wnd_gtk[16]; int g_wnd_have_gtk, g_wnd_open; u32 g_wnd_rx_frames, g_wnd_rx_drop, g_wnd_rx_uc;\n#include "include/nvx/net/wifi/wifi_netdev.h"\n'
      'int wpa_rx_eapol(const u8* eap,int el); int wpa_rx_eapol_p(const u8* eap,int el,int prot);\n'
      'void ccmp_nonce(u8* n,const u8* a2,u64 pn,u8 prio); int aes_ccm_decrypt_verify(const u8* k,const u8* n,const u8* aad,int al,u8* d,int dl,const u8* mic);\n')
code+=fn(src,'static int wnd_eq6(')+'\n'+src[i:j]
open(sys.argv[2],'w').write(code+'\n'+open(sys.argv[3]).read())
