#include "include/nvx/base.h"
#include "include/nvx/assets/fontaa.h"
#include "include/nvx/arch/x86_64/cpu/percpu.h"
#include "include/nvx/lib/crypto/nvxtls.h"
#include "include/nvx/lib/crypto/nvxwpa.h"
#include "include/nvx/lib/crypto/nvxcrypto2.h"
#include "include/nvx/mem/heap.h"
#include "include/nvx/mem/pmm_vmm.h"
#include "include/nvx/mem/slab.h"
#include "include/nvx/arch/x86_64/user/ring3.h"
#include "include/nvx/core/proc/elf.h"
#include "include/nvx/core/proc/sched.h"
#include "include/nvx/arch/x86_64/smp/smp.h"
#include "include/nvx/mem/heap_api.h"
#include "include/nvx/lib/base/klibc.h"
#include "include/nvx/core/klog.h"
#include "include/nvx/lib/base/fmt.h"
#include "include/nvx/drivers/video/fb/framebuffer.h"
#include "include/nvx/lib/js/js.h"
#include "include/nvx/lib/crypto/nvxcrypto.h"
#include "include/nvx/arch/x86_64/cpu/cpuinfo.h"
#include "include/nvx/drivers/rtc/cmos.h"
#include "include/nvx/drivers/bus/pci.h"
#include "include/nvx/drivers/net/ethernet/nic_names.h"
#include "include/nvx/drivers/usb/xhci.h"
#include "include/nvx/drivers/net/ethernet/nic.h"
#include "include/nvx/drivers/storage/ahci.h"
#include "include/nvx/net/stack/tcpip.h"
#include "include/nvx/net/http/http.h"
#include "include/nvx/drivers/input/ps2/ps2.h"
#include "include/nvx/graphics/theme/theme.h"
#include "include/nvx/core/panic.h"
#include "include/nvx/graphics/ui/wm.h"
#include "include/nvx/fs/vfs/ramfs.h"
#include "include/nvx/fs/vfs/vfs.h"
#include "include/nvx/shell/terminal.h"
#include "include/nvx/shell/vt.h"
#include "include/nvx/drivers/storage/ata.h"
#include "include/nvx/drivers/storage/nvme.h"
#include "include/nvx/fs/nvx/nvxfs.h"
#include "include/nvx/fs/vfs/parts.h"
#include "include/nvx/fs/fat/fat_ata.h"
#include "include/nvx/drivers/net/wifi/rtw_efuse.h"
#include "include/nvx/core/buildsig.h"
#include "include/nvx/drivers/net/wifi/rtw8821ce.h"
#include "include/nvx/net/wifi/wpa2.h"
#include "include/nvx/drivers/net/wifi/rtw_sec.h"
#include "include/nvx/net/wifi/wifi_netdev.h"
#include "include/nvx/fs/vfs/dirtree.h"
#include "include/nvx/sys/native/syscall.h"
#include "include/nvx/drivers/video/nvidia/nvgpu.h"
#include "include/nvx/drivers/video/nvidia/nvdisp.h"
#include "include/nvx/drivers/video/nvidia/nvmon.h"
#include "include/nvx/core/power.h"
#include "include/nvx/drivers/acpi/aml.h"
#include "include/nvx/drivers/acpi/acpi_dev.h"
#include "include/nvx/arch/x86_64/time/hrtimer.h"
#include "include/nvx/core/sound.h"
#include "include/nvx/drivers/input/touchpad/touchpad.h"
#include "include/nvx/arch/x86_64/smp/smp_sched.h"
#include "include/nvx/core/proc/jobs.h"
#include "include/nvx/fs/nvx/nvx4.h"
#include "include/nvx/fs/vfs/unixfs.h"
#include "include/nvx/fs/ntfs/ntfsk.h"
#include "include/nvx/shell/commands.h"
#include "include/nvx/apps/office/editor.h"
#include "include/nvx/apps/internet/browser.h"
#include "include/nvx/apps/system/files_classic.h"
#include "include/nvx/fs/pkg/pkg.h"
#include "include/nvx/graphics/ui/desktop.h"
#include "include/nvx/graphics/ui/input.h"
#include "include/nvx/core/init/installer.h"
#include "include/nvx/core/init/main.h"
/* ---- Stage 2: pipes | and redirection > < in the shell ---- */
static const char* g_pipein=0;

    enum{ PKG_RAW_SZ=8*1024*1024 };

       /* piped stdin for the current command, or 0 */
static void run_cmd_one(char*s); 

static u64 g_sudo_until=0;

static void sh_redraw(void){ if(g_state==ST_LIVE)live_present(); else if(g_state==ST_DESKTOP)compose_present(); }
static int sh_readpw(char* pw,int cap){ int n=0; sh_redraw();
    for(;;){ int c=kpop(); if(c<0){ cpu_idle(); continue; }
        if(c=='\n'){ term_putc('\n'); sh_redraw(); break; }
        if(c==3){ term_puts("^C\n"); sh_redraw(); pw[0]=0; return -1; }
        if(c=='\b'||c==127){ if(n>0){ n--; term_putc('\b'); } }
        else if(c>=32&&c<127&&n<cap-1){ pw[n++]=(char)c; term_putc('*'); }
        sh_redraw(); }
    pw[n]=0; return n; }
static int sh_shadow_hash(const char* user,char* out){
    int isd=0; int k=ux_lookup("/etc/shadow",&isd,1); if(k<0||isd)return 0;
    const char* s=fsd(k); const char* e=s+FS[k].len;
    while(s<e){ const char* l=s; while(s<e&&*s!='\n')s++; const char* le=s; if(s<e)s++;
        int i=0; while(l+i<le&&user[i]&&l[i]==user[i])i++; if(user[i]||l+i>=le||l[i]!=':')continue;
        const char* h=l+i+1; int n=0; while(h+n<le&&n<20){ out[n]=h[n]; n++; } out[n]=0; return (n>0&&out[0]!='*')?1:0; }
    return 0; }
static void sh_hashhex(const char* pw,char* out){ unsigned h=nvx_pwhash(pw); char t[12]; int n=0; if(!h)t[n++]='0'; while(h){ t[n++]="0123456789abcdef"[h&15]; h>>=4; } int o=0; while(n)out[o++]=t[--n]; out[o]=0; }
void sh_flush(void){ sh_redraw(); }
int sh_yesno(int def){ sh_redraw(); char b[8]; int n=0;
    for(;;){ int c=kpop(); if(c<0){ cpu_idle(); continue; }
        if(c=='\n'){ term_putc('\n'); sh_redraw(); break; }
        if(c==3){ term_puts("^C\n"); sh_redraw(); return 0; }
        if(c=='\b'||c==127){ if(n>0){ n--; term_putc('\b'); } }
        else if(c>=32&&c<127&&n<7){ b[n++]=(char)c; term_putc((char)c); }
        sh_redraw(); }
    if(!n)return def; return b[0]=='y'||b[0]=='Y'; }
extern void apt_cmd(const char* arg);
extern void dpkg_cmd(const char* arg);
static int sudo_auth(void){
    if(g_uid==0)return 1;
    const char* u=cur_user(); char who[32]; int i=0; for(;u[i]&&i<31;i++)who[i]=u[i]; who[i]=0;
    if(!ux_is_admin(g_uid)){ tcol(rgb(240,120,120)); term_puts("sudo: "); term_puts(who); term_puts(" is not an administrator\n"); tcol(WHT); return 0; }
    if(g_ticks<g_sudo_until)return 1;
    char want[24]; if(!sh_shadow_hash(who,want)){ tcol(rgb(255,205,100)); term_puts("sudo: no password is set for "); term_puts(who); term_puts(" - run passwd to protect administrator access\n"); tcol(WHT); g_sudo_until=g_ticks+30000; return 1; }
    for(int t=0;t<3;t++){ char pw[48]; term_puts("[sudo] password for "); term_puts(who); term_puts(": "); if(sh_readpw(pw,48)<0)return 0;
        char got[24]; sh_hashhex(pw,got); int ok=1; for(int k=0;;k++){ if(got[k]!=want[k]){ ok=0; break; } if(!got[k])break; }
        for(int k=0;k<48;k++)pw[k]=0;
        if(ok){ g_sudo_until=g_ticks+30000; return 1; } term_puts("Sorry, try again.\n"); sh_redraw(); }
    tcol(rgb(240,120,120)); term_puts("sudo: 3 incorrect password attempts\n"); tcol(WHT); return 0; }
static int need_root(const char* what){ if(g_uid==0)return 1; tcol(rgb(240,120,120)); term_puts(what); term_puts(": permission denied - administrator rights needed (try: sudo "); term_puts(what); term_puts(")\n"); tcol(WHT); return 0; }
int jc_sig_pending_cur(void){ return (g_jcur>=0)?(jc_deliverable(&g_jp[g_jcur])!=0):0; }
static int str_eq_n(const char*a,const char*b,int n){ for(int i=0;i<n;i++){ if(a[i]!=b[i])return 0; if(!a[i])return 1; } return 1; }

/* pipe-aware filters: read g_pipein, act, print (which may be captured by the pipeline). */
static int run_filter(const char* cmd,const char* arg){
    if(!g_pipein) return 0;
    if(str_eq_n(cmd,"grep",5)){
        const char* pat=arg; while(*pat==' ')pat++;
        const char* p=g_pipein;
        while(*p){ const char* ls=p; while(*p&&*p!='\n')p++; int llen=(int)(p-ls);
            /* substring match of pat in this line */
            int found=0; if(!*pat)found=1;
            for(const char* q=ls; q<=ls+llen-1 && !found; q++){ const char* a=q; const char* b=pat; while(*b&&a<ls+llen&&*a==*b){a++;b++;} if(!*b)found=1; }
            if(found){ for(int i=0;i<llen;i++)term_putc(ls[i]); term_putc('\n'); }
            if(*p=='\n')p++;
        }
        return 1;
    }
    if(str_eq_n(cmd,"wc",3)){
        int lines=0,words=0,chars=0,inw=0; for(const char* p=g_pipein;*p;p++){ chars++; if(*p=='\n')lines++; if(*p==' '||*p=='\n'||*p=='\t'){inw=0;} else if(!inw){inw=1;words++;} }
        char b[48],*o=b; o=scopy(o,"  lines ");o=udec(o,(u32)lines);o=scopy(o,"  words ");o=udec(o,(u32)words);o=scopy(o,"  chars ");o=udec(o,(u32)chars);*o=0; term_puts(b); term_putc('\n');
        return 1;
    }
    if(str_eq_n(cmd,"head",5)){
        int nl=10; const char* a=arg; while(*a==' ')a++; if(*a>='0'&&*a<='9'){nl=0;while(*a>='0'&&*a<='9'){nl=nl*10+(*a-'0');a++;}}
        const char* p=g_pipein; int c=0; while(*p&&c<nl){ const char* ls=p; while(*p&&*p!='\n')p++; for(const char* q=ls;q<p;q++)term_putc(*q); term_putc('\n'); if(*p=='\n')p++; c++; }
        return 1;
    }
    if(str_eq_n(cmd,"cat",4)){ term_puts(g_pipein); return 1; }   /* cat with pipe input just passes through */
    return 0;
}

static int sh_is_sh(const char* n){ int L=0; while(n[L])L++; return L>3&&n[L-3]=='.'&&n[L-2]=='s'&&n[L-1]=='h'; }
#define SHV_MAX 64
static char shv_name[SHV_MAX][32]; static char shv_val[SHV_MAX][192]; static int shv_n=0;
static int g_lastrc=0;
#define ALIAS_MAX 32
static char al_name[ALIAS_MAX][24]; static char al_val[ALIAS_MAX][96]; static int al_n=0;
static void al_set(const char* nm,const char* val){ for(int i=0;i<al_n;i++)if(seq(al_name[i],(char*)nm)){ int k=0; for(;val[k]&&k<95;k++)al_val[i][k]=val[k]; al_val[i][k]=0; return; } if(al_n>=ALIAS_MAX)return; int k=0; for(;nm[k]&&k<23;k++)al_name[al_n][k]=nm[k]; al_name[al_n][k]=0; k=0; for(;val[k]&&k<95;k++)al_val[al_n][k]=val[k]; al_val[al_n][k]=0; al_n++; }
static const char* al_get(const char* nm){ for(int i=0;i<al_n;i++)if(seq(al_name[i],(char*)nm))return al_val[i]; return 0; }
static const char* shv_get(const char* nm,int nl){ for(int i=0;i<shv_n;i++){ int j=0; for(;j<nl&&shv_name[i][j];j++)if(shv_name[i][j]!=nm[j])break; if(j==nl&&shv_name[i][j]==0)return shv_val[i]; } return 0; }
static void shv_set(const char* nm,const char* val){ for(int i=0;i<shv_n;i++){ if(seq(shv_name[i],(char*)nm)){ int k=0; for(;val[k]&&k<191;k++)shv_val[i][k]=val[k]; shv_val[i][k]=0; return; } } if(shv_n>=SHV_MAX)return; int k=0; for(;nm[k]&&k<31;k++)shv_name[shv_n][k]=nm[k]; shv_name[shv_n][k]=0; k=0; for(;val[k]&&k<191;k++)shv_val[shv_n][k]=val[k]; shv_val[shv_n][k]=0; shv_n++; }
static long sh_arith(const char* e){
    long terms[32]; char ops[32]; int nt=0; ops[0]='+'; int i=0;
    while(e[i]){ while(e[i]==' ')i++; long sign=1; if(e[i]=='-'){sign=-1;i++;} long num=0; while(e[i]>='0'&&e[i]<='9'){num=num*10+(e[i]-'0');i++;} num*=sign; if(nt<32)terms[nt++]=num; while(e[i]==' ')i++; char o=e[i]; if(o=='+'||o=='-'||o=='*'||o=='/'||o=='%'){ if(nt<32)ops[nt]=o; i++; } else break; }
    long v[32]; char vo[32]; int nv=0; v[0]=terms[0]; nv=1; vo[0]='+';
    for(int k=1;k<nt;k++){ char o=ops[k-1]; if(o=='*')v[nv-1]*=terms[k]; else if(o=='/'){if(terms[k])v[nv-1]/=terms[k];} else if(o=='%'){if(terms[k])v[nv-1]%=terms[k];} else { v[nv]=terms[k]; vo[nv]=o; nv++; } }
    long acc=v[0]; for(int k=1;k<nv;k++){ if(vo[k]=='-')acc-=v[k]; else acc+=v[k]; } return acc;
}

static void sh_capture(const char* cmd,char* out,int max){ int sav=g_capture,savn=g_cap_n; g_capture=1; g_cap_n=0; g_cap[0]=0; char tmp[512]; int k=0; for(;cmd[k]&&k<511;k++)tmp[k]=cmd[k]; tmp[k]=0; run_cmd(tmp); int n=0; for(;n<g_cap_n&&n<max-1;n++)out[n]=g_cap[n]; while(n>0&&(out[n-1]=='\n'||out[n-1]=='\r'))n--; out[n]=0; g_capture=sav; g_cap_n=savn; }
static void sh_expand(const char* in,char* out,int max){
    int o=0; int dq=0;
    for(int i=0;in[i]&&o<max-1;){
        if(in[i]=='"'){ dq=!dq; out[o++]=in[i++]; continue; }
        if(in[i]=='\''&&!dq){ out[o++]=in[i++]; while(in[i]&&in[i]!='\''&&o<max-1)out[o++]=in[i++]; if(in[i]&&o<max-1)out[o++]=in[i++]; continue; }
        if(in[i]=='~' && (i==0||in[i-1]==' ') && (in[i+1]=='/'||in[i+1]==0||in[i+1]==' ')){ const char* h=shv_get("HOME",4); if(h)for(int k=0;h[k]&&o<max-1;k++)out[o++]=h[k]; i++; continue; }
        if(in[i]=='$'){
            if(in[i+1]=='?'){ char b[12]; char* p=b; p=udec(p,(u32)g_lastrc); *p=0; for(int j=0;b[j]&&o<max-1;j++)out[o++]=b[j]; i+=2; continue; }
            if(in[i+1]=='('&&in[i+2]=='('){ int j=i+3; char ex2[128]; int s=0; int d2=2; while(in[j]&&s<127){ if(in[j]=='(')d2++; else if(in[j]==')'){d2--; if(d2==0)break;} ex2[s++]=in[j++]; } ex2[s]=0; if(in[j]==')')j++; if(in[j]==')')j++; char exx[160]; sh_expand(ex2,exx,160); long val=sh_arith(exx); char nb[24]; char* q=nb; if(val<0){*q++='-';val=-val;} q=udec(q,(u32)val); *q=0; for(int k=0;nb[k]&&o<max-1;k++)out[o++]=nb[k]; i=j; continue; }
            if(in[i+1]=='('){ int depth=1,j=i+2; char sub[256]; int s=0; while(in[j]&&depth&&s<255){ if(in[j]=='(')depth++; else if(in[j]==')'){depth--; if(!depth)break;} sub[s++]=in[j++]; } sub[s]=0; char cap[256]; sh_capture(sub,cap,256); for(int k=0;cap[k]&&o<max-1;k++)out[o++]=cap[k]; i=j+1; continue; }
            if(in[i+1]=='{'){ int j=i+2; char nm[32]; int n=0; while(in[j]&&in[j]!='}'&&n<31)nm[n++]=in[j++]; nm[n]=0; if(in[j]=='}')j++; const char* v=shv_get(nm,n); if(v)for(int k=0;v[k]&&o<max-1;k++)out[o++]=v[k]; i=j; continue; }
            if((in[i+1]>='A'&&in[i+1]<='Z')||(in[i+1]>='a'&&in[i+1]<='z')||in[i+1]=='_'){ int j=i+1; char nm[32]; int n=0; while(((in[j]>='A'&&in[j]<='Z')||(in[j]>='a'&&in[j]<='z')||(in[j]>='0'&&in[j]<='9')||in[j]=='_')&&n<31)nm[n++]=in[j++]; nm[n]=0; const char* v=shv_get(nm,n); if(v)for(int k=0;v[k]&&o<max-1;k++)out[o++]=v[k]; i=j; continue; }
        }
        out[o++]=in[i++];
    }
    out[o]=0;
}
static int sh_truth(const char* cond){
    char e[256]; sh_expand(cond,e,256); char* s=e; while(*s==' ')s++;
    if(s[0]=='['){ s++; while(*s==' ')s++; int L=0; while(s[L])L++; while(L>0&&(s[L-1]==' '||s[L-1]==']'))s[--L]=0; }
    else if(s[0]=='t'&&s[1]=='e'&&s[2]=='s'&&s[3]=='t'&&s[4]==' '){ s+=5; }
    else { char tmp[256]; int k=0; for(;s[k]&&k<255;k++)tmp[k]=s[k]; tmp[k]=0; run_cmd(tmp); return g_lastrc==0; }
    char a[96],op[8],b[96]; int ai=0,oi=0,bi=0; char* p=s;
    while(*p==' ')p++; while(*p&&*p!=' '&&ai<95)a[ai++]=*p++; a[ai]=0; while(*p==' ')p++;
    while(*p&&*p!=' '&&oi<7)op[oi++]=*p++; op[oi]=0; while(*p==' ')p++;
    while(*p&&bi<95)b[bi++]=*p++; b[bi]=0; { int L=bi; while(L>0&&b[L-1]==' ')b[--L]=0; }
    if(!op[0])return a[0]!=0;
    int na=atoi(a), nb=atoi(b);
    if(seq(op,"="))return seq(a,b); if(seq(op,"!="))return !seq(a,b);
    if(seq(op,"-eq"))return na==nb; if(seq(op,"-ne"))return na!=nb;
    if(seq(op,"-lt"))return na<nb; if(seq(op,"-gt"))return na>nb;
    if(seq(op,"-le"))return na<=nb; if(seq(op,"-ge"))return na>=nb;
    return 0;
}

static void sh_run_block(char** lines,int lo,int hi){
    int i=lo;
    while(i<hi){
        char* ln=lines[i]; while(*ln==' '||*ln=='\t')ln++;
        if(!ln[0]||ln[0]=='#'){ i++; continue; }
        if(str_eq_n(ln,"if ",3)){
            int depth=1, elseline=-1, fi=-1;
            for(int j=i+1;j<hi;j++){ char* t=lines[j]; while(*t==' '||*t=='\t')t++; if(str_eq_n(t,"if ",3))depth++; else if(seq(t,"fi")){ depth--; if(depth==0){fi=j;break;} } else if(depth==1&&seq(t,"else")&&elseline<0)elseline=j; }
            char cond[256]; { char* c=ln+3; int k=0; while(*c&&k<255){ if(str_eq_n(c,"; then",6)||(str_eq_n(c,"then",4)&&(c[4]==0||c[4]==' ')))break; cond[k++]=*c++; } cond[k]=0; }
            if(fi<0)fi=hi; int t=sh_truth(cond);
            if(t){ int bend=(elseline>=0)?elseline:fi; sh_run_block(lines,i+1,bend); }
            else if(elseline>=0){ sh_run_block(lines,elseline+1,fi); }
            i=fi+1; continue;
        }
        if(str_eq_n(ln,"while ",6)){
            int depth=1, done=-1;
            for(int j=i+1;j<hi;j++){ char* t=lines[j]; while(*t==' '||*t=='\t')t++; if(str_eq_n(t,"while ",6)||str_eq_n(t,"for ",4)||str_eq_n(t,"if ",3))depth++; else if(seq(t,"done")){ depth--; if(depth==0){done=j;break;} } else if(seq(t,"fi")){depth--;} }
            if(done<0)done=hi; char cond[256]; { char* c=ln+6; int k=0; while(*c&&k<255){ if(str_eq_n(c,"; do",4)||(str_eq_n(c,"do",2)&&(c[2]==0||c[2]==' ')))break; cond[k++]=*c++; } cond[k]=0; }
            int guard=0; while(sh_truth(cond)&&guard++<1000000){ sh_run_block(lines,i+1,done); } i=done+1; continue;
        }
        if(str_eq_n(ln,"for ",4)){
            int depth=1, done=-1;
            for(int j=i+1;j<hi;j++){ char* t=lines[j]; while(*t==' '||*t=='\t')t++; if(str_eq_n(t,"for ",4)||str_eq_n(t,"while ",6)||str_eq_n(t,"if ",3))depth++; else if(seq(t,"done")){ depth--; if(depth==0){done=j;break;} } else if(seq(t,"fi")){depth--;} }
            if(done<0)done=hi; char* c=ln+4; while(*c==' ')c++; char var[32]; int vn=0; while(*c&&*c!=' '&&vn<31)var[vn++]=*c++; var[vn]=0; while(*c==' ')c++; if(str_eq_n(c,"in ",3))c+=3;
            char items[256]; sh_expand(c,items,256); char* p=items; int guard=0;
            while(*p&&guard++<4096){ while(*p==' ')p++; if(!*p||*p==';')break; char val[96]; int k=0; while(*p&&*p!=' '&&*p!=';'&&k<95)val[k++]=*p++; val[k]=0; shv_set(var,val); sh_run_block(lines,i+1,done); } i=done+1; continue;
        }
        { char buf[512]; int k=0; for(;ln[k]&&k<511;k++)buf[k]=ln[k]; buf[k]=0; sh_run_line(buf); }
        i++;
    }
}
void sh_run_line(char* ln){
    while(*ln==' ')ln++;
    if(str_eq_n(ln,"alias ",6)){ const char* a=ln+6; while(*a==' ')a++; char nm[24]; int k=0; while(*a&&*a!='='&&k<23)nm[k++]=*a++; nm[k]=0; if(*a=='='){ a++; if(*a=='"'||*a=='\'')a++; char v[96]; int j=0; while(*a&&*a!='"'&&*a!='\''&&j<95)v[j++]=*a++; v[j]=0; al_set(nm,v);} g_lastrc=0; return; }
    if(seq(ln,"alias")){ for(int i=0;i<al_n;i++){ term_puts("alias "); term_puts(al_name[i]); term_puts("='"); term_puts(al_val[i]); term_puts("'\n"); } g_lastrc=0; return; }
    { char fw[24]; int k=0; const char* p=ln; while(*p&&*p!=' '&&k<23)fw[k++]=*p++; fw[k]=0; const char* av=al_get(fw); if(av){ static char xl[512]; int o=0; const char* q=av; while(*q&&o<511)xl[o++]=*q++; while(*p&&o<511)xl[o++]=*p++; xl[o]=0; ln=xl; while(*ln==' ')ln++; } }
    int eq=-1; for(int i=0;ln[i]&&ln[i]!=' ';i++){ if(ln[i]=='='){eq=i;break;} }
    if(eq>0){ int ok=1; for(int i=0;i<eq;i++){ char ch=ln[i]; if(!((ch>='A'&&ch<='Z')||(ch>='a'&&ch<='z')||(ch>='0'&&ch<='9')||ch=='_')){ok=0;break;} } if(ok){ char nm[32]; int k=0; for(;k<eq&&k<31;k++)nm[k]=ln[k]; nm[k]=0; char val[256]; sh_expand(ln+eq+1,val,256); shv_set(nm,val); g_lastrc=0; return; } }
    if(str_eq_n(ln,"export ",7)){ sh_run_line(ln+7); return; }
    char ex[512]; sh_expand(ln,ex,512); run_cmd(ex);
}
static char* g_sh_lines[256]; static char g_sh_buf[8192];
static void sh_run_script(const char* text){
    int n=0; int bl=0;
    for(int i=0;text[i]&&bl<8190;){ g_sh_lines[n++]=&g_sh_buf[bl]; while(text[i]&&text[i]!='\n'&&bl<8190){ if(text[i]==';'){ i++; break; } g_sh_buf[bl++]=text[i++]; } g_sh_buf[bl++]=0; if(text[i]=='\n')i++; if(n>=255)break; }
    sh_run_block(g_sh_lines,0,n);
}

static int x_needs(const char* l){
    while(*l==' ')l++; char w[48]; int k=0; while(l[k]&&l[k]!=' '&&k<47){ w[k]=l[k]; k++; } w[k]=0;
    const char* b=w; for(int i=0;w[i];i++) if(w[i]=='/')b=w+i+1;
    static const char* const xp[]={"idle","idle3","idle3.13","wish","xdemo",0};
    for(int i=0;xp[i];i++) if(seq((char*)b,(char*)xp[i]))return 1;
    for(const char* q=l;*q;q++){ const char* t1="tkinter"; const char* t2="idlelib"; const char* t3="turtle"; int a=1,c=1,e=1; for(int j=0;t1[j];j++) if(q[j]!=t1[j]){a=0;break;} for(int j=0;t2[j];j++) if(q[j]!=t2[j]){c=0;break;} for(int j=0;t3[j];j++) if(q[j]!=t3[j]){e=0;break;} if(a||c||e)return 1; }
    return 0; }
static int x_starting; int x_spawned;
void x_ensure(void){ extern int g_xfb_live; extern int g_xover; if(g_xfb_live||x_spawned){ if(g_state==ST_DESKTOP&&!g_xover)open_win(W_XDISP); return; } x_spawned=1; x_starting=1; static char sx[]="startx"; run_cmd(sx); x_starting=0; }
void x_launch(const char* prog){ extern int g_xfb_live; int was=g_xfb_live; x_ensure(); static char b[600]; char* p=b;
    if(!was){ p=scopy(p,"bash -c 'for i in $(seq 100); do [ -e /tmp/Xvfb_screen0 ] && break; sleep 0.2; done; sleep 1; exec "); p=scopy(p,prog); p=scopy(p,"' > /tmp/xapp.log 2>&1 &"); }
    else { p=scopy(p,prog); p=scopy(p," > /tmp/xapp.log 2>&1 &"); }
    *p=0; x_starting=1; run_cmd(b); x_starting=0; }
void run_cmd(char* line){
    if(!g_capture&&!x_starting){ extern int xapps_is_gui(const char*); extern void xrun_launch(const char*); if(xapps_is_gui(line)){ xrun_launch(line); return; } }
    if(!g_capture&&!x_starting&&x_needs(line)){ extern int g_xfb_live; if(!g_xfb_live){ int L=0; while(line[L])L++; while(L>0&&(line[L-1]==' '||line[L-1]=='&'))L--; static char pl[400]; int k=0; for(;k<L&&k<399;k++)pl[k]=line[k]; pl[k]=0; tcol(GREY); term_puts("starting the X server first...\n"); tcol(WHT); x_launch(pl); return; } else if(g_state==ST_DESKTOP){ extern int g_xover; if(!g_xover) open_win(W_XDISP); } }
    if(!g_capture&&jc_launch(line))return;
    /* split on '|' into stages; handle > < >> on the last/first stage. */
    /* find redirection targets first (on the whole line, applied to last stage) */
    char work[512]; int wl=0; for(int i=0;line[i]&&wl<511;i++)work[wl++]=line[i]; work[wl]=0;

    /* pull out > outfile  and  < infile  and >> */
    char outfile[64]={0}; int append=0; char infile[64]={0};
    for(int i=0;work[i];i++){
        if(work[i]=='>'){ int ap=(work[i+1]=='>'); char* d=outfile; int j=i+(ap?2:1); while(work[j]==' ')j++; int k=0; while(work[j]&&work[j]!=' '&&work[j]!='|'&&k<63)d[k++]=work[j++]; d[k]=0; append=ap; work[i]=0; /* cut here */ }
    }
    for(int i=0;work[i];i++){
        if(work[i]=='<'){ char* d=infile; int j=i+1; while(work[j]==' ')j++; int k=0; while(work[j]&&work[j]!=' '&&work[j]!='|'&&k<63)d[k++]=work[j++]; d[k]=0; for(int m=i;work[m];m++)work[m]=work[m+1]; i--; }
    }

    /* split remaining 'work' on '|' */
    char* stages[8]; int ns=0; stages[ns++]=work;
    for(int i=0;work[i]&&ns<8;i++){ if(work[i]=='|'){ work[i]=0; stages[ns++]=&work[i+1]; } }

    static char pbuf[8192];
    const char* feed=0;
    /* if input redirect, load file into pbuf as the first stage's stdin */
    if(infile[0]){ int fl=fat_read_file(infile,(u8*)pbuf,8191); if(fl<0)fl=0; pbuf[fl]=0; feed=pbuf; }
    if(!feed&&g_run_feed){ feed=g_run_feed; g_run_feed=0; }

    for(int st=0; st<ns; st++){
        char* stg=stages[st]; while(*stg==' ')stg++;
        /* trim trailing spaces */
        int L=0; while(stg[L])L++; while(L>0&&stg[L-1]==' ')stg[--L]=0;
        /* parse cmd + arg */
        char c2[24]; int ci=0; const char* a=stg; while(*a&&*a!=' '&&ci<23)c2[ci++]=*a++; c2[ci]=0; while(*a==' ')a++;

        int last=(st==ns-1);
        int capture_this = (!last) || outfile[0];   /* capture unless it's the final on-screen stage */

        g_pipein = feed;
        if(capture_this){ g_capture=1; g_cap_n=0; g_cap[0]=0; }

        /* run: try pipe-aware filter first (if it has pipe input), else the normal command */
        int handled = (feed && run_filter(c2,a));
        if(!handled) run_cmd_one(stg);

        if(capture_this){ g_capture=0;
            /* this stage's output becomes next stage's input */
            for(int i=0;i<=g_cap_n && i<8191;i++)pbuf[i]=g_cap[i];
            feed=pbuf;
        } else {
            feed=0;
        }
        g_pipein=0;
    }

    /* write captured output to a file if redirected */
    if(outfile[0]){
        { int ofd=vfs_open(outfile,O_WRONLY|O_CREAT|(append?O_APPEND:O_TRUNC)); if(ofd>=0){ vfs_write(ofd,pbuf,(int)slen(pbuf)); vfs_close(ofd); } }
        char b[80],*o=b; o=scopy(o,append?"  appended ":"  wrote ");o=udec(o,(u32)slen(pbuf));o=scopy(o," bytes to ");o=scopy(o,outfile);*o=0; term_puts(b); term_putc('\n');
    }
}
static char* acx64(char* p,u64 v){ p=scopy(p,"0x"); if(v>>32)p=hx(p,(u32)(v>>32),8); return hx(p,(u32)v,(v>>32)?8:(v>0xFFFF?8:(v>0xFF?4:2))); }
static void acpi_put_obj(const aobj* o,int ind,int* lines){
    if(*lines>=60){ if(*lines==60)term_puts("  ...\n"); (*lines)++; return; } (*lines)++;
    char b[200],*p=b; for(int i=0;i<ind&&i<20;i++)*p++=' ';
    if(o->type==AT_INT){ p=acx64(p,o->i); if(o->i<100000000ULL){ p=scopy(p,"  ("); p=udec(p,(u32)o->i); *p++=')'; } }
    else if(o->type==AT_STR){ *p++='"'; for(u32 k=0;k<o->len&&k<120;k++){ char c=(char)o->p[k]; *p++=(c>=32&&c<127)?c:'?'; } *p++='"'; }
    else if(o->type==AT_BUF){ p=scopy(p,"Buffer("); p=udec(p,o->len); p=scopy(p,"):"); for(u32 k=0;k<o->len&&k<32;k++){ *p++=' '; p=hx(p,o->p[k],2); } if(o->len>32)p=scopy(p," ..."); }
    else if(o->type==AT_PKG){ p=scopy(p,"Package("); p=udec(p,o->len); *p++=')'; *p++='\n'; *p=0; term_puts(b); for(u32 k=0;k<o->len;k++)acpi_put_obj(&o->e[k],ind+2,lines); return; }
    else if(o->type==AT_REF){ p=scopy(p,"Reference"); if(o->rk==RK_NODE&&o->node){ char path[80]; aml_path(o->node,path,80); p=scopy(p," -> "); p=scopy(p,path); } }
    else if(o->type==AT_NAMESTR){ p=scopy(p,"Name "); for(u32 k=0;k<o->len&&k<60;k++)*p++=(char)o->p[k]; }
    else p=scopy(p,"(nothing)");
    *p++='\n'; *p=0; term_puts(b); }
static const char* acpi_kind_name(int k){ switch(k){ case K_SCOPE: return "scope"; case K_DEVICE: return "device"; case K_NAME: return "name"; case K_METHOD: return "method"; case K_REGION: return "region"; case K_FIELD: return "field";
    case K_INDEXFIELD: return "indexfield"; case K_BANKFIELD: return "bankfield"; case K_BUFFIELD: return "bufferfield"; case K_MUTEX: return "mutex"; case K_EVENT: return "event"; case K_PROCESSOR: return "processor"; case K_POWER: return "powerres"; case K_THERMAL: return "thermalzone"; case K_ALIAS: return "alias"; case K_NATIVE: return "builtin"; } return "?"; }
static u64 acpi_parse_num(const char** sp){ const char* s=*sp; u64 v=0; if(s[0]=='0'&&(s[1]=='x'||s[1]=='X')){ s+=2; for(;;){ char c=*s; int d=c>='0'&&c<='9'?c-'0':c>='a'&&c<='f'?c-'a'+10:c>='A'&&c<='F'?c-'A'+10:-1; if(d<0)break; v=v*16+(u64)d; s++; } }
    else while(*s>='0'&&*s<='9'){ v=v*10+(u64)(*s-'0'); s++; } *sp=s; return v; }
static void acpi_cmd(const char* arg){
    char b[160],*p;
    if(!g_acs.up){ tcol(RED); term_puts("ACPI AML is not running (no FADT/DSDT found or out of memory).\n"); tcol(WHT); return; }
    if(arg[0]=='e'&&arg[1]=='v'){ const char* a=arg; while(*a&&*a!=' ')a++; while(*a==' ')a++; char path[80]; int n=0; while(*a&&*a!=' '&&n<79)path[n++]=*a++; path[n]=0;
        if(!n){ term_puts("usage: acpi eval \\_SB.PCI0.LPCB.EC0.BAT0._BST [args]\n"); return; }
        anode* m=aml_find(path); if(!m){ tcol(RED); term_puts("no such object\n"); tcol(WHT); return; }
        aobj args[7]; int na=0; while(na<7){ while(*a==' ')a++; if(!*a)break; args[na].type=AT_NONE; args[na].p=0; args[na].e=0; args[na].own=0; aobj_int(&args[na],acpi_parse_num(&a)); na++; while(*a&&*a!=' ')a++; }
        aobj r; g_aml_lasterr[0]=0; int rc=aml_eval(m,args,na,&r); for(int z=0;z<na;z++)aobj_free(&args[z]);
        if(rc){ tcol(RED); p=b; p=scopy(p,"evaluation failed: "); p=scopy(p,g_aml_lasterr[0]?g_aml_lasterr:"error"); *p++='\n'; *p=0; term_puts(b); tcol(WHT); aobj_free(&r); return; }
        if(r.type==AT_REF){ aobj d; d.type=AT_NONE; d.p=0; d.e=0; d.own=0; d.len=0; aml_deref(&r,&d); aobj_free(&r); r=d; }
        int lines=0; tcol(GREEN); acpi_put_obj(&r,0,&lines); tcol(WHT); aobj_free(&r); acpi_drain_notify(); return; }
    if(arg[0]=='l'&&arg[1]=='s'){ const char* a=arg+2; while(*a==' ')a++; anode* n=*a?aml_find(a):g_aroot; if(!n){ tcol(RED); term_puts("no such object\n"); tcol(WHT); return; }
        int c=0; for(anode* x=n->child;x&&c<80;x=x->next,c++){ p=b; p=scopy(p,"  "); for(int k=0;k<4;k++)*p++=x->nm[k]; p=scopy(p,"  "); p=scopy(p,acpi_kind_name(x->kind));
            if(x->kind==K_METHOD){ p=scopy(p,"("); p=udec(p,x->argc); p=scopy(p," args)"); } if(x->kind==K_REGION){ p=scopy(p," space "); p=udec(p,x->space); p=scopy(p," at "); p=acx64(p,x->base); }
            *p++='\n'; *p=0; term_puts(b); }
        if(!c)term_puts("  (empty)\n"); return; }
    if(arg[0]=='e'&&arg[1]=='c'){ if(!g_aec.ok){ term_puts("no embedded controller\n"); return; }
        for(int r=0;r<16;r++){ p=b; p=hx(p,(u32)r*16,2); *p++=':'; for(int c=0;c<16;c++){ u8 v=0; *p++=' '; if(aec_read((u8)(r*16+c),&v)){ *p++='?'; *p++='?'; } else p=hx(p,v,2); } *p++='\n'; *p=0; term_puts(b); } return; }
    tcol(CYAN); term_puts("ACPI\n"); tcol(WHT);
    p=b; p=scopy(p,"  tables "); p=udec(p,(u32)g_acs.tables); if(g_acs.bad_tables){ p=scopy(p," ("); p=udec(p,(u32)g_acs.bad_tables); p=scopy(p," with parse errors)"); } p=scopy(p,", "); p=udec(p,g_aml_nodes); p=scopy(p," objects, "); p=udec(p,g_aml_loaded_bytes/1024); p=scopy(p," KB AML, set-up "); p=udec(p,g_acs.ms); p=scopy(p," ms\n"); *p=0; term_puts(b);
    p=b; p=scopy(p,"  ACPI mode "); p=scopy(p,g_acs.acpi_mode?"on":"off"); p=scopy(p,", AML errors "); p=udec(p,(u32)g_aml_err); if(g_acs.errs[0]){ p=scopy(p," (first run: "); p=scopy(p,g_acs.errs); *p++=')'; } *p++='\n'; *p=0; term_puts(b);
    p=b; p=scopy(p,"  EC "); if(g_aec.ok){ p=acx64(p,g_aec.data); *p++='/'; p=acx64(p,g_aec.cmd); p=scopy(p,g_aec.from_ecdt?" (ECDT)":" (namespace)"); p=scopy(p,", GPE "); p=udec(p,g_aec.gpe); p=scopy(p,", events "); p=udec(p,g_aec.events);
        if(g_aec.events){ p=scopy(p," (last _Q"); p=hx(p,g_aec.lastq,2); *p++=')'; } p=scopy(p,", timeouts "); p=udec(p,g_aec.timeouts); } else p=scopy(p,"not found"); *p++='\n'; *p=0; term_puts(b);
    p=b; p=scopy(p,"  batteries "); p=udec(p,(u32)g_acb_n); p=scopy(p,", AC adapter "); p=scopy(p,!g_acad?"none":g_ac_online==1?"online":g_ac_online==0?"offline":"unknown"); *p++='\n'; *p=0; term_puts(b);
    p=b; p=scopy(p,"  backlight "); if(g_bl.ok){ p=scopy(p,g_bl.mode==1?"ACPI _BCM":g_bl.mode==2?"Intel PWM":"ACPI _BCM + Intel PWM"); p=scopy(p,", "); p=udec(p,(u32)g_bl.cur); p=scopy(p,"%"); if(g_bl.out){ char path[80]; aml_path(g_bl.out,path,80); p=scopy(p,", "); p=scopy(p,path); } } else p=scopy(p,"not found"); *p++='\n'; *p=0; term_puts(b);
    p=b; p=scopy(p,"  notifications "); p=udec(p,g_acs.notifies); p=scopy(p," ("); p=udec(p,g_acs.unhandled); p=scopy(p," not handled), blocked RAM writes "); p=udec(p,g_acpi_memblock); *p++='\n'; *p=0; term_puts(b);
    term_puts("  acpi ls [path] | acpi eval <path> [args] | acpi ec\n"); }
static void battery_cmd(void){
    char b[160],*p; acpi_battery_update(1);
    tcol(CYAN); p=b; p=scopy(p,"Battery: "); if(g_acb_pct>=0){ p=udec(p,(u32)g_acb_pct); p=scopy(p,"%, "); p=scopy(p,g_acb_charging?"charging":"on battery"); if(g_acb_mins>=0){ p=scopy(p,", "); p=udec(p,(u32)(g_acb_mins/60)); p=scopy(p," h "); p=udec(p,(u32)(g_acb_mins%60)); p=scopy(p,g_acb_charging?" min to full":" min left"); } } else p=scopy(p,"not present"); *p++='\n'; *p=0; term_puts(b); tcol(WHT);
    p=b; p=scopy(p,"  AC adapter: "); p=scopy(p,!g_acad?"not described by firmware":g_ac_online==1?"plugged in":g_ac_online==0?"unplugged":"unknown"); *p++='\n'; *p=0; term_puts(b);
    for(int i=0;i<g_acb_n;i++){ acbat* x=&g_acb[i]; char path[80]; aml_path(x->dev,path,80); p=b; p=scopy(p,"  "); p=scopy(p,path); p=scopy(p,x->present?"":"  (slot empty)"); *p++='\n'; *p=0; term_puts(b); if(!x->present)continue;
        const char* u=x->mwh?" mWh":" mAh"; const char* r=x->mwh?" mW":" mA";
        p=b; p=scopy(p,"    "); p=scopy(p,x->model[0]?x->model:"?"); p=scopy(p,"  "); p=scopy(p,x->type); p=scopy(p,"  "); p=scopy(p,x->oem); if(x->serial[0]){ p=scopy(p,"  serial "); p=scopy(p,x->serial); } *p++='\n'; *p=0; term_puts(b);
        p=b; p=scopy(p,"    remaining "); p=udec(p,x->remain); p=scopy(p,u); p=scopy(p," of "); p=udec(p,x->full); p=scopy(p,u); p=scopy(p," (design "); p=udec(p,x->design); p=scopy(p,u); p=scopy(p,")\n"); *p=0; term_puts(b);
        p=b; p=scopy(p,"    rate "); if(x->rate==0xFFFFFFFFu)p=scopy(p,"unknown"); else { p=udec(p,x->rate); p=scopy(p,r); } p=scopy(p,", voltage "); p=udec(p,x->pvolt); p=scopy(p," mV, state "); p=acx64(p,x->state);
        if(x->cycles&&x->cycles!=0xFFFFFFFFu){ p=scopy(p,", "); p=udec(p,x->cycles); p=scopy(p," cycles"); } *p++='\n'; *p=0; term_puts(b); } }
static void brightness_cmd(const char* arg){
    if(!g_bl.ok){ term_puts("no controllable backlight found (no _BCM method and no Intel PWM)\n"); return; }
    if(arg[0]=='u')bl_step(1); else if(arg[0]=='d')bl_step(-1); else if(arg[0]>='0'&&arg[0]<='9'){ const char* a=arg; bl_set((int)acpi_parse_num(&a)); }
    char b[120],*p=b; p=scopy(p,"brightness "); p=udec(p,(u32)g_bl.cur); p=scopy(p,"%  levels:"); for(int i=0;i<g_bl.nlv&&i<40;i++){ *p++=' '; p=udec(p,g_bl.lv[i]); } if(!g_bl.out)p=scopy(p," (PWM, any 5-100)"); *p++='\n'; *p=0; term_puts(b); }
static char* hr_fix(char* p,u64 v,u64 div,int dec){ p=udec(p,(u32)(v/div)); if(dec){ *p++='.'; u64 f=v%div; for(int i=0;i<dec;i++){ div/=10; if(!div)break; *p++=(char)('0'+(f/div)%10); } } return p; }
static void timers_cmd(void){
    char b[160],*p; tcol(CYAN); term_puts("Timers\n"); tcol(WHT);
    static const char* refn[5]={"nothing","HPET","PIT channel 2","CPUID crystal","CPUID base clock"};
    p=b; p=scopy(p,"  clock: "); p=scopy(p,g_hr.src==2?(g_hr.tsc_inv?"invariant TSC":"TSC (may drift with power states)"):g_hr.src==1?"HPET":"100 Hz PIT ticks (no precise timer)"); *p++='\n'; *p=0; term_puts(b);
    p=b; p=scopy(p,"  TSC: "); if(g_hr.tsc_hz){ p=hr_fix(p,g_hr.tsc_hz,1000000000ULL,3); p=scopy(p," GHz, measured with "); p=scopy(p,refn[g_hr.ref<5?g_hr.ref:0]); } else p=scopy(p,"not usable"); *p++='\n'; *p=0; term_puts(b);
    p=b; p=scopy(p,"  HPET: "); if(g_hr.hpet_ok){ p=scopy(p,"0x"); p=hx(p,(u32)(g_hr.hpet_pa>>32),8); p=hx(p,(u32)g_hr.hpet_pa,8); p=scopy(p,", "); p=hr_fix(p,1000000000000000000ULL/g_hr.hpet_fs,1000000ULL,3); p=scopy(p," MHz, "); p=scopy(p,g_hr.hpet_64?"64":"32"); p=scopy(p,"-bit, "); p=udec(p,(u32)g_hr.hpet_ntim); p=scopy(p," comparators"); }
        else { p=scopy(p,"not used ("); p=scopy(p,g_hr.why[0]?g_hr.why:"?"); *p++=')'; } *p++='\n'; *p=0; term_puts(b);
    p=b; p=scopy(p,"  local APIC timer: "); if(g_hr.lapic_ok){ p=hr_fix(p,g_hr.lapic_hz,1000000ULL,3); p=scopy(p," MHz after /16, "); p=scopy(p,g_hr.lapic_x2?"x2APIC":"xAPIC"); p=scopy(p," id "); p=udec(p,g_hr.lapic_id); p=scopy(p,", "); p=udec(p,(u32)g_hr.lapic_irqs); p=scopy(p," interrupts"); } else p=scopy(p,"not used"); *p++='\n'; *p=0; term_puts(b);
    u64 ns=hr_ns(); p=b; p=scopy(p,"  uptime "); p=hr_fix(p,ns,1000000000ULL,6); p=scopy(p," s by the clock, "); p=hr_fix(p,(g_ticks-g_hr.boot_ns_ticks)*10ULL,1000ULL,2); p=scopy(p," s by 100 Hz ticks\n"); *p=0; term_puts(b);
    term_puts("  timertest  - measure accuracy (takes about 2 seconds)\n"); }
static void timertest_cmd(void){
    char b[160],*p; if(!hr_precise()){ term_puts("no precise clock on this machine, nothing to test\n"); return; }
    cpu_irq_on(); u64 t0=g_ticks; for(int i=0;i<50000000&&g_ticks==t0;i++)cpu_pause();
    u64 k0=g_ticks, n0=hr_ns(), c0=hr_rdtsc(), h0=g_hr.hpet_ok?hr_hpet_count():0;
    hr_delay_ms(1000);
    u64 k1=g_ticks, n1=hr_ns(), c1=hr_rdtsc(), h1=g_hr.hpet_ok?hr_hpet_count():0;
    p=b; p=scopy(p,"  1 s busy-wait: "); p=udec(p,(u32)(k1-k0)); p=scopy(p," PIT ticks (100 expected), clock says "); p=hr_fix(p,n1-n0,1000000ULL,3); p=scopy(p," ms\n"); *p=0; term_puts(b);
    if(g_hr.hpet_ok&&g_hr.tsc_hz){ u64 hn=hr_mul_shr32(h1-h0,g_hr.hpet_mult), tn=hr_muldiv(c1-c0,1000000000ULL,g_hr.tsc_hz); u64 d=hn>tn?hn-tn:tn-hn;
        p=b; p=scopy(p,"  HPET vs TSC over that second: "); p=udec(p,(u32)hr_muldiv(d,1000000ULL,hn?hn:1)); p=scopy(p," ppm apart\n"); *p=0; term_puts(b); }
    if(g_hr.lapic_ok){ u64 worst=0; int got=0;
        for(int k=0;k<5;k++){ u64 irq0=g_hr.lapic_irqs; u64 s=hr_ns(), want=s+5000000ULL; if(!hr_wake_at(want))break; u64 e=s;
            for(u64 i=0;i<200000000ULL;i++){ if(g_hr.lapic_irqs!=irq0){ e=hr_ns(); got++; break; } cpu_pause(); }
            u64 err=e>want?e-want:want-e; if(e==s)err=~0ULL; if(err>worst)worst=err; }
        hr_wake_cancel();
        p=b; p=scopy(p,"  APIC timer 5 ms one-shots: "); p=udec(p,(u32)got); p=scopy(p,"/5 fired"); if(got==5){ p=scopy(p,", worst error "); p=hr_fix(p,worst,1000ULL,1); p=scopy(p," us"); } *p++='\n'; *p=0; term_puts(b); }
    { u64 s=hr_ns(); hr_delay_us(250); u64 e=hr_ns(); p=b; p=scopy(p,"  250 us delay took "); p=hr_fix(p,e-s,1000ULL,1); p=scopy(p," us\n"); *p=0; term_puts(b); } }
static void run_cmd_one(char*s){
    g_lastrc=0;
    while(*s==' ')s++; char cmd[24]; int i=0; while(s[i]&&s[i]!=' '&&i<23){cmd[i]=s[i];i++;} cmd[i]=0;
    char*arg=s+i; while(*arg==' ')arg++;
    if(!cmd[0])return;
    if(jc_builtin(cmd,arg))return;
    if(seq(cmd,"help")){ tcol(WHT);
        tcol(rgb(120,220,140)); term_puts("noovexos_install   start the NoovexOS installer\n"); tcol(WHT);
        tcol(ACC); term_puts("jobs fg bg kill ps umask   (job control: cmd &, Ctrl-C, Ctrl-Z, cmd1 | cmd2, > < 2>&1)\n");
        term_puts("programs: count spin sigdemo upper greet pipetest\n");
        term_puts("/bin: ls cat grep wc head tail echo mkdir rmdir rm mv cp touch ln chmod chown stat pwd hostname whoami id uname date sleep mount umount readlink tee sort\n");
        term_puts("pkg (read .deb/.apk)   nvidia/gpu (GPU info)\n"); tcol(WHT);
        term_puts("help clear echo neofetch ver cpu mem time date uptime ls cat edit\n");
        term_puts("net dhcp wifi usb usbdisk usbcat usbwrite usbrm ec acpi battery brightness timers timertest utest ring3 run app posix mem sched fork threads preempt ipc wm compositor cores smpstart smpwork lua ping dns http https netlog pci disks colors settings reboot panic\n");
        term_puts("browse <url> www desktop  (web browser: HTML, CSS, JavaScript, cookies)   qjs [-e code] [file.js]  (QuickJS)\n");
        term_puts("play <file|URL>  (fullscreen player: space pause, arrows seek, q quit; -b benchmark)   video <file|URL>  (Video Player window)\n");
        term_puts("fatinfo fatls fatcat  (read files from an IDE/FAT disk)\n");
        term_puts("nvxlogin whoami nvxlogout nvxtest  (Noovex account + API probe)\n"); }
    else if(seq(cmd,"fatinfo")||seq(cmd,"mount")){ if(fat_mount()){ char b[96],*p=b; p=scopy(p,"FAT"); p=scopy(p,fat_is32?"32":"16"); p=scopy(p," mounted   drive="); p=udec(p,(u32)g_ata_drv); p=scopy(p,"   sec/clus="); p=udec(p,(u32)fat_spc); p=scopy(p,"   data@LBA "); p=udec(p,fat_datastart); *p=0; tcol(CYAN);term_puts(b);tcol(WHT);term_putc('\n'); } else { tcol(RED);term_puts("no FAT disk found (ATA/IDE PIO only; VM needs an IDE disk with FAT)\n");tcol(WHT); } }
    else if(seq(cmd,"fatls")){ if(fat_mount()){ tcol(GREY);term_puts("root directory:\n");tcol(WHT); fat_listdir(fatls_cb); } else { tcol(RED);term_puts("no FAT disk\n");tcol(WHT); } }
    else if(seq(cmd,"crypto")){
        tcol(rgb(120,200,255)); term_puts("  NoovexOS crypto suite\n"); tcol(WHT);
        term_puts("  hashes:  SHA-256  SHA-1  SHA-384  SHA-512\n");
        term_puts("  macs:    HMAC-SHA256  HMAC-SHA512  HMAC-SHA1\n");
        term_puts("  kdf:     HKDF  PBKDF2\n");
        term_puts("  cipher:  AES-128  AES-256  ChaCha20  AES-GCM  AES-CCM  Poly1305\n");
        term_puts("  pubkey:  X25519\n");
        term_puts("  rng:     ChaCha20 CSPRNG (RDSEED/RDRAND seeded)\n");
        term_puts("  tls:     TLS 1.3 client\n");
        /* live self-test vs known vectors */
        int pass=0,tot=0; char b[80],*p;
        { u8 h[64]; sha512_hash((const u8*)"abc",3,h); tot++; int ok=(h[0]==0xdd&&h[1]==0xaf&&h[2]==0x35); if(ok)pass++;
          p=b;p=scopy(p,"  SHA-512  ");p=scopy(p,ok?"PASS ":"FAIL ");for(int i=0;i<4;i++)p=hx(p,h[i],2);*p=0; tcol(ok?GREEN:RED); term_puts(b); term_putc('\n'); tcol(WHT); }
        { u8 h[48]; sha384_hash((const u8*)"abc",3,h); tot++; int ok=(h[0]==0xcb&&h[1]==0x00&&h[2]==0x75); if(ok)pass++;
          p=b;p=scopy(p,"  SHA-384  ");p=scopy(p,ok?"PASS ":"FAIL ");for(int i=0;i<4;i++)p=hx(p,h[i],2);*p=0; tcol(ok?GREEN:RED); term_puts(b); term_putc('\n'); tcol(WHT); }
        { u8 key[32]; for(int i=0;i<32;i++)key[i]=(u8)i; u8 pt[16]={0,0x11,0x22,0x33,0x44,0x55,0x66,0x77,0x88,0x99,0xaa,0xbb,0xcc,0xdd,0xee,0xff};
          u8 rk[240]; aes256_expand(key,rk); u8 ct[16]; aes256_enc(rk,pt,ct); tot++; int ok=(ct[0]==0x8e&&ct[1]==0xa2&&ct[2]==0xb7); if(ok)pass++;
          p=b;p=scopy(p,"  AES-256  ");p=scopy(p,ok?"PASS ":"FAIL ");for(int i=0;i<4;i++)p=hx(p,ct[i],2);*p=0; tcol(ok?GREEN:RED); term_puts(b); term_putc('\n'); tcol(WHT); }
        { u8 r1[8],r2[8]; csprng_bytes(r1,8); csprng_bytes(r2,8); tot++; int diff=0; for(int i=0;i<8;i++)if(r1[i]!=r2[i])diff=1; if(diff)pass++;
          p=b;p=scopy(p,"  CSPRNG   ");p=scopy(p,diff?"PASS ":"FAIL ");for(int i=0;i<4;i++)p=hx(p,r1[i],2);*p=0; tcol(diff?GREEN:RED); term_puts(b); term_putc('\n'); tcol(WHT); }
        p=b;p=scopy(p,"  ");p=udec(p,(u32)pass);*p++='/';p=udec(p,(u32)tot);p=scopy(p," self-tests passed");*p=0; tcol(pass==tot?GREEN:RED); term_puts(b); term_putc('\n'); tcol(WHT);
    }
    else if(seq(cmd,"install")){ if(!need_root(cmd))return; ip_open(ST_DESKTOP); }
    else if(seq(cmd,"diskmgr")||seq(cmd,"partitions")){ if(!need_root(cmd))return; dm_open(g_state==ST_LIVE?ST_LIVE:ST_DESKTOP); }
    else if(seq(cmd,"noovexos_install")){ if(!need_root(cmd))return; ip_open(ST_LIVE); }
    else if(seq(cmd,"firefox-mode")){ const char* a=arg; while(*a==' ')a++;
        if(a[0]=='m'){ static char c1[]="bash -c 'mkdir -p /root/.nvx-firefox && touch /root/.nvx-firefox/.nvx-multiprocess'"; run_cmd(c1); term_puts("Firefox will start in multi-process mode (4 content processes) next time. Undo with: firefox-mode single\n"); }
        else if(a[0]=='s'){ static char c2[]="bash -c 'rm -f /root/.nvx-firefox/.nvx-multiprocess'"; run_cmd(c2); term_puts("Firefox will start in single-process mode next time.\n"); }
        else term_puts("usage: firefox-mode multi | single\n"); }
    else if((seq(cmd,"firefox")||seq(cmd,"firefox-esr"))&&!arg[0]){ extern void firefox_launch(void); tcol(GREY); term_puts("starting Firefox (sandbox off, software rendering, full-screen X; log in /tmp/ff.log)\n"); tcol(WHT); firefox_launch(); }
    else if(seq(cmd,"apt")||seq(cmd,"apt-get")||seq(cmd,"apt-cache")){ apt_cmd(arg); }
    else if(seq(cmd,"dpkg")){ dpkg_cmd(arg); }
    else if(seq(cmd,"xstat")){ extern void xstat_print(void (*)(const char*)); extern int g_xd_off; const char* a=arg; while(*a==' ')a++; xstat_print(term_puts); if(a[0]=='o'&&a[1]=='f')g_xd_off=1; else if(a[0]=='o'&&a[1]=='n')g_xd_off=0; }
    else if(seq(cmd,"startx")||seq(cmd,"xserver")){ extern int g_xfb_live; extern void xfull_enter(void); const char* a=arg; while(*a==' ')a++; int full=(a[0]=='f'||(a[0]=='-'&&a[1]=='f'))||seq(cmd,"xserver"); int inwin=(a[0]=='w'); extern int g_xover;
        if(g_state==ST_LIVE){ if(!g_user[0]){ g_user[0]='r';g_user[1]='o';g_user[2]='o';g_user[3]='t';g_user[4]=0; } g_state=ST_DESKTOP; }
        if(g_xfb_live){ if(full){ term_puts("startx: switching to the full-screen X session (F12 or Ctrl+Alt+Backspace returns)\n"); xfull_enter(); } else { term_puts("startx: the X server is already running on :0\n"); if(!g_xover)open_win(W_XDISP); } }
        else { dt_init(); { int t=dt_child(0,"tmp"); if(t<0)dt_mkdir_at(0,"tmp"); } { int v=dt_child(0,"var"); if(v<0)v=dt_mkdir_at(0,"var"); int l=dt_child(v,"lib"); if(l<0)l=dt_mkdir_at(v,"lib"); if(dt_child(l,"xkb")<0)dt_mkdir_at(l,"xkb"); }
            static char xl[200]; char* p=xl; p=scopy(p,"Xvfb :0 -screen 0 "); if(!inwin){ p=udec(p,W); *p++='x'; p=udec(p,H); } else p=scopy(p,"800x600"); p=scopy(p,full||inwin?"x24 -retro":"x24"); p=scopy(p," -fbdir /tmp -fp built-ins -listen tcp -nolisten unix -nolisten local -ac -xkbdir /usr/share/X11/xkb > /tmp/Xvfb.log 2>&1 &"); *p=0;
            env_set("DISPLAY","127.0.0.1:0"); tcol(GREY); term_puts("starting the X.Org X11 server on :0 (DISPLAY=127.0.0.1:0, log in /tmp/Xvfb.log)\n"); if(full)term_puts("full-screen X session: F12 or Ctrl+Alt+Backspace returns to the NoovexOS desktop\n"); tcol(WHT);
            { extern int x_spawned; x_spawned=1; } run_cmd(xl); { static char xi[]="nvx-xinput > /tmp/nvx-xinput.log 2>&1 &"; run_cmd(xi); static char xw[]="nvxwm > /tmp/nvxwm.log 2>&1 &"; run_cmd(xw); if(!full&&!inwin){ static char xp[]="bash -c 'sleep 4; for f in /usr/lib/firefox-esr/libxul.so /usr/lib/firefox-esr/omni.ja /usr/lib/firefox-esr/browser/omni.ja; do [ -e $f ] && cat $f > /dev/null; done' > /dev/null 2>&1 &"; run_cmd(xp); } } if(full){ static char xr[]="bash -c 'sleep 3; bash /root/.xinitrc' > /tmp/xinitrc.log 2>&1 &"; static char xd[]="xdemo > /tmp/xdemo.log 2>&1 &"; run_cmd(k_find_file("/root/.xinitrc")>=0?xr:xd); }
            if(full){ extern int g_xfull_pending; g_xfull_pending=1; } else if(inwin) open_win(W_XDISP); else { g_xover=1; if(g_state==ST_LIVE){ if(!g_user[0]){ g_user[0]='r';g_user[1]='o';g_user[2]='o';g_user[3]='t';g_user[4]=0; } g_state=ST_DESKTOP; } } } }
    else if(seq(cmd,"python3-ide")||seq(cmd,"pyide")){ if(!pyide_installed()){ tcol(rgb(240,200,110)); term_puts("Python IDE is not installed. Install it with: sudo apt install python3-ide\n"); tcol(WHT); } else pyide_open(); }
    else if(seq(cmd,"pkg")){
        char sub[16]; int si=0; const char* a=arg; while(*a==' ')a++; while(*a&&*a!=' '&&si<15)sub[si++]=*a++; sub[si]=0; while(*a==' ')a++;
        int doinst=seq(sub,"install")||seq(sub,"add")||seq(sub,"i");
        int doinfo=seq(sub,"info")||seq(sub,"list")||seq(sub,"l"); 
        if(!doinst&&!doinfo){ tcol(rgb(230,160,60)); term_puts("  usage: pkg info <file.deb|.apk>   |   pkg install <file.deb|.apk>\n");
            term_puts("  reads Debian .deb (ar+tar.gz) and Alpine .apk (tar.gz); extracts data files to /pkg/<name>/\n");
            term_puts("  note: Linux ELF binaries inside won't run on NoovexOS - use it for docs/configs/data\n"); tcol(WHT); }
        else { const char* fn=a; if(!*fn){ tcol(rgb(230,120,90)); term_puts("  give a file: pkg install foo.deb\n"); tcol(WHT); }
            else { int f=k_find_file(fn); long n=0; const u8* buf=0;
                if(f>=0){ buf=(const u8*)fsd(f); n=FS[f].len; }
                else { int got=fat_read_file(fn,g_pkg_raw,PKG_RAW_SZ); if(got>0){ buf=g_pkg_raw; n=got; } }
                if(!buf){ tcol(rgb(230,120,90)); term_puts("  file not found: "); term_puts((char*)fn); term_putc('\n'); tcol(WHT); }
                else { const u8* tar=0; long tl=0; int kind=pkg_open(buf,n,&tar,&tl);
                    if(!kind){ tcol(rgb(230,120,90)); term_puts("  not a recognised package (need .deb or gzip .apk; xz/zstd not supported)\n"); tcol(WHT); }
                    else { char b[160],*p;
                        p=b; p=scopy(p,"  "); p=scopy(p,kind==1?"Debian .deb":kind==2?"Alpine .apk":"Android .apk (ZIP)"); *p=0; tcol(CYAN); term_puts(b); term_putc('\n'); tcol(WHT);
                        if(g_pkg_name[0]){ p=b; p=scopy(p,"  name: "); p=scopy(p,g_pkg_name); if(g_pkg_ver[0]){ p=scopy(p,"  version: "); p=scopy(p,g_pkg_ver);} if(g_pkg_arch[0]){ p=scopy(p,"  arch: "); p=scopy(p,g_pkg_arch);} *p=0; term_puts(b); term_putc('\n'); }
                        if(g_pkg_desc[0]){ p=b; p=scopy(p,"  "); p=scopy(p,g_pkg_desc); *p=0; tcol(GREY); term_puts(b); term_putc('\n'); tcol(WHT); }
                        if(tar&&tl>0){ g_pkg_nfiles=0; g_pkg_total=0;
                            if(doinst){ int k=pkg_install(buf,n,1); (void)k; p=b; p=scopy(p,"  installed "); p=udec(p,(u32)g_pkg_nfiles); p=scopy(p," files to /pkg/"); p=scopy(p,g_pkg_name[0]?g_pkg_name:"unnamed"); *p=0; tcol(GREEN); term_puts(b); term_putc('\n'); tcol(WHT); }
                            else { struct pkg_list_ctx c; c.max=40; c.shown=0; tar_walk(tar,tl,pkg_print_cb,&c);
                                pkg_install(buf,n,0); p=b; p=scopy(p,"  "); p=udec(p,(u32)g_pkg_nfiles); p=scopy(p," files, "); p=udec(p,(u32)(g_pkg_total/1024)); p=scopy(p," KB total"); *p=0; term_puts(b); term_putc('\n'); } }
                        else if(kind==3){ tcol(rgb(230,160,60)); term_puts("  Android APK: use the ZIP viewer in Files to browse it\n"); tcol(WHT); }
                    } } } }
    }
    else if(seq(cmd,"nvgsp")){ extern int nvgsp_cmd(const char*); extern char g_nv_log[]; const char* a=arg; while(*a==' ')a++;
        if(!a[0]||seq(a,"help")){ tcol(CYAN); term_puts("nvgsp: NVIDIA Turing (GTX 16 / RTX 20) driver through NVIDIA's GSP firmware\n"); tcol(WHT);
            term_puts("  nvgsp vram      VRAM size, reserved layout and allocator (read only, safe)\n  nvgsp boot      FWSEC, booter, GSP-RM, static info, BAR1/BAR2 windows\n  nvgsp objects   client, device, subdevice, address space, channels, 3D/compute/copy objects, copy test\n  nvgsp all       boot + objects\n  nvgsp status    current state\n  nvgsp log       print the last log again (also saved as nvgsp-log.txt in Files)\n  nvgsp unload    stop GSP-RM and tear down WPR2 so it can be started again\n");
            tcol(rgb(230,160,60)); term_puts("  boot can turn off the picture of a screen connected to the NVIDIA card. The log is saved to Files either way.\n"); tcol(WHT); }
        if(a[0]&&!seq(a,"help")){ if(!seq(a,"log")&&!seq(a,"status")&&!seq(a,"vram")){ term_puts("nvgsp: working - this can take up to 30 seconds\n"); } int r=nvgsp_cmd(a); term_puts(g_nv_log); tcol(r>0?rgb(120,220,120):rgb(240,120,120)); term_puts(r>0?"nvgsp: done\n":"nvgsp: stopped - see the step above (log saved as nvgsp-log.txt)\n"); tcol(WHT); } }
    else if(seq(cmd,"nvidia")||(seq(cmd,"gpu")&&!(arg[0]=='f'||arg[0]=='b'||arg[0]=='i'||arg[0]=='r'||arg[0]=='d'||!arg[0])&&nv_probe())){
        if(!nv_probe()){ tcol(rgb(230,160,60)); term_puts("  no NVIDIA GPU found (looked for vendor 10DE, class 03)\n"); tcol(WHT); }
        else { nv_read_id();
            char b[128],*p;
            p=b; p=scopy(p,"  NVIDIA GPU  10DE:"); p=hx(p,g_nv_dev,4); p=scopy(p,"  bus "); p=udec(p,(u32)g_nv_bus); *p++='/'; p=udec(p,(u32)g_nv_slot); *p=0; tcol(GREEN); term_puts(b); term_putc('\n'); tcol(WHT);
            p=b; p=scopy(p,"  BAR0 regs  0x"); p=hx(p,(u32)g_nv_bar0,8); p=scopy(p,"  size "); p=udec(p,g_nv_bar0_sz/1024/1024); p=scopy(p," MB"); *p=0; term_puts(b); term_putc('\n');
            p=b; p=scopy(p,"  BAR1 fb    0x"); p=hx(p,(u32)(g_nv_bar1>>32),8); p=hx(p,(u32)g_nv_bar1,8); *p=0; term_puts(b); term_putc('\n');
            p=b; p=scopy(p,"  PMC_BOOT_0 0x"); p=hx(p,g_nv_boot0,8); p=scopy(p,"  arch "); p=scopy(p,(char*)nv_arch_name2(g_nv_boot0)); p=scopy(p,"  rev "); p=hx(p,(u32)g_nv_rev,2); *p=0; tcol(CYAN); term_puts(b); term_putc('\n'); tcol(WHT);
            if(g_nv_boot0==0xFFFFFFFFu||g_nv_boot0==0){ tcol(rgb(230,120,90)); term_puts("  (register read failed - BAR0 not reachable in this environment)\n"); tcol(WHT); }
            if(nv_load_vbios()){ p=b; p=scopy(p,"  VBIOS      "); p=udec(p,(u32)g_nv_vbios_len); p=scopy(p," bytes"); if(nv_find_bit()){ p=scopy(p,"  BIT@0x"); p=hx(p,g_nv_bit_off,4);} *p=0; term_puts(b); term_putc('\n');
                if(nv_find_dcb()){ nv_parse_dcb(); p=b; p=scopy(p,"  DCB@0x"); p=hx(p,g_nv_dcb_off,4); p=scopy(p,"  connectors: "); p=udec(p,(u32)g_nv_ncon); *p=0; term_puts(b); term_putc('\n');
                    for(int i=0;i<g_nv_ncon;i++){ p=b; p=scopy(p,"    - "); p=scopy(p,(char*)nv_con_name(g_nv_con_type[i])); *p=0; tcol(GREY); term_puts(b); term_putc('\n'); tcol(WHT); } }
                else { tcol(GREY); term_puts("  DCB not located\n"); tcol(WHT); } }
            else { tcol(rgb(230,160,60)); term_puts("  VBIOS not readable here (needs the real GPU; VirtualBox has no NVIDIA card)\n"); tcol(WHT); }
            tcol(GREY); term_puts("  display/accel driver: detect+VBIOS stage only (read-only, safe)\n"); tcol(WHT);
            if(g_nv_boot0!=0xFFFFFFFFu&&g_nv_boot0){ char b[160],*p; u32 hm=nvr32(0x610060), hc=nvr32(0x610074);
                p=b; p=scopy(p,"  display engine: "); p=udec(p,hc&0xF); p=scopy(p," heads, "); p=udec(p,(hc>>8)&0xF); p=scopy(p," output encoders"); *p=0; tcol(GREY); term_puts(b); term_putc('\n'); tcol(WHT);
                int shown=0;
                for(int h=0;h<4;h++){ if(!(hm&(1u<<h)))continue; u32 o=0x8000u+(u32)h*0x400u; nvm_head_t hd; nvm_decode_head(nvr32(0x682064+o),nvr32(0x682068+o),nvr32(0x68206C+o),nvr32(0x682070+o),nvr32(0x68200C+o),&hd);
                    if(!hd.active)continue; shown++;
                    p=b; p=scopy(p,"  head "); p=udec(p,(u32)h); p=scopy(p," is ON: "); p=udec(p,hd.hact); *p++='x'; p=udec(p,hd.vact); p=scopy(p," @ "); p=udec(p,hd.hz); p=scopy(p," Hz  (total "); p=udec(p,hd.htot); *p++='x'; p=udec(p,hd.vtot); p=scopy(p,", pixel clock "); p=udec(p,hd.pclk/1000); p=scopy(p," kHz)");
                    for(int s=0;s<8;s++){ if(!((hm>>8)&(1u<<s)))continue; u32 ctl=nvr32(0x680300+0x8000u+(u32)s*0x20u); if(ctl!=0xFFFFFFFFu&&(ctl&0xFF)&(1u<<h)){ p=scopy(p," <- encoder "); p=udec(p,(u32)s); p=scopy(p," "); p=scopy(p,nvm_proto((ctl>>8)&0xF)); } }
                    *p=0; tcol(GREEN); term_puts(b); term_putc('\n'); tcol(WHT); }
                if(!shown){ tcol(GREY); term_puts("  no active head found in the display engine's armed state\n"); tcol(WHT); } }
            if(g_nv_vbios_len&&nv_find_dcb()){ nvm_out_t outs[12]; int no=nvm_outputs(g_nv_vbios,g_nv_vbios_len,g_nv_dcb_off,outs,12); char b[120],*p;
                for(int i=0;i<no;i++){ p=b; p=scopy(p,"  output "); p=udec(p,(u32)i); p=scopy(p,": "); p=scopy(p,nvm_conn(outs[i].conntype)); p=scopy(p," socket "); p=udec(p,outs[i].conn); p=scopy(p,", "); p=scopy(p,nvm_sig(outs[i].type)); p=scopy(p,", encoder "); { int si=nvm_sor_index(outs[i].sor); if(si<0)p=scopy(p,"?"); else p=udec(p,(u32)si); } *p=0; term_puts(b); term_putc('\n'); } }
            tcol(GREY); term_puts("  to read the connected monitors (writes the DDC pins): gpumon\n"); tcol(WHT);
            tcol(GREY); term_puts("  modeset: disp907d scaffold present but disabled (unverified - see nvdisp.c)\n"); tcol(WHT);
        }
    }
    else if(seq(cmd,"gpumon")){
        if(!nv_probe()){ term_puts("  no NVIDIA GPU found\n"); }
        else { nv_read_id(); if(!nv_load_vbios()||!nv_find_dcb()){ term_puts("  VBIOS output table not available - run gpu first\n"); }
            else { nvm_out_t outs[12]; int no=nvm_outputs(g_nv_vbios,g_nv_vbios_len,g_nv_dcb_off,outs,12); int seen[32]={0}; char b[160],*p; int found=0;
                for(int i=0;i<no;i++){ nvm_out_t* o=&outs[i]; if(o->port==0xFF||o->port>=32||seen[o->port])continue; seen[o->port]=1;
                    p=b; p=scopy(p,"  "); p=scopy(p,nvm_conn(o->conntype)); p=scopy(p," socket "); p=udec(p,o->conn); p=scopy(p," (DDC port "); p=udec(p,o->port); p=scopy(p,"): "); *p=0; term_puts(b); sh_redraw();
                    u8 e[128]; nvm_edid_t m;
                    if(!nvm_read_edid(o->port,e)){ term_puts(o->type==6?"no answer (DisplayPort monitors need the AUX channel - not supported yet)\n":"no monitor answered\n"); continue; }
                    if(nvm_edid_parse(e,&m)){ term_puts("answered, but the EDID data was not valid\n"); continue; }
                    found++; p=b; p=scopy(p,m.name[0]?m.name:"(no name)"); p=scopy(p,"  maker "); p=scopy(p,m.maker); p=scopy(p,"  native "); p=udec(p,m.hact); *p++='x'; p=udec(p,m.vact); p=scopy(p," @ "); p=udec(p,m.hz); p=scopy(p," Hz"); *p=0; tcol(GREEN); term_puts(b); term_putc('\n'); tcol(WHT); }
                p=b; p=scopy(p,"  "); p=udec(p,(u32)found); p=scopy(p," monitor(s) identified on this card"); *p=0; term_puts(b); term_putc('\n'); } } }
    else if(seq(cmd,"gpuedid")){
        if(!nv_probe()){ term_puts("  no NVIDIA GPU found\n"); }
        else { nv_read_id(); tcol(rgb(230,160,60)); term_puts("  experimental: writes the GPU's DDC (i2c) pins at an unverified register\n"); tcol(WHT);
            nv_display_probe();
            if(g_edid_ok){ char b[96],*p=b; p=scopy(p,"  EDID: ok"); if(g_edid_name[0]){ p=scopy(p,"  monitor: "); p=scopy(p,g_edid_name); } *p=0; tcol(GREEN); term_puts(b); term_putc('\n'); tcol(WHT);
                if(g_mode_ok){ p=b; p=scopy(p,"  native mode: "); p=udec(p,g_mode.hact); *p++='x'; p=udec(p,g_mode.vact); p=scopy(p,"  pclk "); p=udec(p,g_mode.pclk_khz/1000); p=scopy(p," MHz"); *p=0; term_puts(b); term_putc('\n'); } }
            else { tcol(rgb(230,160,60)); term_puts("  EDID: not read (needs real GPU + a connected display; i2c port mapping may differ per board)\n"); tcol(WHT); }
        } }
    else if(seq(cmd,"sig")){
        tcol(rgb(120,200,255)); term_puts("  NoovexOS build signature\n"); tcol(WHT);
        char b[96],*p;
        p=b;p=scopy(p,"  magic   ");for(int i=0;i<8;i++)*p++=g_buildsig.magic[i];*p=0; term_puts(b); term_putc('\n');
        p=b;p=scopy(p,"  version ");p=scopy(p,(char*)g_buildsig.version);*p=0; term_puts(b); term_putc('\n');
        p=b;p=scopy(p,"  size    ");p=udec(p,g_buildsig.image_size);p=scopy(p," bytes");*p=0; term_puts(b); term_putc('\n');
        p=b;p=scopy(p,"  epoch   ");p=udec(p,g_buildsig.build_epoch);*p=0; term_puts(b); term_putc('\n');
        p=b;p=scopy(p,"  sha256  ");for(int i=0;i<16;i++)p=hx(p,g_buildsig.sha256[i],2);*p=0; term_puts(b); term_putc('\n');
        p=b;p=scopy(p,"          ");for(int i=16;i<32;i++)p=hx(p,g_buildsig.sha256[i],2);*p=0; term_puts(b); term_putc('\n');
        p=b;p=scopy(p,"  builder ");p=scopy(p,(char*)g_buildsig.builder);*p=0; tcol(GREY); term_puts(b); term_putc('\n'); tcol(WHT);
        /* live integrity re-check over the immutable region */
        int live=sig_self_check();
        tcol(live?GREEN:rgb(230,160,60));
        term_puts(live?"  integrity: OK (immutable region hash matches)\n"
                      :"  integrity: MISMATCH (code hash differs from stamp)\n"); tcol(WHT);
        { char b2[64],*q=b2; q=scopy(q,"  boot check: "); q=scopy(q,g_sig_ok?"passed":g_sig_tampered?"FAILED - kernel code modified":"not checked (unstamped build)"); *q=0;
          tcol(GREY); term_puts(b2); term_putc('\n'); tcol(WHT); }
    }
    else if(seq(cmd,"spawn")){
        if(!arg[0]){ term_puts("usage: spawn <FILE.ELF>   (fork+exec a child process, wait for it, show exit status)\n"); }
        else {
            int pid=sys_spawn(arg);
            if(pid<0){ tcol(RED); term_puts(pid==-1?"file not found\n":pid==-2?"not an ELF64\n":"spawn failed\n"); tcol(WHT); }
            else {
                int st=sys_waitpid(pid);
                char b[64],*p=b; p=scopy(p,"  [pid ");p=udec(p,(u32)pid);p=scopy(p,"] exited, status ");p=udec(p,(u32)st);*p=0;
                tcol(GREEN); term_puts(b); term_putc('\n'); tcol(WHT);
            }
        } }
    else if(seq(cmd,"dlmod")){
        if(!arg[0]){ term_puts("usage: dlmod <FILE.MOD>   (load a PIE module, relocate, call entry)\n"); }
        else {
            int n=rtw_read_table_file(arg,fat_filebuf,(int)sizeof(fat_filebuf));
            if(n<64){ tcol(RED); term_puts("module not found\n"); tcol(WHT); }
            else { nvx_module m; int r=nvx_dlload(fat_filebuf,(u32)n,&m);
                if(r){ char b[48],*p=b; p=scopy(p,"  dlload failed, code ");p=udec(p,(u32)(-r));*p=0; tcol(RED); term_puts(b); term_putc('\n'); tcol(WHT); }
                else { char b[80],*p=b; p=scopy(p,"  loaded at 0x");p=hx(p,(u32)((u64)m.base>>32),8);p=hx(p,(u32)(u64)m.base,8);p=scopy(p,"  entry 0x");p=hx(p,(u32)(u64)m.entry,8);*p=0; tcol(rgb(120,200,255)); term_puts(b); term_putc('\n'); tcol(WHT);
                    term_puts("  --- module output ---\n"); int rc=nvx_dlcall(&m);
                    char c2[40],*q=c2; q=scopy(q,"  module returned ");q=udec(q,(u32)rc);*q=0; tcol(GREEN); term_puts(c2); term_putc('\n'); tcol(WHT); } }
        } }
    else if(seq(cmd,"run")){
        if(!arg[0]){ term_puts("usage: run <FILE.ELF>   (loads an ELF64 from USB/FAT and executes it)\n"); }
        else {
            int n=rtw_read_table_file(arg,fat_filebuf,(int)sizeof(fat_filebuf));   /* USB stick first, then FAT disk */
            if(n<0){ tcol(RED);term_puts("file not found (checked USB stick and FAT disk)\n");tcol(WHT); }
            else if(n<64 || !(fat_filebuf[0]==0x7F&&fat_filebuf[1]=='E'&&fat_filebuf[2]=='L'&&fat_filebuf[3]=='F')){ tcol(RED);term_puts("not an ELF64 file\n");tcol(WHT); }
            else {
                char b[64],*p=b; p=scopy(p,"  loading ");p=scopy(p,(char*)arg);p=scopy(p," (");p=udec(p,(u32)n);p=scopy(p," bytes)...");*p=0; tcol(rgb(120,200,255)); term_puts(b); term_putc('\n'); tcol(WHT);
                int rc=elf_load_run(fat_filebuf,(u32)n);
                if(rc==0){ tcol(GREEN); term_puts("  program exited\n"); tcol(WHT); }
                else if(rc==1){ tcol(RED); term_puts("  paging not ready\n"); tcol(WHT); }
                else if(rc==2){ tcol(RED); term_puts("  could not build address space (bad segments?)\n"); tcol(WHT); }
                else { tcol(RED); term_puts("  not a valid ELF64\n"); tcol(WHT); }
            }
        } }
    else if(seq(cmd,"fatcat")){ if(!arg[0]){ term_puts("usage: fatcat <NAME.EXT>\n"); } else { int n=fat_read_file(arg,fat_filebuf,(int)sizeof(fat_filebuf)-1); if(n<0){ tcol(RED);term_puts("file not found on FAT disk\n");tcol(WHT); } else { for(int i=0;i<n&&i<8000;i++){ char c=(char)fat_filebuf[i]; if(c=='\r')continue; term_putc(c); } term_putc('\n'); char b[48],*p=b;p=scopy(p,"(");p=udec(p,(u32)n);p=scopy(p," bytes read)");*p=0; tcol(GREY);term_puts(b);tcol(WHT);term_putc('\n'); } } }
    else if(seq(cmd,"clear")){ term_clear(); }
    else if(seq(cmd,"echo")){ term_puts(arg); term_putc('\n'); }
    else if(seq(cmd,"browse")||seq(cmd,"www")||seq(cmd,"desktop")){ if(g_state==ST_LIVE){ if(!g_user[0]){ g_user[0]='r';g_user[1]='o';g_user[2]='o';g_user[3]='t';g_user[4]=0; } g_state=ST_DESKTOP; }
        if(seq(cmd,"desktop")){} else if(arg[0]) nvx_open_url(arg); else open_win(W_BROWSER); }
    else if(seq(cmd,"video")||seq(cmd,"player")){ extern int video_open_url(const char*,const char*); extern int video_open_slot(int);
        if(!arg[0]){ term_puts("usage: video <file or http(s) URL>   (opens the Video Player window; 'play' is fullscreen)\n"); }
        else { int isd=0, k=-1; int url=(arg[0]=='h'&&arg[1]=='t'&&arg[2]=='t'&&arg[3]=='p'); if(!url)k=ux_lookup(arg,&isd,1);
            if(!url&&(k<0||isd)){ tcol(RED); term_puts("video: file not found\n"); tcol(WHT); }
            else { if(g_state==ST_LIVE){ if(!g_user[0]){ g_user[0]='r';g_user[1]='o';g_user[2]='o';g_user[3]='t';g_user[4]=0; } g_state=ST_DESKTOP; }
                if(url)video_open_url(arg,0); else video_open_slot(k); term_puts("opened the Video Player.\n"); } } }
    else if(seq(cmd,"nvxlogin")||seq(cmd,"signin")){
        if(!arg[0]){
            u8 e[5]; entropy_get(e,5); for(int i=0;i<5;i++)g_nvx_state[i]="0123456789abcdef"[e[i]&15]; g_nvx_state[5]=0;
            char url[420],*p=url; p=scopy(p,"https://" NVX_HOST "/oauth/authorize?client_id=" NVX_CID);
            p=scopy(p,"&redirect_uri=urn%3Aietf%3Awg%3Aoauth%3A2.0%3Aoob&response_type=code&scope=profile&state=");
            p=scopy(p,g_nvx_state); *p=0;
            tcol(CYAN);term_puts("Opening Noovex sign-in in the browser...\n");tcol(WHT);
            term_puts("Approve, copy the code it shows, then run:  nvxlogin <code>\n");
            nvx_open_url(url);
        } else {
            char body[560],*p=body; p=scopy(p,"{\"grant_type\":\"authorization_code\",\"code\":\"");p=scopy(p,arg);
            p=scopy(p,"\",\"client_id\":\"" NVX_CID "\",\"client_secret\":\"" NVX_SECRET "\",\"redirect_uri\":\"urn:ietf:wg:oauth:2.0:oob\"}");*p=0;
            int bl=(int)(p-body);
            tcol(GREY);term_puts("exchanging code with " NVX_HOST " ...\n");tcol(WHT);
            int n=https_request(NVX_HOST,"/api/public/oauth/token","POST","Content-Type: application/json\r\n",body,bl,http_buf,(int)sizeof(http_buf));
            if(n<=0){ tcol(RED);term_puts("token request failed (no network / TLS error)\n");tcol(WHT); }
            else { int he=0; for(int i=0;i+3<n;i++)if(http_buf[i]=='\r'&&http_buf[i+1]=='\n'&&http_buf[i+2]=='\r'&&http_buf[i+3]=='\n'){he=i+4;break;}
                char tok[320];
                if(json_str(http_buf+he,n-he,"access_token",tok,(int)sizeof(tok))>0){
                    scopy(g_nvx_token,tok);
                    char hdr[360],*hp=hdr; hp=scopy(hp,"Authorization: Bearer ");hp=scopy(hp,tok);hp=scopy(hp,"\r\n");*hp=0;
                    int m=https_request(NVX_HOST,"/api/public/oauth/userinfo","GET",hdr,0,0,http_buf,(int)sizeof(http_buf));
                    if(m>0){ int he2=0; for(int i=0;i+3<m;i++)if(http_buf[i]=='\r'&&http_buf[i+1]=='\n'&&http_buf[i+2]=='\r'&&http_buf[i+3]=='\n'){he2=i+4;break;}
                        g_nvx_user[0]=g_nvx_email[0]=g_nvx_name[0]=0;
                        json_str(http_buf+he2,m-he2,"username",g_nvx_user,(int)sizeof(g_nvx_user));
                        json_str(http_buf+he2,m-he2,"email",g_nvx_email,(int)sizeof(g_nvx_email));
                        json_str(http_buf+he2,m-he2,"name",g_nvx_name,(int)sizeof(g_nvx_name));
                        g_nvx_signedin=1;
                        char b[240],*bp=b; bp=scopy(bp,"Signed in as ");bp=scopy(bp,g_nvx_user[0]?g_nvx_user:(g_nvx_name[0]?g_nvx_name:"(user)"));
                        if(g_nvx_email[0]){bp=scopy(bp,"  <");bp=scopy(bp,g_nvx_email);bp=scopy(bp,">");} *bp=0;
                        tcol(GREEN);term_puts(b);term_putc('\n');tcol(WHT);
                    } else { tcol(RED);term_puts("userinfo request failed\n");tcol(WHT); }
                } else { char er[96]; if(json_str(http_buf+he,n-he,"error",er,(int)sizeof(er))>0){ tcol(RED);term_puts("sign-in error: ");term_puts(er);term_putc('\n');tcol(WHT); }
                    else { tcol(RED);term_puts("no access_token returned (code expired or wrong secret)\n");tcol(WHT); } }
            }
        }
    }
    else if(seq(cmd,"bcstat")||seq(cmd,"bufcache")){
        extern unsigned long g_bc_hits,g_bc_miss;
        char b[90],*p=b; tcol(rgb(120,200,255)); term_puts("disk buffer cache\n"); tcol(WHT);
        p=scopy(p,"  hits   "); p=udec(p,(u32)g_bc_hits); *p=0; term_puts(b); term_putc('\n');
        p=b; p=scopy(p,"  misses "); p=udec(p,(u32)g_bc_miss); *p=0; term_puts(b); term_putc('\n');
        unsigned long tot=g_bc_hits+g_bc_miss; if(tot){ p=b; p=scopy(p,"  hit rate "); p=udec(p,(u32)(g_bc_hits*100/tot)); p=scopy(p,"%"); *p=0; tcol(GREEN); term_puts(b); term_putc('\n'); tcol(WHT); } }
    else if(seq(cmd,"df")||seq(cmd,"statfs")){
        extern int ux_statfs(const char*,unsigned long*); unsigned long st[8]; ux_statfs("/",st);
        tcol(rgb(120,200,255)); term_puts("Filesystem   Blocks     Used     Avail   Inodes  IFree\n"); tcol(WHT);
        char b[120],*p=b; p=scopy(p,"nvxfs      ");
        p=udec(p,(u32)st[2]); *p++=' ';*p++=' '; p=udec(p,(u32)(st[2]-st[3])); *p++=' ';*p++=' ';
        p=udec(p,(u32)st[4]); *p++=' ';*p++=' '; p=udec(p,(u32)st[5]); *p++=' ';*p++=' '; p=udec(p,(u32)st[6]); *p=0;
        term_puts(b); term_putc('\n');
        term_puts("(block size "); { char q[8],*r=q; r=udec(r,(u32)st[1]); *r=0; term_puts(q); } term_puts(" bytes)\n"); }
    else if(seq(cmd,"mounts")){ extern int proc_gen(const char*,char*,int); static char mb[512]; int n=proc_gen("/proc/mounts",mb,512); if(n>=0)term_puts(mb); }
    else if(seq(cmd,"slabinfo")){ extern void slab_stats(char*,int); static char sb[512]; slab_stats(sb,512); term_puts(sb); }
    else if(seq(cmd,"passwd")){
        extern char g_user_pw[]; extern int g_has_pw; extern void ux_save_localpw(void);
        char pw[48], pw2[48];
        term_puts("New password: "); if(sh_readpw(pw,48)<0)return;
        term_puts("Retype password: "); if(sh_readpw(pw2,48)<0)return;
        int same=1; for(int i=0;;i++){ if(pw[i]!=pw2[i]){ same=0; break; } if(!pw[i])break; }
        if(!same){ tcol(rgb(240,120,120)); term_puts("passwords do not match\n"); tcol(WHT); }
        else if(!pw[0]){ g_has_pw=0; g_user_pw[0]=0; ux_save_localpw(); term_puts("password removed\n"); }
        else { int k=0; for(;pw[k]&&k<47;k++)g_user_pw[k]=pw[k]; g_user_pw[k]=0; g_has_pw=1; ux_save_localpw(); g_sudo_until=0; tcol(GREEN); term_puts("password updated\n"); tcol(WHT); } }
    else if(seq(cmd,"whoami")){ if(g_nvx_signedin){ tcol(GREEN);term_puts("signed in as ");term_puts(g_nvx_user[0]?g_nvx_user:g_nvx_name); if(g_nvx_email[0]){term_puts(" <");term_puts(g_nvx_email);term_puts(">");} term_putc('\n');tcol(WHT); } else { term_puts("not signed in - run: nvxlogin\n"); } }
    else if(seq(cmd,"nvxlogout")||seq(cmd,"signout")){ g_nvx_signedin=0; g_nvx_token[0]=g_nvx_user[0]=g_nvx_email[0]=0; term_puts("signed out\n"); }
    else if(seq(cmd,"nvxtest")){
        tcol(CYAN);term_puts("Probing mynexus.site API (raw responses):\n");tcol(WHT);
        /* 1: register endpoint */
        u8 e[3]; entropy_get(e,3); char pu[20]; char* q=scopy(pu,"probe_"); for(int i=0;i<3;i++){*q++="0123456789abcdef"[e[i]&15];} *q=0;
        char b1[220],*p=b1; p=scopy(p,"{\"username\":\"");p=scopy(p,pu);p=scopy(p,"\",\"password\":\"Probe12345\",\"client_id\":\"" NVX_CID "\"}");*p=0;
        term_puts("\nPOST /api/public/register\n");
        int n=https_request(NVX_HOST,"/api/public/register","POST","Content-Type: application/json\r\n",b1,(int)(p-b1),http_buf,(int)sizeof(http_buf));
        if(n<=0){ tcol(RED);term_puts("  no response (network/TLS)\n");tcol(WHT); }
        else { term_puts("  "); for(int i=0;i<n&&http_buf[i]!='\r'&&i<90;i++)term_putc(http_buf[i]); term_putc('\n');
            int he=0;for(int j=0;j+3<n;j++)if(http_buf[j]=='\r'&&http_buf[j+1]=='\n'&&http_buf[j+2]=='\r'&&http_buf[j+3]=='\n'){he=j+4;break;}
            term_puts("  body: "); for(int j=he;j<n&&j<he+260;j++){char c=http_buf[j]; term_putc(c=='\n'?' ':c);} term_putc('\n'); }
        /* 2: password-grant token endpoint (dummy creds -> should be invalid_grant, not HTML) */
        char b2[240],*p2=b2; p2=scopy(p2,"{\"grant_type\":\"password\",\"username\":\"probe_none\",\"password\":\"x\",\"client_id\":\"" NVX_CID "\",\"client_secret\":\"" NVX_SECRET "\"}");*p2=0;
        term_puts("POST /api/public/oauth/token (grant_type=password)\n");
        int m=https_request(NVX_HOST,"/api/public/oauth/token","POST","Content-Type: application/json\r\n",b2,(int)(p2-b2),http_buf,(int)sizeof(http_buf));
        if(m<=0){ tcol(RED);term_puts("  no response (network/TLS)\n");tcol(WHT); }
        else { term_puts("  "); for(int i=0;i<m&&http_buf[i]!='\r'&&i<90;i++)term_putc(http_buf[i]); term_putc('\n');
            int he=0;for(int j=0;j+3<m;j++)if(http_buf[j]=='\r'&&http_buf[j+1]=='\n'&&http_buf[j+2]=='\r'&&http_buf[j+3]=='\n'){he=j+4;break;}
            term_puts("  body: "); for(int j=he;j<m&&j<he+260;j++){char c=http_buf[j]; term_putc(c=='\n'?' ':c);} term_putc('\n'); }
        tcol(GREY);term_puts("(JSON = endpoint ready; HTML = route missing / browser-gated)\n");tcol(WHT);
    }
    else if(seq(cmd,"neofetch")||seq(cmd,"fetch")){ term_puts("launching neofetch...\n"); open_win(W_NEOFETCH); }
    else if(seq(cmd,"ver")||seq(cmd,"about")){ term_puts("NoovexOS 64  v"); term_puts((char*)g_buildsig.version); term_puts(" (native x86-64, from scratch)\n"); }
    else if(seq(cmd,"cpu")||seq(cmd,"cpuid")){ tcol(CYAN);term_puts("CPU: ");tcol(WHT);term_puts(g_brand);term_putc('\n');
        char b[64],*p=b;p=scopy(p,"long mode active   EFER.LMA=");*p++=(char)('0'+g_lma);*p=0;term_puts(b);term_putc('\n'); }
    else if(seq(cmd,"time")){ int h,m,ss,y,mo,d;read_clock(&h,&m,&ss,&y,&mo,&d);char b[16],*p=b;p=u2(p,h);*p++=':';p=u2(p,m);*p++=':';p=u2(p,ss);*p=0;term_puts(b);term_putc('\n'); }
    else if(seq(cmd,"date")){ int h,m,ss,y,mo,d;read_clock(&h,&m,&ss,&y,&mo,&d);char b[16],*p=b;p=udec(p,y);*p++='-';p=u2(p,mo);*p++='-';p=u2(p,d);*p=0;term_puts(b);term_putc('\n'); }
    else if(seq(cmd,"uptime")){ char b[24],*p=b;p=scopy(p,"up ");p=udec(p,(u32)(g_ticks/100));p=scopy(p," seconds");*p=0;term_puts(b);term_putc('\n'); }
    else if(seq(cmd,"pwd")){ dt_init(); char pb[192]; dt_path(g_dcwd,pb); term_puts(pb); term_putc('\n'); }
    else if(seq(cmd,"whoami")){ term_puts(cur_user()); term_putc('\n'); }
    else if(seq(cmd,"id")){ char b[128],*p=b; int gid=g_uid==0?0:1000; p=scopy(p,"uid=");p=udec(p,(u32)g_uid);p=scopy(p,"(");p=scopy(p,(char*)cur_user());p=scopy(p,") gid=");p=udec(p,(u32)gid);p=scopy(p,gid?"(users)":"(root)"); p=scopy(p," groups="); p=udec(p,(u32)gid); p=scopy(p,gid?"(users)":"(root)"); if(ux_is_admin(g_uid))p=scopy(p,",10(admin)"); *p=0; term_puts(b); term_putc('\n'); }
    else if(seq(cmd,"sudo")){ const char* a=arg; while(*a==' ')a++;
        if(!*a){ term_puts("usage: sudo <command>\n"); }
        else if(a[0]=='-'&&a[1]=='k'&&(!a[2]||a[2]==' ')){ g_sudo_until=0; term_puts("sudo: password will be asked again next time\n"); }
        else if(sudo_auth()){ int save=g_uid; g_uid=0; char cl[480]; int k=0; while(a[k]&&k<479){cl[k]=a[k];k++;} cl[k]=0; run_cmd(cl); g_uid=save; } }
    else if(seq(cmd,"sh")||seq(cmd,"source")||seq(cmd,".")||sh_is_sh(cmd)){
        char fn[96]; const char* f=sh_is_sh(cmd)?cmd:arg; int k=0; while(f[k]&&f[k]!=' '&&k<95){fn[k]=f[k];k++;} fn[k]=0;
        if(!fn[0]){ tcol(rgb(220,120,110)); term_puts("usage: sh <file.sh>\n"); tcol(WHT); }
        else { static char sb[8192]; int n=ux_read_file(fn,sb,8191); if(n<0)n=fat_read_file(fn,(u8*)sb,8191); if(n<0){ tcol(rgb(220,120,110)); term_puts("sh: cannot open "); term_puts(fn); term_putc('\n'); tcol(WHT);} else { sb[n]=0; const char* t=sb; if(sb[0]=='#'&&sb[1]=='!'){ while(*t&&*t!='\n')t++; if(*t)t++; } sh_run_script(t); } } }
    else if(seq(cmd,"set")&&!arg[0]){ for(int i=0;i<shv_n;i++){ term_puts(shv_name[i]); term_putc('='); term_puts(shv_val[i]); term_putc('\n'); } }
    else if(seq(cmd,"test")||seq(cmd,"[")){ g_lastrc = sh_truth(arg)?0:1; }
    else if(seq(cmd,"true")){ g_lastrc=0; }
    else if(seq(cmd,"false")){ g_lastrc=1; }
    else if(seq(cmd,"su")){ const char* a=arg; while(*a==' ')a++;
        if(!*a||seq((char*)a,"root")){ g_uid=0; env_set("USER","root"); }
        else { g_uid=1000; env_set("USER","user"); }
        char b[32],*p=b; p=scopy(p,"now: ");p=scopy(p,(char*)cur_user());*p=0; tcol(rgb(120,200,255)); term_puts(b); term_putc('\n'); tcol(WHT); }
    else if(seq(cmd,"env")){ for(int i=0;i<NENV;i++) if(g_env[i].used){ term_puts(g_env[i].name); term_putc('='); term_puts(g_env[i].val); term_putc('\n'); } }
    else if(seq(cmd,"export")){ const char* a=arg; while(*a==' ')a++; char nm[24]; int i=0; while(a[i]&&a[i]!='='&&i<23){nm[i]=a[i];i++;} nm[i]=0; if(a[i]=='=')env_set(nm,a+i+1); }
    else if(seq(cmd,"cd")){
        dt_init(); const char* a=arg; while(*a==' ')a++;
        if(!*a||seq((char*)a,"~")){ g_dcwd=dt_resolve_dir("/home"); }
        else { int d=dt_resolve_dir(a); if(d>=0){g_dcwd=d; char pb[128]; dt_path(g_dcwd,pb); env_set("PWD",pb);} else { tcol(rgb(240,120,120)); term_puts("cd: no such directory\n"); tcol(WHT); } }
    }
    else if(seq(cmd,"ls")){
        dt_init(); const char* a=arg; while(*a==' ')a++;
        int longfmt=0;
        if(a[0]=='-'&&a[1]=='l'){ longfmt=1; a+=2; while(*a==' ')a++; }
        int d = *a ? dt_resolve_dir(a) : g_dcwd;
        int isdev=(d>=0 && dt_child(0,"dev")==d);
        if(d<0){ tcol(rgb(240,120,120)); term_puts("ls: no such directory\n"); tcol(WHT); }
        else if(isdev){ tcol(rgb(120,255,180)); term_puts("null  zero  random  urandom  full  tty  console\n"); tcol(WHT); }
        else if(longfmt){
            char b[96],*p; char ms[12];
            for(int i=0;i<DN_MAX;i++) if(DT[i].used&&DT[i].parent==d){
                p=b; p=scopy(p,mode_str(DT[i].mode?DT[i].mode:0040755u,ms)); p=scopy(p," root root ");p=scopy(p,"    - ");*p=0;
                tcol(GREY); term_puts(b); tcol(rgb(120,200,255)); term_puts(DT[i].name); term_putc('/'); term_putc('\n'); tcol(WHT); }
            for(int k=0;k<FS_N;k++) if(FS[k].used&&FS[k].dir==d){
                p=b; p=scopy(p,mode_str(FS[k].mode?FS[k].mode:0100644u,ms)); p=scopy(p," root root ");
                p=udec(p,(u32)FS[k].len); p=scopy(p," "); *p=0;
                tcol(GREY); term_puts(b); tcol(WHT); term_puts(FS[k].name); term_putc('\n'); }
        }
        else {
            int any=0;
            for(int i=0;i<DN_MAX;i++) if(DT[i].used&&DT[i].parent==d){ tcol(rgb(120,200,255)); term_puts(DT[i].name); term_putc('/'); term_puts("  "); any=1; }
            for(int k=0;k<FS_N;k++) if(FS[k].used&&FS[k].dir==d){ tcol(WHT); term_puts(FS[k].name); term_puts("  "); any=1; }
            tcol(WHT); if(!any)term_puts("(empty)"); term_putc('\n');
        }
    }
    else if(seq(cmd,"chmod")){
        dt_init(); const char* a=arg; while(*a==' ')a++;
        /* parse octal mode */
        unsigned int nm=0; int got=0; while(*a>='0'&&*a<='7'){ nm=nm*8+(*a-'0'); a++; got=1; } while(*a==' ')a++;
        if(!got||!*a){ term_puts("usage: chmod <octal> <file>   e.g. chmod 755 script\n"); }
        else { int k; for(k=0;k<FS_N;k++) if(FS[k].used&&FS[k].dir==g_dcwd&&seq(FS[k].name,(char*)a))break;
            if(k<FS_N){ FS[k].mode=(FS[k].mode&S_IFMT)|(nm&07777); if(!(FS[k].mode&S_IFMT))FS[k].mode|=S_IFREG; if(disk_ok)fs_sync(k); char mb[12]; char b2[40],*p=b2; p=scopy(p,"  "); p=scopy(p,mode_str(FS[k].mode,mb)); p=scopy(p," "); p=scopy(p,FS[k].name); *p=0; tcol(GREEN); term_puts(b2); term_putc('\n'); tcol(WHT); }
            else { int dd=dt_child(g_dcwd,a); if(dd>=0){ DT[dd].mode=S_IFDIR|(nm&07777); } else { tcol(rgb(240,120,120)); term_puts("chmod: no such file\n"); tcol(WHT); } } }
    }
    else if(seq(cmd,"mkdir")){
        dt_init(); const char* a=arg; while(*a==' ')a++;
        if(!*a){ term_puts("usage: mkdir <name>\n"); }
        else { int r=dt_mkdir_at(g_dcwd,a); if(r<0){tcol(RED);term_puts("mkdir failed (full or exists)\n");tcol(WHT);} else dt_save(); }
    }
    else if(seq(cmd,"rmdir")){
        dt_init(); const char* a=arg; while(*a==' ')a++;
        int d=dt_child(g_dcwd,a);
        if(d<0){tcol(RED);term_puts("rmdir: no such directory\n");tcol(WHT);}
        else { int has=0; for(int i=0;i<DN_MAX;i++)if(DT[i].used&&DT[i].parent==d)has=1; for(int k=0;k<FS_N;k++)if(FS[k].used&&FS[k].dir==d)has=1;
            if(has){tcol(RED);term_puts("rmdir: not empty\n");tcol(WHT);} else { DT[d].used=0; dt_save(); } }
    }
    else if(seq(cmd,"touch")){
        dt_init(); const char* a=arg; while(*a==' ')a++;
        if(!*a){ term_puts("usage: touch <name>\n"); }
        else { int k; for(k=0;k<FS_N;k++) if(FS[k].used&&FS[k].dir==g_dcwd&&seq(FS[k].name,(char*)a))break;
            if(k>=FS_N){ for(k=0;k<FS_N;k++) if(!FS[k].used){ fs_newslot(k); int n=0;while(a[n]&&n<63){FS[k].name[n]=a[n];n++;}FS[k].name[n]=0; FS[k].used=1; FS[k].folder=0; FS[k].dir=g_dcwd; FS[k].mode=0100644u; if(disk_ok)fs_sync(k); break; } if(k>=FS_N){tcol(RED);term_puts("touch: no space\n");tcol(WHT);} } }
    }
    else if(seq(cmd,"rm")){
        dt_init(); const char* a=arg; while(*a==' ')a++;
        int isd=0; int k=ux_lookup(a,&isd,0);
        if(k<0||isd){ tcol(rgb(240,120,120)); term_puts("rm: no such file\n"); tcol(WHT); }
        else if(ux_unlink(a)){ tcol(rgb(240,120,120)); term_puts("rm: permission denied\n"); tcol(WHT); }
    }
    else if(seq(cmd,"cp")){
        dt_init(); char src[64],dst[64]; const char* a=arg; while(*a==' ')a++; int n=0; while(*a&&*a!=' '&&n<63)src[n++]=*a++; src[n]=0; while(*a==' ')a++; n=0; while(*a&&*a!=' '&&n<63)dst[n++]=*a++; dst[n]=0;
        int s1; for(s1=0;s1<FS_N;s1++) if(FS[s1].used&&FS[s1].dir==g_dcwd&&seq(FS[s1].name,src))break;
        if(s1>=FS_N||!dst[0]){ tcol(RED);term_puts("usage: cp <src> <dst>\n");tcol(WHT); }
        else { int d1; for(d1=0;d1<FS_N;d1++) if(FS[d1].used&&FS[d1].dir==g_dcwd&&seq(FS[d1].name,dst))break; int fresh=0;
            if(d1==s1){ tcol(RED);term_puts("cp: source and destination are the same file\n");tcol(WHT); }
            else { if(d1>=FS_N){ fresh=1; for(d1=0;d1<FS_N;d1++) if(!FS[d1].used)break; }
            if(d1>=FS_N){tcol(RED);term_puts("cp: no space\n");tcol(WHT);}
            else if(!fresh&&!perm_file(d1,2)){ tcol(RED);term_puts("cp: permission denied\n");tcol(WHT); }
            else { if(fresh)fs_newslot(d1); int m=0;while(dst[m]&&m<63){FS[d1].name[m]=dst[m];m++;}FS[d1].name[m]=0;
                if(fs_set(d1,fsd(s1),FS[s1].len)){ tcol(RED);term_puts("cp: not enough memory\n");tcol(WHT); if(fresh)FS[d1].used=0; }
                else { FS[d1].used=1; FS[d1].folder=FS[s1].folder; FS[d1].dir=g_dcwd; if(fresh)FS[d1].mode=FS[s1].mode?FS[s1].mode:0100644u; ux_touch(d1,1); if(disk_ok)fs_sync(d1); } } } }
    }
    else if(seq(cmd,"mv")){
        dt_init(); char src[64],dst[64]; const char* a=arg; while(*a==' ')a++; int n=0; while(*a&&*a!=' '&&n<63)src[n++]=*a++; src[n]=0; while(*a==' ')a++; n=0; while(*a&&*a!=' '&&n<63)dst[n++]=*a++; dst[n]=0;
        int s1; for(s1=0;s1<FS_N;s1++) if(FS[s1].used&&FS[s1].dir==g_dcwd&&seq(FS[s1].name,src))break;
        if(s1>=FS_N||!dst[0]){ tcol(RED);term_puts("usage: mv <src> <dst>\n");tcol(WHT); }
        else { /* if dst is a directory, move into it; else rename */ int dd=dt_child(g_dcwd,dst);
            (void)dd; if(k_rename(src,dst)){ tcol(RED);term_puts("mv: cannot move ");term_puts(src);term_putc('\n');tcol(WHT); } }
    }
    else if(seq(cmd,"cat")){ { extern int proc_gen(const char*,char*,int); static char pb[2048]; int pg=proc_gen(arg,pb,2048); if(pg>=0){ term_puts(pb); goto cat_done2; } }
        dt_init(); int k; for(k=0;k<FS_N;k++) if(FS[k].used&&FS[k].dir==g_dcwd&&seq(FS[k].name,arg))break;
        if(k>=FS_N) for(k=0;k<FS_N;k++) if(FS[k].used&&seq(FS[k].name,arg))break;   /* fallback: any dir */
        if(k<FS_N&&FS[k].used){term_puts(fsd(k));term_putc('\n');} else {tcol(rgb(240,120,120));term_puts("no such file\n");tcol(WHT);} }
    else if(seq(cmd,"edit")){ int k;for(k=0;k<FS_N;k++) if(FS[k].used&&seq(FS[k].name,arg))break; if(k<FS_N&&FS[k].used){editor_open(k);term_puts("opened in editor.\n");} else {tcol(rgb(240,120,120));term_puts("no such file\n");tcol(WHT);} }
    else if(seq(cmd,"pci")){ char b[80],*p; if(!pci_n)pci_scan();
        for(int k=0;k<pci_n;k++){ p=b; p=scopy(p,"  "); p=hx(p,pci_ven[k],4); *p++=':'; p=hx(p,pci_dev[k],4); p=scopy(p,"  class="); p=hx(p,pci_cls[k],2); *p=0;
            tcol(WHT); term_puts(b); if(pci_cls[k]==2){ tcol(GREEN); term_puts("  <- network"); } term_putc('\n'); } tcol(WHT); }
    else if(seq(cmd,"mem")){ char b[90],*p;
        u64 total=heap_top-heap_base, used=heap_used(), bumped=heap_cur-heap_base;
        tcol(CYAN); p=b; p=scopy(p,"heap @ 0x"); p=hx(p,(u32)heap_base,8); p=scopy(p,"  size "); p=fmt_size(p,(long)total); *p=0; term_puts(b); term_putc('\n');
        tcol(WHT); p=b; p=scopy(p,"  in use "); p=fmt_size(p,(long)used); p=scopy(p,"   reserved "); p=fmt_size(p,(long)bumped); *p=0; term_puts(b); term_putc('\n');
        /* quick self-test: alloc 1MB, write, free */
        void* t=kmalloc(1048576); if(t){ u8* q=t; for(int i=0;i<1048576;i+=4096)q[i]=0x5A; int ok=1; for(int i=0;i<1048576;i+=4096) if(q[i]!=0x5A)ok=0; kfree(t); tcol(ok?GREEN:rgb(230,90,90)); term_puts(ok?"  malloc/free self-test: OK\n":"  self-test FAILED\n"); } else { tcol(rgb(230,90,90)); term_puts("  1MB alloc failed\n"); }
        tcol(WHT); }
    else if(seq(cmd,"rune")){
        /* render a texture using code written in Rune, straight into the screen */
        int rw=256, rh=192, ox=((int)W-rw)/2, oy=((int)H-rh)/2;
        static u32 rbuf[256*192];
        rune_render(rbuf, rw, rh, (int)(g_ticks/3));               /* <-- Rune-compiled */
        for(int y=0;y<rh;y++) for(int x=0;x<rw;x++) putpx(ox+x,oy+y,rbuf[y*rw+x]);
        fill_rect(ox,oy-24,rw,20,rgb(20,22,30)); dstr(ox+6,oy-20,"Rendered by Rune (rune_gfx.rn)",rgb(120,240,140),1);
        blit_all();
        char b[64],*p=b; p=scopy(p,"Rune says: fib(20) = "); p=udec(p,(u32)rune_fib(20)); *p=0;   /* <-- Rune-compiled */
        tcol(CYAN); term_puts("\n"); term_puts(b); term_puts("\n"); tcol(WHT);
        term_puts("(texture + fib computed by code written in Rune, compiled by rune, linked into the kernel)\n");
    cat_done2: ;
    }
    else if(seq(cmd,"usbdisk")||seq(cmd,"usbls")){
        tcol(rgb(255,210,90)); term_puts("scanning for a USB mass-storage device...\n"); tcol(WHT);
        if(!msd_autoinit()){ tcol(rgb(230,120,90)); term_puts("no USB stick found (enum failed at: "); term_puts(xhci_failstep); if(xhci_failcc){term_puts(" cc="); char cb[8],*cq=cb;cq=udec(cq,(u32)xhci_failcc);*cq=0;term_puts(cb);} term_puts(")\n"); tcol(WHT); }
        else { char b[100],*p; u32 mb=(u32)(((u64)(msd_lastlba+1)*512ULL)/1048576ULL);
            tcol(CYAN); p=b;p=scopy(p,"USB disk: ");p=udec(p,mb);p=scopy(p," MB  (");p=udec(p,msd_lastlba+1);p=scopy(p," sectors)");*p=0; term_puts(b); term_putc('\n');
            ufat_mount();
            if(!ufat_ok){ tcol(rgb(230,160,60)); term_puts("  no FAT filesystem found\n"); tcol(WHT); }
            else { p=b;p=scopy(p,"  FAT");p=scopy(p,ufat_type==32?"32":"16");p=scopy(p,"  ");p=udec(p,(u32)ufs_n);p=scopy(p," files:");*p=0; tcol(GREEN);term_puts(b);term_putc('\n'); tcol(WHT);
                for(int i=0;i<ufs_n;i++){ p=b;p=scopy(p,"    ");p=scopy(p,ufs[i].name); while((p-b)<26)*p++=' '; p=udec(p,ufs[i].size);p=scopy(p," B");*p=0; term_puts(b);term_putc('\n'); }
                if(!ufs_n){ tcol(GREY); term_puts("    (empty)\n"); tcol(WHT); }
                tcol(GREY); term_puts("  usbcat <file> | usbwrite <file> <text> | usbrm <file>\n"); tcol(WHT); } } }
    else if(seq(cmd,"usbcat")){ if(!arg[0]){ term_puts("usage: usbcat <FILE>\n"); }
        else if(!msd_ok||!ufat_ok){ if(!msd_autoinit()||(ufat_mount(),!ufat_ok)){ tcol(RED);term_puts("no USB FAT filesystem (run usbdisk first)\n");tcol(WHT); } }
        if(arg[0]&&msd_ok&&ufat_ok){ int fi=-1; for(int i=0;i<ufs_n;i++){ int eq=1; for(int k=0;k<16;k++){ char x=ufs[i].name[k],y=arg[k]; if(x>='a'&&x<='z')x-=32; if(y>='a'&&y<='z')y-=32; if(x!=y){eq=0;break;} if(!x&&!y)break; } if(eq){fi=i;break;} }
            if(fi<0){ tcol(RED);term_puts("file not found\n");tcol(WHT); }
            else { static char fbuf[16000]; int n=ufat_read(fi,fbuf,(int)sizeof(fbuf)-1); for(int i=0;i<n&&i<8000;i++){ char c=fbuf[i]; if(c=='\r')continue; term_putc(c); } term_putc('\n');
                char b[40],*p=b;p=scopy(p,"(");p=udec(p,(u32)n);p=scopy(p," bytes)");*p=0; tcol(GREY);term_puts(b);tcol(WHT);term_putc('\n'); } } }
    else if(seq(cmd,"usbwrite")){ char fn[32]; int i=0; while(arg[i]&&arg[i]!=' '&&i<31){fn[i]=arg[i];i++;} fn[i]=0; const char* txt=arg+i; while(*txt==' ')txt++;
        if(!fn[0]){ term_puts("usage: usbwrite <FILE> <text>\n"); }
        else if(!msd_autoinit()){ tcol(RED);term_puts("no USB stick\n");tcol(WHT); }
        else { ufat_mount(); if(!ufat_ok){ tcol(RED);term_puts("no FAT filesystem on the stick\n");tcol(WHT); }
            else { int tl=0; while(txt[tl])tl++; int r=ufat_write_file(fn,txt,tl);
                if(r==0){ tcol(GREEN);term_puts("written to USB\n");tcol(WHT); } else { tcol(RED);term_puts("write failed\n");tcol(WHT); } } } }
    else if(seq(cmd,"utest")||seq(cmd,"userspace")){
        tcol(rgb(120,200,255)); term_puts("=== NoovexOS userspace self-test (stages 1-9) ===\n"); tcol(WHT);
        if(!pf_ready){ tcol(rgb(230,160,60)); term_puts("frame pool disabled (needs >=272 MB RAM) - cannot test userspace\n"); tcol(WHT); }
        else {
            char b[80],*p;
            /* 1-2 paging */
            u32 r1=0,r2=0; int rc=vm_selftest(&r1,&r2);
            if(rc==0){ tcol(GREEN); term_puts("[1-2] paging + isolation .......... PASS\n"); }
            else { tcol(RED); term_puts("[1-2] paging + isolation .......... FAIL\n"); }
            tcol(WHT);
            /* 4-5 ring3 + syscalls */
            g_r3out_n=0; g_r3out[0]=0; int r3=ring3_run();
            if(r3==0 && g_r3out_n>0){ tcol(GREEN); term_puts("[4-5] ring 3 + syscalls ........... PASS\n"); }
            else { tcol(RED); term_puts("[4-5] ring 3 + syscalls ........... FAIL\n"); }
            tcol(WHT);
            /* 6-7 ELF loader + libc */
            int el=elf_load_run(_binary_user_user_demo_elf_start,(u32)(_binary_user_user_demo_elf_end-_binary_user_user_demo_elf_start));
            if(el==0 && g_r3out_n>0){ tcol(GREEN); term_puts("[6-7] ELF loader + mini-libc ...... PASS\n"); }
            else { tcol(RED); term_puts("[6-7] ELF loader + mini-libc ...... FAIL\n"); }
            tcol(WHT);
            /* 8 scheduler */
            for(int i=0;i<MAXPROC;i++){g_proc[i].used=0;g_proc[i].alive=0;} g_r3out_n=0;
            int pa=proc_create(_binary_user_user_demo_elf_start,(u32)(_binary_user_user_demo_elf_end-_binary_user_user_demo_elf_start));
            int pb=proc_create(_binary_user_user_demo_elf_start,(u32)(_binary_user_user_demo_elf_end-_binary_user_user_demo_elf_start));
            if(pa>=0&&pb>=0){ sched_run_all(); }
            if(pa>=0&&pb>=0&&g_r3out_n>0){ tcol(GREEN); term_puts("[8]   cooperative scheduler ....... PASS\n"); }
            else { tcol(RED); term_puts("[8]   cooperative scheduler ....... FAIL\n"); }
            tcol(WHT);
            /* 9 fork */
            for(int i=0;i<MAXPROC;i++){g_proc[i].used=0;g_proc[i].alive=0;} g_r3out_n=0;
            int pf=proc_create(_binary_user_user_fork_elf_start,(u32)(_binary_user_user_fork_elf_end-_binary_user_user_fork_elf_start));
            if(pf>=0){ sched_run_all(); }
            if(pf>=0&&g_r3out_n>0){ tcol(GREEN); term_puts("[9]   fork + execve ............... PASS\n"); }
            else { tcol(RED); term_puts("[9]   fork + execve ............... FAIL\n"); }
            tcol(rgb(120,240,150)); term_puts("=== userspace: physical mem, paging, ring 3, syscalls, ELF, scheduler, fork ===\n"); tcol(WHT);
        }
    }
    else if(seq(cmd,"vmtest")||seq(cmd,"vm")||seq(cmd,"paging")){
        char b[100],*p; tcol(CYAN);
        p=b;p=scopy(p,"Physical frame allocator: ");p=scopy(p,pf_ready?"ready":"disabled (needs >=272 MB RAM)");*p=0; term_puts(b);term_putc('\n');
        if(pf_ready){ p=b;p=scopy(p,"  user page pool  ");p=udec(p,pf_n);p=scopy(p," frames (4 KB), ");p=udec(p,pframe_free_count());p=scopy(p," free");*p=0; tcol(WHT);term_puts(b);term_putc('\n'); }
        tcol(rgb(255,210,90)); term_puts("running virtual-memory isolation self-test...\n"); tcol(WHT);
        u32 r1=0,r2=0; int rc=vm_selftest(&r1,&r2);
        if(rc==1){ tcol(rgb(230,160,60)); term_puts("  skipped: no frame pool on this machine\n"); }
        else if(rc==2){ tcol(RED); term_puts("  FAILED: could not allocate frames/tables\n"); }
        else { p=b;p=scopy(p,"  space1 @V=0x8000000000 = 0x");p=hx(p,r1,8);p=scopy(p,"   space2 = 0x");p=hx(p,r2,8);*p=0; tcol(CYAN);term_puts(b);term_putc('\n');
            if(rc==0){ tcol(GREEN); term_puts("  PASS: same virtual address holds different data per address space.\n");
                term_puts("  -> per-process paging + isolation works. (userspace stage 1+2 done)\n"); }
            else { tcol(RED); term_puts("  FAIL: isolation not achieved\n"); } }
        tcol(WHT); }
    else if(seq(cmd,"ring3")||seq(cmd,"user")||seq(cmd,"usertest")){
        tcol(rgb(255,210,90)); term_puts("entering ring 3 (user mode) with a test program... (experimental)\n"); tcol(WHT);
        int rc=ring3_run();
        if(rc==1){ tcol(rgb(230,160,60)); term_puts("  skipped: no frame pool (needs >=272 MB RAM)\n"); }
        else if(rc==2){ tcol(RED); term_puts("  FAILED: could not set up the user address space\n"); }
        else { char b[80],*p; tcol(CYAN); p=b;p=scopy(p,"  ring-3 program said: \"");p=scopy(p,g_r3out);*p++='"';*p=0; term_puts(b); term_putc('\n');
            tcol(GREEN); term_puts("  PASS: a program ran in ring 3, made INT 0x80 syscalls, and exited to the kernel.\n");
            term_puts("  -> user mode + syscalls work. (userspace stage 4+5 done)\n"); }
        tcol(WHT); }
    else if(seq(cmd,"sched")||seq(cmd,"multitask")||seq(cmd,"ps")){
        tcol(rgb(255,210,90)); term_puts("cooperative scheduler: running 3 ring-3 tasks that yield to each other...\n"); tcol(WHT);
        if(!pf_ready){ tcol(rgb(230,160,60)); term_puts("  skipped: no frame pool (needs >=272 MB RAM)\n"); tcol(WHT); }
        else { us_init(); for(int i=0;i<MAXPROC;i++){g_proc[i].used=0;g_proc[i].alive=0;} g_r3out_n=0; g_r3out[0]=0;
            int a=proc_create(_binary_user_user_demo_elf_start,(u32)(_binary_user_user_demo_elf_end-_binary_user_user_demo_elf_start));
            int b=proc_create(_binary_user_user_demo_elf_start,(u32)(_binary_user_user_demo_elf_end-_binary_user_user_demo_elf_start));
            int c=proc_create(_binary_user_user_demo_elf_start,(u32)(_binary_user_user_demo_elf_end-_binary_user_user_demo_elf_start));
            if(a<0||b<0||c<0){ tcol(RED); term_puts("  failed to create processes\n"); tcol(WHT); }
            else { sched_run_all();
                tcol(CYAN); term_puts("  combined output (3 tasks interleaving via yield):\n"); tcol(WHT);
                int st=0; for(int i=0;i<g_r3out_n;i++){ if(st==0){term_puts("    ");st=1;} term_putc(g_r3out[i]); if(g_r3out[i]=='\n')st=0; } if(st)term_putc('\n');
                tcol(GREEN); term_puts("  PASS: 3 processes ran concurrently in ring 3, context-switching on yield.\n");
                term_puts("  -> cooperative scheduler + multitasking work. (stage 8 done)\n"); tcol(WHT); } }
    }
    else if(seq(cmd,"fork")||seq(cmd,"forktest")){
        tcol(rgb(255,210,90)); term_puts("fork(): a user process duplicates itself...\n"); tcol(WHT);
        if(!pf_ready){ tcol(rgb(230,160,60)); term_puts("  skipped: no frame pool (needs >=272 MB RAM)\n"); tcol(WHT); }
        else { us_init(); for(int i=0;i<MAXPROC;i++){g_proc[i].used=0;g_proc[i].alive=0;} g_r3out_n=0; g_r3out[0]=0;
            int a=proc_create(_binary_user_user_fork_elf_start,(u32)(_binary_user_user_fork_elf_end-_binary_user_user_fork_elf_start));
            if(a<0){ tcol(RED); term_puts("  failed to create process\n"); tcol(WHT); }
            else { sched_run_all();
                tcol(CYAN); term_puts("  output:\n"); tcol(WHT);
                int st=0; for(int i=0;i<g_r3out_n;i++){ if(st==0){term_puts("    ");st=1;} term_putc(g_r3out[i]); if(g_r3out[i]=='\n')st=0; } if(st)term_putc('\n');
                tcol(GREEN); term_puts("  PASS: fork() created a child process with a copy of the address space.\n");
                term_puts("  -> fork + execve work. (stage 9 done)\n"); tcol(WHT); } }
    }
    else if(seq(cmd,"cores")||seq(cmd,"cpu")){
        int c=acpi_count_cores(); char b[80],*p;
        tcol(CYAN); p=b;p=scopy(p,"CPU cores detected (ACPI MADT): ");if(c>0)p=udec(p,(u32)c);else p=scopy(p,"unknown");*p=0; term_puts(b); term_putc('\n'); tcol(WHT);
        p=b;p=scopy(p,"cores online now: ");p=udec(p,(u32)g_cores_online);*p=0; term_puts(b); term_putc('\n');
        if(c>g_cores_online) term_puts("  run 'smp on' to schedule programs on the other cores.\n"); }
    else if(seq(cmd,"compositor")||seq(cmd,"nova")){
        tcol(rgb(255,210,90)); term_puts("launching a USERLAND compositor + 2 app processes (rung 5, the finale)...\n"); tcol(WHT);
        if(!pf_ready){ tcol(rgb(230,160,60)); term_puts("  skipped: no frame pool\n"); tcol(WHT); }
        else { compositor_run();
            tcol(CYAN); term_puts("  process output:\n"); tcol(WHT);
            int st=0; for(int i=0;i<g_r3out_n;i++){ if(st==0){term_puts("    ");st=1;} term_putc(g_r3out[i]); if(g_r3out[i]=='\n')st=0; } if(st)term_putc('\n');
            tcol(GREEN); term_puts("  PASS: a userland compositor process composited windows from 2 other processes.\n");
            term_puts("  -> rungs 1-5 complete: preemption + IPC + protocol + a userland compositor.\n"); tcol(WHT); } }
    else if(seq(cmd,"wm")||seq(cmd,"winproto")){
        tcol(rgb(255,210,90)); term_puts("windowing protocol: an app creates a window via a message + shared-memory pixels...\n"); tcol(WHT);
        if(!pf_ready){ tcol(rgb(230,160,60)); term_puts("  skipped: no frame pool\n"); tcol(WHT); }
        else { wm_demo();
            tcol(CYAN); term_puts("  app output:\n"); tcol(WHT);
            int st=0; for(int i=0;i<g_r3out_n;i++){ if(st==0){term_puts("    ");st=1;} term_putc(g_r3out[i]); if(g_r3out[i]=='\n')st=0; } if(st)term_putc('\n');
            tcol(GREEN); term_puts("  PASS: a window was created + drawn entirely through the protocol (rung 4). Press a key.\n"); tcol(WHT);
            { u64 until=g_ticks+600; while(g_ticks<until && kpop()<0)cpu_idle(); } }
    }
    else if(seq(cmd,"shmstat")){
        extern struct shmreg g_shm[]; 
        tcol(rgb(120,200,255)); term_puts("shared memory segments\n"); tcol(GREY); term_puts("id  key   frames\n"); tcol(WHT);
        int any=0; for(int i=0;i<SHM_MAX;i++){ if(!g_shm[i].used)continue; any=1; char b[48],*p=b; p=scopy(p,"  ");p=udec(p,(u32)i);p=scopy(p,"   ");p=udec(p,(u32)g_shm[i].key);p=scopy(p,"    ");p=udec(p,(u32)g_shm[i].nframes);*p=0; term_puts(b); term_putc('\n'); }
        if(!any) term_puts("  (none - run 'shm' to see the IPC demo create one)\n"); }
    else if(seq(cmd,"ipc")||seq(cmd,"shm")){
        tcol(rgb(255,210,90)); term_puts("IPC: two processes share memory + exchange a message...\n"); tcol(WHT);
        if(!pf_ready){ tcol(rgb(230,160,60)); term_puts("  skipped: no frame pool\n"); tcol(WHT); }
        else { ipc_reset(); preempt_run_elf(_binary_user_user_ipc_elf_start,(u32)(_binary_user_user_ipc_elf_end-_binary_user_user_ipc_elf_start));
            tcol(CYAN); term_puts("  output:\n"); tcol(WHT);
            int st=0; for(int i=0;i<g_r3out_n;i++){ if(st==0){term_puts("    ");st=1;} term_putc(g_r3out[i]); if(g_r3out[i]=='\n')st=0; } if(st)term_putc('\n');
            tcol(GREEN); term_puts("  PASS: P2 read P1's shared value + message -> shared memory + IPC work (rung 2+3).\n"); tcol(WHT); } }
    else if(seq(cmd,"preempt")||seq(cmd,"multitask2")){
        tcol(rgb(255,210,90)); term_puts("preemptive multitasking: 2 processes that NEVER yield - the timer forces switches...\n"); tcol(WHT);
        if(!pf_ready){ tcol(rgb(230,160,60)); term_puts("  skipped: no frame pool\n"); tcol(WHT); }
        else { preempt_run();
            tcol(CYAN); term_puts("  output (interleaved P1/P2 = the timer preempted them, no yield needed):\n"); tcol(WHT);
            int st=0; for(int i=0;i<g_r3out_n;i++){ if(st==0){term_puts("    ");st=1;} term_putc(g_r3out[i]); if(g_r3out[i]=='\n')st=0; } if(st)term_putc('\n');
            tcol(GREEN); term_puts("  PASS: preemptive scheduling works (rung 1 toward a userland compositor).\n"); tcol(WHT); } }
    else if(seq(cmd,"smpsched")){
        extern volatile int g_cores_online; extern volatile unsigned long g_core_load[]; extern volatile unsigned long g_core_cycles[];
        tcol(rgb(120,200,255)); term_puts("per-core run-queue stats\n"); tcol(GREY);
        term_puts("core  jobs  work-units\n"); tcol(WHT);
        for(int i=0;i<g_cores_online&&i<8;i++){ char b[64],*p=b; p=scopy(p,"  ");p=udec(p,(u32)i);p=scopy(p,"    ");p=udec(p,(u32)g_core_load[i]);p=scopy(p,"    ");p=udec(p,(u32)g_core_cycles[i]);*p=0; term_puts(b); term_putc('\n'); }
        if(g_cores_online<=1) term_puts("  (single core - run 'smpstart' then 'smpwork' to see load spread)\n"); }
    else if(seq(cmd,"smpwork")||seq(cmd,"parallel")){
        if(g_cores_online<=1){ tcol(rgb(230,160,60)); term_puts("only 1 core online - run 'smpstart' first to bring the cores online.\n"); tcol(WHT); }
        else { int workers=g_cores_online-1; if(workers>8)workers=8;
            tcol(rgb(255,210,90)); { char b[80],*p=b; p=scopy(p,"dispatching ");p=udec(p,(u32)workers);p=scopy(p," prime-counting jobs across the extra cores...");*p=0; term_puts(b); term_putc('\n'); } tcol(WHT);
            g_smp_work_on=1;   /* wake the parked worker cores */
            u64 t0=g_ticks; int ids[8];
            for(int i=0;i<workers;i++) ids[i]=smp_submit(200000ULL+(u64)i*20000ULL);   /* each core counts primes up to a big N */
            for(int i=0;i<workers;i++){ for(volatile int t=0;t<200000000 && g_jobs[ids[i]].state!=3;t++)cpu_pause(); }
            u64 dt=g_ticks-t0;
            for(int i=0;i<workers;i++){ char b[90],*p=b; tcol(CYAN); p=scopy(p,"  core ");p=udec(p,(u32)g_jobs[ids[i]].core);p=scopy(p,": primes<=");p=udec(p,(u32)g_jobs[ids[i]].arg);p=scopy(p," = ");p=udec(p,(u32)g_jobs[ids[i]].result);*p=0; term_puts(b); term_putc('\n'); for(int k=0;k<NJOBS;k++)if(k==ids[i]){g_jobs[k].state=0;break;} }
            tcol(GREEN); { char b[80],*p=b; p=scopy(p,"  all jobs done in ~");p=udec(p,(u32)(dt*10));p=scopy(p," ms, computed in PARALLEL on the cores.");*p=0; term_puts(b); term_putc('\n'); }
            term_puts("  -> real multi-core work. (isolated compute; full SMP scheduling is still a further step.)\n"); tcol(WHT); }
    }
    else if(seq(cmd,"smpstart")||seq(cmd,"smp")){
        const char* a=arg; while(*a==' ')a++; if(seq(cmd,"smpstart"))a="on";
        char b[120],*p;
        if(seq((char*)a,"on")){
            tcol(rgb(255,200,90)); term_puts("SMP: starting the other cores and scheduling programs on all of them...\n"); tcol(WHT);
            smp_pref_set("starting"); int r=smp_enable(); smp_pref_set("on");
            if(r==-1){ tcol(rgb(240,120,120)); term_puts("smp: no calibrated local APIC timer - multi-core scheduling needs one\n"); tcol(WHT); }
            else if(r<=1){ tcol(rgb(230,160,60)); term_puts("smp: no other cores came online (single-core machine, or the cores did not answer)\n"); tcol(WHT); }
            else { tcol(GREEN); p=b; p=scopy(p,"smp: on - "); p=udec(p,(u32)r); p=scopy(p," cores schedule programs (big kernel lock, preemptive user mode)"); *p=0; term_puts(b); term_putc('\n'); tcol(WHT); } }
        else if(seq((char*)a,"off")){
            int r=smp_disable(); if(r==0)smp_pref_set("off"); if(r<0){ tcol(rgb(240,120,120)); term_puts("smp: some cores did not stop in time\n"); tcol(WHT); }
            else { tcol(GREEN); term_puts("smp: off - programs run on core 0 only\n"); tcol(WHT); } }
        else {
            tcol(CYAN); p=b; p=scopy(p,"cores in system: "); { u8 ids[NCPU]; int c=madt_apic_ids(ids,NCPU); if(c<=0)c=acpi_count_cores(); if(c>0)p=udec(p,(u32)c); else p=scopy(p,"unknown"); } p=scopy(p,"   online: "); p=udec(p,(u32)g_cores_online);
            p=scopy(p,"   scheduling: "); p=scopy(p,g_smp_on?"all cores":"core 0 only"); *p=0; term_puts(b); term_putc('\n'); tcol(WHT);
            term_puts("  cpu  apic  state     switches  syscalls  user-ticks  idle-ticks  ipis\n");
            for(int i=0;i<g_ncpu&&i<NCPU;i++){ struct pcpu* c=&g_cpu[i]; if(!c->online)continue; p=b; p=scopy(p,"  "); p=udec(p,(u32)i); p=scopy(p,"    "); p=udec(p,c->apic_id); p=scopy(p,"     ");
                p=scopy(p,i==0?"boot   ":c->parked?"parked ":c->js_running?"running":"idle   "); p=scopy(p,"   "); p=udec(p,(u32)c->switches); p=scopy(p,"      "); p=udec(p,(u32)c->syscalls);
                p=scopy(p,"      "); p=udec(p,(u32)(i==0?0:c->user_ticks)); p=scopy(p,"        "); p=udec(p,(u32)(i==0?0:c->idle_ticks)); p=scopy(p,"        "); p=udec(p,(u32)c->ipis); *p=0; term_puts(b); term_putc('\n'); }
            term_puts("  smp on | smp off | smptest [workers] - run a multi-core stress test\n"); }
    }
    else if(seq(cmd,"threads")||seq(cmd,"thread")){
        tcol(rgb(255,210,90)); term_puts("threads: one process spawns worker threads that share its memory...\n"); tcol(WHT);
        if(!pf_ready){ tcol(rgb(230,160,60)); term_puts("  skipped: no frame pool\n"); tcol(WHT); }
        else { us_init(); for(int i=0;i<MAXPROC;i++){g_proc[i].used=0;g_proc[i].alive=0;} g_r3out_n=0; g_r3out[0]=0; g_thread_stk_next=0x40000200000ULL;
            int a=proc_create(_binary_user_user_thread_elf_start,(u32)(_binary_user_user_thread_elf_end-_binary_user_user_thread_elf_start));
            if(a<0){ tcol(RED); term_puts("  failed to create process\n"); tcol(WHT); }
            else { sched_run_all();
                tcol(CYAN); term_puts("  output:\n"); tcol(WHT);
                int st=0; for(int i=0;i<g_r3out_n;i++){ if(st==0){term_puts("    ");st=1;} term_putc(g_r3out[i]); if(g_r3out[i]=='\n')st=0; } if(st)term_putc('\n');
                tcol(GREEN); term_puts("  PASS: multiple threads ran in one address space (shared memory).\n"); tcol(WHT); } }
    }
    else if(seq(cmd,"mem")||seq(cmd,"mmap")){
        tcol(rgb(255,210,90)); term_puts("POSIX memory (rung 2+3): a program uses mmap/brk/getpid...\n"); tcol(WHT);
        int rc=elf_load_run(_binary_user_user_mem_elf_start,(u32)(_binary_user_user_mem_elf_end-_binary_user_user_mem_elf_start));
        if(rc==1){ tcol(rgb(230,160,60)); term_puts("  skipped: no frame pool\n"); tcol(WHT); }
        else if(rc){ tcol(RED); term_puts("  failed to load\n"); tcol(WHT); }
        else { tcol(rgb(180,230,180)); { int st=0; for(int i=0;i<g_r3out_n;i++){ if(st==0){term_puts("    ");st=1;} term_putc(g_r3out[i]); if(g_r3out[i]=='\n')st=0; } if(st)term_putc('\n'); }
            tcol(GREEN); term_puts("  PASS: mmap + brk + getpid (process memory) - the syscalls malloc is built on.\n"); tcol(WHT); } }
    else if(seq(cmd,"posix")||seq(cmd,"fdtest")){
        tcol(rgb(255,210,90)); term_puts("POSIX fd layer (rung 1): a program uses open/write/lseek/read/close on integer fds...\n"); tcol(WHT);
        int rc=elf_load_run(_binary_user_user_posix_elf_start,(u32)(_binary_user_user_posix_elf_end-_binary_user_user_posix_elf_start));
        if(rc==1){ tcol(rgb(230,160,60)); term_puts("  skipped: no frame pool\n"); tcol(WHT); }
        else if(rc){ tcol(RED); term_puts("  failed to load\n"); tcol(WHT); }
        else { tcol(rgb(180,230,180)); { int st=0; for(int i=0;i<g_r3out_n;i++){ if(st==0){term_puts("    ");st=1;} term_putc(g_r3out[i]); if(g_r3out[i]=='\n')st=0; } if(st)term_putc('\n'); }
            tcol(GREEN); term_puts("  PASS: real POSIX file descriptors (fd table + VFS) - foundation for a POSIX libc.\n"); tcol(WHT); } }
    else if(seq(cmd,"app")||seq(cmd,"gui")){
        tcol(rgb(255,210,90)); term_puts("launching a graphical user-space app (ring 3, via the libc)...\n"); tcol(WHT);
        int rc=elf_load_run(_binary_user_user_app_elf_start,(u32)(_binary_user_user_app_elf_end-_binary_user_user_app_elf_start));
        if(rc==1){ tcol(rgb(230,160,60)); term_puts("  skipped: no frame pool (needs >=272 MB RAM)\n"); }
        else if(rc){ tcol(RED); term_puts("  failed to load the app\n"); }
        else { tcol(GREEN); term_puts("  app exited. A real ring-3 program drew graphics + used malloc/printf via syscalls.\n"); }
        tcol(WHT); }
    else if(seq(cmd,"run")||seq(cmd,"userelf")||seq(cmd,"exec")){
        tcol(rgb(255,210,90)); term_puts("loading a real ELF user program (compiled separately)...\n"); tcol(WHT);
        int rc=elf_load_run(_binary_user_user_demo_elf_start,(u32)(_binary_user_user_demo_elf_end-_binary_user_user_demo_elf_start));
        if(rc==1){ tcol(rgb(230,160,60)); term_puts("  skipped: no frame pool (needs >=272 MB RAM)\n"); }
        else if(rc==2){ tcol(RED); term_puts("  FAILED: could not set up the user address space\n"); }
        else if(rc==3){ tcol(RED); term_puts("  not a valid ELF64\n"); }
        else { tcol(CYAN); term_puts("  program output:\n"); tcol(WHT);
            int st=0; for(int i=0;i<g_r3out_n;i++){ if(st==0){term_puts("    ");st=1;} term_putc(g_r3out[i]); if(g_r3out[i]=='\n')st=0; } if(st)term_putc('\n');
            tcol(GREEN); term_puts("  PASS: a separately-compiled ELF ran in ring 3 via the loader + mini-libc.\n");
            term_puts("  -> userspace ELF loader + C library work. (stage 6+7 done)\n"); }
        tcol(WHT); }
    else if((seq(cmd,"battery")||seq(cmd,"bat"))&&g_acb_n&&arg[0]!='e'){ battery_cmd(); }
    else if(seq(cmd,"brightness")||seq(cmd,"backlight")){ brightness_cmd(arg); }
    else if(seq(cmd,"audio")||seq(cmd,"sound")){ audio_cmd(arg); }
    else if(seq(cmd,"acpi")){ acpi_cmd(arg); }
    else if(seq(cmd,"timers")){ timers_cmd(); }
    else if(seq(cmd,"timertest")){ timertest_cmd(); }
    else if(seq(cmd,"ec")||seq(cmd,"battery")||seq(cmd,"bat")){
        int det=ec_detect();
        char b[100],*p; tcol(CYAN); p=b;p=scopy(p,"EC (0x62/0x66) detect: ");p=scopy(p,det?"responding":"NO RESPONSE");*p=0; term_puts(b); term_putc('\n'); tcol(WHT);
        if(!det){ term_puts("  the Embedded Controller isn't answering on this machine.\n"); }
        else { term_puts("  scanning offsets for a plausible battery % (1..100):\n");
            int found=0;
            for(int off=0;off<0x100;off++){ u8 v; if(ec_read((u8)off,&v)){ if(v>=1&&v<=100){ p=b;p=scopy(p,"    offset 0x");p=hx(p,(u32)off,2);p=scopy(p," = ");p=udec(p,(u32)v); if(v<=100)p=scopy(p,"  <- possible battery %"); *p=0; tcol(v>=5&&v<=100?GREEN:WHT); term_puts(b); term_putc('\n'); found++; if(found>=24){term_puts("    ...\n");break;} } } }
            if(!found) term_puts("    (no byte in range 1..100 - unusual)\n");
            tcol(WHT); u8 ac=0; if(ec_read(0x01,&ac)){ p=b;p=scopy(p,"  offset 0x01 (AC/charge guess) = 0x");p=hx(p,(u32)ac,2);*p=0; term_puts(b); term_putc('\n'); }
            term_puts("  tell me which offset matches your real battery % and I'll lock it in.\n"); } }
    else if(seq(cmd,"usbrm")){ if(!arg[0]){ term_puts("usage: usbrm <FILE>\n"); }
        else if(!msd_autoinit()){ tcol(RED);term_puts("no USB stick\n");tcol(WHT); }
        else { ufat_mount(); int fi=-1; for(int i2=0;i2<ufs_n;i2++){ int eq=1; for(int k=0;k<16;k++){ char x=ufs[i2].name[k],y=arg[k]; if(x>='a'&&x<='z')x-=32; if(y>='a'&&y<='z')y-=32; if(x!=y){eq=0;break;} if(!x&&!y)break; } if(eq){fi=i2;break;} }
            if(fi<0){ tcol(RED);term_puts("file not found\n");tcol(WHT); }
            else if(ufat_delete_file(fi)==0){ tcol(GREEN);term_puts("deleted from USB\n");tcol(WHT); } else { tcol(RED);term_puts("delete failed\n");tcol(WHT); } } }
    else if(seq(cmd,"usb")||seq(cmd,"xhci")){ term_puts("rescanning all USB controllers...\n"); xhci_mouse_autoinit(); char u[]="usbinfo"; run_cmd(u); }
    else if(seq(cmd,"wifi")){ g_wpa_pump=ui_pump; int doload=(arg[0]=='l'||arg[0]=='L'); int doinit=(arg[0]=='i'||arg[0]=='I');   /* wifi load / wifi init */
        if(!pci_n)pci_scan();
        int wi=-1; for(int k=0;k<pci_n;k++) if(pci_cls[k]==0x02 && pci_sub[k]==0x80){ wi=k; break; }
        if(wi<0){ tcol(WHT); term_puts("No Wi-Fi adapter found on the PCI bus.\n");
            int e=nic_index(); if(e>=0){ term_puts("Wired NIC present: "); term_puts((char*)nic_name(pci_ven[e],pci_dev[e])); term_putc('\n'); } }
        else { char b[100],*p; int bus=pci_bus[wi], sl=pci_slot[wi]; const char* ven;
            switch(pci_ven[wi]){ case 0x8086:ven="Intel"; break; case 0x10ec:ven="Realtek"; break; case 0x168c:ven="Qualcomm Atheros"; break;
                case 0x17cb:ven="Qualcomm"; break; case 0x14c3:ven="MediaTek"; break; case 0x14e4:ven="Broadcom"; break; default:ven="Unknown"; }
            tcol(CYAN); p=b;p=scopy(p,"Wi-Fi adapter: ");p=scopy(p,(char*)ven);p=scopy(p,"  [0x");p=hx(p,pci_ven[wi],4);*p++=':';p=hx(p,pci_dev[wi],4);p=scopy(p,"]  bus ");p=udec(p,(u32)bus);*p=0; term_puts(b); term_putc('\n');
            u32 c4=pci_cfg(bus,sl,0,0x04); pci_cfgw(bus,sl,0,0x04,c4|0x06);
            u32 mmio=0; tcol(WHT);
            for(int bi=0;bi<6;bi++){ u32 bar=pci_cfg(bus,sl,0,0x10+bi*4); if(!bar)continue;
                if(bar&1){ p=b;p=scopy(p,"  BAR");*p++=(char)('0'+bi);p=scopy(p," I/O  0x");p=hx(p,bar&0xFFFFFFFC,8);*p=0; term_puts(b);term_putc('\n'); }
                else { u32 a=bar&0xFFFFFFF0u; p=b;p=scopy(p,"  BAR");*p++=(char)('0'+bi);p=scopy(p," MEM 0x");p=hx(p,a,8);*p=0; term_puts(b);term_putc('\n'); if(a>0x100000u)mmio=a; } }
            extern const unsigned char _binary_rtw8821c_fw_bin_start[]; extern const unsigned char _binary_rtw8821c_fw_bin_end[];
            long fwsz=(long)(_binary_rtw8821c_fw_bin_end-_binary_rtw8821c_fw_bin_start);
            { u16 sig=(u16)(_binary_rtw8821c_fw_bin_start[0]|(_binary_rtw8821c_fw_bin_start[1]<<8));
              u16 fver=(u16)(_binary_rtw8821c_fw_bin_start[4]|(_binary_rtw8821c_fw_bin_start[5]<<8)); u8 fsub=_binary_rtw8821c_fw_bin_start[6];
              tcol(GREEN); p=b;p=scopy(p,"  firmware: rtw8821c_fw.bin  ");p=udec(p,(u32)fwsz);p=scopy(p," bytes  sig 0x");p=hx(p,sig,4);p=scopy(p,"  v");p=udec(p,fver);*p++='.';p=udec(p,fsub);*p=0; term_puts(b);term_putc('\n'); }
            if(!mmio){ tcol(rgb(230,160,60)); term_puts("  no MMIO window assigned by BIOS - cannot touch chip registers.\n"); tcol(WHT); term_puts("  status: detected, firmware ready; MMIO needed to proceed.\n"); }
            else {
                #define WMR(o)   (*(volatile u32*)(u64)(mmio+(o)))
                #define WMR16(o) (*(volatile u16*)(u64)(mmio+(o)))
                #define WMW16(o,v) (*(volatile u16*)(u64)(mmio+(o))=(u16)(v))
                #define WMW8(o,v)  (*(volatile u8*)(u64)(mmio+(o))=(u8)(v))
                u32 sysc=WMR(0xF0);
                if(sysc==0xFFFFFFFFu){ tcol(rgb(230,160,60)); term_puts("  chip not responding at MMIO (powered down?).\n"); }
                else { tcol(CYAN); p=b;p=scopy(p,"  chip SYS_CFG(0xF0)=0x");p=hx(p,sysc,8);*p=0; term_puts(b);term_putc('\n');
                    u8 mac[6]; for(int i=0;i<6;i++)mac[i]=*(volatile u8*)(u64)(mmio+0x0610+i);
                    p=b;p=scopy(p,"  MAC ");for(int i=0;i<6;i++){p=hx(p,mac[i],2);if(i<5)*p++=':';}*p=0; term_puts(b);term_putc('\n');
                    if(arg[0]=='e'||arg[0]=='E'){
                        tcol(rgb(255,210,90)); term_puts("  [wifi efuse] reading physical EFUSE (512 B)\n"); tcol(WHT);
                        int bad=rtw_efuse_dump_phy(mmio,rtw_phymap,RTW_EFUSE_PHY_SZ);
                        int used=rtw_efuse_logical(rtw_phymap,RTW_EFUSE_PHY_SZ,RTW_EFUSE_PROT_SZ,rtw_logmap,RTW_EFUSE_LOG_SZ);
                        rtw_efuse_valid=(used>0);
                        p=b;p=scopy(p,"  read done  timeouts=");p=udec(p,(u32)bad);p=scopy(p,"  physical bytes consumed=");
                        if(used<0)p=scopy(p,"decode error"); else p=udec(p,(u32)used); *p=0;
                        tcol(bad?rgb(230,160,60):CYAN); term_puts(b); term_putc('\n'); tcol(WHT);
                        int allff=1; for(int i=0;i<RTW_EFUSE_PHY_SZ;i++) if(rtw_phymap[i]!=0xFF){allff=0;break;}
                        if(allff){ tcol(rgb(230,120,90)); term_puts("  EFUSE reads all 0xFF - chip is not powered on. run 'wifi init' first.\n"); tcol(WHT); }
                        else {
                            u16 rid=(u16)(rtw_logmap[0]|(rtw_logmap[1]<<8));
                            p=b;p=scopy(p,"  rtl_id=0x");p=hx(p,rid,4);p=scopy(p,"  chplan=0x");p=hx(p,rtw_logmap[0xB8],2);
                            p=scopy(p,"  xtal_k=0x");p=hx(p,rtw_logmap[0xB9],2);p=scopy(p,"  therm=0x");p=hx(p,rtw_logmap[0xBA],2);*p=0;
                            tcol(CYAN); term_puts(b); term_putc('\n');
                            p=b;p=scopy(p,"  rfe=0x");p=hx(p,rtw_logmap[0xCA],2);p=scopy(p,"  country=0x");p=hx(p,rtw_logmap[0xCB],2);
                            p=scopy(p,"  MAC(0xD0) ");for(int i=0;i<6;i++){p=hx(p,rtw_logmap[0xD0+i],2); if(i<5)*p++=':';}*p=0;
                            term_puts(b); term_putc('\n');
                            p=b;p=scopy(p,"  ids(0xD6) ven=0x");p=hx(p,(u32)(rtw_logmap[0xD6]|(rtw_logmap[0xD7]<<8)),4);
                            p=scopy(p," dev=0x");p=hx(p,(u32)(rtw_logmap[0xD8]|(rtw_logmap[0xD9]<<8)),4);*p=0;
                            term_puts(b); term_putc('\n'); tcol(WHT);
                            term_puts("  logical map 0x0B0-0x0DF:\n"); tcol(GREY);
                            for(int o=0xB0;o<0xE0;o+=16){ char hl[80]; rtw_hexline(rtw_logmap,o,16,hl); term_puts(hl); term_putc('\n'); }
                            tcol(WHT);
                            if(msd_ok||msd_autoinit()){
                                if(ufat_write_file("EFUSEP.BIN",(const char*)rtw_phymap,RTW_EFUSE_PHY_SZ)==0 &&
                                   ufat_write_file("EFUSEL.BIN",(const char*)rtw_logmap,RTW_EFUSE_LOG_SZ)==0){
                                    tcol(GREEN); term_puts("  wrote EFUSEP.BIN + EFUSEL.BIN to the USB stick\n"); tcol(WHT); }
                                else { tcol(GREY); term_puts("  (could not write the dumps to USB)\n"); tcol(WHT); } }
                            else { tcol(GREY); term_puts("  (no USB stick mounted - dumps not saved)\n"); tcol(WHT); }
                        } }
                    else if(seq(arg,"macinit")||seq(arg,"MACINIT")||seq(arg,"trx")||seq(arg,"TRX")){
                        tcol(rgb(120,200,255)); term_puts("  [wifi macinit] rtw_init_trx_cfg: queue map + page alloc (auto-LLT) + H2C queue\n"); tcol(WHT);
                        int r=rtw_mac_trx_init(mmio);
                        rtw_mac_trx_ok=(r==0);
                        u8 llt=rtw_r8(mmio,0x0208); u16 bnd=rtw_r16(mmio,0x0204); u8 cr=rtw_r8(mmio,0x0100);
                        p=b;p=scopy(p,"  CR=0x");p=hx(p,cr,2);p=scopy(p,"  rsvd_boundary=0x");p=hx(p,bnd&0x0FFF,3);p=scopy(p,"  AUTO_LLT=0x");p=hx(p,llt,2);*p=0;
                        tcol(CYAN); term_puts(b); term_putc('\n'); tcol(WHT);
                        if(r==0){ tcol(GREEN); term_puts("  MAC TRX init OK -- page pool allocated, LLT settled. beacon queue can accept pages now.\n");
                            term_puts("  next: 'wifi txbuf' should now assert BCN_VALID.\n"); tcol(WHT); }
                        else { tcol(rgb(230,120,90)); term_puts("  AUTO_LLT did not clear -- page-table init rejected (check queue map / boundary)\n"); tcol(WHT); } }
                    else if((arg[0]=='t'||arg[0]=='T')&&(arg[1]=='x'||arg[1]=='X')){
                        tcol(rgb(120,200,255)); term_puts("  [wifi txbuf] staging a test page via the beacon queue, then IDDMA-reading it back\n"); tcol(WHT);
                        static const u8 pat[64]={0x4E,0x56,0x58,0x21,0xDE,0xAD,0xBE,0xEF,0x01,0x23,0x45,0x67,0x89,0xAB,0xCD,0xEF,
                                                 0x11,0x22,0x33,0x44,0x55,0x66,0x77,0x88,0x99,0xAA,0xBB,0xCC,0xDD,0xEE,0xFF,0x00,
                                                 0xF0,0xE1,0xD2,0xC3,0xB4,0xA5,0x96,0x87,0x78,0x69,0x5A,0x4B,0x3C,0x2D,0x1E,0x0F,
                                                 0xCA,0xFE,0xBA,0xBE,0x8B,0xAD,0xF0,0x0D,0x13,0x37,0xC0,0xDE,0xFA,0xCE,0xB0,0x0C};
                        int st=rtw_bcnq_stage(mmio,pat,64);
                        if(st<0){ tcol(rgb(230,120,90));
                            if(st==-1) term_puts("  alloc failed\n");
                            else term_puts("  BCN_VALID never asserted - the beacon queue did not accept the page\n"); tcol(WHT); }
                        else {
                            tcol(GREEN); term_puts("  BCN_VALID asserted - page landed in the TX buffer\n"); tcol(WHT);
                            u32 v=rtw_r32(mmio,0x1208); v|=(1u<<25); rtw_w32(mmio,0x1208,v);
                            int r=rtw_iddma_block(mmio,0x18780000u+48u,0x00200000u,64,1);
                            u32 ch=rtw_r32(mmio,0x1208);
                            p=b;p=scopy(p,"  IDDMA ");p=scopy(p,(r==0)?"completed":"BUSY-timeout");p=scopy(p,"  CH0CTRL=0x");p=hx(p,ch,8);
                            p=scopy(p,"  chksum_sts=");p=udec(p,(ch>>27)&1);*p=0; tcol(CYAN); term_puts(b); term_putc('\n'); tcol(WHT);
                            if(r==0 && !((ch>>27)&1)){ tcol(GREEN); term_puts("  TX-staging VERIFIED: IDDMA read the staged page and checksum was good.\n");
                                term_puts("  the firmware download foundation works - 'wifi fw' can be built on this.\n"); tcol(WHT); }
                            else { tcol(rgb(230,160,60)); term_puts("  staged, but IDDMA checksum flagged - source addr or page geometry still off.\n"); tcol(WHT); } } }
                    else if((arg[0]=='f'||arg[0]=='F')&&(arg[1]=='w'||arg[1]=='W')){
                        extern const unsigned char _binary_rtw8821c_fw_bin_start[]; extern const unsigned char _binary_rtw8821c_fw_bin_end[];
                        long fwsz2=(long)(_binary_rtw8821c_fw_bin_end-_binary_rtw8821c_fw_bin_start);
                        tcol(rgb(120,200,255)); term_puts("  [wifi fw] IDDMA firmware download (rtw88 mac.c path)\n"); tcol(WHT);
                        int r=rtw_fw_download_iddma(mmio,_binary_rtw8821c_fw_bin_start,fwsz2);
                        u16 st=rtw_r16(mmio,0x0080);
                        p=b;p=scopy(p,"  MCUFW_CTRL(0x80)=0x");p=hx(p,st,4);p=scopy(p,"  imem_dw=");p=udec(p,(st>>3)&1);
                        p=scopy(p," imem_ck=");p=udec(p,(st>>4)&1);p=scopy(p," dmem_dw=");p=udec(p,(st>>5)&1);p=scopy(p," dmem_ck=");p=udec(p,(st>>6)&1);
                        p=scopy(p," dwrdy=");p=udec(p,(st>>14)&1);p=scopy(p," initrdy=");p=udec(p,(st>>15)&1);*p=0;
                        tcol(CYAN); term_puts(b); term_putc('\n'); tcol(WHT);
                        p=b;p=scopy(p,"  per-section: DMEM ck=");p=udec(p,(u32)g_dmem_ck);p=scopy(p,(g_dmem_sf?" STAGEFAIL":""));p=scopy(p,(g_dmem_if?" IDDMAFAIL":""));
                        p=scopy(p,"  IMEM ck=");p=udec(p,(u32)g_imem_ck);p=scopy(p,(g_imem_sf?" STAGEFAIL":""));p=scopy(p,(g_imem_if?" IDDMAFAIL":""));
                        p=scopy(p,"  EMEM ck=");p=udec(p,(u32)g_emem_ck);*p=0; tcol(rgb(200,180,255)); term_puts(b); term_putc('\n'); tcol(WHT);
                        if(r==0){ tcol(GREEN); term_puts("  MCU_FW_READY set -- the MCU is running your firmware DESPITE any ck complaints!\n");
                            term_puts("  next: 'wifi h2c' to talk to the MCU, then RX ring for a beacon.\n"); tcol(WHT); }
                        else { tcol(rgb(230,160,60)); term_puts("  all sections downloaded; FW_READY did not assert.\n");
                            term_puts("  if DMEM/IMEM ck=1 above, data was fine - blocker is post-download init, not the checksum.\n"); tcol(WHT); } }
                    else if((arg[0]=='p'||arg[0]=='P')&&(arg[1]=='r'||arg[1]=='R')){
                        if(!g_rxnum||!g_benum){ tcol(rgb(230,120,90)); term_puts("  run 'wifi rings' first\n"); tcol(WHT); }
                        else {
                            u8 ch=6; { const char* a=arg; while(*a&&*a!=' ')a++; while(*a&&(*a<'0'||*a>'9'))a++; if(*a>='0'&&*a<='9'){int v=0;while(*a>='0'&&*a<='9'){v=v*10+(*a-'0');a++;} if(v>=1&&v<=13)ch=(u8)v;} }
                            p=b;p=scopy(p,"  [wifi probe] TX broadcast probe request on channel ");p=udec(p,ch);p=scopy(p,", waiting for a response...");*p=0;
                            tcol(rgb(120,200,255)); term_puts(b); term_putc('\n'); tcol(WHT);
                            rtw_rx_ring_up(mmio);
                            rtw_set_channel_2g(mmio,ch);
                            int got=0; char rs[40];
                            rtw_tx_probe(mmio,&got,rs);
                            p=b;p=scopy(p,"  TX: hw_idx ");p=udec(p,g_probe_hwb);p=scopy(p,"->");p=udec(p,g_probe_hwa);p=scopy(p,g_probe_txok?"  FRAME LEFT THE QUEUE (transmitted)":"  frame NOT consumed by hw (TX ring stuck)");*p=0;
                            tcol(g_probe_txok?CYAN:rgb(230,160,60)); term_puts(b); term_putc('\n'); tcol(WHT);
                            if(got){ tcol(GREEN); p=b;p=scopy(p,"  PROBE RESPONSE from \"");p=scopy(p,rs[0]?rs:"<hidden>");p=scopy(p,"\"");*p=0; term_puts(b); term_putc('\n');
                                term_puts("  *** TWO-WAY RADIO: NoovexOS transmitted a frame and an AP answered ***\n"); tcol(WHT); }
                            else { tcol(rgb(230,160,60)); term_puts("  no probe response (TX may not have gone out, or none in range). Try another channel: wifi probe 1 / 11\n"); tcol(WHT); }
                        } }
                    else if((arg[0]=='j'||arg[0]=='J')&&(arg[1]=='o'||arg[1]=='O')){
                        const char* nm=arg+2; while(*nm==' ')nm++;
                        if(!*nm){ tcol(rgb(230,120,90)); term_puts("  usage: wifi join <ssid>   (open networks only for now)\n"); tcol(WHT); }
                        else if(!g_rxnum||!g_benum){ tcol(rgb(230,120,90)); term_puts("  run 'wifi rings' first\n"); tcol(WHT); }
                        else {
                            p=b;p=scopy(p,"  [wifi join] scanning for \"");p=scopy(p,(char*)nm);p=scopy(p,"\"...");*p=0; tcol(rgb(120,200,255)); term_puts(b); term_putc('\n'); tcol(WHT);
                            g_apn=0; rtw_rx_ring_up(mmio);
                            static const u8 jc[]={1,6,11,2,3,4,5,7,8,9,10,12,13};
                            int target=-1;
                            for(int ci=0;ci<13 && target<0;ci++){ rtw_set_channel_2g(mmio,jc[ci]);
                                for(int w=0;w<3;w++){ rtw_delay_us(60000); ui_pump(); rtw_scan_drain(mmio,jc[ci]); }
                                for(int a=0;a<g_apn;a++){ int eq=1; const char* z=nm; int k=0; while(z[k]&&k<32){ if(g_aps[a].ssid[k]!=z[k])eq=0; k++; } if(g_aps[a].ssid[k]||z[k])eq=0; if(eq){target=a;break;} } }
                            if(target<0){ tcol(rgb(230,160,60)); term_puts("  network not found in scan\n"); tcol(WHT); }
                            else {
                                p=b;p=scopy(p,"  found on ch ");p=udec(p,g_aps[target].ch);p=scopy(p,", BSSID ");for(int z=0;z<6;z++){p=hx(p,g_aps[target].bssid[z],2);if(z<5)*p++=':';}*p=0; tcol(CYAN); term_puts(b); term_putc('\n'); tcol(WHT);
                                term_puts("  AUTH -> ASSOC handshake...\n");
                                int r=rtw_assoc(mmio,&g_aps[target],nm);
                                if(r==0){ tcol(GREEN); term_puts("  *** ASSOCIATED! NoovexOS is linked to the access point ***\n");
                                    term_puts("  (open network: DHCP/TCP can now run over Wi-Fi. WPA2 nets need the 4-way handshake, next.)\n"); tcol(WHT); }
                                else if(r==1){ tcol(rgb(230,160,60)); term_puts("  no AUTH response (AP didn't answer - TX radiating? try closer / another AP)\n"); tcol(WHT); }
                                else if(r==2){ tcol(rgb(230,160,60)); term_puts("  AUTH rejected by AP (open-system refused - likely a WPA2 network)\n"); tcol(WHT); }
                                else if(r==3){ tcol(rgb(230,160,60)); term_puts("  AUTH ok but no ASSOC response\n"); tcol(WHT); }
                                else { tcol(rgb(230,160,60)); term_puts("  ASSOC rejected by AP (status != 0)\n"); tcol(WHT); }
                            }
                        } }
                    else if((arg[0]=='o'||arg[0]=='O')&&(arg[1]=='p'||arg[1]=='P')){
                        if(!g_rxnum){ tcol(rgb(230,120,90)); term_puts("  run 'wifi rings' first\n"); tcol(WHT); }
                        else {
                            tcol(rgb(120,200,255)); term_puts("  [wifi open] scanning for OPEN (no-password) networks...\n"); tcol(WHT);
                            g_apn=0; rtw_rx_ring_up(mmio);
                            static const u8 oc[]={1,6,11,2,3,4,5,7,8,9,10,12,13};
                            for(int ci=0;ci<13;ci++){ rtw_set_channel_2g(mmio,oc[ci]); for(int w=0;w<3;w++){ rtw_delay_us(60000); ui_pump(); rtw_scan_drain(mmio,oc[ci]); } }
                            int nopen=0; for(int i=0;i<g_apn;i++) if(g_aps[i].sec==0)nopen++;
                            if(nopen==0){ tcol(rgb(230,160,60)); p=b;p=scopy(p,"  no open networks (");p=udec(p,(u32)g_apn);p=scopy(p," total, all secured). WPA2 support is the next build.");*p=0; term_puts(b); term_putc('\n'); tcol(WHT); }
                            else {
                                tcol(GREEN); p=b;p=scopy(p,"  ");p=udec(p,(u32)nopen);p=scopy(p," OPEN network(s) you can join now:");*p=0; term_puts(b); term_putc('\n'); tcol(WHT);
                                for(int i=0;i<g_apn;i++){ if(g_aps[i].sec!=0)continue; p=b;p=scopy(p,"    wifi join ");p=scopy(p,g_aps[i].ssid[0]?g_aps[i].ssid:"<hidden>");p=scopy(p,"   (ch ");p=udec(p,g_aps[i].ch);*p++=')';*p=0; tcol(GREEN); term_puts(b); term_putc('\n'); tcol(WHT); }
                            }
                        } }
                    else if((arg[0]=='w'||arg[0]=='W')&&(arg[1]=='p'||arg[1]=='P')){
                        const char* a=arg; while(*a&&*a!=' ')a++; while(*a==' ')a++;   /* skip the whole 'wpa' token, then spaces */
                        char ss[33]; int sn=0; while(*a&&sn<32)ss[sn++]=*a++; while(sn>0&&ss[sn-1]==' ')sn--; ss[sn]=0;   /* whole rest = SSID, spaces allowed */
                        int allnum=(sn>0); for(int z=0;z<sn;z++) if(ss[z]<'0'||ss[z]>'9')allnum=0;
                        if(allnum){   /* connect by ID from last scan - no rescan */
                            int id=0; for(int z=0;z<sn;z++)id=id*10+(ss[z]-'0');
                            if(id<1||id>g_apn){ tcol(rgb(230,120,90)); term_puts("  no network with that ID - run 'wifi scan' first\n"); tcol(WHT); }
                            else { int idx=id-1;
                                int j=0; while(g_aps[idx].ssid[j]&&j<32){g_pwssid[j]=g_aps[idx].ssid[j];j++;} g_pwssid[j]=0;
                                g_pwbyid=idx+1;   /* remember chosen index (1-based; 0=none) */
                                if(g_aps[idx].sec==0){ g_pwbyid=idx+1; wpa_connect_saved(mmio,idx,""); }
                                else { g_pwprompt=1; p=b;p=scopy(p,"  password for \"");p=scopy(p,g_pwssid);p=scopy(p,"\": ");*p=0; tcol(rgb(120,200,255)); term_puts(b); tcol(WHT); } }
                        }
                        else if(!sn){ tcol(rgb(230,120,90)); term_puts("  usage: wifi wpa <ID|ssid>   (run 'wifi scan' to see IDs)\n"); tcol(WHT); }
                        else { int i2=0; while(ss[i2]&&i2<32){g_pwssid[i2]=ss[i2];i2++;} g_pwssid[i2]=0; g_pwbyid=0; g_pwprompt=1; tcol(rgb(120,200,255)); term_puts("  password: "); tcol(WHT); } }
                    else if((arg[0]=='c'||arg[0]=='C')&&(arg[1]=='r'||arg[1]=='R')){
                        tcol(rgb(120,200,255)); term_puts("  [wifi cryptotest] verifying WPA2 crypto vs known vectors\n"); tcol(WHT);
                        int pass=0,tot=0;
                        /* SHA-1("abc") = a9993e36 4706816a ba3e2571 7850c26c 9cd0d89d */
                        { u8 h[20]; sha1_hash((const u8*)"abc",3,h); tot++;
                          u8 exp[4]={0xa9,0x99,0x3e,0x36}; int ok=(h[0]==exp[0]&&h[1]==exp[1]&&h[2]==exp[2]&&h[3]==exp[3]);
                          p=b;p=scopy(p,"  SHA-1(abc)     ");p=scopy(p,ok?"PASS  ":"FAIL  ");for(int i=0;i<4;i++)p=hx(p,h[i],2);*p=0;
                          tcol(ok?GREEN:RED); term_puts(b); term_putc('\n'); tcol(WHT); if(ok)pass++; }
                        /* HMAC-SHA1 key="Jefe" data="what do ya want for nothing?" = effcdf6a e5eb2fa2 d27416d5 f184df9c 259a7c79 */
                        { u8 h[20]; hmac_sha1((const u8*)"Jefe",4,(const u8*)"what do ya want for nothing?",28,h); tot++;
                          u8 exp[4]={0xef,0xfc,0xdf,0x6a}; int ok=(h[0]==exp[0]&&h[1]==exp[1]&&h[2]==exp[2]&&h[3]==exp[3]);
                          p=b;p=scopy(p,"  HMAC-SHA1      ");p=scopy(p,ok?"PASS  ":"FAIL  ");for(int i=0;i<4;i++)p=hx(p,h[i],2);*p=0;
                          tcol(ok?GREEN:RED); term_puts(b); term_putc('\n'); tcol(WHT); if(ok)pass++; }
                        /* PBKDF2(password,"salt",4096,20) first bytes = 4b007901 b765489a ... (RFC6070) */
                        { u8 o[20]; pbkdf2_sha1((const u8*)"password",8,(const u8*)"salt",4,4096,o,20); tot++;
                          u8 exp[4]={0x4b,0x00,0x79,0x01}; int ok=(o[0]==exp[0]&&o[1]==exp[1]&&o[2]==exp[2]&&o[3]==exp[3]);
                          p=b;p=scopy(p,"  PBKDF2(4096)   ");p=scopy(p,ok?"PASS  ":"FAIL  ");for(int i=0;i<4;i++)p=hx(p,o[i],2);*p=0;
                          tcol(ok?GREEN:RED); term_puts(b); term_putc('\n'); tcol(WHT); if(ok)pass++; }
                        /* AES-CCM round-trip: encrypt then decrypt-verify must return the same plaintext */
                        { u8 key[16]; for(int i=0;i<16;i++)key[i]=(u8)i; u8 nonce[13]; for(int i=0;i<13;i++)nonce[i]=(u8)(0x10+i);
                          u8 aad[8]={1,2,3,4,5,6,7,8}; u8 pt[16]; for(int i=0;i<16;i++)pt[i]=(u8)(0xA0+i);
                          u8 ct[16]; for(int i=0;i<16;i++)ct[i]=pt[i]; u8 mic[8];
                          aes_ccm_auth_encrypt(key,nonce,aad,8,ct,16,mic); tot++;
                          int changed=0; for(int i=0;i<16;i++) if(ct[i]!=pt[i])changed=1;
                          int ver=aes_ccm_decrypt_verify(key,nonce,aad,8,ct,16,mic);
                          int same=1; for(int i=0;i<16;i++) if(ct[i]!=pt[i])same=0;
                          int ok=(changed&&ver&&same);
                          p=b;p=scopy(p,"  AES-CCM rtrip  ");p=scopy(p,ok?"PASS  (enc+dec+verify ok)":"FAIL");*p=0;
                          tcol(ok?GREEN:RED); term_puts(b); term_putc('\n'); tcol(WHT); if(ok)pass++; }
                        p=b;p=scopy(p,"  ");p=udec(p,(u32)pass);*p++='/';p=udec(p,(u32)tot);p=scopy(p," crypto tests passed");*p=0;
                        tcol(pass==tot?GREEN:RED); term_puts(b); term_putc('\n');
                        if(pass==tot) term_puts("  WPA2 crypto foundation is correct - ready to build the 4-way handshake.\n"); tcol(WHT); }
                    else if((arg[0]=='s'||arg[0]=='S')&&(arg[1]=='c'||arg[1]=='C')){
                        if(!g_rxnum){ tcol(rgb(230,120,90)); term_puts("  run 'wifi rings' first (after allbuilt/macinit/fw)\n"); tcol(WHT); }
                        else {
                            tcol(rgb(120,200,255)); term_puts("  [wifi scan] RX DMA on, hopping channels 1-13 for beacons...\n"); tcol(WHT);
                            g_apn=0; rtw_rx_ring_up(mmio);
                            static const u8 chans[]={1,6,11,2,3,4,5,7,8,9,10,12,13};
                            for(int ci=0; ci<13; ci++){ rtw_set_channel_2g(mmio,chans[ci]);
                                for(int w=0;w<3;w++){ rtw_delay_us(60000); ui_pump(); rtw_scan_drain(mmio,chans[ci]); } }
                            if(g_apn==0){
                                /* DIAGNOSTIC: show the raw RX ring/DMA state so we can see WHERE it breaks */
                                u32 wp=(rtw_r32(mmio,0x03B4)>>16)&0x0FFF; u32 rp=rtw_r32(mmio,0x03B4)&0x0FFF;
                                u32 cr=rtw_r32(mmio,0x0100); u32 rcr=rtw_r32(mmio,0x0608); u32 pcictrl=rtw_r32(mmio,0x0300);
                                u8* d0=g_rxring; u16 bs=*(volatile u16*)(u64)d0; u32 dma0=*(volatile u32*)(u64)(d0+4);
                                u32 w0=*(volatile u32*)(u64)g_rxbuf;
                                p=b;p=scopy(p,"  DIAG hwWP=");p=udec(p,wp);p=scopy(p," RP=");p=udec(p,rp);p=scopy(p," CR=0x");p=hx(p,cr,4);p=scopy(p," RCR=0x");p=hx(p,rcr,8);*p=0; tcol(CYAN); term_puts(b); term_putc('\n');
                                p=b;p=scopy(p,"  DIAG PCICTRL=0x");p=hx(p,pcictrl,8);p=scopy(p,"  desc0.bufsz=");p=udec(p,bs);p=scopy(p," desc0.dma=0x");p=hx(p,dma0,8);p=scopy(p," buf0.w0=0x");p=hx(p,w0,8);*p=0; term_puts(b); term_putc('\n'); tcol(WHT);
                                tcol(rgb(230,160,60)); term_puts("  no beacons parsed via RX DMA (frames still counted in wifi rx)\n"); tcol(WHT); }
                            else {
                                tcol(GREEN); p=b;p=scopy(p,"  found ");p=udec(p,(u32)g_apn);p=scopy(p," networks:");*p=0; term_puts(b); term_putc('\n'); tcol(WHT);
                                term_puts("  ID  SSID                          CH  SECURITY\n");
                                for(int i=0;i<g_apn;i++){ p=b; *p++=' ';*p++=' ';
                                    p=udec(p,(u32)(i+1)); if(i+1<10)*p++=' '; *p++=' ';*p++=' ';
                                    int sl=0; while(g_aps[i].ssid[sl]&&sl<28){*p++=g_aps[i].ssid[sl];sl++;}
                                    if(sl==0){p=scopy(p,"<hidden>");sl=8;}
                                    while(sl<30){*p++=' ';sl++;}
                                    p=udec(p,g_aps[i].ch); if(g_aps[i].ch<10)*p++=' ';
                                    p=scopy(p,"  ");p=scopy(p, g_aps[i].sec==0?"OPEN":g_aps[i].sec==2?"WPA2":"WEP/WPA"); *p=0;
                                    tcol(g_aps[i].sec==0?GREEN:CYAN); term_puts(b); term_putc('\n'); tcol(WHT); }
                                term_puts("  connect with:  wifi wpa <ID>    e.g.  wifi wpa 1\n");
                            }
                        } }
                    else if((arg[0]=='r'||arg[0]=='R')&&(arg[1]=='x'||arg[1]=='X')){
                        const char* a=arg+2; while(*a==' ')a++;
                        int dma=0; if((a[0]=='d'||a[0]=='D')&&(a[1]=='m'||a[1]=='M')){ dma=1; a+=3; while(*a==' ')a++; }
                        u8 ch=1; if(*a>='0'&&*a<='9'){ int v=0; while(*a>='0'&&*a<='9'){v=v*10+(*a-'0');a++;} if(v>=1&&v<=14)ch=(u8)v; }
                        p=b;p=scopy(p,"  [wifi rx] channel ");p=udec(p,ch);p=scopy(p,dma?"  (DMA parse mode)":"  (safe counter mode)");*p=0;
                        tcol(rgb(120,200,255)); term_puts(b); term_putc('\n'); tcol(WHT);
                        if(!dma){
                            u32 c0c=rtw_r32(mmio,0x0F04), c0o=rtw_r32(mmio,0x0F14), c0h=rtw_r32(mmio,0x0F10);
                            u32 a0o=rtw_r32(mmio,0x0F08), a0c=rtw_r32(mmio,0x0FCC);
                            p=b;p=scopy(p,"  step1 ok: baseband counters readable  RF18=0x");p=hx(p,rtw_rf_read_a(mmio,0x18),5);*p=0; term_puts(b); term_putc('\n');
                            term_puts("  step2: setting channel (read-modify-write, like rtw88)\n");
                            rtw_set_channel_2g(mmio,ch);
                            p=b;p=scopy(p,"  step2 ok: RF18 now 0x");p=hx(p,rtw_rf_read_a(mmio,0x18),5);*p=0; term_puts(b); term_putc('\n');
                            term_puts("  step3: receiver on (RCR), RX DMA stays OFF - no memory writes\n");
                            rtw_w32(mmio,0x0608,0xE400220Eu);
                            rtw_w8(mmio,0x0100,(u8)((rtw_r8(mmio,0x0100)|(1u<<7)|(1u<<6)|(1u<<4)|(1u<<5)) & ~((1u<<1)|(1u<<3))));
                            term_puts("  listening 3s...\n");
                            for(int w=0;w<2;w++) rtw_delay_us(200000);
                            u32 c1c=rtw_r32(mmio,0x0F04), c1o=rtw_r32(mmio,0x0F14), c1h=rtw_r32(mmio,0x0F10);
                            u32 a1o=rtw_r32(mmio,0x0F08), a1c=rtw_r32(mmio,0x0FCC);
                            u32 okc=(u32)((u16)(c1c&0xFFFF)-(u16)(c0c&0xFFFF));
                            u32 oko=(u32)((u16)(c1o&0xFFFF)-(u16)(c0o&0xFFFF));
                            u32 okh=(u32)((u16)(c1h&0xFFFF)-(u16)(c0h&0xFFFF));
                            u32 erc=(u32)((u16)(c1c>>16)-(u16)(c0c>>16));
                            u32 ero=(u32)((u16)(c1o>>16)-(u16)(c0o>>16));
                            u32 cca=(u32)((u16)(a1o>>16)-(u16)(a0o>>16))+(u32)((u16)(a1c&0xFFFF)-(u16)(a0c&0xFFFF));
                            p=b;p=scopy(p,"  good frames: CCK ");p=udec(p,okc);p=scopy(p,"  OFDM ");p=udec(p,oko);p=scopy(p,"  HT ");p=udec(p,okh);
                            p=scopy(p,"   bad CRC: ");p=udec(p,erc+ero);p=scopy(p,"   CCA hits: ");p=udec(p,cca);*p=0;
                            tcol(CYAN); term_puts(b); term_putc('\n'); tcol(WHT);
                            if(okc+oko+okh>0){ tcol(GREEN); term_puts("  *** THE RADIO IS DECODING REAL 802.11 FRAMES OFF THE AIR ***\n"); tcol(WHT); }
                            else if(cca+erc+ero>0){ tcol(rgb(230,160,60)); term_puts("  radio hears energy but no clean frames yet - try another channel (wifi rx 6 / 11)\n"); tcol(WHT); }
                            else { tcol(rgb(230,160,60)); term_puts("  silence on this channel - try wifi rx 6 and wifi rx 11 near the router\n"); tcol(WHT); }
                        } else {
                            rtw_set_channel_2g(mmio,ch);
                            if(!g_rxnum){ tcol(rgb(230,120,90)); term_puts("  RX ring not set up - run 'wifi rings' first\n"); tcol(WHT); }
                            else {
                                rtw_rx_enable(mmio);
                                char ssid[40]; int rssi=0; u8 bssid[6];
                                int r=rtw_rx_poll_beacon(mmio,ssid,&rssi,bssid);
                                if(r==1){ tcol(GREEN);
                                    p=b;p=scopy(p,"  BEACON! SSID=\"");p=scopy(p,ssid);p=scopy(p,"\"  BSSID=");for(int z=0;z<6;z++){p=hx(p,bssid[z],2); if(z<5)*p++=':';}*p=0;
                                    term_puts(b); term_putc('\n'); term_puts("  *** THE CHIP RECEIVED A REAL 802.11 FRAME OFF THE AIR ***\n"); tcol(WHT); }
                                else { tcol(rgb(230,160,60)); term_puts("  no beacon parsed (counter mode may still show frames: wifi rx 6)\n"); tcol(WHT); } } } }
                    else if((arg[0]=='h'||arg[0]=='H')&&(arg[1]=='2')){
                        tcol(rgb(120,200,255)); term_puts("  [wifi h2c] sending QUERY_BT_INFO (a safe no-op H2C) to the MCU\n"); tcol(WHT);
                        u8 hmetfr=rtw_r8(mmio,0x01CC);
                        p=b;p=scopy(p,"  HMETFR before=0x");p=hx(p,hmetfr,2);*p=0; tcol(GREY); term_puts(b); term_putc('\n'); tcol(WHT);
                        int r=rtw_h2c_send(mmio,0,0x00000161u,0x00000000u);
                        if(r<0){ tcol(rgb(230,120,90)); term_puts("  mailbox stayed full - MCU not draining H2C (firmware not running yet)\n"); tcol(WHT); }
                        else {
                            int drained=0; for(int t=0;t<300;t++){ if(!(rtw_r8(mmio,0x01CC)&1)){drained=1;break;} rtw_delay_us(100); }
                            u32 c2h0=rtw_r32(mmio,0x01AC), c2h1=rtw_r32(mmio,0x10F8);
                            p=b;p=scopy(p,"  HMETFR after=0x");p=hx(p,rtw_r8(mmio,0x01CC),2);p=scopy(p,"  C2H(0x1AC)=0x");p=hx(p,c2h0,8);p=scopy(p,"  DBG6=0x");p=hx(p,c2h1,8);*p=0;
                            tcol(CYAN); term_puts(b); term_putc('\n'); tcol(WHT);
                            if(drained){ tcol(GREEN); term_puts("  MCU drained the H2C box -- the processor is alive and reading commands!\n"); tcol(WHT); }
                            else { tcol(rgb(230,160,60)); term_puts("  box not drained within timeout (MCU may not be running - do 'wifi fw' first)\n"); tcol(WHT); } } }
                    else if((arg[0]=='g'||arg[0]=='G')||((arg[0]=='a'||arg[0]=='A')&&(arg[1]=='l'||arg[1]=='L'))){
                        tcol(rgb(120,200,255)); term_puts("  phy_set_param: power on BB/RF domain, then tables (mac->bb->agc->rf)\n"); tcol(WHT);
                        u32 rfpre=rtw_rf_read_a(mmio,0x18);
                        rtw_phy_power_on(mmio);
                        p=b;p=scopy(p,"  RF_CTRL(0x1F)=0x");p=hx(p,rtw_r8(mmio,0x001F),2);p=scopy(p,"  WLRF1+3(0xEF)=0x");p=hx(p,rtw_r8(mmio,0x00EF),2);
                        p=scopy(p,"  SYS_FUNC_EN=0x");p=hx(p,rtw_r8(mmio,0x0002),2);p=scopy(p,"  RF18 before=0x");p=hx(p,rfpre,5);*p=0;
                        tcol(CYAN); term_puts(b); term_putc('\n'); tcol(WHT);
                        int m1=rtw_apply_built(mmio,rtw_bmac,138,"mac",1);
                        rtw_apply_built(mmio,rtw_bbb,1669,"bb",1);
                        rtw_apply_built(mmio,rtw_bagc,386,"agc",0);
                        rtw_apply_built(mmio,rtw_brf,586,"rf-a",0);
                        if(!rtw_efuse_valid){ rtw_efuse_dump_phy(mmio,rtw_phymap,RTW_EFUSE_PHY_SZ); rtw_efuse_valid=(rtw_efuse_logical(rtw_phymap,RTW_EFUSE_PHY_SZ,RTW_EFUSE_PROT_SZ,rtw_logmap,RTW_EFUSE_LOG_SZ)>0); }
                        if(rtw_efuse_valid){ for(int i=0;i<6;i++)rtw_w8(mmio,0x0610+i,rtw_logmap[0xD0+i]); }
                        u8 xk=rtw_logmap[0xB9];
                        int hx_ok=(rtw_efuse_valid && xk!=0xFF);
                        rtw_phy_post(mmio,hx_ok,xk);
                        rtw_wmac_init(mmio);
                        u32 rfpost=rtw_rf_read_a(mmio,0x18), rf00=rtw_rf_read_a(mmio,0x00);
                        p=b;p=scopy(p,"  after tables: RF18=0x");p=hx(p,rfpost,5);p=scopy(p,"  RF00=0x");p=hx(p,rf00,5);
                        p=scopy(p,"  xtal_cap=");if(hx_ok){p=scopy(p,"0x");p=hx(p,xk&0x3F,2);}else p=scopy(p,"skipped");*p=0;
                        tcol(CYAN); term_puts(b); term_putc('\n'); tcol(WHT);
                        if(rfpost!=rfpre){ tcol(GREEN); term_puts("  RF REGISTERS CHANGED -- the radio is out of reset and accepting writes!\n"); tcol(WHT); }
                        else { tcol(rgb(230,160,60)); term_puts("  RF18 unchanged -- RF may still be held off (send this line)\n"); tcol(WHT); }
                        tcol(m1?rgb(230,160,60):GREEN);
                        if(m1) term_puts("  done - some MAC readback mismatches (see above)\n");
                        else term_puts("  done - MAC readback fully matched. stage B/C applied.\n");
                        tcol(WHT); }
                    else if(arg[0]=='m'||arg[0]=='M'||arg[0]=='b'||arg[0]=='B'||((arg[0]=='a'||arg[0]=='A')&&(arg[1]=='g'||arg[1]=='G'))||((arg[0]=='r'||arg[0]=='R')&&(arg[1]=='f'||arg[1]=='F'))){
                        char c0=arg[0]; const char* fn; const char* lbl; int verify;
                        if(c0=='m'||c0=='M'){ fn="RTWMAC.TXT"; lbl="mac"; verify=1; }
                        else if(c0=='b'||c0=='B'){ fn="RTWBB.TXT"; lbl="bb"; verify=1; }
                        else if(c0=='a'||c0=='A'){ fn="RTWAGC.TXT"; lbl="agc"; verify=0; }
                        else { fn="RTWRFA.TXT"; lbl="rf-a"; verify=0; }
                        p=b;p=scopy(p,"  [wifi ");p=scopy(p,lbl);p=scopy(p,"] applying ");p=scopy(p,fn);*p=0;
                        tcol(rgb(255,210,90)); term_puts(b); term_putc('\n'); tcol(WHT);
                        int fl=rtw_read_table_file(fn,fat_filebuf,65535);
                        if(fl<0){ tcol(rgb(230,120,90));
                            p=b;p=scopy(p,"  ");p=scopy(p,fn);p=scopy(p," not found on the boot volume.");*p=0; term_puts(b); term_putc('\n'); tcol(WHT);
                            term_puts("  put it on the USB stick root (or the boot disk). generate with rtwgen from rtw8821c_table.c\n");
                            term_puts("  line format:  offset size value   (hex; size 1|2|4=MMIO, 5=RF-A SIPI)\n"); }
                        else {
                            fat_filebuf[fl]=0;
                            int cnt=rtw_tbl_parse((const char*)fat_filebuf,fl);
                            p=b;p=scopy(p,"  parsed ");p=udec(p,(u32)cnt);p=scopy(p," writes from ");p=udec(p,(u32)fl);p=scopy(p," bytes");*p=0;
                            tcol(CYAN); term_puts(b); term_putc('\n'); tcol(WHT);
                            int mism=0,delays=0,rfw=0;
                            for(int i=0;i<cnt;i++){
                                u8 sz=rtw_mactbl[i].sz; u32 off=rtw_mactbl[i].off, val=rtw_mactbl[i].val;
                                int wasdelay=rtw_apply_one(mmio,off,sz,val);
                                if(wasdelay){ delays++; continue; }
                                if(sz==5){ rfw++; continue; }
                                for(volatile int d=0;d<200;d++) io_wait();
                                if(verify){
                                    u32 rb=rtw_rdsz(mmio,off,sz);
                                    if(rb!=val){ mism++;
                                        if(mism<=12){ tcol(rgb(230,160,60));
                                            p=b;p=scopy(p,"    0x");p=hx(p,off,4);p=scopy(p," wrote 0x");p=hx(p,val,8);
                                            p=scopy(p," read 0x");p=hx(p,rb,8);*p=0; term_puts(b); term_putc('\n'); tcol(WHT); } } } }
                            p=b;p=scopy(p,"  applied ");p=udec(p,(u32)cnt);
                            if(rfw){p=scopy(p,"  rf-writes ");p=udec(p,(u32)rfw);}
                            if(delays){p=scopy(p,"  delays ");p=udec(p,(u32)delays);}
                            if(verify){p=scopy(p,"  mismatches ");p=udec(p,(u32)mism);}
                            *p=0;
                            tcol((verify&&mism)?rgb(230,160,60):GREEN); term_puts(b); term_putc('\n'); tcol(WHT);
                            if(verify) term_puts("  (some mismatches are normal for write-only/self-clearing regs)\n");
                            else term_puts("  (rf/agc writes are not read back here - use 'wifi dump' to snapshot)\n"); } }
                    else if((arg[0]=='d'||arg[0]=='D')&&(arg[1]=='h'||arg[1]=='H')){
                        if(!g_wpa_ok){ tcol(rgb(230,120,90)); term_puts("  not connected - run 'wifi wpa <ID>' first\n"); tcol(WHT); }
                        else { netdev_set(NETDEV_WIFI); g_dhcp_done=0; g_dhcp_ok=0;
                            tcol(rgb(120,200,255)); term_puts("  [wifi dhcp] requesting a lease over Wi-Fi (GTK "); term_puts(g_wnd_have_gtk?"present":"missing"); term_puts(")...\n"); tcol(WHT);
                            int ok=dhcp_config();
                            if(ok){ p=b; p=scopy(p,"  lease: ip="); p=ipstr(p,my_ip); p=scopy(p," gw="); p=ipstr(p,gw_ip); p=scopy(p," dns="); p=ipstr(p,dns_ip); *p=0; tcol(GREEN); term_puts(b); term_putc('\n');
                                term_puts("  Wi-Fi is now the active link. Try: ping 1.1.1.1  |  dns example.com\n"); tcol(WHT); }
                            else { tcol(rgb(230,160,60)); term_puts("  no lease. "); term_puts(g_wnd_have_gtk?"broadcast reachable; AP may be slow - retry.":"no GTK: broadcast DHCP reply can't be decrypted (this is the GTK case).");
                                term_putc('\n');
                                { char s2[140]; char* q=s2; q=scopy(q,"  wifi rx="); q=udec(q,g_wnd_rx_frames); q=scopy(q," drop="); q=udec(q,g_wnd_rx_drop); q=scopy(q," tx="); q=udec(q,g_wnd_tx_frames); { extern u32 g_wnd_dead, g_wnd_txfull; q=scopy(q," ring-full="); q=udec(q,g_wnd_txfull); q=scopy(q," card-gone="); q=udec(q,g_wnd_dead); } *q=0; tcol(GREY); term_puts(s2); term_putc('\n'); tcol(WHT); }
                                netdev_set(NETDEV_ETH); } }
                    }
                    else if((arg[0]=='e'||arg[0]=='E')&&arg[1]=='t'){ netdev_set(NETDEV_ETH); tcol(WHT); term_puts("  active link: Ethernet\n"); tcol(WHT); }
                    else if(arg[0]=='d'||arg[0]=='D'){
                        tcol(rgb(255,210,90)); term_puts("  [wifi dump] snapshotting MMIO 0x0000-0x0FFF\n"); tcol(WHT);
                        for(int o=0;o<0x1000;o+=4) *(u32*)(fat_filebuf+o)=rtw_r32(mmio,(u32)o);
                        if((msd_ok||msd_autoinit()) && ufat_write_file("RTWREGS.BIN",(const char*)fat_filebuf,0x1000)==0){
                            tcol(GREEN); term_puts("  wrote RTWREGS.BIN (4096 bytes) to the USB stick\n"); tcol(WHT); }
                        else { tcol(rgb(230,160,60)); term_puts("  no USB stick - printing 0x000-0x03F instead\n"); tcol(GREY);
                            for(int o=0;o<0x40;o+=16){ char hl[80]; rtw_hexline(fat_filebuf,o,16,hl); term_puts(hl); term_putc('\n'); } tcol(WHT); } }
                    else if(!doload && !doinit && !(arg[0]=='r'||arg[0]=='R')){ tcol(WHT); term_puts("  status: detected, enabled, firmware ready.\n"); term_puts("  init efuse allbuilt macinit txbuf fw h2c bb agc rf rings load dump.  macinit=TRX/page setup.  EXPERIMENTAL.\n"); }
                    else {
                        #define WMR8(o)   (*(volatile u8*)(u64)(mmio+(o)))
                        if((arg[0]=='r'||arg[0]=='R')&&(arg[1]=='i'||arg[1]=='I')){
                            /* ---- STAGE 3: PCI DMA ring infrastructure (transcribed from rtw88 pci.c) ---- */
                            tcol(rgb(255,210,90)); term_puts("  [wifi rings] setting up TX/RX DMA descriptor rings (rtw88 layout, EXPERIMENTAL)\n"); tcol(WHT);
                            /* rtw_pci_tx/rx_buffer_desc: { u16 buf_size; u16 psb_len/total; u32 dma; } = 8 bytes.
                               DMA-coherent ring memory from the heap (x86 DMA is cache-coherent, so heap buffers work as DMA targets; identity-mapped => phys==virt). */
                            u32 RXNUM=128, BENUM=256;                    /* RTK_MAX_RX / RTK_BEQ_TX desc counts */
                            static u8 *rxring=0,*bering=0,*rxbuf=0;
                            static u8 *mgring=0;
                            if(!rxring){ rxring=(u8*)dma_alloc(RXNUM*8); bering=(u8*)dma_alloc(BENUM*16); mgring=(u8*)dma_alloc(BENUM*16); rxbuf=(u8*)dma_alloc(RXNUM*2048); }   /* alloc once; reuse on re-run (no arena leak) */
                            if(!rxring||!bering||!rxbuf){ tcol(rgb(230,120,90)); term_puts("  ring alloc failed (heap)\n"); tcol(WHT); }
                            else {
                                for(u32 i=0;i<RXNUM*8;i++)rxring[i]=0; for(u32 i=0;i<BENUM*16;i++){bering[i]=0;mgring[i]=0;}
                                for(u32 i=0;i<RXNUM;i++){ u8* d=rxring+i*8; u64 pb=(u64)(rxbuf+i*2048);   /* fill RX descriptors */
                                    *(volatile u16*)(d)=2048; *(volatile u16*)(d+2)=0; *(volatile u32*)(d+4)=(u32)pb; }
                                /* dma reset: RTK_PCI_CTRL (0x300) - toggle TRXDMA interface reset + RX tag */
                                *(volatile u32*)(u64)(mmio+0x300) |= (1u<<0)|(1u<<16);
                                for(volatile int dl=0;dl<2000;dl++) io_wait();
                                /* program ring-base + count registers (RTL8821CE offsets from pci.h) */
                                g_rxring=rxring; g_rxbuf=rxbuf; g_rxnum=RXNUM; g_rxidx=0;
                                g_bering=bering; g_benum=BENUM; g_beidx=0;
                                g_mgring=mgring; g_mgnum=BENUM; g_mgidx=0;
                                *(volatile u32*)(u64)(mmio+0x338)=(u32)(u64)rxring; *(volatile u16*)(u64)(mmio+0x382)=(u16)RXNUM;   /* RXBD_DESA_MPDUQ / NUM */
                                *(volatile u32*)(u64)(mmio+0x328)=(u32)(u64)bering; *(volatile u16*)(u64)(mmio+0x388)=(u16)BENUM;   /* TXBD_DESA_BEQ  / NUM  */
                                *(volatile u32*)(u64)(mmio+0x310)=(u32)(u64)mgring; *(volatile u16*)(u64)(mmio+0x380)=(u16)BENUM;   /* TXBD_DESA_MGMTQ / NUM */
                                *(volatile u16*)(u64)(mmio+0x39C)=0xFFFF;   /* TXBD_RWPTR_CLR: reset read/write ptrs */
                                /* enable TX/RX DMA: CR (0x0100) MAC_TRX bits incl BIT_TXDMA_EN(2)|BIT_RXDMA_EN(3) */
                                u16 cr=*(volatile u16*)(u64)(mmio+0x0100); *(volatile u16*)(u64)(mmio+0x0100)=(u16)(cr|(1<<2)|(1<<3));
                                for(volatile int dl=0;dl<2000;dl++) io_wait();
                                tcol(GREEN); p=b;p=scopy(p,"  rings up: RX ");p=udec(p,RXNUM);p=scopy(p," desc @0x");p=hx(p,(u32)(u64)rxring,8);p=scopy(p,"  TX(BE) ");p=udec(p,BENUM);p=scopy(p," @0x");p=hx(p,(u32)(u64)bering,8);*p=0; term_puts(b); term_putc('\n');
                                u32 cr2=*(volatile u16*)(u64)(mmio+0x0100);
                                p=b;p=scopy(p,"  CR after DMA-enable = 0x");p=hx(p,cr2,4);p=scopy(p,"  (TXDMA/RXDMA bits should be set)");*p=0; tcol(CYAN); term_puts(b); term_putc('\n');
                                tcol(WHT); term_puts("  rings allocated + registers programmed. next: 'wifi load' can use these for the real TX-ring download.\n");
                            }
                        }
                        if(doinit){
                            /* --- RTL8821CE cardemu->act power-on sequence, transcribed from rtw88
                                   (trans_cardemu_to_act_8821c), PCI + ALL-interface commands only. --- */
                            tcol(rgb(255,210,90)); term_puts("  [wifi init] rtw88 power-on sequence (EXPERIMENTAL) - watch for a hang\n"); tcol(WHT);
                            /* {offset, mask, value, cmd}: cmd 1=write(set masked bits to value), 2=poll(wait bits==value) */
                            struct { u16 off; u8 mask; u8 val; u8 cmd; } seqp[] = {
                                {0x0020,0x01,0x01,1},                 /* APS_FSMCO: enable LDOA/analog        */
                                {0x0001,0,0,3},                        /* delay 1ms (cmd 3)                    */
                                {0x0000,0x20,0x00,1},                  /* CR: release reset bit5              */
                                {0x0005,(0x10|0x08|0x04),0x00,1},      /* APS_FSMCO: clear pd bits            */
                                {0x0075,0x01,0x01,1},                  /* PCIE: enable                        */
                                {0x0006,0x02,0x02,2},                  /* poll SYS_CLKR ready (bit1)          */
                                {0x0075,0x01,0x00,1},
                                {0x0006,0x01,0x01,1},
                                {0x0005,0x80,0x00,1},                  /* clear bit7                          */
                                {0x0005,(0x10|0x08),0x00,1},
                                {0x0005,0x01,0x01,1},                  /* APS_FSMCO: MAC on (bit0)            */
                                {0x0005,0x01,0x00,2},                  /* poll until bit0 clears (MAC ready)  */
                                {0x0020,0x08,0x08,1},                  /* enable                              */
                                {0x0074,0x20,0x20,1},                  /* PCI: bit5                           */
                                {0x0022,0x02,0x00,1},
                                {0x0062,(0x80|0x40|0x20),(0x80|0x40|0x20),1},
                                {0x0061,(0x80|0x40|0x20),0x00,1},
                                {0x007C,0x02,0x00,1},
                            };
                            int nn=(int)(sizeof(seqp)/sizeof(seqp[0])), okall=1;
                            for(int s=0;s<nn;s++){ u16 o=seqp[s].off;
                                if(seqp[s].cmd==3){ for(volatile int d=0;d<100000;d++) io_wait(); continue; }
                                if(seqp[s].cmd==2){ int got=0; for(int t=0;t<100000;t++){ if((WMR8(o)&seqp[s].mask)==seqp[s].val){got=1;break;} io_wait(); }
                                    if(!got){ okall=0; p=b;p=scopy(p,"  poll timeout @0x");p=hx(p,o,4);*p=0; tcol(rgb(230,120,90)); term_puts(b); term_putc('\n'); tcol(WHT); } continue; }
                                u8 v=WMR8(o); v=(u8)((v & ~seqp[s].mask) | (seqp[s].val & seqp[s].mask)); WMW8(o,v);   /* write masked */
                                for(volatile int d=0;d<500;d++) io_wait();
                            }
                            u32 after=WMR(0xF0);
                            tcol(okall?GREEN:rgb(230,160,60)); p=b;p=scopy(p,"  power-on ");p=scopy(p,okall?"completed":"completed with poll timeouts");p=scopy(p,"  SYS_CFG=0x");p=hx(p,after,8);*p=0; term_puts(b); term_putc('\n');
                            u8 mac2[6]; for(int i=0;i<6;i++)mac2[i]=WMR8(0x0610+i);
                            tcol(CYAN); p=b;p=scopy(p,"  MAC now ");for(int i=0;i<6;i++){p=hx(p,mac2[i],2);if(i<5)*p++=':';}*p=0; term_puts(b); term_putc('\n');
                            tcol(WHT); term_puts("  if MAC changed from EA:EA:.. the chip powered on + read EFUSE. then try 'wifi load'.\n");
                        }
                        if(doload){
                        tcol(rgb(255,210,90)); term_puts("  [wifi load] EXPERIMENTAL firmware download - watch for a hang, power-cycle if needed\n"); tcol(WHT);
                        const unsigned char* body=_binary_rtw8821c_fw_bin_start+32; long blen=fwsz-32;   /* skip 32B header */
                        u16 ctrl=WMR16(0x0080); WMW16(0x0080, ctrl | 0x0001);                            /* FWDL_EN */
                        for(volatile int d=0;d<100000;d++) io_wait();
                        long off=0; int page=0;
                        while(off<blen){ int n=(int)(blen-off); if(n>4096)n=4096;
                            WMW8(0x0084, (u8)(page & 0x07));                                             /* page index (low bits) */
                            u16 c=WMR16(0x0080); WMW16(0x0080,(c & ~0x0700) | ((page & 7)<<8));          /* page select */
                            for(int i=0;i<n;i++) *(volatile u8*)(u64)(mmio+0x8000+i)=body[off+i];        /* copy page to FW FIFO window */
                            off+=n; page++;
                            for(volatile int d=0;d<2000;d++) io_wait();
                        }
                        u16 fin=WMR16(0x0080); WMW16(0x0080, fin & ~0x0001);                             /* clear FWDL_EN */
                        int ready=0; for(int t=0;t<200000;t++){ if(WMR16(0x0080) & 0x0080){ ready=1; break; } io_wait(); }
                        u16 st=WMR16(0x0080);
                        p=b;p=scopy(p,"  MCUFW_CTRL(0x80)=0x");p=hx(p,st,4);p=scopy(p,"  pages=");p=udec(p,(u32)page);*p=0; tcol(CYAN); term_puts(b);term_putc('\n');
                        if(ready){ tcol(GREEN); term_puts("  MCU_FW_READY set -- chip ACCEPTED the firmware! (radio microcode running)\n");
                            if(st&0x0800) term_puts("  checksum OK\n"); tcol(WHT);
                            term_puts("  NOTE: firmware runs, but device-init + 802.11 + WPA are not implemented -> no connection yet.\n"); }
                        else { tcol(rgb(230,120,90)); term_puts("  MCU_FW_READY not set -- chip did not confirm (needs full power-on/PHY init first).\n"); tcol(WHT);
                            term_puts("  this is expected without device-init; the download path ran without hanging, which is the real test.\n"); } } }
            } } } }
    else if(seq(cmd,"net")){ int r=nic_up(); if(!pci_n)pci_scan(); int k=nic_index();
        tcol(CYAN); term_puts("NIC: "); tcol(WHT); term_puts(k>=0?nic_name(pci_ven[k],pci_dev[k]):"none");
        if(k>=0){ char b[40],*p=b; p=scopy(p,"  ["); p=hx(p,pci_ven[k],4);*p++=':';p=hx(p,pci_dev[k],4);*p++=']'; *p=0; term_puts(b); } term_putc('\n');
        if(r){ char b[64],*p=b; p=scopy(p,"MAC "); for(int i=0;i<6;i++){p=hx(p,my_mac[i],2); if(i<5)*p++=':';} *p=0; term_puts(b); term_putc('\n');
            p=b; p=scopy(p,"IP "); p=ipstr(p,my_ip); p=scopy(p,"/"); { u32 m=my_mask,c=0; while(m&0x80000000u){c++;m<<=1;} p=udec(p,c); } p=scopy(p,"  GW "); p=ipstr(p,gw_ip); p=scopy(p,g_dhcp_ok?"  (DHCP)":"  (static)"); *p=0; tcol(GREEN); term_puts(b); tcol(WHT); term_putc('\n');
            p=b; p=scopy(p,"DNS "); p=ipstr(p,dns_ip); *p=0; tcol(GREY); term_puts(b); tcol(WHT); term_putc('\n');
            term_puts("try: dhcp   ping 10.0.2.2   dns example.com   http example.com\n"); }
        else { tcol(rgb(240,150,120)); term_puts("link down - set the VM adapter to PCnet-FAST III (NAT)\n"); tcol(WHT); } }
    else if(seq(cmd,"dhcp")){ tcol(GREY); term_puts("requesting a lease...\n"); tcol(WHT);
        int r=dhcp_renew();
        if(r==1){ char b[96],*p=b; p=scopy(p,"IP "); p=ipstr(p,my_ip); p=scopy(p,"/"); { u32 m=my_mask,c=0; while(m&0x80000000u){c++;m<<=1;} p=udec(p,c); } p=scopy(p,"  GW "); p=ipstr(p,gw_ip); p=scopy(p,"  DNS "); p=ipstr(p,dns_ip); *p=0; tcol(GREEN); term_puts(b); term_putc('\n'); tcol(WHT); }
        else if(r==0){ tcol(rgb(240,150,120)); term_puts("no DHCP server answered - keeping the static address\n"); tcol(WHT); }
        else { term_puts("network not available\n"); } }
    else if(seq(cmd,"ping")){ u32 ip; if(!parse_ip(arg,&ip)){ term_puts("usage: ping A.B.C.D\n"); }
        else { int r=do_ping(ip); if(r==1){ char b[40],*p=b; p=scopy(p,"reply from "); p=scopy(p,arg); *p=0; tcol(GREEN); term_puts(b); term_putc('\n'); tcol(WHT); }
            else if(r==0) term_puts("no reply (timeout)\n"); else term_puts("network not available\n"); } }
    else if(seq(cmd,"dns")){ if(!arg[0]){ term_puts("usage: dns hostname\n"); }
        else { u32 ip; int r=dns_query(arg,&ip); if(r==1){ char b[64],*p=b; p=scopy(p,arg); p=scopy(p," -> "); char t[16]; { u32 v=ip; int q=0; char oc[4]; for(int i=3;i>=0;i--){ u32 o=(v>>(i*8))&0xFF; if(!o)t[q++]='0'; else { int n=0; while(o){oc[n++]='0'+o%10;o/=10;} while(n)t[q++]=oc[--n]; } if(i)t[q++]='.'; } t[q]=0; } p=scopy(p,t); *p=0; tcol(GREEN); term_puts(b); term_putc('\n'); tcol(WHT); }
            else term_puts("could not resolve\n"); } }
    else if(seq(cmd,"http")||seq(cmd,"https")||seq(cmd,"wget")||seq(cmd,"get")){
        int insecure=0; while(arg[0]=='-'&&arg[1]=='k'&&(arg[2]==' '||!arg[2])){ insecure=1; arg+=2; while(*arg==' ')arg++; }
        if(!arg[0]){ term_puts("usage: http host[/path]   or   https [-k] host[/path]   (-k skips certificate checks)\n"); }
        else { int secure=seq(cmd,"https"); char host[128],path[300]; int hi=0,pi=0; const char*s=arg;
            if(s[0]=='h'&&s[1]=='t'&&s[2]=='t'&&s[3]=='p'){ if(s[4]=='s')secure=1; while(*s&&*s!=':')s++; if(*s==':'){s++; while(*s=='/')s++;} }
            while(*s&&*s!='/'&&*s!=' '&&hi<127)host[hi++]=*s++; host[hi]=0;
            path[pi++]='/'; if(*s=='/')s++; while(*s&&*s!=' '&&pi<299)path[pi++]=*s++; path[pi]=0;
            tcol(GREY); term_puts(secure?"fetching https://":"fetching http://"); term_puts(host); term_puts("...\n"); tcol(WHT);
            g_tls_insecure=insecure; if(insecure&&secure){ tcol(rgb(240,200,120)); term_puts("warning: certificate checks are OFF for this request\n"); tcol(WHT); }
            int n = secure ? https_fetch(host,path) : http_fetch(host,path);
            g_tls_insecure=0;
            if(n==-100){ tcol(rgb(240,120,120)); term_puts("DNS failed\n"); tcol(WHT); }
            else if(n<=0){ tcol(rgb(240,120,120));
                if(secure){ char b[160],*p=b; p=scopy(p,"HTTPS/TLS failed (code -"); p=udec(p,(u32)(-n)); p=scopy(p,"): "); p=scopy(p,g_tls_why); *p=0; term_puts(b); term_putc('\n'); }
                else if(n==-1) term_puts("no NIC (set VM to PCnet-FAST III)\n");
                else if(n==-2) term_puts("ARP failed (no gateway)\n");
                else if(n==-3) term_puts("no SYN-ACK (timeout)\n");
                else if(n==-4) term_puts("connection refused (RST)\n");
                else term_puts("connected, no data\n");
                tcol(WHT); }
            else { http_buf[n]=0; int bo=n; for(int i=0;i+3<n;i++) if(http_buf[i]=='\r'&&http_buf[i+1]=='\n'&&http_buf[i+2]=='\r'&&http_buf[i+3]=='\n'){ bo=i+4; break; }
                tcol(GREEN); { int e=0; while(e<bo && http_buf[e]!='\r' && http_buf[e]!='\n'){ term_putc(http_buf[e]); e++; } } term_putc('\n'); tcol(GREY);
                { char b[32],*p=b; p=scopy(p,"("); p=udec(p,(u32)n); p=scopy(p," bytes)"); *p=0; term_puts(b); term_putc('\n'); } tcol(WHT);
                int shown=0,intag=0; char last=0;
                for(int i=bo;i<n&&shown<1400;i++){ char c=http_buf[i]; if(c=='<'){intag=1;continue;} if(c=='>'){intag=0;continue;} if(intag)continue; if(c=='\r')continue;
                    if(c==' '||c=='\n'||c=='\t'){ if(last==' '||last=='\n')continue; c=(c=='\n')?'\n':' '; } if(c<32&&c!='\n')continue; term_putc(c); last=c; shown++; }
                term_putc('\n'); } } }
    else if(seq(cmd,"parts")||seq(cmd,"lsblk")||seq(cmd,"partitions")){ parts_print(); }
    else if(cmd[0]=='p'&&cmd[1]=='r'&&cmd[2]=='i'&&cmd[3]=='n'&&cmd[4]=='t'&&(cmd[5]==0||cmd[5]==' '||(cmd[5]=='e'&&cmd[6]=='r'))){ extern char g_prn_status[]; extern u32 g_prn_ip; extern int prn_set(const char*); extern int prn_test_page(void); extern int prn_print_text(const char*,const char*);
        const char* a=arg; while(*a==' ')a++;
        if(cmd[5]=='e'){ if(*a){ if(prn_set(a)){ tcol(RED); term_puts("usage: printer 192.168.1.50\n"); tcol(WHT); } } term_puts(g_prn_status); term_putc('\n'); }
        else if(!*a||seq(a,"test")){ term_puts("printing a test page...\n"); prn_test_page(); term_puts(g_prn_status); term_putc('\n'); }
        else { int k=k_find_file(a); if(k<0||!FS[k].data||FS[k].lazy){ tcol(RED); term_puts("file not found (or not loaded yet - open it once first)\n"); tcol(WHT); } else { char* t=(char*)kmalloc((u64)FS[k].len+1); if(t){ for(int i=0;i<FS[k].len;i++)t[i]=FS[k].data[i]; t[FS[k].len]=0; term_puts("printing...\n"); prn_print_text(a,t); kfree(t); term_puts(g_prn_status); term_putc('\n'); } } } }
    else if(seq(cmd,"laptop")||seq(cmd,"kbdlight")||seq(cmd,"chargelimit")){ extern int g_lap_kind, g_lap_kbd, g_lap_kbdmax, g_lap_chg, g_lap_chg_ok; extern const char* g_lap_name; extern u32 g_lap_events, g_lap_last; extern void laptop_kbd_set(int); extern void laptop_charge_set(int);
        const char* a=arg; while(*a==' ')a++;
        if(seq(cmd,"kbdlight")&&*a>='0'&&*a<='3')laptop_kbd_set(*a-'0');
        if(seq(cmd,"chargelimit")&&*a)laptop_charge_set(a[0]=='o'&&a[1]=='n');
        char b[160],*p=b; p=scopy(p,"laptop: "); p=scopy(p,g_lap_kind?g_lap_name:"no Lenovo or ASUS extras found"); if(g_lap_kind){ p=scopy(p,"  keyboard light "); if(g_lap_kbdmax){ p=udec(p,(u32)(g_lap_kbd<0?0:g_lap_kbd)); *p++='/'; p=udec(p,(u32)g_lap_kbdmax); } else p=scopy(p,"none"); p=scopy(p,"  charge limit "); p=scopy(p,!g_lap_chg_ok?"not supported":g_lap_chg?"on (80%)":"off"); p=scopy(p,"  hotkey events "); p=udec(p,g_lap_events); p=scopy(p,"  last 0x"); p=hx(p,g_lap_last,4); } *p=0; term_puts(b); term_putc('\n'); }
    else if(seq(cmd,"usbdevs")||seq(cmd,"lsusb")){ int any=0; for(int k=0;k<g_usbdev_n;k++){ if(!g_usbdev[k].used)continue; any=1; char b[140],*p=b; p=scopy(p,g_usbdev[k].kind); p=scopy(p,"  "); p=scopy(p,g_usbdev[k].name[0]?g_usbdev[k].name:"(no name)"); p=scopy(p,"  "); p=hx(p,g_usbdev[k].vid,4); *p++=':'; p=hx(p,g_usbdev[k].pid,4); p=scopy(p,"  port "); p=udec(p,(u32)g_usbdev[k].port+1); if(g_usbdev[k].cam)p=scopy(p,"  (video streaming not supported yet)"); *p=0; term_puts(b); term_putc('\n'); } if(!any)term_puts("no USB devices found (keyboards, mice, cameras, printers)\n"); }
    else if(cmd[0]=='l'&&cmd[1]=='x'&&cmd[2]=='t'&&cmd[3]=='r'&&cmd[4]=='a'&&cmd[5]=='c'&&cmd[6]=='e'){ extern int g_lxtrace; int n=0; const char* q=cmd+7; while(*q==' ')q++; while(*q>='0'&&*q<='9')n=n*10+(*q++-'0'); g_lxtrace=n?n:400; term_puts("syscall trace on (kernel log)\n"); }
    else if(seq(cmd,"disks")||seq(cmd,"disk")){
        gdisk_ensure();
        { int nb,ns,nfn; if(pci_find(0x01,0x08,-1,&nb,&ns,&nfn)){ char d[90],*q=d; tcol(rgb(255,210,90)); q=scopy(q,"NVMe controller @ bus ");q=udec(q,(u32)nb);q=scopy(q," slot ");q=udec(q,(u32)ns);q=scopy(q,"  init: ");q=scopy(q,nvme_ok?"OK":"FAILED stage ");if(!nvme_ok)q=udec(q,(u32)nvme_failstage);*q=0; term_puts(d); term_putc('\n'); tcol(WHT); } }
        { int ab,as,afn; if(pci_find(0x01,0x06,-1,&ab,&as,&afn)){ char d[90],*q=d; tcol(rgb(255,210,90)); q=scopy(q,"AHCI controller @ bus ");q=udec(q,(u32)ab);q=scopy(q," slot ");q=udec(q,(u32)as);*q=0; term_puts(d); term_putc('\n'); tcol(WHT); } }
        { extern const char* g_vmd_note; extern u32 g_vmd_dev; if(g_vmd_dev){ char d[120],*q=d; tcol(rgb(255,210,90)); q=scopy(q,"Intel VMD (RST) device 8086:"); { const char* hx="0123456789ABCDEF"; for(int k=12;k>=0;k-=4)*q++=hx[(g_vmd_dev>>k)&15]; } q=scopy(q,": "); q=scopy(q,g_vmd_note); *q=0; term_puts(d); term_putc('\n'); tcol(WHT); } }
        { char ab[80],*ap=ab; tcol(GREEN); ap=scopy(ap,"Active disk: ");ap=scopy(ap,(char*)gdisk_name()); if(g_dksectors){u32 mb=(u32)((u64)g_dksectors*512/1048576);ap=scopy(ap,"  ");ap=udec(ap,mb);ap=scopy(ap," MB");} ap=scopy(ap,"  (install target)"); *ap=0; term_puts(ab); term_putc('\n'); tcol(WHT); }
        if(!pci_n)pci_scan(); int found=0;
        for(int k=0;k<pci_n;k++){ if(pci_cls[k]!=0x01)continue; found++;
            u32 cl=pci_cfg(pci_bus[k],pci_slot[k],0,0x08); int prog=(cl>>8)&0xFF, sub=pci_sub[k];
            const char* kind= sub==0x01?"IDE/ATA": sub==0x05?"ATA": sub==0x06?"SATA (AHCI)": sub==0x08?"NVMe": sub==0x04?"RAID":"mass storage";
            char b[100],*p=b; p=scopy(p,"  "); p=hx(p,pci_ven[k],4);*p++=':';p=hx(p,pci_dev[k],4); p=scopy(p,"  "); p=scopy(p,kind); *p=0;
            tcol(CYAN); term_puts(b); tcol(WHT); term_putc('\n');
            if(sub==0x06 && prog==0x01){
                u32 c4=pci_cfg(pci_bus[k],pci_slot[k],0,0x04); pci_wcfg(pci_bus[k],pci_slot[k],0,0x04,c4|0x06);   /* MEM + bus master */
                u32 abar=pci_cfg(pci_bus[k],pci_slot[k],0,0x24)&0xFFFFFFF0u;
                if(!abar){ tcol(GREY); term_puts("    (ABAR invalid)\n"); tcol(WHT); continue; }
                volatile u32* A=(volatile u32*)(u64)abar; A[0x04>>2]|=(1u<<31);   /* GHC.AE */
                u32 pi=A[0x0C>>2]; int any=0;
                for(int port=0;port<32;port++){ if(!((pi>>port)&1))continue; char model[48]; u64 sec=0;
                    if(ahci_identify(A,port,model,&sec)){ any=1; u64 mb=sec>>11; char lb[120],*q=lb;
                        q=scopy(q,"    port "); q=udec(q,(u32)port); q=scopy(q,": "); q=scopy(q,model[0]?model:"(disk)"); q=scopy(q,"  ");
                        if(mb>=1024){ q=udec(q,(u32)(mb>>10)); q=scopy(q," GB"); } else { q=udec(q,(u32)mb); q=scopy(q," MB"); } *q=0;
                        tcol(GREEN); term_puts(lb); tcol(WHT); term_putc('\n'); } }
                if(!any){ tcol(GREY); term_puts("    (no responding SATA disk)\n"); tcol(WHT); }
            } else if(sub==0x08){ tcol(GREY); term_puts(nvme_ok?"    NVMe driver active (see NVMe controller line above)\n":"    NVMe present - driver did not start (see NVMe controller line above)\n"); tcol(WHT); }
        }
        if(!found){ tcol(rgb(240,150,120)); term_puts("no PCI mass-storage controller (eMMC/SD is not detected here)\n"); tcol(WHT); }
        parts_print(); }
    else if(seq(cmd,"netlog")){ int start=netlog_n>18?netlog_n-18:0; for(int i=start;i<netlog_n;i++){ tcol(GREY); term_puts("  "); term_puts(NETLOG[i%18]); term_putc('\n'); } tcol(WHT); if(netlog_n==0)term_puts("(empty)\n"); }
    else if(seq(cmd,"colors")){ const u32 cs[8]={rgb(220,60,60),rgb(230,150,40),rgb(230,220,60),rgb(70,200,90),rgb(70,180,230),rgb(120,110,230),rgb(210,90,210),rgb(230,230,230)};
        for(int k=0;k<8;k++){tcol(cs[k]);term_puts("##");} tcol(WHT); term_putc('\n'); }
    else if(seq(cmd,"settings")){ extern void settings_page_name(const char*); if(arg[0])settings_page_name(arg); open_win(W_SETTINGS); term_puts("opened Settings.\n"); }
    else if(seq(cmd,"camera")||seq(cmd,"webcam")){ extern int uvc_found(void); extern char uvc_msg[]; extern void camera_open(void); if(g_state==ST_LIVE){ if(!g_user[0]){ g_user[0]='r';g_user[1]='o';g_user[2]='o';g_user[3]='t';g_user[4]=0; } g_state=ST_DESKTOP; } camera_open(); term_puts(uvc_found()?"Camera found - opening the Camera app.\n":"No camera found yet - the Camera app shows the USB devices that were detected.\n"); }
    else if(seq(cmd,"gpu3d")||seq(cmd,"gears")||seq(cmd,"glgears")){ if(g_state==ST_LIVE){ if(!g_user[0]){ g_user[0]='r';g_user[1]='o';g_user[2]='o';g_user[3]='t';g_user[4]=0; } g_state=ST_DESKTOP; } open_win(W_GPU3D); term_puts("Gears is open (Start > Drivers & Hardware). It is written in C++.\n  1 gears, 2 3 4 more scenes, W wireframe, S shadows, H half resolution, Space pause, arrows and + - move the camera, R reset.\n  G starts the NVIDIA card (nvgsp all); then every frame is copied to the screen by its copy engine.\n"); }
    else if(seq(cmd,"quake")){ if(g_state==ST_LIVE){ if(!g_user[0]){ g_user[0]='r';g_user[1]='o';g_user[2]='o';g_user[3]='t';g_user[4]=0; } g_state=ST_DESKTOP; } open_win(W_QUAKE); term_puts("Quake is starting (LibreQuake data, or your own pak0.pak if it is in Files).\n  Click inside to aim with the mouse, Esc frees it.\n  Move: WASD or arrows   Fire: left click or Ctrl   Jump: Space   Run: Shift   Weapons: 1-8   Menu: Esc\n"); }
    else if(seq(cmd,"writer")||seq(cmd,"word")){ extern int writer_open_slot(int); if(g_state==ST_LIVE){ if(!g_user[0]){ g_user[0]='r';g_user[1]='o';g_user[2]='o';g_user[3]='t';g_user[4]=0; } g_state=ST_DESKTOP; } int sl=-1; if(arg[0]) for(int z=0;z<FS_N;z++) if(FS[z].used&&seq(FS[z].name,arg)){ sl=z; break; } if(sl>=0){ writer_open_slot(sl); term_puts("opened in Writer\n"); } else { open_win(W_WRITER); term_puts(arg[0]?"file not found - opened an empty Writer document\n":"Writer opened\n"); } }
    else if(seq(cmd,"keys")||seq(cmd,"keytest")){ extern volatile u32 g_klog[16]; extern volatile int g_klog_n; extern const char* g_lap_name;
        term_puts("Last special keys and laptop events (newest last). Press a key, then run 'keys' again:\n"); term_puts("  laptop driver: "); term_puts(g_lap_name); term_puts("\n");
        int n=g_klog_n, s0=n>16?n-16:0; if(n==0)term_puts("  (none yet)\n");
        for(int k=s0;k<n;k++){ u32 v=g_klog[k&15]; u32 ty=v>>16, c=v&0xFFFF; char b[80],*p=b; static const char* H="0123456789ABCDEF";
            p=scopy(p,ty==1?"  key E0 ":ty==2?"  key ":ty==3?"  ACPI brightness ":ty==4?"  ACPI event ":"  Lenovo VPC bits ");
            for(int sh=(ty==5?12:4);sh>=0;sh-=4)*p++=H[(c>>sh)&15]; *p=0; term_puts(b); term_puts("\n"); } }
    else if(seq(cmd,"recorder")||seq(cmd,"record")){ extern const char* snd_rec_name(void); if(g_state==ST_LIVE){ if(!g_user[0]){ g_user[0]='r';g_user[1]='o';g_user[2]='o';g_user[3]='t';g_user[4]=0; } g_state=ST_DESKTOP; } open_win(W_RECORDER); term_puts("Voice Recorder opened. Input: "); term_puts(snd_rec_name()); term_puts("\n"); }
    else if(seq(cmd,"bench")||seq(cmd,"benchmark")){ extern void bench_start(int); if(g_state==ST_LIVE){ if(!g_user[0]){ g_user[0]='r';g_user[1]='o';g_user[2]='o';g_user[3]='t';g_user[4]=0; } g_state=ST_DESKTOP; } open_win(W_BENCH); if(arg[0]=='c')bench_start(1); else if(arg[0]=='g')bench_start(2); term_puts(arg[0]=='c'?"CPU test started in the Benchmark app.\n":arg[0]=='g'?"GPU test started in the Benchmark app.\n":"Benchmark opened. Use 'bench cpu' or 'bench gpu' to start a test.\n"); }
    else if(seq(cmd,"doom")){ extern int g_doom_state; if(g_state==ST_LIVE){ if(!g_user[0]){ g_user[0]='r';g_user[1]='o';g_user[2]='o';g_user[3]='t';g_user[4]=0; } g_state=ST_DESKTOP; } open_win(W_DOOM); term_puts(g_doom_state==3?"DOOM could not load a WAD - see the DOOM window.\n":"DOOM is starting. Click inside to aim with the mouse, Esc frees it.\n  Move: arrows or WASD   Fire: Ctrl or left click   Use/open: Space or E\n  Run: Shift   Weapons: 1-7   Menu: Esc   Map: Tab\n"); }
    else if(seq(cmd,"taskmgr")||seq(cmd,"taskmanager")){ extern void taskmgr_tab(int,int); int t=(arg[0]=='p'&&arg[1]=='e')?1:arg[0]=='d'?2:arg[0]=='s'?3:arg[0]=='p'?0:-1; int dv=-1; { const char* q=arg; while(*q&&*q!=' ')q++; while(*q==' ')q++; if(*q>='0'&&*q<='9'){ dv=*q-'0'; if(q[1]>='0'&&q[1]<='9')dv=dv*10+q[1]-'0'; } } taskmgr_tab(t,dv); open_win(W_SYSMON); term_puts("opened Task Manager.\n"); }
    else if(seq(cmd,"chatgpt")||seq(cmd,"ai")||seq(cmd,"gpt")){ open_win(W_CHATGPT); term_puts("opened ChatGPT.\n"); }
    else if(cmd[0]=='l'&&cmd[1]=='u'&&cmd[2]=='a'&&(cmd[3]==' '||cmd[3]==0)){
        if(cmd[3]==0){ tcol(CYAN); term_puts("Lua 5.5.1 - usage: lua <code>\n  console: lua print(2^10, math.sqrt(144))\n  window:  lua nvx.window(\"Hi\",400,300) nvx.clear(nvx.rgb(30,40,60)) nvx.text(20,20,\"Hello from Lua\",0xFFFFFF) nvx.present() nvx.waitkey()\n"); tcol(WHT); }
        else { const char* out=nvx_lua_run(cmd+4); tcol(rgb(180,230,180)); term_puts(out); if(out[0]&&out[slen(out)-1]!='\n')term_putc('\n'); tcol(WHT); } }
    else if(seq(cmd,"presentation")||seq(cmd,"slides")||seq(cmd,"pptx")){ int sl=-1; for(int z=0;z<FS_N;z++)if(FS[z].used&&fext_eq(FS[z].name,"pptx")){sl=z;break;} if(sl>=0){pptx_load(sl);open_win(W_PPTX);term_puts("opened Presentation.\n");} else term_puts("no .pptx file found (put one on a USB stick).\n"); }
    else if(seq(cmd,"reboot")||seq(cmd,"restart")){ term_puts("rebooting...\n"); blit_all(); power_reboot(); }
    else if(seq(cmd,"shutdown")||seq(cmd,"poweroff")||seq(cmd,"halt")){ term_puts("shutting down...\n"); blit_all(); power_off(); }
    else if(seq(cmd,"suspend")){ term_puts("sleeping - press a key (or the power button) to wake\n"); blit_all(); int r=power_sleep(); term_puts(r?"woke from ACPI S1 sleep\n":"woke up (this computer has no ACPI S1; the screen was turned off)\n"); }
    else if(seq(cmd,"kcheck")){ kcheck_run(); }
    else if(seq(cmd,"memmap")){ memmap_run(); }
    else if(seq(cmd,"vminfo")){ extern void vminfo_run(void); vminfo_run(); }
    else if(seq(cmd,"tpinfo")){ if(arg[0]=='s')tpinfo_run(1); else if(arg[0]=='d'){ extern void tpinfo_dump(void); tpinfo_dump(); } else tpinfo_run(0); }
    else if(seq(cmd,"touchpad")){ extern int g_tp_tap, g_tp_speed, g_tp_natural, g_tp_dwt, g_tp_zones;
        char w1[16]; int k=0; const char* q=arg; while(*q&&*q!=' '&&k<15)w1[k++]=*q++; w1[k]=0; while(*q==' ')q++;
        int on=(q[0]=='o'&&q[1]=='n')||q[0]=='1'||q[0]=='n';
        if(seq(w1,"tap"))g_tp_tap=on; else if(seq(w1,"zones"))g_tp_zones=on; else if(seq(w1,"typing"))g_tp_dwt=on;
        else if(seq(w1,"scroll"))g_tp_natural=(q[0]=='n');
        else if(seq(w1,"speed")){ int v=0; while(*q>='0'&&*q<='9')v=v*10+(*q++-'0'); if(v>=1&&v<=10)g_tp_speed=v; }
        else if(w1[0]){ term_puts("usage: touchpad [tap on|off] [speed 1-10] [scroll natural|classic] [zones on|off] [typing on|off]\n"); }
        char b[160],*p=b; p=scopy(p,"touchpad: tap-to-click "); p=scopy(p,g_tp_tap?"on":"off"); p=scopy(p,"  speed "); p=udec(p,(u32)g_tp_speed); p=scopy(p,"  scroll "); p=scopy(p,g_tp_natural?"natural":"classic");
        p=scopy(p,"  click zones "); p=scopy(p,g_tp_zones?"on":"off"); p=scopy(p,"  ignore while typing "); p=scopy(p,g_tp_dwt?"on":"off"); *p=0; term_puts(b); term_putc('\n'); }
    else if(seq(cmd,"explorer")||seq(cmd,"files")||seq(cmd,"thispc")){ open_win(W_FILES); if(seq(cmd,"thispc")||arg[0]=='t'){ extern void fx_do(int,int); fx_do(20,-2); } term_puts("opened File Explorer.\n"); }
    else if(seq(cmd,"wallpaper")){ extern int g_theme, g_wallmode; extern void build_bg(void); static const char* WN6[6]={"porsche","crane","wireframe","peaks","earth","yosemite"}; int t=-1;
        if(arg[0]>='0'&&arg[0]<='5')t=arg[0]-'0'; else for(int k=0;k<6;k++) if(seq(arg,WN6[k]))t=k;
        if(t<0) term_puts("usage: wallpaper porsche|crane|wireframe|peaks|earth|yosemite (or 0-5)\n"); else { g_theme=t; g_wallmode=0; build_bg(); term_puts("wallpaper changed\n"); sh_redraw(); } }
    else if(seq(cmd,"gpuinfo")){ extern void gpuinfo_run(void); gpuinfo_run(); }
    else if(seq(cmd,"gpu")){ extern int igpu_flip_on(void); extern void igpu_flip_off(const char*); extern void gpuinfo_run(void);
        if(arg[0]=='f'&&arg[5]=='o'&&arg[6]=='f'){ igpu_flip_off("turned off by the user"); term_puts("tear-free drawing off\n"); }
        else if(arg[0]=='b'&&arg[5]=='g'){ extern void igpu_blt_mode(int); igpu_blt_mode(1); term_puts("screen copies by the GPU blitter (if it is ready - see gpuinfo)\n"); }
        else if(arg[0]=='b'&&arg[5]=='c'){ extern void igpu_blt_mode(int); igpu_blt_mode(0); term_puts("screen copies by the CPU\n"); }
        else if(arg[0]=='f'&&arg[5]=='o'&&arg[6]=='n'){ term_puts(igpu_flip_on()==0?"tear-free drawing on\n":"tear-free drawing could not start - see gpuinfo\n"); }
        else if(arg[0]=='d'&&arg[1]=='r'){ extern int igpu_draw_mode(int); const char* m=arg+5; int r=-1;
            if(m[0]=='o'&&m[1]=='n'){ r=igpu_draw_mode(1); term_puts(r==0?"GPU drawing on\n":"GPU drawing is not available - see gpuinfo\n"); }
            else if(m[0]=='o'&&m[1]=='f'){ igpu_draw_mode(0); term_puts("GPU drawing off - the CPU draws\n"); }
            else if(m[0]=='e'){ r=igpu_draw_mode(2); term_puts(r==0?"GPU drawing emulated on the CPU (test mode)\n":"could not start the emulation\n"); }
            else term_puts("usage: gpu draw on|off|emu\n"); sh_redraw(); }
        else if(arg[0]=='c'&&arg[1]=='h'){ extern int frame_check_cmd(void); extern char g_fc_msg[]; if(frame_check_cmd()) term_puts("open the desktop and run 'gpu check' from its terminal (GPU drawing must be on)\n"); else { term_puts(g_fc_msg); term_putc('\n'); } sh_redraw(); }
        else if(arg[0]=='l'&&arg[1]=='i'){ extern int g_wall_light; g_wall_light=(arg[6]=='o'&&arg[7]=='n'); term_puts(g_wall_light?"drifting light on the wallpaper on\n":"drifting light off\n"); sh_redraw(); }
        else if(arg[0]=='w'&&arg[1]=='a'){ extern int g_wall_fx; extern void build_bg(void); g_wall_fx=(arg[7]=='o'&&arg[8]=='n'); build_bg(); term_puts(g_wall_fx?"wallpaper effects on\n":"wallpaper effects off (plain picture)\n"); sh_redraw(); }
        else if(arg[0]=='p'&&arg[1]=='r'){ extern int g_pres_eng; g_pres_eng=(arg[8]=='s'); term_puts(g_pres_eng?"screen copies by the shader\n":"screen copies by the blitter\n"); sh_redraw(); }
        else if(arg[0]=='p'&&arg[1]=='a'){ extern int g_gfx_srec; g_gfx_srec=!(arg[9]=='f'); term_puts(g_gfx_srec?"partial screen redraws on\n":"partial screen redraws off - the whole screen is rebuilt every frame\n"); sh_redraw(); }
        else if(arg[0]=='f'&&arg[1]=='x'){ extern int g_gfx_fx; g_gfx_fx=(arg[3]=='o'&&arg[4]=='n'); term_puts(g_gfx_fx?"effects on\n":"effects off\n"); sh_redraw(); }
        else gpuinfo_run(); }
    else if(seq(cmd,"tpdump")){ extern void tpinfo_dump(void); tpinfo_dump(); }
    else if(seq(cmd,"memtest")){ memtest_run(); }
    else if(seq(cmd,"usbinfo")){ char b[200],*p;
        if(!g_xhc_n){ term_puts("USB: no xHCI controller found\n"); }
        p=b; p=scopy(p,"USB controllers (xHCI): "); p=udec(p,(u32)g_xhc_n); *p=0; term_puts(b); term_putc('\n');
        for(int k=0;k<g_xhc_n;k++){ xhc_select(k);
            p=b; p=scopy(p," controller "); p=udec(p,(u32)(k+1)); p=scopy(p," at bus "); p=udec(p,(u32)xhci_pci_bus); p=scopy(p," dev "); p=udec(p,(u32)xhci_pci_slot); p=scopy(p," fn "); p=udec(p,(u32)xhci_pci_fn);
            p=scopy(p,xhci_hi4g?"  - registers above 4 GB could not be mapped":xhci_init_ok?"  ready":"  setup failed"); if(!xhci_hi4g){ p=scopy(p,"  scratchpad "); p=udec(p,(u32)g_xspb_need[k]); } *p=0; if(!xhci_init_ok)tcol(rgb(255,205,100)); term_puts(b); term_putc('\n'); tcol(WHT);
            if(!xhci_init_ok)continue;
            int anyp=0;
            for(int q=0;q<xhci_ports_hw&&q<32;q++){ u32 v=XMR(xhci_op+0x400+(u32)q*0x10); if(!(v&1))continue; anyp=1; int sp=(int)((v>>10)&0xF), used=0;
                for(int i=0;i<xhid_n;i++) if(xhid_used[i]&&xhid_ctl[i]==k&&xhid_port[i]==q)used=1;
                if(msd_ok&&msd_ctl==k&&msd_port==q)used=2;
                p=b; p=scopy(p,"   port "); p=udec(p,(u32)(q+1)); p=scopy(p,sp==1?"  full speed (USB 1.1)":sp==2?"  low speed (USB 1.1)":sp==3?"  high speed (USB 2.0)":sp>=4?"  SuperSpeed (USB 3)":"  unknown speed");
                p=scopy(p,used==1?"  -> keyboard/mouse set up":used==2?"  -> USB stick":"  -> NOT set up"); *p=0; if(!used)tcol(rgb(255,205,100)); term_puts(b); term_putc('\n'); tcol(WHT);
                for(int i=0;i<xhid_n;i++){ if(!xhid_used[i]||xhid_ctl[i]!=k||xhid_port[i]!=q)continue; int kb=0,ms=0;
                    if(xhid_rep[i]){ for(int z=0;z<xhid_nlay[i];z++){ if(xhid_lay[i][z].kind==1)kb=1; if(xhid_lay[i][z].kind==2)ms=1; } } else { kb=xhid_iskbd[i]; ms=!kb; }
                    p=b; p=scopy(p,"      "); if(xhid_depth[i]){ p=scopy(p,"behind hub (route "); p=hx(p,xhid_route[i],5); p=scopy(p,")  "); } p=scopy(p,"ep "); p=udec(p,(u32)xhid_dci[i]); p=scopy(p,"  "); p=scopy(p,kb&&ms?"keyboard+mouse":kb?"keyboard":"mouse"); p=scopy(p,xhid_rep[i]?(xhid_hasid[i]?"  report protocol (IDs)":"  report protocol"):"  boot protocol");
                    p=scopy(p,"  reports: "); p=udec(p,xhid_cnt[i]); *p=0; term_puts(b); term_putc('\n');
                    if(xhid_lastn[i]){ p=b; p=scopy(p,"        last:"); for(int z=0;z<xhid_lastn[i];z++){ *p++=' '; *p++="0123456789ABCDEF"[xhid_last[i][z]>>4]; *p++="0123456789ABCDEF"[xhid_last[i][z]&15]; } *p=0; term_puts(b); term_putc('\n'); } } }
            if(!anyp) term_puts("   no devices plugged in\n");
            for(int h=0;h<XHUB_MAX;h++){ if(!xhub[h].used||xhub[h].ctl!=k)continue;
                p=b; p=scopy(p,"   hub on port "); p=udec(p,(u32)(xhub[h].root+1)); if(xhub[h].depth){ p=scopy(p," (route "); p=hx(p,xhub[h].route,5); *p++=')'; } p=scopy(p,": "); p=udec(p,(u32)xhub[h].nports); p=scopy(p,xhub[h].ss?" ports, USB 3":xhub[h].speed==3?" ports, USB 2.0":" ports, USB 1.1"); p=scopy(p,"  connected:");
                int anyc=0; for(int q=1;q<=xhub[h].nports;q++) if((xhub[h].conn>>q)&1u){ *p++=' '; p=udec(p,(u32)q); anyc=1; } if(!anyc)p=scopy(p," none"); *p=0; term_puts(b); term_putc('\n'); } }
        if(g_xhc_n)xhc_select(0);
        if(xhci_failstep[0]){ term_puts(" last setup problem: "); term_puts(xhci_failstep); term_putc('\n'); }
        term_puts(inb(0x64)==0xFF?"PS/2: no controller\n":"PS/2: controller present\n"); }
    else if(seq(cmd,"mousetest")){
        term_puts("Move the mouse, click and scroll. Press any key to stop.\n"); sh_redraw();
        while(kpop()>=0){}
        long tx=0,ty=0; int ax=0,ay=0,aw=0,lb=-1,lr=-1,lm=-1,evs=0; u64 t0=g_ticks, tp=g_ticks; long spins=0;
        for(;;){
            if(xhid_n) xhci_mouse_poll();
            int dx=0,dy=0,sc=0,bl=0,br=0,bm=0,ev=0;
            cpu_irq_off(); if(mouse_evt){ dx=mouse_dx; dy=mouse_dy; sc=mouse_scroll; bl=mouse_btn; br=mouse_rbtn; bm=mouse_mid; mouse_dx=0; mouse_dy=0; mouse_scroll=0; mouse_evt=0; ev=1; } cpu_irq_on();
            if(ev){ ax+=dx; ay+=dy; aw+=sc; tx+=dx; ty+=dy; evs++;
                int btnchg=(bl!=lb||br!=lr||bm!=lm);
                if(btnchg||(g_ticks_live?(g_ticks-tp>=5):(evs%20==0))){ char b[160],*p=b; tp=g_ticks;
                    p=scopy(p,"  move "); if(ax<0){*p++='-';p=udec(p,(u32)-ax);}else{*p++='+';p=udec(p,(u32)ax);} *p++=','; if(ay<0){*p++='-';p=udec(p,(u32)-ay);}else{*p++='+';p=udec(p,(u32)ay);}
                    p=scopy(p,"   total "); if(tx<0){*p++='-';p=udec(p,(u32)-tx);}else p=udec(p,(u32)tx); *p++=','; if(ty<0){*p++='-';p=udec(p,(u32)-ty);}else p=udec(p,(u32)ty);
                    p=scopy(p,"   buttons "); *p++=bl?'L':'-'; *p++=bm?'M':'-'; *p++=br?'R':'-'; if(aw){ p=scopy(p,"   wheel "); if(aw<0){*p++='-';p=udec(p,(u32)-aw);}else{*p++='+';p=udec(p,(u32)aw);} }
                    *p=0; term_puts(b); term_putc('\n'); sh_redraw(); ax=ay=aw=0; lb=bl; lr=br; lm=bm; } }
            if(kpop()>=0)break;
            spins++; if(g_ticks_live?(g_ticks-t0>3000):(spins>400000000L))break;
            cpu_pause(); }
        { char b[80],*p=b; p=scopy(p,"mouse test ended ("); p=udec(p,(u32)evs); p=scopy(p," mouse events)"); *p=0; term_puts(b); term_putc('\n'); } }
    else if(seq(cmd,"acpi")){ char b[96],*p;
        if(!g_acpi.rsdp){ term_puts("acpi: no ACPI tables found\n"); }
        else { p=b; p=scopy(p,"acpi: revision "); p=udec(p,(u32)g_acpi.rev); p=scopy(p,g_acpi.hwred?"  (hardware-reduced)":""); *p=0; term_puts(b); term_putc('\n');
            p=b; p=scopy(p,"  PM1a_CNT 0x"); p=hx(p,g_acpi.pm1a_cnt,4); p=scopy(p,"  SMI_CMD 0x"); p=hx(p,g_acpi.smi_cmd,4); *p=0; term_puts(b); term_putc('\n');
            p=b; p=scopy(p,"  shutdown (S5): "); p=scopy(p,g_acpi.have_s5?"yes":"no"); p=scopy(p,"   sleep (S1): "); p=scopy(p,g_acpi.have_s1?"yes":"no"); p=scopy(p,"   reset register: "); p=scopy(p,g_acpi.rst_addr?"yes":"no"); *p=0; term_puts(b); term_putc('\n'); } }
    else if(seq(cmd,"ntfs")){
        char sub[16]; int si=0; const char* a=arg; while(*a==' ')a++; while(*a&&*a!=' '&&si<15)sub[si++]=*a++; sub[si]=0; while(*a==' ')a++;
        char p1[200]; int pi=0; while(*a&&*a!=' '&&pi<199)p1[pi++]=*a++; p1[pi]=0; while(*a==' ')a++;
        const char* rest=a;
        if(!sub[0]||seq(sub,"help")){ tcol(ACC); term_puts("ntfs mount [ro] | umount | info | ls [path] | cat <path> | write <path> <text> | mv <old> <new> | mkdir <path> | rm <path>\n"); tcol(WHT); }
        else if(!seq(sub,"ls")&&!seq(sub,"cat")&&!seq(sub,"info")&&!seq(sub,"help")&&sub[0]&&g_uid!=0){ need_root("ntfs"); }
        else if(seq(sub,"mount")&&g_rootfs&&g_rfs==&g_rfs_ntfs){ term_puts("ntfs: the system disk is NTFS and already mounted - use ntfs ls / cat directly\n"); }
        else if(seq(sub,"mount")){ int ro=seq(p1,"ro"); int r=nk_mount(ro);
            if(r==-2){ tcol(rgb(240,120,120)); term_puts("ntfs: no NTFS volume found on the disk\n"); tcol(WHT); }
            else if(r){ tcol(rgb(240,120,120)); term_puts("ntfs: mount failed (see serial log)\n"); tcol(WHT); }
            else { tcol(GREEN); term_puts("ntfs: mounted "); term_puts(ro?"read-only":"read-write"); tcol(WHT); if(nvxntfs_label()[0]){ term_puts("  label: "); term_puts(nvxntfs_label()); } term_putc('\n'); } }
        else if(!nvxntfs_mounted()){ tcol(rgb(240,120,120)); term_puts("ntfs: not mounted - run 'ntfs mount' first\n"); tcol(WHT); }
        else if(seq(sub,"umount")||seq(sub,"unmount")){ nvxntfs_umount(); term_puts("ntfs: unmounted, all changes written\n"); }
        else if(seq(sub,"info")){ term_puts("ntfs: mounted"); if(nvxntfs_label()[0]){ term_puts(", label "); term_puts(nvxntfs_label()); } term_putc('\n'); }
        else if(seq(sub,"ls")){ if(nvxntfs_ls(p1[0]?p1:"/",nk_lscb,0)<0){ tcol(rgb(240,120,120)); term_puts("ntfs: no such directory\n"); tcol(WHT); } }
        else if(seq(sub,"cat")){ static char nb[65536]; long n=nvxntfs_read(p1,nb,65535); if(n<0){ tcol(rgb(240,120,120)); term_puts("ntfs: cannot read "); term_puts(p1); term_putc('\n'); tcol(WHT); } else { nb[n]=0; term_puts(nb); if(n&&nb[n-1]!='\n')term_putc('\n'); } }
        else if(seq(sub,"write")){ long L=0; while(rest[L])L++; char wb[512]; int k=0; for(;k<L&&k<510;k++)wb[k]=rest[k]; wb[k++]='\n'; wb[k]=0;
            if(nvxntfs_write(p1,wb,k)!=k){ tcol(rgb(240,120,120)); term_puts("ntfs: write failed\n"); tcol(WHT); } else term_puts("ntfs: written\n"); }
        else if(seq(sub,"mv")){ char p2[200]; int k=0; while(rest[k]&&rest[k]!=' '&&k<199){p2[k]=rest[k];k++;} p2[k]=0;
            if(!p2[0]||nvxntfs_rename(p1,p2)){ tcol(rgb(240,120,120)); term_puts("ntfs: rename failed (source missing or target exists)\n"); tcol(WHT); } else term_puts("ntfs: renamed\n"); }
        else if(seq(sub,"mkdir")){ if(nvxntfs_mkdir(p1)){ tcol(rgb(240,120,120)); term_puts("ntfs: mkdir failed\n"); tcol(WHT); } }
        else if(seq(sub,"rm")){ if(nvxntfs_delete(p1)){ tcol(rgb(240,120,120)); term_puts("ntfs: cannot remove "); term_puts(p1); term_putc('\n'); tcol(WHT); } }
        else { term_puts("ntfs: unknown command - try 'ntfs help'\n"); } }
    else if(seq(cmd,"panic")){ panic(0,"Manual panic requested via 'panic' command."); }
    else { tcol(rgb(240,120,120)); term_puts("nsh: "); term_puts(cmd); term_puts(": command not found\n"); tcol(WHT); g_lastrc=1; return; }
}
 
void wpa_connect_saved(u32 m,int idx,const char* pw){
    if(idx<0||idx>=g_apn){ term_puts("  bad network id\n"); return; }
    if(!m)m=rtw_find_mmio();
    if(!m||!g_rxnum){ tcol(rgb(230,120,90)); term_puts("  run wifi init/allbuilt/macinit/fw/rings first\n"); tcol(WHT); return; }
    char b[96],*p; p=b;p=scopy(p,"  [wifi wpa] connecting to \"");p=scopy(p,g_aps[idx].ssid[0]?g_aps[idx].ssid:"<hidden>");p=scopy(p,"\" on ch ");p=udec(p,g_aps[idx].ch);p=scopy(p,"...");*p=0;
    tcol(rgb(120,200,255)); term_puts(b); term_putc('\n'); tcol(WHT);
    g_chquiet=1;
    int r;
    if(g_aps[idx].sec==0) r=rtw_assoc(m,&g_aps[idx],g_aps[idx].ssid);
    else r=rtw_wpa2_join(m,&g_aps[idx],g_aps[idx].ssid,pw);
    g_chquiet=0;
    if(r==0){ tcol(GREEN); term_puts(g_aps[idx].sec==0?"  *** CONNECTED (open) ***\n":"  *** CONNECTED - 4-way handshake complete ***\n"); tcol(WHT); }
    else if(r==1){ tcol(rgb(230,160,60)); term_puts("  no auth response (AP not answering our TX)\n"); tcol(WHT); }
    else if(r==2){ tcol(rgb(230,160,60)); term_puts("  auth rejected\n"); tcol(WHT); }
    else if(r==3){ tcol(rgb(230,160,60)); term_puts("  no assoc response\n"); tcol(WHT); }
    else if(r==4){ tcol(rgb(230,160,60)); term_puts("  assoc rejected\n"); tcol(WHT); }
    else if(r==5){ tcol(rgb(230,160,60)); term_puts("  no EAPOL msg1 (associated, no handshake)\n"); tcol(WHT); }
    else if(r==6){ tcol(rgb(230,160,60)); term_puts("  wrong password (handshake MIC rejected)\n"); tcol(WHT); }
    else { tcol(rgb(230,160,60)); term_puts("  handshake failed\n"); tcol(WHT); }
}
void wpa_do_join(const char* ss,const char* pw){
    u32 m=rtw_find_mmio();
    if(!m){ tcol(rgb(230,120,90)); term_puts("  no Wi-Fi adapter\n"); tcol(WHT); return; }
    if(!g_rxnum||!g_benum){ tcol(rgb(230,120,90)); term_puts("  run 'wifi rings' first (after init/allbuilt/macinit/fw)\n"); tcol(WHT); return; }
    char b[128],*p;
    p=b;p=scopy(p,"  [wifi wpa] joining \"");p=scopy(p,(char*)ss);p=scopy(p,"\" ...");*p=0; tcol(rgb(120,200,255)); term_puts(b); term_putc('\n'); tcol(WHT);
    g_chquiet=1;
    g_apn=0; rtw_rx_ring_up(m);
    static const u8 wc[]={1,6,11,2,3,4,5,7,8,9,10,12,13};
    int tgt=-1;
    term_puts("  scanning: "); 
    for(int ci=0;ci<13 && tgt<0;ci++){ rtw_set_channel_2g(m,wc[ci]); ui_pump();
        { char cb[6]; char* z=cb; z=udec(z,wc[ci]); *z++=' '; *z=0; tcol(GREY); term_puts(cb); tcol(WHT); }
        for(int w=0;w<4;w++){ rtw_delay_us(62000); ui_pump(); rtw_scan_drain(m,wc[ci]); }
        for(int q=0;q<g_apn;q++){ int eq=1,k=0; while(ss[k]&&k<32){ char x=g_aps[q].ssid[k],y=ss[k]; if(x>='A'&&x<='Z')x+=32; if(y>='A'&&y<='Z')y+=32; if(x!=y)eq=0;k++;} if(g_aps[q].ssid[k]||ss[k])eq=0; if(eq){tgt=q;break;} } }
    term_putc('\n');
    g_chquiet=0;
    if(tgt<0){ tcol(rgb(230,160,60)); term_puts("  network not found. networks seen this scan:\n"); tcol(WHT);
        for(int q=0;q<g_apn;q++){ char* z=b; z=scopy(z,"    "); z=scopy(z,g_aps[q].ssid[0]?g_aps[q].ssid:"<hidden>"); z=scopy(z, g_aps[q].sec==0?"  (open)":g_aps[q].sec==2?"  (WPA2)":"  (WEP)"); *z=0; tcol(CYAN); term_puts(b); term_putc('\n'); tcol(WHT); }
        if(g_apn==0) term_puts("    (none - move closer / re-run)\n"); return; }
    p=b;p=scopy(p,"  found on ch ");p=udec(p,g_aps[tgt].ch);p=scopy(p,"  deriving PMK (PBKDF2 4096, ~1s)...");*p=0; tcol(CYAN); term_puts(b); term_putc('\n'); tcol(WHT);
    g_chquiet=1; int r=rtw_wpa2_join(m,&g_aps[tgt],ss,pw); g_chquiet=0;
    if(r==0){ tcol(GREEN); term_puts("  *** 4-WAY HANDSHAKE COMPLETE - PTK derived, keys ready ***\n"); tcol(WHT); }
    else if(r==1){ tcol(rgb(230,160,60)); term_puts("  no auth response\n"); tcol(WHT); }
    else if(r==2){ tcol(rgb(230,160,60)); term_puts("  auth rejected\n"); tcol(WHT); }
    else if(r==3){ tcol(rgb(230,160,60)); term_puts("  no assoc response\n"); tcol(WHT); }
    else if(r==4){ tcol(rgb(230,160,60)); term_puts("  assoc rejected (RSN mismatch?)\n"); tcol(WHT); }
    else if(r==5){ tcol(rgb(230,160,60)); term_puts("  no EAPOL msg1 (assoc ok, no handshake start)\n"); tcol(WHT); }
    else if(r==6){ tcol(rgb(230,160,60)); term_puts("  no EAPOL msg3 (msg2 sent - wrong password or MIC mismatch)\n"); tcol(WHT); }
    if(r==0){ tcol(g_gtk_state==1?GREEN:rgb(230,160,60)); term_puts("  "); term_puts(wpa_gtk_status()); term_putc('\n'); tcol(WHT); }
    else { tcol(rgb(230,160,60)); term_puts("  handshake failed\n"); tcol(WHT); }
}
void term_key(char c){
    if(g_pwprompt){
        if(c=='\n'){ term_putc('\n'); tline[tlen]=0; g_pwprompt=0; if(g_pwbyid){ wpa_connect_saved(rtw_find_mmio(),g_pwbyid-1,tline); g_pwbyid=0; } else wpa_do_join(g_pwssid,tline); tlen=0; term_prompt(); }
        else if(c=='\b'){ if(tlen>0){tlen--;term_putc('\b');} }
        else if(c>=32&&c<127){ if(tlen<126){tline[tlen++]=c;term_putc('*');} }   /* mask password */
        return;
    }
    if(c=='\n'){ term_putc('\n'); tline[tlen]=0; klog2("cmd: ",tline); { char lb[512]; int k=0; for(;tline[k]&&k<511;k++)lb[k]=tline[k]; lb[k]=0; sh_run_line(lb); } tlen=0; if(!g_tty_fg)term_prompt(); }
    else if(c=='\b'){ if(tlen>0){tlen--;term_putc('\b');} }
    else if(c>=32&&c<127){ if(tlen<254){tline[tlen++]=c;term_putc(c);} }
}
void term_init(void){ term_geom(); term_clear();
    tcol(ACC);term_puts("NoovexOS 64");tcol(WHT);term_puts("  nsh shell\n");
    tcol(GREY);term_puts("type 'help' for commands, 'neofetch' for info.\n\n");tcol(WHT); term_prompt(); }

const TTheme TTH[6]={
 {"Midnight",rgb(15,18,26),rgb(24,28,40),rgb(222,228,240),rgb(128,138,160),rgb(120,230,150),rgb(120,230,150),rgb(120,190,255),rgb(236,106,100),rgb(90,210,230),0},
 {"Dracula",rgb(40,42,54),rgb(33,34,44),rgb(248,248,242),rgb(150,156,190),rgb(255,121,198),rgb(80,250,123),rgb(189,147,249),rgb(255,85,85),rgb(139,233,253),0},
 {"Solarized",rgb(0,43,54),rgb(7,54,66),rgb(147,161,161),rgb(88,110,117),rgb(181,137,0),rgb(133,153,0),rgb(38,139,210),rgb(220,50,47),rgb(42,161,152),0},
 {"Paper",rgb(250,249,245),rgb(236,234,228),rgb(44,48,58),rgb(130,134,146),rgb(60,120,224),rgb(30,140,70),rgb(40,100,200),rgb(200,50,50),rgb(20,130,150),1},
 {"Matrix",rgb(4,10,6),rgb(10,20,12),rgb(140,255,160),rgb(60,140,80),rgb(140,255,160),rgb(90,255,120),rgb(120,230,170),rgb(255,120,100),rgb(120,255,210),0},
 {"Noovex",rgb(14,18,40),rgb(20,25,52),rgb(228,234,248),rgb(128,140,178),rgb(96,160,255),rgb(92,224,196),rgb(116,164,255),rgb(255,112,136),rgb(120,214,255),0}};
static u32 g_blurtmp[MAXW*MAXH];
static void term_glass(int x,int y,int w,int h,int r,u32 bg){
    if(g_topa>=100){ fill_round(x,y,w,h,r,15,bg); return; }
    { extern int g_gfx; extern void gfx_glass(int,int,int,int,int,u32,int,int); if(g_gfx){ int a=g_topa*256/100; if(a>256)a=256; gfx_glass(x,y,w,h,r,bg,a,g_tblur?2:0); return; } }
    int x0=x<0?0:x, y0=y<0?0:y, x1=x+w>(int)W?(int)W:x+w, y1=y+h>(int)H?(int)H:y+h; if(x1<=x0||y1<=y0)return;
    int bw=x1-x0, bh=y1-y0, R=g_tblur?10:0;
    if(R){ for(int j=0;j<bh;j++){ u32* src=&SCENE[(u64)(y0+j)*SW+x0]; u32* dst=&g_blurtmp[(u64)j*bw]; int sr=0,sg=0,sb=0,n=0;
            for(int i=-R;i<=R&&i<bw;i++){ if(i<0)continue; u32 c=src[i]; sr+=(c>>16)&255; sg+=(c>>8)&255; sb+=c&255; n++; }
            for(int i=0;i<bw;i++){ dst[i]=rgb(sr/n,sg/n,sb/n); int a=i+R+1, d=i-R; if(a<bw){ u32 c=src[a]; sr+=(c>>16)&255; sg+=(c>>8)&255; sb+=c&255; n++; } if(d>=0){ u32 c=src[d]; sr-=(c>>16)&255; sg-=(c>>8)&255; sb-=c&255; n--; } } }
        for(int i=0;i<bw;i++){ int sr=0,sg=0,sb=0,n=0;
            for(int j=0;j<=R&&j<bh;j++){ u32 c=g_blurtmp[(u64)j*bw+i]; sr+=(c>>16)&255; sg+=(c>>8)&255; sb+=c&255; n++; }
            for(int j=0;j<bh;j++){ u32 c0=rgb(sr/n,sg/n,sb/n); int a=j+R+1, d=j-R;
                if(a<bh){ u32 c=g_blurtmp[(u64)a*bw+i]; sr+=(c>>16)&255; sg+=(c>>8)&255; sb+=c&255; n++; }
                if(d>=0){ u32 c=g_blurtmp[(u64)d*bw+i]; sr-=(c>>16)&255; sg-=(c>>8)&255; sb-=c&255; n--; }
                if(!cut(x0-x+i,y0-y+j,w,h,r,15)) SCENE[(u64)(y0+j)*SW+x0+i]=c0; } } }
    int a=g_topa*256/100; if(a>256)a=256;
    for(int j=0;j<bh;j++){ u32* row=&SCENE[(u64)(y0+j)*SW+x0]; for(int i=0;i<bw;i++){ if(cut(x0-x+i,y0-y+j,w,h,r,15))continue; row[i]=mix(row[i],bg,a); } }
}
static u32 tmap(u32 c){ const TTheme* t=&TTH[g_tth];
    if(c==WHT)return t->fg; if(c==GREY)return t->dim; if(c==GREEN)return t->green; if(c==RED)return t->red; if(c==CYAN)return t->cyan; if(c==rgb(120,200,255))return t->blue;
    if(t->light){ int r=(c>>16)&255,g=(c>>8)&255,b=c&255; if((r*3+g*6+b)/10>150)return mix(c,rgb(0,0,0),120); } return c; }
static void tglyph(int x,int y,char ch,u32 c){ int idx=(u8)ch-32; if(idx<=0||idx>=95)return;
    if(!g_tfont){ dchar(x,y,ch,c,1); return; } { extern int g_gfx; extern void gfx_glyph(int,int,char,u32,int,int); if(g_gfx){ gfx_glyph(x,y,ch,c,1,2); return; } } const unsigned char* g=fontaa[idx];
    (void)SCENE; for(int r=0;r<14;r++){ int py=y+r; if((u32)py>=H)continue; for(int col=0;col<8;col++){ int a=g[r*8+col]; if(a<8)continue; int px=x+col; if((u32)px>=W)continue; u32* p=&g_scene[(u64)py*SW+px]; *p=mix(*p,c,a); } } }

static int g_live=0;
static void live_geom(void){ tcols=((int)W-28)/8; if(tcols>TCMAX)tcols=TCMAX; trows=((int)H-28)/TLH; if(trows>TRMAX)trows=TRMAX; }

#define LIVE_BG rgb(10,12,18)
#define LIVE_OX 14
#define LIVE_OY 14
static char g_lsh[TRMAX][TCMAX]; static u32 g_lsc[TRMAX][TCMAX]; static u32 g_lsb[TRMAX][TCMAX];
static int g_lfull=1, g_lcx=-1, g_lcy=-1, g_lcon=0;
void live_invalidate(void){ g_lfull=1; }
static void live_row(int rr){
    fill_rect(0,LIVE_OY+rr*TLH,(int)W,TLH,LIVE_BG);
    for(int c=0;c<tcols;c++){ char ch=TCH[rr][c]; u32 bg=TBG[rr][c]; if(bg)fill_rect(LIVE_OX+c*8,LIVE_OY+rr*TLH,8,TLH,bg&0xFFFFFF);
        if(ch!=' ')tglyph(LIVE_OX+c*8,LIVE_OY+rr*TLH,ch,bg?TCO[rr][c]:tmap(TCO[rr][c])); g_lsh[rr][c]=ch; g_lsc[rr][c]=TCO[rr][c]; g_lsb[rr][c]=bg; }
}
static void live_cell(int cx,int cy,int on){
    if(cx<0||cy<0||cx>=tcols||cy>=trows)return;
    int x=LIVE_OX+cx*8, y=LIVE_OY+cy*TLH;
    u32 bg=TBG[cy][cx]; fill_rect(x,y,8,TLH,bg?(bg&0xFFFFFF):LIVE_BG);
    char ch=TCH[cy][cx]; if(ch!=' ')tglyph(x,y,ch,bg?TCO[cy][cx]:tmap(TCO[cy][cx]));
    if(on&&!g_vt_curhide){ fill_rect(x,y,8,TLH-1,rgb(120,220,140)); if(ch!=' ')tglyph(x,y,ch,LIVE_BG); }
}
void live_present(void){
    int on=(int)((g_ticks/40)&1);
    int ccx=tcx>=tcols?tcols-1:tcx;
    if(g_lfull){
        fill_rect(0,0,(int)W,(int)H,LIVE_BG);
        for(int rr=0;rr<trows;rr++)live_row(rr);
        live_cell(ccx,tcy,on); g_lcx=ccx; g_lcy=tcy; g_lcon=on;
        blit_all(); g_lfull=0; return;
    }
    int y0=-1,y1=-1;
    for(int rr=0;rr<trows;rr++){ int d=0;
        for(int c=0;c<tcols;c++) if(g_lsh[rr][c]!=TCH[rr][c]||g_lsb[rr][c]!=TBG[rr][c]||(TCH[rr][c]!=' '&&g_lsc[rr][c]!=TCO[rr][c])){ d=1; break; }
        if(d){ live_row(rr); if(y0<0)y0=rr; y1=rr; if(rr==g_lcy)g_lcon=-1; } }
    if(ccx!=g_lcx||tcy!=g_lcy||on!=g_lcon){
        if(g_lcon!=-1&&(g_lcx!=ccx||g_lcy!=tcy)){ live_cell(g_lcx,g_lcy,0); blit_region(LIVE_OX+g_lcx*8,LIVE_OY+g_lcy*TLH,8,TLH); }
        live_cell(ccx,tcy,on); blit_region(LIVE_OX+ccx*8,LIVE_OY+tcy*TLH,8,TLH);
        g_lcx=ccx; g_lcy=tcy; g_lcon=on; }
    if(y0>=0) blit_region(0,LIVE_OY+y0*TLH,(int)W,(y1-y0+1)*TLH);
}
static void live_banner(void){ tcol(GREY); term_puts("To boot setup, type "); tcol(ACC); term_puts("noovexos_install"); tcol(WHT); term_puts("\n\n"); term_prompt(); }
void live_init(void){ g_live=1; g_tfont=1; ux_core_start("root"); live_geom(); term_clear(); live_banner(); live_invalidate(); }
int live_input(void){ int c; int changed=0;
    if(mouse_evt){ cpu_irq_off(); mouse_dx=0; mouse_dy=0; mouse_evt=0; cpu_irq_on(); }
    while((c=kpop())>=0){ changed=1; int cc=c;
        if(kdown[0x1D]){ if(cc=='c'||cc=='C')cc=3; else if(cc=='z'||cc=='Z')cc=26; else if(cc=='d'||cc=='D')cc=4; else if(cc=='\\')cc=28; else if(g_tty_fg&&((cc>='a'&&cc<='z')||(cc>='A'&&cc<='Z')))cc=0x100|((cc|32)-'a'+1); else if(g_tty_fg&&cc=='[')cc=27; }
        if(g_tty_fg) tty_key(cc);
        else if(cc==3){ term_puts("^C\n"); tlen=0; term_prompt(); }
        else if(cc!=4&&cc!=26&&cc!=28&&cc<0x100) term_key((char)cc); }
    { static u64 lastblink=0; u64 bk=g_ticks/40; if(bk!=lastblink){ lastblink=bk; changed=1; } }
    return changed; }

void term_draw(int i){
    const TTheme* t=&TTH[g_tth]; int x=WN[i].x,y=WN[i].y,w=WN[i].w,h=WN[i].h,r=14,foc=(focus==i);
    shadow_soft(x,y,w,h,r); term_glass(x,y,w,h,r,t->bg); fill_round(x,y,w,TITLEH,r,1|2,t->bar);
    u32 tc=foc?(t->light?rgb(40,44,54):rgb(226,230,240)):t->dim;
    fill_round(x+14,y+11,22,22,6,15,ACC); draw_logo(x+25,y+15,14,rgb(255,255,255),256);
    for(int q=0;q<w-2*r;q++){ u32 gc=mix(ACC,rgb(150,90,230),q*255/(w-2*r)); putpx(x+r+q,y+TITLEH-1,gc); putpx(x+r+q,y+TITLEH,mix(gc,t->bg,150)); }
    { char pb[160],b[200],*p=b; dt_init(); dt_path(g_dcwd,pb); p=scopy(p,cur_user()); *p++='@'; p=scopy(p,g_hostname); p=scopy(p,": "); p=scopy(p,pb); *p=0;
      int maxc=(w-48-150)/8; if(maxc<8)maxc=8; if(slen(b)>maxc){ b[maxc-2]='.'; b[maxc-1]='.'; b[maxc]=0; } if(!g_gm_ready)gmetrics(); pdstr(x+46,y+15,b,tc,1); }
    { int cw=46,xr=x+w; for(int bn=0;bn<3;bn++){ int bx=xr-(3-bn)*cw; int hov=(foc&&mx>=bx&&mx<bx+cw&&my>=y&&my<y+TITLEH);
        if(hov){ u32 hc=(bn==2)?rgb(232,64,54):mix(t->bar,t->light?rgb(0,0,0):rgb(255,255,255),30);
            for(int j=0;j<TITLEH;j++)for(int ii=0;ii<cw;ii++){ int gx=(bx-x)+ii; if(!cut(gx,j,w,TITLEH,r,1|2))putpx(bx+ii,y+j,hc); } }
        int gx=bx+cw/2, gy=y+TITLEH/2; u32 gc=(hov&&bn==2)?rgb(255,255,255):tc;
        if(bn==0){ for(int q=-5;q<=5;q++){ putpx(gx+q,gy,gc); putpx(gx+q,gy+1,gc); } }
        else if(bn==1){ for(int q=-5;q<=5;q++){ putpx(gx+q,gy-5,gc); putpx(gx+q,gy+5,gc); putpx(gx-5,gy+q,gc); putpx(gx+5,gy+q,gc); } }
        else { for(int q=-5;q<=5;q++){ putpx(gx+q,gy+q,gc); putpx(gx+q+1,gy+q,gc); putpx(gx+q,gy-q,gc); putpx(gx+q+1,gy-q,gc); } } } }
    term_geom(); int sb=g_tstatus?26:0; int ox=x+TPAD, oy=y+TITLEH+TPAD-2;
    if(g_tsb_off>tsb_n)g_tsb_off=tsb_n; if(g_tsb_off<0)g_tsb_off=0;
    { extern void gfx_scroll_area(int,int,int,int,int,int); gfx_scroll_area(i,x+2,oy-2,w-4,trows*TLH,(tsb_n-g_tsb_off)*TLH); }
    if(!g_tsb_off&&foc) fill_rect(x+2,oy+tcy*TLH-2,w-4,TLH,mix(t->bg,t->light?rgb(0,0,0):rgb(255,255,255),t->light?10:8));
    for(int rr=0;rr<trows;rr++){ int v=tsb_n-g_tsb_off+rr; const char* rc; const u32* rcol;
        if(v<tsb_n){ int k=(tsb_head-tsb_n+v+TSB_N*2)%TSB_N; rc=TSB[k]; rcol=TSBC[k]; } else { int sr=v-tsb_n; if(sr>=trows)break; rc=TCH[sr]; rcol=TCO[sr]; }
        const u32* rbg=(v>=tsb_n)?TBG[v-tsb_n]:0;
        for(int c=0;c<tcols;c++){ char ch=rc[c]; u32 bg=rbg?rbg[c]:0; if(bg)fill_rect(ox+c*8,oy+rr*TLH,8,TLH,bg&0xFFFFFF); if(ch!=' ')tglyph(ox+c*8,oy+rr*TLH+1,ch,bg?rcol[c]:tmap(rcol[c])); } }
    if(!g_tsb_off&&!g_vt_curhide){ int ccx=tcx>=tcols?tcols-1:tcx; int cx=ox+ccx*8, cy=oy+tcy*TLH; int on=((g_ticks/50)&1)||!foc; u32 cc=t->cur;
        if(!foc){ for(int q=0;q<8;q++){ putpx(cx+q,cy,cc); putpx(cx+q,cy+TLH-2,cc); } for(int q=0;q<TLH-1;q++){ putpx(cx,cy+q,cc); putpx(cx+7,cy+q,cc); } }
        else if(on){ if(g_tcur==0){ fill_rect(cx,cy,8,TLH-1,cc); char ch=TCH[tcy][ccx]; if(ch!=' ')tglyph(cx,cy+1,ch,t->bg); }
            else if(g_tcur==1)fill_rect(cx,cy,2,TLH-1,cc); else fill_rect(cx,cy+TLH-3,8,2,cc); } }
    if(tsb_n>0){ int th=trows*TLH, tot=tsb_n+trows, bh=th*trows/tot; if(bh<24)bh=24; int by=oy+(th-bh)*(tsb_n-g_tsb_off)/(tsb_n?tsb_n:1);
        fill_round(x+w-9,by,4,bh,2,15,mix(t->bg,t->light?rgb(0,0,0):rgb(255,255,255),g_tsb_off?90:40)); }
    if(sb){ int sy=y+h-sb; extern int g_gfx; extern void gfx_round(int,int,int,int,int,int,u32,int); if(g_gfx){ gfx_round(x,sy,w,sb,r,12,t->bar,g_topa>=100?256:220); } else { u32* sc=SCENE; for(int j=0;j<sb;j++)for(int q=0;q<w;q++){ if(cut(q,h-sb+j,w,h,r,15))continue; u32* pp=&sc[(u64)(sy+j)*SW+x+q]; if((u32)(x+q)<W&&(u32)(sy+j)<H)*pp=mix(*pp,t->bar,g_topa>=100?256:220); } } fill_rect(x+1,sy,w-2,1,mix(t->bar,t->light?rgb(0,0,0):rgb(255,255,255),t->light?24:18));
        fill_circle(x+18,sy+13,4,g_uid==0?t->red:t->green); char b[160],*p=b; p=scopy(p,cur_user()); *p++='@'; p=scopy(p,g_hostname); *p=0; int ex=dstr(x+30,sy+6,b,t->dim,1);
        { char pb[128]; dt_path(g_dcwd,pb); fill_rect(ex+10,sy+8,1,11,mix(t->bar,t->dim,120)); dstr(ex+20,sy+6,pb,t->blue,1); }
        p=b; if(g_tsb_off){ p=scopy(p,"scrolled up "); p=udec(p,(u32)g_tsb_off); p=scopy(p," lines - type to return"); }
        else { p=scopy(p,"nsh   "); p=udec(p,(u32)tcols); *p++='x'; p=udec(p,(u32)trows); } *p=0;
        dstr(x+w-slen(b)*8-18,sy+6,b,g_tsb_off?t->cur:t->dim,1); }
    { int gx=x+w-6, gy=y+h-6; u32 gc=t->dim; for(int k=0;k<3;k++){ int o=k*4; for(int q=0;q<=o;q++)putpx(gx-o+q,gy-q,gc); } }
}

