#ifndef NWE_H
#define NWE_H
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <math.h>
#include "include/nvx/apps/nwe_api.h"

typedef struct { int cp; int adv; int off; } nwe_glyphrec;
typedef struct { const char* name; int upm, asc, desc, gap, n; const nwe_glyphrec* g; const short* d; } nwe_facerec;
extern const nwe_facerec nwe_faces[];
extern const int nwe_nfaces;
const nwe_facerec* nwe_face(int face);
const short* ttf_outline(int face, int g);
int ttf_load(const unsigned char* data, int len, const char* name);
void ttf_reset(void);
int ttf_shape(int face, const char* s, int n, char* out, int cap);

enum { F_SANS, F_SANS_B, F_SANS_I, F_SANS_BI, F_SERIF, F_SERIF_B, F_SERIF_I, F_MONO, F_MONO_B };

void* nalloc(size_t n);
void nfree(void* p);
extern int g_pool;
void pool_reset(int which);
int pool_suspend(void);
void pool_resume(int v);
void* nzalloc(size_t n);
char* nstrdup(const char* s);
char* nstrndup(const char* s, int n);
void nwe_arena_reset(void);
void nwe_arena_init(void);
size_t nwe_arena_used(void);
void nwe_fail(const char* why);

typedef struct { char* s; int n, cap; } sbuf;
void sb_putc(sbuf* b, int c);
void sb_puts(sbuf* b, const char* s);
void sb_putn(sbuf* b, const char* s, int n);
void sb_putu8(sbuf* b, int cp);
char* sb_str(sbuf* b);

int u8dec(const char* s, int n, int* cp);
int lc(int c);
int ieq(const char* a, const char* b);
int ieqn(const char* a, const char* b, int n);
int istarts(const char* s, const char* p);
int is_ws(int c);
float parse_float(const char* s, const char** end);
void url_resolve(const char* base, const char* rel, char* out, int cap);
void url_origin(const char* url, char* out, int cap);
void msort(void* base, int n, int sz, int (*cmp)(const void*, const void*));

typedef struct { int face; float size; int ascent, descent, lineh; float xh; } fontm;
void font_metrics(int face, float size, fontm* m);
float font_adv(int face, float size, int cp);
float text_width(int face, float size, const char* s, int n, float ls);
void font_draw(unsigned* fb, int stride, int cx0, int cy0, int cx1, int cy1, int face, float size, float x, float y, const char* s, int n, unsigned col, float ls);
void font_cache_reset(void);

enum { NT_ELEM = 1, NT_TEXT = 3, NT_COMMENT = 8, NT_DOC = 9, NT_FRAG = 11 };

enum {
    T_UNKNOWN, T_HTML, T_HEAD, T_BODY, T_TITLE, T_META, T_LINK, T_STYLE, T_SCRIPT, T_NOSCRIPT, T_BASE,
    T_DIV, T_SPAN, T_P, T_A, T_IMG, T_BR, T_HR, T_H1, T_H2, T_H3, T_H4, T_H5, T_H6,
    T_UL, T_OL, T_LI, T_DL, T_DT, T_DD, T_TABLE, T_THEAD, T_TBODY, T_TFOOT, T_TR, T_TD, T_TH, T_CAPTION, T_COL, T_COLGROUP,
    T_FORM, T_INPUT, T_BUTTON, T_SELECT, T_OPTION, T_OPTGROUP, T_TEXTAREA, T_LABEL, T_FIELDSET, T_LEGEND,
    T_B, T_STRONG, T_I, T_EM, T_U, T_S, T_STRIKE, T_DEL, T_INS, T_SMALL, T_BIG, T_SUB, T_SUP, T_CODE, T_PRE, T_KBD, T_SAMP, T_TT, T_VAR,
    T_BLOCKQUOTE, T_Q, T_CITE, T_ABBR, T_MARK, T_TIME, T_ADDRESS, T_CENTER, T_FONT,
    T_SECTION, T_ARTICLE, T_ASIDE, T_NAV, T_HEADER, T_FOOTER, T_MAIN, T_FIGURE, T_FIGCAPTION, T_DETAILS, T_SUMMARY,
    T_SVG, T_CANVAS, T_VIDEO, T_AUDIO, T_IFRAME, T_OBJECT, T_EMBED, T_SOURCE, T_PICTURE, T_TEMPLATE, T_AREA, T_MAP,
    T_WBR, T_TRACK, T_PARAM, T_DIALOG, T_PROGRESS, T_METER, T_OUTPUT, T_DATA, T_NOBR, T_MENU, T_HGROUP, T_SEARCH,
    T_PATH, T_CIRCLE, T_RECT, T_G, T_POLYGON, T_LINE, T_POLYLINE, T_ELLIPSE, T_USE,
    T_COUNT
};
int tag_id(const char* name);
extern const char* tag_names[];

typedef struct { char* name; char* val; } nattr;
struct style;
struct box;
struct listener;
struct nimg;

typedef struct node {
    int type, tag, uid;
    char* name;
    nattr* attrs;
    int nattrs, cattrs;
    char* text;
    int tlen;
    struct node *parent, *first, *last, *next, *prev;
    struct style* st;
    struct style* st_before;
    struct style* st_after;
    struct box* box;
    void* jsobj;
    struct listener* ls;
    char* idecl_src;
    void* idecl;
    unsigned flags;
    char* value;
    int vlen, vcap, caret;
    int checked, selidx, dirtyval;
    struct nimg* img;
    float scrolly;
    int media;
    unsigned agen;
    struct node* shadow;
    struct node* host;
} node;

enum { NF_HOVER = 1, NF_FOCUS = 2, NF_ACTIVE = 4, NF_SCRIPTDONE = 8, NF_OPEN = 16, NF_DISABLED = 32, NF_SELOPEN = 64, NF_INLINEDIRTY = 128 };

node* node_new(int type);
node* elem_new(const char* name);
node* text_new(const char* s, int n);
void node_append(node* p, node* c);
void node_insert_before(node* p, node* c, node* ref);
void node_remove(node* c);
const char* attr_get(node* n, const char* name);
void attr_set(node* n, const char* name, const char* val);
void attr_del(node* n, const char* name);
int has_class(node* n, const char* cls);
node* doc_body(void);
node* doc_head(void);
node* doc_elem(void);
node* find_id(node* root, const char* id);
void text_content(node* n, sbuf* b);
void serialize(node* n, sbuf* b, int outer);
node* clone_node(node* n, int deep);
int node_contains(node* a, node* b);
void parse_html(node* doc, const char* src, int len, int run_scripts);
node* parse_fragment(node* ctx, const char* src, int len);
void doc_write(const char* s, int n);
int decode_entity(const char* s, int n, int* used);

typedef struct { float px, pct, lo, hi; unsigned char k; } L;
float lres(L l, float base);
int lauto(L l);
enum { LK_AUTO, LK_VAL, LK_NONE, LK_MINC, LK_MAXC, LK_FITC };

enum { D_NONE, D_INLINE, D_BLOCK, D_IBLOCK, D_LISTITEM, D_FLEX, D_IFLEX, D_GRID, D_IGRID, D_TABLE, D_ITABLE, D_TROW, D_TCELL, D_TROWGROUP, D_THEADG, D_TFOOTG, D_TCAPTION, D_TCOL, D_TCOLGROUP, D_CONTENTS };
enum { P_STATIC, P_RELATIVE, P_ABSOLUTE, P_FIXED, P_STICKY };
enum { FL_NONE, FL_LEFT, FL_RIGHT };
enum { OV_VISIBLE, OV_HIDDEN, OV_SCROLL, OV_AUTO, OV_CLIP };
enum { WS_NORMAL, WS_NOWRAP, WS_PRE, WS_PREWRAP, WS_PRELINE };
enum { TA_LEFT, TA_RIGHT, TA_CENTER, TA_JUSTIFY, TA_START, TA_END };
enum { VA_BASELINE, VA_MIDDLE, VA_TOP, VA_BOTTOM, VA_SUB, VA_SUPER, VA_TEXTTOP, VA_TEXTBOTTOM };
enum { BS_NONE, BS_SOLID, BS_DASHED, BS_DOTTED, BS_DOUBLE, BS_GROOVE, BS_RIDGE, BS_INSET, BS_OUTSET, BS_HIDDEN };
enum { LS_NONE, LS_DISC, LS_CIRCLE, LS_SQUARE, LS_DECIMAL, LS_LALPHA, LS_UALPHA, LS_LROMAN, LS_UROMAN };
enum { FD_ROW, FD_ROWREV, FD_COL, FD_COLREV };
enum { AL_AUTO, AL_START, AL_END, AL_CENTER, AL_STRETCH, AL_BASELINE, AL_BETWEEN, AL_AROUND, AL_EVENLY, AL_NORMAL };

typedef struct gtrack { unsigned char k; float v, minv; unsigned char mink; } gtrack;
enum { GT_PX, GT_PCT, GT_FR, GT_AUTO, GT_MINC, GT_MAXC };

typedef struct gstop { unsigned col; float pos; } gstop;
typedef struct bgimg {
    int kind;
    float angle;
    int nstops;
    gstop stops[8];
    char* url;
    struct nimg* img;
    struct bgimg* next;
} bgimg;

typedef struct cvar { char* name; char* val; struct cvar* next; } cvar;
enum { TF_TRANSLATE, TF_SCALE, TF_ROTATE, TF_SKEW, TF_MATRIX };
typedef struct tfn { unsigned char t; float v[6]; L l[2]; } tfn;
typedef struct tfunc { unsigned char k; int steps; float p[4]; } tfunc;
typedef struct animspec { char* name; float dur, delay, iter; unsigned char dir, fill, paused; tfunc tf; } animspec;
typedef struct transspec { short prop; float dur, delay; tfunc tf; } transspec;

typedef struct style {
    unsigned char display, position, flt, clear, boxsizing, visibility, ovx, ovy, ws, talign, tdec, ttrans, fstyle, ffam, lstype, lspos, valign, cursor, bcollapse, wbreak, pevents, tover, objfit, bgrepeat, bgsizek, bgattach;
    unsigned char flexdir, flexwrap, justify, alignitems, alignself, aligncontent, justifyitems, justifyself, gautoflow;
    unsigned char bstyle[4], ostyle;
    short fweight;
    int face;
    float fsize, lh, lhmul, lspace, wspace, tindent;
    unsigned color, bgcolor, bcolor[4], ocolor, scolor, tscolor, caretcol, accent;
    bgimg* bg;
    L bgsx, bgsy, bgpx, bgpy;
    L width, height, minw, minh, maxw, maxh, margin[4], padding[4], inset[4], basis, tx, ty, gcolgap, growgap;
    float border[4], radius[4], owidth, ooffset;
    int zindex, zauto, order;
    float aspect;
    unsigned fill, stroke;
    float strokew;
    int hasfill, hasstroke, hasstrokew;
    unsigned char clipall, bgcliptext, hastxtfill, lineclamp;
    unsigned txtfill;
    float opacity, grow, shrink, sx, sy, sblur, sspread, tsx, tsy, tsblur, scale, rotate, valignpx;
    int hasshadow, sinset, hastshadow, hastransform, colspan, rowspan, hasvalignpx;
    gtrack* gcols;
    gtrack* grows;
    int ngcols, ngrows, gcs, gce, grs, gre, gautocols;
    gtrack gautorow;
    float bspacingx, bspacingy;
    char* content;
    char* areas;
    char* gta;
    cvar* vars;
    char* ffamily;
    tfn* tf;
    unsigned char ntf, hasmatrix, nanim, ntrans, tfcap;
    L tox, toy;
    animspec* anims;
    transspec* trans;
} style;
void tf_matrix(style* s, float w, float h, float* m);
int tf_has_linear(style* s);

typedef struct frag {
    int kind;
    float x, y, w, h, base;
    const char* s;
    int n;
    style* st;
    node* n_;
    struct box* b;
} frag;
enum { FK_TEXT, FK_BOX, FK_IBG, FK_MARKER, FK_CARET };

typedef struct box {
    int kind;
    node* n;
    style* st;
    struct box *parent, *first, *last, *next;
    float x, y, w, h;
    float m[4], p[4], b[4];
    float sx, sy;
    float minc, maxc;
    int intr, absflag, bfc, lineidx;
    float baseline;
    frag* fr;
    int nfr, cfr;
    float clampy;
    struct box* cb;
    float staticx, staticy;
    float ox, oy;
    float btx, bty;
    int pseudo;
    const char* ptext;
    int ptlen;
    int marker;
    float contenth;
    float mte, mbe;
    int inl, bfcroot, lindex, haslines;
    float firstbase;
    struct box* nextabs;
} box;
enum { BX_BLOCK, BX_INLINE, BX_TEXT, BX_ATOMIC, BX_ANON, BX_BR, BX_REPLACED, BX_FLEX, BX_GRID, BX_TABLE, BX_ROW, BX_CELL, BX_ROWGROUP, BX_CAPTION, BX_LISTMARK };

typedef struct nimg {
    char* url;
    unsigned* px;
    int w, h, state;
    struct nimg* next;
} nimg;
nimg* img_get(const char* url);
void wm_free(node* n);
void wm_reset_all(void);
int wm_create(node* n, const char* url, int split, long long start_ms, int paused);
int wm_id(node* n);
int wm_dims(node* n, int* w, int* h);
void wm_begin_paint(int vx, int vy);
void wm_paint(node* n, float x, float y, float w, float h, int cx0, int cy0, int cx1, int cy1, float op, unsigned* fb, int stride);
int wm_fast(unsigned* fb, int stride, int* rx, int* ry, int* rw, int* rh);
int wm_tick(void);
int wm_click(node* n, float vx, float vy);
int img_pending(void);
int img_load_one(void);

typedef struct rule rule;
void css_reset(void);
void css_add_sheet(const char* src, int len, const char* base, int origin);
void css_ua_init(void);
void style_all(node* root, float vw, float vh);
style* style_of(node* n);
int sel_match_str(node* n, const char* sel, int* err);
node* qsel(node* root, const char* sel, int all, node*** out, int* nout);
void css_decl_apply_inline(node* n, const char* prop, const char* val);
int css_parse_color(const char* s, int n, unsigned* out);
const char* css_prop_get(node* n, const char* prop, char* out, int cap);
void css_computed_get(node* n, const char* prop, char* out, int cap);
int css_media_match(const char* q, int n);
void css_bump(void);
extern int g_css_gen;

void layout_all(float vw, float vh);
style* anon_style(style* par, int display);
box* hit_test(float x, float y, frag** frout);
int caret_pos(node* tn, int off, float* x, float* y, float* h);
extern int g_mut_on;
extern void (*g_mut_hook)(int, node*, node*, node*, node*, const char*, const char*);
void dom_mut_text(node* t, const char* old);
extern node* g_ce_node;
extern int g_ce_off;
node* edit_host(node* n);
void js_ce_call(const char* fn, node* a, int b);
extern box* g_root;
extern float g_doch, g_docw;
void box_rect(node* n, float* x, float* y, float* w, float* h);
box* box_of(node* n);

void paint_all(unsigned* fb, int stride, int fbw, int fbh, int vx, int vy, int vw, int vh, float scroll);
void fill_rect_a(unsigned* fb, int stride, int cx0, int cy0, int cx1, int cy1, float x, float y, float w, float h, unsigned col);

typedef struct listener { char* type; void* fn; int capture, once; struct listener* next; } listener;
int js_init(void);
void js_run_script(const char* src, int len, const char* name, int module);
void js_dispatch(node* target, const char* type, int bubbles, float cx, float cy, int key, int* prevented);
int js_tick(void);
void js_drain(void);
int js_eval_expr(const char* src, char* out, int cap);
void js_fire_load(void);

typedef struct {
    char url[1024];
    char base[1024];
    char origin[256];
    float vw, vh, scroll;
    int dirty_style, dirty_layout, dirty_paint;
    node* doc;
    node* focus;
    node* hover;
    node* active;
    int parsing;
    sbuf wbuf;
    int nav_pending;
    char nav[1024];
    char navmethod[8];
    char* navbody;
    int navblen;
    char title[128];
    int uidc;
    int loaded;
    int nodecount;
    unsigned long t0;
    int quirks;
    node* cur_script;
    float lastw;
    int find_n, find_cur;
} nwe_doc;
extern nwe_doc G;

void eng_navigate(const char* url, const char* method, const char* body, int blen);
void eng_invalidate(int what);
char* eng_fetch_text(const char* url, int* len);
void eng_form_submit(node* form, node* submitter);
void eng_scroll_to(float y);
void eng_layout_if_dirty(void);

void nwe_check_stop(void);
extern int g_nwe_user_stop;
#endif
