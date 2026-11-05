static int g_bmbar_on(void){ return g_bmbar&&nbmarks>0; }

const char* browser_cur_url(void){ return g_cururl; }
static int g_br_down=0, g_lclc=0, g_lclx=0, g_lcly=0; static u64 g_lclt=0;
static void web_mouse(int px,int py){
    if(!g_haspage||web_busy()) return;
    { extern int frame_hit(int,int); extern void frame_mouse(int,int,int); if(frame_hit(px,py)){ frame_mouse(NWE_OP_MOVE,px,py); frame_mouse(NWE_OP_DOWN,px,py); frame_mouse(NWE_OP_UP,px,py); return; } frame_blur(); }
    nwe_req* r=&g_req;
    { u64 now=g_ticks; int dx=px-g_lclx, dy=py-g_lcly; if(now-g_lclt<=45 && dx*dx+dy*dy<=36 && g_lclc<3) g_lclc++; else g_lclc=1; g_lclt=now; g_lclx=px; g_lcly=py; }
    for(unsigned k=0;k<sizeof(*r);k++)((u8*)r)[k]=0; r->x=px-g_cx; r->y=py-g_cy; r->btn=1; r->key=g_lclc; web_call(NWE_OP_DOWN,r); web_info(r);
    g_br_down=1;
    if(r->nav[0]){ g_br_down=0; web_process_nav(r); return; }
}
static int g_bq_down;
typedef struct { int k, x, y, a, b; char c; } bqev;
#define BQN 256
static bqev g_bq[BQN]; static int g_bqh, g_bqt;
int browser_mouse_down(void){ return g_br_down||g_bq_down; }
static void browser_mouse_held_now(int px,int py){
    if(!g_br_down||!g_haspage||web_busy()||g_page!=3) return;
    if(px==g_lastmx&&py==g_lastmy) return;
    g_lastmx=px; g_lastmy=py;
    static nwe_req hreq; nwe_req* r=&hreq; for(unsigned k=0;k<sizeof(*r);k++)((u8*)r)[k]=0; r->x=px-g_cx; r->y=py-g_cy; r->btn=1; web_call(NWE_OP_MOVE,r); web_info(r);
}
static void browser_mouse_release_now(int px,int py){
    if(!g_br_down) return;
    g_br_down=0;
    if(!g_haspage||web_busy()||g_page!=3) return;
    nwe_req* r=&g_req;
    for(unsigned k=0;k<sizeof(*r);k++)((u8*)r)[k]=0; r->x=px-g_cx; r->y=py-g_cy; r->btn=1; r->key=g_lclc; web_call(NWE_OP_UP,r); web_info(r);
    web_process_nav(r);
}
int browser_rclick(int px,int py){
    if(g_page!=3||!g_haspage||web_busy()||g_isimg) return 0;
    if(!(px>=g_cx&&px<g_cx+g_cw&&py>=g_cy&&py<g_cy+g_ch)) return 0;
    nwe_req* r=&g_req;
    for(unsigned k=0;k<sizeof(*r);k++)((u8*)r)[k]=0; r->x=px-g_cx; r->y=py-g_cy; r->btn=2; web_call(NWE_OP_DOWN,r);
    for(unsigned k=0;k<sizeof(*r);k++)((u8*)r)[k]=0; r->x=px-g_cx; r->y=py-g_cy; r->btn=2; web_call(NWE_OP_UP,r);
    int used=r->ret==1; web_info(r);
    if(r->nav[0]) web_process_nav(r);
    return used;
}
int browser_drop_file(int px,int py,int slot){
    if(g_page!=3||!g_haspage||web_busy()||g_isimg) return 0;
    if(!(px>=g_cx&&px<g_cx+g_cw&&py>=g_cy&&py<g_cy+g_ch)) return 0;
    if(slot<0||!FS[slot].used) return 0;
    nwe_req* r=&g_req;
    for(unsigned k=0;k<sizeof(*r);k++)((u8*)r)[k]=0; r->x=px-g_cx; r->y=py-g_cy; r->url=FS[slot].name; r->data=fsd(slot); r->len=FS[slot].len; web_call(NWE_OP_DROPFILE,r);
    web_info(r);
    if(r->nav[0]) web_process_nav(r);
    sset(web_stat,"Dropped a file on the page");
    return 1;
}
int browser_mclick(int px,int py){
    if(g_page!=3||!g_haspage||web_busy()||g_isimg) return 0;
    if(!(px>=g_cx&&px<g_cx+g_cw&&py>=g_cy&&py<g_cy+g_ch)) return 0;
    nwe_req* r=&g_req;
    for(unsigned k=0;k<sizeof(*r);k++)((u8*)r)[k]=0; r->x=px-g_cx; r->y=py-g_cy; r->btn=3; web_call(NWE_OP_DOWN,r);
    for(unsigned k=0;k<sizeof(*r);k++)((u8*)r)[k]=0; r->x=px-g_cx; r->y=py-g_cy; r->btn=3; web_call(NWE_OP_UP,r);
    if(r->ret==2&&r->href[0]&&g_ntab<NTAB){ static char nu[512]; int k=0; for(;r->href[k]&&k<511;k++)nu[k]=r->href[k]; nu[k]=0; tab_new(); sset(g_url,nu); g_urllen=slen(g_url); browser_go(1); return 1; }
    web_info(r);
    if(r->nav[0]) web_process_nav(r);
    return 1;
}
static void browser_click_now(int px,int py){
    if(site_menu_click(px,py)) return;
    if(perm_bar_click(px,py)) return;
    if(sug_click(px,py)) return;
    if(bmbar_click(px,py)) return;
    if(!g_urlfocus&&g_page==3&&g_cururl[0]&&px>=br_barx&&px<br_barx+28&&py>=br_bary&&py<br_bary+28){ g_smenu=!g_smenu; return; }
    if(g_page==3&&g_puw&&in_rect(px,py,g_pux,g_puy,g_puw,g_puh)){ if(web_busy())return; if(py<g_puy+30){ if(px>=g_pucx&&px<g_pucx+22){ popup_close(); g_puw=0; } return; } popup_mouse(NWE_OP_MOVE,px,py); popup_mouse(NWE_OP_DOWN,px,py); popup_mouse(NWE_OP_UP,px,py); return; }
    int tsy=WN[W_BROWSER].y+TITLEH+6, tsh=26;
    if(py>=tsy && py<tsy+tsh){
        if(g_ntab<NTAB && px>=tab_plusx && px<tab_plusx+24){ tab_new(); return; }
        for(int k=0;k<g_ntab;k++){ int tx=tab_x[k]; if(px>=tx && px<tx+tab_w-4){ if(g_ntab>1 && px>=tx+tab_w-24){ tab_close(k); return; } tab_switch(k); return; } }
        return; }
    if(in_rect(px,py,g_starx,g_stary,g_starw,28)){ if(g_cururl[0])bm_toggle(g_cururl); return; }
    if(in_rect(px,py,g_findbx,g_findby,g_findbw,28)){ g_findmode=!g_findmode; g_urlfocus=0; g_findn=-1; return; }
    if(in_rect(px,py,g_cookbx,g_findby,g_cookbw,28)){ sset(g_url,"about:cookies"); g_urllen=slen(g_url); browser_go(1); return; }
    if(in_rect(px,py,tab_savex,br_bary,tab_savew,28)){ browser_save(); return; }
    if(in_rect(px,py,br_bkx,br_bary,br_bkw,28)){ if(backn>0){ extern int g_http_mode; sset(g_url,backstk[--backn]); g_urllen=slen(g_url); g_http_mode=1; browser_go(0); g_http_mode=0; } return; }
    if(in_rect(px,py,br_rlx,br_bary,br_rlw,28)){ extern int g_http_mode; if(g_cururl[0]){ sset(g_url,g_cururl); g_urllen=slen(g_url);} g_http_mode=2; browser_go(0); g_http_mode=0; return; }
    if(in_rect(px,py,br_barx,br_bary,br_barw,28)){ g_urlfocus=1; g_urlsel=1; return; }
    if(in_rect(px,py,br_gx,br_bary,br_gw,28)){ browser_go(1); return; }
    if(in_rect(px,py,br_hx,br_bary,br_hw,28)){ g_page=0; g_urlfocus=0; g_isimg=0; return; }
    g_urlfocus=0;
    for(int k=0;k<lk_n;k++){ if(px>=lk_bx[k]&&px<lk_bx[k]+lk_bw[k]&&py>=lk_y[k]&&py<lk_y[k]+lk_bh[k]){
        int t=lk_tgt[k];
        if(t==10){ sset(g_url,"example.com"); } else if(t==11){ sset(g_url,"noovexos.site"); } else if(t==13){ sset(g_url,"https://en.wikipedia.org/"); } else if(t==14){ sset(g_url,"about:js"); }
        else if(t==15){ sset(g_url,"https://en.wikipedia.org/wiki/Special:Random"); }
        else if(t==20){ sset(g_url,"about:bookmarks"); } else if(t==21){ sset(g_url,"about:history"); }
        else if(t==30){ g_urlfocus=1; g_urlsel=1; g_url[0]=0; g_urllen=0; return; }
        else if(t==1){ g_page=1; return; } else if(t==0){ g_page=0; return; }
        g_urllen=slen(g_url); browser_go(1); return; } }
    if(g_page==3 && !g_isimg && px>=g_cx && px<g_cx+g_cw && py>=g_cy && py<g_cy+g_ch) web_mouse(px,py);
}
static void browser_wheelxy_now(int sc,int hs){
    if(g_page!=3||!g_haspage||web_busy()) return;
    if(!sc&&!hs) return;
    nwe_req* r=&g_req; for(unsigned k=0;k<sizeof(*r);k++)((u8*)r)[k]=0;
    if(frame_wheel(mx,my,-sc*60,hs*60)) return;
    r->x=mx-g_cx; r->y=my-g_cy; r->key=-sc*60; r->btn=hs*60; web_call(NWE_OP_WHEEL,r); web_info(r);
}
void browser_wheelxy(int sc,int hs);
void browser_wheel(int sc){
    extern volatile unsigned char kdown[128]; int sh=kdown[0x2A]||kdown[0x36];
    if(sh) browser_wheelxy(0,-sc); else browser_wheelxy(sc,0);
}
static int browser_xback_now(void){
    if(g_page!=3||backn<=0||web_busy()) return 0;
    extern int g_http_mode; sset(g_url,backstk[--backn]); g_urllen=slen(g_url); g_http_mode=1; browser_go(0); g_http_mode=0; return 1;
}
void browser_media_pause(void){
    if(g_page!=3||!g_haspage||web_busy()) return;
    nwe_req* r=&g_req; for(unsigned k=0;k<sizeof(*r);k++)((u8*)r)[k]=0;
    r->data="document.querySelectorAll('video,audio').forEach(function(e){e.pause()})"; web_call(NWE_OP_EVAL,r);
}
int browser_media_fast(void){
    extern int desk_win_exposed(int i); extern void present_rect(int x,int y,int w,int h);
    if(!WN[W_BROWSER].open||WN[W_BROWSER].min||g_page!=3||!g_haspage||g_isimg||web_busy()) return 0;
    if(!desk_win_exposed(W_BROWSER)) return 0;
    nwe_req* r=&g_req; for(unsigned k=0;k<sizeof(*r);k++)((u8*)r)[k]=0;
    r->fb=SCENE; r->stride=(int)SW; r->fbw=(int)W; r->fbh=(int)H;
    web_call(NWE_OP_MEDIA,r);
    if(r->ret<0) return 1;
    if(r->ret>0) present_rect(r->x,r->y,r->w,r->h);
    return 0;
}

static void browser_crashed(void){
    extern int g_web_failed; g_web_failed=0; if(g_crash_shown>=3)return; g_crash_shown++;
    hb b={0,0,0}; hb_put(&b,"<!doctype html><html><head><title>This page stopped</title>"); hb_put(&b,PAGE_CSS);
    hb_put(&b,"</head><body><main><h1>This page stopped</h1><div class=card><p>The page crashed or stopped responding, so the browser stopped it. The rest of NoovexOS is fine.</p><p class=muted>Address: "); hb_esc(&b,g_url);
    hb_put(&b,"</p>"); if(g_crash_why[0]){ hb_put(&b,"<p>Reason: "); hb_esc(&b,g_crash_why); hb_put(&b,"</p>"); g_crash_why[0]=0; } hb_put(&b,"<p>Press Reload to try again, or open another address. about:pending still shows what the page was waiting for.</p></div></main></body></html>");
    show_page("about:crashed",&b,"This page stopped"); }
static int bq_drain(void);
int browser_tick(void){
    { extern int web_nested(void); if(web_nested()||web_busy()) return 0; }
    { extern void nwk_net_step(void); nwk_net_step(); }
    { extern int g_web_failed; if(g_web_failed)browser_crashed(); }
    if(!WN[W_BROWSER].open||WN[W_BROWSER].min) { g_bqh=g_bqt=0; g_bq_down=0; web_tick_flush(); return 0; }
    int qd=0; { extern int g_web_pump_ok; g_web_pump_ok=1; qd=bq_drain(); g_web_pump_ok=0; }
    if(g_page!=3||!g_haspage||g_isimg||web_busy()) return qd;
    if((u64)(g_ticks-g_lasttick)<2) return qd;
    g_lasttick=g_ticks;
    nwe_req* r=&g_req; for(unsigned k=0;k<sizeof(*r);k++)((u8*)r)[k]=0;
    { extern int g_web_pump_ok; g_web_pump_ok=1; web_call(NWE_OP_TICK,r); g_web_pump_ok=0; } web_info(r);
    int d=r->ret||r->dirty||qd;
    { extern int frame_tick(void); if(frame_tick())d=1; }
    if(r->nav[0]){ extern int g_web_pump_ok; g_web_pump_ok=1; web_process_nav(r); g_web_pump_ok=0; d=1; }
    web_tick_flush();
    return d;
}
static int map_key(int c){
    switch(c){ case 17: return NWE_K_LEFT; case 18: return NWE_K_RIGHT; case 19: return NWE_K_UP; case 20: return NWE_K_DOWN;
        case 2: return NWE_K_HOME; case 5: return NWE_K_END; case 6: return NWE_K_PGUP; case 7: return NWE_K_PGDN; case 14: return 127; }
    return (unsigned char)c;
}
static void browser_key_now(char c,int mods){
    if(g_findmode){
        if(c=='\n'||c==27||c=='\b'||(c>=32&&c<127)){
            if(c==27){ g_findmode=0; g_findn=-1; return; }
            if(c=='\b'){ if(g_findlen>0)g_findstr[--g_findlen]=0; }
            else if(c>=32){ if(g_findlen<60){ g_findstr[g_findlen++]=c; g_findstr[g_findlen]=0; } }
            if(g_haspage&&!web_busy()){ nwe_req* r=&g_req; for(unsigned k=0;k<sizeof(*r);k++)((u8*)r)[k]=0; r->data=g_findstr; r->key=(c=='\n'); web_call(NWE_OP_FIND,r); g_findn=r->ret; web_info(r); }
        }
        return;
    }
    if(!g_urlfocus){
        if(g_page==3&&g_haspage&&!web_busy()){
            nwe_req* r=&g_req; for(unsigned k=0;k<sizeof(*r);k++)((u8*)r)[k]=0;
            unsigned char uc=(unsigned char)c; int kv;
            if((mods&1)&&uc>=1&&uc<=26&&uc!=8&&uc!=9&&uc!=10&&uc!=13) kv='a'+uc-1; else kv=map_key(uc);
            r->key=kv|(mods<<24);
            if(frame_focus()>=0&&frame_key(r->key)) return;
            web_call(NWE_OP_KEY,r); web_info(r);
            int handled=r->ret;
            if(r->nav[0]){ web_process_nav(r); return; }
            if(handled) return;
        }
        if(g_page!=3&&c>=33&&c<127){ g_urlfocus=1; g_urlsel=0; g_url[0]=c; g_url[1]=0; g_urllen=1; }
        return; }
    { if(mods&9){ unsigned char uc=(unsigned char)c; if(uc==1||uc=='a'||uc=='A'){ g_urlsel=1; return; } if((uc>=1&&uc<=26&&uc!=8&&uc!=9&&uc!=10&&uc!=13)||(uc>='a'&&uc<='z')||(uc>='A'&&uc<='Z')) return; } }
    if((c==19||c==20)&&g_nsug){ if(c==20)g_sugsel=g_sugsel+1>=g_nsug?0:g_sugsel+1; else g_sugsel=g_sugsel<=0?g_nsug-1:g_sugsel-1; return; }
    if(c=='\n'&&g_sugsel>=0&&g_sugsel<g_nsug){ sug_pick(g_sugsel); return; }
    if(c=='\n'){ g_urlsel=0; sug_clear(); browser_go(1); }
    else if(c=='\t'){ if(g_acsug[0]){ sset(g_url,g_acsug); g_urllen=slen(g_url); } g_urlsel=0; }
    else if(c=='\b'){ if(g_urlsel||(g_urllen>0&&seq(g_url,g_cururl))){ g_url[0]=0; g_urllen=0; g_urlsel=0; } else if(g_urllen>0)g_url[--g_urllen]=0; }
    else if(c==27){ g_urlfocus=0; if(g_cururl[0]){ sset(g_url,g_cururl); g_urllen=slen(g_url); } }
    else if(c>=32&&c<127){ if(g_urlsel||(g_urllen>0&&seq(g_url,g_cururl))){ g_url[0]=0; g_urllen=0; g_urlsel=0; } if(g_urllen<4000){ g_url[g_urllen++]=c; g_url[g_urllen]=0; } }
}

static int bq_empty(void){ return g_bqh==g_bqt; }
static bqev* bq_last(void){ return bq_empty()?0:&g_bq[(g_bqt+BQN-1)%BQN]; }
static void bq_push(int k,int x,int y,int a,int b,char c){
    int n=(g_bqt+1)%BQN; if(n==g_bqh)g_bqh=(g_bqh+1)%BQN;
    bqev* e=&g_bq[g_bqt]; e->k=k; e->x=x; e->y=y; e->a=a; e->b=b; e->c=c; g_bqt=n; }
void browser_click(int px,int py){ if(web_busy()&&g_ur_on&&py>=g_ur_y&&py<g_ur_y+28){ if(px>=g_ur_sx&&px<g_ur_sx+78){ extern volatile int g_busy_esc; g_busy_esc=1; g_ur_on=0; return; } if(px>=g_ur_wx&&px<g_ur_wx+78){ g_ur_wait=nwk_ms()+15000; g_ur_on=0; return; } } if(g_page==3&&!g_isimg&&px>=g_cx&&px<g_cx+g_cw&&py>=g_cy&&py<g_cy+g_ch)g_bq_down=1; bq_push(1,px,py,0,0,0); }
void browser_mouse_held(int px,int py){ bqev* l=bq_last(); if(l&&l->k==2){ l->x=px; l->y=py; return; } bq_push(2,px,py,0,0,0); }
void browser_mouse_release(int px,int py){ g_bq_down=0; bq_push(3,px,py,0,0,0); }
void browser_wheelxy(int sc,int hs){ if(!sc&&!hs)return; bqev* l=bq_last(); if(l&&l->k==4&&l->x==mx&&l->y==my){ l->a+=sc; l->b+=hs; return; } bq_push(4,mx,my,sc,hs,0); }
int browser_xback(void){ if(g_page!=3||backn<=0)return 0; bq_push(6,0,0,0,0,0); return 1; }
void browser_key(char c){
    extern volatile unsigned char kdown[128]; extern int kalt; extern int g_key_ctrl, g_key_shift, g_key_rep;
    int mods=(g_key_ctrl?1:0)|((g_key_shift||kdown[0x2A]||kdown[0x36])?2:0)|((kdown[0x38]||kalt)?4:0)|(kdown[0x5B]?8:0)|(g_key_rep?16:0);
    bq_push(5,0,0,mods,0,c); }
int browser_input_pending(void){ return !bq_empty(); }
static int bq_drain(void){
    int any=0;
    for(int guard=0;guard<BQN&&!bq_empty()&&!web_busy();guard++){
        bqev e=g_bq[g_bqh]; g_bqh=(g_bqh+1)%BQN; any=1;
        if(e.k==1)browser_click_now(e.x,e.y);
        else if(e.k==2){ int smx=mx,smy=my; browser_mouse_held_now(e.x,e.y); mx=smx; my=smy; }
        else if(e.k==3)browser_mouse_release_now(e.x,e.y);
        else if(e.k==4){ int smx=mx,smy=my; mx=e.x; my=e.y; browser_wheelxy_now(e.a,e.b); mx=smx; my=smy; }
        else if(e.k==5)browser_key_now(e.c,e.a);
        else if(e.k==6)browser_xback_now();
    }
    return any; }
