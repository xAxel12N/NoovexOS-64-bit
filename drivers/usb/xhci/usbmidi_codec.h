#ifndef NVX_USBMIDI_CODEC_H
#define NVX_USBMIDI_CODEC_H
struct umc_enc { u8 b[3]; int n, need, sx; u8 run, cable; };
static const u8 umc_cinlen[16]={0,0,2,3,3,1,2,3,3,3,3,3,2,2,3,1};
static int umc_msglen(u8 st){ if(st<0x80)return 0; if(st<0xF0)return (st&0xE0)==0xC0?2:3; switch(st){ case 0xF1: case 0xF3: return 2; case 0xF2: return 3; case 0xF0: return -1; default: return 1; } }
static int umc_decode(const u8* p,u8* out){
    int cin=p[0]&0x0F, n=umc_cinlen[cin];
    if(cin==0||cin==1)return 0;
    if(cin>=8&&cin<=0x0E){ if(p[1]<0x80||p[1]>=0xF0)return 0; n=umc_msglen(p[1]); }
    if(cin==0x0F&&p[1]<0x80){ out[0]=p[1]; return 1; }
    for(int i=0;i<n;i++)out[i]=p[1+i];
    return n; }
static int umc_pkt(struct umc_enc* e,u8 cin,u8* pk){ pk[0]=(u8)((e->cable<<4)|cin); pk[1]=e->n>0?e->b[0]:0; pk[2]=e->n>1?e->b[1]:0; pk[3]=e->n>2?e->b[2]:0; e->n=0; return 1; }
static void umc_init(struct umc_enc* e,u8 cable){ e->n=0; e->need=0; e->sx=0; e->run=0; e->cable=(u8)(cable&15); }
static int umc_encode(struct umc_enc* e,u8 c,u8* pk){
    if(c>=0xF8){ pk[0]=(u8)((e->cable<<4)|0x0F); pk[1]=c; pk[2]=0; pk[3]=0; return 1; }
    if(c==0xF0){ e->sx=1; e->run=0; e->n=0; e->b[e->n++]=c; return 0; }
    if(e->sx){
        if(c==0xF7){ e->b[e->n++]=c; e->sx=0; return umc_pkt(e,(u8)(4+e->n),pk); }
        if(c>=0x80){ e->sx=0; e->n=0; }
        else { e->b[e->n++]=c; if(e->n==3)return umc_pkt(e,4,pk); return 0; } }
    if(c>=0x80){
        int l=umc_msglen(c); e->n=0;
        if(c<0xF0)e->run=c; else e->run=0;
        if(l==1){ if(c==0xF6){ e->b[0]=c; e->n=1; return umc_pkt(e,5,pk); } return 0; }
        e->b[e->n++]=c; e->need=l; return 0; }
    if(!e->n){ if(!e->run)return 0; e->b[e->n++]=e->run; e->need=umc_msglen(e->run); }
    e->b[e->n++]=c;
    if(e->n<e->need)return 0;
    { u8 st=e->b[0]; u8 cin=st<0xF0?(u8)(st>>4):(u8)(e->need==2?2:3); return umc_pkt(e,cin,pk); } }
struct umc_cfg { int ifn, alt, ie, im, oe, om, ncin, ncout; };
static int umc_parse_cfg(const u8* c,int n,struct umc_cfg* o){
    int cls=0, sub=0, cif=-1, calt=0, found=0, last=0;
    o->ifn=o->alt=o->ie=o->im=o->oe=o->om=0; o->ncin=o->ncout=1;
    if(n<9||c[0]<9||c[1]!=2)return 0;
    for(int i=0;i+2<=n;){ int bl=c[i]; if(bl<2||i+bl>n)break; int t=c[i+1];
        if(t==4){ last=0; if(bl<9){ i+=bl; continue; } if(found&&(o->ie||o->oe))break; cif=c[i+2]; calt=c[i+3]; cls=c[i+5]; sub=c[i+6]; if(cls==1&&sub==3&&(!found||c[i+4])){ found=1; o->ifn=cif; o->alt=calt; o->ie=o->oe=0; o->ncin=o->ncout=1; } }
        else if(t==5){ last=0; if(bl>=7&&cls==1&&sub==3&&found&&o->ifn==cif&&o->alt==calt&&(c[i+3]&3)==2&&(c[i+2]&0x0F)){ int a=c[i+2], m=(c[i+4]|(c[i+5]<<8))&0x7FF; if(m>=4){ if(a&0x80){ if(!o->ie){ o->ie=a; o->im=m; last=1; } } else if(!o->oe){ o->oe=a; o->om=m; last=2; } } } }
        else if(t==0x25&&last&&bl>=4&&c[i+2]==1){ int nj=c[i+3]; if(nj>bl-4)nj=bl-4; if(nj<1)nj=1; if(nj>16)nj=16; if(last==1)o->ncin=nj; else o->ncout=nj; last=0; }
        i+=bl; }
    return found&&(o->ie||o->oe); }
#endif
