#include "include/nvx/base.h"
#include "include/nvx/arch/x86_64/cpu/percpu.h"
#include "include/nvx/lib/base/fmt.h"
#include "include/nvx/drivers/video/fb/framebuffer.h"
#include "include/nvx/fs/vfs/ramfs.h"
#include "include/nvx/fs/vfs/dirtree.h"
#include "include/nvx/fs/vfs/unixfs.h"
#include "include/nvx/fs/nvx/nvx4.h"
#include "include/nvx/fs/nvx/nvxfs.h"
#include "include/nvx/sys/native/syscall.h"
#include "include/nvx/core/klog.h"
#include "include/nvx/core/buildsig.h"
#include "include/nvx/graphics/theme/theme.h"
#include "include/nvx/lib/crypto/sha512crypt.h"
#include "include/nvx/core/auth/accounts.h"

char g_fullname[64]="";
char g_home[96]="/home";
int g_home_dir=1;
int g_guest_on=0;
int g_lock_min=15;
int g_locked=0;
int g_logged_in=0;
int g_sudo_min=15;
char g_acct_err[96]="";

int ux_write_text(const char* path,const char* body,int len,unsigned mode);

#define ac_AP_N 160
typedef struct { char name[33]; char pw[8]; char gecos[96]; char home[96]; char shell[48]; int uid,gid; } ac_ap_t;
typedef struct { char name[33]; char pw[8]; int gid; char mem[384]; } ac_ag_t;
typedef struct { char name[33]; char hash[136]; char rest[96]; } ac_as_t;
static ac_ap_t ac_AP[ac_AP_N]; static int ac_g_np;
static ac_ag_t ac_AG[ac_AP_N]; static int ac_g_ng;
static ac_as_t ac_AS[ac_AP_N]; static int ac_g_ns;
static char ac_g_io[65536];

static int ac_al(const char* s){ int n=0; while(s[n])n++; return n; }
static void ac_acp(char* d,const char* s,int cap){ int i=0; for(;s[i]&&i<cap-1;i++)d[i]=s[i]; d[i]=0; }
static int ac_aeq(const char* a,const char* b){ while(*a&&*a==*b){ a++; b++; } return *a==*b; }
static int ac_aatoi(const char* s){ int v=0; while(*s>='0'&&*s<='9'){ v=v*10+(*s-'0'); s++; } return v; }
static void ac_aerr(const char* m){ ac_acp(g_acct_err,m,(int)sizeof(g_acct_err)); }

static int ac_af_read(const char* path,char* out,int cap){
    int isd=0; int k=ux_lookup(path,&isd,1); if(k<0||isd)return -1;
    int n=FS[k].len; if(n>cap-1)n=cap-1; const char* s=fsd(k); for(int i=0;i<n;i++)out[i]=s[i]; out[n]=0; return n;
}
static int ac_af_write(const char* path,const char* body,int len,unsigned mode){ return ux_write_text(path,body,len,mode)<0?-1:0; }

static int ac_fld(const char* l,int le,int idx,char* out,int cap){
    int f=0,o=0; for(int i=0;i<le;i++){ if(l[i]==':'){ f++; if(f>idx)break; continue; } if(f==idx&&o<cap-1)out[o++]=l[i]; }
    out[o]=0; return f>=idx;
}
static const char* ac_frest(const char* l,int le,int idx,int* rl){
    int f=0; for(int i=0;i<le;i++){ if(l[i]==':'){ f++; if(f==idx){ *rl=le-i-1; return l+i+1; } } }
    *rl=0; return l+le;
}

int acct_reload(void){
    ac_g_np=ac_g_ng=ac_g_ns=0;
    int n=ac_af_read("/etc/passwd",ac_g_io,(int)sizeof(ac_g_io));
    for(int i=0;n>0&&i<n&&ac_g_np<ac_AP_N;){ int j=i; while(j<n&&ac_g_io[j]!='\n')j++; const char* l=ac_g_io+i; int le=j-i;
        if(le>0&&l[0]!='#'){ ac_ap_t* p=&ac_AP[ac_g_np]; char t[16]; ac_fld(l,le,0,p->name,33); ac_fld(l,le,1,p->pw,8); ac_fld(l,le,2,t,16); p->uid=ac_aatoi(t); ac_fld(l,le,3,t,16); p->gid=ac_aatoi(t); ac_fld(l,le,4,p->gecos,96); ac_fld(l,le,5,p->home,96); ac_fld(l,le,6,p->shell,48); if(p->name[0])ac_g_np++; }
        i=j+1; }
    n=ac_af_read("/etc/group",ac_g_io,(int)sizeof(ac_g_io));
    for(int i=0;n>0&&i<n&&ac_g_ng<ac_AP_N;){ int j=i; while(j<n&&ac_g_io[j]!='\n')j++; const char* l=ac_g_io+i; int le=j-i;
        if(le>0&&l[0]!='#'){ ac_ag_t* g=&ac_AG[ac_g_ng]; char t[16]; ac_fld(l,le,0,g->name,33); ac_fld(l,le,1,g->pw,8); ac_fld(l,le,2,t,16); g->gid=ac_aatoi(t); ac_fld(l,le,3,g->mem,384); if(g->name[0])ac_g_ng++; }
        i=j+1; }
    n=ac_af_read("/etc/shadow",ac_g_io,(int)sizeof(ac_g_io));
    for(int i=0;n>0&&i<n&&ac_g_ns<ac_AP_N;){ int j=i; while(j<n&&ac_g_io[j]!='\n')j++; const char* l=ac_g_io+i; int le=j-i;
        if(le>0&&l[0]!='#'){ ac_as_t* s=&ac_AS[ac_g_ns]; ac_fld(l,le,0,s->name,33); ac_fld(l,le,1,s->hash,136); int rl; const char* r=ac_frest(l,le,2,&rl); int o=0; for(int q=0;q<rl&&o<95;q++)s->rest[o++]=r[q]; s->rest[o]=0; if(s->name[0])ac_g_ns++; }
        i=j+1; }
    return ac_g_np;
}

static char* ac_put_i(char* p,int v){ if(v<0){ *p++='-'; v=-v; } return udec(p,(u32)v); }
static int ac_db_save(int what){
    int rc=0;
    if(what&1){ char* p=ac_g_io; for(int i=0;i<ac_g_np;i++){ ac_ap_t* a=&ac_AP[i]; if(p-ac_g_io>(int)sizeof(ac_g_io)-320)break; p=scopy(p,a->name); *p++=':'; p=scopy(p,a->pw[0]?a->pw:"x"); *p++=':'; p=ac_put_i(p,a->uid); *p++=':'; p=ac_put_i(p,a->gid); *p++=':'; p=scopy(p,a->gecos); *p++=':'; p=scopy(p,a->home); *p++=':'; p=scopy(p,a->shell); *p++='\n'; }
        if(ac_af_write("/etc/passwd",ac_g_io,(int)(p-ac_g_io),0100644u))rc=-1; }
    if(what&2){ char* p=ac_g_io; for(int i=0;i<ac_g_ng;i++){ ac_ag_t* g=&ac_AG[i]; if(p-ac_g_io>(int)sizeof(ac_g_io)-480)break; p=scopy(p,g->name); *p++=':'; p=scopy(p,g->pw[0]?g->pw:"x"); *p++=':'; p=ac_put_i(p,g->gid); *p++=':'; p=scopy(p,g->mem); *p++='\n'; }
        if(ac_af_write("/etc/group",ac_g_io,(int)(p-ac_g_io),0100644u))rc=-1; }
    if(what&4){ char* p=ac_g_io; for(int i=0;i<ac_g_ns;i++){ ac_as_t* s=&ac_AS[i]; if(p-ac_g_io>(int)sizeof(ac_g_io)-300)break; p=scopy(p,s->name); *p++=':'; p=scopy(p,s->hash); *p++=':'; p=scopy(p,s->rest[0]?s->rest:"0:0:99999:7:::"); *p++='\n'; }
        if(ac_af_write("/etc/shadow",ac_g_io,(int)(p-ac_g_io),0100600u))rc=-1;
        { int isd=0; int k=ux_lookup("/etc/shadow",&isd,1); if(k>=0&&!isd){ FS[k].mode=0100600u; FS[k].uid=0; FS[k].gid=0; } } }
    return rc;
}

static int ac_fp(const char* name){ for(int i=0;i<ac_g_np;i++) if(ac_aeq(ac_AP[i].name,name))return i; return -1; }
static int ac_fpu(int uid){ for(int i=0;i<ac_g_np;i++) if(ac_AP[i].uid==uid)return i; return -1; }
static int ac_fg(const char* name){ for(int i=0;i<ac_g_ng;i++) if(ac_aeq(ac_AG[i].name,name))return i; return -1; }
static int ac_fgg(int gid){ for(int i=0;i<ac_g_ng;i++) if(ac_AG[i].gid==gid)return i; return -1; }
static int ac_fs_(const char* name){ for(int i=0;i<ac_g_ns;i++) if(ac_aeq(ac_AS[i].name,name))return i; return -1; }

static int ac_mem_has(const char* mem,const char* name){
    const char* m=mem; int nl=ac_al(name);
    while(*m){ const char* a=m; while(*m&&*m!=',')m++; int L=(int)(m-a); if(L==nl){ int eq=1; for(int i=0;i<L;i++) if(a[i]!=name[i]){ eq=0; break; } if(eq)return 1; } if(*m)m++; }
    return 0;
}
static void ac_mem_add(char* mem,const char* name){ if(ac_mem_has(mem,name))return; int n=ac_al(mem), nl=ac_al(name); if(n+nl+2>=384)return; if(n)mem[n++]=','; for(int i=0;i<nl;i++)mem[n++]=name[i]; mem[n]=0; }
static void ac_mem_del(char* mem,const char* name){
    char out[384]; int o=0; const char* m=mem; int nl=ac_al(name);
    while(*m){ const char* a=m; while(*m&&*m!=',')m++; int L=(int)(m-a); int eq=(L==nl); for(int i=0;eq&&i<L;i++) if(a[i]!=name[i])eq=0;
        if(!eq&&L>0){ if(o)out[o++]=','; for(int i=0;i<L&&o<382;i++)out[o++]=a[i]; } if(*m)m++; }
    out[o]=0; ac_acp(mem,out,384);
}
static void ac_mem_ren(char* mem,const char* from,const char* to){ if(!ac_mem_has(mem,from))return; ac_mem_del(mem,from); ac_mem_add(mem,to); }

static int ac_ensure_group(const char* name,int gid){
    int i=ac_fg(name); if(i>=0)return i;
    if(ac_g_ng>=ac_AP_N)return -1;
    ac_ag_t* g=&ac_AG[ac_g_ng]; ac_acp(g->name,name,33); ac_acp(g->pw,"x",8); g->gid=gid; g->mem[0]=0; return ac_g_ng++;
}
static u32 ac_days_now(void){ return ux_now()/86400u; }
static void ac_rest_now(char* rest){ char* p=rest; p=udec(p,ac_days_now()); p=scopy(p,":0:99999:7:::"); *p=0; }

int acct_valid_name(const char* n){
    if(!n||!n[0])return 0;
    int L=ac_al(n);
    if(L>32)return 0;
    if(!((n[0]>='a'&&n[0]<='z')||n[0]=='_'))return 0;
    for(int i=1;i<L;i++){ char c=n[i]; if(!((c>='a'&&c<='z')||(c>='0'&&c<='9')||c=='_'||c=='-'))return 0; }
    static const char* R[]={"root","daemon","bin","sys","adm","admin","sudo","users","nobody","guest","wheel","tty","disk","shadow","staff",0};
    for(int i=0;R[i];i++) if(ac_aeq(n,R[i]))return 0;
    return 1;
}
int acct_valid_host(const char* h){
    if(!h||!h[0])return 0;
    int L=ac_al(h);
    if(L>31)return 0;
    if(h[0]=='-'||h[L-1]=='-')return 0;
    for(int i=0;i<L;i++){ char c=h[i]; if(!((c>='a'&&c<='z')||(c>='A'&&c<='Z')||(c>='0'&&c<='9')||c=='-'))return 0; }
    return 1;
}
static int ac_clean_text(const char* s){ for(int i=0;s[i];i++) if(s[i]==':'||s[i]==','||s[i]=='\n'||s[i]=='\r')return 0; return 1; }

static void ac_fill_info(int i,acct_info* o){
    ac_ap_t* a=&ac_AP[i]; ac_acp(o->name,a->name,33);
    { int k=0; for(;a->gecos[k]&&a->gecos[k]!=','&&k<63;k++)o->full[k]=a->gecos[k]; o->full[k]=0; }
    ac_acp(o->home,a->home,96); ac_acp(o->shell,a->shell,48); o->uid=a->uid; o->gid=a->gid;
    o->admin=acct_is_admin_name(a->name); o->guest=ac_aeq(a->name,"guest");
    int s=ac_fs_(a->name); o->haspw=0; o->locked=1;
    if(s>=0){ const char* h=ac_AS[s].hash; o->locked=(h[0]=='!'||h[0]=='*'); o->haspw=(h[0]&&!o->locked); }
}
int acct_get(const char* name,acct_info* out){ acct_reload(); int i=ac_fp(name); if(i<0)return -1; ac_fill_info(i,out); return 0; }
int acct_get_uid(int uid,acct_info* out){ acct_reload(); int i=ac_fpu(uid); if(i<0)return -1; ac_fill_info(i,out); return 0; }
int acct_list(acct_info* out,int max,int loginable_only){
    acct_reload(); int n=0;
    for(int i=0;i<ac_g_np&&n<max;i++){ ac_ap_t* a=&ac_AP[i];
        if(loginable_only){ int g=ac_aeq(a->name,"guest"); if(g&&!g_guest_on)continue; if(!g&&(a->uid<1000||a->uid>=60000))continue; }
        ac_fill_info(i,&out[n++]); }
    if(loginable_only){ for(int i=0;i<n-1;i++) if(out[i].guest){ acct_info t=out[i]; for(int j=i;j<n-1;j++)out[j]=out[j+1]; out[n-1]=t; break; } }
    return n;
}

int acct_in_group(const char* name,const char* group){
    int g=ac_fg(group); if(g<0)return 0; if(ac_mem_has(ac_AG[g].mem,name))return 1;
    int p=ac_fp(name); return p>=0&&ac_AP[p].gid==ac_AG[g].gid;
}
int acct_is_admin_name(const char* name){
    if(ac_aeq(name,"root"))return 1;
    if(acct_in_group(name,"admin")||acct_in_group(name,"sudo"))return 1;
    if(ac_fg("admin")<0&&ac_fg("sudo")<0){ int p=ac_fp(name); return p>=0&&ac_AP[p].uid==1000; }
    return 0;
}
int acct_is_admin(int uid){
    if(uid==0)return 1;
    acct_reload();
    int p=ac_fpu(uid);
    if(p<0)return 0;
    return acct_is_admin_name(ac_AP[p].name);
}
int acct_admin_count(void){ acct_reload(); int c=0; for(int i=0;i<ac_g_np;i++) if(ac_AP[i].uid>=1000&&ac_AP[i].uid<60000&&acct_is_admin_name(ac_AP[i].name))c++; return c; }
int acct_group_gid(const char* group){ acct_reload(); int g=ac_fg(group); return g<0?-1:ac_AG[g].gid; }
int acct_groups(const char* name,char* out,int cap){
    acct_reload(); int o=0; out[0]=0; int p=ac_fp(name); if(p<0)return -1;
    int pg=ac_fgg(ac_AP[p].gid);
    if(pg>=0){ for(int k=0;ac_AG[pg].name[k]&&o<cap-2;k++)out[o++]=ac_AG[pg].name[k]; }
    for(int i=0;i<ac_g_ng;i++){ if(i==pg)continue; if(ac_mem_has(ac_AG[i].mem,name)){ if(o&&o<cap-2)out[o++]=' '; for(int k=0;ac_AG[i].name[k]&&o<cap-2;k++)out[o++]=ac_AG[i].name[k]; } }
    out[o]=0; return o;
}
int acct_group_add(const char* name,const char* group){
    acct_reload(); if(ac_fp(name)<0){ ac_aerr("no such user"); return -1; }
    int g=ac_fg(group); if(g<0){ ac_aerr("no such group"); return -1; }
    ac_mem_add(ac_AG[g].mem,name); return ac_db_save(2);
}
int acct_group_del(const char* name,const char* group){
    acct_reload(); int g=ac_fg(group); if(g<0){ ac_aerr("no such group"); return -1; }
    ac_mem_del(ac_AG[g].mem,name); return ac_db_save(2);
}

static int ac_legacy_hex_ok(const char* pw,const char* hex){
    unsigned h=nvx_pwhash(pw); char t[12]; int n=0; if(!h)t[n++]='0'; while(h){ t[n++]="0123456789abcdef"[h&15]; h>>=4; }
    char r[12]; int o=0; while(n)r[o++]=t[--n]; r[o]=0; return ac_aeq(r,hex);
}
int acct_check(const char* name,const char* pw){
    acct_reload(); int s=ac_fs_(name); if(s<0)return 0;
    const char* h=ac_AS[s].hash;
    if(h[0]=='!'||h[0]=='*')return 0;
    if(!h[0])return pw[0]==0;
    if(h[0]=='$'&&h[1]=='6'&&h[2]=='$')return sha512_crypt_check(pw,h);
    if(h[0]=='$'&&h[1]=='n'&&h[2]=='v'&&h[3]=='x'&&h[4]=='$'){ if(ac_legacy_hex_ok(pw,h+5)){ acct_set_password(name,pw); return 1; } return 0; }
    return 0;
}
int acct_has_password(const char* name){ acct_reload(); int s=ac_fs_(name); if(s<0)return 0; const char* h=ac_AS[s].hash; return h[0]&&h[0]!='!'&&h[0]!='*'; }

static int ac_set_hash(const char* name,const char* pw){
    int s=ac_fs_(name);
    if(s<0){ if(ac_g_ns>=ac_AP_N){ ac_aerr("too many accounts"); return -1; } s=ac_g_ns++; ac_acp(ac_AS[s].name,name,33); }
    if(!pw||!pw[0]){ ac_AS[s].hash[0]=0; }
    else { char salt[17]; sha512_crypt_salt(salt,16); char st[24]; char* p=st; p=scopy(p,"$6$"); p=scopy(p,salt); *p=0;
        if(sha512_crypt(pw,st,ac_AS[s].hash,136)<0){ ac_aerr("could not hash the password"); return -1; } }
    ac_rest_now(ac_AS[s].rest); return 0;
}
int acct_set_password(const char* name,const char* pw){
    acct_reload(); if(ac_fp(name)<0){ ac_aerr("no such user"); return -1; }
    if(ac_set_hash(name,pw))return -1;
    int rc=ac_db_save(4); acct_log("password changed for ",name,0); return rc;
}
int acct_lock_password(const char* name){
    acct_reload(); int s=ac_fs_(name); if(s<0){ if(ac_g_ns>=ac_AP_N)return -1; s=ac_g_ns++; ac_acp(ac_AS[s].name,name,33); }
    ac_acp(ac_AS[s].hash,"!",136); ac_rest_now(ac_AS[s].rest); return ac_db_save(4);
}

int acct_set_locked(const char* name,int on){
    acct_reload(); int s=ac_fs_(name); if(s<0){ ac_aerr("no such user"); return -1; }
    char* h=ac_AS[s].hash; int L=ac_al(h);
    if(on){ if(h[0]=='!')return 0; if(L>=134){ ac_aerr("hash too long"); return -1; } for(int i=L;i>=0;i--)h[i+1]=h[i]; h[0]='!'; }
    else { if(h[0]!='!')return 0; if(L<=1){ ac_aerr("this account has no password to unlock - set one with passwd"); return -1; } for(int i=0;i<L;i++)h[i]=h[i+1]; }
    int rc=ac_db_save(4); acct_log(on?"account locked: ":"account unlocked: ",name,0); return rc;
}
static int ac_mkdir_owned(int parent,const char* name,unsigned mode,int uid,int gid){
    int d=dt_mkdir_at(parent,name); if(d<0)return -1; DT[d].mode=0040000u|(mode&07777u); DT[d].uid=uid; DT[d].gid=gid; return d;
}
static int ac_mkdir_path(const char* path,unsigned mode,int uid,int gid){
    dt_init(); int cur=0; char part[64]; const char* p=path; if(*p=='/')p++;
    while(*p){ int n=0; while(*p&&*p!='/'&&n<63)part[n++]=*p++; part[n]=0; while(*p=='/')p++;
        if(!n)continue;
        int c=dt_child(cur,part); if(c<0){ c=dt_mkdir_at(cur,part); if(c<0)return -1; DT[c].mode=0040000u|(mode&07777u); DT[c].uid=uid; DT[c].gid=gid; } cur=c; }
    return cur;
}
static const char* ac_HOME_SUB[6]={"Desktop","Documents","Downloads","Pictures","Music","Videos"};
static int ac_make_home(const char* home,int uid,int gid,unsigned mode){
    int h=ac_mkdir_path(home,0755u,0,0); if(h<0)return -1;
    DT[h].mode=0040000u|(mode&07777u); DT[h].uid=uid; DT[h].gid=gid;
    for(int i=0;i<6;i++)ac_mkdir_owned(h,ac_HOME_SUB[i],0755u,uid,gid);
    dt_save(); return h;
}
static void ac_remove_tree(int h,int keep_root){
    if(h<=0)return;
    for(int k=0;k<FS_N;k++) if(FS[k].used&&FS[k].dir>=0&&k_is_ancestor(h,FS[k].dir))fs_drop_name(k);
    for(int pass=0;pass<64;pass++){ int any=0;
        for(int i=1;i<DN_MAX;i++){ if(!DT[i].used||i==h||!k_is_ancestor(h,i))continue; int leaf=1; for(int j=1;j<DN_MAX;j++) if(DT[j].used&&DT[j].parent==i){ leaf=0; break; } if(leaf){ DT[i].used=0; any=1; } }
        if(!any)break; }
    if(!keep_root)DT[h].used=0;
    dt_save(); nvx3_flush_meta();
}
static void ac_chown_tree(int h,int uid,int gid){
    for(int i=0;i<DN_MAX;i++) if(DT[i].used&&k_is_ancestor(h,i)){ DT[i].uid=uid; DT[i].gid=gid; }
    for(int k=0;k<FS_N;k++) if(FS[k].used&&FS[k].dir>=0&&k_is_ancestor(h,FS[k].dir)){ FS[k].uid=uid; FS[k].gid=gid; }
    dt_save(); nvx3_flush_meta();
}

static int ac_next_uid(void){ int u=1000; for(int i=0;i<ac_g_np;i++) if(ac_AP[i].uid>=u&&ac_AP[i].uid<60000)u=ac_AP[i].uid+1; for(int i=0;i<ac_g_ng;i++) if(ac_AG[i].gid==u)u++; return u; }

int acct_create(const char* name,const char* full,const char* pw,int admin){
    acct_reload(); g_acct_err[0]=0;
    if(!acct_valid_name(name)){ ac_aerr("user names start with a-z and use only a-z, 0-9, - and _ (at most 32)"); return -1; }
    if(ac_fp(name)>=0||ac_fg(name)>=0){ ac_aerr("that user name is already taken"); return -1; }
    if(full&&!ac_clean_text(full)){ ac_aerr("the full name cannot contain : or ,"); return -1; }
    if(pw&&pw[0]&&ac_al(pw)<4){ ac_aerr("the password needs at least 4 characters"); return -1; }
    if(ac_g_np>=ac_AP_N||ac_g_ng>=ac_AP_N-4){ ac_aerr("too many accounts"); return -1; }
    int uid=ac_next_uid();
    ac_ap_t* a=&ac_AP[ac_g_np++]; ac_acp(a->name,name,33); ac_acp(a->pw,"x",8); a->uid=uid; a->gid=uid; ac_acp(a->gecos,full?full:"",96);
    { char* p=a->home; p=scopy(p,"/home/"); p=scopy(p,name); *p=0; } ac_acp(a->shell,"/bin/nsh",48);
    { int g=ac_ensure_group(name,uid); if(g>=0)ac_AG[g].gid=uid; }
    { int g=ac_ensure_group("users",100); if(g>=0)ac_mem_add(ac_AG[g].mem,name); }
    if(admin){ int g=ac_ensure_group("admin",10); if(g>=0)ac_mem_add(ac_AG[g].mem,name); g=ac_ensure_group("sudo",27); if(g>=0)ac_mem_add(ac_AG[g].mem,name); }
    if(ac_set_hash(name,pw))return -1;
    if(ac_db_save(7)){ ac_aerr("could not write the account files"); return -1; }
    ac_make_home(a->home,uid,uid,0750u);
    acct_log("new user ",name,admin?" (administrator)":" (standard)");
    return uid;
}

int acct_delete(const char* name,int remove_home){
    acct_reload(); g_acct_err[0]=0;
    int p=ac_fp(name); if(p<0){ ac_aerr("no such user"); return -1; }
    if(ac_AP[p].uid==0){ ac_aerr("root cannot be removed"); return -1; }
    if(ac_aeq(name,g_user)&&g_logged_in){ ac_aerr("you cannot remove the account you are signed in with"); return -1; }
    if(acct_is_admin_name(name)&&acct_admin_count()<=1&&ac_AP[p].uid>=1000){ ac_aerr("this is the last administrator"); return -1; }
    char home[96]; ac_acp(home,ac_AP[p].home,96); int uid=ac_AP[p].uid;
    for(int i=p;i<ac_g_np-1;i++)ac_AP[i]=ac_AP[i+1];
    ac_g_np--;
    int s=ac_fs_(name); if(s>=0){ for(int i=s;i<ac_g_ns-1;i++)ac_AS[i]=ac_AS[i+1]; ac_g_ns--; }
    for(int i=0;i<ac_g_ng;i++)ac_mem_del(ac_AG[i].mem,name);
    { int g=ac_fg(name); if(g>=0&&ac_AG[g].gid==uid){ for(int i=g;i<ac_g_ng-1;i++)ac_AG[i]=ac_AG[i+1]; ac_g_ng--; } }
    if(ac_db_save(7)){ ac_aerr("could not write the account files"); return -1; }
    if(remove_home&&home[0]=='/'&&home[1]){ int h=dt_resolve_dir(home); int hm=dt_resolve_dir("/home"); if(h>0&&h!=hm&&h!=0)ac_remove_tree(h,0); }
    acct_log("user removed: ",name,remove_home?" (home folder deleted)":0);
    return 0;
}

int acct_set_admin(const char* name,int on){
    acct_reload(); g_acct_err[0]=0;
    int p=ac_fp(name); if(p<0){ ac_aerr("no such user"); return -1; }
    if(ac_AP[p].uid==0){ ac_aerr("root is always an administrator"); return -1; }
    if(!on&&acct_is_admin_name(name)&&acct_admin_count()<=1){ ac_aerr("there must be at least one administrator"); return -1; }
    int a=ac_ensure_group("admin",10), s=ac_ensure_group("sudo",27);
    if(on){ if(a>=0)ac_mem_add(ac_AG[a].mem,name); if(s>=0)ac_mem_add(ac_AG[s].mem,name); }
    else { if(a>=0)ac_mem_del(ac_AG[a].mem,name); if(s>=0)ac_mem_del(ac_AG[s].mem,name); }
    int rc=ac_db_save(2); acct_log(on?"administrator rights given to ":"administrator rights removed from ",name,0); return rc;
}
int acct_set_full(const char* name,const char* full){
    acct_reload(); int p=ac_fp(name); if(p<0){ ac_aerr("no such user"); return -1; }
    if(!ac_clean_text(full)){ ac_aerr("the full name cannot contain : or ,"); return -1; }
    ac_acp(ac_AP[p].gecos,full,96); int rc=ac_db_save(1);
    if(ac_aeq(name,g_user))ac_acp(g_fullname,full,64);
    return rc;
}

int acct_set_hostname(const char* host){
    if(!acct_valid_host(host)){ ac_aerr("computer names use letters, digits and - (at most 31)"); return -1; }
    char b[64]; char* p=b; p=scopy(p,host); *p++='\n'; *p=0; ac_af_write("/etc/hostname",b,(int)(p-b),0100644u);
    int o=0; for(;host[o]&&o<31;o++)g_hostname[o]=host[o]; g_hostname[o]=0;
    char h[256]; p=h; p=scopy(p,"127.0.0.1\tlocalhost\n127.0.1.1\t"); p=scopy(p,host); p=scopy(p,"\n::1\tlocalhost ip6-localhost ip6-loopback\n"); *p=0;
    ac_af_write("/etc/hosts",h,(int)(p-h),0100644u);
    return 0;
}

static void ac_move_home_contents(int from,int to){
    if(from<0||to<0||from==to)return;
    for(int k=0;k<FS_N;k++) if(FS[k].used&&FS[k].dir==from){ FS[k].dir=to; ux_touch(k,0); }
    for(int i=1;i<DN_MAX;i++) if(DT[i].used&&DT[i].parent==from&&i!=to){
        int keep=0; for(int q=0;q<ac_g_np;q++){ int h=dt_resolve_dir(ac_AP[q].home); if(h==i){ keep=1; break; } } if(keep)continue;
        int same=dt_child(to,DT[i].name); if(same>=0&&same!=i){ for(int k=0;k<FS_N;k++) if(FS[k].used&&FS[k].dir==i)FS[k].dir=same; for(int j=1;j<DN_MAX;j++) if(DT[j].used&&DT[j].parent==i)DT[j].parent=same; DT[i].used=0; }
        else DT[i].parent=to; }
    dt_save(); nvx3_flush_meta();
}

int acct_setup_primary(const char* name,const char* full,const char* pw,const char* rootpw,const char* host){
    acct_reload(); g_acct_err[0]=0;
    if(!acct_valid_name(name)){ ac_aerr("user names start with a-z and use only a-z, 0-9, - and _ (at most 32)"); return -1; }
    if(full&&!ac_clean_text(full)){ ac_aerr("the full name cannot contain : or ,"); return -1; }
    int p=ac_fpu(1000);
    if(p>=0&&!ac_aeq(ac_AP[p].name,name)&&ac_fp(name)>=0){ ac_aerr("that user name is already taken"); return -1; }
    if(p<0){ if(acct_create(name,full,pw,1)<0)return -1; acct_reload(); p=ac_fp(name); }
    else {
        char old[33]; ac_acp(old,ac_AP[p].name,33); char oldhome[96]; ac_acp(oldhome,ac_AP[p].home,96);
        ac_acp(ac_AP[p].name,name,33); ac_acp(ac_AP[p].gecos,full?full:"",96); { char* q=ac_AP[p].home; q=scopy(q,"/home/"); q=scopy(q,name); *q=0; } ac_AP[p].gid=1000; if(!ac_AP[p].shell[0])ac_acp(ac_AP[p].shell,"/bin/nsh",48);
        int s=ac_fs_(old); if(s>=0)ac_acp(ac_AS[s].name,name,33);
        for(int i=0;i<ac_g_ng;i++)ac_mem_ren(ac_AG[i].mem,old,name);
        { int g=ac_fgg(1000); if(g>=0){ ac_acp(ac_AG[g].name,name,33); ac_mem_del(ac_AG[g].mem,name); } else { g=ac_ensure_group(name,1000); } }
        { int g=ac_ensure_group("users",100); if(g>=0)ac_mem_add(ac_AG[g].mem,name); }
        { int g=ac_ensure_group("admin",10); if(g>=0)ac_mem_add(ac_AG[g].mem,name); g=ac_ensure_group("sudo",27); if(g>=0)ac_mem_add(ac_AG[g].mem,name); }
        if(ac_set_hash(name,pw))return -1;
        if(ac_db_save(7)){ ac_aerr("could not write the account files"); return -1; }
        int h=ac_make_home(ac_AP[p].home,1000,1000,0750u);
        if(ac_aeq(oldhome,"/home")||!oldhome[0]){ int hm=dt_resolve_dir("/home"); ac_move_home_contents(hm,h); }
        if(h>0)ac_chown_tree(h,1000,1000);
    }
    acct_reload();
    if(rootpw&&rootpw[0]){ ac_set_hash("root",rootpw); } else { int s=ac_fs_("root"); if(s<0){ ac_set_hash("root",0); s=ac_fs_("root"); } if(s>=0)ac_acp(ac_AS[s].hash,"!",136); }
    ac_db_save(4);
    if(host&&host[0])acct_set_hostname(host);
    acct_log("setup created the administrator ",name,0);
    return 0;
}

static void ac_wfile_if_missing(const char* path,const char* body,unsigned mode){
    int isd=0; if(ux_lookup(path,&isd,1)>=0)return; ac_af_write(path,body,ac_al(body),mode);
}
static void ac_fhs_layout(void){
    static const char* D[]={"/usr/bin","/usr/sbin","/usr/lib","/usr/share","/usr/local/bin","/usr/local/lib","/usr/local/share","/var/log","/var/cache","/var/lib","/var/spool","/run","/srv","/media","/opt","/mnt","/boot","/home","/etc/skel",0};
    for(int i=0;D[i];i++)ac_mkdir_path(D[i],0755u,0,0);
    { int d=ac_mkdir_path("/var/tmp",01777u,0,0); if(d>=0)DT[d].mode=0041777u; }
    { int d=ac_mkdir_path("/root",0700u,0,0); if(d>=0)DT[d].mode=0040700u; }
    { int d=dt_resolve_dir("/tmp"); if(d>=0)DT[d].mode=0041777u; }
    dt_save();
}
extern const u8* sysroot_get(const char* path,u32* len,int* exec) __attribute__((weak));
static void ac_etc_files(void){
    char b[600]; char* p=b; const char* v=(const char*)g_buildsig.version;
    u32 dl=0; const u8* dr=sysroot_get?sysroot_get("/usr/lib/os-release",&dl,0):0;
    if(dr&&dl>0&&dl<(u32)sizeof(b)){ for(u32 i=0;i<dl;i++)*p++=(char)dr[i]; *p=0; }
    else { p=scopy(p,"PRETTY_NAME=\"NoovexOS 64 "); p=scopy(p,v); p=scopy(p,"\"\nNAME=\"NoovexOS\"\nVERSION_ID=\""); p=scopy(p,v); p=scopy(p,"\"\nVERSION=\""); p=scopy(p,v); p=scopy(p,"\"\nID=noovexos\nHOME_URL=\"https://noovexos.site\"\n"); *p=0; }
    { char cur[600]; int n=ac_af_read("/etc/os-release",cur,(int)sizeof(cur)); if(n<0||!ac_aeq(cur,b))ac_af_write("/etc/os-release",b,(int)(p-b),0100644u); }
    { char h[40]; int n=ac_af_read("/etc/hostname",h,(int)sizeof(h)); const char* hn=g_hostname[0]?g_hostname:"noovex"; char hb[256]; char* q=hb; q=scopy(q,"127.0.0.1\tlocalhost\n127.0.1.1\t"); if(n>0){ for(int i=0;h[i]&&h[i]!='\n'&&i<31;i++)*q++=h[i]; } else q=scopy(q,hn); q=scopy(q,"\n::1\tlocalhost ip6-localhost ip6-loopback\n"); *q=0; ac_wfile_if_missing("/etc/hosts",hb,0100644u); }
    ac_wfile_if_missing("/etc/shells","# /etc/shells: valid login shells\n/bin/nsh\n/bin/sh\n",0100644u);
    ac_wfile_if_missing("/etc/sudoers","# /etc/sudoers - who may use sudo on NoovexOS\n# timestamp_timeout is how many minutes sudo remembers your password\nDefaults timestamp_timeout=15\nroot ALL=(ALL:ALL) ALL\n%admin ALL=(ALL:ALL) ALL\n%sudo ALL=(ALL:ALL) ALL\n",0100440u);
    ac_wfile_if_missing("/etc/issue","NoovexOS 64 \\n \\l\n\n",0100644u);
    ac_wfile_if_missing("/etc/login.defs","# /etc/login.defs - account defaults\nUID_MIN 1000\nUID_MAX 59999\nGID_MIN 1000\nGID_MAX 59999\nPASS_MIN_LEN 4\nENCRYPT_METHOD SHA512\nUMASK 022\n",0100644u);
    ac_wfile_if_missing("/var/log/auth.log","",0100640u);
    ac_wfile_if_missing("/var/log/wtmp","",0100664u);
    { char s[512]; int n=ac_af_read("/etc/sudoers",s,(int)sizeof(s)); if(n>0){ const char* k="timestamp_timeout="; for(int i=0;i<n;i++){ int j=0; while(k[j]&&s[i+j]==k[j])j++; if(!k[j]){ int m=ac_aatoi(s+i+j); if(m>=0&&m<=1440)g_sudo_min=m; break; } } } }
}

static void ac_migrate(void){
    int ch=0;
    if(ac_fg("admin")<0){ ac_ensure_group("admin",10); ch|=2; }
    if(ac_fg("sudo")<0){ int g=ac_ensure_group("sudo",27); int a=ac_fg("admin"); if(g>=0&&a>=0)ac_acp(ac_AG[g].mem,ac_AG[a].mem,384); ch|=2; }
    { int u=ac_fg("users");
      if(u>=0&&ac_AG[u].gid==1000){ int p=ac_fpu(1000);
          if(p>=0&&ac_fg(ac_AP[p].name)<0){ char mem[384]; ac_acp(mem,ac_AG[u].mem,384); ac_acp(ac_AG[u].name,ac_AP[p].name,33); ac_AG[u].mem[0]=0;
              int ng=ac_ensure_group("users",100);
              if(ng>=0){ const char* m=mem; while(*m){ char one[33]; int n=0; while(*m&&*m!=','&&n<32)one[n++]=*m++; one[n]=0; if(*m)m++; if(n)ac_mem_add(ac_AG[ng].mem,one); } ac_mem_add(ac_AG[ng].mem,ac_AP[p].name); }
              ch|=2; } }
      else if(u<0){ ac_ensure_group("users",100); ch|=2; } }
    if(ac_fp("guest")<0&&ac_g_np<ac_AP_N){ ac_ap_t* a=&ac_AP[ac_g_np++]; ac_acp(a->name,"guest",33); ac_acp(a->pw,"x",8); a->uid=999; a->gid=999; ac_acp(a->gecos,"Guest",96); ac_acp(a->home,"/home/guest",96); ac_acp(a->shell,"/bin/nsh",48); ac_ensure_group("guest",999); ch|=3; }
    for(int i=0;i<ac_g_ns;i++){ char* h=ac_AS[i].hash; int legacy=!ac_AS[i].rest[0];
        if(legacy){ if(h[0]=='*'){ int p=ac_fp(ac_AS[i].name); if(p>=0&&ac_AP[p].uid>=1000)h[0]=0; }
            else if(h[0]&&h[0]!='$'&&h[0]!='!'){ char t[144]; char* q=t; q=scopy(q,"$nvx$"); for(int z=0;h[z]&&z<130;z++)*q++=h[z]; *q=0; ac_acp(h,t,136); }
            ac_rest_now(ac_AS[i].rest); ch|=4; } }
    for(int i=0;i<ac_g_np;i++) if(ac_fs_(ac_AP[i].name)<0&&ac_g_ns<ac_AP_N){ ac_as_t* s=&ac_AS[ac_g_ns++]; ac_acp(s->name,ac_AP[i].name,33); ac_acp(s->hash,(ac_AP[i].uid>=1000||ac_aeq(ac_AP[i].name,"guest"))?"":"!",136); ac_rest_now(s->rest); ch|=4; }
    if(ch)ac_db_save(ch);
}

void acct_wipe_guest(void){
    for(int k=0;k<FS_N;k++) if(FS[k].used){ const char* n=FS[k].name; for(int i=0;n[i];i++){ if(n[i]=='.'&&n[i+1]=='g'&&n[i+2]=='u'&&n[i+3]=='e'&&n[i+4]=='s'&&n[i+5]=='t'&&n[i+6]=='.'){ fs_drop_name(k); break; } } }
    int h=dt_resolve_dir("/home/guest");
    if(h>0)ac_remove_tree(h,1);
    ac_make_home("/home/guest",999,999,0700u);
}

void acct_boot(void){
    ac_fhs_layout();
    ac_etc_files();
    acct_reload();
    ac_migrate();
    acct_reload();
    acct_wipe_guest();
    acct_wtmp("boot","system");
}

const char* acct_home_of_current(void){ return g_home; }
void acct_set_path(const char* home){ char p[200]; char* q=p; for(int i=0;home&&home[i]&&i<120;i++)*q++=home[i]; q=scopy(q,"/.local/bin"); *q=0; char full[240]; char* f=full; f=scopy(f,"/usr/local/bin:/opt/gnu/bin:/bin:/usr/bin:"); f=scopy(f,p); *f=0; env_set("PATH",full); }

static void ac_civil(u32 t,int* y,int* mo,int* d,int* h,int* mi,int* s){
    u32 days=t/86400u, rem=t%86400u; *h=(int)(rem/3600u); *mi=(int)((rem%3600u)/60u); *s=(int)(rem%60u);
    long z=(long)days+719468; long era=z/146097; long doe=z-era*146097; long yoe=(doe-doe/1460+doe/36524-doe/146096)/365; long yy=yoe+era*400; long doy=doe-(365*yoe+yoe/4-yoe/100); long mp=(5*doy+2)/153; long dd=doy-(153*mp+2)/5+1; long mm=mp<10?mp+3:mp-9; if(mm<=2)yy++;
    *y=(int)yy; *mo=(int)mm; *d=(int)dd;
}
void acct_fmt_time(u32 t,char* out){
    int y,mo,d,h,mi,s; ac_civil(t,&y,&mo,&d,&h,&mi,&s); char* p=out;
    p=udec(p,(u32)y); *p++='-'; p=u2(p,(u32)mo); *p++='-'; p=u2(p,(u32)d); *p++=' '; p=u2(p,(u32)h); *p++=':'; p=u2(p,(u32)mi); *p++=':'; p=u2(p,(u32)s); *p=0;
}
static void ac_append_log(const char* path,const char* line,int maxlen){
    static char buf[16384]; int n=ac_af_read(path,buf,(int)sizeof(buf)); if(n<0)n=0;
    int L=ac_al(line);
    if(n+L>maxlen){ int cut=n-maxlen/2; if(cut<0)cut=0; while(cut<n&&buf[cut]!='\n')cut++; if(cut<n)cut++; int o=0; for(int i=cut;i<n;i++)buf[o++]=buf[i]; n=o; }
    if(n+L>=(int)sizeof(buf))return;
    for(int i=0;i<L;i++)buf[n++]=line[i];
    ac_af_write(path,buf,n,0);
}
void acct_log(const char* a,const char* b,const char* c){
    char line[300]; char* p=line; acct_fmt_time(ux_now(),p); while(*p)p++;
    *p++=' '; p=scopy(p,g_hostname[0]?g_hostname:"noovex"); p=scopy(p," auth: ");
    if(a){ int n=0; while(a[n]&&n<80)*p++=a[n++]; } if(b){ int n=0; while(b[n]&&n<64)*p++=b[n++]; } if(c){ int n=0; while(c[n]&&n<80)*p++=c[n++]; }
    *p++='\n'; *p=0;
    ac_append_log("/var/log/auth.log",line,12000);
}
void acct_wtmp(const char* kind,const char* user){
    char line[96]; char* p=line; p=scopy(p,kind); *p++=' '; { int n=0; while(user[n]&&n<32)*p++=user[n++]; } *p++=' '; p=udec(p,ux_now()); *p++='\n'; *p=0;
    ac_append_log("/var/log/wtmp",line,12000);
}
int acct_last(char* out,int cap){
    static char buf[16384]; int n=ac_af_read("/var/log/wtmp",buf,(int)sizeof(buf)); if(n<=0){ out[0]=0; return 0; }
    char* rec_k[200]; char* rec_u[200]; u32 rec_t[200]; int nr=0;
    for(int i=0;i<n&&nr<200;){ int j=i; while(j<n&&buf[j]!='\n')j++; buf[j]=0; char* l=buf+i; char* s1=l; while(*s1&&*s1!=' ')s1++; if(*s1){ *s1++=0; char* s2=s1; while(*s2&&*s2!=' ')s2++; if(*s2){ *s2++=0; rec_k[nr]=l; rec_u[nr]=s1; rec_t[nr]=(u32)ac_aatoi(s2); nr++; } } i=j+1; }
    int o=0; out[0]=0; int shown=0;
    for(int r=nr-1;r>=0&&shown<40;r--){
        char ln[160]; char* p=ln; char ts[24];
        if(ac_aeq(rec_k[r],"login")){
            int end=-1, crash=0; for(int q=r+1;q<nr;q++){ if(ac_aeq(rec_k[q],"logout")&&ac_aeq(rec_u[q],rec_u[r])){ end=q; break; } if(ac_aeq(rec_k[q],"boot")){ crash=1; end=q; break; } }
            { int k=0; while(rec_u[r][k]&&k<12)*p++=rec_u[r][k++]; while(k++<12)*p++=' '; }
            p=scopy(p,"desktop  "); acct_fmt_time(rec_t[r],ts); p=scopy(p,ts);
            if(end<0)p=scopy(p,"   still logged in");
            else if(crash)p=scopy(p,"   - (no logout, restarted)");
            else { p=scopy(p,"   - "); acct_fmt_time(rec_t[end],ts); p=scopy(p,ts+11); }
            *p++='\n'; *p=0; shown++;
        }
        else if(ac_aeq(rec_k[r],"boot")){ p=scopy(p,"reboot      system   "); acct_fmt_time(rec_t[r],ts); p=scopy(p,ts); *p++='\n'; *p=0; shown++; }
        else continue;
        int L=ac_al(ln); if(o+L>=cap-1)break; for(int i=0;i<L;i++)out[o++]=ln[i]; out[o]=0;
    }
    return o;
}

int acct_login(const char* name){ return acct_login_ex(name,0); }
int acct_login_ex(const char* name,int quiet){
    acct_reload(); int p=ac_fp(name); if(p<0)return -1;
    ac_ap_t* a=&ac_AP[p];
    if(ac_aeq(name,"guest"))acct_wipe_guest();
    g_uid=a->uid; g_gid=a->gid;
    { int k=0; for(;a->name[k]&&k<31;k++)g_user[k]=a->name[k]; g_user[k]=0; }
    { int k=0; for(;a->gecos[k]&&a->gecos[k]!=','&&k<63;k++)g_fullname[k]=a->gecos[k]; g_fullname[k]=0; }
    ac_acp(g_home,a->home[0]?a->home:"/home",96);
    int d=dt_resolve_dir(g_home); if(d<0){ d=ac_make_home(g_home,a->uid,a->gid,0750u); }
    if(d>=0&&DT[d].uid!=a->uid&&a->uid>=1000&&(ac_aeq(g_home,"/home")||k_is_ancestor(dt_resolve_dir("/home"),d))){ DT[d].uid=a->uid; DT[d].gid=a->gid; dt_save(); }
    if(d>=0){ g_home_dir=d; g_dcwd=d; }
    env_set("HOME",g_home); acct_set_path(g_home); env_set("USER",a->name); env_set("LOGNAME",a->name); env_set("PWD",g_home); env_set("SHELL",a->shell[0]?a->shell:"/bin/nsh");
    g_logged_in=1; g_locked=0;
    if(!quiet){ acct_wtmp("login",a->name); acct_log("session opened for ",a->name,0); }
    return a->uid;
}
void acct_logout(void){
    if(!g_logged_in)return;
    acct_wtmp("logout",g_user);
    acct_log("session closed for ",g_user,0);
    if(ac_aeq(g_user,"guest"))acct_wipe_guest();
    { extern void focus_logout(void) __attribute__((weak)); if(focus_logout)focus_logout(); }
    g_logged_in=0; g_locked=0;
}
int acct_gids(int uid,int* out,int max){
    acct_reload(); int p=ac_fpu(uid); int n=0;
    if(p<0){ if(max>0)out[n++]=uid==0?0:uid; return n; }
    if(n<max)out[n++]=ac_AP[p].gid;
    for(int i=0;i<ac_g_ng&&n<max;i++){ if(ac_AG[i].gid==ac_AP[p].gid)continue; if(ac_mem_has(ac_AG[i].mem,ac_AP[p].name))out[n++]=ac_AG[i].gid; }
    return n;
}
void acct_profile_name(const char* base,char* out,int cap){
    int primary=!g_logged_in||g_uid==0||g_uid==1000;
    if(primary){ ac_acp(out,base,cap); return; }
    int L=ac_al(base), dot=L; for(int i=L-1;i>0;i--) if(base[i]=='.'){ dot=i; break; }
    int o=0; for(int i=0;i<dot&&o<cap-1;i++)out[o++]=base[i];
    if(o<cap-1)out[o++]='.'; for(int i=0;g_user[i]&&o<cap-1;i++)out[o++]=g_user[i];
    for(int i=dot;i<L&&o<cap-1;i++)out[o++]=base[i];
    out[o]=0;
}

