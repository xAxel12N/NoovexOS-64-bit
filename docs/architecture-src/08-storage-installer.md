# Part 8: Storage drivers, PCI and the installer

This part covers everything between the PCI bus and a finished installation on disk: PCI configuration access, the four block-device back ends (ATA PIO, AHCI, NVMe, VirtIO-blk), the generic `gdisk_*` layer and its buffer cache, the graphical installer and disk manager in `init/installer.c`, USB mass storage, and the shell commands that work with disks.

All paths are relative to `source/kernel/`. The storage code is split into units (`drivers/bus/pci.c`, `drivers/storage/ahci.c`, `ata.c`, `gdisk.c`, `nvme.c`, `fs/nvxfs.c`, `fs/fat_ata.c`, `init/installer.c`), each with a header under `include/nvx/`. VirtIO-blk is a `MODS` driver that sees only `include/kapi.h` (see 8.6).

The file names do not match their contents. The code is not split up the way the file names suggest: | File | What it actually contains | |---|---| | `drivers/storage/ahci.c` | A standalone, read-only `ahci_identify()` probe with its own static buffers. It is used only by the `disks` command. The rest of the file is the PCnet NIC driver (`wcsr`, `nic_up_raw`, `nic_send`, ...). | | `drivers/storage/gdisk.c` | The real AHCI driver: `AHCI_*` addresses, `ahci_cmd`, `ahci_read`, `ahci_write1`, `ahci_flush` and `ahci_init`. | | `drivers/storage/nvme.c` | The NVMe driver, then the generic disk layer (`DK_*`, `gdisk_*`, buffer cache, `dk_ratio`), then the first installer helper (`ins83`, the `_binary_inst_*` externs). | | `init/installer.c` | The Disk Manager (`dm_*`), the kernel snapshot and checks (`inst_snapshot_data`, `kimg_check`, `kcheck_run`, `memmap_run`, `memtest_run`), the boot-partition builder (`inst_build_fs`), the installer UI and its run logic (`ip_*`), and the boot menu (`bootmenu_*`). |

---

## 8.1 PCI (`drivers/bus/pci.c`)

### Configuration access

The kernel uses legacy configuration mechanism #1 only, through I/O ports `0xCF8`/`0xCFC`. There is no ECAM/MMCONFIG.

```c
static u32 pci_cfg(int bus,int slot,int fn,int off){ u32 a=(1u<<31)|((u32)bus<<16)|((u32)slot<<11)|((u32)fn<<8)|(off&0xFC); outl(0xCF8,a); return inl(0xCFC); }
static void pci_cfgw(int bus,int slot,int fn,int off,u32 v){ ... outl(0xCF8,a); outl(0xCFC,v); }
```

`drivers/net/nic.c:39` defines an identical writer called `pci_wcfg`. The `disks` command and the PCnet code use that one.

### Functions

| Symbol | Scope | Behaviour |
|---|---|---|
| `pci_scan()` | buses 0-3, function 0 only | Fills the parallel arrays `pci_ven/pci_dev/pci_cls/pci_sub/pci_bus/pci_slot`, holding at most `PCI_MAX` = 24 entries, with the count in `pci_n`. `kmain` calls it (`init/main.c:145`, logged as `"boot: pci scanned"`). Consumers such as `pci`, `net`, `disks`, `wifi` and Settings rescan lazily with `if(!pci_n)pci_scan()`. |
| `pci_find(cls,sub,prog,&bus,&slot,&fn)` | all 256 buses, all functions of multifunction devices (header bit `0x800000` at offset `0x0C`) | Returns the first device that matches class and subclass. `prog<0` means any programming interface. The source comment gives the reason for the full scan: "NVMe/AHCI often live on high buses". |
| `pci_quiesce()` | all 256 buses and functions, header type 0 only | Clears Bus-Master Enable (command bit 2) on devices that the firmware may have left doing DMA. It counts them in `g_pci_quiesced`, which `memmap` reports. It runs right after `pci_scan` at boot and logs `"boot: stopped DMA on devices the firmware left running"`. |

Which devices `pci_quiesce` stops:

| Class | Stopped? |
|---|---|
| `0x01` storage, `0x02` network, `0x04` multimedia, `0x0D` wireless | yes |
| `0x0C` serial bus, subclass `0x05` (SMBus) | no |
| `0x0C03` USB with prog-if other than `0x30` (UHCI/OHCI/EHCI) | yes |
| `0x0C03` prog-if `0x30` (xHCI) | only when BAR0 is a 64-bit BAR (`(b0&6)==4`) and the upper half (BAR1, offset `0x14`) is non-zero, meaning the controller lives above 4 GiB |
| other `0x0C` subclasses | yes |
| anything else | no |

The command register is written back as `(cmd&0xFFFF)&~0x04`, which leaves the memory and I/O decode bits untouched.

### BAR handling and the `< 0xF0000000` limit

None of the storage drivers map BARs. They cast the 32-bit physical BAR value straight to a pointer, so a BAR is accepted only if it falls in a range they can reach that way:

| Driver | BAR used | Rejected when |
|---|---|---|
| AHCI (`ahci_init`, `gdisk.c`) | BAR5 (`0x24`, ABAR) | it is an I/O BAR (`bar5&1`), or `bar5<0x1000`, or `bar5>=0xF0000000` to `ahci_init` returns 0 |
| NVMe (`nvme_init`, `nvme.c`) | BAR0 (`0x10`) plus BAR1 (`0x14`) | BAR1 is non-zero (the 64-bit BAR has a high half) to `nvme_failstage=2`; it is an I/O BAR to 3; `base<0x1000` or `base>=0xF0000000` to 4 |
| VirtIO (`vio_open`, `virtio.c`) | BAR0 | it is not an I/O BAR (`!(bar&1)`). Only the legacy I/O-port interface is supported. |

Before touching registers, AHCI and NVMe set command bits `0x06` (Memory Space and Bus Master). `kapi_pci_enable` (`core/kapi.c:16`), which the VirtIO module uses, ORs in `0x407` instead: I/O, Memory, Bus Master and INTx-disable.

---

## 8.2 ATA PIO (`drivers/storage/ata.c`)

The ATA driver handles the primary legacy channel only: `ATA_IO` = `0x1F0` and `ATA_CTL` = `0x3F6`. `ATA_CTL` is defined but never used. All transfers are LBA28 PIO with polling.

| State | Meaning |
|---|---|
| `g_ata_drv` | `0` = master, `1` = slave, `-1` = none |
| `g_disk_sectors` | sector count from IDENTIFY, clamped to `0xFFFFFFFF` |

### `ata_detect()`

For drive 0 and then drive 1, `ata_detect()` does the following:

1. It selects the drive with `0xA0|(drv<<4)`, zeroes the count and LBA registers, and issues IDENTIFY DEVICE `0xEC`.
2. It skips the drive if status reads `0x00` or `0xFF` (nothing there), or if BSY never clears.
3. It treats non-zero LBA-mid or LBA-high values (`0x1F4`/`0x1F5`) as an ATAPI or other device and skips it.
4. It waits for DRQ and fails on ERR. It then reads 256 words and computes the size: if the LBA48 count in words 100-103 is non-zero it uses that (clamped to 32 bits), otherwise the LBA28 count in words 60-61.

### Transfers

| Function | ATA command | Notes |
|---|---|---|
| `ata_read(lba,count,buf)` | `0x20` READ SECTORS | Splits the request into chunks of at most 255 sectors and calls `ata_poll()` before each 512-byte block. |
| `ata_write(lba,buf)` | `0x30` WRITE SECTORS | Writes one sector, then `0xE7` FLUSH CACHE after every sector. Only BSY is waited on, so a flush error is not checked. |
| `ata_writen(lba,count,buf)` | `0x30`, then a single `0xE7` | Multi-sector write in chunks of at most 255 sectors, with one cache flush at the end. |

`ata_poll()` returns `-1` on ERR (`0x01`) or DF (`0x20`), and `0` once BSY is clear and DRQ is set. Every loop is bounded at 4,000,000 iterations.

Two other files call `ata_detect()` directly: `fs/fat_ata.c` (`fat_mount`) and `fs/nvxfs.c` (`nvx_locate`, which runs from `disk_init()` at boot). When an IDE disk exists, NVXFS uses the `ata_*` functions directly (`g_nvx_generic=0`) rather than the gdisk layer.

---

## 8.3 AHCI (`drivers/storage/gdisk.c`)

### Fixed DMA addresses

The driver uses identity-mapped physical addresses as its DMA memory, not buffers it allocates:

| Macro | Address | Contents |
|---|---|---|
| `AHCI_CLB` | `0x00B00000` | Command list. Only slot 0 is used. |
| `AHCI_FIS` | `0x00B01000` | Received-FIS area |
| `AHCI_CT` | `0x00B02000` | Command table: the CFIS at `+0`, one PRDT entry at `+0x80` |
| `AHCI_BUF` | `0x00B03000` | Data bounce buffer |

`ahci_cmd` refuses `count>360`. Since 360 x 512 = `0x2D000`, `AHCI_BUF + 0x2D000` = `0x00B30000`, which is exactly where `NVME_ASQ` begins. The two drivers share one arena without overlapping.

### `ahci_init()`

1. It calls `pci_find(0x01,0x06,-1,...)` to find a SATA controller on any bus, with any prog-if.
2. It validates BAR5 (see 8.1), then enables memory space only. Bus mastering stays off until every implemented port is stopped: for each bit in `PI` it clears `PxCMD.ST` and waits for `CR`, clears `FRE` and waits for `FR`, sets `PxIE` to 0 and clears `PxIS`. It also clears `GHC.IE` and `IS`. Only then does it set Bus-Master Enable. Before v8.98 bus mastering was turned on first, so a controller whose BAR was then rejected, or ports the firmware left running, could resume DMA into memory the kernel had reclaimed.
3. It sets `GHC.AE` (ABAR + `0x04`, bit 31) and reads `PI` (ABAR + `0x0C`).
4. Port selection. It walks the implemented ports in order. It skips a port unless `PxSSTS.DET == 3` and the signature `PxSIG` (`+0x24`) is `0x00000101` (SATA disk) or `0`. On the first port that qualifies it:
   - stops the port by clearing ST and FRE, then waits for CR and FR to clear,
   - points `PxCLB`/`PxFB` at `AHCI_CLB`/`AHCI_FIS` and zeroes them,
   - sets FRE and then ST,
   - sets `ahci_port`, and
   - issues IDENTIFY (`0xEC`). If IDENTIFY succeeds, it stores the size in `ahci_sectors` (LBA48 words 100-102, else LBA28 words 60-61, clamped to 32 bits) and returns 1. Otherwise it moves on to the next port.

### `ahci_cmd(lba,count,buf,write,atacmd)`

```c
ch[0]=(5)|(write?(1u<<6):0)|((count?1u:0u)<<16); ch[1]=0; ch[2]=AHCI_CT; ch[3]=0;   /* CFL=5 dwords, W bit, PRDTL */
prdt[0]=buf; prdt[3]=(count*512u-1u);                                                /* single PRD */
fis[0]=0x27; fis[1]=0x80; fis[2]=(u8)atacmd; fis[7]=0x40;                            /* H2D, C=1, LBA mode */
fis[4..6]=lba[0..23]; fis[8]=lba[24..31]; fis[9]=fis[10]=0; fis[12..13]=count;
```

The command waits for `PxTFD` (`+0x20`) to drop BSY (`0x80`) and DRQ (`0x08`). It then writes `PxCI=1` (`+0x38`) and polls up to 20,000,000 iterations for bit 0 to clear. It fails if `PxIS` bit 30 (TFES) is set, or if `PxTFD.ERR` is set at the end. LBA bits 32-47 are always zero, so only the first 2 TiB are addressable.

| Function | ATA opcode | Path |
|---|---|---|
| `ahci_init` (IDENTIFY) | `0xEC` IDENTIFY DEVICE | data goes to `AHCI_BUF` |
| `ahci_read(lba,cnt,out)` | `0x25` READ DMA EXT | reads into `AHCI_BUF`, then copies to `out` |
| `ahci_write1(lba,in)` | `0x35` WRITE DMA EXT | copies one sector into `AHCI_BUF` |
| `gdisk_writen` (AHCI branch) | `0x35` | chunks of at most 128 sectors |
| `ahci_flush()` | `0xEA` FLUSH CACHE EXT | issued with `count=0` (PRDTL 0) |

### The separate `ahci_identify()` probe (`drivers/storage/ahci.c`)

`ahci_identify(abar,port,model,&sectors)` is a self-contained IDENTIFY that uses static, aligned `.bss` buffers: `ahci_clb[1024]`, `ahci_fis[256]`, `ahci_ctb[256]` and `ahci_idbuf[512]`. It also returns the 40-character model string from words 27-46. Its acceptance rule is stricter than `ahci_init`'s: it requires `PxSIG == 0x101` exactly.

Only `disks` calls it. The probe itself (`ahci_identify_raw`) has to point `PxCLB`/`PxFB` at its own buffers. Since v8.95 the public wrapper `ahci_identify` saves `PxCLB`, `PxCLBU`, `PxFB`, `PxFBU` and `PxCMD` first. If the probe changed them, it stops the port (clears ST and FRE and waits for CR and FR, bits 15 and 14), writes the four address registers back and turns FRE and ST back on only if they were on before.

Note: Before v8.95 nothing pointed the port back, so after `disks` the main driver (`ahci_cmd`, which always builds its command at `AHCI_CLB`) issued commands through a list the controller was no longer reading. Disk access then failed until reboot. Checked in QEMU (q35, ICH9 AHCI): `PxCLB` of the active port reads `0x00B00000` before and after repeated `disks` calls.

---

## 8.4 NVMe (`drivers/storage/nvme.c`)

### Memory layout

| Macro | Address | Use |
|---|---|---|
| `NVME_ASQ` | `0x00B30000` | Admin submission queue (64-byte entries) |
| `NVME_ACQ` | `0x00B31000` | Admin completion queue (16-byte entries) |
| `NVME_IOSQ` | `0x00B32000` | I/O submission queue, qid 1 |
| `NVME_IOCQ` | `0x00B33000` | I/O completion queue, qid 1 |
| `NVME_ID` | `0x00B34000` | Identify data |
| `NVME_BUF` | `0x00B35000` | One-block data buffer, used as PRP1 only |

### Initialisation and the `nvme_failstage` codes

`nvme_init()` sets `nvme_failstage` as it goes, and the `disks` command prints it as `init: FAILED stage N`. It enables memory space only at first, and turns on bus mastering after `CC.EN` is cleared and `CSTS.RDY` reads 0, so a controller the firmware left running cannot DMA in the meantime.

| Stage | Meaning |
|---|---|
| 0 | no controller found (`pci_find(0x01,0x08,...)`, `nvme_found=0`) |
| 1 | controller found |
| 2 | BAR1 is non-zero (the 64-bit BAR has a high half) |
| 3 | BAR0 is an I/O BAR |
| 4 | the BAR base is `<0x1000` or `>=0xF0000000` |
| 5 | registers are mapped |
| 12 | `CAP.MQES+1 < 2`. The queue depth is `min(MQES+1, 64)`. |
| 13 | `CAP.MPSMIN` (`cap>>48 & 0xF`) is non-zero, i.e. the controller cannot use 4 KiB pages |
| 14 | `CSTS.RDY` did not clear after `CC=0` |
| 6 | enable failed: RDY was not set, or CFS was set |
| 7 | the controller is enabled |
| 8 | Identify Namespace failed for namespace 1 and for the first namespace in the active list |
| 15 | the LBA format is not 512 B-4 KiB (`LBADS` outside 9-12) |
| 11 | the namespace format carries metadata |
| 9 | Create I/O CQ failed |
| 10 | Create I/O SQ failed |

The enable sequence:

- Timeout. It derives the timeout from `CAP.TO` as `to*500000` loop iterations, clamped to the range 1 M-60 M.
- Queue registers. It writes `AQA=((q-1)<<16)|(q-1)`, then `ASQ` (`0x28`) and `ACQ` (`0x30`).
- Enable. It writes `CC=(4<<20)|(6<<16)|1`: IOCQES=16 B, IOSQES=64 B, EN.

Doorbells are at `0x1000 + (2*qid + is_cq) * (4 << CAP.DSTRD)` (see `nvme_db`).

### Command opcodes

| Queue | Opcode | Command | Arguments used |
|---|---|---|---|
| admin | `0x06` | Identify | CNS 0 (namespace) into `NVME_ID`. If that fails, CNS 2 (`d10=2`, active namespace list), then the first NSID it returns. |
| admin | `0x05` | Create I/O CQ | `d10=((q-1)<<16)\|1`, `d11=1` (physically contiguous) |
| admin | `0x01` | Create I/O SQ | `d10=((q-1)<<16)\|1`, `d11=(1<<16)\|1` (CQID 1, PC) |
| I/O | `0x02` | Read | `e[10]=slba`, `e[12]=0`, so NLB=0: one native block per command |
| I/O | `0x01` | Write | same layout |
| I/O | `0x00` | Flush | `nvme_flush()` |

`nvme_wait()` matches completions by command ID (`nvme_next_cid`, which wraps at `0xFFF0`) and handles the phase tag. When the loop's 20 M iterations run out it sets `nvme_lasterr=1`; when `CSTS.CFS` is set it sets `nvme_lasterr=2`. Either way it also sets `nvme_bad=1`. The next `nvme_cmd` sees `nvme_bad` and calls `nvme_init()` again before retrying, which resets the controller.

### 4K sectors: `nvme_ratio`

The rest of the kernel addresses everything in 512-byte LBAs. NVMe bridges the difference with these variables:

- `g_nvme_bsz` is the native block size. It is 0 when the format is unsupported.
- `nvme_ratio = bsz/512`.
- `nvme_sectors = nsze*(bsz/512)` is expressed in 512-byte units and clamped to 32 bits.
- `nvme_cblk` caches which native block currently sits in `NVME_BUF`.

| Function | Behaviour |
|---|---|
| `nvme_read1(lba,out)` | Loads native block `lba/ratio` (skipped if it is already cached) and copies out the 512-byte slice at `(lba%ratio)*512`. |
| `nvme_write1(lba,in)` | When `ratio>1`, it reads the native block first (read-modify-write), patches the slice and writes the block back. |
| `nvme_read(lba,cnt,out)` | A loop over `nvme_read1`. |
| `nvme_writen(lba,cnt,in)` | Writes whole native blocks directly when the request is aligned and long enough. Otherwise it falls back to `nvme_write1`. |

The generic layer exposes the ratio as `dk_ratio()`:

```c
static u32 dk_ratio(void){ return (g_dktype==DK_NVME&&g_nvme_bsz>512)?g_nvme_bsz/512:1u; }
```

---

## 8.5 Generic disk layer (`drivers/storage/nvme.c`, after the NVMe driver)

```c
enum { DK_NONE, DK_IDE, DK_AHCI, DK_NVME, DK_VIRTIO };
static int g_dktype=DK_NONE; static u32 g_dksectors=0;
```

`gdisk_init()` chooses exactly one disk, in this priority order:

1. IDE, if `ata_detect` has already found a drive (`g_ata_drv>=0`)
2. AHCI (`ahci_init`)
3. NVMe (`nvme_init`)
4. VirtIO (`virtio_blk_probe`)

`gdisk_ensure()` runs `gdisk_init` once and records that in `g_gdisk_probed`. The source comments call this lazy ("only when disks/installer is used"). However, `disk_init()` to `nvx_locate()` calls `gdisk_ensure()` at boot whenever no IDE drive exists (`fs/nvxfs.c:32`), so on AHCI, NVMe and VirtIO machines the probe actually happens during `kmain`.

`gdisk_name()` returns `"IDE/ATA"`, `"AHCI/SATA"`, `"NVMe"`, `"VirtIO"` or `"none"`.

| Function | Routing / semantics |
|---|---|
| `gdisk_read(lba,cnt,buf)` | `ata_read` / `ahci_read` / `nvme_read` / `virtio_blk_read` |
| `gdisk_write(lba,buf)` | one sector: `ata_write` / `ahci_write1` / `nvme_write1` / `virtio_blk_write(...,1,...)` |
| `gdisk_writen(lba,cnt,buf)` | Invalidates the cache first. Then IDE uses `ata_writen`; NVMe uses `nvme_writen`; VirtIO and AHCI write in chunks of at most 128 sectors. |
| `gdisk_readn(lba,cnt,buf)` | Invalidates the cache, then calls `gdisk_read` in chunks of at most 128 sectors. |
| `gdisk_flush()` | NVMe Flush / AHCI `0xEA` / VirtIO `T_FLUSH`. Returns 0 for IDE, because ATA writes already flush. `power_off()` and `power_reboot()` call it whenever `g_gdisk_probed` is set (`core/power.c:73,87`). |

### Buffer cache

The cache is an LRU of single 512-byte sectors in front of `gdisk_read`:

```c
#define BC_N 64
static struct { u32 lba; u32 tick; int valid; u8 data[512]; } g_bc[BC_N];
```

- `gdisk_read_cached(lba,cnt,buf)`: when `cnt==1`, it looks the sector up and fills a slot on a miss. The victim is the first invalid slot, otherwise the slot with the lowest tick. When `cnt!=1`, it calls `bc_invalidate(lba,cnt)` and then reads straight from the disk.
- `bc_invalidate(lba,cnt)` is called from `gdisk_writen`, `gdisk_readn`, `gdisk_write_through` and `nk_wr` (`fs/ntfsk.c`).
- `g_bc_hits` and `g_bc_miss` are the counters that `bcstat` prints.

`nvx_rd()` in `fs/nvxfs.c` is the cached reader for NVXFS in generic mode.

All LBAs and sector counts in this layer are `u32`, so every back end is limited to 2^32 x 512 B.

---

## 8.6 VirtIO-blk module (`drivers/virtio/virtio_blk.c`)

This driver is built as a separate object (`build.sh` `MODS=... drivers/virtio/virtio.c ... drivers/virtio/virtio_blk.c ...`) and linked in. It talks to the kernel only through `include/kapi.h`, and its exports are declared in `include/drivers.h`.

- Probe. `vio_open(0x1001,...)` finds the legacy (transitional) virtio-blk device, vendor `0x1AF4`. The driver resets the device and negotiates only `VIRTIO_BLK_F_FLUSH` (bit 9). It sets up virtqueue 0, then allocates two DMA buffers with `kapi_dma_pages`: a bounce buffer of `MAXSEC` (128) x 512 B, and a one-page header. It reads the capacity from the legacy device-config area at I/O offsets `0x14`/`0x18`, sets DRIVER_OK, and logs `"virtio-blk: up"`.
- Requests. `vb_req(type,lba,count,towrite)` builds a 2- or 3-descriptor chain: the 16-byte header (`type`, reserved, `sector`), an optional data buffer (flagged device-writable for reads), and a 1-byte status (device-writable). It then kicks the queue and polls `vq_used` with a 5-second timeout (`kapi_ns`). A timeout logs `"virtio-blk: request timeout"` and sets `ready=0`.

| `type` | Request |
|---|---|
| 0 | `VIRTIO_BLK_T_IN`, a read |
| 1 | `VIRTIO_BLK_T_OUT`, a write |
| 4 | `VIRTIO_BLK_T_FLUSH`. It is sent only if the device offered the flush feature; otherwise `virtio_blk_flush()` returns 0. |

Reads and writes are split into chunks of at most 128 sectors and are bounds-checked against `capacity`.

---

## 8.7 The installer (`init/installer.c`)

### Entry points

| Caller | Call |
|---|---|
| Boot menu item 3, "Install NoovexOS" (`init/bootmenu.c` `BM_ITEMS`) | `bootmenu_activate` to `ip_open(ST_BOOTMENU)` |
| Shell `install` (requires root via `need_root`) | `ip_open(ST_DESKTOP)` |
| Disk Manager buttons "Install to selected" / "Install whole disk" | `dm_do_install` to `ip_open_preset(ret, mode, part)` |

`ip_open()` resets all state, sets `g_state=ST_INSTALL` and opens page `IP_FS`. `ip_load()` re-reads the MBR into `ip_mbr` and the arrays `ip_ptype/ip_pst/ip_psz`. Start and size are converted to 512-byte units with `*dk_ratio()`. Any entry of type `0xEE` sets `ip_gpt`.

### Pages and flow

```text
IP_FS --Continue--> IP_WELCOME/IP_TARGET --Continue--> IP_CONFIRM --Install--> IP_RUN --> IP_DONE --Reboot--> power_reboot()
  ^  (Esc)              | Skip -> setup_begin()            | Back -> IP_TARGET            +--> IP_ERROR --Back--> IP_WELCOME
```

The `ip_act` button IDs are: 2 = Continue/Install, 1 = Back (confirm page), 3 = Back (error page), 5 = Reboot, 6 = Skip, 10 = the disk row, and 40-43 = the file-system radio buttons. Keyboard handling (`ip_input`):

- Enter activates the focused button (`g_ip_focus`).
- Left/Right and Tab (key codes 17/18 and `'\t'`) move the focus.
- Up/Down (19/20) change the file system on `IP_FS`.
- Esc goes back one page.

Install mode. "Continue" on the disk page always sets `ip_mode=0` (whole disk). On `IP_TARGET`, Up/Down changes `ip_mode`, but Continue resets it. Mode 1 (install into an existing partition) is reachable only through `ip_open_preset`, i.e. from the Disk Manager. `ip_open_preset` returns early, leaving the user on `IP_FS`, when `ip_disk_ok()` fails.

### File-system choices and sizes

| `g_ip_fs` | UI label | Data partition type | Formatter | Minimum data size enforced in `ip_run` |
|---|---|---|---|---|
| 0 | NoovexFS ("native, recommended") | `NVX_PTYPE` = `0x7F` (`fs/nvxfs.c:10`) | `n4_format(dsz)` (`fs/nvx4.c`) | - |
| 1 | NTFS | `0x07` | `nk_install(&g_rfs_ntfs,...)` | 32768 sectors: "NTFS needs at least 16 MB - choose FAT16 instead." |
| 2 | FAT32 | `0x0C` | `nk_install(&g_rfs_fat32,...)` to `vf_fmt32` | 66600 sectors: "FAT32 needs at least 33 MB - choose FAT16 instead." |
| 3 | FAT16 ("partitions up to 2 GB") | `0x0E` | `nk_install(&g_rfs_fat16,...)` to `vf_fmt16` | 8400 sectors: "FAT16 needs at least 4 MB." The size is also capped at 4186112 sectors. |

Size constants:

```c
#define INST_BOOTP (dk_ratio()>1?557056u:131072u)          /* boot partition: 64 MiB, or 272 MiB on 4K-sector disks */
#define INST_DATAP (((NVX_REGION_SECS)+2047u)&~2047u)      /* NVX_REGION_SECS = 8+(24+1)*4096 = 102408 -> 104448 sectors */
```

- `ip_need() = INST_BOOTP + INST_DATAP`.
- `ip_disk_ok()` requires a disk of type other than `DK_NONE` with `g_dksectors >= 2048 + ip_need() + 2048`, and rejects NVMe when `g_nvme_bsz==0`.
- `ip_part_ok(e)` rejects GPT disks, partitions smaller than `ip_need()`, and any partition starting before LBA 64. It also requires a second free MBR slot for the data partition.

In whole-disk mode the data partition is fixed at `INST_DATAP` sectors ( about 51 MiB) and starts right after the boot partition. In partition mode it fills the rest of the chosen partition.

`nk_install(o,base,nsect,copy,prog)` (`fs/ntfsk.c:168`) does the following:

1. Formats the partition with label `"NOOVEXOS"` and mounts it.
2. Writes `/.noovexos`.
3. Recreates the directory tree (`DT`).
4. If `ip_copy` is set, copies every RAM file (`FS[]`), reporting progress through `ip_nk_prog`.
5. Writes the metadata and unmounts.

Its return codes: -1 = format failed; -2 = mount failed; -3 = marker write failed; -4 = file copy failed. `ip_run` reports -1 as "Formatting the partition failed." and anything else as "Writing files to the new partition failed."

### The kernel snapshot

`kmain` calls `inst_snapshot_data()` first, right after `.bss` is zeroed (`init/main.c:124`). This happens before anything can modify the loaded image. The function copies the kernel image at `0x100000` into `g_kimg_snap[KSNAP_MAX]` (`KSNAP_MAX` = `0x800000`, 8 MiB, the same as the build limit). The length comes from the size stamp at `0x100000+24`; if that is outside 4096-`KSNAP_MAX`, it falls back to `__file_end`.

Note: Up to v8.94 the buffer was 3 MiB while the kernel had grown past 5 MB, so the snapshot was always empty and every installation stopped with "The kernel image could not be captured at boot." The buffer lives in `.bss`, which starts at `0xC00000`, so the larger size does not touch the fixed low-memory regions.

`kimg_check()` verifies the snapshot against `g_buildsig` using SHA-256 twice:

- the immutable part up to `__immutable_end`, with the hash field at `sig+32` zeroed, compared with `g_buildsig.sha256`;
- the whole file, with the field at `sig+88` also zeroed, compared with `g_buildsig.sha256_full`.

The shell command `kcheck` (`kcheck_run`) prints the same comparison and writes it to the kernel log. It also lists the 4 KiB code pages that have changed since boot. On v8.96 in QEMU (BIOS and UEFI) it reports `code MATCHES   whole file MATCHES` for the 5,298,992-byte kernel.

### The boot partition: `inst_build_fs`

This is a C port of `mkhybrid.py build_fat32` (the comment is in `nvme.c`). It builds a complete FAT32 image in RAM: `img=kmalloc(cap*bps)`, where `cap` comes from `inst_fs_layout`.

| Field | Value |
|---|---|
| bytes/sector | `512*dk_ratio()` |
| sectors/cluster | 1 |
| reserved | 32 |
| FATs | 2 |
| media | `0xF8` |
| root cluster | 2 |
| FSInfo | sector 1 |
| backup boot | sector 6 (the first `2*bps` bytes are copied there) |
| OEM / FS type | `"NOOVEXOS"` / `"FAT32   "` |
| volume ID | `0x4E56584F ^ g_ticks` |
| label | `g_dm_name` (default `"NOOVEXOS"`, editable in the Disk Manager) |
| minimum | `psec - dstart >= 65525` clusters, otherwise returns `-2` |

Contents (all directory timestamps are fixed at `0x6000`/`0x5B3B`):

```text
/            volume label entry
/boot/kernel64.bin       (copy 1 of the snapshot)
/boot.cfg                (assets/installer/inst_boot.cfg: kernel=/boot/kernel64.bin, cmdline=noovexos, ...)
/KERNEL64.BIN            (copy 2)
/EFI/BOOT/BOOTX64.EFI    (bootloader/uefi/BOOTX64.EFI, embedded by build.sh as inst_efi.bin)
```

The installer writes only `used = dstart + (next-2)` sectors, which ends at the last allocated cluster. The FAT, however, describes the whole partition. `ko1` and `ko2` are the byte offsets of the two kernel copies inside the image. `ip_mem_verify` uses them to compare the copies against the snapshot, both after building and after verification. If a copy differs, it rewrites the copy, waits 1 s and compares again, to tell RAM that does not hold data apart from something else overwriting it.

### `ip_run()` stages

`ip_prog` runs from 0 to 1000 (per-mille). The banner on the running page uses its own 8 labels: "Preparing disk", "Partitioning", "Formatting", .... The `IP_STAGES[]` strings are defined but never displayed.

| `ip_stage_go(s,prog)` | Work done |
|---|---|
| 0, 0 | Validate the size stamp (4096-`0x800000`), the snapshot, `kimg_check` and `mm_area_conflicts()`. Compute the layout. |
| 1, 40 | Build the boot-partition image and run `ip_mem_verify` on both kernel copies. |
| 2, 90 | Write the MBR (below). On a GPT disk in whole-disk mode, also zero the last native block, where the backup GPT header lives. |
| 3, 110 | Write `inst_stage2.bin` to LBA 1.... This step is skipped when `dk_ratio()>1` (`s2s=0`). |
| 4, 140 to 700 | Write `used` sectors of the image to `bst`, one sector at a time with `gdisk_write`, refreshing the UI every 128 sectors. |
| 5, 700 | FS 1-3: `nk_install` (progress 760-860 via `ip_nk_prog`). |
| 6, 760 | FS 0: set `g_nvx_base`/`g_nvx_secs`, then call `n4_format(dsz)`. |
| 7, 870 to 1000 | Verify: read back the MBR, stage2 and every boot-partition sector and compare them. Recheck the kernel copies in RAM. Call `gdisk_flush()`. |
| done | `ip_prog=1000`, page `IP_DONE`, `ip_load()` |

### MBR writing

```c
for(int i=0;i<446;i++)mbr[i]=i<s1?_binary_inst_boot_bin_start[i]:0;   /* myboot stage1 (assets/installer/inst_boot.bin) */
for(int e=0;e<4;e++)mbr[446+e*16]=0;                                     /* clear every active flag */
boot: pe[0]=0x80; pe[2]=2; pe[4]=0x0C; pe[5..7]=FE FF FF; start=bst/R; size=bsz/R;
data: pe[2]=2; pe[4]=0x07|0x0C|0x0E|0x7F; ... start=dst/R; size=dsz/R;
if(ip_mode==0) *(u32*)(mbr+440)=0x4E565849u^(u32)g_ticks;                /* fresh disk signature */
```

How the two modes differ:

- Whole-disk mode (0) starts from a zeroed sector. The boot partition goes in slot 0 at LBA 2048 and the data partition in slot 1. Any existing GPT is overwritten: the protective `0xEE` entry disappears, and the backup header is zeroed.
- Partition mode (1) keeps the other entries. The chosen slot becomes the boot partition, and the first free slot becomes the data partition.

Partition entries are written in native blocks (`/R`). Everything else in the installer works in 512-byte units.

### Installer error messages

| Message (`ip_err`) | Detail (`ip_err2`) | Cause |
|---|---|---|
| The kernel size stamp is invalid. | - | stamp outside 4096-8 MiB |
| The kernel image could not be captured at boot. | Run kcheck in the terminal and send a photo of the result. | no snapshot, length mismatch, or `kimg_check<0` |
| The kernel code / data loaded at boot is not the original build. | (same) | SHA-256 mismatch |
| The firmware uses memory that NoovexOS also uses. | Run memmap in the terminal... | `mm_area_conflicts()` |
| The chosen partition can't be used. | - | `ip_part_ok` failed |
| The disk is too small. | - | `dst+dsz > g_dksectors`, which also covers `DK_NONE` |
| The partition is too small for FAT16/NTFS/FAT32. | (minimums above) | size checks |
| Not enough memory to build the boot partition. | - | `kmalloc` failed |
| The boot partition image could not be built. | The partition is too small for FAT32 on this disk. | `inst_build_fs` returned -1 or -2 |
| Memory at *addr* does not keep data / was changed by something else / was changed once | *n* differed, ... | `ip_mem_verify` |
| Writing the partition table failed. | The disk reported an error. | MBR write |
| Writing the boot loader failed. | - | stage2 write |
| Writing the boot partition failed. | The disk reported an error. | image write |
| Formatting the partition failed. / Writing files to the new partition failed. | the FS name | `nk_install` |
| Formatting the data partition failed. | `g_n4_err` | `n4_format` |
| The disk did not confirm that the data was saved. | Try again, or use another disk. | `gdisk_flush()` failed |
| Verification failed - the disk returned different data. | `Sector N differs (NVMe, timeout)` / `Reading sector N failed (...)` | read-back mismatch or read error, with `nvme_lasterr` appended |

---

## 8.8 Disk Manager (`dm_*`, `init/installer.c` lines 1-104)

The Disk Manager is a graphical MBR editor. It works on `g_dm_p[4]` (`dmpart {used,type,start,size}`), loaded by `dm_load()` from LBA 0 with `*dk_ratio()` scaling.

| Action | Function | Effect |
|---|---|---|
| Create partition | `dm_create` | Uses the first empty slot. Starts at `dm_free_start()` (end of the last partition, at least 2048, rounded up to 2048), fills the rest of the disk, sets type `0x0C`, and writes LBA 0 immediately. |
| Delete partition | `dm_delete` | Zeroes the entry and writes LBA 0. |
| Install to selected / whole disk | `dm_do_install(0/1)` | `ip_open_preset(..., whole?0:1, idx)` |
| Volume name | `g_dm_name` | Up to 11 characters `[A-Za-z0-9_-]`. Used as the FAT32 boot-volume label. |
| Back / Esc | - | Returns to `g_dm_ret` |

Rows show "FAT32" (types `0x0B`/`0x0C`, with the volume label read from the BPB at offset 71), "NTFS", "Linux" or "used", plus the size, the start LBA and `[active]`. Messages include "No free partition slots (max 4).", "No free space on the disk.", "Partition created.", "Partition deleted.", "Select a partition to delete." and "Select a partition first."

`main.c` dispatches `ST_DISKMGR` to `dm_input`/`dm_present`. Since v8.95 there are two ways in, both through `dm_open(ret)` (`dm_load()`, `g_dm_open=1`, clear the selection and messages, remember `ret`, `g_state=ST_DISKMGR`):

| Entry | Where | Returns to |
|---|---|---|
| `diskmgr` or `partitions` shell command (root only) | `shell/commands.c` | the live session or the desktop |
| P on the installer's welcome or target page ("Press P to edit partitions.") | `ip_input` | the installer (Esc reloads its disk list with `ip_load()`) |

From the Disk Manager, "Install to selected" opens the installer in mode 1 (into an existing partition). Before v8.95 nothing set `ST_DISKMGR`, so the screen and installer mode 1 could not be reached.

---

## 8.9 USB mass storage (`drivers/usb/xhci.c`)

This is a Bulk-Only Transport class driver on top of the xHCI stack.

| Address | Use |
|---|---|
| `MSD_INPUTCTX` `0x00A50000` / `MSD_DEVCTX` `0x00A51000` | slot contexts |
| `MSD_EP0RING` `0x00A52000`, `MSD_INRING` `0x00A53000`, `MSD_OUTRING` `0x00A54000` | transfer rings (256 TRBs, link TRB at index 255) |
| `MSD_CBW` `0x00A55000`, `MSD_CSW` `0x00A55100` | CBW (`0x43425355` "USBC") and CSW (`0x53425355` "USBS") |
| `MSD_DATABUF` `0x00A56000` | sector buffer ("up to 64 KB") |

- Enumeration. `msd_autoinit()` to `xhci_msd_enum(port)` looks for an interface of class `0x08` with bulk IN and OUT endpoints. It then polls TEST UNIT READY up to 8 times and finally issues READ CAPACITY(10), which sets `msd_lastlba` and `msd_bsize`.
- Commands. `msd_bot()` sends one command as CBW, optional data stage, then CSW, and returns `bCSWStatus`.
- Failures. A failed step is recorded in `xhci_failstep`, which `usbdisk` prints.

| SCSI op | Function |
|---|---|
| `0x00` TEST UNIT READY | `scsi_tur` |
| `0x25` READ CAPACITY(10) | `scsi_capacity` |
| `0x28` READ(10) | `scsi_read10` to `umsd_read` |
| `0x2A` WRITE(10) | `scsi_write10` to `umsd_write` |

`ufat_*` is a FAT16/FAT32 driver for the stick, "ported from the proven Noovex9 32-bit implementation":

- `ufat_mount` uses the partition in the first MBR slot (types `0x01/04/06/0B/0C/0E`), or LBA 0 for a superfloppy. It requires 512 B/sector and loads up to 32 root-directory entries into `ufs[]`.
- `ufat_read`, `ufat_write_file` and `ufat_delete_file` cover reading and writing; write-file replaces any existing chain.
- The FAT is cached one sector at a time in `ufatcache`, and `ufat_flush` writes the cached sector to every FAT copy.
- Only the root directory is supported, with 8.3 names.

This driver is separate from `fs/fat_ata.c` (`fat_mount`, `fat_listdir`, `fat_read_file`), a read-only FAT reader that works only through IDE PIO.

---

## 8.10 Shell commands (`shell/commands.c`)

| Command | Line | What it does |
|---|---|---|
| `disks`, `disk`, `lsblk` | 1498 | Runs `gdisk_ensure()`. Prints the NVMe controller's bus and slot with `init: OK` or `FAILED stage N`, the AHCI controller, and `Active disk: <gdisk_name> N MB (install target)`. Then lists every class-`0x01` entry in `pci_scan`. For AHCI controllers (`0x06`, prog-if `0x01`), it runs `ahci_identify` on each implemented port and prints the model and size. Some printed strings are stale, e.g. "NVMe present - read/identify not implemented yet" and "detection only - installing to disk is not enabled yet". |
| `mount`, `fatinfo` | 337 | `fat_mount()` over IDE only. Prints `FAT16/32 mounted drive= sec/clus= data@LBA`, or "no FAT disk found (ATA/IDE PIO only; VM needs an IDE disk with FAT)". |
| `fatls` | 338 | lists the root directory of the IDE FAT volume |
| `fatcat <NAME.EXT>` | 492 | Reads a file from the IDE FAT volume and prints up to 8000 characters. |
| `bcstat`, `bufcache` | 532 | buffer-cache hits, misses and hit rate |
| `usbdisk`, `usbls` | 719 | `msd_autoinit` + `ufat_mount`. Prints the size, FAT type and file list, or "no USB stick found (enum failed at: ...)". |
| `usbcat <FILE>` | 730 | `ufat_read`. The name match is case-insensitive. |
| `usbwrite <FILE> <text>` | 736 | `ufat_write_file`. Prints "written to USB" or "write failed". |
| `usbrm <FILE>` | 967 | `ufat_delete_file` |
| `usb`, `xhci` | 972 | rescans the controllers (`xhci_mouse_autoinit`), then runs `usbinfo` |
| `ntfs mount [ro] \| umount \| info \| ls \| cat \| write \| mv \| mkdir \| rm` | 1591 | NTFS on the gdisk disk through `nk_mount`/`nvxntfs_*`. Every subcommand except `ls`, `cat`, `info` and `help` needs root. Errors include "ntfs: no NTFS volume found on the disk" and "ntfs: not mounted - run 'ntfs mount' first". |
| `install` | 361 | `ip_open(ST_DESKTOP)` (root only) |
| `kcheck` / `memmap` / `memtest` | 1537-1541 | Installer diagnostics: snapshot integrity, the firmware memory map against kernel areas (plus the `g_pci_quiesced` count), and a fill/verify/re-verify test of free RAM. |
| `pci` | 696 | Prints `vendor:device class=`, flagging network devices. Covers only the `pci_scan` set (buses 0-3, function 0). |

## 8.11 Partition scanner (`fs/parts.c`)

### Disks (`bd_scan`)

`bd_scan` builds `g_bd[16]`, a list of every disk the kernel can read, each with a 512-byte-sector reader (`bd_read`):

| Kind | Source | Reader |
|---|---|---|
| main | the disk `gdisk` chose (IDE, AHCI, NVMe or virtio); 4 KiB-sector NVMe keeps its ratio | `gdisk_read` |
| IDE | the other three of primary/secondary master/slave, found with IDENTIFY; ATAPI is skipped | own LBA48 PIO reader (`pio_cmd`) |
| SATA | every other AHCI port with a SATA signature; `ahci_init` is run if the main disk is not AHCI | `ahci_port_io`: stops the current port, points the chosen port at the shared command list, issues one LBA48 command, then restores the main port |
| USB | the first mass-storage device (`msd_autoinit`) | `usb_msd_read512` |

### Partitions (`part_scan_disk`)

| Layout | What is read |
|---|---|
| GPT | protective or hybrid MBR, or no MBR; the primary header at LBA 1, the backup at the last LBA if the primary is bad; up to 1024 entries of 128 to 4096 bytes; gaps are allowed (scanning stops after 128 empty entries in a row past entry 128); the type GUID is named (27 known types) and the UTF-16 name is kept |
| MBR | the four primaries, then the extended partition's chain of EBRs (up to 128 logical partitions, numbered from 5, with loop protection) |
| none | a file system at sector 0 (a "superfloppy" USB stick or a raw volume) |

Each partition is probed for its file system: NTFS, exFAT, FAT12/16/32, BitLocker, LUKS, XFS, NVX4, ext2/3/4 (from the superblock feature bits), HFS+, Linux swap, ISO9660, btrfs and APFS.

### Users

| Caller | Use |
|---|---|
| `parts`, `lsblk`, `partitions` | list every disk and partition (also written to the log as `parts:` lines) |
| `disks` | controller details, then the same list |
| `nk_scan` (`fs/ntfsk.c`) | the root file system search walks `parts_scan_main` instead of its own four-entry MBR and 128-entry GPT loops |

Limits: reads only (writing is still through the main disk); LBAs above 2^32 on the main and USB disks are not reachable through their 32-bit readers; one NVMe namespace.
