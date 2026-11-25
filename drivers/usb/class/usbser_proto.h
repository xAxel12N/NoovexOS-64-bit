#ifndef NVX_USBSER_PROTO_H
#define NVX_USBSER_PROTO_H
enum { USR_NONE, USR_FTDI, USR_CP210X, USR_CH341, USR_PL2303, USR_ACM };
#define USR_MAXIF 4
#define USR_MAXREQ 24
#define USR_FT_HCLK 1
#define USR_FT_MULTI 2
#define USR_FT_HIX 4
#define USR_TIOCM_LE 0x001
#define USR_TIOCM_DTR 0x002
#define USR_TIOCM_RTS 0x004
#define USR_TIOCM_CTS 0x020
#define USR_TIOCM_CAR 0x040
#define USR_TIOCM_RNG 0x080
#define USR_TIOCM_DSR 0x100
#define USR_CBAUD 0x0000100Fu
#define USR_CBAUDEX 0x00001000u
#define USR_BOTHER 0x00001000u
#define USR_CSIZE 0x00000030u
#define USR_CSTOPB 0x00000040u
#define USR_CREAD 0x00000080u
#define USR_PARENB 0x00000100u
#define USR_PARODD 0x00000200u
#define USR_HUPCL 0x00000400u
#define USR_CLOCAL 0x00000800u
#define USR_CMSPAR 0x40000000u
#define USR_CRTSCTS 0x80000000u
struct usr_req { u8 rt, req; u16 val, idx, len; u8 data[8]; };
struct usr_line { u32 baud; u8 bits, parity, stop; };
struct usr_port { u8 ifn, ctl, ie, oe, intr; u16 im, om; };
struct usr_cfg { int n; struct usr_port p[USR_MAXIF]; };
struct usr_id { u16 vid, pid; u8 chip; };
static const struct usr_id usr_ids[]={
    {0x0403,0x6001,USR_FTDI},{0x0403,0x6010,USR_FTDI},{0x0403,0x6011,USR_FTDI},{0x0403,0x6014,USR_FTDI},{0x0403,0x6015,USR_FTDI},{0x0403,0x6006,USR_FTDI},
    {0x10C4,0xEA60,USR_CP210X},{0x10C4,0xEA61,USR_CP210X},{0x10C4,0xEA63,USR_CP210X},{0x10C4,0xEA70,USR_CP210X},{0x10C4,0xEA71,USR_CP210X},{0x10C4,0xEA7A,USR_CP210X},{0x10C4,0xEA7B,USR_CP210X},
    {0x1A86,0x7523,USR_CH341},{0x1A86,0x7522,USR_CH341},{0x1A86,0x5523,USR_CH341},{0x4348,0x5523,USR_CH341},
    {0x067B,0x2303,USR_PL2303},{0x0557,0x2008,USR_PL2303},{0x04BB,0x0A03,USR_PL2303},
    {0,0,0} };
static const char* const usr_chipname[]={"?","FTDI","CP210x","CH340/CH341","PL2303","CDC-ACM"};
static const u32 usr_btab[32]={0,50,75,110,134,150,200,300,600,1200,1800,2400,4800,9600,19200,38400,
    0,57600,115200,230400,460800,500000,576000,921600,1000000,1152000,1500000,2000000,2500000,3000000,3500000,4000000};
static inline int usr_lookup(u16 vid,u16 pid){ for(int i=0;usr_ids[i].vid;i++) if(usr_ids[i].vid==vid&&usr_ids[i].pid==pid)return usr_ids[i].chip; return USR_NONE; }
static inline int usr_has_acm(const u8* c,int n){ for(int i=0;i+8<n;){ int bl=c[i]; if(bl<2||i+bl>n)break; if(c[i+1]==4&&bl>=9&&c[i+5]==2&&c[i+6]==2)return 1; i+=bl; } return 0; }
static inline int usr_chip(u16 vid,u16 pid,const u8* c,int n){ int k=usr_lookup(vid,pid); if(k)return k; return usr_has_acm(c,n)?USR_ACM:USR_NONE; }
static inline int usr_parse(int chip,const u8* c,int n,struct usr_cfg* o){
    o->n=0; int cur=-1, cls=-1, sub=-1, alt=0, ctl=-1, pending_ctl=-1, pintr=0; struct usr_port t; t.ie=t.oe=t.intr=0; t.im=t.om=0; t.ifn=0; t.ctl=0;
    for(int i=0;i+1<n;){ int bl=c[i]; if(bl<2||i+bl>n)break;
        if(c[i+1]==4&&bl>=9){
            if(cur>=0&&t.ie&&t.oe&&o->n<USR_MAXIF&&(chip!=USR_ACM||cls==0x0A)){ if(chip==USR_ACM){ t.ctl=(u8)(ctl>=0?ctl:t.ifn); t.intr=(u8)pintr; } o->p[o->n++]=t; if(chip==USR_ACM){ ctl=-1; pintr=0; } }
            cur=c[i+2]; alt=c[i+3]; cls=c[i+5]; sub=c[i+6]; t.ifn=(u8)cur; t.ctl=(u8)cur; t.ie=t.oe=t.intr=0; t.im=t.om=0;
            if(chip==USR_ACM&&cls==2&&sub==2&&alt==0){ pending_ctl=cur; ctl=cur; pintr=0; }
            if(alt!=0)cur=-1; }
        else if(c[i+1]==5&&bl>=7&&cur>=0){ u8 a=c[i+2], ty=c[i+3]&3; u16 mp=(u16)((c[i+4]|(c[i+5]<<8))&0x7FF);
            if(ty==2&&mp){ if(a&0x80){ if(!t.ie){ t.ie=a; t.im=mp; } } else if(!t.oe){ t.oe=a; t.om=mp; } }
            else if(ty==3&&(a&0x80)){ if(chip==USR_ACM&&cur==pending_ctl)pintr=a; else if(!t.intr)t.intr=a; } }
        i+=bl; }
    if(cur>=0&&t.ie&&t.oe&&o->n<USR_MAXIF&&(chip!=USR_ACM||cls==0x0A)){ if(chip==USR_ACM){ t.ctl=(u8)(ctl>=0?ctl:t.ifn); t.intr=(u8)pintr; } o->p[o->n++]=t; }
    if(chip==USR_CH341||chip==USR_PL2303)if(o->n>1)o->n=1;
    return o->n; }
static inline int usr_ft_flags(u16 bcd,int nif){ int f=0; u16 h=bcd&0xFF00; if(h==0x0700||h==0x0800||h==0x0900)f|=USR_FT_HCLK; if(h==0x0500||h==0x0700||h==0x0800||h==0x0900)f|=USR_FT_HIX; if(nif>1)f|=USR_FT_MULTI|USR_FT_HIX; return f; }
static inline u32 usr_ftdi_div(u32 baud,int hclk){
    static const u8 fr[8]={0,3,2,4,1,5,6,7};
    if(!baud)return 0xFFFFFFFFu;
    u32 d3, d, hb=0;
    if(hclk&&baud>=1200){ if(baud>12000000u)return 0xFFFFFFFFu; d3=(96000000u+baud/2)/baud; hb=0x20000u; }
    else { if(baud>3000000u||baud<183)return 0xFFFFFFFFu; d3=(24000000u+baud/2)/baud; }
    if(d3>0x1FFFFu)return 0xFFFFFFFFu;
    d=(d3>>3)|((u32)fr[d3&7]<<14);
    if(d==1)d=0; else if(d==0x4001)d=1;
    return d|hb; }
static inline u32 usr_ftdi_rate(u32 enc,int hclk){
    static const u8 fi[8]={0,4,2,1,3,5,6,7};
    u32 base=(hclk&&(enc&0x20000u))?96000000u:24000000u, d=enc&0x3FFFu, f=(enc>>14)&7;
    u32 d3=(f==0&&d<2)?(d?12u:8u):d*8+fi[f]; if(!d3)return 0;
    return (base+d3/2)/d3; }
static inline int usr_ch341_div(u32 baud,u16* out){
    static const u32 base[4]={11719,93750,750000,6000000};
    if(baud<46||baud>2000000u)return 0;
    u64 best=~0ULL; int bp=-1, bf=0; u32 bd=0;
    for(int ps=3;ps>=0;ps--) for(int fact=(ps==3);fact>=0;fact--){ u32 bs=base[ps]<<fact, dv=(bs+baud/2)/baud; if(dv<2||dv>256||(fact&&(dv<9||!(dv&1))))continue;
        u32 r=(bs+dv/2)/dv; u64 e=r>baud?r-baud:baud-r; if(e<best){ best=e; bp=ps; bf=fact; bd=dv; } }
    if(bp<0||best*100>(u64)baud*3)return 0;
    *out=(u16)(((256-bd)<<8)|((u32)bf<<2)|(u32)bp|0x80u); return 1; }
static inline u32 usr_ch341_rate(u16 v){ static const u32 base[4]={11719,93750,750000,6000000}; u32 dv=256-(v>>8), bs=base[v&3]<<((v>>2)&1); if(!dv)return 0; return (bs+dv/2)/dv; }
static inline u8 usr_ch341_lcr(const struct usr_line* l){ u8 r=0xC0|(u8)((l->bits-5)&3); if(l->parity){ r|=0x08; if(l->parity==2)r|=0x10; else if(l->parity==3)r|=0x20; else if(l->parity==4)r|=0x30; } if(l->stop)r|=0x04; return r; }
static inline void usr_r(struct usr_req* q,u8 rt,u8 req,u16 val,u16 idx,u16 len){ q->rt=rt; q->req=req; q->val=val; q->idx=idx; q->len=len; for(int i=0;i<8;i++)q->data[i]=0; }
static inline int usr_line_ok(const struct usr_line* l){ return l->baud&&l->bits>=5&&l->bits<=8&&l->parity<=4&&l->stop<=2; }
static inline u16 usr_ftdi_data(const struct usr_line* l,int brk){ return (u16)(l->bits|((u16)l->parity<<8)|((u16)l->stop<<11)|(brk?0x4000:0)); }
static inline int usr_init_reqs(int chip,const struct usr_port* p,int ftf,struct usr_req* q){
    int n=0; u16 pi=(ftf&USR_FT_MULTI)?(u16)(p->ifn+1):0;
    switch(chip){
    case USR_FTDI: usr_r(&q[n++],0x40,0,0,pi,0); usr_r(&q[n++],0x40,2,0,pi,0); usr_r(&q[n++],0x40,9,2,pi,0); break;
    case USR_CP210X: usr_r(&q[n++],0x41,0x00,1,p->ifn,0); break;
    case USR_CH341: usr_r(&q[n++],0xC0,0x5F,0,0,2); usr_r(&q[n++],0x40,0xA1,0,0,0); break;
    case USR_PL2303: { static const u16 seq[][3]={{1,0x8484,0},{0,0x0404,0},{1,0x8484,0},{1,0x8383,0},{1,0x8484,0},{0,0x0404,1},{1,0x8484,0},{1,0x8383,0},{0,0,1},{0,1,0},{0,2,0x44}};
        for(unsigned i=0;i<sizeof seq/sizeof seq[0];i++){ if(seq[i][0])usr_r(&q[n++],0xC0,1,seq[i][1],0,1); else usr_r(&q[n++],0x40,1,seq[i][1],seq[i][2],0); } break; }
    default: break; }
    return n; }
static inline int usr_line_reqs(int chip,const struct usr_port* p,int ftf,const struct usr_line* l,struct usr_req* q){
    int n=0; if(!usr_line_ok(l))return -1;
    u16 pi=(ftf&USR_FT_MULTI)?(u16)(p->ifn+1):0;
    switch(chip){
    case USR_FTDI: { if(l->bits<7)return -1; u32 d=usr_ftdi_div(l->baud,ftf&USR_FT_HCLK); if(d==0xFFFFFFFFu)return -1;
        u16 ix=(ftf&USR_FT_HIX)?(u16)(((d>>16)<<8)|pi):(u16)(d>>16);
        usr_r(&q[n++],0x40,3,(u16)d,ix,0); usr_r(&q[n++],0x40,4,usr_ftdi_data(l,0),pi,0); break; }
    case USR_CP210X: { if(l->baud>2000000u&&l->baud!=3000000u)return -1; usr_r(&q[n],0x41,0x1E,0,p->ifn,4); q[n].data[0]=(u8)l->baud; q[n].data[1]=(u8)(l->baud>>8); q[n].data[2]=(u8)(l->baud>>16); q[n].data[3]=(u8)(l->baud>>24); n++;
        usr_r(&q[n++],0x41,0x03,(u16)(l->stop|((u16)l->parity<<4)|((u16)l->bits<<8)),p->ifn,0); break; }
    case USR_CH341: { u16 v; if(!usr_ch341_div(l->baud,&v))return -1; usr_r(&q[n++],0x40,0x9A,0x1312,v,0); usr_r(&q[n++],0x40,0x9A,0x2518,usr_ch341_lcr(l),0); break; }
    case USR_PL2303: case USR_ACM: { if(chip==USR_PL2303&&l->baud>12000000u)return -1; usr_r(&q[n],0x21,0x20,0,chip==USR_ACM?p->ctl:0,7); q[n].data[0]=(u8)l->baud; q[n].data[1]=(u8)(l->baud>>8); q[n].data[2]=(u8)(l->baud>>16); q[n].data[3]=(u8)(l->baud>>24); q[n].data[4]=l->stop; q[n].data[5]=l->parity; q[n].data[6]=l->bits; n++;
        if(chip==USR_PL2303)usr_r(&q[n++],0x40,1,0,0,0);
        break; }
    default: return -1; }
    return n; }
static inline int usr_mctl_req(int chip,const struct usr_port* p,int ftf,int dtr,int rts,struct usr_req* q){
    u16 pi=(ftf&USR_FT_MULTI)?(u16)(p->ifn+1):0;
    switch(chip){
    case USR_FTDI: usr_r(q,0x40,1,(u16)(0x0300|(dtr?1:0)|(rts?2:0)),pi,0); return 1;
    case USR_CP210X: usr_r(q,0x41,0x07,(u16)(0x0300|(dtr?1:0)|(rts?2:0)),p->ifn,0); return 1;
    case USR_CH341: usr_r(q,0x40,0xA4,(u16)(~((dtr?0x20:0)|(rts?0x40:0))&0xFF),0,0); return 1;
    case USR_PL2303: usr_r(q,0x21,0x22,(u16)((dtr?1:0)|(rts?2:0)),0,0); return 1;
    case USR_ACM: usr_r(q,0x21,0x22,(u16)((dtr?1:0)|(rts?2:0)),p->ctl,0); return 1; }
    return 0; }
static inline int usr_break_req(int chip,const struct usr_port* p,int ftf,const struct usr_line* l,int on,u8 r05,u8 r18,struct usr_req* q){
    u16 pi=(ftf&USR_FT_MULTI)?(u16)(p->ifn+1):0;
    switch(chip){
    case USR_FTDI: usr_r(q,0x40,4,usr_ftdi_data(l,on),pi,0); return 1;
    case USR_CP210X: usr_r(q,0x41,0x05,(u16)(on?1:0),p->ifn,0); return 1;
    case USR_CH341: { u8 a=on?(u8)(r05&~1u):(u8)(r05|1u), b=on?(u8)(r18&~0x40u):(u8)(r18|0x40u); usr_r(q,0x40,0x9A,0x1805,(u16)(a|((u16)b<<8)),0); return 1; }
    case USR_PL2303: usr_r(q,0x21,0x23,(u16)(on?0xFFFF:0),0,0); return 1;
    case USR_ACM: usr_r(q,0x21,0x23,(u16)(on?0xFFFF:0),p->ctl,0); return 1; }
    return 0; }
static inline int usr_modem_in(int chip,const u8* b,int n){
    int m=0; if(n<1)return -1;
    switch(chip){
    case USR_FTDI: if(b[0]&0x10)m|=USR_TIOCM_CTS; if(b[0]&0x20)m|=USR_TIOCM_DSR; if(b[0]&0x40)m|=USR_TIOCM_RNG; if(b[0]&0x80)m|=USR_TIOCM_CAR; return m;
    case USR_CP210X: if(b[0]&0x10)m|=USR_TIOCM_CTS; if(b[0]&0x20)m|=USR_TIOCM_DSR; if(b[0]&0x40)m|=USR_TIOCM_RNG; if(b[0]&0x80)m|=USR_TIOCM_CAR; return m;
    case USR_CH341: { u8 s=(u8)~b[0]; if(s&1)m|=USR_TIOCM_CTS; if(s&2)m|=USR_TIOCM_DSR; if(s&4)m|=USR_TIOCM_RNG; if(s&8)m|=USR_TIOCM_CAR; return m; } }
    return -1; }
static inline int usr_acm_notify(const u8* b,int n){
    if(n<10||b[0]!=0xA1||b[1]!=0x20)return -1;
    int s=b[8]|(b[9]<<8), m=0; if(s&1)m|=USR_TIOCM_CAR; if(s&2)m|=USR_TIOCM_DSR; if(s&8)m|=USR_TIOCM_RNG; return m; }
static inline int usr_ftdi_strip(const u8* in,int n,int mps,u8* out,int* st){
    int o=0; if(mps<3)mps=64;
    for(int i=0;i<n;i+=mps){ int c=n-i<mps?n-i:mps; if(c<2)break; if(st)*st=in[i]; for(int k=2;k<c;k++)out[o++]=in[i+k]; }
    return o; }
static inline u32 usr_cflag_baud(u32 cf,u32 ospeed){
    u32 b=cf&USR_CBAUD; if(b==USR_BOTHER)return ospeed;
    return usr_btab[(b&0xF)|((b&USR_CBAUDEX)?16:0)]; }
static inline u32 usr_baud_code(u32 baud){ for(int i=1;i<32;i++) if(i!=16&&usr_btab[i]==baud)return (u32)(i&15)|(i>=16?USR_CBAUDEX:0); return USR_BOTHER; }
static inline int usr_cflag_line(u32 cf,u32 ospeed,struct usr_line* l){
    l->baud=usr_cflag_baud(cf,ospeed); l->bits=(u8)(5+((cf&USR_CSIZE)>>4)); l->stop=(cf&USR_CSTOPB)?2:0;
    l->parity=0; if(cf&USR_PARENB){ if(cf&USR_CMSPAR)l->parity=(cf&USR_PARODD)?3:4; else l->parity=(cf&USR_PARODD)?1:2; }
    return l->baud!=0; }
#endif
