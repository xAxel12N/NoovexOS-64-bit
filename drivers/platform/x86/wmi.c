#include "include/nvx/base.h"
#include "include/nvx/core/klog.h"
#include "include/nvx/lib/base/fmt.h"
#include "include/nvx/mem/heap.h"
#include "include/nvx/drivers/acpi/aml.h"
#include "include/nvx/drivers/platform/x86/wmi.h"

wmi_blk g_wmi[WMI_MAX];
int g_wmi_n=0, g_wmi_ndev=0;
u32 g_wmi_events=0;
static struct { u8 g[16]; wmi_event_cb cb; } g_wmi_cb[8];
static int g_wmi_ncb=0;
static const char wmi_hx[]="0123456789ABCDEF";

static void wz(aobj* o){ o->type=AT_NONE; o->p=0; o->e=0; o->own=0; o->len=0; o->i=0; o->cont=0; o->node=0; }
static int wmi_eval(anode* m,aobj* a,int n,aobj* out){
    if(!m||!g_aml_ok)return -1; u64 dl=g_aml_hard_deadline; g_aml_hard_deadline=0; aobj o; wz(&o); int r=aml_eval(m,a,n,&o); g_aml_hard_deadline=dl;
    if(!r&&o.type==AT_REF){ aobj d; wz(&d); aml_deref(&o,&d); aobj_free(&o); o=d; }
    if(out)*out=o; else aobj_free(&o); return r; }
static int hxv(char c){ return c>='0'&&c<='9'?c-'0':c>='a'&&c<='f'?c-'a'+10:c>='A'&&c<='F'?c-'A'+10:-1; }
int wmi_guid_parse(const char* s,u8* g){
    static const u8 ord[16]={3,2,1,0,5,4,7,6,8,9,10,11,12,13,14,15}; u8 raw[16]; int k=0;
    for(int i=0;s[i]&&k<16;i++){ if(s[i]=='-')continue; int a=hxv(s[i]),b=s[i+1]?hxv(s[i+1]):-1; if(a<0||b<0)return -1; raw[k++]=(u8)(a<<4|b); i++; }
    if(k!=16)return -1; for(int i=0;i<16;i++)g[ord[i]]=raw[i]; return 0; }
void wmi_guid_str(const u8* g,char* out){
    static const u8 ord[16]={3,2,1,0,5,4,7,6,8,9,10,11,12,13,14,15}; int p=0;
    for(int i=0;i<16;i++){ if(i==4||i==6||i==8||i==10)out[p++]='-'; u8 b=g[ord[i]]; out[p++]=wmi_hx[b>>4]; out[p++]=wmi_hx[b&15]; } out[p]=0; }
static int geq(const u8* a,const u8* b){ for(int i=0;i<16;i++) if(a[i]!=b[i])return 0; return 1; }
const wmi_blk* wmi_find(const char* guid){ u8 g[16]; if(wmi_guid_parse(guid,g))return 0; for(int i=0;i<g_wmi_n;i++) if(geq(g_wmi[i].guid,g))return &g_wmi[i]; return 0; }
int wmi_has(const char* guid){ return wmi_find(guid)!=0; }
static anode* wmi_m(const wmi_blk* b,char c0,char c1,char x0,char x1){ char nm[5]={c0,c1,x0,x1,0}; return aml_child(b->dev,nm); }
int wmi_method(const char* guid,u32 inst,u32 mid,const u8* in,u32 inlen,aobj* out){
    const wmi_blk* b=wmi_find(guid); if(out)wz(out); if(!b||!(b->flags&WMI_METHOD)||(b->ninst&&inst>=b->ninst))return -1;
    anode* m=wmi_m(b,'W','M',b->oid[0],b->oid[1]); if(!m)return -1;
    aobj a[3]; for(int k=0;k<3;k++)wz(&a[k]); aobj_int(&a[0],inst); aobj_int(&a[1],mid); int n=2;
    if(in&&inlen){ u8* p=(u8*)kmalloc(inlen); if(!p)return -1; for(u32 k=0;k<inlen;k++)p[k]=in[k]; a[2].type=(b->flags&WMI_STRING)?AT_STR:AT_BUF; a[2].p=p; a[2].len=inlen; n=3; }
    int r=wmi_eval(m,a,n,out); for(int k=0;k<3;k++)aobj_free(&a[k]); return r; }
int wmi_query(const char* guid,u32 inst,aobj* out){
    const wmi_blk* b=wmi_find(guid); if(out)wz(out); if(!b||(b->flags&(WMI_METHOD|WMI_EVENT))||(b->ninst&&inst>=b->ninst))return -1;
    anode* q=wmi_m(b,'W','Q',b->oid[0],b->oid[1]); if(!q)return -1;
    anode* c=(b->flags&WMI_EXPENSIVE)?wmi_m(b,'W','C',b->oid[0],b->oid[1]):0; aobj x; wz(&x);
    if(c){ aobj_int(&x,1); wmi_eval(c,&x,1,0); }
    aobj a; wz(&a); aobj_int(&a,inst); int r=wmi_eval(q,&a,1,out); aobj_free(&a);
    if(c){ aobj_int(&x,0); wmi_eval(c,&x,1,0); } return r; }
int wmi_set_events(const char* guid,int on){
    u8 g[16]; if(wmi_guid_parse(guid,g))return -1; int done=0;
    for(int i=0;i<g_wmi_n;i++){ wmi_blk* b=&g_wmi[i]; if(!geq(b->guid,g)||!(b->flags&WMI_EVENT))continue; u8 id=(u8)b->oid[0];
        anode* m=wmi_m(b,'W','E',wmi_hx[id>>4],wmi_hx[id&15]); done=1; if(!m)continue; aobj a; wz(&a); aobj_int(&a,on?1:0); wmi_eval(m,&a,1,0); }
    return done?0:-1; }
int wmi_on_event(const char* guid,wmi_event_cb cb){
    if(g_wmi_ncb>=8)return -1; if(wmi_guid_parse(guid,g_wmi_cb[g_wmi_ncb].g))return -1; g_wmi_cb[g_wmi_ncb++].cb=cb; return 0; }
int wmi_notify(anode* n,u32 v){
    if(!n)return 0; if(hp_wmi_notify(n,v))return 1; if(!g_wmi_ndev)return 0;
    for(int i=0;i<g_wmi_n;i++){ wmi_blk* b=&g_wmi[i]; if(b->dev!=n||!(b->flags&WMI_EVENT)||(u8)b->oid[0]!=(u8)v)continue;
        g_wmi_events++; aobj d; wz(&d); aobj a; wz(&a); aobj_int(&a,v); anode* w=aml_child(n,"_WED"); if(w)wmi_eval(w,&a,1,&d); aobj_free(&a);
        int hit=0; for(int k=0;k<g_wmi_ncb;k++) if(geq(g_wmi_cb[k].g,b->guid)){ g_wmi_cb[k].cb(b,v,&d); hit=1; }
        if(!hit){ extern void klog_key(u32); klog_key(0x60000u|(v&0xFF)); }
        aobj_free(&d); return 1; }
    return 0; }

void wmi_init(void){
    if(!g_aml_ok||!g_aroot)return; anode* dv[WMI_DEV_MAX]; int n=aml_find_devices(g_aroot,"PNP0C14",dv,WMI_DEV_MAX,0,0);
    for(int i=0;i<n;i++){ if(!(aml_sta(dv[i])&1))continue; anode* w=aml_child(dv[i],"_WDG"); if(!w)continue;
        aobj o; if(wmi_eval(w,0,0,&o))continue; if(o.type!=AT_BUF||!o.p){ aobj_free(&o); continue; } g_wmi_ndev++;
        for(u32 off=0;off+20<=o.len&&g_wmi_n<WMI_MAX;off+=20){ wmi_blk* b=&g_wmi[g_wmi_n++]; const u8* e=o.p+off;
            for(int k=0;k<16;k++)b->guid[k]=e[k]; b->oid[0]=(char)e[16]; b->oid[1]=(char)e[17]; b->ninst=e[18]; b->flags=e[19]; b->dev=dv[i]; }
        aobj_free(&o); }
    if(g_wmi_n){ char b[64],*p=b; p=scopy(p,"wmi: "); p=udec(p,(u32)g_wmi_n); p=scopy(p," blocks on "); p=udec(p,(u32)g_wmi_ndev); p=scopy(p," devices"); *p=0; klog(b); }
    hp_wmi_init(); }
