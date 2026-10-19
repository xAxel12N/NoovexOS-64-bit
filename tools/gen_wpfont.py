from PIL import Image, ImageDraw, ImageFont
import sys
D='/usr/share/fonts/truetype/liberation/'
FAM=[('LiberationSans','Regular','Bold','Italic','BoldItalic'),('LiberationSerif','Regular','Bold','Italic','BoldItalic'),('LiberationMono','Regular','Bold','Italic','BoldItalic')]
PT=[8,9,10,11,12,14,16,18,20,24,28,36,48]
PX=[round(p*96/72) for p in PT]
CPS=list(range(32,127))+list(range(160,256))
data=bytearray(); recs=[]; met=[]
for fi,f in enumerate(FAM):
    for si in range(4):
        path=D+f[0]+'-'+f[1+si]+'.ttf'
        for px in PX:
            font=ImageFont.truetype(path,px)
            asc,desc=font.getmetrics()
            met.append((asc,desc))
            for cp in CPS:
                ch=chr(cp)
                adv=font.getlength(ch)
                bb=font.getbbox(ch,anchor='ls')
                x0,y0,x1,y1=bb
                w=max(0,x1-x0); h=max(0,y1-y0)
                off=len(data)
                if w>0 and h>0:
                    im=Image.new('L',(w,h),0); dr=ImageDraw.Draw(im); dr.text((-x0,-y0),ch,font=font,fill=255,anchor='ls')
                    px_=im.tobytes()
                    nib=bytearray((w+1)//2*h)
                    for yy in range(h):
                        for xx in range(w):
                            v=(px_[yy*w+xx]+8)//17
                            if v>15:v=15
                            i=yy*((w+1)//2)+xx//2
                            nib[i]|=v<<(4 if xx%2==0 else 0)
                    data+=nib
                recs.append((int(round(adv*16)),x0,y0,w,h,off))
print(len(data),len(recs),file=sys.stderr)
o=['#include "include/nvx/base.h"']
o.append('const u8 wpf_npt=%d; const u8 wpf_pt[]={%s}; const u8 wpf_px[]={%s};'%(len(PT),','.join(map(str,PT)),','.join(map(str,PX))))
o.append('const short wpf_met[][2]={%s};'%','.join('{%d,%d}'%m for m in met))
o.append('const int wpf_ncp=%d;'%len(CPS))
o.append('const struct { u16 adv16; signed char x0, y0; u8 w, h; u32 off; } wpf_g[]={')
o.append(','.join('{%d,%d,%d,%d,%d,%d}'%r for r in recs)+'};')
open('wpf_data.bin','wb').write(bytes(data))
open('wpfont.c','w').write('\n'.join(o)+'\n')
