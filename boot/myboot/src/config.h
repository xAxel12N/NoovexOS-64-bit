#ifndef CONFIG_H
#define CONFIG_H
#include "types.h"

#define CFG_MAX_ENTRIES 16
#define CFG_TITLE_LEN   64
#define CFG_PATH_LEN    128
#define CFG_CMDLINE_LEN 128

struct cfg_entry {
    char title[CFG_TITLE_LEN];
    char kernel[CFG_PATH_LEN];
    char cmdline[CFG_CMDLINE_LEN];
};

struct cfg {
    struct cfg_entry entries[CFG_MAX_ENTRIES];
    int count;
    int timeout;
    int def;
    int video_w;
    int video_h;
    int bootlog;
};

void cfg_parse(char* text, struct cfg* out);
#endif
