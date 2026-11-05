#ifndef NVX_ASSETS_UIFONT_H
#define NVX_ASSETS_UIFONT_H
typedef struct { short adv, w, h, x, y; int off; } uf_glyph;
typedef struct { short px, asc, desc; int first; } uf_font;
enum { UF_13, UF_15, UF_15M, UF_18, UF_24B, UF_26B, UF_40B, UF_84L, UF_N };
extern const unsigned char uf_data[];
extern const uf_glyph uf_glyphs[];
extern const uf_font uf_fonts[];
int uf_width(int f,const char* s);
int uf_draw(int x,int y,int f,const char* s,unsigned int c);
void uf_draw_ctr(int cx,int y,int f,const char* s,unsigned int c);
int uf_cap_mid(int f);
#endif
