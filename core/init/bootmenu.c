#include "include/nvx/base.h"
#include "include/nvx/lib/base/fmt.h"
#include "include/nvx/drivers/video/fb/framebuffer.h"
#include "include/nvx/arch/x86_64/cpu/cpuinfo.h"
#include "include/nvx/drivers/bus/pci.h"
#include "include/nvx/drivers/net/ethernet/nic_names.h"
#include "include/nvx/drivers/usb/xhci.h"
#include "include/nvx/net/http/http.h"
#include "include/nvx/drivers/input/ps2/ps2.h"
#include "include/nvx/graphics/theme/theme.h"
#include "include/nvx/graphics/ui/wm.h"
#include "include/nvx/drivers/storage/ata.h"
#include "include/nvx/fs/nvx/nvxfs.h"
#include "include/nvx/fs/fat/fat_ata.h"
#include "include/nvx/core/power.h"
#include "include/nvx/apps/system/files_classic.h"
#include "include/nvx/core/init/splash.h"
#include "include/nvx/core/init/bootmenu.h"
#include "include/nvx/core/crash.h"
#include "include/nvx/core/secboot.h"
#include "include/nvx/core/init/main.h"
const char* BM_ITEMS[5]={"NoovexOS Desktop","NoovexOS (Safe Mode)","Recovery Environment","Install NoovexOS","Boot Configuration"};
int g_bootsel=0, g_bootcfg=0, g_bootmsg=0;
int bm_bx,bm_by[5],bm_bw,bm_bh, cfg_tgx,cfg_tgy,cfg_tgw,cfg_tgh, cfg_bkx,cfg_bky,cfg_bkw,cfg_bkh, msg_okx,msg_oky,msg_okw,msg_okh;
int g_recmode=0;
int g_rec_openterm=0;
int g_recsel=0, g_recinfo=0;
int g_rec_ret=ST_SETUP, g_rec_crash=0;
enum { RM_MAIN, RM_MORE };
enum { RA_CONTINUE, RA_RESTART, RA_SHUTDOWN, RA_CMDPROMPT, RA_SAFEMODE, RA_STARTUPREPAIR, RA_DIAG, RA_REPORT,
       RA_MORE, RA_SYSRESTORE, RA_RESETPC, RA_UEFI, RA_RESETDISP, RA_BACK };
typedef struct { const char* t; const char* s; int act; } RItem;
static const RItem rm_main[9]={
  {"Continue to NoovexOS","Start normally",RA_CONTINUE},
  {"Start in Safe Mode","Plain desktop, no effects",RA_SAFEMODE},
  {"Startup Repair","Check the boot files, the disk and the kernel signature",RA_STARTUPREPAIR},
  {"Show the crash report","What stopped the last start",RA_REPORT},
  {"Command Prompt","Safe Mode with a terminal open",RA_CMDPROMPT},
  {"Startup Diagnostics","CPU, memory, display, disks, network, USB",RA_DIAG},
  {"More options","Display mode, restore, reset, firmware",RA_MORE},
  {"Restart","Restart the computer",RA_RESTART},
  {"Turn off","Shut down the computer",RA_SHUTDOWN} };
static const RItem rm_more[5]={
  {"Reset Display Mode","Read the screen mode from the boot loader again",RA_RESETDISP},
  {"System Restore","Go back to a saved point",RA_SYSRESTORE},
  {"Reset this PC","Reinstall NoovexOS",RA_RESETPC},
  {"UEFI Firmware Settings","How to open the firmware setup",RA_UEFI},
  {"Back","Return to the main list",RA_BACK} };
static const RItem* rmenu(int m,int* n){ if(m==RM_MORE){ *n=5; return rm_more; } *n=9; return rm_main; }
static int g_recmenu=RM_MAIN;
static int rec_bx[10], rec_by[10], rec_bw2, rec_bh2;
#define RC_BG rgb(27,27,27)
#define RC_BAR rgb(37,37,37)
#define RC_LINE rgb(58,58,58)
#define RC_PANEL rgb(34,34,34)
#define RC_TXT rgb(212,212,212)
#define RC_HI rgb(230,230,230)
#define RC_DIM rgb(150,150,150)
#define RC_SEL rgb(42,56,70)
#define RC_SELB rgb(61,90,120)
#define RC_OK rgb(141,181,141)
#define RC_WARN rgb(201,166,90)
#define RC_BAD rgb(204,112,112)
static const char* rc_report(int* n){ static const char* r; static int rn, got; if(!got){ r=crash_report_text(&rn); got=1; } *n=rn; return r; }
static int rc_sc(void){ return W>=1600?2:1; }
static void rc_frame(int* l,int* fh,const char* crumb){
    int sc=rc_sc(); fill_rect(0,0,(int)W,(int)H,RC_BG);
    int bh=22+14*sc; fill_rect(0,0,(int)W,bh,RC_BAR); fill_rect(0,bh,(int)W,1,RC_LINE);
    int x=dstr(24,(bh-14*sc)/2,"NoovexOS Recovery",RC_HI,sc); x=dstr(x+8*sc,(bh-14*sc)/2,"11.22",RC_DIM,sc);
    if(crumb){ x=dstr(x+16*sc,(bh-14*sc)/2,">",RC_DIM,sc); dstr(x+8*sc,(bh-14*sc)/2,crumb,RC_DIM,sc); }
    int fy=(int)H-14*sc-18; fill_rect(24,fy-12,(int)W-48,1,RC_LINE);
    dstr(24,fy,g_recmode?"[Esc] Back":"[Up/Down] Choose   [Enter] Select   [Esc] Back",RC_DIM,sc);
    *l=(int)W>=1600?120:40; *fh=fy-24; }
static void rc_box(int x,int y,int w,int h,u32 bg,u32 bd){ fill_rect(x,y,w,h,bd); fill_rect(x+1,y+1,w-2,h-2,bg); }
static int rc_lines(int x,int y,const char* s,u32 c,int sc,int maxch,int maxl){
    int n=0; char ln[160];
    while(*s&&n<maxl){ int k=0; while(*s&&*s!='\n'){ if(k<maxch&&k<159)ln[k++]=*s=='\t'?' ':*s; s++; } ln[k]=0; if(*s=='\n')s++; dstr(x,y,ln,c,sc); y+=(14+4)*sc; n++; }
    return y; }
static void rc_state(int x,int* y,const char* tag,u32 tc,const char* what,int sc){ dstr(x,*y,tag,tc,sc); dstr(x+9*8*sc,*y,what,RC_TXT,sc); *y+=22*sc; }
static void rc_panel(void){
    int sc=rc_sc(), l, fh; const char* crumb=g_recmode==1?"Startup Diagnostics":g_recmode==3?"Crash report":g_recinfo==0?"Startup Repair":g_recinfo==1?"System Restore":g_recinfo==2?"Reset this PC":"UEFI Firmware Settings";
    rc_frame(&l,&fh,crumb); int y=60+14*sc*2, x=l; char b[160],*p;
    dstr(x,y,crumb,RC_HI,sc+1); y+=14*(sc+1)+20*sc;
    int pw=(int)W-2*l, px=l, py=y, ph=fh-py; rc_box(px,py,pw,ph,RC_PANEL,RC_LINE); x=px+20*sc; y=py+18*sc;
    if(g_recmode==1){
        p=b;p=scopy(p,g_brand[0]?g_brand:"x86-64");*p=0; rc_state(x,&y,"CPU",RC_DIM,b,sc);
        p=b; if(g_ram_mb>=1024){p=udec(p,g_ram_mb/1024);*p++='.';*p++=(char)('0'+(int)((g_ram_mb%1024)*10/1024));p=scopy(p," GB");}else{p=udec(p,g_ram_mb);p=scopy(p," MB");} *p=0; rc_state(x,&y,"Memory",RC_DIM,b,sc);
        p=b;p=udec(p,W);*p++='x';p=udec(p,H);p=scopy(p,", ");p=udec(p,BPP);p=scopy(p,"-bit");*p=0; rc_state(x,&y,"Display",RC_DIM,b,sc);
        p=b; if(disk_ok&&g_disk_sectors){u32 mb=(u32)((u64)g_disk_sectors*512/1048576); p=udec(p,mb);p=scopy(p," MB, NoovexOS installed");} else p=scopy(p,"no installed NoovexOS disk"); *p=0; rc_state(x,&y,"Disk",RC_DIM,b,sc);
        { int k=nic_index(); rc_state(x,&y,"Network",RC_DIM,k>=0?(char*)nic_name(pci_ven[k],pci_dev[k]):"none",sc); }
        p=b;p=scopy(p,xhci_present?"controller found":"no controller");p=scopy(p,xhid_n?", keyboard/mouse active":"");p=scopy(p,msd_ok?", USB stick mounted":"");*p=0; rc_state(x,&y,"USB",RC_DIM,b,sc);
    } else if(g_recmode==3){
        int n=0; const char* r=rc_report(&n);
        if(!n)dstr(x,y,"No crash report was found on the disk or the USB stick.",RC_TXT,sc);
        else rc_lines(x,y,r,RC_TXT,sc,(pw-40*sc)/(8*sc),(ph-36*sc)/(18*sc));
    } else if(g_recinfo==0){
        int fok=fat_mount();
        rc_state(x,&y,fok?"[ OK ]":"[ -- ]",fok?RC_OK:RC_WARN,fok?"Boot file system mounted":"Boot file system not found (no installed disk)",sc);
        rc_state(x,&y,disk_ok?"[ OK ]":"[ -- ]",disk_ok?RC_OK:RC_WARN,disk_ok?"Installed NoovexOS disk found":"No installed NoovexOS disk (started from USB or CD)",sc);
        { const char* t; u32 c; const char* w;
          if(g_sb_flags&SB_KERNEL_OK){ t="[ OK ]"; c=RC_OK; w=(g_sb_flags&SB_SYSROOT_OK)?"Kernel signature checked by the loader, Linux files match":"Kernel signature checked by the loader"; }
          else if(g_sb_flags&SB_OVERRIDE){ t="[FAIL]"; c=RC_BAD; w="Kernel signature failed, started as untrusted by the user"; }
          else if(g_sig_ok){ t="[ OK ]"; c=RC_OK; w="Kernel build checksum OK (legacy BIOS start, no signature check)"; }
          else { t="[FAIL]"; c=RC_BAD; w="Kernel build checksum does not match"; }
          rc_state(x,&y,t,c,w,sc); }
        { int n=0; rc_report(&n); rc_state(x,&y,n?"[ !! ]":"[ OK ]",n?RC_WARN:RC_OK,n?"A crash report from an earlier start exists (Show the crash report)":"No crash report saved",sc); }
        y+=10*sc; dstr(x,y,"Nothing was changed. Repairs that change files come in a later version.",RC_DIM,sc);
    } else if(g_recinfo==1){ dstr(x,y,"No restore points are available on this system.",RC_TXT,sc); }
    else if(g_recinfo==2){ dstr(x,y,"Reset is not available here yet. Reinstall from a USB stick to start over.",RC_TXT,sc); y+=22*sc; dstr(x,y,"Your disks were not touched.",RC_DIM,sc); }
    else { dstr(x,y,"Restart and press the setup key (often F2, F1 or Del) while the computer starts.",RC_TXT,sc); }
    rec_bw2=12*8*sc; rec_bh2=14*sc+16; rec_bx[0]=px+pw-rec_bw2-20*sc; rec_by[0]=py+ph-rec_bh2-16*sc;
    int hb=in_rect(mx,my,rec_bx[0],rec_by[0],rec_bw2,rec_bh2);
    rc_box(rec_bx[0],rec_by[0],rec_bw2,rec_bh2,hb?rgb(52,52,52):rgb(46,46,46),rgb(74,74,74)); dstr_ctr(rec_bx[0]+rec_bw2/2,rec_by[0]+8,"Back",RC_TXT,sc);
    draw_cursor(); blit_all(); }
void recovery_present(void){
    if(g_recmode){ rc_panel(); return; }
    int sc=rc_sc(), l, fh; rc_frame(&l,&fh,g_recmenu==RM_MORE?"More options":0);
    int y=40+14*sc*2, x=l;
    dstr(x,y,g_recmenu==RM_MORE?"More options":g_rec_crash?"NoovexOS stopped and restarted into Recovery":"Recovery",RC_HI,sc+1); y+=14*(sc+1)+12*sc;
    dstr(x,y,g_recmenu==RM_MORE?"Less common tools.":g_rec_crash?"The last start ended with a crash. Your files have not been changed.":"Choose what to do.",RC_DIM,sc); y+=24*sc;
    int lw=(int)W-2*l; if(g_rec_crash&&g_recmenu==RM_MAIN){ int lw2=lw*58/100; lw=lw2; }
    if(g_rec_crash&&g_recmenu==RM_MAIN){
        int px=l+lw+24*sc, pw=(int)W-l-px, py=y, ph=fh-py; rc_box(px,py,pw,ph,RC_PANEL,RC_LINE);
        dstr(px+16*sc,py+14*sc,"Crash report",RC_DIM,sc);
        int n=0; const char* r=rc_report(&n);
        if(!n)dstr(px+16*sc,py+40*sc,"The report could not be read.",RC_TXT,sc);
        else rc_lines(px+16*sc,py+40*sc,r,RC_TXT,1,(pw-32*sc)/8,(ph-56*sc)/18); }
    int n; const RItem* it=rmenu(g_recmenu,&n);
    int rh=(14+12)*sc+14*sc, gap=6*sc; if(y+n*(rh+gap)>fh){ rh=(fh-y)/n-gap; if(rh<14*sc+10)rh=14*sc+10; }
    rec_bw2=lw; rec_bh2=rh;
    for(int k=0;k<n;k++){ int ry=y+k*(rh+gap); int hov=in_rect(mx,my,l,ry,lw,rh), sel=(k==g_recsel);
        if(sel)rc_box(l,ry,lw,rh,RC_SEL,RC_SELB); else rc_box(l,ry,lw,rh,hov?rgb(40,40,40):RC_PANEL,RC_LINE);
        int ty=ry+(rh-(14*sc*2+4*sc))/2; if(rh<14*sc*2+10*sc)ty=ry+(rh-14*sc)/2;
        dstr(l+16*sc,ty,(char*)it[k].t,RC_HI,sc);
        if(rh>=14*sc*2+10*sc)dstr(l+16*sc,ty+14*sc+4*sc,(char*)it[k].s,RC_DIM,1);
        rec_bx[k]=l; rec_by[k]=ry; }
    draw_cursor(); blit_all(); }
static void recovery_act(int act){
    switch(act){
      case RA_CONTINUE: g_rec_crash=0; g_state=g_rec_ret; break;
      case RA_SAFEMODE: g_rec_crash=0; g_safemode=1; g_state=g_rec_ret; break;
      case RA_CMDPROMPT: g_rec_crash=0; g_safemode=1; g_state=g_rec_ret; if(g_rec_ret==ST_LIVE){ extern void open_win(int); open_win(W_TERM); } else g_rec_openterm=1; break;
      case RA_DIAG: g_recmode=1; break;
      case RA_REPORT: g_recmode=3; break;
      case RA_RESETDISP: FBB=(u8*)(*(volatile u64*)0x5000ULL); PITCHB=*(volatile u32*)0x5008ULL; W=*(volatile u16*)0x500CULL; H=*(volatile u16*)0x500EULL; BPP=*(volatile u8*)0x5020ULL; BYPP=(BPP>=32)?4:3; if(W>MAXW)W=MAXW; if(H>MAXH)H=MAXH; SW=W; { extern void fb_shadow_invalidate(void); fb_shadow_invalidate(); } break;
      case RA_RESTART: power_reboot(); break;
      case RA_SHUTDOWN: power_off(); break;
      case RA_STARTUPREPAIR: g_recmode=2; g_recinfo=0; break;
      case RA_SYSRESTORE: g_recmode=2; g_recinfo=1; break;
      case RA_RESETPC: g_recmode=2; g_recinfo=2; break;
      case RA_UEFI: g_recmode=2; g_recinfo=3; break;
      case RA_MORE: g_recmenu=RM_MORE; g_recsel=0; break;
      case RA_BACK: g_recmenu=RM_MAIN; g_recsel=0; break;
    }
}
void recovery_after_crash(void){ g_rec_ret=g_state; g_rec_crash=1; g_recmenu=RM_MAIN; g_recmode=0; g_recsel=0; g_state=ST_RECOVERY; recovery_present(); }
int recovery_input(void){
    int changed=0;
    if(mouse_evt){ int dx,dy,btn; cpu_irq_off(); dx=mouse_dx;dy=mouse_dy;mouse_dx=0;mouse_dy=0;btn=mouse_btn;mouse_evt=0; cpu_irq_on();
        if(dx||dy){ mx+=dx;my-=dy; if(mx<0)mx=0;if(my<0)my=0;if(mx>=(int)W)mx=W-1;if(my>=(int)H)my=H-1; changed=1; }
        if(btn&&!mouse_btn_prev){
            if(g_recmode){ if(in_rect(mx,my,rec_bx[0],rec_by[0],rec_bw2,rec_bh2)) g_recmode=0; changed=1; }
            else { int n; const RItem* it=rmenu(g_recmenu,&n); for(int k=0;k<n;k++) if(in_rect(mx,my,rec_bx[k],rec_by[k],rec_bw2,rec_bh2)){ g_recsel=k; recovery_act(it[k].act); changed=1; break; } } }
        mouse_btn_prev=btn;
    }
    int c; while((c=kpop())>=0){ changed=1;
        if(g_recmode){ if(c==27||c=='\n') g_recmode=0; continue; }
        int n; const RItem* it=rmenu(g_recmenu,&n);
        if(c==19){ g_recsel=(g_recsel+n-1)%n; } else if(c==20){ g_recsel=(g_recsel+1)%n; }
        else if(c==27){ recovery_act(RA_BACK); }
        else if(c=='\n'){ recovery_act(it[g_recsel].act); }
    }
    return changed;
}
