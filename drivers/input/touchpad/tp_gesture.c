#include "include/nvx/base.h"
#include "include/nvx/arch/x86_64/time/hrtimer.h"
#include "include/nvx/drivers/input/touchpad/tp_hid.h"
#include "include/nvx/drivers/input/touchpad/tp_gesture.h"

int g_tp_tap=1, g_tp_speed=5, g_tp_natural=1, g_tp_dwt=1, g_tp_zones=1;
volatile u32 g_kbd_last_tick; extern volatile u64 g_ticks;

#define TP_TAP_MS 230
#define TP_TAP_UM 3000
#define TP_DRAG_GAP_MS 260
#define TP_DRAG_UM 1500
#define TP_ZONE_UM 12000
#define TP_EDGE_UM 3000
#define TP_ZONE_LEAVE_UM 6000
#define TP_DWT_MS 400

static int tpe_isqrt(long long v){ if(v<=0)return 0; long long r=0, b=1LL<<40; while(b>v)b>>=2; while(b){ if(v>=r+b){ v-=r+b; r=(r>>1)+b; } else r>>=1; b>>=2; } return (int)r; }
static int tpe_abs(int v){ return v<0?-v:v; }
static u32 tpe_now(void){ return (u32)(hr_ns()/1000000ULL); }
void tpe_init(tpeng* E,int xmin,int xmax,int ymin,int ymax,u32 xum,u32 yum){
    for(u32 i=0;i<sizeof(*E);i++)((u8*)E)[i]=0;
    E->xmin=xmin; E->xmax=xmax; E->ymin=ymin; E->ymax=ymax;
    if(!xum)xum=100000; if(!yum)yum=(u32)((long long)xum*(ymax-ymin)/((xmax-xmin)?(xmax-xmin):1)); if(!yum)yum=60000;
    E->upmm_x=(int)((long long)(xmax-xmin)*1000/xum); if(E->upmm_x<1)E->upmm_x=1;
    E->upmm_y=(int)((long long)(ymax-ymin)*1000/yum); if(E->upmm_y<1)E->upmm_y=1;
    E->natural=1; }
static int tpe_um_x(tpeng* E,int u){ return (int)((long long)u*1000/E->upmm_x); }
static int tpe_um_y(tpeng* E,int u){ return (int)((long long)u*1000/E->upmm_y); }
static int tpe_in_zone(tpeng* E,const tpcontact* p){ return g_tp_zones&&tpe_um_y(E,E->ymax-p->y)<=TP_ZONE_UM; }
static int tpe_zone_side(tpeng* E,const tpcontact* p){ return (p->x-E->xmin)*2>=(E->xmax-E->xmin)?1:0; }
static tptrack* tpe_track(tpeng* E,const tpcontact* p,int* isnew){
    for(int i=0;i<E->ntr;i++) if(E->tr[i].id==p->id){ *isnew=0; return &E->tr[i]; }
    *isnew=1; if(E->ntr>=TPF_MAX)return 0; tptrack* t=&E->tr[E->ntr++]; t->id=p->id; t->sx=p->x; t->sy=p->y; t->zone=tpe_in_zone(E,p);
    t->edge=tpe_um_x(E,p->x-E->xmin)<=TP_EDGE_UM||tpe_um_x(E,E->xmax-p->x)<=TP_EDGE_UM; return t; }
static int tpe_gain(int v_um_per_ms){
    int lo=25, hi=250, glo=4000, ghi=16000, g;
    if(v_um_per_ms<=lo)g=glo; else if(v_um_per_ms>=hi)g=ghi; else g=glo+(int)((long long)(v_um_per_ms-lo)*(ghi-glo)/(hi-lo));
    int s=g_tp_speed<1?1:g_tp_speed>10?10:g_tp_speed; return g*(50+s*10)/100; }
void tpe_frame(tpeng* E,const tpcontact* call,int nall,int button,u32 ms_in,tpout* o){
    (void)ms_in; u32 ms=tpe_now();
    for(u32 i=0;i<sizeof(*o);i++)((u8*)o)[i]=0;
    int L=E->phys_btn&&E->phys_which==0, R=E->phys_btn&&E->phys_which==1, M=0;
    if(E->drag)L=1;
    if(E->pulse){ E->pulse=0; o->btn_changed=1; }
    for(int i=0;i<E->ntr;i++)E->tr[i].seen=0;
    tpcontact c[TPF_MAX]; int n=0, nz=0; tpcontact zc[TPF_MAX];
    for(int i=0;i<nall&&i<TPF_MAX;i++){ int nw=0; tptrack* t=tpe_track(E,&call[i],&nw); if(t)t->seen=1;
        if(t&&t->zone){ int mv=tpe_abs(tpe_um_x(E,call[i].x-t->sx))+tpe_abs(tpe_um_y(E,call[i].y-t->sy)); if(mv>TP_ZONE_LEAVE_UM||!tpe_in_zone(E,&call[i]))t->zone=0; }
        if(t&&t->zone)zc[nz++]=call[i]; else c[n++]=call[i]; }
    for(int i=0;i<E->ntr;){ if(!E->tr[i].seen){ E->tr[i]=E->tr[E->ntr-1]; E->ntr--; } else i++; }
    int zside=-1; if(nz>0){ const tpcontact* low=&zc[0]; for(int i=1;i<nz;i++) if(zc[i].y>low->y)low=&zc[i]; zside=tpe_zone_side(E,low); }
    else if(nall==1&&tpe_in_zone(E,&call[0]))zside=tpe_zone_side(E,&call[0]);
    if(n==0&&nz>0){ for(int i=0;i<nz;i++)c[n++]=zc[i]; nz=0; }
    if(E->palm){ E->palm_sess=1; E->palms++; }
    if(button&&!E->phys_btn){ E->phys_btn=1; E->btn_used=1;
        int which=0;
        if(g_tp_zones&&zside>=0){ which=zside; E->zones_used++; }
        else if(n>=2)which=1;
        E->phys_which=which; L=which==0; R=which==1; o->btn_changed=1; }
    else if(!button&&E->phys_btn){ E->phys_btn=0; L=E->drag; R=0; o->btn_changed=1; }
    if(n==0){
        if(E->active){ u32 dt=ms-E->t0;
            int tap=g_tp_tap&&!E->btn_used&&!E->palm_sess&&!E->dwt_sess&&dt<=TP_TAP_MS&&E->moved_um<=TP_TAP_UM;
            if(E->drag){ E->drag=0; L=0; o->btn_changed=1;
                if(!E->drag_moved&&tap){ E->pulse=1; L=1; E->tap_l++; } E->tap_armed=0; }
            else if(tap){ if(E->maxf==1){ L=1; E->tap_l++; E->tap_armed=1; E->tap_ms=ms; } else if(E->maxf==2){ R=1; E->tap_r++; E->tap_armed=0; } else if(E->maxf>=3){ M=1; E->tap_m++; E->tap_armed=0; } if(L||R||M){ E->pulse=1; o->btn_changed=1; } }
            else E->tap_armed=0; }
        E->active=0; E->n_prev=0; E->fx=E->fy=0; E->sacc_v=E->sacc_h=0; E->swfired=0; E->swx=E->swy=0; E->zacc=0; E->palm_sess=0; E->dwt_sess=0; E->v_prev=0;
        if(!E->phys_btn)E->btn_used=0;
        o->btn_l=L; o->btn_r=R; o->btn_m=M; return; }
    if(!E->active){ E->active=1; E->t0=ms; E->maxf=n; E->moved_um=0; E->swx=E->swy=0; E->swfired=0; E->sacc_v=E->sacc_h=0; E->zacc=0; E->d0=0; E->last_ms=ms; E->v_prev=0;
        E->dwt_sess=g_tp_dwt&&g_kbd_last_tick&&(((u32)g_ticks-g_kbd_last_tick)*10u)<TP_DWT_MS;
        if(E->dwt_sess)E->dwt_skips++;
        if(E->tap_armed&&n==1&&ms-E->tap_ms<=TP_DRAG_GAP_MS&&!E->dwt_sess){ E->drag=1; E->drag_moved=0; E->drags++; L=1; o->btn_changed=1; }
        E->tap_armed=0; }
    if(n>E->maxf)E->maxf=n;
    int ddx=0, ddy=0, m=0, best=-1, bestmag=-1;
    if(n==E->n_prev||E->phys_btn){
        for(int i=0;i<n;i++) for(int j=0;j<E->n_prev;j++) if(c[i].id==E->prev[j].id){ int ex=c[i].x-E->prev[j].x, ey=c[i].y-E->prev[j].y; ddx+=ex; ddy+=ey; m++; int mag=tpe_abs(ex)+tpe_abs(ey); if(mag>bestmag){ bestmag=mag; best=i; } } }
    if(m&&E->phys_btn&&n>=2){ ddx=0; ddy=0; for(int j=0;j<E->n_prev;j++) if(c[best].id==E->prev[j].id){ ddx=c[best].x-E->prev[j].x; ddy=c[best].y-E->prev[j].y; } m=1; }
    int dxum=0, dyum=0; if(m){ dxum=(int)((long long)ddx*1000/m/E->upmm_x); dyum=(int)((long long)ddy*1000/m/E->upmm_y); }
    E->moved_um+=tpe_abs(dxum)+tpe_abs(dyum);
    if(E->drag&&E->moved_um>TP_DRAG_UM)E->drag_moved=1;
    u32 dt=ms-E->last_ms; E->last_ms=ms; if(dt<3)dt=3; if(dt>40)dt=10;
    int typing=g_tp_dwt&&g_kbd_last_tick&&(((u32)g_ticks-g_kbd_last_tick)*10u)<TP_DWT_MS;
    int mode=n; if(E->phys_btn||E->drag)mode=1;
    if(mode==1&&m&&!(typing&&!E->phys_btn&&!E->drag)){
        int sp=tpe_abs(dxum)+tpe_abs(dyum); int v=sp/(int)dt; int vs=(v+E->v_prev)/2; E->v_prev=v;
        int gain=tpe_gain(vs);
        E->fx+=dxum*gain/1000; E->fy+=dyum*gain/1000;
        o->dx=E->fx/1000; o->dy=E->fy/1000; E->fx-=o->dx*1000; E->fy-=o->dy*1000; }
    else if(mode==2&&n==2&&!typing){
        int ax=tpe_um_x(E,c[0].x-c[1].x), ay=tpe_um_y(E,c[0].y-c[1].y);
        int dist=tpe_isqrt((long long)ax*ax+(long long)ay*ay);
        if(!E->d0||E->n_prev!=2)E->d0=dist;
        int dd=dist-E->d0;
        if((dd>8000||dd<-8000)&&tpe_abs(dd)>(E->moved_um)/2){ o->gesture=dd>0?GES_ZOOM_IN:GES_ZOOM_OUT; E->d0=dist; }
        else if(m){ int nat=g_tp_natural?E->natural:0; int dv=nat?dyum:-dyum, dh=nat?-dxum:dxum;
            int sp=tpe_abs(dxum)+tpe_abs(dyum); int v=sp/(int)dt; int boost=v>120?2:1;
            E->sacc_v+=dv*boost; E->sacc_h+=dh*boost;
            while(E->sacc_v>=1800){ o->scroll_v++; E->sacc_v-=1800; } while(E->sacc_v<=-1800){ o->scroll_v--; E->sacc_v+=1800; }
            while(E->sacc_h>=1800){ o->scroll_h++; E->sacc_h-=1800; } while(E->sacc_h<=-1800){ o->scroll_h--; E->sacc_h+=1800; } } }
    else if(mode>=3&&m&&!E->swfired&&!typing){
        E->swx+=dxum; E->swy+=dyum;
        int ax=tpe_abs(E->swx), ay=tpe_abs(E->swy);
        if(ax>=15000||ay>=15000){ if(ay>=ax)o->gesture=E->swy<0?GES_SWIPE_UP:GES_SWIPE_DOWN; else o->gesture=E->swx<0?GES_SWIPE_LEFT:GES_SWIPE_RIGHT; E->swfired=1; } }
    if(o->scroll_v||o->scroll_h||o->gesture)E->btn_used=1;
    E->n_prev=n; for(int i=0;i<n&&i<TPF_MAX;i++)E->prev[i]=c[i];
    o->btn_l=L; o->btn_r=R; o->btn_m=M; }
