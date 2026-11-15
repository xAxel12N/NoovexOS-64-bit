import sys, re
ci = open(sys.argv[1]).read(); km = open(sys.argv[2]).read(); hdr = open(sys.argv[3]).read()
def fn(s, name):
    m = re.search(r'(?m)^(?:static\s+)?[A-Za-z_][\w\s\*]*?\b' + name + r'\s*\([^;{]*\)\s*\{', s)
    if not m: raise SystemExit('missing ' + name)
    k = s.find('{', m.start()); d = 0
    for x in range(k, len(s)):
        if s[x] == '{': d += 1
        elif s[x] == '}':
            d -= 1
            if d == 0: return s[m.start():x + 1]
ty = [l for l in hdr.splitlines() if l.startswith(('typedef struct { u8 id, kind;', '#define HIDLAY_MAX', '#define XHID_MAX'))]
code = ('#include <stdio.h>\n#include <string.h>\n#include <stdlib.h>\n#include <stdint.h>\n'
        'typedef uint8_t u8; typedef uint16_t u16; typedef uint32_t u32; typedef uint64_t u64;\n' + '\n'.join(ty) + '\n'
        'int xhid_nlay[XHID_MAX], xhid_hasid[XHID_MAX], xhid_ccn[XHID_MAX]; u16 xhid_ccp[XHID_MAX][16]; hidlay_t xhid_lay[XHID_MAX][HIDLAY_MAX];\n'
        'int mouse_dx, mouse_dy, mouse_evt, mouse_scroll, mouse_hscroll, g_btn, g_xbtn, g_kbd_calls;\n'
        'void in_btn_raw(int l,int r,int m){ g_btn=(l?1:0)|(r?2:0)|(m?4:0); }\n'
        'void in_xbtn(int a,int b){ g_xbtn=(a?1:0)|(b?2:0); }\n'
        'void in_rel(int dx,int dy){ if(!dx&&!dy)return; mouse_dx+=dx; mouse_dy-=dy; mouse_evt=1; }\n'
        'void in_scroll(int v,int h){ if(!v&&!h)return; mouse_scroll+=v; mouse_hscroll+=h; mouse_evt=1; }\n'
        'void hk_push(int h){ (void)h; }\n'
        'static u8 xhid_kprev[XHID_MAX][8], xhid_kprevx[XHID_MAX][HIDLAY_MAX-1][8];\n'
        'static int xhid_sim_e=-1; static u8 xhd_rd[6][512]; static u16 xhd_rdn[6], xhd_rdl[6]; static void xhid_diag_rx(int e,volatile u8* b,int got,int dx,int dy){ (void)e;(void)b;(void)got;(void)dx;(void)dy; }\n'
        'static void kbd_report(int e,u8* prev,volatile u8* d){ (void)e; (void)prev; (void)d; g_kbd_calls++; }\n')
names = ['hid_parse', 'hid_bits']
code += re.search(r'(?m)^static int hid_inlen;.*$', ci).group(0) + '\n'
code += '\n'.join(fn(ci, n) for n in names) + '\n'
km_names = ['cc_hk'] + [n for n in ('hid_mouse_boot',) if re.search(r'\b' + n + r'\s*\(', km)] + ['hid_report_decode']
code += '\n'.join(fn(km, n) for n in km_names) + '\n'
code += ('#define XHID_INFLIGHT 16\n#include <sys/mman.h>\n'
         'int xhid_n, g_xcur, xhid_used[XHID_MAX], xhid_ctl[XHID_MAX], xhid_slot[XHID_MAX], xhid_dci[XHID_MAX], xhid_rlen[XHID_MAX], xhid_rep[XHID_MAX], xhid_iskbd[XHID_MAX], xhid_lastn[XHID_MAX], g_db_rings;\n'
         'u32 xhid_cnt[XHID_MAX], xhid_epin[XHID_MAX], xhid_ein_enq[XHID_MAX], xhid_ein_cyc[XHID_MAX], xhid_dbuf[XHID_MAX]; u8 xhid_last[XHID_MAX][16]; static u8 xhid_bs[64];\n'
         'static int usbk_rep_dev=-1, usbk_rep_code; static int usn_xfer(u8 a,int b,u8 c,u32 d,u32 e){ (void)a;(void)b;(void)c;(void)d;(void)e; return 0; } static int btu_xfer(u8 a2,int b2,u8 c2,u32 d2,u32 e2){ (void)a2;(void)b2;(void)c2;(void)d2;(void)e2; return 0; }\n'
         'static void xh_db(int s,u32 t){ (void)s;(void)t; g_db_rings++; }\n')
code += fn(ci, 'xhid_arm') + '\nstatic int g_recov; static void xhid_recover(int e){ g_recov++; xhid_arm(e); }\n' + fn(km, 'hid_xfer') + '\n'
open(sys.argv[4], 'w').write(code + '\n' + open(sys.argv[5]).read())
