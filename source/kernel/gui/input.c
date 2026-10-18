#include "include/nvx/base.h"
#include "include/nvx/mm/pmm_vmm.h"
#include "include/nvx/core/proc/sched.h"
#include "include/nvx/lib/fmt.h"
#include "include/nvx/drivers/video/framebuffer.h"
#include "include/nvx/drivers/rtc/cmos.h"
#include "include/nvx/drivers/usb/xhci.h"
#include "include/nvx/drivers/input/input.h"
#include "include/nvx/net/http.h"
#include "include/nvx/drivers/input/ps2.h"
#include "include/nvx/gui/theme.h"
#include "include/nvx/gui/wm.h"
#include "include/nvx/fs/ramfs.h"
#include "include/nvx/shell/terminal.h"
#include "include/nvx/drivers/wifi/rtw8821ce.h"
#include "include/nvx/core/syscall.h"
#include "include/nvx/drivers/input/tp_gesture.h"
#include "include/nvx/core/proc/jobs.h"
#include "include/nvx/fs/unixfs.h"
#include "include/nvx/shell/commands.h"
#include "include/nvx/apps/editor.h"
#include "include/nvx/apps/chatgpt.h"
#include "include/nvx/apps/browser.h"
#include "include/nvx/apps/settings.h"
#include "include/nvx/apps/files_classic.h"
#include "include/nvx/apps/file_explorer.h"
#include "include/nvx/apps/small_apps.h"
#include "include/nvx/apps/doom.h"
#include "include/nvx/apps/logs.h"
#include "include/nvx/gui/desktop.h"
#include "include/nvx/gui/input.h"
/* =========================================================================
   Input
   ========================================================================= */
/* ================= Lua graphics canvas (a window Lua scripts draw into) ================= */
static int g_lc_x,g_lc_y,g_lc_w,g_lc_h; static char g_lc_title[48]; static int g_lc_on=0;
static void lc_chrome(void){
    fill_round(g_lc_x-2,g_lc_y-2,g_lc_w+4,g_lc_h+TITLEH+4,10,15,rgb(255,255,255));
    fill_round(g_lc_x,g_lc_y,g_lc_w,TITLEH,8,15,rgb(38,42,58));
    dstr(g_lc_x+14,g_lc_y+8,g_lc_title,rgb(240,244,252),1);
    fill_round(g_lc_x+g_lc_w-20,g_lc_y+7,12,12,3,15,rgb(220,90,80));
}
void nvxlc_window(const char* t,int w,int h){
    if(w<120)w=120; if(w>(int)W-40)w=(int)W-40; if(h<80)h=80; if(h>(int)H-100)h=(int)H-100;
    g_lc_w=w; g_lc_h=h+TITLEH; g_lc_x=((int)W-w)/2; g_lc_y=((int)H-(h+TITLEH))/2;
    int i=0; for(;t&&t[i]&&i<46;i++)g_lc_title[i]=t[i]; g_lc_title[i]=0; g_lc_on=1;
    fill_rect(g_lc_x,g_lc_y+TITLEH,g_lc_w,g_lc_h-TITLEH,rgb(24,26,34)); lc_chrome(); blit_all();
}
static int lc_cx(int x){ return g_lc_x+x; }
static int lc_cy(int y){ return g_lc_y+TITLEH+y; }
static int lc_ch(void){ return g_lc_h-TITLEH; }
void nvxlc_clear(u32 c){ if(!g_lc_on)return; fill_rect(g_lc_x,g_lc_y+TITLEH,g_lc_w,lc_ch(),c); }
void nvxlc_rect(int x,int y,int w,int h,u32 c){ if(!g_lc_on)return; for(int j=0;j<h;j++){ int py=y+j; if(py<0||py>=lc_ch())continue; for(int i=0;i<w;i++){ int px=x+i; if(px<0||px>=g_lc_w)continue; putpx(lc_cx(px),lc_cy(py),c);} } }
void nvxlc_pixel(int x,int y,u32 c){ if(!g_lc_on)return; if(x>=0&&x<g_lc_w&&y>=0&&y<lc_ch()) putpx(lc_cx(x),lc_cy(y),c); }
void nvxlc_text(int x,int y,const char* s,u32 c){ if(!g_lc_on)return; dstr(lc_cx(x),lc_cy(y),s,c,1); }
void nvxlc_line(int x0,int y0,int x1,int y1,u32 c){ if(!g_lc_on)return; int dx=x1-x0,dy=y1-y0; int n=(dx<0?-dx:dx)>(dy<0?-dy:dy)?(dx<0?-dx:dx):(dy<0?-dy:dy); if(n<1)n=1; for(int i=0;i<=n;i++){ int px=x0+dx*i/n, py=y0+dy*i/n; nvxlc_pixel(px,py,c);} }
void nvxlc_present(void){ if(!g_lc_on)return; lc_chrome(); blit_all(); }
int  nvxlc_w(void){ return g_lc_w; }
int  nvxlc_h(void){ return lc_ch(); }
int  nvxlc_key(void){ return kpop(); }
int  nvxlc_waitkey(void){ int k; while((k=kpop())<0)cpu_idle(); return k; }
void nvxlc_mouse(int* x,int* y,int* b){ if(x)*x=mx-g_lc_x; if(y)*y=my-g_lc_y-TITLEH; if(b)*b=mouse_btn; }
unsigned long nvxlc_ticks(void){ return (unsigned long)g_ticks; }
void nvxlc_sleep(int ms){ u64 until=g_ticks+(ms/10); while(g_ticks<until)cpu_idle(); }
/* read one WM_CREATE off queue 0, find the app's shared pixel buffer, blit it to the screen with chrome */
static int wm_render_from_msg(const char* title){
    u8 mb[MSG_SIZE]; int rl=msg_recv_k(0,mb,MSG_SIZE); if(rl<(int)sizeof(struct wmsg)) return 0;
    struct wmsg* m=(struct wmsg*)mb; if(m->type!=WM_CREATE) return 0;
    int w=m->w, h=m->h; if(w<1||w>400||h<1||h>300) return 0;
    int id=-1; for(int i=0;i<SHM_MAX;i++) if(g_shm[i].used && g_shm[i].key==m->key){ id=i; break; }
    if(id<0) return 0;
    /* the app's pixels live in the shm frames (identity-mapped) - read them directly */
    int wx=((int)W-w)/2, wy=((int)H-(h+TITLEH))/2;
    fill_round(wx-2,wy-2,w+4,h+TITLEH+4,10,15,rgb(255,255,255));
    fill_round(wx,wy,w,TITLEH,8,15,rgb(40,90,200)); dstr(wx+14,wy+8,title,rgb(255,255,255),1);
    fill_round(wx+w-20,wy+7,12,12,3,15,rgb(220,90,80));
    for(int y=0;y<h;y++){ int fy=y; u32 off=(u32)fy*w; 
        for(int x=0;x<w;x++){ u32 idx=off+(u32)x; int fr=idx/1024; int fo=idx%1024; if(fr>=g_shm[id].nframes)continue;
            u32 px=((u32*)g_shm[id].frames[fr])[fo]; putpx(wx+x, wy+TITLEH+y, px); } }
    blit_all();
    return 1;
}
void wm_demo(void){
    if(!pf_ready)return; ipc_reset();
    preempt_run_elf(_binary_user_user_wmapp_elf_start,(u32)(_binary_user_user_wmapp_elf_end-_binary_user_user_wmapp_elf_start));
    /* after the app ran (created window + drew to shm + sent WM_CREATE), render it from the protocol msg */
    wm_render_from_msg("App via protocol");
}
int win_edge_hit(int px,int py,int* win){
    int order[NWIN],n=0; for(int i=0;i<NWIN;i++) if(WN[i].open&&!WN[i].min&&WN[i].ws==g_ws)order[n++]=i;
    for(int a=0;a<n;a++)for(int b=a+1;b<n;b++) if(WN[order[b]].z>WN[order[a]].z){int t=order[a];order[a]=order[b];order[b]=t;}
    const int G=6, I=4, C=14;
    for(int a=0;a<n;a++){ int i=order[a]; Win* w=&WN[i];
        if(px<w->x-G||px>=w->x+w->w+G||py<w->y-G||py>=w->y+w->h+G)continue;
        int inside=px>=w->x&&px<w->x+w->w&&py>=w->y&&py<w->y+w->h;
        if(w->max){ (void)inside; return 0; }
        int f=0; if(px<w->x+I)f|=1; if(px>=w->x+w->w-I)f|=2; if(py<w->y+I)f|=4; if(py>=w->y+w->h-I)f|=8;
        if(f&3){ if(py<w->y+C)f|=4; else if(py>=w->y+w->h-C)f|=8; }
        if(f&12){ if(px<w->x+C)f|=1; else if(px>=w->x+w->w-C)f|=2; }
        if(f){ if(win)*win=i; return f; }
        if(inside)return 0; }
    return 0; }
static void on_click(int px,int py){
    if(g_prop){ g_prop=0; return; }
    if(g_imgview>=0){ g_imgview=-1; return; }
    if(g_rename) return;
    if(g_launcher){ launcher_click(px,py); return; }
    if(g_qs&&g_dstyle==1){ if(classic_qs_click(px,py))return; if(!(py>=g_tby&&classic_tray_click(px))) g_qs=0; else return; }
    if(g_qs){ int pw=250,ph=150,qx=g_qsx-pw+30,qy=g_tby-ph-10; if(qx<6)qx=6;
        if(in_rect(px,py,qx,qy,pw,ph)){
            for(int k=0;k<3;k++){ int bx=qx+16+k*74, by=qy+32; if(in_rect(px,py,bx,by,64,30)){ g_theme=k; g_wallmode=0; build_bg(); return; } }
            for(int k=0;k<5;k++){ int bx=qx+16+k*44, by=qy+102; if(in_rect(px,py,bx,by,28,28)){ ACC=ACV[k]; ACC2=mix(ACC,rgb(0,0,0),60); g_accent=k; return; } }
            return; }
        g_qs=0; return; }
    if(g_notifpanel){ int pw=340,pnx=(int)W-pw-10; if(px<pnx||py>=g_tby){ g_notifpanel=0; if(py>=g_tby&&px>=g_bellx-4&&px<g_bellx+26)return; if(g_dstyle==1&&py>=g_tby&&px>=g_tray_x0+4*22&&px<g_tray_x0+5*22)return; } else return; }
    if(g_clockpop){ g_clockpop=0; return; }
    if(g_ctx){ int r=ctx_click(px,py); g_ctx=0; if(r) return; }
    if(g_dstyle==1&&py>=g_tby){ if(classic_tray_click(px))return; if(px<g_lbx_end){ g_launcher^=1; if(g_launcher){g_lq[0]=0;g_lqn=0;launcher_search();} } else if(px>=g_clockx){ g_clockpop^=1; } else classic_taskbtn_click(px); return; }
    if(py>=g_tby&&py<g_tby+g_tbh&&(px<DOCK_x-8||px>=DOCK_x+DOCK_w+8)){ if(px<g_lbx_end){ if(px>=g_lbx_end-44){ ws_switch(g_ws+1); return; } if(px>=g_lbx_end-96){ open_win(W_FILES); return; } g_launcher^=1; if(g_launcher){g_lq[0]=0;g_lqn=0;launcher_search();} }
        else if(px>=g_wsx&&px<g_wsx+NWS*14){ ws_switch((px-g_wsx)/14); }
        else if(px>=g_bellx-4&&px<g_bellx+22){ g_notifpanel^=1; }
        else if(px>=g_wifix-2&&px<g_wifix+20){ g_wifipop^=1; if(g_wifipop&&g_apn==0)wifi_gui_scan(0); }
        else if(px>=g_qsx-6&&px<g_qsx+22){ g_qs^=1; } else if(px>=g_clockx&&px<g_bellx-12){ g_clockpop^=1; } return; }
    if(g_wifipop){ int pw=320, ppx=g_wifix-pw+20; if(ppx<6)ppx=6;
        int rows=g_apn>7?7:g_apn; int ph=70+rows*30+(g_wifi_entry?70:0)+26; int ppy=g_tby-ph-10;
        if(px>=ppx&&px<ppx+pw&&py>=ppy&&py<ppy+ph){
            int bx=ppx+pw-80, by=ppy+12; if(px>=bx&&px<bx+66&&py>=by&&py<by+22){ wifi_gui_scan(1); return; }
            int cy=ppy+66;
            for(int i=0;i<rows;i++){ if(px>=ppx+8&&px<ppx+pw-8&&py>=cy&&py<cy+28){
                if(g_aps[i].sec==0){ g_wifi_sel=i; g_wifi_entry=0; wifi_gui_connect(i); }
                else { g_wifi_sel=i; g_wifi_entry=1; g_wifi_pwlen=0; g_wifi_pw[0]=0; }
                return; } cy+=30; }
            if(g_wifi_entry && g_wifi_sel>=0){ cy+=6+20;
                int jx=ppx+pw-76; if(px>=jx&&px<jx+60&&py>=cy&&py<cy+26){ g_wifi_pw[g_wifi_pwlen]=0; wifi_gui_connect(g_wifi_sel); g_wifi_entry=0; return; } }
            return; }
        g_wifipop=0; g_wifi_entry=0; return; }
    for(int k=0;k<12;k++){ int ix=DOCK_x+DOCK_PAD+k*(DOCK_IC+DOCK_GAP);
        if(px>=ix-5&&px<ix+DOCK_IC+5&&py>=DOCK_y&&py<DOCK_y+DOCK_h){ focus_win(IC_APP[k]); return; } }   /* dock */
    { int ew=-1; int ef=win_edge_hit(px,py,&ew); if(ef&&ew>=0&&win_ctl_hit(ew,px,py)<0){ focus_win(ew); g_resize=ew; g_rflags=ef; r_sw0=WN[ew].w; r_sh0=WN[ew].h; r_sx0=WN[ew].x; r_sy0=WN[ew].y; r_mx0=px; r_my0=py; return; } }
    int order[NWIN],n=0; for(int i=0;i<NWIN;i++) if(WN[i].open&&!WN[i].min)order[n++]=i;
    for(int a=0;a<n;a++)for(int b=a+1;b<n;b++) if(WN[order[b]].z>WN[order[a]].z){int t=order[a];order[a]=order[b];order[b]=t;}
    for(int a=0;a<n;a++){ int i=order[a]; if(in_rect(px,py,WN[i].x,WN[i].y,WN[i].w,WN[i].h)){ focus_win(i);
        int ctl=win_ctl_hit(i,px,py);
        if(ctl==2){ close_win(i); return; }
        if(ctl==1){ win_max(i); return; }
        if(ctl==0){ win_min(i); return; }
        if(px>=WN[i].x+WN[i].w-18 && px<WN[i].x+WN[i].w && py>=WN[i].y+WN[i].h-18 && py<WN[i].y+WN[i].h){ g_resize=i; g_rflags=10; r_sw0=WN[i].w; r_sh0=WN[i].h; r_sx0=WN[i].x; r_sy0=WN[i].y; r_mx0=px; r_my0=py; return; }
        if(py<WN[i].y+TITLEH){ WN[i].sx=WN[i].x;WN[i].sy=WN[i].y;WN[i].sw=WN[i].w;WN[i].sh=WN[i].h; drag=i; dox=px-WN[i].x; doy=py-WN[i].y; return; }
        app_click(i,px,py); return; } }
    { int it=desk_hit(px,py); if(it>=0){ int ix,iy; if(it<100){ix=g_apx[it];iy=g_apy[it];}else{ix=g_fpx[it-100];iy=g_fpy[it-100];} g_ddrag=it; g_ddx=px-ix; g_ddy=py-iy; g_ddsx=px; g_ddsy=py; return; } }
    g_marq=1; marq_x0=px; marq_y0=py;
}
int process_input(void){
    int changed=0;
    if(g_doom_mcap && (kdown[0x01] || focus!=W_DOOM || !WN[W_DOOM].open)){ g_doom_mcap=0; changed=1; }
    { extern int g_quake_mcap; if(g_quake_mcap && (kdown[0x01] || focus!=W_QUAKE || !WN[W_QUAKE].open)){ g_quake_mcap=0; changed=1; } }   /* ESC / focus loss releases the mouse from DOOM */
    /* --- shell hotkeys (edge-triggered) --- */
    { static int ws_arm[4]={1,1,1,1};                                   /* Ctrl+1..4 -> workspace */
      if(kdown[0x1D]){ int sc4[4]={0x02,0x03,0x04,0x05};
          for(int w=0;w<4;w++){ if(kdown[sc4[w]]){ if(ws_arm[w]){ ws_switch(w); changed=1; } ws_arm[w]=0; } else ws_arm[w]=1; } }
      else for(int w=0;w<4;w++) ws_arm[w]=1;
      { extern int hk_poll(void); if(hk_poll())changed=1; }
      static int winheld=0, winR=0;
      if(kdown[0x5B]){ if(!winheld){ winheld=1; winR=0; }
          if(kdown[0x13] && !winR){ open_win(W_TERM); winR=1; changed=1; }
          if(kdown[0x12] && !winR){ open_win(W_FILES); winR=1; changed=1; }
          if(kdown[0x26] && !winR){ extern void hk_do(int); hk_do(20); winR=1; changed=1; }
          if(kdown[0x19] && !winR){ extern void hk_do(int); hk_do(15); winR=1; changed=1; }
          if(kdown[0x17] && !winR){ open_win(W_SETTINGS); winR=1; changed=1; }
          if(kdown[0x1E] && !winR){ g_qs=!g_qs; winR=1; changed=1; } }
      else { if(winheld){ if(!winR){ g_notifpanel=!g_notifpanel; changed=1; } winheld=0; winR=0; } }
      { static int cafl=0; if(kdown[0x1D]&&kdown[0x38]&&kdown[0x21]){ if(!cafl){ extern void igpu_flip_off(const char*); igpu_flip_off("turned off with Ctrl+Alt+F"); toast("Tear-free drawing off"); changed=1; } cafl=1; } else cafl=0; }
    }
    if(g_gesture){ int g=g_gesture; g_gesture=0; changed=1;
        if(g==GES_SWIPE_UP){ g_launcher=1; } else if(g==GES_SWIPE_DOWN){ g_launcher=0; g_notifpanel=0; }
        else if(g==GES_SWIPE_LEFT){ ws_switch(g_ws+1); } else if(g==GES_SWIPE_RIGHT){ ws_switch(g_ws-1); } }
    if(mouse_evt){ int dx,dy,btn,rbtn,sc; mouse_hscroll=0;
        cpu_irq_off(); dx=mouse_dx;dy=mouse_dy;mouse_dx=0;mouse_dy=0;btn=mouse_btn;rbtn=mouse_rbtn;sc=mouse_scroll;mouse_scroll=0;mouse_evt=0; cpu_irq_on();
        ux_mouse_push(dx,dy,(btn?1:0)|(rbtn?2:0));
        { extern int g_quake_mcap, quake_mdx, quake_mdy, quake_mbtn; if(g_quake_mcap && WN[W_QUAKE].open && focus==W_QUAKE){ quake_mdx+=dx; quake_mdy+=dy; quake_mbtn=(btn?1:0)|(rbtn?2:0); dx=0; dy=0; btn=0; rbtn=0; sc=0; changed=1; } }
        if(g_doom_mcap && WN[W_DOOM].open && focus==W_DOOM){ doom_mdx+=dx; doom_mdy+=dy; doom_mbtn=(btn?1:0)|(rbtn?2:0); dx=0; dy=0; btn=0; rbtn=0; sc=0; changed=1; }   /* DOOM has the mouse */
        if((dx||dy)&&g_mouse_accel){ int m=(g_mouse_accel==1)?3:5; if(dx>6||dx<-6)dx=dx*m/2; if(dy>6||dy<-6)dy=dy*m/2; }
        if(dx||dy){ mx+=dx;my-=dy; if(mx<0)mx=0;if(my<0)my=0;if(mx>=(int)W)mx=W-1;if(my>=(int)H)my=H-1; g_cursor_moved=1;
            { int hv=(g_resize>=0)?g_rflags:(drag<0&&!btn)?win_edge_hit(mx,my,0):0; if(hv!=g_rz_hover){ g_rz_hover=hv; changed=1; } } }
        { extern int g_xfull; extern void xfull_mouse(int,int,int); if(g_xfull){ xfull_mouse(btn,rbtn,sc); btn=0; rbtn=0; sc=0; dx=dy=0; } }
        { extern int xover_mouse(int,int,int,int); if(!g_launcher&&xover_mouse(btn,rbtn,sc,(btn&&!mouse_btn_prev)||(rbtn&&!mouse_rbtn_prev))){ btn=mouse_btn_prev; rbtn=mouse_rbtn_prev; sc=0; changed=1; } }
        if(btn&&!mouse_btn_prev){ on_click(mx,my); changed=1; }
        { extern void xdisp_mouse(int,int,int); xdisp_mouse(btn,rbtn,sc); }
        if(btn&&mouse_btn_prev&&(dx||dy)&&focus==W_WRITER&&WN[W_WRITER].open&&drag<0&&g_resize<0){ extern void writer_drag(int,int); writer_drag(mx,my); changed=1; }
        if(!btn&&mouse_btn_prev){
            if(g_ddrag>=0){ int moved=(mx-g_ddsx)*(mx-g_ddsx)+(my-g_ddsy)*(my-g_ddsy);
                if(moved<64){ desk_open(g_ddrag); }
                else { int nx=mx-g_ddx, ny=my-g_ddy; int c=(nx-DGX0+DGW/2)/DGW; int r=(ny-DGY0+DGH/2)/DGH; if(c<0)c=0; if(r<0)r=0; int sx=DGX0+c*DGW, sy=DGY0+r*DGH;
                    if(g_ddrag<100){ g_apx[g_ddrag]=sx; g_apy[g_ddrag]=sy; } else { int fs=g_ddrag-100; if(fs>=0&&fs<FS_N){ g_fpx[fs]=sx; g_fpy[fs]=sy; g_fpl[fs]=1; } } }
                g_ddrag=-1; changed=1; }
            if(g_fdrag){ int dd=(mx-g_fdrag_x)*(mx-g_fdrag_x)+(my-g_fdrag_y)*(my-g_fdrag_y);
                if(dd<36){ int k=g_fdrag_slot; if(g_files_tree&&k>=0&&FS[k].used&&fkind(k)==0){ extern int py_open_slot(int); if(!py_open_slot(k)) editor_open(k); } }   /* treated as a click (new explorer selects instead) */
                else if(!g_files_tree&&!g_files_usb&&fx_drop(mx,my,g_fdrag_slot)){ }                           /* dropped on a folder / place */
                else if(g_fdrag_slot>=0 && !in_rect(mx,my,WN[W_FILES].x,WN[W_FILES].y,WN[W_FILES].w,WN[W_FILES].h)){   /* dropped outside Files -> desktop */
                    FS[g_fdrag_slot].folder=2; g_fpl[g_fdrag_slot]=0; g_fpx[g_fdrag_slot]=(mx/DGW)*DGW; g_fpy[g_fdrag_slot]=(my/DGH)*DGH; g_fpl[g_fdrag_slot]=1; if(g_fpy[g_fdrag_slot]<DGY0)g_fpy[g_fdrag_slot]=DGY0; toast("Moved to desktop"); }
                else if(g_files_tree){ int wxf=WN[W_FILES].x,wwf=WN[W_FILES].w;                       /* drop on a folder row */
                    for(int r=0;r<ftrow_n;r++) if(ftrow_isfile[r]==0 && mx>=wxf+8&&mx<wxf+wwf-8&&my>=ftrow_y[r]-2&&my<ftrow_y[r]+24){
                        int node=ftrow_node[r]; int fld=(node==5)?0:(node==6)?1:-1;
                        if(fld>=0&&g_fdrag_slot>=0){ FS[g_fdrag_slot].folder=fld; toast("Moved"); } else toast("Can only drop in Documents/Downloads"); break; } }
                g_fdrag=0; g_fdrag_slot=-1; }
            if(drag>=0&&g_snap) apply_snap(drag,g_snap); drag=-1; g_resize=-1; g_snap=0; g_marq=0; changed=1; }
        if(g_marq&&btn) changed=1;
        if(drag>=0&&btn){ int nx=mx-dox, ny=my-doy;
            if(ny<0)ny=0; if(ny>(int)H-TITLEH)ny=(int)H-TITLEH;
            if(nx>(int)W-90)nx=(int)W-90; if(nx<90-WN[drag].w)nx=90-WN[drag].w;
            WN[drag].x=nx; WN[drag].y=ny; if(WN[drag].max)WN[drag].max=0;
            g_snap=0; if(my<=3)g_snap=3; else if(mx<=3)g_snap=(my<(int)H/3)?4:(my>2*(int)H/3)?6:1; else if(mx>=(int)W-4)g_snap=(my<(int)H/3)?5:(my>2*(int)H/3)?7:2;
            changed=1; }
        if(g_resize>=0&&btn){ Win* w=&WN[g_resize]; int ddx=mx-r_mx0, ddy=my-r_my0; int nx=r_sx0, ny=r_sy0, nw=r_sw0, nh=r_sh0; const int MW=240, MH=160;
            if(g_rflags&2){ nw=r_sw0+ddx; if(nw<MW)nw=MW; if(nx+nw>(int)W)nw=(int)W-nx; }
            if(g_rflags&8){ nh=r_sh0+ddy; if(nh<MH)nh=MH; if(ny+nh>(int)H-4)nh=(int)H-4-ny; }
            if(g_rflags&1){ int r=r_sx0+r_sw0; nx=r_sx0+ddx; if(nx<0)nx=0; if(r-nx<MW)nx=r-MW; nw=r-nx; }
            if(g_rflags&4){ int bt=r_sy0+r_sh0; ny=r_sy0+ddy; if(ny<0)ny=0; if(bt-ny<MH)ny=bt-MH; nh=bt-ny; }
            if(nw!=w->w||nh!=w->h||nx!=w->x||ny!=w->y){ w->x=nx; w->y=ny; w->w=nw; w->h=nh; w->max=0; changed=1; } }
        if(rbtn&&!mouse_rbtn_prev){ g_launcher=0; g_qs=0; g_clockpop=0; g_wifipop=0; g_ctxkind=0; g_ctxwin=-1;
            int top=-1,tz=-1; for(int i=0;i<NWIN;i++) if(WN[i].open&&!WN[i].min&&in_rect(mx,my,WN[i].x,WN[i].y,WN[i].w,WN[i].h)&&WN[i].z>tz){tz=WN[i].z;top=i;}
            if(top<0){ int it=desk_hit(mx,my); if(it>=0){ g_dctx_item=it; g_ctxkind=(it<100)?5:6; } }
            if(top>=0){ if(my<WN[top].y+TITLEH){ g_ctxkind=1; g_ctxwin=top; }
                else if(top==W_FILES && g_files_tree){ g_tctx_type=-1; g_tctx_val=-1;
                    for(int r=0;r<ftrow_n;r++) if(mx>=WN[top].x+8&&mx<WN[top].x+WN[top].w-8&&my>=ftrow_y[r]-2&&my<ftrow_y[r]+24){
                        if(ftrow_isfile[r]==0){ g_tctx_type=0; g_tctx_val=ftrow_node[r]; }
                        else if(ftrow_isfile[r]==1){ g_tctx_type=1; g_tctx_val=ftrow_node[r]; g_fsel=ftrow_node[r]; }
                        else { g_tctx_type=2; g_tctx_val=ftrow_node[r]; } break; }
                    g_ctxkind=4; }
                else if(top==W_FILES){ int kk=fx_rclick(mx,my); if(kk)g_ctxkind=kk; }
                else if(top==W_BROWSER && my>WN[top].y+TITLEH){ g_ctxkind=3; } }
            g_ctx=1; g_submenu=0; ctx_x=mx; ctx_y=my; changed=1; }
        if(sc){ if(focus==W_EDITOR&&WN[W_EDITOR].open){ ed_top-=sc*2; if(ed_top<0)ed_top=0; if(ed_top>ed_nl-1)ed_top=ed_nl-1; changed=1; }
            else if(focus==W_FILES&&WN[W_FILES].open){ extern void gfx_win_event(int,int); gfx_win_event(W_FILES,3); fx_scroll-=sc; changed=1; }
            else if(focus==W_TERM&&WN[W_TERM].open){ extern void gfx_win_event(int,int); gfx_win_event(W_TERM,3); g_tsb_off+=sc*3; if(g_tsb_off<0)g_tsb_off=0; if(g_tsb_off>tsb_n)g_tsb_off=tsb_n; changed=1; }
            else if(focus==W_BROWSER&&WN[W_BROWSER].open&&g_page==3){ browser_wheel(sc); changed=1; }
            else if(focus==W_SYSMON&&WN[W_SYSMON].open){ extern void taskmgr_wheel(int); taskmgr_wheel(sc); changed=1; }
            else if(focus==W_SETTINGS&&WN[W_SETTINGS].open){ extern void settings_wheel(int); settings_wheel(sc); changed=1; }
            else if(focus==W_WRITER&&WN[W_WRITER].open){ extern void writer_wheel(int); writer_wheel(sc); changed=1; } }
        if(btn && focus==W_PAINT && WN[W_PAINT].open && !WN[W_PAINT].min && g_resize<0 && drag<0){ paint_at(mx,my); changed=1; }
        mouse_btn_prev=btn; mouse_rbtn_prev=rbtn; }
    int c; while((c=kpop())>=0){
        { extern int g_xfull; extern void xfull_exit(void); extern void xdisp_key(int); if(g_xfull){ if(c==30||c==31){ xfull_exit(); changed=1; } else xdisp_key(c); continue; } { extern int g_xover, g_xkbd; if(g_xover&&g_xkbd&&!g_launcher){ xdisp_key(c); continue; } } }
        if(g_rename){ if(c=='\n'){ fx_rename_commit(); }
            else if(c==27){ g_rename=0; g_renmode=0; } else if(c=='\b'){ if(g_renlen>0)g_renlen--; g_renbuf[g_renlen]=0; } else if(c>=32&&c<127){ if(g_renlen<63){g_renbuf[g_renlen++]=(char)c; g_renbuf[g_renlen]=0;} } changed=1; continue; }
        if(c==9 && kalt){ if(!g_alttab){ g_altn=0; for(int i=0;i<NWIN;i++) if(WN[i].open)g_altlist[g_altn++]=i; if(g_altn>0){ g_alttab=1; g_altsel=(g_altn>1)?1:0; } } else g_altsel=(g_altn>0)?(g_altsel+1)%g_altn:0; changed=1; continue; }
        if(g_launcher){ launcher_key((char)c); changed=1; continue; }
        if(g_wifipop && g_wifi_entry){
            if(c==27){ g_wifi_entry=0; changed=1; }
            else if(c=='\n'){ g_wifi_pw[g_wifi_pwlen]=0; wifi_gui_connect(g_wifi_sel); g_wifi_entry=0; changed=1; }
            else if(c=='\b'){ if(g_wifi_pwlen>0)g_wifi_pwlen--; changed=1; }
            else if(c>=32&&c<127){ if(g_wifi_pwlen<62)g_wifi_pw[g_wifi_pwlen++]=(char)c; changed=1; }
            continue; }
        if(g_qs||g_clockpop||g_notifpanel){ if(c==27){ g_qs=0;g_clockpop=0;g_notifpanel=0;g_wifipop=0; changed=1; } continue; }
        if(focus==W_SETTINGS&&WN[W_SETTINGS].open&&g_setcat==2&&g_wifi_entry){
            if(c==27){ g_wifi_entry=0; changed=1; }
            else if(c=='\n'){ g_wifi_pw[g_wifi_pwlen]=0; wifi_gui_connect(g_wifi_sel); g_wifi_entry=0; changed=1; }
            else if(c=='\b'){ if(g_wifi_pwlen>0)g_wifi_pwlen--; changed=1; }
            else if(c>=32&&c<127){ if(g_wifi_pwlen<62)g_wifi_pw[g_wifi_pwlen++]=(char)c; changed=1; }
        }
        else if(focus==W_TERM&&WN[W_TERM].open){ g_tsb_off=0; int cc=c;
            if(kdown[0x1D]){ if(cc=='c'||cc=='C')cc=3; else if(cc=='z'||cc=='Z')cc=26; else if(cc=='d'||cc=='D')cc=4; else if(cc=='\\')cc=28; else if(g_tty_fg&&((cc>='a'&&cc<='z')||(cc>='A'&&cc<='Z')))cc=0x100|((cc|32)-'a'+1); else if(g_tty_fg&&cc=='[')cc=27; }
            if(g_tty_fg)tty_key(cc);
            else if(cc==3){ term_puts("^C\n"); tlen=0; term_prompt(); }
            else if(cc!=4&&cc!=26&&cc!=28&&cc<0x100) term_key((char)cc);
            changed=1; }
        else if(focus==W_EDITOR&&WN[W_EDITOR].open){ editor_key((char)c); changed=1; }
        else if(focus==W_FILES&&WN[W_FILES].open){ files_key(c); changed=1; }
        else if(focus==W_BROWSER&&WN[W_BROWSER].open){ browser_key((char)c); changed=1; }
        else if(focus==W_CHATGPT&&WN[W_CHATGPT].open){ chatgpt_key((char)c); changed=1; }
        else if(focus==W_PPTX&&WN[W_PPTX].open){ pptx_key(c); changed=1; }
        else if(focus==W_VIDEO&&WN[W_VIDEO].open){ extern void video_key(int); video_key(c); changed=1; }
        else if(focus==W_XDISP&&WN[W_XDISP].open){ extern void xdisp_key(int); xdisp_key(c); }
        else if(focus==W_CAMERA&&WN[W_CAMERA].open){ extern void camera_key(int); camera_key(c); changed=1; }
        else if(focus==W_BENCH&&WN[W_BENCH].open){ extern void bench_key(int); bench_key(c); changed=1; }
        else if(focus==W_WRITER&&WN[W_WRITER].open){ extern void writer_key(int); writer_key(c); changed=1; }
        else if(focus==W_RECORDER&&WN[W_RECORDER].open){ extern void recorder_key(int); recorder_key(c); changed=1; }
        else if(focus==W_SNAKE&&WN[W_SNAKE].open){ snake_key(c); changed=1; }
        else if(focus==W_GPU3D&&WN[W_GPU3D].open){ extern void gpu3d_key(int); gpu3d_key(c); changed=1; }
        else if(focus==W_FLAPPY&&WN[W_FLAPPY].open){ flappy_key(c); changed=1; }
        else if(focus==W_CALENDAR&&WN[W_CALENDAR].open){ cal_key(c); changed=1; } }
    if(!kalt && alt_prev && g_alttab){ if(g_altn>0) focus_win(g_altlist[g_altsel]); g_alttab=0; changed=1; } alt_prev=kalt;
    if(WN[W_BROWSER].open&&browser_tick())changed=1;
    { extern int video_tick(void); if(video_tick())changed=1; }
    if(WN[W_SNAKE].open&&!WN[W_SNAKE].min&&focus==W_SNAKE&&snake_state==1&&(u64)(g_ticks-snake_last)>=8){ snake_last=g_ticks; snake_step(); changed=1; }
    if(WN[W_FLAPPY].open&&!WN[W_FLAPPY].min&&focus==W_FLAPPY&&fb_state==1&&(u64)(g_ticks-flappy_last)>=3){ flappy_last=g_ticks; flappy_step(); changed=1; }
    if(toast_t && (u64)(g_ticks-toast_t)<300) changed=1;
    static int lastsec=-1; int h,m,ss,y,mo,d; read_clock(&h,&m,&ss,&y,&mo,&d); if(ss!=lastsec){lastsec=ss;changed=1;}
    static int lastbl=-1; int bp=(int)((g_ticks/50)&1); if(bp!=lastbl){lastbl=bp; if(g_rename||((focus==W_TERM||focus==W_EDITOR)&&(WN[W_TERM].open||WN[W_EDITOR].open)))changed=1;}
    return changed;
}
/* ---- desktop right-click context menu ---- */
#define CTX_W 196
#define CTX_IH 30
static void new_note(void){
    char nm[16]="note.txt"; int used=0; for(int i=0;i<FS_N;i++) if(FS[i].used&&seq(FS[i].name,nm))used=1;
    if(used) for(int n=2;n<60;n++){ char b[16],*p=b; p=scopy(p,"note"); p=udec(p,(u32)n); scopy(p,".txt"); int u=0; for(int i=0;i<FS_N;i++) if(FS[i].used&&seq(FS[i].name,b))u=1; if(!u){ scopy(nm,b); break; } }
    int s=fs_save_bin(nm,(const u8*)"",0,0); if(s>=0){ g_fsel=s; g_filesnav=2; editor_open(s); toast("New note"); }
}
static void view_source(void){
    int off=g_bodyoff, len=g_httplen-g_bodyoff; if(len<0)len=0; if(len>FS_CAP-1)len=FS_CAP-1;
    g_fs_pulling=1; int s=fs_save_bin("source.html",http_buf+off,len,0); g_fs_pulling=0;
    if(s>=0){ g_filesnav=2; editor_open(s); }
}
static int ctx_items(const char* out[8]){
    if(g_ctxkind==3){ out[0]="Back"; out[1]="Reload"; out[2]="Copy link"; out[3]="View source"; return 4; }
    if(g_ctxkind==1){ out[0]="Minimize"; out[1]=(g_ctxwin>=0&&WN[g_ctxwin].max)?"Restore":"Maximize"; out[2]="Close"; return 3; }
    if(g_ctxkind==7){ out[0]="Open"; out[1]="Rename"; out[2]="Delete"; return 3; }
    if(g_ctxkind==8){ out[0]="New folder"; out[1]="New file"; out[2]="Paste"; out[3]=fx_view?"List view":"Grid view"; return 4; }
    if(g_ctxkind==2){ int n=0; out[n++]="Open"; out[n++]="Rename"; out[n++]="Delete"; out[n++]="Copy"; out[n++]="Cut";
        if(g_fsel>=0&&FS[g_fsel].used&&fx_is_elf(g_fsel)){ out[n++]="Run"; out[n++]="Run as administrator"; return n; }
        if(g_fsel>=0&&FS[g_fsel].used&&fkind(g_fsel)==4) out[n++]="Extract all";
        if(g_fsel>=0&&FS[g_fsel].used&&fkind(g_fsel)==1) out[n++]="Set as wallpaper"; return n; }
    if(g_ctxkind==5){ out[0]="Open"; out[1]="Reset position"; out[2]="Properties"; return 3; }
    if(g_ctxkind==6){ out[0]="Open"; out[1]="Rename"; out[2]="Duplicate"; out[3]="Delete"; out[4]="Properties"; return 5; }
    if(g_ctxkind==4){ int n=0;
        if(g_tctx_type==1){ out[n++]="Open"; out[n++]="Rename"; out[n++]="Delete"; out[n++]="New..."; }
        else if(g_tctx_type==0||g_tctx_type==2){ out[n++]="Open"; out[n++]="New..."; }
        else { out[n++]="New..."; out[n++]="Refresh"; }
        return n; }
    out[0]="View"; out[1]="Sort by"; out[2]="Refresh"; out[3]="New"; out[4]="Display settings"; out[5]="Personalize"; out[6]="Open Terminal"; out[7]="Change wallpaper"; return 8;
}
static int ctx_submenu(int kind,const char* lbl,const char* out[8]){
    (void)kind;
    if(seq(lbl,"View")){ out[0]="Large icons"; out[1]="Small icons"; return 2; }
    if(seq(lbl,"Sort by")){ out[0]="Name"; out[1]="Type"; return 2; }
    if(seq(lbl,"New")){ out[0]="Text File"; out[1]="Markdown"; out[2]="Note"; out[3]="Code (.nvx)"; out[4]="Config"; out[5]="HTML"; return 6; }
    if(seq(lbl,"New...")){ out[0]="Text File"; out[1]="Folder"; return 2; }
    return 0;
}
static void ctx_pos(int*ox,int*oy,int n){ int w=CTX_W,h=n*CTX_IH+10; int x=ctx_x,y=ctx_y;
    if(x+w>(int)W-6)x=(int)W-6-w; if(y+h>(int)H-70)y=(int)H-70-h; if(x<6)x=6; if(y<6)y=6; *ox=x; *oy=y; }
void ctx_draw(void){ const char*it[8]; int n=ctx_items(it); int x,y; ctx_pos(&x,&y,n); int h=n*CTX_IH+10;
    shadow_soft(x,y,CTX_W,h,12); { extern int g_gfx, g_gfx_fx; extern void gfx_glass(int,int,int,int,int,u32,int,int); if(g_gfx&&g_gfx_fx) gfx_glass(x,y,CTX_W,h,12,rgb(32,36,46),226,2); else fill_round(x,y,CTX_W,h,12,15,rgb(32,36,46)); }
    int subrow=-1;
    for(int k=0;k<n;k++){ int iy=y+5+k*CTX_IH; int hov=(mx>=x&&mx<x+CTX_W&&my>=iy&&my<iy+CTX_IH);
        const char* si[8]; int sn=ctx_submenu(g_ctxkind,it[k],si);
        if(hov) fill_round(x+4,iy,CTX_W-8,CTX_IH,6,15,ACC); dstr(x+16,iy+7,it[k],hov?rgb(255,255,255):rgb(230,236,246),1);
        if(sn>0){ dstr(x+CTX_W-18,iy+7,">",hov?rgb(255,255,255):rgb(180,188,204),1);
            if(hov){ subrow=k; g_submenu=1; g_sub_x=x+CTX_W-4; g_sub_y=iy-4; g_subn=sn; int j=0; for(;it[k][j]&&j<15;j++)g_subparent[j]=it[k][j]; g_subparent[j]=0; for(int q=0;q<sn;q++)g_subit[q]=si[q]; } }
    }
    if(g_submenu){ int sh=g_subn*CTX_IH+10; int sm_over=(mx>=g_sub_x&&mx<g_sub_x+130&&my>=g_sub_y&&my<g_sub_y+sh);
        if(subrow<0 && !sm_over) g_submenu=0;
        else { shadow_soft(g_sub_x,g_sub_y,130,sh,10); fill_round(g_sub_x,g_sub_y,130,sh,10,15,rgb(32,36,46));
            for(int k=0;k<g_subn;k++){ int iy=g_sub_y+5+k*CTX_IH; int hv=(mx>=g_sub_x&&mx<g_sub_x+130&&my>=iy&&my<iy+CTX_IH); if(hv)fill_round(g_sub_x+4,iy,122,CTX_IH,6,15,ACC); dstr(g_sub_x+14,iy+7,g_subit[k],hv?rgb(255,255,255):rgb(230,236,246),1); } } }
}
int ctx_click(int px,int py){ const char*it[8]; int n=ctx_items(it); int x,y; ctx_pos(&x,&y,n); int h=n*CTX_IH+10;
    /* submenu click (generic, by parent label) */
    if(g_submenu && px>=g_sub_x && px<g_sub_x+130 && py>=g_sub_y && py<g_sub_y+g_subn*CTX_IH+10){
        int sk=(py-g_sub_y-5)/CTX_IH; if(sk<0)sk=0; if(sk>=g_subn)sk=g_subn-1; const char* par=g_subparent; g_submenu=0;
        if(seq(par,"New")){ const char* nm; const char* body="";
            if(sk==1){ nm="untitled.md"; body="# Untitled\n\n"; }
            else if(sk==2){ nm="note.txt"; body=""; }
            else if(sk==3){ nm="script.nvx"; body="int main(){\n    printf(\"Hello from NoovexOS!\\n\");\n    return 0;\n}\n"; }
            else if(sk==4){ nm="settings.cfg"; body="key = value\n"; }
            else if(sk==5){ nm="page.html"; body="<html><body>\n<h1>Hello</h1>\n<p>Edit me.</p>\n</body></html>\n"; }
            else { nm="new.txt"; body=""; }
            for(int i=0;i<FS_N;i++) if(!FS[i].used){ fs_newslot(i); int j=0;for(;nm[j];j++)FS[i].name[j]=nm[j];FS[i].name[j]=0; int b=0; while(body[b])b++; if(fs_set(i,body,b))break; FS[i].used=1;FS[i].folder=2; g_fpl[i]=0; desk_place_file(i); g_rename=1;g_renslot=i;g_renlen=0;{const char*r=FS[i].name;while(r[g_renlen]&&g_renlen<63){g_renbuf[g_renlen]=r[g_renlen];g_renlen++;}}g_renbuf[g_renlen]=0; toast("New file on desktop"); break; } }
        else if(seq(par,"New...")){ int fld=(g_cwd==6)?1:0; if(sk==0){ for(int i=0;i<FS_N;i++) if(!FS[i].used){ fs_newslot(i); const char*nm="new.txt";int j=0;for(;nm[j];j++)FS[i].name[j]=nm[j];FS[i].name[j]=0;FS[i].used=1;FS[i].folder=fld; g_rename=1;g_renslot=i;g_renlen=0;{const char*r=FS[i].name;while(r[g_renlen]&&g_renlen<63){g_renbuf[g_renlen]=r[g_renlen];g_renlen++;}}g_renbuf[g_renlen]=0; if(g_cwd!=5&&g_cwd!=6)g_cwd=(fld==1)?6:5; toast("New file"); break; } } else toast("Folders are fixed in this build"); }
        else if(seq(par,"View")){ toast(sk==0?"View: Large icons":"View: Small icons"); }
        else if(seq(par,"Sort by")){ toast(sk==0?"Sorted by name":"Sorted by type"); }
        return 1; }
    if(!in_rect(px,py,x,y,CTX_W,h)){ if(g_submenu){g_submenu=0;return 1;} return 0; } int k=(py-y-5)/CTX_IH; if(k<0||k>=n) return 1;
    if(g_ctxkind==3){
        if(k==0){ if(backn>0){ scopy(g_url,backstk[--backn]); g_urllen=slen(g_url); browser_go(0); } }
        else if(k==1){ if(g_cururl[0]){ scopy(g_url,g_cururl); g_urllen=slen(g_url);} browser_go(0); }
        else if(k==2){ scopy(g_clip,g_url); g_clip_len=g_urllen; toast("Link copied"); }
        else view_source();
        return 1; }
    if(g_ctxkind==1){ if(k==0)win_min(g_ctxwin); else if(k==1)win_max(g_ctxwin); else close_win(g_ctxwin); return 1; }
    if(g_ctxkind==2){
        if(k==0){ if(g_fsel>=0) fx_open_file(g_fsel); }
        else if(k==1){ if(g_fsel>=0) fx_start_input(0,FS[g_fsel].name,g_fsel,-1); }
        else if(k==2){ if(g_fsel>=0)fx_delete_sel(); }
        else if(k==3){ fx_do(8,0); } else if(k==4){ fx_do(9,0); }
        else if((k==5||k==6)&&g_fsel>=0&&FS[g_fsel].used&&fx_is_elf(g_fsel)){ char p[192],cl[220]; dt_path(FS[g_fsel].dir,p); char* q=cl; if(k==6)q=scopy(q,"sudo "); q=scopy(q,p); if(!(p[0]=='/'&&!p[1]))*q++='/'; q=scopy(q,FS[g_fsel].name); *q=0;
            open_win(W_TERM); term_puts(cl); term_putc('\n'); run_cmd(cl); if(!g_tty_fg)term_prompt(); return 1; }
        else { const char*lbl=it[k];
            if(lbl[0]=='S'){ if(g_fsel>=0){ wall_custom_slot=g_fsel; g_wallmode=1; build_bg(); toast("Wallpaper set"); } }
            else if(g_fsel>=0&&fkind(g_fsel)==4){ zip_open(g_fsel); int cnt=0;
                for(int e=0;e<znent;e++){ const char*nm=zent[e].name; int q=0; while(nm[q])q++; if(q>0&&(nm[q-1]=='/'||nm[q-1]=='\\'))continue;
                    int L=zip_extract(g_fsel,e,(u8*)jpeg_scratch,(int)sizeof(jpeg_scratch)); if(L<0)continue;
                    const char*base=nm; for(int r=0;nm[r];r++) if(nm[r]=='/'||nm[r]=='\\')base=nm+r+1; if(base[0]) { fs_save_bin(base,(u8*)jpeg_scratch,L,1); cnt++; } }
                char b[40],*p=b; p=scopy(p,"Extracted "); p=udec(p,(u32)cnt); scopy(p," files"); toast(b); } }
        return 1; }
    if(g_ctxkind==4){ const char* lbl=it[k];
        if(lbl[0]=='O'){ if(g_tctx_type==0)g_cwd=g_tctx_val; else if(g_tctx_type==1){ if(fkind(g_tctx_val)==0)editor_open(g_tctx_val); } else if(g_tctx_type==2){ int L=0; while(FFILE[g_tctx_val].text[L])L++; editor_open_buf(FFILE[g_tctx_val].text,L); } }
        else if(lbl[0]=='R'&&lbl[1]=='e'&&lbl[2]=='n'){ g_rename=1; g_renslot=g_tctx_val; g_renlen=0; { const char*nm=FS[g_tctx_val].name; while(nm[g_renlen]&&g_renlen<63){g_renbuf[g_renlen]=nm[g_renlen];g_renlen++;} } g_renbuf[g_renlen]=0; }
        else if(lbl[0]=='D'){ if(g_tctx_val>=0){ fs_drop_name(g_tctx_val); g_fsel=-1; toast("Deleted"); } }
        else if(lbl[0]=='N'){ g_submenu=1; g_ctx=1; return 0; }   /* New... -> keep menu open for submenu hover */
        else { toast("Refreshed"); }
        return 1; }
    if(g_ctxkind==7){ if(k==0){ if(fx_seldir>=0)fx_go(fx_seldir); } else if(k==1)fx_rename_sel(); else fx_delete_sel(); return 1; }
    if(g_ctxkind==8){ if(k==0)fx_do(4,0); else if(k==1)fx_do(5,0); else if(k==2)fx_do(10,0); else fx_do(11,!fx_view); return 1; }
    if(g_ctxkind==5){ int a=g_dctx_item; if(k==0) desk_open(a); else if(k==1){ if(a>=0&&a<5){ g_apx[a]=DGX0; g_apy[a]=DGY0+a*DGH; toast("Icon reset"); } } else { g_prop=1; g_prop_app=a; g_prop_slot=-1; } return 1; }
    if(g_ctxkind==6){ int s2=g_dctx_item-100; if(s2<0||s2>=FS_N){ return 1; }
        if(k==0){ desk_open(g_dctx_item); }
        else if(k==1){ g_rename=1; g_renslot=s2; g_renlen=0; { const char*r=FS[s2].name; while(r[g_renlen]&&g_renlen<63){g_renbuf[g_renlen]=r[g_renlen];g_renlen++;} } g_renbuf[g_renlen]=0; }
        else if(k==2){ for(int i=0;i<FS_N;i++) if(!FS[i].used){ fs_newslot(i); char nn[64]; int j=0; for(;FS[s2].name[j]&&j<58;j++)nn[j]=FS[s2].name[j]; nn[j++]='_'; nn[j++]='2'; nn[j]=0; int q=0;for(;nn[q];q++)FS[i].name[q]=nn[q];FS[i].name[q]=0; if(fs_set(i,fsd(s2),FS[s2].len))break; FS[i].used=1; FS[i].folder=2; g_fpl[i]=0; desk_place_file(i); toast("Duplicated"); break; } }
        else if(k==3){ fs_drop_name(s2); g_fpl[s2]=0; toast("Deleted"); }
        else { g_prop=1; g_prop_slot=s2; g_prop_app=-1; }
        return 1; }
    /* desktop menu (kind 0): View/Sort by/New are submenus (no direct action) */
    if(seq(it[k],"Refresh")){ build_bg(); toast("Refreshed"); }
    else if(seq(it[k],"Display settings")){ open_win(W_SETTINGS); g_setcat=3; }
    else if(seq(it[k],"Personalize")){ open_win(W_SETTINGS); g_setcat=1; }
    else if(seq(it[k],"Open Terminal")){ open_win(W_TERM); }
    else if(seq(it[k],"Change wallpaper")){ g_theme=(g_theme+1)%NWALL; g_wallmode=0; build_bg(); toast(THN[g_theme]); }
    return 1;
}

