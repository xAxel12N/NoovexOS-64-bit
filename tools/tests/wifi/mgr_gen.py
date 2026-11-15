import sys, re
src=open(sys.argv[1]).read(); mh=open(sys.argv[2]).read(); rh=open(sys.argv[3]).read()
def fn(s,sig):
    i=s.index(sig); j=s.index('{',i); d=0; k=j
    while True:
        c=s[k]
        if c=='{': d+=1
        elif c=='}':
            d-=1
            if d==0: return s[i:k+1]
        k+=1
line=lambda h,key:[l for l in h.splitlines() if key in l][0]
code='#include <stdio.h>\n#include <string.h>\ntypedef unsigned char u8; typedef unsigned short u16; typedef unsigned u32; typedef unsigned long long u64;\n'
code+=line(mh,'#define WIFI_MAXNET')+'\n'+line(mh,'} wnet;')+'\n'+line(rh,'scan_ap;')+'\n'
code+=line(src,'#define WDIR')+'\n'+line(src,'#define WFILE')+'\n'+line(src,'#define WM_STABLE')+'\n'+line(src,'#define WM_MAXFAIL')+'\n'
code+=open(sys.argv[4]).read().split('/*MAIN*/')[0]
code+='wnet g_wnets[WIFI_MAXNET]; int g_wnet_n=0; int g_wm_auto=1, g_wm_5g=1;\n'
code+='static u64 wm_next_try=0, wm_backoff=500;\n'+line(src,'static int wm_fails=0').strip()+'\n'
code+='void wifi_saved_load(void);\n'
for s in ['static void wm_cpy(','static int wm_eq(','static char* wm_hex(','static int wm_unhex(','void wifi_saved_save(void){','void wifi_saved_load(void){','static int wm_eq_old(','int wifi_saved_find(','int wifi_saved_add(','static int wm_pick(','static u64 wm_fail_delay(','void wifi_user_power(']:
    code+=fn(src,s)+'\n'
open(sys.argv[5],'w').write(code+open(sys.argv[4]).read().split('/*MAIN*/')[1])
