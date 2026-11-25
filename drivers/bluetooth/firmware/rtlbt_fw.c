/*-
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright (c) 2013 Adrian Chadd <adrian@freebsd.org>
 * Copyright (c) 2019 Vladimir Kondratyev <wulf@FreeBSD.org>
 * Copyright (c) 2023 Future Crew LLC.
 * Copyright (c) 2026 NoovexOS project (port to NoovexOS 64)
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */
#ifndef RTLBT_HOST
#include "include/nvx/base.h"
#include "include/nvx/core/klog.h"
#include "include/nvx/mem/heap.h"
#include "include/nvx/lib/base/fmt.h"
#include "include/nvx/fs/vfs/vfs.h"
#endif
#include "drivers/bluetooth/firmware/rtlbt_fw.h"
static const rtlbt_ic_t rtlbt_ic_tab[]={
    { 0x1200, 0x000b, 0x06, RTLBT_F_SIMPLE, "RTL8723A", "rtl8723a_fw.bin", 0 },
    { 0x8723, 0x000b, 0x06, 0, "RTL8723B", "rtl8723b_fw.bin", "rtl8723b_config.bin" },
    { 0x8723, 0x000d, 0x08, RTLBT_F_CONFIG, "RTL8723D", "rtl8723d_fw.bin", "rtl8723d_config.bin" },
    { 0x8821, 0x000a, 0x06, 0, "RTL8821A", "rtl8821a_fw.bin", "rtl8821a_config.bin" },
    { 0x8821, 0x000c, 0x08, RTLBT_F_MSFT|RTLBT_F_EMB, "RTL8821C", "rtl8821c_fw", "rtl8821c_config" },
    { 0x8761, 0x000a, 0x06, 0, "RTL8761A", "rtl8761a_fw.bin", "rtl8761a_config.bin" },
    { 0x8761, 0x000b, 0x0a, 0, "RTL8761BU", "rtl8761bu_fw.bin", "rtl8761bu_config.bin" },
    { 0x8822, 0x000c, 0x0a, RTLBT_F_MSFT|RTLBT_F_EMB, "RTL8822C", "rtl8822cu_fw", "rtl8822cu_config" },
    { 0x8822, 0x000b, 0x07, RTLBT_F_CONFIG|RTLBT_F_MSFT, "RTL8822B", "rtl8822b_fw.bin", "rtl8822b_config.bin" },
    { 0x8852, 0x000a, 0x0b, RTLBT_F_MSFT, "RTL8852A", "rtl8852au_fw.bin", "rtl8852au_config.bin" },
    { 0x8852, 0x000b, 0x0b, RTLBT_F_MSFT, "RTL8852B", "rtl8852bu_fw.bin", "rtl8852bu_config.bin" },
    { 0x8852, 0x000c, 0x0c, RTLBT_F_MSFT, "RTL8852C", "rtl8852cu_fw_v2.bin", "rtl8852cu_config.bin" },
    { 0x8851, 0x000b, 0x0c, RTLBT_F_MSFT, "RTL8851B", "rtl8851bu_fw.bin", "rtl8851bu_config.bin" },
    { 0x8922, 0x000a, 0x0c, RTLBT_F_MSFT, "RTL8922A", "rtl8922au_fw.bin", "rtl8922au_config.bin" },
    { 0x8852, 0x0087, 0x0c, RTLBT_F_MSFT, "RTL8852BT", "rtl8852btu_fw.bin", "rtl8852btu_config.bin" },
};
static const u16 rtlbt_usb_ids[][2]={
    {0x13d3,0x3529},{0x13d3,0x3600},{0x04c5,0x165c},{0x04ca,0x4006},{0x0cb8,0xc549},{0x04ca,0x4007},{0x04c5,0x1675},{0x0cb8,0xc558},
    {0x13d3,0x3587},{0x13d3,0x3586},{0x13d3,0x3592},{0x13d3,0x3612},{0x0489,0xe122},{0x0cb8,0xc559},{0x13d3,0x3570},{0x13d3,0x3571},
    {0x13d3,0x3572},{0x13d3,0x3591},{0x0489,0xe123},{0x0489,0xe125},{0x13d3,0x3617},{0x13d3,0x3616},{0x0489,0xe130},{0x0930,0x021d},
    {0x13d3,0x3394},{0x0489,0xe085},{0x0489,0xe08b},{0x04f2,0xb49f},{0x13d3,0x3410},{0x13d3,0x3416},{0x13d3,0x3459},{0x13d3,0x3494},
    {0x7392,0xa611},{0x2ff8,0xb011},{0x2c4e,0x0115},{0x2357,0x0604},{0x0b05,0x190e},{0x2550,0x8761},{0x6655,0x8771},{0x7392,0xc611},
    {0x2b89,0x8761},{0x0b05,0x17dc},{0x13d3,0x3414},{0x13d3,0x3458},{0x13d3,0x3461},{0x13d3,0x3462},{0x13d3,0x3526},{0x0b05,0x185c},
    {0x04ca,0x4005},{0x04c5,0x161f},{0x0b05,0x18ef},{0x13d3,0x3548},{0x13d3,0x3549},{0x13d3,0x3553},{0x13d3,0x3555},{0x2ff8,0x3051},
    {0x1358,0xc123},{0x0cb5,0xc547},
};
int rtlbt_usb_listed(u16 vid,u16 pid){ for(unsigned i=0;i<sizeof rtlbt_usb_ids/sizeof rtlbt_usb_ids[0];i++) if(rtlbt_usb_ids[i][0]==vid&&rtlbt_usb_ids[i][1]==pid)return 1; return 0; }
static const u16 rtlbt_proj[]={ [0]=0x1200, [1]=0x8723, [2]=0x8821, [3]=0x8761, [7]=0x8703, [8]=0x8822, [9]=0x8723, [10]=0x8821, [13]=0x8822, [14]=0x8761, [18]=0x8852, [19]=0x8723, [20]=0x8852, [25]=0x8852, [33]=0x8822, [36]=0x8851, [44]=0x8922, [47]=0x8852 };
static const u8 rtlbt_sig1[8]={'R','e','a','l','t','e','c','h'}, rtlbt_sig2[8]={'R','T','B','T','C','o','r','e'}, rtlbt_ext[4]={0x51,0x04,0xFD,0x77};
static int rtlbt_eq(const u8* a,const u8* b,int n){ for(int i=0;i<n;i++) if(a[i]!=b[i])return 0; return 1; }
static u16 rtlbt_g16(const u8* p){ return (u16)(p[0]|(p[1]<<8)); }
static u32 rtlbt_g32(const u8* p){ return (u32)p[0]|((u32)p[1]<<8)|((u32)p[2]<<16)|((u32)p[3]<<24); }
const rtlbt_ic_t* rtlbt_get_ic(u16 lmp,u16 rev,u8 ver){
    for(unsigned i=0;i<sizeof rtlbt_ic_tab/sizeof rtlbt_ic_tab[0];i++) if(rtlbt_ic_tab[i].lmp==lmp&&rtlbt_ic_tab[i].rev==rev&&rtlbt_ic_tab[i].ver==ver)return &rtlbt_ic_tab[i];
    return 0;
}
int rtlbt_fw_type(const u8* fw,int len,u16* lmp){
    int type, hl;
    if(len<8)return 0;
    if(rtlbt_eq(fw,rtlbt_sig1,8)){ type=1; hl=14; } else if(rtlbt_eq(fw,rtlbt_sig2,8)){ type=2; hl=20; } else return 0;
    if(len<hl+4+4)return 0;
    const u8* p=fw+len-4; if(!rtlbt_eq(p,rtlbt_ext,4))return 0;
    while(p-3>=fw+hl){
        u8 op=*--p, ol=*--p; if(p-ol<fw+hl)return 0; p-=ol;
        if(op==0x00){ if(ol!=1)return 0; u8 id=*p; if(id>=sizeof rtlbt_proj/sizeof rtlbt_proj[0]||!rtlbt_proj[id])return 0; *lmp=rtlbt_proj[id]; return type; }
        if(op==0xFF)break;
    }
    return 0;
}
int rtlbt_parse_v1(const u8* fw,int len,u8 rom,u8* out,int cap){
    int np=rtlbt_g16(fw+12);
    if(len<14+4+4+8*np)return -1;
    const u8* cb=fw+14; int i; u32 pl=0, po=0;
    for(i=0;i<np;i++){ if(rtlbt_g16(cb+i*2)!=rom+1)continue; pl=rtlbt_g16(cb+2*(np+i)); po=rtlbt_g32(cb+4*(np+i)); break; }
    if(i>=np||pl<4||po>(u32)len||pl>(u32)len-po||(int)pl>cap)return -1;
    for(u32 k=0;k<pl-4;k++)out[k]=fw[po+k];
    for(int k=0;k<4;k++)out[pl-4+k]=fw[8+k];
    return (int)pl;
}
#define RTLBT_MAXSUB 96
typedef struct { const u8* d; u32 len; u8 prio; } rtlbt_sub_t;
static int rtlbt_add(rtlbt_sub_t* s,int* n,const u8* d,u32 len,u8 prio){
    if(*n>=RTLBT_MAXSUB)return -1;
    int at=*n; for(int i=0;i<*n;i++) if(s[i].prio>=prio){ at=i; break; }
    for(int i=*n;i>at;i--)s[i]=s[i-1];
    s[at].d=d; s[at].len=len; s[at].prio=prio; (*n)++; return 0;
}
static int rtlbt_section(rtlbt_sub_t* s,int* n,u32 op,const u8* d,u32 len,u8 rom,u8 key){
    if(len<4)return -1;
    int num=rtlbt_g16(d); u32 o=4; int tot=0;
    for(int i=0;i<num;i++){
        if(len-o<8)break;
        const u8* h=d+o; u32 sl=rtlbt_g32(h+4); o+=8;
        if(sl>len-o)break;
        const u8* sd=d+o; o+=sl;
        if(h[0]!=rom+1)continue;
        if(op==3&&h[2]!=key)continue;
        if(rtlbt_add(s,n,sd,sl,h[1])<0)return -1;
        tot+=(int)sl;
    }
    return tot;
}
int rtlbt_parse_v2(const u8* fw,int len,u8 rom,u8 key,u8* out,int cap){
    if(len<20+7)return -1;
    static rtlbt_sub_t sub[RTLBT_MAXSUB]; int ns=0;
    u32 avail=(u32)len-7, o=20; u32 nsec=rtlbt_g32(fw+16); int tot=0;
    for(u32 i=0;i<nsec;i++){
        if(avail-o<8)break;
        u32 op=rtlbt_g32(fw+o), sl=rtlbt_g32(fw+o+4); o+=8;
        if(sl>avail-o)break;
        const u8* sd=fw+o; o+=sl;
        int r=0;
        if(op==1||op==2||(op==3&&key))r=rtlbt_section(sub,&ns,op,sd,sl,rom,key);
        if(r<0)return -1;
        tot+=r;
    }
    if(!tot||tot>cap)return -1;
    int pl=0; for(int i=0;i<ns;i++){ for(u32 k=0;k<sub[i].len;k++)out[pl+(int)k]=sub[i].d[k]; pl+=(int)sub[i].len; }
    return pl;
}
extern const u8 _binary_rtl8821c_fw_bin_start[] __attribute__((weak)), _binary_rtl8821c_fw_bin_end[] __attribute__((weak));
extern const u8 _binary_rtl8821c_config_bin_start[] __attribute__((weak)), _binary_rtl8821c_config_bin_end[] __attribute__((weak));
extern const u8 _binary_rtl8822cu_fw_bin_start[] __attribute__((weak)), _binary_rtl8822cu_fw_bin_end[] __attribute__((weak));
extern const u8 _binary_rtl8761bu_config_bin_start[] __attribute__((weak)), _binary_rtl8761bu_config_bin_end[] __attribute__((weak));
static int rtlbt_streq(const char* a,const char* b){ while(*a&&*a==*b){ a++; b++; } return *a==*b; }
int rtlbt_blob(const char* name,const u8** p){
    const u8* s=0; const u8* e=0;
    if(rtlbt_streq(name,"rtl8821c_fw")){ s=_binary_rtl8821c_fw_bin_start; e=_binary_rtl8821c_fw_bin_end; }
    else if(rtlbt_streq(name,"rtl8821c_config")){ s=_binary_rtl8821c_config_bin_start; e=_binary_rtl8821c_config_bin_end; }
    else if(rtlbt_streq(name,"rtl8822cu_fw")){ s=_binary_rtl8822cu_fw_bin_start; e=_binary_rtl8822cu_fw_bin_end; }
    else if(rtlbt_streq(name,"rtl8822cu_config")){ s=_binary_rtl8761bu_config_bin_start; e=_binary_rtl8761bu_config_bin_end; }
    if(!s||!e||e<=s)return 0;
    *p=s; return (int)(e-s);
}
#ifndef RTLBT_HOST
static u8* rtlbt_load(const char* path,int* len){
    int fd=vfs_open(path,O_RDONLY); if(fd<0)return 0;
    u32 cap=1u<<16, n=0; u8* b=(u8*)kmalloc(cap);
    while(b){
        if(n==cap){ if(cap>=RTLBT_FWMAX){ kfree(b); b=0; break; } u8* nb=(u8*)kmalloc(cap*2); if(!nb){ kfree(b); b=0; break; } for(u32 i=0;i<n;i++)nb[i]=b[i]; kfree(b); b=nb; cap*=2; }
        int r=vfs_read(fd,(char*)b+n,(int)(cap-n)); if(r<=0)break; n+=(u32)r;
    }
    vfs_close(fd);
    if(b&&!n){ kfree(b); b=0; }
    *len=(int)n; return b;
}
#endif
static void rtlbt_path(char* p,int cap,const char* nm){ const char* d=RTLBT_FWDIR; int i=0; while(*d&&i<cap-1)p[i++]=*d++; while(*nm&&i<cap-1)p[i++]=*nm++; p[i]=0; }
static void rtlbt_msg(char* m,int cap,const char* a,u32 v,int dg){
    int i=0; for(;a[i]&&i<cap-12;i++)m[i]=a[i];
    if(dg){ static const char h[]="0123456789abcdef"; m[i++]='0'; m[i++]='x'; for(int k=dg-1;k>=0;k--)m[i++]=h[(v>>(4*k))&15]; }
    m[i]=0;
}
static void rtlbt_msg2(char* m,int cap,const char* a,const char* b){ int i=0; while(*a&&i<cap-1)m[i++]=*a++; while(*b&&i<cap-1)m[i++]=*b++; m[i]=0; }
static int rtlbt_fetch(const rtlbt_ic_t* ic,const char* nm,const u8** p,u8** own){
    *own=0; if(!nm)return 0;
    if(ic->flags&RTLBT_F_EMB)return rtlbt_blob(nm,p);
    char path[96]; rtlbt_path(path,(int)sizeof path,nm); int l=0; u8* b=rtlbt_load(path,&l);
    if(!b||l<=0)return 0;
    *own=b; *p=b; return l;
}
int rtlbt_setup(rtlbt_cmd_fn cmd,char* msg,int cap){
    static const u8 subver[5]={0x10,0x38,0x04,0x28,0x80}, secproj[5]={0x10,0xA4,0xAD,0x00,0xB0};
    u8 rp[64]; int n;
    u16 romlmp=0; n=cmd(0xFC61,subver,5,rp,(int)sizeof rp); if(n>=3&&rp[0]==0)romlmp=rtlbt_g16(rp+1);
    n=cmd(0x1001,0,0,rp,(int)sizeof rp); if(n<9||rp[0]){ rtlbt_msg(msg,cap,"Realtek: Read Local Version failed",0,0); return -1; }
    u8 hver=rp[1]; u16 hrev=rtlbt_g16(rp+2), lmp=rtlbt_g16(rp+7);
    const rtlbt_ic_t* ic=rtlbt_get_ic(lmp,hrev,hver);
    if(!ic){
        if(romlmp&&romlmp!=lmp){ rtlbt_msg(msg,cap,"Realtek: firmware already running, version ",((u32)hrev<<16)|lmp,8); return 2; }
        rtlbt_msg(msg,cap,"Realtek: no firmware for this chip, LMP subversion ",lmp,4); return 0; }
    u8 rom=0;
    if(!(ic->flags&RTLBT_F_SIMPLE)){ n=cmd(0xFC6D,0,0,rp,(int)sizeof rp); if(n<2||rp[0]){ rtlbt_msg(msg,cap,"Realtek: Read ROM version failed",0,0); return -1; } rom=rp[1]; }
    const u8* fw=0; const u8* cf=0; u8* fwown=0; u8* cfown=0; char path[96];
    int fl=rtlbt_fetch(ic,ic->fw,&fw,&fwown);
    if(fl<=0){ if(ic->flags&RTLBT_F_EMB)rtlbt_msg(msg,cap,"Realtek: firmware file is not built in",0,0); else { rtlbt_path(path,(int)sizeof path,ic->fw); rtlbt_msg2(msg,cap,"Realtek: firmware file missing: ",path); } return -1; }
    int r=-1, pl=0, cl=0; u8* buf=0;
    u16 flmp=0; int ty=rtlbt_fw_type(fw,fl,&flmp);
    if(ic->flags&RTLBT_F_SIMPLE){
        if(fl>=8&&(rtlbt_eq(fw,rtlbt_sig1,8)||rtlbt_eq(fw,rtlbt_sig2,8))){ rtlbt_msg(msg,cap,"Realtek: unexpected epatch file for a simple chip",0,0); goto out; }
        buf=(u8*)kmalloc((unsigned long)fl+16); if(!buf){ rtlbt_msg(msg,cap,"Realtek: no memory",0,0); goto out; }
        pl=fl; ty=0; for(int i=0;i<fl;i++)buf[i]=fw[i];
    } else {
        if(!ty||flmp!=ic->lmp){ rtlbt_msg(msg,cap,"Realtek: firmware file does not match the chip",0,0); goto out; }
        cl=rtlbt_fetch(ic,ic->cfg,&cf,&cfown);
        if(cl<=0&&(ic->flags&RTLBT_F_CONFIG)){ rtlbt_path(path,(int)sizeof path,ic->cfg); rtlbt_msg2(msg,cap,"Realtek: config file missing: ",path); goto out; }
        u8 key=0; if(ty==2){ n=cmd(0xFC61,secproj,5,rp,(int)sizeof rp); if(n>=3&&rp[0]==0)key=rp[1]; }
        int cap2=fl+(cl>0?cl:0)+16; buf=(u8*)kmalloc((unsigned long)cap2); if(!buf){ rtlbt_msg(msg,cap,"Realtek: no memory",0,0); goto out; }
        pl=ty==1?rtlbt_parse_v1(fw,fl,rom,buf,cap2):rtlbt_parse_v2(fw,fl,rom,key,buf,cap2);
        if(pl<=0){ rtlbt_msg(msg,cap,"Realtek: no patch for ROM version ",rom,2); goto out; }
        if(cl>0&&pl+cl<=cap2){ for(int i=0;i<cl;i++)buf[pl+i]=cf[i]; pl+=cl; }
    }
    u8 dl[1+RTLBT_FRAG]; int nf=pl/RTLBT_FRAG+1, j=0; r=0;
    for(int i=0;i<nf;i++){
        int idx=j++; if(idx==0x7F)j=1;
        int fln=RTLBT_FRAG; if(i==nf-1){ idx|=0x80; fln=pl%RTLBT_FRAG; }
        dl[0]=(u8)idx; for(int k=0;k<fln;k++)dl[1+k]=buf[i*RTLBT_FRAG+k];
        n=cmd(0xFC20,dl,fln+1,rp,(int)sizeof rp);
        if(n<2||rp[0]){ rtlbt_msg(msg,cap,"Realtek: firmware download failed at block ",(u32)i,4); r=-1; break; }
    }
out:
    if(buf)kfree(buf);
    if(fwown)kfree(fwown);
    if(cfown)kfree(cfown);
    if(r<0)return -1;
    n=cmd(0x1001,0,0,rp,(int)sizeof rp);
    if(n>=9&&!rp[0])rtlbt_msg(msg,cap,ty==1?"Realtek: firmware loaded (V1), version ":ty==2?"Realtek: firmware loaded (V2), version ":"Realtek: firmware loaded, version ",((u32)rtlbt_g16(rp+2)<<16)|rtlbt_g16(rp+7),8);
    else rtlbt_msg(msg,cap,"Realtek: firmware sent, version read failed",0,0);
    return 1;
}
