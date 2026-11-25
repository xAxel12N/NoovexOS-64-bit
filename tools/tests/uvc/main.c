static int fails=0; static void ck(const char* w,int c){ printf("  %-60s %s\n",w,c?"ok":"FAIL"); if(!c)fails++; }
static u8 D[2048]; static int dn;
static void b(int n,...){ __builtin_va_list a; __builtin_va_start(a,n); for(int i=0;i<n;i++)D[dn++]=(u8)__builtin_va_arg(a,int); __builtin_va_end(a); }
static void iface(int num,int alt,int cls,int sub,int proto){ b(9,9,4,num,alt,1,cls,sub,proto,0); }
static void ep(int addr,int attr,int mps,int iv){ b(7,7,5,addr,attr,mps&255,mps>>8,iv); }
static void unc(int fi,const char* cc){ b(5,27,0x24,4,fi,1); b(4,cc[0],cc[1],cc[2],cc[3]); for(int i=0;i<12;i++)b(1,0); b(6,16,0,0,0,0,0); }
static void mjpg(int fi){ b(11,11,0x24,6,fi,1,1,1,0,0,0,0); }
static void frame(int st,int ri,int w,int h){ b(5,30,0x24,st,ri,0); b(4,w&255,w>>8,h&255,h>>8); for(int i=0;i<12;i++)b(1,0); b(4,0x15,0x16,0x05,0); b(1,1); b(4,0x15,0x16,0x05,0); }
typedef struct { int unit, sel, mn, mx, df, rs, cur, nomin; } fctl_t;
static fctl_t fc[8]; static int nfc, fset_sel, fset_val, fsets, flog[16];
static int uvc_req(u8 rt,u8 req,u16 val,u16 idx,u16 len,int in,u8* data){
    int unit=idx>>8, sel=val>>8; fctl_t* c=0; for(int k=0;k<nfc;k++) if(fc[k].unit==unit&&fc[k].sel==sel)c=&fc[k]; if(!c)return 0;
    if(!in){ int v=len==1?data[0]:len==2?(short)(data[0]|(data[1]<<8)):(int)uvc_r32(data); if(len==2&&!(sel==2||sel==6))v&=0xFFFF; c->cur=v; fset_sel=sel; fset_val=v; if(fsets<16)flog[fsets]=unit*256+sel; fsets++; return 1; }
    int v; if(req==0x81)v=c->cur; else if(req==0x82){ if(c->nomin)return 0; v=c->mn; } else if(req==0x83){ if(c->nomin)return 0; v=c->mx; } else if(req==0x84)v=c->rs; else if(req==0x87)v=c->df; else return 0;
    for(int k=0;k<len;k++)data[k]=(u8)((u32)v>>(8*k)); return 1; }
static void fadd(int unit,int sel,int mn,int mx,int df,int rs,int cur,int nomin){ fctl_t* c=&fc[nfc++]; c->unit=unit; c->sel=sel; c->mn=mn; c->mx=mx; c->df=df; c->rs=rs; c->cur=cur; c->nomin=nomin; }
static void frame_iv(int st,int ri,int w,int h,int n,const u32* iv){ int bl=26+(n?n*4:12); b(5,bl,0x24,st,ri,0); b(4,w&255,w>>8,h&255,h>>8); for(int i=0;i<12;i++)b(1,0); b(4,0x15,0x16,0x05,0); b(1,n);
    for(int k=0;k<(n?n:3);k++)b(4,iv[k]&255,(iv[k]>>8)&255,(iv[k]>>16)&255,iv[k]>>24); }
static void pkt(const u8* d,int n,int info){ u8 p[64]; p[0]=2; p[1]=(u8)(0x80|info); memcpy(p+2,d,n); uvc_payload(p,n+2); }
int main(void){
    printf("camera and microphone parsing (host test of drivers/usb/xhci.c)\n");
    dn=0; iface(0,0,14,1,0); b(13,13,0x24,1,0x10,0x01,0,0,0,0,0,0,0,0);
    iface(1,0,14,2,0); unc(1,"Y800"); frame(5,1,640,360); iface(1,1,14,2,0); ep(0x81,5,1024,1);
    iface(2,0,14,2,0); mjpg(1); frame(7,1,1280,720); unc(2,"YUY2"); frame(5,1,640,480); iface(2,1,14,2,0); ep(0x82,5,1024|(2<<11),1);
    cam.speed=3; uvc_choose(D,dn,-1); cam.ok=1;
    ck("two video streams found (infrared + colour camera)",cam.nvs==2);
    ck("the colour stream is chosen by default",cam.vs_if==2&&uvc_has_color());
    ck("colour stream: MJPEG 1280x720 and YUY2 640x480",cam.nfr==2&&cam.fr[0].kind==1&&cam.fr[0].w==1280&&cam.fr[1].kind==0&&cam.fr[1].h==480);
    ck("high-bandwidth alt setting: 3 x 1024 bytes per microframe",cam.nalt==1&&cam.alt[0].esit==3072);
    cam.desc=D; cam.descn=dn; uvc_select_stream(0);
    ck("switch camera: infrared stream, grey 640x360",cam.vs_if==1&&cam.nfr==1&&cam.fr[0].kind==4&&cam.fr[0].w==640);
    const char* cc[]={"UYVY","I420","YV12","NV12","H264"}; int want[]={3,5,6,2,-1};
    for(int t=0;t<5;t++){ dn=0; iface(0,0,14,1,0); iface(1,0,14,2,0); unc(1,cc[t]); frame(5,1,320,240); iface(1,1,14,2,0); ep(0x81,5,512,1); uvc_choose(D,dn,-1); char m[80]; sprintf(m,"format %s %s",cc[t],want[t]<0?"is skipped (not supported)":"is understood"); ck(m,want[t]<0?cam.nfr==0:(cam.nfr==1&&cam.fr[0].kind==want[t])); }
    dn=0; iface(0,0,14,1,0); iface(1,0,14,2,0); unc(1,"YUY2"); frame(5,1,640,480); ep(0x83,2,512,0);
    uvc_choose(D,dn,-1); ck("bulk-only camera: bulk endpoint found",cam.bulk&&cam.bep==0x83&&cam.bmps==512&&cam.nalt==0);
    uvc_fb=malloc(4<<20); uvc_rgb=malloc(16<<20); uvc_fcap=4<<20;
    uvc_kind=3; uvc_w=2; uvc_h=1; uvc_fill=4; uvc_fb[0]=128; uvc_fb[1]=235; uvc_fb[2]=128; uvc_fb[3]=16; uvc_frames=0; uvc_frame_done();
    ck("UYVY: white and black pixels",uvc_frames==1&&uvc_rgb[0]==0xFFFFFF&&uvc_rgb[1]==0);
    uvc_kind=4; uvc_w=2; uvc_h=1; uvc_fill=2; uvc_fb[0]=200; uvc_fb[1]=7; uvc_frame_done(); ck("grey (infrared): pixel values kept",uvc_rgb[0]==0xC8C8C8&&uvc_rgb[1]==0x070707);
    uvc_kind=5; uvc_w=2; uvc_h=2; uvc_fill=6; uvc_fb[0]=uvc_fb[1]=uvc_fb[2]=uvc_fb[3]=81; uvc_fb[4]=90; uvc_fb[5]=240; uvc_frame_done(); u32 r=(uvc_rgb[0]>>16)&255,g=(uvc_rgb[0]>>8)&255,bb=uvc_rgb[0]&255;
    ck("I420: a red picture comes out red",r>230&&g<20&&bb<20);
    uvc_kind=6; uvc_fb[4]=240; uvc_fb[5]=90; uvc_frame_done(); r=(uvc_rgb[0]>>16)&255; ck("YV12: planes swapped correctly (still red)",r>230);
    uvc_kind=4; uvc_w=100; uvc_h=199; uvc_fill=0; uvc_lfid=-1; uvc_frames=0; cam.inpay=0; uvc_maxpay=0;
    { static u8 c1[16384], c2[3528]; c1[0]=12; c1[1]=0x80|0x02|1; for(int i=12;i<16384;i++)c1[i]=(u8)i; for(int i=0;i<3528;i++)c2[i]=(u8)i;
      uvc_bulk_chunk(c1,16384,16384); ck("bulk: payload continues after a full 16 KB transfer",cam.inpay==1&&uvc_frames==0);
      uvc_bulk_chunk(c2,3528,16384); ck("bulk: short transfer ends the payload and the frame",cam.inpay==0&&uvc_frames==1); }
    dn=0; iface(0,0,1,1,0); b(10,10,0x24,1,0,1,40,0,1,1,2);
    iface(1,0,1,2,0); iface(1,1,1,2,0); b(7,7,0x24,1,1,1,1,0); b(11,11,0x24,2,1,2,3,24,1,0x80,0xBB,0x00); b(9,9,5,0x84,5,192,0,1,0,0);
    aud_parse(D,dn); int k=aud_pick(1); ck("USB microphone, 24-bit stereo 48 kHz (before: refused)",k>=0&&aud.alt[k].sub==3&&aud.alt[k].ch==2&&aud_pick_rate(&aud.alt[k])==48000);
    aud_cpq=malloc(ACP_N*4); acp.ch=2; acp.bs=3; acp.rate=48000; acp.acc=0; aud_cpw=aud_cpr=0;
    { u8 fr[6]={0x00,0x34,0x12, 0x00,0xCC,0xED}; aud_take_cp(fr,6); u32 v=aud_cpq[0]; ck("24-bit samples become 16-bit (left 0x1234, right -0x1234)",aud_cpw==1&&(v&0xFFFF)==0x1234&&(short)(v>>16)==(short)0xEDCC); }
    dn=0; iface(0,0,1,1,0x20); b(9,9,0x24,1,0,2,0,0,0,0); b(8,8,0x24,0x0A,0x10,1,7,0,0);
    iface(1,0,1,2,0x20); iface(1,1,1,2,0x20); b(16,16,0x24,1,2,0,1,1,0,0,0,2,3,0,0,0,0); b(6,6,0x24,2,1,2,16); b(7,7,5,0x85,5,200,0,4);
    aud_parse(D,dn); k=aud_pick(1); ck("USB Audio 2.0 microphone understood (clock 0x10, 2 channels)",aud.v2==1&&aud.clk==0x10&&k>=0&&aud.alt[k].ch==2&&aud.alt[k].v2&&aud_pick_rate(&aud.alt[k])==48000);
    dn=0; iface(0,0,14,1,0); iface(1,0,14,2,0); iface(2,0,1,1,0); iface(3,0,1,2,0);
    ck("webcam with a microphone: both parts are seen",uvc_has_video(D,dn)&&aud_has_audio(D,dn));
    dn=0; iface(0,0,1,1,0); iface(1,0,1,3,0); ck("MIDI keyboard (control + MIDI streaming only) is not taken as a microphone",!aud_has_audio(D,dn));
    memset(&aud,0,sizeof aud); aud.nalt=2;
    aud.alt[0].ep=0x01; aud.alt[0].sub=3; aud.alt[0].bits=24; aud.alt[0].ch=2; aud.alt[0].v2=1; aud.alt[0].mps=600;
    aud.alt[1].ep=0x01; aud.alt[1].sub=4; aud.alt[1].bits=32; aud.alt[1].ch=2; aud.alt[1].v2=1; aud.alt[1].mps=600;
    ck("headset that only plays 24-bit or 32-bit is accepted for playback",aud_pick(0)>=0);
    aud.alt[2]=aud.alt[0]; aud.alt[2].sub=2; aud.alt[2].bits=16; aud.nalt=3; ck("16-bit playback still preferred when offered",aud_pick(0)==2);
    { static u32 q[256]; aud_pbq=q; for(int i=0;i<200;i++)q[i]=((u32)(u16)(short)(i*100))|((u32)(u16)(short)(-i*100)<<16); aud_pbw=200;
      int bad=0; for(int bs=2;bs<=4;bs++) for(int ch=1;ch<=2;ch++){ aud_pbr=10; aud_pacc=0; apb.rate=48000; apb.ivus=1000; apb.acc=0; apb.mps=600; apb.ch=(u32)ch; apb.bs=(u32)bs;
        u8 d[700]; u32 n=aud_fill_pb(d), fr=n/(u32)(ch*bs); if(fr!=48)bad++;
        for(u32 k=0;k<fr;k++){ int l=(short)(q[10+k]&0xFFFF), r=(short)(q[10+k]>>16); const u8* f=d+k*(u32)(ch*bs); int g0=bs==2?(short)(f[0]|f[1]<<8):bs==3?(short)(f[1]|f[2]<<8):(short)(f[2]|f[3]<<8);
          if(g0!=(ch==2?l:(l+r)/2))bad++; if(bs>=3&&f[0]!=0)bad++; if(bs==4&&f[1]!=0)bad++; if(ch==2){ const u8* g=f+bs; int g1=bs==2?(short)(g[0]|g[1]<<8):bs==3?(short)(g[1]|g[2]<<8):(short)(g[2]|g[3]<<8); if(g1!=r)bad++; } } }
      ck("playback samples packed as 16, 24 and 32 bit, mono and stereo",bad==0);
      aud_pbr=10; apb.acc=0; apb.mps=200; apb.ch=2; apb.bs=3; { u8 d[700]; u32 n=aud_fill_pb(d); ck("packet never larger than the endpoint allows (200 bytes, 24-bit stereo)",n<=200&&n%6==0&&n>0); } }
    printf("camera v2: formats, frame rates, controls, recovery\n");
    dn=0; iface(0,0,14,1,0); b(13,13,0x24,1,0x10,0x01,0,0,0,0,0,0,0,0);
    b(18,18,0x24,2,1,0x01,0x02,0,0,0,0,0,0,0,0,3,0x2A,0x00,0x02); b(11,11,0x24,5,2,1,0,0,2,0x43,0x14,0);
    iface(1,0,14,2,0); unc(1,"YUY2"); { u32 a[3]={666666,333333,166666}; frame_iv(5,1,640,480,3,a); } { u32 c3[3]={166666,2000000,166666}; frame_iv(5,2,320,240,0,c3); }
    unc(2,"NV12"); { u32 a[2]={166666,83333}; frame_iv(5,1,640,480,2,a); } mjpg(3); { u32 a[1]={333333}; frame_iv(7,1,1280,720,1,a); }
    iface(1,1,14,2,0); ep(0x81,5,1024,1);
    memset(&cam,0,sizeof cam); cam.speed=3; uvc_choose(D,dn,-1); cam.ok=1; uvc_cc_reset();
    ck("formats YUY2, NV12 and MJPEG all listed",cam.nfr==4&&cam.fr[0].kind==0&&cam.fr[2].kind==2&&cam.fr[3].kind==1);
    ck("frame rate: 30 fps chosen from 15/30/60 list",cam.fr[0].bestint==333333);
    ck("frame rate: continuous range rounded to a step at or below 30 fps",cam.fr[1].bestint==499998);
    ck("frame rate: 60 fps used when the camera only offers 60/120",cam.fr[2].bestint==166666);
    { int o[64]; int n=uvc_order(0,o); ck("best mode (normal): uncompressed 640x480 first",n==4&&cam.fr[o[0]].w==640&&cam.fr[o[0]].kind!=1);
      n=uvc_order(1,o); ck("best mode (HD): MJPEG 1280x720 first",cam.fr[o[0]].kind==1&&cam.fr[o[0]].w==1280);
      cam.fr[2].bestint=333333; n=uvc_order(0,o); ck("same size and rate: YUY2 preferred over NV12",o[0]==0); cam.fr[2].bestint=166666; n=uvc_order(0,o); ck("same size: faster frame rate preferred",o[0]==2); }
    ck("processing unit found (id 2) with brightness, contrast, PLF, WB",cam.pu==2&&uvc_ctrl_avail(UVC_C_BRIGHTNESS)&&uvc_ctrl_avail(UVC_C_CONTRAST)&&uvc_ctrl_avail(UVC_C_PLF)&&uvc_ctrl_avail(UVC_C_WB_AUTO)&&uvc_ctrl_avail(UVC_C_WB_TEMP));
    ck("camera terminal found (id 1) with exposure auto/time, focus, autofocus",cam.ct==1&&uvc_ctrl_avail(UVC_C_EXPOSURE_AUTO)&&uvc_ctrl_avail(UVC_C_EXPOSURE)&&uvc_ctrl_avail(UVC_C_FOCUS)&&uvc_ctrl_avail(UVC_C_FOCUS_AUTO));
    ck("controls the camera does not have are not offered",!uvc_ctrl_avail(UVC_C_HUE)&&!uvc_ctrl_avail(UVC_C_GAIN)&&!uvc_ctrl_avail(UVC_C_SATURATION));
    nfc=0; fadd(2,2,-64,64,0,1,-10,0); fadd(2,3,0,100,50,5,50,0); fadd(2,5,0,0,1,0,1,1); fadd(2,0x0B,0,0,1,0,1,0); fadd(2,0x0A,2800,6500,4600,10,4600,0);
    fadd(1,2,0,0,8,0x09,8,0); fadd(1,4,3,2047,156,1,156,0); fadd(1,8,0,0,1,0,1,0);
    { int mn,mx,df,v; ck("brightness: signed range -64..64 read",uvc_ctrl_range(UVC_C_BRIGHTNESS,&mn,&mx,&df)&&mn==-64&&mx==64&&uvc_ctrl_get(UVC_C_BRIGHTNESS,&v)&&v==-10);
      uvc_ctrl_set(UVC_C_BRIGHTNESS,500); ck("brightness: value above max is clamped",fset_sel==2&&fset_val==64);
      uvc_ctrl_set(UVC_C_CONTRAST,33); ck("contrast: rounded to the camera's step (5)",fset_val==35);
      ck("power line frequency: works without GET_MIN/MAX (0..2)",uvc_ctrl_range(UVC_C_PLF,&mn,&mx,&df)&&mn==0&&mx==2&&uvc_ctrl_set(UVC_C_PLF,1)&&fset_val==1);
      ck("exposure: auto reported on",uvc_ctrl_get(UVC_C_EXPOSURE_AUTO,&v)&&v==1);
      fsets=0; uvc_ctrl_set(UVC_C_EXPOSURE,300); ck("manual exposure switches auto exposure off first",fsets==2&&flog[0]==(1*256+2)&&fc[5].cur==1&&fc[6].cur==300);
      uvc_ctrl_set(UVC_C_EXPOSURE_AUTO,1); ck("auto exposure on uses aperture-priority mode the camera supports",fc[5].cur==8);
      fsets=0; uvc_ctrl_set(UVC_C_FOCUS,10); ck("control that does not answer GET requests: nothing is sent",fsets==0);
      uvc_ctrl_set(UVC_C_WB_TEMP,5000); ck("manual white balance turns auto white balance off",fc[3].cur==0&&fc[4].cur==5000);
      uvc_ctrl_set(UVC_C_FOCUS_AUTO,0); ck("autofocus can be switched off",fc[7].cur==0); }
    uvc_kind=0; uvc_w=2; uvc_h=1; uvc_fill=4; uvc_fbad=0; uvc_fb[0]=235; uvc_fb[1]=128; uvc_fb[2]=16; uvc_fb[3]=128; uvc_frames=0; uvc_frame_done();
    ck("YUYV: white and black pixels",uvc_frames==1&&uvc_rgb[0]==0xFFFFFF&&uvc_rgb[1]==0);
    { u8 px[4]={235,128,16,128}; uvc_kind=0; uvc_w=2; uvc_h=2; uvc_fill=0; uvc_fbad=0; uvc_lfid=-1; uvc_frames=0; uvc_badframes=0; uvc_short=0; uvc_hdrbad=0; memset(&cam,0,sizeof cam);
      pkt(px,4,0); pkt(px,4,2); ck("isochronous: two packets + end-of-frame give one frame",uvc_frames==1&&uvc_badframes==0);
      pkt(px,4,1); uvc_lost(); pkt(px,4,1|2); ck("dropped USB packet: the torn frame is dropped, not shown",uvc_frames==1&&uvc_badframes==1);
      pkt(px,4,0); pkt(px,4,2); ck("next frame after a drop is shown again",uvc_frames==2);
      pkt(px,4,1); { u8 bad[6]={40,0x81,1,2,3,4}; uvc_payload(bad,6); } pkt(px,4,1|2); ck("broken payload header: frame dropped and counted",uvc_badframes==2&&uvc_hdrbad==2);
      pkt(px,4,0); pkt(px,4,0); pkt(px,4,1); ck("frame-id toggle without end-of-frame still ends the frame",uvc_frames==3);
      uvc_fill=0; uvc_lfid=-1; pkt(px,4,0); pkt(px,4,0|0x40); pkt(px,4,2); ck("camera error bit: frame dropped",uvc_frames==3&&uvc_badframes==3);
      pkt(px,2,1); pkt(px,2,1|2); ck("short frame (half the bytes) is not shown",uvc_frames==3&&uvc_short==1); }
    { static u8 jb[4096]; uvc_jpg=jb; uvc_jnew=0; uvc_kind=1; uvc_fill=0; uvc_lfid=-1; uvc_frames=0; uvc_badframes=0;
      u8 nos[6]={0,0,0x12,0x34,0xFF,0xD9}; pkt(nos,6,0|2); ck("MJPEG without start-of-image marker is dropped",uvc_frames==0&&uvc_badframes==1&&!uvc_jnew);
      u8 ok[8]={0xFF,0xD8,0xFF,0xC4,0,2,0xFF,0xD9}; pkt(ok,8,1|2); ck("MJPEG frame handed to the JPEG decoder",uvc_frames==1&&uvc_jnew==1&&uvc_jlen==8&&jb[0]==0xFF&&jb[1]==0xD8); }
    { static u8 c1[512], c2[512]; uvc_kind=4; uvc_w=10; uvc_h=10; uvc_fill=0; uvc_lfid=-1; uvc_frames=0; uvc_badframes=0; cam.inpay=0; cam.bskip=0; uvc_maxpay=0;
      memset(c1,7,512); c1[0]=0; uvc_bulk_chunk(c1,512,512); ck("bulk: bad header in a full transfer -> skip to the next short transfer",cam.bskip==1&&cam.inpay==0);
      uvc_bulk_chunk(c1,512,512); uvc_bulk_chunk(c1,100,512); ck("bulk: rest of the broken payload is skipped",cam.bskip==0&&uvc_fill==0);
      c2[0]=2; c2[1]=0x80|2; memset(c2+2,9,100); uvc_bulk_chunk(c2,102,512); ck("bulk: stream resynchronised, next frame shown",uvc_frames==1); }
    printf(fails?"FAILED %d\n":"all camera and microphone checks passed\n",fails); return fails?1:0; }
