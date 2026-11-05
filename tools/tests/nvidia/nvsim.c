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
#include "drivers/video/nvidia/nvgsp.c"
#include "drivers/video/fb/damage.c"
#include "include/nvx/drivers/video/fb/gfx.h"
int g_gfx=0, g_gfx_present=0; char g_gfx_why[64]; void (*g_gfx_on_stop)(void)=0; static int sim_gfx_starts=0, sim_gfx_sid=0; static u32* sim_gfx_px=0;
const char* gk_name(int id){ static const char* N[12]={"fill","blend","mask","copy","copym","gradh","gradv","tint","blurh","blurv","over","overm"}; return id>=0&&id<12?N[id]:"op"; }
void gfx_stop(const char* why){ if(g_gfx==0)return; if(g_gfx_on_stop)g_gfx_on_stop(); gq_abort(); g_gfx=0; g_gq_mode=GQM_OFF; int i=0; while(why[i]&&i<63){ g_gfx_why[i]=why[i]; i++; } g_gfx_why[i]=0; }
gfx_alloc_fn g_gfx_alloc_gpu=0; void gfx_forget(void){} u64 g_last_input_t=0; int g_gfx_nomem=0;
int gfx_start(int mode,gfx_alloc_fn al,u32* sp,int ss){ (void)sp; (void)ss; sim_gfx_starts++; u32* px=0; int s=al(300,200,304,&px); if(s<=0)return -1; sim_gfx_sid=s; sim_gfx_px=px; g_gq_mode=GQM_GPU; g_gfx=mode; return 0; }
#include "drivers/video/fb/gq.c"
#include "drivers/video/nvidia/nvdraw.c"
int ufs_n=0; struct nvx_ufs_t ufs[32]; static char sim_uflag=0; int ufat_read(int fi,char* o,int m){ (void)fi; if(m<1)return 0; o[0]=sim_uflag; return sim_uflag?1:0; } volatile u64 g_ticks=0; u32 g_gfx_frames=0, g_gfx_frame_us=0;

int g_nv_bus=1, g_nv_slot=0, g_nv_fn=0; u16 g_nv_dev=0x21C4; u64 g_nv_bar0=0xF6000000ULL, g_nv_bar1=0, g_nv_bar3=0; u32 g_nv_bar0_sz=0x1000000; u32 g_nv_boot0=0x168000A1u;
int g_nv_arch=0, g_nv_impl=0, g_nv_rev=0; u8 g_nv_vbios[65536]; int g_nv_vbios_len=0; u32 g_nv_bit_off=0, g_nv_dcb_off=0; int g_nv_ncon=0; u8 g_nv_con_type[16], g_nv_con_or[16], g_nv_con_heads[16];
FsFile FS[FS_N];
const u8 _binary_nvfw_gsp_xz_start[1], _binary_nvfw_gsp_xz_end[1], _binary_nvfw_bootloader_xz_start[1], _binary_nvfw_bootloader_xz_end[1], _binary_nvfw_booter_load_xz_start[1], _binary_nvfw_booter_load_xz_end[1];
const u8 _binary_nvfw_booter_unload_xz_start[1], _binary_nvfw_booter_unload_xz_end[1], _binary_nvfw_acr_bl_xz_start[1], _binary_nvfw_acr_bl_xz_end[1];

static int fails=0; static void chk(const char* w,int c){ printf("  %-78s %s\n",w,c?"ok":"FAIL"); if(!c)fails++; }

void* kmalloc(unsigned long n){ void* p=0; if(posix_memalign(&p,64,n?n:1))return 0; return p; }
void kfree(void* p){ (void)p; }
char* scopy(char* p,const char* s){ while(*s)*p++=*s++; return p; }
char* udec(char* p,u32 v){ char t[12]; int n=0; do{ t[n++]=(char)('0'+v%10); v/=10; }while(v); while(n)*p++=t[--n]; return p; }
char* hx(char* p,u32 v,int dg){ static const char H[]="0123456789ABCDEF"; for(int i=dg-1;i>=0;i--)*p++=H[(v>>(i*4))&15]; return p; }
int seq(const char* a,const char* b){ while(*a&&*a==*b){ a++; b++; } return *a==*b; }
static int g_quiet=1; void klog(const char* s){ if(!g_quiet)printf("    | %s\n",s); }
u64 hr_ns(void){ struct timespec t; clock_gettime(CLOCK_MONOTONIC,&t); return (u64)t.tv_sec*1000000000ULL+(u64)t.tv_nsec; }
void hr_delay_us(u32 us){ (void)us; }
long xzk_decode_into(const u8* in,long n,u8* out,long cap){ (void)in; (void)n; (void)out; (void)cap; return -1; }
u64 kva_to_pa(u64 va){ return va; }
int fs_find_name(const char* nm){ (void)nm; return -1; }
char* fsd(int k){ (void)k; return 0; }
static int g_saved=0; int fs_save_bin(const char* n,const u8* d,int len,int f){ (void)n; (void)d; (void)f; g_saved=len; return 0; }
void nvx3_sync_all(void){}
int msd_ok=0, ufat_ok=0; int msd_autoinit_safe(void){ return 0; } static int g_usbw=0; int ufat_write_file(const char* n,const char* d,int len){ (void)n; (void)d; g_usbw=len; return -1; }
int nv_probe(void){ return 1; }
void nv_read_id(void){ g_nv_boot0=0x168000A1u; }

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
    e=rd64(VR+t+((va>>12)&511)*8); if(!(e&1))return ~0ULL; *sys=((e>>1)&3)==2; return ((e&~0xFFULL)<<4)+(va&0xFFF); }
static u8* vptr(u64 pd3,u64 va){ int s=0; u64 p=walk(pd3,va,&s); if(p==~0ULL)return 0; return s?(u8*)p:VR+p; }
static u64 SIM_BAR1PD, SIM_BAR2PD;
static int sim_bars=0; static int sim_drop_next=0; static int disp_alloc=0, disp_inst_ok=0, disp_inst_n=0;
static void sim_edid(u8* e){ memset(e,0,128); static const u8 h[8]={0,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0}; memcpy(e,h,8); e[8]=0x10; e[9]=0xAC; e[10]=0x34; e[11]=0x12; e[18]=1; e[19]=3;
    u8* d=e+54; d[0]=0x02; d[1]=0x3A; d[2]=0x80; d[3]=0x18; d[4]=0x71; d[5]=0x38; d[6]=0x2D; d[7]=0x40;
    u8* n=e+72; n[3]=0xFC; memcpy(n+5,"SIM MONITOR\n ",13);
    u8 s=0; for(int i=0;i<127;i++)s=(u8)(s+e[i]); e[127]=(u8)(0x100-s); }
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

static struct { u32 chid, et, runl, sched, promoted, cls[4]; int ncls; u64 inst, userd, gpva; u32 hc; } CH[16]; static int NCH=0;
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
    if(fn==10){ msg_post(10,p,n,0); return; }
    if(fn==47){ msg_post(47,p,n,0); REG[0x110040/4]=0x80000000u; return; }
    if(fn==103){ u32 hc=*(u32*)p, hp=*(u32*)(p+4), ho=*(u32*)(p+8), cls=*(u32*)(p+12), ps=*(u32*)(p+20); u8* pr=p+0x20; u32 st=0;
        u32 want= cls==0?108:cls==0x80?56:cls==0x2080?4:cls==0x90F1?48:cls==0xC46F?360:cls==0xC5B5?8:0; if(cls==0x73){ disp_alloc++; if(!disp_inst_ok)alloc_bad++,st=0x1F; }
        if(ps!=want){ alloc_bad++; st=0x1F; }
        if(cls==0&&(hc!=ho||hp!=ho||*(u32*)pr!=hc))alloc_bad++,st=0x1F;
        if(cls==0xC46F){ u32 chid=ho&0xFFFFu, fl=*(u32*)(pr+0x14); if(((fl>>8)&7)!=chid%8||((fl>>12)&0x1FF)!=chid/8||!(fl&(1u<<21)))alloc_bad++,st=0x1F;
            CH[NCH].chid=chid; CH[NCH].et=*(u32*)(pr+0x80); CH[NCH].inst=rd64(pr+0x90); CH[NCH].userd=rd64(pr+0xA8); CH[NCH].gpva=rd64(pr+8); CH[NCH].hc=hc; CH[NCH].runl=CH[NCH].et==11?8:0; CH[NCH].ncls=0; CH[NCH].sched=0; CH[NCH].promoted=0; NCH++; }
        if(cls==0xC597||cls==0xC5C0||cls==0x902D){ int c=chan_find(hp); if(c<0||!CH[c].promoted||CH[c].et!=1)alloc_bad++,st=0x1F; else CH[c].cls[CH[c].ncls++]=cls; }
        if(cls==0xC5B5){ int c=chan_find(hp); if(c<0||*(u32*)pr!=1||*(u32*)(pr+4)!=CH[c].et)alloc_bad++,st=0x1F; else CH[c].cls[CH[c].ncls++]=cls; }
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
        else if(cmd==0x20800A49u){ if(ps!=24||hc!=0xC1E00004u||*(u32*)(pr+16)!=2||rd64(pr+8)!=0x10000||(rd64(pr)&0xFFFF))ctrl_bad++; else { disp_inst_ok=1; disp_inst_n++; } }
        else if(cmd==0x730102u){ *(u32*)(pr+8)=4; }
        else if(cmd==0x730287u){ *(u32*)(pr+4)=0xF; }
        else if(cmd==0x730120u){ if(ps!=12)ctrl_bad++; *(u32*)(pr+4)=0x700; }
        else if(cmd==0x730122u){ if(ps!=16)ctrl_bad++; *(u32*)(pr+8)&=0x100; }
        else if(cmd==0x73028Bu){ if(ps!=56)ctrl_bad++; u32 id=*(u32*)(pr+4); *(u32*)(pr+12)=2; *(u32*)(pr+16)=id==0x100?1:id==0x200?8:5; }
        else if(cmd==0x731140u){ *(u32*)(pr+8)=(1u<<8); }
        else if(cmd==0x730245u){ if(ps!=2064||*(u32*)(pr+4)!=0x100)ctrl_bad++; else { sim_edid(pr+16); *(u32*)(pr+8)=128; } }
        else if(cmd==0xC36F0108u){ int c=chan_find(ho); *(u32*)pr=c>=0?((CH[c].runl<<16)|CH[c].chid):0; }
        else { ctrl_bad++; st=0x56; }
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
static u8* s2d(u64 pd3,const u32* R,int dst,int x,int y){ u64 base=((u64)R[(dst?0x220:0x250)/4]<<32)|R[(dst?0x224:0x254)/4]; u32 pitch=R[(dst?0x214:0x244)/4]; u32 w=R[(dst?0x218:0x248)/4], h=R[(dst?0x21C:0x24C)/4];
    if(x<0||y<0||(u32)x>=w||(u32)y>=h||R[(dst?0x204:0x234)/4]!=1)return 0; return vptr(pd3,base+(u64)y*pitch+(u64)x*4); }
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
static void ce_exec(u32 token){
    int c=-1; for(int i=0;i<NCH;i++) if(CH[i].chid==(token&0xFFFF)&&CH[i].runl==(token>>16))c=i; if(c<0||!CH[c].sched)return;
    u64 pd3=rd64(VR+CH[c].inst+0x200)&~0xFFFULL; u32 put=*(u32*)(VR+CH[c].userd+0x8C), get=*(u32*)(VR+CH[c].userd+0x88);
    u32* R=R2D[c]; u32* HS=HOST[c]; if(cp_hang&&CH[c].et==1)return;
    while(get!=put){ u8* gp=vptr(pd3,CH[c].gpva+get*8); if(!gp)return; u32 e0=*(u32*)gp, e1=*(u32*)(gp+4); u64 pva=((u64)(e1&0xFF)<<32)|(e0&0xFFFFFFFCu); u32 nd=(e1>>10)&0x1FFFFF;
        u32* pb=(u32*)vptr(pd3,pva); if(!pb)return; u32 regs[0x800]; memset(regs,0,sizeof regs); static u32 obj[16][8];
        for(u32 i=0;i<nd;){ u32 h=pb[i++]; u32 cnt=(h>>16)&0x1FFF, mth=(h&0xFFF)<<2, sub=(h>>13)&7; if((h>>29)!=1)return;
            for(u32 k=0;k<cnt;k++){ u32 m=mth+k*4; u32 v=pb[i++];
                if(m<0x100&&m){ HS[m/4]=v;
                    if(m==0x6C&&(v&7u)==1u){ u8* sm=vptr(pd3,((u64)HS[0x60/4]<<32)|(HS[0x5C/4]&0xFFFFFFFCu)); if(sm&&(v&(1u<<20)))*(u32*)sm=HS[0x64/4]; host_sems++; }
                    if(m==0x34&&(v>>27)==0x10u)mem_ops++;
                    continue; }
                if(m==0){ obj[c][sub]=v; continue; }
                if(obj[c][sub]==0xC5C0u){ if(m<0x2000)CPR[c][m/4]=v; if(m==0x110)cp_wfi++; if(m==0x2BC)cp_launch(pd3,CPR[c]); continue; }
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
                if(m==0x300&&obj[c][sub]==0xC5B5&&(v&0x183u)==0x182u){ u64 s=((u64)regs[0x400/4]<<32)|regs[0x404/4], d=((u64)regs[0x408/4]<<32)|regs[0x40C/4]; u32 len=regs[0x418/4];
                    u32 lines=(v&0x200u)?regs[0x41C/4]:1, sp_=regs[0x410/4], dp_=regs[0x414/4];
                    if(v&0x400u){ u32 cm=regs[0x708/4]; if((cm&7u)!=4u||((cm>>16)&3u)!=3u||((cm>>20)&3u)||((cm>>24)&3u))return;
                        for(u32 l=0;l<lines;l++) for(u32 o=0;o<len;o++){ u8* dp=vptr(pd3,d+(u64)l*dp_+(u64)o*4); if(!dp)return; memcpy(dp,&regs[0x700/4],4); } ce_fills++; }
                    else for(u32 l=0;l<lines;l++) for(u32 o=0;o<len;o++){ u8* sp=vptr(pd3,s+(u64)l*sp_+o); u8* dp=vptr(pd3,d+(u64)l*dp_+o); if(!sp||!dp)return; *dp=*sp; }
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
    if(off==0x110080){ REG[off/4]=v; if(gsp_on&&!gsp_init)gsp_boot(); else if(resume_wait&&resumed&&(REG[0x111240/4]&1u)){ u8 z[4]={0}; resume_wait=0; msg_post(0x1001,z,4,0); } return; }
    if(off==0x110C00){ if(gsp_init)gsp_service(); return; }
    if(off==0xBB0090){ ce_exec(v); return; }
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

int main(int argc,char** argv){
    const char* fwdir=argc>1?argv[1]:"nvidia"; char p[512];
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
        chk("card memory is off by default (a CPU fallback must never touch PCIe memory)",vram_default_off);
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
    { g_nvd_auto=1; msd_ok=1; ufat_ok=1; ufs_n=1; strcpy(ufs[0].name,"NVSTART.TXT"); sim_uflag='1'; g_ticks=1000; nvd_poll(); g_ticks=2000; int ln=g_nv_log_n; nvd_poll();
      chk("no automatic start after boot when the last start-up never finished (flag on the USB stick)",g_nvd_auto_skipped==1&&g_nv_log_n==ln); msd_ok=0; ufat_ok=0; ufs_n=0; }
    { nvgsp_cmd("present on"); int on0=nvgsp_present_on(); int lr=nvgsp_cmd("all"); int still=nvgsp_present_on()&&strstr(g_nv_log,"already running")!=0;
      u8 rc[16]; memset(rc,0,sizeof rc); *(u32*)rc=1; *(u32*)(rc+4)=g_ch[1].chid+100; *(u32*)(rc+8)=31; msg_post(0x1004,rc,16,0); nvgsp_tick(); int foreign=nvgsp_present_on()&&g_rc_other==1;
      *(u32*)(rc+4)=g_ch[1].chid; msg_post(0x1004,rc,16,0); nvgsp_tick();
      chk("a channel error on a channel that is not ours is ignored (like nouveau's per-channel handling)",foreign);
      chk("nvgsp all on a running card starts nothing again; a channel error from GSP-RM turns every NVIDIA path off",on0&&lr==1&&still&&!nvgsp_present_on()&&!nvgsp_ce_ready()&&nvgsp_off_why()[0]&&!nv2d_ready()&&!nvc_ready()); }
    { char m0[120]; int rcref=nvgsp_on(m0,120)==0&&strstr(m0,"stopped one of our GPU channels")!=0; chk("after a channel error on our channel nvgsp on refuses to reuse that channel (nouveau kills it too)",rcref); g_rc_dead=0; }
    { char m[120]; int was=nvgsp_off(); int r1=nvgsp_on(m,120); int back=!nvgsp_off()&&nvgsp_ce_ready()&&nvgsp_present_on()&&nv2d_ready()&&nvc_ready()&&!nvgsp_off_why()[0];
      int r2=nvgsp_on(m,120); int again=r2==1&&strstr(m,"already on");
      chk("Ctrl+Alt+N / nvgsp on turns NVIDIA drawing back on without a restart: copy engine, 2D engine and compute shaders checked again",was&&r1==1&&back&&again);
      nvgsp_all_off("turned off with Ctrl+Alt+N"); u64 age=nvgsp_off_age_s(); g_nv_log_n=0; g_nv_log[0]=0; nvgsp_status(); int st=strstr(g_nv_log,"NVIDIA drawing: OFF - turned off with Ctrl+Alt+N")!=0;
      nvgsp_on(m,120); u32 keep=REG[0]; g_tick_live=0; nvgsp_tick(); int alive=!nvgsp_off(); REG[0]=0xFFFFFFFFu; g_tick_live=0; nvgsp_tick(); int gone=nvgsp_off()&&strstr(nvgsp_off_why(),"0xFFFFFFFF"); char m2[120]; int ron=nvgsp_on(m2,120); REG[0]=keep;
      chk("off reason and time in nvgsp status; liveness like Linux: only a 0xFFFFFFFF or wrong BOOT0 read (twice) means the card is gone, and nvgsp on refuses then",st&&age<5&&alive&&gone&&ron==0&&strstr(m2,"does not answer"));
      nvgsp_on(m,120);
      nv2d_reset(); g_auto_at=0; g_auto_tries=0; sc_off("test: read-back mismatch"); int sched=g_auto_at!=0&&!nvgsp_present_on(); g_last_input_t=g_ticks; g_auto_at=1; nvgsp_tick(); int waited=g_auto_tries==0&&g_auto_at>1&&!nvgsp_present_on(); g_last_input_t=0; g_ticks+=1000; g_auto_at=1; nvgsp_tick(); int back2=nvgsp_present_on()&&g_auto_tries==1&&!nvgsp_off();
      g_auto_tries=3; sc_off("test again"); int capped=g_auto_at==0; nvgsp_on(m,120); if(!nvgsp_present_on())nvgsp_present_set(1);
      chk("a GPU timeout or bad read-back schedules one automatic re-check (only while input is idle) that brings the path back",sched&&waited&&back2&&capped); }
    { int a0=disp_alloc; nvl_reset(); int rm=nvgsp_monitors(); const nvgsp_mon_t* ml=0; u32 hh=0,hm=0; int mn=nvgsp_mon_get(&ml,&hh,&hm,0);
      int good=rm==1&&mn==3&&hh==4&&hm==0xF&&disp_alloc==a0+1&&ml[0].connected&&ml[0].edid_ok&&!strcmp(ml[0].name,"SIM MONITOR")&&!strcmp(ml[0].maker,"DEL")&&ml[0].nmodes==1&&ml[0].mw[0]==1920&&ml[0].mh[0]==1080&&ml[0].mhz[0]==60
        &&!strcmp(nvgsp_mon_sig(&ml[0]),"HDMI")&&!ml[1].connected&&!strcmp(nvgsp_mon_sig(&ml[1]),"DisplayPort")&&strstr(g_nv_log,"1 monitor(s) connected")!=0;
      int again=nvgsp_monitors()==1&&disp_alloc==a0+2&&disp_inst_n==1;
      chk("monitors like nouveau's r535_disp_oneinit: display instance memory, NV04_DISPLAY_COMMON, connected outputs, HDMI/DP type and EDID name and native mode",good&&again); }
    { int a0=alloc_bad; g_rpc_dead=0; nvl_reset(); sim_drop_next=1; u32 x[3]={0,0,0}; int t1=rm_ctrl(HC(1),HDEV,0x730120u,x,12); int t2=rm_ctrl(HC(1),HDEV,0x730120u,x,12); int dead=g_rpc_dead;
      chk("a GSP-RM reply that does not come within 5 s stops all further requests (a late reply is never taken for the next one)",t1!=0&&t2==-1003&&dead&&strstr(g_nv_log,"did not answer")!=0&&alloc_bad==a0); g_rpc_dead=0; sim_drop_next=0; }
    sim_mode=1; nvl_reset(); r=nvgsp_shutdown();
    chk("power off / restart unloads like nouveau's fini: UNLOADING_GUEST_DRIVER, FWSEC-SB, booter_unload, WPR2 cleared",r>0&&booter_ok&&fwsec_ok&&G.state==0&&nvgsp_shutdown()==1);
    { nv_fail(9,"test reason"); int ln=g_nv_log_n; nvgsp_cmd("log"); int kept=g_nv_log_n==ln&&strstr(g_nv_log,"test reason")!=0; nvgsp_cmd("status");
      chk("nvgsp log keeps the last log, nvgsp status keeps the failure reason",kept&&strstr(g_nv_log,"last failure: step 9 - test reason")!=0); }
    if(fails){ g_quiet=0; printf("--- driver log ---\n%s\n",g_nv_log); }
    printf(fails?"FAILURES\n":"ALL PASS\n"); return fails?1:0; }
