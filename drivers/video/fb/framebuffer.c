#include "include/nvx/base.h"
#include "include/nvx/assets/font.h"
#include "include/nvx/assets/fontaa.h"
#include "include/nvx/arch/x86_64/cpu/simd.h"
#include "include/nvx/drivers/video/fb/framebuffer.h"
#include "include/nvx/drivers/video/fb/gfx.h"
#include "include/nvx/net/http/http.h"
#include "include/nvx/apps/system/settings.h"
/* =========================================================================
   Framebuffer + back buffers
   ========================================================================= */

int g_fb_rgb=0; u32 g_uefi_ram_mb=0;
void copy_swap32(u32* d,const u32* s,int n){ for(int i=0;i<n;i++){ u32 v=s[i]; d[i]=(v&0xFF00FF00u)|((v>>16)&0xFFu)|((v&0xFFu)<<16); } }
u32 SCENE_STATIC[MAXW*MAXH]; u32* g_scene=SCENE_STATIC;
u32 BG_STATIC[MAXW*MAXH]; u32* BG=BG_STATIC;

u8  *FBB;                        /* hardware framebuffer         */
u32  PITCHB, W, H, SW;           /* SW = scene stride (=W)       */
u8   BPP; u32 BYPP;
u8  *FBB2;
u32  PITCHB2, W2, H2;
u8   BYPP2;
int  g_fb2_ok=0, g_fb2_rgb=0, g_mirror2=1;

void fill_rect(int x,int y,int w,int h,u32 c){
    if(x<0){w+=x;x=0;} if(y<0){h+=y;y=0;} if(x+w>(int)W)w=(int)W-x; if(y+h>(int)H)h=(int)H-y; if(w<=0||h<=0)return;
    if(g_gfx){ gq_fill(g_rt,x,y,w,h,c); return; }
    if(g_accel2d){ for(int j=0;j<h;j++) sse_fill32(SCENE+(u64)(y+j)*SW+x, c, w); }
    else { for(int j=0;j<h;j++){ u32* r=SCENE+(u64)(y+j)*SW+x; for(int i=0;i<w;i++) r[i]=c; } }
}
static inline u32  getpx(int x,int y){ return ((u32)x<W&&(u32)y<H)?SCENE[(u32)y*SW+x]:0; }

u32 isqrt32(u32 v){ u32 r=0,b=1u<<30; while(b>v)b>>=2; while(b){ if(v>=r+b){v-=r+b;r=(r>>1)+b;} else r>>=1; b>>=2; } return r; }
int cut(int i,int j,int w,int h,int r,int m){
    if(r<=0)return 0;
    if((m&1)&&i<r&&j<r){int dx=r-i,dy=r-j; if(dx*dx+dy*dy>r*r)return 1;}
    if((m&2)&&i>=w-r&&j<r){int dx=i-(w-1-r),dy=r-j; if(dx*dx+dy*dy>r*r)return 1;}
    if((m&4)&&i<r&&j>=h-r){int dx=r-i,dy=j-(h-1-r); if(dx*dx+dy*dy>r*r)return 1;}
    if((m&8)&&i>=w-r&&j>=h-r){int dx=i-(w-1-r),dy=j-(h-1-r); if(dx*dx+dy*dy>r*r)return 1;}
    return 0;
}
/* anti-aliased rounded rectangle */
void fill_round(int x,int y,int w,int h,int r,int m,u32 c){
    if(g_gfx){ gfx_round(x,y,w,h,r,m,c,256); return; }
    for(int j=0;j<h;j++)for(int i=0;i<w;i++){
        int dx=-1,dy=0;
        if((m&1)&&i<r&&j<r){dx=r-i;dy=r-j;}
        else if((m&2)&&i>=w-r&&j<r){dx=i-(w-1-r);dy=r-j;}
        else if((m&4)&&i<r&&j>=h-r){dx=r-i;dy=j-(h-1-r);}
        else if((m&8)&&i>=w-r&&j>=h-r){dx=i-(w-1-r);dy=j-(h-1-r);}
        if(dx>=0){ u32 dd=isqrt32(((u32)(dx*dx+dy*dy))<<16); int cov=(int)(r*256)-(int)dd+128;
            if(cov<=0) continue; if(cov<256){ pblend(x+i,y+j,c,cov); continue; } }
        putpx(x+i,y+j,c);
    }
}
/* soft, layered drop shadow that skips the pixels the window will cover */
void shadow_soft(int wx,int wy,int ww,int wh,int wr){
    if(g_gfx){ gfx_shadow(wx,wy,ww,wh,wr); return; }
    int E[4]={12,8,5,2}, S[4]={14,10,6,3}, A[4]={6,8,10,12};
    for(int k=0;k<4;k++){ int e=E[k],s=S[k],a=A[k], x=wx-e,y=wy-e+s,w=ww+2*e,h=wh+2*e,r=wr+e;
        for(int j=0;j<h;j++)for(int i=0;i<w;i++){ int px=x+i,py=y+j;
            if(px>=wx&&px<wx+ww&&py>=wy&&py<wy+wh) continue;
            if(cut(i,j,w,h,r,15)) continue; pblend(px,py,rgb(0,0,0),a); } }
}
/* anti-aliased filled circle */
void fill_circle(int cx,int cy,int r,u32 c){
    if(g_gfx){ gfx_circle(cx,cy,r,c,256); return; }
    for(int j=-r-1;j<=r+1;j++)for(int i=-r-1;i<=r+1;i++){ int d2=i*i+j*j; if(d2>(r+1)*(r+1)) continue;
        u32 dd=isqrt32(((u32)d2)<<16); int cov=(int)(r*256)-(int)dd+128;
        if(cov<=0) continue; if(cov>=256) putpx(cx+i,cy+j,c); else pblend(cx+i,cy+j,c,cov); }
}

/* ---- text into SCENE ---- */
int slen(const char*s){ int n=0; while(s[n])n++; return n; }
void dchar(int x,int y,char ch,u32 c,int s){
    int idx=(u8)ch-32; if(idx<0||idx>=95)return; if(g_gfx){ gfx_glyph(x,y,ch,c,s,0); return; } const unsigned char*g=fontaa[idx];
    (void)SCENE; for(int r=0;r<14;r++) for(int col=0;col<8;col++){ int a=g[r*8+col]; if(a<6)continue;
        for(int dy=0;dy<s;dy++) for(int dx=0;dx<s;dx++){ int px=x+col*s+dx, py=y+r*s+dy; if((u32)px<W&&(u32)py<H){ u32*p=&g_scene[(u64)py*SW+px]; *p=a>=250?c:mix(*p,c,a); } } }
}
int dstr(int x,int y,const char*s,u32 c,int sc){ while(*s){dchar(x,y,*s,c,sc);x+=8*sc;s++;} return x; }
void dstr_ctr(int cx,int y,const char*s,u32 c,int sc){ dstr(cx-(slen(s)*8*sc)/2,y,s,c,sc); }
/* ---- proportional text (variable width from the 8x14 bitmap ink bounds) ---- */
int g_gl[95], g_gw[95], g_gm_ready=0;
void gmetrics(void){ for(int idx=0;idx<95;idx++){ const unsigned char*g=fontaa[idx]; int lo=8,hi=-1;
        for(int r=0;r<14;r++){ for(int c=0;c<8;c++){ if(g[r*8+c]>40){ if(c<lo)lo=c; if(c>hi)hi=c; } } }
        if(hi<0){ g_gl[idx]=0; g_gw[idx]=0; } else { g_gl[idx]=lo; g_gw[idx]=hi-lo+1; } } g_gm_ready=1; }
int gadv(char ch,int s){ int idx=(u8)ch-32; if(idx<0||idx>=95)return 4*s; if(g_gw[idx]==0)return 4*s; return (g_gw[idx]+1)*s; }
int pw_sub(const char*str,int a,int b,int s){ int w=0; for(int i=a;str[i]&&i<b;i++)w+=gadv(str[i],s); return w; }
int pw_str(const char*str,int s){ int w=0; for(int i=0;str[i];i++)w+=gadv(str[i],s); return w; }
static void pdch(int x,int y,char ch,u32 c,int s){ int idx=(u8)ch-32; if(idx<0||idx>=95||g_gw[idx]==0)return; const unsigned char*gp=fontaa[idx]; int ox=x-g_gl[idx]*s; if(g_gfx){ gfx_glyph(ox,y,ch,c,s,1); return; }
    (void)SCENE; for(int r=0;r<14;r++){ for(int col=0;col<8;col++){ int a=gp[r*8+col]; if(a<6)continue;
        if(s==1){ int px=ox+col,py=y+r; if((u32)px<W&&(u32)py<H){ u32*p=&g_scene[(u64)py*SW+px]; *p=mix(*p,c,a); } }
        else { for(int dy=0;dy<s;dy++)for(int dx=0;dx<s;dx++){ int px=ox+col*s+dx,py=y+r*s+dy; if((u32)px<W&&(u32)py<H){ u32*p=&g_scene[(u64)py*SW+px]; *p=mix(*p,c,a); } } } } } }
int pdstr(int x,int y,const char*str,u32 c,int s){ for(int i=0;str[i];i++){ pdch(x,y,str[i],c,s); x+=gadv(str[i],s); } return x; }
int pdsub(int x,int y,const char*str,int a,int b,u32 c,int s){ for(int i=a;str[i]&&i<b;i++){ pdch(x,y,str[i],c,s); x+=gadv(str[i],s); } return x; }
void pdstr_ctr(int cx,int y,const char*s,u32 c,int sc){ pdstr(cx-pw_str(s,sc)/2,y,s,c,sc); }

/* ---- present SCENE -> hardware framebuffer (honours pitch + bpp) ---- */
    /* fwd: OLED setting + PIT ticks */
void blit_all(void){
    g_scene_quiet++; void blit_all2(void); blit_all2(); g_scene_quiet--; }
void blit_all2(void){
    if(g_gfx)gfx_sync();
    { extern int g_igpu_flip; extern void igpu_pre_blit(void); if(g_igpu_flip)igpu_pre_blit(); }
    /* OLED care: shift the whole image by 1px through 4 positions, changing every ~30s */
    int shx=0,shy=0; if(g_panel_oled){ int ph=(int)((g_ticks/3000ULL)&3); shx=(ph==1)?1:(ph==3)?-1:0; shy=(ph==2)?1:0; }
    { extern int g_igpu_flip, g_blt_present; extern int igpu_blt_present(void); if(g_igpu_flip&&g_blt_present&&!g_fb_rgb&&BYPP==4&&shx==0&&shy==0&&igpu_blt_present()==0)goto presented; }
    { extern int g_igpu_flip; extern void igpu_present_dirty_all(void); if(g_igpu_flip)igpu_present_dirty_all(); }
    for(u32 y=0;y<H;y++){ int sr=(int)y-shy; if(sr<0)sr=0; if(sr>=(int)H)sr=(int)H-1; u32*s=SCENE+(u64)sr*SW;
        if(BYPP==4){ u32*d=(u32*)(FBB+(u64)y*PITCHB);
            if(g_fb_rgb){ if(shx==0)copy_swap32(d,s,(int)W); else for(u32 x=0;x<W;x++){ int sx=(int)x-shx; if(sx<0)sx=0; if(sx>=(int)W)sx=(int)W-1; copy_swap32(d+x,s+sx,1); } }
            else if(!g_accel2d){ for(u32 x=0;x<W;x++){ int sx=(int)x-shx; if(sx<0)sx=0; if(sx>=(int)W)sx=(int)W-1; d[x]=s[sx]; } }
            else if(shx==0) sse_copy32(d,s,(int)W);
            else if(shx==1){ d[0]=s[0]; sse_copy32(d+1,s,(int)W-1); }
            else { sse_copy32(d,s+1,(int)W-1); d[W-1]=s[W-1]; } }
        else { u8*d=FBB+(u64)y*PITCHB;
            if(g_accel2d && shx==0) sse_pack24(d,s,(int)W);
            else for(u32 x=0;x<W;x++){ int sx=(int)x-shx; if(sx<0)sx=0; if(sx>=(int)W)sx=(int)W-1; u32 c=s[sx]; d[0]=(u8)c;d[1]=(u8)(c>>8);d[2]=(u8)(c>>16); d+=3; } } }
    presented:
    { extern int g_igpu_flip; extern void igpu_post_blit(void); if(g_igpu_flip)igpu_post_blit(); }
    if(g_fb2_ok&&g_mirror2)blit_mirror2();
}

/* ---- mirror the composed SCENE onto a second firmware-lit display (nearest-neighbour scale) ---- */
void blit_mirror2(void){
    if(!FBB2||!W2||!H2||!W||!H)return;
    for(u32 y=0;y<H2;y++){
        u32 sy=(u32)((u64)y*H/H2); if(sy>=H)sy=H-1; u32* srow=SCENE+(u64)sy*SW;
        if(BYPP2==4){ u32* d=(u32*)(FBB2+(u64)y*PITCHB2);
            for(u32 x=0;x<W2;x++){ u32 sx=(u32)((u64)x*W/W2); if(sx>=W)sx=W-1; u32 v=srow[sx];
                d[x]=g_fb2_rgb?((v&0xFF00FF00u)|((v>>16)&0xFFu)|((v&0xFFu)<<16)):v; } }
        else { u8* d=FBB2+(u64)y*PITCHB2;
            for(u32 x=0;x<W2;x++){ u32 sx=(u32)((u64)x*W/W2); if(sx>=W)sx=W-1; u32 c=srow[sx]; d[0]=(u8)c;d[1]=(u8)(c>>8);d[2]=(u8)(c>>16); d+=3; } }
    }
}

