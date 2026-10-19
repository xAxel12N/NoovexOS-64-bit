#!/bin/sh
set -e
cd "$(dirname "$0")"
GCCINC=$(echo | gcc -m64 -E -Wp,-v - 2>&1 | grep 'gcc.*include$' | head -1 | tr -d ' ')
QCFLAGS="-m64 -O2 -ffreestanding -fno-pie -fno-stack-protector -fno-asynchronous-unwind-tables \
         -mno-red-zone -mcmodel=small -nostdinc -isystem $GCCINC -Ishim -Isrc -fno-builtin -fno-strict-aliasing \
         -DQUAKEGENERIC_RES_X=640 -DQUAKEGENERIC_RES_Y=480 -w"
rm -rf obj; mkdir -p obj
for f in src/*.c quake_shim.c; do b=$(basename "$f" .c); gcc $QCFLAGS -c "$f" -o "obj/$b.o"; done
ld -r -o quake_blob_all.o obj/*.o
objcopy --keep-global-symbol=QG_Create --keep-global-symbol=QG_Tick --keep-global-symbol=qsh_set_output --keep-global-symbol=qsh_set_file \
  --keep-global-symbol=qsh_set_exit --keep-global-symbol=qsh_set_heap --keep-global-symbol=qsh_set_clock --keep-global-symbol=qsh_tick_us --keep-global-symbol=qsh_set_audio --keep-global-symbol=qsh_audio_pull --keep-global-symbol=key_dest --keep-global-symbol=key_lines \
  quake_blob_all.o quake_blob.o
rm -f quake_blob_all.o
echo "quake_blob.o built:"; size quake_blob.o | tail -1
nm quake_blob.o | grep ' U ' | head -40
