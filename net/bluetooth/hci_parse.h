#ifndef NVX_BT_HCI_PARSE_H
#define NVX_BT_HCI_PARSE_H
#define BTD_MAX 40
#define BTD_NAME 49
typedef struct { u8 addr[6]; u8 le, atype, nameq, psrm; int rssi; u32 cod; u16 clkoff, appear; u32 hits; char name[BTD_NAME]; } btdev_t;
typedef struct { btdev_t d[BTD_MAX]; int n; u32 drops; } btlist_t;
static u16 bth_g16(const u8* p){ return (u16)(p[0]|(p[1]<<8)); }
static u32 bth_g24(const u8* p){ return (u32)p[0]|((u32)p[1]<<8)|((u32)p[2]<<16); }
static void bth_name(btdev_t* d,const u8* s,int n,int q){
    if(q<d->nameq||n<=0)return;
    int o=0; for(int i=0;i<n&&o<BTD_NAME-1;i++){ u8 c=s[i]; if(!c)break; if(c<32||c==127)c=' '; else if(c>=0x80){ if((c&0xC0)==0x80)continue; c='?'; } d->name[o++]=(char)c; }
    while(o>0&&d->name[o-1]==' ')o--;
    if(!o)return;
    d->name[o]=0; d->nameq=(u8)q;
}
static void bth_eir(btdev_t* d,const u8* p,int n){
    for(int i=0;i<n;){ int l=p[i]; if(!l||i+1+l>n)break; int t=p[i+1]; const u8* v=p+i+2; int vl=l-1;
        if(t==0x09)bth_name(d,v,vl,2); else if(t==0x08)bth_name(d,v,vl,1);
        else if(t==0x19&&vl>=2)d->appear=bth_g16(v);
        else if(t==0x0D&&vl>=3)d->cod=bth_g24(v);
        i+=1+l; }
}
static btdev_t* btl_get(btlist_t* L,const u8* a,int le,int atype){
    for(int i=0;i<L->n;i++){ btdev_t* d=&L->d[i]; int k=0; while(k<6&&d->addr[k]==a[k])k++; if(k==6&&d->le==(u8)le&&(!le||d->atype==(u8)atype))return d; }
    if(L->n>=BTD_MAX){ L->drops++; return 0; }
    btdev_t* d=&L->d[L->n++]; u8* z=(u8*)d; for(int i=0;i<(int)sizeof *d;i++)z[i]=0;
    for(int k=0;k<6;k++)d->addr[k]=a[k];
    d->le=(u8)le; d->atype=(u8)atype; d->rssi=127;
    return d;
}
static void bth_rssi(btdev_t* d,int r){ if(r>=127||r<-127)return; if(d->rssi==127||r>d->rssi)d->rssi=r; }
static int hci_evt_parse(btlist_t* L,const u8* e,int n){
    if(n<2)return -1;
    int code=e[0], pl=e[1]; if(pl+2>n)return -1;
    const u8* p=e+2;
    if(code==0x02){ if(pl<1)return -1; int num=p[0]; if(num*14+1>pl)return -1;
        for(int i=0;i<num;i++){ const u8* r=p+1+i*14; btdev_t* d=btl_get(L,r,0,0); if(!d)continue; d->psrm=r[6]; d->cod=bth_g24(r+9); d->clkoff=bth_g16(r+12); d->hits++; } }
    else if(code==0x22){ if(pl<1)return -1; int num=p[0]; if(!num)return code; int st=(pl-1)/num; if(st!=14&&st!=15)return -1;
        for(int i=0;i<num;i++){ const u8* r=p+1+i*st; int o=st==15?1:0; btdev_t* d=btl_get(L,r,0,0); if(!d)continue; d->psrm=r[6]; d->cod=bth_g24(r+8+o); d->clkoff=bth_g16(r+11+o); bth_rssi(d,(int)(signed char)r[13+o]); d->hits++; } }
    else if(code==0x2F){ if(pl<15)return -1; const u8* r=p+1; btdev_t* d=btl_get(L,r,0,0); if(!d)return code;
        d->psrm=r[6]; d->cod=bth_g24(r+8); d->clkoff=bth_g16(r+11); bth_rssi(d,(int)(signed char)r[13]); d->hits++; bth_eir(d,r+14,pl-15); }
    else if(code==0x07){ if(pl<7)return -1; if(p[0]==0){ btdev_t* d=btl_get(L,p+1,0,0); if(d)bth_name(d,p+7,pl-7,3); } }
    else if(code==0x3E){ if(pl<1)return -1;
        if(p[0]==0x02){ if(pl<2)return -1; int num=p[1], o=2;
            for(int i=0;i<num;i++){ if(o+9>pl)return -1; int at=p[o+1], dl=p[o+8]; if(o+9+dl+1>pl)return -1;
                btdev_t* d=btl_get(L,p+o+2,1,at); if(d){ bth_eir(d,p+o+9,dl); bth_rssi(d,(int)(signed char)p[o+9+dl]); d->hits++; }
                o+=10+dl; } } }
    return code;
}
static int hci_cc_match(const u8* e,int n,u16 op){ if(n<5)return 0; if(e[0]==0x0E&&e[1]>=3&&bth_g16(e+3)==op)return 1; if(e[0]==0x0F&&e[1]>=4&&n>=6&&bth_g16(e+4)==op)return 2; return 0; }
static char* bth_addr(char* p,const u8* a){ static const char hx16[]="0123456789ABCDEF"; for(int i=5;i>=0;i--){ *p++=hx16[a[i]>>4]; *p++=hx16[a[i]&15]; if(i)*p++=':'; } *p=0; return p; }
static const char* bth_major(u32 cod){ switch((cod>>8)&0x1F){ case 1: return "computer"; case 2: return "phone"; case 3: return "network"; case 4: return "audio"; case 5: return "input"; case 6: return "imaging"; case 7: return "wearable"; case 8: return "toy"; case 9: return "health"; } return ""; }
#endif
