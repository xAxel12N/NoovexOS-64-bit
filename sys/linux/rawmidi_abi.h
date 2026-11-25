#ifndef NVX_RAWMIDI_ABI_H
#define NVX_RAWMIDI_ABI_H
#define ARM_IOC(d,t,n,s) ((unsigned)(((d)<<30)|((s)<<16)|((t)<<8)|(n)))
#define ARM_VERSION 0x20004
#define ARM_INFO_SZ 268
#define ARM_PARAMS_SZ 48
#define ARM_STATUS_SZ 56
#define ARM_IOC_PVERSION ARM_IOC(2u,'W',0x00,4)
#define ARM_IOC_INFO ARM_IOC(2u,'W',0x01,ARM_INFO_SZ)
#define ARM_IOC_USER_PVERSION ARM_IOC(1u,'W',0x02,4)
#define ARM_IOC_PARAMS ARM_IOC(3u,'W',0x10,ARM_PARAMS_SZ)
#define ARM_IOC_STATUS ARM_IOC(3u,'W',0x20,ARM_STATUS_SZ)
#define ARM_IOC_DROP ARM_IOC(1u,'W',0x30,4)
#define ARM_IOC_DRAIN ARM_IOC(1u,'W',0x31,4)
#define ARM_CTL_NEXT_DEVICE ARM_IOC(3u,'U',0x40,4)
#define ARM_CTL_INFO ARM_IOC(3u,'U',0x41,ARM_INFO_SZ)
#define ARM_CTL_PREFER_SUBDEVICE ARM_IOC(1u,'U',0x42,4)
#define ARM_INFO_DEVICE 0
#define ARM_INFO_SUBDEVICE 4
#define ARM_INFO_STREAM 8
#define ARM_INFO_CARD 12
#define ARM_INFO_FLAGS 16
#define ARM_INFO_ID 20
#define ARM_INFO_NAME 84
#define ARM_INFO_SUBNAME 164
#define ARM_INFO_SUBCOUNT 196
#define ARM_INFO_SUBAVAIL 200
#define ARM_PARAMS_STREAM 0
#define ARM_PARAMS_BUFSZ 8
#define ARM_PARAMS_AVAILMIN 16
#define ARM_STATUS_STREAM 0
#define ARM_STATUS_TSTAMP 8
#define ARM_STATUS_AVAIL 24
#define ARM_STATUS_XRUNS 32
#define ARM_BUFSZ 4096
static int arm_params_ok(const unsigned char* b,int smask){
    int st=*(const int*)(b+ARM_PARAMS_STREAM); unsigned long bs=*(const unsigned long*)(b+ARM_PARAMS_BUFSZ), am=*(const unsigned long*)(b+ARM_PARAMS_AVAILMIN);
    if(st<0||st>1||!((smask>>st)&1))return -22;
    if(bs<32||bs>1024ul*1024ul||am<1||am>bs)return -22;
    return 0; }
#endif
