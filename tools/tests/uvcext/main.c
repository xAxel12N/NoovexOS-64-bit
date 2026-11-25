static int fails=0; static void ck(const char* w,int c){ printf("  %-66s %s\n",w,c?"ok":"FAIL"); if(!c)fails++; }
static u8 D[4096]; static int dn;
static void b(int n,...){ __builtin_va_list a; __builtin_va_start(a,n); for(int i=0;i<n;i++)D[dn++]=(u8)__builtin_va_arg(a,int); __builtin_va_end(a); }
static void b4(u32 v){ b(4,v&255,(v>>8)&255,(v>>16)&255,v>>24); }
static void iface(int num,int alt,int cls,int sub){ b(9,9,4,num,alt,1,cls,sub,0,0); }
static void ep(int addr,int attr,int mps,int iv){ b(7,7,5,addr,attr,mps&255,mps>>8,iv); }
static void hdr(int nf,int method){ b(14,14,0x24,1,nf,0,0,0x81,0,1,method,0,0,1,0); }
static void unc(int fi,const char* cc){ b(5,27,0x24,4,fi,1); b(4,cc[0],cc[1],cc[2],cc[3]); for(int i=0;i<12;i++)b(1,0); b(6,16,0,0,0,0,0); }
static void mjpg(int fi){ b(11,11,0x24,6,fi,1,1,1,0,0,0,0); }
static void frame(int st,int ri,int w,int h,int n,const u32* iv){ b(5,26+n*4,0x24,st,ri,0); b(4,w&255,w>>8,h&255,h>>8); b4(0); b4(0); b4(0); b4(iv[0]); b(1,n); for(int k=0;k<n;k++)b4(iv[k]); }
static void fbfmt(int fi,const char* cc){ b(5,28,0x24,0x10,fi,1); b(4,cc[0],cc[1],cc[2],cc[3]); b(12,0,0,0x10,0,0x80,0,0,0xAA,0,0x38,0x9B,0x71); b(7,16,1,0,0,0,0,1); }
static void fbfr(int ri,int w,int h,int n,const u32* iv){ b(5,26+n*4,0x24,0x11,ri,0); b(4,w&255,w>>8,h&255,h>>8); b4(1000000); b4(8000000); b4(iv[0]); b(1,n); b4(0); for(int k=0;k<n;k++)b4(iv[k]); }
static void h15fmt(int fi){ b(5,52,0x24,0x13,fi,1); for(int i=5;i<52;i++)b(1,0); }
static void h15fr(int ri,int w,int h,int n,const u32* iv){ b(4,44+n*4,0x24,0x14,ri); b(4,w&255,w>>8,h&255,h>>8); for(int i=8;i<39;i++)b(1,0); b4(iv[0]); b(1,n); for(int k=0;k<n;k++)b4(iv[k]); }
static void still(int np,const int* wh){ b(5,5+np*4+1,0x24,3,0,np); for(int k=0;k<np;k++)b(4,wh[2*k]&255,wh[2*k]>>8,wh[2*k+1]&255,wh[2*k+1]>>8); b(1,0); }
static int rq_n, rq_fail; static u16 rq_val[8]; static u8 rq_req[8], rq_d[8][12];
static int uvc_req(u8 rt,u8 req,u16 val,u16 idx,u16 len,int in,u8* data){ (void)rt; (void)idx; if(rq_fail&&rq_n==rq_fail-1){ rq_n++; return 0; } if(rq_n<8){ rq_val[rq_n]=val; rq_req[rq_n]=req; for(int k=0;k<len&&k<12;k++)rq_d[rq_n][k]=data[k]; } rq_n++; if(in){ data[3]=0x55; } return 1; }
static void pkt(const u8* d,int n,int info){ u8 p[64]; p[0]=2; p[1]=(u8)(0x80|info); memcpy(p+2,d,n); uvc_payload(p,n+2); }
int main(void){
    printf("uvc extras: frame-based / UVC 1.5 H.264, still method 2, HDMI capture cards\n");
    u32 i30[2]={333333,666666}, i60[3]={166666,333333,2000000}, i5[1]={2000000}, i30b[1]={333333};
    dn=0; iface(0,0,14,1); iface(1,0,14,2); hdr(3,2);
    unc(1,"YUY2"); frame(5,1,640,480,2,i30);
    fbfmt(2,"H264"); fbfr(1,1920,1080,3,i60); fbfr(2,1280,720,2,i30);
    fbfmt(3,"MJPG"); fbfr(1,1280,720,3,i60); { int wh[4]={1280,720,2592,1944}; still(2,wh); }
    iface(1,1,14,2); ep(0x81,5,1024|(2<<11),1);
    memset(&cam,0,sizeof cam); cam.speed=3; uvc_choose(D,dn,-1); cam.ok=1;
    ck("frame-based H.264 found: 2 sizes kept apart from the shown formats",uvx.nh==2&&uvx.h[0].w==1920&&uvx.h[0].h==1080&&uvx.h[0].fb==1&&uvx.h[1].w==1280);
    ck("H.264 frame rate: 30 fps chosen from the 60/30/5 list",uvx.h[0].bestint==333333);
    ck("H.264 is not offered to the picture path (no decoder)",cam.nfr==2&&cam.fr[0].kind==0&&cam.fr[1].kind==1);
    ck("frame-based MJPEG becomes a normal MJPEG mode (1280x720)",cam.fr[1].fi==3&&cam.fr[1].w==1280&&cam.fr[1].h==720&&cam.fr[1].bestint==333333);
    ck("fastest interval remembered per mode (for capture cards)",uvx.mi[0]==333333&&uvx.mi[1]==166666);
    { int w,h; u32 iv; ck("H.264 list API",uvx_h264_n()==2&&uvx_h264_info(1,&w,&h,&iv)&&w==1280&&h==720&&iv==333333&&!uvx_h264_info(2,&w,&h,&iv)); }
    ck("still image: method 2 read from the input header",uvx.method==2);
    ck("still image sizes belong to the MJPEG format (index 3)",uvx.ns==2&&uvx.s[1].fi==3&&uvx.s[1].w==2592&&uvx.s[1].h==1944);
    rq_n=0; int r=uvx_still_req(3,1);
    ck("still method 2: probe, read back, commit, trigger",r==1&&rq_n==4&&rq_val[0]==0x0300&&rq_req[1]==0x81&&rq_val[2]==0x0400&&rq_val[3]==0x0500&&rq_d[3][0]==1);
    ck("still probe asks for the largest size (pattern 2), compression 1",rq_d[0][0]==3&&rq_d[0][1]==2&&rq_d[0][2]==1&&rq_d[2][3]==0x55&&uvx.want&&uvx.sw==2592);
    ck("still request for a format without still sizes is refused",uvx_still_req(1,1)==-2);
    rq_n=0; rq_fail=3; ck("still commit refused -> error, no trigger",uvx_still_req(3,1)==-4&&rq_n==3); rq_fail=0;
    { static u8 sb[1024]; uvx.sbuf=sb; uvx.scap=sizeof sb; uvx.want=1; uvx.slen=0; uvx_stills=0;
      static u8 fb[1<<16], jb[1<<16]; uvc_fb=fb; uvc_fcap=sizeof fb; uvc_jpg=jb; uvc_jnew=0; uvc_kind=1; uvc_fill=0; uvc_fbad=0; uvc_lfid=-1; uvc_frames=0; uvc_badframes=0;
      u8 j[6]={0xFF,0xD8,1,2,0xFF,0xD9}; pkt(j,6,0x20); pkt(j,6,0x20|2);
      ck("still frame (STI bit) goes to the still buffer, not the screen",uvx_stills==1&&uvx.slen==12&&sb[0]==0xFF&&sb[6]==0xFF&&uvc_frames==0&&!uvx.want);
      pkt(j,6,1|2); ck("next normal frame is shown again",uvc_frames==1&&uvc_jnew==1);
      uvx.want=0; pkt(j,6,0x20|2); ck("unrequested still frame is dropped",uvx_stills==1&&uvc_frames==1);
      uvc_kind=8; uvc_jnew=0; u8 nal[8]={0,0,0,1,0x67,0x42,0,0x1F}; pkt(nal,8,1); pkt(nal,8,1|2);
      ck("H.264 frame passed through whole (no JPEG check, no decode)",uvc_frames==2&&uvx_h264_frames==1&&uvc_jlen==16&&jb[4]==0x67&&jb[12]==0x67);
      uvc_jnew=0; pkt(nal,8,0); pkt(nal,8,0|0x40); pkt(nal,8,2); ck("H.264 frame with the error bit is dropped",uvx_h264_frames==1&&uvc_badframes==1); }
    uvx.force=1; cam.nfr=2; { int o[64]={0,1}; int n=uvx_reorder(o,2); ck("H.264 start: only the asked H.264 mode is probed",n==1&&o[0]==2&&cam.fr[2].kind==8&&cam.fr[2].fi==2&&cam.fr[2].w==1280); } uvx.force=-1;
    ck("uvc_kind_name knows H.264",!strcmp(uvc_kind_name(8),"H.264"));
    dn=0; iface(0,0,14,1); iface(1,0,14,2); hdr(2,1); mjpg(1); frame(7,1,640,480,1,i30b); h15fmt(2); h15fr(1,1920,1080,2,i60); h15fr(2,3840,2160,1,i30b); iface(1,1,14,2); ep(0x81,5,1024,1);
    memset(&cam,0,sizeof cam); cam.speed=3; uvc_choose(D,dn,-1); cam.ok=1;
    ck("UVC 1.5 H.264 format: 1920x1080 found, 3840x2160 left out (buffers are 1920x1200)",uvx.nh==1&&uvx.h[0].w==1920&&uvx.h[0].fb==0&&uvx.h[0].bestint==333333);
    ck("UVC 1.5 H.264 is not shown as a picture format",cam.nfr==1&&cam.fr[0].kind==1);
    ck("still method 1 (take a video frame): no still request sent",uvx.method==1&&uvx_still_req(1,1)==-1);
    printf("HDMI capture cards\n");
    dn=0; iface(0,0,14,1); b(13,13,0x24,1,0x10,0x01,0,0,0,0,0,0,0,0); iface(1,0,14,2); hdr(2,0);
    mjpg(1); frame(7,1,1920,1080,2,i30); frame(7,2,1280,720,3,i60); unc(2,"YUY2"); frame(5,1,1920,1080,1,i5); frame(5,2,640,480,2,i30);
    iface(1,1,14,2); ep(0x81,5,1024|(2<<11),1);
    memset(&cam,0,sizeof cam); cam.speed=3; uvc_choose(D,dn,-1); cam.ok=1; uvx_classify(0x534d,0x2109);
    ck("MS2109 recognised as a capture card",uvx.capture&&uvx.ms2109);
    { int o[64]; int n=uvc_order(1,o); n=uvx_reorder(o,n);
      ck("USB 2 capture card: MJPEG 1920x1080 30 fps first (YUYV 1080p is 5 fps)",n==4&&cam.fr[o[0]].kind==1&&cam.fr[o[0]].w==1920);
      ck("then MJPEG 1280x720 at its fastest rate (60 fps)",cam.fr[o[1]].w==1280&&cam.fr[o[1]].bestint==166666); }
    cam.ct=1; uvx_classify(0x046d,0x0825); cam.ct=0; ck("a webcam (with camera terminal) is not a capture card",!uvx.capture);
    dn=0; iface(0,0,14,1); iface(1,0,14,2); hdr(2,0);
    mjpg(1); frame(7,1,1920,1080,2,i60); unc(2,"YUY2"); frame(5,1,1920,1080,2,i60);
    iface(1,1,14,2); ep(0x81,5,1024,1); b(6,6,0x30,15,2,0x00,0xC0); iface(1,2,14,2); ep(0x81,5,1024,1); b(6,6,0x30,15,0,0x00,0x3C);
    memset(&cam,0,sizeof cam); cam.speed=4; uvc_choose(D,dn,-1); cam.ok=1; uvx_classify(0x1234,0x5678);
    ck("unknown device without camera terminal offering 1080p: capture card",uvx.capture);
    { int o[64]; int n=uvc_order(1,o); n=uvx_reorder(o,n);
      ck("USB 3 capture card: YUYV 1920x1080 at 60 fps first (fits the bandwidth)",n==2&&cam.fr[o[0]].kind==0&&cam.fr[o[0]].bestint==166666); }
    dn=0; iface(0,0,14,1); iface(1,0,14,2); hdr(1,0); unc(1,"YUY2"); frame(5,1,1920,1080,2,i60); ep(0x83,2,512,0);
    memset(&cam,0,sizeof cam); cam.speed=3; uvc_choose(D,dn,-1); cam.ok=1; uvx_classify(0x534d,0x2130);
    { int o[64]; int n=uvc_order(1,o); n=uvx_reorder(o,n); ck("bulk-only USB 2 card: YUYV 1080p kept at 30 fps (60 does not fit)",cam.bulk&&n==1&&cam.fr[o[0]].bestint==333333); }
    { uac_alt_t a[3]; memset(a,0,sizeof a); a[0].ep=0x82; a[0].ch=1; a[0].sub=2; a[0].bits=16; a[0].nrate=1; a[0].rate[0]=96000; a[1]=a[0]; a[1].ep=0x02; a[2]=a[0];
      ck("MS2109 audio: 96 kHz mono descriptor becomes 48 kHz stereo",uvx_aud_fix(0x534d,0x2109,a,2)==1&&a[0].ch==2&&a[0].rate[0]==48000&&a[0].nrate==1&&a[1].ch==1);
      ck("other devices' audio is left alone",uvx_aud_fix(0x046d,0x0825,a+2,1)==0&&a[2].ch==1&&a[2].rate[0]==96000);
      memset(&aud,0,sizeof aud); aud.nalt=1; aud.alt[0]=a[0]; ck("fixed MS2109 input is picked at 48 kHz",aud_pick(1)==0&&aud_pick_rate(&aud.alt[0])==48000); }
    printf(fails?"FAILED %d\n":"uvc ext: all ok\n",fails); return fails?1:0; }
