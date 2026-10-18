#!/bin/bash
set -e
OUT=$(realpath -m ${1:-build/tcltk-root}); PR=$(realpath -m ${2:-build/python-root})
SRC=$(realpath ../third_party_src/tcltk-8.6.tar.xz)
DY=$PR/usr/lib/python3.13/lib-dynload/_tkinter.cpython-313-x86_64-linux-gnu.so
[ -f "$OUT/usr/lib/libtk8.6.so" ] && [ -f "$DY" ] && exit 0
W=$(mktemp -d); tar xJf "$SRC" -C "$W"; mkdir -p "$W/bt" "$W/bk"
( cd "$W/bt" && ../tcl86/unix/configure --prefix=/usr --enable-shared --disable-threads >/dev/null && make -j"$(nproc)" >/dev/null && make install DESTDIR="$OUT" >/dev/null )
( cd "$W/bk" && ../tk86/unix/configure --prefix=/usr --enable-shared --with-tcl="$W/bt" --disable-xft >/dev/null && make -j"$(nproc)" >/dev/null && make install DESTDIR="$OUT" >/dev/null )
CPY=$(mktemp -d); tar xJf ../third_party_src/cpython-3.13.tar.xz -C "$CPY"
gcc -shared -fPIC -O2 -DWITH_APPINIT=1 -DPy_BUILD_CORE_MODULE -I "$CPY/cpython/Include" -I "$CPY/cpython/Include/internal" -I "$PR/usr/include/python3.13" -I "$OUT/usr/include" "$CPY/cpython/Modules/_tkinter.c" "$CPY/cpython/Modules/tkappinit.c" -L"$OUT/usr/lib" -ltk8.6 -ltcl8.6 -lX11 -o "$DY"
rm -rf "$W" "$CPY"
