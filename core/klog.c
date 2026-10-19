#include "include/nvx/base.h"
#include "include/nvx/core/klog.h"
#include "include/nvx/net/http/http.h"
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
void klog(const char* s){
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
