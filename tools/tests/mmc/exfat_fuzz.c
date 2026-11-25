#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include "include/nvx/base.h"
#include "include/nvx/fs/fat/exfat.h"
void* kmalloc(u64 n){ return malloc(n?n:1); } void kfree(void* p){ free(p); }
void read_clock(int* hh,int* mm,int* ss,int* yr,int* mo,int* dy){ *hh=12; *mm=0; *ss=0; *yr=2026; *mo=11; *dy=19; }
static u8 *img, *orig; static u64 nsec; static long wbudget=-1, nwrites=0; static int oob=0, failn=0;
static u64 rng=1; static u32 rnd(void){ rng^=rng<<13; rng^=rng>>7; rng^=rng<<17; return (u32)(rng>>11); }
static int d_rd(unsigned long long l,int n,void* b){ if(n<=0||l+(u64)n>nsec){ oob++; return -1; } memcpy(b,img+l*512,(size_t)n*512); return 0; }
static int d_wr(unsigned long long l,int n,const void* b){ if(n<=0||l+(u64)n>nsec){ oob++; return -1; } if(wbudget>=0&&nwrites>=wbudget)return -1; if(failn>0){ failn--; return -1; } nwrites++; memcpy(img+l*512,b,(size_t)n*512); return 0; }
static int load(const char* p){ FILE* f=fopen(p,"rb"); if(!f)return -1; fseek(f,0,SEEK_END); nsec=(u64)ftell(f)/512; fseek(f,0,SEEK_SET); orig=malloc(nsec*512); img=malloc(nsec*512); if(fread(orig,512,nsec,f)!=nsec)return -1; fclose(f); memcpy(img,orig,nsec*512); return 0; }
static int save(const char* p){ FILE* f=fopen(p,"wb"); if(!f)return -1; fwrite(img,512,nsec,f); fclose(f); return 0; }
static u32 r32(const u8* p){ return (u32)p[0]|((u32)p[1]<<8)|((u32)p[2]<<16)|((u32)p[3]<<24); }
static void bootsum(u8* b){ u32 bs=1u<<orig[108], s=0; for(u32 i=0;i<11*bs;i++){ if(i==106||i==107||i==112)continue; s=((s&1)?0x80000000u:0)+(s>>1)+b[i]; } for(u32 k=0;k<bs/4;k++){ u8* q=b+11*bs+4*k; q[0]=(u8)s; q[1]=(u8)(s>>8); q[2]=(u8)(s>>16); q[3]=(u8)(s>>24); } }
static int fails=0, checks=0;
#define CK(c,m) do{ checks++; if(!(c)){ fails++; printf("FAIL %s\n",m); } }while(0)
static char lsn[256][1100]; static int lsd[256], lsc;
static void lscb(const char* n,int d,unsigned long long sz,void* c){ (void)sz; (void)c; if(lsc<256){ snprintf(lsn[lsc],sizeof lsn[0],"%s",n); lsd[lsc++]=d; } }
static char* rbuf;
static void tour(const char* dir,int depth){
    lsc=0; if(xf_ls(dir,lscb,0))return; int n=lsc; char (*nm)[1100]=malloc(sizeof lsn); int dd[256]; memcpy(nm,lsn,sizeof lsn); memcpy(dd,lsd,sizeof dd);
    for(int i=0;i<n;i++){ char p[2300]; snprintf(p,sizeof p,"%s/%s",strcmp(dir,"/")?dir:"",nm[i]); if(dd[i]){ if(depth<3)tour(p,depth+1); } else { xf_size(p); xf_read(p,rbuf,1<<20); } }
    free(nm); }
static void fuzz(int iters){
    u8* b=orig; u32 bss=b[108], csh=b[109], bs=1u<<bss, fatoff=r32(b+80), heap=r32(b+88), root=r32(b+96); u64 cb=(u64)bs<<csh;
    rbuf=malloc(1<<20); char* wb=malloc(300000); for(int i=0;i<300000;i++)wb[i]=(char)i;
    int mounted=0, rw=0;
    for(int it=0;it<iters;it++){
        if(getenv("XF_TRACE"))fprintf(stderr,"it %d\n",it); memcpy(img,orig,nsec*512); int nm=1+rnd()%8;
        for(int k=0;k<nm;k++){ u32 w=rnd()%6; u64 off;
            if(w==0)off=64+rnd()%56; else if(w==1)off=(u64)fatoff*bs+rnd()%(bs*2); else if(w<=3)off=((u64)heap*bs)+(u64)(root-2)*cb+rnd()%cb; else if(w==4)off=(u64)heap*bs+rnd()%(cb*4); else off=rnd()%(nsec*512);
            if(off>=nsec*512)continue; u32 m=rnd()%4; img[off]=m==0?0:m==1?0xFF:m==2?(u8)rnd():(u8)(img[off]^(1u<<(rnd()%8))); }
        if(rnd()%4)bootsum(img);
        if(xf_mount(d_rd,d_wr,0,nsec,rnd()%5==0))continue; mounted++; if(!xf_ro())rw++;
        tour("/",0); char p[64]; snprintf(p,sizeof p,"/fz%d",it); xf_write(p,wb,(long)(rnd()%300000)); xf_mkdir("/fzd"); xf_write("/fzd/x",wb,(long)(rnd()%9000)); xf_rename(p,"/fzd/y"); xf_write("/fzd/y",wb,100);
        lsc=0; xf_ls("/",lscb,0); if(lsc){ char q[1200]; snprintf(q,sizeof q,"/%s",lsn[rnd()%lsc]); if(rnd()&1)xf_delete(q); else xf_rename(q,"/fzd/moved"); }
        xf_delete("/fzd/x"); xf_mark(1); xf_mark(0); tour("/",0); xf_umount(); }
    CK(!oob,"no access outside the volume"); printf("fuzz: %d images, %d mounted, %d writable\n",iters,mounted,rw); free(rbuf); free(wb); }
typedef struct { char p[400]; int dir; u8* d; long n; } ment;
static ment M[4000], S[4000]; static int MN, SN; static ment* A=M; static int AN, quiet; static u8* pend[8]; static int npend;
static void dfree(u8* p,int crash){ if(crash&&npend<8)pend[npend++]=p; else free(p); }
static ment T[4000]; static int TN, cop, ck_; static char cp[400], cq[400]; static long cn;
static int afind(const char* p){ for(int i=0;i<AN;i++) if(!strcasecmp(A[i].p,p))return i; return -1; }
static int pfx(const char* a,const char* b){ size_t n=strlen(a); return !strncasecmp(a,b,n)&&b[n]=='/'; }
static int mfind(const char* p){ for(int i=0;i<MN;i++) if(!strcasecmp(M[i].p,p))return i; return -1; }
static const char* NAMES[]={"a","bb","Report 2026.txt","\xc3\xa5\xc3\xa4\xc3\xb6","x1","x2","data.bin","longname_abcdefghijklmnopqrstuvwxyz_0123456789_ABCDEFGHIJKLMNOPQRSTUVWXYZ","\xf0\x9f\x98\x80 smile","Q"};
static void rpath(char* o,int wantdir){ int ds[400], nd=0; ds[nd++]=-1; for(int i=0;i<MN;i++) if(M[i].dir&&nd<400&&strlen(M[i].p)<100)ds[nd++]=i; int d=ds[rnd()%nd]; (void)wantdir; snprintf(o,400,"%s/%s",d<0?"":M[d].p,NAMES[rnd()%10]); }
static void mixcase(const char* in,char* o){ int i=0; for(;in[i];i++){ char c=in[i]; if(rnd()%3==0){ if(c>='a'&&c<='z')c-=32; else if(c>='A'&&c<='Z')c+=32; } o[i]=c; } o[i]=0; }
static u8 *gb, *rb;
static long frees(void){ return (long)(xf_free_bytes()/4096); }
static int lookfor(const char* dir,int depth,int* seen){
    lsc=0; if(xf_ls(dir,lscb,0))return -1; int n=lsc; char (*nm)[1100]=malloc(sizeof lsn); int dd[256]; memcpy(nm,lsn,sizeof lsn); memcpy(dd,lsd,sizeof dd); int bad=0;
    for(int i=0;i<n;i++){ char p[2300]; snprintf(p,sizeof p,"%s/%s",strcmp(dir,"/")?dir:"",nm[i]); int k=afind(p);
        if(k<0||A[k].dir!=dd[i]||seen[k]){ if(!quiet)printf("unexpected %s\n",p); bad++; continue; } seen[k]=1;
        if(dd[i])bad+=lookfor(p,depth+1,seen); else { long r=xf_read(p,(char*)rb,2000000); if(r!=A[k].n||memcmp(rb,A[k].d,(size_t)r)){ if(!quiet)printf("content differs %s (%ld vs %ld)\n",p,r,A[k].n); bad++; } } }
    free(nm); return bad; }
static int comparew(ment* a,int an){ static int seen[4000]; memset(seen,0,sizeof seen); A=a; AN=an; int bad=lookfor("/",0,seen); for(int i=0;i<an;i++) if(!seen[i]){ if(!quiet)printf("missing %s\n",a[i].p); bad++; } A=M; return bad; }
static int compare(void){ return comparew(M,MN); }
static int stress(int nops,int crash){
    gb=malloc(2000000); rb=malloc(2000000); int bad=0, ok=0;
    if(xf_mount(d_rd,d_wr,0,nsec,0)){ printf("mount failed\n"); return 1; }
    if(crash){ xf_mark(1); wbudget=nwrites+1+rnd()%crash; }
    for(int op=0;op<nops;op++){
        if(crash){ while(npend)free(pend[--npend]); memcpy(S,M,sizeof(ment)*(size_t)MN); SN=MN; cop=0; }
        char p[400], q[400], mp[400]; u32 t=rnd()%100; long nw0=nwrites; long fr=frees(); int r;
        if(t<40){ rpath(p,0); long n=rnd()%8==0?(long)(rnd()%1800000):rnd()%4==0?0:(long)(rnd()%20000); for(long i=0;i<n;i++)gb[i]=(u8)(rnd()); int k=mfind(p); if(k<0&&MN>=3990)continue; char* sl=strrchr(p,'/'); *sl=0; int pk=p[0]?mfind(p):-1; *sl='/';
            mixcase(p,mp); cop=1; snprintf(cp,400,"%s",p); cn=n; long w=xf_write(mp,(const char*)gb,n); int expect=(k<0||!M[k].dir)&&(!p[1]||pk>=0||sl==p);
            if(w==n){ if(!expect){ printf("write %s should fail\n",p); bad++; } else { if(k<0){ k=MN++; snprintf(M[k].p,400,"%s",p); M[k].dir=0; M[k].d=0; } dfree(M[k].d,crash); M[k].d=malloc((size_t)n+1); memcpy(M[k].d,gb,(size_t)n); M[k].n=n; ok++; } }
            else if(expect&&!crash&&(n+4095)/4096+2<fr){ printf("write %s (%ld) failed with %ld free clusters\n",p,n,fr); bad++; } }
        else if(t<55){ rpath(p,1); int k=mfind(p); char* sl=strrchr(p,'/'); *sl=0; int pk=p[0]?mfind(p):-1; *sl='/'; int expect=k<0&&(sl==p||pk>=0); mixcase(p,mp); cop=2; snprintf(cp,400,"%s",p); r=xf_mkdir(mp);
            if(!r){ if(!expect){ printf("mkdir %s should fail\n",p); bad++; } else if(MN<3990){ k=MN++; snprintf(M[k].p,400,"%s",p); M[k].dir=1; M[k].d=0; M[k].n=0; ok++; } }
            else if(expect&&!crash&&fr>3&&MN<3990){ printf("mkdir %s failed\n",p); bad++; } }
        else if(t<70&&MN){ int k=rnd()%MN; int empty=1; for(int i=0;i<MN;i++) if(pfx(M[k].p,M[i].p))empty=0; mixcase(M[k].p,mp); cop=3; ck_=k; r=xf_delete(mp);
            if(!r){ if(!empty){ printf("deleted non-empty %s\n",M[k].p); bad++; } dfree(M[k].d,crash); M[k]=M[--MN]; ok++; } else if(empty&&!crash){ printf("delete %s failed\n",M[k].p); bad++; } }
        else if(t<88&&MN){ int k=rnd()%MN; rpath(q,0); int tk=mfind(q); char* sl=strrchr(q,'/'); *sl=0; int pk=q[0]?mfind(q):-1; *sl='/'; snprintf(p,400,"%s",M[k].p);
            int deep=0; for(int i=0;i<MN;i++) if(pfx(p,M[i].p)&&strlen(M[i].p)-strlen(p)+strlen(q)>300)deep=1; if(deep)continue;
            int expect=tk<0&&(sl==q||pk>=0)&&!(M[k].dir&&pfx(p,q)); mixcase(p,mp); cop=4; snprintf(cp,400,"%s",p); snprintf(cq,400,"%s",q); r=xf_rename(mp,q);
            if(!r){ if(!expect){ printf("rename %s -> %s should fail\n",p,q); bad++; } else { size_t pl=strlen(p); for(int i=0;i<MN;i++) if(pfx(p,M[i].p)){ char n2[800]; snprintf(n2,sizeof n2,"%s%s",q,M[i].p+pl); snprintf(M[i].p,400,"%s",n2); } snprintf(M[k].p,400,"%s",q); ok++; } }
            else if(expect&&!crash&&fr>3){ printf("rename %s -> %s failed\n",p,q); bad++; } }
        else if(t<97&&MN){ int k=rnd()%MN; if(!M[k].dir){ mixcase(M[k].p,mp); long n=xf_read(mp,(char*)rb,2000000); if(!crash&&(n!=M[k].n||memcmp(rb,M[k].d,(size_t)n))){ printf("read %s differs\n",M[k].p); bad++; } } }
        else if(!crash){ CK(!xf_umount(),"umount"); if(xf_mount(d_rd,d_wr,0,nsec,0)){ printf("remount failed\n"); return 1; } }
        if(crash&&wbudget>=0&&nwrites>=wbudget&&nw0<wbudget){ printf("crash during op %u (%s)\n",t,t<40?"write":t<55?"mkdir":t<70?"delete":t<88?"rename":"other"); break; }
        if(bad>5)break; }
    if(crash){ xf_umount(); printf("crash after %ld writes\n",nwrites); wbudget=-1; CK(!xf_mount(d_rd,d_wr,0,nsec,1)&&xf_ro()&&xf_unclean(),"volume left by a power loss mounts read-only as unclean"); rbuf=malloc(1<<20); tour("/",0); free(rbuf); xf_umount();
        CK(!xf_mount(d_rd,d_wr,0,nsec,1)&&xf_ro()&&xf_unclean(),"read-only mount of the damaged volume does not repair"); xf_umount();
        int m=xf_mount(d_rd,d_wr,0,nsec,0); int rep=!m&&!xf_ro()&&!xf_unclean(); printf("repair %s (%d fixes)\n",rep?"ok":"refused",xf_repaired()); CK(rep,"read-write mount repairs the power-loss damage");
        if(!m){ quiet=1; int b1=comparew(M,MN), b2=comparew(S,SN); quiet=0; if(b1&&b2&&cop){ memcpy(T,S,sizeof(ment)*(size_t)SN); TN=SN; A=T; AN=TN; int k=cop==3?ck_:afind(cp); A=M;
                if(cop==1){ if(k<0){ k=TN++; snprintf(T[k].p,400,"%s",cp); T[k].dir=0; } T[k].d=gb; T[k].n=cn; }
                else if(cop==2&&k<0){ k=TN++; snprintf(T[k].p,400,"%s",cp); T[k].dir=1; T[k].d=0; T[k].n=0; }
                else if(cop==3)T[k]=T[--TN];
                else if(cop==4&&k>=0){ size_t pl=strlen(cp); for(int i=0;i<TN;i++) if(pfx(cp,T[i].p)){ char n2[800]; snprintf(n2,sizeof n2,"%s%s",cq,T[i].p+pl); snprintf(T[i].p,400,"%s",n2); } snprintf(T[k].p,400,"%s",cq); }
                quiet=1; b2=comparew(T,TN); quiet=0; }
            if(b1&&b2){ printf("DATALOSS\n"); comparew(S,SN); bad++; } xf_umount(); }
        return bad; }
    bad+=compare(); CK(!xf_umount(),"final umount"); CK(!xf_mount(d_rd,d_wr,0,nsec,0)&&!xf_ro(),"remount writable"); bad+=compare(); xf_umount();
    printf("stress: %d ops ok, %d entries in the model, %d mismatches\n",ok,MN,bad); return bad; }
static void regress(void){
    memcpy(img,orig,nsec*512); CK(!xf_mount(d_rd,d_wr,0,nsec,0)&&!xf_ro(),"mount");
    CK(!xf_mkdir("/\xc3\xa9t\xc3\xa9")&&xf_write("/\xc3\xa9t\xc3\xa9/f","abc",3)==3,"make /\xc3\xa9t\xc3\xa9");
    CK(xf_rename("/\xc3\xa9t\xc3\xa9","/\xc3\x89T\xc3\x89/sub")!=0,"moving a folder into itself through a non-ASCII case variant is refused");
    CK(xf_rename("/\xc3\xa9t\xc3\xa9","/\xc3\x89T\xc3\x89")!=0,"renaming onto its own case variant is refused");
    char b[8]; CK(xf_read("/\xc3\x89T\xc3\x89/F",b,8)==3&&!memcmp(b,"abc",3),"folder still reachable");
    CK(xf_mkdir("/bad\xed\xa0\x80")!=0,"UTF-8 encoded lone surrogate refused");
    xf_umount();
    memcpy(img,orig,nsec*512); CK(!xf_mount(d_rd,d_wr,0,nsec,0),"mount");
    CK(!xf_mark(1),"set VolumeDirty"); failn=1; CK(xf_write("/w","hello",5)!=5,"write fails on a device write error"); CK(xf_mark(0)!=0,"VolumeDirty is not cleared after a failed device write"); xf_umount(); failn=0; CK(!xf_mount(d_rd,d_wr,0,nsec,0)&&(xf_ro()||xf_repaired()||!xf_unclean()),"next mount checks the dirty volume (repaired, read-only, or found clean)"); xf_umount();
    u8* b0=orig; u32 bs=1u<<b0[108], heap=r32(b0+88); u64 cb=(u64)bs<<b0[109]; u32 bmc=0; u64 roff=(u64)heap*bs+(u64)(r32(b0+96)-2)*cb;
    for(int i=0;i<64;i++){ u8* e=orig+roff+i*32; if(e[0]==0x81){ bmc=r32(e+20); break; } }
    memcpy(img,orig,nsec*512); img[(u64)heap*bs+(u64)(bmc-2)*cb]&=(u8)~1; CK(!xf_mount(d_rd,d_wr,0,nsec,0)&&xf_ro(),"metadata cluster marked free in the bitmap mounts read-only"); xf_umount();
    for(int i=0;i<64;i++){ u8* e=orig+roff+i*32; if(e[0]==0x82){ memcpy(img,orig,nsec*512); u64 uo=(u64)heap*bs+(u64)(r32(e+20)-2)*cb; img[uo+200]^=1; CK(!xf_mount(d_rd,d_wr,0,nsec,0)&&xf_ro(),"up-case table with a bad checksum mounts read-only"); xf_umount(); break; } }
    memcpy(img,orig,nsec*512); CK(!xf_mount(d_rd,d_wr,0,nsec,0)&&!xf_ro(),"mount");
    static char big[40000]; for(int i=0;i<40000;i++)big[i]=(char)(i*7);
    CK(xf_write("/v1",big,40000)==40000,"write v1"); CK(xf_write("/v2","x",1)==1,"write v2");
    u64 fr=xf_free_bytes(); long huge=(long)fr+4096; char* hb=calloc(1,(size_t)huge); CK(xf_write("/v1",hb,huge)<0,"overwrite larger than free space fails"); free(hb);
    char* rr=malloc(40000); CK(xf_read("/v1",rr,40000)==40000&&!memcmp(rr,big,40000),"failed overwrite keeps the old content"); free(rr);
    CK(xf_free_bytes()==fr,"free space unchanged after the failed overwrite");
    u8 eb[32]; u32 v1c=0; for(int i=0;i<128;i++){ memcpy(eb,img+roff+i*32,32); if(eb[0]==0xC0&&img[roff+(i+1)*32]==0xC1&&img[roff+(i+1)*32+2]=='v'&&img[roff+(i+1)*32+4]=='1'){ v1c=r32(eb+20); break; } }
    xf_umount(); CK(v1c>=2,"found /v1 stream entry");
    if(v1c>=2){ u64 bo=(u64)heap*bs+(u64)(bmc-2)*cb; u32 bit=v1c-2; img[bo+(bit>>3)]&=(u8)~(1u<<(bit&7)); bootsum(img);
        CK(!xf_mount(d_rd,d_wr,0,nsec,0),"mount with a bitmap that lost /v1's first cluster");
        CK(xf_write("/v1","new content",11)==11,"overwrite /v1"); CK(xf_write("/v3",big,40000)==40000,"write /v3");
        char t2[16]; CK(xf_read("/v1",t2,16)==11&&!memcmp(t2,"new content",11),"overwritten /v1 not clobbered by /v3 (old clusters pinned)"); xf_umount(); }
    printf("regress done\n"); }
int main(int argc,char** argv){
    if(argc<3||load(argv[2])){ fprintf(stderr,"usage: exfat_fuzz fuzz|stress|crash|regress img [seed n out]\n"); return 2; }
    rng=argc>3?strtoull(argv[3],0,0)*2654435761ull+1:1; int n=argc>4?atoi(argv[4]):100; int bad=0;
    if(!strcmp(argv[1],"fuzz"))fuzz(n);
    else if(!strcmp(argv[1],"stress"))bad=stress(n,0);
    else if(!strcmp(argv[1],"crash"))bad=stress(n,400);
    else if(!strcmp(argv[1],"regress"))regress();
    if(argc>5)save(argv[5]);
    printf("%d checks, %d failures\n",checks,fails); if(!fails&&!bad)printf("ALL PASS\n"); return fails||bad; }
