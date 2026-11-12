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
code='#include <stdio.h>\n#include <string.h>\ntypedef unsigned char u8; typedef unsigned short u16; typedef unsigned u32; typedef unsigned long long u64;\n'+ty+'\n'+'\n'.join(akm)+'\n#ifndef AKM_PSK\n#define AKM_PSK 1\n#define AKM_SAE 2\n#define AKM_8021X 4\n#define AKM_PSK256 8\n#endif\nstatic int rtw_rfe(void){return 1;}\nscan_ap g_aps[24]; int g_apn=0; u64 g_ticks=5; u8 g_scan_grp=0; static u8 g_rsn_grp;\n'
code+='\n'.join(fn(src,s) for s in ['static void scan_add(','int rtw_rx_rssi(','static void rtw_rsn_parse(','int rtw_scan_frame('])
open(sys.argv[3],'w').write(code+'\n'+open(sys.argv[4]).read())
