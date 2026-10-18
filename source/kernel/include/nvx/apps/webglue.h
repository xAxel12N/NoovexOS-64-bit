#ifndef NVX_APPS_WEBGLUE_H
#define NVX_APPS_WEBGLUE_H
#include "include/nvx/apps/nwe_api.h"
int web_init(void);
long web_call(int op, nwe_req* r);
int web_busy(void);
void web_console_add(const char* s);
int web_console_get(int idx, char* out, int cap);
int web_console_count(void);
void web_tick_flush(void);
extern const char* WEB_UA;
#endif
