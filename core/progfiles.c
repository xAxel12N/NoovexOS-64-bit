#include "include/nvx/base.h"
#include "include/nvx/lib/base/fmt.h"
#include "include/nvx/mem/heap.h"
#include "include/nvx/fs/vfs/ramfs.h"
#include "include/nvx/fs/vfs/dirtree.h"
#include "include/nvx/fs/vfs/unixfs.h"
#include "include/nvx/graphics/ui/wm.h"
#include "include/nvx/core/klog.h"
#include "include/nvx/core/buildsig.h"
#include "include/nvx/core/progfiles.h"

extern int g_apps_gen;
int xapps_count(void);
int xapps_get(int i,const char** name,const char** exec,const char** icon,const char** path);
int ux_write_text(const char* path,const char* body,int len,unsigned mode);
int ux_read_file(const char* p,char* out,int cap);
int sysroot_stats(int* files,int* layers);

typedef struct { int win; const char* folder; const char* cmd; const char* desc; int hidden; } pf_builtin;
static const pf_builtin PB[] = {
    { W_FILES, "Files", "files", "Browse and manage files", 0 },
    { W_TERM, "Terminal", "", "Command line", 0 },
    { W_EDITOR, "Editor", "edit", "Text and code editor", 0 },
    { W_BROWSER, "Browser", "browser", "Web browser", 0 },
    { W_VIDEO, "Video Player", "video", "Plays videos and music", 0 },
    { W_CALC, "Calculator", "", "Calculator", 0 },
    { W_CLOCK, "Clock", "", "Clock, alarms and timers", 0 },
    { W_CALENDAR, "Calendar", "", "Calendar", 0 },
    { W_SYSMON, "Task Manager", "taskmgr", "Apps, background services and hidden apps", 0 },
    { W_PAINT, "Paint", "", "Drawing", 0 },
    { W_SETTINGS, "Settings", "settings", "System settings", 0 },
    { W_CAMERA, "Camera", "camera", "Photos and video from the camera", 0 },
    { W_RECORDER, "Voice Recorder", "recorder", "Records sound", 0 },
    { W_WRITER, "Noovex Writer", "writer", "Simple word processor", 0 },
    { W_LOGS, "Logs", "", "Live system activity", 0 },
    { W_SNAKE, "Snake", "", "Game", 0 },
    { W_FLAPPY, "Flappy Bird", "", "Game", 0 },
    { W_DOOM, "DOOM", "doom", "Game (FreeDoom)", 0 },
    { W_CHATGPT, "Registry Editor", "regedit", "View and change the registry (hidden app)", 1 },
};
#define NPB ((int)(sizeof(PB)/sizeof(PB[0])))

static pf_app g_pf[PF_MAX]; static int g_npf, g_pf_key=-1;

static void pf_cp(char* d,const char* s,int cap){ int i=0; for(;s[i]&&i<cap-1;i++)d[i]=s[i]; d[i]=0; }
static int pf_eq(const char* a,const char* b){ while(*a&&*a==*b){ a++; b++; } return *a==*b; }
static void pf_folder(const char* in,char* out){ int o=0; for(int i=0;in[i]&&o<47;i++){ char c=in[i]; if((c>='a'&&c<='z')||(c>='A'&&c<='Z')||(c>='0'&&c<='9')||c==' '||c=='-'||c=='.'||c=='('||c==')'||c=='+'||c=='_')out[o++]=c; } while(o&&out[o-1]==' ')o--; out[o]=0; if(!o)pf_cp(out,"App",48); }

static int pf_owner(const char* path,char* pkg,u32* size){
    pkg[0]=0; *size=0; if(!path[0])return 0; int L=0; while(path[L])L++;
    char nm[80]; int t=0; int cap=262144; char* b=0;
    for(int i=0;i<4096&&k_readdir_at("/var/lib/nvpkg",i,nm,&t)==1;i++){ int n=0; while(nm[n])n++; if(t==4||n<6||!pf_eq(nm+n-5,".list"))continue;
        if(!b){ b=(char*)kmalloc((unsigned long)cap); if(!b)return 0; }
        char lp[120],*q=lp; q=scopy(q,"/var/lib/nvpkg/"); q=scopy(q,nm); *q=0; int r=ux_read_file(lp,b,cap); if(r<=0)continue;
        int hit=0; for(int s=0;s+L<=r;s++){ if((s==0||b[s-1]=='\n')&&(s+L==r||b[s+L]=='\n'||b[s+L]=='\r')){ int k=0; while(k<L&&b[s+k]==path[k])k++; if(k==L){ hit=1; break; } } }
        if(!hit)continue;
        int pl=n-5; if(pl>47)pl=47; for(int k=0;k<pl;k++)pkg[k]=nm[k]; pkg[pl]=0;
        u32 sz=0; int s=0; while(s<r){ int e=s; while(e<r&&b[e]!='\n')e++; char f[160]; int fl=e-s; if(fl>159)fl=159; for(int k=0;k<fl;k++)f[k]=b[s+k]; f[fl]=0; s=e+1; if(fl<2||f[0]!='/')continue; int isd=0; int fk=ux_lookup(f,&isd,0); if(fk>=0&&!isd)sz+=(u32)FS[fk].len; }
        *size=sz; kfree(b); return 1; }
    if(b)kfree(b); return 0; }

static void pf_mkdir(const char* parent,const char* name){ dt_init(); int d=dt_resolve_dir(parent); if(d<0)return; if(dt_child(d,name)<0)dt_mkdir_at(d,name); }

static void pf_write_info(pf_app* a){
    char dir[80],*p=dir; p=scopy(p,"/Programs/"); p=scopy(p,a->folder); *p=0; pf_mkdir("/Programs",a->folder);
    char path[100]; p=path; p=scopy(p,dir); p=scopy(p,"/app.info"); *p=0;
    char b[640]; p=b;
    p=scopy(p,"Name="); p=scopy(p,a->name); p=scopy(p,"\nVersion="); p=scopy(p,a->ver); p=scopy(p,"\nPublisher="); p=scopy(p,a->pub);
    p=scopy(p,"\nKind="); p=scopy(p,a->kind==PF_BUILTIN?"built-in":a->kind==PF_LINUX?"Linux app (part of NoovexOS)":"installed package");
    if(a->pkg[0]){ p=scopy(p,"\nPackage="); p=scopy(p,a->pkg); }
    p=scopy(p,"\nSize="); if(a->kind==PF_BUILTIN)p=scopy(p,"part of the kernel"); else { p=udec(p,a->size); p=scopy(p," bytes"); }
    p=scopy(p,"\nIcon="); p=scopy(p,a->icon[0]?a->icon:"builtin");
    p=scopy(p,"\nCommand="); p=scopy(p,a->cmd[0]?a->cmd:"(Start menu or Settings > Apps)");
    p=scopy(p,"\nDescription="); p=scopy(p,a->desc);
    if(a->hidden)p=scopy(p,"\nHidden=yes");
    if(a->desktop[0]){ p=scopy(p,"\nDesktopFile="); p=scopy(p,a->desktop); }
    *p++='\n'; *p=0; int n=(int)(p-b);
    char old[640]; int on=ux_read_file(path,old,640); if(on==n){ int same=1; for(int i=0;i<n;i++) if(old[i]!=b[i]){ same=0; break; } if(same)return; }
    ux_write_text(path,b,n,0100644u); }

static void pf_prune(void){
    char nm[80]; int t=0; char gone[16][64]; int ng=0;
    for(int i=0;i<512&&k_readdir_at("/Programs",i,nm,&t)==1;i++){ if(t!=4)continue; int keep=0; for(int k=0;k<g_npf;k++) if(pf_eq(g_pf[k].folder,nm)){ keep=1; break; } if(!keep&&ng<16)pf_cp(gone[ng++],nm,64); }
    for(int g=0;g<ng;g++){ char p1[120],*p=p1; p=scopy(p,"/Programs/"); p=scopy(p,gone[g]); *p=0; char p2[140]; p=p2; p=scopy(p,p1); p=scopy(p,"/app.info"); *p=0; k_unlink(p2); k_rmdir(p1); } }

void progfiles_sync(int force){
    if(force)g_apps_gen++;
    int files=0, layers=0; sysroot_stats(&files,&layers); int key=files*131071+g_apps_gen;
    if(!force&&key==g_pf_key)return; g_pf_key=key; g_npf=0;
    char ver[16]; pf_cp(ver,(const char*)g_buildsig.version,16);
    for(int i=0;i<NPB&&g_npf<PF_MAX;i++){ pf_app* a=&g_pf[g_npf++]; for(int k=0;k<(int)sizeof(*a);k++)((char*)a)[k]=0;
        a->kind=PF_BUILTIN; a->win=PB[i].win; a->xi=-1; a->hidden=PB[i].hidden; pf_cp(a->name,PB[i].folder,48); pf_folder(PB[i].folder,a->folder); pf_cp(a->ver,ver,16); pf_cp(a->pub,"Noovex",32); pf_cp(a->cmd,PB[i].cmd,100); pf_cp(a->desc,PB[i].desc,96); }
    int nx=xapps_count();
    for(int i=0;i<nx&&g_npf<PF_MAX;i++){ const char *nm,*ex,*ic,*pa; if(!xapps_get(i,&nm,&ex,&ic,&pa))continue;
        char fo[48]; pf_folder(nm,fo); int dup=0; for(int k=0;k<g_npf;k++) if(pf_eq(g_pf[k].folder,fo)){ dup=1; break; } if(dup)continue;
        pf_app* a=&g_pf[g_npf++]; for(int k=0;k<(int)sizeof(*a);k++)((char*)a)[k]=0;
        a->win=-1; a->xi=i; pf_cp(a->name,nm,48); pf_cp(a->folder,fo,48); pf_cp(a->cmd,ex,100); pf_cp(a->icon,ic,64); pf_cp(a->desktop,pa,100);
        if(pf_owner(pa,a->pkg,&a->size)){ a->kind=PF_PACKAGE; pf_cp(a->ver,"installed with apt",16); pf_cp(a->pub,"Debian package",32); pf_cp(a->desc,"Installed with apt",96); }
        else { a->kind=PF_LINUX; pf_cp(a->ver,ver,16); pf_cp(a->pub,"NoovexOS Linux files",32); pf_cp(a->desc,"Linux program included with NoovexOS",96); } }
    pf_mkdir("/","Programs");
    for(int i=0;i<g_npf;i++)pf_write_info(&g_pf[i]);
    pf_prune();
    { char b[80],*p=b; p=scopy(p,"programs: "); p=udec(p,(u32)g_npf); p=scopy(p," apps listed in /Programs"); *p=0; klog(b); } }

int progfiles_count(void){ progfiles_sync(0); return g_npf; }
const pf_app* progfiles_get(int i){ return (i>=0&&i<g_npf)?&g_pf[i]:0; }
