#!/bin/sh
R=$(cd "$(dirname "$0")/../../.." && pwd)
W=$(mktemp -d); trap 'rm -rf "$W"' EXIT
F=0; ok(){ echo "$1 ok"; }; bad(){ echo "$1 FAIL"; F=1; }
cp -a "$R/third_party/mandoc" "$W/m" && cd "$W/m" && printf 'CFLAGS="-O1 -w"\nMANPATH_DEFAULT=/usr/share/man\nBINM_PAGER=less\n' > configure.local && ./configure > conf.log 2>&1 && make -j2 mandoc > make.log 2>&1 && ok "host mandoc builds" || { bad "host mandoc builds"; tail -5 make.log; exit 1; }
M=$W/man; mkdir -p "$M/man1" "$M/man7"
python3 "$R/tools/mkman.py" "$R" "$M" > mk.log 2>&1 && ok "mkman generates pages ($(cat mk.log))" || { bad "mkman"; cat mk.log; exit 1; }
[ -f "$M/man7/noovex-shell.7" ] && ok "noovex-shell(7) exists" || bad "noovex-shell(7) exists"
n=$(ls "$M/man1" | wc -l); [ "$n" -ge 100 ] && ok "at least 100 shell/tool pages ($n)" || bad "at least 100 shell/tool pages ($n)"
E=0; for f in "$M"/man*/*; do ./mandoc -Tlint -Werror "$f" > lint.out 2>&1 || { E=$((E+1)); [ $E -le 3 ] && head -3 lint.out; }; done
[ $E -eq 0 ] && ok "generated pages lint clean (mandoc -Tlint -Werror)" || bad "generated pages lint clean ($E with errors)"
for p in man.1 apropos.1 mdoc.7; do ./mandoc -Tascii "$R/third_party/mandoc/$p" > /dev/null 2>&1 && ok "renders $p" || bad "renders $p"; done
for p in vim/runtime/doc/vim.1 xz/src/xz/xz.1 dropbear/manpages/dropbear.8; do ./mandoc -Tascii "$R/third_party/$p" | sed "s/.\x08//g" | grep -q NAME && ok "renders $p" || bad "renders $p"; done
gzip -9n "$M/man1/zstd.1"; ln -s mandoc man; ln -s mandoc makewhatis; ln -s mandoc apropos
./makewhatis "$M" && ok "makewhatis indexes" || bad "makewhatis indexes"
./apropos -M "$M" zstd | grep -q 'zstd(1)' && ok "apropos finds gzipped page" || bad "apropos finds gzipped page"
MANPAGER=cat ./man -M "$M" zstd | grep -q 'Zstandard' && ok "man reads gzipped page" || bad "man reads gzipped page"
MANPAGER=cat ./man -M "$M" wifi | grep -q 'NoovexOS shell' && ok "man wifi shows shell page" || bad "man wifi shows shell page"
[ $F -eq 0 ] && echo "MAN HOST TEST: ALL PASS" || { echo "MAN HOST TEST: FAILED"; exit 1; }
