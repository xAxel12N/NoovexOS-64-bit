#!/bin/sh
set -e
B=build
CFLAGS="-m64 -Os -ffreestanding -fno-pie -fstack-protector-strong -mstack-protector-guard=global -fno-omit-frame-pointer \
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
( cd assets && ld -r -b binary -o ../$B/doom_wad.o doom1.wad.xz )
( cd assets && ld -r -b binary -o ../$B/wpf_data.o wpf_data.bin )
( cd assets && ld -r -b binary -o ../$B/emoji_data.o emoji32.bin )
( cd assets && ld -r -b binary -o ../$B/lq_pak0.o lq_pak0.xz && ld -r -b binary -o ../$B/lq_pak1.o lq_pak1.xz )
( cd assets/nvidia && for f in gsp bootloader booter_load booter_unload acr_bl; do ld -r -b binary -o ../../$B/nvfw_$f.o nvfw_$f.xz; done )
for o in doom_wad lq_pak0 lq_pak1 wpf_data emoji_data nvfw_gsp; do objcopy --rename-section .data=.blobdata,alloc,load,data,contents $B/$o.o; done
sh third_party/quake/build_quake.sh >/dev/null && mv third_party/quake/quake_blob.o $B/quake_blob.o && rm -rf third_party/quake/obj
sh apps/graphics/r3d/build_r3d.sh >/dev/null && mv apps/graphics/r3d/r3d_blob.o $B/r3d_blob.o
echo "[3a3] embed myboot bootloader + boot.cfg (for the disk installer)"
( cd boot && make build/boot.bin build/stage2.bin >/dev/null ) && cp boot/build/stage2.bin assets/installer/inst_stage2.bin && cp boot/build/boot.bin assets/installer/inst_boot.bin || { echo "BOOT LOADER BUILD FAILED"; exit 1; }
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
for p in mhello musltest smptest nettest featest afutest kfeat; do
  gcc $MCFLAGS -c usr/musl/$p.c -o $B/user/musl_$p.o || { echo "MUSL PROGRAM BUILD FAILED: $p"; exit 1; }
  gcc -static-pie -nostdlib -Wl,--no-dynamic-linker -Wl,-z,noexecstack -o $B/user/$p.elf $M/lib/rcrt1.o $M/lib/crti.o $B/user/musl_$p.o $M/lib/libc.a $(gcc -print-libgcc-file-name) $M/lib/crtn.o || { echo "MUSL PROGRAM LINK FAILED: $p"; exit 1; }
  strip $B/user/$p.elf
  embz user_${p}_bin.o $p
  objcopy --rename-section .data=.rodata,alloc,load,readonly,data,contents $B/user_${p}_bin.o
done

echo "[musl-shared] build musl as a shared library (the dynamic linker /lib/ld-musl-x86_64.so.1) and a dynamic test program"
MS=$(pwd)/$B/musl-sh
if [ ! -f $MS/lib/libc.so ] || ! grep -qa "/lib:/usr/lib" $MS/lib/libc.so; then
  rm -rf $B/musl-shobj; mkdir -p $B/musl-shobj
  ( cd $B/musl-shobj && CC=gcc ../../third_party/musl/configure --prefix=$MS --syslibdir=/lib --enable-shared --disable-static --disable-wrapper CFLAGS="-O2" >/dev/null && make -j"$(nproc 2>/dev/null || echo 4)" libdir=/usr/lib >/dev/null 2>&1 && make install-libs syslibdir=$MS/lib libdir=$MS/lib >/dev/null 2>&1 ) || { echo "MUSL SHARED BUILD FAILED"; exit 1; }
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
for f in quickjs libregexp libunicode dtoa; do { [ -f $WB/q_$f.o ] && [ $WB/q_$f.o -nt $QJ/$f.c ] && [ $WB/q_$f.o -nt $QJ/quickjs.h ]; } || gcc $WEBF -c $QJ/$f.c -o $WB/q_$f.o || { echo "WEB BUILD FAILED: $f"; exit 1; }; done
if [ ! -f $WB/libmk.a ]; then
  MF="-m64 -O2 -ffreestanding -fno-pie -fno-stack-protector -fno-asynchronous-unwind-tables -mno-red-zone -mcmodel=small -msse -msse2 -mfpmath=sse -w -nostdinc -std=c99 -D_XOPEN_SOURCE=700 -I$MSRC/arch/x86_64 -I$MSRC/arch/generic -I$B/musl-obj/obj/src/internal -I$MSRC/src/include -I$MSRC/src/internal -I$B/musl-obj/obj/include -I$MSRC/include -isystem $(gcc -print-file-name=include)"
  for f in $MSRC/src/math/*.c; do b=$(basename $f .c); s2=${b%l}; if [ "$s2" != "$b" ] && [ -f $MSRC/src/math/$s2.c ]; then continue; fi; [ -f $MSRC/src/math/x86_64/$b.c ] && f=$MSRC/src/math/x86_64/$b.c; gcc $MF -c $f -o $WB/m/$b.o || { echo "WEB MATH BUILD FAILED: $b"; exit 1; }; done
  ar rcs $WB/libmk.a $WB/m/*.o
fi
DVF="-Ithird_party/dav1d -Ithird_party/dav1d/include -Ithird_party/dav1d/include/dav1d -include third_party/dav1d/config.h"
for f in cdf cpu ctx data decode dequant_tables getbits intra_edge itx_1d lf_mask lib log mem msac obu pal picture qm ref refmvs scan tables thread_task warpmv wedge; do [ -f $WB/w_dv_$f.o ] && [ $WB/w_dv_$f.o -nt third_party/dav1d/src/$f.c ] || gcc $WEBF $DVF -c third_party/dav1d/src/$f.c -o $WB/w_dv_$f.o || { echo "WEB BUILD FAILED: dav1d $f"; exit 1; }; done
for b in 8 16; do for f in cdef_apply_tmpl cdef_tmpl fg_apply_tmpl filmgrain_tmpl ipred_prepare_tmpl ipred_tmpl itx_tmpl lf_apply_tmpl loopfilter_tmpl looprestoration_tmpl lr_apply_tmpl mc_tmpl recon_tmpl; do [ -f $WB/w_dv_${f}_$b.o ] && [ $WB/w_dv_${f}_$b.o -nt third_party/dav1d/src/$f.c ] || gcc $WEBF $DVF -DBITDEPTH=$b -c third_party/dav1d/src/$f.c -o $WB/w_dv_${f}_$b.o || { echo "WEB BUILD FAILED: dav1d $f $b"; exit 1; }; done; done
for f in krt util font fontdata dom cssparse cssstyle layout layout2 paint svg canvas webgl js engine wmedia anim imgx webfont ttf woff2 brotli brotli_tab prelude_gen; do gcc $WEBF -c web/$f.c -o $WB/w_$f.o || { echo "WEB BUILD FAILED: $f"; exit 1; }; done
W3F="-DM3_THREAD_LOCAL= -Dd_m3HasPosixHost=0 -Dd_m3HasTypedRefs=0 -Dd_m3HasStackSwitching=0 -Dd_m3HasSnapshots=0 -Dd_m3HasGasMetering=0 -Dd_m3MaxNativeStack=1572864 -Dd_m3LogOutput=0 -Dd_m3HasThreads=0"
for f in third_party/wasm3/m3_*.c web/wasm.c; do b=$(basename $f .c); gcc $WEBF $W3F -c $f -o $WB/w_x$b.o || { echo "WEB BUILD FAILED: $b"; exit 1; }; done
RSLIB=""; rm -f $WB/w_rust_stub.o
if command -v cargo >/dev/null 2>&1 && sh tools/build_rust.sh $B/rust > $B/rust.log 2>&1; then RSLIB=$B/rust/release/libnvxlayout.a; echo "  rust: nvxlayout (Taffy grid layout) $(stat -c %s $RSLIB) bytes"
else gcc $WEBF -c web/rust_stub.c -o $WB/w_rust_stub.o || exit 1; echo "  rust: cargo not found or build failed (see build/rust.log), grid layout uses the C code"; fi
ld -r -o $B/web_pre.o $WB/w_*.o $WB/q_*.o $RSLIB $WB/libmk.a || exit 1
objcopy --keep-global-symbol=nwe_entry --keep-global-symbol=nwe_fail $B/web_pre.o $B/web_all.o || exit 1
for K in 2 3 4 5 6; do
  A=nwk_alloc_arena3; [ $K = 2 ] && A=nwk_alloc_arena2
  IS=nwk_frame_is2; [ $K = 6 ] && IS=nwk_frame_is_popup
  objcopy --redefine-sym nwe_entry=nwe${K}_entry --redefine-sym nwe_fail=nwe${K}_fail --redefine-sym nwk_alloc_arena=$A --redefine-sym nwk_ws_close_all=nwk_frame_noop --redefine-sym nwk_frame_reset=nwk_frame_noop2 --redefine-sym nwk_frame_paint=nwk_frame_paint_nested --redefine-sym nwk_frame_post=nwk_frame_post_up$K --redefine-sym nwk_frame_recv=nwk_frame_recv_down$K --redefine-sym nwk_frame_is=$IS --redefine-sym nwk_frame_open=nwk_frame_open2 --redefine-sym nwk_frame_open_doc=nwk_frame_open_doc2 --redefine-sym nwk_frame_info=nwk_frame_info2 --redefine-sym nwk_frame_switch=nwk_frame_switch2 --redefine-sym nwk_pending_set=nwk_pending_set2 --redefine-sym nwk_frame_slot=nwk_frame_slot2 --redefine-sym nwk_popup_open=nwk_popup_open2 --redefine-sym nwk_popup_info=nwk_popup_info2 --redefine-sym nwk_popup_close=nwk_popup_close_self $B/web_pre.o $B/web${K}_pre.o || exit 1
  objcopy --keep-global-symbol=nwe${K}_entry --keep-global-symbol=nwe${K}_fail $B/web${K}_pre.o $B/web${K}_all.o || exit 1
  rm -f $B/web${K}_pre.o
done
echo "  web engine: $(stat -c %s $B/web_all.o) bytes, needs from kernel: $(nm -u $B/web_all.o | awk '{print $2}' | tr '\n' ' ')"
python3 tools/js2c.py web/wk_prelude.js web/wk_prelude_gen.c nwe_wkprelude || exit 1
gcc $WEBF -c web/wk.c -o $WB/k_wk.o || { echo "WEB BUILD FAILED: wk"; exit 1; }
gcc $WEBF -c web/wk_prelude_gen.c -o $WB/k_wkp.o || exit 1
ld -r -o $B/wkx_pre.o $WB/k_wk.o $WB/k_wkp.o $WB/q_*.o $WB/w_krt.o $WB/libmk.a || exit 1
objcopy --redefine-sym nwk_alloc_arena=wk_k_arena --redefine-sym nwk_log=wk_k_log $B/wkx_pre.o $B/wkx_pre2.o || exit 1
objcopy --keep-global-symbol=wkx_entry --keep-global-symbol=wkx_fail $B/wkx_pre2.o $B/wkx_all.o || exit 1
rm -f $B/wkx_pre.o $B/wkx_pre2.o
echo "  worker engine (second core): $(stat -c %s $B/wkx_all.o) bytes, needs: $(nm -u $B/wkx_all.o | awk '{print $2}' | tr '\n' ' ')"

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
  for p in bash ls cat echo uname date sort env id wc head tail uniq tr seq expr sleep grep sed awk find xargs du df stat touch mkdir rm cp mv ln chmod basename dirname cut tee diff cmp readlink md5sum sha256sum base64 printf yes nproc whoami hostname dd tar gzip sha1sum; do f=""; for d in /usr/bin /bin /usr/local/bin; do if [ -x $d/$p ]; then f=$d/$p; break; fi; done; [ -n "$f" ] && cp -L $f $SR/opt/gnu/bin/ && strip $SR/opt/gnu/bin/$p 2>/dev/null; done
  gcc -O2 -o $SR/opt/gnu/bin/gthreads usr/glibc/gthreads.c -lm -pthread && strip $SR/opt/gnu/bin/gthreads
  gcc -O2 -o $SR/opt/gnu/bin/nvx-mptest usr/glibc/nvx-mptest.c -pthread && strip $SR/opt/gnu/bin/nvx-mptest
  gcc -O2 -o $SR/opt/gnu/bin/nvx-abitest usr/glibc/nvx-abitest.c && strip $SR/opt/gnu/bin/nvx-abitest
  gcc -O2 -o $SR/opt/gnu/bin/nvx-sndtest usr/glibc/nvx-sndtest.c -ldl -lm && strip $SR/opt/gnu/bin/nvx-sndtest
  mkdir -p $SR/usr/lib/nvx && gcc -shared -fPIC -O2 -o $SR/usr/lib/nvx/libnvx-throwlog.so usr/glibc/nvx-throwlog.c -ldl && strip --strip-unneeded $SR/usr/lib/nvx/libnvx-throwlog.so
  for b in $SR/opt/gnu/bin/*; do ldd $b 2>/dev/null | awk '/=> \//{print $3}' | while read l; do n=$(basename $l); [ -f $SR/lib64/$n ] || { cp -L $l $SR/lib64/$n && strip --strip-unneeded $SR/lib64/$n 2>/dev/null; }; done; done
  ln -s /lib64 $SR/lib/x86_64-linux-gnu; ln -s /lib64 $SR/usr/lib/x86_64-linux-gnu; ln -s /lib64 $SR/usr/lib64
  printf 'passwd: files\ngroup: files\nhosts: files dns\n' > $SR/etc/nsswitch.conf
  bash tools/build_python.sh $B/python-root && bash tools/build_tcltk.sh $B/tcltk-root $B/python-root && bash tools/stage_python.sh $B/python-root $SR || echo "  (CPython build failed - sysroot without python3)"
  if [ -f $B/tcltk-root/usr/lib/libtk8.6.so ]; then cp -a $B/tcltk-root/usr/lib/libtcl8.6.so $B/tcltk-root/usr/lib/libtk8.6.so $SR/lib64/; strip --strip-unneeded $SR/lib64/libtcl8.6.so $SR/lib64/libtk8.6.so; mkdir -p $SR/usr/lib; cp -a $B/tcltk-root/usr/lib/tcl8.6 $B/tcltk-root/usr/lib/tk8.6 $SR/usr/lib/; rm -rf $SR/usr/lib/tk8.6/demos $SR/usr/lib/tcl8.6/tcltest*; cp $B/tcltk-root/usr/bin/wish8.6 $SR/usr/bin/wish 2>/dev/null && strip $SR/usr/bin/wish; fi
  bash tools/stage_gcc.sh $SR $B/glibc-root || echo "  (gcc staging failed - sysroot without gcc)"
  if bash tools/build_mlibc.sh $B/mlibc-root && [ -f $B/mlibc-root/usr/mlibc/lib/libc.so ]; then
    mkdir -p $SR/usr/mlibc; cp -a $B/mlibc-root/usr/mlibc/. $SR/usr/mlibc/
    for d in linux asm asm-generic; do ln -sfn /usr/include/$d $SR/usr/mlibc/include/$d; done
    [ -x $SR/usr/bin/gcc ] && ln -sfn /usr/mlibc/bin/mlibc-gcc $SR/usr/bin/mlibc-gcc
  else echo "  (mlibc build failed - sysroot without mlibc)"; fi
  [ -n "$NVX_SR_EXTRA" ] && [ -d "$NVX_SR_EXTRA" ] && cp -a "$NVX_SR_EXTRA"/. $SR/
  mkdir -p $SR/usr/bin $SR/usr/share/X11 $SR/usr/lib/xorg
  for x in Xvfb xkbcomp; do cp -L /usr/bin/$x $SR/usr/bin/ && strip $SR/usr/bin/$x 2>/dev/null; done
  gcc -O2 -o $SR/usr/bin/xdemo usr/x11/xdemo.c -lX11 -lm && strip $SR/usr/bin/xdemo
  gcc -O2 -o $SR/usr/bin/nvx-xinput usr/x11/nvx-xinput.c -lX11 /usr/lib/x86_64-linux-gnu/libXtst.so.6 && strip $SR/usr/bin/nvx-xinput
  gcc -O2 -o $SR/usr/bin/nvxwm usr/x11/nvxwm.c -lX11 && strip $SR/usr/bin/nvxwm
  GL=/usr/lib/x86_64-linux-gnu; if [ -f $GL/libgtk-3.so.0 ]; then gcc -O2 -o $SR/usr/bin/gtkdemo usr/x11/gtkdemo.c $GL/libgtk-3.so.0 $GL/libgobject-2.0.so.0 $GL/libglib-2.0.so.0 && strip $SR/usr/bin/gtkdemo; mkdir -p $SR/etc/fonts $SR/usr/share/mime; cp -rL /etc/fonts/. $SR/etc/fonts/; for f in mime.cache magic globs globs2 aliases subclasses types icons generic-icons treemagic XMLnamespaces version; do [ -f /usr/share/mime/$f ] && cp /usr/share/mime/$f $SR/usr/share/mime/; done; else echo "  (GTK 3 runtime not found on the build machine - sysroot without gtkdemo)"; fi
  cp -rL /usr/share/X11/xkb $SR/usr/share/X11/xkb; cp /usr/share/X11/XErrorDB $SR/usr/share/X11/ 2>/dev/null; cp /usr/lib/xorg/protocol.txt $SR/usr/lib/xorg/ 2>/dev/null
  if [ -x /usr/bin/dbus-daemon ]; then mkdir -p $SR/usr/share/dbus-1/services $SR/usr/share/dbus-1/session.d $SR/etc/dbus-1; cp -L /usr/bin/dbus-daemon $SR/usr/bin/ && strip $SR/usr/bin/dbus-daemon; cp /usr/share/dbus-1/session.conf $SR/usr/share/dbus-1/; printf '[D-BUS Service]\nName=org.freedesktop.secrets\nExec=/usr/bin/nvx-keyring\n' > $SR/usr/share/dbus-1/services/org.freedesktop.secrets.service; printf '[D-BUS Service]\nName=org.freedesktop.Notifications\nExec=/usr/bin/nvx-notify\n' > $SR/usr/share/dbus-1/services/org.freedesktop.Notifications.service; fi
  if [ -f /usr/lib/x86_64-linux-gnu/libsecret-1.so.0 ]; then cp -L /usr/lib/x86_64-linux-gnu/libsecret-1.so.0 $SR/lib64/ && strip --strip-unneeded $SR/lib64/libsecret-1.so.0; fi
  mkdir -p $SR/var/lib/dbus; echo 6e6f6f7665786f7336340000000000a1 > $SR/etc/machine-id; cp $SR/etc/machine-id $SR/var/lib/dbus/machine-id
  if [ -d /usr/share/X11/locale ]; then mkdir -p $SR/usr/share/X11; cp -rL /usr/share/X11/locale $SR/usr/share/X11/; fi
  if [ -x /usr/bin/update-alternatives ]; then cp -L /usr/bin/update-alternatives $SR/usr/bin/ && strip $SR/usr/bin/update-alternatives; mkdir -p $SR/etc/alternatives $SR/var/lib/dpkg/alternatives; fi
  if [ -x /usr/bin/findmnt ]; then cp -L /usr/bin/findmnt $SR/usr/bin/ && strip $SR/usr/bin/findmnt; ldd /usr/bin/findmnt | awk '/=> \//{print $3}' | while read l; do n=$(basename $l); [ -f $SR/lib64/$n ] || { cp -L $l $SR/lib64/$n && strip --strip-unneeded $SR/lib64/$n; }; done; fi
  if [ -f /usr/share/alsa/alsa.conf ]; then mkdir -p $SR/usr/share/alsa $SR/etc; cp -rL /usr/share/alsa/alsa.conf /usr/share/alsa/pcm /usr/share/alsa/cards /usr/share/alsa/ctl $SR/usr/share/alsa/ 2>/dev/null; printf 'pcm.!default {\n    type plug\n    slave.pcm "nvxemu"\n}\npcm.nvxemu {\n    type mmap_emul\n    slave.pcm "hw:0,0"\n}\nctl.!default {\n    type hw\n    card 0\n}\n' > $SR/etc/asound.conf; fi
  CRL=/usr/lib/x86_64-linux-gnu; CRX=""
  for n in libnss3.so libnssutil3.so libsmime3.so libssl3.so libnspr4.so libplc4.so libplds4.so libsoftokn3.so libfreebl3.so libfreeblpriv3.so libasound.so.2 libcups.so.2 libdrm.so.2 libgbm.so.1 libudev.so.1 libX11-xcb.so.1 libxshmfence.so.1 libXss.so.1 libxkbcommon-x11.so.0 libXtst.so.6 libXi.so.6 libxcb-dri3.so.0 libxcb-randr.so.0 libnotify.so.4 libuuid.so.1 libstdc++.so.6 libatomic.so.1; do
    if [ -f $CRL/$n ] && [ ! -f $SR/lib64/$n ]; then cp -L $CRL/$n $SR/lib64/$n && strip --strip-unneeded $SR/lib64/$n 2>/dev/null; CRX="$CRX $SR/lib64/$n"; fi; done
  if [ -z "$CRX" ]; then echo "  note: the Chromium/Electron libraries (libnss3, libgbm, libasound, libcups) are not on this build machine; Electron apps will need them from apt"; fi
  for b in $SR/usr/bin/Xvfb $SR/usr/bin/xkbcomp $SR/usr/bin/xdemo $SR/usr/bin/nvx-xinput $SR/usr/bin/gtkdemo $SR/usr/bin/dbus-daemon $CRX; do ldd $b 2>/dev/null | awk '/=> \//{print $3}' | while read l; do n=$(basename $l); [ -f $SR/lib64/$n ] || { cp -L $l $SR/lib64/$n && strip --strip-unneeded $SR/lib64/$n 2>/dev/null; }; done; done
  echo "[distro] ports from ${NVX_PROFILE:-distro/noovexos.profile} (tools/nvxport.py; see ports/README.md)"
  python3 tools/nvxport.py stage --profile ${NVX_PROFILE:-distro/noovexos.profile} --root $SR --build $B || exit 1
  R=$SR/usr/share/noovex/repo; mkdir -p $R/pool
  for d in usr/packages/*/; do n=$(basename $d); v=$(awk '/^Version:/{print $2}' $d/DEBIAN/control); a=$(awk '/^Architecture:/{print $2}' $d/DEBIAN/control); dpkg-deb --root-owner-group -Zxz -b $d $R/pool/${n}_${v}_${a}.deb >/dev/null; done
  python3 tools/mkrepo.py $R
  mkdir -p $SR/usr/bin $SR/bin; for t in env bash; do ln -sf /opt/gnu/bin/$t $SR/usr/bin/$t; done; ln -sf /opt/gnu/bin/bash $SR/bin/bash; ln -sf /opt/gnu/bin/bash $SR/bin/sh
fi
echo "[ports2] TCC, vim, less, curl, git and ssh (Dropbear) for musl, with ncurses, zlib and mbedTLS (cached in build/ports2)"
sh tools/build_ports2.sh $B/ports2 > $B/ports2.log 2>&1 && cp -a $B/ports2/root/. $SR/ || { echo "PORTS2 BUILD FAILED (see build/ports2.log)"; exit 1; }
echo "[netsurf] NetSurf web browser (framebuffer, NoovexOS surface) with libcss, libdom, libhubbub, FreeType, libpng, libjpeg-turbo, expat (cached in build/netsurf)"
sh tools/build_netsurf.sh $B/netsurf $B/ports2/sys > $B/netsurf.log 2>&1 && cp -a $B/netsurf/root/. $SR/ || { echo "NETSURF BUILD FAILED (see build/netsurf.log)"; exit 1; }
if command -v cargo >/dev/null 2>&1 && sh tools/build_rust_apps.sh $B/rust-apps > $B/rust-apps.log 2>&1; then mkdir -p $SR/usr/bin; cp $B/rust-apps/x86_64-unknown-linux-gnu/release/rustinfo $SR/usr/bin/rustinfo; cp $B/rust-apps/x86_64-unknown-linux-gnu/release/nvx-mem $SR/usr/bin/nvx-mem; echo "  rust: rustinfo ($(stat -c %s $SR/usr/bin/rustinfo) bytes), nvx-mem ($(stat -c %s $SR/usr/bin/nvx-mem) bytes)"; else echo "  rust: rustinfo skipped (see build/rust-apps.log)"; fi
if command -v dpkg-query >/dev/null 2>&1; then mkdir -p $SR/usr/share/noovex; for f in $SR/lib64/*; do b=$(basename $f); for d in /usr/lib/x86_64-linux-gnu /lib/x86_64-linux-gnu; do [ -e $d/$b ] && { echo $d/$b; break; }; done; done | xargs -r dpkg -S 2>/dev/null | cut -d: -f1 | tr ',' '\n' | sed 's/ //g' | sort -u | xargs -r dpkg-query -W -f '${Package} ${Version}|${Provides}\n' 2>/dev/null | python3 tools/provided.py > $SR/usr/share/noovex/provided.txt; echo "  provided.txt: $(wc -l < $SR/usr/share/noovex/provided.txt) packages the Linux files already contain"; fi
echo "[faceid] face engine for Face ID sign-in (dlib, cached in build/faceid)"
sh tools/build_faceid.sh $B/faceid > $B/faceid.log 2>&1 && cp -a $B/faceid/root/. $SR/ && tail -1 $B/faceid.log || echo "  face engine skipped (see build/faceid.log)"
mkdir -p $SR/usr/bin $SR/usr/share/applications $SR/usr/share/icons/hicolor/48x48/apps
for a in writer:Writer:"Word processor (.odt, .docx)":WordProcessor calc:Calc:"Spreadsheet (.ods, .xlsx)":Spreadsheet; do
  k=${a%%:*}; r=${a#*:}; t=${r%%:*}; r=${r#*:}; c=${r%%:*}; g=${r#*:}
  printf '[Desktop Entry]\nType=Application\nName=LibreOffice %s\nComment=%s\nExec=nvx-libreoffice --%s %%U\nIcon=libreoffice-%s\nCategories=Office;%s;\n' "$t" "$c" "$k" "$k" "$g" > $SR/usr/share/applications/nvx-libreoffice-$k.desktop
  [ -f /usr/share/icons/hicolor/48x48/apps/libreoffice-$k.png ] && cp /usr/share/icons/hicolor/48x48/apps/libreoffice-$k.png $SR/usr/share/icons/hicolor/48x48/apps/
done
echo "[apps] built-in LibreOffice Writer and Calc (part of the Linux files)"
rm -f $B/apps.bin $B/apps.hash
if sh tools/stage_libreoffice.sh $B/apps-stage $SR; then cp -a $B/apps-stage/. $SR/; else rm -f $SR/usr/share/applications/nvx-libreoffice-*.desktop; fi
rm -rf $B/apps-stage
python3 tools/mksysroot.py $B/sysroot.bin =$SR

echo "[4/6] link (elf_x86_64, base 0x100000)"
OBJS="boot64.o isr64.o userspace.o ap_trampoline.o"
BLOBS="rune_gfx.o rtw_fw.o inst_boot.o inst_stage2.o inst_cfg.o inst_efi.o user_elf.o user_fork_elf.o user_app_elf.o user_thread_elf.o user_preempt_elf.o user_ipc_elf.o user_wmapp_elf.o user_compdemo_elf.o user_posix_elf.o user_mem_elf.o user_count_bin.o user_spin_bin.o user_sigdemo_bin.o user_upper_bin.o user_greet_bin.o user_pipetest_bin.o user_nvxbox_bin.o user_init_bin.o user_libc8_bin.o user_mhello_bin.o user_musltest_bin.o user_smptest_bin.o user_nettest_bin.o user_featest_bin.o user_afutest_bin.o user_kfeat_bin.o user_sqlite3_bin.o user_gzip_bin.o user_bzip2_bin.o user_xz_bin.o user_zstd_bin.o user_kilo_bin.o user_fetch_bin.o user_qjs_bin.o user_play_bin.o user_libc_so_bin.o user_dyntest_bin.o lua_all.o doom_blob.o doom_wad.o wpf_data.o emoji_data.o quake_blob.o lq_pak0.o lq_pak1.o web_all.o web2_all.o web3_all.o web4_all.o web5_all.o web6_all.o wkx_all.o nvfw_gsp.o nvfw_bootloader.o nvfw_booter_load.o nvfw_booter_unload.o nvfw_acr_bl.o r3d_blob.o"
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
import zlib, os
_sr=open('build/sysroot.bin','rb').read() if os.path.exists('build/sysroot.bin') else b''
assert len(_sr)<=0x10000000, f"sysroot {len(_sr)} > 256 MiB (loader area 0x20000000-0x30000000)"
struct.pack_into('<II', b, 16, zlib.crc32(_sr)&0xffffffff if _sr else 0, len(_sr))
print(f"  sysroot: {len(_sr)} bytes, crc32 {zlib.crc32(_sr)&0xffffffff:08X}, loaded by the boot loader from /sysroot/{zlib.crc32(_sr)&0xffffffff:08X}.BIN")

# --- function names for crash reports (g_ksyms, magic NVXKSYM1 -> NVXKSYM2) ---
import subprocess
try:
    out=subprocess.check_output(['nm','-n','--defined-only','build/kernel64.elf']).decode()
    hd=subprocess.check_output(['objdump','-h','build/kernel64.elf']).decode()
    tlo=thi=0
    for line in hd.splitlines():
        c=line.split()
        if len(c)>=4 and c[1]=='.text': tlo=int(c[3],16); thi=tlo+int(c[2],16)
    ka=None; seen={}; syms=[]
    for line in out.splitlines():
        c=line.split()
        if len(c)<3: continue
        a=int(c[0],16); t=c[1]; n=c[2]
        if n=='g_ksyms': ka=a
        if t in 'tTwW' and tlo<=a<thi and not n.startswith('.') and a not in seen:
            seen[a]=1; syms.append((a,n[:48]))
    cap=393216
    if ka is None or b[ka-0x100000:ka-0x100000+8]!=b'NVXKSYM1':
        print("  WARNING: g_ksyms table not found - crash reports will have no function names")
    else:
        while True:
            names=bytearray(); ents=bytearray()
            for a,n in syms:
                ents+=struct.pack('<II',a,len(names)); names+=n.encode()+b'\0'
            blob=bytearray(b'NVXKSYM2')+struct.pack('<IIII',len(syms),24+len(ents),tlo,thi)+ents+names
            if len(blob)<=cap: break
            syms=[x for x in syms if not x[1].startswith(('__','.'))][:len(syms)*9//10]
        o=ka-0x100000; b[o:o+len(blob)]=blob
        print(f"  function names: {len(syms)} symbols, {len(blob)} of {cap} bytes, text {tlo:#x}-{thi:#x}")
except Exception as e:
    print("  WARNING: function name table not written:",e)

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
