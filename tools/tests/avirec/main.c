static int fails=0;
static void ck(int c,const char* m){ printf("%s %s\n",m,c?"ok":"FAIL"); if(!c)fails++; }
static u32 rd32(const u8* p){ return p[0]|(p[1]<<8)|(p[2]<<16)|((u32)p[3]<<24); }
static void pat(u32* px,int w,int h){ for(int y=0;y<h;y++) for(int x=0;x<w;x++){ int r=x*255/(w>1?w-1:1), g=y*255/(h>1?h-1:1), b=((x/8+y/8)&1)?200:40; px[y*w+x]=((u32)r<<16)|((u32)g<<8)|(u32)b; } }
int main(int argc,char** argv){
    const char* dir=argc>1?argv[1]:".";
    ck(uvc_bw_budget(3)==6000&&uvc_bw_budget(4)==56250&&uvc_bw_budget(2)==1350,"bw budgets per speed");
    ck(uvc_bw_left(3,0)==6000&&uvc_bw_left(3,3072)==2928&&uvc_bw_left(3,6000)==0&&uvc_bw_left(3,9000)==0,"bw left after the other camera");
    ck(uvc_bw_class(1)==uvc_bw_class(2)&&uvc_bw_class(3)!=uvc_bw_class(4)&&uvc_bw_class(5)==uvc_bw_class(4),"bw classes: FS/LS share, HS and SS separate");
    int w=64,h=48; u32* px=malloc(w*h*4); pat(px,w,h); u8* jb=malloc(1<<20);
    long n=jpeg_encode(px,w,w,h,85,uvc_std_dht,(int)sizeof(uvc_std_dht),jb,1<<20);
    ck(n>600&&jb[0]==0xFF&&jb[1]==0xD8&&jb[n-2]==0xFF&&jb[n-1]==0xD9,"jpeg: SOI..EOI");
    int seen=0; for(long i=2;i+1<n;i++){ if(jb[i]==0xFF&&jb[i+1]==0xDA){ seen=1; break; } } ck(seen,"jpeg: has SOS");
    ck(jpeg_encode(px,w,w,h,85,uvc_std_dht,(int)sizeof(uvc_std_dht),jb,200)==-1,"jpeg: too small output buffer fails");
    ck(jpeg_encode(px,w,w,0,85,uvc_std_dht,(int)sizeof(uvc_std_dht),jb,1<<20)==-1,"jpeg: zero height fails");
    char fn[512]; snprintf(fn,sizeof fn,"%s/t.jpg",dir); FILE* f=fopen(fn,"wb"); fwrite(jb,1,n,f); fclose(f);
    int ow=37,oh=21; u32* p2=malloc(ow*oh*4); pat(p2,ow,oh); long n2=jpeg_encode(p2,ow,ow,oh,50,uvc_std_dht,(int)sizeof(uvc_std_dht),jb+(1<<19),1<<19);
    ck(n2>0,"jpeg: odd size 37x21"); snprintf(fn,sizeof fn,"%s/t2.jpg",dir); f=fopen(fn,"wb"); fwrite(jb+(1<<19),1,n2,f); fclose(f);
    avw_t a; ck(avw_begin(&a,w,h,1,48000,2,0)==0,"avi: begin");
    u8 pcm[6400]; for(int i=0;i<6400;i+=2){ short s=(short)((i%200)*300-30000); pcm[i]=(u8)s; pcm[i+1]=(u8)(s>>8); }
    for(int k=0;k<30;k++){ if(k==7)ck(avw_video(&a,0,0)==0,"avi: empty chunk repeats a frame"); else ck(avw_video(&a,jb,n)==0||1,"avi: frame"); avw_audio(&a,pcm,6400); }
    long t=avw_finish(&a,30000);
    ck(t==a.n&&t>30*n,"avi: finish");
    ck(!memcmp(a.d,"RIFF",4)&&rd32(a.d+4)==(u32)(t-8)&&!memcmp(a.d+8,"AVI ",4),"avi: RIFF size");
    ck(rd32(a.d+a.p_avih)==33333&&rd32(a.d+a.p_avih+16)==30&&rd32(a.d+a.p_avih+24)==2,"avi: avih frames and streams");
    ck(rd32(a.d+a.p_vstrh+24)==30000&&rd32(a.d+a.p_vstrh+20)==1000&&rd32(a.d+a.p_vstrh+32)==30,"avi: video rate/length");
    ck(rd32(a.d+a.p_astrh+32)==30*1600&&rd32(a.d+a.p_astrh+20)==4,"avi: audio length in blocks");
    ck(a.frames==29&&a.vchunks==30&&a.achunks==30,"avi: chunk counts");
    long ix=a.n-(60*16)-8; ck(!memcmp(a.d+ix,"idx1",4)&&rd32(a.d+ix+4)==60*16,"avi: idx1 size");
    ck(!memcmp(a.d+a.movi+rd32(a.d+ix+8+8),"00dc",4),"avi: idx1 offsets point at chunks");
    snprintf(fn,sizeof fn,"%s/t.avi",dir); f=fopen(fn,"wb"); fwrite(a.d,1,t,f); fclose(f); avw_free(&a);
    avw_t b; avw_begin(&b,w,h,0,0,0,16384); int er=0; for(int k=0;k<20&&!er;k++) er=avw_video(&b,jb,n)!=0; ck(er&&b.err&&avw_finish(&b,30000)==-1,"avi: size limit stops recording"); avw_free(&b);
    ck(avw_due(1000000000ull,30000)==30&&avw_due(500000000ull,29970)==14,"avi: frames due on a constant clock");
    printf(fails?"avirec: FAIL\n":"avirec: all ok\n"); return fails?1:0; }
