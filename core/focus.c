#include "include/nvx/base.h"
#include "include/drivers.h"
#include "include/nvx/lib/base/fmt.h"
#include "include/nvx/fs/vfs/ramfs.h"
#include "include/nvx/fs/vfs/dirtree.h"
#include "include/nvx/fs/vfs/unixfs.h"
#include "include/nvx/core/sound.h"
#include "include/nvx/core/klog.h"
#include "include/nvx/core/registry.h"
#include "include/nvx/core/focus.h"
#include "include/nvx/graphics/ui/wm.h"
#include "include/nvx/graphics/ui/desktop.h"
#include "include/nvx/drivers/acpi/acpi_dev.h"
#include "include/nvx/arch/x86_64/cpu/percpu.h"
#include "include/nvx/drivers/rtc/cmos.h"

extern int g_battsave, g_refresh_hz, g_logged_in;
void snd_apply_volume(int v);
extern volatile u64 g_ticks;

static focus_mode FM[FOCUS_MAX];
static int fm_n=0;
int g_focus_cur=-1;
static int f_why=0, f_app=-1, f_saved=0, f_sch_skip=-1, f_lb_done=0, f_booted=0;
static int p_nl, p_bl, p_bs, p_hz, p_vol; static char p_theme[8], p_acc[8];
static u64 f_next=0;

#define FDIR "/etc/noovex/focus"
static const char* const ICN[10]={"Moon","Bed","Briefcase","Gamepad","Star","Heart","Book","Leaf","Car","Music"};
const char* focus_icon_name(int k){ return (k>=0&&k<10)?ICN[k]:"Moon"; }

static int f_lc(int c){ return (c>='A'&&c<='Z')?c+32:c; }
static int f_ieq(const char* a,const char* b){ while(*a&&*b&&f_lc((u8)*a)==f_lc((u8)*b)){ a++; b++; } return !*a&&!*b; }
static void f_cpy(char* d,const char* s,int cap){ int i=0; while(s&&s[i]&&i<cap-1){ d[i]=s[i]; i++; } d[i]=0; }
static int f_atoi(const char* s){ int n=0,neg=0; if(*s=='-'){ neg=1; s++; } while(*s>='0'&&*s<='9')n=n*10+(*s++-'0'); return neg?-n:n; }
static u32 f_hex(const char* s){ u32 v=0; for(int i=0;i<8&&s[i];i++){ char c=s[i]; int d=c>='0'&&c<='9'?c-'0':c>='a'&&c<='f'?c-'a'+10:c>='A'&&c<='F'?c-'A'+10:-1; if(d<0)break; v=v*16+(u32)d; } return v; }

static focus_mode* f_new(const char* id,const char* name,int icon,u32 col,int builtin){
    if(fm_n>=FOCUS_MAX)return 0; focus_mode* f=&FM[fm_n++]; for(int i=0;i<(int)sizeof(*f);i++)((u8*)f)[i]=0;
    f_cpy(f->id,id,16); f_cpy(f->name,name,24); f->icon=icon; f->color=col; f->builtin=builtin; f->used=1;
    f->nl=-1; f->bright=-1; f->bsave=-1; f->show_tb=1; f->show_lock=1; f->sch_from=22*60; f->sch_to=7*60; f->sch_days=127; return f; }

static void f_defaults(void){
    fm_n=0; focus_mode* f;
    f=f_new("dnd","Do not disturb",0,0x6C5CE7,1); f->hide_popups=1; f->mute=1;
    f=f_new("sleep","Sleep",1,0x3B5BDB,1); f->hide_popups=1; f->mute=1; f->nl=1; f->bright=30; f->lock1=1;
    f=f_new("work","Work",2,0x2F9E44,1); f->hide_popups=1; f->hide_games=1; f_cpy(f->allow,"System,Wi-Fi,Calendar,Clock,Writer",96); f->sch_from=8*60; f->sch_to=17*60; f->sch_days=31;
    f=f_new("gaming","Gaming",3,0xE8590C,1); f->hide_popups=1; f->bsave=0; f->app_mask=0;
}

int focus_count(void){ return fm_n; }
focus_mode* focus_get(int i){ return (i>=0&&i<fm_n)?&FM[i]:0; }
int focus_why(void){ return f_why; }
int focus_find(const char* s){ if(!s||!s[0])return -1; for(int i=0;i<fm_n;i++) if(f_ieq(FM[i].id,s)||f_ieq(FM[i].name,s))return i;
    if(f_ieq(s,"do-not-disturb")||f_ieq(s,"donotdisturb"))return focus_find("dnd"); if(f_ieq(s,"game")||f_ieq(s,"games"))return focus_find("gaming"); return -1; }

static char* f_kv(char* p,const char* k,const char* v){ p=scopy(p,k); *p++='='; p=scopy(p,v); *p++='\n'; *p=0; return p; }
static char* f_kn(char* p,const char* k,int v){ char t[16],*q=t; if(v<0){ *q++='-'; v=-v; } q=udec(q,(u32)v); *q=0; return f_kv(p,k,t); }
static char* f_kx(char* p,const char* k,u32 v){ char t[12],*q=t; q=hx(q,v,8); *q=0; return f_kv(p,k,t); }
static int f_text(const focus_mode* f,char* b){ char* p=b; p=scopy(p,"NVFOCUS1\n");
    p=f_kv(p,"Name",f->name); p=f_kn(p,"Icon",f->icon); p=f_kx(p,"Color",f->color); p=f_kn(p,"Builtin",f->builtin);
    p=f_kn(p,"HidePopups",f->hide_popups); p=f_kv(p,"AllowApps",f->allow); p=f_kn(p,"Mute",f->mute); p=f_kn(p,"HideGames",f->hide_games);
    p=f_kn(p,"NightLight",f->nl); p=f_kn(p,"Brightness",f->bright); p=f_kn(p,"BatterySaver",f->bsave); p=f_kv(p,"Theme",f->theme); p=f_kn(p,"LockAfter1Min",f->lock1);
    p=f_kn(p,"Schedule",f->sch_on); p=f_kn(p,"From",f->sch_from); p=f_kn(p,"To",f->sch_to); p=f_kn(p,"Days",f->sch_days); p=f_kx(p,"Apps",f->app_mask);
    p=f_kn(p,"SignIn",f->on_login); p=f_kn(p,"LowBattery",f->low_batt); p=f_kn(p,"Taskbar",f->show_tb); p=f_kn(p,"LockScreen",f->show_lock); return (int)(p-b); }
static void f_parse(focus_mode* f,const char* b,int n){
    int i=0; while(i<n){ int e=i; while(e<n&&b[e]!='\n')e++; int q=i; while(q<e&&b[q]!='=')q++;
        if(q<e){ char k[24],v[100]; int kl=q-i; if(kl>23)kl=23; for(int c=0;c<kl;c++)k[c]=b[i+c]; k[kl]=0; int vl=e-q-1; if(vl>99)vl=99; for(int c=0;c<vl;c++)v[c]=b[q+1+c]; v[vl]=0;
            if(seq(k,"Name")&&v[0])f_cpy(f->name,v,24); else if(seq(k,"Icon"))f->icon=((f_atoi(v)%10)+10)%10; else if(seq(k,"Color"))f->color=f_hex(v)&0xFFFFFF;
            else if(seq(k,"HidePopups"))f->hide_popups=f_atoi(v)!=0; else if(seq(k,"AllowApps"))f_cpy(f->allow,v,96); else if(seq(k,"Mute"))f->mute=f_atoi(v)!=0; else if(seq(k,"HideGames"))f->hide_games=f_atoi(v)!=0;
            else if(seq(k,"NightLight")){ int x=f_atoi(v); f->nl=x<-1?-1:x>1?1:x; } else if(seq(k,"Brightness")){ int x=f_atoi(v); f->bright=x<0?-1:x>100?100:x; } else if(seq(k,"BatterySaver")){ int x=f_atoi(v); f->bsave=x<-1?-1:x>1?1:x; }
            else if(seq(k,"Theme"))f_cpy(f->theme,v,16); else if(seq(k,"LockAfter1Min"))f->lock1=f_atoi(v)!=0; else if(seq(k,"Schedule"))f->sch_on=f_atoi(v)!=0;
            else if(seq(k,"From")){ int x=f_atoi(v); f->sch_from=x<0?0:x%1440; } else if(seq(k,"To")){ int x=f_atoi(v); f->sch_to=x<0?0:x%1440; } else if(seq(k,"Days"))f->sch_days=f_atoi(v)&127;
            else if(seq(k,"Apps"))f->app_mask=f_hex(v)&((1u<<NWIN)-1u); else if(seq(k,"SignIn"))f->on_login=f_atoi(v)!=0; else if(seq(k,"LowBattery"))f->low_batt=f_atoi(v)!=0;
            else if(seq(k,"Taskbar"))f->show_tb=f_atoi(v)!=0; else if(seq(k,"LockScreen"))f->show_lock=f_atoi(v)!=0; }
        i=e+1; } }

static void f_mkdir(const char* parent,const char* name,unsigned mode){ dt_init(); int d=dt_resolve_dir(parent); if(d<0)return; int c=dt_child(d,name); if(c<0)c=dt_mkdir_at(d,name); if(c>=0){ DT[c].uid=0; DT[c].gid=0; DT[c].mode=mode; } }
static void f_write(const char* path,const char* b,int n){ ux_write_text(path,b,n,0100644u); int isd=0; int k=ux_lookup(path,&isd,0); if(k>=0&&!isd){ FS[k].uid=0; FS[k].gid=0; FS[k].mode=0100644u; } }
static void f_path(char* o,const char* dir,const char* id){ char* p=o; p=scopy(p,dir); *p++='/'; p=scopy(p,id); p=scopy(p,".focus"); *p=0; }

static void f_mirror(void){
    f_mkdir("/System","Focus",0040755u);
    static char b[1024]; char path[80];
    for(int i=0;i<fm_n;i++){ int n=f_text(&FM[i],b); f_path(path,"/System/Focus",FM[i].id); f_write(path,b,n); }
    char* p=b; p=scopy(p,"Focus modes\n\nCurrent: "); p=scopy(p,g_focus_cur>=0?FM[g_focus_cur].name:"Off"); p=scopy(p,"\n\n");
    for(int i=0;i<fm_n;i++){ p=scopy(p,"  "); p=scopy(p,FM[i].id); p=scopy(p,"  "); p=scopy(p,FM[i].name); if(i==g_focus_cur)p=scopy(p,"  (on)"); *p++='\n'; }
    p=scopy(p,"\nSettings live in " FDIR "/ (one .focus file each).\nCommand: focus list | focus <name> | focus off\n"); f_write("/System/Focus/README.txt",b,(int)(p-b)); }

void focus_save(int i){ if(i<0||i>=fm_n)return; int su=g_uid; g_uid=0; f_mkdir("/etc","noovex",0040755u); f_mkdir("/etc/noovex","focus",0040755u);
    static char b[1024]; char path[80]; int n=f_text(&FM[i],b); f_path(path,FDIR,FM[i].id); f_write(path,b,n); f_mirror(); dt_save(); g_uid=su; }

void focus_boot(void){
    f_defaults(); int su=g_uid; g_uid=0;
    static char b[1024]; char name[64]; int type=0;
    for(int n=0;n<64&&k_readdir_at(FDIR,n,name,&type)>0;n++){ int L=0; while(name[L])L++; if(type==4||L<7||!seq(name+L-6,".focus"))continue; name[L-6]=0;
        char path[96]; f_path(path,FDIR,name); int len=ux_read_file(path,b,(int)sizeof(b)-1); if(len<=0)continue; b[len]=0; if(len<8||b[0]!='N')continue;
        int k=focus_find(name); focus_mode* f=0; if(k>=0&&f_ieq(FM[k].id,name))f=&FM[k]; else f=f_new(name,name,4,0x868E96,0);
        if(f){ int bi=f->builtin; f_parse(f,b,len); f->builtin=bi; } }
    f_mkdir("/etc","noovex",0040755u); f_mkdir("/etc/noovex","focus",0040755u);
    for(int i=0;i<fm_n;i++){ char path[96]; f_path(path,FDIR,FM[i].id); int isd=0; if(ux_lookup(path,&isd,0)<0){ int n=f_text(&FM[i],b); f_write(path,b,n); } }
    f_mirror(); g_uid=su; f_booted=1; }

static void f_theme_read(const char* id,int* wall,int* acc){ *wall=-1; *acc=-1; if(!id||!id[0])return; char path[64],*p=path; p=scopy(p,"/System/Themes/"); p=scopy(p,id); p=scopy(p,".nvw"); *p=0;
    char b[400]; int n=ux_read_file(path,b,(int)sizeof(b)-1); if(n<=0)return; b[n]=0;
    for(int i=0;i<n;i++){ if(i==0||b[i-1]=='\n'){ if(b[i]=='W'&&b[i+1]=='a'&&n-i>10&&b[i+9]=='=')*wall=f_atoi(b+i+10); if(b[i]=='A'&&b[i+1]=='c'&&n-i>7&&b[i+6]=='=')*acc=f_atoi(b+i+7); } } }
static void f_theme_set(const char* wall,const char* acc){ int su=g_uid; g_uid=0; if(wall[0])reg_set("System\\Display","Theme",wall); if(acc[0])reg_set("System\\Display","Accent",acc); g_uid=su; g_wallmode=0; build_bg(); }
static void f_reg(const char* n,const char* v){ int su=g_uid; g_uid=0; if(v)reg_set("System\\Focus",n,v); else reg_delete("System\\Focus",n); g_uid=su; }

int g_lock_override=0; static int p_wall, s_bl, s_nl, s_bs, s_hz; static char s_th[8], s_ac[8];
static void f_apply(focus_mode* f){
    p_nl=g_nightlight; p_bl=g_bl.ok?g_bl.cur:-1; p_bs=g_battsave; p_hz=g_refresh_hz; p_vol=g_snd_vol; p_wall=g_wallmode; p_theme[0]=0; p_acc[0]=0; f_saved=1;
    if(f->nl>=0)g_nightlight=f->nl; s_nl=g_nightlight;
    if(f->bright>=0&&g_bl.ok)bl_set(f->bright<5?5:f->bright); s_bl=g_bl.ok?g_bl.cur:-1;
    if(f->bsave>=0){ g_battsave=f->bsave; g_refresh_hz=g_battsave?30:(p_bs?60:p_hz); } s_bs=g_battsave; s_hz=g_refresh_hz; s_th[0]=0; s_ac[0]=0;
    g_lock_override=f->lock1?1:0;
    if(f->mute){ g_snd_vol=0; snd_apply_volume(0); }
    if(f->theme[0]){ int w,a; f_theme_read(f->theme,&w,&a); if(w>=0||a>=0){
        if(reg_get("System\\Focus","PrevTheme",p_theme,8)<=0||!p_theme[0]){ reg_get("System\\Display","Theme",p_theme,8); reg_get("System\\Display","Accent",p_acc,8); f_reg("PrevTheme",p_theme[0]?p_theme:"-"); f_reg("PrevAccent",p_acc[0]?p_acc:"-"); }
        else { reg_get("System\\Focus","PrevAccent",p_acc,8); }
        if(p_theme[0]=='-')p_theme[0]=0; if(p_acc[0]=='-')p_acc[0]=0;
        char ws[8]="",as[8]="",*q; if(w>=0){ q=ws; q=udec(q,(u32)w); *q=0; } if(a>=0){ q=as; q=udec(q,(u32)a); *q=0; } f_theme_set(ws,as); reg_get("System\\Display","Theme",s_th,8); reg_get("System\\Display","Accent",s_ac,8); } }
    g_dnd=f->hide_popups; }
static void f_restore(focus_mode* f){
    if(!f_saved)return; f_saved=0;
    if(f->nl>=0&&g_nightlight==s_nl)g_nightlight=p_nl;
    if(f->bright>=0&&g_bl.ok&&p_bl>=0&&g_bl.cur==s_bl)bl_set(p_bl);
    if(f->bsave>=0&&g_battsave==s_bs&&g_refresh_hz==s_hz){ g_battsave=p_bs; g_refresh_hz=p_hz; }
    g_lock_override=0;
    if(f->mute&&g_snd_vol==0){ g_snd_vol=p_vol; snd_apply_volume(p_vol); }
    if(p_theme[0]||p_acc[0]){ char ct[8]="",ca[8]=""; reg_get("System\\Display","Theme",ct,8); reg_get("System\\Display","Accent",ca,8); if(seq(ct,s_th)&&seq(ca,s_ac)){ f_theme_set(p_theme,p_acc); g_wallmode=p_wall; build_bg(); } }
    if(f->theme[0]){ f_reg("PrevTheme",0); f_reg("PrevAccent",0); }
    g_dnd=0; }

int focus_set(int i,int why){
    if(i>=fm_n)return -1; if(i==g_focus_cur){ if(i>=0)f_why=why; return 0; }
    if(g_focus_cur>=0){ focus_mode* o=&FM[g_focus_cur]; if(o->sch_on&&f_why==1&&why==0&&i<0)f_sch_skip=g_focus_cur; f_restore(o); }
    g_focus_cur=i; f_why=why; if(why!=2)f_app=-1;
    if(i>=0){ f_apply(&FM[i]); char t[48],*p=t; p=scopy(p,FM[i].name); p=scopy(p," is on"); *p=0; toast(t); }
    else toast("Focus is off");
    if(g_logged_in)reg_set("User\\Focus","Current",(i>=0&&why==0)?FM[i].id:"off");
    { int su=g_uid; g_uid=0; if(f_booted)f_mirror(); g_uid=su; }
    klog(i>=0?"focus: on":"focus: off"); return 0; }

int focus_popups_hidden(void){ return g_focus_cur>=0&&FM[g_focus_cur].hide_popups; }
int focus_hides_games(void){ return g_focus_cur>=0&&FM[g_focus_cur].hide_games; }
int focus_allows(const char* app,const char* title){
    if(!focus_popups_hidden())return 1; const char* a=FM[g_focus_cur].allow; int i=0;
    while(a[i]){ char w[32]; int n=0; while(a[i]==' ')i++; while(a[i]&&a[i]!=','){ if(n<31)w[n++]=a[i]; i++; } while(n&&w[n-1]==' ')n--; w[n]=0; if(a[i]==',')i++;
        if(n&&((app&&f_ieq(app,w))||(title&&f_ieq(title,w))))return 1; }
    return 0; }

int focus_create(const char* name,int icon,u32 color){
    if(!name||!name[0])return -1; if(focus_find(name)>=0)return -2;
    char id[16]; int n=0; for(int i=0;name[i]&&n<12;i++){ int c=f_lc((u8)name[i]); if((c>='a'&&c<='z')||(c>='0'&&c<='9'))id[n++]=(char)c; } if(!n)id[n++]='f'; id[n]=0;
    for(int s=2;focus_find(id)>=0&&s<10;s++){ id[n<14?n:14]=(char)('0'+s); id[(n<14?n:14)+1]=0; }
    focus_mode* f=f_new(id,name,((icon%10)+10)%10,color&0xFFFFFF,0); if(!f)return -3; f->hide_popups=1; focus_save(fm_n-1); return fm_n-1; }

int focus_delete(int i){ if(i<0||i>=fm_n||FM[i].builtin)return -1; if(g_focus_cur==i)focus_set(-1,0);
    int su=g_uid; g_uid=0; char path[80]; f_path(path,FDIR,FM[i].id); k_unlink(path); f_path(path,"/System/Focus",FM[i].id); k_unlink(path); g_uid=su;
    for(int k=i;k<fm_n-1;k++)FM[k]=FM[k+1]; fm_n--; if(g_focus_cur>i)g_focus_cur--; if(f_sch_skip==i)f_sch_skip=-1; else if(f_sch_skip>i)f_sch_skip--;
    su=g_uid; g_uid=0; f_mirror(); dt_save(); g_uid=su; return 0; }

void focus_app_started(int w){ if(w<0||w>=NWIN)return; if(g_focus_cur>=0&&f_why!=2&&f_why!=1)return;
    for(int i=0;i<fm_n;i++) if(FM[i].app_mask&(1u<<w)){ if(g_focus_cur==i&&f_why!=2)return; if(g_focus_cur!=i)focus_set(i,2); f_why=2; f_app=w; return; } }
void focus_app_closed(int w){ if(g_focus_cur<0||f_why!=2||w<0||w>=NWIN)return; u32 m=FM[g_focus_cur].app_mask; if(!(m&(1u<<w)))return;
    for(int k=0;k<NWIN;k++) if(k!=w&&(m&(1u<<k))&&WN[k].open){ f_app=k; return; } focus_set(-1,2);
    for(int k=0;k<NWIN;k++) if(k!=w&&WN[k].open){ focus_app_started(k); if(g_focus_cur>=0)return; } }

void focus_signed_in(void){
    char v[20]; v[0]=0; if(reg_get("User\\Focus","Current",v,20)>=0&&v[0]&&!seq(v,"off")){ int k=focus_find(v); if(k>=0){ focus_set(k,0); return; } }
    for(int i=0;i<fm_n;i++) if(FM[i].on_login){ focus_set(i,3); return; }
    if(g_focus_cur<0||!FM[g_focus_cur].theme[0]){ char pt[8]="",pa[8]=""; if(reg_get("System\\Focus","PrevTheme",pt,8)>0&&pt[0]){ reg_get("System\\Focus","PrevAccent",pa,8); if(pt[0]=='-')pt[0]=0; if(pa[0]=='-')pa[0]=0; if(pt[0]||pa[0])f_theme_set(pt,pa); f_reg("PrevTheme",0); f_reg("PrevAccent",0); } } }

void focus_logout(void){ f_sch_skip=-1; if(g_focus_cur>=0){ f_restore(&FM[g_focus_cur]); g_focus_cur=-1; f_why=0; f_app=-1; } }

static int f_wday(int y,int m,int d){ static const int T[12]={0,3,2,5,0,3,5,1,4,6,2,4}; if(m<1||m>12)m=1; if(m<3)y--; int w=(y+y/4-y/100+y/400+T[m-1]+d)%7; return (w+6)%7; }
static int f_in(const focus_mode* f,int mins,int wd){
    int a=f->sch_from, b=f->sch_to; if(a==b)return (f->sch_days>>wd)&1;
    if(a<b)return mins>=a&&mins<b&&((f->sch_days>>wd)&1);
    if(mins>=a)return (f->sch_days>>wd)&1;
    if(mins<b)return (f->sch_days>>((wd+6)%7))&1;
    return 0; }

void focus_tick(void){
    if(!f_booted||g_ticks<f_next)return; f_next=g_ticks+500;
    int hh,mm,ss,yy,mo,dd; read_clock(&hh,&mm,&ss,&yy,&mo,&dd); (void)ss; int mins=hh*60+mm, wd=f_wday(yy,mo,dd);
    if(f_sch_skip>=0&&f_sch_skip<fm_n&&!f_in(&FM[f_sch_skip],mins,wd))f_sch_skip=-1;
    if(g_focus_cur>=0&&f_why==1&&!f_in(&FM[g_focus_cur],mins,wd))focus_set(-1,1);
    if(g_focus_cur<0||f_why==1) for(int i=0;i<fm_n;i++) if(FM[i].sch_on&&i!=f_sch_skip&&f_in(&FM[i],mins,wd)){ if(g_focus_cur!=i)focus_set(i,1); break; }
    if(bat_present){ if(bat_charging||bat_pct>25)f_lb_done=0;
        else if(bat_pct<=20&&!f_lb_done){ f_lb_done=1; if(g_focus_cur<0) for(int i=0;i<fm_n;i++) if(FM[i].low_batt){ focus_set(i,4); break; } } } }

typedef struct { u32 m; const char* n; } fpreset;
static const fpreset FP[]={ {0,"None"}, {(1u<<W_DOOM)|(1u<<W_QUAKE),"DOOM or Quake"}, {(1u<<W_DOOM)|(1u<<W_QUAKE)|(1u<<W_SNAKE)|(1u<<W_FLAPPY),"Any game"},
    {1u<<W_DOOM,"DOOM"}, {1u<<W_QUAKE,"Quake"}, {1u<<W_VIDEO,"Video player"}, {1u<<W_WRITER,"Writer"}, {1u<<W_BROWSER,"Browser"}, {1u<<W_TERM,"Terminal"}, {1u<<W_CAMERA,"Camera"} };
#define FPN ((int)(sizeof(FP)/sizeof(FP[0])))
const char* focus_app_preset_name(u32 mask){ for(int i=0;i<FPN;i++) if(FP[i].m==mask)return FP[i].n; return "Custom apps"; }
u32 focus_app_preset_next(u32 mask){ for(int i=0;i<FPN;i++) if(FP[i].m==mask)return FP[(i+1)%FPN].m; return 0; }
static const char* const THN[7]={"","dark","night","ocean","forest","sunset","neon"};
const char* focus_theme_next(const char* cur){ for(int i=0;i<7;i++) if(seq(THN[i],cur?cur:""))return THN[(i+1)%7]; return ""; }
