    else if(isdir){ int d=g_prop_dir; int nf=0,nd=0; u64 by2=0; fx_dir_stats(d,&nf,&nd,&by2);
        pr_row(lx,&ly,"Type:","Folder");
        { char pb[200]; dt_path(DT[d].parent>=0?DT[d].parent:0,pb); pr_row(lx,&ly,"Location:",pb); }
        p=b; p=udec(p,(u32)nf); p=scopy(p,nf==1?" file, ":" files, "); p=udec(p,(u32)nd); p=scopy(p,nd==1?" folder":" folders"); *p=0; pr_row(lx,&ly,"Contains:",b);
        p=pr_size(b,by2); *p=0; pr_row(lx,&ly,"Size:",b);
        pr_mode(b,DT[d].mode?DT[d].mode:0755u,1); pr_row(lx,&ly,"Permissions:",b); }
    else { int a=g_prop_app; pr_row(lx,&ly,"Type:","Application");
        pr_row(lx,&ly,"Status:",WN[a].open?(WN[a].min?"Open (minimized)":"Open"):"Closed");
        p=b; p=udec(p,(u32)WN[a].w); p=scopy(p," x "); p=udec(p,(u32)WN[a].h); p=scopy(p," pixels"); *p=0; pr_row(lx,&ly,"Window size:",b);
        pr_row(lx,&ly,"Part of:","NoovexOS 64"); }
    ly+=4; fill_rect(lx,ly,cw-28,1,rgb(128,128,128)); fill_rect(lx,ly+1,cw-28,1,rgb(255,255,255)); ly+=10;
    dstr(lx,ly,"Change icon:",rgb(0,0,0),1); ly+=20;
    int cur=pr_cur();
    for(int k=0;k<12;k++){ int bx=lx+(k%6)*56, by=ly+(k/6)*48; int on=(cur==k+1), hov=in_rect(mx,my,bx,by,48,44);
        ui_bevel(bx,by,48,44,on); fill_rect(bx+2,by+2,44,40,on?rgb(255,255,255):(hov?rgb(226,222,214):FACE)); icon_choice_draw(bx+8,by+6,32,k); }
    ly+=100;
    { int bx=lx, by=ly; int hov=in_rect(mx,my,bx,by,90,24); ui_bevel(bx,by,90,24,0); fill_rect(bx+2,by+2,86,20,hov?rgb(226,222,214):FACE); dstr_ctr(bx+45,by+4,"Default",rgb(0,0,0),1); }
    if(cur){ char t[48],*q=t; q=scopy(q,"Icon: "); q=scopy(q,icon_choice_name(cur-1)); *q=0; dstr(lx+104,ly+4,t,rgb(64,64,64),1); }
    { int bw=80, bx=x+pw-bw-12, by=y+ph-36; int hov=in_rect(mx,my,bx,by,bw,26); ui_bevel(bx,by,bw,26,0); fill_rect(bx+2,by+2,bw-4,22,hov?rgb(226,222,214):FACE); dstr_ctr(bx+bw/2,by+5,"OK",rgb(0,0,0),1); }
    g_round_off=0;
}
int prop_click(int px,int py){
    if(!g_prop)return 0;
    int x=pr_x, y=pr_y, pw=pr_w, ph=pr_h;
    if(!in_rect(px,py,x,y,pw,ph)){ g_prop=0; return 0; }
    if(in_rect(px,py,x+pw-22,y+5,16,14)||in_rect(px,py,x+pw-92,y+ph-36,80,26)){ g_prop=0; return 1; }
    int cx=x+10, cy=y+52, cw=pw-20; (void)cw; int lx=cx+14;
    int ly=cy+14+46+10+5*22+4+10+20;
    if(g_prop_app>=0&&g_prop_slot<0&&g_prop_dir<0) ly-=22;
    for(int k=0;k<12;k++){ int bx=lx+(k%6)*56, by=ly+(k/6)*48; if(in_rect(px,py,bx,by,48,44)){ pr_set(k+1); return 1; } }
    if(in_rect(px,py,lx,ly+100,90,24)){ pr_set(0); return 1; }
    return 1;
}
int desk_hit(int px,int py){ (void)px;(void)py; return -1; for(int k=0;k<5;k++){ if(px>=g_apx[k]&&px<g_apx[k]+48&&py>=g_apy[k]&&py<g_apy[k]+72) return k; }
    for(int s=0;s<FS_N;s++) if(FS[s].used&&FS[s].folder==2&&g_fpl[s]){ if(px>=g_fpx[s]&&px<g_fpx[s]+48&&py>=g_fpy[s]&&py<g_fpy[s]+72) return 100+s; } return -1; }
void desk_open(int it){ if(it<0)return; if(it<100){ focus_win(DESK[it]); return; } int s=it-100; if(s<0||s>=FS_N||!FS[s].used)return; int kd=fkind(s); g_fsel=s; { extern int py_open_slot(int); if(py_open_slot(s))return; } if(fext_eq(FS[s].name,"pptx")){ pptx_load(s); open_win(W_PPTX); } else if(video_is_media_name(FS[s].name)) video_open_slot(s); else if(kd==1)g_imgview=s; else if(kd==2)browser_show_html(fsd(s),FS[s].len,FS[s].name); else editor_open(s); }
static int desk_click(int px,int py){ int it=desk_hit(px,py); if(it>=0){ desk_open(it); return 1; } return 0; }
/* ---- app launcher ---- */
static const int LAUNCH[23]={W_FILES,W_TERM,W_EDITOR,W_BROWSER,W_VIDEO,W_CALC,W_CLOCK,W_CALENDAR,W_SYSMON,W_SNAKE,W_FLAPPY,W_PAINT,W_DOOM,W_NEOFETCH,W_LOGS,W_TFIND,W_SETTINGS,W_ABOUT,W_CAMERA,W_BENCH,W_RECORDER,W_WRITER,W_QUAKE};
char g_lq[40]=""; int g_lqn=0;           /* launcher search query */
/* result kinds: 0=app(open_win), 1=settings page(open settings@cat), 2=file(open in editor/viewer) */
static int g_lres_kind[24], g_lres_arg[24], g_lres_n=0;
static const char* SET_PAGES[5]={"Settings: System","Settings: Personalization","Settings: Network","Settings: Display","Settings: About"};
static int lc_ci(char a){ if(a>='A'&&a<='Z')a+=32; return a; }
static int lc_has(const char* hay,const char* needle){ if(!needle[0])return 1;
    for(int i=0;hay[i];i++){ int j=0; while(needle[j]&&hay[i+j]&&lc_ci(hay[i+j])==lc_ci(needle[j]))j++; if(!needle[j])return 1; } return 0; }
extern int app_cat(int);
void launcher_search(void){ g_lres_n=0;
    int hg=focus_hides_games();
    for(int k=0;k<23 && g_lres_n<24;k++) if(!(hg&&(LAUNCH[k]==W_SNAKE||LAUNCH[k]==W_FLAPPY||LAUNCH[k]==W_DOOM||LAUNCH[k]==W_QUAKE))&&lc_has(APPNAME[LAUNCH[k]],g_lq)){ g_lres_kind[g_lres_n]=0; g_lres_arg[g_lres_n]=LAUNCH[k]; g_lres_n++; }
    if(pyide_installed()&&lc_has("Python IDE",g_lq)&&g_lres_n<24){ g_lres_kind[g_lres_n]=3; g_lres_arg[g_lres_n]=0; g_lres_n++; }
    if((lc_has("IDLE",g_lq)||lc_has("python",g_lq)||lc_has("tkinter",g_lq))&&g_lres_n<24){ g_lres_kind[g_lres_n]=4; g_lres_arg[g_lres_n]=0; g_lres_n++; }
    { int c=xapps_count(); for(int i=0;i<c&&g_lres_n<24;i++) if(!(hg&&app_cat(1100+i)==4)&&lc_has(app_name(1100+i),g_lq)){ g_lres_kind[g_lres_n]=5; g_lres_arg[g_lres_n]=1100+i; g_lres_n++; } }
    if(g_lq[0]) for(int k=0;k<5 && g_lres_n<24;k++) if(lc_has(SET_PAGES[k],g_lq)){ g_lres_kind[g_lres_n]=1; g_lres_arg[g_lres_n]=k; g_lres_n++; }
    if(g_lq[0]) for(int f=0;f<FS_N && g_lres_n<24;f++) if(FS[f].used && lc_has(FS[f].name,g_lq)){ g_lres_kind[g_lres_n]=2; g_lres_arg[g_lres_n]=f; g_lres_n++; }
}
static void launcher_open_result(int idx){ if(idx<0||idx>=g_lres_n)return; g_launcher=0; int kind=g_lres_kind[idx],arg=g_lres_arg[idx];
    if(kind==0) open_win(arg);
    else if(kind==1){ g_setcat=arg; open_win(W_SETTINGS); }
    else if(kind==2){ extern int py_open_slot(int); if(py_open_slot(arg))return; ed_py=0; editor_open(arg); open_win(W_EDITOR); }
    else if(kind==3){ pyide_open(); }
    else if(kind==4){ app_launch(1001); }
    else if(kind==5){ app_launch(arg); }        /* open file in editor */
}
static const char* LPWR[4]={"Log Out","Sleep","Restart","Shut Down"};
static void launcher_geom(int*px,int*py,int*pw,int*ph);
static void launcher_pwr(int i,int* x,int* y,int* w,int* h){ int px,py,pw,ph; launcher_geom(&px,&py,&pw,&ph); int bw[4]={88,72,84,104}; int xx=px+pw-20; for(int k=3;k>=i;k--)xx-=bw[k]+8; *x=xx+8; *y=py+ph-46; *w=bw[i]; *h=30; }
static void launcher_power(int i){ if(i==0){ extern void acct_ui_lock(void); acct_ui_lock(); } else if(i==1){ power_sleep(); } else if(i==2){ power_reboot(); } else power_off(); }
#define LB_SIDE 64
#define LB_TOP 76
#define LB_ROW 46
#define LB_FOOT 56
#define LB_COLS 4
#define LB_CELLH 82
#define LB_HDR 32
#define APP_PYIDE 1000
#define LG_N 8
#define LG_MAX 40
#define LR_H 48
extern int app_cat(int);
static const char* LG_NAME[LG_N]={"Office","Internet","Media & Graphics","Development","Games","System","Accessories","Drivers & Hardware"};
static const int LG_BASE[LG_N][10]={{W_WRITER,W_CALC,W_CALENDAR,W_CLOCK,-1},{W_BROWSER,-1},{W_VIDEO,W_PAINT,W_RECORDER,W_CAMERA,-1},{W_TERM,W_EDITOR,W_TFIND,W_XDISP,1001,-1},{W_SNAKE,W_FLAPPY,W_DOOM,W_QUAKE,-1},{W_FILES,W_SYSMON,W_LOGS,W_NEOFETCH,W_SETTINGS,-1},{-1},{W_GPU3D,W_CAMERA,W_RECORDER,W_BENCH,W_SYSMON,W_SETTINGS,-1}};
static const u32 LG_COL[LG_N]={0x3D7BD9,0x2E9AC8,0xC0508A,0xC8A040,0x8060C0,0x5070C0,0x40A060,0x2E9858};
static int LGRP[LG_N][LG_MAX+1]; static int g_lgn[LG_N]; static int g_lvis[LG_N]; static int g_nvis; static int g_lsub=-1;
static void lgrp_refresh(void){
    for(int g=0;g<LG_N;g++){ int n=0; for(int k=0;LG_BASE[g][k]>=0&&n<LG_MAX;k++)LGRP[g][n++]=LG_BASE[g][k]; if(g==3&&pyide_installed()&&n<LG_MAX)LGRP[g][n++]=APP_PYIDE; g_lgn[g]=n; }
    int c=xapps_count(); for(int i=0;i<c;i++){ int g=app_cat(1100+i); if(g<0||g>6)g=6; if(g_lgn[g]<LG_MAX)LGRP[g][g_lgn[g]++]=1100+i; }
    if(focus_hides_games())g_lgn[4]=0;
    g_nvis=0; for(int g=0;g<LG_N;g++){ LGRP[g][g_lgn[g]]=-1; if(g_lgn[g])g_lvis[g_nvis++]=g; }
    if(g_lsub>=g_nvis)g_lsub=-1; }
static void launcher_geom(int*px,int*py,int*pw,int*ph){ lgrp_refresh(); *pw=420; *ph=LB_TOP+g_nvis*LR_H+24; if(*ph<470)*ph=470; if(*ph>g_tby-24)*ph=g_tby-24; *px=12; *py=g_tby-*ph-12; }
static void lsub_geom(int v,int*x,int*y,int*w,int*h){ int px,py,pw,ph; launcher_geom(&px,&py,&pw,&ph); int g=g_lvis[v], n=g_lgn[g], rows=(n+LB_COLS-1)/LB_COLS;
    *w=LB_COLS*92+24; *h=LB_HDR+rows*LB_CELLH+12; if(*h>g_tby-24){ *h=g_tby-24; }
    *x=px+pw+6; if(*x+*w>(int)W-4)*x=(int)W-4-*w; *y=py+LB_TOP+v*LR_H-8; if(*y+*h>g_tby-8)*y=g_tby-8-*h; if(*y<4)*y=4; }
static void lapp_open(int app){ g_launcher=0; g_lsub=-1; if(app==APP_PYIDE)pyide_open(); else if(app>1000)app_launch(app); else { if(app==W_EDITOR)ed_py=0; open_win(app); } }
static const char* lapp_name(int app){ return app>=1000?(app==1001?"IDLE":app==APP_PYIDE?"Python IDE":app_name(app)):APPNAME[app]; }
static const int LSIDE[6]={W_FILES,W_EDITOR,W_PAINT,W_CLOCK,W_SYSMON,W_BROWSER};
static void lside_icon(int k,int cx,int cy,u32 c){
    if(k==0){ fill_round(cx-8,cy-6,16,11,2,15,c); fill_rect(cx-3,cy+6,6,2,c); }
    else if(k==1){ fill_round(cx-6,cy-8,12,16,2,15,c); for(int r=0;r<3;r++)fill_rect(cx-3,cy-4+r*4,6,1,rgb(40,32,70)); }
    else if(k==2){ fill_round(cx-8,cy-6,16,12,2,15,c); fill_circle(cx-3,cy-2,2,rgb(40,32,70)); }
    else if(k==3){ fill_circle(cx-3,cy+4,3,c); fill_rect(cx-1,cy-7,2,11,c); fill_rect(cx-1,cy-7,7,2,c); }
    else if(k==4){ fill_round(cx-7,cy-7,14,14,2,15,c); for(int r=0;r<3;r++)fill_rect(cx-7+r*5,cy-7,1,14,rgb(40,32,70)); }
    else { fill_circle(cx,cy,7,c); fill_rect(cx-1,cy-4,2,6,rgb(40,32,70)); fill_rect(cx-3,cy,6,2,rgb(40,32,70)); } }
static int lb_hit(int x,int y,int w,int h){ return mx>=x&&mx<x+w&&my>=y&&my<y+h; }
#define CM_NC g_nvis
#define CM_IH 24
static int g_csub=-1;
static void cm_geom(int*x,int*y,int*w,int*h){ *w=206; *h=6+CM_IH+8+CM_NC*CM_IH+8+3*CM_IH+8+CM_IH; *x=2; *y=g_tby-*h+1; }
static int cm_item_y(int it,int y0){ int y=y0+3; if(it==0)return y; y+=CM_IH+8; if(it<=CM_NC)return y+(it-1)*CM_IH; y+=CM_NC*CM_IH+8; if(it<=CM_NC+3)return y+(it-CM_NC-1)*CM_IH; return y+3*CM_IH+8; }
static int cm_cols(int c){ int n=g_lgn[g_lvis[c]]; return (n*CM_IH+6>g_tby-8)?2:1; }
static void cm_sub_geom(int c,int*x,int*y,int*w,int*h){ int mx0,my0,mw,mh; cm_geom(&mx0,&my0,&mw,&mh); int n=g_lgn[g_lvis[c]], cols=cm_cols(c), rows=(n+cols-1)/cols; *w=200*cols; *h=rows*CM_IH+6; *x=mx0+mw-3; *y=cm_item_y(1+c,my0)-3; if(*y+*h>g_tby)*y=g_tby-*h; if(*y<0)*y=0; }
static int cm_sub_app(int c,int k){ return LGRP[g_lvis[c]][k]; }
static void classic_menu_draw(void){ lgrp_refresh(); if(g_csub>=g_nvis)g_csub=-1; int x,y,w,h; cm_geom(&x,&y,&w,&h);
    g_bevel_glass=1; bevel(x,y,w,h,0); g_bevel_glass=0; if(g_gfx&&g_gfx_fx){ gq_blend(g_rt,x+3,y+3,24,h-6,rgb(219,216,209),200); gq_blend(g_rt,x+27,y+3,w-30,h-6,rgb(255,255,255),222); } else { fill_rect(x+3,y+3,24,h-6,rgb(219,216,209)); fill_rect(x+27,y+3,w-30,h-6,rgb(255,255,255)); } fill_rect(x+27,y+3,1,h-6,rgb(128,128,128));
    const char* LB[LG_N+5]; LB[0]="About NoovexOS"; for(int v=0;v<g_nvis;v++)LB[1+v]=LG_NAME[g_lvis[v]]; LB[g_nvis+1]="Settings"; LB[g_nvis+2]="Help"; LB[g_nvis+3]="Run..."; LB[g_nvis+4]="Exit...";
    int hovit=-1;
    for(int it=0;it<CM_NC+5;it++){ int iy=cm_item_y(it,y); int hov=(mx>=x+3&&mx<x+w-3&&my>=iy&&my<iy+CM_IH); if(hov)hovit=it;
        int sel=hov||(it>=1&&it<=CM_NC&&g_csub==it-1);
        if(sel) fill_rect(x+4,iy,w-7,CM_IH,rgb(210,192,182));
        if(it==0) nv_logo_small(x+7,iy+4); else if(it==CM_NC+1){ int cx2=x+15,cy2=iy+12; fill_circle(cx2,cy2,7,rgb(90,90,90)); fill_circle(cx2,cy2,5,rgb(170,170,170)); fill_circle(cx2,cy2,2,rgb(80,80,80)); } else if(it==CM_NC+2){ fill_circle(x+15,iy+12,7,rgb(40,100,200)); dstr(x+12,iy+5,"?",rgb(255,255,255),1); } else if(it==CM_NC+3){ fill_rect(x+7,iy+5,16,13,rgb(128,128,128)); fill_rect(x+8,iy+6,14,11,rgb(0,0,0)); dstr(x+9,iy+5,">",rgb(0,220,0),1); } else if(it<=CM_NC){ u32 c=LG_COL[g_lvis[it-1]]; fill_rect(x+7,iy+4,16,16,c); fill_rect(x+7,iy+4,16,1,mix(c,rgb(255,255,255),110)); char t[2]={LB[it][0],0}; dstr(x+11,iy+5,t,rgb(255,255,255),1); }
        else { fill_rect(x+7,iy+5,16,12,rgb(210,192,182)); fill_rect(x+9,iy+7,12,8,rgb(80,140,220)); fill_rect(x+12,iy+17,6,2,rgb(80,80,80)); }
        dstr(x+36,iy+5,LB[it],rgb(0,0,0),1);
        if(it>=1&&it<=CM_NC){ int ax=x+w-14, ay=iy+8; u32 ac=rgb(0,0,0); for(int k=0;k<4;k++) fill_rect(ax+k,ay+k,1,8-2*k,ac); }
        if(it==0||it==CM_NC||it==CM_NC+3){ int sy=iy+CM_IH+3; fill_rect(x+29,sy,w-34,1,rgb(128,128,128)); fill_rect(x+29,sy+1,w-34,1,rgb(255,255,255)); } }
    if(hovit>=1&&hovit<=CM_NC)g_csub=hovit-1;
    else if(hovit>=0)g_csub=-1;
    else if(g_csub>=0){ int sx,sy,sw,sh; cm_sub_geom(g_csub,&sx,&sy,&sw,&sh); if(!(mx>=sx&&mx<sx+sw&&my>=sy&&my<sy+sh)&&!(mx>=x&&mx<x+w&&my>=y&&my<y+h))g_csub=-1; }
    if(g_csub>=0){ int sx,sy,sw,sh; cm_sub_geom(g_csub,&sx,&sy,&sw,&sh); bevel(sx,sy,sw,sh,0); fill_rect(sx+3,sy+3,sw-6,sh-6,rgb(255,255,255));
        int cols=cm_cols(g_csub), n=g_lgn[g_lvis[g_csub]], rows=(n+cols-1)/cols;
        for(int k=0;k<n;k++){ int app=cm_sub_app(g_csub,k), ix=sx+(k/rows)*200, iy=sy+3+(k%rows)*CM_IH; int hov=(mx>=ix+3&&mx<ix+197&&my>=iy&&my<iy+CM_IH);
            if(hov)fill_rect(ix+3,iy,194,CM_IH,rgb(210,192,182)); if(app>=1000){ if(!app_icon_draw(app,ix+8,iy+4,16)){ fill_rect(ix+8,iy+4,16,16,rgb(55,118,171)); fill_rect(ix+12,iy+8,8,8,rgb(255,214,70)); } dstr(ix+32,iy+5,lapp_name(app),rgb(0,0,0),1); continue; } mini_icon(app,ix+8,iy+4); dstr(ix+32,iy+5,APPNAME[app],rgb(0,0,0),1); } }
}
static int classic_menu_click(int px,int py){ int x,y,w,h; cm_geom(&x,&y,&w,&h);
    lgrp_refresh(); if(g_csub>=g_nvis)g_csub=-1;
    if(g_csub>=0){ int sx,sy,sw,sh; cm_sub_geom(g_csub,&sx,&sy,&sw,&sh); if(px>=sx&&px<sx+sw&&py>=sy&&py<sy+sh){ int cols=cm_cols(g_csub), n=g_lgn[g_lvis[g_csub]], rows=(n+cols-1)/cols; int r=(py-sy-3)/CM_IH, cc=(px-sx)/200; int k=cc*rows+r; if(r>=0&&r<rows&&cc>=0&&cc<cols&&k<n){ int app=cm_sub_app(g_csub,k); g_csub=-1; lapp_open(app); } return 1; } }
    if(px>=x&&px<x+w&&py>=y&&py<y+h){ for(int it=0;it<CM_NC+5;it++){ int iy=cm_item_y(it,y); if(py>=iy&&py<iy+CM_IH){
            if(it==0){ g_launcher=0; open_win(W_ABOUT); } else if(it<=CM_NC){ g_csub=it-1; } else if(it==CM_NC+1){ g_launcher=0; open_win(W_SETTINGS); } else if(it==CM_NC+2){ g_launcher=0; open_win(W_ABOUT); } else if(it==CM_NC+3){ g_launcher=0; open_win(W_TERM); } else { g_launcher=0; launcher_power(0); } return 1; } } return 1; }
    g_launcher=0; g_csub=-1; return 1; }
static void launcher_draw(void){ if(g_dstyle==1){ classic_menu_draw(); return; } if(!g_gm_ready)gmetrics(); int px,py,pw,ph; launcher_geom(&px,&py,&pw,&ph);
    u32 tx=rgb(240,242,250), sub=rgb(178,182,206), ic=rgb(206,210,230), dark=rgb(44,34,78);
    shadow_soft(px,py,pw,ph,18); glass(px,py,pw,ph,18,rgb(46,34,84),252);
    glass(px,py,LB_SIDE,ph,18,rgb(22,16,44),200); glass(px+LB_SIDE-18,py,18,ph,0,rgb(22,16,44),200);
    fill_rect(px+LB_SIDE,py+14,1,ph-28,rgb(84,72,124));
    int cx=px+LB_SIDE/2;
    fill_circle(cx,py+36,19,mix(ACC,rgb(255,255,255),70)); fill_circle(cx,py+36,17,ACC); pdstr_ctr(cx,py+28,"N",rgb(255,255,255),2);
    for(int k=0;k<6;k++){ int cyk=py+96+k*52; if(lb_hit(px+10,cyk-20,LB_SIDE-20,40)) glass(px+10,cyk-20,LB_SIDE-20,40,10,rgb(255,255,255),36); lside_icon(k,cx,cyk,ic); }
    { int cyk=py+ph-96; if(lb_hit(px+10,cyk-20,LB_SIDE-20,40)) glass(px+10,cyk-20,LB_SIDE-20,40,10,rgb(255,255,255),36);
      fill_circle(cx,cyk,8,ic); fill_circle(cx,cyk,4,dark); for(int k=0;k<8;k++){ int dx[8]={0,6,9,6,0,-6,-9,-6},dy[8]={-9,-6,0,6,9,6,0,-6}; fill_circle(cx+dx[k],cyk+dy[k],2,ic); } }
    { int cyk=py+ph-44; if(lb_hit(px+10,cyk-20,LB_SIDE-20,40)) glass(px+10,cyk-20,LB_SIDE-20,40,10,rgb(255,255,255),36);
      fill_circle(cx,cyk,9,ic); fill_circle(cx,cyk,7,rgb(50,38,88)); fill_rect(cx-4,cyk-11,8,7,rgb(50,38,88)); fill_rect(cx-1,cyk-11,2,9,ic); }
    int lx=px+LB_SIDE+16, lw=pw-LB_SIDE-32;
    { int sby=py+18, sbh=38, sbw=lw-48; glass(lx,sby,sbw,sbh,10,rgb(255,255,255),26);
      int scx=g_lq[0]?lx+14:lx+sbw/2-34;
      fill_circle(scx+6,sby+18,6,ic); fill_circle(scx+6,sby+18,4,rgb(66,50,110)); fill_rect(scx+10,sby+22,5,2,ic);
      if(g_lq[0]) pdstr(scx+24,sby+12,g_lq,tx,1); else pdstr(scx+24,sby+12,"Search",sub,1);
      if(g_lq[0]&&((g_ticks/30)&1)){ int cxp=scx+24+g_lqn*8; fill_rect(cxp,sby+10,2,18,tx); }
      int ebx=lx+lw-38; if(lb_hit(ebx,sby,38,sbh)) glass(ebx,sby,38,sbh,10,rgb(255,255,255),30);
      int ex=ebx+12, ey=sby+12; fill_rect(ex+9,ey,6,2,ic); fill_rect(ex+13,ey,2,6,ic); fill_rect(ex,ey+12,6,2,ic); fill_rect(ex,ey+8,2,6,ic); for(int k=0;k<5;k++){ putpx(ex+12-k,ey+2+k,ic); putpx(ex+2+k,ey+12-k,ic); } }
    int ry=py+LB_TOP;
    if(!g_lq[0]){
        int hv=-1;
        for(int v=0;v<g_nvis;v++){ int g=g_lvis[v], gy=ry+v*LR_H; if(gy+LR_H>py+ph-8)break; int hov=lb_hit(lx-8,gy,lw+8,LR_H-4); if(hov)hv=v;
            if(hov||g_lsub==v) glass(lx-8,gy,lw+8,LR_H-4,10,g_lsub==v?ACC:rgb(255,255,255),g_lsub==v?200:34);
            u32 c=LG_COL[g]; fill_round(lx,gy+6,32,32,8,15,c); fill_round(lx+3,gy+9,12,12,3,15,mix(c,rgb(255,255,255),120)); fill_round(lx+17,gy+9,12,12,3,15,mix(c,rgb(255,255,255),90)); fill_round(lx+3,gy+23,12,12,3,15,mix(c,rgb(255,255,255),90)); fill_round(lx+17,gy+23,12,12,3,15,mix(c,rgb(255,255,255),60));
            pdstr(lx+46,gy+6,LG_NAME[g],tx,1); char t[24], *q=t; q=udec(q,(u32)g_lgn[g]); q=scopy(q,g_lgn[g]==1?" app":" apps"); *q=0; pdstr(lx+46,gy+23,t,sub,1);
            int ax=lx+lw-16, ay=gy+14; for(int k=0;k<5;k++) fill_rect(ax+k,ay+k,1,12-2*k,sub); }
        if(hv>=0)g_lsub=hv;
        else if(g_lsub>=0){ int sx,sy,sw,sh; lsub_geom(g_lsub,&sx,&sy,&sw,&sh); if(!lb_hit(sx,sy,sw,sh)&&!lb_hit(px,py,pw,ph))g_lsub=-1; }
        if(g_lsub>=0){ int sx,sy,sw,sh; lsub_geom(g_lsub,&sx,&sy,&sw,&sh); int g=g_lvis[g_lsub], n=g_lgn[g], cw=92;
            shadow_soft(sx,sy,sw,sh,16); glass(sx,sy,sw,sh,16,rgb(46,34,84),252);
            pdstr(sx+14,sy+10,LG_NAME[g],tx,1); fill_rect(sx+pw_str(LG_NAME[g],1)+24,sy+17,sw-pw_str(LG_NAME[g],1)-38,1,rgb(84,72,124));
            for(int k=0;k<n;k++){ int cxk=sx+12+(k%LB_COLS)*cw, cyk=sy+LB_HDR+(k/LB_COLS)*LB_CELLH; if(cyk+LB_CELLH>sy+sh)break; int app=LGRP[g][k];
                if(lb_hit(cxk+2,cyk,cw-4,LB_CELLH-6)) glass(cxk+2,cyk,cw-4,LB_CELLH-6,12,rgb(255,255,255),34);
                if(app>1000&&app!=APP_PYIDE){ if(!app_icon_draw(app,cxk+cw/2-24,cyk+2,48)) launch_icon(W_EDITOR,cxk+cw/2-24,cyk+2); } else launch_icon(app==APP_PYIDE||app==1001?W_EDITOR:app,cxk+cw/2-24,cyk+2);
                if(app==APP_PYIDE){ fill_round(cxk+cw/2+6,cyk+30,22,16,4,15,rgb(55,118,171)); pdstr(cxk+cw/2+9,cyk+31,"py",rgb(255,214,70),1); }
                const char* nm=lapp_name(app); char t[20]; int tn=0; while(nm[tn]&&tn<19){ t[tn]=nm[tn]; tn++; } t[tn]=0; while(tn>3&&pw_str(t,1)>cw-8){ t[--tn]=0; t[tn-1]='.'; }
                pdstr_ctr(cxk+cw/2,cyk+56,t,tx,1); } }
    } else {
        for(int k=0;k<g_lres_n && ry+LB_ROW<py+ph-12;k++){ int hov=lb_hit(lx-8,ry,lw+8,LB_ROW-4);
            if(hov) glass(lx-8,ry,lw+8,LB_ROW-4,10,ACC,200);
            int kind=g_lres_kind[k],arg=g_lres_arg[k];
            if(kind==0){ launch_icon(arg,lx-6,ry-3); pdstr(lx+50,ry+7,APPNAME[arg],tx,1); pdstr(lx+50,ry+24,"Application",sub,1); }
            else if(kind==1){ ic_gear(lx-6,ry-3); pdstr(lx+50,ry+7,SET_PAGES[arg]+10,tx,1); pdstr(lx+50,ry+24,"Settings",sub,1); }
            else if(kind==3){ launch_icon(W_EDITOR,lx-6,ry-3); pdstr(lx+50,ry+7,"Python IDE",tx,1); pdstr(lx+50,ry+24,"Application (python3-ide)",sub,1); }
            else if(kind==4){ if(!app_icon_draw(1001,lx-6,ry-3,40)) launch_icon(W_EDITOR,lx-6,ry-3); pdstr(lx+50,ry+7,"IDLE (Python 3.13)",tx,1); pdstr(lx+50,ry+24,"Application (X11, Tkinter)",sub,1); }
            else if(kind==5){ if(!app_icon_draw(arg,lx-6,ry-3,40)) launch_icon(W_EDITOR,lx-6,ry-3); pdstr(lx+50,ry+7,app_name(arg),tx,1); pdstr(lx+50,ry+24,"Application (installed package)",sub,1); }
            else { ic_edit(lx-6,ry-3); pdstr(lx+50,ry+7,FS[arg].name,tx,1); pdstr(lx+50,ry+24,"File",sub,1); }
            ry+=LB_ROW; }
        if(g_lres_n==0) pdstr(lx,py+LB_TOP+12,"No results",sub,1);
    }
}
int launcher_click(int px,int py){ if(g_dstyle==1)return classic_menu_click(px,py); int lx0,ly,lw0,lh; launcher_geom(&lx0,&ly,&lw0,&lh);
    if(!g_lq[0]&&g_lsub>=0){ int sx,sy,sw,sh; lsub_geom(g_lsub,&sx,&sy,&sw,&sh); if(px>=sx&&px<sx+sw&&py>=sy&&py<sy+sh){ int g=g_lvis[g_lsub], n=g_lgn[g], cw=92;
        for(int k=0;k<n;k++){ int cxk=sx+12+(k%LB_COLS)*cw, cyk=sy+LB_HDR+(k/LB_COLS)*LB_CELLH; if(cyk+LB_CELLH>sy+sh)break; if(px>=cxk+2&&px<cxk+cw-2&&py>=cyk&&py<cyk+LB_CELLH-6){ lapp_open(LGRP[g][k]); return 1; } }
        return 1; } }
    if(px<lx0||px>=lx0+lw0||py<ly||py>=ly+lh){ g_launcher=0; g_lsub=-1; return 1; }
    if(px<lx0+LB_SIDE){ for(int k=0;k<6;k++){ int cyk=ly+96+k*52; if(py>=cyk-20&&py<cyk+20){ g_launcher=0; open_win(LSIDE[k]); return 1; } }
        { int cyk=ly+lh-96; if(py>=cyk-20&&py<cyk+20){ g_launcher=0; open_win(W_SETTINGS); return 1; } }
        { int cyk=ly+lh-44; if(py>=cyk-20&&py<cyk+20){ g_launcher=0; launcher_power(0); return 1; } }
        return 1; }
    int lx=lx0+LB_SIDE+16, lw=lw0-LB_SIDE-32, ry=ly+LB_TOP;
    if(g_lq[0]){ for(int k=0;k<g_lres_n;k++){ if(py>=ry&&py<ry+LB_ROW-4&&px>=lx-8&&px<lx+lw){ launcher_open_result(k); return 1; } ry+=LB_ROW; } return 1; }
    for(int v=0;v<g_nvis;v++){ int gy=ry+v*LR_H; if(py>=gy&&py<gy+LR_H-4&&px>=lx-8&&px<lx+lw){ g_lsub=(g_lsub==v)?-1:v; return 1; } }
    return 1;
}
/* keystrokes while the launcher is open (search box) */
int launcher_key(char c){ if(!g_launcher)return 0;
    if(c=='\n'){ if(g_lq[0]&&g_lres_n>0) launcher_open_result(0); else g_launcher=0; return 1; }
    if(c=='\b'){ if(g_lqn>0){ g_lq[--g_lqn]=0; launcher_search(); } return 1; }
    if(c=='\x1b'){ g_launcher=0; g_lq[0]=0; g_lqn=0; return 1; }
    if(c>=32 && c<127 && g_lqn<38){ g_lq[g_lqn++]=c; g_lq[g_lqn]=0; launcher_search(); return 1; }
    return 1;
}
/* ---- alt-tab switcher ---- */
static void alttab_draw(void){ if(g_altn<=0)return; int cw=96, pw=g_altn*cw+24; if(pw>(int)W-40)pw=(int)W-40; int ph=120; int px=((int)W-pw)/2, py=((int)H-ph)/2;
    shadow_soft(px,py,pw,ph,14); fill_round(px,py,pw,ph,14,15,rgb(30,34,44));
    for(int k=0;k<g_altn;k++){ int cx=px+12+k*cw, cy=py+14; int sel=(k==g_altsel); if(sel) fill_round(cx,cy,cw-8,ph-28,10,15,mix(rgb(30,34,44),rgb(255,255,255),40));
        launch_icon(g_altlist[k], cx+(cw-8)/2-24, cy+8); const char*nm=APPNAME[g_altlist[k]]; char t[14]; int tn=0; for(int q=0;nm[q]&&tn<12;q++)t[tn++]=nm[q]; t[tn]=0; dstr_ctr(cx+(cw-8)/2, cy+62, t, sel?rgb(255,255,255):rgb(180,186,200),1); } }
/* ---- toast ---- */
static void toast_draw(void){ { extern int osd_draw(void); if(osd_draw())return; } if(!toast_t)return; u64 age=(u64)(g_ticks-toast_t); if(age>=300){toast_t=0;return;}
    int a=255; if(age>240) a=255-(int)(age-240)*255/60; int tw=slen(toast_msg)*8+40; int tx=((int)W-tw)/2, ty=(int)H-140;
    if(g_gfx) gfx_glass(tx,ty,tw,34,10,rgb(28,32,44),a*232/255,g_gfx_fx?1:0); else for(int j=0;j<34;j++)for(int i=0;i<tw;i++){ if(cut(i,j,tw,34,10,15))continue; int dx=tx+i,dy=ty+j; if((u32)dx<W&&(u32)dy<H){ u32*p=&SCENE[(u64)dy*SW+dx]; *p=mix(*p,rgb(28,32,44),a); } }
    dstr_ctr(tx+tw/2, ty+9, toast_msg, mix(rgb(28,32,44),rgb(240,244,252),a), 1); }
/* ---- top-bar popovers ---- */
 
static void clockpop_draw(void){ int pw=210, ph=210, px=(int)W-pw-12, py=g_tby-ph-10; shadow_soft(px,py,pw,ph,12); fill_round(px,py,pw,ph,12,15,rgb(250,251,254));
    int hh,mm,ss,yr,mo,dy; read_clock(&hh,&mm,&ss,&yr,&mo,&dy);
    static const char*MON[13]={"","January","February","March","April","May","June","July","August","September","October","November","December"};
    { char hd[24],*p=hd; { const char*mn=MON[mo>=1&&mo<=12?mo:1]; while(*mn)*p++=*mn++; } *p++=' '; p=udec(p,(u32)yr); *p=0; dstr_ctr(px+pw/2,py+10,hd,INK,1); }
    static const char*WD[7]={"M","T","W","T","F","S","S"}; int cw=(pw-20)/7, gx=px+10, gy=py+32;
    for(int c=0;c<7;c++) dstr_ctr(gx+cw*c+cw/2,gy,WD[c],rgb(150,158,176),1);
    int col0=(cal_dow(yr,mo,1)+6)%7, dim=cal_dim(yr,mo), yy=gy+22;
    for(int day=1,cell=col0; day<=dim; day++,cell++){ int c=cell%7, r=cell/7, cxp=gx+cw*c+cw/2, cyp=yy+r*24; int today=(day==dy);
        if(today) fill_circle(cxp,cyp+4,10,ACC); char b[3],*p=b; p=udec(p,(u32)day); *p=0; dstr_ctr(cxp,cyp,b,today?rgb(255,255,255):rgb(52,60,78),1); } }
static void qs_draw(void){ if(g_dstyle==1){ cqs_draw(); if(g_fpop)fpop_draw(); return; } int pw=250, ph=196, px=g_qsx-pw+30, py=g_tby-ph-10; if(px<6)px=6; shadow_soft(px,py,pw,ph,12); fill_round(px,py,pw,ph,12,15,rgb(250,251,254));
    dstr(px+16,py+12,"Wallpaper",rgb(90,100,120),1); static const char*THN2[3]={"Porsche","Crane","Wireframe"};
    for(int k=0;k<3;k++){ int bx=px+16+k*74, by=py+32; fill_round(bx,by,64,30,6,15,THSW[k]); if(k==g_theme&&g_wallmode==0){ for(int t=0;t<64;t++){putpx(bx+t,by,ACC);putpx(bx+t,by+29,ACC);} } dstr_ctr(bx+32,by+38,THN2[k],rgb(90,100,120),1); }
    dstr(px+16,py+82,"Accent",rgb(90,100,120),1);
    for(int k=0;k<5;k++){ int bx=px+16+k*44, by=py+102; if(ACV[k]==ACC){ fill_circle(bx+14,by+12,15,rgb(255,255,255)); } fill_circle(bx+14,by+12,12,ACV[k]); }
    { focus_mode* f=focus_get(g_focus_cur); int bx=px+16, by=py+146, bw=pw-32; int hov=in_rect(mx,my,bx,by,bw,36); fill_round(bx,by,bw,36,9,15,f?f->color:(hov?rgb(226,230,238):rgb(236,239,245)));
      u32 fg=f?rgb(255,255,255):INK; focus_glyph(bx+20,by+18,f?f->icon:0,fg); char t[40],*q=t; q=scopy(q,"Focus: "); q=scopy(q,f?f->name:"Off"); *q=0; dstr(bx+38,by+11,t,fg,1); for(int r=0;r<4;r++){ putpx(bx+bw-18+r,by+14+r,fg); putpx(bx+bw-18+r,by+21-r,fg); } }
    if(g_fpop)fpop_draw(); }
/* ---- rename modal ---- */
static void rename_draw(void){ int pw=360, ph=140, px=((int)W-pw)/2, py=(int)H/2-90;
    if(g_gfx) gq_blend(g_rt,0,0,(int)W,(int)H,rgb(10,12,20),110); else { (void)SCENE; for(u32 y=0;y<H;y++)for(u32 x=0;x<W;x++){ u32*p=&g_scene[(u64)y*SW+x]; *p=mix(*p,rgb(10,12,20),110); } }
    shadow_soft(px,py,pw,ph,14); fill_round(px,py,pw,ph,14,15,rgb(250,251,254));
    dstr(px+24,py+20,g_renmode==2?"New folder":g_renmode==3?"New file":g_renmode==1?"Rename folder":"Rename",INK,2);
    fill_round(px+24,py+56,pw-48,34,8,15,rgb(238,241,246)); int cx2=px+36; for(int k=0;k<g_renlen;k++){ dchar(cx2,py+66,g_renbuf[k],rgb(40,46,64),1); cx2+=8; } if((g_ticks/50)&1) fill_rect(cx2+1,py+65,2,15,ACC);
    dstr(px+24,py+ph-24,g_renmode>=2?"Enter = create    Esc = cancel":"Enter = rename    Esc = cancel",rgb(120,128,146),1); }
static void snap_preview_draw(void){ if(!g_snap)return; int wy=32, wh=(int)H-32-86, hw=((int)W-18)/2, hh=(wh-6)/2; int x=6,y=wy,w=hw,h=wh;
    if(g_snap==3){ x=6;y=wy;w=(int)W-12;h=wh; } else if(g_snap==1){ x=6;y=wy;w=hw;h=wh; } else if(g_snap==2){ x=(int)W-6-hw;y=wy;w=hw;h=wh; }
    else if(g_snap==4){ x=6;y=wy;w=hw;h=hh; } else if(g_snap==5){ x=(int)W-6-hw;y=wy;w=hw;h=hh; } else if(g_snap==6){ x=6;y=wy+hh+6;w=hw;h=hh; } else { x=(int)W-6-hw;y=wy+hh+6;w=hw;h=hh; }
    if(g_gfx){ gfx_round(x,y,w,h,12,15,rgb(120,170,250),90); fill_rect(x,y,w,1,rgb(150,190,255)); fill_rect(x,y+h-1,w,1,rgb(150,190,255)); fill_rect(x,y,1,h,rgb(150,190,255)); fill_rect(x+w-1,y,1,h,rgb(150,190,255)); return; }
    for(int j=0;j<h;j++)for(int i=0;i<w;i++){ if(cut(i,j,w,h,12,15))continue; int dx=x+i,dy=y+j; if((u32)dx<W&&(u32)dy<H){ u32*p=&SCENE[(u64)dy*SW+dx]; *p=mix(*p,rgb(120,170,250),90); } }
    for(int i=0;i<w;i++){ putpx(x+i,y,rgb(150,190,255)); putpx(x+i,y+h-1,rgb(150,190,255)); } for(int j=0;j<h;j++){ putpx(x,y+j,rgb(150,190,255)); putpx(x+w-1,y+j,rgb(150,190,255)); } }
#define ANIMW 768
#define ANIMH 600
static u32 WANIM[ANIMW*ANIMH];
int g_dmg_ok=0, g_dmx=0, g_dmy=0, g_dmw=0, g_dmh=0; u32 g_frames_skipped=0, g_frames_part=0;
static void quiet_on(void){ if(g_gfx)gfx_sync(); g_scene_quiet++; }
static void quiet_off(void){ if(g_gfx)gfx_sync(); if(g_scene_quiet>0)g_scene_quiet--; }
static void compose_build(void){
    if(g_gfx) gfx_frame_begin();
    if(g_gfx) gq_copy(gs_scene,0,0,(int)W,(int)H,gs_bg,0,0,256,0); else memcpy(SCENE,BG,(u64)W*H*4);
    if(g_gfx) gfx_wall_light();
    desk_draw();
    int order[NWIN],n=0; for(int i=0;i<NWIN;i++) if(WN[i].open&&!WN[i].min&&i!=anim_hide&&WN[i].ws==g_ws)order[n++]=i;
    for(int a=0;a<n;a++)for(int b=a+1;b<n;b++) if(WN[order[b]].z<WN[order[a]].z){int t=order[a];order[a]=order[b];order[b]=t;}
    if(drag>=0&&g_snap) snap_preview_draw();
    for(int a=0;a<n;a++){ if(g_gfx) gfx_window(order[a]); else draw_app(order[a]); }
    if(g_gfx) gfx_ghosts();
    marq_draw();
    draw_dock();
    { extern void xover_draw(void); xover_draw(); }
    if(g_launcher) launcher_draw(); if(g_qs) qs_draw(); if(g_clockpop) clockpop_draw();
    if(g_ctx) ctx_draw(); img_viewer_draw(); if(g_alttab) alttab_draw(); if(g_rename) rename_draw(); if(g_prop) prop_draw();
    if(g_wifipop) wifi_panel_draw(); if(g_notifpanel) notif_panel_draw(); toast_frame(); notif_toast_draw(); ws_overlay_draw(); toast_draw(); { extern void ss_draw(void); ss_draw(); } { extern void pm_draw(void); pm_draw(); } { extern void acct_dlg_draw(void); acct_dlg_draw(); }
    night_tint(0,(int)H);
    if(g_gfx) g_dmg_ok=gfx_scene_end(&g_dmx,&g_dmy,&g_dmw,&g_dmh); else g_dmg_ok=0;
}
static u64 fps_t0=0; static int fps_cnt=0, fps_val=0;
u32 SCENE_NC_STATIC[MAXW*MAXH]; u32* SCENE_NC=SCENE_NC_STATIC;
int cxlast=-1, cylast=-1;

static void blit_region_fb(int x,int y,int w,int h);
void blit_region(int x,int y,int w,int h){
    g_scene_quiet++; void blit_region2(int,int,int,int); blit_region2(x,y,w,h); g_scene_quiet--; }
void blit_region2(int x,int y,int w,int h){
    if(x<0){w+=x;x=0;} if(y<0){h+=y;y=0;} if(x+w>(int)W)w=(int)W-x; if(y+h>(int)H)h=(int)H-y; if(w<=0||h<=0)return;
    { extern int g_igpu_flip; extern u8* g_igpu_back; extern void igpu_wait_flip(void); if(g_igpu_flip){ igpu_wait_flip(); if(g_igpu_flip){ u8* f=FBB; blit_region_fb(x,y,w,h); FBB=g_igpu_back; blit_region_fb(x,y,w,h); FBB=f; return; } } }
    blit_region_fb(x,y,w,h); }
static void blit_region_fb(int x,int y,int w,int h){
    { extern void fb_shadow_note(int,int,int,int); fb_shadow_note(x,y,w,h); }
    if(BYPP==4&&g_fb_rgb){ for(int r=0;r<h;r++) copy_swap32((u32*)(FBB+(u64)(y+r)*PITCHB+(u64)x*4), SCENE+(u64)(y+r)*SW+x, w); }
    else if(BYPP==4){ for(int r=0;r<h;r++) sse_copy32((u32*)(FBB+(u64)(y+r)*PITCHB+(u64)x*4), SCENE+(u64)(y+r)*SW+x, w); }
    else { for(int r=0;r<h;r++) sse_pack24(FBB+(u64)(y+r)*PITCHB+(u64)x*3, SCENE+(u64)(y+r)*SW+x, w); }
}
int desk_popups_clear(void){
    if(drag>=0||g_resize>=0||g_fdrag||g_ddrag>=0||g_marq) return 0;
    if(g_launcher||g_qs||g_clockpop||g_ctx||g_submenu||g_imgview>=0||g_alttab||g_rename||g_prop||g_wifipop||g_notifpanel||g_toast_cur>=0||toast_t) return 0;
    return 1; }
