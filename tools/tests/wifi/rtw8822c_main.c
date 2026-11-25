/* Host test for the RTL8822CE chip file. A simulated card: a 64 KB register file that records every write,
   RF registers behind the 8822C direct windows (0x3c00/0x4c00) and the SIPI ports (0x1808/0x4108), plus the
   few status bits the bring-up polls (power FSM, efuse, LTE coex, RFK ack, IQK done). */
#include "include/nvx/drivers/net/wifi/rtw_chip.h"
#include "drivers/net/wifi/realtek/rtw88/chips/rtw8822c.h"
#include "drivers/net/wifi/realtek/rtw88/chips/rtw8822c_tables.h"

typedef struct { u32 a, v; u8 sz; } wrec;
static wrec* rec; static int nrec, caprec;
static u8 regs[0x10000];
static u32 rf[2][256];
static int iqk_sent, h2c_n; static u32 h2c_w0[64]; static u8 last_pkt[32]; static int last_pkt_len;
static u64 waited;
rtw_efuse_info g_rtw_efuse;

static void logw(u32 a, u32 v, u8 sz){ if(nrec == caprec){ caprec = caprec ? caprec * 2 : 65536; rec = realloc(rec, sizeof(wrec) * caprec); } rec[nrec].a = a; rec[nrec].v = v; rec[nrec].sz = sz; nrec++; }
static int rfwin(u32 a, int* p, u32* ra){
    if(a >= 0x3C00 && a < 0x4000){ *p = 0; *ra = (a - 0x3C00) >> 2; return 1; }
    if(a >= 0x4C00 && a < 0x5000){ *p = 1; *ra = (a - 0x4C00) >> 2; return 1; }
    return 0;
}
static u32 raw32(u32 a){ return (u32)regs[a] | ((u32)regs[a + 1] << 8) | ((u32)regs[a + 2] << 16) | ((u32)regs[a + 3] << 24); }
static void put32(u32 a, u32 v){ for(int i = 0; i < 4; i++) regs[a + i] = (u8)(v >> (8 * i)); }
static u32 rd(u32 a, int sz){
    int p; u32 ra;
    a &= 0xFFFF;
    if(rfwin(a & ~3u, &p, &ra)) return rf[p][ra] >> ((a & 3) * 8);
    if(a == 0x0006) regs[6] |= 0x02;
    if(a == 0x0005) regs[5] &= (u8)~0x03;
    if(a >= 0x30 && a <= 0x33) regs[0x33] |= 0x80, regs[0x30] = 0xFF;
    if(a >= 0x1700 && a <= 0x1703) regs[0x1703] |= 0x20;
    if(a == 0x49C) regs[0x49C] |= 1;
    if(a == 0x3F2) regs[0x3F2] = 0;
    if(a == 0x2D9C && iqk_sent) regs[0x2D9C] = 0xAA;
    u32 v = raw32(a & ~3u) >> ((a & 3) * 8);
    if(sz == 1) return v & 0xFF;
    if(sz == 2) return v & 0xFFFF;
    return v;
}
static void wr(u32 a, u32 v, int sz){
    int p; u32 ra;
    a &= 0xFFFF; logw(a, v, (u8)sz);
    if(sz == 4 && rfwin(a, &p, &ra)){ rf[p][ra] = v & 0xFFFFF; return; }
    if(sz == 4 && (a == 0x1808 || a == 0x4108)){ rf[a == 0x4108][(v >> 20) & 0xFF] = v & 0xFFFFF; put32(a, v); return; }
    for(int i = 0; i < sz; i++) regs[a + i] = (u8)(v >> (8 * i));
}
u8  rtw_r8 (u32 m, u32 o){ (void)m; return (u8)rd(o, 1); }
u16 rtw_r16(u32 m, u32 o){ (void)m; return (u16)rd(o, 2); }
u32 rtw_r32(u32 m, u32 o){ (void)m; return rd(o, 4); }
void rtw_w8 (u32 m, u32 o, u8 v){ (void)m; wr(o, v, 1); }
void rtw_w16(u32 m, u32 o, u16 v){ (void)m; wr(o, v, 2); }
void rtw_w32(u32 m, u32 o, u32 v){ (void)m; wr(o, v, 4); }
u32 rtw_rdsz(u32 m, u32 o, u8 sz){ return sz == 1 ? rtw_r8(m, o) : sz == 2 ? rtw_r16(m, o) : rtw_r32(m, o); }
void rtw_delay_us(u32 us){ waited += us; }
static u32 sh(u32 k){ u32 s = 0; while(s < 31 && !((k >> s) & 1)) s++; return s; }
void rtw_bb_write_mask(u32 m, u32 a, u32 k, u32 v){ if(k == 0xFFFFFFFFu){ rtw_w32(m, a, v); return; } u32 o = rtw_r32(m, a); rtw_w32(m, a, (o & ~k) | ((v << sh(k)) & k)); }
u32 rtw_bb_read_mask(u32 m, u32 a, u32 k){ return (rtw_r32(m, a) & k) >> sh(k); }
int g_rtw_iqk_last; static int h2c_fail, npump;
void ui_pump(void){ npump++; }
int rtw_h2c_pkt_send(u32 m, const u8* p, int len){ (void)m; if(h2c_fail) return -1; memcpy(last_pkt, p, len > 32 ? 32 : len); last_pkt_len = len; if(p[2] == 0x0E) iqk_sent = 1; return 0; }
int rtw_h2c_cmd(u32 m, u32 w0, u32 w1){ (void)m; (void)w1; if(h2c_n < 64) h2c_w0[h2c_n++] = w0; return 0; }
void klog(const char* s){ (void)s; }
u32 rtw_rf_read(u32 m, int path, u32 addr, u32 mask){ (void)m; (void)path; (void)addr; (void)mask; printf("contract rtw_rf_read called (8821C addressing)\n"); exit(1); }
void rtw_rf_write(u32 m, int path, u32 addr, u32 mask, u32 val){ (void)m; (void)path; (void)addr; (void)mask; (void)val; printf("contract rtw_rf_write called (8821C addressing)\n"); exit(1); }

static void reset(void){ memset(regs, 0, sizeof regs); memset(rf, 0, sizeof rf); nrec = 0; iqk_sent = 0; h2c_n = 0; waited = 0; regs[0x100] = 0xEA; put32(0xF0, (1u << 27) | (3u << 12)); }
static int bad;
#define CHECK(c, ...) do{ if(!(c)){ bad++; printf("FAIL %s:%d: ", __FILE__, __LINE__); printf(__VA_ARGS__); printf("\n"); } }while(0)

static int is_delay_bb(u32 a){ return a >= 0xF9 && a <= 0xFE; }
static void cmp_bb(const char* nm, int from, const u32* e, int en){
    int k = from, bad0 = bad;
    for(int i = 0; i < en; i++){
        u32 a = e[i * 2], d = e[i * 2 + 1];
        if(is_delay_bb(a)) continue;
        if(k >= nrec || rec[k].a != a || rec[k].v != d || rec[k].sz != 4){ CHECK(0, "%s write %d: want %04x=%08x got %04x=%08x", nm, i, a, d, k < nrec ? rec[k].a : 0, k < nrec ? rec[k].v : 0); break; }
        k++;
    }
    CHECK(bad != bad0 || k == nrec, "%s: %d extra writes", nm, nrec - k);
}
static void cmp_rf(const char* nm, int path, const u32* e, int en){
    u32 base = path ? 0x4C00 : 0x3C00, sipi = path ? 0x4108 : 0x1808;
    int k = 0, n = 0;
    for(int i = 0; i < en; i++){
        u32 a = e[i * 2], d = e[i * 2 + 1];
        if(a == 0xFFE || a == 0xFE) continue;
        u32 wa = a == 0 ? sipi : base + (a << 2), wv = a == 0 ? d : d;
        if(k >= nrec || rec[k].a != wa || rec[k].v != wv){ CHECK(0, "%s write %d (rf %02x=%05x): got %04x=%08x", nm, i, a, d, k < nrec ? rec[k].a : 0, k < nrec ? rec[k].v : 0); return; }
        k++; n++;
    }
    CHECK(k == nrec, "%s: %d extra writes", nm, nrec - k);
}
static void tables(int cut, int rfe, const u32* bb, int bbn, const u32* agc, int agcn, const u32* cal, int caln, const u32* ra, int ran, const u32* rb, int rbn){
    char nm[64];
    g_rtw_cut = cut; g_rtw_pkg = 15; g_rtw_rfe = rfe;
    reset(); rtw_apply_cond(0, rtw8822c_bb, rtw8822c_bb_n, 4, "bb", 0); sprintf(nm, "bb cut%d rfe%d", cut, rfe); cmp_bb(nm, 0, bb, bbn);
    reset(); rtw_apply_cond(0, rtw8822c_agc, rtw8822c_agc_n, 4, "agc", 0); sprintf(nm, "agc cut%d rfe%d", cut, rfe); cmp_bb(nm, 0, agc, agcn);
    reset(); rtw_apply_cond(0, rtw8822c_array_mp_cal_init, rtw8822c_array_mp_cal_init_n, 4, "cal", 0); sprintf(nm, "rfk-init cut%d rfe%d", cut, rfe); cmp_bb(nm, 0, cal, caln);
    reset(); rtw8822c_load_rf(0, 0, rtw8822c_rf_a, rtw8822c_rf_a_n); sprintf(nm, "rf_a->path A cut%d rfe%d", cut, rfe); cmp_rf(nm, 0, ra, ran);
    reset(); rtw8822c_load_rf(0, 1, rtw8822c_rf_b, rtw8822c_rf_b_n); sprintf(nm, "rf_b->path B cut%d rfe%d", cut, rfe); cmp_rf(nm, 1, rb, rbn);
    printf("  tables cut %d rfe 0x%02x: bb %d agc %d rfk-init %d rf_a %d rf_b %d writes ok\n", cut, rfe, bbn, agcn, caln, ran, rbn);
}
static int last_write(u32 a, u32* v){ for(int i = nrec - 1; i >= 0; i--) if(rec[i].a == a){ *v = rec[i].v; return 1; } return 0; }
static void chan(u8 ch, u8 bw, u32 rf18_want, u32 rxbb, u32 sco){
    reset(); rf[0][0x18] = 0x0F3FF; rf[1][0x18] = 0x0F3FF;
    rtw8822c_chip.set_channel(0, ch, bw);
    u32 v;
    CHECK(last_write(0x3C60, &v) && v == rf18_want, "ch %u bw %u: path A RF18 %05x want %05x", ch, bw, v, rf18_want);
    CHECK(last_write(0x4C60, &v) && v == rf18_want, "ch %u bw %u: path B RF18 %05x want %05x", ch, bw, v, rf18_want);
    CHECK(rf[0][0x3F] == rxbb && rf[1][0x3F] == rxbb, "ch %u: RF3F rxbb %x/%x want %x", ch, rf[0][0x3F], rf[1][0x3F], rxbb);
    CHECK((raw32(0xC30) & 0xFFF) == sco, "ch %u: 0xc30 %03x want %03x", ch, raw32(0xC30) & 0xFFF, sco);
    CHECK(((regs[0x454] & 0x80) != 0) == (ch > 14), "ch %u: CCK check bit", ch);
    CHECK(!(raw32(0x1C90) & 0x100) == 0, "ch %u: 3-wire left in reset", ch);
    int seen_rf18_sipi = 0; for(int i = 0; i < nrec; i++) if((rec[i].a == 0x1808 || rec[i].a == 0x4108) && ((rec[i].v >> 20) & 0xFF) == 0x18) seen_rf18_sipi = 1;
    CHECK(!seen_rf18_sipi, "ch %u: RF18 went through SIPI, rtw88 mix mode writes it directly", ch);
    printf("  set_channel ch %3u bw %u: RF18 %05x ok\n", ch, bw, rf18_want);
}

int main(void){
    const rtw_chip* c = &rtw8822c_chip;
    CHECK(!strcmp(c->name, "RTL8822CE") && c->vendor == 0x10EC && c->device == 0xC822, "id");
    CHECK(rtw8822c_chip_c82f.device == 0xC82F && rtw8822c_chip_c82f.phy_init == c->phy_init, "c82f entry");
    CHECK(c->bus == RTW_BUS_PCIE && c->n_paths == 2 && c->wcpu == RTW_WCPU_3081, "bus/paths/wcpu");
    CHECK(c->tx_desc_sz == 48 && c->rx_desc_sz == 24 && c->tx_buf_desc_sz == 16 && c->rsvd_drv_pages == 16 && c->rxff_size == 24576, "sizes");
    CHECK(c->page_table.hq == 64 && c->page_table.nq == 64 && c->page_table.lq == 64 && c->page_table.exq == 64 && c->page_table.gapq == 1, "page table");
    CHECK(c->rqpn.vo == 2 && c->rqpn.vi == 2 && c->rqpn.be == 1 && c->rqpn.bk == 1 && c->rqpn.mg == 0 && c->rqpn.hi == 3, "rqpn");
    CHECK(c->fw_start && c->fw_end - c->fw_start == 202600 && c->fw_start[0] == 0x22 && c->fw_start[1] == 0x88, "firmware blob");
    CHECK(c->power_on && c->power_off && c->efuse_parse && c->mac_init && c->phy_init && c->set_channel && c->set_tx_power && c->calibrate && c->coex_wifi_only && c->rfe_setup, "ops");
    CHECK(c->rf_tbl[0].t == rtw8822c_rf_a && c->rf_tbl[1].t == rtw8822c_rf_b && c->rf_tbl[0].n == rtw8822c_rf_a_n && c->bb_tbl.n == rtw8822c_bb_n && c->agc_tbl.n == rtw8822c_agc_n, "table refs");
    printf("chip table ok\n");

    reset();
    CHECK(c->power_on(0) == 0, "power_on failed");
    { int saw75 = 0, saw1064 = 0; for(int i = 0; i < nrec; i++){ if(rec[i].a == 0x75) saw75++; if(rec[i].a == 0x1064 && (rec[i].v & 2)) saw1064 = 1; }
      CHECK(saw75 == 2 && saw1064, "power sequence: 0x75 writes %d, 0x1064 %d", saw75, saw1064);
      u32 v; CHECK(last_write(0x3FC, &v) && (v & (1u << 26)), "cut D: HCI_MIX_CFG PDN_AUX_TO_FAST_CLK"); }
    printf("power_on ok\n");

    static u8 map[768]; memset(map, 0xFF, sizeof map);
    for(int i = 0; i < 6; i++) map[0x120 + i] = (u8)(0x10 + i);
    map[0xCA] = 5; map[0xB9] = 0x3C; map[0xD0] = 0x20; map[0xD1] = 0x22;
    for(int p = 0; p < 2; p++){ u8* e = map + 0x10 + p * 42; for(int i = 0; i < 6; i++) e[i] = 0x50; for(int i = 0; i < 5; i++) e[6 + i] = (u8)(0x60 + i); e[11] = 0x12; e[12] = 0xF0; for(int i = 0; i < 14; i++) e[18 + i] = (u8)(0x58 + i); e[32] = 0x2E; e[33] = 0x00; e[38] = 0x10; e[39] = 0x00; }
    CHECK(c->efuse_parse(map, 768, &g_rtw_efuse) == 0, "efuse parse");
    CHECK(g_rtw_efuse.mac[0] == 0x10 && g_rtw_efuse.mac[5] == 0x15 && g_rtw_efuse.rfe_option == 5 && g_rtw_efuse.crystal_cap == 0x3C && g_rtw_efuse.thermal_meter == 0x21, "efuse fields");
    CHECK(g_rtw_efuse.tx_pwr_idx_2g[1][5] == 0x62 && g_rtw_efuse.tx_pwr_idx_5g[0][10] == 0x62 && g_rtw_rfe == 5, "efuse power");
    CHECK(rtw8822c_txpwr_index(0, 6, RTW_BW_20, 0x00) == 0x50, "cck idx");
    CHECK(rtw8822c_txpwr_index(0, 6, RTW_BW_20, 0x04) == 0x62 + 2 * 2, "ofdm idx %02x", rtw8822c_txpwr_index(0, 6, RTW_BW_20, 0x04));
    CHECK(rtw8822c_txpwr_index(0, 6, RTW_BW_20, 0x0C) == 0x62 + 2, "ht20 idx");
    CHECK(rtw8822c_txpwr_index(0, 6, RTW_BW_20, 0x14) == 0x62 + 2, "ht20 2ss idx (bw20 diff 0)");
    CHECK(rtw8822c_txpwr_index(0, 6, RTW_BW_40, 0x14) == 0x62 - 2, "ht40 2ss idx (bw40 diff -1)");
    CHECK(rtw8822c_txpwr_index(0, 36, RTW_BW_20, 0x04) == 0x58 + 2 * -2, "5g ofdm idx");
    CHECK(rtw8822c_txpwr_index(0, 42, RTW_BW_80, 0x2C) == (0x58 + 0x59) / 2 + 2, "5g vht80 idx");
    printf("efuse + tx power index ok\n");

    run_tables();

    reset(); rtw8822c_rf_write(0, 1, 0x55, 0xFFFFF, 0x12345);
    CHECK(nrec == 1 && rec[0].a == 0x4C00 + 0x55 * 4 && rec[0].v == 0x12345, "direct RF write path B: %04x=%x", rec[0].a, rec[0].v);
    reset(); rtw8822c_rf_write(0, 0, 0x8F, 1u << 13, 1);
    CHECK(nrec == 1 && rec[0].a == 0x3C00 + 0x8F * 4 && rec[0].v == 0x2000, "masked direct RF write path A");
    reset(); rf[1][0] = 0x30000; rtw8822c_rf_write(0, 1, 0x00, 0x0000F, 0x5);
    CHECK(nrec == 1 && rec[0].a == 0x4108 && rec[0].v == 0x30005, "RF0 via SIPI path B: %04x=%x", rec[0].a, rec[0].v);
    reset(); rf[0][0x18] = 0xABCDE; CHECK(rtw8822c_rf_read(0, 0, 0x18, 0xFF) == 0xDE && rtw8822c_rf_read(0, 0, 0x18, 0xF0000) == 0xA, "RF read path A");
    printf("RF addressing ok (direct 0x3c00/0x4c00, SIPI 0x1808/0x4108 only for RF 0)\n");

    chan(1, RTW_BW_20, (0x0F3FF & ~0x733FFu) | 0x3001, 0x18, 0x9AA);
    chan(6, RTW_BW_20, (0x0F3FF & ~0x733FFu) | 0x3006, 0x18, 0x9AA);
    chan(11, RTW_BW_20, (0x0F3FF & ~0x733FFu) | 0x300B, 0x18, 0x96A);
    chan(36, RTW_BW_20, (0x0F3FF & ~0x733FFu) | 0x13124, 0x18, 0x494);
    chan(149, RTW_BW_20, (0x0F3FF & ~0x733FFu) | 0x53195, 0x18, 0x412);
    chan(36, RTW_BW_40, (0x0F3FF & ~0x733FFu) | 0x12126, 0x10, 0x494);
    chan(40, RTW_BW_80, (0x0F3FF & ~0x733FFu) | 0x1112A, 0x8, 0x494);
    { reset(); c->set_channel(0, 44, RTW_BW_80); CHECK(regs[0x483] == (1 | (9 << 4)), "80 MHz ch44: DATA_SC %02x", regs[0x483]); CHECK(((raw32(0x9B0) >> 8) & 0xFF) == 0x11, "ch44 TXBWCTL sc"); }
    { reset(); c->set_channel(0, 36, RTW_BW_40); CHECK(regs[0x483] == 2 && (raw32(0x668) & 0x180) == 0x80, "40 MHz ch36 lower: DATA_SC %02x", regs[0x483]); }

    reset(); c->set_channel(0, 6, RTW_BW_20); nrec = 0; c->set_tx_power(0, 6);
    { u32 v; CHECK(((raw32(0x18A0) >> 16) & 0x7F) == 0x50 && ((raw32(0x41A0) >> 16) & 0x7F) == 0x50, "cck ref");
      CHECK(((raw32(0x18E8) >> 10) & 0x7F) == 0x64, "ofdm ref %x", (raw32(0x18E8) >> 10) & 0x7F);
      CHECK(last_write(0x3A04, &v) && v == 0x02020202, "ofdm 6-18M diff %08x", v);
      CHECK(last_write(0x3A0C, &v) && v == 0, "ht mcs0-3 diff %08x", v); }
    printf("tx power ok\n");

    reset(); h2c_n = 0; c->calibrate(0);
    CHECK(h2c_n == 2 && h2c_w0[0] == 0x16D && h2c_w0[1] == 0x06D, "RFK handshake H2C 0x6d start/stop: n=%d %x %x", h2c_n, h2c_w0[0], h2c_w0[1]);
    CHECK(last_pkt_len == 32 && last_pkt[0] == 0x01 && last_pkt[1] == 0xFF && last_pkt[2] == 0x0E && last_pkt[4] == 9 && last_pkt[8] == 0x01, "IQK H2C packet");
    { u32 v; CHECK(last_write(0x1B10, &v) && v == 0, "IQKSTAT cleared"); CHECK(!(raw32(0x1B08) & (1u << 31)) && !(raw32(0x1B60) & (1u << 31)), "DPD disabled"); }
    CHECK(npump > 0 && (g_rtw_iqk_last == 1 || g_rtw_iqk_last == -2), "IQK result %d, pumps %d", g_rtw_iqk_last, npump);
    reset(); h2c_fail = 1; c->calibrate(0); h2c_fail = 0; CHECK(g_rtw_iqk_last == -1, "IQK send failure -> %d", g_rtw_iqk_last);
    printf("calibrate ok (TXGAPK/DPK stubbed)\n");

    reset(); h2c_n = 0; c->coex_wifi_only(0);
    CHECK(raw32(0x6C0) == 0x55555555 && raw32(0x6C8) == 0xF0FFFFFF && (regs[0x73] & 0x04) && rf[1][0x1] == 0x40000 && h2c_n == 1 && h2c_w0[0] == 0x010C69, "coex wifi-only");
    printf("coex ok\n");

    reset(); c->efuse_parse(map, 768, &g_rtw_efuse); waited = 0; c->mac_init(0); c->phy_init(0);
    CHECK(g_rtw_cut == 3 && g_rtw_rfe == 5, "phy_init cond");
    { int a = 0, b = 0, sipi = 0; for(int i = 0; i < nrec; i++){ if(rec[i].a >= 0x3C00 && rec[i].a < 0x4000) a++; if(rec[i].a >= 0x4C00 && rec[i].a < 0x5000) b++; if(rec[i].a == 0x1808 || rec[i].a == 0x4108) sipi++; }
      CHECK(a > 500 && b > 500 && sipi > 0, "phy_init RF writes A %d B %d sipi %d", a, b, sipi);
      int first_b = -1, first_a = -1; for(int i = 0; i < nrec; i++){ if(first_b < 0 && rec[i].a >= 0x4C00 && rec[i].a < 0x5000) first_b = i; if(first_a < 0 && rec[i].a >= 0x3C00 && rec[i].a < 0x4000) first_a = i; }
      CHECK(first_b >= 0 && first_b < first_a, "rf_b (path B) loads before rf_a, as rtw88 rf_tbl order"); }
    CHECK(((raw32(0x1040) >> 10) & 0x3FFF) == (0x3C | (0x3C << 7)), "crystal cap");
    CHECK((raw32(0x1800) & 0xFFFFF) == 0x33312 && (raw32(0x4100) & 0xFFFFF) == 0x33312 && (raw32(0x820) & 0xFF) == 0x31, "trx mode 2T2R");
    printf("phy_init ok (%d writes, %llu us of delays)\n", nrec, (unsigned long long)waited);
    CHECK(rf_write_a_calls == 0, "8821C RF path used");
    printf(bad ? "FAILED: %d\n" : "all rtw8822c tests passed\n", bad);
    return bad != 0;
}
