#ifndef NVX_GPAD_PROTO_H
#define NVX_GPAD_PROTO_H
enum { GP_NONE, GP_X360, GP_XONE, GP_DS4, GP_DS5, GP_SWPRO, GP_HID };
enum { GPB_S, GPB_E, GPB_W, GPB_N, GPB_LB, GPB_RB, GPB_BACK, GPB_START, GPB_GUIDE, GPB_LS, GPB_RS, GPB_DU, GPB_DD, GPB_DL, GPB_DR, GPB_MISC, GPB_TPAD, GPB_N_ };
struct gpst { u32 btn; int lx, ly, rx, ry, lt, rt; int bat, chg, tp, tpx, tpy, valid; };
struct gpfield { u16 pos, sz; int lo, hi; };
struct gphid { int ok, rid, nbtn, nax, hat; struct gpfield b[16], ax[6], h; u8 axu[6]; };
static inline u32 gp_bits(const u8* r,int n,int pos,int sz){ u32 v=0; for(int i=0;i<sz&&i<32;i++){ int b=pos+i; if((b>>3)>=n)break; v|=(u32)((r[b>>3]>>(b&7))&1)<<i; } return v; }
static inline int gp_s16(const u8* p){ return (int)(short)(u16)(p[0]|(p[1]<<8)); }
static inline int gp_u8ax(u8 v){ int x=((int)v-128)*256; if(v==255)x=32767; return x; }
static inline int gp_clamp(int v,int lo,int hi){ return v<lo?lo:v>hi?hi:v; }
static inline u32 gp_hat(int h){ static const u8 t[8]={1,1|8,8,8|2,2,2|4,4,4|1}; if(h<0||h>7)return 0; u32 m=t[h], o=0; if(m&1)o|=1u<<GPB_DU; if(m&2)o|=1u<<GPB_DD; if(m&4)o|=1u<<GPB_DL; if(m&8)o|=1u<<GPB_DR; return o; }
static inline int gp_ident(u16 vid,u16 pid,int cls,int sub,int proto){
    if(cls==0xFF&&sub==0x5D&&proto==0x01)return GP_X360;
    if(cls==0xFF&&sub==0x47&&proto==0xD0)return GP_XONE;
    if(vid==0x054C&&(pid==0x05C4||pid==0x09CC||pid==0x0BA0))return GP_DS4;
    if(vid==0x054C&&(pid==0x0CE6||pid==0x0DF2))return GP_DS5;
    if(vid==0x057E&&pid==0x2009)return GP_SWPRO;
    return GP_NONE; }
static inline int gp_x360_parse(const u8* r,int n,struct gpst* s){
    if(n<14||r[0]!=0x00||r[1]<0x14)return 0;
    u32 b=0; u8 a=r[2], c=r[3];
    if(a&1)b|=1u<<GPB_DU; if(a&2)b|=1u<<GPB_DD; if(a&4)b|=1u<<GPB_DL; if(a&8)b|=1u<<GPB_DR;
    if(a&16)b|=1u<<GPB_START; if(a&32)b|=1u<<GPB_BACK; if(a&64)b|=1u<<GPB_LS; if(a&128)b|=1u<<GPB_RS;
    if(c&1)b|=1u<<GPB_LB; if(c&2)b|=1u<<GPB_RB; if(c&4)b|=1u<<GPB_GUIDE; if(c&16)b|=1u<<GPB_S; if(c&32)b|=1u<<GPB_E; if(c&64)b|=1u<<GPB_W; if(c&128)b|=1u<<GPB_N;
    s->btn=b; s->lt=r[4]; s->rt=r[5]; s->lx=gp_s16(r+6); s->ly=gp_clamp(-gp_s16(r+8)-1,-32768,32767); s->rx=gp_s16(r+10); s->ry=gp_clamp(-gp_s16(r+12)-1,-32768,32767); s->valid=1; return 1; }
static inline int gp_x360_rumble(u8* o,int strong,int weak){ u8 t[8]={0x00,0x08,0x00,(u8)strong,(u8)weak,0,0,0}; for(int i=0;i<8;i++)o[i]=t[i]; return 8; }
static inline int gp_x360_led(u8* o,int player){ o[0]=0x01; o[1]=0x03; o[2]=(u8)(player>=1&&player<=4?5+player:0); return 3; }
static inline int gp_xone_init(u8* o,u8 seq){ o[0]=0x05; o[1]=0x20; o[2]=seq; o[3]=0x01; o[4]=0x00; return 5; }
static inline int gp_xone_led(u8* o,u8 seq){ u8 t[7]={0x0A,0x20,seq,0x03,0x00,0x01,0x14}; for(int i=0;i<7;i++)o[i]=t[i]; return 7; }
static inline int gp_xone_rumble(u8* o,u8 seq,int strong,int weak){ u8 t[13]={0x09,0x00,seq,0x09,0x00,0x0F,0x00,0x00,(u8)(strong*100/255),(u8)(weak*100/255),0xFF,0x00,0xFF}; for(int i=0;i<13;i++)o[i]=t[i]; return 13; }
static inline int gp_xone_ack(const u8* r,int n,u8* o){ if(n<4||!(r[1]&0x10))return 0; u8 t[13]={0x01,0x20,r[2],0x09,0x00,r[0],0x20,r[3],0,0,0,0,0}; for(int i=0;i<13;i++)o[i]=t[i]; return 13; }
static inline int gp_xone_parse(const u8* r,int n,struct gpst* s){
    if(n>=5&&r[0]==0x07){ if(r[4]&1)s->btn|=1u<<GPB_GUIDE; else s->btn&=~(1u<<GPB_GUIDE); s->valid=1; return 1; }
    if(n<18||r[0]!=0x20)return 0;
    u32 b=s->btn&(1u<<GPB_GUIDE); u8 a=r[4], c=r[5];
    if(a&4)b|=1u<<GPB_START; if(a&8)b|=1u<<GPB_BACK; if(a&16)b|=1u<<GPB_S; if(a&32)b|=1u<<GPB_E; if(a&64)b|=1u<<GPB_W; if(a&128)b|=1u<<GPB_N;
    if(c&1)b|=1u<<GPB_DU; if(c&2)b|=1u<<GPB_DD; if(c&4)b|=1u<<GPB_DL; if(c&8)b|=1u<<GPB_DR; if(c&16)b|=1u<<GPB_LB; if(c&32)b|=1u<<GPB_RB; if(c&64)b|=1u<<GPB_LS; if(c&128)b|=1u<<GPB_RS;
    s->btn=b; s->lt=gp_clamp((r[6]|(r[7]<<8))>>2,0,255); s->rt=gp_clamp((r[8]|(r[9]<<8))>>2,0,255);
    s->lx=gp_s16(r+10); s->ly=gp_clamp(-gp_s16(r+12)-1,-32768,32767); s->rx=gp_s16(r+14); s->ry=gp_clamp(-gp_s16(r+16)-1,-32768,32767); s->valid=1; return 1; }
static inline void gp_ps_face(u8 f,u8 g,u8 h,struct gpst* s){
    u32 b=gp_hat(f&15);
    if(f&16)b|=1u<<GPB_W; if(f&32)b|=1u<<GPB_S; if(f&64)b|=1u<<GPB_E; if(f&128)b|=1u<<GPB_N;
    if(g&1)b|=1u<<GPB_LB; if(g&2)b|=1u<<GPB_RB; if(g&16)b|=1u<<GPB_BACK; if(g&32)b|=1u<<GPB_START; if(g&64)b|=1u<<GPB_LS; if(g&128)b|=1u<<GPB_RS;
    if(h&1)b|=1u<<GPB_GUIDE; if(h&2)b|=1u<<GPB_TPAD; if(h&4)b|=1u<<GPB_MISC;
    s->btn=b; }
static inline void gp_ps_touch(const u8* t,struct gpst* s){ s->tp=!(t[0]&0x80); if(s->tp){ s->tpx=t[1]|((t[2]&15)<<8); s->tpy=(t[2]>>4)|(t[3]<<4); } }
static inline int gp_ds4_parse(const u8* r,int n,struct gpst* s){
    int o; if(r[0]==0x01&&n>=40)o=1; else if(r[0]==0x11&&n>=42)o=3; else if(r[0]==0x01&&n>=10)o=1; else return 0;
    s->lx=gp_u8ax(r[o]); s->ly=gp_u8ax(r[o+1]); s->rx=gp_u8ax(r[o+2]); s->ry=gp_u8ax(r[o+3]);
    gp_ps_face(r[o+4],r[o+5],r[o+6]&3,s);
    s->lt=r[o+7]; s->rt=r[o+8];
    if(n>=o+38){ u8 bt=r[o+29]; int lv=bt&15; s->chg=(bt&16)?1:0; s->bat=s->chg?(lv>=11?100:gp_clamp(lv*10,0,100)):gp_clamp(lv*10+5,0,100); gp_ps_touch(r+o+34,s); }
    s->valid=1; return 1; }
static inline u32 gp_crc32(u32 c,const u8* p,int n){ c=~c; for(int i=0;i<n;i++){ c^=p[i]; for(int k=0;k<8;k++)c=(c>>1)^(0xEDB88320u&(0u-(c&1))); } return ~c; }
static inline void gp_bt_crc(u8* o,int n){ u8 h=0xA2; u32 c=gp_crc32(0,&h,1); c=gp_crc32(c,o,n-4); o[n-4]=(u8)c; o[n-3]=(u8)(c>>8); o[n-2]=(u8)(c>>16); o[n-1]=(u8)(c>>24); }
static inline int gp_ds4_out(u8* o,int bt,int strong,int weak,u8 r,u8 g,u8 b){
    if(!bt){ for(int i=0;i<32;i++)o[i]=0; o[0]=0x05; o[1]=0x07; o[2]=0x04; o[4]=(u8)weak; o[5]=(u8)strong; o[6]=r; o[7]=g; o[8]=b; return 32; }
    for(int i=0;i<78;i++)o[i]=0; o[0]=0x11; o[1]=0xC0; o[2]=0x20; o[3]=0x07; o[6]=(u8)weak; o[7]=(u8)strong; o[8]=r; o[9]=g; o[10]=b; gp_bt_crc(o,78); return 78; }
static inline int gp_ds5_parse(const u8* r,int n,struct gpst* s){
    int o; if(r[0]==0x01&&n>=55)o=1; else if(r[0]==0x31&&n>=56)o=2; else if(r[0]==0x01&&n>=10&&n<55){ s->lx=gp_u8ax(r[1]); s->ly=gp_u8ax(r[2]); s->rx=gp_u8ax(r[3]); s->ry=gp_u8ax(r[4]); gp_ps_face(r[5],r[6],r[7]&3,s); s->lt=r[8]; s->rt=r[9]; s->valid=1; return 1; } else return 0;
    s->lx=gp_u8ax(r[o]); s->ly=gp_u8ax(r[o+1]); s->rx=gp_u8ax(r[o+2]); s->ry=gp_u8ax(r[o+3]); s->lt=r[o+4]; s->rt=r[o+5];
    gp_ps_face(r[o+7],r[o+8],r[o+9],s); gp_ps_touch(r+o+32,s);
    u8 bt=r[o+52]; int lv=bt&15, st=bt>>4; s->chg=(st==1||st==2); s->bat=st==2?100:gp_clamp(lv*10+5,0,100);
    s->valid=1; return 1; }
static inline int gp_ds5_out(u8* o,int bt,u8 seq,int strong,int weak,u8 r,u8 g,u8 b,int player){
    static const u8 pl[5]={0,0x04,0x0A,0x15,0x1B};
    int n=bt?78:48, c=bt?3:1; for(int i=0;i<n;i++)o[i]=0;
    if(bt){ o[0]=0x31; o[1]=(u8)(seq<<4); o[2]=0x10; } else o[0]=0x02;
    o[c]=0x03; o[c+1]=0x14; o[c+2]=(u8)weak; o[c+3]=(u8)strong; o[c+38]=0x02; o[c+43]=pl[player>=0&&player<=4?player:0]; o[c+44]=r; o[c+45]=g; o[c+46]=b;
    if(bt)gp_bt_crc(o,n); return n; }
static inline int gp_sw_hs(int step,u8* o){ static const u8 st[4]={0x02,0x03,0x02,0x04}; if(step<0||step>3)return 0; o[0]=0x80; o[1]=st[step]; return 2; }
static inline void gp_sw_rum(u8* o,int strong,int weak){ int hf=(strong*0xC8/255)&0xFE, lf=weak*0x72/255; o[0]=0x00; o[1]=(u8)(0x01|hf); o[2]=(u8)(0x40|((lf&1)<<7)); o[3]=(u8)(0x40+(lf>>1)); o[4]=o[0]; o[5]=o[1]; o[6]=o[2]; o[7]=o[3]; }
static inline int gp_sw_sub(u8* o,u8 cnt,u8 cmd,const u8* arg,int an){ for(int i=0;i<64;i++)o[i]=0; o[0]=0x01; o[1]=(u8)(cnt&15); gp_sw_rum(o+2,0,0); o[10]=cmd; for(int i=0;i<an&&i<50;i++)o[11+i]=arg[i]; return 64; }
static inline int gp_sw_rumble(u8* o,u8 cnt,int strong,int weak){ for(int i=0;i<10;i++)o[i]=0; o[0]=0x10; o[1]=(u8)(cnt&15); gp_sw_rum(o+2,strong,weak); return 10; }
static inline int gp_sw_stick(int v){ return gp_clamp((v-2048)*20,-32768,32767); }
static inline int gp_sw_parse(const u8* r,int n,struct gpst* s){
    if(n<12||r[0]!=0x30)return 0;
    u8 a=r[3], c=r[4], d=r[5]; u32 b=0;
    if(a&1)b|=1u<<GPB_W; if(a&2)b|=1u<<GPB_N; if(a&4)b|=1u<<GPB_S; if(a&8)b|=1u<<GPB_E; if(a&64)b|=1u<<GPB_RB;
    if(c&1)b|=1u<<GPB_BACK; if(c&2)b|=1u<<GPB_START; if(c&4)b|=1u<<GPB_RS; if(c&8)b|=1u<<GPB_LS; if(c&16)b|=1u<<GPB_GUIDE; if(c&32)b|=1u<<GPB_MISC;
    if(d&1)b|=1u<<GPB_DD; if(d&2)b|=1u<<GPB_DU; if(d&4)b|=1u<<GPB_DR; if(d&8)b|=1u<<GPB_DL; if(d&64)b|=1u<<GPB_LB;
    s->btn=b; s->lt=(d&128)?255:0; s->rt=(a&128)?255:0;
    s->lx=gp_sw_stick(r[6]|((r[7]&15)<<8)); s->ly=gp_clamp(-gp_sw_stick((r[7]>>4)|(r[8]<<4))-1,-32768,32767);
    s->rx=gp_sw_stick(r[9]|((r[10]&15)<<8)); s->ry=gp_clamp(-gp_sw_stick((r[10]>>4)|(r[11]<<4))-1,-32768,32767);
    int lv=r[2]>>5; s->chg=(r[2]>>4)&1; s->bat=gp_clamp(lv*25,0,100); s->valid=1; return 1; }
static inline int gp_hid_desc(const u8* d,int n,struct gphid* h){
    for(int i=0;i<(int)sizeof *h;i++)((u8*)h)[i]=0;
    int up=0, lmin=0, lmax=0, rsz=0, rcnt=0, rid=0, nu=0, umin=-1, umax=-1, coll=0, isgp=0, gid=-1; u32 pos[256]; u16 us[16]; for(int i=0;i<256;i++)pos[i]=0;
    int i=0; while(i<n){ u8 p=d[i]; if(p==0xFE){ if(i+2>=n)break; i+=3+d[i+1]; continue; }
        int sz=(p&3)==3?4:(p&3); if(i+1+sz>n)break; u32 v=0; for(int k=0;k<sz;k++)v|=(u32)d[i+1+k]<<(8*k);
        int sv=sz==1?(int)(signed char)v:sz==2?(int)(short)v:(int)v; int tag=p&0xFC; i+=1+sz;
        if(tag==0x04)up=(int)v; else if(tag==0x14)lmin=sv; else if(tag==0x24)lmax=(lmin>=0&&sz<4)?(int)v:sv; else if(tag==0x74)rsz=(int)(v>32?32:v); else if(tag==0x94)rcnt=(int)(v>256?256:v); else if(tag==0x84)rid=(int)(v&255);
        else if(tag==0x08){ u32 u=sz==4?v:((u32)up<<16)|v; if(nu<16)us[nu++]=(u16)u; if(coll==0)isgp=(u==0x10004||u==0x10005); }
        else if(tag==0x18)umin=(int)v; else if(tag==0x28)umax=(int)v;
        else if(tag==0xA0){ if(coll==0&&isgp&&gid<0)gid=rid; coll++; nu=0; umin=umax=-1; }
        else if(tag==0xC0){ if(coll>0)coll--; }
        else if(tag==0x80){ u32 bp=pos[rid&255]; int cnst=v&1, var=(v>>1)&1;
            if(isgp&&!cnst&&var&&(h->rid==rid||!h->ok)){
                for(int k=0;k<rcnt;k++){ int u=-1; if(umin>=0&&umax>=umin){ if(umin+k<=umax)u=umin+k; } else if(nu)u=us[k<nu?k:nu-1];
                    if(bp+(u32)(k*rsz)>=8192)break; struct gpfield f={(u16)(bp+(u32)(k*rsz)),(u16)rsz,lmin,lmax};
                    if(up==9&&u>=1&&u<=16&&h->nbtn<16){ h->b[h->nbtn++]=f; h->ok=1; h->rid=rid; }
                    else if(up==1&&u>=0x30&&u<=0x35&&h->nax<6){ h->axu[h->nax]=(u8)(u-0x30); h->ax[h->nax++]=f; h->ok=1; h->rid=rid; }
                    else if(up==1&&u==0x39&&!h->hat){ h->h=f; h->hat=1; h->ok=1; h->rid=rid; } } }
            pos[rid&255]=bp+(u32)(rsz*rcnt); if(pos[rid&255]>8192)pos[rid&255]=8192; nu=0; umin=umax=-1; }
        else if(tag==0x90||tag==0xB0){ nu=0; umin=umax=-1; } }
    return h->ok; }
static inline int gp_hid_val(const u8* r,int n,const struct gpfield* f){ u32 v=gp_bits(r,n,f->pos,f->sz); if(f->lo<0&&f->sz>0&&f->sz<32&&(v>>(f->sz-1))&1)v|=~0u<<f->sz; return (int)v; }
static inline int gp_hid_scale(int v,int lo,int hi,int a,int b){ if(hi<=lo)return (a+b)/2; long long x=(long long)(gp_clamp(v,lo,hi)-lo)*(b-a)/(hi-lo)+a; return (int)x; }
static inline int gp_hid_parse(const struct gphid* h,const u8* r,int n,struct gpst* s){
    if(!h->ok||n<1)return 0;
    if(h->rid){ if(r[0]!=h->rid)return 0; r++; n--; }
    static const u8 bm[16]={GPB_S,GPB_E,GPB_W,GPB_N,GPB_LB,GPB_RB,GPB_BACK,GPB_START,GPB_LS,GPB_RS,GPB_GUIDE,GPB_MISC,GPB_TPAD,GPB_N_,GPB_N_,GPB_N_};
    u32 b=0; for(int k=0;k<h->nbtn;k++) if(gp_bits(r,n,h->b[k].pos,1)&&bm[k]<GPB_N_)b|=1u<<bm[k];
    if(h->hat){ int v=gp_hid_val(r,n,&h->h)-h->h.lo; b|=gp_hat(v); }
    s->btn=b;
    for(int k=0;k<h->nax;k++){ int v=gp_hid_val(r,n,&h->ax[k]); int u=h->axu[k];
        if(u==3||u==4){ int t=gp_hid_scale(v,h->ax[k].lo,h->ax[k].hi,0,255); if(u==3)s->lt=t; else s->rt=t; }
        else { int a=gp_hid_scale(v,h->ax[k].lo,h->ax[k].hi,-32768,32767); if(u==0)s->lx=a; else if(u==1)s->ly=a; else if(u==2)s->rx=a; else s->ry=a; } }
    s->valid=1; return 1; }
#endif
