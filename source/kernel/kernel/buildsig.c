/* ===== NoovexOS build signature (patched post-link) ===== */
__attribute__((aligned(64), used, section(".sig")))
static volatile const struct {
    char     magic[8];       /* "NVX64SIG" */
    char     version[16];    /* e.g. "8.13" */
    unsigned int  build_epoch;    /* unix time, patched post-link */
    unsigned int  image_size;     /* flat image size, patched post-link */
    unsigned char sha256[32];     /* SHA-256 of the image (with these 32 bytes zeroed), patched */
    char     builder[16];    /* "NoovexOS build" */
    unsigned long long uefi_entry; /* physical addr of uefi_entry, patched post-link */
    unsigned char sha256_full[32];
} g_buildsig = {
    {'N','V','X','6','4','S','I','G'},
    "8.13",
    0, 0,
    {0},
    "NoovexOS build",
    0,
    {0}
};
