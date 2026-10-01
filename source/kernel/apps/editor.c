/* =========================================================================
   Text editor (works on the RAM FS)
   ========================================================================= */
#define ED_ROWS 200
#define ED_COLS 200
static char ED[ED_ROWS][ED_COLS]; static int ed_ll[ED_ROWS];
static int ed_nl=1, ed_cx=0, ed_cy=0, ed_top=0, ed_file=-1, ed_dirty=0;
static int eb_save_x,eb_save_y,eb_new_x,eb_new_y,eb_bw=78,eb_bh=26;
static int eb_run_x,eb_run_y,eb_conx_x,eb_conx_y;
static char ed_con[6000]; static int ed_con_n=0, ed_show_con=0;
static char ed_src[ED_ROWS*40];
static void ed_run_cb(const char* s,int n){ for(int i=0;i<n && ed_con_n<5998;i++){ char c=s[i]; ed_con[ed_con_n++]=c; } ed_con[ed_con_n]=0; }
static void editor_run(void){
    int n=0; for(int r=0;r<ed_nl;r++){ for(int c=0;c<ed_ll[r] && n<(int)sizeof(ed_src)-2;c++)ed_src[n++]=ED[r][c]; if(n<(int)sizeof(ed_src)-2)ed_src[n++]='\n'; } ed_src[n]=0;
    ed_con_n=0; ed_con[0]=0; ed_show_con=1;
    js_run(ed_src,n,ed_run_cb);
    if(ed_con_n==0){ const char* m=js_err?"[program error]":"(no output)"; scopy(ed_con,m); ed_con_n=slen(ed_con); }
    else if(js_err && ed_con_n<5980){ scopy(ed_con+ed_con_n,"\n[error]"); ed_con_n+=8; }
}
static void editor_reset(void){ for(int r=0;r<ED_ROWS;r++){ED[r][0]=0;ed_ll[r]=0;} ed_nl=1;ed_cx=ed_cy=ed_top=0;ed_dirty=0; }
static void editor_open(int fsidx){
    editor_reset(); ed_file=fsidx;
    const char*d=FS[fsidx].data; int r=0,c=0;
    for(int k=0; d[k] && r<ED_ROWS; k++){ if(d[k]=='\n'){ ED[r][c]=0; ed_ll[r]=c; r++; c=0; }
        else if(c<ED_COLS-1){ ED[r][c++]=d[k]; } }
    ED[r][c]=0; ed_ll[r]=c; ed_nl=r+1;
    open_win(W_EDITOR);
}
static void editor_open_buf(const char*d,int len){
    editor_reset(); ed_file=-1;
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
static void editor_save(void){
    if(ed_file<0) return; char*o=FS[ed_file].data; int p=0;
    for(int r=0;r<ed_nl && p<FS_CAP-1;r++){ for(int c=0;c<ed_ll[r]&&p<FS_CAP-1;c++) o[p++]=ED[r][c]; if(r<ed_nl-1&&p<FS_CAP-1)o[p++]='\n'; }
    o[p]=0; FS[ed_file].len=p; ed_dirty=0; toast("Saved");
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
static char g_clip[1024]; static int g_clip_len=0;
static void editor_key(char c){
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
static void editor_draw(int i){
    const char*nm = ed_file>=0?FS[ed_file].name:"untitled";
    char title[48],*p=title; p=scopy(p,"Editor  -  "); p=scopy(p,nm); if(ed_dirty){p=scopy(p,"  *");} *p=0;
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

