#include "include/nvx/base.h"
#include "include/nvx/lib/fmt.h"
#include "include/nvx/mm/heap.h"
#include "include/nvx/drivers/video/framebuffer.h"
#include "include/nvx/drivers/usb/xhci.h"
#include "include/nvx/drivers/usb/uvc.h"
#include "include/nvx/gui/theme.h"
#include "include/nvx/gui/wm.h"
#include "include/nvx/core/syscall.h"
int jpeg_decode(const u8* buf,int len,u8* rgba,int rgbamax,int* ow,int* oh);

extern int mx, my; extern void toast(const char*);
static int cam_hd=0, cam_mirror=1, cam_auto=1, cam_shots=0, cam_jerr=0;
static int cbx[6], cby[6], cbw[6], cbh=30, cbn=0;
static int cam_vx=0, cam_vy=0, cam_vw=0, cam_vh=0;

static void cam_decode(void){
    if(!uvc_jnew||!uvc_jpg||!uvc_rgb)return;
    int w=0,h=0; int r=jpeg_decode(uvc_jpg,(int)uvc_jlen,(u8*)uvc_rgb,1920*1200*4,&w,&h);
    if(r==0&&w==uvc_w&&h==uvc_h){ u8* b=(u8*)uvc_rgb; u32 n=(u32)w*(u32)h; for(u32 i=0;i<n;i++){ u8* p=b+i*4; uvc_rgb[i]=((u32)p[0]<<16)|((u32)p[1]<<8)|(u32)p[2]; } uvc_seq++; }
    else cam_jerr++;
    uvc_jnew=0; }
static void cam_btn(int x,int y,int w,const char* t,int on){
    int hov=mx>=x&&mx<x+w&&my>=y&&my<y+cbh;
    fill_round(x,y,w,cbh,8,15,on?ACC:hov?rgb(214,220,232):rgb(226,230,240));
    dstr_ctr(x+w/2,y+9,t,on?rgb(255,255,255):rgb(30,34,46),1);
    if(cbn<6){ cbx[cbn]=x; cby[cbn]=y; cbw[cbn]=w; cbn++; } }
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
    dstr_ctr(cx,cy+48,uvc_found()?"Camera ready - press Start":"No camera found",rgb(230,234,242),2);
    dstr_ctr(cx,cy+80,uvc_msg[0]?uvc_msg:"Plug in a USB camera or check that the laptop camera is turned on.",rgb(160,168,186),1);
    int yy=cy+108; int n=0; for(int k=0;k<g_usbdev_n;k++) if(g_usbdev[k].used){ char b[96],*p=b; p=scopy(p,g_usbdev[k].kind); p=scopy(p,": "); p=scopy(p,g_usbdev[k].name[0]?g_usbdev[k].name:"(no name)"); p=scopy(p,"  "); p=cam_hex(p,g_usbdev[k].vid); *p++=':'; p=cam_hex(p,g_usbdev[k].pid); *p=0; dstr_ctr(cx,yy,b,rgb(130,138,156),1); yy+=16; n++; if(n>6)break; }
    if(!n) dstr_ctr(cx,yy,"No USB devices were detected.",rgb(130,138,156),1); }
void camera_draw(int i){
    win_frame(i,"Camera"); cbn=0;
    if(uvc_found()&&cam_auto&&uvc_state==1){ cam_auto=0; uvc_start(cam_hd); }
    uvc_poll(); if(uvc_kind==1)cam_decode();
    int wx=WN[i].x+1, wy=WN[i].y+TITLEH, ww=WN[i].w-2, wh=WN[i].h-TITLEH-1;
    int infow=ww>=900?300:0; int vw=ww-infow-20, vh=wh-62;
    fill_rect(wx,wy,ww,wh,rgb(243,244,248));
    if(uvc_state==2) cam_view(wx+10,wy+10,vw,vh); else cam_nocam(wx+10,wy+10,vw,vh);
    int by=wy+wh-44, bx=wx+10;
    cam_btn(bx,by,96,uvc_state==2?"Stop":"Start",uvc_state!=2); bx+=104;
    cam_btn(bx,by,96,cam_hd?"HD (MJPEG)":"SD",cam_hd); bx+=104;
    cam_btn(bx,by,96,"Mirror",cam_mirror); bx+=104;
    cam_btn(bx,by,120,"Take photo",0); bx+=128;
    char st[96],*p=st;
    if(uvc_state==2){ p=udec(p,(u32)uvc_w); *p++='x'; p=udec(p,(u32)uvc_h); p=scopy(p,uvc_kind==1?"  MJPEG  ":uvc_kind==2?"  NV12  ":"  YUY2  "); p=udec(p,uvc_fps10/10); *p++='.'; p=udec(p,uvc_fps10%10); p=scopy(p," fps"); }
    else p=scopy(p,uvc_found()?uvc_name:"No camera"); *p=0;
    dstr(bx+6,by+9,st,rgb(70,78,98),1);
    if(infow){ int ix=wx+ww-infow, iy=wy+14; fill_rect(ix-6,wy,1,wh,rgb(226,229,238));
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
        for(int k=0;k<uvc_nfr()&&iy<wy+wh-60;k++){ int kd,w,h; u32 di; if(!uvc_frame_info(k,&kd,&w,&h,&di))continue; q=b; q=scopy(q,kd==1?"MJPEG ":kd==2?"NV12  ":"YUY2  "); q=udec(q,(u32)w); *q++='x'; q=udec(q,(u32)h); if(di){ q=scopy(q,"  "); q=udec(q,10000000u/di); q=scopy(q," fps"); } *q=0;
            dstr(ix,iy,b,(w==uvc_w&&h==uvc_h&&kd==uvc_kind&&uvc_state==2)?ACC:rgb(90,98,118),1); iy+=16; } } }
static u32 crc_t[256]; static int crc_ok=0;
static u32 crc32b(u32 c,const u8* p,u32 n){ if(!crc_ok){ for(u32 i=0;i<256;i++){ u32 v=i; for(int k=0;k<8;k++)v=(v&1)?0xEDB88320u^(v>>1):v>>1; crc_t[i]=v; } crc_ok=1; } c=~c; for(u32 i=0;i<n;i++)c=crc_t[(c^p[i])&255]^(c>>8); return ~c; }
static u8* be32(u8* p,u32 v){ p[0]=(u8)(v>>24); p[1]=(u8)(v>>16); p[2]=(u8)(v>>8); p[3]=(u8)v; return p+4; }
static int cam_photo(void){
    if(uvc_state!=2||!uvc_rgb)return -1; int w=uvc_w, h=uvc_h; u32 raw=(u32)(w*3+1)*(u32)h; u32 nb=(raw+65534u)/65535u;
    u32 sz=8+25+12+(2+raw+nb*5+4)+12+64; u8* o=(u8*)kmalloc(sz); if(!o)return -1; u8* p=o;
    static const u8 sig[8]={137,80,78,71,13,10,26,10}; for(int k=0;k<8;k++)*p++=sig[k];
    p=be32(p,13); u8* c0=p; *p++='I';*p++='H';*p++='D';*p++='R'; p=be32(p,(u32)w); p=be32(p,(u32)h); *p++=8; *p++=2; *p++=0; *p++=0; *p++=0; p=be32(p,crc32b(0,c0,17));
    u32 zl=2+raw+nb*5+4; p=be32(p,zl); u8* c1=p; *p++='I';*p++='D';*p++='A';*p++='T'; *p++=0x78; *p++=0x01;
    u32 a1=1,a2=0, left=raw, row=0, col=0;
    while(left){ u32 n=left>65535u?65535u:left; left-=n; *p++=left?0:1; *p++=(u8)n; *p++=(u8)(n>>8); *p++=(u8)~n; *p++=(u8)(~n>>8);
        for(u32 k=0;k<n;k++){ u8 v; if(col==0)v=0; else { int x=(int)(col-1)/3, ch=(int)(col-1)%3; int sx=cam_mirror?w-1-x:x; u32 px=uvc_rgb[(u64)row*w+sx]; v=(u8)(ch==0?px>>16:ch==1?px>>8:px); }
            *p++=v; a1=(a1+v)%65521u; a2=(a2+a1)%65521u; col++; if(col==(u32)w*3u+1u){ col=0; row++; } } }
    p=be32(p,(a2<<16)|a1); p=be32(p,crc32b(0,c1,zl+4));
    p=be32(p,0); u8* c2=p; *p++='I';*p++='E';*p++='N';*p++='D'; p=be32(p,crc32b(0,c2,4));
    char nm[32],*q=nm; q=scopy(q,"photo-"); q=udec(q,(u32)++cam_shots); q=scopy(q,".png"); *q=0;
    int r=fs_save_bin(nm,o,(int)(p-o),0); kfree(o);
    char t[64]; q=t; q=scopy(q,r>=0?"Saved ":"Could not save "); q=scopy(q,nm); *q=0; toast(t); return r; }
void camera_click(int px,int py){
    for(int k=0;k<cbn;k++){ if(px>=cbx[k]&&px<cbx[k]+cbw[k]&&py>=cby[k]&&py<cby[k]+cbh){
        if(k==0){ if(uvc_state==2)uvc_stop(); else uvc_start(cam_hd); }
        else if(k==1){ cam_hd^=1; if(uvc_state==2)uvc_start(cam_hd); }
        else if(k==2) cam_mirror^=1;
        else if(k==3) cam_photo();
        return; } } }
void camera_key(int c){ if(c==' ')cam_photo(); else if(c=='m'||c=='M')cam_mirror^=1; else if(c=='h'||c=='H'){ cam_hd^=1; if(uvc_state==2)uvc_start(cam_hd); } }
void camera_open(void){ cam_auto=1; open_win(W_CAMERA); }
void camera_closed(void){ uvc_stop(); cam_auto=1; }
