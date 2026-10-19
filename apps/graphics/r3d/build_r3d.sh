#!/bin/sh
set -e
cd "$(dirname "$0")"
GCCINC=$(echo | gcc -m64 -E -Wp,-v - 2>&1 | grep 'gcc.*include$' | head -1 | tr -d ' ')
RFLAGS="-m64 -O2 -std=gnu++20 -ffreestanding -fno-pie -fno-stack-protector -fno-asynchronous-unwind-tables -fno-unwind-tables \
        -fno-exceptions -fno-rtti -fno-threadsafe-statics -fno-use-cxa-atexit -mno-red-zone -mcmodel=small -mstackrealign \
        -fno-math-errno -fno-trapping-math -nostdinc -isystem $GCCINC -fno-builtin -Wall -Wextra"
g++ $RFLAGS -c r3d.cpp -o r3d_all.o
objcopy --keep-global-symbol=r3d_arena_need --keep-global-symbol=r3d_init --keep-global-symbol=r3d_work_bytes \
  --keep-global-symbol=r3d_frame --keep-global-symbol=r3d_scene_name r3d_all.o r3d_blob.o
rm -f r3d_all.o
U=$(nm r3d_blob.o | grep ' U ' | awk '{print $2}' | grep -v -x -e memset -e memcpy -e memmove || true)
if [ -n "$U" ]; then echo "r3d_blob.o needs symbols the kernel does not give it: $U"; exit 1; fi
echo "r3d_blob.o built:"; size r3d_blob.o | tail -1
