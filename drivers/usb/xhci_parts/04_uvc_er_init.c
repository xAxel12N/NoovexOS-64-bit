static int uvc_er_done=0;
static void uvc_er_init(void){
    if(uvc_er_done){ int n=0; for(int g=0;g<8192;g++){ volatile u32* e=uvc_er+uvc_ev1*4; if((e[3]&1u)!=uvc_ev1c)break; uvc_ev1++; if(uvc_ev1>=4096){ uvc_ev1=0; uvc_ev1c^=1; } n++; }
        if(n){ u64 rt=xhci_base+xhci_rtoff+0x40; XMW(rt+0x18,((u32)(u64)(uvc_er+uvc_ev1*4))|(1u<<3)); XMW(rt+0x1C,0); } return; }
    uvc_er_done=1;
    for(int i=0;i<4096*4;i++)uvc_er[i]=0; uvc_erst[0]=(u32)(u64)uvc_er; uvc_erst[1]=0; uvc_erst[2]=4096; uvc_erst[3]=0; uvc_ev1=0; uvc_ev1c=1;
    u64 rt=xhci_base+xhci_rtoff+0x40; XMW(rt+0x00,0); XMW(rt+0x08,1); XMW(rt+0x18,(u32)(u64)uvc_er); XMW(rt+0x1C,0); XMW(rt+0x10,(u32)(u64)uvc_erst); XMW(rt+0x14,0); }
static void uvc_ring_init(void){
    u32 n=(u32)cam.ntd; for(u32 i=0;i<(n+1)*4;i++)uvc_ring[i]=0; volatile u32* lk=uvc_ring+n*4; lk[0]=(u32)(u64)uvc_ring; lk[1]=0; lk[2]=0; lk[3]=(6u<<10)|(1u<<1)|1u;
    uvc_enq=0; uvc_deq=0; uvc_cyc=1; uvc_out=0; }
void uvc_stop(void){
    if(!cam.ok||uvc_state!=2){ return; }
    uvc_state=1; if(cam.isbulk)uvc_req(0x02,0x01,0,(u16)cam.bep,0,0,0); else uvc_req(0x01,0x0B,0,(u16)cam.vs_if,0,0,0);
    if(cam.epon){ uvc_cfg_ep(0,0); } cam.isbulk=0; cam.inpay=0;
    uvc_fps10=0; uvc_m("Camera stopped"); }
int uvc_start(int hd){
    if(!cam.ok){ uvc_m("No camera found"); return -1; }
    if(uvc_state==2)uvc_stop();
    if(cam.vs_if<0||!cam.nfr){ uvc_m("The camera has no supported picture format (needs YUY2, UYVY, NV12, I420, YV12, grey or MJPEG)"); return -1; }
    int bulk=!cam.nalt&&cam.bulk&&cam.bep; if(!cam.nalt&&!bulk){ uvc_m("The camera has no video stream endpoint"); return -1; }
    int sv=g_xcur; xhc_select(cam.ctl); uvc_maxintr=(int)((XMR(xhci_base+0x04)>>8)&0x7FF); xhc_select(sv);
    if(uvc_maxintr<2){ uvc_m("The USB controller has only one interrupter"); return -1; }
    u32 mes=0; for(int k=0;k<cam.nalt;k++) if(cam.alt[k].esit>mes)mes=cam.alt[k].esit; if(mes<64)mes=64;
    u32 bsz=(mes+63u)&~63u; int ntd=bsz<=3072u?1000:(bsz<=16384u?256:96); if(bulk){ bsz=16384u; ntd=256; }
    if(!uvc_bufs||(u32)cam.bufsz<bsz||cam.ntd!=ntd){ uvc_bufs=uvc_al(bsz*(u32)ntd,4096); if(!uvc_bufs){ uvc_m("Out of memory for camera buffers"); return -1; } cam.bufsz=(int)bsz; cam.ntd=ntd; }
    if(!uvc_ring){ uvc_ring=(volatile u32*)uvc_al(16384*2,65536); }
    if(!uvc_ermem){ uvc_ermem=uvc_al(65536+256,65536); if(uvc_ermem){ uvc_er=(volatile u32*)uvc_ermem; uvc_erst=(volatile u32*)(uvc_ermem+65536); } }
    if(!uvc_fb){ uvc_fb=uvc_al(1920*1200*2+65536,64); uvc_jpg=uvc_al(1920*1200*2+65536+1024,64); uvc_rgb=(u32*)uvc_al(1920*1200*4,64); }
    if(!uvc_ring||!uvc_ermem||!uvc_fb||!uvc_jpg||!uvc_rgb){ uvc_m("Out of memory for the camera"); return -1; }
    int order[64], no=0; for(int g=0;g<4;g++){ int idx[64], ni=0; for(int k=0;k<cam.nfr;k++) if(uvc_pref(&cam.fr[k],hd)==g)idx[ni++]=k;
        for(int a=0;a<ni;a++) for(int b2=a+1;b2<ni;b2++){ u32 aa=(u32)cam.fr[idx[a]].w*cam.fr[idx[a]].h, bb=(u32)cam.fr[idx[b2]].w*cam.fr[idx[b2]].h; int sw=(g>=2)?(bb<aa):(bb>aa); if(sw){ int t=idx[a]; idx[a]=idx[b2]; idx[b2]=t; } }
        for(int a=0;a<ni&&no<64;a++)order[no++]=idx[a]; }
    if(!no){ uvc_m("No picture size up to 1920x1200"); return -1; }
    for(int pass=0;pass<2;pass++)
    for(int tr=0;tr<no&&tr<16;tr++){ uvc_fr_t* f=&cam.fr[order[tr]];
        u8 p[48]; for(int k=0;k<48;k++)p[k]=0; p[0]=1; p[2]=f->fi; p[3]=f->ri; p[4]=(u8)f->defint; p[5]=(u8)(f->defint>>8); p[6]=(u8)(f->defint>>16); p[7]=(u8)(f->defint>>24);
        if(!uvc_req(0x21,0x01,0x0100,(u16)cam.vs_if,(u16)cam.plen,0,p)){ uvc_m("Camera refused the picture size (probe)"); continue; }
        u8 g2[48]; for(int k=0;k<48;k++)g2[k]=p[k];
        if(uvc_req(0xA1,0x81,0x0100,(u16)cam.vs_if,(u16)cam.plen,1,g2)){ for(int k=0;k<48;k++)p[k]=g2[k]; }
        uvc_maxfs=uvc_r32(p+18); uvc_maxpay=uvc_r32(p+22); uvc_interval=uvc_r32(p+4);
        if(!uvc_req(0x21,0x01,0x0200,(u16)cam.vs_if,(u16)cam.plen,0,p)){ uvc_m("Camera refused the picture size (commit)"); continue; }
        uvc_alt_t ba; uvc_alt_t* a;
        if(bulk){ for(int k=0;k<(int)sizeof(ba);k++)((u8*)&ba)[k]=0; ba.alt=0; ba.ep=(u8)cam.bep; ba.mps=(u16)cam.bmps; ba.burst=(u8)cam.bburst; ba.esit=bsz; a=&ba; cam.isbulk=1; cam.inpay=0; }
        else { int pick=-1; for(int k=0;k<cam.nalt;k++){ if(uvc_maxpay&&cam.alt[k].esit<uvc_maxpay)continue; if(pick<0||cam.alt[k].esit<cam.alt[pick].esit)pick=k; }
            if(pick<0&&pass==0){ uvc_m("Not enough USB bandwidth for that size - trying another"); continue; }
            if(pick<0) for(int k=0;k<cam.nalt;k++) if(pick<0||cam.alt[k].esit>cam.alt[pick].esit)pick=k;
            a=&cam.alt[pick]; cam.isbulk=0; }
        cam.maxp=a->mps&0x7FF; cam.burst=a->burst; cam.mult=a->mult; cam.esit=bulk?(int)bsz:(int)a->esit; cam.ss=cam.speed>=4;
        if(cam.esit>cam.bufsz){ uvc_m("Stream packet larger than the buffers"); continue; }
        sv=g_xcur; xhc_select(cam.ctl); uvc_ring_init(); uvc_er_init(); xhc_select(sv);
        if(cam.epon)uvc_cfg_ep(0,0);
        if(!uvc_cfg_ep(1,a)){ char b[96],*q=b; q=scopy(q,"USB controller refused the stream (code "); q=udec(q,(u32)uvc_cfgcc); q=scopy(q,uvc_cfgcc==8?", not enough USB bandwidth)":")"); *q=0; uvc_m(b); continue; }
        if(!bulk&&!uvc_req(0x01,0x0B,a->alt,(u16)cam.vs_if,0,0,0)){ uvc_cfg_ep(0,0); uvc_m("Camera refused SET_INTERFACE"); continue; }
        uvc_w=f->w; uvc_h=f->h; uvc_kind=f->kind; uvc_fi=f->fi; uvc_ri=f->ri; uvc_altsel=a->alt;
        uvc_fcap=uvc_kind==1?(1920u*1200u*2u):(u32)uvc_w*(u32)uvc_h*2u+4096u; if(uvc_fcap>1920u*1200u*2u+65536u)uvc_fcap=1920u*1200u*2u+65536u; uvc_fill=0; uvc_fbad=0; uvc_lfid=-1; uvc_jnew=0;
        for(u32 i=0;i<(u32)uvc_w*(u32)uvc_h;i++)uvc_rgb[i]=0x101418;
        uvc_events=0; uvc_tds=0; uvc_tderr=0; uvc_frames=0; uvc_badframes=0; uvc_short=0; uvc_bytes=0; uvc_fps10=0; uvc_ft0=hr_ns(); uvc_fc0=0;
        sv=g_xcur; xhc_select(cam.ctl); while(uvc_out<(u32)cam.ntd-1u)uvc_td(); xh_db(cam.slot,(u32)cam.dci); xhc_select(sv);
        uvc_state=2; { char b[96],*q=b; q=scopy(q,"Streaming "); q=udec(q,(u32)uvc_w); *q++='x'; q=udec(q,(u32)uvc_h); *q++=' '; q=scopy(q,uvc_kind_name(uvc_kind)); if(bulk)q=scopy(q," (bulk)"); *q=0; uvc_m(b); klog(b); }
        return 0; }
    return -1; }
void uvc_poll(void){
    if(!cam.ok||uvc_state!=2)return; int sv=g_xcur; xhc_select(cam.ctl);
    u32 ra=(u32)(u64)uvc_ring; int n=0;
    for(int guard=0;guard<8192;guard++){ volatile u32* e=uvc_er+uvc_ev1*4; if((e[3]&1u)!=uvc_ev1c)break;
        u32 ty=(e[3]>>10)&0x3F, cc=(e[2]>>24)&0xFF, res=e[2]&0xFFFFFF, ptr=e[0];
        uvc_ev1++; if(uvc_ev1>=4096){ uvc_ev1=0; uvc_ev1c^=1; } n++; uvc_events++;
        if(ty!=32)continue; uvc_lastcc=(int)cc;
        if(ptr>=ra&&ptr<ra+(u32)cam.ntd*16u&&uvc_out){ u32 idx=(ptr-ra)/16u;
            for(int g=0;g<cam.ntd&&uvc_out;g++){ u32 k=uvc_deq; uvc_deq++; if(uvc_deq>=(u32)cam.ntd)uvc_deq=0; uvc_out--;
                if(k==idx){ uvc_tds++; if(cc==1||cc==13){ int got=cam.esit-(int)res; if(got<0)got=0; if(cam.isbulk)uvc_bulk_chunk(uvc_bufs+(u64)k*cam.bufsz,got,cam.esit); else uvc_payload(uvc_bufs+(u64)k*cam.bufsz,got); } else uvc_tderr++; break; } } }
        else if(cc!=1&&cc!=13) uvc_tderr++; }
    if(n){ u64 rt=xhci_base+xhci_rtoff+0x40; XMW(rt+0x18,((u32)(u64)(uvc_er+uvc_ev1*4))|(1u<<3)); XMW(rt+0x1C,0); }
    int add=0; while(uvc_out<(u32)cam.ntd-1u){ uvc_td(); add=1; } if(add)xh_db(cam.slot,(u32)cam.dci);
    xhc_select(sv);
    u64 now=hr_ns(); if(now-uvc_ft0>=1000000000ULL){ uvc_fps10=(u32)((u64)(uvc_frames-uvc_fc0)*10000000000ULL/(now-uvc_ft0)); uvc_ft0=now; uvc_fc0=uvc_frames; } }
const u8 uvc_std_dht[]={0xFF,0xC4,0x01,0xA2,
0x00,0,1,5,1,1,1,1,1,1,0,0,0,0,0,0,0,0,1,2,3,4,5,6,7,8,9,10,11,
0x10,0,2,1,3,3,2,4,3,5,5,4,4,0,0,1,0x7d,
0x01,0x02,0x03,0x00,0x04,0x11,0x05,0x12,0x21,0x31,0x41,0x06,0x13,0x51,0x61,0x07,0x22,0x71,0x14,0x32,0x81,0x91,0xa1,0x08,0x23,0x42,0xb1,0xc1,0x15,0x52,0xd1,0xf0,
0x24,0x33,0x62,0x72,0x82,0x09,0x0a,0x16,0x17,0x18,0x19,0x1a,0x25,0x26,0x27,0x28,0x29,0x2a,0x34,0x35,0x36,0x37,0x38,0x39,0x3a,0x43,0x44,0x45,0x46,0x47,0x48,0x49,
0x4a,0x53,0x54,0x55,0x56,0x57,0x58,0x59,0x5a,0x63,0x64,0x65,0x66,0x67,0x68,0x69,0x6a,0x73,0x74,0x75,0x76,0x77,0x78,0x79,0x7a,0x83,0x84,0x85,0x86,0x87,0x88,0x89,
0x8a,0x92,0x93,0x94,0x95,0x96,0x97,0x98,0x99,0x9a,0xa2,0xa3,0xa4,0xa5,0xa6,0xa7,0xa8,0xa9,0xaa,0xb2,0xb3,0xb4,0xb5,0xb6,0xb7,0xb8,0xb9,0xba,0xc2,0xc3,0xc4,0xc5,
0xc6,0xc7,0xc8,0xc9,0xca,0xd2,0xd3,0xd4,0xd5,0xd6,0xd7,0xd8,0xd9,0xda,0xe1,0xe2,0xe3,0xe4,0xe5,0xe6,0xe7,0xe8,0xe9,0xea,0xf1,0xf2,0xf3,0xf4,0xf5,0xf6,0xf7,0xf8,0xf9,0xfa,
0x01,0,3,1,1,1,1,1,1,1,1,1,0,0,0,0,0,0,1,2,3,4,5,6,7,8,9,10,11,
0x11,0,2,1,2,4,4,3,4,7,5,4,4,0,1,2,0x77,
0x00,0x01,0x02,0x03,0x11,0x04,0x05,0x21,0x31,0x06,0x12,0x41,0x51,0x07,0x61,0x71,0x13,0x22,0x32,0x81,0x08,0x14,0x42,0x91,0xa1,0xb1,0xc1,0x09,0x23,0x33,0x52,0xf0,
0x15,0x62,0x72,0xd1,0x0a,0x16,0x24,0x34,0xe1,0x25,0xf1,0x17,0x18,0x19,0x1a,0x26,0x27,0x28,0x29,0x2a,0x35,0x36,0x37,0x38,0x39,0x3a,0x43,0x44,0x45,0x46,0x47,0x48,
0x49,0x4a,0x53,0x54,0x55,0x56,0x57,0x58,0x59,0x5a,0x63,0x64,0x65,0x66,0x67,0x68,0x69,0x6a,0x73,0x74,0x75,0x76,0x77,0x78,0x79,0x7a,0x82,0x83,0x84,0x85,0x86,0x87,
0x88,0x89,0x8a,0x92,0x93,0x94,0x95,0x96,0x97,0x98,0x99,0x9a,0xa2,0xa3,0xa4,0xa5,0xa6,0xa7,0xa8,0xa9,0xaa,0xb2,0xb3,0xb4,0xb5,0xb6,0xb7,0xb8,0xb9,0xba,0xc2,0xc3,
0xc4,0xc5,0xc6,0xc7,0xc8,0xc9,0xca,0xd2,0xd3,0xd4,0xd5,0xd6,0xd7,0xd8,0xd9,0xda,0xe2,0xe3,0xe4,0xe5,0xe6,0xe7,0xe8,0xe9,0xea,0xf2,0xf3,0xf4,0xf5,0xf6,0xf7,0xf8,0xf9,0xfa};
const int uvc_std_dht_len=(int)sizeof(uvc_std_dht);
static void uvc_reset(void){ cam.ok=0; uvc_state=0; uvc_reg_hold=-1; uvc_er_done=0; }
static void uvc_unplug(int port){ if(cam.ok&&cam.ctl==g_xcur&&cam.root==port){ uvc_gone(); uvc_reg_hold=-1; } }
typedef struct { u8 ifn, alt, ep, intv, ch, sub, bits, nrate, cont, v2; u16 mps; u32 rate[8]; } uac_alt_t;
static struct { int ok, ctl, reg, slot, speed, root, ac_if, nalt, nfu, pb, cp, pbon, cpon, need_sync, v2, clk, incam; u32 e0_enq, e0_cyc; uac_alt_t alt[16]; u8 fu[8]; } aud;
static void uvc_aud_share(int from_cam){ if(!cam.ok||!aud.ok||cam.ctl!=aud.ctl||cam.reg!=aud.reg)return; if(from_cam){ aud.e0_enq=cam.e0_enq; aud.e0_cyc=cam.e0_cyc; } else { cam.e0_enq=aud.e0_enq; cam.e0_cyc=aud.e0_cyc; } }
int aud_state=0; u32 aud_pb_rate=0, aud_cp_rate=0, aud_events=0, aud_underrun=0, aud_cperr=0, aud_pberr=0; char aud_name[48]="", aud_msg[96]=""; int aud_pb_ch=0, aud_cp_ch=0;
typedef struct { volatile u32* ring; u8* bufs; u32 enq, deq, cyc, out, ntd, bsz, dci, mps, ivus, rate, ch, acc, dirin, bs, bcap; } aud_ep_t;
static aud_ep_t apb, acp;
static u8* aud_ermem=0; static volatile u32 *aud_er=0, *aud_erst=0; static u32 aud_ev=0, aud_evc=1; static int aud_er_done=0;
#define APB_N 16384u
#define ACP_N 32768u
static u32 *aud_pbq=0, *aud_cpq=0; static volatile u32 aud_pbw=0, aud_pbr=0, aud_cpw=0, aud_cpr=0; static int aud_vol=80; static u32 aud_pacc=0;
static void aud_m(const char* s){ int i=0; while(s[i]&&i<95){ aud_msg[i]=s[i]; i++; } aud_msg[i]=0; }
static int aud_req(u8 rt,u8 req,u16 val,u16 idx,u16 len,int in,u8* data){
    if(!aud.ok)return 0; int sv=g_xcur; xhc_select(aud.ctl);
    u32 s0=cur_ep0,s1=cur_dbuf,s2=e0_enq,s3=e0_cyc; int s4=cur_slot;
    cur_ep0=XHID_EP0RING(aud.reg); cur_dbuf=XHID_DATABUF(aud.reg); e0_enq=aud.e0_enq; e0_cyc=aud.e0_cyc; cur_slot=aud.slot;
    if(!in&&len&&data){ volatile u8* db=(volatile u8*)(u64)cur_dbuf; for(int i=0;i<len;i++)db[i]=data[i]; }
    int r=xh_ctrl(rt,req,val,idx,len,in,in?data:0);
    aud.e0_enq=e0_enq; aud.e0_cyc=e0_cyc; uvc_aud_share(0); cur_ep0=s0; cur_dbuf=s1; e0_enq=s2; e0_cyc=s3; cur_slot=s4; xhc_select(sv); return r; }
static int aud_has_audio(const u8* c,int n){ for(int i=0;i+7<n;){ int bl=c[i]; if(bl<2)break; if(c[i+1]==4&&bl>=9&&c[i+5]==1&&(c[i+6]==1||c[i+6]==2))return 1; i+=bl; } return 0; }
static void aud_mark_sync(void){ aud.need_sync=1; }
static void aud_sync_e0(void){ if(aud.ok&&aud.need_sync&&aud.ctl==g_xcur){ aud.e0_enq=e0_enq; aud.e0_cyc=e0_cyc; aud.need_sync=0; } }
static void aud_parse(const u8* c,int n){
    int cif=-1,calt=0,ccls=0,csub=0,fmt=0,proto=0,v2ch=0; uac_alt_t cur; int have=0;
    aud.ac_if=-1; aud.nalt=0; aud.nfu=0; aud.v2=0; aud.clk=-1;
    for(int i=0;i+2<=n;){ int bl=c[i]; if(bl<2||i+bl>n)break; int t=c[i+1];
        if(t==4&&bl>=9){ cif=c[i+2]; calt=c[i+3]; ccls=c[i+5]; csub=c[i+6]; proto=c[i+7]; fmt=0; have=0; v2ch=0;
            if(ccls==1&&csub==1&&aud.ac_if<0){ aud.ac_if=cif; if(proto==0x20)aud.v2=1; } }
        else if(t==0x24&&ccls==1&&csub==1&&proto==0x20&&bl>=4&&c[i+2]==0x0A&&aud.clk<0) aud.clk=c[i+3];
        else if(t==0x24&&ccls==1&&csub==1&&proto==0x20&&bl>=5&&c[i+2]==6&&aud.nfu<8) aud.fu[aud.nfu++]=c[i+3];
        else if(t==0x24&&ccls==1&&csub==2&&calt>0&&proto==0x20&&bl>=11&&c[i+2]==1){ u32 bm=(u32)(c[i+6]|(c[i+7]<<8)|(c[i+8]<<16)|((u32)c[i+9]<<24)); fmt=(c[i+5]==1&&(bm&1))?1:0; v2ch=c[i+10]; }
        else if(t==0x24&&ccls==1&&csub==2&&calt>0&&proto==0x20&&bl>=6&&c[i+2]==2&&c[i+3]==1&&fmt==1){
            for(int k=0;k<(int)sizeof(cur);k++)((u8*)&cur)[k]=0; cur.ifn=(u8)cif; cur.alt=(u8)calt; cur.ch=(u8)v2ch; cur.sub=c[i+4]; cur.bits=c[i+5]; cur.v2=1; have=1; }
        else if(t==0x24&&ccls==1&&csub==1&&proto!=0x20&&bl>=4&&c[i+2]==6&&aud.nfu<8) aud.fu[aud.nfu++]=c[i+3];
        else if(t==0x24&&ccls==1&&csub==2&&calt>0&&proto!=0x20&&bl>=7&&c[i+2]==1){ fmt=c[i+5]|(c[i+6]<<8); }
        else if(t==0x24&&ccls==1&&csub==2&&calt>0&&proto!=0x20&&bl>=8&&c[i+2]==2&&c[i+3]==1&&fmt==1){
            for(int k=0;k<(int)sizeof(cur);k++)((u8*)&cur)[k]=0; cur.ifn=(u8)cif; cur.alt=(u8)calt; cur.ch=c[i+4]; cur.sub=c[i+5]; cur.bits=c[i+6];
            int nf=c[i+7]; if(nf==0&&bl>=14){ cur.cont=1; cur.nrate=2; cur.rate[0]=(u32)(c[i+8]|(c[i+9]<<8)|(c[i+10]<<16)); cur.rate[1]=(u32)(c[i+11]|(c[i+12]<<8)|(c[i+13]<<16)); }
            else { for(int k=0;k<nf&&k<8&&8+k*3+2<bl;k++){ const u8* f=c+i+8+k*3; cur.rate[cur.nrate++]=(u32)(f[0]|(f[1]<<8)|(f[2]<<16)); } }
            have=1; }
        else if(t==5&&bl>=7&&ccls==1&&csub==2&&have&&(c[i+3]&3)==1&&aud.nalt<16){ cur.ep=c[i+2]; cur.mps=(u16)(c[i+4]|(c[i+5]<<8)); cur.intv=c[i+6]; aud.alt[aud.nalt++]=cur; have=0; }
        i+=bl; } }
static u32 aud_pick_rate(const uac_alt_t* a){
    if(a->v2)return 48000;
    if(a->cont){ if(48000>=a->rate[0]&&48000<=a->rate[1])return 48000; if(44100>=a->rate[0]&&44100<=a->rate[1])return 44100; return a->rate[1]; }
    u32 best=0; for(int k=0;k<a->nrate;k++){ if(a->rate[k]==48000)return 48000; } for(int k=0;k<a->nrate;k++) if(a->rate[k]==44100)return 44100;
    for(int k=0;k<a->nrate;k++) if(a->rate[k]<=48000&&a->rate[k]>best)best=a->rate[k]; if(!best&&a->nrate)best=a->rate[0]; return best; }
static int aud_pick(int in){
    int best=-1,score=-1; for(int k=0;k<aud.nalt;k++){ uac_alt_t* a=&aud.alt[k]; if(((a->ep&0x80)!=0)!=in)continue; if(!a->nrate&&!a->v2)continue;
        if(in){ if(a->sub<2||a->sub>4||a->bits<16||a->ch<1||a->ch>8)continue; } else { if(a->sub!=2||a->bits!=16||a->ch<1||a->ch>2)continue; }
        u32 r=aud_pick_rate(a); int s=(a->ch==2?4:a->ch==1?3:2)+(r==48000?2:r==44100?1:0)+(a->sub==2?1:0); if(s>score){ score=s; best=k; } }
    return best; }
static u8* aud_al(u32 n,u32 al){ u8* p=(u8*)kmalloc((unsigned long)n+(unsigned long)al); if(!p)return 0; u64 a=((u64)p+al-1)&~(u64)(al-1); if(a+n>0xFFFFFFFFULL){ kfree(p); return 0; } return (u8*)a; }
static void aud_er_init(void){
    u64 rt=xhci_base+xhci_rtoff+0x60;
    if(!aud_er_done){ aud_er_done=1; for(int i=0;i<4096*4;i++)aud_er[i]=0; aud_erst[0]=(u32)(u64)aud_er; aud_erst[1]=0; aud_erst[2]=4096; aud_erst[3]=0; aud_ev=0; aud_evc=1;
        XMW(rt+0x00,0); XMW(rt+0x08,1); XMW(rt+0x18,(u32)(u64)aud_er); XMW(rt+0x1C,0); XMW(rt+0x10,(u32)(u64)aud_erst); XMW(rt+0x14,0); } }
static int aud_ivus(const uac_alt_t* a){ if(aud.speed>=3){ int e=a->intv?a->intv-1:0; if(e>15)e=15; return 125<<e; } int e=a->intv?a->intv-1:0; if(e>4)e=4; return 1000<<e; }
static int aud_cfg_ep(aud_ep_t* e,const uac_alt_t* a,int add){
    u32 ic0=XHID_INPUTCTX(aud.reg); ctx_zero(ic0,(int)ctx_sz()*33/4); volatile u32* ic=(volatile u32*)(u64)ic0;
    int dci=(int)e->dci; if(dci<2)return 0;
    ic[0]=add?0u:(1u<<dci); ic[1]=1u|(add?(1u<<dci):0u);
    volatile u32* os=(volatile u32*)(u64)XHID_DEVCTX(aud.reg); volatile u32* sc=(volatile u32*)(u64)(ic0+ctx_sz());
    for(int k=0;k<4;k++)sc[k]=os[k]; u32 ent=(sc[0]>>27)&31u; if(add&&(u32)dci>ent)ent=(u32)dci; sc[0]=(sc[0]&~(31u<<27))|(ent<<27); sc[3]=0;
    if(add){ volatile u32* ep=(volatile u32*)(u64)(ic0+ctx_sz()*(u32)(dci+1));
        u32 iv; if(aud.speed>=3){ int v=a->intv-1; if(v<0)v=0; if(v>15)v=15; iv=(u32)v; } else { int v=a->intv+2; if(v<3)v=3; if(v>18)v=18; iv=(u32)v; }
        u32 maxp=(u32)(a->mps&0x7FF), es=maxp*(((a->mps>>11)&3u)+1u);
        ep[0]=(iv<<16); ep[1]=((e->dirin?5u:1u)<<3)|((u32)((a->mps>>11)&3u)<<8)|(maxp<<16);
        ep[2]=(u32)(u64)e->ring|1u; ep[3]=0; ep[4]=(es&0xFFFFu)|((es&0xFFFFu)<<16); }
    u8 cc=0,sl=0; int sv=g_xcur; xhc_select(aud.ctl); xh_cmd_s(ic0,0,0,12,(u32)aud.slot,&cc,&sl); xhc_select(sv);
    return cc==1; }
static void aud_ring_init(aud_ep_t* e){
    u32 n=e->ntd; for(u32 i=0;i<(n+1)*4;i++)e->ring[i]=0; volatile u32* lk=e->ring+n*4; lk[0]=(u32)(u64)e->ring; lk[1]=0; lk[2]=0; lk[3]=(6u<<10)|(1u<<1)|1u;
    e->enq=0; e->deq=0; e->cyc=1; e->out=0; e->acc=0; }
static void aud_queue(aud_ep_t* e,u32 len){
    volatile u32* t=e->ring+e->enq*4; u32 b=(u32)(u64)(e->bufs+(u64)e->enq*e->bsz);
    t[0]=b; t[1]=0; t[2]=len|(2u<<22); __sync_synchronize();
    t[3]=e->cyc|(1u<<2)|(1u<<5)|(5u<<10)|(1u<<31);
    e->enq++; if(e->enq>=e->ntd){ volatile u32* lk=e->ring+e->ntd*4; lk[3]=(lk[3]&~1u)|e->cyc; e->enq=0; e->cyc^=1; }
    e->out++; }
static u32 aud_fill_pb(u8* d){
    u32 num=apb.rate*apb.ivus+apb.acc; u32 fr=num/1000000u; apb.acc=num%1000000u;
    u32 maxfr=apb.mps/(apb.ch*2u); if(fr>maxfr)fr=maxfr;
    int g=aud_vol*aud_vol; short* o=(short*)d;
    for(u32 k=0;k<fr;k++){ u32 v=0; if(aud_pbr!=aud_pbw)v=aud_pbq[aud_pbr%APB_N]; else if(k==0)aud_underrun++;
        int l=(short)(v&0xFFFF), r=(short)(v>>16); l=l*g/10000; r=r*g/10000;
        if(apb.ch==2){ o[k*2]=(short)l; o[k*2+1]=(short)r; } else o[k]=(short)((l+r)/2);
        aud_pacc+=48000u; while(aud_pacc>=apb.rate){ aud_pacc-=apb.rate; if(aud_pbr!=aud_pbw)aud_pbr++; } }
    return fr*apb.ch*2u; }
static int aud_s16(const u8* p,u32 bs){ return bs==2?(short)(p[0]|(p[1]<<8)):bs==3?(short)(p[1]|(p[2]<<8)):(short)(p[2]|(p[3]<<8)); }
static void aud_take_cp(const u8* d,u32 n){
    u32 bs=acp.bs?acp.bs:2u; u32 fs=acp.ch*bs; if(!fs)return; u32 fr=n/fs;
    for(u32 k=0;k<fr;k++){ const u8* f=d+k*fs; int l=aud_s16(f,bs), r=acp.ch>=2?aud_s16(f+bs,bs):l; u32 v=((u32)(u16)l)|((u32)(u16)r<<16);
        acp.acc+=48000u; while(acp.acc>=acp.rate){ acp.acc-=acp.rate; if(aud_cpw-aud_cpr>=ACP_N)aud_cpr++; aud_cpq[aud_cpw%ACP_N]=v; aud_cpw++; } } }
static void aud_service(void){
    if(!aud.ok||(!aud.pbon&&!aud.cpon))return; int sv=g_xcur; xhc_select(aud.ctl);
    int n=0; for(int guard=0;guard<4096;guard++){ volatile u32* e=aud_er+aud_ev*4; if((e[3]&1u)!=aud_evc)break;
        u32 ty=(e[3]>>10)&0x3F, cc=(e[2]>>24)&0xFF, res=e[2]&0xFFFFFF, ptr=e[0];
        aud_ev++; if(aud_ev>=4096){ aud_ev=0; aud_evc^=1; } n++; aud_events++;
        if(ty!=32)continue;
        aud_ep_t* ep=0; if(aud.pbon&&ptr>=(u32)(u64)apb.ring&&ptr<(u32)(u64)apb.ring+apb.ntd*16u)ep=&apb; else if(aud.cpon&&ptr>=(u32)(u64)acp.ring&&ptr<(u32)(u64)acp.ring+acp.ntd*16u)ep=&acp;
        if(!ep||!ep->out)continue; u32 idx=(ptr-(u32)(u64)ep->ring)/16u;
        for(u32 g=0;g<ep->ntd&&ep->out;g++){ u32 k=ep->deq; ep->deq++; if(ep->deq>=ep->ntd)ep->deq=0; ep->out--;
            if(k==idx){ if(ep==&acp){ if(cc==1||cc==13){ u32 want=(u32)ep->ring[k*4+2]&0x1FFFF; u32 got=want>res?want-res:0; aud_take_cp(ep->bufs+(u64)k*ep->bsz,got); } else aud_cperr++; }
                else if(cc!=1&&cc!=13)aud_pberr++;
                break; } } }
    if(n){ u64 rt=xhci_base+xhci_rtoff+0x60; XMW(rt+0x18,((u32)(u64)(aud_er+aud_ev*4))|(1u<<3)); XMW(rt+0x1C,0); }
    if(aud.pbon){ int add=0; while(apb.out<apb.ntd-2u&&apb.out<96u){ u32 len=aud_fill_pb(apb.bufs+(u64)apb.enq*apb.bsz); aud_queue(&apb,len); add=1; } if(add)xh_db(aud.slot,apb.dci); }
    if(aud.cpon){ int add=0; while(acp.out<acp.ntd-2u&&acp.out<64u){ aud_queue(&acp,acp.mps); add=1; } if(add)xh_db(aud.slot,acp.dci); }
    xhc_select(sv); }
static int aud_setup_ep(aud_ep_t* e,int k,int in){
    uac_alt_t* a=&aud.alt[k]; e->dirin=(u32)in; e->dci=(u32)((a->ep&0x0F)*2+(in?1:0)); e->mps=(u32)(a->mps&0x7FF); e->ch=a->ch; e->bs=a->sub; e->rate=aud_pick_rate(a); if(e->rate<8000||e->rate>192000)e->rate=48000; e->ivus=(u32)aud_ivus(a);
    e->ntd=256; e->bsz=64; while(e->bsz<e->mps)e->bsz<<=1;
    if(!e->ring)e->ring=(volatile u32*)aud_al(4096*2,4096); { u32 need=e->bsz*e->ntd>65536u?e->bsz*e->ntd:65536u; if(!e->bufs||e->bcap<need){ e->bufs=aud_al(need,4096); e->bcap=e->bufs?need:0; } }
    if(!e->ring||!e->bufs){ aud_m("Out of memory for the headset"); return 0; }
    if(!aud_req(0x01,0x0B,a->alt,a->ifn,0,0,0)){ aud_m("Headset refused SET_INTERFACE"); return 0; }
    if(a->v2&&aud.clk>=0&&aud.ac_if>=0){ u8 f[4]={(u8)e->rate,(u8)(e->rate>>8),(u8)(e->rate>>16),0}; u16 ix=(u16)((aud.clk<<8)|aud.ac_if); aud_req(0x21,0x01,0x0100,ix,4,0,f);
        u8 g[4]={0,0,0,0}; if(aud_req(0xA1,0x01,0x0100,ix,4,1,g)){ u32 r=(u32)(g[0]|(g[1]<<8)|(g[2]<<16)|((u32)g[3]<<24)); if(r>=8000&&r<=192000)e->rate=r; } }
    else if(a->cont||a->nrate>1){ u8 f[3]={(u8)e->rate,(u8)(e->rate>>8),(u8)(e->rate>>16)}; aud_req(0x22,0x01,0x0100,a->ep,3,0,f); }
    int sv=g_xcur; xhc_select(aud.ctl); aud_ring_init(e); aud_er_init(); xhc_select(sv);
    if(!aud_cfg_ep(e,a,1)){ aud_req(0x01,0x0B,0,a->ifn,0,0,0); aud_m("USB controller refused the audio stream"); return 0; }
    return 1; }
int ausnd_start(void){
    if(!aud.ok)return 0; if(aud.pbon)return 1; if(aud.pb<0)return 0;
    if(!aud_setup_ep(&apb,aud.pb,0))return 0; aud.pbon=1; aud_pacc=0; aud_pb_rate=apb.rate; aud_pb_ch=(int)apb.ch;
    aud_service(); return 1; }
int ausnd_free(void){ return (int)(APB_N-(aud_pbw-aud_pbr))*4; }
int ausnd_write(const u8* pcm,int len){
    if(!aud.ok)return -1; if(!aud.pbon&&!ausnd_start())return -1;
    aud_service(); int fr=len/4, n=0; const u32* s=(const u32*)pcm;
    while(n<fr&&aud_pbw-aud_pbr<APB_N){ aud_pbq[aud_pbw%APB_N]=s[n]; aud_pbw++; n++; }
    aud_service(); return n*4; }
void ausnd_poll(void){ aud_service(); }
void ausnd_stop(void){ }
void ausnd_volume(int p){ if(p<0)p=0; if(p>100)p=100; aud_vol=p; }
int ausnd_rec_start(void){ if(!aud.ok||aud.cp<0)return 0; if(aud.cpon)return 1; if(!aud_setup_ep(&acp,aud.cp,1))return 0; aud_cpw=aud_cpr=0; aud.cpon=1; aud_cp_rate=acp.rate; aud_cp_ch=(int)acp.ch; aud_service(); return 1; }
int ausnd_rec_read(u8* d,int len){ aud_service(); int fr=len/4, n=0; u32* o=(u32*)d; while(n<fr&&aud_cpr!=aud_cpw){ o[n++]=aud_cpq[aud_cpr%ACP_N]; aud_cpr++; } return n*4; }
void ausnd_rec_stop(void){ if(!aud.cpon)return; aud.cpon=0; aud_cfg_ep(&acp,0,0); aud_req(0x01,0x0B,0,aud.alt[aud.cp].ifn,0,0,0); }
void ausnd_rec_gain(int p){ (void)p; }
static char aud_rname[56];
static int aud_attach(const usbloc_t* L,int d,const u8* full,int wtot,u8 cfgval,u16 vid,u16 pid,const char* nm){
    (void)vid; (void)pid;
    for(int k=0;k<(int)sizeof(aud);k++)((u8*)&aud)[k]=0; aud.incam=aud_incam_next;
    aud.ctl=g_xcur; aud.reg=d; aud.slot=cur_slot; aud.speed=L->speed; aud.root=L->root;
    int q=0; const char* src=(nm&&nm[0])?nm:"USB headset"; for(;src[q]&&q<47;q++)aud_name[q]=src[q]; aud_name[q]=0;
    aud_parse(full,wtot); aud.pb=aud_pick(0); aud.cp=aud_pick(1);
    if(aud.pb<0&&aud.cp<0){ aud_m("USB audio device without a 16-bit PCM stream"); return 0; }
    int sv=g_xcur; xhc_select(aud.ctl); { u32 mi=(XMR(xhci_base+0x04)>>8)&0x7FF; xhc_select(sv); if(mi<3){ aud_m("The USB controller has too few interrupters for audio"); return 0; } }
    if(!xh_ctrl(0x00,9,cfgval,0,0,0,0)){ aud_m("headset: SET_CONFIGURATION failed"); return 0; }
    aud.e0_enq=e0_enq; aud.e0_cyc=e0_cyc; aud.ok=1;
    if(!aud_pbq)aud_pbq=(u32*)kmalloc(APB_N*4); if(!aud_cpq)aud_cpq=(u32*)kmalloc(ACP_N*4); if(!aud_ermem){ aud_ermem=aud_al(65536+256,65536); if(aud_ermem){ aud_er=(volatile u32*)aud_ermem; aud_erst=(volatile u32*)(aud_ermem+65536); } }
    if(!aud_pbq||!aud_cpq||!aud_ermem){ aud.ok=0; aud_m("Out of memory for the headset"); return 0; }
    aud_pbw=aud_pbr=aud_cpw=aud_cpr=0;
    if(aud.ac_if>=0) for(int f=0;f<aud.nfu;f++){ u8 z=0; aud_req(0x21,0x01,0x0100,(u16)((aud.fu[f]<<8)|aud.ac_if),1,0,&z); }
    if(aud.cp>=0){ char* p=aud_rname; p=scopy(p,aud_name); p=scopy(p," microphone"); *p=0; }
    aud_state=1;
    { extern void snd_usb_arrived(const char*,const char*); snd_usb_arrived(aud_name,aud.cp>=0?aud_rname:0); }
    { char b[120],*p=b; p=scopy(p,"usb audio: "); p=scopy(p,aud_name); if(aud.pb>=0){ p=scopy(p,", speakers "); p=udec(p,aud_pick_rate(&aud.alt[aud.pb])); p=scopy(p," Hz "); p=udec(p,aud.alt[aud.pb].ch); p=scopy(p," ch"); } if(aud.cp>=0){ p=scopy(p,", microphone "); p=udec(p,aud_pick_rate(&aud.alt[aud.cp])); p=scopy(p," Hz "); p=udec(p,aud.alt[aud.cp].ch); p=scopy(p," ch"); } *p=0; klog(b); }
    { extern void toast(const char*); char b[96],*p=b; p=scopy(p,aud.incam?"Camera microphone ready: ":aud.pb<0?"Microphone connected: ":"Headset connected: "); p=scopy(p,aud_name); *p=0; toast(b); }
    aud_m(aud.incam?"Camera microphone ready":aud.pb<0?"Microphone ready":"Headset ready"); return 1; }
static void aud_gone(void){ if(!aud.ok)return; aud.ok=0; aud.pbon=0; aud.cpon=0; aud_state=0; aud_reg_hold=-1; aud_m("Headset disconnected"); { extern void snd_usb_detach(void); snd_usb_detach(); } { extern void toast(const char*); toast("Headset disconnected - sound goes to the speakers again"); } }
static void aud_unplug(int port){ if(aud.ok&&aud.ctl==g_xcur&&aud.root==port)aud_gone(); }
static void aud_reset(void){ if(aud.ok){ aud.ok=0; aud.pbon=0; aud.cpon=0; extern void snd_usb_detach(void); snd_usb_detach(); } aud_state=0; aud_reg_hold=-1; aud_er_done=0; }
const char* aud_status(void){ return aud_msg[0]?aud_msg:"No USB headset"; }
static void uvc_add_mic(const usbloc_t* L,int d,const u8* big,int tw,u8 cfgval,u16 vid,u16 pid,const char* nm){
    e0_enq=cam.e0_enq; e0_cyc=cam.e0_cyc; aud_incam_next=1; int ak=aud_attach(L,d,big,tw,cfgval,vid,pid,nm); aud_incam_next=0;
    if(ak){ cam.e0_enq=aud.e0_enq; cam.e0_cyc=aud.e0_cyc; aud_reg_hold=d; klog("camera: the camera's microphone is ready too"); } }
int camera_info_text(char* o,int cap){
    char* p=o; char* e=o+cap-200;
    if(!cam.ok){ p=scopy(p,"State=No camera found\n"); if(uvc_msg[0]){ p=scopy(p,"Message="); p=scopy(p,uvc_msg); *p++='\n'; } *p=0; return (int)(p-o); }
    p=scopy(p,"Name="); p=scopy(p,uvc_name); p=scopy(p,"\nUsbId="); p=hx(p,uvc_vid,4); *p++=':'; p=hx(p,uvc_pid,4);
    p=scopy(p,"\nSpeed="); p=scopy(p,cam.speed>=4?"SuperSpeed":cam.speed==3?"High speed":"Full speed");
    p=scopy(p,"\nUvcVersion="); p=udec(p,(u32)(cam.bcd>>8)); *p++='.'; p=hx(p,(u32)(cam.bcd&0xFF),2);
    p=scopy(p,"\nStreams="); p=udec(p,(u32)cam.nvs); p=scopy(p," (using "); p=udec(p,(u32)uvc_stream_cur()+1); *p++=')';
    p=scopy(p,"\nTransfer="); p=scopy(p,cam.nalt?"isochronous":cam.bulk?"bulk":"none");
    p=scopy(p,"\nState="); p=scopy(p,uvc_state==2?"Streaming":"Ready");
    if(uvc_state==2){ p=scopy(p,"\nPicture="); p=udec(p,(u32)uvc_w); *p++='x'; p=udec(p,(u32)uvc_h); *p++=' '; p=scopy(p,uvc_kind_name(uvc_kind)); p=scopy(p,"\nFps="); p=udec(p,uvc_fps10/10); *p++='.'; p=udec(p,uvc_fps10%10); }
    p=scopy(p,"\nMicrophone="); p=scopy(p,aud.ok&&aud.incam?"yes (built into the camera)":"no");
    p=scopy(p,"\n\nSizes\n");
    for(int k=0;k<cam.nfr&&p<e;k++){ p=scopy(p,"  "); p=scopy(p,uvc_kind_name(cam.fr[k].kind)); *p++=' '; p=udec(p,cam.fr[k].w); *p++='x'; p=udec(p,cam.fr[k].h); if(cam.fr[k].defint){ p=scopy(p,"  "); p=udec(p,10000000u/cam.fr[k].defint); p=scopy(p," fps"); } *p++='\n'; }
    *p=0; return (int)(p-o); }
int mic_info_text(char* o,int cap){
    char* p=o; (void)cap; extern struct nvx_snd g_snd; extern int g_snd_state;
    p=scopy(p,"Built-in microphone\n"); p=scopy(p,"  State="); p=scopy(p,g_snd_state==0?"sound not started yet":(g_snd_state==1&&g_snd.rec_start)?"available":"not available"); *p++='\n'; if(g_snd_state==1&&g_snd.rec_name){ p=scopy(p,"  Name="); p=scopy(p,g_snd.rec_name); *p++='\n'; }
    p=scopy(p,"\nUSB microphone\n");
    if(aud.ok&&aud.cp>=0){ uac_alt_t* a=&aud.alt[aud.cp]; p=scopy(p,"  Name="); p=scopy(p,aud_name); p=scopy(p,aud.incam?"  (in the camera)":"");
        p=scopy(p,"\n  UsbAudio="); p=scopy(p,aud.v2?"2.0":"1.0"); p=scopy(p,"\n  Channels="); p=udec(p,a->ch); p=scopy(p,"\n  Bits="); p=udec(p,a->bits); p=scopy(p,"\n  Rate="); p=udec(p,acp.rate?acp.rate:aud_pick_rate(a)); p=scopy(p," Hz\n  Recording="); p=scopy(p,aud.cpon?"yes":"no"); *p++='\n'; }
    else { p=scopy(p,"  State=none connected\n"); }
    *p=0; return (int)(p-o); }
