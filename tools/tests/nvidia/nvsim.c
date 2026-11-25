#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <time.h>
#include <unistd.h>
#include <sys/mman.h>
#include <sys/wait.h>

static unsigned char g_bootinfo[64];
#define NV_BOOTINFO ((unsigned long long)(unsigned long)g_bootinfo)
#include "drivers/video/nvidia/nvmem.c"
#include "drivers/video/nvidia/nvflcn.c"
#include "drivers/video/dp_link.c"
#include "drivers/video/nvidia/nvgen.c"
#include "drivers/video/nvidia/nvgsp.c"
#include "drivers/video/fb/damage.c"
#include "include/nvx/drivers/video/fb/gfx.h"
int g_gfx=0, g_gfx_present=0; char g_gfx_why[64]; void (*g_gfx_on_stop)(void)=0; static int sim_gfx_starts=0, sim_gfx_sid=0; static u32* sim_gfx_px=0;
const char* gk_name(int id){ static const char* N[12]={"fill","blend","mask","copy","copym","gradh","gradv","tint","blurh","blurv","over","overm"}; return id>=0&&id<12?N[id]:"op"; }
void gfx_stop(const char* why){ if(g_gfx==0)return; if(g_gfx_on_stop)g_gfx_on_stop(); gq_abort(); g_gfx=0; g_gq_mode=GQM_OFF; int i=0; while(why[i]&&i<63){ g_gfx_why[i]=why[i]; i++; } g_gfx_why[i]=0; }
int g_gfx_atlas=0; gfx_alloc_fn g_gfx_alloc_gpu=0; void gfx_forget(void){} u64 g_last_input_t=0; int g_gfx_nomem=0;
int gfx_start(int mode,gfx_alloc_fn al,u32* sp,int ss){ (void)sp; (void)ss; sim_gfx_starts++; u32* px=0; int s=al(300,200,304,&px); if(s<=0)return -1; sim_gfx_sid=s; sim_gfx_px=px; g_gq_mode=GQM_GPU; g_gfx=mode; return 0; }
#include "drivers/video/fb/gq.c"
#include "drivers/video/nvidia/nv2dq.c"
#include "drivers/video/nvidia/nvdraw.c"
#include "drivers/video/nvidia/nvsettings.c"
#include "drivers/video/nvidia/nvcursor.c"
int ufs_n=0; struct nvx_ufs_t ufs[32]; static char sim_uflag=0; int ufat_read(int fi,char* o,int m){ (void)fi; if(m<1)return 0; o[0]=sim_uflag; return sim_uflag?1:0; } volatile u64 g_ticks=0; u32 g_gfx_frames=0, g_gfx_frame_us=0;

int g_nv_bus=1, g_nv_slot=0, g_nv_fn=0; u16 g_nv_dev=0x21C4; u64 g_nv_bar0=0xF6000000ULL, g_nv_bar1=0, g_nv_bar3=0; u32 g_nv_bar0_sz=0x1000000; u32 g_nv_boot0=0x168000A1u;
int g_nv_arch=0, g_nv_impl=0, g_nv_rev=0; u8 g_nv_vbios[65536]; int g_nv_vbios_len=0; u32 g_nv_bit_off=0, g_nv_dcb_off=0; int g_nv_ncon=0; u8 g_nv_con_type[16], g_nv_con_or[16], g_nv_con_heads[16];
FsFile FS[FS_N];
const u8 _binary_nvfw_gsp_xz_start[1], _binary_nvfw_gsp_xz_end[1], _binary_nvfw_bootloader_xz_start[1], _binary_nvfw_bootloader_xz_end[1], _binary_nvfw_booter_load_xz_start[1], _binary_nvfw_booter_load_xz_end[1];
const u8 _binary_nvfw_ga102_gsp_xz_start[1], _binary_nvfw_ga102_gsp_xz_end[1], _binary_nvfw_ga102_bootloader_xz_start[1], _binary_nvfw_ga102_bootloader_xz_end[1], _binary_nvfw_ga102_booter_load_xz_start[1], _binary_nvfw_ga102_booter_load_xz_end[1], _binary_nvfw_ga102_booter_unload_xz_start[1], _binary_nvfw_ga102_booter_unload_xz_end[1];
const u8 _binary_nvfw_booter_unload_xz_start[1], _binary_nvfw_booter_unload_xz_end[1], _binary_nvfw_acr_bl_xz_start[1], _binary_nvfw_acr_bl_xz_end[1];

static int sim_hpd_n, sim_hpd_last=-1; int display_hpd(int p){ sim_hpd_n++; sim_hpd_last=p; return 1; } static int sim_s1_n, sim_s1_last=-1; static u32 sim_w=1920,sim_h=1080,sim_w2=1920,sim_h2=1080; int display_hpd_s1(int p){ sim_s1_n++; sim_s1_last=p; return 1; } void display_sizes(u32* w,u32* h,u32* w2,u32* h2){ *w=sim_w; *h=sim_h; *w2=sim_w2; *h2=sim_h2; }
static int fails=0; static void chk(const char* w,int c){ printf("  %-78s %s\n",w,c?"ok":"FAIL"); if(!c)fails++; }

static unsigned char* sim_low=0; static unsigned long sim_low_n=0;
void* kmalloc(unsigned long n){ if(n==0x2000){ if(!sim_low){ sim_low=mmap((void*)0x50000000ULL,1UL<<24,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANONYMOUS|MAP_FIXED_NOREPLACE,-1,0); if(sim_low==MAP_FAILED)sim_low=0; } if(sim_low&&sim_low_n+n<=(1UL<<24)){ void* q=sim_low+sim_low_n; sim_low_n+=n; return q; } } void* p=0; if(posix_memalign(&p,64,n?n:1))return 0; return p; }
void kfree(void* p){ (void)p; }
char* scopy(char* p,const char* s){ while(*s)*p++=*s++; return p; }
char* udec(char* p,u32 v){ char t[12]; int n=0; do{ t[n++]=(char)('0'+v%10); v/=10; }while(v); while(n)*p++=t[--n]; return p; }
char* hx(char* p,u32 v,int dg){ static const char H[]="0123456789ABCDEF"; for(int i=dg-1;i>=0;i--)*p++=H[(v>>(i*4))&15]; return p; }
int seq(const char* a,const char* b){ while(*a&&*a==*b){ a++; b++; } return *a==*b; }
static int g_quiet=1; void klog(const char* s){ if(!g_quiet)printf("    | %s\n",s); }
u64 hr_ns(void){ struct timespec t; clock_gettime(CLOCK_MONOTONIC,&t); return (u64)t.tv_sec*1000000000ULL+(u64)t.tv_nsec; }
static void (*sim_delay_hook)(u32)=0; void hr_delay_us(u32 us){ if(sim_delay_hook)sim_delay_hook(us); }
void nv_udelay(u32 us){ hr_delay_us(us); }
long xzk_decode_into(const u8* in,long n,u8* out,long cap){ (void)in; (void)n; (void)out; (void)cap; return -1; }
u64 kva_to_pa(u64 va){ return va; }
int fs_find_name(const char* nm){ (void)nm; return -1; }
char* fsd(int k){ (void)k; return 0; }
static int g_saved=0; int fs_save_bin(const char* n,const u8* d,int len,int f){ (void)n; (void)d; (void)f; g_saved=len; return 0; }
void nvx3_sync_all(void){}
int msd_ok=0, ufat_ok=0; int msd_autoinit_safe(void){ return 0; } static int g_usbw=0; int ufat_write_file(const char* n,const char* d,int len){ (void)n; (void)d; g_usbw=len; return -1; }
int nv_probe(void){ return 1; }
static int sim_drv_skip=0; int drv_begin(const char* id){ (void)id; return !sim_drv_skip; } void drv_end(void){}
static u32 sim_boot0=0x168000A1u; static u32 SC_CHAN=0xC46F, SC_CE=0xC5B5, SC_3D=0xC597, SC_CMP=0xC5C0;
void nv_read_id(void){ g_nv_boot0=sim_boot0; }

static u64 BAR1=0xE0000000ULL, BAR3=0xF0000000ULL;
static int sizing[8];
u32 pci_cfg(int b,int s,int f,int off){ (void)b; (void)s; (void)f;
    int i=(off-0x10)/4; if(off==0x04)return 0x00100007u;
    if(off>=0x10&&off<0x28&&sizing[i]){ if(off==0x14)return ~(0x10000000u-1u)|0xCu; if(off==0x1C)return ~(0x2000000u-1u)|0xCu; if(off==0x18||off==0x20)return 0xFFFFFFFFu; return 0; }
    if(off==0x14)return (u32)BAR1|0xCu; if(off==0x18)return (u32)(BAR1>>32); if(off==0x1C)return (u32)BAR3|0xCu; if(off==0x20)return (u32)(BAR3>>32);
    return 0; }
void pci_cfgw(int b,int s,int f,int off,u32 v){ (void)b; (void)s; (void)f; if(off>=0x10&&off<0x28)sizing[(off-0x10)/4]=(v==0xFFFFFFFFu); }

static u32* REG; static u8* VR; static u64 VRSZ=6ULL<<30;
static u8* ROM; static u32 ROMSZ=0x40000;
static struct { u32 imem[0x10000/4]; u32 tags[256]; u8 sec[256]; u32 dmem[0x20000/4]; u32 ic, dc; } FAL[2];
static int fi(u32 f){ return f==NVF_SEC2?1:0; }
static int sim_mode=0, gsp_on=0, gsp_was_reset=0, resumed=0, resume_wait=0;
static u8* sim_shm; static u32 gsp_cmd_rp=0, gsp_msg_wp=0, gsp_seq=0; static int gsp_init=0;

static u64 rd64(const u8* p){ u64 v; memcpy(&v,p,8); return v; }
static u64 walk(u64 pd3,u64 va,int* sys){
    u64 t=pd3; u64 e;
    e=rd64(VR+t+((va>>47)&3)*8); if(((e>>1)&3)!=1)return ~0ULL; t=(e&~0xFFULL)<<4;
    e=rd64(VR+t+((va>>38)&511)*8); if(((e>>1)&3)!=1)return ~0ULL; t=(e&~0xFFULL)<<4;
    e=rd64(VR+t+((va>>29)&511)*8); if(((e>>1)&3)!=1)return ~0ULL; t=(e&~0xFFULL)<<4;
    e=rd64(VR+t+((va>>21)&255)*16+8); if(((e>>1)&3)!=1)return ~0ULL; t=(e&~0xFFULL)<<4;
    e=rd64(VR+t+((va>>12)&511)*8); if(!(e&1))return ~0ULL; *sys=((e>>1)&3)==2; return ((e&0x00FFFFFFFFFFFF00ULL)<<4)+(va&0xFFF); }
static u8* vptr(u64 pd3,u64 va){ int s=0; u64 p=walk(pd3,va,&s); if(p==~0ULL)return 0; return s?(u8*)p:VR+p; }
#include "tools/tests/nvidia/nvvram_sim.c"
static u64 SIM_BAR1PD, SIM_BAR2PD;
static int sim_bars=0; static int sim_drop_next=0; static int disp_alloc=0, disp_inst_ok=0, disp_inst_n=0;
static void sim_edid(u8* e){ memset(e,0,128); static const u8 h[8]={0,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0}; memcpy(e,h,8); e[8]=0x10; e[9]=0xAC; e[10]=0x34; e[11]=0x12; e[18]=1; e[19]=3;
    u8* d=e+54; d[0]=0x02; d[1]=0x3A; d[2]=0x80; d[3]=0x18; d[4]=0x71; d[5]=0x38; d[6]=0x2D; d[7]=0x40; d[8]=0x58; d[9]=0x2C; d[10]=0x45; d[17]=0x1E;
    u8* n=e+72; n[3]=0xFC; memcpy(n+5,"SIM MONITOR\n ",13);
    u8 s=0; for(int i=0;i<127;i++)s=(u8)(s+e[i]); e[127]=(u8)(0x100-s); }
static int sim_fuzz=0; static unsigned sim_fz=99;
static int sens_n=0, sens_bad=0, sens_refuse=0, sens_fuzz=0; static unsigned sens_fz=7;
static int sim_two=0, sim_hdmi2=0, sim_no_ntfy=0, sim_refuse_cls=0, sim_proto2=2; static u32 SD_DISP=0xC570u, SD_CORE=0xC57Du, SD_WNDW=0xC57Eu;
static int sim_vb, sim_ev_alloc, sim_ev_sub, sim_dp_ovr, sim_dp_ovr_n; static u32 sim_dp_wm, sim_dp_hbs, sim_dp_vbs;
static int sim_proto1=1, sim_dp_dead[2], sim_dp_lanes[2]={4,4}, sim_dp_ctrl[2], sim_dp_wr[2], sim_dp_stream, sim_dp_bad; static u8 sim_dpcd[2][0x800];
static void sim_dp_sink(int k,int trained){ memset(sim_dpcd[k],0,0x800); sim_dpcd[k][0]=0x12; sim_dpcd[k][1]=0x14; sim_dpcd[k][2]=0xC4; sim_dp_dead[k]=0; sim_dp_lanes[k]=4; sim_dp_ctrl[k]=0; sim_dp_wr[k]=0; if(trained){ sim_dpcd[k][0x100]=0x14; sim_dpcd[k][0x101]=0x84; sim_dpcd[k][0x202]=0x77; sim_dpcd[k][0x203]=0x77; sim_dpcd[k][0x204]=1; } }
static int sim_edid2(u8* e){ memset(e,0,256); static const u8 h[8]={0,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0}; memcpy(e,h,8); e[8]=0x4C; e[9]=0x2D; e[10]=0x77; e[11]=0x01; e[18]=1; e[19]=3;
    u8* d=e+54; d[0]=0x30; d[1]=0x2A; d[2]=0x00; d[3]=0x98; d[4]=0x51; d[5]=0x00; d[6]=0x2A; d[7]=0x40; d[8]=0x30; d[9]=0x70; d[10]=0x13; d[17]=0x1E;
    u8* n=e+72; n[3]=0xFC; memcpy(n+5,"SIM SECOND\n  ",13);
    if(sim_hdmi2)e[126]=1;
    u8 s=0; for(int i=0;i<127;i++)s=(u8)(s+e[i]); e[127]=(u8)(0x100-s);
    if(!sim_hdmi2)return 128;
    u8* c=e+128; c[0]=0x02; c[1]=0x03; c[2]=10; c[3]=0; c[4]=0x65; c[5]=0x03; c[6]=0x0C; c[7]=0x00; c[8]=0x10; c[9]=0x00;
    s=0; for(int i=0;i<127;i++)s=(u8)(s+c[i]); c[127]=(u8)(0x100-s); return 256; }
static u32 KC_ASY[0x1000], KC_ARM[0x1000], KW_ASY[8][0x200], KW_ARM[8][0x200]; static int kw_pend[8];
static u64 sim_pb[9], sim_inst=0; static int sim_pb_ok[9]; static u32 sim_get[9];
static int disp_sink_caps=0, disp_brightc=0; static int disp_root=0, disp_core=0, disp_win[8], disp_bad=0, disp_updates=0, disp_jumps=0, disp_ntfy=0, disp_sor_asg=0, disp_hdmi_en=0, disp_chan_alloc=0;
static int kw_vbl[8], sim_no_vbl=0, sim_vbls=0, sim_wntfys=0, sim_wtear=0; static u32 sim_vbl_us=0;
static u64 sim_ramht(int chid,u32 handle,u32* fl,u64* limit);
static void sim_wntfy(int w){ u32 hd=KW_ASY[w][0x21C/4]; if(!hd)return; u32 fl=0; u64 st=sim_ramht(1+w,hd,&fl,0); if(st==~0ULL||(fl&0x47u)!=0x45u){ disp_bad++; return; } *(u32*)(VR+st+(KW_ASY[w][0x220/4]&0xFF0u))=0x80000000u; sim_wntfys++; if(sim_vb)for(int h=0;h<4;h++)REG[0x611800/4+h]|=2u; }
static void sim_vblank(void){ sim_vbls++; for(int w=0;w<8;w++) if(kw_vbl[w]){ memcpy(KW_ARM[w],KW_ASY[w],sizeof KW_ARM[w]); kw_vbl[w]=0; sim_wntfy(w); } }
static void sim_vbl_tick(u32 us){ if(sim_no_vbl)return; sim_vbl_us+=us?us:1; if(sim_vbl_us>=16667u){ sim_vbl_us=0; sim_vblank(); } }
static u64 sim_ramht(int chid,u32 handle,u32* fl,u64* limit){
    if(!sim_inst)return ~0ULL; u32 h=0, x=handle; while(x){ h^=x&1023u; x>>=10; } h^=(u32)chid<<6; h&=1023u;
    for(u32 i=0;i<1024;i++){ u32 co=(h+i)&1023u; u32 hd=*(u32*)(VR+sim_inst+co*8), cx=*(u32*)(VR+sim_inst+co*8+4);
        if(!hd&&!cx)return ~0ULL;
        if(hd==handle&&(int)(cx>>25)==chid){ if((cx&0x3FFFu)!=2u)return ~0ULL; u64 obj=sim_inst+((cx>>9)&0xFFFFu); if(obj<sim_inst+0x2000)return ~0ULL;
            *fl=*(u32*)(VR+obj); if(limit)*limit=(((u64)*(u32*)(VR+obj+12))|((u64)*(u32*)(VR+obj+16)<<32))<<8; return (((u64)*(u32*)(VR+obj+4))|((u64)*(u32*)(VR+obj+8)<<32))<<8; } }
    return ~0ULL; }
static void kd_mthd(int ch,u32 m,u32 v){
    if(ch==0){ if(m>=0x4000){ disp_bad++; return; } KC_ASY[m/4]=v;
        if(m==0x200){ disp_updates++; u32 wm=KC_ASY[0x21C/4]; if(wm) for(int w=0;w<8;w++) if(KC_ASY[(0x1000+w*0x80)/4]!=KC_ARM[(0x1000+w*0x80)/4])disp_bad++;
            for(int hh=0;hh<4;hh++){ u32 own=KC_ASY[(0x2020+hh*0x400)/4]; if(own&&((KC_ASY[(0x1010+hh*2*0x80)/4]&0x7FFFu)!=0x7FFFu||(KC_ASY[(0x1000+hh*2*0x80)/4]&0xFu)!=(u32)hh))disp_bad++; }
            for(int w=0;w<8;w++){ if(!(wm&(1u<<w)))continue; if(!kw_pend[w]){ disp_bad++; continue; } memcpy(KW_ARM[w],KW_ASY[w],sizeof KW_ARM[w]); kw_pend[w]=0; }
            memcpy(KC_ARM,KC_ASY,sizeof KC_ARM);
            if((KC_ASY[0x20C/4]&0x1000u)&&!sim_no_ntfy){ u32 fl=0; u64 st=sim_ramht(0,KC_ASY[0x208/4],&fl,0); if(st==~0ULL||(fl&0x47u)!=0x45u)disp_bad++; else { *(u32*)(VR+st+((KC_ASY[0x20C/4]>>4)&0xFFu)*16)=0x80000000u; disp_ntfy++; } } }
        return; }
    int w=ch-1; if(m>=0x800){ disp_bad++; return; } KW_ASY[w][m/4]=v;
    if(m==0x200){ if(KW_ASY[w][0x370/4]&1u){ if(!(KW_ASY[w][0x374/4]&(1u<<w)))disp_bad++; kw_pend[w]=1; } else if(!(KW_ASY[w][0x308/4]&0x70u)&&KW_ASY[w][0x21C/4]){ if(kw_vbl[w])sim_wtear++; kw_vbl[w]=1; } else { memcpy(KW_ARM[w],KW_ASY[w],sizeof KW_ARM[w]); sim_wntfy(w); } } }
static void kd_exec(int ch){
    u32 user=ch?0x690000u+(u32)(ch-1)*0x1000u:0x680000u; u32 put=REG[user/4]/4, get=sim_get[ch];
    if(!sim_pb_ok[ch]||!(ch?disp_win[ch-1]:disp_core)||put>=1024){ disp_bad++; return; }
    u32* pb=(u32*)sim_pb[ch]; int guard=0;
    while(get!=put&&guard++<6000){ u32 hd=pb[get];
        if((hd&0xE0000000u)==0x20000000u){ get=(hd&0x1FFFFFFFu)/4; disp_jumps++; if(get>=1024){ disp_bad++; break; } continue; }
        u32 n=(hd>>18)&0x3FFu, m=hd&0xFFFCu; get++; if(!n||get+n>1024||(hd&0xE0000003u)){ disp_bad++; break; }
        for(u32 k=0;k<n;k++)kd_mthd(ch,m+k*4,pb[get++]); }
    sim_get[ch]=get; REG[(user+4)/4]=get*4; }
static u32 sim_scan(int head,u32 x,u32 y,int* ok){
    int w=head*2; *ok=0; if((KC_ARM[(0x1000+w*0x80)/4]&0xFu)!=(u32)head)return 0;
    u32 fl=0; u64 lim=0; u64 st=sim_ramht(1+w,KW_ARM[w][0x240/4],&fl,&lim); if(st==~0ULL||(fl&0x47u)!=0x05u)return 0;
    u64 off=(u64)KW_ARM[w][0x260/4]<<8; u32 pitch=(KW_ARM[w][0x230/4]&0x1FFFu)<<6; u32 sz=KW_ARM[w][0x224/4];
    if(x>=(sz&0xFFFFu)||y>=(sz>>16)||!pitch||(SD_WNDW==0xC57Eu?(KW_ARM[w][0x228/4]&0x10u)==0:(KW_ARM[w][0x228/4]!=0||(fl&0x00100000u)!=0))||(KW_ARM[w][0x22C/4]&0xFFu)!=0xCFu)return 0;
    { u32 f2=0; u64 vb=sim_ramht(1+w,KW_ARM[w][0x444/4],&f2,0); u64 il=(u64)KW_ARM[w][0x448/4]<<8; if(vb!=0||(f2&0x47u)!=0x45u||!il||KW_ARM[w][0x440/4]!=((2u<<2)|(1029u<<8)))return 0;
      if(*(u16*)(VR+il+0x20)!=0||*(u16*)(VR+il+0x20+1023*8)!=0x3BFEu||*(u16*)(VR+il+0x20+512*8+2)!=0x3800u||*(u16*)(VR+il+0x20+1024*8+4)!=0x3BFEu)return 0; }
    u64 a=st+off+(u64)y*pitch+(u64)x*4; if(a+4>lim+1||a+4>VRSZ)return 0; *ok=1; return *(u32*)(VR+a); }
static u64 sim_b1_cpu=0;
u64 acpi_hi_map(u64 pa){ if(pa==BAR1&&sim_b1_cpu)return sim_b1_cpu; if(!sim_bars)return 0; int s=0;
    if(pa==BAR3){ u64 p=walk(g_bar2.pd3,0,&s); return p==~0ULL?0:(u64)(VR+p); }
    if(pa==BAR1){ u64 p=walk(SIM_BAR1PD,g_bva1,&s); return p==~0ULL?0:(u64)(VR+p)-g_bva1; }
    return 0; }

static void msg_post(u32 fn,const void* pl,u32 plen,u32 result){
    u8 m[0x10000]; u32 rlen=0x20+plen; u32 len=(0x30+rlen+0xFFF)&~0xFFFu; memset(m,0,len);
    *(u32*)(m+0x24)=gsp_seq++; *(u32*)(m+0x28)=len/0x1000; *(u32*)(m+0x30)=0x03000000u; *(u32*)(m+0x34)=0x43505256u; *(u32*)(m+0x38)=rlen; *(u32*)(m+0x3C)=fn; *(u32*)(m+0x40)=result;
    memcpy(m+0x50,pl,plen); u64 cs=0; for(u32 i=0;i<len;i+=8)cs^=rd64(m+i); *(u32*)(m+0x20)=(u32)(cs>>32)^(u32)cs;
    for(u32 o=0;o<len;o+=0x1000){ memcpy(sim_shm+MSGQ+0x1000+gsp_msg_wp*0x1000,m+o,0x1000); gsp_msg_wp=(gsp_msg_wp+1)%QCNT; }
    *(u32*)(sim_shm+MSGQ+0x10)=gsp_msg_wp; }

static struct { u32 chid, et, runl, sched, promoted, cls[4]; int ncls; u64 inst, userd, gpva; u32 hc; } CH[32]; static int NCH=0; static int sim_chan_freed=0;
static u64 vas_pd3[4]; static int bar_pde_ok=0, reserved_pdes_ok=0, alloc_bad=0, ctrl_bad=0, sysinfo_ok=0, reg_ok=0, cks_bad=0, seq_bad=0, ce_done=0;
static u32 cmd_expect_seq=0;
static int chan_find(u32 h){ for(int i=0;i<NCH;i++) if((0xF1F00000u|CH[i].chid)==h)return i; return -1; }

static void gsp_handle(u32 fn,u8* p,u32 n){
    if(fn==72){ sysinfo_ok=(n==664&&rd64(p)==0xF6000000ULL&&rd64(p+8)==BAR1&&rd64(p+16)==BAR3&&*(u32*)(p+0x40)==0x88000u); return; }
    if(fn==73){ reg_ok=(n==117&&*(u32*)p==117&&*(u32*)(p+4)==3&&*(u32*)(p+8)==56&&*(u32*)(p+0x18)==76&&*(u32*)(p+0x28)==98&&!strcmp((char*)p+98,"RMDevidCheckIgnore")); return; }
    if(fn==65){ u8 s[2168]; memset(s,0,sizeof s); *(u32*)(s+0x124)=7; for(int g=0;g<3;g++)*(u32*)(s+0x128+g*8+4)=0x7; *(u32*)(s+0x348)=2;
        u8* e=s+0x348+8; *(u64*)e=0; *(u64*)(e+8)=g_nvl.nonwpr_addr-1; e+=48; *(u64*)e=g_nvl.nonwpr_addr; *(u64*)(e+8)=g_nvl.fb_size-1; *(u64*)(e+16)=1; e[30]=1;
        *(u64*)(s+0x828)=SIM_BAR1PD; *(u64*)(s+0x830)=SIM_BAR2PD; *(u32*)(s+0x868)=0xC1E00004u; *(u32*)(s+0x86C)=0xDE1D0001u; *(u32*)(s+0x870)=0x5D1D0002u;
        strcpy((char*)s+0x71C,"NVIDIA GeForce GTX 1660 SUPER (simulated)"); msg_post(65,s,sizeof s,0); return; }
    if(fn==70){ u64 v=rd64(p+8), sh=rd64(p+16); bar_pde_ok=(*(u32*)p==1&&((v>>1)&3)==1&&sh==47); *(u64*)(VR+SIM_BAR2PD)=v; msg_post(70,p,n,0); return; }
    if(fn==10){ int fc=chan_find(*(u32*)(p+8)); if(fc>=0&&CH[fc].hc==*(u32*)p){ CH[fc].chid=0xFFFFFFFFu; CH[fc].sched=0; sim_chan_freed++; }
        if(*(u32*)p==0xC1D00002u&&*(u32*)(p+8)==0xC1D00002u){ disp_root=0; disp_core=0; for(int i=0;i<8;i++)disp_win[i]=0; for(int i=0;i<9;i++)sim_pb_ok[i]=0; } msg_post(10,p,n,0); return; }
    if(fn==47){ msg_post(47,p,n,0); REG[0x110040/4]=0x80000000u; return; }
    if(fn==103){ u32 hc=*(u32*)p, hp=*(u32*)(p+4), ho=*(u32*)(p+8), cls=*(u32*)(p+12), ps=*(u32*)(p+20); u8* pr=p+0x20; u32 st=0;
        u32 want= cls==0x7E?24:cls==0?108:cls==0x80?56:cls==0x2080?4:cls==0x90F1?48:cls==SC_CHAN?360:cls==SC_CE?8:(cls==SD_CORE||cls==SD_WNDW)?32:0; if(cls==0x73){ disp_alloc++; if(!disp_inst_ok)alloc_bad++,st=0x1F; }
        if(cls==0x7E){ if(ps!=24||hp!=0x5D1D0000u||*(u32*)pr!=hc||*(u32*)(pr+8)!=0x7Eu||(*(u32*)(pr+12)&~0xFFu)!=0x04000000u)alloc_bad++,st=0x1F; else sim_ev_alloc++; }
        if(cls==SD_DISP){ if(hc!=0xC1D00002u||ho!=0xC5700000u||sim_refuse_cls==(int)SD_DISP)st=0x1F; else disp_root=1; }
        if(cls==SD_CORE){ disp_chan_alloc++; if(!disp_root||hp!=0xC5700000u||ho!=(SD_CORE<<16)||!sim_pb_ok[0]||*(u32*)pr!=0||*(u32*)(pr+12)!=0||sim_refuse_cls==(int)SD_CORE)st=0x1F; else { disp_core=1; sim_get[0]=0; REG[0x680000/4]=0; REG[0x680004/4]=0; memset(KC_ASY,0,sizeof KC_ASY); } }
        if(cls==SD_WNDW){ disp_chan_alloc++; u32 wi=*(u32*)pr; if(!disp_root||hp!=0xC5700000u||wi>7||ho!=((SD_WNDW<<16)|wi)||!sim_pb_ok[1+wi]||sim_refuse_cls==(int)SD_WNDW)st=0x1F; else { disp_win[wi]=1; sim_get[1+wi]=0; REG[(0x690000+wi*0x1000)/4]=0; REG[(0x690004+wi*0x1000)/4]=0; memset(KW_ASY[wi],0,sizeof KW_ASY[wi]); kw_pend[wi]=0; } }
        if(ps!=want){ alloc_bad++; st=0x1F; }
        if(cls==0&&(hc!=ho||hp!=ho||*(u32*)pr!=hc))alloc_bad++,st=0x1F;
        if(cls==SC_CHAN&&(sim_refuse_cls==0xC46F||NCH>=32))st=0x1F;
        else if(cls==SC_CHAN){ u32 chid=ho&0xFFFFu, fl=*(u32*)(pr+0x14); if(((fl>>8)&7)!=chid%8||((fl>>12)&0x1FF)!=chid/8||!(fl&(1u<<21)))alloc_bad++,st=0x1F;
            CH[NCH].chid=chid; CH[NCH].et=*(u32*)(pr+0x80); CH[NCH].inst=rd64(pr+0x90); CH[NCH].userd=rd64(pr+0xA8); CH[NCH].gpva=rd64(pr+8); CH[NCH].hc=hc; CH[NCH].runl=CH[NCH].et==11?8:0; CH[NCH].ncls=0; CH[NCH].sched=0; CH[NCH].promoted=0; NCH++; }
        if(cls==SC_3D||cls==SC_CMP||cls==0x902D){ int c=chan_find(hp); if(c<0||!CH[c].promoted||CH[c].et!=1)alloc_bad++,st=0x1F; else CH[c].cls[CH[c].ncls++]=cls; }
        if(cls==SC_CE){ int c=chan_find(hp); if(c<0||*(u32*)pr!=1||*(u32*)(pr+4)!=CH[c].et)alloc_bad++,st=0x1F; else CH[c].cls[CH[c].ncls++]=cls; }
        u8 r[0x1000]; memcpy(r,p,0x20+ps); *(u32*)(r+16)=st; msg_post(103,r,0x20+ps,0); return; }
    if(fn==76&&sim_drop_next==1){ sim_drop_next=2; return; }
    if(fn==76){ u32 hc=*(u32*)p, ho=*(u32*)(p+4), cmd=*(u32*)(p+8), ps=*(u32*)(p+16); u8* pr=p+0x18; u32 st=0; (void)hc;
        if(cmd==0x20801112u){ if(ps!=3212)ctrl_bad++; memset(pr,0,ps); *(u32*)(pr+4)=3; u8* e=pr+12; *(u32*)(e+8)=1; *(u32*)(e+12)=0; strcpy((char*)e+84,"GRAPHICS"); e+=100; *(u32*)(e+8)=9; *(u32*)(e+12)=0; strcpy((char*)e+84,"CE0"); e+=100; *(u32*)(e+8)=11; *(u32*)(e+12)=8; strcpy((char*)e+84,"CE2"); }
        else if(cmd==0x20802A08u){ *(u32*)pr=0x1000; }
        else if(cmd==0x20800A32u){ if(ps!=1600)ctrl_bad++; memset(pr,0,ps); u32 sz[][2]={{0x00,0x20000},{0x10,0x10000},{0x11,0x30000},{0x0D,0x80000},{0x13,0x2D000},{0x14,0x40000},{0x17,0x1000},{0x18,0x3000}}; for(int i=0;i<8;i++)*(u32*)(pr+sz[i][0]*8)=sz[i][1]; }
        else if(cmd==0x2080012Bu){ if(ps!=560)ctrl_bad++; u32 cnt=*(u32*)(pr+40); int c=chan_find(*(u32*)(pr+16)); int good=c>=0&&cnt>0&&*(u32*)pr==1;
            for(u32 k=0;k<cnt&&good;k++){ u8* e=pr+48+k*32; if(e[30]&&(rd64(e)>=VRSZ||!rd64(e+16)||*(u32*)(e+24)!=4))good=0; if(!e[31]&&!rd64(e+8))good=0; if(e[31]&&!e[30])good=0; }
            if(good)CH[c].promoted=1; else { ctrl_bad++; st=0x1F; } }
        else if(cmd==0x90F10106u){ if(ps!=184)ctrl_bad++; u64 pd3=rd64(pr+40), pd2=rd64(pr+64), pd1=rd64(pr+88);
            reserved_pdes_ok=(rd64(pr+16)==0x100000000ULL&&*(u32*)(pr+32)==3&&((rd64(VR+pd3)&~0xFFULL)<<4)==pd2&&((rd64(VR+pd2)&~0xFFULL)<<4)==pd1&&pr[60]==47&&pr[84]==38&&pr[108]==29);
            *(u64*)(VR+pd1+8*8)=(0x7F000000ULL>>4)|2; }
        else if(cmd==0x00801813u){ if(ps!=32||*(u32*)(pr+8)!=4)ctrl_bad++; vas_pd3[1]=rd64(pr); }
        else if(cmd==0xA06F0104u){ int c=chan_find(ho); if(c<0||*(u32*)pr!=CH[c].et)ctrl_bad++; }
        else if(cmd==0xA06F0103u){ int c=chan_find(ho); if(c<0||pr[0]!=1)ctrl_bad++; else CH[c].sched=1; }
        else if(cmd==0x20800A49u){ if(ps!=24||hc!=0xC1E00004u||*(u32*)(pr+16)!=2||rd64(pr+8)!=0x10000||(rd64(pr)&0xFFFF))ctrl_bad++; else { disp_inst_ok=1; disp_inst_n++; sim_inst=rd64(pr); } }
        else if(cmd==0x20800A01u){ if(ps!=28||hc!=0xC1E00004u)ctrl_bad++; *(u32*)(pr+4)=0xFF; }
        else if(cmd==0x731365u){ if(ps!=4)ctrl_bad++; }
        else if(cmd==0x20800A58u){ u32 hcl=*(u32*)(pr+28), ci=*(u32*)(pr+32); int k=hcl==SD_CORE?0:hcl==SD_WNDW&&ci<8?(int)(1+ci):-1;
            if(ps!=40||hc!=0xC1E00004u||k<0||*(u32*)pr!=1||rd64(pr+16)!=0xFFF||*(u32*)(pr+24)!=1||pr[36]!=1||(rd64(pr+8)&0xFFF)){ ctrl_bad++; st=0x1F; } else { sim_pb[k]=rd64(pr+8); sim_pb_ok[k]=1; } }
        else if(cmd==0x730126u){ if(ps!=16)ctrl_bad++; u32 hh=*(u32*)(pr+4); *(u32*)(pr+12)=hh==0?0x100u:0; }
        else if(cmd==0x731152u){ if(ps!=80)ctrl_bad++; u32 id=*(u32*)(pr+4); u32 ex=pr[8]; int o=-1; for(int k=0;k<4;k++) if(!(ex&(1u<<k))){ o=k; break; } if(o<0||(id!=0x100&&id!=0x400))st=0x1F; else { *(u32*)(pr+40+o*8)=id; *(u32*)(pr+24+o*4)=id; disp_sor_asg|=(1<<o)<<(id==0x400?4:0); } }
        else if(cmd==0x730273u){ if(ps!=12||*(u32*)(pr+4)!=0x400)ctrl_bad++; else disp_hdmi_en=pr[8]?1:2; }
        else if(cmd==0x730293u){ if(ps!=12||*(u32*)(pr+4)!=0x400||*(u32*)(pr+8)!=0||disp_hdmi_en!=1)ctrl_bad++; else disp_sink_caps++; }
        else if(cmd==0x20800AC6u){ if(ps!=4104||hc!=0xC1E00004u||*(u32*)pr!=0x56u)ctrl_bad++; else disp_brightc++; }
        else if(cmd==0x730102u){ *(u32*)(pr+8)=4; }
        else if(cmd==0x730287u){ *(u32*)(pr+4)=0xF; }
        else if(cmd==0x730120u){ if(ps!=12)ctrl_bad++; *(u32*)(pr+4)=0x700; }
        else if(cmd==0x730122u){ if(ps!=16)ctrl_bad++; *(u32*)(pr+8)&=sim_two?0x500:0x100; }
        else if(cmd==0x73028Bu){ if(ps!=56)ctrl_bad++; u32 id=*(u32*)(pr+4); *(u32*)(pr+8)=id==0x100?0:id==0x200?2:1; *(u32*)(pr+12)=2; *(u32*)(pr+16)=id==0x100?(u32)sim_proto1:id==0x200?8:(sim_two?(u32)sim_proto2:5); }
        else if(cmd==0x731341u){ u32 id=*(u32*)(pr+4); int k=id==0x100?0:id==0x400?1:-1; u32 a=*(u32*)(pr+16), n=*(u32*)(pr+36)+1;
            if(ps!=48||k<0||n>16||a+n>0x800||!(*(u32*)(pr+12)&8)){ sim_dp_bad++; st=0x1F; } else if(sim_dp_dead[k])st=0x1F; else if(*(u32*)(pr+12)&1)memcpy(pr+20,sim_dpcd[k]+a,n); else { memcpy(sim_dpcd[k]+a,pr+20,n); sim_dp_wr[k]++; } *(u32*)(pr+40)=0; }
        else if(cmd==0x731343u){ u32 id=*(u32*)(pr+4); int k=id==0x100?0:id==0x400?1:-1; u32 ln=*(u32*)(pr+12)&31, bw=(*(u32*)(pr+12)>>8)&0xFF;
            if(ps!=28||k<0||!(*(u32*)(pr+8)&3)){ sim_dp_bad++; st=0x1F; } else if(sim_dp_dead[k])st=0x1F; else { sim_dp_ctrl[k]++; int ok=(int)ln<=sim_dp_lanes[k]&&bw<=0x14; sim_dpcd[k][0x100]=(u8)bw; sim_dpcd[k][0x101]=(u8)(ln|0x80); sim_dpcd[k][0x202]=ok?(ln>1?0x77:0x07):0; sim_dpcd[k][0x203]=ok&&ln>2?0x77:0; sim_dpcd[k][0x204]=(u8)ok; *(u32*)(pr+16)=ok?0:1; } }
        else if(cmd==0x731346u){ if(ps!=44)sim_dp_bad++; }
        else if(cmd==0x731369u){ if(ps!=52)sim_dp_bad++; *(u32*)(pr+8)=3; }
        else if(cmd==0x731362u){ if(ps!=116||*(u32*)(pr+4)>3||*(u32*)(pr+8)>3||*(u32*)(pr+72)!=64||pr[17])sim_dp_bad++; else { sim_dp_stream++; sim_dp_ovr=pr[16]; if(pr[16]){ sim_dp_ovr_n++; sim_dp_wm=*(u32*)(pr+76); sim_dp_hbs=*(u32*)(pr+24); sim_dp_vbs=*(u32*)(pr+28); } } }
        else if(cmd==0x731140u){ *(u32*)(pr+8)=(1u<<8); }
        else if(cmd==0x730245u){ u32 id=*(u32*)(pr+4); if(ps!=2064||(id!=0x100&&!(sim_two&&id==0x400)))ctrl_bad++; else if(id==0x100){ sim_edid(pr+16); *(u32*)(pr+8)=128; } else *(u32*)(pr+8)=(u32)sim_edid2(pr+16); }
        else if(cmd==0xC36F0108u){ int c=chan_find(ho); *(u32*)pr=c>=0?((CH[c].runl<<16)|CH[c].chid):0; }
        else if(cmd==0x20801823u){ sens_n++; if(ps!=420||hc!=G.hclient||ho!=G.hsubdev||*(u32*)pr!=2||*(u32*)(pr+4)!=3||*(u32*)(pr+8)||*(u32*)(pr+12)!=7||*(u32*)(pr+16))sens_bad++; *(u32*)(pr+8)=3u|(16u<<4)|(2u<<12); *(u32*)(pr+16)=2u|(3u<<16)|(16u<<20); }
        else if(cmd==0x20800A81u){ sens_n++; if(ps!=4||hc!=G.hclient||ho!=G.hsubdev||*(u32*)pr)sens_bad++; if(sens_refuse)st=0x56; else *(u32*)pr=2; }
        else if(cmd==0x20801708u){ sens_n++; if(ps!=1||hc!=G.hclient||ho!=G.hsubdev||pr[0])sens_bad++; pr[0]=1; }
        else if(cmd==0x20800301u){ if(ps!=20||ho!=0x5D1D0000u||*(u32*)(pr+4)!=2||(*(u32*)pr!=1&&*(u32*)pr!=7))ctrl_bad++; else sim_ev_sub++; }
        else { ctrl_bad++; st=0x56; }
        if(sens_fuzz&&(cmd==0x20801823u||cmd==0x20800A81u||cmd==0x20801708u)){ for(u32 i=0;i<ps;i++){ sens_fz=sens_fz*1103515245u+12345u; pr[i]=(u8)(sens_fz>>16); } st=((sens_fz>>9)%11==0)?0x56u:0; }
        if(sim_fuzz&&((cmd>>16)==0x73||cmd==0x20800A01u)){ for(u32 i=0;i<ps;i++){ sim_fz=sim_fz*1103515245u+12345u; if((sim_fz>>28)<sim_fuzz)pr[i]=(u8)(sim_fz>>16); } st=((sim_fz>>8)%23==0)?0x1Fu:0; }
        u8 r[0x4000]; memcpy(r,p,0x18+ps); *(u32*)(r+12)=st; msg_post(76,r,0x18+ps,0); return; }
}
static void gsp_service(void){
    u32 w=*(u32*)(sim_shm+CMDQ+0x10);
    while(gsp_cmd_rp!=w){ u8* e=sim_shm+CMDQ+0x1000+gsp_cmd_rp*0x1000; u32 cnt=*(u32*)(e+0x28); u8 m[0x10000];
        for(u32 k=0;k<cnt;k++)memcpy(m+k*0x1000,sim_shm+CMDQ+0x1000+((gsp_cmd_rp+k)%QCNT)*0x1000,0x1000);
        u64 cs=0; for(u32 i=0;i<cnt*0x1000;i+=8)cs^=rd64(m+i); if(((u32)(cs>>32)^(u32)cs)!=0)cks_bad++;
        if(*(u32*)(m+0x24)!=cmd_expect_seq)seq_bad++; cmd_expect_seq++;
        if(*(u32*)(m+0x34)!=0x43505256u||*(u32*)(m+0x30)!=0x03000000u)cks_bad++;
        gsp_cmd_rp=(gsp_cmd_rp+cnt)%QCNT; *(u32*)(sim_shm+MSGQ+0x20)=gsp_cmd_rp;
        gsp_handle(*(u32*)(m+0x3C),m+0x50,*(u32*)(m+0x38)-0x20); } }
static void gsp_boot(void){
    *(u32*)(sim_shm+MSGQ+0x04)=QSZ; *(u32*)(sim_shm+MSGQ+0x08)=0x1000; *(u32*)(sim_shm+MSGQ+0x0C)=QCNT; gsp_msg_wp=0; gsp_cmd_rp=0;
    gsp_service();
    u32 sq[64]; memset(sq,0,sizeof sq); sq[1]=12; u32* c=sq+10; c[0]=0; c[1]=0x12340; c[2]=0xBEEF; c[3]=4; c[4]=0x12340; c[5]=2; c[6]=3; c[7]=5; c[8]=5; c[9]=6; c[10]=7; c[11]=8; msg_post(0x1002,sq,40+12*4,0);
    u8 el[268]; memset(el,0,sizeof el); *(u32*)el=79; strcpy((char*)el+12,"simulated GSP error log entry"); msg_post(0x1006,el,sizeof el,0);
    gsp_init=1; resume_wait=1; }
static u32 ce_lines=0, ce_fills=0;
static u32 R2D[16][0x400]; static u32 HOST[16][0x40]; static u32 tw_fills=0, tw_blits=0, host_sems=0, mem_ops=0;
static u32 sim_kind(u64 pd3,u64 va){
    u64 t=pd3; u64 e;
    e=rd64(VR+t+((va>>47)&3)*8); if(((e>>1)&3)!=1)return 0xFFFF; t=(e&~0xFFULL)<<4;
    e=rd64(VR+t+((va>>38)&511)*8); if(((e>>1)&3)!=1)return 0xFFFF; t=(e&~0xFFULL)<<4;
    e=rd64(VR+t+((va>>29)&511)*8); if(((e>>1)&3)!=1)return 0xFFFF; t=(e&~0xFFULL)<<4;
    e=rd64(VR+t+((va>>21)&255)*16+8); if(((e>>1)&3)!=1)return 0xFFFF; t=(e&~0xFFULL)<<4;
    e=rd64(VR+t+((va>>12)&511)*8); if(!(e&1))return 0xFFFF; return (u32)(e>>56); }
static int s2d_blbad=0;
static u8* s2d(u64 pd3,const u32* R,int dst,int x,int y){ u64 base=((u64)R[(dst?0x220:0x250)/4]<<32)|R[(dst?0x224:0x254)/4]; u32 pitch=R[(dst?0x214:0x244)/4]; u32 w=R[(dst?0x218:0x248)/4], h=R[(dst?0x21C:0x24C)/4];
    if(x<0||y<0||(u32)x>=w||(u32)y>=h)return 0;
    if(!dst&&R[0x234/4]==0){ if(R[0x238/4]!=0||R[0x23C/4]!=1||R[0x240/4]!=2||sim_kind(pd3,base)!=6){ s2d_blbad++; return 0; } return vptr(pd3,base+((u64)y*w+(u64)x)*4); }
    if(R[(dst?0x204:0x234)/4]!=1)return 0; return vptr(pd3,base+(u64)y*pitch+(u64)x*4); }
static u32 R3[16][0x3000/4]; static int s3_bad=0, s3_draws=0, s3_clears=0, s3_nodepth=0; static u32 s3_verts=0, s3_pix=0; static char s3_why[64];
static void s3_err(const char* w){ if(!s3_bad)strncpy(s3_why,w,63); s3_bad++; }
static float s3_f(u32 u){ float f; memcpy(&f,&u,4); return f; }
static u32 s3_field(const u32* in,int lo,int hi){ u64 v=((u64)in[lo/32])|((u64)in[lo/32+1<4?lo/32+1:3]<<32); if(lo/32==3)v=in[3]; return (u32)((v>>(lo%32))&((1ULL<<(hi-lo))-1)); }
static u32 s3_sph(const u32* h,int lo,int hi){ u32 v=0; for(int b=lo;b<hi;b++) if(h[b/32]&(1u<<(b%32)))v|=1u<<(b-lo); return v; }
static int s3_prog(u64 pd3,const u32* R,int idx,const u32** hdr,const u32** code){
    u32 base=N3_SET_PIPELINE_SHADER+(u32)idx*0x40u; if(R[base/4]!=(idx==1?0x11u:0x51u)){ s3_err("pipeline shader not enabled"); return 0; }
    u64 a=((u64)R[(base+0x14)/4]<<32)|R[(base+0x18)/4]; const u32* p=(const u32*)vptr(pd3,a); if(!p||(a&0x7F)){ s3_err("program address"); return 0; }
    if(s3_sph(p,0,5)!=(idx==1?1u:2u)||s3_sph(p,5,10)!=4||s3_sph(p,10,14)!=(u32)idx||s3_sph(p,17,21)!=1){ s3_err("SPH type/version"); return 0; }
    *hdr=p; *code=p+32; return 1; }
static int s3_vs(u64 pd3,const u32* R,u32 vi,float* pos,float* var){
    const u32* h; const u32* code; if(!s3_prog(pd3,R,1,&h,&code))return 0; float r[256]; memset(r,0,sizeof r); u32 maxr=0; int wrote_pos=0;
    for(int ip=0;ip<64;ip++){ const u32* in=code+ip*4; u32 op=in[0]&0xFFFu; if(((in[0]>>12)&7u)!=7u){ s3_err("VS predicate"); return 0; }
        if(op==0x94D){ if(s3_field(in,87,90)!=7){ s3_err("EXIT"); return 0; } break; }
        u32 addr=s3_field(in,40,50), comps=s3_field(in,74,76)+1;
        if(op==0x321){ u32 dst=(in[0]>>16)&255u; if(s3_field(in,32,40)!=255||((in[0]>>24)&255u)!=255||s3_field(in,79,80)){ s3_err("ALD operands"); return 0; }
            if(addr<0x80||addr>=0x280){ s3_err("ALD address"); return 0; } u32 at=(addr-0x80)/16, c0=((addr-0x80)/4)&3;
            for(u32 c=0;c<comps;c++){ if(!(s3_sph(h,192+(int)((addr-0x80)/4+c),193+(int)((addr-0x80)/4+c)))){ s3_err("VS input not in SPH imap"); return 0; } }
            u32 va=R[(N3_SET_VERTEX_ATTRIBUTE_A+at*4)/4]; if((va&0x1Fu)!=0||(va&(1u<<6))||((va>>21)&0x3Fu)!=1||((va>>27)&7u)!=7){ s3_err("vertex attribute format"); return 0; }
            u32 off=(va>>7)&0x3FFFu, fmt=R[N3_SET_VERTEX_STREAM_A_FORMAT/4]; u32 stride=fmt&0xFFFu; if(!(fmt&0x1000u)||!stride){ s3_err("vertex stream disabled"); return 0; }
            u64 sb=((u64)R[(N3_SET_VERTEX_STREAM_A_FORMAT+4)/4]<<32)|R[(N3_SET_VERTEX_STREAM_A_FORMAT+8)/4]; u64 sz=((u64)R[N3_SET_VERTEX_STREAM_SIZE_A/4]<<32)|R[(N3_SET_VERTEX_STREAM_SIZE_A+4)/4];
            u64 o=(u64)vi*stride+off; if(o+16>sz){ s3_err("vertex beyond stream size"); return 0; }
            for(u32 c=0;c<comps;c++){ u8* q=vptr(pd3,sb+o+(c0+c)*4); if(!q){ s3_err("vertex buffer unmapped"); return 0; } memcpy(&r[dst+c],q,4); if(dst+c>maxr)maxr=dst+c; } }
        else if(op==0x322){ u32 src=s3_field(in,32,40); if(s3_field(in,64,72)!=255||((in[0]>>24)&255u)!=255){ s3_err("AST operands"); return 0; }
            if(addr/4<s3_sph(h,140,148)||(addr+comps*4-1)/4>s3_sph(h,152,160)){ s3_err("AST outside store_req"); return 0; }
            for(u32 c=0;c<comps;c++){ u32 a=addr+c*4;
                if(a>=0x70&&a<0x80){ if(!s3_sph(h,400+(int)(a/4),401+(int)(a/4))){ s3_err("position not in omap"); return 0; } pos[(a-0x70)/4]=r[src+c]; wrote_pos|=1<<((a-0x70)/4); }
                else if(a>=0x80&&a<0x100){ if(!s3_sph(h,432+(int)((a-0x80)/4),433+(int)((a-0x80)/4))){ s3_err("output not in omap"); return 0; } var[(a-0x80)/4]=r[src+c]; }
                else { s3_err("AST address"); return 0; } } }
        else { s3_err("unknown VS instruction"); return 0; } }
    if(wrote_pos!=15){ s3_err("VS did not write the position"); return 0; }
    if(R[(N3_SET_PIPELINE_REGISTER_COUNT+0x40)/4]<maxr+1+2){ s3_err("VS register count"); return 0; } return 1; }
static int s3_fs(u64 pd3,const u32* R,const float* var,float* out){
    const u32* h; const u32* code; if(!s3_prog(pd3,R,5,&h,&code))return 0; float r[256]; memset(r,0,sizeof r); u32 maxr=0;
    if(!s3_sph(h,14,15)||s3_sph(h,576,608)!=0xF){ s3_err("FS output map"); return 0; }
    if(!s3_sph(h,191,192)){ s3_err("FS header without the position W input"); return 0; }
    for(int ip=0;ip<64;ip++){ const u32* in=code+ip*4; u32 op=in[0]&0xFFFu; if(op==0x94D)break;
        if(op!=0x326){ s3_err("unknown FS instruction"); return 0; } u32 dst=(in[0]>>16)&255u, addr=s3_field(in,64,72)<<2;
        if(s3_field(in,76,78)||s3_field(in,78,80)||s3_field(in,32,40)!=255||s3_field(in,81,84)!=7||addr<0x80||addr>=0x100){ s3_err("IPA operands"); return 0; }
        u32 ci=(addr-0x80)/4; if(s3_sph(h,192+(int)ci*2,194+(int)ci*2)!=2){ s3_err("FS input not perspective in SPH"); return 0; } r[dst]=var[ci]; if(dst>maxr)maxr=dst; }
    if(R[(N3_SET_PIPELINE_REGISTER_COUNT+5*0x40)/4]<maxr+1+2){ s3_err("FS register count"); return 0; }
    for(int c=0;c<4;c++)out[c]=r[c]; return 1; }
static int s3_targets(u64 pd3,const u32* R,u64* ct,u64* zt,u32* sw,u32* sh){
    *ct=((u64)R[N3_SET_COLOR_TARGET_A/4]<<32)|R[N3_SET_COLOR_TARGET_B/4]; *zt=((u64)R[N3_SET_ZT_A/4]<<32)|R[N3_SET_ZT_B/4]; *sw=R[N3_SET_COLOR_TARGET_WIDTH/4]; *sh=R[N3_SET_COLOR_TARGET_HEIGHT/4];
    if(R[N3_SET_COLOR_TARGET_FORMAT/4]!=0xCF||R[N3_SET_COLOR_TARGET_MEMORY/4]!=0||R[N3_SET_COLOR_TARGET_THIRD_DIMENSION/4]!=1||R[N3_SET_CT_SELECT/4]!=1){ s3_err("colour target state"); return 0; }
    if(R[N3_SET_ZT_FORMAT/4]!=0x0A||R[N3_SET_ZT_BLOCK_SIZE/4]!=0||R[N3_SET_ZT_SELECT/4]!=1||R[N3_SET_ZT_SIZE_A/4]!=*sw||R[N3_SET_ZT_SIZE_B/4]!=*sh){ s3_err("depth target state"); return 0; }
    if(R[0x19CC/4]||R[0x111C/4]||R[0x19E0/4]){ s3_err("compression enabled on plain memory"); return 0; }
    if((*sw&15)||(*sh&7)||sim_kind(pd3,*ct)!=6||sim_kind(pd3,*zt)!=6||sim_kind(pd3,*ct+(u64)*sw*4**sh-4)!=6){ s3_err("target memory kind or size"); return 0; } return 1; }
static void s3_clear(u64 pd3,const u32* R,u32 v){
    u64 ct, zt; u32 sw, sh; if(!s3_targets(pd3,R,&ct,&zt,&sw,&sh))return; if(v!=0x3D||R[N3_SET_CLEAR_SURFACE_CONTROL/4]!=0x10){ s3_err("clear control"); return; }
    u32 x0=R[N3_SET_CLEAR_RECT_HORIZONTAL/4]&0xFFFF, x1=R[N3_SET_CLEAR_RECT_HORIZONTAL/4]>>16, y0=R[N3_SET_CLEAR_RECT_VERTICAL/4]&0xFFFF, y1=R[N3_SET_CLEAR_RECT_VERTICAL/4]>>16; if(x1>sw||y1>sh){ s3_err("clear rect"); return; }
    u32 px=0; for(int c=0;c<4;c++){ float f=s3_f(R[(N3_SET_COLOR_CLEAR_VALUE+c*4)/4]); if(f<0)f=0; if(f>1)f=1; u32 b=(u32)(f*255.0f+0.5f); px|=b<<(c==0?16:c==1?8:c==2?0:24); }
    for(u32 y=y0;y<y1;y++) for(u32 x=x0;x<x1;x++){ u8* cp=vptr(pd3,ct+((u64)y*sw+x)*4); u8* zp=vptr(pd3,zt+((u64)y*sw+x)*4); if(!cp||!zp){ s3_err("target unmapped"); return; } memcpy(cp,&px,4); memcpy(zp,&R[N3_SET_Z_CLEAR_VALUE/4],4); }
    s3_clears++; }
static void s3_draw(u64 pd3,const u32* R,u32 count){
    u64 ct, zt; u32 sw, sh; if(!s3_targets(pd3,R,&ct,&zt,&sw,&sh))return; s3_draws++;
    if(R[N3_SET_PRIMITIVE_TOPOLOGY/4]!=4||R[N3_SET_DRAW_CONTROL_A/4]!=0x200||R[(N3_SET_DRAW_CONTROL_A+4)/4]!=1||R[N3_DRAW_VERTEX_ARRAY_BEGIN_END_A/4]!=0||count%3){ s3_err("draw control"); return; }
    float sx=s3_f(R[N3_SET_VIEWPORT_SCALE_X/4]), sy=s3_f(R[(N3_SET_VIEWPORT_SCALE_X+4)/4]), sz=s3_f(R[(N3_SET_VIEWPORT_SCALE_X+8)/4]), ox=s3_f(R[N3_SET_VIEWPORT_OFFSET_X/4]), oy=s3_f(R[(N3_SET_VIEWPORT_OFFSET_X+4)/4]), oz=s3_f(R[(N3_SET_VIEWPORT_OFFSET_X+8)/4]);
    u32 cw=R[N3_SET_SURFACE_CLIP_HORIZONTAL/4]>>16, chh=R[N3_SET_SURFACE_CLIP_VERTICAL/4]>>16, vw=R[N3_SET_VIEWPORT_CLIP_HORIZONTAL/4]>>16, vh=R[(N3_SET_VIEWPORT_CLIP_HORIZONTAL+4)/4]>>16; if(vw<cw)cw=vw; if(vh<chh)chh=vh; if(cw>sw||chh>sh){ s3_err("clip larger than target"); return; }
    for(u32 t=0;t+2<count;t+=3){ float p[3][4], v[3][8], X[3], Y[3], Z[3], iw[3]; int skip=0;
        for(int k=0;k<3;k++){ memset(v[k],0,sizeof v[k]); if(!s3_vs(pd3,R,t+k,p[k],v[k]))return; if(p[k][3]<=0||p[k][2]<0||p[k][2]>p[k][3])skip=1; iw[k]=1.0f/p[k][3]; X[k]=p[k][0]*iw[k]*sx+ox; Y[k]=p[k][1]*iw[k]*sy+oy; Z[k]=p[k][2]*iw[k]*sz+oz; }
        s3_verts+=3; if(skip)continue;
        float area=(X[1]-X[0])*(Y[2]-Y[0])-(Y[1]-Y[0])*(X[2]-X[0]); if(area==0)continue; float inv=1.0f/area;
        float fx0=X[0],fx1=X[0],fy0=Y[0],fy1=Y[0]; for(int k=1;k<3;k++){ if(X[k]<fx0)fx0=X[k]; if(X[k]>fx1)fx1=X[k]; if(Y[k]<fy0)fy0=Y[k]; if(Y[k]>fy1)fy1=Y[k]; }
        int x0=(int)fx0, x1=(int)fx1+1, y0=(int)fy0, y1=(int)fy1+1; if(x0<0)x0=0; if(y0<0)y0=0; if(x1>(int)cw)x1=(int)cw; if(y1>(int)chh)y1=(int)chh;
        for(int y=y0;y<y1;y++) for(int x=x0;x<x1;x++){ float px=x+0.5f, py=y+0.5f;
            float b0=((X[1]-px)*(Y[2]-py)-(Y[1]-py)*(X[2]-px))*inv, b1=((X[2]-px)*(Y[0]-py)-(Y[2]-py)*(X[0]-px))*inv, b2=1.0f-b0-b1; if(b0<0||b1<0||b2<0)continue;
            float z=b0*Z[0]+b1*Z[1]+b2*Z[2]; u8* zp=vptr(pd3,zt+((u64)y*sw+x)*4); u8* cp=vptr(pd3,ct+((u64)y*sw+x)*4); if(!zp||!cp){ s3_err("target unmapped"); return; }
            float zo; memcpy(&zo,zp,4); if(!s3_nodepth&&!(z<zo))continue;
            float w=b0*iw[0]+b1*iw[1]+b2*iw[2], var[8]; for(int c=0;c<8;c++)var[c]=(b0*v[0][c]*iw[0]+b1*v[1][c]*iw[1]+b2*v[2][c]*iw[2])/w;
            float o[4]; if(!s3_fs(pd3,R,var,o))return; u32 pxl=0; for(int c=0;c<4;c++){ float f=o[c]; if(f<0)f=0; if(f>1)f=1; u32 b=(u32)(f*255.0f+0.5f); pxl|=b<<(c==0?16:c==1?8:c==2?0:24); }
            memcpy(cp,&pxl,4); memcpy(zp,&z,4); s3_pix++; } } }
static u32 tw_op(const u32* R,u32 d,u32 sv,int srcfmt){
    u32 op=R[0x2AC/4]; if(op==3)return sv; if(op!=6)return d;
    u32 b=R[0x2A8/4]>>24; u32 sa=srcfmt==0xE6u?255u:(sv>>24); u32 k=sa*b/255u; u32 o=0;
    for(int c=0;c<32;c+=8){ u32 sc=(sv>>c)&255u, dc=(d>>c)&255u; if(c==24&&srcfmt==0xE6u)sc=255u; u32 v=(sc*b+dc*(255u-k)+127u)/255u; if(v>255u)v=255u; o|=v<<c; }
    return o; }
static u32 CPR[16][0x800]; static int cp_launches=0, cp_bad=0, cp_wfi=0, cp_hang=0, cp_cbad=0; static u64 cp_pd3; static u32 cp_cx, cp_cy, cp_tx, cp_ty;
static u32* cp_ptr(u64 va){ u32* p=(u32*)vptr(cp_pd3,va); if(!p){ cp_bad++; static u32 z[4]; return z; } return p; }
#define GQK_HOST
#define GQK_ENTRY static
#define GQK_P(va) cp_ptr(va)
#define GQK_CX cp_cx
#define GQK_CY cp_cy
#define GQK_NX 32u
#define GQK_NY 4u
#define GQK_TX cp_tx
#define GQK_TY cp_ty
#define clampi gqk_clampi
#define bl gqk_bl
#define over gqk_over
#define rd gqk_rd
#include "drivers/video/nvidia/shaders/gqk.c"
#undef clampi
#undef bl
#undef over
#undef rd
static u32 qget(const u32* q,u32 lo,u32 hi){ u32 w=lo>>5, b=lo&31, n=hi-lo+1; u32 m=n>=32?0xFFFFFFFFu:((1u<<n)-1u); return (q[w]>>b)&m; }
static void cp_launch(u64 pd3,const u32* R){
    cp_pd3=pd3; u64 qa=(u64)R[0x2B4/4]<<8; u32* q=(u32*)vptr(pd3,qa); if(!q||R[0x2BC/4]!=3u){ cp_bad++; return; }
    nvkern k; nvc_parse(g_gqk_cubin,g_gqk_cubin_len,&k);
    if(qget(q,576,579)!=2||qget(q,580,583)!=2||qget(q,592,607)!=32||qget(q,608,623)!=4||qget(q,624,639)!=1||qget(q,448,463)!=1||qget(q,648,656)<k.regs||qget(q,134,134)!=1||qget(q,544,561)!=0){ cp_bad++; return; }
    u64 pa=(u64)qget(q,1536,1567)|((u64)qget(q,1568,1584)<<32); u8* code=vptr(pd3,pa); if(!code||(pa&255)||memcmp(code,k.code,64)||memcmp(code+k.code_len-16,k.code+k.code_len-16,16)){ cp_bad++; return; }
    u64 c0=(u64)qget(q,1024,1055)|((u64)qget(q,1056,1072)<<32); u64 c2=(u64)qget(q,1152,1183)|((u64)qget(q,1184,1200)<<32);
    if(!qget(q,640,640)||!qget(q,642,642)||(c0&255)||qget(q,1075,1087)*16<k.cbl[0]||qget(q,1203,1215)*16<k.cbl[2]){ cp_cbad++; return; }
    u32* cb=(u32*)vptr(pd3,c0); u8* cb2=vptr(pd3,c2); if(!cb||!cb2||memcmp(cb2,k.cb[2],k.cbl[2])){ cp_cbad++; return; }
    u32 gx=qget(q,384,415), gy=qget(q,416,431); if(cb[0]!=32||cb[1]!=4||cb[3]!=gx||cb[4]!=gy){ cp_cbad++; return; }
    nvop o; memcpy(&o,(u8*)cb+k.pofs,sizeof o);
    for(cp_cy=0;cp_cy<gy;cp_cy++) for(cp_cx=0;cp_cx<gx;cp_cx++) for(cp_ty=0;cp_ty<4;cp_ty++) for(cp_tx=0;cp_tx<32;cp_tx++) gqk(o);
    cp_launches++; }
static int sim_ce_hang=0, sim_ce_bad=0, sim_ce_ops=0, sim_rsz_ok=0, sim_rsz_n=0; static u32 sim_rsz_w=0, sim_rsz_h=0;
static u32 sim_rsz_maxw=0, sim_scale=0, sim_rsz_rel=0; u32 fb_desk_scale(void){ return sim_scale; }
int fb_desktop_resize(u32 w,u32 h,int apply){ if(!sim_rsz_ok)return 0; if(!apply&&w==1024&&h==600){ sim_rsz_rel++; return 1; } if(sim_rsz_maxw&&w>sim_rsz_maxw)return 0; if(apply){ sim_rsz_n++; sim_rsz_w=w; sim_rsz_h=h; } return 1; }
static void ce_exec(u32 token){
    int c=-1; for(int i=0;i<NCH;i++) if(CH[i].chid==(token&0xFFFF)&&CH[i].runl==(token>>16))c=i; if(c<0||!CH[c].sched)return;
    u64 pd3=rd64(VR+CH[c].inst+0x200)&~0xFFFULL; u32 put=*(u32*)(VR+CH[c].userd+0x8C), get=*(u32*)(VR+CH[c].userd+0x88);
    u32* R=R2D[c]; u32* HS=HOST[c]; if(cp_hang&&CH[c].et==1){ *(u32*)(VR+CH[c].userd+0x88)=put; return; }
    while(get!=put){ u8* gp=vptr(pd3,CH[c].gpva+get*8); if(!gp)return; u32 e0=*(u32*)gp, e1=*(u32*)(gp+4); u64 pva=((u64)(e1&0xFF)<<32)|(e0&0xFFFFFFFCu); u32 nd=(e1>>10)&0x1FFFFF;
        u32* pb=(u32*)vptr(pd3,pva); if(!pb)return; u32 regs[0x800]; memset(regs,0,sizeof regs); static u32 obj[16][8];
        for(u32 i=0;i<nd;){ u32 h=pb[i++]; u32 cnt=(h>>16)&0x1FFF, mth=(h&0xFFF)<<2, sub=(h>>13)&7; if((h>>29)!=1)return;
            for(u32 k=0;k<cnt;k++){ u32 m=mth+k*4; u32 v=pb[i++];
                if(m<0x100&&m){ HS[m/4]=v;
                    if(m==0x6C&&(v&7u)==1u){ u8* sm=vptr(pd3,((u64)HS[0x60/4]<<32)|(HS[0x5C/4]&0xFFFFFFFCu)); if(sm&&(v&(1u<<20)))*(u32*)sm=HS[0x64/4]; host_sems++; }
                    if(m==0x34&&(v>>27)==0x10u)mem_ops++;
                    continue; }
                if(m==0){ obj[c][sub]=v; continue; }
                if(obj[c][sub]==SC_CMP){ if(m<0x2000)CPR[c][m/4]=v; if(m==0x110)cp_wfi++; if(m==0x2BC)cp_launch(pd3,CPR[c]); continue; }
                if(obj[c][sub]==SC_3D){ if(m<0x3000)R3[c][m/4]=v; if(m==N3_CLEAR_SURFACE)s3_clear(pd3,R3[c],v); if(m==N3_DRAW_VERTEX_ARRAY_BEGIN_END_A+4)s3_draw(pd3,R3[c],v); continue; }
                if(obj[c][sub]==0x902Du){ if(m<0x1000)R[m/4]=v;
                    if(m==0x60C&&R[0x580/4]==4){ int x0=(int)R[0x600/4], y0=(int)R[0x604/4], x1=(int)R[0x608/4], y1=(int)v;
                        for(int y=y0;y<y1;y++) for(int x=x0;x<x1;x++){ u8* dp=s2d(pd3,R,1,x,y); if(dp){ u32 d=*(u32*)dp; *(u32*)dp=tw_op(R,d,R[0x588/4],(int)R[0x584/4]); } } tw_fills++; }
                    if(m==0x8DC){ int dx=(int)R[0x8B0/4], dy=(int)R[0x8B4/4], dw=(int)R[0x8B8/4], dh=(int)R[0x8BC/4];
                        u64 du=((u64)R[0x8C4/4]<<32)|R[0x8C0/4], dv=((u64)R[0x8CC/4]<<32)|R[0x8C8/4], sx=((u64)R[0x8D4/4]<<32)|R[0x8D0/4], sy=((u64)v<<32)|R[0x8D8/4];
                        u64 rnd=(R[0x88C/4]&1u)?0:(1ULL<<31); u32 sw2=R[0x248/4], sh2=R[0x24C/4];
                        for(int j=0;j<dh;j++) for(int i2=0;i2<dw;i2++){ int px=(int)((sx+du*(u64)i2+rnd)>>32), py=(int)((sy+dv*(u64)j+rnd)>>32); if(px>=(int)sw2)px=(int)sw2-1; if(py>=(int)sh2)py=(int)sh2-1;
                            u8* sp=s2d(pd3,R,0,px,py); u8* dp=s2d(pd3,R,1,dx+i2,dy+j); if(sp&&dp){ u32 d=*(u32*)dp; *(u32*)dp=tw_op(R,d,*(u32*)sp,(int)R[0x230/4]); } }
                        tw_blits++; }
                    continue; }
                if(m<0x2000)regs[m/4]=v;
                if(m==0x300&&obj[c][sub]==SC_CE&&(v&0x183u)==0x182u){ u64 s=((u64)regs[0x400/4]<<32)|regs[0x404/4], d=((u64)regs[0x408/4]<<32)|regs[0x40C/4]; u32 len=regs[0x418/4];
                    u32 lines=(v&0x200u)?regs[0x41C/4]:1, sp_=regs[0x410/4], dp_=regs[0x414/4];
                    if(v&0x400u){ u32 cm=regs[0x708/4]; if((cm&7u)!=4u||((cm>>16)&3u)!=3u||((cm>>20)&3u)||((cm>>24)&3u))return;
                        for(u32 l=0;l<lines;l++) for(u32 o=0;o<len;o++){ u8* dp=vptr(pd3,d+(u64)l*dp_+(u64)o*4); if(!dp)return; memcpy(dp,&regs[0x700/4],4); } ce_fills++; }
                    else { if(sim_ce_hang)return; for(u32 l=0;l<lines;l++) for(u32 o=0;o<len;o++){ u8* sp=vptr(pd3,s+(u64)l*sp_+o); u8* dp=vptr(pd3,d+(u64)l*dp_+o); if(!sp||!dp)return; *dp=*sp; } sim_ce_ops++; if(sim_ce_bad){ u8* dp=vptr(pd3,d); if(dp)*dp^=0x5A; } }
                    if(lines>1)ce_lines+=lines;
                    if(((v>>3)&3u)==1u){ u8* sm=vptr(pd3,((u64)regs[0x240/4]<<32)|regs[0x244/4]); if(sm)*(u32*)sm=regs[0x248/4]; }
                    ce_done=1; } } }
        get=(get+1)%512; *(u32*)(VR+CH[c].userd+0x88)=get; } }

static void dmemw(int k,u32 v){ u32 a=(FAL[k].dc&0xFFFC)|(((FAL[k].dc>>8)&0xFF)<<8); FAL[k].dmem[(a/4)%(0x20000/4)]=v; FAL[k].dc=(FAL[k].dc&~0xFFFFu)|((a+4)&0xFFFF); }
static void imemw(int k,u32 v){ u32 a=FAL[k].ic&0xFFFF; FAL[k].imem[(a/4)%(0x10000/4)]=v; if((a&0xFF)==0){ FAL[k].sec[(a>>8)&0xFF]=(FAL[k].ic>>28)&1; } FAL[k].ic=(FAL[k].ic&~0xFFFFu)|((a+4)&0xFFFF); }
static u32 last_tag[2];
static int fwsec_ok=0, bl_ok=0, booter_ok=0, sb_runs=0;
static void falcon_start(u32 f){
    int k=fi(f); REG[(f+0x100)/4]|=0x10;
    if(k==0&&!gsp_on){ u32 lim=(REG[(f+0x108)/4]&0x1FF)<<8; bl_ok=FAL[0].tags[(lim-0x200)>>8]==0xFD&&REG[(f+0x104)/4]==0xFD00u&&(REG[(f+0x610)/4]&7)==5;
        u8* d=(u8*)FAL[0].dmem; u64 code=rd64(d+0x24), data=rd64(d+0x40); u8* dm=(u8*)data;
        u32 mapper=*(u32*)(dm+0x10+4+8+4); u32 cin=*(u32*)(dm+mapper+8); u32 icmd=*(u32*)(dm+mapper+0x2C); u8* cmdp=dm+cin;
        fwsec_ok=code&&*(u32*)(d+0x20)==4&&data==code+0x200&&icmd==(sim_mode?0x19u:0x15u)&&*(u32*)cmdp==1&&*(u32*)(cmdp+4)==24&&*(u32*)(cmdp+0x14)==2;
        if(!sim_mode)fwsec_ok=fwsec_ok&&*(u32*)(cmdp+0x18)==1&&*(u32*)(cmdp+0x20)==(u32)(g_nvl.frts_addr>>12)&&*(u32*)(cmdp+0x24)==0x100&&*(u32*)(cmdp+0x28)==2;
        if(icmd==0x15u&&REG[0x1FA828/4]){ REG[0x1438/4]=0x00030000u; REG[0x1454/4]=0; REG[(f+0x40)/4]=0; return; }
        if(icmd==0x15u){ REG[0x1FA824/4]=(u32)((g_nvl.frts_addr>>12)<<4); REG[0x1FA828/4]=(u32)(((g_nvl.frts_addr+g_nvl.frts_size)>>12)<<4); } else sb_runs++;
        REG[0x1438/4]=0; REG[0x1454/4]=0; REG[(f+0x40)/4]=0; return; }
    if(k==1&&gsp_on&&gsp_init&&!sim_mode){ int ok=gsp_was_reset&&REG[0x110040/4]==(u32)PA(G.libos)&&REG[0x110044/4]==(u32)(PA(G.libos)>>32);
        if(ok){ REG[0x1180F8/4]|=1u<<26; REG[(f+0x40)/4]=0; REG[0x111240/4]|=1u; resumed=1; } else REG[(f+0x40)/4]=0xBAD; return; }
    if(k==1){ u64 meta=(u64)REG[(f+0x40)/4]|((u64)REG[(f+0x44)/4]<<32); u8* dm=(u8*)FAL[1].dmem;
        if(sim_mode||REG[(f+0x40)/4]==0xFF){ booter_ok=REG[(f+0x40)/4]==0xFF; REG[0x1FA828/4]=0; REG[(f+0x40)/4]=0; return; }
        booter_ok=meta&&rd64((u8*)meta)==0xDC3AAE21371A60B3ULL&&memcmp(dm+0x200,g_fw_bload.d+0x3C,16)==0&&FAL[1].sec[0]==0&&FAL[1].sec[1]==1&&FAL[1].tags[1]==1&&FAL[1].tags[0x7F]==0x7F;
        if(booter_ok){ *(u64*)((u8*)meta+0xF8)=0xA0A0A0A0A0A0A0A0ULL; REG[0x111240/4]=1; gsp_on=1; sim_shm=G.shm; }
        REG[(f+0x40)/4]=booter_ok?0:0xBAD; } }
u32 nvr32(u32 off){
    if(off>=0x700000&&off<0x800000){ u64 a=((u64)REG[0x1700/4]<<16)+(off-0x700000); return a+4<=VRSZ?*(u32*)(VR+a):0; }
    if(off>=0x300000&&off<0x400000){ u32 a=off-0x300000; return a+4<=ROMSZ?*(u32*)(ROM+a):0xFFFFFFFFu; }
    return REG[off/4]; }
void nvw32(u32 off,u32 v){
    if(off>=0x700000&&off<0x800000){ u64 a=((u64)REG[0x1700/4]<<16)+(off-0x700000); if(a+4<=VRSZ)*(u32*)(VR+a)=v; return; }
    for(int k=0;k<2;k++){ u32 f=k?NVF_SEC2:NVF_GSP; if(off==f+0x180){ FAL[k].ic=v; return; } if(off==f+0x188){ u32 a=FAL[k].ic&0xFFFF; FAL[k].tags[(a>>8)&0xFF]=v; last_tag[k]=v; return; }
        if(off==f+0x184){ imemw(k,v); return; } if(off==f+0x1C0){ FAL[k].dc=v; return; } if(off==f+0x1C4){ dmemw(k,v); return; }
        if(off==f+0x100&&(v&2)){ REG[off/4]=v&~2u; falcon_start(f); return; } if(off==f+0x3C0){ REG[off/4]=v; if(k==0&&(v&1u)&&gsp_on){ REG[0x111240/4]&=~1u; gsp_was_reset=1; } return; } }
    if(off==0xB830B0){ REG[off/4]=v&~0x80000000u; return; }
    if(off>=0x611800&&off<0x611810){ REG[off/4]&=~v; return; }
    if(off==0x110080){ REG[off/4]=v; if(gsp_on&&!gsp_init)gsp_boot(); else if(resume_wait&&resumed&&(REG[0x111240/4]&1u)){ u8 z[4]={0}; resume_wait=0; msg_post(0x1001,z,4,0); } return; }
    if(off==0x110C00){ if(gsp_init)gsp_service(); return; }
    if(off==0xBB0090){ ce_exec(v); return; }
    if(off==0x680000){ REG[off/4]=v; kd_exec(0); return; }
    if(off>=0x690000&&off<0x698000&&!(off&0xFFF)){ REG[off/4]=v; kd_exec(1+(int)((off-0x690000)>>12)); return; }
    REG[off/4]=v; }

static void build_rom(void){
    ROM=calloc(1,ROMSZ); u8* r=ROM;
    r[0]=0x55; r[1]=0xAA; *(u16*)(r+0x18)=0x40; memcpy(r+0x40,"PCIR",4); *(u16*)(r+0x4A)=0x18; *(u16*)(r+0x50)=0x20; r[0x54]=0; r[0x55]=0;
    memcpy(r+0x60,"NPDE",4); *(u16*)(r+0x68)=0x20; r[0x6A]=0;
    u8 bit[]={0xFF,0xB8,'B','I','T',0,0,0,0,6,1,0}; memcpy(r+0x200,bit,sizeof bit); u8* e=r+0x200+12; e[0]='p'; e[1]=2; *(u16*)(e+2)=4; *(u16*)(e+4)=0x300;
    *(u32*)(r+0x300)=0x4100;
    u32 b1=0x4000; r[b1]=0x55; r[b1+1]=0xAA; *(u16*)(r+b1+0x18)=0x40; memcpy(r+b1+0x40,"PCIR",4); *(u16*)(r+b1+0x4A)=0x18; *(u16*)(r+b1+0x50)=0x20; r[b1+0x54]=3;
    u32 b2=0x8000; r[b2]=0x55; r[b2+1]=0xAA; *(u16*)(r+b2+0x18)=0x40; memcpy(r+b2+0x40,"PCIR",4); *(u16*)(r+b2+0x4A)=0x18; *(u16*)(r+b2+0x50)=0x40; r[b2+0x54]=0xE0; r[b2+0x55]=0x80;
    u8* t=r+0x8100; t[0]=1; t[1]=6; t[2]=6; t[3]=2; t[6]=0x05; *(u32*)(t+8)=0x5000; t[12]=0x85; t[13]=7; *(u32*)(t+14)=0x4200;
    u8* d=r+0x8200; *(u32*)d=(60u<<16)|(2u<<8)|1u; *(u32*)(d+0x10)=0x10; *(u32*)(d+0x14)=0; *(u32*)(d+0x18)=0x200; *(u32*)(d+0x20)=0x100; *(u32*)(d+0x24)=0x100; *(u32*)(d+0x28)=0x200; *(u32*)(d+0x2C)=0; *(u32*)(d+0x30)=0x400;
    u8* uc=d+60; for(int i=0;i<0x200;i++)uc[i]=(u8)(i^0x5A); u8* dm=uc+0x200; dm[0x10]=1; dm[0x11]=4; dm[0x12]=8; dm[0x13]=2; *(u32*)(dm+0x14)=1; *(u32*)(dm+0x18)=0x50; *(u32*)(dm+0x1C)=4; *(u32*)(dm+0x20)=0x80;
    *(u32*)(dm+0x80)=0x50414D44u; *(u32*)(dm+0x88)=0x200; }
static nvblob rdfile(const char* p){ nvblob b={0,0,"disk"}; FILE* f=fopen(p,"rb"); if(!f)return b; fseek(f,0,2); long n=ftell(f); fseek(f,0,0); b.d=malloc(n); b.n=(u32)fread(b.d,1,n,f); fclose(f); return b; }

int g_nv_ada_on=0;
static const nvgsp_cls sim_cls_ad={ .chan=0xC56Fu, .user=0xC561u, .ce=0xC7B5u, .twod=0x902Du, .threed=0xC997u, .compute=0xC9C0u, .disp=0xC770u, .core=0xC77Du, .wndw=0xC67Eu, .wimm=0xC67Bu, .curs=0xC67Au, .doorbell=0xBB0090u, .wndw_storage_pitch=0, .qmd_major=3, .sm=89 };
const nvgsp_gen nvgsp_gen_ad10x={ .name="Ada AD10x (Turing falcons in the simulator)", .chip_lo=0x192, .chip_hi=0x197, .untested=1, .sig_section=".fwsignature_tu11x", .cls=&sim_cls_ad };
int main(int argc,char** argv){
    const char* fwdir=argc>1?argv[1]:"nvidia"; char p[512];
    NVSE[NVSET_MODESET].on=0; NVSE[NVSET_CURSOR].on=0; NVSE[NVSET_CARDMEM].on=0; NVSE[NVSET_DPTRAIN].on=0;
    if(getenv("NVSIM_VERBOSE"))g_quiet=0;
    REG=calloc(1,0x1000000); VR=mmap(0,VRSZ,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANONYMOUS|MAP_NORESERVE,-1,0); build_rom();
    REG[0]=0x168000A1u; REG[0x100CE0/4]=(6u<<4)|10u; REG[0x118128/4]=1; REG[0x118234/4]=0xFF; REG[(NVF_GSP+0x108)/4]=0x100|(0x80u<<9); REG[(NVF_SEC2+0x108)/4]=0x100|(0x100u<<9);
    REG[0x300000/4]=0;
    snprintf(p,sizeof p,"%s/tu102/gsp/gsp-535.113.01.bin",fwdir); g_fw_gsp=rdfile(p); snprintf(p,sizeof p,"%s/tu102/gsp/bootloader-535.113.01.bin",fwdir); g_fw_bl=rdfile(p);
    snprintf(p,sizeof p,"%s/tu116/gsp/booter_load-535.113.01.bin",fwdir); g_fw_bload=rdfile(p); snprintf(p,sizeof p,"%s/tu116/gsp/booter_unload-535.113.01.bin",fwdir); g_fw_bunload=rdfile(p);
    snprintf(p,sizeof p,"%s/tu116/acr/bl.bin",fwdir); g_fw_acr=rdfile(p);
    if(!g_fw_gsp.d||!g_fw_bl.d||!g_fw_bload.d||!g_fw_bunload.d||!g_fw_acr.d){ printf("firmware files not found under %s - skipped\n",fwdir); return 0; }
    printf("NVIDIA GSP driver against a simulated TU116 (6 GB)\n");
    nvl_reset(); int r=probe(); chk("PCI BARs, BOOT0, chipset 0x168, GFW_BOOT, VRAM size 6 GB",r>0&&G.chip==0x168&&G.bar1==BAR1&&G.bar3==BAR3&&nv_vram_size()==(6ULL<<30));
    nv_fb_layout(0x16A3350ULL,0x1000);
    chk("FB layout matches nouveau for 6 GB (FRTS 0x17fe00000, WPR2 0x177d00000, heap 0x6900000)",g_nvl.frts_addr==0x17FE00000ULL&&g_nvl.wpr2_addr==0x177D00000ULL&&g_nvl.heap_size==0x6900000ULL&&g_nvl.elf_addr==0x17E750000ULL&&g_nvl.boot_addr==0x17FDFF000ULL&&g_nvl.nonwpr_addr==0x177C00000ULL&&g_nvl.wpr2_size==0x8200000ULL);
    REG[0x100CE0/4]=(4u<<4)|10u; nv_fb_layout(0x16A3350ULL,0x1000);
    chk("FB layout matches nouveau for 4 GB (FRTS 0xffe00000, WPR2 0xf7d00000)",g_nvl.frts_addr==0xFFE00000ULL&&g_nvl.wpr2_addr==0xF7D00000ULL&&g_nvl.nonwpr_addr==0xF7C00000ULL);
    REG[0x100CE0/4]=(6u<<4)|10u;
    { nvv_init(0,1ULL<<30); u64 tot=nvv_free_bytes(); u64 a[64]={0}; int ok=1; srand(5);
      for(int it=0;it<2000&&ok;it++){ int k=rand()%64; if(a[k]){ nvv_free(a[k]); a[k]=0; } else { u64 al=1ULL<<(12+rand()%10); a[k]=nvv_alloc((u64)(1+rand()%300000),al); if(a[k]&&(a[k]&(al-1)))ok=0; }
        for(int x=0;x<64&&ok;x++) for(int y=0;y<64;y++) if(x!=y&&a[x]&&a[y]){ u64 sx=0,sy=0; for(int q=0;q<g_nvu;q++){ if(g_nvvu[q].a==a[x])sx=g_nvvu[q].s; if(g_nvvu[q].a==a[y])sy=g_nvvu[q].s; } if(a[x]<a[y]+sy&&a[y]<a[x]+sx)ok=0; } }
      for(int k=0;k<64;k++) if(a[k])nvv_free(a[k]);
      chk("VRAM allocator: 2000 random allocs/frees, aligned, never overlapping, fully coalesced after",ok&&nvv_free_bytes()==tot&&g_nvf==1); }
    { fflush(stdout); pid_t pid=fork();
      if(pid==0){ sim_bars=1; nv_fb_layout(0x16A3350ULL,0x1000); SIM_BAR1PD=g_nvl.nonwpr_addr+0x10000; SIM_BAR2PD=g_nvl.nonwpr_addr+0x20000;
          *(u64*)g_bootinfo=BAR1; *(u32*)(g_bootinfo+8)=4096; *(u16*)(g_bootinfo+12)=1024; *(u16*)(g_bootinfo+14)=600;
          int rr=nvgsp_cmd("all"); int ok=rr>0&&nvgsp_present_on()&&nv2d_ready()&&nvc_ready()&&G.state==7&&strncmp(g_logout,"nvgsp all - RESULT: all steps passed",36)==0&&!strstr(g_nv_log,"FAILED");
          if(!ok){ printf("    cold start: r=%d present=%d 2d=%d compute=%d state=%d\n%.600s\n",rr,nvgsp_present_on(),nv2d_ready(),nvc_ready(),G.state,g_logout); fflush(stdout); }
          _exit(ok?0:1); }
      int st=1; waitpid(pid,&st,0);
      fflush(stdout); pid_t p2=fork();
      if(p2==0){ sim_bars=1; nv_fb_layout(0x16A3350ULL,0x1000); SIM_BAR1PD=g_nvl.nonwpr_addr+0x10000; SIM_BAR2PD=g_nvl.nonwpr_addr+0x20000;
          *(u64*)g_bootinfo=BAR1; *(u32*)(g_bootinfo+8)=4096; *(u16*)(g_bootinfo+12)=1024; *(u16*)(g_bootinfo+14)=600;
          REG[0x1FA824/4]=0x00F7D000u; REG[0x1FA828/4]=0x00F7E000u; int rr=nvgsp_cmd("all");
          int ok=rr>0&&sb_runs==1&&strstr(g_nv_log,"WPR2 cleared")!=0&&G.state==7;
          if(!ok){ printf("    stale WPR2: r=%d sb=%d state=%d\n%.500s\n",rr,sb_runs,G.state,g_logout); fflush(stdout); }
          _exit(ok?0:1); }
      int st2=1; waitpid(p2,&st2,0);
      fflush(stdout); pid_t p3=fork();
      if(p3==0){ sim_bars=1; nv_fb_layout(0x16A3350ULL,0x1000); SIM_BAR1PD=g_nvl.nonwpr_addr+0x10000; SIM_BAR2PD=g_nvl.nonwpr_addr+0x20000;
          *(u64*)g_bootinfo=BAR1; *(u32*)(g_bootinfo+8)=4096; *(u16*)(g_bootinfo+12)=1024; *(u16*)(g_bootinfo+14)=600;
          sim_boot0=REG[0]=0x194000A1u; g_nv_dev=0x2786; SC_CHAN=0xC56F; SC_CE=0xC7B5; SC_3D=0xC997; SC_CMP=0xC9C0; REG[0x100CE0/4]=(1u<<4)|10u; REG[0x1183A4/4]=6144;
          g_nv_ada_on=0; int r0=nvgsp_cmd("all"); int refused=r0<=0&&g_nv_step==1&&strstr(g_nv_why,"gpu ada on")!=0&&G.state==0;
          g_nv_ada_on=1; int rr=nvgsp_cmd("all");
          int gotcls=0; for(int i=0;i<NCH;i++) for(int k=0;k<CH[i].ncls;k++){ u32 c=CH[i].cls[k]; if(c==0xC997)gotcls|=1; if(c==0xC9C0)gotcls|=2; if(c==0x902D)gotcls|=4; if(c==0xC7B5)gotcls|=8; }
          int ok=refused&&rr>0&&G.state==7&&g_nvgen==&nvgsp_gen_ad10x&&nv_vram_size()==(6ULL<<30)&&gotcls==15&&alloc_bad==0&&nvgsp_present_on()&&nv2d_ready()&&!nvc_ready()&&strstr(g_nv_log,"Ada AD10x")!=0;
          if(!ok){ printf("    Ada classes: refused=%d r=%d state=%d cls=%x bad=%d present=%d 2d=%d compute=%d vram=%llx\n%.900s\n",refused,rr,G.state,gotcls,alloc_bad,nvgsp_present_on(),nv2d_ready(),nvc_ready(),(unsigned long long)nv_vram_size(),g_logout); fflush(stdout); }
          _exit(ok?0:1); }
      int st3=1; waitpid(p3,&st3,0);
      chk("RTX 40 path: AD104 refused until gpu ada on, then nvgsp all with the Ada table: VRAM from 0x1183a4, GPFIFO c56f, ADA_A c997, ADA_COMPUTE_A c9c0, 902d, CE c7b5; present and 2D run, sm_75 compute shader refused",WIFEXITED(st3)&&WEXITSTATUS(st3)==0);
      chk("WPR2 left set by an earlier start (no unload) is cleared first with FWSEC-SB and booter_unload, then the start succeeds",WIFEXITED(st2)&&WEXITSTATUS(st2)==0);
      chk("nvgsp all from a cold start: steps 1-18 pass, RESULT says all steps passed, screen, 2D and compute stay on",WIFEXITED(st)&&WEXITSTATUS(st)==0); }
    nvl_reset(); r=nvb_read_rom();
    chk("VBIOS: PROM read, 3 images walked, extended image 0xE0 found for the pointer fix-up",r&&g_img0==0x4000&&g_imgd==0x8000);
    nvfwsec fw; r=nvb_find_fwsec(&fw);
    chk("FWSEC found through BIT 'p' and the falcon ucode table (app 0x85, descriptor v2)",r&&fw.imem_size==0x200&&fw.dmem_size==0x400&&fw.dmem_off==0x200&&fw.buf&&fw.buf[0]==0x5A);
    u64 vid=0; { nvl_reset(); G.state=0; r=gsp_pre(); vid=*(u64*)(G.meta+0xF8); }
    chk("GSP pre-boot: firmware, radix3, LibOS, queues, WPR meta, FWSEC-FRTS, booter",r>0&&G.state==1);
    chk("FWSEC: BL at top of IMEM with tag 0xfd, BOOTVEC 0xfd00, TRANSCFG(4)=5",bl_ok);
    chk("FWSEC: DMA descriptor and patched FRTS command (region = FRTS >> 12, 0x100 pages, type FB)",fwsec_ok);
    chk("booter: secure IMEM tags, signature at DMEM 0x200, WPR meta in SEC2 mailboxes",booter_ok&&vid==0xA0A0A0A0A0A0A0A0ULL);
    { int ok=G.r3l0[0]==PA(G.r3l1); u64 pages=(G.elf_size+0xFFF)>>12; for(u64 j=0;j<pages&&ok;j++){ u64* l2=(u64*)G.r3l1[j/512]; if(l2[j%512]!=PA(G.elf)+(j<<12))ok=0; } chk("radix3: level0 -> level1 -> level2 -> every 4 KB page of .fwimage in order",ok); }
    { u8* m=G.meta; chk("GspFwWprMeta: magic, sizes and every FB offset from the layout",rd64(m)==0xDC3AAE21371A60B3ULL&&rd64(m+0x18)==0x16A3350ULL&&rd64(m+0x70)==0x177D00000ULL&&rd64(m+0x98)==0x17FE00000ULL&&rd64(m+0xB0)==(6ULL<<30)&&rd64(m+0x50)==0x1000); }
    SIM_BAR1PD=g_nvl.nonwpr_addr+0x10000; SIM_BAR2PD=g_nvl.nonwpr_addr+0x20000;
    *(u64*)g_bootinfo=BAR1; *(u32*)(g_bootinfo+8)=4096; *(u16*)(g_bootinfo+12)=1024; *(u16*)(g_bootinfo+14)=600;
    nvl_reset(); r=gsp_post();
    chk("GSP-RM: INIT_DONE received, static info parsed (name, 3 GPCs / 9 TPCs, 2 FB regions)",r>0&&G.state==3&&G.tpcs==9&&G.nfbr==2&&!strncmp(G.name,"NVIDIA GeForce GTX 1660",23));
    chk("queued SET_SYSTEM_INFO (BAR0/1/3, config mirror) and SET_REGISTRY (117 bytes) are exact",sysinfo_ok&&reg_ok);
    chk("CPU sequencer event executed (register write + store), GSP error log printed",REG[0x12340/4]==0xBEEF&&G.seq_count==1&&G.err_count>=1);
    chk("sequencer CORE_RESET/START/WAIT_FOR_HALT/CORE_RESUME: GSP reset, LibOS args, SEC2 restart, booter-done bit, app version, then INIT_DONE",G.resumes==1&&resumed&&!G.seq_err[0]&&REG[0x110080/4]==G.app_ver&&(REG[0x1180F8/4]>>26&1u));
    chk("VRAM allocator handed the usable FB region below the GSP reservation",nvv_total()>0&&nvv_total()<=g_nvl.nonwpr_addr);
    nvl_reset(); r=bars(); int s2=0,s1=0; u64 w2=walk(g_bar2.pd3,0x1234,&s2), w1=walk(SIM_BAR1PD,g_bva1+0x10,&s1);
    chk("BAR2: page tables map VA 0 to the test page, UPDATE_BAR_PDE sent with PD3[0] / shift 47",bar_pde_ok&&w2==g_bt2+0x1234&&!s2);
    chk("BAR1: tables installed under GSP's root PD map the test window to VRAM",w1==g_bt1+0x10&&!s1);
    G.state=4; nvl_reset(); r=objects();
    chk("golden context: VA space with server-reserved PDEs (4 GB, 3 levels), promote, TURING_A",reserved_pdes_ok&&G.state>=5);
    chk("client, device, subdevice, FERMI_VASPACE_A, SET_PAGE_DIRECTORY on the host PD3",vas_pd3[1]!=0);
    chk("GR channel + TURING_A + TURING_COMPUTE_A, CE channel + TURING_DMA_COPY_A, all params exact",alloc_bad==0&&ctrl_bad==0&&NCH==3);
    chk("PROMOTE_CTX for our channel maps the shared PRIV_ACCESS_MAP; the copy channel uses CE2 (own runlist 8), not the graphics CE0",CH[1].promoted&&CH[2].et==11&&CH[2].runl==8);
    chk("copy engine: pushbuffer through GPFIFO and doorbell copies 4 KB via the GPU page tables",r>0&&ce_done&&G.state==7);
    chk("every command-queue element has a valid checksum and sequence number",cks_bad==0&&seq_bad==0&&cmd_expect_seq>10);
    { int W=300,H=170,SP=1024; u32* src=malloc(W*H*4); u32* dst=calloc((size_t)SP*200,4); for(int i=0;i<W*H;i++)src[i]=0x10000u*i+7u;
      u64 sv=nvgsp_ce_map((u64)src,(u64)W*H*4), dv=nvgsp_ce_map((u64)dst,(u64)SP*200*4); u32 lb=ce_lines;
      int ok=sv&&dv&&nvgsp_ce_copy2d(sv,W*4,dv+(u64)(10*SP+20)*4,SP*4,W*4,H);
      int same=1; for(int y=0;y<H&&same;y++) for(int x=0;x<W;x++) if(dst[(y+10)*SP+20+x]!=src[y*W+x]){ same=0; break; }
      int untouched=dst[9*SP+20]==0&&dst[10*SP+19]==0&&dst[10*SP+20+W]==0&&dst[(10+H)*SP+20]==0;
      int ok2=nvgsp_ce_copy2d(sv,W*4,dv,SP*4,W*4,2); u32 cp=0,to=0; nvgsp_ce_stats(&cp,&to);
      chk("copy engine 2D copy: 300x170 pixels into a pitched frame, completion semaphore, nothing outside touched",ok&&same&&untouched&&ok2&&ce_lines-lb==H+2&&cp==2&&to==0); free(src); free(dst); }
    { int r16=scanout(); int fbmap=0; u64 fw=walk(g_uv.pd3,g_sc.va+4096*7+12,&fbmap);
      chk("screen through the copy engine: screen VRAM mapped, CE fill (remap) and RAM-to-VRAM copy read back",r16==1&&g_sc.ok&&nvgsp_present_on()&&ce_fills==1&&g_ce_fills==1&&fw==4096*7+12&&!fbmap);
      int SWd=1040, Wd=1024, Hd=600; u32* sc=malloc((size_t)SWd*Hd*4); for(int i=0;i<SWd*Hd;i++)sc[i]=0x01000193u*(u32)i+11u;
      memset(VR,0,4096*Hd); int ok=nvgsp_fb_present(sc,SWd,Wd,Hd,(volatile u32*)VR,4096); int same=1;
      for(int y=0;y<Hd&&same;y++) for(int x=0;x<Wd;x++) if(((u32*)VR)[y*1024+x]!=sc[y*SWd+x]){ same=0; break; }
      chk("desktop present: 1024x600 frame with a 1040-pixel stride lands in the 4096-byte-pitch screen, read-back passes",ok&&same&&g_sc.frames==1);
      int wrongp=nvgsp_fb_present(sc,SWd,Wd,Hd,(volatile u32*)VR,8192);
      u32* fake=calloc((size_t)1024*Hd,4); int bad=nvgsp_fb_present(sc,SWd,Wd,Hd,(volatile u32*)fake,4096);
      chk("present refuses a different pitch, and turns itself off when the screen read-back does not match",!wrongp&&!bad&&!nvgsp_present_on()&&g_sc.bad==1&&g_sc.why[0]);
      nvgsp_cmd("present on"); int on=nvgsp_present_on(); int ok2=nvgsp_fb_present(sc,SWd,Wd,Hd,(volatile u32*)VR,4096);
      u32* prev=malloc((size_t)SWd*Hd*4); memcpy(prev,sc,(size_t)SWd*Hd*4); srand(9);
      int boxes[5][4]={{0,0,3,1},{100,37,300,120},{1020,590,4,10},{500,0,64,600},{63,15,2,2}};
      for(int b=0;b<5;b++) for(int y=boxes[b][1];y<boxes[b][1]+boxes[b][3]&&y<Hd;y++) for(int x=boxes[b][0];x<boxes[b][0]+boxes[b][2]&&x<Wd;x++) sc[y*SWd+x]^=0x00A5A5A5u+(u32)b;
      dmg_rect rr[DMG_MAX]; int nr=dmg_rects(sc,prev,SWd,Wd,Hd,rr,DMG_MAX); u64 area=nr>0?dmg_area(rr,nr):0; u32 cb=ce_lines;
      int ok3=nr>0&&nvgsp_fb_present_rects(sc,SWd,Wd,Hd,rr,nr,(volatile u32*)VR,4096); int same3=1;
      for(int y=0;y<Hd&&same3;y++) for(int x=0;x<Wd;x++) if(((u32*)VR)[y*1024+x]!=sc[y*SWd+x]){ same3=0; break; }
      chk("changed areas only: 5 edits give a few tile rectangles, the copy engine sends just those and the screen matches",ok3&&same3&&nr>=3&&nr<=12&&area<(u64)Wd*Hd/3&&g_sc.rframes==1&&ce_lines-cb<=area/64+64);
      nvgsp_cmd("present off");
      chk("nvgsp present on / off switch the screen path",on&&ok2&&!nvgsp_present_on()&&g_sc.ok);
      free(prev);
      free(sc); free(fake); }
    { int W2=777,H2=333,S2=800; u32* a=malloc((size_t)S2*H2*4); u32* b=malloc((size_t)S2*H2*4); int allok=1; srand(3);
      for(int t=0;t<300&&allok;t++){ for(int i=0;i<S2*H2;i++)a[i]=b[i]=(u32)rand(); int ne=t%7;
        for(int e=0;e<ne;e++){ int x=rand()%W2, y=rand()%H2, w=1+rand()%120, h=1+rand()%90; for(int yy=y;yy<y+h&&yy<H2;yy++) for(int xx=x;xx<x+w&&xx<W2;xx++)a[yy*S2+xx]^=1u+(u32)rand(); }
        dmg_rect r[DMG_MAX]; int n=dmg_rects(a,b,S2,W2,H2,r,DMG_MAX); if(n<0){ allok=0; break; } if(!ne&&n){ allok=0; break; }
        static unsigned char cov[800*333]; memset(cov,0,sizeof cov);
        for(int i=0;i<n;i++){ if(r[i].x<0||r[i].y<0||r[i].x+r[i].w>W2||r[i].y+r[i].h>H2||r[i].x%DMG_TW||r[i].y%DMG_TH){ allok=0; break; } for(int y=r[i].y;y<r[i].y+r[i].h;y++) for(int x=r[i].x;x<r[i].x+r[i].w;x++){ if(cov[y*S2+x])allok=0; cov[y*S2+x]=1; } }
        for(int y=0;y<H2&&allok;y++) for(int x=0;x<W2;x++) if(a[y*S2+x]!=b[y*S2+x]&&!cov[y*S2+x]){ allok=0; break; }
        for(int ty=0;ty<H2&&allok;ty+=DMG_TH) for(int tx=0;tx<W2;tx+=DMG_TW) if(cov[ty*S2+tx]){ int d=0; for(int y=ty;y<ty+DMG_TH&&y<H2;y++) for(int x=tx;x<tx+DMG_TW&&x<W2;x++) if(a[y*S2+x]!=b[y*S2+x])d=1; if(!d){ allok=0; break; } } }
      for(int i=0;i<S2*H2;i++)a[i]=~b[i]; dmg_rect r1[DMG_MAX]; int n1=dmg_rects(a,b,S2,W2,H2,r1,DMG_MAX);
      chk("damage rectangles: 300 random frames, every changed pixel covered once, no unchanged tile sent, full change = 1 rectangle",allok&&n1==1&&r1[0].w==W2&&r1[0].h==H2); free(a); free(b); }
    { nvl_reset(); char ln[200]; for(int i=0;i<150;i++)ln[i]=(char)(i%11==10?' ':'a'+i%26); ln[150]=0; nvl_s("  "); nvl_s(ln); nvl_nl(); nvl_s("short"); nvl_nl(); nv_fail(6,"why text");
      int n=log_build(-6,"all"); int maxl=0, cur=0, nsp=0, osp=0; for(int i=0;i<n;i++){ if(g_logout[i]=='\n'){ if(cur>maxl)maxl=cur; cur=0; } else cur++; if(g_logout[i]!=' '&&g_logout[i]!='\n')nsp++; }
      for(int i=0;g_nv_log[i];i++) if(g_nv_log[i]!=' '&&g_nv_log[i]!='\n')osp++;
      const char* hdr="nvgsp all - RESULT: FAILED at step 6: why text\n"; int head=strncmp(g_logout,hdr,strlen(hdr))==0;
      int hdrchars=0; for(const char* q=g_logout;*q&&q<strstr(g_logout,"\n---");q++) if(*q!=' '&&*q!='\n')hdrchars++; const char* body=strstr(g_logout,"-\n"); int bc=0; for(const char* q=body+2;*q;q++) if(*q!=' '&&*q!='\n')bc++;
      chk("saved log: RESULT line first, every line at most 72 columns, no text lost in the wrapping",head&&maxl<=72&&bc==osp&&nsp>hdrchars); }
    { int r17=twod(); u32 ops=0,subs=0,to=0,fr=0; nv2d_stats(&ops,&subs,&to,&fr);
      chk("step 17: FERMI_TWOD_A fills, copies, 2x scale and premultiplied blend checked by read-back, one semaphore per push buffer",r17==1&&nv2d_ready()&&tw_fills==3&&tw_blits==3&&host_sems>=1&&to==0);
      u32 fw=200, fh=100; u32* fr2=malloc((size_t)fw*fh*4); for(u32 i=0;i<fw*fh;i++)fr2[i]=0x00ABCDEFu^(i*2654435761u&0x00FFFFFFu);
      u64 sva=0; const void* sptr=0; memset(VR,0,4096*600);
      int a1=nv2d_to_screen(fr2,fw,(int)fw,(int)fh,30,40,1,&sva,&sptr); int same=1; for(u32 y=0;y<fh&&same;y++) for(u32 x=0;x<fw;x++) if((((u32*)VR)[(40+y)*1024+30+x]&0xFFFFFFu)!=(fr2[y*fw+x]&0xFFFFFFu)){ same=0; break; }
      int a2=nv2d_to_screen(fr2,fw,100,50,500,300,2,&sva,&sptr); int same2=1; for(u32 y=0;y<100&&same2;y++) for(u32 x=0;x<200;x++) if((((u32*)VR)[(300+y)*1024+500+x]&0xFFFFFFu)!=(fr2[(y/2)*fw+x/2]&0xFFFFFFu)){ same2=0; break; }
      int out=nv2d_to_screen(fr2,fw,(int)fw,(int)fh,1000,40,1,&sva,&sptr);
      chk("2D engine puts a 200x100 frame on the screen 1:1 and 100x50 scaled 2x, and refuses a rectangle off the screen",a1&&same&&a2&&same2&&!out); free(fr2); }
    { nvkern k; int p1=nvc_parse(g_gqk_cubin,g_gqk_cubin_len,&k);
      chk("compiled shader: sm_75 cubin parsed, 44 registers, parameters at c[0][0x160] (136 bytes), jump table in bank 2",p1&&k.regs==44&&k.pofs==0x160&&k.psz==136&&k.cbl[2]==16&&k.code_len==0x1D80&&k.cbl[0]==0x1E8&&!k.bars);
      static u8 bad[16384]; memcpy(bad,g_gqk_cubin,g_gqk_cubin_len); bad[0x12]=62; int n1=nvc_parse(bad,g_gqk_cubin_len,&k); memcpy(bad,g_gqk_cubin,g_gqk_cubin_len); bad[0x30]=0x56; bad[0x32]=0x56; int n2=nvc_parse(bad,g_gqk_cubin_len,&k); int n3=nvc_parse(g_gqk_cubin,0x30,&k);
      chk("shader loader refuses a non-CUDA file, a cubin for another GPU and a cut-off file",!n1&&!n2&&!n3&&k.why[0]); }
    { nvl_reset(); int r18=compute(); u32 ops=0,subs=0,waits=0,to=0; nvc_stats(&ops,&subs,&waits,&to);
      chk("step 18: shader in VRAM, QMD v2.2 per operation, 400 random drawing operations (those on screen) match the CPU exactly",r18==1&&nvc_ready()&&cp_launches>=300&&!cp_bad&&!cp_cbad&&(u32)cp_launches==ops&&to==0);
      chk("dependent operations get a wait-for-idle, independent ones run back to back",cp_wfi>0&&waits>0&&waits<ops);
      int vram_default_off=(g_nvd_vram==0); g_nvd_vram=1; int d=nvd_desktop(); int okd=d==0&&g_nvd_on&&g_gfx==GFX_GPU&&sim_gfx_starts==1&&sim_gfx_sid>0;
      int s0=0; u64 w0=okd?walk(g_uv.pd3,g_gs[sim_gfx_sid].gfx+304*4*199,&s0):~0ULL;
      gq_grad(sim_gfx_sid,0,0,300,200,0x00102030u,0x00F0E0D0u,1); gq_blend(sim_gfx_sid,20,20,100,60,0x00FFFFFFu,128); int sy=gq_sync();
      u32 want=gq_bl(gq_bl(0x00102030u,0x00F0E0D0u,(u32)((40u*((256u<<8)/200u))>>8)),0x00FFFFFFu,128);
      chk("desktop drawing switches to the NVIDIA GPU: buffers mapped for the GPU, a gradient and a blend drawn by the shader",okd&&s0&&w0==(u64)(sim_gfx_px+304*199)&&!sy&&sim_gfx_px[40*304+50]==want&&sim_gfx_px[199*304+299]==gq_bl(0x00102030u,0x00F0E0D0u,(u32)((199u*((256u<<8)/200u))>>8)));
      { static u8* hb=0; if(!hb)hb=mmap(0,G.bar1_len,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANONYMOUS|MAP_NORESERVE,-1,0); sim_b1_cpu=(u64)hb;
        u32* vp=0; int vs=g_gfx_alloc_gpu?g_gfx_alloc_gpu(64,32,64,&vp):0; int vy=0, same=0, drawn=0; if(vs>0){ gq_fill(vs,0,0,64,32,0x00336699u); vy=!gq_sync(); }
        if(vs>0&&vp){ int a1=0,a2=0; u64 bo=(u64)vp-sim_b1_cpu; u64 p1=walk(G.bar1_pde,bo+64*4*31,&a1), p2=walk(g_uv.pd3,g_gs[vs].gfx+64*4*31,&a2); same=p1!=~0ULL&&p1==p2&&!a1&&!a2; drawn=same&&*(u32*)(VR+p2+63*4)==0x00336699u; }
        u32 vn=0; u64 vb=0; nvgsp_vram_stats(&vn,&vb,0); sim_b1_cpu=0;
        chk("card memory stays off while its setting is off (a CPU fallback must never touch PCIe memory)",vram_default_off);
        chk("GPU-only work buffers go into the card's memory: BAR1 and the GPU map the same VRAM pages, and the shader's fill lands there",vs>0&&vp&&g_nvd_vram_n==1&&vn==1&&vb>=64*32*4&&vy&&same&&drawn); }
      cp_hang=1; gq_fill(sim_gfx_sid,0,0,10,10,1); u64 t0=hr_ns(); int hs=gq_sync(); u64 dt=hr_ns()-t0; cp_hang=0; u32 to2=0; nvc_stats(0,0,0,&to2);
      chk("a compute batch that never finishes: 500 ms timeout, GPU drawing stops with a reason, nothing hangs",hs!=0&&to2==1&&!nvc_ready()&&nvc_why()[0]&&dt<2000000000ULL&&g_gq_mode==GQM_OFF);
      { int held=!ch_ready(0)&&!gr_begin(); g_cpsem[0]=g_hung_v[0]; int freed=ch_ready(0); chk("after a timeout the channel takes no new work until the old batch has finished (its push buffer is not overwritten)",held&&freed); }
      gfx_stop("test"); chk("stopping GPU drawing releases the NVIDIA path",!g_nvd_on&&g_gfx==0);
      { int fs=sim_gfx_sid; u64 va=g_gs[fs].gfx; int x0=0; u64 before=walk(g_uv.pd3,va+4096*7,&x0); u32 f0=g_nvd_freed; gq_surf_free(fs); int x1=0; u64 after=walk(g_uv.pd3,va+4096*7,&x1);
        chk("a freed GPU drawing surface gives its memory back and its GPU page-table entries are cleared (no leak per GPU start, no stale mapping)",before!=~0ULL&&after==~0ULL&&g_nvd_freed==f0+1&&!nd_s[fs].raw); } }
    { extern void (*g_nvl_hook)(const char*); nvl_reset(); g_nvl_hook=run_hook; g_run_on=1; g_run_t0=g_step_t0=hr_ns(); g_run_step=0; g_run_dead=0; g_run_boot0=REG[0]; g_run_cmd="all"; g_saved=0;
      nvl_s("[16] screen through the copy engine"); nvl_nl(); int sv=g_saved; int head=strncmp(g_logout,"nvgsp all - RESULT: STOPPED during step 16: screen",50)==0;
      u32 keep=REG[0]; REG[0]=0xFFFFFFFFu; nvl_s("[17] 2D engine"); nvl_nl(); REG[0]=keep; int dead=g_run_dead==1&&run_check()<0&&strstr(g_nv_log,"the card stopped answering");
      g_run_on=0; g_nvl_hook=0;
      chk("start-up log is saved at every step with a STOPPED headline, and a card that stops answering ends the run",sv>0&&head&&dead&&strstr(g_nv_log,"(step 16 took")!=0); }
    { g_gfx=0; g_nvd_auto=0; nvc_set_ok(1); nvd_request(); nvd_poll(); int on=g_nvd_on&&g_gfx==GFX_GPU; g_gfx_frames++; g_gfx_frame_us=400000; nvd_poll(); int still=g_gfx==GFX_GPU; g_gfx_frames++; g_gfx_frame_us=200000; nvd_poll();
      chk("GPU drawing starts from the main loop, not inside the key press, and a slow first frame (after the first) turns it off",on&&still&&g_gfx==0&&!g_nvd_on&&strstr(g_gfx_why,"too slow")); }
    { g_gfx=0; g_nvd_auto=0; nvc_set_ok(1); nvd_request(); nvd_poll(); g_gfx_frames++; g_gfx_frame_us=1000; for(int k=0;k<6;k++){ g_gfx_frames++; nvd_poll(); } int on=g_nvd_on&&g_gfx==GFX_GPU;
      g_gq_mode=GQM_OFF; nvd_poll(); int stop1=!g_nvd_on&&g_gfx==0&&strstr(g_gfx_why,"batch failed");
      nvc_set_ok(1); nvd_request(); nvd_poll(); for(int k=0;k<6;k++){ g_gfx_frames++; g_gfx_frame_us=1000; nvd_poll(); } int on2=g_nvd_on&&g_gfx==GFX_GPU;
      for(int k=0;k<3;k++){ g_gfx_frames++; g_gfx_frame_us=300000; nvd_poll(); } int stop2=!g_nvd_on&&g_gfx==0&&strstr(g_gfx_why,"too slow");
      chk("watchdog: a failed GPU batch or three frames over 150 ms hand the desktop back to the CPU at once",on&&stop1&&on2&&stop2); g_gfx_frame_us=1000; }
    nvvram_tests();
    { g_nvd_auto=1; msd_ok=1; ufat_ok=1; ufs_n=1; strcpy(ufs[0].name,"NVSTART.TXT"); sim_uflag='1'; g_ticks=1000; nvd_poll(); g_ticks=2000; int ln=g_nv_log_n; nvd_poll();
      chk("no automatic start after boot when the last start-up never finished (flag on the USB stick)",g_nvd_auto_skipped==1&&g_nv_log_n==ln); msd_ok=0; ufat_ok=0; ufs_n=0; }
    { nd_auto_done=0; nd_t_desk=0; g_nvd_auto_skipped=0; g_nvd_boot=0; g_nvd_auto=1; msd_ok=1; ufat_ok=1; ufs_n=1; strcpy(ufs[0].name,"NVSTART.TXT"); sim_uflag='1'; int ln=g_nv_log_n; nvd_boot_start();
      g_ticks=5000; nvd_poll(); g_ticks=9000; nvd_poll(); int guard=g_nvd_auto_skipped==1&&g_nvd_boot==3&&g_nv_log_n==ln;
      nd_auto_done=0; nd_t_desk=0; sim_uflag='0'; g_nv_dev=0x1C82; g_usbw=0; nvd_boot_start(); int nonturing=g_nvd_boot==3&&g_usbw==0&&g_nv_log_n==ln; g_nv_dev=0x21C4;
      nd_auto_done=0; nd_t_desk=0; sim_drv_skip=1; g_nvd_boot=0; nvd_boot_start(); int skipdrv=g_nvd_boot==0&&!nd_auto_done; sim_drv_skip=0;
      nvd_boot_start(); int ran=g_nvd_boot==2&&nd_auto_done&&g_usbw==2&&strstr(g_nv_log,"already running")!=0; int lnr=g_nv_log_n;
      g_gfx=0; nvc_set_ok(1); nvd_request(); g_ticks=20000; nvd_poll(); int first=g_nvd_on&&g_gfx==GFX_GPU; g_ticks=40000; nvd_poll(); int once=g_nv_log_n==lnr; nvd_halt("test");
      chk("boot splash start: guard flag skips it, no card/non-Turing or a driver turned off by crash recovery touches nothing",guard&&nonturing&&skipdrv);
      chk("boot splash start runs nvgsp all once; the first desktop poll draws on the GPU, no second start",ran&&first&&once); msd_ok=0; ufat_ok=0; ufs_n=0; sim_uflag=0; }
    { nvgsp_cmd("present on"); int on0=nvgsp_present_on(); int lr=nvgsp_cmd("all"); int still=nvgsp_present_on()&&strstr(g_nv_log,"already running")!=0;
      u8 rc[16]; memset(rc,0,sizeof rc); *(u32*)rc=1; *(u32*)(rc+4)=g_ch[1].chid+100; *(u32*)(rc+8)=31; msg_post(0x1004,rc,16,0); nvgsp_tick(); int foreign=nvgsp_present_on()&&g_rc_other==1;
      int nch0=NCH, fr0=sim_chan_freed, gr0=chan_find(HCH(g_ch[0].chid)); u32 cp0=g_ce_copies, oc0=g_off_count;
      u32 och1=g_ch[1].chid, och0=g_ch[0].chid;
      *(u32*)(rc+4)=g_ch[1].chid; msg_post(0x1004,rc,16,0); nvgsp_tick(); int c1=chan_find(HCH(g_ch[1].chid)); u32 nch1=g_ch[1].chid;
      int rec1=g_rc_n==1&&g_rc_fixed==1&&g_rc_last==1&&!g_rc_dead&&sim_chan_freed==fr0+1&&NCH==nch0+1&&c1==nch0&&CH[c1].sched&&CH[c1].ncls==1&&CH[c1].cls[0]==0xC5B5&&CH[c1].inst==g_ch[1].inst&&chan_find(HCH(g_ch[0].chid))==gr0;
      int back1=!nvgsp_off()&&nvgsp_ce_ready()&&nvgsp_present_on()&&nv2d_ready()&&nvc_ready()&&g_off_count==oc0+1; int cw=nvgsp_ce_copy2d(g_sc.va,64,g_sc.va+0x1000,64,64,1)&&g_ce_copies==cp0+1;
      chk("a channel error on a channel that is not ours is ignored (like nouveau's per-channel handling)",foreign);
      chk("fault injection: a channel error on our copy-engine channel frees only that channel, makes it again (new channel in GSP-RM, copy object, copy test) and turns NVIDIA drawing back on",on0&&lr==1&&still&&rec1&&back1&&cw);
      u32 cpo=cp_launches; *(u32*)(rc+4)=g_ch[0].chid; msg_post(0x1004,rc,16,0); nvgsp_tick(); int c0=chan_find(HCH(g_ch[0].chid));
      int rec0=g_rc_n==2&&g_rc_fixed==2&&g_rc_last==0&&c0==nch0+1&&CH[c0].promoted&&CH[c0].ncls==3&&chan_find(HCH(g_ch[1].chid))==c1&&!nvgsp_off()&&nv2d_ready()&&nvc_ready()&&cp_launches>cpo;
      chk("fault injection: a channel error on the graphics channel remakes it (context promoted, 3D, compute, 2D objects) and the 2D engine and compute shaders pass their checks again",rec0);
      u32 nch0c=g_ch[0].chid;
      chk("recovery takes a fresh channel id (not the freed one, not the other channel's) and old handles are gone",nch1!=och1&&nch1!=och0&&nch0c!=och0&&nch0c!=nch1&&nch0c>=NV_CHID_LO&&nch0c<NV_CHID_END&&chan_find(HCH(och1))<0&&chan_find(HCH(och0))<0);
      int nvu3=g_nvu; u64 van3=g_va_next; int rb=chan_rebuild(1)&&chan_rebuild(0); g_gr_busy=0; g_gpn=0; g_2d_init=0; g_2d_ok=0; g_cpn=0; g_cpi=0; int nvu4=g_nvu; u64 van4=g_va_next;
      rb=rb&&chan_rebuild(0)&&chan_rebuild(1); g_gr_busy=0; g_gpn=0; g_2d_init=0; g_2d_ok=0; g_cpn=0; g_cpi=0; int leak=g_nvu!=nvu3||g_va_next!=van3||nvu4!=nvu3||van4!=van3;
      { char mm[120]; nvgsp_all_off("test"); nvgsp_on(mm,120); }
      chk("recovery leaks nothing: VRAM allocation count and GPU VA cursor stay the same over repeated copy and graphics channel rebuilds (context buffers and copy-test pages reused)",rb&&!leak&&!nvgsp_off()&&nv2d_ready()&&nvc_ready());
      msg_post(0x1005,rc,0,0); nvgsp_tick(); int pf=g_pf_n==1&&g_rc_n==2&&!nvgsp_off()&&nvgsp_present_on();
      chk("a queued GPU page fault is logged like nouveau's r535_gsp_msg_mmu_fault_queued and changes nothing by itself",pf);
      *(u32*)(rc+4)=g_ch[1].chid; msg_post(0x1004,rc,16,0); nvgsp_tick(); int third=g_rc_fixed==3&&!nvgsp_off();
      int fr1=sim_chan_freed; *(u32*)(rc+4)=g_ch[1].chid; msg_post(0x1004,rc,16,0); nvgsp_tick(); char m0[120]; int ref=nvgsp_on(m0,120)==0&&strstr(m0,"stopped one of our GPU channels")!=0;
      chk("only after more than 3 channel errors does GPU drawing fall back to the CPU for good (no new channel; nvgsp on refuses)",third&&g_rc_n==4&&g_rc_dead&&nvgsp_off()&&strstr(nvgsp_off_why(),"too many")&&sim_chan_freed==fr1&&!nvgsp_ce_ready()&&!nvc_ready()&&!nv2d_ready()&&ref);
      g_rc_dead=0; g_rc_n=0; nvgsp_on(m0,120); sim_refuse_cls=0xC46F; *(u32*)(rc+4)=g_ch[1].chid; msg_post(0x1004,rc,16,0); nvgsp_tick(); sim_refuse_cls=0;
      int failed=g_rc_failed==1&&g_rc_dead&&nvgsp_off()&&strstr(nvgsp_off_why(),"could not be made again")&&nvgsp_on(m0,120)==0;
      g_nv_log_n=0; g_nv_log[0]=0; nvgsp_status(); int st=strstr(g_nv_log,"channels freed and made again")!=0&&strstr(g_nv_log,"stays on the CPU")!=0;
      int fix=chan_rebuild(1)&&chan_find(HCH(g_ch[1].chid))>=0;
      chk("if GSP-RM refuses the new channel, NVIDIA drawing stays off with a reason, nvgsp status shows the recovery counts, and the channel can be made again later",failed&&st&&fix); }
    { g_rc_dead=0; }
    { char m[120]; int was=nvgsp_off(); int r1=nvgsp_on(m,120); int back=!nvgsp_off()&&nvgsp_ce_ready()&&nvgsp_present_on()&&nv2d_ready()&&nvc_ready()&&!nvgsp_off_why()[0];
      int r2=nvgsp_on(m,120); int again=r2==1&&strstr(m,"already on");
      chk("Ctrl+Alt+N / nvgsp on turns NVIDIA drawing back on without a restart: copy engine, 2D engine and compute shaders checked again",was&&r1==1&&back&&again);
      nvgsp_all_off("turned off with Ctrl+Alt+N"); u64 age=nvgsp_off_age_s(); g_nv_log_n=0; g_nv_log[0]=0; nvgsp_status(); int st=strstr(g_nv_log,"NVIDIA drawing: OFF - turned off with Ctrl+Alt+N")!=0;
      nvgsp_on(m,120); u32 keep=REG[0]; g_tick_live=0; nvgsp_tick(); int alive=!nvgsp_off(); REG[0]=0xFFFFFFFFu; g_tick_live=0; nvgsp_tick(); int gone=nvgsp_off()&&strstr(nvgsp_off_why(),"0xFFFFFFFF"); char m2[120]; int ron=nvgsp_on(m2,120); REG[0]=keep;
      chk("off reason and time in nvgsp status; liveness like Linux: only a 0xFFFFFFFF or wrong BOOT0 read (twice) means the card is gone, and nvgsp on refuses then",st&&age<5&&alive&&gone&&ron==0&&strstr(m2,"does not answer"));
      nvgsp_on(m,120);
      nv2d_reset(); g_auto_at=0; g_auto_tries=0; sc_off("test: read-back mismatch"); int sched=g_auto_at==0&&!nvgsp_present_on(); /* v11.03: no automatic re-check is scheduled */ g_last_input_t=g_ticks; g_auto_at=1; nvgsp_tick(); int waited=g_auto_tries==0&&g_auto_at>1&&!nvgsp_present_on(); g_last_input_t=0; g_ticks+=1000; g_auto_at=1; nvgsp_tick(); int back2=nvgsp_present_on()&&g_auto_tries==1&&!nvgsp_off();
      g_auto_tries=3; sc_off("test again"); int capped=g_auto_at==0; nvgsp_on(m,120); if(!nvgsp_present_on())nvgsp_present_set(1);
      chk("a GPU timeout or bad read-back schedules one automatic re-check (only while input is idle) that brings the path back",sched&&waited&&back2&&capped); }
    { int a0=disp_alloc; nvl_reset(); int rm=nvgsp_monitors(); const nvgsp_mon_t* ml=0; u32 hh=0,hm=0; int mn=nvgsp_mon_get(&ml,&hh,&hm,0);
      int good=rm==1&&mn==3&&hh==4&&hm==0xF&&disp_alloc==a0+1&&ml[0].connected&&ml[0].edid_ok&&!strcmp(ml[0].name,"SIM MONITOR")&&!strcmp(ml[0].maker,"DEL")&&ml[0].nmodes==1&&ml[0].mw[0]==1920&&ml[0].mh[0]==1080&&ml[0].mhz[0]==60
        &&!strcmp(nvgsp_mon_sig(&ml[0]),"HDMI")&&!ml[1].connected&&!strcmp(nvgsp_mon_sig(&ml[1]),"DisplayPort")&&strstr(g_nv_log,"1 monitor(s) connected")!=0;
      int again=nvgsp_monitors()==1&&disp_alloc==a0+2&&disp_inst_n==1;
      chk("monitors like nouveau's r535_disp_oneinit: display instance memory, NV04_DISPLAY_COMMON, connected outputs, HDMI/DP type and EDID name and native mode",good&&again); }
    { nvl_reset(); int b0=s3_bad; int r3=nv3d_selftest(); if(s3_bad)printf("    3D model: %s\n",s3_why);
      chk("3D engine: state from NVIDIA's class header, block-linear colour and ZF32 depth targets (memory kind 6), shaders run by the model, depth order, read back through the 2D engine",r3==1&&nv3d_ready()&&s3_bad==b0&&s3_clears==1&&s3_draws==1&&s3_verts==9&&s2d_blbad==0);
      nv2d_surf sc; int hs=nv2d_screen(&sc); int ts=hs?nv3d_to_screen(100,50,2):0; u8* q=hs?vptr(g_uv.pd3,sc.va+(u64)(50+2*58)*sc.pitch+(u64)(100+2*6)*4):0; u8* q2=hs?vptr(g_uv.pd3,sc.va+(u64)(50+2*4)*sc.pitch+(u64)(100+2*40)*4):0;
      chk("3D picture goes from the block-linear target to the screen through the 2D engine, scaled 2x",hs&&ts&&q&&q2&&(*(u32*)q&0xFFFFFFu)==0x00FF00u&&(*(u32*)q2&0xFFFFFFu)==0x4080BFu&&s2d_blbad==0);
      u32 v0=s3_verts, sb0=0, sb1=0, tr0=0, tr1=0; nv3d_stats(0,&tr0,&sb0,0,0); int bg=nv3d_begin(320,200,0,0,0); unsigned sd=7; float t[24];
      for(int i=0;i<20000&&bg;i++){ sd=sd*1103515245u+12345u; float cx=(float)((sd>>8)%2000)/1000.0f-1.0f, cy=(float)((sd>>19)%2000)/1000.0f-1.0f, z=(float)((sd>>4)%1000)/1000.0f;
          for(int k=0;k<3;k++){ float* d=t+k*8; d[0]=cx+(k==1?0.02f:0); d[1]=cy+(k==2?0.02f:0); d[2]=z; d[3]=1.0f; d[4]=(float)(i&255)/255.0f; d[5]=(float)k*0.5f; d[6]=z; d[7]=1.0f; } nv3d_tri(0,t); }
      int en=bg?nv3d_end():0; nv3d_stats(0,&tr1,&sb1,0,0);
      chk("20000 triangles in one frame: the vertex buffer goes in two parts and every vertex reaches the engine",en==1&&s3_verts==v0+60000u&&sb1==sb0+2&&tr1==tr0+20000u&&s3_bad==b0&&s3_clears==2);
      s3_nodepth=1; nvl_reset(); int rb=nv3d_selftest(); s3_nodepth=0; int off=rb==0&&!nv3d_ready()&&strstr(nv3d_why(),"did not match")!=0&&strstr(g_nv_log,"3D stays off")!=0; int rg=nv3d_selftest();
      chk("a card that ignores the depth test fails the 3D self-test (3D stays off); a correct one passes again",off&&rg==1&&nv3d_ready()&&s3_bad==b0); }
    { for(u32 y=0;y<600;y++) for(u32 x=0;x<1024;x++) *(u32*)(VR+(u64)y*4096+x*4)=0xFF000000u|(y<<12)|x;
      int cb0=ctrl_bad, ab0=alloc_bad;
      nvl_reset(); int one=nvgsp_mode(0); int one_ok=one==0&&strstr(g_nv_log,"only one monitor")!=0&&KD.up;
      nvgsp_mode_lost(); rm_free(HC(2),HC(2)); sim_two=1;
      sim_refuse_cls=0xC57D; nvl_reset(); int rf=nvgsp_mode(0); int rf_ok=rf==0&&strstr(KD.why,"core display channel")!=0&&!KD.up&&!disp_root; int ab1=alloc_bad; sim_refuse_cls=0;
      sim_proto2=8; nvl_reset(); int dp=nvgsp_mode(0); int dp_ok=dp==0&&strstr(g_nv_log,"link training is off")!=0; sim_proto2=2;
      chk("second screen refused cleanly: one monitor only, core channel refused by GSP-RM (client freed), DisplayPort monitor",one_ok&&rf_ok&&dp_ok&&ctrl_bad==cb0&&alloc_bad==ab1&&ab1==ab0);
      nvl_reset(); int r1=nvgsp_mode(0); int sk=0; u32 px=sim_scan(1,700,333,&sk); u32 H1=0x400;
      int ras=KC_ARM[(0x2064+H1)/4]==(1688u|(1066u<<16))&&KC_ARM[(0x2068+H1)/4]==(111u|(2u<<16))&&KC_ARM[(0x206C+H1)/4]==(359u|(40u<<16))&&KC_ARM[(0x2070+H1)/4]==(1639u|(1064u<<16))&&KC_ARM[(0x200C+H1)/4]==108000000u&&KC_ARM[(0x2028+H1)/4]==108000000u;
      int rt=KC_ARM[(0x2020+H1)/4]==0x400u&&KC_ARM[0x320/4]==((1u<<1)|(2u<<8))&&KC_ARM[0x1100/4]==1u&&disp_brightc>=1&&KC_ARM[(0x2004+H1)/4]==((4u<<4)|(0x3Fu<<26))&&KD.head[1].or==1&&KD.head[1].id==0x400u;
      int vp=KC_ARM[(0x204C+H1)/4]==(1024u|(600u<<16))&&KC_ARM[(0x2058+H1)/4]==(1280u|(750u<<16));
      u32 lf=0; u64 vs=sim_ramht(0,KC_ARM[(0x2288+H1)/4],&lf,0); u64 lut=(u64)KC_ARM[(0x228C+H1)/4]<<8; int lu=vs==0&&(lf&0x47u)==0x45u&&lut&&*(u16*)(VR+lut+0x20+512*8)==512*64&&*(u16*)(VR+lut+0x20+512*8+2)==512*64&&*(u16*)(VR+lut+0x20+512*8+4)==512*64&&*(u16*)(VR+lut+0x20+1024*8)==1023*64&&((KC_ARM[(0x2280+H1)/4]>>8)&0x7FFu)==1029u;
      int h0=KC_ARM[0x2064/4]==0&&KC_ARM[0x300/4]==0&&KC_ARM[0x1000/4]==0;
      chk("second screen: display object, core and window channels, SOR from GSP-RM, 1280x1024 raster and 108 MHz clock from the EDID, head 0 left alone",r1==1&&ras&&rt&&h0&&disp_bad==0&&disp_ntfy==2&&ctrl_bad==cb0&&alloc_bad==ab0);
      chk("second screen: hash table and DMA objects found by a hardware-style lookup, window scans the desktop picture, viewport 1024x600 fitted to 1280x750, identity colour table",sk&&px==(0xFF000000u|(333u<<12)|700u)&&vp&&lu);
      int ca=disp_chan_alloc; nvl_reset(); int r2=nvgsp_mode(0); nvgsp_monitors(); const nvgsp_mon_t* ml=0; int mn=nvgsp_mon_get(&ml,0,0,0); int mk=0; for(int i=0;i<mn;i++) if(ml[i].id==0x400u&&ml[i].lit==2&&ml[i].head==1&&ml[i].act_w==1280&&ml[i].act_h==1024)mk=1;
      nvgsp_mode_t mi; int mc=nvgsp_mode_info(&mi);
      chk("second screen: asking again changes nothing, monitor list and mode info show head 1 at 1280x1024",r2==1&&disp_chan_alloc==ca&&mk&&mc==1&&mi.h[0].head==1&&mi.h[0].w==1280&&mi.h[0].ow==1280&&mi.h[0].oh==750&&mi.h[0].sor==1&&disp_bad==0);
      nvl_reset(); int of=nvgsp_mode_off(); int off_ok=of==1&&KC_ARM[0x320/4]==0&&KW_ARM[2][0x240/4]==0&&KC_ARM[0x1100/4]==1u&&KC_ARM[(0x2020+H1)/4]==0&&KD.head[1].or<0&&nvgsp_mode_off()==0;
      sim_hdmi2=1; disp_hdmi_en=0; nvl_reset(); int r3=nvgsp_mode(0); u8 fr[17]; u32 hd=REG[0x6F0408/4], l0=REG[0x6F040C/4], h0r=REG[0x6F0410/4], l1=REG[0x6F0414/4], h1r=REG[0x6F0418/4];
      fr[0]=(u8)hd; fr[1]=(u8)(hd>>8); fr[2]=(u8)(hd>>16); fr[3]=(u8)l0; fr[4]=(u8)(l0>>8); fr[5]=(u8)(l0>>16); fr[6]=(u8)(l0>>24); fr[7]=(u8)h0r; fr[8]=(u8)(h0r>>8); fr[9]=(u8)(h0r>>16); fr[10]=(u8)l1; fr[11]=(u8)(l1>>8); fr[12]=(u8)(l1>>16); fr[13]=(u8)(l1>>24); fr[14]=(u8)h1r; fr[15]=(u8)(h1r>>8); fr[16]=(u8)(h1r>>16);
      u8 cs=0; for(int i=0;i<17;i++)cs=(u8)(cs+fr[i]); int hm=r3==1&&disp_hdmi_en==1&&disp_sink_caps==1&&(fr[4]&0x10)&&(REG[0x6F0400/4]&1u)&&fr[0]==0x82&&fr[1]==2&&fr[2]==13&&cs==0&&(fr[6]&0x0C)==0x08&&KD.head[1].hdmi==1;
      int of2=nvgsp_mode_off()==1&&disp_hdmi_en==1&&!(REG[0x6F0400/4]&1u); sim_hdmi2=0;
      chk("second screen: off clears the output, window and display id; an HDMI monitor gets HDMI mode and a valid AVI InfoFrame (full-range RGB)",off_ok&&hm&&of2&&disp_bad==0);
      nvl_reset(); int r4=nvgsp_mode(1); int s0=0, s1=0; u32 p0=sim_scan(0,10,599,&s0), p1=sim_scan(1,1023,0,&s1);
      int all_ok=r4==1&&KC_ARM[0x2064/4]==(2200u|(1125u<<16))&&KC_ARM[0x2068/4]==(43u|(4u<<16))&&KC_ARM[0x206C/4]==(191u|(40u<<16))&&KC_ARM[0x2070/4]==(2111u|(1120u<<16))&&KC_ARM[0x200C/4]==148500000u&&KC_ARM[0x300/4]==((1u<<0)|(1u<<8))&&KC_ARM[0x2020/4]==0x100u
        &&KC_ARM[0x2058/4]==(1842u|(1080u<<16))&&s0&&p0==(0xFF000000u|(599u<<12)|10u)&&s1&&p1==(0xFF000000u|1023u)&&KD.head[0].or==0&&KD.head[1].or==1;
      chk("nvgsp mode all: the first monitor is set to its own 1920x1080 mode on its own output as well, both heads scan the desktop",all_ok&&disp_bad==0);
      int lp=1; for(int k=0;k<70&&lp;k++){ if(nvgsp_mode_off()!=1||nvgsp_mode(1)!=1)lp=0; } int s2=0; u32 p2=sim_scan(1,5,5,&s2);
      chk("70 off/on rounds: command buffers wrap with a jump, every update finishes, hash table does not grow",lp&&disp_jumps>=2&&disp_bad==0&&s2&&p2==(0xFF000000u|(5u<<12)|5u)&&KD.nobj==8&&disp_chan_alloc==ca+1);
      nvgsp_mode_off(); sim_no_ntfy=1; nvl_reset(); u32 t0=KD.timeouts; int r5=nvgsp_mode(0); sim_no_ntfy=0; int w5=strstr(KD.why,"did not finish")!=0; int r6=nvgsp_mode(0);
      chk("an update the display engine never finishes is reported after 3 s and the next try works",r5==0&&KD.timeouts==t0+1&&w5&&r6==1&&KD.head[1].or>=0&&disp_bad==0);
      *(u32*)(g_bootinfo+8)=4100; nvgsp_mode_off(); nvl_reset(); int r7=nvgsp_mode(0); *(u32*)(g_bootinfo+8)=4096; *(u64*)g_bootinfo=0x1000; int r8=nvgsp_mode(0); *(u64*)g_bootinfo=BAR1;
      chk("a desktop line length the display engine cannot scan, or a desktop that is not in NVIDIA memory, is refused",r7==0&&r8==0&&strstr(KD.why,"another GPU")!=0&&disp_bad==0);
      unsigned sd=12345; long acc=0, hdm=0; int fz=1;
      for(int it=0;it<300000&&fz;it++){ u8 e[256]; for(int i=0;i<256;i++){ sd=sd*1103515245u+12345u; e[i]=(u8)(sd>>16); } if(it&1){ e[126]=1; e[128]=2; e[130]=(u8)(4+(sd>>8)%124); } if(it%3==0){ e[54]=1; e[71]&=0x7F; }
          kmode m; int n=(it%5==0)?(int)((sd>>4)%257):256; if(nvk_edid_mode(e,n,&m)){ acc++; u32 r[4]; nvk_mode_regs(&m,r); u32 ow=0,oh=0; nvk_fit(1+(sd%4000),1+((sd>>12)%4000),m.hact,m.vact,&ow,&oh);
              if(m.hact<320||m.vact<200||!m.hsw||!m.vsw||(r[0]&0xFFFF)<=m.hact||(r[0]>>16)<=m.vact||(r[3]&0xFFFF)>=(r[0]&0xFFFF)+m.hsw+m.hbp||ow>m.hact||oh>m.vact||!ow||!oh)fz=0; }
          hdm+=nvk_edid_hdmi(e,n); }
      chk("300000 random EDID blocks: the timing and HDMI parsers stay inside the block and only accept sane modes",fz&&acc>0);
      { int cb=ctrl_bad, ab=alloc_bad, db=disp_bad; u64 ft0=hr_ns(); int n1=0;
        for(int it=0;it<1500;it++){ sim_fuzz=1+(it%15); sim_hdmi2=it&1; sim_proto2=(it%7==0)?8:2; int r=nvgsp_mode(it%3==0); if(r)n1++; if(it%4==1)nvgsp_mode_off(); if(it%9==0)nvgsp_monitors(); if(it%50==49){ nvgsp_mode_lost(); sim_fuzz=0; rm_free(HC(2),HC(2)); } }
        sim_fuzz=0; sim_hdmi2=0; sim_proto2=2; nvgsp_mode_lost(); rm_free(HC(2),HC(2)); ctrl_bad=cb; alloc_bad=ab; disp_bad=db; nvl_reset(); int back=nvgsp_mode(0);
        chk("1500 rounds with random bytes and random refusals in GSP-RM's display replies: no crash, no timeout, and a clean start works afterwards",back==1&&hr_ns()-ft0<60000000000ULL&&n1>0);
        sim_two=0; nvl_reset(); int gone=nvgsp_mode(0); chk("a second monitor that was unplugged: its head is turned off at the next nvgsp mode",gone==0&&KD.head[1].or<0&&KC_ARM[0x320/4]==0&&strstr(g_nv_log,"is gone")!=0); sim_two=1; }
      sim_two=0; }
    { nvgsp_sens_t ss; NS.at=0; NS.no=0; sens_n=0; sens_bad=0; int up=nvgsp_sensors(&ss); int n1=sens_n; nvgsp_sens_t s2; nvgsp_sensors(&s2); int cached=sens_n==n1;
      int vals=up==1&&ss.up&&ss.link&&ss.link_gen==3&&ss.link_width==16&&ss.max_gen==3&&ss.max_width==16&&ss.pst&&ss.aux_pstate==2&&ss.hcs&&ss.hostclk_slow_off==1&&!ss.why[0];
      const char* tx=nvgsp_sens_text(); int txt=strstr(tx,"gen 3 (8 GT/s) x16")&&strstr(tx,"P2")&&strstr(tx,"disabled")&&strstr(tx,"535.113.01");
      chk("GPU sensors: BUS_GET_INFO_V2 (420 bytes, PCIe caps + link status), AUX_POWER_STATE and HOSTCLK_SLOWDOWN in their 535.113.01 layouts, decoded, one read per second",vals&&cached&&n1==3&&sens_bad==0&&txt);
      NS.at=0; NS.no=0; sens_refuse=1; nvgsp_sensors(&ss); int r1=sens_n; NS.at=0; nvgsp_sensors(&ss); int r2=sens_n; sens_refuse=0; int asked_once=(r1-n1)==3&&(r2-r1)==2&&!ss.pst&&ss.link&&strstr(nvgsp_sens_text(),"refused by GSP-RM");
      g_rpc_dead=1; int r3=sens_n; NS.at=0; int dn=nvgsp_sensors(&ss); int quiet=dn==0&&!ss.up&&strstr(ss.why,"stopped answering")&&sens_n==r3&&strstr(nvgsp_sens_text(),"not available"); g_rpc_dead=0;
      int st0=G.state; G.state=1; NS.at=0; int dn2=nvgsp_sensors(&ss); int quiet2=dn2==0&&sens_n==r3&&strstr(ss.why,"not running"); G.state=st0;
      chk("GPU sensors: a refused control is asked once and shown as refused; nothing is sent when GSP-RM is stopped or not answering",asked_once&&quiet&&quiet2&&sens_bad==0);
      int fz=1; sens_fuzz=1; for(int it=0;it<3000&&fz;it++){ NS.at=0; if(it%40==0)NS.no=0; nvgsp_sensors(&ss); const char* t=nvgsp_sens_text(); if(strlen(t)>=sizeof(NS.txt)||ss.link_gen>15||ss.link_width>63||ss.max_gen>15||ss.max_width>63||ss.hostclk_slow_off>1)fz=0; } sens_fuzz=0; NS.at=0; NS.no=0;
      chk("GPU sensors: 3000 random sensor replies and refusals stay inside their fields and the text buffer",fz&&!g_rpc_dead); }
    { u32 pb[32]; for(int i=0;i<32;i++)pb[i]=0xDEADBEEFu; u32 n=nvflip_emit_flip(pb,32,0x12345u,1u,1,NVFLIP_HSYNC,NVFLIP_NTFY_OFF);
      static const u32 ef[12]={(1u<<18)|0x308u,1u,(2u<<18)|0x21Cu,0xF0000000u,0x101u,(1u<<18)|0x260u,0x12345u,(2u<<18)|0x370u,0,0,(1u<<18)|0x200u,1u};
      int fo=n==12&&!memcmp(pb,ef,sizeof ef)&&pb[12]==0xDEADBEEFu&&nvflip_emit_flip(pb,11,1,1,1,1,0)==0;
      u32 n2=nvflip_emit_flip(pb,32,7,1,0,0,0); int im=n2==12&&pb[1]==0x10u&&pb[4]==0;
      u32 n3=nvflip_emit_image(pb,32,1024,600,4096,NVFLIP_HFB,0x100u);
      static const u32 ei[15]={(4u<<18)|0x224u,1024u|(600u<<16),0x10u,0xCFu,64u,(1u<<18)|0x240u,0xFB000000u,(1u<<18)|0x260u,0x100u,(1u<<18)|0x290u,0,(1u<<18)|0x298u,1024u|(600u<<16),(1u<<18)|0x2A4u,1024u|(600u<<16)};
      int io=n3==15&&!memcmp(pb,ei,sizeof ei)&&nvflip_emit_image(pb,14,1,1,64,1,0)==0;
      nvflip_mode_t hm={148500,1920,88,44,148,1080,4,5,36,1,1}; u32 hb2[64]; u32 n4=nvflip_emit_head(hb2,64,1,2,0x100u,&hm,1024,600,0x55u,1);
      int ho=n4==34&&hb2[0]==((1u<<18)|0x244Cu)&&hb2[1]==(1024u|(600u<<16))&&hb2[3]==(1842u|(1080u<<16))&&hb2[4]==((4u<<18)|0x2464u)&&hb2[5]==(2200u|(1125u<<16))&&hb2[32]==((1u<<18)|0x0340u)&&hb2[33]==((1u<<1)|(1u<<8))&&nvflip_emit_head(hb2,33,1,2,1,&hm,1,1,0,1)==0;
      chk("flip push buffer: C57E present control (non-tearing, interval 1), notifier, offset, no interlock, update; image and head methods in nouveau's order; short buffers refused",fo&&im&&io&&ho); }
    { static u8* hb=0; if(!hb)hb=mmap(0,G.bar1_len,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANONYMOUS|MAP_NORESERVE,-1,0); sim_b1_cpu=(u64)hb; sim_delay_hook=sim_vbl_tick;
      *(u64*)g_bootinfo=BAR1; *(u32*)(g_bootinfo+8)=4096; *(u16*)(g_bootinfo+12)=1024; *(u16*)(g_bootinfo+14)=600;
      for(u32 y=0;y<600;y++) for(u32 x=0;x<1024;x++) *(u32*)(hb+(u64)y*4096+x*4)=0xFF000000u|(y<<12)|x;
      int db=disp_bad; g_nvflip_want=0; nvl_reset(); int off0=nvflip_on()==0&&!g_nvflip_live&&strstr(nvflip_why(),"off")!=0&&nvflip_present((const u32*)hb,1024,1024,600)==0;
      nvl_reset(); int on=nvflip_setting(1); nvflip_info_t fi; int up=nvflip_info(&fi); int w=fi.win; u64 b1o=(u64)((u8*)NF.cpu[0]-hb), b2o=(u64)((u8*)NF.cpu[1]-hb); int s1=0,s2=0;
      int mapped=up&&walk(G.bar1_pde,b1o,&s1)==fi.vram[0]&&walk(G.bar1_pde,b2o+4096*300,&s2)==fi.vram[1]+4096*300&&!s1&&!s2;
      int native=up&&fi.head==0&&KC_ARM[0x2064/4]==(2200u|(1125u<<16))&&KC_ARM[0x204C/4]==(1024u|(600u<<16))&&KW_ARM[w][0x260/4]==(u32)(fi.vram[0]>>8)&&KW_ARM[w][0x240/4]==NVFLIP_HFB&&fi.pitch==4096&&NF.cpu[0][599*1024+1023]==(0xFF000000u|(599u<<12)|1023u);
      chk("gpu modeset on: main screen (head 0) to its 1920x1080 EDID mode, desktop scanned from GPU-owned VRAM buffer 0 mapped through BAR1",on==1&&off0&&native&&mapped&&disp_bad==db);
      { static u32 cb[128*128], cw[128*128], ci[64*64]; int iw=0,ihx=0,ihy=0; for(int i=0;i<128*128;i++){ cb[i]=0; cw[i]=0xFFFFFFu; }
        for(int y=0;y<36;y++)for(int x=0;x<=y/3;x++)cb[(64+y)*128+64+x]=0xFFFFFFu; cw[67*128+67]=0x808080u;
        int ib=nvcur_img_build(cb,cw,128,128,64,64,ci,&iw,&ihx,&ihy);
        REG[(NVC57A_STAT_BASE+72u*4u)/4]=0x00040000u; g_nvcur_enable=1; int u0=disp_updates;
        int sh=nvcur_hw_shape(ci,iw,ihx,ihy); u32 o8=(u32)(NCUR.vram>>8); int s1=0; u64 co=(u64)((u8*)NCUR.cpu-hb);
        int seq=ib==1&&iw==64&&ihx==0&&ihy==0&&sh==0&&disp_updates==u0+1&&g_nvcur_on[0]&&REG[(NVC57A_CTRL_BASE+73u*4u)/4]==1u&&KC_ARM[0x209C/4]==(0x80000000u|0xCFu|(1u<<8))&&KC_ARM[0x20A0/4]==0x72FFu&&KC_ARM[0x2088/4]==NVFLIP_HVRAM&&KC_ARM[0x2090/4]==o8&&!(NCUR.vram&0xFFu);
        int lay=walk(G.bar1_pde,co,&s1)==NCUR.vram&&!s1&&NCUR.vram+0x8000u<=KD.vtop&&NCUR.vram!=fi.vram[0]&&NCUR.vram!=fi.vram[1]&&NCUR.cpu[0]==0xFFFFFFFFu&&NCUR.cpu[35*64+11]==0xFFFFFFFFu&&NCUR.cpu[3*64+3]==0x7F000000u&&NCUR.cpu[63]==0&&NCUR.cpu[63*64+63]==0;
        u32 ua=nvcur_user(0); int mv=nvcur_hw_move(512,300,1024,600)==0&&REG[(ua+0x208u)/4]==(512u|(300u<<16))&&REG[(ua+0x200u)/4]==1u;
        int ce=nvcur_hw_move(-30,5000,1024,600)==0&&REG[(ua+0x208u)/4]==(0xFFE2u|(599u<<16))&&nvcur_hw_move(9000,-9000,1024,600)==0&&REG[(ua+0x208u)/4]==(1023u|(0xFFC1u<<16));
        int s2=nvcur_hw_shape(ci,iw,ihx,ihy)==0&&KC_ARM[0x2090/4]==o8+0x40u&&NCUR.cpu[0x1000]==0xFFFFFFFFu;
        int hid=nvcur_hw_shape(0,0,0,0)==0&&KC_ARM[0x209C/4]==0xCFu&&KC_ARM[0x2088/4]==0&&nvcur_hw_shape(ci,iw,ihx,ihy)==0&&KC_ARM[0x2090/4]==o8;
        chk("hardware cursor: 64x64 ARGB image in VRAM (BAR1), C57D cursor methods armed by one core update, C57A point/update, edges clipped, double-buffered shape change, hide",seq&&lay&&mv&&ce&&s2&&hid&&disp_bad==db); }
      static u32 fr[1024*600]; for(int i=0;i<1024*600;i++)fr[i]=0xFF00FF00u; int v0=sim_vbls, t0=sim_wtear;
      int f1=nvflip_present(fr,1024,1024,600); int still=KW_ARM[w][0x260/4]==(u32)(fi.vram[0]>>8)&&NF.pend==1&&*(u32*)(VR+fi.vram[1]+5*4)==0xFF00FF00u&&NF.ceframes==1&&NF.cpuframes==0;
      int wt=nvflip_wait(100); int armed=KW_ARM[w][0x260/4]==(u32)(fi.vram[1]>>8)&&sim_vbls>v0&&(KW_ARM[w][0x308/4]&0x7Fu)==1u&&NF.pend==0;
      int ok2=1; for(int k=0;k<40&&ok2;k++){ fr[0]=(u32)k; if(!nvflip_present(fr,1024,1024,600))ok2=0; if(KW_ASY[w][0x260/4]!=(u32)(fi.vram[k&1]>>8)||KW_ARM[w][0x260/4]!=(u32)(fi.vram[(k&1)^1]>>8))ok2=0; }
      nvflip_wait(100); nvflip_info(&fi);
      chk("page flips: the new buffer is shown only at the next vertical blank, the window notifier says done, 40 frames alternate the two buffers, never two flips queued",f1&&still&&wt&&armed&&ok2&&fi.cur==1&&fi.flips==41&&sim_wtear==t0&&disp_bad==db);
      { static u32 sc2[1024*600]; for(int i=0;i<1024*600;i++)sc2[i]=0xFF112233u; int okp=1;
        okp&=nvflip_present(sc2,1024,1024,600); nvflip_wait(100);
        dmg_rect r1={10,10,20,20}, r2={100,200,30,10}; for(int y=0;y<20;y++)for(int x=0;x<20;x++)sc2[(10+y)*1024+10+x]=0xFFAAAAAAu;
        okp&=nvflip_present_rects(sc2,1024,1024,600,&r1,1); nvflip_wait(100);
        for(int y=0;y<10;y++)for(int x=0;x<30;x++)sc2[(200+y)*1024+100+x]=0xFFBBBBBBu;
        int b=NF.cur^1; u32* vb=(u32*)(VR+NF.vram[b]); vb[500*1024+500]=0xDEADBEEFu; int ops0=sim_ce_ops; u32 cpu0=NF.cpuframes, rf0=NF.rframes;
        int pr=nvflip_present_rects(sc2,1024,1024,600,&r2,1);
        int two=pr&&sim_ce_ops==ops0+2&&NF.cpuframes==cpu0&&NF.rframes==rf0+1&&vb[500*1024+500]==0xDEADBEEFu&&vb[15*1024+15]==0xFFAAAAAAu&&vb[205*1024+110]==0xFFBBBBBBu&&vb[300*1024+300]==0xFF112233u;
        u32 f0=NF.flips, id0=NF.idle; nvflip_wait(100); int idle=nvflip_present_rects(sc2,1024,1024,600,0,0)==1&&NF.flips==f0&&NF.idle==id0+1&&NF.pend==0;
        chk("copy-engine frames: only the damaged rectangles of this frame and the previous one reach the VRAM back buffer, untouched pixels stay, no CPU copy, idle frames do not flip",okp&&two&&idle);
        u32 d0=NF.done, w0=NF.waits; okp=nvflip_present_rects(sc2,1024,1024,600,&r2,1); int st0=NV_DISP_NTFY_STATUS(nvp_rd32(KD.sync+NVFLIP_NTFY_OFF));
        int nw=nvflip_wait(100)&&NF.done==d0+1&&NF.pend==0&&NV_DISP_NTFY_STATUS(nvp_rd32(KD.sync+NVFLIP_NTFY_OFF))==NV_DISP_NTFY_FINISHED&&nvflip_wait(100)&&NF.waits==w0+1;
        chk("flip completion from the window notifier: the wait ends when the flip is FINISHED, nothing waits when no flip is queued",okp&&st0!=NV_DISP_NTFY_FINISHED&&nw);
        int fl0=nvset_fails(NVSET_MODESET); sim_ce_bad=1; NF.cechk=0; u32 f1=NF.flips; cpu0=NF.cpuframes; for(int y=0;y<10;y++)for(int x=0;x<30;x++)sc2[(200+y)*1024+100+x]=0xFFCCCCCCu;
        b=NF.cur^1; int p1=nvflip_present_rects(sc2,1024,1024,600,&r2,1); sim_ce_bad=0;
        int rb=p1&&!NF.ce&&strstr(NF.cewhy,"read back")&&NF.cpuframes==cpu0+1&&NF.flips==f1+1&&g_nvflip_live&&NF.cpu[b][200*1024+100]==0xFFCCCCCCu&&NVSE[NVSET_MODESET].st==NVSET_ST_FALLBACK&&strstr(nvset_last_error(NVSET_MODESET),"read back")&&nvset_fails(NVSET_MODESET)==fl0;
        nvflip_wait(100); NF.ce=1; NF.cemiss=0; sim_ce_hang=1; int hs=1; for(int k=0;k<3;k++){ sc2[k]=(u32)k; dmg_rect rk={0,0,4,1}; if(!nvflip_present_rects(sc2,1024,1024,600,&rk,1))hs=0; nvflip_wait(100); }
        sim_ce_hang=0; if(g_hung_p[1])*g_hung_p[1]=g_hung_v[1]; ch_ready(1);
        int hg=hs&&!NF.ce&&strstr(NF.cewhy,"three")&&g_nvflip_live&&NF.cpu[NF.cur][2]==2u;
        chk("copy-engine fallback: a wrong read-back or three missed copies move frames to the CPU, flips go on, the gpu modeset switch shows the fallback without counting a failure",rb&&hg); }
      int gz=nvflip_present(fr,800,800,600)==0&&!g_nvflip_live&&KW_ARM[w][0x260/4]==0&&KW_ARM[w][0x230/4]==64u&&KW_ARM[w][0x240/4]==NVFLIP_HFB&&KD.head[0].or>=0&&strstr(nvflip_why(),"size")!=0;
      nvl_reset(); int re=nvflip_on()==1&&g_nvflip_live&&NF.vram[0]==fi.vram[0]; u32 fb0=fi.fallbacks;
      sim_no_vbl=1; int to=nvflip_present(fr,1024,1024,600); int tw=nvflip_wait(30); sim_no_vbl=0; nvflip_info(&fi);
      int fell=to==1&&tw==0&&!g_nvflip_live&&fi.timeouts>=1&&fi.fallbacks==fb0+1&&KD.head[0].or<0;
      nvl_reset(); int back=nvflip_on()==1; nvflip_setting(0); int offd=!g_nvflip_live&&!g_nvflip_want&&KW_ARM[w][0x260/4]==0&&nvflip_on()==0;
      chk("fallback to the firmware picture: a desktop size change points the window back at it, a flip lost for 30 ms turns the head off; gpu modeset off restores it; buffers reused",gz&&re&&fell&&back&&offd&&disp_bad==db);
      chk("gpu modeset guard: a page flip lost at run time counts as a gpu modeset failure with its reason (3 failures turn it off)",nvset_fails(NVSET_MODESET)==1&&strstr(nvset_last_error(NVSET_MODESET),"page flip")!=0);
      { sim_rsz_ok=1; sim_rsz_n=0; nvl_reset(); int on2=nvflip_setting(1); nvflip_info_t ni; nvflip_info(&ni);
        int bufs=on2&&NF.w==1920&&NF.h==1080&&NF.pitch==7680&&NF.rsz==1&&KC_ARM[0x204C/4]==(1920u|(1080u<<16))&&KC_ARM[0x2058/4]==(1920u|(1080u<<16))&&KW_ARM[w][0x224/4]==(1920u|(1080u<<16))&&NF.cpu[0][599*1920+1023]==(0xFF000000u|(599u<<12)|1023u)&&NF.cpu[0][700*1920+1500]==0xFF000000u&&ni.fww==1024&&ni.fwh==600&&!ni.native;
        u32 f0=NF.flips; u32 a=0,c=0; int sw=nvflip_present(fr,1024,1024,600)==1&&sim_rsz_n==1&&sim_rsz_w==1920&&sim_rsz_h==1080&&NF.flips==f0&&NF.nat&&nvflip_native(&a,&c)&&a==1024&&c==600;
        static u32 big[1920*1080]; for(int i=0;i<1920*1080;i++)big[i]=0xFF445566u; int fr2=nvflip_present(big,1920,1920,1080)&&nvflip_wait(100)&&*(u32*)(VR+NF.vram[NF.cur]+1079ULL*7680+1919*4)==0xFF445566u&&KW_ARM[w][0x260/4]==(u32)(NF.vram[NF.cur]>>8);
        nvflip_setting(0); int bk=!g_nvflip_live&&!nvflip_native(0,0)&&KW_ARM[w][0x224/4]==(1024u|(600u<<16))&&KW_ARM[w][0x260/4]==0&&KC_ARM[0x204C/4]==(1024u|(600u<<16))&&KD.head[0].or>=0;
        sim_rsz_ok=0;
        chk("native desktop: a 1024x600 firmware mode becomes a 1920x1080 desktop (native buffers, window and viewport, firmware picture kept until the compositor switches), gpu modeset off restores the firmware size",bufs&&sw&&fr2&&bk&&disp_bad==db); }
      { u8* hb=(u8*)sim_b1_cpu; for(u32 y=0;y<1200;y++) for(u32 x=0;x<2048;x++) *(u32*)(hb+(u64)y*8192+x*4)=0xFF000000u|(y<<12)|x;
        *(u32*)(g_bootinfo+8)=8192; *(u16*)(g_bootinfo+12)=2048; *(u16*)(g_bootinfo+14)=1200; sim_rsz_ok=1; nvl_reset(); int on3=nvflip_setting(1);
        int big=on3&&NF.w==1920&&NF.h==1080&&NF.fww==2048&&NF.fwh==1200&&(u8*)NF.cpu[0]>=hb+8192ULL*1200&&NF.cpu[0][1079*1920+1919]==(0xFF000000u|(1079u<<12)|1919u)&&NF.cpu[1][0]==0xFF000000u&&NF.cpu[1][5]==0xFF000000u;
        nvflip_setting(0); *(u32*)(g_bootinfo+8)=2048; *(u16*)(g_bootinfo+12)=1024; *(u16*)(g_bootinfo+14)=600; nvl_reset(); int lay=nvflip_setting(1)==0&&!g_nvflip_live&&strstr(nvflip_why(),"layout")!=0;
        nvflip_setting(0); *(u32*)(g_bootinfo+8)=4096; *(u64*)g_bootinfo=BAR1+G.bar1_len-4096; nvl_reset(); lay&=nvflip_setting(1)==0&&!g_nvflip_live&&strstr(nvflip_why(),"layout")!=0; *(u64*)g_bootinfo=BAR1;
        *(u32*)(g_bootinfo+8)=4096; *(u16*)(g_bootinfo+12)=1024; *(u16*)(g_bootinfo+14)=600; nvflip_setting(0); sim_rsz_ok=0;
        chk("native desktop smaller than the firmware screen: the firmware picture is cropped into the native buffer, nothing is written past it; a firmware screen reaching past BAR1 or with a pitch below its width is refused",big&&lay); }
      { u32 ps[4]; u32 dw=0, dh=0; int n4=desk_ladder(3840,2160,0,ps,4); int l4=n4==2&&ps[0]==150&&ps[1]==200&&desk_dims(3840,2160,150,&dw,&dh)&&dw==2560&&dh==1440&&desk_dims(3840,2160,125,&dw,&dh)&&dw==3072&&dh==1728&&desk_dims(3840,2160,100,&dw,&dh)&&dw==3840&&dh==2160;
        int l2=desk_ladder(2560,1440,0,ps,4)==4&&ps[0]==100&&desk_ladder(2560,1440,200,ps,4)==1&&ps[0]==200;
        int l5=!desk_dims(5120,2880,100,&dw,&dh)&&desk_ladder(5120,2880,100,ps,4)==2&&ps[0]==150&&desk_dims(5120,2880,150,&dw,&dh)&&dw==3408&&dh==1920;
        int lo=desk_dims(1366,768,100,&dw,&dh)&&dw==1366&&dh==768&&!desk_dims(1366,768,200,&dw,&dh)&&desk_ladder(1366,768,0,ps,4)==3&&!desk_pct_ok(175)&&desk_pct_ok(0);
        int bb=desk_cpu_bytes(1920,1080,1920,1080)==0&&desk_cpu_bytes(3840,2160,1920,1080)==3840ULL*2160*16+DESK_RESERVE;
        chk("desktop scale: 4K auto picks 150% (2560x1440) then 200%, 125% gives 3072x1728, 1440p auto is 100%, 5K skips 100%, 1366x768 kept exact, bad values refused, CPU buffer budget",l4&&l2&&l5&&lo&&bb); }
      { sim_rsz_ok=1; sim_scale=125; sim_rsz_n=0; nvl_reset(); int o1=nvflip_setting(1); nvflip_info_t ni; nvflip_info(&ni);
        int s1=o1&&NF.w==1536&&NF.h==864&&NF.rsz==1&&KC_ARM[0x204C/4]==(1536u|(864u<<16))&&KW_ARM[w][0x224/4]==(1536u|(864u<<16))&&ni.scale==125&&ni.mw==1920&&ni.mh==1080;
        int a1=nvflip_present(fr,1024,1024,600)==1&&sim_rsz_n==1&&sim_rsz_w==1536&&sim_rsz_h==864&&NF.nat;
        nvflip_setting(0); sim_scale=100; sim_rsz_maxw=1600; sim_rsz_rel=0; nvl_reset(); int o2=nvflip_setting(1); nvflip_info(&ni);
        int s2=o2&&NF.w==1536&&NF.h==864&&ni.scale==125&&strstr(g_nv_log,"more memory")!=0;
        nvflip_setting(0); sim_rsz_maxw=640; nvl_reset(); int o3=nvflip_setting(1); nvflip_info(&ni);
        int s3=o3&&NF.w==1024&&NF.h==600&&NF.rsz==0&&ni.scale==0&&sim_rsz_rel==1&&strstr(g_nv_log,"firmware size, scaled")!=0&&KW_ARM[w][0x224/4]==(1024u|(600u<<16));
        nvflip_setting(0); sim_rsz_maxw=0; sim_scale=0; sim_rsz_ok=0;
        chk("desktop scale in nvflip_on: 125% on a 1080p screen gives a 1536x864 desktop scaled by the head; when memory refuses 100% the next scale is used; when nothing fits the firmware size stays, scaled",s1&&a1&&s2&&s3&&disp_bad==db); }
      { u32 off0=EV.vboff, m0=EV.vbmiss, b0=EV.vbbad, v0=EV.vb[1]; EV.vboff=0; EV.vbbad=0; REG[0x611800/4+1]=4u; int t4=nvvb_take(1)==0&&REG[0x611800/4+1]==4u;
        REG[0x611800/4+1]=6u; int t6=nvvb_take(1)==1&&REG[0x611800/4+1]==4u&&EV.vb[1]==v0+1;
        REG[0x611800/4+1]=0xBADF5040u; int bad=1; for(u32 i=0;i<NVVB_BAD_MAX;i++)if(nvvb_take(1)!=0)bad=0; bad&=REG[0x611800/4+1]==0xBADF5040u&&EV.vboff==1&&strstr(EV.vbwhy,"invalid")!=0&&!nvvb_usable(1);
        REG[0x611800/4+1]=0; EV.vboff=off0; EV.vbmiss=m0; EV.vbbad=b0; EV.vbwhy[0]=0;
        chk("vblank status hardening: only bit 1 (the RM head-timing line nouveau tu102 owns) is taken and cleared, bit 2 is left to RM, PRI-error/reserved-bit reads are never acknowledged and turn the status off after 4",t4&&t6&&bad); }
      { static u32 ci[32*32]; for(int i=0;i<32*32;i++)ci[i]=0xFF000000u; int fb=nvcur_hw_shape(ci,32,0,0)<0&&!nvcur_hw_active()&&nvcur_hw_move(10,10,1024,600)<0; nvcur_hw_off(); g_nvcur_enable=0; nvcur_chan_fini(0);
        chk("hardware cursor fallback: with the flip head gone the image is refused and the cursor is inactive (compositor draws the software cursor)",fb&&!g_nvcur_on[0]); }
      sim_delay_hook=0; sim_b1_cpu=0; }
    { static const struct { u32 b0; u16 dev; u32 disp, core; const char* nm; } GD[2]={{0x174000A1u,0x2484,0xC670u,0xC67Du,"GA104"},{0x194000A1u,0x2786,0xC770u,0xC77Du,"AD104"}};
      int gok[2]={0,0};
      for(int gi=0;gi<2;gi++){ fflush(stdout); pid_t pid=fork();
        if(pid==0){ static u8* hb=0; if(!hb)hb=mmap(0,G.bar1_len,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANONYMOUS|MAP_NORESERVE,-1,0); sim_b1_cpu=(u64)hb; sim_delay_hook=sim_vbl_tick;
            *(u64*)g_bootinfo=BAR1; *(u32*)(g_bootinfo+8)=4096; *(u16*)(g_bootinfo+12)=1024; *(u16*)(g_bootinfo+14)=600;
            for(u32 y=0;y<600;y++) for(u32 x=0;x<1024;x++) *(u32*)(hb+(u64)y*4096+x*4)=0xFF000000u|(y<<12)|x;
            nvflip_setting(0); nvgsp_mode_lost(); rm_free(HC(2),HC(2)); sim_two=0;
            static nvgsp_gen sg; memset(&sg,0,sizeof sg); sg.name=GD[gi].nm; sg.cls=gi?&sim_cls_ad:&nvgsp_cls_ga10x; g_nvgen=&sg;
            sim_boot0=REG[0]=GD[gi].b0; g_nv_boot0=GD[gi].b0; G.chip=(GD[gi].b0>>20)&0x1FFu; g_nv_dev=GD[gi].dev; SD_DISP=GD[gi].disp; SD_CORE=GD[gi].core; SD_WNDW=0xC67Eu;
            g_nv_ampere_on=0; g_nv_ada_on=0; int ab=alloc_bad, cb=ctrl_bad, db=disp_bad;
            g_nvflip_want=1; nvl_reset(); int gate=nvflip_on()==0&&strstr(nvflip_why(),"RTX 30/40")!=0&&!disp_root&&!nvcur_gen_ok(GD[gi].b0);
            if(gi)g_nv_ada_on=1; else g_nv_ampere_on=1;
            sim_refuse_cls=(int)GD[gi].core; nvl_reset(); char want[16]; snprintf(want,sizeof want,"class %X",GD[gi].core); int rf=nvflip_on()==0&&strstr(nvflip_why(),want)!=0&&!disp_root; sim_refuse_cls=0;
            nvl_reset(); int on=nvflip_setting(1); nvflip_info_t fi; int up=nvflip_info(&fi); int w=fi.win; int sk=0; u32 px=sim_scan(0,1023,599,&sk);
            int mode=on==1&&up&&fi.head==0&&disp_root&&disp_core&&disp_win[w]&&KW_ARM[w][0x228/4]==0&&KW_ARM[w][0x260/4]==(u32)(fi.vram[0]>>8)&&KC_ARM[0x2064/4]==(2200u|(1125u<<16))&&sk&&NF.cpu[0][599*1024+1023]==(0xFF000000u|(599u<<12)|1023u); (void)px;
            static u32 fr[1024*600]; for(int k=0;k<1024*600;k++)fr[k]=0xFF0000FFu; int t0=sim_wtear, fl=1; u32 f0=fi.flips;
            for(int k=0;k<12&&fl;k++){ fr[0]=(u32)k; if(!nvflip_present(fr,1024,1024,600))fl=0; if(KW_ASY[w][0x260/4]!=(u32)(fi.vram[(k+1)&1]>>8))fl=0; }
            int wt=nvflip_wait(100); nvflip_info(&fi); sim_scan(0,5,5,&sk); int flips=fl&&wt&&fi.flips==f0+12&&fi.cur==0&&sim_wtear==t0&&(KW_ARM[w][0x308/4]&0x7Fu)==1u&&KW_ARM[w][0x260/4]==(u32)(fi.vram[0]>>8)&&sk&&NF.cpu[0][5]==0xFF0000FFu&&NF.cpu[0][0]==11u;
            static u32 ci[32*32]; for(int k=0;k<32*32;k++)ci[k]=0xFF000000u; REG[(NVC57A_STAT_BASE+72u*4u)/4]=0x00040000u; g_nvcur_enable=1;
            int cur=nvcur_gen_ok(GD[gi].b0)&&nvcur_hw_shape(ci,32,0,0)==0&&(KC_ARM[0x209C/4]&0x80000000u)&&KC_ARM[0x2088/4]==NVFLIP_HVRAM&&nvcur_hw_move(100,50,1024,600)==0&&REG[(nvcur_user(0)+0x208u)/4]==(100u|(50u<<16));
            nvcur_hw_off(); nvcur_chan_fini(0); g_nvcur_enable=0;
            sim_two=1; nvl_reset(); int ex=nvgsp_mode(0)==1&&KD.head[1].or>=0&&KC_ARM[(0x2020+0x400)/4]==0x400u&&disp_win[2]&&KW_ARM[2][0x228/4]==0&&nvgsp_mode_off()==1;
            nvflip_setting(0); int off=!g_nvflip_live&&KW_ARM[w][0x260/4]==0&&KW_ARM[w][0x240/4]==NVFLIP_HFB;
            int ok=gate&&rf&&mode&&flips&&cur&&ex&&off&&alloc_bad==ab&&ctrl_bad==cb&&disp_bad==db;
            if(!ok){ printf("    %s display: gate=%d refused=%d mode=%d flips=%d cursor=%d extend=%d off=%d alloc=%d ctrl=%d disp=%d why=%s\n%.600s\n",GD[gi].nm,gate,rf,mode,flips,cur,ex,off,alloc_bad-ab,ctrl_bad-cb,disp_bad-db,nvflip_why(),g_nv_log); fflush(stdout); }
            _exit(ok?0:1); }
        int st=1; waitpid(pid,&st,0); gok[gi]=WIFEXITED(st)&&WEXITSTATUS(st)==0; }
      chk("RTX 30 display (GA104, classes C670/C67D/C67E): refused while gpu ampere is off, C67D refusal named, native mode with SET_STORAGE 0 and a pitch ctxdma, 12 tear-free flips, C67A cursor, second screen, back to the firmware picture",gok[0]);
      chk("RTX 40 display (AD104, classes C770/C77D/C67E): refused while gpu ada is off, C77D refusal named, native mode with SET_STORAGE 0 and a pitch ctxdma, 12 tear-free flips, C67A cursor, second screen, back to the firmware picture",gok[1]); }
    { static u8* hb=0; if(!hb)hb=mmap(0,G.bar1_len,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANONYMOUS|MAP_NORESERVE,-1,0); sim_b1_cpu=(u64)hb; sim_delay_hook=sim_vbl_tick;
      *(u64*)g_bootinfo=BAR1; *(u32*)(g_bootinfo+8)=4096; *(u16*)(g_bootinfo+12)=1024; *(u16*)(g_bootinfo+14)=600;
      nvflip_setting(0); nvgsp_mode_lost(); rm_free(HC(2),HC(2)); NVSE[NVSET_DPTRAIN].on=1; nvset_reset(NVSET_DPTRAIN); int db=disp_bad, cb=ctrl_bad;
      sim_two=1; sim_proto2=8; sim_dp_sink(0,0); sim_dp_sink(1,0); sim_dp_stream=0; nvl_reset(); int r1=nvgsp_mode(0); int bw=0, ln=0, kp=1; int in=nvdp_info(1,&bw,&ln,&kp,0,0);
      int t2=r1==1&&in&&bw==0x14&&ln==4&&!kp&&sim_dp_ctrl[1]==1&&sim_dp_stream==1&&KC_ARM[0x320/4]==((1u<<1)|(8u<<8))&&sim_dpcd[1][0x100]==0x14&&sim_dpcd[1][0x600]==1&&sim_dp_wr[0]==0&&sim_dp_ctrl[0]==0&&strstr(g_nv_log,"DisplayPort link trained");
      chk("DisplayPort second screen: link trained through DP_CTRL on its own SOR (protocol DP_A), stream set up, the firmware's screen is never touched over AUX",t2&&disp_bad==db&&ctrl_bad==cb&&sim_dp_bad==0);
      sim_dpcd[1][0x202]=0; sim_dpcd[1][0x204]=0; NDP[1].next=0; u32 ev0=0, rt0=0; nvdp_info(1,0,0,0,&ev0,&rt0); nvdp_service(); u32 ev1=0, rt1=0; int st1=nvdp_info(1,&bw,&ln,0,&ev1,&rt1);
      int nos=1; NDP[1].next=hr_ns()+1000000000ULL; int c2=sim_dp_ctrl[1]; nvdp_service(); nos=sim_dp_ctrl[1]==c2;
      chk("DisplayPort hot-plug/link loss: lost status found by the 1 s check, link trained again, nothing done before the next check is due",st1&&ev1==ev0+1&&rt1>rt0&&sim_dp_ctrl[1]==2&&ln==4&&nos&&KD.head[1].or>=0);
      sim_dp_dead[1]=1; int rounds=0; for(int k=0;k<6&&NDP[1].on;k++){ NDP[1].next=0; nvdp_service(); rounds++; }
      int gave=!NDP[1].on&&rounds==DP_HPD_MAX_TRIES&&KD.head[1].or<0&&!nvset_auto_off(NVSET_DPTRAIN)&&KC_ARM[0x320/4]==0;
      sim_dp_sink(1,0); sim_dp_lanes[1]=2; nvl_reset(); int r3=nvgsp_mode(0); nvdp_info(1,&bw,&ln,0,0,0);
      chk("DisplayPort unplugged: bounded retries (3 checks), then the head is turned off without counting as a feature failure; a sink with 2 good lanes falls back to HBR2 x2",gave&&r3==1&&ln==2&&bw==0x14&&sim_dp_ctrl[1]==4&&disp_bad==db);
      { int pr1=nvgsp_dp_present(); int st0=G.state; G.state=6; int pr0=nvgsp_dp_present(); G.state=st0; int p2=sim_proto2; sim_proto2=2; g_mon_n=-1; int pr2=nvgsp_dp_present(); sim_proto2=p2; g_mon_n=-1; mon_scan();
        chk("Settings DisplayPort button: a missing DisplayPort monitor or a stopped card is found before the guard (not a dptrain failure)",pr1==1&&pr0==0&&pr2==0); }
      nvgsp_mode_off(); NVSE[NVSET_DPTRAIN].on=0; nvl_reset(); int r4=nvgsp_mode(0); int off4=r4==0&&strstr(g_nv_log,"link training is off")&&sim_dp_ctrl[1]==4; NVSE[NVSET_DPTRAIN].on=1;
      chk("dptrain off: a DisplayPort monitor is left alone",off4);
      nvgsp_mode_lost(); rm_free(HC(2),HC(2)); sim_two=0; sim_proto1=8; sim_dp_sink(0,1); sim_dp_stream=0; g_nvflip_want=1; nvl_reset(); int on=nvflip_on(); kp=0; in=nvdp_info(0,&bw,&ln,&kp,0,0);
      int keep=on==1&&g_nvflip_live&&in&&kp&&bw==0x14&&ln==4&&sim_dp_ctrl[0]==0&&sim_dp_wr[0]==0&&sim_dp_stream==1&&KC_ARM[0x300/4]==((1u<<0)|(8u<<8))&&strstr(g_nv_log,"kept from the firmware");
      chk("DisplayPort main screen: the firmware-trained link is reused (no AUX writes, no DP_CTRL), native mode and page flips on head 0",keep&&disp_bad==db&&sim_dp_bad==0);
      sim_dpcd[0][0x202]=0; NDP[0].next=0; nvdp_service(); int re=sim_dp_ctrl[0]==1&&g_nvflip_live&&NDP[0].on;
      nvflip_setting(0); nvgsp_mode_lost(); rm_free(HC(2),HC(2)); sim_dp_sink(0,1); sim_dpcd[0][0x100]=0x06; sim_dpcd[0][0x101]=0x81; sim_dpcd[0][0x202]=0x07; g_nvflip_want=1; nvl_reset(); int slow=nvflip_on()==0&&!g_nvflip_live&&strstr(nvflip_why(),"too slow")&&sim_dp_ctrl[0]==0&&sim_dp_wr[0]==0;
      chk("DisplayPort main screen: a lost link is trained again; a firmware link too slow for the native mode keeps the firmware picture",re&&slow);
      nvflip_setting(0); nvgsp_mode_lost(); rm_free(HC(2),HC(2)); nvset_reset(NVSET_DPTRAIN); sim_dp_sink(0,1); sim_dpcd[0][0x202]=0; sim_dp_lanes[0]=0; g_nvflip_want=1; nvl_reset(); int f0=nvset_fails(NVSET_DPTRAIN); int lost=nvflip_on()==0&&!g_nvflip_live&&sim_dp_ctrl[0]>0;
      chk("DisplayPort main screen: a lost firmware link that cannot be trained again counts as a dptrain failure (guard try begun and ended)",lost&&nvset_fails(NVSET_DPTRAIN)==f0+1&&!NVSE[NVSET_DPTRAIN].tried); nvset_reset(NVSET_DPTRAIN); sim_dp_sink(0,1);
      nvflip_setting(0); nvgsp_mode_lost(); rm_free(HC(2),HC(2)); sim_proto1=1; sim_proto2=2; NVSE[NVSET_DPTRAIN].on=0; sim_delay_hook=0; sim_b1_cpu=0; }
    { int a0=alloc_bad; g_rpc_dead=0; nvl_reset(); sim_drop_next=1; u32 x[3]={0,0,0}; int t1=rm_ctrl(HC(1),HDEV,0x730120u,x,12); int t2=rm_ctrl(HC(1),HDEV,0x730120u,x,12); int dead=g_rpc_dead;
      chk("a GSP-RM reply that does not come within 5 s stops all further requests (a late reply is never taken for the next one)",t1!=0&&t2==-1003&&dead&&strstr(g_nv_log,"did not answer")!=0&&alloc_bad==a0); g_rpc_dead=0; sim_drop_next=0; }
    sim_mode=1; nvl_reset(); r=nvgsp_shutdown();
    chk("power off / restart unloads like nouveau's fini: UNLOADING_GUEST_DRIVER, FWSEC-SB, booter_unload, WPR2 cleared",r>0&&booter_ok&&fwsec_ok&&G.state==0&&nvgsp_shutdown()==1);
    { nv_fail(9,"test reason"); int ln=g_nv_log_n; nvgsp_cmd("log"); int kept=g_nv_log_n==ln&&strstr(g_nv_log,"test reason")!=0; nvgsp_cmd("status");
      chk("nvgsp log keeps the last log, nvgsp status keeps the failure reason",kept&&strstr(g_nv_log,"last failure: step 9 - test reason")!=0); }
    if(fails){ g_quiet=0; printf("--- driver log ---\n%s\n",g_nv_log); }
    { nvdp_sst_t a, b; int ok1=nvdp_sst_calc(148500,1920,2200,8,0x14,4,1,0,&a)&&a.wm==20&&a.hbs==960&&a.vbs==6823&&a.tu==64;
      int ok2=nvdp_sst_calc(533250,3840,4000,8,0x14,4,1,0,&a)&&a.wm==20&&a.hbs==134&&a.vbs==3835&&nvdp_sst_calc(241500,2560,2720,8,0x0A,4,1,0,&b)&&b.hbs==149&&b.vbs==2804;
      int ok3=!nvdp_sst_calc(594000,3840,4400,8,0x06,1,1,0,&a)&&!nvdp_sst_calc(148500,1920,1920,8,0x14,4,1,0,&a)&&!nvdp_sst_calc(148500,1920,2200,8,0x14,3,1,0,&a)&&nvdp_sst_calc(148500,1920,2200,8,0x14,4,1,1,&b)&&b.wm==22;
      chk("DP SST stream values computed like nouveau nv50_sor_dp_watermark_sst: 1080p/1440p/2160p watermark, hblank and vblank symbols, refused when the link is too slow",ok1&&ok2&&ok3);
      chk("DP stream setup sends the computed watermark and blank symbols with bEnableOverride (RM no longer left to guess)",sim_dp_ovr_n>0&&sim_dp_wm>=20&&sim_dp_wm<=39&&sim_dp_hbs>0&&sim_dp_vbs>0); }
    { static u8 e[256]; memset(e,0,256); e[0]=0;e[1]=0xFF;e[2]=0xFF;e[3]=0xFF;e[4]=0xFF;e[5]=0xFF;e[6]=0xFF;e[7]=0; e[126]=1;
      #define DTD(d,pk,ha,hb,va,vb,ho,hw,vo,vw) do{ u8* q_=(d); q_[0]=(u8)((pk)&255); q_[1]=(u8)((pk)>>8); q_[2]=(u8)((ha)&255); q_[3]=(u8)((hb)&255); q_[4]=(u8)((((ha)>>8)<<4)|((hb)>>8)); q_[5]=(u8)((va)&255); q_[6]=(u8)((vb)&255); q_[7]=(u8)((((va)>>8)<<4)|((vb)>>8)); q_[8]=(u8)((ho)&255); q_[9]=(u8)((hw)&255); q_[10]=(u8)((((vo)&15)<<4)|((vw)&15)); q_[11]=(u8)((((ho)>>8)<<6)|(((hw)>>8)<<4)|(((vo)>>4)<<2)|((vw)>>4)); q_[17]=0x1E; }while(0)
      DTD(e+54,59400,3840,560,2160,90,176,88,8,10); DTD(e+72,14850,1920,280,1080,45,88,44,4,5);
      e[128]=2; e[129]=3; e[130]=4; DTD(e+132,29700,3840,560,2160,90,176,88,8,10);
      kmode m; int r1=nvk_edid_pick(e,256,0,1ULL<<30,&m); int a1=r1==1&&m.hact==3840&&m.khz==594000;
      int r2=nvk_edid_pick(e,256,340000,1ULL<<30,&m); int a2=r2==2&&m.hact==3840&&m.vact==2160&&m.khz==297000;
      int r3=nvk_edid_pick(e,256,0,nvk_mode_bytes(3840,2160,NVFLIP_NBUF)-1,&m); int a3=r3==2&&m.hact==1920&&m.vact==1080;
      int r4=nvk_edid_pick(e,256,0,0x100000,&m); int r5=nvk_edid_pick(e,128,340000,1ULL<<30,&m); int a5=r5==2&&m.hact==1920;
      DTD(e+54,24150,2560,160,1440,41,48,32,3,5); int r6=nvk_edid_pick(e,256,340000,1ULL<<30,&m); int a6=r6==1&&m.hact==2560&&m.vact==1440;
      chk("native modes above 1920x1080: 4K/1440p picked when the link and the VRAM budget allow, else the largest EDID mode that fits (CTA DTDs), none when nothing fits",a1&&a2&&a3&&r4==0&&a5&&a6&&nvk_mode_bytes(3840,2160,2)==2*0x1FB0000ULL); }
    { u8 b[64]; memset(b,0,64); *(u32*)(b+0)=HC(2); *(u32*)(b+4)=NVEV_H(NVEV_HPD); *(u32*)(b+24)=8; *(u32*)(b+29)=0x400; *(u32*)(b+33)=0;
      u32 p0=EV.posts, h0=EV.hpd, bad0=EV.bad, o0=EV.other; g_nvev_kick=0; ev_handle(0x1003,b,40); int k1=g_nvev_kick==1&&EV.hpd==h0+1&&(EV.plug&0x400);
      ev_handle(0x1003,b,20); *(u32*)(b+4)=0x12345678u; *(u32*)(b+24)=0; ev_handle(0x1003,b,32); *(u32*)(b+24)=999; ev_handle(0x1003,b,40);
      u32 rg=EV.rgline; ev_handle(0x1007,b,8);
      chk("GSP-RM POST_EVENT (0x1003): hot-plug event decoded and wakes the DP link check, short or oversized events rejected, unknown events counted; RG_LINE_INTR counted",k1&&EV.posts==p0+4&&EV.bad==bad0+2&&EV.other==o0+1&&EV.rgline==rg+1);
      { nvgsp_mon_t sv[3]; memcpy(sv,g_mon,sizeof sv); int svn=g_mon_n; u32 sc=g_nvev_conn; int ss=g_nvev_cset; g_nvev_cset=0; g_nvev_np=0; g_nvev_nu=0;
        memset(g_mon,0,sizeof sv); g_mon_n=3; g_mon[0].id=0x100; g_mon[0].connected=1; g_mon[1].id=0x400; g_mon[1].connected=1; g_mon[2].id=0x800;
        u8 e[64]; memset(e,0,64); *(u32*)(e+4)=NVEV_H(NVEV_HPD); *(u32*)(e+24)=8; sim_hpd_n=0;
        *(u32*)(e+33)=0x400; ev_handle(0x1003,e,40); nvev_hpd_tick(); int u1=sim_hpd_n==1&&sim_hpd_last==0;
        nvev_hpd_tick(); *(u32*)(e+33)=0x800; ev_handle(0x1003,e,40); nvev_hpd_tick(); int u2=sim_hpd_n==1;
        *(u32*)(e+29)=0x400; *(u32*)(e+33)=0; ev_handle(0x1003,e,40); nvev_hpd_tick(); int u3=sim_hpd_n==2&&sim_hpd_last==1;
        *(u32*)(e+29)=0x800; *(u32*)(e+33)=0x800; ev_handle(0x1003,e,40); nvev_hpd_tick(); int u4=sim_hpd_n==2;
        chk("hot-plug events drive the extended desktop: unplug leaving one monitor sends screen 2 away, replug brings it back, other outputs ignored",u1&&u2&&u3&&u4);
        { g_mon[0].lit=1; g_mon[0].act_w=2560; g_mon[0].act_h=1440; g_mon[1].lit=1; g_mon[1].act_w=1920; g_mon[1].act_h=1080;
          int m1=nvev_s1_id(g_mon,3,2560,1440,1920,1080)==0x100&&nvev_s1_id(g_mon,3,1920,1080,2560,1440)==0x400&&nvev_s1_id(g_mon,3,1920,1080,1920,1080)==0&&nvev_s1_id(g_mon,3,2560,1440,0,0)==0&&nvev_s1_id(g_mon,3,1280,1024,1920,1080)==0;
          sim_w=2560; sim_h=1440; sim_w2=1920; sim_h2=1080; g_nvev_cset=0; g_nvev_np=0; g_nvev_nu=0; g_nvev_s1lost=0; g_mon[2].connected=0; sim_hpd_n=0; sim_s1_n=0;
          *(u32*)(e+29)=0; *(u32*)(e+33)=0x100; ev_handle(0x1003,e,40); nvev_hpd_tick(); int m2=sim_s1_n==1&&sim_s1_last==0&&sim_hpd_n==0;
          *(u32*)(e+29)=0x100; *(u32*)(e+33)=0; ev_handle(0x1003,e,40); nvev_hpd_tick(); int m3=sim_s1_n==2&&sim_s1_last==1&&sim_hpd_n==0;
          *(u32*)(e+29)=0; *(u32*)(e+33)=0x400; ev_handle(0x1003,e,40); nvev_hpd_tick(); int m4=sim_s1_n==2&&sim_hpd_n==1&&sim_hpd_last==0;
          *(u32*)(e+29)=0x400; *(u32*)(e+33)=0; ev_handle(0x1003,e,40); nvev_hpd_tick(); int m5=sim_hpd_n==2&&sim_hpd_last==1;
          sim_w=sim_w2=1920; sim_h=sim_h2=1080; g_nvev_s1lost=0;
          chk("hot-plug: screen 1 is mapped to a display ID by its active mode (unique W1xH1 vs W2xH2, else unknown); unplugging the screen-1 monitor mirrors the desktop onto the monitor left, replug restores; screen-2 unplug unchanged",m1&&m2&&m3&&m4&&m5); }
        memcpy(g_mon,sv,sizeof sv); g_mon_n=svn; g_nvev_conn=sc; g_nvev_cset=ss; }
      chk("display events subscribed through NV01_EVENT_KERNEL_CALLBACK_EX + EVENT_SET_NOTIFICATION (repeat) for hot-plug and DP IRQ",sim_ev_alloc>=2&&sim_ev_sub>=2); }
    { u64 nt=0x10000000ULL; int off0=EV.vboff; EV.vboff=0; EV.vbmiss=0; u32 f0=EV.vbflips, p0=EV.pollflips;
      nvp_wr32(nt,0); REG[0x611800/4+1]=2; u64 t0=hr_ns(); int w0=nvvb_wait(1,nt,5); u64 dt=hr_ns()-t0; int b0=w0==0&&dt<200000000ULL;
      nvp_wr32(nt,0x80000000u); REG[0x611800/4+1]=2; int w1=nvvb_wait(1,nt,5); int b1=w1==1&&EV.vbflips==f0+1&&(REG[0x611800/4+1]&2)==0;
      for(int i=0;i<(int)NVVB_MISS_MAX;i++)nvvb_wait(1,nt,5); int b2=EV.vboff==1&&EV.vbwhy[0];
      REG[0x611800/4+1]=2; int w3=nvvb_wait(1,nt,5); int b3=w3==1&&EV.pollflips==p0+1&&(REG[0x611800/4+1]&2);
      REG[0x611800/4+1]=0; EV.vboff=off0; EV.vbmiss=0;
      chk("flip completion: head vertical-blank status (0x611800, as nouveau tu102 GSP) seen and acknowledged, bounded wait, 8 flips without it fall back to notifier polling",b0&&b1&&b2&&b3); }
    printf(fails?"FAILURES\n":"ALL PASS\n"); return fails?1:0; }
