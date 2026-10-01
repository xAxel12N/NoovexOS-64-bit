/* =========================================================================
   Browser (online: http + https, text rendering)
   ========================================================================= */
/* =========================================================================
   Image decode (PNG all common types, baseline JPEG) + multi-image pool
   ========================================================================= */
#define IMGPOOL_SZ (8*1024*1024)
static u8 imgpool[IMGPOOL_SZ]; static int imgpool_off=0;
static u8 jpeg_scratch[10u*1024u*1024u];
#define PNG_RAW    ((u8*)0x08000000u)    /* 128 MiB: PNG inflate scratch (shared w/ JPEG, non-concurrent) */
#define PNG_RAW_SZ (64*1024*1024)
/* ---- cloud filesystem: sync FS files to the signed-in Noovex account ---- */
static const char B64E[]="ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
static int b64_enc(const u8* s,int n,char* o){ int j=0; for(int i=0;i<n;i+=3){ int b0=s[i],b1=(i+1<n)?s[i+1]:0,b2=(i+2<n)?s[i+2]:0;
    o[j++]=B64E[b0>>2]; o[j++]=B64E[((b0&3)<<4)|(b1>>4)]; o[j++]=(i+1<n)?B64E[((b1&15)<<2)|(b2>>6)]:'='; o[j++]=(i+2<n)?B64E[b2&63]:'='; } o[j]=0; return j; }
static int b64_val(char c){ if(c>='A'&&c<='Z')return c-'A'; if(c>='a'&&c<='z')return c-'a'+26; if(c>='0'&&c<='9')return c-'0'+52; if(c=='+')return 62; if(c=='/')return 63; return -1; }
static int b64_dec(const char* s,int n,u8* o){ int j=0,acc=0,bits=0; for(int i=0;i<n;i++){ int v=b64_val(s[i]); if(v<0)continue; acc=(acc<<6)|v; bits+=6; if(bits>=8){ bits-=8; o[j++]=(u8)((acc>>bits)&0xFF); } } return j; }
static void nvx_file_put(const char* name,const u8* data,int len){
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
    tls_io* io=(tls_io*)0x00930000ULL; io->wr=tcps_wr; io->rd=tcps_rd; io->ctx=0; io->rlen=0; io->rpos=0;
    tls_get(io,(char*)NVX_HOST,hl,ent,req,r,http_buf,(int)sizeof(http_buf));
    secure_zero(ent,64);
}
/* pull all cloud files for the signed-in account into the FS (on login) */
static void nvx_files_pull(void){
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
#include "include/png.h"
#include "include/jpeg.h"
#include "include/nvx_assets.h"
static u8 img_fetch_buf[262144];
static int g_isimg=0, img_w=0, img_h=0;          /* direct-URL image lives at imgpool offset 0 */
#define WEB_MAXIMG 8
typedef struct { char src[256]; int line, w, h, off, ok; } WebImg;
static WebImg webimg[WEB_MAXIMG]; static int web_nimg=0;
extern int webp_decode(const u8* data,int len,u8* out,int outcap,int* ow,int* oh);
static int is_webp(const u8*d,int len){ return len>=12 && d[0]=='R'&&d[1]=='I'&&d[2]=='F'&&d[3]=='F'&&d[8]=='W'&&d[9]=='E'&&d[10]=='B'&&d[11]=='P'; }
static int looks_image(const u8*d,int len){ return len>=4 && ((d[0]==0x89&&d[1]==0x50)||(d[0]==0xFF&&d[1]==0xD8)||is_webp(d,len)); }
/* decode bytes into imgpool at the current bump offset; returns 1 ok, sets off/w/h and advances pool */
static int decode_into_pool(const u8*d,int len,int*off,int*w,int*h){
    if(len<4 || imgpool_off>=IMGPOOL_SZ-8192) return 0;
    u8* dst=imgpool+imgpool_off; int cap=IMGPOOL_SZ-imgpool_off, ww=0,hh=0,r=-1;
    if(d[0]==0x89&&d[1]==0x50&&d[2]==0x4E&&d[3]==0x47) r=png_decode(d,len,PNG_RAW,PNG_RAW_SZ,dst,cap,&ww,&hh);
    else if(d[0]==0xFF&&d[1]==0xD8){ jp_off=0; r=jpeg_decode(d,len,dst,cap,&ww,&hh); }
    else if(is_webp(d,len)) r=webp_decode(d,len,dst,cap,&ww,&hh);
    else return 0;
    if(r<0||ww<=0||hh<=0||ww>2000||hh>2000) return 0;
    long bytes=(long)ww*hh*4; if(bytes>cap) return 0;
    *off=imgpool_off; *w=ww; *h=hh; imgpool_off+=(int)bytes; return 1;
}
static int decode_image(const u8*d,int len){   /* direct-URL: decode to pool[0] */
    imgpool_off=0; int off=0,w=0,h=0; if(!decode_into_pool(d,len,&off,&w,&h)) return 0; img_w=w; img_h=h; return 1;
}

static int g_page=0; static int lk_y[8],lk_tgt[8],lk_n=0,lk_x,lk_w,lk_bx[8],lk_bw[8],lk_bh[8];
static char g_url[256]="example.com"; static int g_urllen=11, g_urlfocus=0;
static char g_acsug[256]=""; static int g_urlsel=0;
static char g_host[128]="";
static int br_barx,br_bary,br_barw,br_gx,br_gw,br_hx,br_hw,br_bkx,br_bkw,br_rlx,br_rlw;
static int g_forcedark=0, g_darkbx,g_darkbw;
#define WEB_MAXL 600
#define WEB_COLS 160
static char WEBL[WEB_MAXL][WEB_COLS]; static u8 WEBST[WEB_MAXL]; static int web_nl=0, web_scroll=0;
static char web_stat[80]="";
static int webc=0; static char webline[WEB_COLS];
#define WEB_MAXLINK 128
static struct { short line,c0,c1; int href; } weblink[WEB_MAXLINK]; static int web_nlink=0;
static char webhref[4096]; static int webhref_n=0;
static int cur_h=0, in_link=-1, link_c0=0;
static int cur_b=0, bold_c0=0;                 /* bold/emphasis span in progress */
#define WEB_MAXBOLD 256
static struct { short line,c0,c1; } webbold[WEB_MAXBOLD]; static int web_nbold=0;
/* ---- float (right rail: infobox / float:right) ---- */
#define FB_MAXL 220
static char FBL[FB_MAXL][WEB_COLS]; static u8 FBST[FB_MAXL]; static int fb_nl=0;
static struct { short line,c0,c1; int href; } fb_link[64]; static int fb_nlink=0;
static struct { short line,c0,c1; u32 col; } fb_col[96]; static int fb_ncol=0;
static struct { short line,c0,c1; } fb_bold[64]; static int fb_nbold=0;
static int g_hasfloat=0, g_floatw=300, cap_float=0, fdepth=0; static char fnm[14]="";
static int g_railx=0; static int fbN=0, fbvLine[FB_MAXL], fbvY[FB_MAXL], fbvSc[FB_MAXL];
/* ---- left rail (sidebar / nav / float:left) ---- */
#define LB_MAXL 200
static char LBL[LB_MAXL][WEB_COLS]; static u8 LBST[LB_MAXL]; static int lb_nl=0;
static struct { short line,c0,c1; int href; } lb_link[64]; static int lb_nlink=0;
static struct { short line,c0,c1; u32 col; } lb_col[96]; static int lb_ncol=0;
static struct { short line,c0,c1; } lb_bold[64]; static int lb_nbold=0;
static int g_hasleft=0, g_leftw=210, cap_left=0, ldepth=0; static char lnm[14]="";
static int g_leftx=0; static int lbN=0, lbvLine[LB_MAXL], lbvY[LB_MAXL], lbvSc[LB_MAXL];
static int in_title=0; static char g_title[64]=""; static int g_titlen=0;
/* back history */
static char backstk[16][256]; static int backn=0; static char g_cururl[256]="";
/* block/list/table/color state for the renderer */
static struct { char ord; short cnt; } liststk[8]; static int listn=0;
static int cur_quote=0, cur_pre=0;
#define WEB_MAXANCH 64
static struct { char name[24]; short line; } webanch[WEB_MAXANCH]; static int web_nanch=0;
#define WEB_MAXCOL 256
static struct { short line,c0,c1; u32 col; } webcol[WEB_MAXCOL]; static int web_ncol=0;
static u32 cur_color=0; static int col_c0=0;
static struct { char nm[14]; u32 col; } colstk[16]; static int colstn=0;
static int g_wrapcols=WEB_COLS-2, g_webzoom=1, g_wrappx=760;
static void set_wrapcols(void);
static void web_flush(void){
    if(web_nl<WEB_MAXL){
        if(in_link>=0 && webc>link_c0 && web_nlink<WEB_MAXLINK){ weblink[web_nlink].line=(short)web_nl; weblink[web_nlink].c0=(short)link_c0; weblink[web_nlink].c1=(short)webc; weblink[web_nlink].href=in_link; web_nlink++; }
        if(cur_b>0 && webc>bold_c0 && web_nbold<WEB_MAXBOLD){ webbold[web_nbold].line=(short)web_nl; webbold[web_nbold].c0=(short)bold_c0; webbold[web_nbold].c1=(short)webc; web_nbold++; }
        if(cur_color && webc>col_c0 && web_ncol<WEB_MAXCOL){ webcol[web_ncol].line=(short)web_nl; webcol[web_ncol].c0=(short)col_c0; webcol[web_ncol].c1=(short)webc; webcol[web_ncol].col=cur_color; web_ncol++; }
        WEBST[web_nl]=(u8)((cur_h&0x0F) | (cur_quote?0x20:0) | (cap_float?0x40:0) | (cap_left?0x80:0));
        int i=0; for(;i<webc && i<WEB_COLS-1;i++)WEBL[web_nl][i]=webline[i]; WEBL[web_nl][i]=0; web_nl++;
    }
    webc=0; link_c0=0; bold_c0=0; col_c0=0;
}
static void web_emit(char c){ if(c=='\n'){ web_flush(); return; }
    int zsc=g_webzoom; int sc=((cur_h>=1&&cur_h<=2)?(zsc+1):zsc); if(sc>3)sc=3;
    int avail=(cap_left?(g_leftw-20):cap_float?(g_floatw-20):g_wrappx)-(cur_quote?18:0); if(avail<120)avail=120;
    int wln = pw_sub(webline,0,webc,sc) + gadv(c,sc);
    if((wln>avail && webc>0) || webc>=WEB_COLS-2){ int sp=-1; for(int i=webc-1;i>=0;i--)if(webline[i]==' '){sp=i;break;}
        if(sp>4){ char t[WEB_COLS]; int tn=0; for(int i=sp+1;i<webc;i++)t[tn++]=webline[i]; webc=sp; web_flush(); for(int i=0;i<tn;i++)webline[webc++]=t[i]; } else web_flush(); }
    webline[webc++]=c; }
static int g_js_enabled=1;
static void js_write_cb(const char* s,int n){    /* document.write -> emit into page (tags stripped, <br> honored) */
    for(int i=0;i<n;i++){ char c=s[i];
        if(c=='<'){ if(i+3<n && (s[i+1]|32)=='b'&&(s[i+2]|32)=='r') web_emit('\n'); while(i<n && s[i]!='>')i++; continue; }
        if(c=='\n'||c=='\r'||c=='\t'){ web_emit(' '); continue; }
        if(c=='&'){ if(i+4<n&&s[i+1]=='a'&&s[i+2]=='m'&&s[i+3]=='p'){ web_emit('&'); i+=4; continue; } if(i+3<n&&s[i+1]=='l'&&s[i+2]=='t'){ web_emit('<'); i+=3; continue; } if(i+3<n&&s[i+1]=='g'&&s[i+2]=='t'){ web_emit('>'); i+=3; continue; } if(i+5<n&&s[i+1]=='n'&&s[i+2]=='b'&&s[i+3]=='s'&&s[i+4]=='p'){ web_emit(' '); i+=5; continue; } }
        web_emit(c); }
}
static int is_break_tag(const char*nm){ return seq(nm,"br")||seq(nm,"p")||seq(nm,"div")||seq(nm,"li")||seq(nm,"tr")||seq(nm,"hr")||seq(nm,"ul")||seq(nm,"ol")||seq(nm,"table")||seq(nm,"section")||seq(nm,"article")||seq(nm,"header")||seq(nm,"footer")||seq(nm,"nav")||seq(nm,"blockquote")||seq(nm,"pre"); }
static int heading_level(const char*nm){ if(nm[0]=='h'&&nm[1]>='1'&&nm[1]<='6'&&nm[2]==0) return nm[1]-'0'; return 0; }
static int hexv(char c){ if(c>='0'&&c<='9')return c-'0'; if(c>='a'&&c<='f')return c-'a'+10; if(c>='A'&&c<='F')return c-'A'+10; return -1; }
static int tolow_(int c){ return (c>='A'&&c<='Z')?c+32:c; }
static int parse_color(const u8*s,int n,u32*out){        /* value after ':' or '=' -> colour */
    int i=0; while(i<n&&(s[i]==' '||s[i]=='"'||s[i]=='\''))i++;
    if(i<n&&s[i]=='#'){ i++; int h[6],hn=0; while(i<n&&hn<6){ int v=hexv((char)s[i]); if(v<0)break; h[hn++]=v; i++; }
        if(hn>=6){ *out=rgb(h[0]*16+h[1],h[2]*16+h[3],h[4]*16+h[5]); return 1; } if(hn>=3){ *out=rgb(h[0]*17,h[1]*17,h[2]*17); return 1; } return 0; }
    char nm[16]; int k=0; while(i<n&&k<15){ int ch=tolow_(s[i]); if(ch<'a'||ch>'z')break; nm[k++]=(char)ch; i++; } nm[k]=0;
    if(seq(nm,"red")){*out=rgb(214,48,49);return 1;} if(seq(nm,"blue")){*out=rgb(41,86,220);return 1;} if(seq(nm,"green")){*out=rgb(30,150,64);return 1;}
    if(seq(nm,"black")){*out=rgb(24,26,32);return 1;} if(seq(nm,"white")){*out=rgb(245,246,250);return 1;} if(seq(nm,"gray")||seq(nm,"grey")){*out=rgb(128,132,140);return 1;}
    if(seq(nm,"orange")){*out=rgb(226,132,30);return 1;} if(seq(nm,"purple")){*out=rgb(142,60,200);return 1;} if(seq(nm,"navy")){*out=rgb(24,34,110);return 1;}
    if(seq(nm,"teal")){*out=rgb(24,150,140);return 1;} if(seq(nm,"maroon")){*out=rgb(140,32,44);return 1;} if(seq(nm,"yellow")){*out=rgb(190,160,20);return 1;}
    return 0;
}
static int elem_color(const u8*buf,int a,int b,u32*out){  /* text colour from style="color:.." or color=.. (not bg) */
    for(int p=a;p+5<b;p++){
        if(tolow_(buf[p])=='c'&&tolow_(buf[p+1])=='o'&&tolow_(buf[p+2])=='l'&&tolow_(buf[p+3])=='o'&&tolow_(buf[p+4])=='r'){
            if(p>a && buf[p-1]=='-')continue;                                        /* background-color */
            if(p>=a+2 && tolow_(buf[p-2])=='b'&&tolow_(buf[p-1])=='g')continue;       /* bgcolor */
            int q=p+5; while(q<b&&buf[q]==' ')q++; if(q<b&&(buf[q]==':'||buf[q]=='=')){ q++; if(parse_color(buf+q,b-q,out))return 1; } } }
    return 0;
}
static int elem_has(const u8*buf,int a,int b,const char*w){ int wl=slen(w); for(int p=a;p+wl<=b;p++){ int m=1; for(int q=0;q<wl;q++) if(tolow_(buf[p+q])!=w[q]){m=0;break;} if(m)return 1; } return 0; }
static void web_rule(void){ if(web_nl<WEB_MAXL){ WEBL[web_nl][0]=0; WEBST[web_nl]=(u8)(0x0F|(cap_float?0x40:0)|(cap_left?0x80:0)); web_nl++; } }
static void col_set(u32 nc){
    if(cur_color && webc>col_c0 && web_ncol<WEB_MAXCOL){ webcol[web_ncol].line=(short)web_nl; webcol[web_ncol].c0=(short)col_c0; webcol[web_ncol].c1=(short)webc; webcol[web_ncol].col=cur_color; web_ncol++; }
    cur_color=nc; col_c0=webc;
}
static void add_anchor(const u8*buf,int a,int b){
    for(int p=a;p+3<b && web_nanch<WEB_MAXANCH;p++){ if(p>a && buf[p-1]!=' ')continue;
        int isid=(tolow_(buf[p])=='i'&&tolow_(buf[p+1])=='d'&&(buf[p+2]=='='||buf[p+2]==' '||buf[p+2]=='"'||buf[p+2]=='\''));
        int isnm=(p+5<b&&tolow_(buf[p])=='n'&&tolow_(buf[p+1])=='a'&&tolow_(buf[p+2])=='m'&&tolow_(buf[p+3])=='e'&&(buf[p+4]=='='||buf[p+4]==' '));
        if(isid||isnm){ int q=p+(isid?2:4); while(q<b&&(buf[q]==' '||buf[q]=='='))q++; char qt=0; if(q<b&&(buf[q]=='"'||buf[q]=='\'')){qt=buf[q];q++;}
            int k=0; while(q<b&&k<23){ char t=buf[q]; if(qt&&t==qt)break; if(!qt&&(t==' '||t=='>'))break; webanch[web_nanch].name[k++]=t; q++; }
            webanch[web_nanch].name[k]=0; webanch[web_nanch].line=(short)web_nl; if(k>0)web_nanch++; return; } }
}
static u32 g_pagebg=0; static int g_pagedark=0, g_havebg=0;
static void set_pagebg(const u8*buf,int n){ g_havebg=0; g_pagebg=rgb(255,255,255); g_pagedark=0;
    int lim=n<24000?n:24000;
    for(int i=0;i+8<lim;i++){ int m=0;
        if((buf[i]|32)=='b'&&(buf[i+1]|32)=='g'&&(buf[i+2]|32)=='c'&&(buf[i+3]|32)=='o'&&(buf[i+4]|32)=='l'&&(buf[i+5]|32)=='o'&&(buf[i+6]|32)=='r') m=1;
        else if(i+10<lim&&(buf[i]|32)=='b'&&(buf[i+1]|32)=='a'&&(buf[i+2]|32)=='c'&&(buf[i+3]|32)=='k'&&(buf[i+4]|32)=='g'&&(buf[i+5]|32)=='r'&&(buf[i+6]|32)=='o'&&(buf[i+7]|32)=='u'&&(buf[i+8]|32)=='n'&&(buf[i+9]|32)=='d') m=1;
        if(!m)continue;
        int j=i+7; while(j<lim&&j<i+44&&buf[j]!='#'&&buf[j]!='>'&&buf[j]!=';'&&buf[j]!='}')j++;
        if(j<lim&&buf[j]=='#'){ j++; int h[6],hn=0; while(j<lim&&hn<6){ int v=hexv((char)buf[j]); if(v<0)break; h[hn++]=v; j++; }
            int R=0,G=0,B=0; if(hn>=6){R=h[0]*16+h[1];G=h[2]*16+h[3];B=h[4]*16+h[5];} else if(hn>=3){R=h[0]*17;G=h[1]*17;B=h[2]*17;} else continue;
            g_pagebg=rgb(R,G,B); g_pagedark=((R*30+G*59+B*11)/100 < 120); g_havebg=1; return; } }
}
static int mainmap_[WEB_MAXL], fbmap_[WEB_MAXL];
/* ---- CSS engine (subset): <style> rules + inline style; display:none, color, background ---- */
typedef struct { u8 hide,bold,hasfg,hasbg; u32 fg,bg; } CssDecl;
#define CSS_MAXR 1200
static struct { u8 type; char name[24]; u8 hide,bold,hasfg,hasbg; u32 fg,bg; } cssr[CSS_MAXR]; static int css_n=0, g_css_ext_n=0;
static int csshide=0, hidedepth=0; static char hidenm[14]="";
/* ---- CSS custom properties (variables): :root{--x:..} + var(--x[,fallback]) ---- */
#define CSSVAR_MAX 128
static char cssvar_name[CSSVAR_MAX][40]; static char cssvar_val[CSSVAR_MAX][56]; static int cssvar_n=0, g_cssvar_ext_n=0;
static void cssvar_lookup(const char* nm,int nl,char* out,int cap){ out[0]=0;
    for(int i=cssvar_n-1;i>=0;i--){ int m=1; for(int q=0;q<nl;q++)if(cssvar_name[i][q]!=nm[q]){m=0;break;} if(m&&cssvar_name[i][nl]==0){ int k=0; while(cssvar_val[i][k]&&k<cap-1){out[k]=cssvar_val[i][k];k++;} out[k]=0; return; } } }
static void css_resolve_val(const u8* buf,int vs,int ve,char* out,int cap){
    int p=vs; while(p<ve&&(buf[p]==' '||buf[p]=='\t'))p++;
    if(p+4<=ve && buf[p]=='v'&&buf[p+1]=='a'&&buf[p+2]=='r'&&buf[p+3]=='('){
        p+=4; while(p<ve&&buf[p]==' ')p++; int ns=p; while(p<ve&&buf[p]!=','&&buf[p]!=')')p++; int ne=p; while(ne>ns&&buf[ne-1]==' ')ne--;
        char nm[40]; int nl=ne-ns; if(nl>39)nl=39; for(int i=0;i<nl;i++)nm[i]=(char)buf[ns+i]; nm[nl]=0;
        cssvar_lookup(nm,nl,out,cap);
        if(out[0]==0 && p<ve && buf[p]==','){ p++; while(p<ve&&buf[p]==' ')p++; int fs=p; while(p<ve&&buf[p]!=')')p++; int fe=p; while(fe>fs&&buf[fe-1]==' ')fe--; int k=0; for(int i=fs;i<fe&&k<cap-1;i++)out[k++]=(char)buf[i]; out[k]=0; }
        return; }
    int k=0; for(int i=p;i<ve&&k<cap-1;i++)out[k++]=(char)buf[i]; out[k]=0;
}
static void css_parse_decls(const u8*buf,int a,int b,CssDecl*d){
    int p=a;
    while(p<b){ while(p<b&&(buf[p]==' '||buf[p]==';'||buf[p]=='\n'||buf[p]=='\t'||buf[p]=='\r'))p++;
        int ps=p; while(p<b&&buf[p]!=':'&&buf[p]!=';'&&buf[p]!='}')p++;
        if(p>=b||buf[p]!=':'){ if(p<b&&buf[p]==';'){p++;continue;} break; }
        int pe=p; p++; int vs=p; while(p<b&&buf[p]!=';'&&buf[p]!='}')p++; int ve=p; if(p<b&&buf[p]==';')p++;
        int a2=ps,b2=pe; while(a2<b2&&buf[a2]==' ')a2++; while(b2>a2&&buf[b2-1]==' ')b2--;
        if(b2-a2>=2 && buf[a2]=='-' && buf[a2+1]=='-'){       /* variable definition */
            if(cssvar_n<CSSVAR_MAX){ int nl=b2-a2; if(nl>39)nl=39; for(int i=0;i<nl;i++)cssvar_name[cssvar_n][i]=(char)buf[a2+i]; cssvar_name[cssvar_n][nl]=0;
                char vv[56]; css_resolve_val(buf,vs,ve,vv,sizeof(vv)); int k=0; while(vv[k]&&k<55){cssvar_val[cssvar_n][k]=vv[k];k++;} cssvar_val[cssvar_n][k]=0; cssvar_n++; }
            continue; }
        char prop[20]; int pl=0; for(int z=a2;z<b2&&pl<19;z++)prop[pl++]=(char)tolow_(buf[z]); prop[pl]=0;
        if(seq(prop,"display")){ if(elem_has(buf,vs,ve,"none"))d->hide=1; }
        else if(seq(prop,"visibility")){ if(elem_has(buf,vs,ve,"hidden"))d->hide=1; }
        else if(seq(prop,"color")){ char vv[56]; css_resolve_val(buf,vs,ve,vv,sizeof(vv)); u32 c; if(parse_color((const u8*)vv,slen(vv),&c)){d->hasfg=1;d->fg=c;} }
        else if(seq(prop,"background")||seq(prop,"background-color")){ char vv[56]; css_resolve_val(buf,vs,ve,vv,sizeof(vv)); u32 c; if(parse_color((const u8*)vv,slen(vv),&c)){d->hasbg=1;d->bg=c;} }
        else if(seq(prop,"font-weight")){ if(elem_has(buf,vs,ve,"bold")||elem_has(buf,vs,ve,"600")||elem_has(buf,vs,ve,"700")||elem_has(buf,vs,ve,"800")||elem_has(buf,vs,ve,"900"))d->bold=1; } }
}
static void css_parse(const u8*buf,int a,int b){
    int p=a;
    while(p<b && css_n<CSS_MAXR){
        while(p<b){ if(buf[p]==' '||buf[p]=='\n'||buf[p]=='\t'||buf[p]=='\r')p++; else if(p+1<b&&buf[p]=='/'&&buf[p+1]=='*'){ p+=2; while(p+1<b&&!(buf[p]=='*'&&buf[p+1]=='/'))p++; p+=2; } else break; }
        if(p>=b)break;
        int ss=p; while(p<b&&buf[p]!='{'&&buf[p]!='}')p++; if(p>=b||buf[p]!='{')break;
        int se=p;
        int depth=0,z=se,close=b; for(;z<b;z++){ if(buf[z]=='{')depth++; else if(buf[z]=='}'){ depth--; if(depth<=0){ close=z; break; } } }
        int isat=0; for(int k=ss;k<se;k++) if(buf[k]=='@'){isat=1;break;}
        if(isat){ if(elem_has(buf,ss,se,"media")||elem_has(buf,ss,se,"supports")){
                int isresp=elem_has(buf,ss,se,"width")||elem_has(buf,ss,se,"print")||elem_has(buf,ss,se,"light")||elem_has(buf,ss,se,"hover");
                int wantdark=elem_has(buf,ss,se,"dark");
                if(wantdark || !isresp) css_parse(buf,se+1,close);   /* apply dark / plain-screen media; skip responsive/print/light */
            } p=close+1; continue; }
        int ds=se+1, de=close;
        CssDecl d; d.hide=d.bold=d.hasfg=d.hasbg=0; d.fg=0; d.bg=0; css_parse_decls(buf,ds,de,&d);
        if(d.hide||d.bold||d.hasfg||d.hasbg){
            int q=ss;
            while(q<se && css_n<CSS_MAXR){ while(q<se&&(buf[q]==' '||buf[q]==','||buf[q]=='\n'||buf[q]=='\t'||buf[q]=='\r'))q++; int ts=q; while(q<se&&buf[q]!=',')q++; int te=q;
                while(te>ts&&(buf[te-1]==' '||buf[te-1]=='\n'||buf[te-1]=='\t'||buf[te-1]=='\r'))te--; if(te<=ts)continue;
                /* descendant "A B" / child "A>B": use the LAST simple selector (the subject) */
                int wasdesc=0,subj=ts; for(int z=ts;z<te;z++) if(buf[z]==' '||buf[z]=='>'){ subj=z+1; wasdesc=1; } ts=subj;
                if(ts>=te)continue;
                if(wasdesc){ int cid=0; for(int z=ts;z<te;z++) if(buf[z]=='.'||buf[z]=='#'){cid=1;break;} if(!cid)continue; }  /* skip bare-tag descendant subjects (too broad) */
                int end2=te; for(int z=ts;z<te;z++) if(buf[z]==':'||buf[z]=='['){ end2=z; break; }
                int hashpos=-1,dotpos=-1; for(int z=ts;z<end2;z++){ if(buf[z]=='#')hashpos=z; else if(buf[z]=='.')dotpos=z; }
                int type,nstart,nend;
                if(hashpos>=0){ type=2; nstart=hashpos+1; nend=end2; for(int z=hashpos+1;z<end2;z++) if(buf[z]=='.'){nend=z;break;} }
                else if(dotpos>=0){ type=1; nstart=dotpos+1; nend=end2; for(int z=dotpos+1;z<end2;z++) if(buf[z]=='.'){nend=z;break;} }
                else { type=0; nstart=ts; nend=end2; if(buf[ts]=='*')continue; }
                int L=nend-nstart; if(L<=0)continue; if(L>23)L=23;
                cssr[css_n].type=(u8)type; for(int z=0;z<L;z++){ char ch=buf[nstart+z]; if(type==0&&ch>='A'&&ch<='Z')ch+=32; cssr[css_n].name[z]=ch; } cssr[css_n].name[L]=0;
                cssr[css_n].hide=d.hide; cssr[css_n].bold=d.bold; cssr[css_n].hasfg=d.hasfg; cssr[css_n].fg=d.fg; cssr[css_n].hasbg=d.hasbg; cssr[css_n].bg=d.bg; css_n++; }
        }
        p=close+1;
    }
}
static void css_match(const char*tag,const char*cls,const char*id,CssDecl*out){
    for(int r=0;r<css_n;r++){ int hit=0;
        if(cssr[r].type==0){ if(seq(tag,cssr[r].name))hit=1; }
        else if(cssr[r].type==1){ const char*c=cls; int nl=slen(cssr[r].name); while(*c){ while(*c==' ')c++; const char*st=c; while(*c&&*c!=' ')c++; int len=(int)(c-st); if(len==nl){ int m=1; for(int q=0;q<nl;q++)if(st[q]!=cssr[r].name[q]){m=0;break;} if(m){hit=1;break;} } } }
        else { if(id[0]&&seq(id,cssr[r].name))hit=1; }
        if(hit){ if(cssr[r].hide)out->hide=1; if(cssr[r].bold)out->bold=1; if(cssr[r].hasfg){out->hasfg=1;out->fg=cssr[r].fg;} if(cssr[r].hasbg){out->hasbg=1;out->bg=cssr[r].bg;} } }
}
static void attr_get(const u8*buf,int a,int b,const char*name,char*out,int cap){ out[0]=0; int nl=slen(name);
    for(int p=a;p+nl+1<b;p++){ if(p>a){ char pc=buf[p-1]; if(pc!=' '&&pc!='\t'&&pc!='\n'&&pc!='\r'&&pc!='"'&&pc!='\'')continue; }
        int m=1; for(int q=0;q<nl;q++) if(tolow_(buf[p+q])!=name[q]){m=0;break;} if(!m)continue;
        int q=p+nl; while(q<b&&buf[q]==' ')q++; if(q>=b||buf[q]!='=')continue; q++; while(q<b&&buf[q]==' ')q++;
        char qt=0; if(q<b&&(buf[q]=='"'||buf[q]=='\'')){qt=buf[q];q++;}
        int k=0; while(q<b&&k<cap-1){ char t=buf[q]; if(qt&&t==qt)break; if(!qt&&(t==' '||t=='>'))break; out[k++]=t; q++; } out[k]=0; return; }
}
static int stristr(const char*h,const char*needle);
static void float_extract(void){
    fb_nl=0; fb_nlink=0; fb_ncol=0; fb_nbold=0; lb_nl=0; lb_nlink=0; lb_ncol=0; lb_nbold=0; int newmain=0;
    static int lbmap_[WEB_MAXL];
    for(int L=0;L<web_nl;L++){
        if(WEBST[L]&0x40){ fbmap_[L]=fb_nl; mainmap_[L]=-1; lbmap_[L]=-1;
            if(fb_nl<FB_MAXL){ int i=0; for(;WEBL[L][i]&&i<WEB_COLS-1;i++)FBL[fb_nl][i]=WEBL[L][i]; FBL[fb_nl][i]=0; FBST[fb_nl]=(u8)(WEBST[L]&0x2F); fb_nl++; } }
        else if(WEBST[L]&0x80){ lbmap_[L]=lb_nl; mainmap_[L]=-1; fbmap_[L]=-1;
            if(lb_nl<LB_MAXL){ int i=0; for(;WEBL[L][i]&&i<WEB_COLS-1;i++)LBL[lb_nl][i]=WEBL[L][i]; LBL[lb_nl][i]=0; LBST[lb_nl]=(u8)(WEBST[L]&0x2F); lb_nl++; } }
        else { mainmap_[L]=newmain; fbmap_[L]=-1; lbmap_[L]=-1;
            if(newmain!=L){ int i=0; for(;WEBL[L][i]&&i<WEB_COLS-1;i++)WEBL[newmain][i]=WEBL[L][i]; WEBL[newmain][i]=0; WEBST[newmain]=WEBST[L]; } newmain++; } }
    int nl2=0; for(int k=0;k<web_nlink;k++){ int L=weblink[k].line; if(L<0||L>=web_nl)continue;
        if(fbmap_[L]>=0){ if(fb_nlink<64){ fb_link[fb_nlink].line=(short)fbmap_[L]; fb_link[fb_nlink].c0=weblink[k].c0; fb_link[fb_nlink].c1=weblink[k].c1; fb_link[fb_nlink].href=weblink[k].href; fb_nlink++; } }
        else if(lbmap_[L]>=0){ if(lb_nlink<64){ lb_link[lb_nlink].line=(short)lbmap_[L]; lb_link[lb_nlink].c0=weblink[k].c0; lb_link[lb_nlink].c1=weblink[k].c1; lb_link[lb_nlink].href=weblink[k].href; lb_nlink++; } }
        else if(mainmap_[L]>=0){ weblink[nl2]=weblink[k]; weblink[nl2].line=(short)mainmap_[L]; nl2++; } } web_nlink=nl2;
    int nc2=0; for(int k=0;k<web_ncol;k++){ int L=webcol[k].line; if(L<0||L>=web_nl)continue;
        if(fbmap_[L]>=0){ if(fb_ncol<96){ fb_col[fb_ncol].line=(short)fbmap_[L]; fb_col[fb_ncol].c0=webcol[k].c0; fb_col[fb_ncol].c1=webcol[k].c1; fb_col[fb_ncol].col=webcol[k].col; fb_ncol++; } }
        else if(lbmap_[L]>=0){ if(lb_ncol<96){ lb_col[lb_ncol].line=(short)lbmap_[L]; lb_col[lb_ncol].c0=webcol[k].c0; lb_col[lb_ncol].c1=webcol[k].c1; lb_col[lb_ncol].col=webcol[k].col; lb_ncol++; } }
        else if(mainmap_[L]>=0){ webcol[nc2]=webcol[k]; webcol[nc2].line=(short)mainmap_[L]; nc2++; } } web_ncol=nc2;
    int nb2=0; for(int k=0;k<web_nbold;k++){ int L=webbold[k].line; if(L<0||L>=web_nl)continue;
        if(fbmap_[L]>=0){ if(fb_nbold<64){ fb_bold[fb_nbold].line=(short)fbmap_[L]; fb_bold[fb_nbold].c0=webbold[k].c0; fb_bold[fb_nbold].c1=webbold[k].c1; fb_nbold++; } }
        else if(lbmap_[L]>=0){ if(lb_nbold<64){ lb_bold[lb_nbold].line=(short)lbmap_[L]; lb_bold[lb_nbold].c0=webbold[k].c0; lb_bold[lb_nbold].c1=webbold[k].c1; lb_nbold++; } }
        else if(mainmap_[L]>=0){ webbold[nb2]=webbold[k]; webbold[nb2].line=(short)mainmap_[L]; nb2++; } } web_nbold=nb2;
    int ni2=0; for(int k=0;k<web_nimg;k++){ int L=webimg[k].line; if(L>=0&&L<web_nl && mainmap_[L]>=0){ webimg[ni2]=webimg[k]; webimg[ni2].line=mainmap_[L]; ni2++; } } web_nimg=ni2;
    for(int k=0;k<web_nanch;k++){ int L=webanch[k].line; if(L>=0&&L<web_nl) webanch[k].line=(short)(mainmap_[L]>=0?mainmap_[L]:0); }
    web_nl=newmain;
}
/* map a Unicode code point to an ASCII approximation (font is ASCII-only) */
static char translit(int cp){
    if(cp>=32 && cp<127) return (char)cp;
    switch(cp){
        case 0xC0:case 0xC1:case 0xC2:case 0xC3:case 0xC4:case 0xC5:case 0xC6: return 'A';
        case 0xE0:case 0xE1:case 0xE2:case 0xE3:case 0xE4:case 0xE5:case 0xE6: return 'a';
        case 0xC8:case 0xC9:case 0xCA:case 0xCB: return 'E';
        case 0xE8:case 0xE9:case 0xEA:case 0xEB: return 'e';
        case 0xCC:case 0xCD:case 0xCE:case 0xCF: return 'I';
        case 0xEC:case 0xED:case 0xEE:case 0xEF: return 'i';
        case 0xD2:case 0xD3:case 0xD4:case 0xD5:case 0xD6:case 0xD8: return 'O';
        case 0xF2:case 0xF3:case 0xF4:case 0xF5:case 0xF6:case 0xF8: return 'o';
        case 0xD9:case 0xDA:case 0xDB:case 0xDC: return 'U';
        case 0xF9:case 0xFA:case 0xFB:case 0xFC: return 'u';
        case 0xD1: return 'N'; case 0xF1: return 'n';
        case 0xC7: return 'C'; case 0xE7: return 'c';
        case 0xDD:case 0x178: return 'Y'; case 0xFD:case 0xFF: return 'y';
        case 0xDF: return 's'; case 0xD0: return 'D'; case 0xF0: return 'd';
        case 0x2018:case 0x2019:case 0x2032:case 0x2035: return '\'';
        case 0x201C:case 0x201D:case 0x2033: return '"';
        case 0x2013:case 0x2014:case 0x2212:case 0x2010:case 0x2011: return '-';
        case 0x2022:case 0x00B7:case 0x25CF:case 0x2043: return '*';
        case 0x2026: return '.';
        case 0x00A0:case 0x2009:case 0x200A:case 0x2002:case 0x2003:case 0x202F:case 0x3000: return ' ';
        case 0x00A9: return 'c'; case 0x00AE: return 'r'; case 0x2122: return 'T';
        case 0x00D7: return 'x'; case 0x00B0: return 'o'; case 0x20AC: return 'E';
        case 0x00A3: return 'L'; case 0x2192: return '>'; case 0x2190: return '<';
    }
    return 0;   /* unknown -> caller emits a space */
}
static int ent_cp(const char* nm){   /* named HTML entity -> code point (-1 if unknown) */
    if(seq(nm,"amp"))return '&'; if(seq(nm,"lt"))return '<'; if(seq(nm,"gt"))return '>';
    if(seq(nm,"quot"))return '"'; if(seq(nm,"apos"))return '\''; if(seq(nm,"nbsp"))return 0xA0;
    if(seq(nm,"aring"))return 0xE5; if(seq(nm,"Aring"))return 0xC5; if(seq(nm,"auml"))return 0xE4; if(seq(nm,"Auml"))return 0xC4;
    if(seq(nm,"aacute"))return 0xE1; if(seq(nm,"agrave"))return 0xE0; if(seq(nm,"acirc"))return 0xE2; if(seq(nm,"atilde"))return 0xE3; if(seq(nm,"aelig"))return 0xE6;
    if(seq(nm,"ouml"))return 0xF6; if(seq(nm,"Ouml"))return 0xD6; if(seq(nm,"oacute"))return 0xF3; if(seq(nm,"ograve"))return 0xF2; if(seq(nm,"ocirc"))return 0xF4; if(seq(nm,"oslash"))return 0xF8; if(seq(nm,"Oslash"))return 0xD8; if(seq(nm,"otilde"))return 0xF5;
    if(seq(nm,"eacute"))return 0xE9; if(seq(nm,"Eacute"))return 0xC9; if(seq(nm,"egrave"))return 0xE8; if(seq(nm,"ecirc"))return 0xEA; if(seq(nm,"euml"))return 0xEB;
    if(seq(nm,"uuml"))return 0xFC; if(seq(nm,"Uuml"))return 0xDC; if(seq(nm,"uacute"))return 0xFA; if(seq(nm,"ugrave"))return 0xF9;
    if(seq(nm,"iacute"))return 0xED; if(seq(nm,"igrave"))return 0xEC;
    if(seq(nm,"ntilde"))return 0xF1; if(seq(nm,"Ntilde"))return 0xD1; if(seq(nm,"ccedil"))return 0xE7; if(seq(nm,"Ccedil"))return 0xC7; if(seq(nm,"szlig"))return 0xDF;
    if(seq(nm,"mdash"))return 0x2014; if(seq(nm,"ndash"))return 0x2013; if(seq(nm,"hellip"))return 0x2026;
    if(seq(nm,"rsquo"))return 0x2019; if(seq(nm,"lsquo"))return 0x2018; if(seq(nm,"rdquo"))return 0x201D; if(seq(nm,"ldquo"))return 0x201C;
    if(seq(nm,"bull"))return 0x2022; if(seq(nm,"copy"))return 0xA9; if(seq(nm,"reg"))return 0xAE; if(seq(nm,"trade"))return 0x2122;
    if(seq(nm,"middot"))return 0xB7; if(seq(nm,"deg"))return 0xB0; if(seq(nm,"euro"))return 0x20AC; if(seq(nm,"pound"))return 0xA3; if(seq(nm,"times"))return 0xD7;
    if(seq(nm,"rarr"))return 0x2192; if(seq(nm,"larr"))return 0x2190;
    if(seq(nm,"uarr"))return 0x2191; if(seq(nm,"darr"))return 0x2193; if(seq(nm,"harr"))return 0x2194;
    if(seq(nm,"frac12"))return 0xBD; if(seq(nm,"frac14"))return 0xBC; if(seq(nm,"frac34"))return 0xBE;
    if(seq(nm,"sup2"))return 0xB2; if(seq(nm,"sup3"))return 0xB3; if(seq(nm,"sup1"))return 0xB9;
    if(seq(nm,"sect"))return 0xA7; if(seq(nm,"para"))return 0xB6; if(seq(nm,"dagger"))return 0x2020; if(seq(nm,"Dagger"))return 0x2021;
    if(seq(nm,"plusmn"))return 0xB1; if(seq(nm,"divide"))return 0xF7; if(seq(nm,"micro"))return 0xB5; if(seq(nm,"cent"))return 0xA2; if(seq(nm,"yen"))return 0xA5;
    if(seq(nm,"laquo"))return 0xAB; if(seq(nm,"raquo"))return 0xBB; if(seq(nm,"iquest"))return 0xBF; if(seq(nm,"iexcl"))return 0xA1;
    if(seq(nm,"spades"))return 0x2660; if(seq(nm,"clubs"))return 0x2663; if(seq(nm,"hearts"))return 0x2665; if(seq(nm,"diams"))return 0x2666;
    if(seq(nm,"check"))return 0x2713; if(seq(nm,"cross"))return 0x2717; if(seq(nm,"star"))return 0x2605; if(seq(nm,"starf"))return 0x2605;
    if(seq(nm,"infin"))return 0x221E; if(seq(nm,"ne"))return 0x2260; if(seq(nm,"le"))return 0x2264; if(seq(nm,"ge"))return 0x2265;
    if(seq(nm,"alpha"))return 0x3B1; if(seq(nm,"beta"))return 0x3B2; if(seq(nm,"gamma"))return 0x3B3; if(seq(nm,"pi"))return 0x3C0; if(seq(nm,"mu"))return 0x3BC; if(seq(nm,"lambda"))return 0x3BB; if(seq(nm,"sigma"))return 0x3C3; if(seq(nm,"omega"))return 0x3C9; if(seq(nm,"delta"))return 0x3B4; if(seq(nm,"theta"))return 0x3B8;
    if(seq(nm,"emsp"))return 0x2003; if(seq(nm,"ensp"))return 0x2002; if(seq(nm,"thinsp"))return 0x2009; if(seq(nm,"zwnj"))return 0x200C; if(seq(nm,"shy"))return 0xAD;
    return -1;
}
static void browser_render(const u8*buf,int n){
    set_wrapcols();
    set_pagebg(buf,n);
    web_nl=0; web_scroll=0; webc=0; web_nlink=0; webhref_n=0; cur_h=0; in_link=-1; link_c0=0; web_nimg=0;
    css_n=g_css_ext_n; cssvar_n=g_cssvar_ext_n; csshide=0; hidedepth=0;
    cur_b=0; bold_c0=0; web_nbold=0; in_title=0; g_title[0]=0; g_titlen=0;
    listn=0; cur_quote=0; cur_pre=0; web_nanch=0; web_ncol=0; cur_color=0; col_c0=0; colstn=0;
    g_hasfloat=0; cap_float=0; fdepth=0; fb_nl=0; g_hasleft=0; cap_left=0; ldepth=0; lb_nl=0;
    { const char*bs=(const char*)buf;
      if(stristr(bs,"infobox")||stristr(bs,"float:right")||stristr(bs,"float: right")){ if(g_wrappx>g_floatw+180){ g_hasfloat=1; g_wrappx-=(g_floatw+18); } }
      if(stristr(bs,"sidebar")||stristr(bs,"navbar")||stristr(bs,"float:left")||stristr(bs,"float: left")){ if(g_wrappx>g_leftw+180){ g_hasleft=1; g_wrappx-=(g_leftw+18); } } }
    int td_first=1;
    int bo=0;
    int skip=0; char last='\n';
    for(int i=bo;i<n;i++){ unsigned char c=buf[i];
        if(c=='<'){ int j=i+1,closing=0; if(j<n&&buf[j]=='/'){closing=1;j++;} char nm[14]; int k=0;
            while(j<n&&k<13){ char t=buf[j]; if(t=='>'||t==' '||t=='\t'||t=='\n'||t=='/'||t=='\r')break; if(t>='A'&&t<='Z')t+=32; nm[k++]=t; j++; }
            nm[k]=0; int nameend=j; while(j<n&&buf[j]!='>')j++;
            if(seq(nm,"script")||seq(nm,"style")){ if(seq(nm,"style")&&!closing){ int cs=j+1, ce=cs; while(ce+7<n){ if(buf[ce]=='<'&&buf[ce+1]=='/'&&(buf[ce+2]|32)=='s'&&(buf[ce+3]|32)=='t'&&(buf[ce+4]|32)=='y'&&(buf[ce+5]|32)=='l'&&(buf[ce+6]|32)=='e')break; ce++; } css_parse(buf,cs,ce); }
                else if(seq(nm,"script")&&!closing){ int cs=j+1, ce=cs; while(ce+8<n){ if(buf[ce]=='<'&&buf[ce+1]=='/'&&(buf[ce+2]|32)=='s'&&(buf[ce+3]|32)=='c'&&(buf[ce+4]|32)=='r'&&(buf[ce+5]|32)=='i'&&(buf[ce+6]|32)=='p'&&(buf[ce+7]|32)=='t')break; ce++; }
                    int hassrc=elem_has(buf,nameend,j,"src="); int len2=ce-cs;
                    if(g_js_enabled && !hassrc && len2>1 && len2<20000 && !csshide){ js_run((const char*)buf+cs, len2, js_write_cb); } }
                skip=closing?0:1; }
            else if(csshide){ if(!closing && seq(nm,hidenm)) hidedepth++; else if(closing && seq(nm,hidenm)){ hidedepth--; if(hidedepth<=0)csshide=0; } }
            else if(!skip){ int hl=heading_level(nm);
                if(g_hasfloat){
                    if(!cap_float){ if(!closing && (seq(nm,"table")||seq(nm,"div")||seq(nm,"aside")||seq(nm,"figure")||seq(nm,"section")) && (elem_has(buf,nameend,j,"infobox")||elem_has(buf,nameend,j,"float:right")||elem_has(buf,nameend,j,"float: right"))){ if(last!='\n'){ web_emit('\n'); last='\n'; } cap_float=1; int L2=slen(nm); if(L2>13)L2=13; for(int z=0;z<L2;z++)fnm[z]=nm[z]; fnm[L2]=0; fdepth=1; } }
                    else { if(!closing && seq(nm,fnm)) fdepth++; else if(closing && seq(nm,fnm)){ fdepth--; if(fdepth<=0){ web_emit('\n'); web_flush(); cap_float=0; last='\n'; } } }
                }
                if(g_hasleft){
                    if(!cap_left && !cap_float){ if(!closing && (seq(nm,"nav")||seq(nm,"div")||seq(nm,"aside")||seq(nm,"section")) && (elem_has(buf,nameend,j,"sidebar")||elem_has(buf,nameend,j,"navbar")||elem_has(buf,nameend,j,"float:left")||elem_has(buf,nameend,j,"float: left"))){ if(last!='\n'){ web_emit('\n'); last='\n'; } cap_left=1; int L2=slen(nm); if(L2>13)L2=13; for(int z=0;z<L2;z++)lnm[z]=nm[z]; lnm[L2]=0; ldepth=1; } }
                    else if(cap_left){ if(!closing && seq(nm,lnm)) ldepth++; else if(closing && seq(nm,lnm)){ ldepth--; if(ldepth<=0){ web_emit('\n'); web_flush(); cap_left=0; last='\n'; } } }
                }
                if(!closing){ add_anchor(buf,nameend,j); u32 cc;
                    CssDecl cd; cd.hide=cd.bold=cd.hasfg=cd.hasbg=0; cd.fg=0; cd.bg=0;
                    if(css_n>0){ char cls[128],idb[40]; attr_get(buf,nameend,j,"class",cls,sizeof(cls)); attr_get(buf,nameend,j,"id",idb,sizeof(idb)); css_match(nm,cls,idb,&cd); }
                    int inlinehide=(elem_has(buf,nameend,j,"display:none")||elem_has(buf,nameend,j,"display: none")||elem_has(buf,nameend,j,"visibility:hidden"));
                    if((cd.hide||inlinehide) && !seq(nm,"body")&&!seq(nm,"html")){
                        int isvoid=seq(nm,"br")||seq(nm,"hr")||seq(nm,"img")||seq(nm,"meta")||seq(nm,"input")||seq(nm,"link");
                        if(!isvoid && !csshide){ csshide=1; int L=slen(nm); if(L>13)L=13; for(int z=0;z<L;z++)hidenm[z]=nm[z]; hidenm[L]=0; hidedepth=1; }
                        i=j; continue; }
                    if((seq(nm,"body")||seq(nm,"html")) && cd.hasbg){ g_pagebg=cd.bg; g_pagedark=(((cd.bg>>16&0xFF)*30+(cd.bg>>8&0xFF)*59+(cd.bg&0xFF)*11)/100<120); g_havebg=1; }
                    int hascol=0;
                    if(!seq(nm,"br")&&!seq(nm,"hr")&&!seq(nm,"img")&&!seq(nm,"meta")&&!seq(nm,"input")&&!seq(nm,"link")){ hascol=elem_color(buf,nameend,j,&cc); if(!hascol && cd.hasfg){ cc=cd.fg; hascol=1; } }
                    if(hascol){ if(colstn<16){ int L=slen(nm); if(L>13)L=13; for(int z=0;z<L;z++)colstk[colstn].nm[z]=nm[z]; colstk[colstn].nm[L]=0; colstk[colstn].col=cc; colstn++; col_set(cc); } } }
                else { if(colstn>0 && seq(colstk[colstn-1].nm,nm)){ colstn--; col_set(colstn>0?colstk[colstn-1].col:0); } }
                if(seq(nm,"a")){
                    if(closing){ if(in_link>=0 && webc>link_c0 && web_nlink<WEB_MAXLINK){ weblink[web_nlink].line=(short)web_nl; weblink[web_nlink].c0=(short)link_c0; weblink[web_nlink].c1=(short)webc; weblink[web_nlink].href=in_link; web_nlink++; } in_link=-1; }
                    else { int hi=-1;
                        for(int p=nameend;p+3<j;p++){ if((buf[p]|32)=='h'&&(buf[p+1]|32)=='r'&&(buf[p+2]|32)=='e'&&(buf[p+3]|32)=='f'){ int q=p+4; while(q<j&&(buf[q]==' '||buf[q]=='='))q++; char qt=0; if(q<j&&(buf[q]=='"'||buf[q]=='\'')){qt=buf[q];q++;}
                            hi=webhref_n; while(q<j&&webhref_n<4094){ char t=buf[q]; if(qt&&t==qt)break; if(!qt&&(t==' '||t=='>'))break; webhref[webhref_n++]=t; q++; } webhref[webhref_n++]=0; break; } }
                        in_link=hi; link_c0=webc; }
                }
                else if(hl){ if(last!='\n'){ web_emit('\n'); last='\n'; } cur_h=closing?0:hl; }
                else if(seq(nm,"title")){ in_title=closing?0:1; if(!closing){ g_titlen=0; g_title[0]=0; } }
                else if(seq(nm,"b")||seq(nm,"strong")||seq(nm,"i")||seq(nm,"em")){
                    if(closing){ if(cur_b>0){ cur_b--; if(cur_b==0 && webc>bold_c0 && web_nbold<WEB_MAXBOLD){ webbold[web_nbold].line=(short)web_nl; webbold[web_nbold].c0=(short)bold_c0; webbold[web_nbold].c1=(short)webc; web_nbold++; } } }
                    else { if(cur_b==0)bold_c0=webc; cur_b++; } }
                else if(seq(nm,"img")&&!closing){ if(web_nimg<WEB_MAXIMG){ if(last!='\n'){ web_emit('\n'); last='\n'; }
                    char* dp=webimg[web_nimg].src; dp[0]=0;
                    attr_get(buf,nameend,j,"data-src",dp,256);
                    if(!dp[0]) attr_get(buf,nameend,j,"data-lazy-src",dp,256);
                    if(!dp[0]) attr_get(buf,nameend,j,"data-original",dp,256);
                    if(!dp[0]) attr_get(buf,nameend,j,"src",dp,256);
                    if(dp[0]&&!(dp[0]=='d'&&dp[1]=='a'&&dp[2]=='t'&&dp[3]=='a'&&dp[4]==':')){ webimg[web_nimg].line=web_nl; webimg[web_nimg].w=0; webimg[web_nimg].h=0; webimg[web_nimg].off=0; webimg[web_nimg].ok=0; web_nimg++; } } }
                else if(seq(nm,"td")||seq(nm,"th")){ if(!closing){ if(!td_first){ web_emit(' ');web_emit('|');web_emit(' '); } td_first=0;
                        if(seq(nm,"th")){ if(cur_b==0)bold_c0=webc; cur_b++; } last=' '; }
                    else if(seq(nm,"th")){ if(cur_b>0){ cur_b--; if(cur_b==0 && webc>bold_c0 && web_nbold<WEB_MAXBOLD){ webbold[web_nbold].line=(short)web_nl; webbold[web_nbold].c0=(short)bold_c0; webbold[web_nbold].c1=(short)webc; web_nbold++; } } } }
                else if(seq(nm,"tr")){ if(last!='\n'){ web_emit('\n'); last='\n'; } td_first=1; }
                else if(seq(nm,"table")){ if(last!='\n'){ web_emit('\n'); last='\n'; } td_first=1; }
                else if(seq(nm,"ol")||seq(nm,"ul")){ if(!closing){ if(listn<8){ liststk[listn].ord=seq(nm,"ol")?1:0; liststk[listn].cnt=0; listn++; } } else { if(listn>0)listn--; } if(last!='\n'){ web_emit('\n'); last='\n'; } }
                else if(seq(nm,"li")&&!closing){ if(last!='\n'){web_emit('\n');last='\n';} for(int z=1;z<listn;z++){web_emit(' ');web_emit(' ');}
                    if(listn>0 && liststk[listn-1].ord){ liststk[listn-1].cnt++; int v=liststk[listn-1].cnt; char tmp[8]; int tn=0; if(v==0)tmp[tn++]='0'; while(v){tmp[tn++]=(char)('0'+v%10); v/=10;} while(tn)web_emit(tmp[--tn]); web_emit('.'); web_emit(' '); }
                    else { web_emit('-'); web_emit(' '); } last=' '; }
                else if(seq(nm,"hr")){ if(last!='\n'){ web_emit('\n'); } web_rule(); last='\n'; }
                else if(seq(nm,"pre")){ if(last!='\n'){ web_emit('\n'); last='\n'; } cur_pre=closing?0:1; }
                else if(seq(nm,"blockquote")){ if(last!='\n'){ web_emit('\n'); last='\n'; } cur_quote=closing?0:1; }
                else if(is_break_tag(nm)){ if(last!='\n'){ web_emit('\n'); last='\n'; } }
            }
            i=j; continue; }
        if(skip||csshide)continue;
        if(in_title){ if(c=='<'){ /* fallthrough to tag handling below won't happen; titles end at </title> via skip */ }
            if(c=='&'){ int j=i+1; char nm[12]; int k=0; while(j<n&&k<11){ char t=buf[j]; if(t==';'||t=='&'||t==' '||t=='<')break; nm[k++]=t; j++; } nm[k]=0; if(j<n&&buf[j]==';'){ int cp=(nm[0]=='#')?({int v=0;for(int q=1;nm[q];q++)if(nm[q]>='0'&&nm[q]<='9')v=v*10+(nm[q]-'0');v;}):ent_cp(nm); char o=(cp>=0)?translit(cp):0; if(o&&g_titlen<63){g_title[g_titlen++]=o;g_title[g_titlen]=0;} i=j; continue; } }
            unsigned char uc=(unsigned char)c; if(uc>=0x80){ int cp,extra; if(uc<0xC0){continue;} else if(uc<0xE0){cp=uc&0x1F;extra=1;} else if(uc<0xF0){cp=uc&0x0F;extra=2;} else {cp=uc&0x07;extra=3;} for(int e=0;e<extra&&i+1<n;e++){ if(((unsigned char)buf[i+1]&0xC0)!=0x80)break; i++; cp=(cp<<6)|((unsigned char)buf[i]&0x3F); } char tc=translit(cp); if(tc&&g_titlen<63){g_title[g_titlen++]=tc;g_title[g_titlen]=0;} continue; }
            if(c>=32&&c!='<'&&g_titlen<63){ g_title[g_titlen++]=(char)c; g_title[g_titlen]=0; } continue; }
        if(cur_pre){ if(c!='&'){ if(c=='\n'){web_emit('\n');last='\n';continue;} if(c=='\r')continue; if(c=='\t'){web_emit(' ');web_emit(' ');last=' ';continue;} if((unsigned char)c>=32){web_emit((char)c);last=(char)c;} continue; } }
        if(c=='&'){ int j=i+1; char nm[12]; int k=0; while(j<n&&k<11){ char t=buf[j]; if(t==';'||t=='&'||t==' '||t=='<')break; nm[k++]=t; j++; } nm[k]=0;
            if(j<n&&buf[j]==';'){ int cp=-1;
                if(nm[0]=='#'){ if(nm[1]=='x'||nm[1]=='X'){ int v=0; for(int q=2;nm[q];q++){ int d=nm[q]>='0'&&nm[q]<='9'?nm[q]-'0':(nm[q]|32)>='a'&&(nm[q]|32)<='f'?(nm[q]|32)-'a'+10:-1; if(d<0)break; v=v*16+d; } cp=v; } else { int v=0; for(int q=1;nm[q];q++)if(nm[q]>='0'&&nm[q]<='9')v=v*10+(nm[q]-'0'); cp=v; } }
                else cp=ent_cp(nm);
                char o = (cp>=0)?translit(cp):0;
                if(o==0){ if(last!=' '&&last!='\n'){web_emit(' ');last=' ';} } else { web_emit(o); last=o; }
                i=j; continue; }
            web_emit('&'); last='&'; continue; }
        if(c=='\r')continue;
        if(c==' '||c=='\n'||c=='\t'){ if(last!=' '&&last!='\n'){ web_emit(' '); last=' '; } continue; }
        { unsigned char uc=(unsigned char)c;
          if(uc>=0x80){ int cp,extra; if(uc<0xC0){continue;} else if(uc<0xE0){cp=uc&0x1F;extra=1;} else if(uc<0xF0){cp=uc&0x0F;extra=2;} else {cp=uc&0x07;extra=3;}
              for(int e=0;e<extra && i+1<n;e++){ if(((unsigned char)buf[i+1]&0xC0)!=0x80)break; i++; cp=(cp<<6)|((unsigned char)buf[i]&0x3F); }
              char tc=translit(cp); if(tc){ web_emit(tc); last=tc; } else { if(last!=' '&&last!='\n'){web_emit(' ');last=' ';} }
              continue; } }
        if(c<32)continue;
        web_emit((char)c); last=(char)c; }
    web_flush();
    if(g_hasfloat||g_hasleft) float_extract();
    if(g_forcedark){ g_pagedark=1; g_havebg=1; g_pagebg=rgb(23,24,29); }
    if(web_nl==0){ const char*z="(no readable text on this page)"; int i=0; while(z[i]){WEBL[0][i]=z[i];i++;} WEBL[0][i]=0; WEBST[0]=0; web_nl=1; }
}
static int ci_eq(char a,char b){ if(a>='A'&&a<='Z')a+=32; if(b>='A'&&b<='Z')b+=32; return a==b; }
static void url_split(const char*url,int*secure,char*host,char*path){
    const char*s=url; while(*s==' ')s++; *secure=-1;   /* -1 = no scheme given */
    if(ci_eq(s[0],'h')&&ci_eq(s[1],'t')&&ci_eq(s[2],'t')&&ci_eq(s[3],'p')){
        if(s[4]==':'&&s[5]=='/'&&s[6]=='/'){ *secure=0; s+=7; }
        else if(ci_eq(s[4],'s')&&s[5]==':'&&s[6]=='/'&&s[7]=='/'){ *secure=1; s+=8; } }
    int hi=0,pi=0; while(*s&&*s!='/'&&*s!=' '&&hi<127)host[hi++]=*s++; host[hi]=0;
    path[pi++]='/'; if(*s=='/')s++; while(*s&&*s!=' '&&pi<299)path[pi++]=*s++; path[pi]=0;
}
static int http_status(const u8*buf,int n){ int i=0; while(i<n&&buf[i]!=' ')i++; i++; int c=0; while(i<n&&buf[i]>='0'&&buf[i]<='9'){c=c*10+(buf[i]-'0');i++;} return c; }
static int find_hdr(const u8*buf,int hdrend,const char*name,char*out,int outmax){
    int nl=0; while(name[nl])nl++;
    for(int i=0;i<hdrend;i++){ if(i!=0&&buf[i-1]!='\n')continue; int k=0; while(k<nl&&i+k<hdrend&&ci_eq((char)buf[i+k],name[k]))k++;
        if(k==nl&&i+k<hdrend&&buf[i+k]==':'){ int j=i+k+1; while(j<hdrend&&(buf[j]==' '||buf[j]=='\t'))j++;
            int o=0; while(j<hdrend&&buf[j]!='\r'&&buf[j]!='\n'&&o<outmax-1)out[o++]=(char)buf[j++]; out[o]=0; return 1; } }
    return 0;
}
static void compose_present(void);   /* fwd, for loading screen */
static void urlcat(char*dst,const char*a,const char*b,const char*c){ int n=0; if(a)for(int i=0;a[i]&&n<255;i++)dst[n++]=a[i]; if(b)for(int i=0;b[i]&&n<255;i++)dst[n++]=b[i]; if(c)for(int i=0;c[i]&&n<255;i++)dst[n++]=c[i]; dst[n]=0; }
static void img_fullurl(const char*src,char*out){
    if(ci_eq(src[0],'h')&&ci_eq(src[1],'t')&&ci_eq(src[2],'t')&&ci_eq(src[3],'p')) urlcat(out,src,0,0);
    else if(src[0]=='/'&&src[1]=='/') urlcat(out,"https:",src,0);
    else if(src[0]=='/') urlcat(out,g_host,src,0);
    else urlcat(out,g_host,"/",src);
}
static void fetch_inline_images(void){
    if(web_nimg<=0) return;
    u64 start=g_ticks;
    for(int k=0;k<web_nimg;k++){
        if((u64)(g_ticks-start) > 1000) break;                 /* ~10s total budget */
        int dup=-1; for(int j=0;j<k;j++) if(webimg[j].ok && seq(webimg[j].src,webimg[k].src)){ dup=j; break; }
        if(dup>=0){ webimg[k].ok=1; webimg[k].off=webimg[dup].off; webimg[k].w=webimg[dup].w; webimg[k].h=webimg[dup].h; continue; }
        char full[320]; img_fullurl(webimg[k].src, full);
        int sec; char host[128],path[300]; url_split(full,&sec,host,path);
        if(host[0]==0) continue;
        { char b[80],*p=b; p=scopy(p,"Loading images "); p=udec(p,(u32)(k+1)); *p++='/'; p=udec(p,(u32)web_nimg); p=scopy(p,"..."); *p=0; scopy(web_stat,b); compose_present(); }
        int cs=(sec<0)?1:sec, fb=(sec<0)?1:0, n=0, done=0;
        for(int hop=0; hop<3 && !done; hop++){
            n = cs ? https_fetch_to(host,path,img_fetch_buf,sizeof(img_fetch_buf)) : http_fetch_to(host,path,img_fetch_buf,sizeof(img_fetch_buf));
            if(n<=0){ if(cs&&fb){ cs=0; fb=0; hop--; continue; } break; }
            int hdrend=n; for(int i=0;i+3<n;i++) if(img_fetch_buf[i]=='\r'&&img_fetch_buf[i+1]=='\n'&&img_fetch_buf[i+2]=='\r'&&img_fetch_buf[i+3]=='\n'){ hdrend=i+2; break; }
            int code=http_status(img_fetch_buf,n);
            if((code==301||code==302||code==303||code==307||code==308)&&hop<2){ char loc[300];
                if(find_hdr(img_fetch_buf,hdrend,"location",loc,300)){
                    if(ci_eq(loc[0],'h')&&ci_eq(loc[1],'t')&&ci_eq(loc[2],'t')&&ci_eq(loc[3],'p')){ int ns; char nh[128],np[300]; url_split(loc,&ns,nh,np); if(ns>=0)cs=ns; scopy(host,nh); scopy(path,np); }
                    else if(loc[0]=='/'){ scopy(path,loc); }
                    else { char t[300]; t[0]='/'; int q=1; for(int i=0;loc[i]&&q<299;i++)t[q++]=loc[i]; t[q]=0; scopy(path,t); }
                    fb=0; continue; } }
            int bo=hdrend+2; if(bo>n)bo=n; const u8* body=img_fetch_buf+bo; int blen=n-bo;
            int off=0,w=0,h=0; if(looks_image(body,blen)&&decode_into_pool(body,blen,&off,&w,&h)){ webimg[k].ok=1; webimg[k].off=off; webimg[k].w=w; webimg[k].h=h; }
            done=1;
        }
    }
    { int oks=0; for(int k=0;k<web_nimg;k++)if(webimg[k].ok)oks++; char b[64],*p=b; p=scopy(p,"Loaded "); p=udec(p,(u32)oks); *p++='/'; p=udec(p,(u32)web_nimg); p=scopy(p," images"); *p=0; scopy(web_stat,b); }
}
static int g_httplen=0, g_bodyoff=0, g_dln=0;
static int stristr(const char*h,const char*needle){ for(int i=0;h[i];i++){ int k=0; while(needle[k]&&ci_eq(h[i+k],needle[k]))k++; if(!needle[k])return 1; } return 0; }
static int dechunk_inplace(u8* b,int n){   /* HTTP chunked -> raw, in place; returns new length */
    int r=0,w=0;
    while(r<n){ int sz=0,any=0; while(r<n){ int v=hexv((char)b[r]); if(v<0)break; sz=sz*16+v; r++; any=1; }
        if(!any)break; while(r<n&&b[r]!='\n')r++; if(r<n)r++; if(sz==0)break;
        if(r+sz>n)sz=n-r; for(int i=0;i<sz;i++)b[w++]=b[r+i]; r+=sz;
        if(r<n&&b[r]=='\r')r++; if(r<n&&b[r]=='\n')r++; }
    return w;
}
static int gunzip(const u8* src,int n,u8* out,int outcap){   /* gzip -> raw; returns length or <0 */
    if(n<18||src[0]!=0x1f||src[1]!=0x8b||src[2]!=8) return -1;
    int flg=src[3],p=10;
    if(flg&4){ if(p+2>n)return -1; int xl=src[p]|(src[p+1]<<8); p+=2+xl; }
    if(flg&8){ while(p<n&&src[p])p++; p++; }
    if(flg&16){ while(p<n&&src[p])p++; p++; }
    if(flg&2) p+=2;
    if(p>=n) return -1;
    return pz_inflate_raw(src+p,n-p,out,outcap);
}
/* ---- bookmarks + history ---- */
#define BM_N 16
#define HIST_N 32
static char bmarks[BM_N][256]; static int nbmarks=0;
static char hist[HIST_N][256]; static int nhist=0;
static int bm_has(const char*u){ for(int i=0;i<nbmarks;i++)if(seq(bmarks[i],u))return 1; return 0; }
static void list_to_fs(const char*fname,const char*title,char list[][256],int cnt){
    static char tmp[9000]; int p=0; const char* h1="<h1>"; for(int i=0;h1[i];i++)tmp[p++]=h1[i];
    for(int i=0;title[i]&&p<8900;i++)tmp[p++]=title[i]; const char* h2="</h1>\n"; for(int i=0;h2[i];i++)tmp[p++]=h2[i];
    for(int k=cnt-1;k>=0&&p<8600;k--){ const char* a="<p><a href=\""; for(int i=0;a[i];i++)tmp[p++]=a[i];
        for(int i=0;list[k][i]&&p<8700;i++)tmp[p++]=list[k][i]; tmp[p++]='"'; tmp[p++]='>';
        for(int i=0;list[k][i]&&p<8850;i++)tmp[p++]=list[k][i]; const char* e="</a></p>\n"; for(int i=0;e[i];i++)tmp[p++]=e[i]; }
    tmp[p]=0; fs_save_bin(fname,(const u8*)tmp,p,0);
}
static void bm_save_fs(void){ list_to_fs("bookmarks.html","Bookmarks",bmarks,nbmarks); }
static void hist_save_fs(void){ list_to_fs("history.html","History",hist,nhist); }
static void hist_push(const char*u){ if(!u[0])return; if(nhist>0&&seq(hist[nhist-1],u))return;
    if(nhist<HIST_N)scopy(hist[nhist++],u); else { for(int i=1;i<HIST_N;i++)scopy(hist[i-1],hist[i]); scopy(hist[HIST_N-1],u); } hist_save_fs(); nvx_hist_add(u); }
static void bm_toggle(const char*u){ if(!u[0])return;
    for(int i=0;i<nbmarks;i++)if(seq(bmarks[i],u)){ for(int j=i+1;j<nbmarks;j++)scopy(bmarks[j-1],bmarks[j]); nbmarks--; bm_save_fs(); return; }
    if(nbmarks<BM_N)scopy(bmarks[nbmarks++],u); bm_save_fs(); }
static void render_local(const char*title,char list[][256],int cnt,const char*empty){
    static char b[9000]; int p=0; const char* h1="<h1>"; for(int i=0;h1[i];i++)b[p++]=h1[i];
    for(int i=0;title[i];i++)b[p++]=title[i]; const char* h2="</h1>\n"; for(int i=0;h2[i];i++)b[p++]=h2[i];
    if(cnt==0){ const char* e="<p>"; for(int i=0;e[i];i++)b[p++]=e[i]; for(int i=0;empty[i];i++)b[p++]=empty[i]; const char* ec="</p>"; for(int i=0;ec[i];i++)b[p++]=ec[i]; }
    for(int k=cnt-1;k>=0&&p<8600;k--){ const char* a="<p><a href=\""; for(int i=0;a[i];i++)b[p++]=a[i];
        for(int i=0;list[k][i]&&p<8700;i++)b[p++]=list[k][i]; b[p++]='"'; b[p++]='>';
        for(int i=0;list[k][i]&&p<8850;i++)b[p++]=list[k][i]; const char* e="</a></p>\n"; for(int i=0;e[i];i++)b[p++]=e[i]; }
    b[p]=0; g_isimg=0; g_page=3; web_scroll=0; g_host[0]=0; g_httplen=0; web_nimg=0;
    browser_render((const u8*)b,p); g_cururl[0]=0;
    int t=0; while(title[t]&&t<63){g_title[t]=title[t];t++;} g_title[t]=0; scopy(web_stat,title);
}
static void show_bookmarks(void){ render_local("Bookmarks",bmarks,nbmarks,"No bookmarks yet - open a page and click the star."); }
/* pull the signed-in account's history into hist[] (newest last) */
static void nvx_hist_fetch(void){
    if(!g_nvx_signedin) return;
    char hdr[360],*hp=hdr; hp=scopy(hp,"Authorization: Bearer ");hp=scopy(hp,g_nvx_token);hp=scopy(hp,"\r\n");*hp=0;
    int n=https_request(NVX_HOST,"/api/public/history","GET",hdr,0,0,http_buf,(int)sizeof(http_buf));
    if(n<=0) return; int he=0; for(int i=0;i+3<n;i++)if(http_buf[i]=='\r'&&http_buf[i+1]=='\n'&&http_buf[i+2]=='\r'&&http_buf[i+3]=='\n'){he=i+4;break;}
    int cnt=0; /* extract each "url":"..." into hist[] */
    for(int i=he;i+6<n && cnt<HIST_N;i++){
        if(http_buf[i]=='"'&&http_buf[i+1]=='u'&&http_buf[i+2]=='r'&&http_buf[i+3]=='l'&&http_buf[i+4]=='"'){
            int p=i+5; while(p<n&&http_buf[p]!=':')p++; p++; while(p<n&&(http_buf[p]==' '||http_buf[p]=='"'))p++;
            int o=0; while(p<n&&http_buf[p]!='"'&&o<255){ if(http_buf[p]=='\\'&&p+1<n)p++; hist[cnt][o++]=(char)http_buf[p]; p++; }
            hist[cnt][o]=0; if(o>0)cnt++; i=p; }
    }
    if(cnt>0) nhist=cnt;
}
static void show_history(void){ nvx_hist_fetch(); render_local("History",hist,nhist,"No history yet."); }
static void dl_name_from_url(const char*url,char*out){
    int end=0; for(int i=0;url[i];i++){ if(url[i]=='?'||url[i]=='#'){ end=i; break; } end=i+1; }
    int slash=-1; for(int i=0;i<end;i++) if(url[i]=='/') slash=i;
    int st=slash+1, n=0; for(int i=st;i<end&&n<23;i++){ char c=url[i]; if(c==':'||c=='\\'||c=='"')continue; out[n++]=c; }
    out[n]=0; if(n==0)scopy(out,"download");
}
static int has_ext(const char*url,const char*ext){
    int end=0; for(int i=0;url[i];i++){ if(url[i]=='?'||url[i]=='#'){ end=i; break; } end=i+1; }
    int dot=-1; for(int i=0;i<end;i++) if(url[i]=='.') dot=i; if(dot<0)return 0;
    int j=dot+1; for(int k=0;ext[k];k++,j++){ if(j>=end)return 0; if(!ci_eq(url[j],ext[k]))return 0; }
    return j==end;
}
static int is_dl_ext(const char*url){
    static const char*E[]={"zip","gz","tgz","tar","rar","7z","pdf","bin","iso","img","exe","msi","dmg","deb","rpm","apk",
        "mp3","m4a","wav","flac","ogg","mp4","mkv","mov","avi","doc","docx","xls","xlsx","ppt","pptx","csv","epub",0};
    for(int i=0;E[i];i++) if(has_ext(url,E[i]))return 1; return 0;
}
static int is_img_ext(const char*url){ return has_ext(url,"png")||has_ext(url,"jpg")||has_ext(url,"jpeg")||has_ext(url,"gif")||has_ext(url,"bmp")||has_ext(url,"webp")||has_ext(url,"svg"); }
static void browser_download(const char*url){
    int sec; char host[128],path[300]; url_split(url,&sec,host,path);
    if(host[0]==0){ scopy(web_stat,"Bad download URL"); return; }
    char dn[24]; dl_name_from_url(url,dn);
    { char b[80],*q=b; q=scopy(q,"Downloading "); q=scopy(q,dn); q=scopy(q,"..."); *q=0; scopy(web_stat,b); } compose_present();
    int cur_sec=(sec<0)?1:sec, http_fb=(sec<0)?1:0, n=0;
    for(int hop=0;hop<7;hop++){
        n = cur_sec ? https_fetch_to(host,path,imgpool,IMGPOOL_SZ) : http_fetch_to(host,path,imgpool,IMGPOOL_SZ);
        if(n<=0){ if(cur_sec&&http_fb){ cur_sec=0; http_fb=0; continue; } scopy(web_stat,"Download failed - run netlog for detail"); return; }
        int hdrend=n; for(int i=0;i+3<n;i++) if(imgpool[i]=='\r'&&imgpool[i+1]=='\n'&&imgpool[i+2]=='\r'&&imgpool[i+3]=='\n'){ hdrend=i+2; break; }
        int code=http_status(imgpool,n);
        if((code==301||code==302||code==303||code==307||code==308)&&hop<6){
            char loc[300]; if(find_hdr(imgpool,hdrend,"location",loc,300)){
                if(ci_eq(loc[0],'h')&&ci_eq(loc[1],'t')&&ci_eq(loc[2],'t')&&ci_eq(loc[3],'p')){ int nsec; char nh[128],np[300]; url_split(loc,&nsec,nh,np); if(nsec>=0)cur_sec=nsec; scopy(host,nh); scopy(path,np); }
                else if(loc[0]=='/'){ scopy(path,loc); }
                else { char t[300]; t[0]='/'; int j=1; for(int i=0;loc[i]&&j<299;i++)t[j++]=loc[i]; t[j]=0; scopy(path,t); }
                http_fb=0; continue; } }
        int bo=hdrend+2; if(bo>n)bo=n; int blen=n-bo;
        { char te[48]; if(find_hdr(imgpool,hdrend,"transfer-encoding",te,48)&&stristr(te,"chunked"))blen=dechunk_inplace(imgpool+bo,blen); }
        { char ce[48]; if(find_hdr(imgpool,hdrend,"content-encoding",ce,48)&&stristr(ce,"gzip")){ int dl=gunzip(imgpool+bo,blen,jpeg_scratch,(int)sizeof(jpeg_scratch)); if(dl>0){ int cap=(int)IMGPOOL_SZ-bo-1; if(dl>cap)dl=cap; for(int i=0;i<dl;i++)imgpool[bo+i]=jpeg_scratch[i]; blen=dl; } } }
        int slot; { int hasdot=0; for(int i=0;dn[i];i++) if(dn[i]=='.')hasdot=1;
            if(!hasdot && blen>=4){ const u8*b2=imgpool+bo; const char*ext=0;
                if(b2[0]==0x89&&b2[1]==0x50&&b2[2]==0x4E&&b2[3]==0x47)ext=".png"; else if(b2[0]==0xFF&&b2[1]==0xD8&&b2[2]==0xFF)ext=".jpg"; else if(blen>=12&&b2[0]=='R'&&b2[1]=='I'&&b2[2]=='F'&&b2[3]=='F'&&b2[8]=='W'&&b2[9]=='E'&&b2[10]=='B'&&b2[11]=='P')ext=".webp";
                else if(b2[0]==0x50&&b2[1]==0x4B&&b2[2]==0x03&&b2[3]==0x04)ext=".zip"; else if(b2[0]=='G'&&b2[1]=='I'&&b2[2]=='F')ext=".gif"; else if(b2[0]=='B'&&b2[1]=='M')ext=".bmp";
                if(ext){ int L=0; while(dn[L]&&L<19)L++; for(int j=0;ext[j]&&L<23;j++)dn[L++]=ext[j]; dn[L]=0; } } }
        slot=fs_save_bin(dn,imgpool+bo,blen,1);
        if(slot>=0){ char b[80],*q=b; q=scopy(q,"Downloaded "); q=scopy(q,dn); q=scopy(q," ("); q=udec(q,(u32)blen); q=scopy(q," B) to Downloads"); *q=0; scopy(web_stat,b); toast("Download complete"); }
        else scopy(web_stat,"Download failed (disk full)");
        return;
    }
    scopy(web_stat,"Too many redirects");
}
static int looks_url(const char*s){
    if(!s[0])return 1;
    if((s[0]|32)=='h'&&(s[1]|32)=='t'&&(s[2]|32)=='t'&&(s[3]|32)=='p')return 1;
    int hasdot=0,hasspace=0; for(int i=0;s[i];i++){ if(s[i]=='.')hasdot=1; if(s[i]==' ')hasspace=1; }
    if(hasspace)return 0;
    if(!hasdot){ if(seq(s,"localhost"))return 1; return 0; }
    return 1;
}
static void urlencode(const char*s,char*out,int cap){ int o=0; const char* hx="0123456789ABCDEF";
    for(int i=0;s[i]&&o<cap-4;i++){ unsigned char ch=(unsigned char)s[i];
        if((ch>='a'&&ch<='z')||(ch>='A'&&ch<='Z')||(ch>='0'&&ch<='9')||ch=='-'||ch=='_'||ch=='.'||ch=='~')out[o++]=(char)ch;
        else if(ch==' ')out[o++]='+';
        else { out[o++]='%'; out[o++]=hx[ch>>4]; out[o++]=hx[ch&15]; } }
    out[o]=0;
}
static void set_wrapcols(void){ if(!g_gm_ready)gmetrics(); int w=WN[W_BROWSER].w-60; if(w<240)w=800; g_wrappx=w; if(g_wrappx>860)g_wrappx=860; if(g_wrappx<200)g_wrappx=200;
    int c=w/(8*g_webzoom); if(c>WEB_COLS-2)c=WEB_COLS-2; if(c<20)c=20; g_wrapcols=c; }
static void rewrap_render(void){ set_wrapcols(); if(g_httplen>g_bodyoff && g_page==3 && !g_isimg){ int save=web_scroll; browser_render(http_buf+g_bodyoff,g_httplen-g_bodyoff); if(save>=web_nl)save=web_nl>0?web_nl-1:0; web_scroll=save; } }
static void browser_error(const char*msg){
    web_nl=0; web_nlink=0; web_nbold=0; web_ncol=0; web_nanch=0; web_nimg=0; web_scroll=0; g_isimg=0; g_havebg=0;
    scopy(WEBL[0],"Can't reach this page"); WEBST[0]=1;
    WEBL[1][0]=0; WEBST[1]=0;
    { char b[120]; int e=0; while(msg[e]&&e<118){b[e]=msg[e];e++;} b[e]=0; scopy(WEBL[2],b); } WEBST[2]=0;
    WEBL[3][0]=0; WEBST[3]=0;
    { char b[300],*p=b; p=scopy(p,"URL: "); int e=0; while(g_url[e]&&(p-b)<290)*p++=g_url[e++]; *p=0; scopy(WEBL[4],b); } WEBST[4]=0;
    scopy(WEBL[5],"Check the address, or run 'netlog' in Terminal for detail."); WEBST[5]=0;
    web_nl=6; scopy(web_stat,"Error"); scopy(g_title,"Can't reach page");
}
static const char JS_DEMO[]=
"<html><head><style>body{background:#0f1522} h1{color:#5aa0ff} h2{color:#9ad0ff}</style></head><body>"
"<h1>NoovexOS JavaScript</h1>"
"<p>This page is HTML, but every number below is computed live by the built-in integer JavaScript interpreter.</p>"
"<h2>Loop</h2><script>document.write('Counting: '); for(var i=1;i<=12;i++){ document.write(i+' '); }</script>"
"<h2>Recursion (factorials)</h2><script>function fact(n){ if(n<=1) return 1; return n*fact(n-1); } for(var i=1;i<=9;i++){ document.write(i+'! = '+fact(i)+'\\n'); }</script>"
"<h2>Fibonacci</h2><script>function fib(n){ if(n<2) return n; return fib(n-1)+fib(n-2); } var o=''; for(var i=0;i<14;i++){ o=o+fib(i)+' '; } document.write(o);</script>"
"<h2>Strings + math</h2><script>var s='noovexos'; document.write(s.toUpperCase()+' has '+s.length+' letters. '); document.write('max(7,3)='+Math.max(7,3)+', sqrt(144)='+Math.sqrt(144)+', 17%5='+(17%5));</script>"
"<h2>Arrays</h2><script>var a=[]; for(var i=1;i<=6;i++){ a.push(i*i); } document.write('squares = ['+a.join(', ')+'], length '+a.length+', indexOf(16)='+a.indexOf(16));</script>"
"<h2>Objects</h2><script>var os={name:'NoovexOS', bits:64, apps:['Files','Browser','DOOM']}; document.write(os.name+' is '+os.bits+'-bit and ships: '+os.apps.join(' / '));</script>"
"<p>Integers only (no decimals yet). Loops, functions, recursion, strings, arrays, objects and Math all run natively.</p></body></html>";
static void browser_show_html(const char* html,int len,const char* title){
    g_urlfocus=0; g_page=3; web_scroll=0; g_isimg=0; imgpool_off=0; web_nimg=0;
    int cap=(int)sizeof(http_buf)-1; if(len>cap)len=cap; for(int i=0;i<len;i++)http_buf[i]=(u8)html[i]; http_buf[len]=0;
    g_bodyoff=0; g_httplen=len; g_css_ext_n=0; g_cssvar_ext_n=0; set_pagebg(http_buf,len); browser_render(http_buf,len);
    scopy(web_stat, (char*)(title?title:"local"));
    int ti=0; if(title){ while(title[ti]&&ti<62){ g_title[ti]=title[ti]; ti++; } } g_title[ti]=0;
    open_win(W_BROWSER);
}
static void fetch_external_css(const u8* html, int n){
    g_css_ext_n=0; css_n=0; cssvar_n=0; int found=0;
    for(int p=0; p+6<n && found<4; p++){
        if(html[p]=='<' && (html[p+1]|32)=='l' && (html[p+2]|32)=='i' && (html[p+3]|32)=='n' && (html[p+4]|32)=='k' && (html[p+5]==' '||html[p+5]=='\t'||html[p+5]=='\n')){
            int e=p+5; while(e<n && html[e]!='>') e++;
            if(elem_has(html,p+5,e,"stylesheet")){
                char href[256]; attr_get(html,p+5,e,"href",href,sizeof(href));
                if(href[0]){ char full[420]; full[0]=0;
                    if(ci_eq(href[0],'h')&&ci_eq(href[1],'t')&&ci_eq(href[2],'t')&&ci_eq(href[3],'p')) urlcat(full,href,0,0);
                    else if(href[0]=='/'&&href[1]=='/') urlcat(full,(char*)"https:",href,0);
                    else if(href[0]=='/') urlcat(full,g_host,href,0);
                    else urlcat(full,g_host,(char*)"/",href);
                    int sec; char host[128],path[300]; url_split(full,&sec,host,path);
                    if(host[0]){ int cn = (sec>0)? https_fetch_to(host,path,imgpool,IMGPOOL_SZ) : http_fetch_to(host,path,imgpool,IMGPOOL_SZ);
                        if(cn>0){ int he=cn; for(int i=0;i+3<cn;i++) if(imgpool[i]=='\r'&&imgpool[i+1]=='\n'&&imgpool[i+2]=='\r'&&imgpool[i+3]=='\n'){ he=i+2; break; }
                            int bo=he+2; if(bo>cn)bo=cn; int cl=cn-bo;
                            char te[48]; if(find_hdr(imgpool,he,"transfer-encoding",te,48)&&stristr(te,"chunked")) cl=dechunk_inplace(imgpool+bo,cl);
                            char ce[48]; if(find_hdr(imgpool,he,"content-encoding",ce,48)&&stristr(ce,"gzip")){ int dl=gunzip(imgpool+bo,cl,jpeg_scratch,(int)sizeof(jpeg_scratch)); if(dl>0)css_parse(jpeg_scratch,0,dl); }
                            else if(cl>0) css_parse(imgpool+bo,0,cl); } }
                    found++; }
            }
            p=e;
        }
    }
    g_css_ext_n=css_n; g_cssvar_ext_n=cssvar_n; imgpool_off=0;
}
static void browser_go(int push){
    if((g_url[0]=='a'&&g_url[1]=='b'&&g_url[2]=='o'&&g_url[3]=='u'&&g_url[4]=='t'&&g_url[5]==':')){
        const char* p=g_url+6; if((p[0]|32)=='j'&&(p[1]|32)=='s'){ scopy(g_cururl,g_url); browser_show_html(JS_DEMO,(int)sizeof(JS_DEMO)-1,"about:js"); return; }
        if((p[0]|32)=='b'){ browser_show_html("<html><body></body></html>",26,"about:blank"); return; } }
    g_urlfocus=0; g_urlsel=1; g_page=3; web_scroll=0; g_isimg=0; imgpool_off=0; web_nimg=0;
    if(!looks_url(g_url)){ char q[300]; int qi=0,s2=0; while(g_url[s2]==' ')s2++; while(g_url[s2]&&qi<298)q[qi++]=g_url[s2++]; while(qi>0&&q[qi-1]==' ')qi--; q[qi]=0;
        char enc[400]; urlencode(q,enc,sizeof(enc)); char su[512]; char*p=scopy(su,"https://html.duckduckgo.com/html/?q="); int e=0; while(enc[e]&&(p-su)<500)*p++=enc[e++]; *p=0; scopy(g_url,su); g_urllen=slen(g_url); }
    int sec; char host[128],path[300]; url_split(g_url,&sec,host,path);
    if(host[0]==0){ web_nl=0; web_nlink=0; scopy(WEBL[0],"Enter a URL, e.g. mynexus.site"); WEBST[0]=0; web_nl=1; scopy(web_stat,"Ready"); return; }
    if(push && g_cururl[0] && !seq(g_cururl,g_url)){ if(backn<16)scopy(backstk[backn++],g_cururl); else { for(int i=1;i<16;i++)scopy(backstk[i-1],backstk[i]); scopy(backstk[15],g_cururl); } }
    scopy(g_host,host);
    web_nl=0; web_nlink=0; { char b[80],*p=b; p=scopy(p,"Loading "); p=scopy(p,host); scopy(p,"..."); scopy(WEBL[0],b); } WEBST[0]=0; web_nl=1;
    { char b[96],*p=b; p=scopy(p,"Connecting to "); p=scopy(p,host); *p=0; scopy(web_stat,b); }
    compose_present();
    int cur_sec=(sec<0)?1:sec, http_fb=(sec<0)?1:0, n=0, retried=0;
    for(int hop=0;hop<7;hop++){
        n = cur_sec ? https_fetch(host,path) : http_fetch(host,path);
        if(n<=0){ if(cur_sec&&http_fb){ cur_sec=0; http_fb=0; continue; }
            if((n==-2||n==-3||n==-4)&&!retried){ retried=1; netlog("retry after transient error"); nic_kick(); nic_flush_rx(); hop--; continue; }
            char*p=web_stat; p=scopy(p,cur_sec?"HTTPS error: ":"HTTP error: ");
            if(n==-100)scopy(p,"could not resolve host (DNS)"); else if(n==-1)scopy(p,"no NIC - set VM to PCnet or e1000"); else if(n==-2)scopy(p,"could not connect"); else if(n==-3)scopy(p,"timed out"); else if(n==-4)scopy(p,"refused (RST)"); else { p=scopy(p,"code -"); udec(p,(u32)(-n)); }
            const char* em; if(n==-100)em="The site name could not be resolved (DNS lookup failed)."; else if(n==-1)em="No network card detected - set the VM network to PCnet or e1000."; else if(n==-2)em="Could not connect to the server."; else if(n==-3)em="The connection timed out."; else if(n==-4)em="The connection was refused (reset by peer)."; else em="The request failed.";
            browser_error(em); return; }
        int hdrend=n; for(int i=0;i+3<n;i++) if(http_buf[i]=='\r'&&http_buf[i+1]=='\n'&&http_buf[i+2]=='\r'&&http_buf[i+3]=='\n'){ hdrend=i+2; break; }
        int code=http_status(http_buf,n);
        if((code==301||code==302||code==303||code==307||code==308)&&hop<6){
            char loc[300]; if(find_hdr(http_buf,hdrend,"location",loc,300)){
                if(ci_eq(loc[0],'h')&&ci_eq(loc[1],'t')&&ci_eq(loc[2],'t')&&ci_eq(loc[3],'p')){ int nsec; char nh[128],np[300]; url_split(loc,&nsec,nh,np); if(nsec>=0)cur_sec=nsec; scopy(host,nh); scopy(path,np); scopy(g_host,nh); }
                else if(loc[0]=='/'){ scopy(path,loc); }
                else { char t[300]; t[0]='/'; int j=1; for(int i=0;loc[i]&&j<299;i++)t[j++]=loc[i]; t[j]=0; scopy(path,t); }
                http_fb=0; continue; } }
        g_httplen=n; g_bodyoff=hdrend+2; if(g_bodyoff>n)g_bodyoff=n;
        { int e=0; char*p=web_stat; while(e<n && http_buf[e]!='\r'&&http_buf[e]!='\n' && (p-web_stat)<78){ *p++=http_buf[e]; e++; } *p=0; }
        int blen=n-g_bodyoff;
        { char te[48]; if(find_hdr(http_buf,hdrend,"transfer-encoding",te,48)&&stristr(te,"chunked")){ blen=dechunk_inplace(http_buf+g_bodyoff,blen); netlog("de-chunked body"); } }
        { char ce[48]; if(find_hdr(http_buf,hdrend,"content-encoding",ce,48)&&stristr(ce,"gzip")){
            int dl=gunzip(http_buf+g_bodyoff,blen,jpeg_scratch,(int)sizeof(jpeg_scratch));
            if(dl>0){ int cap=(int)sizeof(http_buf)-g_bodyoff-1; if(dl>cap)dl=cap; for(int i=0;i<dl;i++)http_buf[g_bodyoff+i]=jpeg_scratch[i]; blen=dl; netlog("gunzip ok"); } else netlog("gunzip failed"); } }
        g_httplen=g_bodyoff+blen; http_buf[g_bodyoff+blen]=0;
        { char cd[160]; if(find_hdr(http_buf,hdrend,"content-disposition",cd,160)&&stristr(cd,"attachment")){
            char dn[24]; int fn=-1; for(int i=0;cd[i]&&cd[i+7];i++){ if((cd[i]|32)=='f'&&(cd[i+1]|32)=='i'&&(cd[i+2]|32)=='l'&&(cd[i+3]|32)=='e'&&(cd[i+4]|32)=='n'&&(cd[i+5]|32)=='a'&&(cd[i+6]|32)=='m'&&(cd[i+7]|32)=='e'){ fn=i+8; break; } }
            if(fn>=0){ while(cd[fn]&&(cd[fn]=='='||cd[fn]==' '||cd[fn]=='"'||cd[fn]=='\''))fn++; int n2=0; while(cd[fn]&&cd[fn]!='"'&&cd[fn]!='\''&&cd[fn]!=';'&&n2<23)dn[n2++]=cd[fn++]; dn[n2]=0; if(n2==0)dl_name_from_url(g_url,dn); } else dl_name_from_url(g_url,dn);
            int slot=fs_save_bin(dn,http_buf+g_bodyoff,blen,1); web_nl=0; web_nlink=0; g_isimg=0;
            if(slot>=0){ char b[64],*q=b; q=scopy(q,"Downloaded "); q=scopy(q,dn); q=scopy(q," to Downloads"); *q=0; scopy(WEBL[0],b); scopy(web_stat,b); toast("Download complete"); } else { scopy(WEBL[0],"Download failed (disk full)"); scopy(web_stat,"disk full"); }
            WEBST[0]=0; web_nl=1; scopy(g_cururl,g_url); hist_push(g_url); return; } }
        const u8* body=http_buf+g_bodyoff;
        if(looks_image(body,blen)){
            if(decode_image(body,blen)){ g_isimg=1; web_nl=0; web_nlink=0; char b[48],*p=b; p=scopy(p,"Image  "); p=udec(p,(u32)img_w);*p++='x';p=udec(p,(u32)img_h); *p=0; scopy(web_stat,b); }
            else { g_isimg=0; web_nl=0; web_nlink=0; scopy(WEBL[0],"(image could not be decoded - baseline JPEG / truecolor PNG only)"); WEBST[0]=0; web_nl=1; } }
        else { g_isimg=0; fetch_external_css(body,blen); browser_render(body,blen); }
        scopy(g_cururl,g_url); hist_push(g_url);
        if(!g_isimg) fetch_inline_images();
        return;
    }
    scopy(web_stat,"Too many redirects");
}
static void nvx_open_url(const char* u){ scopy(g_url,u); g_urllen=slen(g_url); open_win(W_BROWSER); browser_go(1); }
/* ---- browser tabs (snapshot cache) ---- */
#define NTAB 4
typedef struct { int used,isimg,imgw,imgh,nl,scroll,page,nlink,hrefn,backn,havebg,pagedark,nimg; u32 pagebg;
    char url[256],cururl[256],host[128],stat[80],title[64];
    char L[WEB_MAXL][WEB_COLS]; u8 ST[WEB_MAXL];
    struct{short line,c0,c1;int href;} link[WEB_MAXLINK]; char href[4096];
    char backstk[16][256]; } TabSt;
static TabSt tabs[NTAB]; static int g_tab=0, g_ntab=1;
static int tab_x[NTAB],tab_w=0,tab_plusx,tab_savex,tab_savew;
static void save_tab(int t){ TabSt*T=&tabs[t]; T->used=1; T->isimg=g_isimg; T->imgw=img_w; T->imgh=img_h;
    T->nl=web_nl; T->scroll=web_scroll; T->page=g_page; T->nlink=web_nlink; T->hrefn=webhref_n; T->backn=backn; T->havebg=g_havebg; T->pagedark=g_pagedark; T->pagebg=g_pagebg; T->nimg=web_nimg;
    scopy(T->url,g_url); scopy(T->cururl,g_cururl); scopy(T->host,g_host); scopy(T->stat,web_stat); scopy(T->title,g_title);
    memcpy(T->L,WEBL,sizeof(WEBL)); memcpy(T->ST,WEBST,sizeof(WEBST)); memcpy(T->link,weblink,sizeof(weblink)); memcpy(T->href,webhref,sizeof(webhref)); memcpy(T->backstk,backstk,sizeof(backstk)); }
static void load_tab(int t){ TabSt*T=&tabs[t];
    scopy(g_cururl,T->cururl); scopy(g_host,T->host); scopy(web_stat,T->stat); scopy(g_title,T->title);
    g_page=T->page; backn=T->backn; g_havebg=T->havebg; g_pagedark=T->pagedark; g_pagebg=T->pagebg; memcpy(backstk,T->backstk,sizeof(backstk));
    if((T->isimg||T->nimg>0) && T->cururl[0]){ scopy(g_url,T->cururl); g_urllen=slen(g_url); browser_go(0); return; }
    scopy(g_url,T->url); g_urllen=slen(g_url); g_isimg=0; web_nimg=0; web_nl=T->nl; web_scroll=T->scroll; web_nlink=T->nlink; webhref_n=T->hrefn;
    memcpy(WEBL,T->L,sizeof(WEBL)); memcpy(WEBST,T->ST,sizeof(WEBST)); memcpy(weblink,T->link,sizeof(weblink)); memcpy(webhref,T->href,sizeof(webhref)); }
static void tab_switch(int t){ if(t==g_tab||t<0||t>=g_ntab)return; save_tab(g_tab); g_tab=t; load_tab(t); }
static void tab_new(void){ if(g_ntab>=NTAB)return; save_tab(g_tab); int t=g_ntab++; g_tab=t; tabs[t].used=1; tabs[t].host[0]=0;
    g_url[0]=0; g_urllen=0; g_cururl[0]=0; g_host[0]=0; g_title[0]=0; g_isimg=0; web_nl=0; web_nlink=0; web_scroll=0; g_page=0; backn=0; scopy(web_stat,"New Tab"); }
static void tab_close(int t){ if(g_ntab<=1||t<0||t>=g_ntab)return; for(int k=t;k<g_ntab-1;k++)tabs[k]=tabs[k+1]; g_ntab--; if(g_tab>=g_ntab)g_tab=g_ntab-1; else if(g_tab>t)g_tab--; load_tab(g_tab); }
static void browser_save(void){
    if(g_page!=3||g_httplen<=0){ scopy(web_stat,"Nothing to save"); return; }
    const u8* body=http_buf+g_bodyoff; int blen=g_httplen-g_bodyoff; if(blen<0)blen=0;
    char nm[24],*p=nm; p=scopy(p,"page"); p=udec(p,(u32)(++g_dln)); scopy(p, g_isimg?(http_buf[g_bodyoff]==0x89?".png":".jpg"):".html");
    int slot=fs_save_bin(nm,body,blen,1);   /* folder 1 = Downloads */
    if(slot>=0){ char b[64],*q=b; q=scopy(q,"Saved to Files: "); q=scopy(q,nm); *q=0; scopy(web_stat,b); }
    else scopy(web_stat,"Save failed (disk full)");
}
static int vN=0,vLine[80],vY[80],vSc[80],vX0[80];
static int g_findmode=0; static char g_findstr[48]=""; static int g_findlen=0;
static int g_findml[64], g_findmc[64], g_findn=0, g_findcur=0;
static char g_hoverhref[256]="";
static int g_starx,g_stary,g_starw, g_findbx,g_findby,g_findbw;
static void find_run(void){ g_findn=0; g_findcur=0; if(g_findlen==0)return;
    for(int L=0;L<web_nl && g_findn<64;L++){ for(int c=0;WEBL[L][c];c++){ int k=0; while(g_findstr[k]&&WEBL[L][c+k]&&ci_eq(WEBL[L][c+k],g_findstr[k]))k++;
        if(!g_findstr[k]){ g_findml[g_findn]=L; g_findmc[g_findn]=c; g_findn++; break; } } }
    if(g_findn>0)web_scroll=g_findml[0];
}
static void find_next(void){ if(g_findn==0)return; g_findcur=(g_findcur+1)%g_findn; web_scroll=g_findml[g_findcur]; }
/* top margin above headings (real section spacing) */
static int web_margin(int lvl){ return (lvl==1)?18:(lvl==2)?13:(lvl==3)?9:0; }
static void browser_draw(int i){
    win_frame(i,"Browser"); if(!g_gm_ready)gmetrics();
    int x=WN[i].x+12, w=WN[i].w-24;
    int tsy=WN[i].y+TITLEH+6, tsh=26;
    tab_w=(w-30)/g_ntab; if(tab_w>172)tab_w=172; if(tab_w<70)tab_w=70;
    for(int k=0;k<g_ntab;k++){ int tx=x+k*tab_w; tab_x[k]=tx; int act=(k==g_tab);
        fill_round(tx,tsy,tab_w-4,tsh,7,15, act?rgb(255,255,255):rgb(226,230,240));
        const char* ti=(k==g_tab)?(g_title[0]?g_title:(g_host[0]?g_host:(g_page==0?"New Tab":"Loading"))):(tabs[k].title[0]?tabs[k].title:(tabs[k].host[0]?tabs[k].host:"New Tab"));
        char tt[24]; int tn=0; for(int q=0;ti[q]&&tn<19;q++)tt[tn++]=ti[q]; tt[tn]=0;
        dstr(tx+10,tsy+6,tt, act?INK:rgb(90,98,116),1);
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
    int abx=g_starx+g_starw+8; int barw=w-(abx-x)-292; br_barx=abx; br_bary=y; br_barw=barw;
    fill_round(abx,y,barw,28,6,15,g_urlfocus?rgb(255,255,255):rgb(232,236,244));
    if(g_urlfocus){ for(int t=0;t<28;t++){ SCENE[(u64)(y+t)*SW+abx]=ACC; SCENE[(u64)(y+t)*SW+abx+barw-1]=ACC; } }
    { int maxch=(barw-20)/8; int st=0; if(g_urllen>maxch)st=g_urllen-maxch;
      if(g_urlfocus&&g_urlsel&&g_urllen>0){ int visw=(g_urllen-st)*8; if(visw>barw-16)visw=barw-16; fill_rect(abx+9,y+6,visw+2,16,rgb(178,206,248)); }
      dstr(abx+10,y+7,g_url+st,rgb(50,60,80),1);
      g_acsug[0]=0;
      if(g_urlfocus&&!g_urlsel&&g_urllen>0){ for(int hh=nhist-1;hh>=0;hh--){ int m=1; for(int q=0;q<g_urllen;q++){ if(tolow_(hist[hh][q])!=tolow_(g_url[q])){m=0;break;} } if(m&&slen(hist[hh])>g_urllen){ scopy(g_acsug,hist[hh]); break; } }
        if(g_acsug[0]){ int cxp=abx+10+(g_urllen-st)*8; int rem=(barw-20)/8-(g_urllen-st); if(rem>0){ char sg[128]; int sn=0; for(int q=g_urllen;g_acsug[q]&&sn<rem&&sn<126;q++)sg[sn++]=g_acsug[q]; sg[sn]=0; dstr(cxp,y+7,sg,rgb(170,178,192),1); } } }
      if(g_urlfocus&&!g_urlsel&&((g_ticks/50)&1)){ int cxp=abx+10+(g_urllen-st)*8; fill_rect(cxp,y+8,2,14,ACC); } }
    br_gx=abx+barw+8; br_gw=48; fill_round(br_gx,y,br_gw,28,6,15,ACC); dstr_ctr(br_gx+br_gw/2,y+7,"Go",rgb(255,255,255),1);
    tab_savex=br_gx+br_gw+6; tab_savew=52; fill_round(tab_savex,y,tab_savew,28,6,15,rgb(224,228,238)); dstr_ctr(tab_savex+tab_savew/2,y+7,"Save",INK,1);
    br_hx=tab_savex+tab_savew+6; br_hw=52; fill_round(br_hx,y,br_hw,28,6,15,rgb(224,228,238)); dstr_ctr(br_hx+br_hw/2,y+7,"Home",INK,1);
    g_findbx=br_hx+br_hw+6; g_findbw=52; g_findby=y; fill_round(g_findbx,y,g_findbw,28,6,15,g_findmode?mix(ACC,rgb(255,255,255),150):rgb(224,228,238)); dstr_ctr(g_findbx+g_findbw/2,y+7,"Find",INK,1);
    g_darkbx=g_findbx+g_findbw+6; g_darkbw=52; fill_round(g_darkbx,y,g_darkbw,28,6,15,g_forcedark?rgb(40,42,52):rgb(224,228,238)); dstr_ctr(g_darkbx+g_darkbw/2,y+7,"Dark",g_forcedark?rgb(235,238,246):INK,1);
    int cy=y+40, cx=x+18; if(g_page==3&&!g_isimg){ if(g_hasleft){ cx=x+18+g_leftw+18; } else if(!g_hasfloat){ int contentw=w-36, colw=g_wrappx; if(colw>contentw)colw=contentw; cx=x+18+(contentw-colw)/2; } } lk_n=0; lk_x=cx; lk_w=w-36; g_hoverhref[0]=0;
    int ch=WN[i].h-(cy-8-WN[i].y)-12; fill_round(x,cy-8,w,ch,8,15,(g_page==3&&!g_isimg&&g_havebg)?g_pagebg:rgb(255,255,255));
    if(g_page==3 && g_isimg){
        dstr(cx,cy-2,web_stat[0]?web_stat:"...",rgb(120,130,150),1);
        int aw=w-16, ah=ch-30, sw=img_w, sh=img_h, scn=1000;
        if(sw*scn/1000>aw) scn=aw*1000/sw; if(sh*scn/1000>ah) scn=ah*1000/sh;
        int dw=sw*scn/1000, dh=sh*scn/1000; if(dw<1)dw=1; if(dh<1)dh=1; int ox=x+8+(aw-dw)/2, oy=cy+20+(ah-dh)/2;
        for(int r=0;r<dh;r++){ int dy=oy+r; if(dy<WN[i].y||dy>=WN[i].y+WN[i].h-4)continue; int srcr=r*sh/dh;
            for(int c=0;c<dw;c++){ int dx=ox+c; if(dx<x||dx>=x+w)continue; int srcc=c*sw/dw; u8*pp=&imgpool[((srcr*sw)+srcc)*4]; putpx(dx,dy,rgb(pp[0],pp[1],pp[2])); } }
    } else if(g_page==3){
        u32 bodyc=g_pagedark?rgb(224,228,238):rgb(34,40,54), h1c=g_pagedark?rgb(255,255,255):INK, h2c=g_pagedark?rgb(216,222,234):rgb(48,54,70), linkc=g_pagedark?rgb(120,182,255):ACC;
        u32 pgbg=(g_havebg)?g_pagebg:rgb(255,255,255);
        dstr(cx,cy-2,web_stat[0]?web_stat:"...",g_pagedark?rgb(150,160,180):rgb(120,130,150),1);
        int ly=cy+22, maxy=WN[i].y+WN[i].h-16; vN=0;
        for(int line=web_scroll; line<web_nl && ly<maxy; line++){
            for(int im=0;im<web_nimg;im++) if(webimg[im].line==line){
                if(webimg[im].ok && webimg[im].w>0 && webimg[im].h>0){
                    int iw=webimg[im].w, ih=webimg[im].h, aw=w-24, scn=1000;
                    if(iw*scn/1000>aw)scn=aw*1000/iw; int cap=340; if(ih*scn/1000>cap)scn=cap*1000/ih;
                    int dw=iw*scn/1000, dh=ih*scn/1000; if(dw<1)dw=1; if(dh<1)dh=1; u8* src=imgpool+webimg[im].off;
                    for(int r=0;r<dh;r++){ int dy=ly+r; if(dy<cy+16||dy>=maxy)continue; int srcr=r*ih/dh;
                        for(int c=0;c<dw;c++){ int dx=cx+c; if(dx>=x+w-8)continue; int srcc=c*iw/dw; u8*pp=&src[((srcr*iw)+srcc)*4]; putpx(dx,dy,rgb(pp[0],pp[1],pp[2])); } }
                    ly += dh+8;
                } else {
                    int bw=w-24, bh=54; if(ly+bh<maxy){ fill_round(cx,ly,bw,bh,6,15,g_pagedark?rgb(38,44,58):rgb(238,240,246));
                        dstr(cx+12,ly+bh/2-6,"[image unavailable]",g_pagedark?rgb(150,160,180):rgb(150,158,176),1); }
                    ly += bh+8;
                }
                if(ly>=maxy)break;
            }
            if(ly>=maxy)break;
            int lvl=WEBST[line]&0x0F, quote=(WEBST[line]&0x20)!=0;
            if(lvl==0x0F){ int ry=ly+8; fill_rect(cx,ry,(g_hasfloat?g_wrappx:w-40),1,g_pagedark?rgb(70,78,96):rgb(210,214,222)); if(vN<80){vLine[vN]=line;vY[vN]=ly;vSc[vN]=1;vX0[vN]=cx;vN++;} ly+=17; continue; }
            int zsc=g_webzoom; int sc=(lvl>=1&&lvl<=2)?(zsc+1):zsc; if(sc>3)sc=3;
            if(lvl>=1&&lvl<=3){ ly+=web_margin(lvl); if(ly>=maxy)break; }
            u32 col=(lvl==1)?h1c:(lvl>=2&&lvl<=3)?h2c:bodyc;
            int bx0=cx+(quote?18:0);
            if(quote) fill_rect(cx+4,ly-1,3,(sc==2?26:sc==3?38:15),g_pagedark?rgb(90,120,200):rgb(150,176,230));
            for(int fm=0;fm<g_findn;fm++) if(g_findml[fm]==line){ int hx=bx0+pw_sub(WEBL[line],0,g_findmc[fm],sc), hw=pw_sub(WEBL[line],g_findmc[fm],g_findmc[fm]+g_findlen,sc); fill_rect(hx-1,ly-1,hw+2,14*sc+2,(fm==g_findcur)?rgb(255,196,60):rgb(255,236,150)); }
            pdstr(bx0,ly,WEBL[line],col,sc);
            for(int B=0;B<web_nbold;B++) if(webbold[B].line==line){ int c0=webbold[B].c0,c1=webbold[B].c1; if(c1>WEB_COLS-1)c1=WEB_COLS-1; if(c1<=c0)continue;
                int lxb=bx0+pw_sub(WEBL[line],0,c0,sc); pdsub(lxb+1,ly,WEBL[line],c0,c1,col,sc); }
            for(int C=0;C<web_ncol;C++) if(webcol[C].line==line){ int c0=webcol[C].c0,c1=webcol[C].c1; if(c1>WEB_COLS-1)c1=WEB_COLS-1; if(c1<=c0)continue;
                int lxc=bx0+pw_sub(WEBL[line],0,c0,sc), lwc=pw_sub(WEBL[line],c0,c1,sc); fill_rect(lxc,ly,lwc,14*sc,pgbg); pdsub(lxc,ly,WEBL[line],c0,c1,webcol[C].col,sc); }
            for(int L=0;L<web_nlink;L++) if(weblink[L].line==line){ int c0=weblink[L].c0,c1=weblink[L].c1; if(c1>WEB_COLS-1)c1=WEB_COLS-1; if(c1<=c0)continue;
                int lx=bx0+pw_sub(WEBL[line],0,c0,sc), lw=pw_sub(WEBL[line],c0,c1,sc); fill_rect(lx,ly,lw,14*sc,pgbg); pdsub(lx,ly,WEBL[line],c0,c1,linkc,sc); fill_rect(lx,ly+13*sc,lw,1,linkc);
                if(mx>=lx&&mx<lx+lw&&my>=ly&&my<ly+14*sc){ const char* hh=webhref+weblink[L].href; int q=0; while(hh[q]&&q<255){g_hoverhref[q]=hh[q];q++;} g_hoverhref[q]=0; } }
            if(vN<80){ vLine[vN]=line; vY[vN]=ly; vSc[vN]=sc; vX0[vN]=bx0; vN++; }
            ly += (sc>=3)?38:(sc==2)?26:17;
        }
        if(web_nl>vN&&vN>0){ int tot=ch-40; int sh=tot*vN/web_nl; if(sh<24)sh=24; int rng=web_nl-vN; int sy=cy+18+(rng>0?(tot-sh)*web_scroll/rng:0);
            fill_round(x+w-6,cy+18,4,tot,2,15,rgb(232,234,240)); fill_round(x+w-6,sy,4,sh,2,15,rgb(178,184,200)); }
        if(g_hasfloat && fb_nl>0){ g_railx=cx+g_wrappx+18; int cytop=cy+16, maxy=WN[i].y+WN[i].h-16; u32 boxbg2=g_pagedark?rgb(30,36,50):rgb(246,248,252);
            int scroll_px=0; for(int L=0;L<web_scroll&&L<web_nl;L++){ int lvl=WEBST[L]&0x0F; int s2=(lvl>=1&&lvl<=2)?(g_webzoom+1):g_webzoom; if(s2>3)s2=3; scroll_px+=(lvl==0x0F)?17:((s2>=3)?38:(s2==2)?26:17); }
            int fy0=cy+22-scroll_px; int fh=0; for(int L=0;L<fb_nl;L++){ int lvl=FBST[L]&0x0F; int s2=(lvl>=1&&lvl<=2)?(g_webzoom+1):g_webzoom; if(s2>3)s2=3; fh+=(lvl==0x0F)?17:((s2>=3)?38:(s2==2)?26:17); }
            int vtop=fy0-10; if(vtop<cytop)vtop=cytop; int vbot=fy0+fh+10; if(vbot>maxy)vbot=maxy;
            if(vbot>vtop){ u32 boxbg=g_pagedark?rgb(30,36,50):rgb(246,248,252), brd=g_pagedark?rgb(70,78,96):rgb(206,212,224);
                fill_rect(g_railx-10,vtop,g_floatw+14,vbot-vtop,boxbg);
                for(int xx=g_railx-10;xx<g_railx+g_floatw+4&&xx<x+w;xx++){ putpx(xx,vtop,brd); putpx(xx,vbot-1,brd); }
                for(int yy=vtop;yy<vbot;yy++){ putpx(g_railx-10,yy,brd); if(g_railx+g_floatw+3<x+w)putpx(g_railx+g_floatw+3,yy,brd); } }
            fbN=0; int fyc=fy0;
            for(int L=0;L<fb_nl;L++){ int lvl=FBST[L]&0x0F; int s2=(lvl>=1&&lvl<=2)?(g_webzoom+1):g_webzoom; if(s2>3)s2=3; int lh=(lvl==0x0F)?17:((s2>=3)?38:(s2==2)?26:17);
                if(fyc>=cytop && fyc+14*s2<=maxy){ int bx=g_railx;
                    if(lvl==0x0F){ fill_rect(bx,fyc+8,g_floatw-16,1,g_pagedark?rgb(70,78,96):rgb(210,214,222)); }
                    else { u32 col=(lvl==1)?(g_pagedark?rgb(255,255,255):INK):(lvl>=2&&lvl<=3)?(g_pagedark?rgb(220,226,236):rgb(48,54,70)):(g_pagedark?rgb(214,220,232):rgb(40,46,60));
                        pdstr(bx,fyc,FBL[L],col,s2);
                        for(int B=0;B<fb_nbold;B++) if(fb_bold[B].line==L){ int c0=fb_bold[B].c0,c1=fb_bold[B].c1; if(c1>WEB_COLS-1)c1=WEB_COLS-1; if(c1<=c0)continue; int lxb=bx+pw_sub(FBL[L],0,c0,s2); pdsub(lxb+1,fyc,FBL[L],c0,c1,col,s2); }
                        for(int C=0;C<fb_ncol;C++) if(fb_col[C].line==L){ int c0=fb_col[C].c0,c1=fb_col[C].c1; if(c1>WEB_COLS-1)c1=WEB_COLS-1; if(c1<=c0)continue; int lxc=bx+pw_sub(FBL[L],0,c0,s2), lwc=pw_sub(FBL[L],c0,c1,s2); fill_rect(lxc,fyc,lwc,14*s2,boxbg2); pdsub(lxc,fyc,FBL[L],c0,c1,fb_col[C].col,s2); }
                        for(int Lk=0;Lk<fb_nlink;Lk++) if(fb_link[Lk].line==L){ int c0=fb_link[Lk].c0,c1=fb_link[Lk].c1; if(c1>WEB_COLS-1)c1=WEB_COLS-1; if(c1<=c0)continue; int lx=bx+pw_sub(FBL[L],0,c0,s2), lw=pw_sub(FBL[L],c0,c1,s2); u32 lc=g_pagedark?rgb(120,182,255):ACC; fill_rect(lx,fyc,lw,14*s2,boxbg2); pdsub(lx,fyc,FBL[L],c0,c1,lc,s2); fill_rect(lx,fyc+13*s2,lw,1,lc);
                            if(mx>=lx&&mx<lx+lw&&my>=fyc&&my<fyc+14*s2){ const char*hh=webhref+fb_link[Lk].href; int q=0; while(hh[q]&&q<255){g_hoverhref[q]=hh[q];q++;} g_hoverhref[q]=0; } } }
                    if(fbN<FB_MAXL){ fbvLine[fbN]=L; fbvY[fbN]=fyc; fbvSc[fbN]=s2; fbN++; } }
                fyc+=lh; } }
        if(g_hasleft && lb_nl>0){ g_leftx=x+18; int cytop=cy+16, maxy=WN[i].y+WN[i].h-16; u32 boxbg3=g_pagedark?rgb(30,36,50):rgb(244,246,250);
            int scroll_px=0; for(int L=0;L<web_scroll&&L<web_nl;L++){ int lvl=WEBST[L]&0x0F; int s2=(lvl>=1&&lvl<=2)?(g_webzoom+1):g_webzoom; if(s2>3)s2=3; scroll_px+=(lvl==0x0F)?17:((s2>=3)?38:(s2==2)?26:17); }
            int fy0=cy+22-scroll_px;
            lbN=0; int fyc=fy0;
            for(int L=0;L<lb_nl;L++){ int lvl=LBST[L]&0x0F; int s2=(lvl>=1&&lvl<=2)?(g_webzoom+1):g_webzoom; if(s2>3)s2=3; int lh=(lvl==0x0F)?17:((s2>=3)?38:(s2==2)?26:17);
                if(fyc>=cytop && fyc+14*s2<=maxy){ int bx=g_leftx;
                    if(lvl==0x0F){ fill_rect(bx,fyc+8,g_leftw-16,1,g_pagedark?rgb(70,78,96):rgb(210,214,222)); }
                    else { u32 col=(lvl==1)?(g_pagedark?rgb(255,255,255):INK):(lvl>=2&&lvl<=3)?(g_pagedark?rgb(220,226,236):rgb(48,54,70)):(g_pagedark?rgb(206,212,226):rgb(70,78,96));
                        pdstr(bx,fyc,LBL[L],col,s2);
                        for(int B=0;B<lb_nbold;B++) if(lb_bold[B].line==L){ int c0=lb_bold[B].c0,c1=lb_bold[B].c1; if(c1>WEB_COLS-1)c1=WEB_COLS-1; if(c1<=c0)continue; int lxb=bx+pw_sub(LBL[L],0,c0,s2); pdsub(lxb+1,fyc,LBL[L],c0,c1,col,s2); }
                        for(int C=0;C<lb_ncol;C++) if(lb_col[C].line==L){ int c0=lb_col[C].c0,c1=lb_col[C].c1; if(c1>WEB_COLS-1)c1=WEB_COLS-1; if(c1<=c0)continue; int lxc=bx+pw_sub(LBL[L],0,c0,s2),lwc=pw_sub(LBL[L],c0,c1,s2); fill_rect(lxc,fyc,lwc,14*s2,boxbg3); pdsub(lxc,fyc,LBL[L],c0,c1,lb_col[C].col,s2); }
                        for(int Lk=0;Lk<lb_nlink;Lk++) if(lb_link[Lk].line==L){ int c0=lb_link[Lk].c0,c1=lb_link[Lk].c1; if(c1>WEB_COLS-1)c1=WEB_COLS-1; if(c1<=c0)continue; int lx=bx+pw_sub(LBL[L],0,c0,s2), lw=pw_sub(LBL[L],c0,c1,s2); u32 lc=g_pagedark?rgb(120,182,255):ACC; fill_rect(lx,fyc,lw,14*s2,boxbg3); pdsub(lx,fyc,LBL[L],c0,c1,lc,s2); fill_rect(lx,fyc+13*s2,lw,1,lc);
                            if(mx>=lx&&mx<lx+lw&&my>=fyc&&my<fyc+14*s2){ const char*hh=webhref+lb_link[Lk].href; int q=0; while(hh[q]&&q<255){g_hoverhref[q]=hh[q];q++;} g_hoverhref[q]=0; } } }
                    if(lbN<LB_MAXL){ lbvLine[lbN]=L; lbvY[lbN]=fyc; lbvSc[lbN]=s2; lbN++; } }
                fyc+=lh; }
            fill_rect(g_leftx+g_leftw+2, cytop, 1, maxy-cytop, g_pagedark?rgb(50,56,72):rgb(224,228,236)); }
    } else if(g_page==0){
        int CW=WN[i].w, ccx=WN[i].x+CW/2;
        for(int yy=cy-8; yy<cy+150 && yy<WN[i].y+WN[i].h; yy++){ int a=(cy+150-yy)*44/158; if(a<0)a=0; for(int xx=WN[i].x+1;xx<WN[i].x+w-1;xx++){ u32*p=&SCENE[(u64)yy*SW+xx]; *p=mix(*p,ACC,a); } }
        draw_logo(ccx, cy+18, 40, ACC, 256);
        pdstr_ctr(ccx, cy+70, "Noovex", INK, 3);
        if(g_nvx_signedin){ char ac[100],*ap=ac; ap=scopy(ap,"Signed in as ");ap=scopy(ap,g_nvx_user[0]?g_nvx_user:g_nvx_name);ap=scopy(ap," - history synced"); *ap=0; pdstr_ctr(ccx, cy+108, ac, ACC, 1); }
        else pdstr_ctr(ccx, cy+108, "Fast, private browsing", rgb(140,148,166), 1);
        int pw2=470; if(pw2>CW-90)pw2=CW-90; int pxp=ccx-pw2/2, pyp=cy+136;
        shadow_soft(pxp,pyp,pw2,46,12); fill_round(pxp,pyp,pw2,46,12,15,rgb(246,248,251));
        for(int t=0;t<46;t++){ SCENE[(u64)(pyp+t)*SW+pxp]=rgb(220,224,232); SCENE[(u64)(pyp+t)*SW+pxp+pw2-1]=rgb(220,224,232); }
        { int gx=pxp+26, gy=pyp+21; fill_circle(gx,gy,7,rgb(150,158,176)); fill_circle(gx,gy,5,rgb(246,248,251)); line_bl(gx+5,gy+5,gx+10,gy+10,rgb(150,158,176),256,2); }
        pdstr(pxp+46,pyp+15,"Search the web or type a URL",rgb(150,158,176),2);
        lk_tgt[lk_n]=30; lk_y[lk_n]=pyp; lk_bx[lk_n]=pxp; lk_bw[lk_n]=pw2; lk_bh[lk_n]=46; lk_n++;
        const char* tlabel[4]={"Google","Wikipedia","Example","mynexus"}; int ttgt[4]={12,13,10,11};
        u32 tcol[4]={rgb(66,133,244),rgb(90,100,120),rgb(60,178,120),rgb(150,90,220)}; char tini[4]={'G','W','E','M'};
        int nt=4, tw=104, th=94, gap=18, tot=nt*tw+(nt-1)*gap; if(tot>CW-40){ tw=(CW-40-(nt-1)*gap)/nt; tot=nt*tw+(nt-1)*gap; } int tx0=ccx-tot/2, ty=cy+206;
        for(int t=0;t<nt;t++){ int txx=tx0+t*(tw+gap); int hov=(mx>=txx&&mx<txx+tw&&my>=ty&&my<ty+th);
            shadow_soft(txx,ty,tw,th,14); fill_round(txx,ty,tw,th,14,15,hov?rgb(240,244,250):rgb(250,251,253));
            fill_circle(txx+tw/2,ty+34,19,tcol[t]); { char c1[2]={tini[t],0}; pdstr_ctr(txx+tw/2,ty+24,c1,rgb(255,255,255),2); }
            pdstr_ctr(txx+tw/2,ty+62,(char*)tlabel[t],rgb(60,66,84),1);
            lk_tgt[lk_n]=ttgt[t]; lk_y[lk_n]=ty; lk_bx[lk_n]=txx; lk_bw[lk_n]=tw; lk_bh[lk_n]=th; lk_n++; }
        int by=ty+th+34; char bb[32],*bp=bb; bp=scopy(bp,"Bookmarks ("); bp=udec(bp,(u32)nbmarks); *bp++=')'; *bp=0;
        char hb2[32],*hp=hb2; hp=scopy(hp,"History ("); hp=udec(hp,(u32)nhist); *hp++=')'; *hp=0;
        const char* ab="About"; int w1=pw_str(bb,1),w2=pw_str(hb2,1),w3=pw_str((char*)ab,1); int sepw=24, rowt=w1+w2+w3+2*sepw; int rx=ccx-rowt/2;
        pdstr(rx,by,bb,ACC,1); lk_tgt[lk_n]=20; lk_y[lk_n]=by-2; lk_bx[lk_n]=rx; lk_bw[lk_n]=w1; lk_bh[lk_n]=16; lk_n++; rx+=w1; pdstr_ctr(rx+sepw/2,by,"|",rgb(200,206,216),1); rx+=sepw;
        pdstr(rx,by,hb2,ACC,1); lk_tgt[lk_n]=21; lk_y[lk_n]=by-2; lk_bx[lk_n]=rx; lk_bw[lk_n]=w2; lk_bh[lk_n]=16; lk_n++; rx+=w2; pdstr_ctr(rx+sepw/2,by,"|",rgb(200,206,216),1); rx+=sepw;
        pdstr(rx,by,(char*)ab,ACC,1); lk_tgt[lk_n]=1; lk_y[lk_n]=by-2; lk_bx[lk_n]=rx; lk_bw[lk_n]=w3; lk_bh[lk_n]=16; lk_n++;
    } else if(g_page==1){
        dstr(cx,cy,"About",INK,3);
        dstr(cx,cy+44,"NoovexOS 64 Browser",INK,1);
        dstr(cx,cy+66,"HTTP + gzip/chunked decoding and TLS 1.3 (HTTPS).",rgb(70,80,100),1);
        dstr(cx,cy+86,"Tabs, bookmarks, history, find-in-page, address autocomplete.",rgb(70,80,100),1);
        dstr(cx,cy+106,"Renders headings, lists, tables, quotes, rules, links, colour.",rgb(70,80,100),1);
        dstr(cx,cy+126,"Type a query (not a URL) in the bar to search the web.",rgb(70,80,100),1);
        dstr(cx,cy+146,"Images: PNG (incl. interlaced) and baseline JPEG. + / - zoom.",rgb(70,80,100),1);
        dstr(cx,cy+170,"Certificates are not verified; text-grid layout (no CSS boxes).",rgb(150,110,90),1);
        lk_y[lk_n]=cy+206; lk_tgt[lk_n]=0; lk_bx[lk_n]=cx; lk_bw[lk_n]=w-36; lk_bh[lk_n]=16; dstr(cx,cy+206,"< Back home",ACC,1); lk_n++;
    }
    if(g_findmode){ int fw=252, fx=x+w-fw-8, fy=cy-6; fill_round(fx,fy,fw,26,6,15,rgb(252,252,254));
        for(int t=0;t<26;t++){ SCENE[(u64)(fy+t)*SW+fx]=rgb(210,214,222); SCENE[(u64)(fy+t)*SW+fx+fw-1]=rgb(210,214,222); }
        dstr(fx+8,fy+6,"Find:",rgb(120,128,146),1); int tx0=fx+50; dstr(tx0,fy+6,g_findstr,INK,1);
        if((g_ticks/50)&1)fill_rect(tx0+g_findlen*8,fy+7,2,12,ACC);
        char cb[20],*p=cb; if(g_findn){ p=udec(p,(u32)(g_findcur+1)); *p++='/'; p=udec(p,(u32)g_findn); } else { p=scopy(p,"none"); } *p=0;
        dstr(fx+fw-52,fy+6,cb,g_findn?rgb(90,120,90):rgb(160,110,110),1); }
    if(g_page==3 && g_hoverhref[0]){ int by=WN[i].y+WN[i].h-20; fill_rect(WN[i].x+1,by,WN[i].w-2,19,rgb(244,245,248)); fill_rect(WN[i].x+1,by,WN[i].w-2,1,rgb(228,230,236));
        char hb[92]; int hn=0; for(int q=0;g_hoverhref[q]&&hn<90;q++)hb[hn++]=g_hoverhref[q]; hb[hn]=0; dstr(WN[i].x+10,by+3,hb,rgb(80,90,110),1); }
}
static void browser_click(int px,int py){
    int tsy=WN[W_BROWSER].y+TITLEH+6, tsh=26;
    if(py>=tsy && py<tsy+tsh){
        if(g_ntab<NTAB && px>=tab_plusx && px<tab_plusx+24){ tab_new(); return; }
        for(int k=0;k<g_ntab;k++){ int tx=tab_x[k]; if(px>=tx && px<tx+tab_w-4){ if(g_ntab>1 && px>=tx+tab_w-24){ tab_close(k); return; } tab_switch(k); return; } }
        return; }
    if(in_rect(px,py,g_starx,g_stary,g_starw,28)){ if(g_cururl[0])bm_toggle(g_cururl); return; }
    if(in_rect(px,py,g_findbx,g_findby,g_findbw,28)){ g_findmode=!g_findmode; g_urlfocus=0; if(!g_findmode){ g_findn=0; } return; }
    if(in_rect(px,py,g_darkbx,g_findby,g_darkbw,28)){ g_forcedark=!g_forcedark; g_urlfocus=0; rewrap_render(); return; }
    if(in_rect(px,py,tab_savex,br_bary,tab_savew,28)){ browser_save(); return; }
    if(in_rect(px,py,br_bkx,br_bary,br_bkw,28)){ if(backn>0){ scopy(g_url,backstk[--backn]); g_urllen=slen(g_url); browser_go(0); } return; }
    if(in_rect(px,py,br_rlx,br_bary,br_rlw,28)){ if(g_cururl[0]){ scopy(g_url,g_cururl); g_urllen=slen(g_url);} browser_go(0); return; }
    if(in_rect(px,py,br_barx,br_bary,br_barw,28)){ g_urlfocus=1; g_urlsel=1; return; }
    if(in_rect(px,py,br_gx,br_bary,br_gw,28)){ browser_go(1); return; }
    if(in_rect(px,py,br_hx,br_bary,br_hw,28)){ g_page=0; g_urlfocus=0; g_isimg=0; return; }
    g_urlfocus=0;
    for(int k=0;k<lk_n;k++){ if(px>=lk_bx[k]&&px<lk_bx[k]+lk_bw[k]&&py>=lk_y[k]&&py<lk_y[k]+lk_bh[k]){
        int t=lk_tgt[k]; if(t==10){ scopy(g_url,"example.com"); g_urllen=11; browser_go(1); } else if(t==11){ scopy(g_url,"mynexus.site"); g_urllen=12; browser_go(1); } else if(t==12){ scopy(g_url,"https://www.google.com/"); g_urllen=slen(g_url); browser_go(1); } else if(t==13){ scopy(g_url,"https://en.wikipedia.org/"); g_urllen=slen(g_url); browser_go(1); } else if(t==20){ show_bookmarks(); } else if(t==21){ show_history(); } else if(t==30){ g_urlfocus=1; g_urlsel=1; } else g_page=t; return; } }
    if(g_page==3 && !g_isimg){ for(int v=0;v<vN;v++){ int line=vLine[v],yy=vY[v],sc=vSc[v],bx0=vX0[v]; int lh=(sc>=3)?38:(sc==2)?26:17;
        if(py>=yy&&py<yy+lh){ for(int L=0;L<web_nlink;L++) if(weblink[L].line==line){ int lx=bx0+pw_sub(WEBL[line],0,weblink[L].c0,sc), lw=pw_sub(WEBL[line],weblink[L].c0,weblink[L].c1,sc);
            if(px>=lx&&px<lx+lw){ const char*h=webhref+weblink[L].href; char ru[512]; ru[0]=0;
                if(ci_eq(h[0],'h')&&ci_eq(h[1],'t')&&ci_eq(h[2],'t')&&ci_eq(h[3],'p')) urlcat(ru,h,0,0);
                else if(h[0]=='/'&&h[1]=='/') urlcat(ru,"https:",h,0);
                else if(h[0]=='/') urlcat(ru,g_host,h,0);
                else if(h[0]=='#'){ const char* frag=h+1; if(frag[0]==0){ web_scroll=0; return; } for(int A=0;A<web_nanch;A++) if(seq(webanch[A].name,frag)){ int t=webanch[A].line; if(t>=web_nl)t=web_nl-1; if(t<0)t=0; web_scroll=t; break; } return; }
                else if(h[0]==0) return;
                else urlcat(ru,g_host,"/",h);
                if(is_dl_ext(ru)||is_img_ext(ru)){ browser_download(ru); return; }
                scopy(g_url,ru); g_urllen=slen(g_url); browser_go(1); return; } } } } }
    if(g_page==3 && !g_isimg && g_hasfloat){ for(int v=0;v<fbN;v++){ int L=fbvLine[v],yy=fbvY[v],sc=fbvSc[v]; int lh=(sc>=3)?38:(sc==2)?26:17;
        if(py>=yy&&py<yy+lh){ for(int Lk=0;Lk<fb_nlink;Lk++) if(fb_link[Lk].line==L){ int lx=g_railx+pw_sub(FBL[L],0,fb_link[Lk].c0,sc), lw=pw_sub(FBL[L],fb_link[Lk].c0,fb_link[Lk].c1,sc);
            if(px>=lx&&px<lx+lw){ const char*h=webhref+fb_link[Lk].href; char ru[512]; ru[0]=0;
                if(ci_eq(h[0],'h')&&ci_eq(h[1],'t')&&ci_eq(h[2],'t')&&ci_eq(h[3],'p')) urlcat(ru,h,0,0);
                else if(h[0]=='/'&&h[1]=='/') urlcat(ru,"https:",h,0);
                else if(h[0]=='/') urlcat(ru,g_host,h,0);
                else if(h[0]=='#'||h[0]==0) return;
                else urlcat(ru,g_host,"/",h);
                if(is_dl_ext(ru)||is_img_ext(ru)){ browser_download(ru); return; }
                scopy(g_url,ru); g_urllen=slen(g_url); browser_go(1); return; } } } } }
    if(g_page==3 && !g_isimg && g_hasleft){ for(int v=0;v<lbN;v++){ int L=lbvLine[v],yy=lbvY[v],sc=lbvSc[v]; int lh=(sc>=3)?38:(sc==2)?26:17;
        if(py>=yy&&py<yy+lh){ for(int Lk=0;Lk<lb_nlink;Lk++) if(lb_link[Lk].line==L){ int lx=g_leftx+pw_sub(LBL[L],0,lb_link[Lk].c0,sc), lw=pw_sub(LBL[L],lb_link[Lk].c0,lb_link[Lk].c1,sc);
            if(px>=lx&&px<lx+lw){ const char*h=webhref+lb_link[Lk].href; char ru[512]; ru[0]=0;
                if(ci_eq(h[0],'h')&&ci_eq(h[1],'t')&&ci_eq(h[2],'t')&&ci_eq(h[3],'p')) urlcat(ru,h,0,0);
                else if(h[0]=='/'&&h[1]=='/') urlcat(ru,"https:",h,0);
                else if(h[0]=='/') urlcat(ru,g_host,h,0);
                else if(h[0]=='#'||h[0]==0) return;
                else urlcat(ru,g_host,"/",h);
                if(is_dl_ext(ru)||is_img_ext(ru)){ browser_download(ru); return; }
                scopy(g_url,ru); g_urllen=slen(g_url); browser_go(1); return; } } } } }
}
static void browser_key(char c){
    if(g_findmode){
        if(c=='\n'){ find_next(); return; }
        if(c==27){ g_findmode=0; g_findn=0; return; }        /* Esc closes find */
        if(c=='\b'){ if(g_findlen>0){ g_findstr[--g_findlen]=0; find_run(); } return; }
        if(c>=32&&c<127){ if(g_findlen<46){ g_findstr[g_findlen++]=c; g_findstr[g_findlen]=0; find_run(); } return; }
        return;
    }
    if(!g_urlfocus){
        if(g_page==3){
            if(c=='+'||c=='='){ if(g_webzoom<2){ g_webzoom=2; rewrap_render(); } return; }
            if(c=='-'||c=='_'){ if(g_webzoom>1){ g_webzoom=1; rewrap_render(); } return; }
            if(c==19){ if(web_scroll>0)web_scroll--; return; } if(c==20){ if(web_scroll<web_nl-1)web_scroll++; return; }
        }
        if(c>=33&&c<127){ g_urlfocus=1; g_urlsel=0; g_url[0]=c; g_url[1]=0; g_urllen=1; }   /* start a fresh URL */
        return; }
    if(c=='\n'){ g_urlsel=0; browser_go(1); }
    else if(c=='\t'){ if(g_acsug[0]){ scopy(g_url,g_acsug); g_urllen=slen(g_url); } g_urlsel=0; }
    else if(c=='\b'){ if(g_urlsel||(g_urllen>0&&seq(g_url,g_cururl))){ g_url[0]=0; g_urllen=0; g_urlsel=0; } else if(g_urllen>0)g_url[--g_urllen]=0; }
    else if(c>=32&&c<127){ if(g_urlsel||(g_urllen>0&&seq(g_url,g_cururl))){ g_url[0]=0; g_urllen=0; g_urlsel=0; } if(g_urllen<255){ g_url[g_urllen++]=c; g_url[g_urllen]=0; } }
}

