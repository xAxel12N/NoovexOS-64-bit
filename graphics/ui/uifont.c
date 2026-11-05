#include "include/nvx/base.h"
#include "include/nvx/drivers/video/fb/framebuffer.h"
#include "include/nvx/assets/uifont.h"
static const uf_glyph* uf_g(int f,char ch){ int i=(unsigned char)ch; if(i<32||i>126)i='?'; return &uf_glyphs[uf_fonts[f].first+i-32]; }
int uf_width(int f,const char* s){ if(f<0||f>=UF_N)return 0; int w=0; for(int i=0;s[i];i++)w+=uf_g(f,s[i])->adv; return w; }
int uf_draw(int x,int y,int f,const char* s,unsigned int c){
    if(f<0||f>=UF_N)return x;
    u32* sc=SCENE;
    for(int i=0;s[i];i++){ const uf_glyph* g=uf_g(f,s[i]); const unsigned char* d=uf_data+g->off;
        for(int r=0;r<g->h;r++){ int py=y+g->y+r; if((u32)py>=H)continue; u32* row=sc+(u64)py*SW;
            for(int q=0;q<g->w;q++){ int a=d[r*g->w+q]; if(a<4)continue; int px=x+g->x+q; if((u32)px>=W)continue; row[px]=mix(row[px],c,a); } }
        x+=g->adv; }
    return x; }
void uf_draw_ctr(int cx,int y,int f,const char* s,unsigned int c){ uf_draw(cx-uf_width(f,s)/2,y,f,s,c); }
int uf_cap_mid(int f){ if(f<0||f>=UF_N)return 0; const uf_glyph* g=uf_g(f,'H'); return g->y+g->h/2; }
