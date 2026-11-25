#ifdef EXT4_HOST
#include <stdlib.h>
#define kmalloc malloc
#define kfree free
#define jklog(s) ((void)0)
#else
#include "include/nvx/mem/heap.h"
#include "include/nvx/core/klog.h"
#define jklog(s) klog(s)
#endif
#include "fs/ext4/journal.h"
#define JM 0xC03B3998u
#define JF_REVOKE 1u
#define JF_64 2u
#define JF_ASYNC 4u
#define JF_V2 8u
#define JF_V3 0x10u
#define JF_FC 0x20u
#define JT_ESC 1u
#define JT_SAME 2u
#define JT_LAST 8u
static uint32_t jcrc_tab[256]; static int jcrc_init;
uint32_t ext4_jcrc32c(uint32_t crc,const void* p,uint32_t n){
    const uint8_t* b=(const uint8_t*)p;
    if(!jcrc_init){ for(uint32_t i=0;i<256;i++){ uint32_t c=i; for(int k=0;k<8;k++) c=(c&1)?(c>>1)^0x82F63B78u:c>>1; jcrc_tab[i]=c; } jcrc_init=1; }
    while(n--) crc=jcrc_tab[(crc^*b++)&0xFF]^(crc>>8);
    return crc;
}
static uint32_t jb32(const uint8_t* p){ return ((uint32_t)p[0]<<24)|((uint32_t)p[1]<<16)|((uint32_t)p[2]<<8)|p[3]; }
static uint16_t jb16(const uint8_t* p){ return (uint16_t)((p[0]<<8)|p[1]); }
static void jw32(uint8_t* p,uint32_t v){ p[0]=(uint8_t)(v>>24); p[1]=(uint8_t)(v>>16); p[2]=(uint8_t)(v>>8); p[3]=(uint8_t)v; }
static void jw16(uint8_t* p,uint32_t v){ p[0]=(uint8_t)(v>>8); p[1]=(uint8_t)v; }
static void jzero(void* d,uint32_t n){ uint8_t* q=(uint8_t*)d; while(n--)*q++=0; }
static void jcopy(void* d,const void* s,uint32_t n){ uint8_t* a=(uint8_t*)d; const uint8_t* b=(const uint8_t*)s; while(n--)*a++=*b++; }
static uint32_t jwrap(const ext4_journal* j,uint32_t pos){ if(pos>=j->last) pos-=j->last-j->first; return pos; }
static int jrd(ext4_journal* j,uint32_t lb,void* buf){ uint64_t pb; if(lb>=j->maxlen||j->io.jmap(j->io.ctx,lb,&pb)||j->io.rd(j->io.ctx,pb,buf)) return EXT4_J_EIO; return 0; }
static int jwr(ext4_journal* j,uint32_t lb,const void* buf){ uint64_t pb; if(lb>=j->maxlen||j->io.jmap(j->io.ctx,lb,&pb)||j->io.wr(j->io.ctx,pb,buf)) return EXT4_J_EIO; return 0; }
static int jflush(ext4_journal* j){ return j->io.flush?j->io.flush(j->io.ctx):0; }
static uint32_t jsb_csum(const ext4_journal* j){ uint8_t t[4]; jcopy(t,j->sb+0xFC,4); jzero(j->sb+0xFC,4); uint32_t c=ext4_jcrc32c(0xFFFFFFFFu,j->sb,1024); jcopy(j->sb+0xFC,t,4); return c; }
static int jtail_ok(const ext4_journal* j,uint8_t* b){ uint32_t o=j->bsize-4,v=jb32(b+o); jzero(b+o,4); uint32_t c=ext4_jcrc32c(j->seed,b,j->bsize); jw32(b+o,v); return c==v; }
static void jtail_set(const ext4_journal* j,uint8_t* b){ uint32_t o=j->bsize-4; jzero(b+o,4); jw32(b+o,ext4_jcrc32c(j->seed,b,j->bsize)); }
static int jcommit_ok(const ext4_journal* j,uint8_t* b){ uint32_t v=jb32(b+16); jzero(b+16,4); uint32_t c=ext4_jcrc32c(j->seed,b,j->bsize); jw32(b+16,v); return c==v; }
static uint32_t jtag_csum(const ext4_journal* j,uint32_t tid,const uint8_t* d){ uint8_t s[4]; jw32(s,tid); return ext4_jcrc32c(ext4_jcrc32c(j->seed,s,4),d,j->bsize); }
static int jsb_write(ext4_journal* j){ jw32(j->sb+0x18,j->seq); jw32(j->sb+0x1C,j->start); if(j->csum) jw32(j->sb+0xFC,jsb_csum(j)); return jwr(j,0,j->sb); }
int ext4_jopen(ext4_journal* j,const ext4_jio* io){
    jzero(j,sizeof *j);
    if(!io||!io->rd||!io->wr||!io->jmap||io->bsize<1024||io->bsize>65536||(io->bsize&(io->bsize-1))) return EXT4_J_EINVAL;
    j->io=*io; j->bsize=io->bsize; j->maxlen=1;
    if(!(j->sb=(uint8_t*)kmalloc(j->bsize))) return EXT4_J_ENOMEM;
    int r=jrd(j,0,j->sb); if(r) goto bad;
    r=EXT4_J_EINVAL; uint32_t bt=jb32(j->sb+4);
    if(jb32(j->sb)!=JM||(bt!=3&&bt!=4)||jb32(j->sb+0xC)!=j->bsize) goto bad;
    j->ver=bt==4?2:1; j->maxlen=jb32(j->sb+0x10); j->first=jb32(j->sb+0x14); j->seq=jb32(j->sb+0x18); j->start=jb32(j->sb+0x1C); j->errno_=jb32(j->sb+0x20);
    if(j->ver==2){ j->fcompat=jb32(j->sb+0x24); j->fincompat=jb32(j->sb+0x28); j->frocompat=jb32(j->sb+0x2C); }
    jcopy(j->uuid,j->sb+0x30,16);
    if(j->first<1||j->first>=j->maxlen||j->maxlen<8) goto bad;
    if(j->fincompat&~(JF_REVOKE|JF_64|JF_ASYNC|JF_V2|JF_V3|JF_FC)){ r=EXT4_J_ENOTSUP; goto bad; }
    if((j->fincompat&JF_V2)&&(j->fincompat&JF_V3)) goto bad;
    j->csum=(j->fincompat&(JF_V2|JF_V3))!=0;
    if(j->csum){ if(j->sb[0x50]!=4||jb32(j->sb+0xFC)!=jsb_csum(j)){ r=EXT4_J_EBADCRC; goto bad; } j->seed=ext4_jcrc32c(0xFFFFFFFFu,j->uuid,16); }
    j->last=j->maxlen;
    if(j->fincompat&JF_FC){ uint32_t fc=jb32(j->sb+0x54); if(!fc) fc=256; if(fc>=j->maxlen-j->first) goto bad; j->last=j->maxlen-fc; }
    if(j->start&&(j->start<j->first||j->start>=j->last)) goto bad;
    if(j->fincompat&JF_V3) j->tagsz=16; else { j->tagsz=12; if(j->fincompat&JF_V2) j->tagsz+=2; if(!(j->fincompat&JF_64)) j->tagsz-=4; }
    j->ok=1; return 0;
bad: kfree(j->sb); j->sb=0; return r;
}
int ext4_jneeds_recovery(const ext4_journal* j){ return j->ok&&j->start!=0; }
typedef struct { uint64_t* k; uint32_t* t; uint32_t cap,n; } jrv;
static int jrv_put(jrv* v,uint64_t blk,uint32_t tid){
    if(2*(v->n+1)>v->cap){
        uint32_t nc=v->cap?v->cap*2:1024; uint64_t* nk=(uint64_t*)kmalloc((unsigned long)nc*8); uint32_t* nt=(uint32_t*)kmalloc((unsigned long)nc*4);
        if(!nk||!nt){ if(nk)kfree(nk); if(nt)kfree(nt); return EXT4_J_ENOMEM; }
        jzero(nk,nc*8);
        for(uint32_t i=0;i<v->cap;i++) if(v->k[i]){ uint32_t h=(uint32_t)((v->k[i]*0x9E3779B97F4A7C15ull)>>32)&(nc-1); while(nk[h]) h=(h+1)&(nc-1); nk[h]=v->k[i]; nt[h]=v->t[i]; }
        if(v->k){ kfree(v->k); kfree(v->t); } v->k=nk; v->t=nt; v->cap=nc;
    }
    uint64_t key=blk+1; uint32_t h=(uint32_t)((key*0x9E3779B97F4A7C15ull)>>32)&(v->cap-1);
    while(v->k[h]&&v->k[h]!=key) h=(h+1)&(v->cap-1);
    if(!v->k[h]){ v->k[h]=key; v->t[h]=tid; v->n++; } else if((int32_t)(tid-v->t[h])>0) v->t[h]=tid;
    return 0;
}
static int jrv_has(const jrv* v,uint64_t blk,uint32_t tid){
    if(!v->cap) return 0;
    uint64_t key=blk+1; uint32_t h=(uint32_t)((key*0x9E3779B97F4A7C15ull)>>32)&(v->cap-1);
    while(v->k[h]){ if(v->k[h]==key) return (int32_t)(tid-v->t[h])<=0; h=(h+1)&(v->cap-1); }
    return 0;
}
static int jpass(ext4_journal* j,int pass,uint32_t* endtid,jrv* rv,uint8_t* b,uint8_t* d){
    uint32_t tid=j->seq,pos=j->start,used=0,span=j->last-j->first,lim=j->bsize-(j->csum?4:0);
    int r=0;
    while(used<span){
        if(pass&&tid==*endtid) break;
        if((r=jrd(j,pos,b))) return r;
        if(jb32(b)!=JM||jb32(b+8)!=tid) break;
        uint32_t ty=jb32(b+4); pos=jwrap(j,pos+1); used++;
        if(ty==1){
            if(j->csum&&!jtail_ok(j,b)){ if(pass) j->bad++; break; }
            uint32_t off=12; int stop=0;
            while(off+j->tagsz<=lim&&used<span&&!stop){
                const uint8_t* tg=b+off; uint32_t fl=(j->fincompat&JF_V3)?jb32(tg+4):jb16(tg+6);
                uint64_t bn=jb32(tg); if(j->fincompat&JF_64) bn|=(uint64_t)jb32(tg+8)<<32;
                off+=j->tagsz; if(!(fl&JT_SAME)) off+=16;
                if(off>lim) break;
                if(pass==2){
                    if((r=jrd(j,pos,d))) return r;
                    int ok=1;
                    if(j->csum){ uint32_t c=jtag_csum(j,tid,d); ok=(j->fincompat&JF_V3)?jb32(tg+12)==c:jb16(tg+4)==(c&0xFFFF); }
                    if(!ok||(j->io.nblocks&&bn>=j->io.nblocks)){ j->bad++; j->skipped++; }
                    else if(jrv_has(rv,bn,tid)) j->revoked++;
                    else { if(fl&JT_ESC) jw32(d,JM); if((r=j->io.wr(j->io.ctx,bn,d))) return EXT4_J_EIO; j->replayed++; }
                }
                pos=jwrap(j,pos+1); used++;
                if(fl&JT_LAST) stop=1;
            }
            continue;
        }
        if(ty==2){ if(!pass&&j->csum&&!jcommit_ok(j,b)) break; tid++; continue; }
        if(ty==5){
            if(j->csum&&!jtail_ok(j,b)){ if(pass) j->bad++; break; }
            if(pass==1){
                uint32_t cnt=jb32(b+12),rs=(j->fincompat&JF_64)?8:4;
                if(cnt>lim){ j->bad++; continue; }
                for(uint32_t o=16;o+rs<=cnt;o+=rs){ uint64_t bn=rs==8?((uint64_t)jb32(b+o)<<32)|jb32(b+o+4):jb32(b+o); if((r=jrv_put(rv,bn,tid))) return r; }
            }
            continue;
        }
        break;
    }
    if(!pass) *endtid=tid;
    return 0;
}
int ext4_jreplay(ext4_journal* j){
    if(!j->ok||j->intx) return EXT4_J_EINVAL;
    if(!j->start) return 0;
    if(j->fincompat&JF_FC) return EXT4_J_ENOTSUP;
    uint8_t* b=(uint8_t*)kmalloc(j->bsize); uint8_t* d=(uint8_t*)kmalloc(j->bsize); jrv rv; jzero(&rv,sizeof rv);
    int r=EXT4_J_ENOMEM; uint32_t end=0;
    if(!b||!d) goto out;
    j->replayed=j->revoked=j->skipped=0;
    if((r=jpass(j,0,&end,&rv,b,d))||(r=jpass(j,1,&end,&rv,b,d))||(r=jpass(j,2,&end,&rv,b,d))) goto out;
    if((r=jflush(j))) goto out;
    j->lasttid=end; j->seq=end+1; j->start=0;
    if((r=jsb_write(j))||(r=jflush(j))) goto out;
out:
    if(b){ kfree(b); } if(d){ kfree(d); } if(rv.k){ kfree(rv.k); kfree(rv.t); }
    if(r) j->ok=0;
    return r;
}
static uint32_t jper(const ext4_journal* j){ return (j->bsize-12-(j->csum?4:0)-16)/j->tagsz; }
int ext4_jtx_begin(ext4_journal* j){
    if(!j->ok||j->intx) return EXT4_J_EINVAL;
    if(j->errno_||j->start||(j->fincompat&JF_FC)) return EXT4_J_ENOTSUP;
    uint32_t per=jper(j),avail=j->last-j->first-1;
    uint64_t n=(uint64_t)avail*per/(per+1); if(n>512) n=512; if(n<1) return EXT4_J_ENOSPC;
    if(!(j->tx=(ext4_jtx_ent*)kmalloc((unsigned long)n*sizeof(ext4_jtx_ent)))) return EXT4_J_ENOMEM;
    j->captx=(int)n; j->ntx=0; j->intx=1; return 0;
}
int ext4_jtx_room(const ext4_journal* j){ return j->intx?j->captx-j->ntx:0; }
int ext4_jtx_add(ext4_journal* j,uint64_t blk,const void* data){
    if(!j->intx||(j->io.nblocks&&blk>=j->io.nblocks)||(!(j->fincompat&JF_64)&&(blk>>32))) return EXT4_J_EINVAL;
    for(int i=0;i<j->ntx;i++) if(j->tx[i].blk==blk){ jcopy(j->tx[i].data,data,j->bsize); return 0; }
    if(j->ntx>=j->captx) return EXT4_J_ENOSPC;
    uint8_t* c=(uint8_t*)kmalloc(j->bsize); if(!c) return EXT4_J_ENOMEM;
    jcopy(c,data,j->bsize); j->tx[j->ntx].blk=blk; j->tx[j->ntx].data=c; j->ntx++; return 0;
}
void ext4_jtx_abort(ext4_journal* j){
    if(!j->intx) return;
    for(int i=0;i<j->ntx;i++) kfree(j->tx[i].data);
    kfree(j->tx); j->tx=0; j->ntx=j->captx=0; j->intx=0;
}
int ext4_jtx_commit(ext4_journal* j){
    if(!j->intx) return EXT4_J_EINVAL;
    if(!j->ntx){ ext4_jtx_abort(j); return 0; }
    uint8_t* b=(uint8_t*)kmalloc(j->bsize); uint8_t* d=(uint8_t*)kmalloc(j->bsize);
    int r=EXT4_J_ENOMEM,i=0; uint32_t pos=j->first,lim=j->bsize-(j->csum?4:0),tid=j->seq;
    if(!b||!d) goto fail;
    j->start=j->first;
    if((r=jsb_write(j))||(r=jflush(j))) goto fail;
    if(j->io.recover&&((r=j->io.recover(j->io.ctx,1))||(r=jflush(j)))) goto fail;
    while(i<j->ntx){
        jzero(b,j->bsize); jw32(b,JM); jw32(b+4,1); jw32(b+8,tid);
        uint32_t off=12; int s=i;
        while(i<j->ntx&&off+j->tagsz+(i==s?16:0)<=lim) off+=j->tagsz+(i==s?16:0), i++;
        off=12;
        for(int k=s;k<i;k++){
            uint8_t* tg=b+off; const uint8_t* src=j->tx[k].data; uint32_t fl=(k==s?0:JT_SAME)|(k==i-1?JT_LAST:0);
            jcopy(d,src,j->bsize); if(jb32(d)==JM){ fl|=JT_ESC; jzero(d,4); }
            uint32_t c=j->csum?jtag_csum(j,tid,d):0;
            jw32(tg,(uint32_t)j->tx[k].blk);
            if(j->fincompat&JF_V3){ jw32(tg+4,fl); jw32(tg+8,(uint32_t)(j->tx[k].blk>>32)); jw32(tg+12,c); }
            else { jw16(tg+4,c&0xFFFF); jw16(tg+6,fl); if(j->fincompat&JF_64) jw32(tg+8,(uint32_t)(j->tx[k].blk>>32)); }
            off+=j->tagsz; if(k==s){ jcopy(b+off,j->uuid,16); off+=16; }
            if((r=jwr(j,pos+1+(uint32_t)(k-s),d))) goto fail;
        }
        if(j->csum) jtail_set(j,b);
        if((r=jwr(j,pos,b))) goto fail;
        pos+=1+(uint32_t)(i-s);
        if(pos>=j->last){ r=EXT4_J_ENOSPC; goto fail; }
    }
    if((r=jflush(j))) goto fail;
    jzero(b,j->bsize); jw32(b,JM); jw32(b+4,2); jw32(b+8,tid);
    if(j->csum){ b[12]=4; b[13]=4; jw32(b+16,ext4_jcrc32c(j->seed,b,j->bsize)); }
    if((r=jwr(j,pos,b))||(r=jflush(j))) goto fail;
    for(int k=0;k<j->ntx;k++) if(j->io.wr(j->io.ctx,j->tx[k].blk,j->tx[k].data)){ r=EXT4_J_EIO; j->ok=0; goto out; }
    if((r=jflush(j))){ j->ok=0; goto out; }
    j->seq=tid+1; j->start=0;
    if((r=jsb_write(j))||(r=jflush(j))){ j->ok=0; goto out; }
    if(j->io.recover&&((r=j->io.recover(j->io.ctx,0))||(r=jflush(j)))){ j->ok=0; goto out; }
    goto out;
fail:
    j->ok=0;
out:
    if(b){ kfree(b); } if(d){ kfree(d); }
    ext4_jtx_abort(j);
    return r;
}
void ext4_jclose(ext4_journal* j){ ext4_jtx_abort(j); if(j->sb) kfree(j->sb); j->sb=0; j->ok=0; }
#include "fs/ext4/ext4.h"
struct e4_jnl { ext4_journal j; e4_inode ji; ext4_fs* fs; };
static int gj_rd(void* c,uint64_t b,void* buf){ return ext4_bread_raw(((struct e4_jnl*)c)->fs,b,buf)?EXT4_J_EIO:0; }
static int gj_wr(void* c,uint64_t b,const void* buf){ ext4_fs* fs=((struct e4_jnl*)c)->fs; if(!fs->wr||b>=fs->blocks_count) return EXT4_J_EIO; return fs->wr(fs->base+b*fs->spb,(int)fs->spb,buf)?EXT4_J_EIO:0; }
static int gj_fl(void* c){ ext4_fs* fs=((struct e4_jnl*)c)->fs; return fs->fl&&fs->fl()?EXT4_J_EIO:0; }
static int gj_map(void* c,uint32_t l,uint64_t* p){ struct e4_jnl* x=(struct e4_jnl*)c; u64 pb=0; if(ext4_bmap(x->fs,&x->ji,l,&pb)||!pb||pb>=x->fs->blocks_count) return EXT4_J_EIO; *p=pb; return 0; }
static int gj_sbw(ext4_fs* fs){
    u8* b=(u8*)E4_ALLOC(fs->bsize); if(!b) return EXT4_J_ENOMEM;
    u64 sbb=fs->bsize==1024?1:0; u32 so=fs->bsize==1024?0:1024; int r=gj_rd(fs->jnl,sbb,b);
    if(!r){ e4_cpy(b+so,fs->sb,1024); r=gj_wr(fs->jnl,sbb,b); }
    E4_FREE(b); return r;
}
static int gj_rec(void* c,int on){
    ext4_fs* fs=((struct e4_jnl*)c)->fs; u32 inc=e4_le32(fs->sb+0x60); inc=on?(inc|E4_INCOMPAT_RECOVER):(inc&~(u32)E4_INCOMPAT_RECOVER);
    e4_st32(fs->sb+0x60,inc); fs->feat_incompat=inc; e4_sb_csum_set(fs); return gj_sbw(fs);
}
static int gj_reload(ext4_fs* fs){
    u8* b=(u8*)E4_ALLOC(fs->bsize); if(!b) return EXT4_J_ENOMEM;
    u64 sbb=fs->bsize==1024?1:0; u32 so=fs->bsize==1024?0:1024; int r=ext4_bread_raw(fs,sbb,b);
    if(!r){ e4_cpy(fs->sb,b+so,1024); if(e4_le32(fs->sb+0x38)!=0xEF53u||(fs->csum&&e4_crc32c(~0u,fs->sb,0x3FC)!=e4_le32(fs->sb+0x3FC))) r=EXT4_J_EBADCRC; }
    E4_FREE(b); if(r) return r;
    fs->free_blocks=e4_le32(fs->sb+0xC)|(fs->has64?(u64)e4_le32(fs->sb+0x158)<<32:0); fs->free_inodes=e4_le32(fs->sb+0x10); fs->was_clean=e4_le16(fs->sb+0x3A)&1;
    for(u32 i=0;i<fs->gdt_blocks;i++) if(ext4_bread_raw(fs,ext4_gdt_loc(fs,i),fs->gdt+(u64)i*fs->bsize)) return EXT4_J_EIO;
    ext4_count_free(fs); return 0;
}
int ext4_jnl_reload(ext4_fs* fs){ int w=fs->was_clean; int r=gj_reload(fs); fs->was_clean=w; if(!r){ e4_set(fs->gdt_dirty,0,fs->gdt_blocks+1); fs->dirty=0; } return r; }
int ext4_jnl_open(ext4_fs* fs){
    fs->jnl=0; fs->in_txn=0;
    if(!fs->journal_ino) return 0;
    struct e4_jnl* x=(struct e4_jnl*)E4_ALLOC(sizeof *x); if(!x) return EXT4_J_ENOMEM;
    x->fs=fs; fs->jnl=x;
    if(ext4_iget(fs,fs->journal_ino,&x->ji)){ fs->ro=1; return 0; }
    ext4_jio io={x,fs->bsize,fs->blocks_count,gj_rd,gj_wr,gj_fl,gj_map,gj_rec};
    if(ext4_jopen(&x->j,&io)){ fs->ro=1; return 0; }
    int dirty=ext4_jneeds_recovery(&x->j)||(fs->feat_incompat&E4_INCOMPAT_RECOVER);
    if(dirty&&fs->wr&&!fs->ro){
        if(ext4_jneeds_recovery(&x->j)&&ext4_jreplay(&x->j)){ fs->ro=1; return 0; }
        if(x->j.bad){ jklog("ext4: journal replay skipped bad blocks, read-only, run e2fsck"); fs->ro=1; return 0; }
        if(gj_reload(fs)||gj_rec(x,0)){ fs->ro=1; x->j.ok=0; return 0; }
    }
    else if(dirty){ jklog("ext4: journal needs recovery, mounted read-only with stale view"); fs->ro=1; }
    if(x->j.errno_||!x->j.ok) fs->ro=1;
    return 0;
}
int ext4_jnl_begin(ext4_fs* fs){
    if(!fs->jnl){ fs->in_txn=0; return 0; }
    if(fs->in_txn) return 0;
    int r=ext4_jtx_begin(&fs->jnl->j); if(r){ fs->ro=1; return E4_ERR_IO; }
    fs->in_txn=1; return 0;
}
int ext4_jnl_write(ext4_fs* fs,u64 blk,const void* buf){
    if(!fs->jnl||!fs->in_txn) return ext4_bwrite_raw(fs,blk,buf);
    ext4_journal* j=&fs->jnl->j; int r=ext4_jtx_add(j,blk,buf);
    if(r==EXT4_J_ENOSPC){ ext4_jtx_abort(j); fs->in_txn=0; fs->jabort=1; return E4_ERR_NOSPC; }
    if(r){ ext4_jtx_abort(j); fs->in_txn=0; fs->ro=1; return E4_ERR_IO; }
    return 0;
}
int ext4_jnl_end(ext4_fs* fs){
    if(!fs->jnl||!fs->in_txn) return 0;
    fs->in_txn=0;
    if(ext4_jtx_commit(&fs->jnl->j)){ fs->ro=1; return E4_ERR_IO; }
    return 0;
}
void ext4_jnl_abort(ext4_fs* fs){ if(fs->jnl&&fs->in_txn) ext4_jtx_abort(&fs->jnl->j); fs->in_txn=0; }
void ext4_jnl_close(ext4_fs* fs){
    if(!fs->jnl) return;
    if(fs->in_txn) ext4_jtx_abort(&fs->jnl->j);
    ext4_jclose(&fs->jnl->j); E4_FREE(fs->jnl); fs->jnl=0; fs->in_txn=0;
}
int ext4_jnl_peek(ext4_fs* fs,u64 blk,void* buf){
    if(!fs->jnl||!fs->in_txn) return 0;
    ext4_journal* j=&fs->jnl->j;
    for(int i=0;i<j->ntx;i++) if(j->tx[i].blk==blk){ e4_cpy(buf,j->tx[i].data,j->bsize); return 1; }
    return 0;
}
