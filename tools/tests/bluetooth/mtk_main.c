#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdint.h>
typedef uint8_t u8; typedef uint16_t u16; typedef uint32_t u32; typedef uint64_t u64;
#define MTKBT_HOST
static u64 clk;
static int nlog; static char lastlog[256];
static void klog(const char* s){ nlog++; snprintf(lastlog,sizeof lastlog,"%s",s); printf("    log: %s\n",s); }
static u64 hr_ns(void){ return clk+=1000; }
static void hr_delay_us(u32 us){ clk+=(u64)us*1000; }
static void kfree(void* p){ free(p); }
static struct { u32 id, ver, flav; int ok, mute, busy_once, done_sec, ncmd, nctl, npoll; u8 ev[64]; int evn, delay; u8 got[1<<16]; u32 gotn; u8 flags[4096]; int nfl; u8 spec[8][53]; int nspec; int funcon, eprst; u32 eprst_v; } dev;
static u8* fwfile; static u32 fwlen; static char lastpath[160];
static u8* mtk_load(const char* path,u32* len){ snprintf(lastpath,sizeof lastpath,"%s",path); if(!fwfile)return 0; u8* b=malloc(fwlen); memcpy(b,fwfile,fwlen); *len=fwlen; return b; }
static int btusb_ok(void){ return dev.ok; }
static int btusb_evt(u8* out,int cap){ (void)out; (void)cap; return 0; }
static void reply(u8 op,u8 flag,int st){ u8* e=dev.ev; e[0]=0xE4; e[2]=1; e[3]=op; e[4]=1; e[5]=0; e[6]=flag; int n=7; if(st>=0){ e[7]=(u8)(st>>8); e[8]=(u8)st; n=9; } e[1]=(u8)(n-2); dev.evn=n; dev.delay=1; }
static int btusb_cmd(const u8* pk,int n){
    dev.ncmd++;
    if(n<8||pk[0]!=0x6F||pk[1]!=0xFC||pk[2]!=n-3||pk[3]!=1)return -1;
    u8 op=pk[4], fl=pk[7]; int dl=(pk[5]|(pk[6]<<8))-1; const u8* d=pk+8;
    if(dl!=n-8)return -1;
    if(dev.mute)return 0;
    if(op==0x01&&fl==0){
        if(dl!=53||d[0]!=0)return -1;
        if(dev.nspec<8)memcpy(dev.spec[dev.nspec],d,53);
        dev.nspec++;
        if(dev.busy_once){ dev.busy_once=0; reply(op,1,-1); return 0; }
        if(dev.done_sec&&dev.nspec==dev.done_sec){ reply(op,2,-1); return 0; }
        reply(op,0,-1); return 0;
    }
    if(op==0x01){ memcpy(dev.got+dev.gotn,d,(size_t)dl); dev.gotn+=(u32)dl; if(dev.nfl<4096)dev.flags[dev.nfl++]=fl; reply(op,0,-1); return 0; }
    if(op==0x06){ if(dl==1&&d[0]==1)dev.funcon++; reply(op,0,0x404); return 0; }
    return -1;
}
static int btusb_vctrl(u8 rt,u8 req,u16 val,u16 idx,u8* buf,u16 len){
    dev.nctl++; u32 reg=((u32)val<<16)|idx;
    if(rt==0xC0&&req==0x63&&len==4){ u32 v=reg==0x70010200?dev.id:reg==0x80021004?dev.ver:reg==0x70010020?dev.flav:0; buf[0]=(u8)v; buf[1]=(u8)(v>>8); buf[2]=(u8)(v>>16); buf[3]=(u8)(v>>24); return 1; }
    if(rt==0xC0&&req==1&&val==48&&len==64){ dev.npoll++; if(!dev.evn||dev.delay-->0){ return 1; } memcpy(buf,dev.ev,(size_t)dev.evn); dev.evn=0; return 1; }
    if(rt==0x5E&&req==2&&len==4){ dev.eprst++; dev.eprst_v=(u32)buf[0]|((u32)buf[1]<<8)|((u32)buf[2]<<16)|((u32)buf[3]<<24); if(reg!=0x74011890)return 0; return 1; }
    return 0;
}
#include "drivers/bluetooth/firmware/mtkbt_fw.c"
static int fails, checks;
#define CHECK(c,msg) do{ checks++; if(c)printf("  %-64s ok\n",msg); else { fails++; printf("  %-64s FAIL (line %d)\n",msg,__LINE__); } }while(0)
static void p32(u8* p,u32 v){ p[0]=(u8)v; p[1]=(u8)(v>>8); p[2]=(u8)(v>>16); p[3]=(u8)(v>>24); }
static u32 build(u8* f,int nsec,const u32* sizes,const u32* modes){
    memset(f,0,8192); memcpy(f,"20260101123456ab",16); memcpy(f+16,"ALPS",4);
    p32(f+32+12,(u32)nsec);
    u32 off=32+64+64*(u32)nsec;
    for(int i=0;i<nsec;i++){ u8* m=f+96+64*i; p32(m,0x30000+i); p32(m+4,off); p32(m+8,sizes[i]); p32(m+12,0x900000+0x1000u*i); p32(m+16,sizes[i]); p32(m+32,modes?modes[i]:1);
        for(int k=12;k<64;k++)if(k<16||k>19)if(k<32||k>35)m[k]=m[k]?m[k]:(u8)(0x40+i+k);
        for(u32 k=0;k<sizes[i];k++){ f[off+k]=(u8)(i*31+k*7+1); } off+=sizes[i]; }
    return off;
}
static void reset_dev(u32 id){ memset(&dev,0,sizeof dev); dev.id=id; dev.ver=1; dev.ok=1; nlog=0; lastpath[0]=0; }
int main(void){
    static u8 f[1<<16]; static mtkbt_sec_t s[MTKBT_MAXSEC]; char nm[96];
    printf("MediaTek Bluetooth firmware (mtkbt_fw.c) host test\n");
    CHECK(mtkbt_match(0x0E8D,0x0608)&&mtkbt_match(0x0E8D,0x0616)&&mtkbt_match(0x13D3,0x3563)&&mtkbt_match(0x0489,0xE0D8)&&mtkbt_match(0x13D3,0x3588),"MediaTek and OEM USB ids match");
    CHECK(!mtkbt_match(0x0BDA,0xC123)&&!mtkbt_match(0x8087,0x0026)&&!mtkbt_match(0x0A5C,0x21E8)&&!mtkbt_match(0x13D3,0x3560),"Realtek 0BDA:C123, Intel, Broadcom, MT7615 do not match");
    mtkbt_fw_name(nm,sizeof nm,0x7961,0,0); CHECK(!strcmp(nm,"mediatek/BT_RAM_CODE_MT7961_1_1_hdr.bin"),"name MT7961 v0");
    mtkbt_fw_name(nm,sizeof nm,0x7961,1,1); CHECK(!strcmp(nm,"mediatek/BT_RAM_CODE_MT7961_1a_2_hdr.bin"),"name MT7961 flavour 1 (MT7920) v1");
    mtkbt_fw_name(nm,sizeof nm,0x7922,0,1); CHECK(!strcmp(nm,"mediatek/BT_RAM_CODE_MT7922_1_1_hdr.bin"),"name MT7922 ignores flavour");
    mtkbt_fw_name(nm,sizeof nm,0x7925,0,0); CHECK(!strcmp(nm,"mediatek/mt7925/BT_RAM_CODE_MT7925_1_1_hdr.bin"),"name MT7925");
    mtkbt_fw_name(nm,sizeof nm,0x6639,1,0); CHECK(!strcmp(nm,"mediatek/mt7927/BT_RAM_CODE_MT6639_2_2_hdr.bin"),"name MT6639");
    CHECK(mtkbt_fw_name(nm,10,0x7922,0,0)==0,"name too long for buffer is refused");
    u32 sz[4]={600,0,250,1001}, md[4]={1,1,2,1}; u32 L=build(f,4,sz,md);
    int n=mtkbt_parse(f,L,0x7922,s,MTKBT_MAXSEC);
    CHECK(n==3&&s[0].size==600&&s[1].size==250&&s[2].size==1001&&s[1].idx==2,"parse: 4 sections, empty one skipped");
    CHECK(s[0].spec==f+96+12&&s[0].off==32+64+256,"parse: spec pointer and offset");
    n=mtkbt_parse(f,L,0x6639,s,MTKBT_MAXSEC); CHECK(n==2&&s[1].idx==3,"parse: MT6639 skips sections whose dl mode is not 1");
    CHECK(mtkbt_parse(f,L-1,0x7922,s,MTKBT_MAXSEC)<0,"parse: truncated last section rejected");
    CHECK(mtkbt_parse(f,95,0x7922,s,MTKBT_MAXSEC)<0,"parse: file shorter than header rejected");
    CHECK(mtkbt_parse(f,96+64*3,0x7922,s,MTKBT_MAXSEC)<0,"parse: section table past end rejected");
    { static u8 g[8192]; memcpy(g,f,8192); p32(g+44,0); CHECK(mtkbt_parse(g,L,0x7922,s,MTKBT_MAXSEC)<0,"parse: zero sections rejected");
      memcpy(g,f,8192); p32(g+44,0x40000000u); CHECK(mtkbt_parse(g,L,0x7922,s,MTKBT_MAXSEC)<0,"parse: huge section count rejected");
      memcpy(g,f,8192); p32(g+96+4,0xFFFFFF00u); CHECK(mtkbt_parse(g,L,0x7922,s,MTKBT_MAXSEC)<0,"parse: offset overflow rejected");
      memcpy(g,f,8192); p32(g+96+16,0xFFFFFFFFu); CHECK(mtkbt_parse(g,L,0x7922,s,MTKBT_MAXSEC)<0,"parse: size overflow rejected");
      CHECK(mtkbt_parse(f,L,0x7922,s,2)<0,"parse: more sections than room rejected"); }
    { u8 e[9]={0xE4,5,1,0x17,1,0,2}; CHECK(mtkbt_wmt_status(0x17,e,7)==MTKBT_ST_PATCH_UNDONE,"wmt: semaphore flag 2 = not downloaded");
      e[6]=1; CHECK(mtkbt_wmt_status(0x17,e,7)==MTKBT_ST_PATCH_DONE,"wmt: semaphore other = downloaded");
      e[3]=1; e[6]=2; CHECK(mtkbt_wmt_status(1,e,7)==MTKBT_ST_PATCH_DONE,"wmt: patch flag 2 = done");
      e[6]=1; CHECK(mtkbt_wmt_status(1,e,7)==MTKBT_ST_PATCH_PROGRESS,"wmt: patch flag 1 = in progress");
      e[6]=0; CHECK(mtkbt_wmt_status(1,e,7)==MTKBT_ST_PATCH_UNDONE,"wmt: patch flag 0 = go ahead");
      CHECK(mtkbt_wmt_status(6,e,7)==-2,"wmt: wrong op detected");
      e[3]=6; e[1]=7; e[7]=4; e[8]=4; CHECK(mtkbt_wmt_status(6,e,9)==MTKBT_ST_ON_DONE,"wmt: func ctrl 0x404 = on");
      e[8]=0x20; CHECK(mtkbt_wmt_status(6,e,9)==MTKBT_ST_ON_PROGRESS,"wmt: func ctrl 0x420 = in progress");
      e[1]=5; e[6]=0; CHECK(mtkbt_wmt_status(6,e,9)==MTKBT_ST_ON_DONE,"wmt: func ctrl without status word uses flag");
      CHECK(mtkbt_wmt_status(6,e,6)==-1,"wmt: short event rejected");
      e[0]=0x0E; CHECK(mtkbt_wmt_status(6,e,9)==-1,"wmt: non-WMT event rejected"); }
    reset_dev(0x7922); CHECK(mtkbt_attach(0x0BDA,0xC123)==0&&dev.nctl==0&&dev.ncmd==0&&nlog==0,"attach: Realtek device untouched (no USB traffic, no log)");
    reset_dev(0x7922); fwfile=f; fwlen=L; dev.busy_once=1;
    int r=mtkbt_attach(0x0E8D,0x0616);
    CHECK(r==1,"attach MT7922: firmware download succeeds");
    CHECK(!strcmp(lastpath,"/lib/firmware/mediatek/BT_RAM_CODE_MT7922_1_2_hdr.bin"),"attach: firmware path from chip id and version");
    CHECK(dev.gotn==600+250+1001&&!memcmp(dev.got,f+s[0].off,600),"device received section 0 bytes in order");
    n=mtkbt_parse(f,L,0x7922,s,MTKBT_MAXSEC);
    { u32 o=0; int ok=1; for(int i=0;i<n;i++){ if(memcmp(dev.got+o,f+s[i].off,s[i].size))ok=0; o+=s[i].size; } CHECK(ok,"device received every section byte for byte"); }
    { static const u8 want[]={1,2,3, 1, 1,2,2,2,3}; CHECK(dev.nfl==9&&!memcmp(dev.flags,want,9),"chunk flags start/continue/end (single chunk = start)"); }
    CHECK(dev.nspec==4&&!memcmp(dev.spec[1]+1,f+96+12,52)&&!memcmp(dev.spec[2]+1,f+96+128+12,52),"section query repeated after busy, spec bytes forwarded");
    CHECK(dev.eprst==1&&dev.eprst_v==0x00010001&&dev.funcon==1,"endpoint reset option written, function switched on");
    CHECK(dev.npoll>dev.ncmd,"event polled on control pipe, empty polls retried");
    reset_dev(0x7961); dev.done_sec=1; r=mtkbt_attach(0x0E8D,0x0608);
    CHECK(r==1&&dev.gotn==250+1001,"section already present is skipped");
    reset_dev(0x7922); fwfile=0; r=mtkbt_attach(0x0E8D,0x0616);
    CHECK(r==-1&&strstr(lastlog,"/lib/firmware/mediatek/BT_RAM_CODE_MT7922_1_2_hdr.bin")&&dev.ncmd==0,"missing firmware: clear message, nothing sent");
    reset_dev(0x7663); fwfile=f; r=mtkbt_attach(0x0E8D,0x7663);
    CHECK(r==0&&dev.ncmd==0,"unsupported chip id: left alone");
    reset_dev(0x7922); dev.mute=1; u64 t0=clk; r=mtkbt_attach(0x0E8D,0x0616);
    CHECK(r==-1&&clk-t0<3000000000ULL&&dev.ncmd==1,"silent chip: times out after one command (bounded wait)");
    reset_dev(0x7922); dev.ok=0; dev.mute=1; t0=clk; r=mtkbt_attach(0x0E8D,0x0616);
    CHECK(r==-1&&clk-t0<10000000ULL,"adapter gone: no waiting");
    reset_dev(0); fwfile=f; r=mtkbt_attach(0x13D3,0x3588);
    CHECK(r==1&&strstr(lastpath,"mt7927/BT_RAM_CODE_MT6639_2_2_hdr.bin")&&dev.gotn==600+1001,"chip id 0 on a known MT6639 board: MT6639 path and section filter");
    { static u8 g[8192]; memcpy(g,f,8192); p32(g+96+4,0xFFFF0000u); fwfile=g; reset_dev(0x7922); r=mtkbt_attach(0x0E8D,0x0616);
      CHECK(r==-1&&dev.ncmd==0,"damaged firmware rejected before anything is sent"); }
    printf("%d/%d checks passed\n%s\n",checks-fails,checks,fails?"MTKBT HOST TEST: FAILED":"MTKBT HOST TEST: ALL PASS");
    return fails?1:0;
}
