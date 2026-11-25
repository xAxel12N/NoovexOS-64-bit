#ifndef NVX_FID_POLICY_H
#define NVX_FID_POLICY_H
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <math.h>

#define FP_DIM 128
#define FP_T_MATCH 0.45f
#define FP_T_SUPPORT 0.52f
#define FP_SAME_PERSON 0.40f
#define FP_DUP 0.05f
#define FP_ENROL_MIN 5
#define FP_ENROL_MAX 8
#define FP_BIN_MAX 2
#define FP_NEED_MATCH 3
#define FP_YAW_SPAN 0.10f
#define FP_BLINK_DEV 0.15f
#define FP_FACE_STABLE 0.05f
#define FP_STILL_EMB 0.004f
#define FP_STILL_PX 0.35f
#define FP_STILL_MAX 6

enum { FPV_NOMATCH=0, FPV_NEEDLIVE=1, FPV_MATCH=2, FPV_SPOOF=3 };

static inline float fp_dist(const float* a,const float* b){ float s=0; for(int k=0;k<FP_DIM;k++){ float d=a[k]-b[k]; s+=d*d; } return sqrtf(s); }

static inline int fp_pose(const float* p,float* yaw,float* pitch){
    float ex1=(p[0]+p[2])*0.5f, ey1=(p[1]+p[3])*0.5f, ex2=(p[4]+p[6])*0.5f, ey2=(p[5]+p[7])*0.5f;
    float vx=ex2-ex1, vy=ey2-ey1, d2=vx*vx+vy*vy; if(d2<4.0f) return 0;
    float nx=p[8]-(ex1+ex2)*0.5f, ny=p[9]-(ey1+ey2)*0.5f;
    *yaw=(nx*vx+ny*vy)/d2; *pitch=(vx*ny-vy*nx)/d2; return 1; }

static inline int fp_pose_bin(float yaw){ return yaw<-0.06f?0:yaw>0.06f?2:1; }
static inline int fp_light_bin(float luma){ return luma<80.0f?0:luma>160.0f?2:1; }

typedef struct { float t[FP_ENROL_MAX][FP_DIM]; int bin[FP_ENROL_MAX]; int n; } fp_enrol;

static inline int fp_match_frame(const float (*t)[FP_DIM],int n,const float* e,float* best){
    float b=1e9f; int sup=0; for(int i=0;i<n;i++){ float d=fp_dist(t[i],e); if(d<b) b=d; if(d<FP_T_SUPPORT) sup++; }
    if(best) *best=n?b:-1.0f; int need=n>=2?2:1;
    return n>0&&b<FP_T_MATCH&&sup>=need; }

static inline int fp_enrol_add(fp_enrol* s,const float* e,float yaw,float luma){
    int bin=fp_pose_bin(yaw)*3+fp_light_bin(luma);
    if(s->n>0&&fp_dist(s->t[0],e)>FP_SAME_PERSON) return -1;
    if(s->n>=FP_ENROL_MAX) return 0;
    int inbin=0; float md=1e9f; for(int i=0;i<s->n;i++){ if(s->bin[i]==bin) inbin++; float d=fp_dist(s->t[i],e); if(d<md) md=d; }
    if(inbin>=FP_BIN_MAX) return 0;
    if(s->n>0&&md<FP_DUP) return 0;
    memcpy(s->t[s->n],e,sizeof(float)*FP_DIM); s->bin[s->n]=bin; s->n++; return 1; }

static inline int fp_enrol_poses(const fp_enrol* s){ int m=0; for(int i=0;i<s->n;i++) m|=1<<(s->bin[i]/3); return (m&1)+((m>>1)&1)+((m>>2)&1); }
static inline int fp_enrol_ready(const fp_enrol* s){ return s->n>=FP_ENROL_MIN&&fp_enrol_poses(s)>=2; }
static inline int fp_enrol_shown(const fp_enrol* s){ return fp_enrol_ready(s)?s->n:(s->n<FP_ENROL_MIN-1?s->n:FP_ENROL_MIN-1); }

typedef struct { int n, nmatch, still, varied, blink; float ymin, ymax, emin, emax, fmin, fmax; float last[10]; int have; } fp_live;

static inline void fp_live_reset(fp_live* l){ memset(l,0,sizeof *l); }

static inline int fp_verify_step(fp_live* l,int matched,const float* pts,float emb_delta,float eye,float face){
    l->n++;
    if(l->have){ float m=0; for(int k=0;k<10;k++){ float d=fabsf(pts[k]-l->last[k]); if(d>m) m=d; }
        if(emb_delta>=0.0f&&emb_delta<FP_STILL_EMB&&m<FP_STILL_PX) l->still++; else l->varied=1; }
    memcpy(l->last,pts,sizeof l->last); l->have=1;
    if(!l->varied&&l->still>=FP_STILL_MAX) return FPV_SPOOF;
    if(!matched) return FPV_NOMATCH;
    float yaw=0,pitch=0; if(!fp_pose(pts,&yaw,&pitch)) return FPV_NOMATCH;
    if(!l->nmatch){ l->ymin=l->ymax=yaw; l->emin=l->emax=eye; l->fmin=l->fmax=face; }
    if(yaw<l->ymin) l->ymin=yaw; if(yaw>l->ymax) l->ymax=yaw;
    if(eye<l->emin) l->emin=eye; if(eye>l->emax) l->emax=eye;
    if(face<l->fmin) l->fmin=face; if(face>l->fmax) l->fmax=face;
    l->nmatch++;
    if(l->emax>0&&l->fmax>0&&(l->emax-l->emin)/l->emax>=FP_BLINK_DEV&&(l->fmax-l->fmin)/l->fmax<FP_FACE_STABLE) l->blink=1;
    int live=l->varied&&(l->ymax-l->ymin>=FP_YAW_SPAN||l->blink);
    if(l->nmatch<FP_NEED_MATCH) return FPV_NEEDLIVE;
    return live?FPV_MATCH:FPV_NEEDLIVE; }

#define FP_ROTL(a,b) (((a)<<(b))|((a)>>(32-(b))))
#define FP_QR(a,b,c,d) (a+=b,d^=a,d=FP_ROTL(d,16),c+=d,b^=c,b=FP_ROTL(b,12),a+=b,d^=a,d=FP_ROTL(d,8),c+=d,b^=c,b=FP_ROTL(b,7))
static inline uint32_t fp_le32(const uint8_t* p){ return (uint32_t)p[0]|((uint32_t)p[1]<<8)|((uint32_t)p[2]<<16)|((uint32_t)p[3]<<24); }
static inline void fp_st32(uint8_t* p,uint32_t v){ p[0]=(uint8_t)v; p[1]=(uint8_t)(v>>8); p[2]=(uint8_t)(v>>16); p[3]=(uint8_t)(v>>24); }

static inline void fp_chacha_block(const uint8_t key[32],uint32_t ctr,const uint8_t nonce[12],uint8_t out[64]){
    uint32_t s[16], x[16]; s[0]=0x61707865; s[1]=0x3320646e; s[2]=0x79622d32; s[3]=0x6b206574;
    for(int i=0;i<8;i++) s[4+i]=fp_le32(key+4*i); s[12]=ctr; for(int i=0;i<3;i++) s[13+i]=fp_le32(nonce+4*i);
    memcpy(x,s,sizeof s);
    for(int i=0;i<10;i++){ FP_QR(x[0],x[4],x[8],x[12]); FP_QR(x[1],x[5],x[9],x[13]); FP_QR(x[2],x[6],x[10],x[14]); FP_QR(x[3],x[7],x[11],x[15]);
        FP_QR(x[0],x[5],x[10],x[15]); FP_QR(x[1],x[6],x[11],x[12]); FP_QR(x[2],x[7],x[8],x[13]); FP_QR(x[3],x[4],x[9],x[14]); }
    for(int i=0;i<16;i++) fp_st32(out+4*i,x[i]+s[i]); }

static inline void fp_chacha_xor(const uint8_t key[32],uint32_t ctr,const uint8_t nonce[12],uint8_t* buf,size_t n){
    uint8_t ks[64]; for(size_t o=0;o<n;o+=64){ fp_chacha_block(key,ctr++,nonce,ks); for(size_t i=0;i<64&&o+i<n;i++) buf[o+i]^=ks[i]; } }

typedef struct { uint32_t r[5], h[5], pad[4]; } fp_poly;
static inline void fp_poly_init(fp_poly* p,const uint8_t k[32]){
    p->r[0]=fp_le32(k)&0x3ffffff; p->r[1]=(fp_le32(k+3)>>2)&0x3ffff03; p->r[2]=(fp_le32(k+6)>>4)&0x3ffc0ff; p->r[3]=(fp_le32(k+9)>>6)&0x3f03fff; p->r[4]=(fp_le32(k+12)>>8)&0x00fffff;
    for(int i=0;i<5;i++) p->h[i]=0; for(int i=0;i<4;i++) p->pad[i]=fp_le32(k+16+4*i); }
static inline void fp_poly_block(fp_poly* p,const uint8_t* m,uint32_t hibit){
    uint32_t r0=p->r[0],r1=p->r[1],r2=p->r[2],r3=p->r[3],r4=p->r[4], s1=r1*5,s2=r2*5,s3=r3*5,s4=r4*5;
    uint32_t h0=p->h[0],h1=p->h[1],h2=p->h[2],h3=p->h[3],h4=p->h[4];
    h0+=fp_le32(m)&0x3ffffff; h1+=(fp_le32(m+3)>>2)&0x3ffffff; h2+=(fp_le32(m+6)>>4)&0x3ffffff; h3+=(fp_le32(m+9)>>6)&0x3ffffff; h4+=(fp_le32(m+12)>>8)|hibit;
    uint64_t d0=(uint64_t)h0*r0+(uint64_t)h1*s4+(uint64_t)h2*s3+(uint64_t)h3*s2+(uint64_t)h4*s1;
    uint64_t d1=(uint64_t)h0*r1+(uint64_t)h1*r0+(uint64_t)h2*s4+(uint64_t)h3*s3+(uint64_t)h4*s2;
    uint64_t d2=(uint64_t)h0*r2+(uint64_t)h1*r1+(uint64_t)h2*r0+(uint64_t)h3*s4+(uint64_t)h4*s3;
    uint64_t d3=(uint64_t)h0*r3+(uint64_t)h1*r2+(uint64_t)h2*r1+(uint64_t)h3*r0+(uint64_t)h4*s4;
    uint64_t d4=(uint64_t)h0*r4+(uint64_t)h1*r3+(uint64_t)h2*r2+(uint64_t)h3*r1+(uint64_t)h4*r0;
    uint32_t c=(uint32_t)(d0>>26); h0=(uint32_t)d0&0x3ffffff; d1+=c; c=(uint32_t)(d1>>26); h1=(uint32_t)d1&0x3ffffff;
    d2+=c; c=(uint32_t)(d2>>26); h2=(uint32_t)d2&0x3ffffff; d3+=c; c=(uint32_t)(d3>>26); h3=(uint32_t)d3&0x3ffffff;
    d4+=c; c=(uint32_t)(d4>>26); h4=(uint32_t)d4&0x3ffffff; h0+=c*5; c=h0>>26; h0&=0x3ffffff; h1+=c;
    p->h[0]=h0; p->h[1]=h1; p->h[2]=h2; p->h[3]=h3; p->h[4]=h4; }
static inline void fp_poly_feed(fp_poly* p,const uint8_t* m,size_t n){
    while(n>=16){ fp_poly_block(p,m,1u<<24); m+=16; n-=16; }
    if(n){ uint8_t b[16]={0}; memcpy(b,m,n); b[n]=1; fp_poly_block(p,b,0); } }
static inline void fp_poly_feed_pad(fp_poly* p,const uint8_t* m,size_t n){
    while(n>=16){ fp_poly_block(p,m,1u<<24); m+=16; n-=16; }
    if(n){ uint8_t b[16]={0}; memcpy(b,m,n); fp_poly_block(p,b,1u<<24); } }
static inline void fp_poly_final(fp_poly* p,uint8_t tag[16]){
    uint32_t h0=p->h[0],h1=p->h[1],h2=p->h[2],h3=p->h[3],h4=p->h[4],c;
    c=h1>>26; h1&=0x3ffffff; h2+=c; c=h2>>26; h2&=0x3ffffff; h3+=c; c=h3>>26; h3&=0x3ffffff; h4+=c; c=h4>>26; h4&=0x3ffffff; h0+=c*5; c=h0>>26; h0&=0x3ffffff; h1+=c;
    uint32_t g0=h0+5; c=g0>>26; g0&=0x3ffffff; uint32_t g1=h1+c; c=g1>>26; g1&=0x3ffffff; uint32_t g2=h2+c; c=g2>>26; g2&=0x3ffffff;
    uint32_t g3=h3+c; c=g3>>26; g3&=0x3ffffff; uint32_t g4=h4+c-(1u<<26);
    uint32_t mask=(g4>>31)-1; g0&=mask; g1&=mask; g2&=mask; g3&=mask; g4&=mask; mask=~mask;
    h0=(h0&mask)|g0; h1=(h1&mask)|g1; h2=(h2&mask)|g2; h3=(h3&mask)|g3; h4=(h4&mask)|g4;
    h0=h0|(h1<<26); h1=(h1>>6)|(h2<<20); h2=(h2>>12)|(h3<<14); h3=(h3>>18)|(h4<<8);
    uint64_t f=(uint64_t)h0+p->pad[0]; h0=(uint32_t)f; f=(uint64_t)h1+p->pad[1]+(f>>32); h1=(uint32_t)f;
    f=(uint64_t)h2+p->pad[2]+(f>>32); h2=(uint32_t)f; f=(uint64_t)h3+p->pad[3]+(f>>32); h3=(uint32_t)f;
    fp_st32(tag,h0); fp_st32(tag+4,h1); fp_st32(tag+8,h2); fp_st32(tag+12,h3); }

static inline void fp_aead_tag(const uint8_t key[32],const uint8_t nonce[12],const uint8_t* aad,size_t na,const uint8_t* ct,size_t nc,uint8_t tag[16]){
    uint8_t blk[64], len[16]; fp_chacha_block(key,0,nonce,blk); fp_poly p; fp_poly_init(&p,blk);
    fp_poly_feed_pad(&p,aad,na); fp_poly_feed_pad(&p,ct,nc);
    for(int i=0;i<8;i++){ len[i]=(uint8_t)((uint64_t)na>>(8*i)); len[8+i]=(uint8_t)((uint64_t)nc>>(8*i)); }
    fp_poly_feed(&p,len,16); fp_poly_final(&p,tag); memset(blk,0,sizeof blk); }

static inline void fp_seal(const uint8_t key[32],const uint8_t nonce[12],const uint8_t* aad,size_t na,uint8_t* buf,size_t n,uint8_t tag[16]){
    fp_chacha_xor(key,1,nonce,buf,n); fp_aead_tag(key,nonce,aad,na,buf,n,tag); }

static inline int fp_open(const uint8_t key[32],const uint8_t nonce[12],const uint8_t* aad,size_t na,uint8_t* buf,size_t n,const uint8_t tag[16]){
    uint8_t t[16]; fp_aead_tag(key,nonce,aad,na,buf,n,t); uint8_t d=0; for(int i=0;i<16;i++) d|=(uint8_t)(t[i]^tag[i]);
    if(d) return -1; fp_chacha_xor(key,1,nonce,buf,n); return 0; }
#endif
