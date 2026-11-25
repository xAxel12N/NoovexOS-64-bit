#include <stdio.h>
#include <stdlib.h>
#include "include/nvx/lib/fprint/fprint.h"
int (*fprint_capture)(unsigned char*,int,int*,int*);
static unsigned char img[FP_MAXPIX];
static int load(const char* p,int* w,int* h){ FILE* f=fopen(p,"rb"); if(!f)return -1; int m; if(fscanf(f,"P5 %d %d %d",w,h,&m)!=3){ fclose(f); return -1; } fgetc(f); int n=(int)fread(img,1,(size_t)(*w**h),f); fclose(f); return n==*w**h?0:-1; }
int main(int argc,char** argv){
    const char* d=argc>1?argv[1]:"/tmp/fpimg"; static fp_tpl_t T[6][3]; char p[256]; int w,h,fail=0;
    for(int f=0;f<6;f++)for(int i=0;i<3;i++){ snprintf(p,sizeof p,"%s/f%d_%d.pgm",d,f,i); if(load(p,&w,&h)){ printf("missing %s\n",p); return 2; } printf("f%d_%d minutiae %d\n",f,i,fp_extract(img,w,h,&T[f][i])); }
    int gmin=1000,imax=0;
    for(int f=0;f<6;f++)for(int g=0;g<6;g++)for(int i=1;i<3;i++){ int s=fp_match(&T[f][0],&T[g][i]); if(f==g){ if(s<gmin)gmin=s; if(!fp_match_ok(s)){ printf("FRR f%d vs f%d_%d score %d\n",f,g,i,s); fail++; } } else { if(s>imax)imax=s; if(fp_match_ok(s)){ printf("FAR f%d vs f%d_%d score %d\n",f,g,i,s); fail++; } } }
    printf("genuine min %d impostor max %d threshold %d -> %s\n",gmin,imax,FP_THRESH,fail?"FAIL":"PASS");
    return fail?1:0;
}
