#ifndef NVX_SND_RING_H
#define NVX_SND_RING_H
struct sring { u8* buf; u32 size; u64 wr, rd; u32 last; };
static inline void sring_init(struct sring* r,u8* buf,u32 size){ r->buf=buf; r->size=size; r->wr=r->rd=0; r->last=0; for(u32 i=0;i<size;i++)buf[i]=0; }
static inline void sring_hw(struct sring* r,u32 pos){
    if(pos>=r->size)return;
    u32 delta=(pos+r->size-r->last)%r->size;
    for(u32 i=0;i<delta;i++)r->buf[(r->rd+i)%r->size]=0;
    r->rd+=delta; r->last=pos;
    if(r->wr<r->rd)r->wr=r->rd;
}
static inline int sring_free(const struct sring* r){ u64 used=r->wr-r->rd; u32 guard=4096; if(used+guard>=r->size)return 0; return (int)(r->size-guard-used); }
static inline int sring_write(struct sring* r,const u8* d,int len){
    int f=sring_free(r); if(len>f)len=f; len&=~3;
    for(int i=0;i<len;i++)r->buf[(r->wr+(u64)i)%r->size]=d[i];
    r->wr+=(u64)len; return len;
}
#endif
