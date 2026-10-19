#include "include/nvx/base.h"
#include "include/nvx/arch/x86_64/cpu/percpu.h"
#include "include/nvx/mem/heap.h"
#include "include/nvx/mem/pmm_vmm.h"
#include "include/nvx/lib/base/fmt.h"
#include "include/nvx/mem/slab.h"
#include "include/nvx/core/klog.h"
#include "include/nvx/drivers/video/fb/framebuffer.h"
#include "include/nvx/lib/crypto/nvxcrypto.h"
#include "include/nvx/arch/x86_64/cpu/cpuinfo.h"
#include "include/nvx/drivers/bus/pci.h"
#include "include/nvx/drivers/usb/xhci.h"
#include "include/nvx/net/http/http.h"
#include "include/nvx/arch/x86_64/interrupts/interrupts.h"
#include "include/nvx/drivers/input/ps2/ps2_mouse.h"
#include "include/nvx/graphics/theme/theme.h"
#include "include/nvx/graphics/ui/wm.h"
#include "include/nvx/fs/vfs/ramfs.h"
#include "include/nvx/fs/vfs/vfs.h"
#include "include/nvx/shell/terminal.h"
#include "include/nvx/fs/nvx/nvxfs.h"
#include "include/nvx/core/buildsig.h"
#include "include/nvx/sys/native/syscall.h"
#include "include/nvx/core/power.h"
#include "include/nvx/drivers/acpi/acpi_dev.h"
#include "include/nvx/arch/x86_64/time/hrtimer.h"
#include "include/nvx/core/sound.h"
#include "include/nvx/drivers/input/touchpad/i2c_hid.h"
#include "include/nvx/drivers/input/ps2/elan_ps2.h"
#include "include/nvx/drivers/input/touchpad/touchpad.h"
#include "include/nvx/arch/x86_64/smp/smp_sched.h"
#include "include/nvx/core/proc/jobs.h"
#include "include/nvx/fs/nvx/nvx4.h"
#include "include/nvx/fs/vfs/unixfs.h"
#include "include/nvx/shell/commands.h"
#include "include/nvx/apps/system/settings.h"
#include "include/nvx/apps/system/files_classic.h"
#include "include/nvx/graphics/ui/desktop.h"
#include "include/nvx/graphics/ui/input.h"
#include "include/nvx/core/init/splash.h"
#include "include/nvx/core/init/bootmenu.h"
#include "include/nvx/core/init/installer.h"
#include "include/nvx/core/init/setup.h"
#include "include/nvx/arch/x86_64/cpu/cpu_init.h"
#include "include/nvx/core/init/main.h"
/* =========================================================================
   kmain
   ========================================================================= */
static void init_windows(void){
    WN[W_WELCOME]=(Win){ (int)W/2-280,96,560,340,1,++ztop };
    WN[W_TERM]   =(Win){ (int)W/2-200,120,760,480,0,0 };
    WN[W_FILES]  =(Win){ 150,70,900,540,0,0 };
    WN[W_EDITOR] =(Win){ 260,110,640,470,0,0 };
    WN[W_CALC]   =(Win){ 360,150,300,420,0,0 };
    WN[W_BROWSER]=(Win){ 250,110,700,470,0,0 };
    WN[W_CLOCK]  =(Win){ 380,140,430,430,0,0 };
    WN[W_SYSMON] =(Win){ 180,70,900,640,0,0 };
    WN[W_LOGS]   =(Win){ 240,90,680,520,0,0 };
    WN[W_TFIND]  =(Win){ 280,110,640,520,0,0 };
    WN[W_VIDEO]  =(Win){ (int)W/2-430,70,860,540,0,0 };
    WN[W_CAMERA] =(Win){ (int)W/2-470,60,(int)W>=1000?940:700,600,0,0 };
    WN[W_BENCH]  =(Win){ (int)W/2-390,70,780,620,0,0 };
    WN[W_RECORDER]=(Win){ (int)W/2-320,90,640,520,0,0 };
    WN[W_QUAKE]  =(Win){ (int)W>=1340?(int)W/2-644:(int)W/2-324,(int)H>=1040?30:40,(int)W>=1340?1288:648,(int)H>=1040?(int)H-90:512,0,0 };
    WN[W_GPU3D]  =(Win){ (int)W>=1100?(int)W/2-500:10,(int)H>=800?50:30,(int)W>=1100?1000:(int)W-20,(int)H>=800?680:(int)H-80,0,0 };
    WN[W_WRITER] =(Win){ (int)W>=1100?(int)W/2-520:10,40,(int)W>=1100?1040:(int)W-20,(int)H-110,0,0 };
    WN[W_XDISP]  =(Win){ (int)W/2-403,40,806,600+TITLEH+4,0,0 };
    WN[W_CALENDAR]=(Win){ 330,120,470,430,0,0 };
    WN[W_SNAKE]  =(Win){ 380,110,446,462,0,0 };
    WN[W_FLAPPY] =(Win){ 400,110,420,470,0,0 };
    WN[W_NEOFETCH]=(Win){ 300,90,600,540,0,0 };
    WN[W_PAINT]  =(Win){ 300,110,620,440,0,0 };
    WN[W_DOOM]   =(Win){ 180,44,648,436,0,0 };
    { int sw=(int)W-80<1000?(int)W-80:1000, sh=(int)H-120<800?(int)H-120:800; if(sw<760)sw=760; if(sh<560)sh=560; int sy=((int)H-88-sh)/2; if(sy<12)sy=12; WN[W_SETTINGS]=(Win){ ((int)W-sw)/2,sy,sw,sh,0,0 }; }
    WN[W_ABOUT]  =(Win){ 320,160,470,320,0,0 };
    WN[W_CHATGPT]=(Win){ (int)W/2-240,90,480,520,0,0 };
    WN[W_PPTX]   =(Win){ (int)W/2-320,90,640,470,0,0 };
    focus=W_WELCOME;
}
static void draw_str_big(int x,int y,const char* t,u32 c,int sc){ dstr(x,y,t,c,sc); }
extern char __immutable_end[];   /* end of .text+.rodata+.sig (immutable at runtime) */
int g_sig_ok=1;
static int g_sig_enforce=1; int g_sig_tampered;
int sig_self_check(void){
    /* Recompute SHA-256 over the IMMUTABLE image region [0x100000 .. __immutable_end)
       with the 32-byte hash field zeroed, and compare to the stamped value.
       We deliberately hash only .text/.rodata/.sig, NOT .data — writable globals in
       .data change the instant the kernel starts, so hashing them would always fail. */
    if(g_buildsig.magic[0]!='N'||g_buildsig.magic[7]!='G') return 0;   /* unstamped */
    u64 base=0x100000ULL;
    u64 endv=(u64)__immutable_end;
    if(endv<=base) return 0;
    u32 sz=(u32)(endv-base); if(sz<64 || sz>0x4000000u) return 0;
    u8* img=(u8*)base;
    u64 sigaddr=(u64)(&g_buildsig);
    if(sigaddr<base) return 0;
    u32 sig_off=(u32)(sigaddr-base);
    u32 hash_off=sig_off+8+16+4+4;               /* magic+version+epoch+size */
    if(hash_off+32>sz) return 0;
    /* the sig struct is const/.sig so casting away const to zero the field is fine here */
    u8 saved[32]; for(int i=0;i<32;i++){ saved[i]=img[hash_off+i]; img[hash_off+i]=0; }
    u8 out[32]; sha256_hash(img,(int)sz,out);
    for(int i=0;i<32;i++) img[hash_off+i]=saved[i];
    for(int i=0;i<32;i++) if(out[i]!=saved[i]){ g_sig_tampered=1; return 0; }
    return 1;
}
static void skull_screen(void){
    for(u32 y=0;y<H;y++) for(u32 x=0;x<W;x++) SCENE[y*SW+x]=rgb(8,0,0);
    static const char* SK[28]={
        "........################........",
        ".....######################.....",
        "...##########################...",
        "..############################..",
        ".##############################.",
        "###############################.",
        "###############################.",
        "###############################.",
        "###############################.",
        "####ooooooo########ooooooo#####.",
        "###oooooooooo####oooooooooo####.",
        "###oooooooooo####oooooooooo####.",
        "###oooooooooo####oooooooooo####.",
        "####oooooooo######oooooooo#####.",
        "######oooo##########ooo########.",
        "##############oo##############. ",
        "#############oooo#############. ",
        "############oooooo############. ",
        "#############oooo#############. ",
        "##############################. ",
        ".############################.. ",
        ".####|##|##|##|##|##|##|##|###..",
        ".####|##|##|##|##|##|##|##|###..",
        "..###########################.. ",
        "...####|##|##|##|##|##|##|##... ",
        "....#######################.... ",
        ".....#####################..... ",
        ".......#################....... "
    };
    const int ROWS=28, COLS=32;
    int sc=2; int cw=8*sc, chh=14*sc;
    int gridw=COLS*cw, gridh=ROWS*chh;
    int block=gridh+140;                 /* skull + text total height */
    int top=((int)H-block)/2, left=((int)W-gridw)/2;
    u32 rng=0xC0FFEEu;
    for(int r=0;r<ROWS;r++){
        const char* row=SK[r];
        for(int c=0;c<COLS && row[c];c++){
            if(row[c]=='#'){
                rng=rng*1103515245u+12345u;
                int one=((rng>>16)&1);
                u32 col=one?rgb(190,255,200):rgb(80,180,105);
                char b[2]; b[0]=one?'1':'0'; b[1]=0;
                dstr(left+c*cw, top+r*chh, b, col, sc);
            }
        }
    }
    const char* t1="WRONG VERSION";
    dstr((int)W/2-slen(t1)*8*4/2, top+gridh+30, t1, rgb(255,55,55), 4);
    const char* t2="build signature invalid - kernel modified or corrupted";
    dstr((int)W/2-slen(t2)*8/2, top+gridh+95, t2, rgb(205,150,150), 1);
    const char* t3="Press R to restart, or C to start anyway (not safe)";
    dstr((int)W/2-slen(t3)*8/2, top+gridh+118, t3, rgb(235,200,200), 1);
    blit_all();

}
int g_ticks_live=1;
static void pit_poll_tick(void){ static u16 last=0; outb(0x43,0x00); u16 c=(u16)inb(0x40); c|=(u16)((u16)inb(0x40)<<8); if(c>last)g_ticks++; last=c; }
static int g_bootlog_y=200;
static void bootlog_line(const char* s){
    if(BYPP!=4||!FBB||g_bootlog_y+18>(int)H-60)return;
    fill_rect(40,g_bootlog_y,(int)W-80,16,0); dstr(40,g_bootlog_y,s,0x00C8D2DCu,1);
    for(int y=g_bootlog_y;y<g_bootlog_y+16;y++){ u32* d=(u32*)(FBB+(u64)y*PITCHB); u32* sc=SCENE+(u64)y*SW; if(g_fb_rgb)copy_swap32(d,sc,(int)W); else for(u32 x=0;x<W;x++)d[x]=sc[x]; }
    g_bootlog_y+=18;
}
volatile u32 g_ml_beat=0; volatile int g_ml_stage=0;
void kmain(void){
    /* zero .bss (flat image doesn't) - preserve the boot page tables up to boot_pt_end */
    { extern void early_idt_install(void); early_idt_install(); }
    { extern void early_remap(void); early_remap(); }
    { extern volatile int g_early_step; extern void early_mark(int,u32);
    g_early_step=1;
    { extern char __bss_end[]; extern char __bss_start[]; for(volatile u64* z=(u64*)__bss_start; (u64)z<(u64)__bss_end; z++) *z=0; }
    g_early_step=2; early_mark(6,0x00A040E0u);
    percpu_init_bsp();
    g_early_step=3; early_mark(7,0x0040E0E0u);
    inst_snapshot_data();
    g_early_step=4; early_mark(8,0x00FF60C0u);
    mm_load(); { extern void early_remap_fix_mm(void); early_remap_fix_mm(); extern void dmw_init(void); dmw_init(); }
    g_early_step=5; early_mark(9,0x00909090u);
    arch_fpu_init();
    g_early_step=6; }
    FBB=(u8*)(*(volatile u64*)0x5000ULL); PITCHB=*(volatile u32*)0x5008ULL;
    if(*(volatile u32*)0x502CULL==0x49464555u){ g_fb_rgb=(*(volatile u8*)0x5021ULL==1); g_uefi_ram_mb=*(volatile u32*)0x5028ULL;
        u32 mp=*(volatile u32*)0x5008ULL, mh=*(volatile u16*)0x500EULL; if(mh>40){ u8* r=FBB+(u64)(mh-36)*mp+(u64)(16+5*28)*4; for(int y=0;y<24;y++){ u32* q=(u32*)(r+(u64)y*mp); for(int x=0;x<24;x++)q[x]=0x00FFFFFFu; } } }
    W=*(volatile u16*)0x500CULL; H=*(volatile u16*)0x500EULL; BPP=*(volatile u8*)0x5020ULL;
    BYPP=(BPP>=32)?4:3; if(W>MAXW)W=MAXW; if(H>MAXH)H=MAXH; SW=W;
    if(!PITCHB)PITCHB=W*BYPP; if(!W||!H||!FBB){ for(;;)cpu_halt(); }
    FBB2=0; g_fb2_ok=0;
    { extern void igpu_early(void); igpu_early(); }
    if(*(volatile u32*)0x502CULL==0x49464555u && *(volatile u32*)0x5040ULL==0x4432584Eu && *(volatile u32*)0x5044ULL>0){
        u8* r=(u8*)0x5048ULL; u64 b=*(volatile u64*)(r+0); u32 p=*(volatile u32*)(r+8);
        u16 w2=*(volatile u16*)(r+12),h2=*(volatile u16*)(r+14); u8 bp=*(volatile u8*)(r+16),rg=*(volatile u8*)(r+17),mp=*(volatile u8*)(r+18);
        if(mp&&b&&w2&&h2){ FBB2=(u8*)b; BYPP2=(bp>=32)?4:3; PITCHB2=p?p:(u32)w2*BYPP2; W2=w2; H2=h2; g_fb2_rgb=(rg==1); g_mirror2=1; g_fb2_ok=1; }
    }
    g_klog_hook=bootlog_line;
    g_ram_mb=detect_ram_mb();
    klog_init(); klog("boot: kmain entered, fb ok"); power_init();
    if(g_fb2_ok){ char sb[72],*q=sb; q=scopy(q,"display: second head "); q=udec(q,W2); q=scopy(q,"x"); q=udec(q,H2); q=scopy(q," (mirroring)"); *q=0; klog(sb); }
    heap_init((int)g_ram_mb); klog("boot: heap ok"); { extern int cxx_run_ctors(void); cxx_run_ctors(); }
    { extern void slab_init(void); slab_init(); }   /* slab caches on top of the heap */            /* bring up the heap now that RAM size is known */
    pframe_init();                      /* physical frame allocator (userspace foundation) */
    if(mm_known()){ klog(g_heap_mapped?"memory: firmware map received, heap limited to usable RAM":"memory: firmware map received"); if(mm_area_conflicts()){ klog("memory: WARNING the firmware uses memory inside a kernel area - run memmap"); extern void mm_conflicts_log(void); mm_conflicts_log(); } }
    { extern const char* dmw_where(void); extern u32 g_devbuf; char db[120],*dq=db; dq=scopy(dq,"memory: "); dq=scopy(dq,dmw_where()); dq=scopy(dq," at 0x"); dq=hx(dq,g_devbuf,8); *dq=0; klog(db); }
    { extern u32 g_remap_n, g_remap_fail; extern u64 g_remap_lo, g_remap_badlo, g_remap_badhi; char rb[140],*q=rb;
      if(g_remap_n){ q=scopy(q,"memory: "); q=udec(q,g_remap_n*2); q=scopy(q," MB of kernel memory (0x"); q=hx(q,(u32)g_remap_badlo,8); q=scopy(q,"-0x"); q=hx(q,(u32)g_remap_badhi,8); q=scopy(q,") is not RAM here - moved to 0x"); q=hx(q,(u32)(g_remap_lo>>32),2); q=hx(q,(u32)g_remap_lo,8); *q=0; klog(rb); }
      else if(g_remap_fail){ q=scopy(q,"memory: kernel memory is not RAM here and no spare RAM above 4 GB was found to move it"); *q=0; klog(q==rb?"":rb); } }
    { char mb[96],*q=mb; q=scopy(q,"memory: "); q=udec(q,g_ram_mb); q=scopy(q," MB RAM, "); q=udec(q,pframe_free_count()); q=scopy(q," free frames, "); q=udec(q,pframe_hi_count()); q=scopy(q," above 4 GB"); *q=0; klog(mb); }
    klog(pf_ready?"boot: frame pool ON (>=500MB RAM)":"boot: frame pool OFF (<500MB RAM): userspace programs like ls/cat cannot run");

    get_brand();
    pci_scan(); klog("boot: pci scanned");
    pci_quiesce(); if(g_pci_quiesced)klog("boot: stopped DMA on devices the firmware left running");
    init_windows(); fs_init(); disk_init(); env_init(); vfs_init(); term_init();   /* gdisk (AHCI/NVMe) probed lazily, not at boot */
    klog("boot: windows/fs/disk/vfs/term ok");
    notify("Welcome to NoovexOS 64","Press the Win key for notifications.");
    build_bg();
    arch_idt_init();
    if(*(volatile u32*)0x502CULL==0x49464555u)apic_virtual_wire();
    { extern void ioapic_mask_all(void); extern int g_ioapic_n, g_ioapic_masked; ioapic_mask_all(); char ib[80],*ip=ib; ip=scopy(ip,"boot: I/O APIC lines masked: "); ip=udec(ip,(u32)g_ioapic_masked); ip=scopy(ip," of "); ip=udec(ip,(u32)g_ioapic_n); ip=scopy(ip," controllers"); *ip=0; klog(ib); }
    pic_remap(); pit_init(100); klog("boot: pic+pit ok");
    set_gate(HR_LAPIC_VEC,isr_lapic_timer); hr_init();
    if(inb(0x64)==0xFF){ klog("boot: no PS/2 controller (USB keyboard/mouse only)"); }
    else { for(int i=0;i<64&&(inb(0x64)&1);i++) inb(0x60); mouse_init(); klog("boot: ps2 mouse ok"); }
    xhci_mouse_autoinit(); klog("boot: xhci probe done");
    { extern void vm_mouse_init(void); vm_mouse_init(); }              /* auto-detect a USB mouse - no 'usb' command needed */
    g_acpi_pwrbtn_cb=acpi_power_button; acpi_aml_init(); klog("boot: ACPI AML done");
    { extern void laptop_init(void); laptop_init(); }
    { extern void igpu_init(void); igpu_init(); }
    tp_init(); klog("boot: touchpad probe done");
    battery_init();                     /* read battery % via the Embedded Controller */
    mx=(int)W/2; my=(int)H/2;
    klog("boot: splash start"); g_klog_hook=0; splash_boot(); klog("boot: splash done");                      /* Windows-style animated boot logo */
    g_sig_ok = sig_self_check(); klog(g_sig_ok?"boot: integrity check passed":g_sig_tampered?"boot: INTEGRITY CHECK FAILED - kernel code was modified":"boot: integrity check skipped (unstamped build)");

    { extern void live_init(void); extern void live_present(void); klog("boot: live_init start"); g_state=ST_LIVE; live_init(); klog("boot: live_init done");
      { extern int installed_boot(void); if(installed_boot())klog(g_state==ST_SETUP?"boot: installed system, first start - setup":"boot: installed system - login"); }
      if(g_state==ST_LIVE)live_present(); klog("boot: first frame presented"); }
    cpu_irq_on();
    { u64 t0=g_ticks; for(int i=0;i<3000000&&g_ticks==t0;i++) cpu_pause(); if(g_ticks==t0){ g_ticks_live=0; klog("boot: timer interrupts are not arriving - using a polled timer"); } }
    if(g_sig_enforce&&g_sig_tampered){ skull_screen(); klog("boot: waiting for R (restart) or C (continue)");
        for(;;){ if(xhid_n)xhci_mouse_poll(); int k=kpop(); if(k=='r'||k=='R')power_reboot(); if(k=='c'||k=='C'){ klog("boot: user chose to continue with a modified kernel"); break; } cpu_idle(); }
        { extern void live_invalidate(void); extern void live_present(void); live_invalidate(); live_present(); } }
    { extern void smp_boot_auto_c(void); smp_boot_auto_c(); }
    u64 last_frame=0, last_ns=0, frame_ns=16666667ULL; klog("boot: interrupts on, entering main loop");
    { extern void (*g_wait_pump)(void); extern void busy_pump(void); g_wait_pump=busy_pump; }
    for(;;){
        if(!g_ticks_live)pit_poll_tick();
        int ch=0;
        { static u64 hb=0; if(g_ticks-hb>=500){ hb=g_ticks; klog(g_state==ST_LIVE?"alive: main loop running (live)":"alive: main loop running"); } }
        g_ml_beat++; g_ml_stage=1; tp_poll(); g_ml_stage=2; etp_poll(); { extern void syn_poll(void); syn_poll(); } g_ml_stage=3; { extern void vm_mouse_poll(void); vm_mouse_poll(); }
        g_ml_stage=4; { static u64 ap=0; if(g_ticks-ap>=2){ ap=g_ticks; acpi_poll(); g_ml_stage=5; snd_poll(); } }
        g_ml_stage=6;{ extern void uvc_poll(void); extern void bench_tick(void); extern int bench_busy(void); extern u32 uvc_frames; static u32 lf=0; uvc_poll(); bench_tick(); if(WN[W_CAMERA].open&&!WN[W_CAMERA].min&&uvc_frames!=lf){ lf=uvc_frames; ch=1; } if(WN[W_BENCH].open&&bench_busy())ch=1; { extern int recorder_tick(void); if(recorder_tick())ch=1; } { extern int g_quake_state; if(WN[W_QUAKE].open&&!WN[W_QUAKE].min&&g_quake_state<3)ch=1; } { extern int writer_blink(void); static u64 lbk=0; if(writer_blink()){ u64 ph=hr_ns()/530000000ULL; if(ph!=lbk){ lbk=ph; ch=1; } } } }
        if(xhid_n) xhci_mouse_poll();
        else if(!jc_any_runnable()&&g_ticks_live){ int armed=0; if(g_state==ST_DESKTOP&&g_cursor_moved&&hr_precise())armed=hr_wake_at(last_ns+frame_ns); { int bd=bkl_drop(); cpu_halt(); bkl_take(bd); } if(armed)hr_wake_cancel(); }
        if(bkl_held())g_bkl.depth=1; else bkl_lock();
        bkl_yield();
        if(g_fs_wb_pending&&g_ticks>=g_fs_wb_due)nvx3_sync_all();
        g_ml_stage=7; if(g_state==ST_LIVE){ extern int live_input(void); ch=live_input(); }
        else if(g_state==ST_DESKTOP) ch=process_input();
        else if(g_state==ST_BOOTMENU) ch=bootmenu_input();
        else if(g_state==ST_RECOVERY) ch=recovery_input();
        else if(g_state==ST_DISKMGR) ch=dm_input();
        else if(g_state==ST_INSTALL) ch=ip_input();
        else if(g_state==ST_SETUP) ch=setup_input();
        else ch=login_input();
        if(g_state==ST_DESKTOP){ ux_desktop_start(g_user); static u64 jlast=0; static unsigned jtg=0; int jr=jc_step(); if(jr&2) ch=1; else if((jr&1)&&(g_term_gen!=jtg||g_ticks-jlast>=50)){ jtg=g_term_gen; jlast=g_ticks; ch=1; } }
        { static int prevst=-1; if(g_state==ST_LIVE&&prevst!=ST_LIVE){ live_invalidate(); ch=1; } prevst=g_state; }
        if(g_state==ST_LIVE){ static unsigned lg=0; static int lfg=0; jc_step();
            if(g_term_gen!=lg){ lg=g_term_gen; ch=1; }
            if((g_tty_fg!=0)!=lfg){ lfg=(g_tty_fg!=0); klog(lfg?"live: job took the terminal":"live: job finished, prompt back"); } }
        { static int cag=0; extern int g_gfx; extern void gfx_stop(const char*); if(kdown[0x1D]&&kdown[0x38]&&kdown[0x22]){ if(!cag&&g_gfx){ gfx_stop("turned off with Ctrl+Alt+G"); ch=1; if(g_state==ST_LIVE){ extern void live_invalidate(void); live_invalidate(); } } cag=1; } else cag=0; }
        { extern void xfull_tick(void); xfull_tick(); }
        { static int fbwas=0; if(g_fb0_open>0){ if(g_fb0_dirty){ g_fb0_dirty=0; blit_all(); } fbwas=1; ch=0; } else if(fbwas){ fbwas=0; ch=1; if(g_state==ST_LIVE) live_invalidate(); } }
        { extern void media_tick(void); media_tick(); }
        g_ml_stage=8; usb_hotplug_poll(); g_ml_stage=9;
        if(g_state==ST_DESKTOP){ extern void firefox_tick(void); firefox_tick(); }
        if(g_state==ST_DESKTOP){ battery_tick();   /* USB insert/remove + battery % */
            if(g_usb_arrived){ open_win(W_FILES); g_files_usb=1; g_usb_arrived=0; notify("USB drive","Flash drive connected"); ch=1; } }
        if(g_state==ST_DESKTOP&&g_fb0_open<=0&&!ch){ extern int video_fast(void); extern int browser_media_fast(void); if(video_fast()) ch=1; else if(browser_media_fast()) ch=1; else { extern int xdisp_fast(void); extern int xover_tick(void); if(xdisp_fast()) ch=1; else if(xover_tick()) ch=1; } }
        if(g_state==ST_DESKTOP&&g_fb0_open<=0){
            u64 mt=(g_refresh_hz>=120)?1:(g_refresh_hz>=60)?2:3; frame_ns=1000000000ULL/(u64)(g_refresh_hz>0?g_refresh_hz:60);
            int due=hr_precise()?(hr_ns()-last_ns>=frame_ns):((g_ticks-last_frame)>=mt);
            { extern int gfx_anim_active(void); if(!ch&&due&&gfx_anim_active())ch=1; }
            if(ch){ last_frame=g_ticks; last_ns=hr_ns(); compose_present(); g_cursor_moved=0; }        /* real change -> full frame */
            else if((g_ticks-last_frame)>=50){ last_frame=g_ticks; last_ns=hr_ns();                    /* ~0.5s heartbeat */
                int timebased = WN[W_DOOM].open || g_toast_cur>=0 || toast_t;         /* DOOM/toasts need full frames */
                { extern int g_gfx; if(timebased||g_gfx) compose_present(); else present_topbar(); }               /* else just tick the clock (cheap) */
                g_cursor_moved=0; }
            else if(g_cursor_moved && due){ last_ns=hr_ns();                         /* only the pointer moved -> dirty-rect */
                if(my > (int)H-96 || g_ctx || g_submenu || g_launcher || g_qs || g_fdrag || g_ddrag>=0 || g_prop){ compose_present(); }  /* dock/menus/drag: full frame */
                else present_cursor();
                g_cursor_moved=0; }
        } else if(ch){
            if(g_state==ST_LIVE){ extern void live_present(void); live_present(); } else if(g_state==ST_BOOTMENU) bootmenu_present(); else if(g_state==ST_RECOVERY) recovery_present(); else if(g_state==ST_DISKMGR) dm_present(); else if(g_state==ST_INSTALL) ip_present(); else if(g_state==ST_SETUP) setup_present(); else login_present();
        } else if(g_state==ST_INSTALL&&g_ip_curmoved){ g_ip_curmoved=0; present_cursor();
        }
    }
}
