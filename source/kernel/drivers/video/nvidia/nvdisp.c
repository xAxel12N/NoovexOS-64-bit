static void nvw32(u32 off,u32 v){ if(g_nv_bar0)*(volatile u32*)(u64)(g_nv_bar0+off)=v; }
static void nv_udelay(u32 us){ rtw_delay_us(us); }

/* ============================================================
   EDID via DDC/I2C over the GPU GPIO (Turing PMGR i2c ports)
   The GPIO/i2c port that carries DDC for a connector comes from
   the DCB entry; here we bit-bang the classic DDC lines.
   Turing PMGR i2c: NV_PMGR_I2C_(SCL/SDA) at 0x00E000 range.
   ============================================================ */
#define NV_PMGR_I2C_BASE 0x00E138
static u32 g_i2c_port_reg=NV_PMGR_I2C_BASE;
static void i2c_set(int scl,int sda){
    u32 v=nvr32(g_i2c_port_reg);
    v&=~0x2121u;                      /* clear drive-enable + value bits (port-layout dependent) */
    if(!scl)v|=0x0001u;               /* drive SCL low  */
    if(!sda)v|=0x0100u;               /* drive SDA low  */
    nvw32(g_i2c_port_reg,v); nv_udelay(5);
}
static int i2c_get_sda(void){ return (nvr32(g_i2c_port_reg)>>12)&1; }
static void i2c_start(void){ i2c_set(1,1); i2c_set(1,0); i2c_set(0,0); }
static void i2c_stop(void){ i2c_set(0,0); i2c_set(1,0); i2c_set(1,1); }
static int i2c_wbyte(u8 b){ for(int i=0;i<8;i++){ i2c_set(0,(b>>(7-i))&1); i2c_set(1,(b>>(7-i))&1); i2c_set(0,(b>>(7-i))&1); }
    i2c_set(0,1); i2c_set(1,1); int ack=!i2c_get_sda(); i2c_set(0,1); return ack; }
static u8 i2c_rbyte(int ack){ u8 b=0; i2c_set(0,1); for(int i=0;i<8;i++){ i2c_set(1,1); b=(b<<1)|i2c_get_sda(); i2c_set(0,1); }
    i2c_set(0,!ack); i2c_set(1,!ack); i2c_set(0,!ack); return b; }

static u8 g_edid[128]; static int g_edid_ok=0;
static int edid_read_ddc(void){
    i2c_start();
    if(!i2c_wbyte(0xA0)){ i2c_stop(); return 0; }   /* DDC addr write */
    i2c_wbyte(0x00);                                 /* offset 0 */
    i2c_start();
    if(!i2c_wbyte(0xA1)){ i2c_stop(); return 0; }   /* DDC addr read */
    for(int i=0;i<128;i++)g_edid[i]=i2c_rbyte(i<127);
    i2c_stop();
    if(!(g_edid[0]==0x00&&g_edid[1]==0xFF&&g_edid[2]==0xFF&&g_edid[3]==0xFF&&g_edid[4]==0xFF&&g_edid[5]==0xFF&&g_edid[6]==0xFF&&g_edid[7]==0x00))return 0;
    u8 sum=0; for(int i=0;i<128;i++)sum+=g_edid[i]; if(sum)return 0;
    g_edid_ok=1; return 1;
}

/* ---- parsed mode from EDID detailed timing descriptor ---- */
static nv_mode g_mode; static int g_mode_ok=0;
static char g_edid_name[16];
static int edid_parse(void){
    if(!g_edid_ok)return 0;
    int d=54;                                   /* first detailed timing descriptor */
    u32 pclk=(g_edid[d]|(g_edid[d+1]<<8))*10;    /* in 10 kHz -> kHz */
    if(pclk==0)return 0;
    u16 ha=g_edid[d+2]|((g_edid[d+4]&0xF0)<<4);
    u16 hb=g_edid[d+3]|((g_edid[d+4]&0x0F)<<8);
    u16 va=g_edid[d+5]|((g_edid[d+7]&0xF0)<<4);
    u16 vb=g_edid[d+6]|((g_edid[d+7]&0x0F)<<8);
    u16 hfp=g_edid[d+8]|((g_edid[d+11]&0xC0)<<2);
    u16 hsw=g_edid[d+9]|((g_edid[d+11]&0x30)<<4);
    u16 vfp=(g_edid[d+10]>>4)|((g_edid[d+11]&0x0C)<<2);
    u16 vsw=(g_edid[d+10]&0x0F)|((g_edid[d+11]&0x03)<<4);
    g_mode.pclk_khz=pclk; g_mode.hact=ha; g_mode.hbl=hb; g_mode.vact=va; g_mode.vbl=vb;
    g_mode.hfp=hfp; g_mode.hsync=hsw; g_mode.vfp=vfp; g_mode.vsync=vsw;
    g_mode.hpol=(g_edid[d+17]>>1)&1; g_mode.vpol=(g_edid[d+17]>>2)&1;
    /* monitor name from a 0xFC descriptor if present */
    g_edid_name[0]=0;
    for(int b=54;b<=108;b+=18){ if(g_edid[b]==0&&g_edid[b+1]==0&&g_edid[b+3]==0xFC){ int o=0; for(int i=5;i<18&&o<15;i++){ char c=g_edid[b+i]; if(c=='\n')break; g_edid_name[o++]=c; } g_edid_name[o]=0; } }
    g_mode_ok=1; return 1;
}

/* pick which DCB connector's i2c port to use (first non-VGA with a valid entry) */
static int nv_select_output(void){
    if(!g_nv_ncon)return -1;
    for(int i=0;i<g_nv_ncon;i++){ u8 t=g_nv_con_type[i]; if(t==6||t==8||t==2||t==3||t==0)return i; }
    return 0;
}

/* ============================================================
   disp907d core-channel modeset  ***UNVERIFIED SCAFFOLD***
   This mirrors the shape of Nouveau's dispnv50 core-channel
   push for Turing but the exact method numbers/sequence for
   your silicon must be confirmed on real hardware. It is NOT
   guaranteed to light a panel and is guarded off by default.
   ============================================================ */
#define NV_PDISP_CHANNEL_BASE 0x610000
static u32* g_disp_push=0; static u64 g_disp_push_phys=0; static int g_disp_pushn=0;
static u64 g_fb_phys=0; static u32 g_fb_w=0,g_fb_h=0,g_fb_pitch=0;

static void dpush(u32 mthd,u32 data){ if(!g_disp_push)return; g_disp_push[g_disp_pushn++]=(1u<<28)|(mthd&0x1FFF)|(1u<<18); g_disp_push[g_disp_pushn++]=data; }

static int nv_modeset_scaffold(void){
    if(!g_mode_ok)return -1;
    /* allocate a push buffer and a framebuffer in the BAR1 window (VRAM aperture) */
    g_disp_push=(u32*)dma_alloc(4096); if(!g_disp_push)return -1; g_disp_push_phys=(u64)(u32)(u64)g_disp_push; g_disp_pushn=0;
    g_fb_w=g_mode.hact; g_fb_h=g_mode.vact; g_fb_pitch=g_fb_w*4;
    /* --- the following method stream is the UNVERIFIED part --- */
    dpush(0x0200, 0x00000001);                 /* core channel: set owner/subdevice (placeholder) */
    dpush(0x0400, (g_mode.hact<<16)|g_mode.vact);   /* head raster size (placeholder method) */
    dpush(0x0404, g_mode.pclk_khz);            /* pixel clock (placeholder) */
    dpush(0x0408, (u32)(g_fb_phys>>8));        /* surface base >>8 (placeholder) */
    dpush(0x040C, g_fb_pitch);                 /* surface pitch (placeholder) */
    dpush(0x0480, 0x00000001);                 /* update/kickoff (placeholder) */
    /* real driver would now write the channel PUT pointer register and poll GET */
    return 0;
}

static int g_nv_modeset_enable=0;   /* keep the speculative modeset OFF unless explicitly turned on */

static int nv_display_probe(void){
    if(!nv_probe())return 0;
    nv_read_id();
    if(!g_nv_vbios_len)nv_load_vbios();
    if(!g_nv_dcb_off){ if(nv_find_dcb())nv_parse_dcb(); }
    int out=nv_select_output();
    g_edid_ok=0; g_mode_ok=0;
    if(out>=0){ if(edid_read_ddc())edid_parse(); }
    return 1;
}
