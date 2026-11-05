static void show_errors(void){
    extern int web_err_count(void); extern int web_err_get(int,char*,int,unsigned long*);
    hb b={0,0,0}; hb_put(&b,"<!doctype html><html><head><title>Errors</title>"); hb_put(&b,PAGE_CSS); hb_put(&b,"<style>.e{font-family:monospace;font-size:13px;padding:6px 0;border-top:1px solid #ddd;word-break:break-all}.e b{color:#b00}.p{font-family:monospace;font-size:13px;padding:8px 0 4px;color:#035;font-weight:bold;border-top:2px solid #035}</style></head><body><main><h1>Errors</h1><div class=card>");
    hb_put(&b,"<p class=muted>JavaScript errors, unhandled promise rejections, console.error messages and failed requests, newest last. A line starting with Page marks a new page.</p>");
    int n=web_err_count(); if(!n)hb_put(&b,"<p>No errors so far.</p>");
    static char line[1210]; for(int i=0;i<n;i++){ unsigned long t=0; web_err_get(i,line,sizeof line,&t);
        if(line[0]=='P'&&line[1]=='a'&&line[2]=='g'&&line[3]=='e'&&line[4]==' '){ hb_put(&b,"<div class=p>"); hb_esc(&b,line); hb_put(&b,"</div>"); continue; }
        char ts[24],*q=ts; q=udec(q,(u32)(t/1000)); *q++='.'; q=udec(q,(u32)((t/100)%10)); q=scopy(q," s "); *q=0;
        hb_put(&b,"<div class=e><b>"); hb_esc(&b,ts); hb_put(&b,"</b>"); hb_esc(&b,line); hb_put(&b,"</div>"); }
    hb_put(&b,"</div></main></body></html>");
    show_page("about:errors",&b,"Errors");
}
static void show_pending(void){
    extern const char* web_pending_get(unsigned long*);
    unsigned long t=0; const char* p=web_pending_get(&t);
    hb b={0,0,0}; hb_put(&b,"<!doctype html><html><head><title>Pending</title>"); hb_put(&b,PAGE_CSS); hb_put(&b,"<style>.l{font-family:monospace;font-size:13px;padding:3px 0 3px 12px;word-break:break-all}.h{font-family:monospace;font-size:13px;font-weight:bold;color:#035;padding:10px 0 2px;border-top:1px solid #ccd}.u{font-family:monospace;font-size:13px;font-weight:bold;padding:4px 0}.s{font-family:monospace;font-size:13px;padding:2px 0 6px}</style></head><body><main><h1>Pending</h1><div class=card>");
    hb_put(&b,"<p class=muted>What the last page was still waiting for, taken once a second while it was open. Open the page, wait until it gets stuck, then open about:pending.</p>");
    if(!p){ hb_put(&b,"<p>No page has reported yet.</p>"); }
    else {
        char ts[40],*q=ts; q=scopy(q,"Taken "); q=udec(q,(u32)((nwk_ms()-t)/1000)); q=scopy(q," s ago"); *q=0;
        hb_put(&b,"<p class=muted>"); hb_esc(&b,ts); hb_put(&b,"</p>");
        static char line[1024];
        while(*p){ int n=0; while(p[n]&&p[n]!='\n'&&n<1023){ line[n]=p[n]; n++; } line[n]=0; while(p[n]&&p[n]!='\n')n++;
            const char* cls=line[0]=='#'?"h":line[0]=='@'?"u":line[0]=='='?"s":"l"; const char* tx=(line[0]=='#'||line[0]=='@'||line[0]=='=')&&line[1]==' '?line+2:line;
            hb_put(&b,"<div class="); hb_put(&b,cls); hb_put(&b,">"); hb_esc(&b,tx); hb_put(&b,"</div>");
            p+=n; if(*p=='\n')p++; }
    }
    hb_put(&b,"</div></main></body></html>");
    show_page("about:pending",&b,"Pending");
}
static void show_console(void){
    hb b={0,0,0}; hb_put(&b,"<!doctype html><html><head><title>Console</title>"); hb_put(&b,PAGE_CSS); hb_put(&b,"</head><body><main><h1>JavaScript console</h1><div class=card style='font-family:monospace;font-size:13px'>");
    int n=web_console_count(); if(!n)hb_put(&b,"<p class=muted>No messages.</p>");
    char line[220]; for(int i=0;i<n;i++){ web_console_get(i,line,sizeof line); hb_put(&b,"<div class=row>"); hb_esc(&b,line); hb_put(&b,"</div>"); }
    hb_put(&b,"</div></main></body></html>");
    show_page("about:console",&b,"Console");
}
static const char JS_DEMO[]="<!doctype html><html><head><title>Engine demo</title><style>body{font-family:Arial,sans-serif;margin:0;background:linear-gradient(135deg,#1e3a8a,#7c3aed);color:#fff;min-height:100vh}"
    "main{max-width:760px;margin:0 auto;padding:40px 20px}h1{font-size:40px;margin:0 0 8px}.grid{display:grid;grid-template-columns:repeat(auto-fill,minmax(160px,1fr));gap:12px;margin:20px 0}"
    ".t{background:rgba(255,255,255,.14);border-radius:14px;padding:16px;text-align:center}.t b{display:block;font-size:30px}button{background:#fff;color:#4c1d95;border:0;border-radius:10px;padding:10px 18px;font-size:16px;font-weight:bold}"
    "input{padding:9px;border-radius:8px;border:0;width:260px;font-size:15px}#log{margin-top:16px;background:rgba(0,0,0,.25);border-radius:10px;padding:12px;font-family:monospace;white-space:pre-wrap}</style></head>"
    "<body><main><h1>Noovex Web Engine</h1><p>QuickJS JavaScript, real CSS layout, flexbox, grid and cookies.</p><div class=grid><div class=t><b id=c>0</b>clicks</div><div class=t><b id=t>0</b>seconds</div><div class=t><b id=v>0</b>visits</div><div class=t><b id=f>0</b>fib(25)</div></div>"
    "<p><button id=b>Click me</button> <input id=i placeholder='Type something'></p><div id=log></div></main>"
    "<script>let n=0;const $=s=>document.querySelector(s);$('#b').addEventListener('click',()=>{$('#c').textContent=++n;});"
    "$('#i').addEventListener('input',e=>{$('#log').textContent='You typed: '+e.target.value;});"
    "let s=0;setInterval(()=>{$('#t').textContent=++s;},1000);const v=(+localStorage.getItem('visits')||0)+1;localStorage.setItem('visits',v);$('#v').textContent=v;document.cookie='demo_visits='+v+'; max-age=31536000';"
    "const fib=k=>k<2?k:fib(k-1)+fib(k-2);const t0=performance.now();const r=fib(25);$('#f').textContent=r;"
    "$('#log').textContent='fib(25) took '+(performance.now()-t0).toFixed(0)+' ms. Cookies: '+document.cookie+'\\nclass syntax: '+(class A{static x=()=>'ok'}).x()+', async: '+(async()=>1)().constructor.name;"
    "</script></body></html>";

static void web_process_nav(nwe_req* r);
static void web_info(nwe_req* r){ g_doch=r->doch; g_scrolly=r->scroll; g_viewh=r->viewh; if(r->title[0]){ int t=0; while(r->title[t]&&t<127){g_title[t]=r->title[t];t++;} g_title[t]=0; } }
static int g_bmbar_on(void);
static void content_rect(void){
    int i=W_BROWSER; int x=WN[i].x+8, w=WN[i].w-16;
    int tsy=WN[i].y+TITLEH+6, tsh=26; int y=tsy+tsh+6; int cy=y+36+(g_bmbar_on()?24:0);
    g_cx=x; g_cy=cy; g_cw=w; g_ch=WN[i].y+WN[i].h-cy-22; if(g_ch<40)g_ch=40; if(g_cw<80)g_cw=80;
}
static void perf_capture(const char* url){
    extern nwk_netstat g_netstat; nwk_netstat d=g_netstat; unsigned long long* a=(unsigned long long*)&d; const unsigned long long* b=(const unsigned long long*)&g_go_ns0; for(unsigned i=0;i<sizeof(d)/8;i++)a[i]-=b[i];
    g_load_ns=g_netstat; g_after_have=0; g_perf_ns=d; g_perf_slow=g_nwk_slow; { static nwe_req pr; for(unsigned k=0;k<sizeof(pr);k++)((u8*)&pr)[k]=0; web_call(NWE_OP_PERF,&pr); const u8* src=(const u8*)pr.out; u8* dst=(u8*)&g_perf_last; for(unsigned k=0;k<sizeof(g_perf_last);k++)dst[k]=src[k]; } g_perf_total=nwk_us()-g_go_t0; g_perf_have=1; int k=0; for(;url[k]&&k<255;k++)g_perf_url[k]=url[k]; g_perf_url[k]=0;
    g_load_end=nwk_us();
    char m[200],*q=m; q=scopy(q,"Done in "); ms1(&q,g_perf_total); q=scopy(q,": network "); ms1(&q,d.fetch_us); q=scopy(q," ("); q=udec(q,(u32)d.requests); q=scopy(q," requests, "); q=udec(q,(u32)d.conns); q=scopy(q," new connections), scripts "); ms1(&q,g_perf_last.js_us); q=scopy(q,", layout "); ms1(&q,g_perf_last.style_us+g_perf_last.layout_us); q=scopy(q,"  - about:perf"); { extern int web_err_total(void); int ne=web_err_total()-g_go_err0; if(ne>0){ q=scopy(q,"  - "); q=udec(q,(u32)ne); q=scopy(q,ne==1?" error, see about:errors":" errors, see about:errors"); } } *q=0; sset(web_stat,m);
}
static void web_load_html(const char* url,const char* html,int len,int istext){
    if(!web_init()){ nwk_status("Web engine could not start (out of memory)"); return; }
    content_rect();
    if(WN[W_BROWSER].open)compose_present();
    nwe_req* r=&g_req; for(unsigned k=0;k<sizeof(*r);k++)((u8*)r)[k]=0;
    r->url=url; r->data=html; r->len=len; r->w=g_cw; r->h=g_ch; r->btn=istext?1:0;
    g_lastvw=g_cw; g_lastvh=g_ch;
    { extern int g_web_faults; if(g_web_faults>32)g_web_faults=32; }
    { extern int g_web_pump_ok; int sv=g_web_pump_ok; g_web_pump_ok=1; long lr=web_call(NWE_OP_LOAD,r); g_web_pump_ok=sv; if(lr>=0&&!(url&&url[0]=='a'&&url[6]=='c'&&url[11]=='e'))g_crash_shown=0; }
    g_haspage=1; g_page=3; g_isimg=0; g_findn=-1;
    g_title[0]=0; web_info(r);
    if(g_go_on&&url&&!(url[0]=='a'&&url[1]=='b'&&url[2]=='o')){ g_go_on=0; perf_capture(url); }
    web_process_nav(r);
}

static void web_preview(const char* html,int len,const char* url){
    if(!web_init()||!WN[W_BROWSER].open)return;
    content_rect();
    nwe_req* r=&g_req; for(unsigned k=0;k<sizeof(*r);k++)((u8*)r)[k]=0;
    r->url=url; r->data=html; r->len=len; r->w=g_cw; r->h=g_ch; r->btn=2;
    g_lastvw=g_cw; g_lastvh=g_ch;
    if(web_call(NWE_OP_LOAD,r)<0)return;
    g_haspage=1; g_page=3; g_isimg=0; g_title[0]=0; web_info(r);
    { char b[64],*p=b; p=scopy(p,"Loading... "); p=udec(p,(u32)(len/1024)); p=scopy(p," KB received"); *p=0; nwk_status(b); }
    compose_present();
}
static void browser_error(const char*msg,int code){
    hb b={0,0,0}; hb_put(&b,"<!doctype html><html><head><title>Can't reach this page</title>"); hb_put(&b,PAGE_CSS);
    hb_put(&b,"</head><body><main><h1>Can't reach this page</h1><div class=card><p>"); hb_esc(&b,msg); hb_put(&b,"</p><p class=muted>Address: "); hb_esc(&b,g_url);
    if(code){ char t[16],*q=t; *q++=' '; *q++='('; q=udec(q,(u32)code); *q++=')'; *q=0; hb_put(&b,t); }
    hb_put(&b,"</p><p class=muted>Check the address and the network connection. The <code>netlog</code> command in Terminal shows details.</p><p><a class=btn href=\"");
    hb_esc(&b,g_url); hb_put(&b,"\">Try again</a></p></div></main></body></html>");
    show_page("about:error",&b,"Can't reach this page");
}
void browser_show_html(const char* html,int len,const char* title){
    g_urlfocus=0; g_page=3; g_isimg=0;
    web_load_html("about:local",html,len,0);
    if(title){ int t=0; while(title[t]&&t<127){g_title[t]=title[t];t++;} g_title[t]=0; }
    sset(web_stat,(char*)(title?title:"local"));
    open_win(W_BROWSER);
}
static int ieq_n(const char* a,const char* b,int n){ for(int i=0;i<n;i++){ char x=a[i],y=b[i]; if(x>='A'&&x<='Z')x+=32; if(y>='A'&&y<='Z')y+=32; if(x!=y)return 0; } return 1; }
static void dl_name_from_url(const char*url,char*out){
    int end=0; for(int i=0;url[i];i++){ if(url[i]=='?'||url[i]=='#'){ end=i; break; } end=i+1; }
    int slash=-1; for(int i=0;i<end;i++) if(url[i]=='/') slash=i;
    int st=slash+1, n=0; for(int i=st;i<end&&n<40;i++){ char c=url[i]; if(c==':'||c=='\\'||c=='"')continue; out[n++]=c; }
    out[n]=0; if(n==0)sset(out,"download");
}
static int has_ext(const char*url,const char*ext){
    int end=0; for(int i=0;url[i];i++){ if(url[i]=='?'||url[i]=='#'){ end=i; break; } end=i+1; }
    int dot=-1; for(int i=0;i<end;i++) if(url[i]=='.') dot=i; if(dot<0)return 0;
    int j=dot+1; for(int k=0;ext[k];k++,j++){ if(j>=end)return 0; if(!ci_eq(url[j],ext[k]))return 0; }
    return j==end;
}
static int is_dl_ext(const char*url){
    static const char*E[]={"zip","gz","tgz","tar","rar","7z","pdf","bin","iso","img","exe","msi","dmg","deb","rpm","apk",
        "mp3","m4a","wav","flac","ogg","mp4","mkv","mov","avi","doc","docx","xls","xlsx","ppt","pptx","csv","epub","xz","zst","bz2",0};
    for(int i=0;E[i];i++) if(has_ext(url,E[i]))return 1; return 0;
}
static void save_download(const char* url,const u8* data,int len,const char* cdname){
    char dn[48];
    if(cdname&&cdname[0]){ int i=0; for(;cdname[i]&&i<47;i++)dn[i]=cdname[i]; dn[i]=0; } else dl_name_from_url(url,dn);
    int hasdot=0; for(int i=0;dn[i];i++) if(dn[i]=='.')hasdot=1;
    if(!hasdot&&len>=4){ const char*ext=0;
        if(data[0]==0x89&&data[1]==0x50)ext=".png"; else if(data[0]==0xFF&&data[1]==0xD8)ext=".jpg"; else if(data[0]==0x50&&data[1]==0x4B)ext=".zip"; else if(data[0]=='G'&&data[1]=='I')ext=".gif"; else if(data[0]=='%'&&data[1]=='P')ext=".pdf";
        if(ext){ int L=slen(dn); for(int j=0;ext[j]&&L<47;j++)dn[L++]=ext[j]; dn[L]=0; } }
    int slot=fs_save_bin(dn,data,len,1);
    if(slot>=0){ extern char g_home[]; extern int g_home_dir; char dp[112]; char* q=dp; q=scopy(q,g_home); q=scopy(q,"/Downloads"); *q=0; int dd=dt_resolve_dir(dp); if(dd>=0&&FS[slot].dir==g_home_dir){ FS[slot].dir=dd; extern void nvx3_flush_meta(void); nvx3_flush_meta(); } }
    if(slot>=0){ char b[120],*q=b; q=scopy(q,"Downloaded "); q=scopy(q,dn); q=scopy(q," ("); q=udec(q,(u32)len); q=scopy(q," bytes) to Downloads"); *q=0; sset(web_stat,b); toast("Download complete"); }
    else sset(web_stat,"Download failed (disk full)");
}
#define DL_N 8
#define DL_OWNER 0x444C4E58L
typedef struct { int id, status, len, cap, total; u8* buf; char url[512]; char name[48]; char cd[48]; } dlent;
static dlent g_dl[DL_N];
int dl_active(void){ int n=0; for(int i=0;i<DL_N;i++) if(g_dl[i].id)n++; return n; }
int dl_progress(char* out,int cap){
    int n=0; u64 got=0,tot=0; const char* nm=0;
    for(int i=0;i<DL_N;i++) if(g_dl[i].id){ n++; got+=(u64)g_dl[i].len; tot+=(u64)(g_dl[i].total>0?g_dl[i].total:0); if(!nm)nm=g_dl[i].name; }
    if(!n){ out[0]=0; return -1; }
    char* p=out; char* e=out+cap-24; int k=0; while(nm[k]&&k<22&&p<e)*p++=nm[k++]; if(n>1){ p=scopy(p," +"); p=udec(p,(u32)(n-1)); }
    *p=0;
    return tot>0?(int)(got*100/tot):-2;
}
static void dl_free(dlent* d){ if(d->buf)kfree(d->buf); for(unsigned k=0;k<sizeof(*d);k++)((u8*)d)[k]=0; }
void dl_tick(void){
    nwk_netev ev;
    while(nwk_net_event(DL_OWNER,&ev)){
        dlent* d=0; for(int i=0;i<DL_N;i++) if(g_dl[i].id==ev.id){ d=&g_dl[i]; break; }
        if(!d) continue;
        if(ev.type==1){ d->status=ev.status;
            for(int i=0;ev.hdrs&&i<ev.hlen;i++){ if(i&&ev.hdrs[i-1]!='\n')continue;
                if(i+15<ev.hlen&&ieq_n(ev.hdrs+i,"content-length:",15)){ int j=i+15; long v=0; while(j<ev.hlen&&ev.hdrs[j]==' ')j++; while(j<ev.hlen&&ev.hdrs[j]>='0'&&ev.hdrs[j]<='9'){ v=v*10+(ev.hdrs[j]-'0'); j++; } d->total=v>0x7FFFFFFF?0x7FFFFFFF:(int)v; }
                if(i+20<ev.hlen&&ieq_n(ev.hdrs+i,"content-disposition:",20)){ int j=i+20; while(j<ev.hlen&&ev.hdrs[j]!='\n'){ if(j+9<ev.hlen&&ieq_n(ev.hdrs+j,"filename=",9)){ j+=9; if(ev.hdrs[j]=='"')j++; int k=0; while(j<ev.hlen&&k<47&&ev.hdrs[j]!='"'&&ev.hdrs[j]!=';'&&ev.hdrs[j]!='\r'&&ev.hdrs[j]!='\n'&&ev.hdrs[j]!='/'&&ev.hdrs[j]!='\\')d->cd[k++]=ev.hdrs[j++]; d->cd[k]=0; break; } j++; } } } }
        else if(ev.type==2){ if(d->len+ev.dlen+1>d->cap){ int nc=d->cap?d->cap:262144; while(nc<d->len+ev.dlen+1)nc*=2; u8* nb=kmalloc((unsigned long)nc); if(!nb){ toast("Download failed: out of memory"); nwk_net_abort(DL_OWNER,d->id); dl_free(d); continue; } for(int k=0;k<d->len;k++)nb[k]=d->buf[k]; if(d->buf)kfree(d->buf); d->buf=nb; d->cap=nc; }
            for(int k=0;k<ev.dlen;k++)d->buf[d->len+k]=ev.data[k]; d->len+=ev.dlen; }
        else { if(ev.ok&&d->status>0&&d->status<400){ save_download(d->url,d->buf?d->buf:(const u8*)"",d->len,d->cd[0]?d->cd:0); }
            else { char b[96],*q=b; q=scopy(q,"Download failed: "); q=scopy(q,d->name); *q=0; toast(b); sset(web_stat,b); }
            dl_free(d); } }
}
static void browser_download(const char*url){
    dlent* d=0; for(int i=0;i<DL_N;i++) if(!g_dl[i].id){ d=&g_dl[i]; break; }
    int id=d?nwk_net_start(DL_OWNER,url,"GET",0,0,0,"",g_cururl):0;
    if(id<=0){
        nwk_resp r; char b[160],*q=b; q=scopy(q,"Downloading "); q=scopy(q,(char*)url); *q=0; nwk_status(b); compose_present();
        if(nwk_fetch(url,"GET",0,0,0,g_cururl,&r)<0||!r.body){ sset(web_stat,"Download failed - run netlog for detail"); return; }
        save_download(url,(const u8*)r.body,r.len,0); nwk_free(r.body); return; }
    for(unsigned k=0;k<sizeof(*d);k++)((u8*)d)[k]=0;
    d->id=id; int k=0; for(;url[k]&&k<511;k++)d->url[k]=url[k]; d->url[k]=0; dl_name_from_url(url,d->name);
    char b[120],*q=b; q=scopy(q,"Downloading "); q=scopy(q,d->name); q=scopy(q," in the background"); *q=0; sset(web_stat,b); toast(b);
}
static int looks_url(const char*s){
    if(!s[0])return 1;
    if((s[0]|32)=='h'&&(s[1]|32)=='t'&&(s[2]|32)=='t'&&(s[3]|32)=='p')return 1;
    if((s[0]|32)=='a'&&(s[1]|32)=='b'&&(s[2]|32)=='o'&&(s[3]|32)=='u'&&(s[4]|32)=='t'&&s[5]==':')return 1;
    int hasdot=0,hasspace=0; for(int i=0;s[i];i++){ if(s[i]=='.')hasdot=1; if(s[i]==' ')hasspace=1; }
    if(hasspace)return 0;
    if(!hasdot){ if(seq(s,"localhost"))return 1; for(int i=0;s[i];i++) if(s[i]==':'||s[i]=='/') return 1; return 0; }
    return 1;
}
static void urlencode(const char*s,char*out,int cap){ int o=0; const char* hx="0123456789ABCDEF";
    for(int i=0;s[i]&&o<cap-4;i++){ unsigned char ch=(unsigned char)s[i];
        if((ch>='a'&&ch<='z')||(ch>='A'&&ch<='Z')||(ch>='0'&&ch<='9')||ch=='-'||ch=='_'||ch=='.'||ch=='~')out[o++]=(char)ch;
        else if(ch==' ')out[o++]='+';
        else { out[o++]='%'; out[o++]=hx[ch>>4]; out[o++]=hx[ch&15]; } }
    out[o]=0;
}
static int istarts_(const char* s,const char* p){ while(*p){ if(!ci_eq(*s,*p))return 0; s++; p++; } return 1; }
static int stristr(const char*h,const char*needle){ for(int i=0;h[i];i++){ int k=0; while(needle[k]&&ci_eq(h[i+k],needle[k]))k++; if(!needle[k])return 1; } return 0; }
static void host_of(const char* u,char* out){ const char* p=u; while(*p&&*p!=':')p++; if(*p==':')p++; while(*p=='/')p++; int n=0; while(*p&&*p!='/'&&*p!='?'&&*p!='#'&&*p!=':'&&n<255)out[n++]=*p++; out[n]=0; }


static const char YT_JS[]=
"<div class=card><p id=s>Loading the video from YouTube...</p><video id=v controls autoplay style=\"width:100%;max-width:960px;background:#000\"></video><h2 id=t></h2><p class=muted id=a></p><p class=muted id=q></p></div>"
"<script>(async function(){var id=document.body.getAttribute('data-v');var S=document.getElementById('s');var last='no playable format';"
"var C=[{clientName:'ANDROID_VR',clientVersion:'1.60.19',deviceMake:'Oculus',deviceModel:'Quest 3',androidSdkVersion:32,osName:'Android',osVersion:'12L',hl:'en',gl:'US'},"
"{clientName:'IOS',clientVersion:'20.10.4',deviceMake:'Apple',deviceModel:'iPhone16,2',osName:'iPhone',osVersion:'18.3.2.22D82',hl:'en',gl:'US'},"
"{clientName:'TVHTML5_SIMPLY_EMBEDDED_PLAYER',clientVersion:'2.0',hl:'en',gl:'US'},{clientName:'WEB_EMBEDDED_PLAYER',clientVersion:'1.20250310.01.00',hl:'en',gl:'US'}];"
"for(var i=0;i<C.length;i++){try{S.textContent='Asking YouTube for the video ('+C[i].clientName+')...';"
"var r=await fetch('https://www.youtube.com/youtubei/v1/player?prettyPrint=false',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify({videoId:id,context:{client:C[i]},contentCheckOk:true,racyCheckOk:true})});"
"var j=await r.json();var ps=j.playabilityStatus||{};var sd=j.streamingData;if(!sd){last=ps.reason||ps.status||'no streams';continue;}"
"var f=(sd.formats||[]).filter(function(x){return x.url&&/mp4|webm/.test(x.mimeType||'')&&!/av01/.test(x.mimeType||'');});"
"f.sort(function(a,b){return (b.height||0)-(a.height||0);});var ok=f.filter(function(x){return (x.height||0)<=720;});var pick=ok[0]||f[0];"
"if(!pick){last='only adaptive or protected streams';continue;}"
"var d=j.videoDetails||{};document.getElementById('t').textContent=d.title||'';document.getElementById('a').textContent=(d.author||'')+(d.viewCount?' - '+Number(d.viewCount).toLocaleString()+' views':'');"
"document.title=(d.title||'YouTube')+' - YouTube';document.getElementById('q').textContent=(pick.qualityLabel||'')+' '+(pick.mimeType||'').split(';')[0]+' via '+C[i].clientName;"
"var v=document.getElementById('v');v.src=pick.url;v.play&&v.play();S.textContent='';return;}catch(e){last=String(e&&e.message||e);}}"
"S.textContent='This video could not be played here: '+last+'. YouTube may require sign-in or a newer client for it.';})();</script>";
static const char YT_CSS[]="<style>body{font-family:Arial,sans-serif;margin:0;background:#fff;color:#0f0f0f}.yh{display:flex;align-items:center;gap:16px;padding:10px 20px;border-bottom:1px solid #e5e5e5;background:#fff}.yl{display:flex;align-items:center;gap:6px;font-size:20px;font-weight:bold;color:#0f0f0f;text-decoration:none}.yp{display:inline-block;width:30px;height:21px;background:#f00;border-radius:6px;color:#fff;font-size:12px;line-height:21px;text-align:center}.yf{display:flex;flex:1;max-width:600px}.yf input{flex:1;padding:8px 14px;border:1px solid #ccc;border-radius:20px 0 0 20px;font-size:15px}.yf button{padding:8px 18px;border:1px solid #ccc;border-left:0;border-radius:0 20px 20px 0;background:#f8f8f8;font-size:14px}.ym{padding:16px 20px}.ys{color:#606060;margin:4px 0 14px}.yg{display:flex;flex-wrap:wrap;gap:18px}.vid{display:block;width:320px;color:#0f0f0f;text-decoration:none}.th{position:relative;width:320px;height:180px;background:#ddd;border-radius:10px;overflow:hidden}.th img{display:block;width:320px;height:180px}.len{position:absolute;right:6px;bottom:6px;background:rgba(0,0,0,.8);color:#fff;font-size:12px;padding:1px 4px;border-radius:4px}.tt{font-weight:bold;font-size:14px;margin:8px 0 4px;line-height:19px;max-height:38px;overflow:hidden}.mu{color:#606060;font-size:13px}.card{max-width:980px}.muted{color:#606060}</style>\n";
static const char YT_HDR[]="<div class=yh><a class=yl href=\"https://www.youtube.com/\"><span class=yp>&#9654;</span>YouTube</a><form class=yf id=yf action=\"https://www.youtube.com/results\" method=get><input id=yq name=search_query placeholder=\"Search\"><button>Search</button></form></div><script>(function(){var f=document.getElementById('yf'),i=document.getElementById('yq');var q=document.body.getAttribute('data-q');if(q)i.value=q;f.addEventListener('submit',function(e){e.preventDefault();var v=i.value.trim();if(v)location.href='https://www.youtube.com/results?search_query='+encodeURIComponent(v);});})();</script>\n";
static const char YT_HOME_JS[]="<div class=ym><p class=ys id=s>Loading...</p><div class=yg id=g></div></div><script>(async function(){\n"
"var q=document.body.getAttribute('data-q')||'';\n"
"var S=document.getElementById('s'),G=document.getElementById('g');\n"
"var W={clientName:'WEB',clientVersion:'2.20250312.04.00',hl:'en',gl:'US'};\n"
"function esc(s){return String(s==null?'':s).replace(/&/g,'&amp;').replace(/</g,'&lt;').replace(/>/g,'&gt;').replace(/\"/g,'&quot;');}\n"
"function txt(o){if(!o)return '';if(typeof o=='string')return o;if(o.simpleText)return o.simpleText;if(o.content)return o.content;if(o.runs)return o.runs.map(function(r){return r.text;}).join('');return '';}\n"
"function collect(o,out,seen,d){if(!o||typeof o!='object'||d>60)return;if(Array.isArray(o)){for(var i=0;i<o.length;i++)collect(o[i],out,seen,d+1);return;}\n"
"var v=o.videoRenderer||o.gridVideoRenderer||o.compactVideoRenderer;\n"
"if(v&&v.videoId){if(!seen[v.videoId]){seen[v.videoId]=1;out.push({id:v.videoId,t:txt(v.title),c:txt(v.ownerText||v.longBylineText||v.shortBylineText),v:txt(v.shortViewCountText||v.viewCountText),l:txt(v.lengthText),p:txt(v.publishedTimeText)});}return;}\n"
"var L=o.lockupViewModel;if(L&&L.contentId&&/VIDEO/.test(L.contentType||'')){if(!seen[L.contentId]){seen[L.contentId]=1;var m=(L.metadata&&L.metadata.lockupMetadataViewModel)||{};var rows=[];try{(m.metadata.contentMetadataViewModel.metadataRows||[]).forEach(function(r){(r.metadataParts||[]).forEach(function(p){rows.push(txt(p.text));});});}catch(e){}\n"
"out.push({id:L.contentId,t:txt(m.title),c:rows[0]||'',v:rows.slice(1).join(' - '),l:'',p:''});}return;}\n"
"var R=o.reelItemRenderer||o.shortsLockupViewModel;if(R){var rid=R.videoId||(R.onTap&&R.onTap.innertubeCommand&&R.onTap.innertubeCommand.reelWatchEndpoint&&R.onTap.innertubeCommand.reelWatchEndpoint.videoId);if(rid&&!seen[rid]){seen[rid]=1;out.push({id:rid,t:txt(R.headline||(R.overlayMetadata&&R.overlayMetadata.primaryText)),c:'Shorts',v:txt(R.viewCountText||(R.overlayMetadata&&R.overlayMetadata.secondaryText)),l:'',p:''});}return;}\n"
"for(var k in o)collect(o[k],out,seen,d+1);}\n"
"async function api(ep,body){body.context={client:W};var r=await fetch('https://www.youtube.com/youtubei/v1/'+ep+'?prettyPrint=false',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify(body)});return await r.json();}\n"
"var list=[],seen={},err='';\n"
"try{\n"
"if(q){S.textContent='Searching for \"'+q+'\"...';collect(await api('search',{query:q}),list,seen,0);}\n"
"else{S.textContent='Loading videos...';try{collect(await api('browse',{browseId:'FEwhat_to_watch'}),list,seen,0);}catch(e){err=String(e&&e.message||e);}\n"
"if(list.length<8){try{collect(await api('browse',{browseId:'FEtrending'}),list,seen,0);}catch(e){}}\n"
"if(list.length<8){collect(await api('search',{query:'popular videos today'}),list,seen,0);}}\n"
"}catch(e){err=String(e&&e.message||e);}\n"
"if(!list.length){S.textContent='No videos could be loaded'+(err?': '+err:'')+'.';return;}\n"
"S.textContent=q?list.length+' results for \"'+q+'\"':'Recommended';\n"
"var h='';for(var i=0;i<list.length&&i<60;i++){var x=list[i];var meta=[x.v,x.p].filter(function(s){return s;}).join(' - ');\n"
"h+='<a class=vid href=\"https://www.youtube.com/watch?v='+esc(x.id)+'\"><div class=th><img src=\"https://i.ytimg.com/vi/'+esc(x.id)+'/mqdefault.jpg\" width=320 height=180 alt=\"\">'+(x.l?'<span class=len>'+esc(x.l)+'</span>':'')+'</div><div class=tt>'+esc(x.t)+'</div><div class=mu>'+esc(x.c)+'</div><div class=mu>'+esc(meta)+'</div></a>';}\n"
"G.innerHTML=h;\n"
"})();</script>\n";
static int yt_idch(char c){ return (c>='a'&&c<='z')||(c>='A'&&c<='Z')||(c>='0'&&c<='9')||c=='-'||c=='_'; }
__attribute__((unused)) static int yt_try(const char* full){
    const char* h=full; if(istarts_(h,"https://"))h+=8; else if(istarts_(h,"http://"))h+=7;
    const char* hs[6]={"www.youtube.com/","m.youtube.com/","youtube.com/","music.youtube.com/","youtu.be/","www.youtube-nocookie.com/"};
    int which=-1; for(int i=0;i<6;i++) if(istarts_(h,hs[i])){ which=i; h+=slen(hs[i]); break; } if(which<0)return 0;
    char id[16]; int n=0; const char* q=0;
    if(which==4)q=h; else if(istarts_(h,"watch")){ const char* a=h; while(*a&&*a!='#'){ if((*a=='?'||*a=='&')&&a[1]=='v'&&a[2]=='='){ q=a+3; break; } a++; } }
    else if(istarts_(h,"shorts/"))q=h+7; else if(istarts_(h,"embed/"))q=h+6; else if(istarts_(h,"live/"))q=h+5;
    if(!q){ if(which==4)return 0; int home=(!*h||*h=='?'||*h=='#'||istarts_(h,"results")||istarts_(h,"feed")||istarts_(h,"index")); if(!home)return 0;
        char qs[256]; int qn=0; const char* a=h; while(*a&&*a!='#'){ if((*a=='?'||*a=='&')&&istarts_(a+1,"search_query=")){ a+=14; while(*a&&*a!='&'&&*a!='#'&&qn<250){ char c=*a++; if(c=='+')c=' '; else if(c=='%'&&a[0]&&a[1]){ int v=0; for(int k=0;k<2;k++){ char d=a[k]; v=v*16+(d>='0'&&d<='9'?d-'0':(d|32)>='a'&&(d|32)<='f'?(d|32)-'a'+10:0); } a+=2; c=(char)v; } if((unsigned char)c>=32)qs[qn++]=c; } break; } a++; } qs[qn]=0;
        hb b={0,0,0}; hb_put(&b,"<!doctype html><html><head><title>YouTube</title>"); hb_put(&b,YT_CSS); hb_put(&b,"</head><body data-q=\""); hb_esc(&b,qs); hb_put(&b,"\">"); hb_put(&b,YT_HDR); hb_put(&b,YT_HOME_JS); hb_put(&b,"</body></html>");
        show_page("about:youtube",&b,qn?"YouTube search":"YouTube"); return 1; }
    while(yt_idch(q[n])&&n<15){ id[n]=q[n]; n++; } id[n]=0; if(n<6)return 0;
    hb b={0,0,0}; hb_put(&b,"<!doctype html><html><head><title>YouTube</title>"); hb_put(&b,YT_CSS); hb_put(&b,"</head><body data-v=\""); hb_put(&b,id); hb_put(&b,"\">"); hb_put(&b,YT_HDR); hb_put(&b,"<div class=ym>");
    hb_put(&b,YT_JS); hb_put(&b,"</div></body></html>");
    show_page("about:youtube",&b,"YouTube"); return 1; }
#define PERM_N 256
static const char* PERM_NAMES[6]={"notifications","geolocation","camera","microphone","popups","clipboard-read"};
static const char* PERM_TEXT[6]={"show notifications","know your location","use your camera","use your microphone","open pop-up windows","see text and images you copy"};
typedef struct { char org[120]; unsigned char name, state; } permrec;
static permrec g_perm[PERM_N]; static int g_nperm=0, g_perm_loaded=0;
static struct { int on, id, name, answer; char org[120]; } g_pq; static int g_pq_next=1, g_pq_last=-1, g_pq_last_ans=0;
static int g_pbar_y=0, g_pbar_ax=0, g_pbar_bx=0, g_pbar_cx=0;
static int g_smenu=0, g_smx=0, g_smy=0, g_smw=0, g_smh=0, g_lockx=0, g_locky=0;
static int perm_idx(const char* n){ for(int i=0;i<6;i++) if(seq(PERM_NAMES[i],n))return i; return -1; }
static void perm_fname(char* out){ extern void acct_profile_name(const char*,char*,int); acct_profile_name("permissions.txt",out,64); }
static void perm_load(void){
    if(g_perm_loaded)return; g_perm_loaded=1; g_nperm=0;
    char fn[64]; perm_fname(fn); int k=k_find_file(fn); if(k<0)return;
    const char* d=fsd(k); int n=FS[k].len, i=0;
    while(i<n&&g_nperm<PERM_N){ int st=d[i]-'0'; i++; if(st<1||st>2){ while(i<n&&d[i]!='\n')i++; i++; continue; } while(i<n&&d[i]==' ')i++;
        char nm[24]; int q=0; while(i<n&&d[i]!=' '&&d[i]!='\n'&&q<23)nm[q++]=d[i++]; nm[q]=0; while(i<n&&d[i]==' ')i++;
        permrec* r=&g_perm[g_nperm]; q=0; while(i<n&&d[i]!='\n'&&q<119)r->org[q++]=d[i++]; r->org[q]=0; while(i<n&&d[i]!='\n')i++; i++;
        int pi=perm_idx(nm); if(pi>=0&&r->org[0]){ r->name=(unsigned char)pi; r->state=(unsigned char)st; g_nperm++; } }
}
static void perm_save(void){
    static char buf[PERM_N*150]; int o=0;
    for(int i=0;i<g_nperm&&o<(int)sizeof(buf)-150;i++){ buf[o++]=(char)('0'+g_perm[i].state); buf[o++]=' '; const char* nm=PERM_NAMES[g_perm[i].name]; while(*nm)buf[o++]=*nm++; buf[o++]=' '; const char* og=g_perm[i].org; while(*og)buf[o++]=*og++; buf[o++]='\n'; }
    char fn[64]; perm_fname(fn); fs_save_bin(fn,(const u8*)buf,o,0); }
static int perm_get_i(const char* org,int pi){ perm_load(); for(int i=0;i<g_nperm;i++) if(g_perm[i].name==pi&&seq(g_perm[i].org,org))return g_perm[i].state; return 0; }
static void perm_set_i(const char* org,int pi,int st){
    perm_load(); for(int i=0;i<g_nperm;i++) if(g_perm[i].name==pi&&seq(g_perm[i].org,org)){ if(st==0){ g_perm[i]=g_perm[--g_nperm]; } else g_perm[i].state=(unsigned char)st; perm_save(); return; }
    if(st==0||g_nperm>=PERM_N)return; permrec* r=&g_perm[g_nperm++]; int q=0; for(;org[q]&&q<119;q++)r->org[q]=org[q]; r->org[q]=0; r->name=(unsigned char)pi; r->state=(unsigned char)st; perm_save(); }
int nwk_perm_get(const char* org,const char* name){ int pi=perm_idx(name); if(pi<0||!org||!org[0])return 0; return perm_get_i(org,pi); }
int nwk_perm_ask(const char* org,const char* name){
    int pi=perm_idx(name); if(pi<0||!org||!org[0])return -2;
    int st=perm_get_i(org,pi); if(st)return -st;
    if(g_pq.on){ if(g_pq.name==pi&&seq(g_pq.org,org))return g_pq.id; return -3; }
    g_pq.on=1; g_pq.id=g_pq_next++; g_pq.name=pi; g_pq.answer=0; int q=0; for(;org[q]&&q<119;q++)g_pq.org[q]=org[q]; g_pq.org[q]=0;
    return g_pq.id; }
int nwk_perm_poll(int id){ if(g_pq.on&&g_pq.id==id)return 0; if(id==g_pq_last)return g_pq_last_ans; return 2; }
static void perm_answer(int st,int remember){
    if(!g_pq.on)return; if(remember&&st)perm_set_i(g_pq.org,g_pq.name,st);
    g_pq_last=g_pq.id; g_pq_last_ans=st?st:2; g_pq.on=0; }
static const char* perm_host(const char* org){ const char* h=org; for(const char* q=org;*q;q++) if(q[0]=='/'&&q[1]=='/'){ h=q+2; break; } return h; }
static void perm_bar_draw(int x,int y,int w){
    g_pbar_y=0; if(!g_pq.on||g_page!=3)return;
    int h=40; fill_rect(x,y,w,h,rgb(255,248,225)); fill_rect(x,y+h-1,w,1,rgb(230,210,160));
    char t[260],*p=t; p=scopy(p,perm_host(g_pq.org)); p=scopy(p,g_pq.name==4?" tried to ":" wants to "); p=scopy(p,PERM_TEXT[g_pq.name]); *p=0;
    pdstr(x+14,y+12,t,rgb(60,50,20),1);
    const char* a=g_pq.name==4?"Always allow":"Allow"; const char* b=g_pq.name==4?"Keep blocking":"Block";
    int bw=pw_str((char*)b,1)+24, aw=pw_str((char*)a,1)+24; int bx=x+w-14-bw-26, ax=bx-8-aw;
    fill_round(ax,y+7,aw,26,6,15,ACC); pdstr_ctr(ax+aw/2,y+12,(char*)a,rgb(255,255,255),1);
    fill_round(bx,y+7,bw,26,6,15,rgb(234,236,240)); pdstr_ctr(bx+bw/2,y+12,(char*)b,rgb(40,44,52),1);
    int cx=x+w-30; pdstr(cx+6,y+12,"x",rgb(120,110,80),1);
    g_pbar_y=y; g_pbar_ax=ax; g_pbar_bx=bx; g_pbar_cx=cx; }
