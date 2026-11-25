#include "include/nvx/base.h"
#include "drivers/input/gamepad/gpad_proto.h"
#include "drivers/input/gamepad/gamepad.h"
u64 hr_ns(void);
void klog(const char* s);
static struct gpad g_gp[GP_MAX];
u32 g_gp_gen;
int g_gp_tpptr;
static const u16 gp_keys[GPB_N_]={0x130,0x131,0x134,0x133,0x136,0x137,0x13A,0x13B,0x13C,0x13D,0x13E,0,0,0,0,0x135,0x132};
const u16 gp_keylist[GP_NKEY]={0x130,0x131,0x132,0x133,0x134,0x135,0x136,0x137,0x13A,0x13B,0x13C,0x13D,0x13E};
const u8 gp_abslist[GP_NABS]={0x00,0x01,0x02,0x03,0x04,0x05,0x10,0x11};
static void gp_push(struct gpad* p,u64 t,u16 ty,u16 co,int v){ struct gpev* e=&p->q[p->qw%GP_Q]; e->ns=t; e->type=ty; e->code=co; e->val=v; __sync_synchronize(); p->qw++; }
int gpad_valid(int i){ return i>=0&&i<GP_MAX&&g_gp[i].used; }
struct gpad* gpad_at(int i){ return gpad_valid(i)?&g_gp[i]:0; }
int gpad_count(void){ int n=0; for(int i=0;i<GP_MAX;i++) if(g_gp[i].used)n++; return n; }
int gpad_add(int type,int bus,u16 vid,u16 pid,const char* name,int caps,int (*out)(int,int,int,int,u8,u8,u8),int ref){
    int i=0; while(i<GP_MAX&&g_gp[i].used)i++; if(i>=GP_MAX)return -1;
    struct gpad* p=&g_gp[i]; u32 qw=p->qw; for(u32 k=0;k<sizeof *p;k++)((u8*)p)[k]=0; p->qw=qw;
    p->type=type; p->bus=bus; p->vid=vid; p->pid=pid; p->caps=caps; p->out=out; p->ref=ref; p->bat=-1;
    int q=0; if(name) for(;name[q]&&q<47;q++)p->name[q]=name[q]; p->name[q]=0;
    int pl=1; for(int k=0;k<GP_MAX;k++) if(g_gp[k].used&&g_gp[k].player>=pl)pl=g_gp[k].player+1; p->player=pl>4?4:pl;
    p->rgb[0]=0; p->rgb[1]=0; p->rgb[2]=64;
    p->used=1; p->gen++; g_gp_gen++;
    if(out)out(i,ref,0,0,p->rgb[0],p->rgb[1],p->rgb[2]);
    return i; }
void gpad_remove(int i){ if(!gpad_valid(i))return; g_gp[i].used=0; g_gp[i].gen++; g_gp_gen++; }
static int gp_hx(u32 b){ return ((b>>GPB_DR)&1)-((b>>GPB_DL)&1); }
static int gp_hy(u32 b){ return ((b>>GPB_DD)&1)-((b>>GPB_DU)&1); }
void gpad_report(int i,const struct gpst* s){
    if(!gpad_valid(i)||!s->valid)return;
    struct gpad* p=&g_gp[i]; struct gpst* o=&p->st; u64 t=hr_ns(); int n=0;
    u32 ch=s->btn^o->btn;
    for(int b=0;b<GPB_N_;b++) if(((ch>>b)&1)&&gp_keys[b]){ gp_push(p,t,1,gp_keys[b],(s->btn>>b)&1); n++; }
    int nv[8]={s->lx,s->ly,s->lt,s->rx,s->ry,s->rt,gp_hx(s->btn),gp_hy(s->btn)}, ov[8]={o->lx,o->ly,o->lt,o->rx,o->ry,o->rt,gp_hx(o->btn),gp_hy(o->btn)};
    for(int a=0;a<8;a++) if(nv[a]!=ov[a]||!p->seen){ gp_push(p,t,3,gp_abslist[a],nv[a]); n++; }
    if(n)gp_push(p,t,0,0,0);
    int tp=o->tp, ox=o->tpx, oy=o->tpy, oc=(o->btn>>GPB_TPAD)&1, nc=(s->btn>>GPB_TPAD)&1;
    if(g_gp_tpptr&&(p->caps&GPC_TOUCH)&&((s->tp&&tp)||nc!=oc))gpad_pointer(s->tp&&tp?(s->tpx-ox)*2/3:0,s->tp&&tp?(s->tpy-oy)*2/3:0,nc?1:oc?2:0);
    *o=*s; if(!s->tp&&tp)p->tp_up=1;
    if(s->bat>0||s->chg)p->bat=s->bat; p->chg=s->chg; p->seen=1; p->nrep++; }
int gpad_set_out(int i,int strong,int weak,int r,int g,int b){
    if(!gpad_valid(i))return -1; struct gpad* p=&g_gp[i];
    if(strong>=0)p->rs=gp_clamp(strong,0,255); if(weak>=0)p->rw=gp_clamp(weak,0,255);
    if(r>=0){ p->rgb[0]=(u8)r; p->rgb[1]=(u8)g; p->rgb[2]=(u8)b; }
    if(!p->out)return -1;
    return p->out(i,p->ref,p->rs,p->rw,p->rgb[0],p->rgb[1],p->rgb[2]); }
int gpad_rumble(int i,int strong,int weak,int ms){ if(!gpad_valid(i))return -1; struct gpad* p=&g_gp[i]; if(!(p->caps&GPC_RUMBLE))return -1; p->rstop=ms>0?hr_ns()+(u64)ms*1000000ULL:0; return gpad_set_out(i,strong,weak,-1,-1,-1); }
int gpad_led(int i,int r,int g,int b){ if(!gpad_valid(i)||!(g_gp[i].caps&GPC_LED))return -1; return gpad_set_out(i,-1,-1,r&255,g&255,b&255); }
void gpad_tick(void){ u64 t=hr_ns(); for(int i=0;i<GP_MAX;i++){ struct gpad* p=&g_gp[i]; if(p->used&&p->rstop&&t>=p->rstop){ p->rstop=0; gpad_set_out(i,0,0,-1,-1,-1); } } }
int gpad_get(int i,struct gpst* s){ if(!gpad_valid(i))return 0; *s=g_gp[i].st; return 1; }
static const char* const gp_tn[]={"","Xbox 360","Xbox One/Series","DualShock 4","DualSense","Switch Pro","HID gamepad"};
const char* gpad_typename(int t){ return t>=0&&t<=GP_HID?gp_tn[t]:""; }
int gpad_read_ev(int i,u32* pos,struct gpev* out,int max,int* dropped){
    if(!gpad_valid(i))return -1; struct gpad* p=&g_gp[i]; u32 w=p->qw; int n=0;
    if(w-*pos>GP_Q){ *pos=w-GP_Q+1; if(dropped)*dropped=1; }
    while(n<max&&*pos!=w){ out[n++]=p->q[*pos%GP_Q]; (*pos)++; }
    return n; }
u32 gpad_qpos(int i){ return gpad_valid(i)?g_gp[i].qw:0; }
int gpad_absinfo(int code,int* lo,int* hi,int* flat){
    if(code==0||code==1||code==3||code==4){ *lo=-32768; *hi=32767; *flat=128; return 1; }
    if(code==2||code==5){ *lo=0; *hi=255; *flat=0; return 1; }
    if(code==0x10||code==0x11){ *lo=-1; *hi=1; *flat=0; return 1; }
    return 0; }
int gpad_absval(int i,int code){
    if(!gpad_valid(i))return 0; struct gpst* s=&g_gp[i].st;
    switch(code){ case 0: return s->lx; case 1: return s->ly; case 2: return s->lt; case 3: return s->rx; case 4: return s->ry; case 5: return s->rt; case 0x10: return gp_hx(s->btn); case 0x11: return gp_hy(s->btn); }
    return 0; }
int gpad_keyval(int i,int code){ if(!gpad_valid(i))return 0; for(int b=0;b<GPB_N_;b++) if(gp_keys[b]==code)return (g_gp[i].st.btn>>b)&1; return 0; }
static char* gp_sc(char* p,const char* s){ while(*s)*p++=*s++; return p; }
static char* gp_ud(char* p,u32 v){ char t[12]; int n=0; do{ t[n++]=(char)('0'+v%10); v/=10; }while(v); while(n)*p++=t[--n]; return p; }
static char* gp_sd(char* p,int v){ if(v<0){ *p++='-'; v=-v; } return gp_ud(p,(u32)v); }
int gpad_info(char* out,int cap){
    if(cap<=0)return 0; char b[1024],*p=b; int any=0;
    for(int i=0;i<GP_MAX;i++){ struct gpad* d=&g_gp[i]; if(!d->used)continue; if(any)*p++='\n'; any=1;
        p=gp_sc(p,"js"); p=gp_ud(p,(u32)i); p=gp_sc(p,": "); p=gp_sc(p,d->name[0]?d->name:gpad_typename(d->type)); p=gp_sc(p," ("); p=gp_sc(p,gpad_typename(d->type)); p=gp_sc(p,d->bus==5?", Bluetooth)":", USB)");
        p=gp_sc(p," player "); p=gp_ud(p,(u32)d->player);
        if(d->bat>=0){ p=gp_sc(p," battery "); p=gp_ud(p,(u32)d->bat); p=gp_sc(p,d->chg?"% charging":"%"); }
        p=gp_sc(p,"\n  buttons "); for(int k=0;k<GPB_N_;k++)*p++=((d->st.btn>>k)&1)?'1':'0';
        p=gp_sc(p," L "); p=gp_sd(p,d->st.lx); *p++=','; p=gp_sd(p,d->st.ly); p=gp_sc(p," R "); p=gp_sd(p,d->st.rx); *p++=','; p=gp_sd(p,d->st.ry);
        p=gp_sc(p," LT "); p=gp_ud(p,(u32)d->st.lt); p=gp_sc(p," RT "); p=gp_ud(p,(u32)d->st.rt);
        if(d->st.tp){ p=gp_sc(p," touch "); p=gp_ud(p,(u32)d->st.tpx); *p++=','; p=gp_ud(p,(u32)d->st.tpy); }
        p=gp_sc(p," reports "); p=gp_ud(p,d->nrep); if(p>b+sizeof b-200)break; }
    if(!any)p=gp_sc(p,"no game controller");
    *p=0; int i=0; for(;b[i]&&i<cap-1;i++)out[i]=b[i]; out[i]=0; return any; }
