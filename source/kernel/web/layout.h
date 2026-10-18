#ifndef NWE_LAYOUT_H
#define NWE_LAYOUT_H
#include "web/nwe.h"

typedef struct flt { float x, y, w, h; int side; struct flt* next; } flt;
typedef struct bfc { flt* floats; } bfc;

box* mkbox(int kind, node* n, style* st);
void lay(box* b, float x, float y, float cbw, float cbh, bfc* f, float forcew, float forceh);
void intrinsic(box* b, float* mn, float* mx);
float layout_flex(box* b, float cx, float cy, float cw, float ch);
float layout_grid(box* b, float cx, float cy, float cw, float ch);
float layout_table(box* b, float cx, float cy, float cw, float ch);
void shift_box(box* b, float dx, float dy);
void box_edges(box* b, float cbw);
float lineh_of(style* st);
int in_flow(box* c);
int is_block_level(box* c);
float spec_content_h(box* b, float cbh);
float clamp_w(box* b, float w, float cbw);
float clamp_h(box* b, float h, float cbh);
float replaced_size(box* b, float cbw, float cbh, float* outh, float forcew);
float shrink_fit(box* b, float avail);
void add_abs(box* b);
void inline_measure(box* c, float* mn, float* mx);
extern float g_vw, g_vh;
extern int g_lay_depth;

#endif
