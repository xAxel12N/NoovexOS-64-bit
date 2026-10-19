#!/bin/bash
set -e
OUT=$(realpath -m ${1:-build/python-root})
SRC=$(realpath third_party/src/cpython-3.13.tar.xz)
[ -x "$OUT/usr/bin/python3.13" ] && exit 0
W=$(mktemp -d)
tar xJf "$SRC" -C "$W"
mkdir -p "$W/build"
( cd "$W/build" && ../cpython/configure --prefix=/usr --without-ensurepip CFLAGS="-O2" >/dev/null && make -j"$(nproc)" >/dev/null 2>&1 && make install DESTDIR="$OUT" >/dev/null 2>&1 )
rm -rf "$W"
