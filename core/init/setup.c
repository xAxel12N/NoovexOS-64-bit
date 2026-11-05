#include "include/nvx/base.h"
#include "include/nvx/mem/heap.h"
#include "include/nvx/fs/vfs/dirtree.h"
#include "include/nvx/fs/vfs/ramfs.h"
#include "include/nvx/lib/base/mem.h"
#include "include/nvx/lib/base/fmt.h"
#include "include/nvx/drivers/video/fb/framebuffer.h"
#include "include/nvx/graphics/theme/effects.h"
#include "include/nvx/drivers/rtc/cmos.h"
#include "include/nvx/net/http/http.h"
#include "include/nvx/graphics/theme/theme.h"
#include "include/nvx/graphics/ui/wm.h"
#include "include/nvx/core/power.h"
#include "include/nvx/apps/internet/browser.h"
#include "include/nvx/apps/system/settings.h"
#include "include/nvx/graphics/ui/desktop.h"
#include "include/nvx/core/focus.h"
#include "include/nvx/core/init/bootmenu.h"
#include "include/nvx/core/init/installer.h"
#include "include/nvx/core/init/setup.h"
#include "include/nvx/arch/x86_64/cpu/percpu.h"
#include "include/nvx/core/auth/accounts.h"
#include "include/nvx/core/auth/acctui.h"
#include "include/nvx/fs/vfs/unixfs.h"
#include "include/nvx/core/klog.h"
#include "include/nvx/core/auth/faceid.h"
#include "include/nvx/assets/uifont.h"
/* =========================================================================
   Setup (first-run) and Login screens
   ========================================================================= */
static void screen_bg(void){
    for(u32 y=0;y<H;y++){ u32 c=mix(rgb(16,20,34),rgb(10,12,22),(int)((y*256)/H)); for(u32 x=0;x<W;x++) SCENE[(u64)y*SW+x]=c; }
    radial(W/2,H*36/100,H*44/100,rgb(120,60,200),90); radial(W/2,H*58/100,H*34/100,rgb(40,150,180),70);
}
static int text_field(int x,int y,int w,const char*s,int len,int focus,const char*ph,int mask){
    fill_round(x,y,w,34,8,15,rgb(238,241,246));
    if(len==0 && ph){ dstr(x+12,y+10,ph,rgb(160,168,182),1); }
    else { int cx2=x+12; for(int k=0;k<len;k++){ char c=mask?'*':s[k]; dchar(cx2,y+10,c,rgb(40,46,64),1); cx2+=8; }
        if(focus&&((g_ticks/50)&1)) fill_rect(cx2+1,y+9,2,15,ACC); }
    return 0;
}

/* ================= multi-page first-time setup ================= */
static int setup_acc=0, setup_th=2;
static int su_nx,su_ny,su_nw, su_ax2[5],su_ay, su_wx2[3],su_wy;
#define NSU 11
enum { SU_LANG, SU_KBD, SU_TZ, SU_LOOK, SU_HOST, SU_ROOT, SU_USER, SU_FACE, SU_CLOUD, SU_STORE, SU_DONE };
static const char* SU_LANGS[6]={"English (United States)","Svenska (Sverige)","Deutsch (Deutschland)","Francais (France)","Espanol (Espana)","Italiano (Italia)"};
static const char* SU_KBDS[6] ={"US QWERTY","Swedish (Sverige)","German QWERTZ","French AZERTY","Spanish QWERTY","UK QWERTY"};
static const char* SU_TZS[7]  ={"(UTC-08:00) Los Angeles","(UTC-05:00) New York","(UTC+00:00) London","(UTC+01:00) Stockholm","(UTC+03:00) Moscow","(UTC+05:30) Mumbai","(UTC+09:00) Tokyo"};
static const char* SU_STORAGE[2]={"Cloud  -  files sync to your Noovex account","Local  -  files stay on this device only"};
static const char* SU_TITLE[NSU]={"Choose your language","Keyboard layout","Select your timezone","Make it yours","Computer Name","Root Password Setup","Create Your Account","Face ID","Sign in with Noovex","Where your files live","You are all set"};
static const char* SU_DESC[NSU]={
    "Select the language you would like to use with NoovexOS.",
    "Pick the layout that matches your keyboard.",
    "So NoovexOS shows the correct local time.",
    "Choose an accent colour and a wallpaper.",
    "Enter a name for this computer (letters, digits and -):",
    "Enter password for the 'root' administrator account:",
    "Enter your details. This account is an administrator.",
    "Sign in by looking at the camera. Your password always works too.",
    "Connect your Noovex account - or skip and do it later.",
    "Cloud files sync to your account and return after reboot.",
    "Welcome aboard. Press Get Started to enter NoovexOS."};
static const char* SU_FACEOPT[2]={"Set up Face ID now","Skip - set it up later in Settings > Accounts"};
static int su_face_sel=0, su_face_done=0, su_face_scan=0; static u64 su_face_okt; static char su_face_user[34];
static const char* SU_ACC[3]={"Sign in with Noovex","Create a Noovex account","Skip for now"};
static int g_su_page=0, g_su_lang=0, g_su_kbd=1, g_su_tz=3, g_su_acc=2, g_su_storage=0;
static char su_host[32]="noovex"; static int su_hl=6;
static char su_rpw[2][68]; static int su_rpl[2], su_rfoc;
static char su_full[64], su_user[34], su_upw[2][68]; static int su_fl, su_unl, su_upl[2], su_ufoc, su_utouched;
static char su_err[120];
static int su_fx,su_fw,su_fy[4];
static void su_wipe_pw(void){ for(int i=0;i<2;i++){ for(int k=0;k<68;k++){ su_rpw[i][k]=0; su_upw[i][k]=0; } su_rpl[i]=0; su_upl[i]=0; } }
int nvx_tz_index(void){ return g_su_tz; }
int g_krep_speed=1;
void kbd_apply_repeat(void){ extern int g_krep_delay, g_krep_rate; static const int D[3]={70,50,30}, R[3]={6,3,2}; if(g_krep_speed<0||g_krep_speed>2)g_krep_speed=1; g_krep_delay=D[g_krep_speed]; g_krep_rate=R[g_krep_speed]; }
static void su_save_file(void){
    if(k_find_file("/etc/nvx-installed")<0) return;
    extern void nvx_mark_file(const char*,const char*); extern int g_lid_action, g_pbtn_action; char t[512],*p=t; p=scopy(p,"user="); p=scopy(p,g_user); p=scopy(p,"\naccent="); *p++=(char)('0'+g_accent); p=scopy(p,"\ntheme="); *p++=(char)('0'+g_theme); p=scopy(p,"\nlang="); p=udec(p,(u32)g_su_lang); p=scopy(p,"\nkbd="); p=udec(p,(u32)g_su_kbd); p=scopy(p,"\ntz="); p=udec(p,(u32)g_su_tz); p=scopy(p,"\nkrep="); p=udec(p,(u32)g_krep_speed); p=scopy(p,"\nlid="); p=udec(p,(u32)g_lid_action); p=scopy(p,"\npbtn="); p=udec(p,(u32)g_pbtn_action); { extern int g_guest_on, g_lock_min; p=scopy(p,"\nguest="); p=udec(p,(u32)(g_guest_on?1:0)); p=scopy(p,"\nlockmin="); p=udec(p,(u32)g_lock_min); } { extern u32 g_svc_off; p=scopy(p,"\nsvcoff="); p=udec(p,g_svc_off); } { extern int g_web_netbg, g_web_csp, g_ck_samesite; p=scopy(p,"\nbroff="); p=udec(p,(u32)((g_web_netbg?0:1)|(g_web_csp?0:2)|(g_ck_samesite?0:4))); } p=scopy(p,"\n"); *p=0; nvx_mark_file("/etc/nvx-setup-done",t);
}
static int su_lsel[3]={0,1,3};
static int su_ix,su_iw,su_iy0,su_ih,su_ign,su_ig, su_cbx,su_cby,su_cbw,su_cbh, su_bbx,su_bby,su_bbw,su_bbh, su_signx,su_signy,su_signw,su_signh;
static int g_su_form=0, g_su_field=0;                       /* 0=chooser 1=signin 2=create */
static char su_email[64]="", su_uname[48]="", su_pwd[48]=""; static int su_el=0,su_ul=0,su_pl=0;
static int su_f_x,su_f_w,su_f_h, su_f_ey,su_f_uy,su_f_py;
static void su_cbtn(int cx,int by,const char* t,int foc,int* rx,int* ry,int* rw,int* rh){
    int w=(int)slen(t)*8+32, x=cx-w/2; int hov=in_rect(mx,my,x,by,w,20);
    if(foc)fill_rect(x,by,w,20,hov?rgb(230,80,90):rgb(210,60,70));
    u32 c=foc?rgb(255,255,255):(hov?rgb(255,120,130):rgb(210,60,70));
    dstr(x+4,by+3,"<",c,1); dstr(x+16,by+3,t,c,1); dstr(x+16+(int)slen(t)*8+4,by+3,">",c,1);
    *rx=x; *ry=by; *rw=w; *rh=20;
}
static void su_rows(int ox,int oy,int ow,const char** it,int n,int sel){
    su_ix=ox; su_iw=ow; su_iy0=oy; su_ih=20; su_ig=2; su_ign=n;
    for(int k=0;k<n;k++){ int y=oy+k*22, sl=(k==sel);
        fill_rect(ox,y,ow,20,sl?rgb(20,24,34):rgb(248,249,251));
        char b[96],*p=b; p=scopy(p,sl?"(*) ":"( ) "); p=scopy(p,it[k]); *p=0;
        dstr(ox+8,y+3,b,sl?rgb(255,255,255):rgb(20,24,34),1); }
}
static void su_cfield(int x,int y,int w,const char* label,const char* val,int vlen,int focused,int mask,const char* ph){
    dstr(x,y,label,rgb(20,24,34),1); y+=18;
    fill_rect(x,y,w,22,focused?rgb(20,24,34):rgb(150,156,170)); fill_rect(x+2,y+2,w-4,18,rgb(255,255,255));
    int cxp=x+8;
    if(vlen==0&&ph) dstr(cxp,y+4,ph,rgb(150,156,170),1);
    else for(int k=0;k<vlen&&cxp<x+w-16;k++){ dchar(cxp,y+4,mask?'*':val[k],rgb(20,24,34),1); cxp+=8; }
    if(focused&&((g_ticks/50)&1)&&!(vlen==0&&ph)) fill_rect(cxp+1,y+4,2,14,rgb(20,24,34));
    else if(focused&&((g_ticks/50)&1)) fill_rect(x+8,y+4,2,14,rgb(20,24,34));
}
static int su_page_h(int P){
    if(P==SU_LANG||P==SU_KBD) return 6*22;
    if(P==SU_TZ) return 7*22;
    if(P==SU_LOOK) return 118;
    if(P==SU_HOST) return 44+18;
    if(P==SU_ROOT) return 2*44+18;
    if(P==SU_USER) return 4*44;
    if(P==SU_FACE) return 2*22+48;
    if(P==SU_CLOUD) return (g_su_form==0)?3*22+24:112;
    if(P==SU_STORE) return 2*22+24;
    return 108;
}
void setup_present(void){
    int P=g_su_page;
    fill_rect(0,0,(int)W,(int)H,rgb(72,130,200));
    int ch=su_page_h(P); if(su_err[0]&&(P==SU_HOST||P==SU_ROOT||P==SU_USER))ch+=22;
    const char* ptitle=(P==SU_CLOUD&&g_su_form==2)?"Create a Noovex account":(P==SU_CLOUD&&g_su_form==1)?"Sign in with Noovex":SU_TITLE[P];
    int ox,oy,ow; ip_box(&ox,&oy,&ow,560,ch+132,ptitle);
    dstr(ox,oy,SU_DESC[P],rgb(20,24,34),1); oy+=18;
    { char t[24],*p=t; p=scopy(p,"Step "); p=udec(p,(u32)(P+1)); p=scopy(p," of "); p=udec(p,(u32)NSU); *p=0; dstr(ox,oy,t,rgb(110,118,135),1); } oy+=28;
    su_fx=ox; su_fw=ow;
    if(P==SU_LANG) su_rows(ox,oy,ow,SU_LANGS,6,su_lsel[0]);
    else if(P==SU_KBD) su_rows(ox,oy,ow,SU_KBDS,6,su_lsel[1]);
    else if(P==SU_TZ) su_rows(ox,oy,ow,SU_TZS,7,su_lsel[2]);
    else if(P==SU_LOOK){
        int y=oy; (void)su_nx; (void)su_ny; (void)su_nw;
        dstr(ox,y,"Accent colour",rgb(20,24,34),1); y+=18; su_ay=y;
        for(int k=0;k<5;k++){ int bx=ox+k*44; su_ax2[k]=bx; if(k==setup_acc) fill_rect(bx,su_ay,32,32,rgb(20,24,34)); fill_rect(bx+3,su_ay+3,26,26,rgb(248,249,251)); fill_rect(bx+5,su_ay+5,22,22,ACV[k]); }
        y=su_ay+44; dstr(ox,y,"Wallpaper",rgb(20,24,34),1); y+=18; su_wy=y;
        for(int k=0;k<3;k++){ int bx=ox+k*64; su_wx2[k]=bx; if(k==setup_th) fill_rect(bx-3,su_wy-3,60,40,rgb(20,24,34)); fill_rect(bx,su_wy,54,34,THSW[k]); }
    }
    else if(P==SU_HOST){
        su_fy[0]=oy; acct_field(ox,oy,ow,0,su_host,su_hl,0,1);
        int y=oy+44; if(su_err[0]){ dstr(ox,y-6,su_err,rgb(190,40,50),1); y+=22; }
        dstr(ox,y-6,"Press Enter to confirm, Backspace to edit.",rgb(20,24,34),1);
    }
    else if(P==SU_ROOT){
        su_fy[0]=oy; acct_field(ox,oy,ow,"Password:",su_rpw[0],su_rpl[0],1,su_rfoc==0);
        su_fy[1]=oy+44; acct_field(ox,oy+44,ow,"Type it again:",su_rpw[1],su_rpl[1],1,su_rfoc==1);
        int y=oy+88; if(su_err[0]){ dstr(ox,y-6,su_err,rgb(190,40,50),1); y+=22; }
        dstr(ox,y-6,"Press Enter to confirm, Backspace to edit.",rgb(20,24,34),1);
    }
    else if(P==SU_USER){
        su_fy[0]=oy; acct_field(ox,oy,ow,"Full name:",su_full,su_fl,0,su_ufoc==0);
        su_fy[1]=oy+44; acct_field(ox,oy+44,ow,"User name (a-z, 0-9, - and _):",su_user,su_unl,0,su_ufoc==1);
        su_fy[2]=oy+88; acct_field(ox,oy+88,ow,"Password:",su_upw[0],su_upl[0],1,su_ufoc==2);
        su_fy[3]=oy+132; acct_field(ox,oy+132,ow,"Type the password again:",su_upw[1],su_upl[1],1,su_ufoc==3);
        if(su_err[0]) dstr(ox,oy+172,su_err,rgb(190,40,50),1);
    }
    else if(P==SU_FACE){
        if(su_face_done&&!seq(su_face_user,su_user)){ fid_delete(su_face_user); su_face_done=0; }
        if(su_face_done){ dstr(ox,oy,"Face ID is set up for this account.",rgb(20,24,34),1); dstr(ox,oy+22,"Press Continue.",rgb(110,118,135),1); }
        else if(!fid_camera_present()){ dstr(ox,oy,"No camera was found.",rgb(20,24,34),1); dstr(ox,oy+22,"You can set up Face ID later in Settings > Accounts.",rgb(110,118,135),1); }
        else { su_rows(ox,oy,ow,SU_FACEOPT,2,su_face_sel); dstr(ox,oy+2*22+8,"Only numbers that describe your face are saved, never pictures.",rgb(110,118,135),1); }
        if(su_err[0]) dstr(ox,oy+2*22+28,su_err,rgb(190,40,50),1);
    }
    else if(P==SU_CLOUD){
        if(g_su_form==0){ su_rows(ox,oy,ow,SU_ACC,3,g_su_acc); dstr(ox,oy+3*22+6,"Your local account stays the same; this only links a Noovex account.",rgb(110,118,135),1); }
        else { su_f_x=ox; su_f_w=ow; su_f_h=22; su_f_uy=-1;
            su_f_ey=oy+18; su_cfield(ox,oy,ow,"Username",su_uname,su_ul,g_su_field==0,0,0);
            su_f_py=oy+18+48; su_cfield(ox,oy+48,ow,"Password",su_pwd,su_pl,g_su_field==1,1,0);
            if(su_msg[0]) dstr(ox,oy+96,su_msg,rgb(190,40,50),1);
            else dstr(ox,oy+96,g_su_form==2?"Pick a username and password for your account.":"TAB switches fields    ENTER submits",rgb(110,118,135),1); }
    }
    else if(P==SU_STORE){ su_rows(ox,oy,ow,SU_STORAGE,2,g_su_storage);
        dstr(ox,oy+2*22+6,g_nvx_signedin?"Cloud saves your files to your account.":"Cloud needs a signed-in account - otherwise files stay local.",rgb(110,118,135),1); }
    else {
        dstr(ox,oy,"Everything is ready.",rgb(20,24,34),1);
        char b[96],*p;
        p=b; p=scopy(p,"Account:   "); p=scopy(p,su_user); if(su_fl){ p=scopy(p," ("); p=scopy(p,su_full); p=scopy(p,")"); } *p=0; dstr(ox,oy+22,b,rgb(20,24,34),1);
        p=b; p=scopy(p,"Computer:  "); p=scopy(p,su_host); p=scopy(p,su_face_done?"     Face ID:  set up":""); *p=0; dstr(ox,oy+40,b,rgb(20,24,34),1);
        p=b; p=scopy(p,"Language:  "); p=scopy(p,SU_LANGS[su_lsel[0]]); *p=0; dstr(ox,oy+58,b,rgb(20,24,34),1);
        p=b; p=scopy(p,"Keyboard:  "); p=scopy(p,SU_KBDS[su_lsel[1]]); *p=0; dstr(ox,oy+76,b,rgb(20,24,34),1);
        p=b; p=scopy(p,"Timezone:  "); p=scopy(p,SU_TZS[su_lsel[2]]); *p=0; dstr(ox,oy+94,b,rgb(20,24,34),1);
    }
    int by=oy+ch+20;
    const char* clab=(P==NSU-1)?"Get Started":(P==SU_CLOUD?(g_su_form==1?"Sign in":g_su_form==2?"Create account":"Continue"):"Continue");
    if(P>0){ su_cbtn(ox+ow/3,by,"Back",0,&su_bbx,&su_bby,&su_bbw,&su_bbh); su_cbtn(ox+ow*2/3,by,clab,1,&su_cbx,&su_cby,&su_cbw,&su_cbh); }
    else { su_bbx=-1000; su_bby=-1000; su_bbw=0; su_bbh=0; su_cbtn(ox+ow/2,by,clab,1,&su_cbx,&su_cby,&su_cbw,&su_cbh); }
    if(P==SU_FACE&&(g_fid_mode||g_fid_state==FID_SUCCESS)&&su_face_scan)fid_scan_draw((int)W/2);
    draw_cursor(); blit_all();
}
static void su_apply_prefs(void){
    g_accent=setup_acc; ACC=ACV[setup_acc]; ACC2=mix(ACC,rgb(0,0,0),60);
    g_theme=setup_th; g_su_lang=su_lsel[0]; g_su_kbd=su_lsel[1]; { extern int g_kbd_layout; g_kbd_layout=g_su_kbd; } g_su_tz=su_lsel[2];
}
static int su_accounts_done=0;
static void su_apply(void){
    su_apply_prefs();
    if(!su_accounts_done){
        if(acct_setup_primary(su_user,su_full,su_upw[0],su_rpw[0],su_host)==0){ su_accounts_done=1; }
        else { klog2("setup: accounts: ",g_acct_err); return; }
        { int k=0; for(;su_user[k]&&k<31;k++)g_user[k]=su_user[k]; g_user[k]=0; }
        { int k=0; for(;su_full[k]&&k<63;k++)g_fullname[k]=su_full[k]; g_fullname[k]=0; }
    }
    g_cloud_fs=(g_su_storage==0 && g_nvx_signedin); build_bg(); nvx_settings_save(); if(g_cloud_fs)nvx_files_pull();
    su_save_file();
}
static int su_kv(const char* d,int n,const char* key,char* out,int cap){ int kl=0; while(key[kl])kl++;
    for(int i=0;i<n;){ int j=i; while(j<n&&d[j]!='\n')j++; if(j-i>kl&&d[i+kl]=='='){ int m=1; for(int q=0;q<kl;q++) if(d[i+q]!=key[q]){ m=0; break; } if(m){ int o=0; for(int q=i+kl+1;q<j&&o<cap-1;q++)out[o++]=d[q]; out[o]=0; return o; } } i=j+1; }
    out[0]=0; return -1; }
static int g_lg_installed=0; static int g_lg_liveret=0;
int lg_installed(void){ return g_lg_installed; }
int live_lock_start(void){ extern int live_lock_on(void); if(g_lg_installed||live_lock_on()!=1)return 0; g_lg_liveret=1; g_locked=0; login_pass[0]=0; login_pl=0; g_state=ST_LOGIN; return 1; }
static void lg_live_unlock(void){ extern void live_invalidate(void); for(int i=0;i<32;i++)login_pass[i]=0; login_pl=0; g_locked=0; g_lg_liveret=0; g_state=ST_LIVE; live_invalidate(); }
int installed_boot(void){
    extern int disk_ok; if(!disk_ok||k_find_file("/etc/nvx-installed")<0)return 0;
    g_lg_installed=1;
    ux_core_start(0);
    g_logged_in=0; g_locked=0; g_uid=0; g_gid=0;
    int k=k_find_file("/etc/nvx-setup-done"); if(k<0){ setup_begin(); return 1; }
    const char* d=fsd(k); int n=FS[k].len; char v[40];
    if(su_kv(d,n,"user",v,32)>0){ int q=0; for(;v[q]&&q<31;q++)g_user[q]=v[q]; g_user[q]=0; }
    if(su_kv(d,n,"accent",v,8)>0&&v[0]>='0'&&v[0]<='4'){ g_accent=v[0]-'0'; ACC=ACV[g_accent]; ACC2=mix(ACC,rgb(0,0,0),60); }
    if(su_kv(d,n,"theme",v,8)>0&&v[0]>='0'&&v[0]<='5')g_theme=v[0]-'0';
    if(su_kv(d,n,"lang",v,8)>0)g_su_lang=v[0]-'0'; if(su_kv(d,n,"kbd",v,8)>0)g_su_kbd=v[0]-'0'; { extern int g_kbd_layout; if(g_su_kbd>=0&&g_su_kbd<6)g_kbd_layout=g_su_kbd; } if(su_kv(d,n,"tz",v,8)>0)g_su_tz=v[0]-'0'; if(su_kv(d,n,"krep",v,8)>0){ g_krep_speed=v[0]-'0'; kbd_apply_repeat(); } { extern int g_lid_action, g_pbtn_action; if(su_kv(d,n,"lid",v,8)>0&&v[0]>='0'&&v[0]<='2')g_lid_action=v[0]-'0'; if(su_kv(d,n,"pbtn",v,8)>0&&v[0]>='0'&&v[0]<='2')g_pbtn_action=v[0]-'0'; } { extern u32 g_svc_off; extern void svc_apply_saved(void); if(su_kv(d,n,"svcoff",v,8)>0){ u32 m=0; for(int i=0;v[i]>='0'&&v[i]<='9';i++)m=m*10+(u32)(v[i]-'0'); g_svc_off=m; svc_apply_saved(); } } { extern int g_web_netbg, g_web_csp, g_ck_samesite; if(su_kv(d,n,"broff",v,8)>0&&v[0]>='0'&&v[0]<='7'){ int m=v[0]-'0'; g_web_netbg=!(m&1); g_web_csp=!(m&2); g_ck_samesite=!(m&4); } }
    if(su_kv(d,n,"guest",v,8)>0)g_guest_on=(v[0]=='1');
    if(su_kv(d,n,"lockmin",v,8)>0){ int m=0; for(int i=0;v[i]>='0'&&v[i]<='9';i++)m=m*10+(v[i]-'0'); if(m>=0&&m<=240)g_lock_min=m; }
    { acct_info ai; if(acct_get(g_user,&ai)<0){ ux_legacy_account(g_user); } if(acct_get(g_user,&ai)==0){ int q=0; for(;ai.full[q]&&q<63;q++)g_fullname[q]=ai.full[q]; g_fullname[q]=0; } }
    build_bg(); g_locked=0; acct_set_switch_mode(0); g_state=ST_LOGIN; login_pass[0]=0; login_pl=0; return 1; }
static void su_suggest_user(void){
    if(su_utouched)return; int o=0;
    for(int i=0;su_full[i]&&o<32;i++){ char c=su_full[i]; if(c==' '){ if(o)break; continue; } if(c>='A'&&c<='Z')c=(char)(c+32); if((c>='a'&&c<='z')||(c>='0'&&c<='9'&&o)||((c=='-'||c=='_')&&o))su_user[o++]=c; }
    su_user[o]=0; su_unl=o;
}
static int su_check_page(void){
    su_err[0]=0; int P=g_su_page;
    if(P==SU_HOST){ if(!acct_valid_host(su_host)){ scopy(su_err,"Use 1-31 letters, digits or - (not at the start or end)."); return 0; } }
    if(P==SU_ROOT){
        if(su_rpl[0]<4){ scopy(su_err,"The root password needs at least 4 characters."); su_rfoc=0; return 0; }
        int same=1; for(int i=0;;i++){ if(su_rpw[0][i]!=su_rpw[1][i]){ same=0; break; } if(!su_rpw[0][i])break; }
        if(!same){ scopy(su_err,"The two passwords are not the same."); su_rfoc=1; return 0; } }
    if(P==SU_USER){
        for(int i=0;su_full[i];i++) if(su_full[i]==':'||su_full[i]==','){ scopy(su_err,"The full name cannot contain : or ,"); su_ufoc=0; return 0; }
        if(!acct_valid_name(su_user)){ scopy(su_err,"User names start with a-z and use a-z, 0-9, - or _ (not root, guest...)."); su_ufoc=1; return 0; }
        if(su_upl[0]<4){ scopy(su_err,"Your password needs at least 4 characters."); su_ufoc=2; return 0; }
        int same=1; for(int i=0;;i++){ if(su_upw[0][i]!=su_upw[1][i]){ same=0; break; } if(!su_upw[0][i])break; }
        if(!same){ scopy(su_err,"The two passwords are not the same."); su_ufoc=3; return 0; } }
    return 1;
}
static void su_finish(void){
    su_apply();
    int ok=su_accounts_done;
    if(!ok){ g_su_page=SU_USER; su_ufoc=1; { int k=0; for(;g_acct_err[k]&&k<118;k++)su_err[k]=g_acct_err[k]; su_err[k]=0; } if(!su_err[0])scopy(su_err,"The account could not be created."); return; }
    su_wipe_pw();
    klog("setup: finished - showing the sign-in screen"); g_lg_installed=1; g_lg_liveret=0;
    build_bg(); g_logged_in=0; g_locked=0; acct_set_switch_mode(0); g_state=ST_LOGIN; login_pass[0]=0; login_pl=0;
}
static void su_advance(void){
    if(g_su_page==SU_CLOUD){
        if(g_su_form==0){
            if(g_su_acc==2){ g_su_page++; return; }
            g_su_form=(g_su_acc==0)?1:2; g_su_field=0; su_msg[0]=0; return;
        }
        su_apply_prefs(); su_msg[0]=0;
        int rc=(g_su_form==1)? nvx_signin(su_uname,su_pwd) : nvx_register(su_uname,su_pwd);
        if(rc==0){ g_su_form=0; g_su_page++; }
        return;
    }
    if(g_su_page==SU_FACE&&!su_face_done&&su_face_sel==0&&fid_camera_present()){ su_err[0]=0;
        if(fid_begin(FID_ENROLL,su_user)<0){ int k=0; for(;g_fid_msg[k]&&k<110;k++)su_err[k]=g_fid_msg[k]; su_err[k]=0; return; }
        su_face_scan=1; su_face_okt=0; return; }
    if(g_su_page==SU_ROOT&&su_rfoc==0&&su_rpl[0]>0&&su_rpl[1]==0){ su_rfoc=1; return; }
    if(g_su_page==SU_USER&&su_ufoc<3){ su_ufoc++; return; }
    if(!su_check_page())return;
    if(g_su_page>=NSU-1){ su_finish(); return; }
    g_su_page++;
}
static void su_edit(char* buf,int* len,int cap,int c){
    if(c=='\b'){ if(*len>0)(*len)--; buf[*len]=0; }
    else if(c>=32&&c<127){ if(*len<cap){ buf[(*len)++]=(char)c; buf[*len]=0; } }
}
int setup_input(void){
    int changed=0, P=g_su_page;
    if(su_face_scan){ { static u64 lr; if(g_ticks-lr>=25){ lr=g_ticks; changed=1; } }
        if(g_fid_state==FID_SUCCESS){ if(!su_face_okt)su_face_okt=g_ticks; if(g_ticks-su_face_okt>120){ fid_end(); su_face_scan=0; su_face_done=1; g_fid_state=FID_IDLE; int k=0; for(;su_user[k]&&k<33;k++)su_face_user[k]=su_user[k]; su_face_user[k]=0; } }
        else if(g_fid_state==FID_FAILED||g_fid_state==FID_ERROR||!g_fid_mode){ int k=0; for(;g_fid_msg[k]&&k<110;k++)su_err[k]=g_fid_msg[k]; su_err[k]=0; fid_end(); su_face_scan=0; }
        if(mouse_evt){ int dx,dy,btn; cpu_irq_off(); dx=mouse_dx;dy=mouse_dy;mouse_dx=0;mouse_dy=0;btn=mouse_btn;mouse_evt=0; cpu_irq_on();
            if(dx||dy){ mx+=dx;my-=dy; if(mx<0)mx=0;if(my<0)my=0;if(mx>=(int)W)mx=W-1;if(my>=(int)H)my=H-1; }
            if(btn&&!mouse_btn_prev&&su_face_scan&&g_fid_state!=FID_SUCCESS&&fid_scan_cancel_hit(mx,my)){ fid_end(); su_face_scan=0; }
            mouse_btn_prev=btn; }
        int c; while((c=kpop())>=0){ if(c==27&&su_face_scan&&g_fid_state!=FID_SUCCESS){ fid_end(); su_face_scan=0; } }
        return changed; }
    if(mouse_evt){ int dx,dy,btn; cpu_irq_off(); dx=mouse_dx;dy=mouse_dy;mouse_dx=0;mouse_dy=0;btn=mouse_btn;mouse_evt=0; cpu_irq_on();
        if(dx||dy){ mx+=dx;my-=dy; if(mx<0)mx=0;if(my<0)my=0;if(mx>=(int)W)mx=W-1;if(my>=(int)H)my=H-1; changed=1; }
        if(btn&&!mouse_btn_prev){ changed=1;
            if(P<=SU_TZ){ int* sel=&su_lsel[P]; for(int k=0;k<su_ign;k++) if(in_rect(mx,my,su_ix,su_iy0+k*(su_ih+su_ig),su_iw,su_ih)){ *sel=k; } }
            if(P==SU_LOOK){ for(int k=0;k<5;k++) if(in_rect(mx,my,su_ax2[k],su_ay,32,32)){ setup_acc=k; ACC=ACV[k]; ACC2=mix(ACC,rgb(0,0,0),60); g_accent=k; }
                for(int k=0;k<3;k++) if(in_rect(mx,my,su_wx2[k],su_wy,54,34)) setup_th=k; }
            if(P==SU_ROOT){ for(int k=0;k<2;k++) if(in_rect(mx,my,su_fx,su_fy[k],su_fw,36))su_rfoc=k; }
            if(P==SU_USER){ for(int k=0;k<4;k++) if(in_rect(mx,my,su_fx,su_fy[k],su_fw,36))su_ufoc=k; }
            if(P==SU_CLOUD){ if(g_su_form==0){ for(int k=0;k<su_ign;k++) if(in_rect(mx,my,su_ix,su_iy0+k*(su_ih+su_ig),su_iw,su_ih)) g_su_acc=k; }
                else { if(in_rect(mx,my,su_f_x,su_f_ey,su_f_w,su_f_h)) g_su_field=0;
                    else if(in_rect(mx,my,su_f_x,su_f_py,su_f_w,su_f_h)) g_su_field=1; } }
            if(P==SU_STORE){ for(int k=0;k<su_ign;k++) if(in_rect(mx,my,su_ix,su_iy0+k*(su_ih+su_ig),su_iw,su_ih)) g_su_storage=k; }
            if(P==SU_FACE&&!su_face_done&&fid_camera_present()){ for(int k=0;k<2;k++) if(in_rect(mx,my,su_ix,su_iy0+k*(su_ih+su_ig),su_iw,su_ih)) su_face_sel=k; }
            if(P>0 && in_rect(mx,my,su_bbx,su_bby,su_bbw,su_bbh)){ su_err[0]=0; if(P==SU_CLOUD&&g_su_form!=0){ g_su_form=0; su_msg[0]=0; } else g_su_page--; }
            else if(in_rect(mx,my,su_cbx,su_cby,su_cbw,su_cbh)){ if(P==SU_USER)su_ufoc=3; if(P==SU_ROOT)su_rfoc=1; su_advance(); }
        }
        mouse_btn_prev=btn; }
    int c; while((c=kpop())>=0){ changed=1; P=g_su_page;
        if(c==0x1B){ su_err[0]=0; if(P==SU_CLOUD&&g_su_form!=0){ g_su_form=0; su_msg[0]=0; } else if(g_su_page>0)g_su_page--; continue; }
        if(c=='\n'||c=='\r'){ su_advance(); continue; }
        if(P==SU_HOST){ if(c!='\t'){ su_edit(su_host,&su_hl,31,c); su_err[0]=0; } continue; }
        if(P==SU_ROOT){ if(c=='\t'||c==20){ su_rfoc=!su_rfoc; continue; } if(c==19){ su_rfoc=0; continue; } su_edit(su_rpw[su_rfoc],&su_rpl[su_rfoc],64,c); su_err[0]=0; continue; }
        if(P==SU_USER){ if(c=='\t'||c==20){ su_ufoc=(su_ufoc+1)%4; continue; } if(c==19){ su_ufoc=(su_ufoc+3)%4; continue; }
            if(su_ufoc==0){ su_edit(su_full,&su_fl,60,c); su_suggest_user(); }
            else if(su_ufoc==1){ su_edit(su_user,&su_unl,32,c); su_utouched=1; }
            else su_edit(su_upw[su_ufoc-2],&su_upl[su_ufoc-2],64,c);
            su_err[0]=0; continue; }
        if(P==SU_CLOUD && g_su_form!=0){
            if(c=='\t'){ g_su_field=!g_su_field; continue; }
            char* buf; int* len; int cap;
            if(g_su_field==0){ buf=su_uname; len=&su_ul; cap=47; } else { buf=su_pwd; len=&su_pl; cap=47; }
            su_edit(buf,len,cap,c);
            continue;
        }
        if(P==SU_FACE&&(c==19||c==20||c=='w'||c=='s')){ su_face_sel=(c==19||c=='w')?0:1; continue; }
        if(c==19){ if(P<=SU_TZ && su_lsel[P]>0)su_lsel[P]--; else if(P==SU_CLOUD && g_su_acc>0)g_su_acc--; else if(P==SU_STORE && g_su_storage>0)g_su_storage--; continue; }
        if(c==20){ if(P<=SU_TZ && su_lsel[P]<su_ign-1)su_lsel[P]++; else if(P==SU_CLOUD && g_su_acc<2)g_su_acc++; else if(P==SU_STORE && g_su_storage<1)g_su_storage++; continue; }
        if(P==SU_STORE && c=='w'){ if(g_su_storage>0)g_su_storage--; continue; }
        if(P==SU_STORE && c=='s'){ if(g_su_storage<1)g_su_storage++; continue; }
        if(P<=SU_TZ && c=='w'){ if(su_lsel[P]>0)su_lsel[P]--; continue; }
        if(P<=SU_TZ && c=='s'){ if(su_lsel[P]<su_ign-1)su_lsel[P]++; continue; }
        if(P==SU_CLOUD && g_su_form==0 && c=='w'){ if(g_su_acc>0)g_su_acc--; continue; }
        if(P==SU_CLOUD && g_su_form==0 && c=='s'){ if(g_su_acc<2)g_su_acc++; continue; }
    }
    static int lb=-1; int bp=(int)((g_ticks/50)&1); if(bp!=lb){lb=bp;changed=1;}
    return changed;
}

/* ---- login ---- */
static int lg_bx,lg_by,lg_bw,lg_bh, lg_px,lg_py,lg_pw;
const char *MON[12]={"January","February","March","April","May","June","July","August","September","October","November","December"};
static int lg_avx,lg_avy,lg_avr;
static char lg_sel[34]; static int lg_fail; static u64 lg_wait_until; static char lg_msg[96];
static acct_info lg_users[8]; static int lg_nu; static int lg_ux[8],lg_uy[8],lg_uw[8]; static int lg_swx=-1000,lg_swy,lg_sww;
static int lg_list_shown(void){ return lg_nu>1&&(!g_logged_in||acct_switch_mode()); }
static void lg_refresh(void){
    if(!g_lg_installed){ lg_nu=0; int q=0; for(;g_user[q]&&q<32;q++)lg_sel[q]=g_user[q]; lg_sel[q]=0; return; }
    lg_nu=acct_list(lg_users,8,1);
    if(g_logged_in&&!acct_switch_mode()){ int q=0; for(;g_user[q]&&q<32;q++)lg_sel[q]=g_user[q]; lg_sel[q]=0; return; }
    int found=0; for(int i=0;i<lg_nu;i++) if(seq(lg_users[i].name,lg_sel))found=1;
    if(!found){ int pick=-1; for(int i=0;i<lg_nu;i++) if(seq(lg_users[i].name,g_user))pick=i; if(pick<0&&lg_nu>0)pick=0;
        if(pick>=0){ int q=0; for(;lg_users[pick].name[q]&&q<32;q++)lg_sel[q]=lg_users[pick].name[q]; lg_sel[q]=0; }
        else { int q=0; for(;g_user[q]&&q<32;q++)lg_sel[q]=g_user[q]; lg_sel[q]=0; } }
}
static const char* lg_display(char* out){
    for(int i=0;i<lg_nu;i++) if(seq(lg_users[i].name,lg_sel)){ const char* f=lg_users[i].full[0]?lg_users[i].full:lg_users[i].name; int q=0; for(;f[q]&&q<63;q++)out[q]=f[q]; out[q]=0; return out; }
    const char* f=(g_fullname[0]&&seq(lg_sel,g_user))?g_fullname:lg_sel; int q=0; for(;f[q]&&q<63;q++)out[q]=f[q]; out[q]=0; return out;
}
static int lg_sel_haspw(void){ if(!g_lg_installed)return 0; for(int i=0;i<lg_nu;i++) if(seq(lg_users[i].name,lg_sel))return lg_users[i].haspw; return acct_has_password(lg_sel); }
static int lg_fx,lg_fy,lg_fw,lg_fh, lg_pmx,lg_pmy, lg_pmenu, lg_pmi[3], lg_pmw;
static int lg_scan; static u64 lg_okt; static int lg_cancx,lg_cancy,lg_cancw;
static u32* lg_hbuf; static u32 lg_hw, lg_hh;
static void lg_hills(void){
    if(lg_hbuf&&lg_hw==W&&lg_hh==H){ u32* sc=SCENE; for(u32 y=0;y<H;y++)memcpy(sc+(u64)y*SW,lg_hbuf+(u64)y*W,(u64)W*4); return; }
    if(lg_hbuf){ kfree(lg_hbuf); lg_hbuf=0; }
    u32* sc=SCENE; u32 sky=rgb(223,230,238);
    int ex[3]={(int)W/2,200*(int)W/1280,(380+(1280+20)/2)*(int)W/1280}, ey[3]={870*(int)H/800,960*(int)H/800,940*(int)H/800};
    int rx[3]={((int)W+400*(int)W/1280)/2,500*(int)W/1280,((int)W+20*(int)W/1280)/2}, ry[3]={350*(int)H/800,300*(int)H/800,350*(int)H/800};
    u32 col[3]={rgb(195,208,220),rgb(145,166,184),rgb(168,184,199)};
    for(u32 y=0;y<H;y++){ u32* row=sc+(u64)y*SW; for(u32 x=0;x<W;x++)row[x]=sky;
        for(int e=0;e<3;e++){ int dy=(int)y-ey[e]; if(dy<-ry[e]||dy>ry[e])continue;
            u32 q=(u32)((u64)(ry[e]*ry[e]-dy*dy)*256*256/((u64)ry[e]*ry[e])); int hw=(int)((u64)rx[e]*isqrt32(q)/256);
            int x0=ex[e]-hw, x1=ex[e]+hw; if(x0<0)x0=0; if(x1>=(int)W)x1=(int)W-1;
            for(int x=x0;x<=x1;x++)row[x]=col[e];
            if(x0>0)row[x0-1]=mix(row[x0-1],col[e],128); if(x1<(int)W-1)row[x1+1]=mix(row[x1+1],col[e],128); } }
    lg_hbuf=(u32*)kmalloc((u64)W*H*4); if(lg_hbuf){ lg_hw=W; lg_hh=H; for(u32 y=0;y<H;y++)memcpy(lg_hbuf+(u64)y*W,sc+(u64)y*SW,(u64)W*4); } }
static void lg_rr(int x,int y,int w,int h,int r,u32 c,int a){
    u32* sc=SCENE; for(int j=0;j<h;j++){ int py=y+j; if((u32)py>=H)continue; u32* row=sc+(u64)py*SW;
        for(int i=0;i<w;i++){ int px=x+i; if((u32)px>=W)continue; if(cut(i,j,w,h,r,15))continue; row[px]=a>=256?c:mix(row[px],c,a); } } }
static void lg_shadow(int x,int y,int w,int h,int r){ for(int k=4;k>=1;k--)lg_rr(x-k+1,y+k,w+2*k-2,h+k,r+k,rgb(40,60,90),10); }
static void lg_disc(int cx,int cy,int r,u32 c,int a){
    u32* sc=SCENE; for(int j=-r-1;j<=r+1;j++){ int py=cy+j; if((u32)py>=H)continue; u32* row=sc+(u64)py*SW;
        for(int i=-r-1;i<=r+1;i++){ int px=cx+i; if((u32)px>=W)continue; int d2=i*i+j*j; int d4=(int)isqrt32((u32)d2*16); int e=r*4-d4; if(e<=-4)continue;
            int al=e>=4?a:a*(e+4)/8; row[px]=mix(row[px],c,al); } } }
static void lg_line(int x0,int y0,int x1,int y1,int t,u32 c){ int dx=x1-x0, dy=y1-y0; int n=(dx<0?-dx:dx)>(dy<0?-dy:dy)?(dx<0?-dx:dx):(dy<0?-dy:dy); if(n<1)n=1;
    for(int k=0;k<=n;k++)lg_disc(x0+dx*k/n,y0+dy*k/n,t,c,256); }
static void lg_face_icon(int cx,int cy,int s,u32 c){
    int h=s/2, l=s*2/7, t=s>=48?2:1;
    lg_line(cx-h,cy-h+l,cx-h,cy-h,t,c); lg_line(cx-h,cy-h,cx-h+l,cy-h,t,c);
    lg_line(cx+h-l,cy-h,cx+h,cy-h,t,c); lg_line(cx+h,cy-h,cx+h,cy-h+l,t,c);
    lg_line(cx-h,cy+h-l,cx-h,cy+h,t,c); lg_line(cx-h,cy+h,cx-h+l,cy+h,t,c);
    lg_line(cx+h-l,cy+h,cx+h,cy+h,t,c); lg_line(cx+h,cy+h-l,cx+h,cy+h,t,c);
    int ey=cy-s/6, ex=s/5; lg_line(cx-ex,ey,cx-ex,ey+s/12,t,c); lg_line(cx+ex,ey,cx+ex,ey+s/12,t,c);
    lg_line(cx,ey,cx,ey+s/6,t,c); lg_line(cx,ey+s/6,cx-s/16,ey+s/6,t,c);
    for(int k=-4;k<=4;k++){ int x=cx+k*s/22, y=cy+s/5+(16-k*k)*s/200; int x2=cx+(k+1)*s/22, y2=cy+s/5+(16-(k+1)*(k+1))*s/200; if(k<4)lg_line(x,y,x2,y2,t,c); } }
static void lg_wifi(int cx,int cy,u32 c){ for(int r=4;r<=12;r+=4){ for(int a=-r;a<=r;a++){ int b2=r*r-a*a; int yy=cy-(int)isqrt32((u32)b2); if(yy<=cy-r*7/10)lg_disc(cx+a,yy,1,c,200); } } lg_disc(cx,cy+1,1,c,256); }
static void lg_battery(int x,int y,int pct,u32 c){ lg_rr(x,y,20,11,2,c,256); lg_rr(x+1,y+1,18,9,2,rgb(223,230,238),256); int w=pct*14/100; if(w<1)w=1; lg_rr(x+2,y+2,w,7,1,c,256); fill_rect(x+20,y+3,2,5,c); }
static void lg_power(int cx,int cy,u32 c){ for(int k=0;k<32;k++){ static const int SX[32]={0,195,383,556,707,831,924,981,1000,981,924,831,707,556,383,195,0,-195,-383,-556,-707,-831,-924,-981,-1000,-981,-924,-831,-707,-556,-383,-195};
        int ci=(k+8)&31; if(k>=29||k<=3)continue; int x=cx+SX[k]*8/1000, y=cy-SX[ci]*8/1000; lg_disc(x,y,1,c,220); } lg_line(cx,cy-10,cx,cy-2,1,c); }
static int lg_fid_avail(void){ return fid_has(lg_sel); }
static void lg_success(void){
    lg_fail=0; lg_msg[0]=0; g_fid_fails=0;
    if(!g_lg_installed&&g_lg_liveret){ lg_live_unlock(); return; }
    if(!g_lg_installed){ for(int i=0;i<32;i++)login_pass[i]=0; login_pl=0; g_locked=0; g_state=ST_DESKTOP; acct_ui_unlocked(); if(g_rec_openterm){open_win(W_TERM);g_rec_openterm=0;} return; }
    if(g_logged_in&&seq(lg_sel,g_user)){ acct_log("screen unlocked for ",g_user,0); g_state=ST_DESKTOP; acct_ui_unlocked(); return; }
    if(g_logged_in)acct_ui_end_session();
    if(acct_login(lg_sel)<0){ scopy(lg_msg,"Could not start the session for this account"); return; }
    su_save_file();
    acct_ui_session_started();
    g_state=ST_DESKTOP; acct_ui_unlocked();
    if(g_rec_openterm){open_win(W_TERM);g_rec_openterm=0;} }
void fid_scan_draw(int cx){
    u32* sc=SCENE; for(u32 y=0;y<H;y++){ u32* r=sc+(u64)y*SW; for(u32 x=0;x<W;x++){ u32 c=r[x]; r[x]=((c>>2)&0x3F3F3Fu)+((c>>3)&0x1F1F1Fu)+0x0A0E16u; } }
    int cy=(int)H*120/800+150, R=130;
    if(g_fid_state==FID_SUCCESS){ int ccy=(int)H*220/800+75;
        for(int j=-82;j<=82;j++)for(int i=-82;i<=82;i++){ int d2=i*i+j*j; if(d2>82*82)continue; int d=(int)isqrt32((u32)d2); if(d>=72&&d<=78){ int px=cx+i,py=ccy+j; if((u32)px<W&&(u32)py<H){ u32*p=&sc[(u64)py*SW+px]; *p=mix(*p,rgb(58,208,122),230); } } }
        lg_line(cx-30,ccy+2,cx-9,ccy+23,3,rgb(58,208,122)); lg_line(cx-9,ccy+23,cx+32,ccy-18,3,rgb(58,208,122));
        if(g_state!=ST_LOGIN){ uf_draw_ctr(cx,ccy+105,UF_26B,"Face ID is set up",rgb(255,255,255)); uf_draw_ctr(cx,ccy+148,UF_15,"You can now sign in by looking at the camera",rgb(214,222,234)); return; }
        char nm[64], b[96], *p=b; lg_display(nm); p=scopy(p,"Welcome back, "); p=scopy(p,nm); *p=0;
        uf_draw_ctr(cx,ccy+105,UF_26B,b,rgb(255,255,255)); uf_draw_ctr(cx,ccy+148,UF_15,"Recognized with Face ID",rgb(214,222,234)); return; }
    int pw=0,ph=0; const u32* fr=fid_preview(&pw,&ph);
    for(int j=-R;j<=R;j++){ int py=cy+j; if((u32)py>=H)continue; u32* row=sc+(u64)py*SW;
        for(int i=-R;i<=R;i++){ int d2=i*i+j*j; if(d2>R*R)continue; int px=cx+i; if((u32)px>=W)continue; u32 c;
            if(fr&&pw>0&&ph>0){ int m=pw<ph?pw:ph; int sx=pw/2-(i*m*9/10)/(2*R), sy=ph/2+(j*m*9/10)/(2*R); if(sx<0)sx=0; if(sy<0)sy=0; if(sx>=pw)sx=pw-1; if(sy>=ph)sy=ph-1; c=fr[(u64)sy*pw+sx]; }
            else c=mix(rgb(93,107,128),rgb(43,52,67),(int)isqrt32((u32)d2)*256/R);
            int e=R*4-(int)isqrt32((u32)d2*16); row[px]=e>=4?c:mix(row[px],c,e*64); } }
    int frac=-1, spin=(int)((g_ticks*6)%360); if(g_fid_mode==FID_ENROLL)frac=g_fid_count*360/FID_TARGET;
    for(int j=-147;j<=147;j++){ int py=cy+j; if((u32)py>=H)continue; u32* row=sc+(u64)py*SW;
        for(int i=-147;i<=147;i++){ int d2=i*i+j*j; if(d2<135*135||d2>147*147)continue; int d4=(int)isqrt32((u32)d2*16); int e=12-(d4-141*4<0?141*4-d4:d4-141*4); if(e<=0)continue; int px=cx+i; if((u32)px>=W)continue;
            int ax=i<0?-i:i, ay=j<0?-j:j, a=ax>=ay?(ax?ay*45/ax:0):90-(ay?ax*45/ay:0); int th; if(i>=0&&j<0)th=90-a; else if(i>=0)th=90+a; else if(j>=0)th=270-a; else th=270+a;
            int on; if(frac>=0)on=th<=frac; else { int dd=(th-spin+360)%360; on=dd<120; }
            u32 c=on?rgb(76,155,255):rgb(255,255,255); int al=on?256:(((th/4)&1)?0:60); al=al*(e>12?12:e)/12; if(al)row[px]=mix(row[px],c,al); } }
    const char* t1=g_fid_state==FID_STARTING?"Starting the camera...":g_fid_mode==FID_ENROLL?"Setting up Face ID":"Looking for you...";
    uf_draw_ctr(cx,cy+R+40,UF_26B,t1,rgb(255,255,255));
    char sub[80],*p=sub; if(g_fid_mode==FID_ENROLL){ p=scopy(p,g_fid_face[2]?"Hold still - ":"Look at the camera - "); p=udec(p,(u32)g_fid_count); p=scopy(p," of "); p=udec(p,FID_TARGET); } else p=scopy(p,g_fid_face[2]?"Hold still":"Look at the camera"); if(g_fid_hint){ p=sub; p=scopy(p,g_fid_hint); } *p=0;
    uf_draw_ctr(cx,cy+R+84,UF_15,sub,rgb(214,222,234));
    { int pwm=g_fid_mode!=FID_ENROLL; const char* bt=pwm?"Use password instead":"Cancel";
      lg_cancw=uf_width(UF_15,bt)+(pwm?56:48); lg_cancx=cx-lg_cancw/2; lg_cancy=cy+R+122;
      int hv=in_rect(mx,my,lg_cancx,lg_cancy,lg_cancw,42); lg_rr(lg_cancx,lg_cancy,lg_cancw,42,12,rgb(255,255,255),hv?70:36);
      if(pwm){ int kx=lg_cancx+22, ky=lg_cancy+21; for(int a=0;a<360;a+=20){ static const int CS[18]={1000,940,766,500,174,-174,-500,-766,-940,-1000,-940,-766,-500,-174,174,500,766,940}; int ci=a/20, si=(ci+13)%18; lg_disc(kx-4+CS[ci]*4/1000,ky+4+CS[si]*4/1000,0,rgb(255,255,255),220); }
        lg_line(kx-1,ky+1,kx+7,ky-7,1,rgb(255,255,255)); lg_line(kx+4,ky-4,kx+7,ky-1,1,rgb(255,255,255)); }
      uf_draw(lg_cancx+(pwm?40:24),lg_cancy+21-uf_cap_mid(UF_15),UF_15,bt,rgb(255,255,255)); }
    lg_disc(32,(int)H-30,4,rgb(58,208,122),256);
    uf_draw(44,(int)H-30-uf_cap_mid(UF_13),UF_13,g_fid_mode==FID_ENROLL?"Camera on - only face numbers are saved, never pictures":"Camera on - nothing is saved or sent",rgb(219,227,238)); }
void login_present(void){
    lg_refresh();
    lg_hills();
    int cx=(int)W/2;
    if(lg_scan&&(g_fid_mode||g_fid_state==FID_SUCCESS)){ fid_scan_draw(cx); draw_cursor(); blit_all(); return; }
    int hh,mm,ss,yy,mo,dd; read_clock(&hh,&mm,&ss,&yy,&mo,&dd); (void)ss;
    { static const char* WD[7]={"Sunday","Monday","Tuesday","Wednesday","Thursday","Friday","Saturday"};
      int w=(yy+(mo<3?yy-1:yy)/4-(mo<3?yy-1:yy)/100+(mo<3?yy-1:yy)/400+(int[]){0,3,2,5,0,3,5,1,4,6,2,4}[(mo>=1&&mo<=12?mo:1)-1]+dd)%7;
      char tb[16],*p=tb; p=u2(p,hh); *p++=':'; p=u2(p,mm); *p=0;
      int ty=(int)H*70/800; uf_draw_ctr(cx,ty,UF_84L,tb,rgb(31,42,58));
      char db[48]; p=db; p=scopy(p,WD[w<0?0:w]); p=scopy(p,", "); p=udec(p,(u32)dd); *p++=' '; if(mo>=1&&mo<=12)p=scopy(p,MON[mo-1]); *p=0;
      uf_draw_ctr(cx,ty+112,UF_18,db,rgb(58,69,88));
      { if(g_logged_in&&g_focus_cur>=0){ focus_mode* f=focus_get(g_focus_cur);
          if(f&&f->show_lock){ char fb[48],*fq=fb; fq=scopy(fq,f->name); fq=scopy(fq," is on"); *fq=0; int fw=uf_width(UF_15,fb)+34, fx=cx-fw/2, fy=ty+146; lg_rr(fx-10,fy-6,fw+20,30,15,rgb(255,255,255),170); lg_disc(fx+9,fy+9,10,f->color,256); focus_glyph(fx+9,fy+9,f->icon,rgb(255,255,255)); uf_draw(fx+28,fy+9-uf_cap_mid(UF_15),UF_15,fb,rgb(31,42,58)); } } } }
    int cy=(int)H*352/800, R=52; lg_avx=cx; lg_avy=cy; lg_avr=R;
    lg_disc(cx,cy+6,R+6,rgb(30,60,120),40); lg_disc(cx,cy,R,rgb(255,255,255),256);
    { u32* sc=SCENE; int r2=R-4; for(int j=-r2-1;j<=r2+1;j++)for(int i=-r2-1;i<=r2+1;i++){ int d2=i*i+j*j; int e=r2*4-(int)isqrt32((u32)d2*16); if(e<=-4)continue; int px=cx+i,py=cy+j; if((u32)px>=W||(u32)py>=H)continue;
        u32 c=mix(mix(ACC,rgb(255,255,255),70),mix(ACC,rgb(0,0,0),50),((i+j)+2*r2)*256/(4*r2)); u32* p=&sc[(u64)py*SW+px]; *p=e>=4?c:mix(*p,c,(e+4)*32); } }
    char dname[64]; lg_display(dname);
    { char ini=dname[0]; if(ini>='a'&&ini<='z')ini-=32; char st[2]={ini?ini:'U',0}; uf_draw_ctr(cx,cy-uf_cap_mid(UF_40B),UF_40B,st,rgb(255,255,255)); }
    { char nm[64]; int k=0; for(;dname[k]&&k<40;k++)nm[k]=(k==0&&dname[k]>='a'&&dname[k]<='z')?dname[k]-32:dname[k]; nm[k]=0; uf_draw_ctr(cx,cy+R+20,UF_24B,nm,rgb(31,36,48)); }
    lg_pw=320; int ph=46; lg_px=cx-lg_pw/2; lg_py=cy+R+64;
    lg_shadow(lg_px,lg_py,lg_pw,ph,12); lg_rr(lg_px,lg_py,lg_pw,ph,12,rgb(207,216,227),256); lg_rr(lg_px+1,lg_py+1,lg_pw-2,ph-2,11,rgb(255,255,255),256);
    lg_bw=46; lg_bh=ph; lg_bx=lg_px+lg_pw-46; lg_by=lg_py;
    { int hov=in_rect(mx,my,lg_bx,lg_by,lg_bw,lg_bh); u32 bc=hov?mix(ACC,rgb(0,0,0),30):ACC; u32* sc=SCENE;
      for(int j=0;j<ph;j++)for(int i=0;i<46;i++){ if(cut(i+lg_pw-46,j,lg_pw,ph,12,15))continue; int px=lg_bx+i,py=lg_by+j; if((u32)px<W&&(u32)py<H)sc[(u64)py*SW+px]=bc; }
      int ax=lg_bx+23, ay=lg_by+23; lg_line(ax-7,ay,ax+7,ay,1,rgb(255,255,255)); lg_line(ax+2,ay-5,ax+7,ay,1,rgb(255,255,255)); lg_line(ax+2,ay+5,ax+7,ay,1,rgb(255,255,255)); }
    int tmid=lg_py+ph/2-uf_cap_mid(UF_15);
    if(login_pl==0)uf_draw(lg_px+16,tmid,UF_15,g_lg_installed&&!lg_sel_haspw()?"No password - press Enter":"Password",rgb(140,150,166));
    else { int c2=lg_px+22; for(int k=0;k<login_pl&&k<18;k++){ lg_disc(c2,lg_py+ph/2,4,rgb(40,50,70),256); c2+=14; }
        if(((g_ticks/40)&1)){ int cxp=lg_px+22+(login_pl<18?login_pl:18)*14-6; fill_rect(cxp,lg_py+13,2,ph-26,ACC); } }
    int y=lg_py+ph;
    lg_fw=0;
    if(lg_fid_avail()){
        y+=16; { u32* sc=SCENE; for(int x=lg_px;x<lg_px+lg_pw;x++){ if(x>cx-18&&x<cx+18)continue; u32*p=&sc[(u64)(y+8)*SW+x]; *p=mix(*p,rgb(150,165,184),200); } } uf_draw_ctr(cx,y+8-uf_cap_mid(UF_13)-2,UF_13,"or",rgb(90,101,119));
        y+=26; lg_fx=lg_px; lg_fy=y; lg_fw=lg_pw; lg_fh=48;
        int locked=g_fid_fails>=FID_MAX_FAILS, hov=!locked&&in_rect(mx,my,lg_fx,lg_fy,lg_fw,lg_fh);
        lg_rr(lg_fx,lg_fy,lg_fw,lg_fh,12,rgb(207,216,227),256); lg_rr(lg_fx+1,lg_fy+1,lg_fw-2,lg_fh-2,11,hov?rgb(255,255,255):rgb(239,243,247),256);
        const char* ft=locked?"Face ID locked - use your password":"Sign in with Face ID"; u32 fc=locked?rgb(120,130,146):mix(ACC,rgb(0,0,0),70);
        int tw=uf_width(UF_15M,ft)+(locked?0:34), tx=cx-tw/2;
        if(!locked){ lg_face_icon(tx+11,lg_fy+lg_fh/2,22,fc); tx+=34; }
        uf_draw(tx,lg_fy+lg_fh/2-uf_cap_mid(UF_15M),UF_15M,ft,fc); y=lg_fy+lg_fh; }
    { char hb[120]; const char* hint=0; int warn=0;
      if(lg_wait_until>g_ticks){ char* q=hb; q=scopy(q,"Too many wrong passwords - try again in "); q=udec(q,(u32)((lg_wait_until-g_ticks)/100+1)); q=scopy(q," s"); *q=0; hint=hb; warn=1; }
      else if(lg_msg[0]){ hint=lg_msg; warn=1; }
      else if(!g_lg_installed)hint="Press Return to unlock";
      else if(seq(lg_sel,"guest"))hint="Guest - files are removed at sign out";
      else if(g_logged_in&&!acct_switch_mode()){ char* q=hb; q=scopy(q,"Locked - "); q=scopy(q,lg_sel); *q=0; hint=hb; }
      int ny=y+18;
      if(hint){ uf_draw_ctr(cx,ny,UF_13,hint,warn?rgb(196,48,60):rgb(80,92,110)); ny+=22; }
      { extern int kcaps; if(kcaps){ uf_draw_ctr(cx,ny,UF_13,"Caps Lock is on",rgb(176,112,20)); ny+=22; } }
      if(g_logged_in&&acct_switch_mode()&&!seq(lg_sel,g_user)){ char* q=hb; q=scopy(q,"Signing in closes the open apps of "); q=scopy(q,g_user); *q=0; uf_draw_ctr(cx,ny,UF_13,hb,rgb(80,92,110)); } }
    lg_swx=-1000;
    if(lg_list_shown()){ int ly=(int)H-24-lg_nu*40;
        for(int i=0;i<lg_nu;i++){ int yy2=ly+i*40, x=20; const char* f=lg_users[i].full[0]?lg_users[i].full:lg_users[i].name; int on=seq(lg_users[i].name,lg_sel);
            int w=52+uf_width(UF_15,f); if(w>300)w=300; lg_ux[i]=x; lg_uy[i]=yy2; lg_uw[i]=w;
            if(on||in_rect(mx,my,x,yy2,w,36))lg_rr(x,yy2,w,36,10,rgb(255,255,255),on?140:80);
            lg_disc(x+20,yy2+18,14,on?ACC:rgb(125,140,160),256); char ini[2]={f[0]>='a'&&f[0]<='z'?(char)(f[0]-32):f[0],0}; uf_draw_ctr(x+20,yy2+18-uf_cap_mid(UF_13),UF_13,ini,rgb(255,255,255));
            char nm[34]; int k=0; for(;f[k]&&k<30;k++)nm[k]=f[k]; nm[k]=0; uf_draw(x+44,yy2+18-uf_cap_mid(UF_15),UF_15,nm,rgb(42,49,64)); } }
    else if(g_lg_installed&&g_logged_in&&(lg_nu>1)){ lg_swx=20; lg_swy=(int)H-56; lg_sww=uf_width(UF_15,"Other user")+56; int hv=in_rect(mx,my,lg_swx,lg_swy,lg_sww,36);
        if(hv)lg_rr(lg_swx,lg_swy,lg_sww,36,10,rgb(255,255,255),90);
        lg_disc(lg_swx+20,lg_swy+18,14,rgb(125,140,160),256); lg_line(lg_swx+16,lg_swy+18,lg_swx+24,lg_swy+18,1,rgb(255,255,255)); lg_line(lg_swx+20,lg_swy+14,lg_swx+20,lg_swy+22,1,rgb(255,255,255));
        uf_draw(lg_swx+44,lg_swy+18-uf_cap_mid(UF_15),UF_15,"Other user",rgb(42,49,64)); }
    { extern int bat_present, bat_pct; extern int g_dhcp_ok; u32 tc=rgb(42,49,64); int ry=(int)H-30;
      lg_pmx=(int)W-46; lg_pmy=ry-16; lg_power((int)W-30,ry,in_rect(mx,my,lg_pmx,lg_pmy,32,32)||lg_pmenu?rgb(0,0,0):tc);
      int x=(int)W-64;
      if(bat_present){ char pb[8],*p=pb; p=udec(p,(u32)bat_pct); *p++='%'; *p=0; int tw=uf_width(UF_15,pb); x-=tw; uf_draw(x,ry-uf_cap_mid(UF_15),UF_15,pb,tc); x-=28; lg_battery(x,ry-5,bat_pct,tc); x-=20; }
      lg_wifi(x-10,ry+4,g_dhcp_ok?tc:rgb(150,160,176)); }
    if(lg_pmenu){ static const char* PM[3]={"Sleep","Restart","Shut down"}; int w=170, h=3*38+12, x=(int)W-w-16, yb=(int)H-56-h; lg_pmw=w;
        lg_shadow(x,yb,w,h,12); lg_rr(x,yb,w,h,12,rgb(255,255,255),256);
        for(int k=0;k<3;k++){ int iy=yb+6+k*38; lg_pmi[k]=iy; if(in_rect(mx,my,x+6,iy,w-12,36))lg_rr(x+6,iy,w-12,36,8,rgb(232,238,248),256); uf_draw(x+20,iy+18-uf_cap_mid(UF_15),UF_15,PM[k],rgb(31,36,48)); } }
    draw_cursor(); blit_all();
}
int fid_scan_cancel_hit(int x,int y){ return in_rect(x,y,lg_cancx,lg_cancy,lg_cancw,42); }
static void lg_try(void){
    if(!g_lg_installed&&g_lg_liveret){ lg_live_unlock(); return; }
    if(!g_lg_installed){ for(int i=0;i<32;i++)login_pass[i]=0; login_pl=0; g_locked=0; g_state=ST_DESKTOP; acct_ui_unlocked(); if(g_rec_openterm){open_win(W_TERM);g_rec_openterm=0;} return; }
    if(lg_wait_until>g_ticks)return;
    int ok=acct_check(lg_sel,login_pass);
    for(int i=0;i<32;i++)login_pass[i]=0; login_pl=0;
    if(!ok){ lg_fail++; acct_log("FAILED password for ",lg_sel,0); scopy(lg_msg,"Wrong password - try again");
        if(lg_fail>=5){ lg_wait_until=g_ticks+3000; lg_fail=0; lg_msg[0]=0; acct_log("5 wrong passwords, waiting 30 s, for ",lg_sel,0); } return; }
    lg_success();
}
static void lg_pick(int i){ if(i<0||i>=lg_nu)return; int q=0; for(;lg_users[i].name[q]&&q<32;q++)lg_sel[q]=lg_users[i].name[q]; lg_sel[q]=0; for(int k=0;k<32;k++)login_pass[k]=0; login_pl=0; lg_msg[0]=0; }
static void lg_scan_stop(void){ fid_end(); lg_scan=0; }
static void lg_scan_start(void){
    lg_msg[0]=0;
    if(g_fid_fails>=FID_MAX_FAILS){ scopy(lg_msg,"Face ID is locked after 5 tries - use your password"); return; }
    if(fid_begin(FID_VERIFY,lg_sel)<0){ int k=0; for(;g_fid_msg[k]&&k<90;k++)lg_msg[k]=g_fid_msg[k]; lg_msg[k]=0; return; }
    lg_scan=1; lg_okt=0; }
static void lg_scan_poll(void){
    if(!lg_scan)return;
    if(!g_fid_mode&&g_fid_state!=FID_SUCCESS){ lg_scan=0; return; }
    if(g_fid_state==FID_SUCCESS){ if(!lg_okt)lg_okt=g_ticks; if(g_ticks-lg_okt>90){ lg_scan_stop(); acct_log("Face ID sign-in for ",lg_sel,0); lg_success(); } return; }
    if(g_fid_state==FID_FAILED||g_fid_state==FID_ERROR){ char* q=lg_msg; q=scopy(q,g_fid_msg); if(g_fid_state==FID_FAILED&&g_fid_fails<FID_MAX_FAILS)q=scopy(q," - try again or type your password"); *q=0;
        if(g_fid_fails>=FID_MAX_FAILS){ scopy(lg_msg,"Face ID is locked after 5 tries - use your password"); acct_log("5 Face ID tries failed for ",lg_sel,0); }
        if(g_fid_state==FID_FAILED)acct_log("FAILED Face ID for ",lg_sel,0);
        lg_scan_stop(); } }
int login_input(void){
    int changed=0;
    lg_scan_poll();
    if(lg_scan){ static u64 lr; if(g_ticks-lr>=(g_fid_state==FID_SUCCESS?10:25)){ lr=g_ticks; changed=1; } }
    if(mouse_evt){ int dx,dy,btn; cpu_irq_off(); dx=mouse_dx;dy=mouse_dy;mouse_dx=0;mouse_dy=0;btn=mouse_btn;mouse_evt=0; cpu_irq_on();
        if(dx||dy){ mx+=dx;my-=dy; if(mx<0)mx=0;if(my<0)my=0;if(mx>=(int)W)mx=W-1;if(my>=(int)H)my=H-1; changed=1; }
        if(btn&&!mouse_btn_prev){
            if(lg_scan){ if(in_rect(mx,my,lg_cancx,lg_cancy,lg_cancw,42)&&g_fid_state!=FID_SUCCESS)lg_scan_stop(); }
            else if(lg_pmenu){ int x=(int)W-lg_pmw-16; int hit=-1; for(int k=0;k<3;k++) if(in_rect(mx,my,x+6,lg_pmi[k],lg_pmw-12,36))hit=k; lg_pmenu=0;
                if(hit==0)power_sleep(); else if(hit==1)power_reboot(); else if(hit==2)power_off(); }
            else if(in_rect(mx,my,lg_pmx,lg_pmy,32,32)){ lg_pmenu=1; }
            else if(in_rect(mx,my,lg_bx,lg_by,lg_bw,lg_bh)){ lg_try(); }
            else if(lg_fw>0&&in_rect(mx,my,lg_fx,lg_fy,lg_fw,lg_fh)){ lg_scan_start(); }
            else if(lg_swx>-1000&&in_rect(mx,my,lg_swx,lg_swy,lg_sww,36)){ acct_set_switch_mode(1); for(int k=0;k<32;k++)login_pass[k]=0; login_pl=0; }
            else if(lg_list_shown()){ for(int i=0;i<lg_nu;i++) if(in_rect(mx,my,lg_ux[i],lg_uy[i],lg_uw[i],36))lg_pick(i); }
            changed=1; }
        mouse_btn_prev=btn; }
    int c; while((c=kpop())>=0){ changed=1;
        if(lg_scan){ if(g_fid_state==FID_SUCCESS)continue; lg_scan_stop(); if(c==27||c=='\n')continue; }
        if(lg_pmenu){ lg_pmenu=0; if(c==27)continue; }
        if(c=='\n'){ lg_try(); }
        else if(c==27){ if(g_logged_in&&acct_switch_mode()){ acct_set_switch_mode(0); for(int k=0;k<32;k++)login_pass[k]=0; login_pl=0; lg_msg[0]=0; } }
        else if((c==19||c==20)&&lg_list_shown()){ int cur=0; for(int i=0;i<lg_nu;i++) if(seq(lg_users[i].name,lg_sel))cur=i; cur=(cur+(c==20?1:lg_nu-1))%lg_nu; lg_pick(cur); }
        else if(c=='\b'){ if(login_pl>0)login_pl--; }
        else if(c>=32&&c<127){ if(login_pl<31)login_pass[login_pl++]=(char)c; lg_msg[0]=0; }
        login_pass[login_pl]=0; }
    static int lb=-1; int bp=(int)((g_ticks/50)&1); if(bp!=lb){lb=bp;changed=1;}
    { static u64 lm=0; if(g_ticks/1000!=lm){ lm=g_ticks/1000; changed=1; } }
    if(lg_wait_until>g_ticks){ static u64 lw=0; if(g_ticks/100!=lw){ lw=g_ticks/100; changed=1; } }
    return changed;
}

void setup_begin(void){ ux_core_start(0); g_su_page=0; g_su_form=0; su_err[0]=0; su_accounts_done=0; g_state=ST_SETUP; setup_present(); }

const char* kbd_repeat_name(int r){ static const char* N[3]={"Slow","Normal","Fast"}; return (r>=0&&r<3)?N[r]:N[1]; }
void power_prefs_save(void){ su_save_file(); extern int reg_save(void); reg_save(); }
void kbd_set_repeat(int r){ if(r<0||r>2)r=1; g_krep_speed=r; kbd_apply_repeat(); su_save_file(); }
const char* kbd_layout_name(int l){ return (l>=0&&l<6)?SU_KBDS[l]:SU_KBDS[0]; }
void kbd_set_layout(int l){
    extern int g_kbd_layout; if(l<0||l>=6)l=0; g_kbd_layout=l; g_su_kbd=l;
    su_save_file();
}
