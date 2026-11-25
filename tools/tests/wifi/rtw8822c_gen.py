import re, sys
# Builds the RTL8822C host test: cuts rtw_apply_cond/rtw_cond_ok out of rtw_sec.c and rtw_apply_one out of
# rtw8821ce.c, and computes the expected table writes straight from the Linux rtw8822c_table.c with the rtw88
# rules (rtw_parse_tbl_phy_cond + check_positive, PCIe, pkg 15) as an independent oracle.
# build: python3 tools/tests/wifi/rtw8822c_gen.py drivers/net/wifi/realtek/rtw88/rtw_sec.c drivers/net/wifi/realtek/rtw88/rtw8821ce.c \
#          /tmp/ref/linux/drivers/net/wireless/realtek/rtw88/rtw8822c_table.c tools/tests/wifi/rtw8822c_main.c /tmp/t.c
#        (cd assets/firmware && ld -r -b binary -o /tmp/fw.o rtw8822c_fw.bin)
#        gcc -fsanitize=address,undefined -no-pie -Itools/tests/wifi/shim -I. /tmp/t.c drivers/net/wifi/realtek/rtw88/chips/rtw8822c.c \
#            drivers/net/wifi/realtek/rtw88/chips/rtw8822c_tables.c /tmp/fw.o -o /tmp/t && /tmp/t
# usage: rtw8822c_gen.py rtw_sec.c rtw8821ce.c linux/rtw8822c_table.c rtw8822c_main.c out.c
sec, r21, ltab, main_c, out = sys.argv[1:6]
def fn(s, sig):
    i = s.index(sig); j = s.index('{', i); d = 0; k = j
    while True:
        c = s[k]
        if c == '{': d += 1
        elif c == '}':
            d -= 1
            if d == 0: return s[i:k + 1]
        k += 1
S = open(sec).read(); R = open(r21).read(); L = open(ltab).read()
def table(name):
    m = re.search(r'static const u32 rtw8822c_%s\[\] = \{(.*?)\};' % name, L, re.S)
    return [int(v, 16) for v in re.findall(r'0x[0-9A-Fa-f]+', m.group(1))]
def check_positive(c, cut, pkg, rfe):
    ccut, cpkg, cintf, crfe = (c >> 24) & 0xF, (c >> 12) & 0xF, (c >> 8) & 0xF, c & 0xFF
    if ccut and ccut != cut: return False
    if cpkg and cpkg != pkg: return False
    if cintf and cintf != 1: return False
    return crfe == rfe
def walk(t, cut, pkg, rfe):
    out = []; pos = 0; matched, skipped = True, False
    for i in range(0, len(t), 2):
        a, d = t[i], t[i + 1]
        if a & 0x80000000:
            br = (a >> 28) & 3
            if br == 3: matched, skipped = True, False
            elif br == 2: matched = not skipped
            else: pos = a
        elif a & 0x40000000:
            if not skipped:
                if check_positive(pos, cut, pkg, rfe): matched, skipped = True, True
                else: matched, skipped = False, False
            else: matched = False
        elif matched: out.append((a, d))
    return out
code = ['#include <stdio.h>', '#include <string.h>', '#include <stdlib.h>',
        '#include "include/nvx/base.h"', '#include "include/nvx/drivers/net/wifi/rtw_efuse.h"',
        '#define WHT 0', '#define GREEN 0', 'static u32 rgb(int r,int g,int b){ return (u32)((r<<16)|(g<<8)|b); }',
        'static void tcol(u32 c){ (void)c; } static void term_puts(const char* s){ (void)s; } static void term_putc(char c){ (void)c; }',
        'static char* scopy(char* p,const char* s){ while(*s)*p++=*s++; *p=0; return p; }',
        'static char* udec(char* p,u32 v){ char t[12]; int n=0; do{ t[n++]=(char)("0123456789"[v%10]); v/=10; }while(v); while(n)*p++=t[--n]; *p=0; return p; }',
        'static char* hx(char* p,u32 v,int dg){ for(int i=dg-1;i>=0;i--)*p++="0123456789ABCDEF"[(v>>(i*4))&15]; *p=0; return p; }',
        'u32 rtw_rdsz(u32 m,u32 o,u8 sz);', 'int g_rtw_rf_path;', 'int rf_write_a_calls;', 'void rtw_rf_write_a(u32 m,u32 addr,u32 data){ (void)m; (void)addr; (void)data; rf_write_a_calls++; }']
i = S.index('int g_rtw_cut='); code.append(S[i:S.index('\n', i)])
code.append(fn(R, 'int rtw_apply_one('))
code.append(fn(S, 'static int rtw_cond_ok('))
code.append(fn(S, 'int rtw_apply_cond('))
def emit(name, pairs):
    flat = ','.join('0x%X,0x%X' % p for p in pairs) or '0'
    code.append('static const u32 exp_%s[]={%s}; static const int exp_%s_n=%d;' % (name, flat, name, len(pairs)))
for cut in (3, 5):
    for rfe in (0, 5, 0x15):
        emit('bb_c%d_r%d' % (cut, rfe), walk(table('bb'), cut, 15, rfe))
        emit('agc_c%d_r%d' % (cut, rfe), walk(table('agc'), cut, 15, rfe))
        emit('cal_c%d_r%d' % (cut, rfe), walk(table('array_mp_cal_init'), cut, 15, rfe))
        emit('rfa_c%d_r%d' % (cut, rfe), walk(table('rf_a'), cut, 15, rfe))
        emit('rfb_c%d_r%d' % (cut, rfe), walk(table('rf_b'), cut, 15, rfe))
code.append('static void tables(int cut,int rfe,const u32* bb,int bbn,const u32* agc,int agcn,const u32* cal,int caln,const u32* ra,int ran,const u32* rb,int rbn); static void run_tables(void);')
calls = ['static void run_tables(void){']
for cut in (3, 5):
    for rfe in (0, 5, 0x15):
        t = '_c%d_r%d' % (cut, rfe)
        calls.append('    tables(%d,%d,%s);' % (cut, rfe, ','.join('exp_%s%s,exp_%s%s_n' % (n, t, n, t) for n in ('bb', 'agc', 'cal', 'rfa', 'rfb'))))
calls.append('}')
open(out, 'w').write('\n'.join(code) + '\n' + open(main_c).read() + '\n' + '\n'.join(calls) + '\n')
