// SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause
/* Copyright(c) 2018-2019  Realtek Corporation
 */
/* RTL8822CE chip file for the NoovexOS rtw88 contract, ported from Linux rtw88 rtw8822c.c/rtw8822ce.c. */
#include "include/nvx/base.h"
#include "include/nvx/core/klog.h"
#include "include/nvx/drivers/net/wifi/rtw_chip.h"
#include "include/nvx/drivers/net/wifi/rtw_efuse.h"
#include "drivers/net/wifi/rtw88/chips/rtw8822c.h"
#include "drivers/net/wifi/rtw88/chips/rtw8822c_tables.h"

extern const unsigned char _binary_rtw8822c_fw_bin_start[];
extern const unsigned char _binary_rtw8822c_fw_bin_end[];
int rtw_h2c_cmd(u32 m, u32 w0, u32 w1);
void ui_pump(void);

#define BB_PATH_A  1
#define BB_PATH_B  2
#define BB_PATH_AB 3
#define RFMASK     0xFFFFFu
#define IQK_DONE_8822C 0xAA
#define DACK_SN    100
#define DACK_MSBK  15

static const u32 rf_base[2] = {0x3C00, 0x4C00};
static const u32 rf_sipi[2] = {0x1808, 0x4108};

static struct {
    u8 efuse[0xD2];
    int efuse_ok;
    u8 rfe, xtal, npath, cut, bw, ch, pri;
    u8 ant_tx, ant_rx;
    u16 dack_msbk[2][2][DACK_MSBK];
    u8 dack_dck[2][2][2];
    u32 dack_adck[2];
    int bt_iqk_timeout;
} s = { .npath = 2, .ant_tx = BB_PATH_AB, .ant_rx = BB_PATH_AB };

static u32 shift_of(u32 mask){ u32 sh = 0; if(!mask) return 0; while(!((mask >> sh) & 1u)) sh++; return sh; }
static void m32(u32 m, u32 a, u32 mask, u32 v){ rtw_bb_write_mask(m, a, mask, v); }
static u32 r32m(u32 m, u32 a, u32 mask){ return rtw_bb_read_mask(m, a, mask); }
static void set32(u32 m, u32 a, u32 b){ rtw_w32(m, a, rtw_r32(m, a) | b); }
static void clr32(u32 m, u32 a, u32 b){ rtw_w32(m, a, rtw_r32(m, a) & ~b); }
static void set8(u32 m, u32 a, u8 b){ rtw_w8(m, a, (u8)(rtw_r8(m, a) | b)); }
static void clr8(u32 m, u32 a, u8 b){ rtw_w8(m, a, (u8)(rtw_r8(m, a) & ~b)); }
static void set16(u32 m, u32 a, u16 b){ rtw_w16(m, a, (u16)(rtw_r16(m, a) | b)); }
static void m8(u32 m, u32 a, u8 mask, u8 v){ u32 sh = shift_of(mask); rtw_w8(m, a, (u8)((rtw_r8(m, a) & ~mask) | ((v << sh) & mask))); }
static void mdelay(u32 ms){ while(ms--) rtw_delay_us(1000); }
static int hw_ready(u32 m, u32 a, u32 mask, u32 target){
    for(int c = 0; c < 1000; c++){ if(r32m(m, a, mask) == target) return 1; rtw_delay_us(10); }
    return 0;
}

/* 8822C RF access (rtw_phy_read_rf + rtw_phy_write_rf_reg_mix): direct MMIO window at rf_base for every
   register except RF 0x00, which goes through the SIPI port. The contract's rtw_rf_read/write use the 8821C
   addressing, so the chip keeps its own. */
u32 rtw8822c_rf_read(u32 m, int path, u32 addr, u32 mask){
    if(path < 0 || path > 1) return 0xFFFFFFFFu;
    mask &= RFMASK;
    return (rtw_r32(m, rf_base[path] + ((addr & 0xFFu) << 2)) & mask) >> shift_of(mask);
}
void rtw8822c_rf_write(u32 m, int path, u32 addr, u32 mask, u32 data){
    if(path < 0 || path > 1) return;
    addr &= 0xFFu; mask &= RFMASK;
    if(addr != 0){ m32(m, rf_base[path] + (addr << 2), mask, data); rtw_delay_us(1); return; }
    if(mask != RFMASK){ u32 old = rtw8822c_rf_read(m, path, addr, RFMASK); data = (old & ~mask) | (data << shift_of(mask)); }
    rtw_w32(m, rf_sipi[path], ((addr << 20) | (data & RFMASK)) & 0x0FFFFFFFu);
    rtw_delay_us(13);
}
#define RFR(p,a,k)   rtw8822c_rf_read(m, p, a, k)
#define RFW(p,a,k,v) rtw8822c_rf_write(m, p, a, k, v)

static int cond_ok(u32 c){
    u32 cut = (c >> 24) & 0xFu, pkg = (c >> 12) & 0xFu, itf = (c >> 8) & 0xFu, rfe = c & 0xFFu;
    if(cut && cut != (u32)g_rtw_cut) return 0;
    if(pkg && pkg != (u32)g_rtw_pkg) return 0;
    if(itf && itf != 1u) return 0;
    return rfe == (u32)g_rtw_rfe;
}
int rtw8822c_load_rf(u32 m, int path, const unsigned int* t, int n){
    u32 pc = 0; int matched = 1, skipped = 0, wr = 0;
    for(int i = 0; i < n; i++){
        u32 a = t[i * 2], d = t[i * 2 + 1];
        if(a >> 31){ u32 br = (a >> 28) & 3u; if(br == 3){ matched = 1; skipped = 0; } else if(br == 2) matched = skipped ? 0 : 1; else pc = a; continue; }
        if((a >> 30) & 1u){ if(!skipped){ if(cond_ok(pc)){ matched = 1; skipped = 1; } else { matched = 0; skipped = 0; } } else matched = 0; continue; }
        if(!matched) continue;
        if(a == 0xFFEu){ mdelay(50); continue; }
        if(a == 0xFEu){ rtw_delay_us(100); continue; }
        RFW(path, a, RFMASK, d); rtw_delay_us(1); wr++;
    }
    return wr;
}

static u8 phy_efuse(u32 m, u16 addr){
    m32(m, 0x30, 0x3FF00, addr);
    clr32(m, 0x30, 1u << 31);
    for(int t = 0; t < 100; t++){ rtw_delay_us(1000); if(rtw_r32(m, 0x30) & (1u << 31)) return rtw_r8(m, 0x30); }
    return 0xFF;
}

static int pwr_poll(u32 m, u32 a, u8 mask, u8 v){
    for(int t = 0; t < 20000; t++){ if((rtw_r8(m, a) & mask) == (v & mask)) return 1; rtw_delay_us(50); }
    return 0;
}
static int pwr_seq(u32 m, const rtw8822c_pwr_cmd* c){
    for(; c->cmd != PWR_END; c++){
        if(c->cmd == PWR_WRITE){ u8 v = rtw_r8(m, c->off); v = (u8)((v & ~c->mask) | (c->val & c->mask)); rtw_w8(m, c->off, v); continue; }
        if(pwr_poll(m, c->off, c->mask, c->val)) continue;
        u8 v = rtw_r8(m, 0x04);
        rtw_w8(m, 0x04, (u8)(v | 0x08)); rtw_w8(m, 0x04, (u8)(v & ~0x08));
        if(!pwr_poll(m, c->off, c->mask, c->val)){ klog("rtw8822c: power sequence poll timeout"); return -1; }
    }
    return 0;
}
/* trans_* tables of rtw8822c.c, keeping only the PCIe and all-interface commands */
static const rtw8822c_pwr_cmd carddis_to_cardemu[] = {
    {0x002E, 0x04, 0x04, PWR_WRITE}, {0x002D, 0x01, 0, PWR_WRITE}, {0x007F, 0x80, 0, PWR_WRITE},
    {0x0005, 0x98, 0, PWR_WRITE}, {0, 0, 0, PWR_END}};
static const rtw8822c_pwr_cmd cardemu_to_act[] = {
    {0x0005, 0x1C, 0, PWR_WRITE}, {0x0075, 0x01, 0x01, PWR_WRITE}, {0x0006, 0x02, 0x02, PWR_POLL},
    {0x0075, 0x01, 0, PWR_WRITE}, {0x002E, 0x08, 0, PWR_WRITE}, {0x0006, 0x01, 0x01, PWR_WRITE},
    {0x0005, 0x18, 0, PWR_WRITE}, {0x1018, 0x04, 0x04, PWR_WRITE}, {0x0005, 0x01, 0x01, PWR_WRITE},
    {0x0005, 0x01, 0, PWR_POLL}, {0x0074, 0x20, 0x20, PWR_WRITE}, {0x0071, 0x10, 0, PWR_WRITE},
    {0x0062, 0xE0, 0xE0, PWR_WRITE}, {0x0061, 0xE0, 0, PWR_WRITE}, {0x001F, 0xC0, 0x80, PWR_WRITE},
    {0x00EF, 0xC0, 0x80, PWR_WRITE}, {0x1045, 0x10, 0x10, PWR_WRITE}, {0x0010, 0x04, 0x04, PWR_WRITE},
    {0x1064, 0x02, 0x02, PWR_WRITE}, {0, 0, 0, PWR_END}};
static const rtw8822c_pwr_cmd act_to_cardemu[] = {
    {0x0093, 0x08, 0, PWR_WRITE}, {0x001F, 0xFF, 0, PWR_WRITE}, {0x00EF, 0xFF, 0, PWR_WRITE},
    {0x1045, 0x10, 0, PWR_WRITE}, {0x0049, 0x02, 0, PWR_WRITE}, {0x0006, 0x01, 0x01, PWR_WRITE},
    {0x0002, 0x02, 0, PWR_WRITE}, {0x0005, 0x02, 0x02, PWR_WRITE}, {0x0005, 0x02, 0, PWR_POLL},
    {0, 0, 0, PWR_END}};
static const rtw8822c_pwr_cmd cardemu_to_carddis[] = {
    {0x0067, 0x20, 0, PWR_WRITE}, {0x0081, 0xC0, 0, PWR_WRITE}, {0x0090, 0x02, 0, PWR_WRITE},
    {0x0092, 0xFF, 0x20, PWR_WRITE}, {0x0093, 0xFF, 0x04, PWR_WRITE}, {0x0005, 0x04, 0x04, PWR_WRITE},
    {0, 0, 0, PWR_END}};

static void pre_system_cfg(u32 m){
    rtw_w8(m, 0x1C, 0);
    set32(m, 0x74, 0x100);
    set32(m, 0x64, 0x20000000u | 0x10000000u);
    clr32(m, 0x4C, 0x2000000u | 0x4000000u);
    set32(m, 0x40, 0x4);
    clr8(m, 0x02, 0x03);
    clr8(m, 0x1F, 0x07);
    clr32(m, 0xEC, 0x7000000u);
}
static int power_switch(u32 m, int on){
    if(rtw_r16(m, 0x80) == 0xC078){ u8 rpwm = (u8)((rtw_r8(m, 0x3D9) ^ 0x80) & 0x80); rtw_w8(m, 0x3D9, rpwm); }
    int cur = rtw_r8(m, 0x100) != 0xEA;
    if(cur == on) return 1;
    if(on){ if(pwr_seq(m, carddis_to_cardemu) || pwr_seq(m, cardemu_to_act)) return -1; }
    else { if(pwr_seq(m, act_to_cardemu) || pwr_seq(m, cardemu_to_carddis)) return -1; }
    return 0;
}
static void dbi_write8(u32 m, u16 addr, u8 data){
    u16 rem = addr & 0x3;
    u16 wa = (u16)((addr & 0xFFC) | ((1u << rem) << 12));
    rtw_w8(m, 0x3E8 + rem, data);
    rtw_w16(m, 0x3F0, wa);
    rtw_w8(m, 0x3F2, 0x01);
    for(int c = 0; c < 20; c++){ if(!rtw_r8(m, 0x3F2)) return; rtw_delay_us(10); }
    klog("rtw8822c: DBI write timeout");
}
static int rtw8822c_power_on(u32 m){
    pre_system_cfg(m);
    int r = power_switch(m, 1);
    if(r == 1){ power_switch(m, 0); pre_system_cfg(m); r = power_switch(m, 1); }
    if(r){ klog("rtw8822c: mac power on failed"); return -1; }
    set32(m, 0x1080, (1u << 16) | (1u << 8));
    set8(m, 0x03, 0xD8);
    rtw_w8(m, 0x1103, (u8)((rtw_r8(m, 0x1103) & 0xF0) | 0x0C));
    u32 t = rtw_r32(m, 0x80);
    if(t & (1u << 20)){ rtw_w32(m, 0x80, t & ~(1u << 20)); clr32(m, 0x40, 1u << 19); }
    u32 v = rtw_r32(m, 0xF0);
    s.cut = (u8)((v >> 12) & 0xF);
    s.npath = (v & (1u << 27)) ? 2 : 1;
    s.ant_tx = s.ant_rx = s.npath == 2 ? BB_PATH_AB : BB_PATH_A;
    if(s.cut >= 3) m32(m, 0x3FC, 1u << 26, 1);
    dbi_write8(m, 0x725, 0);
    if(s.efuse_ok && s.rfe == 5) m32(m, 0x1010, 0x30000000u, 1);
    return 0;
}
static void rtw8822c_power_off(u32 m){
    rtw_w16(m, 0xAA, 0x8000);
    if(power_switch(m, 0) < 0) klog("rtw8822c: power off sequence failed");
}

static int rtw8822c_efuse_parse(const u8* map, int len, rtw_efuse_info* o){
    if(len < 0x126) return -1;
    for(int i = 0; i < (int)sizeof(s.efuse); i++) s.efuse[i] = map[i];
    for(int i = 0; i < 6; i++) o->mac[i] = map[0x120 + i];
    s.rfe = o->rfe_option = map[0xCA];
    u8 x = map[0xB9];
    s.xtal = o->crystal_cap = x == 0xFF ? 0 : (x & 0x7F);
    o->thermal_meter = (u8)((map[0xD0] + map[0xD1]) >> 1);
    o->country_code[0] = map[0xCB]; o->country_code[1] = map[0xCC];
    for(int p = 0; p < 2; p++){
        const u8* e = map + 0x10 + p * 42;
        for(int ch = 1; ch <= 14; ch++) o->tx_pwr_idx_2g[p][ch - 1] = e[6 + rtw8822c_ch_group((u8)ch, 4)];
        for(int g = 0; g < 14; g++) o->tx_pwr_idx_5g[p][g] = e[18 + g];
    }
    g_rtw_rfe = s.rfe; g_rtw_pkg = 15;
    s.efuse_ok = 1;
    return 0;
}

static void rtw8822c_mac_init(u32 m);
static int rtw8822c_mac_init_op(u32 m){ rtw8822c_mac_init(m); return 0; }
static void rtw8822c_mac_init(u32 m){
    u8 v8 = rtw_r8(m, 0x420); v8 |= 0x80; rtw_w8(m, 0x420, v8);
    rtw_w8(m, 0x421, 0x1F);
    rtw_w16(m, 0x428, 0x0A | (0x10 << 8));
    rtw_w32(m, 0x514, 0x0A | (0x0E << 8) | (0x0A << 16) | (0x10u << 24));
    rtw_w16(m, 0x63C, 0x0A | (0x0A << 8));
    rtw_w16(m, 0x63E, 0x0E | (0x0E << 8));
    rtw_w32(m, 0x430, 0x01000000); rtw_w32(m, 0x434, 0x08070504); rtw_w32(m, 0x43C, 0x08070504);
    rtw_w32(m, 0x444, 0xFE01F010); rtw_w32(m, 0x448, 0x40000000);
    rtw_w32(m, 0x44C, 0x003FF010); rtw_w32(m, 0x450, 0x40000000);
    rtw_w32(m, 0x49C, 0x0600F010); rtw_w32(m, 0x4A0, 0x400003E0);
    rtw_w32(m, 0x4A4, 0x0600F015); rtw_w32(m, 0x4A8, 0x000000E0);
    rtw_w8(m, 0x455, 0x70);
    set8(m, 0x45E, 0x04);
    u16 pre = 0x1E0 | 0x800;
    rtw_w8(m, 0x4E5, (u8)(pre & 0xFF)); rtw_w8(m, 0x4E6, (u8)(pre >> 8));
    rtw_w32(m, 0x4C8, 0xFF | (0x08 << 8) | (0x3F << 16) | (0x3Fu << 24));
    rtw_w16(m, 0x4CE, 0x01 | (0x08 << 8));
    rtw_w8(m, 0x1448, 0x06); rtw_w8(m, 0x144A, 0x06); rtw_w8(m, 0x144C, 0x06); rtw_w8(m, 0x144E, 0x06);
    clr8(m, 0x426, 0x20);
    clr32(m, 0x440, 0x600000);
    rtw_w32(m, 0x500, 0x002FA226); rtw_w32(m, 0x504, 0x005EA328);
    rtw_w32(m, 0x508, 0x005EA42B); rtw_w32(m, 0x50C, 0x0000A44F);
    rtw_w8(m, 0x512, 0x1C);
    clr8(m, 0x521, 0x10);
    set8(m, 0x525, 0x07);
    clr32(m, 0x24, 0x300000);
    rtw_w8(m, 0x55C, 80); rtw_w8(m, 0x638, 80);
    set8(m, 0x577, 0x08 | 0x03);
    clr8(m, 0x5B4, 0x70);
    rtw_w16(m, 0x522, 0);
    rtw_w8(m, 0x51B, 0x09);
    rtw_w32(m, 0x544, 0x05 | (0x1B << 16));
    rtw_w16(m, 0x55E, 0x30 | (0x30 << 8));
    set8(m, 0x550, 0x08);
    rtw_w32(m, 0x540, 0x04 | (0x064 << 8));
    rtw_w8(m, 0x558, 0x04); rtw_w8(m, 0x551, 0x10); rtw_w8(m, 0x559, 0x02); rtw_w8(m, 0x55D, 0xFF);
    rtw_w32(m, 0x620, 0xFFFFFFFF); rtw_w32(m, 0x624, 0xFFFFFFFF);
    rtw_w8(m, 0x6DE, 0x84);
    rtw_w8(m, 0x640, 0x21); rtw_w8(m, 0x639, 0x6A);
    rtw_w16(m, 0x642, 0x40);
    rtw_w8(m, 0x652, 0xC8);
    rtw_w8(m, 0x66E, 0x05);
    rtw_w16(m, 0x6A0, 0xFFFF); rtw_w16(m, 0x6A2, 0xFFFF); rtw_w16(m, 0x6A4, 0xFFFF);
    rtw_w32(m, 0x608, 0xE400220E);
    rtw_w8(m, 0x60C, 12288 >> 9);
    rtw_w8(m, 0x606, 0x30); rtw_w8(m, 0x605, 0x30);
    set32(m, 0x1664, 0x200);
    rtw_w32(m, 0x7D8, 0xB0810041);
    rtw_w8(m, 0x7D4, 0x98);
    u16 v16 = (u16)(rtw_r16(m, 0x1612) & 0xF00F);
    v16 |= ((1u << 26) | (1u << 24) | (1u << 22) | (1u << 20)) >> 16;
    rtw_w16(m, 0x1612, v16);
    rtw_w16(m, 0x1610, (1u << 13) | 0x1000 | 0x800 | 0x400 | 0x200 | 0x100 | 0x80);
    rtw_w32(m, 0x1614, 0xFFFFFFFF);
    v16 = rtw_r16(m, 0x1610);
    v16 &= (u16)~(0x20 | 0x40 | 0x10);
    v16 = (u16)((v16 & ~0x7) | 0x7);
    rtw_w16(m, 0x1610, v16);
    set8(m, 0x718, 0x40);
    rtw_w32(m, 0x304, 0x33330000);
}

static void header_file_init(u32 m, int pre){
    set32(m, 0x180C, 0x3); set32(m, 0x180C, 1u << 28);
    set32(m, 0x410C, 0x3); set32(m, 0x410C, 1u << 28);
    if(pre) clr32(m, 0x1C3C, 0x3); else set32(m, 0x1C3C, 0x3);
}
static void bb_reset(u32 m){ set16(m, 0x02, 0x1); rtw_w16(m, 0x02, (u16)(rtw_r16(m, 0x02) & ~1u)); set16(m, 0x02, 0x1); }
static void toggle_igi(u32 m){
    u32 igi = r32m(m, 0x1D70, 0x7F);
    m32(m, 0x1D70, 0x7F, igi - 2); m32(m, 0x1D70, 0x7F00, igi - 2);
    m32(m, 0x1D70, 0x7F, igi); m32(m, 0x1D70, 0x7F00, igi);
}
static void config_trx_mode(u32 m, u8 tx, u8 rx){
    m32(m, 0x1800, RFMASK, ((tx | rx) & BB_PATH_A) ? 0x33312 : 0x11111);
    m32(m, 0x4100, RFMASK, ((tx | rx) & BB_PATH_B) ? 0x33312 : 0x11111);
    if(rx == BB_PATH_A || rx == BB_PATH_B){ m32(m, 0x1A2C, 0x00060000, 0); m32(m, 0x1A2C, 0x00600000, 0); }
    else if(rx == BB_PATH_AB){ m32(m, 0x1A2C, 0x00600000, 1); m32(m, 0x1A2C, 0x00060000, 1); }
    if(rx == BB_PATH_A) m32(m, 0x1A04, 0x0F000000, 0); else if(rx == BB_PATH_B) m32(m, 0x1A04, 0x0F000000, 5); else if(rx == BB_PATH_AB) m32(m, 0x1A04, 0x0F000000, 1);
    u32 ab = rx == BB_PATH_AB;
    if(rx == BB_PATH_A || rx == BB_PATH_B || ab){
        m32(m, 0x1D30, 0x300, ab); m32(m, 0x1D30, 0x600000, ab);
        m32(m, 0xC44, 1u << 17, ab); m32(m, 0xC54, 1u << 20, ab); m32(m, 0xC38, 1u << 24, ab);
    }
    m32(m, 0x824, 0x0F000000, rx); m32(m, 0x824, 0x000F0000, rx);
    m32(m, 0x1A04, 0xF0000000, 0x8);
    bb_reset(m);
    if(tx == BB_PATH_A){ m32(m, 0x820, 0xFF, 0x11); m32(m, 0x1E2C, 0xFF, 0); }
    else if(tx == BB_PATH_B){ m32(m, 0x820, 0xFF, 0x12); m32(m, 0x1E2C, 0xFF, 0); }
    else { m32(m, 0x820, 0xFF, 0x31); m32(m, 0x1E2C, 0xFFFF, 0x0400); }
    bb_reset(m);
    bb_reset(m);
    toggle_igi(m);
}

static void dack_backup_reg(u32 m, u32* bk, u32* bkrf){
    static const u32 addrs[16] = {0x180C, 0x1810, 0x410C, 0x4110, 0x1C3C, 0x1C24, 0x1D70, 0x9B4,
                                  0x1A00, 0x1A14, 0x1D58, 0x1C38, 0x1E24, 0x1E28, 0x1860, 0x4160};
    for(int i = 0; i < 16; i++) bk[i] = rtw_r32(m, addrs[i]);
    for(int p = 0; p < 2; p++) bkrf[0] = RFR(p, 0x8F, RFMASK);
}
static void dack_restore_reg(u32 m, const u32* bk, const u32* bkrf){
    static const u32 addrs[16] = {0x180C, 0x1810, 0x410C, 0x4110, 0x1C3C, 0x1C24, 0x1D70, 0x9B4,
                                  0x1A00, 0x1A14, 0x1D58, 0x1C38, 0x1E24, 0x1E28, 0x1860, 0x4160};
    for(int i = 0; i < 16; i++) rtw_w32(m, addrs[i], bk[i]);
    for(int p = 0; p < 2; p++) RFW(p, 0x8F, RFMASK, bkrf[0]);
}
static void minmax(u32 v, u32* mn, u32* mx){
    if(v >= 0x200){
        if(*mn >= 0x200){ if(*mn > v) *mn = v; } else *mn = v;
        if(*mx >= 0x200){ if(*mx < v) *mx = v; }
    } else {
        if(*mn < 0x200){ if(*mn > v) *mn = v; }
        if(*mx >= 0x200) *mx = v; else if(*mx < v) *mx = v;
    }
}
static void iq_sort2(u32* a, u32* b){
    u32 t;
    if((*a >= 0x200 && *b >= 0x200) || (*a < 0x200 && *b < 0x200)){ if(*a > *b){ t = *a; *a = *b; *b = t; } }
    else if(*a < 0x200 && *b >= 0x200){ t = *a; *a = *b; *b = t; }
}
static void iq_offset(const u32* vec, u32* val){
    u32 p = 0, mm = 0, t;
    for(int i = 10; i < DACK_SN - 10; i++){ if(vec[i] > 0x200) mm = (0x400 - vec[i]) + mm; else p = vec[i] + p; }
    if(p > mm){ t = (p - mm) / (DACK_SN - 20); }
    else { t = (mm - p) / (DACK_SN - 20); if(t) t = 0x400 - t; }
    *val = t;
}
static int iq_check(u32 v){ return !((v >= 0x200 && (0x400 - v) > 0x64) || (v < 0x200 && v > 0x64)); }
static void dac_rf_mode(u32 m, u32* iout, u32* qout){
    static u32 iv[DACK_SN], qv[DACK_SN];
    u32 t; int i = 0, cnt = 0;
    while(i < DACK_SN && cnt < 10000){
        cnt++;
        t = r32m(m, 0x2DBC, 0x3FFFFF);
        iv[i] = (t & 0x3FF000) >> 12; qv[i] = t & 0x3FF;
        if(iq_check(iv[i]) && iq_check(qv[i])) i++;
    }
    cnt = 0;
    do {
        u32 imn = iv[0], imx = iv[0], qmn = qv[0], qmx = qv[0], id, qd;
        for(i = 0; i < DACK_SN; i++){ minmax(iv[i], &imn, &imx); minmax(qv[i], &qmn, &qmx); }
        id = ((imx < 0x200 && imn < 0x200) || (imx >= 0x200 && imn >= 0x200)) ? imx - imn : imx + (0x400 - imn);
        qd = ((qmx < 0x200 && qmn < 0x200) || (qmx >= 0x200 && qmn >= 0x200)) ? qmx - qmn : qmx + (0x400 - qmn);
        for(int a = 0; a < DACK_SN - 1; a++) for(int b = 0; b < DACK_SN - 1 - a; b++){ iq_sort2(&iv[b], &iv[b + 1]); iq_sort2(&qv[b], &qv[b + 1]); }
        if(id > 5 || qd > 5){
            t = r32m(m, 0x2DBC, 0x3FFFFF); iv[0] = (t & 0x3FF000) >> 12; qv[0] = t & 0x3FF;
            t = r32m(m, 0x2DBC, 0x3FFFFF); iv[DACK_SN - 1] = (t & 0x3FF000) >> 12; qv[DACK_SN - 1] = t & 0x3FF;
        } else break;
    } while(cnt++ < 100);
    iq_offset(iv, iout); iq_offset(qv, qout);
}
static u32 dack_waddr(int p){ return p ? 0x4100 : 0x1800; }
static u32 dack_raddr(int p){ return p ? 0x4500 : 0x2800; }
static void dac_bb_setting(u32 m){
    m32(m, 0x1D58, 0xFF8, 0x1FF); m32(m, 0x1A00, 0x3, 0x2); m32(m, 0x1A14, 0x300, 0x3);
    rtw_w32(m, 0x1D70, 0x7E7E7E7E); m32(m, 0x180C, 0x3, 0); m32(m, 0x410C, 0x3, 0);
    rtw_w32(m, 0x1B00, 0x8); rtw_w8(m, 0x1BCC, 0x3F); rtw_w32(m, 0x1B00, 0xA); rtw_w8(m, 0x1BCC, 0x3F);
    m32(m, 0x1E24, 1u << 31, 0); m32(m, 0x1E28, 0xF, 0x3);
}
static void dac_cal_adc(u32 m, int p, u32* adc_ic, u32* adc_qc){
    u32 ic = 0, qc = 0, temp, b = dack_waddr(p), sel = p ? 0x80000 : 0xA0000;
    m32(m, b + 0x30, 1u << 30, 0);
    if(p) rtw_w32(m, b + 0x30, 0x30DB8041);
    rtw_w32(m, b + 0x60, 0xF0040FF0); rtw_w32(m, b + 0x0C, 0xDFF00220);
    rtw_w32(m, b + 0x10, 0x02DD08C4); rtw_w32(m, b + 0x0C, 0x10000260);
    RFW(0, 0x0, RFMASK, 0x10000); RFW(1, 0x0, RFMASK, 0x10000);
    for(int i = 0; i < 10; i++){
        rtw_w32(m, 0x1C3C, sel + 0x8003); rtw_w32(m, 0x1C24, 0x00010002);
        dac_rf_mode(m, &ic, &qc);
        if(ic){ ic = 0x400 - ic; *adc_ic = ic; }
        if(qc){ qc = 0x400 - qc; *adc_qc = qc; }
        temp = (ic & 0x3FF) | ((qc & 0x3FF) << 10);
        rtw_w32(m, b + 0x68, temp); s.dack_adck[p] = temp;
        rtw_w32(m, 0x1C3C, sel + 0x8103);
        dac_rf_mode(m, &ic, &qc);
        if(ic >= 0x200) ic = 0x400 - ic;
        if(qc >= 0x200) qc = 0x400 - qc;
        if(ic < 5 && qc < 5) break;
    }
    rtw_w32(m, 0x1C3C, 0x3); rtw_w32(m, b + 0x0C, 0x10000260); rtw_w32(m, b + 0x10, 0x02D508C4);
    RFW(p, 0x8F, 1u << 13, 1);
}
static void dac_step1(u32 m, int p){
    u32 b = dack_waddr(p), r = dack_raddr(p);
    rtw_w32(m, b + 0x68, s.dack_adck[p]); rtw_w32(m, b + 0x0C, 0xDFF00220);
    if(!p){ rtw_w32(m, b + 0x60, 0xF0040FF0); rtw_w32(m, 0x1C38, 0xFFFFFFFF); }
    rtw_w32(m, b + 0x10, 0x02D508C5); rtw_w32(m, 0x9B4, 0xDB66DB00);
    rtw_w32(m, b + 0xB0, 0x0A11FB88); rtw_w32(m, b + 0xBC, 0x0008FF81); rtw_w32(m, b + 0xC0, 0x0003D208);
    rtw_w32(m, b + 0xCC, 0x0A11FB88); rtw_w32(m, b + 0xD8, 0x0008FF81); rtw_w32(m, b + 0xDC, 0x0003D208);
    rtw_w32(m, b + 0xB8, 0x60000000); mdelay(2);
    rtw_w32(m, b + 0xBC, 0x000AFF8D); mdelay(2);
    rtw_w32(m, b + 0xB0, 0x0A11FB89); rtw_w32(m, b + 0xCC, 0x0A11FB89); mdelay(1);
    rtw_w32(m, b + 0xB8, 0x62000000); rtw_w32(m, b + 0xD4, 0x62000000); mdelay(20);
    if(!hw_ready(m, r + 0x08, 0x7FFF80, 0xFFFF) || !hw_ready(m, r + 0x34, 0x7FFF80, 0xFFFF)) klog("rtw8822c: DACK not ready");
    rtw_w32(m, b + 0xB8, 0x02000000); mdelay(1);
    rtw_w32(m, b + 0xBC, 0x0008FF87); rtw_w32(m, 0x9B4, 0xDB6DB600);
    rtw_w32(m, b + 0x10, 0x02D508C5); rtw_w32(m, b + 0xBC, 0x0008FF87); rtw_w32(m, b + 0x60, 0xF0000000);
}
static void dac_step2(u32 m, int p, u32* ic_out, u32* qc_out){
    u32 b = dack_waddr(p), ic, qc;
    m32(m, b + 0xBC, 0xF0000000, 0); m32(m, b + 0xC0, 0xF, 0x8);
    m32(m, b + 0xD8, 0xF0000000, 0); m32(m, b + 0xDC, 0xF, 0x8);
    rtw_w32(m, 0x1B00, 0x8); rtw_w8(m, 0x1BCC, 0x3F);
    rtw_w32(m, b + 0x0C, 0xDFF00220); rtw_w32(m, b + 0x10, 0x02D508C5); rtw_w32(m, 0x1C3C, 0x00088103);
    dac_rf_mode(m, &ic, &qc);
    if(ic) ic = 0x400 - ic;
    if(qc) qc = 0x400 - qc;
    if(ic < 0x300) ic = ic * 2 * 6 / 5 + 0x80; else ic = 0x7F - (0x400 - ic) * 2 * 6 / 5;
    if(qc < 0x300) qc = qc * 2 * 6 / 5 + 0x80; else qc = 0x7F - (0x400 - qc) * 2 * 6 / 5;
    *ic_out = ic; *qc_out = qc;
}
static void dac_step3(u32 m, int p, u32 adc_ic, u32 adc_qc, u32* ic_in, u32* qc_in, u32* i_out, u32* q_out){
    u32 b = dack_waddr(p), r = dack_raddr(p), ic = *ic_in, qc = *qc_in, temp;
    rtw_w32(m, b + 0x0C, 0xDFF00220); rtw_w32(m, b + 0x10, 0x02D508C5); rtw_w32(m, 0x9B4, 0xDB66DB00);
    rtw_w32(m, b + 0xB0, 0x0A11FB88); rtw_w32(m, b + 0xBC, 0xC008FF81); rtw_w32(m, b + 0xC0, 0x0003D208);
    m32(m, b + 0xBC, 0xF0000000, ic & 0xF); m32(m, b + 0xC0, 0xF, (ic & 0xF0) >> 4);
    rtw_w32(m, b + 0xCC, 0x0A11FB88); rtw_w32(m, b + 0xD8, 0xE008FF81); rtw_w32(m, b + 0xDC, 0x0003D208);
    m32(m, b + 0xD8, 0xF0000000, qc & 0xF); m32(m, b + 0xDC, 0xF, (qc & 0xF0) >> 4);
    rtw_w32(m, b + 0xB8, 0x60000000); mdelay(2);
    m32(m, b + 0xBC, 0xE, 0x6); mdelay(2);
    rtw_w32(m, b + 0xB0, 0x0A11FB89); rtw_w32(m, b + 0xCC, 0x0A11FB89); mdelay(1);
    rtw_w32(m, b + 0xB8, 0x62000000); rtw_w32(m, b + 0xD4, 0x62000000); mdelay(20);
    if(!hw_ready(m, r + 0x24, 0x07F80000, ic) || !hw_ready(m, r + 0x50, 0x07F80000, qc)) klog("rtw8822c: DACK IQ vector not written");
    rtw_w32(m, b + 0xB8, 0x02000000); mdelay(1);
    m32(m, b + 0xBC, 0xE, 0x3); rtw_w32(m, 0x9B4, 0xDB6DB600);
    temp = ((adc_ic + 0x10) & 0x3FF) | (((adc_qc + 0x10) & 0x3FF) << 10);
    rtw_w32(m, b + 0x68, temp); rtw_w32(m, b + 0x10, 0x02D508C5); rtw_w32(m, b + 0x60, 0xF0000000);
    dac_rf_mode(m, &ic, &qc);
    ic = ic >= 0x10 ? ic - 0x10 : 0x400 - (0x10 - ic);
    qc = qc >= 0x10 ? qc - 0x10 : 0x400 - (0x10 - qc);
    *i_out = ic; *q_out = qc;
    if(ic >= 0x200) ic = 0x400 - ic;
    if(qc >= 0x200) qc = 0x400 - qc;
    *ic_in = ic; *qc_in = qc;
}
static void dac_step4(u32 m, int p){
    u32 b = dack_waddr(p);
    rtw_w32(m, b + 0x68, 0); rtw_w32(m, b + 0x10, 0x02D508C4);
    m32(m, b + 0xBC, 0x1, 0); m32(m, b + 0x30, 1u << 30, 1);
}
static void dac_backup_vec(u32 m, int p, int vec, u32 w, u32 r){
    for(int i = 0; i < DACK_MSBK; i++){ m32(m, w, 0xF0000000, (u32)i); s.dack_msbk[p][vec][i] = (u16)r32m(m, r, 0x7FC0000); }
}
static void dac_backup(u32 m){
    u32 t0 = rtw_r32(m, 0x1860), t1 = rtw_r32(m, 0x4160), t2 = rtw_r32(m, 0x9B4);
    rtw_w32(m, 0x9B4, 0xDB66DB00);
    for(int p = 0; p < 2; p++){
        clr32(m, p ? 0x4130 : 0x1830, 1u << 30);
        m32(m, p ? 0x4160 : 0x1860, 0xFC000000, 0x3C);
        dac_backup_vec(m, p, 0, dack_waddr(p) + 0xB0, dack_raddr(p) + 0x10);
        dac_backup_vec(m, p, 1, dack_waddr(p) + 0xB0 + 0x1C, dack_raddr(p) + 0x10 + 0x2C);
    }
    s.dack_dck[0][0][0] = (u8)r32m(m, 0x18BC, 0xF0000000);
    s.dack_dck[0][0][1] = (u8)r32m(m, 0x18C0, 0xF);
    s.dack_dck[0][1][0] = (u8)r32m(m, 0x18D8, 0xF0000000);
    s.dack_dck[0][1][1] = (u8)r32m(m, 0x18DC, 0xF);
    s.dack_dck[1][0][0] = (u8)r32m(m, 0x41BC, 0xF0000000);
    s.dack_dck[1][1][0] = (u8)r32m(m, 0x41C0, 0xF);
    s.dack_dck[1][0][1] = (u8)r32m(m, 0x41D8, 0xF0000000);
    s.dack_dck[1][1][1] = (u8)r32m(m, 0x41DC, 0xF);
    set32(m, 0x1830, 1u << 30); set32(m, 0x4130, 1u << 30);
    rtw_w32(m, 0x1860, t0); rtw_w32(m, 0x4160, t1); rtw_w32(m, 0x9B4, t2);
}
static void dac_restore_dck(u32 m){
    static const u32 r0[2][2] = {{0x18BC, 0x18D8}, {0x41BC, 0x41D8}}, r1[2][2] = {{0x18C0, 0x18DC}, {0x41C0, 0x41DC}};
    for(int p = 0; p < 2; p++) for(int q = 0; q < 2; q++){
        set32(m, r0[p][q], 1u << 19);
        m32(m, r0[p][q], 0xF0000000, s.dack_dck[p][q][0]);
        m32(m, r1[p][q], 0xF, s.dack_dck[p][q][1]);
    }
}
static void dac_restore_prepare(u32 m){
    rtw_w32(m, 0x9B4, 0xDB66DB00);
    m32(m, 0x18B0, 1u << 27, 0); m32(m, 0x18CC, 1u << 27, 0); m32(m, 0x41B0, 1u << 27, 0); m32(m, 0x41CC, 1u << 27, 0);
    m32(m, 0x1830, 1u << 30, 0); m32(m, 0x1860, 0xFC000000, 0x3C); m32(m, 0x18B4, 1, 1); m32(m, 0x18D0, 1, 1);
    m32(m, 0x4130, 1u << 30, 0); m32(m, 0x4160, 0xFC000000, 0x3C); m32(m, 0x41B4, 1, 1); m32(m, 0x41D0, 1, 1);
    m32(m, 0x18B0, 0xF00, 0); m32(m, 0x18C0, 1u << 14, 0); m32(m, 0x18CC, 0xF00, 0); m32(m, 0x18DC, 1u << 14, 0);
    m32(m, 0x18B0, 1, 0); m32(m, 0x18CC, 1, 0); m32(m, 0x18B0, 1, 1); m32(m, 0x18CC, 1, 1);
    dac_restore_dck(m);
    m32(m, 0x18C0, 0x38000, 7); m32(m, 0x18DC, 0x38000, 7); m32(m, 0x41C0, 0x38000, 7); m32(m, 0x41DC, 0x38000, 7);
    m32(m, 0x18B8, 0x6000000, 1); m32(m, 0x18D4, 0x6000000, 1);
    m32(m, 0x41B0, 0xF00, 0); m32(m, 0x41C0, 1u << 14, 0); m32(m, 0x41CC, 0xF00, 0); m32(m, 0x41DC, 1u << 14, 0);
    m32(m, 0x41B0, 1, 0); m32(m, 0x41CC, 1, 0); m32(m, 0x41B0, 1, 1); m32(m, 0x41CC, 1, 1);
    m32(m, 0x41B8, 0x6000000, 1); m32(m, 0x41D4, 0x6000000, 1);
}
static int dac_restore_wait(u32 m, u32 target, u32 toggle){
    u32 cnt = 0;
    do { m32(m, toggle, 0x6000000, 0); m32(m, toggle, 0x6000000, 2); if(r32m(m, target, 0xF) == 0x6) return 1; } while(cnt++ < 100);
    return 0;
}
static int dac_restore_path(u32 m, int p){
    u32 wi = dack_waddr(p) + 0xB0, ri = dack_raddr(p) + 0x08, wq = wi + 0x1C, rq = ri + 0x2C;
    if(!dac_restore_wait(m, ri, wi + 0x8)) return 0;
    for(int i = 0; i < DACK_MSBK; i++){ m32(m, wi + 4, 1u << 2, 0); m32(m, wi + 4, 0xFF8, s.dack_msbk[p][0][i]); m32(m, wi, 0xF0000000, (u32)i); m32(m, wi + 4, 1u << 2, 1); }
    m32(m, wi + 4, 1u << 2, 0);
    if(!dac_restore_wait(m, rq, wq + 0x8)) return 0;
    for(int i = 0; i < DACK_MSBK; i++){ m32(m, wq + 4, 1u << 2, 0); m32(m, wq + 4, 0xFF8, s.dack_msbk[p][1][i]); m32(m, wq, 0xF0000000, (u32)i); m32(m, wq + 4, 1u << 2, 1); }
    m32(m, wq + 4, 1u << 2, 0);
    m32(m, wi + 8, 0x6000000, 0); m32(m, wq + 8, 0x6000000, 0); m32(m, wi + 4, 1, 0); m32(m, wq + 4, 1, 0);
    return 1;
}
static int dac_restore(u32 m){
    if(!s.dack_msbk[0][0][0] && !s.dack_msbk[0][1][0] && !s.dack_msbk[1][0][0] && !s.dack_msbk[1][1][0]) return 0;
    u32 t0 = rtw_r32(m, 0x1860), t1 = rtw_r32(m, 0x4160), t2 = rtw_r32(m, 0x9B4);
    dac_restore_prepare(m);
    if(!hw_ready(m, 0x2808, 0x7FFF80, 0xFFFF) || !hw_ready(m, 0x2834, 0x7FFF80, 0xFFFF) ||
       !hw_ready(m, 0x4508, 0x7FFF80, 0xFFFF) || !hw_ready(m, 0x4534, 0x7FFF80, 0xFFFF)) return 0;
    if(!dac_restore_path(m, 0) || !dac_restore_path(m, 1)){ klog("rtw8822c: failed to restore DACK vectors"); return 0; }
    m32(m, 0x1830, 1u << 30, 1); m32(m, 0x4130, 1u << 30, 1);
    rtw_w32(m, 0x1860, t0); rtw_w32(m, 0x4160, t1);
    m32(m, 0x18B0, 1u << 27, 1); m32(m, 0x18CC, 1u << 27, 1); m32(m, 0x41B0, 1u << 27, 1); m32(m, 0x41CC, 1u << 27, 1);
    rtw_w32(m, 0x9B4, t2);
    return 1;
}
void rtw8822c_dac_cal(u32 m){
    u32 bk[16], bkrf[2] = {0, 0}, ic = 0, qc = 0, iv, qv, adc_ic, adc_qc;
    if(dac_restore(m)){ klog("rtw8822c: DACK restored from backup"); return; }
    dack_backup_reg(m, bk, bkrf);
    dac_bb_setting(m);
    for(int p = 0; p < 2; p++){
        adc_ic = adc_qc = 0;
        dac_cal_adc(m, p, &adc_ic, &adc_qc);
        for(int i = 0; i < 10; i++){
            dac_step1(m, p);
            dac_step2(m, p, &ic, &qc);
            dac_step3(m, p, adc_ic, adc_qc, &ic, &qc, &iv, &qv);
            if(ic < 5 && qc < 5) break;
        }
        dac_step4(m, p);
    }
    rtw_w32(m, 0x1B00, 0x8); m32(m, 0x4130, 1u << 30, 1); rtw_w8(m, 0x1BCC, 0);
    rtw_w32(m, 0x1B00, 0xA); rtw_w8(m, 0x1BCC, 0);
    dack_restore_reg(m, bk, bkrf);
    dac_backup(m);
    klog("rtw8822c: DACK done");
}

static void rf_x2_check(u32 m){
    mdelay(1);
    if(RFR(0, 0xB8, 1u << 15) == 1){
        RFW(0, 0xB8, RFMASK, 0xC4440); RFW(0, 0xBA, RFMASK, 0x6840D); RFW(0, 0xB8, RFMASK, 0x80440);
        mdelay(1);
    }
}
static void power_trim(u32 m){
    static const u16 e2g[3] = {0x1D4, 0x1EE, 0x1D2};
    static const u16 e5g[2][5] = {{0x1EC, 0x1E8, 0x1E4, 0x1E0, 0x1DC}, {0x1EB, 0x1E7, 0x1E3, 0x1DF, 0x1DB}};
    static const u8 seq_idx[15] = {0, 1, 2, 2, 3, 4, 5, 6, 7, 3, 4, 5, 6, 7, 7};
    u8 g[2][8] = {{0}}; int set = 0;
    for(int i = 0; i < 3; i++){ u8 v = phy_efuse(m, e2g[i]); if(v == 0xFF) continue; set = 1; g[0][i] = v & 0xF; g[1][i] = (v >> 4) & 0xF; }
    for(int i = 0; i < 5; i++) for(int p = 0; p < s.npath; p++){ u8 v = phy_efuse(m, e5g[p][i]); if(v == 0xFF) continue; set = 1; g[p][i + 3] = v & 0x1F; }
    if(set) for(int p = 0; p < s.npath; p++){
        RFW(p, 0xEE, 1u << 19, 1);
        for(int q = 0; q < 15; q++){ RFW(p, 0x33, RFMASK, (u32)q); RFW(p, 0x3F, RFMASK, g[p][seq_idx[q]]); }
        RFW(p, 0xEE, 1u << 19, 0);
    }
    m32(m, 0xA70, 0x3FF, 0x3FF);
}
static void thermal_trim(u32 m){
    static const u16 e[2] = {0x1EF, 0x1B0};
    for(int p = 0; p < s.npath; p++){
        u8 v = phy_efuse(m, e[p]);
        if(v == 0xFF) return;
        RFW(p, 0x43, 0xF0000, (u32)(((v >> 1) & 0x7) | ((v & 1) << 3)));
    }
}
static void pa_bias(u32 m){
    static const u16 e2g[2] = {0x1D6, 0x1D5}, e5g[2] = {0x1D8, 0x1D7};
    for(int p = 0; p < s.npath; p++){ u8 v = phy_efuse(m, e2g[p]); if(v == 0xFF) return; RFW(p, 0x60, 0xF000, v & 0xF); }
    for(int p = 0; p < s.npath; p++){ u8 v = phy_efuse(m, e5g[p]); RFW(p, 0x60, 0xF0000, v & 0xF); }
}

static void rtw8822c_phy_init(u32 m){
    u32 v = rtw_r32(m, 0xF0);
    s.cut = (u8)((v >> 12) & 0xF);
    s.npath = (v & (1u << 27)) ? 2 : 1;
    s.ant_tx = s.ant_rx = s.npath == 2 ? BB_PATH_AB : BB_PATH_A;
    g_rtw_cut = s.cut ? s.cut : 15; g_rtw_pkg = 15; g_rtw_rfe = s.rfe; g_rtw_cond_set = 1;
    set8(m, 0x02, 0x03);
    set8(m, 0x1F, 0x07);
    set32(m, 0xEC, 0x7000000u);
    m32(m, 0xA70, 0x3FF, 0x3FF);
    header_file_init(m, 1);
    rtw_apply_cond(m, rtw8822c_bb, rtw8822c_bb_n, 4, "8822c bb", 0);
    rtw_apply_cond(m, rtw8822c_agc, rtw8822c_agc_n, 4, "8822c agc", 0);
    m32(m, 0x1E24, 1u << 17, 1); m32(m, 0x1CD0, 1u << 28, 1); m32(m, 0x1CD0, 1u << 29, 1);
    m32(m, 0x1CD0, 1u << 30, 1); m32(m, 0x1CD0, 1u << 31, 0);
    rtw_apply_cond(m, rtw8822c_array_mp_cal_init, rtw8822c_array_mp_cal_init_n, 4, "8822c rfk init", 0);
    if(s.npath > 1) rtw8822c_load_rf(m, 1, rtw8822c_rf_b, rtw8822c_rf_b_n);
    rtw8822c_load_rf(m, 0, rtw8822c_rf_a, rtw8822c_rf_a_n);
    m32(m, 0x1040, 0xFFFC00, (u32)s.xtal | ((u32)s.xtal << 7));
    header_file_init(m, 0);
    config_trx_mode(m, s.ant_tx, s.ant_rx);
    m32(m, 0x84C, 0xFF0000, 0x7F + 0x80); m32(m, 0x84C, 0xFF000000, 0x7F + 0x80);
    clr32(m, 0x520, 0x8000);
    set32(m, 0x524, 0x800);
    clr32(m, 0x844, 0x60000000);
    rtw8822c_dac_cal(m);
    rf_x2_check(m);
    thermal_trim(m);
    power_trim(m);
    pa_bias(m);
}

void rtw8822c_chan_params(u8 ch, u8 bw, u8* center, u8* pri_idx){
    *center = ch; *pri_idx = 0;
    if(bw == RTW_BW_40){
        int up = ch <= 14 ? ch > 7 : ((ch >= 149 ? (ch - 149) : (ch - 36)) / 4) & 1;
        *center = (u8)(up ? ch - 2 : ch + 2); *pri_idx = up ? 1 : 2;
    } else if(bw == RTW_BW_80 && ch > 14){
        int k = ((ch >= 149 ? (ch - 149) : (ch - 36)) / 4) & 3;
        static const signed char off[4] = {6, 2, -2, -6};
        static const u8 idx[4] = {4, 2, 1, 3};
        *center = (u8)(ch + off[k]); *pri_idx = idx[k];
    }
}
static int band5_1(u8 c){ return c >= 36 && c <= 48; }
static int band5_2(u8 c){ return c >= 52 && c <= 64; }
static int band5_3(u8 c){ return c >= 100 && c <= 144; }
static int band5_4(u8 c){ return c >= 149 && c <= 177; }
static int band5(u8 c){ return band5_1(c) || band5_2(c) || band5_3(c) || band5_4(c); }

static void set_channel_bb(u32 m, u8 ch, u8 bw, u8 pri){
    if(ch <= 14){
        clr32(m, 0x1A14, 0x300); set32(m, 0x1A9C, 1u << 20); clr32(m, 0x454, 0x80); clr32(m, 0x1A80, 1u << 18);
        m32(m, 0x1C80, 0x3F000000, 0xF);
        if(bw == RTW_BW_20){ m32(m, 0x18AC, 0xF000, 5); m32(m, 0x41AC, 0xF000, 5); m32(m, 0x18AC, 0x1F0, 6); m32(m, 0x41AC, 0x1F0, 6); }
        else if(bw == RTW_BW_40){ m32(m, 0x18AC, 0xF000, 4); m32(m, 0x41AC, 0xF000, 4); m32(m, 0x18AC, 0x1F0, 0); m32(m, 0x41AC, 0x1F0, 0); }
        m32(m, 0xC30, 0xFFF, (ch == 13 || ch == 14) ? 0x969 : (ch == 11 || ch == 12) ? 0x96A : 0x9AA);
        if(ch == 14){
            m32(m, 0x1A20, 0xFFFF0000, 0x3DA0); m32(m, 0x1A24, 0xFFFFFFFF, 0x4962C931); m32(m, 0x1A28, 0xFFFF, 0x6AA3);
            m32(m, 0x1A98, 0xFFFF0000, 0xAA7B); m32(m, 0x1A9C, 0xFFFF, 0xF3D7); m32(m, 0x1AA0, 0xFFFFFFFF, 0);
            m32(m, 0x1AAC, 0xFFFFFFFF, 0xFF012455); m32(m, 0x1AB0, 0xFFFFFFFF, 0xFFFF);
        } else {
            m32(m, 0x1A20, 0xFFFF0000, 0x5284); m32(m, 0x1A24, 0xFFFFFFFF, 0x3E18FEC8); m32(m, 0x1A28, 0xFFFF, 0x0A88);
            m32(m, 0x1A98, 0xFFFF0000, 0xACC4); m32(m, 0x1A9C, 0xFFFF, 0xC8B2); m32(m, 0x1AA0, 0xFFFFFFFF, 0x00FAF0DE);
            m32(m, 0x1AAC, 0xFFFFFFFF, 0x00122344); m32(m, 0x1AB0, 0xFFFFFFFF, 0x0FFFFFFF);
        }
        m32(m, 0x808, 0x70, ch == 13 ? 3 : 1);
    } else if(band5(ch)){
        set32(m, 0x1A80, 1u << 18); set32(m, 0x454, 0x80); set32(m, 0x1A14, 0x300); clr32(m, 0x1A9C, 1u << 20);
        m32(m, 0x1C80, 0x3F000000, 0x22); m32(m, 0x808, 0x70, 3);
        u32 agc = (band5_1(ch) || band5_2(ch)) ? 1 : band5_3(ch) ? 2 : 3;
        m32(m, 0x18AC, 0x1F0, agc); m32(m, 0x41AC, 0x1F0, agc);
        if(ch >= 36 && ch <= 51) m32(m, 0xC30, 0xFFF, 0x494);
        else if(ch >= 52 && ch <= 55) m32(m, 0xC30, 0xFFF, 0x493);
        else if(ch >= 56 && ch <= 111) m32(m, 0xC30, 0xFFF, 0x453);
        else if(ch >= 112 && ch <= 119) m32(m, 0xC30, 0xFFF, 0x452);
        else if(ch >= 120 && ch <= 172) m32(m, 0xC30, 0xFFF, 0x412);
        else if(ch >= 173 && ch <= 177) m32(m, 0xC30, 0xFFF, 0x411);
    }
    if(bw == RTW_BW_20){
        m32(m, 0x810, 0x3FF0, 0x19B); m32(m, 0x9B0, 0xF, 0); m32(m, 0x9B0, 0xFFC0, 0);
        m32(m, 0x9B4, 0x700, 7); m32(m, 0x9B4, 0x700000, 6); m32(m, 0x1ABC, 1u << 30, 0);
        m32(m, 0x88C, 0xF000, 1); m32(m, 0xCBC, 1u << 21, 0);
    } else if(bw == RTW_BW_40){
        m32(m, 0x1A00, 1u << 4, pri == 1 ? 1 : 0); m32(m, 0x9B0, 0xF, 5); m32(m, 0x9B0, 0xC0, 0);
        m32(m, 0x9B0, 0xFF00, pri | (pri << 4)); m32(m, 0x1ABC, 1u << 30, 1);
        m32(m, 0x88C, 0xF000, 1); m32(m, 0xCBC, 1u << 21, 1);
    } else if(bw == RTW_BW_80){
        m32(m, 0x9B0, 0xF, 0xA); m32(m, 0x9B0, 0xC0, 0); m32(m, 0x9B0, 0xFF00, pri | (pri << 4));
        m32(m, 0x88C, 0xF000, 6); m32(m, 0xCBC, 1u << 21, 1);
    }
}
static void set_channel_mac(u32 m, u8 ch, u8 bw, u8 pri){
    u8 sc40 = 0;
    if(bw == RTW_BW_80) sc40 = (pri == 1 || pri == 3) ? 9 : 10;
    rtw_w8(m, 0x483, (u8)((pri & 0xF) | ((sc40 & 0xF) << 4)));
    u32 v = rtw_r32(m, 0x668) & ~0x180u;
    if(bw == RTW_BW_80) v |= 0x100; else if(bw == RTW_BW_40) v |= 0x80;
    rtw_w32(m, 0x668, v);
    rtw_w32(m, 0x24, rtw_r32(m, 0x24) & ~0x300000u);
    rtw_w8(m, 0x55C, 80); rtw_w8(m, 0x638, 80);
    u8 c = (u8)(rtw_r8(m, 0x454) & ~0x80);
    if(band5(ch)) c |= 0x80;
    rtw_w8(m, 0x454, c);
}
static void rstb_3wire(u32 m, int en){
    if(en){ m32(m, 0x1C90, 1u << 8, 1); m32(m, 0x1830, 1u << 29, 1); m32(m, 0x4130, 1u << 29, 1); }
    else m32(m, 0x1C90, 1u << 8, 0);
}
static void set_channel_rf(u32 m, u8 ch, u8 bw){
    u32 rf18 = RFR(0, 0x18, RFMASK), rxbb;
    rf18 &= ~((1u << 16) | (1u << 9) | (1u << 8) | 0xFFu | (1u << 18) | (1u << 17) | (1u << 13) | (1u << 12));
    rf18 |= ch <= 14 ? 0 : ((1u << 16) | (1u << 8));
    rf18 |= ch;
    if(band5_4(ch)) rf18 |= 1u << 18; else if(band5_3(ch)) rf18 |= 1u << 17;
    if(bw == RTW_BW_40){ rf18 |= 1u << 13; rxbb = 0x10; }
    else if(bw == RTW_BW_80){ rf18 |= 1u << 12; rxbb = 0x8; }
    else { rf18 |= (1u << 13) | (1u << 12); rxbb = 0x18; }
    rstb_3wire(m, 0);
    for(int p = 0; p < 2; p++){ RFW(p, 0xEE, 0x04, 1); RFW(p, 0x33, 0x1F, 0x12); RFW(p, 0x3F, RFMASK, rxbb); RFW(p, 0xEE, 0x04, 0); }
    RFW(0, 0x18, RFMASK, rf18);
    RFW(1, 0x18, RFMASK, rf18);
    rstb_3wire(m, 1);
}
void rtw8822c_set_channel_ex(u32 m, u8 center, u8 bw, u8 pri){
    s.ch = center; s.bw = bw; s.pri = pri;
    set_channel_bb(m, center, bw, pri);
    set_channel_mac(m, center, bw, pri);
    set_channel_rf(m, center, bw);
    toggle_igi(m);
}
static void rtw8822c_set_channel(u32 m, u8 ch, u8 bw){
    u8 c, p;
    if(ch <= 14 && bw == RTW_BW_80) bw = RTW_BW_40;
    rtw8822c_chan_params(ch, bw, &c, &p);
    rtw8822c_set_channel_ex(m, c, bw, p);
}

u8 rtw8822c_ch_group(u8 ch, u8 rate){
    if(ch == 14) return rate <= 3 ? 5 : 4;
    if(ch <= 2) return 0;
    if(ch <= 5) return 1;
    if(ch <= 8) return 2;
    if(ch <= 11) return 3;
    if(ch <= 13) return 4;
    if(ch >= 36 && ch <= 42) return 0;
    if(ch >= 44 && ch <= 50) return 1;
    if(ch >= 52 && ch <= 58) return 2;
    if(ch >= 60 && ch <= 64) return 3;
    if(ch >= 100 && ch <= 106) return 4;
    if(ch >= 108 && ch <= 114) return 5;
    if(ch >= 116 && ch <= 122) return 6;
    if(ch >= 124 && ch <= 130) return 7;
    if(ch >= 132 && ch <= 138) return 8;
    if(ch >= 140 && ch <= 144) return 9;
    if(ch >= 149 && ch <= 155) return 10;
    if(ch >= 157 && ch <= 161) return 11;
    if(ch >= 165 && ch <= 171) return 12;
    if(ch >= 173 && ch <= 177) return 13;
    return 0;
}
static int nib(u8 v, int hi){ int x = hi ? (v >> 4) : (v & 0xF); return x >= 8 ? x - 16 : x; }
u8 rtw8822c_txpwr_index(int path, u8 ch, u8 bw, u8 rate){
    const u8* e = s.efuse + 0x10 + path * 42;
    u8 g = rtw8822c_ch_group(ch, rate);
    int mcs = (rate >= 0x0C && rate <= 0x2B) || (rate >= 0x2C && rate <= 0x53);
    int ss2 = (rate >= 0x14 && rate <= 0x2B) || rate >= 0x36;
    int p;
    if(ch <= 14){
        p = rate <= 3 ? e[g] : e[6 + g];
        if(rate >= 4 && rate <= 11) p += nib(e[11], 0) * 2;
        if(mcs){
            if(bw == RTW_BW_20){ p += nib(e[11], 1) * 2; if(ss2) p += nib(e[12], 0) * 2; }
            else if(ss2) p += nib(e[12], 1) * 2;
        }
    } else {
        p = e[18 + g];
        if(!mcs) p += nib(e[32], 0) * 2;
        else if(bw == RTW_BW_20){ p += nib(e[32], 1) * 2; if(ss2) p += nib(e[33], 0) * 2; }
        else if(bw == RTW_BW_40){ if(ss2) p += nib(e[33], 1) * 2; }
        else { p = (e[18 + g] + e[18 + (g < 13 ? g + 1 : g)]) / 2; p += nib(e[38], 1) * 2; if(ss2) p += nib(e[39], 1) * 2; }
    }
    p &= 0xFF;
    if(p > 0x7F) p = 0x7F;
    return (u8)p;
}
static void rtw8822c_set_tx_power(u32 m, u8 ch){
    static const u8 rs_start[6] = {0x00, 0x04, 0x0C, 0x14, 0x2C, 0x36}, rs_len[6] = {4, 8, 8, 8, 10, 10};
    u8 c = ch, bw = s.bw;
    if(ch != s.ch && !(s.bw != RTW_BW_20 && ch > s.ch - 8 && ch < s.ch + 8)) bw = RTW_BW_20; else c = s.ch ? s.ch : ch;
    if(!s.efuse_ok){ klog("rtw8822c: no efuse, tx power left at table defaults"); return; }
    u8 ref_cck[2], ref_ofdm[2];
    for(int p = 0; p < 2; p++){ ref_cck[p] = rtw8822c_txpwr_index(p, c, bw, 0x03); ref_ofdm[p] = rtw8822c_txpwr_index(p, c, bw, 0x13); }
    for(int p = 0; p < s.npath; p++){ m32(m, 0x1C90, 1u << 15, 0); m32(m, p ? 0x41A0 : 0x18A0, 0x7F0000, ref_cck[p]); }
    for(int p = 0; p < s.npath; p++){ m32(m, 0x1C90, 1u << 15, 0); m32(m, p ? 0x41E8 : 0x18E8, 0x1FC00, ref_ofdm[p]); }
    signed char diff[4] = {0, 0, 0, 0};
    for(int r = 0; r < 6; r++) for(int j = 0; j < rs_len[r]; j++){
        u8 rate = (u8)(rs_start[r] + j);
        signed char a = (signed char)(rtw8822c_txpwr_index(0, c, bw, rate) - (r ? ref_ofdm[0] : ref_cck[0]));
        signed char b = (signed char)(rtw8822c_txpwr_index(1, c, bw, rate) - (r ? ref_ofdm[1] : ref_cck[1]));
        diff[rate % 4] = a < b ? a : b;
        if(rate % 4 == 3){
            u32 v = (u32)(diff[0] & 0x7F) | ((u32)(diff[1] & 0x7F) << 8) | ((u32)(diff[2] & 0x7F) << 16) | ((u32)(diff[3] & 0x7F) << 24);
            m32(m, 0x1C90, 1u << 15, 0);
            m32(m, 0x3A00 + ((rate - 3) & 0xFC), 0xFFFFFFFF, v);
        }
    }
}

static void rfk_handshake(u32 m, int before){
    if(before){
        if(!s.bt_iqk_timeout){
            int t = 0;
            while(r32m(m, 0xA8, 0x600000) != 0 && t < 30000){ rtw_delay_us(20); t++; if((t & 1023) == 0) ui_pump(); }
            if(t >= 30000){ klog("rtw8822c: wait BT IQK finish timeout"); s.bt_iqk_timeout = 1; }
        }
    }
    rtw_h2c_cmd(m, 0x6Du | ((before ? 1u : 0u) << 8), 0);
    int t = 0;
    while((rtw_r8(m, 0x49C) & 0x1) != 1 && t < 5000){ rtw_delay_us(20); t++; if((t & 1023) == 0) ui_pump(); }
    if(t >= 5000) klog(before ? "rtw8822c: WiFi RFK start H2C not acknowledged" : "rtw8822c: WiFi RFK finish H2C not acknowledged");
}
static void rfk_power_save(u32 m, int ps){
    for(int p = 0; p < s.npath; p++){ m32(m, 0x1B00, 0x6, (u32)p); m32(m, 0x1B08, 0x80, ps ? 0 : 1); }
}
int rtw8822c_do_iqk(u32 m){
    u8 pkt[32] = {0};
    pkt[0] = 0x01; pkt[1] = 0xFF; pkt[2] = 0x0E; pkt[3] = 0x00;
    pkt[4] = 8 + 1; pkt[5] = 0;
    pkt[8] = 0x01;
    if(rtw_h2c_pkt_send(m, pkt, 32) != 0){ klog("rtw8822c: IQK command not taken"); return -1; }
    int ok = 0;
    for(int t = 0; t < 15; t++){ mdelay(20); ui_pump(); if(rtw_r8(m, 0x2D9C) == IQK_DONE_8822C){ ok = 1; break; } }
    if(!ok) klog("rtw8822c: failed to poll IQK status");
    rtw_w8(m, 0x1B10, 0);
    return ok ? 0 : -2;
}
static void rtw8822c_calibrate(u32 m){
    rfk_power_save(m, 0);
    rfk_handshake(m, 1);
    klog("rtw8822c: TX gain-gap calibration (TXGAPK) not ported, tx gain table left at defaults");
    rfk_handshake(m, 0);
    int r = rtw8822c_do_iqk(m);
    g_rtw_iqk_last = r == 0 ? 1 : r;
    m32(m, 0x1B00, 0xF, 0xC);
    m32(m, 0x1B08, 1u << 31, 0);
    m32(m, 0x1B60, 1u << 31, 0);
    klog("rtw8822c: DPK not ported, digital predistortion disabled on both paths");
    rfk_power_save(m, 1);
}

static int lte_write_mask(u32 m, u16 off, u32 mask, u32 v){
    if(!hw_ready(m, 0x1700, 0x20000000, 1)) return 0;
    rtw_w32(m, 0x1700, 0x800F0000u | off);
    u32 t = rtw_r32(m, 0x1708);
    t = (t & ~mask) | ((v << shift_of(mask)) & mask);
    if(!hw_ready(m, 0x1700, 0x20000000, 1)) return 0;
    rtw_w32(m, 0x1704, t);
    rtw_w32(m, 0x1700, 0xC00F0000u | off);
    return 1;
}
static void rtw8822c_coex_wifi_only(u32 m){
    rtw_h2c_cmd(m, 0x69u | (0x0Cu << 8) | (0x01u << 16), 0);
    set8(m, 0x550, 0x08);
    lte_write_mask(m, 0x38, 0x80, 0);
    lte_write_mask(m, 0xA0, 0xFFFF, 0xFFFF);
    lte_write_mask(m, 0xA4, 0xFFFF, 0xFFFF);
    set8(m, 0x550, 0x08);
    m8(m, 0x790, 0x3F, 0x5);
    rtw_w8(m, 0x778, 0x1);
    set32(m, 0x40, 0x20); set32(m, 0x40, 0x200);
    set8(m, 0x4C6, 0x10); clr8(m, 0x4C6, 0x20);
    set16(m, 0x762, 0x1000);
    clr8(m, 0x4FC, 0x3);
    RFW(1, 0x1, RFMASK, 0x40000);
    m8(m, 0x6CC + 0, 1u << 3, 1);
    m8(m, 0x6CC + 0, 1u << 4, 1);
    m8(m, 0x6CC + 3, 1u << 3, 1);
    lte_write_mask(m, 0x38, 0xC000, 1); lte_write_mask(m, 0x38, 0x0C00, 1);
    lte_write_mask(m, 0x38, 0x3000, 3); lte_write_mask(m, 0x38, 0x0300, 3);
    set8(m, 0x73, 0x04);
    rtw_w16(m, 0xAA, (u16)(0x2 | 0x1 | 0x2 | 0x8000));
    rtw_w32(m, 0x6C0, 0x55555555); rtw_w32(m, 0x6C4, 0x55555555); rtw_w32(m, 0x6C8, 0xF0FFFFFF);
    rtw_w8(m, 0xFF1A, 0);
    m8(m, 0x66, 0x10, 0); m8(m, 0x67, 0x01, 0); m8(m, 0x42, 0x08, 0); m8(m, 0x65, 0x80, 0); m8(m, 0x73, 0x08, 0);
}
static void rtw8822c_rfe_setup(u32 m, int band5g){
    (void)band5g;
    if(s.rfe == 5) m32(m, 0x1010, 0x30000000u, 1);
}

const rtw_chip rtw8822c_chip = {
    .name = "RTL8822CE", .vendor = 0x10EC, .device = 0xC822, .bus = RTW_BUS_PCIE,
    .n_paths = 2, .wcpu = RTW_WCPU_3081,
    .txff_pages = RTW8822C_TXFF_SIZE / 128, .rxff_size = 24576, .txff_size = RTW8822C_TXFF_SIZE, .rsvd_drv_pages = 16, .csi_pages = 50,
    .rf_base = {0x3C00, 0x4C00}, .rf_sipi = {0x1808, 0x4108}, .rf_mix = 1,
    .tables_in_phy_init = 1, .cond_mode = RTW_COND_CHIP, .sys_func_en = 0xD8, .fw_rxff_size = 12288,
    .efuse_phy = 512, .efuse_log = 768, .efuse_prot = 124,
    .tx_desc_sz = 48, .rx_desc_sz = 24, .tx_buf_desc_sz = 16,
    .fw_start = _binary_rtw8822c_fw_bin_start, .fw_end = _binary_rtw8822c_fw_bin_end, .fw_name = "rtw8822c_fw.bin",
    .page_table = {64, 64, 64, 64, 1},
    .rqpn = {RTW8822C_DMA_NORMAL, RTW8822C_DMA_NORMAL, RTW8822C_DMA_LOW, RTW8822C_DMA_LOW, RTW8822C_DMA_EXTRA, RTW8822C_DMA_HIGH},
    .mac_tbl = {0, 0, 1},
    .bb_tbl = {rtw8822c_bb, sizeof(rtw8822c_bb) / 8, 4},
    .agc_tbl = {rtw8822c_agc, sizeof(rtw8822c_agc) / 8, 4},
    .agc_btg_tbl = {0, 0, 4},
    .rf_tbl = {{rtw8822c_rf_a, sizeof(rtw8822c_rf_a) / 8, 5}, {rtw8822c_rf_b, sizeof(rtw8822c_rf_b) / 8, 5}},
    .power_on = rtw8822c_power_on, .power_off = rtw8822c_power_off,
    .efuse_parse = rtw8822c_efuse_parse, .mac_init = rtw8822c_mac_init_op, .phy_init = rtw8822c_phy_init,
    .set_channel = rtw8822c_set_channel, .set_tx_power = rtw8822c_set_tx_power, .calibrate = rtw8822c_calibrate,
    .coex_wifi_only = rtw8822c_coex_wifi_only, .rfe_setup = rtw8822c_rfe_setup,
};
const rtw_chip rtw8822c_chip_c82f = {
    .name = "RTL8822CE", .vendor = 0x10EC, .device = 0xC82F, .bus = RTW_BUS_PCIE,
    .n_paths = 2, .wcpu = RTW_WCPU_3081,
    .txff_pages = RTW8822C_TXFF_SIZE / 128, .rxff_size = 24576, .txff_size = RTW8822C_TXFF_SIZE, .rsvd_drv_pages = 16, .csi_pages = 50,
    .rf_base = {0x3C00, 0x4C00}, .rf_sipi = {0x1808, 0x4108}, .rf_mix = 1,
    .tables_in_phy_init = 1, .cond_mode = RTW_COND_CHIP, .sys_func_en = 0xD8, .fw_rxff_size = 12288,
    .efuse_phy = 512, .efuse_log = 768, .efuse_prot = 124,
    .tx_desc_sz = 48, .rx_desc_sz = 24, .tx_buf_desc_sz = 16,
    .fw_start = _binary_rtw8822c_fw_bin_start, .fw_end = _binary_rtw8822c_fw_bin_end, .fw_name = "rtw8822c_fw.bin",
    .page_table = {64, 64, 64, 64, 1},
    .rqpn = {RTW8822C_DMA_NORMAL, RTW8822C_DMA_NORMAL, RTW8822C_DMA_LOW, RTW8822C_DMA_LOW, RTW8822C_DMA_EXTRA, RTW8822C_DMA_HIGH},
    .mac_tbl = {0, 0, 1},
    .bb_tbl = {rtw8822c_bb, sizeof(rtw8822c_bb) / 8, 4},
    .agc_tbl = {rtw8822c_agc, sizeof(rtw8822c_agc) / 8, 4},
    .agc_btg_tbl = {0, 0, 4},
    .rf_tbl = {{rtw8822c_rf_a, sizeof(rtw8822c_rf_a) / 8, 5}, {rtw8822c_rf_b, sizeof(rtw8822c_rf_b) / 8, 5}},
    .power_on = rtw8822c_power_on, .power_off = rtw8822c_power_off,
    .efuse_parse = rtw8822c_efuse_parse, .mac_init = rtw8822c_mac_init_op, .phy_init = rtw8822c_phy_init,
    .set_channel = rtw8822c_set_channel, .set_tx_power = rtw8822c_set_tx_power, .calibrate = rtw8822c_calibrate,
    .coex_wifi_only = rtw8822c_coex_wifi_only, .rfe_setup = rtw8822c_rfe_setup,
};
