#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <sys/mman.h>
#include "include/kapi.h"
#include "include/drivers.h"

static int g_fail, g_pass;
static void rec(int ok,const char* name,const char* det){ if(ok)g_pass++; else g_fail++; printf("  %-66s %s%s%s\n",name,ok?"ok":"FAIL",(!ok&&det)?"   ":"",(!ok&&det)?det:""); }

#define ARENA ((u8*)0x20000000)
#define ARENA_SZ (64u<<20)
static u64 arena_used;
void* kapi_dma_pages(unsigned long pages){ u64 n=pages*4096; if(arena_used+n>ARENA_SZ)return 0; void* p=ARENA+arena_used; memset(p,0,n); arena_used+=n; return p; }
u64 kapi_phys(const void* p){ return (u64)p; }
static u8* P(u64 a){ if(a<(u64)ARENA||a>=(u64)ARENA+ARENA_SZ){ fprintf(stderr,"DMA outside arena %llx\n",(unsigned long long)a); exit(3); } return (u8*)a; }
void kapi_log(const char* s){ if(getenv("SIM_LOG"))printf("    log: %s\n",s); }
static u64 g_now; static void (*g_tick)(void);
void kapi_delay_us(u32 us){ g_now+=(u64)us*1000; if(g_tick)g_tick(); }
u64 kapi_ns(void){ g_now+=1000; if(g_tick)g_tick(); return g_now; }
void kapi_mb(void){ __sync_synchronize(); if(g_tick)g_tick(); }
static u64 g_hi_pa; static u8* g_hi_va;
void* kapi_mmio(u64 pa,u64 len){ (void)len; if(pa>=0x100000000ULL)return (g_hi_pa&&pa==g_hi_pa)?(void*)g_hi_va:0; return (void*)pa; }

struct pdev { int present; u16 ven, dev; u8 cls, sub; u32 bar[6]; u32 cmd; };
static struct pdev g_pci, g_pcix;
static struct pdev* pdev_at(int d){ return d==8?&g_pcix:&g_pci; }
u32 kapi_pci_rd32(int b,int d,int f,int off){ (void)b;(void)f; struct pdev* p=pdev_at(d); if(!p->present)return 0xFFFFFFFFu;
    switch(off){ case 0: return p->ven|((u32)p->dev<<16); case 4: return p->cmd; case 8: return ((u32)p->cls<<24)|((u32)p->sub<<16); case 0x0C: return 0; }
    if(off>=0x10&&off<0x28)return p->bar[(off-0x10)/4]; return 0; }
void kapi_pci_wr32(int b,int d,int f,int off,u32 v){ (void)b;(void)f; if(off==4)pdev_at(d)->cmd=v; }
int kapi_pci_find(u16 ven,const u16* devs,int nth,int* b,int* d,int* f){ if(!g_pci.present||g_pci.ven!=ven)return 0; for(int i=0;devs[i];i++) if(devs[i]==g_pci.dev){ if(nth)return 0; *b=0;*d=3;*f=0; return 1; } return 0; }
int kapi_pci_find_class(int c,int s,int nth,int* b,int* d,int* f){ int ds[2]={4,8}; for(int i=0;i<2;i++){ struct pdev* p=pdev_at(ds[i]); if(!p->present||p->cls!=c||p->sub!=s)continue; if(nth--)continue; *b=0;*d=ds[i];*f=0; return 1; } return 0; }
void kapi_pci_enable(int b,int d,int f){ (void)b;(void)f; pdev_at(d)->cmd|=7; }

static u8 (*io_in8)(u16); static void (*io_out8)(u16,u8);
static u16 (*io_in16)(u16); static void (*io_out16)(u16,u16);
static u32 (*io_in32)(u16); static void (*io_out32)(u16,u32);
u8 kapi_inb(u16 p){ return io_in8?io_in8(p):0xFF; } u16 kapi_inw(u16 p){ return io_in16?io_in16(p):0xFFFF; } u32 kapi_inl(u16 p){ return io_in32?io_in32(p):0xFFFFFFFFu; }
void kapi_outb(u16 p,u8 v){ if(io_out8)io_out8(p,v); } void kapi_outw(u16 p,u16 v){ if(io_out16)io_out16(p,v); } void kapi_outl(u16 p,u32 v){ if(io_out32)io_out32(p,v); }

static u8 wire[256][2048]; static int wire_len[256], wire_n;
static void wire_reset(void){ wire_n=0; }
static void wire_put(const u8* d,int n){ if(wire_n<256){ memcpy(wire[wire_n],d,(size_t)n); wire_len[wire_n]=n; wire_n++; } }
static unsigned rs=12345; static unsigned rnd(void){ rs=rs*1103515245u+12345u; return rs>>8; }
static void mkpkt(u8* p,int n,int seed){ for(int i=0;i<n;i++)p[i]=(u8)(seed*31+i*7); }

#define IOB 0xC000
static u8 r39[256]; static u8* r39_ring; static u32 r39_cbr; static int r39_rx_en;
static u32 rd32le(const u8* p){ return (u32)p[0]|((u32)p[1]<<8)|((u32)p[2]<<16)|((u32)p[3]<<24); }
static void wr32le(u8* p,u32 v){ p[0]=(u8)v; p[1]=(u8)(v>>8); p[2]=(u8)(v>>16); p[3]=(u8)(v>>24); }
static u16 r39_capr(void){ return (u16)(r39[0x38]|(r39[0x39]<<8)); }
static int r39_empty(void){ return ((u32)(r39_capr()+16)&0xFFFF)%8192==r39_cbr%8192; }
static u8 r39_in8(u16 p){ int r=p-IOB; if(r==0x37){ u8 v=r39[0x37]&~1; if(r39_empty())v|=1; return v; } return r39[r&255]; }
static u16 r39_in16(u16 p){ int r=p-IOB; return (u16)(r39_in8(p)|(r39[(r+1)&255]<<8)); }
static u32 r39_in32(u16 p){ int r=p-IOB; return rd32le(r39+r); }
static void r39_out8(u16 p,u8 v){ int r=p-IOB; if(r==0x37){ if(v&0x10){ r39[0x37]=0; r39_cbr=0; r39[0x38]=0xF0; r39[0x39]=0xFF; return; } r39[0x37]=v; r39_rx_en=(v&8)!=0; if(!(v&8)){ r39_cbr=0; r39[0x38]=0xF0; r39[0x39]=0xFF; } return; } r39[r]=v; }
static void r39_out16(u16 p,u16 v){ int r=p-IOB; r39[r]=(u8)v; r39[r+1]=(u8)(v>>8); }
static void r39_out32(u16 p,u32 v){ int r=p-IOB; wr32le(r39+r,v);
    if(r>=0x10&&r<0x20){ int i=(r-0x10)/4; u32 a=rd32le(r39+0x20+i*4); wire_put(P(a),(int)(v&0x1FFF)); wr32le(r39+r,(v&0x1FFF)|(1u<<13)|(1u<<15)); }
    if(r==0x30)r39_ring=P(v); }
static int r39_inject(const u8* d,int n){
    if(!r39_rx_en)return 0;
    u32 need=(u32)n+4+4; u32 used=(r39_cbr+8192-((u32)(r39_capr()+16)&0xFFFF)%8192)%8192; if(used+need+16>=8192)return 0;
    u8* h=r39_ring+r39_cbr; h[0]=1; h[1]=0; h[2]=(u8)(n+4); h[3]=(u8)((n+4)>>8);
    for(int i=0;i<n;i++)r39_ring[r39_cbr+4+i]=d[i];
    for(int i=0;i<4;i++)r39_ring[r39_cbr+4+n+i]=0xEE;
    r39_cbr=(r39_cbr+need+3)&~3u; r39_cbr%=8192;
    return 1;
}
static void test_rtl8139(void){
    memset(&g_pci,0,sizeof g_pci); g_pci.present=1; g_pci.ven=0x10EC; g_pci.dev=0x8139; g_pci.cls=2; g_pci.bar[0]=IOB|1;
    memset(r39,0,sizeof r39); for(int i=0;i<6;i++)r39[i]=(u8)(0x52+i); r39[0x58]=0;
    io_in8=r39_in8; io_in16=r39_in16; io_in32=r39_in32; io_out8=r39_out8; io_out16=r39_out16; io_out32=r39_out32; g_tick=0;
    struct nvx_nic n; memset(&n,0,sizeof n);
    int ok=rtl8139_probe(&n);
    rec(ok&&n.mac[0]==0x52&&n.mac[5]==0x57&&(g_pci.cmd&4)&&n.link(),"RTL8139: probe, reset, MAC, bus mastering, link",0);
    wire_reset(); u8 pk[1600]; int good=1;
    for(int i=0;i<12;i++){ int len=(int)(rnd()%1400)+14; mkpkt(pk,len,i); n.send(pk,len); if(wire_len[i]!=(len<60?60:len)||memcmp(wire[i],pk,(size_t)len))good=0; }
    rec(good&&wire_n==12,"RTL8139: 12 frames sent through the 4 transmit slots",0);
    int sent=0, got=0, bad=0, expect=0; u8 out[2048];
    for(int round=0;round<400;round++){ int len=(int)(rnd()%1500)+14; mkpkt(pk,len,sent); if(r39_inject(pk,len))sent++; int k=(int)(rnd()%3); for(int j=0;j<k;j++){ int m=n.recv(out,sizeof out); if(m>0){ u8 ex[2048]; int el=0; (void)el; got++; mkpkt(ex,m,expect); if(memcmp(ex,out,(size_t)m))bad++; expect++; } } }
    for(int j=0;j<1000;j++){ int m=n.recv(out,sizeof out); if(m<=0)break; u8 ex[2048]; mkpkt(ex,m,expect); if(memcmp(ex,out,(size_t)m))bad++; expect++; got++; }
    char det[100]; snprintf(det,sizeof det,"sent=%d got=%d bad=%d",sent,got,bad);
    rec(sent>200&&got==sent&&!bad,"RTL8139: hundreds of frames received across ring wrap-around",det);
}

static u8 r69[256]; static u32 r69_txi, r69_rxi;
struct d69 { u32 o1,o2; u64 addr; };
static struct d69* r69_tx(void){ return (struct d69*)P((u64)rd32le(r69+0x20)|((u64)rd32le(r69+0x24)<<32)); }
static struct d69* r69_rx(void){ return (struct d69*)P((u64)rd32le(r69+0xE4)|((u64)rd32le(r69+0xE8)<<32)); }
static u8 r69_in8(u16 p){ return r69[(p-IOB)&255]; }
static u16 r69_in16(u16 p){ int r=p-IOB; return (u16)(r69[r]|(r69[r+1]<<8)); }
static u32 r69_in32(u16 p){ return rd32le(r69+(p-IOB)); }
static void r69_out8(u16 p,u8 v){ int r=p-IOB; if(r==0x37&&(v&0x10)){ r69[0x37]=0; r69_txi=r69_rxi=0; return; }
    if(r==0x38&&(v&0x40)){ struct d69* t=r69_tx(); for(int g=0;g<64&&(t[r69_txi].o1&0x80000000u);g++){ struct d69* d=&t[r69_txi]; wire_put(P(d->addr),(int)(d->o1&0x3FFF)); int eor=(d->o1&0x40000000u)!=0; d->o1&=~0x80000000u; r69_txi=eor?0:r69_txi+1; } return; }
    r69[r]=v; }
static int r69_25;
static void r69_out16(u16 p,u16 v){ int r=p-IOB; if(r69_25&&r==0x90&&(v&1)){ r69_out8((u16)(IOB+0x38),0x40); r69[0x38]=0; return; } r69[r]=(u8)v; r69[r+1]=(u8)(v>>8); }
static void r69_out32(u16 p,u32 v){ wr32le(r69+(p-IOB),v); }
static int r69_inject(const u8* d,int n){ struct d69* x=&r69_rx()[r69_rxi]; if(!(x->o1&0x80000000u))return 0; if((int)(x->o1&0x3FFF)<n+4)return 0;
    u8* b=P(x->addr); memcpy(b,d,(size_t)n); memset(b+n,0xEE,4); int eor=(x->o1&0x40000000u)!=0; x->o1=(x->o1&0x40000000u)|0x30000000u|(u32)(n+4); r69_rxi=eor?0:r69_rxi+1; return 1; }
static void test_rtl8169(void){
    memset(&g_pci,0,sizeof g_pci); g_pci.present=1; g_pci.ven=0x10EC; g_pci.dev=0x8168; g_pci.cls=2; g_pci.bar[0]=IOB|1;
    memset(r69,0,sizeof r69); for(int i=0;i<6;i++)r69[i]=(u8)(0x10+i); r69[0x6C]=0x02;
    io_in8=r69_in8; io_in16=r69_in16; io_in32=r69_in32; io_out8=r69_out8; io_out16=r69_out16; io_out32=r69_out32; g_tick=0;
    struct nvx_nic n; memset(&n,0,sizeof n);
    int ok=rtl8169_probe(&n);
    rec(ok&&n.mac[0]==0x10&&n.mac[5]==0x15&&n.link()&&r69[0x37]==0x0C&&r69[0x50]==0,"RTL8111/8168: probe, reset, MAC, link, RX+TX enabled, config locked",0);
    wire_reset(); u8 pk[1600]; int good=1;
    for(int i=0;i<40;i++){ int len=(int)(rnd()%1450)+14; mkpkt(pk,len,i); n.send(pk,len); if(wire_n<=i||wire_len[i]!=(len<60?60:len)||memcmp(wire[i],pk,(size_t)len))good=0; }
    rec(good&&wire_n==40,"RTL8111/8168: 40 frames sent across the 16-entry ring",0);
    int sent=0, got=0, bad=0, expect=0; u8 out[2048];
    for(int round=0;round<400;round++){ int len=(int)(rnd()%1500)+14; mkpkt(pk,len,sent); if(r69_inject(pk,len))sent++; int k=(int)(rnd()%3); for(int j=0;j<k;j++){ int m=n.recv(out,sizeof out); if(m>0){ u8 ex[2048]; mkpkt(ex,m,expect); if(memcmp(ex,out,(size_t)m))bad++; expect++; got++; } } }
    for(int j=0;j<1000;j++){ int m=n.recv(out,sizeof out); if(m<=0)break; u8 ex[2048]; mkpkt(ex,m,expect); if(memcmp(ex,out,(size_t)m))bad++; expect++; got++; }
    char det[100]; snprintf(det,sizeof det,"sent=%d got=%d bad=%d",sent,got,bad);
    rec(sent>200&&got==sent&&!bad,"RTL8111/8168: hundreds of frames received, CRC stripped, ring wraps",det);
}


static u8 vio[64]; static u32 vq_pfn[2]; static u16 vq_sel, vq_last[2]; static int vio_kind; static u8* ramdisk; static u64 rd_sectors; static u32 vio_guestf;
struct vd { u64 addr; u32 len; u16 flags, next; };
static u8* vq_base(int q){ return P((u64)vq_pfn[q]<<12); }
static int vq_n(void){ return vio_kind==1?256:128; }
static volatile u16* vq_avail(int q){ int n=vq_n(); return (volatile u16*)(vq_base(q)+16*n); }
static u8* vq_used(int q){ int n=vq_n(); unsigned long a=((unsigned long)16*n+6+2*n+4095)&~4095UL; return vq_base(q)+a; }
static void vq_push_used(int q,u32 id,u32 len){ u8* u=vq_used(q); u16 idx=*(u16*)(u+2); u32* e=(u32*)(u+4+8*(idx%vq_n())); e[0]=id; e[1]=len; __sync_synchronize(); *(u16*)(u+2)=(u16)(idx+1); }
static int vq_pop(int q,u16* head){ volatile u16* a=vq_avail(q); if(a[1]==vq_last[q])return 0; *head=a[2+(vq_last[q]%vq_n())]; vq_last[q]++; return 1; }
static void vblk_process(void){ u16 h; struct vd* D=(struct vd*)vq_base(0);
    while(vq_pop(0,&h)){ struct vd* d0=&D[h]; u8* hd=P(d0->addr); u32 type=*(u32*)hd; u64 sec=*(u64*)(hd+8);
        struct vd* d1=&D[d0->next]; struct vd* ds=d1; u8* data=0; u32 dl=0;
        if(d1->flags&1){ data=P(d1->addr); dl=d1->len; ds=&D[d1->next]; }
        u8 st=0;
        if(type==0||type==1){ if(sec+dl/512>rd_sectors)st=1; else if(type==0){ if(!(d1->flags&2))st=1; else memcpy(data,ramdisk+sec*512,dl); } else memcpy(ramdisk+sec*512,data,dl); }
        else if(type==4)st=0; else st=2;
        *P(ds->addr)=st; vq_push_used(0,h,dl+1); } }
static void vnet_tx(void){ u16 h; struct vd* D=(struct vd*)vq_base(1); while(vq_pop(1,&h)){ struct vd* d=&D[h]; u8 tmp[4096]; u32 n=0; for(int g=0;g<8;g++){ memcpy(tmp+n,P(d->addr),d->len); n+=d->len; if(!(d->flags&1))break; d=&D[d->next]; } if(n>=10)wire_put(tmp+10,(int)n-10); vq_push_used(1,h,0); } }
static int vnet_inject(const u8* p,int n){ u16 h; if(!vq_pop(0,&h))return 0; struct vd* d=&((struct vd*)vq_base(0))[h]; if(!(d->flags&2)||d->len<(u32)n+10){ return 0; } u8* b=P(d->addr); memset(b,0,10); memcpy(b+10,p,(size_t)n); vq_push_used(0,h,(u32)n+10); return 1; }
static u8 vio_in8(u16 p){ return vio[(p-IOB)&63]; }
static u16 vio_in16(u16 p){ int r=p-IOB; if(r==0x0C)return (u16)vq_n(); return (u16)(vio[r]|(vio[r+1]<<8)); }
static u32 vio_in32(u16 p){ int r=p-IOB; if(r==0x08)return vq_pfn[vq_sel&1]; return rd32le(vio+r); }
static void vio_out8(u16 p,u8 v){ int r=p-IOB; vio[r]=v; if(r==0x12&&v==0){ vq_last[0]=vq_last[1]=0; vq_pfn[0]=vq_pfn[1]=0; } }
static void vio_out16(u16 p,u16 v){ int r=p-IOB; if(r==0x0E){ vq_sel=v; return; } if(r==0x10){ if(vio_kind==2)vblk_process(); else if(v==1)vnet_tx(); return; } vio[r]=(u8)v; vio[r+1]=(u8)(v>>8); }
static void vio_out32(u16 p,u32 v){ int r=p-IOB; if(r==0x08){ vq_pfn[vq_sel&1]=v; return; } if(r==0x04)vio_guestf=v; wr32le(vio+r,v); }
static void vio_setup(int kind){
    memset(&g_pci,0,sizeof g_pci); g_pci.present=1; g_pci.ven=0x1AF4; g_pci.dev=kind==1?0x1000:0x1001; g_pci.cls=kind==1?2:1; g_pci.bar[0]=IOB|1;
    memset(vio,0,sizeof vio); vio_kind=kind; vq_pfn[0]=vq_pfn[1]=0; vq_last[0]=vq_last[1]=0;
    io_in8=vio_in8; io_in16=vio_in16; io_in32=vio_in32; io_out8=vio_out8; io_out16=vio_out16; io_out32=vio_out32; g_tick=0;
}
static void test_virtio_net(void){
    vio_setup(1); wr32le(vio+0,(1u<<5)|(1u<<16)|(1u<<15)); for(int i=0;i<6;i++)vio[0x14+i]=(u8)(0xA0+i); vio[0x1A]=1;
    struct nvx_nic n; memset(&n,0,sizeof n);
    int ok=virtio_net_probe(&n);
    rec(ok&&n.mac[0]==0xA0&&n.mac[5]==0xA5&&vio[0x12]==7&&vio_guestf==((1u<<5)|(1u<<16))&&n.link(),"VirtIO-net: legacy handshake, only MAC+STATUS negotiated, MAC, link",0);
    vio[0x1A]=0; int down=!n.link(); vio[0x1A]=1;
    wire_reset(); u8 pk[1600]; int good=1;
    for(int i=0;i<100;i++){ int len=(int)(rnd()%1500)+14; mkpkt(pk,len,i); if(n.send(pk,len)!=len)good=0; if(wire_n!=(i<256?i+1:256)||wire_len[i]!=len||memcmp(wire[i],pk,(size_t)len))good=0; }
    rec(good&&down,"VirtIO-net: 100 frames sent with the 10-byte header stripped, link-down seen",0);
    int sent=0, got=0, bad=0, expect=0; u8 out[2048];
    for(int round=0;round<1200;round++){ int len=(int)(rnd()%1500)+14; mkpkt(pk,len,sent); if(vnet_inject(pk,len))sent++; int k=(int)(rnd()%3); for(int j=0;j<k;j++){ int m=n.recv(out,sizeof out); if(m>0){ u8 ex[2048]; mkpkt(ex,m,expect); if(memcmp(ex,out,(size_t)m))bad++; expect++; got++; } } }
    for(int j=0;j<2000;j++){ int m=n.recv(out,sizeof out); if(m<=0)break; u8 ex[2048]; mkpkt(ex,m,expect); if(memcmp(ex,out,(size_t)m))bad++; expect++; got++; }
    char det[100]; snprintf(det,sizeof det,"sent=%d got=%d bad=%d",sent,got,bad);
    rec(sent>500&&got==sent&&!bad,"VirtIO-net: over a thousand frames received, buffers recycled",det);
}
static void test_virtio_blk(void){
    vio_setup(2); wr32le(vio+0,(1u<<9)); rd_sectors=8192; ramdisk=calloc(rd_sectors,512); wr32le(vio+0x14,(u32)rd_sectors); wr32le(vio+0x18,0);
    u64 secs=0; int ok=virtio_blk_probe(&secs);
    rec(ok&&secs==8192&&vio[0x12]==7,"VirtIO-blk: handshake, FLUSH negotiated, capacity",0);
    static u8 buf[300*512], back[300*512]; int good=1;
    for(int t=0;t<60;t++){ int cnt=(int)(rnd()%300)+1; u64 lba=rnd()%(rd_sectors-(u64)cnt); for(int i=0;i<cnt*512;i++)buf[i]=(u8)rnd();
        if(virtio_blk_write(lba,cnt,buf))good=0; if(memcmp(ramdisk+lba*512,buf,(size_t)cnt*512))good=0;
        memset(back,0,sizeof back); if(virtio_blk_read(lba,cnt,back))good=0; if(memcmp(back,buf,(size_t)cnt*512))good=0; }
    rec(good,"VirtIO-blk: 60 random reads/writes up to 300 sectors (split into 128-sector requests)",0);
    rec(virtio_blk_read(8190,5,buf)!=0&&virtio_blk_flush()==0,"VirtIO-blk: read past the end refused, flush works",0);
    free(ramdisk);
}

static u8 nam[128], nabm[128]; static int ac_ready_after; static u8* spk; static long spk_n; static long spk_cap=4<<20;
static u8 ac_in8(u16 p){ if(p>=0xD000)return nabm[(p-0xD000)&127]; return nam[(p-0xC000)&127]; }
static u16 ac_in16(u16 p){ u8* a=p>=0xD000?nabm:nam; int r=(p-(p>=0xD000?0xD000:0xC000))&127; return (u16)(a[r]|(a[r+1]<<8)); }
static u32 ac_in32(u16 p){ if(p==0xD000+0x30){ return ac_ready_after<=0?0x100u:0u; } u8* a=p>=0xD000?nabm:nam; int r=(p-(p>=0xD000?0xD000:0xC000))&127; return rd32le(a+r); }
static void ac_out8(u16 p,u8 v){ if(p>=0xD000){ int r=p-0xD000; if(r==0x1B&&(v&2)){ nabm[0x14]=0; nabm[0x16]=1; nabm[0x18]=nabm[0x19]=0; nabm[0x1B]=0; return; } if(r==0x1B&&(v&1))nabm[0x16]&=(u8)~1; nabm[r]=v; } else nam[p-0xC000]=v; }
static void ac_out16(u16 p,u16 v){ if(p>=0xD000){ int r=p-0xD000; if(r==0x16){ nabm[0x16]&=(u8)~(v&0x1C); return; } nabm[r]=(u8)v; nabm[r+1]=(u8)(v>>8); } else { int r=p-0xC000; nam[r]=(u8)v; nam[r+1]=(u8)(v>>8); if(r==0x2C){ nam[0x2C]=0x80; nam[0x2D]=0xBB; } } }
static void ac_out32(u16 p,u32 v){ if(p==0xD000+0x2C){ ac_ready_after=3; return; } if(p>=0xD000)wr32le(nabm+(p-0xD000),v); else wr32le(nam+(p-0xC000),v); }
static void ac_tick(void){ if(ac_ready_after>0)ac_ready_after--; }
static int ac_samples_left=-1;
static void ac_play(int bytes){
    bytes&=~3;
    while(bytes>0){
        if(!(nabm[0x1B]&1)||(nabm[0x16]&1))return;
        u32 bd=rd32le(nabm+0x10); u8 civ=nabm[0x14]&31; u8* e=P(bd+civ*8); u32 addr=rd32le(e); u16 ns=(u16)(e[4]|(e[5]<<8));
        if(ac_samples_left<0)ac_samples_left=ns;
        int done=ns-ac_samples_left; u8* src=P(addr)+done*2;
        int take=bytes/2; if(take>ac_samples_left)take=ac_samples_left; if(take<=0&&ac_samples_left>0)return;
        if(spk_n+take*2<=spk_cap){ memcpy(spk+spk_n,src,(size_t)take*2); spk_n+=take*2; }
        ac_samples_left-=take; bytes-=take*2;
        nabm[0x18]=(u8)ac_samples_left; nabm[0x19]=(u8)(ac_samples_left>>8);
        if(ac_samples_left==0){ if(civ==nabm[0x15]){ nabm[0x16]|=1; ac_samples_left=-1; return; } nabm[0x14]=(u8)((civ+1)&31); ac_samples_left=-1; u8* e2=P(bd+nabm[0x14]*8); int n2=e2[4]|(e2[5]<<8); nabm[0x18]=(u8)n2; nabm[0x19]=(u8)(n2>>8); } }
}
static int check_stream(const u8* pat,long n,const char** why){
    long i=0; while(i<spk_n&&spk[i]==0)i++;
    if(spk_n-i<n){ *why="not all audio was played"; return 0; }
    if(memcmp(spk+i,pat,(size_t)n)){ *why="played audio differs from what was written"; return 0; }
    for(long k=i+n;k<spk_n;k++) if(spk[k]){ *why="old audio replayed after the end"; return 0; }
    return 1;
}
static void test_ac97(void){
    memset(&g_pci,0,sizeof g_pci); g_pci.present=1; g_pci.ven=0x8086; g_pci.dev=0x2415; g_pci.cls=4; g_pci.sub=1; g_pci.bar[0]=0xC000|1; g_pci.bar[1]=0xD000|1;
    memset(nam,0,sizeof nam); memset(nabm,0,sizeof nabm); nam[0x28]=1; ac_ready_after=1000000;
    io_in8=ac_in8; io_in16=ac_in16; io_in32=ac_in32; io_out8=ac_out8; io_out16=ac_out16; io_out32=ac_out32; g_tick=ac_tick;
    struct nvx_snd s; memset(&s,0,sizeof s);
    int ok=ac97_probe(&s);
    rec(ok&&s.rate==48000&&(nam[0x2A]&1)&&!(nam[0x03]&0x80),"AC97: cold reset, codec ready, VRA at 48 kHz, master unmuted",0);
    s.start();
    rec(rd32le(nabm+0x10)!=0&&(nabm[0x1B]&1)&&nabm[0x15]==31,"AC97: 32-entry buffer list loaded and PCM out running",0);
    spk=calloc(1,(size_t)spk_cap); spk_n=0; ac_samples_left=-1;
    long n=600000; u8* pat=malloc((size_t)n); for(long i=0;i<n;i++)pat[i]=(u8)(1+(i*13+i/97)%250);
    long off=0; int stalls=0;
    while(off<n&&stalls<100000){ int w=s.write(pat+off,(int)((n-off)>8192?8192:(n-off))); if(w>0)off+=w; else stalls++; ac_play(4096+(int)(rnd()%4096)); s.poll(); }
    for(int k=0;k<200;k++){ ac_play(4096); s.poll(); }
    const char* why="ok"; int sok=check_stream(pat,n,&why);
    char det[120]; snprintf(det,sizeof det,"%s (written %ld, played %ld)",why,off,spk_n);
    rec(off==n&&sok,"AC97: 600 KB streamed through the ring, exact order, silence after",det);
    s.volume(0); rec((ac_in16(0xC002)&0x8000)!=0,"AC97: volume 0 mutes the master output",0);
    s.stop(); free(spk); free(pat);
    memset(&g_pci,0,sizeof g_pci); g_pci.present=1; g_pci.ven=0x8086; g_pci.dev=0x2415; g_pci.cls=4; g_pci.sub=1; g_pci.bar[0]=0xC000|1; g_pci.bar[1]=0xD000|1;
    io_out32=0; ac_ready_after=1000000000; u64 t0=g_now; int ok2=ac97_probe(&s); io_out32=ac_out32;
    rec(!ok2&&g_now-t0<500000000ULL,"AC97: a codec that never becomes ready is given up within 0.5 s",0);
}

#define HBAR ((u8*)0x30000000)
static u8* H; static int hcorb_rp, hrirb_wp;
static u32 h_pinctl[64], h_eapd[64], h_sel[64], h_stream[64], h_fmt[64], h_amp_out[64], h_amp_in[64][4], h_power[64];
static u16 hr16(int o){ return *(u16*)(H+o); } static u32 hr32(int o){ return *(u32*)(H+o); }
static void hw16(int o,u16 v){ *(u16*)(H+o)=v; } static void hw32(int o,u32 v){ *(u32*)(H+o)=v; }
struct hw { int nid,type; u32 caps; u8 conn[4]; int nc; u32 pincap, cfg; };
static const struct hw HW[]={
    {0x02,0,(1u<<2)|1,{0},0,0,0}, {0x03,0,(1u<<2)|1,{0},0,0,0},
    {0x0B,2,(1u<<8)|(1u<<1),{0x18,0x19},2,0,0},
    {0x0C,2,(1u<<8)|(1u<<1),{0x02,0x0B},2,0,0}, {0x0D,2,(1u<<8)|(1u<<1),{0x03,0x0B},2,0,0},
    {0x14,4,(1u<<8)|(1u<<2),{0x0C,0x0D},2,(1u<<4)|(1u<<16),0x90170110u},
    {0x18,4,(1u<<8),{0x0C},1,(1u<<5),0x04A11030u},
    {0x19,4,0,{0},0,(1u<<5),0x411111F0u},
    {0x1B,4,(1u<<8),{0x0C,0x0D},2,(1u<<4),0x411111F0u},
    {0x21,4,(1u<<8)|(1u<<2),{0x0C,0x0D},2,(1u<<4),0x0421101Fu} };
static const struct hw* hfind(int nid){ for(unsigned i=0;i<sizeof HW/sizeof HW[0];i++) if(HW[i].nid==nid)return &HW[i]; return 0; }
static u32 hverb(u32 v){
    int nid=(v>>20)&0x7F; u32 verb=v&0xFFFFF; const struct hw* w=hfind(nid);
    if((verb>>8)==0xF00){ int p=verb&0xFF;
        if(nid==0){ if(p==0)return 0x10EC0269u; if(p==4)return (1u<<16)|1; return 0; }
        if(nid==1){ if(p==4)return (2u<<16)|0x20; if(p==5)return 1; if(p==0x12)return (0x57u<<8)|0x57u|(1u<<31); if(p==0x0D)return (0x1Fu<<8)|0x0B; return 0; }
        if(!w)return 0;
        if(p==9)return ((u32)w->type<<20)|w->caps; if(p==0x0C)return w->pincap; if(p==0x0E)return (u32)w->nc; if(p==0x12)return (w->caps&4)?((0x40u<<8)|0x40u):0; if(p==0x0D)return 0;
        return 0; }
    if((verb>>8)==0xF02){ if(!w)return 0; u32 r=0; int i0=verb&0xFF; for(int k=0;k<4&&i0+k<w->nc;k++)r|=(u32)w->conn[i0+k]<<(8*k); return r; }
    if((verb>>8)==0xF1C)return w?w->cfg:0;
    if((verb>>8)==0x707){ h_pinctl[nid&63]=verb&0xFF; return 0; }
    if((verb>>8)==0x70C){ h_eapd[nid&63]=verb&0xFF; return 0; }
    if((verb>>8)==0x701){ h_sel[nid&63]=verb&0xFF; return 0; }
    if((verb>>8)==0x706){ h_stream[nid&63]=verb&0xFF; return 0; }
    if((verb>>8)==0x705){ h_power[nid&63]=1+(verb&0xFF); return 0; }
    if((verb>>16)==0x2){ h_fmt[nid&63]=verb&0xFFFF; return 0; }
    if((verb>>16)==0x3){ u32 pl=verb&0xFFFF; if(pl&0x8000)h_amp_out[nid&63]=pl; if(pl&0x4000)h_amp_in[nid&63][(pl>>8)&3]=pl; return 0; }
    return 0;
}
static int h_mute_codec, h_cad_analog;
static u32 hverb_hdmi(u32 v){
    int nid=(v>>20)&0x7F; u32 verb=v&0xFFFFF;
    if((verb>>8)==0xF00){ int p=verb&0xFF;
        if(nid==0){ if(p==0)return 0x80862882u; if(p==4)return (1u<<16)|1; return 0; }
        if(nid==1){ if(p==4)return (2u<<16)|2; if(p==5)return 1; return 0; }
        if(nid==2){ if(p==9)return 1u|(1u<<9); return 0; }
        if(nid==3){ if(p==9)return (4u<<20)|(1u<<8)|(1u<<9); if(p==0x0C)return (1u<<4)|(1u<<7); if(p==0x0E)return 1; return 0; }
        return 0; }
    if((verb>>8)==0xF02)return nid==3?0x02u:0;
    if((verb>>8)==0xF1C)return nid==3?0x18560010u:0;
    return 0;
}
static void hda_win_tick(u8* W,int* crp,int* rwp,int hdmi){
    if(!(W[0x4C]&2)||!(W[0x5C]&2))return;
    int wp=*(u16*)(W+0x48)&0xFF; u32* corb=(u32*)P((u64)*(u32*)(W+0x40)|((u64)*(u32*)(W+0x44)<<32)); u64* rirb=(u64*)P((u64)*(u32*)(W+0x50)|((u64)*(u32*)(W+0x54)<<32));
    int n=(W[0x4E]&3)==2?256:(W[0x4E]&3)==1?16:2; int rn=(W[0x5E]&3)==2?256:(W[0x5E]&3)==1?16:2;
    if(*(u16*)(W+0x4A)&0x8000){ *crp=0; return; }
    if(*(u16*)(W+0x58)&0x8000){ *rwp=0; *(u16*)(W+0x58)=0; }
    while(*crp!=wp){ *crp=(*crp+1)%n; u32 v=corb[*crp]; if(h_mute_codec)continue; u32 r=(hdmi||(int)(v>>28)!=h_cad_analog)?hverb_hdmi(v):hverb(v); *rwp=(*rwp+1)%rn; rirb[*rwp]=r; *(u16*)(W+0x58)=(u16)*rwp; W[0x5D]|=1; }
}
static u8* H2; static int h2corb_rp, h2rirb_wp;
static void hda_tick(void){ if(H)hda_win_tick(H,&hcorb_rp,&hrirb_wp,0); if(H2)hda_win_tick(H2,&h2corb_rp,&h2rirb_wp,1); }
static int hsd(int r){ return 0x80+4*0x20+r; }
static void hda_play(int bytes){
    if(!(H[hsd(0)]&2))return;
    u64* bdl=(u64*)P((u64)hr32(hsd(0x18))|((u64)hr32(hsd(0x1C))<<32)); u32 cbl=hr32(hsd(8)); int lvi=hr16(hsd(0x0C));
    u32 pos=hr32(hsd(4));
    while(bytes>0){ u32 acc=0; int e=0; for(e=0;e<=lvi;e++){ u32 l=(u32)bdl[e*2+1]; if(pos<acc+l)break; acc+=l; } if(e>lvi){ pos=0; continue; }
        u32 in=pos-acc; u32 l=(u32)bdl[e*2+1]; u32 take=l-in; if(take>(u32)bytes)take=(u32)bytes;
        if(spk_n+take<=spk_cap){ memcpy(spk+spk_n,P(bdl[e*2])+in,take); spk_n+=take; }
        pos=(pos+take)%cbl; bytes-=(int)take; }
    hw32(hsd(4),pos);
}
static void hda_reset_regs(void){ memset(H,0,0x4000); hw16(0x00,0x4401); hw16(0x0E,1); H[0x4E]=0x40; H[0x5E]=0x40; memset(h_pinctl,0,sizeof h_pinctl); memset(h_eapd,0,sizeof h_eapd); memset(h_stream,0,sizeof h_stream); memset(h_fmt,0,sizeof h_fmt); memset(h_amp_out,0,sizeof h_amp_out); memset(h_amp_in,0,sizeof h_amp_in); memset(h_sel,0,sizeof h_sel); hcorb_rp=0; hrirb_wp=0; }
static void test_hda(void){
    H=mmap(HBAR,0x4000,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANONYMOUS|MAP_FIXED_NOREPLACE,-1,0); if(H!=HBAR){ rec(0,"HDA: simulator MMIO window",0); return; }
    memset(&g_pci,0,sizeof g_pci); g_pci.present=1; g_pci.ven=0x8086; g_pci.dev=0xA170; g_pci.cls=4; g_pci.sub=3; g_pci.bar[0]=(u32)(u64)HBAR;
    hda_reset_regs(); io_in8=0; io_out8=0; io_in16=0; io_out16=0; io_in32=0; io_out32=0; g_tick=hda_tick; h_mute_codec=0;
    struct nvx_snd s; memset(&s,0,sizeof s);
    int ok=hda_probe(&s);
    const char* inf=hda_info();
    rec(ok&&strstr(inf,"10ec:269")&&strstr(inf,"speaker(pin 14 dac 2)")&&strstr(inf,"headphones(pin 21 dac 2)"),"HDA: controller reset, CORB/RIRB, codec graph walk finds speaker and headphones",inf);
    int amps=(h_amp_out[0x02]&0xB000)==0xB000&&!(h_amp_out[0x02]&0x80)&&(h_amp_in[0x0C][0]&0x7000)==0x7000&&(h_amp_out[0x14]&0xB000)==0xB000;
    rec(h_pinctl[0x14]==0x40&&h_pinctl[0x21]==0xC0&&h_eapd[0x14]==2&&h_stream[0x02]==0x10&&h_fmt[0x02]==0x0011&&amps&&h_sel[0x14]==0&&h_pinctl[0x1B]==0&&h_pinctl[0x18]==0,"HDA: pins enabled (HP with amp), EAPD on, DAC 48 kHz/16-bit stream 1, path unmuted, unused pins untouched",0);
    s.start();
    rec((H[hsd(0)]&2)&&(H[hsd(2)]>>4)==1&&hr32(hsd(8))==32*4096&&hr16(hsd(0x0C))==31&&hr16(hsd(0x12))==0x11,"HDA: output stream 1 running with a 128 KB cyclic buffer list",0);
    spk=calloc(1,(size_t)spk_cap); spk_n=0;
    long n=700000; u8* pat=malloc((size_t)n); for(long i=0;i<n;i++)pat[i]=(u8)(1+(i*7+i/131)%250);
    long off=0; int stalls=0;
    while(off<n&&stalls<100000){ int w=s.write(pat+off,(int)((n-off)>10000?10000:(n-off))); if(w>0)off+=w; else stalls++; s.poll(); hda_play(3000+(int)(rnd()%6000)); s.poll(); }
    for(int k=0;k<200;k++){ s.poll(); hda_play(4096); }
    const char* why="ok"; int sok=check_stream(pat,n,&why);
    char det[120]; snprintf(det,sizeof det,"%s (written %ld, played %ld)",why,off,spk_n);
    rec(off==n&&sok,"HDA: 700 KB streamed, exact order, the looping buffer plays silence after the end",det);
    s.volume(0); rec((h_amp_out[0x02]&0x80)!=0,"HDA: volume 0 mutes the DAC output amplifier",0);
    { h_mute_codec=1; u64 spent=0; for(int k=0;k<40;k++){ g_now+=310000000ULL; u64 a=g_now; s.poll(); spent+=g_now-a; } h_mute_codec=0;
      rec(spent<1000000000ULL,"HDA: codec that stops answering mid-play: jack polling backs off (12 s of polls cost < 1 s)",0); }
    s.stop(); free(spk); free(pat);
    hda_reset_regs(); h_mute_codec=1; u64 t0=g_now; struct nvx_snd s2; int ok2=hda_probe(&s2);
    rec(!ok2&&g_now-t0<2000000000ULL,"HDA: a codec that never answers is given up within 2 s",0);
    hda_reset_regs(); hw16(0x0E,0); h_mute_codec=0; ok2=hda_probe(&s2);
    rec(!ok2,"HDA: controller without codecs is refused",0);
    munmap(H,0x4000);
}
#define HBAR2 ((u8*)0x34000000)
static void hda_pci(struct pdev* p,u16 dev,u8 sub,u32 b0,u32 b1){ memset(p,0,sizeof *p); p->present=1; p->ven=0x8086; p->dev=dev; p->cls=4; p->sub=sub; p->bar[0]=b0; p->bar[1]=b1; }
static void test_hda_multi(void){
    H=mmap(HBAR,0x4000,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANONYMOUS|MAP_FIXED_NOREPLACE,-1,0); H2=mmap(HBAR2,0x4000,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANONYMOUS|MAP_FIXED_NOREPLACE,-1,0);
    if(H!=HBAR||H2!=HBAR2){ rec(0,"HDA multi: simulator MMIO windows",0); return; }
    io_in8=0; io_out8=0; io_in16=0; io_out16=0; io_in32=0; io_out32=0; g_tick=hda_tick; h_mute_codec=0; h_cad_analog=0;
    struct nvx_snd s; char det[200];
    hda_reset_regs(); memset(H2,0,0x4000); *(u16*)H2=0x4401; *(u16*)(H2+0x0E)=1; H2[0x4E]=0x40; H2[0x5E]=0x40; h2corb_rp=h2rirb_wp=0;
    hda_pci(&g_pci,0x0F04,3,(u32)(u64)HBAR2,0); hda_pci(&g_pcix,0xA170,3,(u32)(u64)HBAR,0);
    memset(&s,0,sizeof s); int ok=hda_probe(&s); snprintf(det,sizeof det,"ok=%d %s corb2=%x rirb2=%x",ok,hda_info(),H2[0x4C],H2[0x5C]);
    rec(ok&&strstr(hda_info(),"10ec:269")&&strstr(hda_info(),"speaker(pin 14")&&!(H2[0x4C]&2)&&!(H2[0x5C]&2)&&!(g_pci.cmd&4)&&(g_pcix.cmd&4),"HDA: HDMI-only controller first is skipped (CORB/RIRB stopped, bus master off), analog controller used",det);
    H2=0; munmap(HBAR2,0x4000); memset(&g_pcix,0,sizeof g_pcix);
    hda_reset_regs(); hw16(0x0E,0x5); h_cad_analog=2; hda_pci(&g_pci,0xA170,3,(u32)(u64)HBAR,0);
    memset(&s,0,sizeof s); ok=hda_probe(&s); snprintf(det,sizeof det,"ok=%d %s",ok,hda_info());
    rec(ok&&strstr(hda_info(),"10ec:269")&&strstr(hda_info(),"headphones(pin 21"),"HDA: HDMI codec 0 skipped, analog codec 2 on the same controller used",det);
    hda_reset_regs(); hw16(0x0E,0x1); h_cad_analog=1; memset(&s,0,sizeof s); ok=hda_probe(&s);
    rec(!ok,"HDA: HDMI-only machine still refused (falls back to AC97 as before)",hda_info());
    h_cad_analog=0; memset(&g_pci,0,sizeof g_pci);
    hda_reset_regs(); hda_pci(&g_pcix,0x9C36,1,0x30000004u,0x1u); g_hi_pa=0x130000000ULL; g_hi_va=HBAR; H[0x02]=0; H[0x03]=0;
    memset(&s,0,sizeof s); ok=hda_probe(&s); snprintf(det,sizeof det,"ok=%d gctl=%x corb=%x cmd=%x",ok,hr32(0x08),hr32(0x40),g_pcix.cmd);
    rec(!ok&&!hr32(0x08)&&!hr32(0x40)&&!H[0x4C]&&!(g_pcix.cmd&4),"HDA: class 0401 DSP without HDA version registers is skipped before any register write",det);
    hda_reset_regs(); hda_pci(&g_pcix,0x9DC8,1,0x30000004u,0x1u); H[0x03]=1;
    memset(&s,0,sizeof s); ok=hda_probe(&s); snprintf(det,sizeof det,"ok=%d %s",ok,hda_info());
    rec(ok&&strstr(hda_info(),"10ec:269"),"HDA: Intel DSP-class controller (0401) with a 64-bit BAR above 4 GB is mapped and used",det);
    hda_reset_regs(); H[0x03]=1; g_hi_pa=0; memset(&s,0,sizeof s); ok=hda_probe(&s);
    rec(!ok,"HDA: a BAR above 4 GB that cannot be mapped is skipped cleanly",0);
    hda_reset_regs(); hda_pci(&g_pcix,0x2415,1,0xC000|1,0xD000|1); memset(&s,0,sizeof s); ok=hda_probe(&s);
    rec(!ok&&!(g_pcix.cmd&2),"HDA: an AC97 controller (class 0401, I/O BARs) is not touched",0);
    hda_reset_regs(); hda_pci(&g_pcix,0x9DC8,1,0x30000000u,0); g_pci.present=0; memset(&s,0,sizeof s); ok=hda_probe(&s);
    rec(!ok,"HDA: class 0401 with a 32-bit memory BAR is not taken for HD Audio",0);
    memset(&g_pcix,0,sizeof g_pcix); g_hi_pa=0; g_hi_va=0; munmap(H,0x4000); H=0;
}
#define EBAR ((u8*)0x31000000)
static volatile u32* E; static u32 e_rxh, e_txh; static u16 e_eeprom[3]={0x1100,0x3322,0x5544};
static u32 er(int o){ return E[o>>2]; } static void ew(int o,u32 v){ E[o>>2]=v; }
struct e_rxd { u64 addr; u16 len, csum; u8 status, errors; u16 special; };
struct e_txd { u64 addr; u16 len; u8 cso, cmd, status, css; u16 special; };
static void e_tick(void){
    if(er(0x0000)&0x04000000u){ ew(0x0000,er(0x0000)&~0x04000000u); e_rxh=e_txh=0; ew(0x3818,0); ew(0x2818,0); }
    u32 eerd=er(0x0014); if((eerd&1)&&!(eerd&0x10)){ u32 a=(eerd>>8)&0xFF; ew(0x0014,(a<<8)|0x10u|((u32)(a<3?e_eeprom[a]:0xFFFF)<<16)); }
    if(!(er(0x0400)&2))return;
    u32 n=er(0x3808)/16; if(!n)return; struct e_txd* t=(struct e_txd*)P((u64)er(0x3800)|((u64)er(0x3804)<<32));
    while(e_txh!=er(0x3818)){ struct e_txd* d=&t[e_txh]; wire_put(P(d->addr),d->len); if(d->cmd&8)d->status|=1; e_txh=(e_txh+1)%n; ew(0x3810,e_txh); }
}
static int e_inject(const u8* d,int len){
    if(!(er(0x0100)&2))return 0; u32 n=er(0x2808)/16; if(e_rxh==er(0x2818))return 0;
    struct e_rxd* x=&((struct e_rxd*)P((u64)er(0x2800)|((u64)er(0x2804)<<32)))[e_rxh];
    memcpy(P(x->addr),d,(size_t)len); x->len=(u16)len; x->errors=0; __sync_synchronize(); x->status=3; e_rxh=(e_rxh+1)%n; ew(0x2810,e_rxh); return 1;
}
static void test_e1000(void){
    E=mmap(EBAR,0x20000,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANONYMOUS|MAP_FIXED_NOREPLACE,-1,0); if((u8*)E!=EBAR){ rec(0,"e1000: simulator MMIO window",0); return; }
    memset(&g_pci,0,sizeof g_pci); g_pci.present=1; g_pci.ven=0x8086; g_pci.dev=0x100E; g_pci.cls=2; g_pci.bar[0]=(u32)(u64)EBAR;
    io_in8=0; io_out8=0; io_in16=0; io_out16=0; io_in32=0; io_out32=0; g_tick=e_tick; e_rxh=e_txh=0;
    ew(0x0008,2);
    struct nvx_nic n; memset(&n,0,sizeof n);
    int ok=e1000_probe(&n);
    int macok=n.mac[0]==0x00&&n.mac[1]==0x11&&n.mac[2]==0x22&&n.mac[3]==0x33&&n.mac[4]==0x44&&n.mac[5]==0x55;
    rec(ok&&macok&&n.link()&&(g_pci.cmd&4)&&(er(0x5404)&0x80000000u)&&(er(0x0100)&2)&&(er(0x0400)&2),"e1000: probe, reset, MAC from EEPROM, RX+TX enabled, link",0);
    wire_reset(); u8 pk[1600]; int good=1;
    for(int i=0;i<40;i++){ int len=(int)(rnd()%1450)+14; mkpkt(pk,len,i); n.send(pk,len); if(wire_n<=i||wire_len[i]!=(len<60?60:len)||memcmp(wire[i],pk,(size_t)len))good=0; }
    rec(good&&wire_n==40,"e1000: 40 frames sent across the 16-entry ring",0);
    int sent=0, got=0, bad=0, expect=0; u8 out[2048];
    for(int round=0;round<400;round++){ int len=(int)(rnd()%1500)+14; mkpkt(pk,len,sent); if(e_inject(pk,len))sent++; int k=(int)(rnd()%3); for(int j=0;j<k;j++){ int m=n.recv(out,sizeof out); if(m>0){ u8 ex[2048]; mkpkt(ex,m,expect); if(memcmp(ex,out,(size_t)m))bad++; expect++; got++; } } }
    for(int j=0;j<1000;j++){ int m=n.recv(out,sizeof out); if(m<=0)break; u8 ex[2048]; mkpkt(ex,m,expect); if(memcmp(ex,out,(size_t)m))bad++; expect++; got++; }
    char det[100]; snprintf(det,sizeof det,"sent=%d got=%d bad=%d",sent,got,bad);
    rec(sent>200&&got==sent&&!bad,"e1000: hundreds of frames received, ring tail handed back, wraps",det);
    g_pci.bar[0]=0x00000004u|0; g_pci.bar[1]=1; memset(&n,0,sizeof n);
    rec(!e1000_probe(&n),"e1000: a 64-bit BAR above 4 GB is refused",0);
    g_tick=0; munmap((void*)E,0x20000);
}
static void test_rtl8125(void){
    memset(&g_pci,0,sizeof g_pci); g_pci.present=1; g_pci.ven=0x10EC; g_pci.dev=0x8125; g_pci.cls=2; g_pci.bar[0]=IOB|1;
    memset(r69,0,sizeof r69); for(int i=0;i<6;i++)r69[i]=(u8)(0x20+i); r69[0x6C]=0x02; r69[0x38]=0xAA; r69[0x39]=0xBB; r69[0x3A]=0xCC; r69[0x3B]=0xDD; r69_25=1;
    io_in8=r69_in8; io_in16=r69_in16; io_in32=r69_in32; io_out8=r69_out8; io_out16=r69_out16; io_out32=r69_out32; g_tick=0;
    struct nvx_nic n; memset(&n,0,sizeof n);
    int ok=rtl8169_probe(&n);
    u32 rcr=rd32le(r69+0x44), tcr=rd32le(r69+0x40), imr=rd32le(r69+0x38);
    rec(ok&&n.mac[0]==0x20&&strstr(n.name,"8125")&&rcr==((8u<<27)|(7u<<8)|0x0Fu)&&tcr==0x03000780u&&imr==0&&r69[0x37]==0x0C,"RTL8125: 32-bit interrupt registers cleared, 8125 receive/transmit setup",0);
    wire_reset(); u8 pk[1600]; int good=1;
    for(int i=0;i<40;i++){ int len=(int)(rnd()%1450)+14; mkpkt(pk,len,i); n.send(pk,len); if(wire_n<=i||wire_len[i]!=(len<60?60:len)||memcmp(wire[i],pk,(size_t)len))good=0; }
    rec(good&&wire_n==40,"RTL8125: 40 frames sent through the 16-bit transmit poll register",0);
    int sent=0, got=0, bad=0, expect=0; u8 out[2048];
    for(int round=0;round<300;round++){ int len=(int)(rnd()%1500)+14; mkpkt(pk,len,sent); if(r69_inject(pk,len))sent++; int k=(int)(rnd()%3); for(int j=0;j<k;j++){ int m=n.recv(out,sizeof out); if(m>0){ u8 ex[2048]; mkpkt(ex,m,expect); if(memcmp(ex,out,(size_t)m))bad++; expect++; got++; } } }
    for(int j=0;j<1000;j++){ int m=n.recv(out,sizeof out); if(m<=0)break; u8 ex[2048]; mkpkt(ex,m,expect); if(memcmp(ex,out,(size_t)m))bad++; expect++; got++; }
    char det[100]; snprintf(det,sizeof det,"sent=%d got=%d bad=%d",sent,got,bad);
    rec(sent>200&&got==sent&&!bad,"RTL8125: frames received across ring wrap-around",det);
    r69_25=0;
}
#define GBAR ((u8*)0x32000000)
static volatile u32* G; static u32 g_txh, g_rxh; static u16 g_phy0, g_eewd[3]; static int g_igc, g_phy_bad, g_pch, g_lpcd_phase, g_e1ke, g_txh2;
static u32 gr(int o){ return G[o>>2]; } static void gw(int o,u32 v){ G[o>>2]=v; }
static void ig_tick(void){
    if(gr(0)&(1u<<26)){ gw(0,gr(0)&~(1u<<26)); g_txh=g_rxh=0; gw(0x10,gr(0x10)|(1u<<9)); if(g_pch)gw(8,gr(8)|(1u<<9)); }
    if(g_pch&&(gr(0)&(1u<<16))){ g_lpcd_phase=1; } else if(g_pch&&g_lpcd_phase==1){ g_lpcd_phase=2; g_phy_bad=0; gw(0x18,gr(0x18)|(1u<<2)); }
    u32 rx=gr(0xC028); if(rx&(1u<<25))gw(0xC028,rx); u32 tx=gr(0xE028); if(tx&(1u<<25))gw(0xE028,tx);
    { int eo=g_igc?0x12014:0x14; u32 ee=gr(eo); if((ee&1)&&!(ee&2)){ u32 a=(ee>>2)&0x3FFF; gw(eo,(a<<2)|2u|((u32)(a<3?g_eewd[a]:0xFFFF)<<16)); } }
    u32 md=gr(0x20); if(!(md&(1u<<28))&&(md>>26&3)){ int op=(int)(md>>26&3), reg=(int)(md>>16&31);
        if(g_phy_bad){ gw(0x20,md|(1u<<28)|(1u<<30)); }
        else if(op==1){ if(reg==0)g_phy0=(u16)md; gw(0x20,md|(1u<<28)); } else { u16 v=reg==0?g_phy0:reg==2?0x0141:reg==3?0x0CC0:0; gw(0x20,(md&0xFFFF0000u)|v|(1u<<28)); } }
    if(g_e1ke){
        if(!(gr(0x400)&2))return; u32 n=gr(0x3808)/16; if(!n)return; struct e_txd* t=(struct e_txd*)P((u64)gr(0x3800)|((u64)gr(0x3804)<<32));
        while(g_txh2!=(u32)gr(0x3818)){ struct e_txd* d=&t[g_txh2]; wire_put(P(d->addr),d->len); if(d->cmd&8)d->status|=1; g_txh2=(g_txh2+1)%n; gw(0x3810,g_txh2); }
        return; }
    if(!(gr(0x400)&2)||!(gr(0xE028)&(1u<<25)))return;
    u32 n=gr(0xE008)/16; if(!n)return; u8* ring=P((u64)gr(0xE000)|((u64)gr(0xE004)<<32));
    while(g_txh!=gr(0xE018)){ u8* d=ring+g_txh*16; u64 a; memcpy(&a,d,8); u32 c=rd32le(d+8), o=rd32le(d+12);
        if(((c>>20)&15)==3&&(c&(1u<<29))&&(c&(1u<<24))&&(o>>14)==(c&0xFFFF))wire_put(P(a),(int)(c&0xFFFF)); else wire_put((const u8*)"BAD",3);
        if(c&(1u<<27)){ memset(d,0,8); wr32le(d+8,0x12345678u); wr32le(d+12,1); }
        g_txh=(g_txh+1)%n; gw(0xE010,g_txh); }
}
static int ig_inject(const u8* d,int len,int eop){
    if(!(gr(0x100)&2)||!(gr(0xC028)&(1u<<25)))return 0; u32 n=gr(0xC008)/16; if(g_rxh==gr(0xC018))return 0;
    if((gr(0xC00C)&0x7F)!=2||((gr(0xC00C)>>25)&7)!=1)return 0;
    u8* x=P((u64)gr(0xC000)|((u64)gr(0xC004)<<32))+g_rxh*16; u64 a; memcpy(&a,x,8);
    memcpy(P(a),d,(size_t)len); u64 wb=(u64)(eop?3u:1u)|((u64)len<<32); memset(x,0xA5,8); __sync_synchronize(); memcpy(x+8,&wb,8);
    g_rxh=(g_rxh+1)%n; gw(0xC010,g_rxh); return 1;
}
static void ig_regs(int rah_ok){ memset((void*)G,0,0x20000); gw(8,2); g_txh=g_rxh=0; g_phy0=0x1940; g_eewd[0]=0x2211; g_eewd[1]=0x4433; g_eewd[2]=0x6655;
    if(rah_ok){ gw(0x5400,0xDDCCBBAAu); gw(0x5404,0x8000FFEEu); } }
static void test_igb_one(u16 dev,const char* tag,int igc){
    G=mmap(GBAR,0x20000,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANONYMOUS|MAP_FIXED_NOREPLACE,-1,0); if((u8*)G!=GBAR){ rec(0,"igb: simulator MMIO window",0); return; }
    memset(&g_pci,0,sizeof g_pci); g_pci.present=1; g_pci.ven=0x8086; g_pci.dev=dev; g_pci.cls=2; g_pci.sub=0; g_pci.bar[0]=(u32)(u64)GBAR;
    io_in8=0; io_out8=0; io_in16=0; io_out16=0; io_in32=0; io_out32=0; g_tick=ig_tick; g_igc=igc; g_phy_bad=0; g_pch=0; g_e1ke=0;
    char nm[120]; struct nvx_nic n; memset(&n,0,sizeof n); ig_regs(1);
    int ok=igb_probe(&n);
    snprintf(nm,sizeof nm,"%s: probe, reset, MAC from RAR0, PHY powered up and negotiating, queues enabled",tag);
    rec(ok&&n.mac[0]==0xAA&&n.mac[5]==0xFF&&n.link()&&!(g_phy0&(1u<<11))&&(g_phy0&(1u<<9))&&(gr(0xC028)&(1u<<25))&&(gr(0xE028)&(1u<<25))&&(gr(0x100)&2)&&(gr(0x400)&2)&&gr(0xC018)==255&&(gr(0x18)&(1u<<28)),nm,0);
    wire_reset(); u8 pk[1600]; int good=1;
    for(int i=0;i<150;i++){ int len=(int)(rnd()%1450)+14; mkpkt(pk,len,i); if(n.send(pk,len)<0)good=0; if(wire_n<=i%256||wire_len[i%256]!=(len<60?60:len)||memcmp(wire[i%256],pk,(size_t)len))good=0; if(wire_n>=200)wire_reset(); }
    snprintf(nm,sizeof nm,"%s: 150 frames through the 64-entry advanced transmit ring",tag); rec(good,nm,0);
    int sent=0, got=0, bad=0, expect=0; u8 out[2048];
    for(int round=0;round<700;round++){ int len=(int)(rnd()%1500)+14; mkpkt(pk,len,sent); if(round%97==5){ if(ig_inject(pk,len,0)){ mkpkt(pk,len,99999); ig_inject(pk,len,1); } continue; } if(ig_inject(pk,len,1))sent++; int k=(int)(rnd()%3); for(int j=0;j<k;j++){ int m=n.recv(out,sizeof out); if(m>0){ u8 ex[2048]; mkpkt(ex,m,expect); if(memcmp(ex,out,(size_t)m))bad++; expect++; got++; } } }
    for(int j=0;j<2000;j++){ int m=n.recv(out,sizeof out); if(m<=0)break; u8 ex[2048]; mkpkt(ex,m,expect); if(memcmp(ex,out,(size_t)m))bad++; expect++; got++; }
    char det[100]; snprintf(det,sizeof det,"sent=%d got=%d bad=%d",sent,got,bad);
    snprintf(nm,sizeof nm,"%s: frames received, buffers re-armed, frames split over two buffers dropped",tag); rec(sent>300&&got==sent&&!bad,nm,det);
    ig_regs(0); memset(&n,0,sizeof n); ok=igb_probe(&n);
    snprintf(nm,sizeof nm,"%s: MAC read from the NVM when RAR0 is empty",tag); rec(ok&&n.mac[0]==0x11&&n.mac[1]==0x22&&n.mac[5]==0x66&&(gr(0x5404)&0x80000000u),nm,0);
    g_pci.cls=3; memset(&n,0,sizeof n); snprintf(nm,sizeof nm,"%s: a device of another class with the same ID is left alone",tag); rec(!igb_probe(&n),nm,0);
    g_tick=0; munmap((void*)G,0x20000);
}
static void test_igb(void){ test_igb_one(0x1533,"igb (I210)",0); test_igb_one(0x15F3,"igc (I225-V)",1); }
static void test_e1000e(void){
    G=mmap(GBAR,0x20000,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANONYMOUS|MAP_FIXED_NOREPLACE,-1,0); if((u8*)G!=GBAR){ rec(0,"e1000e: simulator MMIO window",0); return; }
    memset(&g_pci,0,sizeof g_pci); g_pci.present=1; g_pci.ven=0x8086; g_pci.dev=0x15BC; g_pci.cls=2; g_pci.bar[0]=(u32)(u64)GBAR;
    io_in8=0; io_out8=0; io_in16=0; io_out16=0; io_in32=0; io_out32=0; g_tick=ig_tick; g_e1ke=1; g_txh2=0; g_pch=1; g_phy_bad=1; g_lpcd_phase=0;
    ig_regs(1); gw(0x5B54,(1u<<15)|(1u<<6));
    struct nvx_nic n; memset(&n,0,sizeof n);
    int ok=e1000e_probe(&n);
    rec(ok&&!g_phy_bad&&(g_phy0&(1u<<9))&&n.mac[0]==0xAA&&n.link()&&(gr(0x0F28)&0x10000u)&&((gr(0x3840)>>28)&3)==3&&(gr(0x100)&2)&&(gr(0x400)&2)&&!(gr(0)&(1u<<16))&&!(gr(0x18)&(1u<<11)),"e1000e (I219): PHY woken from low-power mode, reset, MAC, link, I219 transmit fixes",0);
    wire_reset(); u8 pk[1600]; int good=1;
    for(int i=0;i<100;i++){ int len=(int)(rnd()%1450)+14; mkpkt(pk,len,i); if(n.send(pk,len)<0)good=0; if(wire_len[i]!=(len<60?60:len)||memcmp(wire[i],pk,(size_t)len))good=0; }
    rec(good&&wire_n==100,"e1000e: 100 frames through the 64-entry transmit ring",0);
    E=G; e_rxh=0; int sent=0, got=0, bad=0, expect=0; u8 out[2048];
    for(int round=0;round<600;round++){ int len=(int)(rnd()%1500)+14; mkpkt(pk,len,sent); if(e_inject(pk,len))sent++; int k=(int)(rnd()%3); for(int j=0;j<k;j++){ int m=n.recv(out,sizeof out); if(m>0){ u8 ex[2048]; mkpkt(ex,m,expect); if(memcmp(ex,out,(size_t)m))bad++; expect++; got++; } } }
    for(int j=0;j<2000;j++){ int m=n.recv(out,sizeof out); if(m<=0)break; u8 ex[2048]; mkpkt(ex,m,expect); if(memcmp(ex,out,(size_t)m))bad++; expect++; got++; }
    char det[100]; snprintf(det,sizeof det,"sent=%d got=%d bad=%d",sent,got,bad);
    rec(sent>300&&got==sent&&!bad,"e1000e: frames received across ring wrap-around",det);
    ig_regs(1); gw(0x5B54,(1u<<15)); g_phy_bad=1; g_lpcd_phase=0; g_txh2=0; memset(&n,0,sizeof n);
    ok=e1000e_probe(&n);
    rec(ok&&g_phy_bad&&!(gr(0)&(1u<<31)),"e1000e: PHY power toggle and PHY reset skipped while the management engine blocks them",0);
    g_tick=0; g_pch=0; g_e1ke=0; munmap((void*)G,0x20000);
}
int main(void){
    setvbuf(stdout,0,_IOLBF,0);
    if(mmap(ARENA,ARENA_SZ,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANONYMOUS|MAP_FIXED_NOREPLACE,-1,0)!=ARENA){ printf("arena mmap failed\n"); return 2; }
    test_e1000(); test_e1000e(); test_igb(); test_rtl8139(); test_rtl8169(); test_rtl8125(); test_virtio_net(); test_virtio_blk(); test_ac97(); test_hda(); test_hda_multi();
    memset(&g_pci,0,sizeof g_pci); { struct nvx_nic n; struct nvx_snd s; u64 x; rec(!e1000_probe(&n)&&!e1000e_probe(&n)&&!igb_probe(&n)&&!vmxnet3_probe(&n)&&!rtl8139_probe(&n)&&!rtl8169_probe(&n)&&!virtio_net_probe(&n)&&!virtio_blk_probe(&x)&&!ac97_probe(&s)&&!hda_probe(&s),"no device present: every probe returns 0 without touching hardware",0); }
    printf("%s\n",g_fail?"FAILURES":"ALL PASS");
    return g_fail?1:0;
}
