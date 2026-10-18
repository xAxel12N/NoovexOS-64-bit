#include "include/nvx/core/klog.h"
#include "include/nvx/base.h"
#include "include/nvx/lib/crypto/nvxtls.h"
#include "include/nvx/lib/mem.h"
#include "include/nvx/mm/heap.h"
#include "include/nvx/lib/fmt.h"
#include "include/nvx/drivers/video/framebuffer.h"
#include "include/nvx/lib/js.h"
#include "include/nvx/drivers/net/nic.h"
#include "include/nvx/drivers/storage/ahci.h"
#include "include/nvx/net/tcpip.h"
#include "include/nvx/net/http.h"
#include "include/nvx/gui/theme.h"
#include "include/nvx/gui/wm.h"
#include "include/nvx/shell/terminal.h"
#include "include/nvx/core/syscall.h"
#include "include/nvx/apps/browser.h"
#include "include/nvx/gui/desktop.h"
/* =========================================================================
   Browser (online: http + https, text rendering)
   ========================================================================= */
/* =========================================================================
   Image decode (PNG all common types, baseline JPEG) + multi-image pool
   ========================================================================= */
#define IMGPOOL_SZ (8*1024*1024)
static u8 imgpool[IMGPOOL_SZ]; static int imgpool_off=0;
u8 jpeg_scratch[10u*1024u*1024u];
u8* g_png_raw; int g_png_raw_sz;
void png_scratch_init(void){ if(!g_png_raw){ g_png_raw=(u8*)kmalloc(64*1024*1024); g_png_raw_sz=g_png_raw?64*1024*1024:0; } }

/* ---- cloud filesystem: sync FS files to the signed-in Noovex account ---- */
static const char B64E[]="ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
static int b64_enc(const u8* s,int n,char* o){ int j=0; for(int i=0;i<n;i+=3){ int b0=s[i],b1=(i+1<n)?s[i+1]:0,b2=(i+2<n)?s[i+2]:0;
    o[j++]=B64E[b0>>2]; o[j++]=B64E[((b0&3)<<4)|(b1>>4)]; o[j++]=(i+1<n)?B64E[((b1&15)<<2)|(b2>>6)]:'='; o[j++]=(i+2<n)?B64E[b2&63]:'='; } o[j]=0; return j; }
static int b64_val(char c){ if(c>='A'&&c<='Z')return c-'A'; if(c>='a'&&c<='z')return c-'a'+26; if(c>='0'&&c<='9')return c-'0'+52; if(c=='+')return 62; if(c=='/')return 63; return -1; }
static int b64_dec(const char* s,int n,u8* o){ int j=0,acc=0,bits=0; for(int i=0;i<n;i++){ int v=b64_val(s[i]); if(v<0)continue; acc=(acc<<6)|v; bits+=6; if(bits>=8){ bits-=8; o[j++]=(u8)((acc>>bits)&0xFF); } } return j; }
void nvx_file_put(const char* name,const u8* data,int len){
    if(!g_cloud_fs || !g_nvx_signedin) return;
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
    if(!g_cloud_fs || !g_nvx_signedin) return;
    g_fs_pulling=1;
    char hdr[360],*hp=hdr; hp=scopy(hp,"Authorization: Bearer ");hp=scopy(hp,g_nvx_token);hp=scopy(hp,"\r\n");*hp=0;
    int n=https_request(NVX_HOST,"/api/public/files","GET",hdr,0,0,jpeg_scratch,(int)sizeof(jpeg_scratch));
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
#include "lib/png.h"
#include "lib/jpeg.h"
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
#include "include/nvx/apps/webglue.h"
#include "include/nvx/net/cookies.h"

int g_page=0; static int lk_y[16],lk_tgt[16],lk_n=0,lk_bx[16],lk_bw[16],lk_bh[16];
char g_url[2048]="example.com"; int g_urllen=11, g_urlfocus=0;
static char g_acsug[2048]=""; static int g_urlsel=0;
static char g_host[256]="";
static int br_barx,br_bary,br_barw,br_gx,br_gw,br_hx,br_hw,br_bkx,br_bkw,br_rlx,br_rlw;
int web_nl=0, web_scroll=0;
static char web_stat[200]="";
static void sset(char* d,const char* s){ while(*s)*d++=*s++; *d=0; }
static char g_title[128]="";
char backstk[16][2048]; int backn=0; char g_cururl[2048]="";
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

void nwk_status(const char* s){ int i=0; for(;s[i]&&i<(int)sizeof(web_stat)-1;i++)web_stat[i]=s[i]; web_stat[i]=0; }

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
static void bm_save_fs(void){ list_to_fs("bookmarks.html","Bookmarks",bmarks,nbmarks); }
static void hist_save_fs(void){ list_to_fs("history.html","History",hist,nhist); }
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
    hb_put(&b,"<div class=card><p>"); hb_put(&b,nb); hb_put(&b," cookies stored. Cookies that have an expiry date are kept across reboots in <code>cookies.txt</code>; site storage is kept in <code>webstorage.txt</code>.</p><p><a class='btn red' href='about:cookies?clear'>Clear all cookies and site data</a></p></div><div class=card>");
    char dom[128],nm[64],val[512]; u64 ex; int fl;
    for(int i=0;cookie_list(i,dom,sizeof dom,nm,sizeof nm,val,sizeof val,&ex,&fl)==0;i++){
        hb_put(&b,"<div class=row><div><b>"); hb_esc(&b,nm); hb_put(&b,"</b> <span class=muted>"); hb_esc(&b,dom);
        if(fl&1)hb_put(&b," &middot; Secure"); if(fl&2)hb_put(&b," &middot; HttpOnly"); hb_put(&b,ex?" &middot; persistent":" &middot; session");
        hb_put(&b,"</span><div class=muted style='word-break:break-all'>"); char v2[90]; int k=0; for(;val[k]&&k<80;k++)v2[k]=val[k]; v2[k]=0; hb_esc(&b,v2); if(val[k])hb_put(&b,"..."); hb_put(&b,"</div></div><a href='about:cookies?del="); hb_esc(&b,dom); hb_put(&b,"'>Remove site</a></div>");
    }
    hb_put(&b,"</div></main></body></html>");
    show_page("about:cookies",&b,"Cookies");
}
static void show_console(void){
    hb b={0,0,0}; hb_put(&b,"<!doctype html><html><head><title>Console</title>"); hb_put(&b,PAGE_CSS); hb_put(&b,"</head><body><main><h1>JavaScript console</h1><div class=card style='font-family:monospace;font-size:13px'>");
    int n=web_console_count(); if(!n)hb_put(&b,"<p class=muted>No messages.</p>");
    char line[220]; for(int i=0;i<n;i++){ web_console_get(i,line,sizeof line); hb_put(&b,"<div class=row>"); hb_esc(&b,line); hb_put(&b,"</div>"); }
    hb_put(&b,"</div></main></body></html>");
    show_page("about:console",&b,"Console");
}
static const char JS_DEMO[]="<!doctype html><html><head><title>Engine demo</title><style>body{font-family:Arial,sans-serif;margin:0;background:linear-gradient(135deg,#1e3a8a,#7c3aed);color:#fff;min-height:100vh}"
    "main{max-width:760px;margin:0 auto;padding:40px 20px}h1{font-size:40px;margin:0 0 8px}.grid{display:grid;grid-template-columns:repeat(auto-fill,minmax(160px,1fr));gap:12px;margin:20px 0}"
    ".t{background:rgba(255,255,255,.14);border-radius:14px;padding:16px;text-align:center}.t b{display:block;font-size:30px}button{background:#fff;color:#4c1d95;border:0;border-radius:10px;padding:10px 18px;font-size:16px;font-weight:bold}"
    "input{padding:9px;border-radius:8px;border:0;width:260px;font-size:15px}#log{margin-top:16px;background:rgba(0,0,0,.25);border-radius:10px;padding:12px;font-family:monospace;white-space:pre-wrap}</style></head>"
    "<body><main><h1>Noovex Web Engine</h1><p>QuickJS JavaScript, real CSS layout, flexbox, grid and cookies.</p><div class=grid><div class=t><b id=c>0</b>clicks</div><div class=t><b id=t>0</b>seconds</div><div class=t><b id=v>0</b>visits</div><div class=t><b id=f>0</b>fib(25)</div></div>"
    "<p><button id=b>Click me</button> <input id=i placeholder='Type something'></p><div id=log></div></main>"
    "<script>let n=0;const $=s=>document.querySelector(s);$('#b').addEventListener('click',()=>{$('#c').textContent=++n;});"
    "$('#i').addEventListener('input',e=>{$('#log').textContent='You typed: '+e.target.value;});"
    "let s=0;setInterval(()=>{$('#t').textContent=++s;},1000);const v=(+localStorage.getItem('visits')||0)+1;localStorage.setItem('visits',v);$('#v').textContent=v;document.cookie='demo_visits='+v+'; max-age=31536000';"
    "const fib=k=>k<2?k:fib(k-1)+fib(k-2);const t0=performance.now();const r=fib(25);$('#f').textContent=r;"
    "$('#log').textContent='fib(25) took '+(performance.now()-t0).toFixed(0)+' ms. Cookies: '+document.cookie+'\\nclass syntax: '+(class A{static x=()=>'ok'}).x()+', async: '+(async()=>1)().constructor.name;"
    "</script></body></html>";

static void web_process_nav(nwe_req* r);
static void web_info(nwe_req* r){ g_doch=r->doch; g_scrolly=r->scroll; g_viewh=r->viewh; if(r->title[0]){ int t=0; while(r->title[t]&&t<127){g_title[t]=r->title[t];t++;} g_title[t]=0; } }
static void content_rect(void){
    int i=W_BROWSER; int x=WN[i].x+8, w=WN[i].w-16;
    int tsy=WN[i].y+TITLEH+6, tsh=26; int y=tsy+tsh+6; int cy=y+36;
    g_cx=x; g_cy=cy; g_cw=w; g_ch=WN[i].y+WN[i].h-cy-22; if(g_ch<40)g_ch=40; if(g_cw<80)g_cw=80;
}
static void web_load_html(const char* url,const char* html,int len,int istext){
    if(!web_init()){ nwk_status("Web engine could not start (out of memory)"); return; }
    content_rect();
    nwe_req* r=&g_req; for(unsigned k=0;k<sizeof(*r);k++)((u8*)r)[k]=0;
    r->url=url; r->data=html; r->len=len; r->w=g_cw; r->h=g_ch; r->btn=istext?1:0;
    g_lastvw=g_cw; g_lastvh=g_ch;
    if(WN[W_BROWSER].open)compose_present();
    { extern int g_web_faults; if(g_web_faults>32)g_web_faults=32; }
    if(web_call(NWE_OP_LOAD,r)>=0&&!(url&&url[0]=='a'&&url[6]=='c'&&url[11]=='e'))g_crash_shown=0;
    g_haspage=1; g_page=3; g_isimg=0; g_findn=-1;
    g_title[0]=0; web_info(r);
    web_process_nav(r);
}

static void browser_error(const char*msg,int code){
    hb b={0,0,0}; hb_put(&b,"<!doctype html><html><head><title>Can't reach this page</title>"); hb_put(&b,PAGE_CSS);
    hb_put(&b,"</head><body><main><h1>Can't reach this page</h1><div class=card><p>"); hb_esc(&b,msg); hb_put(&b,"</p><p class=muted>Address: "); hb_esc(&b,g_url);
    if(code){ char t[16],*q=t; *q++=' '; *q++='('; q=udec(q,(u32)code); *q++=')'; *q=0; hb_put(&b,t); }
    hb_put(&b,"</p><p class=muted>Check the address and the network connection. The <code>netlog</code> command in Terminal shows details.</p><p><a class=btn href=\"");
    hb_esc(&b,g_url); hb_put(&b,"\">Try again</a></p></div></main></body></html>");
    show_page("about:error",&b,"Can't reach this page");
}
void browser_show_html(const char* html,int len,const char* title){
    g_urlfocus=0; g_page=3; g_isimg=0;
    web_load_html("about:local",html,len,0);
    if(title){ int t=0; while(title[t]&&t<127){g_title[t]=title[t];t++;} g_title[t]=0; }
    sset(web_stat,(char*)(title?title:"local"));
    open_win(W_BROWSER);
}
static void dl_name_from_url(const char*url,char*out){
    int end=0; for(int i=0;url[i];i++){ if(url[i]=='?'||url[i]=='#'){ end=i; break; } end=i+1; }
    int slash=-1; for(int i=0;i<end;i++) if(url[i]=='/') slash=i;
    int st=slash+1, n=0; for(int i=st;i<end&&n<40;i++){ char c=url[i]; if(c==':'||c=='\\'||c=='"')continue; out[n++]=c; }
    out[n]=0; if(n==0)sset(out,"download");
}
static int has_ext(const char*url,const char*ext){
    int end=0; for(int i=0;url[i];i++){ if(url[i]=='?'||url[i]=='#'){ end=i; break; } end=i+1; }
    int dot=-1; for(int i=0;i<end;i++) if(url[i]=='.') dot=i; if(dot<0)return 0;
    int j=dot+1; for(int k=0;ext[k];k++,j++){ if(j>=end)return 0; if(!ci_eq(url[j],ext[k]))return 0; }
    return j==end;
}
static int is_dl_ext(const char*url){
    static const char*E[]={"zip","gz","tgz","tar","rar","7z","pdf","bin","iso","img","exe","msi","dmg","deb","rpm","apk",
        "mp3","m4a","wav","flac","ogg","mp4","mkv","mov","avi","doc","docx","xls","xlsx","ppt","pptx","csv","epub","xz","zst","bz2",0};
    for(int i=0;E[i];i++) if(has_ext(url,E[i]))return 1; return 0;
}
static void save_download(const char* url,const u8* data,int len,const char* cdname){
    char dn[48];
    if(cdname&&cdname[0]){ int i=0; for(;cdname[i]&&i<47;i++)dn[i]=cdname[i]; dn[i]=0; } else dl_name_from_url(url,dn);
    int hasdot=0; for(int i=0;dn[i];i++) if(dn[i]=='.')hasdot=1;
    if(!hasdot&&len>=4){ const char*ext=0;
        if(data[0]==0x89&&data[1]==0x50)ext=".png"; else if(data[0]==0xFF&&data[1]==0xD8)ext=".jpg"; else if(data[0]==0x50&&data[1]==0x4B)ext=".zip"; else if(data[0]=='G'&&data[1]=='I')ext=".gif"; else if(data[0]=='%'&&data[1]=='P')ext=".pdf";
        if(ext){ int L=slen(dn); for(int j=0;ext[j]&&L<47;j++)dn[L++]=ext[j]; dn[L]=0; } }
    int slot=fs_save_bin(dn,data,len,1);
    if(slot>=0){ char b[120],*q=b; q=scopy(q,"Downloaded "); q=scopy(q,dn); q=scopy(q," ("); q=udec(q,(u32)len); q=scopy(q," bytes) to Downloads"); *q=0; sset(web_stat,b); toast("Download complete"); }
    else sset(web_stat,"Download failed (disk full)");
}
static void browser_download(const char*url){
    nwk_resp r; char b[160],*q=b; q=scopy(q,"Downloading "); q=scopy(q,(char*)url); *q=0; nwk_status(b); compose_present();
    if(nwk_fetch(url,"GET",0,0,0,g_cururl,&r)<0||!r.body){ sset(web_stat,"Download failed - run netlog for detail"); return; }
    save_download(url,(const u8*)r.body,r.len,0); nwk_free(r.body);
}
static int looks_url(const char*s){
    if(!s[0])return 1;
    if((s[0]|32)=='h'&&(s[1]|32)=='t'&&(s[2]|32)=='t'&&(s[3]|32)=='p')return 1;
    if((s[0]|32)=='a'&&(s[1]|32)=='b'&&(s[2]|32)=='o'&&(s[3]|32)=='u'&&(s[4]|32)=='t'&&s[5]==':')return 1;
    int hasdot=0,hasspace=0; for(int i=0;s[i];i++){ if(s[i]=='.')hasdot=1; if(s[i]==' ')hasspace=1; }
    if(hasspace)return 0;
    if(!hasdot){ if(seq(s,"localhost"))return 1; for(int i=0;s[i];i++) if(s[i]==':'||s[i]=='/') return 1; return 0; }
    return 1;
}
static void urlencode(const char*s,char*out,int cap){ int o=0; const char* hx="0123456789ABCDEF";
    for(int i=0;s[i]&&o<cap-4;i++){ unsigned char ch=(unsigned char)s[i];
        if((ch>='a'&&ch<='z')||(ch>='A'&&ch<='Z')||(ch>='0'&&ch<='9')||ch=='-'||ch=='_'||ch=='.'||ch=='~')out[o++]=(char)ch;
        else if(ch==' ')out[o++]='+';
        else { out[o++]='%'; out[o++]=hx[ch>>4]; out[o++]=hx[ch&15]; } }
    out[o]=0;
}
static int istarts_(const char* s,const char* p){ while(*p){ if(!ci_eq(*s,*p))return 0; s++; p++; } return 1; }
static int stristr(const char*h,const char*needle){ for(int i=0;h[i];i++){ int k=0; while(needle[k]&&ci_eq(h[i+k],needle[k]))k++; if(!needle[k])return 1; } return 0; }
static void host_of(const char* u,char* out){ const char* p=u; while(*p&&*p!=':')p++; if(*p==':')p++; while(*p=='/')p++; int n=0; while(*p&&*p!='/'&&*p!='?'&&*p!='#'&&*p!=':'&&n<255)out[n++]=*p++; out[n]=0; }


static const char YT_JS[]=
"<div class=card><p id=s>Loading the video from YouTube...</p><video id=v controls autoplay style=\"width:100%;max-width:960px;background:#000\"></video><h2 id=t></h2><p class=muted id=a></p><p class=muted id=q></p></div>"
"<script>(async function(){var id=document.body.getAttribute('data-v');var S=document.getElementById('s');var last='no playable format';"
"var C=[{clientName:'ANDROID_VR',clientVersion:'1.60.19',deviceMake:'Oculus',deviceModel:'Quest 3',androidSdkVersion:32,osName:'Android',osVersion:'12L',hl:'en',gl:'US'},"
"{clientName:'IOS',clientVersion:'20.10.4',deviceMake:'Apple',deviceModel:'iPhone16,2',osName:'iPhone',osVersion:'18.3.2.22D82',hl:'en',gl:'US'},"
"{clientName:'TVHTML5_SIMPLY_EMBEDDED_PLAYER',clientVersion:'2.0',hl:'en',gl:'US'},{clientName:'WEB_EMBEDDED_PLAYER',clientVersion:'1.20250310.01.00',hl:'en',gl:'US'}];"
"for(var i=0;i<C.length;i++){try{S.textContent='Asking YouTube for the video ('+C[i].clientName+')...';"
"var r=await fetch('https://www.youtube.com/youtubei/v1/player?prettyPrint=false',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify({videoId:id,context:{client:C[i]},contentCheckOk:true,racyCheckOk:true})});"
"var j=await r.json();var ps=j.playabilityStatus||{};var sd=j.streamingData;if(!sd){last=ps.reason||ps.status||'no streams';continue;}"
"var f=(sd.formats||[]).filter(function(x){return x.url&&/mp4|webm/.test(x.mimeType||'')&&!/av01/.test(x.mimeType||'');});"
"f.sort(function(a,b){return (b.height||0)-(a.height||0);});var ok=f.filter(function(x){return (x.height||0)<=720;});var pick=ok[0]||f[0];"
"if(!pick){last='only adaptive or protected streams';continue;}"
"var d=j.videoDetails||{};document.getElementById('t').textContent=d.title||'';document.getElementById('a').textContent=(d.author||'')+(d.viewCount?' - '+Number(d.viewCount).toLocaleString()+' views':'');"
"document.title=(d.title||'YouTube')+' - YouTube';document.getElementById('q').textContent=(pick.qualityLabel||'')+' '+(pick.mimeType||'').split(';')[0]+' via '+C[i].clientName;"
"var v=document.getElementById('v');v.src=pick.url;v.play&&v.play();S.textContent='';return;}catch(e){last=String(e&&e.message||e);}}"
"S.textContent='This video could not be played here: '+last+'. YouTube may require sign-in or a newer client for it.';})();</script>";
static const char YT_CSS[]="<style>body{font-family:Arial,sans-serif;margin:0;background:#fff;color:#0f0f0f}.yh{display:flex;align-items:center;gap:16px;padding:10px 20px;border-bottom:1px solid #e5e5e5;background:#fff}.yl{display:flex;align-items:center;gap:6px;font-size:20px;font-weight:bold;color:#0f0f0f;text-decoration:none}.yp{display:inline-block;width:30px;height:21px;background:#f00;border-radius:6px;color:#fff;font-size:12px;line-height:21px;text-align:center}.yf{display:flex;flex:1;max-width:600px}.yf input{flex:1;padding:8px 14px;border:1px solid #ccc;border-radius:20px 0 0 20px;font-size:15px}.yf button{padding:8px 18px;border:1px solid #ccc;border-left:0;border-radius:0 20px 20px 0;background:#f8f8f8;font-size:14px}.ym{padding:16px 20px}.ys{color:#606060;margin:4px 0 14px}.yg{display:flex;flex-wrap:wrap;gap:18px}.vid{display:block;width:320px;color:#0f0f0f;text-decoration:none}.th{position:relative;width:320px;height:180px;background:#ddd;border-radius:10px;overflow:hidden}.th img{display:block;width:320px;height:180px}.len{position:absolute;right:6px;bottom:6px;background:rgba(0,0,0,.8);color:#fff;font-size:12px;padding:1px 4px;border-radius:4px}.tt{font-weight:bold;font-size:14px;margin:8px 0 4px;line-height:19px;max-height:38px;overflow:hidden}.mu{color:#606060;font-size:13px}.card{max-width:980px}.muted{color:#606060}</style>\n";
static const char YT_HDR[]="<div class=yh><a class=yl href=\"https://www.youtube.com/\"><span class=yp>&#9654;</span>YouTube</a><form class=yf id=yf action=\"https://www.youtube.com/results\" method=get><input id=yq name=search_query placeholder=\"Search\"><button>Search</button></form></div><script>(function(){var f=document.getElementById('yf'),i=document.getElementById('yq');var q=document.body.getAttribute('data-q');if(q)i.value=q;f.addEventListener('submit',function(e){e.preventDefault();var v=i.value.trim();if(v)location.href='https://www.youtube.com/results?search_query='+encodeURIComponent(v);});})();</script>\n";
static const char YT_HOME_JS[]="<div class=ym><p class=ys id=s>Loading...</p><div class=yg id=g></div></div><script>(async function(){\n"
"var q=document.body.getAttribute('data-q')||'';\n"
"var S=document.getElementById('s'),G=document.getElementById('g');\n"
"var W={clientName:'WEB',clientVersion:'2.20250312.04.00',hl:'en',gl:'US'};\n"
"function esc(s){return String(s==null?'':s).replace(/&/g,'&amp;').replace(/</g,'&lt;').replace(/>/g,'&gt;').replace(/\"/g,'&quot;');}\n"
"function txt(o){if(!o)return '';if(typeof o=='string')return o;if(o.simpleText)return o.simpleText;if(o.content)return o.content;if(o.runs)return o.runs.map(function(r){return r.text;}).join('');return '';}\n"
"function collect(o,out,seen,d){if(!o||typeof o!='object'||d>60)return;if(Array.isArray(o)){for(var i=0;i<o.length;i++)collect(o[i],out,seen,d+1);return;}\n"
"var v=o.videoRenderer||o.gridVideoRenderer||o.compactVideoRenderer;\n"
"if(v&&v.videoId){if(!seen[v.videoId]){seen[v.videoId]=1;out.push({id:v.videoId,t:txt(v.title),c:txt(v.ownerText||v.longBylineText||v.shortBylineText),v:txt(v.shortViewCountText||v.viewCountText),l:txt(v.lengthText),p:txt(v.publishedTimeText)});}return;}\n"
"var L=o.lockupViewModel;if(L&&L.contentId&&/VIDEO/.test(L.contentType||'')){if(!seen[L.contentId]){seen[L.contentId]=1;var m=(L.metadata&&L.metadata.lockupMetadataViewModel)||{};var rows=[];try{(m.metadata.contentMetadataViewModel.metadataRows||[]).forEach(function(r){(r.metadataParts||[]).forEach(function(p){rows.push(txt(p.text));});});}catch(e){}\n"
"out.push({id:L.contentId,t:txt(m.title),c:rows[0]||'',v:rows.slice(1).join(' - '),l:'',p:''});}return;}\n"
"var R=o.reelItemRenderer||o.shortsLockupViewModel;if(R){var rid=R.videoId||(R.onTap&&R.onTap.innertubeCommand&&R.onTap.innertubeCommand.reelWatchEndpoint&&R.onTap.innertubeCommand.reelWatchEndpoint.videoId);if(rid&&!seen[rid]){seen[rid]=1;out.push({id:rid,t:txt(R.headline||(R.overlayMetadata&&R.overlayMetadata.primaryText)),c:'Shorts',v:txt(R.viewCountText||(R.overlayMetadata&&R.overlayMetadata.secondaryText)),l:'',p:''});}return;}\n"
"for(var k in o)collect(o[k],out,seen,d+1);}\n"
"async function api(ep,body){body.context={client:W};var r=await fetch('https://www.youtube.com/youtubei/v1/'+ep+'?prettyPrint=false',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify(body)});return await r.json();}\n"
"var list=[],seen={},err='';\n"
"try{\n"
"if(q){S.textContent='Searching for \"'+q+'\"...';collect(await api('search',{query:q}),list,seen,0);}\n"
"else{S.textContent='Loading videos...';try{collect(await api('browse',{browseId:'FEwhat_to_watch'}),list,seen,0);}catch(e){err=String(e&&e.message||e);}\n"
"if(list.length<8){try{collect(await api('browse',{browseId:'FEtrending'}),list,seen,0);}catch(e){}}\n"
"if(list.length<8){collect(await api('search',{query:'popular videos today'}),list,seen,0);}}\n"
"}catch(e){err=String(e&&e.message||e);}\n"
"if(!list.length){S.textContent='No videos could be loaded'+(err?': '+err:'')+'.';return;}\n"
"S.textContent=q?list.length+' results for \"'+q+'\"':'Recommended';\n"
"var h='';for(var i=0;i<list.length&&i<60;i++){var x=list[i];var meta=[x.v,x.p].filter(function(s){return s;}).join(' - ');\n"
"h+='<a class=vid href=\"https://www.youtube.com/watch?v='+esc(x.id)+'\"><div class=th><img src=\"https://i.ytimg.com/vi/'+esc(x.id)+'/mqdefault.jpg\" width=320 height=180 alt=\"\">'+(x.l?'<span class=len>'+esc(x.l)+'</span>':'')+'</div><div class=tt>'+esc(x.t)+'</div><div class=mu>'+esc(x.c)+'</div><div class=mu>'+esc(meta)+'</div></a>';}\n"
"G.innerHTML=h;\n"
"})();</script>\n";
static int yt_idch(char c){ return (c>='a'&&c<='z')||(c>='A'&&c<='Z')||(c>='0'&&c<='9')||c=='-'||c=='_'; }
__attribute__((unused)) static int yt_try(const char* full){
    const char* h=full; if(istarts_(h,"https://"))h+=8; else if(istarts_(h,"http://"))h+=7;
    const char* hs[6]={"www.youtube.com/","m.youtube.com/","youtube.com/","music.youtube.com/","youtu.be/","www.youtube-nocookie.com/"};
    int which=-1; for(int i=0;i<6;i++) if(istarts_(h,hs[i])){ which=i; h+=slen(hs[i]); break; } if(which<0)return 0;
    char id[16]; int n=0; const char* q=0;
    if(which==4)q=h; else if(istarts_(h,"watch")){ const char* a=h; while(*a&&*a!='#'){ if((*a=='?'||*a=='&')&&a[1]=='v'&&a[2]=='='){ q=a+3; break; } a++; } }
    else if(istarts_(h,"shorts/"))q=h+7; else if(istarts_(h,"embed/"))q=h+6; else if(istarts_(h,"live/"))q=h+5;
    if(!q){ if(which==4)return 0; int home=(!*h||*h=='?'||*h=='#'||istarts_(h,"results")||istarts_(h,"feed")||istarts_(h,"index")); if(!home)return 0;
        char qs[256]; int qn=0; const char* a=h; while(*a&&*a!='#'){ if((*a=='?'||*a=='&')&&istarts_(a+1,"search_query=")){ a+=14; while(*a&&*a!='&'&&*a!='#'&&qn<250){ char c=*a++; if(c=='+')c=' '; else if(c=='%'&&a[0]&&a[1]){ int v=0; for(int k=0;k<2;k++){ char d=a[k]; v=v*16+(d>='0'&&d<='9'?d-'0':(d|32)>='a'&&(d|32)<='f'?(d|32)-'a'+10:0); } a+=2; c=(char)v; } if((unsigned char)c>=32)qs[qn++]=c; } break; } a++; } qs[qn]=0;
        hb b={0,0,0}; hb_put(&b,"<!doctype html><html><head><title>YouTube</title>"); hb_put(&b,YT_CSS); hb_put(&b,"</head><body data-q=\""); hb_esc(&b,qs); hb_put(&b,"\">"); hb_put(&b,YT_HDR); hb_put(&b,YT_HOME_JS); hb_put(&b,"</body></html>");
        show_page("about:youtube",&b,qn?"YouTube search":"YouTube"); return 1; }
    while(yt_idch(q[n])&&n<15){ id[n]=q[n]; n++; } id[n]=0; if(n<6)return 0;
    hb b={0,0,0}; hb_put(&b,"<!doctype html><html><head><title>YouTube</title>"); hb_put(&b,YT_CSS); hb_put(&b,"</head><body data-v=\""); hb_put(&b,id); hb_put(&b,"\">"); hb_put(&b,YT_HDR); hb_put(&b,"<div class=ym>");
    hb_put(&b,YT_JS); hb_put(&b,"</div></body></html>");
    show_page("about:youtube",&b,"YouTube"); return 1; }
static void about_page(const char* p){
    if(istarts_(p,"blank")){ web_load_html("about:blank","<html><body></body></html>",26,0); sset(g_title,"about:blank"); return; }
    if(istarts_(p,"js")||istarts_(p,"engine")||istarts_(p,"demo")){ web_load_html("about:js",JS_DEMO,(int)sizeof(JS_DEMO)-1,0); return; }
    if(istarts_(p,"cookies")){ const char* q=p+7; if(istarts_(q,"?clear")){ cookie_clear(); webstore_clear(0,0); webstore_clear(0,1); toast("Cookies and site data cleared"); } else if(istarts_(q,"?del=")) cookie_delete_domain(q+5); show_cookies(); sset(g_cururl,"about:cookies"); return; }
    if(istarts_(p,"console")){ show_console(); return; }
    if(istarts_(p,"history")){ show_history(); return; }
    if(istarts_(p,"bookmarks")){ show_bookmarks(); return; }
    if(istarts_(p,"crash")){ web_load_html("about:crash","",0,0); return; }
    web_load_html("about:blank","<html><body><h1>Unknown about: page</h1></body></html>",52,0);
}

void browser_go(int push){
    while(g_url[0]==' '){ int i=0; for(;g_url[i];i++)g_url[i]=g_url[i+1]; }
    g_urllen=slen(g_url);
    if(istarts_(g_url,"about:")){
        if(push && g_cururl[0] && !seq(g_cururl,g_url)){ if(backn<16)sset(backstk[backn++],g_cururl); else { for(int i=1;i<16;i++)sset(backstk[i-1],backstk[i]); sset(backstk[15],g_cururl); } }
        g_urlfocus=0; g_urlsel=1; sset(g_cururl,g_url); about_page(g_url+6); return; }
    if(istarts_(g_url,"javascript:")) return;
    if(istarts_(g_url,"data:")){ const char* c=g_url+5; int b64=0; while(*c&&*c!=','){ if(istarts_(c,";base64"))b64=1; c++; } if(*c==',')c++;
        int n=slen(c); char* out=(char*)kmalloc((unsigned long)n+1); if(!out)return; int o=0;
        if(b64){ u32 acc=0; int bits=0; for(int i=0;i<n;i++){ char ch=c[i]; int v=ch>='A'&&ch<='Z'?ch-'A':ch>='a'&&ch<='z'?ch-'a'+26:ch>='0'&&ch<='9'?ch-'0'+52:ch=='+'||ch=='-'?62:ch=='/'||ch=='_'?63:-1; if(v<0)continue; acc=(acc<<6)|(u32)v; bits+=6; if(bits>=8){ bits-=8; out[o++]=(char)((acc>>bits)&0xFF); } } }
        else for(int i=0;i<n;i++){ char ch=c[i]; if(ch=='%'&&c[i+1]&&c[i+2]){ int v=0; for(int k=1;k<=2;k++){ char d=c[i+k]; v=v*16+(d>='0'&&d<='9'?d-'0':(d|32)>='a'&&(d|32)<='f'?(d|32)-'a'+10:0); } out[o++]=(char)v; i+=2; } else out[o++]=ch; }
        out[o]=0; if(push && g_cururl[0] && !seq(g_cururl,g_url)){ if(backn<16)sset(backstk[backn++],g_cururl); }
        g_urlfocus=0; g_urlsel=1; sset(g_cururl,g_url); g_page=3; web_load_html("about:blank",out,o,0); kfree(out); return; }
    g_urlfocus=0; g_urlsel=1; g_page=3;
    if(!looks_url(g_url)){ char q[600]; int qi=0,s2=0; while(g_url[s2]==' ')s2++; while(g_url[s2]&&qi<598)q[qi++]=g_url[s2++]; while(qi>0&&q[qi-1]==' ')qi--; q[qi]=0;
        char enc[1200]; urlencode(q,enc,sizeof(enc)); char su[1400]; char*p=scopy(su,"https://html.duckduckgo.com/html/?q="); int e=0; while(enc[e]&&(p-su)<1390)*p++=enc[e++]; *p=0; sset(g_url,su); g_urllen=slen(g_url); }
    int noscheme=!istarts_(g_url,"http://")&&!istarts_(g_url,"https://");
    static char full[2048];
    if(noscheme){ char* p=scopy(full,"https://"); int i=0; while(g_url[i]&&(p-full)<2040)*p++=g_url[i++]; *p=0; } else sset(full,g_url);
    host_of(full,g_host);
    if(!g_host[0]){ nwk_status("Enter a URL, e.g. example.com"); return; }
    if(push && g_cururl[0] && !seq(g_cururl,full)){ if(backn<16)sset(backstk[backn++],g_cururl); else { for(int i=1;i<16;i++)sset(backstk[i-1],backstk[i]); sset(backstk[15],g_cururl); } }
    { char b[300],*p=b; p=scopy(p,"Connecting to "); p=scopy(p,g_host); *p=0; nwk_status(b); }
    g_title[0]=0; compose_present();
    nwk_resp r;
    const char* meth=g_post_body?"POST":"GET";
    extern unsigned long g_fetch_ms; extern int g_fetch_n; unsigned long lt0=nwk_ms(); g_fetch_ms=0; g_fetch_n=0;
    int rc=nwk_fetch(full,meth,g_post_body?"application/x-www-form-urlencoded":0,g_post_body,g_post_len,g_cururl,&r);
    if(rc<0 && noscheme){ char* p=scopy(full,"http://"); int i=0; while(g_url[i]&&(p-full)<2040)*p++=g_url[i++]; *p=0; rc=nwk_fetch(full,meth,g_post_body?"application/x-www-form-urlencoded":0,g_post_body,g_post_len,g_cururl,&r); }
    if(g_post_body){ kfree(g_post_body); g_post_body=0; g_post_len=0; }
    if(rc<0||!r.body){
        const char* em; if(rc==-100)em="The site name could not be resolved (DNS lookup failed)."; else if(rc==-1)em="No network connection, or the address is not valid."; else if(rc==-2)em="Could not connect to the server."; else if(rc==-3)em="The connection timed out or the server sent nothing."; else if(rc==-5)em="The secure (TLS) connection failed."; else if(rc==-6)em="Too many redirects."; else em="The request failed.";
        sset(g_cururl,full); browser_error(em,-rc); sset(web_stat,em); return; }
    sset(g_cururl,r.final_url[0]?r.final_url:full); sset(g_url,g_cururl); g_urllen=slen(g_url); host_of(g_cururl,g_host);
    { char b[160],*p=b; p=scopy(p,"HTTP "); p=udec(p,(u32)r.status); p=scopy(p,"  "); int i=0; while(r.ctype[i]&&i<60)*p++=r.ctype[i++]; *p=0; nwk_status(b); }
    const u8* body=(const u8*)r.body; int blen=r.len;
    if(stristr(r.ctype,"attachment")||(!stristr(r.ctype,"html")&&!stristr(r.ctype,"text")&&!stristr(r.ctype,"xml")&&!stristr(r.ctype,"json")&&!stristr(r.ctype,"javascript")&&!looks_image(body,blen)&&r.ctype[0])||is_dl_ext(g_cururl)){
        if(!looks_image(body,blen)){ save_download(g_cururl,body,blen,0); hist_push(g_cururl); nwk_free(r.body); return; } }
    if(looks_image(body,blen)&&!stristr(r.ctype,"html")){
        if(decode_image(body,blen)){ g_isimg=1; g_page=3; g_haspage=0; char b[48],*p=b; p=scopy(p,"Image  "); p=udec(p,(u32)img_w);*p++='x';p=udec(p,(u32)img_h); *p=0; sset(web_stat,b); host_of(g_cururl,g_title); }
        else { browser_error("The image could not be decoded.",0); }
        hist_push(g_cururl); nwk_free(r.body); return; }
    if(g_lastdoc){ kfree(g_lastdoc); g_lastdoc=0; }
    g_lastdoc=(char*)kmalloc((unsigned long)blen+1); if(g_lastdoc){ for(int i=0;i<blen;i++)g_lastdoc[i]=(char)body[i]; g_lastdoc[blen]=0; g_lastdoclen=blen; }
    int istext=r.ctype[0]&&!stristr(r.ctype,"html")&&!stristr(r.ctype,"xml");
    if(!r.ctype[0]){ int i=0; while(i<blen&&(body[i]==' '||body[i]=='\n'||body[i]=='\r'||body[i]=='\t'))i++; istext=!(i<blen&&body[i]=='<'); }
    unsigned long lt1=nwk_ms(), mainf=g_fetch_ms; g_fetch_ms=0; g_fetch_n=0;
    web_load_html(g_cururl,(const char*)body,blen,istext);
    { unsigned long lt2=nwk_ms(); char b[200],*p=b; p=scopy(p,"Loaded in "); p=udec(p,(u32)(lt2-lt0)); p=scopy(p," ms: page "); p=udec(p,(u32)(lt1-lt0)); p=scopy(p," ms, "); p=udec(p,(u32)g_fetch_n); p=scopy(p," files "); p=udec(p,(u32)g_fetch_ms); p=scopy(p," ms, engine "); u32 eng=(u32)((lt2-lt1)>g_fetch_ms?(lt2-lt1)-g_fetch_ms:0); p=udec(p,eng); p=scopy(p," ms"); *p=0; (void)mainf; nwk_status(b); extern void web_console_add(const char*); web_console_add(b); klog(b); }
    nwk_free(r.body);
    hist_push(g_cururl);
    if(!g_title[0]) sset(g_title,g_host);
    if(r.status>=400){ char b[80],*p=b; p=scopy(p,"HTTP error "); p=udec(p,(u32)r.status); *p=0; nwk_status(b); }
    else nwk_status("Done");
}
void nvx_open_url(const char* u){ sset(g_url,u); g_urllen=slen(g_url); open_win(W_BROWSER); browser_go(1); }

static void web_process_nav(nwe_req* r){
    if(!r->nav[0]) return;
    static char nav[1024]; int i=0; for(;r->nav[i]&&i<1023;i++)nav[i]=r->nav[i]; nav[i]=0;
    if(r->navmethod[0]=='P'&&r->navbody){ g_post_body=(char*)kmalloc((unsigned long)r->navblen+1); if(g_post_body){ for(int k=0;k<r->navblen;k++)g_post_body[k]=r->navbody[k]; g_post_body[r->navblen]=0; g_post_len=r->navblen; } }
    r->nav[0]=0;
    if(is_dl_ext(nav)&&!g_post_body){ browser_download(nav); return; }
    sset(g_url,nav); g_urllen=slen(g_url); browser_go(1);
}

#define NTAB 6
typedef struct { int used; char url[2048]; char title[64]; int page; int isimg; int backn; char back[16][256]; } TabSt;
static TabSt tabs[NTAB]; static int g_ntab=1, g_tab=0; static int tab_x[NTAB], tab_w=172, tab_plusx, tab_savex, tab_savew;
static void save_tab(int t){ TabSt*T=&tabs[t]; T->used=1; sset(T->url,g_cururl); int i=0; for(;g_title[i]&&i<63;i++)T->title[i]=g_title[i]; T->title[i]=0; T->page=g_page; T->isimg=g_isimg; T->backn=backn; for(int k=0;k<backn&&k<16;k++)cut256(T->back[k],backstk[k]); }
static void load_tab(int t){ TabSt*T=&tabs[t]; backn=T->backn; for(int k=0;k<backn&&k<16;k++)sset(backstk[k],T->back[k]);
    if(T->page==3&&T->url[0]){ g_cururl[0]=0; sset(g_url,T->url); g_urllen=slen(g_url); browser_go(0); return; }
    g_page=T->page; g_url[0]=0; g_urllen=0; g_cururl[0]=0; g_title[0]=0; g_haspage=0; }
static void tab_switch(int t){ if(t==g_tab||t<0||t>=g_ntab)return; save_tab(g_tab); g_tab=t; load_tab(t); }
static void tab_new(void){ if(g_ntab>=NTAB)return; save_tab(g_tab); int t=g_ntab++; g_tab=t; tabs[t].used=1; tabs[t].url[0]=0; tabs[t].title[0]=0;
    g_url[0]=0; g_urllen=0; g_cururl[0]=0; g_host[0]=0; g_title[0]=0; g_isimg=0; g_page=0; backn=0; g_haspage=0; sset(web_stat,"New Tab"); }
static void tab_close(int t){ if(g_ntab<=1||t<0||t>=g_ntab)return; if(t!=g_tab)save_tab(g_tab); for(int k=t;k<g_ntab-1;k++)tabs[k]=tabs[k+1]; g_ntab--; if(g_tab>=g_ntab)g_tab=g_ntab-1; else if(g_tab>t)g_tab--; load_tab(g_tab); }
static void browser_save(void){
    if(g_page!=3||!g_lastdoc){ sset(web_stat,"Nothing to save"); return; }
    char nm[32],*p=nm; p=scopy(p,"page"); p=udec(p,(u32)(++g_dln)); sset(p,".html");
    int slot=fs_save_bin(nm,(const u8*)g_lastdoc,g_lastdoclen,1);
    if(slot>=0){ char b[64],*q=b; q=scopy(q,"Saved to Files: "); q=scopy(q,nm); *q=0; sset(web_stat,b); }
    else sset(web_stat,"Save failed (disk full)");
}

static void web_paint(int x,int y,int w,int h){
    if(!g_haspage) return;
    if(web_busy()){ fill_rect(x,y,w,h,rgb(255,255,255)); pdstr(x+16,y+16,"Loading...",rgb(120,130,150),1); return; }
    nwe_req* r=&g_req;
    if(w!=g_lastvw||h!=g_lastvh){ for(unsigned k=0;k<sizeof(*r);k++)((u8*)r)[k]=0; r->w=w; r->h=h; web_call(NWE_OP_LAYOUT,r); g_lastvw=w; g_lastvh=h; web_info(r); }
    for(unsigned k=0;k<sizeof(*r);k++)((u8*)r)[k]=0;
    r->fb=SCENE; r->stride=(int)SW; r->fbw=(int)W; r->fbh=(int)H; r->x=x; r->y=y; r->w=w; r->h=h;
    long rc=web_call(NWE_OP_PAINT,r);
    if(rc==-1){ fill_rect(x,y,w,h,rgb(255,255,255)); pdstr(x+16,y+16,"This page stopped working (engine error). Reload to try again.",rgb(160,60,60),1); g_haspage=0; return; }
    web_info(r);
    if(g_doch>h){ int tot=h-4; int sh=tot*h/g_doch; if(sh<24)sh=24; int rng=g_doch-h; int sy=y+2+(rng>0?(tot-sh)*g_scrolly/rng:0);
        fill_round(x+w-7,y+2,5,tot,2,15,rgb(232,234,240)); fill_round(x+w-7,sy,5,sh,2,15,rgb(170,176,192)); }
}

void browser_draw(int i){
    win_frame(i,"Browser"); if(!g_gm_ready)gmetrics();
    int x=WN[i].x+12, w=WN[i].w-24;
    int tsy=WN[i].y+TITLEH+6, tsh=26;
    tab_w=(w-30)/g_ntab; if(tab_w>172)tab_w=172; if(tab_w<70)tab_w=70;
    for(int k=0;k<g_ntab;k++){ int tx=x+k*tab_w; tab_x[k]=tx; int act=(k==g_tab);
        fill_round(tx,tsy,tab_w-4,tsh,7,15, act?rgb(255,255,255):rgb(226,230,240));
        const char* ti=(k==g_tab)?(g_title[0]?g_title:(g_host[0]?g_host:(g_page==0?"New Tab":"Loading"))):(tabs[k].title[0]?tabs[k].title:(tabs[k].url[0]?tabs[k].url:"New Tab"));
        char tt[24]; int tn=0; for(int q=0;ti[q]&&tn<19;q++)tt[tn++]=ti[q]; tt[tn]=0;
        pdstr(tx+10,tsy+6,tt, act?INK:rgb(90,98,116),1);
        if(g_ntab>1){ int cxk=tx+tab_w-16,cyk=tsy+tsh/2; u32 xc=rgb(150,158,174); for(int t=-3;t<=3;t++){putpx(cxk+t,cyk+t,xc);putpx(cxk+t,cyk-t,xc);} } }
    tab_plusx=x+g_ntab*tab_w+2; if(g_ntab<NTAB){ fill_round(tab_plusx,tsy,24,tsh,7,15,rgb(226,230,240)); dstr_ctr(tab_plusx+12,tsy+5,"+",INK,2); }
    int y=tsy+tsh+6;
    br_bkx=x; br_bkw=30; fill_round(br_bkx,y,br_bkw,28,6,15, backn>0?rgb(226,230,240):rgb(240,242,247));
    { int a=br_bkx+18,b=y+14; u32 c=backn>0?INK:rgb(184,190,202); for(int t=-5;t<=5;t++)putpx(a+t,b,c); for(int t=0;t<5;t++){putpx(a-5+t,b-t,c);putpx(a-5+t,b+t,c);} }
    br_rlx=br_bkx+br_bkw+6; br_rlw=30; fill_round(br_rlx,y,br_rlw,28,6,15,rgb(240,242,247));
    { int a=br_rlx+15,b=y+14; u32 c=rgb(70,78,96); for(int t=-4;t<=4;t++){putpx(a+t,b-6,c);} for(int t=0;t<5;t++){putpx(a+4-t,b-6+t,c);} for(int t=-4;t<=4;t++){putpx(a-4,b+t,c);putpx(a+4,b+t,c);putpx(a+t,b+4,c);} }
    g_starx=br_rlx+br_rlw+6; g_starw=30; g_stary=y; int marked=(g_cururl[0]&&bm_has(g_cururl));
    fill_round(g_starx,y,g_starw,28,6,15, marked?mix(ACC,rgb(255,255,255),150):rgb(240,242,247));
    { int scx=g_starx+15, scy=y+14; u32 sc=marked?ACC:rgb(150,158,174); for(int r=-5;r<=5;r++)for(int c=-5;c<=5;c++){ int a=(c<0?-c:c),b=(r<0?-r:r); if(a+b<=5)putpx(scx+c,scy+r,sc); } }
    int abx=g_starx+g_starw+8; int barw=w-(abx-x)-310; if(barw<120)barw=120; br_barx=abx; br_bary=y; br_barw=barw;
    fill_round(abx,y,barw,28,6,15,g_urlfocus?rgb(255,255,255):rgb(232,236,244));
    if(g_urlfocus){ for(int t=0;t<28;t++){ SCENE[(u64)(y+t)*SW+abx]=ACC; SCENE[(u64)(y+t)*SW+abx+barw-1]=ACC; } }
    { int maxch=(barw-20)/8; int st=0; if(g_urllen>maxch)st=g_urllen-maxch;
      if(g_urlfocus&&g_urlsel&&g_urllen>0){ int visw=(g_urllen-st)*8; if(visw>barw-16)visw=barw-16; fill_rect(abx+9,y+6,visw+2,16,rgb(178,206,248)); }
      char vis[300]; int vn=0; for(int q=st;g_url[q]&&vn<maxch&&vn<299;q++)vis[vn++]=g_url[q]; vis[vn]=0;
      dstr(abx+10,y+7,vis,rgb(50,60,80),1);
      g_acsug[0]=0;
      if(g_urlfocus&&!g_urlsel&&g_urllen>0){ for(int hh=nhist-1;hh>=0;hh--){ int m=1; for(int q=0;q<g_urllen;q++){ if((hist[hh][q]|32)!=(g_url[q]|32)){m=0;break;} } if(m&&slen(hist[hh])>g_urllen){ sset(g_acsug,hist[hh]); break; } }
        if(g_acsug[0]){ int cxp=abx+10+(g_urllen-st)*8; int rem=(barw-20)/8-(g_urllen-st); if(rem>0){ char sg[128]; int sn=0; for(int q=g_urllen;g_acsug[q]&&sn<rem&&sn<126;q++)sg[sn++]=g_acsug[q]; sg[sn]=0; dstr(cxp,y+7,sg,rgb(170,178,192),1); } } }
      if(g_urlfocus&&!g_urlsel&&((g_ticks/50)&1)){ int cxp=abx+10+(g_urllen-st)*8; fill_rect(cxp,y+8,2,14,ACC); } }
    br_gx=abx+barw+8; br_gw=48; fill_round(br_gx,y,br_gw,28,6,15,ACC); dstr_ctr(br_gx+br_gw/2,y+7,"Go",rgb(255,255,255),1);
    tab_savex=br_gx+br_gw+6; tab_savew=52; fill_round(tab_savex,y,tab_savew,28,6,15,rgb(224,228,238)); dstr_ctr(tab_savex+tab_savew/2,y+7,"Save",INK,1);
    br_hx=tab_savex+tab_savew+6; br_hw=52; fill_round(br_hx,y,br_hw,28,6,15,rgb(224,228,238)); dstr_ctr(br_hx+br_hw/2,y+7,"Home",INK,1);
    g_findbx=br_hx+br_hw+6; g_findbw=52; g_findby=y; fill_round(g_findbx,y,g_findbw,28,6,15,g_findmode?mix(ACC,rgb(255,255,255),150):rgb(224,228,238)); dstr_ctr(g_findbx+g_findbw/2,y+7,"Find",INK,1);
    g_cookbx=g_findbx+g_findbw+6; g_cookbw=70; fill_round(g_cookbx,y,g_cookbw,28,6,15,rgb(224,228,238)); dstr_ctr(g_cookbx+g_cookbw/2,y+7,"Cookies",INK,1);
    content_rect();
    int cx=g_cx, cy=g_cy, cw=g_cw, ch=g_ch;
    lk_n=0;
    if(g_page==3 && g_isimg){
        fill_rect(cx,cy,cw,ch,rgb(246,247,250));
        int aw=cw-16, ah=ch-16, sw=img_w, sh=img_h, scn=1000;
        if(sw*scn/1000>aw) scn=aw*1000/sw; if(sh*scn/1000>ah) scn=ah*1000/sh;
        int dw=sw*scn/1000, dh=sh*scn/1000; if(dw<1)dw=1; if(dh<1)dh=1; int ox=cx+8+(aw-dw)/2, oy=cy+8+(ah-dh)/2;
        for(int r=0;r<dh;r++){ int dy=oy+r; int srcr=r*sh/dh;
            for(int c=0;c<dw;c++){ int dx=ox+c; int srcc=c*sw/dw; u8*pp=&imgpool[((srcr*sw)+srcc)*4]; u32 bgc=(((dx-ox)>>3)+((dy-oy)>>3))&1?rgb(236,238,242):rgb(255,255,255); if(pp[3]==255)putpx(dx,dy,rgb(pp[0],pp[1],pp[2])); else putpx(dx,dy,mix(bgc,rgb(pp[0],pp[1],pp[2]),pp[3])); } }
    } else if(g_page==3){
        if((mx!=g_lastmx||my!=g_lastmy)&&g_haspage&&!web_busy()&&mx>=cx&&mx<cx+cw&&my>=cy&&my<cy+ch){
            g_lastmx=mx; g_lastmy=my; nwe_req* r=&g_req; for(unsigned k=0;k<sizeof(*r);k++)((u8*)r)[k]=0; r->x=mx-cx; r->y=my-cy; web_call(NWE_OP_MOVE,r);
            int q=0; while(r->href[q]&&q<511){g_hoverhref[q]=r->href[q];q++;} g_hoverhref[q]=0; }
        else if(!(mx>=cx&&mx<cx+cw&&my>=cy&&my<cy+ch)) g_hoverhref[0]=0;
        if(g_haspage) web_paint(cx,cy,cw,ch); else fill_rect(cx,cy,cw,ch,rgb(255,255,255));
    } else if(g_page==0){
        fill_rect(cx,cy,cw,ch,rgb(255,255,255));
        int CW=cw, ccx=cx+cw/2;
        char bb[32],*bp=bb; bp=scopy(bp,"Bookmarks"); *bp=0; char hb2[32],*hp=hb2; hp=scopy(hp,"History"); *hp=0;
        { int rx=cx+cw-18; const char* tl[3]={"About",hb2,bb}; int tt[3]={1,21,20};
          for(int k=0;k<3;k++){ int w=pw_str((char*)tl[k],1); rx-=w; pdstr(rx,cy+16,(char*)tl[k],rgb(32,33,36),1); lk_tgt[lk_n]=tt[k]; lk_y[lk_n]=cy+12; lk_bx[lk_n]=rx; lk_bw[lk_n]=w; lk_bh[lk_n]=20; lk_n++; rx-=18; } }
        int ly=cy+(ch>460?ch/2-170:44);
        { const char* L="Noovex"; u32 lc[6]={rgb(66,133,244),rgb(234,67,53),rgb(251,188,5),rgb(66,133,244),rgb(52,168,83),rgb(234,67,53)}; int sz=4; int tw=0; char c1[2]={0,0};
          for(int k=0;k<6;k++){ c1[0]=L[k]; tw+=pw_str(c1,sz); } int lx=ccx-tw/2;
          for(int k=0;k<6;k++){ c1[0]=L[k]; pdstr(lx,ly,c1,lc[k],sz); pdstr(lx+1,ly,c1,lc[k],sz); lx+=pw_str(c1,sz); } }
        int pw2=560; if(pw2>CW-80)pw2=CW-80; int pxp=ccx-pw2/2, pyp=ly+92, ph=46; int hov=(mx>=pxp&&mx<pxp+pw2&&my>=pyp&&my<pyp+ph);
        if(hov)shadow_soft(pxp,pyp,pw2,ph,23);
        fill_round(pxp,pyp,pw2,ph,23,15,rgb(223,225,229)); fill_round(pxp+1,pyp+1,pw2-2,ph-2,22,15,rgb(255,255,255));
        { int gx=pxp+24, gy=pyp+ph/2; fill_circle(gx,gy-2,7,rgb(154,160,166)); fill_circle(gx,gy-2,5,rgb(255,255,255)); line_bl(gx+4,gy+3,gx+9,gy+8,rgb(154,160,166),256,2); }
        pdstr(pxp+46,pyp+15,"Search Noovex or type a URL",rgb(117,117,117),1);
        { int mx0=pxp+pw2-28, my0=pyp+ph/2; fill_round(mx0-4,my0-9,8,13,4,15,rgb(66,133,244)); fill_rect(mx0-1,my0+5,2,4,rgb(66,133,244)); }
        lk_tgt[lk_n]=30; lk_y[lk_n]=pyp; lk_bx[lk_n]=pxp; lk_bw[lk_n]=pw2; lk_bh[lk_n]=ph; lk_n++;
        { const char* bl[2]={"Noovex Search","I'm Feeling Lucky"}; int bt[2]={30,15}; int bw[2]; int gap=12, tot=0; for(int k=0;k<2;k++){ bw[k]=pw_str((char*)bl[k],1)+32; tot+=bw[k]; } tot+=gap; int bx=ccx-tot/2, by=pyp+ph+28;
          for(int k=0;k<2;k++){ int h2=(mx>=bx&&mx<bx+bw[k]&&my>=by&&my<by+36); fill_round(bx,by,bw[k],36,4,15,h2?rgb(232,234,237):rgb(248,249,250)); if(h2){ fill_rect(bx+4,by,bw[k]-8,1,rgb(218,220,224)); fill_rect(bx+4,by+35,bw[k]-8,1,rgb(218,220,224)); }
            pdstr_ctr(bx+bw[k]/2,by+11,(char*)bl[k],rgb(60,64,67),1); lk_tgt[lk_n]=bt[k]; lk_y[lk_n]=by; lk_bx[lk_n]=bx; lk_bw[lk_n]=bw[k]; lk_bh[lk_n]=36; lk_n++; bx+=bw[k]+gap; } }
        { const char* tlabel[4]={"Wikipedia","Example","Engine demo","mynexus"}; int ttgt[4]={13,10,14,11};
          u32 tcol[4]={rgb(95,99,104),rgb(52,168,83),rgb(66,133,244),rgb(234,67,53)}; char tini[4]={'W','E','J','M'};
          int nt=4, tw=112, gap=8, tot=nt*tw+(nt-1)*gap; if(tot>CW-40){ tw=(CW-40-(nt-1)*gap)/nt; tot=nt*tw+(nt-1)*gap; } int tx0=ccx-tot/2, ty=pyp+ph+96;
          if(ty+104<=cy+ch) for(int t=0;t<nt;t++){ int txx=tx0+t*(tw+gap); int h2=(mx>=txx&&mx<txx+tw&&my>=ty&&my<ty+104);
            if(h2) fill_round(txx,ty,tw,104,8,15,rgb(241,243,244));
            fill_circle(txx+tw/2,ty+36,24,rgb(241,243,244)); fill_circle(txx+tw/2,ty+36,14,tcol[t]); { char c1[2]={tini[t],0}; pdstr_ctr(txx+tw/2,ty+28,c1,rgb(255,255,255),1); }
            pdstr_ctr(txx+tw/2,ty+74,(char*)tlabel[t],rgb(32,33,36),1);
            lk_tgt[lk_n]=ttgt[t]; lk_y[lk_n]=ty; lk_bx[lk_n]=txx; lk_bw[lk_n]=tw; lk_bh[lk_n]=104; lk_n++; } }
        { int fy=cy+ch-40; if(fy>pyp+ph+210){ fill_rect(cx,fy,cw,40,rgb(242,242,242)); fill_rect(cx,fy,cw,1,rgb(218,220,224));
            if(g_nvx_signedin){ char ac[100],*ap=ac; ap=scopy(ap,"Signed in as ");ap=scopy(ap,g_nvx_user[0]?g_nvx_user:g_nvx_name); *ap=0; pdstr(cx+24,fy+13,ac,rgb(112,117,122),1); }
            else pdstr(cx+24,fy+13,"NoovexOS",rgb(112,117,122),1);
            pdstr(cx+cw-24-pw_str("Private  -  Fast  -  Built from scratch",1),fy+13,"Private  -  Fast  -  Built from scratch",rgb(112,117,122),1); } }
    } else if(g_page==1){
        fill_round(cx,cy,cw,ch,8,15,rgb(255,255,255));
        int ax=cx+18, ay=cy+16;
        dstr(ax,ay,"About",INK,3);
        pdstr(ax,ay+44,"NoovexOS 64 Browser with the Noovex Web Engine",INK,1);
        pdstr(ax,ay+66,"JavaScript: QuickJS (ES2023) with DOM, events, timers, fetch, XMLHttpRequest.",rgb(70,80,100),1);
        pdstr(ax,ay+86,"Layout: block, inline, floats, positioning, z-index, flexbox, grid, tables.",rgb(70,80,100),1);
        pdstr(ax,ay+106,"CSS: selectors, cascade, variables, calc(), media queries, gradients, shadows.",rgb(70,80,100),1);
        pdstr(ax,ay+126,"Cookies with expiry, Secure and HttpOnly; localStorage kept across reboots.",rgb(70,80,100),1);
        pdstr(ax,ay+146,"Images: PNG, JPEG, GIF, WebP, BMP, ICO and SVG. TLS 1.3 for HTTPS.",rgb(70,80,100),1);
        pdstr(ax,ay+170,"about:js  about:cookies  about:console  about:history  about:bookmarks",rgb(150,110,90),1);
        lk_y[lk_n]=ay+206; lk_tgt[lk_n]=0; lk_bx[lk_n]=ax; lk_bw[lk_n]=cw-36; lk_bh[lk_n]=16; pdstr(ax,ay+206,"< Back home",ACC,1); lk_n++;
    }
    int sby=WN[i].y+WN[i].h-20; fill_rect(WN[i].x+1,sby,WN[i].w-2,19,rgb(244,245,248)); fill_rect(WN[i].x+1,sby,WN[i].w-2,1,rgb(228,230,236));
    { const char* st=(g_page==3&&g_hoverhref[0])?g_hoverhref:web_stat; char hb3[160]; int hn=0; for(int q=0;st[q]&&hn<150;q++)hb3[hn++]=st[q]; hb3[hn]=0; pdstr(WN[i].x+10,sby+3,hb3,rgb(80,90,110),1); }
    if(g_findmode){ int fw=292, fx=x+w-fw-8, fy=cy+6; fill_round(fx,fy,fw,28,6,15,rgb(252,252,254));
        for(int t=0;t<28;t++){ SCENE[(u64)(fy+t)*SW+fx]=rgb(210,214,222); SCENE[(u64)(fy+t)*SW+fx+fw-1]=rgb(210,214,222); }
        pdstr(fx+8,fy+7,"Find:",rgb(120,128,146),1); int tx0=fx+50; pdstr(tx0,fy+7,g_findstr,INK,1);
        if((g_ticks/50)&1)fill_rect(tx0+pw_str(g_findstr,1)+1,fy+8,2,12,ACC);
        char cb[24],*p=cb; if(g_findn>0){ p=udec(p,(u32)g_findn); p=scopy(p," found"); } else if(g_findn==0) p=scopy(p,"none"); *p=0;
        pdstr(fx+fw-70,fy+7,cb,g_findn>0?rgb(90,120,90):rgb(160,110,110),1); }
}

static void web_mouse(int px,int py){
    if(!g_haspage||web_busy()) return;
    nwe_req* r=&g_req;
    for(unsigned k=0;k<sizeof(*r);k++)((u8*)r)[k]=0; r->x=px-g_cx; r->y=py-g_cy; web_call(NWE_OP_DOWN,r); web_info(r);
    if(r->nav[0]){ web_process_nav(r); return; }
    for(unsigned k=0;k<sizeof(*r);k++)((u8*)r)[k]=0; r->x=px-g_cx; r->y=py-g_cy; web_call(NWE_OP_UP,r); web_info(r);
    web_process_nav(r);
}
void browser_click(int px,int py){
    int tsy=WN[W_BROWSER].y+TITLEH+6, tsh=26;
    if(py>=tsy && py<tsy+tsh){
        if(g_ntab<NTAB && px>=tab_plusx && px<tab_plusx+24){ tab_new(); return; }
        for(int k=0;k<g_ntab;k++){ int tx=tab_x[k]; if(px>=tx && px<tx+tab_w-4){ if(g_ntab>1 && px>=tx+tab_w-24){ tab_close(k); return; } tab_switch(k); return; } }
        return; }
    if(in_rect(px,py,g_starx,g_stary,g_starw,28)){ if(g_cururl[0])bm_toggle(g_cururl); return; }
    if(in_rect(px,py,g_findbx,g_findby,g_findbw,28)){ g_findmode=!g_findmode; g_urlfocus=0; g_findn=-1; return; }
    if(in_rect(px,py,g_cookbx,g_findby,g_cookbw,28)){ sset(g_url,"about:cookies"); g_urllen=slen(g_url); browser_go(1); return; }
    if(in_rect(px,py,tab_savex,br_bary,tab_savew,28)){ browser_save(); return; }
    if(in_rect(px,py,br_bkx,br_bary,br_bkw,28)){ if(backn>0){ sset(g_url,backstk[--backn]); g_urllen=slen(g_url); browser_go(0); } return; }
    if(in_rect(px,py,br_rlx,br_bary,br_rlw,28)){ if(g_cururl[0]){ sset(g_url,g_cururl); g_urllen=slen(g_url);} browser_go(0); return; }
    if(in_rect(px,py,br_barx,br_bary,br_barw,28)){ g_urlfocus=1; g_urlsel=1; return; }
    if(in_rect(px,py,br_gx,br_bary,br_gw,28)){ browser_go(1); return; }
    if(in_rect(px,py,br_hx,br_bary,br_hw,28)){ g_page=0; g_urlfocus=0; g_isimg=0; return; }
    g_urlfocus=0;
    for(int k=0;k<lk_n;k++){ if(px>=lk_bx[k]&&px<lk_bx[k]+lk_bw[k]&&py>=lk_y[k]&&py<lk_y[k]+lk_bh[k]){
        int t=lk_tgt[k];
        if(t==10){ sset(g_url,"example.com"); } else if(t==11){ sset(g_url,"mynexus.site"); } else if(t==13){ sset(g_url,"https://en.wikipedia.org/"); } else if(t==14){ sset(g_url,"about:js"); }
        else if(t==15){ sset(g_url,"https://en.wikipedia.org/wiki/Special:Random"); }
        else if(t==20){ sset(g_url,"about:bookmarks"); } else if(t==21){ sset(g_url,"about:history"); }
        else if(t==30){ g_urlfocus=1; g_urlsel=1; g_url[0]=0; g_urllen=0; return; }
        else if(t==1){ g_page=1; return; } else if(t==0){ g_page=0; return; }
        g_urllen=slen(g_url); browser_go(1); return; } }
    if(g_page==3 && !g_isimg && px>=g_cx && px<g_cx+g_cw && py>=g_cy && py<g_cy+g_ch) web_mouse(px,py);
}
void browser_wheel(int sc){
    if(g_page!=3||!g_haspage||web_busy()) return;
    nwe_req* r=&g_req; for(unsigned k=0;k<sizeof(*r);k++)((u8*)r)[k]=0;
    r->x=mx-g_cx; r->y=my-g_cy; r->key=-sc*60; web_call(NWE_OP_WHEEL,r); web_info(r);
}
void browser_media_pause(void){
    if(g_page!=3||!g_haspage||web_busy()) return;
    nwe_req* r=&g_req; for(unsigned k=0;k<sizeof(*r);k++)((u8*)r)[k]=0;
    r->data="document.querySelectorAll('video,audio').forEach(function(e){e.pause()})"; web_call(NWE_OP_EVAL,r);
}
int browser_media_fast(void){
    extern int desk_win_exposed(int i); extern void present_rect(int x,int y,int w,int h);
    if(!WN[W_BROWSER].open||WN[W_BROWSER].min||g_page!=3||!g_haspage||g_isimg||web_busy()) return 0;
    if(!desk_win_exposed(W_BROWSER)) return 0;
    nwe_req* r=&g_req; for(unsigned k=0;k<sizeof(*r);k++)((u8*)r)[k]=0;
    r->fb=SCENE; r->stride=(int)SW; r->fbw=(int)W; r->fbh=(int)H;
    web_call(NWE_OP_MEDIA,r);
    if(r->ret<0) return 1;
    if(r->ret>0) present_rect(r->x,r->y,r->w,r->h);
    return 0;
}

static void browser_crashed(void){
    extern int g_web_failed; g_web_failed=0; if(g_crash_shown>=3)return; g_crash_shown++;
    hb b={0,0,0}; hb_put(&b,"<!doctype html><html><head><title>This page stopped</title>"); hb_put(&b,PAGE_CSS);
    hb_put(&b,"</head><body><main><h1>This page stopped</h1><div class=card><p>The page crashed or stopped responding, so the browser stopped it. The rest of NoovexOS is fine.</p><p class=muted>Address: "); hb_esc(&b,g_url);
    hb_put(&b,"</p><p>Press Reload to try again, or open another address.</p></div></main></body></html>");
    show_page("about:crashed",&b,"This page stopped"); }
int browser_tick(void){
    { extern int g_web_failed; if(g_web_failed)browser_crashed(); }
    if(!WN[W_BROWSER].open||WN[W_BROWSER].min) { web_tick_flush(); return 0; }
    if(g_page!=3||!g_haspage||g_isimg||web_busy()) return 0;
    if((u64)(g_ticks-g_lasttick)<2) return 0;
    g_lasttick=g_ticks;
    nwe_req* r=&g_req; for(unsigned k=0;k<sizeof(*r);k++)((u8*)r)[k]=0;
    web_call(NWE_OP_TICK,r); web_info(r);
    int d=r->ret||r->dirty;
    if(r->nav[0]){ web_process_nav(r); d=1; }
    web_tick_flush();
    return d;
}
static int map_key(int c){
    switch(c){ case 17: return NWE_K_LEFT; case 18: return NWE_K_RIGHT; case 19: return NWE_K_UP; case 20: return NWE_K_DOWN;
        case 2: return NWE_K_HOME; case 5: return NWE_K_END; case 6: return NWE_K_PGUP; case 7: return NWE_K_PGDN; case 14: return 127; }
    return (unsigned char)c;
}
void browser_key(char c){
    if(g_findmode){
        if(c=='\n'||c==27||c=='\b'||(c>=32&&c<127)){
            if(c==27){ g_findmode=0; g_findn=-1; return; }
            if(c=='\b'){ if(g_findlen>0)g_findstr[--g_findlen]=0; }
            else if(c>=32){ if(g_findlen<60){ g_findstr[g_findlen++]=c; g_findstr[g_findlen]=0; } }
            if(g_haspage&&!web_busy()){ nwe_req* r=&g_req; for(unsigned k=0;k<sizeof(*r);k++)((u8*)r)[k]=0; r->data=g_findstr; r->key=(c=='\n'); web_call(NWE_OP_FIND,r); g_findn=r->ret; web_info(r); }
        }
        return;
    }
    if(!g_urlfocus){
        if(g_page==3&&g_haspage&&!web_busy()){
            nwe_req* r=&g_req; for(unsigned k=0;k<sizeof(*r);k++)((u8*)r)[k]=0; r->key=map_key((unsigned char)c);
            web_call(NWE_OP_KEY,r); web_info(r);
            int handled=r->ret;
            if(r->nav[0]){ web_process_nav(r); return; }
            if(handled) return;
        }
        if(g_page!=3&&c>=33&&c<127){ g_urlfocus=1; g_urlsel=0; g_url[0]=c; g_url[1]=0; g_urllen=1; }
        return; }
    if(c=='\n'){ g_urlsel=0; browser_go(1); }
    else if(c=='\t'){ if(g_acsug[0]){ sset(g_url,g_acsug); g_urllen=slen(g_url); } g_urlsel=0; }
    else if(c=='\b'){ if(g_urlsel||(g_urllen>0&&seq(g_url,g_cururl))){ g_url[0]=0; g_urllen=0; g_urlsel=0; } else if(g_urllen>0)g_url[--g_urllen]=0; }
    else if(c==27){ g_urlfocus=0; if(g_cururl[0]){ sset(g_url,g_cururl); g_urllen=slen(g_url); } }
    else if(c>=32&&c<127){ if(g_urlsel||(g_urllen>0&&seq(g_url,g_cururl))){ g_url[0]=0; g_urllen=0; g_urlsel=0; } if(g_urllen<2000){ g_url[g_urllen++]=c; g_url[g_urllen]=0; } }
}
