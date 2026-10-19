# Part 7: File systems

NoovexOS 64 has no block cache layer under a generic inode cache the way Linux does. Every file the system knows about lives in one fixed in-memory table, `FS[]`, and every directory in a second table, `DT[]`. The on-disk formats (NVX4, or NTFS/FAT when installed that way) are back ends that fill those tables at mount, load file contents on demand, and get the changes written back. The rest of the kernel, the shell, the desktop apps and the Linux system-call layer all work on `FS[]`/`DT[]` through a small set of helpers.

Every file here is its own compilation unit with a header under `include/nvx/fs/`. Before v8.96, `fs/vfs/unixfs.c` included `fs/nvx/nvx4.c` and `fs/ntfs/ntfsk.c` included `fs/fat/vfat.c`; they are now separate units too.

```
              shell / desktop apps / core/linux.c (Linux syscalls)
                                  |
        +-------------------------+-----------------------------+
        | vfs.c: vfs_open/read/write/lseek/close, procfs, /dev   |
        | unixfs.c: lookup, links, symlinks, permissions, stat   |
        | dirtree.c: DT[] tree, path resolution, rename/unlink   |
        +-------------------------+-----------------------------+
                                  |
              ramfs.c: FS[1024] in-memory files (heap buffers)
                                  |
        +-------------------------+-----------------------------+
        | NVX4 (nvx4.c)           |  rootfs: NTFS / FAT32 / FAT16 |
        | partition 0x7F or       |  (ntfsk.c + rfs_ops,           |
        | scratch disk @LBA 2048  |   ntfs-3g bridge, vfat.c)      |
        +-------------------------+-----------------------------+
                         gdisk_* (AHCI/NVMe) or ata_* (legacy IDE)
```

---

## 7.1 The in-memory file table (`fs/vfs/ramfs.c`)

### Constants

| Symbol | Value | Meaning |
|---|---|---|
| `FS_N` | 1024 | Number of file slots (`FS[FS_N]`). The slot index is also the file's "inode" for every back end. |
| `FS_CAP` | `0x40000000` (1 GiB) | Largest file. `fs_reserve` refuses `need > FS_CAP`, and writers clamp to `FS_CAP-1`. |
| `FS_OLDCAP` | 2097152 (2 MiB) | The old fixed per-file size. Now only used as the read buffer size when converting NVX3 disks. |

`ramfs.c` also defines the Unix mode bits taken from V6 `ino.h` (`S_IFMT`, `S_IFDIR 0040000`, `S_IFREG 0100000`, `S_IFCHR 0020000`, `S_ISUID`, `S_ISGID`, `S_IRUSR`...). `S_IFLNK 0120000` is defined in `unixfs.c`.

### `FsFile`

```c
typedef struct{ char name[64]; char* data; int len; int cap; int used; int folder; int dir;
    unsigned int mode; int uid, gid; u32 nlink, mtime, ctime, atime;
    u8 lazy, clean, orphan, pad; int dlo, dhi; u64 tick; } FsFile;
static FsFile FS[FS_N];
```

| Field | Meaning |
|---|---|
| `name[64]` | Leaf name (up to 63 characters plus NUL). An empty name is used for orphans. |
| `data`, `cap` | Heap buffer from `kmalloc` and its size. `data` may be `NULL` while `len > 0`, for a file that has not been loaded yet or has been evicted. The buffer always keeps one spare byte so `data[len]` can be `0`. |
| `len` | Logical file size in bytes. |
| `used` | Slot is in use. |
| `folder` | Legacy category index used by the file explorer (`apps/system/file_explorer.c` filters on `FS[k].folder`). Not part of the directory tree. |
| `dir` | Index of the parent directory in `DT[]`. `-2` marks an orphan (see 7.3). |
| `mode`, `uid`, `gid`, `nlink`, `mtime`, `ctime`, `atime` | Unix metadata. Times are seconds since the epoch from `ux_now()` (= `k_realtime_ms()/1000`). |
| `lazy` | The contents are on disk but not in memory. The next `fs_ensure` loads them. |
| `clean` | The memory copy equals the on-disk copy. |
| `orphan` | Unlinked but still open (see `fs_slot_freed`). |
| `dlo`, `dhi` | The dirty byte range `[dlo, dhi)` when `clean == 0`. `0x7FFFFFFF` as `dhi` means "to the end". |
| `tick` | Value of the global `g_fs_tick` at the last `fs_ensure`, used as an LRU clock for eviction. |

There are two separate "dirty" concepts:

* `FS[k].clean` / `dlo` / `dhi`: the memory copy differs from disk, and which bytes do. The back ends use this to decide what to write.
* `g_fs_dirty[k]` (a `u8[FS_N]`): the file was written through a descriptor since it was opened. `vf_unref` uses it on the last close to schedule background writeback (7.1.4).

### Buffer management

* `fs_reserve(k, need)` makes sure `cap > need`. It grows the buffer by 1.5x (starting at 64 bytes), at least to `need+1`, at most to `FS_CAP+1`, rounded up to 64 bytes. If that allocation fails it retries with an exact size. The old contents are copied and `data[len] = 0`.
* `fs_alloc(k, n)` wraps `kmalloc`. On failure it calls `fs_evict(n+4096, k)` and tries once more.
* `fs_ensure(k)` is the "make it resident" call. It bumps `FS[k].tick = ++g_fs_tick`. If the file is `lazy` it calls `fs_backend_load(k)` and marks the file `clean`. If there is still no buffer it reserves `len` bytes and zero-fills them.
* `fsd(k)` returns `FS[k].data` after `fs_ensure`, or the static 64 KiB `g_fs_zero` buffer if loading fails, so callers never get a NULL pointer.
* `fs_dirty(k, lo, hi)` widens the dirty range, or starts a new one if the file was clean, and clears `clean`.
* `fs_wr(k, need)` ensures and reserves, then marks the whole file dirty. `fs_wr_range(k, lo, hi)` marks only `[lo, hi)` dirty. `vfs_write` uses the range form, so a small write into a large file only dirties a few blocks.
* `fs_set(k, src, n)` replaces the contents. If the old buffer is much larger than needed (`cap > n + cap/2 + 64K`) it is freed first. `src == NULL` means zero-fill. The whole file is marked dirty and `lazy` is cleared.
* `fs_truncate_to(k, n)`: when `n == 0`, a buffer larger than 64 KiB is freed outright, without loading the file. Otherwise the file is loaded, grown or shrunk, the gap is zero-filled, and the range from `min(n, old len)` to the end is marked dirty.
* `fs_free(k)` releases the buffer and resets `lazy`, `clean`, `dlo`, `dhi` and `orphan`. It does not clear `used`.

### Lazy loading and eviction

At mount, the back ends fill only metadata and set `lazy = len > 0` and `clean = 1` (`n4_mount`, `nk_load_tree`). `fs_backend_load(k)` (in `nvx4.c`) allocates `len` bytes and reads the file with `nk_root_read` when `g_rootfs` is set, or otherwise with `n4_read_file`.

```c
static long fs_evict(long want,int keep){
    long got=0;
    for(int pass=0;pass<2&&got<want;pass++)
    for(int k=0;k<FS_N&&got<want;k++){
        if(k==keep||!FS[k].used||!FS[k].data||FS[k].lazy||!FS[k].clean)continue;
        if(FS[k].tick+(pass==0?64u:8u)>g_fs_tick)continue;
        if(fs_is_open(k)||!fs_reloadable(k))continue;
        got+=FS[k].cap; kfree(FS[k].data); FS[k].data=0; FS[k].cap=0; FS[k].lazy=1; }
    return got;
}
```

A file can be evicted only if all of these hold:

* it is resident and clean;
* it is not the file being grown;
* it has not been used in the last 64 `fs_ensure` calls (8 on the second, more aggressive pass);
* it is not open in any `g_vf` entry;
* `fs_reloadable` says it can be read back.

`fs_reloadable` returns false for orphans, when there is no disk, and for anything under `/tmp`. On NVX4 it also requires that the extents already cover `len` and that no indirect block is pending (`!g_n4indd[k]`).

### Background writeback

```c
static volatile int g_fs_wb_pending; static u64 g_fs_wb_due;
static void fs_wb_mark(int k){ (void)k; if(!g_fs_wb_pending){ g_fs_wb_pending=1; g_fs_wb_due=g_ticks+100; } }
```

When the last reference to a file `vfile` goes away (`vf_unref` in `core/proc/jobs.c`) and `g_fs_dirty[k]` is set, the flag is cleared and `fs_wb_mark` is called. Then `fs_orphan_close(k)` runs. The kernel main loop (`core/init/main.c`) checks `if(g_fs_wb_pending&&g_ticks>=g_fs_wb_due)nvx3_sync_all();`, so a burst of closes becomes one commit about 100 ticks (one second at 100 Hz) later. `nvx3_sync_all` and `nvx3_flush_meta` both clear `g_fs_wb_pending`.

Synchronous paths bypass the timer:

* `fs_sync(k)` to `nvx3_sync(k)` to `n4_sync_file` + `n4_commit`. This is used by `k_fsync`, `k_truncate_idx`, `k_rename`, `ux_mkfile`, `ux_symlink`, package extraction and `fs_save_bin` in `sys/native/syscall.c`.
* `nvx3_flush_meta()` commits metadata after link, unlink, chmod, chown and utime.

### File descriptors (declared in `ramfs.c`)

```c
#define VMAXOPEN 1024
#define FDMAX    256
struct vfile { int used, fs_idx, off, flags, dev, ref; const vfs_fs_ops* vfs; char vpath[64]; };
static struct vfile g_vf[VMAXOPEN];
```

There is one global table of open files, `g_vf`. Per-process descriptor tables come from `fdt()`, and `fdce()` holds the close-on-exec flags. Both are defined with the process code. A descriptor slot holds `-1` when free and `-2` for the kernel's own stdin/stdout/stderr. `vfile.dev` selects the kind of object:

| `dev` | Object |
|---|---|
| 0 | regular file `FS[fs_idx]` |
| 1 `/dev/null`, 2 `/dev/zero`, 3 `/dev/random` and `/dev/urandom`, 4 `/dev/full`, 5 `/dev/tty` and `/dev/console` | character devices (`dev_id`) |
| 6 / 7 | pipe read end / write end (`jc_pipe_read` / `jc_pipe_write`) |
| 9 `/dev/fb0`, 10 `/dev/input/mouse` and `/dev/input/mice`, 12 `/dev/dsp` and `/dev/audio`, 13 `/dev/kmsg` | devices handled by `ux_dev_read`/`ux_dev_write`, `snd_dev_write` and `klog` |
| 11 | open directory (`fs_idx` = `DT` index). `dt_busy` checks it. |
| 100 | a file served by a registered `vfs_fs_ops` (procfs, usbfs). The path is kept in `vpath`. |

`fs_find_name(nm)` is a legacy flat lookup by leaf name only.

---

## 7.2 The directory tree (`fs/vfs/dirtree.c`)

```c
#define DN_MAX 512
typedef struct { char name[64]; int parent; int used; unsigned int mode; int uid,gid; u32 mtime, ctime, atime; } DNode;
static DNode DT[DN_MAX];
```

`DT[0]` is always the root, with name `"/"` and `parent = -1`. A file belongs to directory `FS[k].dir`. A directory belongs to `DT[i].parent`. Nothing on disk is kept as directory entries: listing a directory means scanning `DT[]`, `FS[]` and the hard-link table for matching parents (`k_readdir_at`, then `k_readdir_extra`).

| Function | Behaviour |
|---|---|
| `dt_init()` | Runs once (`g_dtree_init`). It clears the tree and creates the root. If an old NVX2/NVX3 tree was loaded (`g_nvx_have_tree`) it calls `dt_load_from_disk()`. Otherwise it creates `/home /bin /etc /downloads /tmp /dev` and sets the cwd to `/home`. |
| `dt_load_done()` | Called by `n4_mount` after it fills `DT[]` itself. It repairs the root, makes sure `/dev` exists and sets `g_dcwd` to `/home` (or `/`). |
| `dt_mkdir_at(parent, name)` | Returns an existing child of that name, or allocates a slot with mode `0040755` owned by `g_uid:g_gid`. It updates the parent's mtime and ctime. Names must be 1..63 characters. |
| `dt_child(parent, name)` | Linear search for a child directory. |
| `dt_resolve_dir(p)` | Walks an absolute or relative (from `g_dcwd`) path one component at a time, handling `.` and `..`. A component longer than 63 characters fails. Returns a `DT` index or -1. |
| `dt_path_n(node, out, cap)` | Builds an absolute path by climbing parents, at most 16 levels. `dt_path(n, o)` is a macro in `sys/native/syscall.c` that passes `sizeof(o)` as the capacity. |
| `dt_is_tmp(d)` | Climbs up to 32 levels. Returns true if the ancestor directly under `/` is named `tmp`. |
| `dt_save()` | Persists the tree: `nk_root_dt_save()` on NTFS/FAT, otherwise `nvx3_flush_meta()`. |
| `dt_dir_ok`, `dt_dir_empty`, `k_is_ancestor` | Validity, emptiness and ancestry tests. A directory is not empty while it holds files, subdirectories or hard links (`ux_links_in`). |
| `dt_busy(d)` (`core/proc/jobs.c`) | True if `d` is the current cwd, the cwd of any live process (read from `g_cpu[].dcwd` for processes running on another core), or open as a directory descriptor (`dev == 11`). |

### `k_split`

`k_split(path, &dir, base)` divides a path into its parent directory index and its leaf name. It refuses leaves longer than 63 characters and prefixes longer than 127, rather than truncating them. A bare name resolves against `g_dcwd`. `k_find_file` uses it, and as a fallback also matches a file whose `dir` no longer exists (`!dt_dir_ok`) by its full string. This is the only place a "name anywhere" match is still allowed.

### Operations

* `k_unlink(p)`: `k_find_file`, then `ux_can_unlink(dir, k)`, then `fs_drop_name(k)`.
* `k_rmdir(p)`: refuses the root, directories without `perm_dir`, ancestors of the cwd, busy directories, and non-empty ones (subdirectories, files or links). It clears `used`, touches the parent and calls `dt_save()`.
* `k_rename(from, to)`:
  1. `ux_rename_link` is tried first, in case `from` is a hard-link name (it returns -2 when it is not).
  2. For a file: the caller needs `ux_can_unlink` on the source and `perm_dir` on the destination. If `to` is an existing directory the file moves into it under its own name. A same-named subdirectory blocks the rename. An existing file or link with the target name is dropped, which needs `ux_can_unlink` on it. The file is then renamed and moved, both parents are touched, and `fs_sync` and `nvx3_flush_meta` are called.
  3. For a directory: refuses `/` and moves into its own subtree (`k_is_ancestor`). It may replace an existing empty directory that is not busy and is not the cwd. A file with the target name blocks it. On success it updates `parent` and the times, then calls `dt_save()`.
* `k_truncate(p, len)` / `k_ftruncate(fd, len)` to `k_truncate_idx`: needs write permission and `len <= FS_CAP-1`. It calls `fs_truncate_to`, `ux_touch`, then `fs_sync` when a disk is present.
* `k_fsync(fd)`: `fs_sync` on a regular file. It succeeds trivially for devices and the standard streams.

### `/tmp` is memory-only

`dt_is_tmp` is checked in every persistence path:

* `n4_sync_file` frees any blocks a `/tmp` file still holds and writes nothing.
* `n4_serialize` skips `/tmp` files and links that point into or out of `/tmp`.
* `nvx3_sync` and `nk_root_sync_file` return early.
* `fs_reloadable` refuses eviction, because the data could not be read back.

`ux_boot_layout` additionally clears every file and link in `/tmp` at boot and sets its mode to `041777`.

### Time

`k_realtime_ms()` reads the RTC once (`read_clock`), converts it with `k_days_from_civil`, and from then on adds `g_ticks*10` ms.

---

## 7.3 Unix semantics (`fs/vfs/unixfs.c`)

### Lookup and symlinks

`ux_lookup_nofollow(p, &isdir)` first tries `dt_resolve_dir` and returns a `DT` index with `isdir = 1` if that succeeds. Otherwise it splits the path and searches `FS[]`, then the link table `g_lk[]`, in the parent directory. Last, it tries the orphaned-directory fallback.

`ux_lookup(p, &isdir, follow)` adds symlink resolution. A symlink is an ordinary `FS` slot with mode `S_IFLNK|0777` whose data is the target path.

* At most 8 hops are followed.
* Paths and targets are limited to 191 characters.
* A relative target is joined to `dt_path(FS[k].dir)`.

Related calls:

* `ux_symlink(target, path)` creates the slot. The caller needs `perm_dir` on the parent and the name must not be taken (`ux_name_taken` checks files, links and directories). It stores the target with `fs_set` and syncs it.
* `ux_readlink` returns the raw target.

### Hard links: `g_lk[NLINKS]`

```c
#define NLINKS 256
struct ulink { int used, dir, ino; char name[52]; };
static struct ulink g_lk[NLINKS];
```

A file's primary name lives in `FS[k]`. Each extra name is a `g_lk` entry pointing at slot `ino`. `ux_link_count(k)` = 1 + the number of links, and it is stored in `FS[k].nlink`.

| Call | Behaviour |
|---|---|
| `ux_link(from, to)` | Source must be a file (never followed). Non-root callers who are not the owner need read and write permission on it. The target name must be free and at most 51 characters, and the caller needs `perm_dir`. |
| `ux_unlink(p)` | If `p` names a link, removes only that entry (after `ux_can_unlink`). If it is a primary name, calls `fs_drop_name(k)`. |
| `fs_drop_name(k)` | If any link points at `k`, the first one is promoted: its name and directory become the file's primary name and the link entry is freed. Otherwise the slot is released through `fs_slot_freed`. |
| `ux_rename_link(from, to)` | Moves or renames a link entry. It needs write permission on the target file. It drops an existing file or link of the target name, and refuses if a directory of that name exists or if the target is the same inode. |

### Orphaned open files

```c
static void fs_slot_freed(int k){
    ...
    for(int i=0;i<NLINKS;i++) if(g_lk[i].used&&g_lk[i].ino==k)g_lk[i].used=0;
    g_fs_dirty[k]=0;
    if(fs_is_open(k)&&fs_ensure(k)==0){ FS[k].used=1; FS[k].orphan=1; FS[k].clean=0; FS[k].name[0]=0;
        FS[k].dir=-2; FS[k].folder=-1; FS[k].nlink=0; return; }
    for(int i=0;i<VMAXOPEN;i++) if(g_vf[i].used&&g_vf[i].dev==0&&g_vf[i].fs_idx==k){ g_vf[i].dev=1; g_vf[i].fs_idx=-1; }
    FS[k].used=0; fs_free(k);
}
```

If the file is still open, it is made resident and kept as an orphan: no name, `dir = -2`, `nlink = 0`. Its descriptors keep working. The orphan is skipped by `n4_serialize`, by `nvx3_sync`, and by the commit loop. Because it is no longer in the metadata, its blocks become free at the next commit.

`fs_orphan_close(k)`, called from `vf_unref`, frees the slot once no descriptor refers to it. If the file could not be made resident, any descriptors are redirected to `/dev/null` (`dev = 1`) instead, so a reused slot can never be reached through a stale descriptor.

### Permissions

```c
static int ux_perm_ok(int k,int want){            /* want: 4=r 2=w 1=x */
    if(k<0||k>=FS_N||!FS[k].used)return 0;
    if(g_uid==0)return 1;
    unsigned m=FS[k].mode;
    if(FS[k].uid==g_uid) return (m & (want<<6)) ? 1 : 0;
    return (m & want) ? 1 : 0;
}
```

* The model has only two classes: owner and other. The group bits are not consulted. `perm_file` is an alias for `ux_perm_ok`.
* `perm_dir(d)` grants permission to create or modify entries in a directory if any of these is true: the caller is root, owns the directory, the directory is world-writable (`0002`, default mode `0040755` if unset), or `ux_is_admin(g_uid)`.
* `ux_can_unlink(d, k)`: root always may. Otherwise the caller must own `d` or `d` must be world-writable. The exception is an admin deleting their own file in someone else's directory (`FS[k].uid==g_uid && ux_is_admin`). If the sticky bit `01000` is set on `d`, a non-root caller must own either the file or the directory. When `d` is invalid the check falls back to `perm_file(k, 2)`.
* `ux_is_admin(uid)`: true for uid 0 and uid 1000, and for any user whose name (from `/etc/passwd` via `ux_uid_name`) appears in the comma-separated member list (fourth field) of the `admin:` line of `/etc/group`.
* `ux_chmod` and `ux_chown` require root or ownership. A non-root caller cannot give a file away. `ux_chmod` keeps the `S_IFMT` bits and fills in `S_IFDIR`/`S_IFREG` if they are missing. Directory changes call `dt_save()`, file changes call `nvx3_flush_meta()`.

### `ux_stat2` and inode numbers

`ux_stat2(path, u32 o[10], follow)` fills `o[] = {mode, uid, gid, nlink, size, ino, mtime, atime, ctime, o[9]}` (`o[9]` is set to 1 only for directories). Inode numbers come from disjoint ranges:

| Object | `o[5]` (inode) | Other values |
|---|---|---|
| Regular file / symlink in slot `k` | `k + 1` | `nlink` from `FS[k]` (at least 1) |
| Directory `DT[k]` | `10000 + k` | size 4096; `nlink` = 2 + number of child directories; `o[9] = 1` |
| `/dev` device | `20000 + dev_id` | mode `0020666`, nlink 1 |
| `/mnt/usb` file `fi` | `21000 + fi` | mode `0100644`, size from `ufs[fi].size` |
| Built-in program (`jc_find_prog`) under `/bin`, `/sbin`, `/usr`, or a bare name | `22000` | mode `0100755`, size = image length |

`k_readdir_at` reports type 4 for directories, 10 for symlinks and 8 for files. `k_readdir_extra` then appends:

* hard links;
* the built-in `JS_BIN` programs in `/bin`;
* `init` in `/sbin`;
* USB files in `/mnt/usb`;
* the fixed device names in `/dev` (type 2).

### Account files

`ux_boot_layout()`, run by `ux_core_start`, builds the default tree and seeds `/etc` if the files are absent (`ux_mkfile` returns an existing slot unchanged):

```
/etc  /usr/bin  /var/log  /sbin  /root  /home  /bin  /dev  /mnt/usb  /tmp (041777)
/sys/{kernel,devices,class,power}  /proc
/etc/passwd    root:x:0:0:root:/root:/bin/nsh
               user:x:1000:1000:user:/home:/bin/nsh
/etc/group     root:x:0:
               admin:x:10:user
               users:x:1000:user
/etc/hostname  noovex            (copied into g_hostname)
/etc/rc        # /etc/rc - run by init (PID 1) at boot ...
```

* `/etc/passwd` has seven colon-separated fields. `ux_pw_field` reads them, `ux_login_user` uses fields 0 (name), 2 (uid), 3 (gid) and 5 (home), and sets `g_uid`, `g_gid`, `g_dcwd`, `HOME` and `USER`.
* `ux_ensure_account(name)` renames uid 1000 to the setup account name in both `/etc/passwd` (field 0) and `/etc/group` member lists, using `ux_rewrite_names`.
* `/etc/shadow` (mode `0100600`) is written by `ux_save_localpw` as one line, `user:<hash>`. `<hash>` is the 32-bit FNV-1a of the password in lowercase hex, or `*` when there is no password. `ux_load_localpw` sets `g_has_pw` if the second field is not empty or `*`.

### `/proc`, `/sys` and synthetic `/dev` text

`proc_gen(p, out, cap)` returns generated text for each path below. `ux_read_file` consults it before the file table, which is how the shell reads these files.

| Path | Content |
|---|---|
| `/proc/cpuinfo` | Fixed text |
| `/proc/version` | Fixed text |
| `/proc/hostname` | `g_hostname` |
| `/proc/uptime` | `g_ticks/100` seconds |
| `/proc/meminfo` | Total from `g_ram_mb`; free and available = 3/4 of total |
| `/proc/filesystems` | Fixed list |
| `/proc/loadavg` | Process count field = used `FS` slots |
| `/proc/mounts` | `nvxfs /`, `proc`, `sysfs`, plus `vfat /mnt/usb` when mounted |
| `/proc/self`, `/proc/self/status` | Name `nsh`, uid, gid |
| `/dev/null` | Empty |
| `/dev/zero` | Up to 4096 zero bytes |
| `/dev/random`, `/dev/urandom` | 64 hex characters from an xorshift seeded by `g_ticks` |
| `/dev/tty`, `/dev/console` | `console` |
| `/sys/kernel/ostype` | `NoovexOS` |
| `/sys/kernel/osrelease` | `8.96` |
| `/sys/kernel/hostname` | `g_hostname` |
| `/sys/kernel/arch` | `x86_64` |
| `/sys/devices/system/cpu/online` | `0` or `0-N` (from `g_cores_online` when SMP is on) |
| `/sys/class/net/eth0/operstate` | `down` |
| `/sys/power/state` | `mem disk` |

`ux_statfs` reports a RAM-based view: `f_type 0x4e5658`, 4 KiB blocks, `g_ram_mb*256` blocks, `FS_N` inodes, `f_namelen` 255. The file explorer and Settings use `n4_usage` for real disk numbers.

---

## 7.4 The VFS layer (`fs/vfs/vfs.c`)

```c
struct vfs_fs_ops {
    const char* name;
    int  (*match)(const char* path);
    int  (*read)(const char* path,char* buf,int off,int len);
    int  (*size)(const char* path);
};
#define VFS_MAXFS 8
```

`vfs_register` appends to `g_vfs[]`, and `vfs_lookup` returns the first back end whose `match` accepts the path. `vfs_init()` registers `PROCFS`. `ux_mount` registers `USBFS` the first time `/mnt/usb` is mounted. Registered back ends are read-only.

procfs (`procfs_build`) is the descriptor-level `/proc`. It matches `/proc` and `/proc/...` and serves:

* `meminfo`
* `uptime` (in ticks)
* `version` (`NoovexOS 64 v8.95 ...`)
* `cpuinfo` (one block per online `g_cpu[]`, plus the scheduling mode)
* `fb` (width, height, 32, pitch)
* `mounts` (registered back-end names plus `rootfs`)
* an empty leaf, which lists the entries above.

The output is built into the 4 KiB `g_procbuf` on every read. This set differs from `proc_gen`, which serves the shell's `ux_read_file` path.

### `vfs_open(nm, flags)`

1. If a registered back end matches, allocate a `vfile` with `dev = 100` and copy the path.
2. Otherwise, if `dev_id(nm)` is non-zero, open a device. `g_fb0_open` and `g_mouse_open` count users of `/dev/fb0` and the mouse.
3. Otherwise resolve with `ux_lookup(..., follow=1)`. Directories are refused here; directory descriptors are opened elsewhere.
4. `O_CREAT|O_EXCL` (`0x40|0x80`) fails if the file exists.
5. If the file is missing and `O_CREAT` is set, `k_split` the path, require `perm_dir(parent)`, take the first free slot, and set mode `0100000 | (0666 & ~g_umask)` with owner `g_uid`.
6. For an existing file: reading (access mode not 1) needs `perm_file(k, 4)`. Writing or `O_TRUNC` needs `perm_file(k, 2)`.
7. `O_TRUNC` on a non-empty file to `fs_truncate_to(k, 0)`. Create and truncate both set `g_fs_dirty`.
8. The starting offset is `len` for `O_APPEND`, otherwise 0. Descriptors 0-2 are not handed out when no process is current (`g_jcur < 0`). `O_CLOEXEC` (`0x80000`) is recorded in `fdce()`.

Flag values: `O_RDONLY 0`, `O_WRONLY 1`, `O_RDWR 2`, `O_CREAT 0x40`, `O_EXCL 0x80`, `O_TRUNC 0x200`, `O_APPEND 0x400`, `O_CLOEXEC 0x80000`. The Linux layer (`sys/linux/linux.c`) also decodes `O_DIRECTORY` (`0x10000`).

### `vfs_read` / `vfs_write`

* Reads from a regular file clamp to `len - off`, call `fs_ensure`, and copy the bytes out.
* For writes:
  * `O_APPEND` moves the offset to the end first.
  * The write is clamped to `FS_CAP-1`.
  * `fs_wr_range(k, min(off, len), off+n)` marks only the touched range dirty.
  * A gap past the old end is zero-filled.
  * `len` grows, `data[len] = 0`, `ux_touch(k, 1)`, and `g_fs_dirty[k] = 1`.
* Writes never commit to disk; that happens on close, on `fsync` or `sync`, or at the next commit.

| Device | Read | Write |
|---|---|---|
| null (1) | EOF | accepted and discarded |
| zero (2) | zeros | accepted and discarded |
| random (3) | bytes from the LCG `__dev_rng*1664525+1013904223` | accepted and discarded |
| full (4) | zeros | -1 (device full) |
| tty (5) | `jc_tty_read` while a process runs, else 0 | `jc_tty_out` while a process runs, else the `g_r3out` buffer |
| fb0 (9) | raw `SCENE` pixels, `W*H*4` bytes | writes pixels and sets `g_fb0_dirty`. `vfs_lseek` supports `SEEK_END` = the frame size. |
| mouse (10) | 3-byte PS/2-style packets from the `g_mq` ring that `ux_mouse_push` fills. Returns `JS_BLOCK` when empty. | - |
| dsp (12) | - | `snd_dev_write` |
| kmsg (13) | - | split into printable lines to `klog` |

`vfs_lseek` clamps negative results to 0 and refuses offsets above `FS_CAP-1`. `vfs_close` drops the descriptor and calls `vf_unref`, which runs pipe cleanup, writeback scheduling and orphan release when the reference count reaches zero. Closing descriptors 0-2 is refused when no process is current (`g_jcur < 0`). `vfs_dup` shares the `vfile` and increments `ref`. `vfs_fstat` returns the file length.

---

## 7.5 Locating the data area (`fs/nvx/nvxfs.c`)

`disk_init()` (called from `kmain` after `fs_init`) calls `nvx_locate()`, which picks the disk interface and the base LBA `g_nvx_base`. The legacy ATA driver is used when `ata_detect` finds a drive. Otherwise the generic disk is used: `gdisk_*` for AHCI or NVMe, with `g_nvx_generic = 1` and partition LBAs scaled by `dk_ratio()` on 4 KiB-sector drives.

```
read sector 0 (MBR)
 +- valid 55AA and an entry with type 0x7F (NVX_PTYPE) and size >= 2048
 |      -> base = its start, secs = its size          "NoovexOS data partition"   (2)
 +- any other non-empty partition entry               "Disk belongs to another system - not touched" (0)
 +- sector 0 non-zero and no 55AA                     "Unrecognised disk - not touched" (0)
 +- otherwise: scratch disk, base = 2048, secs = total-2048
        read LBA 2048:
          first u32 = NVX2 / NVX3 / NVX4 magic        "Scratch disk" (2)
          all zero                                    "Blank scratch disk" (1)
          anything else                               "Unrecognised data at LBA 2048 - not touched" (0)
```

When `nvx_locate` returns 0, `disk_init` tries `nk_root_try()` (NTFS/FAT root, 7.8). Otherwise it reads the first sector at `base` and dispatches on its magic:

| First u32 at base | Format | Action |
|---|---|---|
| `0x3458564E` ("NVX4") | NVX4 | `n4_mount(g_nvx_secs)`. On failure the disk is left alone (`disk_ok = 0`) and the note is `g_n4_err`. |
| `0x3358564E` ("NVX3") | NVX3 (v8.13-v8.90) | `nvx3_try_load()`, then `dt_init()`, then convert with `n4_format`. |
| `0x4E565849` (`NVX_MAGIC`) | NVX2 (before v8.13) | `nvx_mount_old()`, read each file, then convert. |
| zero, and the location said "blank" (`how == 1`) | - | `dt_init()` then `n4_format` ("Formatted as NVX4"). |
| other | - | "Unrecognised data - not touched". |

The legacy readers `nvx_rd`/`nvx_rdn` return -1 whenever `g_rootfs` is set, so the old NVXFS code cannot touch an NTFS/FAT root.

---

## 7.6 The NVX4 on-disk format (`fs/nvx/nvx4.c`)

### Geometry

| Constant | Value |
|---|---|
| `N4_MAGIC` | `0x3458564E`, bytes `4E 56 58 34` = "NVX4" |
| `N4_MMAGIC` | `0x544D344E`, "N4MT", the metadata header magic |
| `N4_BS` / `N4_SPB` | 4096-byte blocks = 8 sectors |
| `N4_INL` | 17 extents in the inode |
| `N4_INDN` | 512 extents in one indirect block (512 x 8 bytes = 4096) |
| `N4_MAXEXT` | 529 extents per file |
| `N4_MAXBLK` | 16 Mi blocks = 64 GiB cap (`n4_layout` clamps `nblocks`) |
| `N4_METAB` | `ceil(sizeof(n4meta)/4096)` = ceil(344128/4096) = 85 blocks |

Block numbers are relative to `g_nvx_base`: `lba = g_nvx_base + blk*8` (`n4_rd`/`n4_wr`). With the fixed table sizes (`FS_N` 1024, `DN_MAX` 512, `NLINKS` 256) the layout is:

```
block 0        superblock (first 512 bytes; rest of block unused)
blocks 1..3    unused
blocks 4..88   metadata copy A   (meta_a = 4,           85 blocks)
blocks 89..173 metadata copy B   (meta_b = 4+N4_METAB)
blocks 174..   data              (data0 = 4+2*N4_METAB)  ... nblocks-1
```

`n4_layout` refuses a partition smaller than `data0 + 64` blocks ("partition too small").

### Superblock `n4super` (512 bytes, at LBA `g_nvx_base`)

| Offset | Type | Field | Value |
|---|---|---|---|
| 0 | u32 | `magic` | `N4_MAGIC` |
| 4 | u32 | `version` | 4 |
| 8 | u32 | `bsize` | 4096 |
| 12 | u32 | `nblocks` | partition blocks (at most 16 Mi) |
| 16 | u32 | `metab` | `N4_METAB` (85) |
| 20 | u32 | `meta_a` | 4 |
| 24 | u32 | `meta_b` | 89 |
| 28 | u32 | `data0` | 174 |
| 32 | u64 | `created` | seconds since epoch at format |
| 40 | u32 | `inodes` | `FS_N` (1024) |
| 44 | u32 | `dirs` | `DN_MAX` (512) |
| 48 | u32 | `links` | `NLINKS` (256) |
| 52 | u32[115] | `pad` | 0 |

### Metadata image `n4meta` (344128 bytes, one per copy)

```c
typedef struct { n4hdr h; n4ino ino[FS_N]; n4dir dir[DN_MAX]; n4lnk lk[NLINKS]; } n4meta;
```

| Offset in image | Contents | Size |
|---|---|---|
| 0 | `n4hdr` | 64 |
| 64 | `n4ino ino[1024]` | 262144 |
| 262208 | `n4dir dir[512]` | 65536 |
| 327744 | `n4lnk lk[256]` | 16384 |
| 344128 | end; zero padding to 85 x 4096 = 348160 | |

Array index = `FS`/`DT`/`g_lk` slot index, so slot numbers (and therefore inode numbers) stay the same across reboots. Every struct size is checked with `_Static_assert`.

`n4hdr` (64 bytes)

| Off | Type | Field |
|---|---|---|
| 0 | u32 | `magic` = `N4_MMAGIC` |
| 4 | u32 | `version` = 4 |
| 8 | u64 | `gen`, generation, incremented on every commit |
| 16 | u32 | `crc`, CRC-32 (poly `0xEDB88320`) of the whole `n4meta`, computed with this field set to 0 |
| 20 | u32 | `size` = `sizeof(n4meta)` |
| 24 | u32 | `ninode` = 1024 |
| 28 | u32 | `ndir` = 512 |
| 32 | u32 | `nlink` = 256 |
| 36 | u32 | `bsize` = 4096 |
| 40 | u32[6] | `pad` |

`n4ino` (256 bytes)

| Off | Type | Field |
|---|---|---|
| 0 | u32 | `used` |
| 4 | u32 | `dir` (parent `DT` index) |
| 8 | u32 | `mode` (default written: `0100644`) |
| 12 / 16 | u32 | `uid` / `gid` |
| 20 | u32 | `nlink` |
| 24 / 28 / 32 | u32 | `mtime` / `ctime` / `atime` |
| 36 | u32 | `folder` |
| 40 | u64 | `len`, clamped on write to the allocated blocks x 4096 |
| 48 | u32 | `nex`, number of extents in use |
| 52 | u32 | `ind`, indirect block number (0 when `nex <= 17`) |
| 56 | `n4ext[17]` | `{u32 start, count}` inline extents |
| 192 | char[64] | `name` |

`n4dir` (128 bytes): `name[64]` @0, `int parent` @64, `used` @68, `mode` @72 (default `0040755`), `uid` @76, `gid` @80, `mtime` @84, `ctime` @88, `atime` @92, `pad[8]` @96.

`n4lnk` (64 bytes): `used` @0, `dir` @4, `ino` @8, `name[52]` @12.

Indirect block: 4096 bytes holding `n4ext[512]`, the extents numbered 17..528 of one file. It is rewritten to a *newly allocated* block whenever the extent list changes (`g_n4indd[k]`), and the old block goes on the pending-free list.

### In-memory state

| Variable | Role |
|---|---|
| `g_n4_bm` | Free-block bitmap, one bit per block, never stored on disk |
| `g_n4_free`, `g_n4_hint` | Free count and the next-fit allocation cursor |
| `g_n4x[k][17]`, `g_n4nx[k]` | Inline extents and extent count per file |
| `g_n4xx[k]` | Heap array of 512 indirect extents (allocated on demand) |
| `g_n4ind[k]`, `g_n4indd[k]` | Current indirect block and its "needs rewrite" flag |
| `g_n4_img`, `g_n4_shadow` | Metadata image being built, and the last committed image |
| `g_n4_stale[2]` | Copy A/B must be rewritten in full |
| `g_n4pf`, `g_n4pfn` | Pending frees (`n4ext` list) |
| `g_n4_gen` | Generation of the last committed image |

### Allocation and extents

* `n4_alloc(want, hint, &got)` takes the run starting at `hint` if that block is free. Otherwise it calls `n4_find_free`, which scans from the cursor to the end and then wraps to `data0`, skipping full bytes (`0xFF`) 8 blocks at a time. It then extends the run while blocks are free, up to `want`. It may return fewer blocks than asked for.
* `n4_resize(k, nb)` grows a file by appending runs. The hint is the block after the last extent, so files stay contiguous when possible. `n4_push` merges a run into the last extent when they are adjacent. If the disk is full or the file has more than 529 extents, the new blocks are released immediately (`n4_trim(k, cur, 1)`), with `g_n4_err = "disk full"`.
* Shrinking (`n4_trim(k, keep, 0)`) moves the cut blocks onto the pending-free list instead of freeing them.
* `n4_map(k, fb, &run)` turns a file block into a disk block plus the length of the contiguous run.

### Writing a file: `n4_sync_file(k)`

1. Skip if `g_n4` is off, the slot is unused, or the file is an orphan. Files under `/tmp` drop all their blocks.
2. `nb = ceil(len/4096)`. A `lazy` file (never loaded) can only shrink; nothing is read back.
3. Otherwise, unless the file is clean with the right block count and no indirect change pending:
   * `n4_resize(k, nb)`;
   * the blocks to write are `[dlo/4096, ceil(dhi/4096))`, widened to include every newly added block (`cur < nb`);
   * `n4_write_range` writes runs of up to 256 blocks with multi-sector commands (`gdisk_writen`/`ata_writen`). The partial last block is padded with zeros through `g_n4blk`.
   * Mark the file `clean`, `dlo = dhi = 0`.
4. If `g_n4indd[k]` is set: allocate a new block for the indirect extents, write it, and put the old one on the pending list. If the file no longer needs an indirect block, the old one is queued and `ind = 0`.

### The commit protocol: `n4_commit()`

```
 n4_commit()
 |
 +- 1. for every used, non-orphan file: n4_sync_file(k)
 |       data blocks + new indirect blocks -> only blocks that are
 |       FREE in the last committed state (old blocks are still "used")
 +-    n4_barrier()                    (gdisk_flush; no-op on legacy ATA)
 |
 +- 2. n4_serialize()  -> g_n4_img: header gen = g_n4_gen+1, CRC-32
 |
 +- 3. n4_write_meta(0)
 |       copy A: write only 4 KiB blocks that differ from g_n4_shadow
 |               (runs of <=64 blocks), or all if stale[0]
 |       n4_barrier()     stale[0]=0
 |       copy B: same comparison against the shadow, or all if stale[1]
 |       n4_barrier()     stale[1]=0
 |       (any write error -> stale[c]=1, "metadata write failed", abort)
 |
 +- 4. shadow := img ;  g_n4_gen++
 |
 +- 5. release pending frees: n4_mark(start,count,0) for each
```

Power-cut reasoning, from the code:

* Until step 3 finishes copy A, both on-disk copies still describe the old state. None of the blocks they reference were overwritten, because freed blocks are only released in step 5, after both copies hold the new generation.
* If the cut happens while copy A is partly written, A's CRC fails and B (old generation) is used.
* If it happens after A and during B, A is valid with the higher generation and wins.

`n4_mount` then marks the losing copy stale, so the next commit rewrites it in full.

The metadata write is a delta: `n4_write_meta` compares each 4 KiB block of the new image with `g_n4_shadow` (64-bit word compare). The header block always changes because of `gen` and `crc`.

Callers of the commit: `nvx3_sync` (after one file), `nvx3_flush_meta` and `nvx3_sync_all` (background writeback, `sync`).

### Mounting: `n4_mount(secs)`

1. Read the superblock at `g_nvx_base`. Reject it ("unsupported NVX4 layout") unless magic, version, `bsize`, `metab`, `inodes`, `dirs` and `links` all match the compiled constants. Reject it ("does not fit the partition") if `nblocks*8 > secs`, `nblocks > N4_MAXBLK`, `meta_a/meta_b/data0` differ from the fixed layout, or `nblocks < data0+64`.
2. `n4_alloc_state()`: allocate the image, the shadow and the bitmap, zero the bitmap, and mark blocks `0..data0-1` used.
3. Read both copies (85 blocks each) and validate each with `n4_valid_meta`: header fields plus the CRC with `crc` zeroed during the computation.
   * Neither valid to "both metadata copies are damaged", mount fails.
   * Only B valid, or B newer (`gb > ga`) to use B and set `stale[0] = 1`.
   * Otherwise use A, and set `stale[1]` unless B is valid with the same generation.
4. Clear all `FS` slots and fill them from the inodes. For each file:
   * `nex <= 529` is checked;
   * the indirect block (if any) is checked to lie in `[data0, nblocks)` and is read;
   * every extent and the indirect block are claimed in the bitmap with `n4_claim`, which fails on any overlap or out-of-range run.
   On failure the claims made for that file are rolled back and the file is emptied (`len = 0`), so it is counted as damaged rather than failing the mount.
5. `len` is clamped to the allocated blocks x 4096 and to `FS_CAP-1`, and `lazy = len > 0`, `clean = 1`. No file data is read at mount.
6. Load `DT[]`. A directory whose parent is invalid, unused or itself is re-parented to `/`. Load `g_lk[]`, dropping links to unused slots. Files with an invalid `dir` go to `/`.
7. `g_n4 = 1`, `dt_load_done()`. If any files were damaged, `g_n4_err = "some damaged files were emptied"` and a `klog` line is written.

Because the bitmap is rebuilt from the extents on every mount, blocks leaked by a crash (written but never committed) come back automatically.

### Formatting: `n4_format(secs)`

```c
if(n4_layout(secs))return -1;  n4_alloc_state();  ...  n4_apply_res(1);
g_n4=1; g_n4_gen=0;
for each used, non-orphan FS[k]: fs_ensure(k); mark fully dirty; n4_sync_file(k);
n4_barrier(); n4_serialize(); g_n4_stale[0]=g_n4_stale[1]=1;
n4_write_meta(1);                         /* both copies, in full, gen = 1 */
shadow = img; g_n4_gen=1;
n4_write_super(k_realtime_ms()/1000);     /* sector 0 of the partition - LAST */
n4_apply_res(0); g_n4_nres=0; g_n4pfn=0;
```

Whatever is in `FS[]` at that point is written out: the default files from `fs_init`, or the files just read from an old disk. `n4_write_super` writes one sector and then flushes. `n4_usage` reports `(nblocks - data0)*4096` total bytes and `g_n4_free*4096` free bytes.

---

## 7.7 Old formats and conversion

### NVX2 (`NVX_MAGIC 0x4E565849`, `nvx_mount_old`)

```c
typedef struct { char name[64]; u32 start,len,folder,dir,mode,uid,gid; } nvx_ent;          /* 92 B */
typedef struct { char name[32]; int parent, used; unsigned int mode; } nvx_dnode;            /* 44 B */
typedef struct { u32 magic,count; nvx_ent e[NVX_MAX]; u32 dcount; nvx_dnode d[NVX_DN_MAX]; } nvxfs_t;
```

The directory lives at `NVX_DIR_LBA = g_nvx_base`. It holds 24 files (`NVX_MAX`), each with an absolute starting LBA in `start`, and up to 64 directory nodes with 31-character names. `disk_init` reads each file (through a 2 MiB static buffer) into a free slot. Entries starting before `base+8` are skipped. Each file's sector range is recorded with `n4_reserve_secs`.

### NVX3 (`0x3358564E`, `nvx3_try_load` in `unixfs.c`)

```c
typedef struct { u32 used, dir, mode, uid, gid, nlink, len, mtime, ctime, atime, folder; char name[64]; } nvx3_ino;
typedef struct { u32 used, dir, ino; char name[52]; } nvx3_link;
typedef struct { u32 magic, version, ninode, nlinks, ndn; nvx3_ino ino[24]; nvx3_link lk[64]; nvx_dnode d[64]; } nvx3_t;
```

Each of the 24 inodes has a fixed 2 MiB slot at `nvx3_slot(k) = NVX_DATA0 + (k+1)*4096` sectors (`NVX_DATA0 = base+8`). The loader:

* requires `ninode == 24` and `nlinks == 64`;
* loads each file into `FS[k]` with the same index;
* copies links into `g_lk`;
* copies the directory nodes into `g_nvx.d` so `dt_init` to `dt_load_from_disk` can rebuild `DT[]`;
* reserves every data range.

### Crash-safe conversion

```c
static u32 g_n4_res[64][2]; static int g_n4_nres;
static void n4_reserve_secs(u32 sec,u32 n){ ... g_n4_res[i] = {sec/8, blocks covering [sec, sec+n)} ... }
static void n4_apply_res(int set){ /* clamp each range to [data0, nblocks) and mark used/free */ }
```

`n4_format` marks the reserved ranges as used *before* writing anything, so new file data can only go into blocks the old format did not use. The NVX4 metadata copies occupy blocks 4..173 (sectors 32..1391 of the partition). The NVX3 header (about 9.5 KB at sectors 0-18) and NVX3 data (from sector 4104) lie outside that range. The new superblock is written into sector 0 last, replacing the old magic.

If power fails at any earlier point, sector 0 still carries the NVX2/NVX3 magic and the next boot converts again. If the format fails, `disk_init` reports "Old disk could not be converted - left unchanged" and leaves `disk_ok = 0`. Reservations are clamped to `data0` and above, so they protect old data only in the data area.

---

## 7.8 NTFS and FAT root file systems (`fs/ntfs/ntfsk.c`)

When no NoovexFS location is found, the system can run from an NTFS, FAT32 or FAT16 volume. Every back end is reached through one table:

```c
typedef struct { const char* name;
    int  (*mount)(nvxblk_rd,nvxblk_wr,unsigned long long base,unsigned long long nsect,int ro);
    void (*umount)(void);
    int  (*exists)(const char*); int (*mkdir)(const char*);
    long (*write)(const char*,const char*,long); long (*read)(const char*,char*,long);
    int  (*del)(const char*);   int (*ls)(const char*,nvxntfs_lscb,void*);
    int  (*format)(nvxblk_rd,nvxblk_wr,unsigned long long,unsigned long long,const char*); } rfs_ops;
static const rfs_ops g_rfs_ntfs  = {"NTFS",  nvxntfs_mount, ..., nvxntfs_format};
static const rfs_ops g_rfs_fat32 = {"FAT32", vf_mount, ..., vf_fmt32};
static const rfs_ops g_rfs_fat16 = {"FAT16", vf_mount, ..., vf_fmt16};
```

* Sector I/O. `nk_rd`/`nk_wr` provide 512-byte sector callbacks over `gdisk_read`/`gdisk_write`. On 4 KiB-sector drives (`dk_ratio() > 1`) they read-modify-write the containing 4 KiB sector and call `bc_invalidate`.
* Finding volumes. `nk_scan` probes, in order:
  * the whole disk;
  * the four MBR entries;
  * when an entry has type `0xEE`, up to 128 GPT entries (header "EFI " at LBA 1, entry LBA at offset 72, count at 80, entry size at 84, first and last LBA at entry offsets 32 and 40).

  `nk_probe` needs the `55AA` signature, then recognises `"NTFS "` at byte 3 (returns 1), or a FAT BPB: 512 bytes/sector, a power-of-two cluster size, FAT count non-zero, reserved sectors non-zero (returns 2). `nk_find`, used by the `ntfs` command, looks only for NTFS: MBR types `0x07/0x17/0x27`, or the GPT Microsoft basic-data GUID.
* Taking over a root (`nk_root_try`). Each candidate is mounted read-only (type 1 with the NTFS ops, type 2 with the FAT ops; `vf_mount` decides FAT16 or FAT32 from the cluster count). It then reads `/.noovexos`.
  * If the marker is missing, the volume is unmounted and logged as "left untouched", so a Windows disk is never written.
  * If it is present, the volume is remounted read-write, and the system sets `g_rfs = o`, runs `nk_load_tree()` and `nk_meta_apply()`, and sets `g_rootfs = 1`.
* Lazy tree load (`nk_load_tree`). A breadth-first walk with `g_rfs->ls` creates `DT` nodes with `dt_mkdir_at` and fills `FS` slots with name, size, `lazy = 1` and `clean = 1`. Mode defaults to `0100644` and owner to root.
  * Names beginning with `$`, `.noovexos`, `.noovexmeta` and `System Volume Information` are skipped.
  * Files larger than `FS_CAP-1` are skipped.
  * Contents are read on demand by `nk_root_read`, which fills any short read with zeros.
* Permissions file `/.noovexmeta`. Each line is `<D|F> <mode octal> <uid> <gid> <absolute path>`, for every directory except `/` and every non-orphan file, in a 128 KiB buffer.
  * `nk_meta_write` rewrites the whole file after every save, flush and tree save.
  * `nk_meta_apply` applies it at mount.
* Saving:
  * `nk_root_sync_file(k)`: when the file is clean and its path is in the known-files list `g_nk_kf`, only the meta file is rewritten. Otherwise the parent directories are created (`nk_mkdirs`), the whole file is written with `g_rfs->write`, and it is marked clean.
  * `nk_root_flush()`: deletes on the volume every known path that no longer matches a live `FS` slot (this is how deletes and renames reach the disk), then rewrites the meta file.
  * `nk_root_dt_save()`: creates the directories, then removes known directories that no longer resolve, longest path first (up to 8 per call).
  * `nvx3_sync`, `nvx3_flush_meta`, `nvx3_sync_all` and `dt_save` all route here while `g_rootfs` is set.
* Installing (`nk_install`, used by `core/init/installer.c`): format with the chosen ops (volume label `NOOVEXOS`), mount, write `/.noovexos` = `"NoovexOS\n"`, create every directory, copy every file (reporting progress), write the meta file, then unmount. The installer sets the data partition's MBR type to `0x07` (NTFS), `0x0C` (FAT32), `0x0E` (FAT16) or `0x7F` (NoovexFS).

---

## 7.9 FAT drivers

### `fs/fat/vfat.c`, read/write FAT16/FAT32 on sector callbacks

* State. The state is `g_vfat`:
  * geometry: `spc`, `rsvd`, `nfats`, `fatsz`, `root_lba`, `data_lba`, `clusters`, `rootclus`, `fsinfo`, `cbytes`;
  * the whole FAT in memory (`fat`), up to 16 MiB, otherwise mount returns -3;
  * one dirty flag per FAT sector (`fdirty`);
  * a one-cluster buffer;
  * a one-sector directory cache (`g_vfat_sec`/`g_vfat_secno`).
* Mount. Mount requires 512-byte sectors, a power-of-two `spc` and a volume no bigger than the partition. The type is FAT16 when there are fewer than 65525 clusters. Fewer than 4085 clusters is refused (-2). The cluster count is clamped to what the FAT can index. The free count is computed by scanning the FAT. On FAT32 the FSInfo (`0x41615252`) next-free hint is used.
* Flush. `vf_flush` writes each dirty FAT sector to every FAT copy, and on FAT32 updates FSInfo's free count (offset 488) and next-free (492).
* Directories. `vf_next` walks entries and assembles long file names. Each LFN entry holds 13 UTF-16 characters at offsets `{1,3,5,7,9,14,16,18,20,22,24,28,30}`. An LFN is accepted only when its checksum matches `vf_cksum` of the 8.3 name. Otherwise the short name is used, honouring the NT lowercase flags in byte 12 (`0x08` base, `0x10` extension). `vf_walk` resolves paths case-insensitively.
* Creating entries. `vf_mkent` stores a name as a pure 8.3 entry when `vf_make83` accepts it. Otherwise it writes LFN entries plus a `~N` alias from `vf_alias`. `vf_slot` finds free directory slots and grows a subdirectory by a zeroed cluster when needed (the FAT16 root cannot grow).
* Writes. `vf_write` always writes a new cluster chain (`vf_writechain`), then points the entry at it and frees the old chain. `vf_mkdir` writes `.` and `..`. `vf_delete` refuses non-empty directories.
* Format (`vf_format`):
  * cluster sizes from the Microsoft tables;
  * FAT16 needs at least 8400 sectors and at most 4194304 (2 GiB); FAT32 needs at least 66600 sectors;
  * 32 reserved sectors on FAT32, 1 on FAT16; 2 FATs; 512 root entries on FAT16;
  * the FAT size is iterated until the clusters fit;
  * on FAT32 a backup boot sector goes in sector 6 and FSInfo in sectors 1 and 7;
  * a volume-label entry is written in the root.

### `fs/fat/fat_ata.c`, boot-media reader

A minimal read-only FAT16/FAT32 reader on the legacy ATA drive (`ata_read`). It handles an MBR partition (types `0x01,0x04,0x06,0x0B,0x0C,0x0E`) or a superfloppy whose sector 0 starts with a jump. It supports only the root directory and 8.3 names (`fat_name83`):

* `fat_listdir(cb)` backs the shell's `fatls`;
* `fat_read_file(name, out, cap)` backs `fatcat`, the `sh` fallback and `pkg` loading.

### USB mass storage at `/mnt/usb`

The USB FAT driver lives in `drivers/usb/xhci.c`. `ufat_mount` reads the first MBR entry or a superfloppy and caches up to 32 root-directory entries in `ufs[32]` (`name[16]`, `size`, `clus`). `ufat_read` follows the cluster chain.

On the file-system side (`unixfs.c`):

* `ux_mount("...", "/mnt/usb")` is root only (syscall 168 and Linux `mount`, number 165). It initialises the device if needed (`msd_autoinit`, `ufat_mount`), creates `/mnt/usb`, and registers the read-only `USBFS` back end once.
* `ux_usb_idx` matches `/mnt/usb/<name>` case-insensitively.
* `usbfs_load` caches one whole file (up to 1 MiB) in `g_usbbuf`.
* `ux_umount` clears `g_usb_mounted`.
* Files appear in `readdir` through `k_readdir_extra`, and in `stat` with inode `21000+fi`.

---

## 7.10 Packages (`fs/pkg/pkg.c`)

`pkg_install(file, n, extract)` (shell command in `shell/commands.c`) works from an 8 MiB raw buffer (`g_pkg_raw`) and an 8 MiB inflate buffer (`g_pkg_inf`).

`pkg_open` recognises three kinds of package:

* `.deb` (`!<arch>` header, kind 1): `ar_find` walks the 60-byte `ar` headers (size is the decimal at bytes 48..57, bodies are 2-byte aligned).
  * From `control.tar*` (gunzipped when it starts `1F 8B`), `./control` supplies `Package`, `Version`, `Architecture` and `Description`.
  * `data.tar*` becomes the payload.
* Alpine `.apk` (gzip, kind 2): the whole file is gunzipped. `.PKGINFO` supplies `pkgname`, `pkgver`, `arch` and `pkgdesc`.
* Android/ZIP `.apk` (`PK\3\4`, kind 3): recognised, but not extracted.

`xz` and `zstd` payloads are passed through unchanged and are not unpacked.

`tar_walk` handles ustar headers: name at 0 (plus the 155-byte prefix at 345 for ustar), octal mode at 100, octal size at 124, type at 156. It visits regular files (`0`, NUL, `7`) and directories (`5`).

With `extract`:

1. `/pkg` and then `/pkg/<name>` are created (`unnamed` when there is no name).
2. `pkg_extract_cb` strips `./`, creates every intermediate directory with mode `0040755`, and stores each file in a free `FS` slot. The mode is `0100000 | (mode & 0777)` (`0644` when zero) and the owner is `g_uid`.
3. Each file is synced, then `dt_save()` is called.

Without `extract`, only the file count and total size (`g_pkg_nfiles`, `g_pkg_total`) are computed.
