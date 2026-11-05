static int fails=0; static void ck(const char* w,int c){ printf("  %-60s %s\n",w,c?"ok":"FAIL"); if(!c)fails++; }
static u8 D[2048]; static int dn;
static void b(int n,...){ __builtin_va_list a; __builtin_va_start(a,n); for(int i=0;i<n;i++)D[dn++]=(u8)__builtin_va_arg(a,int); __builtin_va_end(a); }
static void iface(int num,int alt,int cls,int sub,int proto){ b(9,9,4,num,alt,1,cls,sub,proto,0); }
static void ep(int addr,int attr,int mps,int iv){ b(7,7,5,addr,attr,mps&255,mps>>8,iv); }
static void unc(int fi,const char* cc){ b(5,27,0x24,4,fi,1); b(4,cc[0],cc[1],cc[2],cc[3]); for(int i=0;i<12;i++)b(1,0); b(6,16,0,0,0,0,0); }
static void mjpg(int fi){ b(11,11,0x24,6,fi,1,1,1,0,0,0,0); }
static void frame(int st,int ri,int w,int h){ b(5,30,0x24,st,ri,0); b(4,w&255,w>>8,h&255,h>>8); for(int i=0;i<12;i++)b(1,0); b(4,0x15,0x16,0x05,0); b(1,1); b(4,0x15,0x16,0x05,0); }
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
    printf(fails?"FAILED %d\n":"all camera and microphone checks passed\n",fails); return fails?1:0; }
