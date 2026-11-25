#include <stdio.h>
#include <string.h>
#include "drivers/usb/class/uac_out.c"
static char lastlog[200]; void klog(const char* s){ snprintf(lastlog,sizeof lastlog,"%s",s); }
static int fg_fails=0, fg_oks=0, fg_block=0;
void fg_boot(struct featguard* g){ (void)g; }
int fg_begin(struct featguard* g){ if(fg_block||g->dead)return 0; g->trying=1; return 1; }
void fg_ok(struct featguard* g){ g->trying=0; g->fails=0; fg_oks++; }
void fg_fail(struct featguard* g,const char* why){ (void)why; g->trying=0; g->fails++; fg_fails++; if(g->fails>=g->maxfail)g->dead=1; }
static int fails=0, checks=0; static void ck(const char* w,int c){ checks++; printf("  %-66s %s\n",w,c?"ok":"FAIL"); if(!c)fails++; }
static u8 D[2048]; static int dn;
static void b(int n,...){ __builtin_va_list a; __builtin_va_start(a,n); for(int i=0;i<n;i++)D[dn++]=(u8)__builtin_va_arg(a,int); __builtin_va_end(a); }
static void iface(int num,int alt,int cls,int sub,int proto){ b(9,9,4,num,alt,1,cls,sub,proto,0); }
static void ep7(int addr,int attr,int mps,int iv){ b(7,7,5,addr,attr,mps&255,mps>>8,iv); }
static void ep9(int addr,int attr,int mps,int iv,int sa){ b(9,9,5,addr,attr,mps&255,mps>>8,iv,0,sa); }
static void uac1_headset(int mixer){
    dn=0; iface(0,0,1,1,0); b(10,10,0x24,1,0,1,0,0,2,1,2);
    b(12,12,0x24,2,1,0x01,0x01,0,2,3,0,0,0);
    b(10,10,0x24,6,2,1,1,0x03,0x00,0x00,0);
    b(9,9,0x24,3,3,0x02,0x03,0,mixer?7:2,0);
    b(12,12,0x24,2,4,0x01,0x02,0,1,0,0,0,0);
    b(9,9,0x24,6,5,4,1,0x03,0x00,0);
    b(9,9,0x24,3,6,0x01,0x01,0,5,0);
    if(mixer)b(13,13,0x24,4,7,2,5,2,2,3,0,0,0,0);
    iface(1,0,1,2,0); iface(1,1,1,2,0); b(7,7,0x24,1,1,1,1,0); b(14,14,0x24,2,1,2,2,16,2,0x44,0xAC,0,0x80,0xBB,0); ep9(0x01,0x09,192,1,0); b(7,7,0x25,1,1,0,0);
    iface(2,0,1,2,0); iface(2,1,1,2,0); b(7,7,0x24,1,6,1,1,0); b(11,11,0x24,2,1,1,2,16,1,0x80,0xBB,0); ep9(0x82,0x05,96,1,0); }
static void uac2_dac(void){
    dn=0; b(8,8,11,0,2,1,0,0x20,0); iface(0,0,1,1,0x20); b(9,9,0x24,1,0,2,10,64,0,0);
    b(8,8,0x24,0x0A,41,3,7,0,0);
    b(8,8,0x24,0x0B,40,1,41,3,0);
    b(17,17,0x24,2,2,0x01,0x01,0,40,2,3,0,0,0,0,0,0,0);
    b(18,18,0x24,6,10,2,0x0F,0,0,0,0,0,0,0,0,0,0,0,0);
    b(12,12,0x24,3,20,0x01,0x03,0,10,40,0,0,0);
    iface(1,0,1,2,0x20); iface(1,1,1,2,0x20); b(16,16,0x24,1,2,0,1,1,0,0,0,2,3,0,0,0,0); b(6,6,0x24,2,1,4,24); ep7(0x01,0x05,584,1); b(8,8,0x25,1,0,0,0,0,0);
    ep7(0x81,0x11,4,4); }
static u8 rq_last[8]; static u8 rq_set[16][4]; static int rq_n=0, rq_fail=0, rq_cnt=0; static u16 rq_wv[16], rq_wi[16];
static int fake_req(u8 rt,u8 req,u16 val,u16 idx,u16 len,int in,u8* d){
    rq_cnt++; rq_last[0]=rt; rq_last[1]=req; rq_last[2]=(u8)val; rq_last[3]=(u8)(val>>8); rq_last[4]=(u8)idx; rq_last[5]=(u8)(idx>>8); rq_last[6]=(u8)len;
    if(rq_fail)return 0;
    if(in){ if(req==0x82){ d[0]=0x00; d[1]=0xE0; } else if(req==0x83){ d[0]=0; d[1]=0; } else if(req==0x84){ d[0]=0x80; d[1]=0; }
        else if(req==0x02&&len==8){ d[0]=1; d[1]=0; d[2]=0x00; d[3]=0xC0; d[4]=0x00; d[5]=0x00; d[6]=0x00; d[7]=0x01; } return 1; }
    if(rq_n<16){ for(int k=0;k<len&&k<4;k++)rq_set[rq_n][k]=d[k]; rq_wv[rq_n]=val; rq_wi[rq_n]=idx; rq_n++; } return 1; }
static u32 ref_pick(const u32* r,int n,int cont){
    if(cont){ if(48000>=r[0]&&48000<=r[1])return 48000; if(44100>=r[0]&&44100<=r[1])return 44100; return r[1]; }
    u32 best=0; for(int k=0;k<n;k++){ if(r[k]==48000)return 48000; } for(int k=0;k<n;k++) if(r[k]==44100)return 44100;
    for(int k=0;k<n;k++) if(r[k]<=48000&&r[k]>best)best=r[k]; if(!best&&n)best=r[0]; return best; }
int main(void){
    uac_topo_t t; printf("USB audio output (UAC1/UAC2) host test\n");
    uac1_headset(0); ck("UAC1 headset: AudioControl found",uac_topo_parse(D,dn,&t)&&t.ac_if==0&&!t.v2);
    ck("UAC1: output path found",uac_out_path(&t)&&t.ot==3&&t.it==1&&t.fu==2&&t.ot_type==0x0302);
    ck("UAC1: output named Headphones",!strcmp(uac_kind_name(t.ot_type),"Headphones"));
    ck("UAC1: headset microphone terminal found",t.mic_it==4);
    { int k=uac_as_find(&t,1,1); ck("UAC1: playback interface linked to terminal 1, adaptive",k>=0&&t.as[k].link==1&&t.as[k].ep==0x01&&t.as[k].sync==2&&!t.as[k].fb); }
    ck("UAC1: feature unit master mute+volume",t.ent[1].kind==UAC_E_FU&&t.ent[1].fc[0]==3);
    uac1_headset(1); uac_topo_parse(D,dn,&t);
    ck("UAC1 mixer (sidetone first input): path goes through the USB input",uac_out_path(&t)&&t.it==1&&t.fu==2);
    uac2_dac(); ck("UAC2 DAC: AudioControl v2",uac_topo_parse(D,dn,&t)&&t.v2&&t.ac_if==0);
    ck("UAC2: speaker path with FU 10",uac_out_path(&t)&&t.ot==20&&t.it==2&&t.fu==10&&!strcmp(uac_kind_name(t.ot_type),"Speakers"));
    ck("UAC2: clock resolved through the selector to source 41",t.clk==41);
    ck("UAC2: FU bmaControls 0x0F = mute+volume programmable",t.ent[3].fc[0]==3);
    { int k=uac_as_find(&t,1,1); ck("UAC2: asynchronous OUT with feedback endpoint 0x81",k>=0&&t.as[k].sync==1&&t.as[k].fb==0x81&&t.as[k].fbmps==4&&t.as[k].fbint==4); }
    uac1_headset(0); { int sv=dn; D[9]=0; ck("hostile: zero-length descriptor stops the walk",uac_topo_parse(D,sv,&t)==1&&t.nent==0); }
    dn=0; iface(0,0,1,1,0); for(int i=0;i<40;i++)b(10,10,0x24,6,i+1,i,1,3,0,0,0); b(9,9,0x24,3,99,2,3,0,1,0);
    ck("hostile: 40 units capped at 32, self-loop graph bounded",uac_topo_parse(D,dn,&t)&&t.nent==32&&!uac_out_path(&t));
    dn=0; iface(0,0,1,1,0); b(9,9,0x24,6,5,5,1,3,0,0); b(9,9,0x24,3,6,0x02,0x03,0,5,0); uac_topo_parse(D,dn,&t);
    ck("hostile: unit feeding itself does not loop",!uac_out_path(&t));
    for(int cut=0;cut<dn;cut++){ uac2_dac(); uac_topo_parse(D,cut,&t); uac_out_path(&t); } ck("hostile: every truncation of the DAC descriptor parses safely",1);
    { u32 r1[3]={44100,48000,96000}; ck("rate: preferred 96 kHz chosen when offered",uac_pick(r1,3,0,96000)==96000);
      ck("rate: 48 kHz default",uac_pick(r1,3,0,48000)==48000); ck("rate: 44.1 kHz preferred",uac_pick(r1,3,0,44100)==44100);
      u32 r2[2]={8000,96000}; ck("rate: continuous UAC1 range honours 96 kHz",uac_pick(r2,2,1,96000)==96000);
      u32 r3[2]={32000,22050}; ck("rate: no 44.1/48 -> best below 48 kHz",uac_pick(r3,2,0,96000)==32000);
      static const u32 T[][4]={{48000,0,0,1},{44100,0,0,1},{96000,0,0,1},{32000,22050,0,2},{44100,48000,0,2},{96000,192000,0,2},{16000,96000,0,2},{8000,44100,0,2}};
      int same=1; for(unsigned i=0;i<sizeof(T)/sizeof(T[0]);i++){ int n=(int)T[i][3]; if(uac_pick(T[i],n,0,48000)!=ref_pick(T[i],n,0))same=0; if(n==2&&uac_pick(T[i],2,1,48000)!=ref_pick(T[i],2,1))same=0; }
      ck("rate: with the default preference the choice equals the old driver",same); }
    { u8 g[2+24]={2,0, 0x44,0xAC,0,0, 0x44,0xAC,0,0, 0,0,0,0, 0x80,0xBB,0,0, 0x80,0xBB,0,0, 0,0,0,0};
      ck("UAC2 range: two discrete rates, 96k wanted -> 48k",uac2_range_pick(g,26,96000,1)==48000);
      ck("UAC2 range: 44.1k wanted and offered",uac2_range_pick(g,26,44100,1)==44100);
      u8 h[14]={1,0, 0x00,0x7D,0,0, 0x00,0xEE,2,0, 0xA0,0x0F,0,0};
      ck("UAC2 range: 32k..192k step 4000 gives 96k",uac2_range_pick(h,14,96000,1)==96000);
      ck("UAC2 range: 44.1k is not on the 4000 grid -> falls to 48k",uac2_range_pick(h,14,44100,1)==48000);
      u8 bad[14]={200,0}; ck("UAC2 range: lying subrange count bounded by length",uac2_range_pick(bad,14,48000,7)==7); }
    { uac_rate_t r; uac_rate_init(&r,48000,0); u8 f[3]={0,0,0x0C}; ck("feedback FS 10.14: 48.0 frames/ms accepted",uac_fb_take(&r,f,3)&&r.fb==(48u<<16));
      u32 s=0; for(int i=0;i<1000;i++)s+=uac_rate_frames(&r,1000); ck("feedback FS: 1000 packets carry 48000 frames",s==48000);
      uac_rate_init(&r,44100,0); u32 v=(u32)(44.1*16384+0.5); u8 g[3]={(u8)v,(u8)(v>>8),(u8)(v>>16)}; uac_fb_take(&r,g,3); s=0; for(int i=0;i<1000;i++)s+=uac_rate_frames(&r,1000);
      ck("feedback FS: 44.1 kHz gives 44100 +-1 frames per second",s>=44099&&s<=44101);
      uac_rate_init(&r,48000,0); v=(u32)(48.05*16384); u8 q[3]={(u8)v,(u8)(v>>8),(u8)(v>>16)}; uac_fb_take(&r,q,3); s=0; for(int i=0;i<1000;i++)s+=uac_rate_frames(&r,1000);
      ck("feedback: DAC clock 0.1% fast -> 48050 frames/s sent",s>=48049&&s<=48051);
      uac_rate_init(&r,96000,1); u8 h[4]={0,0,12,0}; ck("feedback HS 16.16: 12 frames/microframe",uac_fb_take(&r,h,4)&&r.fb==(12u<<16));
      s=0; for(int i=0;i<1000;i++)s+=uac_rate_frames(&r,1000); ck("feedback HS: 1 ms packets carry 96 frames",s==96000);
      uac_rate_init(&r,48000,1); u8 k[4]={0,0x80,0x01,0}; ck("feedback HS quirk: 10.14 in 4 bytes detected by shift",uac_fb_take(&r,k,4)&&r.fb==(6u<<16)&&r.shift==2);
      uac_rate_init(&r,48000,0); u8 z[3]={0,0,0}; ck("feedback: zero value rejected, nominal kept",!uac_fb_take(&r,z,3)&&r.bad==1&&uac_rate_frames(&r,1000)==48);
      u8 w[3]={0,0,0x18}; uac_fb_take(&r,f,3); ck("feedback: value 2x off after lock rejected",!uac_fb_take(&r,w,3)&&r.fb==(48u<<16));
      uac_rate_init(&r,48000,0); ck("feedback: short packet ignored",!uac_fb_take(&r,f,2)); }
    { uac1_headset(0); uac_topo_parse(D,dn,&t); uac_out_path(&t); uac_vol_t v; rq_n=0;
      ck("UAC1 volume: range read (GET_MIN/MAX/RES)",uac_vol_probe(&t,fake_req,&v)&&v.ok&&v.min==(short)0xE000&&v.max==0&&v.res==0x80&&v.nch==1&&v.ch[0]==0&&v.mute);
      ck("volume map: 100% = max, 0% = min, 50% = -16 dB on the 0.5 dB grid",uac_vol_value(&v,100)==0&&uac_vol_value(&v,0)==(short)0xE000&&uac_vol_value(&v,50)==-16*256);
      rq_n=0; ck("UAC1 SET_CUR volume + unmute",uac_vol_set(&v,fake_req,100)&&rq_n==2&&rq_wv[0]==0x0200&&rq_wi[0]==0x0200&&rq_wv[1]==0x0100&&rq_set[1][0]==0);
      rq_n=0; uac_vol_set(&v,fake_req,0); ck("0% mutes",rq_n==2&&rq_set[1][0]==1);
      uac2_dac(); uac_topo_parse(D,dn,&t); uac_out_path(&t); rq_n=0;
      ck("UAC2 volume: RANGE request (bRequest 2, 8 bytes)",uac_vol_probe(&t,fake_req,&v)&&v.ok&&v.v2&&v.min==(short)0xC000&&v.max==0&&v.res==256);
      ck("volume map: wide range limited to the top 50 dB",uac_vol_value(&v,1)>=-50*256&&uac_vol_value(&v,1)<-48*256); }
    { uac2_dac(); rq_n=0; rq_fail=0; fg_fails=0; uac_out_attach(D,dn,1,fake_req);
      ck("attach: present, log names the output",uac_out_present()&&strstr(lastlog,"Speakers")&&strstr(lastlog,"USB Audio 2")&&strstr(lastlog,"clock 41"));
      ck("attach: output clock is 41 (not the first clock found)",uac_out_clock(7)==41);
      u8 e=0,iv=0; u16 m=0; ck("attach: feedback endpoint offered for alt 1",uac_out_fb(1,1,&e,&m,&iv)&&e==0x81&&m==4&&iv==4);
      uac_out_fb_start(96000,1); u8 h[4]={0,0,12,0}; for(int i=0;i<64;i++)uac_out_fb_data(h,4); ck("feedback live + guard confirmed after 64 values",uac_out_fb_live()&&fg_oks>0&&uac_out_frames(1000)==96);
      uac_out_stream(96000,24,2); ck("Settings text: 24-bit 96 kHz asynchronous, hardware volume",strstr(uac_out_desc(),"24-bit 96 kHz")&&strstr(uac_out_desc(),"asynchronous")&&strstr(uac_out_desc(),"hardware volume"));
      uac_out_fb_stop(); rq_n=0; ck("hardware volume used (software gain 100%)",uac_out_volume(40)==1&&rq_n>=1);
      rq_fail=1; ck("volume failure falls back to software volume",uac_out_volume(40)==0&&fg_fails==1); rq_fail=0;
      uac_out_fb_start(48000,0); fg_fails=0; uac_out_fb_stop(); ck("no feedback received -> guard failure counted",fg_fails==1);
      uac_out_set_rate_pref(96000); u32 r1[3]={44100,48000,96000}; ck("rate preference 96 kHz used for the stream",uac_out_rate(r1,3,0,48000)==96000);
      uac_out_set_rate_pref(12345); ck("bad preference ignored",uac_out_rate_pref()==96000); uac_out_set_rate_pref(48000);
      uac_out_detach(); u8 e2; u16 m2; u8 i2; ck("detach: nothing offered afterwards",!uac_out_present()&&!uac_out_fb(1,1,&e2,&m2,&i2)&&!uac_out_volume(50)&&uac_out_clock(7)==7);
      uac1_headset(0); uac_out_attach(D,dn,0,fake_req); u8 e3; u16 m3; u8 i3; ck("adaptive UAC1 headset: no feedback endpoint used",uac_out_present()&&!uac_out_fb(1,1,&e3,&m3,&i3));
      fg_block=1; uac2_dac(); uac_out_attach(D,dn,1,fake_req); ck("feature guard off: no feedback endpoint, no hardware volume",!uac_out_fb(1,1,&e3,&m3,&i3)&&!uac_out_volume(50)); fg_block=0; uac_out_detach(); }
    printf(fails?"UAC HOST TEST: %d FAILED of %d\n":"UAC HOST TEST: ALL PASS (%d checks)\n",fails?fails:checks,checks); return fails?1:0; }
