#include "include/nvx/base.h"
#include "include/nvx/core/auth/faceid.h"
#include "include/nvx/arch/x86_64/cpu/percpu.h"
#include "include/nvx/lib/base/fmt.h"
#include "include/nvx/drivers/video/fb/framebuffer.h"
#include "include/nvx/graphics/theme/theme.h"
#include "include/nvx/graphics/ui/wm.h"
#include "include/nvx/net/http/http.h"
#include "include/nvx/core/init/installer.h"
#include "include/nvx/core/proc/jobs.h"
#include "include/nvx/core/auth/accounts.h"
#include "include/nvx/core/auth/acctui.h"

extern volatile u64 g_ticks;
extern u64 g_last_input_t;
extern char login_pass[]; extern int login_pl;
void toast(const char* m);
void power_prefs_save(void);

static int au_ul(const char* s){ int n=0; while(s[n])n++; return n; }
static void au_ucp(char* d,const char* s,int cap){ int i=0; for(;s[i]&&i<cap-1;i++)d[i]=s[i]; d[i]=0; }
static int au_ueq(const char* a,const char* b){ while(*a&&*a==*b){ a++; b++; } return *a==*b; }

void acct_field(int x,int y,int w,const char* label,const char* val,int len,int mask,int focus){
    if(label)dstr(x,y,label,rgb(20,24,34),1);
    int fy=y+18;
    dstr(x,fy,"[",rgb(20,24,34),1);
    dstr(x+w-8,fy,"]",rgb(20,24,34),1);
    fill_rect(x+8,fy+1,w-16,14,focus?rgb(0,0,0):rgb(60,64,74));
    int cx=x+12, maxc=(w-32)/8, st=len>maxc?len-maxc:0;
    for(int k=st;k<len;k++){ dchar(cx,fy,mask?'*':val[k],rgb(255,255,255),1); cx+=8; }
    if(focus&&((g_ticks/50)&1))fill_rect(cx+1,fy+2,2,12,rgb(255,255,255));
}
void acct_button(int cx,int by,const char* t,int foc,int* rx,int* ry,int* rw,int* rh){
    int w=au_ul(t)*8+32, x=cx-w/2; int hov=in_rect(mx,my,x,by,w,20);
    if(foc)fill_rect(x,by,w,20,hov?rgb(230,80,90):rgb(210,60,70));
    u32 c=foc?rgb(255,255,255):(hov?rgb(255,120,130):rgb(210,60,70));
    dstr(x+4,by+3,"<",c,1); dstr(x+16,by+3,t,c,1); dstr(x+16+au_ul(t)*8+4,by+3,">",c,1);
    if(rx){ *rx=x; *ry=by; *rw=w; *rh=20; }
}
void acct_check_row(int x,int y,const char* label,int on,int focus){
    u32 c=rgb(20,24,34);
    dstr(x,y,on?"[x] ":"[ ] ",focus?rgb(210,60,70):c,1);
    dstr(x+32,y,label,focus?rgb(210,60,70):c,1);
}

enum { au_D_NONE, au_D_PW_SELF, au_D_NAME, au_D_ADD, au_D_DEL, au_D_PW_OTHER, au_D_FACE, au_D_FACESCAN };
static struct { int on, kind, nf, foc, nbtn, chk, chkon; char title[48]; char text[2][120]; char lab[5][48]; char val[5][72]; int len[5], mask[5]; char err[120]; char target[33]; const char* btn[3]; int bx[3],by[3],bw[3],bh[3]; int fy[5],fx,fw,ckx,cky; } au_D;

int acct_dlg_active(void){ return au_D.on; }
static void au_d_reset(int kind,const char* title){
    for(int i=0;i<(int)sizeof(au_D);i++)((u8*)&au_D)[i]=0;
    au_D.on=1; au_D.kind=kind; au_ucp(au_D.title,title,48); au_D.nbtn=2; au_D.btn[0]="OK"; au_D.btn[1]="Cancel";
}
static void au_d_field(const char* lab,const char* init,int mask){ int i=au_D.nf++; au_ucp(au_D.lab[i],lab,48); au_D.mask[i]=mask; if(init){ au_ucp(au_D.val[i],init,72); au_D.len[i]=au_ul(au_D.val[i]); } }
static void au_d_wipe(void){ for(int i=0;i<5;i++){ for(int k=0;k<72;k++)au_D.val[i][k]=0; au_D.len[i]=0; } }
static void au_d_close(void){ au_d_wipe(); au_D.on=0; au_D.kind=au_D_NONE; }
static int au_d_nfocus(void){ return au_D.nf+(au_D.chk?1:0)+au_D.nbtn; }

void acct_dlg_open_pw_self(void){
    au_d_reset(au_D_PW_SELF,"Change Password");
    au_ucp(au_D.text[0],"Choose a new password for your account.",120);
    if(acct_has_password(g_user))au_d_field("Current password:",0,1);
    au_d_field("New password:",0,1); au_d_field("Type the new password again:",0,1);
}
void acct_dlg_open_name(void){ au_d_reset(au_D_NAME,"Change Name"); au_ucp(au_D.text[0],"This name is shown on the sign-in screen.",120); au_d_field("Full name:",g_fullname,0); }
void acct_dlg_open_add(void){
    au_d_reset(au_D_ADD,"Create User Account");
    au_ucp(au_D.text[0],"The new person signs in with this user name and password.",120);
    au_d_field("Full name:",0,0); au_d_field("User name (a-z, 0-9, - and _):",0,0); au_d_field("Password:",0,1); au_d_field("Type the password again:",0,1);
    au_D.chk=1; au_D.chkon=0;
}
void acct_dlg_open_del(const char* name){
    au_d_reset(au_D_DEL,"Remove User Account"); au_ucp(au_D.target,name,33);
    char* p=au_D.text[0]; p=scopy(p,"Remove the account '"); p=scopy(p,name); p=scopy(p,"'?"); *p=0;
    au_ucp(au_D.text[1],"Delete files also removes the home folder and everything in it.",120);
    au_D.nbtn=3; au_D.btn[0]="Delete files"; au_D.btn[1]="Keep files"; au_D.btn[2]="Cancel"; au_D.foc=2;
}
void acct_dlg_open_pw_other(const char* name){
    au_d_reset(au_D_PW_OTHER,"Set Password"); au_ucp(au_D.target,name,33);
    char* p=au_D.text[0]; p=scopy(p,"New password for '"); p=scopy(p,name); p=scopy(p,"':"); *p=0;
    au_d_field("New password:",0,1); au_d_field("Type the new password again:",0,1);
}

void acct_dlg_open_face(void){
    au_d_reset(au_D_FACE,fid_has(g_user)?"Scan Face ID Again":"Set Up Face ID");
    au_ucp(au_D.text[0],"Sign in by looking at the camera. Your password still works.",120);
    au_ucp(au_D.text[1],acct_has_password(g_user)?"Type your password to continue.":"Press OK to continue.",120);
    if(acct_has_password(g_user))au_d_field("Password:",0,1);
    au_D.btn[0]="Continue";
}
static u64 au_fok;
static void au_face_start(void){
    au_d_wipe();
    if(fid_begin(FID_ENROLL,g_user)<0){ au_ucp(au_D.err,g_fid_msg,120); return; }
    au_D.kind=au_D_FACESCAN; au_D.nf=0; au_D.nbtn=0; au_fok=0;
}
static void au_face_stop(void){ fid_end(); au_d_close(); }
static int au_d_same(int a,int b){ return au_ueq(au_D.val[a],au_D.val[b]); }
static void au_d_ok(int btn){
    au_D.err[0]=0;
    if(au_D.kind==au_D_DEL){
        if(btn==2){ au_d_close(); return; }
        if(acct_delete(au_D.target,btn==0)<0){ au_ucp(au_D.err,g_acct_err,120); return; }
        toast(btn==0?"Account and home folder removed":"Account removed, files kept"); au_d_close(); return; }
    if(btn==1){ au_d_close(); return; }
    if(au_D.kind==au_D_FACE){
        if(au_D.nf&&!acct_check(g_user,au_D.val[0])){ acct_log("FAILED password for Face ID set up, ",g_user,0); au_ucp(au_D.err,"The password is not correct.",120); return; }
        au_face_start(); return; }
    if(au_D.kind==au_D_PW_SELF){
        int o=au_D.nf==3?1:0;
        if(o&&!acct_check(g_user,au_D.val[0])){ au_ucp(au_D.err,"The current password is not correct.",120); return; }
        if(au_D.len[o]<4){ au_ucp(au_D.err,"The new password needs at least 4 characters.",120); return; }
        if(!au_d_same(o,o+1)){ au_ucp(au_D.err,"The two new passwords are not the same.",120); return; }
        if(acct_set_password(g_user,au_D.val[o])<0){ au_ucp(au_D.err,g_acct_err,120); return; }
        toast("Password changed"); au_d_close(); return; }
    if(au_D.kind==au_D_NAME){
        if(acct_set_full(g_user,au_D.val[0])<0){ au_ucp(au_D.err,g_acct_err,120); return; }
        toast("Name changed"); au_d_close(); return; }
    if(au_D.kind==au_D_ADD){
        if(!acct_is_admin(g_uid)){ au_ucp(au_D.err,"Only an administrator can add accounts.",120); return; }
        if(au_D.len[2]<4){ au_ucp(au_D.err,"The password needs at least 4 characters.",120); return; }
        if(!au_d_same(2,3)){ au_ucp(au_D.err,"The two passwords are not the same.",120); return; }
        if(acct_create(au_D.val[1],au_D.val[0],au_D.val[2],au_D.chkon)<0){ au_ucp(au_D.err,g_acct_err,120); return; }
        toast("Account created"); au_d_close(); return; }
    if(au_D.kind==au_D_PW_OTHER){
        if(!acct_is_admin(g_uid)){ au_ucp(au_D.err,"Only an administrator can do this.",120); return; }
        if(au_D.len[0]<4){ au_ucp(au_D.err,"The password needs at least 4 characters.",120); return; }
        if(!au_d_same(0,1)){ au_ucp(au_D.err,"The two passwords are not the same.",120); return; }
        if(acct_set_password(au_D.target,au_D.val[0])<0){ au_ucp(au_D.err,g_acct_err,120); return; }
        toast("Password set"); au_d_close(); return; }
}

void acct_dlg_draw(void){
    if(!au_D.on)return;
    if(au_D.kind==au_D_FACESCAN){
        if(g_fid_state==FID_SUCCESS){ if(!au_fok){ au_fok=g_ticks; acct_log("Face ID set up for ",g_user,0); } if(g_ticks-au_fok>120){ au_face_stop(); toast("Face ID is set up"); return; } }
        else if(g_fid_state==FID_FAILED||g_fid_state==FID_ERROR||!g_fid_mode){ char m[96]; au_ucp(m,g_fid_msg[0]?g_fid_msg:"Face ID set up stopped",96); au_face_stop(); toast(m); return; }
        fid_scan_draw((int)W/2); return; }
    int bw=560, rows=(au_D.text[0][0]?1:0)+(au_D.text[1][0]?1:0);
    int bh=40+rows*18+au_D.nf*44+(au_D.chk?26:0)+(au_D.err[0]?22:0)+48;
    int ox,oy,ow; ip_box(&ox,&oy,&ow,bw,bh,au_D.title);
    int y=oy;
    for(int t=0;t<2;t++) if(au_D.text[t][0]){ dstr(ox,y,au_D.text[t],rgb(20,24,34),1); y+=18; }
    if(rows)y+=6;
    au_D.fx=ox; au_D.fw=ow;
    for(int i=0;i<au_D.nf;i++){ au_D.fy[i]=y; acct_field(ox,y,ow,au_D.lab[i],au_D.val[i],au_D.len[i],au_D.mask[i],au_D.foc==i); y+=44; }
    if(au_D.chk){ au_D.ckx=ox; au_D.cky=y; acct_check_row(ox,y,"Make this account an administrator",au_D.chkon,au_D.foc==au_D.nf); y+=26; }
    if(au_D.err[0]){ dstr(ox,y,au_D.err,rgb(190,40,50),1); y+=22; }
    y+=8; int b0=au_D.nf+(au_D.chk?1:0);
    for(int b=0;b<au_D.nbtn;b++){ int cx=ox+ow*(b+1)/(au_D.nbtn+1); acct_button(cx,y,au_D.btn[b],au_D.foc==b0+b,&au_D.bx[b],&au_D.by[b],&au_D.bw[b],&au_D.bh[b]); }
}
void acct_dlg_key(int c){
    if(!au_D.on)return;
    if(au_D.kind==au_D_FACESCAN){ if(c==27&&g_fid_state!=FID_SUCCESS){ au_face_stop(); toast("Face ID set up cancelled"); } return; }
    int n=au_d_nfocus(), b0=au_D.nf+(au_D.chk?1:0);
    if(c==27){ au_d_close(); return; }
    if(c=='\t'||c==20){ au_D.foc=(au_D.foc+1)%n; return; }
    if(c==19){ au_D.foc=(au_D.foc+n-1)%n; return; }
    if(c=='\n'||c=='\r'){
        if(au_D.foc>=b0){ au_d_ok(au_D.foc-b0); return; }
        if(au_D.chk&&au_D.foc==au_D.nf){ au_D.chkon^=1; return; }
        if(au_D.foc<au_D.nf-1){ au_D.foc++; return; }
        au_d_ok(0); return; }
    if(au_D.chk&&au_D.foc==au_D.nf&&c==' '){ au_D.chkon^=1; return; }
    if(au_D.foc<au_D.nf){ int i=au_D.foc;
        if(c=='\b'){ if(au_D.len[i]>0)au_D.val[i][--au_D.len[i]]=0; }
        else if(c>=32&&c<127&&au_D.len[i]<64){ au_D.val[i][au_D.len[i]++]=(char)c; au_D.val[i][au_D.len[i]]=0; }
        au_D.err[0]=0; }
}
void acct_dlg_click(int x,int y){
    if(!au_D.on)return;
    if(au_D.kind==au_D_FACESCAN){ if(fid_scan_cancel_hit(x,y)&&g_fid_state!=FID_SUCCESS){ au_face_stop(); toast("Face ID set up cancelled"); } return; }
    for(int i=0;i<au_D.nf;i++) if(in_rect(x,y,au_D.fx,au_D.fy[i],au_D.fw,36)){ au_D.foc=i; return; }
    if(au_D.chk&&in_rect(x,y,au_D.ckx,au_D.cky-2,au_D.fw,20)){ au_D.foc=au_D.nf; au_D.chkon^=1; return; }
    for(int b=0;b<au_D.nbtn;b++) if(in_rect(x,y,au_D.bx[b],au_D.by[b],au_D.bw[b],au_D.bh[b])){ au_d_ok(b); return; }
}

static int au_g_sw_mode=0;
int acct_switch_mode(void){ return au_g_sw_mode; }
void acct_set_switch_mode(int on){ au_g_sw_mode=on; }

void acct_ui_lock(void){
    for(int i=0;i<32;i++)login_pass[i]=0;
    login_pl=0;
    au_g_sw_mode=0;
    if(g_logged_in){ g_locked=1; acct_log("screen locked for ",g_user,0); }
    g_state=ST_LOGIN;
}
static void au_end_session_windows(void){
    for(int i=0;i<NWIN;i++) if(WN[i].open)close_win(i);
    g_launcher=0;
}
static void au_end_session_jobs(int uid){
    if(uid<=0)return;
    for(int i=0;i<JMAX;i++) if(g_jp[i].state!=S_FREE&&g_jp[i].state!=S_ZOMB&&g_jp[i].uid==uid&&g_jp[i].pid>1)jc_signal_proc(i,9);
}
void acct_ui_end_session(void){
    if(!g_logged_in)return;
    int uid=g_uid;
    au_end_session_windows();
    au_end_session_jobs(uid);
    { extern void browser_profile_switch(void); extern void term_session_reset(void); extern void fx_session_reset(void); browser_profile_switch(); term_session_reset(); fx_session_reset(); }
    acct_logout();
    { extern void sh_sudo_reset(void); sh_sudo_reset(); }
    g_uid=0; g_gid=0;
}
void acct_ui_signout(void){
    acct_ui_end_session();
    login_pass[0]=0; login_pl=0; au_g_sw_mode=1; g_state=ST_LOGIN;
}
void acct_ui_session_started(void){ extern void term_session_reset(void); extern void term_prompt(void); extern void reg_user_changed(void); term_session_reset(); term_prompt(); reg_user_changed(); { extern void focus_signed_in(void) __attribute__((weak)); if(focus_signed_in)focus_signed_in(); } }
void acct_ui_unlocked(void){ g_last_input_t=g_ticks; g_locked=0; au_g_sw_mode=0; }

void acct_idle_tick(void){
    extern int g_lock_override; int lm=g_lock_override>0?g_lock_override:g_lock_min;
    if(g_state!=ST_DESKTOP||!g_logged_in||lm<=0)return;
    if(WN[W_VIDEO].open&&!WN[W_VIDEO].min){ g_last_input_t=g_ticks; return; }
    if(g_ticks-g_last_input_t>(u64)lm*6000u){ acct_ui_lock(); }
}

static acct_info au_g_sl[16]; static int au_g_sln;
int acct_ui_others(acct_info* out,int max){
    acct_info all[16]; int n=acct_list(all,16,0), k=0;
    for(int i=0;i<n&&k<max;i++){ if(all[i].uid<1000||all[i].uid>=60000)continue; if(au_ueq(all[i].name,g_user))continue; out[k++]=all[i]; }
    return k;
}
int acct_ui_click(int id){
    if(id<600||id>=700)return 0;
    int admin=acct_is_admin(g_uid);
    if((id==600||id==601)&&au_ueq(g_user,"guest")){ toast("The guest account cannot be changed"); return 1; }
    if(id==600){ acct_dlg_open_name(); return 1; }
    if(id==601){ acct_dlg_open_pw_self(); return 1; }
    if(id==604){ acct_ui_lock(); return 1; }
    if(id==605){ acct_ui_signout(); return 1; }
    if(id==606){ acct_ui_lock(); au_g_sw_mode=1; return 1; }
    if(id==680){ if(au_ueq(g_user,"guest")){ toast("The guest account cannot use Face ID"); return 1; } if(!fid_camera_present()){ toast("No camera found"); return 1; } acct_dlg_open_face(); return 1; }
    if(id==681){ if(fid_delete(g_user)<0)toast("Face ID could not be removed"); else { acct_log("Face ID removed for ",g_user,0); toast("Face ID removed"); } return 1; }
    if(id>=610&&id<614){ static const int M[4]={5,15,30,0}; g_lock_min=M[id-610]; power_prefs_save(); return 1; }
    if(!admin){ toast("Only an administrator can change other accounts"); return 1; }
    if(id==602){ acct_dlg_open_add(); return 1; }
    if(id==607){ extern int live_lock_on(void); extern int live_lock_set(int); int on=live_lock_on(); if(on<0||live_lock_set(!on)<0)toast("The setting could not be saved on the USB stick"); else toast(on?"The sign-in screen is off for starts from this USB stick":"The sign-in screen is on for starts from this USB stick"); return 1; }
    if(id==603){ g_guest_on=!g_guest_on; power_prefs_save(); acct_log(g_guest_on?"guest account turned on by ":"guest account turned off by ",g_user,0); return 1; }
    au_g_sln=acct_ui_others(au_g_sl,16);
    if(id>=620&&id<636){ int i=id-620; if(i<au_g_sln){ if(acct_set_admin(au_g_sl[i].name,!au_g_sl[i].admin)<0)toast(g_acct_err); } return 1; }
    if(id>=640&&id<656){ int i=id-640; if(i<au_g_sln)acct_dlg_open_pw_other(au_g_sl[i].name); return 1; }
    if(id>=660&&id<676){ int i=id-660; if(i<au_g_sln)acct_dlg_open_del(au_g_sl[i].name); return 1; }
    return 1;
}
