#include "include/nvx/base.h"
#include "include/nvx/arch/x86_64/percpu.h"
#include "include/nvx/mm/heap.h"
#include "include/nvx/lib/fmt.h"
#include "include/nvx/gui/theme.h"
#include "include/nvx/drivers/video/framebuffer.h"
#include "include/nvx/shell/terminal.h"
#include "include/nvx/fs/ramfs.h"
#include "include/nvx/fs/dirtree.h"
#include "include/nvx/fs/vfs.h"
#include "include/nvx/fs/unixfs.h"
#include "include/nvx/fs/sysroot.h"
#include "include/nvx/net/http.h"
#include "include/nvx/net/tcpip.h"
#include "include/nvx/lib/crypto/nvxcrypto.h"
#include "include/nvx/fs/fat_ata.h"

extern u8* unp_auto(const u8* in,long n,long* outlen);
extern void sh_flush(void);
extern int sh_yesno(int def);

#define AP_LISTDIR "/var/lib/apt/lists"
#define AP_PKGDIR "/var/lib/nvpkg"
#define AP_STATUS "/var/lib/dpkg/status"
#define AP_SOURCES "/etc/apt/sources.list"
#define AP_LOCALREPO "/usr/share/noovex/repo/"
#define AP_DEFSRC "deb http://deb.debian.org/debian trixie main contrib non-free non-free-firmware\n"

typedef struct { const char* p; int n; } sv;
typedef struct { sv name, ver, arch, dep, pre, prov, file, sha, desc, ess, prio, isize, stanza; long size; int src, next; } prec;
typedef struct { char name[64]; char ver[64]; char arch[16]; char desc[96]; char dep[256]; int autoi; } inst_t;

static char g_srcbase[16][160]; static int g_nsrc;
static u8* g_idxtxt[16]; static long g_idxlen[16]; static int g_nidx;
static prec* g_pr; static int g_npr, g_cpr;
static int* g_nh; static int g_nhcap;
static int* g_pv; static int* g_pvn; static int g_npv, g_cpv;
static int g_idx_loaded; static int g_have_remote;
static inst_t* g_in; static int g_nin, g_cin;

static int asl(const char* s){ int n=0; while(s[n])n++; return n; }
static int sveq(sv a,const char* b){ int n=asl(b); if(a.n!=n)return 0; for(int i=0;i<n;i++) if(a.p[i]!=b[i])return 0; return 1; }
static int sveqv(sv a,sv b){ if(a.n!=b.n)return 0; for(int i=0;i<a.n;i++) if(a.p[i]!=b.p[i])return 0; return 1; }
static void svcpy(char* d,sv s,int cap){ int n=s.n<cap-1?s.n:cap-1; for(int i=0;i<n;i++)d[i]=s.p[i]; d[n]=0; }
static sv svs(const char* s){ sv r; r.p=s; r.n=asl(s); return r; }
static u32 hsv(sv s){ u32 h=2166136261u; for(int i=0;i<s.n;i++)h=(h^(u8)s.p[i])*16777619u; return h; }
static void AP(const char* s){ term_puts(s); }
static void APL(const char* s){ term_puts(s); term_putc('\n'); sh_flush(); }
static void APN(long v){ char b[24],*p=b; if(v<0){ *p++='-'; v=-v; } p=udec(p,(u32)v); *p=0; term_puts(b); }
static void APSV(sv s){ char b[256]; svcpy(b,s,256); term_puts(b); }
static void APE(const char* a,const char* b){ tcol(rgb(240,120,120)); AP("E: "); AP(a); if(b)AP(b); term_putc('\n'); tcol(WHT); sh_flush(); }
static void APW(const char* a,const char* b){ tcol(rgb(240,200,110)); AP("W: "); AP(a); if(b)AP(b); term_putc('\n'); tcol(WHT); sh_flush(); }
static void Psize(long b){ if(b>=1000000){ APN(b/1000000); term_putc('.'); APN((b/100000)%10); AP(" MB"); } else if(b>=1000){ APN(b/1000); AP(" kB"); } else { APN(b); AP(" B"); } }
static char* sj(char* d,const char* a){ while(*a)*d++=*a++; *d=0; return d; }

static void mkdirp(const char* path){
    dt_init(); int cur=0; char comp[64]; int c=0; const char* a=path; if(*a=='/')a++;
    for(;;a++){ if(*a=='/'||!*a){ comp[c]=0; if(c){ int d=dt_child(cur,comp); if(d<0)d=dt_mkdir_at(cur,comp); if(d<0)return; cur=d; } c=0; if(!*a)break; continue; } if(c<63)comp[c++]=*a; }
    dt_save(); }
static int wfile(const char* path,const u8* data,long n){
    char dir[160]; int L=asl(path); int s=L; while(s>0&&path[s-1]!='/')s--; for(int i=0;i<s-1&&i<159;i++)dir[i]=path[i]; dir[s>1?s-1:0]=0; if(dir[0])mkdirp(dir);
    k_unlink(path);
    int fd=vfs_open(path,O_CREAT|O_TRUNC|O_WRONLY); if(fd<0)return 0;
    long o=0; while(o<n){ int ch=(int)((n-o)>(1<<20)?(1<<20):(n-o)); int w=vfs_write(fd,(const char*)data+o,ch); if(w<=0){ vfs_close(fd); return 0; } o+=w; }
    vfs_close(fd); return 1; }
static const u8* rfile(const char* path,long* n){ int k=k_find_file(path); if(k<0)return 0; *n=FS[k].len; return (const u8*)fsd(k); }

static int ap_hexval(char c){ if(c>='0'&&c<='9')return c-'0'; if(c>='a'&&c<='f')return c-'a'+10; if(c>='A'&&c<='F')return c-'A'+10; return -1; }
static int ap_dig(char c){ return c>='0'&&c<='9'; }
static int ap_alpha(char c){ return (c>='a'&&c<='z')||(c>='A'&&c<='Z'); }
static int vorder(char c){ if(ap_dig(c))return 0; if(ap_alpha(c))return c; if(c=='~')return -1; if(c)return c+256; return 0; }
static int verrev(const char* a,int an,const char* b,int bn){
    int i=0,j=0;
    while(i<an||j<bn){ int fd=0;
        while((i<an&&!ap_dig(a[i]))||(j<bn&&!ap_dig(b[j]))){ int ac=i<an?vorder(a[i]):0, bc=j<bn?vorder(b[j]):0; if(ac!=bc)return ac-bc; i++; j++; }
        while(i<an&&a[i]=='0')i++; while(j<bn&&b[j]=='0')j++;
        while(i<an&&ap_dig(a[i])&&j<bn&&ap_dig(b[j])){ if(!fd)fd=a[i]-b[j]; i++; j++; }
        if(i<an&&ap_dig(a[i]))return 1; if(j<bn&&ap_dig(b[j]))return -1; if(fd)return fd; }
    return 0; }
static int vcmp(sv a,sv b){
    long ea=0,eb=0; int ca=-1,cb=-1; for(int i=0;i<a.n;i++) if(a.p[i]==':'){ ca=i; break; } for(int i=0;i<b.n;i++) if(b.p[i]==':'){ cb=i; break; }
    if(ca>=0)for(int i=0;i<ca;i++)ea=ea*10+(a.p[i]-'0'); if(cb>=0)for(int i=0;i<cb;i++)eb=eb*10+(b.p[i]-'0');
    if(ea!=eb)return ea<eb?-1:1;
    const char* ap=a.p+ca+1; int an=a.n-ca-1; const char* bp=b.p+cb+1; int bn=b.n-cb-1;
    int ha=-1,hb=-1; for(int i=an-1;i>=0;i--) if(ap[i]=='-'){ ha=i; break; } for(int i=bn-1;i>=0;i--) if(bp[i]=='-'){ hb=i; break; }
    int r=verrev(ap,ha>=0?ha:an,bp,hb>=0?hb:bn); if(r)return r;
    return verrev(ha>=0?ap+ha+1:"",ha>=0?an-ha-1:0,hb>=0?bp+hb+1:"",hb>=0?bn-hb-1:0); }

static void st_load(void){
    g_nin=0; long n=0; const u8* d=rfile(AP_STATUS,&n); if(!d)return;
    const char* s=(const char*)d; const char* e=s+n; inst_t cur; int have=0; for(int i=0;i<(int)sizeof(cur);i++)((char*)&cur)[i]=0;
    for(;;){ const char* l=s; while(s<e&&*s!='\n')s++; int ln=(int)(s-l); int end=(s>=e);
        if(ln==0){ if(have&&cur.name[0]){ if(g_nin>=g_cin){ int nc=g_cin?g_cin*2:64; inst_t* x=(inst_t*)krealloc(g_in,sizeof(inst_t)*(unsigned long)nc); if(!x)return; g_in=x; g_cin=nc; } g_in[g_nin++]=cur; } have=0; for(int i=0;i<(int)sizeof(cur);i++)((char*)&cur)[i]=0; }
        else { sv L; L.p=l; L.n=ln; int c=0; while(c<ln&&l[c]!=':')c++; if(c<ln&&l[0]!=' '){ sv k; k.p=l; k.n=c; sv v; v.p=l+c+1; v.n=ln-c-1; while(v.n&&*v.p==' '){ v.p++; v.n--; }
                have=1; if(sveq(k,"Package"))svcpy(cur.name,v,64); else if(sveq(k,"Version"))svcpy(cur.ver,v,64); else if(sveq(k,"Architecture"))svcpy(cur.arch,v,16); else if(sveq(k,"Description"))svcpy(cur.desc,v,96); else if(sveq(k,"Depends"))svcpy(cur.dep,v,256); else if(sveq(k,"Auto-Installed"))cur.autoi=(v.n&&v.p[0]=='1'); } (void)L; }
        if(end)break; s++; }
    if(have&&cur.name[0]){ if(g_nin>=g_cin){ int nc=g_cin?g_cin*2:64; inst_t* x=(inst_t*)krealloc(g_in,sizeof(inst_t)*(unsigned long)nc); if(!x)return; g_in=x; g_cin=nc; } g_in[g_nin++]=cur; } }
static void st_save(void){
    long cap=(long)g_nin*600+16; char* b=(char*)kmalloc((unsigned long)cap); if(!b)return; char* p=b;
    for(int i=0;i<g_nin;i++){ inst_t* x=&g_in[i]; p=sj(p,"Package: "); p=sj(p,x->name); p=sj(p,"\nStatus: install ok installed\nVersion: "); p=sj(p,x->ver); p=sj(p,"\nArchitecture: "); p=sj(p,x->arch[0]?x->arch:"amd64");
        if(x->dep[0]){ p=sj(p,"\nDepends: "); p=sj(p,x->dep); } if(x->autoi)p=sj(p,"\nAuto-Installed: 1"); p=sj(p,"\nDescription: "); p=sj(p,x->desc); p=sj(p,"\n\n"); }
    wfile(AP_STATUS,(const u8*)b,(long)(p-b)); kfree(b); }
static int st_find(sv name){ for(int i=0;i<g_nin;i++) if(sveq(name,g_in[i].name))return i; return -1; }

static const char* const g_prot[]={"libc6","libc-bin","libc6-x32","libgcc-s1","gcc-14-base","gcc-13-base","python3","python3-minimal","python3.13","python3.13-minimal","libpython3.13-minimal","libpython3.13-stdlib","libpython3-stdlib","python3-venv","python3.13-venv","bash","coreutils","dpkg","base-files","base-passwd","libcrypt1","debconf","init-system-helpers","apt","usrmerge","sensible-utils","debianutils",0};
static int is_prot(sv n){ for(int i=0;g_prot[i];i++) if(sveq(n,g_prot[i]))return 1; return 0; }

static void src_load(void){
    g_nsrc=0; long n=0; const u8* d=rfile(AP_SOURCES,&n);
    if(!d){ wfile(AP_SOURCES,(const u8*)AP_DEFSRC,asl(AP_DEFSRC)); d=rfile(AP_SOURCES,&n); if(!d){ d=(const u8*)AP_DEFSRC; n=asl(AP_DEFSRC); } }
    const char* s=(const char*)d; const char* e=s+n;
    while(s<e&&g_nsrc<16){ const char* l=s; while(s<e&&*s!='\n')s++; const char* le=s; if(s<e)s++;
        while(l<le&&(*l==' '||*l=='\t'))l++; if(le-l<4||l[0]!='d'||l[1]!='e'||l[2]!='b'||(l[3]!=' '&&l[3]!='\t'))continue; l+=3;
        char tok[8][96]; int nt=0;
        while(l<le&&nt<8){ while(l<le&&(*l==' '||*l=='\t'))l++; if(l>=le)break; if(*l=='['){ while(l<le&&*l!=']')l++; if(l<le)l++; continue; } int k=0; while(l<le&&*l!=' '&&*l!='\t'&&k<95)tok[nt][k++]=*l++; tok[nt][k]=0; nt++; }
        if(nt<2)continue;
        for(int c=2;c<nt||(c==2&&nt==2);c++){ if(g_nsrc>=16)break; char* p=g_srcbase[g_nsrc]; p=sj(p,tok[0]); if(p[-1]!='/'){ *p++='/'; *p=0; }
            if(nt==2){ int sl2=asl(tok[1]); if(sl2&&tok[1][sl2-1]=='/'){ p=sj(p,tok[1]); p=sj(p,"|flat"); } else { p=sj(p,"dists/"); p=sj(p,tok[1]); p=sj(p,"/main"); } }
            else { p=sj(p,"dists/"); p=sj(p,tok[1]); p=sj(p,"/"); p=sj(p,tok[c]); }
            g_nsrc++; if(nt==2)break; } } }

static int url_split(const char* url,char* host,int* port,char* path,int* tls){
    const char* u=url; *tls=0; *port=80;
    if(u[0]=='h'&&u[1]=='t'&&u[2]=='t'&&u[3]=='p'){ if(u[4]=='s'){ *tls=1; *port=443; u+=8; } else u+=7; } else return 0;
    int h=0; while(*u&&*u!='/'&&*u!=':'&&h<127)host[h++]=*u++; host[h]=0;
    if(*u==':'){ u++; int p=0; while(ap_dig(*u))p=p*10+(*u++-'0'); *port=p; }
    int q=0; if(*u!='/')path[q++]='/'; while(*u&&q<511)path[q++]=*u++; path[q]=0; return h>0; }
static const char* g_fetch_err;
static u8* ap_get(const char* url0,long* outlen,long hint){
    if(url0[0]=='f'&&url0[1]=='i'&&url0[2]=='l'&&url0[3]=='e'&&url0[4]==':'){ const char* pth=url0+5; while(pth[0]=='/'&&pth[1]=='/')pth++; u32 L=0; const u8* d=sysroot_get(pth,&L,0); long n=L; if(!d)d=rfile(pth,&n); if(!d){ g_fetch_err="404  Not Found"; return 0; }
        u8* o=(u8*)kmalloc((unsigned long)(n?n:1)); if(!o){ g_fetch_err="out of memory"; return 0; } for(long i=0;i<n;i++)o[i]=d[i]; *outlen=n; (void)hint; return o; }
    char url[600]; { int i=0; for(;url0[i]&&i<599;i++)url[i]=url0[i]; url[i]=0; }
    for(int hop=0;hop<5;hop++){
        char host[128],path[512]; int port,tls; if(!url_split(url,host,&port,path,&tls)){ g_fetch_err="unsupported URL"; return 0; }
        long cap=hint>0?hint+262144:(64L<<20); u8* buf=(u8*)kmalloc((unsigned long)cap); if(!buf){ g_fetch_err="out of memory"; return 0; }
        int n;
        if(tls){ n=https_fetch_to(host,path,buf,(int)cap); }
        else { u32 ip; if(!parse_ip(host,&ip)){ if(dns_query(host,&ip)<=0){ kfree(buf); g_fetch_err="Temporary failure resolving host"; return 0; } }
            char req[900]; char* p=req; p=sj(p,"GET "); p=sj(p,path); p=sj(p," HTTP/1.0\r\nHost: "); p=sj(p,host); if(port!=80){ *p++=':'; p=udec(p,(u32)port); *p=0; }
            p=sj(p,"\r\nUser-Agent: Debian APT-HTTP/1.3 (NoovexOS)\r\nAccept: */*\r\nConnection: close\r\n\r\n");
            n=tcp_get(ip,(u16)port,req,(int)(p-req),buf,(int)cap); }
        if(n<=0){ kfree(buf); g_fetch_err=n==-3?"Connection timed out":n==-4?"Connection refused":n==-1?"No network card":n==-2?"No route to gateway":"Connection failed"; return 0; }
        if(n>=cap-1){ kfree(buf); g_fetch_err="file too large"; return 0; }
        int he=-1; for(int i=0;i+3<n;i++) if(buf[i]=='\r'&&buf[i+1]=='\n'&&buf[i+2]=='\r'&&buf[i+3]=='\n'){ he=i; break; }
        if(he<0||n<12){ kfree(buf); g_fetch_err="bad HTTP reply"; return 0; }
        int code=0; { int i=0; while(i<he&&buf[i]!=' ')i++; i++; while(i<he&&ap_dig((char)buf[i]))code=code*10+(buf[i++]-'0'); }
        int chunked=0; char loc[600]; loc[0]=0;
        for(int i=0;i<he;i++){ if(buf[i]!='\n')continue; const char* h=(const char*)buf+i+1; int ok=1; const char* L="location:"; for(int k=0;L[k];k++){ char c=h[k]; if(c>='A'&&c<='Z')c+=32; if(c!=L[k]){ ok=0; break; } }
            if(ok){ const char* v=h+9; while(*v==' ')v++; int k=0; while(v[k]&&v[k]!='\r'&&v[k]!='\n'&&k<599){ loc[k]=v[k]; k++; } loc[k]=0; }
            const char* T="transfer-encoding: chunked"; int ok2=1; for(int k=0;T[k];k++){ char c=h[k]; if(c>='A'&&c<='Z')c+=32; if(c!=T[k]){ ok2=0; break; } } if(ok2)chunked=1; }
        if(code>=300&&code<400&&loc[0]){ kfree(buf);
            if(loc[0]=='/'){ char nu[600]; char* p=nu; p=sj(p,tls?"https://":"http://"); p=sj(p,host); if(port!=(tls?443:80)){ *p++=':'; p=udec(p,(u32)port); *p=0; } p=sj(p,loc); for(int i=0;i<600;i++){ url[i]=nu[i]; if(!nu[i])break; } }
            else for(int i=0;i<600;i++){ url[i]=loc[i]; if(!loc[i])break; }
            continue; }
        if(code!=200){ kfree(buf); g_fetch_err=code==404?"404  Not Found":"HTTP error"; return 0; }
        long bo=he+4, bl=n-bo;
        if(chunked){ long r=bo,w=0; for(;;){ long sz=0; while(r<n&&buf[r]!='\r'){ int v=ap_hexval((char)buf[r]); if(v<0)break; sz=sz*16+v; r++; } while(r<n&&buf[r]!='\n')r++; r++; if(sz<=0||r+sz>n)break; for(long k=0;k<sz;k++)buf[bo+w+k]=buf[r+k]; w+=sz; r+=sz+2; } bl=w; }
        u8* out=(u8*)kmalloc((unsigned long)(bl?bl:1)); if(!out){ kfree(buf); g_fetch_err="out of memory"; return 0; }
        for(long i=0;i<bl;i++)out[i]=buf[bo+i]; kfree(buf); *outlen=bl; return out; }
    g_fetch_err="too many redirects"; return 0; }

static int pv_add(int rec){ if(g_npv>=g_cpv){ int nc=g_cpv?g_cpv*2:4096; int* a=(int*)krealloc(g_pv,sizeof(int)*(unsigned long)nc); int* b=(int*)krealloc(g_pvn,sizeof(int)*(unsigned long)nc); if(!a||!b)return 0; g_pv=a; g_pvn=b; g_cpv=nc; } g_pv[g_npv]=rec; g_pvn[g_npv]=0; g_npv++; return 1; }
static void idx_free(void){
    for(int i=0;i<g_nidx;i++) if(g_idxtxt[i])kfree(g_idxtxt[i]); g_nidx=0; g_npr=0; g_npv=0; g_idx_loaded=0; }
static void idx_parse(int src,const char* t,long n){
    const char* s=t; const char* e=t+n;
    while(s<e){ while(s<e&&*s=='\n')s++; if(s>=e)break;
        prec r; for(int i=0;i<(int)sizeof(r);i++)((char*)&r)[i]=0; r.src=src; r.stanza.p=s; const char* lastk=0; int lastkn=0;
        while(s<e&&*s!='\n'){ const char* l=s; while(s<e&&*s!='\n')s++; int ln=(int)(s-l); if(s<e)s++;
            if(l[0]==' '||l[0]=='\t'){ (void)lastk; (void)lastkn; continue; }
            int c=0; while(c<ln&&l[c]!=':')c++; if(c>=ln)continue; sv k; k.p=l; k.n=c; sv v; v.p=l+c+1; v.n=ln-c-1; while(v.n&&*v.p==' '){ v.p++; v.n--; } lastk=l; lastkn=c;
            switch(k.p[0]){
            case 'P': if(sveq(k,"Package"))r.name=v; else if(sveq(k,"Pre-Depends"))r.pre=v; else if(sveq(k,"Provides"))r.prov=v; else if(sveq(k,"Priority"))r.prio=v; break;
            case 'V': if(sveq(k,"Version"))r.ver=v; break;
            case 'A': if(sveq(k,"Architecture"))r.arch=v; break;
            case 'D': if(sveq(k,"Depends"))r.dep=v; else if(sveq(k,"Description"))r.desc=v; break;
            case 'F': if(sveq(k,"Filename"))r.file=v; break;
            case 'S': if(sveq(k,"SHA256"))r.sha=v; else if(sveq(k,"Size")){ long z=0; for(int i=0;i<v.n&&ap_dig(v.p[i]);i++)z=z*10+(v.p[i]-'0'); r.size=z; } break;
            case 'E': if(sveq(k,"Essential"))r.ess=v; break;
            case 'I': if(sveq(k,"Installed-Size"))r.isize=v; break; } }
        r.stanza.n=(int)(s-r.stanza.p);
        if(!r.name.n||!r.ver.n)continue;
        if(r.arch.n&&!sveq(r.arch,"amd64")&&!sveq(r.arch,"all"))continue;
        if(g_npr>=g_cpr){ int nc=g_cpr?g_cpr*2:8192; prec* x=(prec*)krealloc(g_pr,sizeof(prec)*(unsigned long)nc); if(!x)return; g_pr=x; g_cpr=nc; }
        g_pr[g_npr++]=r; } }
static void idx_hash(void){
    int need=1; while(need<g_npr*2+16)need<<=1;
    if(need>g_nhcap){ int* h=(int*)krealloc(g_nh,sizeof(int)*(unsigned long)need); if(!h)return; g_nh=h; g_nhcap=need; }
    for(int i=0;i<g_nhcap;i++)g_nh[i]=-1;
    for(int i=0;i<g_npr;i++){ u32 b=hsv(g_pr[i].name)&(u32)(g_nhcap-1); g_pr[i].next=g_nh[b]; g_nh[b]=i; }
    g_npv=0; for(int i=0;i<g_npr;i++) if(g_pr[i].prov.n)pv_add(i); }
static int idx_load(int quiet){
    if(g_idx_loaded){ if(!g_npr&&!quiet)APE("The package lists are empty - run 'sudo apt update' first",0); return g_npr; }
    idx_free(); src_load(); g_have_remote=0;
    for(int i=0;i<g_nsrc&&g_nidx<16;i++){ char fn[96]; char* p=fn; p=sj(p,AP_LISTDIR "/src"); p=udec(p,(u32)i); p=sj(p,".pkgs"); *p=0;
        long n=0; const u8* d=rfile(fn,&n); if(!d||n<8)continue;
        int hl=0; while(hl<n&&d[hl]!='\n')hl++; if(hl>=n)continue;
        int same=(hl-5==asl(g_srcbase[i])); if(same) for(int k=0;k<hl-5;k++) if(d[5+k]!=(u8)g_srcbase[i][k]){ same=0; break; }
        if(!same)continue;
        long ul=0; u8* t=unp_auto(d+hl+1,n-hl-1,&ul); if(!t)continue;
        g_idxtxt[g_nidx]=t; g_idxlen[g_nidx]=ul; idx_parse(i,(const char*)t,ul); g_nidx++; g_have_remote=1; }
    { u32 L=0; const u8* d=sysroot_get(AP_LOCALREPO "Packages",&L,0); if(d&&g_nidx<16){ char* p=g_srcbase[15]; p=sj(p,"file:" AP_LOCALREPO "|flat"); u8* t=(u8*)kmalloc(L+1); if(t){ for(u32 i=0;i<L;i++)t[i]=d[i]; t[L]=0; g_idxtxt[g_nidx]=t; g_idxlen[g_nidx]=L; idx_parse(15,(const char*)t,L); g_nidx++; } } }
    idx_hash(); g_idx_loaded=1;
    if(!g_npr&&!quiet){ APE("The package lists are empty - run 'sudo apt update' first",0); }
    return g_npr; }

static int idx_best(sv name){
    if(!g_nh)return -1; int best=-1; u32 b=hsv(name)&(u32)(g_nhcap-1);
    for(int i=g_nh[b];i>=0;i=g_pr[i].next) if(sveqv(g_pr[i].name,name)){ if(best<0||vcmp(g_pr[i].ver,g_pr[best].ver)>0)best=i; }
    return best; }
static int prov_has(sv list,sv name){
    const char* s=list.p; const char* e=s+list.n;
    while(s<e){ while(s<e&&(*s==' '||*s==','))s++; const char* a=s; while(s<e&&*s!=' '&&*s!=','&&*s!='(')s++; sv t; t.p=a; t.n=(int)(s-a);
        if(t.n&&sveqv(t,name))return 1; while(s<e&&*s!=',')s++; }
    return 0; }
static int idx_provider(sv name){ for(int i=0;i<g_npv;i++) if(prov_has(g_pr[g_pv[i]].prov,name))return g_pv[i]; return -1; }
static int idx_find(sv name,int* virt){ int r=idx_best(name); if(virt)*virt=0; if(r>=0)return r; r=idx_provider(name); if(r>=0&&virt)*virt=1; return r; }

static int g_plan[4096]; static int g_nplan; static char g_planauto[4096];
static int in_plan(sv n){ for(int i=0;i<g_nplan;i++) if(sveqv(g_pr[g_plan[i]].name,n))return 1; return 0; }
static int plan_provides(sv n){ for(int i=0;i<g_nplan;i++) if(g_pr[g_plan[i]].prov.n&&prov_has(g_pr[g_plan[i]].prov,n))return 1; return 0; }
static int sat_name(sv n){
    if(is_prot(n))return 1; if(st_find(n)>=0)return 1; if(in_plan(n)||plan_provides(n))return 1;
    int r=idx_best(n); if(r>=0&&(sveq(g_pr[r].ess,"yes")||sveq(g_pr[r].prio,"required")))return 1;
    if(r<0){ for(int i=0;i<g_nin;i++){ (void)i; } }
    return 0; }
static char g_unmet[160];
static int resolve(int rec,int autoi,int depth);
static int resolve_list(sv list,int depth){
    const char* s=list.p; const char* e=s+list.n;
    while(s<e){ const char* gs=s; while(s<e&&*s!=',')s++; const char* ge=s; if(s<e)s++;
        sv alts[8]; int na=0; const char* q=gs;
        while(q<ge&&na<8){ while(q<ge&&(*q==' '||*q=='|'))q++; const char* a=q; while(q<ge&&*q!=' '&&*q!='('&&*q!='|'&&*q!='['&&*q!='<'&&*q!=':')q++; sv t; t.p=a; t.n=(int)(q-a); while(q<ge&&*q!='|')q++; if(t.n)alts[na++]=t; }
        int ok=0; for(int i=0;i<na;i++) if(sat_name(alts[i])){ ok=1; break; }
        if(ok)continue;
        int pick=-1; for(int i=0;i<na&&pick<0;i++){ int v=0; int r=idx_find(alts[i],&v); if(r>=0)pick=r; }
        if(pick<0){ char* p=g_unmet; int k=0; for(;k<(int)(ge-gs)&&k<150;k++)p[k]=gs[k]; p[k]=0; return 0; }
        if(!resolve(pick,1,depth+1))return 0; }
    return 1; }
static int resolve(int rec,int autoi,int depth){
    if(depth>40)return 1; if(in_plan(g_pr[rec].name))return 1;
    if(g_nplan>=4096){ char* p=g_unmet; p=sj(p,"too many packages"); return 0; }
    int me=g_nplan; g_plan[g_nplan]=rec; g_planauto[g_nplan]=(char)autoi; g_nplan++;
    if(g_pr[rec].pre.n&&!resolve_list(g_pr[rec].pre,depth))return 0;
    if(g_pr[rec].dep.n&&!resolve_list(g_pr[rec].dep,depth))return 0;
    int self=g_plan[me]; char sa=g_planauto[me]; for(int i=me;i<g_nplan-1;i++){ g_plan[i]=g_plan[i+1]; g_planauto[i]=g_planauto[i+1]; } g_plan[g_nplan-1]=self; g_planauto[g_nplan-1]=sa;
    return 1; }

typedef struct { char* name; char* tgt; u32 mode; long off; long len; } lent;
static lent* g_le; static int g_nle, g_cle; static u8* g_ldata; static long g_nld, g_cld;
__attribute__((unused)) static int le_push(const char* name,const char* tgt,u32 mode,const u8* data,long len){
    if(g_nle>=g_cle){ int nc=g_cle?g_cle*2:256; lent* x=(lent*)krealloc(g_le,sizeof(lent)*(unsigned long)nc); if(!x)return 0; g_le=x; g_cle=nc; }
    if(len>0){ if(g_nld+len>g_cld){ long nc=g_cld?g_cld*2:(1<<20); while(nc<g_nld+len)nc*=2; u8* x=(u8*)krealloc(g_ldata,(unsigned long)nc); if(!x)return 0; g_ldata=x; g_cld=nc; } for(long i=0;i<len;i++)g_ldata[g_nld+i]=data[i]; }
    int nl=asl(name), tl=tgt?asl(tgt):0; char* a=(char*)kmalloc((unsigned long)nl+1); char* b=(char*)kmalloc((unsigned long)tl+1); if(!a||!b)return 0;
    for(int i=0;i<=nl;i++)a[i]=name[i]; for(int i=0;i<tl;i++)b[i]=tgt[i]; b[tl]=0;
    lent* e=&g_le[g_nle++]; e->name=a; e->tgt=b; e->mode=mode; e->off=g_nld; e->len=len; g_nld+=len>0?len:0; return 1; }
__attribute__((unused)) static void le_reset(void){ for(int i=0;i<g_nle;i++){ kfree(g_le[i].name); kfree(g_le[i].tgt); } g_nle=0; g_nld=0; }
static long aoct(const char* s,int n){ long v=0; for(int i=0;i<n&&s[i];i++){ if(s[i]==' ')continue; if(s[i]<'0'||s[i]>'7')break; v=(v<<3)+(s[i]-'0'); } return v; }
static void absname(const char* in,char* out){ int o=0; out[o++]='/'; const char* p=in; while(*p=='.'&&p[1]=='/')p+=2; while(*p=='/')p++; while(*p&&o<250)out[o++]=*p++; while(o>1&&out[o-1]=='/')o--; out[o]=0; }
static void abslink(const char* name,const char* tgt,char* out){
    if(tgt[0]=='/'){ int k=0; for(;tgt[k]&&k<250;k++)out[k]=tgt[k]; out[k]=0; return; }
    char tmp[512]; int o=0; int L=asl(name); int s=L; while(s>0&&name[s-1]!='/')s--; for(int i=0;i<s&&o<250;i++)tmp[o++]=name[i]; for(int i=0;tgt[i]&&o<500;i++)tmp[o++]=tgt[i]; tmp[o]=0;
    char st[64][64]; int ns=0; const char* p=tmp;
    while(*p){ while(*p=='/')p++; if(!*p)break; const char* a=p; int l=0; while(p[l]&&p[l]!='/')l++; p+=l;
        if(l==1&&a[0]=='.')continue; if(l==2&&a[0]=='.'&&a[1]=='.'){ if(ns)ns--; continue; } if(ns<64){ int k=0; for(;k<l&&k<63;k++)st[ns][k]=a[k]; st[ns][k]=0; ns++; } }
    o=0; for(int i=0;i<ns;i++){ out[o++]='/'; for(int k=0;st[i][k]&&o<250;k++)out[o++]=st[i][k]; } if(!o)out[o++]='/'; out[o]=0; }
static char* g_man; static long g_man_n, g_man_cap; static long g_man_files;
static void man_reset(void){ g_man_n=0; g_man_files=0; if(g_man)g_man[0]=0; }
static void man_add(const char* path){
    long L=asl(path); if(g_man_n+L+2>g_man_cap){ long nc=g_man_cap?g_man_cap*2:4096; while(nc<g_man_n+L+2)nc*=2; char* b=(char*)krealloc(g_man,(unsigned long)nc); if(!b)return; g_man=b; g_man_cap=nc; }
    for(long i=0;i<L;i++)g_man[g_man_n++]=path[i]; g_man[g_man_n++]='\n'; g_man[g_man_n]=0; g_man_files++; }
static int disk_put_file(const char* an,const u8* data,long sz,unsigned mode){
    if(!wfile(an,data,sz))return 0; if(mode&0111)ux_chmod(an,0100755u); man_add(an); return 1; }
static int disk_put_link(const char* an,const char* tg){
    k_unlink(an); { char dir[160]; int L=asl(an); int e=L; while(e>0&&an[e-1]!='/')e--; if(e>1){ for(int i=0;i<e-1&&i<159;i++)dir[i]=an[i]; dir[e-1]=0; mkdirp(dir); } }
    if(ux_symlink(tg,an)<0)return 0; man_add(an); return 1; }

static int tar_scan(const u8* t,long n,int collect,const char* want,const u8** wdata,long* wlen){
    long off=0; char lname[512]; lname[0]=0; char llink[512]; llink[0]=0; int cnt=0;
    while(off+512<=n){ const char* h=(const char*)(t+off); int empty=1; for(int i=0;i<512;i++) if(h[i]){ empty=0; break; } if(empty)break;
        long sz=aoct(h+124,12); if((u8)h[124]&0x80){ sz=0; for(int i=4;i<12;i++)sz=(sz<<8)|(u8)h[124+i]; }
        char ty=h[156]; const u8* data=t+off+512; long adv=512+((sz+511)/512)*512;
        if(off+512+sz>n)break;
        if(ty=='L'){ int k=0; for(;k<sz&&k<511&&data[k];k++)lname[k]=(char)data[k]; lname[k]=0; off+=adv; continue; }
        if(ty=='K'){ int k=0; for(;k<sz&&k<511&&data[k];k++)llink[k]=(char)data[k]; llink[k]=0; off+=adv; continue; }
        if(ty=='x'){ long q=0; while(q<sz){ long len=0; long r=q; while(r<sz&&ap_dig((char)data[r])){ len=len*10+(data[r]-'0'); r++; } if(len<=0)break; r++; const char* kv=(const char*)data+r; long kl=len-(r-q);
                if(kl>5&&kv[0]=='p'&&kv[1]=='a'&&kv[2]=='t'&&kv[3]=='h'&&kv[4]=='='){ int k=0; for(;k<kl-6&&k<511;k++)lname[k]=kv[5+k]; lname[k]=0; }
                if(kl>9&&kv[0]=='l'&&kv[1]=='i'&&kv[2]=='n'&&kv[3]=='k'&&kv[4]=='p'&&kv[5]=='a'&&kv[6]=='t'&&kv[7]=='h'&&kv[8]=='='){ int k=0; for(;k<kl-10&&k<511;k++)llink[k]=kv[9+k]; llink[k]=0; }
                q+=len; } off+=adv; continue; }
        if(ty=='g'){ off+=adv; continue; }
        char name[512]; if(lname[0]){ int k=0; for(;lname[k];k++)name[k]=lname[k]; name[k]=0; }
        else { int nn=0; const char* pre=h+345; if(pre[0]&&h[257]=='u'&&h[258]=='s'){ for(int i=0;i<155&&pre[i];i++)name[nn++]=pre[i]; if(nn)name[nn++]='/'; } for(int i=0;i<100&&h[i];i++)name[nn++]=h[i]; name[nn]=0; }
        char link[512]; if(llink[0]){ int k=0; for(;llink[k];k++)link[k]=llink[k]; link[k]=0; } else { int k=0; for(;k<100&&h[157+k];k++)link[k]=h[157+k]; link[k]=0; }
        lname[0]=0; llink[0]=0;
        unsigned mode=(unsigned)aoct(h+100,8);
        char an[256]; absname(name,an);
        if(want){ char wn[256]; absname(want,wn); if((ty=='0'||ty==0||ty=='7')&&asl(an)==asl(wn)){ int eq=1; for(int i=0;an[i];i++) if(an[i]!=wn[i]){ eq=0; break; } if(eq){ *wdata=data; *wlen=sz; return 1; } } }
        else if(collect==2){
            if(ty=='0'||ty==0||ty=='7'){ if(!disk_put_file(an,data,sz,mode))return -1; cnt++; }
            else if(ty=='2'){ char tg[256]; abslink(an,link,tg); if(!disk_put_link(an,tg))return -1; cnt++; }
            else if(ty=='1'){ char ln2[256]; absname(link,ln2); long tn=0; const u8* td=rfile(ln2,&tn); if(td){ u8* cp=(u8*)kmalloc((unsigned long)(tn?tn:1)); if(cp){ for(long i=0;i<tn;i++)cp[i]=td[i]; disk_put_file(an,cp,tn,mode); kfree(cp); cnt++; } } }
            else if(ty=='5'){ mkdirp(an); } }
        else if(collect){
            if(ty=='0'||ty==0||ty=='7'){ if(!le_push(an,0,(mode&0111)?1:0,data,sz))return -1; cnt++; }
            else if(ty=='2'){ char tg[256]; abslink(an,link,tg); if(!le_push(an,tg,2,0,0))return -1; cnt++; }
            else if(ty=='1'){ char ln2[256]; absname(link,ln2); int f=-1; for(int i=0;i<g_nle;i++) if(asl(g_le[i].name)==asl(ln2)){ int eq=1; for(int k=0;ln2[k];k++) if(g_le[i].name[k]!=ln2[k]){ eq=0; break; } if(eq){ f=i; break; } }
                if(f>=0){ if(!le_push(an,0,g_le[f].mode,g_ldata+g_le[f].off,g_le[f].len))return -1; cnt++; } } }
        off+=adv; }
    return want?0:cnt; }
static int ar_member(const u8* a,long n,const char* pfx,const u8** out,long* olen){
    if(n<8||a[0]!='!'||a[1]!='<'||a[2]!='a'||a[3]!='r'||a[4]!='c'||a[5]!='h'||a[6]!='>')return 0;
    long off=8;
    while(off+60<=n){ const char* h=(const char*)(a+off); long sz=0; for(int i=48;i<58&&ap_dig(h[i]);i++)sz=sz*10+(h[i]-'0');
        int m=1; for(int i=0;pfx[i];i++) if(h[i]!=pfx[i]){ m=0; break; }
        if(m&&off+60+sz<=n){ *out=a+off+60; *olen=sz; return 1; }
        off+=60+sz+(sz&1); }
    return 0; }
__attribute__((unused)) static u8* build_layer(long* outn){
    long strn=1; for(int i=0;i<g_nle;i++)strn+=asl(g_le[i].name)+1+asl(g_le[i].tgt)+1;
    long hdr=16+16+(long)g_nle*24; long so=hdr; long doff=(so+strn+15)&~15L; long tot=doff+g_nld;
    u8* b=(u8*)kmalloc((unsigned long)tot); if(!b)return 0; for(long i=0;i<doff;i++)b[i]=0;
    b[0]='N'; b[1]='V'; b[2]='S'; b[3]='2'; *(u32*)(b+4)=(u32)g_nle; *(u32*)(b+8)=1; *(u32*)(b+12)=(u32)so;
    *(u32*)(b+16)=0; *(u32*)(b+20)=(u32)doff; *(u32*)(b+24)=(u32)g_nld; *(u32*)(b+28)=(u32)g_nld;
    long sp=so+1;
    for(int i=0;i<g_nle;i++){ u32* r=(u32*)(b+32+(long)i*24); int nl=asl(g_le[i].name), tl=asl(g_le[i].tgt);
        r[0]=(u32)(sp-so); for(int k=0;k<=nl;k++)b[sp+k]=(u8)g_le[i].name[k]; sp+=nl+1;
        if(tl){ r[1]=(u32)(sp-so); for(int k=0;k<=tl;k++)b[sp+k]=(u8)g_le[i].tgt[k]; sp+=tl+1; } else r[1]=0;
        r[2]=g_le[i].mode; r[3]=0; r[4]=(u32)g_le[i].off; r[5]=(u32)g_le[i].len; }
    for(long i=0;i<g_nld;i++)b[doff+i]=g_ldata[i];
    *outn=tot; return b; }
static int ctl_field(const u8* c,long n,const char* key,char* out,int cap){
    int kl=asl(key); long i=0; out[0]=0;
    while(i<n){ long l=i; while(i<n&&c[i]!='\n')i++; long le=i; i++;
        if(le-l>kl&&c[l+kl]==':'){ int m=1; for(int k=0;k<kl;k++) if(c[l+k]!=(u8)key[k]){ m=0; break; } if(m){ long v=l+kl+1; while(v<le&&c[v]==' ')v++; int o=0; while(v<le&&o<cap-1)out[o++]=(char)c[v++]; out[o]=0; return 1; } } }
    return 0; }
static int deb_install(const u8* deb,long n,const char* expect,int autoi,int verbose_files){
    const u8* cm; long cl; const u8* dm; long dl;
    if(!ar_member(deb,n,"control.tar",&cm,&cl)||!ar_member(deb,n,"data.tar",&dm,&dl)){ APE("not a Debian package archive",0); return 0; }
    long ctl_n=0; u8* ctl=unp_auto(cm,cl,&ctl_n); if(!ctl){ APE("cannot decompress control.tar",0); return 0; }
    const u8* cf; long cfl; if(tar_scan(ctl,ctl_n,0,"./control",&cf,&cfl)!=1){ kfree(ctl); APE("package has no control file",0); return 0; }
    inst_t x; for(int i=0;i<(int)sizeof(x);i++)((char*)&x)[i]=0;
    ctl_field(cf,cfl,"Package",x.name,64); ctl_field(cf,cfl,"Version",x.ver,64); ctl_field(cf,cfl,"Architecture",x.arch,16); ctl_field(cf,cfl,"Description",x.desc,96); { char pd[128]; char dd[256]; ctl_field(cf,cfl,"Pre-Depends",pd,128); ctl_field(cf,cfl,"Depends",dd,256); char* p=x.dep; if(pd[0]){ p=sj(p,pd); if(dd[0])p=sj(p,", "); } if(asl(x.dep)+asl(dd)<255)sj(p,dd); }
    const u8* ps; long psl; int has_post=tar_scan(ctl,ctl_n,0,"./postinst",&ps,&psl)==1;
    kfree(ctl);
    if(!x.name[0]){ APE("package has no name",0); return 0; }
    if(expect&&expect[0]){ int m=1; for(int i=0;;i++){ if(expect[i]!=x.name[i]){ m=0; break; } if(!expect[i])break; } if(!m)APW("package name differs from index: ",x.name); }
    if(x.arch[0]&&!seq(x.arch,"amd64")&&!seq(x.arch,"all")){ APE("wrong architecture: ",x.arch); return 0; }
    AP("Unpacking "); AP(x.name); AP(" ("); AP(x.ver); APL(") ...");
    long dn=0; u8* data=unp_auto(dm,dl,&dn); if(!data){ APE("cannot decompress data.tar (needs gzip, xz or zstd)",0); return 0; }
    man_reset(); int cnt=tar_scan(data,dn,2,0,0,0); kfree(data);
    if(cnt<0){ APE("out of space or memory while unpacking",0); return 0; }
    if(verbose_files){ long shown=0,i=0; while(i<g_man_n&&shown<12){ long e=i; while(e<g_man_n&&g_man[e]!='\n')e++; char lb[160]; long c=e-i<159?e-i:159; for(long k=0;k<c;k++)lb[k]=g_man[i+k]; lb[c]=0; tcol(GREY); AP("  "); AP(lb); term_putc('\n'); tcol(WHT); i=e+1; shown++; } if(g_man_files>12){ tcol(GREY); AP("  ... "); APN(g_man_files-12); APL(" more"); tcol(WHT); } }
    char fn[128]; char* p=fn; p=sj(p,AP_PKGDIR "/"); p=sj(p,x.name); p=sj(p,".list");
    wfile(fn,(const u8*)(g_man?g_man:(const char*)""),g_man_n); man_reset(); { extern int g_apps_gen; g_apps_gen++; }
    int si=st_find(svs(x.name)); if(si>=0){ int keep=g_in[si].autoi&&autoi; g_in[si]=x; g_in[si].autoi=keep; }
    else { if(g_nin>=g_cin){ int nc=g_cin?g_cin*2:64; inst_t* y=(inst_t*)krealloc(g_in,sizeof(inst_t)*(unsigned long)nc); if(!y)return 0; g_in=y; g_cin=nc; } x.autoi=autoi; g_in[g_nin++]=x; }
    st_save();
    AP("Setting up "); AP(x.name); AP(" ("); AP(x.ver); APL(") ...");
    if(has_post){ (void)ps; (void)psl; }
    return 1; }

static void apt_update(void){
    src_load(); idx_free(); int ok=0, hit=1;
    for(int i=0;i<g_nsrc;i++){ char base[200]; int flat=0; { int k=0; for(;g_srcbase[i][k]&&k<199;k++){ if(g_srcbase[i][k]=='|'){ flat=1; break; } base[k]=g_srcbase[i][k]; } base[k]=0; }
        static const char* const ext[]={".xz",".gz","",0}; u8* got=0; long gl=0; char url[400];
        for(int e=0;ext[e]&&!got;e++){ char* p=url; p=sj(p,base); if(!flat)p=sj(p,"/binary-amd64"); p=sj(p,"/Packages"); p=sj(p,ext[e]);
            if(e==0){ AP("Get:"); APN(hit); AP(" "); AP(base); APL(flat?" Packages":" amd64 Packages"); }
            got=ap_get(url,&gl,0); }
        if(!got){ tcol(rgb(240,120,120)); AP("Err:"); APN(hit); AP(" "); AP(base); AP("  "); APL(g_fetch_err?g_fetch_err:"failed"); tcol(WHT); hit++; continue; }
        long hl=5+asl(g_srcbase[i])+1; u8* f=(u8*)kmalloc((unsigned long)(gl+hl)); if(!f){ kfree(got); continue; }
        char* p=(char*)f; p=sj(p,"NVXL "); p=sj(p,g_srcbase[i]); *p++='\n'; for(long k=0;k<gl;k++)f[hl+k]=got[k]; kfree(got);
        char fn[96]; p=fn; p=sj(p,AP_LISTDIR "/src"); p=udec(p,(u32)i); p=sj(p,".pkgs"); *p=0;
        wfile(fn,f,gl+hl); kfree(f); tcol(GREY); AP("Fetched "); Psize(gl); term_putc('\n'); tcol(WHT); sh_flush(); ok++; hit++; }
    AP("Reading package lists... "); sh_flush(); int n=idx_load(1); APL("Done");
    if(!ok){ APE("no package lists could be downloaded - check the network and " AP_SOURCES,0); return; }
    st_load(); int up=0; for(int i=0;i<g_nin;i++){ int r=idx_best(svs(g_in[i].name)); if(r>=0&&vcmp(g_pr[r].ver,svs(g_in[i].ver))>0)up++; }
    APN(n); AP(" packages available. "); if(up){ APN(up); APL(" packages can be upgraded. Run 'apt list --upgradable' to see them."); } else APL("All packages are up to date."); }

static int g_yes;
static void plan_print(int nreq){
    long dl=0, isz=0; int nnew=0;
    for(int i=0;i<g_nplan;i++){ prec* r=&g_pr[g_plan[i]]; dl+=r->size; long z=0; for(int k=0;k<r->isize.n&&ap_dig(r->isize.p[k]);k++)z=z*10+(r->isize.p[k]-'0'); isz+=z*1024; if(st_find(r->name)<0)nnew++; }
    if(g_nplan>nreq){ APL("The following additional packages will be installed:"); int col=0; AP(" "); for(int i=0;i<g_nplan;i++){ if(!g_planauto[i])continue; prec* r=&g_pr[g_plan[i]]; if(col+r->name.n+1>96){ term_puts("\n "); col=0; } term_putc(' '); APSV(r->name); col+=r->name.n+1; } term_putc('\n'); }
    APL(nnew?"The following NEW packages will be installed:":"The following packages will be upgraded:"); { int col=0; AP(" "); for(int i=0;i<g_nplan;i++){ prec* r=&g_pr[g_plan[i]]; if(col+r->name.n+1>96){ term_puts("\n "); col=0; } term_putc(' '); APSV(r->name); col+=r->name.n+1; } term_putc('\n'); }
    APN(g_nplan-nnew); AP(" upgraded, "); APN(nnew); APL(" newly installed, 0 to remove and 0 not upgraded.");
    AP("Need to get "); Psize(dl); APL(" of archives.");
    AP("After this operation, "); Psize(isz); APL(" of additional disk space will be used."); }
static int plan_run(void){
    int done=0;
    for(int i=0;i<g_nplan;i++){ prec* r=&g_pr[g_plan[i]]; char base[200]; { const char* sb=g_srcbase[r->src]; int k=0; int ds=-1; for(int q=0;sb[q];q++) if(sb[q]=='d'&&sb[q+1]=='i'&&sb[q+2]=='s'&&sb[q+3]=='t'&&sb[q+4]=='s'&&sb[q+5]=='/'){ ds=q; break; }
            if(ds<0){ for(;sb[k]&&sb[k]!='|'&&k<199;k++)base[k]=sb[k]; } else for(;k<ds&&k<199;k++)base[k]=sb[k]; base[k]=0; }
        char url[600]; char* p=url; p=sj(p,base); { char f[400]; svcpy(f,r->file,400); p=sj(p,f); }
        AP("Get:"); APN(i+1); AP(" "); AP(base); AP(" "); APSV(r->name); AP(" "); APSV(r->arch); AP(" "); APSV(r->ver); AP(" ["); Psize(r->size); APL("]");
        long n=0; u8* deb=ap_get(url,&n,r->size);
        if(!deb){ tcol(rgb(240,120,120)); AP("Err:"); APN(i+1); AP(" "); AP(url); AP("  "); APL(g_fetch_err?g_fetch_err:"failed"); tcol(WHT); APE("Unable to fetch some archives, maybe run apt update",0); return done; }
        if(r->size&&n!=r->size){ kfree(deb); APE("Size mismatch for ",url); return done; }
        if(r->sha.n==64){ u8 h[32]; sha256_hash(deb,(int)n,h); int ok=1; for(int k=0;k<32;k++){ int v=ap_hexval(r->sha.p[k*2])*16+ap_hexval(r->sha.p[k*2+1]); if(v!=h[k]){ ok=0; break; } } if(!ok){ kfree(deb); APE("Hash Sum mismatch for ",url); return done; } }
        char nm[64]; svcpy(nm,r->name,64);
        int ok=deb_install(deb,n,nm,g_planauto[i],0); kfree(deb); if(!ok)return done; done++; }
    return done; }
static void apt_install(const char* args,int reinstall){
    if(!idx_load(0))return; st_load(); g_nplan=0; int nreq=0;
    AP("Reading package lists... Done\nBuilding dependency tree... Done\nReading state information... Done\n"); sh_flush();
    const char* a=args;
    for(;;){ while(*a==' ')a++; if(!*a)break; char nm[96]; int k=0; while(*a&&*a!=' '&&k<95)nm[k++]=*a++; nm[k]=0; if(nm[0]=='-')continue;
        { static const char* const al[]={"ide","python-ide","pyide",0}; for(int q=0;al[q];q++) if(seq(nm,al[q])){ char* p=nm; p=sj(p,"python3-ide"); break; } }
        sv n=svs(nm); for(int q=0;q<n.n;q++) if(nm[q]=='='){ n.n=q; break; }
        if(is_prot(n)){ AP(nm); APL(" is already the newest version (provided by NoovexOS)."); continue; }
        int si=st_find(n); int v=0; int r=idx_find(n,&v);
        if(r<0){ APE("Unable to locate package ",nm);
            if(!g_have_remote){ tcol(rgb(230,200,120)); APL("N: Only the built-in package list is loaded. To install software from Debian,"); APL("N: download the full package list first:  sudo apt update"); tcol(WHT); }
            return; }
        if(si>=0&&!reinstall&&vcmp(g_pr[r].ver,svs(g_in[si].ver))<=0){ g_in[si].autoi=0; st_save(); AP(g_in[si].name); AP(" is already the newest version ("); AP(g_in[si].ver); APL(")."); continue; }
        if(v){ tcol(GREY); AP("Note, selecting '"); APSV(g_pr[r].name); AP("' instead of '"); AP(nm); APL("'"); tcol(WHT); }
        if(!resolve(r,0,0)){ APE("Unmet dependencies. Cannot satisfy: ",g_unmet); return; }
        for(int i=0;i<g_nplan;i++) if(g_plan[i]==r)g_planauto[i]=0;
        nreq++; }
    if(!g_nplan){ APL("0 upgraded, 0 newly installed, 0 to remove and 0 not upgraded."); return; }
    plan_print(nreq);
    if(!g_yes&&g_nplan>nreq){ AP("Do you want to continue? [Y/n] "); sh_flush(); if(!sh_yesno(1)){ APL("Abort."); return; } }
    int d=plan_run(); if(d==g_nplan){ tcol(GREEN); AP("Installed "); APN(d); APL(d==1?" package.":" packages."); tcol(WHT); } }
static int remove_one(const char* nm,int purge){
    int si=st_find(svs(nm)); if(si<0){ AP("Package '"); AP(nm); APL("' is not installed, so not removed"); return 0; }
    AP("Removing "); AP(g_in[si].name); AP(" ("); AP(g_in[si].ver); APL(") ...");
    char tag[72]; char* p=tag; p=sj(p,"pkg:"); p=sj(p,g_in[si].name); sysroot_del_layer(tag);
    char lf[128]; p=lf; p=sj(p,AP_PKGDIR "/"); p=sj(p,g_in[si].name); p=sj(p,".list"); long mn=0; const u8* md=rfile(lf,&mn);
    if(md){ u8* mc=(u8*)kmalloc((unsigned long)(mn?mn:1)); if(mc){ for(long i=0;i<mn;i++)mc[i]=md[i]; long i=0; while(i<mn){ long e=i; while(e<mn&&mc[e]!='\n')e++; char pb[256]; long c=e-i<255?e-i:255; for(long k=0;k<c;k++)pb[k]=(char)mc[i+k]; pb[c]=0; if(c>0)k_unlink(pb); i=e+1; } kfree(mc); } k_unlink(lf); }
    char fn[128]; p=fn; p=sj(p,AP_PKGDIR "/"); p=sj(p,g_in[si].name); p=sj(p,".nvs"); k_unlink(fn);
    for(int i=si;i<g_nin-1;i++)g_in[i]=g_in[i+1]; g_nin--; st_save(); { extern int g_apps_gen; g_apps_gen++; } (void)purge; return 1; }
static int needed_by_others(int idx){
    sv me=svs(g_in[idx].name);
    for(int i=0;i<g_nin;i++){ if(i==idx)continue; const char* s=g_in[i].dep; const char* e=s+asl(s);
        while(s<e){ while(s<e&&(*s==' '||*s==','||*s=='|'))s++; const char* a=s; while(s<e&&*s!=' '&&*s!=','&&*s!='|'&&*s!='('&&*s!=':')s++; sv t; t.p=a; t.n=(int)(s-a); if(t.n&&sveqv(t,me))return 1; while(s<e&&*s!=','&&*s!='|')s++; } }
    return 0; }
static void apt_autoremove(void){
    st_load(); int n=0;
    for(int pass=0;pass<8;pass++){ int any=0; for(int i=0;i<g_nin;i++){ if(!g_in[i].autoi||needed_by_others(i))continue; char nm[64]; for(int k=0;k<64;k++)nm[k]=g_in[i].name[k]; if(remove_one(nm,0)){ n++; any=1; i--; } } if(!any)break; }
    APN(n); APL(" packages removed."); }
static void apt_remove(const char* args,int purge){
    st_load(); const char* a=args; int n=0;
    for(;;){ while(*a==' ')a++; if(!*a)break; char nm[96]; int k=0; while(*a&&*a!=' '&&k<95)nm[k++]=*a++; nm[k]=0; if(nm[0]=='-')continue;
        if(is_prot(svs(nm))){ APE(nm," is part of NoovexOS and cannot be removed"); continue; }
        n+=remove_one(nm,purge); }
    APN(n); APL(n==1?" package removed.":" packages removed.");
    int orphan=0; for(int i=0;i<g_nin;i++) if(g_in[i].autoi&&!needed_by_others(i))orphan++;
    if(orphan){ APN(orphan); APL(" automatically installed packages are no longer required. Use 'sudo apt autoremove' to remove them."); } }
static void apt_list(const char* args){
    int inst=0, upg=0; const char* a=args; char pat[64]; pat[0]=0;
    for(;;){ while(*a==' ')a++; if(!*a)break; char t[64]; int k=0; while(*a&&*a!=' '&&k<63)t[k++]=*a++; t[k]=0; if(seq(t,"--installed"))inst=1; else if(seq(t,"--upgradable"))upg=1; else { for(int q=0;q<64;q++){ pat[q]=t[q]; if(!t[q])break; } } }
    st_load();
    if(inst){ APL("Listing... Done"); for(int i=0;i<g_nin;i++){ tcol(GREEN); AP(g_in[i].name); tcol(WHT); AP("/now "); AP(g_in[i].ver); AP(" "); AP(g_in[i].arch[0]?g_in[i].arch:"amd64"); APL(g_in[i].autoi?" [installed,automatic]":" [installed]"); } return; }
    if(!idx_load(0))return; APL("Listing... Done"); int shown=0;
    for(int i=0;i<g_npr&&shown<400;i++){ prec* r=&g_pr[i]; int b=idx_best(r->name); if(b!=i)continue;
        int si=st_find(r->name); if(upg&&(si<0||vcmp(r->ver,svs(g_in[si].ver))<=0))continue;
        if(pat[0]){ int pl=asl(pat); int m=0; for(int s=0;s+pl<=r->name.n&&!m;s++){ int ok=1; for(int k=0;k<pl;k++){ if(pat[k]=='*')continue; if(r->name.p[s+k]!=pat[k]){ ok=0; break; } } if(ok)m=1; } if(!m)continue; }
        tcol(GREEN); APSV(r->name); tcol(WHT); AP("/stable "); APSV(r->ver); AP(" "); APSV(r->arch); if(si>=0)AP(" [installed]"); term_putc('\n'); shown++; if((shown&15)==0)sh_flush(); }
    if(shown>=400)APL("... (list truncated - use a pattern: apt list 'lib*')"); sh_flush(); }
static int ci_has(sv h,const char* n){ int nl=asl(n); for(int s=0;s+nl<=h.n;s++){ int ok=1; for(int k=0;k<nl;k++){ char a=h.p[s+k], b=n[k]; if(a>='A'&&a<='Z')a+=32; if(b>='A'&&b<='Z')b+=32; if(a!=b){ ok=0; break; } } if(ok)return 1; } return 0; }
static void apt_search(const char* args){
    while(*args==' ')args++; if(!*args){ APL("usage: apt search <word>"); return; } if(!idx_load(0))return; st_load();
    APL("Sorting... Done\nFull Text Search... Done"); int shown=0;
    for(int i=0;i<g_npr&&shown<120;i++){ prec* r=&g_pr[i]; if(idx_best(r->name)!=i)continue; if(!ci_has(r->name,args)&&!ci_has(r->desc,args))continue;
        tcol(GREEN); APSV(r->name); tcol(WHT); AP("/stable "); APSV(r->ver); AP(" "); APSV(r->arch); if(st_find(r->name)>=0)AP(" [installed]"); term_putc('\n'); tcol(GREY); AP("  "); APSV(r->desc); term_putc('\n'); tcol(WHT); shown++; if((shown&7)==0)sh_flush(); }
    if(!shown)APL("(no matches)"); else if(shown>=120)APL("... (more results - try a longer word)"); sh_flush(); }
static void apt_show(const char* args){
    while(*args==' ')args++; char nm[96]; int k=0; while(args[k]&&args[k]!=' '&&k<95){ nm[k]=args[k]; k++; } nm[k]=0; if(!nm[0]){ APL("usage: apt show <package>"); return; }
    if(!idx_load(0))return; int r=idx_find(svs(nm),0); if(r<0){ APE("No packages found: ",nm); return; }
    sv s=g_pr[r].stanza; char line[300]; const char* p=s.p; const char* e=p+s.n; int nl=0;
    while(p<e&&nl<60){ const char* l=p; while(p<e&&*p!='\n')p++; int n=(int)(p-l); if(p<e)p++; int c=n<299?n:299; for(int i=0;i<c;i++)line[i]=l[i]; line[c]=0;
        if(line[0]=='M'&&line[1]=='D'&&line[2]=='5')continue; if(line[0]=='S'&&line[1]=='H'&&line[2]=='A')continue;
        APL(line); nl++; }
    st_load(); AP("APT-Manual-Installed: "); APL(st_find(g_pr[r].name)>=0?"yes":"no"); }
static void apt_upgrade(void){
    if(!idx_load(0))return; st_load(); g_nplan=0; int nreq=0;
    for(int i=0;i<g_nin;i++){ int r=idx_best(svs(g_in[i].name)); if(r<0||vcmp(g_pr[r].ver,svs(g_in[i].ver))<=0)continue; if(!resolve(r,g_in[i].autoi,0)){ APE("Unmet dependencies: ",g_unmet); return; } nreq++; }
    if(!g_nplan){ APL("0 upgraded, 0 newly installed, 0 to remove and 0 not upgraded."); return; }
    plan_print(g_nplan); if(!g_yes){ AP("Do you want to continue? [Y/n] "); sh_flush(); if(!sh_yesno(1)){ APL("Abort."); return; } }
    plan_run(); (void)nreq; }

static void apt_help(void){
    APL("apt 2.9 (NoovexOS) - install Debian packages (amd64)");
    APL("Usage: apt [options] command");
    APL("  update             download the package lists from " AP_SOURCES);
    APL("  install <pkg...>   install packages and their dependencies (-y: don't ask)");
    APL("  reinstall <pkg>    download and unpack again");
    APL("  remove <pkg...>    remove packages (purge = remove)");
    APL("  autoremove         remove packages that were only needed as dependencies");
    APL("  upgrade            upgrade installed packages");
    APL("  search <word>      search names and descriptions");
    APL("  show <pkg>         show package details");
    APL("  list [--installed|--upgradable] [pattern]");
    APL("Packages unpack into the system image layer (programs land in /usr/bin and");
    APL("run through the glibc layer). Maintainer scripts are not run.");
    sh_flush(); }
static int need_root2(void){ if(g_uid==0)return 1; tcol(rgb(240,120,120)); APL("E: Could not open lock file /var/lib/dpkg/lock-frontend - open (13: Permission denied)"); APL("E: Unable to acquire the dpkg frontend lock, are you root? (try: sudo apt ...)"); tcol(WHT); return 0; }

void apt_cmd(const char* arg){
    const char* a=arg; g_yes=0; char sub[24]; sub[0]=0;
    for(;;){ while(*a==' ')a++; if(*a=='-'){ if((a[1]=='y'&&(a[2]==' '||!a[2]))||(a[1]=='-'&&a[2]=='y'))g_yes=1; while(*a&&*a!=' ')a++; continue; } int k=0; while(*a&&*a!=' '&&k<23)sub[k++]=*a++; sub[k]=0; break; }
    { const char* q=a; while(*q){ if(q[0]==' '&&q[1]=='-'&&q[2]=='y'&&(q[3]==' '||!q[3]))g_yes=1; q++; } }
    if(seq(sub,"update")){ if(need_root2())apt_update(); }
    else if(seq(sub,"install")){ if(need_root2())apt_install(a,0); }
    else if(seq(sub,"reinstall")){ if(need_root2())apt_install(a,1); }
    else if(seq(sub,"remove")||seq(sub,"purge")){ if(need_root2())apt_remove(a,seq(sub,"purge")); }
    else if(seq(sub,"autoremove")){ if(need_root2())apt_autoremove(); }
    else if(seq(sub,"upgrade")||seq(sub,"full-upgrade")||seq(sub,"dist-upgrade")){ if(need_root2())apt_upgrade(); }
    else if(seq(sub,"search")){ apt_search(a); }
    else if(seq(sub,"show")||seq(sub,"policy")){ apt_show(a); }
    else if(seq(sub,"list")){ apt_list(a); }
    else if(seq(sub,"clean")||seq(sub,"autoclean")){ }
    else apt_help(); }

void dpkg_cmd(const char* arg){
    const char* a=arg; while(*a==' ')a++; char op[24]; int k=0; while(*a&&*a!=' '&&k<23)op[k++]=*a++; op[k]=0; while(*a==' ')a++;
    if(seq(op,"-i")||seq(op,"--install")){ if(!need_root2())return;
        char fn[160]; int f=0; while(a[f]&&a[f]!=' '&&f<159){ fn[f]=a[f]; f++; } fn[f]=0; if(!fn[0]){ APL("dpkg: error: --install needs at least one package archive file argument"); return; }
        long n=0; const u8* d=rfile(fn,&n);
        if(!d){ static u8 g_dbuf[1]; (void)g_dbuf; long cap=96L<<20; u8* b=(u8*)kmalloc((unsigned long)cap); if(b){ int got=fat_read_file(fn,b,(int)cap); if(got>0){ n=got; st_load(); AP("Selecting previously unselected package.\n"); deb_install(b,n,0,0,1); } else APE("cannot access archive: ",fn); kfree(b); } return; }
        u8* c=(u8*)kmalloc((unsigned long)n); if(!c)return; for(long i=0;i<n;i++)c[i]=d[i]; st_load(); deb_install(c,n,0,0,1); kfree(c); }
    else if(seq(op,"-r")||seq(op,"-P")||seq(op,"--remove")||seq(op,"--purge")){ if(!need_root2())return; apt_remove(a,0); }
    else if(seq(op,"-l")||seq(op,"--list")){ st_load();
        APL("Desired=Unknown/Install/Remove/Purge/Hold"); APL("| Status=Not/Inst/Conf-files/Unpacked/halF-conf/Half-inst/trig-aWait/Trig-pend"); APL("||/ Name                    Version              Architecture Description"); APL("+++-=======================-====================-============-================================");
        for(int i=0;i<g_nin;i++){ AP("ii  "); AP(g_in[i].name); for(int s=asl(g_in[i].name);s<24;s++)term_putc(' '); AP(g_in[i].ver); for(int s=asl(g_in[i].ver);s<21;s++)term_putc(' '); { const char* ar=g_in[i].arch[0]?g_in[i].arch:"amd64"; AP(ar); for(int s2=asl(ar);s2<13;s2++)term_putc(' '); } APL(g_in[i].desc); } }
    else if(seq(op,"-L")||seq(op,"--listfiles")){ char pkg[96]; { int q=0; while(a[q]&&a[q]!=' '&&q<95){ pkg[q]=a[q]; q++; } pkg[q]=0; }
        char lf[128]; char* p=sj(sj(sj(lf,AP_PKGDIR "/"),pkg),".list"); (void)p; long mn=0; const u8* md=rfile(lf,&mn);
        if(md){ long i=0; while(i<mn){ long e=i; while(e<mn&&md[e]!='\n')e++; char pb[256]; long c=e-i<255?e-i:255; for(long k=0;k<c;k++)pb[k]=(char)md[i+k]; pb[c]=0; if(c>0)APL(pb); i=e+1; } return; }
        char tag[96]; p=sj(tag,"pkg:"); for(int q=0;pkg[q]&&q<91;q++)*p++=pkg[q]; *p=0;
        if(!sysroot_has_layer(tag)){ AP("dpkg-query: package '"); AP(pkg); APL("' is not installed"); return; }
        char nm[256]; for(int i=0;sysroot_layer_files(tag,i,nm,256);i++){ APL(nm); } }
    else if(seq(op,"-s")||seq(op,"--status")){ st_load(); int si=st_find(svs(a)); if(si<0){ AP("dpkg-query: package '"); AP(a); APL("' is not installed"); return; }
        AP("Package: "); APL(g_in[si].name); APL("Status: install ok installed"); AP("Version: "); APL(g_in[si].ver); AP("Architecture: "); APL(g_in[si].arch); if(g_in[si].dep[0]){ AP("Depends: "); APL(g_in[si].dep); } AP("Description: "); APL(g_in[si].desc); }
    else if(seq(op,"--print-architecture")){ APL("amd64"); }
    else { APL("Usage: dpkg -i <file.deb> | -r <pkg> | -l | -L <pkg> | -s <pkg> | --print-architecture"); } }

void apt_boot(void){
    char nm[64]; int t=0; int n=0;
    for(int i=0;k_readdir_at(AP_PKGDIR,i,nm,&t)==1&&i<4096;i++){
        int L=asl(nm); if(L<5||nm[L-4]!='.'||nm[L-3]!='n'||nm[L-2]!='v'||nm[L-1]!='s')continue;
        char fn[128]; char* p=fn; p=sj(p,AP_PKGDIR "/"); p=sj(p,nm); long ln=0; const u8* d=rfile(fn,&ln); if(!d||ln<16)continue;
        u8* c=(u8*)kmalloc((unsigned long)ln); if(!c)continue; for(long q=0;q<ln;q++)c[q]=d[q];
        char tag[80]; p=tag; p=sj(p,"pkg:"); for(int q=0;q<L-4&&q<70;q++)*p++=nm[q]; *p=0;
        if(sysroot_add_layer(tag,c,(u32)ln,1))n++; else kfree(c); }
    (void)n; }
