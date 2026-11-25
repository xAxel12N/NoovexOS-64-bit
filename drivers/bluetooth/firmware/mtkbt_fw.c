/*
 * SPDX-License-Identifier: ISC
 *
 * Copyright (C) 2021 MediaTek Inc.
 * Copyright (c) 2026 NoovexOS project (port to NoovexOS 64)
 *
 * Permission to use, copy, modify, and/or distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */
#ifndef MTKBT_HOST
#include "include/nvx/base.h"
#include "include/nvx/core/klog.h"
#include "include/nvx/mem/heap.h"
#include "include/nvx/arch/x86_64/time/hrtimer.h"
#include "include/nvx/fs/vfs/vfs.h"
#include "include/nvx/drivers/bluetooth/bt.h"
void ui_pump(void);
extern volatile u32 g_ml_beat;
#endif
#include "drivers/bluetooth/firmware/mtkbt_fw.h"
static const u16 mtkbt_ids[][2]={
    {0x0489,0xe0c8},{0x0489,0xe0cd},{0x0489,0xe0e0},{0x0489,0xe0f2},{0x04ca,0x3802},{0x13d3,0x3563},{0x13d3,0x3564},{0x13d3,0x3567},{0x13d3,0x3576},{0x13d3,0x3578},{0x13d3,0x3583},{0x13d3,0x3606},
    {0x0489,0xe134},{0x0489,0xe135},{0x13d3,0x3620},{0x13d3,0x3621},{0x13d3,0x3622},{0x0489,0xe158},
    {0x0489,0xe156},{0x13d3,0x3579},{0x13d3,0x3580},{0x13d3,0x3594},{0x13d3,0x3596},
    {0x13d3,0x3585},{0x13d3,0x3610},
    {0x0489,0xe0d8},{0x0489,0xe0d9},{0x0489,0xe0e2},{0x0489,0xe0e4},{0x0489,0xe0f1},{0x0489,0xe0f5},{0x0489,0xe0f6},{0x0489,0xe102},{0x0489,0xe11d},{0x0489,0xe152},{0x0489,0xe153},{0x0489,0xe170},{0x0489,0xe174},
    {0x04ca,0x3804},{0x04ca,0x3807},{0x04ca,0x38e4},{0x13d3,0x3568},{0x13d3,0x3584},{0x13d3,0x3605},{0x13d3,0x3607},{0x13d3,0x3614},{0x13d3,0x3615},{0x13d3,0x3633},{0x35f5,0x7922},
    {0x0489,0xe111},{0x0489,0xe113},{0x0489,0xe118},{0x0489,0xe11e},{0x0489,0xe124},{0x0489,0xe139},{0x0489,0xe13a},{0x0489,0xe0fa},{0x0489,0xe10f},{0x0489,0xe110},{0x0489,0xe116},{0x13d3,0x3588},
    {0x0489,0xe14e},{0x0489,0xe14f},{0x0489,0xe150},{0x0489,0xe151},{0x13d3,0x3602},{0x13d3,0x3603},{0x13d3,0x3604},{0x13d3,0x3608},{0x13d3,0x3609},{0x13d3,0x3613},{0x13d3,0x3625},{0x13d3,0x3627},
    {0x13d3,0x3628},{0x13d3,0x3630},{0x2c7c,0x7009},
};
static const u16 mtkbt_6639[][2]={ {0x0489,0xe13a},{0x0489,0xe0fa},{0x0489,0xe10f},{0x0489,0xe110},{0x0489,0xe116},{0x13d3,0x3588} };
static u32 mtk_g32(const u8* p){ return (u32)p[0]|((u32)p[1]<<8)|((u32)p[2]<<16)|((u32)p[3]<<24); }
int mtkbt_match(u16 vid,u16 pid){
    if(vid==0x0E8D)return 1;
    for(unsigned i=0;i<sizeof mtkbt_ids/sizeof mtkbt_ids[0];i++) if(mtkbt_ids[i][0]==vid&&mtkbt_ids[i][1]==pid)return 1;
    return 0;
}
int mtkbt_supported(u32 id){ return id==0x7961||id==0x7922||id==0x7925||id==0x7902||id==0x6639; }
static char* mtk_s(char* p,char* e,const char* s){ while(*s&&p<e)*p++=*s++; return p; }
static char* mtk_hx(char* p,char* e,u32 v,int dg){ static const char h[]="0123456789abcdef"; if(!dg){ dg=1; while(dg<8&&(v>>(4*dg)))dg++; } for(int k=dg-1;k>=0&&p<e;k--)*p++=h[(v>>(4*k))&15]; return p; }
int mtkbt_fw_name(char* out,int cap,u32 id,u32 ver,u32 flavor){
    if(cap<2)return 0;
    char* p=out; char* e=out+cap-1; u32 n=(ver&0xff)+1;
    if(id==0x6639){ p=mtk_s(p,e,"mediatek/mt7927/BT_RAM_CODE_MT"); p=mtk_hx(p,e,id&0xffff,4); p=mtk_s(p,e,"_2_"); p=mtk_hx(p,e,n,0); }
    else if(id==0x7925){ p=mtk_s(p,e,"mediatek/mt"); p=mtk_hx(p,e,id&0xffff,4); p=mtk_s(p,e,"/BT_RAM_CODE_MT"); p=mtk_hx(p,e,id&0xffff,4); p=mtk_s(p,e,"_1_"); p=mtk_hx(p,e,n,0); }
    else { p=mtk_s(p,e,"mediatek/BT_RAM_CODE_MT"); p=mtk_hx(p,e,id&0xffff,4); p=mtk_s(p,e,id==0x7961&&flavor?"_1a_":"_1_"); p=mtk_hx(p,e,n,0); }
    p=mtk_s(p,e,"_hdr.bin"); *p=0;
    return p<e?(int)(p-out):0;
}
int mtkbt_parse(const u8* fw,u32 len,u32 id,mtkbt_sec_t* s,int cap){
    if(!fw||len<MTKBT_HDR+MTKBT_GD)return -1;
    u32 ns=mtk_g32(fw+MTKBT_HDR+12);
    if(!ns||ns>MTKBT_MAXSEC||(u32)MTKBT_HDR+MTKBT_GD+(u32)MTKBT_SEC*ns>len)return -1;
    int n=0;
    for(u32 i=0;i<ns;i++){
        const u8* m=fw+MTKBT_HDR+MTKBT_GD+MTKBT_SEC*i;
        u32 off=mtk_g32(m+4), dl=mtk_g32(m+16), mode=mtk_g32(m+32);
        if(!dl)continue;
        if(id==0x6639&&(mode&0xff)!=0x01)continue;
        if(off>len||dl>len-off)return -1;
        if(n>=cap)return -1;
        s[n].off=off; s[n].size=dl; s[n].idx=i; s[n].spec=m+MTKBT_SPEC_OFF; n++;
    }
    return n;
}
int mtkbt_wmt_status(u8 op,const u8* e,int n){
    if(n<7||e[0]!=0xE4)return -1;
    if(n>2+e[1])n=2+e[1];
    if(n<7)return -1;
    if(e[3]!=op)return -2;
    u8 fl=e[6];
    if(op==MTKBT_WMT_SEMAPHORE)return fl==2?MTKBT_ST_PATCH_UNDONE:MTKBT_ST_PATCH_DONE;
    if(op==MTKBT_WMT_FUNC_CTRL){
        if(n<9)return fl?MTKBT_ST_ON_UNDONE:MTKBT_ST_ON_DONE;
        u16 st=(u16)((e[7]<<8)|e[8]);
        return st==0x404?MTKBT_ST_ON_DONE:st==0x420?MTKBT_ST_ON_PROGRESS:MTKBT_ST_ON_UNDONE;
    }
    if(op==MTKBT_WMT_PATCH_DWNLD)return fl==2?MTKBT_ST_PATCH_DONE:fl==1?MTKBT_ST_PATCH_PROGRESS:MTKBT_ST_PATCH_UNDONE;
    return MTKBT_ST_INVALID;
}
static void mtk_log(const char* a,const char* b,u32 v,int dg){
    char m[160]; char* p=m; char* e=m+sizeof m-1;
    p=mtk_s(p,e,"bluetooth: MediaTek: "); p=mtk_s(p,e,a);
    if(b)p=mtk_s(p,e,b);
    if(dg){ p=mtk_s(p,e,"0x"); p=mtk_hx(p,e,v,dg); }
    *p=0; klog(m);
}
static void mtk_idle(void){
#ifndef MTKBT_HOST
    static u64 mtk_idle_t; u64 t=hr_ns(); if(t-mtk_idle_t>20000000ULL){ mtk_idle_t=t; g_ml_beat++; ui_pump(); }
#endif
}
static int mtk_reg_read(u32 reg,u32* v){
    u8 b[4]={0,0,0,0};
    if(!btusb_vctrl(0xC0,0x63,(u16)(reg>>16),(u16)reg,b,4))return 0;
    *v=mtk_g32(b); return 1;
}
static int mtk_uhw_write(u32 reg,u32 v){
    u8 b[4]={(u8)v,(u8)(v>>8),(u8)(v>>16),(u8)(v>>24)};
    return btusb_vctrl(0x5E,0x02,(u16)(reg>>16),(u16)reg,b,4);
}
static int mtk_evt_take(u8 op,const u8* e,int n,int* st){
    if(n<2||e[0]!=0xE4)return 0;
    int r=mtkbt_wmt_status(op,e,n);
    if(r==-2){ mtk_log("WMT reply for another command, op ",0,e[3],2); *st=-1; return 1; }
    if(r<0)return 0;
    *st=r; return 1;
}
static int mtk_wmt(u8 op,u8 flag,const u8* d,int dlen,int* st,u32 ms){
    u8 pk[3+255], e[64], ev[264];
    if(dlen<0||dlen>MTKBT_CHUNK)return -1;
    pk[0]=0x6F; pk[1]=0xFC; pk[2]=(u8)(5+dlen); pk[3]=1; pk[4]=op; pk[5]=(u8)(dlen+1); pk[6]=(u8)((dlen+1)>>8); pk[7]=flag;
    for(int i=0;i<dlen;i++)pk[8+i]=d[i];
    if(btusb_cmd(pk,8+dlen)<0){ mtk_log("WMT command could not be sent, op ",0,op,2); return -1; }
    u64 t0=hr_ns(), lim=(u64)ms*1000000ULL; int s=-1;
    while(hr_ns()-t0<lim){
        int n;
        while((n=btusb_evt(ev,(int)sizeof ev))>0) if(mtk_evt_take(op,ev,n,&s))goto got;
        for(int i=0;i<64;i++)e[i]=0;
        if(btusb_vctrl(0xC0,1,48,0,e,64)&&mtk_evt_take(op,e,64,&s))goto got;
        if(!btusb_ok())return -1;
        mtk_idle(); hr_delay_us(500);
    }
    mtk_log("WMT command timed out, op ",0,op,2);
    return -1;
got:
    if(s<0)return -1;
    if(st)*st=s;
    return 0;
}
static int mtk_download(const u8* fw,u32 len,u32 id){
    static mtkbt_sec_t sec[MTKBT_MAXSEC];
    int ns=mtkbt_parse(fw,len,id,sec,MTKBT_MAXSEC);
    if(ns<0){ mtk_log("firmware file is damaged or has a bad section table",0,0,0); return -1; }
    for(int i=0;i<ns;i++){
        u8 cmd[1+MTKBT_SPEC_LEN]; int st=0, retry=20;
        cmd[0]=0; for(int k=0;k<MTKBT_SPEC_LEN;k++)cmd[1+k]=sec[i].spec[k];
        for(;retry>0;retry--){
            if(mtk_wmt(MTKBT_WMT_PATCH_DWNLD,0,cmd,(int)sizeof cmd,&st,2000)<0)return -1;
            if(st!=MTKBT_ST_PATCH_PROGRESS)break;
            hr_delay_us(100000);
        }
        if(st==MTKBT_ST_PATCH_DONE)continue;
        if(st!=MTKBT_ST_PATCH_UNDONE){ mtk_log("section refused by the chip, status ",0,(u32)st,2); return -1; }
        const u8* p=fw+sec[i].off; u32 left=sec[i].size; int first=1;
        while(left>0){
            int dl=left>MTKBT_CHUNK?MTKBT_CHUNK:(int)left;
            u8 fl=first?1:(left-(u32)dl==0)?3:2; first=0;
            st=0; if(mtk_wmt(MTKBT_WMT_PATCH_DWNLD,fl,p,dl,&st,2000)<0){ mtk_log("download failed in section ",0,sec[i].idx,2); return -1; }
            if(st==MTKBT_ST_PATCH_PROGRESS){ mtk_log("unexpected busy reply in section ",0,sec[i].idx,2); return -1; }
            left-=(u32)dl; p+=dl;
        }
    }
    hr_delay_us(100000);
    return ns;
}
#ifndef MTKBT_HOST
static u8* mtk_load(const char* path,u32* len){
    int fd=vfs_open(path,O_RDONLY); if(fd<0)return 0;
    u32 cap=1u<<20, n=0; u8* b=(u8*)kmalloc(cap);
    while(b){
        if(n==cap){ if(cap>=MTKBT_FWMAX){ kfree(b); b=0; break; } u8* nb=(u8*)kmalloc(cap*2); if(!nb){ kfree(b); b=0; break; } for(u32 i=0;i<n;i++)nb[i]=b[i]; kfree(b); b=nb; cap*=2; }
        int r=vfs_read(fd,(char*)b+n,(int)(cap-n)); if(r<=0)break; n+=(u32)r;
    }
    vfs_close(fd);
    if(b&&!n){ kfree(b); b=0; }
    *len=n; return b;
}
#endif
int mtkbt_attach(u16 vid,u16 pid){
    if(!mtkbt_match(vid,pid))return 0;
    u32 id=0, ver=0, fl=0;
    if(!mtk_reg_read(0x70010200,&id)||!mtk_reg_read(0x80021004,&ver)||!mtk_reg_read(0x70010020,&fl)){ mtk_log("could not read the chip id",0,0,0); return -1; }
    fl=(fl&0x80)>>7;
    if(!id) for(unsigned i=0;i<sizeof mtkbt_6639/sizeof mtkbt_6639[0];i++) if(mtkbt_6639[i][0]==vid&&mtkbt_6639[i][1]==pid)id=0x6639;
    if(!mtkbt_supported(id)){ mtk_log("unsupported chip id ",0,id,8); return 0; }
    char nm[80], path[112]; if(!mtkbt_fw_name(nm,(int)sizeof nm,id,ver,fl))return -1;
    { char* p=path; char* e=path+sizeof path-1; p=mtk_s(p,e,"/lib/firmware/"); p=mtk_s(p,e,nm); *p=0; }
    u32 len=0; u8* fw=mtk_load(path,&len);
    if(!fw){ mtk_log("firmware file missing, Bluetooth stays off: ",path,0,0); return -1; }
    mtk_log("loading ",path,0,0);
    int r=mtk_download(fw,len,id);
    kfree(fw);
    if(r<0){ mtk_log("firmware download failed for chip ",0,id,4); return -1; }
    if(!mtk_uhw_write(0x74011890,0x00010001))mtk_log("endpoint reset option write failed",0,0,0);
    u8 on=1; int st=0;
    if(mtk_wmt(MTKBT_WMT_FUNC_CTRL,0,&on,1,&st,3000)<0){ mtk_log("could not switch the Bluetooth function on",0,0,0); return -1; }
    mtk_log("firmware loaded, Bluetooth function on, chip ",0,id,4);
    return 1;
}
