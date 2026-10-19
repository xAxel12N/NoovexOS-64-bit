#include "include/kapi.h"
#include "include/drivers.h"
#include "drivers/sound/ring.h"
#define NBUF 32
#define BSZ 4096
static u16 nam, nabm; static struct bdl { u32 addr; u16 samples; u16 flags; }* bdl; static u8* pcm; static struct sring ring; static int running, rate=48000;
static u32 hwpos(void){ u8 civ=kapi_inb(nabm+0x14)&31; u16 picb=kapi_inw(nabm+0x18); u32 left=picb?(u32)picb*2u:BSZ; if(left>BSZ)left=BSZ; return (u32)civ*BSZ+(BSZ-left); }
static void a97_poll(void){
    if(!running)return;
    sring_hw(&ring,hwpos()%(NBUF*BSZ));
    u8 civ=kapi_inb(nabm+0x14)&31;
    kapi_outb(nabm+0x15,(u8)((civ+NBUF-1)%NBUF));
    u16 sr=kapi_inw(nabm+0x16);
    if(sr&1){ kapi_outw(nabm+0x16,0x1C); kapi_outb(nabm+0x1B,0x01); }
    else if(sr&0x1C)kapi_outw(nabm+0x16,0x1C);
}
static int a97_start(void){
    if(running)return 1;
    kapi_outb(nabm+0x1B,0x02);
    for(int t=0;t<1000&&(kapi_inb(nabm+0x1B)&0x02);t++)kapi_delay_us(10);
    for(int i=0;i<NBUF;i++){ bdl[i].addr=(u32)kapi_phys(pcm+i*BSZ); bdl[i].samples=BSZ/2; bdl[i].flags=0; }
    sring_init(&ring,pcm,NBUF*BSZ);
    kapi_outl(nabm+0x10,(u32)kapi_phys(bdl));
    kapi_outb(nabm+0x15,NBUF-1);
    kapi_outw(nabm+0x16,0x1C);
    kapi_outb(nabm+0x1B,0x01);
    running=1; return 1;
}
static int a97_free(void){ a97_poll(); return running?sring_free(&ring):0; }
static int a97_write(const u8* d,int len){ a97_poll(); return running?sring_write(&ring,d,len):0; }
static void a97_stop(void){ if(!running)return; kapi_outb(nabm+0x1B,0); kapi_outb(nabm+0x1B,0x02); running=0; }
static void a97_volume(int pct){ if(pct<0)pct=0; if(pct>100)pct=100; u16 att=(u16)((100-pct)*31/100); u16 v=pct==0?0x8000:(u16)((att<<8)|att); kapi_outw(nam+0x02,v); kapi_outw(nam+0x04,v); kapi_outw(nam+0x18,pct==0?0x8000:0x0808); }
int ac97_probe(struct nvx_snd* out){
    int b,d,f; if(!kapi_pci_find_class(0x04,0x01,0,&b,&d,&f))return 0;
    u32 b0=kapi_pci_rd32(b,d,f,0x10), b1=kapi_pci_rd32(b,d,f,0x14);
    if(!(b0&1)||!(b1&1))return 0;
    nam=(u16)(b0&0xFFFC); nabm=(u16)(b1&0xFFFC); if(!nam||!nabm)return 0;
    kapi_pci_enable(b,d,f);
    kapi_outl(nabm+0x2C,0x00000002u);
    int ok=0; for(int t=0;t<2000;t++){ if(kapi_inl(nabm+0x30)&0x100){ ok=1; break; } kapi_delay_us(100); }
    if(!ok){ kapi_log("ac97: codec not ready"); return 0; }
    kapi_outw(nam+0x00,0); kapi_delay_us(1000);
    u16 ext=kapi_inw(nam+0x28);
    if(ext&1){ kapi_outw(nam+0x2A,(u16)(kapi_inw(nam+0x2A)|1)); kapi_outw(nam+0x2C,48000); rate=kapi_inw(nam+0x2C); if(rate<8000)rate=48000; }
    a97_volume(80);
    bdl=(struct bdl*)kapi_dma_pages(1); pcm=(u8*)kapi_dma_pages(NBUF*BSZ/4096);
    if(!bdl||!pcm)return 0;
    if(kapi_phys(pcm)>=0xFFFF0000ULL)return 0;
    running=0;
    out->name="AC97"; out->rate=rate; out->start=a97_start; out->free_bytes=a97_free; out->write=a97_write; out->poll=a97_poll; out->stop=a97_stop; out->volume=a97_volume;
    kapi_log("ac97: codec ready");
    return 1;
}
