import sys, os, re
# Builds a host test of the rtw88 multi-chip layer: chip.c and chips/rtw8821c.c are compiled whole, the bus/RF/BB
# helpers and the core wrappers are cut out of rtw_efuse.c / rtw8821ce.c, and everything runs against a simulated
# card in tools/tests/wifi/chip_main.c, together with the frozen v10.98 code (chip_ref_v1098.c) for trace compares.
# usage: chip_gen.py <kernel dir> <out.c> <chip_main.c>
K = sys.argv[1]
def rd(p): return open(os.path.join(K, p)).read()
def fn(s, sig):
    i = s.index(sig); j = s.index('{', i); d = 0; k = j
    while True:
        c = s[k]
        if c == '{': d += 1
        elif c == '}':
            d -= 1
            if d == 0: return s[i:k+1]
        k += 1
def strip_inc(s, inline=()):
    out = []
    for l in s.splitlines():
        m = re.match(r'\s*#include\s+"([^"]+)"', l)
        if m:
            if m.group(1) in inline: out.append(rd(m.group(1)))
            continue
        if re.match(r'\s*#include\s+<', l): continue
        out.append(l)
    return '\n'.join(out)
hdr = ''.join(strip_inc(rd(h)) + '\n' for h in ['include/nvx/drivers/net/wifi/rtw_efuse.h', 'include/nvx/drivers/net/wifi/rtw8821ce.h', 'include/nvx/drivers/net/wifi/rtw_chip.h'])
pre = r'''#include <stdio.h>
#include <string.h>
#include <stdlib.h>
typedef unsigned char u8; typedef unsigned short u16; typedef unsigned u32; typedef unsigned long long u64;
void rtw_delay_us(u32 us); void io_wait(void); void* dma_alloc_pages(int n); void cpu_wmb(void); void klog(const char* s);
void tcol(u32 c); void term_puts(const char* s); void term_putc(char c); void rtw_jlog(const char* a,const char* b,int v); void ui_pump(void); int rtw_get_nettype(u32 m);
__asm__(".section .rodata\n.globl _binary_rtw8821c_fw_bin_start\n.globl _binary_rtw8821c_fw_bin_end\n_binary_rtw8821c_fw_bin_start:\n.fill 256,1,0x5a\n_binary_rtw8821c_fw_bin_end:\n.previous\n");
'''
defs = r'''
u8 rtw_phymap[RTW_EFUSE_PHY_SZ]; u8 rtw_logmap[RTW_EFUSE_LOG_SZ]; int rtw_efuse_valid=0;
int g_rtw_rf_path=0; int g_chquiet=0; int g_wifi_txpwr=1, g_wifi_band5=0; u8 g_wifi_curch=0; u8 g_wifi_pwr[20]; int g_rtw_coex_done=0;
'''
ef = rd('drivers/net/wifi/rtw88/rtw_efuse.c')
core = rd('drivers/net/wifi/rtw88/rtw8821ce.c')
code = pre + hdr + defs
code += '\n'.join(fn(ef, s) for s in ['u8  rtw_r8 (', 'u16 rtw_r16(', 'u32 rtw_r32(', 'void rtw_w8 (', 'void rtw_w16(', 'void rtw_w32(',
    'static int rtw_rf_path_ok(', 'static u32 rtw_rf_base(', 'static u32 rtw_rf_sipi(', 'static u32 rtw_mask_shift(', 'u32 rtw_rf_read(', 'void rtw_rf_write(',
    'void rtw_bb_write_mask(', 'u32 rtw_bb_read_mask(', 'void rtw_rf_write_a(', 'int rtw_efuse_phy_sz(', 'int rtw_efuse_log_sz(', 'int rtw_efuse_prot_sz(', 'void rtw_efuse_apply(']) + '\n'
code += '\n'.join(fn(core, s) for s in ['static int rtw_rfe(void){', 'static int rtw_lte_wait(', 'u32 rtw_lte_read(', 'int rtw_lte_write(', 'int rtw_lte_mask(',
    'int rtw_coex_wifi_only(', 'int rtw_rfe_type(', 'void rtw_set_txpwr(', 'void rtw_set_channel_2g(', 'int rtw_mac_trx_init(', 'u32 rtw_rf_read_a(', 'int rtw_apply_one(']) + '\n'
code += strip_inc(rd('drivers/net/wifi/rtw88/chip.c')) + '\n'
code += strip_inc(rd('drivers/net/wifi/rtw88/chips/rtw8821c.c'), inline=('drivers/net/wifi/rtw88/rtw_tables_cond.h',)) + '\n'
code += open(os.path.join(os.path.dirname(os.path.abspath(sys.argv[3])), 'chip_ref_v1098.c')).read() + '\n'
open(sys.argv[2], 'w').write(code + open(sys.argv[3]).read())
