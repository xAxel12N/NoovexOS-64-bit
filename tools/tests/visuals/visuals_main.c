#include <stdio.h>
#include <string.h>
typedef unsigned int u32; typedef unsigned char u8; typedef unsigned long long u64;
static int bad=0;
#define T(c) do{ if(!(c)){ printf("FAIL %s line %d\n",#c,__LINE__); bad++; } }while(0)
static u32 rgb(int r,int g,int b){ return ((u32)r<<16)|((u32)g<<8)|(u32)b; }
static int slen(const char* s){ return (int)strlen(s); }
static char drawn[16][160]; static int drawn_x[16], drawn_y[16], ndrawn=0, nrect=0, rect_x[8], rect_y[8];
static int dstr(int x,int y,const char* s,u32 c,int sc){ (void)c; if(ndrawn<16){ strncpy(drawn[ndrawn],s,159); drawn[ndrawn][159]=0; drawn_x[ndrawn]=x; drawn_y[ndrawn]=y; ndrawn++; } return x+(int)strlen(s)*8*sc; }
static void fill_rect(int x,int y,int w,int h,u32 c){ (void)w; (void)h; (void)c; if(nrect<8){ rect_x[nrect]=x; rect_y[nrect]=y; } nrect++; }
static int pw_str(const char* t,int s){ int w=0; for(int i=0;t[i];i++) w+=(t[i]=='.'?3:t[i]==' '?4:7)*s; return w; }
static void re_cp(char* d,const char* s,int cap){ int i=0; for(;s[i]&&i<cap-1;i++)d[i]=s[i]; d[i]=0; }
static void reset(void){ ndrawn=0; nrect=0; }
#include "visuals_ext.c"
int main(void){
    const char* adv="GPU drawing is off, so the graphics chip could not be tested. Turn it on in Settings > Graphics.";
    reset(); int y=bn_wrap(36,100,36+400,1000,adv); T(ndrawn>=2); T(y==100+18*ndrawn);
    { char j[512]={0}; for(int i=0;i<ndrawn;i++){ T((int)strlen(drawn[i])*8<=400); T(drawn_x[i]==36); T(drawn_y[i]==100+18*i); if(i)strcat(j," "); strcat(j,drawn[i]); } T(!strcmp(j,adv)); }
    reset(); y=bn_wrap(36,100,36+400,110,adv); T(ndrawn==1); T(y==118);
    reset(); bn_wrap(36,100,36+800,1000,"short line"); T(ndrawn==1&&!strcmp(drawn[0],"short line"));
    reset(); bn_wrap(36,100,36+80,1000,"Averyveryverylongwordwithoutspaces"); T(ndrawn>=3); for(int i=0;i<ndrawn;i++)T((int)strlen(drawn[i])*8<=80);
    char t[100]; re_fit(t,"Accent colour 0-4 (blue, purple, green, orange, red)",150,100); T(pw_str(t,1)<=150); T(strlen(t)>3&&!strcmp(t+strlen(t)-3,"...")); T(!strncmp(t,"Accent colour",13));
    re_fit(t,"Desktop theme 0-5",400,100); T(!strcmp(t,"Desktop theme 0-5"));
    re_fit(t,"",100,100); T(!t[0]);
    re_fit(t,"abcdefghij",20,100); T(pw_str(t,1)<=20||!strcmp(t,"..."));
    reset(); tb_label(10,8,"Registry Editor",15*8); T(ndrawn==1&&!strcmp(drawn[0],"Registry Editor")); T(nrect==0);
    reset(); tb_label(10,8,"Registry Editor",7*8); T(ndrawn==1&&!strcmp(drawn[0],"Regist")); T(nrect==3); T(rect_x[0]==10+6*8); T(rect_x[2]+2<=10+7*8); T(rect_y[0]==18);
    reset(); tb_label(10,8,"Task Manager",5*8); T(ndrawn==1&&!strcmp(drawn[0],"Task")); T(nrect==3);
    reset(); tb_label(10,8,"Benchmark",0); T(ndrawn==0&&nrect==0);
    if(!bad)printf("visuals: all ok\n"); return bad?1:0; }
