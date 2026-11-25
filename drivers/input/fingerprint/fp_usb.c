#include "include/nvx/base.h"
#include "include/nvx/lib/base/fmt.h"
#include "include/nvx/drivers/input/fingerprint/fprint.h"
#ifndef FP_HOST
#include "include/nvx/drivers/usb/xhci.h"
#endif
static const u16 FP_ELAN_X[]={0x0903,0x0907,0x0c3d,0x0c42,0x0c4b,0x0c4d,0x0c4f,0x0c58,0x0c63,0x0c6e};
static const u16 FP_SYNA[]={0x00bd,0x00c2,0x00c4,0x00c6,0x00df,0x00e9,0x00f0,0x00f9,0x00fc,0x0100,0x0103,0x0104,0x0106,0x0107,0x0108,0x0109,0x010a,0x010b,0x010d,0x010e,0x0123,0x0124,0x0126,0x0129,0x015f,0x0168,0x0169,0x016c,0x0173,0x0174,0x019d,0x019f,0x01a0,0x01a4};
static const u16 FP_GXMOC[]={0x5840,0x6014,0x6082,0x6090,0x6092,0x6094,0x609a,0x609c,0x60a2,0x60a4,0x60bc,0x60c2,0x6304,0x631c,0x633c,0x634c,0x6382,0x6384,0x639c,0x63ac,0x63bc,0x63cc,0x6496,0x650a,0x650c,0x6582,0x6584,0x658c,0x6592,0x6594,0x659a,0x659c,0x6a94,0x6512,0x6890,0x689a,0x66a9,0x6984,0x65ba,0x6515,0x66ac,0x660a};
static const u16 FP_VFS[]={0x0001,0x0005,0x0008,0x0010,0x0011,0x0015,0x0017,0x0018,0x0050,0x0091};
static const u16 FP_EMOC[]={0x0c7d,0x0c7e,0x0c82,0x0c88,0x0c8c,0x0c8d,0x0c98,0x0c99,0x0c9c,0x0c9d,0x0c9f,0x0ca3,0x0ca7,0x0ca8,0x0cb0,0x0cb2,0x0cb5,0x0cb6};
static const u16 FP_GX12[]={0x6496,0x60a2,0x60a4,0x6014,0x6090,0x6092,0x6094,0x609a,0x609c,0x60bc,0x60c2,0x6304,0x631c,0x633c,0x634c,0x6382,0x6384,0x639c,0x63ac,0x63bc,0x63cc,0x650a,0x650c,0x6582,0x6a94,0x659a,0x6890,0x6984,0x65ba};
static int fp_in(const u16* t,unsigned n,u16 p){ for(unsigned i=0;i<n;i++) if(t[i]==p)return 1; return 0; }
int fprint_gx_stages(u16 pid){ return pid==0x6515?17:fp_in(FP_GX12,sizeof FP_GX12/2,pid)?12:8; }
static fp_id_t fp_hit;
const fp_id_t* fprint_lookup(u16 vid,u16 pid){
    fp_hit.vid=vid; fp_hit.pid=pid; fp_hit.kind=FPK_NONE; fp_hit.quirk=0;
    if(vid==0x04f3){
        if(pid>=0x0c01&&pid<=0x0c33)fp_hit.kind=FPK_ELAN;
        for(unsigned i=0;i<sizeof FP_ELAN_X/2;i++) if(FP_ELAN_X[i]==pid)fp_hit.kind=FPK_ELAN;
        if(fp_hit.kind==FPK_ELAN)fp_hit.quirk=pid==0x0907?1:pid==0x0c03?2:pid==0x0c42?4:pid==0x0c58?8:0;
        else if(fp_in(FP_EMOC,sizeof FP_EMOC/2,pid))fp_hit.kind=FPK_ELAN_MOC;
        else if(pid>=0x0c00&&pid<=0x0cff)fp_hit.kind=FPK_UNSUPP;
    } else if(vid==0x06cb){ for(unsigned i=0;i<sizeof FP_SYNA/2;i++) if(FP_SYNA[i]==pid)fp_hit.kind=FPK_SYNA_MOC; if(!fp_hit.kind&&pid<0x0200)fp_hit.kind=FPK_VALIDITY; else if(!fp_hit.kind&&(pid==0xce67||pid==0xceef))fp_hit.kind=FPK_UNSUPP; }
    else if(vid==0x27c6){ fp_hit.kind=FPK_GOODIX; for(unsigned i=0;i<sizeof FP_GXMOC/2;i++) if(FP_GXMOC[i]==pid)fp_hit.kind=FPK_GOODIX_MOC; }
    else if(vid==0x138a){ fp_hit.kind=FPK_VALIDITY; int ok=0; for(unsigned i=0;i<sizeof FP_VFS/2;i++) if(FP_VFS[i]==pid)ok=1; fp_hit.quirk=(u8)ok; }
    return fp_hit.kind?&fp_hit:0;
}
const char* fprint_kind_name(int k){
    return k==FPK_ELAN?"Elan image sensor (libfprint elan, ported)":k==FPK_ELAN_MOC?"Elan match-on-chip (libfprint elanmoc, not ported)":k==FPK_SYNA_MOC?"Synaptics match-on-chip (libfprint synaptics, ported)":k==FPK_GOODIX_MOC?"Goodix match-on-chip (libfprint goodixmoc, ported)":
           k==FPK_GOODIX?"Goodix image sensor (not supported even on Linux)":k==FPK_VALIDITY?"Validity/Synaptics VFS (libfprint vfs*, not ported)":k==FPK_UNSUPP?"Elan/Synaptics reader with no open driver (unsupported even on Linux)":"unknown";
}
int fprint_supported(int k){ return k==FPK_ELAN||k==FPK_SYNA_MOC||k==FPK_GOODIX_MOC; }
int fprint_is_moc(int k){ return k==FPK_SYNA_MOC||k==FPK_GOODIX_MOC; }
#ifdef FP_HOST
int fp_host_vid, fp_host_pid;
int fprint_find(u16* vid,u16* pid){ if(!fprint_lookup((u16)fp_host_vid,(u16)fp_host_pid))return 0; *vid=(u16)fp_host_vid; *pid=(u16)fp_host_pid; return fp_hit.kind; }
#else
int fprint_find(u16* vid,u16* pid){
    for(int k=0;k<USBDEV_MAX;k++) if(g_usbdev[k].used&&fprint_lookup(g_usbdev[k].vid,g_usbdev[k].pid)){ *vid=g_usbdev[k].vid; *pid=g_usbdev[k].pid; return fp_hit.kind; }
    return 0;
}
#endif
int fprint_status_text(char* out,int cap){
    char b[256],*p=b; u16 v=0,d=0; int k=fprint_find(&v,&d);
    if(!k)p=scopy(p,"fingerprint: no USB fingerprint reader found\n");
    else { p=scopy(p,"fingerprint: reader "); p=hx(p,v,4); *p++=':'; p=hx(p,d,4); p=scopy(p," - "); p=scopy(p,fprint_kind_name(k));
        if(k==FPK_VALIDITY)p=scopy(p,fp_hit.quirk&&v==0x138a?" [libfprint has a driver]":" [no libfprint driver either]");
        p=scopy(p,fprint_supported(k)?"\n":"\nfingerprint: this reader needs a protocol NoovexOS does not have yet; sign in with your password\n"); }
    *p=0; int n=(int)(p-b); if(n>=cap)n=cap-1; if(n<0)return 0;
    for(int i=0;i<n;i++){ out[i]=b[i]; } out[n]=0; return n;
}
