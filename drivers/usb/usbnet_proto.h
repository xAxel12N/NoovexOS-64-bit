#ifndef NVX_USBNET_PROTO_H
#define NVX_USBNET_PROTO_H
static void usn_q_put(const u8* f,int n);
static u16 usn_g16(const u8* p){ return (u16)(p[0]|(p[1]<<8)); }
static u32 usn_g32(const u8* p){ return (u32)p[0]|((u32)p[1]<<8)|((u32)p[2]<<16)|((u32)p[3]<<24); }
static void usn_p16(u8* p,u16 v){ p[0]=(u8)v; p[1]=(u8)(v>>8); }
static void usn_p32(u8* p,u32 v){ p[0]=(u8)v; p[1]=(u8)(v>>8); p[2]=(u8)(v>>16); p[3]=(u8)(v>>24); }
static int usn_hexv(int c){ if(c>='0'&&c<='9')return c-'0'; if(c>='a'&&c<='f')return c-'a'+10; if(c>='A'&&c<='F')return c-'A'+10; return -1; }
static int usn_strmac(const u8* s,int n,u8* mac){
    if(n<2||s[1]!=3)return 0;
    int len=s[0]; if(len>n)len=n; if(len<2+24)return 0;
    for(int i=0;i<6;i++){ int hi=usn_hexv(s[2+i*4]), lo=usn_hexv(s[2+i*4+2]); if(hi<0||lo<0||s[3+i*4]||s[3+i*4+2])return 0; mac[i]=(u8)(hi*16+lo); }
    if(mac[0]&1)return 0;
    int any=0; for(int i=0;i<6;i++)if(mac[i])any=1; return any;
}
static int ncm_build(u8* out,int cap,u16 seq,const u8* f,int len,int div,int rem){
    if(div<4||div>256)div=4;
    if(rem<0||rem>=div)rem=0;
    int dg=28; while(dg%div!=rem)dg++;
    int tot=dg+len; if(len<14||tot>cap)return -1;
    for(int i=0;i<dg;i++)out[i]=0;
    usn_p32(out,0x484D434Eu); usn_p16(out+4,12); usn_p16(out+6,seq); usn_p16(out+8,(u16)tot); usn_p16(out+10,12);
    usn_p32(out+12,0x304D434Eu); usn_p16(out+16,16); usn_p16(out+18,0); usn_p16(out+20,(u16)dg); usn_p16(out+22,(u16)len);
    for(int i=0;i<len;i++)out[dg+i]=f[i];
    return tot;
}
static int ncm_parse(const u8* b,int n){
    if(n<12||usn_g32(b)!=0x484D434Eu||usn_g16(b+4)!=12)return -1;
    int blk=usn_g16(b+8); if(blk==0||blk>n)blk=n;
    int ndp=usn_g16(b+10), frames=0;
    for(int guard=0;guard<16&&ndp;guard++){
        if(ndp<12||(ndp&3)||ndp+16>blk)return frames?frames:-1;
        u32 sig=usn_g32(b+ndp); int crc;
        if(sig==0x304D434Eu)crc=0; else if(sig==0x314D434Eu)crc=4; else return frames?frames:-1;
        int nl=usn_g16(b+ndp+4); if(nl<16||(nl&3)||ndp+nl>blk)return frames?frames:-1;
        for(int e=ndp+8;e+4<=ndp+nl;e+=4){
            int di=usn_g16(b+e), dl=usn_g16(b+e+2);
            if(!di||!dl)break;
            if(dl-crc<14||di+dl>blk)continue;
            usn_q_put(b+di,dl-crc); frames++;
        }
        ndp=usn_g16(b+ndp+6);
    }
    return frames;
}
static int rndis_wrap(u8* out,int cap,const u8* f,int len){
    int tot=44+len; if(len<14||tot>cap)return -1;
    for(int i=0;i<44;i++)out[i]=0;
    usn_p32(out,1); usn_p32(out+4,(u32)tot); usn_p32(out+8,36); usn_p32(out+12,(u32)len);
    for(int i=0;i<len;i++)out[44+i]=f[i];
    return tot;
}
static int rndis_parse(const u8* b,int n){
    int off=0, frames=0;
    for(int guard=0;guard<64&&off+44<=n;guard++){
        u32 ty=usn_g32(b+off), ml=usn_g32(b+off+4);
        if(ml<8||ml>(u32)(n-off))break;
        if(ty==1&&ml>=44){
            u32 doff=usn_g32(b+off+8), dlen=usn_g32(b+off+12);
            if(doff>=36&&dlen>=14&&dlen<=ml&&doff<=ml-8&&8+doff+dlen<=ml){ usn_q_put(b+off+8+doff,(int)dlen); frames++; }
        }
        off+=(int)ml;
    }
    return frames;
}
static int rndis_msg(u8* out,u32 type,u32 id,u32 oid,const u8* val,int vlen){
    int len=type==2?24:28+vlen;
    for(int i=0;i<len;i++)out[i]=0;
    usn_p32(out,type); usn_p32(out+4,(u32)len); usn_p32(out+8,id);
    if(type==2){ usn_p32(out+12,1); usn_p32(out+16,0); usn_p32(out+20,0x4000); }
    else { usn_p32(out+12,oid); usn_p32(out+16,(u32)vlen); usn_p32(out+20,vlen?20u:0u); for(int i=0;i<vlen;i++)out[28+i]=val[i]; }
    return len;
}
static int rndis_reply(const u8* b,int n,u32 want,u32 id,const u8** data,int* dlen){
    if(n<16)return -1;
    u32 ml=usn_g32(b+4); if(ml<16||ml>(u32)n)return -1;
    if(usn_g32(b)!=want||usn_g32(b+8)!=id)return -1;
    if(usn_g32(b+12)!=0)return 0;
    if(data&&dlen){ *data=0; *dlen=0;
        if(want==0x80000004u&&ml>=24){ u32 il=usn_g32(b+16), io=usn_g32(b+20); if(il<=ml&&io<=ml&&8+io+il<=ml){ *data=b+8+io; *dlen=(int)il; } } }
    return 1;
}
#endif
