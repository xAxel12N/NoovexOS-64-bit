#include "include/nvx/base.h"
#include "include/nvx/lib/base/fmt.h"
#include "include/nvx/mem/heap.h"
#include "include/nvx/drivers/usb/uvc.h"
#include "include/nvx/fs/vfs/ramfs.h"
#include "include/nvx/fs/vfs/unixfs.h"
#include "include/nvx/core/proc/jobs.h"
#include "include/nvx/core/klog.h"
#include "include/nvx/core/auth/faceid.h"
#include "include/nvx/arch/x86_64/interrupts/interrupts.h"
#include "include/nvx/arch/x86_64/cpu/percpu.h"

extern volatile u64 g_ticks;
int jpeg_decode(const u8* buf,int len,u8* rgba,int rgbamax,int* ow,int* oh);

#define Q_MAGIC 0x3151464Eu
#define R_MAGIC 0x3152464Eu
#define FMAXW 640
#define FMAXH 640
enum { C_VERIFY=1, C_ADD=2, C_SAVE=3, C_RESET=4 };
enum { S_NOFACE=0, S_FACE=1, S_MATCH=2, S_NOMATCH=3, S_ADDED=4, S_SAVED=5, S_NOFILE=6, S_ERROR=7, S_SMALL=8, S_MANY=9 };

typedef struct { u32 magic, seq, cmd, w, h; char user[32]; u32 pad[3]; } fid_req;
typedef struct { u32 magic, seq, status; int dist, x, y, w, h, pts[10], count, faces; } fid_res;

int g_fid_state, g_fid_mode, g_fid_count, g_fid_dist=-1, g_fid_fails, g_fid_face[4], g_fid_fw, g_fid_fh, g_fid_seen;
char g_fid_msg[96]; const char* g_fid_hint;
static char fid_user[32];
static int fid_pid=-1, fid_cam_ours, fid_matches, fid_saving; static volatile int fid_inflight, fid_ready, fid_off, fid_total;
static u32 fid_seq, fid_last_frame; static int fid_nsent;
static u64 fid_t0, fid_tready, fid_tsent, fid_tframe;
static u8* fid_buf;
static fid_res fid_r; static int fid_rn;
#define FID_NFEED 16
static u32* fid_feeds[FID_NFEED]; static int fid_fw_[FID_NFEED], fid_fh_[FID_NFEED], fid_nfeed; static u32 fid_feed_seq;
#define fid_feed (fid_nfeed>0)

static void fid_scpy(char* d,const char* s,int cap){ int i=0; for(;s[i]&&i<cap-1;i++)d[i]=s[i]; d[i]=0; }
static void fid_path(const char* user,char* out){ char* p=out; p=scopy(p,"/etc/noovex/faceid/"); p=scopy(p,user); p=scopy(p,".face"); *p=0; }
static int fid_name_ok(const char* u){ if(!u||!u[0])return 0; for(int i=0;u[i];i++){ char c=u[i]; if(i>=31)return 0; if(!((c>='a'&&c<='z')||(c>='0'&&c<='9')||c=='-'||c=='_'))return 0; } return 1; }

int fid_has(const char* user){ if(!fid_name_ok(user))return 0; char p[80]; fid_path(user,p); int d=0; return ux_lookup(p,&d,1)>=0&&!d; }
int fid_delete(const char* user){ if(!fid_name_ok(user))return -1; char p[80]; fid_path(user,p); if(!fid_has(user))return 0; return ux_unlink(p)<0?-1:0; }
int fid_camera_present(void){ return fid_feed!=0||uvc_found(); }
int fid_active(void){ return g_fid_mode!=0; }

static int fid_pid_alive(int pid){ if(pid<=0)return 0; int j=jc_idx(pid); return j>=0&&g_jp[j].state!=S_ZOMB&&g_jp[j].state!=S_FREE; }
static int fid_alive(void){ return fid_pid_alive(fid_pid); }
static int fid_oldpid=-1, fid_free_later;

static int fid_spawn(void){
    int ji=jc_create("/usr/bin/nvx-faceid serve /dev/nvxface",0,0);
    if(ji<0){ klog("faceid: cannot start /usr/bin/nvx-faceid"); return -1; }
    struct jproc* jp=&g_jp[ji]; jp->uid=0; jp->gid=0;
    int nul=jc_newvf(1,-1,0), lg=jc_newvf(13,-1,1);
    if(nul>=0){ vf_unref(jp->fd[0]); jp->fd[0]=nul; }
    if(lg>=0){ vf_unref(jp->fd[1]); jp->fd[1]=lg; vf_unref(jp->fd[2]); jp->fd[2]=lg; g_vf[lg].ref++; }
    fid_pid=jp->pid; return 0; }

static void fid_stop_all(void){
    if(fid_pid>0&&fid_alive()){ jc_kill(fid_pid,9,0); fid_oldpid=fid_pid; }
    fid_pid=-1;
    if(fid_cam_ours&&uvc_state==2)uvc_stop();
    fid_cam_ours=0; fid_inflight=0; fid_ready=0; fid_off=0; fid_rn=0; fid_saving=0; }

void fid_end(void){ fid_stop_all(); g_fid_mode=0; g_fid_hint=0; fid_free_later=1; }

void fid_feed_set(u32* rgb,int w,int h){ if(!rgb){ for(int i=0;i<fid_nfeed;i++)kfree(fid_feeds[i]); fid_nfeed=0; fid_feed_seq++; return; } if(fid_nfeed>=FID_NFEED){ kfree(rgb); return; } fid_feeds[fid_nfeed]=rgb; fid_fw_[fid_nfeed]=w; fid_fh_[fid_nfeed]=h; fid_nfeed++; fid_feed_seq++; }

int fid_begin(int mode,const char* user){
    fid_stop_all();
    g_fid_mode=0; g_fid_count=0; g_fid_dist=-1; g_fid_seen=0; g_fid_hint=0; fid_matches=0; g_fid_face[2]=0; g_fid_msg[0]=0;
    if(!fid_name_ok(user)){ g_fid_state=FID_ERROR; scopy(g_fid_msg,"This account name cannot use Face ID"); return -1; }
    if(mode==FID_VERIFY&&!fid_has(user)){ g_fid_state=FID_ERROR; scopy(g_fid_msg,"Face ID is not set up for this account"); return -1; }
    if(!fid_camera_present()){ g_fid_state=FID_ERROR; scopy(g_fid_msg,"No camera found"); return -1; }
    fid_free_later=0;
    if(!fid_buf){ fid_buf=(u8*)kmalloc(sizeof(fid_req)+FMAXW*FMAXH*3); if(!fid_buf){ g_fid_state=FID_ERROR; scopy(g_fid_msg,"Not enough memory for Face ID"); return -1; } }
    fid_scpy(fid_user,user,32);
    if(!fid_feed&&uvc_state!=2){ if(uvc_start(0)<0&&uvc_state!=2){ g_fid_state=FID_ERROR; fid_scpy(g_fid_msg,uvc_msg[0]?uvc_msg:"The camera did not start",96); return -1; } fid_cam_ours=1; }
    if(fid_spawn()<0){ fid_stop_all(); g_fid_state=FID_ERROR; scopy(g_fid_msg,"The face engine is missing"); return -1; }
    g_fid_mode=mode; g_fid_state=FID_STARTING; fid_t0=g_ticks; fid_tready=0; fid_last_frame=0xFFFFFFFFu; fid_nsent=0;
    klog2(mode==FID_ENROLL?"faceid: set up started for ":"faceid: sign-in check started for ",user);
    return 0; }

static void fid_put_req(u32 cmd,int w,int h){
    fid_req* q=(fid_req*)fid_buf; for(u32 i=0;i<sizeof(*q);i++)((u8*)q)[i]=0;
    fid_nsent++; q->magic=Q_MAGIC; q->seq=++fid_seq; q->cmd=cmd; q->w=(u32)w; q->h=(u32)h; fid_scpy(q->user,fid_user,32);
    fid_total=(int)sizeof(*q)+w*h*3; fid_off=0; fid_tsent=g_ticks; __sync_synchronize(); fid_ready=1; }

static const u32* fid_src(int* w,int* h,u32* seq){
    if(fid_feed){ u32 t=(u32)(g_ticks/30); int i=(int)(t%(u32)fid_nfeed); *w=fid_fw_[i]; *h=fid_fh_[i]; *seq=t+fid_feed_seq*100000u; return fid_feeds[i]; }
    if(uvc_state!=2||!uvc_rgb||uvc_w<=0||uvc_h<=0)return 0;
    *w=uvc_w; *h=uvc_h; *seq=uvc_seq; return uvc_rgb; }

const u32* fid_preview(int* w,int* h){ u32 s; return fid_src(w,h,&s); }

static void fid_send_frame(void){
    int sw,sh; u32 sq; const u32* src=fid_src(&sw,&sh,&sq); if(!src||sq==fid_last_frame)return;
    fid_last_frame=sq;
    int ow=sw, oh=sh; if(ow>FMAXW){ oh=oh*FMAXW/ow; ow=FMAXW; } if(oh>FMAXH){ ow=ow*FMAXH/oh; oh=FMAXH; } if(ow<16||oh<16)return;
    u8* d=fid_buf+sizeof(fid_req);
    for(int y=0;y<oh;y++){ const u32* r=src+(u64)(y*sh/oh)*sw; for(int x=0;x<ow;x++){ u32 c=r[x*sw/ow]; *d++=(u8)(c>>16); *d++=(u8)(c>>8); *d++=(u8)c; } }
    g_fid_fw=ow; g_fid_fh=oh;
    fid_put_req(g_fid_mode==FID_ENROLL?C_ADD:C_VERIFY,ow,oh); }

static void fid_finish(int st,const char* msg){ g_fid_state=st; if(msg)fid_scpy(g_fid_msg,msg,96); if(st!=FID_SUCCESS&&msg)klog2("faceid: stopped: ",msg); fid_stop_all(); }

static void fid_result(const fid_res* r){
    fid_inflight=0; fid_tframe=g_ticks-fid_tsent;
    if(!fid_tready){ char b[80],*p=b; p=scopy(p,"faceid: first answer after "); p=udec(p,(u32)(fid_tframe/100)); p=scopy(p," s"); *p=0; klog(b); }
    if(!fid_tready){ fid_tready=g_ticks; if(g_fid_state==FID_STARTING)g_fid_state=FID_RUNNING; }
    if(r->status!=S_NOFACE&&r->w>0){ g_fid_face[0]=r->x; g_fid_face[1]=r->y; g_fid_face[2]=r->w; g_fid_face[3]=r->h; g_fid_seen=1; } else g_fid_face[2]=0;
    { const char* h=r->status==S_MANY?"Only one face in view, please":r->status==S_SMALL?"Move a little closer":0; if(h&&h!=g_fid_hint)klog2("faceid: hint: ",h); g_fid_hint=h; }
    if(r->status==S_ERROR){ fid_finish(FID_ERROR,"The face engine reported an error"); return; }
    if(g_fid_mode==FID_VERIFY){
        g_fid_dist=r->dist;
        if(r->status==S_NOFILE){ fid_finish(FID_ERROR,"Face ID is not set up for this account"); return; }
        if(r->status==S_MATCH){ if(++fid_matches>=2){ klog2("faceid: recognized ",fid_user); fid_finish(FID_SUCCESS,"Recognized with Face ID"); } return; }
        fid_matches=0; return; }
    if(r->status==S_SAVED){ g_fid_count=r->count; klog2("faceid: set up saved for ",fid_user); fid_finish(FID_SUCCESS,"Face ID is set up"); return; }
    if(fid_saving){ fid_finish(FID_ERROR,"Face ID could not be saved"); return; }
    if(r->status==S_ADDED||r->status==S_FACE||r->status==S_SMALL||r->status==S_MANY)g_fid_count=r->count;
    if(g_fid_count>=FID_TARGET){ fid_saving=1; fid_put_req(C_SAVE,0,0); } }

int fid_tick(void){
    if(!g_fid_mode){ if(fid_free_later&&!fid_pid_alive(fid_oldpid)){ fid_free_later=0; fid_oldpid=-1; if(fid_buf){ kfree(fid_buf); fid_buf=0; } } return 0; }
    static u64 fid_lastdraw; int ch=0; if(g_ticks-fid_lastdraw>=25){ fid_lastdraw=g_ticks; ch=1; }
    if(!fid_feed&&uvc_state==2){ uvc_poll(); if(uvc_kind==1&&uvc_jnew&&uvc_jpg&&uvc_rgb){ int w=0,h=0; int r=jpeg_decode(uvc_jpg,(int)uvc_jlen,(u8*)uvc_rgb,1920*1200*4,&w,&h);
        if(r==0&&w==uvc_w&&h==uvc_h){ u8* b=(u8*)uvc_rgb; u32 n=(u32)w*(u32)h; for(u32 i=0;i<n;i++){ u8* p=b+i*4; uvc_rgb[i]=((u32)p[0]<<16)|((u32)p[1]<<8)|(u32)p[2]; } uvc_seq++; } uvc_jnew=0; } }
    if(g_fid_state!=FID_STARTING&&g_fid_state!=FID_RUNNING)return ch;
    if(!fid_alive()){ fid_finish(FID_ERROR,"The face engine stopped"); return 1; }
    u64 now=g_ticks;
    if(g_fid_state==FID_STARTING&&now-fid_t0>18000){ fid_finish(FID_ERROR,"The face engine did not start"); return 1; }
    if(g_fid_state==FID_RUNNING){
        u64 lim=g_fid_mode==FID_ENROLL?6000+12*fid_tframe:800+3*fid_tframe;
        if(now-fid_tready>lim){
            if(g_fid_mode==FID_VERIFY){ g_fid_fails++; klog2("faceid: not recognized: ",fid_user); fid_finish(FID_FAILED,g_fid_hint?g_fid_hint:g_fid_seen?"Face not recognized":"No face seen - look at the camera"); }
            else fid_finish(FID_FAILED,g_fid_hint?g_fid_hint:"Could not see your face clearly - try again with more light");
            return 1; } }
    if(!fid_ready&&!fid_inflight&&!fid_saving)fid_send_frame();
    return ch; }

int fid_dev_ready(void){ return fid_ready; }
int fid_dev_read(char* buf,int len){
    if(!fid_ready||!fid_buf){ return g_js_running?JS_BLOCK:0; }
    int n=fid_total-fid_off; if(n>len)n=len; if(n<=0)return 0;
    for(int i=0;i<n;i++)buf[i]=(char)fid_buf[fid_off+i];
    fid_off+=n; if(fid_off>=fid_total){ fid_ready=0; fid_inflight=1; if(fid_nsent<=2)klog("faceid: picture taken by the face engine"); }
    return n; }
int fid_dev_write(const char* buf,int len){
    for(int i=0;i<len;i++){ ((u8*)&fid_r)[fid_rn++]=(u8)buf[i];
        if(fid_rn==4&&fid_r.magic!=R_MAGIC){ u8* b=(u8*)&fid_r; b[0]=b[1]; b[1]=b[2]; b[2]=b[3]; fid_rn=3; continue; }
        if(fid_rn==(int)sizeof(fid_r)){ fid_rn=0; if(fid_r.magic==R_MAGIC&&fid_r.seq==fid_seq&&g_fid_mode)fid_result(&fid_r); else klog("faceid: an answer did not match the request"); } }
    return len; }
