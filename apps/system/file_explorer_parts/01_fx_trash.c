static int fx_trash(void); static int fx_trash_id(void); static int fx_in_trash(int d);
static int fx_dir=-1, fx_mode=0;                  /* mode 0 = browse, 1 = This PC */
static int fx_hist[32], fx_hn=0, fx_hp=-1;
int fx_view=0, fx_prev=1, fx_sort=0, fx_sdesc=0, fx_scroll=0;
int fx_seldir=-1;                          /* selected folder node (files use g_fsel) */
static char fx_filter[32]; static int fx_flen=0;
static u64 fx_lastt=0; static int fx_lastid=-99999;
static int fx_clip=-1, fx_clipcut=0;
static int fx_confirm=0, fx_confirm_dir=-1;
static int fx_rk[FX_MAXROWS], fx_rid[FX_MAXROWS], fx_rn=0;
static int fx_hx[FX_MAXROWS], fx_hy[FX_MAXROWS], fx_hw[FX_MAXROWS], fx_hh[FX_MAXROWS], fx_hrow[FX_MAXROWS], fx_hn2=0;
static int fx_bx[FX_MAXB], fx_by[FX_MAXB], fx_bw[FX_MAXB], fx_bh[FX_MAXB], fx_bid[FX_MAXB], fx_bval[FX_MAXB], fx_bn=0;
static int fx_mainx, fx_mainy, fx_mainw, fx_mainh, fx_pvx0=0, fx_pvy0=0, fx_pvw=0, fx_cols=1;
int g_renmode=0, g_rennode=-1;             /* 0 rename file, 1 rename folder, 2 new folder, 3 new file */

static int fx_lc(int c){ return (c>='A'&&c<='Z')?c+32:c; }
static int fx_icmp(const char* a,const char* b){ while(*a&&*b){ int x=fx_lc(*a),y=fx_lc(*b); if(x!=y)return x-y; a++;b++; } return fx_lc(*a)-fx_lc(*b); }
static int fx_match(const char* s){ if(!fx_flen)return 1; for(int i=0;s[i];i++){ int k=0; while(k<fx_flen&&s[i+k]&&fx_lc(s[i+k])==fx_lc(fx_filter[k]))k++; if(k==fx_flen)return 1; } return 0; }
static const char* fx_ext(const char* s){ const char* d=0; for(;*s;s++)if(*s=='.')d=s; return d?d+1:""; }
static u8 fx_exp[DN_MAX];
static void fx_btn(int x,int y,int w,int h,int id,int val){ if(fx_bn<FX_MAXB){ fx_bx[fx_bn]=x; fx_by[fx_bn]=y; fx_bw[fx_bn]=w; fx_bh[fx_bn]=h; fx_bid[fx_bn]=id; fx_bval[fx_bn]=val; fx_bn++; } }
static void fx_fit(char* o,const char* s,int maxch){ int n=slen(s); if(maxch<4)maxch=4; if(n<=maxch){ scopy(o,s)[0]=0; return; } int i=0; for(;i<maxch-2;i++)o[i]=s[i]; o[i++]='.'; o[i++]='.'; o[i]=0; }
int fx_is_elf(int k){ if(FS[k].len<4)return 0; if(FS[k].lazy)return (FS[k].mode&0111)!=0&&(FS[k].mode&0170000u)!=0120000u; const char* d=fsd(k); return (u8)d[0]==0x7F&&d[1]=='E'&&d[2]=='L'&&d[3]=='F'; }
static int fx_ikind(int k){   /* icon kind: 1 text 2 code 3 image 4 archive 5 binary 6 program 7 web */
    if(fx_is_elf(k)&&!fx_ext(FS[k].name)[0])return 6; { extern int fx_xkind(const char*); int x=fx_xkind(FS[k].name); if(x>=0)return x; } if(fx_is_elf(k))return 6; int f=fkind(k); if(f==1)return 3; if(f==4)return 4; if(f==2)return 7; if(f==3)return 5;
    const char* e=fx_ext(FS[k].name); const char* code[]={"c","h","cpp","hpp","rs","py","js","ts","sh","rn","nvx","s","asm","go","lua","json","css",0};
    for(int i=0;code[i];i++) if(fx_icmp(e,code[i])==0)return 2; return 1; }
static const char* fx_kname(int kind,int k){ { extern const char* fx_xname(int); const char* xn=fx_xname(kind); if(xn)return xn; } if(kind==6)return "Program"; if(kind==3)return "Image"; if(kind==4)return "Archive"; return file_type_name(FS[k].name); }

static void fx_glyph(int x,int y,int g,u32 c){
    switch(g){
    case 1: line_bl(x+10,y+2,x+4,y+7,c,255,1); line_bl(x+4,y+7,x+10,y+12,c,255,1); break;
    case 2: line_bl(x+4,y+2,x+10,y+7,c,255,1); line_bl(x+10,y+7,x+4,y+12,c,255,1); break;
    case 3: line_bl(x+2,y+9,x+7,y+3,c,255,1); line_bl(x+7,y+3,x+12,y+9,c,255,1); break;
    case 4: fill_round(x,y+2,8,4,1,15,c); fill_round(x,y+4,15,10,2,15,c); fill_rect(x+6,y+8,5,2,rgb(255,255,255)); fill_rect(x+7,y+6,2,6,rgb(255,255,255)); break;
    case 5: fill_round(x+2,y,11,14,2,15,c); fill_rect(x+4,y+6,7,2,rgb(255,255,255)); fill_rect(x+6,y+4,2,6,rgb(255,255,255)); break;
    case 6: line_bl(x+2,y+12,x+11,y+3,c,255,1); fill_rect(x+1,y+12,3,2,c); break;
    case 7: fill_rect(x+2,y+2,11,2,c); fill_rect(x+5,y,5,2,c); fill_round(x+3,y+5,9,9,2,15,c); fill_rect(x+6,y+7,1,5,rgb(255,255,255)); fill_rect(x+9,y+7,1,5,rgb(255,255,255)); break;
    case 8: fill_round(x+4,y,10,11,2,15,c); fill_rect(x+5,y+1,8,9,rgb(255,255,255)); fill_round(x,y+4,10,11,2,15,c); break;
    case 9: fill_circle(x+3,y+11,3,c); fill_circle(x+11,y+11,3,c); fill_circle(x+3,y+11,1,rgb(255,255,255)); fill_circle(x+11,y+11,1,rgb(255,255,255)); line_bl(x+4,y+9,x+11,y+1,c,255,0); line_bl(x+10,y+9,x+3,y+1,c,255,0); break;
    case 10: fill_round(x+1,y+2,13,12,2,15,c); fill_rect(x+3,y+5,9,7,rgb(255,255,255)); fill_round(x+4,y,7,4,1,15,c); break;
    case 11: fill_rect(x+1,y+1,5,5,c); fill_rect(x+8,y+1,5,5,c); fill_rect(x+1,y+8,5,5,c); fill_rect(x+8,y+8,5,5,c); break;
    case 12: for(int k=0;k<3;k++){ fill_rect(x+1,y+2+k*4,2,2,c); fill_rect(x+5,y+2+k*4,9,2,c); } break;
    case 13: fill_round(x,y+1,15,12,2,15,c); fill_rect(x+1,y+2,8,10,rgb(255,255,255)); break;
    }
}
extern int g_dstyle;
#define FXC(a,b) (g_dstyle==1?(u32)(b):(u32)(a))
static void fx_bev(int x,int y,int w,int h,int in){ u32 a=in?rgb(128,128,128):rgb(255,255,255), b=in?rgb(255,255,255):rgb(64,64,64), c=in?rgb(64,64,64):rgb(212,208,200), d=in?rgb(212,208,200):rgb(128,128,128);
    fill_rect(x,y,w,1,a); fill_rect(x,y,1,h,a); fill_rect(x,y+h-1,w,1,b); fill_rect(x+w-1,y,1,h,b); if(w>4&&h>4){ fill_rect(x+1,y+1,w-2,1,c); fill_rect(x+1,y+1,1,h-2,c); fill_rect(x+1,y+h-2,w-2,1,d); fill_rect(x+w-2,y+1,1,h-2,d); } }
/* ---- icons ---- */
static void fx_icon_item(int x,int y,int s,int isd,int id);
int item_icon_kind(int isd,int id);

enum { XG_DOC, XG_ZIP, XG_IMG, XG_VID, XG_AUD, XG_SHEET, XG_SLIDE, XG_TXT, XG_WEB, XG_EXE, XG_FONT, XG_DISC, XG_PKG, XG_KEY, XG_DB, XG_GEAR, XG_LOG, XG_REG, XG_CHIP, XG_PLUG, XG_PUZZLE, XG_BOOKS, XG_NVX, XG_TERM, XG_BOOK };
typedef struct { const char* e; u8 g; u32 c; const char* t; const char* n; } fx_xi;
static const fx_xi FXI[]={
    {"zip",XG_ZIP,0xE0A020,0,"ZIP archive"},{"7z",XG_ZIP,0x2B6CB0,0,"7-Zip archive"},{"rar",XG_ZIP,0x8E44AD,0,"RAR archive"},{"tar",XG_ZIP,0x8D6E63,0,"TAR archive"},
    {"gz",XG_ZIP,0x795548,0,"Gzip archive"},{"tgz",XG_ZIP,0x795548,0,"Gzip TAR archive"},{"xz",XG_ZIP,0x5D4037,0,"XZ archive"},{"bz2",XG_ZIP,0xA1887F,0,"Bzip2 archive"},{"zst",XG_ZIP,0x4E342E,0,"Zstandard archive"},{"cab",XG_ZIP,0x6D4C41,0,"Cabinet archive"},
    {"png",XG_IMG,0x2E86DE,0,"PNG image"},{"jpg",XG_IMG,0xE67E22,0,"JPEG image"},{"jpeg",XG_IMG,0xE67E22,0,"JPEG image"},{"gif",XG_IMG,0x9B59B6,0,"GIF image"},{"bmp",XG_IMG,0x16A085,0,"Bitmap image"},
    {"webp",XG_IMG,0x27AE60,0,"WebP image"},{"svg",XG_IMG,0xF39C12,0,"SVG drawing"},{"ico",XG_IMG,0x34495E,0,"Icon"},{"tga",XG_IMG,0x1ABC9C,0,"TGA image"},{"ppm",XG_IMG,0x95A5A6,0,"PPM image"},{"jxl",XG_IMG,0x2C3E50,0,"JPEG XL image"},{"avif",XG_IMG,0x8E44AD,0,"AVIF image"},
    {"mp4",XG_VID,0xC0392B,0,"MP4 video"},{"mkv",XG_VID,0x6C3483,0,"Matroska video"},{"webm",XG_VID,0x1E8449,0,"WebM video"},{"avi",XG_VID,0x2471A3,0,"AVI video"},{"mov",XG_VID,0x7F8C8D,0,"QuickTime video"},{"m4v",XG_VID,0xA93226,0,"M4V video"},
    {"mp3",XG_AUD,0x8E44AD,0,"MP3 audio"},{"wav",XG_AUD,0x2980B9,0,"WAV audio"},{"ogg",XG_AUD,0xD35400,0,"Ogg audio"},{"flac",XG_AUD,0x117A65,0,"FLAC audio"},{"m4a",XG_AUD,0xCB4335,0,"M4A audio"},{"opus",XG_AUD,0x1F618D,0,"Opus audio"},{"aac",XG_AUD,0xB03A2E,0,"AAC audio"},{"mid",XG_AUD,0x7D3C98,0,"MIDI music"},
    {"txt",XG_DOC,0x7F8C8D,0,"Text file"},{"md",XG_TXT,0x2C3E50,"M","Markdown"},{"doc",XG_TXT,0x2B579A,"W","Word document"},{"docx",XG_TXT,0x2B579A,"W","Word document"},{"odt",XG_TXT,0x1E5A9A,"W","OpenDocument text"},{"rtf",XG_DOC,0x34495E,0,"Rich text"},
    {"pdf",XG_TXT,0xD32F2F,"PDF","PDF document"},{"epub",XG_BOOK,0x7CB342,0,"E-book"},
    {"xlsx",XG_SHEET,0x217346,0,"Excel spreadsheet"},{"xls",XG_SHEET,0x217346,0,"Excel spreadsheet"},{"ods",XG_SHEET,0x1D7A47,0,"OpenDocument spreadsheet"},{"csv",XG_SHEET,0x4CAF50,0,"CSV table"},
    {"pptx",XG_SLIDE,0xD24726,0,"PowerPoint slides"},{"ppt",XG_SLIDE,0xD24726,0,"PowerPoint slides"},{"odp",XG_SLIDE,0xC0571F,0,"OpenDocument slides"},
    {"c",XG_TXT,0x5C6BC0,"C","C source"},{"h",XG_TXT,0x7986CB,"H","C header"},{"cpp",XG_TXT,0x00599C,"C++","C++ source"},{"hpp",XG_TXT,0x00599C,"H++","C++ header"},{"rs",XG_TXT,0xB7410E,"RS","Rust source"},
    {"py",XG_TXT,0x3776AB,"PY","Python script"},{"js",XG_TXT,0xC9A800,"JS","JavaScript"},{"ts",XG_TXT,0x3178C6,"TS","TypeScript"},{"go",XG_TXT,0x00ADD8,"GO","Go source"},{"lua",XG_TXT,0x000080,"LUA","Lua script"},
    {"java",XG_TXT,0xE76F00,"JV","Java source"},{"rn",XG_TXT,0xE0603A,"RN","Rune source"},{"s",XG_TXT,0x546E7A,"ASM","Assembly"},{"asm",XG_TXT,0x546E7A,"ASM","Assembly"},{"json",XG_TXT,0x8D6E63,"{}","JSON data"},
    {"css",XG_TXT,0x264DE4,"CSS","Style sheet"},{"xml",XG_TXT,0x0277BD,"<>","XML data"},{"html",XG_WEB,0xE34F26,0,"Web page"},{"htm",XG_WEB,0xE34F26,0,"Web page"},
    {"sh",XG_TERM,0x2D3436,0,"Shell script"},{"bash",XG_TERM,0x2D3436,0,"Shell script"},
    {"ini",XG_GEAR,0x607D8B,0,"Settings file"},{"cfg",XG_GEAR,0x607D8B,0,"Settings file"},{"conf",XG_GEAR,0x607D8B,0,"Settings file"},{"yml",XG_GEAR,0xCB171E,0,"YAML settings"},{"yaml",XG_GEAR,0xCB171E,0,"YAML settings"},{"toml",XG_GEAR,0x9C4221,0,"TOML settings"},
    {"nvk",XG_CHIP,0x1565C0,0,"NoovexOS kernel"},{"nvi",XG_TXT,0x0277BD,"i","System information"},{"nva",XG_EXE,0x3949AB,0,"NoovexOS app"},{"nvs",XG_GEAR,0x00897B,0,"NoovexOS service"},{"nvr",XG_REG,0x00838F,0,"Registry hive"},
    {"nvg",XG_LOG,0x455A64,0,"Event log"},{"nvq",XG_TXT,0xC62828,"!","Crash report"},{"nvf",XG_TXT,0xAD1457,"60s","Flight recorder"},{"nvb",XG_TXT,0x283593,"B","Boot settings"},{"nvh",XG_TXT,0x00695C,"HW","Hardware information"},
    {"nvo",XG_FONT,0x37474F,0,"NoovexOS font"},{"nvn",XG_AUD,0x6A1B9A,0,"System sound"},{"nvw",XG_IMG,0x8E24AA,0,"NoovexOS theme"},{"nvz",XG_TXT,0x2E7D32,"<<","Restore point"},{"nvd",XG_PLUG,0x2E7D32,0,"NoovexOS driver"},{"nvm",XG_PUZZLE,0x6A1B9A,0,"NoovexOS kernel module"},{"nvl",XG_BOOKS,0xEF6C00,0,"NoovexOS shared library"},{"nvx",XG_NVX,0xE0603A,0,"NoovexOS program"},
    {"so",XG_BOOKS,0xF57C00,0,"Shared library"},{"dll",XG_BOOKS,0x0078D7,0,"Windows library"},{"drv",XG_PLUG,0x388E3C,0,"Driver"},{"sys",XG_PLUG,0x0078D7,0,"Windows driver"},
    {"reg",XG_REG,0x00838F,0,"Registry file"},{"log",XG_LOG,0x455A64,0,"Log file"},{"svc",XG_GEAR,0x00897B,0,"Service"},{"info",XG_TXT,0x0277BD,"i","System information"},
    {"font",XG_FONT,0x37474F,0,"Font"},{"ttf",XG_FONT,0x37474F,0,"TrueType font"},{"otf",XG_FONT,0x263238,0,"OpenType font"},{"woff",XG_FONT,0x455A64,0,"Web font"},{"woff2",XG_FONT,0x455A64,0,"Web font"},
    {"exe",XG_EXE,0x0078D7,0,"Windows program"},{"elf",XG_EXE,0x5D4037,0,"Linux program"},{"bin",XG_EXE,0x616161,0,"Binary file"},{"appimage",XG_EXE,0x3E7CB1,0,"AppImage program"},{"desktop",XG_EXE,0x5E35B1,0,"App shortcut"},
    {"iso",XG_DISC,0x546E7A,0,"Disc image"},{"img",XG_DISC,0x78909C,0,"Disk image"},{"vhd",XG_DISC,0x0078D7,0,"Virtual disk"},{"qcow2",XG_DISC,0xFF6600,0,"QEMU disk"},
    {"deb",XG_PKG,0xA80030,0,"Debian package"},{"rpm",XG_PKG,0xCC0000,0,"RPM package"},{"nvp",XG_PKG,0xE0603A,0,"NoovexOS package"},
    {"pem",XG_KEY,0xC9A227,0,"Certificate"},{"crt",XG_KEY,0xC9A227,0,"Certificate"},{"key",XG_KEY,0xB8860B,0,"Private key"},
    {"db",XG_DB,0x0F7C9D,0,"Database"},{"sqlite",XG_DB,0x003B57,0,"SQLite database"},
};
#define NFXI ((int)(sizeof(FXI)/sizeof(FXI[0])))
static int fx_xfind(const char* name){ for(int k=0;name[k];k++) if(name[k]=='.'&&name[k+1]=='s'&&name[k+2]=='o'&&name[k+3]=='.'){ for(int i=0;i<NFXI;i++) if(FXI[i].e[0]=='s'&&FXI[i].e[1]=='o'&&!FXI[i].e[2])return i; } const char* e=fx_ext(name); if(!*e)return -1; for(int i=0;i<NFXI;i++) if(fx_icmp(e,FXI[i].e)==0)return i; return -1; }
static u32 fx_hexc(u32 v){ return rgb((v>>16)&255,(v>>8)&255,v&255); }
static void fx_xglyph(int bx,int by,int q,int g,u32 c,const char* t){
    u32 W_=rgb(255,255,255); int cx=bx+q/2, cy=by+q/2, u=q/8; if(u<1)u=1;
    switch(g){
    case XG_ZIP: for(int k=0;k<6;k++)fill_rect(cx-u+((k&1)?u:-u/2),by+u+k*u,u+u/2,u/2+1,W_); fill_round(cx-u,by+u*7-u/2,u*2,u+u/2,2,15,W_); break;
    case XG_IMG: fill_circle(bx+q*2/3,by+q/3,u,rgb(255,230,120)); for(int r=0;r<3*u;r++)fill_rect(cx-u*2-r/2+u,by+q-u-3*u+r,r+1,1,W_); for(int r=0;r<2*u;r++)fill_rect(cx+r/3,by+q-u-2*u+r,r+2,1,mix(W_,c,180)); break;
    case XG_VID: for(int r=0;r<4*u;r++){ int w=r<2*u?r:4*u-r; fill_rect(cx-u-u/2,cy-2*u+r,w+1,1,W_); } break;
    case XG_AUD: fill_rect(cx+u/2,by+u+u/2,u/2+1,q-3*u,W_); fill_circle(cx,by+q-2*u,u+u/3,W_); fill_rect(cx+u/2,by+u+u/2,u*2,u/2+1,W_); fill_rect(cx+u/2,by+2*u+u/2,u*2,u/2+1,W_); break;
    case XG_SHEET: fill_rect(bx+u,by+u+u/2,q-2*u,q-3*u,W_); for(int k=1;k<3;k++){ fill_rect(bx+u+k*(q-2*u)/3,by+u+u/2,1,q-3*u,c); fill_rect(bx+u,by+u+u/2+k*(q-3*u)/3,q-2*u,1,c); } break;
    case XG_SLIDE: fill_rect(bx+u,by+u+u/2,q-2*u,q/2,W_); fill_rect(bx+u+u,by+u*2+u/2,(q-4*u)/2,u/2+1,c); fill_rect(cx-1,by+u+u/2+q/2,2,u+u/2,W_); fill_rect(cx-u,by+q-u-u/2,2*u,1+u/3,W_); break;
    case XG_DOC: case XG_LOG: for(int k=0;k<4;k++)fill_rect(bx+u+(g==XG_LOG?u+u/2:0),by+u*2+k*(u+u/2),q-2*u-(g==XG_LOG?u+u/2:0)-(k==3?u*2:0),u/2+1,W_); if(g==XG_LOG)for(int k=0;k<4;k++)fill_rect(bx+u,by+u*2+k*(u+u/2),u/2+1,u/2+1,W_); break;
    case XG_TXT: dstr_ctr(cx,cy-7,t?t:"?",W_,1); break;
    case XG_WEB: fill_circle(cx,cy,q/2-u,W_); fill_circle(cx,cy,q/2-u-1-u/3,c); fill_rect(bx+u,cy,q-2*u,1,W_); fill_rect(cx,by+u,1,q-2*u,W_); fill_rect(bx+u*2,cy-u-u/2,q-4*u,1,W_); fill_rect(bx+u*2,cy+u+u/2,q-4*u,1,W_); break;
    case XG_EXE: fill_rect(bx+u,by+u+u/2,q-2*u,q-3*u,W_); fill_rect(bx+u,by+u+u/2,q-2*u,u+u/2,mix(c,W_,60)); fill_rect(bx+q-2*u-u/2,by+u*2-1,u,u/2+1,W_); break;
    case XG_FONT: dstr_ctr(cx,cy-7,"Aa",W_,1); break;
    case XG_DISC: fill_circle(cx,cy,q/2-u,W_); fill_circle(cx,cy,u+u/2,c); fill_circle(cx,cy,u/2+1,W_); break;
    case XG_PKG: fill_rect(bx+u+u/2,by+u*2,q-3*u,q-3*u-u/2,W_); fill_rect(bx+u,by+u+u/2,q-2*u,u,mix(W_,c,200)); fill_rect(cx-u/2,by+u+u/2,u,q-3*u,c); break;
    case XG_KEY: fill_circle(bx+u*2+u/2,cy,u+u/2,W_); fill_circle(bx+u*2+u/2,cy,u/2,c); fill_rect(bx+u*3+u/2,cy-u/3,q-u*5,u/2+1,W_); fill_rect(bx+q-u*2,cy,u/2+1,u,W_); fill_rect(bx+q-u*3,cy,u/2+1,u,W_); break;
    case XG_DB: for(int k=0;k<3;k++){ fill_round(bx+u+u/2,by+u+k*(u*2),q-3*u,u+u/2,u,15,W_); } fill_rect(bx+u+u/2,by+u*2,q-3*u,u*4,W_); for(int k=0;k<2;k++)fill_rect(bx+u+u/2,by+u*3+k*u*2,q-3*u,1,c); break;
    case XG_GEAR: for(int k=0;k<4;k++){ fill_rect(cx-u/2,by+u,u+1,q-2*u,W_); fill_rect(bx+u,cy-u/2,q-2*u,u+1,W_); } fill_circle(cx,cy,q/2-2*u,W_); fill_rect(cx-u*2,cy-u*2,u,u,W_); fill_rect(cx+u,cy-u*2,u,u,W_); fill_rect(cx-u*2,cy+u,u,u,W_); fill_rect(cx+u,cy+u,u,u,W_); fill_circle(cx,cy,u,c); break;
    case XG_REG: fill_rect(bx+u,by+q/2,u*2+u/2,u*2+u/2,W_); fill_rect(bx+u*4,by+q/2,u*2+u/2,u*2+u/2,W_); fill_rect(bx+u*2+u/2,by+u+u/2,u*2+u/2,u*2+u/2,W_); break;
    case XG_CHIP: fill_rect(bx+u*2,by+u*2,q-4*u,q-4*u,W_); fill_rect(bx+u*3,by+u*3,q-6*u,q-6*u,c); for(int k=0;k<3;k++){ int o=by+u*2+u/2+k*u+k*u/2; fill_rect(bx+u,o,u,u/2+1,W_); fill_rect(bx+q-u*2,o,u,u/2+1,W_); int ox=bx+u*2+u/2+k*u+k*u/2; fill_rect(ox,by+u,u/2+1,u,W_); fill_rect(ox,by+q-u*2,u/2+1,u,W_); } break;
    case XG_PLUG: fill_rect(cx-u-u/2,by+u,u/2+1,u*2,W_); fill_rect(cx+u,by+u,u/2+1,u*2,W_); fill_round(cx-u*2-u/2,by+u*3,u*5,u*3,u/2+1,15,W_); fill_rect(cx-u/2,by+u*6,u,q-u*7,W_); break;
    case XG_PUZZLE: fill_rect(bx+u+u/2,by+u*2+u/2,q-u*4,q-u*4,W_); fill_circle(cx-u/2,by+u*2,u,W_); fill_circle(bx+q-u*2,cy,u,W_); fill_circle(cx-u/2,by+q-u*2+u/2,u-u/3,c); break;
    case XG_BOOKS: fill_rect(bx+u+u/2,by+u*2,u+u/2,q-u*3,W_); fill_rect(bx+u*3+u/2,by+u+u/2,u+u/2,q-u*2-u/2,W_); fill_rect(bx+u*5+u/2,by+u*2+u/2,u+u/2,q-u*3-u/2,W_); fill_rect(bx+u,by+q-u,q-2*u,u/2+1,W_); break;
    case XG_NVX: { int h=q-u*3, x0=bx+u*2, x1=bx+q-u*3; fill_rect(x0,by+u+u/2,u,h,W_); fill_rect(x1,by+u+u/2,u,h,W_); for(int r=0;r<h;r++){ int xx=x0+(x1-x0)*r/h; fill_rect(xx,by+u+u/2+r,u+1,1,W_); } } break;
    case XG_TERM: dstr_ctr(cx,cy-7,">_",W_,1); break;
    case XG_BOOK: fill_rect(bx+u+u/2,by+u+u/2,q-u*3,q-u*3,W_); fill_rect(bx+u+u/2,by+u+u/2,u,q-u*3,mix(c,W_,90)); fill_rect(bx+u*3+u/2,by+u*3,q-u*6,u/2+1,c); break;
    } }
static void fx_xicon(int x,int y,int s,int i){
    const fx_xi* d=&FXI[i]; u32 c=fx_hexc(d->c);
    int w=s*4/5, x0=x+(s-w)/2, f=s/4;
    fill_round(x0,y,w,s,s/10+1,15,rgb(198,204,216)); fill_round(x0+1,y+1,w-2,s-2,s/10,15,rgb(250,251,253));
    fill_rect(x0+w-f,y+1,f-1,f,rgb(232,236,242)); fill_rect(x0+w-f,y+f,f-1,1,rgb(198,204,216)); fill_rect(x0+w-f,y+1,1,f,rgb(198,204,216));
    if(s<24){ int q=w-4; fill_round(x0+2,y+s-q-2,q,q,2,15,c); return; }
    int q=w-s/5; if(q>s*3/5)q=s*3/5; int bx=x0+(w-q)/2, by=y+s-q-s/8;
    fill_round(bx,by,q,q,q/6+1,15,c); fill_round(bx,by+q/2,q,q/2,q/6+1,15,mix(c,rgb(0,0,0),230));
    fx_xglyph(bx,by,q,d->g,c,d->t); }
int fx_xkind(const char* name){ int i=fx_xfind(name); return i<0?-1:32+i; }
const char* fx_xname(int kind){ return kind>=32&&kind<32+NFXI?FXI[kind-32].n:0; }
static void fx_icon(int x,int y,int s,int kind){
    if(kind>=32&&kind<32+NFXI){ fx_xicon(x,y,s,kind-32); return; }
    if(kind==0&&g_dstyle==1){ int h=s*3/4, t=y+s/5; fill_rect(x,y+s/8,s*2/5,s/6,rgb(150,98,60)); fill_rect(x+1,y+s/8+1,s*2/5-2,s/6,rgb(214,160,116)); fill_rect(x,t,s,h,rgb(150,98,60)); fill_rect(x+1,t+1,s-2,h-2,rgb(222,172,128)); fill_rect(x+1,t+s/8,s-2,h-s/8-1,rgb(236,196,156)); fill_rect(x+1,t+s/8,s-2,1,rgb(248,222,192)); return; }
    if(kind==0){ int h=s*3/4; fill_round(x,y+s/8,s*2/5,s/5,2,15,rgb(226,178,80)); fill_round(x,y+s/5,s,h,s/10+1,15,rgb(236,190,92)); fill_round(x,y+s/3,s,h-s/7,s/10+1,15,rgb(248,208,112)); return; }
    u32 body=rgb(246,248,252), band=rgb(150,160,180);
    if(kind==2)band=rgb(70,130,230); else if(kind==3)band=rgb(60,170,110); else if(kind==4)band=rgb(170,120,60);
    else if(kind==5)band=rgb(120,128,146); else if(kind==6)band=rgb(140,90,210); else if(kind==7)band=rgb(230,110,60);
    int w=s*4/5, x0=x+(s-w)/2; fill_round(x0,y,w,s,s/8+1,15,rgb(206,212,224)); fill_round(x0+1,y+1,w-2,s-2,s/8,15,body);
    fill_rect(x0+1,y+s-s/3,w-2,s/3-1,mix(band,rgb(255,255,255),60));
    if(s>=28){ if(kind==2){ dstr(x0+w/2-12,y+s/4,"</>",band,1); } else if(kind==3){ fill_circle(x0+w/3,y+s/3,s/10+1,rgb(240,190,70)); }
        else if(kind==4){ for(int k=0;k<4;k++)fill_rect(x0+w/2-2,y+3+k*4,4,2,band); } else if(kind==6){ dstr(x0+w/2-12,y+s/4,"EXE",band,1); }
        else { for(int k=0;k<3;k++)fill_rect(x0+5,y+6+k*5,w-10,2,rgb(196,204,220)); } }
    else { fill_rect(x0+3,y+4,w-6,1,rgb(196,204,220)); fill_rect(x0+3,y+7,w-6,1,rgb(196,204,220)); }
}

/* ---- navigation ---- */
static void fx_init(void){ if(fx_dir>=0&&DT[fx_dir].used)return; if(g_dstyle==1)fx_view=1; dt_init(); fx_trash(); int h=dt_child(0,"home"); fx_dir=(h>=0)?h:0; fx_hn=1; fx_hist[0]=fx_dir; fx_hp=0; }
void fx_session_reset(void){ fx_dir=-1; fx_mode=0; fx_hp=-1; fx_hn=0; fx_seldir=-1; }
void fx_go(int d){ if(d<0||!DT[d].used)return; { extern int ux_dir_ok(int,int); if(!ux_dir_ok(d,5)){ toast("You do not have permission to open this folder"); return; } } fx_mode=0; if(d==fx_dir)return; fx_dir=d; fx_hn=fx_hp+1; if(fx_hn>=32){ for(int i=1;i<32;i++)fx_hist[i-1]=fx_hist[i]; fx_hn=31; } fx_hist[fx_hn++]=d; fx_hp=fx_hn-1;
    fx_seldir=-1; g_fsel=-1; fx_scroll=0; fx_flen=0; fx_filter[0]=0; }
static void fx_back(void){ if(fx_mode){ fx_mode=0; return; } while(fx_hp>0){ fx_hp--; if(DT[fx_hist[fx_hp]].used){ fx_dir=fx_hist[fx_hp]; break; } } fx_seldir=-1; g_fsel=-1; fx_scroll=0; }
static void fx_fwd(void){ while(fx_hp<fx_hn-1){ fx_hp++; if(DT[fx_hist[fx_hp]].used){ fx_dir=fx_hist[fx_hp]; fx_mode=0; break; } } fx_seldir=-1; g_fsel=-1; fx_scroll=0; }
static void fx_up(void){ if(fx_mode){ fx_mode=0; return; } if(fx_dir>0&&DT[fx_dir].parent>=0){ int old=fx_dir; fx_go(DT[fx_dir].parent); fx_seldir=old; } }

/* ---- listing ---- */
static int fx_cmp2(int ka,int ia,int kb,int ib){   /* folders first, then by chosen key */
    if(ka!=kb)return ka-kb;
    const char* na=ka?FS[ia].name:DT[ia].name; const char* nb=kb?FS[ib].name:DT[ib].name; int r=0;
    if(ka==1&&fx_sort==1)r=FS[ia].len-FS[ib].len; else if(ka==1&&fx_sort==2)r=fx_icmp(fx_ext(na),fx_ext(nb));
    if(r==0)r=fx_icmp(na,nb); return fx_sdesc?-r:r; }
static void fx_build(void){
    fx_rn=0;
    for(int i=0;i<DN_MAX&&fx_rn<FX_MAXROWS;i++) if(DT[i].used&&i!=fx_dir&&DT[i].parent==fx_dir&&fx_match(DT[i].name)&&!(fx_dir==0&&seq(DT[i].name,".trash"))){ fx_rk[fx_rn]=0; fx_rid[fx_rn]=i; fx_rn++; }
    for(int k=0;k<FS_N&&fx_rn<FX_MAXROWS;k++) if(FS[k].used&&FS[k].dir==fx_dir&&fx_match(FS[k].name)&&!seq(FS[k].name,".trashinfo")){ fx_rk[fx_rn]=1; fx_rid[fx_rn]=k; fx_rn++; }
    for(int i=1;i<fx_rn;i++){ int tk=fx_rk[i], ti=fx_rid[i], j=i;
        while(j>0&&fx_cmp2(fx_rk[j-1],fx_rid[j-1],tk,ti)>0){ fx_rk[j]=fx_rk[j-1]; fx_rid[j]=fx_rid[j-1]; j--; }
        fx_rk[j]=tk; fx_rid[j]=ti; }
}
static int fx_selrow(void){ for(int r=0;r<fx_rn;r++){ if(fx_rk[r]==0&&fx_rid[r]==fx_seldir)return r; if(fx_rk[r]==1&&fx_rid[r]==g_fsel)return r; } return -1; }
static void fx_select_row(int r){ if(r<0||r>=fx_rn)return; if(fx_rk[r]==0){ fx_seldir=fx_rid[r]; g_fsel=-1; } else { g_fsel=fx_rid[r]; fx_seldir=-1; } }

/* ---- actions ---- */
static int fx_name_ok(const char* n){ if(!n[0])return 0; for(int i=0;n[i];i++)if(n[i]=='/')return 0; if(seq((char*)n,".")||seq((char*)n,".."))return 0; return 1; }
static int fx_exists(int dir,const char* n,int skip_file,int skip_dir){
    for(int k=0;k<FS_N;k++) if(k!=skip_file&&FS[k].used&&FS[k].dir==dir&&seq(FS[k].name,(char*)n))return 1;
    for(int i=0;i<DN_MAX;i++) if(i!=skip_dir&&DT[i].used&&DT[i].parent==dir&&seq(DT[i].name,(char*)n))return 1; return 0; }
static int fx_fname_used(const char* n,int skip){ (void)n; (void)skip; return 0; }   /* NVXFS keys files by name */
static void fx_persist(int k){ if(disk_ok&&k>=0&&FS[k].used)fs_sync(k); }
int py_open_slot(int k){ if(k<0||!FS[k].used||!fext_eq(FS[k].name,"py"))return 0; char p[200]; dt_path_n(FS[k].dir,p,150); int n=0; while(p[n])n++; if(n>1&&n<190)p[n++]='/'; for(int i=0;FS[k].name[i]&&n<198;i++)p[n++]=FS[k].name[i]; p[n]=0;
    static char cl[260]; char* q=cl; const char* a="idle3 '"; while(*a)*q++=*a++; for(int i=0;p[i]&&q<cl+250;i++)*q++=p[i]; *q++='\''; *q=0; extern void x_launch(const char*); x_launch(cl); return 1; }
void fx_open_file(int k){ if(k<0||!FS[k].used)return; { extern int perm_file(int,int); if((FS[k].mode&0777u)&&!perm_file(k,4)){ toast("You do not have permission to open this file"); return; } } int kd=fkind(k); if(py_open_slot(k))return; { extern int sysfiles_open(int); if(sysfiles_open(k))return; }
    if(fext_eq(FS[k].name,"pptx")){ pptx_load(k); open_win(W_PPTX); }
    else if(fext_eq(FS[k].name,"docx")){ extern int writer_open_slot(int); writer_open_slot(k); }
    else if(video_is_media_name(FS[k].name)){ video_open_slot(k); return; }
    else if(kd==1)g_imgview=k; else if(kd==2)browser_show_html(fsd(k),FS[k].len,FS[k].name);
    else if(kd==4||kd==3||fx_is_elf(k)){ g_fsel=k; fx_prev=1; toast(fx_is_elf(k)?"Program - run it from the Terminal":"Shown in the preview pane"); }
    else { editor_open(k); if(fext_eq(FS[k].name,"nvx"))editor_run(); } }
static void fx_open_row(int r){ if(r<0||r>=fx_rn)return; if(fx_rk[r]==0)fx_go(fx_rid[r]); else fx_open_file(fx_rid[r]); }
static int fx_trash(void){ dt_init(); int t=dt_child(0,".trash"); if(t<0){ t=dt_mkdir_at(0,".trash"); if(t>0){ DT[t].mode=040700u; DT[t].uid=g_uid; DT[t].gid=g_uid; dt_save(); } } return t; }
static int fx_trash_id(void){ return dt_child(0,".trash"); }
static int fx_in_trash(int d){ int t=fx_trash_id(); if(t<0)return 0; for(int g=0;d>0&&g<64;g++){ if(d==t)return 1; d=DT[d].parent; } return 0; }
static int fx_under(int x,int d){ for(int g=0;x>=0&&g<64;g++){ if(x==d)return 1; if(x==0)break; x=DT[x].parent; } return 0; }
static int fx_info_slot(int t){ for(int k=0;k<FS_N;k++) if(FS[k].used&&FS[k].dir==t&&seq(FS[k].name,".trashinfo"))return k; return -1; }
static char g_fx_ib[8192];
static int fx_info_text(int t){ int k=fx_info_slot(t); int n=0; if(k>=0){ n=FS[k].len; if(n>8191)n=8191; const char* sd=fsd(k); for(int i=0;i<n;i++)g_fx_ib[i]=sd[i]; } g_fx_ib[n]=0; return n; }
static void fx_info_store(int t,int n){
    int k=fx_info_slot(t);
    if(n<=0){ if(k>=0){ FS[k].used=0; fs_slot_freed(k); if(disk_ok)nvx3_flush_meta(); } return; }
    if(k<0){ for(int i=0;i<FS_N;i++) if(!FS[i].used){ k=i; break; } if(k<0)return; fs_newslot(k);
        scopy(FS[k].name,".trashinfo"); FS[k].used=1; FS[k].folder=0; FS[k].dir=t; FS[k].mode=0100600u; FS[k].uid=g_uid; FS[k].gid=g_uid; FS[k].nlink=1; }
    if(fs_set(k,g_fx_ib,n))return; if(disk_ok)nvx3_sync(k);
}
static int fx_info_find(int t,char kind,const char* nm,char* odir,char* oname){
    int n=fx_info_text(t); char* p=g_fx_ib; char* e=g_fx_ib+n;
    while(p<e){ char* l=p; while(p<e&&*p!='\n')p++; char* le=p; if(p<e)p++;
        if(le-l<4||l[0]!=kind||l[1]!='\t')continue; char* f[3]; int fc=0; char* q=l+2; f[fc++]=q;
        while(q<le&&fc<3){ if(*q=='\t'){ f[fc++]=q+1; } q++; } if(fc<3)continue;
        int L=(int)(f[1]-1-f[0]); int m=1; for(int i=0;i<L;i++) if(f[0][i]!=nm[i]){ m=0; break; } if(!m||nm[L])continue;
        int a=0; for(char* x=f[1];x<f[2]-1&&a<190;x++)odir[a++]=*x; odir[a]=0; a=0; for(char* x=f[2];x<le&&a<63;x++)oname[a++]=*x; oname[a]=0; return 1; }
    return 0;
}
static void fx_info_drop(int t,char kind,const char* nm){
    int n=fx_info_text(t); char out[8192]; int o=0; char* p=g_fx_ib; char* e=g_fx_ib+n;
    while(p<e){ char* l=p; while(p<e&&*p!='\n')p++; char* le=p; if(p<e)p++; int drop=0;
        if(le-l>=4&&l[0]==kind&&l[1]=='\t'){ char* q=l+2; int i=0; while(q<le&&*q!='\t'&&nm[i]&&*q==nm[i]){ q++; i++; } if(q<le&&*q=='\t'&&!nm[i])drop=1; }
        if(!drop){ for(char* x=l;x<le&&o<8190;x++)out[o++]=*x; if(o<8191)out[o++]='\n'; } }
    for(int i=0;i<o;i++)g_fx_ib[i]=out[i]; fx_info_store(t,o);
}
static void fx_info_add(int t,char kind,const char* nm,const char* odir,const char* oname){
    int n=fx_info_text(t); if(n>7600){ toast("The Trash index is full - empty the Trash"); return; } char* o=g_fx_ib+n; char* lim=g_fx_ib+8000;
    *o++=kind; *o++='\t'; for(const char* x=nm;*x&&o<lim;x++)*o++=*x; *o++='\t'; for(const char* x=odir;*x&&o<lim;x++)*o++=*x; *o++='\t'; for(const char* x=oname;*x&&o<lim;x++)*o++=*x; *o++='\n';
    fx_info_store(t,(int)(o-g_fx_ib));
}
static int fx_name_taken(int d,const char* nm){ for(int k=0;k<FS_N;k++) if(FS[k].used&&FS[k].dir==d&&seq(FS[k].name,(char*)nm))return 1; for(int i=0;i<DN_MAX;i++) if(DT[i].used&&i!=d&&DT[i].parent==d&&seq(DT[i].name,(char*)nm))return 1; return 0; }
static void fx_unique(int d,const char* nm,char* out,int cap){
    int L=0; while(nm[L])L++; int dot=-1; for(int i=L-1;i>0;i--) if(nm[i]=='.'){ dot=i; break; }
    for(int i=0;i<L&&i<cap-1;i++)out[i]=nm[i]; out[L<cap-1?L:cap-1]=0; if(!fx_name_taken(d,out))return;
    for(int n=2;n<1000;n++){ char suf[8]; char* q=suf; *q++=' '; *q++='('; q=udec(q,(u32)n); *q++=')'; *q=0; int sl=(int)slen(suf);
        int bl=dot>0?dot:L, el=dot>0?L-dot:0; if(bl+sl+el>cap-1)bl=cap-1-sl-el; if(bl<1)bl=1; int o=0;
        for(int i=0;i<bl;i++)out[o++]=nm[i]; for(int i=0;i<sl;i++)out[o++]=suf[i]; for(int i=0;i<el&&o<cap-1;i++)out[o++]=nm[dot+i]; out[o]=0;
        if(!fx_name_taken(d,out))return; }
}
static void fx_resync_tree(int d){ if(!disk_ok)return; for(int k=0;k<FS_N;k++) if(FS[k].used&&fx_under(FS[k].dir,d))nvx3_sync(k); nvx3_flush_meta(); dt_save(); }
static int fx_mkpath(const char* path){ dt_init(); int d=0; const char* p=path; while(*p=='/')p++;
    while(*p){ char c[64]; int n=0; while(*p&&*p!='/'&&n<63)c[n++]=*p++; c[n]=0; while(*p=='/')p++; int x=dt_child(d,c); if(x<0)x=dt_mkdir_at(d,c); if(x<0)return -1; d=x; }
    dt_save(); return d; }
static void fx_trash_file(int k){
    int t=fx_trash(); if(t<0){ toast("Trash is not available"); return; } if(fx_info_text(t)>7600){ toast("The Trash index is full - empty the Trash"); return; }
    char od[192]; dt_path(FS[k].dir,od); char on[64]; scopy(on,FS[k].name); char nn[64]; fx_unique(t,on,nn,64);
    FS[k].dir=t; scopy(FS[k].name,nn); fx_info_add(t,'F',nn,od,on);
    if(disk_ok){ nvx3_sync(k); nvx3_flush_meta(); }
    if(zip_slot==k)zip_slot=-1; if(view_slot==k)view_slot=-1; if(fx_clip==k)fx_clip=-1; if(g_fsel==k)g_fsel=-1;
}
static void fx_trash_folder(int d){
    int t=fx_trash(); if(t<0||d<=0||d==t){ toast("Trash is not available"); return; } if(fx_info_text(t)>7600){ toast("The Trash index is full - empty the Trash"); return; }
    char od[192]; dt_path(DT[d].parent,od); char on[64]; scopy(on,DT[d].name); char nn[64]; fx_unique(t,on,nn,64);
    DT[d].parent=t; scopy(DT[d].name,nn); fx_info_add(t,'D',nn,od,on); fx_resync_tree(d); if(fx_seldir==d)fx_seldir=-1;
}
static void fx_restore_sel(void){
    int t=fx_trash_id(); if(t<0)return; char od[192], on[64];
    if(g_fsel>=0&&FS[g_fsel].used&&FS[g_fsel].dir==t){ int k=g_fsel;
        if(!fx_info_find(t,'F',FS[k].name,od,on)){ extern char g_home[]; scopy(od,g_home); scopy(on,FS[k].name); }
        int d=dt_resolve_dir(od); if(d<0)d=fx_mkpath(od); if(d<0)d=0; char nn[64]; fx_unique(d,on,nn,64);
        fx_info_drop(t,'F',FS[k].name); FS[k].dir=d; scopy(FS[k].name,nn); if(disk_ok){ nvx3_sync(k); nvx3_flush_meta(); } g_fsel=-1;
        char b[96],*p=b; p=scopy(p,"Restored to "); p=scopy(p,od); *p=0; toast(b); return; }
    if(fx_seldir>0&&DT[fx_seldir].used&&DT[fx_seldir].parent==t){ int d0=fx_seldir;
        if(!fx_info_find(t,'D',DT[d0].name,od,on)){ extern char g_home[]; scopy(od,g_home); scopy(on,DT[d0].name); }
        int d=dt_resolve_dir(od); if(d<0)d=fx_mkpath(od); if(d<0)d=0; char nn[64]; fx_unique(d,on,nn,64);
        fx_info_drop(t,'D',DT[d0].name); DT[d0].parent=d; scopy(DT[d0].name,nn); fx_resync_tree(d0); fx_seldir=-1;
        char b[96],*p=b; p=scopy(p,"Restored to "); p=scopy(p,od); *p=0; toast(b); return; }
    toast("Select an item in Trash to restore");
}
static void fx_delete_file(int k){ if(k<0||!FS[k].used)return; if(!perm_file(k,2)){ toast("Permission denied"); return; }
    fs_drop_name(k); if(zip_slot==k)zip_slot=-1; if(view_slot==k)view_slot=-1; if(fx_clip==k)fx_clip=-1; if(g_fsel==k)g_fsel=-1; }
static int fx_rmtree(int d){ if(d<=0||!DT[d].used)return 0; int n=0;
    for(int i=0;i<DN_MAX;i++) if(DT[i].used&&i!=d&&DT[i].parent==d)n+=fx_rmtree(i);
    for(int k=0;k<FS_N;k++) if(FS[k].used&&FS[k].dir==d){ fx_delete_file(k); n++; }
    if(g_dcwd==d)g_dcwd=DT[d].parent>=0?DT[d].parent:0; DT[d].used=0; return n+1; }
static int fx_dir_empty(int d){ for(int i=0;i<DN_MAX;i++) if(DT[i].used&&i!=d&&DT[i].parent==d)return 0; for(int k=0;k<FS_N;k++) if(FS[k].used&&FS[k].dir==d)return 0; return 1; }
static void fx_empty_trash(void){ int t=fx_trash_id(); if(t<0)return; int n=0;
    for(int i=0;i<DN_MAX;i++) if(DT[i].used&&i!=t&&DT[i].parent==t){ n+=fx_rmtree(i); }
    for(int k=0;k<FS_N;k++) if(FS[k].used&&FS[k].dir==t&&!seq(FS[k].name,".trashinfo")){ fx_delete_file(k); n++; }
    fx_info_store(t,0); dt_save(); if(disk_ok)nvx3_flush_meta(); g_fsel=-1; fx_seldir=-1; toast(n?"Trash emptied":"Trash is already empty"); }
void fx_delete_sel(void){
    int intr=fx_in_trash(fx_dir), t=fx_trash_id();
    if(!intr){
        if(g_fsel>=0&&FS[g_fsel].used){ if(!perm_file(g_fsel,2)){ toast("Permission denied"); return; } fx_trash_file(g_fsel); toast("Moved to Trash"); return; }
        if(fx_seldir>0&&DT[fx_seldir].used){ if(fx_seldir==t){ toast("Use Empty Trash instead"); return; } if(!perm_dir(DT[fx_seldir].parent)){ toast("Permission denied"); return; } fx_trash_folder(fx_seldir); toast("Moved to Trash"); return; }
        return; }
    if(g_fsel>=0&&FS[g_fsel].used){ if(FS[g_fsel].dir==t)fx_info_drop(t,'F',FS[g_fsel].name); fx_delete_file(g_fsel); toast("Deleted permanently"); return; }
    if(fx_seldir>0&&DT[fx_seldir].used){ if(DT[fx_seldir].parent==t)fx_info_drop(t,'D',DT[fx_seldir].name); if(fx_dir_empty(fx_seldir)){ DT[fx_seldir].used=0; fx_seldir=-1; dt_save(); toast("Folder deleted permanently"); }
        else { fx_confirm=1; fx_confirm_dir=fx_seldir; } return; }
    toast("Select a file or folder first"); }
void fx_start_input(int mode,const char* init,int slot,int node){
    g_rename=1; g_renmode=mode; g_renslot=slot; g_rennode=node; g_renlen=0;
    if(init) while(init[g_renlen]&&g_renlen<63){ g_renbuf[g_renlen]=init[g_renlen]; g_renlen++; } g_renbuf[g_renlen]=0; }
void fx_rename_sel(void){ if(g_fsel>=0&&FS[g_fsel].used)fx_start_input(0,FS[g_fsel].name,g_fsel,-1); else if(fx_seldir>0&&DT[fx_seldir].used)fx_start_input(1,DT[fx_seldir].name,-1,fx_seldir); else toast("Select a file or folder first"); }
static void fx_paste(void){
    if(fx_clip<0||!FS[fx_clip].used){ toast("Clipboard is empty"); return; } if(fx_mode){ toast("Open a folder first"); return; }
    if(!perm_dir(fx_dir)||(fx_clipcut&&!perm_dir(FS[fx_clip].dir))){ toast("Permission denied"); return; }
    if(fx_clipcut){ if(FS[fx_clip].dir==fx_dir){ fx_clip=-1; return; } if(fx_exists(fx_dir,FS[fx_clip].name,fx_clip,-1)){ toast("A file with that name already exists here"); return; }
        FS[fx_clip].dir=fx_dir; fx_persist(fx_clip); g_fsel=fx_clip; fx_clip=-1; toast("Moved"); return; }
    int d=-1; for(int k=0;k<FS_N;k++) if(!FS[k].used){ d=k; break; } if(d<0){ toast("No free file slots"); return; }
    char nm[64]; scopy(nm,FS[fx_clip].name)[0]=0;
    if(fx_exists(fx_dir,nm,-1,-1)||fx_fname_used(nm,-1)){ const char* e=fx_ext(FS[fx_clip].name); int bl=(*e)?(int)(e-FS[fx_clip].name)-1:slen(FS[fx_clip].name);
        for(int n=2;n<100;n++){ char* p=nm; for(int i=0;i<bl&&i<40;i++)*p++=FS[fx_clip].name[i]; p=scopy(p," ("); p=udec(p,(u32)n); *p++=')'; if(*e){ *p++='.'; p=scopy(p,e); } *p=0; if(!fx_exists(fx_dir,nm,-1,-1)&&!fx_fname_used(nm,-1))break; } }
    fs_newslot(d); if(fs_set(d,fsd(fx_clip),FS[fx_clip].len)){ toast("Not enough memory"); return; }
    int m=0; while(nm[m]&&m<63){FS[d].name[m]=nm[m];m++;} FS[d].name[m]=0;
    FS[d].used=1; FS[d].folder=FS[fx_clip].folder; FS[d].dir=fx_dir; FS[d].mode=FS[fx_clip].mode?FS[fx_clip].mode:0100644u; FS[d].uid=g_uid; FS[d].gid=g_uid;
    fx_persist(d); g_fsel=d; fx_seldir=-1; toast("Pasted"); }
void fx_rename_commit(void){
    char n[64]; int L=g_renlen; if(L>63)L=63; for(int i=0;i<L;i++)n[i]=g_renbuf[i]; n[L]=0;
    while(L>0&&n[L-1]==' ')n[--L]=0;
    if(!fx_name_ok(n)){ toast("Invalid name"); g_rename=0; g_renmode=0; return; }
    { int pd=g_renmode==0?(g_renslot>=0&&FS[g_renslot].used?FS[g_renslot].dir:-1):g_renmode==1?(g_rennode>0&&DT[g_rennode].used?DT[g_rennode].parent:-1):fx_dir;
      if(pd>=0&&!perm_dir(pd)){ toast("Permission denied"); g_rename=0; g_renmode=0; return; } }
    if(g_renmode==0){ int k=g_renslot; if(k>=0&&FS[k].used){ if(seq(FS[k].name,n)){}
        else if(fx_exists(FS[k].dir,n,k,-1))toast("That name is already taken");
        else if(fx_fname_used(n,k))toast("File names must be unique on this disk");
        else { char on[64]; scopy(on,FS[k].name)[0]=0; int j=0; for(;n[j]&&j<63;j++)FS[k].name[j]=n[j]; FS[k].name[j]=0;
            if(zip_slot==k)zip_slot=-1; if(disk_ok){ nvx_delete_name(on); fx_persist(k); } toast("Renamed"); } } }
