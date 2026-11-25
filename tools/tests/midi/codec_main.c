#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <stdlib.h>
typedef uint8_t u8; typedef uint16_t u16; typedef uint32_t u32; typedef uint64_t u64;
#include "drivers/usb/xhci/usbmidi_codec.h"
#include "drivers/sound/midisynth_core.h"
static int fails;
#define CK(c,m) do{ if(!(c)){ printf("FAIL %s (line %d)\n",m,__LINE__); fails++; } }while(0)
static int enc_all(const u8* in,int n,u8* pk){ struct umc_enc e; umc_init(&e,1); int np=0; for(int i=0;i<n;i++) if(umc_encode(&e,in[i],pk+np*4))np++; return np; }
static int dec_all(const u8* pk,int np,u8* out){ int n=0; for(int i=0;i<np;i++)n+=umc_decode(pk+i*4,out+n); return n; }
static void rt(const u8* in,int n,const u8* expect,int en,int expk,const char* nm){
    u8 pk[1024], out[1024]; int np=enc_all(in,n,pk); int m=dec_all(pk,np,out);
    if(expk>=0)CK(np==expk,nm);
    CK(m==en&&!memcmp(out,expect,(size_t)en),nm);
    for(int i=0;i<np;i++)CK((pk[i*4]>>4)==1,"cable"); }
int main(void){
    { u8 a[]={0x90,0x3C,0x64}; u8 pk[8]; int np=enc_all(a,3,pk); CK(np==1&&pk[0]==0x19&&pk[1]==0x90&&pk[2]==0x3C&&pk[3]==0x64,"note on packet"); rt(a,3,a,3,1,"note on"); }
    { u8 a[]={0x90,0x3C,0x64,0x3E,0x64,0x3C,0x00}; u8 e[]={0x90,0x3C,0x64,0x90,0x3E,0x64,0x90,0x3C,0x00}; rt(a,7,e,9,3,"running status"); }
    { u8 a[]={0xC5,0x10,0xD0,0x7F}; rt(a,4,a,4,2,"2-byte channel"); u8 pk[8]; enc_all(a,2,pk); CK(pk[0]==0x1C,"cin C"); }
    { u8 a[]={0xF0,0x7E,0x7F,0x06,0x01,0xF7}; rt(a,6,a,6,2,"sysex 6"); u8 pk[8]; enc_all(a,6,pk); CK(pk[0]==0x14&&pk[4]==0x17,"sysex cins"); }
    { u8 a[]={0xF0,0xF7}; u8 pk[4]; CK(enc_all(a,2,pk)==1&&pk[0]==0x16,"sysex 2"); rt(a,2,a,2,1,"sysex 2 rt"); }
    { u8 a[]={0xF0,0x01,0x02,0x03,0xF7}; u8 pk[8]; CK(enc_all(a,5,pk)==2&&pk[0]==0x14&&pk[4]==0x16,"sysex 5"); rt(a,5,a,5,2,"sysex 5 rt"); }
    { u8 a[]={0x90,0x3C,0xF8,0x64}; u8 e[]={0xF8,0x90,0x3C,0x64}; rt(a,4,e,4,2,"realtime inside message"); }
    { u8 a[]={0xF2,0x10,0x20,0xF3,0x05,0xF6,0xF1,0x33}; rt(a,8,a,8,4,"system common"); }
    { u8 a[]={0x3C,0x64,0xF4,0xF5}; u8 pk[8]; CK(enc_all(a,4,pk)==0,"stray data dropped"); }
    { u8 a[]={0xE0,0x00,0x40,0xB0,0x40,0x7F}; rt(a,6,a,6,2,"bend+cc"); }
    { u8 p0[4]={0x00,0,0,0}, p1[4]={0x01,0x90,1,2}, pb[4]={0x09,0x30,1,2}, ps[4]={0x0F,0x40,0,0}, o[4]; CK(umc_decode(p0,o)==0&&umc_decode(p1,o)==0&&umc_decode(pb,o)==0,"reserved/invalid cin"); CK(umc_decode(ps,o)==1&&o[0]==0x40,"cin F single byte"); }
    { u8 p[4]={0x08,0x95,1,2}, o[4]; CK(umc_decode(p,o)==3&&o[0]==0x95,"lenient cin"); }
    srand(1); for(int it=0;it<20000;it++){ u8 in[64], pk[512], o[512]; int n=rand()%64; for(int i=0;i<n;i++)in[i]=(u8)rand(); int np=enc_all(in,n,pk); CK(np<=n,"fuzz count"); int m=dec_all(pk,np,o); CK(m<=n*3,"fuzz len"); for(int i=0;i<np;i++){ int c=pk[i*4]&15; CK(c>=2,"fuzz cin"); } }
    { struct ms_state s; ms_init(&s,48000); short buf[2*4800]; ms_render(&s,buf,4800); int z=1; for(int i=0;i<9600;i++) if(buf[i])z=0; CK(z,"silence");
      ms_event(&s,0x90,69,127); CK(ms_active(&s),"voice on"); ms_render(&s,buf,4800); int pk=0, zc=0; for(int i=0;i<4800;i++){ int v=buf[i*2]; if(v<0)v=-v; if(v>pk)pk=v; if(i&&((buf[i*2]<0)!=(buf[i*2-2]<0)))zc++; } CK(pk>1000,"audible"); CK(zc>=80&&zc<=96,"440 Hz pitch");
      CK(ms_inc(81,48000,0)/2-ms_inc(69,48000,0)<2,"octave");
      ms_event(&s,0x80,69,0); for(int k=0;k<10;k++)ms_render(&s,buf,4800); CK(!ms_active(&s),"release ends");
      for(int n=0;n<40;n++){ ms_event(&s,0x90,(u8)(30+n),100); } int a=0; for(int i=0;i<MS_NV;i++)a+=s.v[i].on; CK(a==MS_NV,"steal"); ms_render(&s,buf,4800);
      ms_event(&s,0xB0,64,127); ms_event(&s,0xB0,123,0); ms_render(&s,buf,48000/10); for(int k=0;k<10;k++)ms_render(&s,buf,4800); CK(!ms_active(&s),"all notes off");
      ms_event(&s,0xB0,64,127); ms_event(&s,0x90,60,100); ms_event(&s,0x80,60,0); for(int k=0;k<10;k++)ms_render(&s,buf,4800); CK(ms_active(&s),"sustain holds"); ms_event(&s,0xB0,64,0); for(int k=0;k<10;k++)ms_render(&s,buf,4800); CK(!ms_active(&s),"pedal up releases");
      ms_event(&s,0xE0,0x7F,0x7F); ms_event(&s,0x90,69,100); u32 hi=s.v[0].inc; int f=0; for(int i=0;i<MS_NV;i++) if(s.v[i].on)hi=s.v[i].inc,f=1; CK(f&&hi>ms_inc(70,48000,0)&&hi<ms_inc(72,48000,0),"bend up 2"); }
    printf(fails?"midi codec: %d FAILED\n":"midi codec: all passed\n",fails); return fails?1:0; }
