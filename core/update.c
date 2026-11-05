#include "include/nvx/base.h"
#include "include/nvx/core/update.h"
#include "include/nvx/core/klog.h"
#include "include/nvx/mem/heap.h"
#include "include/nvx/core/buildsig.h"
#include "include/nvx/core/crash.h"
#include "include/nvx/lib/crypto/nvxcrypto.h"
#include "include/nvx/lib/crypto/nvxtls.h"
#include "include/nvx/fs/fat/vfat.h"
#include "include/nvx/fs/ntfs/ntfsk.h"
#include "include/nvx/fs/pkg/pkg.h"
#include "include/nvx/fs/vfs/dirtree.h"
#include "include/nvx/drivers/storage/nvme.h"
#include "include/nvx/drivers/video/fb/framebuffer.h"
#include "include/nvx/net/http/http.h"
#include "include/nvx/graphics/theme/theme.h"
#include "include/nvx/core/init/installer.h"
#include "include/nvx/fs/vfs/ramfs.h"
#include "include/nvx/drivers/usb/xhci.h"
#include "include/nvx/fs/vfs/sysroot.h"

extern volatile u64 g_ticks;
extern volatile u32 g_ml_beat;
extern int g_state;
extern int disk_ok;
extern void (*g_wait_pump)(void);
void toast(const char*);
void term_puts(const char*);
void power_reboot(void);
void nvx3_sync_all(void);
void fb_shadow_invalidate(void);
int isin(int deg);
int icos(int deg);
char* fsd(int k);
void nvx_mark_file(const char*,const char*);

upd_info g_upd;

#define UP_MANIFEST "https://" NVX_HOST "/api/public/update"
#define UP_KBIN "/boot/kernel64.bin"
#define UP_KOLD "/boot/kernel64.old"
#define UP_KROOT "/KERNEL64.BIN"
#define UP_STATE "/boot/update.txt"
#define UP_CFG "/boot.cfg"
#define UP_MARGIN (1L<<20)

static const char* UP_SN[9]={"none","OK","PREP","SWAP","TRY0","TRY1","TRY2","FAIL","OLD "};
const char* upd_state_name(int st){ static const char* N[9]={"no update recorded","up to date","update was being prepared","new version was being written","new version waiting for its first start","new version starting (try 1 of 2)","new version starting (try 2 of 2)","new version did not start","previous version in use"}; return st>=0&&st<9?N[st]:"unknown"; }

static int up_len(const char* s){ int n=0; while(s&&s[n])n++; return n; }
static char* up_cpy(char* d,const char* s){ while(s&&*s)*d++=*s++; *d=0; return d; }
static void up_ncpy(char* d,const char* s,int cap){ int i=0; while(s&&s[i]&&i<cap-1){ d[i]=s[i]; i++; } d[i]=0; }
static char* up_num(char* d,unsigned long long v){ char t[24]; int n=0; if(!v)t[n++]='0'; while(v){ t[n++]=(char)('0'+v%10); v/=10; } while(n)*d++=t[--n]; *d=0; return d; }
static int up_eq(const char* a,const char* b){ while(*a&&*a==*b){ a++; b++; } return *a==*b; }
static int up_has(const char* h,long n,const char* nd){ int m=up_len(nd); for(long i=0;i+m<=n;i++){ int k=0; while(k<m&&h[i+k]==nd[k])k++; if(k==m)return 1; } return 0; }
static u32 up_rd32(const u8* p){ return (u32)p[0]|((u32)p[1]<<8)|((u32)p[2]<<16)|((u32)p[3]<<24); }
static void up_set_why(const char* a,const char* b){ char* p=g_upd.why; char* e=g_upd.why+sizeof(g_upd.why)-1; while(a&&*a&&p<e)*p++=*a++; while(b&&*b&&p<e)*p++=*b++; *p=0; }

int upd_vnum(const char* v){ int a=0,b=0,nb=0; while(*v=='v'||*v=='V')v++; while(*v>='0'&&*v<='9'){ a=a*10+(*v-'0'); v++; } if(*v=='.'){ v++; while(*v>='0'&&*v<='9'&&nb<2){ b=b*10+(*v-'0'); v++; nb++; } if(nb==1)b*=10; } return a*100+b; }

static int g_us_on=0, g_us_pct=0, g_us_phase=0; static long g_us_dl_total=0; static u64 g_us_last=0; static char g_us_l1[64], g_us_det[170]; static int g_us_drawn=0;
static unsigned long long g_us_wdone=0, g_us_wtotal=0; static int g_us_wlo=0, g_us_whi=0;
extern const u8 wpf_npt; extern const int wpf_ncp; extern const struct { u16 adv16; signed char x0, y0; u8 w, h; u32 off; } wpf_g[]; extern const u8 _binary_wpf_data_bin_start[];
static int us_ci(int ch){ if(ch>=32&&ch<127)return ch-32; return '?'-32; }
static int us_tw(const char* t,int sz){ int w=0, b=sz*wpf_ncp; for(;*t;t++)w+=wpf_g[b+us_ci((u8)*t)].adv16; return w/16; }
static void us_text(int cx,int base,const char* t,int sz,u32 col){
    u32* sc=SCENE; int b=sz*wpf_ncp; int x16=(cx-us_tw(t,sz)/2)*16;
    for(;*t;t++){ int gi=b+us_ci((u8)*t); int w=wpf_g[gi].w, h=wpf_g[gi].h; const u8* d=_binary_wpf_data_bin_start+wpf_g[gi].off; int bw=(w+1)/2;
        int gx=x16/16+wpf_g[gi].x0, gy=base+wpf_g[gi].y0;
        for(int r=0;r<h;r++){ int yy=gy+r; if(yy<0||(u32)yy>=H)continue; u32* row=sc+(u64)yy*SW;
            for(int k=0;k<w;k++){ int xx=gx+k; if(xx<0||(u32)xx>=W)continue; u8 v=d[r*bw+k/2]; int a=(k&1)?(v&15):(v>>4); if(a)row[xx]=mix(row[xx],col,a*17*256/255); } }
        x16+=wpf_g[gi].adv16; } }
static void us_frame(void){
    int w=(int)W, h=(int)H; if(w<=0||h<=0)return;
    u32 bg=rgb(43,95,201), fg=rgb(255,255,255), dim=mix(bg,fg,214), faint=mix(bg,fg,170);
    if(!g_gm_ready)gmetrics();
    if(!g_us_drawn){ fill_rect(0,0,w,h,bg); g_us_drawn=1; }
    int cx=w/2, cy=h/2;
    int bx=cx-420, by=cy-90, bw=840, bh=190; if(bx<0){ bx=0; bw=w; }
    fill_rect(bx,by,bw,bh,bg);
    { int a=(int)((g_ticks/14)%5); for(int i=0;i<5;i++){ int al=i==a?256:i==(a+1)%5?180:90; fill_circle(cx-40+i*20,cy-60,5,mix(bg,fg,al)); } }
    char l[96],*p=l; p=up_cpy(p,g_us_l1); if(g_us_pct>=0&&g_us_pct<100){ p=up_cpy(p," "); p=up_num(p,(unsigned)g_us_pct); p=up_cpy(p,"%"); }
    us_text(cx,cy+8,l,8,fg);
    if(g_us_pct<0){ if(g_us_det[0])us_text(cx,cy+44,g_us_det,4,dim); }
    else us_text(cx,cy+44,"Don't turn off your computer",4,dim);
    fill_rect(0,h-56,w,24,bg);
    us_text(cx,h-38,g_us_pct<0?"Your computer will keep using the current version":"Your computer will restart once when it's done",2,faint);
    blit_all();
}
static void us_pump(void){
    g_ml_beat++;
    if(!g_us_on||g_ticks-g_us_last<4)return; g_us_last=g_ticks;
    if(g_us_phase==1&&g_us_dl_total>0){ long got=g_net_rx; if(got>g_us_dl_total)got=g_us_dl_total; int pc=(int)(got*55/g_us_dl_total); if(pc>g_us_pct)g_us_pct=pc;
        char* p=g_us_det; p=up_cpy(p,"Downloading NoovexOS "); p=up_cpy(p,g_upd.latest); p=up_cpy(p," - "); p=up_num(p,(unsigned long long)got/1048576ULL); p=up_cpy(p," of "); p=up_num(p,(unsigned long long)g_us_dl_total/1048576ULL); p=up_cpy(p," MB"); }
    if(g_us_phase==3&&g_us_wtotal){ unsigned long long d=g_us_wdone; if(d>g_us_wtotal)d=g_us_wtotal; int pc=g_us_wlo+(int)(d*(unsigned long long)(g_us_whi-g_us_wlo)/g_us_wtotal); if(pc>g_us_pct)g_us_pct=pc; }
    us_frame(); }
void upd_screen_begin(const char* l1){ g_frz_hold=1; g_us_on=1; g_us_drawn=0; g_us_pct=0; g_us_phase=0; g_us_det[0]=0; up_ncpy(g_us_l1,l1,sizeof g_us_l1); g_us_last=0; us_frame(); }
void upd_screen(int pct,const char* detail){ if(pct>g_us_pct||pct<0)g_us_pct=pct; if(detail){ up_ncpy(g_us_det,detail,sizeof g_us_det); klog2("update: ",detail); } g_us_last=0; if(g_us_on){ g_ml_beat++; us_frame(); g_us_last=g_ticks; } }
static void us_title(const char* l1){ up_ncpy(g_us_l1,l1,sizeof g_us_l1); }
void upd_screen_end(void){ g_us_on=0; g_us_phase=0; g_frz_hold=0; fb_shadow_invalidate(); }
static void us_hold(int ticks){ u64 t=g_ticks; for(long k=0;g_ticks-t<(u64)ticks&&k<(long)ticks*400;k++){ us_pump(); for(int i=0;i<20000;i++)cpu_pause(); } }

static int up_rd(unsigned long long lba,int n,void* buf){ if(g_us_phase==3)g_us_wdone+=(unsigned long long)n; if(g_us_on)us_pump(); else g_ml_beat++;
    if(g_upd.usb)return lba+(unsigned long long)n<=0xFFFFFFFFULL?usb_msd_rdn((u32)lba,n,(u8*)buf):-1;
    if(dk_ratio()==1&&lba+(unsigned long long)n<=0xFFFFFFFFULL)return gdisk_readn((u32)lba,n,(u8*)buf); return nk_blk_rd(lba,n,buf); }
static int up_wr(unsigned long long lba,int n,const void* buf){ g_us_wdone+=(unsigned long long)n; if(g_us_on)us_pump(); else g_ml_beat++;
    if(g_upd.usb)return lba+(unsigned long long)n<=0xFFFFFFFFULL?usb_msd_wrn((u32)lba,n,(const u8*)buf):-1;
    if(dk_ratio()==1&&lba+(unsigned long long)n<=0xFFFFFFFFULL)return gdisk_writen((u32)lba,n,(const u8*)buf); return nk_blk_wr(lba,n,buf); }
static void up_flush(void){ if(g_upd.usb)usb_msd_sync(); else gdisk_flush(); }
static const u8* g_up_mem; static long g_up_memlen;
static int up_memrd(unsigned long long lba,int n,void* buf){ unsigned long long o=lba*512ULL, e=o+(unsigned long long)n*512ULL; if(e>(unsigned long long)g_up_memlen)return -1; const u8* s=g_up_mem+o; u8* d=(u8*)buf; for(int i=0;i<n*512;i++)d[i]=s[i]; return 0; }

static struct nvx_g_vfat_t g_up_save; static int g_up_swapped=0, g_up_usbw=0;
static void up_close(void){ vf_umount(); if(g_up_swapped){ vf_swap_in(&g_up_save); g_up_swapped=0; } if(g_up_usbw){ g_up_usbw=0; ufat_mount(); } }
static int up_mount(nvxblk_rd rd,nvxblk_wr wr,unsigned long long base,unsigned long long n,int ro){
    if(g_up_swapped)up_close(); vf_swap_out(&g_up_save); g_up_swapped=1;
    if(g_upd.usb&&rd==up_rd&&!ro){ ufat_sync(); g_up_usbw=1; }
    if(vf_mount(rd,wr,base,n,ro)){ up_close(); return -1; } return 0; }
static int up_open(int ro){ if(!g_upd.pbase)return -1; return up_mount(up_rd,up_wr,g_upd.pbase,g_upd.pn,ro); }

static int up_installed(void){ return disk_ok&&k_find_file("/etc/nvx-installed")>=0; }
static int up_part_scan(const u8* m){
    for(int pass=0;pass<2;pass++) for(int i=0;i<4;i++){ const u8* pe=m+446+i*16; u32 st=up_rd32(pe+8), n=up_rd32(pe+12);
        if(!st||!n||(pe[4]!=0x0C&&pe[4]!=0x0B))continue; if((pass==0)!=(pe[0]==0x80))continue;
        g_upd.pbase=st; g_upd.pn=n;
        if(up_open(1)==0){ int ok=vf_exists(UP_CFG)&&(vf_exists(UP_KBIN)||vf_exists(UP_KOLD)||vf_exists(UP_KROOT)); up_close(); if(ok)return 1; }
        g_upd.pbase=0; g_upd.pn=0; }
    return 0; }
static u64 g_up_usb_try=0;
static int up_find_usb(void){
    g_upd.usb=0; g_upd.probed=1;
    if(!msd_ok&&(!g_up_usb_try||g_ticks-g_up_usb_try>1000)){ extern int msd_autoinit_safe(void); g_up_usb_try=g_ticks?g_ticks:1; msd_autoinit_safe(); if(msd_ok)ufat_mount(); }
    if(!msd_ok||msd_bsize!=512){ up_set_why("This copy of NoovexOS runs from the ISO or a CD, so it cannot update itself. Write the new ISO to a USB stick or install NoovexOS on a disk.",0); return 0; }
    u8 m[512]; if(usb_msd_rdn(0,1,m)||m[510]!=0x55||m[511]!=0xAA){ up_set_why("The partition table of the USB stick could not be read.",0); return 0; }
    ufat_sync(); g_upd.usb=1;
    if(up_part_scan(m))return 1;
    g_upd.usb=0; up_set_why("The USB stick does not hold NoovexOS. Updates from a USB stick work on a stick made from the NoovexOS ISO.",0); return 0; }
static int up_find(void){
    if(g_upd.pbase){ if(!g_upd.usb||msd_ok)return 1; g_upd.pbase=0; g_upd.pn=0; g_upd.usb=0; }
    if(!up_installed())return up_find_usb();
    if(g_upd.probed)return 0; g_upd.probed=1;
    gdisk_ensure(); if(g_dktype==DK_NONE){ up_set_why("No system disk was found.",0); return 0; }
    if(dk_ratio()!=1){ up_set_why("Updates from inside NoovexOS need a disk with 512-byte sectors. Install the new version from the ISO.",0); return 0; }
    u8 m[512]; if(nk_blk_rd(0,1,m)||m[510]!=0x55||m[511]!=0xAA){ up_set_why("The partition table of the system disk could not be read.",0); return 0; }
    if(up_part_scan(m)){ g_upd.installed=1; return 1; }
    up_set_why("The NoovexOS boot partition was not found on the system disk.",0); return 0;
}
static int up_grow(void){
    u8 m[512], b[512], c[512];
    if(up_rd(0,1,m)||m[510]!=0x55||m[511]!=0xAA)return -1;
    int pi=-1; for(int i=0;i<4;i++) if(up_rd32(m+446+i*16+8)==(u32)g_upd.pbase)pi=i; if(pi<0)return -1;
    u8* pe=m+446+pi*16; u32 st=up_rd32(pe+8), pn=up_rd32(pe+12);
    if(up_rd(st,1,b)||b[510]!=0x55||b[511]!=0xAA||b[11]!=0||b[12]!=2)return -1;
    u32 spc=b[13], rsvd=(u32)b[14]|((u32)b[15]<<8), nf=b[16], fz=up_rd32(b+36), ts=up_rd32(b+32), bk=(u32)b[50]|((u32)b[51]<<8);
    if(!spc||!rsvd||!nf||!fz||b[19]||b[20]||ts>pn)return -1;
    u32 ds=rsvd+nf*fz, maxcl=fz*128u-2u; unsigned long long maxs=(unsigned long long)ds+(unsigned long long)maxcl*spc;
    unsigned long long lim=usb_msd_sectors(); if(lim<=st)return 0; lim-=st;
    for(int i=0;i<4;i++){ if(i==pi)continue; u32 os=up_rd32(m+446+i*16+8), on=up_rd32(m+446+i*16+12); if(on&&os>st&&(unsigned long long)(os-st)<lim)lim=os-st; }
    unsigned long long nn=maxs<lim?maxs:lim; if(nn>0xFFFFFFFFULL)nn=0xFFFFFFFFULL; nn&=~7ULL;
    if(nn<(unsigned long long)ts+16384ULL)return 0;
    u32 oldcl=(ts-ds)/spc, newcl=(u32)((nn-ds)/spc);
    { u8* fb=(u8*)kmalloc(64*512); if(!fb)return -1; u32 s0=(oldcl+2)/128, s1=(newcl+1)/128;
      for(u32 sct=s0; sct<=s1; sct+=64){ int k=(int)(s1-sct+1>64?64:s1-sct+1); if(up_rd(st+rsvd+sct,k,fb)){ kfree(fb); return -1; }
          for(int e=0;e<k*128;e++){ u32 cl=sct*128+(u32)e; if(cl<oldcl+2||cl>=newcl+2)continue; if(up_rd32(fb+e*4)&0x0FFFFFFFu){ kfree(fb); return -2; } } }
      kfree(fb); }
    { char t[96],*p=t; p=up_cpy(p,"update: growing the USB stick partition from "); p=up_num(p,ts/2048); p=up_cpy(p," MB to "); p=up_num(p,nn/2048); up_cpy(p," MB"); klog(t); }
    { u8 v[512]; if(up_rd(64,1,v)==0&&v[0]==1&&v[1]=='C'&&v[2]=='D'&&v[3]=='0'&&v[4]=='0'&&v[5]=='1'){ unsigned long long e2=((unsigned long long)st+nn+3ULL)/4ULL; u32 cur=up_rd32(v+80);
        if(e2<=0xFFFFFFFFULL&&cur<e2){ u32 x=(u32)e2; v[80]=(u8)x; v[81]=(u8)(x>>8); v[82]=(u8)(x>>16); v[83]=(u8)(x>>24); v[84]=(u8)(x>>24); v[85]=(u8)(x>>16); v[86]=(u8)(x>>8); v[87]=(u8)x;
            if(up_wr(64,1,v)||up_rd(64,1,c))return -1; for(int i=0;i<512;i++) if(c[i]!=v[i])return -1; } } }
    if(nn>pn){ pe[12]=(u8)nn; pe[13]=(u8)(nn>>8); pe[14]=(u8)(nn>>16); pe[15]=(u8)(nn>>24); pe[5]=0xFE; pe[6]=0xFF; pe[7]=0xFF;
        if(up_wr(0,1,m)||up_rd(0,1,c))return -1; for(int i=0;i<512;i++) if(c[i]!=m[i])return -1; }
    b[32]=(u8)nn; b[33]=(u8)(nn>>8); b[34]=(u8)(nn>>16); b[35]=(u8)(nn>>24);
    if(bk&&bk<rsvd){ u8 k2[512]; if(up_rd(st+bk,1,k2)==0&&k2[510]==0x55&&k2[511]==0xAA&&up_rd32(k2+32)==ts){ for(int i=32;i<36;i++)k2[i]=b[i]; if(up_wr(st+bk,1,k2))return -1; } }
    if(up_wr(st,1,b)||up_rd(st,1,c))return -1; for(int i=0;i<512;i++) if(c[i]!=b[i])return -1;
    up_flush(); g_upd.pn=nn; return 1; }
static void up_parse_state(const char* b,long n){
    g_upd.state=UPS_NONE; g_upd.newv[0]=0; g_upd.oldv[0]=0; if(n<4)return;
    for(int s=1;s<9;s++){ const char* t=UP_SN[s]; int tl=up_len(t), ok=1; for(int k=0;k<4;k++){ char c=k<tl?t[k]:' '; if(b[k]!=c){ ok=0; break; } } if(ok){ g_upd.state=s; break; } }
    long i=0; while(i<n){ long l=i; while(i<n&&b[i]!='\n')i++; long e=i; i++;
        if(e-l>4&&b[l]=='n'&&b[l+1]=='e'&&b[l+2]=='w'&&b[l+3]=='='){ int k=0; for(long j=l+4;j<e&&k<15&&b[j]>' ';j++)g_upd.newv[k++]=b[j]; g_upd.newv[k]=0; }
        if(e-l>4&&b[l]=='o'&&b[l+1]=='l'&&b[l+2]=='d'&&b[l+3]=='='){ int k=0; for(long j=l+4;j<e&&k<15&&b[j]>' ';j++)g_upd.oldv[k++]=b[j]; g_upd.oldv[k]=0; } }
}
static int up_state_load(void){ char b[520]; long n=vf_read(UP_STATE,b,512); if(n<0)n=0; up_parse_state(b,n); g_upd.has_old=vf_exists(UP_KOLD); g_upd.has_new=vf_size(UP_KBIN)>4096; return g_upd.state; }
static int up_state_save(int st){
    char b[512]; for(int i=0;i<512;i++)b[i]='\n';
    char t[96],*p=t; const char* nm=UP_SN[st]; for(int k=0;k<4;k++)*p++=(char)(k<up_len(nm)?nm[k]:' ');
    p=up_cpy(p,"\nnew="); p=up_cpy(p,g_upd.newv); p=up_cpy(p,"\nold="); p=up_cpy(p,g_upd.oldv); p=up_cpy(p,"\n");
    for(int i=0;t+i<p;i++)b[i]=t[i];
    long r=vf_size(UP_STATE)==512?vf_inplace(UP_STATE,b,512):vf_write(UP_STATE,b,512);
    if(r!=512)return -1;
    char c[520]; if(vf_read(UP_STATE,c,512)!=512)return -1; for(int i=0;i<512;i++) if(c[i]!=b[i])return -1;
    up_flush(); g_upd.state=st; return 0;
}

int upd_kcheck(const u8* k,u32 n,char* ver){
    const volatile u8* mg=(const volatile u8*)g_buildsig.magic; u32 off=0xFFFFFFFFu;
    if(n<4096)return -1;
    for(u32 i=0;i+128<=n;i++){ if(k[i]!=mg[0])continue; int ok=1; for(int j=1;j<8;j++) if(k[i+j]!=mg[j]){ ok=0; break; } if(ok){ off=i; break; } if((i&0xFFFFF)==0){ if(g_us_on)us_pump(); else g_ml_beat++; } }
    if(off==0xFFFFFFFFu)return -2;
    if(ver){ int j=0; for(;j<15&&k[off+8+j]>=' '&&k[off+8+j]<127;j++)ver[j]=(char)k[off+8+j]; ver[j]=0; }
    if(up_rd32(k+off+28)!=n)return -3;
    int zero=1; for(int j=0;j<32;j++) if(k[off+88+j])zero=0; if(zero)return 1;
    sha256 c; sha256_init(&c); u8 z[32]; for(int j=0;j<32;j++)z[j]=0;
    u32 seg[3][2]={{0,off+32},{off+64,off+88},{off+120,n}};
    for(int s=0;s<3;s++){ u32 a=seg[s][0], e=seg[s][1]; while(a<e){ u32 q=e-a; if(q>1048576u)q=1048576u; sha256_update(&c,k+a,(int)q); a+=q; if(g_us_on)us_pump(); else g_ml_beat++; } if(s<2)sha256_update(&c,z,32); }
    u8 d[32]; sha256_final(&c,d); for(int j=0;j<32;j++) if(d[j]!=k[off+88+j])return -4;
    return 0;
}

static int up_hexv(char c){ if(c>='0'&&c<='9')return c-'0'; if(c>='a'&&c<='f')return c-'a'+10; if(c>='A'&&c<='F')return c-'A'+10; return -1; }
void upd_server(char* out,int cap);
static void up_manifest_url(char* out,int cap){
    int k=k_find_file("/etc/update-url"); if(k>=0){ char* d=fsd(k); int n=FS[k].len, o=0; while(o<n&&o<cap-1&&d[o]>' '){ out[o]=d[o]; o++; } out[o]=0; if(o>8)return; }
    up_ncpy(out,UP_MANIFEST,cap); }
static void up_notes(const u8* j,int n){
    g_upd.nnotes=0; int i=0; const char* key="\"notes\""; int kl=up_len(key);
    for(;i+kl<n;i++){ int m=1; for(int q=0;q<kl;q++) if(j[i+q]!=(u8)key[q]){ m=0; break; } if(m)break; }
    if(i+kl>=n)return; i+=kl; while(i<n&&j[i]!='[')i++; i++;
    while(i<n&&j[i]!=']'&&g_upd.nnotes<UPD_NOTES){ while(i<n&&j[i]!='"'&&j[i]!=']')i++; if(i>=n||j[i]==']')break; i++;
        char* o=g_upd.notes[g_upd.nnotes]; int k=0;
        while(i<n&&j[i]!='"'){ char c=(char)j[i]; if(c=='\\'&&i+1<n){ i++; c=(char)j[i]; if(c=='u'){ i+=4; c='-'; } else if(c=='n')c=' '; } if(k<118&&c>=' '&&c<127)o[k++]=c; i++; }
        o[k]=0; i++; if(k)g_upd.nnotes++; }
}

void upd_server(char* out,int cap){ up_manifest_url(out,cap); }
int upd_check(void){
    if(g_upd.busy)return -1; g_upd.busy=1; g_upd.want_check=0;
    up_ncpy(g_upd.cur,(const char*)g_buildsig.version,16);
    char url[320]; up_manifest_url(url,sizeof url);
    long n=0; const char* err=0; u8* d=net_get_url(url,&n,65536,&err);
    for(int t=0;t<2&&!d&&err&&(err[0]=='N'||err[0]=='C'||err[0]=='T');t++){ extern int dhcp_renew(void); klog("update: network not ready, asking for an address again"); if(dhcp_renew()>=0)d=net_get_url(url,&n,65536,&err); }
    g_upd.checked_at=g_ticks?g_ticks:1;
    if(!d){ g_upd.checked=-1; g_upd.busy=0; up_ncpy(g_upd.err,err,sizeof g_upd.err); up_set_why("Could not reach the update server: ",err); klog2("update: check failed: ",err); return -1; }
    char ver[24], isou[320], sha[80]; ver[0]=0; isou[0]=0; sha[0]=0;
    int ni=n>65535?65535:(int)n;
    json_str(d,ni,"version",ver,sizeof ver); json_str(d,ni,"iso_url",isou,sizeof isou); json_str(d,ni,"iso_sha256",sha,sizeof sha);
    int bytes=nvx_json_int(d,ni,"iso_bytes",0); char pub[24]; pub[0]=0; json_str(d,ni,"published",pub,sizeof pub);
    up_notes(d,ni); kfree(d);
    int shok=up_len(sha)==64; for(int i=0;i<64&&shok;i++) if(up_hexv(sha[i])<0)shok=0;
    if(!ver[0]||up_len(isou)<12||!shok||bytes<=0){ g_upd.checked=-1; g_upd.busy=0; up_ncpy(g_upd.err,"bad answer",sizeof g_upd.err); up_set_why("The update server sent an answer this version does not understand.",0); klog("update: check failed: bad manifest"); return -1; }
    up_ncpy(g_upd.latest,ver,16); up_ncpy(g_upd.iso_url,isou,sizeof g_upd.iso_url); up_ncpy(g_upd.sha,sha,sizeof g_upd.sha); up_ncpy(g_upd.published,pub,sizeof g_upd.published); g_upd.iso_bytes=bytes;
    g_upd.avail=upd_vnum(ver)>upd_vnum(g_upd.cur); g_upd.checked=1; g_upd.err[0]=0;
    up_set_why(g_upd.avail?"A new version is available.":"NoovexOS is up to date.",0);
    { char t[96],*p=t; p=up_cpy(p,"update: server has "); p=up_cpy(p,ver); p=up_cpy(p,g_upd.avail?" (newer)":" (not newer)"); klog(t); }
    g_upd.busy=0; return g_upd.avail;
}

static int up_cfg_prev(void){
    char b[8200]; long n=vf_read(UP_CFG,b,8192); if(n<=0)return -1; b[n]=0;
    if(up_has(b,n,"kernel=/boot/kernel64.old"))return 0;
    const char* add="\nentry=NoovexOS 64 (previous version)\nkernel=/boot/kernel64.old\ncmdline=noovexos\n";
    int al=up_len(add); if(n+al>8190)return -1; for(int i=0;i<al;i++)b[n+i]=add[i];
    return vf_write(UP_CFG,b,n+al)==n+al?0:-1;
}
static int up_put_verify(const char* path,const u8* data,long len,u8* vbuf){
    extern int g_vf_err; extern u32 g_vf_errlba; g_vf_err=0; int r=0; long i=0;
    if(vf_write(path,(const char*)data,len)!=len)r=-1;
    else if(vf_read(path,(char*)vbuf,len)!=len)r=-2;
    else { for(;i<len;i++) if(vbuf[i]!=data[i]){ r=-3; break; } }
    if(r){ char t[120],*p=t; p=up_cpy(p,"update: writing "); p=up_cpy(p,path); p=up_cpy(p," failed, step "); p=up_num(p,(unsigned)-r); p=up_cpy(p,", code "); p=up_num(p,(unsigned)g_vf_err); p=up_cpy(p,", sector "); p=up_num(p,g_vf_errlba); if(r==-3){ p=up_cpy(p,", byte "); p=up_num(p,(unsigned long long)i); } klog(t); }
    return r;
}
static u32 up_hdr_crc(const char* path){ u8 h[32]; if(vf_read(path,(char*)h,32)!=32)return 0; if(!up_rd32(h+20))return 0; return up_rd32(h+16); }
typedef struct { char n[16][40]; int c; } up_srl;
static void up_srl_cb(const char* name,int isdir,unsigned long long size,void* ctx){ (void)size; up_srl* l=(up_srl*)ctx; if(isdir||l->c>=16)return; int i=0; for(;name[i]&&i<39;i++)l->n[l->c][i]=name[i]; l->n[l->c][i]=0; l->c++; }
static int up_hexname(const char* nm,u32* crc){ u32 v=0; for(int i=0;i<8;i++){ int h=up_hexv(nm[i]); if(h<0)return 0; v=v*16+(u32)h; } if(nm[8]!='.')return 0; *crc=v; return 1; }
static int up_sr_prune(u32 k1,u32 k2,u32 k3){
    up_srl l; l.c=0; if(vf_ls("/sysroot",up_srl_cb,&l))return 0; int n=0; u32 k4=up_hdr_crc(UP_KROOT);
    for(int i=0;i<l.c;i++){ u32 c=0; int hx=up_hexname(l.n[i],&c); if(hx&&c&&(c==k1||c==k2||c==k3||c==k4))continue;
        char p[64],*q=p; q=up_cpy(q,"/sysroot/"); up_cpy(q,l.n[i]); if(vf_delete(p)==0){ n++; klog2("update: removed old Linux files ",p); } }
    return n; }
static int up_sr_put(const u8* sb,long slen,u8* vb){
    u32 crc=sysroot_crc(sb,(u32)slen); char nm[32]; sysroot_file_name(crc,nm);
    if(vf_size(nm)==slen){ u8* t=(u8*)kmalloc((unsigned long)slen); if(t){ int same=vf_read(nm,(char*)t,slen)==slen&&sysroot_crc(t,(u32)slen)==crc; kfree(t); if(same)return 0; } }
    if(!vf_exists("/sysroot")&&vf_mkdir("/sysroot")){ klog("update: could not make the /sysroot folder"); return -1; }
    if(vf_size(nm)>=0)vf_delete(nm);
    int r=up_put_verify(nm,sb,slen,vb); if(r)vf_delete(nm); else klog2("update: wrote Linux files ",nm); return r; }
int upd_put_sysroot_part(unsigned long long base,unsigned long long n){
    u32 sl=0; const u8* sb=sysroot_base_blob(&sl); if(!sb||!sl)return -1;
    int su=g_upd.usb; g_upd.usb=0;
    if(up_mount(up_rd,up_wr,base,n,0)){ g_upd.usb=su; return -1; }
    u8* vb=(u8*)kmalloc(sl); int r=vb?up_sr_put(sb,(long)sl,vb):-1; if(vb)kfree(vb);
    up_close(); up_flush(); g_upd.usb=su; return r; }
static int up_sr_load_disk(void){
    if(sysroot_base_ok()||!sysroot_want_size())return 1;
    char nm[32]; sysroot_file_name(sysroot_want_crc(),nm); long s=vf_size(nm);
    if(s!=(long)sysroot_want_size()){ klog2("update: Linux files missing on the boot partition: ",nm); return 0; }
    u8* b=(u8*)kmalloc((unsigned long)s); if(!b)return 0;
    if(vf_read(nm,(char*)b,s)!=s||!sysroot_set_base(b,(u32)s,1)){ kfree(b); klog2("update: Linux files damaged on the boot partition: ",nm); return 0; }
    g_sr_src=2; sysroot_mkdirs(); klog2("update: Linux files loaded from the boot partition: ",nm); return 1; }
static void up_phase(int lo,int hi,unsigned long long sectors){ g_us_phase=3; g_us_wlo=lo; g_us_whi=hi; g_us_wdone=0; g_us_wtotal=sectors?sectors:1; }

static int up_apply(const u8* nk,long nlen,const u8* sb,long slen,const char* nver){
    if(g_upd.usb){ upd_screen(70,"Making room on the USB stick"); int gr=up_grow(); if(gr<0){ up_set_why(gr==-2?"The free space on the USB stick is not empty in its file table, so it was not changed.":"The USB stick could not be prepared for the update. Nothing was changed.",0); return -1; } }
    if(up_open(0)){ up_set_why("The boot partition could not be opened for writing.",0); return -1; }
    int st=up_state_load();
    if(st==UPS_TRY0||st==UPS_TRY1||st==UPS_TRY2){ up_close(); up_set_why("The last update is still being checked. Wait one minute after the start, then try again.",0); return -1; }
    { int fr=vf_reclaim(); if(fr>0){ char t[64],*p=t; p=up_cpy(p,"update: freed "); p=up_num(p,(unsigned)fr); up_cpy(p," lost clusters on the boot partition"); klog(t); } }
    int good_old=(st==UPS_FAIL||st==UPS_OLD||st==UPS_SWAP);
    long sroot=vf_size(UP_KROOT), sbin=vf_size(UP_KBIN), sold=vf_size(UP_KOLD);
    const char* src=good_old?UP_KOLD:(sbin>0?UP_KBIN:UP_KROOT);
    long scur=vf_size(src);
    if(scur<=0){ up_close(); up_set_why("The installed version could not be found on the boot partition.",0); return -1; }
    long freeb=(long)vf_free_bytes_kb()*1024L;
    long r0=sroot>0?sroot:0, b0=sbin>0?sbin:0, o0=sold>0?sold:0;
    int ok;
    if(!good_old){ long f1=freeb+o0+(sbin>0?r0:0); ok=f1>=scur+UP_MARGIN&&(f1-scur+(sbin>0?b0:r0))>=nlen+UP_MARGIN; }
    else ok=freeb+r0+b0>=nlen+UP_MARGIN;
    if(!ok){ up_close(); if(g_upd.usb){ up_set_why("The USB stick was made from an older NoovexOS ISO and has no room to keep the previous version. Write NoovexOS 10.02 or newer to the stick once - after that it updates itself.",0); return -1; } char t[40],*p=t; p=up_num(p,(unsigned long long)(freeb/1048576)); up_cpy(p," MB free)."); up_set_why("The boot partition is too small to keep the previous version next to the new one (",t); return -1; }
    if(sb&&slen>0){ u32 nc=up_rd32(nk+16); up_sr_prune(sysroot_want_crc(),nc,good_old?up_hdr_crc(UP_KOLD):0);
        char nm[32]; sysroot_file_name(nc,nm); long have=vf_size(nm)==slen?slen:0; long fb=(long)vf_free_bytes_kb()*1024L;
        if(!good_old&&sold>0&&fb+have<slen+nlen+UP_MARGIN){ vf_delete(UP_KOLD); sold=-1; }
        u8* sv=(u8*)kmalloc((unsigned long)slen); if(!sv){ up_close(); up_set_why("Not enough memory to install the update.",0); return -1; }
        upd_screen(71,"Installing the Linux files of the new version");
        up_phase(71,72,(unsigned long long)slen/512ULL*2ULL);
        int sr=up_sr_put(sb,slen,sv); kfree(sv); g_us_phase=0;
        if(sr){ up_close(); up_set_why("The Linux files of the new version could not be written to the boot partition. Nothing else was changed.",0); return -1; }
        freeb=(long)vf_free_bytes_kb()*1024L;
        if(!good_old){ long f1=freeb+(sold>0?sold:0)+(sbin>0?r0:0); ok=f1>=scur+UP_MARGIN&&(f1-scur+(sbin>0?b0:r0))>=nlen+UP_MARGIN; }
        else ok=freeb+r0+b0>=nlen+UP_MARGIN;
        if(!ok){ up_close(); up_set_why("The boot partition has no room for the new version and its Linux files.",0); return -1; } }
    long vlen=scur>nlen?scur:nlen;
    u8* cur=(u8*)kmalloc((unsigned long)scur); u8* vb=(u8*)kmalloc((unsigned long)vlen);
    if(!cur||!vb){ if(cur)kfree(cur); if(vb)kfree(vb); up_close(); up_set_why("Not enough memory to install the update.",0); return -1; }
    upd_screen(72,"Checking the installed version");
    char over[16]; over[0]=0; int kr=-1;
    if(vf_read(src,(char*)cur,scur)==scur)kr=upd_kcheck(cur,(u32)scur,over);
    if(kr<0){ kfree(cur); kfree(vb); up_close(); up_set_why("The installed version on the disk is damaged, so there would be no way back. Install the new version from the ISO instead.",0); return -1; }
    up_ncpy(g_upd.newv,nver,16); up_ncpy(g_upd.oldv,over,16);
    int fail=0;
    if(!good_old){
        if(up_state_save(UPS_PREP)){ fail=1; goto out; }
        if(sbin>0&&sroot>0)vf_delete(UP_KROOT);
        if(sold>0)vf_delete(UP_KOLD);
        upd_screen(74,"Keeping the current version as the previous version");
        up_phase(74,84,(unsigned long long)scur/512ULL*2ULL);
        if(up_put_verify(UP_KOLD,cur,scur,vb)){ vf_delete(UP_KOLD); up_state_save(UPS_OK); fail=2; goto out; }
        if(up_state_save(UPS_SWAP)){ fail=3; goto out; }
        if(vf_size(UP_KROOT)>0)vf_delete(UP_KROOT);
        vf_delete(UP_KBIN);
    } else {
        if(up_state_save(UPS_SWAP)){ fail=3; goto out; }
        if(vf_size(UP_KROOT)>0)vf_delete(UP_KROOT);
        if(vf_size(UP_KBIN)>=0)vf_delete(UP_KBIN);
    }
    g_upd.has_old=1;
    upd_screen(85,"Installing the new version");
    up_phase(85,97,(unsigned long long)nlen/512ULL*2ULL);
    if(up_put_verify(UP_KBIN,nk,nlen,vb)){
        vf_delete(UP_KBIN);
        if(!good_old&&up_put_verify(UP_KBIN,cur,scur,vb)==0){ up_state_save(UPS_OK); fail=4; }
        else { up_state_save(UPS_OLD); fail=5; }
        goto out; }
    g_us_phase=0;
    upd_screen(98,"Adding the previous version to the start menu");
    up_cfg_prev();
    if(up_state_save(UPS_TRY0)){ fail=6; goto out; }
out:
    kfree(cur); kfree(vb); up_close(); up_flush();
    if(fail){ static const char* M[7]={"","The update state could not be saved. Nothing was changed.","The previous version could not be saved, so the update was stopped. Nothing was changed.","The update state could not be saved after keeping the previous version. NoovexOS will start the previous version.","The new version could not be written. The current version was put back.","The new version could not be written. NoovexOS will start the previous version.","The final update state could not be saved. NoovexOS will start the previous version."};
        up_set_why(M[fail],0); { char t[48],*p=t; p=up_cpy(p,"update: install failed, step "); up_num(p,(unsigned)fail); klog(t); } return -1; }
    return 0;
}

static int up_fail_screen(void){ upd_screen(-1,g_upd.why); us_title("Update not installed"); upd_screen(-1,g_upd.why); us_hold(400); upd_screen_end(); toast(g_upd.why); g_upd.busy=0; return -1; }

static u8* up_download(long* outn){
    upd_screen(1,"Connecting to the update server");
    { extern void nvx3_sync_all(void); nvx3_sync_all(); }
    void (*sv)(void)=g_wait_pump; g_wait_pump=us_pump; g_us_phase=1; g_us_dl_total=g_upd.iso_bytes; g_net_rx=0;
    long n=0; const char* err=0; u8* iso=net_get_url(g_upd.iso_url,&n,g_upd.iso_bytes,&err);
    for(int t=1;t<3&&!iso&&err&&(err[0]=='N'||err[0]=='C'||err[0]=='c'||err[0]=='T');t++){
        char m[80],*q=m; q=up_cpy(q,"The download failed, trying again ("); q=up_num(q,(unsigned)(t+1)); up_cpy(q," of 3)"); klog(m); upd_screen(1,m);
        u64 w0=g_ticks; while(g_ticks-w0<300){ if(g_wait_pump)g_wait_pump(); cpu_idle(); }
        if(err[0]=='N'){ extern int dhcp_renew(void); dhcp_renew(); }
        g_net_rx=0; n=0; err=0; iso=net_get_url(g_upd.iso_url,&n,g_upd.iso_bytes,&err); }
    g_wait_pump=sv; g_us_phase=0;
    if(!iso){ up_set_why("The download failed: ",err); return 0; }
    if(n!=g_upd.iso_bytes){ kfree(iso); up_set_why("The download was incomplete. Check the network and try again.",0); return 0; }
    upd_screen(56,"Checking the download");
    { sha256 c; sha256_init(&c); long a=0; while(a<n){ long q=n-a; if(q>1048576)q=1048576; sha256_update(&c,iso+a,(int)q); a+=q; upd_screen(56+(int)(a*8/n),0); }
      u8 d[32]; sha256_final(&c,d); int ok=1; for(int i=0;i<32;i++){ int v=up_hexv(g_upd.sha[i*2])*16+up_hexv(g_upd.sha[i*2+1]); if(v!=d[i])ok=0; }
      if(!ok){ kfree(iso); up_set_why("The download is damaged (its checksum does not match). Nothing was changed.",0); return 0; } }
    *outn=n; return iso; }

static u8* up_mem_file(const char* path,long* len,long lim){
    long ks=vf_size(path); if(ks<=0||ks>lim)return 0; u8* d=(u8*)kmalloc((unsigned long)ks); if(!d)return 0;
    if(vf_read(path,(char*)d,ks)!=ks){ kfree(d); return 0; } *len=ks; return d; }

static int up_iso_files(u8* iso,long n,u8** kb,long* klen,u8** sb,long* slen){
    *kb=0; *sb=0; *klen=0; *slen=0;
    if(n>2048L*512L+4096L&&iso[510]==0x55&&iso[511]==0xAA){ u32 pst=up_rd32(iso+446+8), pn=up_rd32(iso+446+12);
        if(pst&&(unsigned long long)pst*512ULL<(unsigned long long)n){ g_up_mem=iso; g_up_memlen=n; if(!pn||(unsigned long long)(pst+pn)*512ULL>(unsigned long long)n)pn=(u32)(n/512-pst);
            if(up_mount(up_memrd,0,pst,pn,1)==0){
                *kb=up_mem_file(UP_KBIN,klen,n);
                if(*kb&&*klen>64){ u32 sz=up_rd32(*kb+20), crc=up_rd32(*kb+16);
                    if(sz){ char nm[32]; sysroot_file_name(crc,nm); *sb=up_mem_file(nm,slen,n);
                        if(*sb&&(*slen!=(long)sz||sysroot_crc(*sb,(u32)*slen)!=crc)){ kfree(*sb); *sb=0; *slen=0; }
                        if(!*sb){ kfree(*kb); *kb=0; *klen=0; up_close(); return -2; } } }
                up_close(); } } }
    return *kb?0:-1; }

int upd_install(int force){
    if(g_upd.busy)return -1;
    if(!up_find()){ toast(g_upd.why); return -1; }
    if(force||g_upd.checked!=1||!g_upd.avail){ if(upd_check()<0){ toast(g_upd.why); return -1; } if(!g_upd.avail&&!force){ toast("NoovexOS is up to date."); return 0; } }
    g_upd.busy=1;
    upd_screen_begin("Working on updates");
    long n=0; u8* iso=up_download(&n);
    if(!iso)return up_fail_screen();
    upd_screen(65,"Unpacking the new version");
    u8* kb=0; u8* sb=0; long klen=0, slen=0;
    int fr=up_iso_files(iso,n,&kb,&klen,&sb,&slen);
    kfree(iso);
    if(fr){ up_set_why(fr==-2?"The Linux files of the new version are missing or damaged inside the download.":"The new version could not be found inside the download.",0); return up_fail_screen(); }
    upd_screen(68,"Checking the new version");
    char nver[16]; nver[0]=0; int kr=upd_kcheck(kb,(u32)klen,nver);
    if(kr!=0){ kfree(kb); if(sb)kfree(sb); up_set_why(kr==1?"The new version has no build checksum, so it was not installed.":"The new version failed its build check, so it was not installed.",0); return up_fail_screen(); }
    if(!force&&upd_vnum(nver)<=upd_vnum(g_upd.cur)){ kfree(kb); if(sb)kfree(sb); up_set_why("The download is not newer than this version.",0); return up_fail_screen(); }
    g_upd.new_bytes=klen;
    int r=up_apply(kb,klen,sb,slen,nver); kfree(kb); if(sb)kfree(sb);
    if(r)return up_fail_screen();
    { char t[64],*p=t; p=up_cpy(p,"update: installed "); p=up_cpy(p,nver); p=up_cpy(p,", restarting"); klog(t); }
    us_title("Restarting"); upd_screen(100,"NoovexOS will check the new version when it starts");
    nvx3_sync_all(); up_flush(); us_hold(200);
    power_reboot();
    upd_screen_end(); g_upd.busy=0; return 0;
}

int upd_repair(void){
    if(g_upd.busy)return -1;
    if(sysroot_base_ok()){ up_set_why("The Linux files are already loaded.",0); return 0; }
    if(!sysroot_want_size()){ up_set_why("This version has no separate Linux files.",0); return 0; }
    if(!up_find())return -1;
    if(upd_check()<0)return -1;
    if(g_upd.avail)return upd_install(0);
    g_upd.busy=1; g_upd.sr_tried=1;
    upd_screen_begin("Finishing the update");
    long n=0; u8* iso=up_download(&n);
    if(!iso)return up_fail_screen();
    upd_screen(65,"Unpacking the Linux files");
    u8* kb=0; u8* sb=0; long klen=0, slen=0;
    int fr=up_iso_files(iso,n,&kb,&klen,&sb,&slen); kfree(iso); if(kb)kfree(kb);
    if(fr||!sb){ if(sb)kfree(sb); up_set_why("The Linux files could not be found inside the download.",0); return up_fail_screen(); }
    if(slen!=(long)sysroot_want_size()||sysroot_crc(sb,(u32)slen)!=sysroot_want_crc()){ kfree(sb); up_set_why("The server has a different build of this version. Install the newest version from the ISO.",0); return up_fail_screen(); }
    upd_screen(80,"Saving the Linux files on the boot partition");
    int w=-1; if(up_open(0)==0){ u8* vb=(u8*)kmalloc((unsigned long)slen); if(vb){ up_phase(80,96,(unsigned long long)slen/512ULL*2ULL); w=up_sr_put(sb,slen,vb); g_us_phase=0; kfree(vb); } up_close(); up_flush(); }
    if(!sysroot_set_base(sb,(u32)slen,1)){ kfree(sb); up_set_why("The Linux files could not be loaded.",0); return up_fail_screen(); }
    g_sr_src=3; sysroot_mkdirs(); g_upd.sr_missing=0;
    up_set_why(w==0?"The Linux files are installed.":"The Linux files are loaded for now, but could not be saved on the boot partition.",0);
    upd_screen(100,g_upd.why); us_hold(150); upd_screen_end(); toast(g_upd.why); klog2("update: ",g_upd.why);
    g_upd.busy=0; return w==0?0:-1; }

static int up_switch(int to_state,const char* title,const char* need){
    if(g_upd.busy)return -1;
    if(!up_find()){ toast(g_upd.why); return -1; }
    g_upd.busy=1;
    if(up_open(0)){ g_upd.busy=0; toast("The boot partition could not be opened."); return -1; }
    up_state_load();
    long sz=vf_size(need); u8* b=sz>4096?(u8*)kmalloc((unsigned long)sz):0; int ok=0;
    if(b){ upd_screen_begin(title); upd_screen(20,"Checking the version on the disk"); char v[16]; if(vf_read(need,(char*)b,sz)==sz&&upd_kcheck(b,(u32)sz,v)>=0)ok=1; kfree(b); }
    else upd_screen_begin(title);
    if(!ok){ up_close(); up_set_why("That version is missing or damaged on the disk.",0); return up_fail_screen(); }
    if(up_state_save(to_state)){ up_close(); up_set_why("The choice could not be saved.",0); return up_fail_screen(); }
    if(to_state==UPS_OLD)up_cfg_prev();
    up_close(); up_flush();
    us_title("Restarting"); upd_screen(100,to_state==UPS_OLD?"Starting the previous version":"Starting the new version"); nvx3_sync_all(); us_hold(150);
    power_reboot(); upd_screen_end(); g_upd.busy=0; return 0;
}
int upd_go_back(void){ return up_switch(UPS_OLD,"Going back to the previous version",UP_KOLD); }
int upd_use_new(void){ return up_switch(UPS_TRY0,"Switching to the new version",UP_KBIN); }

static void up_heal_sr(void){
    u32 sl=0; const u8* sb=sysroot_base_blob(&sl);
    if(sb&&sl){ char nm[32]; sysroot_file_name(sysroot_want_crc(),nm); if(vf_size(nm)!=(long)sl){ u8* t=(u8*)kmalloc(sl); if(t){ up_sr_put(sb,(long)sl,t); kfree(t); } } }
    up_sr_prune(sysroot_want_crc(),up_hdr_crc(UP_KBIN),up_hdr_crc(UP_KOLD)); }
static void up_heal(void){
    up_heal_sr();
    const u8* efi=_binary_inst_efi_bin_start; long el=(long)(_binary_inst_efi_bin_end-_binary_inst_efi_bin_start);
    static const char* EP[2]={"/EFI/BOOT/BOOTX64.EFI","/EFI/NoovexOS/BOOTX64.EFI"};
    if(el>1024) for(int i=0;i<2;i++){ long s=vf_size(EP[i]); if(s<=0)continue; int same=(s==el);
        if(same){ u8* t=(u8*)kmalloc((unsigned long)s); if(!t)continue; if(vf_read(EP[i],(char*)t,s)!=s)same=0; else for(long k=0;k<s;k++) if(t[k]!=efi[k]){ same=0; break; } kfree(t); }
        if(!same){ u8* t=(u8*)kmalloc((unsigned long)el); if(t){ if(up_put_verify(EP[i],efi,el,t)==0)klog2("update: refreshed the UEFI loader ",EP[i]); kfree(t); } } }
    const u8* s2=_binary_inst_stage2_bin_start; u32 sl=(u32)(_binary_inst_stage2_bin_end-_binary_inst_stage2_bin_start), ns=(sl+511)/512;
    if(ns<2||ns>62||(!g_upd.usb&&dk_ratio()!=1))return;
    u8 m[512]; if(up_rd(0,1,m)||m[510]!=0x55||m[511]!=0xAA)return;
    for(int i=0;i<4;i++){ const u8* pe=m+446+i*16; if(pe[4]==0xEE)return; u32 st=up_rd32(pe+8); if(pe[4]&&st&&st<=ns+1)return; }
    u8 b[512]; if(up_rd(1,1,b))return; for(int i=0;i<64;i++) if(b[i]!=s2[i])return;
    int diff=0; for(u32 s=0;s<ns&&!diff;s++){ if(up_rd(1+s,1,b))return; for(int i=0;i<512;i++){ u32 o=s*512+i; if(b[i]!=(o<sl?s2[o]:0)){ diff=1; break; } } }
    if(!diff)return;
    for(u32 s=0;s<ns;s++){ for(int i=0;i<512;i++){ u32 o=s*512+i; b[i]=o<sl?s2[o]:0; } if(up_wr(1+s,1,b)){ klog("update: writing the BIOS boot loader failed"); return; } }
    up_flush(); klog("update: refreshed the BIOS boot loader");
}

static void up_confirm(void){
    if(up_open(0))return; int st=up_state_load();
    if((st==UPS_TRY0||st==UPS_TRY1||st==UPS_TRY2)&&up_eq(g_upd.newv,g_upd.cur)){
        if(up_state_save(UPS_OK)==0){ up_heal(); char t[96],*p=t; p=up_cpy(p,"NoovexOS was updated to version "); p=up_cpy(p,g_upd.cur); p=up_cpy(p,". The previous version is kept."); up_set_why(t,0); toast(t); klog("update: new version confirmed"); }
    }
    up_close(); up_flush();
}

int upd_refresh(void){
    up_ncpy(g_upd.cur,(const char*)g_buildsig.version,16);
    if(!up_find())return -1; if(up_open(1))return -1; up_state_load(); g_upd.old_bytes=vf_size(UP_KOLD); up_close(); return g_upd.state; }

static int up_boot(void){
    up_ncpy(g_upd.cur,(const char*)g_buildsig.version,16);
    if(!up_find())return 0;
    if(up_open(0))return 0;
    if(!up_sr_load_disk())g_upd.sr_missing=1;
    int st=up_state_load(); g_upd.old_bytes=vf_size(UP_KOLD);
    { char t[80],*p=t; p=up_cpy(p,"update: state "); p=up_cpy(p,upd_state_name(st)); klog(t); }
    int mine=g_upd.newv[0]&&up_eq(g_upd.newv,g_upd.cur);
    if((st==UPS_TRY0||st==UPS_TRY1||st==UPS_TRY2)&&mine){ g_upd.trial=1; g_upd.trial_t0=g_ticks; up_set_why("Checking the new version. It will be kept after one minute.",0); }
    else if(st==UPS_FAIL&&!mine){ char t[150],*p=t; p=up_cpy(p,"NoovexOS "); p=up_cpy(p,g_upd.newv); p=up_cpy(p," did not start twice, so NoovexOS went back to "); p=up_cpy(p,g_upd.cur); p=up_cpy(p,"."); up_set_why(t,0); up_ncpy(g_upd.ntext,t,sizeof g_upd.ntext); g_upd.notice=1; toast(t); }
    if(st==UPS_PREP||st==UPS_SWAP)vf_reclaim();
    if(st==UPS_PREP){ long s=vf_size(UP_KOLD); if(s>0){ u8* b=(u8*)kmalloc((unsigned long)s); int bad=1; if(b){ if(vf_read(UP_KOLD,(char*)b,s)==s&&upd_kcheck(b,(u32)s,0)>=0)bad=0; kfree(b); } if(bad)vf_delete(UP_KOLD); }
        up_state_save(UPS_OK); up_set_why("The last update was stopped before it changed anything.",0); up_ncpy(g_upd.ntext,g_upd.why,sizeof g_upd.ntext); g_upd.notice=1; toast(g_upd.why); }
    else if(st==UPS_SWAP){ up_state_save(UPS_OLD); up_set_why("The last update was interrupted. NoovexOS started the previous version. You can try the update again.",0); up_ncpy(g_upd.ntext,g_upd.why,sizeof g_upd.ntext); g_upd.notice=1; toast(g_upd.why); }
    else if(st==UPS_OLD&&!mine){ up_set_why("You are using the previous version.",0); }
    g_upd.has_old=vf_exists(UP_KOLD); g_upd.has_new=vf_size(UP_KBIN)>4096;
    up_close(); return 1;
}

int upd_tick(void){
    static int ph=0, tries=0; static u64 t0=0, tn=0;
    if(!t0){ t0=g_ticks?g_ticks:1; return 0; }
    if(ph==0){ if(g_ticks-t0<300||g_upd.busy)return 0; ph=1; tries++; if(!up_boot()&&!up_installed()&&tries<24){ ph=2; tn=g_ticks; } return 1; }
    if(ph==2){ if(g_ticks-tn<500||g_upd.busy)return 0; ph=0; t0=g_ticks-300; return 0; }
    if(g_upd.trial&&!g_upd.busy&&g_ticks-g_upd.trial_t0>=6000){ g_upd.trial=0; up_confirm(); return 1; }
    if(g_upd.want_check&&!g_upd.busy&&g_state==ST_DESKTOP){ upd_check(); return 1; }
    if(g_upd.sr_missing&&!g_upd.sr_tried&&!g_upd.busy&&!g_upd.trial&&g_state==ST_DESKTOP&&ph==1&&g_ticks-t0>=3000){ g_upd.sr_tried=1; klog("update: the Linux files are missing, downloading them"); upd_repair(); return 1; }
    return 0;
}

static void up_line(const char* a,const char* b){ term_puts(a); if(b)term_puts(b); term_puts("\n"); }
void upd_cmd(const char* a){
    while(a&&*a==' ')a++;
    if(!a||!*a||up_eq(a,"status")){
        upd_refresh();
        up_line("  this version:   ",g_upd.cur);
        up_line("  boot partition: ",g_upd.pbase?(g_upd.usb?"found on the USB stick":"found"):"not found");
        if(g_upd.pbase){ up_line("  update state:   ",upd_state_name(g_upd.state)); if(g_upd.newv[0])up_line("  new version:    ",g_upd.newv); if(g_upd.oldv[0])up_line("  previous:       ",g_upd.oldv); up_line("  previous kept:  ",g_upd.has_old?"yes":"no"); }
        up_line("  Linux files:    ",!sysroot_want_size()?"none in this build":g_sr_src==1?"loaded by the boot loader":g_sr_src==2?"loaded from the boot partition":g_sr_src==3?"downloaded":"missing (run update repair)");
        if(g_upd.checked==1)up_line("  server has:     ",g_upd.latest);
        if(g_upd.why[0])up_line("  ",g_upd.why);
        term_puts("  update check | update install | update repair | update back | update new | update url <url>\n"); return; }
    if(up_eq(a,"check")){ int r=upd_check(); if(r<0)up_line("  ",g_upd.why); else { up_line("  server has: ",g_upd.latest); up_line("  ",g_upd.why); for(int i=0;i<g_upd.nnotes;i++)up_line("   - ",g_upd.notes[i]); } return; }
    if(up_eq(a,"install")){ if(upd_install(0)<0)up_line("  ",g_upd.why); return; }
    if(up_eq(a,"repair")){ upd_repair(); up_line("  ",g_upd.why); return; }
    if(up_eq(a,"reinstall")){ if(upd_install(1)<0)up_line("  ",g_upd.why); return; }
    if(up_eq(a,"back")){ if(upd_go_back()<0)up_line("  ",g_upd.why); return; }
    if(up_eq(a,"new")){ if(upd_use_new()<0)up_line("  ",g_upd.why); return; }
    if(a[0]=='u'&&a[1]=='r'&&a[2]=='l'){ const char* u=a+3; while(*u==' ')u++;
        if(!*u){ char t[320]; up_manifest_url(t,sizeof t); up_line("  update server: ",t); return; }
        if(up_eq(u,"default")){ nvx_mark_file("/etc/update-url","default"); g_upd.checked=0; up_line("  update server: ",UP_MANIFEST); return; }
        nvx_mark_file("/etc/update-url",u); up_line("  update server: ",u); g_upd.checked=0; return; }
    term_puts("  update [status|check|install|reinstall|repair|back|new|url <url>|url default]\n");
}

