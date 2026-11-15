#include "posix.h"

long __sys(long n,long a,long b,long c,long d,long e);
typedef struct { u64 sectors; int kind, cur, ok, pad; char name[48]; } idisk;
typedef struct { int ok, gpt, ratio, nparts; u64 sectors; int ptype[4], pok[4]; u64 pst[4], psz[4]; char name[48]; char tname[4][16]; } idinfo;
typedef struct { int mode, part, fs, copy; char label[16]; } iparams;
typedef struct { int stage, prog, done, failed; char e1[120]; char e2[120]; char stagename[48]; } iprog;

enum { P_WELCOME, P_DISK, P_WHERE, P_FS, P_CONFIRM, P_RUN, P_DONE, P_ERR, P_PREP };
#define BLUE 0x4882C8
#define BOX 0xF8F9FB
#define INK 0x141822
#define GREY 0x6E7687
#define RED 0xD23C46
#define REDH 0xE6505A
#define OK 0x3C965A
#define WARN 0xC88C28

static int W, H, LOCKED, SRC, CLONE; static long CLONE_MB;
static int page=P_WELCOME, mxp, myp;
static int nl, lsel, lx, ly[8], lw;
static int nb, bfoc, bx[3], by, bw[3]; static const char* bt[3];
static idisk disks[8]; static int ndisks;
static idinfo di; static iparams pr; static iprog pg;
static int where_part[5], nwhere;
static const char* STG[8]={"Checking the kernel image","Preparing the boot partition","Writing the partition table","Installing the boot loader","Writing system files","Creating the data partition","Copying your files","Verifying the installation"};
static const char* FSN[4]={"NoovexFS","FAT32","NTFS","FAT16"};
static const char* FSD[4]={"native, recommended","readable almost everywhere","also readable by Windows","classic, partitions up to 2 GB"};
static const int FSV[4]={0,2,1,3};

static void rect(int x,int y,int w,int h,int c){ if(w>0&&h>0)__sys(42,x,y,w,h,c); }
static void text(int x,int y,const char* s,int c){ __sys(206,(long)(((u64)(unsigned)y<<32)|(unsigned)x),(long)(((u64)1<<32)|(unsigned)c),(long)s,0,0); }
static void present(void){ __sys(207,0,0,0,0,0); }
static void logk(const char* s){ __sys(210,(long)s,0,0,0,0); }
static int slen(const char* s){ int n=0; while(s[n])n++; return n; }
static void size_str(char* o,int cap,u64 secs){ unsigned long long mb=(unsigned long long)secs*512/1048576ULL; if(mb>=1024)snprintf(o,cap,"%llu.%llu GB",mb/1024,(mb%1024)*10/1024); else snprintf(o,cap,"%llu MB",mb); }

static void box(int* ox,int* oy,int* ow,int bwid,int bh,const char* title){
    int x=(W-bwid)/2, y=(H-bh)/2;
    rect(0,0,W,H,BLUE);
    rect(x+10,y+10,bwid,bh,0x000000); rect(x,y,bwid,bh,BOX);
    rect(x+3,y+3,bwid-6,2,INK); rect(x+3,y+bh-5,bwid-6,2,INK); rect(x+3,y+3,2,bh-6,INK); rect(x+bwid-5,y+3,2,bh-6,INK);
    int tw=slen(title)*8, full=tw+32, fx=x+(bwid-full)/2;
    for(int gx=x+10;gx<fx-4;gx+=8)text(gx,y+6,"-",INK);
    for(int gx=fx+full+4;gx<x+bwid-10;gx+=8)text(gx,y+6,"-",INK);
    text(fx,y+6,"[",INK); text(fx+16,y+6,title,RED); text(fx+16+tw,y+6,"]",INK);
    *ox=x+18; *oy=y+30; *ow=bwid-36; }

static void item(int k,int x,int y,int w,const char* s){
    int on=(k==lsel); rect(x,y-2,w,20,on?INK:0xEEF0F4);
    char b[160]; snprintf(b,160,"%s %s",on?"(*)":"( )",s); text(x+8,y+2,b,on?0xFFFFFF:INK);
    ly[k]=y-2; lx=x; lw=w; }

static void buttons(int cx,int y){
    int tot=0; for(int i=0;i<nb;i++){ bw[i]=slen(bt[i])*8+32; tot+=bw[i]; } tot+=(nb-1)*90;
    int x=cx-tot/2; by=y;
    for(int i=0;i<nb;i++){ int foc=(i==bfoc), hov=mxp>=x&&mxp<x+bw[i]&&myp>=y&&myp<y+20;
        if(foc)rect(x,y,bw[i],20,hov?REDH:RED);
        int c=foc?0xFFFFFF:(hov?0xFF7882:RED);
        text(x+4,y+3,"<",c); text(x+16,y+3,bt[i],c); text(x+16+slen(bt[i])*8+4,y+3,">",c);
        bx[i]=x; x+=bw[i]+90; } }

static void draw(void){
    char b[200]; int ox,oy,ow; nl=0; nb=0;
    if(CLONE&&page==P_WELCOME){
        box(&ox,&oy,&ow,640,214,"Take NoovexOS With You");
        text(ox,oy,"This copies the whole system - apps, settings, users and",INK); oy+=18;
        text(ox,oy,"files - to a USB stick that starts on other PCs.",INK); oy+=26;
        text(ox,oy,"Everything on the USB stick will be erased.",RED); oy+=18;
        text(ox,oy,"This computer restarts when the copy is finished.",GREY); oy+=18;
        text(ox,oy,"Ctrl+Alt+Esc closes this program at any time.",GREY); oy+=30;
        bt[0]="Continue"; bt[1]="Cancel"; nb=2; buttons(ox+ow/2,oy); }
    else if(page==P_PREP){
        box(&ox,&oy,&ow,560,120,"Take NoovexOS With You");
        text(ox,oy,"Reading every file into memory before the copy...",INK); oy+=18;
        text(ox,oy,"This can take a minute.",GREY); }
    else if(page==P_WELCOME){
        box(&ox,&oy,&ow,620,LOCKED?196:176,"NoovexOS Setup");
        text(ox,oy,"Welcome. This program installs NoovexOS 11.10 on a disk",INK); oy+=18;
        text(ox,oy,"in this computer.",INK); oy+=26;
        text(ox,oy,SRC==1?"Installer: INSTALL.NVX on the USB stick (signature OK)":"Installer: built into the Linux files (signature OK)",OK); oy+=18;
        if(LOCKED){ text(ox,oy,"Signed boot is on: checked with the kernel's key.",GREY); oy+=18; }
        text(ox,oy,"Ctrl+Alt+Esc closes the installer at any time.",GREY); oy+=30;
        bt[0]="Continue"; bt[1]="Cancel"; nb=2; buttons(ox+ow/2,oy); }
    else if(page==P_DISK){
        int n=ndisks<1?1:ndisks; box(&ox,&oy,&ow,660,150+(n-1)*24,"Select Target Disk");
        if(CLONE){ snprintf(b,200,"Choose the USB stick (needs room for about %ld MB):",CLONE_MB+CLONE_MB/6+80); text(ox,oy,b,INK); oy+=18; text(ox,oy,"Only USB sticks can be chosen. Plug one in, then Back and Continue.",GREY); oy+=28; }
        else { text(ox,oy,"Choose the disk to install NoovexOS to (click it or use Up/Down):",INK); oy+=18;
        text(ox,oy,"The USB stick itself is not listed.",GREY); oy+=28; }
        if(!ndisks){ text(ox+8,oy,"No disk was found (SATA, NVMe or IDE).",RED); oy+=24; }
        for(int i=0;i<ndisks;i++){ char sz[32]; size_str(sz,32,disks[i].sectors); snprintf(b,200,"%-14s -   %s%s",disks[i].name,sz,disks[i].ok?"":(CLONE&&disks[i].kind!=5)?"  (not a USB stick)":"  (too small)"); item(i,ox,oy,ow,b); oy+=24; }
        nl=ndisks; oy+=8; bt[0]="Continue"; bt[1]=CLONE?"Refresh":"Back"; nb=2; buttons(ox+ow/2,oy); }
    else if(page==P_WHERE){
        nwhere=0; where_part[nwhere++]=-1; for(int e=0;e<4;e++) if(di.ptype[e]&&di.pok[e])where_part[nwhere++]=e;
        box(&ox,&oy,&ow,660,176+(nwhere-1)*24+(di.nparts?di.nparts*18:18),"Installation Type");
        char sz[32]; size_str(sz,32,di.sectors); snprintf(b,200,"Where should NoovexOS go on %s (%s)?",di.name,sz); text(ox,oy,b,INK); oy+=28;
        for(int k=0;k<nwhere;k++){ int e=where_part[k]; if(e<0)snprintf(b,200,"Erase the whole disk and install"); else { char ps[32]; size_str(ps,32,di.psz[e]); snprintf(b,200,"Replace partition %d (%s, %s)",e+1,di.tname[e],ps); } item(k,ox,oy,ow,b); oy+=24; }
        nl=nwhere; oy+=4;
        if(!di.nparts){ text(ox,oy,"Partitions: none (empty disk)",GREY); oy+=18; }
        else for(int e=0;e<4;e++){ if(!di.ptype[e])continue; char ps[32]; size_str(ps,32,di.psz[e]); snprintf(b,200,"Partition %d: %-10s %s%s",e+1,di.tname[e],ps,di.pok[e]?"":"  (cannot be used)"); text(ox,oy,b,GREY); oy+=18; }
        oy+=12; bt[0]="Continue"; bt[1]="Back"; nb=2; buttons(ox+ow/2,oy); }
    else if(page==P_FS){
        box(&ox,&oy,&ow,560,216,"Select File System");
        text(ox,oy,"Choose the file system for NoovexOS:",INK); oy+=28;
        for(int i=0;i<4;i++){ snprintf(b,200,"%-12s %s",FSN[i],FSD[i]); item(i,ox,oy,ow,b); oy+=22; }
        nl=4; oy+=18; bt[0]="Continue"; bt[1]="Back"; nb=2; buttons(ox+ow/2,oy); }
    else if(page==P_CONFIRM){
        box(&ox,&oy,&ow,560,176,CLONE?"Confirm Copy":"Confirm Installation");
        if(CLONE){ snprintf(b,200,"About %ld MB of this system will be copied to:",CLONE_MB); text(ox,oy,b,INK); } else text(ox,oy,"NoovexOS will be installed to:",INK); oy+=24;
        if(pr.mode==0)snprintf(b,200,"  %s",di.name); else snprintf(b,200,"  %s, partition %d",di.name,pr.part+1); text(ox,oy,b,INK); oy+=22;
        int fi=0; for(int i=0;i<4;i++) if(FSV[i]==pr.fs)fi=i; snprintf(b,200,"File system: %s",FSN[fi]); text(ox,oy,b,INK); oy+=24;
        text(ox,oy,CLONE?"Everything on the USB stick will be erased.":pr.mode==0?"All data on this disk will be erased.":"All data in this partition will be erased.",RED); oy+=38;
        bt[0]="Back"; bt[1]=CLONE?"Copy":"Install"; nb=2; buttons(ox+ow/2,oy); }
    else {
        const char* t=CLONE?(page==P_RUN?"Copying NoovexOS":page==P_DONE?"USB Stick Ready":"Copy Failed"):page==P_RUN?"Installing NoovexOS":page==P_DONE?"Installation Complete":"Installation Failed";
        int extra=page==P_RUN?0:page==P_DONE?36:72;
        box(&ox,&oy,&ow,560,268+extra,t);
        text(ox,oy,page==P_RUN?(CLONE?"Please wait. Do not remove the USB stick.":"Please wait. Do not turn off the computer."):page==P_DONE?(CLONE?"This system was copied to the USB stick.":"NoovexOS has been installed successfully."):"The installation stopped. Nothing more was written.",INK); oy+=24;
        rect(ox,oy,ow,14,0xE2E6EE); rect(ox,oy,(int)((long)ow*pg.prog/1000),14,page==P_ERR?RED:0x4882C8); oy+=26;
        for(int i=0;i<8;i++){ const char* tag; int c;
            if(page==P_DONE||i<pg.stage){ tag="[ OK ]"; c=OK; } else if(i==pg.stage){ tag=page==P_ERR?"[FAIL]":"[ .. ]"; c=page==P_ERR?RED:0x4882C8; } else { tag="[    ]"; c=0xB4BAC6; }
            if(i==6&&pr.fs==0&&!CLONE&&page!=P_ERR&&i<pg.stage){ tag="[ -- ]"; c=GREY; }
            if(page==P_DONE&&i==6&&pr.fs==0&&!CLONE){ tag="[ -- ]"; c=GREY; }
            text(ox,oy,tag,c); text(ox+64,oy,STG[i],INK); oy+=18; }
        if(page==P_DONE&&CLONE){ oy+=6; text(ox,oy,"Restart now. Then the stick starts NoovexOS on any PC.",GREY); oy+=30; bt[0]="Restart"; nb=1; bfoc=0; buttons(ox+ow/2,oy); }
        else if(page==P_DONE){ oy+=6; text(ox,oy,"Remove the install media and reboot.",GREY); oy+=30; bt[0]="Reboot"; bt[1]="Close"; nb=2; buttons(ox+ow/2,oy); }
        if(page==P_ERR){ oy+=6; text(ox,oy,pg.e1,RED); oy+=18; text(ox,oy,pg.e2,INK); oy+=30; bt[0]="Back"; bt[1]="Close"; nb=2; buttons(ox+ow/2,oy); } }
    present(); }

static void go(int p){ page=p; lsel=0; bfoc=(p==P_CONFIRM)?1:0; }
static void quit(void){ __sys(209,0,0,0,0,0); exit(0); }
static void run_install(void){
    long r=__sys(204,(long)&pr,0,0,0,0);
    if(r){ snprintf(pg.e1,120,"The installer refused these settings (code %ld).",r); snprintf(pg.e2,120,"Choose another disk or partition."); pg.stage=0; pg.prog=0; go(P_ERR); return; }
    logk("installer program: install started"); pg.stage=0; pg.prog=0; go(P_RUN); draw();
    for(;;){ if(__sys(205,(long)&pg,0,0,0,0))break; draw(); if(pg.done){ go(P_DONE); logk("installer program: install finished"); break; } if(pg.failed){ go(P_ERR); logk("installer program: install failed"); break; } } }

static void press(int b){
    if(CLONE&&page==P_WELCOME){ if(b!=0){ quit(); return; } go(P_PREP); draw(); long r=__sys(211,0,0,0,0,0);
        if(r<=0){ snprintf(pg.e1,120,r==-2?"Start NoovexOS from the disk inside this computer first.":r==-3?"Not enough free memory to copy all files at once.":"The files could not be read (code %ld).",r); snprintf(pg.e2,120,r==-2?"A system started from a USB stick cannot copy itself.":r==-3?"Close programs and try again.":"Nothing was written."); pg.stage=0; pg.prog=0; go(P_ERR); return; }
        CLONE_MB=r; ndisks=(int)__sys(202,(long)disks,8,0,0,0); if(ndisks<0)ndisks=0; go(P_DISK); for(int i=0;i<ndisks;i++) if(disks[i].ok){ lsel=i; break; } return; }
    if(CLONE&&page==P_DISK&&b==0){ if(lsel<ndisks&&disks[lsel].ok&&__sys(203,lsel,(long)&di,0,0,0)==0){ pr.mode=0; pr.part=-1; pr.fs=0; pr.copy=1; snprintf(pr.label,16,"NoovexOS"); go(P_CONFIRM); } return; }
    if(CLONE&&page==P_DISK&&b==1){ ndisks=(int)__sys(202,(long)disks,8,0,0,0); if(ndisks<0)ndisks=0; lsel=0; return; }
    if(CLONE&&page==P_CONFIRM&&b==0){ go(P_DISK); return; }
    if(CLONE&&page==P_DONE){ __sys(209,1,0,0,0,0); return; }
    if(page==P_WELCOME){ if(b==0){ ndisks=(int)__sys(202,(long)disks,8,0,0,0); if(ndisks<0)ndisks=0; go(P_DISK); for(int i=0;i<ndisks;i++) if(disks[i].ok){ lsel=i; break; } } else quit(); }
    else if(page==P_DISK){ if(b==1){ go(P_WELCOME); return; } if(lsel<ndisks&&disks[lsel].ok&&__sys(203,lsel,(long)&di,0,0,0)==0)go(P_WHERE); }
    else if(page==P_WHERE){ if(b==1){ go(P_DISK); return; } if(lsel<nwhere){ pr.mode=where_part[lsel]<0?0:1; pr.part=where_part[lsel]; go(P_FS); } }
    else if(page==P_FS){ if(b==1){ go(P_WHERE); return; } pr.fs=FSV[lsel]; pr.copy=1; snprintf(pr.label,16,"NoovexOS"); go(P_CONFIRM); }
    else if(page==P_CONFIRM){ if(b==0)go(P_FS); else run_install(); }
    else if(page==P_DONE){ if(b==0)__sys(209,1,0,0,0,0); else quit(); }
    else if(page==P_ERR){ if(b==0)go(P_WELCOME); else quit(); } }

static void back(void){
    if(CLONE&&page==P_CONFIRM){ go(P_DISK); return; } if(CLONE&&page==P_DISK){ quit(); return; }
    if(page==P_WELCOME)quit(); else if(page==P_DISK)go(P_WELCOME); else if(page==P_WHERE)go(P_DISK); else if(page==P_FS)go(P_WHERE); else if(page==P_CONFIRM)go(P_FS); }

int main(void){
    if(__sys(200,0,0,0,0,0)!=1){ printf("nvx-install: this program can only be started by NoovexOS (install command)\n"); return 1; }
    int info[4]; __sys(201,(long)info,0,0,0,0); W=info[0]; H=info[1]; LOCKED=info[2]; SRC=info[3];
    { u64 ci[2]={0,0}; if(__sys(212,(long)ci,0,0,0,0)==0&&ci[0])CLONE=1; }
    draw();
    for(;;){
        int ev[4]; int dirty=0;
        for(;;){ __sys(208,(long)ev,0,0,0,0); mxp=ev[1]; myp=ev[2]; if(ev[3]&1)dirty=1;
            if((ev[3]&2)&&page!=P_RUN){ int hit=0;
                for(int k=0;k<nl;k++) if(mxp>=lx&&mxp<lx+lw&&myp>=ly[k]&&myp<ly[k]+20){ lsel=k; hit=1; dirty=1; break; }
                if(!hit) for(int i=0;i<nb;i++) if(mxp>=bx[i]&&mxp<bx[i]+bw[i]&&myp>=by&&myp<by+20){ bfoc=i; press(i); dirty=1; break; } }
            int c=ev[0]; if(c<0&&!(ev[3]&2))break; if(c<0)continue; dirty=1;
            if(page==P_RUN||page==P_PREP)continue;
            if(c==19&&nl)lsel=(lsel+nl-1)%nl; else if(c==20&&nl)lsel=(lsel+1)%nl;
            else if((c==17||c==18||c=='\t')&&nb>1)bfoc=(bfoc+(c==17?nb-1:1))%nb;
            else if(c=='\n'&&nb)press(bfoc); else if(c==27)back(); }
        if(dirty)draw();
        usleep(15000); }
    return 0; }
