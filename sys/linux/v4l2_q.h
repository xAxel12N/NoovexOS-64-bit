#ifndef NVX_V4L2_Q_H
#define NVX_V4L2_Q_H
#define V4Q_MAX 8
#define V4Q_USER 0
#define V4Q_QUEUED 1
#define V4Q_DONE 2
#define V4Q_EINVAL 22
#define V4Q_EBUSY 16
#define V4Q_EAGAIN 11
#define V4Q_EIO 5
struct v4q { int n, streaming, owner; unsigned size, seq; int st[V4Q_MAX], q[V4Q_MAX], qh, qn, d[V4Q_MAX], dh, dn; unsigned used[V4Q_MAX], bseq[V4Q_MAX]; unsigned long long ts[V4Q_MAX]; };
static inline unsigned v4q_pgsize(unsigned sz){ return (sz+0xFFFu)&~0xFFFu; }
static inline void v4q_clear(struct v4q* q){ q->qh=q->qn=q->dh=q->dn=0; for(int i=0;i<V4Q_MAX;i++){ q->st[i]=V4Q_USER; q->used[i]=0; } }
static inline int v4q_reqbufs(struct v4q* q,int n,unsigned size){
    if(q->streaming)return -V4Q_EBUSY; if(n<0)return -V4Q_EINVAL; if(n>V4Q_MAX)n=V4Q_MAX; if(n&&!size)return -V4Q_EINVAL;
    v4q_clear(q); q->n=n; q->size=n?size:0; return n; }
static inline int v4q_qbuf(struct v4q* q,int i){
    if(i<0||i>=q->n)return -V4Q_EINVAL; if(q->st[i]!=V4Q_USER)return -V4Q_EINVAL;
    q->st[i]=V4Q_QUEUED; q->q[(q->qh+q->qn)%V4Q_MAX]=i; q->qn++; return 0; }
static inline int v4q_dqbuf(struct v4q* q,int* out){
    if(!q->n)return -V4Q_EINVAL; if(!q->dn)return q->streaming?-V4Q_EAGAIN:-V4Q_EINVAL;
    int i=q->d[q->dh]; q->dh=(q->dh+1)%V4Q_MAX; q->dn--; q->st[i]=V4Q_USER; *out=i; return 0; }
static inline int v4q_take(struct v4q* q){
    if(!q->streaming||!q->qn)return -1; int i=q->q[q->qh]; q->qh=(q->qh+1)%V4Q_MAX; q->qn--; return i; }
static inline void v4q_done(struct v4q* q,int i,unsigned used,unsigned long long ts){
    if(i<0||i>=q->n)return; q->st[i]=V4Q_DONE; q->used[i]=used; q->ts[i]=ts; q->bseq[i]=q->seq++; q->d[(q->dh+q->dn)%V4Q_MAX]=i; q->dn++; }
static inline int v4q_streamon(struct v4q* q){ if(!q->n)return -V4Q_EINVAL; if(!q->streaming){ q->streaming=1; q->seq=0; } return 0; }
static inline int v4q_streamoff(struct v4q* q){ q->streaming=0; v4q_clear(q); return 0; }
static inline unsigned v4q_flags(const struct v4q* q,int i){ unsigned f=0x2000u; if(q->st[i]==V4Q_QUEUED)f|=0x2u; if(q->st[i]==V4Q_DONE)f|=0x4u; return f; }
static inline int v4q_by_offset(const struct v4q* q,unsigned long long off,unsigned long long len){
    unsigned ps=v4q_pgsize(q->size); if(!ps||off%ps)return -1; unsigned long long i=off/ps; if(i>=(unsigned long long)q->n||len>ps)return -1; return (int)i; }
static inline unsigned char v4q_clip(int v){ return (unsigned char)(v<0?0:v>255?255:v); }
static inline void v4q_rgb_yuyv(const unsigned* src,int sw,int sh,unsigned char* d,int w,int h){
    for(int y=0;y<h;y++){ const unsigned* r=src+(unsigned long)(y*sh/h)*(unsigned long)sw; unsigned char* o=d+(unsigned long)y*(unsigned long)w*2;
        for(int x=0;x+1<w;x+=2){ unsigned a=r[x*sw/w], b=r[(x+1)*sw/w];
            int r0=(a>>16)&255, g0=(a>>8)&255, b0=a&255, r1=(b>>16)&255, g1=(b>>8)&255, b1=b&255;
            int y0=((66*r0+129*g0+25*b0+128)>>8)+16, y1=((66*r1+129*g1+25*b1+128)>>8)+16;
            int ra=(r0+r1)/2, ga=(g0+g1)/2, ba=(b0+b1)/2;
            int u=((-38*ra-74*ga+112*ba+128)>>8)+128, v=((112*ra-94*ga-18*ba+128)>>8)+128;
            o[x*2]=v4q_clip(y0); o[x*2+1]=v4q_clip(u); o[x*2+2]=v4q_clip(y1); o[x*2+3]=v4q_clip(v); } } }
static inline void v4q_rgb_bgr4(const unsigned* src,int sw,int sh,unsigned char* d,int w,int h){
    for(int y=0;y<h;y++){ const unsigned* r=src+(unsigned long)(y*sh/h)*(unsigned long)sw; unsigned* o=(unsigned*)(d+(unsigned long)y*(unsigned long)w*4); for(int x=0;x<w;x++)o[x]=r[x*sw/w]&0x00FFFFFFu; } }
static inline unsigned v4q_frame_size(unsigned pix,int w,int h){ return pix==0x56595559u?(unsigned)w*(unsigned)h*2u:pix==0x34524742u?(unsigned)w*(unsigned)h*4u:(unsigned)w*(unsigned)h*2u+4096u; }
static inline unsigned v4q_bpl(unsigned pix,int w){ return pix==0x56595559u?(unsigned)w*2u:pix==0x34524742u?(unsigned)w*4u:0u; }
#endif
