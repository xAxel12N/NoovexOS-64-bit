#define ZSTD_DISABLE_ASM 1
#define ZSTD_LEGACY_SUPPORT 0
#define ZSTD_NO_INTRINSICS 1
#define ZSTD_NOTRACE 1
#define DEBUGLEVEL 0
#define ZSTD_DEPS_NEED_MALLOC
#define XXH_NO_STDLIB
#include "third_party/zstd/lib/common/debug.c"
#include "third_party/zstd/lib/common/entropy_common.c"
#include "third_party/zstd/lib/common/error_private.c"
#include "third_party/zstd/lib/common/fse_decompress.c"
#include "third_party/zstd/lib/common/xxhash.c"
#include "third_party/zstd/lib/common/zstd_common.c"
#include "third_party/zstd/lib/decompress/huf_decompress.c"
#include "third_party/zstd/lib/decompress/zstd_ddict.c"
#include "third_party/zstd/lib/decompress/zstd_decompress.c"
#include "third_party/zstd/lib/decompress/zstd_decompress_block.c"
