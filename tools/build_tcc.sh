#!/bin/sh
set -e
SRC=$(cd "$1" && pwd); OUT=$2; MCC=$3; MS=${NVX_MUSL_SH:?}; M=${NVX_MUSL:?}
mkdir -p "$OUT"; OUT=$(cd "$OUT" && pwd)
CONF="--prefix=/usr --config-musl --config-pie --sysincludepaths=/usr/lib/tcc/include:/usr/local/include:/usr/lib/musl/include:/usr/include --libpaths=/usr/lib/tcc:/usr/local/lib:/usr/lib/musl/lib:/usr/lib --crtprefix=/usr/lib/musl/lib --elfinterp=/lib/ld-musl-x86_64.so.1"
rm -rf "$OUT/host" "$OUT/tgt" "$OUT/root"; mkdir -p "$OUT/host" "$OUT/tgt" "$OUT/root"
cd "$OUT/host"; "$SRC/configure" --cc=gcc --prefix=/usr --config-musl --config-pie --sysincludepaths="$SRC/include:$M/include" --libpaths="$OUT/host:$MS/lib" --crtprefix="$MS/lib" --elfinterp=/lib/ld-musl-x86_64.so.1 >/dev/null
make -j"$(nproc)" tcc libtcc1.a >/dev/null
cd "$OUT/tgt"; "$SRC/configure" --cc="$MCC" $CONF >/dev/null 2>&1
cp "$OUT/host/c2str.exe" "$OUT/host/tccdefs_.h" "$OUT/tgt/"; touch "$OUT/tgt/tccdefs_.h"; make -j"$(nproc)" tcc >/dev/null
R="$OUT/root"; mkdir -p "$R/usr/bin" "$R/usr/lib/tcc/include" "$R/usr/lib/musl"
cp "$OUT/tgt/tcc" "$R/usr/bin/tcc"; strip "$R/usr/bin/tcc"
cp "$OUT/host/libtcc1.a" "$R/usr/lib/tcc/"
for f in runmain.o bt-exe.o bt-log.o bcheck.o; do [ -f "$OUT/host/$f" ] && cp "$OUT/host/$f" "$R/usr/lib/tcc/"; done
cp "$SRC"/include/*.h "$R/usr/lib/tcc/include/"
cp -a "$M/include" "$R/usr/lib/musl/include"
mkdir -p "$R/usr/lib/musl/lib"; for f in Scrt1.o crt1.o crti.o crtn.o rcrt1.o; do cp "$MS/lib/$f" "$R/usr/lib/musl/lib/"; done
cp "$MS/lib/libc.so" "$R/usr/lib/musl/lib/libc.so"; strip --strip-unneeded "$R/usr/lib/musl/lib/libc.so"
for f in libm libpthread libdl librt libcrypt libutil libresolv libxnet; do cp "$MS/lib/$f.a" "$R/usr/lib/musl/lib/"; done
ln -sf tcc "$R/usr/bin/cc"
