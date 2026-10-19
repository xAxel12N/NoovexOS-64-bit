#include "include/nvx/base.h"
#include "include/nvx/mem/dmawin.h"
#include "include/nvx/core/klog.h"
#include "include/nvx/lib/base/fmt.h"
#include "include/nvx/arch/x86_64/cpu/cpuinfo.h"
#include "include/nvx/drivers/bus/pci.h"
#include "include/nvx/drivers/input/input.h"
#include "include/nvx/drivers/video/fb/framebuffer.h"
#include "include/nvx/net/http/http.h"

extern int mx, my;
extern volatile int mouse_rbtn, mouse_mid;

#define VMW_MAGIC 0x564D5868u
#define VMW_PORT 0x5658u
#define VMM_GETVERSION 10u
#define VMM_DATA 39u
#define VMM_STATUS 40u
#define VMM_COMMAND 41u
#define VMM_READ_ID 0x45414552u
#define VMM_DISABLE 0x000000F5u
#define VMM_REQ_ABS 0x53424152u

int g_vm_kind;
const char* g_vm_name="none";
int g_vmm_mode;
u32 g_vmm_events, g_vmm_errors, g_vmm_polls;
static u16 g_vbox_io;
#define g_vbox_req ((u8*)(u64)DMW(0x1A1000u))
static int g_vm_lx=-1, g_vm_ly=-1, g_vm_lb=-1;

static void vmw_call(u32 cmd,u32 arg,u32* a,u32* b,u32* c,u32* d){
    u32 ra=VMW_MAGIC, rb=arg, rc=cmd, rd=VMW_PORT, rsi=0, rdi=0;
    cpu_vmw_backdoor(&ra,&rb,&rc,&rd,&rsi,&rdi);
    *a=ra; *b=rb; *c=rc; *d=rd; }
static int vm_hv(char* sig){
    u32 a,b,c,d; cpuid(1,&a,&b,&c,&d); if(!(c&(1u<<31)))return 0;
    cpuid(0x40000000u,&a,&b,&c,&d); *(u32*)(sig)=b; *(u32*)(sig+4)=c; *(u32*)(sig+8)=d; sig[12]=0; return 1; }
static int sig_is(const char* s,const char* t){ for(int i=0;i<12;i++){ if(s[i]!=t[i])return 0; if(!t[i])return 1; } return 1; }

static int vmw_init(void){
    u32 a,b,c,d; vmw_call(VMM_GETVERSION,0,&a,&b,&c,&d); if(b!=VMW_MAGIC||a==0xFFFFFFFFu)return 0;
    vmw_call(VMM_COMMAND,VMM_READ_ID,&a,&b,&c,&d);
    vmw_call(VMM_STATUS,0,&a,&b,&c,&d); if((a&0xFFFF0000u)==0xFFFF0000u||(a&0xFFFFu)==0)return 0;
    vmw_call(VMM_DATA,1,&a,&b,&c,&d); if(a!=0x3442554Au)return 0;
    vmw_call(VMM_COMMAND,VMM_REQ_ABS,&a,&b,&c,&d);
    return 1; }

static void vm_apply(u32 x,u32 y,int l,int r,int m,int wheel){
    if(x>0xFFFFu)x=0xFFFFu; if(y>0xFFFFu)y=0xFFFFu;
    int tx=(int)((u64)x*W/65536u), ty=(int)((u64)y*H/65536u);
    if(tx!=g_vm_lx||ty!=g_vm_ly){ u64 f=cpu_irq_save(); int cx=mx+mouse_dx, cy=my-mouse_dy; mouse_dx+=tx-cx; mouse_dy-=ty-cy; mouse_evt=1; if(f&0x200)cpu_irq_on(); g_vm_lx=tx; g_vm_ly=ty; g_vmm_events++; }
    int bb=(l?1:0)|(r?2:0)|(m?4:0); if(bb!=g_vm_lb&&l>=0){ in_buttons(l,r,m); g_vm_lb=bb; }
    if(wheel)in_scroll(wheel,0); }

static void vmw_poll(void){
    for(int guard=0;guard<16;guard++){
        u32 a,b,c,d; vmw_call(VMM_STATUS,0,&a,&b,&c,&d);
        if((a&0xFFFF0000u)==0xFFFF0000u){ g_vmm_errors++; vmw_call(VMM_COMMAND,VMM_DISABLE,&a,&b,&c,&d); vmw_call(VMM_COMMAND,VMM_READ_ID,&a,&b,&c,&d); vmw_call(VMM_COMMAND,VMM_REQ_ABS,&a,&b,&c,&d); return; }
        if((a&0xFFFFu)<4)return;
        vmw_call(VMM_DATA,4,&a,&b,&c,&d);
        int z=(int)(signed char)(u8)d;
        vm_apply(b,c,(a&0x20)!=0,(a&0x10)!=0,(a&0x08)!=0,z); } }

static u16 vbox_find(void){
    for(int bus=0;bus<8;bus++) for(int dev=0;dev<32;dev++){ u32 id=pci_cfg(bus,dev,0,0); if(id!=0xCAFE80EEu)continue;
        u32 b0=pci_cfg(bus,dev,0,0x10); if(!(b0&1))return 0; u32 cmd=pci_cfg(bus,dev,0,4); pci_cfgw(bus,dev,0,4,(cmd&0xFFFFu)|0x1u); return (u16)(b0&0xFFFCu); }
    return 0; }
static int vbox_req(u32 type,u32 size){
    u32* h=(u32*)g_vbox_req; h[0]=size; h[1]=0x10001u; h[2]=type; h[3]=(u32)-1; h[4]=0; h[5]=0;
    u64 pa=(u64)g_vbox_req; if(pa>=0x100000000ULL)return -1;
    outl((u16)g_vbox_io,(u32)pa);
    return (int)h[3]; }
static int vbox_init(void){
    g_vbox_io=vbox_find(); if(!g_vbox_io)return 0;
    u32* h=(u32*)g_vbox_req;
    for(int i=0;i<16;i++)h[i]=0; h[6]=0x00010004u; h[7]=0x00053100u; if(vbox_req(50,32)<0){ klog("vmmouse: VirtualBox guest info rejected"); }
    for(int i=0;i<16;i++)h[i]=0; h[6]=0x1u|0x10u; if(vbox_req(2,36)<0){ klog("vmmouse: VirtualBox refused absolute mouse"); return 0; }
    return 1; }
static void vbox_poll(void){
    u32* h=(u32*)g_vbox_req; for(int i=0;i<16;i++)h[i]=0;
    if(vbox_req(1,36)<0){ g_vmm_errors++; return; }
    u32 feat=h[6]; if(!(feat&0x2u)){ g_vm_lx=-1; g_vm_ly=-1; return; }
    vm_apply(h[7],h[8],-1,0,0,0); }

void vm_mouse_init(void){
    char sig[13]; sig[0]=0; g_vmm_mode=0;
    if(!vm_hv(sig)){ g_vm_kind=0; g_vm_name="none (physical machine)"; return; }
    if(sig_is(sig,"VBoxVBoxVBox")){ g_vm_kind=1; g_vm_name="VirtualBox"; }
    else if(sig_is(sig,"VMwareVMware")){ g_vm_kind=2; g_vm_name="VMware"; }
    else if(sig_is(sig,"KVMKVMKVM")||sig_is(sig,"TCGTCGTCGTCG")){ g_vm_kind=3; g_vm_name="QEMU/KVM"; }
    else if(sig_is(sig,"Microsoft Hv")){ g_vm_kind=4; g_vm_name="Hyper-V"; }
    else { g_vm_kind=5; g_vm_name="other hypervisor"; }
    if(vbox_find()){ if(g_vm_kind!=1){ g_vm_kind=1; g_vm_name="VirtualBox"; } if(vbox_init()){ g_vmm_mode=2; klog("vmmouse: VirtualBox mouse integration on"); return; } }
    if(g_vm_kind==2||g_vm_kind==3||g_vm_kind==5){ if(vmw_init()){ g_vmm_mode=1; klog("vmmouse: VMware/QEMU absolute pointer on"); return; } }
    klog("vmmouse: no absolute pointer device"); }
void vm_mouse_poll(void){
    static u64 last; extern volatile u64 g_ticks; if(!g_vmm_mode)return; if(g_ticks==last)return; last=g_ticks;
    g_vmm_polls++; if(g_vmm_mode==1)vmw_poll(); else vbox_poll(); }
void vminfo_run(void){
    extern void term_puts(const char*); char b[160],*p=b;
    p=scopy(p,"virtual machine: "); p=scopy(p,g_vm_name); p=scopy(p,"\nmouse integration: "); p=scopy(p,g_vmm_mode==2?"VirtualBox (VMMDev)":g_vmm_mode==1?"VMware backdoor (vmmouse)":"off");
    p=scopy(p,"  updates "); p=udec(p,g_vmm_events); p=scopy(p,"  errors "); p=udec(p,g_vmm_errors); p=scopy(p,"  polls "); p=udec(p,g_vmm_polls); p=scopy(p,"\n"); *p=0; term_puts(b); }
