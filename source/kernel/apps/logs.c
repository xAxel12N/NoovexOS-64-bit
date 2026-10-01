/* ================= Logs app: live low-level system activity ================= */
static void logs_draw(int i){
    win_frame(i,"Logs - System Activity Monitor");
    int x=WN[i].x+16, y=WN[i].y+TITLEH+12, w=WN[i].w-32;
    fill_round(x,y,w,WN[i].h-TITLEH-24,8,15,rgb(12,14,20));
    int lx=x+14, ly=y+14; char b[128],*p;
    tcol(WHT);
    #define LOG(col) tcol(col); term_puts(b); ly+=16
    /* header */
    dstr(lx,ly,"NoovexOS live activity  (byte/address level)",rgb(120,255,180),1); ly+=22;
    /* memory map */
    p=b;p=scopy(p,"KERNEL   base=0x00100000  size=");p=udec(p,g_buildsig.image_size);p=scopy(p," bytes");*p=0; dstr(lx,ly,b,rgb(180,200,255),1); ly+=16;
    p=b;p=scopy(p,"DMA arena 0x09000000-0x0A000000   RAM=");p=udec(p,g_ram_mb);p=scopy(p," MB");*p=0; dstr(lx,ly,b,rgb(180,200,255),1); ly+=16;
    { u64 cr3; __asm__ volatile("mov %%cr3,%0":"=r"(cr3));
      p=b;p=scopy(p,"CR3 (page dir) = 0x");p=hx(p,(u32)(cr3>>32),8);p=hx(p,(u32)cr3,8);*p=0; dstr(lx,ly,b,rgb(255,220,150),1); ly+=16; }
    { u64 sp; __asm__ volatile("mov %%rsp,%0":"=r"(sp));
      p=b;p=scopy(p,"kernel RSP     = 0x");p=hx(p,(u32)(sp>>32),8);p=hx(p,(u32)sp,8);*p=0; dstr(lx,ly,b,rgb(255,220,150),1); ly+=16; }
    p=b;p=scopy(p,"uptime ticks   = ");p=udec(p,(u32)g_ticks);*p=0; dstr(lx,ly,b,rgb(200,255,200),1); ly+=16;
    ly+=6; dstr(lx,ly,"-- memory hexdump @ 0x00100000 --",rgb(120,140,180),1); ly+=18;
    /* live hexdump of the kernel's first bytes, updates each frame */
    volatile u8* mem=(volatile u8*)0x00100000ULL; int base=(int)((g_ticks/30)%256)*16;
    for(int r=0;r<8;r++){ p=b; u32 a=0x00100000u+base+r*16; p=hx(p,a,8); *p++=':';*p++=' ';
        for(int c=0;c<16;c++){ p=hx(p,mem[base+r*16+c],2); *p++=' '; } *p++=' ';
        for(int c=0;c<16;c++){ u8 ch=mem[base+r*16+c]; *p++=(ch>=32&&ch<127)?ch:'.'; } *p=0;
        dstr(lx,ly,b,rgb(120,230,150),1); ly+=15; }
    ly+=6; p=b;p=scopy(p,"disk=");p=scopy(p,disk_ok?"NVXFS mounted":"none");p=scopy(p,"   net=");p=scopy(p,net_ok?"up":"down");p=scopy(p,"   wifi bus3 RTL8821CE");*p=0;
    dstr(lx,ly,b,rgb(200,200,220),1); ly+=16;
    tcol(WHT);
    #undef LOG
}

/* ================= Terminal Finder: hidden developer commands ================= */
static const char* TF_CMD[]={
    "wifi init/scan/wpa","bring up + scan + join Wi-Fi (RTL8821CE)",
    "wifi rx","raw baseband frame counter (RE debug)",
    "wifi cryptotest","verify WPA2 crypto vs RFC vectors",
    "sig","show the kernel build signature (SHA-256)",
    "crypto","list crypto suite + run self-test",
    "run <ELF>","execute an ELF64 from USB/FAT",
    "spawn <ELF>","fork+exec a child process, wait for it",
    "mkdir/rm/mv/cp/touch","coreutils on the directory tree",
    "chmod <oct> <f>","set Unix permission bits",
    "su / whoami / id","switch user, show identity",
    "env / export","environment variables",
    "ls /dev","device files: null zero random full tty",
    "cat /dev/random","read cryptographic random bytes",
    "fatls / fatcat","list/read USB FAT files",
    "gdisk / install","AHCI/NVMe disks + installer",
    "h2c / efuse / macinit","low-level Wi-Fi MAC bring-up",
    0,0
};
static void tfind_draw(int i){
    win_frame(i,"Terminal Finder - Hidden Developer Commands");
    int x=WN[i].x+16, y=WN[i].y+TITLEH+12, w=WN[i].w-32;
    fill_round(x,y,w,WN[i].h-TITLEH-24,8,15,rgb(18,20,28));
    int lx=x+16, ly=y+16;
    dstr(lx,ly,"Type these in the Terminal. Hidden commands not shown in menus.",rgb(150,200,255),1); ly+=24;
    for(int k=0; TF_CMD[k]; k+=2){
        int rowy=ly+(k/2)*30;
        if(rowy>WN[i].h+WN[i].y-40)break;
        fill_round(lx-4,rowy-4,w-24,26,6,15,rgb(28,32,44));
        dstr(lx+4,rowy+2,TF_CMD[k],rgb(120,255,180),1);
        dstr(lx+230,rowy+2,TF_CMD[k+1],rgb(190,196,214),1);
    }
}

static void draw_app(int i){
    if(i==W_WELCOME)welcome_draw(i); else if(i==W_TERM)term_draw(i); else if(i==W_FILES)files_draw(i);
    else if(i==W_EDITOR)editor_draw(i); else if(i==W_CALC)calc_draw(i); else if(i==W_BROWSER)browser_draw(i);
    else if(i==W_CLOCK)clock_draw(i); else if(i==W_SYSMON)sysmon_draw(i);
    else if(i==W_CALENDAR)calendar_draw(i); else if(i==W_SNAKE)snake_draw(i); else if(i==W_FLAPPY)flappy_draw(i);
    else if(i==W_NEOFETCH)neofetch_draw(i); else if(i==W_PAINT)paint_draw(i);
    else if(i==W_DOOM)doom_draw(i);
    else if(i==W_SETTINGS)settings_draw(i); else if(i==W_CHATGPT)chatgpt_draw(i); else if(i==W_PPTX)pptx_draw(i); else if(i==W_LOGS)logs_draw(i); else if(i==W_TFIND)tfind_draw(i); else about_draw(i);
}
static void app_click(int i,int px,int py){
    if(i==W_WELCOME){ if(in_rect(px,py,gb_x,gb_y,gb_w,gb_h)) open_win(W_TERM); }
    else if(i==W_FILES) files_click(px,py);
    else if(i==W_EDITOR){ if(in_rect(px,py,eb_save_x,eb_save_y,eb_bw,eb_bh)) editor_save();
                          else if(in_rect(px,py,eb_run_x,eb_run_y,eb_bw,eb_bh)) editor_run();
                          else if(ed_show_con && in_rect(px,py,eb_conx_x-4,eb_conx_y-2,18,16)) ed_show_con=0;
                          else if(in_rect(px,py,eb_new_x,eb_new_y,eb_bw,eb_bh)){ editor_reset(); ed_file=-1; ed_show_con=0; } }
    else if(i==W_CALC) calc_click(px,py);
    else if(i==W_SYSMON) sysmon_click(px,py);
    else if(i==W_DOOM){ if(g_doom_state==2) g_doom_mcap=1; }        /* click in DOOM -> capture mouse */
    else if(i==W_BROWSER) browser_click(px,py);
    else if(i==W_CHATGPT) chatgpt_click(px,py);
    else if(i==W_PPTX) pptx_click(px,py);
    else if(i==W_CALENDAR){ if(in_rect(px,py,cal_lx,cal_ly,cal_aw2,30)) cal_key(17); else if(in_rect(px,py,cal_rx,cal_ry,cal_aw2,30)) cal_key(18); }
    else if(i==W_FLAPPY) flappy_flap();
    else if(i==W_PAINT){
        for(int k=0;k<12;k++) if(in_rect(px,py,pt_colx[k],pt_coly[k],20,20)){ paint_col=k; return; }
        for(int k=0;k<3;k++) if(in_rect(px,py,pt_bx[k],pt_by+k*26-11,44,22)){ paint_size=k; return; }
        if(in_rect(px,py,pt_clrx,pt_clry,pt_clrw,pt_clrh)){ paint_clear(); return; }
        paint_at(px,py); }
    else if(i==W_SNAKE){ if(snake_state==2) snake_reset(); }
    else if(i==W_SETTINGS) settings_click(px,py);
}

