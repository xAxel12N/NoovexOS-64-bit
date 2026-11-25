#define HPP_RMAX 4
#define HPP_DMAX 6
#define HPP_SWID 0x0A
#define HPP_TMO 50
#define HPP_BATT_EVERY 30000
#define HPP_PAIR_TMO 9000
typedef struct { u8 on, kind, proto, ver, bat, bst, mult, fb, fu, fw, step, tries, hires, named; u16 wpid; char name[24]; u64 bat_t; } hpp_dev_t;
typedef struct { u8 used, e, own, iface, rep, bolt, rstep, rtries, wdev, wsub, waddr, wlen, pair, ok; u16 pid; u64 due; int acc; u8 wmsg[20]; hpp_dev_t d[HPP_DMAX+1]; u8 rq, pst, pkind, pauth, paddr[6], perr, pnew, ppress, pcs, pgot, uslot, ures, pcan; u16 pcnt; u64 pdue; char pname[16], pkey[8]; } hpp_rcv_t;
static hpp_rcv_t hpp_r[HPP_RMAX];
static int hpp_vendor_iface(u16 vid,const u8* rd,int n){
    if(vid!=0x046D||!rd||n<6)return 0; int vp=0, m=0;
    for(int i=0;i+2<n;i++){ if(rd[i]==0x06&&rd[i+1]==0x00&&rd[i+2]==0xFF)vp=1; if(vp&&rd[i]==0x85){ if(rd[i+1]==0x10)m|=1; else if(rd[i+1]==0x11)m|=2; } }
    return vp?m:0; }
static int hpp_is_bolt(u16 pid){ return pid==0xC548; }
static int hpp_receiver_pid(u16 pid){ static const u16 P[]={0xC52B,0xC532,0xC548,0xC52F,0xC534,0xC539,0xC53A,0xC53F,0xC547,0xC531,0xC517}; for(unsigned i=0;i<sizeof P/sizeof P[0];i++) if(P[i]==pid)return 1; return 0; }
static hpp_rcv_t* hpp_new(int e,int own,u16 pid,u8 iface,int rep){
    if(!hpp_receiver_pid(pid)||!rep)return 0; hpp_rcv_t* R=0;
    for(int i=0;i<HPP_RMAX;i++) if(hpp_r[i].used&&hpp_r[i].e==e)R=&hpp_r[i];
    for(int i=0;i<HPP_RMAX&&!R;i++) if(!hpp_r[i].used)R=&hpp_r[i];
    if(!R)return 0; u8* z=(u8*)R; for(unsigned i=0;i<sizeof *R;i++)z[i]=0;
    R->used=1; R->e=(u8)e; R->own=(u8)own; R->pid=pid; R->iface=iface; R->rep=(u8)rep; R->bolt=(u8)hpp_is_bolt(pid); return R; }
static int hpp_msg(hpp_rcv_t* R,u8* o,u8 dev,u8 sub,u8 addr,u8 p0,u8 p1,u8 p2,u64 now){
    int n=(R->rep&1)?7:20; for(int i=0;i<20;i++)o[i]=0;
    o[0]=n==7?0x10:0x11; o[1]=dev; o[2]=sub; o[3]=addr; o[4]=p0; o[5]=p1; o[6]=p2;
    R->wdev=dev; R->wsub=sub; R->waddr=addr; R->wlen=(u8)n; R->due=now+HPP_TMO; for(int i=0;i<20;i++)R->wmsg[i]=o[i]; return n; }
static int hpp_msgl(hpp_rcv_t* R,u8* o,u8 sub,u8 addr,const u8* p,int pn,u64 now){
    if(!(R->rep&2))return 0; for(int i=0;i<20;i++)o[i]=0; o[0]=0x11; o[1]=0xFF; o[2]=sub; o[3]=addr; for(int i=0;i<pn&&i<16;i++)o[4+i]=p[i];
    R->wdev=0xFF; R->wsub=sub; R->waddr=addr; R->wlen=20; R->due=now+HPP_TMO; for(int i=0;i<20;i++)R->wmsg[i]=o[i]; return 20; }
static int hpp_nidx(const hpp_rcv_t* R,u8 v){ return R->bolt?(v&0x0F):(v&0x0F)+1; }
static void hpp_pair_start(hpp_rcv_t* R,u64 now){ R->pair=1; R->pst=1; R->perr=0; R->pnew=0; R->pcs=0; R->pgot=0; R->ppress=0; R->pcan=0; R->pname[0]=0; R->pkey[0]=0; R->pdue=now+HPP_PAIR_TMO; }
static void hpp_rdone(hpp_rcv_t* R,int ok,u8 err){
    int q=R->rq; R->rq=0;
    if(q==1){ R->pair=0; if(!ok){ R->pst=6; R->perr=err; } else if(!R->bolt&&R->pst==1)R->pst=3; }
    else if(q==3&&!ok&&R->pst>=1&&R->pst<=4){ R->pst=6; R->perr=err; }
    else if(q==4&&ok&&R->ures>=1&&R->ures<=HPP_DMAX){ hpp_dev_t* D=&R->d[R->ures]; u8* z=(u8*)D; for(unsigned k=0;k<sizeof *D;k++)z[k]=0; }
    if(q==4)R->ures=0; }
static int hpp_dev_req(hpp_rcv_t* R,int i,u8* o,u64 now){
    hpp_dev_t* D=&R->d[i];
    switch(D->step){
    case 1: if(D->named){ D->step=2; return hpp_dev_req(R,i,o,now); } return R->bolt?hpp_msg(R,o,0xFF,0x83,0xB5,(u8)(0x60+i),0x01,0,now):hpp_msg(R,o,0xFF,0x83,0xB5,(u8)(0x40+i-1),0,0,now);
    case 2: return hpp_msg(R,o,(u8)i,0x00,0x10|HPP_SWID,0,0,0xA5,now);
    case 3: return hpp_msg(R,o,(u8)i,0x00,HPP_SWID,0x10,0x00,0,now);
    case 4: return hpp_msg(R,o,(u8)i,0x00,HPP_SWID,0x10,0x04,0,now);
    case 5: return hpp_msg(R,o,(u8)i,0x00,HPP_SWID,0x21,0x21,0,now);
    case 6: if(D->ver<2) return hpp_msg(R,o,(u8)i,0x81,0x0D,0,0,0,now);
        if(D->fu) return hpp_msg(R,o,(u8)i,D->fu,0x10|HPP_SWID,0,0,0,now);
        if(D->fb) return hpp_msg(R,o,(u8)i,D->fb,HPP_SWID,0,0,0,now);
        D->step=8; return hpp_dev_req(R,i,o,now);
    case 7: return hpp_msg(R,o,(u8)i,0x81,0x07,0,0,0,now);
    case 8: if(D->ver<2||!D->fw){ D->step=10; return 0; } return hpp_msg(R,o,(u8)i,D->fw,HPP_SWID,0,0,0,now);
    case 9: return hpp_msg(R,o,(u8)i,D->fw,0x30|HPP_SWID,0x02,0,0,now);
    } return 0; }
static void hpp_dev_next(hpp_rcv_t* R,int i,int ok,const u8* p){
    hpp_dev_t* D=&R->d[i]; int s=D->step; D->tries=0;
    switch(s){
    case 1: if(ok&&!R->bolt&&p[0]==(u8)(0x40+i-1)){ int L=p[1]; if(L>14)L=14; for(int k=0;k<L;k++){ u8 c=p[2+k]; D->name[k]=(char)((c<32||c>126)?'?':c); } D->name[L]=0; }
        if(ok&&R->bolt&&p[0]==(u8)(0x60+i)&&p[1]==0x01){ int L=p[2]; if(L>13)L=13; for(int k=0;k<L;k++){ u8 c=p[3+k]; D->name[k]=(char)((c<32||c>126)?'?':c); } D->name[L]=0; } D->named=1; D->step=2; return;
    case 2: if(!ok){ if(p[1]==0x01){ D->ver=1; D->step=6; } else { D->on=0; D->step=0; } return; } D->ver=p[0]?p[0]:2; D->step=3; return;
    case 3: D->fb=ok?p[0]:0; D->step=4; return;
    case 4: D->fu=ok?p[0]:0; D->step=5; return;
    case 5: D->fw=ok?p[0]:0; D->step=6; return;
    case 6: if(ok){ if(D->ver<2){ if(p[0]<=100){ D->bat=p[0]?p[0]:1; D->bst=0; } }
            else if(D->fu){ D->bat=p[0]>100?100:p[0]; D->bst=(u8)(p[2]==1||p[2]==2?1:p[2]==3?2:0); if(!D->bat&&p[1]){ D->bat=(p[1]&8)?90:(p[1]&4)?50:(p[1]&2)?20:5; } }
            else { D->bat=p[0]>100?100:p[0]; D->bst=(u8)(p[2]==1||p[2]==4?1:p[2]==3?2:0); }
            D->step=8; return; }
        D->step=D->ver<2?7:8; return;
    case 7: if(ok){ int l=p[0]; D->bat=(u8)(l>=7?90:l>=5?50:l>=3?20:l>=1?5:0); } D->step=8; return;
    case 8: if(ok){ D->mult=p[0]>=2&&p[0]<=64?p[0]:0; } D->step=D->mult?9:10; return;
    case 9: if(ok)D->hires=1; D->step=10; return;
    } }
static void hpp_on(hpp_rcv_t* R,int i,int on,u8 kind,u16 wpid,u8 proto){
    hpp_dev_t* D=&R->d[i]; if(on&&!D->on){ D->step=1; D->tries=0; D->hires=0; }
    if(!on){ D->hires=0; if(D->step&&D->step<10)D->step=0; }
    D->on=(u8)(on!=0); if(kind)D->kind=kind; if(wpid)D->wpid=wpid; if(proto)D->proto=proto; }
static void hpp_prx(hpp_rcv_t* R,const u8* b,int n){
    int s=b[2], a=b[3];
    if(s==0x4A&&!R->bolt){ if(a&1){ if(R->pst==1)R->pst=3; return; } if(R->pst>=1&&R->pst<=3){ if(R->pnew)R->pst=5; else { R->pst=6; R->perr=b[4]?b[4]:0xFC; } } return; }
    if(!R->bolt)return;
    if(s==0x53){ if(R->pst!=1)return; if(!a&&!b[4]){ R->pcs=0; R->pgot=0; return; } if(a||b[4]){ R->pst=6; R->perr=b[4]?b[4]:0xFC; } return; }
    if(s==0x4F){ if(R->pst!=1||n<20)return; u16 c=(u16)(a|(b[4]<<8)); if(!R->pcs){ R->pcs=1; R->pcnt=c; } else if(c!=R->pcnt)return;
        if(b[5]==0){ R->pkind=b[7]; for(int k=0;k<6;k++)R->paddr[k]=b[10+k]; R->pauth=b[18]; R->pgot|=1; }
        else if(b[5]==1){ int L=b[6]; if(L>13)L=13; for(int k=0;k<L;k++){ u8 c2=b[7+k]; R->pname[k]=(char)((c2<32||c2>126)?'?':c2); } R->pname[L]=0; R->pgot|=2; }
        if(R->pgot==3)R->pst=2; return; }
    if(s==0x54){ R->pkey[0]=0; if(!a){ if(b[4]&&R->pst>=1&&R->pst<=4){ R->pst=6; R->perr=b[4]; } else if(R->pst==2||R->pst==3)R->pst=3; return; }
        if(R->pst<1||R->pst>4)return; if(a==2&&!b[4]){ R->pst=5; R->pnew=n>11?b[11]:0; } else { R->pst=6; R->perr=b[4]?b[4]:0xFB; } return; }
    if(s==0x4D){ if(R->pst!=3&&R->pst!=4)return; int k=0; for(;k<6;k++){ u8 c=b[4+k]; if(c<'0'||c>'9')break; R->pkey[k]=(char)c; } R->pkey[k]=0; if(k==6){ R->pst=4; R->ppress=0; } else R->pkey[0]=0; return; }
    if(s==0x4E&&R->pst==4){ if(R->ppress<255)R->ppress++; return; } }
static void hpp_rx(hpp_rcv_t* R,const u8* b,int n,u64 now){
    if(n<7||(b[0]!=0x10&&b[0]!=0x11))return; int dev=b[1];
    int waiting=R->wlen!=0, mine=waiting&&dev==R->wdev;
    if(mine&&b[2]==R->wsub&&(R->wsub>=0x80?b[3]==R->waddr:(b[3]&0xF0)==(R->waddr&0xF0)&&(b[3]&0x0F)==HPP_SWID)){ R->wlen=0; R->ok=1; R->rtries=0;
        if(dev==0xFF){ if(R->rq){ hpp_rdone(R,1,0); return; } if(R->rstep&&R->rstep<3)R->rstep++; if(R->wsub==0x83&&R->waddr==0xB5){ int i=hpp_nidx(R,b[4]); if(i>=1&&i<=HPP_DMAX)hpp_dev_next(R,i,1,b+4); } return; }
        if(dev>=1&&dev<=HPP_DMAX)hpp_dev_next(R,dev,1,b+4); return; }
    if(mine&&(b[2]==0x8F||b[2]==0xFF)&&b[3]==R->wsub&&b[4]==R->waddr){ R->wlen=0; R->ok=1; R->rtries=0;
        if(dev==0xFF){ if(R->rq){ hpp_rdone(R,0,b[5]?b[5]:0xFE); return; } if(R->rstep&&R->rstep<3)R->rstep++; if(R->wsub==0x83&&R->waddr==0xB5){ int i=hpp_nidx(R,R->wmsg[4]); if(i>=1&&i<=HPP_DMAX)hpp_dev_next(R,i,0,b+4); } return; }
        if(dev>=1&&dev<=HPP_DMAX)hpp_dev_next(R,dev,0,b+4); return; }
    if(dev==0xFF&&b[2]>=0x4A&&b[2]<=0x54){ hpp_prx(R,b,n); return; }
    if(b[2]==0x41&&dev>=1&&dev<=HPP_DMAX){ R->ok=1; if(!R->bolt&&R->pst==3&&!(b[4]&0x40)&&!R->d[dev].kind&&!R->d[dev].on)R->pnew=(u8)dev; hpp_on(R,dev,!(b[4]&0x40),(u8)(b[4]&0x0F),(u16)(b[5]|(b[6]<<8)),b[3]); return; }
    if(b[2]==0x40&&dev>=1&&dev<=HPP_DMAX){ hpp_dev_t* D=&R->d[dev]; u8* z=(u8*)D; for(unsigned k=0;k<sizeof *D;k++)z[k]=0; if(R->wlen&&R->wdev==dev)R->wlen=0; return; }
    if(dev>=1&&dev<=HPP_DMAX&&(b[3]&0x0F)==0&&b[2]&&b[2]<0x40){ hpp_dev_t* D=&R->d[dev];
        if(D->fu&&b[2]==D->fu&&(b[3]>>4)==0){ D->bat=b[4]>100?100:b[4]; D->bst=(u8)(b[6]==1||b[6]==2?1:b[6]==3?2:0); D->bat_t=now; }
        else if(D->fb&&b[2]==D->fb&&(b[3]>>4)==0){ D->bat=b[4]>100?100:b[4]; D->bst=(u8)(b[6]==1||b[6]==4?1:b[6]==3?2:0); D->bat_t=now; }
        if(!D->on){ D->on=1; if(!D->step)D->step=1; } } }
static int hpp_step(hpp_rcv_t* R,u64 now,u8* o){
    if(!R->used)return 0;
    if(R->wlen){ if(now<R->due)return 0; if(++R->rtries<3){ R->due=now+HPP_TMO; for(int i=0;i<20;i++)o[i]=R->wmsg[i]; return R->wlen; }
        int dev=R->wdev; R->wlen=0; R->rtries=0;
        if(dev==0xFF){ if(R->rq)hpp_rdone(R,0,0xFE); else { if(R->rstep&&R->rstep<3)R->rstep++; if(R->wmsg[3]==0xB5){ int i=hpp_nidx(R,R->wmsg[4]); if(i>=1&&i<=HPP_DMAX)hpp_dev_next(R,i,0,R->wmsg); } } }
        else if(dev>=1&&dev<=HPP_DMAX){ hpp_dev_t* D=&R->d[dev]; if(D->step==2){ D->on=0; D->step=0; } else hpp_dev_next(R,dev,0,R->wmsg); }
        return 0; }
    if(R->rstep==0)R->rstep=1;
    if(R->rstep==1) return hpp_msg(R,o,0xFF,0x80,0x00,0x00,0x09,0x00,now);
    if(R->rstep==2) return hpp_msg(R,o,0xFF,0x80,0x02,0x02,0x00,0x00,now);
    if(R->pst>=1&&R->pst<=4&&now>=R->pdue){ R->pcan=(u8)(R->pst>=3?2:1); R->pst=6; R->perr=0xFD; R->pair=0; }
    if(R->pcan){ int c=R->pcan; R->pcan=0; R->rq=5; if(!R->bolt)return hpp_msg(R,o,0xFF,0x80,0xB2,0x02,0x00,0x00,now); if(c==2){ u8 q[1]={0x02}; int k=hpp_msgl(R,o,0x82,0xC1,q,1,now); if(k)return k; } return hpp_msg(R,o,0xFF,0x80,0xC0,0x00,0x02,0x00,now); }
    if(R->pair==1){ R->pair=2; R->rq=1; if(R->bolt){ if(!(R->rep&2)){ R->rq=0; R->pair=0; R->pst=6; R->perr=0xF9; return 0; } return hpp_msg(R,o,0xFF,0x80,0xC0,30,0x01,0x00,now); } return hpp_msg(R,o,0xFF,0x80,0xB2,0x01,0x00,60,now); }
    if(R->pst==2&&R->bolt){ u8 p[10]={0x01,0}; for(int k=0;k<6;k++)p[2+k]=R->paddr[k]; p[8]=R->pauth; p[9]=(u8)((R->pkind&0x0F)==1?20:10); R->pst=3; R->rq=3; int k=hpp_msgl(R,o,0x82,0xC1,p,10,now); if(k)return k; R->rq=0; R->pst=6; R->perr=0xF9; return 0; }
    if(R->uslot){ int u=R->uslot; R->uslot=0; R->ures=(u8)u; R->rq=4; if(R->bolt){ u8 p[2]={0x03,(u8)u}; int k=hpp_msgl(R,o,0x82,0xC1,p,2,now); if(k)return k; R->rq=0; R->ures=0; return 0; } return hpp_msg(R,o,0xFF,0x80,0xB2,0x03,(u8)u,0x00,now); }
    for(int i=1;i<=HPP_DMAX;i++){ hpp_dev_t* D=&R->d[i]; if(!D->on)continue;
        if(D->step==10&&D->bat_t&&now-D->bat_t>=HPP_BATT_EVERY){ D->step=6; }
        if(D->step>=1&&D->step<10){ int k=hpp_dev_req(R,i,o,now); if(k){ if(D->step==6)D->bat_t=now?now:1; return k; } } }
    return 0; }
static int hpp_scale(hpp_rcv_t* R,int wz){
    int m=0; for(int i=1;i<=HPP_DMAX;i++) if(R->d[i].hires&&R->d[i].mult>m)m=R->d[i].mult;
    if(m<2){ R->acc=0; return wz; }
    if((wz>0&&R->acc<0)||(wz<0&&R->acc>0))R->acc=0;
    R->acc+=wz; int out=R->acc/m; R->acc-=out*m; return out; }
static const char* hpp_kind_name(int k){ static const char* K[16]={"Device","Keyboard","Mouse","Numpad","Presenter","Device","Device","Remote","Trackball","Touchpad","Tablet","Gamepad","Joystick","Headset","Remote","Receiver"}; return K[k&15]; }
static int hpp_fmt(const hpp_rcv_t* R,int i,char* o,int n){
    const hpp_dev_t* D=&R->d[i]; int k=0;
    #define HPC(c) do{ if(k<n-1)o[k++]=(c); }while(0)
    const char* s=D->name[0]?D->name:hpp_kind_name(D->kind); while(*s)HPC(*s++);
    s=R->bolt?", Logi Bolt":R->pid==0xC52B||R->pid==0xC532?", Unifying":", Logitech receiver"; while(*s)HPC(*s++);
    if(!D->on){ s=", asleep"; while(*s)HPC(*s++); }
    if(D->bat){ s=", battery "; while(*s)HPC(*s++); int v=D->bat; if(v>=100)HPC('1'); if(v>=10)HPC((char)('0'+(v/10)%10)); HPC((char)('0'+v%10)); HPC('%'); if(D->bst==1){ s=" charging"; while(*s)HPC(*s++); } else if(D->bst==2){ s=" full"; while(*s)HPC(*s++); } }
    if(D->hires){ s=", smooth scroll"; while(*s)HPC(*s++); }
    #undef HPC
    if(n>0)o[k]=0; return k; }
static struct featguard hpp_fg={"Logitech receivers (HID++)","feat-hidpp.cfg",1,0,3,0,0};
static u8 hpp_gq;
static int hppx_on(void){ return hpp_fg.trying?hpp_fg.on&&!hpp_fg.dead:0; }
void hidpp_service(void){ fg_boot(&hpp_fg); u8 q=hpp_gq; hpp_gq=0; if(q&1)fg_begin(&hpp_fg); if(q&2)fg_ok(&hpp_fg); if(q&4)hpp_gq|=4; }
static int hppx_live(const hpp_rcv_t* R){ int e=R->e; return R->used&&e<XHID_MAX&&xhid_used[e]&&xhid_own[e]==R->own; }
static void hppx_attach(int e,int own,u16 vid,u16 pid,u8 iface,int rep){
    if(vid!=0x046D||!rep||!hppx_on())return; for(int i=0;i<HPP_RMAX;i++) if(hpp_r[i].used&&!hppx_live(&hpp_r[i]))hpp_r[i].used=0;
    hpp_rcv_t* R=hpp_new(e,own,pid,iface,rep); if(!R)return; hpp_gq|=1; klog(R->bolt?"usb: Logi Bolt receiver, asking for paired devices":"usb: Logitech receiver, asking for paired devices"); }
static int hpp_xfer(int e,const u8* b,int n){
    for(int i=0;i<HPP_RMAX;i++){ hpp_rcv_t* R=&hpp_r[i]; if(!R->used||R->e!=e)continue; if(!hppx_live(R)){ R->used=0; return 0; }
        if(n>=7&&(b[0]==0x10||b[0]==0x11)){ int was=R->ok; hpp_rx(R,b,n,g_ticks); if(R->ok&&!was)hpp_gq|=2; return 1; } return n>=1&&b[0]==0x20; }
    return 0; }
static int hpp_wheel(int e,int wz){
    if(!wz||e<0||e>=XHID_MAX)return wz;
    for(int i=0;i<HPP_RMAX;i++){ hpp_rcv_t* R=&hpp_r[i]; if(R->used&&R->own==xhid_own[e]&&hppx_live(R))return hpp_scale(R,wz); }
    return wz; }
static void hppx_poll(void){
    extern int g_pi_depth __attribute__((weak));
    if(xh_wdepth>0||(&g_pi_depth&&g_pi_depth>0)||!xh_irq_on())return;
    for(int i=0;i<HPP_RMAX;i++){ hpp_rcv_t* R=&hpp_r[i]; if(!R->used)continue; int e=R->e; if(!hppx_live(R)){ R->used=0; continue; } if(xhid_ctl[e]!=g_xcur)continue;
        u8 m[20]; int n=hpp_step(R,g_ticks,m); if(!n)continue;
        if(!R->ok&&R->rstep==1&&R->rtries==2){ hpp_gq|=4; }
        int d=R->own; int s_slot=cur_slot; u32 s_ep0=cur_ep0, s_db=cur_dbuf, s_enq=e0_enq, s_cyc=e0_cyc;
        cur_slot=xhid_slot[e]; cur_ep0=XHID_EP0RING(d); cur_dbuf=XHID_DATABUF(d); e0_enq=xhid_e0enq[d]; e0_cyc=xhid_e0cyc[d];
        { volatile u8* db=(volatile u8*)(u64)cur_dbuf; for(int k=0;k<n;k++)db[k]=m[k]; }
        xh_ctrl(0x21,0x09,(u16)(0x0200|m[0]),R->iface,(u16)n,0,0);
        xhid_e0enq[d]=e0_enq; xhid_e0cyc[d]=e0_cyc; cur_slot=s_slot; cur_ep0=s_ep0; cur_dbuf=s_db; e0_enq=s_enq; e0_cyc=s_cyc;
        return; } }
static int hpp_cat(char* o,int k,int n,const char* s){ while(s&&*s&&k<n-1)o[k++]=*s++; if(n>0)o[k]=0; return k; }
static const char* hpp_perr(const hpp_rcv_t* R){ switch(R->perr){ case 0x01: return "the device did not answer in time"; case 0x02: return R->bolt?"the device refused":"device not supported"; case 0x03: return "too many devices on this receiver"; case 0x06: return "the device took too long"; case 0xF9: return "this receiver interface cannot pair"; case 0xFA: return "cancelled"; case 0xFB: return "the receiver closed pairing"; case 0xFC: return "no device was found"; case 0xFD: return "timed out after 90 seconds"; case 0xFE: return "the receiver did not answer"; } return "the receiver refused"; }
static int hpp_pstate(const hpp_rcv_t* R,char* a,int an,char* b,int bn){
    int k=0, j=0; if(an>0)a[0]=0; if(bn>0)b[0]=0; const char* nm=R->pname[0]?R->pname:"the device";
    switch(R->pst){
    case 1: hpp_cat(a,0,an,R->bolt?"Searching for a Logi Bolt device":"Unifying receiver is open for pairing");
        hpp_cat(b,0,bn,R->bolt?"Switch the device on or hold its Easy-Switch / connect button until it blinks fast":"Switch the new device off and on again"); return 1;
    case 2: case 3: k=hpp_cat(a,0,an,"Connecting to "); hpp_cat(a,k,an,R->bolt?nm:"the new device"); hpp_cat(b,0,bn,R->bolt?"Waiting for the passkey":"Switch the new device off and on again"); return 1;
    case 4: if(R->pauth&1){ k=hpp_cat(a,0,an,"Type the passkey on "); hpp_cat(a,k,an,nm); j=hpp_cat(b,0,bn,"Type "); j=hpp_cat(b,j,bn,R->pkey); hpp_cat(b,j,bn," and then press Enter"); return 2; }
        k=hpp_cat(a,0,an,"Click on "); k=hpp_cat(a,k,an,nm); hpp_cat(a,k,an," (R = right button, L = left), then both together");
        { unsigned v=0; for(int q=0;R->pkey[q];q++)v=v*10+(unsigned)(R->pkey[q]-'0'); for(int q=9;q>=0;q--){ j=hpp_cat(b,j,bn,(v>>q)&1?"R":"L"); if(q)j=hpp_cat(b,j,bn," "); }
          if(R->ppress){ char c[8]; int m=0, x=R->ppress; c[m++]=' '; c[m++]='('; if(x>=100)c[m++]=(char)('0'+x/100); if(x>=10)c[m++]=(char)('0'+(x/10)%10); c[m++]=(char)('0'+x%10); c[m]=0; j=hpp_cat(b,j,bn,c); hpp_cat(b,j,bn," pressed)"); } } return 2;
    case 5: k=hpp_cat(a,0,an,"Paired: "); { int d=R->pnew; const char* t=R->pname[0]?R->pname:d>=1&&d<=HPP_DMAX&&R->d[d].name[0]?R->d[d].name:"new device"; k=hpp_cat(a,k,an,t); }
        hpp_cat(b,0,bn,"It shows up in the list above when it connects"); return 3;
    case 6: hpp_cat(a,0,an,"Pairing failed"); hpp_cat(b,0,bn,hpp_perr(R)); return 4; }
    return 0; }
static hpp_rcv_t* hppx_pairable(void){ for(int i=0;i<HPP_RMAX;i++){ hpp_rcv_t* R=&hpp_r[i]; if(R->used&&hppx_live(R)&&R->bolt&&(R->rep&2))return R; } for(int i=0;i<HPP_RMAX;i++){ hpp_rcv_t* R=&hpp_r[i]; if(R->used&&hppx_live(R)&&!R->bolt)return R; } return 0; }
int hidpp_pair(void){ hpp_rcv_t* R=hppx_pairable(); if(!R)return -1; if(R->pst>=1&&R->pst<=4)return 0; hpp_pair_start(R,g_ticks); klog(R->bolt?"usb: Logi Bolt pairing started":"usb: Unifying pairing lock opening"); return 0; }
int hidpp_pair_cancel(void){ int r=-1; for(int i=0;i<HPP_RMAX;i++){ hpp_rcv_t* R=&hpp_r[i]; if(!R->used)continue; if(R->pst>=1&&R->pst<=4){ R->pcan=(u8)(R->bolt&&R->pst>=3?2:1); R->pst=6; R->perr=0xFA; R->pair=0; r=0; } else if(R->pst){ R->pst=0; r=0; } } return r; }
int hidpp_pair_state(char* a,int an,char* b,int bn){ if(an>0)a[0]=0; if(bn>0)b[0]=0; for(int i=0;i<HPP_RMAX;i++){ hpp_rcv_t* R=&hpp_r[i]; if(R->used&&hppx_live(R)&&R->pst)return hpp_pstate(R,a,an,b,bn); } hpp_rcv_t* R=hppx_pairable(); if(!R)return -1;
    hpp_cat(a,0,an,R->bolt?"Logi Bolt receiver":"Logitech Unifying receiver"); hpp_cat(b,0,bn,R->bolt?"Pair a Bolt mouse or keyboard; a passkey is shown here":"Pair a Unifying mouse or keyboard"); return 0; }
int hidpp_unpair(int idx){ int c=0; for(int i=0;i<HPP_RMAX;i++){ hpp_rcv_t* R=&hpp_r[i]; if(!R->used||!hppx_live(R))continue;
        for(int j=1;j<=HPP_DMAX;j++){ hpp_dev_t* D=&R->d[j]; if(!D->kind&&!D->on)continue; if(c++!=idx)continue; if(R->bolt&&!(R->rep&2))return -1; R->uslot=(u8)j; klog("usb: Logitech unpair requested"); return 0; } }
    return -1; }
int hidpp_info(int idx,char* kind,int kn,char* txt,int tn){
    if(hpp_gq)hidpp_service();
    int c=0; for(int i=0;i<HPP_RMAX;i++){ hpp_rcv_t* R=&hpp_r[i]; if(!R->used||!hppx_live(R))continue;
        for(int j=1;j<=HPP_DMAX;j++){ hpp_dev_t* D=&R->d[j]; if(!D->kind&&!D->on)continue; if(c++!=idx)continue;
            const char* s=hpp_kind_name(D->kind); int k=0; while(s[k]&&k<kn-1){ kind[k]=s[k]; k++; } if(kn>0)kind[k]=0; hpp_fmt(R,j,txt,tn); return D->on?1:2; } }
    return 0; }
