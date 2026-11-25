static int fails=0; static void ck(const char* w,int c){ printf("  %-66s %s\n",w,c?"ok":"FAIL"); if(!c)fails++; }
static u8 D[2048]; static int dn;
static void b(int n,...){ __builtin_va_list a; __builtin_va_start(a,n); for(int i=0;i<n;i++)D[dn++]=(u8)__builtin_va_arg(a,int); __builtin_va_end(a); }
static void iface(int num,int alt,int cls,int sub,int proto){ b(9,9,4,num,alt,1,cls,sub,proto,0); }
static void ep(int addr,int attr,int mps,int iv){ b(7,7,5,addr,attr,mps&255,mps>>8,iv); }
static void unc(int fi,const char* cc){ b(5,27,0x24,4,fi,1); b(4,cc[0],cc[1],cc[2],cc[3]); for(int i=0;i<12;i++)b(1,0); b(6,16,0,0,0,0,0); }
static void mjpg(int fi){ b(11,11,0x24,6,fi,1,1,1,0,0,0,0); }
static void frame(int st,int ri,int w,int h){ b(5,30,0x24,st,ri,0); b(4,w&255,w>>8,h&255,h>>8); for(int i=0;i<12;i++)b(1,0); b(4,0x15,0x16,0x05,0); b(1,1); b(4,0x15,0x16,0x05,0); }
static void xu(int unit,int nb,int b0,int b1){ b(4,25+nb,0x24,6,unit); for(int i=0;i<16;i++)b(1,0x10+i); b(4,8,1,1,nb); b(1,b0); if(nb>1)b(1,b1); b(1,0); }
static int simu=0x0E, sims=6, simon=0, sets=0, simro=0; static u8 simcur[9], simmax[9]={3,3,3,0,0,0,0,0,0};
static int uvc_req(u8 rt,u8 req,u16 val,u16 idx,u16 len,int in,u8* data){
    int unit=idx>>8, sel=val>>8; (void)in;
    if(unit!=simu||sel!=sims){ if(req==0x86){ data[0]=1; return 1; } if(req==0x85){ data[0]=2; data[1]=0; return 1; } if(req==0x81){ data[0]=data[1]=0; return 1; } if(rt==0x21){ sets++; return 1; } return 0; }
    if(req==0x86){ data[0]=simro?1:3; return 1; } if(req==0x85){ data[0]=9; data[1]=0; return 1; }
    if(req==0x81){ for(int k=0;k<len;k++)data[k]=simcur[k]; return 1; } if(req==0x83){ for(int k=0;k<len;k++)data[k]=simmax[k]; return 1; }
    if(rt==0x21&&req==1){ sets++; for(int k=0;k<len;k++)simcur[k]=data[k]; simon=simcur[0]==1&&simcur[1]==0; return 1; }
    return 0; }
static u32 fr[64*48]; static int fno;
static void shoot(int n){ for(int i=0;i<n;i++){ fno++; int lit=simon&&(fno&1); for(int p=0;p<64*48;p++)fr[p]=(u32)((30+(p%7)+(lit?90:0))*0x010101); uvc_seq++; uvc_ir_poll(); } }
int main(void){
    printf("infrared (Windows Hello) camera: stream detection, light control search, Face ID glue\n");
    dn=0; iface(0,0,14,1,0); b(13,13,0x24,1,0x10,0x01,0,0,0,0,0,0,0,0); xu(4,1,0x03,0);
    iface(1,0,14,2,0); mjpg(1); frame(7,1,1280,720); iface(1,1,14,2,0); ep(0x81,5,1024,1);
    iface(2,0,14,1,0); b(13,13,0x24,1,0x10,0x01,0,0,0,0,0,0,0,0); xu(14,2,0x20,0x01);
    iface(3,0,14,2,0); unc(1,"Y16 "); frame(5,1,340,340); unc(2,"Y800"); frame(5,1,640,360); iface(3,1,14,2,0); ep(0x82,5,1024,1);
    cam.speed=3; uvc_choose(D,dn,-1); cam.ok=1; cam.desc=D; cam.descn=dn;
    ck("two functions, colour stream chosen first",cam.nvs==2&&cam.vs_if==1&&uvc_has_color());
    ck("infrared stream is found: the stream with only grey formats",uvc_ir_stream()==1&&cam.vs_if==1);
    ck("detection is cached and leaves the colour stream selected",uvc_ir_stream()==1&&uvc_stream_cur()==0);
    uvc_select_stream(1); ck("IR stream: Y16 340x340 and Y8 640x360 understood",cam.nfr==2&&cam.fr[0].kind==7&&cam.fr[1].kind==4&&cam.fr[0].w==340);
    ck("grey-only stream counts as no colour, so IR sizes are not demoted",!uvc_has_color());
    { uvc_xuc_t c[32]; int vc=-1; int n=uvc_xu_list(D,dn,3,c,32,&vc);
      ck("extension controls of the IR function: unit 14, selectors 6 and 9",vc==2&&n==2&&c[0].unit==14&&c[0].sel==6&&c[1].sel==9);
      n=uvc_xu_list(D,dn,1,c,32,&vc); ck("colour function has its own extension unit 4 (selectors 1, 2)",vc==0&&n==2&&c[0].unit==4&&c[1].sel==2);
      ck("unknown stream interface gives no controls",uvc_xu_list(D,dn,7,c,32,&vc)==0); }
    { u8 cur[2]={0,250}, ini[2]={0,250}, mx[2]={3,255}; int by=0,tr=0,seen=0,last=-1;
      while(uvc_xu_next(cur,ini,mx,2,&by,&tr)&&seen<100){ seen++; last=cur[0]; if(by==1&&cur[0]!=0)last=99; }
      ck("value search: byte 0 goes 1..max then back, byte 1 to 255, bounded",seen==3+5&&last!=99&&cur[0]==0&&cur[1]==250); }
    { u32 big=255; ck("value step keeps at most 16 tries per byte",uvc_xu_step(0,big)==16&&uvc_xu_step(5,3)==1); }
    uvc_rgb=fr; uvc_w=64; uvc_h=48; uvc_state=2; uvc_seq=0; fno=0;
    for(int k=0;k<9;k++)simcur[k]=0;
    ck("search starts on the IR function",uvc_ir_search_begin()&&uvc_ir_emitter()==UIR_BASE);
    shoot(15); ck("12 dark frames give the reference, first trial applied",uvc_ir_emitter()==UIR_TRY);
    shoot(200); ck("light found: control 14/6 value 01 00 ..",uvc_ir_emitter()==UIR_ON&&simon);
    { int u,s,l; u8 v[16]; ck("found control is reported for saving",uvc_ir_emitter_ctl(&u,&s,&l,v)&&u==14&&s==6&&l==9&&v[0]==1&&v[1]==0);
      char line[80]; fir_cfg_line(line,0x04f2,0xb6d9,u,s,l,v); ck("saved line format",!strcmp(line,"04f2 b6d9 0e 06 09 01 00 00 00 00 00 00 00 00\n"));
      int u2,s2,l2; u8 v2[16]; char two[200]; sprintf(two,"1234 5678 01 01 01 ff\n%s",line);
      ck("saved line is found again for the same camera",fir_cfg_parse(two,0x04f2,0xb6d9,&u2,&s2,&l2,v2)&&u2==14&&s2==6&&l2==9&&v2[0]==1);
      ck("other cameras do not use it",!fir_cfg_parse(two,0x04f2,0x1111,&u2,&s2,&l2,v2));
      ck("broken lines are rejected",!fir_cfg_parse("04f2 b6d9 0e 06 20 01\n",0x04f2,0xb6d9,&u2,&s2,&l2,v2)&&!fir_cfg_parse("04f2 b6d9 0e 06 03 01 02\n",0x04f2,0xb6d9,&u2,&s2,&l2,v2)); }
    uvc_ir_reset(); for(int k=0;k<9;k++)simcur[k]=0; simon=0;
    { u8 v[9]={1,0,0,0,0,0,0,0,0}; ck("saved setting is applied directly",uvc_ir_emitter_apply(14,6,9,v)&&simon&&uvc_ir_emitter()==UIR_SAVED); }
    uvc_ir_reset(); for(int k=0;k<9;k++)simcur[k]=0; simon=0; simu=0x55; fno=0; sets=0;
    uvc_ir_search_begin(); shoot(2000); ck("camera without a working control: search ends, not found",uvc_ir_emitter()==UIR_NONE&&!simon);
    ck("search is bounded (at most 160 trials)",sets<=2*160+4);
    simu=0x0E; simro=1; uvc_ir_reset(); for(int k=0;k<9;k++)simcur[k]=0; simon=0; fno=0; sets=0;
    uvc_ir_search_begin(); shoot(400); ck("read-only control is never written",uvc_ir_emitter()==UIR_NONE&&!simon); simro=0;
    uvc_ir_reset(); uvc_ir_search_begin(); shoot(5); uvc_state=1; uvc_ir_poll(); ck("stream stopping ends the search",uvc_ir_emitter()==UIR_NONE);
    uvc_fb=malloc(1<<16); uvc_fcap=1<<16; uvc_kind=7; uvc_w=2; uvc_h=1; uvc_fill=4; uvc_fb[0]=0x34; uvc_fb[1]=0xC8; uvc_fb[2]=0xFF; uvc_fb[3]=0x07; uvc_frames=0; uvc_frame_done();
    ck("Y16 frame: high byte becomes the grey value",uvc_frames==1&&fr[0]==0xC8C8C8&&fr[1]==0x070707);
    ck("Y16 is named",!strcmp(uvc_kind_name(7),"Grey 16-bit (infrared)"));
    { static u32 im[80*60]; for(int p=0;p<80*60;p++){ int x=p%80,y=p/80; int inf=x>=20&&x<60&&y>=10&&y<50; im[p]=(u32)((inf?100+((x*7+y*3)%37):20)*0x010101); }
      int real=fir_live(im,80,60,20,10,40,40); for(int p=0;p<80*60;p++)im[p]=0x050505; int screen=fir_live(im,80,60,20,10,40,40);
      for(int p=0;p<80*60;p++)im[p]=0x808080; int flat=fir_live(im,80,60,20,10,40,40);
      ck("IR liveness: lit textured face scores high",real>=70);
      ck("IR liveness: a phone screen is dark in IR, score 0",screen==0);
      ck("IR liveness: flat print without texture, score 0",flat==0);
      ck("IR liveness: box outside the picture is clipped safely",fir_live(im,80,60,70,50,40,40)==0&&fir_live(0,80,60,0,0,10,10)==0); }
    printf(fails?"uvc ir: %d FAILED\n":"uvc ir: all ok\n",fails); return fails!=0; }
