#include "include/nvx/base.h"
#include "include/drivers.h"
#include "include/nvx/mem/heap_api.h"
#include "include/nvx/mem/heap.h"
#include "include/nvx/lib/base/fmt.h"
#include "include/nvx/drivers/video/fb/framebuffer.h"
#include "include/nvx/drivers/video/fb/gfx.h"
#include "include/nvx/graphics/theme/theme.h"
#include "include/nvx/graphics/ui/wm.h"
#include "include/nvx/graphics/ui/desktop.h"
#include "include/nvx/fs/vfs/ramfs.h"
#include "include/nvx/fs/vfs/dirtree.h"
#include "include/nvx/fs/vfs/unixfs.h"
#include "include/nvx/fs/nvx/nvx4.h"
#include "include/nvx/drivers/rtc/cmos.h"
#include "include/nvx/arch/x86_64/cpu/percpu.h"

extern int disk_ok; extern int g_rt; extern volatile u64 g_ticks;
void toast(const char*);
void gq_blend(int d,int x,int y,int w,int h,u32 c,int a);

int g_ss_on=0;
u32* g_clip_img=0; int g_clip_iw=0, g_clip_ih=0;
char g_ss_last[80];
static int ss_mode=0, ss_x0, ss_y0, ss_x1, ss_y1, ss_have=0, ss_drag=0, ss_pending=0, ss_bprev=0, ss_pick=-1;
static int ss_hx[8], ss_hy[8], ss_hw[8], ss_hh[8], ss_hid[8], ss_hn=0;
static void ss_hit(int x,int y,int w,int h,int id){ if(ss_hn<8){ ss_hx[ss_hn]=x; ss_hy[ss_hn]=y; ss_hw[ss_hn]=w; ss_hh[ss_hn]=h; ss_hid[ss_hn]=id; ss_hn++; } }

static u32 ss_crc_t[256]; static int ss_crc_ok=0;
static u32 ss_crc(u32 c,const u8* p,long n){ if(!ss_crc_ok){ for(u32 i=0;i<256;i++){ u32 v=i; for(int k=0;k<8;k++)v=(v&1)?0xEDB88320u^(v>>1):v>>1; ss_crc_t[i]=v; } ss_crc_ok=1; }
    c=~c; for(long i=0;i<n;i++)c=ss_crc_t[(c^p[i])&255]^(c>>8); return ~c; }
typedef struct { u8* o; long n, cap; u32 bb; int bc; } ssbits;
static void sb_put(ssbits* s,u32 v,int nb){ s->bb|=v<<s->bc; s->bc+=nb; while(s->bc>=8){ if(s->n<s->cap)s->o[s->n]=(u8)s->bb; s->n++; s->bb>>=8; s->bc-=8; } }
static void sb_huff(ssbits* s,u32 code,int len){ u32 r=0; for(int i=0;i<len;i++){ r=(r<<1)|(code&1); code>>=1; } sb_put(s,r,len); }
static void sb_lit(ssbits* s,int v){ if(v<144)sb_huff(s,0x30u+(u32)v,8); else if(v<256)sb_huff(s,0x190u+(u32)(v-144),9); else if(v<280)sb_huff(s,(u32)(v-256),7); else sb_huff(s,0xC0u+(u32)(v-280),8); }
static const u16 LB[29]={3,4,5,6,7,8,9,10,11,13,15,17,19,23,27,31,35,43,51,59,67,83,99,115,131,163,195,227,258};
static const u8 LE[29]={0,0,0,0,0,0,0,0,1,1,1,1,2,2,2,2,3,3,3,3,4,4,4,4,5,5,5,5,0};
static const u16 DB[30]={1,2,3,4,5,7,9,13,17,25,33,49,65,97,129,193,257,385,513,769,1025,1537,2049,3073,4097,6145,8193,12289,16385,24577};
static void sb_match(ssbits* s,int len,int dist){ int i=28; if(len<258){ i=0; while(i<27&&LB[i+1]<=len)i++; } sb_lit(s,257+i); if(LE[i])sb_put(s,(u32)(len-LB[i]),LE[i]);
    int d=0; while(d<29&&DB[d+1]<=dist)d++; sb_huff(s,(u32)d,5); int de=d<4?0:(d-2)/2; if(de)sb_put(s,(u32)(dist-DB[d]),de); }
static void be32(u8* p,u32 v){ p[0]=(u8)(v>>24); p[1]=(u8)(v>>16); p[2]=(u8)(v>>8); p[3]=(u8)v; }
static long png_encode(const u32* px,int stride,int w,int h,u8** out){
    long rb=1+(long)w*3, raw=rb*h; u8* f=(u8*)kmalloc((unsigned long)raw); if(!f)return -1;
    u8* cur=(u8*)kmalloc((unsigned long)rb*3); if(!cur){ kfree(f); return -1; } u8* up=cur+rb; u8* sub=up+rb;
    for(int y=0;y<h;y++){ const u32* r=px+(long)y*stride; const u32* pr=y?px+(long)(y-1)*stride:0; long su=0, ss=0;
        for(int x=0;x<w;x++){ u32 c=r[x]; u8 v[3]={(u8)(c>>16),(u8)(c>>8),(u8)c}; u32 p=pr?pr[x]:0; u8 a[3]={(u8)(p>>16),(u8)(p>>8),(u8)p}; u32 l=x?r[x-1]:0; u8 b[3]={(u8)(l>>16),(u8)(l>>8),(u8)l};
            for(int k=0;k<3;k++){ u8 du=(u8)(v[k]-a[k]), ds=(u8)(v[k]-b[k]); up[1+x*3+k]=du; sub[1+x*3+k]=ds; su+=du<128?du:256-du; ss+=ds<128?ds:256-ds; } }
        u8* d=f+(long)y*rb; const u8* pick=(y&&su<=ss)?up:sub; d[0]=(y&&su<=ss)?2:1; for(long i=1;i<rb;i++)d[i]=pick[i]; }
    kfree(cur);
    ssbits s; s.cap=raw+raw/8+4096; s.o=(u8*)kmalloc((unsigned long)s.cap+64); s.n=0; s.bb=0; s.bc=0; if(!s.o){ kfree(f); return -1; }
    sb_put(&s,1,1); sb_put(&s,1,2);
    int* head=(int*)kmalloc(32768*4*2); int* prv=head?head+32768:0; if(head)for(int k=0;k<32768;k++)head[k]=-1;
    long i=0; while(i<raw){ int best=0, bd=0;
        if(i+3<=raw){ u32 hv=(((u32)f[i]<<10)^((u32)f[i+1]<<5)^f[i+2])&32767u;
            if(i>0&&f[i]==f[i-1]){ int run=0; while(run<258&&i+run<raw&&f[i+run]==f[i-1])run++; if(run>=3){ best=run; bd=1; } }
            if(head){ long c=head[hv]; int depth=48; long lim=i+258<raw?i+258:raw;
                while(c>=0&&i-c<=32768&&depth-->0&&i+best<lim){ if(f[c+best]==f[i+best]&&f[c]==f[i]){ long k=0; while(i+k<lim&&f[c+k]==f[i+k])k++; if(k>best){ best=(int)k; bd=(int)(i-c); } }
                    long nc=prv[c&32767]; if(nc>=c)break; c=nc; }
                prv[i&32767]=head[hv]; head[hv]=(int)i; } }
        if(best>=3){ sb_match(&s,best,bd); if(head) for(long k=i+1;k<i+best&&k+3<=raw;k++){ u32 hv=(((u32)f[k]<<10)^((u32)f[k+1]<<5)^f[k+2])&32767u; prv[k&32767]=head[hv]; head[hv]=(int)k; } i+=best; }
        else { sb_lit(&s,f[i]); i++; } }
    if(head)kfree(head);
    sb_lit(&s,256); if(s.bc)sb_put(&s,0,8-s.bc);
    u32 a1=1,a2=0; for(long k=0;k<raw;k++){ a1=(a1+f[k])%65521u; a2=(a2+a1)%65521u; } kfree(f);
    long zl=2+s.n+4; long tot=8+25+12+zl+12; u8* o=(u8*)kmalloc((unsigned long)tot); if(!o){ kfree(s.o); return -1; }
    static const u8 SIG[8]={0x89,'P','N','G',0x0D,0x0A,0x1A,0x0A}; long q=0; for(int k=0;k<8;k++)o[q++]=SIG[k];
    be32(o+q,13); q+=4; long c0=q; o[q++]='I'; o[q++]='H'; o[q++]='D'; o[q++]='R'; be32(o+q,(u32)w); q+=4; be32(o+q,(u32)h); q+=4; o[q++]=8; o[q++]=2; o[q++]=0; o[q++]=0; o[q++]=0; be32(o+q,ss_crc(0,o+c0,q-c0)); q+=4;
    be32(o+q,(u32)zl); q+=4; c0=q; o[q++]='I'; o[q++]='D'; o[q++]='A'; o[q++]='T'; o[q++]=0x78; o[q++]=0x01; for(long k=0;k<s.n;k++)o[q++]=s.o[k]; be32(o+q,(a2<<16)|a1); q+=4; be32(o+q,ss_crc(0,o+c0,q-c0)); q+=4;
    be32(o+q,0); q+=4; c0=q; o[q++]='I'; o[q++]='E'; o[q++]='N'; o[q++]='D'; be32(o+q,ss_crc(0,o+c0,4)); q+=4;
    kfree(s.o); *out=o; return q; }

static char* ss_2(char* p,int v){ *p++=(char)('0'+(v/10)%10); *p++=(char)('0'+v%10); return p; }
static int ss_store(const char* name,const u8* d,long n){
    dt_init(); extern char g_home[]; int home=dt_resolve_dir(g_home); if(home<0)home=dt_resolve_dir("/home"); if(home<0)home=dt_mkdir_at(0,"home"); if(home<0)return -1;
    int pic=dt_child(home,"Pictures"); if(pic<0)pic=dt_mkdir_at(home,"Pictures"); if(pic<0)return -1;
    int k=-1; for(int i=0;i<FS_N;i++) if(!FS[i].used){ k=i; break; } if(k<0)return -1;
    fs_newslot(k); int q=0; while(name[q]&&q<63){ FS[k].name[q]=name[q]; q++; } FS[k].name[q]=0;
    FS[k].used=1; FS[k].folder=0; FS[k].dir=pic; FS[k].mode=0100644u; FS[k].uid=g_uid; FS[k].gid=g_gid;
    if(fs_set(k,(const char*)d,n)){ FS[k].used=0; return -1; } g_fs_dirty[k]=1; if(disk_ok)fs_sync(k); return k; }
static void ss_capture(int x,int y,int w,int h){
    if(x<0){ w+=x; x=0; } if(y<0){ h+=y; y=0; } if(x+w>(int)W)w=(int)W-x; if(y+h>(int)H)h=(int)H-y;
    if(w<2||h<2){ toast("Screenshot: nothing selected"); return; }
    { extern int g_gfx; extern void gfx_sync(void); if(g_gfx)gfx_sync(); }
    const u32* sc=SCENE; u32* img=(u32*)kmalloc((unsigned long)w*h*4); if(!img){ toast("Screenshot: not enough memory"); return; }
    for(int j=0;j<h;j++){ const u32* r=sc+(u64)(y+j)*SW+x; u32* d=img+(long)j*w; for(int i=0;i<w;i++)d[i]=r[i]&0x00FFFFFFu; }
    if(g_clip_img)kfree(g_clip_img); g_clip_img=img; g_clip_iw=w; g_clip_ih=h;
    u8* png=0; long n=png_encode(img,w,w,h,&png); if(n<0){ toast("Screenshot copied, but it could not be saved (memory)"); return; }
    int hh=12,mm=0,ss=0,yr=2026,mo=1,dy=1; read_clock(&hh,&mm,&ss,&yr,&mo,&dy);
    char nm[64],*p=nm; p=scopy(p,"Screenshot-"); p=udec(p,(u32)yr); *p++='-'; p=ss_2(p,mo); *p++='-'; p=ss_2(p,dy); *p++='-'; p=ss_2(p,hh); p=ss_2(p,mm); p=ss_2(p,ss); *p=0;
    { int dup=0; for(int i=0;i<FS_N;i++) if(FS[i].used&&seq(FS[i].name,nm))dup=1; if(dup){ static int sn=1; *p++='-'; p=udec(p,(u32)(sn++)); *p=0; } }
    p=scopy(p,".png"); *p=0;
    int r=ss_store(nm,png,n); kfree(png);
    p=g_ss_last; { extern char g_home[]; p=scopy(p,g_home); } p=scopy(p,"/Pictures/"); p=scopy(p,nm); *p=0;
    { extern void klog(const char*); char t[120],*q=t; q=scopy(q,"screenshot: "); q=udec(q,(u32)w); *q++='x'; q=udec(q,(u32)h); q=scopy(q,", "); q=udec(q,(u32)(n/1024)); q=scopy(q," KB, "); q=scopy(q,r>=0?g_ss_last:"not saved"); *q=0; klog(t); }
    toast(r>=0?"Screenshot saved to Pictures and copied":"Screenshot copied, but it could not be saved"); }
static int ss_win_at(int x,int y){ int top=-1,tz=-1; for(int i=0;i<NWIN;i++) if(WN[i].open&&!WN[i].min&&in_rect(x,y,WN[i].x,WN[i].y,WN[i].w,WN[i].h)&&WN[i].z>tz){ tz=WN[i].z; top=i; } return top; }
static void ss_norm(int* x,int* y,int* w,int* h){ int a=ss_x0<ss_x1?ss_x0:ss_x1, b=ss_y0<ss_y1?ss_y0:ss_y1; *x=a; *y=b; *w=(ss_x0<ss_x1?ss_x1-ss_x0:ss_x0-ss_x1)+1; *h=(ss_y0<ss_y1?ss_y1-ss_y0:ss_y0-ss_y1)+1; }
static int ss_ready(void){ return ss_mode==1||(ss_mode==0&&ss_have)||(ss_mode==2&&(ss_pick>=0||ss_win_at(mx,my)>=0)); }
static void ss_go(void){ if(ss_mode==2&&ss_pick<0)ss_pick=ss_win_at(mx,my); if(!ss_ready())return; ss_pending=1; }
void ss_open(int mode,int now){ g_ss_on=1; ss_mode=mode; ss_have=0; ss_drag=0; ss_pick=-1; ss_pending=0; ss_bprev=1; if(now){ if(mode==2)ss_pick=ss_win_at(mx,my); if(mode==2&&ss_pick<0)ss_pick=focus; ss_pending=1; } }
void ss_close(void){ g_ss_on=0; ss_pending=0; ss_drag=0; }
static void ss_dim(int x,int y,int w,int h,int a){ if(w<=0||h<=0)return; if(x<0){ w+=x; x=0; } if(y<0){ h+=y; y=0; } if(x+w>(int)W)w=(int)W-x; if(y+h>(int)H)h=(int)H-y; if(w<=0||h<=0)return;
    if(g_gfx){ gq_blend(g_rt,x,y,w,h,rgb(0,0,0),a); return; } u32* sc=SCENE; for(int j=0;j<h;j++){ u32* r=sc+(u64)(y+j)*SW+x; for(int i=0;i<w;i++)r[i]=mix(r[i],0,a); } }
static void ss_frame(int x,int y,int w,int h,u32 c,int t){ fill_rect(x-t,y-t,w+2*t,t,c); fill_rect(x-t,y+h,w+2*t,t,c); fill_rect(x-t,y,t,h,c); fill_rect(x+w,y,t,h,c); }
static void ss_icon(int x,int y,int k,u32 c,u32 bg){
    if(k==0){ for(int i=0;i<18;i+=4){ fill_rect(x+i,y,2,2,c); fill_rect(x+i,y+16,2,2,c); fill_rect(x,y+i,2,2,c); fill_rect(x+16,y+i,2,2,c); } }
    else if(k==1){ fill_round(x,y+1,18,13,2,15,c); fill_rect(x+2,y+3,14,9,bg); fill_rect(x+7,y+14,4,2,c); fill_rect(x+4,y+16,10,2,c); }
    else { fill_round(x,y+1,18,15,2,15,c); fill_rect(x+2,y+5,14,9,bg); fill_rect(x+12,y+2,2,2,bg); fill_rect(x+15,y+2,1,2,bg); } }
void ss_draw(void){
    if(!g_ss_on)return;
    if(ss_pending){ int x=0,y=0,w=(int)W,h=(int)H;
        if(ss_mode==0)ss_norm(&x,&y,&w,&h); else if(ss_mode==2){ int i=ss_pick; if(i>=0&&i<NWIN&&WN[i].open){ x=WN[i].x; y=WN[i].y; w=WN[i].w; h=WN[i].h; } }
        ss_close(); ss_capture(x,y,w,h); return; }
    if(!g_gm_ready)gmetrics(); ss_hn=0;
    if(ss_mode==0){ if(ss_have||ss_drag){ int x,y,w,h; ss_norm(&x,&y,&w,&h); ss_dim(0,0,(int)W,y,150); ss_dim(0,y+h,(int)W,(int)H-y-h,150); ss_dim(0,y,x,h,150); ss_dim(x+w,y,(int)W-x-w,h,150); ss_frame(x,y,w,h,rgb(255,255,255),2);
            if(!ss_drag){ fill_circle(x,y,5,rgb(255,255,255)); fill_circle(x+w,y,5,rgb(255,255,255)); fill_circle(x,y+h,5,rgb(255,255,255)); fill_circle(x+w,y+h,5,rgb(255,255,255)); }
            char b[24],*p=b; p=udec(p,(u32)w); p=scopy(p," x "); p=udec(p,(u32)h); *p=0; int tw=pw_str(b,1)+16, ty=y>=34?y-30:y+h+6; fill_round(x,ty,tw,24,8,15,rgb(36,36,40)); pdstr(x+8,ty+4,b,rgb(255,255,255),1); }
        else ss_dim(0,0,(int)W,(int)H,150); }
    else if(ss_mode==2){ int i=ss_win_at(mx,my); if(i>=0){ int x=WN[i].x,y=WN[i].y,w=WN[i].w,h=WN[i].h; ss_dim(0,0,(int)W,y,150); ss_dim(0,y+h,(int)W,(int)H-y-h,150); ss_dim(0,y,x,h,150); ss_dim(x+w,y,(int)W-x-w,h,150); ss_frame(x,y,w,h,ACC,3); } else ss_dim(0,0,(int)W,(int)H,150); }
    else ss_frame(3,3,(int)W-6,(int)H-6,ACC,3);
    static const char* ML[3]={"Area","Screen","Window"};
    int bw[3], tw=0; for(int k=0;k<3;k++){ bw[k]=pw_str(ML[k],1)+52; tw+=bw[k]+6; }
    int pw=tw+16+64+16+40, ph=68, px=(int)W/2-pw/2, py=(int)H-ph-90; u32 pb=rgb(36,36,40);
    { const char* hint=ss_mode==0?(ss_have?"Press Enter or the round button to take the screenshot":"Drag to choose an area"):ss_mode==1?"Press Enter or the round button to capture the whole screen":"Click a window to capture it"; int hw=pw_str(hint,1)+28; fill_round((int)W/2-hw/2,py-44,hw,32,16,15,pb); pdstr_ctr((int)W/2,py-36,hint,rgb(235,235,240),1); }
    fill_round(px+2,py+3,pw,ph,20,15,rgb(0,0,0)); fill_round(px,py,pw,ph,20,15,pb);
    int bx=px+14; for(int k=0;k<3;k++){ int on=(k==ss_mode), hov=in_rect(mx,my,bx,py+14,bw[k],40); u32 bg=on?rgb(78,78,86):hov?rgb(56,56,62):pb; if(on||hov)fill_round(bx,py+14,bw[k],40,10,15,bg);
        ss_icon(bx+12,py+25,k,rgb(240,240,245),bg); pdstr(bx+40,py+26,ML[k],rgb(240,240,245),1); if(on)pdstr(bx+41,py+26,ML[k],rgb(240,240,245),1); ss_hit(bx,py+14,bw[k],40,10+k); bx+=bw[k]+6; }
    { int cx=bx+8+32, cy=py+ph/2, en=ss_ready(), hov=in_rect(mx,my,cx-26,cy-26,52,52); fill_circle(cx,cy,25,en?rgb(255,255,255):rgb(120,120,126)); fill_circle(cx,cy,21,pb); fill_circle(cx,cy,17,en?(hov?rgb(255,255,255):rgb(232,232,236)):rgb(120,120,126)); ss_hit(cx-26,cy-26,52,52,20); }
    { int cx=px+pw-30, cy=py+ph/2, hov=in_rect(mx,my,cx-14,cy-14,28,28); if(hov)fill_circle(cx,cy,14,rgb(70,70,76)); for(int t=0;t<10;t++){ fill_rect(cx-5+t,cy-5+t,2,2,rgb(240,240,245)); fill_rect(cx+4-t,cy-5+t,2,2,rgb(240,240,245)); } ss_hit(cx-14,cy-14,28,28,21); } }
int ss_mouse(int x,int y,int b){
    int down=(b&1)&&!(ss_bprev&1), up=!(b&1)&&(ss_bprev&1), rdown=(b&2)&&!(ss_bprev&2); ss_bprev=b;
    if(rdown){ ss_close(); return 1; }
    if(down){ for(int k=ss_hn-1;k>=0;k--) if(in_rect(x,y,ss_hx[k],ss_hy[k],ss_hw[k],ss_hh[k])){ int id=ss_hid[k]; if(id>=10&&id<13){ ss_mode=id-10; ss_have=0; ss_pick=-1; } else if(id==20)ss_go(); else if(id==21)ss_close(); return 1; }
        if(ss_mode==0){ ss_drag=1; ss_have=0; ss_x0=ss_x1=x; ss_y0=ss_y1=y; }
        else if(ss_mode==2){ ss_pick=ss_win_at(x,y); if(ss_pick>=0)ss_pending=1; }
        else ss_pending=1;
        return 1; }
    if(ss_drag){ ss_x1=x; ss_y1=y; if(up){ ss_drag=0; int a,c,w,h; ss_norm(&a,&c,&w,&h); ss_have=(w>=4&&h>=4); } }
    return 1; }
int ss_key(int c){
    if(c==27){ ss_close(); return 1; }
    if(c=='\n'||c==' '){ ss_go(); return 1; }
    if(c=='a'||c=='A'){ ss_mode=0; ss_have=0; } else if(c=='s'||c=='S'){ ss_mode=1; } else if(c=='w'||c=='W'){ ss_mode=2; ss_pick=-1; }
    return 1; }
void ss_cmd(const char* a){
    while(a&&*a==' ')a++;
    if(!a||!*a){ ss_open(0,0); return; }
    if(a[0]=='s'){ ss_open(1,1); return; } if(a[0]=='w'){ ss_open(2,1); return; } if(a[0]=='a'){ ss_open(0,0); return; }
    if(a[0]=='l'){ extern void term_puts(const char*); term_puts(g_ss_last[0]?g_ss_last:"no screenshot yet"); term_puts("\n"); return; } }
