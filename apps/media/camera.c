#include "include/nvx/base.h"
#include "include/nvx/lib/base/fmt.h"
#include "include/nvx/mem/heap.h"
#include "include/nvx/drivers/video/fb/framebuffer.h"
#include "include/nvx/drivers/usb/xhci.h"
#include "include/nvx/drivers/usb/uvc.h"
#include "include/nvx/graphics/theme/theme.h"
#include "include/nvx/graphics/ui/wm.h"
#include "include/nvx/sys/native/syscall.h"
#include "include/nvx/drivers/rtc/cmos.h"
#include "include/nvx/core/sound.h"
#include "include/nvx/arch/x86_64/time/hrtimer.h"
#define AVW_ALLOC kmalloc
#define AVW_FREE kfree
#include "apps/media/avirec.h"
int jpeg_decode(const u8* buf,int len,u8* rgba,int rgbamax,int* ow,int* oh);

extern int mx, my; extern void toast(const char*);
static int cam_hd=0, cam_mirror=1, cam_auto=1, cam_shots=0, cam_jerr=0, cam_busy=0, cam_failed=0, cam_req=0;
static int cbx[40], cby[40], cbw[40], cbid[40], cbh=30, cbn=0, cam_panel=0;
long pic_png_encode(const u32* px,int stride,int w,int h,u8** out); int pic_store(const char* name,const u8* d,long n);
static int cam_vx=0, cam_vy=0, cam_vw=0, cam_vh=0;
extern const u8 uvc_std_dht[]; extern const int uvc_std_dht_len; int vid_store(const char* name,const u8* d,long n);
static avw_t cam_av; static int cam_rec=0, cam_rmic=0, cam_rdev=0, cam_vids=0; static u64 cam_rt0=0; static u32 cam_rfps=30000, cam_rlast=0; static u8* cam_jbuf=0; static long cam_jcap=0;
#define CAM_RLIM (192L<<20)
static char* cam_stamp(char* q,const char* pre){ int hh=12,mm=0,ss=0,yr=2026,mo=1,dy=1; read_clock(&hh,&mm,&ss,&yr,&mo,&dy);
    q=scopy(q,pre); q=udec(q,(u32)yr); *q++='-'; *q++=(char)('0'+mo/10); *q++=(char)('0'+mo%10); *q++='-'; *q++=(char)('0'+dy/10); *q++=(char)('0'+dy%10); *q++='-';
    *q++=(char)('0'+hh/10); *q++=(char)('0'+hh%10); *q++=(char)('0'+mm/10); *q++=(char)('0'+mm%10); *q++=(char)('0'+ss/10); *q++=(char)('0'+ss%10); *q=0; return q; }
static void cam_rec_stop(void){
    if(!cam_rec)return; cam_rec=0; if(cam_rmic==2)snd_rec_stop(); cam_rmic=0;
    long n=avw_finish(&cam_av,cam_rfps); char nm[64],*q=cam_stamp(nm,"Video-"); *q++='-'; q=udec(q,(u32)++cam_vids); q=scopy(q,".avi"); *q=0;
    int r=n>0?vid_store(nm,cam_av.d,n):-1; avw_free(&cam_av); if(cam_jbuf){ kfree(cam_jbuf); cam_jbuf=0; cam_jcap=0; }
    char t[96]; q=t; q=scopy(q,r>=0?"Saved to Videos: ":"Could not save the video "); q=scopy(q,nm); *q=0; toast(t); }
static void cam_rec_start(void){
    if(cam_rec||uvc_state!=2||uvc_w<=0||uvc_h<=0)return;
    cam_rfps=uvc_interval?(u32)(10000000000ULL/uvc_interval):30000u; if(cam_rfps<1000)cam_rfps=1000; if(cam_rfps>60000)cam_rfps=60000;
    if(uvc_kind!=1){ cam_jcap=(long)uvc_w*uvc_h*3+65536; cam_jbuf=(u8*)kmalloc((unsigned long)cam_jcap); if(!cam_jbuf){ toast("Recording: not enough memory"); return; } }
    cam_rmic=0; if(snd_rec_avail()){ if(g_cap_on)cam_rmic=1; else if(snd_rec_start())cam_rmic=2; }
    if(avw_begin(&cam_av,uvc_w,uvc_h,cam_rmic!=0,48000,2,CAM_RLIM)<0){ avw_free(&cam_av); if(cam_rmic==2)snd_rec_stop(); cam_rmic=0; if(cam_jbuf){ kfree(cam_jbuf); cam_jbuf=0; } toast("Recording: not enough memory"); return; }
    if(cam_rmic){ u8 t[4096]; for(int g=0;g<64&&snd_cap_read(t,(int)sizeof t)>0;g++){} }
    cam_rdev=uvc_selected(); cam_rlast=uvc_kind==1?uvc_frames:uvc_seq; cam_rt0=hr_ns(); cam_rec=1; }
static void cam_rec_pump(void){
    if(!cam_rec)return; if(uvc_state!=2||uvc_selected()!=cam_rdev){ cam_rec_stop(); return; }
    if(cam_rmic){ u8 t[8192]; for(int g=0;g<32;g++){ int n=snd_cap_read(t,(int)sizeof t); if(n<=0)break; n&=~3; if(cam_av.n+n+(1L<<20)>CAM_RLIM)break; avw_audio(&cam_av,t,n); } }
    const u8* fr=0; long fl=0;
    if(uvc_kind==1){ if(uvc_frames!=cam_rlast&&uvc_jpg&&uvc_jlen>4&&uvc_jpg[0]==0xFF&&uvc_jpg[1]==0xD8){ cam_rlast=uvc_frames; fr=uvc_jpg; fl=(long)uvc_jlen; } }
    else if(uvc_seq!=cam_rlast&&uvc_rgb&&cam_jbuf&&uvc_w==cam_av.w&&uvc_h==cam_av.h){ cam_rlast=uvc_seq; fl=jpeg_encode(uvc_rgb,uvc_w,uvc_w,uvc_h,80,uvc_std_dht,uvc_std_dht_len,cam_jbuf,cam_jcap); fr=fl>0?cam_jbuf:0; }
    if(!fr)return;
    if(cam_av.n+fl+(2L<<20)>CAM_RLIM){ toast("Recording stopped: the video reached its size limit"); cam_rec_stop(); return; }
    u32 due=avw_due(hr_ns()-cam_rt0,cam_rfps); for(int g=0;g<120&&cam_av.vchunks+1<due;g++)avw_video(&cam_av,0,0);
    if(cam_av.vchunks<=due)avw_video(&cam_av,fr,fl);
    if(cam_av.err){ toast("Recording stopped: not enough memory"); cam_av.err=0; cam_rec_stop(); } }

static void cam_decode(void){
    if(!uvc_jnew||!uvc_jpg||!uvc_rgb)return;
    int w=0,h=0; int r=jpeg_decode(uvc_jpg,(int)uvc_jlen,(u8*)uvc_rgb,1920*1200*4,&w,&h);
    if(r==0&&w==uvc_w&&h==uvc_h){ u8* b=(u8*)uvc_rgb; u32 n=(u32)w*(u32)h; for(u32 i=0;i<n;i++){ u8* p=b+i*4; uvc_rgb[i]=((u32)p[0]<<16)|((u32)p[1]<<8)|(u32)p[2]; } uvc_seq++; }
    else cam_jerr++;
    uvc_jnew=0; }
static int cam_bid=0;
static void cam_btn(int x,int y,int w,const char* t,int on){
    int hov=mx>=x&&mx<x+w&&my>=y&&my<y+cbh;
    fill_round(x,y,w,cbh,8,15,on?ACC:hov?rgb(214,220,232):rgb(226,230,240));
    dstr_ctr(x+w/2,y+9,t,on?rgb(255,255,255):rgb(30,34,46),1);
    if(cbn<40){ cbx[cbn]=x; cby[cbn]=y; cbw[cbn]=w; cbid[cbn]=cam_bid; cbn++; } }
static void cam_line(int x,int* y,const char* a,const char* b){ dstr(x,*y,a,rgb(110,118,138),1); dstr(x+96,*y,b,rgb(30,34,46),1); *y+=18; }
static char* cam_hex(char* p,u32 v){ const char* H="0123456789ABCDEF"; for(int s=12;s>=0;s-=4)*p++=H[(v>>s)&15]; *p=0; return p; }
static void cam_view(int x,int y,int w,int h){
    cam_vx=x; cam_vy=y; cam_vw=w; cam_vh=h;
    if(uvc_state!=2||!uvc_rgb||uvc_w<=0||uvc_h<=0){ fill_rect(x,y,w,h,rgb(16,18,24)); return; }
    int dw=w, dh=w*uvc_h/uvc_w; if(dh>h){ dh=h; dw=h*uvc_w/uvc_h; } int ox=x+(w-dw)/2, oy=y+(h-dh)/2;
    fill_rect(x,y,w,h,rgb(8,9,12));
    u32* sc=SCENE; for(int yy=0;yy<dh;yy++){ int sy=oy+yy; if((u32)sy>=H)continue; const u32* src=uvc_rgb+(u64)(yy*uvc_h/dh)*uvc_w; u32* d=sc+(u64)sy*SW;
        for(int xx=0;xx<dw;xx++){ int sx=ox+xx; if((u32)sx>=W)continue; int u=xx*uvc_w/dw; if(cam_mirror)u=uvc_w-1-u; d[sx]=src[u]; } } }
static void cam_nocam(int x,int y,int w,int h){
    fill_rect(x,y,w,h,rgb(20,22,30)); int cx=x+w/2, cy=y+h/2-70;
    fill_round(cx-40,cy-26,80,52,10,15,rgb(60,66,84)); fill_circle(cx,cy,18,rgb(30,34,46)); fill_circle(cx,cy,10,rgb(90,140,230)); fill_round(cx-14,cy-34,28,10,4,15,rgb(60,66,84));
    const char* head=!uvc_found()?"No camera found":cam_failed?"The camera did not start":(cam_auto||cam_req)?"Starting camera...":"Camera ready - press Start";
    const char* sub=uvc_msg[0]&&!seq(uvc_msg,head)&&!(!uvc_found()&&seq(uvc_msg,"No camera found"))?uvc_msg:"Plug in a USB camera or check that the laptop camera is turned on.";
    dstr_ctr(cx,cy+48,head,rgb(230,234,242),2);
    dstr_ctr(cx,cy+80,sub,rgb(160,168,186),1);
    int yy=cy+108; int n=0; for(int k=0;k<g_usbdev_n;k++) if(g_usbdev[k].used){ char b[96],*p=b; p=scopy(p,g_usbdev[k].kind); p=scopy(p,": "); p=scopy(p,g_usbdev[k].name[0]?g_usbdev[k].name:"(no name)"); p=scopy(p,"  "); p=cam_hex(p,g_usbdev[k].vid); *p++=':'; p=cam_hex(p,g_usbdev[k].pid); *p=0; dstr_ctr(cx,yy,b,rgb(130,138,156),1); yy+=16; n++; if(n>6)break; }
    if(!n) dstr_ctr(cx,yy,"No USB devices were detected.",rgb(130,138,156),1); }
static void cam_try_start(void){
    if(cam_busy)return; cam_busy=1; int r=uvc_start(cam_hd); cam_busy=0; cam_failed=r<0; }
void camera_draw(int i){
    win_frame(i,"Camera"); cbn=0;
    if(uvc_found()&&cam_auto==1&&uvc_state==1&&!cam_failed&&!cam_busy){ cam_auto=0; cam_req=1; }
    if(uvc_kind==1)cam_decode();
    int wx=WN[i].x+1, wy=WN[i].y+TITLEH, ww=WN[i].w-2, wh=WN[i].h-TITLEH-1;
    int infow=ww>=900?300:0; int vw=ww-infow-20, vh=wh-62;
    fill_rect(wx,wy,ww,wh,rgb(243,244,248));
    if(uvc_state==2) cam_view(wx+10,wy+10,vw,vh); else cam_nocam(wx+10,wy+10,vw,vh);
    if(cam_rec){ fill_circle(wx+30,wy+30,8,rgb(220,40,40)); dstr(wx+44,wy+24,cam_rmic?"REC":"REC (no sound)",rgb(255,255,255),1); }
    int by=wy+wh-44, bx=wx+10;
    cam_bid=0; cam_btn(bx,by,96,uvc_state==2?"Stop":"Start",uvc_state!=2); bx+=104;
    cam_bid=1; cam_btn(bx,by,96,cam_hd?"HD (MJPEG)":"SD",cam_hd); bx+=104;
    cam_bid=2; cam_btn(bx,by,96,"Mirror",cam_mirror); bx+=104;
    cam_bid=3; cam_btn(bx,by,120,"Take photo",0); bx+=128;
    { extern int uvc_streams(void); if(uvc_streams()>1){ cam_bid=4; char sb[32],*sq=sb; extern int uvc_stream_cur(void); sq=scopy(sq,"Camera "); sq=udec(sq,(u32)uvc_stream_cur()+1); *sq++='/'; sq=udec(sq,(u32)uvc_streams()); *sq=0; cam_btn(bx,by,110,sb,0); bx+=118; } }
    cam_bid=5; cam_btn(bx,by,96,"Settings",cam_panel); bx+=104;
    { char rb[32],*rq=rb; if(cam_rec){ u32 s2=(u32)((hr_ns()-cam_rt0)/1000000000ULL); rq=scopy(rq,"Stop "); rq=udec(rq,s2/60); *rq++=':'; *rq++=(char)('0'+(s2%60)/10); *rq++=(char)('0'+s2%10); } else rq=scopy(rq,"Record"); *rq=0; cam_bid=7; cam_btn(bx,by,96,rb,cam_rec); bx+=104; }
    if(uvc_count()>1){ char db[32],*dq=db; dq=scopy(dq,"Device "); dq=udec(dq,(u32)uvc_selected()+1); *dq++='/'; dq=udec(dq,(u32)UVC_NCAM); *dq=0; cam_bid=6; cam_btn(bx,by,104,db,0); bx+=112; }
    char st[96],*p=st;
    if(uvc_state==2){ p=udec(p,(u32)uvc_w); *p++='x'; p=udec(p,(u32)uvc_h); extern const char* uvc_kind_name(int); *p++=' '; *p++=' '; p=scopy(p,uvc_kind_name(uvc_kind)); *p++=' '; *p++=' '; p=udec(p,uvc_fps10/10); *p++='.'; p=udec(p,uvc_fps10%10); p=scopy(p," fps"); }
    else p=scopy(p,uvc_found()?uvc_name:"No camera"); *p=0;
    dstr(bx+6,by+9,st,rgb(70,78,98),1);
    if(cam_panel){ int pw=300, px0=wx+ww-pw-16, py0=wy+16, n=0; for(int c=0;c<UVC_C_N;c++) if(uvc_ctrl_avail(c))n++;
        int ph=48+(n?n:1)*34; fill_round(px0,py0,pw,ph,10,15,rgb(250,251,253)); dstr(px0+14,py0+12,"Camera settings",rgb(30,34,46),2); int yy=py0+44;
        if(!n) dstr(px0+14,yy+8,uvc_found()?"This camera has no adjustable controls":"No camera",rgb(110,118,138),1);
        for(int c=0;c<UVC_C_N;c++){ if(!uvc_ctrl_avail(c))continue; int mn=0,mxv=0,df=0,v=0; char vb[24],*q=vb; dstr(px0+14,yy+9,uvc_ctrl_name(c),rgb(60,66,84),1);
            if(uvc_ctrl_range(c,&mn,&mxv,&df)&&uvc_ctrl_get(c,&v)){ if(v<0){ *q++='-'; q=udec(q,(u32)-v); } else q=udec(q,(u32)v); } else q=scopy(q,"-"); *q=0;
            dstr(px0+pw-130,yy+9,vb,rgb(30,34,46),1); cam_bid=100+c*2; cam_btn(px0+pw-78,yy,30,"-",0); cam_bid=101+c*2; cam_btn(px0+pw-44,yy,30,"+",0); yy+=34; } }
    if(infow&&!cam_panel){ int ix=wx+ww-infow, iy=wy+14; fill_rect(ix-6,wy,1,wh,rgb(226,229,238));
        dstr(ix,iy,"Camera details",rgb(30,34,46),2); iy+=30; char b[64],*q;
        cam_line(ix,&iy,"Name",uvc_found()?uvc_name:"-");
        q=b; q=cam_hex(q,uvc_vid); *q++=':'; q=cam_hex(q,uvc_pid); *q=0; cam_line(ix,&iy,"USB id",uvc_found()?b:"-");
        int sp=uvc_speed(); cam_line(ix,&iy,"USB speed",!uvc_found()?"-":sp>=4?"SuperSpeed":sp==3?"High speed":"Full speed");
        q=b; q=udec(q,(u32)uvc_nfr()); q=scopy(q," sizes, "); q=udec(q,(u32)uvc_nalt()); q=scopy(q," modes"); *q=0; cam_line(ix,&iy,"Formats",b);
        q=b; q=udec(q,(u32)uvc_altsel); q=scopy(q,"  payload "); q=udec(q,uvc_maxpay); *q=0; cam_line(ix,&iy,"Stream mode",b);
        q=b; q=udec(q,uvc_frames); *q=0; cam_line(ix,&iy,"Frames",b);
        q=b; q=udec(q,uvc_badframes); q=scopy(q," bad, "); q=udec(q,uvc_short+(u32)cam_jerr); q=scopy(q," incomplete"); *q=0; cam_line(ix,&iy,"Dropped",b);
        q=b; q=udec(q,uvc_tds); q=scopy(q," ok, "); q=udec(q,uvc_tderr); q=scopy(q," err"); *q=0; cam_line(ix,&iy,"USB packets",b);
        q=b; q=udec(q,uvc_bytes/1024u); q=scopy(q," KB  cc "); q=udec(q,(u32)uvc_lastcc); *q=0; cam_line(ix,&iy,"Data",b);
        iy+=8; dstr(ix,iy,"Sizes",rgb(30,34,46),1); iy+=18;
        for(int k=0;k<uvc_nfr()&&iy<wy+wh-60;k++){ int kd,w,h; u32 di; if(!uvc_frame_info(k,&kd,&w,&h,&di))continue; q=b; { extern const char* uvc_kind_name(int); q=scopy(q,uvc_kind_name(kd)); *q++=' '; } q=udec(q,(u32)w); *q++='x'; q=udec(q,(u32)h); if(di){ q=scopy(q,"  "); q=udec(q,10000000u/di); q=scopy(q," fps"); } *q=0;
            dstr(ix,iy,b,(w==uvc_w&&h==uvc_h&&kd==uvc_kind&&uvc_state==2)?ACC:rgb(90,98,118),1); iy+=16; } } }
static int cam_photo(void){
    if(uvc_state!=2||!uvc_rgb||uvc_w<=0||uvc_h<=0)return -1; int w=uvc_w, h=uvc_h; u32* img=(u32*)kmalloc((unsigned long)w*(unsigned long)h*4); if(!img){ toast("Photo: not enough memory"); return -1; }
    for(int y=0;y<h;y++){ const u32* r=uvc_rgb+(u64)y*(u64)w; u32* d=img+(u64)y*(u64)w; for(int x=0;x<w;x++)d[x]=r[cam_mirror?w-1-x:x]&0x00FFFFFFu; }
    u8* png=0; long n=pic_png_encode(img,w,w,h,&png); kfree(img); if(n<0){ toast("Photo: not enough memory"); return -1; }
    int hh=12,mm=0,ss=0,yr=2026,mo=1,dy=1; read_clock(&hh,&mm,&ss,&yr,&mo,&dy);
    char nm[64],*q=nm; q=scopy(q,"Photo-"); q=udec(q,(u32)yr); *q++='-'; *q++=(char)('0'+mo/10); *q++=(char)('0'+mo%10); *q++='-'; *q++=(char)('0'+dy/10); *q++=(char)('0'+dy%10); *q++='-';
    *q++=(char)('0'+hh/10); *q++=(char)('0'+hh%10); *q++=(char)('0'+mm/10); *q++=(char)('0'+mm%10); *q++=(char)('0'+ss/10); *q++=(char)('0'+ss%10); *q++='-'; q=udec(q,(u32)++cam_shots); q=scopy(q,".png"); *q=0;
    int r=pic_store(nm,png,n); kfree(png);
    char t[96]; q=t; q=scopy(q,r>=0?"Saved to Pictures: ":"Could not save "); q=scopy(q,nm); *q=0; toast(t); return r; }
static void cam_ctrl_step(int c,int dir){ int mn,mxv,df,v; if(!uvc_ctrl_range(c,&mn,&mxv,&df)||!uvc_ctrl_get(c,&v))return; int st=(mxv-mn)/16; if(st<1)st=1; v+=dir*st; if(v<mn)v=mn; if(v>mxv)v=mxv; if(!uvc_ctrl_set(c,v))toast("The camera did not accept the change"); }
void camera_click(int px,int py){
    for(int k=0;k<cbn;k++){ if(px>=cbx[k]&&px<cbx[k]+cbw[k]&&py>=cby[k]&&py<cby[k]+cbh){
        int id=cbid[k];
        if(id>=100){ int c=(id-100)/2; cam_ctrl_step(c,(id&1)?1:-1); }
        else if(id==5) cam_panel^=1;
        else if(id==0){ if(uvc_state==2){ cam_rec_stop(); uvc_stop(); } else if(uvc_found()){ cam_failed=0; cam_req=1; } else toast("No camera found"); }
        else if(id==7){ if(cam_rec)cam_rec_stop(); else cam_rec_start(); }
        else if(id==6){ cam_rec_stop(); for(int s2=1;s2<=UVC_NCAM;s2++){ int ni=(uvc_selected()+s2)%UVC_NCAM; if(uvc_cam_ok(ni)){ uvc_select(ni); if(uvc_state!=2)cam_req=1; cam_failed=0; break; } } }
        else if(id==1){ cam_rec_stop(); cam_hd^=1; if(uvc_state==2)cam_req=1; }
        else if(id==2) cam_mirror^=1;
        else if(id==3) cam_photo();
        else if(id==4){ extern int uvc_streams(void), uvc_stream_cur(void), uvc_select_stream(int); int n=uvc_streams(); if(n>1){ int was=uvc_select_stream((uvc_stream_cur()+1)%n); if(was>=0)cam_req=1; } }
        return; } } }
void camera_key(int c){ if(c==' ')cam_photo(); else if(c=='m'||c=='M')cam_mirror^=1; else if(c=='h'||c=='H'){ cam_rec_stop(); cam_hd^=1; if(uvc_state==2)cam_req=1; } else if(c=='r'||c=='R'){ if(cam_rec)cam_rec_stop(); else cam_rec_start(); } }
void camera_open(void){ if(!WN[W_CAMERA].open){ cam_auto=1; cam_failed=0; cam_req=0; } open_win(W_CAMERA); }
void camera_closed(void){ cam_rec_stop(); uvc_stop(); cam_auto=1; cam_failed=0; cam_req=0; }
int camera_tick(void){
    extern int drag, g_resize; static int settle=0;
    if(cam_rec&&WN[W_CAMERA].open)cam_rec_pump();
    if(!WN[W_CAMERA].open){ cam_req=0; settle=0; return 0; }
    if(drag>=0||g_resize>=0){ settle=1; return 0; }
    if(settle){ settle=0; return 0; }
    if(!cam_req||cam_busy)return 0;
    cam_req=0; if(!uvc_found())return 0; cam_try_start(); return 1; }
int g_drag_settle=0;
int app_io_tick(void){
    extern int drag, g_resize; extern int files_vol_tick(void);
    if(drag>=0||g_resize>=0)g_drag_settle=2; else if(g_drag_settle)g_drag_settle--;
    int ch=camera_tick(); if(files_vol_tick())ch=1; { extern int wifi_gui_tick(void); extern int settings_defer_tick(void); if(wifi_gui_tick())ch=1; if(settings_defer_tick())ch=1; } return ch; }
