/* ===========================================================================
   NoovexOS 64  -  kernel64.c
   Native x86-64 kernel.  Milestone 3: interactive desktop.

   - 64-bit IDT + exception handlers -> Red Screen of Death
   - PIC remap, PIT tick, PS/2 keyboard + mouse (all waits time-bounded)
   - back-buffer compositor (bpp aware: 24bpp / 32bpp)
   - window manager: open / close / focus / drag
   - apps: Welcome, Terminal (nsh shell + neofetch), Settings, Files, About
   =========================================================================== */

#include <stdint.h>
#include "include/font.h"                      /* FONT814[95][14], 8x14, LSB-first */
#include "include/fontaa.h"                    /* fontaa[95][112], DejaVu Sans Mono AA, 8x14 alpha */
#include "include/js.h"                        /* small integer JavaScript interpreter */

typedef uint8_t  u8;
typedef uint16_t u16;
typedef uint32_t u32;
typedef uint64_t u64;
typedef int64_t  i64;

/* --- ported from Noovex9 (32-bit): RFC-verified crypto + TLS 1.3 client --- */
#include "lib/crypto/nvxcrypto.h"
#include "lib/crypto/nvxtls.h"
#include "lib/crypto/nvxwpa.h"
#include "lib/crypto/nvxcrypto2.h"

void *memset(void *d,int c,unsigned long n){ u8 *p=d; while(n--) *p++=(u8)c; return d; }
void *memcpy(void *d,const void *s,unsigned long n){ u8 *p=d; const u8 *q=s; while(n--) *p++=*q++; return d; }
/* ---- heap allocator (foundation for large ports: Lua/NetSurf/Quake): first-fit free-list + bump ---- */
typedef struct hblk { unsigned long size; struct hblk* next; int used; } hblk;
static hblk* heap_head=0; static unsigned long long heap_cur=0, heap_top=0, heap_base=0;
static void heap_init(int ram_mb){
    heap_base=0x10000000ULL;                                    /* 256 MB: above image/.bss/scratch/WAD */
    unsigned long long ram=(unsigned long long)(ram_mb>0?ram_mb:64)*1048576ULL;
    heap_top=(ram>heap_base+0x8000000ULL)?(ram-0x100000ULL):(heap_base+0x8000000ULL);  /* 1MB headroom; >=128MB heap */
    heap_cur=heap_base; heap_head=0;
}
static int hblk_ok(hblk* b){ unsigned long long a=(unsigned long long)b; return b && a>=heap_base && a<heap_top && (a&7)==0; }
static void* kmalloc(unsigned long n){
    if(n==0)return 0; n=(n+15)&~15UL; int g=0;
    for(hblk* b=heap_head; hblk_ok(b); b=b->next){ if(++g>1000000)break; if(!b->used && b->size>=n){ b->used=1;
        if(b->size >= n+sizeof(hblk)+32){ hblk* nb=(hblk*)((u8*)b+sizeof(hblk)+n); nb->size=b->size-n-sizeof(hblk); nb->used=0; nb->next=b->next; b->next=nb; b->size=n; }
        return (u8*)b+sizeof(hblk); } }
    unsigned long long need=sizeof(hblk)+n; if(heap_cur+need>heap_top)return 0;
    hblk* b=(hblk*)heap_cur; heap_cur+=need; b->size=n; b->used=1; b->next=0;
    if(!heap_head)heap_head=b; else { hblk* t=heap_head; int g2=0; while(hblk_ok(t->next)){t=t->next; if(++g2>1000000)break;} t->next=b; }
    return (u8*)b+sizeof(hblk);
}
static void kfree(void* p){ if(!p)return; hblk* b=(hblk*)((u8*)p-sizeof(hblk)); if(!hblk_ok(b))return; b->used=0;
    int g=0; for(hblk* c=heap_head; hblk_ok(c); c=c->next){ if(++g>1000000)break; int gg=0;
        while(hblk_ok(c->next) && !c->used && !c->next->used && (u8*)c->next==(u8*)c+sizeof(hblk)+c->size){ c->size+=sizeof(hblk)+c->next->size; c->next=c->next->next; if(++gg>1000000)break; } } }
static void* krealloc(void* p,unsigned long n){ if(!p)return kmalloc(n); if(n==0){kfree(p);return 0;} hblk* b=(hblk*)((u8*)p-sizeof(hblk)); unsigned long old=b->size; if(old>=n)return p; void* np=kmalloc(n); if(!np)return 0; unsigned long c=old<n?old:n; for(unsigned long i=0;i<c;i++)((u8*)np)[i]=((u8*)p)[i]; kfree(p); return np; }
static void* kcalloc(unsigned long a,unsigned long b){ unsigned long n=a*b; void* p=kmalloc(n); if(p){u8* q=p; for(unsigned long i=0;i<n;i++)q[i]=0;} return p; }
#include "mm/pmm_vmm.c"
#include "kernel/proc/ring3_sched.c"
#include "lib/klibc.c"
#include "drivers/video/framebuffer.c"
#include "drivers/usb/xhci.c"
#include "drivers/net/nic.c"
#include "drivers/storage/ahci.c"
#include "net/tcpip.c"
#include "net/sockets.c"
#include "net/http.c"
#include "drivers/input/ps2.c"
#include "arch/x86_64/interrupts.c"
#include "gui/theme.c"
#include "kernel/panic.c"
#include "gui/wm.c"
#include "fs/ramfs.c"
#include "fs/vfs.c"
#include "kernel/syscall.c"
#include "shell/terminal.c"
#include "drivers/storage/ata.c"
#include "drivers/storage/gdisk.c"
#include "drivers/storage/nvme.c"
#include "fs/nvxfs.c"
#include "fs/fat_ata.c"
#include "drivers/wifi/rtw_efuse.c"
#include "kernel/buildsig.c"
#include "drivers/wifi/rtw8821ce.c"
#include "net/wifi/wpa2.c"
#include "drivers/wifi/rtw_sec.c"
#include "net/wifi/wifi_netdev.c"
#include "fs/dirtree.c"
#include "drivers/video/nvidia/nvgpu.c"
#include "drivers/video/nvidia/nvdisp.c"
#include "shell/commands.c"
#include "apps/editor.c"
#include "apps/calculator.c"
#include "apps/chatgpt.c"
#include "apps/browser.c"
#include "apps/settings.c"
#include "apps/files_classic.c"
#include "fs/pkg.c"
#include "apps/file_explorer.c"
#include "apps/small_apps.c"
#include "apps/doom.c"
#include "apps/logs.c"
#include "gui/desktop.c"
#include "gui/input.c"
#include "init/splash.c"
#include "init/bootmenu.c"
#include "init/installer.c"
#include "init/setup.c"
#include "init/main.c"
