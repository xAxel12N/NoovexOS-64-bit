#include "sys/linux/v4l2_abi.h"
#include "sys/linux/v4l2_q.h"
#include "include/nvx/drivers/usb/uvc.h"
#include "include/nvx/drivers/usb/uvc_ext.h"
#include "include/nvx/arch/x86_64/time/hrtimer.h"
#include "include/nvx/core/sysfeat.h"
#define LX_V4L 124
int jpeg_decode(const u8* buf,int len,u8* rgba,int rgbamax,int* ow,int* oh);
static long lx_sock_wait(struct sysregs* r,int n,int nb);
static const unsigned v4c_cid[UVC_C_N]={V4_CID_BASE+0,V4_CID_BASE+1,V4_CID_BASE+2,V4_CID_BASE+0x1B,V4_CID_BASE+0x10,V4_CID_BASE+3,V4_CID_BASE+0x13,V4_CID_BASE+0x1C,V4_CID_BASE+0x0C,V4_CID_BASE+0x1A,V4_CID_BASE+0x18,V4_CID_CAM_BASE+1,V4_CID_CAM_BASE+2,V4_CID_CAM_BASE+0x0C,V4_CID_CAM_BASE+0x0A};
static int v4c_by_cid(unsigned cid){ for(int i=0;i<UVC_C_N;i++) if(v4c_cid[i]==cid)return i; return -1; }
static int v4c_type(int c){ return c==UVC_C_WB_AUTO||c==UVC_C_FOCUS_AUTO?V4_CTRL_TYPE_BOOLEAN:(c==UVC_C_EXPOSURE_AUTO||c==UVC_C_PLF)?V4_CTRL_TYPE_MENU:V4_CTRL_TYPE_INTEGER; }
static int v4c_info(int c,int* mn,int* mx,int* df){ if(c<0||c>=UVC_C_N||!uvc_ctrl_avail(c)||!uvc_ctrl_range(c,mn,mx,df))return 0; if(c==UVC_C_EXPOSURE_AUTO){ *mn=1; *mx=3; *df=*df?3:1; } return 1; }
static int v4c_get(int c,int* v){ if(!uvc_ctrl_get(c,v))return -1; if(c==UVC_C_EXPOSURE_AUTO)*v=*v?3:1; return 0; }
static int v4c_set(int c,int v){ if(c==UVC_C_EXPOSURE_AUTO){ if(v!=1&&v!=3)return -1; v=v==3; } return uvc_ctrl_set(c,v)?0:-1; }
static int v4_start(int kind){ return uvc_start(kind==1)<0?-1:0; }
static struct v4q g_v4q; static u64* g_v4fr[V4Q_MAX]; static int g_v4np, g_v4w=640, g_v4h=480, g_v4mine; static u32 g_v4pix=V4_PIX_YUYV, g_v4lseq, g_v4lfr; static u64 g_v4tlast; static u8* g_v4stage;
static int g_v4cur=0, g_v4sv=0;
static struct { struct v4q q; u64* fr[V4Q_MAX]; int np, w, h, mine; u32 pix, lseq, lfr; u64 tlast; u8* stage; int used; } g_v4d[UVC_NCAM];
static void v4_swap(int d){
    if(d==g_v4cur)return;
    __builtin_memcpy(&g_v4d[g_v4cur].q,&g_v4q,sizeof g_v4q); __builtin_memcpy(g_v4d[g_v4cur].fr,g_v4fr,sizeof g_v4fr); g_v4d[g_v4cur].np=g_v4np; g_v4d[g_v4cur].w=g_v4w; g_v4d[g_v4cur].h=g_v4h; g_v4d[g_v4cur].mine=g_v4mine; g_v4d[g_v4cur].pix=g_v4pix; g_v4d[g_v4cur].lseq=g_v4lseq; g_v4d[g_v4cur].lfr=g_v4lfr; g_v4d[g_v4cur].tlast=g_v4tlast; g_v4d[g_v4cur].stage=g_v4stage; g_v4d[g_v4cur].used=1;
    if(!g_v4d[d].used){ u8* z=(u8*)&g_v4d[d]; for(unsigned long k=0;k<sizeof g_v4d[d];k++)z[k]=0; g_v4d[d].w=640; g_v4d[d].h=480; g_v4d[d].pix=V4_PIX_YUYV; g_v4d[d].used=1; }
    __builtin_memcpy(&g_v4q,&g_v4d[d].q,sizeof g_v4q); __builtin_memcpy(g_v4fr,g_v4d[d].fr,sizeof g_v4fr); g_v4np=g_v4d[d].np; g_v4w=g_v4d[d].w; g_v4h=g_v4d[d].h; g_v4mine=g_v4d[d].mine; g_v4pix=g_v4d[d].pix; g_v4lseq=g_v4d[d].lseq; g_v4lfr=g_v4d[d].lfr; g_v4tlast=g_v4d[d].tlast; g_v4stage=g_v4d[d].stage; g_v4cur=d; }
static void v4_enter(int d){ if(d<0||d>=UVC_NCAM)d=0; v4_swap(d); g_v4sv=uvc_selected(); uvc_select(d); }
static void v4_leave(void){ uvc_select(g_v4sv); }
static int v4_devof(const char* p){ return lx_streq(p,"/dev/video0")?0:lx_streq(p,"/dev/video1")?1:-1; }
static int v4_pix_ok(u32 pix,int k){ return pix==V4_PIX_MJPG?k==1:pix==V4_PIX_H264?0:1; }
static int v4_size(u32 pix,int idx,int* w,int* h,u32* di){
    int c=0, n=uvc_nfr();
    if(pix==V4_PIX_H264){ for(int i=0;i<uvx_h264_n();i++){ int fw,fh,dup=0; u32 d; if(!uvx_h264_info(i,&fw,&fh,&d))continue; for(int j=0;j<i;j++){ int w2,h2; u32 d2; if(uvx_h264_info(j,&w2,&h2,&d2)&&w2==fw&&h2==fh)dup=1; } if(dup)continue; if(c==idx){ *w=fw; *h=fh; if(di)*di=d; return 1; } c++; } return 0; }
    for(int i=0;i<n;i++){ int kd,fw,fh; u32 d; if(!uvc_frame_info(i,&kd,&fw,&fh,&d)||!v4_pix_ok(pix,kd)||fw<=0||fh<=0)continue; if(pix!=V4_PIX_MJPG&&(fw>1920||fh>1200))continue;
        int dup=0; for(int j=0;j<i;j++){ int k2,w2,h2; u32 d2; if(uvc_frame_info(j,&k2,&w2,&h2,&d2)&&v4_pix_ok(pix,k2)&&w2==fw&&h2==fh){ dup=1; break; } } if(dup)continue;
        if(c==idx){ *w=fw; *h=fh; if(di)*di=d; return 1; } c++; }
    return 0; }
static int v4_nfmt(int idx,u32* pix){
    static const u32 F[4]={V4_PIX_YUYV,V4_PIX_MJPG,V4_PIX_BGR4,V4_PIX_H264}; int c=0;
    for(int i=0;i<4;i++){ int w,h; if(!v4_size(F[i],0,&w,&h,0))continue; if(c==idx){ *pix=F[i]; return 1; } c++; } return 0; }
static int v4_fmt_known(u32 pix){ u32 p; for(int i=0;v4_nfmt(i,&p);i++) if(p==pix)return 1; return 0; }
static void v4_nearest(u32 pix,int* w,int* h){ int bw=0,bh=0; long bd=-1; int fw,fh; for(int i=0;v4_size(pix,i,&fw,&fh,0);i++){ long d=(long)(fw>*w?fw-*w:*w-fw)+(long)(fh>*h?fh-*h:*h-fh); if(bd<0||d<bd){ bd=d; bw=fw; bh=fh; } } if(bd>=0){ *w=bw; *h=bh; } }
static int v4_open(const char* p,int flags){
    int d=v4_devof(p); if(d<0)return -9999;
    if(!sysfeat_get(SYSF_V4L2)||sysfeat_state(SYSF_V4L2)==SYSF_ST_FAILED)return -LX_ENODEV;
    if(!uvc_cam_ok(d))return -LX_ENOENT; v4_enter(d); int nf=uvc_nfr(); v4_leave(); if(nf<=0)return -LX_ENOENT;
    int vi=jc_newvf(LX_V4L,d,flags&~0x80000); if(vi<0)return -LX_EMFILE;
    int q=0; for(;p[q]&&q<127;q++)g_vf[vi].vpath[q]=p[q]; g_vf[vi].vpath[q]=0;
    int fd=lx_fd_install(vi,(flags&0x80000)!=0,0); if(fd<0)vf_unref(vi); return fd; }
static int v4_stat(const char* p,u32* o,u64* rd){
    int d=v4_devof(p); if(d<0||!uvc_cam_ok(d))return 0;
    for(int i=0;i<10;i++)o[i]=0; o[0]=0020660u; o[2]=44+(u32)d; o[3]=1; o[5]=0x51000000u; o[6]=o[7]=o[8]=1760000000u; *rd=(81u<<8)|(u32)d; return 1; }
static int v4_list(int n,char* name){ int k=0; for(int d=0;d<UVC_NCAM;d++){ if(!uvc_cam_ok(d))continue; if(k++==n){ const char* s="video"; int q=0; for(;s[q];q++)name[q]=s[q]; name[q++]=(char)('0'+d); name[q]=0; return 1; } } return 0; }
static void v4_free(void){ for(int i=0;i<V4Q_MAX;i++){ if(g_v4fr[i]){ for(int k=0;k<g_v4np;k++) if(g_v4fr[i][k])pframe_free(g_v4fr[i][k]); kfree(g_v4fr[i]); g_v4fr[i]=0; } } g_v4np=0; if(g_v4stage){ kfree(g_v4stage); g_v4stage=0; } }
static void v4l_close1(int vi){ if(g_v4mine!=vi+1)return; v4q_streamoff(&g_v4q); v4q_reqbufs(&g_v4q,0,0); v4_free(); g_v4mine=0; }
static int v4_alloc(int n,u32 size){
    v4_free(); int np=(int)(v4q_pgsize(size)>>12);
    for(int i=0;i<n;i++){ g_v4fr[i]=(u64*)kmalloc(sizeof(u64)*(unsigned long)np); if(!g_v4fr[i]){ v4_free(); return -LX_ENOMEM; } for(int k=0;k<np;k++)g_v4fr[i][k]=0; g_v4np=np;
        for(int k=0;k<np;k++){ u64 f=pframe_alloc(); if(!f){ v4_free(); return -LX_ENOMEM; } lx_zero((u8*)f,4096); g_v4fr[i][k]=f; } }
    g_v4stage=(u8*)kmalloc(size); if(!g_v4stage){ v4_free(); return -LX_ENOMEM; }
    g_v4np=np; return 0; }
static int v4_map_frames1(u64 off,u64 len,u64** fr,int* np){ int i=v4q_by_offset(&g_v4q,off,len); if(i<0||!g_v4fr[i])return -1; *fr=g_v4fr[i]; *np=g_v4np; return i; }
static void v4_copy_out(int i,const u8* s,u32 n){ for(u32 o=0;o<n;o+=4096){ u32 c=n-o<4096?n-o:4096; u8* d=(u8*)g_v4fr[i][o>>12]; for(u32 b=0;b<c;b++)d[b]=s[o+b]; } }
static void v4_pump(void){
    if(!g_v4q.streaming||uvc_state!=2)return;
    if(g_v4pix==V4_PIX_MJPG||g_v4pix==V4_PIX_H264){
        if(uvc_kind!=(g_v4pix==V4_PIX_H264?8:1)||!uvc_jpg||!uvc_jlen||uvc_frames==g_v4lfr)return; int i=v4q_take(&g_v4q); if(i<0)return; g_v4lfr=uvc_frames;
        u32 n=uvc_jlen; if(n>g_v4q.size)n=g_v4q.size; v4_copy_out(i,uvc_jpg,n); g_v4tlast=hr_ns(); v4q_done(&g_v4q,i,n,g_v4tlast); return; }
    if(uvc_kind==1&&uvc_jnew&&uvc_jpg&&uvc_rgb){ int w=0,h=0; int r=jpeg_decode(uvc_jpg,(int)uvc_jlen,(u8*)uvc_rgb,1920*1200*4,&w,&h);
        if(r==0&&w==uvc_w&&h==uvc_h){ u8* b=(u8*)uvc_rgb; u32 n=(u32)w*(u32)h; for(u32 k=0;k<n;k++){ u8* p=b+k*4; uvc_rgb[k]=((u32)p[0]<<16)|((u32)p[1]<<8)|(u32)p[2]; } uvc_seq++; } uvc_jnew=0; }
    if(!uvc_rgb||uvc_w<=0||uvc_h<=0||uvc_seq==g_v4lseq)return; int i=v4q_take(&g_v4q); if(i<0)return; g_v4lseq=uvc_seq;
    if(g_v4pix==V4_PIX_BGR4)v4q_rgb_bgr4(uvc_rgb,uvc_w,uvc_h,g_v4stage,g_v4w,g_v4h); else v4q_rgb_yuyv(uvc_rgb,uvc_w,uvc_h,g_v4stage,g_v4w,g_v4h);
    u32 n=v4q_frame_size(g_v4pix,g_v4w,g_v4h); v4_copy_out(i,g_v4stage,n); g_v4tlast=hr_ns(); v4q_done(&g_v4q,i,n,g_v4tlast); }
static int v4_poll1(struct vfile* v){ (void)v; if(!uvc_found())return 8|16; v4_pump(); if(g_v4q.dn)return 1|0x40; if(!g_v4q.streaming&&g_v4q.n)return 8; return 0; }
static void v4_putpix(u8* b){ *(u32*)(b+V4_PIX_W)=(u32)g_v4w; *(u32*)(b+V4_PIX_H)=(u32)g_v4h; *(u32*)(b+V4_PIX_FMT)=g_v4pix; *(u32*)(b+V4_PIX_FIELD)=V4_FIELD_NONE; *(u32*)(b+V4_PIX_BPL)=v4q_bpl(g_v4pix,g_v4w); *(u32*)(b+V4_PIX_SIZE)=v4q_frame_size(g_v4pix,g_v4w,g_v4h); *(u32*)(b+V4_PIX_CS)=g_v4pix==V4_PIX_MJPG?V4_CS_JPEG:V4_CS_SRGB; }
static void v4_putbuf(u8* b,int i){ lx_zero(b,V4_BUF_SZ); *(u32*)(b+V4_B_INDEX)=(u32)i; *(u32*)(b+V4_B_TYPE)=V4_BUF_TYPE_CAPTURE; *(u32*)(b+V4_B_USED)=g_v4q.st[i]==V4Q_USER||g_v4q.st[i]==V4Q_DONE?g_v4q.used[i]:0; *(u32*)(b+V4_B_FLAGS)=v4q_flags(&g_v4q,i)|V4_BF_MAPPED;
    *(u32*)(b+V4_B_FIELD)=V4_FIELD_NONE; *(u64*)(b+V4_B_TS)=g_v4q.ts[i]/1000000000ULL; *(u64*)(b+V4_B_TS+8)=(g_v4q.ts[i]%1000000000ULL)/1000ULL; *(u32*)(b+V4_B_SEQ)=g_v4q.bseq[i]; *(u32*)(b+V4_B_MEMORY)=V4_MEMORY_MMAP; *(u32*)(b+V4_B_OFFSET)=((u32)g_v4cur<<28)+(u32)i*v4q_pgsize(g_v4q.size); *(u32*)(b+V4_B_LENGTH)=g_v4q.size; }
static void v4_str(u8* d,const char* s,int cap){ int i=0; for(;s[i]&&i<cap-1;i++)d[i]=(u8)s[i]; }
static int v4_busy(struct vfile* v){ int vi=(int)(v-g_vf); return g_v4mine&&g_v4mine!=vi+1; }
static long v4l_ioctl1(struct sysregs* r,struct vfile* v,u32 req,u64 arg){
    if(!uvc_found())return -LX_ENODEV;
    int vi=(int)(v-g_vf);
    switch(req){
    case V4_QUERYCAP: { u8 b[V4_CAP_SZ]; lx_zero(b,sizeof b); v4_str(b+V4_CAP_DRIVER,"uvcvideo",16); v4_str(b+V4_CAP_CARD,uvc_name[0]?uvc_name:"USB Camera",32); v4_str(b+V4_CAP_BUS,g_v4cur?"usb-noovex-2":"usb-noovex-1",32); *(u32*)(b+V4_CAP_VERSION)=0x00060600u;
        *(u32*)(b+V4_CAP_CAPS)=V4_CAP_VIDEO_CAPTURE|V4_CAP_STREAMING|V4_CAP_DEVICE_CAPS; *(u32*)(b+V4_CAP_DEVCAPS)=V4_CAP_VIDEO_CAPTURE|V4_CAP_STREAMING; return lx_put(arg,b,sizeof b); }
    case V4_ENUM_FMT: { u8 b[V4_FMTDESC_SZ]; if(lx_get(b,arg,sizeof b))return -LX_EFAULT; if(*(u32*)(b+V4_FD_TYPE)!=V4_BUF_TYPE_CAPTURE)return -LX_EINVAL; u32 pix; if(!v4_nfmt((int)*(u32*)(b+V4_FD_INDEX),&pix))return -LX_EINVAL;
        lx_zero(b+V4_FD_FLAGS,V4_FMTDESC_SZ-V4_FD_FLAGS); *(u32*)(b+V4_FD_FLAGS)=pix==V4_PIX_MJPG||pix==V4_PIX_H264?V4_FMT_FLAG_COMPRESSED:0; v4_str(b+V4_FD_DESC,pix==V4_PIX_H264?"H.264":pix==V4_PIX_MJPG?"Motion-JPEG":pix==V4_PIX_YUYV?"YUYV 4:2:2":"32-bit BGRX 8-8-8-8",32); *(u32*)(b+V4_FD_PIXFMT)=pix; return lx_put(arg,b,sizeof b); }
    case V4_G_FMT: { u8 b[V4_FORMAT_SZ]; if(lx_get(b,arg,sizeof b))return -LX_EFAULT; if(*(u32*)(b+V4_F_TYPE)!=V4_BUF_TYPE_CAPTURE)return -LX_EINVAL; if(!v4_fmt_known(g_v4pix)){ u32 p0; if(v4_nfmt(0,&p0))g_v4pix=p0; } v4_nearest(g_v4pix,&g_v4w,&g_v4h); lx_zero(b+V4_F_PIX,V4_FORMAT_SZ-V4_F_PIX); v4_putpix(b+V4_F_PIX); return lx_put(arg,b,sizeof b); }
    case V4_S_FMT: case V4_TRY_FMT: { u8 b[V4_FORMAT_SZ]; if(lx_get(b,arg,sizeof b))return -LX_EFAULT; if(*(u32*)(b+V4_F_TYPE)!=V4_BUF_TYPE_CAPTURE)return -LX_EINVAL;
        u32 pix=*(u32*)(b+V4_F_PIX+V4_PIX_FMT); int w=(int)*(u32*)(b+V4_F_PIX+V4_PIX_W), h=(int)*(u32*)(b+V4_F_PIX+V4_PIX_H); if(!v4_fmt_known(pix)){ if(!v4_nfmt(0,&pix))return -LX_EINVAL; }
        if(w<=0||w>8192)w=640; if(h<=0||h>8192)h=480; v4_nearest(pix,&w,&h);
        if(req==V4_S_FMT){ if(v4_busy(v)||g_v4q.n)return -LX_EBUSY; g_v4pix=pix; g_v4w=w; g_v4h=h; }
        u32 sp=g_v4pix; int sw=g_v4w, sh=g_v4h; g_v4pix=pix; g_v4w=w; g_v4h=h; lx_zero(b+V4_F_PIX,V4_FORMAT_SZ-V4_F_PIX); v4_putpix(b+V4_F_PIX); if(req==V4_TRY_FMT){ g_v4pix=sp; g_v4w=sw; g_v4h=sh; } return lx_put(arg,b,sizeof b); }
    case V4_ENUM_FRAMESIZES: { u8 b[V4_FRMSIZE_SZ]; if(lx_get(b,arg,sizeof b))return -LX_EFAULT; int w,h; u32 pix=*(u32*)(b+V4_FS_PIXFMT); if(!v4_fmt_known(pix)||!v4_size(pix,(int)*(u32*)(b+V4_FS_INDEX),&w,&h,0))return -LX_EINVAL;
        lx_zero(b+V4_FS_TYPE,V4_FRMSIZE_SZ-V4_FS_TYPE); *(u32*)(b+V4_FS_TYPE)=V4_FRMSIZE_DISCRETE; *(u32*)(b+V4_FS_W)=(u32)w; *(u32*)(b+V4_FS_H)=(u32)h; return lx_put(arg,b,sizeof b); }
    case V4_ENUM_FRAMEINTERVALS: { u8 b[V4_FRMIVAL_SZ]; if(lx_get(b,arg,sizeof b))return -LX_EFAULT; if(*(u32*)(b+V4_FI_INDEX)!=0)return -LX_EINVAL; u32 pix=*(u32*)(b+V4_FI_PIXFMT); int w=(int)*(u32*)(b+V4_FI_W), h=(int)*(u32*)(b+V4_FI_H), fw,fh; u32 di=0, found=0;
        for(int i=0;v4_size(pix,i,&fw,&fh,&di);i++) if(fw==w&&fh==h){ found=1; break; } if(!v4_fmt_known(pix)||!found)return -LX_EINVAL;
        lx_zero(b+V4_FI_TYPE,V4_FRMIVAL_SZ-V4_FI_TYPE); *(u32*)(b+V4_FI_TYPE)=V4_FRMIVAL_DISCRETE; *(u32*)(b+V4_FI_NUM)=di?di:333333u; *(u32*)(b+V4_FI_DEN)=10000000u; return lx_put(arg,b,sizeof b); }
    case V4_G_PARM: case V4_S_PARM: { u8 b[V4_PARM_SZ]; if(lx_get(b,arg,sizeof b))return -LX_EFAULT; if(*(u32*)b!=V4_BUF_TYPE_CAPTURE)return -LX_EINVAL; int fw,fh; u32 di=0; for(int i=0;v4_size(g_v4pix,i,&fw,&fh,&di);i++) if(fw==g_v4w&&fh==g_v4h)break;
        lx_zero(b+4,V4_PARM_SZ-4); *(u32*)(b+V4_PARM_CAPS)=V4_CAP_TIMEPERFRAME; *(u32*)(b+V4_PARM_NUM)=di?di:333333u; *(u32*)(b+V4_PARM_DEN)=10000000u; *(u32*)(b+V4_PARM_READBUFS)=0; return lx_put(arg,b,sizeof b); }
    case V4_ENUMINPUT: { u8 b[V4_INPUT_SZ]; if(lx_get(b,arg,sizeof b))return -LX_EFAULT; if(*(u32*)b!=0)return -LX_EINVAL; lx_zero(b+4,V4_INPUT_SZ-4); v4_str(b+V4_IN_NAME,"Camera 1",32); *(u32*)(b+V4_IN_TYPE)=2; return lx_put(arg,b,sizeof b); }
    case V4_G_INPUT: { u32 z=0; return lx_put(arg,&z,4); }
    case V4_S_INPUT: { u32 z; if(lx_get(&z,arg,4))return -LX_EFAULT; return z==0?0:-LX_EINVAL; }
    case V4_G_PRIORITY: { u32 z=2; return lx_put(arg,&z,4); }
    case V4_REQBUFS: { u8 b[V4_REQBUFS_SZ]; if(lx_get(b,arg,sizeof b))return -LX_EFAULT; if(*(u32*)(b+V4_RB_TYPE)!=V4_BUF_TYPE_CAPTURE||*(u32*)(b+V4_RB_MEMORY)!=V4_MEMORY_MMAP)return -LX_EINVAL; if(v4_busy(v))return -LX_EBUSY;
        int n=(int)*(u32*)(b+V4_RB_COUNT); u32 sz=v4q_frame_size(g_v4pix,g_v4w,g_v4h); if(n>0&&n<2)n=2; int e=v4q_reqbufs(&g_v4q,n,sz); if(e<0)return e;
        if(e>0){ int a=v4_alloc(e,sz); if(a<0){ v4q_reqbufs(&g_v4q,0,0); return a; } g_v4mine=vi+1; } else { v4_free(); g_v4mine=0; }
        *(u32*)(b+V4_RB_COUNT)=(u32)e; *(u32*)(b+V4_RB_CAPS)=1; return lx_put(arg,b,sizeof b); }
    case V4_QUERYBUF: case V4_QBUF: { u8 b[V4_BUF_SZ]; if(lx_get(b,arg,sizeof b))return -LX_EFAULT; if(*(u32*)(b+V4_B_TYPE)!=V4_BUF_TYPE_CAPTURE)return -LX_EINVAL; if(v4_busy(v))return -LX_EBUSY; int i=(int)*(u32*)(b+V4_B_INDEX); if(i<0||i>=g_v4q.n)return -LX_EINVAL;
        if(req==V4_QBUF){ if(*(u32*)(b+V4_B_MEMORY)!=V4_MEMORY_MMAP)return -LX_EINVAL; int e=v4q_qbuf(&g_v4q,i); if(e<0)return e; v4_pump(); }
        v4_putbuf(b,i); return lx_put(arg,b,sizeof b); }
    case V4_DQBUF: { u8 b[V4_BUF_SZ]; if(lx_get(b,arg,sizeof b))return -LX_EFAULT; if(*(u32*)(b+V4_B_TYPE)!=V4_BUF_TYPE_CAPTURE)return -LX_EINVAL; if(v4_busy(v))return -LX_EBUSY;
        v4_pump(); int i; int e=v4q_dqbuf(&g_v4q,&i);
        if(e==-V4Q_EAGAIN){ if(lx_nonblock(v))return -LX_EAGAIN; if(uvc_state!=2||hr_ns()-g_v4tlast>5000000000ULL){ sysfeat_fail(SYSF_V4L2,"no frame from the camera for 5 s"); return -LX_EIO; } v4_leave(); return lx_sock_wait(r,JS_BLOCK,0); }
        if(e<0)return e; v4_putbuf(b,i); return lx_put(arg,b,sizeof b); }
    case V4_STREAMON: { u32 t; if(lx_get(&t,arg,4))return -LX_EFAULT; if(t!=V4_BUF_TYPE_CAPTURE)return -LX_EINVAL; if(v4_busy(v)||!g_v4q.n)return -LX_EINVAL; if(g_v4q.streaming)return 0;
        int kind=0, fw,fh; { int n=uvc_nfr(); for(int k=0;k<n;k++){ int kd; u32 d; if(uvc_frame_info(k,&kd,&fw,&fh,&d)&&fw==g_v4w&&fh==g_v4h&&(g_v4pix==V4_PIX_MJPG?kd==1:1)){ kind=kd; if(kd!=1||g_v4pix==V4_PIX_MJPG)break; } } }
        if(g_v4pix==V4_PIX_MJPG)kind=1; sysfeat_begin(SYSF_V4L2);
        if(g_v4pix==V4_PIX_H264){ if((uvc_state!=2||uvc_kind!=8||uvc_w!=g_v4w||uvc_h!=g_v4h)&&(uvx_h264_start(g_v4w,g_v4h)<0||uvc_state!=2||uvc_kind!=8)){ sysfeat_fail(SYSF_V4L2,"camera did not start H.264"); return -LX_EIO; } }
        else if(uvc_state!=2||uvc_w!=g_v4w||uvc_h!=g_v4h||(g_v4pix==V4_PIX_MJPG&&uvc_kind!=1)||uvc_kind==8){ if(v4_start(kind)<0||uvc_state!=2){ sysfeat_fail(SYSF_V4L2,"camera did not start"); return -LX_EIO; } }
        sysfeat_ok(SYSF_V4L2); g_v4lseq=uvc_seq; g_v4lfr=uvc_frames; g_v4tlast=hr_ns(); return v4q_streamon(&g_v4q); }
    case V4_STREAMOFF: { u32 t; if(lx_get(&t,arg,4))return -LX_EFAULT; if(t!=V4_BUF_TYPE_CAPTURE)return -LX_EINVAL; if(v4_busy(v))return -LX_EBUSY; int n=g_v4q.n; u32 sz=g_v4q.size; v4q_streamoff(&g_v4q); g_v4q.n=n; g_v4q.size=sz; return 0; }
    case V4_QUERYCTRL: { u8 b[V4_QCTRL_SZ]; if(lx_get(b,arg,sizeof b))return -LX_EFAULT; u32 id=*(u32*)(b+V4_QC_ID); int c=-1, mn=0,mx=0,df=0;
        if(id&V4_CTRL_FLAG_NEXT){ u32 base=id&~V4_CTRL_FLAG_NEXT; u32 best=0xFFFFFFFFu; for(int k=0;k<UVC_C_N;k++) if(v4c_cid[k]>base&&v4c_cid[k]<best&&v4c_info(k,&mn,&mx,&df)){ best=v4c_cid[k]; c=k; } }
        else c=v4c_by_cid(id);
        if(c<0||!v4c_info(c,&mn,&mx,&df))return -LX_EINVAL;
        lx_zero(b,sizeof b); *(u32*)(b+V4_QC_ID)=v4c_cid[c]; *(u32*)(b+V4_QC_TYPE)=(u32)v4c_type(c); v4_str(b+V4_QC_NAME,uvc_ctrl_name(c),32);
        *(int*)(b+V4_QC_MIN)=mn; *(int*)(b+V4_QC_MAX)=mx; *(int*)(b+V4_QC_STEP)=1; *(int*)(b+V4_QC_DEF)=df; return lx_put(arg,b,sizeof b); }
    case V4_QUERYMENU: { u8 b[V4_QMENU_SZ]; if(lx_get(b,arg,sizeof b))return -LX_EFAULT; int c=v4c_by_cid(*(u32*)(b+V4_QM_ID)); u32 ix=*(u32*)(b+V4_QM_INDEX); int mn,mx,df;
        static const char* const PL[4]={"Disabled","50 Hz","60 Hz","Auto"}; static const char* const EA[4]={"Auto Mode","Manual Mode","Shutter Priority Mode","Aperture Priority Mode"};
        if((c!=UVC_C_PLF&&c!=UVC_C_EXPOSURE_AUTO)||ix>3||(c==UVC_C_EXPOSURE_AUTO&&ix!=1&&ix!=3)||!v4c_info(c,&mn,&mx,&df)||(int)ix<mn||(int)ix>mx)return -LX_EINVAL; lx_zero(b+V4_QM_NAME,V4_QMENU_SZ-V4_QM_NAME); v4_str(b+V4_QM_NAME,c==UVC_C_PLF?PL[ix]:EA[ix],32); return lx_put(arg,b,sizeof b); }
    case V4_G_CTRL: case V4_S_CTRL: { u8 b[V4_CTRL_SZ]; if(lx_get(b,arg,sizeof b))return -LX_EFAULT; int c=v4c_by_cid(*(u32*)b), mn,mx,df; if(c<0||!v4c_info(c,&mn,&mx,&df))return -LX_EINVAL; int val=*(int*)(b+4);
        if(req==V4_S_CTRL){ if(val<mn||val>mx)return -LX_ERANGE; return v4c_set(c,val)<0?-LX_EIO:0; }
        if(v4c_get(c,&val)<0)return -LX_EIO; *(int*)(b+4)=val; return lx_put(arg,b,sizeof b); }
    }
    return -LX_ENOTTY; }
void v4l_close(int vi){ v4_enter(g_vf[vi].fs_idx); v4l_close1(vi); v4_leave(); }
static int v4_map_frames(u64 off,u64 len,u64** fr,int* np){ v4_enter((int)((off>>28)&7)); int r=v4_map_frames1(off&((1ULL<<28)-1),len,fr,np); v4_leave(); return r; }
static int v4_poll(struct vfile* v){ v4_enter(v->fs_idx); int r=v4_poll1(v); v4_leave(); return r; }
static long v4l_ioctl(struct sysregs* r,struct vfile* v,u32 req,u64 arg){ v4_enter(v->fs_idx); long x=v4l_ioctl1(r,v,req,arg); v4_leave(); return x; }
