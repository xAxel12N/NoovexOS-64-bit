#!/bin/sh
set -e
B=build
CFLAGS="-m64 -Os -ffreestanding -fno-pie -fno-stack-protector \
        -fno-asynchronous-unwind-tables -mno-red-zone -mno-mmx -mno-sse -mno-sse2 \
        -mcmodel=small -DNOOVEX64 -Wall -Wextra -I. -Werror=implicit-function-declaration -Werror=int-conversion -Werror=incompatible-pointer-types -Werror=return-type -Werror=shift-negative-value -Werror=empty-body -Werror=duplicated-branches -Werror=logical-op -Wduplicated-branches -Wlogical-op"
U="usr/src"
mkdir -p $B $B/user/src $B/user/libc $B/usr/src $B/usr/libc $B/lua/src $B/ntfs

echo "[1/6] assemble boot64.S"
gcc $CFLAGS -c arch/x86_64/boot/boot64.S -o $B/boot64.o

echo "[2/6] assemble isr64.S"
gcc $CFLAGS -c arch/x86_64/boot/isr64.S -o $B/isr64.o
gcc $CFLAGS -c arch/x86_64/boot/userspace.S -o $B/userspace.o
gcc $CFLAGS -c arch/x86_64/boot/ap_trampoline.S -o $B/ap_trampoline.o

echo "[3/6] compile kernel units (incremental, $(nproc 2>/dev/null || echo 4) at a time)"
CXXFLAGS="$(echo "$CFLAGS" | sed 's/-Werror=implicit-function-declaration//; s/-Werror=int-conversion//; s/-Werror=incompatible-pointer-types//') -std=gnu++20 -fno-exceptions -fno-rtti -fno-threadsafe-statics -fno-use-cxa-atexit -fno-unwind-tables -Wno-missing-field-initializers"
CFLAGS="$CFLAGS" CXXFLAGS="$CXXFLAGS" python3 tools/kbuild.py $B/units $KBUILD_ARGS || exit 1
UNITOBJS=$(cat $B/units/objects.txt)
echo "[3m] compile separately built kernel modules (drivers/*: network, VirtIO, sound)"
MODS="drivers/net/ethernet/e1000.c drivers/net/ethernet/rtl8139.c drivers/net/ethernet/rtl8169.c drivers/virtio/virtio.c drivers/virtio/virtio_net.c drivers/virtio/virtio_blk.c drivers/sound/ac97.c drivers/sound/hda.c"
MODOBJS=""
for m in $MODS; do o=$B/mod_$(basename $m .c).o; gcc $CFLAGS -c $m -o $o || { echo "MODULE BUILD FAILED: $m"; exit 1; }; MODOBJS="$MODOBJS $o"; done

echo "[3a] compile Rune graphics module (lang/rune/rune_gfx.rn) with the Rune compiler"
tools/rune build lang/rune/rune_gfx.rn --freestanding --target=x86_64-none -o $B/rune_gfx.o >/dev/null 2>&1 || { echo "RUNE BUILD FAILED"; exit 1; }

echo "[3a2] embed RTL8821CE firmware blob"
( cd assets/firmware && ld -r -b binary -o ../../$B/rtw_fw.o rtw8821c_fw.bin )
( cd assets && ld -r -b binary -o ../$B/doom_wad.o doom1.wad )
( cd assets && ld -r -b binary -o ../$B/wpf_data.o wpf_data.bin )
( cd assets && ld -r -b binary -o ../$B/lq_pak0.o lq_pak0.xz && ld -r -b binary -o ../$B/lq_pak1.o lq_pak1.xz )
( cd assets/nvidia && for f in gsp bootloader booter_load booter_unload acr_bl; do ld -r -b binary -o ../../$B/nvfw_$f.o nvfw_$f.xz; done )
sh third_party/quake/build_quake.sh >/dev/null && mv third_party/quake/quake_blob.o $B/quake_blob.o && rm -rf third_party/quake/obj
sh apps/graphics/r3d/build_r3d.sh >/dev/null && mv apps/graphics/r3d/r3d_blob.o $B/r3d_blob.o
echo "[3a3] embed myboot bootloader + boot.cfg (for the disk installer)"
( cd assets/installer && ld -r -b binary -o ../../$B/inst_boot.o inst_boot.bin && ld -r -b binary -o ../../$B/inst_stage2.o inst_stage2.bin && ld -r -b binary -o ../../$B/inst_cfg.o inst_boot.cfg )
echo "[efi] rebuild UEFI bootloader (BOOTX64.EFI) and embed it for the installer"
( cd boot/uefi &&   gcc -I. -fpic -ffreestanding -fno-stack-protector -fno-stack-check -fshort-wchar -mno-red-zone -m64 -Os -fno-jump-tables -Werror=implicit-function-declaration -c uefiboot.c -o uefiboot.o &&   gcc -ffreestanding -c dummyreloc.S -o dummyreloc.o &&   ld -nostdlib -znocombreloc -shared -Bsymbolic -T efi.ld uefiboot.o dummyreloc.o -o uefiboot.so 2>/dev/null &&   objcopy -j .text -j .data -j .reloc --target=pei-x86-64 --subsystem=10 uefiboot.so BOOTX64.EFI &&   echo "  BOOTX64.EFI rebuilt" ) || echo "  (EFI build skipped)"
cp boot/uefi/BOOTX64.EFI $B/inst_efi.bin
( cd $B && ld -r -b binary -o inst_efi.o inst_efi.bin )
echo "[u] build the user-space demo programs (crt0 + mini-libc)"
UFLAGS="-m64 -ffreestanding -nostdlib -fno-pie -fno-stack-protector -mno-red-zone -mcmodel=large -Os -Iusr/src"
gcc $UFLAGS -c $U/user_crt0.S -o $B/$U/user_crt0.o
for p in user_demo user_fork user_app user_thread user_preempt user_ipc user_wmapp user_compdemo user_posix user_mem; do gcc $UFLAGS -c $U/$p.c -o $B/$U/$p.o; done
emb(){ ( cd $B && ld -r -b binary -o $1 user/$2.elf ); }
embz(){ mkdir -p $B/zz/user; python3 tools/nvz.py $B/user/$2.elf $B/zz/user/$2.elf; ( cd $B/zz && ld -r -b binary -o ../$1 user/$2.elf ); }
for p in demo:user_elf fork:user_fork_elf app:user_app_elf thread:user_thread_elf preempt:user_preempt_elf ipc:user_ipc_elf wmapp:user_wmapp_elf compdemo:user_compdemo_elf posix:user_posix_elf mem:user_mem_elf; do
  n=${p%%:*}; o=${p##*:}
  ld -m elf_x86_64 -nostdlib -no-pie -T $U/user.ld -o $B/user/user_$n.elf $B/$U/user_crt0.o $B/$U/user_$n.o
  emb $o.o user_$n
done
echo "[lua] compile Lua 5.5.1 + nvx libc"
L=third_party/lua
LUAF="-m64 -Os -ffreestanding -fno-pie -fno-stack-protector -fno-asynchronous-unwind-tables -mno-red-zone -mcmodel=small -msse -msse2 -mfpmath=sse -w -I$L/inc -I$L/src"
for f in $L/src/*.c; do gcc $LUAF -c "$f" -o "$B/lua/src/$(basename ${f%.c}).o" || { echo "LUA BUILD FAILED: $f"; exit 1; }; done
for f in nvxlibc nvxlua; do gcc $LUAF -c $L/$f.c -o $B/lua/$f.o || { echo "LUA BUILD FAILED: $f"; exit 1; }; done
ld -r -o $B/lua_all.o $B/lua/src/*.o $B/lua/nvxlibc.o $B/lua/nvxlua.o
echo "[u2] build nvxlibc (standard headers + libc.a) for user programs"
LGINC=$(gcc -print-file-name=include)
LFLAGS="-m64 -ffreestanding -nostdinc -isystem usr/libc/include -isystem $LGINC -fno-pie -fno-stack-protector -mno-red-zone -mcmodel=large -Os -w -ffunction-sections -fdata-sections"
LB=$B/user/libc
for f in libc printf stdlib2 stdio2 sys_nvx; do gcc $LFLAGS -c usr/libc/src/$f.c -o $LB/$f.o || { echo "LIBC BUILD FAILED: $f"; exit 1; }; done
gcc $LFLAGS -c usr/libc/src/crt0.S -o $LB/crt0.o
rm -f $LB/libc.a; ar rcs $LB/libc.a $LB/libc.o $LB/printf.o $LB/stdlib2.o $LB/stdio2.o $LB/sys_nvx.o
for p in count spin sigdemo upper greet pipetest nvxbox init libc8; do
  gcc $LFLAGS -I$U -c $U/$p.c -o $B/$U/$p.o || { echo "USER BUILD FAILED: $p"; exit 1; }
  EXTRA=""; if [ "$p" = nvxbox ]; then gcc $LFLAGS -I$U -c $U/coreutils.c -o $B/$U/coreutils.o || { echo "USER BUILD FAILED: coreutils"; exit 1; }; EXTRA=$B/$U/coreutils.o; fi
  ld -m elf_x86_64 -nostdlib -no-pie --gc-sections -T $U/user.ld -o $B/user/$p.elf $LB/crt0.o $B/$U/$p.o $EXTRA $LB/libc.a || { echo "USER LINK FAILED: $p"; exit 1; }
  emb user_${p}_bin.o $p
  objcopy --rename-section .data=.rodata,alloc,load,readonly,data,contents $B/user_${p}_bin.o
done

echo "[musl] build musl libc 1.2.6 (static, position independent) for Linux-ABI programs"
M=$(pwd)/$B/musl
if [ ! -f $M/lib/libc.a ] || [ ! -f $M/lib/rcrt1.o ]; then
  rm -rf $B/musl-obj; mkdir -p $B/musl-obj
  ( cd $B/musl-obj && CC=gcc ../../third_party/musl/configure --prefix=$M --disable-shared --disable-wrapper CFLAGS="-fPIE -O2" >/dev/null && make -j"$(nproc 2>/dev/null || echo 4)" >/dev/null 2>&1 && make install >/dev/null 2>&1 ) || { echo "MUSL BUILD FAILED"; exit 1; }
fi
MCFLAGS="-fPIE -O2 -nostdinc -isystem $M/include -isystem $(gcc -print-file-name=include) -Wall -Wno-unused-result -Wno-format-zero-length"
for p in mhello musltest smptest nettest featest afutest; do
  gcc $MCFLAGS -c usr/musl/$p.c -o $B/user/musl_$p.o || { echo "MUSL PROGRAM BUILD FAILED: $p"; exit 1; }
  gcc -static-pie -nostdlib -Wl,--no-dynamic-linker -Wl,-z,noexecstack -o $B/user/$p.elf $M/lib/rcrt1.o $M/lib/crti.o $B/user/musl_$p.o $M/lib/libc.a $(gcc -print-libgcc-file-name) $M/lib/crtn.o || { echo "MUSL PROGRAM LINK FAILED: $p"; exit 1; }
  strip $B/user/$p.elf
  embz user_${p}_bin.o $p
  objcopy --rename-section .data=.rodata,alloc,load,readonly,data,contents $B/user_${p}_bin.o
done

echo "[musl-shared] build musl as a shared library (the dynamic linker /lib/ld-musl-x86_64.so.1) and a dynamic test program"
MS=$(pwd)/$B/musl-sh
if [ ! -f $MS/lib/libc.so ]; then
  rm -rf $B/musl-shobj; mkdir -p $B/musl-shobj
  ( cd $B/musl-shobj && CC=gcc ../../third_party/musl/configure --prefix=$MS --syslibdir=/lib --enable-shared --disable-static --disable-wrapper CFLAGS="-O2" >/dev/null && make -j"$(nproc 2>/dev/null || echo 4)" >/dev/null 2>&1 && make install-libs syslibdir=$MS/lib >/dev/null 2>&1 ) || { echo "MUSL SHARED BUILD FAILED"; exit 1; }
fi
cp $MS/lib/libc.so $B/user/libc_so.elf && strip $B/user/libc_so.elf
embz user_libc_so_bin.o libc_so
objcopy --rename-section .data=.rodata,alloc,load,readonly,data,contents $B/user_libc_so_bin.o
gcc -fPIC -O2 -nostdinc -isystem $M/include -isystem $(gcc -print-file-name=include) -Wall -c usr/musl/libnvxdemo.c -o $B/user/libnvxdemo.o || { echo "DEMO LIB BUILD FAILED"; exit 1; }
gcc -shared -nostdlib -Wl,-soname,libnvxdemo.so -o $B/user/libnvxdemo.so $B/user/libnvxdemo.o -L$MS/lib -lc $(gcc -print-libgcc-file-name) || { echo "DEMO LIB LINK FAILED"; exit 1; }
strip $B/user/libnvxdemo.so
python3 tools/bin2c.py $B/user/libnvxdemo.so libnvxdemo_so $B/user/libnvxdemo_so.h
gcc -fPIE -O2 -nostdinc -isystem $M/include -isystem $(gcc -print-file-name=include) -I$B/user -Wall -Wno-unused-result -c usr/musl/dyntest.c -o $B/user/musl_dyntest.o || { echo "DYNTEST BUILD FAILED"; exit 1; }
gcc -pie -nostdlib -Wl,-dynamic-linker,/lib/ld-musl-x86_64.so.1 -Wl,-z,noexecstack -o $B/user/dyntest.elf $MS/lib/Scrt1.o $MS/lib/crti.o $B/user/musl_dyntest.o -L$MS/lib -lc $(gcc -print-libgcc-file-name) $MS/lib/crtn.o || { echo "DYNTEST LINK FAILED"; exit 1; }
strip $B/user/dyntest.elf
embz user_dyntest_bin.o dyntest
objcopy --rename-section .data=.rodata,alloc,load,readonly,data,contents $B/user_dyntest_bin.o

echo "[ports] build SQLite, gzip (zlib), bzip2, xz, zstd, kilo, fetch (BearSSL) and qjs (QuickJS) as dynamic musl programs (cached in build/ports-dyn)"
export NVX_MUSL=$M
export NVX_MUSL_SH=$MS
MCC="$(pwd)/tools/mcc-dyn"
P=$B/ports-dyn; mkdir -p $P
TP=third_party
port(){ n=$1; shift; if [ ! -f $P/$n ]; then "$@" || { echo "PORT BUILD FAILED: $n"; exit 1; }; strip $P/$n; fi; }
port sqlite3 $MCC -O2 -w -DSQLITE_THREADSAFE=1 -DSQLITE_OMIT_LOAD_EXTENSION -DSQLITE_DEFAULT_MEMSTATUS=0 -I$TP/sqlite -o $P/sqlite3 $TP/sqlite/shell.c $TP/sqlite/sqlite3.c
Z=$TP/zlib
port gzip $MCC -O2 -w -I$Z -o $P/gzip $Z/test/minigzip.c $Z/adler32.c $Z/compress.c $Z/crc32.c $Z/deflate.c $Z/gzclose.c $Z/gzlib.c $Z/gzread.c $Z/gzwrite.c $Z/infback.c $Z/inffast.c $Z/inflate.c $Z/inftrees.c $Z/trees.c $Z/uncompr.c $Z/zutil.c
BZ=$TP/bzip2
port bzip2 $MCC -O2 -w -D_FILE_OFFSET_BITS=64 -o $P/bzip2 $BZ/blocksort.c $BZ/huffman.c $BZ/crctable.c $BZ/randtable.c $BZ/compress.c $BZ/decompress.c $BZ/bzlib.c $BZ/bzip2.c
XS=$TP/xz/src
XZDEF="-include $TP/xz/nvx_config.h"
XZINC="-I$XS/common -I$XS/liblzma/api -I$XS/liblzma/check -I$XS/liblzma/common -I$XS/liblzma/delta -I$XS/liblzma/lz -I$XS/liblzma/lzma -I$XS/liblzma/rangecoder -I$XS/liblzma/simple"
XZSRC=""; for f in common/tuklib_physmem.c common/tuklib_cpucores.c liblzma/common/hardware_cputhreads.c liblzma/common/outqueue.c liblzma/common/stream_encoder_mt.c liblzma/common/stream_decoder_mt.c common/tuklib_mbstr_nonprint.c common/tuklib_exit.c common/tuklib_mbstr_fw.c common/tuklib_mbstr_width.c common/tuklib_mbstr_wrap.c common/tuklib_open_stdxxx.c common/tuklib_progname.c liblzma/check/check.c liblzma/check/crc32_fast.c liblzma/check/crc64_fast.c liblzma/check/sha256.c liblzma/common/block_util.c liblzma/common/common.c liblzma/common/easy_preset.c liblzma/common/filter_common.c liblzma/common/hardware_physmem.c liblzma/common/index.c liblzma/common/stream_flags_common.c liblzma/common/string_conversion.c liblzma/common/vli_size.c liblzma/common/alone_encoder.c liblzma/common/block_buffer_encoder.c liblzma/common/block_encoder.c liblzma/common/block_header_encoder.c liblzma/common/easy_buffer_encoder.c liblzma/common/easy_encoder.c liblzma/common/easy_encoder_memusage.c liblzma/common/filter_buffer_encoder.c liblzma/common/filter_encoder.c liblzma/common/filter_flags_encoder.c liblzma/common/index_encoder.c liblzma/common/stream_buffer_encoder.c liblzma/common/stream_encoder.c liblzma/common/stream_flags_encoder.c liblzma/common/vli_encoder.c liblzma/common/alone_decoder.c liblzma/common/auto_decoder.c liblzma/common/block_buffer_decoder.c liblzma/common/block_decoder.c liblzma/common/block_header_decoder.c liblzma/common/easy_decoder_memusage.c liblzma/common/file_info.c liblzma/common/filter_buffer_decoder.c liblzma/common/filter_decoder.c liblzma/common/filter_flags_decoder.c liblzma/common/index_decoder.c liblzma/common/index_hash.c liblzma/common/stream_buffer_decoder.c liblzma/common/stream_decoder.c liblzma/common/stream_flags_decoder.c liblzma/common/vli_decoder.c liblzma/common/microlzma_encoder.c liblzma/common/microlzma_decoder.c liblzma/common/lzip_decoder.c liblzma/simple/simple_encoder.c liblzma/simple/simple_decoder.c liblzma/simple/simple_coder.c liblzma/simple/x86.c liblzma/simple/arm.c liblzma/simple/armthumb.c liblzma/simple/arm64.c liblzma/simple/powerpc.c liblzma/simple/ia64.c liblzma/simple/sparc.c liblzma/simple/riscv.c liblzma/lzma/lzma_encoder.c liblzma/lzma/lzma_encoder_optimum_fast.c liblzma/lzma/lzma_encoder_optimum_normal.c liblzma/lzma/fastpos_table.c liblzma/lzma/lzma2_encoder.c liblzma/lzma/lzma_decoder.c liblzma/lzma/lzma2_decoder.c liblzma/lzma/lzma_encoder_presets.c liblzma/lz/lz_encoder.c liblzma/lz/lz_encoder_mf.c liblzma/lz/lz_decoder.c liblzma/rangecoder/price_table.c liblzma/delta/delta_encoder.c liblzma/delta/delta_decoder.c liblzma/delta/delta_common.c xz/args.c xz/coder.c xz/file_io.c xz/hardware.c xz/main.c xz/message.c xz/mytime.c xz/options.c xz/sandbox.c xz/signals.c xz/suffix.c xz/util.c xz/list.c; do XZSRC="$XZSRC $XS/$f"; done
port xz $MCC -O2 -w -std=gnu11 $XZDEF $XZINC -o $P/xz $XZSRC
ZD=$TP/zstd
port zstd sh -c "$MCC -O2 -w -DZSTD_MULTITHREAD -DZSTD_NOBENCH -DZSTD_NODICT -DZSTD_LEGACY_SUPPORT=0 -DZSTD_DISABLE_ASM -DBACKTRACE_ENABLE=0 -DZSTD_NOTRACE -I$ZD/lib -I$ZD/lib/common -o $P/zstd $ZD/lib/common/*.c $ZD/lib/compress/*.c $ZD/lib/decompress/*.c $ZD/programs/fileio.c $ZD/programs/fileio_asyncio.c $ZD/programs/timefn.c $ZD/programs/util.c $ZD/programs/zstdcli.c $ZD/programs/zstdcli_trace.c $ZD/programs/benchfn.c $ZD/programs/datagen.c $ZD/programs/lorem.c"
BR=$TP/bearssl; BL=$B/bearssl
if [ ! -f $BL/libbearssl.a ]; then sh tools/build_bearssl.sh $BR $BL "$MCC" || { echo "BEARSSL BUILD FAILED"; exit 1; }; fi
$MCC -O2 -Wall -Wno-unused-result -I$BR/inc -Iusr/musl -o $P/fetch usr/musl/fetch.c usr/musl/nvxtls.c $BL/libbearssl.a || { echo "PORT BUILD FAILED: fetch"; exit 1; }; strip $P/fetch
port kilo $MCC -O2 -w -std=c99 -D_DEFAULT_SOURCE -D_BSD_SOURCE -o $P/kilo $TP/kilo/kilo.c
QJS=$TP/quickjs
port qjs $MCC -O2 -w -D_GNU_SOURCE -I$QJS -o $P/qjs $QJS/qjs.c $QJS/quickjs.c $QJS/libregexp.c $QJS/libunicode.c $QJS/dtoa.c $QJS/quickjs-libc.c $QJS/gen/repl.c $QJS/gen/standalone.c -lm
FF=$TP/ffmpeg; FB=$B/ffmpeg
if [ ! -f $FB/inst/lib/libavcodec.a ]; then
  echo "  [ffmpeg] configuring and building libavcodec/libavformat/libswscale/libswresample (once, cached in build/ffmpeg)"
  mkdir -p $FB && FFS=$(pwd)/$FF && ( cd $FB && $FFS/configure --prefix=$(pwd)/inst --enable-cross-compile --arch=x86_64 --target-os=linux --cc=$MCC --ar=ar --ranlib=ranlib --nm=nm --strip=strip \
    --disable-everything --disable-autodetect --disable-programs --disable-doc --disable-avdevice --disable-avfilter --disable-network --disable-debug \
    --enable-static --disable-shared --enable-pic --enable-pthreads --enable-x86asm --enable-swscale --enable-swresample \
    --enable-demuxer=mov,mp3,aac,matroska,ogg,wav --enable-decoder=h264,hevc,aac,aac_latm,mp3float,vorbis,opus,pcm_s16le,vp8,vp9 \
    --enable-parser=h264,hevc,aac,mpegaudio,vp9,opus,vorbis --enable-protocol=file --extra-cflags=-O2 > configure.log 2>&1 && make -j$(nproc) > make.log 2>&1 && make install > install.log 2>&1 ) || { echo "FFMPEG BUILD FAILED (see build/ffmpeg/*.log)"; exit 1; }
fi
port play $MCC -O2 -Wall -Wno-unused-result -I$FB/inst/include -I$BR/inc -Iusr/musl -o $P/play usr/musl/play.c usr/musl/httpio.c usr/musl/nvxtls.c $BL/libbearssl.a -L$FB/inst/lib -lavformat -lavcodec -lswscale -lswresample -lavutil
for n in sqlite3 gzip bzip2 xz zstd kilo fetch qjs play; do
  cp $P/$n $B/user/$n.elf
  embz user_${n}_bin.o $n
  objcopy --rename-section .data=.rodata,alloc,load,readonly,data,contents $B/user_${n}_bin.o
done

echo "[web] build the Noovex web engine (HTML, CSS, layout, paint) with QuickJS and musl libm (SSE, own heap)"
QJ=third_party/quickjs; MSRC=third_party/musl; WB=$B/web; mkdir -p $WB/m
WEBF="-m64 -O2 -ffreestanding -fno-pie -fno-stack-protector -fno-asynchronous-unwind-tables -mno-red-zone -mcmodel=small -msse -msse2 -mfpmath=sse -w -nostdinc -isystem $M/include -isystem $(gcc -print-file-name=include) -DNDEBUG -D_GNU_SOURCE -I. -I$QJ"
python3 tools/js2c.py web/prelude.js web/prelude_gen.c nwe_prelude || exit 1
for f in quickjs libregexp libunicode dtoa; do [ -f $WB/q_$f.o ] || gcc $WEBF -c $QJ/$f.c -o $WB/q_$f.o || { echo "WEB BUILD FAILED: $f"; exit 1; }; done
if [ ! -f $WB/libmk.a ]; then
  MF="-m64 -O2 -ffreestanding -fno-pie -fno-stack-protector -fno-asynchronous-unwind-tables -mno-red-zone -mcmodel=small -msse -msse2 -mfpmath=sse -w -nostdinc -std=c99 -D_XOPEN_SOURCE=700 -I$MSRC/arch/x86_64 -I$MSRC/arch/generic -I$B/musl-obj/obj/src/internal -I$MSRC/src/include -I$MSRC/src/internal -I$B/musl-obj/obj/include -I$MSRC/include -isystem $(gcc -print-file-name=include)"
  for f in $MSRC/src/math/*.c; do b=$(basename $f .c); s2=${b%l}; if [ "$s2" != "$b" ] && [ -f $MSRC/src/math/$s2.c ]; then continue; fi; [ -f $MSRC/src/math/x86_64/$b.c ] && f=$MSRC/src/math/x86_64/$b.c; gcc $MF -c $f -o $WB/m/$b.o || { echo "WEB MATH BUILD FAILED: $b"; exit 1; }; done
  ar rcs $WB/libmk.a $WB/m/*.o
fi
for f in krt util font fontdata dom cssparse cssstyle layout layout2 paint svg js engine wmedia anim webfont ttf woff2 brotli brotli_tab prelude_gen; do gcc $WEBF -c web/$f.c -o $WB/w_$f.o || { echo "WEB BUILD FAILED: $f"; exit 1; }; done
ld -r -o $B/web_pre.o $WB/w_*.o $WB/q_*.o $WB/libmk.a || exit 1
objcopy --keep-global-symbol=nwe_entry --keep-global-symbol=nwe_fail $B/web_pre.o $B/web_all.o || exit 1
echo "  web engine: $(stat -c %s $B/web_all.o) bytes, needs from kernel: $(nm -u $B/web_all.o | awk '{print $2}' | tr '\n' ' ')"

echo "[3b] build Doom engine blob (doomgeneric + FreeDoom libc shim, SSE)"
( cd third_party/doom && sh build_doom.sh >/dev/null 2>&1 )
mv third_party/doom/doom_blob.o $B/doom_blob.o; rm -rf third_party/doom/obj third_party/doom/doom_blob_all.o

echo "[3c] build NTFS support (ntfs-3g library, GPLv2, + NoovexOS glue)"
N=third_party/ntfs
NTFS_GINC=$(gcc -print-file-name=include)
NTFS_CFLAGS="-m64 -Os -ffreestanding -nostdinc -isystem $N/compat -isystem $NTFS_GINC -fno-pie -fno-stack-protector -fno-asynchronous-unwind-tables -mno-red-zone -mno-mmx -mno-sse -mno-sse2 -mcmodel=small -DHAVE_CONFIG_H -Ulinux -U__linux__ -U__linux -U__gnu_linux__ -I$N -I$N/include/ntfs-3g -fmacro-prefix-map=third_party/= -w"
NTFSOBJ=""
for f in $N/lib/*.c $N/nvxntfs.c $N/ntfslibc.c; do o=$B/ntfs/$(basename $f .c).o; gcc $NTFS_CFLAGS -c $f -o $o || exit 1; NTFSOBJ="$NTFSOBJ $o"; done
for f in $N/mkntfs/*.c; do o=$B/ntfs/mk_$(basename $f .c).o; gcc $NTFS_CFLAGS -I$N/mkntfs -Dmain=mkntfs_main -c $f -o $o || exit 1; NTFSOBJ="$NTFSOBJ $o"; done

echo "[3d] glibc 2.44 + CPython 3.13 sysroot (/lib64, /opt/gnu/bin, /usr/bin/python3)"
bash tools/build_glibc.sh $B/glibc-root || echo "  (glibc build failed - sysroot without glibc)"
SR=$B/sysroot-stage; rm -rf $SR; mkdir -p $SR/lib64 $SR/lib $SR/usr/lib $SR/opt/gnu/bin $SR/etc
if [ -f $B/glibc-root/lib64/libc.so.6 ]; then
  for f in ld-linux-x86-64.so.2 libc.so.6 libm.so.6 libpthread.so.0 libdl.so.2 librt.so.1 libresolv.so.2 libutil.so.1 libanl.so.1 libnss_files.so.2 libnss_dns.so.2 libmvec.so.1; do cp $B/glibc-root/lib64/$f $SR/lib64/ && strip --strip-unneeded $SR/lib64/$f 2>/dev/null; done
  for f in libtinfo.so.6 libselinux.so.1 libpcre2-8.so.0 libz.so.1 libexpat.so.1 libgcc_s.so.1 libcrypt.so.1; do [ -f /lib/x86_64-linux-gnu/$f ] && cp -L /lib/x86_64-linux-gnu/$f $SR/lib64/ && strip --strip-unneeded $SR/lib64/$f 2>/dev/null; done
  for p in bash ls cat echo uname date sort env id wc head tail uniq tr seq expr sleep grep sed awk find xargs du df stat touch mkdir rm cp mv ln chmod basename dirname cut tee diff cmp readlink md5sum sha256sum base64 printf yes nproc whoami hostname; do f=$(type -P $p); [ -n "$f" ] && cp -L $f $SR/opt/gnu/bin/ && strip $SR/opt/gnu/bin/$p 2>/dev/null; done
  gcc -O2 -o $SR/opt/gnu/bin/gthreads usr/glibc/gthreads.c -lm -pthread && strip $SR/opt/gnu/bin/gthreads
  gcc -O2 -o $SR/opt/gnu/bin/nvx-mptest usr/glibc/nvx-mptest.c -pthread && strip $SR/opt/gnu/bin/nvx-mptest
  for b in $SR/opt/gnu/bin/*; do ldd $b 2>/dev/null | awk '/=> \//{print $3}' | while read l; do n=$(basename $l); [ -f $SR/lib64/$n ] || { cp -L $l $SR/lib64/$n && strip --strip-unneeded $SR/lib64/$n 2>/dev/null; }; done; done
  ln -s /lib64 $SR/lib/x86_64-linux-gnu; ln -s /lib64 $SR/usr/lib/x86_64-linux-gnu; ln -s /lib64 $SR/usr/lib64
  printf 'passwd: files\ngroup: files\nhosts: files dns\n' > $SR/etc/nsswitch.conf
  bash tools/build_python.sh $B/python-root && bash tools/build_tcltk.sh $B/tcltk-root $B/python-root && bash tools/stage_python.sh $B/python-root $SR || echo "  (CPython build failed - sysroot without python3)"
  if [ -f $B/tcltk-root/usr/lib/libtk8.6.so ]; then cp -a $B/tcltk-root/usr/lib/libtcl8.6.so $B/tcltk-root/usr/lib/libtk8.6.so $SR/lib64/; strip --strip-unneeded $SR/lib64/libtcl8.6.so $SR/lib64/libtk8.6.so; mkdir -p $SR/usr/lib; cp -a $B/tcltk-root/usr/lib/tcl8.6 $B/tcltk-root/usr/lib/tk8.6 $SR/usr/lib/; rm -rf $SR/usr/lib/tk8.6/demos $SR/usr/lib/tcl8.6/tcltest*; cp $B/tcltk-root/usr/bin/wish8.6 $SR/usr/bin/wish 2>/dev/null && strip $SR/usr/bin/wish; fi
  mkdir -p $SR/usr/bin $SR/usr/share/X11 $SR/usr/lib/xorg
  for x in Xvfb xkbcomp; do cp -L /usr/bin/$x $SR/usr/bin/ && strip $SR/usr/bin/$x 2>/dev/null; done
  gcc -O2 -o $SR/usr/bin/xdemo usr/x11/xdemo.c -lX11 -lm && strip $SR/usr/bin/xdemo
  gcc -O2 -o $SR/usr/bin/nvx-xinput usr/x11/nvx-xinput.c -lX11 /usr/lib/x86_64-linux-gnu/libXtst.so.6 && strip $SR/usr/bin/nvx-xinput
  gcc -O2 -o $SR/usr/bin/nvxwm usr/x11/nvxwm.c -lX11 && strip $SR/usr/bin/nvxwm
  cp -rL /usr/share/X11/xkb $SR/usr/share/X11/xkb; cp /usr/share/X11/XErrorDB $SR/usr/share/X11/ 2>/dev/null; cp /usr/lib/xorg/protocol.txt $SR/usr/lib/xorg/ 2>/dev/null
  cat > $SR/usr/bin/nvx-firefox <<'NVXFF'
#!/bin/bash
rm -f /tmp/ff.done
export HOME=/root
export DISPLAY=127.0.0.1:0
export MOZ_DISABLE_CONTENT_SANDBOX=1
export MOZ_DISABLE_GMP_SANDBOX=1
export MOZ_DISABLE_RDD_SANDBOX=1
export MOZ_DISABLE_SOCKET_PROCESS_SANDBOX=1
export MOZ_SANDBOX=0
export LIBGL_ALWAYS_SOFTWARE=1
export MOZ_WEBRENDER=0
export MOZ_X11_EGL=0
export GTK_A11Y=none
export DBUS_SESSION_BUS_ADDRESS=unix:path=/tmp/.nvx-no-session-bus
export MOZ_ACCELERATED=0
export MOZ_CRASHREPORTER_DISABLE=1
export NO_AT_BRIDGE=1
echo "=== nvx-firefox $(date) ===" >> /tmp/ff.log
if ! command -v firefox-esr >/dev/null 2>&1; then echo "firefox-esr is not installed - run: sudo apt install firefox-esr" >> /tmp/ff.log; echo 127 > /tmp/ff.done; exit 127; fi
for i in $(seq 150); do [ -e /tmp/Xvfb_screen0 ] && break; sleep 0.2; done
[ -e /tmp/Xvfb_screen0 ] || echo "X server did not start (see /tmp/Xvfb.log)" >> /tmp/ff.log
export XDG_CACHE_HOME=/root/.cache
mkdir -p /root/.cache/fontconfig
FCS=/root/.cache/nvx-fc-stamp
if command -v fc-cache >/dev/null 2>&1; then if [ ! -e "$FCS" ] || [ -n "$(find /usr/share/fonts /usr/local/share/fonts /root/.fonts /root/.local/share/fonts -newer "$FCS" 2>/dev/null | head -1)" ]; then echo "updating the font cache (only after fonts change)" >> /tmp/ff.log; fc-cache >/dev/null 2>&1; touch "$FCS"; fi; fi
P=/root/.nvx-firefox
mkdir -p "$P"
L=$(readlink "$P/lock" 2>/dev/null); LP=${L##*+}
if [ -n "$L" ] && { [ -z "$LP" ] || ! kill -0 "$LP" 2>/dev/null; }; then rm -f "$P/lock" "$P/.parentlock"; echo "removed a stale profile lock" >> /tmp/ff.log; fi
if [ -e "$P/.nvx-running" ]; then echo "last run did not end cleanly - Firefox will restore the previous session" >> /tmp/ff.log; fi
touch "$P/.nvx-running"
MP=0; [ -e "$P/.nvx-multiprocess" ] && MP=1
cat > /tmp/.nvx-user.js <<'NVXPREFS'
user_pref("browser.shell.checkDefaultBrowser", false);
user_pref("browser.aboutwelcome.enabled", false);
user_pref("browser.startup.homepage_override.mstone", "ignore");
user_pref("browser.startup.preXulSkeletonUI", false);
user_pref("browser.newtabpage.enabled", false);
user_pref("browser.newtabpage.activity-stream.feeds.telemetry", false);
user_pref("browser.newtabpage.activity-stream.telemetry", false);
user_pref("browser.sessionstore.resume_from_crash", true);
user_pref("browser.sessionstore.max_resumed_crashes", 3);
user_pref("browser.sessionstore.interval", 30000);
user_pref("browser.sessionstore.restore_on_demand", true);
user_pref("browser.safebrowsing.malware.enabled", false);
user_pref("browser.safebrowsing.phishing.enabled", false);
user_pref("browser.safebrowsing.downloads.enabled", false);
user_pref("browser.safebrowsing.blockedURIs.enabled", false);
user_pref("datareporting.policy.dataSubmissionEnabled", false);
user_pref("datareporting.healthreport.uploadEnabled", false);
user_pref("toolkit.telemetry.enabled", false);
user_pref("toolkit.telemetry.unified", false);
user_pref("toolkit.telemetry.archive.enabled", false);
user_pref("app.normandy.enabled", false);
user_pref("app.shield.optoutstudies.enabled", false);
user_pref("extensions.update.enabled", false);
user_pref("extensions.pocket.enabled", false);
user_pref("extensions.getAddons.cache.enabled", false);
user_pref("identity.fxaccounts.enabled", false);
user_pref("dom.ipc.processCount", 1);
user_pref("dom.ipc.processPrelaunch.enabled", false);
user_pref("fission.autostart", false);
user_pref("network.dns.disableIPv6", true);
user_pref("network.prefetch-next", false);
user_pref("network.predictor.enabled", false);
user_pref("network.http.speculative-parallel-limit", 0);
user_pref("general.smoothScroll", false);
user_pref("toolkit.cosmeticAnimations.enabled", false);
user_pref("layout.frame_rate", 30);
user_pref("media.hardware-video-decoding.enabled", false);
user_pref("browser.tabs.inTitlebar", 0);
user_pref("browser.startup.blankWindow", false);
user_pref("browser.startup.page", 0);
user_pref("browser.cache.disk.enable", true);
user_pref("browser.cache.disk.capacity", 262144);
user_pref("browser.cache.disk.smart_size.enabled", false);
user_pref("browser.places.importBookmarksHTML", false);
user_pref("extensions.webextensions.remote", false);
user_pref("network.process.enabled", false);
user_pref("media.rdd-process.enabled", false);
user_pref("dom.ipc.forkserver.enable", false);
user_pref("gfx.webrender.software", true);
user_pref("toolkit.startup.max_resumed_crashes", -1);
NVXPREFS
if [ "$MP" = 1 ]; then sed -i -e 's/"dom.ipc.processCount", 1/"dom.ipc.processCount", 4/' -e 's/"extensions.webextensions.remote", false/"extensions.webextensions.remote", true/' /tmp/.nvx-user.js; echo "multi-process mode: 4 content processes" >> /tmp/ff.log; fi
cmp -s /tmp/.nvx-user.js "$P/user.js" || cp /tmp/.nvx-user.js "$P/user.js"
T0=$(date +%s)
firefox-esr --no-remote --new-instance --profile "$P" >> /tmp/ff.log 2>&1
R=$?
echo "Firefox ran for $(( $(date +%s) - T0 )) s, exit code $R" >> /tmp/ff.log
[ $R -eq 0 ] && rm -f "$P/.nvx-running"
if [ $R -ne 0 ]; then for f in /usr/lib/firefox-esr/firefox-esr /usr/lib/firefox-esr/libxul.so; do [ -e "$f" ] && /lib64/ld-linux-x86-64.so.2 --list "$f" 2>&1 | grep -i "not found" | sed "s|^|missing library for ${f##*/}: |" >> /tmp/ff.log; done; fi
echo $R > /tmp/ff.done
NVXFF
  chmod 755 $SR/usr/bin/nvx-firefox
  cat > $SR/usr/bin/nvx-xrun <<'NVXXR'
#!/bin/bash
N="$1"; shift
L=/tmp/$N.log; D=/tmp/$N.done
rm -f "$D"
export HOME=/root
export DISPLAY=127.0.0.1:0
export GTK_A11Y=none
export NO_AT_BRIDGE=1
export GSETTINGS_BACKEND=memory
export GDK_BACKEND=x11
export QT_QPA_PLATFORM=xcb
export LIBGL_ALWAYS_SOFTWARE=1
export SDL_AUDIODRIVER=dummy
export DBUS_SESSION_BUS_ADDRESS=unix:path=/tmp/.nvx-no-session-bus
export XDG_RUNTIME_DIR=/tmp/xdg-runtime
mkdir -p "$XDG_RUNTIME_DIR" /root/.config /root/.local/share /root/.cache; chmod 700 "$XDG_RUNTIME_DIR"
echo "=== $N $(date) ===" >> "$L"
if ! command -v "$1" >/dev/null 2>&1; then echo "$1 is not installed - run: sudo apt install $N" >> "$L"; echo 127 > "$D"; exit 127; fi
for i in $(seq 150); do [ -e /tmp/Xvfb_screen0 ] && break; sleep 0.2; done
[ -e /tmp/Xvfb_screen0 ] || echo "X server did not start (see /tmp/Xvfb.log)" >> "$L"
B=$(command -v "$1"); /lib64/ld-linux-x86-64.so.2 --list "$B" 2>&1 | grep -i "not found" | sed "s|^|missing library: |" >> "$L"
export XDG_CACHE_HOME=/root/.cache
FCS=/root/.cache/nvx-fc-stamp
if command -v fc-cache >/dev/null 2>&1; then if [ ! -e "$FCS" ] || [ -n "$(find /usr/share/fonts /usr/local/share/fonts /root/.fonts /root/.local/share/fonts -newer "$FCS" 2>/dev/null | head -1)" ]; then fc-cache >/dev/null 2>&1; touch "$FCS"; fi; fi
"$@" >> "$L" 2>&1
echo $? > "$D"
NVXXR
  chmod 755 $SR/usr/bin/nvx-xrun
  mkdir -p $SR/var/lib/dbus; echo 6e6f6f7665786f7336340000000000a1 > $SR/etc/machine-id; cp $SR/etc/machine-id $SR/var/lib/dbus/machine-id
  for b in $SR/usr/bin/Xvfb $SR/usr/bin/xkbcomp $SR/usr/bin/xdemo $SR/usr/bin/nvx-xinput; do ldd $b 2>/dev/null | awk '/=> \//{print $3}' | while read l; do n=$(basename $l); [ -f $SR/lib64/$n ] || { cp -L $l $SR/lib64/$n && strip --strip-unneeded $SR/lib64/$n 2>/dev/null; }; done; done
  R=$SR/usr/share/noovex/repo; mkdir -p $R/pool
  for d in usr/packages/*/; do n=$(basename $d); v=$(awk '/^Version:/{print $2}' $d/DEBIAN/control); a=$(awk '/^Architecture:/{print $2}' $d/DEBIAN/control); dpkg-deb --root-owner-group -Zxz -b $d $R/pool/${n}_${v}_${a}.deb >/dev/null; done
  python3 tools/mkrepo.py $R
  mkdir -p $SR/usr/bin $SR/bin; for t in env bash; do ln -sf /opt/gnu/bin/$t $SR/usr/bin/$t; done; ln -sf /opt/gnu/bin/bash $SR/bin/bash; ln -sf /opt/gnu/bin/bash $SR/bin/sh
fi
python3 tools/mksysroot.py $B/sysroot.bin =$SR
( cd $B && ld -r -b binary -o sysroot_bin.o sysroot.bin && objcopy --rename-section .data=.rodata,alloc,load,readonly,data,contents sysroot_bin.o )

echo "[4/6] link (elf_x86_64, base 0x100000)"
OBJS="boot64.o isr64.o userspace.o ap_trampoline.o"
BLOBS="sysroot_bin.o rune_gfx.o rtw_fw.o inst_boot.o inst_stage2.o inst_cfg.o inst_efi.o user_elf.o user_fork_elf.o user_app_elf.o user_thread_elf.o user_preempt_elf.o user_ipc_elf.o user_wmapp_elf.o user_compdemo_elf.o user_posix_elf.o user_mem_elf.o user_count_bin.o user_spin_bin.o user_sigdemo_bin.o user_upper_bin.o user_greet_bin.o user_pipetest_bin.o user_nvxbox_bin.o user_init_bin.o user_libc8_bin.o user_mhello_bin.o user_musltest_bin.o user_smptest_bin.o user_nettest_bin.o user_featest_bin.o user_afutest_bin.o user_sqlite3_bin.o user_gzip_bin.o user_bzip2_bin.o user_xz_bin.o user_zstd_bin.o user_kilo_bin.o user_fetch_bin.o user_qjs_bin.o user_play_bin.o user_libc_so_bin.o user_dyntest_bin.o lua_all.o doom_blob.o doom_wad.o wpf_data.o quake_blob.o lq_pak0.o lq_pak1.o web_all.o nvfw_gsp.o nvfw_bootloader.o nvfw_booter_load.o nvfw_booter_unload.o nvfw_acr_bl.o r3d_blob.o"
ld -m elf_x86_64 -nostdlib -no-pie -z noexecstack -T arch/x86_64/kernel64.ld \
   -o $B/kernel64.elf $(for o in $OBJS; do printf '%s ' $B/$o; done) $UNITOBJS $MODOBJS $NTFSOBJ $(for o in $BLOBS; do printf '%s ' $B/$o; done)

echo "[5/6] objcopy -> flat binary"
objcopy -O binary $B/kernel64.elf $B/kernel64.bin

echo "[6/6] stamp size at offset 24 (Multiboot load_end_addr slot)"
python3 - <<'PY'
import struct
b=bytearray(open('build/kernel64.bin','rb').read())
assert len(b)<=104857600, f"kernel {len(b)} > 100 MiB (loader limit)"
struct.pack_into('<I', b, 24, len(b))

# --- stamp the build signature (find NVX64SIG magic, patch epoch/size/sha256) ---
import time, hashlib
off = b.find(b'NVX64SIG')
if off >= 0:
    import subprocess
    # resolve symbols from the ELF (links at 0x100000)
    ue=0; imm_end=0
    try:
        nm=subprocess.check_output(['nm','build/kernel64.elf']).decode()
        for line in nm.splitlines():
            parts=line.split()
            if len(parts)>=3 and parts[-1]=='uefi_entry': ue=int(parts[0],16)
            if len(parts)>=3 and parts[-1]=='__immutable_end': imm_end=int(parts[0],16)
    except Exception as e:
        print("  (nm failed:",e,")")
    # immutable region = [0x100000 .. __immutable_end)  -> offset 0..imm_sz in the flat file
    imm_sz = (imm_end - 0x100000) if imm_end>0x100000 else len(b)
    if imm_sz>len(b): imm_sz=len(b)
    # struct layout: magic[8] version[16] epoch(u32) size(u32) sha256[32] builder[16] uefi(u64)
    epoch_off = off + 8 + 16
    size_off  = epoch_off + 4
    hash_off  = size_off + 4
    ue_off    = hash_off + 32 + 16
    struct.pack_into('<I', b, epoch_off, int(time.time()))
    struct.pack_into('<I', b, size_off, len(b))      # image_size = full flat size (for loading/display)
    struct.pack_into('<Q', b, ue_off, ue)
    full_off  = ue_off + 8
    for i in range(32): b[hash_off+i] = 0
    for i in range(32): b[full_off+i] = 0
    full = hashlib.sha256(bytes(b)).digest()
    b[full_off:full_off+32] = full
    digest = hashlib.sha256(bytes(b[:imm_sz])).digest()
    b[hash_off:hash_off+32] = digest
    print(f"  build signature stamped @ {off}: sha256={digest[:8].hex()}... imm_size={imm_sz} file={len(b)} uefi_entry=0x{ue:x}")
else:
    print("  WARNING: NVX64SIG signature block not found")

open('build/kernel64.bin','wb').write(b)
print(f"  kernel64.bin = {len(b)} bytes  (size stamped @24)")
PY
echo "done."
