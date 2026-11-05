#!/bin/bash
set -e
OUT=$(realpath -m "$1")
SRC=$(realpath third_party/mlibc)
B=$(realpath -m build/mlibc-obj)
if [ -f $OUT/usr/mlibc/lib/libc.so ] && [ -f $OUT/.stamp ] && [ $OUT/.stamp -nt $SRC/sysdeps/noovex/generic/sysdeps.cpp ]; then exit 0; fi
command -v clang >/dev/null && command -v ld.lld >/dev/null && command -v ninja >/dev/null || { echo "  (clang, lld or ninja missing - skipping mlibc)"; exit 1; }
MESON="python3 $(realpath tools/meson/meson.py)"
K=$(realpath -m build/mlibc-khdr); mkdir -p $K
for d in linux asm-generic mtd rdma sound video xen drm misc scsi; do [ -d /usr/include/$d ] && ln -sfn /usr/include/$d $K/$d; done
ln -sfn /usr/include/x86_64-linux-gnu/asm $K/asm
[ -d $B ] && find $B -mindepth 1 -delete
$MESON setup --cross-file $SRC/ci/noovex-x86_64.cross-file -Dlinux_kernel_headers=$K -Duse_freestnd_hdrs=enabled -Ddefault_library=both -Ddefault_library_paths=/usr/mlibc/lib --prefix=/usr/mlibc --buildtype=release $B $SRC >/dev/null
ninja -C $B >/dev/null
mkdir -p $OUT; DESTDIR=$OUT ninja -C $B install >/dev/null
llvm-strip --strip-unneeded $OUT/usr/mlibc/lib/*.so 2>/dev/null || true
touch $OUT/.stamp
echo "  mlibc built: $(ls $OUT/usr/mlibc/lib/*.so | wc -l) shared libraries, $(du -sh $OUT/usr/mlibc | cut -f1)"
