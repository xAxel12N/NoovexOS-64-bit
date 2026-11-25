#include "drivers/input/gamepad/gpad_proto.h"
#include "drivers/input/gamepad/gamepad.h"
int gpusb_scan(void); void gpusb_poll(void); int gpusb_enabled(void); const char* gpusb_state(void); void gpusb_enable(int on);
#include "include/nvx/core/auth/accounts.h"
#include "include/nvx/core/kaslr.h"
#include "include/nvx/shell/vtcon.h"
#include "include/nvx/shell/commands.h"
#include "fs/ext4/ext4.h"
#include "include/nvx/core/sysfeat.h"
#include "include/nvx/core/sound.h"
void svcu_list(char*,int); void svcu_logs(const char*,char*,int); int svcu_start(const char*); int svcu_stop(const char*); int svcu_restart(const char*); int svcu_enable(const char*,int);
const char* cur_user(void); int sudo_fresh(void); void sudo_touch(void); int vtc_slot_info(int n,char* user,int cap); int bpf_allowed(void);
int umidi_scan(void); int umidi_info(char*,int); int msynth_enable(int); int msynth_enabled(void); void msynth_note(int,int,int);
int ux_read_file(const char*,char*,int); int ux_write_text(const char*,const char*,int,unsigned); int ext4k_mounted(void);
#include "apps/system/settings/secui.c"
static char ux_sn[8][24], ux_ss[8][12]; static int ux_se[8], ux_sc=0; static u64 ux_sat=0;
static char ux_log[1024]; static char ux_logname[24];
static int ux_pend=0, ux_pwn=0, ux_tries=0, ux_pwon=0; static char ux_pw[72];
static int ux_ch=3, ux_cm=0, ux_cmdn=0, ux_cmdon=0; static char ux_cmd[96];
static char ux_cron[2048]; static int ux_cronn=-2; static u64 ux_cronat=0;
static const char* st_ux_title(int p){ switch(p){ case P_UXHUB:return "Advanced system"; case P_UXSVC:return "Services"; case P_UXSSH:return "Remote login"; case P_UXCRON:return "Scheduled tasks"; case P_UXDISK:return "File systems"; case P_UXMIDI:return "MIDI"; case P_UXVT:return "Terminals"; case P_UXPAD:return "Game controllers"; case P_UXSEC:return "Security features"; case P_UXPRN:return "Printers & Scanners"; default:return 0; } }
static int ux_pre(const char* s,const char* t){ while(*t){ if(*s!=*t)return 0; s++; t++; } return 1; }
static int ux_word(const char** s,char* o,int cap){ const char* p=*s; while(*p==' ')p++; int n=0; while(*p&&*p!=' '&&*p!='\n'){ if(n<cap-1)o[n++]=*p; p++; } o[n]=0; *s=p; return n; }
static void ux_svc_sync(void){ extern volatile u64 g_ticks; if(ux_sat&&g_ticks-ux_sat<100)return; ux_sat=g_ticks; static char b[2048]; svcu_list(b,sizeof b); ux_sc=0; const char* p=b;
    while(*p&&ux_sc<8){ char en[12]; if(!ux_word(&p,ux_ss[ux_sc],12))break; ux_word(&p,ux_sn[ux_sc],24); ux_word(&p,en,12); ux_se[ux_sc]=en[0]=='e'; while(*p&&*p!='\n')p++; if(*p)p++; if(ux_sn[ux_sc][0])ux_sc++; } }
static char* ux_cron_path(char* o){ char* q=scopy(o,"/var/spool/cron/crontabs/"); return scopy(q,cur_user()); }
static void ux_cron_sync(void){ extern volatile u64 g_ticks; if(ux_cronn!=-2&&g_ticks-ux_cronat<100)return; ux_cronat=g_ticks; char pth[96]; ux_cron_path(pth); ux_cronn=ux_read_file(pth,ux_cron,sizeof ux_cron); if(ux_cronn<0)ux_cron[0]=0; }
static int ux_cron_line(int i,const char** s,int* len){ const char* p=ux_cron; int k=0; while(*p){ const char* e=p; while(*e&&*e!='\n')e++; const char* t=p; while(*t==' '||*t=='\t')t++; if(t<e&&*t!='#'&&!(*t>='A'&&*t<='Z'&&0)){ int env=0; for(const char* z=t;z<e&&*z!=' ';z++) if(*z=='='){ env=1; break; } if(!env){ if(k==i){ *s=p; *len=(int)(e-p); return 1; } k++; } } p=*e?e+1:e; } return 0; }
static int ux_card_rows(int cy,const char* title,const char* t,const char* d){ int y=st_card(cy,64,title); st_rowtext(y,t,d); return y; }
static void st_ux_pwrow(int* cy){ if(!ux_pend)return; int X1=st_x0+st_w0-24; int y=st_card(*cy,64,"Administrator password"); char m[40]; int n=ux_pwn>36?36:ux_pwn; for(int z=0;z<n;z++)m[z]='*'; m[n]=0;
    fill_round(st_x0+20,y+16,st_w0-260,32,8,15,rgb(240,242,246)); pdstr(st_x0+32,y+25,m[0]?m:"Password for sudo, then Enter",m[0]?ST_FG:ST_DIM,1); st_hit(st_x0+20,y+16,st_w0-260,32,3708);
    st_btn(X1-200,y+16,90,"Cancel",3711,0); st_btn(X1-100,y+16,100,"OK",3709,1); *cy=y+64+24; }
static void st_ux_page(int p,int cy){
    int X1=st_x0+st_w0-24; char b[160],*q; int y;
    if(p==P_UXHUB){ static const stile T[9]={{P_UXSVC,12,"Services","Start, stop and enable background programs"},{P_UXSSH,2,"Remote login","SSH server on or off"},{P_UXCRON,13,"Scheduled tasks","Run a command at a set time every day"},{P_UXDISK,5,"File systems","Disks, read-only or read-write"},{P_UXMIDI,10,"MIDI","Music keyboards and the built-in synth"},{P_UXVT,6,"Terminals","Text consoles on Ctrl+Alt+F1 to F6"},{P_UXSEC,12,"Security features","Address randomisation, counters, eBPF"},{P_UXPRN,5,"Printers & Scanners","USB printers, test page, scanning"},{P_UXPAD,10,"Game controllers","Xbox, PlayStation, Switch Pro and USB gamepads"}}; st_tiles(&cy,T,9); return; }
    st_ux_pwrow(&cy);
    if(p==P_UXSVC){ ux_svc_sync(); int n=ux_sc; if(!n){ ux_card_rows(cy,"Services","No services","Unit files go in /etc/nvx/services"); return; }
        y=st_card(cy,n*56,"Services");
        for(int i=0;i<n;i++){ int ry=y+i*56; if(i)st_sep(ry); pdstr(st_x0+20,ry+10,ux_sn[i],ST_FG,1); q=b; q=scopy(q,ux_ss[i]); q=scopy(q,ux_se[i]?", starts at boot":", does not start at boot"); *q=0; pdstr(st_x0+20,ry+30,b,ST_DIM,1);
            int run=ux_ss[i][0]=='r'; int bx=X1-70; st_btn(bx,ry+11,70,"Log",3680+i,0); bx-=96; st_btn(bx,ry+11,90,ux_se[i]?"Disable":"Enable",3660+i,0); bx-=96; st_btn(bx,ry+11,90,"Restart",3640+i,0); bx-=86; st_btn(bx,ry+11,80,run?"Stop":"Start",run?3620+i:3600+i,run?2:1); }
        cy=y+n*56+24;
        if(ux_logname[0]){ y=st_card(cy,8*18+20,ux_logname); int ln=0; const char* s=ux_log; const char* L[64]; int nl=0; while(*s&&nl<64){ L[nl++]=s; while(*s&&*s!='\n')s++; if(*s)s++; } int st=nl>8?nl-8:0;
            for(int k=st;k<nl;k++){ int c=0; char t[96]; while(L[k][c]&&L[k][c]!='\n'&&c<(st_w0-40)/8&&c<95){ t[c]=L[k][c]; c++; } t[c]=0; pdstr(st_x0+20,y+10+ln*18,t,ST_DIM,1); ln++; } }
        return; }
    if(p==P_UXSSH){ static char sb[1024]; extern void svcu_status(const char*,char*,int); svcu_status("sshd",sb,sizeof sb); const char* s=sb; int run=0, en=0, none=0; for(;*s;s++){ if(ux_pre(s,"State:    running"))run=1; if(ux_pre(s,"(enabled)"))en=1; if(ux_pre(s,"no such service"))none=1; }
        y=st_card(cy,192,"SSH server"); st_rowtext(y,none?"Not installed":run?"On":"Off",none?"This image has no sshd service":run?"Others can sign in to this computer over the network on port 22":"Nobody can sign in over the network");
        if(!none){ st_btn(X1-110,y+16,110,run?"Turn off":"Turn on",run?3701:3700,run?2:1); }
        st_sep(y+64);
        st_rowtext(y+64,"Start at boot",none?"Not available":en?"The SSH server starts by itself when the computer starts":"The SSH server only runs when it is turned on here or with the services command");
        st_chip(X1,y+84,none?"Off":en?"On":"Off",en&&!none?rgb(226,246,233):rgb(238,241,246),en&&!none?rgb(40,140,80):rgb(90,98,118));
        st_sep(y+128);
        st_rowtext(y+128,"Root password login","Always off. Sign in with a user account and its password, then use sudo"); return; }
    if(p==P_UXCRON){ ux_cron_sync(); int n=0; const char* s; int l; while(n<8&&ux_cron_line(n,&s,&l))n++;
        y=st_card(cy,n?n*44:44,"Your scheduled tasks");
        if(!n)pdstr(st_x0+20,y+14,"No tasks yet",ST_DIM,1);
        for(int i=0;i<n;i++){ ux_cron_line(i,&s,&l); int ry=y+i*44; if(i)st_sep(ry); char t[100]; int c=0; while(c<l&&c<(st_w0-160)/8&&c<99){ t[c]=s[c]=='\t'?' ':s[c]; c++; } t[c]=0; pdstr(st_x0+20,ry+14,t,ST_FG,1); st_btn(X1-90,ry+5,90,"Remove",3730+i,2); }
        cy=y+(n?n*44:44)+24; y=st_card(cy,128,"Add a task");
        fr_label(y,"Every day at"); q=b; q=u2(q,ux_ch); *q++=':'; q=u2(q,ux_cm); *q=0; st_bold(st_x0+160,y+16,b,ST_FG);
        st_btn(st_x0+230,y+8,40,"-",3720,0); st_btn(st_x0+274,y+8,40,"+",3721,0); pdstr(st_x0+330,y+16,"hour",ST_DIM,1); st_btn(st_x0+380,y+8,40,"-",3722,0); st_btn(st_x0+424,y+8,40,"+",3723,0); pdstr(st_x0+480,y+16,"minute",ST_DIM,1); st_sep(y+64);
        fill_round(st_x0+20,y+80,st_w0-170,32,8,15,ux_cmdon?ACC:rgb(240,242,246)); fill_round(st_x0+21,y+81,st_w0-172,30,7,15,rgb(240,242,246)); ux_cmd[ux_cmdn]=0; pdstr(st_x0+32,y+89,ux_cmdn?ux_cmd:"Command to run",ux_cmdn?ST_FG:ST_DIM,1); st_hit(st_x0+20,y+80,st_w0-170,32,3707);
        st_btn(X1-100,y+80,100,"Add",3704,1); return; }
    if(p==P_UXDISK){ int st=sysfeat_state(SYSF_EXT4_RW), v=sysfeat_get(SYSF_EXT4_RW); const char* why=sysfeat_text(SYSF_EXT4_RW);
        y=st_card(cy,128,"ext4 disks"); st_rowtext(y,ext4k_mounted()?(g_ext4.ro?"Mounted read-only":"Mounted read-write"):"No ext4 disk mounted",ext4k_mounted()?(g_ext4.ro?"Files can be read but not changed":"Changes are written to the disk"):"Linux disks show up here when found"); st_sep(y+64);
        st_rowtext(y+64,v==SYSF_EXT4_FLUSH&&st==SYSF_ST_ON?"Read-write allowed":"Read-only by policy",why&&why[0]?why:"Writing stays off until it is verified on this disk"); cy=y+128+24;
        y=st_card(cy,256,"Other disks and SD cards"); st_rowtext(y,"Browse ext4 disks and SD cards in Files",sysfeat_text(SYSF_VOLFILES)); st_toggle(X1-44,y+20,sysfeat_state(SYSF_VOLFILES)==SYSF_ST_ON,3760); st_sep(y+64);
        st_rowtext(y+64,"Fast SD card transfers (ADMA2)",sysfeat_text(SYSF_SD_ADMA)); st_toggle(X1-44,y+84,sysfeat_state(SYSF_SD_ADMA)==SYSF_ST_ON,3761); st_sep(y+128);
        st_rowtext(y+128,"exFAT SD cards and USB sticks",sysfeat_text(SYSF_EXFAT)); st_toggle(X1-44,y+148,sysfeat_state(SYSF_EXFAT)==SYSF_ST_ON,3762); st_sep(y+192);
        st_rowtext(y+192,"Change files on USB sticks and fixed disks",sysfeat_text(SYSF_VOLWRITE)); st_toggle(X1-44,y+212,sysfeat_state(SYSF_VOLWRITE)==SYSF_ST_ON,3763); cy=y+256+24;
        static char mb[512]; extern int proc_gen(const char*,char*,int); int n=proc_gen("/proc/mounts",mb,sizeof mb); if(n<0)mb[0]=0; const char* s=mb; int rows=0; const char* L[8]; while(*s&&rows<8){ L[rows++]=s; while(*s&&*s!='\n')s++; if(*s)s++; }
        if(rows){ y=st_card(cy,rows*40,"Mounted file systems"); for(int i=0;i<rows;i++){ int ry=y+i*40; if(i)st_sep(ry); char dev[32],mp[48],fs[16],op[24]; const char* t=L[i]; ux_word(&t,dev,32); ux_word(&t,mp,48); ux_word(&t,fs,16); ux_word(&t,op,24);
            q=b; q=scopy(q,mp); q=scopy(q,"  "); q=scopy(q,fs); *q=0; pdstr(st_x0+20,ry+13,b,ST_FG,1); const char* rw=(op[0]=='r'&&op[1]=='o')?"read-only":"read-write"; pdstr(X1-pw_str(rw,1),ry+13,rw,ST_DIM,1); } }
        return; }
    if(p==P_UXPRN){ extern int uprn_info(char*,int); extern int uprn_present(void); extern int uprn_has_ipp(void); extern char g_prn_status[]; extern char g_scan_status[]; static char pb[512]; uprn_info(pb,sizeof pb); char* nl=pb; while(*nl&&*nl!='\n')nl++; char sv=*nl; *nl=0;
        y=st_card(cy,64,"USB printer"); st_rowtext(y,pb,uprn_present()?(uprn_has_ipp()?"IPP over USB: prints and scans without a driver":"USB printer class: raw printing only"):"Plug in a printer with a USB cable"); st_btn(X1-120,y+16,120,"Look again",3750,0); *nl=sv; cy=y+64+24;
        y=st_card(cy,128,"Printing"); st_rowtext(y,"Use this printer",g_prn_status); st_btn(X1-110,y+16,110,"Use",3753,1); st_sep(y+64);
        st_rowtext(y+64,"Test page","Prints one A4 page with text"); st_btn(X1-110,y+80,110,"Print",3751,1); cy=y+128+24;
        y=st_card(cy,64,"Scanning"); st_rowtext(y,"Scan to Pictures (300 dpi, colour)",g_scan_status); st_btn(X1-110,y+16,110,"Scan",3752,1); return; }
    if(p==P_UXMIDI){ umidi_info(b,sizeof b); y=st_card(cy,64,"Devices"); st_rowtext(y,b[0]?b:"No MIDI device","Plug in a USB music keyboard"); st_btn(X1-120,y+16,120,"Look again",3712,0); cy=y+64+24;
        y=st_card(cy,128,"Built-in synth"); st_rowtext(y,"Play notes through the speakers",!msynth_enabled()&&g_snd_state==2?"No audio output - this computer has no sound card the synth can use":"Notes from a MIDI keyboard are played by NoovexOS"); st_toggle(X1-44,y+20,msynth_enabled(),3702); st_sep(y+64);
        st_rowtext(y+64,"Test note","Plays middle C for a moment"); st_btn(X1-110,y+80,110,"Play",3703,1); cy=y+128+24;
        y=st_card(cy,64,"Camera for Linux programs"); st_rowtext(y,"/dev/video0 for ffmpeg, browsers and video calls",sysfeat_text(SYSF_V4L2)); st_toggle(X1-44,y+20,sysfeat_state(SYSF_V4L2)==SYSF_ST_ON,3764); cy=y+64+24;
        y=st_card(cy,64,"USB serial adapters"); st_rowtext(y,"Arduino, ESP32, Pico and FTDI/CP210x/CH340/PL2303 cables",sysfeat_text(SYSF_USBSER)); st_toggle(X1-44,y+20,sysfeat_state(SYSF_USBSER)==SYSF_ST_ON,3765); return; }
    if(p==P_UXPAD){ static int gsc; if(!gsc){ gsc=1; gpusb_scan(); } gpusb_poll(); static char gb[1024]; gpad_info(gb,sizeof gb); int nl=1; for(char* c=gb;*c;c++) if(*c=='\n')nl++; y=st_card(cy,64,"Controllers"); st_rowtext(y,gpad_count()?"Connected":"No game controller","Plug in an Xbox, PlayStation, Switch Pro or USB gamepad"); st_btn(X1-120,y+16,120,"Look again",3740,0); cy=y+64+24;
        if(gpad_count()){ y=st_card(cy,20+nl*18,"Test buttons and sticks"); int ly=y+10; char* s0=gb; for(char* c=gb;;c++){ if(*c=='\n'||!*c){ char e=*c; *c=0; pdstr(st_x0+20,ly,s0,ST_FG,1); ly+=18; if(!e)break; s0=c+1; } } cy=y+20+nl*18+24;
            y=st_card(cy,128,"Rumble and light"); st_rowtext(y,"Rumble test","Shakes every controller for half a second"); st_btn(X1-110,y+16,110,"Rumble",3741,1); st_sep(y+64);
            st_rowtext(y+64,"Light bar colour","DualShock 4 and DualSense"); st_btn(X1-110,y+80,110,"Next colour",3742,0); cy=y+128+24; }
        y=st_card(cy,128,"Options"); st_rowtext(y,"Touchpad moves the pointer","DualShock 4 and DualSense touchpad works as a mouse"); st_toggle(X1-44,y+20,g_gp_tpptr,3743); st_sep(y+64);
        st_rowtext(y+64,"Game controller support",gpusb_state()); st_toggle(X1-44,y+84,gpusb_enabled(),3744); return; }
    if(p==P_UXVT){ y=st_card(cy,64,0); st_rowtext(y,"Press Ctrl+Alt+F1 to F6 for a text console","Ctrl+Alt+F7 comes back to the desktop. Each console asks you to sign in."); cy=y+64+24;
        y=st_card(cy,VTC_N*40,"Consoles"); for(int i=1;i<=VTC_N;i++){ int ry=y+(i-1)*40; if(i>1)st_sep(ry); char u[33]; int r=vtc_slot_info(i,u,33); q=b; q=scopy(q,"Ctrl+Alt+F"); q=udec(q,(u32)i); q=scopy(q,"  /dev/tty"); q=udec(q,(u32)i); *q=0; pdstr(st_x0+20,ry+13,b,ST_FG,1);
            q=b; if(r==2){ q=scopy(q,"signed in: "); q=scopy(q,u); } else q=scopy(q,r==1?"waiting for sign-in":"not opened yet"); if(g_vtc_active==i)q=scopy(q,"  (shown)"); *q=0; pdstr(X1-pw_str(b,1),ry+13,b,r==2?rgb(40,140,80):ST_DIM,1); }
        return; }
    if(p==P_UXSEC)cy=secui_page(cy);
    if(p==P_UXSEC){ y=st_card(cy,64*4,0); st_rowtext(y,g_kaslr_on?"Address randomisation is on":"Address randomisation is off","New programs load at random addresses. Change it with: sudo kaslr on|off"); st_sep(y+64);
        st_rowtext(y+64,"Performance counters","Your own programs only; watching other users' programs needs root"); st_sep(y+128);
        st_rowtext(y+128,"eBPF","Loading eBPF programs needs root; normal users cannot"); st_sep(y+192);
        { extern const char* harden_line(void); extern int g_hd_smap; st_rowtext(y+192,harden_line(),g_hd_smap==1?"Kernel protections set on every processor at start":"Kernel protections set on every processor at start; SMAP stays off until all system calls use checked copies"); } return; }
}
static void ux_run_priv(int id);
static int ux_need_admin(int id){
    extern volatile u64 g_ticks; if(g_uid==0)return 1; const char* who=cur_user();
    if(!acct_is_admin(g_uid)){ acct_log("sudo: refused for ",who," (not an administrator, Settings)"); toast("Only an administrator can do this"); return 0; }
    if(sudo_fresh())return 1;
    if(!acct_has_password(who)){ sudo_touch(); return 1; }
    ux_pend=id; ux_pwn=0; ux_pw[0]=0; ux_tries=0; ux_pwon=1; return 0; }
static void ux_wipe(void){ for(int i=0;i<72;i++)((volatile char*)ux_pw)[i]=0; ux_pwn=0; }
static void st_ux_heavy(int id);
static int st_ux_key(int c){
    if(ux_pend&&ux_pwon){ if(c==27){ ux_wipe(); ux_pend=0; return 1; } if(c=='\b'){ if(ux_pwn>0)ux_pwn--; ux_pw[ux_pwn]=0; return 1; } if(c=='\n'){ st_ux_heavy(3709); return 1; } if(c>=32&&c<127&&ux_pwn<70){ ux_pw[ux_pwn++]=(char)c; ux_pw[ux_pwn]=0; } return 1; }
    if(ux_cmdon&&g_stpg==P_UXCRON){ if(c==27){ ux_cmdon=0; return 1; } if(c=='\b'){ if(ux_cmdn>0)ux_cmdn--; return 1; } if(c=='\n'){ ux_cmdon=0; return 1; } if(c>=32&&c<127&&c!='%'&&ux_cmdn<94){ ux_cmd[ux_cmdn++]=(char)c; ux_cmd[ux_cmdn]=0; } return 1; }
    return 0; }
static int ux_pw_submit(void){ const char* who=cur_user(); ux_pw[ux_pwn]=0; int ok=acct_check(who,ux_pw); ux_wipe();
    if(ok){ sudo_touch(); acct_log("sudo: accepted for ",who," (Settings)"); int id=ux_pend; ux_pend=0; return id; }
    acct_log("sudo: wrong password for ",who," (Settings)"); if(++ux_tries>=3){ ux_pend=0; toast("3 incorrect password attempts"); } else toast("Wrong password, try again"); return 0; }
static int st_ux_click(int id){
    if(id>=3780&&id<3800)return secui_click(id);
    if(id==3708){ ux_pwon=1; ux_cmdon=0; return 0; } if(id==3711){ ux_wipe(); ux_pend=0; return 0; }
    if(id==3709){ return ux_pend?3709:0; }
    if(id==3707){ ux_cmdon=1; ux_pwon=ux_pend!=0; return 0; }
    if(id>=3720&&id<=3723){ if(id==3720)ux_ch=(ux_ch+23)%24; if(id==3721)ux_ch=(ux_ch+1)%24; if(id==3722)ux_cm=(ux_cm+55)%60; if(id==3723)ux_cm=(ux_cm+5)%60; return 0; }
    if(id>=3680&&id<3700){ int i=id-3680; if(i>=ux_sc)return 0; scopy(ux_logname,ux_sn[i])[0]=0; svcu_logs(ux_sn[i],ux_log,sizeof ux_log); return 0; }
    if(id==3703){ if(!msynth_enabled()&&!msynth_enable(1)){ toast("No audio output for the synth"); return 0; } msynth_note(0,60,100); return 0; }
    if(id==3702){ if(msynth_enabled()){ msynth_enable(0); toast("Synth off"); } else { umidi_scan(); toast(msynth_enable(1)?"Synth on - play the MIDI keyboard":"No audio output for the synth"); } return 0; }
    if(id==3712){ umidi_scan(); return 0; }
    if(id==3740){ gpusb_scan(); toast(gpad_count()?"Game controller found":"No game controller found"); return 0; }
    if(id==3741){ int n=0; for(int i=0;i<GP_MAX;i++) if(gpad_rumble(i,200,200,500)>0)n++; toast(n?"Rumble":"No controller can rumble"); return 0; }
    if(id==3742){ static int ci; static const u8 C[6][3]={{0,0,64},{64,0,0},{0,64,0},{64,0,64},{64,32,0},{0,0,0}}; ci=(ci+1)%6; for(int i=0;i<GP_MAX;i++)gpad_led(i,C[ci][0],C[ci][1],C[ci][2]); return 0; }
    if(id==3743){ g_gp_tpptr=!g_gp_tpptr; toast(g_gp_tpptr?"Touchpad moves the pointer":"Touchpad pointer off"); return 0; }
    if(id==3744){ gpusb_enable(!gpusb_enabled()); toast(gpusb_enabled()?"Game controllers on":"Game controllers off - plugged-in controllers keep working until unplugged"); return 0; }
    if(id==3774){ extern void snd_usb_route(int); extern int snd_usb_routed(void); snd_usb_route(!snd_usb_routed()); toast(snd_usb_routed()?"Sound and microphone use the USB device":"Sound and microphone use the computer"); return 0; }
    if(id>=3775&&id<=3777){ static const u32 R[3]={44100,48000,96000}; extern void uac_out_set_rate_pref(u32); extern void ausnd_out_restart(void); uac_out_set_rate_pref(R[id-3775]); ausnd_out_restart(); return 0; }
    if(id==3750){ extern int uprn_scan(void); uprn_scan(); return 0; }
    if(id>=3751&&id<=3753)return id;
    if(id>=3760&&id<=3765){ int f=id==3760?SYSF_VOLFILES:id==3761?SYSF_SD_ADMA:id==3762?SYSF_EXFAT:id==3763?SYSF_VOLWRITE:id==3764?SYSF_V4L2:SYSF_USBSER; int on=sysfeat_state(f)==SYSF_ST_ON; if(!on&&!ux_need_admin(id))return 0; sysfeat_set(f,!on); toast(sysfeat_text(f)); return 0; }
    if(id==3771){ extern int s3_enable(int); extern const char* s3_status(void); int on=!s3_enable(-1); s3_enable(on); toast(on?"Deep sleep (S3) on - try it once with sleep and a key press":"Deep sleep (S3) off - light sleep is used"); return 0; }
    if(id==3774){ extern int ucsi_enabled(void); extern void ucsi_set_enabled(int); extern const char* ucsi_status(void); extern void ucsi_tick(int); ucsi_set_enabled(!ucsi_enabled()); ucsi_tick(1); toast(ucsi_status()); return 0; }
    if(id==3770){ extern int sleep_enabled(void); extern void sleep_set_enabled(int); extern const char* sleep_status_text(void); sleep_set_enabled(!sleep_enabled()); toast(sleep_status_text()); return 0; }
    if((id>=3600&&id<3680)||id==3700||id==3701){ if(!ux_need_admin(id))return 0; return id; }
    if(id==3704||(id>=3730&&id<3740))return id;
    return 0; }
static void ux_cron_install(const char* body,int len){ char pth[128]; const char* h=acct_home_of_current(); char* q=scopy(pth,h&&h[0]?h:"/tmp"); q=scopy(q,"/.settings-crontab"); *q=0;
    char ln[200]; if(len<=0){ q=scopy(ln,"crontab -r"); *q=0; } else { if(ux_write_text(pth,body,len,0100644u)<0){ toast("Could not write the task list"); return; } q=scopy(ln,"crontab "); q=scopy(q,pth); *q=0; }
    sh_run_line(ln); ux_cronn=-2; toast("Saved - the list updates in a moment"); }
static void st_ux_heavy(int id){
    if(id==3709){ id=ux_pw_submit(); if(!id)return; }
    if(id>=3780&&id<3800){ secui_heavy(id); return; }
    if(id>=3751&&id<=3753){ extern int prn_set(const char*); extern int prn_test_page(void); extern int escl_scan(int,int); extern char g_prn_status[]; extern char g_scan_status[]; extern int g_prn_usb;
        if(id==3753||(id==3751&&!g_prn_usb))prn_set("usb"); if(id==3751&&g_prn_usb)prn_test_page(); if(id==3752)escl_scan(300,1); toast(id==3752?g_scan_status:g_prn_status); return; }
    if(id>=3760&&id<=3765){ int f=id==3760?SYSF_VOLFILES:id==3761?SYSF_SD_ADMA:id==3762?SYSF_EXFAT:id==3763?SYSF_VOLWRITE:id==3764?SYSF_V4L2:SYSF_USBSER; sysfeat_set(f,1); toast(sysfeat_text(f)); return; }
    if(id==3704){ if(!ux_cmdn){ toast("Type a command first"); return; } ux_cron_sync(); static char nb[2200]; char* q=nb; int n=ux_cronn>0?ux_cronn:0; if(n>2000){ toast("Too many tasks"); return; } for(int i=0;i<n;i++){ *q++=ux_cron[i]; } if(n&&q[-1]!='\n')*q++='\n';
        q=udec(q,(u32)ux_cm); *q++=' '; q=udec(q,(u32)ux_ch); q=scopy(q," * * * "); ux_cmd[ux_cmdn]=0; q=scopy(q,ux_cmd); *q++='\n'; *q=0; ux_cron_install(nb,(int)(q-nb)); ux_cmdn=0; return; }
    if(id>=3730&&id<3740){ ux_cron_sync(); const char* s; int l; if(!ux_cron_line(id-3730,&s,&l))return; static char nb[2200]; char* q=nb; int any=0; for(const char* p=ux_cron;*p;p++){ if(p==s){ p+=l; if(!*p)break; continue; } *q++=*p; } *q=0;
        for(char* t=nb;*t;t++) if(*t!=' '&&*t!='\n'&&*t!='\t'){ any=1; break; } ux_cron_install(nb,any?(int)(q-nb):0); return; }
    ux_run_priv(id); }
static void ux_run_priv(int id){
    int save=g_uid; g_uid=0; int r=0; char ln[40];
    if(id==3700){ scopy(ln,"sshd on")[0]=0; sh_run_line(ln); }
    else if(id==3701){ scopy(ln,"sshd off")[0]=0; sh_run_line(ln); }
    else { int i=(id-3600)%20, k=(id-3600)/20; if(i<ux_sc){ const char* nm=ux_sn[i]; r=k==0?svcu_start(nm):k==1?svcu_stop(nm):k==2?svcu_restart(nm):svcu_enable(nm,!ux_se[i]);
        toast(r==-1?"No such service":r<0?"The service did not start - see its log":k==0?"Started":k==1?"Stopping":k==2?"Restarted":ux_se[i]?"Disabled":"Enabled"); } }
    g_uid=save; ux_sat=0; }
