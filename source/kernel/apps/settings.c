/* =========================================================================
   Settings (expanded)
   ========================================================================= */
static const char *THN[6]={"Aurora","Wireframe","Crane","Porsche","Field","Desert"};
static u32 wall_thumb[6][64*40]; static int wall_thumb_ready=0; static void wall_thumbs_build(void);
static const u32 THSW[6]={rgb(40,90,220),rgb(70,80,160),rgb(84,90,150),rgb(150,150,155),rgb(200,150,60),rgb(150,110,150)};
static const char *ACN[5]={"Blue","Violet","Teal","Rose","Amber"};
static const u32 ACV[5]={rgb(60,120,224),rgb(140,90,220),rgb(30,170,160),rgb(220,80,120),rgb(230,150,40)};
static const char *CUN[5]={"Arrow","Large","White","Cross","Dot"};
static int st_wifiscan_x,st_wifiscan_y,st_wifijoin_x,st_wifijoin_y; static int st_wifi_ln=0;
static u32 g_ram_mb;
static int g_refresh_hz=60, g_panel_oled=0, g_mouse_accel=0, g_fps_overlay=0;   /* Display settings */
static int g_setcat=1;
static void build_bg(void);
/* sync accent + wallpaper to/from the signed-in Noovex account */
static void nvx_settings_save(void){
    if(!g_nvx_signedin) return;
    char body[96],*p=body; p=scopy(p,"{\"accent\":"); *p++=(char)('0'+g_accent); p=scopy(p,",\"theme\":"); *p++=(char)('0'+g_theme); *p++='}'; *p=0;
    char hdr[380],*hp=hdr; hp=scopy(hp,"Content-Type: application/json\r\nAuthorization: Bearer ");hp=scopy(hp,g_nvx_token);hp=scopy(hp,"\r\n");*hp=0;
    https_request(NVX_HOST,"/api/public/settings","POST",hdr,body,(int)(p-body),http_buf,(int)sizeof(http_buf));
}
static void nvx_settings_load(void){
    if(!g_nvx_signedin) return;
    char hdr[360],*hp=hdr; hp=scopy(hp,"Authorization: Bearer ");hp=scopy(hp,g_nvx_token);hp=scopy(hp,"\r\n");*hp=0;
    int n=https_request(NVX_HOST,"/api/public/settings","GET",hdr,0,0,http_buf,(int)sizeof(http_buf));
    if(n<=0) return; int he=0; for(int i=0;i+3<n;i++)if(http_buf[i]=='\r'&&http_buf[i+1]=='\n'&&http_buf[i+2]=='\r'&&http_buf[i+3]=='\n'){he=i+4;break;}
    int a=nvx_json_int(http_buf+he,n-he,"accent",-1), t=nvx_json_int(http_buf+he,n-he,"theme",-1);
    if(a>=0&&a<5){ g_accent=a; ACC=ACV[a]; ACC2=mix(ACC,rgb(0,0,0),60); }
    if(t>=0&&t<3) g_theme=t;
    build_bg();
}
static int g_wifipop=0, g_wifix=0; static u32 g_wifi_mmio=0;
static void wifi_gui_connect(int idx); static void wifi_gui_scan(int full);
static int g_wifi_sel=-1;              /* selected AP index for password entry */
static char g_wifi_pw[64]; static int g_wifi_pwlen=0; static int g_wifi_entry=0;  /* entry mode active */
static char g_wifi_conn[33]={0};      /* SSID we last connected to (for status) */
static int g_wifi_scanning=0; static char g_wifi_status[48]={'r','e','a','d','y',0};

static const char *SC_LBL[8]={"System","Personalization","Network","Display","Battery","Storage","Terminal","About"};
static const char *SC_SUB[8]={"Clock, power and build integrity","Wallpaper, accent colour and pointer","Ethernet and Wi-Fi","Refresh rate, panel and pointer speed","Charge level and power source","Disks, data partition and install","Theme, font and cursor for nsh","Version and hardware"};
static int st_hx[64],st_hy[64],st_hw[64],st_hh[64],st_hid[64],st_hn=0, st_x0,st_w0;
static void st_hit(int x,int y,int w,int h,int id){ if(st_hn<64){ st_hx[st_hn]=x; st_hy[st_hn]=y; st_hw[st_hn]=w; st_hh[st_hn]=h; st_hid[st_hn]=id; st_hn++; } }
static void st_icon(int x,int y,int k,int on){
    static const u32 IC[8]={rgb(120,128,146),rgb(226,90,150),rgb(60,130,230),rgb(40,170,170),rgb(70,180,100),rgb(236,150,50),rgb(52,58,76),rgb(100,120,230)};
    u32 c=IC[k]; fill_round(x,y,26,26,7,15,c); u32 w=rgb(255,255,255);
    switch(k){
    case 0: fill_circle(x+13,y+13,7,w); fill_circle(x+13,y+13,3,c); for(int a=0;a<4;a++){ int dx=(a==0)?0:(a==1)?8:(a==2)?0:-8, dy=(a==0)?-8:(a==1)?0:(a==2)?8:0; fill_rect(x+12+dx,y+12+dy,3,3,w); } break;
    case 1: fill_circle(x+13,y+13,8,w); fill_circle(x+10,y+10,2,c); fill_circle(x+16,y+10,2,rgb(250,200,80)); fill_circle(x+10,y+16,2,rgb(80,180,240)); break;
    case 2: for(int rr=3;rr>=1;rr--){ fill_circle(x+13,y+18,rr*4-1,w); fill_circle(x+13,y+18,rr*4-3,c); } fill_rect(x+1,y+19,24,6,c); fill_circle(x+13,y+18,2,w); break;
    case 3: fill_round(x+5,y+6,16,11,2,15,w); fill_rect(x+7,y+8,12,7,c); fill_rect(x+11,y+18,4,2,w); fill_rect(x+8,y+20,10,2,w); break;
    case 4: fill_round(x+5,y+8,15,10,2,15,w); fill_rect(x+20,y+11,2,4,w); fill_rect(x+7,y+10,8,6,c); break;
    case 5: fill_round(x+5,y+6,16,14,3,15,w); fill_rect(x+7,y+14,12,1,c); fill_circle(x+17,y+17,1,c); break;
    case 6: fill_round(x+4,y+6,18,14,3,15,w); dstr(x+6,y+6,">",c,1); fill_rect(x+13,y+16,6,2,c); break;
    case 7: fill_circle(x+13,y+13,8,w); fill_rect(x+12,y+12,3,6,c); fill_rect(x+12,y+8,3,2,c); break; }
    (void)on; }
static void st_toggle(int x,int y,int on,int id){ u32 tr=on?ACC:rgb(206,211,222); fill_round(x,y,44,24,12,15,tr); int kx=on?x+32:x+12; fill_circle(kx,y+12,9,rgb(255,255,255)); st_hit(x-4,y-4,52,32,id); }
static int st_segw(const char** l,int n){ int w=0; for(int k=0;k<n;k++)w+=slen(l[k])*8+24; return w+4; }
static void st_seg(int x,int y,const char** l,int n,int sel,int idb){ int w=st_segw(l,n); fill_round(x,y,w,30,8,15,rgb(232,235,241)); int cx=x+2;
    for(int k=0;k<n;k++){ int bw=slen(l[k])*8+24; if(k==sel){ fill_round(cx,y+2,bw,26,7,15,rgb(255,255,255)); } else if(in_rect(mx,my,cx,y+2,bw,26))fill_round(cx,y+2,bw,26,7,15,rgb(242,244,248));
        dstr_ctr(cx+bw/2,y+8,l[k],k==sel?ACC2:rgb(90,98,116),1); st_hit(cx,y+2,bw,26,idb+k); cx+=bw; } }
static void st_btn(int x,int y,int w,const char* t,int id,int kind){ int hov=in_rect(mx,my,x,y,w,32);
    u32 bg=kind==1?(hov?ACC2:ACC):kind==2?(hov?rgb(252,232,232):rgb(255,242,242)):(hov?rgb(228,232,240):rgb(238,241,246)); u32 fg=kind==1?rgb(255,255,255):kind==2?rgb(200,60,60):INK;
    fill_round(x,y,w,32,8,15,bg); dstr_ctr(x+w/2,y+9,t,fg,1); st_hit(x,y,w,32,id); }
static int st_card(int y,int h,const char* title){ if(title){ dstr(st_x0+4,y,title,rgb(120,128,146),1); y+=22; }
    fill_round(st_x0,y,st_w0,h,12,15,rgb(226,229,236)); fill_round(st_x0+1,y+1,st_w0-2,h-2,11,15,rgb(255,255,255)); return y; }
static void st_rowtext(int y,const char* t,const char* d){ if(!g_gm_ready)gmetrics(); pdstr(st_x0+20,d?y+14:y+22,t,INK,1); if(d)dstr(st_x0+20,y+34,d,rgb(130,138,156),1); }
static void st_sep(int y){ fill_rect(st_x0+20,y,st_w0-40,1,rgb(236,238,243)); }
static char* st_mb(char* p,u64 bytes){ u64 mb=bytes/1048576ULL; if(mb>=1024){ p=udec(p,(u32)(mb/1024)); *p++='.'; p=udec(p,(u32)((mb%1024)*10/1024)); return scopy(p," GB"); } p=udec(p,(u32)mb); return scopy(p," MB"); }
static void settings_draw(int i){
    win_frame(i,"Settings"); st_hn=0;
    int wx=WN[i].x, wy=WN[i].y+TITLEH, ww=WN[i].w, wh=WN[i].h-TITLEH; int sbw=ww>=760?224:180;
    fill_rect(wx+1,wy,sbw,wh-1,rgb(243,244,247)); fill_rect(wx+sbw+1,wy,ww-sbw-2,wh-1,rgb(247,248,251)); fill_rect(wx+sbw,wy,1,wh-1,rgb(228,230,236));
    { fill_round(wx+12,wy+14,sbw-24,54,12,15,rgb(255,255,255)); fill_circle(wx+40,wy+41,17,ACC); char ini=g_user[0]; if(ini>='a'&&ini<='z')ini-=32; char s[2]={ini?ini:'U',0}; dstr_ctr(wx+40,wy+34,s,rgb(255,255,255),1);
      if(!g_gm_ready)gmetrics(); pdstr(wx+66,wy+25,g_user,INK,1); dstr(wx+66,wy+43,g_uid==0?"Administrator":"Standard user",rgb(130,138,156),1); }
    for(int k=0;k<8;k++){ int iy=wy+84+k*44; int on=(k==g_setcat), hov=in_rect(mx,my,wx+10,iy,sbw-20,38);
        if(on){ fill_round(wx+10,iy,sbw-20,38,9,15,rgb(255,255,255)); fill_round(wx+10,iy+10,3,18,1,15,ACC); } else if(hov)fill_round(wx+10,iy,sbw-20,38,9,15,rgb(234,237,243));
        st_icon(wx+22,iy+6,k,on); pdstr(wx+58,iy+12,SC_LBL[k],on?INK:rgb(72,80,98),1); st_hit(wx+10,iy,sbw-20,38,100+k); }
    st_x0=wx+sbw+28; st_w0=ww-sbw-56; int cy=wy+22; int X1=st_x0+st_w0-20;
    pdstr(st_x0,cy,SC_LBL[g_setcat],INK,2); cy+=34; dstr(st_x0,cy,SC_SUB[g_setcat],rgb(130,138,156),1); cy+=34;
    if(g_setcat==0){
        int y=st_card(cy,64,"Clock"); st_rowtext(y,"Show seconds","Adds seconds to the clock in the top bar"); st_toggle(X1-44,y+20,g_showsec,1); cy=y+64+20;
        y=st_card(cy,128,"Power"); st_rowtext(y,"Restart","Restart the computer now"); st_btn(X1-110,y+16,110,"Restart",2,1); st_sep(y+64);
        st_rowtext(y+64,"Crash test","Triggers a kernel panic screen (for testing)"); st_btn(X1-110,y+80,110,"Crash",3,2); cy=y+128+20;
        y=st_card(cy,64,"Build"); { char b[64],*p=b; p=scopy(p,"NoovexOS 64 v"); p=scopy(p,(char*)g_buildsig.version); *p=0; st_rowtext(y,b,g_sig_ok?"Integrity check passed at boot":"Integrity check reported a mismatch at boot"); }
        fill_round(X1-110,y+20,110,24,12,15,g_sig_ok?rgb(226,246,233):rgb(253,236,226)); dstr_ctr(X1-55,y+25,g_sig_ok?"Verified":"Mismatch",g_sig_ok?rgb(40,140,80):rgb(200,110,40),1);
    } else if(g_setcat==1){
        wall_thumbs_build(); int cols=3, gap=14, tw=(st_w0-40-gap*(cols-1))/cols; if(tw>132)tw=132; int th=tw*10/16;
        int y=st_card(cy,2*(th+30)+26,"Wallpaper");
        for(int k=0;k<6;k++){ int bx=st_x0+20+(k%cols)*(tw+gap), by=y+18+(k/cols)*(th+30);
            if(k==g_theme&&!g_wallmode)fill_round(bx-3,by-3,tw+6,th+6,9,15,ACC);
            for(int yy=0;yy<th;yy++){ int ty=yy*40/th; for(int xx=0;xx<tw;xx++){ if(cut(xx,yy,tw,th,6,15))continue; putpx(bx+xx,by+yy,wall_thumb[k][ty*64+xx*64/tw]); } }
            dstr_ctr(bx+tw/2,by+th+7,THN[k],k==g_theme?INK:rgb(120,128,146),1); st_hit(bx,by,tw,th,10+k); }
        cy=y+2*(th+30)+26+20;
        y=st_card(cy,128,"Colour and pointer"); st_rowtext(y,"Accent colour",ACN[g_accent]);
        for(int k=0;k<5;k++){ int bx=X1-16-(4-k)*40; if(k==g_accent)fill_circle(bx,y+32,15,ACV[k]); fill_circle(bx,y+32,k==g_accent?13:12,k==g_accent?rgb(255,255,255):ACV[k]); if(k==g_accent)fill_circle(bx,y+32,9,ACV[k]); st_hit(bx-15,y+17,30,30,20+k); }
        st_sep(y+64); st_rowtext(y+64,"Pointer style",0); { int w=st_segw(CUN,5); st_seg(X1-w,y+81,CUN,5,g_cursor,30); }
    } else if(g_setcat==2){
        if(!pci_n)pci_scan(); int ek=nic_index();
        int y=st_card(cy,64,"Ethernet"); st_rowtext(y,ek>=0?(char*)nic_name(pci_ven[ek],pci_dev[ek]):"No wired adapter",net_ok?"Connected":"Not started - run 'net' in the Terminal");
        if(net_ok){ char b[40],*p=b; p=udec(p,my_ip>>24);*p++='.';p=udec(p,(my_ip>>16)&255);*p++='.';p=udec(p,(my_ip>>8)&255);*p++='.';p=udec(p,my_ip&255); *p=0; dstr(X1-slen(b)*8,y+24,b,INK,1); }
        cy=y+64+20;
        int rows=g_apn<8?g_apn:8; int extra=(g_wifi_entry&&g_wifi_sel>=0)?44:0; int ch=64+(rows?rows*44:40)+extra;
        y=st_card(cy,ch,"Wi-Fi"); { u32 wm=rtw_find_mmio(); st_rowtext(y,wm?"Wireless adapter":"No Wi-Fi adapter",wm?(g_rxnum?g_wifi_status:"Run wifi init in the Terminal first"):"This computer has no supported Wi-Fi card"); }
        st_wifiscan_x=X1-96; st_wifiscan_y=y+16; st_btn(X1-96,y+16,96,g_wifi_scanning?"Scanning":"Scan",40,1); st_sep(y+64);
        int ry=y+64; st_wifi_ln=0;
        if(!rows)dstr(st_x0+20,ry+14,"Press Scan to find networks",rgb(150,158,176),1);
        for(int q=0;q<rows;q++){ int open=(g_aps[q].sec==0), isconn=0;
            if(g_wifi_conn[0]){ int kk=0; while(g_aps[q].ssid[kk]&&g_wifi_conn[kk]&&g_aps[q].ssid[kk]==g_wifi_conn[kk])kk++; isconn=(!g_aps[q].ssid[kk]&&!g_wifi_conn[kk]); }
            int sel=(g_wifi_sel==q); if(sel||in_rect(mx,my,st_x0+8,ry+4,st_w0-16,36))fill_round(st_x0+8,ry+4,st_w0-16,36,8,15,sel?mix(ACC,rgb(255,255,255),215):rgb(245,247,250));
            for(int b=0;b<3;b++)fill_rect(st_x0+22+b*5,ry+26-b*5,3,4+b*5,isconn?ACC:rgb(120,128,146));
            dstr(st_x0+48,ry+14,g_aps[q].ssid[0]?g_aps[q].ssid:"<hidden>",INK,1);
            const char* tag=isconn?"Connected":open?"Open":"Secured"; dstr(X1-slen(tag)*8,ry+14,tag,isconn?rgb(40,150,90):rgb(130,138,156),1);
            st_hit(st_x0+8,ry+4,st_w0-16,36,50+q); ry+=44;
            if(sel&&!open&&g_wifi_entry){ fill_round(st_x0+20,ry+2,st_w0-150,32,8,15,rgb(240,242,246)); char mask[40]; int n=g_wifi_pwlen>36?36:g_wifi_pwlen; for(int z=0;z<n;z++)mask[z]='*'; mask[n]=0;
                dstr(st_x0+32,ry+11,mask[0]?mask:"Password",mask[0]?INK:rgb(150,158,176),1); if((g_ticks/50)&1)fill_rect(st_x0+33+n*8,ry+10,2,15,ACC);
                st_wifijoin_x=X1-100; st_wifijoin_y=ry+2; st_btn(X1-100,ry+2,100,"Join",41,1); ry+=44; } }
    } else if(g_setcat==3){
        static const char* HZ[3]={"30 Hz","60 Hz","120 Hz"}; static const char* PN[2]={"LCD","OLED"}; static const char* MA[3]={"Off","Low","High"};
        int y=st_card(cy,64*3,"Screen"); st_rowtext(y,"Refresh rate","How often the desktop redraws"); { int w=st_segw(HZ,3); st_seg(X1-w,y+17,HZ,3,g_refresh_hz==30?0:g_refresh_hz==60?1:2,60); } st_sep(y+64);
        st_rowtext(y+64,"Panel type","OLED shifts pixels slightly to avoid burn-in"); { int w=st_segw(PN,2); st_seg(X1-w,y+81,PN,2,g_panel_oled,63); } st_sep(y+128);
        { char b[48],*p=b; p=udec(p,W); *p++='x'; p=udec(p,H); p=scopy(p,"  "); p=udec(p,BPP); p=scopy(p,"-bit colour"); *p=0; st_rowtext(y+128,"Resolution",b); }
        cy=y+192+20;
        y=st_card(cy,64*3,"Performance"); st_rowtext(y,"Mouse acceleration","Moves faster when you move the mouse quickly"); { int w=st_segw(MA,3); st_seg(X1-w,y+17,MA,3,g_mouse_accel,65); } st_sep(y+64);
        st_rowtext(y+64,"2D acceleration","Use SSE for fills and frame copies"); st_toggle(X1-44,y+84,g_accel2d,68); st_sep(y+128);
        st_rowtext(y+128,"Performance overlay","Show an FPS counter in the top-right corner"); st_toggle(X1-44,y+148,g_fps_overlay,70);
    } else if(g_setcat==4){
        battery_tick(); int y=st_card(cy,150,0);
        if(bat_present){ char b[16],*p=b; p=udec(p,(u32)bat_pct); *p++='%'; *p=0; pdstr(st_x0+24,y+22,b,INK,3);
            dstr(st_x0+24,y+72,bat_charging?"Plugged in, charging":"On battery",bat_charging?rgb(40,150,90):rgb(150,110,70),1);
            int bw=st_w0-48; fill_round(st_x0+24,y+98,bw,16,8,15,rgb(230,233,240)); fill_round(st_x0+24,y+98,(int)bat_pct*bw/100,16,8,15,bat_charging?rgb(80,190,120):(bat_pct<=20?rgb(230,90,90):ACC));
            dstr(st_x0+24,y+124,bat_via_ec?"Read from the Embedded Controller":"Estimated",rgb(130,138,156),1); }
        else { pdstr(st_x0+24,y+30,"No battery detected",INK,2); dstr(st_x0+24,y+70,"Running on mains power, or the battery can't be read.",rgb(130,138,156),1); dstr(st_x0+24,y+92,"Desktops and virtual machines usually have none.",rgb(130,138,156),1); }
    } else if(g_setcat==5){
        gdisk_ensure(); int y=st_card(cy,120,"System disk");
        if(g_dktype!=DK_NONE){ char b[64],*p=b; p=scopy(p,gdisk_name()); p=scopy(p," disk"); *p=0; pdstr(st_x0+20,y+16,b,INK,1);
            p=b; p=st_mb(p,(u64)g_dksectors*512ULL); if(g_dktype==DK_NVME&&g_nvme_bsz>512)p=scopy(p,"   4K sectors"); *p=0; dstr(X1-slen(b)*8,y+16,b,rgb(120,128,146),1);
            long used=0; int nf=0; for(int k=0;k<FS_N;k++)if(FS[k].used){ used+=FS[k].len; nf++; }
            int bw=st_w0-40; fill_round(st_x0+20,y+46,bw,12,6,15,rgb(230,233,240)); if(disk_ok){ long cap=(long)NVX_MAX*FS_CAP; int fw=(int)((long long)used*bw/cap); if(fw<6)fw=6; fill_round(st_x0+20,y+46,fw,12,6,15,ACC); }
            p=b; if(disk_ok){ p=udec(p,(u32)nf); p=scopy(p," files, "); p=udec(p,(u32)(used/1024)); p=scopy(p," KB in "); p=scopy(p,g_nvx_note); } else { p=scopy(p,"Files are kept in memory only - "); p=scopy(p,g_nvx_note[0]?g_nvx_note:"no data partition"); } *p=0;
            dstr(st_x0+20,y+72,b,disk_ok?rgb(40,150,90):rgb(170,120,60),1); }
        else { pdstr(st_x0+20,y+20,"No disk found",INK,1); dstr(st_x0+20,y+48,"No IDE, SATA or NVMe disk was detected.",rgb(130,138,156),1); }
        cy=y+120+20;
        y=st_card(cy,64,"Removable"); { char b[64],*p=b; if(msd_ok){ p=scopy(p,"USB drive, "); p=st_mb(p,(u64)(msd_lastlba+1)*512ULL); } else p=scopy(p,"No USB drive connected"); *p=0; st_rowtext(y,b,msd_ok?"Mounted - open it in File Explorer":"Plug in a USB stick to use it"); }
        cy=y+64+20;
        y=st_card(cy,64,"Install"); st_rowtext(y,"Install NoovexOS","Put this system on a disk so it starts without the ISO"); st_btn(X1-120,y+16,120,"Open Setup",95,1);
    } else if(g_setcat==6){
        int tw=(st_w0-40-5*10)/6; if(tw<70)tw=70; int th=64;
        int y=st_card(cy,th+50,"Theme");
        for(int k=0;k<6;k++){ const TTheme* t=&TTH[k]; int bx=st_x0+20+k*(tw+10), by=y+16; if(bx+tw>st_x0+st_w0-10)break;
            if(k==g_tth)fill_round(bx-3,by-3,tw+6,th+6,11,15,ACC); fill_round(bx,by,tw,th,9,15,t->bg); fill_round(bx,by,tw,14,9,1|2,t->bar);
            fill_circle(bx+9,by+7,2,rgb(236,106,100)); fill_circle(bx+16,by+7,2,rgb(240,190,80)); fill_circle(bx+23,by+7,2,rgb(110,200,120));
            dstr(bx+8,by+22,"$",t->green,1); fill_rect(bx+20,by+27,tw-40,3,t->fg); fill_rect(bx+8,by+42,tw-30,3,t->dim); fill_rect(bx+8,by+54,tw-50,3,t->blue); fill_rect(bx+tw-26,by+52,7,11,t->cur);
            dstr_ctr(bx+tw/2,by+th+8,t->name,k==g_tth?INK:rgb(120,128,146),1); st_hit(bx,by,tw,th,80+k); }
        cy=y+th+50+16;
        { static const char* OP[4]={"Off","Low","Medium","High"}; int sel=g_topa>=100?0:g_topa>=90?1:g_topa>=80?2:3;
          y=st_card(cy,128,"Window"); st_rowtext(y,"Transparency","See the desktop through the terminal"); { int w=st_segw(OP,4); st_seg(X1-w,y+17,OP,4,sel,110); } st_sep(y+64);
          st_rowtext(y+64,"Blur behind","Soften what shows through, like frosted glass"); st_toggle(X1-44,y+84,g_tblur,114); cy=y+128+16; }
        static const char* FN[2]={"Smooth","Pixel"}; static const char* CS[3]={"Block","Bar","Underline"};
        y=st_card(cy,64*3,"Text"); st_rowtext(y,"Font","Anti-aliased or classic bitmap glyphs"); { int w=st_segw(FN,2); st_seg(X1-w,y+17,FN,2,g_tfont?0:1,85); } st_sep(y+64);
        st_rowtext(y+64,"Cursor",0); { int w=st_segw(CS,3); st_seg(X1-w,y+81,CS,3,g_tcur,87); } st_sep(y+128);
        st_rowtext(y+128,"Status bar","Shows the user, current folder and size"); st_toggle(X1-44,y+148,g_tstatus,90);

    } else {
        int y=st_card(cy,110,0); draw_logo(st_x0+62,y+22,54,ACC,256); pdstr(st_x0+120,y+26,"NoovexOS 64",INK,2);
        { char b[48],*p=b; p=scopy(p,"Version "); p=scopy(p,(char*)g_buildsig.version); p=scopy(p,"  -  built from scratch"); *p=0; dstr(st_x0+120,y+64,b,rgb(120,128,146),1); }
        cy=y+110+20; y=st_card(cy,64*4,"Hardware");
        char b[96],*p;
        st_rowtext(y,"Processor",0); { char pb[48]; int n=slen(g_brand); if(n>(st_w0-200)/8){ int m=(st_w0-200)/8; for(int q=0;q<m;q++)pb[q]=g_brand[q]; pb[m]=0; } else scopy(pb,g_brand)[0]=0; dstr(X1-slen(pb)*8,y+24,pb,rgb(90,98,116),1); } st_sep(y+64);
        st_rowtext(y+64,"Memory",0); p=b; p=udec(p,g_ram_mb); p=scopy(p," MB"); *p=0; dstr(X1-slen(b)*8,y+88,b,rgb(90,98,116),1); st_sep(y+128);
        st_rowtext(y+128,"Display",0); p=b; p=udec(p,W); *p++='x'; p=udec(p,H); p=scopy(p,"  "); p=udec(p,BPP); p=scopy(p,"-bit"); *p=0; dstr(X1-slen(b)*8,y+152,b,rgb(90,98,116),1); st_sep(y+192);
        st_rowtext(y+192,"Kernel",0); p=b; p=udec(p,g_buildsig.image_size/1024); p=scopy(p," KB, x86-64 long mode"); *p=0; dstr(X1-slen(b)*8,y+216,b,rgb(90,98,116),1);
    }
}
static void settings_click(int px,int py){
    for(int h=st_hn-1;h>=0;h--) if(in_rect(px,py,st_hx[h],st_hy[h],st_hw[h],st_hh[h])){ int id=st_hid[h];
        if(id>=100&&id<108){ g_setcat=id-100; if(g_setcat!=2)g_wifi_entry=0; return; }
        if(id==1){ g_showsec^=1; return; } if(id==2){ blit_all(); outb(0x64,0xFE); return; } if(id==3){ panic(0,"Crash test triggered from Settings."); return; }
        if(id>=10&&id<16){ g_theme=id-10; g_wallmode=0; build_bg(); return; }
        if(id>=20&&id<25){ g_accent=id-20; ACC=ACV[g_accent]; ACC2=mix(ACC,rgb(0,0,0),60); build_bg(); return; }
        if(id>=30&&id<35){ g_cursor=id-30; return; }
        if(id==40){ wifi_gui_scan(1); return; }
        if(id==41){ if(g_wifi_entry&&g_wifi_sel>=0){ g_wifi_pw[g_wifi_pwlen]=0; wifi_gui_connect(g_wifi_sel); g_wifi_entry=0; } return; }
        if(id>=50&&id<58){ int q=id-50; if(g_aps[q].sec==0){ g_wifi_sel=q; g_wifi_entry=0; wifi_gui_connect(q); } else { g_wifi_sel=q; g_wifi_entry=1; g_wifi_pwlen=0; g_wifi_pw[0]=0; } return; }
        if(id>=60&&id<63){ static const int hz[3]={30,60,120}; g_refresh_hz=hz[id-60]; return; }
        if(id==63||id==64){ g_panel_oled=(id==64); return; }
        if(id>=65&&id<68){ g_mouse_accel=id-65; return; }
        if(id==68){ g_accel2d^=1; return; } if(id==70){ g_fps_overlay^=1; return; }
        if(id>=80&&id<86){ g_tth=id-80; return; }
        if(id>=110&&id<114){ static const int op[4]={100,90,80,65}; g_topa=op[id-110]; return; } if(id==114){ g_tblur^=1; return; } if(id==85||id==86){ g_tfont=(id==85); return; }
        if(id>=87&&id<90){ g_tcur=id-87; return; } if(id==90){ g_tstatus^=1; term_geom(); return; }
        if(id==95){ ip_open(ST_DESKTOP); return; }
        return; }
}

