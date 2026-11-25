import sys
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
ty=[l for l in hdr.splitlines() if 'scan_ap;' in l][0]
akm=[l for l in hdr.splitlines() if 'AKM_' in l and '#define' in l]
code='#include <stdio.h>\n#include <string.h>\ntypedef unsigned char u8; typedef unsigned short u16; typedef unsigned u32; typedef unsigned long long u64;\n'+ty+'\n'+'\n'.join(akm)+'\n#ifndef AKM_PSK\n#define AKM_PSK 1\n#define AKM_SAE 2\n#define AKM_8021X 4\n#define AKM_PSK256 8\n#endif\n#ifndef AKM_SAE_H2E\n#define AKM_SAE_H2E 16\n#define AKM_SAE_H2E_ONLY 32\n#endif\n#define RTW_RXBUF 8192u\nstatic int rtw_rfe(void){return 1;}\ntypedef struct { u8 rx_p0_pwdb; } rtw_chip; static const rtw_chip rtw_chip0; static const rtw_chip* g_rtw_chip=&rtw_chip0;\nvoid regdb_note_ie(const u8* b,const u8* c){(void)b;(void)c;}\nscan_ap g_aps[24]; int g_apn=0; u64 g_ticks=5; u8 g_scan_grp=0; static u8 g_rsn_grp;\n'
code+='void rtw_c2h_rx(const u8* c,int len){ (void)c; (void)len; }\n'
code+='void rtw_bw_note(const u8* b,const u8* i,int l){ (void)b; (void)i; (void)l; }\n'
code+='\n'.join(fn(src,s) for s in ['int rtw_rx_bad(','static void scan_add(','int rtw_rx_rssi(','static void rtw_rsn_parse(','int wifi_scan_parse(','int rtw_scan_frame('])
open(sys.argv[3],'w').write(code+'\n'+open(sys.argv[4]).read())
