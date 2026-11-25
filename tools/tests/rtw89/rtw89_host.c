static int t_fb=0, t_unsup=0; int wifi_feat_begin(int f){ (void)f; return t_fb; } void wifi_feat_ok(int f){ (void)f; } void wifi_feat_unsup(int f,const char* why){ (void)f; (void)why; t_unsup++; }
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include "drivers/net/wifi/realtek/rtw89/rtw89.h"
#include "include/nvx/drivers/net/wifi/realtek/rtw89/rtw89_chan.h"
#include "include/nvx/drivers/net/wifi/realtek/rtw89/rtw89_sta.h"
#include "include/nvx/drivers/net/wifi/realtek/rtw89/rtw89_mac.h"
#include "include/nvx/drivers/net/wifi/realtek/rtw89/rtw89_core.h"
#include "include/nvx/net/wifi/wifi_hw.h"
#include "include/nvx/drivers/bus/pci.h"
#include "include/nvx/fs/vfs/vfs.h"
static const char* FWDIR;
u16 pci_ven[PCI_MAX], pci_dev[PCI_MAX]; u8 pci_cls[PCI_MAX], pci_sub[PCI_MAX], pci_bus[PCI_MAX], pci_slot[PCI_MAX]; int pci_n; static u32 bar2=0xfc000004u, pcicmd;
void pci_scan(void){}
u32 pci_cfg(int b,int s,int f,int off){ (void)b;(void)s;(void)f; return off==0x10?0xe001u:off==0x18?bar2:off==0x04?pcicmd:0; }
void pci_cfgw(int b,int s,int f,int off,u32 v){ (void)b;(void)s;(void)f; if(off==4)pcicmd=v; }
void klog(const char* s){ printf("klog: %s\n",s); }
const char* regdb_country(void){ return "00"; }
int regdb_max_dbm(u8 ch){ (void)ch; return 0; }
void* kmalloc(unsigned long n){ return malloc(n); }
void kfree(void* p){ free(p); }
void hr_delay_us(u32 us){ (void)us; }
static int hide_fw;
static const char* hpath(const char* p,char* o){ if(strncmp(p,"/lib/firmware/",14)||hide_fw)return 0; snprintf(o,512,"%s/%s",FWDIR,p+14); return o; }
static int h_size(const char* p){ char b[512]; if(!hpath(p,b))return -1; FILE* f=fopen(b,"rb"); if(!f)return -1; fseek(f,0,SEEK_END); int n=(int)ftell(f); fclose(f); return n; }
static int h_read(const char* p,char* buf,int off,int len){ char b[512]; if(!hpath(p,b))return -1; FILE* f=fopen(b,"rb"); if(!f)return -1; fseek(f,off,SEEK_SET); int n=(int)fread(buf,1,(size_t)len,f); fclose(f); return n; }
static const vfs_fs_ops HFS={"host",0,h_read,h_size};
const vfs_fs_ops* vfs_lookup(const char* p){ (void)p; return &HFS; }
static int registered;
void* dma_alloc_pages(unsigned long pg){ (void)pg; return 0; } void wifi_hw_rx(const u8* f,int l,int r){ (void)f;(void)l;(void)r; }
int wifi_hw_assoc_ie(u8* o,int c){ (void)o;(void)c; return 0; }
int wifi_hw_register(const struct wifi_hw_ops* o,void* c){ (void)o;(void)c; registered++; return 0; }

static u8 regs[0x20010]; static u8 sim_ef[1216]; static int sim_ef_dead, sim_fw_fail, sim_fw_noboot; static int sim_rdy, sim_dead, nw, nrd; static u32 wl[256][3]; static u32 delays;
static u32 sim_rd(struct rtw89_softc* sc,u32 o,int w){ (void)sc; nrd++; u32 v=0; for(int i=0;i<w;i++)v|=(u32)regs[o+i]<<(8*i); if(o==R_AX_SYS_PW_CTRL&&sim_rdy)v|=B_AX_RDY_SYSPWR; if((o==0x8D00||o==0x9100)&&!sim_dead)v|=3; if(o==0x9E10&&w==4&&!sim_dead)v|=1u<<31; return v; }
static void sim_put(u32 o,u32 v){ for(int i=0;i<4;i++)regs[o+i]=(u8)(v>>(8*i)); }
static u32 g32(u32 o){ return (u32)regs[o]|((u32)regs[o+1]<<8)|((u32)regs[o+2]<<16)|((u32)regs[o+3]<<24); }
static void sim_wr(struct rtw89_softc* sc,u32 o,u32 v,int w){ (void)sc; if(nw<256){ wl[nw][0]=o; wl[nw][1]=v; wl[nw][2]=(u32)w; } nw++;
    for(int i=0;i<w;i++)regs[o+i]=(u8)(v>>(8*i));
    if(sim_dead)return;
    if(o==0x30&&!sim_ef_dead){ u32 a=(v>>16)&0x7FF; sim_put(0x30,(v&~0xFFu)|(1u<<29)|(a<1216?sim_ef[a]:0xFF)); }
    if(o==0x88&&(v&2)&&(g32(0x1E0)&1)&&!sim_fw_noboot)sim_put(0x1E0,g32(0x1E0)|2);
    if(o==R_AX_SYS_PW_CTRL)sim_put(o,v&~B_AX_APFN_ONMAC);
    if(o==R_AX_WLAN_XTAL_SI_CTRL)sim_put(o,v&~B_AX_WL_XTAL_SI_CMD_POLL);
    if(o==0xCE34)sim_put(o,v&~0x100u); }
static void sim_delay(u32 us){ delays+=us; if(!sim_dead)sim_rdy=1; }
static const struct rtw89_io SIM={ sim_rd,sim_wr,sim_delay };
static void ef_blk(u32* p,u32 log,u8 we,const u8* d,int n){ u32 b=log>>3; sim_ef[(*p)++]=(u8)(0x30|(b>>4)); sim_ef[(*p)++]=(u8)(((b&0xF)<<4)|we); for(int i=0;i<n;i++)sim_ef[(*p)++]=d[i]; }
static void sim_ef_init(void){ memset(sim_ef,0xFF,sizeof sim_ef); u32 p=4; static const u8 mac[6]={0x00,0xe0,0x4c,0x88,0x52,0xbe}, rfe[4]={0x01,'S','E',0xFF}, xt[2]={0x00,0x3F};
    ef_blk(&p,0x400,0x8,mac,6); ef_blk(&p,0x2C8,0x9,rfe,4); ef_blk(&p,0x2B8,0xE,xt,2); }
static void sim_reset(void){ memset(regs,0,sizeof regs); sim_ef_init(); sim_ef_dead=0; sim_fw_fail=0; sim_fw_noboot=0; sim_put(0x0A,1u<<5); nw=0; nrd=0; delays=0; sim_dead=0; sim_rdy=0; sim_put(R_AX_SYS_CFG1,0x1000u); }

static const u32 EXP[][3]={
    {0x228,0x4F42,2},{0x4,0,4},{0x4,0x40000,4},{0x90,2,4},{0x4,0x40000,4},{0x4,0x40000,4},{0x20,0x800000,4},{0x400,1,4},{0x400,0x31,4},
    {0x4,0x70000,4},{0x4,0x70100,4},{0x88,1,1},{0x88,0,1},{0x88,1,1},{0x88,0,1},{0x88,1,1},{0x70,0,4},{0x18,0x40,4},
    {0x270,0x80404090u,4},{0x18,0x60,4},{0x270,0x80202090u,4},{0x270,0x80040490u,4},{0x270,0x80080890u,4},{0x270,0x80100090u,4},
    {0x270,0x80010190u,4},{0x270,0x80020290u,4},{0x270,0x80800090u,4},{0x270,0x800200A1u,4},{0x270,0x80700024u,4},{0x270,0x800F0026u,4},
    {0xCC,4,4},{0x0,0x100,4},{0x0,0x100,4},{0x0,0x100,4},{0xCC,0,4},{0x8400,0x7FFF8000u,4},{0xC000,0x7000803Fu,4},{0x2D8,0x10,4} };

static void test_pwr_on(void){
    struct rtw89_softc sc; memset(&sc,0,sizeof sc); sc.io=&SIM; sim_reset();
    assert(rtw89_read_cv(&sc)==1);
    assert(rtw8852b_pwr_on(&sc)==0); assert(!sc.err);
    int n=(int)(sizeof EXP/sizeof EXP[0]);
    if(nw!=n)printf("writes %d expected %d\n",nw,n);
    for(int i=0;i<n&&i<nw;i++) if(wl[i][0]!=EXP[i][0]||wl[i][1]!=EXP[i][1]||wl[i][2]!=EXP[i][2]){ printf("write %d: got %x=%x/%u want %x=%x/%u\n",i,wl[i][0],wl[i][1],wl[i][2],EXP[i][0],EXP[i][1],EXP[i][2]); assert(0); }
    assert(nw==n); assert(sc.xtal_writes==11); assert(delays==2000);
    memset(&sc,0,sizeof sc); sc.io=&SIM; sim_reset(); sim_dead=1;
    assert(rtw8852b_pwr_on(&sc)<0); assert(sc.err&&strstr(sc.err,"system power")); assert(sc.err_reg==R_AX_SYS_PW_CTRL); assert(sc.polls==21); assert(delays==20000);
    printf("power-on sequence: %d writes match rtw8852b_pwr_on_func, dead chip times out after %u polls\n",n,sc.polls);
    memset(&sc,0,sizeof sc); sc.io=&SIM; sim_reset();
    sim_put(R_AX_WLAN_XTAL_SI_CTRL,0); assert(rtw89_write_xtal_si(&sc,0x90,1,1)==0);
    sim_dead=1; assert(rtw89_write_xtal_si(&sc,0x90,1,1)<0); assert(sc.polls==1+1001); }

static u8* load(const char* nm,u32* len){ char p[512]; snprintf(p,sizeof p,"%s/%s",FWDIR,nm); FILE* f=fopen(p,"rb"); if(!f)return 0; fseek(f,0,SEEK_END); long n=ftell(f); fseek(f,0,SEEK_SET); u8* b=malloc((size_t)n); assert(fread(b,1,(size_t)n,f)==(size_t)n); fclose(f); *len=(u32)n; return b; }
static void le32(u8* p,u32 v){ for(int i=0;i<4;i++)p[i]=(u8)(v>>(8*i)); }
static u32 mk_fw(u8* b,int nsec,const u32* sz,const u8* type,const u32* mssc,int ck){
    memset(b,0,32+16*nsec); le32(b+4,0x0f1d1d00u); le32(b+8,0xdeadbeef); le32(b+16,0x2a0e0b07u); le32(b+20,2024); le32(b+24,(u32)nsec<<8);
    u32 pos=32+16*(u32)nsec;
    for(int i=0;i<nsec;i++){ u8* s=b+32+16*i; le32(s,0xb8970000u+(u32)i); le32(s+4,sz[i]|((u32)type[i]<<24)|(ck?1u<<28:0)); le32(s+8,mssc[i]); pos+=sz[i]+(ck?8:0)+(type[i]==9?mssc[i]*512:0); }
    return pos; }
static void test_fw(void){
    u32 len=0; u8* d=load("rtw89/rtw8852b_fw-1.bin",&len); assert(d);
    struct rtw89_fw_info fi; memset(&fi,0,sizeof fi);
    assert(rtw89_fw_parse(d,len,1,&fi)==0);
    assert(fi.major==0&&fi.minor==29&&fi.sub==29&&fi.idx==15&&fi.cv==1&&fi.type==RTW89_FW_NORMAL_CE&&fi.nsec==3&&fi.dyn&&fi.hdr_len==144);
    assert(fi.sec[0].type==2&&fi.sec[1].type==1&&fi.sec[2].type==9&&fi.sec[0].dladdr==(0xb8970000u&0x1fffffffu)&&fi.sec[2].off==144+fi.sec[0].len+fi.sec[1].len);
    printf("rtw8852b_fw-1.bin: v%u.%u.%u.%u %u-%u-%u, CV%u image at 0x%x size %u, %d sections\n",fi.major,fi.minor,fi.sub,fi.idx,fi.year,fi.month,fi.day,fi.cv,fi.off,fi.size,fi.nsec);
    struct rtw89_fw_info f2; memset(&f2,0,sizeof f2); assert(rtw89_fw_parse(d,len,2,&f2)==0&&f2.cv==2&&f2.off!=fi.off);
    assert(rtw89_fw_parse(d,len,7,&f2)==0&&f2.cv==2);
    assert(rtw89_fw_parse(d,len,0,&f2)==-2);
    u32 o,s; u8 c; assert(rtw89_mfw_pick(d,len,2,RTW89_FW_WOWLAN,&o,&s,&c)==0&&c==2);
    for(u32 cut=0;cut<200;cut++){ struct rtw89_fw_info t; assert(rtw89_fw_parse(d,cut,1,&t)<0); }
    for(u32 cut=fi.off;cut<fi.off+fi.size;cut+=997){ struct rtw89_fw_info t; assert(rtw89_fw_hdr_parse(d+fi.off,cut-fi.off,&t)<0); }
    u8* m=malloc(len); memcpy(m,d,len); le32(m+16+8,len); assert(rtw89_fw_parse(m,len,1,&f2)<0);
    memcpy(m,d,len); m[1]=200; assert(rtw89_fw_parse(m,len,1,&f2)<0); free(m); free(d);
    static u8 b[8192]; u32 sz[3]={100,40,64}; u8 ty[3]={2,1,9}; u32 ms[3]={0,0,2};
    u32 L=mk_fw(b,3,sz,ty,ms,0); assert(rtw89_fw_parse(b,L,0,&f2)==0&&f2.type==RTW89_FW_NORMAL&&f2.nsec==3&&f2.sec[2].mssc==2&&f2.year==2024&&f2.month==7&&f2.commit==0xdeadbeef);
    assert(rtw89_fw_parse(b,L-1,0,&f2)<0); assert(rtw89_fw_parse(b,L+1,0,&f2)<0);
    L=mk_fw(b,2,sz,ty,ms,1); assert(rtw89_fw_hdr_parse(b,L,&f2)==0&&f2.sec[0].len==108&&f2.sec[1].len==48);
    le32(b+24,0); assert(rtw89_fw_hdr_parse(b,L,&f2)<0); le32(b+24,11u<<8); assert(rtw89_fw_hdr_parse(b,L,&f2)<0);
    L=mk_fw(b,1,sz,ty,ms,0); le32(b+12,3u<<24); assert(rtw89_fw_hdr_parse(b,L,&f2)==-3);
    ms[2]=1000; L=mk_fw(b,3,sz,ty,ms,0); assert(rtw89_fw_hdr_parse(b,4096,&f2)<0);
    printf("firmware header parser: MFW CV selection, legacy, checksum, security, truncation and corruption cases\n"); }

static u32 h_bytes, h_pkts, h_cmds, h_max, h_total; static u8 h_last[2048]; static u32 h_last_len; static u8 c2hq[4][64]; static u32 c2hl[4]; static int c2hn, c2hi;
static int h_tx(struct rtw89_softc* sc,const u8* pk,u32 len,int kind){ (void)sc; assert(len<=2048); memcpy(h_last,pk,len); h_last_len=len;
    if(kind==RTW89_H2C_PKT_CMD){ h_cmds++; if((pk[0]>>2)==3&&pk[1]==0&&(g32(0x1E0)&2)){ assert(len==8+32+16*3); assert(((u32)pk[4]|((u32)pk[5]<<8))==len); assert(pk[8+28]==(2020&0xFF)&&pk[8+29]==(2020>>8)); sim_put(0x1E0,g32(0x1E0)|4); h_bytes=0; } return 0; }
    assert(g32(0x1E0)&4); assert(len<=2020); h_pkts++; h_bytes+=len; if(len>h_max)h_max=len;
    if(h_bytes==h_total)sim_put(0x1E0,(g32(0x1E0)&~0xE0u)|((sim_fw_fail?2u:7u)<<5));
    return 0; }
static int h_rx(struct rtw89_softc* sc,u8* b,u32 cap){ (void)sc; if(c2hi>=c2hn)return 0; u32 n=c2hl[c2hi]<cap?c2hl[c2hi]:cap; memcpy(b,c2hq[c2hi],n); c2hi++; return (int)n; }
static const struct rtw89_hci_ops HCI={ h_tx,h_rx,0 };
static void q_c2h(u8 cls,u8 func,u32 w2,const char* txt){ u8* c=c2hq[c2hn]; memset(c,0,64); u32 n=12; c[0]=(u8)(1|(cls<<2)); c[1]=func;
    if(txt){ n=8+(u32)strlen(txt); memcpy(c+8,txt,strlen(txt)); } else le32(c+8,w2); c[4]=(u8)n; c2hl[c2hn++]=n; }

static void test_efuse(void){
    struct rtw89_softc sc; memset(&sc,0,sizeof sc); sc.io=&SIM; sc.chip=RTW89_CHIP_8852B; sc.cv=1; sim_reset();
    assert(rtw89_efuse_read(&sc)==0&&sc.efuse_ok&&sc.efuse_valid&&sc.rfe_type==1&&sc.xtal_cap==0x3F&&sc.country[0]=='S'&&sc.country[1]=='E');
    assert(sc.mac[0]==0x00&&sc.mac[1]==0xe0&&sc.mac[5]==0xbe); assert(sc.efuse_reads==1216);
    assert(regs[1]&1); assert(!(regs[1]&0xC0)); assert(!(regs[0xCC]&4));
    memset(&sc,0,sizeof sc); sc.io=&SIM; sc.chip=RTW89_CHIP_8852B; sim_reset(); sim_ef_dead=1;
    assert(rtw89_efuse_read(&sc)<0&&strstr(sc.err,"timed out")&&sc.err_reg==0x30&&sc.efuse_reads==5*10001);
    memset(&sc,0,sizeof sc); sc.io=&SIM; sc.chip=RTW89_CHIP_8852B; sim_reset(); memset(sim_ef,0xFF,sizeof sim_ef);
    assert(rtw89_efuse_read(&sc)==-3&&strstr(sc.err,"MAC"));
    static u8 ph[1216], lg[2048]; memset(ph,0xFF,sizeof ph); ph[4]=0x3F; ph[5]=0xF0; ph[6]=0xAB; assert(rtw89_efuse_logical(ph,1216,4,lg,2048)==0&&lg[2040]==0xAB);
    memset(ph,0xFF,sizeof ph); u32 q=4; while(q+10<1212){ ph[q]=0x30; ph[q+1]=0x00; for(int i=0;i<8;i++)ph[q+2+i]=(u8)i; q+=10; } ph[q]=0x30; ph[q+1]=0x00; assert(rtw89_efuse_logical(ph,1216,4,lg,2048)<0);
    memset(&sc,0,sizeof sc); sc.io=&SIM; sc.chip=RTW89_CHIP_8852C; assert(rtw89_efuse_read(&sc)<0);
    printf("efuse: 1216-byte physical dump, logical map, MAC 00:e0:4c:88:52:be RFE 1 xtal 0x3f country SE, timeout bounded, blank and corrupt maps rejected\n"); }

static void test_fwdl(void){
    u32 len=0; u8* d=load("rtw89/rtw8852b_fw-1.bin",&len); assert(d);
    struct rtw89_softc sc; memset(&sc,0,sizeof sc); sc.io=&SIM; sc.chip=RTW89_CHIP_8852B; sim_reset();
    assert(rtw89_fw_parse(d,len,1,&sc.fw)==0);
    rtw89_hci=0; assert(rtw89_fw_download(&sc,d,len)<0&&strstr(sc.err,"H2C ring")&&nw==0);
    assert(rtw89_h2c_send(&sc,1,2,3,0,0,0,0)<0);
    h_total=0; u32 exp_pkts=0; for(int i=0;i<sc.fw.nsec;i++){ h_total+=sc.fw.sec[i].len; exp_pkts+=(sc.fw.sec[i].len+2019)/2020; }
    rtw89_hci=&HCI; h_pkts=0; h_cmds=0; h_max=0;
    assert(rtw89_fw_download(&sc,d,len)==0); assert(sc.fw_rdy&&sc.fwdl_tries==1&&sc.fwdl_sts==7&&h_cmds==1&&h_pkts==exp_pkts&&h_bytes==h_total&&h_max==2020);
    assert(g32(0x1E0)&1); assert(regs[0x88]&2); assert(g32(0x160)==0&&g32(0x164)==0);
    printf("fwdl: rtw8852b_fw-1.bin CV1 image, header H2C 88 bytes + %u section packets (%u bytes) on simulated H2C ring, WCPU init ready\n",h_pkts,h_bytes);
    u8 pl[4]={1,2,3,4};
    for(int i=0;i<5;i++){ assert(rtw89_h2c_send(&sc,1,7,9,0,0,pl,4)==0); assert(h_last_len==12&&h_last[3]==(u8)i&&(h_last[0]&3)==1&&(h_last[0]>>2)==7&&h_last[1]==9); assert(!!(h_last[5]&0x40)==(i%4==0)); }
    assert(sc.h2c_seq==5&&sc.h2c_cnt==5); static u8 big[2048]; assert(rtw89_h2c_send(&sc,1,7,9,0,0,big,2041)<0);
    c2hn=c2hi=0; q_c2h(0,0,(1u|(7u<<2)|(9u<<8)|(4u<<16)),0); q_c2h(0,2,0,"fw hello\n"); q_c2h(0,1,(1u|(7u<<2)|(9u<<8)|(0u<<16)|(4u<<24)),0);
    assert(rtw89_h2c_wait_done(&sc,7,9,1000)==0&&sc.rec_seq==4&&sc.done_seq==4&&sc.done_ret==0&&!strcmp(sc.fwlog,"fw hello")&&sc.c2h_cnt==3);
    c2hn=c2hi=0; q_c2h(0,1,(1u|(7u<<2)|(9u<<8)|(5u<<16)|(5u<<24)),0); assert(rtw89_h2c_wait_done(&sc,7,9,1000)==-3);
    c2hn=c2hi=0; u32 dl=delays; assert(rtw89_h2c_wait_done(&sc,7,9,1000)==-2&&delays-dl==1000);
    u8 sh[8]={5,0,0,0,200,0,0,0}; assert(rtw89_c2h_handle(&sc,sh,8)==0); u8 ack[8]={1,1,0,0,8,0,0,0}; assert(rtw89_c2h_handle(&sc,ack,8)<0); assert(rtw89_c2h_handle(&sc,sh,4)<0);
    memset(&sc,0,sizeof sc); sc.io=&SIM; sc.chip=RTW89_CHIP_8852B; sim_reset(); assert(rtw89_fw_parse(d,len,1,&sc.fw)==0); sim_fw_fail=1;
    assert(rtw89_fw_download(&sc,d,len)==-2&&sc.fwdl_tries==5&&strstr(sc.err,"checksum")&&!sc.fw_rdy&&sc.fwdl_sts==2);
    memset(&sc,0,sizeof sc); sc.io=&SIM; sc.chip=RTW89_CHIP_8852B; sim_reset(); assert(rtw89_fw_parse(d,len,1,&sc.fw)==0); sim_fw_noboot=1;
    assert(rtw89_fw_download(&sc,d,len)<0&&strstr(sc.err,"H2C path")&&sc.fwdl_tries==5&&sc.err_reg==0x1E0);
    rtw89_hci=0; free(d);
    printf("h2c/c2h: header seq/rack every 4th, oversize rejected, rec/done ack, fw log, wait bounded; fwdl checksum failure after 5 tries, dead WCPU path bounded\n"); }

static void test_glue(void){
    char out[768];
    pci_n=0; rtw89_status(out,sizeof out); assert(strstr(out,"no Realtek")); rtw89_up(out,sizeof out); assert(strstr(out,"no Realtek")); rtw89_wifi_hw_probe(); assert(!registered);
    memset(&g_rtw89,0,sizeof g_rtw89); g_rtw89.io=&SIM; sim_reset();
    pci_n=2; pci_ven[0]=0x10ec; pci_dev[0]=0xc821; pci_ven[1]=0x10ec; pci_dev[1]=0xb852; pci_bus[1]=3;
    rtw89_wifi_hw_probe(); assert(g_rtw89.state==RTW89_ST_FOUND&&g_rtw89.mmio==0xfc000000u&&!registered&&nw==0&&nrd==0);
    rtw89_status(out,sizeof out); assert(strstr(out,"RTL8852BE")&&strstr(out,"detect only")&&strstr(out,"(present)")&&nw==0);
    rtw89_up(out,sizeof out); printf("%s",out);
    assert(g_rtw89.state==RTW89_ST_PWRON&&(pcicmd&6)==6&&strstr(out,"version 0.29.29.15")&&(strstr(out,"H2C ring")||strstr(out,"DMA memory"))&&strstr(out,"MAC=00:e0:4c:88:52:be RFE type=1"));
    rtw89_wifi_hw_probe(); assert(!registered);
    h_total=0; for(int i=0;i<g_rtw89.fw.nsec;i++)h_total+=g_rtw89.fw.sec[i].len; rtw89_hci=&HCI; rtw89_up(out,sizeof out); rtw89_hci=0;
    assert(g_rtw89.state==RTW89_ST_READY&&strstr(out,"Firmware download: ready, tries 1")&&strstr(out,"ready")&&strstr(out,"MAC/BB/RF init: done")&&g_rtw89.mmio_size==0x200000u&&rtw89_chan_hook==rtw89_set_channel&&rtw89_scan_pre_hook==rtw89_sta_vif_init&&g_rtw89.h2c_cnt>0); rtw89_wifi_hw_probe(); assert(registered==1); registered=0;
    t_fb=1; memset(&g_rtw89,0,sizeof g_rtw89); g_rtw89.io=&SIM; sim_reset(); rtw89_wifi_hw_probe(); assert(!registered&&t_unsup==1&&g_rtw89.state==RTW89_ST_PWRON); rtw89_wifi_hw_probe(); assert(t_unsup==1); t_fb=0;
    memset(&g_rtw89,0,sizeof g_rtw89); g_rtw89.io=&SIM; sim_reset(); sim_dead=1;
    rtw89_up(out,sizeof out); assert(g_rtw89.state==RTW89_ST_HWFAIL&&strstr(out,"system power")&&strstr(out,"register 0x0004"));
    memset(&g_rtw89,0,sizeof g_rtw89); g_rtw89.io=&SIM; sim_reset(); hide_fw=1;
    rtw89_up(out,sizeof out); assert(g_rtw89.state==RTW89_ST_PWRON&&strstr(out,"MISSING")&&strstr(out,"rtw89/rtw8852b_fw-1.bin"));
    memset(&g_rtw89,0,sizeof g_rtw89); g_rtw89.io=&SIM; sim_reset(); pci_dev[1]=0xc852; nw=0;
    rtw89_up(out,sizeof out); assert(g_rtw89.state==RTW89_ST_NOFW&&strstr(out,"RTL8852CE")&&strstr(out,"rtw8852c_fw-1.bin (MISSING)")&&nw==0);
    hide_fw=0; memset(&g_rtw89,0,sizeof g_rtw89); bar2=0x1; pci_dev[1]=0xb85b; g_rtw89.io=&SIM;
    assert(rtw89_probe()==1&&g_rtw89.state==RTW89_ST_HWFAIL); rtw89_up(out,sizeof out); assert(strstr(out,"BAR2 is not memory"));
    memset(&g_rtw89,0,sizeof g_rtw89); bar2=0xfc000004u; pcicmd=0; assert(rtw89_probe()==1&&g_rtw89.state==RTW89_ST_FOUND&&g_rtw89.device==0xb85b);
    printf("glue: inert without card, detect-only probe touches no registers, wifi rtw89 on powers up, failures named, no wifi_hw registration before firmware runs\n"); }

int main(int argc,char** argv){
    FWDIR=argc>1?argv[1]:"assets/firmware/linux-firmware";
    test_pwr_on(); test_fw(); test_efuse(); test_fwdl(); test_glue();
    printf("rtw89 host ok\n"); return 0; }
