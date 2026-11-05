#include "include/nvx/base.h"
#include "include/nvx/lib/base/fmt.h"
#include "include/nvx/drivers/video/fb/framebuffer.h"
#include "include/nvx/graphics/theme/theme.h"
#include "include/nvx/graphics/ui/wm.h"
#include "include/nvx/core/registry.h"

#define RE_K 96
#define RE_V 48
static char re_keys[RE_K][64]; static int re_nk, re_ksel, re_kscroll;
static char re_vals[RE_V][32]; static int re_nv, re_vsel=-1;
static int re_mode; static char re_in[100]; static int re_inl; static char re_find[40];
static char re_msg[96]; static int re_msg_err;
static int re_hx[48], re_hy[48], re_hw[48], re_hh[48], re_hid[48], re_hn;
static int re_kx, re_ky, re_kw, re_kh, re_krh;

enum { RM_NONE, RM_EDIT, RM_NEWNAME, RM_NEWVAL, RM_NEWKEY, RM_FIND, RM_EXPORT, RM_IMPORT };
static char re_newname[32];

static void re_hit(int x,int y,int w,int h,int id){ if(re_hn<48){ re_hx[re_hn]=x; re_hy[re_hn]=y; re_hw[re_hn]=w; re_hh[re_hn]=h; re_hid[re_hn]=id; re_hn++; } }
static void re_cp(char* d,const char* s,int cap){ int i=0; for(;s[i]&&i<cap-1;i++)d[i]=s[i]; d[i]=0; }
static int re_has(const char* s,const char* w){ if(!w[0])return 1; for(int i=0;s[i];i++){ int k=0; while(w[k]){ char a=s[i+k], b=w[k]; if(a>='A'&&a<='Z')a+=32; if(b>='A'&&b<='Z')b+=32; if(a!=b)break; k++; } if(!w[k])return 1; } return 0; }
static void re_say(const char* m,int err){ re_cp(re_msg,m,96); re_msg_err=err; }

static void re_reload(void){
    char cur[64]; re_cp(cur,re_nk&&re_ksel<re_nk?re_keys[re_ksel]:"System\\Display",64);
    re_nk=reg_keys(re_keys,RE_K); re_ksel=0; for(int i=0;i<re_nk;i++){ int s=1; for(int k=0;;k++){ if(re_keys[i][k]!=cur[k]){ s=0; break; } if(!cur[k])break; } if(s){ re_ksel=i; break; } }
    re_nv=reg_values(re_keys[re_ksel],re_vals,RE_V); if(re_vsel>=re_nv)re_vsel=-1; }
void regedit_open(void){ re_reload(); re_mode=RM_NONE; re_msg[0]=0; }

static int re_depth(const char* k){ int d=0; for(int i=0;k[i];i++) if(k[i]=='\\')d++; return d; }
static const char* re_leaf(const char* k){ const char* l=k; for(int i=0;k[i];i++) if(k[i]=='\\')l=k+i+1; return l; }
static int re_matches(int i){ if(!re_find[0])return 1; if(re_has(re_keys[i],re_find))return 1; char nm[RE_V][32]; int c=reg_values(re_keys[i],nm,RE_V); for(int q=0;q<c;q++){ if(re_has(nm[q],re_find))return 1; char v[96]; reg_get(re_keys[i],nm[q],v,96); if(re_has(v,re_find))return 1; } return 0; }

static void re_btn(int x,int y,const char* t,int id,int on){ int w=pw_str(t,1)+20; int hov=on&&in_rect(mx,my,x,y,w,26); fill_round(x,y,w,26,6,15,!on?rgb(236,236,240):hov?rgb(214,224,244):rgb(255,255,255)); fill_round(x,y,w,1,0,15,rgb(210,212,220)); pdstr(x+10,y+5,t,on?INK:rgb(160,160,168),1); if(on)re_hit(x,y,w,26,id); }

void regedit_draw(int i){
    { extern int g_gm_ready; extern void gmetrics(void); if(!g_gm_ready)gmetrics(); }
    if(!re_nk)re_reload(); re_hn=0;
    win_frame(i,"Registry Editor");
    int X=WN[i].x+1, Y=WN[i].y+TITLEH, W2=WN[i].w-2, H2=WN[i].h-TITLEH-1;
    fill_rect(X,Y,W2,H2,rgb(246,247,250));
    int bx=X+10, by=Y+8; const char* key=re_keys[re_ksel]; int canw=reg_can_write(key);
    int vro=1; if(re_vsel>=0){ char v[96]; vro=(reg_get(key,re_vals[re_vsel],v,96)==2); }
    re_btn(bx,by,"New key",10,canw); bx+=pw_str("New key",1)+26;
    re_btn(bx,by,"New value",11,canw); bx+=pw_str("New value",1)+26;
    re_btn(bx,by,"Edit",12,re_vsel>=0&&!vro&&canw); bx+=pw_str("Edit",1)+26;
    re_btn(bx,by,"Delete",13,canw); bx+=pw_str("Delete",1)+26;
    re_btn(bx,by,"Export",14,1); bx+=pw_str("Export",1)+26;
    re_btn(bx,by,"Import",15,1); bx+=pw_str("Import",1)+26;
    { int sw=180, sx=X+W2-sw-10; fill_round(sx,by,sw,26,13,15,rgb(255,255,255)); fill_round(sx,by,sw,1,0,15,rgb(210,212,220));
      const char* st=re_mode==RM_FIND?re_in:re_find; pdstr(sx+12,by+5,st[0]?st:"Search",st[0]?INK:rgb(150,154,166),1); if(re_mode==RM_FIND)fill_rect(sx+12+pw_str(re_in,1)+1,by+5,1,16,INK); re_hit(sx,by,sw,26,16); }
    int top=Y+44, bot=Y+H2-(re_mode!=RM_NONE&&re_mode!=RM_FIND?74:34);
    int lw=W2*36/100; if(lw<200)lw=200;
    fill_rect(X+10,top,lw,bot-top,rgb(255,255,255)); fill_rect(X+10,top,lw,1,rgb(220,222,228));
    re_kx=X+10; re_ky=top; re_kw=lw; re_kh=bot-top; re_krh=22;
    int vis=(bot-top-6)/re_krh; int shown=0, row=0;
    for(int k=0;k<re_nk;k++){ if(!re_matches(k))continue; if(row++<re_kscroll)continue; if(shown>=vis)break; int ry=top+3+shown*re_krh; int d=re_depth(re_keys[k]);
        if(k==re_ksel)fill_rect(X+11,ry,lw-2,re_krh,mix(rgb(255,255,255),ACC,50)); else if(in_rect(mx,my,X+11,ry,lw-2,re_krh))fill_rect(X+11,ry,lw-2,re_krh,rgb(240,243,250));
        int ix=X+18+d*16; fill_round(ix,ry+5,14,11,2,15,rgb(232,190,90)); fill_rect(ix,ry+4,6,3,rgb(232,190,90));
        pdstr(ix+20,ry+3,d?re_leaf(re_keys[k]):re_keys[k],INK,1); shown++; }
    int vx=X+20+lw, vw=W2-lw-30;
    fill_rect(vx,top,vw,bot-top,rgb(255,255,255)); fill_rect(vx,top,vw,1,rgb(220,222,228));
    { char path[80],*p=path; p=scopy(p,"Computer\\"); p=scopy(p,key); *p=0; pdstr(vx+10,top+6,path,rgb(90,96,112),1); }
    int c1=vx+10, c2=vx+10+vw*30/100, c3=vx+10+vw*55/100;
    pdstr(c1,top+30,"Name",rgb(110,116,130),1); pdstr(c2,top+30,"Value",rgb(110,116,130),1); pdstr(c3,top+30,"About",rgb(110,116,130),1); fill_rect(vx+6,top+50,vw-12,1,rgb(230,232,236));
    for(int v=0;v<re_nv;v++){ int ry=top+54+v*24; if(ry+24>bot)break; char val[96]; int r=reg_get(key,re_vals[v],val,96);
        if(v==re_vsel)fill_rect(vx+1,ry,vw-2,24,mix(rgb(255,255,255),ACC,50)); else if(in_rect(mx,my,vx+1,ry,vw-2,24))fill_rect(vx+1,ry,vw-2,24,rgb(240,243,250));
        fill_round(c1,ry+6,12,12,3,15,r==2?rgb(170,174,186):rgb(80,140,220));
        { char t[32]; re_cp(t,re_vals[v],30); pdstr(c1+18,ry+4,t,INK,1); }
        { char t[40]; re_cp(t,val,26); pdstr(c2,ry+4,t,INK,1); }
        { const char* h=reg_help(key,re_vals[v]); char t[64]; int mxw=(vx+vw-c3-8)/8; if(mxw>62)mxw=62; if(mxw<4)mxw=4; re_cp(t,h[0]?h:(r==2?"Read only":""),mxw+1); pdstr(c3,ry+4,t,rgb(120,126,140),1); }
        re_hit(vx+1,ry,vw-2,24,100+v); }
    if(!re_nv)pdstr(vx+10,top+60,"No values in this key",rgb(140,144,156),1);
    if(re_mode!=RM_NONE&&re_mode!=RM_FIND){
        int ey=bot+8; const char* lab=re_mode==RM_EDIT?"New value:":re_mode==RM_NEWNAME?"Name of the new value:":re_mode==RM_NEWVAL?"Value:":re_mode==RM_NEWKEY?"Name of the new key:":re_mode==RM_EXPORT?"Export to file:":"Import from file:";
        pdstr(X+12,ey+6,lab,INK,1); int fx=X+12+pw_str(lab,1)+10, fw=W2-(fx-X)-190;
        fill_round(fx,ey,fw,28,6,15,rgb(255,255,255)); fill_round(fx,ey,fw,1,0,15,ACC); pdstr(fx+8,ey+6,re_in,INK,1); fill_rect(fx+8+pw_str(re_in,1)+1,ey+6,1,16,INK);
        re_btn(X+W2-170,ey+1,"OK",20,1); re_btn(X+W2-110,ey+1,"Cancel",21,1); }
    { int sy=Y+H2-26; fill_rect(X,sy-2,W2,1,rgb(222,224,230)); if(re_msg[0])pdstr(X+12,sy+3,re_msg,re_msg_err?rgb(196,40,40):rgb(40,120,70),1);
      else pdstr(X+12,sy+3,canw?"Double-click a value or press Enter to edit. Saved in /etc/noovex/registry":"Only an administrator can change System keys",rgb(120,126,140),1); }
}

static void re_start(int m,const char* init){ re_mode=m; re_cp(re_in,init?init:"",100); re_inl=0; while(re_in[re_inl])re_inl++; re_msg[0]=0; }
static void re_commit(void){
    const char* key=re_keys[re_ksel]; int r=0; char b[120],*p;
    if(re_mode==RM_EDIT&&re_vsel>=0){ r=reg_set(key,re_vals[re_vsel],re_in); if(r)re_say(reg_err(r),1); else re_say("Saved",0); }
    else if(re_mode==RM_NEWNAME){ if(!re_in[0]){ re_mode=RM_NONE; return; } re_cp(re_newname,re_in,32); re_start(RM_NEWVAL,""); return; }
    else if(re_mode==RM_NEWVAL){ r=reg_set(key,re_newname,re_in); if(r)re_say(reg_err(r),1); else re_say("Value added",0); }
    else if(re_mode==RM_NEWKEY){ if(!re_in[0]){ re_mode=RM_NONE; return; } char nk[64]; p=nk; p=scopy(p,key); *p++='\\'; *p=0; int L=(int)(p-nk); re_cp(p,re_in,64-L); r=reg_add_key(nk); if(r)re_say(reg_err(r),1); else { re_say("Key added",0); re_mode=RM_NONE; re_reload(); for(int i=0;i<re_nk;i++){ int s=1; for(int k=0;;k++){ char a=re_keys[i][k], c=nk[k]; if(a>='A'&&a<='Z')a+=32; if(c>='A'&&c<='Z')c+=32; if(a!=c){ s=0; break; } if(!a)break; } if(s){ re_ksel=i; break; } } re_nv=reg_values(re_keys[re_ksel],re_vals,RE_V); re_vsel=-1; return; } }
    else if(re_mode==RM_EXPORT){ int n=reg_export(re_in,key); p=b; if(n<0)p=scopy(p,"Could not write the file"); else { p=scopy(p,"Exported "); p=udec(p,(u32)n); p=scopy(p," values to "); p=scopy(p,re_in); } *p=0; re_say(b,n<0); }
    else if(re_mode==RM_IMPORT){ int s=0,f=0; int q=reg_import(re_in,&s,&f); p=b; if(q<0)p=scopy(p,"Could not read the file"); else { p=scopy(p,"Imported "); p=udec(p,(u32)s); p=scopy(p," values"); if(f){ p=scopy(p,", "); p=udec(p,(u32)f); p=scopy(p," refused"); } } *p=0; re_say(b,q<0||f); }
    re_mode=RM_NONE; re_reload(); }

static u64 re_lastclick; static int re_lastv=-2;
void regedit_click(int px,int py){
    extern volatile u64 g_ticks;
    if(in_rect(px,py,re_kx,re_ky,re_kw,re_kh)){ int idx=(py-re_ky-3)/re_krh; if(idx<0)return; int row=0, shown=0;
        for(int k=0;k<re_nk;k++){ if(!re_matches(k))continue; if(row++<re_kscroll)continue; if(shown==idx){ re_ksel=k; re_vsel=-1; re_nv=reg_values(re_keys[k],re_vals,RE_V); if(re_mode==RM_EDIT||re_mode==RM_NEWVAL||re_mode==RM_NEWNAME)re_mode=RM_NONE; return; } shown++; } return; }
    for(int k=re_hn-1;k>=0;k--) if(in_rect(px,py,re_hx[k],re_hy[k],re_hw[k],re_hh[k])){ int id=re_hid[k]; const char* key=re_keys[re_ksel];
        if(id>=100&&id<100+RE_V){ int v=id-100; int dbl=(v==re_lastv&&g_ticks-re_lastclick<45); re_vsel=v; re_lastv=v; re_lastclick=g_ticks;
            if(dbl){ char val[96]; if(reg_get(key,re_vals[v],val,96)==2){ re_say("This value is read only",1); return; } re_start(RM_EDIT,val); } return; }
        if(id==10)re_start(RM_NEWKEY,""); else if(id==11)re_start(RM_NEWNAME,"");
        else if(id==12&&re_vsel>=0){ char val[96]; reg_get(key,re_vals[re_vsel],val,96); re_start(RM_EDIT,val); }
        else if(id==13){ int r=reg_delete(key,re_vsel>=0?re_vals[re_vsel]:""); if(r)re_say(reg_err(r),1); else re_say(re_vsel>=0?"Value deleted":"Key deleted",0); re_vsel=-1; re_reload(); }
        else if(id==14){ char p[80],*q=p; q=scopy(q,"/root/registry-export.reg"); *q=0; re_start(RM_EXPORT,p); }
        else if(id==15)re_start(RM_IMPORT,"/root/registry-export.reg");
        else if(id==16)re_start(RM_FIND,re_find);
        else if(id==20)re_commit(); else if(id==21)re_mode=RM_NONE;
        return; } }
int regedit_key(int c){
    if(re_mode!=RM_NONE){
        if(c==27){ if(re_mode==RM_FIND)re_find[0]=0; re_mode=RM_NONE; return 1; }
        if(c=='\n'){ if(re_mode==RM_FIND){ re_cp(re_find,re_in,40); re_mode=RM_NONE; re_kscroll=0; return 1; } re_commit(); return 1; }
        if(c=='\b'){ if(re_inl>0)re_in[--re_inl]=0; if(re_mode==RM_FIND)re_cp(re_find,re_in,40); return 1; }
        if(c>=32&&c<127&&re_inl<98){ re_in[re_inl++]=(char)c; re_in[re_inl]=0; if(re_mode==RM_FIND){ re_cp(re_find,re_in,40); re_kscroll=0; } return 1; }
        return 0; }
    if(c==19||c==20){ int d=c==19?-1:1; int n=re_ksel; for(int s=0;s<re_nk;s++){ n+=d; if(n<0||n>=re_nk)break; if(re_matches(n)){ re_ksel=n; re_vsel=-1; re_nv=reg_values(re_keys[n],re_vals,RE_V); break; } } return 1; }
    if(c=='\n'&&re_vsel>=0){ char val[96]; if(reg_get(re_keys[re_ksel],re_vals[re_vsel],val,96)==2){ re_say("This value is read only",1); return 1; } re_start(RM_EDIT,val); return 1; }
    if(c==6){ re_start(RM_FIND,re_find); return 1; }
    return 0; }
void regedit_wheel(int d){ re_kscroll+=d>0?-3:3; if(re_kscroll<0)re_kscroll=0; if(re_kscroll>re_nk-1)re_kscroll=re_nk>0?re_nk-1:0; }
