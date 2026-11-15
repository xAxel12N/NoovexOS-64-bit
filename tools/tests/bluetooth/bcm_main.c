#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdint.h>
typedef uint8_t u8; typedef uint16_t u16; typedef uint32_t u32; typedef uint64_t u64;
#define BCMBT_HOST
static void kfree(void* p){ free(p); }
static void klog(const char* s){ printf("    log: %s\n",s); }
static void klog2(const char* a,const char* b){ printf("    log: %s%s\n",a,b); }
static int slept;
static void bcmbt_sleep_ms(int ms){ slept+=ms; }
static u8* file_d; static int file_n; static char file_nm[64]; static char tried[8][64]; static int ntried;
static u8* bcmbt_read(const char* name,int* len);
#include "drivers/bluetooth/bcmbt_fw.c"
static u8* bcmbt_read(const char* name,int* len){
    if(ntried<8)snprintf(tried[ntried++],64,"%s",name);
    *len=0; if(!file_d||strcmp(name,file_nm))return 0;
    u8* b=malloc((size_t)file_n); memcpy(b,file_d,(size_t)file_n); *len=file_n; return b;
}
static int fails, checks;
#define CHECK(c,msg) do{ checks++; if(c)printf("  %-64s ok\n",msg); else { fails++; printf("  %-64s FAIL (line %d)\n",msg,__LINE__); } }while(0)
static u16 log_op[512]; static int nlog, sim_fail_op, sim_launched, sim_dead, sim_wr;
static u16 sim_sv=0x220e, sim_rev=0x0410;
static int sim_cmd(u16 op,const u8* p,int n,u8* rp,int rcap){
    (void)p; if(nlog<512)log_op[nlog++]=op;
    if(sim_dead)return -1;
    if(op==sim_fail_op){ rp[0]=0x12; return 1; }
    if(op==0x1001){ u8 r[9]={0,6,(u8)sim_rev,(u8)(sim_rev>>8),6,0x0F,0,(u8)sim_sv,(u8)(sim_sv>>8)}; if(sim_launched){ r[2]=0x55; r[3]=0x11; } if(rcap<9)return -1; memcpy(rp,r,9); return 9; }
    if(op==0xFC5A){ u8 r[5]={0,0x5c,0x0a,0xe8,0x21}; memcpy(rp,r,5); return 5; }
    if(op==0x0C14){ if(rcap<249)return -1; memset(rp,0,249); memcpy(rp+1,"BCM20702A",9); return 249; }
    if(op==0xFC4C){ if(n<4)return -1; sim_wr++; }
    if(op==0xFC4E)sim_launched=1;
    rp[0]=0; return 1;
}
static int mkrec(u8* b,u16 op,int pl,u8 fill){ b[0]=(u8)op; b[1]=(u8)(op>>8); b[2]=(u8)pl; memset(b+3,fill,(size_t)pl); return 3+pl; }
static int mkhcd(u8* b,int nw){ int o=0; for(int i=0;i<nw;i++)o+=mkrec(b+o,0xFC4C,i==nw-1?20:251,(u8)i); o+=mkrec(b+o,0xFC4E,4,0xFF); return o; }
static void reset_sim(void){ nlog=0; sim_fail_op=0; sim_launched=0; sim_dead=0; sim_wr=0; ntried=0; slept=0; }
int main(void){
    static u8 h[8192]; int nr=0, nl=0, n;
    printf("Broadcom .hcd parser and patchram sequence\n");
    n=mkhcd(h,10);
    CHECK(bcmbt_hcd_check(h,n,&nr,&nl)==0&&nr==11&&nl==1,"valid .hcd: 10 Write RAM + 1 Launch RAM records");
    CHECK(bcmbt_hcd_check(h,n-1,0,0)<0,"truncated last record rejected");
    CHECK(bcmbt_hcd_check(h,n+2,0,0)<0,"trailing partial header rejected");
    CHECK(bcmbt_hcd_check(h,0,0,0)<0&&bcmbt_hcd_check(0,10,0,0)<0,"empty / null input rejected");
    { u8 b[8]; int m=mkrec(b,0x0C03,0,0); CHECK(bcmbt_hcd_check(b,m,0,0)<0,"non-vendor opcode (HCI Reset) rejected"); }
    { u8 b[300]; int m=mkrec(b,0xFC4C,255,1); CHECK(bcmbt_hcd_check(b,m,&nr,&nl)==0&&nr==1&&nl==0,"255-byte record accepted, no launch counted"); }
    { u8 junk[64]; for(int i=0;i<64;i++)junk[i]=(u8)(i*37+11); CHECK(bcmbt_hcd_check(junk,64,0,0)<0,"random bytes rejected"); }
    CHECK(!strcmp(bcmbt_hw_name(0x220e),"BCM20702A1")&&!strcmp(bcmbt_hw_name(0x2118),"BCM20702A0")&&!bcmbt_hw_name(0x8821),"subversion table lookup");
    { char nm[3][48]; int k=bcmbt_fw_names("BCM20702A1",0x0a5c,0x21e8,nm);
      CHECK(k==3&&!strcmp(nm[0],"BCM20702A1-0a5c-21e8.hcd")&&!strcmp(nm[1],"BCM-0a5c-21e8.hcd")&&!strcmp(nm[2],"BCM20702A1.hcd"),"firmware names for a known chip");
      k=bcmbt_fw_names(0,0x0b05,0x17cb,nm); CHECK(k==1&&!strcmp(nm[0],"BCM-0b05-17cb.hcd"),"firmware name for an unknown chip"); }
    CHECK(bcmbt_match(0x0a5c,0)&&bcmbt_match(0x0b05,0x000F)&&!bcmbt_match(0x0bda,0x005D)&&!bcmbt_match(0x8087,0x0002)&&!bcmbt_match(0x0e8d,0x0046),"inert for Realtek, Intel, MediaTek");
    CHECK(bcmbt_usb_vendor(0x0a5c)&&bcmbt_usb_vendor(0x0b05)&&!bcmbt_usb_vendor(0x0bda)&&!bcmbt_usb_vendor(0x8087),"vendor-specific interface claim list excludes Realtek / Intel");
    char msg[96];
    reset_sim(); file_d=0;
    int r=bcmbt_setup(sim_cmd,0x0a5c,0x21e8,msg,(int)sizeof msg);
    printf("    msg: %s\n",msg);
    CHECK(r==0&&strstr(msg,"no .hcd file, ROM firmware in use")&&!strcmp(tried[0],"BCM20702A1-0a5c-21e8.hcd"),"no firmware file: ROM fallback with the file name");
    CHECK(ntried==3,"all three names tried");
    { int dl=0; for(int i=0;i<nlog;i++) if(log_op[i]==0xFC2E||log_op[i]==0xFC4C)dl=1; CHECK(!dl,"nothing downloaded without a file"); }
    reset_sim(); n=mkhcd(h,10); file_d=h; file_n=n; strcpy(file_nm,"BCM-0a5c-21e8.hcd");
    r=bcmbt_setup(sim_cmd,0x0a5c,0x21e8,msg,(int)sizeof msg);
    printf("    msg: %s\n",msg);
    CHECK(r==1&&strstr(msg,"loaded BCM-0a5c-21e8.hcd"),"firmware found under the generic name and loaded");
    { static const u16 want_head[]={0x0C03,0x1001,0xFC5A,0x0C14,0xFC2E}; int ok=nlog>=5; for(int i=0;i<5&&ok;i++)ok=log_op[i]==want_head[i];
      CHECK(ok,"order: Reset, Read Version, USB Product, Local Name, Download Minidriver");
      int ok2=1; for(int i=0;i<10;i++)ok2&=log_op[5+i]==0xFC4C; ok2&=log_op[15]==0xFC4E&&log_op[16]==0x0C03&&log_op[17]==0x1001&&log_op[18]==0x0C14&&nlog==19;
      CHECK(ok2&&sim_wr==10,"then 10 Write RAM, Launch RAM, Reset, Read Version, Local Name"); }
    CHECK(slept==100+50+250+100,"delays 100 / 50 / 250 / 100 ms as in btbcm");
    CHECK(strstr(msg,"BCM20702A1 (001.002.014) build 0341")!=0,"version shown after patch (build from new revision)");
    reset_sim(); sim_fail_op=0xFC2E;
    r=bcmbt_setup(sim_cmd,0x0a5c,0x21e8,msg,(int)sizeof msg); printf("    msg: %s\n",msg);
    CHECK(r==0&&strstr(msg,"Download Minidriver refused"),"minidriver refused: ROM fallback");
    reset_sim(); sim_fail_op=0xFC4C;
    r=bcmbt_setup(sim_cmd,0x0a5c,0x21e8,msg,(int)sizeof msg); printf("    msg: %s\n",msg);
    { int launched=0; for(int i=0;i<nlog;i++) if(log_op[i]==0xFC4E)launched=1;
      CHECK(r==0&&strstr(msg,"patch failed at record 0")&&!launched&&log_op[nlog-3]==0x0C03,"write error stops before Launch RAM, controller reset"); }
    reset_sim(); sim_fail_op=0xFC4E;
    r=bcmbt_setup(sim_cmd,0x0a5c,0x21e8,msg,(int)sizeof msg);
    CHECK(r==1,"Launch RAM without Command Complete is tolerated");
    reset_sim(); h[5]=0xFF; h[2]=200; file_n=n;
    r=bcmbt_setup(sim_cmd,0x0a5c,0x21e8,msg,(int)sizeof msg); printf("    msg: %s\n",msg);
    { int dl=0; for(int i=0;i<nlog;i++) if(log_op[i]==0xFC2E)dl=1; CHECK(r==0&&strstr(msg,"invalid, ROM")&&!dl,"corrupt file rejected before anything is sent"); }
    n=mkhcd(h,10);
    reset_sim(); sim_dead=1;
    r=bcmbt_setup(sim_cmd,0x0a5c,0x21e8,msg,(int)sizeof msg);
    CHECK(r==0&&nlog==1&&strstr(msg,"HCI Reset failed"),"dead controller: one command, then give up");
    reset_sim(); sim_sv=0x9999;
    r=bcmbt_setup(sim_cmd,0x0b05,0x17cb,msg,(int)sizeof msg); printf("    msg: %s\n",msg);
    CHECK(r==1&&!strcmp(tried[0],"BCM-0a5c-21e8.hcd"),"unknown subversion: generic name from controller USB product id");
    { char small[20]; reset_sim(); file_d=0; r=bcmbt_setup(sim_cmd,0x0a5c,0x21e8,small,(int)sizeof small); CHECK(r==0&&strlen(small)==19,"message bounded by caller buffer"); }
    printf("%d/%d checks, %s\n",checks-fails,checks,fails?"FAILED":"ALL PASS");
    return fails?1:0;
}
