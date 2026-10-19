#include "include/nvx/base.h"
#include "include/nvx/drivers/video/fb/framebuffer.h"
#include "include/nvx/graphics/theme/effects.h"
static u32 g_glasstmp[MAXW];
static void glass_panel(int x,int y,int w,int h,int r,u32 tint,int tintamt,int blurR,int hairline){
    int x0=x<0?0:x, y0=y<0?0:y, x1=x+w>(int)W?(int)W:x+w, y1=y+h>(int)H?(int)H:y+h;
    if(x1<=x0||y1<=y0)return; int bw=x1-x0;
    if(blurR>0){
        for(int pass=0;pass<2;pass++){
            for(int j=y0;j<y1;j++){ u32* row=&SCENE[(u64)j*SW]; int sr=0,sg=0,sb=0,n=0;
                for(int i=-blurR;i<=blurR;i++){ int xi=x0+i; if(xi<x0)xi=x0; if(xi>=x1)xi=x1-1; u32 c=row[xi]; sr+=(c>>16)&255; sg+=(c>>8)&255; sb+=c&255; n++; }
                for(int i=0;i<bw;i++){ if(!cut(i,j-y,w,h,r,15))g_glasstmp[i]=rgb(sr/n,sg/n,sb/n);
                    int ad=x0+i+blurR+1; if(ad>=x1)ad=x1-1; int rm=x0+i-blurR; if(rm<x0)rm=x0;
                    u32 ca=row[ad],cr=row[rm]; sr+=((ca>>16)&255)-((cr>>16)&255); sg+=((ca>>8)&255)-((cr>>8)&255); sb+=(ca&255)-(cr&255); }
                for(int i=0;i<bw;i++) if(!cut(i,j-y,w,h,r,15))row[x0+i]=g_glasstmp[i]; }
        }
    }
    int a=tintamt*256/100; if(a>256)a=256;
    for(int j=y0;j<y1;j++){ u32* row=&SCENE[(u64)j*SW]; int gy=j-y;
        for(int i=x0;i<x1;i++){ int gx=i-x; if(cut(gx,gy,w,h,r,15))continue;
            u32 t=tint; int extra=0;
            if(gy<h/2){ int f=(h/2-gy)*36/(h/2); t=mix(t,rgb(255,255,255),255-f); extra=f; }
            row[i]=mix(row[i],t,a); if(extra)row[i]=mix(row[i],rgb(255,255,255),255-extra/2); }
    }
    if(hairline){ u32 hl=rgb(255,255,255);
        for(int i=0;i<w;i++){ if(!cut(i,0,w,h,r,15)&&(u32)(x+i)<W&&(u32)y<H){ u32* p=&SCENE[(u64)y*SW+x+i]; *p=mix(*p,hl,150); } }
        for(int j=0;j<h;j++){ int gx=0; while(gx<w&&cut(gx,j,w,h,r,15))gx++; if(gx<w&&(u32)(x+gx)<W&&(u32)(y+j)<H){ u32* p=&SCENE[(u64)(y+j)*SW+x+gx]; *p=mix(*p,hl,180); } }
    }
}
void glass_liquid(int x,int y,int w,int h,int r,u32 tint,int tintamt,int blurR){
    int x0=x<0?0:x, y0=y<0?0:y, x1=x+w>(int)W?(int)W:x+w, y1=y+h>(int)H?(int)H:y+h;
    if(x1<=x0||y1<=y0)return; int bw=x1-x0;
    if(blurR>0) for(int pass=0;pass<3;pass++){
        for(int j=y0;j<y1;j++){ u32* row=&SCENE[(u64)j*SW]; int sr=0,sg=0,sb=0,n=0;
            for(int i=-blurR;i<=blurR;i++){ int xi=x0+i; if(xi<x0)xi=x0; if(xi>=x1)xi=x1-1; u32 c=row[xi]; sr+=(c>>16)&255; sg+=(c>>8)&255; sb+=c&255; n++; }
            for(int i=0;i<bw;i++){ if(!cut(i,j-y,w,h,r,15))g_glasstmp[i]=rgb(sr/n,sg/n,sb/n);
                int ad=x0+i+blurR+1; if(ad>=x1)ad=x1-1; int rm=x0+i-blurR; if(rm<x0)rm=x0;
                u32 ca=row[ad],cr=row[rm]; sr+=((ca>>16)&255)-((cr>>16)&255); sg+=((ca>>8)&255)-((cr>>8)&255); sb+=(ca&255)-(cr&255); }
            for(int i=0;i<bw;i++) if(!cut(i,j-y,w,h,r,15))row[x0+i]=g_glasstmp[i]; } }
    int a=tintamt*256/100; if(a>256)a=256;
    for(int j=y0;j<y1;j++){ u32* row=&SCENE[(u64)j*SW]; int gy=j-y;
        for(int i=x0;i<x1;i++){ int gx=i-x; if(cut(gx,gy,w,h,r,15))continue;
            u32 base=mix(row[i],tint,a);
            int top=(gy<h*45/100); if(top){ int f=(h*45/100-gy)*70/(h*45/100); base=mix(base,rgb(255,255,255),255-f); }
            int edge=gx; if(w-1-gx<edge)edge=w-1-gx; if(gy<edge)edge=gy; if(h-1-gy<edge)edge=h-1-gy;
            if(edge<3){ base=mix(base,rgb(255,255,255),150); }
            int bot=(gy>h*80/100); if(bot){ int f=(gy-h*80/100)*40/(h-h*80/100+1); base=mix(base,rgb(10,14,26),255-f); }
            row[i]=base; }
    }
    /* top specular sweep — gentle */
    for(int gy=1;gy<h*22/100;gy++){ int j=y+gy; if((u32)j>=H)continue; u32* row=&SCENE[(u64)j*SW];
        int str=110-gy*110/(h*22/100);
        for(int i=x0;i<x1;i++){ int gx=i-x; if(cut(gx,gy,w,h,r,15))continue; row[i]=mix(row[i],rgb(255,255,255),255-str); } }
    /* crisp 1px rim highlight */
    for(int gx=0;gx<w;gx++){ int i=x+gx; if((u32)i>=W)continue;
        for(int gy=0;gy<h;gy++){ if(cut(gx,gy,w,h,r,15))continue; int j=y+gy; if((u32)j<H){ u32* p=&SCENE[(u64)j*SW+i]; *p=mix(*p,rgb(255,255,255),150); } break; }
        for(int gy=h-1;gy>=0;gy--){ if(cut(gx,gy,w,h,r,15))continue; int j=y+gy; if((u32)j<H){ u32* p=&SCENE[(u64)j*SW+i]; *p=mix(*p,rgb(150,160,185),200); } break; } }
}
void soft_ring(int cx,int cy,int rr,int thick,u32 c,int amt){
    for(int y=-rr-thick;y<=rr+thick;y++)for(int x=-rr-thick;x<=rr+thick;x++){ int d2=x*x+y*y; int lo=(rr-thick)*(rr-thick),hi=(rr+thick)*(rr+thick);
        if(d2>=lo&&d2<=hi){ int px=cx+x,py=cy+y; if((u32)px<W&&(u32)py<H){ u32* p=&SCENE[(u64)py*SW+px]; *p=mix(*p,c,amt); } } }
}
