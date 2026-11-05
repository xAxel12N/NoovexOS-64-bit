void win_title_icon(int app,int x,int y){ mini_icon(app,x,y); }
void mini_icon_big(int app,int x,int y){ if(app>=0&&app<NWIN&&g_aicon[app]){ icon_choice_draw(x,y,32,g_aicon[app]-1); return; } u32 c=(app>=0&&app<NWIN)?APPCOL[app]:rgb(120,120,140); fill_rect(x,y,32,32,c); fill_rect(x,y,32,1,mix(c,rgb(255,255,255),90)); fill_rect(x+31,y,1,32,mix(c,rgb(0,0,0),90)); fill_rect(x,y+31,32,1,mix(c,rgb(0,0,0),90));
    const char* nm=(app>=0&&app<NWIN)?APPNAME[app]:"?"; char t[2]={nm[0],0}; int light=((c>>16&255)+(c>>8&255)+(c&255))>600; dstr(x+8,y+0,t,light?rgb(30,30,40):rgb(255,255,255),2); }
static void nv_logo_small(int x,int y){ fill_round(x,y,16,16,3,15,rgb(40,60,150)); fill_round(x+2,y+2,12,12,2,15,rgb(90,130,230)); dstr(x+4,y+1,"N",rgb(255,255,255),1); }
int g_tbtn_n=0, g_tbtn_x[16], g_tbtn_w[16], g_tbtn_app[16]; int g_tray_x0=0, g_airplane=0, g_nightlight=0, g_battsave=0, g_dnd=0;
int g_ql_x0=-1000, g_lbx_end_ql=0;
static void draw_classic_bar(void){
    g_tbh=30; g_tby=(int)H-g_tbh; DOCK_x=-20000; DOCK_w=0; DOCK_y=g_tby; DOCK_h=g_tbh;
    if(g_gfx&&g_gfx_fx) gfx_glass(0,g_tby,(int)W,g_tbh,0,C_FACE,212,2); else fill_rect(0,g_tby,(int)W,g_tbh,C_FACE); fill_rect(0,g_tby,(int)W,1,rgb(223,223,223)); fill_rect(0,g_tby+1,(int)W,1,rgb(255,255,255));
    int sb_w=96; bevel(3,g_tby+4,sb_w,g_tbh-7,g_launcher); nv_logo_small(9+(g_launcher?1:0),g_tby+7+(g_launcher?1:0));
    dstr(30+(g_launcher?1:0),g_tby+8+(g_launcher?1:0),"NoovexOS",rgb(0,0,0),1); dstr(31+(g_launcher?1:0),g_tby+8+(g_launcher?1:0),"NoovexOS",rgb(0,0,0),1);
    g_lbx_end=3+sb_w;
    { extern int g_ql_x0; g_ql_x0=g_lbx_end+6; fill_rect(g_ql_x0-3,g_tby+6,1,g_tbh-11,rgb(128,128,128)); fill_rect(g_ql_x0-2,g_tby+6,1,g_tbh-11,rgb(255,255,255));
      for(int q=0;q<3;q++){ int qx=g_ql_x0+q*24, qy=g_tby+7; int hv=(mx>=qx&&mx<qx+22&&my>=g_tby+4&&my<g_tby+g_tbh-3); if(hv) bevel(qx,g_tby+4,22,g_tbh-7,mouse_btn); int ox=qx+3, oy=qy;
        if(q==0){ fill_rect(ox+1,oy+3,6,2,rgb(150,98,60)); fill_rect(ox,oy+4,16,11,rgb(150,98,60)); fill_rect(ox+1,oy+5,14,9,rgb(222,176,128)); }
        else if(q==1){ fill_rect(ox,oy+1,16,13,rgb(128,128,128)); fill_rect(ox+1,oy+2,14,11,rgb(0,0,0)); fill_rect(ox+3,oy+5,2,1,rgb(0,220,0)); fill_rect(ox+5,oy+6,1,1,rgb(0,220,0)); fill_rect(ox+3,oy+7,2,1,rgb(0,220,0)); fill_rect(ox+7,oy+8,4,1,rgb(0,220,0)); }
        else { fill_circle(ox+8,oy+8,7,rgb(30,90,200)); fill_circle(ox+8,oy+8,6,rgb(70,150,235)); fill_rect(ox+2,oy+8,13,1,rgb(200,230,255)); fill_rect(ox+8,oy+2,1,13,rgb(200,230,255)); fill_rect(ox+4,oy+4,3,4,rgb(60,170,70)); } }
      g_lbx_end_ql=g_ql_x0+3*24+4; fill_rect(g_lbx_end_ql-3,g_tby+6,1,g_tbh-11,rgb(128,128,128)); fill_rect(g_lbx_end_ql-2,g_tby+6,1,g_tbh-11,rgb(255,255,255)); }
    int hh,mm,ss,yy2,mo,dd; read_clock(&hh,&mm,&ss,&yy2,&mo,&dd); (void)ss;(void)yy2;(void)mo;(void)dd; char t[8],*p=t; p=u2(p,hh);*p++=':';p=u2(p,mm);*p=0;
    int cw=56, tw2=5*22+6, bxw=tw2+cw; int bx0=(int)W-bxw-4; bevel(bx0,g_tby+4,bxw,g_tbh-7,1); fill_rect(bx0+2,g_tby+6,bxw-4,g_tbh-11,C_FACE);
    { extern int sysmon_cpu_pct(void), sysmon_mem_pct(void); static u8 hc[24], hm[24]; static u64 lt; static int hp; if(g_ticks-lt>=100||!lt){ lt=g_ticks; hc[hp]=(u8)sysmon_cpu_pct(); hm[hp]=(u8)sysmon_mem_pct(); hp=(hp+1)%24; }
      for(int g=0;g<2;g++){ int gx=bx0-60+g*29, gy=g_tby+6, gw=26, gh=g_tbh-11; fill_rect(gx,gy,gw,gh,rgb(0,0,0)); for(int q=0;q<24;q++){ int v=(g==0?hc:hm)[(hp+q)%24]; int bh=(gh-2)*v/100; if(v>0&&bh<1)bh=1; if(bh>0) fill_rect(gx+1+q,gy+gh-1-bh,1,bh,g==0?rgb(0,200,200):rgb(220,60,60)); } fill_rect(gx-1,gy-1,gw+2,1,rgb(128,128,128)); fill_rect(gx-1,gy-1,1,gh+2,rgb(128,128,128)); fill_rect(gx-1,gy+gh,gw+2,1,rgb(255,255,255)); fill_rect(gx+gw,gy-1,1,gh+2,rgb(255,255,255)); } }
    g_clockx=bx0+tw2; dstr_ctr(g_clockx+cw/2-2,g_tby+8,t,rgb(0,0,0),1);
    g_tray_x0=bx0+4; for(int k=0;k<5;k++){ int ix=g_tray_x0+k*22, cy=g_tby+15; u32 ic=rgb(0,0,0);
        if((k==1||k==2||k==3)&&g_qs) fill_rect(ix,g_tby+6,20,g_tbh-11,rgb(168,168,176));
        if(k==4&&g_notifpanel) fill_rect(ix,g_tby+6,20,g_tbh-11,rgb(168,168,176));
        if(k==0){ for(int r=0;r<4;r++) fill_rect(ix+10-r,cy-1+r,1+2*r,1,ic); }
        else if(k==1){ if(g_airplane){ fill_rect(ix+9,cy-6,2,12,ic); fill_rect(ix+4,cy-1,12,2,ic); fill_rect(ix+7,cy+4,6,2,ic); }
            else { for(int a=0;a<3;a++){ int rr=3+a*3; for(int dx=-rr;dx<=rr;dx++){ int dy2=rr*rr-dx*dx; int d=0; while((d+1)*(d+1)<=dy2)d++; if(dx>=-d&&dx<=d) putpx(ix+10+dx,cy+4-d,(a==2&&g_apn==0)?rgb(128,128,128):ic); } } fill_rect(ix+9,cy+3,3,3,ic); } }
        else if(k==2){ fill_rect(ix+4,cy-2,4,5,ic); for(int q=0;q<5;q++) fill_rect(ix+8+q,cy-2-q,1,5+2*q,ic); if(g_snd_vol==0){ for(int q=0;q<5;q++){ putpx(ix+14+q,cy-2+q,ic); putpx(ix+18-q,cy-2+q,ic); } } else { fill_rect(ix+15,cy-1,1,3,ic); if(g_snd_vol>50) fill_rect(ix+17,cy-3,1,7,ic); } }
        else if(k==3){ if(bat_present){ fill_rect(ix+3,cy-4,14,9,ic); fill_rect(ix+4,cy-3,12,7,C_FACE); fill_rect(ix+17,cy-2,2,5,ic); int fw=bat_pct*10/100; fill_rect(ix+5,cy-2,fw,5,bat_pct<=15?rgb(200,0,0):rgb(0,128,0)); }
            else { fill_rect(ix+6,cy-6,2,4,ic); fill_rect(ix+12,cy-6,2,4,ic); fill_rect(ix+4,cy-2,12,5,ic); fill_rect(ix+9,cy+3,2,4,ic); } }
        else { fill_round(ix+6,cy-6,8,10,3,15,ic); fill_rect(ix+4,cy+3,12,2,ic); fill_rect(ix+8,cy+5,4,2,ic); if(g_notif_n>0&&!g_dnd) fill_circle(ix+15,cy-5,3,rgb(220,0,0)); { focus_mode* f=focus_get(g_focus_cur); if(f&&f->show_tb){ fill_rect(ix,g_tby+6,20,g_tbh-11,C_FACE); fill_circle(ix+10,cy,8,f->color); focus_glyph(ix+10,cy,f->icon,rgb(255,255,255)); } } } }
    g_bellx=(int)W+100; g_qsx=-1000; g_wifix=(int)W-24; g_wsx=-1000; g_tray_left=bx0;
    { extern int dl_active(void); extern int dl_progress(char*,int); if(dl_active()){ char nm[48]; int pc=dl_progress(nm,sizeof nm); int pw2=150, px=bx0-64-pw2, py=g_tby+4, ph=g_tbh-7;
        bevel(px,py,pw2,ph,1); fill_rect(px+2,py+2,pw2-4,ph-4,C_FACE); dstr(px+6,py+3,nm,rgb(0,0,0),1);
        int bw2=pw2-12; fill_rect(px+6,py+ph-8,bw2,4,rgb(128,128,128)); if(pc>=0) fill_rect(px+6,py+ph-8,pc*bw2/100,4,rgb(0,0,128)); else { int t=(int)(g_ticks/2%(u64)(bw2-30)); fill_rect(px+6+t,py+ph-8,30,4,rgb(0,0,128)); }
        g_tray_left=px; } }
    g_tbtn_n=0; int bx=g_lbx_end_ql+4, avail=g_tray_left-64-6-bx; int n=0; for(int i=0;i<NWIN;i++) if(WN[i].open&&WN[i].ws==g_ws)n++;
    int bw=n?avail/n:160; if(bw>160)bw=160; if(bw<40)bw=40;
    for(int i=0;i<NWIN&&g_tbtn_n<16;i++){ if(!WN[i].open||WN[i].ws!=g_ws)continue; if(bx+bw>g_tray_left-64)break; int act=(focus==i&&!WN[i].min);
        bevel(bx,g_tby+4,bw-3,g_tbh-7,act); if(act)fill_rect(bx+2,g_tby+6,bw-7,g_tbh-11,rgb(227,223,219));
        mini_icon(i,bx+5+act,g_tby+7+act); char tt[24]; int k=0; const char* nm=APPNAME[i]; int maxc=(bw-34)/8; if(maxc>22)maxc=22; while(nm[k]&&k<maxc){ tt[k]=nm[k]; k++; } tt[k]=0;
        dstr(bx+26+act,g_tby+8+act,tt,rgb(0,0,0),1); { extern int g_web_badge; if(i==W_BROWSER&&g_web_badge>0){ char bd[8],*q=bd; q=udec(q,(u32)(g_web_badge>99?99:g_web_badge)); *q=0; int bwx=bx+bw-24; fill_circle(bwx+8,g_tby+12,8,rgb(210,30,30)); dstr_ctr(bwx+8,g_tby+5,bd,rgb(255,255,255),1); } } g_tbtn_x[g_tbtn_n]=bx; g_tbtn_w[g_tbtn_n]=bw-3; g_tbtn_app[g_tbtn_n]=i; g_tbtn_n++; bx+=bw; }
}
void focus_glyph(int cx,int cy,int k,u32 c){
    if(k==0){ for(int j=-7;j<=7;j++)for(int i=-7;i<=7;i++){ int a=i*i+j*j, b=(i-3)*(i-3)+(j+3)*(j+3); if(a<=42&&b>30)putpx(cx+i,cy+j,c); } }
    else if(k==1){ fill_rect(cx-8,cy+1,16,4,c); fill_rect(cx-8,cy-4,2,11,c); fill_rect(cx+6,cy+2,2,5,c); fill_round(cx-5,cy-3,5,4,1,15,c); }
    else if(k==2){ fill_round(cx-8,cy-3,16,10,2,15,c); fill_rect(cx-3,cy-6,6,2,c); fill_rect(cx-3,cy-6,1,4,c); fill_rect(cx+2,cy-6,1,4,c); }
    else if(k==3){ fill_round(cx-9,cy-4,18,10,4,15,c); fill_rect(cx-6,cy,5,1,0xFFFFFF); fill_rect(cx-4,cy-2,1,5,0xFFFFFF); fill_circle(cx+4,cy,1,0xFFFFFF); fill_circle(cx+6,cy-2,1,0xFFFFFF); }
    else if(k==4){ for(int j=-7;j<=7;j++)for(int i=-7;i<=7;i++){ int ai=i<0?-i:i, aj=j<0?-j:j; if(ai+aj<=4||(aj<=1&&ai<=7)||(ai<=1&&j>=-7&&j<=2)||(j>0&&ai<=7-j&&ai>=j-1&&j<=6))putpx(cx+i,cy+j,c); } }
    else if(k==5){ fill_circle(cx-3,cy-2,4,c); fill_circle(cx+3,cy-2,4,c); for(int r=0;r<7;r++)fill_rect(cx-7+r,cy+r,15-2*r,1,c); }
    else if(k==6){ fill_rect(cx-8,cy-6,7,12,c); fill_rect(cx+1,cy-6,7,12,c); fill_rect(cx-6,cy-3,3,1,0xFFFFFF); fill_rect(cx+3,cy-3,3,1,0xFFFFFF); fill_rect(cx-6,cy,3,1,0xFFFFFF); fill_rect(cx+3,cy,3,1,0xFFFFFF); }
    else if(k==7){ for(int j=-7;j<=5;j++)for(int i=-6;i<=6;i++){ int u=i-j, v=i+j; if(u*u*2+v*v<=60)putpx(cx+i,cy+j,c); } for(int t=0;t<5;t++)putpx(cx-4-t,cy+4+t,c); }
    else if(k==8){ fill_round(cx-9,cy-1,18,6,2,15,c); fill_round(cx-5,cy-5,10,5,2,15,c); fill_circle(cx-5,cy+5,2,c); fill_circle(cx+5,cy+5,2,c); }
    else { fill_circle(cx-3,cy+4,3,c); fill_rect(cx-1,cy-7,2,11,c); fill_rect(cx+1,cy-7,5,2,c); fill_rect(cx+4,cy-6,2,3,c); } }
int g_fpop=0; u64 g_fpop_closed=0;
static void fpop_geom(int*x,int*y,int*w,int*h){ int n=focus_count(); *w=250; *h=12+(n+2+(g_focus_cur>=0))*32; if(g_dstyle==1){ int px,py,pw,ph; px=(int)W-360-4; py=g_tby-290-2; pw=360; ph=290; (void)ph; *x=px+pw-*w; *y=py-*h-6; }
    else { *x=g_qsx-*w+30; if(*x<6)*x=6; *y=g_tby-196-10-*h-8; } if(*y<4)*y=4; }
static int fpop_row(int r){ int n=focus_count(); int off=g_focus_cur>=0; if(off&&r==0)return -1; r-=off; if(r<n)return r; return r==n?1000:1001; }
static void fpop_draw(void){ int x,y,w,h; fpop_geom(&x,&y,&w,&h); int n=focus_count(), rows=n+2+(g_focus_cur>=0);
    if(g_dstyle==1){ g_bevel_glass=1; bevel(x,y,w,h,0); g_bevel_glass=0; } else { shadow_soft(x,y,w,h,12); fill_round(x,y,w,h,12,15,rgb(250,251,254)); }
    for(int r=0;r<rows;r++){ int ry=y+6+r*32; int k=fpop_row(r); int hov=in_rect(mx,my,x+4,ry,w-8,30); u32 fg=g_dstyle==1?rgb(0,0,0):INK;
        if(hov){ if(g_dstyle==1)fill_rect(x+4,ry,w-8,30,rgb(0,0,128)); else fill_round(x+4,ry,w-8,30,7,15,mix(ACC,rgb(255,255,255),200)); if(g_dstyle==1)fg=rgb(255,255,255); }
        if(k==-1){ fill_circle(x+22,ry+15,7,rgb(150,150,160)); fill_rect(x+18,ry+14,9,2,rgb(255,255,255)); dstr(x+40,ry+8,"Turn focus off",fg,1); }
        else if(k==1000){ fill_rect(x+16,ry+14,13,2,fg); fill_rect(x+21,ry+9,2,13,fg); dstr(x+40,ry+8,"Create focus",fg,1); }
        else if(k==1001){ fill_circle(x+22,ry+15,6,fg); fill_circle(x+22,ry+15,3,hov&&g_dstyle==1?rgb(0,0,128):g_dstyle==1?C_FACE:rgb(250,251,254)); dstr(x+40,ry+8,"Focus settings",fg,1); }
        else { focus_mode* f=focus_get(k); fill_circle(x+22,ry+15,11,f->color); focus_glyph(x+22,ry+15,f->icon,rgb(255,255,255)); dstr(x+40,ry+8,f->name,fg,1);
            if(k==g_focus_cur){ for(int t=0;t<5;t++){ putpx(x+w-28+t,ry+13+t,fg); putpx(x+w-28+t,ry+14+t,fg); } for(int t=0;t<9;t++){ putpx(x+w-23+t,ry+17-t,fg); putpx(x+w-23+t,ry+18-t,fg); } } } } }
static int fpop_click(int px,int py){ int x,y,w,h; fpop_geom(&x,&y,&w,&h); if(!in_rect(px,py,x,y,w,h)){ g_fpop=0; g_fpop_closed=g_ticks+1; return 0; }
    if(py<y+6||(py-y-6)%32>=30)return 1; int r=(py-y-6)/32; int rows=focus_count()+2+(g_focus_cur>=0); if(r<0||r>=rows)return 1; int k=fpop_row(r);
    if(k==-1)focus_set(-1,0); else if(k==1000){ g_fpop=0; g_qs=0; { extern int g_stpg; g_stpg=120; } { extern void settings_clear_search(void); settings_clear_search(); } extern int g_focus_newmode; g_focus_newmode=1; open_win(W_SETTINGS); return 1; }
    else if(k==1001){ g_fpop=0; g_qs=0; { extern int g_stpg; g_stpg=120; } open_win(W_SETTINGS); return 1; } else focus_set(k==g_focus_cur?-1:k,0);
    g_fpop=0; return 1; }
int focus_popup_click(int px,int py){ if(!g_fpop)return 0; return fpop_click(px,py); }
static void cqs_geom(int*x,int*y,int*w,int*h){ *w=360; *h=290; *x=(int)W-*w-4; *y=g_tby-*h-2; }
static void cqs_tile(int k,int*x,int*y,int*w,int*h){ int px,py,pw,ph; cqs_geom(&px,&py,&pw,&ph); *w=(pw-16-16)/3; *h=60; *x=px+8+(k%3)*(*w+8); *y=py+10+(k/3)*(*h+8); }
static void cqs_slider_geom(int k,int*x,int*y,int*w){ int px,py,pw,ph; cqs_geom(&px,&py,&pw,&ph); *x=px+44; *y=py+164+k*38; *w=pw-44-18; }
static const char* CQS_N[6]={"Wi-Fi","Airplane","Night light","Battery saver","Focus","All settings"};
static int cqs_on(int k){ return k==0?(g_wm_state!=WM_OFF&&!g_airplane):k==1?g_airplane:k==2?g_nightlight:k==3?g_battsave:k==4?(g_focus_cur>=0):0; }
static void cqs_draw(void){ int px,py,pw,ph; cqs_geom(&px,&py,&pw,&ph); g_bevel_glass=1; bevel(px,py,pw,ph,0); g_bevel_glass=0;
    for(int k=0;k<6;k++){ int x,y,w,h; cqs_tile(k,&x,&y,&w,&h); int on=cqs_on(k); bevel(x,y,w,h,on); if(on) fill_rect(x+2,y+2,w-4,h-4,rgb(132,53,26));
        u32 fc=on?rgb(255,255,255):rgb(0,0,0); int cx=x+w/2, cy=y+18;
        if(k==0){ for(int a=0;a<3;a++){ int rr=3+a*3; for(int dx=-rr;dx<=rr;dx++){ int dy2=rr*rr-dx*dx; int d=0; while((d+1)*(d+1)<=dy2)d++; putpx(cx+dx,cy+5-d,fc); } } fill_rect(cx-1,cy+4,3,3,fc); }
        else if(k==1){ fill_rect(cx-1,cy-7,2,14,fc); fill_rect(cx-7,cy-1,14,2,fc); fill_rect(cx-4,cy+5,8,2,fc); }
        else if(k==2){ fill_circle(cx,cy,7,fc); fill_circle(cx+3,cy-3,6,on?rgb(132,53,26):C_FACE); }
        else if(k==3){ fill_rect(cx-8,cy-5,16,10,fc); fill_rect(cx-7,cy-4,14,8,on?rgb(132,53,26):C_FACE); fill_rect(cx+8,cy-2,2,4,fc); fill_rect(cx-5,cy-2,5,4,rgb(0,160,0)); }
        else if(k==4){ focus_mode* f=focus_get(g_focus_cur); focus_glyph(cx,cy,f?f->icon:0,fc); }
        else { fill_circle(cx,cy,6,fc); fill_circle(cx,cy,3,on?rgb(132,53,26):C_FACE); for(int q=0;q<8;q++){ int dx[8]={0,5,7,5,0,-5,-7,-5},dy[8]={-7,-5,0,5,7,5,0,-5}; fill_rect(cx+dx[q]-1,cy+dy[q]-1,2,2,fc); } }
        if(!g_gm_ready)gmetrics(); { const char* lb=CQS_N[k]; if(k==4&&g_focus_cur>=0)lb=focus_get(g_focus_cur)->name; pdstr_ctr(cx,y+38,lb,fc,1); } }
    for(int k=0;k<2;k++){ int x,y,w; cqs_slider_geom(k,&x,&y,&w); int v=k==0?(g_bl.ok?(g_bl.cur<0?0:g_bl.cur):-1):g_snd_vol; u32 ic=v<0?rgb(128,128,128):rgb(0,0,0);
        if(k==0){ fill_circle(x-22,y+6,4,ic); for(int q=0;q<8;q++){ int dx[8]={0,6,8,6,0,-6,-8,-6},dy[8]={-8,-6,0,6,8,6,0,-6}; putpx(x-22+dx[q],y+6+dy[q],ic); } }
        else { fill_rect(x-28,y+3,4,6,ic); for(int q=0;q<5;q++) fill_rect(x-24+q,y+3-q,1,6+2*q,ic); }
        if(v>=0){ bevel(x,y+3,w,6,1); int kx=x+v*(w-11)/100; bevel(kx,y-4,11,20,0); } else dstr(x,y-1,k==0?"Brightness: laptop screens only":"",rgb(128,128,128),1); }
    fill_rect(px+6,py+ph-38,pw-12,1,rgb(128,128,128)); fill_rect(px+6,py+ph-37,pw-12,1,rgb(255,255,255));
    { char b[32],*p=b; if(bat_present){ p=udec(p,(u32)bat_pct); p=scopy(p,bat_charging?"%  charging":"%"); } else p=scopy(p,"Plugged in"); *p=0; dstr(px+12,py+ph-28,b,rgb(0,0,0),1); }
    { int gx=px+pw-34, gy=py+ph-32; bevel(gx,gy,26,24,0); fill_circle(gx+13,gy+12,5,rgb(0,0,0)); fill_circle(gx+13,gy+12,2,C_FACE); for(int q=0;q<8;q++){ int dx[8]={0,5,7,5,0,-5,-7,-5},dy[8]={-7,-5,0,5,7,5,0,-5}; fill_rect(gx+13+dx[q]-1,gy+12+dy[q]-1,2,2,rgb(0,0,0)); } }
}
int classic_qs_click(int mx0,int my0){ int px,py,pw,ph; cqs_geom(&px,&py,&pw,&ph); if(mx0<px||mx0>=px+pw||my0<py||my0>=py+ph) return 0;
    for(int k=0;k<6;k++){ int x,y,w,h; cqs_tile(k,&x,&y,&w,&h); if(mx0>=x&&mx0<x+w&&my0>=y&&my0<y+h){
        if(k==0){ g_qs=0; g_fpop=0; g_wifipop=1; if(g_apn==0||g_wm_state!=WM_CONNECTED)wifi_gui_scan(0); }
        else if(k==1){ g_airplane^=1; }
        else if(k==2){ g_nightlight^=1; }
        else if(k==3){ g_battsave^=1; g_refresh_hz=g_battsave?30:60; }
        else if(k==4){ if(g_ticks>g_fpop_closed)g_fpop^=1; }
        else { g_qs=0; open_win(W_SETTINGS); }
        return 1; } }
    for(int k=0;k<2;k++){ int x,y,w; cqs_slider_geom(k,&x,&y,&w); if(my0>=y-6&&my0<y+18&&mx0>=x-4&&mx0<x+w+4){ int v=(mx0-x)*100/(w>0?w:1); if(v<0)v=0; if(v>100)v=100;
        if(k==0){ if(g_bl.ok)bl_set(v); } else { g_snd_vol=v; if(g_snd_state)g_snd.volume(v); } return 1; } }
    { int gx=px+pw-34, gy=py+ph-32; if(mx0>=gx&&mx0<gx+26&&my0>=gy&&my0<gy+24){ g_qs=0; open_win(W_SETTINGS); } }
    return 1; }
int classic_tray_click(int px){ if(px<g_tray_x0||px>=g_tray_x0+5*22) return 0; int k=(px-g_tray_x0)/22;
    if(k==4){ g_notifpanel^=1; g_qs=0; g_fpop=0; } else { g_qs^=1; g_fpop=0; g_notifpanel=0; g_clockpop=0; } return 1; }
static void night_tint(int y0,int y1){ if(!g_nightlight)return; if(g_gfx){ gq_tint(g_rt,0,y0,(int)W,y1-y0); return; } for(int y=y0;y<y1;y++){ u32* r=&SCENE[(u64)y*SW]; for(u32 x=0;x<W;x++){ u32 c=r[x]; u32 g=(c>>8)&255, b=c&255; r[x]=(c&0xFF0000)|((g*15/16)<<8)|(b*5/8); } } }
void classic_taskbtn_click(int px){ for(int k=0;k<g_tbtn_n;k++) if(px>=g_tbtn_x[k]&&px<g_tbtn_x[k]+g_tbtn_w[k]){ int i=g_tbtn_app[k]; if(focus==i&&!WN[i].min)win_min(i); else focus_win(i); return; } }
static void draw_dock(void){
    if(g_dstyle==1){ draw_classic_bar(); return; }
    int n=12; g_tbh=64; g_tby=(int)H-g_tbh-12; DOCK_h=g_tbh; DOCK_y=g_tby; DOCK_w=n*DOCK_IC+(n-1)*DOCK_GAP+2*DOCK_PAD;
    int bx0=12, bw0=(int)W-24, cy=g_tby+g_tbh/2;
    shadow_soft(bx0,g_tby,bw0,g_tbh,18); glass(bx0,g_tby,bw0,g_tbh,18,rgb(28,26,46),205);
    for(int i=18;i<bw0-18;i++){ u32*p=&SCENE[(u64)g_tby*SW+bx0+i]; *p=mix(*p,rgb(255,255,255),46); }
    { int lx=bx0+12;
      tb_btn(lx,cy-22,44,44,g_launcher); ic_launcher(lx+22,cy);
      int wx=lx+50; tb_btn(wx,cy-22,44,44,0); fill_round(wx+9,cy-12,26,24,6,15,rgb(46,190,150)); fill_rect(wx+14,cy-5,16,2,rgb(255,255,255)); fill_rect(wx+14,cy+1,11,2,rgb(255,255,255));
      int m2=wx+50; tb_btn(m2,cy-22,44,44,0); fill_round(m2+9,cy-12,26,24,6,15,rgb(244,120,90)); for(int k=0;k<4;k++){ int qx=m2+13+(k&1)*10, qy=cy-8+(k>>1)*9; fill_round(qx,qy,8,7,2,15,rgb(255,255,255)); }
      g_lbx_end=m2+44; tb_sep(g_lbx_end+12); }
    draw_tray();
    { int l=g_lbx_end+24, r=g_tray_left-12; DOCK_x=(l+r)/2-DOCK_w/2; if(DOCK_x<l)DOCK_x=l; }
    int hovk=-1, iy=cy-DOCK_IC/2;
    for(int k=0;k<n;k++){ int ix=DOCK_x+DOCK_PAD+k*(DOCK_IC+DOCK_GAP), cxk=ix+DOCK_IC/2; int app=IC_APP[k];
        int hov=(my>=g_tby&&my<g_tby+g_tbh&&mx>=ix-5&&mx<ix+DOCK_IC+5);
        if(hov){ glass(ix-3,cy-26,DOCK_IC+6,52,12,rgb(255,255,255),34); hovk=k; }
        dock_icon(k,ix,iy-1);
        if(WN[app].open) fill_round(cxk-(focus==app?9:4),g_tby+g_tbh-7,focus==app?18:8,3,1,15,(focus==app)?ACC:rgb(200,204,222)); }
    if(hovk>=0){ const char*nm=APPNAME[IC_APP[hovk]]; int cxk=DOCK_x+DOCK_PAD+hovk*(DOCK_IC+DOCK_GAP)+DOCK_IC/2;
        int tw=pw_str(nm,1)+28, tx=cxk-tw/2, ty=g_tby-38; if(tx<6)tx=6; if(tx+tw>(int)W-6)tx=(int)W-6-tw;
        shadow_soft(tx,ty,tw,28,8); glass(tx,ty,tw,28,8,rgb(28,26,46),225); pdstr_ctr(tx+tw/2,ty+8,nm,rgb(240,244,252),1); }
}
static void applemenu_draw(void){ int ph=AM_N*AM_IH+8; shadow_soft(AM_X,AM_Y,AM_W,ph,10); fill_round(AM_X,AM_Y,AM_W,ph,10,15,rgb(250,251,253));
    for(int k=0;k<AM_N;k++){ int iy=AM_Y+4+k*AM_IH; int hov=(mx>=AM_X&&mx<AM_X+AM_W&&my>=iy&&my<iy+AM_IH);
        if(hov) fill_round(AM_X+4,iy,AM_W-8,AM_IH,6,15,ACC); dstr(AM_X+14,iy+8,AM_LBL[k], hov?rgb(255,255,255):INK,1); } }
/* ---- notifications: toast + history panel ---- */
static void toast_frame(void){                        /* advance the toast timer */
    if(g_toast_cur<0) return;
    if(g_toast_until==0){ g_toast_until=g_ticks+400; g_notif[g_toast_cur].shown=1; }   /* ~4 s */
    if(g_ticks>g_toast_until){ g_toast_cur=-1; }
}
static void notif_toast_draw(void){
    if(g_toast_cur<0) return; Notif* nt=&g_notif[g_toast_cur];
    int tw=300, th=nt->body[0]?64:44, tx=(int)W-tw-16, ty=34;
    shadow_soft(tx,ty,tw,th,12); fill_round(tx,ty,tw,th,12,15,g_theme?rgb(34,37,48):rgb(252,253,255));
    fill_rect(tx,ty+10,3,th-20,ACC);
    dstr(tx+16,ty+10,nt->title,g_theme?rgb(236,239,246):rgb(24,28,40),1);
    if(nt->body[0]) dstr(tx+16,ty+32,nt->body,g_theme?rgb(150,158,176):rgb(110,118,138),1);
}
static void wifi_panel_draw(void){
    int pw=320, px=g_wifix-pw+20; if(px<6)px=6;
    int rows=g_apn>7?7:g_apn; int ph=70+rows*30+((g_wifi_entry&&g_wifi_sel>=0)?70:0)+26; int py=g_tby-ph-10;
    shadow_soft(px,py,pw,ph,12); fill_round(px,py,pw,ph,12,15,g_theme?rgb(28,31,42):rgb(250,251,254));
    dstr(px+16,py+14,"Wi-Fi",g_theme?rgb(236,239,246):rgb(24,28,40),2);
    int bx=px+pw-80, by=py+12; fill_round(bx,by,66,22,6,15, g_wifi_scanning?rgb(150,158,176):ACC); dstr_ctr(bx+33,by+7,g_wifi_scanning?"...":"Scan",rgb(255,255,255),1);
    { char st[64]; int k=0; for(;g_wifi_status[k]&&k<36;k++)st[k]=g_wifi_status[k]; if(g_wifi_status[k]){ st[k++]='.'; st[k++]='.'; } st[k]=0; dstr(px+16,py+38,st,g_theme?rgb(150,158,176):rgb(110,118,138),1); }
    int cy=py+66;
    for(int i=0;i<rows;i++){
        int hov=(mx>=px+8&&mx<px+pw-8&&my>=cy&&my<cy+28);
        int isconn=(g_wm_state==WM_CONNECTED&&g_wifi_conn[0]); if(isconn){ int k=0; while(g_aps[i].ssid[k]&&g_wifi_conn[k]&&g_aps[i].ssid[k]==g_wifi_conn[k])k++; isconn=(!g_aps[i].ssid[k]&&!g_wifi_conn[k]); }
        if(i==g_wifi_sel) fill_round(px+8,cy,pw-16,28,6,15,mix(ACC,g_theme?rgb(28,31,42):rgb(255,255,255),180));
        else if(hov) fill_round(px+8,cy,pw-16,28,6,15,g_theme?rgb(40,44,58):rgb(234,238,246));
        u32 nmc=g_theme?rgb(224,228,236):rgb(30,34,46), dim=g_theme?rgb(120,128,146):rgb(130,138,156);
        wifi_bars(px+16,cy+7,wifi_signal_bars(g_aps[i].rssi>-127?g_aps[i].rssi:-80),isconn?rgb(40,170,90):(g_theme?rgb(140,200,255):ACC),g_theme?rgb(60,66,82):rgb(214,220,230));
        dstr(px+42,cy+8,g_aps[i].ssid[0]?g_aps[i].ssid:"<hidden>",nmc,1);
        if(g_aps[i].sec)wifi_lock(px+pw-94,cy+9,dim);
        dstr(px+pw-80,cy+8,isconn?"on":g_aps[i].ch>14?"5G":"2.4",isconn?rgb(40,170,90):dim,1);
        if(wifi_saved_find(g_aps[i].ssid)>=0&&!isconn)fill_circle(px+pw-24,cy+14,3,dim);
        cy+=30;
    }
    if(g_apn==0) dstr(px+16,py+66,"(press Scan to find networks)",g_theme?rgb(120,128,146):rgb(150,158,176),1);
    if(g_wifi_entry && g_wifi_sel>=0){
        cy+=6; dstr(px+16,cy,g_wifi_entry==2?"User name (then Enter):":"Password:",g_theme?rgb(200,206,220):rgb(70,78,98),1); cy+=20;
        fill_round(px+16,cy,pw-100,26,5,15,g_theme?rgb(44,48,62):rgb(238,241,247));
        char mask[40]; int n=g_wifi_pwlen>36?36:g_wifi_pwlen; for(int k=0;k<n;k++)mask[k]='*'; mask[n]=0;
        if(g_wifi_entry==2){ int k=0; for(;g_wifi_user[k]&&k<36;k++)mask[k]=g_wifi_user[k]; mask[k]=0; }
        dstr(px+22,cy+7,mask,g_theme?rgb(236,239,246):rgb(24,28,40),1);
        int jx=px+pw-76; fill_round(jx,cy,60,26,5,15,ACC); dstr_ctr(jx+30,cy+7,"Join",rgb(255,255,255),1);
    }
}
static void notif_panel_draw(void){
    int pw=340, px=(int)W-pw-10, py=10, ph=g_tby-20;
    shadow_soft(px,py,pw,ph,0); fill_rect(px,py,pw,ph,g_theme?rgb(26,28,38):rgb(248,249,252));
    fill_rect(px,py,2,ph,g_theme?rgb(48,52,66):rgb(226,229,238));
    dstr(px+18,py+16,"Notifications",g_theme?rgb(236,239,246):rgb(24,28,40),2);
    int cy=py+52; int shown=0;
    for(int q=g_notif_n-1;q>=0 && shown<12;q--){ Notif* nt=&g_notif[q%NOTIF_MAX];
        int ch=nt->body[0]?60:40; fill_round(px+12,cy,pw-24,ch-6,8,15,g_theme?rgb(34,37,48):rgb(255,255,255));
        fill_rect(px+12,cy+8,3,ch-22,ACC);
        dstr(px+26,cy+8,nt->title,g_theme?rgb(232,236,244):rgb(28,32,44),1);
        if(nt->body[0]) dstr(px+26,cy+28,nt->body,g_theme?rgb(150,158,176):rgb(110,118,138),1);
        cy+=ch; shown++; }
    if(g_notif_n==0) dstr(px+18,py+60,"No new notifications",g_theme?rgb(120,128,146):rgb(150,158,176),1);
}
/* ---- workspaces: switcher overlay (shown briefly on change) ---- */
static u64 g_ws_show=0;
void ws_switch(int w){ if(w<0)w=NWS-1; if(w>=NWS)w=0; if(w==g_ws)return; g_ws=w; if(focus>=0&&WN[focus].ws!=g_ws)focus=-1; g_ws_show=g_ticks+120; }
static void ws_overlay_draw(void){
    if(g_ticks>g_ws_show) return;
    int bw=NWS*66+16, bh=54, bx=(int)W/2-bw/2, by=(int)H-140;
    shadow_soft(bx,by,bw,bh,12); fill_round(bx,by,bw,bh,12,15,g_theme?rgb(30,33,44):rgb(250,251,255));
    for(int w=0;w<NWS;w++){ int cx=bx+8+w*66, cyy=by+8; int cnt=0; for(int i=0;i<NWIN;i++) if(WN[i].open&&WN[i].ws==w)cnt++;
        fill_round(cx,cyy,58,38,7,15,w==g_ws?ACC:(g_theme?rgb(44,48,62):rgb(228,232,242)));
        char b[8],*p=b;*p++=(char)('1'+w);*p=0; dstr_ctr(cx+29,cyy+6,b,w==g_ws?rgb(255,255,255):(g_theme?rgb(200,206,220):rgb(90,98,118)),2);
        if(cnt){ char d[8],*q=d;q=udec(q,(u32)cnt);*q=0; dstr_ctr(cx+29,cyy+26,d,w==g_ws?rgb(230,236,255):rgb(120,128,146),1); } }
}
static int anim_hide=-1;
   /* fwd - defined in boot splash section */
/* ---- desktop shortcuts on the wallpaper ---- */
static const int DESK[5]={W_FILES,W_BROWSER,W_TERM,W_PAINT,W_SETTINGS};
static const char* DESK_L[5]={"Files","Browser","Terminal","Paint","Settings"};
/* per-format desktop file icons */
static void file_icon(const char* name,int x,int y){
    if(fext_eq(name,"pptx")){ itile(x,y,rgb(190,74,52)); fill_round(x+11,y+12,26,22,3,15,rgb(255,255,255)); fill_rect(x+15,y+17,18,3,rgb(198,80,58)); fill_rect(x+15,y+23,13,3,rgb(230,150,130)); }
    else if(fext_eq(name,"docx")){ itile(x,y,rgb(40,86,170)); fill_round(x+11,y+11,26,26,3,15,rgb(255,255,255)); dstr(x+17,y+16,"W",rgb(40,86,170),2); }
    else if(fext_eq(name,"png")||fext_eq(name,"jpg")||fext_eq(name,"jpeg")||fext_eq(name,"webp")){ itile(x,y,rgb(36,120,92)); fill_round(x+10,y+12,28,22,3,15,rgb(214,232,224)); fill_round(x+27,y+16,6,6,3,15,rgb(245,205,90)); for(int i=0;i<20;i++){int yy=y+32-(i%7); putpx(x+13+i,yy,rgb(70,150,110)); putpx(x+13+i,yy+1,rgb(70,150,110));} }
    else if(fext_eq(name,"nvx")||fext_eq(name,"c")||fext_eq(name,"h")||fext_eq(name,"rn")){ itile(x,y,rgb(28,36,54)); dstr(x+8,y+16,"</>",rgb(120,220,150),2); }
    else if(fext_eq(name,"zip")||fext_eq(name,"tar")||fext_eq(name,"gz")){ itile(x,y,rgb(120,98,46)); fill_round(x+15,y+10,18,28,3,15,rgb(224,204,140)); for(int j=0;j<26;j+=4)fill_rect(x+22,y+11+j,4,2,rgb(120,98,46)); }
    else if(fext_eq(name,"md")||fext_eq(name,"txt")||fext_eq(name,"cfg")||fext_eq(name,"log")){ itile(x,y,rgb(58,68,94)); fill_round(x+12,y+9,24,30,3,15,rgb(246,248,251)); fill_rect(x+16,y+16,16,2,rgb(150,160,180)); fill_rect(x+16,y+21,16,2,rgb(180,188,204)); fill_rect(x+16,y+26,11,2,rgb(180,188,204)); }
    else { itile(x,y,rgb(60,66,86)); fill_round(x+12,y+9,24,30,3,15,rgb(232,236,244)); fill_rect(x+28,y+9,8,8,rgb(200,206,220)); }
}
/* desktop item positions (grid-snapped pixel coords) */

int g_apx[5], g_apy[5], g_apinit=0;
int g_fpx[FS_N], g_fpy[FS_N]; u8 g_fpl[FS_N];
int g_ddrag=-1, g_ddx=0, g_ddy=0, g_ddsx=0, g_ddsy=0;   /* dragging: -1 none, 0-4 app, 100+slot file */
 
static void desk_init_pos(void){ if(g_apinit)return; g_apinit=1; for(int k=0;k<5;k++){ g_apx[k]=DGX0; g_apy[k]=DGY0+k*DGH; } for(int k=0;k<FS_N;k++)g_fpl[k]=0; }
void desk_place_file(int slot){   /* assign the next free grid cell to a desktop file */
    if(g_fpl[slot])return; desk_init_pos();
    for(int c=0;c<12;c++)for(int r=0;r<8;r++){ int px=DGX0+c*DGW, py=DGY0+r*DGH; int used=0;
        for(int k=0;k<5;k++) if(g_apx[k]==px&&g_apy[k]==py)used=1;
        for(int k=0;k<FS_N;k++) if(g_fpl[k]&&g_fpx[k]==px&&g_fpy[k]==py)used=1;
        if(!used){ g_fpx[slot]=px; g_fpy[slot]=py; g_fpl[slot]=1; return; } }
    g_fpx[slot]=DGX0; g_fpy[slot]=DGY0; g_fpl[slot]=1;
}
void launch_icon(int w,int x,int y){ if(w>=0&&w<NWIN&&g_aicon[w]){ fill_round(x,y,48,48,10,15,rgb(236,236,240)); icon_choice_draw(x+8,y+8,32,g_aicon[w]-1); return; } switch(w){
    case W_FILES:ic_folder(x,y);break; case W_TERM:ic_term(x,y);break; case W_EDITOR:ic_edit(x,y);break; case W_BROWSER:ic_web(x,y);break;
    case W_CALC:ic_calc(x,y);break; case W_CLOCK:ic_clock(x,y);break; case W_CALENDAR:ic_calendar(x,y);break; case W_SYSMON:ic_sysmon(x,y);break;
    case W_LOGS:{ itile(x,y,rgb(12,14,20)); for(int r=0;r<4;r++)fill_rect(x+10,y+11+r*7,28-(r*3),3,rgb(120,255,150)); break; }
    case W_TFIND:{ itile(x,y,rgb(18,20,28)); dstr(x+9,y+13,">_",rgb(120,255,180),2); break; }
    case W_VIDEO:{ itile(x,y,rgb(32,40,56)); fill_round(x+9,y+12,30,22,4,15,rgb(60,140,240)); for(int r=0;r<12;r++){ int l=r<6?r:11-r; fill_rect(x+21,y+17+r,l+1,1,rgb(255,255,255)); } break; }
    case W_SNAKE:ic_snake(x,y);break; case W_FLAPPY:ic_flappy(x,y);break; case W_PAINT:ic_paint(x,y);break; case W_SETTINGS:ic_gear(x,y);break;
    case W_CAMERA:{ itile(x,y,rgb(34,44,70)); fill_round(x+10,y+15,28,20,4,15,rgb(90,100,130)); fill_circle(x+24,y+25,7,rgb(20,24,34)); fill_circle(x+24,y+25,4,rgb(110,170,250)); fill_round(x+18,y+11,12,5,2,15,rgb(90,100,130)); break; }
    case W_QUAKE:{ itile(x,y,rgb(58,38,22)); fill_circle(x+24,y+24,13,rgb(196,140,70)); fill_circle(x+24,y+24,9,rgb(58,38,22)); fill_rect(x+22,y+8,4,30,rgb(196,140,70)); fill_rect(x+16,y+30,16,3,rgb(196,140,70)); break; }
    case W_WRITER:{ itile(x,y,rgb(43,87,154)); fill_round(x+13,y+9,22,30,3,15,rgb(255,255,255)); for(int r=0;r<5;r++)fill_rect(x+17,y+15+r*4,r==4?8:14,2,rgb(43,87,154)); dstr(x+9,y+25,"W",rgb(255,255,255),1); break; }
    case W_RECORDER:{ itile(x,y,rgb(150,30,40)); fill_round(x+19,y+10,10,18,5,15,rgb(255,255,255)); fill_rect(x+16,y+24,2,6,rgb(255,255,255)); fill_rect(x+30,y+24,2,6,rgb(255,255,255)); fill_rect(x+16,y+30,16,2,rgb(255,255,255)); fill_rect(x+23,y+31,2,6,rgb(255,255,255)); fill_rect(x+18,y+37,12,2,rgb(255,255,255)); break; }
    case W_GPU3D:{ itile(x,y,rgb(18,26,20)); fill_round(x+10,y+12,28,22,3,15,rgb(118,185,0)); fill_round(x+13,y+15,22,16,2,15,rgb(18,26,20)); for(int k=0;k<5;k++)fill_rect(x+14+k*4,y+34,2,5,rgb(118,185,0)); fill_circle(x+24,y+23,5,rgb(240,140,60)); fill_circle(x+22,y+21,2,rgb(255,220,180)); break; }
    case W_BENCH:{ itile(x,y,rgb(24,70,52)); for(int k=0;k<4;k++){ int bh=6+k*6; fill_round(x+11+k*7,y+36-bh,5,bh,2,15,k==3?rgb(250,210,90):rgb(120,220,160)); } break; }
    case W_DOOM:{ itile(x,y,rgb(78,18,14)); fill_round(x+11,y+11,26,26,5,15,rgb(196,52,30)); dstr_ctr(x+24,y+17,"D",rgb(255,224,180),2); }break;
    case W_CHATGPT:{ itile(x,y,rgb(36,62,120)); fill_rect(x+10,y+12,12,10,rgb(120,190,255)); fill_rect(x+26,y+12,12,10,rgb(90,220,170)); fill_rect(x+10,y+26,12,10,rgb(250,200,90)); fill_rect(x+26,y+26,12,10,rgb(240,120,140)); break; }
    default:{ itile(x,y,rgb(46,52,72)); fill_round(x+13,y+13,22,22,4,15,ACC); dstr_ctr(x+24,y+18,"N",rgb(255,255,255),1); } } }
static void desk_hi(int x,int y){ if(mx>=x-6&&mx<x+54&&my>=y-6&&my<y+74){ if(g_gfx){ gfx_round(x-6,y-6,60,80,12,15,rgb(255,255,255),42); return; } for(int j=-6;j<74;j++)for(int i=-6;i<54;i++){ if(cut(i+6,j+6,60,80,12,15))continue; int dx=x+i,dy=y+j; if((u32)dx<W&&(u32)dy<H){ u32*p=&SCENE[(u64)dy*SW+dx]; *p=mix(*p,rgb(255,255,255),42); } } } }
static const int CL_IY[6]={12,82,152,222,292,362};
int classic_icon_hit(int px,int py){ (void)px; (void)py; (void)CL_IY; return -1; }
static void desk_draw(void){ return; desk_init_pos();
    for(int k=0;k<5;k++){ int x=g_apx[k],y=g_apy[k]; if(g_ddrag==k){ x=mx-g_ddx; y=my-g_ddy; } else desk_hi(x,y);
        launch_icon(DESK[k],x,y); dstr_ctr(x+24,y+52,DESK_L[k],rgb(238,242,250),1); }
    for(int s=0;s<FS_N;s++) if(FS[s].used && FS[s].folder==2){ if(!g_fpl[s])desk_place_file(s); int x=g_fpx[s],y=g_fpy[s]; if(g_ddrag==100+s){ x=mx-g_ddx; y=my-g_ddy; } else desk_hi(x,y);
        file_icon(FS[s].name,x,y); char nm[13]; int i=0; for(;FS[s].name[i]&&i<11;i++)nm[i]=FS[s].name[i]; if(FS[s].name[i]){nm[i-1]='.';nm[i]=0;}else nm[i]=0; dstr_ctr(x+24,y+52,nm,rgb(238,242,250),1); } }
static void marq_draw(void){ if(!g_marq)return; int x0=marq_x0,y0=marq_y0,x1=mx,y1=my; if(x1<x0){int t=x0;x0=x1;x1=t;} if(y1<y0){int t=y0;y0=y1;y1=t;}
    int w=x1-x0, h=y1-y0; if(w<2||h<2)return;
    if(g_gfx){ gq_blend(g_rt,x0,y0,w,h,rgb(96,152,240),68); fill_rect(x0,y0,w,1,rgb(60,120,230)); fill_rect(x0,y1-1,w,1,rgb(60,120,230)); fill_rect(x0,y0,1,h,rgb(60,120,230)); fill_rect(x1-1,y0,1,h,rgb(60,120,230)); return; }
    for(int j=0;j<h;j++)for(int i=0;i<w;i++){ int dx=x0+i,dy=y0+j; if((u32)dx<W&&(u32)dy<H){ u32*p=&SCENE[(u64)dy*SW+dx]; *p=mix(*p,rgb(96,152,240),68); } }
    for(int i=0;i<w;i++){ putpx(x0+i,y0,rgb(60,120,230)); putpx(x0+i,y1-1,rgb(60,120,230)); } for(int j=0;j<h;j++){ putpx(x0,y0+j,rgb(60,120,230)); putpx(x1-1,y0+j,rgb(60,120,230)); } }
extern u8 g_ficon[], g_dicon[], g_aicon[];
extern void icon_choice_draw(int x,int y,int s,int ic);
extern const char* icon_choice_name(int ic);
extern void icons_save(void);
extern int item_icon_kind(int isd,int id);
extern void fx_dir_stats(int d,int* nf,int* nd,u64* bytes);
static int pr_x, pr_y, pr_w=420, pr_h=462;
static char* pr_size(char* p,u64 L){ if(L>=1048576ULL){ p=udec(p,(u32)(L/1048576ULL)); *p++='.'; p=udec(p,(u32)((L%1048576ULL)*10/1048576ULL)); return scopy(p," MB"); } if(L>=1024){ p=udec(p,(u32)(L/1024)); *p++='.'; p=udec(p,(u32)((L%1024)*10/1024)); return scopy(p," KB"); } p=udec(p,(u32)L); return scopy(p," bytes"); }
static char* pr_mode(char* p,u32 m,int dir){ *p++=dir?'d':'-'; const char* r="rwxrwxrwx"; for(int i=0;i<9;i++) *p++=(m&(1u<<(8-i)))?r[i]:'-'; *p=0; return p; }
static int pr_cur(void){ if(g_prop_slot>=0)return g_ficon[g_prop_slot]; if(g_prop_dir>=0)return g_dicon[g_prop_dir]; if(g_prop_app>=0)return g_aicon[g_prop_app]; return 0; }
static void pr_set(int v){ if(g_prop_slot>=0)g_ficon[g_prop_slot]=(u8)v; else if(g_prop_dir>=0)g_dicon[g_prop_dir]=(u8)v; else if(g_prop_app>=0)g_aicon[g_prop_app]=(u8)v; icons_save(); }
static void pr_row(int x,int* y,const char* k,const char* v){ dstr(x,*y,k,rgb(0,0,0),1); char b[44]; int n=0; while(v[n]&&n<40){ b[n]=v[n]; n++; } if(v[n]){ b[n-3]='.'; b[n-2]='.'; b[n-1]='.'; } b[n]=0; dstr(x+112,*y,b,rgb(0,0,0),1); *y+=22; }
void prop_draw(void){
    if(!g_prop)return;
    extern int g_round_off; g_round_off=1;
    int pw=pr_w, ph=pr_h, x=(int)W/2-pw/2, y=(int)H/2-ph/2; pr_x=x; pr_y=y;
    u32 FACE=rgb(212,208,200);
    fill_rect(x,y,pw,ph,FACE); ui_bevel(x,y,pw,ph,0);
    for(int i=0;i<pw-6;i++){ int t=i*255/(pw-6); u32 c=rgb(110+(244-110)*t/255,34+(202-34)*t/255,9+(158-9)*t/255); fill_rect(x+3+i,y+3,1,20,c); }
    char nm[80]; nm[0]=0;
    if(g_prop_slot>=0&&g_prop_slot<FS_N&&FS[g_prop_slot].used) scopy(nm,FS[g_prop_slot].name);
    else if(g_prop_dir>=0&&DT[g_prop_dir].used) scopy(nm,g_prop_dir==0?"/":DT[g_prop_dir].name);
    else if(g_prop_app>=0&&g_prop_app<NWIN) scopy(nm,APPNAME[g_prop_app]);
    else { g_prop=0; g_round_off=0; return; }
    { char t[100],*p=t; int n=0; while(nm[n]&&n<36)*p++=nm[n++]; p=scopy(p," Properties"); *p=0; dstr(x+8,y+5,t,rgb(255,255,255),1); }
    { int bx=x+pw-22, by=y+5; ui_bevel(bx,by,16,14,0); fill_rect(bx+2,by+2,12,10,FACE); for(int t=0;t<6;t++){ putpx(bx+5+t,by+4+t,0); putpx(bx+10-t,by+4+t,0); } }
    int tx=x+10, ty=y+30; fill_rect(tx,ty,70,22,FACE); fill_rect(tx,ty,70,1,rgb(255,255,255)); fill_rect(tx,ty,1,22,rgb(255,255,255)); fill_rect(tx+69,ty,1,22,rgb(64,64,64)); dstr(tx+8,ty+4,"General",rgb(0,0,0),1);
    int cx=x+10, cy=ty+22, cw=pw-20, ch=ph-(cy-y)-50; ui_bevel(cx,cy,cw,ch,0); fill_rect(tx+1,cy,68,2,FACE);
    int lx=cx+14, ly=cy+14;
    int isdir=g_prop_dir>=0, isapp=(g_prop_slot<0&&g_prop_dir<0);
    { int ic=pr_cur(); if(ic) icon_choice_draw(lx,ly,32,ic-1); else if(isapp){ extern void mini_icon_big(int,int,int); mini_icon_big(g_prop_app,lx,ly); } else icon_choice_draw(lx,ly,32,item_icon_kind(isdir,isdir?g_prop_dir:g_prop_slot)); }
    ui_bevel(lx+52,ly+6,cw-80,22,1); fill_rect(lx+54,ly+8,cw-84,18,rgb(255,255,255)); { char b[44]; int n=0; while(nm[n]&&n<(cw-96)/8){ b[n]=nm[n]; n++; } b[n]=0; dstr(lx+58,ly+10,b,rgb(0,0,0),1); }
    ly+=46; fill_rect(lx,ly,cw-28,1,rgb(128,128,128)); fill_rect(lx,ly+1,cw-28,1,rgb(255,255,255)); ly+=10;
    char b[120],*p;
    if(g_prop_slot>=0){ int s=g_prop_slot; extern const char* file_type_name(const char*); pr_row(lx,&ly,"Type:",file_type_name(FS[s].name));
        { char pb[200]; dt_path(FS[s].dir,pb); pr_row(lx,&ly,"Location:",pb); }
        p=pr_size(b,(u64)FS[s].len); *p=0; pr_row(lx,&ly,"Size:",b);
        pr_mode(b,FS[s].mode?FS[s].mode:0644u,0); pr_row(lx,&ly,"Permissions:",b);
        pr_row(lx,&ly,"Stored on:",disk_ok?"NVXFS disk":"Memory (live system)"); }
