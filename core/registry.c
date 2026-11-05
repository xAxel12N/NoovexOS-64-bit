#include "include/nvx/base.h"
#include "include/nvx/lib/base/fmt.h"
#include "include/nvx/mem/heap.h"
#include "include/nvx/fs/vfs/ramfs.h"
#include "include/nvx/fs/vfs/dirtree.h"
#include "include/nvx/fs/vfs/unixfs.h"
#include "include/nvx/core/klog.h"
#include "include/nvx/drivers/video/fb/framebuffer.h"
#include "include/nvx/core/registry.h"
#include "include/nvx/core/buildsig.h"
#include "include/nvx/arch/x86_64/cpu/percpu.h"

extern int g_accent, g_theme, g_showsec, g_kbd_layout, g_krep_speed, g_guest_on, g_lock_min, g_lid_action, g_pbtn_action;
extern u32 ACC, ACC2; extern const u32 ACV[5]; extern u32 g_ram_mb; extern char g_hostname[], g_user[];
extern void kbd_apply_repeat(void);
extern int acct_is_admin(int uid);
extern int fid_has(const char* user);
extern int live_lock_on(void);
extern int live_lock_set(int on);
int ux_write_text(const char* path,const char* body,int len,unsigned mode);
int ux_read_file(const char* p,char* out,int cap);

#define RG_MAX 256
typedef struct { char key[64]; char name[32]; char val[96]; int used; } rg_free;
static rg_free g_rf[RG_MAX];
static int g_rg_loaded, g_rg_busy;

static int rg_eq(const char* a,const char* b){ while(*a&&*b){ char x=*a, y=*b; if(x>='A'&&x<='Z')x+=32; if(y>='A'&&y<='Z')y+=32; if(x!=y)return 0; a++; b++; } return *a==*b; }
static int rg_pre(const char* s,const char* p){ while(*p){ char x=*s, y=*p; if(x>='A'&&x<='Z')x+=32; if(y>='A'&&y<='Z')y+=32; if(x!=y)return 0; s++; p++; } return 1; }
static void rg_cpy(char* d,const char* s,int cap){ int i=0; for(;s[i]&&i<cap-1;i++)d[i]=s[i]; d[i]=0; }
static int rg_atoi(const char* s,int* ok){ int neg=0, v=0, n=0; while(*s==' ')s++; if(*s=='-'){ neg=1; s++; } while(*s>='0'&&*s<='9'){ v=v*10+(*s-'0'); s++; n++; } while(*s==' ')s++; *ok=(n>0&&!*s); return neg?-v:v; }

static void ap_accent(void){ ACC=ACV[g_accent]; ACC2=mix(ACC,0,60); }
static void ap_krep(void){ kbd_apply_repeat(); }
static int gt_usb(void){ int v=live_lock_on(); return v<0?1:v; }
static void st_usb(int v){ live_lock_set(v); }
static int gt_face(void){ return fid_has(g_user); }

typedef struct { const char* key; const char* name; int* p; int lo, hi, ro; void (*apply)(void); int (*get)(void); void (*set)(int); const char* help; } rg_bound;
static const rg_bound B[] = {
    { "System\\Display", "Accent", &g_accent, 0, 4, 0, ap_accent, 0, 0, "Accent colour 0-4 (blue, purple, green, orange, pink)" },
    { "System\\Display", "Theme", &g_theme, 0, 5, 0, 0, 0, 0, "Desktop theme 0-5" },
    { "System\\Display", "ShowSeconds", &g_showsec, 0, 1, 0, 0, 0, 0, "Seconds on the taskbar clock" },
    { "System\\Input", "KeyboardLayout", &g_kbd_layout, 0, 5, 0, 0, 0, 0, "Keyboard layout 0-5" },
    { "System\\Input", "KeyRepeat", &g_krep_speed, 0, 2, 0, ap_krep, 0, 0, "Key repeat 0 slow, 1 normal, 2 fast" },
    { "System\\Security", "GuestAccount", &g_guest_on, 0, 1, 0, 0, 0, 0, "Guest account on (1) or off (0)" },
    { "System\\Security", "LockMinutes", &g_lock_min, 0, 240, 0, 0, 0, 0, "Lock the screen after this many idle minutes, 0 never" },
    { "System\\Power", "LidAction", &g_lid_action, 0, 2, 0, 0, 0, 0, "When the lid closes: 0 sleep, 1 lock, 2 nothing" },
    { "System\\Power", "PowerButtonAction", &g_pbtn_action, 0, 2, 0, 0, 0, 0, "Power button: 0 menu, 1 sleep, 2 shut down" },
    { "System\\SignIn", "UsbStickSignInScreen", 0, 0, 1, 0, 0, gt_usb, st_usb, "Sign-in screen when starting from the USB stick (saved on the stick)" },
    { "User\\FaceID", "Enabled", 0, 0, 1, 1, 0, gt_face, 0, "Face ID is set up for this account (read only)" },
};
#define NB ((int)(sizeof(B)/sizeof(B[0])))

static int rg_info_val(const char* key,const char* name,char* out,int cap){
    if(rg_eq(key,"System\\Info")){
        if(rg_eq(name,"Version")){ char* p=out; p=scopy(p,"NoovexOS 64 v"); *p=0; int L=(int)(p-out); rg_cpy(p,(const char*)g_buildsig.version,cap-L); return 1; }
        if(rg_eq(name,"ComputerName")){ rg_cpy(out,g_hostname,cap); return 1; }
        if(rg_eq(name,"MemoryMB")){ char* p=out; p=udec(p,g_ram_mb); *p=0; return 1; }
        if(rg_eq(name,"User")){ rg_cpy(out,g_user,cap); return 1; } }
    return 0; }
static const char* const INFO_N[4]={"Version","ComputerName","MemoryMB","User"};

static int rg_bget(int i){ return B[i].get?B[i].get():*B[i].p; }
static int rg_bfind(const char* key,const char* name){ for(int i=0;i<NB;i++) if(rg_eq(B[i].key,key)&&rg_eq(B[i].name,name))return i; return -1; }
static int rg_ffind(const char* key,const char* name){ for(int i=0;i<RG_MAX;i++) if(g_rf[i].used&&rg_eq(g_rf[i].key,key)&&rg_eq(g_rf[i].name,name))return i; return -1; }

static void rg_norm(const char* in,char* out,int cap){ int o=0; while(*in==' '||*in=='\\'||*in=='/')in++; for(;*in&&o<cap-1;in++){ char c=*in=='/'?'\\':*in; if(c=='\\'&&o&&out[o-1]=='\\')continue; out[o++]=c; } while(o&&out[o-1]=='\\')o--; out[o]=0;
    if(rg_pre(out,"HKLM\\")){ char t[64]; rg_cpy(t,out+5,64); rg_cpy(out,"System\\",cap); int L=0; while(out[L])L++; rg_cpy(out+L,t,cap-L); }
    if(rg_pre(out,"HKCU\\")){ char t[64]; rg_cpy(t,out+5,64); rg_cpy(out,"User\\",cap); int L=0; while(out[L])L++; rg_cpy(out+L,t,cap-L); } }
static int rg_hive(const char* key){ return rg_pre(key,"User")?1:0; }
static int rg_valid_key(const char* key){ return (rg_pre(key,"System")&&(key[6]==0||key[6]=='\\'))||(rg_pre(key,"User")&&(key[4]==0||key[4]=='\\')); }
static int rg_valid_name(const char* n){ if(!n[0])return 0; for(int i=0;n[i];i++){ char c=n[i]; if(i>=31||c=='='||c=='['||c==']'||c=='\n'||c=='\\')return 0; } return 1; }

static void rg_path(int hive,char* out){ char* p=out; p=scopy(p,"/etc/noovex/registry/"); if(hive){ p=scopy(p,"user-"); p=scopy(p,g_user); p=scopy(p,".reg"); } else p=scopy(p,"system.reg"); *p=0; }
static void rg_mkdirs(void){ dt_init(); int d=dt_resolve_dir("/etc"); if(d<0)return; int a=dt_child(d,"noovex"); if(a<0)a=dt_mkdir_at(d,"noovex"); if(a<0)return; if(dt_child(a,"registry")<0)dt_mkdir_at(a,"registry"); }

static char* rg_line(char* p,char* end,const char* name,const char* val){ int nl=0, vl=0; while(name[nl])nl++; while(val[vl])vl++; if(p+nl+vl+3>=end)return p; p=scopy(p,name); *p++='='; p=scopy(p,val); *p++='\n'; *p=0; return p; }
static int rg_write_hive(int hive){
    int cap=98304; char* b=(char*)kmalloc((unsigned long)cap); if(!b)return -1; char* p=b; char* end=b+cap-1; *p=0;
    char (*keys)[64]=(char(*)[64])kmalloc(256*64); if(!keys){ kfree(b); return -1; } int full=0;
    p=scopy(p,hive?"; NoovexOS registry - this account\n":"; NoovexOS registry - whole system\n");
    int nk=0;
    for(int i=0;i<NB;i++) if(rg_hive(B[i].key)==hive&&!B[i].ro){ int f=0; for(int k=0;k<nk;k++) if(rg_eq(keys[k],B[i].key)){ f=1; break; } if(!f&&nk<256)rg_cpy(keys[nk++],B[i].key,64); }
    for(int i=0;i<RG_MAX;i++) if(g_rf[i].used&&rg_hive(g_rf[i].key)==hive){ int f=0; for(int k=0;k<nk;k++) if(rg_eq(keys[k],g_rf[i].key)){ f=1; break; } if(!f){ if(nk<256)rg_cpy(keys[nk++],g_rf[i].key,64); else full=1; } }
    for(int k=0;k<nk;k++){ if(p+400>=end){ full=1; break; } *p++='\n'; *p++='['; p=scopy(p,keys[k]); *p++=']'; *p++='\n'; *p=0;
        for(int i=0;i<NB;i++) if(!B[i].ro&&!B[i].get&&rg_eq(B[i].key,keys[k])){ char v[16],*q=v; int x=rg_bget(i); if(x<0){ *q++='-'; x=-x; } q=udec(q,(u32)x); *q=0; p=rg_line(p,end,B[i].name,v); }
        for(int i=0;i<RG_MAX;i++) if(g_rf[i].used&&rg_eq(g_rf[i].key,keys[k]))p=rg_line(p,end,g_rf[i].name,g_rf[i].val); }
    kfree(keys); if(full||p>=end-2){ kfree(b); klog("registry: too much data - the hive was not rewritten"); return -1; }
    rg_mkdirs(); char path[96]; rg_path(hive,path);
    int r=ux_write_text(path,b,(int)(p-b),hive?0100600u:0100644u); kfree(b); return r<0?-1:0; }
int reg_save(void){ if(g_rg_busy)return 0; int a=rg_write_hive(0), b=rg_write_hive(1); return (a<0||b<0)?-1:0; }

static void rg_load_hive(int hive,int apply){
    char path[96]; rg_path(hive,path); int cap=98304; char* b=(char*)kmalloc((unsigned long)cap); if(!b)return; int n=ux_read_file(path,b,cap); if(n<=0){ kfree(b); return; }
    char key[64]; key[0]=0; int i=0;
    while(i<n){ int j=i; while(j<n&&b[j]!='\n')j++; char line[200]; int L=j-i; if(L>199)L=199; for(int q=0;q<L;q++)line[q]=b[i+q]; line[L]=0; while(L&&(line[L-1]=='\r'||line[L-1]==' '))line[--L]=0; i=j+1;
        if(!L||line[0]==';'||line[0]=='#')continue;
        if(line[0]=='['){ char t[64]; int o=0; for(int q=1;line[q]&&line[q]!=']'&&o<63;q++)t[o++]=line[q]; t[o]=0; rg_norm(t,key,64); if(!rg_valid_key(key)||rg_hive(key)!=hive)key[0]=0; continue; }
        if(!key[0])continue; int eq=-1; for(int q=0;line[q];q++) if(line[q]=='='){ eq=q; break; } if(eq<=0)continue;
        line[eq]=0; const char* nm=line; const char* v=line+eq+1;
        int bi=rg_bfind(key,nm);
        if(bi>=0){ if(apply&&!B[bi].ro&&!B[bi].get){ int ok; int x=rg_atoi(v,&ok); if(ok&&x>=B[bi].lo&&x<=B[bi].hi){ *B[bi].p=x; if(B[bi].apply)B[bi].apply(); } } continue; }
        if(!rg_valid_name(nm))continue; int fi=rg_ffind(key,nm); if(fi<0){ for(int q=0;q<RG_MAX;q++) if(!g_rf[q].used){ fi=q; break; } } if(fi<0)continue;
        g_rf[fi].used=1; rg_cpy(g_rf[fi].key,key,64); rg_cpy(g_rf[fi].name,nm,32); rg_cpy(g_rf[fi].val,v,96); }
    kfree(b); }
void reg_load(int apply){ for(int i=0;i<RG_MAX;i++)g_rf[i].used=0; g_rg_busy=1; rg_load_hive(0,apply); rg_load_hive(1,apply); g_rg_busy=0; g_rg_loaded=1; }
void reg_boot(void){ char path[96]; rg_path(0,path); int isd=0; int have=ux_lookup(path,&isd,1)>=0; reg_load(have); if(!have)reg_save(); klog(have?"registry: loaded /etc/noovex/registry":"registry: created /etc/noovex/registry"); }
void reg_user_changed(void){ reg_load(1); }

int reg_can_write(const char* key){ return rg_hive(key)==1||g_uid==0||acct_is_admin(g_uid); }

int reg_get(const char* key0,const char* name,char* out,int cap){
    char key[64]; rg_norm(key0,key,64); out[0]=0;
    if(rg_info_val(key,name,out,cap))return 2;
    int bi=rg_bfind(key,name); if(bi>=0){ char* p=out; int x=rg_bget(bi); if(x<0){ *p++='-'; x=-x; } p=udec(p,(u32)x); *p=0; return B[bi].ro?2:1; }
    int fi=rg_ffind(key,name); if(fi>=0){ rg_cpy(out,g_rf[fi].val,cap); return 1; }
    return 0; }
const char* reg_help(const char* key0,const char* name){ char key[64]; rg_norm(key0,key,64); int bi=rg_bfind(key,name); if(bi>=0)return B[bi].help; if(rg_eq(key,"System\\Info"))return "Read only"; return ""; }

int reg_set(const char* key0,const char* name,const char* val){
    char key[64]; rg_norm(key0,key,64);
    if(!rg_valid_key(key))return REG_EBADKEY; if(!rg_valid_name(name))return REG_EBADNAME; if(rg_eq(key,"System\\Info"))return REG_ERO;
    if(!reg_can_write(key))return REG_EPERM;
    int bi=rg_bfind(key,name);
    if(bi>=0){ if(B[bi].ro)return REG_ERO; int ok; int x=rg_atoi(val,&ok); if(!ok||x<B[bi].lo||x>B[bi].hi)return REG_ERANGE;
        if(B[bi].set)B[bi].set(x); else { *B[bi].p=x; if(B[bi].apply)B[bi].apply(); }
        extern void power_prefs_save(void); g_rg_busy=1; power_prefs_save(); g_rg_busy=0; reg_save(); return 0; }
    int fi=rg_ffind(key,name); if(fi<0){ for(int q=0;q<RG_MAX;q++) if(!g_rf[q].used){ fi=q; break; } if(fi<0)return REG_EFULL; }
    g_rf[fi].used=1; rg_cpy(g_rf[fi].key,key,64); rg_cpy(g_rf[fi].name,name,32); rg_cpy(g_rf[fi].val,val,96); reg_save(); return 0; }

int reg_delete(const char* key0,const char* name){
    char key[64]; rg_norm(key0,key,64); if(!reg_can_write(key))return REG_EPERM; int n=0;
    if(name&&name[0]){ if(rg_bfind(key,name)>=0||rg_eq(key,"System\\Info"))return REG_ERO; int fi=rg_ffind(key,name); if(fi<0)return REG_ENOENT; g_rf[fi].used=0; reg_save(); return 0; }
    int L=0; while(key[L])L++;
    for(int i=0;i<NB;i++) if(rg_pre(B[i].key,key)&&(B[i].key[L]==0||B[i].key[L]=='\\'))return REG_ERO;
    for(int i=0;i<RG_MAX;i++) if(g_rf[i].used&&rg_pre(g_rf[i].key,key)&&(g_rf[i].key[L]==0||g_rf[i].key[L]=='\\')){ g_rf[i].used=0; n++; }
    if(!n)return REG_ENOENT; reg_save(); return 0; }

int reg_add_key(const char* key0){ char key[64]; rg_norm(key0,key,64); if(!rg_valid_key(key))return REG_EBADKEY; if(!reg_can_write(key))return REG_EPERM; return reg_set(key,"(default)",""); }

static int rg_addkey(char (*out)[64],int n,int cap,const char* k){ for(int i=0;i<n;i++) if(rg_eq(out[i],k))return n; if(n<cap)rg_cpy(out[n++],k,64); return n; }
int reg_keys(char (*out)[64],int cap){
    int n=0; n=rg_addkey(out,n,cap,"System"); n=rg_addkey(out,n,cap,"System\\Info"); n=rg_addkey(out,n,cap,"User");
    for(int i=0;i<NB;i++){ char t[64]; rg_cpy(t,B[i].key,64); for(int q=0;t[q];q++) if(t[q]=='\\'){ t[q]=0; n=rg_addkey(out,n,cap,t); t[q]='\\'; } n=rg_addkey(out,n,cap,t); }
    for(int i=0;i<RG_MAX;i++) if(g_rf[i].used){ char t[64]; rg_cpy(t,g_rf[i].key,64); for(int q=0;t[q];q++) if(t[q]=='\\'){ t[q]=0; n=rg_addkey(out,n,cap,t); t[q]='\\'; } n=rg_addkey(out,n,cap,t); }
    for(int a=1;a<n;a++){ char t[64]; rg_cpy(t,out[a],64); int b=a-1; while(b>=0){ const char* x=out[b]; const char* y=t; int c=0; while(*x&&*x==*y){ x++; y++; } c=(unsigned char)*x-(unsigned char)*y; if(c<=0)break; rg_cpy(out[b+1],out[b],64); b--; } rg_cpy(out[b+1],t,64); }
    return n; }
int reg_values(const char* key0,char (*names)[32],int cap){
    char key[64]; rg_norm(key0,key,64); int n=0;
    if(rg_eq(key,"System\\Info")){ for(int i=0;i<4&&n<cap;i++)rg_cpy(names[n++],INFO_N[i],32); return n; }
    for(int i=0;i<NB&&n<cap;i++) if(rg_eq(B[i].key,key))rg_cpy(names[n++],B[i].name,32);
    for(int i=0;i<RG_MAX&&n<cap;i++) if(g_rf[i].used&&rg_eq(g_rf[i].key,key))rg_cpy(names[n++],g_rf[i].name,32);
    return n; }

int reg_export(const char* path,const char* prefix0){
    char prefix[64]; rg_norm(prefix0?prefix0:"",prefix,64); int pl=0; while(prefix[pl])pl++;
    int cap=32768; char* b=(char*)kmalloc((unsigned long)cap); if(!b)return -1; char* p=b; char* end=b+cap-1; p=scopy(p,"; NoovexOS registry export\n");
    char (*keys)[64]=(char(*)[64])kmalloc(96*64); if(!keys){ kfree(b); return -1; } int nk=reg_keys(keys,96); int nv=0;
    for(int k=0;k<nk;k++){ if(pl&&!(rg_pre(keys[k],prefix)&&(keys[k][pl]==0||keys[k][pl]=='\\')))continue; char nm[48][32]; int c=reg_values(keys[k],nm,48); if(!c)continue;
        if(p+80>=end)break; *p++='\n'; *p++='['; p=scopy(p,keys[k]); *p++=']'; *p++='\n'; *p=0;
        for(int i=0;i<c;i++){ char v[96]; reg_get(keys[k],nm[i],v,96); p=rg_line(p,end,nm[i],v); nv++; } }
    kfree(keys); int r=ux_write_text(path,b,(int)(p-b),0100644u); kfree(b); return r<0?-1:nv; }

int reg_import(const char* path,int* nset,int* nfail){
    int cap=32768; char* b=(char*)kmalloc((unsigned long)cap); if(!b)return -1; int n=ux_read_file(path,b,cap); if(n<0){ kfree(b); return -1; }
    char key[64]; key[0]=0; int i=0; *nset=0; *nfail=0;
    while(i<n){ int j=i; while(j<n&&b[j]!='\n')j++; char line[200]; int L=j-i; if(L>199)L=199; for(int q=0;q<L;q++)line[q]=b[i+q]; line[L]=0; while(L&&(line[L-1]=='\r'||line[L-1]==' '))line[--L]=0; i=j+1;
        if(!L||line[0]==';'||line[0]=='#')continue;
        if(line[0]=='['){ char t[64]; int o=0; for(int q=1;line[q]&&line[q]!=']'&&o<63;q++)t[o++]=line[q]; t[o]=0; rg_norm(t,key,64); continue; }
        int eq=-1; for(int q=0;line[q];q++) if(line[q]=='='){ eq=q; break; } if(eq<=0||!key[0])continue; line[eq]=0;
        if(rg_eq(key,"System\\Info")||(rg_bfind(key,line)>=0&&B[rg_bfind(key,line)].ro))continue;
        if(reg_set(key,line,line+eq+1)==0)(*nset)++; else (*nfail)++; }
    kfree(b); return 0; }

const char* reg_err(int e){ switch(e){ case REG_EBADKEY: return "keys start with System or User"; case REG_EBADNAME: return "names are 1-31 characters without = [ ] or \\"; case REG_ERO: return "this value is read only"; case REG_EPERM: return "only an administrator can change System keys (try sudo)"; case REG_ERANGE: return "the value is not a number in the allowed range"; case REG_EFULL: return "the registry is full"; case REG_ENOENT: return "not found"; } return "error"; }
