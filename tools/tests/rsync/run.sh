#!/bin/sh
R=$(cd "$(dirname "$0")/../../.." && pwd)
W=${1:-/tmp/nvx-rsync-test}; mkdir -p "$W"; W=$(cd "$W" && pwd)
B=${RSYNC_BIN:-}
if [ -z "$B" ]; then
  if [ ! -x "$W/build/rsync" ]; then
    mkdir -p "$W/build"; cd "$W/build"
    "$R/third_party/rsync/configure" --disable-openssl --disable-xxhash --disable-zstd --disable-lz4 --disable-iconv --disable-acl-support --disable-xattr-support --disable-md2man --disable-roll-simd --with-included-zlib --with-included-popt > conf.log 2>&1 && make -j2 rsync > make.log 2>&1 || { echo "build FAIL"; tail -20 "$W/build/make.log" "$W/build/conf.log"; exit 1; }
  fi
  B=$W/build/rsync
fi
F=0
same(){ [ "$(cd "$1" && find . -printf '%y %p %m %l\n' | sort)" = "$(cd "$2" && find . -printf '%y %p %m %l\n' | sort)" ] || return 1; for f in $(cd "$1" && find . -type f); do cmp -s "$1/$f" "$2/$f" || return 1; done; }
ck(){ if [ "$1" = 0 ]; then echo "$2 ok"; else echo "$2 FAIL"; F=1; fi; }
T=$W/t; chmod -R u+w "$T" 2>/dev/null; rm -rf "$T"; mkdir -p "$T/src/sub/deep" "$T/src/empty"
echo hello > "$T/src/a"; chmod 640 "$T/src/a"; touch -d 2020-01-02 "$T/src/a"
ln -s a "$T/src/l"; ln "$T/src/a" "$T/src/hard"; mkfifo "$T/src/fifo"
dd if=/dev/urandom of="$T/src/sub/big" bs=1k count=700 2>/dev/null; printf 'x' > "$T/src/sub/deep/one"
"$B" -aH "$T/src/" "$T/d1/"; ck $? "local -aH copy"
same "$T/src" "$T/d1"; r=$?; [ -p "$T/d1/fifo" ] || r=1; ck $r "tree identical, fifo kept"
[ "$(stat -c '%a %Y' "$T/d1/a")" = "$(stat -c '%a %Y' "$T/src/a")" ]; ck $? "mode and mtime preserved"
[ "$(readlink "$T/d1/l")" = a ]; ck $? "symlink preserved"
[ "$(stat -c %i "$T/d1/a")" = "$(stat -c %i "$T/d1/hard")" ]; ck $? "hard link preserved"
dd if=/dev/urandom of="$T/src/sub/big" bs=1k count=4 seek=100 conv=notrunc 2>/dev/null
o=$("$B" -a -I --no-whole-file --stats "$T/src/" "$T/d1/"); r=$?; m=$(echo "$o" | awk -F': ' '/Matched data/{gsub(",","",$2); print $2+0}')
[ $r = 0 ] && [ "${m:-0}" -gt 600000 ] && cmp -s "$T/src/sub/big" "$T/d1/sub/big"; ck $? "delta transfer reuses unchanged blocks"
rm "$T/src/sub/deep/one"; "$B" -a --delete "$T/src/" "$T/d1/" && [ ! -e "$T/d1/sub/deep/one" ]; ck $? "--delete removes extraneous file"
P=$((20000 + $$ % 20000)); mkdir -p "$T/dm"
printf 'use chroot = no\naddress = 127.0.0.1\nport = %s\npid file = %s/dm/pid\nlock file = %s/dm/lock\n[m]\npath = %s/src\nread only = no\nuid = %s\ngid = %s\n' "$P" "$T" "$T" "$T" "$(id -u)" "$(id -g)" > "$T/dm/c.conf"
"$B" --daemon --no-detach --config="$T/dm/c.conf" & DP=$!
i=0; while [ $i -lt 50 ] && ! "$B" "rsync://127.0.0.1:$P/" >/dev/null 2>&1; do sleep 0.1; i=$((i+1)); done
"$B" "rsync://127.0.0.1:$P/" | grep -q '^m'; ck $? "daemon module list"
"$B" -a "rsync://127.0.0.1:$P/m/" "$T/d2/" && same "$T/src" "$T/d2"; ck $? "daemon pull"
"$B" -a "$T/d1/sub/" "rsync://127.0.0.1:$P/m/pushed/" && cmp -s "$T/d1/sub/big" "$T/src/pushed/big"; ck $? "daemon push"
kill $DP 2>/dev/null; wait $DP 2>/dev/null; rm -rf "$T/src/pushed"
"$B" -a -e "sh -c 'shift; exec \"\$@\"' rsh" --rsync-path="$B" "$T/src/" "remote:$T/d3/" && same "$T/src" "$T/d3"; ck $? "remote shell transport (RSYNC_RSH style)"
RSYNC_RSH="sh -c 'shift; exec \"\$@\"' rsh" "$B" -a --rsync-path="$B" "remote:$T/d3/" "$T/d4/" && same "$T/src" "$T/d4"; ck $? "RSYNC_RSH pull"
rm -rf "$T"
[ $F = 0 ] && echo "RSYNC HOST TEST: ALL PASS" || { echo "RSYNC HOST TEST: FAILURES"; exit 1; }
