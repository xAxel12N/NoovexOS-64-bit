#!/bin/bash
set -e
OUT=${1:-build/glibc-root}
SRC=third_party/src/glibc-2.44.tar.gz
[ -f "$OUT/lib64/libc.so.6" ] && exit 0
W=$(mktemp -d)
tar xzf "$SRC" -C "$W"
mkdir -p "$W/build"
( cd "$W/build" && ../glibc-2.44/configure --prefix=/usr --libdir=/usr/lib/x86_64-linux-gnu --libexecdir=/usr/lib/x86_64-linux-gnu --disable-werror --enable-kernel=4.4 --disable-nscd --disable-profile CFLAGS="-O2 -g0" >/dev/null && make -j"$(nproc)" >/dev/null && make install DESTDIR="$W/root" >/dev/null )
mkdir -p "$OUT" && cp -a "$W/root/." "$OUT/"
rm -rf "$W"
