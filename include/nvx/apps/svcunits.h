#ifndef NVX_APPS_SVCUNITS_H
#define NVX_APPS_SVCUNITS_H

#include "include/nvx/base.h"

int svcu_reload(void);
int svcu_find(const char* name);
int svcu_count(void);
int svcu_start(const char* name);
int svcu_stop(const char* name);
int svcu_restart(const char* name);
int svcu_enable(const char* name, int on);
int svcu_state(int i, const char** name, const char** desc, const char** state, int* pid, int* enabled);
void svcu_tick(void);
void svcu_boot(void);
void svcu_shutdown(int timeout_ms);
void svcu_status(const char* name, char* out, int cap);
void svcu_logs(const char* name, char* out, int cap);
void svcu_list(char* out, int cap);
int svcu_parse(const char* txt, int n, char* desc, int dcap, char* exec, int ecap, char* user, int ucap, char* wd, int wcap, char* env, int envcap, char* after, int acap, int* type, int* restart, int* rsec, int* tstop, int* defen);
int svcu_split_args(const char* exec, char* out, int cap);
u32 svcu_backoff(int rsec, int fails);

#endif
