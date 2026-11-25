#include <stdio.h>
#include <string.h>
typedef unsigned int u32; typedef unsigned char u8; typedef unsigned long long u64;
static int bad=0;
#define T(c) do{ if(!(c)){ printf("FAIL %s line %d\n",#c,__LINE__); bad++; } }while(0)
u32 W=200, H=100, SW=200; static u32 scene[200*100]; int g_gfx=0, g_accel2d=0, g_rt=0;
#define SCENE scene
static void sse_fill32(u32* p,u32 c,int n){ for(int i=0;i<n;i++)p[i]=c; }
static void gq_fill(int t,int x,int y,int w,int h,u32 c){ (void)t; (void)x; (void)y; (void)w; (void)h; (void)c; }
int g_gl[95], g_gw[95], g_gm_ready=1;
static int gadv(char ch,int s){ (void)ch; return 7*s; }
static int pw_str(const char* t,int s){ return (int)strlen(t)*7*s; }
static const char* volatile g_crumb_seen[32]; static volatile u32 g_crumb_sk;
#define CRUMB_SEEN 32
#define WIN_TITLE_MAX 158
int g_amdsdma_sys=0; int amdsdma_fill_sys(u32* s,u32 sw,u32 h,int x,int y,int w,int hh,u32 c){ (void)s; (void)sw; (void)h; (void)x; (void)y; (void)w; (void)hh; (void)c; return -1; }
#include "ui_ext.c"
static int rows(int y0,int y1,u32 c){ int n=0; for(int y=0;y<(int)H;y++){ int hit=0; for(int x=0;x<(int)W;x++) if(scene[y*SW+x]==c)hit=1; if(hit&&(y<y0||y>=y1))return -1; n+=hit; } return n; }
int main(void){
    fill_rect(0,0,200,100,0); fb_clip_y(20,60); fill_rect(10,0,50,100,7); T(rows(20,60,7)==40); T(g_clip_maxy==100);
    fill_rect(10,70,20,20,9); T(rows(0,0,9)==0); fill_rect(10,10,20,5,9); T(rows(0,0,9)==0); fb_clip_off(); fill_rect(100,0,10,100,5); T(rows(0,100,5)==100);
    fb_clip_y(30,40); fill_rect(0,-50,10,10,3); fill_rect(0,1000,10,10,3); fb_clip_off(); T(rows(0,0,3)==0);
    char ln[2][128]; int n=st_wrap("With limits follows the country's power rules for each speed",300,ln,2); T(n==2); T(pw_str(ln[0],1)<=300); T(pw_str(ln[1],1)<=300);
    { char j[256]; snprintf(j,sizeof j,"%s %s",ln[0],ln[1]); T(!strcmp(j,"With limits follows the country's power rules for each speed")); }
    n=st_wrap("short",200,ln,2); T(n==1&&!strcmp(ln[0],"short"));
    n=st_wrap("one two three four five six seven eight nine ten eleven twelve thirteen fourteen fifteen sixteen",140,ln,2); T(n==2); T(strstr(ln[1],"...")!=0); T(pw_str(ln[1],1)<=140);
    n=st_wrap("Averyveryveryverylongwordwithoutanyspacesthatcannotbreak",100,ln,2); T(n==2); T(pw_str(ln[0],1)<=100);
    { char f[64]; strcpy(f,"Version 11.22  -  1535 MB memory"); st_fit(f,400); T(!strcmp(f,"Version 11.22  -  1535 MB memory")); st_fit(f,100); T(pw_str(f,1)<=100); T(strstr(f,"...")!=0); strcpy(f,"Intel Core"); st_fit(f,20); T(pw_str(f,1)<=40); }
    const char* A="@process_input"; const char* B="@video update"; const char* L[40]; char nm[40][8];
    T(!crumb_seen(A)); crumb_mark(A); T(crumb_seen(A)); crumb_mark(B);
    for(int i=0;i<6;i++){ snprintf(nm[i],8,"c%d",i); L[i]=nm[i]; crumb_mark(L[i]); }
    int printed=0; for(int f=0;f<1000;f++){ const char* cyc[8]={A,B,L[0],L[1],L[2],L[3],L[4],L[5]}; for(int k=0;k<8;k++) if(!crumb_seen(cyc[k])){ crumb_mark(cyc[k]); printed++; } } T(printed==0);
    for(int i=6;i<40;i++){ snprintf(nm[i],8,"c%d",i); L[i]=nm[i]; if(!crumb_seen(L[i]))crumb_mark(L[i]); } T(!crumb_seen(A));
    T(win_title_fit(8*62+10)==62); T(win_title_fit(8*90+10)==90); T(win_title_fit(8*90+17)==90); T(win_title_fit(0)==0); T(win_title_fit(-50)==0); T(win_title_fit(100000)==WIN_TITLE_MAX);
    if(!bad)printf("ui: all ok\n"); return bad?1:0; }
