#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <stdlib.h>
#include <stddef.h>
#include <sys/ioctl.h>
#include <sound/asound.h>
typedef uint8_t u8; typedef uint16_t u16; typedef uint32_t u32; typedef uint64_t u64;
#include "drivers/usb/xhci/usbmidi_codec.h"
#include "sys/linux/rawmidi_abi.h"
static int fails;
#define CK(c,m) do{ if(!(c)){ printf("FAIL %s (line %d)\n",m,__LINE__); fails++; } }while(0)
static int put(u8* b,int o,const u8* d,int n){ memcpy(b+o,d,n); return o+n; }
static int mk(u8* b,int epin,int mpin,int epout,int mpout,int ncin,int ncout,int ac_first){
    int o=9;
    static const u8 ac[]={9,4,0,0,0,1,1,0,0};
    if(ac_first)o=put(b,o,ac,9);
    u8 ms[]={9,4,1,0,2,1,3,0,0}; o=put(b,o,ms,9);
    u8 csms[]={7,0x24,1,0,1,0,0}; o=put(b,o,csms,7);
    if(epout>=0){ u8 e[]={9,5,(u8)epout,2,(u8)mpout,(u8)(mpout>>8),0,0,0}; o=put(b,o,e,9); u8 c[20]={(u8)(4+ncout),0x25,1,(u8)ncout}; for(int i=0;i<ncout;i++)c[4+i]=(u8)(1+i); o=put(b,o,c,4+ncout); }
    if(epin>=0){ u8 e[]={9,5,(u8)epin,2,(u8)mpin,(u8)(mpin>>8),0,0,0}; o=put(b,o,e,9); u8 c[20]={(u8)(4+ncin),0x25,1,(u8)ncin}; for(int i=0;i<ncin;i++)c[4+i]=(u8)(1+i); o=put(b,o,c,4+ncin); }
    u8 h[]={9,2,(u8)o,(u8)(o>>8),2,1,0,0x80,50}; memcpy(b,h,9); return o; }
static void t_desc(void){
    u8 b[512]; struct umc_cfg c;
    int n=mk(b,0x81,64,0x01,64,1,1,1);
    CK(umc_parse_cfg(b,n,&c)==1,"plain keyboard parses"); CK(c.ifn==1&&c.ie==0x81&&c.oe==0x01&&c.im==64&&c.om==64,"endpoints"); CK(c.ncin==1&&c.ncout==1,"one cable");
    n=mk(b,0x82,512,0x02,512,4,2,1);
    CK(umc_parse_cfg(b,n,&c)==1&&c.ncin==4&&c.ncout==2&&c.im==512,"multi-cable interface");
    n=mk(b,0x80,64,0x01,64,1,1,1);
    CK(umc_parse_cfg(b,n,&c)==1&&c.ie==0&&c.oe==0x01,"IN endpoint 0 rejected, OUT kept");
    n=mk(b,0x80,64,0x00,64,1,1,1);
    CK(umc_parse_cfg(b,n,&c)==0,"both endpoints 0 rejected");
    n=mk(b,0x81,0,0x01,0x1800|64,1,1,1);
    CK(umc_parse_cfg(b,n,&c)==1&&c.ie==0&&c.om==64,"zero max packet rejected, high-bandwidth bits masked");
    n=mk(b,0x81,64,-1,0,1,1,1);
    CK(umc_parse_cfg(b,n,&c)==1&&c.ie==0x81&&!c.oe,"input-only device");
    n=mk(b,0x81,64,0x01,64,0,1,0);
    CK(umc_parse_cfg(b,n,&c)==1&&c.ncin==1,"zero jacks counted as one cable");
    n=mk(b,0x81,64,0x01,64,1,1,1); b[1]=4;
    CK(umc_parse_cfg(b,n,&c)==0,"not a configuration descriptor");
    n=mk(b,0x81,64,0x01,64,1,1,1);
    CK(umc_parse_cfg(b,8,&c)==0,"truncated header");
    { int m=mk(b,0x81,64,0x01,64,16,16,1); b[m-4-16]=(u8)(4+30); CK(umc_parse_cfg(b,m,&c)==1&&c.ncin<=16,"jack count clamped to descriptor"); }
    { u8 x[64]={9,2,30,0,1,1,0,0x80,50, 9,4,0,0,2,1,3,0,0, 3,5,0x81}; CK(umc_parse_cfg(x,21,&c)==0,"short endpoint descriptor ignored"); }
    { u8 x[64]={9,2,40,0,1,1,0,0x80,50, 9,4,0,0,2,1,3,0,0, 9,5,0x81,3,64,0,1,0,0}; CK(umc_parse_cfg(x,27,&c)==0,"interrupt endpoint is not a MIDI bulk endpoint"); }
    { u8 x[64]={9,2,40,0,1,1,0,0x80,50, 9,4,0,0,2,1,1,0,0, 9,5,0x81,2,64,0,0,0,0}; CK(umc_parse_cfg(x,27,&c)==0,"AudioControl only is not MIDI"); }
    srand(7); for(int it=0;it<200000;it++){ u8 f[300]; int l=rand()%300; for(int i=0;i<l;i++)f[i]=(u8)rand(); if(l>2&&(it&1)){ f[0]=9; f[1]=2; } if(umc_parse_cfg(f,l,&c)){ CK((c.ie&0x0F)||(c.oe&0x0F),"fuzz: parsed endpoint nonzero"); CK(!c.ie||(c.im>=4&&c.im<=0x7FF),"fuzz mps"); CK(c.ncin>=1&&c.ncin<=16&&c.ncout>=1&&c.ncout<=16,"fuzz cables"); if(fails)return; } } }
static void t_abi(void){
    CK(ARM_IOC_PVERSION==SNDRV_RAWMIDI_IOCTL_PVERSION,"PVERSION"); CK(ARM_IOC_INFO==SNDRV_RAWMIDI_IOCTL_INFO,"INFO");
    CK(ARM_IOC_PARAMS==SNDRV_RAWMIDI_IOCTL_PARAMS,"PARAMS"); CK(ARM_IOC_STATUS==SNDRV_RAWMIDI_IOCTL_STATUS,"STATUS");
    CK(ARM_IOC_DROP==SNDRV_RAWMIDI_IOCTL_DROP,"DROP"); CK(ARM_IOC_DRAIN==SNDRV_RAWMIDI_IOCTL_DRAIN,"DRAIN");
#ifdef SNDRV_RAWMIDI_IOCTL_USER_PVERSION
    CK(ARM_IOC_USER_PVERSION==SNDRV_RAWMIDI_IOCTL_USER_PVERSION,"USER_PVERSION");
#endif
    CK(ARM_CTL_NEXT_DEVICE==SNDRV_CTL_IOCTL_RAWMIDI_NEXT_DEVICE,"CTL NEXT_DEVICE"); CK(ARM_CTL_INFO==SNDRV_CTL_IOCTL_RAWMIDI_INFO,"CTL INFO"); CK(ARM_CTL_PREFER_SUBDEVICE==SNDRV_CTL_IOCTL_RAWMIDI_PREFER_SUBDEVICE,"CTL PREFER");
    CK(ARM_VERSION<=SNDRV_RAWMIDI_VERSION&&(ARM_VERSION>>16)==2,"protocol version");
    CK(sizeof(struct snd_rawmidi_info)==ARM_INFO_SZ,"info size"); CK(sizeof(struct snd_rawmidi_params)==ARM_PARAMS_SZ,"params size"); CK(sizeof(struct snd_rawmidi_status)==ARM_STATUS_SZ,"status size");
    CK(offsetof(struct snd_rawmidi_info,device)==ARM_INFO_DEVICE&&offsetof(struct snd_rawmidi_info,subdevice)==ARM_INFO_SUBDEVICE&&offsetof(struct snd_rawmidi_info,stream)==ARM_INFO_STREAM,"info head");
    CK(offsetof(struct snd_rawmidi_info,card)==ARM_INFO_CARD&&offsetof(struct snd_rawmidi_info,flags)==ARM_INFO_FLAGS&&offsetof(struct snd_rawmidi_info,id)==ARM_INFO_ID,"info card/flags/id");
    CK(offsetof(struct snd_rawmidi_info,name)==ARM_INFO_NAME&&offsetof(struct snd_rawmidi_info,subname)==ARM_INFO_SUBNAME,"info names");
    CK(offsetof(struct snd_rawmidi_info,subdevices_count)==ARM_INFO_SUBCOUNT&&offsetof(struct snd_rawmidi_info,subdevices_avail)==ARM_INFO_SUBAVAIL,"info counts");
    CK(offsetof(struct snd_rawmidi_params,buffer_size)==ARM_PARAMS_BUFSZ&&offsetof(struct snd_rawmidi_params,avail_min)==ARM_PARAMS_AVAILMIN,"params fields");
    CK(offsetof(struct snd_rawmidi_status,avail)==ARM_STATUS_AVAIL&&offsetof(struct snd_rawmidi_status,xruns)==ARM_STATUS_XRUNS,"status fields");
    CK(SNDRV_RAWMIDI_STREAM_OUTPUT==0&&SNDRV_RAWMIDI_STREAM_INPUT==1&&SNDRV_RAWMIDI_INFO_OUTPUT==1&&SNDRV_RAWMIDI_INFO_INPUT==2&&SNDRV_RAWMIDI_INFO_DUPLEX==4,"stream and flag values");
    struct snd_rawmidi_params p; memset(&p,0,sizeof p); p.stream=1; p.buffer_size=4096; p.avail_min=1;
    CK(arm_params_ok((const u8*)&p,2)==0,"params ok for input"); CK(arm_params_ok((const u8*)&p,1)==-22,"params wrong stream");
    p.buffer_size=16; CK(arm_params_ok((const u8*)&p,3)==-22,"params tiny buffer"); p.buffer_size=4096; p.avail_min=5000; CK(arm_params_ok((const u8*)&p,3)==-22,"params avail_min > buffer"); }
int main(void){ t_desc(); t_abi(); if(fails){ printf("%d failed\n",fails); return 1; } printf("all passed\n"); return 0; }
