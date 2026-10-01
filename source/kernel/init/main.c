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
    WN[W_SYSMON] =(Win){ 300,120,620,480,0,0 };
    WN[W_LOGS]   =(Win){ 240,90,680,520,0,0 };
    WN[W_TFIND]  =(Win){ 280,110,640,520,0,0 };
    WN[W_CALENDAR]=(Win){ 330,120,470,430,0,0 };
    WN[W_SNAKE]  =(Win){ 380,110,446,462,0,0 };
    WN[W_FLAPPY] =(Win){ 400,110,420,470,0,0 };
    WN[W_NEOFETCH]=(Win){ 300,90,600,540,0,0 };
    WN[W_PAINT]  =(Win){ 300,110,620,440,0,0 };
    WN[W_DOOM]   =(Win){ 180,44,648,436,0,0 };
    WN[W_SETTINGS]=(Win){ 200,36,860,680,0,0 };
    WN[W_ABOUT]  =(Win){ 320,160,470,320,0,0 };
    WN[W_CHATGPT]=(Win){ (int)W/2-240,90,480,520,0,0 };
    WN[W_PPTX]   =(Win){ (int)W/2-320,90,640,470,0,0 };
    focus=W_WELCOME;
}
static void draw_str_big(int x,int y,const char* t,u32 c,int sc){ dstr(x,y,t,c,sc); }
extern char __immutable_end[];   /* end of .text+.rodata+.sig (immutable at runtime) */
int g_sig_ok=1;      /* result of the boot-time integrity check */
static int g_sig_enforce=0; /* when 1, a failed check shows the skull and halts */
static int sig_self_check(void){
    /* Recompute SHA-256 over the IMMUTABLE image region [0x100000 .. __immutable_end)
       with the 32-byte hash field zeroed, and compare to the stamped value.
       We deliberately hash only .text/.rodata/.sig, NOT .data — writable globals in
       .data change the instant the kernel starts, so hashing them would always fail. */
    if(g_buildsig.magic[0]!='N'||g_buildsig.magic[7]!='G') return 0;   /* unstamped */
    u64 base=0x100000ULL;
    u64 endv=(u64)__immutable_end;
    if(endv<=base) return 0;
    u32 sz=(u32)(endv-base); if(sz<64 || sz>0x800000u) return 0;
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
    for(int i=0;i<32;i++) if(out[i]!=saved[i]) return 0;   /* mismatch = tampered */
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
    blit_all();

}
void kmain(void){
    /* zero .bss (flat image doesn't) - preserve page tables at 0xC00000-0xC06000 */
    { extern char __bss_end[]; for(volatile u64* z=(u64*)0xC06000ULL; (u64)z<(u64)__bss_end; z++) *z=0; }
    inst_snapshot_data();
    /* enable SSE/FPU so the Doom code (compiled with SSE) can run */
    { u64 cr0,cr4; __asm__ volatile("mov %%cr0,%0":"=r"(cr0)); cr0&=~(1ULL<<2); cr0|=(1ULL<<1); __asm__ volatile("mov %0,%%cr0"::"r"(cr0));
      __asm__ volatile("mov %%cr4,%0":"=r"(cr4)); cr4|=(1ULL<<9)|(1ULL<<10); __asm__ volatile("mov %0,%%cr4"::"r"(cr4));
      { u32 a,b,c,d; cpuid(1,&a,&b,&c,&d); g_ssse3=(c>>9)&1; }   /* SSSE3 -> pshufb for the 24bpp present */
      __asm__ volatile("fninit"); u32 mx=0x1F80; __asm__ volatile("ldmxcsr %0"::"m"(mx)); }
    FBB=(u8*)(*(volatile u64*)0x5000ULL); PITCHB=*(volatile u32*)0x5008ULL;
    W=*(volatile u16*)0x500CULL; H=*(volatile u16*)0x500EULL; BPP=*(volatile u8*)0x5020ULL;
    BYPP=(BPP>=32)?4:3; if(W>MAXW)W=MAXW; if(H>MAXH)H=MAXH; SW=W;
    if(!PITCHB)PITCHB=W*BYPP; if(!W||!H||!FBB){ for(;;)__asm__ volatile("hlt"); }
    g_ram_mb=detect_ram_mb();
    heap_init((int)g_ram_mb);            /* bring up the heap now that RAM size is known */
    pframe_init();                      /* physical frame allocator (userspace foundation) */
    /* NOTE: secondary cores are started on demand via the 'smpstart' command, not at
       boot -- auto-starting APs hangs on some firmware/VM configs. Boot stays single-core. */

    get_brand();
    pci_scan();
    init_windows(); fs_init(); disk_init(); env_init(); vfs_init(); term_init();   /* gdisk (AHCI/NVMe) probed lazily, not at boot */
    notify("Welcome to NoovexOS 64","Press the Win key for notifications.");
    build_bg();
    for(int i=0;i<48;i++) set_gate(i,isr_stubs[i]);
    for(int i=48;i<256;i++) set_gate(i,isr_ignore);
    struct idt_ptr ip={(u16)(sizeof(idt)-1),(u64)idt}; __asm__ volatile("lidt %0"::"m"(ip));
    pic_remap(); pit_init(100);
    while(inb(0x64)&1) inb(0x60);
    mouse_init();
    xhci_mouse_autoinit();              /* auto-detect a USB mouse - no 'usb' command needed */
    battery_init();                     /* read battery % via the Embedded Controller */
    mx=(int)W/2; my=(int)H/2;
    splash_boot();                      /* Windows-style animated boot logo */
    /* Integrity check: report a mismatch but DO NOT block a legitimate boot.
       (A halting self-hash is too fragile across loaders/load-addresses to gate boot on.)
       Set g_sig_enforce=1 only once the check is proven reliable on real hardware. */
    g_sig_ok = sig_self_check();

    g_state=ST_BOOTMENU; bootmenu_present();
    __asm__ volatile("sti");
    u64 last_frame=0;
    for(;;){
        int ch=0;
        if(xhid_n) xhci_mouse_poll();               /* poll USB HID every iteration */
        else if(!jc_any_runnable()) __asm__ volatile("hlt");
        if(g_state==ST_DESKTOP) ch=process_input();
        else if(g_state==ST_BOOTMENU) ch=bootmenu_input();
        else if(g_state==ST_RECOVERY) ch=recovery_input();
        else if(g_state==ST_DISKMGR) ch=dm_input();
        else if(g_state==ST_INSTALL) ch=ip_input();
        else if(g_state==ST_SETUP) ch=setup_input();
        else ch=login_input();
        if(g_state==ST_DESKTOP){ ux_desktop_start(g_user); if(jc_step()) ch=1; }
        { static int fbwas=0; if(g_fb0_open>0){ if(g_fb0_dirty){ g_fb0_dirty=0; blit_all(); } fbwas=1; ch=0; } else if(fbwas){ fbwas=0; ch=1; } }
        if(g_state==ST_DESKTOP){ usb_hotplug_poll(); battery_tick();   /* USB insert/remove + battery % */
            if(g_usb_arrived){ open_win(W_FILES); g_files_usb=1; g_usb_arrived=0; notify("USB drive","Flash drive connected"); ch=1; } }
        if(g_state==ST_DESKTOP&&g_fb0_open<=0){
            u64 mt=(g_refresh_hz>=120)?1:(g_refresh_hz>=60)?2:3;
            if(ch){ last_frame=g_ticks; compose_present(); g_cursor_moved=0; }        /* real change -> full frame */
            else if((g_ticks-last_frame)>=50){ last_frame=g_ticks;                    /* ~0.5s heartbeat */
                int timebased = WN[W_DOOM].open || g_toast_cur>=0 || toast_t;         /* DOOM/toasts need full frames */
                if(timebased) compose_present(); else present_topbar();               /* else just tick the clock (cheap) */
                g_cursor_moved=0; }
            else if(g_cursor_moved && (g_ticks-last_frame)>=mt){                       /* only the pointer moved -> dirty-rect */
                if(my > (int)H-96 || g_ctx || g_submenu || g_launcher || g_qs || g_fdrag || g_ddrag>=0 || g_prop){ compose_present(); }  /* dock/menus/drag: full frame */
                else present_cursor();
                g_cursor_moved=0; }
        } else if(ch){
            if(g_state==ST_BOOTMENU) bootmenu_present(); else if(g_state==ST_RECOVERY) recovery_present(); else if(g_state==ST_DISKMGR) dm_present(); else if(g_state==ST_INSTALL) ip_present(); else if(g_state==ST_SETUP) setup_present(); else login_present();
        }
    }
}
