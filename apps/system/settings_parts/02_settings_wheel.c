void settings_wheel(int sc){ if(g_stpg!=P_APPS)return; g_st_appscroll+=sc>0?-2:2; int n=progfiles_count(); if(g_st_appscroll>n-3)g_st_appscroll=n-3; if(g_st_appscroll<0)g_st_appscroll=0; }
void settings_page_name(const char* n){ static const char* NM[21]={"focus","system","personalization","network","devices","apps","accounts","time","privacy","update","display","graphics","advanced","background","sound","mouse","installed","nvidia","screens","recovery","about"}; static const int PG[21]={P_FOCUS,0,1,2,3,4,5,6,7,P_UPDATE,P_DISPLAY,P_GRAPHICS,P_GFXADV,P_BACKGROUND,P_SOUND,P_MOUSE,P_APPS,P_NVIDIA,P_SCREENS,P_RECOVERY,P_ABOUT};
    for(int k=0;k<21;k++){ int j=0; while(NM[k][j]&&n[j]==NM[k][j])j++; if(!NM[k][j]&&(!n[j]||n[j]==' ')){ g_stpg=PG[k]; return; } } }
static char g_st_q[40]; static int g_st_qn=0, g_st_qf=0;
static int st_lc(int c){ return (c>='A'&&c<='Z')?c+32:c; }
static int st_norm(const char* s,char* o,int cap){ int n=0; for(int i=0;s&&s[i]&&n<cap-1;i++){ int c=st_lc((u8)s[i]); if((c>='a'&&c<='z')||(c>='0'&&c<='9'))o[n++]=(char)c; } o[n]=0; return n; }
static int st_has(const char* h,const char* q){ if(!h)return 0; char a[128], b[48]; int na=st_norm(h,a,128), nb=st_norm(q,b,48); if(!nb)return 0; for(int i=0;i+nb<=na;i++){ int k=0; while(k<nb&&a[i+k]==b[k])k++; if(k==nb)return 1; } return 0; }
static int st_add(stile* out,int n,int cap,stile t){ if(n>=cap)return n; for(int d=0;d<n;d++) if(out[d].pg==t.pg)return n; out[n]=t; return n+1; }
static int st_search(stile* out,int cap){
    static const stile EX[4]={{P_GFXADV,15,"Advanced graphics","GPU details, screen copy, frame check"},{P_NVIDIA,15,"NVIDIA graphics","Driver status and switches"},{P_SCREENS,3,"Screen and monitors","Screen mode and monitors"},{P_ACCOUNT,11,"Your account","User name and Noovex account"}};
    int n=0; g_st_q[g_st_qn]=0;
    for(int pass=0;pass<3;pass++){
        for(int c=0;c<9;c++){ int m; const stile* t=cat_tiles(c,&m); for(int k=0;k<m;k++){ if(pass==0&&st_has(t[k].t,g_st_q))n=st_add(out,n,cap,t[k]); if(pass==2&&st_has(t[k].d,g_st_q))n=st_add(out,n,cap,t[k]); } }
        for(int k=0;k<4;k++){ if(pass==0&&st_has(EX[k].t,g_st_q))n=st_add(out,n,cap,EX[k]); if(pass==2&&st_has(EX[k].d,g_st_q))n=st_add(out,n,cap,EX[k]); }
        for(int c=0;c<9;c++){ stile t={c,CATI[c],CATN[c],CATD[c]}; if(pass==1&&st_has(t.t,g_st_q))n=st_add(out,n,cap,t); if(pass==2&&st_has(t.d,g_st_q))n=st_add(out,n,cap,t); } }
    return n; }
void settings_clear_search(void){ g_st_qn=0; g_st_q[0]=0; g_st_qf=0; }
int settings_key(int c){
    if(settings_focus_key(c))return 1;
    if(c==27){ if(!g_st_qn&&!g_st_qf)return 0; g_st_qn=0; g_st_q[0]=0; g_st_qf=0; return 1; }
    if(c=='\b'){ if(g_st_qn>0)g_st_qn--; g_st_q[g_st_qn]=0; g_st_qf=1; return 1; }
    if(c=='\n'){ if(!g_st_qn)return 0; stile r[1]; if(st_search(r,1)){ g_stpg=r[0].pg; g_st_qn=0; g_st_q[0]=0; g_st_qf=0; } return 1; }
    if(c>=32&&c<127){ if(g_st_qn<38){ g_st_q[g_st_qn++]=(char)c; g_st_q[g_st_qn]=0; } g_st_qf=1; return 1; }
    return 0; }
static void st_magnifier(int x,int y,u32 c,u32 bg){ fill_circle(x+6,y+6,6,c); fill_circle(x+6,y+6,4,bg); for(int i=0;i<5;i++)fill_rect(x+10+i,y+10+i,2,2,c); }
static void st_winbtn(int b,int bx,int cy){ int hov=in_rect(mx,my,bx-1,cy-10,20,20); fill_circle(bx+9,cy,9,hov?rgb(210,210,215):rgb(226,226,230)); u32 g=ST_FG;
    if(b==0)fill_rect(bx+6,cy+2,7,2,g);
    else if(b==1){ fill_rect(bx+6,cy-3,7,1,g); fill_rect(bx+6,cy+3,7,1,g); fill_rect(bx+6,cy-3,1,7,g); fill_rect(bx+12,cy-3,1,7,g); fill_rect(bx+6,cy-2,7,1,g); }
    else { for(int t=0;t<6;t++){ fill_rect(bx+6+t,cy-3+t,2,1,g); fill_rect(bx+11-t,cy-3+t,2,1,g); } } }
static int st_hdr_lo=0;
int settings_title_click(int px,int py){ for(int h=st_hn-1;h>=0;h--) if(st_hid[h]==1999||st_hid[h]==500||st_hid[h]==1000+P_ACCOUNT) if(in_rect(px,py,st_hx[h],st_hy[h],st_hw[h],st_hh[h])&&py<st_hdr_lo){ settings_click(px,py); return 1; } return 0; }
static void settings_draw_classic(int i){
    extern int g_round_off; g_round_off=1;
    int X=WN[i].x, Y=WN[i].y, ww=WN[i].w, wh=WN[i].h; int sbw=ww>=900?236:ww>=760?212:190;
    win_frame(i,"Settings"); st_hdr_lo=Y+TITLEH;
    fill_rect(X+3,Y+TITLEH,ww-6,wh-TITLEH-3,ST_FACE);
    int top=Y+TITLEH+6;
    { int sx=X+8, sy=top, sw=sbw-10; int fo=g_st_qf; ui_bevel(sx,sy,sw,24,1); fill_rect(sx+2,sy+2,sw-4,20,rgb(255,255,255));
      if(g_st_qn){ g_st_q[g_st_qn]=0; int w=pdstr(sx+6,sy+5,g_st_q,ST_FG,1); if(fo&&((g_ticks/50)&1))fill_rect(w+1,sy+4,1,16,0); }
      else { pdstr(sx+6,sy+5,"Search settings",rgb(128,128,128),1); if(fo&&((g_ticks/50)&1))fill_rect(sx+5,sy+4,1,16,0); }
      st_hit(sx,sy,sw,24,500); }
    int lx=X+8, ly=top+30, lw=sbw-10, lh=wh-(ly-Y)-10; ui_bevel(lx,ly,lw,lh,1); fill_rect(lx+2,ly+2,lw-4,lh-4,rgb(255,255,255));
    int cur=g_st_qn?-1:pg_cat(g_stpg); int rh=26;
    for(int k=0;k<9;k++){ int iy=ly+4+k*rh; if(iy+rh>ly+lh-2)break; int on=(k==cur), hov=in_rect(mx,my,lx+3,iy,lw-6,rh-2);
        if(on)fill_rect(lx+3,iy,lw-6,rh-2,ST_HL); else if(hov)fill_rect(lx+3,iy,lw-6,rh-2,rgb(236,233,226));
        char b[28]; int n=0; while(CATN[k][n]&&n<(lw-20)/8&&n<27){ b[n]=CATN[k][n]; n++; } b[n]=0; pdstr(lx+10,iy+4,b,0,1);
        if(k==8&&g_upd.checked==1&&g_upd.avail)fill_circle(lx+lw-14,iy+11,4,rgb(200,0,0)); st_hit(lx+3,iy,lw-6,rh-2,100+k); }
    { int uy=ly+lh-34; if(uy>ly+4+9*rh){ fill_rect(lx+6,uy-4,lw-12,1,rgb(128,128,128)); char b[40],*p=b; p=scopy(p,g_user); p=scopy(p,acct_is_admin(g_uid)?" (admin)":""); *p=0; pdstr(lx+10,uy+6,b,0,1); st_hit(lx+3,uy,lw-6,28,1000+P_ACCOUNT); } }
    int x0=X+sbw+4, pw=ww-sbw-10;
    { int hy=top; const char* t=g_st_qn?"Search":pg_title(g_stpg); int tx=x0+8;
      if(!g_st_qn&&pg_parent(g_stpg)>=0){ int hov=in_rect(mx,my,x0+4,hy,30,24); ui_bevel(x0+4,hy,30,24,0); fill_rect(x0+6,hy+2,26,20,hov?rgb(226,222,214):ST_FACE); st_chev(x0+15,hy+7,0,1); st_hit(x0+4,hy,30,24,1999); tx=x0+44; }
      st_bold(tx,hy+4,t,0); fill_rect(x0+4,hy+30,pw-8,1,rgb(128,128,128)); fill_rect(x0+4,hy+31,pw-8,1,rgb(255,255,255)); }
    st_w0=pw-32; if(st_w0>720)st_w0=720; st_x0=x0+(pw-st_w0)/2; int cy=top+40; st_ybot=Y+wh-10;
    if(g_st_qn){ stile r[24]; int n=st_search(r,24); int maxn=(st_ybot-cy-24)/56; if(n>maxn)n=maxn; if(n)st_tiles(&cy,r,n); else pdstr(st_x0+10,cy+20,"No results found",ST_FG,1); g_round_off=0; return; }
    st_page(g_stpg,cy); g_round_off=0; }
void settings_draw(int i){
    { static int seen=1; if(g_setcat!=seen){ static const int MP[9]={0,P_BACKGROUND,P_WIFI,P_DISPLAY,P_POWER,P_STORAGE,P_TERMINAL,P_ABOUT,3}; seen=g_setcat; if(g_setcat>=0&&g_setcat<9)g_stpg=MP[g_setcat]; } }
    st_hn=0; if(g_stpg<0)g_stpg=0; if(!g_gm_ready)gmetrics();
    if(ST_CL){ settings_draw_classic(i); return; }
    int X=WN[i].x, Y=WN[i].y, ww=WN[i].w, wh=WN[i].h, foc=(focus==i); int sbw=ww>=900?260:ww>=760?232:204; int HB=48;
    shadow_soft(X,Y,ww,wh,12);
    fill_round(X,Y,ww,wh,12,15,foc?rgb(190,190,196):rgb(210,210,214)); fill_round(X+1,Y+1,ww-2,wh-2,11,15,ST_BG);
    fill_round(X+1,Y+1,sbw,wh-2,11,1|4,ST_SIDE); fill_rect(X+1+sbw,Y+1,1,wh-2,ST_LINE);
    st_hdr_lo=Y+TITLEH;
    st_bold_ctr(X+1+sbw/2,Y+17,"Settings",ST_FG);
    { int x0=X+sbw+2, pw=ww-sbw-3; const char* t=g_st_qn?"Search":pg_title(g_stpg); int c0=x0+pw/2; int tw=pw_str(t,1)+1;
      int lim=X+ww-80-c0; if(tw/2>lim&&lim>40)c0=X+ww-80-tw/2; st_bold_ctr(c0,Y+17,t,ST_FG);
      if(!g_st_qn&&pg_parent(g_stpg)>=0){ int bx=x0+10, by=Y+8; int hov=in_rect(mx,my,bx,by,34,30); if(hov)fill_round(bx,by,34,30,8,15,rgb(228,228,232)); st_chev(bx+14,by+11,ST_FG,1); st_hit(bx,by,34,30,1999); } }
    for(int b=0;b<3;b++){ int cx=X+ww-6-18; int bx=(b==2)?cx:(b==1)?cx-2-18:cx-2-18-18; st_winbtn(b,bx,Y+16); }
    { int sx=X+12, sy=Y+HB, sw=sbw-22; int fo=g_st_qf; fill_round(sx,sy,sw,34,9,15,fo?ACC:rgb(222,222,226)); fill_round(sx+(fo?2:0),sy+(fo?2:0),sw-(fo?4:0),34-(fo?4:0),fo?7:9,15,fo?rgb(255,255,255):rgb(222,222,226));
      u32 sb=fo?rgb(255,255,255):rgb(222,222,226); st_magnifier(sx+11,sy+10,ST_DIM,sb);
      if(g_st_qn){ g_st_q[g_st_qn]=0; int w=pdstr(sx+34,sy+10,g_st_q,ST_FG,1); if(fo&&((g_ticks/50)&1))fill_rect(w+1,sy+9,2,16,ACC); }
      else { pdstr(sx+34,sy+10,"Search settings",ST_DIM,1); if(fo&&((g_ticks/50)&1))fill_rect(sx+33,sy+9,2,16,ACC); }
      st_hit(sx,sy,sw,34,500); }
    int cur=g_st_qn?-1:pg_cat(g_stpg); int rh=40, ry0=Y+HB+46; int avail=wh-HB-46-70; if(9*rh>avail){ rh=avail/9; if(rh<32)rh=32; }
    for(int k=0;k<9;k++){ int iy=ry0+k*rh; int on=(k==cur), hov=in_rect(mx,my,X+8,iy,sbw-14,rh-2); u32 rb=on?rgb(214,214,219):hov?rgb(225,225,229):ST_SIDE;
        if(on||hov)fill_round(X+8,iy,sbw-14,rh-2,8,15,rb);
        st_glyph(X+16,iy+(rh-2)/2-13,CATI[k],ST_FG,rb); if(on)st_bold(X+52,iy+(rh-2)/2-7,CATN[k],ST_FG); else pdstr(X+52,iy+(rh-2)/2-7,CATN[k],ST_FG,1);
        if(k==8&&g_upd.checked==1&&g_upd.avail)fill_circle(X+sbw-22,iy+(rh-2)/2,4,ACC); st_hit(X+8,iy,sbw-14,rh-2,100+k); }
    { int uy=Y+wh-60; fill_rect(X+12,uy-6,sbw-22,1,ST_LINE); int hov=in_rect(mx,my,X+8,uy,sbw-14,48); if(hov)fill_round(X+8,uy,sbw-14,48,8,15,rgb(225,225,229));
      fill_circle(X+32,uy+24,15,ACC); extern char g_fullname[]; char ini=g_fullname[0]?g_fullname[0]:g_user[0]; if(ini>='a'&&ini<='z')ini-=32; char s2[2]={ini?ini:'U',0}; st_bold_ctr(X+32,uy+17,s2,rgb(255,255,255));
      { extern char g_fullname[]; st_bold(X+56,uy+8,g_fullname[0]?g_fullname:g_user,ST_FG); } pdstr(X+56,uy+27,acct_is_admin(g_uid)?"Administrator":"Standard user",ST_DIM,1); st_hit(X+8,uy,sbw-14,48,1000+P_ACCOUNT); }
    int x0=X+sbw+2, pw=ww-sbw-3; st_w0=pw-64; if(st_w0>720)st_w0=720; if(st_w0<300)st_w0=pw-24; st_x0=x0+(pw-st_w0)/2; int cy=Y+HB+6; st_ybot=Y+wh-10;
    if(g_st_qn){ stile r[24]; int n=st_search(r,24); int maxn=(st_ybot-cy-24)/56; if(n>maxn)n=maxn;
        if(n)st_tiles(&cy,r,n);
        else { int cx=st_x0+st_w0/2, yy=cy+90; u32 bgc=rgb(236,236,240); fill_circle(cx,yy,40,bgc); fill_circle(cx-5,yy-5,14,ST_DIM); fill_circle(cx-5,yy-5,10,bgc); for(int t=0;t<12;t++)fill_rect(cx+4+t,yy+4+t,4,4,ST_DIM); st_bold_ctr(cx,yy+56,"No results found",ST_FG); pdstr_ctr(cx,yy+80,"Try a different search",ST_DIM,1); }
        return; }
    st_page(g_stpg,cy); }
static void old_leaf(int cat,int cy){ int X1=st_x0+st_w0-24; int g_setcat_sv=g_setcat; g_setcat=cat;
    if(g_setcat==0){
        int y=st_card(cy,64,"Clock"); st_rowtext(y,"Show seconds","Adds seconds to the clock in the top bar"); st_toggle(X1-44,y+20,g_showsec,1); cy=y+64+28;
        y=st_card(cy,128,"Power"); st_rowtext(y,"Restart","Restart the computer now"); st_btn(X1-110,y+16,110,"Restart",2,1); st_sep(y+64);
        st_rowtext(y+64,"Crash test","Triggers a kernel panic screen (for testing)"); st_btn(X1-110,y+80,110,"Crash",3,2); cy=y+128+28;
        y=st_card(cy,64,"Build"); { char b[64],*p=b; p=scopy(p,"NoovexOS 64 v"); p=scopy(p,(char*)g_buildsig.version); *p=0; st_rowtext(y,b,g_sig_ok?"Integrity check passed at boot":"Integrity check reported a mismatch at boot"); }
        fill_round(X1-110,y+20,110,24,12,15,g_sig_ok?rgb(226,246,233):rgb(253,236,226)); pdstr_ctr(X1-55,y+25,g_sig_ok?"Verified":"Mismatch",g_sig_ok?rgb(40,140,80):rgb(200,110,40),1);
    } else if(g_setcat==1){
        wall_thumbs_build(); int cols=3, gap=14, tw=(st_w0-40-gap*(cols-1))/cols; if(tw>132)tw=132; int th=tw*10/16; int y=cy;
        if(st_part!=2){ y=st_card(cy,((NWALL+cols-1)/cols)*(th+30)+26,"Wallpaper");
        for(int k=0;k<NWALL;k++){ int bx=st_x0+20+(k%cols)*(tw+gap), by=y+18+(k/cols)*(th+30);
            if(k==g_theme&&!g_wallmode)fill_round(bx-3,by-3,tw+6,th+6,9,15,ACC);
            for(int yy=0;yy<th;yy++){ int ty=yy*40/th; for(int xx=0;xx<tw;xx++){ if(cut(xx,yy,tw,th,6,15))continue; putpx(bx+xx,by+yy,wall_thumb[k][ty*64+xx*64/tw]); } }
            pdstr_ctr(bx+tw/2,by+th+7,THN[k],k==g_theme?INK:rgb(120,128,146),1); st_hit(bx,by,tw,th,10+k); }
        st_btn(X1-150,y-31,150,g_wallmode==2?"Plain grey (on)":"Plain grey",19,g_wallmode==2?1:0);
        cy=y+((NWALL+cols-1)/cols)*(th+30)+26+28; }
        if(st_part!=1){ y=st_card(cy,128,"Colour and pointer"); st_rowtext(y,"Accent colour",ACN[g_accent]);
        for(int k=0;k<5;k++){ int bx=X1-16-(4-k)*40; if(k==g_accent)fill_circle(bx,y+32,15,ACV[k]); fill_circle(bx,y+32,k==g_accent?13:12,k==g_accent?rgb(255,255,255):ACV[k]); if(k==g_accent)fill_circle(bx,y+32,9,ACV[k]); st_hit(bx-15,y+17,30,30,20+k); }
        st_sep(y+64); st_rowtext(y+64,"Pointer style",0); { int w=st_segw(CUN,5); st_seg(X1-w,y+81,CUN,5,g_cursor,30); } }
    } else if(g_setcat==2){
        if(!pci_n)pci_scan(); int ek=nic_index();
        int y=st_card(cy,64,"Ethernet"); st_rowtext(y,ek>=0?(char*)nic_name(pci_ven[ek],pci_dev[ek]):"No wired adapter",net_ok?"Connected":"Not started - run 'net' in the Terminal");
        if(net_ok){ char b[40],*p=b; p=udec(p,my_ip>>24);*p++='.';p=udec(p,(my_ip>>16)&255);*p++='.';p=udec(p,(my_ip>>8)&255);*p++='.';p=udec(p,my_ip&255); *p=0; pdstr(X1-pw_str(b,1),y+24,b,INK,1); }
        cy=y+64+28;
        extern char g_wifi_user[64], g_wifi_hid[33]; extern int g_wifi_userlen, g_wifi_hidlen; extern const char* wifi_sec_name(const scan_ap*); extern void wifi_bars(int,int,int,u32,u32); extern void wifi_lock(int,int,u32);
        int maxr=(WN[W_SETTINGS].y+WN[W_SETTINGS].h-cy-260)/44; if(maxr<3)maxr=3; if(maxr>8)maxr=8;
        int rows=g_apn<maxr?g_apn:maxr; int extra=g_wifi_entry?44:0; int ch=64+(rows?rows*44:40)+extra;
        y=st_card(cy,ch,"Wi-Fi"); { u32 wm=rtw_find_mmio(); st_rowtext(y,wm?"Wi-Fi":"No Wi-Fi adapter",wm?g_wifi_status:"This computer has no supported Wi-Fi card"); }
        if(rtw_find_mmio()){ st_toggle(X1-44,y+20,g_wm_state!=WM_OFF,1460); st_btn(X1-264,y+16,96,g_wifi_scanning?"Scanning":"Scan",40,1); st_btn(X1-160,y+16,100,"Hidden...",1465,0); }
        st_wifiscan_x=X1-264; st_wifiscan_y=y+16; st_sep(y+64);
        int ry=y+64; st_wifi_ln=0;
        if(!rows&&rtw_find_mmio())pdstr(st_x0+20,ry+14,g_wifi_scanning?"Looking for networks...":"Press Scan to find networks",rgb(150,158,176),1);
        for(int q=0;q<rows;q++){ int isconn=0;
            if(g_wm_state==WM_CONNECTED&&g_wifi_conn[0]){ int kk=0; while(g_aps[q].ssid[kk]&&g_wifi_conn[kk]&&g_aps[q].ssid[kk]==g_wifi_conn[kk])kk++; isconn=(!g_aps[q].ssid[kk]&&!g_wifi_conn[kk]); }
            int sel=(g_wifi_sel==q); if(sel||in_rect(mx,my,st_x0+8,ry+4,st_w0-16,36))fill_round(st_x0+8,ry+4,st_w0-16,36,8,15,sel?mix(ACC,rgb(255,255,255),215):rgb(245,247,250));
            wifi_bars(st_x0+20,ry+15,wifi_signal_bars(g_aps[q].rssi>-127?g_aps[q].rssi:-80),isconn?rgb(40,150,90):ACC,rgb(205,210,220));
            pdstr(st_x0+48,ry+14,g_aps[q].ssid[0]?g_aps[q].ssid:"<hidden>",INK,1);
            { char t[64],*p=t; p=scopy(p,g_aps[q].ch>14?"5 GHz":"2.4 GHz"); p=scopy(p,"  "); p=scopy(p,wifi_sec_name(&g_aps[q])); if(g_aps[q].ht)p=scopy(p,"  Wi-Fi 4"); if(wifi_saved_find(g_aps[q].ssid)>=0)p=scopy(p,"  saved"); *p=0; pdstr(st_x0+300,ry+14,t,rgb(130,138,156),1); }
            if(g_aps[q].sec)wifi_lock(st_x0+282,ry+16,rgb(130,138,156));
            const char* tag=isconn?(g_wm_portal?"Sign in":"Connected"):""; if(tag[0])pdstr(X1-pw_str(tag,1)-(isconn?60:0),ry+14,tag,rgb(40,150,90),1);
            if(isconn){ st_btn(X1-54,ry+5,54,"Info",1466,0); }
            st_hit(st_x0+8,ry+4,st_w0-16-(isconn?70:0),36,50+q); ry+=44;
            if(sel&&g_wifi_entry){ fill_round(st_x0+20,ry+2,st_w0-150,32,8,15,rgb(240,242,246)); char mask[40]; int n=g_wifi_pwlen>36?36:g_wifi_pwlen; for(int z=0;z<n;z++)mask[z]='*'; mask[n]=0; if(g_wifi_entry==2){ int z=0; for(;g_wifi_user[z]&&z<36;z++)mask[z]=g_wifi_user[z]; mask[z]=0; }
                pdstr(st_x0+32,ry+11,mask[0]?mask:g_wifi_entry==2?"User name, then Enter":"Password",mask[0]?INK:rgb(150,158,176),1); if((g_ticks/50)&1)fill_rect(st_x0+33+pw_str(mask,1),ry+10,2,15,ACC);
                st_wifijoin_x=X1-100; st_wifijoin_y=ry+2; st_btn(X1-100,ry+2,100,"Join",41,1); ry+=44; } }
        if(g_wifi_entry&&(g_wifi_sel==-2||g_wifi_entry==3)){ fill_round(st_x0+20,ry+2,st_w0-150,32,8,15,rgb(240,242,246)); char mask[40]; int z=0; if(g_wifi_entry==3){ for(;g_wifi_hid[z]&&z<36;z++)mask[z]=g_wifi_hid[z]; } else { int n=g_wifi_pwlen>36?36:g_wifi_pwlen; for(;z<n;z++)mask[z]='*'; } mask[z]=0;
            pdstr(st_x0+32,ry+11,mask[0]?mask:g_wifi_entry==3?"Network name (SSID), then Enter":"Password",mask[0]?INK:rgb(150,158,176),1); st_btn(X1-100,ry+2,100,"Join",41,1); }
        cy=y+ch+28;
        if(cy+200<WN[W_SETTINGS].y+WN[W_SETTINGS].h){ int R=44; y=st_card(cy,R*2,"Wi-Fi options");
            fr_label(y,"Connect automatically"); st_toggle(st_x0+220,y+10,g_wm_auto,1461); pdstr(st_x0+300,y+16,"Use 5 GHz",ST_FG,1); st_toggle(st_x0+400,y+10,g_wm_5g,1462); st_sep(y+R);
            fr_label(y+R,"Power saving (test)"); st_toggle(st_x0+220,y+R+10,g_wm_ps,1463); pdstr(st_x0+300,y+R+16,"Fast speeds",ST_FG,1); st_toggle(st_x0+400,y+R+10,g_wifi_ra,1464);
            cy=y+R*2+28; }
        { int left=(WN[W_SETTINGS].y+WN[W_SETTINGS].h-cy-40)/40; int n=g_wnet_n<left?g_wnet_n:left; if(n>0){ y=st_card(cy,n*40,"Saved networks");
            for(int i=0;i<n;i++){ int yy=y+i*40; if(i)st_sep(yy); pdstr(st_x0+20,yy+13,g_wnets[i].ssid,INK,1); char t[40],*p=t; p=scopy(p,"priority "); p=udec(p,(u32)g_wnets[i].prio); if(g_wnets[i].hidden)p=scopy(p,", hidden"); if(g_wnets[i].eap)p=scopy(p,", enterprise"); if(!g_wnets[i].autoc)p=scopy(p,", manual"); *p=0; pdstr(st_x0+260,yy+13,t,rgb(130,138,156),1);
                st_btn(X1-250,yy+3,40,"+",1500+i,0); st_btn(X1-204,yy+3,40,"-",1520+i,0); st_btn(X1-158,yy+3,70,g_wnets[i].autoc?"Auto":"Manual",1540+i,0); st_btn(X1-82,yy+3,82,"Forget",1400+i,2); } } }
    } else if(g_setcat==3){
        static const char* HZ[3]={"30 Hz","60 Hz","120 Hz"}; static const char* PN[2]={"LCD","OLED"}; static const char* MA[3]={"Off","Low","High"};
        int y=st_card(cy,64*3,"Screen"); st_rowtext(y,"Refresh rate","How often the desktop redraws"); { int w=st_segw(HZ,3); st_seg(X1-w,y+17,HZ,3,g_refresh_hz==30?0:g_refresh_hz==60?1:2,60); } st_sep(y+64);
        st_rowtext(y+64,"Panel type","OLED shifts pixels slightly to avoid burn-in"); { int w=st_segw(PN,2); st_seg(X1-w,y+81,PN,2,g_panel_oled,63); } st_sep(y+128);
        { char b[48],*p=b; p=udec(p,W); *p++='x'; p=udec(p,H); p=scopy(p,"  "); p=udec(p,BPP); p=scopy(p,"-bit colour - screen and monitors"); *p=0; st_rowtext(y+128,"Resolution",b); { int X1r=st_x0+st_w0-24; int hv=in_rect(mx,my,st_x0,y+128,st_w0,64); st_chev(X1r-4,y+156,hv?ACC:ST_DIM,0); st_hit(st_x0,y+128,st_w0,64,1000+P_SCREENS); } }
        cy=y+192+28;
        if(g_bl.ok){ y=st_card(cy,64,"Brightness"); { char b[24],*p=b; p=udec(p,(u32)(g_bl.cur<0?0:g_bl.cur)); *p++='%'; *p=0; st_rowtext(y,"Screen brightness",b); }
            int bw=st_w0-320; if(bw<140)bw=140; int bx=X1-bw-10; st_blx=bx; st_blw=bw; int fw=(g_bl.cur<0?0:g_bl.cur)*bw/100;
            fill_round(bx,y+28,bw,8,4,15,rgb(226,229,236)); if(fw>0)fill_round(bx,y+28,fw,8,4,15,ACC); fill_circle(bx+fw,y+33,11,rgb(200,204,216)); fill_circle(bx+fw,y+32,10,rgb(255,255,255)); fill_circle(bx+fw,y+32,4,ACC);
            st_hit(bx-12,y+14,bw+24,36,120); cy=y+64+28; }
        y=st_card(cy,64*3,"Performance"); st_rowtext(y,"Mouse acceleration","Moves faster when you move the mouse quickly"); { int w=st_segw(MA,3); st_seg(X1-w,y+17,MA,3,g_mouse_accel,65); } st_sep(y+64);
        st_rowtext(y+64,"2D acceleration","Use SSE for fills and frame copies"); st_toggle(X1-44,y+84,g_accel2d,68); st_sep(y+128);
        st_rowtext(y+128,"Performance overlay","Show an FPS counter in the top-right corner"); st_toggle(X1-44,y+148,g_fps_overlay,70);
    } else if(g_setcat==4){
        { extern int g_lid_action, g_pbtn_action; extern anode* g_lid; static const char* LA[3]={"Turn off screen","Sleep","Do nothing"}; static const char* PA[3]={"Show menu","Sleep","Shut down"};
          int rows=g_lid?2:1; int y=st_card(cy,64*rows,"Lid and power button"); int ry=y;
          if(g_lid){ st_rowtext(ry,"When I close the lid","The screen comes back when you open it"); { int w=st_segw(LA,3); st_seg(X1-w,ry+17,LA,3,g_lid_action,360); } st_sep(ry+64); ry+=64; }
          st_rowtext(ry,"When I press the power button","Press it twice to shut down from the menu"); { int w=st_segw(PA,3); st_seg(X1-w,ry+17,PA,3,g_pbtn_action,363); }
          cy=y+64*rows+28; }
        battery_tick(); int y=bat_present?st_card(cy,150,0):cy;
        if(bat_present){ char b[16],*p=b; p=udec(p,(u32)bat_pct); *p++='%'; *p=0; pdstr(st_x0+24,y+22,b,INK,3);
            pdstr(st_x0+24,y+72,bat_charging?"Plugged in, charging":"On battery",bat_charging?rgb(40,150,90):rgb(150,110,70),1);
            int bw=st_w0-48; fill_round(st_x0+24,y+98,bw,16,8,15,rgb(230,233,240)); fill_round(st_x0+24,y+98,(int)bat_pct*bw/100,16,8,15,bat_charging?rgb(80,190,120):(bat_pct<=20?rgb(230,90,90):ACC));
            { char b[64],*p=b; if(bat_via_ec==2){ if(g_acb_mins>=0){ p=udec(p,(u32)(g_acb_mins/60)); p=scopy(p," h "); p=udec(p,(u32)(g_acb_mins%60)); p=scopy(p,bat_charging?" min until full - ":" min left - "); } p=scopy(p,"reported by the ACPI firmware"); } else p=scopy(p,bat_via_ec?"Read from the Embedded Controller":"Estimated"); *p=0;
              pdstr(st_x0+24,y+124,b,rgb(130,138,156),1); }
            int rows=0; for(int i=0;i<g_acb_n;i++) if(g_acb[i].present)rows++;
            if(bat_via_ec==2&&rows){ cy=y+150+28; y=st_card(cy,64*rows,"Battery details"); int ry=y;
                for(int i=0;i<g_acb_n;i++){ acbat* b2=&g_acb[i]; if(!b2->present)continue; char t[64],*p=t; p=scopy(p,b2->model[0]?b2->model:"Battery"); if(b2->oem[0]){ p=scopy(p,"  "); p=scopy(p,b2->oem); } *p=0;
                    char d[80],*q=d; u32 cap=b2->full!=0xFFFFFFFFu?b2->full:0; if(cap&&b2->design&&b2->design!=0xFFFFFFFFu){ q=scopy(q,"Health "); q=udec(q,(u32)((u64)cap*100/b2->design)); q=scopy(q,"%  ("); q=udec(q,cap); q=scopy(q," of "); q=udec(q,b2->design); q=scopy(q,b2->mwh?" mWh)":" mAh)"); }
                    if(b2->cycles&&b2->cycles!=0xFFFFFFFFu){ q=scopy(q,"   "); q=udec(q,b2->cycles); q=scopy(q," cycles"); } *q=0;
                    st_rowtext(ry,t,d); ry+=64; if(ry<y+64*rows)st_sep(ry); } } }
        else { int cx=st_x0+st_w0/2, by=y+90; fill_round(cx-40,by,72,40,8,15,ST_DIM); fill_round(cx-37,by+3,66,34,6,15,ST_BG); fill_round(cx+32,by+12,6,16,3,15,ST_DIM);
            for(int i=0;i<=30;i++) fill_rect(cx-19+i,by+31-i*22/30,3,3,ST_DIM);
            st_bold_ctr(cx,by+66,"No battery detected",ST_FG); pdstr_ctr(cx,by+94,"Running on mains power, or the battery can't be read.",ST_DIM,1); pdstr_ctr(cx,by+116,"Desktops and virtual machines usually have none.",ST_DIM,1); }
    } else if(g_setcat==5){
        gdisk_ensure(); int y=st_card(cy,120,"System disk");
        if(g_dktype!=DK_NONE){ char b[64],*p=b; p=scopy(p,gdisk_name()); p=scopy(p," disk"); *p=0; pdstr(st_x0+20,y+16,b,INK,1);
            p=b; p=st_mb(p,(u64)g_dksectors*512ULL); if(g_dktype==DK_NVME&&g_nvme_bsz>512)p=scopy(p,"   4K sectors"); *p=0; pdstr(X1-pw_str(b,1),y+16,b,rgb(120,128,146),1);
            long used=0; int nf=0; for(int k=0;k<FS_N;k++)if(FS[k].used){ used+=FS[k].len; nf++; }
            int bw=st_w0-40; fill_round(st_x0+20,y+46,bw,12,6,15,rgb(230,233,240)); if(disk_ok){ u64 t4=0,f4=0; n4_usage(&t4,&f4); long cap=t4?(long)t4:(long)g_dksectors*512L; if(t4)used=(long)(t4-f4); if(cap<=0)cap=1; int fw=(int)((long long)used*bw/cap); if(fw<6)fw=6; fill_round(st_x0+20,y+46,fw,12,6,15,ACC); }
            p=b; if(disk_ok){ p=udec(p,(u32)nf); p=scopy(p," files, "); p=udec(p,(u32)(used/1024)); p=scopy(p," KB in "); p=scopy(p,g_nvx_note); } else { p=scopy(p,"Files are kept in memory only - "); p=scopy(p,g_nvx_note[0]?g_nvx_note:"no data partition"); } *p=0;
            pdstr(st_x0+20,y+72,b,disk_ok?rgb(40,150,90):rgb(170,120,60),1); }
        else { pdstr(st_x0+20,y+20,"No disk found",INK,1); pdstr(st_x0+20,y+48,"No IDE, SATA or NVMe disk was detected.",rgb(130,138,156),1); }
        cy=y+120+28;
        y=st_card(cy,64,"Removable"); { char b[64],*p=b; if(msd_ok){ p=scopy(p,"USB drive, "); p=st_mb(p,(u64)(msd_lastlba+1)*512ULL); } else p=scopy(p,"No USB drive connected"); *p=0; st_rowtext(y,b,msd_ok?"Mounted - open it in File Explorer":"Plug in a USB stick to use it"); }
        cy=y+64+28;
        y=st_card(cy,64,"Install"); st_rowtext(y,"Install NoovexOS","Put this system on a disk so it starts without the ISO"); st_btn(X1-120,y+16,120,"Open Setup",95,1);
    } else if(g_setcat==6){
        int tw=(st_w0-40-5*10)/6; if(tw<70)tw=70; int th=64;
        int y=st_card(cy,th+50,"Theme");
        for(int k=0;k<6;k++){ const TTheme* t=&TTH[k]; int bx=st_x0+20+k*(tw+10), by=y+16; if(bx+tw>st_x0+st_w0-10)break;
            if(k==g_tth)fill_round(bx-3,by-3,tw+6,th+6,11,15,ACC); fill_round(bx,by,tw,th,9,15,t->bg); fill_round(bx,by,tw,14,9,1|2,t->bar);
            fill_circle(bx+9,by+7,2,rgb(236,106,100)); fill_circle(bx+16,by+7,2,rgb(240,190,80)); fill_circle(bx+23,by+7,2,rgb(110,200,120));
            pdstr(bx+8,by+22,"$",t->green,1); fill_rect(bx+20,by+27,tw-40,3,t->fg); fill_rect(bx+8,by+42,tw-30,3,t->dim); fill_rect(bx+8,by+54,tw-50,3,t->blue); fill_rect(bx+tw-26,by+52,7,11,t->cur);
            pdstr_ctr(bx+tw/2,by+th+8,t->name,k==g_tth?INK:rgb(120,128,146),1); st_hit(bx,by,tw,th,80+k); }
        cy=y+th+50+16;
        { static const char* OP[4]={"Off","Low","Medium","High"}; int sel=g_topa>=100?0:g_topa>=90?1:g_topa>=80?2:3;
          y=st_card(cy,128,"Window"); st_rowtext(y,"Transparency","See the desktop through the terminal"); { int w=st_segw(OP,4); st_seg(X1-w,y+17,OP,4,sel,110); } st_sep(y+64);
          st_rowtext(y+64,"Blur behind","Soften what shows through, like frosted glass"); st_toggle(X1-44,y+84,g_tblur,114); cy=y+128+16; }
        static const char* FN[2]={"Smooth","Pixel"}; static const char* CS[3]={"Block","Bar","Underline"};
        y=st_card(cy,64*3,"Text"); st_rowtext(y,"Font","Anti-aliased or classic bitmap glyphs"); { int w=st_segw(FN,2); st_seg(X1-w,y+17,FN,2,g_tfont?0:1,85); } st_sep(y+64);
        st_rowtext(y+64,"Cursor",0); { int w=st_segw(CS,3); st_seg(X1-w,y+81,CS,3,g_tcur,87); } st_sep(y+128);
        st_rowtext(y+128,"Status bar","Shows the user, current folder and size"); st_toggle(X1-44,y+148,g_tstatus,90);

    } else if(g_setcat==8){
        extern int g_lap_kind, g_lap_kbd, g_lap_kbdmax, g_lap_chg, g_lap_chg_ok, g_lap_tp_off; extern const char* g_lap_name; extern char g_prn_status[]; extern u32 g_prn_ip; extern char g_prn_model[];
        extern int g_syn_ready, g_etp_ready; extern int net_ok; extern u32 my_ip; extern int g_wnd_up; extern int g_snd_state; extern int gdisk_count(void); extern u32 gdisk_size_of(int); extern const char* gdisk_name_of(int); extern int gdisk_current(void);
        static const char* TABS[7]={"Input","USB","Storage","Network","Media","Printers","Laptop"};
        if(!st_notabs){ int w=st_segw(TABS,7); st_seg(st_x0,cy,TABS,7,g_devtab,140); cy+=52; }
        char b[160],*p; const char* rt[24]; char rd[24][96]; u32 rc[24]; int n=0;
        #define DROW(T,COL) do{ if(n<24){ rt[n]=(T); rc[n]=(COL); n++; } }while(0)
        u32 OK=rgb(40,140,80), WARN=rgb(200,120,40), OFF=rgb(140,146,160);
        if(!pci_n)pci_scan();
        if(g_devtab==0){
            DROW("Keyboard",OK); scopy(rd[n-1],"Built-in keyboard, PS/2 controller i8042")[0]=0;
            DROW("Touchpad",g_lap_tp_off?WARN:OK); p=rd[n-1]; p=scopy(p,g_tp.ok?(g_tp.mode==1?"Precision touchpad, HID over I2C":g_tp.proto==1?"ELAN touchpad, I2C":"HID touchpad over I2C"):g_syn_ready?"Synaptics touchpad, PS/2 absolute mode":g_etp_ready?"ELAN touchpad, PS/2":"PS/2 pointing device"); if(g_lap_tp_off)p=scopy(p,"  (switched off)"); *p=0;
            for(int k=0;k<g_usbdev_n&&n<24;k++) if(g_usbdev[k].used&&g_usbdev[k].hid){ DROW(g_usbdev[k].kind,OK); p=rd[n-1]; p=scopy(p,g_usbdev[k].name[0]?g_usbdev[k].name:"USB HID device"); p=scopy(p,", USB"); *p=0; } }
        else if(g_devtab==1){
            for(int k=0;k<pci_n&&n<24;k++) if(pci_cls[k]==0x0C&&pci_sub[k]==0x03){ DROW("USB controller",OK); p=rd[n-1]; p=hx(p,pci_ven[k],4); *p++=':'; p=hx(p,pci_dev[k],4); p=scopy(p,"  PCI bus "); p=udec(p,pci_bus[k]); *p=0; }
            for(int k=0;k<g_usbdev_n&&n<24;k++) if(g_usbdev[k].used){ DROW(g_usbdev[k].kind,g_usbdev[k].cam?WARN:OK); p=rd[n-1]; p=scopy(p,g_usbdev[k].name[0]?g_usbdev[k].name:"(no name)"); p=scopy(p,"  "); p=hx(p,g_usbdev[k].vid,4); *p++=':'; p=hx(p,g_usbdev[k].pid,4); p=scopy(p,"  port "); p=udec(p,(u32)g_usbdev[k].port+1); if(g_ticks-g_usbdev[k].t<1000)p=scopy(p,"  NEW"); *p=0; }
            if(msd_ok){ DROW("USB drive",OK); scopy(rd[n-1],ufat_ok?"Mass storage, FAT file system mounted":"Mass storage, no FAT file system")[0]=0; } }
        else if(g_devtab==2){
            for(int k=0;k<gdisk_count()&&n<24;k++){ DROW(gdisk_name_of(k),k==gdisk_current()?OK:OFF); p=rd[n-1]; u64 mb=(u64)gdisk_size_of(k)*512ULL/1048576ULL; if(mb>=1024){ p=udec(p,(u32)(mb/1024)); p=scopy(p," GB"); } else { p=udec(p,(u32)mb); p=scopy(p," MB"); } if(k==gdisk_current())p=scopy(p,"  (in use)"); *p=0; }
            for(int k=0;k<pci_n&&n<24;k++) if(pci_cls[k]==0x01){ DROW(pci_sub[k]==0x08?"NVMe controller":pci_sub[k]==0x06?"SATA controller":pci_sub[k]==0x04?"RAID / Intel RST controller":pci_sub[k]==0x01?"IDE controller":"Storage controller",OK); p=rd[n-1]; p=hx(p,pci_ven[k],4); *p++=':'; p=hx(p,pci_dev[k],4); *p=0; }
            if(!n){ DROW("No disks",WARN); scopy(rd[0],"No IDE, SATA, NVMe or VirtIO disk was found")[0]=0; } }
        else if(g_devtab==3){
            for(int k=0;k<pci_n&&n<24;k++) if(pci_cls[k]==0x02){ int wl=pci_sub[k]==0x80; DROW(wl?"Wi-Fi adapter":"Ethernet adapter",wl?(g_wnd_up?OK:OFF):(net_ok?OK:OFF)); p=rd[n-1]; p=scopy(p,(char*)nic_name(pci_ven[k],pci_dev[k])); p=scopy(p,wl?(g_wnd_up?"  connected":"  not connected"):(net_ok?"  connected":"  no link")); *p=0; }
            if(net_ok||g_wnd_up){ DROW("IP address",OK); p=rd[n-1]; for(int q=3;q>=0;q--){ p=udec(p,(my_ip>>(8*(3-q)))&0xFF); if(q)*p++='.'; } *p=0; }
            if(!n){ DROW("No network adapter",WARN); scopy(rd[0],"No supported Ethernet or Wi-Fi card was found")[0]=0; } }
        else if(g_devtab==4){
            for(int k=0;k<pci_n&&n<24;k++) if(pci_cls[k]==0x03){ DROW("Graphics",OK); p=rd[n-1]; p=scopy(p,pci_ven[k]==0x10DE?"NVIDIA":pci_ven[k]==0x8086?"Intel":pci_ven[k]==0x1002?"AMD":"GPU"); p=scopy(p,"  "); p=hx(p,pci_ven[k],4); *p++=':'; p=hx(p,pci_dev[k],4); *p=0; }
            DROW("Display",OK); p=rd[n-1]; p=udec(p,W); *p++='x'; p=udec(p,H); p=scopy(p,"  "); p=udec(p,BPP); p=scopy(p,"-bit framebuffer"); *p=0;
            for(int k=0;k<pci_n&&n<24;k++) if(pci_cls[k]==0x04){ DROW("Sound card",g_snd_state?OK:OFF); p=rd[n-1]; p=scopy(p,g_snd_state&&g_snd.name?g_snd.name:pci_sub[k]==0x03?"HD Audio":"Audio"); p=scopy(p,g_snd_state?"  driver active":"  driver not started"); *p=0; }
            for(int k=0;k<g_usbdev_n&&n<24;k++) if(g_usbdev[k].used&&g_usbdev[k].cam){ DROW("Camera",WARN); p=rd[n-1]; p=scopy(p,g_usbdev[k].name[0]?g_usbdev[k].name:"USB video camera"); p=scopy(p,"  detected, no video yet"); *p=0; } }
        else if(g_devtab==5){
            DROW("Network printer",g_prn_ip?OK:OFF); p=rd[n-1]; p=scopy(p,g_prn_model[0]?g_prn_model:g_prn_status); *p=0;
            if(g_prn_ip){ DROW("Status",OK); scopy(rd[n-1],g_prn_status)[0]=0; }
            for(int k=0;k<g_usbdev_n&&n<24;k++) if(g_usbdev[k].used&&g_usbdev[k].prn){ DROW("USB printer",WARN); p=rd[n-1]; p=scopy(p,g_usbdev[k].name[0]?g_usbdev[k].name:"USB printer"); p=scopy(p,"  use it over the network"); *p=0; } }
        else if(g_devtab==6){
            if(!g_lap_kind){ DROW("No laptop extras",OFF); scopy(rd[0],"No Lenovo ThinkPad/IdeaPad or ASUS firmware interface found")[0]=0; }
            else { DROW(g_lap_name,OK); scopy(rd[0],"Hotkeys, keyboard light and charge limit through ACPI")[0]=0; } }
        int y=st_card(cy,n*44+8,0);
        for(int r=0;r<n;r++){ int ry=y+6+r*44; if(r)st_sep(ry); fill_circle(st_x0+28,ry+22,4,rc[r]); pdstr(st_x0+44,ry+5,rt[r],ST_FG,1); pdstr(st_x0+44,ry+23,rd[r],ST_DIM,1); }
        cy=y+n*44+8+24;
        if(g_devtab==5&&g_prn_ip){ st_btn(st_x0,cy,150,"Print test page",134,1); cy+=48; }
        if(g_devtab==6&&g_lap_kind){ static const char* KL[4]={"Off","Low","Medium","High"}; static const char* KL3[3]={"Off","Low","High"};
            y=st_card(cy,128,0);
            st_rowtext(y,"Keyboard light",g_lap_kbdmax?0:"Not available on this laptop");
            if(g_lap_kbdmax==2){ int w=st_segw(KL3,3); st_seg(X1-w,y+17,KL3,3,g_lap_kbd<0?0:g_lap_kbd,130); } else if(g_lap_kbdmax>=3){ int w=st_segw(KL,4); st_seg(X1-w,y+17,KL,4,g_lap_kbd<0?0:g_lap_kbd,130); } else if(g_lap_kbdmax==1){ st_toggle(X1-44,y+20,g_lap_kbd>0,135); }
            st_sep(y+64); st_rowtext(y+64,"Battery charge limit",g_lap_chg_ok?(g_lap_kind==2?"Conservation mode":"Stop charging at 80%"):"Not available on this laptop"); if(g_lap_chg_ok)st_toggle(X1-44,y+84,g_lap_chg,136); }
        #undef DROW
    } else {
        int y=st_card(cy,110,0); draw_logo(st_x0+62,y+22,54,ACC,256); st_bold(st_x0+120,y+34,"NoovexOS 64",ST_FG);
        { char b[48],*p=b; p=scopy(p,"Version "); p=scopy(p,(char*)g_buildsig.version); p=scopy(p,"  -  built from scratch"); *p=0; pdstr(st_x0+120,y+64,b,rgb(120,128,146),1); }
        cy=y+110+28; y=st_card(cy,64*4,"Hardware");
        char b[96],*p;
        st_rowtext(y,"Processor",0); { char pb[48]; int n=slen(g_brand); if(n>(st_w0-200)/8){ int m=(st_w0-200)/8; for(int q=0;q<m;q++)pb[q]=g_brand[q]; pb[m]=0; } else scopy(pb,g_brand)[0]=0; pdstr(X1-pw_str(pb,1),y+24,pb,rgb(90,98,116),1); } st_sep(y+64);
        st_rowtext(y+64,"Memory",0); p=b; p=udec(p,g_ram_mb); p=scopy(p," MB"); *p=0; pdstr(X1-pw_str(b,1),y+88,b,rgb(90,98,116),1); st_sep(y+128);
        st_rowtext(y+128,"Display",0); p=b; p=udec(p,W); *p++='x'; p=udec(p,H); p=scopy(p,"  "); p=udec(p,BPP); p=scopy(p,"-bit"); *p=0; pdstr(X1-pw_str(b,1),y+152,b,rgb(90,98,116),1); st_sep(y+192);
        st_rowtext(y+192,"Kernel",0); p=b; p=udec(p,g_buildsig.image_size/1024); p=scopy(p," KB, x86-64 long mode"); *p=0; pdstr(X1-pw_str(b,1),y+216,b,rgb(90,98,116),1);
    }

    g_setcat=g_setcat_sv; }

void settings_click(int px,int py){
    for(int h=st_hn-1;h>=0;h--) if(in_rect(px,py,st_hx[h],st_hy[h],st_hw[h],st_hh[h])){ int id=st_hid[h];
        if(id>=1200&&id<1400){ st_focus_click(id); return; }
        if(id>=600&&id<700){ extern int acct_ui_click(int); acct_ui_click(id); return; }
        if(id>=140&&id<147){ g_devtab=id-140; return; }
        if(id>=130&&id<134){ extern void laptop_kbd_set(int); laptop_kbd_set(id-130); return; }
        if(id==134){ extern int prn_test_page(void); prn_test_page(); return; }
        if(id==135){ extern int g_lap_kbd; extern void laptop_kbd_set(int); laptop_kbd_set(g_lap_kbd>0?0:1); return; }
        if(id==136){ extern int g_lap_chg; extern void laptop_charge_set(int); laptop_charge_set(!g_lap_chg); return; }
        if(id==500){ if(g_fedit){ if(g_fedit==1)g_fedit=0; else fe_commit(); } if(g_wifi_entry){ extern void wifi_entry_cancel(void); wifi_entry_cancel(); } g_st_qf=1; return; }
        g_st_qf=0;
        if(id>=100&&id<109){ g_stpg=id-100; g_wifi_entry=0; g_st_qn=0; g_st_q[0]=0; return; }
        if(id==1999){ int pp=pg_parent(g_stpg); if(pp>=0)g_stpg=pp; return; }
        if(id>=1000&&id<1200){ g_stpg=id-1000; g_st_qn=0; g_st_q[0]=0; return; }
        if(id>=2000&&id<2000+NWIN){ open_win(id-2000); return; }
        if(id>=2200&&id<2200+PF_MAX){ const pf_app* a=progfiles_get(id-2200); if(!a)return; if(a->win>=0)open_win(a->win); else { extern void app_launch(int); app_launch(1100+a->xi); } return; }
        if(id>=2300&&id<2300+PF_MAX){ const pf_app* a=progfiles_get(id-2300); if(!a||a->kind!=PF_PACKAGE)return; extern int acct_is_admin(int); extern void apt_cmd(const char*); if(g_uid!=0&&!acct_is_admin(g_uid)){ toast("Only an administrator can uninstall apps"); return; }
            char cmd[80],*p=cmd; p=scopy(p,"remove -y "); p=scopy(p,a->pkg); *p=0; char nm[48]; { int k=0; for(;a->name[k]&&k<47;k++)nm[k]=a->name[k]; nm[k]=0; } int u=g_uid; g_uid=0; apt_cmd(cmd); g_uid=u; extern void progfiles_sync(int); progfiles_sync(1); { char t[96],*r=t; r=scopy(r,nm); r=scopy(r," was uninstalled"); *r=0; toast(t); } return; }
        if(id==300){ extern int g_gfx; extern int igpu_draw_mode(int); igpu_draw_mode(g_gfx?0:1); return; }
        if(id==301){ extern int g_gfx_fx; g_gfx_fx^=1; return; } if(id==302){ extern int g_gfx_srec; g_gfx_srec^=1; return; }
        if(id==303){ extern int g_wall_fx; g_wall_fx^=1; build_bg(); return; } if(id==304){ extern int g_wall_light; g_wall_light^=1; return; }
        if(id==310||id==311){ extern int g_pres_eng; g_pres_eng=id-310; return; }
        if(id==312){ extern int g_igpu_flip; extern int igpu_flip_on(void); extern void igpu_flip_off(const char*); if(g_igpu_flip)igpu_flip_off("turned off in Settings"); else igpu_flip_on(); return; }
        if(id==313){ extern int frame_check_cmd(void); frame_check_cmd(); return; }
        if(id==330){ extern int g_nvd_auto; extern void nvd_set_auto(int); nvd_set_auto(!g_nvd_auto); return; }
        if(id==331){ nvgsp_info_t ni; nvgsp_info(&ni); extern int nvgsp_present_set(int); nvgsp_present_set(!ni.present); return; }
        if(id==332){ extern int g_nvd_on; extern int nvd_desktop(void); extern char g_nvd_why[]; extern void gfx_stop(const char*); if(g_nvd_on)gfx_stop("turned off in Settings"); else if(nvd_desktop())toast(g_nvd_why); return; }
        if(id==333){ char m[120]; if(nvgsp_off()){ nvgsp_on(m,120); toast(m); } else { nvgsp_all_off("turned off in Settings"); toast("NVIDIA drawing off - the CPU draws"); } return; }
        if(id==335){ nvgsp_monitors(); return; }
        if(id==334){ extern void nvd_run_request(void); nvd_run_request(); toast("Starting the NVIDIA GPU..."); return; }
        if(id==322){ extern void snd_rec_gain(int); int pc=(px-st_mlx)*100/(st_mlw>0?st_mlw:1); snd_rec_gain(pc); return; }
        if(id==323){ open_win(W_RECORDER); return; }
        if(id==320){ int pc=(px-st_blx)*100/(st_blw>0?st_blw:1); if(pc<0)pc=0; if(pc>100)pc=100; g_snd_vol=pc; if(g_snd_state)g_snd.volume(pc); return; }
        if(id==321){ static int sv=50; if(g_snd_vol){ sv=g_snd_vol; g_snd_vol=0; } else g_snd_vol=sv?sv:50; if(g_snd_state)g_snd.volume(g_snd_vol); return; }
        if(id==1){ g_showsec^=1; return; } if(id==4){ extern int g_kbd_layout; extern void kbd_set_layout(int); kbd_set_layout((g_kbd_layout+1)%6); return; } if(id==5){ extern int g_krep_speed; extern void kbd_set_repeat(int); kbd_set_repeat((g_krep_speed+1)%3); return; } if(id==2){ power_reboot(); return; } if(id==3){ panic(0,"Crash test triggered from Settings."); return; }
        if(id>=10&&id<10+NWALL){ g_theme=id-10; g_wallmode=0; build_bg(); return; }
        if(id==19){ g_wallmode=2; build_bg(); return; }
        if(id>=20&&id<25){ g_accent=id-20; ACC=ACV[g_accent]; ACC2=mix(ACC,rgb(0,0,0),60); build_bg(); return; }
        if(id>=30&&id<35){ g_cursor=id-30; return; }
        if(id==40){ wifi_gui_scan(1); return; }
        if(id==41){ extern int wifi_entry_key(int); if(g_wifi_entry)wifi_entry_key('\n'); return; }
        if(id>=50&&id<58){ extern void wifi_gui_pick(int); wifi_gui_pick(id-50); return; }
        if(id>=1400&&id<1600){ if(id<1400+WIFI_MAXNET){ wifi_saved_forget(g_wnets[id-1400].ssid); return; } if(id>=1500&&id<1500+WIFI_MAXNET){ wifi_saved_prio(g_wnets[id-1500].ssid,1); return; } if(id>=1520&&id<1520+WIFI_MAXNET){ wifi_saved_prio(g_wnets[id-1520].ssid,-1); return; }
            if(id>=1540&&id<1540+WIFI_MAXNET){ g_wnets[id-1540].autoc^=1; wifi_saved_save(); return; }
            if(id==1460){ extern void wifi_user_power(int); if(g_wm_state==WM_OFF){ wifi_user_power(1); if(wifi_up()==0)wifi_radio_on(); } else { wifi_user_power(0); wifi_radio_off(); } return; } if(id==1461){ g_wm_auto^=1; wifi_saved_save(); return; } if(id==1462){ g_wm_5g^=1; wifi_saved_save(); return; }
            if(id==1463){ wifi_powersave(!g_wm_ps); return; } if(id==1464){ g_wifi_ra^=1; return; }
            if(id==1465){ extern char g_wifi_hid[33]; extern int g_wifi_hidlen; g_wifi_hidlen=0; g_wifi_hid[0]=0; g_wifi_sel=-2; g_wifi_entry=3; return; }
            if(id==1466){ g_stpg=P_NETINFO; return; } return; }
        if(id>=60&&id<63){ static const int hz[3]={30,60,120}; g_refresh_hz=hz[id-60]; return; }
        if(id==63||id==64){ g_panel_oled=(id==64); return; }
        if(id>=65&&id<68){ g_mouse_accel=id-65; return; }
        if(id>=400&&id<420&&g_stpg==P_SERVICES){ extern int svc_info(int,const char**,const char**,const char**,int*,int*); extern int svc_set(int,int); const char *a,*b2,*c; int on=0,req=0; if(svc_info(id-400,&a,&b2,&c,&on,&req))svc_set(id-400,!on); return; }
        if(id==420){ extern void clock_sync_now(void); clock_sync_now(); return; }
        if(id>=360&&id<366){ extern int g_lid_action, g_pbtn_action; extern void power_prefs_save(void); if(id<363)g_lid_action=id-360; else g_pbtn_action=id-363; power_prefs_save(); return; }
        if(id==68){ g_accel2d^=1; return; } if(id==70){ g_fps_overlay^=1; return; }
        if(id>=80&&id<86){ g_tth=id-80; return; }
        if(id>=110&&id<114){ static const int op[4]={100,90,80,65}; g_topa=op[id-110]; return; } if(id==114){ g_tblur^=1; return; } if(id==85||id==86){ g_tfont=(id==85); return; }
        if(id>=87&&id<90){ g_tcur=id-87; return; } if(id==90){ g_tstatus^=1; term_geom(); return; }
        if(id==95){ ip_open(ST_DESKTOP); return; }
        if(id==400){ if(!g_upd.busy){ g_upd.checked=0; g_upd.want_check=1; } return; }
        if(id==401){ upd_install(0); return; }
        if(id==402){ upd_use_new(); return; }
        if(id==403){ upd_go_back(); return; }
        if(id==120){ int pc=(px-st_blx)*100/(st_blw>0?st_blw:1); if(pc<0)pc=0; if(pc>100)pc=100; bl_set(pc); return; }
        return; }
}

