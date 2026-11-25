#include <stdio.h>
#include <stddef.h>
#include <string.h>
#include <linux/videodev2.h>
#include "sys/linux/v4l2_abi.h"
#include "sys/linux/v4l2_q.h"
static int fails, oks;
#define EQ(a,b) do{ unsigned long long _a=(unsigned long long)(a), _b=(unsigned long long)(b); if(_a!=_b){ printf("FAIL %s: %llu != %llu (%s)\n",#a,_a,_b,#b); fails++; } else oks++; }while(0)
static void layouts(void){
    EQ(V4_CAP_SZ,sizeof(struct v4l2_capability)); EQ(V4_CAP_CARD,offsetof(struct v4l2_capability,card)); EQ(V4_CAP_BUS,offsetof(struct v4l2_capability,bus_info)); EQ(V4_CAP_VERSION,offsetof(struct v4l2_capability,version)); EQ(V4_CAP_CAPS,offsetof(struct v4l2_capability,capabilities)); EQ(V4_CAP_DEVCAPS,offsetof(struct v4l2_capability,device_caps));
    EQ(V4_FMTDESC_SZ,sizeof(struct v4l2_fmtdesc)); EQ(V4_FD_TYPE,offsetof(struct v4l2_fmtdesc,type)); EQ(V4_FD_FLAGS,offsetof(struct v4l2_fmtdesc,flags)); EQ(V4_FD_DESC,offsetof(struct v4l2_fmtdesc,description)); EQ(V4_FD_PIXFMT,offsetof(struct v4l2_fmtdesc,pixelformat));
    EQ(V4_FORMAT_SZ,sizeof(struct v4l2_format)); EQ(V4_F_PIX,offsetof(struct v4l2_format,fmt.pix));
    EQ(V4_PIX_W,offsetof(struct v4l2_pix_format,width)); EQ(V4_PIX_H,offsetof(struct v4l2_pix_format,height)); EQ(V4_PIX_FMT,offsetof(struct v4l2_pix_format,pixelformat)); EQ(V4_PIX_FIELD,offsetof(struct v4l2_pix_format,field)); EQ(V4_PIX_BPL,offsetof(struct v4l2_pix_format,bytesperline)); EQ(V4_PIX_SIZE,offsetof(struct v4l2_pix_format,sizeimage)); EQ(V4_PIX_CS,offsetof(struct v4l2_pix_format,colorspace));
    EQ(V4_FRMSIZE_SZ,sizeof(struct v4l2_frmsizeenum)); EQ(V4_FS_PIXFMT,offsetof(struct v4l2_frmsizeenum,pixel_format)); EQ(V4_FS_TYPE,offsetof(struct v4l2_frmsizeenum,type)); EQ(V4_FS_W,offsetof(struct v4l2_frmsizeenum,discrete.width)); EQ(V4_FS_H,offsetof(struct v4l2_frmsizeenum,discrete.height));
    EQ(V4_FRMIVAL_SZ,sizeof(struct v4l2_frmivalenum)); EQ(V4_FI_PIXFMT,offsetof(struct v4l2_frmivalenum,pixel_format)); EQ(V4_FI_W,offsetof(struct v4l2_frmivalenum,width)); EQ(V4_FI_H,offsetof(struct v4l2_frmivalenum,height)); EQ(V4_FI_TYPE,offsetof(struct v4l2_frmivalenum,type)); EQ(V4_FI_NUM,offsetof(struct v4l2_frmivalenum,discrete.numerator)); EQ(V4_FI_DEN,offsetof(struct v4l2_frmivalenum,discrete.denominator));
    EQ(V4_REQBUFS_SZ,sizeof(struct v4l2_requestbuffers)); EQ(V4_RB_TYPE,offsetof(struct v4l2_requestbuffers,type)); EQ(V4_RB_MEMORY,offsetof(struct v4l2_requestbuffers,memory)); EQ(V4_RB_CAPS,offsetof(struct v4l2_requestbuffers,capabilities));
    EQ(V4_BUF_SZ,sizeof(struct v4l2_buffer)); EQ(V4_B_TYPE,offsetof(struct v4l2_buffer,type)); EQ(V4_B_USED,offsetof(struct v4l2_buffer,bytesused)); EQ(V4_B_FLAGS,offsetof(struct v4l2_buffer,flags)); EQ(V4_B_FIELD,offsetof(struct v4l2_buffer,field)); EQ(V4_B_TS,offsetof(struct v4l2_buffer,timestamp)); EQ(V4_B_SEQ,offsetof(struct v4l2_buffer,sequence)); EQ(V4_B_MEMORY,offsetof(struct v4l2_buffer,memory)); EQ(V4_B_OFFSET,offsetof(struct v4l2_buffer,m.offset)); EQ(V4_B_LENGTH,offsetof(struct v4l2_buffer,length));
    EQ(V4_QCTRL_SZ,sizeof(struct v4l2_queryctrl)); EQ(V4_QC_TYPE,offsetof(struct v4l2_queryctrl,type)); EQ(V4_QC_NAME,offsetof(struct v4l2_queryctrl,name)); EQ(V4_QC_MIN,offsetof(struct v4l2_queryctrl,minimum)); EQ(V4_QC_MAX,offsetof(struct v4l2_queryctrl,maximum)); EQ(V4_QC_STEP,offsetof(struct v4l2_queryctrl,step)); EQ(V4_QC_DEF,offsetof(struct v4l2_queryctrl,default_value)); EQ(V4_QC_FLAGS,offsetof(struct v4l2_queryctrl,flags));
    EQ(V4_QMENU_SZ,sizeof(struct v4l2_querymenu)); EQ(V4_QM_INDEX,offsetof(struct v4l2_querymenu,index)); EQ(V4_QM_NAME,offsetof(struct v4l2_querymenu,name));
    EQ(V4_CTRL_SZ,sizeof(struct v4l2_control)); EQ(4,offsetof(struct v4l2_control,value));
    EQ(V4_PARM_SZ,sizeof(struct v4l2_streamparm)); EQ(V4_PARM_CAPS,offsetof(struct v4l2_streamparm,parm.capture.capability)); EQ(V4_PARM_NUM,offsetof(struct v4l2_streamparm,parm.capture.timeperframe.numerator)); EQ(V4_PARM_DEN,offsetof(struct v4l2_streamparm,parm.capture.timeperframe.denominator)); EQ(V4_PARM_READBUFS,offsetof(struct v4l2_streamparm,parm.capture.readbuffers));
    EQ(V4_INPUT_SZ,sizeof(struct v4l2_input)); EQ(V4_IN_NAME,offsetof(struct v4l2_input,name)); EQ(V4_IN_TYPE,offsetof(struct v4l2_input,type)); EQ(V4_IN_STATUS,offsetof(struct v4l2_input,status)); EQ(V4_IN_CAPS,offsetof(struct v4l2_input,capabilities));
    EQ(V4_QUERYCAP,VIDIOC_QUERYCAP); EQ(V4_ENUM_FMT,VIDIOC_ENUM_FMT); EQ(V4_G_FMT,VIDIOC_G_FMT); EQ(V4_S_FMT,VIDIOC_S_FMT); EQ(V4_TRY_FMT,VIDIOC_TRY_FMT); EQ(V4_REQBUFS,VIDIOC_REQBUFS); EQ(V4_QUERYBUF,VIDIOC_QUERYBUF); EQ(V4_QBUF,VIDIOC_QBUF); EQ(V4_DQBUF,VIDIOC_DQBUF); EQ(V4_STREAMON,VIDIOC_STREAMON); EQ(V4_STREAMOFF,VIDIOC_STREAMOFF);
    EQ(V4_G_PARM,VIDIOC_G_PARM); EQ(V4_S_PARM,VIDIOC_S_PARM); EQ(V4_ENUMINPUT,VIDIOC_ENUMINPUT); EQ(V4_G_INPUT,VIDIOC_G_INPUT); EQ(V4_S_INPUT,VIDIOC_S_INPUT); EQ(V4_QUERYCTRL,VIDIOC_QUERYCTRL); EQ(V4_QUERYMENU,VIDIOC_QUERYMENU); EQ(V4_G_CTRL,VIDIOC_G_CTRL); EQ(V4_S_CTRL,VIDIOC_S_CTRL); EQ(V4_ENUM_FRAMESIZES,VIDIOC_ENUM_FRAMESIZES); EQ(V4_ENUM_FRAMEINTERVALS,VIDIOC_ENUM_FRAMEINTERVALS); EQ(V4_G_PRIORITY,VIDIOC_G_PRIORITY);
    EQ(V4_PIX_YUYV,V4L2_PIX_FMT_YUYV); EQ(V4_PIX_MJPG,V4L2_PIX_FMT_MJPEG); EQ(V4_PIX_BGR4,V4L2_PIX_FMT_BGR32); EQ(V4_PIX_RGB3,V4L2_PIX_FMT_RGB24);
    EQ(V4_CID_BASE,V4L2_CID_BRIGHTNESS); EQ(V4_CID_BASE+0x13,V4L2_CID_GAIN); EQ(V4_CID_BASE+0x1B,V4L2_CID_SHARPNESS); EQ(V4_CID_BASE+0x10,V4L2_CID_GAMMA); EQ(V4_CID_BASE+3,V4L2_CID_HUE); EQ(V4_CID_BASE+0x1C,V4L2_CID_BACKLIGHT_COMPENSATION); EQ(V4_CID_BASE+0x0C,V4L2_CID_AUTO_WHITE_BALANCE); EQ(V4_CID_BASE+0x1A,V4L2_CID_WHITE_BALANCE_TEMPERATURE); EQ(V4_CID_BASE+0x18,V4L2_CID_POWER_LINE_FREQUENCY);
    EQ(V4_CID_CAM_BASE+1,V4L2_CID_EXPOSURE_AUTO); EQ(V4_CID_CAM_BASE+2,V4L2_CID_EXPOSURE_ABSOLUTE); EQ(V4_CID_CAM_BASE+0x0C,V4L2_CID_FOCUS_AUTO); EQ(V4_CID_CAM_BASE+0x0A,V4L2_CID_FOCUS_ABSOLUTE); EQ(V4_CTRL_FLAG_NEXT,V4L2_CTRL_FLAG_NEXT_CTRL);
    EQ(V4_CAP_VIDEO_CAPTURE,V4L2_CAP_VIDEO_CAPTURE); EQ(V4_CAP_STREAMING,V4L2_CAP_STREAMING); EQ(V4_CAP_DEVICE_CAPS,V4L2_CAP_DEVICE_CAPS); EQ(V4_CAP_TIMEPERFRAME,V4L2_CAP_TIMEPERFRAME);
    EQ(V4_BF_MAPPED,V4L2_BUF_FLAG_MAPPED); EQ(V4_BF_QUEUED,V4L2_BUF_FLAG_QUEUED); EQ(V4_BF_DONE,V4L2_BUF_FLAG_DONE); EQ(V4_BF_TS_MONO,V4L2_BUF_FLAG_TIMESTAMP_MONOTONIC); EQ(V4_FMT_FLAG_COMPRESSED,V4L2_FMT_FLAG_COMPRESSED);
    EQ(V4_BUF_TYPE_CAPTURE,V4L2_BUF_TYPE_VIDEO_CAPTURE); EQ(V4_MEMORY_MMAP,V4L2_MEMORY_MMAP); EQ(V4_FIELD_NONE,V4L2_FIELD_NONE); EQ(V4_CS_SRGB,V4L2_COLORSPACE_SRGB); EQ(V4_CS_JPEG,V4L2_COLORSPACE_JPEG); EQ(V4_FRMSIZE_DISCRETE,V4L2_FRMSIZE_TYPE_DISCRETE); EQ(V4_FRMIVAL_DISCRETE,V4L2_FRMIVAL_TYPE_DISCRETE);
    EQ(V4_CTRL_TYPE_INTEGER,V4L2_CTRL_TYPE_INTEGER); EQ(V4_CTRL_TYPE_BOOLEAN,V4L2_CTRL_TYPE_BOOLEAN); EQ(V4_CTRL_TYPE_MENU,V4L2_CTRL_TYPE_MENU);
    printf("  layouts and ioctl numbers match linux/videodev2.h ok\n"); }
static void queue(void){
    struct v4q q; memset(&q,0,sizeof q); int i;
    EQ(v4q_dqbuf(&q,&i),-V4Q_EINVAL); EQ(v4q_streamon(&q),-V4Q_EINVAL); EQ(v4q_reqbufs(&q,4,0),-V4Q_EINVAL);
    EQ(v4q_reqbufs(&q,20,640*480*2),V4Q_MAX); EQ(v4q_reqbufs(&q,4,640*480*2),4); EQ(q.n,4);
    EQ(v4q_pgsize(614400),614400); EQ(v4q_pgsize(614401),618496);
    EQ(v4q_by_offset(&q,0,614400),0); EQ(v4q_by_offset(&q,614400*3,4096),3); EQ(v4q_by_offset(&q,614400*4,4096),-1); EQ(v4q_by_offset(&q,4096,4096),-1); EQ(v4q_by_offset(&q,0,614400+4096),-1);
    EQ(v4q_qbuf(&q,4),-V4Q_EINVAL); EQ(v4q_qbuf(&q,-1),-V4Q_EINVAL);
    for(int k=0;k<4;k++)EQ(v4q_qbuf(&q,k),0); EQ(v4q_qbuf(&q,2),-V4Q_EINVAL);
    EQ(v4q_flags(&q,1)&V4_BF_QUEUED,V4_BF_QUEUED);
    EQ(v4q_take(&q),-1); EQ(v4q_dqbuf(&q,&i),-V4Q_EINVAL);
    EQ(v4q_streamon(&q),0); EQ(v4q_reqbufs(&q,2,100),-V4Q_EBUSY); EQ(v4q_dqbuf(&q,&i),-V4Q_EAGAIN);
    int t=v4q_take(&q); EQ(t,0); v4q_done(&q,t,1000,111); EQ(v4q_flags(&q,0)&(V4_BF_DONE|V4_BF_QUEUED),V4_BF_DONE);
    t=v4q_take(&q); EQ(t,1); v4q_done(&q,t,2000,222);
    EQ(v4q_dqbuf(&q,&i),0); EQ(i,0); EQ(q.used[0],1000); EQ(q.bseq[0],0); EQ(q.ts[0],111);
    EQ(v4q_dqbuf(&q,&i),0); EQ(i,1); EQ(q.bseq[1],1);
    EQ(v4q_dqbuf(&q,&i),-V4Q_EAGAIN); EQ(v4q_qbuf(&q,0),0);
    EQ(v4q_take(&q),2); EQ(v4q_take(&q),3); t=v4q_take(&q); EQ(t,0); EQ(v4q_take(&q),-1); v4q_done(&q,t,5,333); EQ(q.bseq[0],2);
    EQ(v4q_dqbuf(&q,&i),0); EQ(i,0);
    for(int k=0;k<10000;k++){ if(v4q_qbuf(&q,0)){ fails++; break; } int a=v4q_take(&q); if(a!=0){ fails++; printf("FAIL loop take %d\n",a); break; } v4q_done(&q,a,1,k); if(v4q_dqbuf(&q,&i)||i!=0){ fails++; break; } } oks++;
    EQ(v4q_streamoff(&q),0); EQ(q.streaming,0); for(int k=0;k<4;k++)EQ(q.st[k],V4Q_USER); EQ(q.qn,0); EQ(q.dn,0);
    EQ(v4q_reqbufs(&q,0,0),0); EQ(q.n,0); EQ(v4q_qbuf(&q,0),-V4Q_EINVAL);
    EQ(v4q_reqbufs(&q,2,4096),2); EQ(v4q_qbuf(&q,0),0); EQ(v4q_qbuf(&q,1),0); EQ(v4q_streamon(&q),0); EQ(v4q_streamon(&q),0);
    t=v4q_take(&q); v4q_done(&q,t,4096,1); EQ(v4q_streamoff(&q),0); EQ(v4q_dqbuf(&q,&i),-V4Q_EINVAL);
    printf("  buffer queue: reqbufs/qbuf/dqbuf/streamon/off state machine, FIFO order, sequence, mmap offsets ok\n"); }
static void convert(void){
    unsigned src[4*2]={0xFFFFFF,0xFFFFFF,0x000000,0x000000,0xFF0000,0xFF0000,0x0000FF,0x0000FF}; unsigned char d[4*2*2], e[4*2*4];
    v4q_rgb_yuyv(src,4,2,d,4,2); EQ(d[0],235); EQ(d[2],235); EQ(d[1],128); EQ(d[3],128); EQ(d[4],16); EQ(d[8],82); EQ(d[11],240); EQ(d[12],41); EQ(d[13],240);
    v4q_rgb_bgr4(src,4,2,e,4,2); EQ(e[16],0); EQ(e[17],0); EQ(e[18],255); EQ(e[19],0); EQ(e[24],255);
    unsigned char s2[2*1*2]; v4q_rgb_yuyv(src,4,2,s2,2,1); EQ(s2[0],235); EQ(s2[2],16);
    EQ(v4q_frame_size(V4_PIX_YUYV,640,480),614400); EQ(v4q_bpl(V4_PIX_YUYV,640),1280); EQ(v4q_frame_size(V4_PIX_BGR4,320,240),307200); EQ(v4q_bpl(V4_PIX_MJPG,640),0);
    printf("  pixel conversion: RGB to YUYV (BT.601 limited range) and BGRX, scaling ok\n"); }
int main(void){ layouts(); queue(); convert(); printf("%d checks, %d failed\n",oks+fails,fails); if(!fails)printf("v4l2 host test: ALL PASS\n"); return fails!=0; }
