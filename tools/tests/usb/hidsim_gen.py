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
def between(s, a, b):
    i = s.find(a)
    if i < 0: raise SystemExit('missing ' + a)
    j = s.find(b, i)
    if j < 0: raise SystemExit('missing ' + b)
    return s[i:j + len(b)]
ty = [l for l in hdr.splitlines() if l.startswith(('typedef struct { u8 id, kind;', '#define HIDLAY_MAX', '#define XHID_MAX'))]
code = ('#include <stdio.h>\n#include <string.h>\n#include <stdlib.h>\n#include <stdint.h>\n#include <sys/mman.h>\n'
        'typedef uint8_t u8; typedef uint16_t u16; typedef uint32_t u32; typedef uint64_t u64;\n' + '\n'.join(ty) + '\n'
        '#define XHID_INFLIGHT 16\n'
        'int xhid_nlay[XHID_MAX], xhid_hasid[XHID_MAX], xhid_ccn[XHID_MAX], xhid_rep[XHID_MAX], xhid_iskbd[XHID_MAX], xhid_rlen[XHID_MAX], xhid_used[XHID_MAX], xhid_ctl[XHID_MAX], xhid_slot[XHID_MAX], xhid_dci[XHID_MAX], xhid_n, g_xcur;\n'
        'u16 xhid_ccp[XHID_MAX][16]; hidlay_t xhid_lay[XHID_MAX][HIDLAY_MAX]; u32 xhid_cnt[XHID_MAX], xhid_epin[XHID_MAX], xhid_dbuf[XHID_MAX]; u8 xhid_last[XHID_MAX][16]; int xhid_lastn[XHID_MAX];\n'
        'int usbk_rep_code, usbk_rep_dev=-1; u32 cur_dbuf;\n'
        'int mouse_dx, mouse_dy, mouse_evt, mouse_scroll, mouse_hscroll, g_btn, g_xbtn, g_kbd_calls;\n'
        'void in_btn_raw(int l,int r,int m){ g_btn=(l?1:0)|(r?2:0)|(m?4:0); }\n'
        'void in_xbtn(int a,int b){ g_xbtn=(a?1:0)|(b?2:0); }\n'
        'void in_rel(int dx,int dy){ if(!dx&&!dy)return; mouse_dx+=dx; mouse_dy-=dy; mouse_evt=1; }\n'
        'void in_scroll(int v,int h){ if(!v&&!h)return; mouse_scroll+=v; mouse_hscroll+=h; mouse_evt=1; }\n'
        'void hk_push(int h){ (void)h; }\n'
        'static u8 xhid_kprev[XHID_MAX][8], xhid_kprevx[XHID_MAX][HIDLAY_MAX-1][8];\n'
        'static int xhid_sim_e=-1; static u8 xhd_rd[6][512]; static u16 xhd_rdn[6], xhd_rdl[6]; static void xhid_diag_rx(int e,volatile u8* b,int got,int dx,int dy){ (void)e;(void)b;(void)got;(void)dx;(void)dy; }\n'
        'static void kbd_report(int e,u8* prev,volatile u8* d){ (void)e; (void)prev; (void)d; g_kbd_calls++; }\n'
        'static int usn_xfer(u8 sl,int ep,u8 cc,u32 res,u32 ptr){ (void)sl; (void)ep; (void)cc; (void)res; (void)ptr; return 0; } static int btu_xfer(u8 a2,int b2,u8 c2,u32 d2,u32 e2){ (void)a2;(void)b2;(void)c2;(void)d2;(void)e2; return 0; }\n'
        'static int g_armed; static void xhid_arm(int e){ (void)e; g_armed++; }\n'
        'static int g_recov; static void xhid_recover(int e){ (void)e; g_recov++; }\n'
        'static int xh_ctrl(u8 rt,u8 req,u16 val,u16 idx,u16 len,int in,u8* data);\n'
        'static int g_usbproto_off, xh_ctrl_to;\n')
code += re.search(r'(?m)^typedef struct \{ u8 iface, proto.*$', ci).group(0) + '\n'
code += re.search(r'(?m)^static int hid_inlen;.*$', ci).group(0) + '\n'
code += '\n'.join(fn(ci, n) for n in ['hid_parse', 'hid_bits', 'hid_pick']) + '\n'
code += '\n'.join(fn(km, n) for n in ['cc_hk', 'hid_report_decode', 'hid_xfer']) + '\n'
blk = between(ci, 'hidlay_t lays[6][HIDLAY_MAX];', 'if(!nh) XFAIL("no keyboard or mouse interface");')
rl = re.search(r'int rlen=.*?if\(rlen<1\)rlen=8; \}', ci, re.S).group(0)
code += ('typedef struct { int nh, mode[6], nlays[6], hasid[6], inl[6], rlen[6]; hidif_t hf[6]; hidlay_t lays[6][HIDLAY_MAX]; } simenum_t;\n'
         '#define XFAIL(m) return -1\n'
         'static int sim_enum(hidif_t* hf,int nh,simenum_t* S){\n' + blk + '\n'
         '    S->nh=nh; for(int j=0;j<nh;j++){ ' + rl + '\n'
         '        S->hf[j]=hf[j]; S->mode[j]=mode[j]; S->nlays[j]=nlays[j]; S->hasid[j]=hasid[j]; S->inl[j]=inl[j]; S->rlen[j]=rlen; for(int q=0;q<HIDLAY_MAX;q++)S->lays[j][q]=lays[j][q]; }\n'
         '    return nh; }\n#undef XFAIL\n')
open(sys.argv[4], 'w').write(code + '\n' + open(sys.argv[5]).read())
