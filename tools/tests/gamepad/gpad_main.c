#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <stdlib.h>
#include <stddef.h>
#include <linux/input.h>
#include <linux/joystick.h>
typedef uint8_t u8; typedef uint16_t u16; typedef uint32_t u32; typedef uint64_t u64;
#include "drivers/input/gamepad/gpad_proto.h"
#include "drivers/input/gamepad/gamepad.h"
#include "sys/linux/evdev_abi.h"
static u64 now_ns=1000;
u64 hr_ns(void){ return now_ns; }
void klog(const char* s){ (void)s; }
static int ptr_dx, ptr_dy, ptr_click;
void gpad_pointer(int dx,int dy,int click){ ptr_dx+=dx; ptr_dy+=dy; if(click)ptr_click=click; }
#include "drivers/input/gamepad/gamepad.c"
static int fails, checks;
#define CK(c,m) do{ checks++; if(!(c)){ printf("FAIL %s (line %d)\n",m,__LINE__); fails++; } }while(0)
#define B(x) (1u<<(x))
static int out_n, out_s, out_w, out_r;
static int outcb(int i,int ref,int s,int w,u8 r,u8 g,u8 b){ (void)i; (void)ref; (void)g; (void)b; out_n++; out_s=s; out_w=w; out_r=r; return 1; }
static const u8 ds4_desc_hid[]={0x05,0x01,0x09,0x05,0xA1,0x01,0x85,0x01,0x09,0x30,0x09,0x31,0x09,0x32,0x09,0x35,0x15,0x00,0x26,0xFF,0x00,0x75,0x08,0x95,0x04,0x81,0x02,
    0x09,0x39,0x15,0x00,0x25,0x07,0x35,0x00,0x46,0x3B,0x01,0x65,0x14,0x75,0x04,0x95,0x01,0x81,0x42,0x65,0x00,0x05,0x09,0x19,0x01,0x29,0x0E,0x15,0x00,0x25,0x01,0x75,0x01,0x95,0x0E,0x81,0x02,
    0x75,0x06,0x95,0x01,0x81,0x01,0x05,0x01,0x09,0x33,0x09,0x34,0x15,0x00,0x26,0xFF,0x00,0x75,0x08,0x95,0x02,0x81,0x02,0xC0};
static const u8 kbd_desc[]={0x05,0x01,0x09,0x06,0xA1,0x01,0x05,0x07,0x19,0xE0,0x29,0xE7,0x15,0x00,0x25,0x01,0x75,0x01,0x95,0x08,0x81,0x02,0xC0};
int main(void){
    CK(sizeof(struct input_event)==AEV_EVSZ,"input_event size"); CK(sizeof(struct input_absinfo)==AEV_ABSSZ,"absinfo size"); CK(sizeof(struct ff_effect)==AEV_FFSZ,"ff_effect size"); CK(sizeof(struct js_event)==AEV_JSSZ,"js_event size");
    CK(offsetof(struct ff_effect,u.rumble.strong_magnitude)==16&&offsetof(struct ff_effect,u.rumble.weak_magnitude)==18&&offsetof(struct ff_effect,replay.length)==10,"ff_effect offsets");
    CK(EVIOCGVERSION==AEV_GVERSION&&EVIOCGID==AEV_GID&&EVIOCSFF==AEV_SFF&&EVIOCRMFF==AEV_RMFF&&EVIOCGEFFECTS==AEV_GEFFECTS&&EVIOCGRAB==AEV_GRAB&&EVIOCREVOKE==AEV_REVOKE&&EVIOCSCLOCKID==AEV_SCLOCKID,"evdev ioctl numbers");
    CK(EVIOCGNAME(256)==AEV_IOC(2,'E',0x06,256)&&EVIOCGBIT(EV_KEY,96)==AEV_IOC(2,'E',0x21,96)&&EVIOCGABS(ABS_RZ)==AEV_IOC(2,'E',0x45,24)&&EVIOCGKEY(96)==AEV_IOC(2,'E',0x18,96)&&EVIOCGPROP(8)==AEV_IOC(2,'E',0x09,8),"evdev variable ioctls");
    CK(JSIOCGVERSION==AEV_JSGVERSION&&JSIOCGAXES==AEV_JSGAXES&&JSIOCGBUTTONS==AEV_JSGBUTTONS&&JSIOCGAXMAP==AEV_JSGAXMAP&&JSIOCGBTNMAP==AEV_JSGBTNMAP&&JSIOCGNAME(128)==AEV_IOC(2,'j',0x13,128),"joydev ioctl numbers");
    CK(AEV_FF_RUMBLE==FF_RUMBLE&&AEV_FF_GAIN==FF_GAIN&&AEV_EV_FF==EV_FF&&JS_VERSION==AEV_JSVERSION,"ff/js constants");
    CK(gp_keylist[0]==BTN_SOUTH&&gp_keylist[1]==BTN_EAST&&gp_keylist[3]==BTN_NORTH&&gp_keylist[4]==BTN_WEST&&gp_keylist[6]==BTN_TL&&gp_keylist[9]==BTN_START&&gp_keylist[10]==BTN_MODE&&gp_keylist[12]==BTN_THUMBR,"key codes");
    CK(gp_abslist[2]==ABS_Z&&gp_abslist[5]==ABS_RZ&&gp_abslist[6]==ABS_HAT0X&&gp_abslist[7]==ABS_HAT0Y,"abs codes");
    for(int i=1;i<GP_NKEY;i++)CK(gp_keylist[i]>gp_keylist[i-1],"keys ascending (joydev order)");
    CK(gp_ident(0x045E,0x028E,0xFF,0x5D,0x01)==GP_X360,"ident 360"); CK(gp_ident(0x045E,0x0B12,0xFF,0x47,0xD0)==GP_XONE,"ident one");
    CK(gp_ident(0x054C,0x09CC,3,0,0)==GP_DS4,"ident ds4"); CK(gp_ident(0x054C,0x0CE6,3,0,0)==GP_DS5,"ident ds5"); CK(gp_ident(0x057E,0x2009,3,0,0)==GP_SWPRO,"ident switch"); CK(gp_ident(0x1234,0x5678,3,0,0)==GP_NONE,"ident unknown");
    struct gpst s; u8 r[80];
    memset(&s,0,sizeof s); memset(r,0,sizeof r); r[0]=0; r[1]=0x14; r[2]=0x01|0x10; r[3]=0x10|0x04|0x02; r[4]=200; r[5]=7; r[6]=0xFF; r[7]=0x7F; r[8]=0x00; r[9]=0x80; r[10]=0x34; r[11]=0x12; r[12]=0xFF; r[13]=0x7F;
    CK(gp_x360_parse(r,20,&s),"360 parse"); CK(s.btn==(B(GPB_DU)|B(GPB_START)|B(GPB_S)|B(GPB_GUIDE)|B(GPB_RB)),"360 buttons"); CK(s.lt==200&&s.rt==7,"360 triggers"); CK(s.lx==32767&&s.ly==32767&&s.rx==0x1234&&s.ry==-32768,"360 sticks (Y down)");
    CK(!gp_x360_parse(r,10,&s),"360 short rejected");
    CK(gp_x360_rumble(r,255,128)==8&&r[1]==8&&r[3]==255&&r[4]==128,"360 rumble"); CK(gp_x360_led(r,2)==3&&r[2]==7,"360 led player 2");
    CK(gp_xone_init(r,3)==5&&r[0]==5&&r[1]==0x20&&r[2]==3&&r[3]==1,"gip power on");
    memset(&s,0,sizeof s); memset(r,0,sizeof r); r[0]=0x20; r[3]=14; r[4]=0x10|0x80|0x04; r[5]=0x08|0x20; r[6]=0xFF; r[7]=0x03; r[10]=0x00; r[11]=0x80; r[12]=0x00; r[13]=0x00;
    CK(gp_xone_parse(r,18,&s),"gip parse"); CK(s.btn==(B(GPB_S)|B(GPB_N)|B(GPB_START)|B(GPB_DR)|B(GPB_RB)),"gip buttons"); CK(s.lt==255&&s.rt==0&&s.lx==-32768&&s.ly==-1,"gip axes");
    u8 g7[6]={0x07,0x30,5,2,1,0x5B}; CK(gp_xone_parse(g7,6,&s)&&(s.btn&B(GPB_GUIDE))&&(s.btn&B(GPB_S)),"gip guide keeps buttons");
    u8 ack[16]; CK(gp_xone_ack(g7,6,ack)==13&&ack[0]==1&&ack[2]==5&&ack[5]==7,"gip ack"); g7[1]=0x20; CK(gp_xone_ack(g7,6,ack)==0,"gip no ack when not requested");
    CK(gp_xone_rumble(r,9,255,0)==13&&r[0]==9&&r[2]==9&&r[8]==100&&r[9]==0,"gip rumble");
    memset(&s,0,sizeof s); memset(r,0,sizeof r); r[0]=1; r[1]=0; r[2]=255; r[3]=128; r[4]=128; r[5]=0x20|2; r[6]=0x01|0x20; r[7]=0x01|0x02; r[8]=10; r[9]=250; r[30]=0x1B; r[35]=0x05; r[36]=0x34; r[37]=0x12|0x50; r[38]=0x20;
    CK(gp_ds4_parse(r,64,&s),"ds4 usb parse"); CK(s.btn==(B(GPB_S)|B(GPB_DR)|B(GPB_LB)|B(GPB_START)|B(GPB_GUIDE)|B(GPB_TPAD)),"ds4 buttons"); CK(s.lx==-32768&&s.ly==32767&&s.rx==0,"ds4 sticks"); CK(s.lt==10&&s.rt==250,"ds4 triggers");
    CK(s.chg==1&&s.bat==100,"ds4 battery charging full"); CK(s.tp==1&&s.tpx==0x234&&s.tpy==0x205,"ds4 touch");
    memset(r+3,0,64); r[0]=0x11; memcpy(r+3,r+1,0); { u8 bt[78]; memset(bt,0,78); bt[0]=0x11; bt[3]=128; bt[4]=128; bt[5]=128; bt[6]=128; bt[7]=0x14; bt[32]=0x05; CK(gp_ds4_parse(bt,78,&s),"ds4 bt parse"); CK(s.btn==(B(GPB_DD)|B(GPB_W)),"ds4 bt hat+square"); CK(s.bat==55&&!s.chg,"ds4 bt battery"); }
    { u8 o[80]; CK(gp_ds4_out(o,0,200,100,1,2,3)==32&&o[0]==5&&o[4]==100&&o[5]==200&&o[6]==1&&o[8]==3,"ds4 usb out");
      CK(gp_ds4_out(o,1,200,100,1,2,3)==78&&o[0]==0x11&&o[6]==100&&o[7]==200,"ds4 bt out"); u8 h=0xA2; u32 c=gp_crc32(gp_crc32(0,&h,1),o,74); CK(o[74]==(u8)c&&o[77]==(u8)(c>>24),"ds4 bt crc");
      CK(gp_crc32(0,(const u8*)"123456789",9)==0xCBF43926u,"crc32 check value"); }
    memset(&s,0,sizeof s); memset(r,0,sizeof r); r[0]=1; r[1]=128; r[2]=0; r[3]=128; r[4]=128; r[5]=255; r[6]=0; r[8]=0x80|8; r[9]=0x10; r[10]=0x04; r[33]=0x80; r[53]=0x13;
    CK(gp_ds5_parse(r,64,&s),"ds5 usb parse"); CK(s.btn==(B(GPB_N)|B(GPB_BACK)|B(GPB_MISC)),"ds5 buttons (create, mute)"); CK(s.ly==-32768&&s.lt==255,"ds5 axes"); CK(s.chg==1&&s.bat==35,"ds5 battery"); CK(!s.tp,"ds5 no touch");
    { u8 o[80]; CK(gp_ds5_out(o,0,0,255,10,9,8,7,1)==48&&o[0]==2&&o[3]==10&&o[4]==255&&o[45]==9&&o[47]==7&&o[44]==4,"ds5 usb out");
      CK(gp_ds5_out(o,1,3,255,10,9,8,7,2)==78&&o[0]==0x31&&o[1]==0x30&&o[2]==0x10&&o[5]==10&&o[47]==9,"ds5 bt out"); }
    { u8 o[64]; CK(gp_sw_hs(0,o)==2&&o[0]==0x80&&o[1]==2,"switch hs 0"); CK(gp_sw_hs(3,o)==2&&o[1]==4,"switch hs 3"); CK(gp_sw_hs(4,o)==0,"switch hs end");
      u8 a=0x30; CK(gp_sw_sub(o,17,0x03,&a,1)==64&&o[0]==1&&o[1]==1&&o[10]==3&&o[11]==0x30&&o[3]==1&&o[4]==0x40,"switch subcmd"); CK(gp_sw_rumble(o,2,0,0)==10&&o[0]==0x10&&o[3]==1&&o[5]==0x40,"switch rumble neutral"); }
    memset(&s,0,sizeof s); memset(r,0,sizeof r); r[0]=0x30; r[2]=0x90; r[3]=0x08|0x80; r[4]=0x02|0x10; r[5]=0x02|0x40; r[6]=0x00; r[7]=0x00; r[8]=0x80; r[9]=0xFF; r[10]=0xFF; r[11]=0xFF;
    CK(gp_sw_parse(r,49,&s),"switch parse"); CK(s.btn==(B(GPB_E)|B(GPB_START)|B(GPB_GUIDE)|B(GPB_DU)|B(GPB_LB)),"switch buttons (A east)"); CK(s.rt==255&&s.lt==0,"switch ZR"); CK(s.lx==-32768&&s.ly>-200&&s.ly<200&&s.rx==32767&&s.ry==-32768,"switch sticks"); CK(s.bat==100&&s.chg==1,"switch battery");
    struct gphid h; CK(gp_hid_desc(ds4_desc_hid,(int)sizeof ds4_desc_hid,&h),"hid gamepad desc"); CK(h.rid==1&&h.nbtn==14&&h.nax==6&&h.hat,"hid fields");
    memset(&s,0,sizeof s); memset(r,0,sizeof r); r[0]=1; r[1]=0; r[2]=255; r[3]=128; r[4]=128; r[5]=0x02|(0x05<<4); r[6]=0x01; r[8]=255; r[9]=0;
    CK(gp_hid_parse(&h,r,10,&s),"hid parse"); CK(s.lx==-32768&&s.ly==32767,"hid sticks"); CK((s.btn&B(GPB_DR))&&(s.btn&B(GPB_S))&&(s.btn&B(GPB_W))&&(s.btn&B(GPB_LB)),"hid buttons + hat"); CK(s.lt==255&&s.rt==0,"hid Rx/Ry triggers");
    r[0]=2; CK(!gp_hid_parse(&h,r,10,&s),"hid wrong report id"); CK(!gp_hid_desc(kbd_desc,(int)sizeof kbd_desc,&h),"keyboard is not a gamepad");
    srand(7); for(int it=0;it<200000;it++){ u8 d[96]; int n=rand()%96; for(int i=0;i<n;i++){ int t=rand()%6; d[i]=(u8)(t==0?0x05:t==1?0x09:t==2?0x81:t==3?0x95:t==4?0x75:rand()); } struct gphid hh; gp_hid_desc(d,n,&hh); u8 rr[64]; for(int i=0;i<64;i++)rr[i]=(u8)rand(); struct gpst ss; memset(&ss,0,sizeof ss); gp_hid_parse(&hh,rr,rand()%65,&ss);
        int m=rand()%80; gp_ds4_parse(rr,m<64?m:64,&ss); gp_ds5_parse(rr,m<64?m:64,&ss); gp_sw_parse(rr,m<64?m:64,&ss); gp_xone_parse(rr,m<64?m:64,&ss); gp_x360_parse(rr,m<64?m:64,&ss); }
    CK(1,"fuzz ran");
    int i0=gpad_add(GP_DS4,3,0x054C,0x09CC,"Wireless Controller",GPC_RUMBLE|GPC_LED|GPC_TOUCH,outcb,0); CK(i0==0&&out_n==1,"add pad sets lightbar");
    int i1=gpad_add(GP_X360,3,0x045E,0x028E,0,GPC_RUMBLE,outcb,1); CK(i1==1&&gpad_at(1)->player==2&&gpad_count()==2,"second pad player 2");
    u32 pos=gpad_qpos(0); memset(&s,0,sizeof s); s.valid=1; s.btn=B(GPB_S)|B(GPB_DL); s.lx=1000; gpad_report(0,&s);
    struct gpev ev[64]; int dr=0; int n=gpad_read_ev(0,&pos,ev,64,&dr); int sawS=0, sawHat=0, sawSyn=0, sawX=0; for(int i=0;i<n;i++){ if(ev[i].type==1&&ev[i].code==BTN_SOUTH&&ev[i].val==1)sawS=1; if(ev[i].type==3&&ev[i].code==ABS_HAT0X&&ev[i].val==-1)sawHat=1; if(ev[i].type==3&&ev[i].code==ABS_X&&ev[i].val==1000)sawX=1; if(ev[i].type==0)sawSyn=1; }
    CK(sawS&&sawHat&&sawX&&sawSyn&&ev[n-1].type==0,"report -> evdev events + SYN"); CK(n==1+8+1,"first report sends all axes once");
    s.btn=B(GPB_S)|B(GPB_DL); gpad_report(0,&s); CK(gpad_read_ev(0,&pos,ev,64,&dr)==0,"no change, no events");
    s.lx=2000; gpad_report(0,&s); n=gpad_read_ev(0,&pos,ev,64,&dr); CK(n==2&&ev[0].code==ABS_X&&ev[0].val==2000,"axis delta only");
    CK(gpad_keyval(0,BTN_SOUTH)==1&&gpad_absval(0,ABS_HAT0X)==-1&&gpad_absval(0,ABS_X)==2000,"state queries");
    for(int i=0;i<GP_Q;i++){ s.lx=i; gpad_report(0,&s); } dr=0; n=gpad_read_ev(0,&pos,ev,64,&dr); CK(dr==1&&n==64,"overflow reported as dropped");
    out_n=0; CK(gpad_rumble(0,255,128,100)==1&&out_s==255&&out_w==128,"rumble out"); now_ns+=50000000ULL; gpad_tick(); CK(out_n==1,"rumble still on"); now_ns+=60000000ULL; gpad_tick(); CK(out_n==2&&out_s==0&&out_w==0,"rumble stops after duration");
    CK(gpad_led(0,9,8,7)==1&&out_r==9,"lightbar"); CK(gpad_led(1,1,1,1)==-1,"360 has no lightbar");
    g_gp_tpptr=1; s.tp=1; s.tpx=100; s.tpy=100; gpad_report(0,&s); s.tpx=130; s.tpy=70; gpad_report(0,&s); CK(ptr_dx==20&&ptr_dy==-20,"touchpad moves pointer"); s.btn|=B(GPB_TPAD); gpad_report(0,&s); CK(ptr_click==1,"touchpad click"); g_gp_tpptr=0;
    char info[1024]; CK(gpad_info(info,sizeof info)&&strstr(info,"js0: Wireless Controller (DualShock 4, USB)")&&strstr(info,"js1"),"info text");
    gpad_remove(0); CK(!gpad_valid(0)&&gpad_count()==1,"remove"); CK(gpad_read_ev(0,&pos,ev,4,&dr)<0,"read after remove fails");
    int lo,hi,fl; CK(gpad_absinfo(ABS_RZ,&lo,&hi,&fl)&&lo==0&&hi==255,"absinfo trigger"); CK(gpad_absinfo(ABS_HAT0Y,&lo,&hi,&fl)&&lo==-1&&hi==1,"absinfo hat"); CK(!gpad_absinfo(ABS_MISC,&lo,&hi,&fl),"absinfo unknown");
    printf("gamepad host test: %d checks, %d failed\n",checks,fails);
    if(!fails)printf("gamepad: all passed\n");
    return fails?1:0; }
