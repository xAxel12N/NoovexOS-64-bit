# cs4: integer bug sweep (whole OS minus third_party), v11.06

Scope: fs/, net/, sys/linux (sockets, netlink, ALSA, mmap), lib/image (JPEG/PNG), browser images (GIF/BMP/ICO), web/text/ttf.c, USB HID/UAC/FAT, ELF loader, NVX4, volumes probe.
Checked and found clean (no patch needed): tcpip.c DHCP/DNS/TCP parsing, lxsock TCP/UDP input, PNG decoder, GIF LZW, BMP/ICO, ELF loader, NVX4 mount, mmap/mprotect/munmap/madvise range math (jc_uok catches wrap), timerfd (ival guarded), UAC audio (ch/sub filtered in aud_pick), HID report parser, deb ar parser.
None of these explains the taskbar/start-menu freeze. They are separate crash, hang and memory-corruption bugs.

---

## 1. JPEG: SOS component count overflows the stack array `sc[4]`. HIGH, 90%
lib/image/jpeg.h:199
```
int ns=seg[0]; const u8* q=seg+1; int sc[4]={0,0,0,0};
for(int i=0;i<ns;i++){ ... sc[i]=j; }
```
How it fails: a JPEG on any web page (or in Files/Writer, or as a wallpaper) has an SOS with ns=255 and every component id equal to the first SOF component. Then `sc[i]=0` is written for i up to 254, which overwrites jpeg_decode's stack frame and return address, and the kernel crashes.
Patch (line 199):
```
            int ns=seg[0]; const u8* q=seg+1; int sc[4]={0,0,0,0};
→
            int ns=seg[0]; const u8* q=seg+1; int sc[4]={0,0,0,0}; if(ns<1||ns>4||1+2*ns+3>L-2)return -14;
```

## 2. JPEG progressive: Se>63 indexes past JZZ[64] and writes coefficients at arbitrary offsets. HIGH, 90%
lib/image/jpeg.h:201 (used in jp_prog_scan :141, :145-153)
How it fails: a progressive JPEG with an SOS where Ss=1 and Se=255. In the AC loop `k` runs past 63, `JZZ[k]` reads whatever rodata follows the table, and `cf[JZZ[k]]=...` writes an int at that garbage offset from the JCOEF heap block. That is out-of-bounds kernel heap corruption from a web image.
Patch (line 201):
```
            if(JPROG){ int Ss=q[0],Se=q[1],AhAl=q[2],Ah=AhAl>>4,Al=AhAl&15;
→
            if(JPROG){ int Ss=q[0],Se=q[1],AhAl=q[2],Ah=AhAl>>4,Al=AhAl&15; if(Se>63)Se=63; if(Ss>Se||Al>13){ p=nx; continue; }
```

## 3. JPEG progressive: no output-size check, so the RGBA buffer overflows. HIGH, 85%
lib/image/jpeg.h:235. The baseline path checks `W*H*4>rgbamax` at :204. The progressive path never checks it, and the final loop writes W*H*4 bytes (up to 1920x1200x4 = 9.2 MB).
How it fails: the browser's decode_into_pool passes `cap=IMGPOOL_SZ-imgpool_off`, which can be as small as 8 KB when the pool is nearly full. A 1920x1200 progressive JPEG as the next `<img>` then writes about 9 MB past the end of imgpool. Files viewbuf, Writer, wallsrc and the camera have the same exposure.
Patch (line 234):
```
    if(JPROG) jp_prog_finish();
→
    if((long)W*H*4>(long)rgbamax)return -12;
    if(JPROG) jp_prog_finish();
```

## 4. JPEG DHT: more than 256 Huffman values overflow `JHuff.val[256]`. HIGH, 85%
lib/image/jpeg.h:47 (jhuff_build). The 16 counts can sum to as much as 4080, and `h->val[i]=vals[i]` copies all of them into a 256-byte array. That overwrites the following JHDC/JHAC tables and other statics.
Patch (line 47):
```
    h->nval=k; for(int i=0;i<k;i++)h->val[i]=vals[i];
→
    if(k>256)k=256; h->nval=k; for(int i=0;i<k;i++)h->val[i]=vals[i];
```
Also (MEDIUM): table indexes from the file are never masked, which gives out-of-bounds reads of the JQt/JHDC/JHAC arrays (each has 4 entries).
- :183: `JC[i].q=q[2];` → `JC[i].q=q[2]&3;`
- :200: `JC[j].dct=tt>>4; JC[j].act=tt&15;` → `JC[j].dct=(tt>>4)&3; JC[j].act=tt&3;`
- :182: `if(NC<1||NC>4)return -3;` → `if(NC<1||NC>4||NC==2)return -3;`. With NC==2 the colour conversion at :241 reads `JC[2].plane`, which is stale from the previous image or NULL.

## 5. Netlink: a huge nlmsg_len makes `off` negative, causing an out-of-bounds kernel read and a leak to the user. HIGH, 85%
sys/linux/lxsock.c:398
```
if(L<16||off+(int)L>n)break;
```
How it fails: a Linux program sends a NETLINK_ROUTE message with nlmsg_len=0xFFFFFFF0. `(int)L` is -16, so the check passes. Then `off+=(L+3)&~3u` makes off=-16, the next pass reads `m=b-16` (before the kmalloc'd copy), and nl_err copies 16 of those bytes back into the user's receive queue. The loop can also keep running on garbage.
Patch:
```
        if(L<16||off+(int)L>n)break;
→
        if(L<16||L>(u32)(n-off))break;
```

## 6. ALSA PCM ioctls divide by `a->boundary==0`, so a user program can crash the kernel with #DE. HIGH, 90%
sys/linux/syscalls/timers_and_handles.c:66 `apcm_queued(){ return (...)%a->boundary; }`. It is reached without any state check from STATUS 0x80984120 (:160), DELAY 0x80084121 (:162) and REWIND/FORWARD 0x40084146/0x40084149 (:181-182). boundary is 0 right after open (lx_zero) until HW_PARAMS.
How it fails: any program (SDL, mpv, a game) that opens /dev/snd/pcmC0D0p and calls snd_pcm_status/snd_pcm_delay before hw_params triggers a divide error in the kernel.
Patch:
```
static u64 apcm_queued(struct lx_apcm* a){ return (a->appl+a->boundary-a->hw)%a->boundary; }
→
static u64 apcm_queued(struct lx_apcm* a){ return a->boundary?(a->appl+a->boundary-a->hw)%a->boundary:0; }
```
and at :180:
```
    case 0x40084146u: case 0x40084149u: { u64 fr; if(lx_get(&fr,arg,8))return -LX_EFAULT; u64 n=0;
→
    case 0x40084146u: case 0x40084149u: { u64 fr; if(lx_get(&fr,arg,8))return -LX_EFAULT; u64 n=0; if(!a->boundary||!a->bsize)return -LX_EBADFD;
```

## 7. NTFS volume probe: 32-bit wrap in the attribute walk causes an endless loop (Files freezes when a stick is plugged in). MEDIUM-HIGH, 80%
fs/vfs/volumes.c:58 (vntfs_attr)
```
while(a+16<=rsz){ u32 t=r32(rec+a), l=r32(rec+a+4); if(t==0xFFFFFFFFu||l<16||a+l>rsz)break; ... a+=l; }
```
How it fails: a corrupt or crafted NTFS stick has an MFT record with an attribute at 0x100 where l=0xFFFFFF80 (so a+l wraps to 0x80, which is ≤ rsz) and an attribute at 0x80 where l=0x80. `a` then alternates 0x100 → 0x80 → 0x100 forever inside vprobe_ntfs. The volume scan in Files hangs, and the heartbeat stops if this runs on the main loop.
Patch:
```
if(t==0xFFFFFFFFu||l<16||a+l>rsz)break;
→
if(t==0xFFFFFFFFu||l<16||l>rsz-a)break;
```
Same file, :66 (LOW, 70%): the run list starts at `a+r16(a+32)` and ends at `a+r32(a+4)`, neither checked against the 4096-byte record, so it reads up to 64 KB past the heap block. Add `if(r16(a+32)>=r32(a+4)||r32(a+4)>rsz-(u32)(a-rec))` → skip (set ok=0).
Also :72 (LOW): `1u<<s[108]` and `bs<<s[109]` (exFAT), and :60-61 `1u<<(256-spc)` and `1u<<(-c)` (NTFS), are shifts of 32 or more taken straight from disk bytes. Reject first: `if(s[108]<9||s[108]>12||s[109]>25)return;`, and for NTFS `if(spc>128&&256-spc>31)return; if(c<=0&&(-c)>31)return;`.

## 8. vfat: no bounds check against the partition, and the data-size math can underflow, so writes can land in the next partition. MEDIUM, 75%
fs/fat/vfat.c:184 `data=ts-rsvd-nf*fz-rs` has no check that ts covers the metadata. If it wraps, cl becomes huge and is only clamped to the FAT capacity. vf_c2s() then maps clusters past `ts` and even past `nsect`, and vf_ws (:11) writes there without checking. nk_wr then writes to whichever LBA that is on the main disk, which can be the user's Windows partition.
Patches:
```
:184  u32 rs=(re*32+511)/512, data=ts-rsvd-nf*fz-rs, cl=data/spc;
→     u32 rs=(re*32+511)/512; if((unsigned long long)rsvd+(unsigned long long)nf*fz+rs>=ts)return -1; u32 data=ts-rsvd-nf*fz-rs, cl=data/spc;
:11   static int vf_ws(u32 rel,int n,const void* b){ if(g_vfat.ro||!g_vfat.wr){ g_vf_err=1; return -1; }
→     static int vf_ws(u32 rel,int n,const void* b){ if(g_vfat.ro||!g_vfat.wr){ g_vf_err=1; return -1; } if(n<0||(unsigned long long)rel+(unsigned)n>g_vfat.nsect){ g_vf_err=2; g_vf_errlba=rel; return -1; }
```
(Use the same guard in vf_rs at :9 with g_vf_err=3.)

## 9. TTF cmap: `off+4` wraps, so the kernel reads about 4 GB past the font (page fault from a web font). MEDIUM, 75%
web/text/ttf.c:82 `if (off + 4 > (unsigned)len) continue;`. With off=0xFFFFFFFD the sum wraps to 1, the check passes, and `rd16(cm+off)` touches cm+4 GB.
Patch:
```
        if (off + 4 > (unsigned)len) continue;
→
        if (off > (unsigned)len - 4) continue;
```
(len≥4 is already guaranteed at :74.) Related, LOW: ADDG at :94 uses `nhm - 1` when nhm==0, which reads hmtx-4. Use `ai_ = nhm > 0 ? (gi_ < nhm ? gi_ : nhm - 1) : -1;` and `adv_ = ai_ >= 0 && ...`.
Not integer, but noted: composite glyph recursion (depth 6, 64 components per level, :219) allows 64^6 outline calls, so a crafted font can hang text rendering. Consider a global per-glyph counter (stop after 4096 components).

## 10. GPT entry reads in ntfsk.c run past the 512-byte sector. LOW, 70%
fs/ntfs/ntfsk.c:55-61 allows any esz from 128 to 512 that is not a multiple of 8 (e.g. 129). Then `off=byte%512` can be 511, and `s[off+40..47]` reads past `u8 s[512]` on the stack. Also, `*nsect=(lz-f+1)*r` with lz<f gives an enormous nsect, which removes the protection patch 8 adds.
Patch :55 `if(esz<128||esz>512)return -1;` → `if(esz<128||esz>512||(esz&7))return -1;`. In the loop add `if(off+128>512)continue;` and, before nk_is_ntfs, add `if(lz<f)continue;`.

---
Not reported (already in the v11.02 "not fixed" list): WOFF2/AVIF overflows, and direct dereference of syscall user pointers (e.g. lx_rwv dev 13, lx_un_msg ctl/iov).
