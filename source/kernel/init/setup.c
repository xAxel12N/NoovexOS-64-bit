/* =========================================================================
   Setup (first-run) and Login screens
   ========================================================================= */
static void screen_bg(void){
    for(u32 y=0;y<H;y++){ u32 c=mix(rgb(16,20,34),rgb(10,12,22),(int)((y*256)/H)); for(u32 x=0;x<W;x++) SCENE[(u64)y*SW+x]=c; }
    radial(W/2,H*36/100,H*44/100,rgb(120,60,200),90); radial(W/2,H*58/100,H*34/100,rgb(40,150,180),70);
}
static int text_field(int x,int y,int w,const char*s,int len,int focus,const char*ph,int mask){
    fill_round(x,y,w,34,8,15,rgb(238,241,246));
    if(len==0 && ph){ dstr(x+12,y+10,ph,rgb(160,168,182),1); }
    else { int cx2=x+12; for(int k=0;k<len;k++){ char c=mask?'*':s[k]; dchar(cx2,y+10,c,rgb(40,46,64),1); cx2+=8; }
        if(focus&&((g_ticks/50)&1)) fill_rect(cx2+1,y+9,2,15,ACC); }
    return 0;
}

/* ================= multi-page first-time setup ================= */
static char setup_name[32]=""; static int setup_nl=0, setup_acc=0, setup_th=0;
static int su_nx,su_ny,su_nw, su_ax2[5],su_ay, su_wx2[3],su_wy;
#define NSU 7
static const char* SU_LANGS[6]={"English (United States)","Svenska (Sverige)","Deutsch (Deutschland)","Francais (France)","Espanol (Espana)","Italiano (Italia)"};
static const char* SU_KBDS[6] ={"US QWERTY","Swedish (Sverige)","German QWERTZ","French AZERTY","Spanish QWERTY","UK QWERTY"};
static const char* SU_TZS[7]  ={"(UTC-08:00) Los Angeles","(UTC-05:00) New York","(UTC+00:00) London","(UTC+01:00) Stockholm","(UTC+03:00) Moscow","(UTC+05:30) Mumbai","(UTC+09:00) Tokyo"};
static const char* SU_STORAGE[2]={"Cloud  -  files sync to your Noovex account","Local  -  files stay on this device only"};
static const char* SU_TITLE[NSU]={"Choose your language","Keyboard layout","Select your timezone","Make it yours","Sign in with Noovex","Where your files live","You are all set"};
static const char* SU_DESC[NSU]={
    "Select the language you would like to use with NoovexOS.",
    "Pick the layout that matches your keyboard.",
    "So NoovexOS shows the correct local time.",
    "Choose a name, an accent colour and a wallpaper.",
    "Connect your Noovex account - or skip and do it later.",
    "Cloud files sync to your account and return after reboot.",
    "Welcome aboard. Press Get Started to enter NoovexOS."};
static const char* SU_ACC[3]={"Sign in with Noovex","Create a Noovex account","Skip for now"};
static int g_su_page=0, g_su_lang=0, g_su_kbd=1, g_su_tz=3, g_su_acc=2, g_su_storage=0;
static int su_lsel[3]={0,1,3};
static int su_ix,su_iw,su_iy0,su_ih,su_ign,su_ig, su_cbx,su_cby,su_cbw,su_cbh, su_bbx,su_bby,su_bbw,su_bbh, su_signx,su_signy,su_signw,su_signh;
static int g_su_form=0, g_su_field=0;                       /* 0=chooser 1=signin 2=create */
static char su_email[64]="", su_uname[48]="", su_pwd[48]=""; static int su_el=0,su_ul=0,su_pl=0;
static int su_f_x,su_f_w,su_f_h, su_f_ey,su_f_uy,su_f_py;
static void su_field(int x,int y,int w,int h,const char* label,const char* val,int vlen,int focused,int mask){
    dstr(x,y-17,label,rgb(212,202,226),1);
    fill_round(x,y,w,h,8,15,focused?rgb(255,255,255):rgb(236,238,244));
    int cxp=x+14; for(int k=0;k<vlen;k++){ char ch=mask?'*':val[k]; dchar(cxp,y+h/2-7,ch,rgb(40,46,64),1); cxp+=8; }
    if(focused && (g_ticks/50)&1) fill_rect(cxp+1,y+h/2-8,2,15,ACC);
}

static void su_button(int x,int y,int w,int h,const char* label,u32 col,u32 txt,int scale){
    shadow_soft(x,y,w,h,11);
    for(int j=0;j<h;j++)for(int ii=0;ii<w;ii++){ if(cut(ii,j,w,h,11,15))continue; u32 c=mix(col,rgb(255,255,255),((h-j)*34)/h); SCENE[(u64)(y+j)*SW+(x+ii)]=c; }
    pdstr_ctr(x+w/2,y+h/2-7,label,txt,scale);
}
static void su_list(int cx,int topy,const char** it,int n,int sel){
    int iw=460, ih=33, gap=6, ix=cx-iw/2;
    su_ix=ix; su_iw=iw; su_iy0=topy; su_ih=ih; su_ign=n; su_ig=gap;
    for(int k=0;k<n;k++){ int iy=topy+k*(ih+gap);
        if(k==sel){
            shadow_soft(ix,iy,iw,ih,9);
            for(int j=0;j<ih;j++)for(int ii=0;ii<iw;ii++){ if(cut(ii,j,iw,ih,9,15))continue; SCENE[(u64)(iy+j)*SW+(ix+ii)]=rgb(250,250,253); }
            fill_round(ix+10,iy+8,4,ih-16,2,15,rgb(245,80,190));         /* accent selection bar */
            pdstr(ix+26,iy+ih/2-7,it[k],rgb(30,18,46),1);
        } else {
            for(int j=0;j<ih;j++)for(int ii=0;ii<iw;ii++){ if(cut(ii,j,iw,ih,9,15))continue; u32*p=&SCENE[(u64)(iy+j)*SW+(ix+ii)]; *p=mix(*p,rgb(30,14,52),165); }
            for(int ii=0;ii<iw;ii++){ if(!cut(ii,0,iw,ih,9,3)){u32*p=&SCENE[(u64)iy*SW+ix+ii];*p=mix(*p,rgb(210,80,180),120);} if(!cut(ii,ih-1,iw,ih,9,12)){u32*p=&SCENE[(u64)(iy+ih-1)*SW+ix+ii];*p=mix(*p,rgb(210,80,180),120);} }
            for(int j=0;j<ih;j++){ if(!cut(0,j,iw,ih,9,9)){u32*p=&SCENE[(u64)(iy+j)*SW+ix];*p=mix(*p,rgb(210,80,180),120);} if(!cut(iw-1,j,iw,ih,9,6)){u32*p=&SCENE[(u64)(iy+j)*SW+(ix+iw-1)];*p=mix(*p,rgb(210,80,180),120);} }
            pdstr(ix+26,iy+ih/2-7,it[k],rgb(230,222,244),1);
        } }
}
static void setup_present(void){
    if(!g_gm_ready)gmetrics();                 /* proportional-font widths (else pdstr draws nothing) */
    synthwave_scene();
    for(u32 y=(u32)((int)H*36/100);y<H;y++)for(u32 x=0;x<W;x++){ u32*p=&SCENE[(u64)y*SW+x]; *p=mix(*p,rgb(8,3,20),120); }
    int cx=(int)W/2, P=g_su_page;
    /* progress dots */
    { int n=NSU,dw=20,gp=8,tot=n*dw+(n-1)*gp,x0=cx-tot/2,dy=(int)H*385/1000;
      for(int k=0;k<n;k++){ int bx=x0+k*(dw+gp); u32 c=(k<=P)?rgb(245,80,190):mix(rgb(245,80,190),rgb(18,9,34),200); fill_round(bx,dy,dw,5,2,15,c); } }
    int lx=cx-228, ty=(int)H*43/100;
    const char* ptitle=(P==4&&g_su_form==2)?"Create a Noovex account":(P==4&&g_su_form==1)?"Sign in with Noovex":SU_TITLE[P];
    { char t[72],*p=t; p=scopy(p,":: "); p=scopy(p,ptitle); *p=0; dstr(lx,ty,t,rgb(255,238,248),1); }
    dstr(lx,ty+18,SU_DESC[P],rgb(190,170,206),1);
    int topy=(int)H*47/100;
    if(P==0) su_list(cx,topy,SU_LANGS,6,su_lsel[0]);
    else if(P==1) su_list(cx,topy,SU_KBDS,6,su_lsel[1]);
    else if(P==2) su_list(cx,topy,SU_TZS,7,su_lsel[2]);
    else if(P==3){
        int y=topy; dstr(cx-228,y,"Your name",rgb(224,234,248),1); y+=20; su_nw=456; su_nx=cx-su_nw/2; su_ny=y;
        fill_round(su_nx,su_ny,su_nw,36,8,15,rgb(255,255,255));
        if(setup_nl==0) dstr(su_nx+14,su_ny+11,"Type your name (optional)",rgb(150,158,172),1);
        else { int c2=su_nx+14; for(int k=0;k<setup_nl;k++){dchar(c2,su_ny+11,setup_name[k],rgb(40,46,64),1);c2+=8;} if((g_ticks/50)&1)fill_rect(c2+1,su_ny+10,2,15,ACC); }
        y=su_ny+52; dstr(cx-228,y,"Accent colour",rgb(224,234,248),1); y+=22; su_ay=y;
        { int sx=cx-228; for(int k=0;k<5;k++){ int bx=sx+k*44; su_ax2[k]=bx; if(k==setup_acc) fill_circle(bx+16,su_ay+15,18,rgb(255,255,255)); fill_circle(bx+16,su_ay+15,15,ACV[k]); } }
        y=su_ay+52; dstr(cx-228,y,"Wallpaper",rgb(224,234,248),1); y+=22; su_wy=y;
        { int sx=cx-228; for(int k=0;k<3;k++){ int bx=sx+k*64; su_wx2[k]=bx; fill_round(bx,su_wy,54,34,7,15,THSW[k]);
            if(k==setup_th){ for(int t=0;t<54;t++){putpx(bx+t,su_wy,rgb(255,255,255));putpx(bx+t,su_wy+33,rgb(255,255,255));} for(int t=0;t<34;t++){putpx(bx,su_wy+t,rgb(255,255,255));putpx(bx+53,su_wy+t,rgb(255,255,255));} } } }
    }
    else if(P==4){
        if(g_su_form==0){ su_list(cx,topy,SU_ACC,3,g_su_acc);
            dstr_ctr(cx,topy+3*(su_ih+su_ig)+16,"Enter your details on the next screen - no browser needed.",rgb(196,180,212),1); }
        else {
            su_f_w=456; su_f_x=cx-su_f_w/2; su_f_h=36; int y=topy+30; su_f_uy=-1;
            su_f_ey=y; su_field(su_f_x,y,su_f_w,su_f_h,"Username",su_uname,su_ul,g_su_field==0,0); y+=66;
            su_f_py=y; su_field(su_f_x,y,su_f_w,su_f_h,"Password",su_pwd,su_pl,g_su_field==1,1); y+=54;
            if(su_msg[0]) dstr_ctr(cx,y,su_msg,rgb(255,140,150),1);
            else dstr_ctr(cx,y,g_su_form==2?"Pick a username and password to create your account.":"TAB switches fields    ENTER submits",rgb(160,144,180),1);
        }
    }
    else if(P==5){ su_list(cx,topy,SU_STORAGE,2,g_su_storage);
        dstr_ctr(cx,topy+2*(su_ih+su_ig)+16, g_nvx_signedin?"Cloud saves your files to your account.":"Cloud needs a signed-in account - otherwise files stay local.", rgb(196,180,212),1); }
    else { /* finish */
        int scx=cx; dstr_ctr(scx,topy+16,"Everything is ready.",rgb(236,224,248),2);
        char b[80],*p=b; p=scopy(p,"Language: ");p=scopy(p,SU_LANGS[su_lsel[0]]);*p=0; dstr_ctr(scx,topy+56,b,rgb(200,188,216),1);
        p=b;p=scopy(p,"Timezone: ");p=scopy(p,SU_TZS[su_lsel[2]]);*p=0; dstr_ctr(scx,topy+76,b,rgb(200,188,216),1);
    }
    /* bottom controls */
    int byB=(int)H*90/100;
    su_cbw=170; su_cbh=44; su_cbx=cx-su_cbw/2; su_cby=byB;
    const char* clab=(P==NSU-1)?"Get Started":(P==4?(g_su_form==1?"Sign in":g_su_form==2?"Create account":"Continue"):"Continue");
    su_button(su_cbx,su_cby,su_cbw,su_cbh,clab,rgb(70,110,240),rgb(255,255,255),2);
    if(P>0){ su_bbw=96; su_bbh=34; su_bbx=cx-260; su_bby=byB+5;
        for(int j=0;j<su_bbh;j++)for(int ii=0;ii<su_bbw;ii++){ if(cut(ii,j,su_bbw,su_bbh,10,15))continue; u32*p=&SCENE[(u64)(su_bby+j)*SW+(su_bbx+ii)]; *p=mix(*p,rgb(40,20,60),150); }
        pdstr_ctr(su_bbx+su_bbw/2,su_bby+su_bbh/2-7,"Back",rgb(220,210,232),1); }
    dstr_ctr(cx,(int)H-20,"ARROWS select    ENTER continue    ESC back",rgb(150,132,168),1);
    draw_cursor(); blit_all();
}
static void su_apply(void){
    if(setup_nl>0){ int k=0; for(;k<setup_nl&&k<31;k++)g_user[k]=setup_name[k]; g_user[k]=0; } else scopy(g_user,"user");
    g_accent=setup_acc; ACC=ACV[setup_acc]; ACC2=mix(ACC,rgb(0,0,0),60);
    g_theme=setup_th; g_su_lang=su_lsel[0]; g_su_kbd=su_lsel[1]; g_su_tz=su_lsel[2]; g_cloud_fs=(g_su_storage==0 && g_nvx_signedin); build_bg(); nvx_settings_save(); if(g_cloud_fs)nvx_files_pull();
}
static void su_advance(void){
    if(g_su_page==4){
        if(g_su_form==0){
            if(g_su_acc==2){ g_su_page++; return; }                 /* skip -> finish */
            g_su_form=(g_su_acc==0)?1:2; g_su_field=0; su_msg[0]=0; return;   /* open the form */
        }
        su_apply(); su_msg[0]=0;
        int rc=(g_su_form==1)? nvx_signin(su_uname,su_pwd) : nvx_register(su_uname,su_pwd);
        if(rc==0){ if(g_nvx_user[0])scopy(g_user,g_nvx_user); g_su_form=0; g_su_page++; }   /* success -> finish */
        return;                                                     /* else stay, su_msg shows error */
    }
    if(g_su_page>=NSU-1){ su_apply(); g_state=ST_LOGIN; login_pass[0]=0; login_pl=0; return; }
    g_su_page++;
}
static int setup_input(void){
    int changed=0, P=g_su_page;
    if(mouse_evt){ int dx,dy,btn; __asm__ volatile("cli"); dx=mouse_dx;dy=mouse_dy;mouse_dx=0;mouse_dy=0;btn=mouse_btn;mouse_evt=0; __asm__ volatile("sti");
        if(dx||dy){ mx+=dx;my-=dy; if(mx<0)mx=0;if(my<0)my=0;if(mx>=(int)W)mx=W-1;if(my>=(int)H)my=H-1; changed=1; }
        if(btn&&!mouse_btn_prev){ changed=1;
            if(P<=2){ int* sel=&su_lsel[P]; for(int k=0;k<su_ign;k++) if(in_rect(mx,my,su_ix,su_iy0+k*(su_ih+su_ig),su_iw,su_ih)){ *sel=k; } }
            if(P==3){ for(int k=0;k<5;k++) if(in_rect(mx,my,su_ax2[k],su_ay,32,32)){ setup_acc=k; ACC=ACV[k]; ACC2=mix(ACC,rgb(0,0,0),60); g_accent=k; }
                for(int k=0;k<3;k++) if(in_rect(mx,my,su_wx2[k],su_wy,54,34)) setup_th=k; }
            if(P==4){ if(g_su_form==0){ for(int k=0;k<su_ign;k++) if(in_rect(mx,my,su_ix,su_iy0+k*(su_ih+su_ig),su_iw,su_ih)) g_su_acc=k; }
                else { if(in_rect(mx,my,su_f_x,su_f_ey,su_f_w,su_f_h)) g_su_field=0;
                    else if(in_rect(mx,my,su_f_x,su_f_py,su_f_w,su_f_h)) g_su_field=1; } }
            if(P==5){ for(int k=0;k<su_ign;k++) if(in_rect(mx,my,su_ix,su_iy0+k*(su_ih+su_ig),su_iw,su_ih)) g_su_storage=k; }
            if(P>0 && in_rect(mx,my,su_bbx,su_bby,su_bbw,su_bbh)){ if(P==4&&g_su_form!=0){ g_su_form=0; su_msg[0]=0; } else g_su_page--; }
            else if(in_rect(mx,my,su_cbx,su_cby,su_cbw,su_cbh)){ su_advance(); }
        }
        mouse_btn_prev=btn; }
    int c; while((c=kpop())>=0){ changed=1;
        if(c==0x1B){ if(P==4&&g_su_form!=0){ g_su_form=0; su_msg[0]=0; } else if(g_su_page>0)g_su_page--; continue; }   /* ESC */
        if(c=='\n'||c=='\r'){ su_advance(); continue; }               /* ENTER */
        if(P==4 && g_su_form!=0){                                       /* account form fields */
            if(c=='\t'){ g_su_field=!g_su_field; continue; }
            char* buf; int* len; int cap;
            if(g_su_field==0){ buf=su_uname; len=&su_ul; cap=47; } else { buf=su_pwd; len=&su_pl; cap=47; }
            if(c=='\b'){ if(*len>0)(*len)--; buf[*len]=0; } else if(c>=32&&c<127){ if(*len<cap){buf[(*len)++]=(char)c; buf[*len]=0;} }
            continue;
        }
        if(c==19){ if(P<=2 && su_lsel[P]>0)su_lsel[P]--; else if(P==4 && g_su_acc>0)g_su_acc--; else if(P==5 && g_su_storage>0)g_su_storage--; continue; }        /* up arrow */
        if(c==20){ if(P<=2 && su_lsel[P]<su_ign-1)su_lsel[P]++; else if(P==4 && g_su_acc<2)g_su_acc++; else if(P==5 && g_su_storage<1)g_su_storage++; continue; } /* down arrow */
        if(P==5 && c=='w'){ if(g_su_storage>0)g_su_storage--; continue; }
        if(P==5 && c=='s'){ if(g_su_storage<1)g_su_storage++; continue; }
        if(P<=2 && c=='w'){ if(su_lsel[P]>0)su_lsel[P]--; continue; }
        if(P<=2 && c=='s'){ if(su_lsel[P]<su_ign-1)su_lsel[P]++; continue; }
        if(P==4 && g_su_form==0 && c=='w'){ if(g_su_acc>0)g_su_acc--; continue; }
        if(P==4 && g_su_form==0 && c=='s'){ if(g_su_acc<2)g_su_acc++; continue; }
        if(P==3){ if(c=='\b'){ if(setup_nl>0)setup_nl--; } else if(c>=32&&c<127){ if(setup_nl<31)setup_name[setup_nl++]=(char)c; } setup_name[setup_nl]=0; }
    }
    static int lb=-1; int bp=(int)((g_ticks/50)&1); if(bp!=lb){lb=bp;changed=1;}
    return changed;
}

/* ---- login ---- */
static int lg_bx,lg_by,lg_bw,lg_bh, lg_px,lg_py,lg_pw, lg_pwr;
static const char *MON[12]={"January","February","March","April","May","June","July","August","September","October","November","December"};
static int lg_avx,lg_avy,lg_avr;
static int lg_sd,lg_rs,lg_sl;
static void pwr_icon(int cx,int cy,int kind,u32 c){
    if(kind==0){ soft_ring(cx,cy,12,2,c,220); for(int t=-8;t<=-1;t++)putpx(cx,cy+t,c); putpx(cx-1,cy-8,c); putpx(cx+1,cy-8,c); }
    else if(kind==1){ soft_ring(cx,cy,12,2,c,220); for(int t=-5;t<=1;t++){ putpx(cx-6+t,cy-6,c); } for(int t=0;t<6;t++){ putpx(cx-6,cy-6+t,c); putpx(cx-6+t,cy-6+t,c);} }
    else { for(int a=-9;a<=9;a++){ int yy=cy-(int)(a*a)/26; putpx(cx+a,yy,c); putpx(cx+a,yy+1,c);} for(int a=-9;a<=9;a++){ int yy=cy+6-(int)(a*a)/26; putpx(cx+a,yy,c);} }
}
static void login_present(void){
    memcpy(SCENE,BG,(u64)W*H*4);
    /* keep wallpaper vivid; only a faint top-and-bottom vignette */
    for(u32 y=0;y<H;y++){ int t=0; if(y<H/6)t=(H/6-y)*40/(H/6); else if(y>H*5/6)t=(y-H*5/6)*60/(H/6); if(t){ u32*r=&SCENE[(u64)y*SW]; for(u32 x=0;x<W;x++)r[x]=mix(r[x],rgb(6,8,16),t); } }
    /* top menu bar */
    { for(u32 x=0;x<W;x++){ u32*p=&SCENE[x]; for(int y=0;y<26;y++){ p=&SCENE[(u64)y*SW+x]; *p=mix(*p,rgb(8,10,18),150); } }
      int hh,mm,ss,yy,mo,dd; read_clock(&hh,&mm,&ss,&yy,&mo,&dd); (void)ss;
      static const char* WD3[7]={"Sun","Mon","Tue","Wed","Thu","Fri","Sat"};
      int w=(yy+ (mo<3?yy-1:yy)/4 - (mo<3?yy-1:yy)/100 + (mo<3?yy-1:yy)/400 + (int[]){0,3,2,5,0,3,5,1,4,6,2,4}[mo-1] + dd)%7;
      char tb[40],*p=tb; p=scopy(p,WD3[w]); *p++=' '; if(mo>=1&&mo<=12){ p=scopy(p,MON[mo-1]); *p++=' '; } p=udec(p,(u32)dd); *p++=' '; *p++=' '; p=u2(p,hh);*p++=':';p=u2(p,mm); *p=0;
      dstr((int)W-slen(tb)*8-14,6,tb,rgb(236,240,248),1);
      /* wifi + battery glyphs */
      int bx=(int)W-slen(tb)*8-58; for(int b=0;b<3;b++){ int hgt=3+b*3; for(int t=0;t<hgt;t++)putpx(bx+b*5,14-t,rgb(220,226,238)); putpx(bx+b*5+1,14,rgb(220,226,238)); }
      int cbx=(int)W-slen(tb)*8-96; for(int t=0;t<18;t++){ putpx(cbx+t,7,rgb(210,216,230)); putpx(cbx+t,15,rgb(210,216,230)); } for(int t=7;t<=15;t++){ putpx(cbx,t,rgb(210,216,230)); putpx(cbx+17,t,rgb(210,216,230)); } for(int t=8;t<15;t++)for(int q=2;q<12;q++)putpx(cbx+q,t,rgb(120,220,140));
      dstr(12,6,"NoovexOS",rgb(236,240,248),1); }
    int cx=(int)W/2, cy=(int)H*44/100;
    /* avatar: liquid-glass disc with the user initial + soft drop shadow */
    int R=54; lg_avx=cx; lg_avy=cy; lg_avr=R;
    for(int yy2=-R-8;yy2<=R+10;yy2++)for(int xx=-R-8;xx<=R+8;xx++){ int d2=xx*xx+(yy2-4)*(yy2-4); if(d2>=(R+3)*(R+3)&&d2<=(R+10)*(R+10)){ int px=cx+xx,py=cy+yy2; if((u32)px<W&&(u32)py<H){ u32*p=&SCENE[(u64)py*SW+px]; *p=mix(*p,rgb(0,0,0),80); } } }
    for(int yy2=-R;yy2<=R;yy2++)for(int xx=-R;xx<=R;xx++){ int d2=xx*xx+yy2*yy2; if(d2>R*R)continue; int px=cx+xx,py=cy+yy2; if((u32)px>=W||(u32)py>=H)continue; u32*p=&SCENE[(u64)py*SW+px];
        u32 glass=mix(*p,ACC,150); int grad=(yy2+R)*230/(2*R); glass=mix(rgb(255,255,255),glass,grad);
        int edge2=R-(int)isqrt32((u32)d2); if(edge2<3)glass=mix(glass,rgb(255,255,255),150);
        *p=glass; }
    { char ini=g_user[0]; if(ini>='a'&&ini<='z')ini-=32; char sst[2]={ini?ini:'U',0}; dstr_ctr(cx+1,cy-15+1,sst,rgb(0,0,0),5); dstr_ctr(cx,cy-15,sst,rgb(255,255,255),5); }
    { char nm[32]; int k=0; for(;g_user[k]&&k<31;k++)nm[k]=(k==0&&g_user[k]>='a'&&g_user[k]<='z')?g_user[k]-32:g_user[k]; nm[k]=0;
      dstr_ctr(cx+1,cy+R+20+1,nm,rgb(0,0,0),3); dstr_ctr(cx,cy+R+20,nm,rgb(248,250,255),3); }
    /* password pill — compact liquid glass, macOS proportions */
    lg_pw=210; int ph=32; lg_px=cx-lg_pw/2; lg_py=cy+R+56;
    glass_liquid(lg_px,lg_py,lg_pw,ph,ph/2,rgb(236,240,250),26,6);
    lg_bw=26; lg_bh=26; lg_bx=lg_px+lg_pw-30; lg_by=lg_py+3;
    if(login_pl==0){ dstr_ctr(lg_px+lg_pw/2-6,lg_py+11,"Enter Password",rgb(90,100,120),1); }
    else { int c2=lg_px+18; for(int k=0;k<login_pl&&k<14;k++){ fill_circle(c2,lg_py+ph/2,4,rgb(40,50,70)); c2+=14; }
        if(((g_ticks/40)&1)){ int cxp=lg_px+18+login_pl*14-4; fill_rect(cxp,lg_py+9,2,ph-18,ACC); } }
    /* arrow unlock */
    int hov=in_rect(mx,my,lg_bx,lg_by,lg_bw,lg_bh);
    for(int yy2=-13;yy2<=13;yy2++)for(int xx=-13;xx<=13;xx++){ if(xx*xx+yy2*yy2>13*13)continue; int px=lg_bx+13+xx,py=lg_by+13+yy2; if((u32)px<W&&(u32)py<H){ u32*p=&SCENE[(u64)py*SW+px]; u32 base=mix(*p,ACC,hov?255:215); if(yy2<0)base=mix(base,rgb(255,255,255),210); *p=base; } }
    { int ax=lg_bx+11, ay=lg_by+13; for(int t=-4;t<=4;t++)putpx(ax+t,ay,rgb(255,255,255)); for(int t=0;t<5;t++){ putpx(ax+4-t,ay-t,rgb(255,255,255)); putpx(ax+4-t,ay+t,rgb(255,255,255)); } }
    /* help ? circle to the right */
    { int qx=lg_px+lg_pw+22, qy=lg_py+ph/2; soft_ring(qx,qy,9,2,rgb(210,216,230),200); dstr_ctr(qx,qy-7,"?",rgb(220,226,238),1); }
    { const char* hint=login_pl?"Press Return to unlock":"Your password is required to enable Touch ID"; dstr_ctr(cx,lg_py+ph+22,hint,rgb(206,214,230),1); }
    /* bottom controls: Shut Down / Restart / Sleep */
    { int by=(int)H-92, sp=140; lg_sd=cx-sp; lg_rs=cx; lg_sl=cx+sp;
      int h0=in_rect(mx,my,lg_sd-24,by-24,48,48), h1=in_rect(mx,my,lg_rs-24,by-24,48,48), h2=in_rect(mx,my,lg_sl-24,by-24,48,48);
      pwr_icon(lg_sd,by,0,h0?rgb(255,255,255):rgb(220,226,238)); pwr_icon(lg_rs,by,1,h1?rgb(255,255,255):rgb(220,226,238)); pwr_icon(lg_sl,by,2,h2?rgb(255,255,255):rgb(220,226,238));
      dstr_ctr(lg_sd,by+22,"Shut Down",rgb(206,214,230),1); dstr_ctr(lg_rs,by+22,"Restart",rgb(206,214,230),1); dstr_ctr(lg_sl,by+22,"Sleep",rgb(206,214,230),1); }
    draw_cursor(); blit_all();
}
static int login_input(void){
    int changed=0;
    if(mouse_evt){ int dx,dy,btn; __asm__ volatile("cli"); dx=mouse_dx;dy=mouse_dy;mouse_dx=0;mouse_dy=0;btn=mouse_btn;mouse_evt=0; __asm__ volatile("sti");
        if(dx||dy){ mx+=dx;my-=dy; if(mx<0)mx=0;if(my<0)my=0;if(mx>=(int)W)mx=W-1;if(my>=(int)H)my=H-1; changed=1; }
        if(btn&&!mouse_btn_prev){ int by=(int)H-92;
            if(in_rect(mx,my,lg_bx,lg_by,lg_bw,lg_bh)){ g_state=ST_DESKTOP; if(g_rec_openterm){open_win(W_TERM);g_rec_openterm=0;} }
            else if(in_rect(mx,my,lg_sd-24,by-24,48,48)){ blit_all(); for(;;)__asm__ volatile("hlt"); }
            else if(in_rect(mx,my,lg_rs-24,by-24,48,48)){ blit_all(); outb(0x64,0xFE); }
            else if(in_rect(mx,my,lg_sl-24,by-24,48,48)){ /* sleep: dim screen */ }
            changed=1; }
        mouse_btn_prev=btn; }
    int c; while((c=kpop())>=0){ changed=1;
        if(c=='\n'){ g_state=ST_DESKTOP; if(g_rec_openterm){open_win(W_TERM);g_rec_openterm=0;} }
        else if(c=='\b'){ if(login_pl>0)login_pl--; }
        else if(c>=32&&c<127){ if(login_pl<31)login_pass[login_pl++]=(char)c; }
        login_pass[login_pl]=0; }
    static int lb=-1; int bp=(int)((g_ticks/50)&1); if(bp!=lb){lb=bp;changed=1;}
    return changed;
}

