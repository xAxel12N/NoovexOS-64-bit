#include <stdio.h>
#include <string.h>
#include <math.h>
#include <dlfcn.h>
#include <stdlib.h>
#include "net/bluetooth/sbc/sbc_enc.c"
#include "net/bluetooth/sbc/sbc_dec.c"
static int fails, checks;
#define CHECK(c,m) do{ checks++; if(c)printf("  %s ok\n",m); else { printf("  FAIL %s\n",m); fails++; } }while(0)
typedef struct { unsigned long flags; u8 frequency, blocks, subbands, mode, allocation, bitpool, endian; void* priv; void* priv_alloc_base; } lsbc_t;
static int (*l_init)(lsbc_t*,unsigned long); static long (*l_enc)(lsbc_t*,const void*,size_t,void*,size_t,long*); static void (*l_fin)(lsbc_t*); static int (*l_imsbc)(lsbc_t*,unsigned long); static long (*l_dec)(lsbc_t*,const void*,size_t,void*,size_t,size_t*);
static short pcm[2*48000];
static void gen(int ch,int rate){ unsigned s=12345; for(int i=0;i<rate;i++) for(int c=0;c<ch;c++){ s=s*1103515245+12345; double v=0.45*sin(2*M_PI*(c?1000.0:441.0)*i/rate)+0.15*sin(2*M_PI*6300.0*i/rate)+((int)(s>>16)%2000-1000)/32768.0; pcm[i*ch+c]=(short)(v*32767); } }
static void msbc_tests(void){
    static sbcenc_t e; static msbcdec_t d; static u8 a[64], b[64]; static short x[240], y[240]; lsbc_t L, D; memset(&L,0,sizeof L); memset(&D,0,sizeof D);
    CHECK(sbc_enc_init_msbc(&e)==57&&e.codesize==240,"mSBC encoder: 57-byte frames from 120 samples (16 kHz mono, 8 subbands, 15 blocks, bitpool 26)");
    gen(1,16000); if(!l_imsbc||!l_dec){ printf("  libsbc without mSBC: reference comparison skipped\n"); return; }
    l_imsbc(&L,0); l_imsbc(&D,0); L.endian=0; D.endian=0; msbc_dec_init(&d);
    int same=1, dsame=1, frames=0;
    for(const u8* p=(const u8*)pcm;p+240<=(const u8*)(pcm+16000);p+=240,frames++){ long w=0; size_t dw=0; l_enc(&L,p,240,b,sizeof b,&w); int n=sbc_enc_frame(&e,p,a,sizeof a);
        if(n!=57||w!=57||memcmp(a,b,57)){ if(same)printf("    mSBC frame %d differs (ours %d lib %ld)\n",frames,n,w); same=0; }
        long u=l_dec(&D,b,57,y,sizeof y,&dw); int k=msbc_dec_frame(&d,b,57,(int16_t*)x);
        if(u!=57||dw!=240||k!=120||memcmp(x,y,240)){ if(dsame)printf("    mSBC decode %d differs (ours %d lib %ld/%zu)\n",frames,k,u,dw); dsame=0; } }
    l_fin(&L); l_fin(&D);
    CHECK(same&&frames>100,"mSBC encoder: every frame bit-identical to BlueZ libsbc sbc_init_msbc");
    CHECK(dsame&&frames>100,"mSBC decoder: every frame decodes to the same samples as BlueZ libsbc");
    u8 bad[57]; memcpy(bad,b,57); bad[5]^=0x10; int16_t z[120]; CHECK(msbc_dec_frame(&d,bad,57,z)<0&&msbc_dec_frame(&d,b,40,z)<0,"mSBC decoder refuses a bad CRC and a short frame");
}
static int cmp_lib(int rate,int ch,int mode,int sbn,int blk,int alloc,int bp,const char* name){
    static sbcenc_t e; static u8 a[512], b[512]; lsbc_t L; memset(&L,0,sizeof L); l_init(&L,0);
    L.frequency=(u8)(rate==16000?0:rate==32000?1:rate==44100?2:3); L.blocks=(u8)(blk/4-1); L.subbands=sbn==8; L.mode=(u8)mode; L.allocation=(u8)alloc; L.bitpool=(u8)bp; L.endian=0;
    int fl=sbc_enc_init(&e,rate,ch,mode,sbn,blk,alloc,bp); int same=fl>0, frames=0; const u8* p=(const u8*)pcm; int left=rate*ch*2;
    while(same&&left>=e.codesize){ long w=0; long used=l_enc(&L,p,e.codesize,b,sizeof b,&w); int n=sbc_enc_frame(&e,p,a,sizeof a);
        if(used!=e.codesize||n!=w||n!=fl||memcmp(a,b,n)){ printf("    frame %d differs (ours %d lib %ld)\n",frames,n,w); same=0; } p+=e.codesize; left-=e.codesize; frames++; }
    l_fin(&L); char m[160]; snprintf(m,sizeof m,"%s: %d frames bit-identical to BlueZ libsbc",name,frames); CHECK(same&&frames>10,m); return same;
}
int main(void){
    setvbuf(stdout,0,_IONBF,0);
    u8 hdr[2]={0xBD,0x35}; u8 c1=sbc_crc8(hdr,16);
    CHECK(sbc_frame_len(8,16,2,SBC_MODE_JOINT,53)==119,"frame length 48k/16blk/8sb/joint/bitpool 53 = 119 bytes (A2DP high quality)");
    CHECK(sbc_frame_len(8,16,2,SBC_MODE_STEREO,53)==118&&sbc_frame_len(4,4,1,SBC_MODE_MONO,10)==4+2+5,"frame length stereo/mono formula");
    sbcenc_t t; CHECK(sbc_enc_init(&t,22050,2,SBC_MODE_JOINT,8,16,0,53)<0&&sbc_enc_init(&t,48000,2,SBC_MODE_JOINT,8,16,0,251)<0&&sbc_enc_init(&t,48000,1,SBC_MODE_JOINT,8,16,0,30)<0,"bad rate/bitpool/mode refused");
    (void)c1;
    void* h=dlopen("libsbc.so.1",RTLD_NOW);
    if(h){ l_init=dlsym(h,"sbc_init"); l_enc=dlsym(h,"sbc_encode"); l_fin=dlsym(h,"sbc_finish"); l_imsbc=dlsym(h,"sbc_init_msbc"); l_dec=dlsym(h,"sbc_decode"); }
    if(l_init&&l_enc&&l_fin){
        gen(2,48000); cmp_lib(48000,2,3,8,16,0,53,"48k joint 8sb 16blk bp53 loudness");
        cmp_lib(48000,2,2,8,16,1,51,"48k stereo 8sb 16blk bp51 SNR");
        cmp_lib(48000,2,1,8,12,0,30,"48k dual 8sb 12blk bp30");
        gen(2,44100); cmp_lib(44100,2,3,4,8,0,35,"44.1k joint 4sb 8blk bp35");
        gen(1,32000); cmp_lib(32000,1,0,8,16,0,31,"32k mono 8sb 16blk bp31");
        gen(1,16000); cmp_lib(16000,1,0,4,4,1,12,"16k mono 4sb 4blk bp12 SNR");
        msbc_tests();
    } else printf("  libsbc.so.1 not on this host: reference comparison skipped\n");
    gen(2,48000); static sbcenc_t e; sbc_enc_init(&e,48000,2,SBC_MODE_JOINT,8,16,0,53);
    FILE* f=fopen("/tmp/bt_sbc_t.sbc","wb"); static u8 o[256]; int nf=0;
    for(const u8* p=(const u8*)pcm;p+e.codesize<=(const u8*)(pcm+2*48000);p+=e.codesize){ int n=sbc_enc_frame(&e,p,o,sizeof o); if(n>0){ fwrite(o,1,n,f); nf++; } }
    fclose(f); FILE* r=fopen("/tmp/bt_sbc_t.raw","wb"); fwrite(pcm,2,2*48000,r); fclose(r);
    printf("  wrote %d frames to /tmp/bt_sbc_t.sbc\n",nf);
    printf(fails?"SBC HOST TEST: %d FAIL of %d\n":"SBC HOST TEST: ALL PASS (%d checks)\n",fails?fails:checks,checks);
    return fails!=0;
}
