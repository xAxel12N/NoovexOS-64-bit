#!/bin/sh
set -e
R=$(cd "$(dirname "$0")/.." && pwd)
T=$R/third_party/netsurf
W=${1:-$R/build/netsurf}; mkdir -p "$W"; W=$(cd "$W" && pwd)
PS=${2:-$R/build/ports2/sys}; PS=$(cd "$PS" && pwd)
export NVX_MUSL=${NVX_MUSL:-$R/build/musl} NVX_MUSL_SH=${NVX_MUSL_SH:-$R/build/musl-sh} NVX_PSYS=$PS
MCC=$R/tools/mcc-port
J=$(nproc 2>/dev/null || echo 4)
S=$PS
export PKG_CONFIG_LIBDIR=$S/usr/lib/pkgconfig:$S/usr/share/pkgconfig PKG_CONFIG_SYSROOT_DIR=$S PKG_CONFIG_PATH=
step(){ [ -f "$W/.$1" ]; }
done_(){ touch "$W/.$1"; }
CM="-DCMAKE_SYSTEM_NAME=Linux -DCMAKE_SYSTEM_PROCESSOR=x86_64 -DCMAKE_C_COMPILER=$MCC -DCMAKE_AR=/usr/bin/ar -DCMAKE_RANLIB=/usr/bin/ranlib -DCMAKE_INSTALL_PREFIX=/usr -DCMAKE_INSTALL_LIBDIR=lib -DCMAKE_FIND_ROOT_PATH=$S -DCMAKE_FIND_ROOT_PATH_MODE_PROGRAM=NEVER -DCMAKE_FIND_ROOT_PATH_MODE_LIBRARY=ONLY -DCMAKE_FIND_ROOT_PATH_MODE_INCLUDE=ONLY -DCMAKE_POSITION_INDEPENDENT_CODE=ON -DBUILD_SHARED_LIBS=OFF -DCMAKE_BUILD_TYPE=Release -DCMAKE_C_FLAGS=-O2"

if ! step jpeg; then
  rm -rf "$W/jpeg"; mkdir -p "$W/jpeg"; cd "$W/jpeg"
  cmake "$T/libjpeg-turbo" $CM -DENABLE_SHARED=OFF -DENABLE_STATIC=ON -DWITH_SIMD=OFF -DWITH_TURBOJPEG=OFF -DWITH_TOOLS=OFF -DWITH_TESTS=OFF > conf.log 2>&1
  make -j$J > make.log 2>&1; make install DESTDIR="$S" > inst.log 2>&1
  done_ jpeg
fi

if ! step png; then
  rm -rf "$W/png"; mkdir -p "$W/png"; cd "$W/png"
  cmake "$T/libpng" $CM -DPNG_SHARED=OFF -DPNG_STATIC=ON -DPNG_TESTS=OFF -DPNG_TOOLS=OFF -DPNG_FRAMEWORK=OFF -DZLIB_ROOT="$S/usr" -DZLIB_LIBRARY="$S/usr/lib/libz.a" -DZLIB_INCLUDE_DIR="$S/usr/include" > conf.log 2>&1
  make -j$J > make.log 2>&1; make install DESTDIR="$S" > inst.log 2>&1
  done_ png
fi

if ! step freetype; then
  rm -rf "$W/freetype"; mkdir -p "$W/freetype"; cd "$W/freetype"
  cmake "$T/freetype" $CM -DFT_DISABLE_BZIP2=ON -DFT_DISABLE_BROTLI=ON -DFT_DISABLE_HARFBUZZ=ON -DFT_DISABLE_PNG=ON -DFT_REQUIRE_ZLIB=ON -DZLIB_LIBRARY="$S/usr/lib/libz.a" -DZLIB_INCLUDE_DIR="$S/usr/include" > conf.log 2>&1
  make -j$J > make.log 2>&1; make install DESTDIR="$S" > inst.log 2>&1
  done_ freetype
fi

if ! step utf8proc; then
  rm -rf "$W/utf8proc"; cp -a "$T/utf8proc" "$W/utf8proc"; cd "$W/utf8proc"
  $MCC -O2 -fPIC -DUTF8PROC_STATIC -c utf8proc.c -o utf8proc.o
  rm -f "$S/usr/lib/libutf8proc.a"; ar rcs "$S/usr/lib/libutf8proc.a" utf8proc.o
  cp utf8proc.h "$S/usr/include/"
  mkdir -p "$S/usr/lib/pkgconfig"
  printf 'prefix=/usr\nlibdir=${prefix}/lib\nincludedir=${prefix}/include\n\nName: libutf8proc\nDescription: UTF-8 processing\nVersion: 2.12.0\nLibs: -L${libdir} -lutf8proc\nCflags: -I${includedir} -DUTF8PROC_STATIC\n' > "$S/usr/lib/pkgconfig/libutf8proc.pc"
  done_ utf8proc
fi

if ! step expat; then
  rm -rf "${W:?}/expat"; mkdir -p "$W/expat"; cd "$W/expat"
  cmake "$T/expat" $CM -DEXPAT_BUILD_TOOLS=OFF -DEXPAT_BUILD_EXAMPLES=OFF -DEXPAT_BUILD_TESTS=OFF -DEXPAT_BUILD_DOCS=OFF -DEXPAT_SHARED_LIBS=OFF -DEXPAT_BUILD_FUZZERS=OFF > conf.log 2>&1
  make -j$J > make.log 2>&1; make install DESTDIR="$S" > inst.log 2>&1
  done_ expat
fi

mkdir -p "$W/host"; export PATH="$W/host:$PATH"
if ! step gperf; then
  G=$T/gperf; printf '#define HAVE_DYNAMIC_ARRAY 1\n' > "$W/host/config.h"
  g++ -O2 -w -I"$W/host" -I"$G/lib" -I"$G/src" -o "$W/host/gperf" "$G"/src/*.cc "$G"/lib/*.cc "$G/lib/getopt.c" "$G/lib/getopt1.c" -lm
  done_ gperf
fi
BS=$W/bs/share/netsurf-buildsystem
if ! step bs; then
  rm -rf "${W:?}/bs"; make -C "$T/buildsystem" install PREFIX="$W/bs" > "$W/bs.log" 2>&1
  done_ bs
fi
nslib(){
  L=$1; shift
  if ! step "$L"; then
    rm -rf "${W:?}/${L:?}"; cp -a "$T/$L" "$W/$L"
    make -C "$W/$L" PREFIX=/usr DESTDIR="$S" NSSHARED="$BS" CC="$MCC" AR=ar BUILD_CC=gcc HOST=x86_64-linux-musl COMPONENT_TYPE=lib-static "$@" install > "$W/$L.log" 2>&1
    done_ "$L"
  fi
}
nslib libwapcaplet
nslib libparserutils
nslib libhubbub
nslib libdom
nslib libcss
nslib libnsgif
nslib libnsbmp
nslib libnsutils
nslib libnspsl
nslib libsvgtiny
nslib libnsfb

[ -f "$S/usr/lib/libiconv.a" ] || ar rcs "$S/usr/lib/libiconv.a"
if ! step netsurf; then
  rm -rf "${W:?}/netsurf"; cp -a "$T/netsurf" "$W/netsurf"
  cat > "$W/netsurf/Makefile.config" <<'CFG'
override NETSURF_USE_DUKTAPE := NO
override NETSURF_USE_OPENSSL := NO
override NETSURF_USE_LIBICONV_PLUG := NO
override NETSURF_USE_ROSPRITE := NO
override NETSURF_USE_WEBP := NO
override NETSURF_USE_JPEGXL := NO
override NETSURF_USE_NSLOG := NO
override NETSURF_USE_NSSVG := YES
override NETSURF_USE_UTF8PROC := YES
override NETSURF_FB_FRONTEND := noovex
override NETSURF_FB_FONTLIB := freetype
override NETSURF_FB_FONTPATH := /usr/share/fonts/truetype/dejavu
override NETSURF_FB_RESPATH := $${HOME}/.netsurf/:$${NETSURFRES}:/usr/share/netsurf
override NETSURF_FRAMEBUFFER_RESOURCES := /usr/share/netsurf
CFG
  make -C "$W/netsurf" -j$J TARGET=framebuffer PREFIX=/usr NSSHARED="$BS" CC="$MCC" BUILD_CC=gcc HOST=x86_64-linux-musl Q= > "$W/netsurf.log" 2>&1
  done_ netsurf
fi

O=$W/root; rm -rf "${O:?}"; mkdir -p "$O"
make -C "$W/netsurf" TARGET=framebuffer PREFIX=/usr NSSHARED="$BS" CC="$MCC" BUILD_CC=gcc HOST=x86_64-linux-musl DESTDIR="$O" install > "$W/install.log" 2>&1
strip --strip-unneeded "$O"/usr/bin/* 2>/dev/null || true
mkdir -p "$O/usr/share/fonts/truetype/dejavu"
cp "$R"/third_party/fonts/dejavu/*.ttf "$R/third_party/fonts/dejavu/LICENSE" "$O/usr/share/fonts/truetype/dejavu/"
ln -sf netsurf-fb "$O/usr/bin/netsurf"
du -sh "$O"
