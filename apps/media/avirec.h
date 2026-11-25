#ifndef NVX_APPS_MEDIA_AVIREC_H
#define NVX_APPS_MEDIA_AVIREC_H
typedef struct { u16 code[256]; u8 len[256]; } jph_t;
typedef struct { u8* o; long n, cap; u32 acc; int nb, err; } jpe_t;
static const u8 jpe_zz[64]={0,1,8,16,9,2,3,10,17,24,32,25,18,11,4,5,12,19,26,33,40,48,41,34,27,20,13,6,7,14,21,28,35,42,49,56,57,50,43,36,29,22,15,23,30,37,44,51,58,59,52,45,38,31,39,46,53,60,61,54,47,55,62,63};
static const u8 jpe_ql[64]={16,11,10,16,24,40,51,61,12,12,14,19,26,58,60,55,14,13,16,24,40,57,69,56,14,17,22,29,51,87,80,62,18,22,37,56,68,109,103,77,24,35,55,64,81,104,113,92,49,64,78,87,103,121,120,101,72,92,95,98,112,100,103,99};
static const u8 jpe_qc[64]={17,18,24,47,99,99,99,99,18,21,26,66,99,99,99,99,24,26,56,99,99,99,99,99,47,66,99,99,99,99,99,99,99,99,99,99,99,99,99,99,99,99,99,99,99,99,99,99,99,99,99,99,99,99,99,99,99,99,99,99,99,99,99,99};
static const short jpe_ct[9]={4096,4017,3784,3406,2896,2276,1567,799,0};
static int jpe_cosv(int a){ a&=31; return a<=8?jpe_ct[a]:a<=16?-jpe_ct[16-a]:a<=24?-jpe_ct[a-16]:jpe_ct[32-a]; }
static void jpe_b(jpe_t* j,int v){ if(j->n<j->cap)j->o[j->n++]=(u8)v; else j->err=1; }
static void jpe_w16(jpe_t* j,int v){ jpe_b(j,(v>>8)&255); jpe_b(j,v&255); }
static void jpe_bits(jpe_t* j,u32 code,int len){ if(len<=0)return; j->acc=(j->acc<<len)|(code&((1u<<len)-1u)); j->nb+=len; while(j->nb>=8){ int c=(int)((j->acc>>(j->nb-8))&255u); jpe_b(j,c); if(c==255)jpe_b(j,0); j->nb-=8; } }
static void jpe_tabs(const u8* d,int dl,jph_t* t){ int i=4; while(i+17<=dl){ int tc=d[i]>>4, th=d[i]&15; jph_t* h=&t[(tc?2:0)+(th?1:0)]; const u8* bits=d+i+1; const u8* v=d+i+17; int k=0; u32 c=0;
        for(int l=1;l<=16;l++){ for(int m=0;m<bits[l-1]&&i+17+k<dl;m++){ h->code[v[k]]=(u16)c; h->len[v[k]]=(u8)l; k++; c++; } c<<=1; } i+=17+k; } }
static int jpe_cat(int v){ if(v<0)v=-v; int n=0; while(v){ n++; v>>=1; } return n; }
static void jpe_block(jpe_t* j,const int* f,const u8* q,int* pdc,const jph_t* dc,const jph_t* ac){
    long long t[64]; int z[64];
    for(int y=0;y<8;y++) for(int u=0;u<8;u++){ long long s=0; for(int x=0;x<8;x++)s+=(long long)f[y*8+x]*jpe_cosv((2*x+1)*u); t[y*8+u]=(s*(u?4096:2896))>>12; }
    for(int v=0;v<8;v++) for(int u=0;u<8;u++){ long long s=0; for(int y=0;y<8;y++)s+=t[y*8+u]*jpe_cosv((2*y+1)*v); s=(s*(v?4096:2896))>>12; long long d=(long long)q[v*8+u]<<26; z[v*8+u]=(int)(s>=0?(s+d/2)/d:-((-s+d/2)/d)); }
    int dcv=z[0]-*pdc; *pdc=z[0]; int c=jpe_cat(dcv); jpe_bits(j,dc->code[c],dc->len[c]); if(c)jpe_bits(j,(u32)(dcv<0?dcv-1:dcv),c);
    int run=0; for(int k=1;k<64;k++){ int v=z[jpe_zz[k]]; if(!v){ run++; continue; } while(run>15){ jpe_bits(j,ac->code[0xF0],ac->len[0xF0]); run-=16; } c=jpe_cat(v); if(c>10)c=10; int sym=(run<<4)|c; jpe_bits(j,ac->code[sym],ac->len[sym]); jpe_bits(j,(u32)(v<0?v-1:v),c); run=0; }
    if(run)jpe_bits(j,ac->code[0],ac->len[0]); }
static void jpe_qt(const u8* b,int qual,u8* o){ if(qual<1)qual=1; if(qual>100)qual=100; int s=qual<50?5000/qual:200-qual*2; for(int i=0;i<64;i++){ int v=(b[i]*s+50)/100; o[i]=(u8)(v<1?1:v>255?255:v); } }
static long jpeg_encode(const u32* px,int stride,int w,int h,int qual,const u8* dht,int dhtlen,u8* out,long cap){
    if(!px||w<=0||h<=0||w>65535||h>65535||!out)return -1;
    static jph_t tb[4]; for(int i=0;i<4;i++) for(int k=0;k<256;k++){ tb[i].code[k]=0; tb[i].len[k]=0; } jpe_tabs(dht,dhtlen,tb);
    u8 ql[64], qc[64]; jpe_qt(jpe_ql,qual,ql); jpe_qt(jpe_qc,qual,qc);
    jpe_t J; J.o=out; J.n=0; J.cap=cap; J.acc=0; J.nb=0; J.err=0; jpe_t* j=&J;
    static const u8 app0[18]={0xFF,0xE0,0,16,'J','F','I','F',0,1,1,0,0,1,0,1,0,0};
    jpe_w16(j,0xFFD8); for(int i=0;i<18;i++)jpe_b(j,app0[i]);
    jpe_w16(j,0xFFDB); jpe_w16(j,132); jpe_b(j,0); for(int i=0;i<64;i++)jpe_b(j,ql[jpe_zz[i]]); jpe_b(j,1); for(int i=0;i<64;i++)jpe_b(j,qc[jpe_zz[i]]);
    jpe_w16(j,0xFFC0); jpe_w16(j,17); jpe_b(j,8); jpe_w16(j,h); jpe_w16(j,w); jpe_b(j,3); jpe_b(j,1); jpe_b(j,0x22); jpe_b(j,0); jpe_b(j,2); jpe_b(j,0x11); jpe_b(j,1); jpe_b(j,3); jpe_b(j,0x11); jpe_b(j,1);
    for(int i=0;i<dhtlen;i++)jpe_b(j,dht[i]);
    jpe_w16(j,0xFFDA); jpe_w16(j,12); jpe_b(j,3); jpe_b(j,1); jpe_b(j,0x00); jpe_b(j,2); jpe_b(j,0x11); jpe_b(j,3); jpe_b(j,0x11); jpe_b(j,0); jpe_b(j,63); jpe_b(j,0);
    int dy=0, dcb=0, dcr=0, Y[4][64], Cb[64], Cr[64];
    for(int my=0;my<h;my+=16) for(int mx=0;mx<w;mx+=16){
        for(int i=0;i<64;i++){ Cb[i]=0; Cr[i]=0; }
        for(int yy=0;yy<16;yy++) for(int xx=0;xx<16;xx++){ int sy=my+yy, sx=mx+xx; if(sy>=h)sy=h-1; if(sx>=w)sx=w-1; u32 p=px[(long)sy*stride+sx]; int r=(int)((p>>16)&255), g=(int)((p>>8)&255), b=(int)(p&255);
            Y[(yy>>3)*2+(xx>>3)][(yy&7)*8+(xx&7)]=((77*r+150*g+29*b)>>8)-128; int ci=(yy>>1)*8+(xx>>1); Cb[ci]+=(-43*r-85*g+128*b)>>8; Cr[ci]+=(128*r-107*g-21*b)>>8; }
        for(int i=0;i<64;i++){ Cb[i]=Cb[i]>=0?(Cb[i]+2)>>2:-((-Cb[i]+2)>>2); Cr[i]=Cr[i]>=0?(Cr[i]+2)>>2:-((-Cr[i]+2)>>2); }
        for(int k=0;k<4;k++)jpe_block(j,Y[k],ql,&dy,&tb[0],&tb[2]); jpe_block(j,Cb,qc,&dcb,&tb[1],&tb[3]); jpe_block(j,Cr,qc,&dcr,&tb[1],&tb[3]);
        if(j->err)return -1; }
    if(j->nb)jpe_bits(j,0x7F,8-j->nb); jpe_w16(j,0xFFD9); return j->err?-1:j->n; }
typedef struct { u8* d; long n, cap, lim; u32* ix; long ixn, ixcap; int w, h, audio, err; u32 frames, vchunks, achunks, abytes, rate, ch, vmax, amax; long movi, p_avih, p_vstrh, p_astrh; } avw_t;
static int avw_grow(avw_t* a,long need){ if(a->err)return 0; if(a->n+need<=a->cap)return 1; long nc=a->cap?a->cap*2:(1L<<20); while(nc<a->n+need)nc*=2; if(a->lim&&nc>a->lim)nc=a->lim; if(nc<a->n+need){ a->err=1; return 0; }
    u8* nd=(u8*)AVW_ALLOC((unsigned long)nc); if(!nd){ a->err=1; return 0; } for(long i=0;i<a->n;i++)nd[i]=a->d[i]; if(a->d)AVW_FREE(a->d); a->d=nd; a->cap=nc; return 1; }
static void avw_put(avw_t* a,const void* s,long n){ if(!avw_grow(a,n))return; const u8* p=(const u8*)s; for(long i=0;i<n;i++)a->d[a->n+i]=p[i]; a->n+=n; }
static void avw_set32(avw_t* a,long at,u32 v){ if(at>=0&&at+4<=a->n){ a->d[at]=(u8)v; a->d[at+1]=(u8)(v>>8); a->d[at+2]=(u8)(v>>16); a->d[at+3]=(u8)(v>>24); } }
static void avw_32(avw_t* a,u32 v){ u8 b[4]={(u8)v,(u8)(v>>8),(u8)(v>>16),(u8)(v>>24)}; avw_put(a,b,4); }
static void avw_16(avw_t* a,u32 v){ u8 b[2]={(u8)v,(u8)(v>>8)}; avw_put(a,b,2); }
static void avw_cc(avw_t* a,const char* c){ avw_put(a,c,4); }
static u32 avw_fcc(const char* c){ return (u32)(u8)c[0]|((u32)(u8)c[1]<<8)|((u32)(u8)c[2]<<16)|((u32)(u8)c[3]<<24); }
static void avw_zero(avw_t* a,int n){ for(int i=0;i<n;i++){ u8 z=0; avw_put(a,&z,1); } }
static int avw_begin(avw_t* a,int w,int h,int audio,u32 rate,u32 ch,long lim){
    u8* z=(u8*)a; for(unsigned long i=0;i<sizeof(*a);i++)z[i]=0; a->w=w; a->h=h; a->audio=audio; a->rate=rate; a->ch=ch; a->lim=lim;
    avw_cc(a,"RIFF"); avw_32(a,0); avw_cc(a,"AVI "); avw_cc(a,"LIST"); long hl=a->n; avw_32(a,0); avw_cc(a,"hdrl");
    avw_cc(a,"avih"); avw_32(a,56); a->p_avih=a->n; avw_32(a,33333); avw_32(a,0); avw_32(a,0); avw_32(a,0x10); avw_32(a,0); avw_32(a,0); avw_32(a,audio?2:1); avw_32(a,1u<<20); avw_32(a,(u32)w); avw_32(a,(u32)h); avw_zero(a,16);
    avw_cc(a,"LIST"); avw_32(a,4+8+56+8+40); avw_cc(a,"strl"); avw_cc(a,"strh"); avw_32(a,56); a->p_vstrh=a->n; avw_cc(a,"vids"); avw_cc(a,"MJPG"); avw_32(a,0); avw_32(a,0); avw_32(a,0); avw_32(a,1000); avw_32(a,30000); avw_32(a,0); avw_32(a,0); avw_32(a,1u<<20); avw_32(a,0xFFFFFFFFu); avw_32(a,0); avw_16(a,0); avw_16(a,0); avw_16(a,(u32)w); avw_16(a,(u32)h);
    avw_cc(a,"strf"); avw_32(a,40); avw_32(a,40); avw_32(a,(u32)w); avw_32(a,(u32)h); avw_16(a,1); avw_16(a,24); avw_cc(a,"MJPG"); avw_32(a,(u32)w*(u32)h*3u); avw_zero(a,16);
    if(audio){ u32 ba=ch*2u; avw_cc(a,"LIST"); avw_32(a,4+8+56+8+18); avw_cc(a,"strl"); avw_cc(a,"strh"); avw_32(a,56); a->p_astrh=a->n; avw_cc(a,"auds"); avw_32(a,0); avw_32(a,0); avw_32(a,0); avw_32(a,0); avw_32(a,ba); avw_32(a,rate*ba); avw_32(a,0); avw_32(a,0); avw_32(a,rate*ba); avw_32(a,0xFFFFFFFFu); avw_32(a,ba); avw_zero(a,8);
        avw_cc(a,"strf"); avw_32(a,18); avw_16(a,1); avw_16(a,ch); avw_32(a,rate); avw_32(a,rate*ba); avw_16(a,ba); avw_16(a,16); avw_16(a,0); }
    avw_set32(a,hl,(u32)(a->n-hl-4)); avw_cc(a,"LIST"); avw_32(a,0); a->movi=a->n; avw_cc(a,"movi"); return a->err?-1:0; }
static int avw_chunk(avw_t* a,const char* id,const u8* s,long n){
    if(a->err)return -1; if(a->ixn+4>a->ixcap){ long nc=a->ixcap?a->ixcap*2:4096; u32* ni=(u32*)AVW_ALLOC((unsigned long)nc*4u); if(!ni){ a->err=1; return -1; } for(long i=0;i<a->ixn;i++)ni[i]=a->ix[i]; if(a->ix)AVW_FREE(a->ix); a->ix=ni; a->ixcap=nc; }
    if(!avw_grow(a,8+n+1))return -1; long at=a->n; avw_cc(a,id); avw_32(a,(u32)n); if(n)avw_put(a,s,n); if(n&1)avw_zero(a,1);
    a->ix[a->ixn++]=avw_fcc(id); a->ix[a->ixn++]=n?0x10u:0u; a->ix[a->ixn++]=(u32)(at-a->movi); a->ix[a->ixn++]=(u32)n; return a->err?-1:0; }
static int avw_video(avw_t* a,const u8* jpg,long n){ if(avw_chunk(a,"00dc",jpg,n))return -1; a->vchunks++; if(n)a->frames++; if((u32)n>a->vmax)a->vmax=(u32)n; return 0; }
static int avw_audio(avw_t* a,const u8* pcm,long n){ if(!a->audio||n<=0)return 0; if(avw_chunk(a,"01wb",pcm,n))return -1; a->achunks++; a->abytes+=(u32)n; if((u32)n>a->amax)a->amax=(u32)n; return 0; }
static long avw_finish(avw_t* a,u32 fps1000){
    if(a->err)return -1; if(fps1000<1000)fps1000=1000; avw_set32(a,a->movi-4,(u32)(a->n-a->movi));
    avw_cc(a,"idx1"); avw_32(a,(u32)(a->ixn*4)); for(long i=0;i<a->ixn;i++)avw_32(a,a->ix[i]); if(a->err)return -1;
    u32 bs=a->vmax>a->amax?a->vmax:a->amax; bs+=8;
    avw_set32(a,4,(u32)(a->n-8)); avw_set32(a,a->p_avih,(u32)(1000000000ull/fps1000)); avw_set32(a,a->p_avih+4,(u32)((unsigned long long)(a->vmax+8)*fps1000/1000ull+a->rate*a->ch*2u)); avw_set32(a,a->p_avih+16,a->vchunks); avw_set32(a,a->p_avih+28,bs);
    avw_set32(a,a->p_vstrh+24,fps1000); avw_set32(a,a->p_vstrh+32,a->vchunks); avw_set32(a,a->p_vstrh+36,a->vmax+8);
    if(a->audio){ avw_set32(a,a->p_astrh+32,a->abytes/(a->ch*2u)); avw_set32(a,a->p_astrh+36,a->amax+8); }
    if(a->ix){ AVW_FREE(a->ix); a->ix=0; } return a->n; }
static void avw_free(avw_t* a){ if(a->d)AVW_FREE(a->d); if(a->ix)AVW_FREE(a->ix); a->d=0; a->ix=0; a->n=a->cap=0; }
static u32 avw_due(unsigned long long elapsed_ns,u32 fps1000){ return (u32)(elapsed_ns*fps1000/1000000000000ull); }
#endif
