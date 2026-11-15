/* RTL8821CE code as it was in v10.99 (before the rtw_chip split), renamed old_*. tools/tests/wifi/chip_main.c
   runs it and the new chip ops on the same simulated card and compares every register write and delay. */
#define RTW_RFA_SIPI 0xC90u
static int g_chstep;
static void chmark(const char* t){ (void)t; }
static void old_rtw_wm32(u32 m,u32 r,u32 mask,u32 v);
static void old_rtw_rf_write_a(u32 m,u32 addr,u32 data);
static void old_rtw_rf_write_a(u32 m,u32 addr,u32 data){
    u32 daa=(((addr&0xFF)<<20)|(data&0x000FFFFFu))&0x0FFFFFFFu;
    rtw_w32(m,RTW_RFA_SIPI,daa);
    rtw_delay_us(13);
}
static u32 old_rtw_rf_read_a(u32 m,u32 addr){ return rtw_r32(m,0x2800u+((addr&0xFFu)<<2)) & 0x000FFFFFu; }
static void old_rtw_rf_write_mask_a(u32 m,u32 addr,u32 mask,u32 val){
    if(mask==0x000FFFFFu){ old_rtw_rf_write_a(m,addr,val); return; }
    u32 sh=0; while(sh<20 && !((mask>>sh)&1u))sh++;
    u32 cur=old_rtw_rf_read_a(m,addr);
    cur=(cur&~mask)|((val<<sh)&mask);
    old_rtw_rf_write_a(m,addr,cur);
}
static void old_rtw_switch_to_wlg(u32 m){
    rtw_w32(m,0x1080,rtw_r32(m,0x1080)|(1u<<16));
    rtw_w32(m,0x0000,rtw_r32(m,0x0000)|(1u<<26));
    u32 reg=rtw_r32(m,0x0cb8);
    reg|=(1u<<20)|(1u<<22)|(1u<<21);
    reg&=~((1u<<16)|(1u<<18)|(1u<<23));
    u32 a84=rtw_r32(m,0x0a84); a84=(a84&~0x00FF0000u)|(0x12u<<16); rtw_w32(m,0x0a84,a84);
    u32 a80=rtw_r32(m,0x0a80); a80=(a80&~0x0000FFFFu)|0x7532u; rtw_w32(m,0x0a80,a80);
    rtw_w32(m,0x0cb8,reg);
}
static void old_rtw_switch_to_wla(u32 m){
    rtw_w32(m,0x1080,rtw_r32(m,0x1080)|(1u<<16));
    rtw_w32(m,0x0000,rtw_r32(m,0x0000)|(1u<<26));
    u32 reg=rtw_r32(m,0x0cb8); reg|=(1u<<20)|(1u<<22)|(1u<<23); reg&=~((1u<<16)|(1u<<18)|(1u<<21)); rtw_w32(m,0x0cb8,reg);
}
static void old_rtw_switch_to_btg(u32 m){
    rtw_w32(m,0x1080,rtw_r32(m,0x1080)|(1u<<16));
    rtw_w32(m,0x0000,rtw_r32(m,0x0000)|(1u<<26));
    u32 reg=rtw_r32(m,0x0cb8); reg|=(1u<<16); reg&=~((1u<<18)|(1u<<20)|(1u<<21)|(1u<<22)|(1u<<23));
    u32 a84=rtw_r32(m,0x0a84); a84=(a84&~0x00FF0000u)|(0x0Eu<<16); rtw_w32(m,0x0a84,a84);
    u32 a80=rtw_r32(m,0x0a80); a80=(a80&~0x0000FFFFu)|0xFC84u; rtw_w32(m,0x0a80,a80);
    rtw_w32(m,0x0cb8,reg);
}
static int old_rtw_rfe(void){ return rtw_efuse_valid?(rtw_logmap[0xCA]&0x1f):0; }
static int old_rtw_rfe_btg(void){ int r=old_rtw_rfe(); return r==2||r==4||r==7||r==0xa||r==0xc||r==0xf; }
static int old_rtw_coex_wifi_only(u32 m){
    int rfe=old_rtw_rfe();
    int at_btg=(rfe==2||rfe==10||rfe==7||rfe==15||rfe==4||rfe==12);
    int pol=(rfe==3||rfe==11||rfe==4||rfe==12);
    rtw_w8(m,0x0002,(u8)(rtw_r8(m,0x0002)|0x03u));
    rtw_w8(m,0x0550,(u8)(rtw_r8(m,0x0550)|(1u<<3)));
    rtw_w8(m,0x0790,(u8)((rtw_r8(m,0x0790)&~0x3Fu)|0x05u));
    rtw_w8(m,0x0778,0x01);
    rtw_w32(m,0x0040,rtw_r32(m,0x0040)|(1u<<5)|(1u<<9));
    rtw_w8(m,0x04C6,(u8)((rtw_r8(m,0x04C6)|(1u<<4))&~(1u<<5)));
    rtw_w16(m,0x0762,(u16)(rtw_r16(m,0x0762)|(1u<<12)));
    rtw_w8(m,0x06CF,(u8)(rtw_r8(m,0x06CF)|(1u<<3)));
    int ok=1;
    ok&=rtw_lte_mask(m,0x38,0xC000u,1); ok&=rtw_lte_mask(m,0x38,0x0C00u,1);
    ok&=rtw_lte_mask(m,0x38,0x3000u,3); ok&=rtw_lte_mask(m,0x38,0x0300u,3);
    rtw_w8(m,0x0073,(u8)(rtw_r8(m,0x0073)|0x04u));
    u32 rv;
    if(at_btg){ if(pol)rv=2; else rv=(rfe!=4&&rfe!=2)?3:2; }
    else rv=pol?1:2;
    int sw=!(rfe==5||rfe==6||rfe==13||rfe==14);
    if(sw){
        rtw_w32(m,0x004C,(rtw_r32(m,0x004C)&~(1u<<23))|(1u<<24));
        rtw_w8(m,0x0CB4,0x77);
        rtw_w32(m,0x0CB4,(rtw_r32(m,0x0CB4)&~0xF0000000u)|(rv<<28));
        rtw_w8(m,0x0067,(u8)(rtw_r8(m,0x0067)|(1u<<5)|(1u<<4)));
    }
    rtw_w32(m,0x06C0,0x55555555u); rtw_w32(m,0x06C4,0x55555555u); rtw_w32(m,0x06C8,0xF0FFFFFFu);
    rtw_w16(m,0x00AA,(u16)(0x8000u|0x0003u));
    rtw_w8(m,0xFF1A,0);
    g_rtw_coex_done=1;
    return ok;
}
static void old_rtw_ant_band(u32 m,int g5){
    int rfe=old_rtw_rfe();
    if(rfe==5||rfe==6||rfe==13||rfe==14)return;
    if(rfe==2||rfe==10||rfe==7||rfe==15||rfe==4||rfe==12)return;
    int pol=(rfe==3||rfe==11);
    u32 rv=g5?(pol?2u:1u):(pol?1u:2u);
    rtw_w32(m,0x0CB4,(rtw_r32(m,0x0CB4)&~0xF0000000u)|(rv<<28));
}
static void old_rtw_wm32(u32 m,u32 r,u32 mask,u32 v){ u32 sh=0; while(sh<31&&!((mask>>sh)&1u))sh++; u32 c=rtw_r32(m,r); c=(c&~mask)|((v<<sh)&mask); rtw_w32(m,r,c); }
static void old_rtw_bw20(u32 m,u8 ch){
    u32 v=rtw_r32(m,0x08AC); v=(v&0xffcffc00u)|0x10010000u; rtw_w32(m,0x08AC,v);
    old_rtw_wm32(m,0x08C4,1u<<30,1);
    old_rtw_wm32(m,0x0948,3u<<28,2); old_rtw_wm32(m,0x094C,3u<<28,2); old_rtw_wm32(m,0x0C20,1u<<31,1); old_rtw_wm32(m,0x08F0,1u<<31,0);
    static const u32 SW[4]={0x200,0x16a,0x101,0x0b6}; u8 sv=rtw_efuse_valid?rtw_logmap[ch<=14?0xC6:0xC7]:0; if(sv>9)sv=0; old_rtw_wm32(m,0x0c1c,0xFFE00000u,SW[sv/3]);
}
static void old_rtw_bb_band(u32 m,u8 ch){
    if(ch<=14){ old_rtw_wm32(m,0x0808,1u<<28,1); old_rtw_wm32(m,0x0454,1u<<7,0); old_rtw_wm32(m,0x0a80,1u<<18,0); old_rtw_wm32(m,0x0814,0x0000FC00u,15); old_rtw_wm32(m,0x0c1c,0xF00u,0); old_rtw_wm32(m,0x0860,0x1FFE0000u,0x96A); return; }
    old_rtw_wm32(m,0x0a80,1u<<18,1); old_rtw_wm32(m,0x0454,1u<<7,1); old_rtw_wm32(m,0x0808,1u<<28,0); old_rtw_wm32(m,0x0814,0x0000FC00u,15);
    old_rtw_wm32(m,0x0c1c,0xF00u,ch<=64?1u:ch<=144?2u:3u);
    old_rtw_wm32(m,0x0860,0x1FFE0000u,ch<=48?0x494u:ch<=64?0x453u:ch<=116?0x452u:0x412u);
}
static int old_rtw_s4(u8 v){ v&=0xF; return (v&8)?(int)v-16:(int)v; }
static int old_rtw_pg2(u8 c){ return c<=2?0:c<=5?1:c<=8?2:c<=11?3:c<=13?4:5; }
static int old_rtw_pg5(u8 c){ if(c<=42)return 0; if(c<=50)return 1; if(c<=58)return 2; if(c<=64)return 3; if(c<=106)return 4; if(c<=114)return 5; if(c<=122)return 6; if(c<=130)return 7; if(c<=138)return 8; if(c<=144)return 9; if(c<=155)return 10; if(c<=161)return 11; if(c<=171)return 12; return 13; }
static int old_rtw_clp(int v,int d){ if(v==0xFF||v<0)return d; return v>63?63:v; }
static void old_rtw_set_txpwr(u32 m,u8 ch){
    if(!g_wifi_txpwr||!rtw_efuse_valid)return; const u8* e=rtw_logmap+0x10; int cck=0x20,b40,d;
    if(ch<=14){ int g=old_rtw_pg2(ch); cck=old_rtw_clp(e[g],0x20); b40=old_rtw_clp(e[6+(g>4?4:g)],0x20); d=e[11]; }
    else { b40=old_rtw_clp(e[18+old_rtw_pg5(ch)],0x20); d=e[32]; }
    if(d==0xFF)d=0; int of=b40+old_rtw_s4((u8)d), b20=b40+old_rtw_s4((u8)(d>>4));
    for(int r=0;r<20;r++){ int v=r<4?cck:r<12?of:b20; if(v<0)v=0; if(v>63)v=63; g_wifi_pwr[r]=(u8)v; }
    for(int r=(ch<=14?0:4);r<20;r+=4) rtw_w32(m,0x1d00u+(u32)r,(u32)g_wifi_pwr[r]|((u32)g_wifi_pwr[r+1]<<8)|((u32)g_wifi_pwr[r+2]<<16)|((u32)g_wifi_pwr[r+3]<<24));
}
static void old_rtw_set_channel_2g(u32 m,u8 ch){
    int g5=ch>14;
    chmark("    ch.a read RF18 (0x2860)");
    u32 rf18=old_rtw_rf_read_a(m,0x18);
    g_chstep=2;
    rf18&=~(((1u<<16)|(1u<<9)|(1u<<8)) | 0xFFu | ((1u<<18)|(1u<<17)) | ((1u<<11)|(1u<<10)));
    rf18|=(ch&0xFFu);
    if(g5){ rf18|=(1u<<16)|(1u<<8); if(ch>=100&&ch<=140)rf18|=(1u<<17); else if(ch>140)rf18|=(1u<<18); }
    rf18|=(1u<<11)|(1u<<10);
    chmark("    ch.b antenna switch (0xcb8/0xa80/0xa84)");
    if(g5)old_rtw_switch_to_wla(m); else if(old_rtw_rfe_btg())old_rtw_switch_to_btg(m); else old_rtw_switch_to_wlg(m);
    if(g_rtw_coex_done)old_rtw_ant_band(m,g5);
    g_chstep=3;
    chmark("    ch.c RF LUTDBG/0x64");
    if(g5)old_rtw_rf_write_mask_a(m,0xDF,(1u<<6),0);
    else { old_rtw_rf_write_mask_a(m,0xDF,(1u<<6),1); old_rtw_rf_write_mask_a(m,0x64,0xFu,0xF); }
    g_chstep=4;
    chmark("    ch.d RF18 write (the channel)");
    old_rtw_rf_write_a(m,0x18,rf18);
    rtw_delay_us(100);
    chmark("    ch.e RF XTALX2 toggle");
    old_rtw_rf_write_mask_a(m,0xB8,(1u<<19),0);
    old_rtw_rf_write_mask_a(m,0xB8,(1u<<19),1);
    g_chstep=5;
    chmark("    ch.f MAC channel regs (0x483/0x668)");
    rtw_w8(m,0x0483,0);
    rtw_w32(m,0x0024,rtw_r32(m,0x0024)&~(3u<<20));
    u32 v=rtw_r32(m,0x0668); v&=~((1u<<7)|(1u<<8)); rtw_w32(m,0x0668,v);
    if(g5||g_wifi_band5)old_rtw_bb_band(m,ch);
    old_rtw_bw20(m,ch);
    g_wifi_band5=g5; g_wifi_curch=ch;
    old_rtw_set_txpwr(m,ch);
    rtw_delay_us(2000);
    g_chstep=6;
}
static int old_rtw_mac_trx_init(u32 m){
    u16 pqmap=(u16)((2u<<4)|(2u<<6)|(1u<<8)|(1u<<10)|(0u<<12)|(3u<<14));
    rtw_w16(m,0x010C,pqmap);
    rtw_w8(m,0x0100,0);
    rtw_w8(m,0x0100,0xFF);
    rtw_w32(m,0x1330,(1u<<31));

    u32 rsvd_boundary=460, h2cq_addr_pg=500;
    u16 hq=16,nq=16,lq=16,exq=14,gap=1;
    u16 acq=460;
    u16 pubq=acq-hq-lq-nq-exq-gap;
    rtw_w16(m,0x0230,hq);
    rtw_w16(m,0x0234,lq);
    rtw_w16(m,0x0238,nq);
    rtw_w16(m,0x023C,exq);
    rtw_w16(m,0x0240,pubq);
    rtw_w32(m,0x022C,rtw_r32(m,0x022C)|(1u<<31));
    rtw_w16(m,0x0204,(u16)rsvd_boundary);
    rtw_w8(m,0x0422,(u8)(rtw_r8(m,0x0422)|((1u<<20)>>16)));
    rtw_w16(m,0x0424,(u16)rsvd_boundary);
    rtw_w16(m,0x0206,(u16)rsvd_boundary);
    rtw_w16(m,0x0456,(u16)rsvd_boundary);
    rtw_w32(m,0x011C,16384u-256u-1u);
    rtw_w8(m,0x060F,4);
    rtw_w8(m,0x0208,(u8)(rtw_r8(m,0x0208)|1u));
    int llt=0; for(int t=0;t<1000;t++){ if(!(rtw_r8(m,0x0208)&1u)){llt=1;break;} rtw_delay_us(10); }
    if(!llt)return -1;
    rtw_w8(m,0x0103,0);

    u32 h2cq_addr=h2cq_addr_pg<<7;
    u32 h2cq_size=8u<<7;
    rtw_w32(m,0x0244,(rtw_r32(m,0x0244)&0xFFFC0000u)|h2cq_addr);
    rtw_w32(m,0x024C,(rtw_r32(m,0x024C)&0xFFFC0000u)|h2cq_addr);
    rtw_w32(m,0x0248,(rtw_r32(m,0x0248)&0xFFFC0000u)|(h2cq_addr+h2cq_size));
    rtw_w8(m,0x0254,(u8)((rtw_r8(m,0x0254)&0xFC)|0x01));
    rtw_w8(m,0x0254,(u8)((rtw_r8(m,0x0254)&0xFB)|0x04));
    rtw_w8(m,0x020D,(u8)((rtw_r8(m,0x020D)&0x7F)|0x80));
    rtw_w8(m,0x0115,(u8)((rtw_r8(m,0x0115)&0xF0u)|0x0Fu));
    rtw_w8(m,0x07D5,(u8)(rtw_r8(m,0x07D5)&~0x03u));
    return 0;
}
static void old_rtw_phy_power_on(u32 m){
    u8 v=rtw_r8(m,0x0002);
    v|=(u8)(1u<<6); rtw_w8(m,0x0002,v);
    v|=(u8)((1u<<0)|(1u<<1)); rtw_w8(m,0x0002,v);
    v&=(u8)~((1u<<0)|(1u<<1)); rtw_w8(m,0x0002,v);
    v|=(u8)((1u<<0)|(1u<<1)); rtw_w8(m,0x0002,v);
    rtw_w8(m,0x001F,(u8)((1u<<0)|(1u<<1)|(1u<<2)));
    rtw_delay_us(20);
    rtw_w8(m,0x00EF,(u8)((1u<<0)|(1u<<1)|(1u<<2)));
    rtw_delay_us(20);
    rtw_w32(m,0x0808,rtw_r32(m,0x0808)&~((1u<<28)|(1u<<29)));
}
static void old_rtw_wmac_init(u32 m){
    rtw_w8(m,0x0455,0x70);
    rtw_w8(m,0x045E,(u8)(rtw_r8(m,0x045E)|(1u<<2)));
    u16 pre=(u16)(0x1E4u|(1u<<11));
    rtw_w8(m,0x04E5,(u8)(pre&0xFF)); rtw_w8(m,0x04E6,(u8)(pre>>8));
    rtw_w32(m,0x04C8,0xFFu|(0x08u<<8)|(0x20u<<16)|(0x20u<<24));
    rtw_w16(m,0x04CE,(u16)(0x01u|(0x08u<<8)));
    rtw_w8(m,0x1448,0x06); rtw_w8(m,0x144A,0x06);
    rtw_w8(m,0x144C,0x06); rtw_w8(m,0x144E,0x06);
    rtw_w8(m,0x0480,(u8)(rtw_r8(m,0x0480)|(1u<<5)));
    rtw_w8(m,0x05B4,(u8)(rtw_r8(m,0x05B4)&~((1u<<4)|(1u<<5)|(1u<<6))));
    rtw_w16(m,0x0522,0);
    rtw_w8(m,0x051B,0x09);
    rtw_w8(m,0x0512,0x19);
    rtw_w32(m,0x0514,(0x0Au|(0x0Eu<<8)|(0x10u<<16)|(0x10u<<24)));
    rtw_w16(m,0x0502,0x186);
    rtw_w16(m,0x0506,0x3BC);
    rtw_w32(m,0x0544,(0x05u|(0x1Bu<<16)));
    rtw_w16(m,0x055E,(u16)(0x30u|(0x30u<<8)));
    rtw_w8(m,0x0550,(u8)(rtw_r8(m,0x0550)|(1u<<3)));
    rtw_w32(m,0x0540,(0x04u|(0x064u<<8)));
    rtw_w8(m,0x0558,0x04);
    rtw_w8(m,0x0559,0x02);
    rtw_w8(m,0x0521,(u8)(rtw_r8(m,0x0521)&~((1u<<12)>>8)));
    rtw_w16(m,0x06A0,0xFFFF);
    rtw_w16(m,0x06A2,0x0FFF);
    rtw_w16(m,0x06A4,0xFFFF);
    rtw_w32(m,0x0608,0xE400220Eu);
    rtw_w8(m,0x060C,(u8)(12288u>>9));
    rtw_w8(m,0x0606,0x30);
    rtw_w8(m,0x0605,0x30);
    rtw_w8(m,0x0639,0x40);
    rtw_w8(m,0x066C,(u8)(rtw_r8(m,0x066C)|(1u<<1)));
    rtw_w8(m,0x0718,(u8)(rtw_r8(m,0x0718)|(1u<<6)));
    rtw_w32(m,0x07D8,0xb0810041u);
    rtw_w8(m,0x07D4,0x98);
    rtw_w8(m,0x060F,4);
    rtw_w8(m,0x060C,15);
    rtw_w32(m,0x0608,rtw_r32(m,0x0608)|(1u<<28));
}
static void old_rtw_phy_post(u32 m,int have_xtal,u8 xtal){
    if(have_xtal){
        u32 cap=(u32)(xtal&0x3Fu);
        u32 a=rtw_r32(m,0x0024); a=(a&~0x7E000000u)|((cap<<25)&0x7E000000u); rtw_w32(m,0x0024,a);
        u32 b2=rtw_r32(m,0x0028); b2=(b2&~0x0000007Eu)|((cap<<1)&0x0000007Eu); rtw_w32(m,0x0028,b2);
    }
    rtw_w32(m,0x0A2C,rtw_r32(m,0x0A2C)&~((1u<<18)|(1u<<22)));
    rtw_w32(m,0x0808,rtw_r32(m,0x0808)|((1u<<28)|(1u<<29)));
}
static int old_power_on(u32 m){
    struct { u16 off; u8 mask; u8 val; u8 cmd; } seqp[] = {
                                {0x001C,0xFF,0x00,1},
                                {0x0075,0x01,0x01,1},
                                {0x0067,0x30,0x30,1},
                                {0x004F,0x06,0x00,1},
                                {0x0040,0x04,0x04,1},
                                {0x0002,0x03,0x00,1},
                                {0x001F,0x07,0x00,1},
                                {0x00EF,0x07,0x00,1},
                                {0x0005,0x98,0x00,1},
                                {0x0300,0xFF,0x00,1},
                                {0x0301,0xFF,0x00,1},
                                {0x0005,(0x10|0x08|0x04),0x00,1},      /* APS_FSMCO: clear pd bits            */
                                {0x0075,0x01,0x01,1},                  /* PCIE: enable                        */
                                {0x0006,0x02,0x02,2},                  /* poll SYS_CLKR ready (bit1)          */
                                {0x0075,0x01,0x00,1},
                                {0x0006,0x01,0x01,1},
                                {0x0005,0x80,0x00,1},                  /* clear bit7                          */
                                {0x0005,(0x10|0x08),0x00,1},
                                {0x0005,0x01,0x01,1},                  /* APS_FSMCO: MAC on (bit0)            */
                                {0x0005,0x01,0x00,2},                  /* poll until bit0 clears (MAC ready)  */
                                {0x0020,0x08,0x08,1},                  /* enable                              */
                                {0x0074,0x20,0x20,1},                  /* PCI: bit5                           */
                                {0x0022,0x02,0x00,1},
                                {0x0062,(0x80|0x40|0x20),(0x80|0x40|0x20),1},
                                {0x0061,(0x80|0x40|0x20),0x00,1},
                                {0x007C,0x02,0x00,1},
                            };
    int nn=(int)(sizeof(seqp)/sizeof(seqp[0])), okall=1;
    for(int s=0;s<nn;s++){ u16 o=seqp[s].off;
        if(seqp[s].cmd==3){ rtw_delay_us(1000); continue; }
        if(seqp[s].cmd==2){ int got=0; for(int t=0;t<100000;t++){ if((rtw_r8(m,o)&seqp[s].mask)==seqp[s].val){got=1;break;} io_wait(); }
            if(!got){ okall=0; } continue; }
        u8 v=rtw_r8(m,o); v=(u8)((v & ~seqp[s].mask) | (seqp[s].val & seqp[s].mask)); rtw_w8(m,o,v);
        for(volatile int d=0;d<500;d++) io_wait();
    }
    return okall?0:1;
}
