#ifndef NVX_LIB_FPRINT_H
#define NVX_LIB_FPRINT_H
#define FP_MAXM 64
#define FP_MAXW 512
#define FP_MAXPIX 81920
#define FP_NTPL 3
#define FP_THRESH 50
#ifndef FP_MINCNT
#define FP_MINCNT 5
#endif
#ifndef FP_MINOV
#define FP_MINOV 8
#endif
#ifndef FP_DTOL
#define FP_DTOL 10
#endif
#ifndef FP_ATOL
#define FP_ATOL 20
#endif
#ifndef FP_EDGE
#define FP_EDGE 8
#endif
#ifndef FP_MINN
#define FP_MINN 8
#endif
#ifndef FP_FUSE
#define FP_FUSE 100
#endif
#ifndef FP_FUSEMIN
#define FP_FUSEMIN 35
#endif
#ifndef FP_BOXM
#define FP_BOXM 4
#endif
typedef struct { short x, y; unsigned char a, t; } fp_min_t;
typedef struct { int n; fp_min_t m[FP_MAXM]; } fp_tpl_t;
int fp_extract(const unsigned char* img, int w, int h, fp_tpl_t* t);
int fp_match(const fp_tpl_t* a, const fp_tpl_t* b);
int fp_match_ok(int score);
int fp_match_set(const fp_tpl_t* t, int nt, const fp_tpl_t* probe);
int fp_set_ok(int score);
extern int (*fprint_capture)(unsigned char* img, int cap, int* w, int* h);
int fprint_reader(void);
int fprint_enrolled(const char* user);
int fprint_verify_user(const char* user);
int fprint_shell(const char* sub, const char* user);
int fprint_ui_state(void);
int fprint_ui(int op, char* msg, int cap);
int fprint_login_on(void);
int fprint_enrol_busy(void);
void fprint_enrol_cancel(void);
int fprint_enrol_poll(void);
int fprint_enrol_progress(char* msg, int cap);
#endif
