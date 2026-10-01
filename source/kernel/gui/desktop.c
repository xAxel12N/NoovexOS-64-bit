/* =========================================================================
   Desktop icons / wallpaper / taskbar / compositor
   ========================================================================= */
/* ---- dock icons: consistent rounded tiles, flat glyphs ---- */
#define ICT 9
static void itile(int x,int y,u32 c){ fill_round(x+4,y+4,40,40,ICT,15,c); }
static void ic_folder(int x,int y){ itile(x,y,rgb(74,118,222));
    fill_round(x+11,y+15,10,4,2,1|2,rgb(255,214,120)); fill_round(x+10,y+18,24,14,3,15,rgb(255,203,92)); fill_round(x+10,y+18,24,4,2,1|2,rgb(255,224,150)); }
static void ic_term(int x,int y){ itile(x,y,rgb(26,30,42));
    dchar(x+13,y+15,'>',rgb(120,240,150),2); fill_rect(x+24,y+27,10,3,rgb(120,240,150)); }
static void ic_edit(int x,int y){ itile(x,y,rgb(96,110,232));
    fill_round(x+13,y+12,22,24,3,15,rgb(248,250,255)); for(int k=0;k<4;k++)fill_rect(x+17,y+17+k*5,14,2,rgb(150,170,220));
    line_bl(x+28,y+34,x+37,y+25,rgb(240,170,50),256,3); fill_rect(x+35,y+23,4,4,rgb(150,110,40)); }
static void ic_web(int x,int y){ itile(x,y,rgb(40,140,230)); int cx=x+24,cy=y+24;
    fill_circle(cx,cy,13,rgb(70,168,246)); for(int k=-1;k<=1;k++) line_bl(cx-12,cy+k*7,cx+12,cy+k*7,rgb(240,248,255),150,0);
    line_bl(cx,cy-13,cx,cy+13,rgb(240,248,255),160,0); fill_circle(cx,cy,2,rgb(255,255,255)); }
static void ic_calc(int x,int y){ itile(x,y,rgb(60,66,84));
    fill_round(x+12,y+11,24,8,2,15,rgb(150,210,180)); for(int r=0;r<3;r++)for(int c=0;c<3;c++) fill_round(x+13+c*8,y+23+r*6,5,4,1,15,rgb(150,158,178)); }
static void ic_clock(int x,int y){ itile(x,y,rgb(250,251,255)); int cx=x+24,cy=y+24;
    fill_circle(cx,cy,15,ACC); fill_circle(cx,cy,12,rgb(252,253,255));
    for(int t=0;t<12;t++){ int a=t*30; int px=cx+icos(a-90)*10/1000,py=cy+isin(a-90)*10/1000; fill_rect(px-1,py-1,2,2,rgb(120,128,146)); }
    line_bl(cx,cy,cx+icos(-30)*6/1000,cy+isin(-30)*6/1000,rgb(40,46,64),255,2);
    line_bl(cx,cy,cx+icos(60)*9/1000,cy+isin(60)*9/1000,rgb(40,46,64),255,1); fill_circle(cx,cy,2,rgb(220,70,90)); }
static void ic_calendar(int x,int y){ itile(x,y,rgb(250,251,255));
    fill_round(x+10,y+11,28,26,4,15,rgb(255,255,255)); fill_round(x+10,y+11,28,9,4,1|2,rgb(226,75,74));
    fill_rect(x+16,y+9,3,6,rgb(255,255,255)); fill_rect(x+29,y+9,3,6,rgb(255,255,255));
    dstr_ctr(x+24,y+23,"30",rgb(60,66,84),1); }
static void ic_sysmon(int x,int y){ itile(x,y,rgb(26,30,42));
    int bx=x+13; u32 g=rgb(90,222,150); int hs[5]={8,15,7,18,12}; for(int k=0;k<5;k++) fill_rect(bx+k*5,y+32-hs[k],3,hs[k],g);
    fill_rect(x+13,y+34,20,2,rgb(90,100,120)); }
static void ic_snake(int x,int y){ itile(x,y,rgb(46,152,72));
    u32 s=rgb(150,236,180); fill_round(x+12,y+26,7,7,2,15,s); fill_round(x+18,y+26,7,7,2,15,s); fill_round(x+24,y+26,7,7,2,15,s); fill_round(x+24,y+20,7,7,2,15,s); fill_round(x+30,y+20,8,8,2,15,rgb(190,250,210));
    fill_circle(x+34,y+23,1,rgb(20,40,26)); fill_circle(x+17,y+15,4,rgb(228,76,74)); }
static void ic_flappy(int x,int y){ itile(x,y,rgb(96,176,236));
    int cx=x+22,cy=y+25; fill_circle(cx,cy,9,rgb(250,208,60)); fill_round(x+11,y+25,7,5,2,15,rgb(236,182,44));
    fill_circle(cx+4,cy-3,2,rgb(30,30,34)); fill_rect(cx+8,cy-2,5,4,rgb(240,132,40)); }
static void ic_paint(int x,int y){ itile(x,y,rgb(250,251,255));
    fill_circle(x+14,y+15,3,rgb(228,76,74)); fill_circle(x+22,y+13,3,rgb(240,200,50)); fill_circle(x+30,y+15,3,rgb(90,200,110)); fill_circle(x+18,y+22,3,rgb(50,120,224)); fill_circle(x+27,y+22,3,rgb(150,90,220));
    line_bl(x+14,y+35,x+30,y+27,rgb(120,90,60),256,3); fill_round(x+28,y+24,7,7,2,15,rgb(228,76,74)); }
static void ic_gear(int x,int y){ itile(x,y,rgb(122,130,148)); int cx=x+24,cy=y+24;
    for(int a=0;a<8;a++){ int dx=0,dy=0; if(a==0)dy=-13;if(a==4)dy=13;if(a==2)dx=13;if(a==6)dx=-13; if(a==1){dx=9;dy=-9;}if(a==3){dx=9;dy=9;}if(a==5){dx=-9;dy=9;}if(a==7){dx=-9;dy=-9;} fill_rect(cx+dx-3,cy+dy-3,6,6,rgb(238,240,246)); }
    fill_circle(cx,cy,10,rgb(238,240,246)); fill_circle(cx,cy,4,rgb(122,130,148)); }

static int IC_X=28, IC_W=76, IC_H=76; static int IC_Y[6]={16,110,204,298,392,486};
static int IC_APP[12]={W_FILES,W_TERM,W_EDITOR,W_BROWSER,W_CALC,W_CLOCK,W_CALENDAR,W_SYSMON,W_SNAKE,W_FLAPPY,W_PAINT,W_SETTINGS};
static const char *IC_LBL[12]={"Files","Terminal","Editor","Browser","Calc","Clock","Calendar","Monitor","Snake","Flappy","Paint","Settings"};
static void draw_icon(int k){ int x=IC_X,y=IC_Y[k],ix=x+14;
    if(k==0)ic_folder(ix,y);else if(k==1)ic_term(ix,y);else if(k==2)ic_edit(ix,y);else if(k==3)ic_web(ix,y);else if(k==4)ic_calc(ix,y);else ic_gear(ix,y);
    dstr_ctr(x+38,y+52,IC_LBL[k],rgb(238,242,250),1); }

static void wall_gradient(u32 t,u32 b){ for(u32 y=0;y<H;y++){ u32 c=mix(t,b,(int)((y*256)/H)); for(u32 x=0;x<W;x++) SCENE[(u64)y*SW+x]=c; } }
static void fill_tri(int x0,int y0,int x1,int y1,int x2,int y2,u32 c){
    int t;
    if(y1<y0){t=x0;x0=x1;x1=t;t=y0;y0=y1;y1=t;}
    if(y2<y0){t=x0;x0=x2;x2=t;t=y0;y0=y2;y2=t;}
    if(y2<y1){t=x1;x1=x2;x2=t;t=y1;y1=y2;y2=t;}
    if(y2==y0)return;
    for(int y=y0;y<=y2;y++){ if(y<0||y>=(int)H)continue;
        int xa=x0+(x2-x0)*(y-y0)/(y2-y0), xb;
        if(y<y1) xb = (y1==y0)? x0 : x0+(x1-x0)*(y-y0)/(y1-y0);
        else     xb = (y2==y1)? x1 : x1+(x2-x1)*(y-y1)/(y2-y1);
        int lx=xa<xb?xa:xb, rx=xa<xb?xb:xa; if(lx<0)lx=0; if(rx>=(int)W)rx=(int)W-1;
        u32* row=&SCENE[(u64)y*SW]; for(int x=lx;x<=rx;x++)row[x]=c; }
}
static u32 lp_rng;
static u32 lpr(void){ lp_rng^=lp_rng<<13; lp_rng^=lp_rng>>17; lp_rng^=lp_rng<<5; return lp_rng; }
static void wall_lowpoly(void){
    lp_rng=2246822519u;
    int COLS=14, ROWS=9; int cw=(int)W/COLS, ch=(int)H/ROWS;
    static int vx[16][11], vy[16][11];
    for(int r=0;r<=ROWS;r++) for(int c=0;c<=COLS;c++){
        int jx=(int)(lpr()%((u32)cw?(u32)cw/2+1:1))-cw/4, jy=(int)(lpr()%((u32)ch?(u32)ch/2+1:1))-ch/4;
        int px=c*cw+jx, py=r*ch+jy;
        if(c==0)px=0; if(c==COLS)px=(int)W; if(r==0)py=0; if(r==ROWS)py=(int)H;
        vx[c][r]=px; vy[c][r]=py; }
    for(int r=0;r<ROWS;r++) for(int c=0;c<COLS;c++){
        int td=(c*255/COLS + r*255/ROWS)/2;
        u32 base=mix(rgb(16,26,82), rgb(58,116,224), td);
        u32 c1=mix(base, rgb(96,156,246), (int)(lpr()%34));
        fill_tri(vx[c][r],vy[c][r], vx[c+1][r],vy[c+1][r], vx[c][r+1],vy[c][r+1], c1);
        u32 c2=mix(base, rgb(24,54,140), (int)(lpr()%34));
        fill_tri(vx[c+1][r],vy[c+1][r], vx[c+1][r+1],vy[c+1][r+1], vx[c][r+1],vy[c][r+1], c2); }
}
static u8 wallsrc[640*360*4]; static int wall_have=-1, wall_sw=0, wall_sh=0;
static void wall_decode(int th){
    if(th==wall_have && wall_sw>0) return;
    const unsigned char* jd; int jl;
    if(th==1){ jd=wall_wire_jpg; jl=wall_wire_jpg_len; }
    else if(th==2){ jd=wall_crane_jpg; jl=wall_crane_jpg_len; }
    else if(th==3){ jd=wall_porsche_jpg; jl=wall_porsche_jpg_len; }
    else if(th==4){ jd=wall_field_jpg; jl=wall_field_jpg_len; }
    else if(th==5){ jd=wall_desert_jpg; jl=wall_desert_jpg_len; }
    else { jd=wall_cobalt_jpg; jl=wall_cobalt_jpg_len; }
    int w=0,h=0;
    if(jpeg_decode(jd,jl,wallsrc,(int)sizeof(wallsrc),&w,&h)==0 && w>0 && h>0){ wall_sw=w; wall_sh=h; wall_have=th; }
    else { wall_sw=0; wall_sh=0; wall_have=-1; }
}
static void wall_blit_cover(void){
    if(view_w<=0||view_h<=0){ wall_gradient(rgb(28,34,72),rgb(58,66,120)); return; }
    int sw=view_w, sh=view_h; int s1=(int)W*1000/sw, s2=(int)H*1000/sh; int scn=s1>s2?s1:s2;
    int dw=sw*scn/1000, dh=sh*scn/1000; if(dw<1)dw=1; if(dh<1)dh=1; int ox=((int)W-dw)/2, oy=((int)H-dh)/2;
    for(u32 y=0;y<H;y++){ int syv=((int)y-oy)*sh/dh; if(syv<0)syv=0; if(syv>=sh)syv=sh-1; u8* srow=viewbuf+(u64)syv*sw*4;
        for(u32 x=0;x<W;x++){ int sxv=((int)x-ox)*sw/dw; if(sxv<0)sxv=0; if(sxv>=sw)sxv=sw-1; u8* p=srow+sxv*4; SCENE[(u64)y*SW+x]=rgb(p[0],p[1],p[2]); } }
}
static void wall_thumbs_build(void){
    if(wall_thumb_ready)return;
    for(int k=0;k<6;k++){
        if(k==0){ for(int y=0;y<40;y++)for(int x=0;x<64;x++){ u32 c=mix(rgb(44,54,120),rgb(120,72,150),(x*256)/64); wall_thumb[0][y*64+x]=mix(c,rgb(200,120,90),(y*140)/40); } continue; }
        const unsigned char* jd; int jl;
        if(k==1){jd=wall_wire_jpg;jl=wall_wire_jpg_len;} else if(k==2){jd=wall_crane_jpg;jl=wall_crane_jpg_len;}
        else if(k==3){jd=wall_porsche_jpg;jl=wall_porsche_jpg_len;} else if(k==4){jd=wall_field_jpg;jl=wall_field_jpg_len;}
        else {jd=wall_desert_jpg;jl=wall_desert_jpg_len;}
        int w=0,h=0;
        if(jpeg_decode(jd,jl,wallsrc,(int)sizeof(wallsrc),&w,&h)==0 && w>0 && h>0){
            for(int y=0;y<40;y++){ int sy=y*h/40; if(sy>=h)sy=h-1; u8* srow=wallsrc+(u64)sy*w*4;
                for(int x=0;x<64;x++){ int sx=x*w/64; if(sx>=w)sx=w-1; u8* p=srow+sx*4; wall_thumb[k][y*64+x]=rgb(p[0],p[1],p[2]); } }
        } else { for(int i=0;i<64*40;i++)wall_thumb[k][i]=THSW[k]; }
    }
    wall_thumb_ready=1; wall_have=-1;   /* wallsrc was reused; force wallpaper re-decode */
}
static void draw_wallpaper(int th){
    if(g_safemode){ wall_gradient(rgb(28,34,72),rgb(58,66,120)); return; }
    if(g_wallmode==1 && wall_custom_slot>=0 && FS[wall_custom_slot].used){ if(ensure_img(wall_custom_slot)&&view_w>0){ wall_blit_cover(); return; } }
    if(th==0){ wall_lowpoly(); return; }
    if(th<0)th=0; if(th>5)th=5; wall_decode(th);
    if(wall_sw<=0){ wall_gradient(rgb(38,52,132),rgb(70,80,150)); return; }
    for(u32 y=0;y<H;y++){ u32 sy=(u32)y*(u32)wall_sh/H; if(sy>=(u32)wall_sh)sy=wall_sh-1; u8* srow=wallsrc+(u64)sy*wall_sw*4;
        for(u32 x=0;x<W;x++){ u32 sx=(u32)x*(u32)wall_sw/W; if(sx>=(u32)wall_sw)sx=wall_sw-1; u8* p=srow+sx*4; SCENE[(u64)y*SW+x]=rgb(p[0],p[1],p[2]); } }
}
static void build_bg(void){ draw_wallpaper(g_theme); memcpy(BG,SCENE,(u64)W*H*4); }

/* ---- macOS-style top menu bar + dock ---- */
static int g_applemenu=0;
static int DOCK_x,DOCK_y,DOCK_w,DOCK_h;
#define DOCK_IC 48
#define DOCK_GAP 14
#define DOCK_PAD 14
static const char *AM_LBL[3]={"About This System","Log Out","Restart"};
#define AM_N 3
static int AM_X=8,AM_Y=27,AM_W=190,AM_IH=30;
static int g_bellx=0,g_wsx=0;
/* ---- battery via the Embedded Controller (ports 0x62/0x66), ported from Noovex9 32-bit ---- */
#define EC_DATA 0x62
#define EC_CMD  0x66
static int bat_present=0, bat_pct=100, bat_charging=1, bat_via_ec=0; static u64 bat_poll_t=0;
static int ec_wait_ibf(void){ for(int t=0;t<300000;t++){ if(!(inb(EC_CMD)&0x02))return 1; io_wait(); } return 0; }
static int ec_wait_obf(void){ for(int t=0;t<300000;t++){ if(inb(EC_CMD)&0x01)return 1; io_wait(); } return 0; }
static int ec_read(u8 off,u8* out){ if(!ec_wait_ibf())return 0; outb(EC_CMD,0x80); if(!ec_wait_ibf())return 0; outb(EC_DATA,off); if(!ec_wait_obf())return 0; *out=inb(EC_DATA); return 1; }
static int ec_detect(void){ u8 st=inb(EC_CMD); if(st==0xFF)return 0; return ec_wait_ibf(); }
static int ec_battery_pct(int* pct,int* charging){
    static const u8 cand[]={0x2A,0x32,0x42,0x90,0xB0,0x14,0x24};
    for(unsigned i=0;i<sizeof(cand);i++){ u8 v; if(ec_read(cand[i],&v)&&v>0&&v<=100){ *pct=v; u8 ac=0; if(ec_read(0x01,&ac))*charging=(ac&0x01)?1:0; else *charging=0; return 1; } }
    return 0;
}
static void battery_init(void){ bat_present=0; bat_pct=100; bat_charging=1; bat_via_ec=0;
    if(ec_detect()){ int p=0,ch=0; if(ec_battery_pct(&p,&ch)){ bat_present=1; bat_pct=p; bat_charging=ch; bat_via_ec=1; } } }
static void battery_tick(void){
    if(!bat_via_ec){ if(g_ticks-bat_poll_t<100)return; bat_poll_t=g_ticks;   /* keep retrying ~1s until the EC answers */
        if(ec_detect()){ int p=0,ch=0; if(ec_battery_pct(&p,&ch)){ bat_present=1; bat_pct=p; bat_charging=ch; bat_via_ec=1; } } return; }
    if(g_ticks-bat_poll_t<300)return; bat_poll_t=g_ticks;                     /* poll ~3s once found */
    int p=0,ch=0; if(ec_battery_pct(&p,&ch)){ bat_present=1; bat_pct=p; bat_charging=ch; } }
static void draw_battery(int x,int y){   /* small battery glyph + charging bolt */
    u32 col = bat_charging?rgb(90,200,120) : (bat_pct<=15?rgb(236,80,80) : bat_pct<=30?rgb(230,180,70) : rgb(210,216,228));
    fill_round(x,y,22,11,2,15,rgb(60,64,78)); fill_round(x+1,y+1,20,9,2,15,rgb(24,26,34)); fill_rect(x+22,y+3,2,5,rgb(120,126,140));
    int w=(bat_pct*18)/100; if(w<1&&bat_pct>0)w=1; if(w>18)w=18; fill_round(x+2,y+2,w,7,1,15,col);
    if(bat_charging){ /* lightning bolt overlay */
        for(int i=0;i<6;i++){ putpx(x+12-i/2,y+2+i,rgb(255,255,255)); putpx(x+11-i/2,y+2+i,rgb(255,255,255)); }
        for(int i=0;i<5;i++){ putpx(x+9+i/2,y+9-i,rgb(255,255,255)); } }
}
static void compose_present(void);
static void wifi_gui_connect(int idx){
    if(idx<0||idx>=g_apn)return;
    u32 m=rtw_find_mmio(); if(!m||!g_rxnum){ scopy(g_wifi_status,"init Wi-Fi in Terminal first (init/allbuilt/macinit/fw/rings)"); return; }
    /* show progress immediately so the UI isn't a silent freeze */
    { char* p=g_wifi_status; p=scopy(p,"connecting to "); int j=0; while(g_aps[idx].ssid[j]&&j<24){*p++=g_aps[idx].ssid[j];j++;} p=scopy(p,"..."); *p=0; }
    compose_present();
    g_chquiet=1;
    int r;
    if(g_aps[idx].sec==0) r=rtw_assoc(m,&g_aps[idx],g_aps[idx].ssid);
    else r=rtw_wpa2_join(m,&g_aps[idx],g_aps[idx].ssid,g_wifi_pw);
    g_chquiet=0;
    if(r==0){ char* p=g_wifi_status; p=scopy(p,g_aps[idx].sec==0?"CONNECTED (open) to ":"CONNECTED to "); int j=0; while(g_aps[idx].ssid[j]&&j<24){*p++=g_aps[idx].ssid[j];j++;} *p=0;
        int k=0; while(g_aps[idx].ssid[k]&&k<32){g_wifi_conn[k]=g_aps[idx].ssid[k];k++;} g_wifi_conn[k]=0;
        notify("Wi-Fi", g_wifi_conn); }
    else if(r==1) scopy(g_wifi_status,"FAILED: no auth response (AP not answering)");
    else if(r==2) scopy(g_wifi_status,"FAILED: auth rejected by AP");
    else if(r==3) scopy(g_wifi_status,"FAILED: no assoc response");
    else if(r==4) scopy(g_wifi_status,"FAILED: assoc rejected (security mismatch)");
    else if(r==5) scopy(g_wifi_status,"FAILED: associated but AP didn't start handshake");
    else if(r==6) scopy(g_wifi_status,"FAILED: wrong password (handshake MIC rejected)");
    else scopy(g_wifi_status,"FAILED: handshake error");
    compose_present();
}
static int nic_index_wifi(void){ if(!pci_n)pci_scan(); for(int k=0;k<pci_n;k++) if(pci_cls[k]==0x02&&pci_sub[k]==0x80)return k; return -1; }
static u32 rtw_find_mmio(void){
    if(g_wifi_mmio)return g_wifi_mmio;
    int wi=nic_index_wifi(); if(wi<0)return 0;
    int bus=pci_bus[wi],sl=pci_slot[wi];
    u32 c4=pci_cfg(bus,sl,0,0x04); pci_cfgw(bus,sl,0,0x04,c4|0x06);
    for(int bi=0;bi<6;bi++){ u32 bar=pci_cfg(bus,sl,0,0x10+bi*4); if(!bar)continue;
        if(!(bar&1)){ u32 a=bar&0xFFFFFFF0u; if(a>0x100000u)g_wifi_mmio=a; } }
    return g_wifi_mmio;
}
static void wifi_gui_scan(int full){
    u32 m=rtw_find_mmio(); if(!m){ scopy(g_wifi_status,"no Wi-Fi adapter"); return; }
    if(!g_rxnum){ scopy(g_wifi_status,"init Wi-Fi in Terminal first"); return; }
    g_wifi_scanning=1; g_chquiet=1;
    g_apn=0; rtw_rx_ring_up(m);
    static const u8 qc[]={1,6,11,2,3,4,5,7,8,9,10,12,13};
    int nch=full?13:3;
    for(int ci=0;ci<nch;ci++){ rtw_set_channel_2g(m,qc[ci]); ui_pump(); for(int w=0;w<4;w++){ rtw_delay_us(62000); ui_pump(); rtw_scan_drain(m,qc[ci]); } }
    g_wifi_scanning=0; g_chquiet=0;
    char* p=g_wifi_status; p=scopy(p,"found "); p=udec(p,(u32)g_apn); p=scopy(p," networks"); *p=0;
}
static void wifi_icon_glyph(int cx,int cy,u32 col){
    /* three stacked arcs + dot, simple Wi-Fi symbol */
    for(int r=2;r<=8;r+=3){ for(int a=-r;a<=r;a++){ int y=(int)((r*r-a*a)); if(y<0)continue;
        /* approximate arc top: y-offset ~ sqrt */ int yo=0,tt=r*r-a*a; while((yo+1)*(yo+1)<=tt)yo++;
        int px=cx+a, py=cy-yo; if(px>=0&&py>=0&&px<(int)W&&py<27) putpx(px,py,col); } }
    fill_circle(cx,cy,1,col);
}
static void draw_topbar(void){
    for(u32 yy=0;yy<27;yy++) for(u32 xx=0;xx<W;xx++){ u32*p=&SCENE[(u64)yy*SW+xx]; *p=mix(*p,rgb(16,18,26),200); }    fill_rect(0,27,(int)W,1,rgb(4,5,9));
    if(g_launcher||(my<27&&mx<114)) fill_round(4,3,110,21,5,15,mix(rgb(16,18,26),rgb(255,255,255),44));
    fill_round(12,7,15,15,4,15,ACC); dstr_ctr(19,10,"N",rgb(255,255,255),1);
    dstr(35,9,"NoovexOS",rgb(238,242,250),1);
    if(focus>=0&&WN[focus].open&&!WN[focus].min) dstr(128,9,APPNAME[focus],rgb(188,194,210),1);
    int hh,mm,ss,yy2,mo,dd; read_clock(&hh,&mm,&ss,&yy2,&mo,&dd);
    char t[12],*p=t; p=u2(p,hh);*p++=':';p=u2(p,mm); if(g_showsec){*p++=':';p=u2(p,ss);} *p=0;
    g_clockx=(int)W-slen(t)*8-14; dstr(g_clockx,9,t,rgb(238,242,250),1);
    g_qsx=g_clockx-32; if(g_qs) fill_round(g_qsx-4,3,26,21,5,15,mix(rgb(16,18,26),rgb(255,255,255),44));
    for(int r=0;r<3;r++){ int yy=10+r*4; fill_rect(g_qsx+2,yy,14,2,rgb(200,206,220)); int kx=g_qsx+2+((r*5)%9); fill_rect(kx,yy-1,3,4,rgb(120,150,230)); }
    /* notification bell (unread count) */
    g_bellx=g_qsx-30;
    { int bx=g_bellx+7,by=7; fill_round(bx+2,by,8,9,3,15,rgb(210,216,228)); fill_rect(bx,by+9,12,2,rgb(210,216,228)); fill_rect(bx+4,by+11,4,2,rgb(210,216,228));
      if(g_notif_n>0){ fill_circle(bx+12,by+1,4,rgb(236,72,72)); } }
    /* workspace pager (four pips, left of bell) */
    g_wsx=g_bellx-8-NWS*14;
    for(int w=0;w<NWS;w++){ int cx=g_wsx+w*14+6; if(w==g_ws) fill_round(cx-5,10,12,6,3,15,ACC); else fill_circle(cx,13,2,rgb(150,158,176)); }
    /* battery (left of the workspace pips) */
    if(bat_present){ char bp[8],*bq=bp; bq=udec(bq,(u32)bat_pct); *bq++='%'; *bq=0; int bw2=slen(bp)*8; int batx=g_wsx-10-28-bw2-4;
        draw_battery(batx,8); dstr(batx+28,9,bp, bat_pct<=15?rgb(236,120,120):rgb(210,216,228),1); }
    { int base=(bat_present?(g_wsx-10-28-24):(g_wsx-10)); g_wifix=base-20;
      wifi_icon_glyph(g_wifix+9,17, g_apn>0?rgb(120,200,255):rgb(150,158,176)); }
}
static void dock_icon(int k,int x,int y){ switch(k){ case 0:ic_folder(x,y);break; case 1:ic_term(x,y);break; case 2:ic_edit(x,y);break; case 3:ic_web(x,y);break; case 4:ic_calc(x,y);break; case 5:ic_clock(x,y);break; case 6:ic_calendar(x,y);break; case 7:ic_sysmon(x,y);break; case 8:ic_snake(x,y);break; case 9:ic_flappy(x,y);break; case 10:ic_paint(x,y);break; default:ic_gear(x,y);break; } }
static void draw_dock(void){
    int n=12; DOCK_w=n*DOCK_IC+(n-1)*DOCK_GAP+2*DOCK_PAD; DOCK_h=DOCK_IC+20; DOCK_x=(int)W/2-DOCK_w/2; DOCK_y=(int)H-DOCK_h-10;
    shadow_soft(DOCK_x,DOCK_y,DOCK_w,DOCK_h,20);
    for(int j=0;j<DOCK_h;j++) for(int ii=0;ii<DOCK_w;ii++){ if(cut(ii,j,DOCK_w,DOCK_h,20,15))continue; u32*p=&SCENE[(u64)(DOCK_y+j)*SW+(DOCK_x+ii)]; *p=mix(*p,rgb(250,251,255),150); }
    for(int ii=0;ii<DOCK_w;ii++) if(!cut(ii,0,DOCK_w,DOCK_h,20,1|2)) putpx(DOCK_x+ii,DOCK_y,rgb(255,255,255));
    int near=(my>=DOCK_y-34); int hovk=-1;
    for(int k=0;k<n;k++){ int ix=DOCK_x+DOCK_PAD+k*(DOCK_IC+DOCK_GAP), cxk=ix+DOCK_IC/2; int app=IC_APP[k];
        int dxk=mx-cxk; if(dxk<0)dxk=-dxk; int lift=(near&&dxk<100)?(int)(26*(100-dxk)*(100-dxk)/(100*100)):0;
        dock_icon(k,ix,DOCK_y+8-lift);
        if(WN[app].open) fill_circle(cxk,DOCK_y+DOCK_h-5,2,rgb(60,64,78));
        if(near&&dxk<DOCK_IC/2+DOCK_GAP/2) hovk=k; }
    if(hovk>=0){ const char*nm=APPNAME[IC_APP[hovk]]; int cxk=DOCK_x+DOCK_PAD+hovk*(DOCK_IC+DOCK_GAP)+DOCK_IC/2;
        int tw=slen(nm)*8+18, tx=cxk-tw/2, ty=DOCK_y-32; if(tx<6)tx=6; if(tx+tw>(int)W-6)tx=(int)W-6-tw;
        shadow_soft(tx,ty,tw,24,7); fill_round(tx,ty,tw,24,7,15,rgb(38,42,52)); dstr_ctr(tx+tw/2,ty+5,nm,rgb(240,244,252),1); }
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
    int pw=320, px=g_wifix-pw+20; if(px<6)px=6; int py=30;
    int rows=g_apn>7?7:g_apn; int ph=70+rows*30+(g_wifi_entry?70:0)+26;
    shadow_soft(px,py,pw,ph,12); fill_round(px,py,pw,ph,12,15,g_theme?rgb(28,31,42):rgb(250,251,254));
    dstr(px+16,py+14,"Wi-Fi",g_theme?rgb(236,239,246):rgb(24,28,40),2);
    int bx=px+pw-80, by=py+12; fill_round(bx,by,66,22,6,15, g_wifi_scanning?rgb(150,158,176):ACC); dstr_ctr(bx+33,by+7,g_wifi_scanning?"...":"Scan",rgb(255,255,255),1);
    dstr(px+16,py+38,g_wifi_status,g_theme?rgb(150,158,176):rgb(110,118,138),1);
    int cy=py+66;
    for(int i=0;i<rows;i++){
        int hov=(mx>=px+8&&mx<px+pw-8&&my>=cy&&my<cy+28);
        int open=(g_aps[i].sec==0);
        int isconn=(g_wifi_conn[0] && g_aps[i].ssid[0]); if(isconn){ int k=0; while(g_aps[i].ssid[k]&&g_wifi_conn[k]&&g_aps[i].ssid[k]==g_wifi_conn[k])k++; isconn=(!g_aps[i].ssid[k]&&!g_wifi_conn[k]); }
        if(i==g_wifi_sel) fill_round(px+8,cy,pw-16,28,6,15,mix(ACC,g_theme?rgb(28,31,42):rgb(255,255,255),180));
        else if(hov) fill_round(px+8,cy,pw-16,28,6,15,g_theme?rgb(40,44,58):rgb(234,238,246));
        u32 nmc=g_theme?rgb(224,228,236):rgb(30,34,46);
        dstr(px+16,cy+8,g_aps[i].ssid[0]?g_aps[i].ssid:"<hidden>",nmc,1);
        /* signal dots by SEEN count */
        int sig=g_aps[i].seen>30?4:g_aps[i].seen>15?3:g_aps[i].seen>5?2:1;
        for(int b=0;b<4;b++){ u32 dc=b<sig?(g_theme?rgb(140,200,255):ACC):(g_theme?rgb(60,66,82):rgb(214,220,230)); fill_rect(px+pw-58+b*7,cy+18-b*3,4,3+b*3,dc); }
        if(isconn) dstr(px+pw-92,cy+8,"",rgb(90,200,120),1);
        else { const char* sec=open?"open":"lock"; dstr(px+pw-92,cy+8,sec, open?rgb(90,170,110):rgb(160,140,90),1); }
        cy+=30;
    }
    if(g_apn==0) dstr(px+16,py+66,"(press Scan to find networks)",g_theme?rgb(120,128,146):rgb(150,158,176),1);
    if(g_wifi_entry && g_wifi_sel>=0){
        cy+=6; dstr(px+16,cy,"Password:",g_theme?rgb(200,206,220):rgb(70,78,98),1); cy+=20;
        fill_round(px+16,cy,pw-100,26,5,15,g_theme?rgb(44,48,62):rgb(238,241,247));
        char mask[40]; int n=g_wifi_pwlen>36?36:g_wifi_pwlen; for(int k=0;k<n;k++)mask[k]='*'; mask[n]=0;
        dstr(px+22,cy+7,mask,g_theme?rgb(236,239,246):rgb(24,28,40),1);
        int jx=px+pw-76; fill_round(jx,cy,60,26,5,15,ACC); dstr_ctr(jx+30,cy+7,"Join",rgb(255,255,255),1);
    }
}
static void notif_panel_draw(void){
    int pw=340, px=(int)W-pw, py=28, ph=(int)H-28-46;
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
static void ws_switch(int w){ if(w<0)w=NWS-1; if(w>=NWS)w=0; if(w==g_ws)return; g_ws=w; if(focus>=0&&WN[focus].ws!=g_ws)focus=-1; g_ws_show=g_ticks+120; }
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
static void spin(int n);   /* fwd - defined in boot splash section */
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
#define DGX0 24
#define DGY0 44
#define DGW  92
#define DGH  94
static int g_apx[5], g_apy[5], g_apinit=0;
static int g_fpx[FS_N], g_fpy[FS_N]; static u8 g_fpl[FS_N];
static int g_ddrag=-1, g_ddx=0, g_ddy=0, g_ddsx=0, g_ddsy=0;   /* dragging: -1 none, 0-4 app, 100+slot file */
static int desk_hit(int,int); static void desk_open(int);
static void desk_init_pos(void){ if(g_apinit)return; g_apinit=1; for(int k=0;k<5;k++){ g_apx[k]=DGX0; g_apy[k]=DGY0+k*DGH; } for(int k=0;k<FS_N;k++)g_fpl[k]=0; }
static void desk_place_file(int slot){   /* assign the next free grid cell to a desktop file */
    if(g_fpl[slot])return; desk_init_pos();
    for(int c=0;c<12;c++)for(int r=0;r<8;r++){ int px=DGX0+c*DGW, py=DGY0+r*DGH; int used=0;
        for(int k=0;k<5;k++) if(g_apx[k]==px&&g_apy[k]==py)used=1;
        for(int k=0;k<FS_N;k++) if(g_fpl[k]&&g_fpx[k]==px&&g_fpy[k]==py)used=1;
        if(!used){ g_fpx[slot]=px; g_fpy[slot]=py; g_fpl[slot]=1; return; } }
    g_fpx[slot]=DGX0; g_fpy[slot]=DGY0; g_fpl[slot]=1;
}
static void launch_icon(int w,int x,int y){ switch(w){
    case W_FILES:ic_folder(x,y);break; case W_TERM:ic_term(x,y);break; case W_EDITOR:ic_edit(x,y);break; case W_BROWSER:ic_web(x,y);break;
    case W_CALC:ic_calc(x,y);break; case W_CLOCK:ic_clock(x,y);break; case W_CALENDAR:ic_calendar(x,y);break; case W_SYSMON:ic_sysmon(x,y);break;
    case W_LOGS:{ itile(x,y,rgb(12,14,20)); for(int r=0;r<4;r++)fill_rect(x+10,y+11+r*7,28-(r*3),3,rgb(120,255,150)); break; }
    case W_TFIND:{ itile(x,y,rgb(18,20,28)); dstr(x+9,y+13,">_",rgb(120,255,180),2); break; }
    case W_SNAKE:ic_snake(x,y);break; case W_FLAPPY:ic_flappy(x,y);break; case W_PAINT:ic_paint(x,y);break; case W_SETTINGS:ic_gear(x,y);break;
    case W_DOOM:{ itile(x,y,rgb(78,18,14)); fill_round(x+11,y+11,26,26,5,15,rgb(196,52,30)); dstr_ctr(x+24,y+17,"D",rgb(255,224,180),2); }break;
    default:{ itile(x,y,rgb(46,52,72)); fill_round(x+13,y+13,22,22,4,15,ACC); dstr_ctr(x+24,y+18,"N",rgb(255,255,255),1); } } }
static void desk_hi(int x,int y){ if(mx>=x-6&&mx<x+54&&my>=y-6&&my<y+74){ for(int j=-6;j<74;j++)for(int i=-6;i<54;i++){ if(cut(i+6,j+6,60,80,12,15))continue; int dx=x+i,dy=y+j; if((u32)dx<W&&(u32)dy<H){ u32*p=&SCENE[(u64)dy*SW+dx]; *p=mix(*p,rgb(255,255,255),42); } } } }
static void desk_draw(void){ desk_init_pos();
    for(int k=0;k<5;k++){ int x=g_apx[k],y=g_apy[k]; if(g_ddrag==k){ x=mx-g_ddx; y=my-g_ddy; } else desk_hi(x,y);
        launch_icon(DESK[k],x,y); dstr_ctr(x+24,y+52,DESK_L[k],rgb(238,242,250),1); }
    for(int s=0;s<FS_N;s++) if(FS[s].used && FS[s].folder==2){ if(!g_fpl[s])desk_place_file(s); int x=g_fpx[s],y=g_fpy[s]; if(g_ddrag==100+s){ x=mx-g_ddx; y=my-g_ddy; } else desk_hi(x,y);
        file_icon(FS[s].name,x,y); char nm[13]; int i=0; for(;FS[s].name[i]&&i<11;i++)nm[i]=FS[s].name[i]; if(FS[s].name[i]){nm[i-1]='.';nm[i]=0;}else nm[i]=0; dstr_ctr(x+24,y+52,nm,rgb(238,242,250),1); } }
static void marq_draw(void){ if(!g_marq)return; int x0=marq_x0,y0=marq_y0,x1=mx,y1=my; if(x1<x0){int t=x0;x0=x1;x1=t;} if(y1<y0){int t=y0;y0=y1;y1=t;}
    int w=x1-x0, h=y1-y0; if(w<2||h<2)return;
    for(int j=0;j<h;j++)for(int i=0;i<w;i++){ int dx=x0+i,dy=y0+j; if((u32)dx<W&&(u32)dy<H){ u32*p=&SCENE[(u64)dy*SW+dx]; *p=mix(*p,rgb(96,152,240),68); } }
    for(int i=0;i<w;i++){ putpx(x0+i,y0,rgb(60,120,230)); putpx(x0+i,y1-1,rgb(60,120,230)); } for(int j=0;j<h;j++){ putpx(x0,y0+j,rgb(60,120,230)); putpx(x1-1,y0+j,rgb(60,120,230)); } }
static void prop_draw(void){
    if(!g_prop)return;
    int pw=340, ph=210, x=(int)W/2-pw/2, y=(int)H/2-ph/2;
    shadow_soft(x,y,pw,ph,12); fill_round(x,y,pw,ph,12,15,rgb(244,246,250));
    fill_round(x,y,pw,40,12,15,rgb(52,110,200)); dstr(x+16,y+13,"Properties",rgb(255,255,255),2);
    int lx=x+22, ly=y+58; char b[96],*p;
    if(g_prop_slot>=0 && g_prop_slot<FS_N && FS[g_prop_slot].used){
        file_icon(FS[g_prop_slot].name, x+pw-72, y+56);
        dstr(lx,ly,"Name:",rgb(120,128,146),1); dstr(lx+80,ly,FS[g_prop_slot].name,rgb(30,34,48),1); ly+=28;
        const char* ty="Document"; const char* nm=FS[g_prop_slot].name;
        if(fext_eq(nm,"pptx"))ty="PowerPoint"; else if(fext_eq(nm,"docx"))ty="Word document"; else if(fext_eq(nm,"png")||fext_eq(nm,"jpg")||fext_eq(nm,"jpeg")||fext_eq(nm,"webp"))ty="Image";
        else if(fext_eq(nm,"nvx")||fext_eq(nm,"c")||fext_eq(nm,"h")||fext_eq(nm,"rn"))ty="Source code"; else if(fext_eq(nm,"zip"))ty="Archive"; else if(fext_eq(nm,"md"))ty="Markdown"; else if(fext_eq(nm,"html"))ty="HTML page"; else if(fext_eq(nm,"txt"))ty="Text file"; else if(fext_eq(nm,"cfg"))ty="Config";
        dstr(lx,ly,"Type:",rgb(120,128,146),1); dstr(lx+80,ly,(char*)ty,rgb(30,34,48),1); ly+=28;
        p=b;p=scopy(p,"");u32 L=(u32)FS[g_prop_slot].len; if(L>=1024){p=udec(p,L/1024);*p++='.';p=udec(p,(L%1024)*10/1024);p=scopy(p," KB");}else{p=udec(p,L);p=scopy(p," bytes");}*p=0;
        dstr(lx,ly,"Size:",rgb(120,128,146),1); dstr(lx+80,ly,b,rgb(30,34,48),1); ly+=28;
        const char* loc=FS[g_prop_slot].folder==2?"Desktop":FS[g_prop_slot].folder==1?"Downloads":"Documents";
        dstr(lx,ly,"Location:",rgb(120,128,146),1); dstr(lx+80,ly,(char*)loc,rgb(30,34,48),1); ly+=28;
        dstr(lx,ly,"Storage:",rgb(120,128,146),1); dstr(lx+80,ly,disk_ok?"NVXFS (disk)":"RAM",rgb(30,34,48),1);
    } else if(g_prop_app>=0 && g_prop_app<5){
        launch_icon(DESK[g_prop_app], x+pw-70, y+54);
        dstr(lx,ly,"Name:",rgb(120,128,146),1); dstr(lx+80,ly,(char*)DESK_L[g_prop_app],rgb(30,34,48),1); ly+=28;
        dstr(lx,ly,"Type:",rgb(120,128,146),1); dstr(lx+80,ly,"Application",rgb(30,34,48),1); ly+=28;
        dstr(lx,ly,"Status:",rgb(120,128,146),1); dstr(lx+80,ly,WN[DESK[g_prop_app]].open?"Open":"Closed",rgb(30,34,48),1); ly+=28;
        dstr(lx,ly,"System:",rgb(120,128,146),1); dstr(lx+80,ly,"NoovexOS 64",rgb(30,34,48),1);
    }
    int bw=90,bx=x+pw-bw-16,by=y+ph-40; int over=(mx>=bx&&mx<bx+bw&&my>=by&&my<by+28);
    fill_round(bx,by,bw,28,7,15,over?rgb(40,90,180):rgb(52,110,200)); dstr_ctr(bx+bw/2,by+7,"Close",rgb(255,255,255),1);
}
static int desk_hit(int px,int py){ for(int k=0;k<5;k++){ if(px>=g_apx[k]&&px<g_apx[k]+48&&py>=g_apy[k]&&py<g_apy[k]+72) return k; }
    for(int s=0;s<FS_N;s++) if(FS[s].used&&FS[s].folder==2&&g_fpl[s]){ if(px>=g_fpx[s]&&px<g_fpx[s]+48&&py>=g_fpy[s]&&py<g_fpy[s]+72) return 100+s; } return -1; }
static void desk_open(int it){ if(it<0)return; if(it<100){ focus_win(DESK[it]); return; } int s=it-100; if(s<0||s>=FS_N||!FS[s].used)return; int kd=fkind(s); g_fsel=s; if(fext_eq(FS[s].name,"pptx")){ pptx_load(s); open_win(W_PPTX); } else if(kd==1)g_imgview=s; else if(kd==2)browser_show_html(FS[s].data,FS[s].len,FS[s].name); else editor_open(s); }
static int desk_click(int px,int py){ int it=desk_hit(px,py); if(it>=0){ desk_open(it); return 1; } return 0; }
/* ---- app launcher ---- */
static const int LAUNCH[17]={W_FILES,W_TERM,W_EDITOR,W_BROWSER,W_CALC,W_CLOCK,W_CALENDAR,W_SYSMON,W_SNAKE,W_FLAPPY,W_PAINT,W_DOOM,W_NEOFETCH,W_LOGS,W_TFIND,W_SETTINGS,W_ABOUT};
static char g_lq[40]=""; static int g_lqn=0;           /* launcher search query */
/* result kinds: 0=app(open_win), 1=settings page(open settings@cat), 2=file(open in editor/viewer) */
static int g_lres_kind[24], g_lres_arg[24], g_lres_n=0;
static const char* SET_PAGES[5]={"Settings: System","Settings: Personalization","Settings: Network","Settings: Display","Settings: About"};
static int lc_ci(char a){ if(a>='A'&&a<='Z')a+=32; return a; }
static int lc_has(const char* hay,const char* needle){ if(!needle[0])return 1;
    for(int i=0;hay[i];i++){ int j=0; while(needle[j]&&hay[i+j]&&lc_ci(hay[i+j])==lc_ci(needle[j]))j++; if(!needle[j])return 1; } return 0; }
static void launcher_search(void){ g_lres_n=0;
    for(int k=0;k<17 && g_lres_n<24;k++) if(lc_has(APPNAME[LAUNCH[k]],g_lq)){ g_lres_kind[g_lres_n]=0; g_lres_arg[g_lres_n]=LAUNCH[k]; g_lres_n++; }
    if(g_lq[0]) for(int k=0;k<5 && g_lres_n<24;k++) if(lc_has(SET_PAGES[k],g_lq)){ g_lres_kind[g_lres_n]=1; g_lres_arg[g_lres_n]=k; g_lres_n++; }
    if(g_lq[0]) for(int f=0;f<FS_N && g_lres_n<24;f++) if(FS[f].used && lc_has(FS[f].name,g_lq)){ g_lres_kind[g_lres_n]=2; g_lres_arg[g_lres_n]=f; g_lres_n++; }
}
static void launcher_open_result(int idx){ if(idx<0||idx>=g_lres_n)return; g_launcher=0; int kind=g_lres_kind[idx],arg=g_lres_arg[idx];
    if(kind==0) open_win(arg);
    else if(kind==1){ g_setcat=arg; open_win(W_SETTINGS); }
    else if(kind==2){ editor_open(arg); open_win(W_EDITOR); }        /* open file in editor */
}
static void launcher_geom(int*px,int*py,int*pw,int*ph){ *pw=536; *ph=468; *px=((int)W-*pw)/2; *py=((int)H-*ph)/2; }
static void launcher_draw(void){ int px,py,pw,ph; launcher_geom(&px,&py,&pw,&ph);
    for(u32 y=0;y<H;y++)for(u32 x=0;x<W;x++){ u32*p=&SCENE[(u64)y*SW+x]; *p=mix(*p,rgb(10,12,20),120); }
    shadow_soft(px,py,pw,ph,16); fill_round(px,py,pw,ph,16,15,rgb(248,249,252));
    /* search box */
    int sbx=px+20, sby=py+18, sbw=pw-40, sbh=34; fill_round(sbx,sby,sbw,sbh,9,15,rgb(236,238,244));
    fill_circle(sbx+18,sby+17,5,rgb(120,128,146)); fill_rect(sbx+22,sby+21,6,3,rgb(120,128,146));
    if(g_lq[0]) dstr(sbx+36,sby+10,g_lq,INK,1); else dstr(sbx+36,sby+10,"Search apps, settings, files...",rgb(150,158,176),1);
    if(((g_ticks/30)&1)) { int cxp=sbx+36+g_lqn*8; fill_rect(cxp,sby+8,2,18,INK); }
    if(!g_lq[0]){
        dstr(px+24,py+66,"Applications",INK,2);
        int gx=px+24, gy=py+96, cw=124, ch=92;
        for(int k=0;k<17;k++){ int c=k%4, r=k/4; int cx=gx+c*cw, cy=gy+r*ch;
            int hov=(mx>=cx&&mx<cx+cw-8&&my>=cy&&my<cy+ch-8); if(hov) fill_round(cx,cy,cw-8,ch-8,10,15,rgb(233,237,244));
            launch_icon(LAUNCH[k], cx+(cw-8)/2-24, cy+8); dstr_ctr(cx+(cw-8)/2, cy+58, APPNAME[LAUNCH[k]], rgb(60,66,84),1); }
    } else {
        int ry=py+66;
        for(int k=0;k<g_lres_n && ry<py+ph-20;k++){ int hov=(my>=ry&&my<ry+44&&mx>=px+16&&mx<px+pw-16);
            if(hov) fill_round(px+12,ry,pw-24,44,9,15,ACC);
            u32 tc=hov?rgb(255,255,255):INK, sc=hov?rgb(228,236,255):rgb(120,128,146);
            int kind=g_lres_kind[k],arg=g_lres_arg[k];
            if(kind==0){ launch_icon(arg,px+18,ry-2); dstr(px+64,ry+8,APPNAME[arg],tc,1); dstr(px+64,ry+26,"Application",sc,1); }
            else if(kind==1){ ic_gear(px+18,ry-2); dstr(px+64,ry+8,SET_PAGES[arg]+10,tc,1); dstr(px+64,ry+26,"Settings",sc,1); }
            else { ic_edit(px+18,ry-2); dstr(px+64,ry+8,FS[arg].name,tc,1); dstr(px+64,ry+26,"File",sc,1); }
            ry+=48; }
        if(g_lres_n==0) dstr(px+24,py+80,"No results",rgb(150,158,176),1);
    }
}
static int launcher_click(int px,int py){ int lx,ly,lw,lh; launcher_geom(&lx,&ly,&lw,&lh);
    if(g_lq[0]){ int ry=ly+66; for(int k=0;k<g_lres_n;k++){ if(py>=ry&&py<ry+44&&px>=lx+12&&px<lx+lw-12){ launcher_open_result(k); return 1; } ry+=48; } g_launcher=0; return 1; }
    int gx=lx+24, gy=ly+96, cw=124, ch=92;
    for(int k=0;k<17;k++){ int c=k%4, r=k/4; int cx=gx+c*cw, cy=gy+r*ch; if(px>=cx&&px<cx+cw-8&&py>=cy&&py<cy+ch-8){ g_launcher=0; open_win(LAUNCH[k]); return 1; } }
    g_launcher=0; return 1;
}
/* keystrokes while the launcher is open (search box) */
static int launcher_key(char c){ if(!g_launcher)return 0;
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
static void toast_draw(void){ if(!toast_t)return; u64 age=(u64)(g_ticks-toast_t); if(age>=300){toast_t=0;return;}
    int a=255; if(age>240) a=255-(int)(age-240)*255/60; int tw=slen(toast_msg)*8+40; int tx=((int)W-tw)/2, ty=(int)H-140;
    for(int j=0;j<34;j++)for(int i=0;i<tw;i++){ if(cut(i,j,tw,34,10,15))continue; int dx=tx+i,dy=ty+j; if((u32)dx<W&&(u32)dy<H){ u32*p=&SCENE[(u64)dy*SW+dx]; *p=mix(*p,rgb(28,32,44),a); } }
    dstr_ctr(tx+tw/2, ty+9, toast_msg, mix(rgb(28,32,44),rgb(240,244,252),a), 1); }
/* ---- top-bar popovers ---- */
static int cal_dow(int y,int m,int d); static int cal_dim(int y,int m);
static void clockpop_draw(void){ int pw=210, ph=210, px=(int)W-pw-8, py=30; shadow_soft(px,py,pw,ph,12); fill_round(px,py,pw,ph,12,15,rgb(250,251,254));
    int hh,mm,ss,yr,mo,dy; read_clock(&hh,&mm,&ss,&yr,&mo,&dy);
    static const char*MON[13]={"","January","February","March","April","May","June","July","August","September","October","November","December"};
    { char hd[24],*p=hd; { const char*mn=MON[mo>=1&&mo<=12?mo:1]; while(*mn)*p++=*mn++; } *p++=' '; p=udec(p,(u32)yr); *p=0; dstr_ctr(px+pw/2,py+10,hd,INK,1); }
    static const char*WD[7]={"M","T","W","T","F","S","S"}; int cw=(pw-20)/7, gx=px+10, gy=py+32;
    for(int c=0;c<7;c++) dstr_ctr(gx+cw*c+cw/2,gy,WD[c],rgb(150,158,176),1);
    int col0=(cal_dow(yr,mo,1)+6)%7, dim=cal_dim(yr,mo), yy=gy+22;
    for(int day=1,cell=col0; day<=dim; day++,cell++){ int c=cell%7, r=cell/7, cxp=gx+cw*c+cw/2, cyp=yy+r*24; int today=(day==dy);
        if(today) fill_circle(cxp,cyp+4,10,ACC); char b[3],*p=b; p=udec(p,(u32)day); *p=0; dstr_ctr(cxp,cyp,b,today?rgb(255,255,255):rgb(52,60,78),1); } }
static void qs_draw(void){ int pw=250, ph=150, px=g_qsx-pw+30, py=30; if(px<6)px=6; shadow_soft(px,py,pw,ph,12); fill_round(px,py,pw,ph,12,15,rgb(250,251,254));
    dstr(px+16,py+12,"Wallpaper",rgb(90,100,120),1); static const char*THN2[6]={"Aurora","Wire","Crane","Porsche","Field","Desert"};
    for(int k=0;k<3;k++){ int bx=px+16+k*74, by=py+32; fill_round(bx,by,64,30,6,15,THSW[k]); if(k==g_theme&&g_wallmode==0){ for(int t=0;t<64;t++){putpx(bx+t,by,ACC);putpx(bx+t,by+29,ACC);} } dstr_ctr(bx+32,by+38,THN2[k],rgb(90,100,120),1); }
    dstr(px+16,py+82,"Accent",rgb(90,100,120),1);
    for(int k=0;k<5;k++){ int bx=px+16+k*44, by=py+102; if(ACV[k]==ACC){ fill_circle(bx+14,by+12,15,rgb(255,255,255)); } fill_circle(bx+14,by+12,12,ACV[k]); } }
/* ---- rename modal ---- */
static void rename_draw(void){ int pw=360, ph=140, px=((int)W-pw)/2, py=(int)H/2-90;
    for(u32 y=0;y<H;y++)for(u32 x=0;x<W;x++){ u32*p=&SCENE[(u64)y*SW+x]; *p=mix(*p,rgb(10,12,20),110); }
    shadow_soft(px,py,pw,ph,14); fill_round(px,py,pw,ph,14,15,rgb(250,251,254));
    dstr(px+24,py+20,g_renmode==2?"New folder":g_renmode==3?"New file":g_renmode==1?"Rename folder":"Rename",INK,2);
    fill_round(px+24,py+56,pw-48,34,8,15,rgb(238,241,246)); int cx2=px+36; for(int k=0;k<g_renlen;k++){ dchar(cx2,py+66,g_renbuf[k],rgb(40,46,64),1); cx2+=8; } if((g_ticks/50)&1) fill_rect(cx2+1,py+65,2,15,ACC);
    dstr(px+24,py+ph-24,g_renmode>=2?"Enter = create    Esc = cancel":"Enter = rename    Esc = cancel",rgb(120,128,146),1); }
static void snap_preview_draw(void){ if(!g_snap)return; int wy=32, wh=(int)H-32-86, hw=((int)W-18)/2, hh=(wh-6)/2; int x=6,y=wy,w=hw,h=wh;
    if(g_snap==3){ x=6;y=wy;w=(int)W-12;h=wh; } else if(g_snap==1){ x=6;y=wy;w=hw;h=wh; } else if(g_snap==2){ x=(int)W-6-hw;y=wy;w=hw;h=wh; }
    else if(g_snap==4){ x=6;y=wy;w=hw;h=hh; } else if(g_snap==5){ x=(int)W-6-hw;y=wy;w=hw;h=hh; } else if(g_snap==6){ x=6;y=wy+hh+6;w=hw;h=hh; } else { x=(int)W-6-hw;y=wy+hh+6;w=hw;h=hh; }
    for(int j=0;j<h;j++)for(int i=0;i<w;i++){ if(cut(i,j,w,h,12,15))continue; int dx=x+i,dy=y+j; if((u32)dx<W&&(u32)dy<H){ u32*p=&SCENE[(u64)dy*SW+dx]; *p=mix(*p,rgb(120,170,250),90); } }
    for(int i=0;i<w;i++){ putpx(x+i,y,rgb(150,190,255)); putpx(x+i,y+h-1,rgb(150,190,255)); } for(int j=0;j<h;j++){ putpx(x,y+j,rgb(150,190,255)); putpx(x+w-1,y+j,rgb(150,190,255)); } }
#define ANIMW 768
#define ANIMH 600
static u32 WANIM[ANIMW*ANIMH];
static void compose_build(void){
    memcpy(SCENE,BG,(u64)W*H*4);
    desk_draw();
    int order[NWIN],n=0; for(int i=0;i<NWIN;i++) if(WN[i].open&&!WN[i].min&&i!=anim_hide&&WN[i].ws==g_ws)order[n++]=i;
    for(int a=0;a<n;a++)for(int b=a+1;b<n;b++) if(WN[order[b]].z<WN[order[a]].z){int t=order[a];order[a]=order[b];order[b]=t;}
    if(drag>=0&&g_snap) snap_preview_draw();
    for(int a=0;a<n;a++) draw_app(order[a]);
    marq_draw();
    draw_topbar(); draw_dock();
    if(g_launcher) launcher_draw(); if(g_qs) qs_draw(); if(g_clockpop) clockpop_draw();
    if(g_ctx) ctx_draw(); img_viewer_draw(); if(g_alttab) alttab_draw(); if(g_rename) rename_draw(); if(g_prop) prop_draw();
    if(g_wifipop) wifi_panel_draw(); if(g_notifpanel) notif_panel_draw(); toast_frame(); notif_toast_draw(); ws_overlay_draw(); toast_draw();
}
static u64 fps_t0=0; static int fps_cnt=0, fps_val=0;
static u32 SCENE_NC[MAXW*MAXH];          /* cursor-free scene cache (for dirty-rect cursor overlay) */
static int cxlast=-1, cylast=-1;
#define CURBB 44
static void blit_region(int x,int y,int w,int h){    /* copy SCENE[region] -> framebuffer[region] */
    if(x<0){w+=x;x=0;} if(y<0){h+=y;y=0;} if(x+w>(int)W)w=(int)W-x; if(y+h>(int)H)h=(int)H-y; if(w<=0||h<=0)return;
    if(BYPP==4){ for(int r=0;r<h;r++) sse_copy32((u32*)(FBB+(u64)(y+r)*PITCHB+(u64)x*4), SCENE+(u64)(y+r)*SW+x, w); }
    else { for(int r=0;r<h;r++) sse_pack24(FBB+(u64)(y+r)*PITCHB+(u64)x*3, SCENE+(u64)(y+r)*SW+x, w); }
}
static void nc_restore(int x,int y,int w,int h){     /* copy cursor-free cache back into SCENE[region] */
    if(x<0){w+=x;x=0;} if(y<0){h+=y;y=0;} if(x+w>(int)W)w=(int)W-x; if(y+h>(int)H)h=(int)H-y; if(w<=0||h<=0)return;
    for(int r=0;r<h;r++){ u32* d=SCENE+(u64)(y+r)*SW+x; u32* s=SCENE_NC+(u64)(y+r)*SW+x; sse_copy32(d,s,w); }
}
static void compose_present(void){ compose_build();
    fps_cnt++; if(g_ticks-fps_t0>=100){ fps_val=fps_cnt; fps_cnt=0; fps_t0=g_ticks; }
    if(g_fps_overlay){ char b[16],*p=b; p=udec(p,(u32)fps_val); p=scopy(p," FPS"); *p=0; int bw=(int)slen(b)*8+18;
        fill_round((int)W-bw-10,8,bw,24,6,15,rgb(20,22,30)); dstr((int)W-bw-1,13,b,rgb(120,240,140),1); }
    memcpy(SCENE_NC, SCENE, (u64)W*H*4);          /* snapshot cursor-free scene */
    draw_cursor(); blit_all();                     /* full frame with cursor */
    nc_restore(mx-CURBB/2,my-CURBB/2,CURBB,CURBB); cxlast=mx; cylast=my;   /* keep SCENE cursor-free; remember pos */
}
/* fast path: only the cursor moved -> repaint just the old + new cursor rectangles */
static void present_cursor(void);
/* cheap idle update: redraw only the top bar (clock/battery) instead of the whole screen */
static void present_topbar(void){
    int th=28;
    for(int y=0;y<th;y++){ u32* d=SCENE+(u64)y*SW; u32* s=BG+(u64)y*SW; sse_copy32(d,s,(int)W); }   /* fresh wallpaper strip */
    draw_topbar();                                                                                   /* topbar is always on top */
    for(int y=0;y<th;y++){ u32* d=SCENE_NC+(u64)y*SW; u32* s=SCENE+(u64)y*SW; sse_copy32(d,s,(int)W); } /* keep cursor-free cache consistent */
    blit_region(0,0,(int)W,th);
    if(cxlast>=0 && cylast < th+CURBB) present_cursor();                                             /* re-overlay cursor if it's up there */
}
static void present_cursor(void){
    if(cxlast>=0) blit_region(cxlast-CURBB/2, cylast-CURBB/2, CURBB, CURBB);   /* erase old (SCENE is cursor-free) */
    draw_cursor();                                                             /* draw into SCENE at mx,my */
    blit_region(mx-CURBB/2, my-CURBB/2, CURBB, CURBB);                         /* show new */
    nc_restore(mx-CURBB/2, my-CURBB/2, CURBB, CURBB);                          /* restore SCENE to cursor-free */
    cxlast=mx; cylast=my;
}
static u64 g_pump_last=0; static int g_pump_hooked=0;
static void ui_pump(void){
    if(!g_pump_hooked){ g_wpa_pump=ui_pump; g_pump_hooked=1; }
    if(xhid_n) xhci_mouse_poll();
    if(g_ticks-g_pump_last>=2){ g_pump_last=g_ticks; present_cursor(); }
}

static void win_anim(int i,int opening){ (void)i; (void)opening; return;   /* animations off: windows open/close instantly (smoother) */ }
static void win_anim_unused(int i,int opening){
    if(g_safemode) return;
    int wx=WN[i].x,wy=WN[i].y,ww=WN[i].w,wh=WN[i].h;
    if(ww<=0||wh<=0||ww>ANIMW||wh>ANIMH||wx<0||wy<0||wx+ww>(int)W||wy+wh>(int)H) return;   /* skip if off-screen/too big */
    anim_hide=-1; compose_build();
    for(int r=0;r<wh;r++){ u32* s=&SCENE[(u64)(wy+r)*SW+wx]; u32* d=&WANIM[r*ANIMW]; for(int cc=0;cc<ww;cc++)d[cc]=s[cc]; }
    int frames=7;
    for(int f=0;f<=frames;f++){ int t=opening?f:(frames-f); int sc=800+200*t/frames; int al=40+216*t/frames;
        anim_hide=i; compose_build();
        int sw=ww*sc/1000, sh=wh*sc/1000; if(sw<1)sw=1; if(sh<1)sh=1; int ox=wx+(ww-sw)/2, oy=wy+(wh-sh)/2;
        for(int r=0;r<sh;r++){ int dy=oy+r; if(dy<0||dy>=(int)H)continue; int srcr=r*wh/sh; u32* srow=&WANIM[srcr*ANIMW];
            for(int cc=0;cc<sw;cc++){ int dx=ox+cc; if(dx<0||dx>=(int)W)continue; int srcc=cc*ww/sw; u32* dd=&SCENE[(u64)dy*SW+dx]; *dd=mix(*dd,srow[srcc],al); } }
        draw_cursor(); blit_all(); spin(1300000);
    }
    anim_hide=-1;
}

