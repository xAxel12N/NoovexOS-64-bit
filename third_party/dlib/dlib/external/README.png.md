# Bundled PNG dependencies

When dlib cannot use an external libpng, it builds the sources in `libpng/`
and `zlib/` directly. Python builds with MSVC also use these bundled sources.

The bundled releases are:

* libpng 1.6.58: <https://download.sourceforge.net/libpng/libpng-1.6.58.tar.xz>
  SHA-256: `28eb403f51f0f7405249132cecfe82ea5c0ef97f1b32c5a65828814ae0d34775`
* zlib 1.3.2: <https://zlib.net/zlib-1.3.2.tar.gz>
  SHA-256: `bb329a0a2cd0274d05519d61c667c062e06990d72e125ee2dfa8de64f0119d16`

Only the library sources, headers, ARM PNG optimizations, and accompanying
notices are included. Upstream build systems, examples, and tests are omitted.

Local adjustments to preserve when updating:

* `libpng/pngconf.h` and `zlib/gzguts.h` suppress MSVC warning 4996.
* `zlib/gzguts.h` includes `unistd.h` on non-Windows platforms because dlib
  does not run zlib's configure step.
* `libpng/pnglibconf.h` comes from upstream's
  `scripts/pnglibconf.h.prebuilt`, with `PNG_ZLIB_VERNUM` set to the bundled
  zlib version. MIPS, PowerPC, and LoongArch optimizations are disabled because
  their source files are not included in dlib's build. ARM NEON remains enabled
  where supported by the compiler.

To exercise these sources with CMake, configure a fresh build with
`-DDLIB_PNG_SUPPORT=ON -DPNG_FOUND=OFF` and run dlib's `--test_image` tests.
Confirm that both `external/libpng` and `external/zlib` appear in the build;
testing against an installed libpng does not validate this fallback.
