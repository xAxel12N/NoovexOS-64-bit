#include "include/nvx/base.h"
#include "include/nvx/mm/heap.h"
#define XZ_USE_CRC64
#include <stdbool.h>
#include "third_party/xzembedded/xz.h"
#include "third_party/zstd/lib/zstd.h"
extern int pz_inflate_raw(const unsigned char* src,int slen,unsigned char* out,int outcap);
void* xzk_alloc(unsigned long n){ return kmalloc(n); }
void xzk_free(void* p){ if(p)kfree(p); }
static int g_xz_ready;
static void xzk_init(void){ if(!g_xz_ready){ xz_crc32_init(); xz_crc64_init(); g_xz_ready=1; } }
long xzk_decode_into(const u8* in,long n,u8* out,long cap){
    xzk_init(); struct xz_dec* s=xz_dec_init(XZ_SINGLE,0); if(!s)return -1;
    struct xz_buf b; b.in=in; b.in_pos=0; b.in_size=(size_t)n; b.out=out; b.out_pos=0; b.out_size=(size_t)cap;
    enum xz_ret r=xz_dec_run(s,&b); xz_dec_end(s); if(r!=XZ_STREAM_END)return -1; return (long)b.out_pos; }
static int ugrow(u8** buf,long* cap,long need){ if(need<=*cap)return 1; long nc=*cap*2; if(nc<need)nc=need; u8* n=(u8*)krealloc(*buf,(unsigned long)nc); if(!n)return 0; *buf=n; *cap=nc; return 1; }
u8* unp_xz(const u8* in,long n,long* outlen){
    xzk_init(); struct xz_dec* s=xz_dec_init(XZ_DYNALLOC,1u<<26); if(!s)return 0;
    long cap=n*4+65536; u8* out=(u8*)kmalloc((unsigned long)cap); if(!out){ xz_dec_end(s); return 0; }
    struct xz_buf b; b.in=in; b.in_pos=0; b.in_size=(size_t)n; b.out=out; b.out_pos=0; b.out_size=(size_t)cap;
    for(;;){ enum xz_ret r=xz_dec_run(s,&b);
        if(r==XZ_STREAM_END)break;
        if(r!=XZ_OK&&r!=XZ_UNSUPPORTED_CHECK){ xz_dec_end(s); kfree(out); return 0; }
        if(b.out_pos==b.out_size){ long c=cap; if(!ugrow(&out,&c,cap*2)){ xz_dec_end(s); kfree(out); return 0; } cap=c; b.out=out; b.out_size=(size_t)cap; }
        else if(b.in_pos==b.in_size){ xz_dec_end(s); kfree(out); return 0; } }
    xz_dec_end(s); *outlen=(long)b.out_pos; return out; }
u8* unp_zst(const u8* in,long n,long* outlen){
    ZSTD_DStream* d=ZSTD_createDStream(); if(!d)return 0; ZSTD_initDStream(d);
    long cap=n*4+65536; u8* out=(u8*)kmalloc((unsigned long)cap); if(!out){ ZSTD_freeDStream(d); return 0; }
    ZSTD_inBuffer ib={in,(size_t)n,0}; ZSTD_outBuffer ob={out,(size_t)cap,0};
    for(;;){ size_t r=ZSTD_decompressStream(d,&ob,&ib); if(ZSTD_isError(r)){ ZSTD_freeDStream(d); kfree(out); return 0; }
        if(r==0&&ib.pos==ib.size)break;
        if(ob.pos==ob.size){ long c=cap; if(!ugrow(&out,&c,cap*2)){ ZSTD_freeDStream(d); kfree(out); return 0; } cap=c; ob.dst=out; ob.size=(size_t)cap; }
        else if(ib.pos==ib.size){ if(r==0)break; ZSTD_freeDStream(d); kfree(out); return 0; } }
    ZSTD_freeDStream(d); *outlen=(long)ob.pos; return out; }
u8* unp_gz(const u8* in,long n,long* outlen){
    if(n<18||in[0]!=0x1F||in[1]!=0x8B||in[2]!=8)return 0; int fl=in[3]; long p=10;
    if(fl&4){ if(p+2>n)return 0; p+=2+(in[p]|(in[p+1]<<8)); }
    if(fl&8){ while(p<n&&in[p])p++; p++; }
    if(fl&16){ while(p<n&&in[p])p++; p++; }
    if(fl&2)p+=2; if(p>=n)return 0;
    u32 isz=(u32)in[n-4]|((u32)in[n-3]<<8)|((u32)in[n-2]<<16)|((u32)in[n-1]<<24);
    u8* out=(u8*)kmalloc(isz?isz:1); if(!out)return 0;
    int r=pz_inflate_raw(in+p,(int)(n-p-8),out,(int)isz); if(r!=(int)isz){ kfree(out); return 0; }
    *outlen=(long)isz; return out; }
int unp_kind(const u8* in,long n){
    if(n>=6&&in[0]==0xFD&&in[1]=='7'&&in[2]=='z'&&in[3]=='X'&&in[4]=='Z')return 2;
    if(n>=4&&in[0]==0x28&&in[1]==0xB5&&in[2]==0x2F&&in[3]==0xFD)return 3;
    if(n>=3&&in[0]==0x1F&&in[1]==0x8B)return 1;
    return 0; }
u8* unp_auto(const u8* in,long n,long* outlen){
    int k=unp_kind(in,n);
    if(k==1)return unp_gz(in,n,outlen);
    if(k==2)return unp_xz(in,n,outlen);
    if(k==3)return unp_zst(in,n,outlen);
    u8* o=(u8*)kmalloc((unsigned long)(n?n:1)); if(!o)return 0; for(long i=0;i<n;i++)o[i]=in[i]; *outlen=n; return o; }
