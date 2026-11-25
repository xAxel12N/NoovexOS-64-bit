import sys, os, re
K = sys.argv[1]
def rd(p): return open(os.path.join(K, p)).read()
def strip_inc(s):
    return '\n'.join(l for l in s.splitlines() if not re.match(r'\s*#include\s', l))
hdr = ''.join(strip_inc(rd(h)) + '\n' for h in ['include/nvx/drivers/net/wifi/rtw_efuse.h', 'include/nvx/drivers/net/wifi/rtw8821ce.h', 'include/nvx/drivers/net/wifi/rtw_chip.h', 'include/nvx/drivers/net/wifi/rtw_usb.h', 'include/nvx/net/wifi/wifi_hw.h'])
pre = r'''#include <stdio.h>
#include <string.h>
#include <stdlib.h>
typedef unsigned char u8; typedef unsigned short u16; typedef unsigned u32; typedef unsigned long long u64; typedef signed char s8;
struct featguard { const char* name; const char* flag; int on, fails, maxfail, dead, trying; };
void fg_boot(struct featguard* g); int fg_begin(struct featguard* g); void fg_ok(struct featguard* g); void fg_fail(struct featguard* g,const char* why);
void klog(const char* s); char* scopy(char* p,const char* s); char* udec(char* p,u32 v); char* hx(char* p,u32 v,int dg);
'''
ef = rd('drivers/net/wifi/realtek/rtw88/rtw_efuse.c')
wr = '\n'.join(l for l in ef.splitlines() if re.match(r'(u8  rtw_r8 |u16 rtw_r16|u32 rtw_r32|void rtw_w8 |void rtw_w16|void rtw_w32)\(', l))
open(sys.argv[2], 'w').write(pre + hdr + wr + '\n' + strip_inc(rd('drivers/net/wifi/realtek/rtw88/rtw_usb.c')) + '\n' + open(sys.argv[3]).read())
