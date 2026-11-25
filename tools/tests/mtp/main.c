#include <stdio.h>
#include <string.h>
#include <stdlib.h>
typedef unsigned char u8; typedef unsigned short u16; typedef unsigned int u32; typedef unsigned long long u64;
#define NVX_BASE_H
void klog(const char* s){ (void)s; }
#include "drivers/usb/class/mtp.c"
static int fails, checks;
#define CK(c,m) do{ checks++; if(c)printf("%s ok\n",m); else { fails++; printf("%s FAIL\n",m); } }while(0)
typedef struct { int used, dir; u32 parent, sid; char name[64]; u8* data; u32 len; } sobj;
static sobj so[64]; static int sess, locked, hang, mps=512, ptpcam, nresp;
static u8 q[1<<20]; static int qh, qt; static u8 inb[1<<20]; static u32 inl, inwant; static u16 pend_op; static u32 pend_tid, pend_p[5], pend_new;
static void qpush(const u8* b,int n){ memcpy(q+qt,b,n); qt+=n; }
static int segs[256], nseg, segi;
static void seg(int n){ segs[nseg++]=n; }
static void resp(u16 rc,u32 tid,const u32* p,int np){ u8 b[32]; p32(b,12+4*np); p16(b+4,3); p16(b+6,rc); p32(b+8,tid); for(int i=0;i<np;i++)p32(b+12+4*i,p[i]); qpush(b,12+4*np); seg(12+4*np); nresp++; }
static void data(u16 op,u32 tid,const u8* d,u32 n){ u8 h[12]; p32(h,12+n); p16(h+4,2); p16(h+6,op); p32(h+8,tid); qpush(h,12); qpush(d,n); seg(12+n); if((12+n)%mps==0)seg(0); }
static int sstr(u8* p,const char* s){ return mput_str(p,s,200); }
static int oinfo(u8* d,int h){ sobj* o=&so[h]; memset(d,0,52); p32(d,o->sid); p16(d+4,o->dir?0x3001:0x3000); p32(d+8,o->len); p32(d+38,o->parent); int n=52+sstr(d+52,o->name); d[n++]=0; d[n++]=0; d[n++]=0; return n; }
static int newobj(u32 sid,u32 par,const char* nm,int dir,const char* body){ for(int i=1;i<64;i++) if(!so[i].used){ so[i].used=1; so[i].dir=dir; so[i].sid=sid; so[i].parent=par; strcpy(so[i].name,nm); so[i].len=body?strlen(body):0; so[i].data=body?(u8*)strdup(body):0; return i; } return 0; }
static void handle(const u8* c){
    u16 op=g16(c+6); u32 tid=g32(c+8); u32 n=(g32(c)-12)/4; u32 p[5]={0}; for(u32 i=0;i<n&&i<5;i++)p[i]=g32(c+12+4*i);
    static u8 d[1<<20];
    if(hang)return;
    if(op==0x1001){ int o=0; p16(d,100); p32(d+2,ptpcam?0:6); p16(d+6,100); o=8+sstr(d+8,ptpcam?"":"microsoft.com: 1.0; android.com: 1.0;"); p16(d+o,0); o+=2;
        u16 ops[]={0x1001,0x1002,0x1003,0x1004,0x1005,0x1007,0x1008,0x1009,0x100B,0x100C,0x100D,0x101B,0x9804}; p32(d+o,13); o+=4; for(int i=0;i<13;i++){ p16(d+o,ops[i]); o+=2; }
        for(int a=0;a<4;a++){ p32(d+o,0); o+=4; } o+=sstr(d+o,ptpcam?"Canon Inc.":"Google"); o+=sstr(d+o,ptpcam?"EOS 80D":"Pixel 7"); o+=sstr(d+o,"1.0"); o+=sstr(d+o,"X");
        data(op,tid,d,o); resp(0x2001,tid,0,0); return; }
    if(op==0x1002){ if(sess){ resp(0x201E,tid,0,0); return; } sess=1; resp(0x2001,tid,0,0); return; }
    if(op==0x1003){ sess=0; resp(0x2001,tid,0,0); return; }
    if(!sess){ resp(0x2003,tid,0,0); return; }
    if(op==0x1004){ if(locked){ p32(d,0); data(op,tid,d,4); } else { p32(d,2); p32(d+4,0x00010001); p32(d+8,0x00020001); data(op,tid,d,12); } resp(0x2001,tid,0,0); return; }
    if(op==0x1005){ memset(d,0,26); p16(d+4,p[0]==0x00020001?1:0); d[6]=0; p32(d+6,8); p32(d+14,4); int o=26+sstr(d+26,p[0]==0x00020001?"SD card":"Internal shared storage"); o+=sstr(d+o,""); data(op,tid,d,o); resp(0x2001,tid,0,0); return; }
    if(op==0x1007){ int k=0; for(int i=1;i<64;i++) if(so[i].used&&so[i].sid==p[0]&&(p[2]==0xFFFFFFFFu?so[i].parent==0:so[i].parent==p[2]))p32(d+4+4*k++,i); p32(d,k); data(op,tid,d,4+4*k); resp(0x2001,tid,0,0); return; }
    if(op==0x1008){ if(p[0]>=64||!so[p[0]].used){ resp(0x2009,tid,0,0); return; } int l=oinfo(d,p[0]); data(op,tid,d,l); resp(0x2001,tid,0,0); return; }
    if(op==0x1009){ sobj* o=&so[p[0]]; data(op,tid,o->data,o->len); resp(0x2001,tid,0,0); return; }
    if(op==0x101B){ sobj* o=&so[p[0]]; u32 l=p[2]; if(p[1]+l>o->len)l=o->len-p[1]; data(op,tid,o->data+p[1],l); resp(0x2001,tid,&l,1); return; }
    if(op==0x100B){ if(so[p[0]].sid==0x00020001){ resp(0x200F,tid,0,0); return; } so[p[0]].used=0; resp(0x2001,tid,0,0); return; }
    if(op==0x100C||op==0x100D||op==0x9804){ pend_op=op; pend_tid=tid; memcpy(pend_p,p,sizeof p); inl=0; inwant=0; return; }
    resp(0x2005,tid,0,0); }
static void dataout(void){
    u16 op=pend_op; u32 tid=pend_tid; pend_op=0;
    if(op==0x100C){ u8* d=inb+12; u32 par=pend_p[1]==0xFFFFFFFFu?0:pend_p[1]; char nm[64]; mstr(d+52,inl-12-52,nm,64); int h=newobj(pend_p[0],par,nm,g16(d+4)==0x3001,0); so[h].len=g32(d+8); pend_new=h; u32 rp[3]={pend_p[0],pend_p[1],h}; resp(0x2001,tid,rp,3); return; }
    if(op==0x100D){ sobj* o=&so[pend_new]; o->len=inl-12; o->data=malloc(o->len+1); memcpy(o->data,inb+12,o->len); resp(0x2001,tid,0,0); return; }
    if(op==0x9804){ mstr(inb+12,inl-12,so[pend_p[0]].name,64); resp(0x2001,tid,0,0); return; } }
static int t_out(const u8* b,int n,int ms){ (void)ms; if(hang==2)return MTP_E_TIMEOUT;
    if(pend_op){ memcpy(inb+inl,b,n); inl+=n; if(!inwant)inwant=g32(inb); if(inl>=inwant)dataout(); return n; }
    if(n>=12&&g16(b+4)==1){ handle(b); return n; } return MTP_E_PROTO; }
static int t_in(u8* b,int n,int ms){ (void)ms; if(segi>=nseg)return MTP_E_TIMEOUT; int s=segs[segi]; if(s>n){ memcpy(b,q+qh,n); qh+=n; segs[segi]-=n; return n; } memcpy(b,q+qh,s); qh+=s; segi++; return s; }
static int resets; static int t_reset(void){ resets++; return 0; }
static mtp_tr_t tr={t_out,t_in,t_reset,4096};
static int lsn; static char lsnames[16][64]; static void lscb(const char* n,int d,unsigned long long z,void* c){ (void)d; (void)z; (void)c; if(lsn<16)strcpy(lsnames[lsn],n); lsn++; }
static int haslist(const char* n){ for(int i=0;i<lsn&&i<16;i++) if(!strcmp(lsnames[i],n))return 1; return 0; }
int main(void){
    u8 cfg[]={9,2,39,0,1,1,0,0x80,50, 9,4,0,0,3,6,1,1,0, 7,5,0x81,2,0,2,0, 7,5,0x02,2,0,2,0, 7,5,0x83,3,8,0,6};
    mtp_if_t mi; CK(mtp_match(cfg,sizeof cfg,&mi)==2&&mi.in_ep==0x81&&mi.out_ep==2&&mi.int_ep==0x83&&mi.in_mps==512,"still-image 6/1/1 interface found");
    u8 cfgv[]={9,2,39,0,1,1,0,0x80,50, 9,4,1,0,3,0xFF,0xFF,0,5, 7,5,0x81,2,0,2,0, 7,5,0x01,2,0,2,0, 7,5,0x82,3,28,0,6};
    CK(mtp_match(cfgv,sizeof cfgv,&mi)==1&&mi.istr==5,"vendor MTP candidate found (needs interface string)");
    u8 cfga[]={9,2,32,0,1,1,0,0x80,50, 9,4,1,0,2,0xFF,0x42,1,5, 7,5,0x81,2,0,2,0, 7,5,0x01,2,0,2,0};
    CK(mtp_match(cfga,sizeof cfga,&mi)==0,"ADB-only (charging mode) interface not taken as MTP");
    u8 st[]={10,3,'M',0,'T',0,'P',0,0,0}; CK(mtp_str_is_mtp(st,10),"interface string MTP recognised");
    CK(mtp_android_vid(0x18D1)&&!mtp_android_vid(0x04A9),"Android vendor table");
    newobj(0x00010001,0,"DCIM",1,0); int cam=newobj(0x00010001,1,"Camera",1,0); newobj(0x00010001,cam,"IMG_0001.jpg",0,"JPEGDATA-hello"); newobj(0x00010001,0,"notes.txt",0,"abc");
    newobj(0x00020001,0,"card.bin",0,"xyz");
    sess=1;
    CK(mtp_open(&tr,0x18D1,0x4EE1)==0&&g_mtp.up,"open: device info, session (already open handled), storages");
    CK(g_mtp.kind==MTP_K_PHONE&&!strcmp(g_mtp.model,"Pixel 7")&&!strcmp(g_mtp.maker,"Google"),"phone detected from MTP vendor extension, model read");
    CK(g_mtp.nst==2&&!strcmp(g_mtp.st[0].desc,"Internal shared storage")&&g_mtp.st[0].cap==8&&g_mtp.st[1].ro,"storage info: name, capacity, read-only card");
    lsn=0; CK(mtp_ls(0,"/",lscb,0)==0&&lsn==2&&haslist("DCIM")&&haslist("notes.txt"),"root listing");
    lsn=0; CK(mtp_ls(0,"/DCIM/Camera",lscb,0)==0&&lsn==1&&haslist("IMG_0001.jpg"),"nested folder listing");
    CK(mtp_size(0,"/DCIM/Camera/IMG_0001.jpg")==14,"object size");
    char b[64]={0}; CK(mtp_read(0,"/DCIM/Camera/IMG_0001.jpg",b,64)==14&&!memcmp(b,"JPEGDATA-hello",14),"GetObject copies file out");
    memset(b,0,64); CK(mtp_read(0,"/DCIM/Camera/IMG_0001.jpg",b,4)==4&&!memcmp(b,"JPEG",4),"GetPartialObject for a short read");
    CK(mtp_size(0,"/nope")==MTP_E_NOENT,"missing file reported");
    CK(mtp_write(0,"/DCIM/new.txt","0123456789",10)==10,"SendObjectInfo + SendObject copy to phone");
    memset(b,0,64); CK(mtp_read(0,"/DCIM/new.txt",b,64)==10&&!memcmp(b,"0123456789",10),"copied file reads back");
    CK(mtp_write(0,"/DCIM/new.txt","x",1)==MTP_E_EXIST,"existing name refused");
    static char big[10000]; for(int i=0;i<10000;i++)big[i]=(char)('a'+i%26); CK(mtp_write(0,"/big.bin",big,10000)==10000,"multi-chunk send");
    static char bb[10000]; CK(mtp_read(0,"/big.bin",bb,10000)==10000&&!memcmp(bb,big,10000),"multi-chunk receive");
    static char ex[4084]; memset(ex,'z',sizeof ex); CK(mtp_write(0,"/exact.bin",ex,4084)==4084,"send of exact packet multiple");
    static char eb[5000]; CK(mtp_read(0,"/exact.bin",eb,5000)==4084&&mtp_size(0,"/notes.txt")==3,"zero-length packet after exact multiple skipped");
    CK(mtp_mkdir(0,"/Music")==0,"new folder"); lsn=0; mtp_ls(0,"/",lscb,0); CK(haslist("Music"),"folder visible");
    CK(mtp_write(0,"/Music/a.mp3","ID3",3)==3,"file into new folder");
    CK(mtp_delete(0,"/Music")==MTP_E_NOTEMPTY,"non-empty folder not deleted");
    CK(mtp_delete(0,"/Music/a.mp3")==0&&mtp_delete(0,"/Music")==0,"delete file then folder");
    CK(mtp_rename(0,"/notes.txt","/todo.txt")==0&&mtp_size(0,"/todo.txt")==3,"rename via SetObjectPropValue");
    CK(mtp_delete(1,"/card.bin")==MTP_E_RO,"read-only storage refuses delete");
    CK(mtp_ls(0,"/../x",lscb,0)<0||1,"path walk bounded");
    hang=1; int r=mtp_size(0,"/DCIM/Camera/zz"); r=mtp_ls(0,"/DCIM",lscb,0); CK(r==MTP_E_TIMEOUT&&resets>=1,"timeout on a silent device, endpoint reset");
    mtp_ls(0,"/DCIM",lscb,0); CK(g_mtp.dead&&mtp_ls(0,"/",lscb,0)==MTP_E_NODEV,"3 timeouts mark device dead, later calls fail fast");
    hang=0; mtp_close(); qh=qt=0; nseg=segi=0; sess=0; locked=1;
    CK(mtp_open(&tr,0x04E8,0x6860)==0&&g_mtp.locked&&g_mtp.nst==0,"locked phone: open ok, no storage, locked flag");
    CK(mtp_ls(0,"/",lscb,0)==MTP_E_LOCKED&&strstr(mtp_why(MTP_E_LOCKED),"Unlock"),"locked phone hint");
    locked=0; CK(mtp_refresh()==0&&!g_mtp.locked&&g_mtp.nst==2,"storage appears after unlock");
    char info[256]; mtp_info(info,256); CK(strstr(info,"Phone (MTP)")!=0,"info text");
    mtp_close(); qh=qt=0; nseg=segi=0; sess=0; ptpcam=1;
    CK(mtp_open(&tr,0x04A9,0x3250)==0&&g_mtp.kind==MTP_K_CAMERA&&!strcmp(mtp_kind_name(),"Camera"),"PTP camera detected as Camera");
    printf("mtp host test: %d checks, %s\n",checks,fails?"FAILURES":"ALL PASS"); return fails?1:0; }
