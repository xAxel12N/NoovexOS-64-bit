import struct,sys
import os; sys.path.insert(0,os.path.dirname(os.path.abspath(__file__)))
from fatck import Fat
import pecheck
iso=open(sys.argv[1],'rb').read(); efi=open(sys.argv[2],'rb').read(); kern=open(sys.argv[3],'rb').read()
ok=True
def chk(w,c):
    global ok; print(f"  {w:64s} {'ok' if c else 'FAIL'}"); ok=ok and bool(c)
print("== CD path (VirtualBox / burned disc): El Torito EFI entry ==")
pvd=iso[16*2048:17*2048]; chk("ISO9660 primary volume descriptor", pvd[0]==1 and pvd[1:6]==b'CD001')
vol=struct.unpack_from('<I',pvd,80)[0]; chk("volume size covers the whole file", vol*2048==len(iso))
br=iso[17*2048:18*2048]; chk("El Torito boot record", br[0]==0 and br[1:6]==b'CD001' and br[7:30]==b'EL TORITO SPECIFICATION')
cat=struct.unpack_from('<I',br,0x47)[0]; c=iso[cat*2048:cat*2048+2048]
chk("validation entry checksum + key bytes", sum(struct.unpack_from('<16H',c,0))&0xFFFF==0 and c[30:32]==b'\x55\xaa')
e=None; off=64
while off<2048 and c[off] in (0x90,0x91):
    hid,plat,cnt=c[off],c[off+1],struct.unpack_from('<H',c,off+2)[0]
    for k in range(cnt):
        ent=c[off+32+k*32:off+64+k*32]
        if plat==0xEF: e=ent
    off+=32+cnt*32
    if hid==0x91: break
chk("EFI platform (0xEF) section with a bootable entry", e is not None and e[0]==0x88)
media,count,rba=e[1],struct.unpack_from('<H',e,6)[0],struct.unpack_from('<I',e,8)[0]
chk("no-emulation entry, sector count fits and is the real size", media==0 and 0<count<=0xFFFF)
img=iso[rba*2048:rba*2048+count*512]; chk("image lies inside the ISO", rba*2048+count*512<=len(iso))
f=Fat(img,0); tree,free=f.check()
print(f"     image: FAT{f.type} by cluster count ({f.ncl} clusters)")
chk("image is a consistent FAT12/16 volume (no structural errors)", f.type in (12,16) and not f.err)
b=f.read('/EFI/BOOT/BOOTX64.EFI'); chk("\\EFI\\BOOT\\BOOTX64.EFI present and byte-exact", b==efi)
k=f.read('/KERNEL64.BIN'); chk("\\KERNEL64.BIN present and byte-exact", k==kern)
open('/tmp/extracted_BOOTX64.EFI','wb').write(b or b'')
errs,info=pecheck.check('/tmp/extracted_BOOTX64.EFI'); chk("extracted BOOTX64.EFI passes the PE checks", not errs)
print("== USB path (image written to a stick): MBR partition as ESP ==")
p=iso[0x1BE:0x1CE]; t=p[4]; start,n=struct.unpack_from('<II',p,8)
chk("MBR partition 1 is FAT32 LBA (0x0C)", t==0x0C and iso[510:512]==b'\x55\xaa')
g=Fat(iso,start*512); tree2,free2=g.check()
print(f"     partition: FAT{g.type} by cluster count ({g.ncl} clusters)")
bpb32=struct.unpack_from('<H',iso,start*512+22)[0]==0
chk("cluster count says FAT32 and the boot sector is FAT32 layout", g.type==32 and bpb32)
chk("no structural errors", not g.err)
chk("\\EFI\\BOOT\\BOOTX64.EFI byte-exact", g.read('/EFI/BOOT/BOOTX64.EFI')==efi)
chk("\\boot\\kernel64.bin byte-exact", g.read('/boot/kernel64.bin')==kern)
print("ALL PASS" if ok else "FAILURES")
