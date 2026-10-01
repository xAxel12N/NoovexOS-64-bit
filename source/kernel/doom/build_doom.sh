#!/bin/sh
# Build the Doom engine + libc shim into a single relocatable object (doom_blob.o)
# with only the kernel<->doom interface symbols global. Compiled WITH SSE.
set -e
GCCINC=$(echo | gcc -m64 -E -Wp,-v - 2>&1 | grep 'gcc.*include$' | head -1 | tr -d ' ')
DCFLAGS="-m64 -O2 -ffreestanding -fno-pie -fno-stack-protector -fno-asynchronous-unwind-tables \
         -mno-red-zone -mcmodel=small -nostdinc -isystem $GCCINC -Ishim -I. \
         -DDOOMGENERIC_RESX=640 -DDOOMGENERIC_RESY=400 -w"
rm -rf obj; mkdir -p obj
for f in *.c; do case "$f" in test_host.c) continue;; esac; gcc $DCFLAGS -c "$f" -o "obj/${f%.c}.o"; done
gcc $DCFLAGS -c webp_stub.c -o obj/webp_stub.o
# combine into one relocatable object
ld -r -o doom_blob_all.o obj/*.o
# keep only the interface symbols global; localize the rest of the defined symbols
objcopy \
  --keep-global-symbol=doomgeneric_Create \
  --keep-global-symbol=doomgeneric_Tick \
  --keep-global-symbol=DG_ScreenBuffer \
  --keep-global-symbol=dsh_set_output \
  --keep-global-symbol=dsh_set_file \
  --keep-global-symbol=dsh_set_exit \
  --keep-global-symbol=webp_decode \
  doom_blob_all.o doom_blob.o
echo "doom_blob.o built:"; size doom_blob.o 2>/dev/null | tail -1
echo "--- global/undefined symbols in blob ---"; nm doom_blob.o | grep -E ' T | U | D | B ' | grep -viE ' t | d | b | r ' | head -40
