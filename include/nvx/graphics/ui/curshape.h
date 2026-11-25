#ifndef NVX_GRAPHICS_UI_CURSHAPE_H
#define NVX_GRAPHICS_UI_CURSHAPE_H

enum{ CUR_ARROW, CUR_TEXT, CUR_BUSY, CUR_WAIT, CUR_HAND, CUR_MOVE, CUR_NO, CUR_N };
extern int g_cur_shape, g_cur_force;
void cur_busy_note(void);
int cur_text_app(int i);
int cur_shape_pick(void);
int cur_busy_phase(void);
int cur_shape_draw(int px,int py,int shape);

#endif
