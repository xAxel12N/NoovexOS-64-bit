#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "lib/fprint/fprint.c"
int (*fprint_capture)(unsigned char*,int,int*,int*);
#define NF 40
#define NI 6
static unsigned rs;
static double rnd(void){ rs=rs*1103515245u+12345u; return ((rs>>8)&0xffffff)/16777216.0; }
static double gauss(void){ double u=rnd()+1e-9, v=rnd(); return sqrt(-2*log(u))*cos(6.283185307*v); }
typedef struct { double f, cx, cy, k, sx[48], sy[48]; int sg[48], ns; } fg_t;
static void mkfinger(fg_t* g,unsigned seed){ rs=seed*2654435761u+7; g->f=6.283185307/(7.5+2*rnd()); g->cx=40+80*rnd(); g->cy=50+100*rnd(); g->k=0.004+0.008*rnd(); g->ns=30+(int)(10*rnd());
    for(int i=0;i<g->ns;i++){ g->sx[i]=10+140*rnd(); g->sy[i]=10+180*rnd(); g->sg[i]=rnd()<0.5?-1:1; } }
static void render(const fg_t* g,int W,int H,double rot,double tx,double ty,double noise,double warp,unsigned seed,unsigned char* o){
    rs=seed*40503u+11; double c=cos(rot), s=sin(rot), wa=warp*rnd(), wp=6.28*rnd();
    for(int y=0;y<H;y++)for(int x=0;x<W;x++){ double u=x-W/2.0-tx, v=y-H/2.0-ty, X=c*u+s*v+80, Y=-s*u+c*v+100;
        X+=wa*sin(Y/40.0+wp); Y+=wa*cos(X/40.0+wp);
        double ex=(X-80)/70, ey=(Y-100)/92; if(ex*ex+ey*ey>1){ o[y*W+x]=200; continue; }
        double dx=X-g->cx, dy=Y-g->cy, ph=g->f*sqrt(dx*dx+dy*dy*1.69)+g->k*dx*dx;
        for(int i=0;i<g->ns;i++)ph+=g->sg[i]*atan2(Y-g->sy[i],X-g->sx[i]);
        double v2=128+90*cos(ph)+gauss()*noise; o[y*W+x]=(unsigned char)(v2<0?0:v2>255?255:v2); }
}
static fp_tpl_t T[NF][NI]; static unsigned char img[FP_MAXPIX];
static int best3(int f,int g,int i){ return fp_match_set(T[f],FP_NTPL,&T[g][i]); }
static int run(int W,int H,int frrmax,int* fails){
    for(int f=0;f<NF;f++){ fg_t g; mkfinger(&g,1000+f);
        for(int i=0;i<NI;i++){ rs=f*977+i*31+5; double rot=i?(rnd()-0.5)*0.5:0, tx=i?(rnd()-0.5)*16:0, ty=i?(rnd()-0.5)*16:0; render(&g,W,H,rot,tx,ty,18,i?2.5:0,f*100+i,img); fp_extract(img,W,H,&T[f][i]); } }
    int gn=0,gok=0,in=0,iok=0,gmin=1000,imax=0;
    for(int f=0;f<NF;f++)for(int g=0;g<NF;g++)for(int i=FP_NTPL;i<NI;i++){ int sc=best3(f,g,i);
        if(getenv("FPDIST"))printf("%c %d\n",f==g?'G':'I',sc);
        if(f==g){ gn++; gok+=fp_set_ok(sc); if(sc<gmin)gmin=sc; } else { in++; iok+=fp_set_ok(sc); if(sc>imax)imax=sc; } }
    int sym=1; for(int f=0;f<NF;f++){ int a=fp_match(&T[f][0],&T[f][1]), b=fp_match(&T[f][1],&T[f][0]); if(a!=b)sym=0; }
    printf("  %dx%d, %d enrolled samples per finger: genuine %d/%d accepted (FRR %.2f%%, min score %d), impostor %d/%d accepted (FAR %.3f%%, max score %d), fused threshold %d (pair %d), order-%s\n",W,H,FP_NTPL,gok,gn,100.0*(gn-gok)/gn,gmin,iok,in,100.0*iok/in,imax,FP_FUSE,FP_THRESH,sym?"independent":"DEPENDENT");
    if(iok*1000>in){ printf("  FAIL FAR above 0.1%%\n"); (*fails)++; }
    if((gn-gok)*100>gn*frrmax){ printf("  FAIL FRR above %d%%\n",frrmax); (*fails)++; }
    if(!sym){ printf("  FAIL score depends on argument order\n"); (*fails)++; }
    return in;
}
int main(void){ int fails=0, tot=0;
    tot+=run(160,200,3,&fails); tot+=run(112,144,25,&fails);
    fp_tpl_t e={0}; if(fp_match(&e,&T[0][0])!=0||fp_match_ok(fp_match(&T[0][0],&e))){ puts("  FAIL empty template"); fails++; }
    fp_tpl_t few=T[0][0]; few.n=6; if(fp_match_ok(fp_match(&few,&T[0][1]))){ puts("  FAIL a 6-minutia template must never match"); fails++; }
    fp_tpl_t fs[3]={few,few,few}; if(fp_set_ok(fp_match_set(fs,3,&T[0][1]))||fp_set_ok(fp_match_set(T[0],0,&T[0][1]))||fp_set_ok(fp_match_set(T[1],3,&e))){ puts("  FAIL fused score accepted a 6-minutia set, an empty set or an empty probe"); fails++; }
    if(fp_set_ok(fp_match_set(T[0],1,&T[0][3]))!=fp_match_ok(fp_match(&T[0][0],&T[0][3]))){ puts("  FAIL a single old template must use the pair threshold"); fails++; }
    printf(fails?"%d FAILED\n":"ALL PASS (%d impostor comparisons)\n",fails?fails:tot); return fails!=0; }
