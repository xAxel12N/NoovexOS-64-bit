#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "tools/faceid/fid_policy.h"

static int fails;
#define CK(c,m) do{ if(c) printf("  %s ok\n",m); else { printf("  FAIL %s\n",m); fails++; } }while(0)

static uint64_t rs=0x9E3779B97F4A7C15ull;
static double urand(void){ rs^=rs<<13; rs^=rs>>7; rs^=rs<<17; return ((rs>>11)+0.5)/9007199254740992.0; }
static double grand(void){ return sqrt(-2*log(urand()))*cos(6.283185307179586*urand()); }

static int hexv(const char* h,uint8_t* o){ int n=0; while(h[0]&&h[1]){ unsigned v; sscanf(h,"%2x",&v); o[n++]=(uint8_t)v; h+=2; } return n; }

static void mkpts(float* p,float cx,float cy,float s,float rot,float yaw,float pitch){
    float base[10]={-1.0f,0,-0.4f,0, 0.4f,0,1.0f,0, 0,0.8f};
    base[8]=yaw*1.4f; base[9]=0.8f+pitch;
    for(int k=0;k<5;k++){ float x=base[2*k]*s, y=base[2*k+1]*s; p[2*k]=cx+x*cosf(rot)-y*sinf(rot); p[2*k+1]=cy+x*sinf(rot)+y*cosf(rot); } }

#define NID 300
#define NT 6
static float C[NID][FP_DIM], SG[NID];
static void sample(int id,float* e,float noise){ float g=noise*SG[id]; for(int k=0;k<FP_DIM;k++) e[k]=C[id][k]+(float)(grand()*g); }

int main(int argc,char** argv){
    if(argc==6&&!strcmp(argv[1],"seal")){
        uint8_t k[32],n[12],a[256],b[1024],t[16]; hexv(argv[2],k); hexv(argv[3],n); int na=hexv(argv[4],a), nb=hexv(argv[5],b);
        fp_seal(k,n,a,(size_t)na,b,(size_t)nb,t); for(int i=0;i<nb;i++) printf("%02x",b[i]); for(int i=0;i<16;i++) printf("%02x",t[i]); printf("\n"); return 0; }
    printf("faceid policy tests\n");
    { uint8_t k[32]; hexv("85d6be7857556d337f4452fe42d506a80103808afb0db2fd4abff6af4149f51b",k); fp_poly p; fp_poly_init(&p,k);
      const char* m="Cryptographic Forum Research Group"; fp_poly_feed(&p,(const uint8_t*)m,strlen(m)); uint8_t t[16],w[16]; fp_poly_final(&p,t); hexv("a8061dc1305136c6c22b8baf0c0127a9",w);
      CK(!memcmp(t,w,16),"Poly1305 matches RFC 8439 2.5.2"); }
    { uint8_t k[32],n[12],o[64],w[16]; for(int i=0;i<32;i++) k[i]=(uint8_t)i; hexv("000000090000004a00000000",n); fp_chacha_block(k,1,n,o); hexv("10f1e7e4d13b5915500fdd1fa32071c4",w);
      CK(!memcmp(o,w,16),"ChaCha20 block matches RFC 8439 2.3.2"); }
    { uint8_t k[32],n[12],b[600],c[600],t[16]; for(int i=0;i<32;i++) k[i]=(uint8_t)(i*7+3); for(int i=0;i<12;i++) n[i]=(uint8_t)i; for(int i=0;i<600;i++) b[i]=c[i]=(uint8_t)(i*13);
      const uint8_t aad[8]={'N','V','X','F','A','C','E','2'}; fp_seal(k,n,aad,8,c,600,t);
      CK(memcmp(b,c,600)!=0,"sealed templates are not stored in the clear");
      uint8_t d[600]; memcpy(d,c,600); CK(fp_open(k,n,aad,8,d,600,t)==0&&!memcmp(d,b,600),"sealed templates open again with the same machine key");
      memcpy(d,c,600); d[100]^=1; CK(fp_open(k,n,aad,8,d,600,t)!=0,"a changed byte in the template file is refused");
      memcpy(d,c,600); k[0]^=1; CK(fp_open(k,n,aad,8,d,600,t)!=0,"another machine's key cannot open the templates"); }
    for(int i=0;i<NID;i++){ SG[i]=(float)(0.6+0.6*urand()); for(int k=0;k<FP_DIM;k++) C[i][k]=(float)(grand()*0.053); }
    for(int i=0;i<3;i++) SG[i]=1.0f;
    for(int i=0;i<NID;i+=10){ float s=0; for(int k=0;k<FP_DIM;k++){ float d=(float)grand(); C[i+1][k]=C[i][k]+d; s+=d*d; } s=sqrtf(s); float ld=(float)(0.40+0.30*urand()); for(int k=0;k<FP_DIM;k++) C[i+1][k]=C[i][k]+(C[i+1][k]-C[i][k])/s*ld; }
    { static fp_enrol s; float e[FP_DIM]; memset(&s,0,sizeof s);
      sample(0,e,0.02f); CK(fp_enrol_add(&s,e,0,120)==1,"enrol: first picture accepted");
      CK(fp_enrol_add(&s,e,0,120)==0,"enrol: an identical picture is not a new template");
      for(int i=0;i<4;i++){ sample(0,e,0.02f); fp_enrol_add(&s,e,0,120); }
      CK(s.n==2,"enrol: at most two templates per angle and light");
      sample(3,e,0.02f); CK(fp_enrol_add(&s,e,-0.2f,120)==-1,"enrol: another person's face is refused");
      sample(0,e,0.02f); fp_enrol_add(&s,e,0,40); sample(0,e,0.02f); fp_enrol_add(&s,e,0,200);
      CK(s.n==4&&!fp_enrol_ready(&s)&&fp_enrol_shown(&s)==4,"enrol: four straight-on pictures are not enough");
      sample(0,e,0.02f); fp_enrol_add(&s,e,0,40);
      CK(s.n==5&&!fp_enrol_ready(&s)&&fp_enrol_shown(&s)==4,"enrol: five pictures from one angle still ask for a head turn");
      sample(0,e,0.02f); fp_enrol_add(&s,e,-0.15f,120); sample(0,e,0.02f); fp_enrol_add(&s,e,0.15f,120);
      CK(fp_enrol_ready(&s)&&fp_enrol_poses(&s)==3&&s.n>=FP_ENROL_MIN,"enrol: ready with 5+ templates from several angles and lighting"); }
    { static float T[NT][FP_DIM]; float pts[10], e[FP_DIM]; fp_live l;
      for(int i=0;i<NT;i++) sample(0,T[i],0.02f);
      fp_live_reset(&l); int r=0; mkpts(pts,320,240,40,0,0,0); for(int f=0;f<12&&r!=FPV_SPOOF;f++){ sample(0,e,0.0f); r=fp_verify_step(&l,1,pts,f?0.0f:-1.0f,0.5f,120); }
      CK(r==FPV_SPOOF,"liveness: a still picture fed to the camera is refused");
      fp_live_reset(&l); int any=0; for(int f=0;f<40;f++){ mkpts(pts,320+f*3,240+f,40+f,0.02f*f,0.03f,0); r=fp_verify_step(&l,1,pts,0.03f,0.5f,120); if(r==FPV_MATCH) any=1; }
      CK(!any&&r==FPV_NEEDLIVE,"liveness: a photo moved, turned and brought closer is not a live face");
      fp_live_reset(&l); any=0; for(int f=0;f<40;f++){ mkpts(pts,320+f*2,240,40,0.01f*f,0.03f,0.01f*(f%3)); r=fp_verify_step(&l,1,pts,0.03f,0.5f,120); if(r==FPV_MATCH) any=1; }
      CK(!any,"liveness: small in-plane shaking of a photo is not a live face");
      fp_live_reset(&l); for(int f=0;f<10;f++){ mkpts(pts,320,240,40,0,-0.08f+0.02f*f,0); r=fp_verify_step(&l,1,pts,0.03f,0.5f,120); }
      CK(r==FPV_MATCH,"liveness: a small head turn passes");
      fp_live_reset(&l); for(int f=0;f<10;f++){ mkpts(pts,320+(f&1),240,40,0,0.02f,0); r=fp_verify_step(&l,1,pts,0.03f,f==4?0.38f:0.5f,120); }
      CK(r==FPV_MATCH,"liveness: a blink passes");
      fp_live_reset(&l); for(int f=0;f<10;f++){ mkpts(pts,320+(f&1),240,40,0,0.02f,0); r=fp_verify_step(&l,1,pts,0.03f,f==4?0.38f:0.5f,120+(f==4?30:0)); }
      CK(r==FPV_NEEDLIVE,"liveness: an eye change with a whole-face light change is not a blink");
      fp_live_reset(&l); for(int f=0;f<10;f++){ mkpts(pts,320,240,40,0,-0.08f+0.02f*f,0); r=fp_verify_step(&l,0,pts,0.03f,0.5f,120); }
      CK(r==FPV_NOMATCH,"liveness: movement without a matching face never unlocks");
      fp_live_reset(&l); for(int f=0;f<2;f++){ mkpts(pts,320,240,40,0,-0.1f+0.1f*f,0); r=fp_verify_step(&l,1,pts,0.03f,0.5f,120); }
      CK(r==FPV_NEEDLIVE,"liveness: fewer than three matching frames never unlock"); }
    {
      static float T[NID][NT][FP_DIM]; float e[FP_DIM];
      for(int i=0;i<NID;i++) for(int t=0;t<NT;t++) sample(i,T[i][t],0.02f);
      static const float TH[]={0.40f,0.45f,0.50f,0.55f,0.60f};
      long ga=0, gn=0, ia=0, in=0, ia1[5]={0}, ga1[5]={0}, ia3=0, ga3=0;
      for(int i=0;i<NID;i++) for(int a=0;a<4;a++){
          int ok3g=0, ok3i=0; int j=i^1;
          for(int f=0;f<12;f++){
              sample(i,e,0.02f+(float)(0.01*urand())); float b; int m=fp_match_frame((const float(*)[FP_DIM])T[i],NT,e,&b); gn++; ga+=m; ok3g+=m;
              for(int q=0;q<5;q++){ float bb=1e9f; for(int t=0;t<NT;t++){ float d=fp_dist(T[i][t],e); if(d<bb) bb=d; } if(f==0&&bb<TH[q]) ga1[q]++; }
              int m2=fp_match_frame((const float(*)[FP_DIM])T[j],NT,e,&b); in++; ia+=m2; ok3i+=m2;
              if(f==0) for(int q=0;q<5;q++){ float bb=1e9f; for(int t=0;t<NT;t++){ float d=fp_dist(T[j][t],e); if(d<bb) bb=d; } if(bb<TH[q]) ia1[q]++; }
              for(int o=0;o<NID;o+=7) if(o!=i&&o!=j){ int m3=fp_match_frame((const float(*)[FP_DIM])T[o],NT,e,&b); in++; ia+=m3; }
          }
          ga3+=ok3g>=FP_NEED_MATCH; ia3+=ok3i>=FP_NEED_MATCH; }
      long att=NID*4;
      printf("  synthetic set: %d identities (%d lookalike pairs at distance 0.40-0.70, per-person noise 0.6-1.2x), %d templates each, %ld genuine and %ld impostor frames\n",NID,NID/10,NT,gn,in);
      for(int q=0;q<5;q++) printf("  single best-template rule at %.2f: lookalike accept %.2f%%  genuine accept %.2f%%\n",TH[q],100.0*ia1[q]/att,100.0*ga1[q]/att);
      printf("  policy (%.2f with 2 templates under %.2f): frame FAR %.4f%%  frame FRR %.2f%%  lookalike attempt accept (%d of 12 frames) %.3f%%  genuine attempt accept (%d of 12) %.2f%%\n",FP_T_MATCH,FP_T_SUPPORT,100.0*ia/in,100.0*(gn-ga)/gn,FP_NEED_MATCH,100.0*ia3/att,FP_NEED_MATCH,100.0*ga3/att);
      CK((double)ia/in<0.001,"calibration: frame false-accept rate under 0.1% on the synthetic set");
      CK((double)ia3/att<(double)ia1[4]/att||ia1[4]==0,"calibration: the policy accepts fewer lookalikes than the old 0.5/0.6 single-template rule");
      CK((double)ga3/att>0.95,"calibration: genuine users still get in at least 95% of attempts on the synthetic set");
    }
    if(fails){ printf("faceid policy: %d FAILED\n",fails); return 1; }
    printf("faceid policy: all passed\n"); return 0; }
