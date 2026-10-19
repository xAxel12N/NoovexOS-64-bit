/* baseline + progressive JPEG decoder for NoovexOS kernel (ported from host-verified jdec.c,
   pixel-matched vs PIL). Outputs RGBA. Integer IDCT (float consts are compile-time). */
#ifndef NVX_JPEG_H
#define NVX_JPEG_H

/* scratch for component planes (bump-allocated, decode is one-shot) */
#ifndef JP_BASE
static u8* g_jp_mem;
#define JP_BASE (g_jp_mem?g_jp_mem:(g_jp_mem=(u8*)kmalloc(JP_BUDGET)))
#endif
#define JP_BUDGET (16u*1024u*1024u)
#define JP_MAXPIX (1920*1200)        /* output cap (keeps RGBA buffer < ~2MB) */

static const int JZZ[64]={
 0,1,8,16,9,2,3,10,17,24,32,25,18,11,4,5,12,19,26,33,40,48,41,34,
 27,20,13,6,7,14,21,28,35,42,49,56,57,50,43,36,29,22,15,23,30,37,44,51,
 58,59,52,45,38,31,39,46,53,60,61,54,47,55,62,63};

static const u8* JP; static const u8* JPE; static u32 JBB; static int JBC, JATM;
static u32 jp_off;
static u8* jp_alloc(u32 n){ u8* base=JP_BASE; if(jp_off+n>JP_BUDGET||!base)return 0; u8* r=base+jp_off; jp_off+=n; return r; }

static void jbr_reset(void){ JBB=0; JBC=0; JATM=0; }
static int jbr_byte(void){
    if(JP>=JPE){ JATM=1; return 0xFF; }
    int c=*JP;
    if(c!=0xFF){ JP++; return c; }
    const u8* q=JP+1; while(q<JPE && *q==0xFF) q++;
    if(q>=JPE){ JATM=1; return 0xFF; }
    int n=*q;
    if(n==0x00){ JP=q+1; return 0xFF; }
    JATM=1; return 0;
}
static int jgetbit(void){ if(JBC==0){ int c=jbr_byte(); JBB=c; JBC=8; } JBC--; return (JBB>>JBC)&1; }
static int jgetbits(int n){ int v=0; while(n-->0)v=(v<<1)|jgetbit(); return v; }
static int jrecv_ext(int s){ if(s==0)return 0; int v=jgetbits(s); if(v<(1<<(s-1)))v+=1-(1<<s); return v; }

typedef struct { int mincode[18],maxcode[18],valptr[18]; u8 val[256]; int nval; } JHuff;
static void jhuff_build(JHuff* h,const u8* bits,const u8* vals){
    int code=0,k=0;
    for(int l=1;l<=16;l++){
        h->valptr[l]=k; h->mincode[l]=code;
        if(bits[l-1]>0){ code+=bits[l-1]; h->maxcode[l]=code-1; k+=bits[l-1]; }
        else h->maxcode[l]=-1;
        code<<=1;
    }
    h->nval=k; for(int i=0;i<k;i++)h->val[i]=vals[i];
}
static int jhuff_dec(JHuff* h){
    int code=jgetbit(),l=1;
    while(l<=16 && code>h->maxcode[l]){ code=(code<<1)|jgetbit(); l++; }
    if(l>16)return 0;
    int idx=h->valptr[l]+code-h->mincode[l];
    if(idx<0||idx>=h->nval)return 0;
    return h->val[idx];
}

#define JF2F(x) ((int)((x)*4096+0.5))
#define JFSH(x) ((x)*4096)
static u8 jclamp(int x){ return x<0?0:x>255?255:(u8)x; }
#define JIDCT1D(s0,s1,s2,s3,s4,s5,s6,s7) \
   int t0,t1,t2,t3,p1,p2,p3,p4,p5,x0,x1,x2,x3; \
   p2=s2; p3=s6; p1=(p2+p3)*JF2F(0.5411961f); \
   t2=p1+p3*JF2F(-1.847759065f); t3=p1+p2*JF2F(0.765366865f); \
   p2=s0; p3=s4; t0=JFSH(p2+p3); t1=JFSH(p2-p3); \
   x0=t0+t3; x3=t0-t3; x1=t1+t2; x2=t1-t2; \
   t0=s7; t1=s5; t2=s3; t3=s1; \
   p3=t0+t2; p4=t1+t3; p1=t0+t3; p2=t1+t2; \
   p5=(p3+p4)*JF2F(1.175875602f); \
   t0=t0*JF2F(0.298631336f); t1=t1*JF2F(2.053119869f); \
   t2=t2*JF2F(3.072711026f); t3=t3*JF2F(1.501321110f); \
   p1=p5+p1*JF2F(-0.899976223f); p2=p5+p2*JF2F(-2.562915447f); \
   p3=p3*JF2F(-1.961570560f); p4=p4*JF2F(-0.390180644f); \
   t3+=p1+p4; t2+=p2+p3; t1+=p2+p4; t0+=p1+p3;
static void jidct(u8* out,int stride,int* d){
    int val[64],*v=val,i;
    for(i=0;i<8;i++,d++,v++){
        if(d[8]==0&&d[16]==0&&d[24]==0&&d[32]==0&&d[40]==0&&d[48]==0&&d[56]==0){
            int dc=d[0]*4; v[0]=v[8]=v[16]=v[24]=v[32]=v[40]=v[48]=v[56]=dc;
        } else {
            JIDCT1D(d[0],d[8],d[16],d[24],d[32],d[40],d[48],d[56])
            x0+=512;x1+=512;x2+=512;x3+=512;
            v[0]=(x0+t3)>>10; v[56]=(x0-t3)>>10; v[8]=(x1+t2)>>10; v[48]=(x1-t2)>>10;
            v[16]=(x2+t1)>>10; v[40]=(x2-t1)>>10; v[24]=(x3+t0)>>10; v[32]=(x3-t0)>>10;
        }
    }
    v=val;
    for(i=0;i<8;i++,v+=8,out+=stride){
        JIDCT1D(v[0],v[1],v[2],v[3],v[4],v[5],v[6],v[7])
        x0+=65536+(128<<17); x1+=65536+(128<<17); x2+=65536+(128<<17); x3+=65536+(128<<17);
        out[0]=jclamp((x0+t3)>>17); out[7]=jclamp((x0-t3)>>17);
        out[1]=jclamp((x1+t2)>>17); out[6]=jclamp((x1-t2)>>17);
        out[2]=jclamp((x2+t1)>>17); out[5]=jclamp((x2-t1)>>17);
        out[3]=jclamp((x3+t0)>>17); out[4]=jclamp((x3-t0)>>17);
    }
}

static int JQt[4][64];
static JHuff JHDC[4],JHAC[4];
typedef struct { int id,h,v,q,dct,act,pw,ph; u8* plane; } JComp;
static JComp JC[4];
static int jrd16(const u8* p){ return (p[0]<<8)|p[1]; }

/* decode JPEG -> RGBA at rgba (4 bytes/px). returns 0 ok, negative on error/unsupported. */

/* ===================== progressive JPEG (SOF2) ===================== */
static int JPROG=0, jp_eobrun=0, JNC, JHmaxg, JVmaxg, Jmcux, Jmcuy, JRI;
static int JCOEF_bw[4],JCOEF_bh[4],JCOEF_nbw[4],JCOEF_nbh[4]; static int* JCOEF[4];
#ifndef JCOEF_BASE
static int* g_jcoef_mem;
#define JCOEF_BASE (g_jcoef_mem?g_jcoef_mem:(g_jcoef_mem=(int*)kmalloc(6u*1024*1024*sizeof(int))))
#endif
static u32 jcoef_off;
static int* jcoef_alloc(u32 n){ if((jcoef_off+n)>6u*1024*1024||!JCOEF_BASE)return 0; int* r=JCOEF_BASE+jcoef_off; jcoef_off+=n; return r; }
static void jp_restart(void){ JBC=0; JBB=0; while(JP+1<JPE && !(JP[0]==0xFF&&JP[1]>=0xD0&&JP[1]<=0xD7))JP++; if(JP+1<JPE)JP+=2; JATM=0; jp_eobrun=0; }
static void jp_prog_scan(int ns,int* sc,int Ss,int Se,int Ah,int Al){
    int p1=1<<Al, m1=-(1<<Al);
    if(Ss==0){                                   /* DC scan: interleaved, MCU order */
        int pred[4]={0,0,0,0}, cnt=0;
        for(int my=0;my<Jmcuy;my++)for(int mx=0;mx<Jmcux;mx++){
            if(JRI && cnt>0 && (cnt%JRI)==0){ jp_restart(); pred[0]=pred[1]=pred[2]=pred[3]=0; }
            for(int si=0;si<ns;si++){ int c=sc[si];
                for(int by=0;by<JC[c].v;by++)for(int bx=0;bx<JC[c].h;bx++){
                    int brow=my*JC[c].v+by, bcol=mx*JC[c].h+bx;
                    int* cf=JCOEF[c]+((long)brow*JCOEF_bw[c]+bcol)*64;
                    if(Ah==0){ int t=jhuff_dec(&JHDC[JC[c].dct]); int diff=jrecv_ext(t); pred[c]+=diff; cf[0]=pred[c]<<Al; }
                    else { if(jgetbit()) cf[0]|=p1; }
                } }
            cnt++;
        }
    } else {                                     /* AC scan: non-interleaved, single component */
        int c=sc[0]; JHuff* ac=&JHAC[JC[c].act]; int cnt=0;
        for(int by=0;by<JCOEF_nbh[c];by++)for(int bx=0;bx<JCOEF_nbw[c];bx++){
            if(JRI && cnt>0 && (cnt%JRI)==0) jp_restart();
            int* cf=JCOEF[c]+((long)by*JCOEF_bw[c]+bx)*64;
            if(Ah==0){                           /* AC first */
                if(jp_eobrun>0){ jp_eobrun--; cnt++; continue; }
                int k=Ss;
                while(k<=Se){ int rs=jhuff_dec(ac); int r=rs>>4,ss=rs&15;
                    if(ss==0){ if(r<15){ jp_eobrun=(1<<r)-1; if(r)jp_eobrun+=jgetbits(r); break; } k+=16; }
                    else { k+=r; if(k>Se)break; cf[JZZ[k]]=jrecv_ext(ss)<<Al; k++; } }
            } else {                             /* AC refine */
                int k=Ss;
                if(jp_eobrun==0){
                    for(k=Ss;k<=Se;k++){ int rs=jhuff_dec(ac); int r=rs>>4,ss=rs&15;
                        if(ss){ if(jgetbit())ss=p1; else ss=m1; }
                        else { if(r!=15){ jp_eobrun=1<<r; if(r)jp_eobrun+=jgetbits(r); break; } }
                        do{ int* co=&cf[JZZ[k]];
                            if(*co!=0){ if(jgetbit()){ if((*co&p1)==0){ *co += (*co>=0)?p1:m1; } } }
                            else { if(--r<0)break; }
                            k++;
                        }while(k<=Se);
                        if(ss){ if(k<=Se)cf[JZZ[k]]=ss; } }
                }
                if(jp_eobrun>0){ for(;k<=Se;k++){ int* co=&cf[JZZ[k]]; if(*co!=0){ if(jgetbit()){ if((*co&p1)==0){ *co += (*co>=0)?p1:m1; } } } } jp_eobrun--; }
            }
            cnt++;
        }
    }
}
static void jp_prog_finish(void){
    for(int c=0;c<JNC;c++){ int* Q=JQt[JC[c].q];
        for(int by=0;by<JCOEF_bh[c];by++)for(int bx=0;bx<JCOEF_bw[c];bx++){
            int* cf=JCOEF[c]+((long)by*JCOEF_bw[c]+bx)*64;
            int blk[64]; for(int k=0;k<64;k++) blk[JZZ[k]]=cf[JZZ[k]]*Q[k];
            jidct(JC[c].plane+(by*8)*JC[c].pw+(bx*8), JC[c].pw, blk);
        } }
}
int jpeg_decode(const u8* buf,int len,u8* rgba,int rgbamax,int* ow,int* oh){
    const u8* p=buf; const u8* end=buf+len;
    if(len<4||jrd16(p)!=0xFFD8)return -1; p+=2;
    int Hmax=1,Vmax=1,NC=0,W=0,H=0,RI=0;
    jp_off=0;
    while(p+4<=end){
        if(p[0]!=0xFF){ p++; continue; }
        int m=p[1]; p+=2;
        if(m==0xD9)break;
        if(m==0x01||(m>=0xD0&&m<=0xD7))continue;
        int L=jrd16(p); const u8* seg=p+2; const u8* nx=p+L; p+=L; if(nx>end)return -1;
        if(m==0xDB){ const u8* q=seg; while(q<nx){ int pq=q[0]>>4,tq=q[0]&15; q++; if(tq>3)return -2;
                for(int i=0;i<64;i++){ if(pq){ JQt[tq][i]=jrd16(q); q+=2; } else JQt[tq][i]=*q++; } } }
        else if(m==0xC0||m==0xC1||m==0xC2){ JPROG=(m==0xC2); H=jrd16(seg+1); W=jrd16(seg+3); NC=seg[5]; if(NC<1||NC>4)return -3;
            const u8* q=seg+6; for(int i=0;i<NC;i++){ JC[i].id=q[0]; JC[i].h=q[1]>>4; JC[i].v=q[1]&15; JC[i].q=q[2]; q+=3;
                if(JC[i].h<1)JC[i].h=1; if(JC[i].v<1)JC[i].v=1; if(JC[i].h>Hmax)Hmax=JC[i].h; if(JC[i].v>Vmax)Vmax=JC[i].v; JC[i].plane=0; }
            if(W<1||H<1||W>4096||H>4096||W*H>JP_MAXPIX)return -11;
            if(JPROG){ JNC=NC; JHmaxg=Hmax; JVmaxg=Vmax; int mcuW=8*Hmax,mcuH=8*Vmax; Jmcux=(W+mcuW-1)/mcuW; Jmcuy=(H+mcuH-1)/mcuH;
                jcoef_off=0; jp_off=0;
                for(int i=0;i<NC;i++){ JCOEF_bw[i]=Jmcux*JC[i].h; JCOEF_bh[i]=Jmcuy*JC[i].v;
                    JCOEF_nbw[i]=(W*JC[i].h+8*Hmax-1)/(8*Hmax); JCOEF_nbh[i]=(H*JC[i].v+8*Vmax-1)/(8*Vmax);
                    JC[i].pw=JCOEF_bw[i]*8; JC[i].ph=JCOEF_bh[i]*8;
                    JCOEF[i]=jcoef_alloc((u32)JCOEF_bw[i]*JCOEF_bh[i]*64); if(!JCOEF[i])return -13;
                    for(long z=0;z<(long)JCOEF_bw[i]*JCOEF_bh[i]*64;z++)JCOEF[i][z]=0;
                    JC[i].plane=jp_alloc((u32)JC[i].pw*JC[i].ph); if(!JC[i].plane)return -13; } } }
        else if(m==0xC4){ const u8* q=seg; while(q<nx){ int tc=q[0]>>4,th=q[0]&15; q++; if(th>3)return -4;
                int tot=0; const u8* bits=q; for(int i=0;i<16;i++)tot+=q[i]; q+=16;
                if(tc) jhuff_build(&JHAC[th],bits,q); else jhuff_build(&JHDC[th],bits,q); q+=tot; } }
        else if(m==0xDD){ RI=jrd16(seg); }
        else if(m==0xDA){
            int ns=seg[0]; const u8* q=seg+1; int sc[4]={0,0,0,0};
            for(int i=0;i<ns;i++){ int cid=q[0],tt=q[1]; q+=2; for(int j=0;j<NC;j++) if(JC[j].id==cid){ JC[j].dct=tt>>4; JC[j].act=tt&15; sc[i]=j; } }
            if(JPROG){ int Ss=q[0],Se=q[1],AhAl=q[2],Ah=AhAl>>4,Al=AhAl&15;
                JRI=RI; JP=nx; JPE=end; jbr_reset(); jp_eobrun=0; jp_prog_scan(ns,sc,Ss,Se,Ah,Al); p=JP; continue; }
            if((int)(W*H*4)>rgbamax)return -12;
            int mcuW=8*Hmax, mcuH=8*Vmax;
            int mcux=(W+mcuW-1)/mcuW, mcuy=(H+mcuH-1)/mcuH;
            for(int i=0;i<NC;i++){ JC[i].pw=mcux*JC[i].h*8; JC[i].ph=mcuy*JC[i].v*8;
                JC[i].plane=jp_alloc((u32)JC[i].pw*JC[i].ph); if(!JC[i].plane)return -13;
                for(u32 z=0;z<(u32)JC[i].pw*JC[i].ph;z++)JC[i].plane[z]=128; }
            JP=nx; JPE=end; jbr_reset();
            int dcp[4]; for(int i=0;i<4;i++)dcp[i]=0;
            int cnt=0;
            for(int my=0;my<mcuy;my++)for(int mx=0;mx<mcux;mx++){
                if(RI && cnt>0 && (cnt%RI)==0){ JBC=0; JBB=0;
                    while(JP+1<JPE && !(JP[0]==0xFF && JP[1]>=0xD0 && JP[1]<=0xD7))JP++;
                    if(JP+1<JPE)JP+=2; JATM=0; for(int i=0;i<4;i++)dcp[i]=0; }
                for(int ci=0;ci<NC;ci++){
                    JHuff* dch=&JHDC[JC[ci].dct]; JHuff* ach=&JHAC[JC[ci].act]; int* Q=JQt[JC[ci].q];
                    for(int by=0;by<JC[ci].v;by++)for(int bx=0;bx<JC[ci].h;bx++){
                        int blk[64]; for(int z=0;z<64;z++)blk[z]=0;
                        int t=jhuff_dec(dch); int diff=jrecv_ext(t); dcp[ci]+=diff; blk[0]=dcp[ci]*Q[0];
                        int k=1; while(k<64){ int rs=jhuff_dec(ach); int r=rs>>4,s=rs&15;
                            if(s==0){ if(r==15){ k+=16; continue; } else break; }
                            k+=r; if(k>63)break; int co=jrecv_ext(s); blk[JZZ[k]]=co*Q[k]; k++; }
                        int px=(mx*JC[ci].h+bx)*8, py=(my*JC[ci].v+by)*8;
                        jidct(JC[ci].plane+py*JC[ci].pw+px, JC[ci].pw, blk);
                    }
                }
                cnt++;
            }
            break;
        }
    }
    if(NC==0||!JC[0].plane)return -5;
    if(JPROG) jp_prog_finish();
    for(int y=0;y<H;y++)for(int x=0;x<W;x++){
        int Y=JC[0].plane[(y*JC[0].v/Vmax)*JC[0].pw + (x*JC[0].h/Hmax)];
        u8* o=rgba+(y*W+x)*4; o[3]=255;
        if(NC==1){ o[0]=o[1]=o[2]=(u8)Y; }
        else {
            int cb=JC[1].plane[(y*JC[1].v/Vmax)*JC[1].pw + (x*JC[1].h/Hmax)];
            int cr=JC[2].plane[(y*JC[2].v/Vmax)*JC[2].pw + (x*JC[2].h/Hmax)];
            o[0]=jclamp(Y+((91881*(cr-128))>>16));
            o[1]=jclamp(Y-((22554*(cb-128)+46802*(cr-128))>>16));
            o[2]=jclamp(Y+((116130*(cb-128))>>16));
        }
    }
    *ow=W; *oh=H; return 0;
}
#endif
