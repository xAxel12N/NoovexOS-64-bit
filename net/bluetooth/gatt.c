#include "include/nvx/base.h"
#include "net/bluetooth/bt_hid.h"
static int g_gerr;
static u8 g_rsp[BT_L2_MTU];
static u16 g16(const u8* p){ return (u16)(p[0]|p[1]<<8); }
static void p16(u8* p,u16 v){ p[0]=(u8)v; p[1]=(u8)(v>>8); }
int gatt_err(void){ return g_gerr; }
static int gq(btconn_t* c,const u8* q,int n,u8 want,int ms){
    g_gerr=0; int r=att_req(c,q,n,g_rsp,(int)sizeof g_rsp,ms);
    if(r<1){ g_gerr=-1; return -1; }
    if(g_rsp[0]==0x01){ g_gerr=r>=5?g_rsp[4]:-1; return -1; }
    return g_rsp[0]==want?r:-1;
}
static u16 uuid_of(const u8* p,int len){
    if(len==2)return g16(p);
    static const u8 base[12]={0xFB,0x34,0x9B,0x5F,0x80,0x00,0x00,0x80,0x00,0x10,0x00,0x00};
    if(len!=16||p[14]||p[15])return 0;
    for(int i=0;i<12;i++) if(p[i]!=base[i])return 0;
    return g16(p+12);
}
int gatt_exchange_mtu(btconn_t* c,u16 want,int ms){
    u8 q[3]={0x02}; p16(q+1,want);
    int r=gq(c,q,3,0x03,ms); if(r<3){ att_set_mtu(c,23); return 23; }
    u16 m=g16(g_rsp+1); if(m>want)m=want; att_set_mtu(c,m); return att_mtu(c);
}
int gatt_find_svc(btconn_t* c,u16 uuid,u16* start,u16* end,int ms){
    u16 h=1;
    for(int it=0;it<64&&h;it++){
        u8 q[7]={0x10}; p16(q+1,h); p16(q+3,0xFFFF); p16(q+5,0x2800);
        int r=gq(c,q,7,0x11,ms); if(r<2)return 0;
        int el=g_rsp[1]; if(el<6)return 0;
        u16 last=0;
        for(int o=2;o+el<=r;o+=el){ u16 s=g16(g_rsp+o), e=g16(g_rsp+o+2); last=e;
            if(uuid_of(g_rsp+o+4,el-4)==uuid){ *start=s; *end=e; return 1; } }
        if(!last||last==0xFFFF)return 0;
        h=(u16)(last+1);
    }
    return 0;
}
int gatt_chars(btconn_t* c,u16 start,u16 end,gchr_t* out,int max,int ms){
    int k=0; u16 h=start;
    for(int it=0;it<64&&h&&h<=end&&k<max;it++){
        u8 q[7]={0x08}; p16(q+1,h); p16(q+3,end); p16(q+5,0x2803);
        int r=gq(c,q,7,0x09,ms); if(r<2)break;
        int el=g_rsp[1]; if(el<7)break;
        u16 last=0;
        for(int o=2;o+el<=r&&k<max;o+=el){ gchr_t* g=&out[k++]; g->decl=g16(g_rsp+o); g->props=g_rsp[o+2]; g->vh=g16(g_rsp+o+3); g->uuid=uuid_of(g_rsp+o+5,el-5); g->end=end; last=g->decl; }
        if(!last||last>=end)break;
        h=(u16)(last+1);
    }
    for(int i=0;i+1<k;i++)out[i].end=(u16)(out[i+1].decl-1);
    return k;
}
int gatt_find_desc(btconn_t* c,u16 from,u16 to,u16 uuid,int ms){
    u16 h=from;
    for(int it=0;it<32&&h&&h<=to;it++){
        u8 q[5]={0x04}; p16(q+1,h); p16(q+3,to);
        int r=gq(c,q,5,0x05,ms); if(r<2)return 0;
        int el=g_rsp[1]==1?4:18; u16 last=0;
        for(int o=2;o+el<=r;o+=el){ last=g16(g_rsp+o); if(uuid_of(g_rsp+o+2,el-2)==uuid)return last; }
        if(!last||last>=to)return 0;
        h=(u16)(last+1);
    }
    return 0;
}
int gatt_read(btconn_t* c,u16 h,u8* out,int cap,int ms){
    u8 q[5]={0x0A}; p16(q+1,h);
    int r=gq(c,q,3,0x0B,ms); if(r<1)return -1;
    int n=0, mtu=att_mtu(c);
    for(int i=1;i<r&&n<cap;i++)out[n++]=g_rsp[i];
    int got=r-1;
    for(int it=0;it<64&&got==mtu-1&&n<cap;it++){
        q[0]=0x0C; p16(q+3,(u16)n);
        r=gq(c,q,5,0x0D,ms); if(r<1){ if(g_gerr==0x07||g_gerr==0x0B)break; return n?n:-1; }
        got=r-1; for(int i=1;i<r&&n<cap;i++)out[n++]=g_rsp[i];
    }
    return n;
}
int gatt_write(btconn_t* c,u16 h,const u8* p,int n,int ms){
    u8 q[64]; if(n<0||n>(int)sizeof q-3||n>att_mtu(c)-3)return -1;
    q[0]=0x12; p16(q+1,h); for(int i=0;i<n;i++)q[3+i]=p[i];
    return gq(c,q,n+3,0x13,ms)>=1?0:-1;
}
int gatt_write_cmd(btconn_t* c,u16 h,const u8* p,int n){
    u8 q[64]; if(n<0||n>(int)sizeof q-3||n>att_mtu(c)-3)return -1;
    q[0]=0x52; p16(q+1,h); for(int i=0;i<n;i++)q[3+i]=p[i];
    return att_cmd(c,q,n+3);
}
