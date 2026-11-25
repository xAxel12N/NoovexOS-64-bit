#include "include/nvx/base.h"
#include "include/nvx/lib/base/fmt.h"
#include "include/nvx/drivers/video/fb/framebuffer.h"
#include "include/nvx/drivers/video/fb/gfx.h"
#include "include/nvx/graphics/theme/theme.h"
#include "include/nvx/graphics/ui/wm.h"
#include "include/nvx/graphics/ui/desktop.h"
#include "include/nvx/shell/terminal.h"
#include "include/nvx/shell/commands.h"
#include "include/nvx/core/proc/jobs.h"
#include "include/nvx/apps/internet/browser.h"

extern volatile u64 g_ticks;
extern int g_gfx, g_gfx_fx;
extern int g_round_off;
void in_mark(const char*,int,const char*,int);

#define RUN_MAX 120
#define RUN_HIST 8
int g_run_on;
static char run_buf[RUN_MAX+1]; static int run_len, run_cur;
static char run_hist[RUN_HIST][RUN_MAX+1]; static int run_hn, run_hpos;
static int run_bprev;
static int rx, ry, rw, rh, rbx[2], rby, rbw, rbh;

void run_open(void){ g_run_on=1; run_len=0; run_cur=0; run_buf[0]=0; run_hpos=run_hn; run_bprev=1; }
void run_close(void){ g_run_on=0; }

static void run_hist_add(const char* s){
    if(run_hn>0&&seq(run_hist[run_hn-1],(char*)s))return;
    if(run_hn==RUN_HIST){ for(int i=1;i<RUN_HIST;i++)scopy(run_hist[i-1],run_hist[i]); run_hn--; }
    scopy(run_hist[run_hn++],s);
}
static void run_exec(void){
    char cl[RUN_MAX+1]; int n=0; int s=0; while(run_buf[s]==' ')s++;
    for(int i=s;i<run_len;i++)cl[n++]=run_buf[i]; while(n>0&&cl[n-1]==' ')n--; cl[n]=0;
    g_run_on=0; if(!n)return;
    run_hist_add(cl);
    open_win(W_TERM); term_puts(cl); term_putc('\n');
    in_mark("Terminal command (Run)",W_TERM,cl,-1); run_cmd(cl); if(!g_tty_fg)term_prompt();
}
static void run_set(const char* s){ run_len=0; while(s[run_len]&&run_len<RUN_MAX){ run_buf[run_len]=s[run_len]; run_len++; } run_buf[run_len]=0; run_cur=run_len; }

void run_draw(void){
    if(!g_run_on)return;
    int classic=(g_dstyle==1);
    rw=420; rh=168; rx=((int)W-rw)/2; ry=((int)H-rh)/2-40;
    u32 face=rgb(212,208,200), ink=rgb(0,0,0), dim=rgb(96,96,96);
    if(classic){ g_round_off=1; fill_rect(rx,ry,rw,rh,face); ui_bevel(rx,ry,rw,rh,0);
        for(int i=0;i<rw-6;i++){ int t=i*255/(rw-6); u32 c=rgb(110+(244-110)*t/255,34+(202-34)*t/255,9+(158-9)*t/255); fill_rect(rx+3+i,ry+3,1,20,c); }
        dstr(rx+10,ry+6,"Run",rgb(255,255,255),1);
        { int cx=rx+rw-22, cy=ry+5; ui_bevel(cx,cy,16,14,0); fill_rect(cx+2,cy+2,12,10,face); for(int t=0;t<6;t++){ fill_rect(cx+5+t,cy+3+t,1,1,ink); fill_rect(cx+10-t,cy+3+t,1,1,ink); } }
    } else { shadow_soft(rx,ry,rw,rh,14); fill_round(rx,ry,rw,rh,14,15,rgb(250,251,254)); dstr(rx+24,ry+18,"Run",INK,2); ink=INK; dim=rgb(120,128,146); }
    int ty=ry+(classic?34:50);
    dstr(rx+(classic?12:24),ty,"Type the name of a program or a command, and NoovexOS will run it.",classic?ink:dim,1);
    int fx=rx+(classic?12:24), fy=ty+22, fw=rw-(classic?24:48), fh=26;
    if(classic){ ui_bevel(fx,fy,fw,fh,1); fill_rect(fx+2,fy+2,fw-4,fh-4,rgb(255,255,255)); }
    else fill_round(fx,fy,fw,fh,8,15,rgb(238,241,246));
    int maxc=(fw-16)/8; int st=run_cur>maxc?run_cur-maxc:0;
    { int cx=fx+8; for(int k=st;k<run_len&&k-st<maxc;k++){ dchar(cx,fy+6,run_buf[k],classic?ink:rgb(40,46,64),1); cx+=8; } }
    if((g_ticks/50)&1){ int cx=fx+8+(run_cur-st)*8; fill_rect(cx,fy+5,2,16,classic?ink:ACC); }
    rbw=78; rbh=26; rby=ry+rh-rbh-12; rbx[0]=rx+rw-2*rbw-12-8; rbx[1]=rx+rw-rbw-12;
    static const char* L[2]={"OK","Cancel"};
    for(int k=0;k<2;k++){ int hov=in_rect(mx,my,rbx[k],rby,rbw,rbh);
        if(classic){ ui_bevel(rbx[k],rby,rbw,rbh,0); fill_rect(rbx[k]+2,rby+2,rbw-4,rbh-4,hov?rgb(226,222,214):face); if(k==0){ fill_rect(rbx[k]+3,rby+3,rbw-6,1,ink); fill_rect(rbx[k]+3,rby+3,1,rbh-6,ink); } dstr_ctr(rbx[k]+rbw/2,rby+5,L[k],ink,1); }
        else { fill_round(rbx[k],rby,rbw,rbh,8,15,k==0?ACC:(hov?rgb(226,230,238):rgb(236,239,245))); dstr_ctr(rbx[k]+rbw/2,rby+5,L[k],k==0?rgb(255,255,255):INK,1); } }
    if(run_hn)dstr(fx,rby+5,"Up/Down: earlier commands",dim,1);
    if(classic)g_round_off=0;
}
int run_mouse(int x,int y,int b){
    int down=(b&1)&&!(run_bprev&1); run_bprev=b;
    if(!down)return 1;
    if(in_rect(x,y,rbx[0],rby,rbw,rbh)){ run_exec(); return 1; }
    if(in_rect(x,y,rbx[1],rby,rbw,rbh)){ run_close(); return 1; }
    if(g_dstyle==1&&in_rect(x,y,rx+rw-22,ry+5,16,14)){ run_close(); return 1; }
    if(!in_rect(x,y,rx,ry,rw,rh))run_close();
    return 1;
}
int run_key(int c){
    if(c==27){ run_close(); return 1; }
    if(c=='\n'){ run_exec(); return 1; }
    if(c=='\b'){ if(run_cur>0){ for(int i=run_cur-1;i<run_len;i++)run_buf[i]=run_buf[i+1]; run_cur--; run_len--; } return 1; }
    if(c==14){ if(run_cur<run_len){ for(int i=run_cur;i<run_len;i++)run_buf[i]=run_buf[i+1]; run_len--; } return 1; }
    if(c==17){ if(run_cur>0)run_cur--; return 1; }
    if(c==18){ if(run_cur<run_len)run_cur++; return 1; }
    if(c==2){ run_cur=0; return 1; }
    if(c==5){ run_cur=run_len; return 1; }
    if(c==19){ if(run_hpos>0){ run_hpos--; run_set(run_hist[run_hpos]); } return 1; }
    if(c==20){ if(run_hpos<run_hn){ run_hpos++; if(run_hpos==run_hn)run_set(""); else run_set(run_hist[run_hpos]); } return 1; }
    if(c>=32&&c<127&&run_len<RUN_MAX){ for(int i=run_len;i>run_cur;i--)run_buf[i]=run_buf[i-1]; run_buf[run_cur++]=(char)c; run_len++; run_buf[run_len]=0; return 1; }
    return 1;
}

static const char HELP_HTML[]=
"<!doctype html><html><head><title>NoovexOS Help</title>"
"<style>body{font-family:Arial,sans-serif;margin:0;background:#f4f6fb;color:#1f2937}main{max-width:820px;margin:32px auto;padding:0 20px}"
"h1{font-size:28px;margin:0 0 18px}h2{font-size:18px;margin:0 0 10px}.card{background:#fff;border-radius:12px;box-shadow:0 2px 10px rgba(0,0,0,.08);padding:16px 20px;margin-bottom:14px}"
".muted{color:#6b7280;font-size:14px}.row{display:flex;justify-content:space-between;gap:12px;padding:6px 0;border-bottom:1px solid #eef0f4}.row:last-child{border-bottom:0}"
"code{background:#eef2ff;padding:1px 5px;border-radius:4px}kbd{background:#e5e7eb;border-radius:4px;padding:1px 6px;font-family:monospace}</style></head><body><main>"
"<h1>NoovexOS Help</h1>"
"<div class=card><h2>Getting around</h2>"
"<p>The <b>Start</b> button at the left of the taskbar opens the menu with every application, grouped by category. "
"The taskbar shows the open windows; click one to bring it to the front. The tray at the right has the network, sound, power and notification icons and the clock.</p>"
"<p>Right-click the desktop for the desktop menu: <b>New</b> creates files on the desktop, <b>Personalize</b> and <b>Display settings</b> open Settings, and <b>Change wallpaper</b> cycles the built-in wallpapers.</p>"
"<p>Drag a window to the left or right edge of the screen to snap it to that half, or to the top to maximise it.</p></div>"
"<div class=card><h2>Keyboard shortcuts</h2>"
"<div class=row><span><kbd>Super</kbd></span><span>Open or close the Start menu</span></div>"
"<div class=row><span><kbd>Super</kbd> + <kbd>R</kbd></span><span>Run a command</span></div>"
"<div class=row><span><kbd>Super</kbd> + <kbd>E</kbd></span><span>Files</span></div>"
"<div class=row><span><kbd>Super</kbd> + <kbd>I</kbd></span><span>Settings</span></div>"
"<div class=row><span><kbd>Super</kbd> + <kbd>D</kbd></span><span>Show the desktop</span></div>"
"<div class=row><span><kbd>Super</kbd> + <kbd>L</kbd></span><span>Lock the screen</span></div>"
"<div class=row><span><kbd>Super</kbd> + <kbd>Left</kbd> / <kbd>Right</kbd> / <kbd>Up</kbd></span><span>Snap the window left or right, or maximise it</span></div>"
"<div class=row><span><kbd>Alt</kbd> + <kbd>Tab</kbd></span><span>Switch between windows</span></div>"
"<div class=row><span><kbd>Alt</kbd> + <kbd>F4</kbd></span><span>Close the window</span></div>"
"<div class=row><span><kbd>Ctrl</kbd> + <kbd>Shift</kbd> + <kbd>Esc</kbd></span><span>Task Manager</span></div>"
"<div class=row><span><kbd>Ctrl</kbd> + <kbd>1</kbd> / <kbd>2</kbd></span><span>Switch workspace</span></div>"
"<div class=row><span><kbd>Ctrl</kbd> + <kbd>Alt</kbd> + <kbd>F2</kbd> &hellip; <kbd>F6</kbd></span><span>Text console; <kbd>F7</kbd> returns to the desktop</span></div>"
"<div class=row><span><kbd>Esc</kbd></span><span>Close a menu, popup or dialog</span></div></div>"
"<div class=card><h2>Terminal</h2>"
"<p>Type <code>help</code> in the Terminal for the list of commands. The usual Unix tools are there: <code>ls</code>, <code>cat</code>, <code>cd</code>, <code>mkdir</code>, <code>cp</code>, <code>mv</code>, <code>rm</code>, <code>grep</code>, <code>ps</code>, <code>kill</code> and more.</p>"
"<p>Useful commands: <code>neofetch</code> (system summary), <code>net</code> and <code>wifi</code> (network), <code>disks</code> (storage), <code>log</code> (kernel log), <code>settings</code> (open Settings), <code>browse &lt;url&gt;</code> (open a web page), <code>update</code> (NoovexOS updates), <code>noovexos_install</code> (install to disk).</p>"
"<p>Commands can be chained with <code>|</code>, redirected with <code>&gt;</code> and run in the background with <code>&amp;</code>. <kbd>Ctrl</kbd> + <kbd>C</kbd> stops the running command.</p></div>"
"<div class=card><h2>Files</h2>"
"<p>Your home folder has Desktop, Documents, Downloads, Pictures, Music and Videos. Files on the desktop live in <code>~/Desktop</code>. "
"Right-click a file for Open, Rename, Copy, Cut, Delete and Properties; deleted files go to the Trash. Double-click a text file to open it in the Editor, an image to view it, and a web page to open it in the Browser.</p></div>"
"<div class=card><h2>Power</h2>"
"<p><b>Start &gt; Exit...</b> offers Shut down, Restart, Sleep, Log out and Lock. The power button on the computer opens the same dialog; pressing it twice shuts down.</p></div>"
"<div class=card><h2>About</h2>"
"<p class=muted>NoovexOS 64 is a 64-bit operating system written from scratch. <b>Start &gt; About NoovexOS</b> shows the version and the hardware this computer is running on. Settings &gt; About has more detail.</p></div>"
"</main></body></html>";

void help_open(void){ int n=0; while(HELP_HTML[n])n++; browser_show_html(HELP_HTML,n,"NoovexOS Help"); }
