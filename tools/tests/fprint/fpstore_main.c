#include <stdio.h>
#include <string.h>
#include "include/nvx/base.h"
#include "include/nvx/arch/x86_64/cpu/percpu.h"
static struct pcpu hp;
#undef PCPU
#define PCPU (&hp)
#include "lib/fprint/fpstore.c"
FsFile FS[FS_N]; DNode DT[DN_MAX];
static char fdata[4][128]; static int nf;
static const char* g_me="alice"; static int g_kind=1, g_moc=1, g_mocr;
static char g_mocuid[64];
int seq(const char* a,const char* b){ return strcmp(a,b)==0; }
char* scopy(char* d,const char* s){ while(*s)*d++=*s++; *d=0; return d; }
char* udec(char* p,u32 v){ return p+sprintf(p,"%u",v); }
const char* cur_user(void){ return g_me; }
int acct_get(const char* n,acct_info* o){ (void)o; return seq(n,"alice")||seq(n,"bob")?0:-1; }
void dt_init(void){} int dt_child(int p,const char* n){ (void)p; (void)n; return 1; } int dt_mkdir_at(int p,const char* n){ (void)p; (void)n; return 1; }
char* fsd(int k){ return FS[k].data; }
int ux_lookup(const char* p,int* isd,int f){ (void)f; *isd=0; for(int i=0;i<nf;i++)if(seq(FS[i].name,p))return i; return -1; }
int ux_unlink(const char* p){ for(int i=0;i<nf;i++)if(seq(FS[i].name,p)){ FS[i].name[0]=0; return 0; } return -1; }
int ux_write_text(const char* p,const char* b,int l,unsigned m){ (void)m; int i=ux_lookup(p,&(int){0},0); if(i<0){ i=nf++; strcpy(FS[i].name,p); FS[i].data=fdata[i]; } memcpy(FS[i].data,b,(size_t)l); FS[i].len=l; return 0; }
static char out[512]; void term_puts(const char* s){ strncat(out,s,sizeof out-strlen(out)-1); }
int fprint_find(u16* v,u16* d){ *v=0x27c6; *d=0x63ac; return g_kind; }
int fprint_supported(int k){ return k!=0; } int fprint_is_moc(int k){ (void)k; return g_moc; }
int fprint_usb_capture(fp_image_t* img,int ms,char* m,int c){ (void)img; (void)ms; (void)m; (void)c; return 0; }
int fprint_usb_moc(int op,const char* uid,u8* tid,void (*pr)(int),char* m,int c){ (void)pr; (void)m; (void)c; strcpy(g_mocuid,uid); if(op==FPM_ENROLL&&tid)memset(tid,0x5a,32); return op==FPM_VERIFY?g_mocr:1; }
int fprint_usb_slice(fp_image_t* img,int ms,char* m,int c){ (void)img; (void)ms; (void)m; (void)c; return 0; } void fprint_usb_end(void){}
int fprint_moc_enroll_begin(const char* uid,char* m,int c){ (void)m; (void)c; strcpy(g_mocuid,uid); return 0; }
int fprint_moc_enroll_step(u8* tid,int* pct,char* m,int c){ (void)m; (void)c; *pct=100; memset(tid,0x5a,32); return 1; }
int fp_extract(const unsigned char* im,int w,int h,fp_tpl_t* t){ (void)im; (void)w; (void)h; t->n=0; return 0; }
int fp_match(const fp_tpl_t* a,const fp_tpl_t* b){ (void)a; (void)b; return 100; }
int fp_match_ok(int s){ return s>=FP_THRESH; }
int fp_match_set(const fp_tpl_t* t,int nt,const fp_tpl_t* p){ (void)t; (void)nt; (void)p; return 0; } int fp_set_ok(int s){ (void)s; return 0; } u64 hr_ns(void){ return 0; }
static int fails;
#define CK(c) do{ if(!(c)){ printf("FAIL %d: %s\n",__LINE__,#c); fails++; } }while(0)
int main(void){
    hp.uid=1000; out[0]=0;
    fprint_shell("login","on"); CK(g_fprint_login==0); CK(strstr(out,"enrol your own finger")!=0);
    hp.uid=0; fprint_shell("login","on"); CK(g_fprint_login==1); fprint_shell("login","off"); CK(g_fprint_login==0);
    hp.uid=1000; out[0]=0; fprint_shell("enroll",0); CK(fprint_enrol_busy()); CK(!fprint_enrolled("alice")); fprint_enrol_poll(); CK(!fprint_enrol_busy()); CK(fprint_enrolled("alice")); CK(g_fprint_login==0); CK(strstr(out,"enrolled on the reader: alice")!=0); CK(!fprint_enrolled("bob")); CK(seq(g_mocuid,"nvx:alice"));
    fprint_shell("login","on"); CK(g_fprint_login==1); fprint_shell("login","off");
    g_me="bob"; fprint_shell("login","on"); CK(g_fprint_login==0); g_me="alice";
    { char m[160]; g_me="bob"; hp.uid=1001; CK(fprint_ui(4,m,sizeof m)==0); CK(g_fprint_login==0); CK(strstr(m,"own finger")!=0);
      g_me="root"; hp.uid=0; CK(fprint_ui(4,m,sizeof m)==1); CK(g_fprint_login==1); CK(fprint_ui(4,m,sizeof m)==1); CK(g_fprint_login==0);
      g_me="alice"; hp.uid=1000; CK(fprint_ui(4,m,sizeof m)==1); CK(g_fprint_login==1);
      g_me="bob"; hp.uid=1001; CK(fprint_ui(4,m,sizeof m)==1); CK(g_fprint_login==0); g_me="alice"; hp.uid=1000; }
    out[0]=0; fprint_shell("enroll","bob"); CK(!fprint_enrolled("bob")); CK(strstr(out,"only root")!=0);
    g_mocr=1; CK(fprint_verify_user("alice")==1); CK(seq(g_mocuid,"nvx:alice"));
    CK(fprint_verify_user("bob")==-1);
    g_mocr=0; CK(fprint_verify_user("alice")==0);
    g_mocr=-1; CK(fprint_verify_user("alice")==0);
    g_mocr=-3; CK(fprint_verify_user("alice")==0);
    g_mocr=-2; CK(fprint_verify_user("alice")==-1);
    CK(fprint_verify_user("../etc/x")==-1); CK(fprint_verify_user("")==-1);
    g_kind=0; g_mocr=1; CK(fprint_verify_user("alice")==-1);
    if(fails){ printf("FPSTORE: %d failures\n",fails); return 1; }
    puts("fpstore: all passed"); return 0; }
