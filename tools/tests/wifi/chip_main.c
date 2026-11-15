/* Simulated Realtek card for the rtw88 multi-chip layer. Registers are a 64 KB byte array; SIPI writes (0xC90/0xE90,
   0x1808/0x4108) land in the RF register files, which show up at the direct-read windows (0x2800/0x2C00, 0x3C00/0x4C00).
   0x132C is the H2C ring index register: host index in bits 11:0, card index in bits 27:16. The simulated firmware
   fetches every packet the driver queues (unless h2c_stuck), and answers an IQK packet by setting RF 0x08 = 0xABCDE.
   Every register write and delay is traced so old (v10.99) and new code can be compared step by step. */
static u8 R[0x10000]; static u32 RF[2][256];
static int trace_on=0, tn=0, card_gone=0, h2c_stuck=0, iqk_answer=1, nh2c=0, nettype=0; static u32 h2c_host=0, h2c_hw=0; static u64 waited=0, iowaits=0;
#define TMAX 400000
static u32 T[TMAX][3]; static u8 H2C[256][32];
static void tr(u32 k,u32 o,u32 v){ if(trace_on&&tn<TMAX){ T[tn][0]=k; T[tn][1]=o; T[tn][2]=v; tn++; } }
static u32 raw32(u32 o){ u32 v; memcpy(&v,R+o,4); return v; }
static void put32(u32 o,u32 v){ memcpy(R+o,&v,4); }
static void rf_set(int p,u32 a,u32 v,u32 base){ RF[p][a&0xFF]=v&0xFFFFFu; put32(base+((a&0xFF)<<2),v&0xFFFFFu); }
static void h2c_fetch(void){
    if(h2c_stuck)return;
    u32 num=raw32(0x1328)&0xFFFu; if(!num)return;
    u8* ring=(u8*)(u64)raw32(0x1320);
    while(h2c_hw!=h2c_host){ u8* bd=ring+h2c_hw*16; u8* pk=(u8*)(u64)(*(u32*)(bd+12)); if(nh2c<256)memcpy(H2C[nh2c],pk,32); nh2c++;
        if(pk[0]==0x01&&pk[1]==0xFF&&pk[2]==0x0E&&iqk_answer)rf_set(0,0x08,0xABCDE,0x2800);
        h2c_hw=(h2c_hw+1)%num; } }
static void sim_w(u32 o,u32 v,int sz){
    tr(sz,o,v);
    if(card_gone)return;
    if(o==0x132C&&sz==2){ h2c_host=v&0xFFFu; h2c_fetch(); return; }
    if(o==0x1330&&sz==4){ if(v&(1u<<16))h2c_host=0; if(v&(1u<<8))h2c_hw=0; put32(o,v&~((1u<<16)|(1u<<8))); return; }
    memcpy(R+o,&v,(size_t)sz);
    if(sz==4&&(o==0x0C90||o==0x0E90))rf_set(o==0x0E90,v>>20,v,o==0x0E90?0x2C00:0x2800);
    if(sz==4&&(o==0x1808||o==0x4108))rf_set(o==0x4108,v>>20,v,o==0x4108?0x4C00:0x3C00);
}
static u32 sim_r(u32 o,int sz){
    if(card_gone)return sz==1?0xFFu:sz==2?0xFFFFu:0xFFFFFFFFu;
    u32 v=0; memcpy(&v,R+o,(size_t)sz);
    if(o==0x132C)v=(h2c_hw<<16)|h2c_host;
    if(o==0x0208)v&=~1u;
    if(o==0x1700)v|=(1u<<29);
    if(o==0x0006)v|=0x02u;
    if(o==0x0005)v&=~0x03u;
    if(o==0x0100&&sz==4)v=(v&~0x30000u)|((u32)nettype<<16);
    return v;
}
static u8  s_r8(u32 h,u32 o){ (void)h; return (u8)sim_r(o,1); }
static u16 s_r16(u32 h,u32 o){ (void)h; return (u16)sim_r(o,2); }
static u32 s_r32(u32 h,u32 o){ (void)h; return sim_r(o,4); }
static void s_w8(u32 h,u32 o,u8 v){ (void)h; sim_w(o,v,1); }
static void s_w16(u32 h,u32 o,u16 v){ (void)h; sim_w(o,v,2); }
static void s_w32(u32 h,u32 o,u32 v){ (void)h; sim_w(o,v,4); }
static const rtw_bus simbus={"sim",RTW_BUS_PCIE,s_r8,s_r16,s_r32,s_w8,s_w16,s_w32};
const rtw_bus* g_rtw_bus=&simbus;
void rtw_delay_us(u32 us){ waited+=us; tr(9,us,0); }
void io_wait(void){ iowaits++; }
void cpu_wmb(void){}
static char jl[64][96]; static int njl=0; static int nklog=0;
void klog(const char* s){ (void)s; nklog++; }
void tcol(u32 c){ (void)c; } void term_puts(const char* s){ (void)s; } void term_putc(char c){ (void)c; }
void rtw_jlog(const char* a,const char* b,int v){ if(njl<64)snprintf(jl[njl++],96,"%s%s %d",a,b,v); }
void ui_pump(void){}
int rtw_get_nettype(u32 m){ (void)m; return nettype; }
static int mdio_n=0, mdio_a=-1, mdio_d=-1, mdio_g1=-1;
int rtw_mdio_write(u32 m,u8 addr,u16 data,int g1){ (void)m; mdio_n++; mdio_a=addr; mdio_d=data; mdio_g1=g1; return 0; }
static u8 dpool[16*4096] __attribute__((aligned(4096))); static int dused=0;
void* dma_alloc_pages(int n){ if(dused+n>16)return 0; void* p=dpool+dused*4096; dused+=n; return p; }

const rtw_chip rtw8822b_chip={ .name="FAKE8822B", .vendor=0x10EC, .device=0xB822, .bus=RTW_BUS_PCIE, .n_paths=2, .wcpu=RTW_WCPU_3081, .txff_size=262144, .rxff_size=24576, .rsvd_drv_pages=8 };
static const rtw_chip mixchip={ .name="MIX", .vendor=0x10EC, .device=0x1234, .bus=RTW_BUS_PCIE, .n_paths=2, .rf_base={0x3C00,0x4C00}, .rf_sipi={0x1808,0x4108}, .rf_mix=1 };

static u32 seed=12345; static u32 rnd(void){ seed=seed*1103515245u+12345u; return seed>>8; }
static u8 R0[0x10000]; static u8 L0[512];
static void snap_init(u32 s){ seed=s; for(int i=0;i<0x10000;i++)R0[i]=(u8)rnd(); for(int i=0;i<512;i++)L0[i]=(u8)rnd(); }
static void restore(int rfe,int valid,int coex,int band5){
    memcpy(R,R0,sizeof R); memset(RF,0,sizeof RF); for(int a=0;a<256;a++){ RF[0][a]=raw32(0x2800+a*4)&0xFFFFFu; RF[1][a]=raw32(0x2C00+a*4)&0xFFFFFu; }
    memcpy(rtw_logmap,L0,512); rtw_logmap[0xCA]=(u8)((rtw_logmap[0xCA]&0xE0)|rfe); rtw_efuse_valid=valid; memset(&g_rtw_efuse,0,sizeof g_rtw_efuse); rtw_efuse_apply();
    g_rtw_coex_done=coex; g_wifi_band5=band5; g_wifi_curch=0; memset(g_wifi_pwr,0,sizeof g_wifi_pwr); g_wifi_txpwr=1; h2c_host=h2c_hw=0;
    tn=0; waited=0; iowaits=0; trace_on=1; }
static u32 TA[TMAX][3]; static int tna; static u8 RA[0x10000]; static u8 PA[20]; static int BA, CA; static u64 WA, IA;
static void keep(void){ trace_on=0; tna=tn; memcpy(TA,T,sizeof(T[0])*(size_t)tn); memcpy(RA,R,sizeof R); memcpy(PA,g_wifi_pwr,20); BA=g_wifi_band5; CA=g_wifi_curch; WA=waited; IA=iowaits; }
static int same(const char* what){ trace_on=0;
    if(tna!=tn){ printf("%s: %d writes before, %d now\n",what,tna,tn); int k=0; while(k<tn&&k<tna&&!memcmp(TA[k],T[k],12))k++; printf("  first difference at #%d: old %u/%04x=%x new %u/%04x=%x\n",k,TA[k][0],TA[k][1],TA[k][2],T[k][0],T[k][1],T[k][2]); return 0; }
    for(int k=0;k<tn;k++) if(memcmp(TA[k],T[k],12)){ printf("%s: write #%d differs: old %u/%04x=%x new %u/%04x=%x\n",what,k,TA[k][0],TA[k][1],TA[k][2],T[k][0],T[k][1],T[k][2]); return 0; }
    if(memcmp(RA,R,sizeof R)||memcmp(PA,g_wifi_pwr,20)||BA!=g_wifi_band5||CA!=g_wifi_curch||WA!=waited||IA!=iowaits){ printf("%s: end state differs\n",what); return 0; }
    return 1; }

int main(void){ int bad=0, cmp=0;
    /* 1: chip lookup */
    if(rtw_chip_find(0x10EC,0xC821,RTW_BUS_PCIE)!=&rtw8821c_chip){ bad++; puts("1 10ec:c821 not found"); }
    if(rtw_chip_find(0x10EC,0xB821,RTW_BUS_PCIE)!=&rtw8821c_chip||rtw_chip_find(0x10EC,0xC82A,RTW_BUS_PCIE)!=&rtw8821c_chip||rtw_chip_find(0x10EC,0xC82B,RTW_BUS_PCIE)!=&rtw8821c_chip){ bad++; puts("1 8821C aliases"); }
    if(rtw_chip_find(0x10EC,0xB822,RTW_BUS_PCIE)!=&rtw8822b_chip){ bad++; puts("1 registered 8822B (strong over weak) not found"); }
    if(rtw_chip_find(0x10EC,0xC822,RTW_BUS_PCIE)||rtw_chip_find(0x10EC,0xC82F,RTW_BUS_PCIE)){ bad++; puts("1 missing 8822C file should give no chip"); }
    if(rtw_chip_find(0x8086,0xC821,RTW_BUS_PCIE)||rtw_chip_find(0x10EC,0xC821,RTW_BUS_USB)||rtw_chip_find(0x10EC,0x8168,RTW_BUS_PCIE)){ bad++; puts("1 wrong vendor/bus/device matched"); }
    if(rtw_chip_count()!=2||rtw_chip_at(0)!=&rtw8821c_chip||rtw_chip_at(1)!=&rtw8822b_chip||rtw_chip_at(2)){ bad++; printf("1 count %d\n",rtw_chip_count()); }
    if(rtw_chip_select(0x10EC,0x1111)||g_rtw_chip!=&rtw8821c_chip||rtw_chip_select(0x10EC,0xB822)!=&rtw8822b_chip||g_rtw_chip!=&rtw8822b_chip){ bad++; puts("1 select"); }
    g_rtw_chip=&rtw8821c_chip;
    /* 2: 8821C ops table */
    { const rtw_chip* c=&rtw8821c_chip;
      if(!c->power_on||!c->power_off||!c->efuse_parse||!c->mac_init||!c->phy_init||!c->set_channel||!c->set_tx_power||!c->calibrate||!c->coex_wifi_only||!c->rfe_setup||!c->phy_post){ bad++; puts("2 missing op"); }
      if(c->fw_end-c->fw_start!=256||c->fw_start[0]!=0x5a||!c->fw_name||!c->name){ bad++; puts("2 firmware"); }
      if(c->mac_tbl.n!=rtw_cmac_n||c->bb_tbl.n!=rtw_cbb_n||c->agc_tbl.n!=rtw_cagc_n||c->agc_btg_tbl.n!=rtw_cagc_btg2_n||c->rf_tbl[0].n!=rtw_crf_n||c->rf_tbl[1].t||
         c->mac_tbl.sz!=1||c->bb_tbl.sz!=4||c->agc_tbl.sz!=4||c->rf_tbl[0].sz!=5){ bad++; puts("2 tables"); }
      if(c->page_table.hq!=16||c->page_table.nq!=16||c->page_table.lq!=16||c->page_table.exq!=14||c->page_table.gapq!=1){ bad++; puts("2 page table"); }
      if(c->rqpn.vo!=2||c->rqpn.vi!=2||c->rqpn.be!=1||c->rqpn.bk!=1||c->rqpn.mg!=0||c->rqpn.hi!=3){ bad++; puts("2 rqpn"); }
      if(rtw_rsvd_boundary()!=460||c->n_paths!=1||c->vendor!=0x10EC||c->device!=0xC821||c->bus!=RTW_BUS_PCIE||c->wcpu!=RTW_WCPU_3081){ bad++; printf("2 boundary %u\n",rtw_rsvd_boundary()); }
      u8 map[512]; memset(map,0xFF,512); for(int i=0;i<6;i++)map[0xD0+i]=(u8)(0xA0+i); map[0xCA]=0x24; map[0xB9]=0x2B; map[0xBA]=0x1C; map[0xCB]='S'; map[0xCC]='E';
      rtw_efuse_info e; if(c->efuse_parse(map,512,&e)||e.mac[0]!=0xA0||e.mac[5]!=0xA5||e.rfe_option!=0x24||e.crystal_cap!=0x2B||e.thermal_meter!=0x1C||e.country_code[0]!='S'){ bad++; puts("2 efuse parse"); }
      if(c->efuse_parse(map,100,&e)==0){ bad++; puts("2 short efuse accepted"); }
      if(rtw_efuse_phy_sz()!=512||rtw_efuse_log_sz()!=512||rtw_efuse_prot_sz()!=96){ bad++; puts("2 8821C efuse sizes"); }
      if(!c->pcie_phy||c->pcie_phy(0)!=0||mdio_n!=1||mdio_a!=0x09||mdio_d!=0x6380||mdio_g1!=1){ bad++; puts("2 8821C PCIe PHY write"); } }
    /* 3: old (v10.99) and new code write the same registers in the same order */
    snap_init(777);
    static const int RFE[]={0,2,3,4,5,7,11,12,13,15}; static const u8 CH[]={1,6,11,13,14,36,48,64,100,116,140,149,165};
    for(int ri=0;ri<10;ri++) for(int v=0;v<2;v++) for(int co=0;co<2;co++) for(int b5=0;b5<2;b5++) for(int ci=0;ci<13;ci++){
        u8 ch=CH[ci];
        restore(RFE[ri],v,co,b5); old_rtw_set_channel_2g(0,ch); keep();
        restore(RFE[ri],v,co,b5); rtw_set_channel_2g(0,ch); cmp++;
        if(!same("3 set_channel")){ bad++; printf("  rfe %d valid %d coex %d band5 %d ch %u\n",RFE[ri],v,co,b5,ch); goto out3; } }
    for(int ri=0;ri<10;ri++) for(int v=0;v<2;v++){
        restore(RFE[ri],v,0,0); int a=old_rtw_coex_wifi_only(0); keep(); restore(RFE[ri],v,0,0); int b=rtw_coex_wifi_only(0); cmp++;
        if(!same("3 coex")||a!=b||!g_rtw_coex_done){ bad++; printf("  rfe %d ok %d/%d\n",RFE[ri],a,b); goto out3; } }
    restore(0,1,0,0); old_rtw_mac_trx_init(0); keep(); restore(0,1,0,0); rtw_mac_trx_init(0); cmp++; if(!same("3 mac_trx_init"))bad++;
    if(raw32(0x0204)%65536!=460||(raw32(0x0244)&0x3FFFFu)!=(500u<<7)||(raw32(0x010C)&0xFFFF)!=0xC5A0u||raw32(0x011C)!=16127u){ bad++; puts("3 trx values"); }
    restore(0,1,0,0); old_rtw_wmac_init(0); keep(); restore(0,1,0,0); rtw8821c_chip.mac_init(0); cmp++; if(!same("3 mac_init"))bad++;
    restore(0,1,0,0); old_rtw_phy_power_on(0); keep(); restore(0,1,0,0); rtw8821c_chip.phy_init(0); cmp++; if(!same("3 phy_init"))bad++;
    for(int v=0;v<2;v++) for(int x=0;x<2;x++){ restore(0,v,0,0); if(x)rtw_logmap[0xB9]=0xFF; rtw_efuse_apply(); u8 xk=rtw_logmap[0xB9]; old_rtw_phy_post(0,(rtw_efuse_valid&&xk!=0xFF),xk); keep();
        restore(0,v,0,0); if(x)rtw_logmap[0xB9]=0xFF; rtw_efuse_apply(); rtw8821c_chip.phy_post(0); cmp++; if(!same("3 phy_post")){ bad++; break; } }
    restore(0,1,0,0); int po=old_power_on(0); keep(); restore(0,1,0,0); int pn=rtw8821c_chip.power_on(0); cmp++; if(!same("3 power_on")||po!=0||pn!=0){ bad++; printf("  power on %d %d\n",po,pn); }
    for(int k=0;k<4000;k++){ u32 a=rnd()&0xFF, msk; int sh=(int)(rnd()%20), w=1+(int)(rnd()%20); if(sh+w>20)w=20-sh; msk=(k&7)==0?0xFFFFFu:(((1u<<w)-1u)<<sh); u32 val=rnd()&0xFFFFFu;
        restore(0,1,0,0); old_rtw_rf_write_mask_a(0,a,msk,val); u32 ra=old_rtw_rf_read_a(0,a); keep(); restore(0,1,0,0); rtw_rf_write(0,0,a,msk,val); u32 rb=rtw_rf_read_a(0,a); cmp++;
        if(!same("3 rf masked write")||ra!=rb){ bad++; printf("  addr %x mask %x val %x\n",a,msk,val); break; } }
    for(u32 a=0;a<256;a++){ restore(0,1,0,0); old_rtw_rf_write_a(0,a,a*4099u); keep(); restore(0,1,0,0); rtw_apply_one(0,a,5,a*4099u); if(a==0xFE||a==0xFF)continue; cmp++; if(!same("3 rf table write")){ bad++; break; } }
    out3:
    trace_on=0;
    /* 4: RF path selection */
    restore(0,1,0,0); trace_on=0; g_rtw_chip=&rtw8821c_chip;
    rtw_rf_write(0,0,0x18,0xFFFFFu,0x12345); if(raw32(0x0C90)!=((0x18u<<20)|0x12345u)||rtw_rf_read(0,0,0x18,0xFFFFFu)!=0x12345u){ bad++; puts("4 path A SIPI"); }
    put32(0x0E90,0); rtw_rf_write(0,1,0x18,0xFFFFFu,0x54321); if(raw32(0x0E90)!=0||rtw_rf_read(0,1,0x18,0xFFFFFu)!=0xFFFFFFFFu){ bad++; puts("4 1-path chip took path B"); }
    rtw_rf_write(0,0,0xDF,1u<<6,1); if(!(RF[0][0xDF]&0x40)){ bad++; puts("4 masked"); } rtw_rf_write(0,0,0xDF,1u<<6,0); if(RF[0][0xDF]&0x40){ bad++; puts("4 masked clear"); }
    if(rtw_rf_read(0,0,0x18,0xF0u)!=((0x12345u>>4)&0xFu)){ bad++; puts("4 masked read"); }
    g_rtw_chip=&rtw8822b_chip;
    rtw_rf_write(0,1,0x18,0xFFFFFu,0x54321); if(raw32(0x0E90)!=((0x18u<<20)|0x54321u)||rtw_rf_read(0,1,0x18,0xFFFFFu)!=0x54321u||RF[0][0x18]!=0x12345u){ bad++; puts("4 path B SIPI 0xE90 / read 0x2C00"); }
    rtw_rf_write(0,2,0x18,0xFFFFFu,1); if(rtw_rf_read(0,2,0x18,0xFFFFFu)!=0xFFFFFFFFu||rtw_rf_read(0,-1,0,0xFFFFF)!=0xFFFFFFFFu){ bad++; puts("4 path 2 / -1"); }
    g_rtw_chip=&mixchip; put32(0x1808,0); put32(0x4108,0);
    put32(0x4C00+0x18*4,0xFFFFFu); rtw_rf_write(0,1,0x18,0x00F00u,0x3); if(raw32(0x4C00+0x18*4)!=0xFF3FFu||raw32(0x4108)!=0){ bad++; printf("4 mix direct %x\n",raw32(0x4C00+0x18*4)); }
    rtw_rf_write(0,0,0x00,0xFFFFFu,0x33333); if(raw32(0x1808)!=0x33333u||rtw_rf_read(0,0,0x00,0xFFFFFu)!=0x33333u){ bad++; puts("4 mix RF00 via SIPI"); }
    rtw_bb_write_mask(0,0x0808,0x30000000u,2); if(((raw32(0x0808)>>28)&3u)!=2u||rtw_bb_read_mask(0,0x0808,0x30000000u)!=2u){ bad++; puts("4 bb mask"); }
    g_rtw_chip=&rtw8821c_chip;
    /* 5: H2C packet ring */
    memset(R,0,sizeof R); h2c_host=h2c_hw=0; nh2c=0; h2c_stuck=0; card_gone=0;
    u8 pk[32]; for(int i=0;i<32;i++)pk[i]=(u8)i;
    if(rtw_h2c_pkt_send(0,pk,32)!=-1){ bad++; puts("5 send without ring"); }
    if(rtw_h2c_ring_setup(0)!=0||(raw32(0x1328)&0xFFFF)!=32||!raw32(0x1320)){ bad++; puts("5 ring setup"); }
    u8* ring=(u8*)(u64)raw32(0x1320); u32 sent0=g_rtw_h2c_sent;
    for(int n=0;n<100;n++){ pk[8]=(u8)n; u32 slot=(u32)n%32u;
        int r=rtw_h2c_pkt_send(0,pk,32); u8* bd=ring+slot*16; u8* buf=(u8*)(u64)(*(u32*)(bd+4));
        if(r!=0||*(u16*)bd!=48||*(u16*)(bd+2)!=1||*(u16*)(bd+8)!=32||*(u32*)(bd+12)!=*(u32*)(bd+4)+48||*(u32*)buf!=32u||((*(u32*)(buf+4)>>8)&0x1Fu)!=19u){ bad++; printf("5 packet %d descriptor r=%d\n",n,r); break; }
        if(H2C[n][8]!=(u8)n||H2C[n][0]!=0||H2C[n][31]!=31||(H2C[n][6]|(H2C[n][7]<<8))!=n){ bad++; printf("5 packet %d content/seq\n",n); break; }
        if((sim_r(0x132C,4)&0xFFF)!=((u32)n+1u)%32u){ bad++; printf("5 index %u after %d\n",sim_r(0x132C,4)&0xFFF,n); break; } }
    if(g_rtw_h2c_sent-sent0!=100){ bad++; puts("5 sent count"); }
    if(rtw_h2c_pkt_send(0,pk,7)!=-1||rtw_h2c_pkt_send(0,pk,33)!=-1||rtw_h2c_pkt_send(0,0,32)!=-1){ bad++; puts("5 bad length"); }
    h2c_stuck=1; int full=0, got1=0; for(int n=0;n<40;n++){ int r=rtw_h2c_pkt_send(0,pk,32); if(r==1)got1++; if(r==-2){ full=n; break; } }
    if(full!=31||got1!=31){ bad++; printf("5 stuck ring: full after %d, not fetched %d\n",full,got1); }
    h2c_stuck=0; h2c_fetch(); if(rtw_h2c_pkt_send(0,pk,32)!=0){ bad++; puts("5 ring did not recover"); }
    card_gone=1; if(rtw_h2c_pkt_send(0,pk,32)!=-1){ bad++; puts("5 card gone"); } card_gone=0;
    /* 6: calibration (IQK) through the chip op */
    memset(R,0,sizeof R); memset(RF,0,sizeof RF); h2c_host=h2c_hw=0; nh2c=0; njl=0; rtw_efuse_valid=1; g_rtw_efuse.rfe_option=0x04; put32(0x00F0,0x2000u);
    if(g_rtw_iqk_en!=0){ bad++; puts("6 IQK must be off by default"); } g_rtw_iqk_en=1;
    rtw_h2c_ring_off(); memset(R,0,sizeof R); put32(0x00F0,0x2000u); put32(0x0080,0xC000u); g_rtw_iqk_en=0; rtw_chip_calibrate(1); if(raw32(0x1320)||raw32(0x1328)||nh2c){ bad++; puts("6 IQK off must not touch the H2C ring registers"); } g_rtw_iqk_en=1;
    put32(0x0080,0); rtw_chip_calibrate(1); if(!raw32(0x1320)||(raw32(0x1328)&0xFFFF)!=32){ bad++; puts("6 IQK on did not set up the ring on demand"); }
    rtw_h2c_ring_setup(0); put32(0x0080,0); rtw_chip_calibrate(1); if(g_rtw_iqk_last!=-4||nh2c){ bad++; printf("6 fw not running: %d\n",g_rtw_iqk_last); }
    put32(0x0080,0xC000u); g_rtw_iqk_en=0; rtw_chip_calibrate(1); if(nh2c){ bad++; puts("6 IQK off still sent"); } g_rtw_iqk_en=1;
    waited=0; rtw_chip_calibrate(1);
    if(g_rtw_iqk_last<=0||nh2c!=3||H2C[0][2]!=0x0D||H2C[1][2]!=0x11||H2C[2][2]!=0x0E||H2C[0][10]!=48||H2C[1][8]!=4||H2C[1][9]!=4||H2C[1][10]!=2||H2C[1][11]!=0x11||H2C[2][4]!=9||H2C[2][8]!=0||RF[0][8]!=0){ bad++; printf("6 IQK ok path: last %d packets %d\n",g_rtw_iqk_last,nh2c); }
    if(waited>100000){ bad++; printf("6 IQK waited %llu us with an answering card\n",waited); }
    { const rtw_chip* sv=g_rtw_chip; g_rtw_chip=&rtw8822b_chip; put32(0x00F0,0x2000u); int p1=rtw_rf_paths(1); put32(0x00F0,0x2000u|(1u<<27)); int p2=rtw_rf_paths(1); put32(0x00F0,0x2000u); g_rtw_chip=sv;
      if(p1!=1||p2!=2||rtw_rf_paths(1)!=1){ bad++; printf("6 path count from SYS_CFG1: %d %d\n",p1,p2); } }
    nettype=2; rtw_chip_calibrate(1); nettype=0; if(nh2c!=4||H2C[3][2]!=0x0E||H2C[3][8]!=2){ bad++; puts("6 connected IQK should be segment IQK, info sent once"); }
    iqk_answer=0; waited=0; int nk=nklog; rtw_chip_calibrate(1);
    if(g_rtw_iqk_last!=-2||!g_rtw_iqk_dead||nklog!=nk+1||waited<5000000||waited>7000000){ bad++; printf("6 IQK timeout: last %d dead %d waited %llu\n",g_rtw_iqk_last,g_rtw_iqk_dead,waited); }
    int nb=nh2c; rtw_chip_calibrate(1); if(nh2c!=nb){ bad++; puts("6 IQK retried after timeout"); }
    rtw_h2c_ring_setup(0); iqk_answer=1; if(g_rtw_iqk_dead){ bad++; puts("6 restart did not re-enable IQK"); }
    h2c_stuck=1; waited=0; rtw_chip_calibrate(1); if(g_rtw_iqk_last!=-1||waited>200000){ bad++; printf("6 stuck command ring: last %d waited %llu\n",g_rtw_iqk_last,waited); } h2c_stuck=0;
    printf("%d old/new trace compares, %d failures\n",cmp,bad);
    if(!bad)puts("chip ok");
    return bad?1:0; }
