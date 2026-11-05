static int perm_bar_click(int px,int py){
    if(!g_pbar_y||!g_pq.on||py<g_pbar_y||py>=g_pbar_y+40)return 0;
    if(px>=g_pbar_cx){ perm_answer(0,0); return 1; }
    if(px>=g_pbar_bx){ perm_answer(2,1); return 1; }
    if(px>=g_pbar_ax){ perm_answer(1,1); if(g_pq.name==4)sset(web_stat,"Pop-ups allowed for this site - click again to open it"); return 1; }
    return 1; }
static void site_origin(char* out,int cap){ const char* u=g_cururl; int i=0, sl=0; while(u[i]&&i<cap-1){ if(u[i]=='/'){ sl++; if(sl==3)break; } out[i]=u[i]; i++; } out[i]=0; }
static void site_menu_draw(void){
    if(!g_smenu||g_page!=3||!g_cururl[0]){ g_smenu=0; return; }
    char org[120]; site_origin(org,sizeof org); int sec=istarts_(g_cururl,"https://");
    int x=g_lockx-6, y=g_locky+30, w=330, rh=26, h=96+6*rh;
    fill_round(x-1,y-1,w+2,h+2,10,15,rgb(198,204,216)); fill_round(x,y,w,h,9,15,rgb(255,255,255));
    pdstr(x+14,y+12,(char*)perm_host(org),rgb(30,34,46),1);
    pdstr(x+14,y+32,sec?"Connection is secure":"Connection is not secure",sec?rgb(22,140,80):rgb(180,60,50),1);
    fill_rect(x+10,y+54,w-20,1,rgb(230,232,238));
    for(int i=0;i<6;i++){ int ry=y+60+i*rh; int st=perm_get_i(org,i); const char* nm=i==0?"Notifications":i==1?"Location":i==2?"Camera":i==3?"Microphone":i==4?"Pop-ups":"Clipboard";
        pdstr(x+14,ry+5,(char*)nm,rgb(50,58,76),1); const char* sv=st==1?"Allow":st==2?"Block":"Ask";
        int bw=64, bx=x+w-14-bw; fill_round(bx,ry+2,bw,22,6,15,st==1?mix(rgb(52,168,83),rgb(255,255,255),180):st==2?mix(rgb(234,67,53),rgb(255,255,255),190):rgb(238,240,244)); pdstr_ctr(bx+bw/2,ry+5,(char*)sv,rgb(40,44,52),1); }
    int ly=y+60+6*rh+8; pdstr(x+14,ly,"All site permissions",ACC,1);
    g_smx=x; g_smy=y; g_smw=w; g_smh=h; }
static int site_menu_click(int px,int py){
    if(!g_smenu)return 0;
    if(px<g_smx||px>=g_smx+g_smw||py<g_smy||py>=g_smy+g_smh){ g_smenu=0; return 0; }
    char org[120]; site_origin(org,sizeof org);
    int ry=py-(g_smy+60); if(ry>=0&&ry<6*26&&px>=g_smx+g_smw-14-64){ int i=ry/26; int st=perm_get_i(org,i); perm_set_i(org,i,(st+1)%3); return 1; }
    if(py>=g_smy+60+6*26+4){ g_smenu=0; sset(g_url,"about:permissions"); g_urllen=slen(g_url); browser_go(1); return 1; }
    return 1; }
static void show_permissions(const char* q){
    perm_load();
    if(istarts_(q,"?clear=")){ int i=0; for(const char* c=q+7;*c>='0'&&*c<='9';c++)i=i*10+(*c-'0'); if(i>=0&&i<g_nperm){ g_perm[i]=g_perm[--g_nperm]; perm_save(); } }
    else if(istarts_(q,"?all")){ g_nperm=0; perm_save(); }
    hb b={0,0,0}; hb_put(&b,"<!doctype html><html><head><title>Site permissions</title>"); hb_put(&b,PAGE_CSS); hb_put(&b,"</head><body><main><h1>Site permissions</h1><div class=card>");
    hb_put(&b,"<p class=muted>What each site may do. Sites ask the first time; Allow and Block are remembered here. The lock icon in the address bar changes them for the open site.</p>");
    if(!g_nperm)hb_put(&b,"<p>No site has asked yet.</p>");
    for(int i=0;i<g_nperm;i++){ char n[16],*p=n; p=udec(p,(u32)i); *p=0; hb_put(&b,"<p><b>"); hb_esc(&b,perm_host(g_perm[i].org)); hb_put(&b,"</b> - "); hb_put(&b,PERM_NAMES[g_perm[i].name]); hb_put(&b,": "); hb_put(&b,g_perm[i].state==1?"Allow":"Block"); hb_put(&b," <a href=\"about:permissions?clear="); hb_put(&b,n); hb_put(&b,"\">Reset</a></p>"); }
    if(g_nperm)hb_put(&b,"<p><a href=\"about:permissions?all\">Reset all</a></p>");
    hb_put(&b,"</div></main></body></html>");
    show_page("about:permissions",&b,"Site permissions"); }
static void about_page(const char* p){
    if(istarts_(p,"blank")){ web_load_html("about:blank","<html><body></body></html>",26,0); sset(g_title,"about:blank"); return; }
    if(istarts_(p,"js")||istarts_(p,"engine")||istarts_(p,"demo")){ web_load_html("about:js",JS_DEMO,(int)sizeof(JS_DEMO)-1,0); return; }
    if(istarts_(p,"cookies")){ const char* q=p+7; if(istarts_(q,"?clear")){ extern void nwk_http_cache_clear(void); cookie_clear(); webstore_clear(0,0); webstore_clear(0,1); nwk_http_cache_clear(); { extern void nwk_webcache_clear(void); nwk_webcache_clear(); } toast("Cookies, cache and site data cleared"); } else if(istarts_(q,"?del=")) cookie_delete_domain(q+5); show_cookies(); sset(g_cururl,"about:cookies"); return; }
    if(istarts_(p,"console")){ show_console(); return; }
    if(istarts_(p,"errors")){ show_errors(); return; }
    if(istarts_(p,"pending")){ show_pending(); return; }
    if(istarts_(p,"perf")){ show_perf(); return; }
    if(istarts_(p,"history")){ show_history(); return; }
    if(istarts_(p,"bookmarks")){ show_bookmarks(); return; }
    if(istarts_(p,"permissions")){ show_permissions(p+11); return; }
    if(istarts_(p,"crash")){ web_load_html("about:crash","",0,0); return; }
    web_load_html("about:blank","<html><body><h1>Unknown about: page</h1></body></html>",52,0);
}

static int g_nav_from_page;
void browser_go(int push){
    while(g_url[0]==' '){ int i=0; for(;g_url[i];i++)g_url[i]=g_url[i+1]; }
    g_urllen=slen(g_url);
    if(istarts_(g_url,"about:")){
        if(push && g_cururl[0] && !seq(g_cururl,g_url)){ if(backn<16)sset(backstk[backn++],g_cururl); else { for(int i=1;i<16;i++)sset(backstk[i-1],backstk[i]); sset(backstk[15],g_cururl); } }
        if(istarts_(g_url+6,"perf")&&g_cururl[0]&&!istarts_(g_cururl,"about:")) perf_after();
        g_urlfocus=0; g_urlsel=1; sset(g_cururl,g_url); { extern int web_err_total(void); g_go_err0=web_err_total(); } about_page(g_url+6); return; }
    if(istarts_(g_url,"javascript:")) return;
    if(!g_post_body&&is_dl_ext(g_url)&&(istarts_(g_url,"http://")||istarts_(g_url,"https://"))){ browser_download(g_url); if(g_cururl[0]){ sset(g_url,g_cururl); g_urllen=slen(g_url); } return; }
    { extern nwk_netstat g_netstat; g_go_ns0=g_netstat; g_go_t0=nwk_us(); g_go_on=1; g_nwk_slow.ms=0; g_nwk_slow.host[0]=0; }
    if(istarts_(g_url,"data:")){ const char* c=g_url+5; int b64=0; while(*c&&*c!=','){ if(istarts_(c,";base64"))b64=1; c++; } if(*c==',')c++;
        int n=slen(c); char* out=(char*)kmalloc((unsigned long)n+1); if(!out)return; int o=0;
        if(b64){ u32 acc=0; int bits=0; for(int i=0;i<n;i++){ char ch=c[i]; int v=ch>='A'&&ch<='Z'?ch-'A':ch>='a'&&ch<='z'?ch-'a'+26:ch>='0'&&ch<='9'?ch-'0'+52:ch=='+'||ch=='-'?62:ch=='/'||ch=='_'?63:-1; if(v<0)continue; acc=(acc<<6)|(u32)v; bits+=6; if(bits>=8){ bits-=8; out[o++]=(char)((acc>>bits)&0xFF); } } }
        else for(int i=0;i<n;i++){ char ch=c[i]; if(ch=='%'&&c[i+1]&&c[i+2]){ int v=0; for(int k=1;k<=2;k++){ char d=c[i+k]; v=v*16+(d>='0'&&d<='9'?d-'0':(d|32)>='a'&&(d|32)<='f'?(d|32)-'a'+10:0); } out[o++]=(char)v; i+=2; } else out[o++]=ch; }
        out[o]=0; if(push && g_cururl[0] && !seq(g_cururl,g_url)){ if(backn<16)sset(backstk[backn++],g_cururl); }
        g_urlfocus=0; g_urlsel=1; sset(g_cururl,g_url); g_page=3; web_load_html("about:blank",out,o,0); kfree(out); return; }
    g_urlfocus=0; g_urlsel=1; g_page=3;
    if(!looks_url(g_url)){ char q[600]; int qi=0,s2=0; while(g_url[s2]==' ')s2++; while(g_url[s2]&&qi<598)q[qi++]=g_url[s2++]; while(qi>0&&q[qi-1]==' ')qi--; q[qi]=0;
        char enc[1200]; urlencode(q,enc,sizeof(enc)); char su[1400]; char*p=scopy(su,"https://html.duckduckgo.com/html/?q="); int e=0; while(enc[e]&&(p-su)<1390)*p++=enc[e++]; *p=0; sset(g_url,su); g_urllen=slen(g_url); }
    int noscheme=!istarts_(g_url,"http://")&&!istarts_(g_url,"https://");
    static char full[4096];
    if(noscheme){ char* p=scopy(full,"https://"); int i=0; while(g_url[i]&&(p-full)<4090)*p++=g_url[i++]; *p=0; } else sset(full,g_url);
    host_of(full,g_host);
    if(!g_host[0]){ nwk_status("Enter a URL, e.g. example.com"); return; }
    { extern int web_err_total(void); extern void nwk_error_mark(const char*); nwk_error_mark(full); g_go_err0=web_err_total(); extern void web_pending_clear(void); web_pending_clear(); }
    if(push && g_cururl[0] && !seq(g_cururl,full)){ if(backn<16)sset(backstk[backn++],g_cururl); else { for(int i=1;i<16;i++)sset(backstk[i-1],backstk[i]); sset(backstk[15],g_cururl); } }
    { char b[300],*p=b; p=scopy(p,"Connecting to "); p=scopy(p,g_host); *p=0; nwk_status(b); }
    g_title[0]=0; compose_present();
    nwk_resp r;
    const char* meth=g_post_body?"POST":"GET";
    extern unsigned long g_fetch_ms; extern int g_fetch_n; unsigned long lt0=nwk_ms(); g_fetch_ms=0; g_fetch_n=0;
    const char* nref=g_nav_from_page?g_cururl:0;
    g_nwk_kind=1; { extern void (*g_nwk_stream)(const char*,int,const char*); g_nwk_stream=web_preview; }
    int rc=nwk_fetch(full,meth,g_post_body?"application/x-www-form-urlencoded":0,g_post_body,g_post_len,nref,&r);
    if(rc<0 && noscheme){ char* p=scopy(full,"http://"); int i=0; while(g_url[i]&&(p-full)<4090)*p++=g_url[i++]; *p=0; rc=nwk_fetch(full,meth,g_post_body?"application/x-www-form-urlencoded":0,g_post_body,g_post_len,nref,&r); }
    g_nwk_kind=0; { extern void (*g_nwk_stream)(const char*,int,const char*); g_nwk_stream=0; }
    if(g_post_body){ kfree(g_post_body); g_post_body=0; g_post_len=0; }
    if(rc<0||!r.body){
        const char* em; if(rc==-100)em="The site name could not be resolved (DNS lookup failed)."; else if(rc==-1)em="No network connection, or the address is not valid."; else if(rc==-2)em="Could not connect to the server."; else if(rc==-3)em="The connection timed out or the server sent nothing."; else if(rc==-5)em="The secure (TLS) connection failed."; else if(rc==-6)em="Too many redirects."; else em="The request failed.";
        sset(g_cururl,full); browser_error(em,-rc); sset(web_stat,em); return; }
    sset(g_cururl,r.final_url[0]?r.final_url:full); sset(g_url,g_cururl); g_urllen=slen(g_url); host_of(g_cururl,g_host);
    { char b[160],*p=b; p=scopy(p,"HTTP "); p=udec(p,(u32)r.status); p=scopy(p,"  "); int i=0; while(r.ctype[i]&&i<60)*p++=r.ctype[i++]; *p=0; nwk_status(b); }
    const u8* body=(const u8*)r.body; int blen=r.len;
    if(stristr(r.ctype,"attachment")||(!stristr(r.ctype,"html")&&!stristr(r.ctype,"text")&&!stristr(r.ctype,"xml")&&!stristr(r.ctype,"json")&&!stristr(r.ctype,"javascript")&&!looks_image(body,blen)&&r.ctype[0])||is_dl_ext(g_cururl)){
        if(!looks_image(body,blen)){ save_download(g_cururl,body,blen,0); hist_push(g_cururl); nwk_free(r.body); return; } }
    if(looks_image(body,blen)&&!stristr(r.ctype,"html")){
        if(decode_image(body,blen)){ g_isimg=1; g_page=3; g_haspage=0; char b[48],*p=b; p=scopy(p,"Image  "); p=udec(p,(u32)img_w);*p++='x';p=udec(p,(u32)img_h); *p=0; sset(web_stat,b); host_of(g_cururl,g_title); }
        else { browser_error("The image could not be decoded.",0); }
        hist_push(g_cururl); nwk_free(r.body); return; }
    if(g_lastdoc){ kfree(g_lastdoc); g_lastdoc=0; }
    g_lastdoc=(char*)kmalloc((unsigned long)blen+1); if(g_lastdoc){ for(int i=0;i<blen;i++)g_lastdoc[i]=(char)body[i]; g_lastdoc[blen]=0; g_lastdoclen=blen; }
    int istext=r.ctype[0]&&!stristr(r.ctype,"html")&&!stristr(r.ctype,"xml");
    if(!r.ctype[0]){ int i=0; while(i<blen&&(body[i]==' '||body[i]=='\n'||body[i]=='\r'||body[i]=='\t'))i++; istext=!(i<blen&&body[i]=='<'); }
    unsigned long lt1=nwk_ms(), mainf=g_fetch_ms; g_fetch_ms=0; g_fetch_n=0;
    web_load_html(g_cururl,(const char*)body,blen,istext);
    { unsigned long lt2=nwk_ms(); char b[200],*p=b; p=scopy(p,"Loaded in "); p=udec(p,(u32)(lt2-lt0)); p=scopy(p," ms: page "); p=udec(p,(u32)(lt1-lt0)); p=scopy(p," ms, "); p=udec(p,(u32)g_fetch_n); p=scopy(p," files "); p=udec(p,(u32)g_fetch_ms); p=scopy(p," ms, engine "); u32 eng=(u32)((lt2-lt1)>g_fetch_ms?(lt2-lt1)-g_fetch_ms:0); p=udec(p,eng); p=scopy(p," ms"); *p=0; (void)mainf; nwk_status(b); extern void web_console_add(const char*); web_console_add(b); klog(b); }
    nwk_free(r.body);
    hist_push(g_cururl);
    if(!g_title[0]){ int i=0; for(;g_host[i]&&i<127;i++)g_title[i]=g_host[i]; g_title[i]=0; }
    if(r.status>=400){ char b[80],*p=b; p=scopy(p,"HTTP error "); p=udec(p,(u32)r.status); *p=0; nwk_status(b); }
    else nwk_status("Done");
}
void nvx_open_url(const char* u){ sset(g_url,u); g_urllen=slen(g_url); open_win(W_BROWSER); browser_go(1); }

static void web_process_nav(nwe_req* r){
    if(!r->nav[0]) return;
    static char nav[4096]; int i=0; for(;r->nav[i]&&i<4095;i++)nav[i]=r->nav[i]; nav[i]=0;
    if(r->navmethod[0]=='P'&&r->navbody){ g_post_body=(char*)kmalloc((unsigned long)r->navblen+1); if(g_post_body){ for(int k=0;k<r->navblen;k++)g_post_body[k]=r->navbody[k]; g_post_body[r->navblen]=0; g_post_len=r->navblen; } }
    r->nav[0]=0;
    if(is_dl_ext(nav)&&!g_post_body){ browser_download(nav); return; }
    sset(g_url,nav); g_urllen=slen(g_url); g_nav_from_page=1; browser_go(1); g_nav_from_page=0;
}

#define NTAB 6
typedef struct { int used; char url[4096]; char title[64]; int page; int isimg; int backn; char back[16][4096]; } TabSt;
static TabSt tabs[NTAB]; static int g_ntab=1, g_tab=0; static int tab_x[NTAB], tab_w=172, tab_plusx, tab_savex, tab_savew;
static void save_tab(int t){ TabSt*T=&tabs[t]; T->used=1; sset(T->url,g_cururl); int i=0; for(;g_title[i]&&i<63;i++)T->title[i]=g_title[i]; T->title[i]=0; T->page=g_page; T->isimg=g_isimg; T->backn=backn; for(int k=0;k<backn&&k<16;k++)sset(T->back[k],backstk[k]); }
static void load_tab(int t){ TabSt*T=&tabs[t]; backn=T->backn; for(int k=0;k<backn&&k<16;k++)sset(backstk[k],T->back[k]);
    if(T->page==3&&T->url[0]){ extern int g_http_mode; g_cururl[0]=0; sset(g_url,T->url); g_urllen=slen(g_url); g_http_mode=1; browser_go(0); g_http_mode=0; return; }
    g_page=T->page; g_url[0]=0; g_urllen=0; g_cururl[0]=0; g_title[0]=0; g_haspage=0; }
static void tab_switch(int t){ if(t==g_tab||t<0||t>=g_ntab)return; save_tab(g_tab); g_tab=t; load_tab(t); }
void browser_profile_switch(void){
    nhist=0; nbmarks=0; g_perm_loaded=0; g_nperm=0; g_pq.on=0; g_smenu=0;
    for(int t=0;t<NTAB;t++){ tabs[t].used=0; tabs[t].url[0]=0; tabs[t].title[0]=0; tabs[t].backn=0; }
    g_ntab=1; g_tab=0; tabs[0].used=1;
    g_url[0]=0; g_urllen=0; g_cururl[0]=0; g_host[0]=0; g_title[0]=0; g_isimg=0; g_page=0; backn=0; g_haspage=0; sset(web_stat,"New Tab");
    { extern void ck_profile_switch(void); ck_profile_switch(); }
}
static void tab_new(void){ if(g_ntab>=NTAB)return; save_tab(g_tab); int t=g_ntab++; g_tab=t; tabs[t].used=1; tabs[t].url[0]=0; tabs[t].title[0]=0;
    g_url[0]=0; g_urllen=0; g_cururl[0]=0; g_host[0]=0; g_title[0]=0; g_isimg=0; g_page=0; backn=0; g_haspage=0; sset(web_stat,"New Tab"); }
static void tab_close(int t){ if(g_ntab<=1||t<0||t>=g_ntab)return; int cur=(t==g_tab); if(!cur)save_tab(g_tab); for(int k=t;k<g_ntab-1;k++)tabs[k]=tabs[k+1]; g_ntab--; if(!cur){ if(g_tab>t)g_tab--; return; } if(g_tab>=g_ntab)g_tab=g_ntab-1; load_tab(g_tab); }
static void browser_save(void){
    if(g_page!=3||!g_lastdoc){ sset(web_stat,"Nothing to save"); return; }
    char nm[32],*p=nm; p=scopy(p,"page"); p=udec(p,(u32)(++g_dln)); sset(p,".html");
    int slot=fs_save_bin(nm,(const u8*)g_lastdoc,g_lastdoclen,1);
    if(slot>=0){ char b[64],*q=b; q=scopy(q,"Saved to Files: "); q=scopy(q,nm); *q=0; sset(web_stat,b); }
    else sset(web_stat,"Save failed (disk full)");
}

static u32* g_pc; static int g_pcw, g_pch, g_pccap, g_pcok;
static void pc_save(int x,int y,int w,int h){
    if(w<=0||h<=0||x<0||y<0||x+w>(int)W||y+h>(int)H){ g_pcok=0; return; }
    int need=w*h; if(need>g_pccap){ if(g_pc)kfree(g_pc); g_pc=(u32*)kmalloc((u64)need*4); g_pccap=g_pc?need:0; }
    if(!g_pc){ g_pcok=0; return; }
    for(int j=0;j<h;j++){ const u32* s=SCENE+(u64)(y+j)*SW+x; u32* d=g_pc+(u64)j*w; kmemcpy(d,s,(unsigned long)w*4); }
    g_pcw=w; g_pch=h; g_pcok=1; }
static int pc_show(int x,int y,int w,int h){
    if(!g_pcok||!g_pc||x<0||y<0||x+w>(int)W||y+h>(int)H)return 0;
    int cw=w<g_pcw?w:g_pcw, ch=h<g_pch?h:g_pch;
    for(int j=0;j<ch;j++){ u32* d=SCENE+(u64)(y+j)*SW+x; const u32* s=g_pc+(u64)j*g_pcw; kmemcpy(d,s,(unsigned long)cw*4); }
    if(cw<w)fill_rect(x+cw,y,w-cw,h,rgb(255,255,255)); if(ch<h)fill_rect(x,y+ch,cw,h-ch,rgb(255,255,255));
    return 1; }
static void web_paint(int x,int y,int w,int h){
    if(!g_haspage) return;
    if(web_busy()){ if(!pc_show(x,y,w,h)){ fill_rect(x,y,w,h,rgb(255,255,255)); pdstr(x+16,y+16,"Loading...",rgb(120,130,150),1); }
        { extern unsigned long web_busy_ms(void); unsigned long bm=web_busy_ms(); g_ur_on=0; if(bm>8000&&nwk_ms()>=g_ur_wait){ int bw=w>520?500:w-20, bx=x+(w-bw)/2, by=y+10; fill_round(bx-1,by-1,bw+2,66,11,15,rgb(196,200,212)); fill_round(bx,by,bw,64,10,15,rgb(255,255,255)); pdstr(bx+16,by+10,"Page unresponsive",rgb(30,34,44),1); pdstr(bx+16,by+34,"Wait for it to respond, or stop it.",rgb(90,96,110),1); g_ur_sx=bx+bw-90; g_ur_wx=bx+bw-176; g_ur_y=by+18; fill_round(g_ur_wx,g_ur_y,78,28,8,15,rgb(232,236,244)); pdstr(g_ur_wx+22,g_ur_y+6,"Wait",rgb(30,34,44),1); fill_round(g_ur_sx,g_ur_y,78,28,8,15,ACC); pdstr(g_ur_sx+12,g_ur_y+6,"Stop page",rgb(255,255,255),1); g_ur_on=1; } else if(bm>3000){ char t[48],*p=t; p=scopy(p,"Loading the page... "); p=udec(p,(u32)(bm/1000)); p=scopy(p," s  (Esc stops)"); *p=0; int tw=pw_str(t,1)+20; fill_round(x+w-tw-10,y+h-34,tw,24,8,15,rgb(40,44,56)); pdstr(x+w-tw,y+h-30,t,rgb(240,242,248),1); } }
        return; }
    static nwe_req preq; nwe_req* r=&preq;
    if(w!=g_lastvw||h!=g_lastvh){ for(unsigned k=0;k<sizeof(*r);k++)((u8*)r)[k]=0; r->w=w; r->h=h; web_call(NWE_OP_LAYOUT,r); g_lastvw=w; g_lastvh=h; web_info(r); }
    for(unsigned k=0;k<sizeof(*r);k++)((u8*)r)[k]=0;
    r->fb=SCENE; r->stride=(int)SW; r->fbw=(int)W; r->fbh=(int)H; r->x=x; r->y=y; r->w=w; r->h=h;
    long rc=web_call(NWE_OP_PAINT,r);
    if(rc==-1){ fill_rect(x,y,w,h,rgb(255,255,255)); pdstr(x+16,y+16,"This page stopped working (engine error). Reload to try again.",rgb(160,60,60),1); g_haspage=0; return; }
    web_info(r);
    pc_save(x,y,w,h);
    if(g_doch>h){ int tot=h-4; int sh=tot*h/g_doch; if(sh<24)sh=24; int rng=g_doch-h; int sy=y+2+(rng>0?(tot-sh)*g_scrolly/rng:0);
        fill_round(x+w-7,y+2,5,tot,2,15,rgb(232,234,240)); fill_round(x+w-7,sy,5,sh,2,15,rgb(170,176,192)); }
}

static void cl_edge(int x,int y,int w,int h,int in){ u32 a=in?rgb(128,128,128):rgb(255,255,255), b=in?rgb(255,255,255):rgb(64,64,64), c=in?rgb(64,64,64):rgb(128,128,128);
    fill_rect(x,y,w,1,a); fill_rect(x,y,1,h,a); fill_rect(x,y+h-1,w,1,b); fill_rect(x+w-1,y,1,h,b); if(in){ fill_rect(x+1,y+1,w-2,1,c); fill_rect(x+1,y+1,1,h-2,c); } else { fill_rect(x+1,y+h-2,w-2,1,c); fill_rect(x+w-2,y+1,1,h-2,c); } }
static int g_pux=0,g_puy=0,g_puw=0,g_puh=0,g_pucx=0;
extern int popup_state(void); extern const char* popup_url(void); extern void popup_size(int*,int*); extern int popup_why(char*,int);
extern int popup_paint(int,int,int,int,int,int,int,int,unsigned int*,int); extern void popup_mouse(int,int,int); extern void popup_close(void);
extern int frame_focus(void); extern void frame_blur(void); extern int frame_key(int); extern int frame_wheel(int,int,int,int);
static void popup_draw(int cx,int cy,int cw,int ch){
    int st=popup_state(); if(!st){ g_puw=0; return; }
    int pw=500,ph=600,tb=30; popup_size(&pw,&ph);
    if(pw>cw-40)pw=cw-40; if(ph>ch-40-tb)ph=ch-40-tb; if(pw<160||ph<100){ g_puw=0; return; }
    int x=cx+(cw-pw)/2, y=cy+(ch-ph-tb)/2;
    fill_round(x-4,y-4,pw+8,ph+tb+8,10,15,rgb(150,156,168));
    fill_round(x-2,y-2,pw+4,ph+tb+4,8,15,rgb(214,218,226));
    fill_rect(x,y,pw,tb,rgb(241,243,247)); fill_rect(x,y+tb-1,pw,1,rgb(210,214,222));
    { const char* u=popup_url(); const char* h=u; for(const char* q=u;*q;q++){ if(q[0]=='/'&&q[1]=='/'){ h=q+2; break; } }
      char t[96]; int n=0; t[n++]=' '; for(;h[0]&&h[0]!='/'&&n<60;h++)t[n++]=*h; t[n]=0;
      int sec=u[0]=='h'&&u[1]=='t'&&u[2]=='t'&&u[3]=='p'&&u[4]=='s'; const char* lb=sec?"Secure":"Not secure";
      pdstr(x+10,y+8,(char*)lb,sec?rgb(30,130,70):rgb(180,60,50),1);
      pdstr(x+10+pw_str((char*)lb,1),y+8,t,rgb(40,44,52),1); }
    int bx=x+pw-28, hov=(mx>=bx&&mx<bx+22&&my>=y+4&&my<y+26);
    fill_round(bx,y+4,22,22,4,15,hov?rgb(232,72,72):rgb(226,229,235));
    pdstr(bx+7,y+7,"x",hov?rgb(255,255,255):rgb(60,64,72),1);
    if(!popup_paint(x,y+tb,pw,ph,x,y+tb,x+pw,y+tb+ph,SCENE,(int)SW)){
        fill_rect(x,y+tb,pw,ph,rgb(255,255,255));
        if(st==3){ char why[200]; popup_why(why,sizeof why); pdstr(x+16,y+tb+20,"This pop-up window could not be loaded.",rgb(40,44,52),1); pdstr(x+16,y+tb+44,why[0]?why:"Unknown reason",rgb(150,70,60),1); }
        else pdstr(x+16,y+tb+20,"Loading...",rgb(110,116,128),1); }
    g_pux=x; g_puy=y; g_puw=pw; g_puh=ph+tb; g_pucx=bx;
}

#define SUG_N 8
static char g_sug[SUG_N][256]; static char g_sugt[SUG_N][80]; static int g_sugk[SUG_N], g_sugi[SUG_N], g_nsug=0, g_sugsel=-1, g_sugx=0, g_sugy=0, g_sugw=0;
static char g_sugq[256];
static int g_bmbar=1, g_bmy=0, g_bmx[16], g_bmw[16], g_nbmx=0;
static int sug_lc(int c){ return c>='A'&&c<='Z'?c+32:c; }
static const char* sug_bare(const char* u){ if(istarts_(u,"https://"))u+=8; else if(istarts_(u,"http://"))u+=7; if(u[0]=='w'&&u[1]=='w'&&u[2]=='w'&&u[3]=='.')u+=4; return u; }
static int sug_has(const char* h,const char* q){ int ql=slen(q); if(!ql)return 0; for(int i=0;h[i];i++){ int k=0; while(k<ql&&h[i+k]&&sug_lc(h[i+k])==sug_lc(q[k]))k++; if(k==ql)return 1; } return 0; }
static int sug_dup(const char* u){ for(int i=0;i<g_nsug;i++) if(seq(sug_bare(g_sug[i]),sug_bare(u)))return 1; return 0; }
static void sug_add(int kind,int idx,const char* u,const char* t){ if(g_nsug>=SUG_N||!u[0]||sug_dup(u))return; int i=g_nsug++; cut256(g_sug[i],u); int k=0; for(;t&&t[k]&&k<79;k++)g_sugt[i][k]=t[k]; g_sugt[i][k]=0; g_sugk[i]=kind; g_sugi[i]=idx; }
static void sug_build(void){
    if(seq(g_sugq,g_url))return;
    cut256(g_sugq,g_url); g_nsug=0; g_sugsel=-1;
    if(!g_urllen||g_urlsel)return;
    int sp=0, dot=0; for(int i=0;g_url[i];i++){ if(g_url[i]==' ')sp=1; if(g_url[i]=='.')dot=1; }
    for(int t=0;t<g_ntab;t++){ if(t==g_tab)continue; if(tabs[t].url[0]&&(sug_has(sug_bare(tabs[t].url),g_url)||sug_has(tabs[t].title,g_url)))sug_add(2,t,tabs[t].url,tabs[t].title[0]?tabs[t].title:"Open tab"); }
    for(int b=0;b<nbmarks;b++) if(sug_has(sug_bare(bmarks[b]),g_url))sug_add(1,b,bmarks[b],"Bookmark");
    for(int h=nhist-1;h>=0&&g_nsug<SUG_N-1;h--) if(sug_has(sug_bare(hist[h]),g_url))sug_add(0,h,hist[h],"History");
    if(sp||!dot){ if(g_nsug>=SUG_N)g_nsug=SUG_N-1; for(int i=g_nsug;i>0;i--){ sset(g_sug[i],g_sug[i-1]); sset(g_sugt[i],g_sugt[i-1]); g_sugk[i]=g_sugk[i-1]; g_sugi[i]=g_sugi[i-1]; } g_nsug++; cut256(g_sug[0],g_url); sset(g_sugt[0],"Search"); g_sugk[0]=3; g_sugi[0]=0; }
}
static void sug_clear(void){ g_nsug=0; g_sugsel=-1; g_sugq[0]=1; g_sugq[1]=0; }
static void sug_draw(void){
    if(!g_urlfocus||!g_nsug||!WN[W_BROWSER].open||WN[W_BROWSER].min)return;
    int x=br_barx, y=br_bary+31, w=br_barw, rh=30, h=g_nsug*rh+8;
    fill_round(x-1,y-1,w+2,h+2,10,15,rgb(198,204,216)); fill_round(x,y,w,h,9,15,rgb(255,255,255));
    for(int i=0;i<g_nsug;i++){ int ry=y+4+i*rh; int hv=(mx>=x&&mx<x+w&&my>=ry&&my<ry+rh);
        if(i==g_sugsel||hv)fill_round(x+4,ry,w-8,rh,6,15,i==g_sugsel?mix(ACC,rgb(255,255,255),200):rgb(241,243,248));
        u32 ic=g_sugk[i]==2?rgb(52,168,83):g_sugk[i]==1?rgb(251,188,4):g_sugk[i]==3?ACC:rgb(140,148,164);
        fill_circle(x+18,ry+15,5,ic);
        char line[200]; int n=0; const char* src=g_sugk[i]==3?g_sug[i]:sug_bare(g_sug[i]); if(g_sugk[i]==3){ const char* pre="Search for "; while(*pre&&n<20)line[n++]=*pre++; }
        int maxc=(w-170)/8; if(maxc<10)maxc=10; for(int k=0;src[k]&&n<maxc&&n<190;k++)line[n++]=src[k]; line[n]=0;
        pdstr(x+32,ry+7,line,rgb(32,36,48),1);
        if(g_sugk[i]!=3){ const char* t=g_sugk[i]==2?"Switch to tab":g_sugt[i]; pdstr(x+w-12-pw_str((char*)t,1),ry+7,(char*)t,rgb(130,138,156),1); } }
    g_sugx=x; g_sugy=y; g_sugw=w;
}
static int sug_pick(int i){
    if(i<0||i>=g_nsug)return 0;
    if(g_sugk[i]==2){ int t=g_sugi[i]; sug_clear(); g_urlfocus=0; tab_switch(t); return 1; }
    sset(g_url,g_sug[i]); g_urllen=slen(g_url); sug_clear(); g_urlsel=0; browser_go(1); return 1; }
static int sug_click(int px,int py){
    if(!g_urlfocus||!g_nsug)return 0;
    if(px<g_sugx||px>=g_sugx+g_sugw||py<g_sugy||py>=g_sugy+g_nsug*30+8)return 0;
    int i=(py-g_sugy-4)/30; return sug_pick(i)||1; }
static void bmbar_draw(int x,int y,int w){
    g_nbmx=0; g_bmy=y; if(!g_bmbar||!nbmarks)return;
    { extern int g_dstyle; fill_rect(x,y,w,24,g_dstyle==1?rgb(212,208,200):rgb(248,249,251)); } fill_rect(x,y+23,w,1,rgb(226,229,236));
    int bx=x+6;
    for(int b=0;b<nbmarks&&g_nbmx<16;b++){ const char* u=sug_bare(bmarks[b]); char lb[24]; int n=0; for(;u[n]&&u[n]!='/'&&n<18;n++)lb[n]=u[n]; lb[n]=0; if(u[n]&&u[n]!='/'&&n>=18){ lb[n-1]='.'; lb[n-2]='.'; }
        int bw=pw_str(lb,1)+26; if(bx+bw>x+w-6)break;
        int hv=(mx>=bx&&mx<bx+bw&&my>=y+2&&my<y+22); if(hv)fill_round(bx,y+2,bw,20,8,15,rgb(232,235,241));
        fill_circle(bx+9,y+12,4,rgb(251,188,4)); pdstr(bx+18,y+4,lb,rgb(50,58,76),1);
        g_bmx[g_nbmx]=bx; g_bmw[g_nbmx]=bw; g_nbmx++; bx+=bw+4; }
}
static int bmbar_click(int px,int py){
    if(!g_nbmx||py<g_bmy||py>=g_bmy+24)return 0;
    for(int k=0;k<g_nbmx;k++) if(px>=g_bmx[k]&&px<g_bmx[k]+g_bmw[k]){ sset(g_url,bmarks[k]); g_urllen=slen(g_url); g_urlfocus=0; browser_go(1); return 1; }
    return 1; }
void browser_draw(int i){
    extern int g_dstyle, g_round_off; int CL=(g_dstyle==1);
    { char tb[96],*p=tb; const char* tt=g_title[0]?g_title:(g_host[0]?g_host:"New Tab"); int k=0; while(tt[k]&&k<60)*p++=tt[k++]; p=scopy(p," - Browser"); *p=0; win_frame(i,CL?tb:"Browser"); } if(!g_gm_ready)gmetrics();
    if(CL) g_round_off=1;
    int x=WN[i].x+12, w=WN[i].w-24;
    int tsy=WN[i].y+TITLEH+6, tsh=26;
    u32 STRIP=CL?rgb(212,208,200):rgb(222,227,237), BAR=CL?rgb(212,208,200):rgb(250,251,253), MUTE=CL?rgb(64,64,64):rgb(96,104,122), BTN=CL?rgb(212,208,200):rgb(240,242,247);
    fill_rect(WN[i].x+2,WN[i].y+TITLEH,WN[i].w-4,tsh+8,STRIP);
    fill_rect(WN[i].x+2,tsy+tsh,WN[i].w-4,38,BAR);
    fill_rect(WN[i].x+2,tsy+tsh+38,WN[i].w-4,1,rgb(214,219,229));
    tab_w=(w-30)/g_ntab; if(tab_w>200)tab_w=200; if(tab_w<70)tab_w=70;
    for(int k=0;k<g_ntab;k++){ int tx=x+k*tab_w; tab_x[k]=tx; int act=(k==g_tab);
        if(act){ fill_round(tx,tsy,tab_w-4,tsh+6,8,3,BAR); }
        else { fill_round(tx,tsy+2,tab_w-4,tsh-4,8,15,CL?rgb(200,196,188):rgb(232,236,244)); }
        if(CL){ if(act){ fill_rect(tx,tsy,tab_w-4,1,rgb(255,255,255)); fill_rect(tx,tsy,1,tsh+6,rgb(255,255,255)); fill_rect(tx+tab_w-5,tsy,1,tsh+6,rgb(64,64,64)); fill_rect(tx+tab_w-6,tsy+1,1,tsh+5,rgb(128,128,128)); } else cl_edge(tx,tsy+2,tab_w-4,tsh-4,0); }
        const char* ti=(k==g_tab)?(g_title[0]?g_title:(g_host[0]?g_host:(g_page==0?"New Tab":"Loading"))):(tabs[k].title[0]?tabs[k].title:(tabs[k].url[0]?tabs[k].url:"New Tab"));
        const char* hu=(k==g_tab)?g_host:tabs[k].url; char ini='N';
        { const char* hh=hu; if(hh&&hh[0]){ const char* q=hh; for(const char* z=hh;*z;z++)if(z[0]=='/'&&z[1]=='/'){q=z+2;break;} if(q[0]=='w'&&q[1]=='w'&&q[2]=='w'&&q[3]=='.')q+=4; if(*q)ini=*q; } }
        if(ini>='a'&&ini<='z')ini=(char)(ini-32);
        u32 fc=rgb(66+(ini*37)%120,90+(ini*53)%110,160+(ini*29)%80);
        fill_circle(tx+16,tsy+tsh/2+(act?1:0),7,fc); char ic[2]={ini,0}; pdstr_ctr(tx+16,tsy+tsh/2-6+(act?1:0),ic,rgb(255,255,255),1);
        int maxw=tab_w-(g_ntab>1?58:38); char tt[64]; int tn=0; for(int q=0;ti[q]&&tn<60;q++){ tt[tn++]=ti[q]; tt[tn]=0; if(pw_str(tt,1)>maxw){ tn--; if(tn>2){tt[tn-2]='.';tt[tn-1]='.';} break; } } tt[tn]=0;
        pdstr(tx+28,tsy+6+(act?1:0),tt, act?INK:MUTE,1);
        if(g_ntab>1){ int cxk=tx+tab_w-18,cyk=tsy+tsh/2+(act?1:0); u32 xc=act?rgb(110,118,136):rgb(150,158,174); for(int t=-3;t<=3;t++){putpx(cxk+t,cyk+t,xc);putpx(cxk+t,cyk-t,xc);putpx(cxk+t+1,cyk+t,xc);putpx(cxk+t+1,cyk-t,xc);} } }
    tab_plusx=x+g_ntab*tab_w+2; if(g_ntab<NTAB){ fill_circle(tab_plusx+12,tsy+tsh/2,11,rgb(232,236,244)); fill_rect(tab_plusx+7,tsy+tsh/2-1,11,2,MUTE); fill_rect(tab_plusx+11,tsy+tsh/2-5,2,11,MUTE); }
    int y=tsy+tsh+6;
    br_bkx=x; br_bkw=30;
    { int a=br_bkx+15,b=y+14; u32 c=backn>0?rgb(60,68,86):rgb(190,196,208); if(backn>0)fill_circle(a,b,14,BTN); for(int t=-6;t<=6;t++){putpx(a+t,b,c);putpx(a+t,b+1,c);} for(int t=0;t<6;t++){putpx(a-6+t,b-t,c);putpx(a-6+t,b+1+t,c);putpx(a-5+t,b-t,c);putpx(a-5+t,b+1+t,c);} }
    br_rlx=br_bkx+br_bkw+6; br_rlw=30;
    { int a=br_rlx+15,b=y+14; u32 rc=rgb(60,68,86); fill_circle(a,b,14,BTN);
      for(int r=-7;r<=7;r++)for(int c=-7;c<=7;c++){ int d2=r*r+c*c; if(d2<26||d2>44)continue; if(c>=0&&r<0&&c*2>-r)continue; putpx(a+c,b+r,rc); }
      for(int t=0;t<5;t++){ putpx(a+2+t,b-6,rc); putpx(a+6,b-6+t,rc); putpx(a+2+t,b-5,rc); putpx(a+5,b-6+t,rc); } }
    g_starx=br_rlx+br_rlw+6; g_starw=30; g_stary=y; int marked=(g_cururl[0]&&bm_has(g_cururl));
    { int scx=g_starx+15, scy=y+14; if(marked)fill_circle(scx,scy,14,mix(ACC,rgb(255,255,255),190)); u32 sc=marked?ACC:rgb(130,138,156);
      static const int SX[10]={0,2,7,3,4,0,-4,-3,-7,-2}, SY[10]={-7,-2,-2,1,7,4,7,1,-2,-2};
      for(int r=-7;r<=7;r++)for(int c=-7;c<=7;c++){ int in=0; for(int k=0,m=9;k<10;m=k++){ if(((SY[k]>r)!=(SY[m]>r))&&(c*4 < (SX[m]-SX[k])*(r*4-SY[k]*4)/(SY[m]-SY[k])+SX[k]*4))in=!in; }
        if(!in)continue; int edge=0; for(int dy=-1;dy<=1&&!edge;dy++)for(int dx=-1;dx<=1;dx++){ int rr=r+dy,cc=c+dx,in2=0; for(int k=0,m=9;k<10;m=k++){ if(((SY[k]>rr)!=(SY[m]>rr))&&(cc*4 < (SX[m]-SX[k])*(rr*4-SY[k]*4)/(SY[m]-SY[k])+SX[k]*4))in2=!in2; } if(!in2){edge=1;break;} }
        if(marked||edge)putpx(scx+c,scy+r,sc); } }
    int abx=g_starx+g_starw+8; int barw=w-(abx-x)-310; if(barw<120)barw=120; br_barx=abx; br_bary=y; br_barw=barw;
    if(!CL) fill_round(abx-1,y-1,barw+2,30,14,15,g_urlfocus?ACC:rgb(214,219,229));
    fill_round(abx,y,barw,28,13,15,(g_urlfocus||CL)?rgb(255,255,255):rgb(241,243,248));
    if(g_urlfocus){ int maxch=(barw-28)/8; int st=0; if(g_urllen>maxch)st=g_urllen-maxch;
      if(g_urlsel&&g_urllen>0){ int visw=(g_urllen-st)*8; if(visw>barw-24)visw=barw-24; fill_rect(abx+13,y+6,visw+2,16,rgb(178,206,248)); }
      char vis[300]; int vn=0; for(int q=st;g_url[q]&&vn<maxch&&vn<299;q++)vis[vn++]=g_url[q]; vis[vn]=0;
      dstr(abx+14,y+7,vis,rgb(40,48,66),1);
      g_acsug[0]=0;
      if(!g_urlsel&&g_urllen>0){ for(int hh=nhist-1;hh>=0;hh--){ int m=1; for(int q=0;q<g_urllen;q++){ if((hist[hh][q]|32)!=(g_url[q]|32)){m=0;break;} } if(m&&slen(hist[hh])>g_urllen){ sset(g_acsug,hist[hh]); break; } }
        if(g_acsug[0]){ int cxp=abx+14+(g_urllen-st)*8; int rem=maxch-(g_urllen-st); if(rem>0){ char sg[128]; int sn=0; for(int q=g_urllen;g_acsug[q]&&sn<rem&&sn<126;q++)sg[sn++]=g_acsug[q]; sg[sn]=0; dstr(cxp,y+7,sg,rgb(170,178,192),1); } } }
      if(!g_urlsel&&((g_ticks/50)&1)){ int cxp=abx+14+(g_urllen-st)*8; fill_rect(cxp,y+8,2,14,ACC); } }
    else { g_acsug[0]=0; int ux=abx+12; g_lockx=abx; g_locky=y; const char* u=g_url; int sec=istarts_(u,"https://"); int pl=sec?8:istarts_(u,"http://")?7:0;
      if(pl){ u32 lc=sec?rgb(22,140,80):rgb(150,158,174); fill_round(ux,y+13,10,8,2,15,lc); for(int t=0;t<5;t++){putpx(ux+2,y+8+t,lc);putpx(ux+7,y+8+t,lc);} for(int t=2;t<8;t++)putpx(ux+t,y+8,lc); fill_rect(ux+4,y+16,2,3,rgb(241,243,248)); ux+=16; }
      int hl=0; while(u[pl+hl]&&u[pl+hl]!='/'&&u[pl+hl]!='?'&&u[pl+hl]!='#')hl++;
      char host[160]; int hn=0; for(;hn<hl&&hn<159;hn++)host[hn]=u[pl+hn]; host[hn]=0;
      int lim=abx+barw-14; char rest[300]; int rn=0;
      if(!u[0]){ pdstr(ux,y+6,"Search or enter address",rgb(150,158,174),1); }
      else { while(hn>3&&ux+pw_str(host,1)>lim){ host[--hn]=0; host[hn-1]='.'; host[hn-2]='.'; } int rx=pdstr(ux,y+6,host,rgb(30,36,52),1);
        for(int q=pl+hl;u[q]&&rn<296;q++){ rest[rn++]=u[q]; rest[rn]=0; if(rx+pw_str(rest,1)>lim){ rn--; if(rn>2){rest[rn-2]='.';rest[rn-1]='.';} break; } } rest[rn]=0;
        if(rn)pdstr(rx,y+6,rest,rgb(130,138,156),1); } }
    br_gx=abx+barw+8; br_gw=48; fill_round(br_gx,y,br_gw,28,14,15,CL?BTN:ACC); pdstr_ctr(br_gx+br_gw/2,y+6,"Go",CL?rgb(0,0,0):rgb(255,255,255),1);
    tab_savex=br_gx+br_gw+6; tab_savew=52; fill_round(tab_savex,y,tab_savew,28,14,15,BTN); pdstr_ctr(tab_savex+tab_savew/2,y+6,"Save",rgb(50,58,76),1);
    br_hx=tab_savex+tab_savew+6; br_hw=52; fill_round(br_hx,y,br_hw,28,14,15,BTN); pdstr_ctr(br_hx+br_hw/2,y+6,"Home",rgb(50,58,76),1);
    g_findbx=br_hx+br_hw+6; g_findbw=52; g_findby=y; fill_round(g_findbx,y,g_findbw,28,14,15,g_findmode?mix(ACC,rgb(255,255,255),170):BTN); pdstr_ctr(g_findbx+g_findbw/2,y+6,"Find",rgb(50,58,76),1);
    g_cookbx=g_findbx+g_findbw+6; g_cookbw=70; fill_round(g_cookbx,y,g_cookbw,28,14,15,BTN); pdstr_ctr(g_cookbx+g_cookbw/2,y+6,"Cookies",rgb(50,58,76),1);
    if(CL){ int hv; hv=(mx>=br_bkx&&mx<br_bkx+br_bkw&&my>=y&&my<y+28); cl_edge(br_bkx,y,br_bkw,28,hv&&mouse_btn); hv=(mx>=br_rlx&&mx<br_rlx+br_rlw&&my>=y&&my<y+28); cl_edge(br_rlx,y,br_rlw,28,hv&&mouse_btn); cl_edge(g_starx,y,g_starw,28,0);
        cl_edge(br_barx-1,y-1,br_barw+2,30,1); cl_edge(br_gx,y,br_gw,28,0); cl_edge(tab_savex,y,tab_savew,28,0); cl_edge(br_hx,y,br_hw,28,0); cl_edge(g_findbx,y,g_findbw,28,g_findmode); cl_edge(g_cookbx,y,g_cookbw,28,0);
        fill_rect(WN[i].x+2,tsy+tsh+38,WN[i].w-4,1,rgb(128,128,128)); fill_rect(WN[i].x+2,tsy+tsh+39,WN[i].w-4,1,rgb(255,255,255)); g_round_off=0; }
    if(web_busy()){ int pw=(int)((g_ticks*7)%(unsigned)(WN[i].w-4)); fill_rect(WN[i].x+2,tsy+tsh+37,WN[i].w-4,2,mix(ACC,BAR,200)); fill_rect(WN[i].x+2+pw/2,tsy+tsh+37,pw/2>60?60:pw/2+20,2,ACC); }
    if(g_bmbar_on()) bmbar_draw(WN[i].x+2,tsy+tsh+40,WN[i].w-4); else g_nbmx=0;
    content_rect();
    int cx=g_cx, cy=g_cy, cw=g_cw, ch=g_ch;
    lk_n=0;
    if(g_page==3 && g_isimg){
        fill_rect(cx,cy,cw,ch,rgb(246,247,250));
        int aw=cw-16, ah=ch-16, sw=img_w, sh=img_h, scn=1000;
        if(sw*scn/1000>aw) scn=aw*1000/sw; if(sh*scn/1000>ah) scn=ah*1000/sh;
        int dw=sw*scn/1000, dh=sh*scn/1000; if(dw<1)dw=1; if(dh<1)dh=1; int ox=cx+8+(aw-dw)/2, oy=cy+8+(ah-dh)/2;
        for(int r=0;r<dh;r++){ int dy=oy+r; int srcr=r*sh/dh;
            for(int c=0;c<dw;c++){ int dx=ox+c; int srcc=c*sw/dw; u8*pp=&imgpool[((srcr*sw)+srcc)*4]; u32 bgc=(((dx-ox)>>3)+((dy-oy)>>3))&1?rgb(236,238,242):rgb(255,255,255); if(pp[3]==255)putpx(dx,dy,rgb(pp[0],pp[1],pp[2])); else putpx(dx,dy,mix(bgc,rgb(pp[0],pp[1],pp[2]),pp[3])); } }
    } else if(g_page==3){
        if((mx!=g_lastmx||my!=g_lastmy)&&g_haspage&&!web_busy()&&mx>=cx&&mx<cx+cw&&my>=cy&&my<cy+ch){
            g_lastmx=mx; g_lastmy=my; static nwe_req mreq; nwe_req* r=&mreq; for(unsigned k=0;k<sizeof(*r);k++)((u8*)r)[k]=0; r->x=mx-cx; r->y=my-cy; web_call(NWE_OP_MOVE,r);
            int q=0; while(r->href[q]&&q<511){g_hoverhref[q]=r->href[q];q++;} g_hoverhref[q]=0; }
        else if(!(mx>=cx&&mx<cx+cw&&my>=cy&&my<cy+ch)) g_hoverhref[0]=0;
        if(g_haspage) web_paint(cx,cy,cw,ch); else fill_rect(cx,cy,cw,ch,rgb(255,255,255));
        if(g_haspage) popup_draw(cx,cy,cw,ch); else g_puw=0;
        perm_bar_draw(cx,cy,cw);
    } else if(g_page==0){
        fill_rect(cx,cy,cw,ch,rgb(255,255,255));
        int CW=cw, ccx=cx+cw/2;
        char bb[32],*bp=bb; bp=scopy(bp,"Bookmarks"); *bp=0; char hb2[32],*hp=hb2; hp=scopy(hp,"History"); *hp=0;
        { int rx=cx+cw-18; const char* tl[3]={"About",hb2,bb}; int tt[3]={1,21,20};
          for(int k=0;k<3;k++){ int w=pw_str((char*)tl[k],1); rx-=w; pdstr(rx,cy+16,(char*)tl[k],rgb(32,33,36),1); lk_tgt[lk_n]=tt[k]; lk_y[lk_n]=cy+12; lk_bx[lk_n]=rx; lk_bw[lk_n]=w; lk_bh[lk_n]=20; lk_n++; rx-=18; } }
        int ly=cy+(ch>460?ch/2-170:44);
        { const char* L="Noovex"; u32 lc[6]={rgb(66,133,244),rgb(234,67,53),rgb(251,188,5),rgb(66,133,244),rgb(52,168,83),rgb(234,67,53)}; int sz=4; int tw=0; char c1[2]={0,0};
          for(int k=0;k<6;k++){ c1[0]=L[k]; tw+=pw_str(c1,sz); } int lx=ccx-tw/2;
          for(int k=0;k<6;k++){ c1[0]=L[k]; pdstr(lx,ly,c1,lc[k],sz); pdstr(lx+1,ly,c1,lc[k],sz); lx+=pw_str(c1,sz); } }
        int pw2=560; if(pw2>CW-80)pw2=CW-80; int pxp=ccx-pw2/2, pyp=ly+92, ph=46; int hov=(mx>=pxp&&mx<pxp+pw2&&my>=pyp&&my<pyp+ph);
        if(hov)shadow_soft(pxp,pyp,pw2,ph,23);
        fill_round(pxp,pyp,pw2,ph,23,15,rgb(223,225,229)); fill_round(pxp+1,pyp+1,pw2-2,ph-2,22,15,rgb(255,255,255));
        { int gx=pxp+24, gy=pyp+ph/2; fill_circle(gx,gy-2,7,rgb(154,160,166)); fill_circle(gx,gy-2,5,rgb(255,255,255)); line_bl(gx+4,gy+3,gx+9,gy+8,rgb(154,160,166),256,2); }
        pdstr(pxp+46,pyp+15,"Search Noovex or type a URL",rgb(117,117,117),1);
        { int mx0=pxp+pw2-28, my0=pyp+ph/2; fill_round(mx0-4,my0-9,8,13,4,15,rgb(66,133,244)); fill_rect(mx0-1,my0+5,2,4,rgb(66,133,244)); }
        lk_tgt[lk_n]=30; lk_y[lk_n]=pyp; lk_bx[lk_n]=pxp; lk_bw[lk_n]=pw2; lk_bh[lk_n]=ph; lk_n++;
        { const char* bl[2]={"Noovex Search","I'm Feeling Lucky"}; int bt[2]={30,15}; int bw[2]; int gap=12, tot=0; for(int k=0;k<2;k++){ bw[k]=pw_str((char*)bl[k],1)+32; tot+=bw[k]; } tot+=gap; int bx=ccx-tot/2, by=pyp+ph+28;
          for(int k=0;k<2;k++){ int h2=(mx>=bx&&mx<bx+bw[k]&&my>=by&&my<by+36); fill_round(bx,by,bw[k],36,4,15,h2?rgb(232,234,237):rgb(248,249,250)); if(h2){ fill_rect(bx+4,by,bw[k]-8,1,rgb(218,220,224)); fill_rect(bx+4,by+35,bw[k]-8,1,rgb(218,220,224)); }
            pdstr_ctr(bx+bw[k]/2,by+11,(char*)bl[k],rgb(60,64,67),1); lk_tgt[lk_n]=bt[k]; lk_y[lk_n]=by; lk_bx[lk_n]=bx; lk_bw[lk_n]=bw[k]; lk_bh[lk_n]=36; lk_n++; bx+=bw[k]+gap; } }
        { const char* tlabel[4]={"Wikipedia","Example","Engine demo","noovexos"}; int ttgt[4]={13,10,14,11};
          u32 tcol[4]={rgb(95,99,104),rgb(52,168,83),rgb(66,133,244),rgb(234,67,53)}; char tini[4]={'W','E','J','M'};
          int nt=4, tw=112, gap=8, tot=nt*tw+(nt-1)*gap; if(tot>CW-40){ tw=(CW-40-(nt-1)*gap)/nt; tot=nt*tw+(nt-1)*gap; } int tx0=ccx-tot/2, ty=pyp+ph+96;
          if(ty+104<=cy+ch) for(int t=0;t<nt;t++){ int txx=tx0+t*(tw+gap); int h2=(mx>=txx&&mx<txx+tw&&my>=ty&&my<ty+104);
            if(h2) fill_round(txx,ty,tw,104,8,15,rgb(241,243,244));
            fill_circle(txx+tw/2,ty+36,24,rgb(241,243,244)); fill_circle(txx+tw/2,ty+36,14,tcol[t]); { char c1[2]={tini[t],0}; pdstr_ctr(txx+tw/2,ty+28,c1,rgb(255,255,255),1); }
            pdstr_ctr(txx+tw/2,ty+74,(char*)tlabel[t],rgb(32,33,36),1);
            lk_tgt[lk_n]=ttgt[t]; lk_y[lk_n]=ty; lk_bx[lk_n]=txx; lk_bw[lk_n]=tw; lk_bh[lk_n]=104; lk_n++; } }
        { int fy=cy+ch-40; if(fy>pyp+ph+210){ fill_rect(cx,fy,cw,40,rgb(242,242,242)); fill_rect(cx,fy,cw,1,rgb(218,220,224));
            if(g_nvx_signedin){ char ac[100],*ap=ac; ap=scopy(ap,"Signed in as ");ap=scopy(ap,g_nvx_user[0]?g_nvx_user:g_nvx_name); *ap=0; pdstr(cx+24,fy+13,ac,rgb(112,117,122),1); }
            else pdstr(cx+24,fy+13,"NoovexOS",rgb(112,117,122),1);
            pdstr(cx+cw-24-pw_str("Private  -  Fast  -  Built from scratch",1),fy+13,"Private  -  Fast  -  Built from scratch",rgb(112,117,122),1); } }
    } else if(g_page==1){
        fill_round(cx,cy,cw,ch,8,15,rgb(255,255,255));
        int ax=cx+18, ay=cy+16;
        dstr(ax,ay,"About",INK,3);
        pdstr(ax,ay+44,"NoovexOS 64 Browser with the Noovex Web Engine",INK,1);
        pdstr(ax,ay+66,"JavaScript: QuickJS (ES2023) with DOM, events, timers, fetch, XMLHttpRequest.",rgb(70,80,100),1);
        pdstr(ax,ay+86,"Layout: block, inline, floats, positioning, z-index, flexbox, grid, tables.",rgb(70,80,100),1);
        pdstr(ax,ay+106,"CSS: selectors, cascade, variables, calc(), media queries, gradients, shadows.",rgb(70,80,100),1);
        pdstr(ax,ay+126,"Cookies with expiry, Secure and HttpOnly; localStorage kept across reboots.",rgb(70,80,100),1);
        pdstr(ax,ay+146,"Images: PNG, JPEG, GIF, WebP, BMP, ICO and SVG. TLS 1.3 for HTTPS.",rgb(70,80,100),1);
        pdstr(ax,ay+170,"about:js  about:cookies  about:errors  about:pending  about:perf",rgb(150,110,90),1);
        lk_y[lk_n]=ay+206; lk_tgt[lk_n]=0; lk_bx[lk_n]=ax; lk_bw[lk_n]=cw-36; lk_bh[lk_n]=16; pdstr(ax,ay+206,"< Back home",ACC,1); lk_n++;
    }
    int sby=WN[i].y+WN[i].h-20; if(CL){ fill_rect(WN[i].x+2,sby,WN[i].w-4,18,rgb(212,208,200)); cl_edge(WN[i].x+4,sby+1,WN[i].w-8,17,1); } else { fill_rect(WN[i].x+1,sby,WN[i].w-2,19,rgb(244,245,248)); fill_rect(WN[i].x+1,sby,WN[i].w-2,1,rgb(228,230,236)); }
    { const char* st=(g_page==3&&g_hoverhref[0])?g_hoverhref:web_stat; char hb3[160]; int hn=0; for(int q=0;st[q]&&hn<150;q++)hb3[hn++]=st[q]; hb3[hn]=0; pdstr(WN[i].x+10,sby+2,hb3,CL?rgb(0,0,0):rgb(80,90,110),1); }
    if(g_findmode){ int fw=292, fx=x+w-fw-8, fy=cy+6; fill_round(fx,fy,fw,28,6,15,rgb(252,252,254));
        for(int t=0;t<28;t++){ SCENE[(u64)(fy+t)*SW+fx]=rgb(210,214,222); SCENE[(u64)(fy+t)*SW+fx+fw-1]=rgb(210,214,222); }
        pdstr(fx+8,fy+7,"Find:",rgb(120,128,146),1); int tx0=fx+50; pdstr(tx0,fy+7,g_findstr,INK,1);
        if((g_ticks/50)&1)fill_rect(tx0+pw_str(g_findstr,1)+1,fy+8,2,12,ACC);
        char cb[24],*p=cb; if(g_findn>0){ p=udec(p,(u32)g_findn); p=scopy(p," found"); } else if(g_findn==0) p=scopy(p,"none"); *p=0;
        pdstr(fx+fw-70,fy+7,cb,g_findn>0?rgb(90,120,90):rgb(160,110,110),1); }
    if(g_urlfocus) sug_build(); else if(g_nsug) sug_clear();
    sug_draw();
    site_menu_draw();
}
