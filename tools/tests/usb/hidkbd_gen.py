import sys
# Builds a host test of the USB keyboard decode: the real hid_parse / hid_report_decode / kbd_report and
# helpers are cut out of the xHCI driver and fed real report descriptors and reports (hidkbd_main.c).
ci=open(sys.argv[1]).read(); hk=open(sys.argv[2]).read(); hdr=open(sys.argv[3]).read()
def fn(s,sig):
    i=s.index(sig); j=s.index('{',i); d=0; k=j
    while True:
        c=s[k]
        if c=='{': d+=1
        elif c=='}':
            d-=1
            if d==0: return s[i:k+1]
        k+=1
code='#include "hidkbd_env.h"\n'
code+='\n'.join(l for l in hdr.splitlines() if l.startswith('typedef struct { u8 id, kind;') or l.startswith('#define HIDLAY_MAX'))+'\n'
code+='#define XHID_MAX 16\nhidlay_t xhid_lay[XHID_MAX][HIDLAY_MAX]; int xhid_nlay[XHID_MAX], xhid_hasid[XHID_MAX]; int xhid_n=2, xhid_used[XHID_MAX]={1,1};\n'
i=ci.index('static u8 xhid_kprev['); code+=ci[i:ci.index('\n',i)]+'\n'
code+='static int hid_inlen;\n'+fn(ci,'static int hid_parse(')+'\n'+fn(ci,'static int hid_bits(')+'\n'
code+=fn(hk,'static char hid2ascii(')+'\n'+fn(hk,'static u8 hid2scan(')+'\n'
i=hk.index('static int usbk_rep_code'); code+=hk[i:hk.index('\n',i)]+'\n'
code+='static int hpp_wheel(int e,int wz){ (void)e; return wz; }\n'
for s in ['static int usb_key_code(','static void usbk_mods_sync(','static void kbd_report(','static void kbd_nkro(','static void usb_kbd_repeat(','static int cc_hk(','static void hid_report_decode(']: code+=fn(hk,s)+'\n'
open(sys.argv[4],'w').write(code+'\n#include "hidkbd_main.c"\n')
