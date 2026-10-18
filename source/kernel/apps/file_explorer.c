#include "include/nvx/base.h"
#include "include/nvx/arch/x86_64/percpu.h"
#include "include/nvx/lib/fmt.h"
#include "include/nvx/drivers/video/framebuffer.h"
#include "include/nvx/arch/x86_64/cpuinfo.h"
#include "include/nvx/drivers/usb/xhci.h"
#include "include/nvx/net/http.h"
#include "include/nvx/gui/theme.h"
#include "include/nvx/gui/wm.h"
#include "include/nvx/fs/ramfs.h"
#include "include/nvx/drivers/storage/ata.h"
#include "include/nvx/fs/nvxfs.h"
#include "include/nvx/fs/dirtree.h"
#include "include/nvx/core/syscall.h"
#include "include/nvx/fs/nvx4.h"
#include "include/nvx/fs/unixfs.h"
#include "include/nvx/apps/editor.h"
#include "include/nvx/apps/browser.h"
#include "include/nvx/apps/files_classic.h"
#include "include/nvx/apps/file_explorer.h"
#include "include/nvx/apps/videoplayer.h"
#include "include/nvx/init/installer.h"
#include "include/nvx/fs/volumes.h"
/* =========================================================================
   File Explorer (v9.82) - browses the real Unix directory tree (DT + FS)
   ========================================================================= */
#define FX_MAXROWS 96
#define FX_MAXB    72
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
static void fx_btn(int x,int y,int w,int h,int id,int val){ if(fx_bn<FX_MAXB){ fx_bx[fx_bn]=x; fx_by[fx_bn]=y; fx_bw[fx_bn]=w; fx_bh[fx_bn]=h; fx_bid[fx_bn]=id; fx_bval[fx_bn]=val; fx_bn++; } }
static void fx_fit(char* o,const char* s,int maxch){ int n=slen(s); if(maxch<4)maxch=4; if(n<=maxch){ scopy(o,s)[0]=0; return; } int i=0; for(;i<maxch-2;i++)o[i]=s[i]; o[i++]='.'; o[i++]='.'; o[i]=0; }
int fx_is_elf(int k){ if(FS[k].len<4)return 0; if(FS[k].lazy)return (FS[k].mode&0111)!=0&&(FS[k].mode&0170000u)!=0120000u; const char* d=fsd(k); return (u8)d[0]==0x7F&&d[1]=='E'&&d[2]=='L'&&d[3]=='F'; }
static int fx_ikind(int k){   /* icon kind: 1 text 2 code 3 image 4 archive 5 binary 6 program 7 web */
    if(fx_is_elf(k))return 6; int f=fkind(k); if(f==1)return 3; if(f==4)return 4; if(f==2)return 7; if(f==3)return 5;
    const char* e=fx_ext(FS[k].name); const char* code[]={"c","h","cpp","hpp","rs","py","js","ts","sh","rn","nvx","s","asm","go","lua","json","css",0};
    for(int i=0;code[i];i++) if(fx_icmp(e,code[i])==0)return 2; return 1; }
static const char* fx_kname(int kind,int k){ if(kind==6)return "Program"; if(kind==3)return "Image"; if(kind==4)return "Archive"; return file_type_name(FS[k].name); }

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
/* ---- icons ---- */
static void fx_icon(int x,int y,int s,int kind){
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
static void fx_init(void){ if(fx_dir>=0&&DT[fx_dir].used)return; dt_init(); fx_trash(); int h=dt_child(0,"home"); fx_dir=(h>=0)?h:0; fx_hn=1; fx_hist[0]=fx_dir; fx_hp=0; }
void fx_go(int d){ if(d<0||!DT[d].used)return; fx_mode=0; if(d==fx_dir)return; fx_dir=d; fx_hn=fx_hp+1; if(fx_hn>=32){ for(int i=1;i<32;i++)fx_hist[i-1]=fx_hist[i]; fx_hn=31; } fx_hist[fx_hn++]=d; fx_hp=fx_hn-1;
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
void fx_open_file(int k){ if(k<0||!FS[k].used)return; int kd=fkind(k); if(py_open_slot(k))return;
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
        if(!drop){ for(char* x=l;x<le&&o<8190;x++)out[o++]=*x; out[o++]='\n'; } }
    for(int i=0;i<o;i++)g_fx_ib[i]=out[i]; fx_info_store(t,o);
}
static void fx_info_add(int t,char kind,const char* nm,const char* odir,const char* oname){
    int n=fx_info_text(t); char* o=g_fx_ib+n; char* lim=g_fx_ib+8000;
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
    int t=fx_trash(); if(t<0){ toast("Trash is not available"); return; }
    char od[192]; dt_path(FS[k].dir,od); char on[64]; scopy(on,FS[k].name); char nn[64]; fx_unique(t,on,nn,64);
    FS[k].dir=t; scopy(FS[k].name,nn); fx_info_add(t,'F',nn,od,on);
    if(disk_ok){ nvx3_sync(k); nvx3_flush_meta(); }
    if(zip_slot==k)zip_slot=-1; if(view_slot==k)view_slot=-1; if(fx_clip==k)fx_clip=-1; if(g_fsel==k)g_fsel=-1;
}
static void fx_trash_folder(int d){
    int t=fx_trash(); if(t<0||d<=0||d==t){ toast("Trash is not available"); return; }
    char od[192]; dt_path(DT[d].parent,od); char on[64]; scopy(on,DT[d].name); char nn[64]; fx_unique(t,on,nn,64);
    DT[d].parent=t; scopy(DT[d].name,nn); fx_info_add(t,'D',nn,od,on); fx_resync_tree(d); if(fx_seldir==d)fx_seldir=-1;
}
static void fx_restore_sel(void){
    int t=fx_trash_id(); if(t<0)return; char od[192], on[64];
    if(g_fsel>=0&&FS[g_fsel].used&&FS[g_fsel].dir==t){ int k=g_fsel;
        if(!fx_info_find(t,'F',FS[k].name,od,on)){ scopy(od,"/home"); scopy(on,FS[k].name); }
        int d=dt_resolve_dir(od); if(d<0)d=fx_mkpath(od); if(d<0)d=0; char nn[64]; fx_unique(d,on,nn,64);
        fx_info_drop(t,'F',FS[k].name); FS[k].dir=d; scopy(FS[k].name,nn); if(disk_ok){ nvx3_sync(k); nvx3_flush_meta(); } g_fsel=-1;
        char b[96],*p=b; p=scopy(p,"Restored to "); p=scopy(p,od); *p=0; toast(b); return; }
    if(fx_seldir>0&&DT[fx_seldir].used&&DT[fx_seldir].parent==t){ int d0=fx_seldir;
        if(!fx_info_find(t,'D',DT[d0].name,od,on)){ scopy(od,"/home"); scopy(on,DT[d0].name); }
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
        if(fx_seldir>0&&DT[fx_seldir].used){ if(fx_seldir==t){ toast("Use Empty Trash instead"); return; } fx_trash_folder(fx_seldir); toast("Moved to Trash"); return; }
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
    if(g_renmode==0){ int k=g_renslot; if(k>=0&&FS[k].used){ if(seq(FS[k].name,n)){}
        else if(fx_exists(FS[k].dir,n,k,-1))toast("That name is already taken");
        else if(fx_fname_used(n,k))toast("File names must be unique on this disk");
        else { char on[64]; scopy(on,FS[k].name)[0]=0; int j=0; for(;n[j]&&j<63;j++)FS[k].name[j]=n[j]; FS[k].name[j]=0;
            if(zip_slot==k)zip_slot=-1; if(disk_ok){ nvx_delete_name(on); fx_persist(k); } toast("Renamed"); } } }
    else if(g_renmode==1){ int d=g_rennode; if(d>0&&DT[d].used){ if(fx_exists(DT[d].parent,n,-1,d))toast("That name is already taken");
        else { int j=0; for(;n[j]&&j<63;j++)DT[d].name[j]=n[j]; DT[d].name[j]=0; dt_save(); toast("Renamed"); } } }
    else if(g_renmode==2){ if(fx_exists(fx_dir,n,-1,-1))toast("That name is already taken");
        else { int d=dt_mkdir_at(fx_dir,n); if(d<0)toast("Folder limit reached"); else { DT[d].mode=0040755u; DT[d].uid=g_uid; DT[d].gid=g_uid; dt_save(); fx_seldir=d; g_fsel=-1; toast("Folder created"); } } }
    else if(g_renmode==3){ if(fx_exists(fx_dir,n,-1,-1))toast("That name is already taken"); else if(fx_fname_used(n,-1))toast("File names must be unique on this disk");
        else { int k=-1; for(int s=0;s<FS_N;s++) if(!FS[s].used){k=s;break;}
            if(k<0)toast("No free file slots");
            else { fs_newslot(k); int j=0; for(;n[j]&&j<63;j++)FS[k].name[j]=n[j]; FS[k].name[j]=0; FS[k].used=1; FS[k].folder=0; FS[k].dir=fx_dir; FS[k].mode=0100644u; FS[k].uid=g_uid; FS[k].gid=g_uid;
                fx_persist(k); g_fsel=k; fx_seldir=-1; toast("File created"); } } }
    g_rename=0; g_renmode=0; }
static int tpc_sel=-1, tpc_props=-1, tpc_li=-1; static u64 tpc_lt=0;
static void tpc_open(int i);
void fx_do(int id,int val){
    switch(id){
    case 70: if(tpc_sel>=0)tpc_props=tpc_sel; break;
    case 71: if(tpc_sel>=0)tpc_open(tpc_sel); break;
    case 72: vol_scan(1); toast("Drives refreshed"); break;
    case 73: tpc_props=-1; break;
    case 74: open_win(W_SETTINGS); break;
    case 75: { if(val==tpc_li&&g_ticks-tpc_lt<45){ tpc_open(val); tpc_lt=0; } else { tpc_sel=val; tpc_li=val; tpc_lt=g_ticks; } } break;
    case 76: if(val>=0&&val<g_vol_n){ fx_mode=1; g_fsel=-1; fx_seldir=-1; tpc_sel=val; tpc_open(val); } break;
    case 1: fx_back(); break; case 2: fx_fwd(); break; case 3: fx_up(); break;
    case 4: if(fx_mode)fx_mode=0; fx_start_input(2,"New folder",-1,-1); break;
    case 5: if(fx_mode)fx_mode=0; fx_start_input(3,"untitled.txt",-1,-1); break;
    case 6: fx_rename_sel(); break; case 7: fx_delete_sel(); break;
    case 60: fx_restore_sel(); break; case 61: fx_empty_trash(); break;
    case 8: if(g_fsel>=0){ fx_clip=g_fsel; fx_clipcut=0; toast("Copied"); } else toast("Select a file to copy"); break;
    case 9: if(g_fsel>=0){ fx_clip=g_fsel; fx_clipcut=1; toast("Cut - paste it in another folder"); } else toast("Select a file to cut"); break;
    case 10: fx_paste(); break; case 11: if(fx_view!=val){ fx_view=val; fx_scroll=0; } break; case 50: if(g_uid!=0){ toast("Installing needs administrator rights - run: sudo noovexos_install"); break; } ip_open(ST_DESKTOP); break; case 12: fx_prev=!fx_prev; break;
    case 13: case 14: case 15: { int s=id-13; if(fx_sort==s)fx_sdesc=!fx_sdesc; else { fx_sort=s; fx_sdesc=0; } } break;
    case 16: fx_flen=0; fx_filter[0]=0; break;
    case 20: if(val==-2){ fx_mode=1; g_fsel=-1; fx_seldir=-1; } else if(val==-3){ g_files_usb=1; } else fx_go(val); break;
    case 40: fx_go(val); break;
    }
}

/* ---- drawing ---- */
static void fx_toolbtn2(int x,int y,int w,int h,const char* lbl,int g,int id,int on,int enabled){
    int hov=enabled&&in_rect(mx,my,x,y,w,h);
    u32 bg=on?mix(ACC,rgb(255,255,255),196):hov?rgb(232,236,244):rgb(247,248,250);
    fill_round(x,y,w,h,7,15,bg);
    u32 fg=enabled?(on?ACC2:rgb(58,66,86)):rgb(184,190,204); u32 gc=enabled?(on?ACC:(g>=4&&g<=10?ACC:rgb(90,100,120))):rgb(200,205,216);
    if(g==7&&enabled)gc=rgb(214,80,80);
    int tw=lbl?slen(lbl)*8:0, gw=g?15:0, sp=(g&&lbl)?7:0, sx=x+(w-(tw+gw+sp))/2;
    if(g)fx_glyph(sx,y+(h-14)/2,g,gc); if(lbl)dstr(sx+gw+sp,y+(h-14)/2,lbl,fg,1);
    if(enabled)fx_btn(x,y,w,h,id,0); }
static void fx_toolbtn(int x,int y,int w,int h,const char* lbl,int id,int on,int enabled){ fx_toolbtn2(x,y,w,h,lbl,0,id,on,enabled); }
extern u32 g_nvx_secs;
static long fx_disk_total(void){ if(disk_ok&&g_nvx_secs)return (long)g_nvx_secs*512L; if(disk_ok&&g_disk_sectors)return (long)g_disk_sectors*512L; return (long)(g_ram_mb?g_ram_mb:16)*1048576L; }
static void fx_disk_list(int x,int y){ extern int gdisk_count(void); extern u32 gdisk_size_of(int); extern const char* gdisk_name_of(int); extern int g_gdisk_probed;
    if(!g_gdisk_probed)return; int n=gdisk_count(); if(n<=0)return; char b[200],*p=b; p=scopy(p,"Physical disks: ");
    for(int i=0;i<n&&i<5;i++){ if(i)p=scopy(p,",  "); p=scopy(p,(char*)gdisk_name_of(i)); p=scopy(p," "); u64 mb=(u64)gdisk_size_of(i)*512ULL/1048576ULL; if(mb>=1024){ p=udec(p,(u32)(mb/1024)); *p++='.'; p=udec(p,(u32)((mb%1024)*10/1024)); p=scopy(p," GB"); } else { p=udec(p,(u32)mb); p=scopy(p," MB"); } }
    *p=0; dstr(x,y,b,rgb(110,118,140),1); }
static void tpc_size(char* o,u64 v){ static const char* U[5]={"bytes","KB","MB","GB","TB"}; int u=0; u64 d=1; while(u<4&&v>=d*1024ULL){ d*=1024ULL; u++; }
    if(!u){ char* p=udec(o,(u32)v); p=scopy(p," bytes"); *p=0; return; }
    u64 w=v/d, f=(v%d)*100/d; char* p=o; if(w>=100){ p=udec(p,(u32)w); } else if(w>=10){ p=udec(p,(u32)w); *p++='.'; p=udec(p,(u32)(f/10)); } else { p=udec(p,(u32)w); *p++='.'; *p++=(char)('0'+f/10); *p++=(char)('0'+f%10); }
    *p++=' '; p=scopy(p,(char*)U[u]); *p=0; }
static void tpc_commas(char* o,u64 v){ char t[32]; int n=0; do{ t[n++]=(char)('0'+v%10); v/=10; }while(v); int k=0; for(int i=n-1;i>=0;i--){ o[k++]=t[i]; if(i&&i%3==0)o[k++]=','; } o[k]=0; }
static void tpc_bold(int x,int y,const char* s,u32 c){ dstr(x,y,s,c,1); dstr(x+1,y,s,c,1); }
static void tpc_usage_c(nvol* v){ long used=0; for(int k=0;k<FS_N;k++)if(FS[k].used)used+=FS[k].len; long total=fx_disk_total(); { u64 t4=0,f4=0; n4_usage(&t4,&f4); if(t4){ total=(long)t4; used=(long)(t4-f4); } } if(used>total)used=total; v->total=(u64)total; v->freeb=(u64)(total-used); v->has_free=1; }
static void tpc_icon(int x,int y,int kind,int big){
    int s=big?48:16;
    if(kind==VOL_USB){ int bw=s*3/4, bh=s*3/8, bx=x+(s-bw)/2+s/10, by=y+s/2-bh/2+s/8;
        fill_round(bx-s/6,by+bh/4,s/6+1,bh/2,1,15,rgb(176,182,192)); if(big){ fill_rect(bx-s/6+2,by+bh/4+3,3,3,rgb(120,126,138)); fill_rect(bx-s/6+2,by+bh/2+2,3,3,rgb(120,126,138)); }
        fill_round(bx,by,bw,bh,big?4:2,15,rgb(70,78,92)); fill_rect(bx+2,by+1,bw-4,big?3:1,rgb(110,118,134)); fill_circle(bx+bw-(big?7:3),by+bh-(big?6:3),big?2:1,rgb(110,220,140)); return; }
    int bw=s*7/8, bh=big?20:7, bx=x+(s-bw)/2, by=y+s-bh-(big?6:2);
    fill_round(bx,by,bw,bh,big?4:2,15,rgb(150,156,166)); fill_round(bx,by,bw,bh-(big?5:2),big?4:2,15,rgb(214,218,224)); fill_rect(bx+2,by+1,bw-4,1,rgb(240,242,245));
    fill_rect(bx+(big?5:2),by+bh-(big?4:2),bw-(big?18:7),1,rgb(120,126,136)); fill_circle(bx+bw-(big?7:3),by+bh-(big?3:2),big?2:1,rgb(80,200,120));
    if(kind==VOL_SYS){ int ls=big?20:7, lx=bx+(big?6:2), ly=by-ls+(big?6:2); fill_round(lx,ly,ls,ls,big?4:1,15,ACC); if(big){ dchar(lx+6,ly+3,'N',rgb(255,255,255),1); } } }
static void tpc_bar(int x,int y,int w,int h,u64 used,u64 total,int known){
    fill_rect(x,y,w,h,rgb(188,188,188)); fill_rect(x+1,y+1,w-2,h-2,rgb(230,230,230));
    if(!known||!total)return; u64 fw=used*(u64)(w-2)/total; if(used&&fw<2)fw=2; if(fw>(u64)(w-2))fw=(u64)(w-2);
    int red=used*10>total*9; fill_rect(x+1,y+1,(int)fw,h-2,red?rgb(218,38,38):rgb(38,160,218)); }
static int tpc_cols(int w){ int c=(w-24)/262; return c<1?1:c; }
static void tpc_open(int i){ if(i<0||i>=g_vol_n)return; nvol* v=&g_vol[i];
    if(v->kind==VOL_SYS){ fx_mode=0; fx_go(0); return; }
    if(v->kind==VOL_USB&&ufat_ok&&(v->fs[0]=='F')){ g_files_usb=1; return; }
    tpc_props=i; }
static int tpc_isin(int sx,int sy,int r2o,int r2i,int dx,int dy){ int d=dx*dx+dy*dy; (void)sx; (void)sy; return d<=r2o&&d>=r2i; }
static long long tpc_pa(long long ux,long long uy){ if(ux>=0&&uy>=0)return (ux+uy)?(ux*65536)/(ux+uy):0; if(ux>=0&&uy<0)return 65536+((-uy)*65536)/(ux-uy); if(ux<0&&uy<0)return 131072+((-ux)*65536)/(-ux-uy); return 196608+(uy*65536)/(uy-ux); }
static void tpc_pie(int cx,int cy,int R,int r,u64 used,u64 total,u32 cu,u32 cf){
    static const short SN[91]={0,175,349,523,698,872,1045,1219,1392,1564,1736,1908,2079,2250,2419,2588,2756,2924,3090,3256,3420,3584,3746,3907,4067,4226,4384,4540,4695,4848,5000,5150,5299,5446,5592,5736,5878,6018,6157,6293,6428,6561,6691,6820,6947,7071,7193,7314,7431,7547,7660,7771,7880,7986,8090,8192,8290,8387,8480,8572,8660,8746,8829,8910,8988,9063,9135,9205,9272,9336,9397,9455,9511,9563,9613,9659,9703,9744,9781,9816,9848,9877,9903,9925,9945,9962,9976,9986,9994,9998,10000};
    long long deg=total?(long long)(used*3600ULL/total):0; long long bp;
    if(deg>=3600)bp=262144; else { int d=(int)(deg/10); int q=d/90, rr=d%90; long long sx,cy2; int sn=SN[rr], cs=SN[90-rr];
        if(q==0){ sx=sn; cy2=cs; } else if(q==1){ sx=cs; cy2=-sn; } else if(q==2){ sx=-sn; cy2=-cs; } else { sx=-cs; cy2=sn; } bp=tpc_pa(sx,cy2); }
    for(int dy=-R;dy<=R;dy++) for(int dx=-R;dx<=R;dx++){ if(!tpc_isin(cx,cy,R*R,r*r,dx,dy))continue; long long p=tpc_pa(dx,-dy); u32 c=(p<bp)?cu:cf; int d2=dx*dx+dy*dy;
        int edge=(d2>(R-1)*(R-1))||(d2<(r+1)*(r+1)); putpx(cx+dx,cy+dy,edge?mix(c,rgb(255,255,255),110):c); } }
static void tpc_dialog(int wx,int wy,int ww,int wh){
    nvol* v=&g_vol[tpc_props]; if(v->kind==VOL_SYS)tpc_usage_c(v);
    for(int y=wy;y<wy+wh-1;y++)for(int x=wx+1;x<wx+ww-1;x++){ u32*pp=&SCENE[(u64)y*SW+x]; *pp=mix(*pp,rgb(20,24,34),70); }
    int dw=410, dh=560; if(dh>(int)H-50)dh=(int)H-50; int dx=((int)W-dw)/2, dy=((int)H-dh)/2-20; if(dy<4)dy=4; shadow_soft(dx,dy,dw,dh,8);
    fill_round(dx,dy,dw,dh,8,15,rgb(240,240,240)); fill_rect(dx,dy+31,dw,1,rgb(218,218,218));
    char nm[64]; vol_name(v,nm,64); char t[96],*p=t; p=scopy(p,nm); p=scopy(p," Properties"); *p=0; tpc_icon(dx+10,dy+8,v->kind,0); dstr(dx+34,dy+9,t,rgb(30,30,30),1);
    { int cx=dx+dw-46; int hv=in_rect(mx,my,cx,dy,46,31); if(hv)fill_round(cx,dy,46,31,8,2,rgb(196,43,28)); dstr_ctr(cx+23,dy+9,"x",hv?rgb(255,255,255):rgb(60,60,60),1); fx_btn(cx,dy,46,31,73,0); }
    int ty=dy+40; { const char* T[3]={"General","Tools","Hardware"}; int tx=dx+10; for(int k=0;k<3;k++){ int tw=slen(T[k])*8+20; if(k==0){ fill_rect(tx,ty,tw,26,rgb(255,255,255)); fill_rect(tx,ty,tw,1,rgb(200,200,200)); fill_rect(tx,ty,1,26,rgb(200,200,200)); fill_rect(tx+tw-1,ty,1,26,rgb(200,200,200)); }
        dstr(tx+10,ty+6,T[k],k==0?rgb(30,30,30):rgb(140,140,140),1); tx+=tw; } }
    int bx=dx+10, by=ty+26, bw=dw-20, bh=dh-(by-dy)-50; fill_rect(bx,by,bw,bh,rgb(255,255,255)); fill_rect(bx,by,bw,1,rgb(200,200,200)); fill_rect(bx,by+bh-1,bw,1,rgb(200,200,200)); fill_rect(bx,by,1,bh,rgb(200,200,200)); fill_rect(bx+bw-1,by,1,bh,rgb(200,200,200));
    int x=bx+16, y=by+16; tpc_icon(x,y-4,v->kind,1);
    fill_rect(x+70,y+6,bw-102,24,rgb(122,122,122)); fill_rect(x+71,y+7,bw-104,22,rgb(255,255,255)); dstr(x+76,y+10,v->label[0]?v->label:"",rgb(30,30,30),1); y+=52;
    fill_rect(x,y,bw-32,1,rgb(222,222,222)); y+=12;
    int c2=x+110; dstr(x,y,"Type:",rgb(30,30,30),1); dstr(c2,y,v->kind==VOL_USB?"USB Drive":"Local Disk",rgb(30,30,30),1); y+=22;
    dstr(x,y,"File system:",rgb(30,30,30),1); dstr(c2,y,v->fs[0]?v->fs:"Unknown",rgb(30,30,30),1); y+=22;
    dstr(x,y,"Location:",rgb(30,30,30),1); { char d[30]; fx_fit(d,v->dev,(bw-140)/8); dstr(c2,y,d,rgb(30,30,30),1); } y+=26;
    fill_rect(x,y,bw-32,1,rgb(222,222,222)); y+=12;
    u64 used=v->has_free&&v->total>=v->freeb?v->total-v->freeb:0; u32 cu=rgb(38,129,214), cf=rgb(214,214,214);
    char b1[32],b2[24]; int cb=x+124, cg=bx+bw-78;
    fill_rect(x,y+2,12,12,cu); dstr(x+18,y,"Used space:",rgb(30,30,30),1); if(v->has_free){ tpc_commas(b1,used); p=b1; while(*p)p++; scopy(p," bytes")[0]=0; tpc_size(b2,used); } else { scopy(b1,"unknown")[0]=0; b2[0]=0; } dstr(cb,y,b1,rgb(30,30,30),1); dstr(cg,y,b2,rgb(30,30,30),1); y+=22;
    fill_rect(x,y+2,12,12,cf); dstr(x+18,y,"Free space:",rgb(30,30,30),1); if(v->has_free){ tpc_commas(b1,v->freeb); p=b1; while(*p)p++; scopy(p," bytes")[0]=0; tpc_size(b2,v->freeb); } else { scopy(b1,"unknown")[0]=0; b2[0]=0; } dstr(cb,y,b1,rgb(30,30,30),1); dstr(cg,y,b2,rgb(30,30,30),1); y+=26;
    fill_rect(x,y,bw-32,1,rgb(222,222,222)); y+=10;
    dstr(x,y,"Capacity:",rgb(30,30,30),1); tpc_commas(b1,v->total); p=b1; while(*p)p++; scopy(p," bytes")[0]=0; tpc_size(b2,v->total); dstr(cb,y,b1,rgb(30,30,30),1); dstr(cg,y,b2,rgb(30,30,30),1); y+=24;
    int R=52, pcx=bx+bw/2, pcy=y+R+8; if(pcy+R+24>by+bh){ R=(by+bh-y-40)/2; pcy=y+R+8; } if(R>=20){ tpc_pie(pcx,pcy,R,R*5/10,v->has_free?used:0,v->has_free?v->total:1,cu,cf); char d[16]; p=d; p=scopy(p,"Drive "); *p++=v->letter; *p++=':'; *p=0; dstr_ctr(pcx,pcy+R+8,d,rgb(30,30,30),1); }
    int oy=dy+dh-40; const char* B[3]={"OK","Cancel","Apply"}; for(int k=0;k<3;k++){ int ox=dx+dw-10-(3-k)*86; int hv=k<2&&in_rect(mx,my,ox,oy,80,28);
        fill_round(ox,oy,80,28,4,15,k==0?(hv?rgb(0,95,184):rgb(0,103,192)):hv?rgb(229,229,229):rgb(253,253,253)); if(k)fill_round(ox,oy,80,28,4,15,hv?rgb(229,229,229):rgb(253,253,253));
        if(k){ fill_rect(ox+3,oy,74,1,rgb(208,208,208)); fill_rect(ox+3,oy+27,74,1,rgb(188,188,188)); }
        dstr_ctr(ox+40,oy+7,B[k],k==0?rgb(255,255,255):k==2?rgb(160,160,160):rgb(30,30,30),1); if(k<2)fx_btn(ox,oy,80,28,73,0); } }
static void fx_draw_thispc(int x0,int y0,int w,int h){
    vol_scan(0); if(g_vol_n>0&&g_vol[0].kind==VOL_SYS)tpc_usage_c(&g_vol[0]); if(tpc_sel>=g_vol_n)tpc_sel=-1; if(tpc_props>=g_vol_n)tpc_props=-1;
    fill_rect(x0,y0,w,h,rgb(255,255,255));
    int y=y0+14; dstr(x0+20,y,"v",rgb(96,96,96),1); tpc_bold(x0+36,y,"Devices and drives",rgb(31,31,31)); fill_rect(x0+36+18*8+12,y+8,w-(36+18*8+12)-24,1,rgb(229,229,229));
    int cols=tpc_cols(w), tw=256, th=68, gx=x0+24, gy=y+30;
    for(int i=0;i<g_vol_n;i++){ nvol* v=&g_vol[i]; int tx=gx+(i%cols)*(tw+6), ty=gy+(i/cols)*(th+6); if(ty+th>y0+h-4)break;
        int sel=(i==tpc_sel), hov=in_rect(mx,my,tx,ty,tw,th);
        if(sel){ fill_rect(tx,ty,tw,th,rgb(153,209,255)); fill_rect(tx+1,ty+1,tw-2,th-2,rgb(204,232,255)); } else if(hov){ fill_rect(tx,ty,tw,th,rgb(229,243,255)); }
        tpc_icon(tx+6,ty+10,v->kind,1);
        char nm[48]; vol_name(v,nm,48); char nf[28]; fx_fit(nf,nm,24); dstr(tx+62,ty+6,nf,rgb(31,31,31),1);
        u64 used=v->has_free&&v->total>=v->freeb?v->total-v->freeb:0; tpc_bar(tx+62,ty+26,180,13,used,v->total,v->has_free);
        char b[64],*p=b; if(v->has_free){ char a1[20],a2[20]; tpc_size(a1,v->freeb); tpc_size(a2,v->total); p=scopy(p,a1); p=scopy(p," free of "); p=scopy(p,a2); } else { char a2[20]; tpc_size(a2,v->total); p=scopy(p,a2); } *p=0;
        dstr(tx+62,ty+46,b,rgb(109,109,109),1); fx_btn(tx,ty,tw,th,75,i); }
    { int rows=(g_vol_n+cols-1)/cols; int iy=gy+rows*(th+6)+24; if(iy+70<y0+h&&g_uid==0&&!disk_ok){ int iw=w-48; int hov=in_rect(mx,my,gx,iy,iw,56);
        fill_rect(gx,iy,iw,56,hov?rgb(229,243,255):rgb(243,243,243)); fill_circle(gx+28,iy+28,16,ACC); line_bl(gx+28,iy+19,gx+28,iy+34,rgb(255,255,255),255,1); line_bl(gx+22,iy+28,gx+28,iy+35,rgb(255,255,255),255,1); line_bl(gx+34,iy+28,gx+28,iy+35,rgb(255,255,255),255,1);
        dstr(gx+56,iy+12,"Install NoovexOS",rgb(31,31,31),1); dstr(gx+56,iy+30,"Put this system on a disk so it starts without the USB stick",rgb(109,109,109),1); fx_btn(gx,iy,iw,56,50,0); } }
}
static void fx_tpc_cmdbar(int wx,int wy,int ww){
    int y=wy+42, x=wx+10; int has=tpc_sel>=0&&tpc_sel<g_vol_n; int wide=ww>=760;
    fx_toolbtn2(x,y,wide?92:34,28,wide?"Properties":0,6,70,0,has); x+=wide?96:38;
    fx_toolbtn2(x,y,wide?66:34,28,wide?"Open":0,4,71,0,has); x+=wide?74:44; fill_rect(x-5,y+6,1,16,rgb(220,224,232));
    fx_toolbtn2(x,y,wide?82:34,28,wide?"Refresh":0,3,72,0,1); x+=wide?90:44; fill_rect(x-5,y+6,1,16,rgb(220,224,232));
    fx_toolbtn2(x,y,wide?120:34,28,wide?"Open Settings":0,0,74,0,1);
}
static void fx_preview(int px0,int py0,int pw,int ybot){
    fx_pvx0=px0; fx_pvy0=py0; fx_pvw=pw; fpv_edw=0; ze_n=0;
    if(fx_seldir>=0&&DT[fx_seldir].used){ fx_icon(px0+pw/2-28,py0+8,56,0); char b[40]; fx_fit(b,DT[fx_seldir].name,pw/16); dstr_ctr(px0+pw/2,py0+74,b,INK,2);
        int nd=0,nf=0; for(int i=0;i<DN_MAX;i++)if(DT[i].used&&i!=fx_seldir&&DT[i].parent==fx_seldir)nd++; for(int k=0;k<FS_N;k++)if(FS[k].used&&FS[k].dir==fx_seldir)nf++;
        char s[48],*p=s; p=udec(p,(u32)nd); p=scopy(p,nd==1?" folder, ":" folders, "); p=udec(p,(u32)nf); p=scopy(p,nf==1?" file":" files"); *p=0; dstr_ctr(px0+pw/2,py0+104,s,rgb(120,128,146),1);
        char pth[192]; dt_path(fx_seldir,pth); char pf[48]; fx_fit(pf,pth,pw/8); dstr_ctr(px0+pw/2,py0+124,pf,rgb(150,158,176),1);
        char ms[12]; mode_str(DT[fx_seldir].mode?DT[fx_seldir].mode:0040755u,ms); dstr_ctr(px0+pw/2,py0+144,ms,rgb(150,158,176),1); return; }
    if(g_fsel<0||!FS[g_fsel].used){ dstr_ctr(px0+pw/2,py0+(ybot-py0)/2-20,"Select a file",rgb(160,166,180),1); dstr_ctr(px0+pw/2,py0+(ybot-py0)/2,"to see a preview",rgb(160,166,180),1); return; }
    FsFile* f=&FS[g_fsel]; int kind=fkind(g_fsel), ik=fx_ikind(g_fsel);
    fx_icon(px0,py0+2,40,ik); char nb[40]; fx_fit(nb,f->name,(pw-52)/8); dstr(px0+50,py0+4,nb,INK,1);
    { char b[64],*p=b; p=scopy(p,(char*)fx_kname(ik,g_fsel)); *p=0; dstr(px0+50,py0+22,b,rgb(120,128,146),1); }
    { char b[48],*p=b; p=fmt_size(p,f->len); p=scopy(p,"   "); char ms[12]; mode_str(f->mode?f->mode:0100644u,ms); p=scopy(p,ms); *p=0; dstr(px0,py0+50,b,rgb(120,128,146),1); }
    const char* btn=(kind==1)?"Open image":kind==2?"Open in browser":(kind==0&&!fx_is_elf(g_fsel))?"Open in editor":0;
    int y=py0+72;
    if(btn){ fpv_edx=px0; fpv_edy=y; fpv_edw=pw; fpv_edh=28; fill_round(fpv_edx,fpv_edy,fpv_edw,fpv_edh,7,15,in_rect(mx,my,fpv_edx,fpv_edy,fpv_edw,fpv_edh)?ACC2:ACC); dstr_ctr(fpv_edx+pw/2,fpv_edy+7,btn,rgb(255,255,255),1); y+=38; }
    fill_rect(px0,y,pw,1,rgb(232,234,240)); y+=10; int ph=ybot-y;
    if(kind==1){ if(ensure_img(g_fsel)&&view_w>0) blit_img_fit(px0,y,pw,ph); else dstr(px0,y,"(cannot decode image)",rgb(178,120,120),1); }
    else if(kind==4){ zip_open(g_fsel);
        if(znent==0)dstr(px0,y,"Not a readable ZIP archive",rgb(120,128,146),1);
        else { char hb[40],*p=hb; p=udec(p,(u32)znent); scopy(p," entries - click to open")[0]=0; dstr(px0,y,hb,rgb(90,100,120),1); int ty=y+22;
            for(int e=0;e<znent&&ty<ybot-18;e++){ int sel=(e==zsel); if(sel)fill_round(px0-4,ty-3,pw+4,22,4,15,mix(ACC,rgb(255,255,255),204)); else if(in_rect(mx,my,px0-4,ty-3,pw+4,22))fill_round(px0-4,ty-3,pw+4,22,4,15,rgb(236,241,250));
                const char* nm=zent[e].name; int q=slen(nm); int isdir=(q>0&&(nm[q-1]=='/'||nm[q-1]=='\\')); fx_icon(px0,ty,14,isdir?0:1);
                char en[40]; fx_fit(en,nm,(pw-24)/8); dstr(px0+20,ty,en,INK,1); ze_y[e]=ty; ze_n++; ty+=22; } } }
    else if(fx_is_elf(g_fsel)||kind==3){ dstr(px0,y,fx_is_elf(g_fsel)?"Executable program.":"Binary file.",rgb(120,128,146),1); dstr(px0,y+18,fx_is_elf(g_fsel)?"Run it from the Terminal.":"No preview available.",rgb(150,158,176),1);
        int ty=y+44; for(int r=0;r<8&&r*8<f->len&&ty<ybot-14;r++){ char hx2[40],*p=hx2; for(int c=0;c<8&&r*8+c<f->len;c++){ p=hx(p,(u8)f->data[r*8+c],2); *p++=' '; } *p=0; dstr(px0,ty,hx2,rgb(150,158,176),1); ty+=16; } }
    else { int col=pw/8; if(col>80)col=80; int cx2=0; char lb[84]; int ty=y;
        for(int q=0;q<f->len&&f->data[q]&&ty<ybot-14;q++){ char ch=f->data[q];
            if(ch=='\n'||cx2>=col){ lb[cx2]=0; dstr(px0,ty,lb,rgb(60,66,84),1); ty+=16; cx2=0; if(ch=='\n')continue; }
            if(ch=='\r')continue; if(ch=='\t')ch=' '; if((unsigned char)ch<32)ch='.'; if(cx2<80)lb[cx2++]=ch; }
        if(cx2>0&&ty<ybot-14){ lb[cx2]=0; dstr(px0,ty,lb,rgb(60,66,84),1); }
        if(f->len==0)dstr(px0,ty,"(empty file)",rgb(150,158,176),1); }
}
static void fx_draw(int i){
    fx_init(); if(fx_dir<0||!DT[fx_dir].used)fx_go(0);
    if(g_fsel>=0&&(!FS[g_fsel].used||FS[g_fsel].dir!=fx_dir))g_fsel=-1; if(fx_seldir>=0&&(!DT[fx_seldir].used||DT[fx_seldir].parent!=fx_dir))fx_seldir=-1;
    win_frame(i,"File Explorer");
    int wx=WN[i].x, wy=WN[i].y+TITLEH, ww=WN[i].w, wh=WN[i].h-TITLEH; fx_bn=0; fx_hn2=0;
    fill_rect(wx+1,wy,ww-2,wh-1,rgb(255,255,255)); fill_rect(wx+1,wy,ww-2,76,rgb(247,248,250));
    /* row 1: navigation + path + filter */
    fx_toolbtn2(wx+10,wy+8,30,28,0,1,1,0,fx_mode||fx_hp>0); fx_toolbtn2(wx+42,wy+8,30,28,0,2,2,0,!fx_mode&&fx_hp<fx_hn-1); fx_toolbtn2(wx+74,wy+8,30,28,0,3,3,0,fx_mode||fx_dir>0);
    int sw=ww>=700?200:150, px=wx+112, pwid=ww-(px-wx)-sw-18;
    fill_round(px,wy+8,pwid,28,7,15,rgb(255,255,255)); fill_rect(px+6,wy+35,pwid-12,1,rgb(222,226,234));
    if(fx_mode){ int ix=px+10; fill_round(ix,wy+14,18,12,2,15,rgb(70,120,190)); fill_rect(ix+2,wy+16,14,8,rgb(160,205,250)); fill_rect(ix+6,wy+27,6,2,rgb(110,118,134)); dstr(ix+26,wy+15,">",rgb(150,158,176),1); dstr(ix+40,wy+15,"This PC",INK,1); }
    else { int chain[16],cn=0,c=fx_dir; while(c>=0&&cn<16){ chain[cn++]=c; c=DT[c].parent; }
        int x=px+10, maxx=px+pwid-10, first=cn-1;
        { int tot=0; for(int k=cn-1;k>=0;k--)tot+=(k==cn-1?2:slen(DT[chain[k]].name))*8+22; while(first>0&&tot>pwid-40){ tot-=slen(DT[chain[first-1]].name)*8+22; first--; } }
        if(first<cn-1){ dstr(x,wy+15,"..",rgb(150,158,176),1); x+=24; }
        for(int k=(first<cn-1?first:cn-1);k>=0&&x<maxx;k--){ const char* nm=(chain[k]==0)?"/":DT[chain[k]].name; int w=slen(nm)*8+10;
            int hov=in_rect(mx,my,x-4,wy+10,w,24); if(hov)fill_round(x-4,wy+10,w,24,5,15,rgb(232,237,246));
            dstr(x+1,wy+15,nm,k==0?INK:rgb(90,100,120),1); fx_btn(x-4,wy+10,w,24,40,chain[k]); x+=w; if(k>0&&chain[k]!=0){ dstr(x,wy+15,">",rgb(180,186,200),1); x+=14; } } }
    { int sx=wx+ww-sw-8; fill_round(sx,wy+8,sw,28,7,15,rgb(255,255,255)); fill_round(sx+1,wy+9,sw-2,26,6,15,focus==W_FILES?rgb(252,253,255):rgb(255,255,255));
        fill_circle(sx+15,wy+20,5,rgb(150,158,176)); fill_circle(sx+15,wy+20,3,rgb(255,255,255)); fill_rect(sx+18,wy+24,4,2,rgb(150,158,176));
        if(fx_flen){ char fb[34]; fx_fit(fb,fx_filter,(sw-54)/8); dstr(sx+28,wy+15,fb,INK,1); fill_rect(sx+28+slen(fb)*8+1,wy+14,2,15,((g_ticks/50)&1)?ACC:rgb(255,255,255));
            fill_circle(sx+sw-14,wy+22,8,in_rect(mx,my,sx+sw-24,wy+12,20,20)?rgb(210,214,224):rgb(228,231,238)); dstr(sx+sw-18,wy+15,"x",rgb(90,100,120),1); fx_btn(sx+sw-24,wy+12,20,20,16,0); }
        else dstr(sx+28,wy+15,fx_mode?"Search This PC":"Type to filter",rgb(170,176,190),1); }
    /* row 2: actions */
    if(fx_mode)fx_tpc_cmdbar(wx,wy,ww); else
    { int y=wy+42, x=wx+10; int have=(g_fsel>=0)||(fx_seldir>0);
        int wide=ww>=880;
        if(fx_in_trash(fx_dir)){ fx_toolbtn2(x,y,wide?82:34,28,wide?"Restore":0,60,6,0,have&&(g_fsel<0||FS[g_fsel].dir==fx_trash_id())); x+=wide?86:38; fx_toolbtn2(x,y,wide?110:34,28,wide?"Empty Trash":0,61,7,0,1); x+=wide?118:44; }
        else { fx_toolbtn2(x,y,wide?82:34,28,wide?"Folder":0,4,4,0,1); x+=wide?86:38; fx_toolbtn2(x,y,wide?66:34,28,wide?"File":0,5,5,0,1); x+=wide?74:44; }
        fill_rect(x-5,y+6,1,16,rgb(220,224,232));
        fx_toolbtn2(x,y,wide?82:34,28,wide?"Rename":0,6,6,0,have); x+=wide?86:38; fx_toolbtn2(x,y,wide?82:34,28,wide?"Delete":0,7,7,0,have); x+=wide?90:44;
        fill_rect(x-5,y+6,1,16,rgb(220,224,232));
        fx_toolbtn2(x,y,wide?66:34,28,wide?"Copy":0,8,8,0,g_fsel>=0); x+=wide?70:38; fx_toolbtn2(x,y,wide?58:34,28,wide?"Cut":0,9,9,0,g_fsel>=0); x+=wide?62:38; fx_toolbtn2(x,y,wide?70:34,28,wide?"Paste":0,10,10,0,fx_clip>=0&&FS[fx_clip].used);
        int rx=wx+ww-10; if(ww>=700){ rx-=34; fx_toolbtn2(rx,y,34,28,0,13,12,fx_prev,1); }
        rx-=34; fx_toolbtn2(rx,y,34,28,0,11,40001,fx_view==1,1); fx_bid[fx_bn-1]=11; fx_bval[fx_bn-1]=1;
        rx-=36; fx_toolbtn2(rx,y,34,28,0,12,40000,fx_view==0,1); fx_bid[fx_bn-1]=11; fx_bval[fx_bn-1]=0; }
    fill_rect(wx+1,wy+76,ww-2,1,rgb(226,229,236));
    /* sidebar */
    int sbw=ww>=620?164:128, top=wy+77, bot=wy+wh-26;
    fill_rect(wx+1,top,sbw,bot-top,rgb(245,246,249)); fill_rect(wx+sbw+1,top,1,bot-top,rgb(228,230,236));
    { static const char* PN[8]={"Home","Downloads","Programs","Config","Temporary","Devices","Root","Trash"}; static const char* PP[8]={"/home","/downloads","/bin","/etc","/tmp","/dev","/","/.trash"};
        int y=top+10; dstr(wx+14,y,"PLACES",rgb(150,158,176),1); y+=20;
        for(int k=0;k<8;k++){ int d=dt_resolve_dir(PP[k]); if(d<0||y+26>bot)continue; int on=(!fx_mode&&fx_dir==d);
            if(on){ fill_round(wx+6,y,sbw-10,26,7,15,mix(ACC,rgb(255,255,255),210)); fill_round(wx+6,y+6,3,14,1,15,ACC); } else if(in_rect(mx,my,wx+6,y,sbw-10,26))fill_round(wx+6,y,sbw-10,26,7,15,rgb(233,236,242));
            fx_icon(wx+16,y+5,16,0); dstr(wx+40,y+6,PN[k],on?INK:rgb(72,80,98),1); fx_btn(wx+6,y,sbw-10,26,20,d); y+=28; }
        y+=8; if(y+48<bot){ dstr(wx+14,y,"DEVICES",rgb(150,158,176),1); y+=20; }
        if(y+26<=bot){ int on=fx_mode; if(on)fill_round(wx+6,y,sbw-10,26,6,15,mix(ACC,rgb(255,255,255),204)); else if(in_rect(mx,my,wx+6,y,sbw-10,26))fill_round(wx+6,y,sbw-10,26,6,15,rgb(233,236,242));
            fill_round(wx+14,y+8,18,12,3,15,rgb(120,130,150)); fill_rect(wx+17,y+11,12,3,rgb(200,206,218)); dstr(wx+38,y+6,"This PC",on?INK:rgb(72,80,98),1); fx_btn(wx+6,y,sbw-10,26,20,-2); y+=28; }
        vol_scan(0);
        for(int vi=0;vi<g_vol_n&&y+26<=bot-4;vi++){ nvol* v=&g_vol[vi]; int on=fx_mode&&tpc_sel==vi; if(on)fill_round(wx+6,y,sbw-10,24,6,15,rgb(229,243,255)); else if(in_rect(mx,my,wx+6,y,sbw-10,24))fill_round(wx+6,y,sbw-10,24,6,15,rgb(233,236,242));
            tpc_icon(wx+22,y+4,v->kind,0); char nm[48]; vol_name(v,nm,48); char nf[24]; fx_fit(nf,nm,(sbw-50)/8); dstr(wx+44,y+5,nf,rgb(72,80,98),1); fx_btn(wx+6,y,sbw-10,24,76,vi); y+=26; }
        if(fx_clip>=0&&FS[fx_clip].used&&y<bot-44){ y=bot-40; fill_round(wx+8,y,sbw-14,32,6,15,rgb(236,240,248)); dstr(wx+14,y+2,fx_clipcut?"Cut:":"Copied:",rgb(120,128,146),1);
            char cb[24]; fx_fit(cb,FS[fx_clip].name,(sbw-24)/8); dstr(wx+14,y+16,cb,INK,1); } }
    /* main area (+ preview) */
    int pvw=(fx_prev&&!fx_mode&&ww>=700)?(ww>=900?280:230):0;
    fx_mainx=wx+sbw+2; fx_mainy=top; fx_mainw=ww-sbw-3-pvw; fx_mainh=bot-top;
    if(fx_mode){ fx_draw_thispc(fx_mainx,fx_mainy,fx_mainw+pvw,fx_mainh); }
    else {
        fx_build();
        int x0=fx_mainx, y0=fx_mainy, mw=fx_mainw;
        if(fx_view==0){
            int cType=x0+mw*48/100, cSize=x0+mw*72/100, cPerm=x0+mw*84/100; int showPerm=(mw>=520); if(!showPerm){ cType=x0+mw*56/100; cSize=x0+mw*80/100; }
            fill_rect(x0,y0,mw,28,rgb(250,251,253)); fill_rect(x0,y0+27,mw,1,rgb(232,234,240));
            { const char* hn[3]={"Name","Size","Type"}; int hxs[3]={x0+42,cSize,cType}; int hid[3]={13,14,15};
              for(int h=0;h<3;h++){ int w=(h==0)?(cType-x0-44):(h==1?(showPerm?cPerm-cSize:x0+mw-cSize):(cSize-cType)); int hov=in_rect(mx,my,hxs[h]-6,y0,w,27); if(hov)fill_rect(hxs[h]-6,y0+2,w,24,rgb(238,241,247));
                  dstr(hxs[h],y0+7,hn[h],fx_sort==(hid[h]-13)?INK:rgb(120,128,146),1); if(fx_sort==(hid[h]-13)){ int ax=hxs[h]+slen(hn[h])*8+6; dstr(ax,y0+7,fx_sdesc?"v":"^",ACC,1); }
                  fx_btn(hxs[h]-6,y0,w,27,hid[h],0); }
              if(showPerm)dstr(cPerm,y0+7,"Permissions",rgb(120,128,146),1); }
            int rh=28, vis=(fx_mainh-32)/rh; if(vis<1)vis=1; int maxs=fx_rn-vis; if(maxs<0)maxs=0; if(fx_scroll>maxs)fx_scroll=maxs; if(fx_scroll<0)fx_scroll=0;
            { int sr=fx_selrow(); if(sr>=0){ if(sr<fx_scroll)fx_scroll=sr; if(sr>=fx_scroll+vis)fx_scroll=sr-vis+1; } }
            int y=y0+32; { extern void gfx_scroll_area(int,int,int,int,int,int); gfx_scroll_area(W_FILES,x0,y0+32,mw,vis*rh,fx_scroll*rh); }
            for(int r=fx_scroll;r<fx_rn&&r<fx_scroll+vis;r++){ int isd=(fx_rk[r]==0), id=fx_rid[r];
                int sel=isd?(id==fx_seldir):(id==g_fsel); int cut=(!isd&&fx_clipcut&&id==fx_clip);
                if(sel)fill_round(x0+6,y,mw-12,rh-2,6,15,mix(ACC,rgb(255,255,255),200)); else if(in_rect(mx,my,x0+6,y,mw-12,rh-2))fill_round(x0+6,y,mw-12,rh-2,6,15,rgb(240,243,249));
                const char* nm=isd?DT[id].name:FS[id].name; char nb[64]; fx_fit(nb,nm,(cType-x0-56)/8);
                fx_icon(x0+14,y+4,20,isd?0:fx_ikind(id)); dstr(x0+42,y+7,nb,cut?rgb(150,158,176):INK,1);
                if(isd){ dstr(cType,y+7,"Folder",rgb(120,128,146),1); int n=0; for(int q=0;q<DN_MAX;q++)if(DT[q].used&&q!=id&&DT[q].parent==id)n++; for(int q=0;q<FS_N;q++)if(FS[q].used&&FS[q].dir==id)n++;
                    char s[20],*p=s; p=udec(p,(u32)n); p=scopy(p,n==1?" item":" items"); *p=0; dstr(cSize,y+7,s,rgb(150,158,176),1); }
                else { char tb[24]; fx_fit(tb,fx_kname(fx_ikind(id),id),(cSize-cType-10)/8); dstr(cType,y+7,tb,rgb(120,128,146),1);
                    char s[20],*p=s; p=fmt_size(p,FS[id].len); *p=0; dstr(cSize,y+7,s,rgb(120,128,146),1); }
                if(showPerm){ char ms[12]; mode_str(isd?(DT[id].mode?DT[id].mode:0040755u):(FS[id].mode?FS[id].mode:0100644u),ms); dstr(cPerm,y+7,ms,rgb(150,158,176),1); }
                if(fx_hn2<FX_MAXROWS){ fx_hx[fx_hn2]=x0+6; fx_hy[fx_hn2]=y; fx_hw[fx_hn2]=mw-12; fx_hh[fx_hn2]=rh-2; fx_hrow[fx_hn2]=r; fx_hn2++; }
                y+=rh; }
            if(fx_rn>vis){ int th=fx_mainh-36, bh=th*vis/fx_rn; if(bh<20)bh=20; int by=y0+32+(th-bh)*fx_scroll/(maxs?maxs:1); fill_round(x0+mw-7,by,4,bh,2,15,rgb(200,205,216)); }
        } else {
            int tw=104, th=96, cols=(mw-16)/tw; if(cols<1)cols=1; fx_cols=cols; int rows=(fx_mainh-12)/th; if(rows<1)rows=1;
            int total=(fx_rn+cols-1)/cols, maxs=total-rows; if(maxs<0)maxs=0; if(fx_scroll>maxs)fx_scroll=maxs; if(fx_scroll<0)fx_scroll=0;
            { int sr=fx_selrow(); if(sr>=0){ int rr=sr/cols; if(rr<fx_scroll)fx_scroll=rr; if(rr>=fx_scroll+rows)fx_scroll=rr-rows+1; } }
            for(int r=fx_scroll*cols;r<fx_rn&&r<(fx_scroll+rows)*cols;r++){ int idx=r-fx_scroll*cols; int tx=x0+10+(idx%cols)*tw, ty=y0+10+(idx/cols)*th;
                int isd=(fx_rk[r]==0), id=fx_rid[r]; int sel=isd?(id==fx_seldir):(id==g_fsel);
                if(sel)fill_round(tx,ty,tw-6,th-6,8,15,mix(ACC,rgb(255,255,255),200)); else if(in_rect(mx,my,tx,ty,tw-6,th-6))fill_round(tx,ty,tw-6,th-6,8,15,rgb(240,243,249));
                fx_icon(tx+(tw-6)/2-22,ty+8,44,isd?0:fx_ikind(id));
                if(!isd){ const char* e=fx_ext(FS[id].name); int el=slen(e); if(el>0&&el<=4){ char eb[6]; int q=0; for(;q<el;q++){ char c=e[q]; if(c>='a'&&c<='z')c-=32; eb[q]=c; } eb[q]=0;
                    int bw=el*8+8, bx=tx+(tw-6)/2-bw/2, byy=ty+38; fill_round(bx,byy,bw,15,4,15,rgb(255,255,255)); fill_round(bx+1,byy+1,bw-2,13,3,15,rgb(58,66,86)); dstr(bx+4,byy,eb,rgb(255,255,255),1); } }
                char nb[16]; fx_fit(nb,isd?DT[id].name:FS[id].name,11); dstr_ctr(tx+(tw-6)/2,ty+62,nb,INK,1);
                if(fx_hn2<FX_MAXROWS){ fx_hx[fx_hn2]=tx; fx_hy[fx_hn2]=ty; fx_hw[fx_hn2]=tw-6; fx_hh[fx_hn2]=th-6; fx_hrow[fx_hn2]=r; fx_hn2++; } } }
        if(fx_rn==0){ if(fx_flen)dstr_ctr(x0+mw/2,y0+fx_mainh/2-10,"No matches",rgb(150,158,176),1);
            else { fx_icon(x0+mw/2-24,y0+fx_mainh/2-60,48,0); dstr_ctr(x0+mw/2,y0+fx_mainh/2,"This folder is empty",rgb(150,158,176),1); dstr_ctr(x0+mw/2,y0+fx_mainh/2+20,"Use + Folder or + File to add something",rgb(170,176,190),1); } }
        if(pvw){ int pvx=wx+ww-pvw-1; fill_rect(pvx,top,1,bot-top,rgb(228,230,236)); fill_rect(pvx+1,top,pvw-1,bot-top,rgb(251,252,254)); fx_preview(pvx+16,top+14,pvw-32,bot-8); }
        else { fx_pvw=0; fpv_edw=0; ze_n=0; }
    }
    /* status bar */
    fill_rect(wx+1,bot,ww-2,1,rgb(228,230,236)); fill_rect(wx+1,bot+1,ww-2,wh-(bot-wy)-2,rgb(247,248,250));
    { char s[128],*p=s;
        if(fx_mode){ p=udec(p,(u32)g_vol_n); p=scopy(p,g_vol_n==1?" item":" items"); if(tpc_sel>=0&&tpc_sel<g_vol_n)p=scopy(p,"     1 item selected"); }
        else { int nd=0,nf=0; long tot=0; for(int r=0;r<fx_rn;r++){ if(fx_rk[r]==0)nd++; else { nf++; tot+=FS[fx_rid[r]].len; } }
            p=udec(p,(u32)nd); p=scopy(p,nd==1?" folder, ":" folders, "); p=udec(p,(u32)nf); p=scopy(p,nf==1?" file (":" files ("); p=fmt_size(p,tot); *p++=')';
            if(g_fsel>=0&&FS[g_fsel].used){ p=scopy(p,"     Selected: "); char nb[28]; fx_fit(nb,FS[g_fsel].name,26); p=scopy(p,nb); p=scopy(p,"  "); p=fmt_size(p,FS[g_fsel].len); }
            else if(fx_seldir>=0){ p=scopy(p,"     Selected: "); char nb[28]; fx_fit(nb,DT[fx_seldir].name,26); p=scopy(p,nb); } }
        *p=0; dstr(wx+12,bot+6,s,rgb(110,118,136),1);
        char r[40],*q=r; q=scopy(q,disk_ok?"Saved to disk":"RAM only - not saved"); *q=0; dstr(wx+ww-slen(r)*8-14,bot+6,r,disk_ok?rgb(80,150,100):rgb(200,130,60),1); }
    /* drag ghost */
    if(g_fdrag&&g_fdrag_slot>=0&&FS[g_fdrag_slot].used){ int dd=(mx-g_fdrag_x)*(mx-g_fdrag_x)+(my-g_fdrag_y)*(my-g_fdrag_y);
        if(dd>=36){ int gw=slen(FS[g_fdrag_slot].name)*8+40; fill_round(mx+10,my+10,gw,26,7,15,mix(ACC,rgb(255,255,255),40)); fx_icon(mx+16,my+14,18,fx_ikind(g_fdrag_slot)); dstr(mx+40,my+16,FS[g_fdrag_slot].name,rgb(255,255,255),1); } }
    if(fx_mode&&tpc_props>=0&&tpc_props<g_vol_n){ fx_bn=0; fx_hn2=0; tpc_dialog(wx,wy,ww,wh); }
    /* recursive-delete confirmation */
    if(fx_confirm&&fx_confirm_dir>0&&DT[fx_confirm_dir].used){
        for(int y=wy;y<wy+wh-1;y++)for(int x=wx+1;x<wx+ww-1;x++){ u32*pp=&SCENE[(u64)y*SW+x]; *pp=mix(*pp,rgb(20,24,34),90); }
        int dw=380, dh=150, dx=wx+(ww-dw)/2, dy=wy+(wh-dh)/2; shadow_soft(dx,dy,dw,dh,12); fill_round(dx,dy,dw,dh,12,15,rgb(255,255,255));
        dstr(dx+22,dy+20,"Delete folder?",INK,2);
        char b[80],*p=b; p=scopy(p,"\""); char nb[30]; fx_fit(nb,DT[fx_confirm_dir].name,28); p=scopy(p,nb); p=scopy(p,"\" and everything in it"); *p=0; dstr(dx+22,dy+56,b,rgb(90,100,120),1);
        dstr(dx+22,dy+74,"will be permanently deleted.",rgb(90,100,120),1);
        int by=dy+dh-44; fill_round(dx+dw-212,by,94,30,7,15,in_rect(mx,my,dx+dw-212,by,94,30)?rgb(228,231,238):rgb(240,242,246)); dstr_ctr(dx+dw-165,by+8,"Cancel",INK,1);
        fill_round(dx+dw-110,by,90,30,7,15,in_rect(mx,my,dx+dw-110,by,90,30)?rgb(200,60,60):rgb(220,72,72)); dstr_ctr(dx+dw-65,by+8,"Delete",rgb(255,255,255),1);
        fx_bn=0; fx_btn(dx+dw-212,by,94,30,90,0); fx_btn(dx+dw-110,by,90,30,91,0); fx_hn2=0; }
}
static int fx_rowat(int px,int py){ for(int h=0;h<fx_hn2;h++) if(in_rect(px,py,fx_hx[h],fx_hy[h],fx_hw[h],fx_hh[h]))return fx_hrow[h]; return -1; }
static void fx_confirm_do(int yes){ if(yes&&fx_confirm_dir>0){ int n=fx_rmtree(fx_confirm_dir); dt_save(); char b[40],*p=b; p=scopy(p,"Deleted "); p=udec(p,(u32)n); p=scopy(p," items"); *p=0; toast(b); fx_seldir=-1; if(!DT[fx_dir].used)fx_go(0); } fx_confirm=0; fx_confirm_dir=-1; }
static void fx_click(int px,int py){
    for(int b=0;b<fx_bn;b++) if(in_rect(px,py,fx_bx[b],fx_by[b],fx_bw[b],fx_bh[b])){
        int id=fx_bid[b]; if(id==90){ fx_confirm_do(0); return; } if(id==91){ fx_confirm_do(1); return; }
        if(id==40&&fx_mode){ fx_mode=0; fx_go(0); return; } fx_do(id,fx_bval[b]); return; }
    if(fx_confirm)return;
    if(!fx_mode&&fx_pvw>0){
        if(g_fsel>=0&&FS[g_fsel].used&&fpv_edw>0&&in_rect(px,py,fpv_edx,fpv_edy,fpv_edw,fpv_edh)){ fx_open_file(g_fsel); return; }
        if(g_fsel>=0&&FS[g_fsel].used&&fkind(g_fsel)==4) for(int e=0;e<ze_n;e++) if(px>=fx_pvx0-4&&px<fx_pvx0+fx_pvw&&py>=ze_y[e]-3&&py<ze_y[e]+19){ zsel=e;
            const char* nm=zent[e].name; int q=slen(nm); if(q>0&&(nm[q-1]=='/'||nm[q-1]=='\\'))return;
            int L=zip_extract(g_fsel,e,(u8*)jpeg_scratch,(int)sizeof(jpeg_scratch)); if(L<0)return;
            const char* base=nm; for(int r=0;nm[r];r++) if(nm[r]=='/'||nm[r]=='\\')base=nm+r+1;
            int k2=file_kind(base); if(k2==0||k2==2){ editor_open_buf((const char*)jpeg_scratch,L); }
            else { int slot=fs_save_bin(base,(u8*)jpeg_scratch,L,1); if(slot>=0){ FS[slot].dir=fx_dir; fx_persist(slot); if(k2==1)g_imgview=slot; else g_fsel=slot; } }
            return; } }
    if(fx_mode){ if(tpc_props<0&&px>=fx_mainx&&py>=fx_mainy&&py<fx_mainy+fx_mainh)tpc_sel=-1; return; }
    int r=fx_rowat(px,py);
    if(r>=0){ int key=fx_rk[r]?(1000+fx_rid[r]):fx_rid[r];
        if(key==fx_lastid&&(g_ticks-fx_lastt)<45){ fx_lastid=-99999; fx_open_row(r); return; }
        fx_lastid=key; fx_lastt=g_ticks; fx_select_row(r);
        if(fx_rk[r]==1){ g_fdrag=1; g_fdrag_slot=fx_rid[r]; g_fdrag_x=px; g_fdrag_y=py; }
        return; }
    if(in_rect(px,py,fx_mainx,fx_mainy,fx_mainw,fx_mainh)){ g_fsel=-1; fx_seldir=-1; }
}
int fx_drop(int px,int py,int slot){   /* drag-and-drop a file onto a folder row or a sidebar place */
    if(slot<0||!FS[slot].used||fx_mode)return 0; int target=-1;
    int r=fx_rowat(px,py); if(r>=0&&fx_rk[r]==0)target=fx_rid[r];
    if(target<0) for(int b=0;b<fx_bn;b++) if((fx_bid[b]==20||fx_bid[b]==40)&&fx_bval[b]>=0&&in_rect(px,py,fx_bx[b],fx_by[b],fx_bw[b],fx_bh[b])){ target=fx_bval[b]; break; }
    if(target<0)return in_rect(px,py,WN[W_FILES].x,WN[W_FILES].y,WN[W_FILES].w,WN[W_FILES].h);
    if(target==FS[slot].dir)return 1;
    if(fx_exists(target,FS[slot].name,slot,-1)){ toast("A file with that name already exists there"); return 1; }
    FS[slot].dir=target; fx_persist(slot); if(g_fsel==slot)g_fsel=-1;
    char b[64],*p=b; p=scopy(p,"Moved to "); p=scopy(p,target==0?"/":DT[target].name); *p=0; toast(b); return 1; }
int fx_rclick(int px,int py){   /* returns context-menu kind for a right-click inside Files */
    if(fx_mode||g_files_usb||g_files_tree||fx_confirm)return 0;
    int r=fx_rowat(px,py); if(r>=0){ fx_select_row(r); return fx_rk[r]?2:7; }
    if(in_rect(px,py,fx_mainx,fx_mainy,fx_mainw,fx_mainh)){ g_fsel=-1; fx_seldir=-1; return 8; } return 0; }
void files_key(int c){
    if(g_files_usb||g_files_tree)return;
    if(fx_confirm){ if(c=='\n')fx_confirm_do(1); else if(c==27)fx_confirm_do(0); return; }
    if(fx_mode){ if(c=='\b'||c==27)fx_mode=0; return; }
    fx_build(); int sr=fx_selrow(); int step=(fx_view==1)?fx_cols:1;
    if(c==20||(c==18&&fx_view==1)){ int d=(c==18)?1:step; if(fx_rn){ fx_select_row(sr<0?0:(sr+d<fx_rn?sr+d:fx_rn-1)); } return; }
    if(c==19||(c==17&&fx_view==1)){ int d=(c==17)?1:step; if(fx_rn){ fx_select_row(sr<0?0:(sr-d>=0?sr-d:0)); } return; }
    if(c=='\n'){ if(sr>=0)fx_open_row(sr); return; }
    if(c==27){ if(fx_flen){ fx_flen=0; fx_filter[0]=0; } else { g_fsel=-1; fx_seldir=-1; } return; }
    if(c=='\b'){ if(fx_flen){ fx_filter[--fx_flen]=0; } else fx_up(); return; }
    if(c==127){ fx_delete_sel(); return; }
    if(c>=32&&c<127&&fx_flen<30){ fx_filter[fx_flen++]=(char)c; fx_filter[fx_flen]=0; fx_scroll=0; g_fsel=-1; fx_seldir=-1; }
}
void files_draw(int i){
    if(!g_files_tree&&!g_files_usb){ fx_draw(i); return; }
    if(g_files_tree){                                    /* Windows-style folder tree view */
        win_frame(i,"File Explorer");
        int wx=WN[i].x, wy=WN[i].y+TITLEH, ww=WN[i].w, wh=WN[i].h-TITLEH;
        fill_rect(wx+1,wy+1,ww-2,34,rgb(247,248,250));
        int bh=(mx>=wx+10&&mx<wx+52&&my>=wy+7&&my<wy+30);
        fill_round(wx+10,wy+7,42,22,5,15,bh?mix(ACC,rgb(255,255,255),120):rgb(236,238,242)); dstr(wx+22,wy+11,"<",bh?rgb(255,255,255):rgb(72,80,98),2);
        /* breadcrumb: walk parents */
        { char bc[128]; int chain[12],cn=0,c=g_cwd; while(c>=0&&cn<12){ chain[cn++]=c; c=FTREE[c].parent; }
          char* p=bc; p=scopy(p,"This PC  >  "); for(int k=cn-1;k>=0;k--){ p=scopy(p,(char*)FTREE[chain[k]].name); if(k>0)p=scopy(p,"  >  "); } *p=0;
          dstr(wx+64,wy+11,bc,rgb(90,100,120),1); }
        fill_rect(wx+1,wy+35,ww-2,1,rgb(228,230,236)); fill_rect(wx+1,wy+36,ww-2,wh-38,rgb(255,255,255));
        int y=wy+52; ftrow_n=0;
        /* subfolders of g_cwd */
        for(int nnode=0;nnode<FTREE_N && y<wy+wh-26;nnode++){ if(FTREE[nnode].parent!=g_cwd)continue;
            int hov=(mx>=wx+8&&mx<wx+ww-8&&my>=y-2&&my<y+24);
            if(hov)fill_round(wx+8,y-2,ww-16,26,5,15,rgb(233,236,242));
            fill_round(wx+18,y+2,18,14,3,15,rgb(224,184,96)); fill_round(wx+18,y+2,18,4,3,15,rgb(236,206,130));
            dstr(wx+46,y+3,(char*)FTREE[nnode].name,INK,1);
            ftrow_y[ftrow_n]=y; ftrow_node[ftrow_n]=nnode; ftrow_isfile[ftrow_n]=0; ftrow_n++; y+=28; }
        /* curated real files in this folder */
        for(int fi=0;fi<FFILE_N && y<wy+wh-26 && ftrow_n<24;fi++) if(FFILE[fi].node==g_cwd){
            int hov=(mx>=wx+8&&mx<wx+ww-8&&my>=y-2&&my<y+24);
            if(hov)fill_round(wx+8,y-2,ww-16,26,5,15,rgb(236,241,250));
            mini_doc(wx+20,y+1); dstr(wx+46,y+3,(char*)FFILE[fi].name,INK,1);
            int L=0; while(FFILE[fi].text[L])L++; char sb[16],*p=sb; p=udec(p,(u32)L); p=scopy(p," B"); *p=0; dstr(wx+ww-90,y+3,sb,rgb(150,158,176),1);
            ftrow_y[ftrow_n]=y; ftrow_node[ftrow_n]=fi; ftrow_isfile[ftrow_n]=2; ftrow_n++; y+=28; }
        /* real files if this is Documents(5) or Downloads(6) */
        int showfolder=(g_cwd==5)?0:(g_cwd==6)?1:-1;
        if(showfolder>=0){ for(int k=0;k<FS_N && y<wy+wh-26 && ftrow_n<24;k++) if(FS[k].used && FS[k].folder==showfolder){
            int hov=(mx>=wx+8&&mx<wx+ww-8&&my>=y-2&&my<y+24);
            if(hov)fill_round(wx+8,y-2,ww-16,26,5,15,rgb(236,241,250));
            mini_doc(wx+20,y+1); dstr(wx+46,y+3,FS[k].name,INK,1);
            char sb[16],*p=sb; p=udec(p,(u32)FS[k].len); p=scopy(p," B"); *p=0; dstr(wx+ww-90,y+3,sb,rgb(150,158,176),1);
            ftrow_y[ftrow_n]=y; ftrow_node[ftrow_n]=k; ftrow_isfile[ftrow_n]=1; ftrow_n++; y+=28; } }
        if(ftrow_n==0) dstr(wx+24,wy+56,"(empty folder)",rgb(150,158,176),1);
        if(g_fdrag && g_fdrag_slot>=0 && FS[g_fdrag_slot].used){   /* drag ghost following the cursor */
            int gw=slen(FS[g_fdrag_slot].name)*8+34; fill_round(mx+8,my+8,gw,24,6,15,mix(ACC,rgb(255,255,255),40));
            mini_doc(mx+14,my+12); dstr(mx+34,my+14,FS[g_fdrag_slot].name,rgb(255,255,255),1); }
        return;
    }
    if(g_files_usb){                                     /* USB drive view */
        win_frame(i,"File Explorer  -  USB (E:)");
        int wx=WN[i].x, wy=WN[i].y+TITLEH, ww=WN[i].w, wh=WN[i].h-TITLEH;
        fill_rect(wx+1,wy+1,ww-2,34,rgb(247,248,250));
        int bh=(mx>=wx+10&&mx<wx+96&&my>=wy+7&&my<wy+30);
        fill_round(wx+10,wy+7,86,22,5,15,bh?mix(ACC,rgb(255,255,255),120):rgb(236,238,242)); dstr(wx+20,wy+11,"< This PC",bh?rgb(255,255,255):rgb(72,80,98),1);
        { char t[40],*p=t; p=scopy(p,"USB (E:)  -  "); p=udec(p,(u32)ufs_n); p=scopy(p," files"); *p=0; dstr(wx+110,wy+11,t,rgb(90,100,120),1); }
        fill_rect(wx+1,wy+35,ww-2,1,rgb(228,230,236)); fill_rect(wx+1,wy+36,ww-2,wh-38,rgb(255,255,255));
        if(!ufat_ok){ dstr(wx+20,wy+62,"No USB filesystem detected.",rgb(120,128,146),1); }
        else if(!ufs_n){ dstr(wx+20,wy+62,"(empty)",rgb(120,128,146),1); }
        else { int y=wy+50; for(int k=0;k<ufs_n && y<wy+wh-24;k++){
            int hov=(mx>=wx+8&&mx<wx+ww-8&&my>=y-2&&my<y+22);
            if(hov)fill_round(wx+8,y-2,ww-16,24,5,15,rgb(233,236,242));
            fill_round(wx+16,y+1,15,17,2,15,rgb(240,244,250)); fill_round(wx+16,y+1,15,4,2,15,rgb(224,184,96)); dstr(wx+21,y+7,".",rgb(160,168,182),1);
            dstr(wx+42,y+3,ufs[k].name,INK,1);
            char sb[24],*p=sb;p=udec(p,ufs[k].size);p=scopy(p," B");*p=0; dstr(wx+ww-110,y+3,sb,rgb(120,128,146),1);
            y+=26; } }
        return;
    }
    win_frame(i,"File Explorer");
    int wx=WN[i].x, wy=WN[i].y+TITLEH, ww=WN[i].w, wh=WN[i].h-TITLEH;
    int sbw=150, listw=232, lx0=wx+sbw, pvx=lx0+listw;
    const char*nav[5]={"Home","Desktop","Documents","Downloads","This PC"};
    fill_rect(wx+1,wy+1,ww-2,34,rgb(247,248,250));
    fill_round(wx+sbw+12,wy+7,listw-24,22,5,15,rgb(255,255,255));
    { char bc[48],*p=bc; p=scopy(p,"This PC  >  "); p=scopy(p,(char*)nav[g_filesnav]); *p=0; dstr(wx+sbw+22,wy+11,bc,rgb(90,100,120),1); }
    fill_rect(wx+1,wy+35,ww-2,1,rgb(228,230,236));
    fill_rect(wx+1,wy+36,sbw,wh-38,rgb(243,244,247)); fill_rect(wx+sbw,wy+36,1,wh-38,rgb(228,230,236));
    for(int k=0;k<5;k++){ int iy=wy+46+k*30; if(k==g_filesnav) fill_round(wx+6,iy,sbw-12,26,6,15,mix(ACC,rgb(255,255,255),204));
        else if(mx>=wx+6&&mx<wx+sbw-6&&my>=iy&&my<iy+26) fill_round(wx+6,iy,sbw-12,26,6,15,rgb(233,236,242));
        fill_round(wx+14,iy+8,13,9,2,15,k==g_filesnav?ACC:rgb(224,184,96)); dstr(wx+34,iy+7,nav[k],k==g_filesnav?INK:rgb(72,80,98),1); }
    if(g_filesnav==4){                                 /* This PC -> Devices with capacity bars */
        int mx0=wx+sbw+1, my0=wy+36, mw=ww-sbw-2, mh=wh-38; fill_rect(mx0,my0,mw,mh,rgb(255,255,255));
        dstr(mx0+24,my0+18,"Devices",rgb(90,100,120),1);
        long used=0; for(int k=0;k<FS_N;k++)if(FS[k].used)used+=FS[k].len; long total=fx_disk_total(); { u64 t4=0,f4=0; n4_usage(&t4,&f4); if(t4){ total=(long)t4; used=(long)(t4-f4); } } if(used>total)used=total;
        int tw=(mw-72)/2; if(tw>330)tw=330; if(tw<220){ tw=mw-48; } int th=82;
        int tx=mx0+24, ty=my0+46; g_drv_n=0; g_drv_w=tw;
        int h0=(mx>=tx&&mx<tx+tw&&my>=ty&&my<ty+th); draw_drive_tile(tx,ty,tw,th,"NoovexOS (C:)",used,total,0,h0);
        g_drv_x[g_drv_n]=tx; g_drv_y[g_drv_n]=ty; g_drv_tgt[g_drv_n]=98; g_drv_n++;   /* 98 = open folder tree */
        int cx2 = (tw>=mw-48)? tx : tx+tw+24, cy2=(tw>=mw-48)? ty+th+16 : ty;
        if(g_nvx_signedin){ int h1=(mx>=cx2&&mx<cx2+tw&&my>=cy2&&my<cy2+th); draw_drive_tile(cx2,cy2,tw,th,"Cloud (N:)",0,0,1,h1);
            g_drv_x[g_drv_n]=cx2; g_drv_y[g_drv_n]=cy2; g_drv_tgt[g_drv_n]=2; g_drv_n++; }
        else { dstr(cx2+4,cy2+th/2-4,"Sign in for cloud storage (nvxlogin)",rgb(150,158,176),1); }
        if(msd_ok&&ufat_ok){                            /* USB stick as a removable-drive tile */
            int ux, uy; if(tw>=mw-48){ ux=tx; uy=cy2+th+16; } else { ux=tx; uy=ty+th+16; }
            long uused=0; for(int k=0;k<ufs_n;k++)uused+=ufs[k].size; long utot=(long)(msd_lastlba+1)*512L; if(uused>utot)uused=utot;
            int hu=(mx>=ux&&mx<ux+tw&&my>=uy&&my<uy+th); draw_drive_tile(ux,uy,tw,th,"USB Drive (E:)",uused,utot,2,hu);
            g_drv_x[g_drv_n]=ux; g_drv_y[g_drv_n]=uy; g_drv_tgt[g_drv_n]=99; g_drv_n++;   /* tgt 99 = open USB view */
        }
        return;
    }
    fill_rect(pvx,wy+36,1,wh-38,rgb(228,230,236));
    int lxs=lx0+14, y=wy+50; fl_n=0; fl_x=lx0+4; fl_w=listw-8;
    for(int k=0;k<FS_N;k++) if(FS[k].used && files_show(FS[k].folder)){
        int sel=(k==g_fsel);
        if(sel) fill_round(fl_x,y-4,fl_w,26,5,15,mix(ACC,rgb(255,255,255),200));
        else if(mx>=fl_x&&mx<fl_x+fl_w&&my>=y-4&&my<y+22) fill_round(fl_x,y-4,fl_w,26,5,15,rgb(236,241,250));
        mini_doc(lxs,y-2); dstr(lxs+26,y,FS[k].name,INK,1);
        fl_y[fl_n]=y; fl_idx[fl_n]=k; fl_n++; y+=30; }
    if(fl_n==0) dstr(lx0+18,wy+52,"(empty)",rgb(150,158,176),1);
    int px0=pvx+18, py0=wy+50, pw=ww-(pvx-wx)-32;
    if(g_fsel>=0 && FS[g_fsel].used){
        FsFile*f=&FS[g_fsel]; int kind=fkind(g_fsel);
        fill_round(px0,py0,40,52,4,15,rgb(246,248,252)); for(int k=0;k<6;k++)fill_rect(px0+8,py0+10+k*6,24,2,rgb(196,204,220));
        dstr(px0+54,py0+6,f->name,INK,2);
        { char b[64],*p=b; const char*tn=(kind==1)?"Image":(kind==4)?"ZIP archive":(char*)file_type_name(f->name); p=scopy(p,(char*)tn); p=scopy(p,"  -  "); p=udec(p,(u32)f->len); scopy(p," bytes"); dstr(px0+54,py0+34,b,rgb(120,128,146),1); }
        const char* btn=(kind==1)?"Open Image":kind==2?"Open in Browser":kind==0?"Open in Editor":0;
        if(btn){ fpv_edx=px0+54; fpv_edy=py0+52; fpv_edw=118; fpv_edh=30; fill_round(fpv_edx,fpv_edy,fpv_edw,fpv_edh,7,15,ACC); dstr_ctr(fpv_edx+fpv_edw/2,fpv_edy+8,btn,rgb(255,255,255),1); }
        else fpv_edw=0;
        fill_rect(px0,py0+96,pw,1,rgb(232,234,240));
        int ph2=wy+wh-14-(py0+108);
        if(kind==1){ if(ensure_img(g_fsel)&&view_w>0) blit_img_fit(px0,py0+108,pw,ph2); else dstr(px0,py0+112,"(cannot decode image)",rgb(178,120,120),1); }
        else if(kind==4){ zip_open(g_fsel); ze_n=0;
            if(znent==0){ dstr(px0,py0+112,"Not a ZIP archive, or its directory is missing.",rgb(120,128,146),1); }
            else { char hb[40],*p=hb; p=scopy(p,"ZIP - "); p=udec(p,(u32)znent); scopy(p," entries  (click to open)"); dstr(px0,py0+108,hb,rgb(90,100,120),1);
                int ty=py0+130, maxy=wy+wh-14;
                for(int e=0;e<znent && ty<maxy;e++){ int sel=(e==zsel);
                    if(sel) fill_round(px0-4,ty-3,pw+4,22,4,15,mix(ACC,rgb(255,255,255),204));
                    else if(mx>=px0-4&&mx<px0+pw&&my>=ty-3&&my<ty+19) fill_round(px0-4,ty-3,pw+4,22,4,15,rgb(236,241,250));
                    const char*nm=zent[e].name; int isdir=0; { int q=0; while(nm[q])q++; if(q>0&&(nm[q-1]=='/'||nm[q-1]=='\\'))isdir=1; }
                    fill_round(px0,ty+2,10,12,2,15,isdir?rgb(224,184,96):rgb(150,180,230));
                    char en[44]; int en_n=0; for(int q=0;nm[q]&&en_n<43;q++)en[en_n++]=nm[q]; en[en_n]=0;
                    dstr(px0+16,ty,en,isdir?rgb(120,100,60):INK,1);
                    { char sb[16],*p=sb; p=udec(p,zent[e].usize); *p=0; dstr(px0+pw-slen(sb)*8-2,ty,sb,rgb(150,158,176),1); }
                    ze_y[e]=ty; ze_n++; ty+=22; } } }
        else if(kind==3){ dstr(px0,py0+112,"Binary file - preview and opening not supported.",rgb(120,128,146),1); }
        else { int ty=py0+108, col=pw/8; if(col>127)col=127; int maxy=wy+wh-14, cx2=0; char lb[128];
            for(int q=0; f->data[q] && ty<maxy; q++){ char ch=f->data[q];
                if(ch=='\n'||cx2>=col){ lb[cx2]=0; dstr(px0,ty,lb,rgb(60,66,84),1); ty+=16; cx2=0; if(ch=='\n')continue; }
                if(ch=='\r')continue; if(ch=='\t')ch=' '; if((unsigned char)ch<32)ch='.'; if(cx2<127)lb[cx2++]=ch; }
            if(cx2>0&&ty<maxy){ lb[cx2]=0; dstr(px0,ty,lb,rgb(60,66,84),1); } }
    } else dstr_ctr(pvx+(ww-(pvx-wx))/2, wy+wh/2-10, "Select a file to preview", rgb(150,158,176),1);
}
void files_click(int px,int py){
    if(!g_files_tree&&!g_files_usb){ fx_click(px,py); return; }
    if(g_files_tree){
        int wx=WN[W_FILES].x, wy=WN[W_FILES].y+TITLEH, ww=WN[W_FILES].w;
        if(in_rect(px,py,wx+10,wy+7,42,22)){ if(g_cwd==0){ g_files_tree=0; g_filesnav=4; } else { int p=FTREE[g_cwd].parent; g_cwd=(p<0)?0:p; } return; }   /* back */
        for(int r=0;r<ftrow_n;r++) if(px>=wx+8&&px<wx+ww-8&&py>=ftrow_y[r]-2&&py<ftrow_y[r]+24){
            if(ftrow_isfile[r]==2){ int fi=ftrow_node[r]; int L=0; while(FFILE[fi].text[L])L++; editor_open_buf(FFILE[fi].text,L); }   /* curated file */
            else if(ftrow_isfile[r]==1){ int k=ftrow_node[r]; g_fsel=k; g_fdrag=1; g_fdrag_slot=k; g_fdrag_x=px; g_fdrag_y=py; }   /* NVXFS file: begin drag (open on release if not moved) */
            else g_cwd=ftrow_node[r];                                                                        /* enter subfolder */
            return; }
        return;
    }
    if(g_files_usb){
        int wx=WN[W_FILES].x, wy=WN[W_FILES].y+TITLEH, ww=WN[W_FILES].w, wh=WN[W_FILES].h-TITLEH;
        if(px>=wx+10&&px<wx+96&&py>=wy+7&&py<wy+30){ g_files_usb=0; return; }   /* back to This PC */
        if(ufat_ok){ int y=wy+50; for(int k=0;k<ufs_n && y<wy+wh-24;k++){
            if(px>=wx+8&&px<wx+ww-8&&py>=y-2&&py<y+22){                        /* open a file in the editor */
                extern void ed_load_usb(int fi); ed_load_usb(k); return; }
            y+=26; } }
        return;
    }
    int wx=WN[W_FILES].x, wy=WN[W_FILES].y+TITLEH, ww=WN[W_FILES].w, sbw=150;
    int pvx=wx+sbw+232, px0=pvx+18, pw=ww-(pvx-wx)-32;
    for(int k=0;k<5;k++){ int iy=wy+46+k*30; if(in_rect(px,py,wx+6,iy,sbw-12,26)){ if(g_filesnav!=k){ g_filesnav=k; g_fsel=-1; } return; } }
    if(g_filesnav==4){ for(int k=0;k<g_drv_n;k++) if(in_rect(px,py,g_drv_x[k],g_drv_y[k],g_drv_w,82)){ if(g_drv_tgt[k]==99){ g_files_usb=1; } else if(g_drv_tgt[k]==98){ g_files_tree=1; g_cwd=0; } else { g_filesnav=g_drv_tgt[k]; g_fsel=-1; } return; } return; }
    if(g_fsel>=0 && FS[g_fsel].used && fkind(g_fsel)==4){
        for(int e=0;e<ze_n;e++) if(px>=px0-4 && px<px0+pw && py>=ze_y[e]-3 && py<ze_y[e]+19){ zsel=e;
            const char*nm=zent[e].name; int q=0; while(nm[q])q++; if(q>0&&(nm[q-1]=='/'||nm[q-1]=='\\')) return;   /* directory */
            int L=zip_extract(g_fsel,e,(u8*)jpeg_scratch,(int)sizeof(jpeg_scratch));
            if(L<0) return;
            const char*base=nm; for(int r=0;nm[r];r++) if(nm[r]=='/'||nm[r]=='\\') base=nm+r+1;
            int k2=file_kind(base);
            if(k2==0||k2==2){ editor_open_buf((const char*)jpeg_scratch, L); }
            else { int slot=fs_save_bin(base,(u8*)jpeg_scratch,L,1); if(slot>=0){ if(k2==1) g_imgview=slot; else { g_fsel=slot; g_filesnav=3; } } }
            return; } }
    if(g_fsel>=0 && FS[g_fsel].used && fpv_edw>0 && in_rect(px,py,fpv_edx,fpv_edy,fpv_edw,fpv_edh)){
        { int kd=fkind(g_fsel); if(fext_eq(FS[g_fsel].name,"pptx")){ pptx_load(g_fsel); open_win(W_PPTX); } else if(video_is_media_name(FS[g_fsel].name)) video_open_slot(g_fsel); else if(kd==1)g_imgview=g_fsel; else if(kd==2)browser_show_html(fsd(g_fsel),FS[g_fsel].len,FS[g_fsel].name); else { editor_open(g_fsel); if(fext_eq(FS[g_fsel].name,"nvx"))editor_run(); } } return; }
    for(int k=0;k<fl_n;k++) if(px>=fl_x&&px<fl_x+fl_w&&py>=fl_y[k]-4&&py<fl_y[k]+22){ g_fsel=fl_idx[k]; return; }
}
void about_draw(int i){
    win_frame(i,"About NoovexOS");
    int x=WN[i].x+28,y=WN[i].y+TITLEH+24;
    draw_logo(WN[i].x+70, y, 60, ACC, 256);
    dstr(x+90,y+6,"NoovexOS 64",INK,3); dstr(x+90,y+46,"Version 9.82  -  from scratch",rgb(90,100,120),1);
    y+=96; dstr(x,y,"A hobby operating system in C and assembly.",INK,1); y+=22;
    dstr(x,y,"Native x86-64 long mode. No external kernel code.",INK,1); y+=22;
    char b[64],*p=b;p=scopy(p,"CPU: ");p=scopy(p,g_brand);*p=0; dstr(x,y,b,INK,1);
}
int gb_x,gb_y,gb_w,gb_h;
void welcome_draw(int i){
    win_frame(i,"NoovexOS 64  -  Welcome");
    int x=WN[i].x+26,y=WN[i].y+TITLEH+22;
    { const char* hd=(slen("Native x86-64 desktop  -  booted from scratch")*16+52<=WN[i].w)?"Native x86-64 desktop  -  booted from scratch":"Native x86-64 desktop"; dstr(x,y,hd,INK,2); } y+=40;
    char L[96],*p; p=L;p=scopy(p,"CPU:  ");p=scopy(p,g_brand);*p=0; dstr(x,y,L,INK,1); y+=22;
    p=L;p=scopy(p,"Display:  ");p=udec(p,W);*p++='x';p=udec(p,H);p=scopy(p,"  @ ");p=udec(p,BPP);p=scopy(p,"-bit");*p=0; dstr(x,y,L,INK,1); y+=22;
    p=L;p=scopy(p,"Long mode:  active   EFER.LMA=");*p++=(char)('0'+g_lma);*p=0; dstr(x,y,L,INK,1); y+=28;
    dstr(x,y,"Click N for the app launcher. Right-click for menus.",rgb(90,100,120),1); y+=18;
    dstr(x,y,"Drag windows to the edges to snap; Alt+Tab switches apps.",rgb(90,100,120),1); y+=26;
    gb_x=x;gb_y=WN[i].y+WN[i].h-54;gb_w=210;gb_h=38; fill_round(gb_x,gb_y,gb_w,gb_h,8,15,ACC);
    dstr_ctr(gb_x+gb_w/2,gb_y+6,"Open Terminal",rgb(255,255,255),2);
}
