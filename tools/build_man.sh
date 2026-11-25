#!/bin/sh
set -e
R=$(cd "$(dirname "$0")/.." && pwd)
T=$R/third_party
W=${1:-$R/build/man}; mkdir -p "$W"; W=$(cd "$W" && pwd)
export NVX_MUSL=${NVX_MUSL:-$R/build/musl} NVX_MUSL_SH=${NVX_MUSL_SH:-$R/build/musl-sh} NVX_PSYS=${2:-$R/build/ports2/sys}
case "$NVX_PSYS" in /*) ;; *) NVX_PSYS=$(cd "$NVX_PSYS" && pwd); export NVX_PSYS;; esac
MCC=$R/tools/mcc-port
if [ ! -f "$W/.mandoc" ]; then
  rm -rf "$W/mandoc"; cp -a "$T/mandoc" "$W/mandoc"; cd "$W/mandoc"
  { echo "CC=$MCC"; echo 'CFLAGS="-O2 -w -D_GNU_SOURCE"'; cat configure.noovex; } > configure.local
  ./configure > conf.log 2>&1
  make -j2 mandoc > make.log 2>&1
  touch "$W/.mandoc"
fi
O=$W/root; M=$O/usr/share/man; rm -rf "$O"; mkdir -p "$O/usr/bin" "$O/etc" "$O/usr/share/licenses/mandoc"
for s in 1 5 7 8; do mkdir -p "$M/man$s"; done
cp "$W/mandoc/mandoc" "$O/usr/bin/mandoc"; strip --strip-unneeded "$O/usr/bin/mandoc"
for l in man apropos whatis makewhatis; do ln -sf mandoc "$O/usr/bin/$l"; done
cp "$T/mandoc/LICENSE" "$O/usr/share/licenses/mandoc/LICENSE"
printf 'manpath /usr/share/man\nmanpath /usr/local/share/man\noutput indent 5\n' > "$O/etc/man.conf"
cd "$T/mandoc"; cp man.1 apropos.1 mandoc.1 "$M/man1/"; cp man.conf.5 mandoc.db.5 "$M/man5/"; cp man.7 mdoc.7 roff.7 tbl.7 eqn.7 mandoc_char.7 "$M/man7/"; cp makewhatis.8 "$M/man8/"
ln -sf apropos.1 "$M/man1/whatis.1"
V=$T/vim/runtime/doc; cp $V/vim.1 $V/vimdiff.1 $V/xxd.1 "$M/man1/"; for l in vi view ex rview; do ln -sf vim.1 "$M/man1/$l.1"; done
LV=$(sed -n 's/.*char version\[\] = "\([^"]*\)".*/\1/p' "$T/less/version.c" | head -1)
for p in less lessecho; do sed -e "s/@@VERSION@@/${LV:-unknown}/g" "$T/less/$p.nro.VER" > "$M/man1/$p.1"; done
cp "$T/xz/src/xz/xz.1" "$M/man1/"; for l in unxz xzcat lzma unlzma lzcat; do ln -sf xz.1 "$M/man1/$l.1"; done
cp "$T/dropbear/manpages/dropbear.8" "$M/man8/"; cp "$T/dropbear/manpages/dbclient.1" "$T/dropbear/manpages/dropbearkey.1" "$T/dropbear/manpages/dropbearconvert.1" "$M/man1/"; ln -sf dbclient.1 "$M/man1/ssh.1"
[ -f "$T/dropbear/manpages/scp.1" ] && cp "$T/dropbear/manpages/scp.1" "$M/man1/" || true
if command -v perl >/dev/null 2>&1; then
  cd "$T/curl/docs/cmdline-opts"
  D=$(awk '/^DPAGES = /{f=1;next} f{ if($0 !~ /\\$/){print; exit} print}' Makefile.inc | tr -d '\\')
  perl ../../scripts/managen -d . -I ../../include mainpage $D > "$M/man1/curl.1" 2>/dev/null || rm -f "$M/man1/curl.1"
fi
python3 "$R/tools/mkman.py" "$R" "$M"
find "$M" -type f -name '*.[1-8]' -exec gzip -9nf {} +
find "$M" -type l -name '*.[1-8]' | while read l; do t=$(readlink "$l"); rm -f "$l"; ln -sf "$t.gz" "$l.gz"; done
L="$NVX_MUSL_SH/lib/libc.so"
if [ -x "$L" ]; then ln -sf "$W/mandoc/mandoc" "$W/makewhatis"; "$L" "$W/makewhatis" "$M" || echo "makewhatis failed: apropos needs makewhatis run once on the system"; fi
echo "man: $(find "$M" -name '*.gz' | wc -l) pages, $(du -sb "$M" | cut -f1) bytes in /usr/share/man"
