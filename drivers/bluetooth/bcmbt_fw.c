/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * Bluetooth support for Broadcom devices
 * Copyright (C) 2015  Intel Corporation
 * Copyright (c) 2026 NoovexOS project (port to NoovexOS 64)
 *
 * Derived from Linux drivers/bluetooth/btbcm.c (GPL-2.0-or-later).
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */
#ifndef BCMBT_HOST
#include "include/nvx/base.h"
#include "include/nvx/core/klog.h"
#include "include/nvx/mem/heap.h"
#include "include/nvx/arch/x86_64/time/hrtimer.h"
#include "include/nvx/fs/vfs/vfs.h"
#endif
#include "drivers/bluetooth/bcmbt_fw.h"
#define BCMBT_MAXFW (256*1024)
#define BCMBT_DIR "/lib/firmware/brcm/"
static const u16 bcmbt_vids[]={ 0x0A5C, 0x0B05, 0x0489, 0x04CA, 0x050D, 0x13D3, 0x413C, 0x0930, 0x105B, 0x0BB4, 0x19FF };
static const struct { u16 sv; const char* nm; } bcmbt_sv[]={
    { 0x2105, "BCM20703A1" }, { 0x210b, "BCM43142A0" }, { 0x2112, "BCM4314A0" }, { 0x2118, "BCM20702A0" },
    { 0x2126, "BCM4335A0" }, { 0x220e, "BCM20702A1" }, { 0x230f, "BCM4356A2" }, { 0x4106, "BCM4335B0" },
    { 0x410e, "BCM20702B0" }, { 0x6109, "BCM4335C0" }, { 0x610c, "BCM4354" }, { 0x6607, "BCM4350C5" },
};
static u16 bcmbt_g16(const u8* p){ return (u16)(p[0]|(p[1]<<8)); }
int bcmbt_usb_vendor(u16 vid){ for(unsigned i=0;i<sizeof bcmbt_vids/sizeof bcmbt_vids[0];i++) if(bcmbt_vids[i]==vid)return 1; return 0; }
int bcmbt_match(u16 vid,u16 manuf){ return manuf==0x000F||vid==0x0A5C; }
const char* bcmbt_hw_name(u16 subver){ for(unsigned i=0;i<sizeof bcmbt_sv/sizeof bcmbt_sv[0];i++) if(bcmbt_sv[i].sv==subver)return bcmbt_sv[i].nm; return 0; }
int bcmbt_hcd_check(const u8* d,int len,int* nrec,int* nlaunch){
    int o=0, r=0, l=0;
    if(!d||len<3)return -1;
    while(len-o>=3){ u16 op=bcmbt_g16(d+o); int pl=d[o+2]; if((op>>10)!=0x3F||pl>len-o-3)return -1; if(op==0xFC4E)l++; o+=3+pl; r++; }
    if(o!=len)return -1;
    if(nrec)*nrec=r;
    if(nlaunch)*nlaunch=l;
    return 0;
}
static char* bcmbt_s(char* p,char* e,const char* s){ while(*s&&p<e)*p++=*s++; *p=0; return p; }
static char* bcmbt_x(char* p,char* e,u32 v,int dg){ static const char h[]="0123456789abcdef"; for(int k=dg-1;k>=0&&p<e;k--)*p++=h[(v>>(4*k))&15]; *p=0; return p; }
static char* bcmbt_d(char* p,char* e,u32 v,int dg){ char t[10]; int n=0; do{ t[n++]=(char)('0'+v%10); v/=10; }while(v&&n<10); while(n<dg&&n<10)t[n++]='0'; while(n&&p<e)*p++=t[--n]; *p=0; return p; }
int bcmbt_fw_names(const char* hw,u16 vid,u16 pid,char names[3][48]){
    int n=0;
    if(hw){ char* p=names[n]; char* e=p+47; p=bcmbt_s(p,e,hw); p=bcmbt_s(p,e,"-"); p=bcmbt_x(p,e,vid,4); p=bcmbt_s(p,e,"-"); p=bcmbt_x(p,e,pid,4); bcmbt_s(p,e,".hcd"); n++; }
    { char* p=names[n]; char* e=p+47; p=bcmbt_s(p,e,"BCM-"); p=bcmbt_x(p,e,vid,4); p=bcmbt_s(p,e,"-"); p=bcmbt_x(p,e,pid,4); bcmbt_s(p,e,".hcd"); n++; }
    if(hw){ char* p=names[n]; char* e=p+47; p=bcmbt_s(p,e,hw); bcmbt_s(p,e,".hcd"); n++; }
    return n;
}
#ifndef BCMBT_HOST
static void bcmbt_sleep_ms(int ms){ hr_delay_us((u32)ms*1000u); }
static u8* bcmbt_read(const char* name,int* len){
    char path[96]; char* p=bcmbt_s(path,path+95,BCMBT_DIR); bcmbt_s(p,path+95,name);
    *len=0; int fd=vfs_open(path,O_RDONLY); if(fd<0)return 0;
    u8* b=(u8*)kmalloc(BCMBT_MAXFW+1); if(!b){ vfs_close(fd); *len=-1; return 0; }
    int n=0; for(int i=0;i<4096&&n<=BCMBT_MAXFW;i++){ int r=vfs_read(fd,(char*)b+n,BCMBT_MAXFW+1-n); if(r<=0)break; n+=r; }
    vfs_close(fd);
    if(n<=0||n>BCMBT_MAXFW){ kfree(b); *len=-1; return 0; }
    *len=n; return b;
}
#endif
static char* bcmbt_id(char* p,char* e,const char* hw,u16 sv,u16 rev){
    p=bcmbt_s(p,e,"Broadcom "); p=bcmbt_s(p,e,hw?hw:"BCM"); p=bcmbt_s(p,e," (");
    p=bcmbt_d(p,e,(sv&0xE000u)>>13,3); p=bcmbt_s(p,e,"."); p=bcmbt_d(p,e,(sv&0x1F00u)>>8,3); p=bcmbt_s(p,e,"."); p=bcmbt_d(p,e,sv&0xFFu,3);
    p=bcmbt_s(p,e,") build "); return bcmbt_d(p,e,rev&0x0FFFu,4);
}
static int bcmbt_reset(bcmbt_cmd_fn cmd){ u8 rp[8]; int n=cmd(0x0C03,0,0,rp,(int)sizeof rp); if(n<1||rp[0])return 0; bcmbt_sleep_ms(100); return 1; }
static int bcmbt_ver(bcmbt_cmd_fn cmd,u16* sv,u16* rev){ u8 rp[16]; int n=cmd(0x1001,0,0,rp,(int)sizeof rp); if(n<9||rp[0])return 0; *rev=bcmbt_g16(rp+2); *sv=bcmbt_g16(rp+7); return 1; }
static void bcmbt_name(bcmbt_cmd_fn cmd){
    static u8 rp[260]; int n=cmd(0x0C14,0,0,rp,(int)sizeof rp); if(n<2||rp[0])return;
    char b[80]; char* p=bcmbt_s(b,b+79,"bluetooth: Broadcom local name: "); for(int i=1;i<n&&rp[i]&&p<b+79;i++)*p++=(rp[i]>=32&&rp[i]<127)?(char)rp[i]:'?'; *p=0; klog(b);
}
int bcmbt_setup(bcmbt_cmd_fn cmd,u16 vid,u16 pid,char* msg,int cap){
    char* e=msg+cap-1; char* p; u8 rp[16]; int n; u16 sv=0, rev=0;
    if(cap<2)return 0;
    msg[0]=0;
    if(!bcmbt_reset(cmd)){ bcmbt_s(msg,e,"Broadcom: HCI Reset failed, firmware not loaded"); return 0; }
    if(!bcmbt_ver(cmd,&sv,&rev)){ bcmbt_s(msg,e,"Broadcom: Read Local Version failed, firmware not loaded"); return 0; }
    const char* hw=bcmbt_hw_name(sv);
    n=cmd(0xFC5A,0,0,rp,(int)sizeof rp); if(n>=5&&!rp[0]){ vid=bcmbt_g16(rp+1); pid=bcmbt_g16(rp+3); }
    bcmbt_name(cmd);
    char names[3][48]; int nn=bcmbt_fw_names(hw,vid,pid,names), fi=-1, fl=0; u8* fw=0;
    for(int i=0;i<nn&&!fw;i++){ fw=bcmbt_read(names[i],&fl); if(fw)fi=i; else if(fl<0)break; }
    if(!fw){ if(fl>=0) for(int i=0;i<nn;i++)klog2("bluetooth: Broadcom firmware not found: " BCMBT_DIR,names[i]);
        p=bcmbt_id(msg,e,hw,sv,rev); bcmbt_s(p,e,fl<0?": .hcd unreadable or too big, ROM firmware in use":": no .hcd file, ROM firmware in use"); return 0; }
    int nrec=0, nl=0;
    if(bcmbt_hcd_check(fw,fl,&nrec,&nl)<0||!nl){ kfree(fw); p=bcmbt_id(msg,e,hw,sv,rev); p=bcmbt_s(p,e,": "); p=bcmbt_s(p,e,names[fi]); bcmbt_s(p,e," invalid, ROM firmware in use"); return 0; }
    n=cmd(0xFC2E,0,0,rp,(int)sizeof rp);
    if(n<1||rp[0]){ kfree(fw); p=bcmbt_id(msg,e,hw,sv,rev); bcmbt_s(p,e,": Download Minidriver refused, ROM firmware in use"); return 0; }
    bcmbt_sleep_ms(50);
    int o=0, bad=-1;
    for(int i=0;i<nrec;i++){ u16 op=bcmbt_g16(fw+o); int pl=fw[o+2];
        n=cmd(op,fw+o+3,pl,rp,(int)sizeof rp);
        if((n<1||rp[0])&&op!=0xFC4E){ bad=i; break; }
        o+=3+pl; }
    kfree(fw);
    bcmbt_sleep_ms(250);
    int rs=bcmbt_reset(cmd);
    u16 sv2=sv, rev2=rev; int vok=rs&&bcmbt_ver(cmd,&sv2,&rev2);
    if(vok)bcmbt_name(cmd);
    p=bcmbt_id(msg,e,hw,sv2,rev2);
    if(bad>=0){ p=bcmbt_s(p,e,": patch failed at record "); p=bcmbt_d(p,e,(u32)bad,1); bcmbt_s(p,e,", controller reset"); return 0; }
    if(!vok){ bcmbt_s(p,e,": patch sent, controller did not answer after reset"); return 0; }
    p=bcmbt_s(p,e,": loaded "); bcmbt_s(p,e,names[fi]);
    return 1;
}
