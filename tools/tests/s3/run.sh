#!/bin/sh
set -e
K="$1"; O="${2:-/tmp/nvx_s3t}"; mkdir -p "$O"
F="-m64 -O1 -ffreestanding -fno-pie -fno-stack-protector -mno-red-zone -mno-sse -fno-asynchronous-unwind-tables -nostdlib"
D=$(dirname "$0")
gcc $F -c "$D/boot.S" -o "$O/boot.o"
gcc $F -c "$K/arch/x86_64/boot/s3_wake.S" -o "$O/s3w.o"
gcc $F -Wall -Werror -c "$D/s3t.c" -o "$O/s3t.o"
ld -m elf_x86_64 -nostdlib -no-pie -z noexecstack -T "$D/link.ld" -o "$O/s3t.elf" "$O/boot.o" "$O/s3w.o" "$O/s3t.o"
objcopy -O binary "$O/s3t.elf" "$O/s3t.bin"
cp "$O/s3t.bin" "$O/s3t.img"
python3 "$D/drive.py" "$O"
