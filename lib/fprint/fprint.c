#include "include/nvx/lib/fprint/fprint.h"

static const short fp_sq[65]={0,402,804,1205,1606,2006,2404,2801,3196,3590,3981,4370,4756,5139,5520,5897,6270,6639,7005,7366,7723,8076,8423,8765,9102,9434,9760,10080,10394,10702,11003,11297,11585,11866,12140,12406,12665,12916,13160,13395,13623,13842,14053,14256,14449,14635,14811,14978,15137,15286,15426,15557,15679,15791,15893,15986,16069,16143,16207,16261,16305,16340,16364,16379,16384};
static int fp_sin(int a){ a&=255; if(a<64)return fp_sq[a]; if(a<128)return fp_sq[128-a]; if(a<192)return -fp_sq[a-128]; return -fp_sq[256-a]; }
static int fp_cos(int a){ return fp_sin(a+64); }
static int fp_atan2(int y,int x){
    if(!x&&!y){ return 0; } int ax=x<0?-x:x, ay=y<0?-y:y, r;
    if(ax>=ay){ int z=ay*1024/ax; r=(z*32+ (z*(1024-z)/1024)*9)/1024; } else { int z=ax*1024/ay; r=64-(z*32+(z*(1024-z)/1024)*9)/1024; }
    if(x<0){ r=128-r; } if(y<0){ r=256-r; } return r&255;
}
static int fp_adiff(int a,int b){ int d=(a-b)&255; return d>128?256-d:d; }

static unsigned int fp_ii[FP_MAXPIX+FP_MAXW*2+2];
static unsigned char fp_bw[FP_MAXPIX], fp_tmp[FP_MAXPIX], fp_mask[FP_MAXPIX];

static void fp_binarize(const unsigned char* g,int w,int h){
    int W=w+1; for(int x=0;x<=w;x++)fp_ii[x]=0;
    for(int y=0;y<h;y++){ unsigned int s=0; fp_ii[(y+1)*W]=0; for(int x=0;x<w;x++){ s+=g[y*w+x]; fp_ii[(y+1)*W+x+1]=fp_ii[y*W+x+1]+s; } }
    const int R=6;
    for(int y=0;y<h;y++)for(int x=0;x<w;x++){
        int x0=x-R<0?0:x-R, x1=x+R+1>w?w:x+R+1, y0=y-R<0?0:y-R, y1=y+R+1>h?h:y+R+1;
        unsigned int s=fp_ii[y1*W+x1]-fp_ii[y0*W+x1]-fp_ii[y1*W+x0]+fp_ii[y0*W+x0]; int n=(x1-x0)*(y1-y0);
        fp_bw[y*w+x]=(unsigned)g[y*w+x]*n<s; }
    const int B=8;
    for(int by=0;by<h;by+=B)for(int bx=0;bx<w;bx+=B){
        int s=0,s2=0,n=0; for(int y=by;y<by+B&&y<h;y++)for(int x=bx;x<bx+B&&x<w;x++){ int v=g[y*w+x]; s+=v; s2+=v*v; n++; }
        int var=(s2-s*s/n)/n, on=var>120; for(int y=by;y<by+B&&y<h;y++)for(int x=bx;x<bx+B&&x<w;x++)fp_mask[y*w+x]=(unsigned char)on; }
    for(int y=1;y<h-1;y++)for(int x=1;x<w-1;x++){ int c=0; for(int dy=-1;dy<=1;dy++)for(int dx=-1;dx<=1;dx++)c+=fp_bw[(y+dy)*w+x+dx]; fp_tmp[y*w+x]=c>=5; }
    for(int y=0;y<h;y++)for(int x=0;x<w;x++)fp_bw[y*w+x]=(y>0&&y<h-1&&x>0&&x<w-1)?fp_tmp[y*w+x]:0;
}
static const int fp_nx[8]={0,1,1,1,0,-1,-1,-1}, fp_ny[8]={-1,-1,0,1,1,1,0,-1};
static void fp_thin(int w,int h){
    for(int it=0,ch=1;ch&&it<64;it++){ ch=0;
        for(int pass=0;pass<2;pass++){ int nd=0;
            for(int y=1;y<h-1;y++)for(int x=1;x<w-1;x++){ fp_tmp[y*w+x]=0; if(!fp_bw[y*w+x])continue;
                int p[8],b=0,a=0; for(int k=0;k<8;k++){ p[k]=fp_bw[(y+fp_ny[k])*w+x+fp_nx[k]]; b+=p[k]; }
                for(int k=0;k<8;k++)if(!p[k]&&p[(k+1)&7])a++;
                if(b<2||b>6||a!=1)continue;
                if(!pass){ if(p[0]&&p[2]&&p[4])continue; if(p[2]&&p[4]&&p[6])continue; } else { if(p[0]&&p[2]&&p[6])continue; if(p[0]&&p[4]&&p[6])continue; }
                fp_tmp[y*w+x]=1; nd++; }
            if(nd){ ch=1; for(int i=0;i<w*h;i++)if(fp_tmp[i])fp_bw[i]=0; } } }
}
static int fp_trace(int w,int h,int x,int y,int steps,int* ex,int* ey){
    int px=-1,py=-1; for(int s=0;s<steps;s++){ int f=0;
        for(int k=0;k<8;k++){ int nx=x+fp_nx[k], ny=y+fp_ny[k]; if(nx<1||ny<1||nx>=w-1||ny>=h-1||(nx==px&&ny==py)||!fp_bw[ny*w+nx])continue; px=x; py=y; x=nx; y=ny; f=1; break; }
        if(!f)break; }
    *ex=x; *ey=y; return 1;
}
static int fp_inside(int w,int h,int x,int y,int r){
    for(int dy=-r;dy<=r;dy+=r)for(int dx=-r;dx<=r;dx+=r){ int xx=x+dx, yy=y+dy; if(xx<0||yy<0||xx>=w||yy>=h||!fp_mask[yy*w+xx])return 0; }
    return 1;
}
int fp_extract(const unsigned char* img,int w,int h,fp_tpl_t* t){
    t->n=0; if(!img||w<32||h<32||w>FP_MAXW||h>FP_MAXW||w*h>FP_MAXPIX)return -1;
    fp_binarize(img,w,h); fp_thin(w,h);
    static fp_min_t c[512]; int nc=0;
    for(int y=2;y<h-2;y++)for(int x=2;x<w-2;x++){ if(!fp_bw[y*w+x])continue;
        int p[8],cn=0; for(int k=0;k<8;k++)p[k]=fp_bw[(y+fp_ny[k])*w+x+fp_nx[k]];
        for(int k=0;k<8;k++){ cn+=p[k]!=p[(k+1)&7]; } cn/=2;
        if((cn!=1&&cn!=3)||!fp_inside(w,h,x,y,FP_EDGE))continue;
        int ex,ey,a;
        if(cn==1){ fp_trace(w,h,x,y,8,&ex,&ey); a=fp_atan2(y-ey,x-ex); }
        else { int sx=0,sy=0; for(int k=0;k<8;k++)if(p[k]){ fp_trace(w,h,x+fp_nx[k],y+fp_ny[k],7,&ex,&ey); sx+=ex-x; sy+=ey-y; } a=fp_atan2(-sy,-sx); }
        if(nc<512){ c[nc].x=(short)x; c[nc].y=(short)y; c[nc].a=(unsigned char)a; c[nc].t=(unsigned char)(cn==3); nc++; } }
    static unsigned char bad[512]; for(int i=0;i<nc;i++)bad[i]=0;
    for(int i=0;i<nc;i++)for(int j=i+1;j<nc;j++){ int dx=c[i].x-c[j].x, dy=c[i].y-c[j].y; if(dx*dx+dy*dy<64){ bad[i]=bad[j]=1; } }
    int cx=w/2, cy=h/2;
    for(int k=0;k<FP_MAXM;k++){ int bi=-1,bd=0x7fffffff; for(int i=0;i<nc;i++){ if(bad[i])continue; int dx=c[i].x-cx, dy=c[i].y-cy, d=dx*dx+dy*dy; if(d<bd){ bd=d; bi=i; } }
        if(bi<0){ break; } t->m[t->n++]=c[bi]; bad[bi]=1; }
    return t->n;
}
static int fp_box(const fp_tpl_t* T,int* b){ if(T->n<1)return 0; b[0]=b[2]=T->m[0].x; b[1]=b[3]=T->m[0].y;
    for(int i=1;i<T->n;i++){ int x=T->m[i].x, y=T->m[i].y; if(x<b[0])b[0]=x; if(x>b[2])b[2]=x; if(y<b[1])b[1]=y; if(y>b[3])b[3]=y; }
    b[0]-=FP_BOXM; b[1]-=FP_BOXM; b[2]+=FP_BOXM; b[3]+=FP_BOXM; return 1; }
static int fp_inbox(const int* b,int x,int y){ return x>=b[0]&&x<=b[2]&&y>=b[1]&&y<=b[3]; }
static int fp_match1(const fp_tpl_t* A,const fp_tpl_t* B){
    static unsigned char used[FP_MAXM]; int bb[4], ba[4], best=0;
    if(A->n<FP_MINCNT||B->n<FP_MINCNT||!fp_box(B,bb)||!fp_box(A,ba))return 0;
    for(int i=0;i<A->n;i++)for(int j=0;j<B->n;j++){ const fp_min_t* a=&A->m[i]; const fp_min_t* b=&B->m[j];
        int r=(b->a-a->a)&255, cs=fp_cos(r), sn=fp_sin(r), cnt=0, ova=0, ovb=0;
        for(int q=0;q<B->n;q++)used[q]=0;
        for(int k=0;k<A->n;k++){ const fp_min_t* m=&A->m[k]; int dx=m->x-a->x, dy=m->y-a->y;
            int tx=b->x+((dx*cs-dy*sn)>>14), ty=b->y+((dx*sn+dy*cs)>>14), ta=(m->a+r)&255, bq=-1, bd=FP_DTOL*FP_DTOL;
            if(fp_inbox(bb,tx,ty))ova++;
            for(int q=0;q<B->n;q++){ if(used[q])continue; int ex=B->m[q].x-tx, ey=B->m[q].y-ty, d=ex*ex+ey*ey; if(d<bd&&fp_adiff(B->m[q].a,ta)<=FP_ATOL){ bd=d; bq=q; } }
            if(bq>=0){ used[bq]=1; cnt++; } }
        if(cnt<FP_MINCNT)continue;
        for(int q=0;q<B->n;q++){ int dx=B->m[q].x-b->x, dy=B->m[q].y-b->y; int tx=a->x+((dx*cs+dy*sn)>>14), ty=a->y+((-dx*sn+dy*cs)>>14); if(fp_inbox(ba,tx,ty))ovb++; }
        ova=ova<FP_MINOV?FP_MINOV:ova; ovb=ovb<FP_MINOV?FP_MINOV:ovb;
        int sc=cnt*cnt*100/(ova*ovb); if(sc>100)sc=100; if(sc>best)best=sc; }
    return best;
}
int fp_match(const fp_tpl_t* A,const fp_tpl_t* B){ int a=fp_match1(A,B), b=fp_match1(B,A); return a<b?a:b; }
int fp_match_ok(int score){ return score>=FP_THRESH; }
int fp_match_set(const fp_tpl_t* T,int nt,const fp_tpl_t* p){ int s=0,mx=0; if(!T||!p||nt<=0||p->n<FP_MINCNT)return 0; if(nt>FP_NTPL)nt=FP_NTPL;
    for(int i=0;i<nt;i++){ int v=T[i].n<FP_MINN?0:fp_match(&T[i],p); s+=v; if(v>mx)mx=v; }
    if(nt<FP_NTPL)return fp_match_ok(mx)?FP_FUSE:0;
    return mx>=FP_FUSEMIN?s:0; }
int fp_set_ok(int score){ return score>=FP_FUSE; }
