    else if(r==1){ tcol(rgb(230,160,60)); term_puts("  no auth response\n"); tcol(WHT); }
    else if(r==2){ tcol(rgb(230,160,60)); term_puts("  auth rejected\n"); tcol(WHT); }
    else if(r==3){ tcol(rgb(230,160,60)); term_puts("  no assoc response\n"); tcol(WHT); }
    else if(r==4){ tcol(rgb(230,160,60)); term_puts("  assoc rejected (RSN mismatch?)\n"); tcol(WHT); }
    else if(r==5){ tcol(rgb(230,160,60)); term_puts("  no EAPOL msg1 (assoc ok, no handshake start)\n"); tcol(WHT); }
    else if(r==6){ tcol(rgb(230,160,60)); term_puts("  no EAPOL msg3 (msg2 sent - wrong password or MIC mismatch)\n"); tcol(WHT); }
    if(r==0){ tcol(g_gtk_state==1?GREEN:rgb(230,160,60)); term_puts("  "); term_puts(wpa_gtk_status()); term_putc('\n'); tcol(WHT); }
    else { tcol(rgb(230,160,60)); term_puts("  handshake failed\n"); tcol(WHT); }
}
void term_key(char c){
    if(g_pwprompt){
        if(c=='\n'){ term_putc('\n'); tline[tlen]=0; g_pwprompt=0; if(g_pwbyid){ wpa_connect_saved(rtw_find_mmio(),g_pwbyid-1,tline); g_pwbyid=0; } else wpa_do_join(g_pwssid,tline); tlen=0; term_prompt(); }
        else if(c=='\b'){ if(tlen>0){tlen--;term_putc('\b');} }
        else if(c>=32&&c<127){ if(tlen<126){tline[tlen++]=c;term_putc('*');} }   /* mask password */
        return;
    }
    if(c=='\n'){ term_putc('\n'); tline[tlen]=0; { char rl[300]; int k=0; { int st=0; while(tline[st]==' '||tline[st]=='\t')st++; for(;tline[st+k]&&k<299;k++)rl[k]=tline[st+k]; } rl[k]=0; static const char* const SEC[]={"wifi connect","wifi eap","wifi hidden","passwd","su ","sudo","useradd","chpasswd",0}; for(int q=0;SEC[q];q++){ int j=0; while(SEC[q][j]&&rl[j]==SEC[q][j])j++; if(!SEC[q][j]){ int c=0; for(int z=j;rl[z];z++){ if(rl[z]==' ')c++; if(c>=(q<3?2:1)){ rl[z]=0; break; } } break; } } klog2("cmd: ",rl); } { char lb[512]; int k=0; for(;tline[k]&&k<511;k++)lb[k]=tline[k]; lb[k]=0; sh_run_line(lb); } tlen=0; if(!g_tty_fg)term_prompt(); }
    else if(c=='\b'){ if(tlen>0){tlen--;term_putc('\b');} }
    else if(c>=32&&c<127){ if(tlen<254){tline[tlen++]=c;term_putc(c);} }
}
void term_init(void){ term_geom(); term_clear();
    tcol(ACC);term_puts("NoovexOS 64");tcol(WHT);term_puts("  nsh shell\n");
    tcol(GREY);term_puts("type 'help' for commands, 'neofetch' for info.\n\n");tcol(WHT); term_prompt(); }

const TTheme TTH[6]={
 {"Midnight",rgb(15,18,26),rgb(24,28,40),rgb(222,228,240),rgb(128,138,160),rgb(120,230,150),rgb(120,230,150),rgb(120,190,255),rgb(236,106,100),rgb(90,210,230),0},
 {"Dracula",rgb(40,42,54),rgb(33,34,44),rgb(248,248,242),rgb(150,156,190),rgb(255,121,198),rgb(80,250,123),rgb(189,147,249),rgb(255,85,85),rgb(139,233,253),0},
 {"Solarized",rgb(0,43,54),rgb(7,54,66),rgb(147,161,161),rgb(88,110,117),rgb(181,137,0),rgb(133,153,0),rgb(38,139,210),rgb(220,50,47),rgb(42,161,152),0},
 {"Paper",rgb(250,249,245),rgb(236,234,228),rgb(44,48,58),rgb(130,134,146),rgb(60,120,224),rgb(30,140,70),rgb(40,100,200),rgb(200,50,50),rgb(20,130,150),1},
 {"Matrix",rgb(4,10,6),rgb(10,20,12),rgb(140,255,160),rgb(60,140,80),rgb(140,255,160),rgb(90,255,120),rgb(120,230,170),rgb(255,120,100),rgb(120,255,210),0},
 {"Noovex",rgb(14,18,40),rgb(20,25,52),rgb(228,234,248),rgb(128,140,178),rgb(96,160,255),rgb(92,224,196),rgb(116,164,255),rgb(255,112,136),rgb(120,214,255),0}};
static u32 g_blurtmp[MAXW*MAXH];
static void term_glass(int x,int y,int w,int h,int r,u32 bg){
    if(g_topa>=100){ fill_round(x,y,w,h,r,15,bg); return; }
    { extern int g_gfx; extern void gfx_glass(int,int,int,int,int,u32,int,int); if(g_gfx){ int a=g_topa*256/100; if(a>256)a=256; gfx_glass(x,y,w,h,r,bg,a,g_tblur?2:0); return; } }
    int x0=x<0?0:x, y0=y<0?0:y, x1=x+w>(int)W?(int)W:x+w, y1=y+h>(int)H?(int)H:y+h; if(x1<=x0||y1<=y0)return;
    int bw=x1-x0, bh=y1-y0, R=g_tblur?10:0;
    if(R){ for(int j=0;j<bh;j++){ u32* src=&SCENE[(u64)(y0+j)*SW+x0]; u32* dst=&g_blurtmp[(u64)j*bw]; int sr=0,sg=0,sb=0,n=0;
            for(int i=-R;i<=R&&i<bw;i++){ if(i<0)continue; u32 c=src[i]; sr+=(c>>16)&255; sg+=(c>>8)&255; sb+=c&255; n++; }
            for(int i=0;i<bw;i++){ dst[i]=rgb(sr/n,sg/n,sb/n); int a=i+R+1, d=i-R; if(a<bw){ u32 c=src[a]; sr+=(c>>16)&255; sg+=(c>>8)&255; sb+=c&255; n++; } if(d>=0){ u32 c=src[d]; sr-=(c>>16)&255; sg-=(c>>8)&255; sb-=c&255; n--; } } }
        for(int i=0;i<bw;i++){ int sr=0,sg=0,sb=0,n=0;
            for(int j=0;j<=R&&j<bh;j++){ u32 c=g_blurtmp[(u64)j*bw+i]; sr+=(c>>16)&255; sg+=(c>>8)&255; sb+=c&255; n++; }
            for(int j=0;j<bh;j++){ u32 c0=rgb(sr/n,sg/n,sb/n); int a=j+R+1, d=j-R;
                if(a<bh){ u32 c=g_blurtmp[(u64)a*bw+i]; sr+=(c>>16)&255; sg+=(c>>8)&255; sb+=c&255; n++; }
                if(d>=0){ u32 c=g_blurtmp[(u64)d*bw+i]; sr-=(c>>16)&255; sg-=(c>>8)&255; sb-=c&255; n--; }
                if(!cut(x0-x+i,y0-y+j,w,h,r,15)) SCENE[(u64)(y0+j)*SW+x0+i]=c0; } } }
    int a=g_topa*256/100; if(a>256)a=256;
    for(int j=0;j<bh;j++){ u32* row=&SCENE[(u64)(y0+j)*SW+x0]; for(int i=0;i<bw;i++){ if(cut(x0-x+i,y0-y+j,w,h,r,15))continue; row[i]=mix(row[i],bg,a); } }
}
static u32 tmap(u32 c){ const TTheme* t=&TTH[g_tth];
    if(c==WHT)return t->fg; if(c==GREY)return t->dim; if(c==GREEN)return t->green; if(c==RED)return t->red; if(c==CYAN)return t->cyan; if(c==rgb(120,200,255))return t->blue;
    if(t->light){ int r=(c>>16)&255,g=(c>>8)&255,b=c&255; if((r*3+g*6+b)/10>150)return mix(c,rgb(0,0,0),120); } return c; }
static void tglyph(int x,int y,char ch,u32 c){ int idx=(u8)ch-32; if(idx<=0||idx>=95)return;
    if(!g_tfont){ dchar(x,y,ch,c,1); return; } { extern int g_gfx; extern void gfx_glyph(int,int,char,u32,int,int); if(g_gfx){ gfx_glyph(x,y,ch,c,1,2); return; } } const unsigned char* g=fontaa[idx];
    (void)SCENE; for(int r=0;r<14;r++){ int py=y+r; if((u32)py>=H)continue; for(int col=0;col<8;col++){ int a=g[r*8+col]; if(a<8)continue; int px=x+col; if((u32)px>=W)continue; u32* p=&g_scene[(u64)py*SW+px]; *p=mix(*p,c,a); } } }

void mono_glyph(int x,int y,char ch,u32 c){ int idx=(u8)ch-32; if(idx<=0||idx>=95)return;
    { extern int g_gfx; extern void gfx_glyph(int,int,char,u32,int,int); if(g_gfx){ gfx_glyph(x,y,ch,c,1,2); return; } } const unsigned char* g=fontaa[idx];
    for(int r=0;r<14;r++){ int py=y+r; if((u32)py>=H)continue; for(int col=0;col<8;col++){ int a=g[r*8+col]; if(a<8)continue; int px=x+col; if((u32)px>=W)continue; u32* p=&g_scene[(u64)py*SW+px]; *p=mix(*p,c,a); } } }
static int g_live=0;
static void live_geom(void){ tcols=((int)W-28)/8; if(tcols>TCMAX)tcols=TCMAX; trows=((int)H-28)/TLH; if(trows>TRMAX)trows=TRMAX; }

#define LIVE_BG rgb(10,12,18)
#define LIVE_OX 14
#define LIVE_OY 14
static char g_lsh[TRMAX][TCMAX]; static u32 g_lsc[TRMAX][TCMAX]; static u32 g_lsb[TRMAX][TCMAX];
static int g_lfull=1, g_lcx=-1, g_lcy=-1, g_lcon=0;
void live_invalidate(void){ g_lfull=1; }
static void live_row(int rr){
    fill_rect(0,LIVE_OY+rr*TLH,(int)W,TLH,LIVE_BG);
    for(int c=0;c<tcols;c++){ char ch=TCH[rr][c]; u32 bg=TBG[rr][c]; if(bg)fill_rect(LIVE_OX+c*8,LIVE_OY+rr*TLH,8,TLH,bg&0xFFFFFF);
        if(ch!=' ')tglyph(LIVE_OX+c*8,LIVE_OY+rr*TLH,ch,bg?TCO[rr][c]:tmap(TCO[rr][c])); g_lsh[rr][c]=ch; g_lsc[rr][c]=TCO[rr][c]; g_lsb[rr][c]=bg; }
}
static void live_cell(int cx,int cy,int on){
    if(cx<0||cy<0||cx>=tcols||cy>=trows)return;
    int x=LIVE_OX+cx*8, y=LIVE_OY+cy*TLH;
    u32 bg=TBG[cy][cx]; fill_rect(x,y,8,TLH,bg?(bg&0xFFFFFF):LIVE_BG);
    char ch=TCH[cy][cx]; if(ch!=' ')tglyph(x,y,ch,bg?TCO[cy][cx]:tmap(TCO[cy][cx]));
    if(on&&!g_vt_curhide){ fill_rect(x,y,8,TLH-1,rgb(120,220,140)); if(ch!=' ')tglyph(x,y,ch,LIVE_BG); }
}
void live_present(void){
    { extern int g_fb0_open; if(g_fb0_open>0){ g_lfull=1; return; } }
    int on=(int)((g_ticks/40)&1);
    int ccx=tcx>=tcols?tcols-1:tcx;
    if(g_lfull){
        fill_rect(0,0,(int)W,(int)H,LIVE_BG);
        for(int rr=0;rr<trows;rr++)live_row(rr);
        live_cell(ccx,tcy,on); g_lcx=ccx; g_lcy=tcy; g_lcon=on;
        blit_all(); g_lfull=0; return;
    }
    int y0=-1,y1=-1;
    for(int rr=0;rr<trows;rr++){ int d=0;
        for(int c=0;c<tcols;c++) if(g_lsh[rr][c]!=TCH[rr][c]||g_lsb[rr][c]!=TBG[rr][c]||(TCH[rr][c]!=' '&&g_lsc[rr][c]!=TCO[rr][c])){ d=1; break; }
        if(d){ live_row(rr); if(y0<0)y0=rr; y1=rr; if(rr==g_lcy)g_lcon=-1; } }
    if(ccx!=g_lcx||tcy!=g_lcy||on!=g_lcon){
        if(g_lcon!=-1&&(g_lcx!=ccx||g_lcy!=tcy)){ live_cell(g_lcx,g_lcy,0); blit_region(LIVE_OX+g_lcx*8,LIVE_OY+g_lcy*TLH,8,TLH); }
        live_cell(ccx,tcy,on); blit_region(LIVE_OX+ccx*8,LIVE_OY+tcy*TLH,8,TLH);
        g_lcx=ccx; g_lcy=tcy; g_lcon=on; }
    if(y0>=0) blit_region(0,LIVE_OY+y0*TLH,(int)W,(y1-y0+1)*TLH);
}
static void live_banner(void){ tcol(GREY); term_puts("To boot setup, type "); tcol(ACC); term_puts("noovexos_install"); tcol(WHT); term_puts("\n\n"); term_prompt(); }
void live_init(void){ g_live=1; g_tfont=1; ux_core_start("root"); live_geom(); term_clear(); live_banner(); live_invalidate(); }
int live_input(void){ int c; int changed=0; extern int g_mouse_open, g_fb0_open; extern void ux_mouse_push(int,int,int);
    if(mouse_evt||g_mouse_open){ cpu_irq_off(); int dx=mouse_dx, dy=mouse_dy, ev=mouse_evt; mouse_dx=0; mouse_dy=0; mouse_evt=0; cpu_irq_on();
        if(dx||dy){ mx+=dx; my-=dy; if(mx<0)mx=0; if(my<0)my=0; if(mx>=(int)W)mx=(int)W-1; if(my>=(int)H)my=(int)H-1; }
        if(g_mouse_open){ static int lb=-1; int b=(mouse_btn?1:0)|(mouse_rbtn?2:0); if(ev||dx||dy||b!=lb){ ux_mouse_push(dx,dy,b); lb=b; } } }
    while((c=kpop())>=0){ changed=1; int cc=c;
        if(kdown[0x1D]){ if(cc=='c'||cc=='C')cc=3; else if(cc=='z'||cc=='Z')cc=26; else if(cc=='d'||cc=='D')cc=4; else if(cc=='\\')cc=28; else if(g_tty_fg&&((cc>='a'&&cc<='z')||(cc>='A'&&cc<='Z')))cc=0x100|((cc|32)-'a'+1); else if(g_tty_fg&&cc=='[')cc=27; }
        if(g_tty_fg) tty_key(cc);
        else if(cc==3){ g_pwprompt=0; term_puts("^C\n"); tlen=0; term_prompt(); }
        else if(cc!=4&&cc!=26&&cc!=28&&cc<0x100) term_key((char)cc); }
    if(!g_fb0_open){ static u64 lastblink=0; u64 bk=g_ticks/40; if(bk!=lastblink){ lastblink=bk; changed=1; } }
    return changed; }

static void term_draw_classic(int i){
    const TTheme* t=&TTH[0]; int x=WN[i].x,y=WN[i].y,w=WN[i].w,h=WN[i].h,foc=(focus==i);
    char b[240]; { char pb[160],*p=b; dt_init(); dt_path(g_dcwd,pb); { extern void term_tilde(const char*,char*); char tb[170]; term_tilde(pb,tb); p=scopy(p,cur_user()); *p++='@'; p=scopy(p,g_hostname); *p++=':'; p=scopy(p,tb); } *p=0; }
    win_frame(i,b);
    static const char* const MB[4]={"File","Edit","View","Help"}; ui_menubar(x+3,y+TITLEH,w-6,MB,4);
    term_geom(); int cx0=x+4, cy0=y+TITLEH+22, cw=w-8, ch=h-TITLEH-22-4;
    ui_bevel(cx0,cy0,cw,ch,1); fill_rect(cx0+2,cy0+2,cw-4-16,ch-4,rgb(0,0,0));
    int ox=cx0+6, oy=cy0+5;
    if(g_tsb_off>tsb_n)g_tsb_off=tsb_n; if(g_tsb_off<0)g_tsb_off=0;
    for(int rr=0;rr<trows;rr++){ int v=tsb_n-g_tsb_off+rr; const char* rc; const u32* rcol;
        if(v<tsb_n){ int k=(tsb_head-tsb_n+v+TSB_N*2)%TSB_N; rc=TSB[k]; rcol=TSBC[k]; } else { int sr=v-tsb_n; if(sr>=trows)break; rc=TCH[sr]; rcol=TCO[sr]; }
        const u32* rbg=(v>=tsb_n)?TBG[v-tsb_n]:0;
        for(int c=0;c<tcols;c++){ char chh=rc[c]; u32 bg=rbg?rbg[c]:0; if(bg)fill_rect(ox+c*8,oy+rr*TLH,8,TLH,bg&0xFFFFFF); if(chh!=' ')tglyph(ox+c*8,oy+rr*TLH+1,chh,bg?rcol[c]:tmap(rcol[c])); } }
    if(!g_tsb_off&&!g_vt_curhide){ int ccx=tcx>=tcols?tcols-1:tcx; int ccx2=ox+ccx*8, ccy=oy+tcy*TLH; int on=((g_ticks/50)&1)||!foc; u32 cc=rgb(220,220,220);
        if(!foc){ for(int q=0;q<8;q++){ putpx(ccx2+q,ccy,cc); putpx(ccx2+q,ccy+TLH-2,cc); } for(int q=0;q<TLH-1;q++){ putpx(ccx2,ccy+q,cc); putpx(ccx2+7,ccy+q,cc); } }
        else if(on){ fill_rect(ccx2,ccy,8,TLH-1,cc); char c2=TCH[tcy][ccx]; if(c2!=' ')tglyph(ccx2,ccy+1,c2,rgb(0,0,0)); } }
    ui_scrollbar(cx0+cw-18,cy0+2,ch-4,tsb_n+trows,trows,tsb_n-g_tsb_off);
    (void)t;
}
void term_draw(int i){
    { extern int g_dstyle; if(g_dstyle==1){ term_draw_classic(i); return; } }
    const TTheme* t=&TTH[g_tth]; int x=WN[i].x,y=WN[i].y,w=WN[i].w,h=WN[i].h,r=14,foc=(focus==i);
    shadow_soft(x,y,w,h,r); term_glass(x,y,w,h,r,t->bg); fill_round(x,y,w,TITLEH,r,1|2,t->bar);
    u32 tc=foc?(t->light?rgb(40,44,54):rgb(226,230,240)):t->dim;
    fill_round(x+14,y+11,22,22,6,15,ACC); draw_logo(x+25,y+15,14,rgb(255,255,255),256);
    for(int q=0;q<w-2*r;q++){ u32 gc=mix(ACC,rgb(150,90,230),q*255/(w-2*r)); putpx(x+r+q,y+TITLEH-1,gc); putpx(x+r+q,y+TITLEH,mix(gc,t->bg,150)); }
    { char pb[160],b[240],*p=b; dt_init(); dt_path(g_dcwd,pb); { extern void term_tilde(const char*,char*); char tb[170]; term_tilde(pb,tb); p=scopy(p,cur_user()); *p++='@'; p=scopy(p,g_hostname); *p++=':'; p=scopy(p,tb); } *p=0;
      int maxc=(w-48-150)/8; if(maxc<8)maxc=8; if(slen(b)>maxc){ b[maxc-2]='.'; b[maxc-1]='.'; b[maxc]=0; } if(!g_gm_ready)gmetrics(); pdstr(x+46,y+15,b,tc,1); }
    { int cw=46,xr=x+w; for(int bn=0;bn<3;bn++){ int bx=xr-(3-bn)*cw; int hov=(foc&&mx>=bx&&mx<bx+cw&&my>=y&&my<y+TITLEH);
        if(hov){ u32 hc=(bn==2)?rgb(232,64,54):mix(t->bar,t->light?rgb(0,0,0):rgb(255,255,255),30);
            for(int j=0;j<TITLEH;j++)for(int ii=0;ii<cw;ii++){ int gx=(bx-x)+ii; if(!cut(gx,j,w,TITLEH,r,1|2))putpx(bx+ii,y+j,hc); } }
        int gx=bx+cw/2, gy=y+TITLEH/2; u32 gc=(hov&&bn==2)?rgb(255,255,255):tc;
        if(bn==0){ for(int q=-5;q<=5;q++){ putpx(gx+q,gy,gc); putpx(gx+q,gy+1,gc); } }
        else if(bn==1){ for(int q=-5;q<=5;q++){ putpx(gx+q,gy-5,gc); putpx(gx+q,gy+5,gc); putpx(gx-5,gy+q,gc); putpx(gx+5,gy+q,gc); } }
        else { for(int q=-5;q<=5;q++){ putpx(gx+q,gy+q,gc); putpx(gx+q+1,gy+q,gc); putpx(gx+q,gy-q,gc); putpx(gx+q+1,gy-q,gc); } } } }
    term_geom(); int sb=g_tstatus?26:0; int ox=x+TPAD, oy=y+TITLEH+TPAD-2;
    if(g_tsb_off>tsb_n)g_tsb_off=tsb_n; if(g_tsb_off<0)g_tsb_off=0;
    { extern void gfx_scroll_area(int,int,int,int,int,int); gfx_scroll_area(i,x+2,oy-2,w-4,trows*TLH,(tsb_n-g_tsb_off)*TLH); }
    if(!g_tsb_off&&foc) fill_rect(x+2,oy+tcy*TLH-2,w-4,TLH,mix(t->bg,t->light?rgb(0,0,0):rgb(255,255,255),t->light?10:8));
    for(int rr=0;rr<trows;rr++){ int v=tsb_n-g_tsb_off+rr; const char* rc; const u32* rcol;
        if(v<tsb_n){ int k=(tsb_head-tsb_n+v+TSB_N*2)%TSB_N; rc=TSB[k]; rcol=TSBC[k]; } else { int sr=v-tsb_n; if(sr>=trows)break; rc=TCH[sr]; rcol=TCO[sr]; }
        const u32* rbg=(v>=tsb_n)?TBG[v-tsb_n]:0;
        for(int c=0;c<tcols;c++){ char ch=rc[c]; u32 bg=rbg?rbg[c]:0; if(bg)fill_rect(ox+c*8,oy+rr*TLH,8,TLH,bg&0xFFFFFF); if(ch!=' ')tglyph(ox+c*8,oy+rr*TLH+1,ch,bg?rcol[c]:tmap(rcol[c])); } }
    if(!g_tsb_off&&!g_vt_curhide){ int ccx=tcx>=tcols?tcols-1:tcx; int cx=ox+ccx*8, cy=oy+tcy*TLH; int on=((g_ticks/50)&1)||!foc; u32 cc=t->cur;
        if(!foc){ for(int q=0;q<8;q++){ putpx(cx+q,cy,cc); putpx(cx+q,cy+TLH-2,cc); } for(int q=0;q<TLH-1;q++){ putpx(cx,cy+q,cc); putpx(cx+7,cy+q,cc); } }
        else if(on){ if(g_tcur==0){ fill_rect(cx,cy,8,TLH-1,cc); char ch=TCH[tcy][ccx]; if(ch!=' ')tglyph(cx,cy+1,ch,t->bg); }
            else if(g_tcur==1)fill_rect(cx,cy,2,TLH-1,cc); else fill_rect(cx,cy+TLH-3,8,2,cc); } }
    if(tsb_n>0){ int th=trows*TLH, tot=tsb_n+trows, bh=th*trows/tot; if(bh<24)bh=24; int by=oy+(th-bh)*(tsb_n-g_tsb_off)/(tsb_n?tsb_n:1);
        fill_round(x+w-9,by,4,bh,2,15,mix(t->bg,t->light?rgb(0,0,0):rgb(255,255,255),g_tsb_off?90:40)); }
    if(sb){ int sy=y+h-sb; extern int g_gfx; extern void gfx_round(int,int,int,int,int,int,u32,int); if(g_gfx){ gfx_round(x,sy,w,sb,r,12,t->bar,g_topa>=100?256:220); } else { u32* sc=SCENE; for(int j=0;j<sb;j++)for(int q=0;q<w;q++){ if(cut(q,h-sb+j,w,h,r,15))continue; u32* pp=&sc[(u64)(sy+j)*SW+x+q]; if((u32)(x+q)<W&&(u32)(sy+j)<H)*pp=mix(*pp,t->bar,g_topa>=100?256:220); } } fill_rect(x+1,sy,w-2,1,mix(t->bar,t->light?rgb(0,0,0):rgb(255,255,255),t->light?24:18));
        fill_circle(x+18,sy+13,4,g_uid==0?t->red:t->green); char b[160],*p=b; p=scopy(p,cur_user()); *p++='@'; p=scopy(p,g_hostname); *p=0; int ex=dstr(x+30,sy+6,b,t->dim,1);
        { char pb[128]; dt_path(g_dcwd,pb); fill_rect(ex+10,sy+8,1,11,mix(t->bar,t->dim,120)); dstr(ex+20,sy+6,pb,t->blue,1); }
        p=b; if(g_tsb_off){ p=scopy(p,"scrolled up "); p=udec(p,(u32)g_tsb_off); p=scopy(p," lines - type to return"); }
        else { p=scopy(p,"nsh   "); p=udec(p,(u32)tcols); *p++='x'; p=udec(p,(u32)trows); } *p=0;
        dstr(x+w-slen(b)*8-18,sy+6,b,g_tsb_off?t->cur:t->dim,1); }
    { int gx=x+w-6, gy=y+h-6; u32 gc=t->dim; for(int k=0;k<3;k++){ int o=k*4; for(int q=0;q<=o;q++)putpx(gx-o+q,gy-q,gc); } }
}


static int sh_is_user_cmd(const char* c){
    static const char* L[]={"passwd","id","groups","su","useradd","adduser","userdel","deluser","usermod","last","lastlog","hostname","lock","logout","users","who",0};
    for(int i=0;L[i];i++) if(seq((char*)c,L[i]))return 1; return 0;
}
static void sh_id_line(const char* name){
    acct_info a; if(acct_get(name,&a)<0){ sh_err("id: no such user: ",name); return; }
    char b[400],*p=b; p=scopy(p,"uid="); p=udec(p,(u32)a.uid); *p++='('; p=scopy(p,a.name); p=scopy(p,") gid="); p=udec(p,(u32)a.gid); *p++='(';
    { char gl[256]; acct_groups(a.name,gl,256); int k=0; while(gl[k]&&gl[k]!=' ')*p++=gl[k++]; *p++=')'; p=scopy(p," groups=");
      int first=1; const char* g=gl; while(*g){ char one[40]; int n=0; while(*g&&*g!=' '&&n<39)one[n++]=*g++; one[n]=0; while(*g==' ')g++; int gid=acct_group_gid(one); if(!first)*p++=','; first=0; p=udec(p,(u32)(gid<0?0:gid)); *p++='('; p=scopy(p,one); *p++=')'; if(p-b>360)break; } }
    *p=0; term_puts(b); term_putc('\n');
}
static void sh_user_cmds(const char* cmd,const char* arg){
    char w[8][72]; int n=sh_words(arg,w,8);
    if(seq((char*)cmd,"id")){ sh_id_line(n?w[0]:cur_user()); return; }
    if(seq((char*)cmd,"groups")){ char gl[256]; const char* who=n?w[0]:cur_user(); if(acct_groups(who,gl,256)<0){ sh_err("groups: no such user: ",who); return; } if(n){ term_puts(who); term_puts(" : "); } term_puts(gl); term_putc('\n'); return; }
    if(seq((char*)cmd,"users")||seq((char*)cmd,"who")){ if(g_logged_in){ term_puts(g_user); if(seq((char*)cmd,"who")){ term_puts("     desktop"); } term_putc('\n'); } return; }
    if(seq((char*)cmd,"last")||seq((char*)cmd,"lastlog")){ static char lb[4096]; int k=acct_last(lb,(int)sizeof(lb)); if(k<=0)term_puts("wtmp is empty\n"); else term_puts(lb); return; }
    if(seq((char*)cmd,"hostname")){
        if(!n){ term_puts(g_hostname); term_putc('\n'); return; }
        if(!need_root("hostname"))return;
        if(acct_set_hostname(w[0])<0){ sh_err("hostname: ",g_acct_err); return; }
        acct_log("hostname changed to ",w[0],0); return; }
    if(seq((char*)cmd,"lock")){ if(g_state==ST_DESKTOP&&g_logged_in)acct_ui_lock(); else term_puts("lock: no desktop session\n"); return; }
    if(seq((char*)cmd,"logout")){ if(g_state==ST_DESKTOP&&g_logged_in)acct_ui_signout(); else term_puts("logout: no desktop session\n"); return; }
    if(seq((char*)cmd,"passwd")){
        int lockf=0,unlk=0,del=0; const char* who=0;
        for(int i=0;i<n;i++){ if(seq(w[i],"-l"))lockf=1; else if(seq(w[i],"-u"))unlk=1; else if(seq(w[i],"-d"))del=1; else who=w[i]; }
        const char* me=cur_user(); if(!who)who=me;
        acct_info a; if(acct_get(who,&a)<0){ sh_err("passwd: no such user: ",who); return; }
        if(g_uid!=0&&!seq((char*)who,(char*)me)){ sh_err("passwd: you may only change your own password (try: sudo passwd ",who); return; }
        if(g_uid!=0&&seq((char*)who,"guest")){ sh_err("passwd: the guest account has no password",0); return; }
        if(lockf||unlk){ if(g_uid!=0){ sh_err("passwd: -l and -u need root",0); return; } if(acct_set_locked(who,lockf)<0){ sh_err("passwd: ",g_acct_err); return; } term_puts(lockf?"passwd: password locked\n":"passwd: password unlocked\n"); return; }
        if(del){ if(g_uid!=0){ sh_err("passwd: -d needs root",0); return; } acct_set_password(who,""); term_puts("passwd: password removed (signs in without a password)\n"); return; }
        term_puts("Changing password for "); term_puts(who); term_putc('\n');
        if(g_uid!=0&&acct_has_password(who)){ char cur[72]; term_puts("Current password: "); if(sh_readpw(cur,72)<0)return; int ok=acct_check(who,cur); sh_wipe(cur,72); if(!ok){ sh_err("passwd: authentication token manipulation error (wrong password)",0); acct_log("passwd: wrong current password for ",who,0); return; } }
        char np[72]; if(sh_ask_newpw(0,np,72)<0){ sh_err("passwd: password unchanged",0); return; }
        int rc=acct_set_password(who,np); sh_wipe(np,72);
        if(rc<0)sh_err("passwd: ",g_acct_err); else { g_sudo_until=0; tcol(GREEN); term_puts("passwd: password updated successfully\n"); tcol(WHT); }
        return; }
    if(seq((char*)cmd,"su")){
        const char* who="root"; for(int i=0;i<n;i++) if(!seq(w[i],"-")&&!seq(w[i],"-l"))who=w[i];
        acct_info a; if(acct_get(who,&a)<0){ sh_err("su: user does not exist: ",who); return; }
        if(g_uid!=0){
            if(a.locked&&!a.haspw){ sh_err("su: ",a.uid==0?"root has no password (it was not set in setup) - use sudo":"this account is locked"); return; }
            if(a.haspw){ char pw[72]; term_puts("Password: "); if(sh_readpw(pw,72)<0)return; int ok=acct_check(who,pw); sh_wipe(pw,72); if(!ok){ sh_err("su: Authentication failure",0); acct_log("su: FAILED to ",who," (wrong password)"); return; } } }
        g_uid=a.uid; g_gid=a.gid; g_sudo_until=0; env_set("USER",a.name); env_set("LOGNAME",a.name); env_set("HOME",a.uid==0?"/root":a.home); { extern void acct_set_path(const char*); acct_set_path(a.uid==0?"/root":a.home); }
        { int d=dt_resolve_dir(a.uid==0?"/root":a.home); if(d>=0)g_dcwd=d; }
        acct_log("su: session opened for ",a.name,0);
        return; }
    if(!need_root(cmd))return;
    if(seq((char*)cmd,"useradd")||seq((char*)cmd,"adduser")){
        int adm=0; const char* full=""; const char* nm=0; int inter=seq((char*)cmd,"adduser");
        for(int i=0;i<n;i++){ if(seq(w[i],"-m"))continue; else if(seq(w[i],"-a")||seq(w[i],"--admin"))adm=1; else if((seq(w[i],"-c")||seq(w[i],"--comment"))&&i+1<n)full=w[++i]; else if(seq(w[i],"-G")&&i+1<n){ if(seq(w[i+1],"admin")||seq(w[i+1],"sudo"))adm=1; i++; } else nm=w[i]; }
        if(!nm){ term_puts(inter?"usage: adduser [--admin] [-c \"Full Name\"] NAME   (asks for a password)\n":"usage: useradd [-a] [-c \"Full Name\"] NAME   (then: passwd NAME)\n"); return; }
        char np[72]; np[0]=0;
        if(inter){ char fb[72]; if(!full[0]){ term_puts("Full name: "); int L=0; sh_redraw(); for(;;){ int c=kpop(); if(c<0){ cpu_idle(); continue; } if(c=='\n'){ term_putc('\n'); break; } if(c==3){ term_puts("^C\n"); return; } if((c=='\b'||c==127)&&L>0){ L--; term_putc('\b'); } else if(c>=32&&c<127&&L<60){ fb[L++]=(char)c; term_putc((char)c); } sh_redraw(); } fb[L]=0; full=fb; }
            if(sh_ask_newpw(nm,np,72)<0){ sh_err("adduser: no account created",0); return; } }
        int uid=acct_create(nm,full,np,adm); sh_wipe(np,72);
        if(uid<0){ sh_err(cmd,": "); sh_err(g_acct_err,0); return; }
        if(!inter)acct_lock_password(nm);
        { char b[160],*p=b; p=scopy(p,inter?"adduser: created ":"useradd: created "); p=scopy(p,nm); p=scopy(p," (uid "); p=udec(p,(u32)uid); p=scopy(p,", home /home/"); p=scopy(p,nm); p=scopy(p,adm?", administrator)":", standard user)"); *p=0; term_puts(b); term_putc('\n'); }
        if(!inter)term_puts("useradd: the account is locked until you set a password: passwd NAME\n");
        return; }
    if(seq((char*)cmd,"userdel")||seq((char*)cmd,"deluser")){
        int rm=0; const char* nm=0; for(int i=0;i<n;i++){ if(seq(w[i],"-r")||seq(w[i],"--remove-home"))rm=1; else nm=w[i]; }
        if(!nm){ term_puts("usage: userdel [-r] NAME   (-r also deletes the home folder)\n"); return; }
        if(acct_delete(nm,rm)<0){ sh_err("userdel: ",g_acct_err); return; }
        term_puts(rm?"userdel: account and home folder removed\n":"userdel: account removed (home folder kept)\n"); return; }
    if(seq((char*)cmd,"usermod")){
        const char* nm=n?w[n-1]:0; if(!nm||n<2){ term_puts("usage: usermod -aG GROUP NAME | -rG GROUP NAME | -c \"Full Name\" NAME | -L NAME | -U NAME\n"); return; }
        for(int i=0;i<n-1;i++){
            if((seq(w[i],"-aG")||seq(w[i],"-G")||seq(w[i],"-a"))&&i+1<n-1){ if(seq(w[i],"-a")&&seq(w[i+1],"-G"))i++; const char* g=w[i+1]; i++;
                if(seq((char*)g,"admin")||seq((char*)g,"sudo")){ if(acct_set_admin(nm,1)<0){ sh_err("usermod: ",g_acct_err); return; } }
                else if(acct_group_add(nm,g)<0){ sh_err("usermod: ",g_acct_err); return; } }
            else if(seq(w[i],"-rG")&&i+1<n-1){ const char* g=w[++i];
                if(seq((char*)g,"admin")||seq((char*)g,"sudo")){ if(acct_set_admin(nm,0)<0){ sh_err("usermod: ",g_acct_err); return; } }
                else if(acct_group_del(nm,g)<0){ sh_err("usermod: ",g_acct_err); return; } }
            else if(seq(w[i],"-c")&&i+1<n-1){ if(acct_set_full(nm,w[++i])<0){ sh_err("usermod: ",g_acct_err); return; } }
            else if(seq(w[i],"-L")){ if(acct_set_locked(nm,1)<0){ sh_err("usermod: ",g_acct_err); return; } }
            else if(seq(w[i],"-U")){ if(acct_set_locked(nm,0)<0){ sh_err("usermod: ",g_acct_err); return; } }
            else { sh_err("usermod: unknown option ",w[i]); return; } }
        term_puts("usermod: done\n"); return; }
}
