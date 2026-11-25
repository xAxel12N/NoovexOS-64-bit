import sys,os
src=open(sys.argv[1]).read(); out=open(sys.argv[2],'w')
def func(sig):
    i=src.index(sig); j=src.index('{',i); d=0; k=j
    while True:
        c=src[k]
        if c=='{': d+=1
        elif c=='}':
            d-=1
            if d==0: return src[i:k+1]
        k+=1
i=src.index('const u8 uvc_std_dht[]={'); j=src.index('};',i)+2
o=['#include <stdio.h>\n#include <stdlib.h>\n#include <string.h>\ntypedef unsigned char u8; typedef unsigned short u16; typedef unsigned int u32; typedef unsigned long long u64;\n#define AVW_ALLOC malloc\n#define AVW_FREE free\n',src[i:j],'\n']
for s in ['static u32 uvc_bw_budget(int speed){','static int uvc_bw_class(int speed){','static u32 uvc_bw_left(int speed,u32 used){']: o.append(func(s)+'\n')
o.append('#include "'+os.path.abspath(sys.argv[3])+'"\n')
o.append(open(sys.argv[4]).read())
out.write(''.join(o))
