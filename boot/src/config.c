#include "config.h"
#include "string.h"

static char* trim(char* s){
    while(*s == ' ' || *s == '\t') s++;
    char* end = s;
    while(*end) end++;
    while(end > s && (end[-1] == ' ' || end[-1] == '\t' || end[-1] == '\r' || end[-1] == '\n')) end--;
    *end = 0;
    return s;
}

void cfg_parse(char* text, struct cfg* out){
    memset(out, 0, sizeof(*out));
    out->timeout = 0;
    out->def = 0;
    out->bootlog = 5;
    char* p = text;
    while(*p){
        char* line = p;
        while(*p && *p != '\n') p++;
        if(*p == '\n'){ *p = 0; p++; }
        line = trim(line);
        if(line[0] == 0 || line[0] == '#') continue;
        char* eq = strchr(line, '=');
        if(!eq) continue;
        *eq = 0;
        char* key = trim(line);
        char* val = trim(eq + 1);
        if(strcmp(key, "timeout") == 0){
            out->timeout = str_to_int(val);
        } else if(strcmp(key, "default") == 0){
            out->def = str_to_int(val);
        } else if(strcmp(key, "entry") == 0){
            if(out->count < CFG_MAX_ENTRIES){
                strncpy_z(out->entries[out->count].title, val, CFG_TITLE_LEN);
                out->entries[out->count].kernel[0] = 0;
                out->entries[out->count].cmdline[0] = 0;
                out->count++;
            }
        } else if(strcmp(key, "kernel") == 0){
            if(out->count > 0) strncpy_z(out->entries[out->count - 1].kernel, val, CFG_PATH_LEN);
        } else if(strcmp(key, "cmdline") == 0){
            if(out->count > 0) strncpy_z(out->entries[out->count - 1].cmdline, val, CFG_CMDLINE_LEN);
        } else if(strcmp(key, "bootlog") == 0){
            out->bootlog = str_to_int(val);
        } else if(strcmp(key, "video") == 0){
            if(strcmp(val, "auto") == 0){ out->video_w = 0xFFFF; out->video_h = 0xFFFF; continue; }
            out->video_w = str_to_int(val);
            char* x = strchr(val, 'x');
            if(!x) x = strchr(val, 'X');
            out->video_h = x ? str_to_int(x + 1) : 0;
        }
    }
    if(out->def < 0 || out->def >= out->count) out->def = 0;
}
