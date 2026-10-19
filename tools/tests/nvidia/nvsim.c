#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <time.h>
#include <unistd.h>
#include <sys/mman.h>

static unsigned char g_bootinfo[64];
#define NV_BOOTINFO ((unsigned long long)(unsigned long)g_bootinfo)
#include "drivers/video/nvidia/nvmem.c"
#include "drivers/video/nvidia/nvflcn.c"
#include "drivers/video/nvidia/nvgsp.c"

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
u64 acpi_hi_map(u64 pa){ (void)pa; return 0; }
u64 kva_to_pa(u64 va){ return va; }
int fs_find_name(const char* nm){ (void)nm; return -1; }
char* fsd(int k){ (void)k; return 0; }
static int g_saved=0; int fs_save_bin(const char* n,const u8* d,int len,int f){ (void)n; (void)d; (void)f; g_saved=len; return 0; }
void nvx3_sync_all(void){}
int nv_probe(void){ return 1; }
void nv_read_id(void){ g_nv_boot0=0x168000A1u; }

static u64 BAR1=0xE0000000ULL, BAR3=0xF0000000ULL;
u32 pci_cfg(int b,int s,int f,int off){ (void)b; (void)s; (void)f;
    static u32 sz[6]={0}; static int sizing[6]={0};
    int i=(off-0x10)/4; if(off==0x04)return 0x00100007u;
    if(i>=0&&i<6&&sizing[i]){ sizing[i]=0; u32 v=sz[i]; return v; }
    if(off==0x14)return (u32)BAR1|0xCu; if(off==0x18)return (u32)(BAR1>>32); if(off==0x1C)return (u32)BAR3|0xCu; if(off==0x20)return (u32)(BAR3>>32);
    (void)sz; return 0; }
void pci_cfgw(int b,int s,int f,int off,u32 v){ (void)b; (void)s; (void)f; (void)off; (void)v; }

static u32* REG; static u8* VR; static u64 VRSZ=6ULL<<30;
static u8* ROM; static u32 ROMSZ=0x40000;
static struct { u32 imem[0x10000/4]; u32 tags[256]; u8 sec[256]; u32 dmem[0x20000/4]; u32 ic, dc; } FAL[2];
static int fi(u32 f){ return f==NVF_SEC2?1:0; }
static int sim_mode=0, gsp_on=0;
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

static void msg_post(u32 fn,const void* pl,u32 plen,u32 result){
    u8 m[0x10000]; u32 rlen=0x20+plen; u32 len=(0x30+rlen+0xFFF)&~0xFFFu; memset(m,0,len);
    *(u32*)(m+0x24)=gsp_seq++; *(u32*)(m+0x28)=len/0x1000; *(u32*)(m+0x30)=0x03000000u; *(u32*)(m+0x34)=0x43505256u; *(u32*)(m+0x38)=rlen; *(u32*)(m+0x3C)=fn; *(u32*)(m+0x40)=result;
    memcpy(m+0x50,pl,plen); u64 cs=0; for(u32 i=0;i<len;i+=8)cs^=rd64(m+i); *(u32*)(m+0x20)=(u32)(cs>>32)^(u32)cs;
    for(u32 o=0;o<len;o+=0x1000){ memcpy(sim_shm+MSGQ+0x1000+gsp_msg_wp*0x1000,m+o,0x1000); gsp_msg_wp=(gsp_msg_wp+1)%QCNT; }
    *(u32*)(sim_shm+MSGQ+0x10)=gsp_msg_wp; }

static struct { u32 chid, et, runl, sched, promoted, cls[4]; int ncls; u64 inst, userd, gpva; u32 hc; } CH[16]; static int NCH=0;
static u64 vas_pd3[4]; static int bar_pde_ok=0, reserved_pdes_ok=0, alloc_bad=0, ctrl_bad=0, sysinfo_ok=0, reg_ok=0, cks_bad=0, seq_bad=0, ce_done=0;
static u32 cmd_expect_seq=0;
static u64 SIM_BAR1PD, SIM_BAR2PD;
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
        u32 want= cls==0?108:cls==0x80?56:cls==0x2080?4:cls==0x90F1?48:cls==0xC46F?360:cls==0xC5B5?8:0;
        if(ps!=want){ alloc_bad++; st=0x1F; }
        if(cls==0&&(hc!=ho||hp!=ho||*(u32*)pr!=hc))alloc_bad++,st=0x1F;
        if(cls==0xC46F){ u32 chid=ho&0xFFFFu, fl=*(u32*)(pr+0x14); if(((fl>>8)&7)!=chid%8||((fl>>12)&0x1FF)!=chid/8||!(fl&(1u<<21)))alloc_bad++,st=0x1F;
            CH[NCH].chid=chid; CH[NCH].et=*(u32*)(pr+0x80); CH[NCH].inst=rd64(pr+0x90); CH[NCH].userd=rd64(pr+0xA8); CH[NCH].gpva=rd64(pr+8); CH[NCH].hc=hc; CH[NCH].runl=CH[NCH].et==1?0:1; CH[NCH].ncls=0; CH[NCH].sched=0; CH[NCH].promoted=0; NCH++; }
        if(cls==0xC597||cls==0xC5C0){ int c=chan_find(hp); if(c<0||!CH[c].promoted||CH[c].et!=1)alloc_bad++,st=0x1F; else CH[c].cls[CH[c].ncls++]=cls; }
        if(cls==0xC5B5){ int c=chan_find(hp); if(c<0||*(u32*)pr!=1||*(u32*)(pr+4)!=CH[c].et)alloc_bad++,st=0x1F; else CH[c].cls[CH[c].ncls++]=cls; }
        u8 r[0x1000]; memcpy(r,p,0x20+ps); *(u32*)(r+16)=st; msg_post(103,r,0x20+ps,0); return; }
    if(fn==76){ u32 hc=*(u32*)p, ho=*(u32*)(p+4), cmd=*(u32*)(p+8), ps=*(u32*)(p+16); u8* pr=p+0x18; u32 st=0; (void)hc;
        if(cmd==0x20801112u){ if(ps!=3212)ctrl_bad++; memset(pr,0,ps); *(u32*)(pr+4)=2; u8* e=pr+12; *(u32*)(e+8)=1; *(u32*)(e+12)=0; strcpy((char*)e+84,"GRAPHICS"); e+=100; *(u32*)(e+8)=9; *(u32*)(e+12)=1; strcpy((char*)e+84,"COPY0"); }
        else if(cmd==0x20802A08u){ *(u32*)pr=0x1000; }
        else if(cmd==0x20800A32u){ if(ps!=1600)ctrl_bad++; memset(pr,0,ps); u32 sz[][2]={{0x00,0x20000},{0x10,0x10000},{0x11,0x30000},{0x0D,0x80000},{0x13,0x2D000},{0x14,0x40000},{0x17,0x1000},{0x18,0x3000}}; for(int i=0;i<8;i++)*(u32*)(pr+sz[i][0]*8)=sz[i][1]; }
        else if(cmd==0x2080012Bu){ if(ps!=560)ctrl_bad++; u32 cnt=*(u32*)(pr+40); int c=chan_find(*(u32*)(pr+16)); int good=c>=0&&cnt>0&&*(u32*)pr==1;
            for(u32 k=0;k<cnt&&good;k++){ u8* e=pr+48+k*32; if(e[30]&&(rd64(e)>=VRSZ||!rd64(e+16)||*(u32*)(e+24)!=4))good=0; if(!e[31]&&!rd64(e+8))good=0; }
            if(good)CH[c].promoted=1; else { ctrl_bad++; st=0x1F; } }
        else if(cmd==0x90F10106u){ if(ps!=184)ctrl_bad++; u64 pd3=rd64(pr+40), pd2=rd64(pr+64), pd1=rd64(pr+88);
            reserved_pdes_ok=(rd64(pr+16)==0x100000000ULL&&*(u32*)(pr+32)==3&&((rd64(VR+pd3)&~0xFFULL)<<4)==pd2&&((rd64(VR+pd2)&~0xFFULL)<<4)==pd1&&pr[60]==47&&pr[84]==38&&pr[108]==29);
            *(u64*)(VR+pd1+8*8)=(0x7F000000ULL>>4)|2; }
        else if(cmd==0x00801813u){ if(ps!=32||*(u32*)(pr+8)!=4)ctrl_bad++; vas_pd3[1]=rd64(pr); }
        else if(cmd==0xA06F0104u){ int c=chan_find(ho); if(c<0||*(u32*)pr!=CH[c].et)ctrl_bad++; }
        else if(cmd==0xA06F0103u){ int c=chan_find(ho); if(c<0||pr[0]!=1)ctrl_bad++; else CH[c].sched=1; }
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
    u32 sq[64]; memset(sq,0,sizeof sq); sq[1]=8; u32* c=sq+10; c[0]=0; c[1]=0x12340; c[2]=0xBEEF; c[3]=4; c[4]=0x12340; c[5]=2; c[6]=3; c[7]=5; msg_post(0x1002,sq,40+8*4,0);
    u8 el[268]; memset(el,0,sizeof el); *(u32*)el=79; strcpy((char*)el+12,"simulated GSP error log entry"); msg_post(0x1006,el,sizeof el,0);
    msg_post(0x1001,el,4,0); gsp_init=1; }
static u32 ce_lines=0;
static void ce_exec(u32 token){
    int c=-1; for(int i=0;i<NCH;i++) if(CH[i].chid==(token&0xFFFF)&&CH[i].runl==(token>>16))c=i; if(c<0||!CH[c].sched)return;
    u64 pd3=rd64(VR+CH[c].inst+0x200)&~0xFFFULL; u32 put=*(u32*)(VR+CH[c].userd+0x8C), get=*(u32*)(VR+CH[c].userd+0x88);
    while(get!=put){ u8* gp=vptr(pd3,CH[c].gpva+get*8); if(!gp)return; u32 e0=*(u32*)gp, e1=*(u32*)(gp+4); u64 pva=((u64)(e1&0xFF)<<32)|(e0&0xFFFFFFFCu); u32 nd=(e1>>10)&0x1FFFFF;
        u32* pb=(u32*)vptr(pd3,pva); if(!pb)return; u32 regs[0x800]; memset(regs,0,sizeof regs); u32 obj=0;
        for(u32 i=0;i<nd;){ u32 h=pb[i++]; u32 cnt=(h>>16)&0x1FFF, mth=(h&0xFFF)<<2; if((h>>29)!=1)return; for(u32 k=0;k<cnt;k++){ u32 m=mth+k*4; u32 v=pb[i++]; if(m==0)obj=v; else if(m<0x2000)regs[m/4]=v;
            if(m==0x300&&obj==0xC5B5&&(v&0x187u)==0x186u){ u64 s=((u64)regs[0x400/4]<<32)|regs[0x404/4], d=((u64)regs[0x408/4]<<32)|regs[0x40C/4]; u32 len=regs[0x418/4];
                u32 lines=(v&0x200u)?regs[0x41C/4]:1, sp_=regs[0x410/4], dp_=regs[0x414/4];
                for(u32 l=0;l<lines;l++) for(u32 o=0;o<len;o++){ u8* sp=vptr(pd3,s+(u64)l*sp_+o); u8* dp=vptr(pd3,d+(u64)l*dp_+o); if(!sp||!dp)return; *dp=*sp; }
                if(lines>1)ce_lines+=lines;
                if(((v>>3)&3u)==1u){ u8* sm=vptr(pd3,((u64)regs[0x240/4]<<32)|regs[0x244/4]); if(sm)*(u32*)sm=regs[0x248/4]; }
                ce_done=1; } } }
        get=(get+1)%512; *(u32*)(VR+CH[c].userd+0x88)=get; } }

static void dmemw(int k,u32 v){ u32 a=(FAL[k].dc&0xFFFC)|(((FAL[k].dc>>8)&0xFF)<<8); FAL[k].dmem[(a/4)%(0x20000/4)]=v; FAL[k].dc=(FAL[k].dc&~0xFFFFu)|((a+4)&0xFFFF); }
static void imemw(int k,u32 v){ u32 a=FAL[k].ic&0xFFFF; FAL[k].imem[(a/4)%(0x10000/4)]=v; if((a&0xFF)==0){ FAL[k].sec[(a>>8)&0xFF]=(FAL[k].ic>>28)&1; } FAL[k].ic=(FAL[k].ic&~0xFFFFu)|((a+4)&0xFFFF); }
static u32 last_tag[2];
static int fwsec_ok=0, bl_ok=0, booter_ok=0;
static void falcon_start(u32 f){
    int k=fi(f); REG[(f+0x100)/4]|=0x10;
    if(k==0&&!gsp_on){ u32 lim=(REG[(f+0x108)/4]&0x1FF)<<8; bl_ok=FAL[0].tags[(lim-0x200)>>8]==0xFD&&REG[(f+0x104)/4]==0xFD00u&&(REG[(f+0x610)/4]&7)==5;
        u8* d=(u8*)FAL[0].dmem; u64 code=rd64(d+0x24), data=rd64(d+0x40); u8* dm=(u8*)data;
        u32 mapper=*(u32*)(dm+0x10+4+8+4); u32 cin=*(u32*)(dm+mapper+8); u32 icmd=*(u32*)(dm+mapper+0x2C); u8* cmdp=dm+cin;
        fwsec_ok=code&&*(u32*)(d+0x20)==4&&data==code+0x200&&icmd==(sim_mode?0x19u:0x15u)&&*(u32*)cmdp==1&&*(u32*)(cmdp+4)==24&&*(u32*)(cmdp+0x14)==2;
        if(!sim_mode)fwsec_ok=fwsec_ok&&*(u32*)(cmdp+0x18)==1&&*(u32*)(cmdp+0x20)==(u32)(g_nvl.frts_addr>>12)&&*(u32*)(cmdp+0x24)==0x100&&*(u32*)(cmdp+0x28)==2;
        if(!sim_mode){ REG[0x1FA824/4]=(u32)((g_nvl.frts_addr>>12)<<4); REG[0x1FA828/4]=(u32)(((g_nvl.frts_addr+g_nvl.frts_size)>>12)<<4); }
        REG[0x1438/4]=0; REG[0x1454/4]=0; REG[(f+0x40)/4]=0; return; }
    if(k==1){ u64 meta=(u64)REG[(f+0x40)/4]|((u64)REG[(f+0x44)/4]<<32); u8* dm=(u8*)FAL[1].dmem;
        if(sim_mode){ booter_ok=REG[(f+0x40)/4]==0xFF; REG[0x1FA828/4]=0; REG[(f+0x40)/4]=0; return; }
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
        if(off==f+0x100&&(v&2)){ REG[off/4]=v&~2u; falcon_start(f); return; } if(off==f+0x3C0){ REG[off/4]=v; return; } }
    if(off==0xB830B0){ REG[off/4]=v&~0x80000000u; return; }
    if(off==0x110080){ REG[off/4]=v; if(gsp_on&&!gsp_init)gsp_boot(); return; }
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
    nvl_reset(); r=gsp_post();
    chk("GSP-RM: INIT_DONE received, static info parsed (name, 3 GPCs / 9 TPCs, 2 FB regions)",r>0&&G.state==3&&G.tpcs==9&&G.nfbr==2&&!strncmp(G.name,"NVIDIA GeForce GTX 1660",23));
    chk("queued SET_SYSTEM_INFO (BAR0/1/3, config mirror) and SET_REGISTRY (117 bytes) are exact",sysinfo_ok&&reg_ok);
    chk("CPU sequencer event executed (register write + store), GSP error log printed",REG[0x12340/4]==0xBEEF&&G.seq_count==1&&G.err_count>=1);
    chk("VRAM allocator handed the usable FB region below the GSP reservation",nvv_total()>0&&nvv_total()<=g_nvl.nonwpr_addr);
    nvl_reset(); r=bars(); int s2=0,s1=0; u64 w2=walk(g_bar2.pd3,0x1234,&s2), w1=walk(SIM_BAR1PD,g_bva1+0x10,&s1);
    chk("BAR2: page tables map VA 0 to the test page, UPDATE_BAR_PDE sent with PD3[0] / shift 47",bar_pde_ok&&w2==g_bt2+0x1234&&!s2);
    chk("BAR1: tables installed under GSP's root PD map the test window to VRAM",w1==g_bt1+0x10&&!s1);
    G.state=4; nvl_reset(); r=objects();
    chk("golden context: VA space with server-reserved PDEs (4 GB, 3 levels), promote, TURING_A",reserved_pdes_ok&&G.state>=5);
    chk("client, device, subdevice, FERMI_VASPACE_A, SET_PAGE_DIRECTORY on the host PD3",vas_pd3[1]!=0);
    chk("GR channel + TURING_A + TURING_COMPUTE_A, CE channel + TURING_DMA_COPY_A, all params exact",alloc_bad==0&&ctrl_bad==0&&NCH==3);
    chk("copy engine: pushbuffer through GPFIFO and doorbell copies 4 KB via the GPU page tables",r>0&&ce_done&&G.state==7);
    chk("every command-queue element has a valid checksum and sequence number",cks_bad==0&&seq_bad==0&&cmd_expect_seq>10);
    { int W=300,H=170,SP=1024; u32* src=malloc(W*H*4); u32* dst=calloc((size_t)SP*200,4); for(int i=0;i<W*H;i++)src[i]=0x10000u*i+7u;
      u64 sv=nvgsp_ce_map((u64)src,(u64)W*H*4), dv=nvgsp_ce_map((u64)dst,(u64)SP*200*4); u32 lb=ce_lines;
      int ok=sv&&dv&&nvgsp_ce_copy2d(sv,W*4,dv+(u64)(10*SP+20)*4,SP*4,W*4,H);
      int same=1; for(int y=0;y<H&&same;y++) for(int x=0;x<W;x++) if(dst[(y+10)*SP+20+x]!=src[y*W+x]){ same=0; break; }
      int untouched=dst[9*SP+20]==0&&dst[10*SP+19]==0&&dst[10*SP+20+W]==0&&dst[(10+H)*SP+20]==0;
      int ok2=nvgsp_ce_copy2d(sv,W*4,dv,SP*4,W*4,2); u32 cp=0,to=0; nvgsp_ce_stats(&cp,&to);
      chk("copy engine 2D copy: 300x170 pixels into a pitched frame, completion semaphore, nothing outside touched",ok&&same&&untouched&&ok2&&ce_lines-lb==H+2&&cp==2&&to==0); free(src); free(dst); }
    sim_mode=1; nvl_reset(); r=unload();
    chk("unload: UNLOADING_GUEST_DRIVER, FWSEC-SB, booter_unload, WPR2 cleared",r>0&&booter_ok&&fwsec_ok&&G.state==0);
    { nv_fail(9,"test reason"); int ln=g_nv_log_n; nvgsp_cmd("log"); int kept=g_nv_log_n==ln&&strstr(g_nv_log,"test reason")!=0; nvgsp_cmd("status");
      chk("nvgsp log keeps the last log, nvgsp status keeps the failure reason",kept&&strstr(g_nv_log,"last failure: step 9 - test reason")!=0); }
    if(fails){ g_quiet=0; printf("--- driver log ---\n%s\n",g_nv_log); }
    printf(fails?"FAILURES\n":"ALL PASS\n"); return fails?1:0; }
