#include "include/nvx/base.h"
#include "include/nvx/core/klog.h"
#include "include/nvx/net/http/http.h"
#include "include/nvx/mem/heap.h"
static int g_klog_ok=0;
void (*g_klog_hook)(const char*)=0;
void klog_init(void){
    outb(0x3F8+7,0xA5); if(inb(0x3F8+7)!=0xA5){ g_klog_ok=0; return; }
    outb(0x3F8+1,0x00); outb(0x3F8+3,0x80); outb(0x3F8+0,0x01); outb(0x3F8+1,0x00);
    outb(0x3F8+3,0x03); outb(0x3F8+2,0xC7); outb(0x3F8+4,0x03);
    g_klog_ok=1;
}
static void klog_c(char c){
    if(!g_klog_ok)return;
    for(int i=0;i<100000;i++){ if(inb(0x3F8+5)&0x20){ outb(0x3F8,(u8)c); return; } }
    g_klog_ok=0;
}
static char* g_tl; static u32 g_tl_n, g_tl_cap; int g_tl_on;
static volatile int g_tl_busy;
static void tl_add2(const char* s){
    if(!g_tl)return; char t[24]; int n=0; u64 v=g_ticks; t[n++]='['; char d[22]; int k=0; if(!v)d[k++]='0'; while(v){ d[k++]=(char)('0'+v%10); v/=10; } while(k)t[n++]=d[--k]; t[n++]=']'; t[n++]=' ';
    u32 L=0; while(s[L])L++; u32 need=(u32)n+L+1; if(need>g_tl_cap/4)return;
    if(g_tl_n+need>g_tl_cap){ u32 keep=g_tl_cap/2, from=g_tl_n-keep; while(from<g_tl_n&&g_tl[from]!='\n')from++; if(from<g_tl_n)from++; u32 m=g_tl_n-from; for(u32 i=0;i<m;i++)g_tl[i]=g_tl[from+i]; g_tl_n=m; }
    for(int i=0;i<n;i++)g_tl[g_tl_n++]=t[i]; for(u32 i=0;i<L;i++)g_tl[g_tl_n++]=s[i]; g_tl[g_tl_n++]='\n';
}
static void tl_add(const char* s){ if(__atomic_exchange_n(&g_tl_busy,1,__ATOMIC_ACQUIRE))return; tl_add2(s); __atomic_store_n(&g_tl_busy,0,__ATOMIC_RELEASE); }
int tracelog_start(void){ if(!g_tl){ g_tl_cap=4u<<20; g_tl=(char*)kmalloc(g_tl_cap); if(!g_tl){ g_tl_cap=0; return -1; } } g_tl_n=0; g_tl_on=1; return 0; }
void tracelog_stop(void){ g_tl_on=0; }
const char* tracelog_data(u32* n){ *n=g_tl_n; return g_tl; }
static char g_kr[16384]; static u32 g_kr_w, g_kr_n, g_kr_rd;
static void kr_c(char c){ g_kr[g_kr_w]=c; g_kr_w=(g_kr_w+1)&16383u; if(g_kr_n<16384u)g_kr_n++; if(g_kr_rd<16384u)g_kr_rd++; }
static void kr_line(const char* s){ const char* pf="<6>["; while(*pf)kr_c(*pf++); char t[24]; int n=0; u64 v=g_ticks/100; if(!v)t[n++]='0'; while(v){ t[n++]=(char)('0'+v%10); v/=10; } while(n)kr_c(t[--n]); kr_c('.'); u64 cs=g_ticks%100; kr_c((char)('0'+cs/10)); kr_c((char)('0'+cs%10)); kr_c(']'); kr_c(' '); for(int i=0;s[i]&&i<400;i++)kr_c(s[i]); kr_c('\n'); }
int klog_ring(char* out,int cap,int mode){
    if(mode==2){ g_kr_n=0; g_kr_rd=0; return 0; }
    if(!out)return (int)(mode==5?g_kr_rd:g_kr_n);
    if(mode==4){ u32 n=g_kr_rd; if((u32)cap<n)n=(u32)cap; u32 st=(g_kr_w-g_kr_rd)&16383u; for(u32 i=0;i<n;i++)out[i]=g_kr[(st+i)&16383u]; g_kr_rd-=n; return (int)n; }
    u32 n=g_kr_n; if((u32)cap<n)n=(u32)cap; u32 st=(g_kr_w-n)&16383u;
    for(u32 i=0;i<n;i++)out[i]=g_kr[(st+i)&16383u];
    if(mode==3){ g_kr_n=0; g_kr_rd=0; }
    return (int)n;
}
void klog(const char* s){
    if(g_tl_on)tl_add(s);
    kr_line(s);
    { extern void crash_logline(const char*) __attribute__((weak)); if(crash_logline)crash_logline(s); }
    { extern void sysfiles_logline(const char*) __attribute__((weak)); if(sysfiles_logline)sysfiles_logline(s); }
    if(g_klog_hook)g_klog_hook(s);
    if(!g_klog_ok)return;
    char t[24]; int n=0; u64 v=g_ticks; if(!v)t[n++]='0'; while(v){t[n++]=(char)('0'+v%10);v/=10;}
    klog_c('['); while(n)klog_c(t[--n]); klog_c(']'); klog_c(' ');
    while(*s)klog_c(*s++); klog_c('\r'); klog_c('\n');
}
void klog2(const char* a,const char* b){
    if(!g_klog_ok&&!g_klog_hook)return; char buf[160]; int k=0;
    while(*a&&k<79)buf[k++]=*a++; while(b&&*b&&k<159)buf[k++]=*b++; buf[k]=0; klog(buf);
}
