#ifndef NVX_EXT4_JOURNAL_H
#define NVX_EXT4_JOURNAL_H
#include <stdint.h>
#define EXT4_J_OK 0
#define EXT4_J_EIO (-5)
#define EXT4_J_ENOMEM (-12)
#define EXT4_J_EINVAL (-22)
#define EXT4_J_ENOSPC (-28)
#define EXT4_J_EBADCRC (-74)
#define EXT4_J_ENOTSUP (-95)
typedef struct ext4_jio {
    void* ctx; uint32_t bsize; uint64_t nblocks;
    int (*rd)(void* ctx,uint64_t blk,void* buf);
    int (*wr)(void* ctx,uint64_t blk,const void* buf);
    int (*flush)(void* ctx);
    int (*jmap)(void* ctx,uint32_t lblk,uint64_t* pblk);
    int (*recover)(void* ctx,int on);
} ext4_jio;
typedef struct ext4_jtx_ent { uint64_t blk; uint8_t* data; } ext4_jtx_ent;
typedef struct ext4_journal {
    ext4_jio io; uint32_t bsize,maxlen,first,last,seq,start,fcompat,fincompat,frocompat,seed,tagsz,errno_,ver;
    int csum,ok,intx,ntx,captx,bad; uint8_t uuid[16]; uint8_t* sb;
    ext4_jtx_ent* tx; uint32_t replayed,revoked,skipped,lasttid;
} ext4_journal;
int ext4_jopen(ext4_journal* j,const ext4_jio* io);
int ext4_jneeds_recovery(const ext4_journal* j);
int ext4_jreplay(ext4_journal* j);
int ext4_jtx_begin(ext4_journal* j);
int ext4_jtx_add(ext4_journal* j,uint64_t blk,const void* data);
int ext4_jtx_commit(ext4_journal* j);
void ext4_jtx_abort(ext4_journal* j);
int ext4_jtx_room(const ext4_journal* j);
void ext4_jclose(ext4_journal* j);
struct ext4_fs;
int ext4_jnl_peek(struct ext4_fs* fs,uint64_t blk,void* buf);
uint32_t ext4_jcrc32c(uint32_t crc,const void* p,uint32_t n);
#endif
