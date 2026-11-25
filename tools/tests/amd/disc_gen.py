import sys,re
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
i=hdr.index('enum { AMD_IP_GC'); j=hdr.index('extern amd_dev_t g_amd;')
code=('#include <stdio.h>\n#include <string.h>\n#include <stdlib.h>\n'
      'typedef unsigned char u8; typedef unsigned short u16; typedef unsigned u32; typedef unsigned long long u64;\n'
      +hdr[i:j]+'\namd_dev_t g_amd;\n#define AMD_IPV(a,b,c) (((u32)(a)<<16)|((u32)(b)<<8)|(u32)(c))\n'
      'static inline u32 amd_ipver(int ip){ return ((u32)g_amd.ip[ip].maj<<16)|((u32)g_amd.ip[ip].min<<8)|g_amd.ip[ip].rev; }\n')
code+='\n'.join(l for l in src.splitlines() if l.startswith('#define AC_DISC'))+'\n'
code+='u32 g_amd_disc_ver, g_amd_disc_nips;\n'
for s in ['static u16 ac_rd16(','static u32 ac_rd32(','static int ac_ipidx(','int amd_disc_parse(','const char* amd_asic_name(','static int ac_family(']:
    code+=fn(src,s)+'\n'
open(sys.argv[3],'w').write(code+'\n'+open(sys.argv[4]).read())
