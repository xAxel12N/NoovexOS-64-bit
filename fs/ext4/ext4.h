#ifndef NVX_FS_EXT4_H
#define NVX_FS_EXT4_H

#ifdef EXT4_HOST
#include <stdint.h>
#include <stdlib.h>
typedef uint8_t u8; typedef uint16_t u16; typedef uint32_t u32; typedef uint64_t u64;
#define E4_ALLOC(n) calloc(1,(n))
#define E4_FREE(p) free(p)
#else
#include "include/nvx/base.h"
#include "include/nvx/mem/heap.h"
#define E4_ALLOC(n) e4_zalloc(n)
#define E4_FREE(p) kfree(p)
#endif

#define EXT4_RW_VERIFIED 1

#define E4_SB_MAGIC 0xEF53
#define E4_ROOT_INO 2
#define E4_JOURNAL_INO 8
#define E4_NAME_MAX 255
#define E4_PATH_MAX 512
#define E4_INODE_BLOCK_BYTES 60

#define E4_COMPAT_HAS_JOURNAL 0x4
#define E4_COMPAT_DIR_INDEX 0x20
#define E4_COMPAT_SPARSE_SUPER2 0x200
#define E4_INCOMPAT_FILETYPE 0x2
#define E4_INCOMPAT_RECOVER 0x4
#define E4_INCOMPAT_JOURNAL_DEV 0x8
#define E4_INCOMPAT_META_BG 0x10
#define E4_INCOMPAT_EXTENTS 0x40
#define E4_INCOMPAT_64BIT 0x80
#define E4_INCOMPAT_MMP 0x100
#define E4_INCOMPAT_FLEX_BG 0x200
#define E4_INCOMPAT_CSUM_SEED 0x2000
#define E4_INCOMPAT_LARGEDIR 0x4000
#define E4_INCOMPAT_INLINE_DATA 0x8000
#define E4_INCOMPAT_ENCRYPT 0x10000
#define E4_INCOMPAT_CASEFOLD 0x20000
#define E4_RO_SPARSE_SUPER 0x1
#define E4_RO_LARGE_FILE 0x2
#define E4_RO_HUGE_FILE 0x8
#define E4_RO_GDT_CSUM 0x10
#define E4_RO_DIR_NLINK 0x20
#define E4_RO_EXTRA_ISIZE 0x40
#define E4_RO_QUOTA 0x100
#define E4_RO_BIGALLOC 0x200
#define E4_RO_METADATA_CSUM 0x400
#define E4_RO_READONLY 0x1000
#define E4_RO_PROJECT 0x2000
#define E4_RO_VERITY 0x8000
#define E4_RO_ORPHAN_PRESENT 0x10000

#define E4_INCOMPAT_RW_OK (E4_INCOMPAT_FILETYPE|E4_INCOMPAT_RECOVER|E4_INCOMPAT_EXTENTS|E4_INCOMPAT_64BIT|E4_INCOMPAT_FLEX_BG|E4_INCOMPAT_CSUM_SEED|E4_INCOMPAT_LARGEDIR)
#define E4_INCOMPAT_RO_OK (E4_INCOMPAT_RW_OK|E4_INCOMPAT_MMP|E4_INCOMPAT_META_BG)
#define E4_ROCOMPAT_RW_OK (E4_RO_SPARSE_SUPER|E4_RO_LARGE_FILE|E4_RO_HUGE_FILE|E4_RO_GDT_CSUM|E4_RO_DIR_NLINK|E4_RO_EXTRA_ISIZE|E4_RO_METADATA_CSUM)

#define E4_BG_INODE_UNINIT 0x1
#define E4_BG_BLOCK_UNINIT 0x2
#define E4_BG_INODE_ZEROED 0x4

#define E4_IF_INDEX 0x1000
#define E4_IF_HUGE_FILE 0x40000
#define E4_IF_EXTENTS 0x80000
#define E4_IF_INLINE_DATA 0x10000000
#define E4_EXT_MAGIC 0xF30A

#define E4_S_IFMT 0170000
#define E4_S_IFDIR 0040000
#define E4_S_IFREG 0100000
#define E4_S_IFLNK 0120000

#define E4_FT_UNKNOWN 0
#define E4_FT_REG 1
#define E4_FT_DIR 2
#define E4_FT_SYMLINK 7
#define E4_FT_CSUM 0xDE

#define E4_ERR_IO -5
#define E4_ERR_NOENT -2
#define E4_ERR_EXIST -17
#define E4_ERR_NOTDIR -20
#define E4_ERR_ISDIR -21
#define E4_ERR_INVAL -22
#define E4_ERR_NOSPC -28
#define E4_ERR_ROFS -30
#define E4_ERR_NOTEMPTY -39
#define E4_ERR_NOMEM -12
#define E4_ERR_CORRUPT -117
#define E4_ERR_NAMETOOLONG -36

typedef int (*e4_rd_fn)(unsigned long long lba,int n,void* buf);
typedef int (*e4_wr_fn)(unsigned long long lba,int n,const void* buf);
typedef int (*e4_fl_fn)(void);
typedef void (*e4_ls_fn)(const char* name,int isdir,unsigned long long size,void* ctx);

typedef struct {
    u16 mode, uid, gid, links;
    u32 flags, gen, dtime;
    u64 size, blocks;
    u32 atime, ctime, mtime, crtime;
    u32 blk[15];
    u8 fresh;
    u8 raw[256];
} e4_inode;

struct e4_jnl;

typedef struct ext4_fs {
    e4_rd_fn rd; e4_wr_fn wr; e4_fl_fn fl; u64 base, nsect;
    int mounted, ro, dirty, was_clean;
    u8 sb[1024];
    u32 bsize, log_bsize, spb;
    u64 blocks_count, free_blocks, first_data_block;
    u32 inodes_count, free_inodes;
    u32 blocks_per_group, inodes_per_group, inode_size, desc_size, ngroups, gdt_blocks;
    u32 feat_compat, feat_incompat, feat_rocompat;
    int has64, csum, gdtcsum, extents, flexbg, filetype, htree;
    u32 csum_seed;
    u8* gdt;
    u8* gdt_dirty;
    u32 last_alloc_group, last_ino_group;
    u32 hash_seed[4]; u8 hash_version;
    u32 journal_ino;
    struct e4_jnl* jnl;
    int in_txn, jabort;
    char label[17];
    u8 uuid[16];
} ext4_fs;

extern ext4_fs g_ext4;

int ext4_mount(ext4_fs* fs,e4_rd_fn rd,e4_wr_fn wr,u64 base,u64 nsect,int ro);
int ext4_mount_fl(ext4_fs* fs,e4_rd_fn rd,e4_wr_fn wr,e4_fl_fn fl,u64 base,u64 nsect,int ro);
int ext4_umount(ext4_fs* fs);
int ext4_sync(ext4_fs* fs);

int ext4_bread(ext4_fs* fs,u64 blk,void* buf);
int ext4_bwrite(ext4_fs* fs,u64 blk,const void* buf);
int ext4_bwrite_meta(ext4_fs* fs,u64 blk,const void* buf);
int ext4_bread_raw(ext4_fs* fs,u64 blk,void* buf);
int ext4_bwrite_raw(ext4_fs* fs,u64 blk,const void* buf);

int ext4_iget(ext4_fs* fs,u32 ino,e4_inode* in);
int ext4_iput(ext4_fs* fs,u32 ino,e4_inode* in);
int ext4_bmap(ext4_fs* fs,e4_inode* in,u64 lblk,u64* pblk);

int ext4_lookup(ext4_fs* fs,const char* path,u32* ino);
long ext4_pread(ext4_fs* fs,u32 ino,u64 off,void* buf,long len);
long ext4_pwrite(ext4_fs* fs,u32 ino,u64 off,const void* buf,long len);
int ext4_truncate(ext4_fs* fs,u32 ino,u64 size);
int ext4_create(ext4_fs* fs,const char* path,u16 mode,u32* ino);
int ext4_mkdir_p(ext4_fs* fs,const char* path,u16 mode);
int ext4_unlink(ext4_fs* fs,const char* path);
int ext4_rmdir(ext4_fs* fs,const char* path);
int ext4_rename(ext4_fs* fs,const char* from,const char* to);
int ext4_readdir(ext4_fs* fs,const char* path,e4_ls_fn cb,void* ctx);
int ext4_stat(ext4_fs* fs,const char* path,e4_inode* in,u32* ino);
u64 ext4_free_bytes(ext4_fs* fs);
u64 ext4_total_bytes(ext4_fs* fs);
void ext4_count_free(ext4_fs* fs);
int ext4_grp_has_sb(ext4_fs* fs,u32 g);
u32 ext4_grp_sb_len(ext4_fs* fs,u32 g);
u64 ext4_gdt_loc(ext4_fs* fs,u32 i);

u32 e4_crc32c(u32 crc,const void* p,unsigned long n);
void e4_sb_csum_set(ext4_fs* fs);

int ext4_jnl_open(ext4_fs* fs);
int ext4_jnl_begin(ext4_fs* fs);
int ext4_jnl_write(ext4_fs* fs,u64 blk,const void* buf);
int ext4_jnl_end(ext4_fs* fs);
void ext4_jnl_abort(ext4_fs* fs);
void ext4_jnl_close(ext4_fs* fs);
int ext4_jnl_reload(ext4_fs* fs);

int ext4k_mount(int ro);
int ext4k_mount_disk(int disk,unsigned long long base,unsigned long long nsect,int ro);
int ext4k_mounted(void);
int ext4k_is_ro(void);
void ext4k_umount(void);
int ext4k_exists(const char* path);
int ext4k_mkdir(const char* path);
long ext4k_write(const char* path,const char* buf,long len);
long ext4k_read(const char* path,char* buf,long cap);
int ext4k_delete(const char* path);
int ext4k_ls(const char* path,e4_ls_fn cb,void* ctx);
int ext4k_rename(const char* from,const char* to);
long ext4k_size(const char* path);

static inline void e4_cpy(void* d,const void* s,unsigned long n){ u8* a=(u8*)d; const u8* b=(const u8*)s; while(n--)*a++=*b++; }
static inline void e4_set(void* d,int v,unsigned long n){ u8* a=(u8*)d; while(n--)*a++=(u8)v; }
static inline u16 e4_le16(const u8* p){ return (u16)(p[0]|(p[1]<<8)); }
static inline u32 e4_le32(const u8* p){ return (u32)p[0]|((u32)p[1]<<8)|((u32)p[2]<<16)|((u32)p[3]<<24); }
static inline u64 e4_le64(const u8* p){ return (u64)e4_le32(p)|((u64)e4_le32(p+4)<<32); }
static inline void e4_st16(u8* p,u16 v){ p[0]=(u8)v; p[1]=(u8)(v>>8); }
static inline void e4_st32(u8* p,u32 v){ p[0]=(u8)v; p[1]=(u8)(v>>8); p[2]=(u8)(v>>16); p[3]=(u8)(v>>24); }
static inline void e4_st64(u8* p,u64 v){ e4_st32(p,(u32)v); e4_st32(p+4,(u32)(v>>32)); }
#ifndef EXT4_HOST
static inline void* e4_zalloc(unsigned long n){ void* p=kmalloc(n); if(p)e4_set(p,0,n); return p; }
#endif

#endif
