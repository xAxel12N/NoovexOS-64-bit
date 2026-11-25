import sys
src=open(sys.argv[1]).read(); rh=open(sys.argv[2]).read()
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
main=open(sys.argv[3]).read().split('/*MAIN*/')
code='#include <stdio.h>\n#include <string.h>\ntypedef unsigned char u8; typedef unsigned short u16; typedef unsigned u32; typedef unsigned long long u64;\n'
code+=line(rh,'scan_ap;')+'\n'+line(src,'int g_wm_fast=1;')+'\n'+main[0]
for s in ['static void wm_cpy(','static int wm_eq(','static int wm_fr_seen(','static void wm_fr_probe(','void wifi_fast_set(','int wifi_fast_state(','static void wm_fr_bad(','static int wm_fr_start(','static void wm_fr_join(']:
    code+=fn(src,s)+'\n'
open(sys.argv[4],'w').write(code+main[1])
