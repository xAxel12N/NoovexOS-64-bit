#include "include/nvx/base.h"
#include "include/nvx/drivers/video/fb/framebuffer.h"
#include "include/nvx/mem/heap.h"

int g_wall_fx=1;
u32 g_wall_fx_us=0;
static const u8 BAY[16]={0,8,2,10,12,4,14,6,3,11,1,9,15,7,13,5};

static void hpass(const u32* src,int w,int sp,int r,int y0,int y1,u16* o0,u16* o1,u16* o2){
    int n=2*r+1;
    for(int y=y0;y<y1;y++){ const u32* row=src+(u64)y*sp; u32 s0=0,s1=0,s2=0;
        for(int k=-r;k<=r;k++){ int x=k<0?0:(k>=w?w-1:k); u32 c=row[x]; s0+=(c>>16)&255; s1+=(c>>8)&255; s2+=c&255; }
        u16* a=o0+(u64)y*w; u16* b=o1+(u64)y*w; u16* d=o2+(u64)y*w;
        for(int x=0;x<w;x++){ a[x]=(u16)((s0<<8)/n); b[x]=(u16)((s1<<8)/n); d[x]=(u16)((s2<<8)/n);
            int xo=x-r; if(xo<0)xo=0; int xi=x+r+1; if(xi>=w)xi=w-1; u32 co=row[xo], ci=row[xi];
            s0+=((ci>>16)&255)-((co>>16)&255); s1+=((ci>>8)&255)-((co>>8)&255); s2+=(ci&255)-(co&255); } } }
static void vpass(u16* p,int w,int h,int r,int x0,int x1,u32* col){
    int n=2*r+1;
    for(int x=x0;x<x1;x++){ u32 s=0;
        for(int k=-r;k<=r;k++){ int y=k<0?0:(k>=h?h-1:k); s+=p[(u64)y*w+x]; }
        for(int y=0;y<h;y++){ col[y]=s/(u32)n; int yo=y-r; if(yo<0)yo=0; int yi=y+r+1; if(yi>=h)yi=h-1; s+=p[(u64)yi*w+x]; s-=p[(u64)yo*w+x]; }
        for(int y=0;y<h;y++)p[(u64)y*w+x]=(u16)col[y]; } }
static int iabs(int v){ return v<0?-v:v; }
static u32 isq(u32 v){ u32 r=0, b=1u<<30; while(b>v)b>>=2; while(b){ if(v>=r+b){ v-=r+b; r=(r>>1)+b; } else r>>=1; b>>=2; } return r; }

static struct { u32* img; int w,h,sp; u16* P; u32* col; int phase, pos; u64 us; } E;

static void compose_rows(int y0,int y1){
    int w=E.w, h=E.h; u64 N=(u64)w*h; u16 *s0=E.P, *s1=E.P+N, *s2=E.P+2*N, *b0=E.P+3*N, *b1=E.P+4*N, *b2=E.P+5*N;
    int cx=w/2, cy=h/2; u32 R2=(u32)cx*cx+(u32)cy*cy; int lx=w/4, ly=h/10; u32 LR=isq((u32)w*w+(u32)h*h)*9/10; if(LR<1)LR=1;
    for(int y=y0;y<y1;y++){ u32* row=E.img+(u64)y*E.sp;
        int ty=y*256/h; int dof=ty<56?(56-ty)*40/56:0;
        for(int x=0;x<w;x++){ u64 i=(u64)y*w+x; u32 c=row[x]; int ch[3]={(int)((c>>16)&255),(int)((c>>8)&255),(int)(c&255)};
            int sm[3]={s0[i],s1[i],s2[i]}, bg[3]={b0[i],b1[i],b2[i]};
            int smooth=1; for(int k=0;k<3;k++){ int d=iabs((ch[k]<<8)-sm[k]); if(d>(3<<8))smooth=0; }
            u32 dx=(u32)iabs(x-cx), dy=(u32)iabs(y-cy); u32 d2=(dx*dx+dy*dy); u32 vig=256-(u32)((u64)d2*60/R2);
            u32 ld=isq((u32)(x-lx)*(u32)(x-lx)+(u32)(y-ly)*(u32)(y-ly)); int la=ld>=LR?0:(int)(30-ld*30/LR);
            int dz=BAY[((y&3)<<2)|(x&3)]*16+8; u32 out=0;
            for(int k=0;k<3;k++){ int v=smooth?sm[k]:(ch[k]<<8);
                if(dof) v+=((bg[k]-v)*dof)>>8;
                if(bg[k]>v) v+=((bg[k]-v)*28)>>8;
                v=(int)(((u32)v*vig)>>8);
                v+=(((255<<8)-v)*la)>>8;
                v+=dz; v>>=8; if(v<0)v=0; if(v>255)v=255; out=(out<<8)|(u32)v; }
            row[x]=(c&0xFF000000u)|out; } } }

void wall_enhance_abort(void){ if(E.P)kfree(E.P); if(E.col)kfree(E.col); E.P=0; E.col=0; E.img=0; E.phase=0; }
int wall_enhance_begin(u32* img,int w,int h,int sp){
    wall_enhance_abort();
    if(!g_wall_fx||!img||w<64||h<64)return 0;
    u64 N=(u64)w*h; E.P=(u16*)kmalloc((unsigned long)(N*2*6)); E.col=(u32*)kmalloc((unsigned long)(h*4+64));
    if(!E.P||!E.col){ wall_enhance_abort(); return -1; }
    E.img=img; E.w=w; E.h=h; E.sp=sp; E.phase=1; E.pos=0; E.us=0; return 1; }
int wall_enhance_step(int units){
    if(!E.img)return 1;
    extern u64 hr_ns(void); u64 t0=hr_ns(); int done=0;
    int w=E.w, h=E.h; u64 N=(u64)w*h; u16 *s0=E.P, *s1=E.P+N, *s2=E.P+2*N, *b0=E.P+3*N, *b1=E.P+4*N, *b2=E.P+5*N;
    while(units-->0){
        switch(E.phase){
        case 1: { int y1=E.pos+16; if(y1>h)y1=h; hpass(E.img,w,E.sp,6,E.pos,y1,s0,s1,s2); E.pos=y1; if(E.pos>=h){ E.phase=2; E.pos=0; } break; }
        case 2: { int x1=E.pos+32; if(x1>w)x1=w; vpass(s0,w,h,6,E.pos,x1,E.col); vpass(s1,w,h,6,E.pos,x1,E.col); vpass(s2,w,h,6,E.pos,x1,E.col); E.pos=x1; if(E.pos>=w){ E.phase=3; E.pos=0; } break; }
        case 3: { int y1=E.pos+16; if(y1>h)y1=h; hpass(E.img,w,E.sp,24,E.pos,y1,b0,b1,b2); E.pos=y1; if(E.pos>=h){ E.phase=4; E.pos=0; } break; }
        case 4: { int x1=E.pos+32; if(x1>w)x1=w; vpass(b0,w,h,24,E.pos,x1,E.col); vpass(b1,w,h,24,E.pos,x1,E.col); vpass(b2,w,h,24,E.pos,x1,E.col); E.pos=x1; if(E.pos>=w){ E.phase=5; E.pos=0; } break; }
        case 5: { int y1=E.pos+16; if(y1>h)y1=h; compose_rows(E.pos,y1); E.pos=y1; if(E.pos>=h){ done=1; } break; }
        default: done=1; }
        if(done)break; }
    E.us+=(hr_ns()-t0)/1000ULL;
    if(done){ g_wall_fx_us=(u32)E.us; wall_enhance_abort(); return 1; }
    return 0; }
int wall_enhance(u32* img,int w,int h,int sp){
    int r=wall_enhance_begin(img,w,h,sp); if(r<=0)return r;
    while(!wall_enhance_step(64)){}
    return 1; }
