#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdint.h>
#define NVX_BASE_H
#define NVX_CORE_KLOG_H
#define NVX_MM_HEAP_H
#define NVX_ARCH_X86_64_HRTIMER_H
#define NVX_FS_VFS_H
#define NVX_DRIVERS_BLUETOOTH_BT_H
typedef uint8_t u8; typedef uint16_t u16; typedef uint32_t u32; typedef uint64_t u64;
#define O_RDONLY 0
static u64 clk; static u64 hr_ns(void){ return clk+=1000; } static void hr_delay_us(u32 us){ clk+=(u64)us*1000; }
static char lastlog[200]; static void klog(const char* s){ snprintf(lastlog,sizeof lastlog,"%s",s); }
static void* kmalloc(unsigned long n){ return malloc(n); } static void* krealloc(void* p,unsigned long n){ return realloc(p,n); } static void kfree(void* p){ free(p); }
volatile u32 g_ml_beat; void ui_pump(void){}
static struct { const char* name; u8* d; int n; } files[4]; static int nfiles;
static struct { int f, o; } fds[4];
static char lastopen[8][96]; static int nopen;
static int vfs_open(const char* p,int fl){ (void)fl; if(nopen<8)snprintf(lastopen[nopen++],96,"%s",p); for(int i=0;i<nfiles;i++) if(!strcmp(files[i].name,p)){ for(int k=0;k<4;k++) if(!fds[k].f){ fds[k].f=i+1; fds[k].o=0; return k; } } return -1; }
static int vfs_read(int fd,char* b,int n){ int i=fds[fd].f-1; int r=files[i].n-fds[fd].o; if(r>n)r=n; memcpy(b,files[i].d+fds[fd].o,(size_t)r); fds[fd].o+=r; return r; }
static void vfs_close(int fd){ fds[fd].f=0; }
static u8 evq[64][64]; static int evl[64], evh, evt;
static void push(const u8* e,int n){ memcpy(evq[evt%64],e,(size_t)n); evl[evt%64]=n; evt++; }
static void cc(u16 op,const u8* p,int n){ u8 e[64]={0x0E,(u8)(3+n),1,(u8)op,(u8)(op>>8)}; memcpy(e+5,p,(size_t)n); push(e,5+n); }
static void vend(u8 code){ u8 e[3]={0xFF,1,code}; push(e,3); }
static struct { int variant, oper, mute6, ops[64], nops; u8 rst[8], ddc[64], evm[8], mfg[8][2]; int nddc, nmfg; u8 ftype[64]; int foff[64], flen[64], nfr; u8 fw[8192]; int fwn, expect; } D;
static void ver_reply(void){ u8 v[10]={0,0x37,(u8)D.variant,0x10,(u8)(D.oper?0x23:0x06),0x01,0x80,0x01,0x2d,(u8)(D.oper&&D.variant==8?1:0)}; if(D.variant==8)v[4]=0x01; cc(0xFC05,v,10); }
static int btusb_ok(void){ return 1; }
static void btusb_ids(u16* v,u16* p){ *v=0x8087; *p=(u16)(D.variant==8?0x0a2a:0x0aaa); }
static int btusb_evt(u8* o,int cap){ if(evh==evt)return 0; int n=evl[evh%64]; if(n>cap)n=cap; memcpy(o,evq[evh%64],(size_t)n); evh++; return n; }
static int btusb_bulk_in(u8* o,int cap,int ms){ (void)o; (void)cap; (void)ms; return 0; }
static int btusb_cmd(const u8* pk,int n){
    u16 op=(u16)(pk[0]|pk[1]<<8); const u8* p=pk+3; int pl=pk[2]; if(pl!=n-3)return -1; if(D.nops<64)D.ops[D.nops++]=op;
    if(op==0xFC05){ ver_reply(); return 0; }
    if(op==0xFC0D){ u8 b[23]={0}; b[4]=16; cc(op,b,23); return 0; }
    if(op==0xFC01){ memcpy(D.rst,p,8); D.oper=1; vend(0x02); return 0; }
    if(op==0xFC8B){ memcpy(D.ddc+D.nddc,p,(size_t)pl); D.nddc+=pl; u8 s=0; cc(op,&s,1); return 0; }
    if(op==0xFC52){ memcpy(D.evm,p,8); u8 s=0; cc(op,&s,1); return 0; }
    if(op==0xFC11){ if(D.nmfg<8){ D.mfg[D.nmfg][0]=p[0]; D.mfg[D.nmfg][1]=p[1]; } D.nmfg++; u8 s=0; cc(op,&s,1); if(p[0]==0&&p[1]==2)D.oper=1; return 0; }
    { u8 s=0; cc(op,&s,1); } return 0; }
static int btusb_bulk_out(const u8* pk,int n,int ms){ (void)ms;
    if(n<4||pk[0]!=0x09||pk[1]!=0xFC||pk[2]!=n-3)return -1;
    if(D.nfr<64){ D.ftype[D.nfr]=pk[3]; D.foff[D.nfr]=D.fwn; D.flen[D.nfr]=n-4; } D.nfr++;
    memcpy(D.fw+D.fwn,pk+4,(size_t)(n-4)); D.fwn+=n-4;
    { u8 s=0; cc(0xFC09,&s,1); }
    if(D.fwn==D.expect&&!D.mute6)vend(0x06);
    return n; }
#include "drivers/bluetooth/firmware/intelbt_fw.c"
static int fails, checks;
#define CHECK(c,m) do{ checks++; if(c)printf("  %-70s ok\n",m); else { fails++; printf("  %-70s FAIL (line %d)\n",m,__LINE__); } }while(0)
static u8 sfi[2048]; static int sfin; static u8 ddc[16]={3,1,0,0xAA, 5,2,0,1,2,3}; static u8 bseq[64]; static int bseqn;
static void reset(int variant){ memset(&D,0,sizeof D); D.variant=variant; evh=evt=0; nopen=0; nfiles=0; }
static int opidx(u16 op,int from){ for(int i=from;i<D.nops;i++) if(D.ops[i]==op)return i; return -1; }
int main(void){
    for(int i=0;i<644;i++){ sfi[i]=(u8)(i*7); }
    sfi[8]=0; sfi[9]=0; sfi[10]=1; sfi[11]=0; sfin=644;
    for(int r=0;r<5;r++){ sfi[sfin]=0x8E; sfi[sfin+1]=0xFC; sfi[sfin+2]=0x3D; for(int k=0;k<0x3D;k++)sfi[sfin+3+k]=(u8)(r+k); sfin+=64; }
    { u8 b[7]={0x0E,0xFC,4,0x45,0x23,0x01,0x00}; memcpy(sfi+sfin,b,7); sfin+=7; u8 c[5]={0x10,0xFC,2,0x55,0x66}; memcpy(sfi+sfin,c,5); sfin+=5; }
    char m[160]; int r;
    reset(0x0b); files[0].name="/lib/firmware/intel/ibt-11-16.sfi"; files[0].d=sfi; files[0].n=sfin; files[1].name="/lib/firmware/intel/ibt-11-16.ddc"; files[1].d=ddc; files[1].n=10; nfiles=2; D.expect=644-4+(sfin-644);
    r=intelbt_setup(m,sizeof m); printf("    %s\n",m);
    CHECK(r==1&&strstr(m,"firmware loaded: ibt-11-16.sfi (with DDC)"),"8260 bootloader: setup succeeds, DDC applied");
    { int a=opidx(0xFC05,0), b=opidx(0xFC0D,a), c=opidx(0xFC01,b), d=opidx(0xFC8B,c), e=opidx(0xFC52,d); CHECK(a==0&&b>a&&c>b&&d>c&&e>d,"order: Read Version(0xFF), Read Boot Params, [fragments], Intel Reset, Write DDC, Set Event Mask"); }
    CHECK(D.nfr>=5&&D.ftype[0]==0&&D.flen[0]==128&&D.ftype[1]==3&&D.flen[1]==128&&D.ftype[2]==3&&D.ftype[3]==2&&D.ftype[4]==2&&D.flen[4]==128,"RSA header: CSS 0x80, key 2x0x80, signature 2x0x80 (secure send types 0/3/3/2/2)");
    CHECK(!memcmp(D.fw,sfi,384)&&!memcmp(D.fw+384,sfi+388,256),"header bytes as in iwmbtfw: 4-byte exponent at 384 skipped");
    { int ok=1; for(int i=5;i<D.nfr;i++){ if(D.ftype[i]!=1||D.flen[i]>252||(i<D.nfr-1&&D.flen[i]!=252&&D.flen[i]%4))ok=0; } CHECK(ok&&!memcmp(D.fw+640,sfi+644,(size_t)(sfin-644))&&D.fwn==D.expect,"command buffer: type 1 fragments, 0xFC bytes or 4-aligned, byte-exact from 644"); }
    { static const u8 want[8]={0,0,0,1,0x45,0x23,0x01,0x00}; CHECK(!memcmp(D.rst,want,8),"Intel Reset 0xFC01 params 00 00 00 01 + boot address 0x00012345 from the 0xFC0E record"); }
    CHECK(D.nddc==10&&!memcmp(D.ddc,ddc,10),"DDC: each record sent whole (length byte + payload) with 0xFC8B");
    { static const u8 want[8]={0x87,0x0c,0,0,0,0,0,0}; CHECK(!memcmp(D.evm,want,8),"event mask 0xFC52 87 0c 00 00 00 00 00 00"); }
    reset(0x0b); D.oper=1; r=intelbt_setup(m,sizeof m); CHECK(r==2&&D.nfr==0&&opidx(0xFC01,0)<0,"operational firmware (variant 0x23) left alone");
    reset(0x0b); files[0].name="/lib/firmware/intel/ibt-11-16.sfi"; files[0].d=sfi; files[0].n=sfin; nfiles=1; D.expect=644-4+(sfin-644); D.mute6=1;
    r=intelbt_setup(m,sizeof m); CHECK(r==-1&&opidx(0xFC01,0)<0&&strstr(m,"download-complete"),"no 0x06 event: bounded wait, no Intel Reset sent");
    reset(0x0b); r=intelbt_setup(m,sizeof m); CHECK(r==-1&&D.nfr==0&&strstr(m,"missing")&&!strcmp(lastopen[0],"/lib/firmware/intel/ibt-11-16.sfi"),"missing .sfi: nothing sent");
    { u8 b[]={0x01,0x8E,0xFC,0x02,0xAA,0xBB, 0x02,0x0E,0x04,0x01,0x8E,0xFC,0x00, 0x01,0x22,0xFC,0x01,0x05, 0x02,0x0E,0x04,0x01,0x22,0xFC,0x00}; memcpy(bseq,b,sizeof b); bseqn=(int)sizeof b; }
    reset(0x08); files[0].name="/lib/firmware/intel/ibt-hw-37.8.bseq"; files[0].d=bseq; files[0].n=bseqn; nfiles=1;
    r=intelbt_setup(m,sizeof m); printf("    %s\n",m);
    CHECK(r==1&&D.ops[0]==0x0C03&&D.ops[1]==0xFC05,"7260: HCI Reset, then Read Version");
    CHECK(nopen>=2&&!strcmp(lastopen[0],"/lib/firmware/intel/ibt-hw-37.8.10-fw-1.1.80.1.2d.bseq")&&!strcmp(lastopen[1],"/lib/firmware/intel/ibt-hw-37.8.bseq"),"7260: specific .bseq tried, then ibt-hw-37.8.bseq fallback");
    CHECK(D.nmfg==4&&D.mfg[0][0]==1&&D.mfg[0][1]==0&&D.mfg[1][0]==0&&D.mfg[1][1]==2&&D.mfg[2][0]==1&&D.mfg[3][0]==0&&D.mfg[3][1]==0,"7260: mfg on, patch, mfg off with warm reset (0xFC8E present), mfg on/event mask/off");
    { int a=opidx(0xFC8E,0), b=opidx(0xFC22,a); CHECK(a>0&&b>a&&opidx(0xFC52,b)>b,"7260: patch commands sent in file order, each checked against its recorded event"); }
    bseq[11]=0xFD; reset(0x08); files[0].name="/lib/firmware/intel/ibt-hw-37.8.bseq"; files[0].d=bseq; files[0].n=bseqn; nfiles=1;
    r=intelbt_setup(m,sizeof m); CHECK(r==-1&&D.nmfg==2&&D.mfg[1][0]==0&&D.mfg[1][1]==1,"7260: event mismatch -> mfg off with cold reset, failure reported");
    printf("%d of %d checks passed\n",checks-fails,checks);
    if(!fails)printf("INTEL BT FLOW: ALL PASS\n");
    return fails!=0; }
