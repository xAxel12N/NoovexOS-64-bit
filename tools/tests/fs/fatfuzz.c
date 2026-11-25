#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "fs/fat/vfat.c"
void* kmalloc(unsigned long n){ return malloc(n?n:1); }
void kfree(void* p){ free(p); }
void read_clock(int* hh,int* mm,int* ss,int* yr,int* mo,int* dy){ *hh=12; *mm=0; *ss=0; *yr=2026; *mo=11; *dy=20; }
static u8* img; static u64 nsec; static int wr_fail;
typedef struct { u64 lba; u8 old[512]; } undo_t; static undo_t* ul; static int un, ucap;
static int r_rd(unsigned long long l,int n,void* b){ if(n<0||l+(u64)n>nsec)return -5; memcpy(b,img+l*512,(size_t)n*512); return 0; }
static int r_wr(unsigned long long l,int n,const void* b){ if(n<0||l+(u64)n>nsec)return -5; if(wr_fail&&rand()%64==0)return -5;
    for(int i=0;i<n;i++){ if(un==ucap){ ucap=ucap?ucap*2:256; ul=realloc(ul,sizeof(undo_t)*ucap); } ul[un].lba=l+i; memcpy(ul[un].old,img+(l+i)*512,512); un++; }
    memcpy(img+l*512,b,(size_t)n*512); return 0; }
static void undo(void){ while(un>0){ un--; memcpy(img+ul[un].lba*512,ul[un].old,512); } }
static int nls; static char names[64][256];
static void lscb(const char* name,int isdir,unsigned long long size,void* ctx){ (void)isdir; (void)size; (void)ctx; if(nls<64){ strncpy(names[nls],name,255); names[nls][255]=0; nls++; } }
static char rb[1<<20];
static void exercise(int rw,unsigned sel){
    if(vf_mount(r_rd,r_wr,0,nsec,!rw))return;
    nls=0; vf_ls("/",lscb,0); int top=nls;
    for(int i=0;i<top&&i<16;i++){ char p[300]; snprintf(p,sizeof p,"/%s",names[i]); long sz=vf_size(p); vf_exists(p); if(sz>=0)vf_read(p,rb,sizeof rb); else { vf_ls(p,lscb,0); } }
    for(int i=top;i<nls&&i<48;i++){ char p[600]; snprintf(p,sizeof p,"/%s/%s",names[0],names[i]); vf_read(p,rb,sizeof rb); }
    if(rw){ wr_fail=sel&1;
        vf_write("/NEWFILE.TXT","hello fat fuzz",14); vf_mkdir("/A long directory name"); vf_write("/A long directory name/inner file.bin",rb,(long)(sel%40000));
        if(top){ char p[300], q[300]; snprintf(p,sizeof p,"/%s",names[sel%top]); snprintf(q,sizeof q,"/renamed %u",sel); if(sel&2)vf_rename(p,q); else vf_delete(p); }
        vf_inplace("/NEWFILE.TXT","HELLO FAT FUZZ",14); vf_free_bytes_kb(); if(sel&4)vf_reclaim(); wr_fail=0; }
    vf_umount();
}
static u8* mkimg(int type,u64 n){
    img=calloc(n,512); nsec=n; if(vf_format(r_rd,r_wr,0,n,"FUZZ",type)){ printf("FAIL format %d\n",type); exit(1); }
    un=0; if(vf_mount(r_rd,r_wr,0,n,0)){ printf("FAIL mount fresh %d\n",type); exit(1); }
    static char big[70000]; for(int i=0;i<(int)sizeof big;i++)big[i]=(char)(i*7);
    vf_write("/README.TXT","readme",6); vf_write("/A file with a long name.dat",big,5000); vf_mkdir("/DIR"); vf_mkdir("/DIR/Sub Directory");
    for(int i=0;i<40;i++){ char p[64]; snprintf(p,sizeof p,"/DIR/entry number %d with long name.txt",i); vf_write(p,big,i*311); }
    vf_write("/DIR/Sub Directory/deep.bin",big,sizeof big); vf_umount(); un=0; return img;
}
static void fuzz(int type,u64 n,long iters,u64 hot){
    u8* base=mkimg(type,n); u8* pristine=malloc(hot*512); memcpy(pristine,base,hot*512);
    for(long it=0;it<iters;it++){
        int k=1+rand()%12; for(int j=0;j<k;j++){ u64 o; int r=rand()%10;
            if(r<3)o=(u64)(rand()%90); else if(r<5)o=(u64)rand()%(hot*512); else o=(u64)rand()%(hot*256);
            int v=rand()%5; img[o]=v==0?0:v==1?0xFF:v==2?(u8)(img[o]^(1<<(rand()%8))):(u8)rand(); }
        exercise(1,(unsigned)rand()); undo(); memcpy(img,pristine,hot*512); exercise(0,0);
    }
    free(pristine); free(img); img=0;
}
static void regress(void){
    for(int t=16;t<=32;t+=16){ mkimg(t,t==16?9000:70000); int ok=!vf_mount(r_rd,r_wr,0,nsec,1); nls=0; if(ok){ vf_ls("/",lscb,0); vf_ls("/DIR",lscb,0); } long r=ok?vf_read("/README.TXT",rb,sizeof rb):-1; long d=ok?vf_read("/DIR/Sub Directory/deep.bin",rb,sizeof rb):-1; vf_umount();
        if(!ok||nls<44||r!=6||memcmp(rb,"readme",0)||d!=70000){ printf("FAIL FAT%d seed image: mount %d, %d names, read %ld/%ld\n",t,ok,nls,r,d); exit(1); } free(img); img=0; }
    mkimg(16,9000); img[13]=128; img[22]=0xFF; img[23]=0xFF; exercise(1,3); undo(); free(img); img=0;
    mkimg(32,70000); memset(img+44,0xFF,4); exercise(1,5); undo(); free(img); img=0;
}
int main(int argc,char** argv){
    long iters=argc>1?atol(argv[1]):3000; srand(argc>2?(unsigned)atol(argv[2]):1);
    regress(); puts("fatfuzz: regressions ok");
    fuzz(16,9000,iters,200); fuzz(32,70000,iters/3,1400);
    printf("fatfuzz: %ld images per type, no faults\n",iters); return 0;
}
