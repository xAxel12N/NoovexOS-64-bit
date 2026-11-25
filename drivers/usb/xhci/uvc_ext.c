typedef struct { u8 fi, ri, fb; u16 w, h; u32 defint, bestint; } uvx_h_t;
typedef struct { u8 fi, idx; u16 w, h; } uvx_s_t;
static struct { int nh, ns, method, trig, sep, capture, ms2109, sti, want, force, sw, sh; u32 slen, scap; u8* sbuf; uvx_h_t h[16]; uvx_s_t s[24]; u32 mi[64]; } uvx={0,0,0,0,0,0,0,0,0,-1,0,0,0,0,0,{{0}},{{0}},{0}};
u32 uvx_stills=0, uvx_h264_frames=0;
static int uvx_cc(const u8* g,const char* s){ return g[0]==(u8)s[0]&&g[1]==(u8)s[1]&&g[2]==(u8)s[2]&&g[3]==(u8)s[3]; }
static u32 uvx_pick(const u8* f,int bl,int doff,int noff,int loff){
    u8 t[255]; for(int k=0;k<255;k++)t[k]=0; if(bl<noff+1)return 0; for(int k=0;k<4;k++)t[21+k]=f[doff+k]; t[25]=f[noff]; int ln=bl-loff; if(ln<0)ln=0; if(ln>255-26)ln=255-26; for(int k=0;k<ln;k++)t[26+k]=f[loff+k];
    return uvc_pickint(t,26+ln); }
static u32 uvx_minint(const u8* f,int bl,int doff,int noff,int loff){ if(bl<noff+1)return 0; int nt=f[noff]; u32 m=0; if(nt==0)return bl>=loff+12?uvc_r32(f+loff):0;
    for(int k=0;k<nt&&loff+k*4+4<=bl;k++){ u32 v=uvc_r32(f+loff+k*4); if(v&&(!m||v<m))m=v; } (void)doff; return m; }
static void uvx_mi_set(int fi,int ri,u32 m){ for(int k=0;k<cam.nfr;k++) if(cam.fr[k].fi==fi&&cam.fr[k].ri==ri)uvx.mi[k]=m; }
static void uvx_addh(int fi,int ri,int fb,int w,int h,u32 def,u32 best){ if(uvx.nh>=16||w<16||h<16||w>1920||h>1200)return; uvx_h_t* x=&uvx.h[uvx.nh++]; x->fi=(u8)fi; x->ri=(u8)ri; x->fb=(u8)fb; x->w=(u16)w; x->h=(u16)h; x->defint=def; x->bestint=best?best:def; }
static void uvx_parse(const u8* c,int n,int vs){
    int cif=-1,calt=0,ccls=0,csub=0,cur=0,fi=0; uvx.nh=0; uvx.ns=0; uvx.method=0; uvx.trig=0; uvx.sep=0; for(int k=0;k<64;k++)uvx.mi[k]=0;
    if(vs<0)return;
    for(int i=0;i+2<=n;){ int bl=c[i]; if(bl<2||i+bl>n)break; int t=c[i+1];
        if(t==4&&bl>=9){ cif=c[i+2]; calt=c[i+3]; ccls=c[i+5]; csub=c[i+6]; }
        else if(t==0x24&&ccls==14&&csub==2&&cif==vs&&calt==0&&bl>=3){ int st=c[i+2];
            if(st==1&&bl>=11){ uvx.method=c[i+9]; uvx.trig=c[i+10]; }
            else if(st==0x10&&bl>=21){ fi=c[i+3]; const u8* g=c+i+5; cur=uvx_cc(g,"H264")||uvx_cc(g,"h264")||uvx_cc(g,"AVC1")?1:uvx_cc(g,"MJPG")?2:0; }
            else if(st==0x11&&bl>=26&&cur){ int w=c[i+5]|(c[i+6]<<8), h=c[i+7]|(c[i+8]<<8); u32 def=uvc_r32(c+i+17), best=uvx_pick(c+i,bl,17,21,26);
                if(cur==1)uvx_addh(fi,c[i+3],1,w,h,def,best);
                else if(cam.nfr<64){ uvx.mi[cam.nfr]=uvx_minint(c+i,bl,17,21,26); uvc_fr_t* f=&cam.fr[cam.nfr++]; f->kind=1; f->fi=(u8)fi; f->ri=c[i+3]; f->w=(u16)w; f->h=(u16)h; f->defint=def; f->bestint=best?best:def; } }
            else if(st==0x13&&bl>=4){ fi=c[i+3]; cur=3; }
            else if(st==0x14&&bl>=44&&cur==3){ int w=c[i+4]|(c[i+5]<<8), h=c[i+6]|(c[i+7]<<8); u32 def=uvc_r32(c+i+39); uvx_addh(fi,c[i+3],0,w,h,def,uvx_pick(c+i,bl,39,43,44)); }
            else if((st==5||st==7)&&bl>=26&&!cur)uvx_mi_set(fi,c[i+3],uvx_minint(c+i,bl,21,25,26));
            else if(st==4||st==6){ fi=c[i+3]; cur=0; }
            else if(st==0x16){ cur=0; }
            else if(st==3&&bl>=5){ int np=c[i+4]; uvx.sep=c[i+3]; for(int k=0;k<np&&5+k*4+4<=bl&&uvx.ns<24;k++){ uvx_s_t* s=&uvx.s[uvx.ns++]; s->fi=(u8)fi; s->idx=(u8)(k+1); s->w=(u16)(c[i+5+k*4]|(c[i+6+k*4]<<8)); s->h=(u16)(c[i+7+k*4]|(c[i+8+k*4]<<8)); } } }
        i+=bl; } }
static const u16 uvx_ctab[][2]={{0x534d,0x2109},{0x534d,0x2130},{0x345f,0x2109},{0x345f,0x2130},{0x534d,0x0021},{0x1164,0x757a},{0x2935,0x0001},{0x07ca,0x0553}};
static int uvx_is_capture_id(u16 vid,u16 pid){ for(unsigned k=0;k<sizeof uvx_ctab/sizeof uvx_ctab[0];k++) if(uvx_ctab[k][0]==vid&&uvx_ctab[k][1]==pid)return 1; return 0; }
static void uvx_classify(u16 vid,u16 pid){ int big=0; for(int k=0;k<cam.nfr;k++) if(cam.fr[k].w==1920&&cam.fr[k].h==1080)big=1;
    uvx.capture=uvx_is_capture_id(vid,pid)||(big&&!cam.ct&&!cam.pu); uvx.ms2109=vid==0x534d&&pid==0x2109; }
static u32 uvx_bpp2(int kind){ return kind==0||kind==3||kind==7?4u:kind==4?2u:3u; }
static u64 uvx_need(const uvc_fr_t* f){ u32 iv=f->bestint?f->bestint:f->defint; if(!iv)iv=333333u; return (u64)f->w*f->h*uvx_bpp2(f->kind)/2u*10000000ull/iv; }
static u64 uvx_have(void){ u32 mes=0; for(int k=0;k<cam.nalt;k++) if(cam.alt[k].esit>mes)mes=cam.alt[k].esit;
    if(!cam.nalt&&cam.bulk)return cam.speed>=4?350000000ull:35000000ull; return (u64)mes*(cam.speed>=3?8000u:1000u); }
static int uvx_score(const uvc_fr_t* f,u64 have){ if(f->w>1920||f->h>1200||f->w<16||f->h<16)return -1; if((f->kind==4||f->kind==7)&&uvc_has_color())return 0;
    u32 a=(u32)f->w*f->h, iv=f->bestint?f->bestint:f->defint; u32 fps=iv?(10000000u+iv/2)/iv:30u; if(fps<1)fps=1; if(fps>60)fps=60; int s=(int)((a>>8)*fps/60u); int fit=f->kind==1||uvx_need(f)<=have; if(!fit)return 1+s/64; if(f->kind!=1)s+=2; return 100000+s; }
static int uvx_reorder(int* order,int no){
    if(uvx.force>=0&&uvx.force<uvx.nh&&cam.nfr<64){ uvx_h_t* x=&uvx.h[uvx.force]; uvc_fr_t* f=&cam.fr[cam.nfr]; f->kind=8; f->fi=x->fi; f->ri=x->ri; f->w=x->w; f->h=x->h; f->defint=x->defint; f->bestint=x->bestint; order[0]=cam.nfr; return 1; }
    if(!uvx.capture)return no; u64 have=uvx_have();
    for(int a=0;a<no;a++){ int k=order[a]; uvc_fr_t* f=&cam.fr[k]; if(!uvx.mi[k]||uvx.mi[k]>=f->bestint||uvx.mi[k]<166000u)continue; u32 sv=f->bestint; f->bestint=uvx.mi[k]; if(f->kind!=1&&uvx_need(f)>have)f->bestint=sv; }
    for(int a=0;a<no;a++) for(int b=a+1;b<no;b++){ if(uvx_score(&cam.fr[order[b]],have)>uvx_score(&cam.fr[order[a]],have)){ int t=order[a]; order[a]=order[b]; order[b]=t; } }
    while(no>0&&uvx_score(&cam.fr[order[no-1]],have)<0)no--; return no; }
static int uvx_aud_fix(u16 vid,u16 pid,uac_alt_t* a,int n){ if(!(vid==0x534d&&pid==0x2109))return 0; int c=0;
    for(int k=0;k<n;k++){ if(!(a[k].ep&0x80)||a[k].v2)continue; if(a[k].ch==1&&a[k].sub==2&&a[k].bits==16){ a[k].ch=2; a[k].cont=0; a[k].nrate=1; a[k].rate[0]=48000; c++; } } return c; }
static void uvx_sti(int info){ uvx.sti=(info>>5)&1; }
static int uvx_take_still(void){
    if(uvc_kind==8&&!uvx.sti){ if(!uvc_fbad&&uvc_fill>0&&uvc_jpg&&!uvc_jnew){ for(u32 i=0;i<uvc_fill;i++)uvc_jpg[i]=uvc_fb[i]; uvc_jlen=uvc_fill; uvc_jnew=1; uvc_frames++; uvc_seq++; uvx_h264_frames++; } else if(uvc_fbad&&uvc_fill>0)uvc_badframes++; uvc_fill=0; uvc_fbad=0; return 1; }
    if(!uvx.sti)return 0;
    if(!uvc_fbad&&uvc_fill>0&&uvx.want&&uvx.sbuf&&uvc_fill<=uvx.scap){ for(u32 i=0;i<uvc_fill;i++)uvx.sbuf[i]=uvc_fb[i]; uvx.slen=uvc_fill; uvx.want=0; uvx_stills++; }
    uvc_fill=0; uvc_fbad=0; return 1; }
static int uvx_still_pick(int fi){ int best=-1; for(int k=0;k<uvx.ns;k++){ if(uvx.s[k].fi!=fi)continue; if(best<0||(u32)uvx.s[k].w*uvx.s[k].h>(u32)uvx.s[best].w*uvx.s[best].h)best=k; } return best; }
static int uvx_still_req(int fi,int vsif){
    if(uvx.method!=2)return -1; int k=uvx_still_pick(fi); if(k<0)return -2;
    u8 p[11]; for(int j=0;j<11;j++)p[j]=0; p[0]=(u8)fi; p[1]=uvx.s[k].idx; p[2]=1;
    if(!uvc_req(0x21,0x01,0x0300,(u16)vsif,11,0,p))return -3;
    u8 g[11]; for(int j=0;j<11;j++)g[j]=p[j]; if(uvc_req(0xA1,0x81,0x0300,(u16)vsif,11,1,g)) for(int j=0;j<11;j++)p[j]=g[j];
    if(!uvc_req(0x21,0x01,0x0400,(u16)vsif,11,0,p))return -4;
    u8 one=1; if(!uvc_req(0x21,0x01,0x0500,(u16)vsif,1,0,&one))return -5;
    uvx.sw=uvx.s[k].w; uvx.sh=uvx.s[k].h; uvx.slen=0; uvx.want=1; return k; }
int uvx_h264_n(void){ return cam.ok?uvx.nh:0; }
int uvx_h264_info(int i,int* w,int* h,u32* iv){ if(i<0||i>=uvx.nh)return 0; *w=uvx.h[i].w; *h=uvx.h[i].h; *iv=uvx.h[i].bestint; return 1; }
int uvx_is_capture(void){ return cam.ok&&uvx.capture; }
int uvx_still_method(void){ return cam.ok?uvx.method:0; }
int uvx_still_n(void){ return cam.ok?uvx.ns:0; }
int uvx_still_info(int i,int* fi,int* w,int* h){ if(i<0||i>=uvx.ns)return 0; *fi=uvx.s[i].fi; *w=uvx.s[i].w; *h=uvx.s[i].h; return 1; }
int uvx_h264_start(int w,int h){
    int pick=-1; for(int k=0;k<uvx.nh;k++){ if(uvx.h[k].w==w&&uvx.h[k].h==h){ pick=k; break; } if(pick<0||(u32)uvx.h[k].w*uvx.h[k].h>(u32)uvx.h[pick].w*uvx.h[pick].h)pick=k; }
    if(pick<0)return -1; uvx.force=pick; int r=uvc_start(1); uvx.force=-1; return r; }
int uvx_still_snap(void){
    if(uvc_state!=2)return -1; if(uvx.method!=2)return -1;
    if(!uvx.sbuf){ uvx.scap=1920u*1200u*2u+65536u; uvx.sbuf=(u8*)kmalloc(uvx.scap); if(!uvx.sbuf){ uvx.scap=0; return -1; } }
    int r=uvx_still_req(uvc_fi,cam.vs_if); if(r<0)return -1;
    u64 t0=hr_ns(); while(uvx.want&&hr_ns()-t0<2000000000ULL)uvc_poll();
    if(uvx.want){ uvx.want=0; return -1; } return (int)uvx.slen; }
const u8* uvx_still_data(int* w,int* h,u32* len){ *w=uvx.sw; *h=uvx.sh; *len=uvx.slen; return uvx.slen?uvx.sbuf:0; }
