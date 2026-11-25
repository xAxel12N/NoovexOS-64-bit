#include <stdio.h>
#include <string.h>
#define FP_HOST
#include "lib/base/fmt.c"
#include "drivers/input/fingerprint/fp_usb.c"
int main(void){ char b[300]; int bad=0;
 struct { int v,p,k; } t[]={{0x04f3,0x0c1a,FPK_ELAN},{0x04f3,0x0903,FPK_ELAN},{0x04f3,0x0c7e,FPK_ELAN_MOC},{0x06cb,0x00bd,FPK_SYNA_MOC},{0x27c6,0x5840,FPK_GOODIX_MOC},{0x27c6,0x5110,FPK_GOODIX},{0x06cb,0x009a,FPK_VALIDITY},{0x138a,0x0097,FPK_VALIDITY},{0x8086,0x1234,0},
  {0x06cb,0x00c9,FPK_VALIDITY},{0x06cb,0x00df,FPK_SYNA_MOC},{0x06cb,0x00f9,FPK_SYNA_MOC},{0x06cb,0x00fc,FPK_SYNA_MOC},{0x06cb,0x0100,FPK_SYNA_MOC},{0x06cb,0x0103,FPK_SYNA_MOC},{0x06cb,0x0123,FPK_SYNA_MOC},{0x06cb,0x0124,FPK_SYNA_MOC},{0x06cb,0x0126,FPK_SYNA_MOC},{0x06cb,0x0129,FPK_SYNA_MOC},{0x06cb,0x00f0,FPK_SYNA_MOC},{0x06cb,0x0102,FPK_VALIDITY},{0x06cb,0xce67,FPK_UNSUPP},
  {0x04f3,0x0c4b,FPK_ELAN},{0x04f3,0x0c58,FPK_ELAN},{0x04f3,0x0c4c,FPK_UNSUPP},{0x04f3,0x0c00,FPK_UNSUPP},{0x04f3,0x0c7d,FPK_ELAN_MOC},{0x04f3,0x0cb6,FPK_ELAN_MOC},{0x04f3,0x0c80,FPK_UNSUPP},{0x04f3,0x0c34,FPK_UNSUPP},
  {0x27c6,0x63ac,FPK_GOODIX_MOC},{0x27c6,0x6594,FPK_GOODIX_MOC},{0x27c6,0x6515,FPK_GOODIX_MOC},{0x27c6,0x660a,FPK_GOODIX_MOC},{0x27c6,0x538c,FPK_GOODIX},
  {0x138a,0x0050,FPK_VALIDITY},{0x138a,0x0091,FPK_VALIDITY},{0x138a,0x0090,FPK_VALIDITY},{0x138a,0x00ab,FPK_VALIDITY}};
 for(unsigned i=0;i<sizeof t/sizeof t[0];i++){ const fp_id_t* d=fprint_lookup(t[i].v,t[i].p); int k=d?d->kind:0; if(k!=t[i].k){ printf("FAIL %04x:%04x\n",t[i].v,t[i].p); bad=1; } }
 { u16 vfs_ok[]={0x0001,0x0005,0x0008,0x0010,0x0011,0x0015,0x0017,0x0018,0x0050,0x0091}, vfs_no[]={0x0006,0x0007,0x003a,0x003c,0x003d,0x003f,0x0090,0x0092,0x0094,0x0097,0x009d,0x00a6,0x00ab};
   for(unsigned i=0;i<10;i++) if(fprint_lookup(0x138a,vfs_ok[i])->quirk!=1){ printf("FAIL vfs %04x has libfprint driver\n",vfs_ok[i]); bad=1; }
   for(unsigned i=0;i<13;i++) if(fprint_lookup(0x138a,vfs_no[i])->quirk!=0){ printf("FAIL vfs %04x\n",vfs_no[i]); bad=1; } }
 if(fprint_gx_stages(0x63ac)!=12||fprint_gx_stages(0x6515)!=17||fprint_gx_stages(0x5840)!=8||fprint_gx_stages(0x6594)!=8||fprint_gx_stages(0x6a94)!=12){ puts("FAIL goodix stages"); bad=1; }
 fp_host_vid=0x138a; fp_host_pid=0x0097; fprint_status_text(b,sizeof b); fputs(b,stdout); if(!strstr(b,"no libfprint driver")){ puts("FAIL vfs status"); bad=1; }
 fp_host_vid=0x138a; fp_host_pid=0x0050; fprint_status_text(b,sizeof b); fputs(b,stdout); if(!strstr(b,"libfprint has a driver")){ puts("FAIL vfs status 2"); bad=1; }
 fp_host_vid=0x06cb; fp_host_pid=0x00bd; fprint_status_text(b,sizeof b); fputs(b,stdout);
 fp_host_vid=0x04f3; fp_host_pid=0x0c1a; fprint_status_text(b,sizeof b); fputs(b,stdout);
 fp_host_vid=0; fprint_status_text(b,sizeof b); fputs(b,stdout);
 puts(bad?"FAIL":"ALL PASS"); return bad; }
