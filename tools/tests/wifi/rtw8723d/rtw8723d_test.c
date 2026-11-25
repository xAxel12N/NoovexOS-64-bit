#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include <stdlib.h>
#include "include/nvx/base.h"
#include "include/nvx/drivers/net/wifi/rtw_chip.h"
#include "drivers/net/wifi/realtek/rtw88/chips/rtw8723d.h"
#include "drivers/net/wifi/realtek/rtw88/chips/rtw8723d_tables.h"
#include "drivers/net/wifi/realtek/rtw88/chips/rtw8723d_txpwr_tbl.h"
#include "include/nvx/drivers/net/wifi/rtw_ra.h"
#include "include/nvx/drivers/net/wifi/rtw8821ce.h"
#include "drivers/net/wifi/realtek/rtw88/rtw_coex_core.h"
int rtw_h2c_send(u32 m,int box,u32 w0,u32 w1); int rtw_h2c_cmd(u32 m,u32 w0,u32 w1); int rtw_mac_trx_init(u32 m); int rtw_rx_rssi(const u8* base); int rtw_tx_mgmt_path(u32 m,const u8* frame,int fl,int force_be);
extern u8* g_bering; extern u32 g_benum, g_beidx; extern int g_h2c_box;
static u8 regs[0x10000]; static int fails, depth, llt_stuck, tn, fb, fo, ff; static struct { u32 o, v; u8 sz; } tr[256];
rtw_ra_rpt g_rtw_ra_rpt; int g_rtw_coex_ok, cal_n; static u32 lte[256];
void rtw_chip_calibrate(u32 m){ (void)m; cal_n++; }
static void lte_hook(void){ u32 c=*(u32*)(regs+0x7C0), off=c&0xFF; if((c>>30)==3)lte[off]=*(u32*)(regs+0x7C4); else if((c>>30)==2)*(u32*)(regs+0x7C8)=lte[off]; regs[0x7C3]|=0x20; }
static void tlog(u32 o,u32 v,u8 sz){ if(depth==1&&tn<256){ tr[tn].o=o; tr[tn].v=v; tr[tn].sz=sz; tn++; } }
const unsigned char _binary_rtw8723d_fw_bin_start[64]={0xD1,0x23}; const unsigned char _binary_rtw8723d_fw_bin_end[1];
rtw_efuse_info g_rtw_efuse; const rtw_chip* g_rtw_chip=&rtw8723d_chip; int rtw_efuse_valid;
u16 rtw_rsvd_boundary(void){ return 0; }
int wifi_feat_begin(int f){ (void)f; fb++; return 1; } void wifi_feat_ok(int f){ (void)f; fo++; } void wifi_feat_fail(int f,const char* w){ (void)f; (void)w; ff++; }
static int t_regd=12; int rtw_txpwr_regd_now(void){ return t_regd; } static int t_dbm; int regdb_max_dbm(u8 c){ (void)c; return t_dbm; }
int g_rtw_bw_cur, g_rtw_iqk_last; static u8 pool[64*4096] __attribute__((aligned(4096))); static int pused;
void* dma_alloc_pages(int n){ if(pused+n>64)return 0; void* r=pool+pused*4096; pused+=n; return r; }
u8 rtw_r8(u32 m,u32 o){ (void)m; return regs[o&0xFFFF]; }
u16 rtw_r16(u32 m,u32 o){ return (u16)(rtw_r8(m,o)|(rtw_r8(m,o+1)<<8)); }
u32 rtw_r32(u32 m,u32 o){ return rtw_r16(m,o)|((u32)rtw_r16(m,o+2)<<16); }
void rtw_w8(u32 m,u32 o,u8 v){ (void)m; depth++; tlog(o,v,1); regs[o&0xFFFF]=v; if(o==0x80&&(v&1))regs[0x80]|=4; if(o==0x226&&(v&1)&&!llt_stuck)regs[0x226]&=(u8)~1; if(o==0x7C3)lte_hook(); depth--; }
void rtw_w16(u32 m,u32 o,u16 v){ depth++; tlog(o,v,2); depth++; rtw_w8(m,o,(u8)v); rtw_w8(m,o+1,(u8)(v>>8)); depth-=2; }
void rtw_w32(u32 m,u32 o,u32 v){ depth++; tlog(o,v,4); depth++; rtw_w16(m,o,(u16)v); rtw_w16(m,o+2,(u16)(v>>16)); depth-=2; }
void rtw_delay_us(u32 u){ (void)u; } void ui_pump(void){} void klog(const char* s){ (void)s; } int regdb_probe_ok(u8 c){ (void)c; return 1; }
int rtw_pwr_seq_run(u32 m,const rtw_pwr_cmd* s,int n){ (void)m; (void)s; return n<0; }
void rtw_rf_write(u32 m,int p,u32 a,u32 mask,u32 v){ (void)p; (void)mask; rtw_w32(m,0x840,(a<<20)|(v&0xFFFFF)); }
static char* skipc(char* p){ while(*p){ if(p[0]=='/'&&p[1]=='*'){ char* e=strstr(p,"*/"); p=e?e+2:p+strlen(p); continue; } if(isdigit((unsigned char)*p)||(*p=='-'&&isdigit((unsigned char)p[1])))return p; p++; } return p; }
static int nums(const char* buf,const char* name,long* o,int cap){ const char* p=strstr(buf,name); if(!p)return -1; p=strchr(p,'{'); const char* e=p?strstr(p,"};"):0; if(!e)return -1; int n=0; char* q=(char*)p;
    for(;;){ q=skipc(q); if(!*q||q>=e)break; char* x; long v=strtol(q,&x,0); if(n<cap)o[n]=v; n++; q=x; } return n; }
static char* slurp(const char* f,char* b,int cap){ FILE* h=fopen(f,"r"); size_t n=h?fread(b,1,cap-1,h):0; if(h)fclose(h); b[n]=0; return n?b:0; }
static void ck(int c,const char* w){ if(!c){ printf("FAIL %s\n",w); fails++; } }
static int walk(const unsigned int* t,int n,int pkg,int rfe){ int out=0,mt=1,sk=0; unsigned pc=0;
    for(int i=0;i<n;i++){ unsigned a=t[i*2]; if(a>>31){ unsigned br=(a>>28)&3; if(br==3){mt=1;sk=0;} else if(br==2) mt=!sk; else pc=a; continue; }
        if((a>>30)&1){ if(!sk){ unsigned cp=(pc>>12)&15, cr=pc&255; int ok=(!cp||(int)cp==pkg)&&(int)cr==rfe; mt=ok; sk=ok; } else mt=0; continue; }
        if(mt)out++; } return out; }
int main(int argc,char** argv){
    ck(rtw8723d_chip.vendor==0x10EC&&rtw8723d_chip.device==0xD723&&rtw8723d_chip.bus==RTW_BUS_PCIE,"PCI ID 10ec:d723");
    ck(rtw8723d_chip.n_paths==1&&rtw8723d_chip.wcpu==RTW_WCPU_8051&&rtw8723d_chip.tx_desc_sz==40,"1T1R, 8051, 40-byte TX desc");
    ck(rtw8723d_cmac_n==113&&rtw8723d_cagc_n==386&&rtw8723d_cbb_n==223&&rtw8723d_crf_a_n==180,"table pair counts match Linux rtw8723d_table.c");
    ck(rtw8723d_chip.mac_tbl.n==113&&rtw8723d_chip.rf_tbl[0].n==180&&rtw8723d_chip.rf_tbl[1].t==0,"chip table descriptors");
    if(argc>1){ FILE* f=fopen(argv[1],"r"); static char buf[1<<20]; size_t n=f?fread(buf,1,sizeof buf-1,f):0; if(f)fclose(f); buf[n]=0;
        const char* nm[4]={"rtw8723d_mac[]","rtw8723d_agc[]","rtw8723d_bb[]","rtw8723d_rf_a[]"}; const unsigned int* tb[4]={rtw8723d_cmac,rtw8723d_cagc,rtw8723d_cbb,rtw8723d_crf_a}; int tn[4]={113,386,223,180};
        for(int k=0;k<4;k++){ char* p=strstr(buf,nm[k]); ck(p!=0,"linux table found"); if(!p)continue; p=strchr(p,'{'); int i=0,bad=0; char* e=strstr(p,"};");
            while(p&&p<e){ char* x=strstr(p,"0x"); if(!x||x>e)break; unsigned v=(unsigned)strtoul(x,&p,16); if(i>=tn[k]*2||tb[k][i]!=v)bad++; i++; }
            ck(!bad&&i==tn[k]*2,nm[k]); } }
    int w=walk(rtw8723d_cbb,rtw8723d_cbb_n,15,0), wr=walk(rtw8723d_crf_a,rtw8723d_crf_a_n,15,0);
    ck(w>0&&w<=rtw8723d_cbb_n&&wr>0,"conditional walk yields writes");
    u8 map[512]; memset(map,0xFF,sizeof map); for(int i=0;i<6;i++)map[0xD0+i]=(u8)(0x10+i);
    map[0x10+1]=0x2A; map[0x10+6+1]=0x28; map[0x10+11]=0x2E; map[0xB9]=0x1C; map[0xCB]='S'; map[0xCC]='E';
    rtw_efuse_info o; ck(rtw8723d_efuse_parse(map,512,&o)==0,"efuse parse");
    ck(o.mac[0]==0x10&&o.mac[5]==0x15&&o.crystal_cap==0x1C&&o.rfe_option==0&&o.country_code[0]=='S',"efuse fields at 8723x offsets");
    ck(o.tx_pwr_idx_2g[0][3]==0x28&&o.tx_pwr_idx_2g[1][3]==0&&o.tx_pwr_idx_5g[0][0]==0,"2.4 GHz only, path A only");
    ck(rtw8723d_efuse_parse(map,100,&o)<0,"short map refused");
    u8 c,of,h; rtw8723d_txpwr_calc(4,&c,&of,&h); ck(c==0x2A&&of==0x28-2&&h==0x28+2,"tx power index from efuse diffs");
    regs[0x18]=0; memset(regs,0,sizeof regs); regs[0x840]=0; rtw8723d_chip.set_channel(0,36,RTW_BW_20); ck(regs[0xE00]==0&&regs[0x840]==0,"5 GHz channel ignored");
    rtw8723d_chip.set_channel(0,6,RTW_BW_40); ck((regs[0x800]&1)==1&&regs[0x483]==2,"40 MHz on ch6, primary lower");
    rtw8723d_chip.set_channel(0,13,RTW_BW_40); ck((regs[0x800]&1)==0,"ch13 falls back to 20 MHz");
    u8 fw[100]; memset(fw,0,sizeof fw); fw[0]=0xD1; fw[1]=0x23; for(int i=32;i<100;i++)fw[i]=(u8)i;
    ck(rtw8723d_fw_download(0,fw,10)<0,"tiny firmware refused"); fw[1]=0x88; ck(rtw8723d_fw_download(0,fw,100)==-11,"wrong signature refused"); fw[1]=0x23;
    memset(regs,0,sizeof regs); int r=rtw8723d_fw_download(0,fw,100); ck(regs[0x1000]==32&&regs[0x1000+67]==99&&r==1,"legacy page write, bounded ready wait");

    static const struct { u32 o, v; u8 sz; } ex[]={ {0x010C,0xC5A0,2},{0x0100,0,1},{0x0100,0xFF,1},{0x0214,0x00000002,4},{0x0200,0x80E7020C,4},{0x0114,0xF8,1},{0x0116,0x3F7F,2},
        {0x0209,0xF8,1},{0x0424,0xF8,1},{0x0425,0xF8,1},{0x045D,0xF8,1},{0x0224,0x00010000,4},{0x060F,4,1},{0x0608,0x10000000,4},{0x07D4,0,4} };
    memset(regs,0,sizeof regs); tn=0; fo=ff=0; g_rtw_chip=&rtw8723d_chip; r=rtw_mac_trx_init(0); int ne=(int)(sizeof ex/sizeof ex[0]), bad=tn!=ne;
    for(int i=0;i<ne&&i<tn;i++) if(tr[i].o!=ex[i].o||tr[i].v!=ex[i].v||tr[i].sz!=ex[i].sz){ printf("  trace %d: got %03X=%X/%d want %03X=%X/%d\n",i,tr[i].o,tr[i].v,tr[i].sz,ex[i].o,ex[i].v,ex[i].sz); bad=1; }
    ck(r==0&&!bad,"8051 TRX init register trace matches rtw88 txdma_queue_mapping + __priority_queue_cfg_legacy + rtw_drv_info_cfg");
    ck(fo==1&&ff==0,"TRX init success reports the guard ok");
    memset(regs,0,sizeof regs); llt_stuck=1; ck(rtw_mac_trx_init(0)<0&&ff==1,"stuck auto-LLT refused, guard counts a failure"); llt_stuck=0;
    memset(regs,0,sizeof regs); regs[0x100]=0xEA; regs[0xF3]=1; fb=0; rtw8723d_chip.power_on(0);
    ck(fb==1&&regs[0x7C]==0xC3&&regs[0x423]==0x7F&&(regs[0x08]&8)&&regs[0x100]==0xFF&&regs[0x101]==0x02,"power on: LDO select, legacy system cfg (CR 0x2FF, HWSEQ 0x7F, WAKEPAD)");
    memset(regs,0,sizeof regs); tn=0; g_h2c_box=0; regs[0x1CC]=0; ck(rtw_h2c_cmd(0,0x11223344u,0x55667788u)==0&&g_h2c_box==1,"H2C mailbox 0 taken, next box 1");
    ck(regs[0x1D0]==0x44&&regs[0x1D3]==0x11&&regs[0x1F0]==0x88&&regs[0x1F3]==0x55&&tn==2&&tr[0].o==0x1F0&&tr[1].o==0x1D0,"H2C: extension word first, then box (rtw88 fw.c order)");
    rtw_h2c_cmd(0,1,0); rtw_h2c_cmd(0,2,0); ck(g_h2c_box==3&&regs[0x1D8]==2,"boxes rotate 0..3");
    regs[0x1CC]=8; ck(rtw_h2c_cmd(0,3,0)<0&&g_h2c_box==3,"busy box refused after a bounded wait");
    regs[0x1CC]=0; rtw_h2c_cmd(0,4,0); ck(g_h2c_box==0&&regs[0x1DC]==4,"box 3 wraps to 0");
    u8 fr[64]; memset(fr,0,sizeof fr); fr[0]=0xD0; fr[4]=0x02; for(int i=24;i<64;i++)fr[i]=(u8)i;
    static u8 ring[16*16]; memset(regs,0,sizeof regs); g_bering=ring; g_benum=16; g_beidx=0; rtw_tx_mgmt_path(0,fr,64,1);
    u8* pk=(u8*)(unsigned long)*(u32*)(ring+4); u32 w0=pk?*(u32*)pk:0;
    ck(*(u16*)ring==40&&*(u16*)(ring+2)==1&&*(u16*)(ring+8)==64&&*(u32*)(ring+12)==*(u32*)(ring+4)+40,"TX buffer descriptor: 40-byte packet descriptor segment, payload at +40");
    ck(pk&&(w0&0xFFFF)==64&&((w0>>16)&0xFF)==40&&pk[40]==0xD0&&pk[40+63]==63,"TX packet descriptor OFFSET=40, frame right after it");
    if(pk){ u16 x=0; for(int i=0;i<16;i++)x^=(u16)(pk[i*2]|(pk[i*2+1]<<8)); ck(x==0,"descriptor checksum over the first 32 bytes"); }
    ck(rtw_txd_sz()==40,"rtw_txd_sz follows the chip"); { rtw_chip t=rtw8723d_chip; t.tx_desc_sz=48; g_rtw_chip=&t; ck(rtw_txd_sz()==48,"3081 chips keep 48"); t.tx_desc_sz=0; ck(rtw_txd_sz()==48,"unset size means 48"); g_rtw_chip=&rtw8723d_chip; }
    u8 rx[64]; memset(rx,0,sizeof rx); *(u32*)rx=40u|(4u<<16)|(1u<<26); rx[24]=0x00; rx[25]=60; ck(rtw_rx_rssi(rx)==60-97,"RX PHY status page 0 (CCK): PWDB - 97 like rtw8723d query_phy_status_page0");
    rx[24]=0x01; rx[25]=50; ck(rtw_rx_rssi(rx)==50-110,"RX PHY status page 1 (OFDM): PWDB_A - 110");
    rx[24]=0x02; ck(rtw_rx_rssi(rx)==-127,"unknown PHY status page ignored");

    memset(regs,0,sizeof regs); regs[0x0948]=0x11; regs[0x67]=0x5A; regs[0x522]=0x00;
    *(u32*)(regs+0xE94)=0x100u<<16; *(u32*)(regs+0xE9C)=0x005u<<16; *(u32*)(regs+0xEA4)=0x100u<<16; *(u32*)(regs+0xEAC)=(1u<<26)|(0x003u<<16);
    *(u32*)(regs+0xC80)=0x100u<<22; *(u32*)(regs+0xCD4)=0x100u<<13; *(u32*)(regs+0x85C)=0x1234;
    g_rtw_iqk_last=0; rtw8723d_chip.calibrate(0);
    ck(g_rtw_iqk_last==1&&rtw8723d_iqk_res[0]==0x100&&rtw8723d_iqk_res[1]==5&&rtw8723d_iqk_res[2]==0x100&&rtw8723d_iqk_res[3]==3&&rtw8723d_iqk_res[4]==0x100,"IQK: S1+S0 TX/RX results taken, rounds agree, no fail bits");
    ck((*(u32*)(regs+0xC80)&0x3FF)==0x100&&((*(u32*)(regs+0xC80)>>16)&0x3F)==5&&(*(u32*)(regs+0xC14)&0x3FF)==0x100&&((*(u32*)(regs+0xCD0)>>1)&0x3FF)==0x100&&(*(u32*)(regs+0xCD8)&0x3FF)==0x100,"IQK matrices written for S1 (0xC80/0xC14) and S0 (0xCD0/0xCD8)");
    ck(*(u32*)(regs+0x948)==0x11&&regs[0x67]==0x5A&&*(u32*)(regs+0x85C)==0x1234&&regs[0x522]==0,"IQK restores BB_SEL_BTG, path control, ADDA and TX pause");
    *(u32*)(regs+0xEAC)=(1u<<26)|(1u<<28)|(1u<<27); g_rtw_iqk_last=0; rtw8723d_chip.calibrate(0);
    ck(g_rtw_iqk_last==(1|(0xF<<8)),"IQK with fail bits: identity results, fail mask 0xF reported");
    { int rs[4][8]; memset(rs,0,sizeof rs); for(int i=0;i<8;i++){ rs[0][i]=0x100; rs[1][i]=0x103; } ck(rtw8723d_iqk_similar(rs,0,1)==1,"similarity within tolerance 5");
      rs[1][0]=0x120; ck(rtw8723d_iqk_similar(rs,0,1)==0&&rs[3][0]==0&&rs[3][2]==0x100,"TX X off by more than 5: S1 TX pair dropped, rest goes to the hybrid round");
      memset(rs,0,sizeof rs); for(int i=0;i<8;i++){ rs[0][i]=0x100; rs[1][i]=0x100; } rs[0][2]=0; rs[0][3]=0; rs[1][3]=0; ck(rtw8723d_iqk_similar(rs,0,1)==0&&rs[3][0]==0x100&&rs[3][1]==0x100,"empty RX pair: candidate round copied into the hybrid round (rtw8723x similarity_cmp)"); }
    memset(regs,0,sizeof regs); *(u32*)(regs+0x8B4)=0x20; rtw8723d_spur_cal(0,13);
    ck(((*(u32*)(regs+0xC40)>>24)&0x1F)==0xB&&(*(u32*)(regs+0xC40)&(1u<<9))&&*(u32*)(regs+0xD40)==0x04000000u&&*(u32*)(regs+0xD4C)==0&&(*(u32*)(regs+0xD2C)&(1u<<28))&&*(u32*)(regs+0x88C)==0xCCC000C0u&&*(u32*)(regs+0x808)==0xFCCDu,"ch13 spur over 0x16: notch 0xB, CSI1 0x04000000, CFO track on, 3-wire back on");
    memset(regs,0,sizeof regs); *(u32*)(regs+0x8B4)=0x16; rtw8723d_spur_cal(0,14);
    ck(((*(u32*)(regs+0xC40)>>24)&0x1F)==0x5&&*(u32*)(regs+0xD40)==0&&*(u32*)(regs+0xD4C)==0x00080000u&&*(u32*)(regs+0x808)==0xFF9Au,"ch14 spur at threshold: notch 0x5, CSI4 0x00080000");
    memset(regs,0,sizeof regs); *(u32*)(regs+0x8B4)=0x15; *(u32*)(regs+0xD40)=7; rtw8723d_spur_cal(0,13);
    ck(((*(u32*)(regs+0xC40)>>24)&0x1F)==0x1F&&!(*(u32*)(regs+0xC40)&(1u<<9))&&*(u32*)(regs+0xD40)==0&&!(*(u32*)(regs+0xD2C)&(1u<<28)),"ch13 below threshold: notch off (0x1F, CSI cleared)");
    memset(regs,0,sizeof regs); *(u32*)(regs+0x8B4)=0x30; rtw8723d_spur_cal(0,6); ck(*(u32*)(regs+0x808)==0&&*(u32*)(regs+0x88C)==0&&((*(u32*)(regs+0xC40)>>24)&0x1F)==0x1F,"ch1-12: no PSD scan, notch off");

    memset(regs,0,sizeof regs); memset(&g_rtw_ra_rpt,0,sizeof g_rtw_ra_rpt); g_rtw_efuse.thermal_meter=0x18; g_rtw_efuse.crystal_cap=0x20; g_wifi_curch=6; rtw8723d_iqk_done=0; cal_n=0; rtw8723d_pt_init();
    rtw8723d_pwr_track(1); ck(*(u32*)(regs+0x840)==((0x42u<<20)|3u),"power tracking: first call triggers the thermal meter (RF 0x42[17:16]=3)");
    *(u32*)(regs+0x8A0)=0x1Cu<<10; rtw8723d_pwr_track(1);
    ck(rtw8723d_pt_avg==0x1C&&rtw8723d_pt_delta==2&&*(u32*)(regs+0xC80)==0x40000100u&&(*(u32*)(regs+0xAB4)&0x7FF)==0x47F,"thermal +4, CCK rate: delta 2 (pwrtrk_2g_cck_a_p), OFDM swing 30, CCK swing 30");
    ck(((*(u32*)(regs+0xCD0)>>1)&0x3FF)==0x100&&((*(u32*)(regs+0xCD4)>>13)&0x3FF)==0x100&&((*(u32*)(regs+0x2C)>>12)&0xFFF)==0x820&&cal_n==0,"S0 image of the swing at 0xCD0/0xCD4, crystal unchanged, no IQK");
    g_rtw_ra_rpt.n=1; g_rtw_ra_rpt.rate=0x13; *(u32*)(regs+0x8A0)=0x2Cu<<10; rtw8723d_pwr_track(1); rtw8723d_pwr_track(1);
    ck(rtw8723d_pt_avg==32&&rtw8723d_pt_delta==3&&*(u32*)(regs+0xC80)==0x43c0010fu&&(*(u32*)(regs+0xAB4)&0x7FF)==0x4C3&&cal_n==1&&rtw8723d_pt_iqk==1,"EWMA(10,4) average 32, OFDM table delta 3, thermal moved 8: LCK + IQK through the guard");
    *(u32*)(regs+0x8A0)=0x3Fu<<10; rtw8723d_pwr_track(1); rtw8723d_pwr_track(1);
    ck(rtw8723d_pt_avg==39&&rtw8723d_pt_delta==7&&*(u32*)(regs+0xC80)==0x50800142u&&rtw8723d_pt_rem_ofdm==1&&rtw8723d_pt_rem_cck==0&&(*(u32*)(regs+0xAB4)&0x7FF)==0x5FF,"MCS7 OFDM limit 34: swing capped, remnant 1 goes to TX AGC");
    rtw8723d_pwr_track(1); rtw8723d_pwr_track(1);
    { u8 cc,oo,hh; rtw8723d_txpwr_calc(6,&cc,&oo,&hh);
      ck(rtw8723d_pt_avg==45&&rtw8723d_pt_delta==10&&rtw8723d_pt_rem_ofdm==4&&(*(u32*)(regs+0xAB4)&0x7FF)==0x720&&((*(u32*)(regs+0x2C)>>12)&0xFFF)==0x410&&cal_n==2,"delta 10: remnant 4, CCK swing 38, crystal 0x20-16 (pwrtrk_xtal_p), second IQK");
      ck(regs[0xE00]==oo+2&&regs[0xE10]==hh+4&&regs[0xE09]==cc-2,"OFDM/HT TX power index carries the remnant up to the worldwide limit (OFDM 30-28, HT 30-26, CCK 30-32)"); }
    memset(regs,0,sizeof regs); memset(&g_rtw_ra_rpt,0,sizeof g_rtw_ra_rpt); g_rtw_efuse.thermal_meter=0x30; rtw8723d_pt_init(); rtw8723d_pwr_track(1); *(u32*)(regs+0x8A0)=0x28u<<10; rtw8723d_pwr_track(1);
    ck(rtw8723d_pt_delta==-3&&*(u32*)(regs+0xC80)==0x300000c0u&&(*(u32*)(regs+0xAB4)&0x7FF)==0x35F,"colder by 8: negative table, swing 25");
    g_rtw_efuse.thermal_meter=0xFF; *(u32*)(regs+0x840)=0; rtw8723d_pt_init(); rtw8723d_pwr_track(1); ck(*(u32*)(regs+0x840)==0,"no efuse thermal value: power tracking does nothing");

    memset(regs,0,sizeof regs); g_rtw_efuse.thermal_meter=0x18; rtw8723d_pt_init(); g_rtw_ra_rpt.n=1; g_rtw_ra_rpt.rate=0x13; rtw8723d_iqk_done=1; rtw8723d_iqk_res[0]=0x100; rtw8723d_iqk_res[1]=0x10; rtw8723d_iqk_res[4]=0xF0; rtw8723d_iqk_res[5]=0x3F0;
    rtw8723d_pwr_track(1); *(u32*)(regs+0x8A0)=0x1Fu<<10; rtw8723d_pwr_track(1);
    ck(rtw8723d_pt_delta==3&&*(u32*)(regs+0xC80)==0x43D0010Fu&&*(u32*)(regs+0xC4C)==0x20000000u,"after IQK: S1 swing folded with the IQK X/Y (rtw8723d_set_iqk_matrix_by_result), C ext bit");
    ck(*(u32*)(regs+0xCD0)==0x1FCu&&*(u32*)(regs+0xCD4)==0x21E7DEu,"after IQK: S0 A/C/D with a negative Y");
    { static const u16 rr[8]={0xC80,0xC94,0xC4C,0xC14,0xCA0,0xCD0,0xCD4,0xCD8}; for(int i=0;i<8;i++)*(u32*)(regs+rr[i])=0x1000u+i; rtw8723d_iqk_regs(0,0);
      for(int i=0;i<8;i++)*(u32*)(regs+rr[i])=0xBAD; rtw8723d_iqk_done=1; rtw8723d_iqk_regs(0,1); int ok=1; for(int i=0;i<8;i++)ok&=*(u32*)(regs+rr[i])==0x1000u+i;
      ck(ok&&!rtw8723d_iqk_done&&rtw8723d_iqk_res[0]==0,"IQK guard undo restores the 8 matrix registers and drops the results from power tracking");
      ck(rtw8723d_chip.iqk_regs==rtw8723d_iqk_regs&&rtw8723d_chip.pwr_track==rtw8723d_pwr_track&&rtw8723d_chip.lte_base==0x7C0&&rtw8723d_chip.coex_wifi_only==rtw8723d_coex_init,"chip hooks wired"); }

    memset(regs,0,sizeof regs); memset(lte,0,sizeof lte); regs[0x7C3]=0x20; lte[0x38]=0xFF; map[0xC3]=0x01; rtw8723d_efuse_parse(map,512,&o); g_rtw_coex_ok=0; rtw8723d_coex_init(1);
    ck(g_rtw_coex_ok==1&&lte[0x38]==0x777Fu&&(lte[0xA0]&0xFFFF)==0xFFFF&&(lte[0xA4]&0xFFFF)==0xFFFF&&*(u16*)(regs+0x948)==0x200,"coex init: LTE coex bit 7 off, GNT BT SW low / WL SW high via 0x7C0, shared antenna 0x948=0x200");
    ck((regs[0x73]&4)&&(regs[0x67]&0x80)&&(regs[0x40]&0x20)&&(regs[0x41]&2)&&(regs[0x4C6]&0x10)&&(regs[0x790]&0x3F)==5&&regs[0x778]==1&&*(u32*)(regs+0x6C0)==0x55555555u&&*(u16*)(regs+0xAA)==0x8003&&regs[0x1704]==0,"coex init: PTA, owner Wi-Fi, BTG WL, table 1, scoreboard; 0x1700 untouched");
    g_rtw_coex_done=1; ck(rtw_coex_bt_share(1,1,1)==1&&(lte[0x38]&0xFF00)==0&&*(u32*)(regs+0x6C0)==0x66555555u,"BT share on 8723D: GNT to PTA through 0x7C0");
    ck(rtw_coex_bt_share(1,0,0)==1&&lte[0x38]==0x777Fu&&*(u32*)(regs+0x6C0)==0x55555555u,"BT share off restores the LTE coex word and tables"); g_rtw_coex_done=0;
    ck(rtw_cx_tab_8723d[0][0]==0xffffffffu&&rtw_cx_tab_8723d[13][1]==0x6aaa5aaau&&rtw_cx_tdma_8723d[3][1]==0x30&&rtw_cx_tdma_8723d[22][1]==0x0c,"8723D coex sets differ from 8821C where rtw88 does");
    if(argc>2){ static char lb[1<<20], ob[1<<18]; char ld[512]; snprintf(ld,sizeof ld,"%s",argv[1]); char* sl=strrchr(ld,'/'); if(sl)strcpy(sl+1,"rtw8723d.c");
      if(slurp(ld,lb,sizeof lb)&&slurp(argv[2],ob,sizeof ob)){
        static const char* pr[][2]={ {"rtw8723d_ofdm_swing_table[]","pt_ofdm[43]"},{"rtw8723d_cck_swing_table[]","pt_cck[41]"},{"rtw8723d_pwrtrk_2ga_n[]","pt_ga_n[30]"},{"rtw8723d_pwrtrk_2ga_p[]","pt_ga_p[30]"},
            {"rtw8723d_pwrtrk_2gb_n[]","pt_ga_n[30]"},{"rtw8723d_pwrtrk_2gb_p[]","pt_ga_p[30]"},{"rtw8723d_pwrtrk_2g_cck_a_n[]","pt_cck_n[30]"},{"rtw8723d_pwrtrk_2g_cck_a_p[]","pt_cck_p[30]"},
            {"rtw8723d_pwrtrk_2g_cck_b_n[]","pt_cck_n[30]"},{"rtw8723d_pwrtrk_2g_cck_b_p[]","pt_cck_p[30]"},{"rtw8723d_pwrtrk_xtal_p[]","pt_xtal_p[30]"} };
        for(unsigned k=0;k<sizeof pr/sizeof pr[0];k++){ long a[64],b[64]; int na=nums(lb,pr[k][0],a,64), nb=nums(ob,pr[k][1],b,64); int eq=na>0&&na==nb; for(int i=0;eq&&i<na;i++)eq=a[i]==b[i]; ck(eq,pr[k][0]); }
        { long a[64]; int na=nums(lb,"rtw8723d_pwrtrk_xtal_n[]",a,64), z=na==30; for(int i=0;z&&i<na;i++)z=a[i]==0; ck(z,"pwrtrk_xtal_n all zero (code uses 0)"); }
        { long a[160]; int na=nums(lb,"table_sant_8723d[]",a,160), eq=na==76; for(int i=0;eq&&i<76;i++)eq=(u32)a[i]==rtw_cx_tab_8723d[i/2][i%2]; ck(eq,"table_sant_8723d matches rtw88");
          na=nums(lb,"tdma_sant_8723d[]",a,160); eq=na==140; for(int i=0;eq&&i<140;i++)eq=a[i]==rtw_cx_tdma_8723d[i/5][i%5]; ck(eq,"tdma_sant_8723d matches rtw88"); } }
      else ck(0,"rtw88 rtw8723d.c / own source readable"); }
    if(fails){ printf("rtw8723d: %d failures\n",fails); return 1; }
    ck(rtw8723d_txpwr_limit(0,13,0,13,1)==14&&rtw8723d_txpwr_limit(0,13,0,13,0)==17&&rtw8723d_txpwr_limit(0,12,0,12,2)==23&&rtw8723d_txpwr_limit(3,13,0,13,1)==14,"FCC ch13 OFDM 14 / CCK 17, ch12 HT 23, IC uses FCC (regd alternative)");
    ck(rtw8723d_txpwr_limit(11,6,0,6,1)==31&&rtw8723d_txpwr_limit(4,6,0,6,2)==31&&rtw8723d_txpwr_limit(12,1,0,1,1)==26&&rtw8723d_txpwr_limit(99,1,0,1,2)==24,"UK/KCC use ETSI, worldwide (and unknown) is the minimum over FCC/MKK/ETSI");
    ck(rtw8723d_txpwr_limit(0,14,0,14,1)==14&&rtw8723d_txpwr_limit(1,14,0,14,2)==31&&rtw8723d_txpwr_limit(0,14,0,14,0)==17&&rtw8723d_txpwr_limit(12,14,0,14,0)==30,"ch14 OFDM/HT unset (63) falls back to the lowest limit of that regulator (stricter than rtw88)");
    ck(rtw8723d_txpwr_limit(0,9,1,11,2)==20&&rtw8723d_txpwr_limit(0,9,0,11,2)==30&&rtw8723d_txpwr_limit(0,11,1,9,1)==26,"HT40: min of HT20 at the primary and HT40 at the centre; OFDM stays 20 MHz");
    t_dbm=5; ck(rtw8723d_txpwr_limit(2,6,0,6,1)==10,"regdb dBm cap lowers the limit"); t_dbm=0;
    { int bad=0; for(int rg=0;rg<=13;rg++)for(u8 ch=1;ch<=14;ch++)for(int rem=-3;rem<=12;rem+=3){ t_regd=rg; rtw8723d_pt_rem_ofdm=(int8_t)rem; rtw8723d_pt_rem_cck=(int8_t)(rem/2); rtw8723d_set_tx_power(0,ch);
        u8 c,o,h; rtw8723d_txpwr_calc(ch,&c,&o,&h); int lc=rtw8723d_txpwr_limit(rg,ch,0,ch,0), lo=rtw8723d_txpwr_limit(rg,ch,0,ch,1), lh=rtw8723d_txpwr_limit(rg,ch,0,ch,2);
        if(regs[0xE09]>c+lc-32||regs[0xE00]>o+lo-28||regs[0xE10]>h+lh-26||regs[0xE13]!=regs[0xE10]||regs[0x86D]!=regs[0xE09]||regs[0xE00]>o+rem||rtw8723d_txpwr_used[3]!=rg)bad++; }
      ck(!bad,"TX power index never exceeds efuse base + (limit - by-rate base) for any regulator/channel/remnant"); t_regd=12; rtw8723d_pt_rem_ofdm=rtw8723d_pt_rem_cck=0; }
    if(argc>1){ static char lb[1<<20]; static long lv[2048]; char* b=slurp(argv[1],lb,sizeof lb); int n=b?nums(b,"rtw8723d_txpwr_lmt[]",lv,2048):-1, bad=n!=(int)sizeof rtw8723d_txpwr_lmt;
        for(int i=0;!bad&&i<n;i++)if(lv[i]!=rtw8723d_txpwr_lmt[i/6][i%6])bad++; ck(!bad,"txpwr_lmt table matches Linux rtw8723d_table.c"); }
    printf("rtw8723d: all ok\n"); return 0; }
