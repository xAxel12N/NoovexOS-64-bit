#include "include/nvx/base.h"
#include "include/nvx/graphics/theme/theme.h"
#include "include/nvx/drivers/input/touchpad/tp_hid.h"


static u32 tp_rd_u(const u8* p,int n){ u32 v=0; for(int i=0;i<n;i++)v|=(u32)p[i]<<(8*i); return v; }
static int tp_rd_s(const u8* p,int n){ u32 v=tp_rd_u(p,n); if(n==1)return (int)(int8_t)v; if(n==2)return (int)(int16_t)v; return (int)v; }
static u32 tp_unit_um(u32 unit,int exp,int range){
    long long um; if((unit&0xF)==1&&((unit>>4)&0xF)==1)um=10000; else if((unit&0xF)==3&&((unit>>4)&0xF)==1)um=25400; else return 0;
    if(range<=0)return 0; long long v=(long long)range*um; if(exp>0){ for(int i=0;i<exp;i++){ v*=10; if(v>2000000000LL)return 0; } } else for(int i=0;i<-exp;i++)v/=10;
    if(v<=0||v>2000000)return 0; return (u32)v; }
int g_tpp_items, g_tpp_skip, g_tpp_skip_off=-1, g_tpp_stop=-1, g_tpp_depth_err;
int tp_parse(const u8* d,int n,tplay* L){
    for(u32 i=0;i<sizeof(*L);i++)((u8*)L)[i]=0;
    g_tpp_items=0; g_tpp_skip=0; g_tpp_skip_off=-1; g_tpp_stop=-1; g_tpp_depth_err=0; int cov=0;
    if(!d||n<=0)return -1;
    struct { u32 page; int lmin,lmax,pmin,pmax,uexp; u32 unit; int rsize,rid,rcount; } G, GS[4]; int gsp=0;
    for(u32 i=0;i<sizeof(G);i++)((u8*)&G)[i]=0;
    u32 usage[32]; int nu=0; u32 umin=0, umax=0; int hasmin=0, hasmax=0;
    u32 coll[16]; int csp=0; int finger=-1;
    static int inbits[256], fbits[256]; for(int i=0;i<256;i++){ inbits[i]=0; fbits[i]=0; }
    int i=0;
    while(i<n){
        int at=i; u8 pre=d[i]; if(pre==0xFE){ if(i+2>=n||i+3+d[i+1]>n){ g_tpp_stop=i; break; } i+=3+d[i+1]; g_tpp_items++; continue; }
        int sz=pre&3; if(sz==3)sz=4; if(i+1+sz>n){ g_tpp_stop=i; break; }
        int type=(pre>>2)&3, tag=(pre>>4)&0xF; const u8* v=d+i+1; u32 uv=tp_rd_u(v,sz); int sv=sz?tp_rd_s(v,sz):0; i+=1+sz; g_tpp_items++;
        if(type==1){
            switch(tag){ case 0: G.page=uv; break; case 1: G.lmin=sv; break; case 2: G.lmax=(G.lmin<0)?sv:(int)uv; break; case 3: G.pmin=sv; break; case 4: G.pmax=(G.pmin<0)?sv:(int)uv; break;
                case 5: G.uexp=(int)(uv&0xF); if(G.uexp>7)G.uexp-=16; break; case 6: G.unit=uv; break; case 7: G.rsize=(int)uv; break;
                case 8: G.rid=(int)(uv&0xFF); L->uses_ids=1; break; case 9: G.rcount=(int)uv; break;
                case 10: if(gsp<4)GS[gsp++]=G; break; case 11: if(gsp>0)G=GS[--gsp]; break; }
            continue; }
        if(type==2){
            if(tag==0){ if(nu<32)usage[nu++]=(sz==4)?uv:((G.page<<16)|uv); }
            else if(tag==1){ umin=(sz==4)?uv:((G.page<<16)|uv); hasmin=1; }
            else if(tag==2){ umax=(sz==4)?uv:((G.page<<16)|uv); hasmax=1; }
            continue; }
        if(type!=0)continue;
        if(tag==10){
            u32 cu=nu?usage[0]:(hasmin?umin:0);
            if(csp<16)coll[csp++]=cu; else { cov++; g_tpp_depth_err++; }
            if(cu==0x000D0022u&&csp>=2){ int app=0; for(int k=0;k<csp-1;k++) if(coll[k]==0x000D0005u)app=1; if(app&&L->nfing<TPF_MAX)finger=L->nfing++; else finger=-1; }
            nu=0; hasmin=hasmax=0; continue; }
        if(tag==12){ if(cov>0)cov--; else if(csp>0){ if(coll[csp-1]==0x000D0022u)finger=-1; csp--; } else g_tpp_depth_err++; nu=0; hasmin=hasmax=0; continue; }
        if(tag==8||tag==11){
            int feat=(tag==11); u32 app=csp?coll[0]:0; int* bits=feat?fbits:inbits; int rid=G.rid;
            if(G.rsize<=0||G.rsize>32||G.rcount<0||G.rcount>4096){
                g_tpp_skip++; if(g_tpp_skip_off<0)g_tpp_skip_off=at;
                if(G.rsize>0&&G.rsize<=4096&&G.rcount>0&&G.rcount<=65536){ long long nb=(long long)bits[rid]+(long long)G.rsize*G.rcount; if(nb>(1<<20))nb=1<<20; bits[rid]=(int)nb; if(!feat)L->in_len[rid]=(bits[rid]+7)/8; else L->feat_len[rid]=(bits[rid]+7)/8; }
                nu=0; hasmin=hasmax=0; continue; }
            for(int k=0;k<G.rcount;k++){
                u32 u=0; if(nu)u=usage[k<nu?k:nu-1]; else if(hasmin){ u=umin+(u32)k; if(hasmax&&u>umax)u=umax; }
                tpfld f; f.off=bits[rid]+k*G.rsize; f.size=G.rsize; f.sgn=G.lmin<0; f.ok=1;
                if(uv&1)continue;
                if(!feat&&app==0x000D0005u){
                    L->ptp=1; L->ptp_rid=rid;
                    if(finger>=0){ if(u==0x000D0042u)L->f[finger].tip=f; else if(u==0x000D0047u)L->f[finger].conf=f; else if(u==0x000D0051u)L->f[finger].cid=f;
                        else if(u==0x00010030u){ L->f[finger].x=f; if(finger==0){ L->xmin=G.lmin; L->xmax=G.lmax; L->xum=tp_unit_um(G.unit,G.uexp,G.pmax-G.pmin); } }
                        else if(u==0x00010031u){ L->f[finger].y=f; if(finger==0){ L->ymin=G.lmin; L->ymax=G.lmax; L->yum=tp_unit_um(G.unit,G.uexp,G.pmax-G.pmin); } } }
                    else { if(u==0x000D0054u)L->cc=f; else if(u==0x000D0056u)L->scan=f; else if(u>=0x00090001u&&u<=0x00090003u)L->btn[u-0x00090001u]=f; } }
                else if(feat){
                    if(u==0x000D0052u){ L->mode=f; L->mode_rid=rid; }
                    else if(u==0x000D0055u){ L->ccmax=f; L->ccmax_rid=rid; }
                    else if(u==0x000D0059u){ L->btype=f; L->btype_rid=rid; }
                    else if(u==0x000D0057u){ L->surf=f; L->sw_rid=rid; }
                    else if(u==0x000D0058u){ L->bsw=f; L->sw_rid=rid; } }
                else if(app==0x00010002u){
                    L->mouse=1; L->m_rid=rid;
                    if(u>=0x00090001u&&u<=0x00090003u)L->mbtn[u-0x00090001u]=f; else if(u==0x00010030u)L->mx=f; else if(u==0x00010031u)L->my=f; else if(u==0x00010038u)L->mw=f; } }
            bits[rid]+=G.rsize*G.rcount; if(bits[rid]>(1<<20))bits[rid]=1<<20; if(!feat)L->in_len[rid]=(bits[rid]+7)/8; else L->feat_len[rid]=(bits[rid]+7)/8;
            nu=0; hasmin=hasmax=0; continue; }
        if(tag==9){ nu=0; hasmin=hasmax=0; continue; } }
    if(L->ptp){ int good=0; for(int k=0;k<L->nfing;k++) if(L->f[k].tip.ok&&L->f[k].x.ok&&L->f[k].y.ok)good++; if(!good)L->ptp=0; if(L->xmax<=L->xmin||L->ymax<=L->ymin)L->ptp=0; }
    return 0; }
int tp_bits(const u8* p,int plen,tpfld f){ if(!f.ok||f.size<=0||f.size>32)return 0; u32 v=0; for(int b=0;b<f.size;b++){ int bit=f.off+b; if((bit>>3)>=plen)return 0; if(p[bit>>3]&(1u<<(bit&7)))v|=1u<<b; }
    if(f.sgn&&f.size<32&&(v&(1u<<(f.size-1))))v|=~0u<<f.size; return (int)v; }
void tp_setbits(u8* p,int plen,tpfld f,u32 val){ for(int b=0;b<f.size;b++){ int bit=f.off+b; if((bit>>3)>=plen)return; if(val&(1u<<b))p[bit>>3]|=(u8)(1u<<(bit&7)); else p[bit>>3]&=(u8)~(1u<<(bit&7)); } }
