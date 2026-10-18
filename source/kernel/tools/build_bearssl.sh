#!/bin/sh
SRC=$1; OUT=$2; CC=$3
rm -rf "$OUT"; mkdir -p "$OUT/obj"
for f in $(find "$SRC/src" -name '*.c' | sort); do
  o="$OUT/obj/$(echo "$f" | sed "s#.*/src/##; s#/#_#g; s#\.c\$#.o#")"
  echo "$f $o"
done | xargs -P "$(nproc 2>/dev/null || echo 4)" -n 2 sh -c "$CC -O2 -w -DBR_USE_GETENTROPY=1 -DBR_USE_URANDOM=1 -I$SRC/inc -I$SRC/src -c \$0 -o \$1" || exit 1
ar rcs "$OUT/libbearssl.a" "$OUT"/obj/*.o
