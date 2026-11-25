#include "include/nvx/drivers/usb/usbnet_frame.h"
static u16 unf_g16(const u8* p){ return (u16)(p[0]|(p[1]<<8)); }
static u32 unf_g32(const u8* p){ return (u32)p[0]|((u32)p[1]<<8)|((u32)p[2]<<16)|((u32)p[3]<<24); }
static void unf_p16(u8* p,u16 v){ p[0]=(u8)v; p[1]=(u8)(v>>8); }
static void unf_p32(u8* p,u32 v){ p[0]=(u8)v; p[1]=(u8)(v>>8); p[2]=(u8)(v>>16); p[3]=(u8)(v>>24); }
static void unf_cp(u8* d,const u8* s,int n){ for(int i=0;i<n;i++)d[i]=s[i]; }
static void unf_zero(u8* d,int n){ for(int i=0;i<n;i++)d[i]=0; }
static void unf_emit(unf_ctx_t* c,unf_sink_t sink,void* arg,const u8* f,int n){ if(n<14||n>UNF_MTU_FRAME+4){ c->rx_bad++; return; } c->rx_frames++; if(sink)sink(arg,f,n); }
void unf_init(unf_ctx_t* c,int kind,int mps){ unf_zero((u8*)c,(int)sizeof *c); c->kind=kind; c->mps=mps; c->ndiv=4; c->nrem=0; }
const char* unf_kind_name(int k){ return k==UNF_ECM?"CDC-ECM":k==UNF_NCM?"CDC-NCM":k==UNF_RNDIS?"RNDIS":k==UNF_URE?"Realtek RTL815x":k==UNF_AXEN?"ASIX AX88179":k==UNF_IPHETH?"iPhone":"none"; }
int unf_tx_overhead(int k){ return k==UNF_NCM?32+256:k==UNF_RNDIS?44:k==UNF_URE?8+4:k==UNF_AXEN?8:k==UNF_IPHETH?2:0; }
int unf_rx_bufsize(int k){ return k==UNF_ECM?2048:k==UNF_IPHETH?UNF_IPHETH_BUF:16384; }
int unf_mac_ok(const u8* m){ if(m[0]&1)return 0; int any=0, ff=1; for(int i=0;i<6;i++){ if(m[i])any=1; if(m[i]!=0xFF)ff=0; } return any&&!ff; }
void unf_mac_local(u8* m,u64 s){ s=s*0x9E3779B97F4A7C15ULL+0x632BE59BD9B4E019ULL; for(int i=0;i<6;i++)m[i]=(u8)(s>>(8*i+3)); m[0]=(u8)((m[0]&0xFE)|0x02); if(!unf_mac_ok(m))m[5]=1; }
static int unf_tx_raw(unf_ctx_t* c,u8* out,int cap,const u8* f,int len){
    int div=c->ndiv, rem=c->nrem, n=-1;
    if(len<14||len>UNF_MTU_FRAME+4||cap<=0)return -1;
    switch(c->kind){
    case UNF_ECM: if(len>cap)return -1; unf_cp(out,f,len); return len;
    case UNF_NCM: {
        if(div<4||div>256)div=4;
        if(rem<0||rem>=div)rem=0;
        int dg=28; while(dg%div!=rem)dg++;
        n=dg+len; if(n>cap)return -1;
        unf_zero(out,dg);
        unf_p32(out,0x484D434Eu); unf_p16(out+4,12); unf_p16(out+6,c->seq++); unf_p16(out+8,(u16)n); unf_p16(out+10,12);
        unf_p32(out+12,0x304D434Eu); unf_p16(out+16,16); unf_p16(out+18,0); unf_p16(out+20,(u16)dg); unf_p16(out+22,(u16)len);
        unf_cp(out+dg,f,len); return n; }
    case UNF_RNDIS:
        n=44+len; if(n>cap)return -1;
        unf_zero(out,44); unf_p32(out,1); unf_p32(out+4,(u32)n); unf_p32(out+8,36); unf_p32(out+12,(u32)len);
        unf_cp(out+44,f,len); return n;
    case UNF_URE:
        n=8+len; if(n>cap)return -1;
        unf_p32(out,(u32)len|(1u<<31)|(1u<<30)); unf_p32(out+4,0); unf_cp(out+8,f,len);
        while((n&3)&&n<cap)out[n++]=0;
        return n;
    case UNF_AXEN:
        n=8+len; if(n>cap)return -1;
        unf_p32(out,(u32)len&0x1FFFFu); unf_p32(out+4,0); unf_cp(out+8,f,len); return n;
    case UNF_IPHETH:
        if(cap<UNF_IPHETH_BUF-2||len>UNF_IPHETH_BUF-2)return -1;
        unf_cp(out,f,len); unf_zero(out+len,UNF_IPHETH_BUF-2-len); return UNF_IPHETH_BUF-2;
    }
    return -1;
}
int unf_tx(unf_ctx_t* c,u8* out,int cap,const u8* f,int len){
    int n=unf_tx_raw(c,out,cap,f,len); if(n<0)return -1;
    if(c->mps>0&&n%c->mps==0&&c->kind!=UNF_IPHETH){ if(n>=cap)return -1; out[n++]=0; if(c->kind==UNF_NCM)unf_p16(out+8,(u16)n); else if(c->kind==UNF_RNDIS)unf_p32(out+4,(u32)n); }
    c->tx_frames++; return n;
}
static int unf_rx_ncm(unf_ctx_t* c,const u8* b,int n,unf_sink_t sink,void* arg){
    if(n<12||unf_g32(b)!=0x484D434Eu||unf_g16(b+4)!=12)return -1;
    int blk=unf_g16(b+8); if(blk==0||blk>n)blk=n;
    int ndp=unf_g16(b+10), fr=0;
    for(int g=0;g<16&&ndp;g++){
        if(ndp<12||(ndp&3)||ndp+16>blk)return fr?fr:-1;
        u32 sig=unf_g32(b+ndp); int crc;
        if(sig==0x304D434Eu)crc=0; else if(sig==0x314D434Eu)crc=4; else return fr?fr:-1;
        int nl=unf_g16(b+ndp+4); if(nl<16||(nl&3)||ndp+nl>blk)return fr?fr:-1;
        for(int e=ndp+8;e+4<=ndp+nl;e+=4){
            int di=unf_g16(b+e), dl=unf_g16(b+e+2);
            if(!di||!dl)break;
            if(dl-crc<14||di+dl>blk){ c->rx_bad++; continue; }
            unf_emit(c,sink,arg,b+di,dl-crc); fr++;
        }
        ndp=unf_g16(b+ndp+6);
    }
    return fr;
}
static int unf_rx_rndis(unf_ctx_t* c,const u8* b,int n,unf_sink_t sink,void* arg){
    int off=0, fr=0;
    for(int g=0;g<64&&off+44<=n;g++){
        u32 ty=unf_g32(b+off), ml=unf_g32(b+off+4);
        if(ml<8||ml>(u32)(n-off))break;
        if(ty==1&&ml>=44){
            u32 doff=unf_g32(b+off+8), dlen=unf_g32(b+off+12);
            if(doff>=36&&dlen>=14&&dlen<=ml&&doff<=ml-8&&8+doff+dlen<=ml){ unf_emit(c,sink,arg,b+off+8+doff,(int)dlen); fr++; } else c->rx_bad++;
        }
        off+=(int)ml;
    }
    return fr?fr:(n>0?-1:0);
}
static int unf_rx_ure(unf_ctx_t* c,const u8* b,int n,unf_sink_t sink,void* arg){
    int off=0, fr=0;
    for(int g=0;g<512&&n-off>0;g++){
        if(n-off<24){ c->rx_bad++; return fr?fr:-1; }
        int len=(int)(unf_g32(b+off)&0x7FFF); off+=24;
        if(len>=0x7FFF||len>n-off){ c->rx_bad++; return fr?fr:-1; }
        if(len>=18){ unf_emit(c,sink,arg,b+off,len-4); fr++; } else c->rx_bad++;
        off+=(len+7)&~7;
    }
    return fr;
}
static int unf_rx_axen(unf_ctx_t* c,const u8* b,int n,unf_sink_t sink,void* arg){
    if(n<4)return -1;
    u32 h=unf_g32(b+n-4); int cnt=(int)(h&0xFFFF), hoff=(int)(h>>16), end=hoff, pos=0, fr=0;
    if(hoff>n-4||cnt>512)return -1;
    while(cnt-->0){
        if(hoff+4>n-4)break;
        u32 st=unf_g32(b+hoff); int len=(int)((st&0x1FFF0000u)>>16); hoff+=4;
        if(!len)continue;
        if(pos+len>end)break;
        if(st&0xA0000000u)c->rx_bad++; else { unf_emit(c,sink,arg,b+pos,len); fr++; }
        pos+=(len+7)&~7;
    }
    return fr;
}
int unf_rx(unf_ctx_t* c,const u8* b,int n,unf_sink_t sink,void* arg){
    if(!b||n<=0)return 0;
    switch(c->kind){
    case UNF_ECM: unf_emit(c,sink,arg,b,n); return 1;
    case UNF_NCM: return unf_rx_ncm(c,b,n,sink,arg);
    case UNF_RNDIS: return unf_rx_rndis(c,b,n,sink,arg);
    case UNF_URE: return unf_rx_ure(c,b,n,sink,arg);
    case UNF_AXEN: return unf_rx_axen(c,b,n,sink,arg);
    case UNF_IPHETH: if(n<16){ c->rx_bad++; return -1; } unf_emit(c,sink,arg,b+2,n-2); return 1;
    }
    return -1;
}
