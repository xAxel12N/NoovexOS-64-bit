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
        'int g_absx=-1, g_absy=-1, g_absxr, g_absyr, g_absn; void in_abs(int x,int xr,int y,int yr){ g_absx=x; g_absy=y; g_absxr=xr; g_absyr=yr; g_absn++; }\n'
        'void in_scroll(int v,int h){ if(!v&&!h)return; mouse_scroll+=v; mouse_hscroll+=h; mouse_evt=1; }\n'
        'u32 g_btnmask; int g_hrv, g_hrh, g_hrn, g_hrsum; void in_btnmask(u32 m){ g_btnmask=m; }\n'
        'void in_scroll_hr(int v,int h){ g_hrn++; g_hrsum+=v; g_hrv+=v; g_hrh+=h; int a=g_hrv/120, b=g_hrh/120; g_hrv-=a*120; g_hrh-=b*120; in_scroll(a,b); }\n'
        'void hk_push(int h){ (void)h; }\n'
        'static u8 xhid_kprev[XHID_MAX][8], xhid_kprevx[XHID_MAX][HIDLAY_MAX-1][8];\n'
        'static int xhid_sim_e=-1; static u8 xhd_rd[6][512]; static u16 xhd_rdn[6], xhd_rdl[6]; static void xhid_diag_rx(int e,volatile u8* b,int got,int dx,int dy){ (void)e;(void)b;(void)got;(void)dx;(void)dy; }\n'
        'static void kbd_report(int e,u8* prev,volatile u8* d){ (void)e; (void)prev; (void)d; g_kbd_calls++; }\n'
        'static void kbd_nkro(int e,int q,u8* prev,u8 mod,const u8* bm){ (void)e; (void)q; (void)prev; (void)mod; (void)bm; g_kbd_calls++; }\n'
        'static int usn_xfer(u8 sl,int ep,u8 cc,u32 res,u32 ptr){ (void)sl; (void)ep; (void)cc; (void)res; (void)ptr; return 0; } static int btu_xfer(u8 a2,int b2,u8 c2,u32 d2,u32 e2){ (void)a2;(void)b2;(void)c2;(void)d2;(void)e2; return 0; } static int um_xfer(u8 a3,int b3,u8 c3,u32 d3,u32 e3){ (void)a3;(void)b3;(void)c3;(void)d3;(void)e3; return 0; } static int gpu_xfer(u8 a4,int b4,u8 c4,u32 d4,u32 e4){ (void)a4;(void)b4;(void)c4;(void)d4;(void)e4; return 0; } static int gpu_active(void){ return 0; }\n static int us_xfer(u8 a4,int b4,u8 c4,u32 d4,u32 e4){ (void)a4;(void)b4;(void)c4;(void)d4;(void)e4; return 0; }\n'
        'static int g_armed; static void xhid_arm(int e){ (void)e; g_armed++; }\n'
        'static int g_recov; static void xhid_recover(int e){ (void)e; g_recov++; }\n'
        'static int xh_ctrl(u8 rt,u8 req,u16 val,u16 idx,u16 len,int in,u8* data);\n'
        'static int g_usbproto_off, xh_ctrl_to;\n')
code += re.search(r'(?m)^typedef struct \{ u8 iface, proto.*$', ci).group(0) + '\n'
code += re.search(r'(?m)^static int hid_inlen;.*$', ci).group(0) + '\n'
code += '\n'.join(fn(ci, n) for n in ['hid_parse', 'hid_bits', 'hid_pick']) + '\n'
code += 'int dg_xfer(int e,const u8* b,int n){ (void)e; (void)b; (void)n; return 0; } int dg_probe(int k,u16 v,u16 p,const u8* r,int n,u8* f,int* fl,int* fr){ (void)k; (void)v; (void)p; (void)r; (void)n; (void)f; *fl=0; *fr=0; return 0; } void dg_bind(int k,int e){ (void)k; (void)e; } static int hpp_xfer(int e,const u8* b,int n){ (void)e; (void)b; (void)n; return 0; } static int hpp_wheel(int e,int wz){ (void)e; return wz; } static void hppx_poll(void){} static int hpp_receiver_pid(u16 p){ (void)p; return 0; } static int hpp_vendor_iface(u16 v,const u8* r,int n){ (void)v; (void)r; (void)n; return 0; } static void hppx_attach(int e,int o,u16 v,u16 p,u8 i,int r){ (void)e; (void)o; (void)v; (void)p; (void)i; (void)r; } void hidpp_service(void){} static u16 cvid, cpid;\n'
import os
code += open(os.path.join(os.path.dirname(sys.argv[1]), 'xhci_robust.c')).read() + '\n' + 'static void xrc_hid_note(int e,u8 cc){ (void)e; (void)cc; } static void xrc_hid_cleared(int e){ (void)e; }\n'
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
