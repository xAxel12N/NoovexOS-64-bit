#include "include/nvx/base.h"
#include "include/nvx/core/klog.h"
#include "include/nvx/core/sysfeat.h"

int fs_find_name(const char*); char* fsd(int); int fs_save_bin(const char*,const u8*,int,int); void nvx3_sync_all(void);
static int fg_flag(const char* nm){ if(!nm)return -1; int k=fs_find_name(nm); if(k<0)return -1; char* d=fsd(k); return d?d[0]:-1; }
static void fg_put(const char* nm,char v){ if(!nm)return; u8 b[2]={(u8)v,'\n'}; if(fs_save_bin(nm,b,2,0)>=0)nvx3_sync_all(); }
static void fg_log(const struct featguard* g,const char* a,const char* b){ char m[160]; int n=0; const char* s[3]={g->name,a,b};
    for(int i=0;i<3;i++)for(const char* q=s[i];q&&*q&&n<158;q++)m[n++]=*q; m[n]=0; klog(m); }
void fg_boot(struct featguard* g){
    if(g->trying)return; g->trying=-1;
    int f=fg_flag(g->flag);
    if(f=='1'){ g->dead=1; fg_put(g->flag,'3'); fg_log(g,": off - it did not finish starting during the last boot",0); }
    else if(f=='3'){ g->dead=1; }
    else if(f=='2'){ g->on=0; }
    else if(f=='0'){ g->on=1; } }
int fg_begin(struct featguard* g){
    fg_boot(g); if(!g->on||g->dead)return 0;
    if(g->trying!=1){ g->trying=1; fg_put(g->flag,'1'); } return 1; }
void fg_ok(struct featguard* g){ if(g->trying==1){ g->trying=-1; fg_put(g->flag,'0'); } }
void fg_fail(struct featguard* g,const char* why){
    if(g->trying==1){ g->trying=-1; fg_put(g->flag,'0'); }
    g->fails++; int mx=g->maxfail>0?g->maxfail:3;
    if(g->fails>=mx&&!g->dead){ g->dead=1; fg_log(g,": off for this session after repeated failures: ",why); } else fg_log(g,": failed: ",why); }
void fg_enable(struct featguard* g,int on){ fg_boot(g); g->on=on?1:0; g->fails=0; if(on)g->dead=0; g->trying=-1; fg_put(g->flag,on?'0':'2'); }

static struct featguard sf_g[SYSF_COUNT]={ {"ext4 read-write","feat-ext4rw.cfg",1,0,3,0,0}, {"USB MIDI","feat-midi.cfg",1,0,3,0,0}, {"Intel Gen 12 blitter","feat-iblt12.cfg",1,0,1,0,0}, {"AMD tear-free flips","feat-amdflip.cfg",1,0,3,0,0}, {"Files on other disks","feat-volfs.cfg",1,0,3,0,0}, {"SD card ADMA2","feat-sdadma.cfg",1,0,3,0,0}, {"AMD SDMA copy engine","feat-amdsdma.cfg",1,0,3,0,0}, {"exFAT disks","feat-exfat.cfg",1,0,3,0,0}, {"Writing to other disks","feat-volwr.cfg",1,0,3,0,0} , {"Linux camera interface","feat-v4l2.cfg",1,0,3,0,0}, {"USB serial adapters","feat-usbser.cfg",1,0,3,0,0} };
static const char* const sf_nm[SYSF_COUNT]={"ext4 read-write on other disks","USB MIDI","Intel Gen 12 blitter","AMD tear-free page flips","Files on ext4 disks and SD cards","SD card DMA transfers (ADMA2)","AMD SDMA copy engine","exFAT SD cards and USB sticks","Writing to USB sticks and other disks","Linux camera interface (/dev/video0)","USB serial adapters (/dev/ttyUSB, /dev/ttyACM)"};
static int sfv(int f){ return f>=0&&f<SYSF_COUNT; }
int sysfeat_get(int f){ if(!sfv(f))return -1; fg_boot(&sf_g[f]); return sf_g[f].on; }
int sysfeat_set(int f,int v){ if(!sfv(f)||v<0||v>1)return -1; fg_enable(&sf_g[f],v); return 0; }
int sysfeat_state(int f){ if(!sfv(f))return SYSF_ST_OFF; fg_boot(&sf_g[f]); return !sf_g[f].on?SYSF_ST_OFF:sf_g[f].dead?SYSF_ST_FAILED:SYSF_ST_ON; }
int sysfeat_default(int f){ return sfv(f)?1:-1; }
int sysfeat_max(int f){ return sfv(f)?1:-1; }
const char* sysfeat_name(int f){ return sfv(f)?sf_nm[f]:""; }
const char* sysfeat_choice(int f,int v){ if(f==SYSF_EXT4_RW)return v?"read-write if the disk supports cache flush":"read-only"; return v?"on":"off"; }
int sysfeat_begin(int f){ return sfv(f)?fg_begin(&sf_g[f]):0; }
void sysfeat_ok(int f){ if(sfv(f))fg_ok(&sf_g[f]); }
void sysfeat_fail(int f,const char* why){ if(sfv(f))fg_fail(&sf_g[f],why); }
const char* sysfeat_text(int f){
    static const char* const T[SYSF_COUNT][3]={
        {"ext4 disks: read-only","ext4 disks: read-write when the disk supports cache flush (AHCI, NVMe, virtio, USB)","ext4 disks: read-only for this session after failures"},
        {"USB MIDI: off","USB MIDI: on (up to 4 devices, /dev/midiN)","USB MIDI: off for this session after failures"},
        {"Intel Gen 12 blitter: off (CPU drawing)","Intel Gen 12 blitter: on (Tiger/Alder/Raptor Lake)","Intel Gen 12 blitter: off after a failed start (CPU drawing)"},
        {"AMD tear-free flips: off (UEFI framebuffer)","AMD tear-free flips: on (two buffers, flip at vertical blank)","AMD tear-free flips: off after failures (UEFI framebuffer)"},
        {"Files on other disks: off","Files on other disks: on (ext4, FAT16/FAT32 and exFAT disks, SD cards, USB sticks)","Files on other disks: off after failures"},
        {"SD card DMA: off (PIO)","SD card DMA: on (ADMA2, PIO if it fails)","SD card DMA: off after failures (PIO)"},
        {"AMD SDMA: off (CPU copies)","AMD SDMA: on (GPU copies and fills, CPU if one fails)","AMD SDMA: off after failures (CPU copies)"},
        {"exFAT: off","exFAT: on (read and write, flushed after every change)","exFAT: off after failures"},
        {"Writing to USB sticks and fixed disks: off (read-only)","Writing to USB sticks and fixed disks: on when the disk supports cache flush","Writing to USB sticks and fixed disks: off after failures (read-only)"},
        {"Linux camera interface: off","Linux camera interface: on (/dev/video0 for ffmpeg, browsers and other Linux programs)","Linux camera interface: off for this session after failures"},
        {"USB serial adapters: off","USB serial adapters: on (FTDI, CP210x, CH340, PL2303, CDC-ACM as /dev/ttyUSBn and /dev/ttyACMn)","USB serial adapters: off for this session after failures"} };
    if(!sfv(f))return ""; int s=sysfeat_state(f); return T[f][s==SYSF_ST_FAILED?2:s==SYSF_ST_ON?1:0]; }
