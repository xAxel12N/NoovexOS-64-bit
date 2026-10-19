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
    js_run(ed_src,n,ed_run_cb);
    if(ed_con_n==0){ const char* m=js_err?"[program error]":"(no output)"; scopy(ed_con,m); ed_con_n=slen(ed_con); }
    else if(js_err && ed_con_n<5980){ scopy(ed_con+ed_con_n,"\n[error]"); ed_con_n+=8; }
}
void editor_reset(void){ for(int r=0;r<ED_ROWS;r++){ED[r][0]=0;ed_ll[r]=0;} ed_nl=1;ed_cx=ed_cy=ed_top=0;ed_dirty=0; }
void editor_open(int fsidx){
    editor_reset(); ed_file=fsidx; ed_py=0;
    const char*d=fsd(fsidx); int r=0,c=0; int dl=FS[fsidx].len;
    for(int k=0; k<dl && d[k] && r<ED_ROWS; k++){ if(d[k]=='\n'){ ED[r][c]=0; ed_ll[r]=c; r++; c=0; }
        else if(c<ED_COLS-1){ ED[r][c++]=d[k]; } }
    ED[r][c]=0; ed_ll[r]=c; ed_nl=r+1;
    open_win(W_EDITOR);
}
void editor_open_buf(const char*d,int len){
    editor_reset(); ed_file=-1; ed_py=0;
    int r=0,c=0;
    for(int k=0; k<len && r<ED_ROWS; k++){ char ch=d[k]; if(ch=='\r')continue;
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
    if(ed_file<0) return; long need=0; for(int r=0;r<ed_nl;r++)need+=ed_ll[r]+1; char*o=fs_wr(ed_file,need); if(!o){ toast("Not enough memory to save"); return; } int p=0;
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
char g_clip[1024]; int g_clip_len=0;
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
void editor_draw(int i){
    const char*nm = ed_file>=0?FS[ed_file].name:"untitled";
    if(ed_py)nm="main.py";
    char title[48],*p=title; p=scopy(p,ed_py?"Python IDE  -  ":"Editor  -  "); p=scopy(p,nm); if(ed_dirty){p=scopy(p,"  *");} *p=0;
    win_frame(i,title);
    int bx=WN[i].x+10,by=WN[i].y+TITLEH+8,bw=WN[i].w-20;
    /* toolbar */
    eb_save_x=bx; eb_save_y=by; eb_new_x=bx+eb_bw+10; eb_new_y=by;
    fill_round(eb_save_x,eb_save_y,eb_bw,eb_bh,6,15,ACC); dstr_ctr(eb_save_x+eb_bw/2,eb_save_y+6,"Save",rgb(255,255,255),1);
    fill_round(eb_new_x,eb_new_y,eb_bw,eb_bh,6,15,rgb(90,100,120)); dstr_ctr(eb_new_x+eb_bw/2,eb_new_y+6,"New",rgb(255,255,255),1);
    eb_run_x=eb_new_x+eb_bw+10; eb_run_y=by;
    fill_round(eb_run_x,eb_run_y,eb_bw,eb_bh,6,15,rgb(64,180,96)); dstr_ctr(eb_run_x+eb_bw/2,eb_run_y+6,"Run",rgb(255,255,255),1);
    int ty=by+eb_bh+8, th=WN[i].h-(ty-WN[i].y)-12;
    int conh = ed_show_con ? (th*42/100) : 0; int edh = th-conh-(conh?8:0);
    fill_round(bx,ty,bw,edh,6,15,rgb(252,252,254));
    int rows=edh/16, ox=bx+10, oy=ty+8;
    if(ed_cy<ed_top)ed_top=ed_cy; if(ed_cy>=ed_top+rows)ed_top=ed_cy-rows+1;
    for(int vr=0;vr<rows;vr++){ int r=ed_top+vr; if(r>=ed_nl)break;
        if(ed_py){ u32 col[ED_COLS]; py_colors(ED[r],ed_ll[r],col); dchar(ox-8,oy+vr*16,' ',0,1);
            for(int c=0;c<ed_ll[r]&&c<ED_COLS;c++){ char ch=ED[r][c]; if(ch>=32) dchar(ox+c*8,oy+vr*16,ch,col[c],1); } continue; }
        for(int c=0;c<ed_ll[r]&&c<ED_COLS;c++){ char ch=ED[r][c]; if(ch>=32) dchar(ox+c*8,oy+vr*16,ch,rgb(30,36,52),1); } }
    if(((g_ticks/50)&1)&&focus==i){ int vy=ed_cy-ed_top; if(vy>=0&&vy<rows) fill_rect(ox+ed_cx*8,oy+vy*16,2,14,ACC); }
    if(ed_show_con){ int cty=ty+edh+8;
        fill_round(bx,cty,bw,conh,6,15,rgb(20,24,32));
        dstr(bx+10,cty+7,"Output",rgb(120,200,140),1);
        eb_conx_x=bx+bw-22; eb_conx_y=cty+6; dstr(eb_conx_x,cty+6,"x",rgb(180,120,120),1);
        int crows=(conh-26)/14, cx=bx+10, cyy=cty+24, col=0, ln=0;
        for(int k=0;k<ed_con_n && ln<crows;k++){ char c=ed_con[k];
            if(c=='\n'||col>=(bw-20)/8){ ln++; col=0; if(c=='\n')continue; }
            if(ln<crows && c>=32) dchar(cx+col*8,cyy+ln*14,c,rgb(210,220,210),1); col++; }
    }
}

