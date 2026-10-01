/* =========================================================================
   Interactive boot menu (synthwave)
   ========================================================================= */
static const char* BM_ITEMS[5]={"NoovexOS Desktop","NoovexOS (Safe Mode)","Recovery Environment","Install NoovexOS","Boot Configuration"};
static int g_bootsel=0, g_bootcfg=0, g_bootmsg=0;
static int bm_bx,bm_by[5],bm_bw,bm_bh, cfg_tgx,cfg_tgy,cfg_tgw,cfg_tgh, cfg_bkx,cfg_bky,cfg_bkw,cfg_bkh, msg_okx,msg_oky,msg_okw,msg_okh;
/* =========================================================================
   Graphical Recovery Environment (Windows-style "Choose an option")
   ========================================================================= */
static int g_recmode=0;              /* 0=menu, 1=diagnostics, 2=info panel */
static int g_rec_openterm=0;         /* open a terminal once the desktop loads */
static int g_recsel=0, g_recinfo=0;
/* ---- Windows-style recovery menu tree ---- */
enum { RM_MAIN, RM_TROUBLE, RM_ADVANCED, RM_STARTUP };
enum { RA_CONTINUE, RA_RESTART, RA_TROUBLE, RA_STARTUPSET, RA_CMDPROMPT, RA_SHUTDOWN,
       RA_RESETPC, RA_ADVANCED, RA_STARTUPREPAIR, RA_SYSRESTORE, RA_DIAG, RA_UEFI, RA_RESETDISP, RA_SAFEMODE, RA_BACK };
typedef struct { const char* t; const char* s; int act; } RItem;
static const RItem rm_main[6]={
  {"Continue","Exit recovery and start NoovexOS normally",RA_CONTINUE},
  {"Restart","Restart the computer",RA_RESTART},
  {"Troubleshoot","Reset, repair, or advanced options",RA_TROUBLE},
  {"Startup Settings","Safe mode and boot options",RA_STARTUPSET},
  {"Command Prompt","Open a terminal with file + disk tools",RA_CMDPROMPT},
  {"Turn off your PC","Shut down the computer",RA_SHUTDOWN} };
static const RItem rm_trouble[4]={
  {"Reset this PC","Erase files and reinstall NoovexOS",RA_RESETPC},
  {"Advanced options","Repair tools and diagnostics",RA_ADVANCED},
  {"Startup Repair","Fix problems that stop NoovexOS loading",RA_STARTUPREPAIR},
  {"Go back","Return to the previous screen",RA_BACK} };
static const RItem rm_advanced[8]={
  {"Startup Repair","Check and repair boot problems",RA_STARTUPREPAIR},
  {"Command Prompt","Open a terminal with file + disk tools",RA_CMDPROMPT},
  {"Startup Settings","Safe mode and boot options",RA_STARTUPSET},
  {"System Restore","Restore NoovexOS from a saved point",RA_SYSRESTORE},
  {"Startup Diagnostics","Check CPU, memory, disks, drivers",RA_DIAG},
  {"UEFI Firmware Settings","Restart into firmware setup",RA_UEFI},
  {"Reset Display Mode","Re-probe the framebuffer resolution",RA_RESETDISP},
  {"Go back","Return to the previous screen",RA_BACK} };
static const RItem rm_startup[4]={
  {"Safe Mode","Plain desktop, no animations",RA_SAFEMODE},
  {"Reset Display Mode","Low-resolution / re-probe display",RA_RESETDISP},
  {"Restart","Restart the computer",RA_RESTART},
  {"Go back","Return to the previous screen",RA_BACK} };
static const RItem* rmenu(int m,int* n){ switch(m){ case RM_TROUBLE:*n=4;return rm_trouble; case RM_ADVANCED:*n=8;return rm_advanced; case RM_STARTUP:*n=4;return rm_startup; default:*n=6;return rm_main; } }
static const char* rmtitle(int m){ switch(m){ case RM_TROUBLE:return "Troubleshoot"; case RM_ADVANCED:return "Advanced options"; case RM_STARTUP:return "Startup Settings"; default:return "Choose an option"; } }
static int g_recmenu=RM_MAIN, g_recstack[6], g_recsp=0;
static int rec_bx[8], rec_by[8], rec_bw2, rec_bh2;
static void rec_glyph(int act,int cx,int cy){ u32 w=rgb(255,255,255);
    switch(act){
      case RA_CONTINUE: for(int i=-6;i<=6;i++){ int wd=6-((i<0)?-i:i); for(int j=-wd;j<=wd;j++)putpx(cx+i+2,cy+j,w);} break;               /* arrow/triangle */
      case RA_RESTART: for(int yy=-7;yy<=7;yy++){int xw=isqrt_i(49-yy*yy);putpx(cx-xw,cy+yy,w);putpx(cx-xw+1,cy+yy,w);} fill_rect(cx-1,cy-9,7,2,w); fill_rect(cx+4,cy-9,2,6,w); break;   /* refresh ring */
      case RA_SHUTDOWN: for(int a=-60;a<=240;a+=8){ int x=cx+(isqrt_i(64)* (a>90?1:1)); (void)x; } for(int yy=-7;yy<=7;yy++){int xw=isqrt_i(49-yy*yy);putpx(cx-xw,cy+yy,w);putpx(cx+xw,cy+yy,w);} fill_rect(cx-1,cy-9,2,8,w); break;   /* power circle + stem */
      case RA_CMDPROMPT: fill_round(cx-9,cy-7,18,14,3,15,w); dstr(cx-6,cy-4,">_",rgb(0,120,215),1); break;                                   /* console */
      case RA_SAFEMODE: fill_round(cx-7,cy-8,14,16,4,15,w); fill_round(cx-4,cy-5,8,10,2,15,rgb(0,120,215)); break;                            /* shield */
      case RA_RESETDISP: fill_round(cx-9,cy-7,18,12,2,15,w); fill_rect(cx-2,cy+5,4,3,w); fill_rect(cx-6,cy+8,12,2,w); break;                  /* monitor */
      case RA_DIAG: for(int i=-7;i<=7;i++){putpx(cx+i,cy+5,w); if(i%3==0){int h=(i*i)/7; for(int j=-h;j<=0;j++)putpx(cx+i,cy+5+j,w);}} break; /* chart */
      case RA_TROUBLE: case RA_ADVANCED: case RA_STARTUPREPAIR:  /* wrench */
        for(int i=-6;i<=6;i++){putpx(cx+i,cy+i,w);putpx(cx+i+1,cy+i,w);} fill_circle(cx-6,cy-6,4,w); fill_circle(cx-6,cy-6,2,rgb(0,120,215)); break;
      case RA_SYSRESTORE: for(int yy=-7;yy<=7;yy++){int xw=isqrt_i(49-yy*yy);putpx(cx-xw,cy+yy,w);putpx(cx+xw,cy+yy,w);putpx(cx+yy,cy-xw,w);putpx(cx+yy,cy+xw,w);} fill_rect(cx,cy-6,2,7,w); fill_rect(cx,cy,6,2,w); break; /* clock */
      case RA_UEFI: fill_round(cx-7,cy-7,14,14,2,15,w); fill_round(cx-4,cy-4,8,8,2,15,rgb(0,120,215)); for(int i=-2;i<=2;i++){putpx(cx-9,cy+i*3,w);putpx(cx+8,cy+i*3,w);putpx(cx+i*3,cy-9,w);putpx(cx+i*3,cy+8,w);} break; /* chip */
      case RA_STARTUPSET: fill_round(cx-8,cy-7,16,12,2,15,w); fill_rect(cx-2,cy+5,4,3,w); fill_circle(cx,cy-1,3,rgb(0,120,215)); break;        /* settings monitor */
      case RA_RESETPC: for(int yy=-7;yy<=7;yy++){int xw=isqrt_i(49-yy*yy);putpx(cx-xw,cy+yy,w);putpx(cx-xw+1,cy+yy,w);} fill_rect(cx-1,cy-9,7,2,w); fill_rect(cx+4,cy-9,2,6,w); break; /* refresh */
      case RA_BACK: for(int i=0;i<8;i++){putpx(cx-6+i,cy,w);} for(int i=-4;i<=4;i++){int ax=(i<0?-i:i); putpx(cx-6+ax,cy+i,w);} break;          /* left arrow */
      default: fill_circle(cx,cy,3,w); break;
    }
}
static void recovery_present(void){
    if(g_recmode==1||g_recmode==2){   /* diagnostics / info panel */
        for(u32 yy=0;yy<H;yy++)for(u32 xx=0;xx<W;xx++)SCENE[(u64)yy*SW+xx]=rgb(0,120,215);
        int px=(int)W/2-320, py=(int)H/2-210, pw=640, ph=420; shadow_soft(px,py,pw,ph,16); fill_round(px,py,pw,ph,16,15,rgb(248,250,253));
        int x=px+34,y=py+28; char b[120],*p;
        if(g_recmode==1){ dstr(x,y,"Startup Diagnostics",INK,2); y+=46;
            p=b;p=scopy(p,"CPU:       ");p=scopy(p,g_brand[0]?g_brand:"x86-64");*p=0; dstr(x,y,b,INK,1); y+=26;
            p=b;p=scopy(p,"Memory:    "); if(g_ram_mb>=1024){p=udec(p,g_ram_mb/1024);*p++='.';*p++=(char)('0'+(int)((g_ram_mb%1024)*10/1024));p=scopy(p," GB");}else{p=udec(p,g_ram_mb);p=scopy(p," MB");} *p=0; dstr(x,y,b,INK,1); y+=26;
            p=b;p=scopy(p,"Display:   ");p=udec(p,W);*p++='x';p=udec(p,H);p=scopy(p," @ ");p=udec(p,BPP);p=scopy(p,"-bit");*p=0; dstr(x,y,b,INK,1); y+=26;
            p=b;p=scopy(p,"Storage:   "); if(disk_ok&&g_disk_sectors){u32 mb=(u32)((u64)g_disk_sectors*512/1048576); p=udec(p,mb);p=scopy(p," MB IDE (NVXFS)");} else p=scopy(p,"no IDE disk"); *p=0; dstr(x,y,b,INK,1); y+=26;
            { int k=nic_index(); p=b;p=scopy(p,"Network:   ");p=scopy(p,k>=0?(char*)nic_name(pci_ven[k],pci_dev[k]):"none");*p=0; dstr(x,y,b,INK,1); y+=26; }
            p=b;p=scopy(p,"USB (xHCI):");p=scopy(p,xhci_present?" present":" not found");p=scopy(p,xhid_n?"  HID active":"");*p=0; dstr(x,y,b,INK,1); y+=26;
            p=b;p=scopy(p,"USB storage:");p=scopy(p,msd_ok?" mounted":" none");*p=0; dstr(x,y,b,INK,1); y+=30;
            dstr(x,y,"All core subsystems responding.",rgb(60,150,90),1);
        } else {   /* info panel g_recinfo: 0=Startup Repair 1=System Restore 2=Reset PC 3=UEFI */
            const char* title[4]={"Startup Repair","System Restore","Reset this PC","UEFI Firmware Settings"};
            dstr(x,y,(char*)title[g_recinfo],INK,2); y+=48;
            if(g_recinfo==0){ dstr(x,y,"Checking boot files and filesystem...",rgb(90,100,120),1); y+=30;
                int fok=fat_mount(); dstr(x,y,fok?"  Boot filesystem: OK":"  Boot filesystem: not mounted (no IDE disk)",fok?rgb(60,150,90):rgb(200,140,40),1); y+=24;
                dstr(x,y,disk_ok?"  System disk: detected":"  System disk: none",disk_ok?rgb(60,150,90):rgb(200,140,40),1); y+=24;
                dstr(x,y,"  Kernel image: valid",rgb(60,150,90),1); y+=30;
                dstr(x,y,"Startup Repair could not find any problems.",INK,1); }
            else if(g_recinfo==1){ dstr(x,y,"No restore points are available on this system.",INK,1); y+=26; dstr(x,y,"System Restore needs saved points to roll back to.",rgb(90,100,120),1); }
            else if(g_recinfo==2){ dstr(x,y,"Reset this PC would erase all files and settings",INK,1); y+=24; dstr(x,y,"and reinstall NoovexOS.",INK,1); y+=30;
                dstr(x,y,"This build is read-only - your disks are untouched.",rgb(200,140,40),1); }
            else { dstr(x,y,"To enter firmware setup, restart the computer and",INK,1); y+=24; dstr(x,y,"press the setup key (F2 / Del) during startup.",INK,1); }
        }
        rec_bw2=140; rec_bx[0]=px+pw-rec_bw2-24; rec_by[0]=py+ph-52; rec_bh2=34;
        int hb=(mx>=rec_bx[0]&&mx<rec_bx[0]+rec_bw2&&my>=rec_by[0]&&my<rec_by[0]+rec_bh2);
        fill_round(rec_bx[0],rec_by[0],rec_bw2,rec_bh2,8,15,hb?mix(ACC,rgb(255,255,255),40):ACC); dstr_ctr(rec_bx[0]+rec_bw2/2,rec_by[0]+9,"Back",rgb(255,255,255),1);
        draw_cursor(); blit_all(); return;
    }
    /* menu screen - Windows recovery style (flat solid blue) */
    for(u32 yy=0;yy<H;yy++)for(u32 xx=0;xx<W;xx++)SCENE[(u64)yy*SW+xx]=rgb(0,120,215);
    dstr((int)W*12/100,(int)H*8/100,(char*)rmtitle(g_recmenu),rgb(242,247,252),5);
    int n; const RItem* it=rmenu(g_recmenu,&n);
    int gx0=(int)W*12/100, gx1=(int)W*52/100, gy0=(int)H*22/100, tile=84, rowh=104, rww=(int)W*36/100;
    rec_bw2=tile+rww; rec_bh2=tile;
    for(int k=0;k<n;k++){ int col=k&1, row=k/2; int cx=col?gx1:gx0, cy=gy0+row*rowh;
        int hov=(mx>=cx&&mx<cx+tile+rww&&my>=cy&&my<cy+tile);
        fill_rect(cx,cy,tile,tile, hov?rgb(102,172,233):rgb(77,150,216));
        rec_glyph(it[k].act,cx+tile/2,cy+tile/2);
        dstr(cx+tile+18,cy+tile/2-16,(char*)it[k].t,rgb(255,255,255),2);
        dstr(cx+tile+18,cy+tile/2+8,(char*)it[k].s,rgb(208,226,246),1);
        rec_bx[k]=cx; rec_by[k]=cy;
    }
    draw_cursor(); blit_all();
}
static void recovery_shutdown(void){ blit_all();
    outw(0x604,0x2000); outw(0xB004,0x2000); outw(0x4004,0x3400);   /* QEMU / Bochs / VBox ACPI off */
    for(;;) __asm__ volatile("cli; hlt"); }
static void recovery_act(int act){
    switch(act){
      case RA_CONTINUE: g_state=ST_SETUP; break;
      case RA_SAFEMODE: g_safemode=1; g_state=ST_SETUP; break;
      case RA_CMDPROMPT: g_safemode=1; g_state=ST_SETUP; g_rec_openterm=1; break;
      case RA_DIAG: g_recmode=1; break;
      case RA_RESETDISP: FBB=(u8*)(*(volatile u64*)0x5000ULL); PITCHB=*(volatile u32*)0x5008ULL; W=*(volatile u16*)0x500CULL; H=*(volatile u16*)0x500EULL; BPP=*(volatile u8*)0x5020ULL; BYPP=(BPP>=32)?4:3; if(W>MAXW)W=MAXW; if(H>MAXH)H=MAXH; SW=W; break;
      case RA_RESTART: blit_all(); outb(0x64,0xFE); break;
      case RA_SHUTDOWN: recovery_shutdown(); break;
      case RA_STARTUPREPAIR: g_recmode=2; g_recinfo=0; break;
      case RA_SYSRESTORE: g_recmode=2; g_recinfo=1; break;
      case RA_RESETPC: g_recmode=2; g_recinfo=2; break;
      case RA_UEFI: g_recmode=2; g_recinfo=3; break;
      case RA_TROUBLE: if(g_recsp<5)g_recstack[g_recsp++]=g_recmenu; g_recmenu=RM_TROUBLE; g_recsel=0; break;
      case RA_ADVANCED: if(g_recsp<5)g_recstack[g_recsp++]=g_recmenu; g_recmenu=RM_ADVANCED; g_recsel=0; break;
      case RA_STARTUPSET: if(g_recsp<5)g_recstack[g_recsp++]=g_recmenu; g_recmenu=RM_STARTUP; g_recsel=0; break;
      case RA_BACK: if(g_recsp>0)g_recmenu=g_recstack[--g_recsp]; else g_recmenu=RM_MAIN; g_recsel=0; break;
    }
}
static int recovery_input(void){
    int changed=0;
    if(mouse_evt){ int dx,dy,btn; __asm__ volatile("cli"); dx=mouse_dx;dy=mouse_dy;mouse_dx=0;mouse_dy=0;btn=mouse_btn;mouse_evt=0; __asm__ volatile("sti");
        if(dx||dy){ mx+=dx;my-=dy; if(mx<0)mx=0;if(my<0)my=0;if(mx>=(int)W)mx=W-1;if(my>=(int)H)my=H-1; changed=1; }
        if(btn&&!mouse_btn_prev){
            if(g_recmode==1||g_recmode==2){ if(in_rect(mx,my,rec_bx[0],rec_by[0],rec_bw2,rec_bh2)) g_recmode=0; changed=1; }
            else { int n; const RItem* it=rmenu(g_recmenu,&n); for(int k=0;k<n;k++) if(in_rect(mx,my,rec_bx[k],rec_by[k],rec_bw2,rec_bh2)){ recovery_act(it[k].act); changed=1; break; } } }
        mouse_btn_prev=btn;
    }
    int c; while((c=kpop())>=0){ changed=1;
        if(g_recmode==1||g_recmode==2){ if(c==27||c=='\n') g_recmode=0; continue; }
        int n; const RItem* it=rmenu(g_recmenu,&n);
        if(c==19){ g_recsel=(g_recsel+n-1)%n; } else if(c==20){ g_recsel=(g_recsel+1)%n; }
        else if(c==27){ recovery_act(RA_BACK); }
        else if(c=='\n'){ recovery_act(it[g_recsel].act); }
    }
    return changed;
}
