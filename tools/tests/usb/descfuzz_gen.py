import sys, re, os
K = sys.argv[1]
def rd(p): return open(os.path.join(K, p)).read()
def fn(s, name):
    m = re.search(r'(?m)^(?:static\s+)?[A-Za-z_][\w\s\*]*?\b' + name + r'\s*\([^;{]*\)\s*\{', s)
    if not m: raise SystemExit('missing ' + name)
    k = s.find('{', m.start()); d = 0
    for x in range(k, len(s)):
        if s[x] == '{': d += 1
        elif s[x] == '}':
            d -= 1
            if d == 0: return s[m.start():x + 1]
def line(s, prefix):
    i = s.index(prefix); return s[i:s.index('\n', i)]
ci = rd('drivers/usb/xhci/controller_init.c'); sf = rd('drivers/usb/xhci/storage_fat_and_hubs.c'); uw = rd('drivers/usb/xhci/uvc_webcam.c')
ir = rd('drivers/usb/xhci/uvc_ir.c'); um = rd('drivers/usb/xhci/usb_midi.c'); un = rd('drivers/usb/xhci/usb_network.c'); bt = rd('drivers/bluetooth/transport/btusb.c')
km = rd('drivers/usb/xhci/hid_keyboard_mouse.c'); hp = rd('drivers/usb/xhci/logitech_hidpp.c'); hdr = rd('include/nvx/drivers/usb/xhci.h')
o = ['#include <stdio.h>\n#include <stdlib.h>\n#include <string.h>\n#include <stdint.h>\ntypedef uint8_t u8; typedef uint16_t u16; typedef uint32_t u32; typedef uint64_t u64;']
o += [l for l in hdr.splitlines() if l.startswith(('typedef struct { u8 id, kind;', '#define HIDLAY_MAX', '#define USBDEV_MAX', 'typedef struct { int used, ctl, port, cam'))]
o.append(line(ci, 'typedef struct { int root, depth, hub, hport, speed;'))
o.append(line(ci, 'typedef struct { u8 iface, proto, sub, ep;'))
o.append('static int g_xcur=0, usb_seen_init=1; static u64 g_ticks=0; static int g_notes=0;\nstatic char* scopy(char* p,const char* s){ while(*s)*p++=*s++; *p=0; return p; }\nvoid notify(const char* a,const char* b){ (void)a; (void)b; g_notes++; }\nstatic void klog(const char* s){ (void)s; }')
o.append(line(ci, 'static int hid_inlen;'))
o += [fn(ci, 'hid_parse'), fn(ci, 'hid_pick')]
o.append(line(sf, 'typedef struct { u8 alt, ep, intv, burst, mult; u16 mps; u32 esit; } uvc_alt_t;'))
o.append(line(sf, 'typedef struct { u8 kind, fi, ri; u16 w, h; u32 defint, bestint; } uvc_fr_t;'))
o.append(line(sf, 'static struct { int ok, ctl, reg, slot, speed, root, vc_if, vs_if'))
o.append(fn(sf, 'uvc_r32')); o.append(line(sf, '#define UVC_WANTINT'))
o += [fn(sf, n) for n in ('uvc_has_video', 'uvc_fourcc', 'uvc_has_color')]
o.append('static void uvc_parse(const u8* c,int n,int target);')
o.append('static void uvx_parse(const u8* c,int n,int vs){ (void)c; (void)n; (void)vs; }')
o += [fn(sf, n) for n in ('uvc_choose', 'uvc_pickint', 'uvc_parse')]
o.append(line(sf, 'usbdev_t g_usbdev[USBDEV_MAX];'))
o += [fn(sf, 'usb_cls_name'), fn(sf, 'usbdev_note')]
o.append(line(uw, 'typedef struct { u8 ifn, alt, ep, intv, ch, sub, bits, nrate, cont, v2; u16 mps; u32 rate[8]; } uac_alt_t;'))
o.append(line(uw, 'static struct { int ok, ctl, reg, slot, speed, root, ac_if, nalt'))
o += [fn(uw, n) for n in ('aud_has_audio', 'aud_parse', 'aud_pick_rate', 'aud_pick')]
o.append(line(ir, 'typedef struct { u8 unit, sel; } uvc_xuc_t;')); o.append(fn(ir, 'uvc_xu_list'))
o.append(fn(um, 'umidi_has'))
o.append(rd('drivers/usb/xhci/usbmidi_codec.h'))
o.append(rd('drivers/usb/xhci/xhci_robust.c'))
o.append(line(un, 'typedef struct { int kind, cfgval, ctlif, dataif'))
o.append(line(un, 'int g_usn_pref=0;')); o.append(fn(un, 'usn_parse'))
o.append('static int g_qn=0; static void usn_q_put(const u8* f,int n){ volatile u8 x=0; for(int i=0;i<n;i++)x^=f[i]; (void)x; g_qn++; }')
o.append(rd('drivers/usb/usbnet_proto.h'))
o.append('static int btu_vclass(u16 vid,u16 pid){ return (vid^pid)&1; }')
i = bt.index('static struct { int ok, ctl, slot, iface'); o.append(bt[i:bt.index('} btu;', i) + 6])
o.append(line(bt, 'static struct { int n, ifn, on, stp, alt, in_dci'))
o += [fn(bt, 'btu_parse'), fn(bt, 'btu_parse_iso')]
o.append(fn(km, 'msd_cfg_eps')); o.append(fn(hp, 'hpp_vendor_iface'))
open(sys.argv[2], 'w').write('\n'.join(o) + '\n' + open(sys.argv[3]).read())
