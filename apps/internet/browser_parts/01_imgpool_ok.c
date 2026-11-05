static int imgpool_ok(void){ if(!imgpool)imgpool=(u8*)kmalloc(IMGPOOL_SZ); return imgpool!=0; }
static u8* g_jscr; static int g_jscr_cap; static u8 g_jscr_small[65536];
u8* jpeg_scratch_buf(void){ if(!g_jscr){ g_jscr=(u8*)kmalloc(10u*1024u*1024u); g_jscr_cap=g_jscr?(int)(10u*1024u*1024u):0; } return g_jscr?g_jscr:g_jscr_small; }
int jpeg_scratch_cap(void){ jpeg_scratch_buf(); return g_jscr?g_jscr_cap:(int)sizeof(g_jscr_small); }
u8* g_png_raw; int g_png_raw_sz;
void png_scratch_init(void){ if(!g_png_raw){ g_png_raw=(u8*)kmalloc(64*1024*1024); g_png_raw_sz=g_png_raw?64*1024*1024:0; } }

/* ---- cloud filesystem: sync FS files to the signed-in Noovex account ---- */
static const char B64E[]="ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
static int b64_enc(const u8* s,int n,char* o){ int j=0; for(int i=0;i<n;i+=3){ int b0=s[i],b1=(i+1<n)?s[i+1]:0,b2=(i+2<n)?s[i+2]:0;
    o[j++]=B64E[b0>>2]; o[j++]=B64E[((b0&3)<<4)|(b1>>4)]; o[j++]=(i+1<n)?B64E[((b1&15)<<2)|(b2>>6)]:'='; o[j++]=(i+2<n)?B64E[b2&63]:'='; } o[j]=0; return j; }
static int b64_val(char c){ if(c>='A'&&c<='Z')return c-'A'; if(c>='a'&&c<='z')return c-'a'+26; if(c>='0'&&c<='9')return c-'0'+52; if(c=='+')return 62; if(c=='/')return 63; return -1; }
static int b64_dec(const char* s,int n,u8* o){ int j=0,acc=0,bits=0; for(int i=0;i<n;i++){ int v=b64_val(s[i]); if(v<0)continue; acc=(acc<<6)|v; bits+=6; if(bits>=8){ bits-=8; o[j++]=(u8)((acc>>bits)&0xFF); } } return j; }
void nvx_file_put(const char* name,const u8* data,int len){
    if(!g_cloud_fs || !g_nvx_signedin || !imgpool_ok()) return;
    u32 ip; if(!parse_ip(NVX_HOST,&ip)){ if(dns_query(NVX_HOST,&ip)<=0) return; }
    if(!tcps_open(ip,443)) return;
    int b64n=b64_enc(data,len,(char*)imgpool);                 /* base64 the file into imgpool */
    char* req=(char*)jpeg_scratch; int r=0;
    const char* h="POST /api/public/files HTTP/1.0\r\nHost: " NVX_HOST "\r\nConnection: close\r\nUser-Agent: Noovex64\r\nContent-Type: application/json\r\nAuthorization: Bearer ";
    for(int i=0;h[i];i++)req[r++]=h[i]; for(int i=0;g_nvx_token[i];i++)req[r++]=g_nvx_token[i];
    /* body = {"name":"<name>","content":"<b64>"}  -> compute length first into imgpool tail is easier: build body inline */
    int bodystart_len = 0; /* build body after headers, but need Content-Length. build body into req tail region */
    /* assemble body in a temp position then splice: put body right after a fixed header block */
    char* body=(char*)jpeg_scratch + 5u*1024u*1024u; int bl=0;
    { char* p=body; *p++='{'; p=scopy(p,"\"name\":\""); for(const char* nn=name; *nn && (p-body)<200; nn++){ if(*nn=='"'||*nn=='\\')*p++='\\'; *p++=*nn; } p=scopy(p,"\",\"content\":\""); for(int i=0;i<b64n;i++)p[i]=((char*)imgpool)[i]; p+=b64n; *p++='"'; *p++='}'; bl=(int)(p-body); }
    const char* cl="\r\nContent-Length: "; for(int i=0;cl[i];i++)req[r++]=cl[i];
    { char t[12];int tn=0;int b=bl;if(b==0)t[tn++]='0';while(b){t[tn++]=(char)('0'+b%10);b/=10;}while(tn)req[r++]=t[--tn]; }
    req[r++]='\r';req[r++]='\n';req[r++]='\r';req[r++]='\n';
    for(int i=0;i<bl;i++)req[r++]=body[i];
    (void)bodystart_len;
    int hl=0; while(NVX_HOST[hl])hl++;
    u8 ent[64]; entropy_get(ent,64);
    tls_io* io=&g_tls_io; io->wr=tcps_wr; io->rd=tcps_rd; io->ctx=0; io->rlen=0; io->rpos=0;
    tls_get(io,(char*)NVX_HOST,hl,ent,req,r,http_buf,(int)sizeof(http_buf));
    secure_zero(ent,64);
}
/* pull all cloud files for the signed-in account into the FS (on login) */
void nvx_files_pull(void){
    if(!g_cloud_fs || !g_nvx_signedin || !imgpool_ok()) return;
    g_fs_pulling=1;
    char hdr[360],*hp=hdr; hp=scopy(hp,"Authorization: Bearer ");hp=scopy(hp,g_nvx_token);hp=scopy(hp,"\r\n");*hp=0;
    int n=https_request(NVX_HOST,"/api/public/files","GET",hdr,0,0,jpeg_scratch,jpeg_scratch_cap());
    if(n<=0){ g_fs_pulling=0; return; } int he=0; for(int i=0;i+3<n;i++)if(jpeg_scratch[i]=='\r'&&jpeg_scratch[i+1]=='\n'&&jpeg_scratch[i+2]=='\r'&&jpeg_scratch[i+3]=='\n'){he=i+4;break;}
    /* iterate "name":"..","content":".." pairs */
    for(int i=he;i+8<n;i++){
        if(jpeg_scratch[i]=='"'&&jpeg_scratch[i+1]=='n'&&jpeg_scratch[i+2]=='a'&&jpeg_scratch[i+3]=='m'&&jpeg_scratch[i+4]=='e'&&jpeg_scratch[i+5]=='"'){
            int p=i+6; while(p<n&&jpeg_scratch[p]!=':')p++;p++; while(p<n&&(jpeg_scratch[p]==' '||jpeg_scratch[p]=='"'))p++;
            char nm[64]; int o=0; while(p<n&&jpeg_scratch[p]!='"'&&o<63){ if(jpeg_scratch[p]=='\\'&&p+1<n)p++; nm[o++]=(char)jpeg_scratch[p]; p++; } nm[o]=0;
            /* find the content for this object */
            int cp=p; while(cp+8<n && !(jpeg_scratch[cp]=='"'&&jpeg_scratch[cp+1]=='c'&&jpeg_scratch[cp+2]=='o'&&jpeg_scratch[cp+3]=='n'&&jpeg_scratch[cp+4]=='t') ){ if(jpeg_scratch[cp]=='{')break; cp++; }
            if(cp+8<n && jpeg_scratch[cp]=='"'&&jpeg_scratch[cp+1]=='c'){ int q=cp+7; while(q<n&&jpeg_scratch[q]!=':')q++;q++; while(q<n&&(jpeg_scratch[q]==' '||jpeg_scratch[q]=='"'))q++;
                int cs=q; while(q<n&&jpeg_scratch[q]!='"')q++; int ce=q;
                int dl=b64_dec((char*)jpeg_scratch+cs,ce-cs,imgpool);
                if(nm[0]&&dl>=0) fs_save_bin(nm,imgpool,dl,0);
                i=q; }
        }
    }
    g_fs_pulling=0;
}
#define JP_BASE jpeg_scratch
#include "lib/image/png.h"
#include "lib/image/jpeg.h"
#include "assets/nvx_assets.h"
static u8 img_fetch_buf[262144];
static int g_isimg=0, img_w=0, img_h=0;          /* direct-URL image lives at imgpool offset 0 */
#define WEB_MAXIMG 8
typedef struct { char src[256]; int line, w, h, off, ok; } WebImg;
static WebImg webimg[WEB_MAXIMG]; static int web_nimg=0;

int is_webp(const u8*d,int len){ return len>=12 && d[0]=='R'&&d[1]=='I'&&d[2]=='F'&&d[3]=='F'&&d[8]=='W'&&d[9]=='E'&&d[10]=='B'&&d[11]=='P'; }
static int is_gif(const u8*d,int len){ return len>=13&&d[0]=='G'&&d[1]=='I'&&d[2]=='F'&&d[3]=='8'; }
static int looks_image(const u8*d,int len){ return len>=4 && ((d[0]==0x89&&d[1]==0x50)||(d[0]==0xFF&&d[1]==0xD8)||is_webp(d,len)||is_gif(d,len)||(d[0]=='B'&&d[1]=='M')); }
static int gif_decode(const u8* d,int len,u8* out,int cap,int* ow,int* oh){
    static u16 pre[4096]; static u8 suf[4096]; static u8 stk[4100];
    if(!is_gif(d,len))return -1;
    int W=d[6]|(d[7]<<8), H2=d[8]|(d[9]<<8); if(W<=0||H2<=0||W>2000||H2>2000)return -1;
    if((long)W*H2*4>cap)return -1;
    int p=13; const u8* gct=0; int gn=0; if(d[10]&0x80){ gn=2<<(d[10]&7); gct=d+p; p+=gn*3; if(p>len)return -1; }
    for(long q=0;q<(long)W*H2;q++){ out[q*4]=255; out[q*4+1]=255; out[q*4+2]=255; out[q*4+3]=0; }
    int trans=-1;
    while(p<len){ int b=d[p++];
        if(b==0x3B)break;
        if(b==0x21){ if(p>=len)return -1; int lab=d[p++]; if(lab==0xF9&&p+4<len&&d[p]>=4&&(d[p+1]&1))trans=d[p+4];
            while(p<len){ int sz=d[p++]; if(!sz)break; p+=sz; } continue; }
        if(b!=0x2C||p+9>len)return -1;
        int ix=d[p]|(d[p+1]<<8), iy=d[p+2]|(d[p+3]<<8), iw=d[p+4]|(d[p+5]<<8), ih=d[p+6]|(d[p+7]<<8), fl=d[p+8]; p+=9;
        const u8* ct=gct; int cn=gn; if(fl&0x80){ cn=2<<(fl&7); ct=d+p; p+=cn*3; if(p>len)return -1; }
        if(!ct||iw<=0||ih<=0||p>=len)return -1;
        int inter=fl&0x40, mcs=d[p++]; if(mcs<2||mcs>8)return -1;
        int clr=1<<mcs, eoi=clr+1, csz=mcs+1, avail=clr+2, old=-1, first=0;
        for(int q=0;q<clr;q++){ pre[q]=0; suf[q]=(u8)q; }
        u32 acc=0; int nb=0, bl=0, row=0, col=0, pass=0, done=0;
        static const int pst[4]={0,4,2,1}, pstep[4]={8,8,4,2};
        while(!done){
            while(nb<csz){ if(bl==0){ if(p>=len){done=1;break;} bl=d[p++]; if(!bl){done=1;break;} } if(p>=len){done=1;break;} acc|=(u32)d[p++]<<nb; nb+=8; bl--; }
            if(done)break;
            int code=(int)(acc&((1u<<csz)-1)); acc>>=csz; nb-=csz;
            if(code==clr){ csz=mcs+1; avail=clr+2; old=-1; continue; }
            if(code==eoi)break;
            int sp=0, c=code;
            if(old<0){ if(code>=clr)break; stk[sp++]=(u8)code; first=code; old=code; }
            else { if(code>avail)break;
                if(code==avail){ stk[sp++]=(u8)first; c=old; }
                while(c>=clr&&sp<4096){ stk[sp++]=suf[c]; c=pre[c]; }
                stk[sp++]=(u8)c; first=c;
                if(avail<4096){ pre[avail]=(u16)old; suf[avail]=(u8)first; avail++; if(avail==(1<<csz)&&csz<12)csz++; }
                old=code; }
            while(sp>0){ int idx=stk[--sp];
                if(row<ih){ int X=ix+col, Y=iy+row; if(X<W&&Y<H2&&idx!=trans&&idx<cn){ u8* o=out+((long)Y*W+X)*4; o[0]=ct[idx*3]; o[1]=ct[idx*3+1]; o[2]=ct[idx*3+2]; o[3]=255; } }
                if(++col>=iw){ col=0; if(inter){ row+=pstep[pass]; while(row>=ih&&pass<3){ pass++; row=pst[pass]; } } else row++; if(row>=ih&&(!inter||pass>=3)){ done=1; sp=0; } } } }
        *ow=W; *oh=H2; return 0; }
    return -1; }
/* decode bytes into imgpool at the current bump offset; returns 1 ok, sets off/w/h and advances pool */
static int bmp_decode(const u8* d,int len,u8* out,int cap,int* ow,int* oh){
    if(len<54||d[0]!='B'||d[1]!='M')return -1;
    int off=d[10]|(d[11]<<8)|(d[12]<<16)|(d[13]<<24);
    int w=d[18]|(d[19]<<8)|(d[20]<<16)|(d[21]<<24);
    int h=d[22]|(d[23]<<8)|(d[24]<<16)|(d[25]<<24);
    int bpp=d[28]|(d[29]<<8), comp=d[30]|(d[31]<<8);
    if(comp!=0||w<=0||w>2000)return -1;
    int flip=1; if(h<0){h=-h;flip=0;} if(h<=0||h>2000)return -1;
    if(bpp!=24&&bpp!=32)return -1;
    long need=(long)w*h*4; if(need>cap)return -1;
    int bypp=bpp/8, rowsz=((w*bypp)+3)&~3;
    for(int y=0;y<h;y++){ int sy=flip?(h-1-y):y; const u8* row=d+off+(long)sy*rowsz; if(row+w*bypp>d+len)return -1;
        u8* o=out+(long)y*w*4; for(int x=0;x<w;x++){ int b=row[x*bypp],g=row[x*bypp+1],r=row[x*bypp+2]; o[x*4]=r;o[x*4+1]=g;o[x*4+2]=b;o[x*4+3]=255; } }
    *ow=w;*oh=h; return 0;
}
static int decode_into_pool(const u8*d,int len,int*off,int*w,int*h){
    if(!imgpool_ok()) return 0;
    if(len<4 || imgpool_off>=IMGPOOL_SZ-8192) return 0;
    u8* dst=imgpool+imgpool_off; int cap=IMGPOOL_SZ-imgpool_off, ww=0,hh=0,r=-1;
    if(d[0]==0x89&&d[1]==0x50&&d[2]==0x4E&&d[3]==0x47) r=png_decode(d,len,PNG_RAW,PNG_RAW_SZ,dst,cap,&ww,&hh);
    else if(d[0]==0xFF&&d[1]==0xD8){ jp_off=0; r=jpeg_decode(d,len,dst,cap,&ww,&hh); }
    else if(is_webp(d,len)) r=webp_decode(d,len,dst,cap,&ww,&hh);
    else if(is_gif(d,len)) r=gif_decode(d,len,dst,cap,&ww,&hh);
    else if(d[0]=='B'&&d[1]=='M') r=bmp_decode(d,len,dst,cap,&ww,&hh);
    else return 0;
    if(r<0||ww<=0||hh<=0||ww>2000||hh>2000) return 0;
    long bytes=(long)ww*hh*4; if(bytes>cap) return 0;
    *off=imgpool_off; *w=ww; *h=hh; imgpool_off+=(int)bytes; return 1;
}
static int decode_image(const u8*d,int len){   /* direct-URL: decode to pool[0] */
    imgpool_off=0; int off=0,w=0,h=0; if(!decode_into_pool(d,len,&off,&w,&h)) return 0; img_w=w; img_h=h; return 1;
}

#include "include/nvx/apps/nwe_api.h"
#include "include/nvx/apps/internet/webglue.h"
#include "include/nvx/net/http/cookies.h"
static int g_ur_on,g_ur_sx,g_ur_wx,g_ur_y; static unsigned long g_ur_wait;

int g_page=0; static int lk_y[16],lk_tgt[16],lk_n=0,lk_bx[16],lk_bw[16],lk_bh[16];
char g_url[4096]="example.com"; int g_urllen=11, g_urlfocus=0;
static char g_acsug[4096]=""; static int g_urlsel=0;
static char g_host[256]="";
static int br_barx,br_bary,br_barw,br_gx,br_gw,br_hx,br_hw,br_bkx,br_bkw,br_rlx,br_rlw;
int web_nl=0, web_scroll=0;
static char web_stat[200]="";
static char g_crash_why[200]="";
static void sset(char* d,const char* s){ int n=0; while(*s&&n<4095){ *d++=*s++; n++; } *d=0; }
static char g_title[128]="";
char backstk[16][4096]; int backn=0; char g_cururl[4096]="";
int g_httplen=0, g_bodyoff=0, g_dln=0;
static int g_haspage=0;
static int g_cx=0,g_cy=0,g_cw=0,g_ch=0;
static int g_lastvw=0,g_lastvh=0;
static int g_lastmx=-1,g_lastmy=-1;
static char g_hoverhref[512]="";
static int g_doch=0,g_scrolly=0,g_viewh=0;
static char* g_post_body=0; static int g_post_len=0;
static char* g_lastdoc=0; static int g_lastdoclen=0;
static u64 g_lasttick=0;
static int g_findmode=0; static char g_findstr[64]=""; static int g_findlen=0; static int g_findn=-1;
static int g_starx,g_stary,g_starw, g_findbx,g_findby,g_findbw, g_cookbx,g_cookbw;
static nwe_req g_req;

static int g_go_err0;
void nwk_status(const char* s){ static char db[96]; if(s[0]=='D'&&s[1]=='o'&&s[2]=='n'&&s[3]=='e'&&!s[4]){ extern int web_err_total(void); int ne=web_err_total()-g_go_err0; if(ne>0){ char* q=db; q=scopy(q,"Done, "); q=udec(q,(u32)ne); q=scopy(q,ne==1?" error - see about:errors":" errors - see about:errors"); *q=0; s=db; } } int i=0; for(;s[i]&&i<(int)sizeof(web_stat)-1;i++)web_stat[i]=s[i]; web_stat[i]=0; { const char* pf="Page engine stopped: "; int k=0; while(pf[k]&&s[k]==pf[k])k++; if(!pf[k]){ int j=0; for(;s[k+j]&&j<(int)sizeof(g_crash_why)-1;j++)g_crash_why[j]=s[k+j]; g_crash_why[j]=0; } } }

int nwk_image(const unsigned char* d,int len,unsigned int** argb,int* ow,int* oh){
    *argb=0; *ow=0; *oh=0;
    if(len<4) return -1;
    static u8* tmp=0; const int TMPCAP=24*1024*1024;
    if(!tmp){ tmp=(u8*)kmalloc(TMPCAP); if(!tmp) return -1; }
    int w=0,h=0,r=-1;
    if(d[0]==0x89&&d[1]==0x50&&d[2]==0x4E&&d[3]==0x47) r=png_decode(d,len,PNG_RAW,PNG_RAW_SZ,tmp,TMPCAP,&w,&h);
    else if(d[0]==0xFF&&d[1]==0xD8){ jp_off=0; r=jpeg_decode(d,len,tmp,TMPCAP,&w,&h); }
    else if(is_webp(d,len)) r=webp_decode(d,len,tmp,TMPCAP,&w,&h);
    else if(is_gif(d,len)) r=gif_decode(d,len,tmp,TMPCAP,&w,&h);
    else if(d[0]=='B'&&d[1]=='M') r=bmp_decode(d,len,tmp,TMPCAP,&w,&h);
    else if(len>=4&&d[0]==0&&d[1]==0&&d[2]==1&&d[3]==0){
        int cnt=d[4]|(d[5]<<8); int best=-1,bw=0; for(int i=0;i<cnt&&6+i*16+16<=len;i++){ const u8* e=d+6+i*16; int ew=e[0]?e[0]:256; if(ew>bw&&ew<=64){bw=ew;best=i;} }
        if(best<0&&cnt>0)best=0;
        if(best>=0){ const u8* e=d+6+best*16; int sz=e[8]|(e[9]<<8)|(e[10]<<16)|(e[11]<<24); int off=e[12]|(e[13]<<8)|(e[14]<<16)|(e[15]<<24);
            if(off>0&&off+sz<=len&&sz>8){ const u8* p=d+off; if(p[0]==0x89&&p[1]==0x50) r=png_decode(p,sz,PNG_RAW,PNG_RAW_SZ,tmp,TMPCAP,&w,&h);
                else { int bw2=p[4]|(p[5]<<8)|(p[6]<<16)|(p[7]<<24), bh2=(p[8]|(p[9]<<8)|(p[10]<<16)|(p[11]<<24))/2; int bpp=p[14]|(p[15]<<8);
                    if(bpp==32&&bw2>0&&bh2>0&&bw2<=256&&bh2<=256&&40+bw2*bh2*4<=sz){ for(int y=0;y<bh2;y++) for(int x=0;x<bw2;x++){ const u8* s=p+40+((bh2-1-y)*bw2+x)*4; u8* o=tmp+(y*bw2+x)*4; o[0]=s[2];o[1]=s[1];o[2]=s[0];o[3]=s[3]; } w=bw2; h=bh2; r=0; } } } } }
    if(r<0||w<=0||h<=0||(long)w*h*4>TMPCAP) return -1;
    unsigned int* out=(unsigned int*)kmalloc((unsigned long)w*(unsigned long)h*4UL);
    if(!out) return -1;
    for(long i=0;i<(long)w*h;i++){ u8* p=tmp+i*4; out[i]=((unsigned)p[3]<<24)|((unsigned)p[0]<<16)|((unsigned)p[1]<<8)|(unsigned)p[2]; }
    *argb=out; *ow=w; *oh=h; return 0;
}

int ci_eq(char a,char b){ if(a>='A'&&a<='Z')a+=32; if(b>='A'&&b<='Z')b+=32; return a==b; }
int gunzip(const u8* src,int n,u8* out,int outcap){
    if(n<18||src[0]!=0x1F||src[1]!=0x8B||src[2]!=8) return -1;
    int flg=src[3], p=10;
    if(flg&4){ if(p+2>n)return -1; int xl=src[p]|(src[p+1]<<8); p+=2+xl; }
    if(flg&8){ while(p<n&&src[p])p++; p++; }
    if(flg&16){ while(p<n&&src[p])p++; p++; }
    if(flg&2) p+=2;
    if(p>=n) return -1;
    return pz_inflate_raw(src+p,n-p,out,outcap);
}

#define BM_N 32
#define HIST_N 64
static char bmarks[BM_N][256]; static int nbmarks=0;
static char hist[HIST_N][256]; static int nhist=0;
static int bm_has(const char*u){ for(int i=0;i<nbmarks;i++)if(seq(bmarks[i],u))return 1; return 0; }
static void list_to_fs(const char*fname,const char*title,char list[][256],int cnt){
    static char tmp[20000]; int p=0; const char* h1="<h1>"; for(int i=0;h1[i];i++)tmp[p++]=h1[i];
    for(int i=0;title[i]&&p<19000;i++)tmp[p++]=title[i]; const char* h2="</h1>\n"; for(int i=0;h2[i];i++)tmp[p++]=h2[i];
    for(int k=cnt-1;k>=0&&p<19000;k--){ const char* a="<p><a href=\""; for(int i=0;a[i];i++)tmp[p++]=a[i];
        for(int i=0;list[k][i]&&p<19200;i++)tmp[p++]=list[k][i]; tmp[p++]='"'; tmp[p++]='>';
        for(int i=0;list[k][i]&&p<19500;i++)tmp[p++]=list[k][i]; const char* e="</a></p>\n"; for(int i=0;e[i];i++)tmp[p++]=e[i]; }
    tmp[p]=0; fs_save_bin(fname,(const u8*)tmp,p,0);
}
static void bm_save_fs(void){ extern void acct_profile_name(const char*,char*,int); char n[64]; acct_profile_name("bookmarks.html",n,64); list_to_fs(n,"Bookmarks",bmarks,nbmarks); }
static void hist_save_fs(void){ extern void acct_profile_name(const char*,char*,int); char n[64]; acct_profile_name("history.html",n,64); list_to_fs(n,"History",hist,nhist); }
static void cut256(char* d,const char* s){ int i=0; for(;s[i]&&i<255;i++)d[i]=s[i]; d[i]=0; }
static void hist_push(const char*u){ if(!u[0])return; if(nhist>0&&seq(hist[nhist-1],u))return;
    if(nhist<HIST_N)cut256(hist[nhist++],u); else { for(int i=1;i<HIST_N;i++)sset(hist[i-1],hist[i]); cut256(hist[HIST_N-1],u); } hist_save_fs(); nvx_hist_add(u); }
static void bm_toggle(const char*u){ if(!u[0])return;
    for(int i=0;i<nbmarks;i++)if(seq(bmarks[i],u)){ for(int j=i+1;j<nbmarks;j++)sset(bmarks[j-1],bmarks[j]); nbmarks--; bm_save_fs(); return; }
    if(nbmarks<BM_N)cut256(bmarks[nbmarks++],u); bm_save_fs(); }
static void nvx_hist_fetch(void){
    if(!g_nvx_signedin) return;
    char hdr[360],*hp=hdr; hp=scopy(hp,"Authorization: Bearer ");hp=scopy(hp,g_nvx_token);hp=scopy(hp,"\r\n");*hp=0;
    int n=https_request(NVX_HOST,"/api/public/history","GET",hdr,0,0,http_buf,(int)sizeof(http_buf));
    if(n<=0) return; int he=0; for(int i=0;i+3<n;i++)if(http_buf[i]=='\r'&&http_buf[i+1]=='\n'&&http_buf[i+2]=='\r'&&http_buf[i+3]=='\n'){he=i+4;break;}
    int cnt=0;
    for(int i=he;i+6<n && cnt<HIST_N;i++){
        if(http_buf[i]=='"'&&http_buf[i+1]=='u'&&http_buf[i+2]=='r'&&http_buf[i+3]=='l'&&http_buf[i+4]=='"'){
            int p=i+5; while(p<n&&http_buf[p]!=':')p++; p++; while(p<n&&(http_buf[p]==' '||http_buf[p]=='"'))p++;
            int o=0; while(p<n&&http_buf[p]!='"'&&o<255){ if(http_buf[p]=='\\'&&p+1<n)p++; hist[cnt][o++]=(char)http_buf[p]; p++; }
            hist[cnt][o]=0; if(o>0)cnt++; i=p; }
    }
    if(cnt>0) nhist=cnt;
}

typedef struct { char* s; int n,cap; } hb;
static void hb_put(hb* b,const char* s){ int l=slen(s); if(b->n+l+1>b->cap){ int nc=(b->n+l+1)*2; if(nc<4096)nc=4096; char* ns=(char*)kmalloc((unsigned long)nc); if(!ns)return; for(int i=0;i<b->n;i++)ns[i]=b->s[i]; if(b->s)kfree(b->s); b->s=ns; b->cap=nc; } for(int i=0;i<l;i++)b->s[b->n++]=s[i]; b->s[b->n]=0; }
static void hb_esc(hb* b,const char* s){ char t[2]={0,0}; for(;*s;s++){ if(*s=='<')hb_put(b,"&lt;"); else if(*s=='>')hb_put(b,"&gt;"); else if(*s=='&')hb_put(b,"&amp;"); else if(*s=='"')hb_put(b,"&quot;"); else { t[0]=*s; hb_put(b,t); } } }
static const char* PAGE_CSS="<style>body{font-family:Arial,sans-serif;margin:0;background:#f4f6fb;color:#1f2937}main{max-width:820px;margin:32px auto;padding:0 20px}h1{font-size:28px;margin:0 0 18px}"
    ".card{background:#fff;border-radius:12px;box-shadow:0 2px 10px rgba(0,0,0,.08);padding:16px 20px;margin-bottom:14px}a{color:#2563eb;text-decoration:none}a:hover{text-decoration:underline}"
    ".muted{color:#6b7280;font-size:14px}.row{display:flex;justify-content:space-between;gap:12px;padding:8px 0;border-bottom:1px solid #eef0f4}.row:last-child{border-bottom:0}"
    ".btn{display:inline-block;background:#2563eb;color:#fff;padding:8px 14px;border-radius:8px}.btn.red{background:#dc2626}code{background:#eef2ff;padding:1px 5px;border-radius:4px}</style>";

static int g_crash_shown=0;
static void web_load_html(const char* url,const char* html,int len,int istext);
static void show_page(const char* url,hb* b,const char* title){
    web_load_html(url,b->s?b->s:"",b->n,0);
    if(b->s)kfree(b->s);
    int t=0; while(title[t]&&t<127){g_title[t]=title[t];t++;} g_title[t]=0;
}
static void render_local(const char*title,char list[][256],int cnt,const char*empty,const char* url){
    hb b={0,0,0}; hb_put(&b,"<!doctype html><html><head><title>"); hb_put(&b,title); hb_put(&b,"</title>"); hb_put(&b,PAGE_CSS); hb_put(&b,"</head><body><main><h1>"); hb_put(&b,title); hb_put(&b,"</h1><div class=card>");
    if(cnt==0){ hb_put(&b,"<p class=muted>"); hb_put(&b,empty); hb_put(&b,"</p>"); }
    for(int k=cnt-1;k>=0;k--){ hb_put(&b,"<div class=row><a href=\""); hb_esc(&b,list[k]); hb_put(&b,"\">"); hb_esc(&b,list[k]); hb_put(&b,"</a></div>"); }
    hb_put(&b,"</div></main></body></html>");
    show_page(url,&b,title); g_cururl[0]=0;
}
static void show_bookmarks(void){ render_local("Bookmarks",bmarks,nbmarks,"No bookmarks yet. Open a page and click the star.","about:bookmarks"); }
static void show_history(void){ nvx_hist_fetch(); render_local("History",hist,nhist,"No history yet.","about:history"); }
static void show_cookies(void){
    hb b={0,0,0}; hb_put(&b,"<!doctype html><html><head><title>Cookies</title>"); hb_put(&b,PAGE_CSS); hb_put(&b,"</head><body><main><h1>Cookies and site data</h1>");
    char nb[16],*np=nb; np=udec(np,(u32)cookie_count()); *np=0;
    hb_put(&b,"<div class=card><p>"); hb_put(&b,nb); hb_put(&b," cookies stored. Cookies that have an expiry date are kept across reboots in <code>cookies.txt</code>; site storage is kept in <code>webstorage.txt</code>.</p><p><a class='btn red' href='about:cookies?clear'>Clear cookies, cache and site data</a></p></div><div class=card>");
    char dom[128],nm[64],val[512]; u64 ex; int fl;
    for(int i=0;cookie_list(i,dom,sizeof dom,nm,sizeof nm,val,sizeof val,&ex,&fl)==0;i++){
        hb_put(&b,"<div class=row><div><b>"); hb_esc(&b,nm); hb_put(&b,"</b> <span class=muted>"); hb_esc(&b,dom);
        if(fl&1)hb_put(&b," &middot; Secure"); if(fl&2)hb_put(&b," &middot; HttpOnly"); hb_put(&b,ex?" &middot; persistent":" &middot; session");
        hb_put(&b,"</span><div class=muted style='word-break:break-all'>"); char v2[90]; int k=0; for(;val[k]&&k<80;k++)v2[k]=val[k]; v2[k]=0; hb_esc(&b,v2); if(val[k])hb_put(&b,"..."); hb_put(&b,"</div></div><a href='about:cookies?del="); hb_esc(&b,dom); hb_put(&b,"'>Remove site</a></div>");
    }
    hb_put(&b,"</div></main></body></html>");
    show_page("about:cookies",&b,"Cookies");
}
static nwk_slow g_perf_slow; static nwk_netstat g_go_ns0, g_perf_ns, g_load_ns, g_after_ns; static nwe_perf g_perf_last, g_perf_after; static unsigned long long g_load_end, g_after_wall; static int g_after_have; static unsigned long long g_go_t0, g_perf_total; static int g_go_on, g_perf_have; static char g_perf_url[256];
static void ms1(char** q,unsigned long long us){ u32 t=(u32)(us/100000ull); *q=udec(*q,t/10); *(*q)++='.'; *(*q)++=(char)('0'+t%10); *q=scopy(*q," s"); }
static void perf_after(void){
    if(!g_perf_have||!g_haspage) return;
    extern nwk_netstat g_netstat; nwk_netstat d=g_netstat; unsigned long long* a=(unsigned long long*)&d; const unsigned long long* b=(const unsigned long long*)&g_load_ns; for(unsigned i=0;i<sizeof(d)/8;i++)a[i]-=b[i];
    g_after_ns=d; static nwe_req pr; for(unsigned k=0;k<sizeof(pr);k++)((u8*)&pr)[k]=0; web_call(NWE_OP_PERF,&pr); const u8* src=(const u8*)pr.out; u8* dst=(u8*)&g_perf_after; for(unsigned k=0;k<sizeof(g_perf_after);k++)dst[k]=src[k];
    g_after_wall=nwk_us()-g_load_end; g_after_have=1;
}
static void prow(hb* b,const char* k,unsigned long long us,const char* extra){ char t[80],*q=t; ms1(&q,us); *q=0; hb_put(b,"<div class=row><div>"); hb_put(b,k); if(extra){ hb_put(b," <span class=muted>"); hb_put(b,extra); hb_put(b,"</span>"); } hb_put(b,"</div><b>"); hb_put(b,t); hb_put(b,"</b></div>"); }
static void show_perf(void){
    hb b={0,0,0}; hb_put(&b,"<!doctype html><html><head><title>Page timing</title>"); hb_put(&b,PAGE_CSS); hb_put(&b,"</head><body><main><h1>Page timing</h1>");
    if(!g_perf_have){ hb_put(&b,"<div class=card><p class=muted>Open a web page first, then come back here.</p></div></main></body></html>"); show_page("about:perf",&b,"Page timing"); return; }
    hb_put(&b,"<div class=card><p class=muted>"); hb_esc(&b,g_perf_url); hb_put(&b,"</p>");
    char x[96],*q;
    prow(&b,"Total",g_perf_total,0);
    q=x; q=udec(q,(u32)g_perf_ns.requests); q=scopy(q," requests, "); q=udec(q,(u32)(g_perf_ns.bytes/1024)); q=scopy(q," KB, "); q=udec(q,(u32)g_perf_ns.cache_hits); q=scopy(q," from the cache, "); q=udec(q,(u32)g_perf_ns.prefetched); q=scopy(q," fetched in parallel"); *q=0; prow(&b,"Network",g_perf_ns.fetch_us,x);
    prow(&b,"&nbsp; Name lookups",g_perf_ns.dns_us,0);
    q=x; q=udec(q,(u32)g_perf_ns.conns); q=scopy(q," new, "); q=udec(q,(u32)g_perf_ns.reused); q=scopy(q," reused"); if(g_perf_ns.conns){ q=scopy(q,", "); q=udec(q,(u32)(g_perf_ns.connect_us/1000/g_perf_ns.conns)); q=scopy(q," ms each"); } if(g_perf_ns.syn_resends){ q=scopy(q,", "); q=udec(q,(u32)g_perf_ns.syn_resends); q=scopy(q," SYN resent"); } *q=0; prow(&b,"&nbsp; Connecting",g_perf_ns.connect_us,x);
    if(g_perf_slow.ms){ char y[160],*r=y; r=scopy(r,g_perf_slow.host); r=scopy(r,": lookup "); r=udec(r,g_perf_slow.dns); r=scopy(r," ms, connect "); r=udec(r,g_perf_slow.tcp); r=scopy(r," ms, handshake "); r=udec(r,g_perf_slow.tls); r=scopy(r," ms"); *r=0; prow(&b,"&nbsp; Slowest new connection",(unsigned long long)g_perf_slow.ms*1000ULL,y); }
    q=x; q=udec(q,(u32)g_perf_ns.handshakes); q=scopy(q," handshakes"); *q=0; prow(&b,"&nbsp; Secure handshakes",g_perf_ns.tls_us,x);
    prow(&b,"&nbsp; Waiting for the server",g_perf_ns.wait_us,0);
    prow(&b,"&nbsp; Downloading",g_perf_ns.body_us,0);
    q=x; q=udec(q,(u32)g_perf_last.scripts); q=scopy(q," scripts, "); q=udec(q,(u32)g_perf_last.code_hits); q=scopy(q," from the code cache"); *q=0; prow(&b,"Scripts",g_perf_last.js_us,x);
    prow(&b,"&nbsp; Compiling",g_perf_last.compile_us,0);
    prow(&b,"Styles",g_perf_last.style_us,0);
    prow(&b,"Layout",g_perf_last.layout_us,0);
    prow(&b,"Painting",g_perf_last.paint_us,0);
    { static nwe_req pm; for(unsigned k=0;k<sizeof(pm);k++)((u8*)&pm)[k]=0; if(!web_busy()){ web_call(NWE_OP_PERF,&pm); nwe_perf mp; const u8* s2=(const u8*)pm.out; u8* d2=(u8*)&mp; for(unsigned k=0;k<sizeof(mp);k++)d2[k]=s2[k];
        if(mp.mem_size){ char y[160],*r=y; r=udec(r,(u32)(mp.mem_used>>20)); r=scopy(r," MB of "); r=udec(r,(u32)(mp.mem_size>>20)); r=scopy(r," MB, scripts "); r=udec(r,(u32)(mp.js_mem>>20)); r=scopy(r," MB"); *r=0;
            hb_put(&b,"<div class=row><div>Page memory</div><div>"); hb_put(&b,y); hb_put(&b,"</div></div>"); } }
      extern int nwk_net_active(long); char y[64],*r=y; r=udec(r,(u32)nwk_net_active(0)); r=scopy(r," open"); *r=0; hb_put(&b,"<div class=row><div>Background requests</div><div>"); hb_put(&b,y); hb_put(&b,"</div></div>"); }
    q=x; q=udec(q,(u32)g_perf_ns.pumps); q=scopy(q," turns"); *q=0; prow(&b,"Mouse pointer while busy",g_perf_ns.pump_us,x);
    if(g_after_have){
        hb_put(&b,"</div><div class=card><p><b>After the page loaded</b> <span class=muted>(until this page was opened)</span></p>");
        prow(&b,"Time since it loaded",g_after_wall,0);
        q=x; q=udec(q,(u32)g_perf_after.after_ops); q=scopy(q," turns"); *q=0; prow(&b,"Page engine busy",g_perf_after.after_us,x);
        prow(&b,"&nbsp; Timers and animation frames",g_perf_after.tick_us,0);
        prow(&b,"&nbsp; Mouse and keyboard events",g_perf_after.event_us,0);
        prow(&b,"&nbsp; Longest single turn",g_perf_after.longest_us,0);
        q=x; q=udec(q,(u32)g_perf_after.after_scripts); q=scopy(q," scripts added later"); *q=0; prow(&b,"&nbsp; Scripts loaded later",g_perf_after.after_js_us,x);
        q=x; q=udec(q,(u32)g_after_ns.requests); q=scopy(q," requests, "); q=udec(q,(u32)(g_after_ns.bytes/1024)); q=scopy(q," KB, "); q=udec(q,(u32)g_after_ns.handshakes); q=scopy(q," handshakes"); *q=0; prow(&b,"&nbsp; Network",g_after_ns.fetch_us,x);
    }
    hb_put(&b,"<p class=muted>Network time includes the scripts and styles the page loads; script time includes downloads started by scripts.</p></div></main></body></html>");
    show_page("about:perf",&b,"Page timing");
}
