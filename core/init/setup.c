#include "include/nvx/base.h"
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
#include "include/nvx/core/init/bootmenu.h"
#include "include/nvx/core/init/installer.h"
#include "include/nvx/core/init/setup.h"
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
static char setup_name[32]=""; static int setup_nl=0, setup_acc=0, setup_th=2;
static int su_nx,su_ny,su_nw, su_ax2[5],su_ay, su_wx2[3],su_wy;
#define NSU 7
static const char* SU_LANGS[6]={"English (United States)","Svenska (Sverige)","Deutsch (Deutschland)","Francais (France)","Espanol (Espana)","Italiano (Italia)"};
static const char* SU_KBDS[6] ={"US QWERTY","Swedish (Sverige)","German QWERTZ","French AZERTY","Spanish QWERTY","UK QWERTY"};
static const char* SU_TZS[7]  ={"(UTC-08:00) Los Angeles","(UTC-05:00) New York","(UTC+00:00) London","(UTC+01:00) Stockholm","(UTC+03:00) Moscow","(UTC+05:30) Mumbai","(UTC+09:00) Tokyo"};
static const char* SU_STORAGE[2]={"Cloud  -  files sync to your Noovex account","Local  -  files stay on this device only"};
static const char* SU_TITLE[NSU]={"Choose your language","Keyboard layout","Select your timezone","Make it yours","Sign in with Noovex","Where your files live","You are all set"};
static const char* SU_DESC[NSU]={
    "Select the language you would like to use with NoovexOS.",
    "Pick the layout that matches your keyboard.",
    "So NoovexOS shows the correct local time.",
    "Choose a name, an accent colour and a wallpaper.",
    "Connect your Noovex account - or skip and do it later.",
    "Cloud files sync to your account and return after reboot.",
    "Welcome aboard. Press Get Started to enter NoovexOS."};
static const char* SU_ACC[3]={"Sign in with Noovex","Create a Noovex account","Skip for now"};
static int g_su_page=0, g_su_lang=0, g_su_kbd=1, g_su_tz=3, g_su_acc=2, g_su_storage=0;
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
void setup_present(void){
    int P=g_su_page;
    fill_rect(0,0,(int)W,(int)H,rgb(72,130,200));
    int ch;
    if(P<=1) ch=6*22; else if(P==2) ch=7*22; else if(P==3) ch=166;
    else if(P==4) ch=(g_su_form==0)?3*22+24:112; else if(P==5) ch=2*22+24; else ch=88;
    const char* ptitle=(P==4&&g_su_form==2)?"Create a Noovex account":(P==4&&g_su_form==1)?"Sign in with Noovex":SU_TITLE[P];
    int ox,oy,ow; ip_box(&ox,&oy,&ow,560,ch+132,ptitle);
    dstr(ox,oy,SU_DESC[P],rgb(20,24,34),1); oy+=18;
    { char t[24],*p=t; p=scopy(p,"Step "); p=udec(p,(u32)(P+1)); p=scopy(p," of "); p=udec(p,(u32)NSU); *p=0; dstr(ox,oy,t,rgb(110,118,135),1); } oy+=28;
    if(P==0) su_rows(ox,oy,ow,SU_LANGS,6,su_lsel[0]);
    else if(P==1) su_rows(ox,oy,ow,SU_KBDS,6,su_lsel[1]);
    else if(P==2) su_rows(ox,oy,ow,SU_TZS,7,su_lsel[2]);
    else if(P==3){
        int y=oy; su_nx=ox; su_ny=y+18; su_nw=ow;
        su_cfield(ox,y,ow,"Your name",setup_name,setup_nl,1,0,"Type your name (optional)"); y+=52;
        dstr(ox,y,"Accent colour",rgb(20,24,34),1); y+=18; su_ay=y;
        for(int k=0;k<5;k++){ int bx=ox+k*44; su_ax2[k]=bx; if(k==setup_acc) fill_rect(bx,su_ay,32,32,rgb(20,24,34)); fill_rect(bx+3,su_ay+3,26,26,rgb(248,249,251)); fill_rect(bx+5,su_ay+5,22,22,ACV[k]); }
        y=su_ay+44; dstr(ox,y,"Wallpaper",rgb(20,24,34),1); y+=18; su_wy=y;
        for(int k=0;k<3;k++){ int bx=ox+k*64; su_wx2[k]=bx; if(k==setup_th) fill_rect(bx-3,su_wy-3,60,40,rgb(20,24,34)); fill_rect(bx,su_wy,54,34,THSW[k]); }
    }
    else if(P==4){
        if(g_su_form==0){ su_rows(ox,oy,ow,SU_ACC,3,g_su_acc); dstr(ox,oy+3*22+6,"Enter your details on the next screen - no browser needed.",rgb(110,118,135),1); }
        else { su_f_x=ox; su_f_w=ow; su_f_h=22; su_f_uy=-1;
            su_f_ey=oy+18; su_cfield(ox,oy,ow,"Username",su_uname,su_ul,g_su_field==0,0,0);
            su_f_py=oy+18+48; su_cfield(ox,oy+48,ow,"Password",su_pwd,su_pl,g_su_field==1,1,0);
            if(su_msg[0]) dstr(ox,oy+96,su_msg,rgb(190,40,50),1);
            else dstr(ox,oy+96,g_su_form==2?"Pick a username and password for your account.":"TAB switches fields    ENTER submits",rgb(110,118,135),1); }
    }
    else if(P==5){ su_rows(ox,oy,ow,SU_STORAGE,2,g_su_storage);
        dstr(ox,oy+2*22+6,g_nvx_signedin?"Cloud saves your files to your account.":"Cloud needs a signed-in account - otherwise files stay local.",rgb(110,118,135),1); }
    else {
        dstr(ox,oy,"Everything is ready.",rgb(20,24,34),1);
        char b[96],*p;
        p=b; p=scopy(p,"Name:      "); p=scopy(p,setup_nl?setup_name:"user"); *p=0; dstr(ox,oy+22,b,rgb(20,24,34),1);
        p=b; p=scopy(p,"Language:  "); p=scopy(p,SU_LANGS[su_lsel[0]]); *p=0; dstr(ox,oy+40,b,rgb(20,24,34),1);
        p=b; p=scopy(p,"Keyboard:  "); p=scopy(p,SU_KBDS[su_lsel[1]]); *p=0; dstr(ox,oy+58,b,rgb(20,24,34),1);
        p=b; p=scopy(p,"Timezone:  "); p=scopy(p,SU_TZS[su_lsel[2]]); *p=0; dstr(ox,oy+76,b,rgb(20,24,34),1);
    }
    int by=oy+ch+20;
    const char* clab=(P==NSU-1)?"Get Started":(P==4?(g_su_form==1?"Sign in":g_su_form==2?"Create account":"Continue"):"Continue");
    if(P>0){ su_cbtn(ox+ow/3,by,"Back",0,&su_bbx,&su_bby,&su_bbw,&su_bbh); su_cbtn(ox+ow*2/3,by,clab,1,&su_cbx,&su_cby,&su_cbw,&su_cbh); }
    else { su_bbx=-1000; su_bby=-1000; su_bbw=0; su_bbh=0; su_cbtn(ox+ow/2,by,clab,1,&su_cbx,&su_cby,&su_cbw,&su_cbh); }
    draw_cursor(); blit_all();
}
static void su_apply(void){
    if(setup_nl>0){ int k=0; for(;k<setup_nl&&k<31;k++)g_user[k]=setup_name[k]; g_user[k]=0; } else scopy(g_user,"user");
    g_accent=setup_acc; ACC=ACV[setup_acc]; ACC2=mix(ACC,rgb(0,0,0),60);
    g_theme=setup_th; g_su_lang=su_lsel[0]; g_su_kbd=su_lsel[1]; g_su_tz=su_lsel[2]; g_cloud_fs=(g_su_storage==0 && g_nvx_signedin); build_bg(); nvx_settings_save(); if(g_cloud_fs)nvx_files_pull();
    if(k_find_file("/etc/nvx-installed")>=0){ extern void nvx_mark_file(const char*,const char*); char t[160],*p=t; p=scopy(p,"user="); p=scopy(p,g_user); p=scopy(p,"\naccent="); *p++=(char)('0'+g_accent); p=scopy(p,"\ntheme="); *p++=(char)('0'+g_theme); p=scopy(p,"\nlang="); p=udec(p,(u32)g_su_lang); p=scopy(p,"\nkbd="); p=udec(p,(u32)g_su_kbd); p=scopy(p,"\ntz="); p=udec(p,(u32)g_su_tz); p=scopy(p,"\n"); *p=0; nvx_mark_file("/etc/nvx-setup-done",t); }
}
static int su_kv(const char* d,int n,const char* key,char* out,int cap){ int kl=0; while(key[kl])kl++;
    for(int i=0;i<n;){ int j=i; while(j<n&&d[j]!='\n')j++; if(j-i>kl&&d[i+kl]=='='){ int m=1; for(int q=0;q<kl;q++) if(d[i+q]!=key[q]){ m=0; break; } if(m){ int o=0; for(int q=i+kl+1;q<j&&o<cap-1;q++)out[o++]=d[q]; out[o]=0; return o; } } i=j+1; }
    out[0]=0; return -1; }
int installed_boot(void){
    extern int disk_ok; if(!disk_ok||k_find_file("/etc/nvx-installed")<0)return 0;
    int k=k_find_file("/etc/nvx-setup-done"); if(k<0){ setup_begin(); return 1; }
    const char* d=fsd(k); int n=FS[k].len; char v[40];
    if(su_kv(d,n,"user",v,32)>0){ int q=0; for(;v[q]&&q<31;q++)g_user[q]=v[q]; g_user[q]=0; }
    if(su_kv(d,n,"accent",v,8)>0&&v[0]>='0'&&v[0]<='4'){ g_accent=v[0]-'0'; ACC=ACV[g_accent]; ACC2=mix(ACC,rgb(0,0,0),60); }
    if(su_kv(d,n,"theme",v,8)>0&&v[0]>='0'&&v[0]<='5')g_theme=v[0]-'0';
    if(su_kv(d,n,"lang",v,8)>0)g_su_lang=v[0]-'0'; if(su_kv(d,n,"kbd",v,8)>0)g_su_kbd=v[0]-'0'; if(su_kv(d,n,"tz",v,8)>0)g_su_tz=v[0]-'0';
    build_bg(); g_state=ST_LOGIN; login_pass[0]=0; login_pl=0; return 1; }
static void su_advance(void){
    if(g_su_page==4){
        if(g_su_form==0){
            if(g_su_acc==2){ g_su_page++; return; }                 /* skip -> finish */
            g_su_form=(g_su_acc==0)?1:2; g_su_field=0; su_msg[0]=0; return;   /* open the form */
        }
        su_apply(); su_msg[0]=0;
        int rc=(g_su_form==1)? nvx_signin(su_uname,su_pwd) : nvx_register(su_uname,su_pwd);
        if(rc==0){ if(g_nvx_user[0])scopy(g_user,g_nvx_user); g_su_form=0; g_su_page++; }   /* success -> finish */
        return;                                                     /* else stay, su_msg shows error */
    }
    if(g_su_page>=NSU-1){ su_apply(); g_state=ST_LOGIN; login_pass[0]=0; login_pl=0; return; }
    g_su_page++;
}
int setup_input(void){
    int changed=0, P=g_su_page;
    if(mouse_evt){ int dx,dy,btn; cpu_irq_off(); dx=mouse_dx;dy=mouse_dy;mouse_dx=0;mouse_dy=0;btn=mouse_btn;mouse_evt=0; cpu_irq_on();
        if(dx||dy){ mx+=dx;my-=dy; if(mx<0)mx=0;if(my<0)my=0;if(mx>=(int)W)mx=W-1;if(my>=(int)H)my=H-1; changed=1; }
        if(btn&&!mouse_btn_prev){ changed=1;
            if(P<=2){ int* sel=&su_lsel[P]; for(int k=0;k<su_ign;k++) if(in_rect(mx,my,su_ix,su_iy0+k*(su_ih+su_ig),su_iw,su_ih)){ *sel=k; } }
            if(P==3){ for(int k=0;k<5;k++) if(in_rect(mx,my,su_ax2[k],su_ay,32,32)){ setup_acc=k; ACC=ACV[k]; ACC2=mix(ACC,rgb(0,0,0),60); g_accent=k; }
                for(int k=0;k<3;k++) if(in_rect(mx,my,su_wx2[k],su_wy,54,34)) setup_th=k; }
            if(P==4){ if(g_su_form==0){ for(int k=0;k<su_ign;k++) if(in_rect(mx,my,su_ix,su_iy0+k*(su_ih+su_ig),su_iw,su_ih)) g_su_acc=k; }
                else { if(in_rect(mx,my,su_f_x,su_f_ey,su_f_w,su_f_h)) g_su_field=0;
                    else if(in_rect(mx,my,su_f_x,su_f_py,su_f_w,su_f_h)) g_su_field=1; } }
            if(P==5){ for(int k=0;k<su_ign;k++) if(in_rect(mx,my,su_ix,su_iy0+k*(su_ih+su_ig),su_iw,su_ih)) g_su_storage=k; }
            if(P>0 && in_rect(mx,my,su_bbx,su_bby,su_bbw,su_bbh)){ if(P==4&&g_su_form!=0){ g_su_form=0; su_msg[0]=0; } else g_su_page--; }
            else if(in_rect(mx,my,su_cbx,su_cby,su_cbw,su_cbh)){ su_advance(); }
        }
        mouse_btn_prev=btn; }
    int c; while((c=kpop())>=0){ changed=1;
        if(c==0x1B){ if(P==4&&g_su_form!=0){ g_su_form=0; su_msg[0]=0; } else if(g_su_page>0)g_su_page--; continue; }   /* ESC */
        if(c=='\n'||c=='\r'){ su_advance(); continue; }               /* ENTER */
        if(P==4 && g_su_form!=0){                                       /* account form fields */
            if(c=='\t'){ g_su_field=!g_su_field; continue; }
            char* buf; int* len; int cap;
            if(g_su_field==0){ buf=su_uname; len=&su_ul; cap=47; } else { buf=su_pwd; len=&su_pl; cap=47; }
            if(c=='\b'){ if(*len>0)(*len)--; buf[*len]=0; } else if(c>=32&&c<127){ if(*len<cap){buf[(*len)++]=(char)c; buf[*len]=0;} }
            continue;
        }
        if(c==19){ if(P<=2 && su_lsel[P]>0)su_lsel[P]--; else if(P==4 && g_su_acc>0)g_su_acc--; else if(P==5 && g_su_storage>0)g_su_storage--; continue; }        /* up arrow */
        if(c==20){ if(P<=2 && su_lsel[P]<su_ign-1)su_lsel[P]++; else if(P==4 && g_su_acc<2)g_su_acc++; else if(P==5 && g_su_storage<1)g_su_storage++; continue; } /* down arrow */
        if(P==5 && c=='w'){ if(g_su_storage>0)g_su_storage--; continue; }
        if(P==5 && c=='s'){ if(g_su_storage<1)g_su_storage++; continue; }
        if(P<=2 && c=='w'){ if(su_lsel[P]>0)su_lsel[P]--; continue; }
        if(P<=2 && c=='s'){ if(su_lsel[P]<su_ign-1)su_lsel[P]++; continue; }
        if(P==4 && g_su_form==0 && c=='w'){ if(g_su_acc>0)g_su_acc--; continue; }
        if(P==4 && g_su_form==0 && c=='s'){ if(g_su_acc<2)g_su_acc++; continue; }
        if(P==3){ if(c=='\b'){ if(setup_nl>0)setup_nl--; } else if(c>=32&&c<127){ if(setup_nl<31)setup_name[setup_nl++]=(char)c; } setup_name[setup_nl]=0; }
    }
    static int lb=-1; int bp=(int)((g_ticks/50)&1); if(bp!=lb){lb=bp;changed=1;}
    return changed;
}

/* ---- login ---- */
static int lg_bx,lg_by,lg_bw,lg_bh, lg_px,lg_py,lg_pw, lg_pwr;
const char *MON[12]={"January","February","March","April","May","June","July","August","September","October","November","December"};
static int lg_avx,lg_avy,lg_avr;
static int lg_sd,lg_rs,lg_sl;
static void pwr_icon(int cx,int cy,int kind,u32 c){
    if(kind==0){ soft_ring(cx,cy,12,2,c,220); for(int t=-8;t<=-1;t++)putpx(cx,cy+t,c); putpx(cx-1,cy-8,c); putpx(cx+1,cy-8,c); }
    else if(kind==1){ soft_ring(cx,cy,12,2,c,220); for(int t=-5;t<=1;t++){ putpx(cx-6+t,cy-6,c); } for(int t=0;t<6;t++){ putpx(cx-6,cy-6+t,c); putpx(cx-6+t,cy-6+t,c);} }
    else { for(int a=-9;a<=9;a++){ int yy=cy-(int)(a*a)/26; putpx(cx+a,yy,c); putpx(cx+a,yy+1,c);} for(int a=-9;a<=9;a++){ int yy=cy+6-(int)(a*a)/26; putpx(cx+a,yy,c);} }
}
void login_present(void){
    memcpy(SCENE,BG,(u64)W*H*4);
    /* keep wallpaper vivid; only a faint top-and-bottom vignette */
    for(u32 y=0;y<H;y++){ int t=0; if(y<H/6)t=(H/6-y)*40/(H/6); else if(y>H*5/6)t=(y-H*5/6)*60/(H/6); if(t){ u32*r=&SCENE[(u64)y*SW]; for(u32 x=0;x<W;x++)r[x]=mix(r[x],rgb(6,8,16),t); } }
    /* top menu bar */
    { for(u32 x=0;x<W;x++){ u32*p=&SCENE[x]; for(int y=0;y<26;y++){ p=&SCENE[(u64)y*SW+x]; *p=mix(*p,rgb(8,10,18),150); } }
      int hh,mm,ss,yy,mo,dd; read_clock(&hh,&mm,&ss,&yy,&mo,&dd); (void)ss;
      static const char* WD3[7]={"Sun","Mon","Tue","Wed","Thu","Fri","Sat"};
      int w=(yy+ (mo<3?yy-1:yy)/4 - (mo<3?yy-1:yy)/100 + (mo<3?yy-1:yy)/400 + (int[]){0,3,2,5,0,3,5,1,4,6,2,4}[mo-1] + dd)%7;
      char tb[40],*p=tb; p=scopy(p,WD3[w]); *p++=' '; if(mo>=1&&mo<=12){ p=scopy(p,MON[mo-1]); *p++=' '; } p=udec(p,(u32)dd); *p++=' '; *p++=' '; p=u2(p,hh);*p++=':';p=u2(p,mm); *p=0;
      dstr((int)W-slen(tb)*8-14,6,tb,rgb(236,240,248),1);
      /* wifi + battery glyphs */
      int bx=(int)W-slen(tb)*8-58; for(int b=0;b<3;b++){ int hgt=3+b*3; for(int t=0;t<hgt;t++)putpx(bx+b*5,14-t,rgb(220,226,238)); putpx(bx+b*5+1,14,rgb(220,226,238)); }
      int cbx=(int)W-slen(tb)*8-96; for(int t=0;t<18;t++){ putpx(cbx+t,7,rgb(210,216,230)); putpx(cbx+t,15,rgb(210,216,230)); } for(int t=7;t<=15;t++){ putpx(cbx,t,rgb(210,216,230)); putpx(cbx+17,t,rgb(210,216,230)); } for(int t=8;t<15;t++)for(int q=2;q<12;q++)putpx(cbx+q,t,rgb(120,220,140));
      dstr(12,6,"NoovexOS",rgb(236,240,248),1); }
    int cx=(int)W/2, cy=(int)H*44/100;
    /* avatar: liquid-glass disc with the user initial + soft drop shadow */
    int R=54; lg_avx=cx; lg_avy=cy; lg_avr=R;
    for(int yy2=-R-8;yy2<=R+10;yy2++)for(int xx=-R-8;xx<=R+8;xx++){ int d2=xx*xx+(yy2-4)*(yy2-4); if(d2>=(R+3)*(R+3)&&d2<=(R+10)*(R+10)){ int px=cx+xx,py=cy+yy2; if((u32)px<W&&(u32)py<H){ u32*p=&SCENE[(u64)py*SW+px]; *p=mix(*p,rgb(0,0,0),80); } } }
    for(int yy2=-R;yy2<=R;yy2++)for(int xx=-R;xx<=R;xx++){ int d2=xx*xx+yy2*yy2; if(d2>R*R)continue; int px=cx+xx,py=cy+yy2; if((u32)px>=W||(u32)py>=H)continue; u32*p=&SCENE[(u64)py*SW+px];
        u32 glass=mix(*p,ACC,150); int grad=(yy2+R)*230/(2*R); glass=mix(rgb(255,255,255),glass,grad);
        int edge2=R-(int)isqrt32((u32)d2); if(edge2<3)glass=mix(glass,rgb(255,255,255),150);
        *p=glass; }
    { char ini=g_user[0]; if(ini>='a'&&ini<='z')ini-=32; char sst[2]={ini?ini:'U',0}; dstr_ctr(cx+1,cy-15+1,sst,rgb(0,0,0),5); dstr_ctr(cx,cy-15,sst,rgb(255,255,255),5); }
    { char nm[32]; int k=0; for(;g_user[k]&&k<31;k++)nm[k]=(k==0&&g_user[k]>='a'&&g_user[k]<='z')?g_user[k]-32:g_user[k]; nm[k]=0;
      dstr_ctr(cx+1,cy+R+20+1,nm,rgb(0,0,0),3); dstr_ctr(cx,cy+R+20,nm,rgb(248,250,255),3); }
    /* password pill — compact liquid glass, macOS proportions */
    lg_pw=210; int ph=32; lg_px=cx-lg_pw/2; lg_py=cy+R+56;
    glass_liquid(lg_px,lg_py,lg_pw,ph,ph/2,rgb(236,240,250),26,6);
    lg_bw=26; lg_bh=26; lg_bx=lg_px+lg_pw-30; lg_by=lg_py+3;
    if(login_pl==0){ dstr_ctr(lg_px+lg_pw/2-6,lg_py+11,"Enter Password",rgb(90,100,120),1); }
    else { int c2=lg_px+18; for(int k=0;k<login_pl&&k<14;k++){ fill_circle(c2,lg_py+ph/2,4,rgb(40,50,70)); c2+=14; }
        if(((g_ticks/40)&1)){ int cxp=lg_px+18+login_pl*14-4; fill_rect(cxp,lg_py+9,2,ph-18,ACC); } }
    /* arrow unlock */
    int hov=in_rect(mx,my,lg_bx,lg_by,lg_bw,lg_bh);
    for(int yy2=-13;yy2<=13;yy2++)for(int xx=-13;xx<=13;xx++){ if(xx*xx+yy2*yy2>13*13)continue; int px=lg_bx+13+xx,py=lg_by+13+yy2; if((u32)px<W&&(u32)py<H){ u32*p=&SCENE[(u64)py*SW+px]; u32 base=mix(*p,ACC,hov?255:215); if(yy2<0)base=mix(base,rgb(255,255,255),210); *p=base; } }
    { int ax=lg_bx+11, ay=lg_by+13; for(int t=-4;t<=4;t++)putpx(ax+t,ay,rgb(255,255,255)); for(int t=0;t<5;t++){ putpx(ax+4-t,ay-t,rgb(255,255,255)); putpx(ax+4-t,ay+t,rgb(255,255,255)); } }
    /* help ? circle to the right */
    { int qx=lg_px+lg_pw+22, qy=lg_py+ph/2; soft_ring(qx,qy,9,2,rgb(210,216,230),200); dstr_ctr(qx,qy-7,"?",rgb(220,226,238),1); }
    { const char* hint=login_pl?"Press Return to unlock":"Your password is required to enable Touch ID"; dstr_ctr(cx,lg_py+ph+22,hint,rgb(206,214,230),1); }
    /* bottom controls: Shut Down / Restart / Sleep */
    { int by=(int)H-92, sp=140; lg_sd=cx-sp; lg_rs=cx; lg_sl=cx+sp;
      int h0=in_rect(mx,my,lg_sd-24,by-24,48,48), h1=in_rect(mx,my,lg_rs-24,by-24,48,48), h2=in_rect(mx,my,lg_sl-24,by-24,48,48);
      pwr_icon(lg_sd,by,0,h0?rgb(255,255,255):rgb(220,226,238)); pwr_icon(lg_rs,by,1,h1?rgb(255,255,255):rgb(220,226,238)); pwr_icon(lg_sl,by,2,h2?rgb(255,255,255):rgb(220,226,238));
      dstr_ctr(lg_sd,by+22,"Shut Down",rgb(206,214,230),1); dstr_ctr(lg_rs,by+22,"Restart",rgb(206,214,230),1); dstr_ctr(lg_sl,by+22,"Sleep",rgb(206,214,230),1); }
    draw_cursor(); blit_all();
}
int login_input(void){
    int changed=0;
    if(mouse_evt){ int dx,dy,btn; cpu_irq_off(); dx=mouse_dx;dy=mouse_dy;mouse_dx=0;mouse_dy=0;btn=mouse_btn;mouse_evt=0; cpu_irq_on();
        if(dx||dy){ mx+=dx;my-=dy; if(mx<0)mx=0;if(my<0)my=0;if(mx>=(int)W)mx=W-1;if(my>=(int)H)my=H-1; changed=1; }
        if(btn&&!mouse_btn_prev){ int by=(int)H-92;
            if(in_rect(mx,my,lg_bx,lg_by,lg_bw,lg_bh)){ g_state=ST_DESKTOP; if(g_rec_openterm){open_win(W_TERM);g_rec_openterm=0;} }
            else if(in_rect(mx,my,lg_sd-24,by-24,48,48)){ power_off(); }
            else if(in_rect(mx,my,lg_rs-24,by-24,48,48)){ power_reboot(); }
            else if(in_rect(mx,my,lg_sl-24,by-24,48,48)){ power_sleep(); }
            changed=1; }
        mouse_btn_prev=btn; }
    int c; while((c=kpop())>=0){ changed=1;
        if(c=='\n'){ g_state=ST_DESKTOP; if(g_rec_openterm){open_win(W_TERM);g_rec_openterm=0;} }
        else if(c=='\b'){ if(login_pl>0)login_pl--; }
        else if(c>=32&&c<127){ if(login_pl<31)login_pass[login_pl++]=(char)c; }
        login_pass[login_pl]=0; }
    static int lb=-1; int bp=(int)((g_ticks/50)&1); if(bp!=lb){lb=bp;changed=1;}
    return changed;
}

void setup_begin(void){ g_su_page=0; g_su_form=0; g_state=ST_SETUP; setup_present(); }
