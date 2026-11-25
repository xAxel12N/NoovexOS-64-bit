#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include "include/nvx/base.h"
#include "include/nvx/arch/x86_64/cpu/percpu.h"
static struct pcpu hp={.uid=1000};
#undef PCPU
#define PCPU (&hp)
#include "lib/fprint/fpstore.c"
FsFile FS[FS_N]; DNode DT[DN_MAX];
static char paths[64][64]; static int npath;
static int slot(const char* p,int mk){ for(int i=0;i<npath;i++)if(!strcmp(paths[i],p))return FS[i].used?i:(mk?i:-1); if(!mk||npath>=64)return -1; strcpy(paths[npath],p); return npath++; }
int ux_lookup(const char* p,int* isd,int f){ (void)f; *isd=0; return slot(p,0); }
char* fsd(int k){ return FS[k].data; }
int ux_write_text(const char* p,const char* b,int n,unsigned m){ int k=slot(p,1); if(k<0)return -1; free(FS[k].data); FS[k].data=malloc((size_t)n+1); memcpy(FS[k].data,b,(size_t)n); FS[k].data[n]=0; FS[k].len=n; FS[k].used=1; FS[k].uid=0; FS[k].mode=m; const char* s=strrchr(p,'/'); strcpy(FS[k].name,s+1); return 0; }
int ux_unlink(const char* p){ int k=slot(p,0); if(k<0)return -1; FS[k].used=0; return 0; }
void dt_init(void){} int dt_child(int p,const char* n){ (void)p;(void)n; return 1; } int dt_mkdir_at(int p,const char* n){ (void)p;(void)n; return 1; }
int acct_get(const char* n,acct_info* o){ (void)n;(void)o; return 0; }
static const char* me="alice"; const char* cur_user(void){ return me; }
static char tout[4096]; void term_puts(const char* s){ if(strlen(tout)+strlen(s)<sizeof tout)strcat(tout,s); }
char* scopy(char* d,const char* s){ while(*s)*d++=*s++; *d=0; return d; }
char* udec(char* p,u32 v){ char t[12]; int n=0; do{ t[n++]=(char)('0'+v%10); v/=10; }while(v); while(n)*p++=t[--n]; *p=0; return p; }
int seq(const char* a,const char* b){ return !strcmp(a,b); }
static u64 now_ms; u64 hr_ns(void){ return now_ms*1000000ull; }
static int moc, mbeg, mstep[64], nms, mpos, mpct, ends, slices; static char muid[64];
int fprint_find(u16* v,u16* d){ (void)v;(void)d; return 3; } int fprint_supported(int k){ return k==3; } int fprint_is_moc(int k){ (void)k; return moc; }
int fprint_usb_moc(int op,const char* uid,u8* tid,void (*prog)(int),char* msg,int cap){ (void)op;(void)uid;(void)tid;(void)prog;(void)msg;(void)cap; return -1; }
static int script[32],nscript,pos,caps,last_ms;
void fprint_usb_end(void){ ends++; }
int fprint_moc_enroll_begin(const char* uid,char* m,int c){ strcpy(muid,uid); snprintf(m,(size_t)c,"init failed"); return mbeg; }
int fprint_moc_enroll_step(u8* tid,int* pct,char* m,int c){ int s=mpos<nms?mstep[mpos++]:0; if(s>=100){ *pct=s-100; return 0; } if(s==1)memset(tid,0x77,32); snprintf(m,(size_t)c,s==1?"enrolled":"storage full"); return s; }
int fprint_usb_slice(fp_image_t* img,int ms,char* msg,int cap);
int fprint_usb_capture(fp_image_t* img,int ms,char* msg,int cap){ (void)msg;(void)cap; caps++; last_ms=ms; int s=pos<nscript?script[pos++]:0; if(s<=0)return s; img->w=64; img->h=64; memset(img->px,s,sizeof img->px); return 1; }
int fp_extract(const unsigned char* img,int w,int h,fp_tpl_t* t){ (void)w;(void)h; t->n=img[0]==9?4:20; for(int i=0;i<t->n;i++){ t->m[i].x=(short)img[0]; t->m[i].y=(short)i; t->m[i].a=0; t->m[i].t=0; } return t->n; }
int fp_match(const fp_tpl_t* a,const fp_tpl_t* b){ return a->m[0].x==b->m[0].x?80:0; }
int fp_match_ok(int s){ return s>=FP_THRESH; }
int fp_match_set(const fp_tpl_t* t,int nt,const fp_tpl_t* p){ int s=0; for(int i=0;i<nt;i++)s+=fp_match(&t[i],p); return s; }
int fp_set_ok(int s){ return s>=FP_FUSE; }
int fprint_usb_slice(fp_image_t* img,int ms,char* msg,int cap){ slices++; return fprint_usb_capture(img,ms,msg,cap); }
static void mset(const int* v,int n){ memcpy(mstep,v,sizeof(int)*(size_t)n); nms=n; mpos=0; }
static void set(const int* v,int n){ memcpy(script,v,sizeof(int)*(size_t)n); nscript=n; pos=0; }
static int run(int max){ int i=0; while(fprint_enrol_busy()&&i<max){ fprint_enrol_poll(); now_ms+=300; i++; } return i; }
int main(void){
    char m[128];
    assert(!fprint_login_on()&&!(fprint_ui_state()&8));
    assert(fprint_ui(4,m,sizeof m)==0&&strstr(m,"Enrol"));
    set((const int[]){0,0,5,7,5,9,5},7); caps=0;
    assert(fprint_ui(1,m,sizeof m)==1&&fprint_enrol_busy()&&caps==0&&(fprint_ui_state()&16));
    assert(fprint_ui(2,m,sizeof m)==0&&strstr(m,"progress"));
    fprint_enrol_poll(); assert(caps==1&&last_ms==FPE_SLICE_MS&&fpe_got==0&&fprint_enrol_busy()&&fps_fms==6000);
    fprint_enrol_poll(); assert(fpe_got==0&&fprint_enrol_progress(m,sizeof m)==0);
    fprint_enrol_poll(); assert(fpe_got==1&&fprint_enrol_progress(m,sizeof m)==33&&strstr(m,"2/3"));
    fprint_enrol_poll(); assert(fpe_got==1&&fpe_bad==1&&strstr(fpe_msg,"same finger"));
    fprint_enrol_poll(); assert(fpe_got==2);
    fprint_enrol_poll(); assert(fpe_got==2&&fpe_bad==2&&strstr(fpe_msg,"features"));
    fprint_enrol_poll(); assert(!fprint_enrol_busy()&&fprint_enrol_progress(m,sizeof m)==100&&fprint_enrolled("alice")&&(fprint_ui_state()&4)&&!(fprint_ui_state()&8));
    assert(fprint_ui(4,m,sizeof m)==1&&fprint_login_on());
    int k=slot("/etc/fprint/.login",0); assert(k>=0&&!strcmp(FS[k].data,"on\n")&&FS[k].mode==0100600u);
    g_fprint_login=0; fps_lgload=0; assert(fprint_login_on()==1);
    FS[k].uid=1000; g_fprint_login=0; fps_lgload=0; assert(fprint_login_on()==0); FS[k].uid=0; fps_lgload=0; assert(fprint_login_on()==1);
    set((const int[]){5},1); assert(fprint_verify_user("alice")==1); set((const int[]){6},1); assert(fprint_verify_user("alice")==0);
    assert(fprint_ui(4,m,sizeof m)==1&&!fprint_login_on()&&!strcmp(FS[k].data,"off\n")); g_fprint_login=1; fps_lgload=0; assert(fprint_login_on()==0);
    assert(fprint_ui(4,m,sizeof m)==1&&fprint_login_on());
    assert(fprint_ui(3,m,sizeof m)==1&&!fprint_login_on()&&!strcmp(FS[k].data,"off\n")&&!fprint_enrolled("alice"));
    nscript=0; assert(fprint_ui(1,m,sizeof m)==1); int n=run(1000); assert(!fprint_enrol_busy()&&n<=FPE_TOTAL_MS/300+2&&strstr(fpe_msg,"timed out")&&!fprint_enrolled("alice"));
    set((const int[]){9,9,9,9,9,9,9,9},8); assert(fprint_ui(1,m,sizeof m)==1); run(100); assert(!fprint_enrol_busy()&&fpe_bad==FP_NTPL+4&&strstr(fpe_msg,"failed"));
    nscript=0; assert(fprint_ui(1,m,sizeof m)==1&&fprint_ui(5,m,sizeof m)==1&&!fprint_enrol_busy()); fprint_enrol_poll(); assert(!fprint_enrol_busy());
    set((const int[]){5},1); assert(fprint_ui(1,m,sizeof m)==1); me="bob"; fprint_enrol_poll(); assert(!fprint_enrol_busy()&&strstr(fpe_msg,"user")); me="alice";
    me="guest"; assert(fprint_ui(1,m,sizeof m)==0&&!fprint_enrol_busy()); me="alice";
    assert(slices>0&&slices==caps-2&&ends>0);
    moc=1; ux_unlink("/etc/fprint/alice"); fps_login_set(0); mbeg=-1; assert(fprint_ui(1,m,sizeof m)==0&&!fprint_enrol_busy()&&strstr(m,"init failed"));
    mbeg=0; mset((const int[]){0,0,125,0,150,175,1},7); now_ms=0; assert(fprint_ui(1,m,sizeof m)==1&&fprint_enrol_busy()&&!strcmp(muid,"nvx:alice")&&mpos==0);
    assert(fprint_ui(2,m,sizeof m)==0&&strstr(m,"progress"));
    assert(fprint_enrol_poll()==0&&mpos==1&&fprint_enrol_progress(m,sizeof m)==0);
    fprint_enrol_poll(); assert(fprint_enrol_poll()==1&&fprint_enrol_progress(m,sizeof m)==25&&strstr(m,"25%"));
    fprint_enrol_poll(); fprint_enrol_poll(); fprint_enrol_poll(); assert(fprint_enrol_progress(0,0)==75&&fprint_enrol_busy());
    int e0=ends; assert(fprint_enrol_poll()==1&&!fprint_enrol_busy()&&fprint_enrol_progress(m,sizeof m)==100&&fprint_enrolled("alice")&&fprint_login_on()&&ends==e0+1);
    { u8 t[32]; assert(fps_mload("alice",t)&&t[0]==0x77&&t[31]==0x77); }
    ux_unlink("/etc/fprint/alice"); fps_login_set(0); mset((const int[]){110,-2},2); assert(fprint_ui(1,m,sizeof m)==1); run(10); assert(!fprint_enrol_busy()&&strstr(fpe_msg,"storage full")&&!fprint_enrolled("alice")&&!fprint_login_on());
    nms=0; now_ms=0; assert(fprint_ui(1,m,sizeof m)==1); n=run(100000); assert(!fprint_enrol_busy()&&strstr(fpe_msg,"timed out")&&n<=(FPE_TOTAL_MS+5000)/300+2&&!fprint_enrolled("alice"));
    nms=0; assert(fprint_ui(1,m,sizeof m)==1); e0=ends; assert(fprint_ui(5,m,sizeof m)==1&&!fprint_enrol_busy()&&ends==e0+1); int p0=mpos; fprint_enrol_poll(); assert(mpos==p0);
    assert(fprint_ui(1,m,sizeof m)==1); me="bob"; fprint_enrol_poll(); assert(!fprint_enrol_busy()&&strstr(fpe_msg,"user")); me="alice";
    ux_unlink("/etc/fprint/alice"); fps_login_set(0); mbeg=-1; tout[0]=0; assert(fprint_shell("enroll","")==1&&!fprint_enrol_busy()&&strstr(tout,"init failed"));
    mbeg=0; mset((const int[]){0,140,0,1},4); now_ms=0; tout[0]=0; assert(fprint_shell("enroll","")==1&&fprint_enrol_busy()&&mpos==0&&strstr(tout,"progress is shown"));
    tout[0]=0; assert(fprint_shell("enroll","")==1&&strstr(tout,"already running")&&mpos==0);
    fprint_enrol_poll(); fprint_enrol_poll(); assert(strstr(tout,"40% done"));
    fprint_enrol_poll(); e0=ends; fprint_enrol_poll(); assert(!fprint_enrol_busy()&&ends==e0+1&&strstr(tout,"fprint: enrolled on the reader: alice")&&fprint_enrolled("alice")&&!fprint_login_on());
    ux_unlink("/etc/fprint/alice"); nms=0; tout[0]=0; assert(fprint_shell("enroll","")==1&&fprint_enrol_busy()); assert(fprint_shell("cancel","")==1&&!fprint_enrol_busy()&&strstr(tout,"enrolment cancelled"));
    tout[0]=0; assert(fprint_shell("cancel","")==1&&strstr(tout,"no enrolment"));
    nms=0; now_ms=0; tout[0]=0; assert(fprint_shell("enroll","")==1); n=run(100000); assert(!fprint_enrol_busy()&&strstr(tout,"fprint: Enrolment timed out")&&!fprint_enrolled("alice"));
    printf("fprint store: all passed\n"); return 0;
}
