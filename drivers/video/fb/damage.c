#include "include/nvx/base.h"
#include "include/nvx/drivers/video/fb/damage.h"

static int dmg_tile(const u32* a,const u32* b,u32 stride,int x,int y,int w,int h){
    for(int r=0;r<h;r++){
        const u32* p=a+(u64)(y+r)*stride+(u32)x; const u32* q=b+(u64)(y+r)*stride+(u32)x;
        if(!(((u64)p|(u64)q)&7u)){ const u64* p8=(const u64*)p; const u64* q8=(const u64*)q; int n=w>>1; for(int i=0;i<n;i++) if(p8[i]!=q8[i])return 1; if((w&1)&&p[w-1]!=q[w-1])return 1; }
        else { for(int i=0;i<w;i++) if(p[i]!=q[i])return 1; } }
    return 0; }

int dmg_rects(const u32* now,const u32* prev,u32 stride,int w,int h,dmg_rect* out,int max){
    static int act[DMG_MAX], nxt[DMG_MAX];
    int n=0, na=0;
    if(max>DMG_MAX)max=DMG_MAX;
    for(int ty=0;ty<h;ty+=DMG_TH){
        int th=h-ty<DMG_TH?h-ty:DMG_TH; int nn=0; int run=-1;
        for(int tx=0;tx<w+DMG_TW;tx+=DMG_TW){
            int d=0;
            if(tx<w){ int tw=w-tx<DMG_TW?w-tx:DMG_TW; d=dmg_tile(now,prev,stride,tx,ty,tw,th); }
            if(d){ if(run<0)run=tx; continue; }
            if(run<0)continue;
            int rx=run, rw=(tx<w?tx:w)-run; run=-1;
            int m=-1; for(int k=0;k<na;k++){ dmg_rect* o=&out[act[k]]; if(o->x==rx&&o->w==rw&&o->y+o->h==ty){ m=act[k]; break; } }
            if(m>=0){ out[m].h+=th; nxt[nn++]=m; continue; }
            if(n>=max)return -1;
            out[n].x=rx; out[n].y=ty; out[n].w=rw; out[n].h=th; nxt[nn++]=n; n++;
            if(tx>=w)break; }
        for(int k=0;k<nn;k++)act[k]=nxt[k]; na=nn; }
    return n; }

u64 dmg_area(const dmg_rect* r,int n){ u64 a=0; for(int i=0;i<n;i++)a+=(u64)r[i].w*(u64)r[i].h; return a; }
