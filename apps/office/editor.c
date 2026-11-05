#include "include/nvx/base.h"
#include "include/nvx/lib/base/fmt.h"
#include "include/nvx/drivers/video/fb/framebuffer.h"
#include "include/nvx/lib/js/js.h"
#include "include/nvx/drivers/usb/xhci.h"
#include "include/nvx/net/http/http.h"
#include "include/nvx/graphics/theme/theme.h"
#include "include/nvx/graphics/ui/wm.h"
#include "include/nvx/fs/vfs/ramfs.h"
#include "include/nvx/fs/nvx/nvxfs.h"
#include "include/nvx/fs/nvx/nvx4.h"
#include "include/nvx/fs/vfs/unixfs.h"
#include "include/nvx/apps/office/editor.h"
#include "include/nvx/fs/vfs/vfs.h"
#include "include/nvx/fs/vfs/sysroot.h"
#include "include/nvx/shell/terminal.h"
#include "include/nvx/shell/commands.h"
#include "include/nvx/core/proc/jobs.h"
#include "include/nvx/fs/vfs/dirtree.h"
/* =========================================================================
   Text editor (works on the RAM FS)
   ========================================================================= */
#define ED_ROWS 200
#define ED_COLS 200
static char ED[ED_ROWS][ED_COLS]; static int ed_ll[ED_ROWS];
int ed_nl=1, ed_cx=0, ed_cy=0, ed_top=0, ed_file=-1, ed_dirty=0;
int eb_save_x,eb_save_y,eb_new_x,eb_new_y,eb_bw=78,eb_bh=26;
int eb_run_x,eb_run_y,eb_conx_x,eb_conx_y;
static char ed_con[6000]; int ed_con_n=0, ed_show_con=0;
static char ed_src[ED_ROWS*40];
static void ed_run_cb(const char* s,int n){ for(int i=0;i<n && ed_con_n<5998;i++){ char c=s[i]; ed_con[ed_con_n++]=c; } ed_con[ed_con_n]=0; }
int ed_py=0;
#define PYIDE_FILE "/root/main.py"
int pyide_installed(void){ return sysroot_exists("/usr/share/applications/python3-ide.desktop"); }
static int ed_text(char* o,int cap){ int n=0; for(int r=0;r<ed_nl;r++){ for(int c=0;c<ed_ll[r]&&n<cap-2;c++)o[n++]=ED[r][c]; if(r<ed_nl-1&&n<cap-2)o[n++]='\n'; } o[n]=0; return n; }
static void py_save(void){ static char b[ED_ROWS*ED_COLS+ED_ROWS]; int n=ed_text(b,(int)sizeof(b)); if(n>0&&b[n-1]!='\n')b[n++]='\n';
    k_unlink(PYIDE_FILE); int fd=vfs_open(PYIDE_FILE,O_CREAT|O_TRUNC|O_WRONLY); if(fd<0){ toast("Cannot write " PYIDE_FILE); return; } vfs_write(fd,b,n); vfs_close(fd); ed_dirty=0; }
void pyide_run(void){ py_save(); static char cl[]="python3 " PYIDE_FILE; open_win(W_TERM); term_puts(cl); term_putc('\n'); run_cmd(cl); if(!g_tty_fg)term_prompt(); }
void pyide_open(void){
    if(!pyide_installed()){ toast("Python IDE is not installed - run: sudo apt install python3-ide"); return; }
    int k=k_find_file(PYIDE_FILE);
    if(k>=0){ editor_open(k); ed_file=-1; }
    else { u32 L=0; const u8* d=sysroot_get("/usr/lib/python3-ide/examples/hello.py",&L,0); if(d)editor_open_buf((const char*)d,(int)L); else editor_open_buf("print('Hello from Python 3')\n",28); }
    ed_py=1; ed_show_con=0; open_win(W_EDITOR); }
void editor_run(void){
    if(ed_py){ pyide_run(); return; }
    int n=0; for(int r=0;r<ed_nl;r++){ for(int c=0;c<ed_ll[r] && n<(int)sizeof(ed_src)-2;c++)ed_src[n++]=ED[r][c]; if(n<(int)sizeof(ed_src)-2)ed_src[n++]='\n'; } ed_src[n]=0;
    ed_con_n=0; ed_con[0]=0; ed_show_con=1;
    { extern int js_console; js_console=1; js_run(ed_src,n,ed_run_cb); js_console=0; }
    if(ed_con_n==0){ const char* m=js_err?"[program error]":"(no output)"; *scopy(ed_con,m)=0; ed_con_n=slen(ed_con); }
    else if(js_err && ed_con_n<5980){ *scopy(ed_con+ed_con_n,"\n[error]")=0; ed_con_n+=8; }
}
void editor_reset(void){ for(int r=0;r<ED_ROWS;r++){ED[r][0]=0;ed_ll[r]=0;} ed_nl=1;ed_cx=ed_cy=ed_top=0;ed_dirty=0; }
void editor_open(int fsidx){
    editor_reset(); ed_file=fsidx; ed_py=0; ed_show_con=0;
    const char*d=fsd(fsidx); int r=0,c=0; int dl=FS[fsidx].len;
    for(int k=0; k<dl && d[k] && r<ED_ROWS-1; k++){ if(d[k]=='\n'){ ED[r][c]=0; ed_ll[r]=c; r++; c=0; }
        else if(c<ED_COLS-1){ ED[r][c++]=d[k]; } }
    ED[r][c]=0; ed_ll[r]=c; ed_nl=r+1;
    open_win(W_EDITOR);
}
void editor_open_buf(const char*d,int len){
    editor_reset(); ed_file=-1; ed_py=0; ed_show_con=0;
    int r=0,c=0;
    for(int k=0; k<len && r<ED_ROWS-1; k++){ char ch=d[k]; if(ch=='\r')continue;
        if(ch=='\n'){ ED[r][c]=0; ed_ll[r]=c; r++; c=0; }
        else { if(ch=='\t')ch=' '; if((unsigned char)ch<32)ch='.'; if(c<ED_COLS-1) ED[r][c++]=ch; } }
    ED[r][c]=0; ed_ll[r]=c; ed_nl=r+1;
    open_win(W_EDITOR);
}
/* read a USB file (by ufs index) into the editor */
void ed_load_usb(int fi){ extern int ufat_read(int,char*,int); static char ubuf[16000];
    int n=ufat_read(fi,ubuf,(int)sizeof(ubuf)-1); if(n<0)n=0; ubuf[n]=0; editor_open_buf(ubuf,n); }
void editor_save(void){
    if(ed_py){ py_save(); toast("Saved " PYIDE_FILE); return; }
    if(ed_file<0) return; { extern int perm_file(int,int); if(((FS[ed_file].mode&0777u)&&!perm_file(ed_file,2))||(FS[ed_file].mode&0170000u)==0120000u){ toast("Permission denied - you cannot change this file"); return; } } long need=0; for(int r=0;r<ed_nl;r++)need+=ed_ll[r]+1; char*o=fs_wr(ed_file,need); if(!o){ toast("Not enough memory to save"); return; } int p=0;
    for(int r=0;r<ed_nl && p<FS_CAP-1;r++){ for(int c=0;c<ed_ll[r]&&p<FS_CAP-1;c++) o[p++]=ED[r][c]; if(r<ed_nl-1&&p<FS_CAP-1)o[p++]='\n'; }
    o[p]=0; FS[ed_file].len=p; ux_touch(ed_file,1); if(disk_ok)fs_sync(ed_file); ed_dirty=0; toast("Saved");
}
static void ed_ins(char c){ int L=ed_ll[ed_cy]; if(L>=ED_COLS-1)return;
    for(int k=L;k>ed_cx;k--) ED[ed_cy][k]=ED[ed_cy][k-1]; ED[ed_cy][ed_cx]=c; ed_ll[ed_cy]=L+1; ED[ed_cy][L+1]=0; ed_cx++; ed_dirty=1; }
static void ed_enter(void){ if(ed_nl>=ED_ROWS)return;
    for(int r=ed_nl;r>ed_cy+1;r--){ for(int c=0;c<=ed_ll[r-1];c++)ED[r][c]=ED[r-1][c]; ed_ll[r]=ed_ll[r-1]; }
    int L=ed_ll[ed_cy],c2=0; for(int k=ed_cx;k<L;k++)ED[ed_cy+1][c2++]=ED[ed_cy][k]; ED[ed_cy+1][c2]=0; ed_ll[ed_cy+1]=c2;
    ED[ed_cy][ed_cx]=0; ed_ll[ed_cy]=ed_cx; ed_nl++; ed_cy++; ed_cx=0; ed_dirty=1; }
static void ed_bs(void){ if(ed_cx>0){ int L=ed_ll[ed_cy]; for(int k=ed_cx-1;k<L;k++)ED[ed_cy][k]=ED[ed_cy][k+1]; ed_ll[ed_cy]=L-1; ed_cx--; ed_dirty=1; }
    else if(ed_cy>0){ int pl=ed_ll[ed_cy-1],L=ed_ll[ed_cy]; if(pl+L<ED_COLS-1){ for(int k=0;k<L;k++)ED[ed_cy-1][pl+k]=ED[ed_cy][k]; ED[ed_cy-1][pl+L]=0; ed_ll[ed_cy-1]=pl+L;
        for(int r=ed_cy;r<ed_nl-1;r++){ for(int c=0;c<=ed_ll[r+1];c++)ED[r][c]=ED[r+1][c]; ed_ll[r]=ed_ll[r+1]; } ed_nl--; ed_cy--; ed_cx=pl; ed_dirty=1; } } }
char g_clip[65536]; int g_clip_len=0;
void editor_key(char c){
    if(kdown[0x1D]){                                        /* Ctrl held: clipboard */
        if(c=='c'||c==3){ int L=ed_ll[ed_cy]; if(L>1020)L=1020; for(int k=0;k<L;k++)g_clip[k]=ED[ed_cy][k]; g_clip_len=L; return; }
        if(c=='x'||c==24){ int L=ed_ll[ed_cy]; if(L>1020)L=1020; for(int k=0;k<L;k++)g_clip[k]=ED[ed_cy][k]; g_clip_len=L; ed_ll[ed_cy]=0; ED[ed_cy][0]=0; ed_cx=0; ed_dirty=1; return; }
        if(c=='v'||c==22){ for(int k=0;k<g_clip_len;k++){ if(g_clip[k]=='\n')ed_enter(); else ed_ins(g_clip[k]); } return; }
    }
    if(c=='\n') ed_enter();
    else if(c=='\b') ed_bs();
    else if(c==17){ if(ed_cx>0)ed_cx--; else if(ed_cy>0){ed_cy--;ed_cx=ed_ll[ed_cy];} }   /* left  */
    else if(c==18){ if(ed_cx<ed_ll[ed_cy])ed_cx++; else if(ed_cy<ed_nl-1){ed_cy++;ed_cx=0;} } /* right */
    else if(c==19){ if(ed_cy>0){ed_cy--; if(ed_cx>ed_ll[ed_cy])ed_cx=ed_ll[ed_cy];} }        /* up    */
    else if(c==20){ if(ed_cy<ed_nl-1){ed_cy++; if(ed_cx>ed_ll[ed_cy])ed_cx=ed_ll[ed_cy];} }   /* down  */
    else if(c>=32&&c<127) ed_ins(c);
}
static int py_idc(char c){ return (c>='a'&&c<='z')||(c>='A'&&c<='Z')||(c>='0'&&c<='9')||c=='_'; }
static void py_colors(const char* t,int n,u32* col){
    static const char* const kw[]={"False","None","True","and","as","assert","async","await","break","class","continue","def","del","elif","else","except","finally","for","from","global","if","import","in","is","lambda","nonlocal","not","or","pass","raise","return","try","while","with","yield","match","case",0};
    static const char* const bi[]={"print","input","len","range","int","str","float","list","dict","set","tuple","open","enumerate","zip","map","filter","sum","min","max","abs","sorted","type","isinstance","super","self",0};
    int i=0; while(i<n){ char c=t[i];
        if(c=='#'){ while(i<n)col[i++]=rgb(110,140,110); break; }
        if(c=='"'||c=='\''){ char q=c; col[i++]=rgb(196,110,40); while(i<n){ char d=t[i]; col[i++]=rgb(196,110,40); if(d=='\\'&&i<n){ col[i++]=rgb(196,110,40); continue; } if(d==q)break; } continue; }
        if(c>='0'&&c<='9'&&(i==0||!py_idc(t[i-1]))){ while(i<n&&(py_idc(t[i])||t[i]=='.'))col[i++]=rgb(30,110,200); continue; }
        if(py_idc(c)){ int s0=i; while(i<n&&py_idc(t[i]))i++; int L=i-s0; u32 cc=rgb(30,36,52);
            for(int k=0;kw[k];k++){ int m=1; int q=0; for(;kw[k][q];q++) if(q>=L||t[s0+q]!=kw[k][q]){ m=0; break; } if(m&&q==L){ cc=rgb(150,60,180); break; } }
            if(cc==rgb(30,36,52)) for(int k=0;bi[k];k++){ int m=1; int q=0; for(;bi[k][q];q++) if(q>=L||t[s0+q]!=bi[k][q]){ m=0; break; } if(m&&q==L){ cc=rgb(20,130,140); break; } }
            if(s0>=4&&t[s0-1]==' '&&((t[s0-2]=='f'&&t[s0-3]=='e'&&t[s0-4]=='d')||(t[s0-2]=='s'&&t[s0-3]=='s'&&t[s0-4]=='a')))cc=rgb(30,90,200);
            for(int k=s0;k<i;k++)col[k]=cc; continue; }
        col[i++]=rgb(30,36,52); } }
extern void mono_glyph(int x,int y,char ch,u32 c);
#define EI_INK rgb(46,46,52)
#define EI_DIM rgb(128,128,136)
#define EI_LINE rgb(222,222,226)
#define EI_HDR rgb(242,242,244)
#define EI_GUT rgb(250,250,251)
#define EI_CUR rgb(240,244,252)
#define EI_LH 20
static int eh_n=0, eh_x[8], eh_y[8], eh_w[8], eh_h[8], eh_id[8];
static int ed_tx=0, ed_ty=0, ed_rows=0, ed_hdr_lo=0, ed_gw=0;
static void eh_add(int x,int y,int w,int h,int id){ if(eh_n<8){ eh_x[eh_n]=x; eh_y[eh_n]=y; eh_w[eh_n]=w; eh_h[eh_n]=h; eh_id[eh_n]=id; eh_n++; } }
static void ebold(int x,int y,const char* t,u32 c){ pdstr(x,y,t,c,1); pdstr(x+1,y,t,c,1); }
static int ed_lang(void){ if(ed_py)return 1; if(ed_file>=0){ const char* n=FS[ed_file].name; int L=slen(n); if(L>3&&n[L-3]=='.'&&n[L-2]=='j'&&n[L-1]=='s')return 2; if(L>4&&n[L-4]=='.'&&n[L-3]=='n'&&n[L-2]=='v'&&n[L-1]=='x')return 2; if(L>3&&n[L-3]=='.'&&n[L-2]=='p'&&n[L-1]=='y')return 1; } return 0; }
static void ed_icon(int k,int x,int y,u32 c){
    if(k==0){ fill_rect(x+7,y+2,2,12,c); fill_rect(x+2,y+7,12,2,c); }
    else if(k==1){ fill_rect(x+2,y+2,12,12,c); fill_rect(x+4,y+3,8,4,rgb(255,255,255)); fill_rect(x+4,y+9,8,4,rgb(255,255,255)); fill_rect(x+9,y+4,2,2,c); }
    else if(k==2){ for(int r=0;r<12;r++){ int w=r<6?r+1:12-r; fill_rect(x+4,y+2+r,w,1,c); } }
    else { for(int t=0;t<8;t++){ fill_rect(x+4+t,y+4+t,2,1,c); fill_rect(x+11-t,y+4+t,2,1,c); } } }
static void ed_hbtn(int x,int y,int w,int k,const char* t,int id,int accent){
    int hov=in_rect(mx,my,x,y,w,32); u32 bg=accent?(hov?mix(ACC,rgb(0,0,0),30):ACC):(hov?rgb(222,222,226):rgb(232,232,236)); u32 fg=accent?rgb(255,255,255):EI_INK;
    fill_round(x,y,w,32,8,15,bg); ed_icon(k,x+10,y+8,fg); if(t)pdstr(x+32,y+8,t,fg,1); eh_add(x,y,w,32,id); }
static void ed_winbtn(int b,int bx,int cy){ int hov=in_rect(mx,my,bx-1,cy-10,20,20); fill_circle(bx+9,cy,9,hov?rgb(210,210,215):rgb(226,226,230)); u32 g=EI_INK;
    if(b==0)fill_rect(bx+6,cy+2,7,2,g);
    else if(b==1){ fill_rect(bx+6,cy-3,7,1,g); fill_rect(bx+6,cy+3,7,1,g); fill_rect(bx+6,cy-3,1,7,g); fill_rect(bx+12,cy-3,1,7,g); fill_rect(bx+6,cy-2,7,1,g); }
    else { for(int t=0;t<6;t++){ fill_rect(bx+6+t,cy-3+t,2,1,g); fill_rect(bx+11-t,cy-3+t,2,1,g); } } }
void editor_draw(int i){
    eh_n=0;
    const char*nm = ed_file>=0?FS[ed_file].name:"Untitled";
    if(ed_py)nm="main.py";
    int X=WN[i].x, Y=WN[i].y, ww=WN[i].w, wh=WN[i].h, foc=(focus==i), HB=48, SB=28;
    shadow_soft(X,Y,ww,wh,12);
    fill_round(X,Y,ww,wh,12,15,foc?rgb(190,190,196):rgb(210,210,214)); fill_round(X+1,Y+1,ww-2,wh-2,11,15,rgb(255,255,255));
    fill_round(X+1,Y+1,ww-2,HB,11,1|2,EI_HDR); fill_rect(X+1,Y+HB,ww-2,1,EI_LINE); ed_hdr_lo=Y+TITLEH;
    int bx=X+10; int lang=ed_lang();
    ed_hbtn(bx,Y+8,44,0,0,1,0); bx+=48;
    { int w=pw_str("Save",1)+44; ed_hbtn(bx,Y+8,w,1,"Save",2,0); bx+=w+4; }
    if(lang==2||ed_py){ int w=pw_str("Run",1)+44; ed_hbtn(bx,Y+8,w,2,"Run",3,1); bx+=w+4; }
    { char sub[80]; if(ed_dirty)*scopy(sub,"Modified")=0; else if(ed_py)*scopy(sub,"Python IDE")=0; else if(ed_file>=0){ dt_path_n(FS[ed_file].dir,sub,(int)sizeof sub); } else *scopy(sub,"Not saved yet")=0;
      int cx=X+ww/2; int tw=pw_str(nm,1)+1; int lim=ww-2*(bx-X)-80; if(tw<=lim)ebold(cx-tw/2,Y+9,nm,EI_INK);
      int sw=pw_str(sub,1); if(sw<=lim)pdstr(cx-sw/2,Y+26,sub,EI_DIM,1); }
    for(int b=0;b<3;b++){ int cx=X+ww-6-18; int wx=(b==2)?cx:(b==1)?cx-2-18:cx-2-18-18; ed_winbtn(b,wx,Y+16); }
    int ty=Y+HB+1, th=wh-HB-1-SB;
    int conh=ed_show_con?(th*38/100):0; int edh=th-conh;
    int dig=1; for(int n=ed_nl;n>=10;n/=10)dig++; if(dig<3)dig=3; ed_gw=dig*8+24;
    fill_rect(X+1,ty,ed_gw,edh,EI_GUT); fill_rect(X+1+ed_gw,ty,1,edh,rgb(236,236,239));
    int rows=(edh-12)/EI_LH; if(rows<1)rows=1; ed_rows=rows; ed_tx=X+1+ed_gw+12; ed_ty=ty+8;
    if(ed_cy<ed_top)ed_top=ed_cy; if(ed_cy>=ed_top+rows)ed_top=ed_cy-rows+1;
    int maxc=(X+ww-14-ed_tx)/8;
    for(int vr=0;vr<rows;vr++){ int r=ed_top+vr; if(r>=ed_nl)break; int ly=ed_ty+vr*EI_LH;
        if(r==ed_cy&&foc){ fill_rect(X+1+ed_gw+1,ly-3,ww-ed_gw-3,EI_LH,EI_CUR); fill_rect(X+1,ly-3,ed_gw,EI_LH,rgb(238,240,246)); }
        { char nb[12]; int k=0; int v=r+1; char t[12]; int q=0; while(v){ t[q++]=(char)('0'+v%10); v/=10; } while(q)nb[k++]=t[--q]; nb[k]=0;
          for(int c=0;c<k;c++) mono_glyph(X+1+ed_gw-12-(k-c)*8,ly,nb[c],r==ed_cy?EI_INK:rgb(160,160,168)); }
        u32 col[ED_COLS]; if(lang==1)py_colors(ED[r],ed_ll[r],col);
        for(int c=0;c<ed_ll[r]&&c<maxc;c++){ char ch=ED[r][c]; if(ch>32) mono_glyph(ed_tx+c*8,ly,ch,lang==1?col[c]:EI_INK); } }
    if(((g_ticks/50)&1)==0&&foc){ int vy=ed_cy-ed_top; if(vy>=0&&vy<rows&&ed_cx<=maxc) fill_rect(ed_tx+ed_cx*8,ed_ty+vy*EI_LH-2,2,EI_LH-2,ACC); }
    if(ed_nl>rows){ int sh=edh-8, kh=sh*rows/ed_nl; if(kh<24)kh=24; int ky=ty+4+(sh-kh)*ed_top/(ed_nl-rows>0?ed_nl-rows:1); fill_round(X+ww-9,ky,5,kh,2,15,rgb(196,196,202)); }
    if(ed_show_con){ int cty=ty+edh; fill_rect(X+1,cty,ww-2,1,EI_LINE); fill_rect(X+1,cty+1,ww-2,conh-1,rgb(248,248,250));
        ebold(X+16,cty+10,"Output",EI_INK);
        eb_conx_x=X+ww-34; eb_conx_y=cty+7; { int hov=in_rect(mx,my,eb_conx_x,eb_conx_y,22,22); fill_circle(eb_conx_x+11,eb_conx_y+11,11,hov?rgb(222,222,226):rgb(236,236,240)); for(int t=0;t<6;t++){ fill_rect(eb_conx_x+8+t,eb_conx_y+8+t,2,1,EI_INK); fill_rect(eb_conx_x+13-t,eb_conx_y+8+t,2,1,EI_INK); } }
        eh_add(eb_conx_x,eb_conx_y,22,22,4);
        int crows=(conh-40)/18, cx=X+16, cyy=cty+34, col=0, ln=0, cmax=(ww-32)/8;
        for(int k=0;k<ed_con_n && ln<crows;k++){ char c=ed_con[k];
            if(c=='\n'||col>=cmax){ ln++; col=0; if(c=='\n')continue; }
            if(ln<crows && c>32){ mono_glyph(cx+col*8,cyy+ln*18,c,rgb(60,64,74)); } col++; } }
    { int sy=Y+wh-SB; fill_rect(X+1,sy,ww-2,1,EI_LINE); fill_round(X+1,sy+1,ww-2,SB-2,11,4|8,EI_HDR);
      char a[64],*p=a; p=scopy(p,"Ln "); p=udec(p,(u32)(ed_cy+1)); p=scopy(p,", Col "); p=udec(p,(u32)(ed_cx+1)); *p=0; pdstr(X+16,sy+7,a,EI_DIM,1);
      char b[64]; p=b; p=scopy(p,lang==1?"Python":lang==2?"JavaScript":"Plain Text"); p=scopy(p,"    "); p=udec(p,(u32)ed_nl); p=scopy(p,ed_nl==1?" line":" lines"); p=scopy(p,"    UTF-8"); *p=0;
      pdstr(X+ww-16-pw_str(b,1),sy+7,b,EI_DIM,1); }
}
static int ed_hit(int px,int py){ for(int k=eh_n-1;k>=0;k--) if(in_rect(px,py,eh_x[k],eh_y[k],eh_w[k],eh_h[k]))return eh_id[k]; return 0; }
int editor_click(int px,int py){ int id=ed_hit(px,py);
    if(id==1){ editor_reset(); ed_file=-1; ed_py=0; ed_show_con=0; return 1; }
    if(id==2){ editor_save(); return 1; }
    if(id==3){ editor_run(); return 1; }
    if(id==4){ ed_show_con=0; return 1; }
    if(ed_rows>0&&px>=ed_tx-12&&py>=ed_ty-3&&py<ed_ty+ed_rows*EI_LH-3){ int r=ed_top+(py-ed_ty+3)/EI_LH; if(r>=ed_nl)r=ed_nl-1; if(r<0)r=0; int c=(px-ed_tx+4)/8; if(c<0)c=0; if(c>ed_ll[r])c=ed_ll[r]; ed_cy=r; ed_cx=c; return 1; }
    return 0; }
int editor_title_click(int px,int py){ if(py>=ed_hdr_lo)return 0; if(ed_hit(px,py)){ editor_click(px,py); return 1; } return 0; }
