#ifndef NVX_SHELL_COMMANDS_H
#define NVX_SHELL_COMMANDS_H

#include "include/nvx/base.h"
#include "include/nvx/lib/js/js.h"

struct pkg_list_ctx { int max, shown; };
typedef struct { const char* name; u32 bg, bar, fg, dim, cur, green, blue, red, cyan; int light; } TTheme;

extern const TTheme TTH[6];

int jc_sig_pending_cur(void);
void sh_run_line(char* ln);
void run_cmd(char* line);
void wpa_connect_saved(u32 m,int idx,const char* pw);
void wpa_do_join(const char* ss,const char* pw);
void term_key(char c);
void term_init(void);
void live_invalidate(void);
void live_present(void);
void live_init(void);
int live_input(void);
void term_draw(int i);

#endif
