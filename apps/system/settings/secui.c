#include "include/nvx/net/stack/firewall.h"
#include "include/nvx/lib/crypto/sha512crypt.h"
static int ux_need_admin(int id);
static int secui_page(int cy){
    int X1=st_x0+st_w0-24; char b[160],c[160],*q; int y; static int st=0, wk=0; static u64 at=0; extern volatile u64 g_ticks;
    if(!at||g_ticks-at>200){ at=g_ticks; acct_hash_stats(&st,&wk); }
    y=st_card(cy,64*3,"Passwords and sign-in");
    q=b; q=scopy(q,"Passwords are stored as SHA-512 crypt, "); q=udec(q,SHA512_CRYPT_ROUNDS); q=scopy(q," rounds"); *q=0;
    q=c; if(wk){ q=udec(q,(u32)wk); q=scopy(q,wk==1?" older password is":" older passwords are"); q=scopy(q," upgraded the next time its owner signs in"); } else q=scopy(q,"All passwords use the strong format"); *q=0;
    st_rowtext(y,b,c); st_sep(y+64);
    st_rowtext(y+64,"Wrong passwords slow down the next try","After 3 wrong tries: wait 5 s, doubling up to 5 min (sign-in, lock screen, sudo, su). SSH: longer pause each try, 6 tries per connection"); st_sep(y+128);
    q=b; q=scopy(q,"sudo remembers your password for "); q=udec(q,(u32)g_sudo_min); q=scopy(q," min"); *q=0;
    st_rowtext(y+128,b,"Separately in every terminal and console. sudo -k forgets it at once"); cy=y+64*3+24;
    { static const char* LM[4]={"5 min","15 min","30 min","Never"}; int sel=g_lock_min==5?0:g_lock_min==15?1:g_lock_min==30?2:g_lock_min<=0?3:-1;
      int w=st_segw(LM,4); const char* lt="Lock the screen when idle for"; int tw=pw_str(lt,1); int two=st_x0+20+tw+16>X1-w; int ch=two?104:64;
      y=st_card(cy,ch,"Screen lock"); st_rowtext_w(y,lt,sel<0?"A custom time is set":"Also on lid close and with Win+L",two?st_w0-40:st_w0-w-60); st_seg(X1-w,two?y+62:y+13,LM,4,sel,3780); cy=y+ch+24; }
    fw_load(); int nr=0; for(int i=0;i<FW_RULES&&nr<12;i++) if(g_fw_rule[i].used)nr++;
    y=st_card(cy,64+nr*40+64,"Firewall");
    q=c; q=scopy(q,g_fw_on?"Inbound connections are blocked except replies and the services below. Blocked so far: ":"Every inbound connection is allowed. Blocked so far: "); q=udec(q,g_fw_dropped); *q=0;
    st_rowtext(y,g_fw_on?"Firewall is on":"Firewall is off",c); st_toggle(X1-44,y+20,g_fw_on,3784);
    for(int i=0,k=0;i<FW_RULES&&k<12;i++){ struct fw_rule* r=&g_fw_rule[i]; if(!r->used)continue; int ry=y+64+k*40; st_sep(ry);
        q=b; q=scopy(q,r->proto==6?"TCP port ":"UDP port "); q=udec(q,r->port); if(r->svc[0]){ q=scopy(q,"  - only while "); q=scopy(q,r->svc); q=scopy(q," runs"); } *q=0; pdstr(st_x0+20,ry+13,b,ST_FG,1);
        const char* o=fw_rule_open(r)?"open":"closed"; pdstr(X1-110-pw_str(o,1),ry+13,o,fw_rule_open(r)?rgb(40,140,80):ST_DIM,1); st_btn(X1-90,ry+3,90,"Remove",3786+k,2); k++; }
    st_sep(y+64+nr*40); st_rowtext(y+64+nr*40,"Add a rule in a terminal","sudo firewall allow tcp 8080   (or udp; add a service name to open it only while that service runs)"); cy=y+64+nr*40+64+24;
    y=st_card(cy,64,"Programs with special rights");
    st_rowtext(y,"No setuid or setgid programs","NoovexOS never raises a program's rights through setuid or setgid bits, and packages are unpacked without them. Use sudo instead"); cy=y+64+24;
    return cy;
}
static int secui_rule_at(int k){ for(int i=0,n=0;i<FW_RULES;i++){ if(!g_fw_rule[i].used)continue; if(n==k)return i; n++; } return -1; }
static int secui_click(int id){
    if(id>=3780&&id<=3783){ static const int M[4]={5,15,30,0}; extern void power_prefs_save(void); g_lock_min=M[id-3780]; power_prefs_save(); toast(g_lock_min?"Screen lock time saved":"The screen no longer locks by itself"); return 0; }
    if(id==3784&&!g_fw_on){ toast(fw_set(1)<0?"Firewall on - could not save the setting":"Firewall on"); return 0; }
    if(id==3784||(id>=3786&&id<3798)){ if(!ux_need_admin(id))return 0; return id; }
    return 0;
}
static void secui_heavy(int id){
    if(id==3784){ toast(fw_set(!g_fw_on)<0?"Could not save the firewall setting":g_fw_on?"Firewall on":"Firewall off - every inbound connection is allowed"); return; }
    if(id>=3786&&id<3798){ int i=secui_rule_at(id-3786); if(i<0)return; struct fw_rule* r=&g_fw_rule[i]; toast(fw_deny(r->proto,r->port)<0?"Could not remove the rule":"Rule removed"); }
}
