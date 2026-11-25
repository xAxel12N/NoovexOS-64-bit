#include "include/nvx/base.h"
#include "include/nvx/lib/base/klibc.h"
#include "include/nvx/lib/base/fmt.h"
#include "include/nvx/drivers/video/fb/framebuffer.h"
#include "include/nvx/drivers/net/ethernet/nic.h"
#include "include/nvx/net/http/http.h"
#include "include/nvx/graphics/theme/theme.h"
#include "include/nvx/graphics/ui/wm.h"
#include "include/nvx/apps/office/calculator.h"
#include "include/nvx/apps/office/calculator.h"
#include "include/nvx/shell/terminal.h"
#include "include/nvx/fs/nvx/nvxfs.h"
#include "include/nvx/core/buildsig.h"
#include "include/nvx/shell/commands.h"
#include "include/nvx/apps/office/editor.h"
#include "include/nvx/apps/internet/browser.h"
#include "include/nvx/apps/system/settings.h"
#include "include/nvx/apps/system/files_classic.h"
#include "include/nvx/apps/system/file_explorer.h"
#include "include/nvx/apps/accessories/small_apps.h"
#include "include/nvx/apps/games/doom.h"
#include "include/nvx/apps/system/logs.h"
#include "include/nvx/apps/media/videoplayer.h"
static char logs_buf[16385];
static int logs_lines(int* starts,int* lens,int maxl){
    extern int klog_ring(char*,int,int); extern int kaslr_mask(char*,int);
    int n=klog_ring(logs_buf,16384,1); if(n<0)n=0; logs_buf[n]=0; kaslr_mask(logs_buf,n);
    int cnt=0, st=0;
    for(int i=0;i<=n;i++){ if(i==n||logs_buf[i]=='\n'){ if(i>st){ if(cnt<maxl){ starts[cnt]=st; lens[cnt]=i-st; cnt++; } else { for(int k=1;k<maxl;k++){ starts[k-1]=starts[k]; lens[k-1]=lens[k]; } starts[maxl-1]=st; lens[maxl-1]=i-st; } } st=i+1; } }
    return cnt; }
static void logs_draw(int i){
    win_frame(i,"Logs");
    int x=WN[i].x+16, y=WN[i].y+TITLEH+12, w=WN[i].w-32, h=WN[i].h-TITLEH-24;
    fill_round(x,y,w,h,8,15,rgb(12,14,20));
    int lx=x+14, ly=y+14; char b[160],*p;
    p=b; p=scopy(p,"Kernel log   uptime "); p=udec(p,(u32)(g_ticks/100)); p=scopy(p," s   RAM "); p=udec(p,g_ram_mb); p=scopy(p," MB   disk "); p=scopy(p,disk_ok?"mounted":"none"); p=scopy(p,"   net "); p=scopy(p,net_ok?"up":"down"); *p=0;
    dstr(lx,ly,b,rgb(120,255,180),1); ly+=22;
    static int starts[512], lens[512];
    int rows=(y+h-10-ly)/15; if(rows<1)rows=1; if(rows>512)rows=512;
    int cols=(w-28)/8; if(cols<16)cols=16; if(cols>159)cols=159;
    int cnt=logs_lines(starts,lens,rows);
    if(!cnt){ dstr(lx,ly,"The kernel log is empty.",rgb(120,140,180),1); return; }
    for(int k=0;k<cnt;k++){
        const char* s0=logs_buf+starts[k]; int L=lens[k];
        if(L>3&&s0[0]=='<'&&s0[2]=='>'){ s0+=3; L-=3; }
        int n=L<cols?L:cols; for(int c=0;c<n;c++){ char ch=s0[c]; b[c]=(ch>=32&&ch<127)?ch:' '; } b[n]=0;
        u32 col=rgb(190,196,214);
        if(strstr(b,"fail")||strstr(b,"error")||strstr(b,"refused")||strstr(b,"panic"))col=rgb(255,150,150);
        else if(strstr(b,"warn")||strstr(b,"timeout")||strstr(b,"retry"))col=rgb(255,220,150);
        dstr(lx,ly,b,col,1); ly+=15; }
}

/* ================= Terminal Finder: hidden developer commands ================= */
static const char* TF_CMD[]={
    "wifi init/scan/wpa","bring up + scan + join Wi-Fi (RTL8821CE)",
    "wifi rx","raw baseband frame counter (RE debug)",
    "wifi cryptotest","verify WPA2 crypto vs RFC vectors",
    "iwx status/up","Intel AX200/AX210 Wi-Fi probe + firmware boot",
    "mtwifi status/up","MediaTek MT7921/MT7922 Wi-Fi probe + firmware boot",
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

void draw_app(int i){
    if(i==W_WELCOME)welcome_draw(i); else if(i==W_TERM)term_draw(i); else if(i==W_FILES)files_draw(i);
    else if(i==W_EDITOR)editor_draw(i); else if(i==W_CALC)calc_draw(i); else if(i==W_BROWSER)browser_draw(i);
    else if(i==W_CLOCK)clock_draw(i); else if(i==W_SYSMON)sysmon_draw(i);
    else if(i==W_CALENDAR)calendar_draw(i); else if(i==W_SNAKE)snake_draw(i); else if(i==W_FLAPPY)flappy_draw(i);
    else if(i==W_NEOFETCH)neofetch_draw(i); else if(i==W_PAINT)paint_draw(i);
    else if(i==W_DOOM)doom_draw(i);
    else if(i==W_SETTINGS)settings_draw(i); else if(i==W_PPTX)pptx_draw(i); else if(i==W_LOGS)logs_draw(i); else if(i==W_CHATGPT){ extern void regedit_draw(int); regedit_draw(i); } else if(i==W_TFIND)tfind_draw(i); else if(i==W_VIDEO)video_draw(i); else if(i==W_CAMERA){ extern void camera_draw(int); camera_draw(i); } else if(i==W_GPU3D){ extern void gpu3d_draw(int); gpu3d_draw(i); } else if(i==W_BENCH){ extern void bench_draw(int); bench_draw(i); } else if(i==W_RECORDER){ extern void recorder_draw(int); recorder_draw(i); } else if(i==W_WRITER){ extern void writer_draw(int); writer_draw(i); } else if(i==W_QUAKE){ extern void quake_draw(int); quake_draw(i); } else if(i==W_XDISP){ extern void xdisp_draw(int); xdisp_draw(i); } else about_draw(i);
}
void app_click(int i,int px,int py){
    if(i==W_WELCOME){ if(in_rect(px,py,gb_x,gb_y,gb_w,gb_h)) open_win(W_TERM); }
    else if(i==W_FILES) files_click(px,py);
    else if(i==W_EDITOR){ extern int editor_click(int,int); editor_click(px,py); }
    else if(i==W_CALC) calc_click(px,py);
    else if(i==W_SYSMON) sysmon_click(px,py);
    else if(i==W_DOOM) doom_click();
    else if(i==W_BROWSER) browser_click(px,py);
    else if(i==W_PPTX) pptx_click(px,py);
    else if(i==W_VIDEO) video_click(px,py);
    else if(i==W_CAMERA){ extern void camera_click(int,int); camera_click(px,py); }
    else if(i==W_BENCH){ extern void bench_click(int,int); bench_click(px,py); }
    else if(i==W_RECORDER){ extern void recorder_click(int,int); recorder_click(px,py); }
    else if(i==W_WRITER){ extern void writer_click(int,int); writer_click(px,py); }
    else if(i==W_QUAKE){ extern void quake_click(void); quake_click(); }
    else if(i==W_GPU3D){ extern void gpu3d_click(void); gpu3d_click(); }
    else if(i==W_CALENDAR){ if(in_rect(px,py,cal_lx,cal_ly,cal_aw2,30)) cal_key(17); else if(in_rect(px,py,cal_rx,cal_ry,cal_aw2,30)) cal_key(18); }
    else if(i==W_FLAPPY) flappy_flap();
    else if(i==W_PAINT){
        for(int k=0;k<12;k++) if(in_rect(px,py,pt_colx[k],pt_coly[k],20,20)){ paint_col=k; return; }
        for(int k=0;k<3;k++) if(in_rect(px,py,pt_bx[k],pt_by+k*26-11,44,22)){ paint_size=k; return; }
        if(in_rect(px,py,pt_clrx,pt_clry,pt_clrw,pt_clrh)){ paint_clear(); return; }
        paint_at(px,py); }
    else if(i==W_SNAKE){ if(snake_state!=1) snake_start(); }
    else if(i==W_SETTINGS) settings_click(px,py);
    else if(i==W_CHATGPT){ extern void regedit_click(int,int); regedit_click(px,py); }
}

