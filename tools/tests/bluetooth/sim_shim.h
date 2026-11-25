#include <stdint.h>
#include <string.h>
#define NVX_BASE_H
#define NVX_CORE_KLOG_H
#define NVX_LIB_FMT_H
#define NVX_ARCH_X86_64_HRTIMER_H
#define NVX_NET_WIFI_WIFI_MGR_H
#define NVX_DRIVERS_WIFI_RTW8821CE_H
#define NVX_RTLBT_FW_H
#define NVX_INTELBT_FW_H
#define NVX_BCMBT_FW_H
#define NVX_ARCH_X86_64_PERCPU_H
typedef uint8_t u8; typedef uint16_t u16; typedef uint32_t u32; typedef uint64_t u64; typedef int64_t i64;
void klog(const char* s); void klog2(const char* a,const char* b);
char* scopy(char* p,const char* s); char* udec(char* p,u32 v); char* hx(char* p,u32 v,int dg); int slen(const char* s);
u64 hr_ns(void); void hr_delay_us(u32 us);
extern int g_rtw_coex_done, g_wm_state; enum { WM_CONNECTED=5 };
int rtw_coex_bt_share(u32 m,int on,int conn);
int rtlbt_setup(int (*cmd)(u16,const u8*,int,u8*,int),char* m,int cap);
int intelbt_match(u16 v,u16 p); int intelbt_setup(char* m,int cap);
int bcmbt_match(u16 v,u16 m); int bcmbt_setup(int (*cmd)(u16,const u8*,int,u8*,int),u16 v,u16 p,char* m,int cap);
