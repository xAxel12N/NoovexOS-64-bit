#!/bin/sh
# NoovexOS 64 - build the flat kernel binary.
# Produces kernel64.bin (flat image loaded by stage2 at 0x100000).
set -e

CFLAGS="-m64 -Os -ffreestanding -fno-pie -fno-stack-protector \
        -fno-asynchronous-unwind-tables -mno-red-zone -mno-mmx -mno-sse -mno-sse2 \
        -mcmodel=small -DNOOVEX64 -Wall -Wextra -I."

echo "[1/6] assemble boot64.S"
gcc $CFLAGS -c arch/x86_64/boot/boot64.S -o boot64.o

echo "[2/6] assemble isr64.S"
gcc $CFLAGS -c arch/x86_64/boot/isr64.S -o isr64.o
gcc $CFLAGS -c arch/x86_64/boot/userspace.S -o userspace.o
gcc $CFLAGS -c arch/x86_64/boot/ap_trampoline.S -o ap_trampoline.o

echo "[3/6] compile kernel64.c"
gcc $CFLAGS -c kernel64.c -o kernel64.o

echo "[3a] compile Rune graphics module (rune_gfx.rn) with the Rune compiler"
./rune build rune_gfx.rn --freestanding --target=x86_64-none -o rune_gfx.o >/dev/null 2>&1 || { echo "RUNE BUILD FAILED"; exit 1; }

echo "[3a2] embed RTL8821CE firmware blob"
ld -r -b binary -o rtw_fw.o rtw8821c_fw.bin
echo "[3a3] embed myboot bootloader + boot.cfg (for the disk installer)"
ld -r -b binary -o inst_boot.o inst_boot.bin
ld -r -b binary -o inst_stage2.o inst_stage2.bin
ld -r -b binary -o inst_cfg.o inst_boot.cfg
echo "[efi] rebuild UEFI bootloader (BOOTX64.EFI) and embed it for the installer"
( cd ../bootloader/uefi &&   gcc -I. -fpic -ffreestanding -fno-stack-protector -fno-stack-check -fshort-wchar -mno-red-zone -m64 -c uefiboot.c -o uefiboot.o &&   gcc -ffreestanding -c dummyreloc.S -o dummyreloc.o &&   ld -nostdlib -znocombreloc -shared -Bsymbolic -T efi.ld uefiboot.o dummyreloc.o -o uefiboot.so 2>/dev/null &&   objcopy -j .text -j .data -j .reloc --target=pei-x86-64 --subsystem=10 uefiboot.so BOOTX64.EFI &&   echo "  BOOTX64.EFI rebuilt" ) || echo "  (EFI build skipped)"
cp ../bootloader/uefi/BOOTX64.EFI inst_efi.bin
ld -r -b binary -o inst_efi.o inst_efi.bin
echo "[u] build the user-space demo program (crt0 + mini-libc)"
UFLAGS="-m64 -ffreestanding -nostdlib -fno-pie -fno-stack-protector -mno-red-zone -mcmodel=large -Os -Iuser/src"
gcc $UFLAGS -c user/src/user_crt0.S -o user/src/user_crt0.o
gcc $UFLAGS -c user/src/user_demo.c  -o user/src/user_demo.o
gcc $UFLAGS -c user/src/user_fork.c  -o user/src/user_fork.o
gcc $UFLAGS -c user/src/user_app.c   -o user/src/user_app.o
gcc $UFLAGS -c user/src/user_thread.c -o user/src/user_thread.o
gcc $UFLAGS -c user/src/user_preempt.c -o user/src/user_preempt.o
gcc $UFLAGS -c user/src/user_ipc.c -o user/src/user_ipc.o
gcc $UFLAGS -c user/src/user_wmapp.c -o user/src/user_wmapp.o
gcc $UFLAGS -c user/src/user_compdemo.c -o user/src/user_compdemo.o
gcc $UFLAGS -c user/src/user_posix.c -o user/src/user_posix.o
gcc $UFLAGS -c user/src/user_mem.c -o user/src/user_mem.o
ld -m elf_x86_64 -nostdlib -no-pie -T user/src/user.ld -o user/user_demo.elf user/src/user_crt0.o user/src/user_demo.o
ld -r -b binary -o user_elf.o user/user_demo.elf
ld -m elf_x86_64 -nostdlib -no-pie -T user/src/user.ld -o user/user_fork.elf user/src/user_crt0.o user/src/user_fork.o
ld -r -b binary -o user_fork_elf.o user/user_fork.elf
echo "[lua] compile Lua 5.5.1 + nvx libc"
LUAF="-m64 -Os -ffreestanding -fno-pie -fno-stack-protector -fno-asynchronous-unwind-tables -mno-red-zone -mcmodel=small -msse -msse2 -mfpmath=sse -w -Ilua/inc -Ilua/src"
for f in lua/src/*.c lua/nvxlibc.c lua/nvxlua.c; do gcc $LUAF -c "$f" -o "${f%.c}.o" || { echo "LUA BUILD FAILED: $f"; exit 1; }; done
ld -r -o lua_all.o lua/src/*.o lua/nvxlibc.o lua/nvxlua.o
ld -m elf_x86_64 -nostdlib -no-pie -T user/src/user.ld -o user/user_app.elf user/src/user_crt0.o user/src/user_app.o
ld -r -b binary -o user_app_elf.o user/user_app.elf
ld -m elf_x86_64 -nostdlib -no-pie -T user/src/user.ld -o user/user_thread.elf user/src/user_crt0.o user/src/user_thread.o
ld -r -b binary -o user_thread_elf.o user/user_thread.elf
ld -m elf_x86_64 -nostdlib -no-pie -T user/src/user.ld -o user/user_preempt.elf user/src/user_crt0.o user/src/user_preempt.o
ld -r -b binary -o user_preempt_elf.o user/user_preempt.elf
ld -m elf_x86_64 -nostdlib -no-pie -T user/src/user.ld -o user/user_ipc.elf user/src/user_crt0.o user/src/user_ipc.o
ld -r -b binary -o user_ipc_elf.o user/user_ipc.elf
ld -m elf_x86_64 -nostdlib -no-pie -T user/src/user.ld -o user/user_wmapp.elf user/src/user_crt0.o user/src/user_wmapp.o
ld -r -b binary -o user_wmapp_elf.o user/user_wmapp.elf
ld -m elf_x86_64 -nostdlib -no-pie -T user/src/user.ld -o user/user_compdemo.elf user/src/user_crt0.o user/src/user_compdemo.o
ld -r -b binary -o user_compdemo_elf.o user/user_compdemo.elf
ld -m elf_x86_64 -nostdlib -no-pie -T user/src/user.ld -o user/user_posix.elf user/src/user_crt0.o user/src/user_posix.o
ld -r -b binary -o user_posix_elf.o user/user_posix.elf
ld -m elf_x86_64 -nostdlib -no-pie -T user/src/user.ld -o user/user_mem.elf user/src/user_crt0.o user/src/user_mem.o
ld -r -b binary -o user_mem_elf.o user/user_mem.elf
for p in count spin sigdemo upper greet pipetest nvxbox init libc8; do
  gcc $UFLAGS -c user/src/$p.c -o user/src/$p.o || { echo "USER BUILD FAILED: $p"; exit 1; }
  ld -m elf_x86_64 -nostdlib -no-pie -T user/src/user.ld -o user/$p.elf user/src/user_crt0.o user/src/$p.o 2>/dev/null || true
  ld -r -b binary -o user_${p}_bin.o user/$p.elf
  objcopy --rename-section .data=.rodata,alloc,load,readonly,data,contents user_${p}_bin.o
done

echo "[3b] build Doom engine blob (doomgeneric + FreeDoom libc shim, SSE)"
( cd doom && sh build_doom.sh >/dev/null 2>&1 )

echo "[4/6] link (elf_x86_64, base 0x100000)"
ld -m elf_x86_64 -nostdlib -no-pie -z noexecstack -T arch/x86_64/kernel64.ld \
   -o kernel64.elf boot64.o isr64.o userspace.o ap_trampoline.o kernel64.o rune_gfx.o rtw_fw.o inst_boot.o inst_stage2.o inst_cfg.o inst_efi.o user_elf.o user_fork_elf.o user_app_elf.o user_thread_elf.o user_preempt_elf.o user_ipc_elf.o user_wmapp_elf.o user_compdemo_elf.o user_posix_elf.o user_mem_elf.o user_count_bin.o user_spin_bin.o user_sigdemo_bin.o user_upper_bin.o user_greet_bin.o user_pipetest_bin.o user_nvxbox_bin.o user_init_bin.o user_libc8_bin.o lua_all.o doom/doom_blob.o

echo "[5/6] objcopy -> flat binary"
objcopy -O binary kernel64.elf kernel64.bin

echo "[6/6] stamp size at offset 24 (Multiboot load_end_addr slot)"
python3 - <<'PY'
import struct
b=bytearray(open('kernel64.bin','rb').read())
assert len(b)<=8388608, f"kernel {len(b)} > 8 MiB (myboot FAT load limit)"
struct.pack_into('<I', b, 24, len(b))

# --- stamp the build signature (find NVX64SIG magic, patch epoch/size/sha256) ---
import time, hashlib
off = b.find(b'NVX64SIG')
if off >= 0:
    import subprocess
    # resolve symbols from the ELF (links at 0x100000)
    ue=0; imm_end=0
    try:
        nm=subprocess.check_output(['nm','kernel64.elf']).decode()
        for line in nm.splitlines():
            parts=line.split()
            if len(parts)>=3 and parts[-1]=='uefi_entry': ue=int(parts[0],16)
            if len(parts)>=3 and parts[-1]=='__immutable_end': imm_end=int(parts[0],16)
    except Exception as e:
        print("  (nm failed:",e,")")
    # immutable region = [0x100000 .. __immutable_end)  -> offset 0..imm_sz in the flat file
    imm_sz = (imm_end - 0x100000) if imm_end>0x100000 else len(b)
    if imm_sz>len(b): imm_sz=len(b)
    # struct layout: magic[8] version[16] epoch(u32) size(u32) sha256[32] builder[16] uefi(u64)
    epoch_off = off + 8 + 16
    size_off  = epoch_off + 4
    hash_off  = size_off + 4
    ue_off    = hash_off + 32 + 16
    struct.pack_into('<I', b, epoch_off, int(time.time()))
    struct.pack_into('<I', b, size_off, len(b))      # image_size = full flat size (for loading/display)
    struct.pack_into('<Q', b, ue_off, ue)
    full_off  = ue_off + 8
    for i in range(32): b[hash_off+i] = 0
    for i in range(32): b[full_off+i] = 0
    full = hashlib.sha256(bytes(b)).digest()
    b[full_off:full_off+32] = full
    digest = hashlib.sha256(bytes(b[:imm_sz])).digest()
    b[hash_off:hash_off+32] = digest
    print(f"  build signature stamped @ {off}: sha256={digest[:8].hex()}... imm_size={imm_sz} file={len(b)} uefi_entry=0x{ue:x}")
else:
    print("  WARNING: NVX64SIG signature block not found")

open('kernel64.bin','wb').write(b)
print(f"  kernel64.bin = {len(b)} bytes  (size stamped @24)")
PY
echo "done."
