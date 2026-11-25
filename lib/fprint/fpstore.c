#include "include/nvx/base.h"
#include "include/nvx/arch/x86_64/cpu/percpu.h"
#include "include/nvx/lib/base/fmt.h"
#include "include/nvx/fs/vfs/ramfs.h"
#include "include/nvx/fs/vfs/dirtree.h"
#include "include/nvx/fs/vfs/unixfs.h"
#include "include/nvx/shell/terminal.h"
#include "include/nvx/sys/native/syscall.h"
#include "include/nvx/core/auth/accounts.h"
#include "include/nvx/lib/fprint/fprint.h"
#include "include/nvx/arch/x86_64/time/hrtimer.h"

int (*fprint_capture)(unsigned char* img,int cap,int* w,int* h);
#if __has_include("include/nvx/drivers/input/fingerprint/fprint.h")
#include "include/nvx/drivers/input/fingerprint/fprint.h"
static fp_image_t fps_cap;
static char fps_cmsg[96];
static int fps_fms=6000,fps_crc,fps_sl;
static int fps_reader(void){ if(fprint_capture)return 1; u16 v=0,d=0; return fprint_supported(fprint_find(&v,&d)); }
static int fps_moc(void){ if(fprint_capture)return 0; u16 v=0,d=0; return fprint_is_moc(fprint_find(&v,&d)); }
static int fps_mocop(int op,const char* uid,u8* tid){ return fprint_usb_moc(op,uid,tid,0,fps_cmsg,(int)sizeof fps_cmsg); }
static int fps_mbeg(const char* uid){ return fprint_moc_enroll_begin(uid,fps_cmsg,(int)sizeof fps_cmsg); }
static int fps_mstep(u8* tid,int* pct){ return fprint_moc_enroll_step(tid,pct,fps_cmsg,(int)sizeof fps_cmsg); }
static void fps_uend(void){ fprint_usb_end(); }
static int fps_usb(unsigned char* img,int cap,int* w,int* h){
    fps_cmsg[0]=0; fps_crc=(fps_sl?fprint_usb_slice:fprint_usb_capture)(&fps_cap,fps_fms,fps_cmsg,(int)sizeof fps_cmsg); if(fps_crc<=0){ return -1; }
    int n=fps_cap.w*fps_cap.h; if(fps_cap.w<=0||fps_cap.h<=0||n>cap){ return -1; } for(int i=0;i<n;i++)img[i]=fps_cap.px[i]; *w=fps_cap.w; *h=fps_cap.h; return 0;
}
#else
static char fps_cmsg[96];
static int fps_fms=6000,fps_crc,fps_sl;
static int fps_reader(void){ return fprint_capture!=0; }
static int fps_mbeg(const char* uid){ (void)uid; return -1; }
static int fps_mstep(u8* tid,int* pct){ (void)tid; (void)pct; return -1; }
static void fps_uend(void){}
static int fps_usb(unsigned char* img,int cap,int* w,int* h){ (void)img; (void)cap; (void)w; (void)h; return -1; }
static int fps_moc(void){ return 0; }
static int fps_mocop(int op,const char* uid,u8* tid){ (void)op; (void)uid; (void)tid; return -1; }
#endif
static unsigned char fps_img[FP_MAXPIX];
static fp_tpl_t fps_t[FP_NTPL];
static char fps_buf[FP_NTPL*FP_MAXM*20+64];

static int fps_name_ok(const char* u){ if(!u||!u[0])return 0; for(int i=0;u[i];i++){ char c=u[i]; if(i>31||!((c>='a'&&c<='z')||(c>='A'&&c<='Z')||(c>='0'&&c<='9')||c=='_'||c=='-'||c=='.')||(i==0&&c=='.'))return 0; } return 1; }
static void fps_path(char* p,const char* u){ p=scopy(p,"/etc/fprint/"); p=scopy(p,u); *p=0; }
static char* fps_int(char* p,int v){ if(v<0){ *p++='-'; v=-v; } return udec(p,(u32)v); }
static int fps_rd(const char** s){ const char* p=*s; while(*p==' '||*p=='\n')p++; int neg=0; if(*p=='-'){ neg=1; p++; } if(*p<'0'||*p>'9'){ *s=p; return -99999; } int v=0; while(*p>='0'&&*p<='9'&&v<100000)v=v*10+(*p++-'0'); *s=p; return neg?-v:v; }
static int fps_load(const char* u){
    char path[64]; fps_path(path,u); int isd=0, k=ux_lookup(path,&isd,1); if(k<0||isd)return 0;
    int n=FS[k].len; if(n>(int)sizeof(fps_buf)-1)n=(int)sizeof(fps_buf)-1; const char* d=fsd(k); for(int i=0;i<n;i++)fps_buf[i]=d[i]; fps_buf[n]=0;
    const char* s=fps_buf; if(n<7||fps_buf[0]!='n'||fps_buf[1]!='v'||fps_buf[2]!='x'||fps_buf[3]!='f'||fps_buf[4]!='p'||fps_buf[5]!='1')return 0; s+=6;
    int nt=0; while(nt<FP_NTPL){ while(*s==' '||*s=='\n')s++; if(*s!='T')break; s++; int m=fps_rd(&s); if(m<0||m>FP_MAXM)break; fps_t[nt].n=0;
        for(int i=0;i<m;i++){ int x=fps_rd(&s), y=fps_rd(&s), a=fps_rd(&s), t=fps_rd(&s); if(x<0||y<0||x>4096||y>4096||a<0||a>255||t<0||t>1)return nt; fp_min_t* q=&fps_t[nt].m[fps_t[nt].n++]; q->x=(short)x; q->y=(short)y; q->a=(unsigned char)a; q->t=(unsigned char)t; }
        nt++; }
    return nt;
}
static int fps_save(const char* u,int nt){
    dt_init(); int e=dt_child(0,"etc"); if(e<0)return -1; int d=dt_child(e,"fprint"); if(d<0){ d=dt_mkdir_at(e,"fprint"); if(d<0)return -1; DT[d].mode=0040700u; DT[d].uid=0; DT[d].gid=0; }
    char* p=fps_buf; p=scopy(p,"nvxfp1\n");
    for(int t=0;t<nt;t++){ *p++='T'; p=fps_int(p,fps_t[t].n); *p++='\n';
        for(int i=0;i<fps_t[t].n;i++){ fp_min_t* m=&fps_t[t].m[i]; p=fps_int(p,m->x); *p++=' '; p=fps_int(p,m->y); *p++=' '; p=fps_int(p,m->a); *p++=' '; p=fps_int(p,m->t); *p++='\n'; } }
    *p=0; char path[64]; fps_path(path,u); return ux_write_text(path,fps_buf,(int)(p-fps_buf),0100600u)<0?-1:0;
}
static int fps_grab(fp_tpl_t* t){
    if(!fps_reader()){ return -1; } int w=0,h=0; if((fprint_capture?fprint_capture:fps_usb)(fps_img,FP_MAXPIX,&w,&h)<0)return -2;
    if(w<32||h<32||w>FP_MAXW||w*h>FP_MAXPIX){ return -2; } return fp_extract(fps_img,w,h,t)<8?-3:0;
}
static char fps_uid[40];
static const char* fps_chipid(const char* u){ char* p=scopy(fps_uid,"nvx:"); p=scopy(p,u); *p=0; return fps_uid; }
static int fps_hx(char c){ return c>='0'&&c<='9'?c-'0':c>='a'&&c<='f'?c-'a'+10:-1; }
static int fps_mload(const char* u,u8* tid){
    char path[64]; fps_path(path,u); int isd=0, k=ux_lookup(path,&isd,1); if(k<0||isd||FS[k].len<7)return 0; const char* d=fsd(k);
    if(d[0]!='n'||d[1]!='v'||d[2]!='x'||d[3]!='f'||d[4]!='p'||d[5]!='m')return 0;
    if(tid){ for(int i=0;i<32;i++)tid[i]=0; if(FS[k].len>=7+64) for(int i=0;i<32;i++){ int a=fps_hx(d[7+2*i]), b=fps_hx(d[8+2*i]); if(a<0||b<0)break; tid[i]=(u8)(a<<4|b); } }
    return 1; }
static int fps_msave(const char* u,const u8* tid){
    dt_init(); int e=dt_child(0,"etc"); if(e<0)return -1; int d=dt_child(e,"fprint"); if(d<0){ d=dt_mkdir_at(e,"fprint"); if(d<0)return -1; DT[d].mode=0040700u; DT[d].uid=0; DT[d].gid=0; }
    static const char hx[]="0123456789abcdef"; char* p=fps_buf; p=scopy(p,"nvxfpm\n"); for(int i=0;i<32;i++){ *p++=hx[tid[i]>>4]; *p++=hx[tid[i]&15]; } *p++='\n'; *p=0;
    char path[64]; fps_path(path,u); return ux_write_text(path,fps_buf,(int)(p-fps_buf),0100600u)<0?-1:0; }
int fprint_reader(void){ return fps_reader(); }
int fprint_enrolled(const char* u){ if(!fps_name_ok(u))return 0; if(fps_moc())return fps_mload(u,0); return fps_load(u)>0; }
int fprint_verify_user(const char* u){
    if(!fps_reader()||!fps_name_ok(u)){ return -1; }
    if(fps_moc()){ if(!fps_mload(u,0))return -1; int r=fps_mocop(FPM_VERIFY,fps_chipid(u),0); return r==1?1:r==-2?-1:0; }
    int nt=fps_load(u); if(nt<=0)return -1;
    static fp_tpl_t probe; if(fps_grab(&probe)<0)return 0;
    return fp_set_ok(fp_match_set(fps_t,nt,&probe));
}
static void fps_msg(const char* a,const char* b){ term_puts(a); if(b)term_puts(b); term_puts("\n"); }
static const char* fps_err(int r){ return r==-1?"no fingerprint reader":r==-2?"the reader did not return an image (timed out)":"too few features - press the whole fingertip flat on the sensor"; }
volatile int g_fprint_login=0;
static void fps_cp(char* o,int cap,const char* a,const char* b){ int n=0; for(;a&&*a&&n<cap-1;a++)o[n++]=*a; for(;b&&*b&&n<cap-1;b++)o[n++]=*b; if(cap>0)o[n]=0; }
static int fpe_st,fpe_got,fpe_bad,fpe_moc,fpe_pct,fpe_sh; static u64 fpe_t0; static char fpe_msg[96],fpe_user[33],fpe_who[33]; static u8 fpe_tid[32];
static void fpe_cpu(char* o,const char* u){ int q=0; while(u[q]&&q<32){ o[q]=u[q]; q++; } o[q]=0; }
static int fpe_mstart(const char* u,int sh){
    fpe_cpu(fpe_user,u); fpe_cpu(fpe_who,cur_user()); fpe_got=0; fpe_bad=0; fpe_pct=0; fpe_t0=hr_ns()/1000000ull; fpe_moc=1; fpe_sh=sh;
    for(int i=0;i<32;i++)fpe_tid[i]=0;
    if(fps_mbeg(fps_chipid(u))<0)return -1;
    fpe_st=1; fps_cp(fpe_msg,(int)sizeof fpe_msg,"Touch the sensor repeatedly until it says done",0); return 0; }
static int fps_lgload;
#define FPS_LOGIN "/etc/fprint/.login"
int fprint_login_on(void){
    if(!fps_lgload){ int isd=0,k=ux_lookup(FPS_LOGIN,&isd,1); if(k>=0&&!isd){ const char* d=fsd(k); fps_lgload=1; g_fprint_login=FS[k].len>=2&&FS[k].uid==0&&d[0]=='o'&&d[1]=='n'; } }
    return g_fprint_login; }
static int fps_login_set(int v){ g_fprint_login=v?1:0; fps_lgload=1;
    dt_init(); int e=dt_child(0,"etc"); if(e<0)return -1; int d=dt_child(e,"fprint"); if(d<0){ d=dt_mkdir_at(e,"fprint"); if(d<0)return -1; DT[d].mode=0040700u; DT[d].uid=0; DT[d].gid=0; }
    return ux_write_text(FPS_LOGIN,v?"on\n":"off\n",v?3:4,0100600u)<0?-1:0; }
static int fps_may_enable(void){ return g_uid==0||fprint_enrolled(cur_user()); }
int fprint_shell(const char* sub,const char* user){
    if(sub&&seq(sub,"login")){ int r=0; fprint_login_on(); if(user&&seq(user,"on")&&!fps_may_enable()){ fps_msg("fprint: enrol your own finger first (or run as root) to turn fingerprint sign-in on",0); return 1; } if(user&&(seq(user,"on")||seq(user,"off")))r=fps_login_set(seq(user,"on"));
        fps_msg(g_fprint_login?(r<0?"fprint: fingerprint sign-in is ON until restart (could not save it)":"fprint: fingerprint sign-in is ON (password still works)"):"fprint: fingerprint sign-in is OFF",0); return 1; }
    if(sub&&seq(sub,"cancel")){ int b=fprint_enrol_busy(); fprint_enrol_cancel(); fps_msg(b?"fprint: enrolment cancelled":"fprint: no enrolment is running",0); return 1; }
    if(!sub||!sub[0]||(!seq(sub,"enroll")&&!seq(sub,"verify")&&!seq(sub,"delete")&&!seq(sub,"list")))return 0;
    const char* me=cur_user(); const char* u=user&&user[0]?user:me;
    if(seq(sub,"list")){ int any=0; dt_init(); int e=dt_child(0,"etc"), d=e<0?-1:dt_child(e,"fprint");
        if(d>=0)for(int i=0;i<FS_N;i++)if(FS[i].used&&FS[i].dir==d&&FS[i].name[0]!='.'){ fps_msg("enrolled: ",FS[i].name); any=1; }
        if(!any){ fps_msg("fprint: no enrolled fingers",0); } return 1; }
    if(!fps_name_ok(u)||acct_get(u,&(acct_info){0})<0){ fps_msg("fprint: no such user: ",u); return 1; }
    if(g_uid!=0&&!seq((char*)u,(char*)me)){ fps_msg("fprint: only root may manage another user's fingerprints",0); return 1; }
    if(seq(sub,"delete")){ u8 tid[32]; if(fps_moc()&&fps_mload(u,tid)&&fps_mocop(FPM_DELETE,fps_chipid(u),tid)<=0)fps_msg("fprint: the reader did not delete it: ",fps_cmsg); char path[64]; fps_path(path,u); fps_msg(ux_unlink(path)<0?"fprint: nothing enrolled for ":"fprint: deleted fingerprint of ",u); return 1; }
    if(!fps_reader()){ fps_msg("fprint: no supported fingerprint reader found (see fprint status)",0); return 1; }
    if(seq(sub,"verify")){ int r=fprint_verify_user(u); fps_msg(r<0?"fprint: nothing enrolled for ":r?"fprint: MATCH for ":"fprint: no match for ",u); return 1; }
    if(fprint_enrol_busy()){ fps_msg("fprint: an enrolment is already running (fprint cancel stops it)",0); return 1; }
    if(fps_moc()){ if(fpe_mstart(u,1)<0){ fps_msg("fprint: ",fps_cmsg); return 1; }
        fps_msg("Touch the sensor repeatedly; progress is shown here (fprint cancel stops it)",0); return 1; }
    int got=0, tries=0;
    while(got<FP_NTPL&&tries<FP_NTPL+4){ tries++; char b[64],*p=b; p=scopy(p,"Touch the sensor ("); p=udec(p,(u32)(got+1)); p=scopy(p,"/"); p=udec(p,FP_NTPL); p=scopy(p,")..."); *p=0; fps_msg(b,0);
        int r=fps_grab(&fps_t[got]); if(r<0){ fps_msg("  ",r==-2&&fps_cmsg[0]?fps_cmsg:fps_err(r)); if(r==-1)return 1; continue; }
        if(got&&fp_match_ok(fp_match(&fps_t[0],&fps_t[got]))==0){ fps_msg("  that did not look like the same finger - try again",0); continue; }
        got++; }
    if(got<FP_NTPL){ fps_msg("fprint: enrollment failed",0); return 1; }
    fps_msg(fps_save(u,got)<0?"fprint: could not save the template for ":"fprint: enrolled ",u); return 1;
}
#define FPE_SLICE_MS 250
#define FPE_TOTAL_MS 60000
static int fpe_end(int st,const char* a,const char* b){ fpe_st=st; fps_cp(fpe_msg,(int)sizeof fpe_msg,a,b); fps_uend(); if(fpe_sh){ fpe_sh=0; fps_msg("fprint: ",fpe_msg); } return 1; }
int fprint_ui_state(void){ const char* u=cur_user(); int r=fps_reader(); return (r?1:0)|(r&&fps_moc()?2:0)|(fprint_enrolled(u)?4:0)|(fprint_login_on()?8:0)|(fprint_enrol_busy()?16:0); }
int fprint_ui(int op,char* msg,int cap){
    const char* u=cur_user(); fps_cmsg[0]=0;
    if(!fps_name_ok(u)||seq((char*)u,"guest")){ fps_cp(msg,cap,"The guest account cannot use fingerprints",0); return 0; }
    if(op==5){ fprint_enrol_cancel(); fps_cp(msg,cap,"Enrolment cancelled",0); return 1; }
    if(fprint_enrol_busy()){ fps_cp(msg,cap,"Enrolment is in progress",0); return 0; }
    if(op==4){ int st=fprint_ui_state(); if(!g_fprint_login&&!fps_may_enable()){ fps_cp(msg,cap,"Enrol your own finger first (or sign in as root) to turn fingerprint sign-in on",0); return 0; } int sv=fps_login_set(!g_fprint_login); fps_cp(msg,cap,g_fprint_login?(sv<0?"Fingerprint sign-in is on until restart (could not save the setting)":st&2?"Fingerprint sign-in is on (matched on the reader)":"Fingerprint sign-in is on; your password still works"):"Fingerprint sign-in is off",0); return 1; }
    if(op==3){ u8 tid[32]; if(fps_moc()&&fps_mload(u,tid)&&fps_mocop(FPM_DELETE,fps_chipid(u),tid)<=0){ fps_cp(msg,cap,"The reader did not delete it: ",fps_cmsg); return 0; } char path[64]; fps_path(path,u); int ok=ux_unlink(path)>=0; if(ok)fps_login_set(0); fps_cp(msg,cap,ok?"Fingerprint removed":"No fingerprint was enrolled",0); return ok; }
    if(!fps_reader()){ fps_cp(msg,cap,"No supported fingerprint reader found",0); return 0; }
    if(op==2){ int r=fprint_verify_user(u); fps_cp(msg,cap,r<0?"No fingerprint enrolled":r?"Match - this is your finger":"No match",0); return r>0; }
    if(op!=1)return 0;
    if(fps_moc()){ if(fpe_mstart(u,0)<0){ fps_cp(msg,cap,"Enrolment failed: ",fps_cmsg); return 0; } fps_cp(msg,cap,fpe_msg,0); return 1; }
    fpe_cpu(fpe_user,u); fpe_cpu(fpe_who,u); fpe_got=0; fpe_bad=0; fpe_pct=0; fpe_t0=hr_ns()/1000000ull; fpe_moc=0; fpe_sh=0;
    fpe_st=1; fps_cp(fpe_msg,(int)sizeof fpe_msg,"Touch the sensor (1/3)",0);
    fps_cp(msg,cap,"Touch the sensor three times with the same finger",0); return 1;
}
int fprint_enrol_busy(void){ return fpe_st==1; }
void fprint_enrol_cancel(void){ if(fpe_st==1)fpe_end(3,"Enrolment cancelled",0); }
int fprint_enrol_progress(char* msg,int cap){ if(msg)fps_cp(msg,cap,fpe_msg,0); return fpe_st==1?(fpe_moc?fpe_pct:fpe_got*100/FP_NTPL):fpe_st==2?100:-1; }
static int fpe_mpoll(void){
    int pct=fpe_pct, r=fps_mstep(fpe_tid,&pct);
    if(!r){ if(pct!=fpe_pct){ fpe_pct=pct<0?0:pct>99?99:pct; char b[48],*p=b; p=scopy(p,"Lift and touch again ("); p=udec(p,(u32)fpe_pct); p=scopy(p,"% done)"); *p=0; fps_cp(fpe_msg,(int)sizeof fpe_msg,b,0); if(fpe_sh)fps_msg("  ",fpe_msg); return 1; } return 0; }
    if(r!=1)return fpe_end(3,"Enrolment failed: ",fps_cmsg);
    if(fpe_sh){ if(fps_msave(fpe_user,fpe_tid)<0)return fpe_end(3,"could not save the record for ",fpe_user); return fpe_end(2,"enrolled on the reader: ",fpe_user); }
    if(fps_msave(fpe_user,fpe_tid)<0)return fpe_end(3,"Could not save the fingerprint record",0);
    fps_login_set(1); return fpe_end(2,"Fingerprint enrolled on the reader; fingerprint sign-in is on",0);
}
int fprint_enrol_poll(void){
    if(fpe_st!=1)return 0;
    const char* u=cur_user(); if(!seq((char*)u,fpe_who))return fpe_end(3,"Enrolment stopped: the user changed",0);
    u64 now=hr_ns()/1000000ull; if(now-fpe_t0>FPE_TOTAL_MS+(fpe_moc?5000:0))return fpe_end(3,"Enrolment timed out - press Enrol to try again",0);
    if(fpe_moc)return fpe_mpoll();
    fps_fms=FPE_SLICE_MS; fps_crc=-1; fps_sl=1; int r=fps_grab(&fps_t[fpe_got]); fps_sl=0; fps_fms=6000;
    if(r==-1)return fpe_end(3,"The fingerprint reader is gone",0);
    if(r==-2&&fps_crc==0)return 0;
    if(r<0||(fpe_got&&!fp_match_ok(fp_match(&fps_t[0],&fps_t[fpe_got])))){ if(++fpe_bad>=FP_NTPL+4)return fpe_end(3,"Enrolment failed: ",fps_cmsg[0]&&r<0?fps_cmsg:"press the whole fingertip on the sensor, the same finger each time");
        fps_cp(fpe_msg,(int)sizeof fpe_msg,r==-3?"Too few features - press the whole fingertip flat, then lift":r<0?"No image - lift the finger and touch again":"That did not look like the same finger - try again",0); return 1; }
    fpe_got++;
    if(fpe_got<FP_NTPL){ char b[48],*p=b; p=scopy(p,"Good - lift and touch again ("); p=udec(p,(u32)(fpe_got+1)); p=scopy(p,"/"); p=udec(p,FP_NTPL); p=scopy(p,")"); *p=0; fps_cp(fpe_msg,(int)sizeof fpe_msg,b,0); return 1; }
    if(fps_save(fpe_user,fpe_got)<0)return fpe_end(3,"Could not save the fingerprint",0);
    return fpe_end(2,"Fingerprint enrolled; turn on fingerprint sign-in to use it",0);
}
